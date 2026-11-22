// SquachWatch-CM0 -- the ES8389 behind the ESP32 I2S API (i2s_alsa.h).
//
// The firmware's sound.cpp renders each sound to 16 kHz / 16-bit stereo
// and i2s_write()s it 256 frames at a time from its own task; this file
// is that I2S. One snd_pcm stream on the Cardputer's ES8389 card, opened
// in shared mode (the default: no exclusive grab, so it mixes with
// whatever PipeWire is playing). The kernel's ASoC driver owns the codec
// itself -- the ES8311 register init the firmware performs (Wire, 0x18)
// is a no-op on this board -- so the only real work at install is the
// mixer: the speaker switch on and the DAC volumes at the codec's own
// 85%, the same level Espressif's es8311 driver uses in sound.cpp.
//
// Threading: the sound task (freertos_shim.h) is the sole caller of
// start/write/stop; install and set_pin run before the task exists.
// The UI thread never touches the stream. No locks.
#include "i2s_alsa.h"
#include "driver/i2s.h"

#include <alsa/asoundlib.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <string>
#include <sys/auxv.h>
#include <time.h>
#include <vector>

namespace {

snd_pcm_t* s_pcm = nullptr;
std::mutex s_pcmLock;      // install/close vs. the stream; writes are task-only
std::atomic<bool> s_muted{false};
std::atomic<uint64_t> s_frames{0};
int s_rate = 0;             // the installed rate, for re-opening on start

// The muted writes: silence that keeps the stream's timing honest.
thread_local std::vector<int16_t> s_zero;

const char* devName() {
    static std::string name;
    if (name.empty()) {
        // Ignored when privileged (file capabilities -> AT_SECURE), like
        // the other device overrides: the caller's environment must not
        // pick what a capability-raised process opens.
        const char* e = getauxval(AT_SECURE) ? nullptr : getenv("SQUACH_AUDIO_DEV");
        name = (e && *e) ? e : "plughw:ES8389Audio";
    }
    return name.c_str();
}

const char* ctlName() {
    static std::string name;
    if (name.empty()) {
        const char* e = getauxval(AT_SECURE) ? nullptr : getenv("SQUACH_AUDIO_CTL");
        if (e && *e) {
            name = e;
        } else {
            // This alsa on the device cannot resolve the card by name
            // (snd_ctl_open("ES8389Audio") -> ENOENT), so take the card
            // name out of the PCM name and ask /proc/asound/cards for
            // its index: hw:<index> always works.
            std::string card(devName());
            if (const size_t c = card.find(':')) card.erase(0, c + 1);
            int idx = -1;
            if (FILE* f = fopen("/proc/asound/cards", "r")) {
                char line[160];
                while (fgets(line, sizeof line, f)) {
                    char* lb = strchr(line, '[');
                    char* rb = lb ? strchr(lb, ']') : nullptr;
                    if (lb && rb && rb > lb) {
                        *rb = 0;
                        lb++;
                        if (strncmp(lb, card.c_str(), card.size()) == 0 &&
                            (lb[card.size()] == ' ' || lb[card.size()] == 0)) {
                            idx = atoi(line);
                            break;
                        }
                    }
                }
                fclose(f);
            }
            name = (idx >= 0) ? "hw:" + std::to_string(idx) : card;
        }
    }
    return name.c_str();
}

// EPIPE/ESTRPIPE = the stream fell out from under us (an XRUN, a card
// reset): re-arm and try again. The frames the firmware handed over are
// lost either way -- a dropped 16 ms chunk in a blip is inaudible.
//
// A PREPARED stream is ready: this card's kernel rejects start() on an
// empty buffer (EPIPE) and starts the DMA from the first write once the
// (small) start_threshold is passed (openStream).
int armStream() {
    const snd_pcm_state_t st = snd_pcm_state(s_pcm);
    if (st == SND_PCM_STATE_RUNNING || st == SND_PCM_STATE_PREPARED) return 0;
    if (st == SND_PCM_STATE_XRUN || st == SND_PCM_STATE_SUSPENDED)
        if (snd_pcm_prepare(s_pcm) < 0) return -1;
    return snd_pcm_start(s_pcm) < 0 ? -1 : 0;
}

int writeAll(int16_t const* frames, long n) {
    while (n > 0) {
        const long r = snd_pcm_writei(s_pcm, frames, n);
        if (r == -EPIPE || r == -ESTRPIPE) {
            if (armStream() < 0) return -1;
            continue;
        }
        if (r == -EAGAIN) {
            struct timespec ts = { 0, 1000000 };   // 1 ms
            nanosleep(&ts, nullptr);
            continue;
        }
        if (r < 0) return -1;
        frames += r;
        n -= r;
    }
    return 0;
}

// The mixer, by control name. Returns false when the control is absent
// or unreadable: the DTS can expose a different set than the bench's.
// Volumes are set as a percent of the control's own min..max (from its
// element info, TLV or not).
bool mixerCtl(const char* name, bool isSwitch, int percent) {
    // This alsa version prints "ALSA lib control.c: ..." on every open
    // attempt at a card that is not there, so remember a failed card and
    // stop asking (three names per open, one open per sound).
    static bool s_noCard = false;
    if (s_noCard) return false;
    snd_ctl_t* ctl = nullptr;
    if (snd_ctl_open(&ctl, ctlName(), 0) < 0) {
        s_noCard = true;
        return false;
    }
    bool ok = false;
    snd_ctl_elem_id_t* id = nullptr;
    snd_ctl_elem_info_t* info = nullptr;
    snd_ctl_elem_value_t* val = nullptr;
    snd_ctl_elem_id_malloc(&id);
    snd_ctl_elem_info_malloc(&info);
    snd_ctl_elem_value_malloc(&val);
    snd_ctl_elem_id_set_name(id, name);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_info_set_id(info, id);
    if (snd_ctl_elem_info(ctl, info) == 0) {   // -ENOENT: the control is absent
        snd_ctl_elem_value_set_id(val, id);
        if (snd_ctl_elem_read(ctl, val) == 0) {
            if (isSwitch) {
                snd_ctl_elem_value_set_integer(val, 0, percent ? 1 : 0);
            } else {
                const long minv = snd_ctl_elem_info_get_min(info);
                const long maxv = snd_ctl_elem_info_get_max(info);
                snd_ctl_elem_value_set_integer(val, 0, minv + (maxv - minv) * percent / 100);
            }
            ok = snd_ctl_elem_write(ctl, val) == 0;
        }
    }
    snd_ctl_elem_id_free(id);
    snd_ctl_elem_info_free(info);
    snd_ctl_elem_value_free(val);
    snd_ctl_close(ctl);
    return ok;
}

}   // namespace

