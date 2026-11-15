// SquachWatch-CYD -- HUMAN SENSING: the engine. See include/sense_cyd.h.
//
// While the HUMAN SENSING screen is up, main.cpp pauses the detector the way a
// WiFi update does (DetectionEngine::startUpdateRadio), calls senseCydStart()
// with a saved network, senseCydTick() every pass, and senseCydStop() on the
// way out. In between, this:
//   - joins the network and pings the router 50 times a second, so the
//     router's replies are a steady stream of frames from one place;
//   - takes the channel state (CSI, the legacy long training field: 64 tones
//     of int8 I/Q) of every frame from the router;
//   - runs the motion model (csi_motion_model.*, ESP32Marauder's) on it, and
//     the SPECTRUM.EXE signal below;
//   - hands the screen a status ten times a second and the router's own
//     numbers once a second (SenseStore, include/sense_store.h).
// Notes go to the console as "[sense] ..."; nothing is saved.
//
// Console (SENSE <cmd>): cal | win <ms> | caltime <ms> | hold <ms> | pingms <ms> | rates | stat | help
#if defined(SENSE_CYD_APP)
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <new>
#include "sense_cyd.h"
#include "sense_store.h"
#include "settings.h"
#include "csi_motion_model.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip4.h"

static const uint8_t  kQueueSize   = 32;
static const uint16_t kMaxCsiBytes = 128;   // L-LTF only: 64 tones x int8 imag, real

struct Sample { uint16_t length; int8_t rssi; bool fwi; uint32_t at; int8_t iq[kMaxCsiBytes]; };
// The queue and the motion model (~6.6 KB) come from the heap while the
// screen is up and go back when it closes (senseCydStart / senseCydStop).
// As fixed RAM they cost every board that much heap all the time, sensing or
// not, and from v1.42.0 on CYDs short of it heard nothing (2026-11-14).
static Sample* volatile  s_queue = nullptr;
static volatile uint8_t  s_wr = 0, s_rd = 0;
static volatile uint32_t s_seen = 0, s_accepted = 0, s_dropped = 0;
static uint8_t  s_apMac[6];
static volatile bool s_haveAp = false;

static char     s_ssid[33], s_pass[65];
static uint8_t  s_mode = SENSE_MODE_OFF;
static uint16_t s_chan = 0;
// A 15 s calibration. Promiscuous mode stays off: the detector's promiscuous
// callback is still installed and would be fed.
static uint32_t s_windowMs = 1000, s_calMs = 15000, s_holdMs = 15000, s_pingMs = 20;
static bool     s_csiOn = false, s_running = false;
static uint32_t s_joinStartedMs = 0, s_lastLineMs = 0, s_lastFusionMs = 0, s_lastMotionMs = 0;
static uint32_t s_staSince = 0, s_lastReadingAt = 0;
static bool     s_presence = false;
static volatile uint32_t s_pingOk = 0, s_pingTimeout = 0;
static volatile uint16_t s_discReason = 0;
// Sign-in attempts, for the bench (SENSE joins): ours (startJoin) and every
// disconnect the WiFi driver reports, which is one per failed attempt, the
// library's own instant retries included.
static uint32_t s_tryCount = 0;
static volatile uint32_t s_discCount = 0;
// Refused, the board waits before it knocks again, longer each time: 20 s,
// 40, 80, 160, then every 5 minutes. Knocking straight away (the WiFi
// library's own auto-reconnect, 19 attempts a minute on the bench with a
// wrong password) gets a board blocked by many routers for a few minutes,
// which a tester saw as code 2 then 202 until the board sat unplugged.
static uint8_t  s_failStreak = 0;
static uint32_t s_retryAt = 0;
static bool     s_waiting = false;

static CsiMotionModel* s_model = nullptr;
static uint32_t s_modelAccepted = 0;
static uint8_t  s_score = 0, s_rel = 0, s_state = SENSE_STATE_CAL;
static uint16_t s_rate = 0, s_live = 0;
static int8_t   s_rssi = -127;
static uint8_t  s_motionVotes = 0, s_stillVotes = 0;

static void note(const char* fmt, ...) {
  char b[220];
  va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  Serial.printf("[sense] %s\n", b);
}

// Privacy Mode: the network's name and the router's MAC and IP stay off the console.
static bool priv() { return Settings::privacyMode(); }

