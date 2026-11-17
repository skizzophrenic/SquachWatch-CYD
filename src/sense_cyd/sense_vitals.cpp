// SquachWatch-CYD -- HUMAN SENSING: VITALS, breathing and an experimental heart
// rate from the chest signal. See include/sense_vitals.h.
#include "sense_vitals.h"
#if SENSE_RICH
#include "sense_store.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <esp_heap_caps.h>
static void* bigAlloc(size_t n) {
#if defined(BOARD_HAS_PSRAM)
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
#else
    return calloc(1, n);   // the emulator
#endif
}

namespace Vitals {
void end();
namespace {

// ---- filters ----------------------------------------------------------------
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void design(float fc, bool high) {
        const float w = 6.2831853f * fc / FS, c = cosf(w), al = sinf(w) / (2 * 0.7071f), a0 = 1 + al;
        if (high) { b0 = (1 + c) / 2; b1 = -(1 + c); } else { b0 = (1 - c) / 2; b1 = 1 - c; }
        b2 = b0; a1 = -2 * c; a2 = 1 - al;
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0; z1 = z2 = 0;
    }
    float run(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// ---- state --------------------------------------------------------------------
Metrics  s_m = {};
uint32_t s_next = 0;                 // next $V frame to read
uint32_t s_frames = 0;               // frames processed since the session began
float    s_dRef = 0, s_brRef = 0;     // slow-falling references (see frame())
Biquad   s_dh1, s_dh2;                 // 0.1 Hz high-pass: the drift out, before everything
Biquad   s_lp, s_hp1, s_hp2, s_hlp;
// The rings and the waterfall live in PSRAM (~40 KB the internal heap can't spare).
float   *s_d = nullptr, *s_br = nullptr, *s_hb = nullptr;   // detrended, breathing, heart band
uint8_t* s_mot = nullptr;
uint16_t s_head = 0, s_n = 0;
float    s_brScale = 1, s_hbScale = 1, s_dStd = 0;
// breath detection
int8_t   s_dir = 0; float s_ext = 0, s_extT = 0;
struct Peak { float t, v, amp; };
struct Trough { float t, v; };
Peak     s_pk[64]; uint8_t s_npk = 0;
Trough   s_tr[64]; uint8_t s_ntr = 0;
float    s_stillSince = 0; bool s_wasMotion = false;
// spectrum
float    s_spec[BINS];
uint8_t (*s_wf)[BINS] = nullptr; uint8_t s_wfHead = 0, s_wfN = 0;
float    s_hrHist[4]; uint8_t s_hrN = 0;
bool     s_locked = false; float s_lockT = -100;
float    s_holdFrom = -1;
Event    s_ev[EVN]; uint8_t s_evHead = 0, s_evN = 0;
uint32_t s_sessionMs = 0, s_frameAt = 0;
bool     s_any = false;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline uint16_t idx(uint16_t k) { return (uint16_t)((s_head + HN - 1 - k) % HN); }   // k frames ago
inline float tNow() { return s_frames / FS; }

void addEvent(Ev k, uint8_t val) {
    s_ev[s_evHead] = { tNow(), k, val };
    s_evHead = (uint8_t)((s_evHead + 1) % EVN);
    if (s_evN < EVN) s_evN++;
}

float medianOf(float* v, int n) {
    for (int i = 1; i < n; i++) { const float x = v[i]; int j = i; while (j && v[j - 1] > x) { v[j] = v[j - 1]; j--; } v[j] = x; }
    return n ? v[n / 2] : 0;
}

// ---- FFT ------------------------------------------------------------------------
const int NFFT = 512;
float *s_re = nullptr, *s_im = nullptr, *s_mag = nullptr, *s_scr = nullptr;
bool ready() {
    if (s_d) return true;
    s_d = (float*)bigAlloc(sizeof(float) * HN); s_br = (float*)bigAlloc(sizeof(float) * HN); s_hb = (float*)bigAlloc(sizeof(float) * HN);
    s_mot = (uint8_t*)bigAlloc(HN); s_wf = (uint8_t(*)[BINS])bigAlloc((size_t)ROWS * BINS);
    s_re = (float*)bigAlloc(sizeof(float) * NFFT); s_im = (float*)bigAlloc(sizeof(float) * NFFT); s_mag = (float*)bigAlloc(sizeof(float) * NFFT / 2);
    s_scr = (float*)bigAlloc(sizeof(float) * 1200);
    if (s_d && s_br && s_hb && s_mot && s_wf && s_re && s_im && s_mag && s_scr) return true;
    end();
    return false;
}
void fft(float* re, float* im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float a = -6.2831853f / len, wr = cosf(a), wi = sinf(a);
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                const int p = i + k, q = p + len / 2;
                const float vr = re[q] * cr - im[q] * ci, vi = re[q] * ci + im[q] * cr;
                re[q] = re[p] - vr; im[q] = im[p] - vi; re[p] += vr; im[p] += vi;
                const float t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}
float interp(int k) {   // parabolic peak position, in bins
    if (k <= 0 || k >= NFFT / 2 - 1) return (float)k;
    const float a = s_mag[k - 1], b = s_mag[k], c = s_mag[k + 1], d = a - 2 * b + c;
    // Only a real local peak is refined, and by at most half a bin: on a
    // flat or rising stretch the parabola's vertex flew off (a breathing peak
    // of -1.33 Hz on the bench, 2026-11-08), and the screen drew a callout to it.
    if (b < a || b < c || d > -1e-12f) return (float)k;
    const float off = 0.5f * (a - c) / d;
    return k + (off > 0.5f ? 0.5f : (off < -0.5f ? -0.5f : off));
}

void spectrumStep() {
    if (s_n < NFFT) return;
    for (int i = 0; i < NFFT; i++) {
        const float w = 0.5f - 0.5f * cosf(6.2831853f * i / (NFFT - 1));
        s_re[i] = s_d[idx((uint16_t)(NFFT - 1 - i))] * w; s_im[i] = 0;
    }
    fft(s_re, s_im, NFFT);
    for (int k = 0; k < NFFT / 2; k++) s_mag[k] = sqrtf(s_re[k] * s_re[k] + s_im[k] * s_im[k]);
    // noise floor: the median from 2.5 to 7.8 Hz, where nothing alive is
    float tmp[160]; int nt = 0; for (int k = 64; k < 200; k++) tmp[nt++] = s_mag[k];
    const float floorM = medianOf(tmp, nt) + 1e-9f;
    // breathing peak, 0.12-0.62 Hz
    int bk = 3; for (int k = 3; k <= 16; k++) if (s_mag[k] > s_mag[bk]) bk = k;
    const float bkf = interp(bk);
    s_m.peakHz = bkf * FS / NFFT;
    s_m.snr = s_mag[bk] > 0 ? (int)clampf(lroundf(20 * log10f(s_mag[bk] / floorM)), -99, 99) : 0;
    // the screen's spectrum and a waterfall row
    for (int b = 0; b < BINS; b++) {
        // the noise floor reads as black: only what stands out of it glows
        const float v = clampf((log10f(1 + s_mag[b] / floorM) - 0.45f) / 1.75f, 0, 1);
        s_spec[b] = v; s_wf[s_wfHead][b] = (uint8_t)(v * 255);
    }
    s_wfHead = (uint8_t)((s_wfHead + 1) % ROWS); if (s_wfN < ROWS) s_wfN++;
    // heart band, 0.82-2.19 Hz, skipping the breathing's harmonics
    float band[40]; int nb = 0; int hk = -1;
    for (int k = 21; k <= 56; k++) {
        bool harm = false;
        for (int h = 2; h <= 9; h++) if (fabsf(k - h * bkf) <= 1.3f) harm = true;
        if (harm) continue;
        band[nb++] = s_mag[k];
        if (hk < 0 || s_mag[k] > s_mag[hk]) hk = k;
    }
    const float bandMed = nb ? medianOf(band, nb) + 1e-9f : 1;
    if (hk < 0 || s_m.motion) { s_m.conf += (0 - s_m.conf) * 0.3f; return; }
    const float prom = s_mag[hk] / bandMed;
    const float cand = interp(hk) * FS / NFFT * 60;
    memmove(s_hrHist + 1, s_hrHist, sizeof(float) * 3); s_hrHist[0] = cand; if (s_hrN < 4) s_hrN++;
    float lo = 1e9f, hi = -1e9f; for (int i = 0; i < s_hrN; i++) { lo = fminf(lo, s_hrHist[i]); hi = fmaxf(hi, s_hrHist[i]); }
    const bool agree = s_hrN >= 3 && hi - lo <= 6;
    const float target = 0.95f * clampf((prom - 2.0f) / 3.0f, 0, 1) * clampf((s_m.still - 8) / 20.0f, 0, 1) * (agree ? 1.0f : 0.4f);
    s_m.conf += (target - s_m.conf) * 0.25f;
    if (s_m.hr <= 0) s_m.hr = cand; else if (agree) s_m.hr += (cand - s_m.hr) * 0.3f;
}

// ---- one frame --------------------------------------------------------------------
void frame(float x, uint8_t q, bool radioMotion) {
    // A real high-pass (two at 0.1 Hz), not the 6 s mean it replaced: on the
    // bench the mean let slow drift through, and the spectrum's "breathing
    // peak" sat on the band's bottom edge half the time (2026-11-08).
    const float d = s_dh2.run(s_dh1.run(x));
    const float br = s_lp.run(d);
    const float hb = s_hlp.run(s_hp2.run(s_hp1.run(x)));
    // a movement holds for 1.5 s after its last spike, so one shift in the
    // chair is one MOVED, not a flicker of them
    // Movement is judged on the chest signal alone, against a reference
    // that falls off slowly: the radio's room-motion state read a person
    // breathing half a metre away as MOTION 58 % of the time, and a reference
    // that tracked the last few seconds called a quiet breath hold "moving"
    // (bench, 2026-11-08).
    (void)radioMotion;
    static uint16_t hold = 0;
    const float ref = s_dStd > s_dRef * 0.6f ? s_dStd : s_dRef * 0.6f;
    if (ref > 0 && fabsf(d) > 7 * ref && s_n > 200) hold = (uint16_t)(FS * 1.5f);
    else if (hold) hold--;
    const bool motion = hold > 0;
    s_d[s_head] = d; s_br[s_head] = br; s_hb[s_head] = hb; s_mot[s_head] = motion;
    s_head = (uint16_t)((s_head + 1) % HN); if (s_n < HN) s_n++;
    s_frames++;
    const float t = tNow();
    s_m.q = q;
    // scales, every half second over the last 20 s
    if (s_frames % 10 == 0) {
        // The scales come from the median size over the last 20 s, all of it:
        // they used to skip frames marked as movement, so after one burst the
        // "typical size" collapsed, every wobble counted as movement, and the
        // screen latched on MOVING for good (bench, 2026-11-08).
        const uint16_t w = s_n < 400 ? s_n : 400;
        float* ab = s_scr; float* ah = s_scr + 400; float* ad = s_scr + 800;   // PSRAM scratch
        for (uint16_t k = 0; k < w; k++) { const uint16_t i = idx(k); ab[k] = fabsf(s_br[i]); ah[k] = fabsf(s_hb[i]); ad[k] = fabsf(s_d[i]); }
        // median |x| x 1.4826 is the spread of a normal signal, x 1.11 more for a sine's
        s_brScale = (w ? medianOf(ab, w) : 0) * 1.4826f * 1.6f * 1.11f + 1e-9f;
        s_hbScale = (w ? medianOf(ah, w) : 0) * 1.4826f * 2.2f + 1e-9f;
        s_dStd    = (w ? medianOf(ad, w) : 0) * 1.4826f;
        // slow references: up at once, down ~2 % a second, so a hold reads as
        // the signal going quiet, not as everything being rescaled to it
        s_dRef  = s_dStd > s_dRef ? s_dStd : s_dRef * 0.99f;
        s_brRef = s_brScale > s_brRef ? s_brScale : s_brRef * 0.99f;
    }
    // movement
    if (motion) { s_stillSince = t; s_dir = 0; s_m.conf = 0; }
    if (motion && !s_wasMotion) addEvent(Ev::MOVED, 0);
    s_wasMotion = motion;
    s_m.motion = motion;
    s_m.still = t - s_stillSince;
    // breaths: peaks and troughs of the breathing wave, with hysteresis
    const float hy = 0.5f * s_brScale / 1.6f;
    // No two breaths closer than 60 % of the breathing period the spectrum
    // sees (1.5 s until it sees one): noisy breaths had wobbles counted as
    // breaths, 20 a minute counted against 13.5 in the spectrum (bench).
    const float refractory = (s_m.snr > 10 && s_m.peakHz > 0.1f) ? 0.6f / s_m.peakHz : 1.5f;
    if (!motion) {
        if (s_dir == 0) { s_dir = 1; s_ext = br; s_extT = t; }
        else if (s_dir == 1) {
            if (br > s_ext) { s_ext = br; s_extT = t; }
            else if (br < s_ext - hy && s_npk && s_extT - s_pk[s_npk - 1].t < refractory) {
                s_dir = -1; s_ext = br; s_extT = t;   // a wobble inside the same breath, not a new one
            }
            else if (br < s_ext - hy) {
                const float amp = s_ntr ? s_ext - s_tr[s_ntr - 1].v : 2 * hy;
                if (s_npk == 64) { memmove(s_pk, s_pk + 1, sizeof(Peak) * 63); s_npk--; }
                s_pk[s_npk++] = { s_extT, s_ext, amp };
                s_m.total++;
                // a sigh: a breath half as big again as your usual
                if (s_npk >= 8) { float a[64]; for (int i = 0; i < s_npk; i++) a[i] = s_pk[i].amp; const float med = medianOf(a, s_npk); if (amp > med * 1.45f) { s_m.sighs++; addEvent(Ev::SIGH, 0); } }
                if (s_holdFrom >= 0) { addEvent(Ev::HOLD, (uint8_t)clampf(t - s_holdFrom, 0, 255)); s_holdFrom = -1; }
                s_dir = -1; s_ext = br; s_extT = t;
            }
        } else {
            if (br < s_ext) { s_ext = br; s_extT = t; }
            else if (br > s_ext + hy) {
                if (s_ntr == 64) { memmove(s_tr, s_tr + 1, sizeof(Trough) * 63); s_ntr--; }
                s_tr[s_ntr++] = { s_extT, s_ext };
                s_dir = 1; s_ext = br; s_extT = t;
            }
        }
    }
    s_m.pause = s_npk && !motion ? t - s_pk[s_npk - 1].t : 0;
    if (s_m.pause > 8 && s_holdFrom < 0) s_holdFrom = s_pk[s_npk - 1].t;
    if (s_frames % 6 == 0) spectrumStep();
    // heart lock-on, at most once in 20 s
    const bool lock = s_m.conf > 0.35f;
    if (lock && !s_locked && t - s_lockT > 20) { addEvent(Ev::HEART_LOCK, (uint8_t)clampf(s_m.hr, 0, 255)); s_lockT = t; }
    s_locked = lock;
}

void metricsStep() {
    const float t = tNow();
    s_m.bpm = -1; s_m.rhythm = -1; s_m.depth = -1; s_m.ie = -1;
    float iv[64]; int n = 0;
    for (int i = 1; i < s_npk; i++) if (s_pk[i].t > t - 30) iv[n++] = s_pk[i].t - s_pk[i - 1].t;
    if (n >= 2) {
        float tmp[64]; memcpy(tmp, iv, sizeof(float) * n); const float med = medianOf(tmp, n);
        float sum = 0, sq = 0; int m = 0;
        for (int i = 0; i < n; i++) if (iv[i] < med * 1.7f && iv[i] > med * 0.45f) { sum += iv[i]; sq += iv[i] * iv[i]; m++; }
        if (m >= 2) {
            const float mean = sum / m, sd = sqrtf(fmaxf(0, sq / m - mean * mean));
            s_m.bpm = 60 / mean; s_m.rhythm = (int)clampf(100 * (1 - 2.2f * sd / mean), 0, 100);
        }
    }
    // The number on screen: the spectrum's breathing peak whenever it stands
    // clear of the noise (it held at 13.5 a minute on the bench while counted
    // peaks swung 9-28); the counted rate only when the spectrum has no clear
    // peak. Rhythm, depth, sighs and holds still come from the counted breaths.
    if (!s_m.motion && s_m.snr >= 12 && s_m.peakHz >= 0.12f && s_m.peakHz <= 0.62f) s_m.bpm = s_m.peakHz * 60;
    if (s_npk >= 3) {
        float a[64]; for (int i = 0; i < s_npk; i++) a[i] = s_pk[i].amp;
        const float med = medianOf(a, s_npk) + 1e-9f;
        s_m.depth = (int)lroundf(100 * (s_pk[s_npk - 1].amp + s_pk[s_npk - 2].amp + s_pk[s_npk - 3].amp) / 3 / med);
    }
    float ins = 0, exs = 0; int ni = 0, ne = 0;
    for (int i = s_npk - 6 < 0 ? 0 : s_npk - 6; i < s_npk; i++) {
        for (int j = s_ntr - 1; j >= 0; j--) if (s_tr[j].t < s_pk[i].t) { ins += s_pk[i].t - s_tr[j].t; ni++; break; }
        for (int j = 0; j < s_ntr; j++) if (s_tr[j].t > s_pk[i].t) { exs += s_tr[j].t - s_pk[i].t; ne++; break; }
    }
    if (ni && ne) s_m.ie = (exs / ne) / (ins / ni);
    s_m.breathNow = s_n ? clampf(s_br[idx(0)] / s_brScale, -1.4f, 1.4f) : 0;
    s_m.inhale = s_n > 2 && s_br[idx(0)] > s_br[idx(2)];
    s_m.session = t;
}

}  // namespace

void newSession(uint32_t now) {
    s_frames = 0; s_npk = s_ntr = 0; s_dir = 0; s_stillSince = 0; s_wasMotion = false;
    s_evHead = s_evN = 0; s_holdFrom = -1; s_locked = false; s_lockT = -100; s_hrN = 0;
    const bool keep = s_m.live; const int rate = s_m.rate;
    memset(&s_m, 0, sizeof s_m); s_m.live = keep; s_m.rate = rate; s_m.hr = 0;
    s_sessionMs = now;
}

bool begin(uint32_t now) {
    if (!ready()) { Serial.println("[vitals] no PSRAM for the buffers"); return false; }
    s_dh1.design(0.1f, true); s_dh2.design(0.1f, true); s_lp.design(0.7f, false);
    s_hp1.design(0.8f, true); s_hp2.design(0.8f, true); s_hlp.design(2.2f, false);
    s_head = s_n = 0; s_wfHead = s_wfN = 0; s_brScale = s_hbScale = 1; s_dStd = s_dRef = s_brRef = 0;
    memset(s_spec, 0, sizeof s_spec);
    newSession(now);
    s_frameAt = 0;
    return true;
}

void end() {
    free(s_d); free(s_br); free(s_hb); free(s_mot); free(s_wf); free(s_re); free(s_im); free(s_mag); free(s_scr);
    s_d = s_br = s_hb = nullptr; s_mot = nullptr; s_wf = nullptr; s_re = s_im = s_mag = s_scr = nullptr;
    s_n = 0; s_wfN = 0; s_frameAt = 0;
}

void feed(float c1, uint8_t q) {
    if (!s_d) return;
    frame(c1, q, false);
    s_frameAt = millis() ? millis() : 1;
    s_any = true;
}

uint32_t lastFrameAt() { return s_frameAt; }

void update(uint32_t now) {
    // Signed: feed() stamps millis(), which can be a little past the loop's
    // "now", and unsigned that read as a frame 49 days old (WAITING
    // flickered on the bench, 2026-11-17).
    s_m.live = s_frameAt && (int32_t)(now - s_frameAt) < 1500;
    s_m.rate = SenseStore::fresh(now) ? SenseStore::last().rate : 0;
    if (s_any) { s_any = false; metricsStep(); }
    // One console line every 10 s while VITALS is up, for a bench log.
    static uint32_t statAt = 0;
    if (s_d && now - statAt >= 10000) {
        statAt = now;
        Serial.printf("[vitals] %s  breaths/min %.1f  rhythm %d  depth %d  snr %d dB  peak %.2f Hz  heart %.0f conf %.2f  still %.0f s  q %u  breaths %d  motion %d\n",
                      s_m.live ? "LIVE" : "WAITING", s_m.bpm, s_m.rhythm, s_m.depth, s_m.snr, s_m.peakHz, s_m.hr, s_m.conf,
                      s_m.still, (unsigned)s_m.q, s_m.total, (int)s_m.motion);
    }
}

const Metrics& metrics() { return s_m; }
uint16_t count() { return s_n; }
float breathAt(uint16_t k) { const float sc = s_brRef > s_brScale ? s_brRef : s_brScale; return s_br && k < s_n ? clampf(s_br[idx(k)] / sc, -1.6f, 1.6f) : 0; }
float heartAt(uint16_t k) { return s_hb && k < s_n ? clampf(s_hb[idx(k)] / s_hbScale, -1.6f, 1.6f) : 0; }
bool  motionAt(uint16_t k) { return s_mot && k < s_n && s_mot[idx(k)]; }
const float* spectrum() { return s_spec; }
const uint8_t* waterfallRow(uint8_t r) { static const uint8_t none[BINS] = {}; return s_wf && r < s_wfN ? s_wf[(s_wfHead + ROWS - 1 - r) % ROWS] : none; }
uint8_t waterfallRows() { return s_wfN; }
float binHz(int b) { return b * FS / NFFT; }
uint8_t breathTicks(float* ago, uint8_t cap) {
    uint8_t n = 0; const float t = tNow();
    for (int i = s_npk - 1; i >= 0 && n < cap; i--) ago[n++] = t - s_pk[i].t;
    return n;
}
uint8_t events(Event* out, uint8_t cap) {
    uint8_t n = 0;
    for (uint8_t k = 0; k < s_evN && n < cap; k++) out[n++] = s_ev[(s_evHead + EVN - 1 - k) % EVN];
    return n;
}

}  // namespace Vitals
#endif  // SENSE_RICH