// ---- keeping the codec awake (i2s_alsa.h, audioKeepAwake) -------------------
namespace {

std::thread       s_awakeThread;
std::atomic<bool> s_awakeRun{false};
bool              s_awakeOn = false;     // the UI thread's view; it is the only caller
uint32_t          s_awakeRetryAt = 0;    // after a failed open: not before this

uint32_t monoMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

// Reads the capture side and throws it away: the stream being open and
// RUNNING is the whole point. This card does not start a stream on its own
// (the playback side has the same quirk, see openStream), so it is started
// explicitly, and again after any overrun. Non-blocking, with a 100 ms wait,
// so a stopped stream can never hold the thread (and the app's exit) hostage.
void awakeLoop(snd_pcm_t* cap) {
    std::vector<int16_t> buf(256 * 2);
    snd_pcm_start(cap);
    while (s_awakeRun.load(std::memory_order_relaxed)) {
        const snd_pcm_state_t st = snd_pcm_state(cap);
        if (st == SND_PCM_STATE_XRUN || st == SND_PCM_STATE_SUSPENDED || st == SND_PCM_STATE_SETUP) {
            snd_pcm_prepare(cap);
            snd_pcm_start(cap);
        } else if (st == SND_PCM_STATE_PREPARED) {
            snd_pcm_start(cap);
        }
        if (snd_pcm_wait(cap, 100) <= 0) continue;   // timeout or error: re-check the state
        const snd_pcm_sframes_t r = snd_pcm_readi(cap, buf.data(), 256);
        if (r == -EPIPE || r == -ESTRPIPE) snd_pcm_prepare(cap);
    }
    snd_pcm_drop(cap);
    snd_pcm_close(cap);
}

}   // namespace