// ---- SPECTRUM.EXE -------------------------------------------------------------
// How fast the channel's shape changes, 0-2.5 Hz, for the screen's spectrum and
// waterfall: the strongest rhythm shared across the tones, found online with
// Oja's rule (one 50-tone direction, nudged every frame), so it needs a few KB,
// allocated only while HUMAN SENSING is up:
//   per CSI sample: the 50 data tones' amplitudes, normalised by their total
//   every 100 ms:   the frame's mean, minus a ~5 s baseline -> d; c = d . v,
//                   v += eta * c * (d - c * v), then v renormalised; c goes in a ring
//   every 100 ms:   a 128-point FFT of the last 12.8 s (Hann, mean removed),
//                   bins 0-31 = 0-2.42 Hz, in dB over the room's recent level -> one row
// A row every frame (100 ms), drawn 1 px tall: the waterfall slides a pixel
// at a time instead of jumping two every 300 ms. 108 rows fill the same 108 px.
static const int kSpTones = 50, kSpN = 128, kSpBins = 32, kSpRows = 108;
struct SpecState {
  float acc[kSpTones]; int accN;
  float last[kSpTones]; bool haveLast;
  float base[kSpTones]; int baseN;
  float v[kSpTones]; float pw;
  float ring[kSpN]; uint32_t ringTotal;
  float re[kSpN], im[kSpN];
  float ref; int refN;
  uint8_t rows[kSpRows][kSpBins]; int rowHead, rowCount;
  int peak; uint32_t frameAt; int sinceRow;
};
static SpecState* s_sp = nullptr;

static void specReset() {
  if (!s_sp) return;
  SpecState& p = *s_sp;
  memset(&p, 0, sizeof p);
  for (int i = 0; i < kSpTones; i++) p.v[i] = (i & 1 ? -1.0f : 1.0f) / sqrtf((float)kSpTones);
  p.peak = -1; p.frameAt = millis();
}
static void specBegin() {
  if (!s_sp) s_sp = (SpecState*)malloc(sizeof(SpecState));
  if (s_sp) specReset();
  note("spectrum: %s, %u bytes", s_sp ? "on" : "NO MEMORY", (unsigned)sizeof(SpecState));
}
static void specEnd() { free(s_sp); s_sp = nullptr; }

static void specAdd(const int8_t* iq, uint16_t len) {
  if (!s_sp || len < 128) return;
  float mag[kSpTones], total = 0; int n = 0;
  for (int k = 0; k < 64; k++) {
    if (!((k >= 2 && k <= 26) || (k >= 38 && k <= 62))) continue;   // the data tones, +-2..26
    const float im = iq[k * 2], re = iq[k * 2 + 1];
    mag[n] = sqrtf(re * re + im * im); total += mag[n]; n++;
  }
  if (total < 1.0f) return;
  const float sc = (float)n / total;
  for (int i = 0; i < n; i++) s_sp->acc[i] += mag[i] * sc;
  s_sp->accN++;
}

static void fft128(float* re, float* im) {
  const int n = kSpN;
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const float ang = -6.2831853f / len;
    for (int k = 0; k < len / 2; k++) {
      const float wr = cosf(ang * k), wi = sinf(ang * k);
      for (int i = 0; i < n; i += len) {
        const int a = i + k, b = a + len / 2;
        const float vr = re[b] * wr - im[b] * wi, vi = re[b] * wi + im[b] * wr;
        re[b] = re[a] - vr; im[b] = im[a] - vi; re[a] += vr; im[a] += vi;
      }
    }
  }
}