namespace cm0 {

void audioKeepAwake(bool on) {
    if (on == s_awakeOn) return;
    if (!on) {
        s_awakeRun = false;
        if (s_awakeThread.joinable()) s_awakeThread.join();
        s_awakeOn = false;
        fprintf(stderr, "[i2s] codec released: it may sleep between sounds\n");
        return;
    }
    if ((int32_t)(monoMs() - s_awakeRetryAt) < 0) return;
    snd_pcm_t* cap = nullptr;
    int r = snd_pcm_open(&cap, devName(), SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
    if (r == 0)
        r = snd_pcm_set_params(cap, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                               (unsigned)(s_rate > 0 ? s_rate : 16000), 1, 100000);
    if (r < 0 || !cap) {
        if (cap) snd_pcm_close(cap);
        s_awakeRetryAt = monoMs() + 30000;
        fprintf(stderr, "[i2s] cannot hold the codec awake (%s): short sounds may be lost; "
                        "retrying in 30 s\n", snd_strerror(r));
        return;
    }
    s_awakeRun = true;
    s_awakeThread = std::thread(awakeLoop, cap);
    s_awakeOn = true;
    fprintf(stderr, "[i2s] codec held awake (SOUND is on): a capture stream is open and "
                    "discarded, so short alerts are not lost to the codec waking\n");
}

void audioMute(bool on) { s_muted = on; }

uint64_t audioFramesWritten() { return s_frames; }

bool audioMixerSwitch(const char* name, bool on) { return mixerCtl(name, true, on ? 1 : 0); }
bool audioMixerVolume(const char* name, int percent) { return mixerCtl(name, false, percent); }

}   // namespace cm0

// ------------------------------------------------------------- the I2S API

// TEMP bench trace: SQUACH_I2S_TRACE=1 prints the I2S calls with results.
static bool i2sTrace() {
    static const bool on = getenv("SQUACH_I2S_TRACE") != nullptr;
    return on;
}
static long i2sWriteN = 0;

// Open (or re-open) the stream with the installed rate. The firmware's
// i2s_stop ends a sound, and this alsa-lib's drop/close leaves the stream
// unusable, so every start re-opens: an open is a few milliseconds, and a
// sound is a few hundred.
int openStream(int rate) {
    snd_pcm_open(&s_pcm, devName(), SND_PCM_STREAM_PLAYBACK, 0);
    if (!s_pcm) {
        fprintf(stderr, "[i2s] %s refused (no card? in use?)\n", devName());
        return -1;
    }
    snd_pcm_hw_params_t* hw = nullptr;
    snd_pcm_hw_params_malloc(&hw);
    snd_pcm_hw_params_any(s_pcm, hw);
    int r = snd_pcm_hw_params_set_access(s_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (r == 0) r = snd_pcm_hw_params_set_format(s_pcm, hw, SND_PCM_FORMAT_S16_LE);
    if (r == 0) r = snd_pcm_hw_params_set_channels(s_pcm, hw, 2);
    if (r == 0) r = snd_pcm_hw_params_set_rate(s_pcm, hw, (unsigned)rate, 0);
    // Bound the buffer. Left alone, ALSA picks the card's maximum (131072
    // frames, 8 s at 16 kHz, measured on the device): every write returns
    // at once, a whole sound is queued in ~8 ms, and i2s_stop() used to
    // close it away unplayed. ~80 ms in 256-frame periods is the ESP32's
    // own DMA depth (4 x 256), so writes pace the sound task the same way.
    if (r == 0) {
        snd_pcm_uframes_t period = 256;
        unsigned int bufUs = 80000;
        snd_pcm_hw_params_set_period_size_near(s_pcm, hw, &period, nullptr);
        r = snd_pcm_hw_params_set_buffer_time_near(s_pcm, hw, &bufUs, nullptr);
    }
    if (r < 0) {                   // plughw: let the plugin negotiate instead
        snd_pcm_hw_params_free(hw);
        r = snd_pcm_set_params(s_pcm, SND_PCM_FORMAT_S16_LE,
                               SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                               (unsigned)rate, 100, 200000);
    } else {
        r = snd_pcm_hw_params(s_pcm, hw);
        snd_pcm_hw_params_free(hw);
    }
    if (r < 0) {
        fprintf(stderr, "[i2s] %s: parameters refused (%s)\n", devName(), snd_strerror(r));
        snd_pcm_close(s_pcm);
        s_pcm = nullptr;
        return -1;
    }
    if (getenv("SQUACH_I2S_TRACE")) {
        snd_pcm_uframes_t bs = 0, ps = 0;
        snd_pcm_get_params(s_pcm, &bs, &ps);
        fprintf(stderr, "[i2s] open %s: buffer %lu period %lu frames\n", devName(),
                (unsigned long)bs, (unsigned long)ps);
    }
    // The card's ASoC default leaves start_threshold so high that an
    // explicit start() gets EPIPE and a short sound never reaches the DMA
    // (the probe: 3200-frame buffer, no implicit start after 512 frames).
    // A small threshold starts the stream on the first write: the sound
    // task's 256-frame chunks always pass it.
    {
        snd_pcm_sw_params_t* sw = nullptr;
        if (snd_pcm_sw_params_malloc(&sw) == 0 &&
            snd_pcm_sw_params_current(s_pcm, sw) == 0) {
            snd_pcm_sw_params_set_start_threshold(s_pcm, sw, 64);
            snd_pcm_sw_params(s_pcm, sw);
        }
        if (sw) snd_pcm_sw_params_free(sw);
    }
    // The kernel driver owns the codec; this is the mixer side of it.
    // Absent controls are skipped (the null card has none).
    cm0::audioMixerSwitch("Speaker Switch", true);
    cm0::audioMixerVolume("DACL Playback Volume", 85);
    cm0::audioMixerVolume("DACR Playback Volume", 85);
    return 0;
}

esp_err_t i2s_driver_install(i2s_port_t, const i2s_config_t* cfg, int, void*) {
    if (s_pcm) return ESP_OK;      // begin() once; the firmware calls it once
    s_rate = (int)cfg->sample_rate;
    return openStream(s_rate) < 0 ? ESP_FAIL : ESP_OK;
}

esp_err_t i2s_set_pin(i2s_port_t, const i2s_pin_config_t*) {
    return s_pcm ? ESP_OK : ESP_FAIL;   // the pins are the DTS's, not ours
}

esp_err_t i2s_start(i2s_port_t) {
    std::lock_guard<std::mutex> g(s_pcmLock);
    if (!s_pcm) {
        if (openStream(s_rate) < 0) return ESP_FAIL;
    }
    const esp_err_t r = armStream() < 0 ? ESP_FAIL : ESP_OK;
    if (i2sTrace())
        fprintf(stderr, "[i2s] start -> %d (state %d)\n", r,
                s_pcm ? (int)snd_pcm_state(s_pcm) : -1);
    return r;
}

esp_err_t i2s_write(i2s_port_t, const void* src, size_t size, size_t* bytes_written,
                    uint32_t) {
    const int16_t* in = static_cast<const int16_t*>(src);
    const long frames = (long)(size / 4);
    std::lock_guard<std::mutex> g(s_pcmLock);
    esp_err_t r = ESP_OK;
    if (s_pcm) {
        if (s_muted) {
            s_zero.resize((size_t)frames * 2);
            std::memset(s_zero.data(), 0, (size_t)frames * 4);
            if (writeAll(s_zero.data(), frames) < 0) r = ESP_FAIL;
        } else {
            if (writeAll(in, frames) < 0) r = ESP_FAIL;
            else s_frames += (uint64_t)frames;
        }
    } else {
        r = ESP_FAIL;
    }
    if (i2sTrace() && (i2sWriteN++ < 24 || r != ESP_OK))
        fprintf(stderr, "[i2s] write %ld frames -> %d (state %d)\n", frames, r,
                s_pcm ? (int)snd_pcm_state(s_pcm) : -1);
    if (r == ESP_OK && bytes_written) *bytes_written = size;
    return r;
}



esp_err_t i2s_stop(i2s_port_t) {
    std::lock_guard<std::mutex> g(s_pcmLock);
    if (!s_pcm) return ESP_FAIL;
    // Play out what is queued first: close() alone drops the ring buffer,
    // which is the tail of the sound (the ESP32's i2s_stop runs after its
    // DMA has drained; here the firmware's trailing silence does not cover
    // the buffer). Blocking is fine -- only the sound task calls this.
    snd_pcm_sframes_t queued = 0;
    if (i2sTrace()) snd_pcm_delay(s_pcm, &queued);
    if (snd_pcm_state(s_pcm) == SND_PCM_STATE_RUNNING) snd_pcm_drain(s_pcm);
    snd_pcm_close(s_pcm);   // this alsa-lib's drop/close parks the stream
    s_pcm = nullptr;        // unusable: the next start re-opens it
    if (i2sTrace()) fprintf(stderr, "[i2s] stop -> 0 (drained %ld frames)\n", (long)queued);
    return ESP_OK;
}

esp_err_t i2s_zero_dma_buffer(i2s_port_t) {
    return s_pcm ? ESP_OK : ESP_FAIL;   // nothing to zero: the stream is ours
}