static void specTick(uint32_t now) {
  if (!s_sp) return;
  SpecState& p = *s_sp;
  if ((int32_t)(now - p.frameAt) < 100) return;
  p.frameAt = now - ((now - p.frameAt) % 100);
  float x[kSpTones];
  if (p.accN) { for (int i = 0; i < kSpTones; i++) x[i] = p.acc[i] / p.accN; memcpy(p.last, x, sizeof x); p.haveLast = true; }
  else if (p.haveLast) memcpy(x, p.last, sizeof x);
  else return;
  memset(p.acc, 0, sizeof p.acc); p.accN = 0;
  // baseline: a plain mean for the first 5 s, then a ~5 s EMA
  p.baseN = p.baseN < 50 ? p.baseN + 1 : 50;
  const float a = 1.0f / p.baseN;
  float d[kSpTones], c = 0, dd = 0;
  for (int i = 0; i < kSpTones; i++) { p.base[i] += (x[i] - p.base[i]) * a; d[i] = x[i] - p.base[i]; c += d[i] * p.v[i]; dd += d[i] * d[i]; }
  // Oja's rule, its step scaled by the signal's own power so it neither stalls
  // nor runs away: ~1 % of the way per frame, so the direction settles in ~10 s.
  p.pw += (dd - p.pw) * 0.02f;
  const float eta = p.pw > 1e-9f ? 0.01f / p.pw : 0.0f;
  float nrm = 0;
  for (int i = 0; i < kSpTones; i++) { p.v[i] += eta * c * (d[i] - c * p.v[i]); nrm += p.v[i] * p.v[i]; }
  nrm = sqrtf(nrm);
  if (nrm > 1e-6f) for (int i = 0; i < kSpTones; i++) p.v[i] /= nrm;
  p.ring[p.ringTotal % kSpN] = c; p.ringTotal++;
  if (++p.sinceRow < 1 || p.ringTotal < (uint32_t)kSpN) return;
  p.sinceRow = 0;
  float mean = 0;
  for (int i = 0; i < kSpN; i++) mean += p.ring[(p.ringTotal + i) % kSpN];
  mean /= kSpN;
  for (int i = 0; i < kSpN; i++) {
    p.re[i] = (p.ring[(p.ringTotal + i) % kSpN] - mean) * (0.5f - 0.5f * cosf(6.2831853f * i / (kSpN - 1)));
    p.im[i] = 0;
  }
  fft128(p.re, p.im);
  // Each bin in dB against a slow (~30 s) average of the rows' median bin, so a
  // still room settles dark and a rhythm, or movement, stands out of it. (Held
  // against the calibration instead, the reference came out far too low -- the
  // direction is still settling then -- and the waterfall sat saturated.)
  // Bins 0-1 (below 0.16 Hz) are the channel's slow drift, not the room, and
  // are left out of the reference and the picture.
  uint8_t* row = p.rows[p.rowHead];
  float db[kSpBins], srt[kSpBins];
  for (int b = 0; b < kSpBins; b++) db[b] = 10.0f * log10f(p.re[b] * p.re[b] + p.im[b] * p.im[b] + 1e-12f);
  int ns = 0;
  for (int b = 2; b < kSpBins; b++) {   // the median of bins 2-31, by insertion
    int j = ns++; const float v = db[b];
    while (j > 0 && srt[j - 1] > v) { srt[j] = srt[j - 1]; j--; }
    srt[j] = v;
  }
  const float med = srt[ns / 2];
  p.ref = p.refN ? p.ref + (med - p.ref) * (p.refN < 90 ? 1.0f / (p.refN + 1) : 0.0033f) : med;
  p.refN++;
  int best = -1; float bestOver = 8.0f;   // a peak must stand 8 dB over the room's median
  for (int b = 0; b < kSpBins; b++) {
    const float over = db[b] - p.ref;
    const float v = (over - 4.0f) * (255.0f / 24.0f);
    row[b] = b < 2 ? 0 : (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    if (b >= 2 && over > bestOver) { bestOver = over; best = b; }
  }
  p.peak = best;
  p.rowHead = (p.rowHead + 1) % kSpRows;
  if (p.rowCount < kSpRows) p.rowCount++;
}

int senseCydSpecRows() { return s_sp ? s_sp->rowCount : 0; }
int senseCydSpecRowsMax() { return kSpRows; }
const uint8_t* senseCydSpecRow(int r) {
  if (!s_sp || r < 0 || r >= s_sp->rowCount) return nullptr;
  return s_sp->rows[(s_sp->rowHead - 1 - r + kSpRows * 2) % kSpRows];
}
int senseCydSpecPeak() { return s_sp ? s_sp->peak : -1; }
float senseCydSpecBinHz() { return 10.0f / kSpN; }

// ---- CSI: only the router's frames are queued ------------------------------
static void IRAM_ATTR csiCb(void* ctx, wifi_csi_info_t* info) {
  (void)ctx;
  Sample* const q = s_queue;
  if (!q || !info || !info->buf || info->len < 16) return;
  s_seen++;
  if (!s_haveAp || memcmp(info->mac, s_apMac, 6) != 0) return;
  const uint8_t next = (uint8_t)((s_wr + 1) % kQueueSize);
  if (next == s_rd) { s_dropped++; return; }
  Sample& s = q[s_wr];
  s.length = info->len > kMaxCsiBytes ? kMaxCsiBytes : info->len;
  s.rssi = info->rx_ctrl.rssi;
  s.fwi = info->first_word_invalid;
  s.at = millis();
  memcpy(s.iq, info->buf, s.length);
  s_wr = next;
  s_accepted++;
}

static bool csiStart() {
  wifi_csi_config_t c = {};
  c.lltf_en = true;            // the legacy long training field only: 64 tones, every frame alike
  c.htltf_en = false;
  c.stbc_htltf2_en = false;
  c.ltf_merge_en = false;
  c.channel_filter_en = false; // keep neighbouring tones independent
  c.manu_scale = false;
  c.shift = 0;
  esp_err_t e;
  if ((e = esp_wifi_set_csi_rx_cb(csiCb, nullptr)) != ESP_OK) { note("csi cb: %s", esp_err_to_name(e)); return false; }
  if ((e = esp_wifi_set_csi_config(&c)) != ESP_OK) { note("csi config: %s", esp_err_to_name(e)); return false; }
  if ((e = esp_wifi_set_csi(true)) != ESP_OK) { note("csi on: %s", esp_err_to_name(e)); return false; }
  s_csiOn = true;
  return true;
}

static void csiStop() {
  if (!s_csiOn) return;
  esp_wifi_set_csi(false);
  esp_wifi_set_csi_rx_cb(nullptr, nullptr);
  s_csiOn = false;
}

// ---- The pings: the router's answers are what the radio measures ------------
// Sent from the loop on a raw socket, one every s_pingMs, the replies drained
// as they come. No task: esp_ping ran one, whose 3 KB stack had to come out of
// the heap in one piece. Made again on every join it broke the heap up until
// it could not start (the soak board, 27 rejoins); kept for the visit at 2 KB
// it overflowed after 29 minutes (a PANIC with the heap checker in "ping"; it
// peaks just over 2 KB when the link has trouble); kept at 3 KB the heap was
// broken up by the WiFi reconnects instead, and it still could not start
// (2026-11-09). A socket costs a few hundred bytes and needs no big block.
static int      s_icmp = -1;
static uint32_t s_pingGw = 0, s_pingAt = 0, s_pingSent = 0;
static uint16_t s_pingSeq = 0;
static const uint16_t PING_ID = 0x5351;   // "SQ"

static void stopPing() {
  if (s_icmp >= 0) { lwip_close(s_icmp); s_icmp = -1; }
}

static void startPing() {
  const IPAddress gw = WiFi.gatewayIP();
  s_pingGw = (uint32_t)gw;
  if (s_icmp < 0) {
    s_icmp = lwip_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (s_icmp < 0) { note("ping socket failed (heap %lu)", (unsigned long)ESP.getFreeHeap()); return; }
    const int nb = 1;
    lwip_ioctl(s_icmp, FIONBIO, (void*)&nb);
  }
  s_pingAt = millis();
  if (priv()) note("pinging the router every %lu ms", (unsigned long)s_pingMs);
  else note("pinging the router %s every %lu ms", gw.toString().c_str(), (unsigned long)s_pingMs);
}

// Every loop pass while joined: at most one echo out, and the answers in.
static void pingTick(uint32_t now) {
  if (s_icmp < 0 || !s_pingGw) return;
  // On the 20 ms beat: up to two a pass when a frame ran long (a pass is often
  // over 20 ms, and one a pass held the readings near 35 a second), but never
  // a burst to make up for a stall: more than 100 ms behind starts afresh.
  if ((int32_t)(now - s_pingAt) > 100) s_pingAt = now;
  for (int k = 0; k < 2 && (int32_t)(now - s_pingAt) >= 0; k++) {
    s_pingAt += s_pingMs;
    struct __attribute__((packed)) { icmp_echo_hdr h; uint8_t data; } pkt = {};
    pkt.h.type = ICMP_ECHO; pkt.h.code = 0;
    pkt.h.id = lwip_htons(PING_ID); pkt.h.seqno = lwip_htons(++s_pingSeq);
    pkt.data = 0x51;
    pkt.h.chksum = inet_chksum(&pkt, sizeof pkt);
    sockaddr_in to = {};
    to.sin_family = AF_INET; to.sin_addr.s_addr = s_pingGw;
    if (lwip_sendto(s_icmp, &pkt, sizeof pkt, 0, (sockaddr*)&to, sizeof to) > 0) s_pingSent++;
  }
  uint8_t buf[64];
  for (int i = 0; i < 8; i++) {   // the replies, a few a pass, so their buffers come straight back
    const int n = lwip_recvfrom(s_icmp, buf, sizeof buf, 0, nullptr, nullptr);
    if (n <= 0) break;
    const int ihl = (buf[0] & 0x0f) * 4;
    if (n < ihl + (int)sizeof(icmp_echo_hdr)) continue;
    const icmp_echo_hdr* h = (const icmp_echo_hdr*)(buf + ihl);
    if (h->type == ICMP_ER && h->id == lwip_htons(PING_ID)) s_pingOk++;
  }
  // Lost ones, for the console: everything sent more than one answer behind.
  s_pingTimeout = s_pingSent > s_pingOk + 50 ? s_pingSent - s_pingOk - 50 : 0;
}

static void resetModels() {
  const uint32_t now = millis();
  if (!s_model) return;
  s_model->reset(now, s_calMs, s_windowMs);
  s_modelAccepted = 0; s_wr = s_rd = 0; s_accepted = s_dropped = 0;
  s_score = s_rel = 0; s_rate = s_live = 0;
  s_motionVotes = s_stillVotes = 0; s_state = SENSE_STATE_CAL; s_presence = false;
  s_lastFusionMs = now;
  specReset();
}

static void startJoin() {
  if (!s_ssid[0]) return;
  csiStop(); s_haveAp = false;   // the ping socket stays: see startPing
  esp_wifi_set_promiscuous(false);
  WiFi.disconnect(false, false);   // as the WiFi update does: the radio stays up for the detector
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  // NOT WIFI_PS_NONE: with Bluetooth enabled (NimBLE, its scan merely paused)
  // ESP-IDF aborts in pm_set_sleep_type ("should enable WiFi modem sleep when
  // both WiFi and Bluetooth are enabled"). The default modem sleep stays.
  s_discReason = 0;
  s_tryCount++;
  s_waiting = false;
  // Third try in a row: the radio itself restarted first. On the soak board a
  // join that kept answering "network not found" for minutes joined in a
  // second once the radio was started afresh (2026-11-09).
  if (s_failStreak >= 2) {
    note("restarting the radio before this try");
    esp_wifi_stop();
    delay(30);
    esp_wifi_start();
  }
  // One attempt per startJoin: the library's instant retries are what get a
  // board blocked. (It still retries once on a first connect, by itself.)
  WiFi.setAutoReconnect(false);
  WiFi.begin(s_ssid, s_pass);
  s_mode = SENSE_MODE_JOINING; s_joinStartedMs = millis();
  if (priv()) note("joining a saved network...");
  else note("joining \"%s\"...", s_ssid);
}

// Refused or timed out: stop, and come back after the next wait.
static void joinFailed(uint32_t now, const char* why) {
  const uint32_t wait = s_failStreak >= 4 ? 300000UL : 20000UL << s_failStreak;
  if (s_failStreak < 255) s_failStreak++;
  WiFi.disconnect(false, false);
  s_waiting = true; s_retryAt = now + wait;
  note("join %s (status %d, reason %u, try %u); next try in %lu s", why, (int)WiFi.status(),
       (unsigned)s_discReason, (unsigned)s_failStreak, (unsigned long)(wait / 1000));
}

static void onJoined() {
  const uint8_t* b = WiFi.BSSID();
  if (!b) return;
  s_waiting = false;   // the streak goes back to 0 only once readings flow (tickBody)
  s_staSince = s_lastReadingAt = millis();
  memcpy(s_apMac, b, 6);
  s_chan = (uint16_t)WiFi.channel();
  resetModels();
  s_haveAp = true;
  const bool csi = csiStart();
  // Joined either way: a CSI that will not start is reported once and the
  // screen shows LOW SIGNAL, rather than this running again every pass (each
  // failed esp_wifi_set_csi held the loop up for seconds).
  s_mode = SENSE_MODE_STA;
  startPing();
  if (priv()) note("joined: channel %u rssi %d csi %s heap %lu largest %lu", s_chan, WiFi.RSSI(), csi ? "on" : "FAILED",
                   (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  else note("joined: BSSID %02X:%02X:%02X:%02X:%02X:%02X channel %u rssi %d csi %s heap %lu",
            b[0], b[1], b[2], b[3], b[4], b[5], s_chan, WiFi.RSSI(), csi ? "on" : "FAILED", (unsigned long)ESP.getFreeHeap());
}

// ---- One transmitter, the router: the motion model's score into a state ------
static void fuse(uint32_t now) {
  CsiMotionModel& m = *s_model;
  m.tick(now, s_modelAccepted, 0);
  s_rate = m.samplesPerSecond();
  s_rssi = m.rssi();
  const float lv = m.live() * 100.0f; s_live = (uint16_t)(lv > 1000.0f ? 1000.0f : lv);
  if (!m.calibrated()) { s_state = SENSE_STATE_CAL; s_rel = m.calibrationPercent(now) / 4; s_score = 0; return; }
  if (m.reliability() < 25) { s_state = SENSE_STATE_LOWDATA; s_rel = 0; s_score = 0; return; }
  s_rel = m.reliability(); s_score = m.motionScore();
  if (s_score >= CsiMotionModel::kMotionThreshold) {
    s_motionVotes = s_motionVotes < 5 ? s_motionVotes + 1 : 5; s_stillVotes = 0;
    s_state = s_motionVotes >= 2 ? SENSE_STATE_MOTION : SENSE_STATE_POSSIBLE;
  } else if (s_score >= CsiMotionModel::kPossibleMotionThreshold) {
    s_motionVotes = s_stillVotes = 0; s_state = SENSE_STATE_POSSIBLE;
  } else {
    s_motionVotes = 0; s_stillVotes = s_stillVotes < 3 ? s_stillVotes + 1 : 3;
    if (s_stillVotes >= 2) s_state = SENSE_STATE_STILL;
  }
}

static void sendStatus(uint32_t now) {
  SenseReport r = {};
  r.upMs = now; r.score = s_score; r.state = s_state; r.rel = s_rel;
  r.cal = s_mode == SENSE_MODE_STA ? s_model->calibrationPercent(now) : 0;
  r.mode = s_mode; r.rate = s_rate; r.chan = s_chan; r.live = s_live; r.rssi = s_rssi;
  r.drop = s_dropped; r.presence = s_presence;
  SenseStore::status(r, millis());
}

static void sendRouter(uint32_t now) {
  if (s_mode != SENSE_MODE_STA) return;
  SenseStore::router((uint16_t)((s_apMac[4] << 8) | s_apMac[5]), s_model->rssi(), s_model->samplesPerSecond(),
                     s_model->calibrated() ? s_model->motionScore() : 0, s_model->calibrationPercent(now),
                     s_model->reliability(), millis());
}

// Every 10 s on the console: the numbers a bench run needs, in one line.
static void sendStat(uint32_t now) {
  static uint32_t lastSeen = 0, lastAcc = 0, lastOk = 0, lastAt = 0;
  const uint32_t dt = now - lastAt ? now - lastAt : 1;
  const uint32_t seen = s_seen, acc = s_accepted, ok = s_pingOk;
  if (seen < lastSeen) lastSeen = 0;   // the counts start again when the models reset
  if (acc < lastAcc) lastAcc = 0;
  if (ok < lastOk) lastOk = 0;
  note("stat up %lu  csi_all %lu/s  csi_router %lu/s  ping_ok %lu/s  timeouts %lu  drop %lu  heap %lu min %lu big %lu",
       (unsigned long)(now / 1000),
       (unsigned long)((seen - lastSeen) * 1000UL / dt), (unsigned long)((acc - lastAcc) * 1000UL / dt),
       (unsigned long)((ok - lastOk) * 1000UL / dt), (unsigned long)s_pingTimeout, (unsigned long)s_dropped,
       (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
       (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  lastSeen = seen; lastAcc = acc; lastOk = ok; lastAt = now;
}

static void help() {
  note("SENSE <cmd>: cal | win <ms> | caltime <ms> | hold <ms> | pingms <ms> | rates | stat | joins | wrongpass | help");
}

static void command(char* line) {
  while (*line == ' ') line++;
  if (!*line) return;
  char* sp = strchr(line, ' ');
  char* arg = nullptr;
  if (sp) { *sp = 0; arg = sp + 1; while (*arg == ' ') arg++; }
  if (!strcasecmp(line, "cal")) {
    resetModels(); note("recalibrating: keep the room still for %lu s", (unsigned long)(s_calMs / 1000));
  } else if (!strcasecmp(line, "win")) {
    const long v = arg ? atol(arg) : 0;
    if (v < 200 || v > 5000) { note("win 200..5000 ms (now %lu)", (unsigned long)s_windowMs); return; }
    s_windowMs = (uint32_t)v; resetModels();
  } else if (!strcasecmp(line, "caltime")) {
    const long v = arg ? atol(arg) : 0;
    if (v < 5000 || v > 120000) { note("caltime 5000..120000 ms (now %lu)", (unsigned long)s_calMs); return; }
    s_calMs = (uint32_t)v; resetModels();
  } else if (!strcasecmp(line, "hold")) {
    const long v = arg ? atol(arg) : -1;
    if (v < 0 || v > 120000) { note("hold 0..120000 ms (now %lu)", (unsigned long)s_holdMs); return; }
    s_holdMs = (uint32_t)v; note("presence hold %lu ms", (unsigned long)s_holdMs);
  } else if (!strcasecmp(line, "pingms")) {
    const long v = arg ? atol(arg) : 0;
    if (v < 5 || v > 1000) { note("pingms 5..1000 (now %lu)", (unsigned long)s_pingMs); return; }
    s_pingMs = (uint32_t)v; if (s_mode == SENSE_MODE_STA) startPing();
  } else if (!strcasecmp(line, "rates")) {
    if (!s_model) { note("HUMAN SENSING is not open"); return; }
    note("router %u/s rssi %d tones %u cal %u%% rel %u score %u (a%u p%u c%u) live %.2f",
         s_model->samplesPerSecond(), s_model->rssi(), (unsigned)s_model->carriers(), s_model->calibrationPercent(millis()),
         s_model->reliability(), s_model->motionScore(), s_model->amplitudeScore(), s_model->phaseScore(), s_model->correlationScore(),
         s_model->live());
    sendStat(millis());
  } else if (!strcasecmp(line, "stat")) {
    sendStat(millis());
  } else if (!strcasecmp(line, "joins")) {
    note("joins %lu  radio drops %lu  last reason %u", (unsigned long)s_tryCount, (unsigned long)s_discCount, (unsigned)s_discReason);
  } else if (!strcasecmp(line, "wrongpass")) {
    // Bench only: join with a password that cannot be right, to watch how
    // often a refused board knocks. The real one is forgotten until the
    // screen is opened again.
    strncpy(s_pass, "squachwatch-wrong-password", sizeof s_pass - 1);
    note("bench: joining with a wrong password");
    startJoin();
  } else if (!strcasecmp(line, "help") || !strcmp(line, "?")) {
    help();
  } else {
    note("unknown: %s", line); help();
  }
}

// One pass of the engine: the join, the readings into the models, the screen's numbers.
static void tickBody(uint32_t now) {
  if (!s_model || !s_queue) return;   // no memory at the start: nothing to run
  if (s_mode == SENSE_MODE_JOINING) {
    const uint32_t jt = millis() - s_joinStartedMs;
    const wl_status_t st = WiFi.status();
    if (s_waiting) { if ((int32_t)(now - s_retryAt) >= 0) startJoin(); }
    else if (st == WL_CONNECTED) onJoined();
    // The router said no, or the network is not there: done after the
    // library's one retry has had its few seconds.
    // (Reason 8 is our own WiFi.disconnect() in startJoin, arriving late: not a refusal.)
    else if (s_discReason && s_discReason != 8 && jt > 12000 && (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL || st == WL_DISCONNECTED || st == WL_IDLE_STATUS))
      joinFailed(now, "refused");
    else if (jt > 20000) joinFailed(now, "timed out");
  } else if (s_mode == SENSE_MODE_STA && WiFi.status() != WL_CONNECTED) {
    // The first loss rejoins at once; a board that keeps losing it backs off.
    if (!s_failStreak) { note("link lost (reason %u), rejoining", (unsigned)s_discReason); startJoin(); }
    else { s_mode = SENSE_MODE_JOINING; joinFailed(now, "lost"); }
  } else if (s_mode == SENSE_MODE_STA && s_csiOn && (int32_t)(now - s_lastReadingAt) > 8000) {
    // Joined but deaf: no reading from the router for 8 s (a router that
    // restarted looks the same). Join again, after the back-off's wait.
    note("joined but no readings for 8 s (%lu ping timeouts)", (unsigned long)s_pingTimeout);
    s_mode = SENSE_MODE_JOINING;
    joinFailed(now, "deaf");
  } else if (s_mode == SENSE_MODE_STA && s_failStreak && now - s_staSince > 30000 && (int32_t)(now - s_lastReadingAt) < 2000) {
    s_failStreak = 0;   // 30 s of readings: the next trouble starts the waits afresh
  }
  if (s_mode == SENSE_MODE_STA) pingTick(now);

  while (s_rd != s_wr) {
    Sample& s = s_queue[s_rd];
    s_model->addSample(s.iq, s.length, s.rssi, s.at, s.fwi, CsiMotionModel::SampleEncoding::SIGNED_8_IMAG_REAL);
    specAdd(s.iq, s.length);
    s_modelAccepted++;
    s_lastReadingAt = now;
    s_rd = (uint8_t)((s_rd + 1) % kQueueSize);
  }
  if (s_mode == SENSE_MODE_STA) specTick(now);

  if (now - s_lastFusionMs >= 1000) {
    s_lastFusionMs = now;
    fuse(now); sendRouter(now);
    static uint8_t statTick = 0;
    if (++statTick >= 10) { statTick = 0; sendStat(now); }
    if (s_state == SENSE_STATE_MOTION) { s_presence = true; s_lastMotionMs = now; }
    else if (s_presence && (now - s_lastMotionMs > s_holdMs || s_state == SENSE_STATE_CAL || s_state == SENSE_STATE_LOWDATA)) s_presence = false;
  }
  if (now - s_lastLineMs >= 1000 / SENSE_LINE_HZ) {
    s_lastLineMs = now;
    if (s_model->calibrated()) { const float lv = s_model->live() * 100.0f; s_live = (uint16_t)(lv > 1000.0f ? 1000.0f : lv); }
    sendStatus(now);
  }
}

static void onWifiEvent(WiFiEvent_t e, WiFiEventInfo_t info) {
  if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) { s_discReason = info.wifi_sta_disconnected.reason; s_discCount++; }
}

// ---- The app's side (include/sense_cyd.h) -----------------------------------
// The screen's RECAL and the console's SENSE <cmd> land here.
void senseCydCommand(const char* cmd) {
  char b[160];
  strncpy(b, cmd, sizeof b - 1); b[sizeof b - 1] = 0;
  command(b);
}

// The detector has already let go of the radio (startUpdateRadio). ssid
// empty: nothing to join, and the screen says ADD WIFI.
void senseCydStart(const char* ssid, const char* pass) {
  if (s_running) return;
  static wifi_event_id_t evId = 0;
  if (!evId) evId = WiFi.onEvent(onWifiEvent);
  strncpy(s_ssid, ssid ? ssid : "", sizeof s_ssid - 1); s_ssid[sizeof s_ssid - 1] = 0;
  strncpy(s_pass, pass ? pass : "", sizeof s_pass - 1); s_pass[sizeof s_pass - 1] = 0;
  s_running = true;
  if (!s_queue) s_queue = (Sample*)malloc(sizeof(Sample) * kQueueSize);
  if (!s_model) s_model = new (std::nothrow) CsiMotionModel();
  if (!s_queue || !s_model) {
    note("HUMAN SENSING: NO MEMORY (queue %s, model %s, largest block %lu)", s_queue ? "ok" : "no",
         s_model ? "ok" : "no", (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    free(s_queue); s_queue = nullptr;
    delete s_model; s_model = nullptr;
    return;   // running, so the loop does not ask again; tickBody does nothing
  }
  specBegin();
  s_lastLineMs = s_lastFusionMs = millis();
  s_mode = SENSE_MODE_OFF;
  resetModels();
  note("HUMAN SENSING on: heap %lu, largest block %lu", (unsigned long)ESP.getFreeHeap(),
       (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  if (s_ssid[0]) startJoin();
  else note("no saved WiFi network: add one in SETTINGS > WIFI NETWORKS");
}

// Leave the radio the way the WiFi update leaves it: no association, no CSI,
// no ping. The caller hands it back to the detector (stopUpdateRadio).
void senseCydStop() {
  if (!s_running) return;
  stopPing(); csiStop(); s_haveAp = false;
  WiFi.disconnect(false, false);
  WiFi.setAutoReconnect(true);   // the library's default, for whatever uses WiFi next
  stopPing(); s_pingGw = 0;
  s_failStreak = 0; s_waiting = false;
  memset(s_pass, 0, sizeof s_pass);
  s_mode = SENSE_MODE_OFF; s_running = false;
  specEnd();
  // csiStop() took the callback away first, so nothing writes the queue now.
  Sample* q = s_queue; s_queue = nullptr; free(q);
  delete s_model; s_model = nullptr;
  note("HUMAN SENSING off: heap %lu, %lu readings dropped", (unsigned long)ESP.getFreeHeap(), (unsigned long)s_dropped);
}

volatile uint8_t g_senseConsole = 0;
void senseCydTick() { if (s_running) tickBody(millis()); }
bool senseCydRunning() { return s_running; }
const char* senseCydNetwork() { return s_ssid; }
uint16_t senseCydReason() { return s_discReason; }
uint16_t senseCydRetryIn() {
  if (!s_running || s_mode != SENSE_MODE_JOINING || !s_waiting) return 0;
  const int32_t left = (int32_t)(s_retryAt - millis());
  return left > 0 ? (uint16_t)((left + 999) / 1000) : 0;
}
uint8_t senseCydMode() { return s_mode; }
#endif  // SENSE_CYD_APP
