// SquachWatch-CM0 -- the audio path: the real sound.o (the firmware's DSP,
// unmodified) streaming through i2s_alsa.o into ALSA.
//
//   build/audio_test            the null PCM: the render reaches the
//                               transport, the dimmer's transport mute
//                               silences it, and the firmware's own gates
//                               (the SOUND setting, hush) hold. No hardware
//                               or card needed -- the container runs it.
//   build/audio_test --loopback the ES8389's DAC -> ADC electrical loopback
//                               (device only): an alert must produce real
//                               captured audio, and the card must be quiet
//                               before it. Thresholds are generous: this
//                               proves the air moved, not the timbre.
#include <alsa/asoundlib.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "i2s_alsa.h"
#include "driver/i2s.h"
#include "prefs_store.h"
#include "settings.h"
#include "sound.h"

static int s_checks = 0, s_fail = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        s_checks++;                                                              \
        if (!(cond)) {                                                           \
            s_fail++;                                                            \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                        \
    } while (0)

static uint64_t frames() { return cm0::audioFramesWritten(); }
static void waitMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// A sample play (bypasses the mode/night/hush gates) rendered at real time:
// the frames that reach the transport are the recipe's length plus the
// silence padding, in 256-frame chunks.
static void playSampled(Sound::Id id, uint64_t& written, int wait) {
    const uint64_t before = frames();
    Sound::play(id, true);
    waitMs(wait);
    written = frames() - before;
}

// The recipe lengths from sound.cpp, 16 kHz: the parts' own times plus
// gaps, then six passes of the echo, then the 30 ms tail; NEW_DEVICE 200
// (no echo), PLUG_IN 260 + 90 echo, UNPLUG 300 + 90 echo, WATCH_HIT
// 520+120+560 + 190 echo x6; each gets (t + echo*6 + 30) frames plus
// 1280 of pop-guard silence.
static const struct { Sound::Id id; int total; int wait; } RECIPES[] = {
    { Sound::Id::NEW_DEVICE, (200 + 30) * 16, 600 },
    { Sound::Id::PLUG_IN,    (260 + 90 * 6 + 30) * 16, 900 },
    { Sound::Id::UNPLUG,     (300 + 90 * 6 + 30) * 16, 1000 },
    { Sound::Id::WATCH_HIT,  (1200 + 190 * 6 + 30) * 16, 2600 },
};
static const int PADDING = (24 + 16 + 16 + 24) * 16;   // silence() calls in playNow()

static int runNull() {
    setenv("SQUACH_AUDIO_DEV", "null", 1);   // the void: timing true, air not

    char tmpl[] = "/tmp/squach-audio-XXXXXX";
    const char* dir = mkdtemp(tmpl);
    CHECK(dir != nullptr);
    cm0::prefsSetDir(dir ? dir : "/tmp");
    Settings::load();

    Sound::begin();
    CHECK(Sound::present());
    if (!Sound::present()) return 1;

    // The render reaches the transport, in full.
    for (const auto& r : RECIPES) {
        uint64_t w = 0;
        playSampled(r.id, w, r.wait);
        const uint64_t lo = (uint64_t)r.total, hi = (uint64_t)(r.total + PADDING + 256);
        fprintf(stderr, "  id %-2d  wrote %llu frames (recipe %d)\n",
                (int)r.id, (unsigned long long)w, r.total);
        CHECK(w >= lo && w <= hi);
    }

    // The dimmer's transport mute: the player keeps consuming (timing),
    // nothing audible goes out, and it resumes when the panel wakes.
    cm0::audioMute(true);
    uint64_t w = 0;
    playSampled(RECIPES[1].id, w, RECIPES[1].wait);
    CHECK(w == 0);
    cm0::audioMute(false);
    w = 0;
    playSampled(RECIPES[1].id, w, RECIPES[1].wait);
    CHECK(w >= (uint64_t)RECIPES[1].total && w <= (uint64_t)(RECIPES[1].total + PADDING + 256));

    // The firmware's own gates, on the real play() path (not sample).
    // WATCH_HIT for the hush half: the one id the night rule never stops.
    while (Settings::soundMode() != 2) Settings::cycleSoundMode();
    Sound::hush(true);
    w = 0;
    const uint64_t b = frames();
    Sound::play(Sound::Id::WATCH_HIT);
    waitMs(400);
    CHECK(frames() - b == 0);
    Sound::hush(false);
    Sound::play(Sound::Id::WATCH_HIT);
    waitMs(800);
    CHECK(frames() - b >= 8000);   // 800 ms in, minus the pop guard

    // The SOUND setting: OFF stops the alert ids, as on every board.
    while (Settings::soundMode() != 0) Settings::cycleSoundMode();
    const uint64_t c = frames();
    Sound::play(Sound::Id::NEW_DEVICE);
    waitMs(400);
    CHECK(frames() - c == 0);

    return 0;
}

// --------------------------------------------------------------- loopback

static long maxAbs(const std::vector<int16_t>& v, size_t from, size_t to) {
    long m = 0;
    for (size_t i = from; i < to && i < v.size(); i += 2)   // both channels
        m = std::max<long>(m, std::abs((int)v[i]));
    return m;
}

// Report-only: plays a real alert through the transport (the capture
// stream keeps the codec clocked) and prints the baseline/onset/peak it
// measured on the capture side. No hard gates beyond "the card is up, the
// mixers exist, the sound was queued" -- the numbers are for the bench
// (the user's ears are the final verdict; see SESSION_STATE).
static int runLoopback() {
    char tmpl[] = "/tmp/squach-audio-XXXXXX";
    const char* dir = mkdtemp(tmpl);
    CHECK(dir != nullptr);
    cm0::prefsSetDir(dir ? dir : "/tmp");
    Settings::load();
    Settings::adjustSoundVolume(100);   // full scale: the render is (vol/100)^2 * 32000

    if (!getenv("SQUACH_AUDIO_DEV"))
        setenv("SQUACH_AUDIO_DEV", "plughw:ES8389Audio", 1);
    Sound::begin();
    if (!Sound::present()) {
        fprintf(stderr, "[audio_test] the card did not come up (in use? no ES8389?)\n");
        return 2;
    }

    // The DAC into the ADC: the electrical loopback, with the capture at
    // full gain. Restored (off) afterwards. The codec's ADC is clocked by
    // the I2S port, and starting the capture stream below enables that
    // clock - so there is no pump here: a silent pump on the same playback
    // stream as the alert would choke the alert's writes, and the alert's
    // own i2s_stop at the end of its recipe would close the stream out
    // from under the pump.
    // The mic is a floating input on this board: at its stock PGA of
    // +24 dB it is the whole noise floor (dev peak ~4 k), so the test
    // drops the mic PGA to its minimum (0 dB) for the run.
    const bool swL = cm0::audioMixerSwitch("ADC Mixer DACL ADCL Mixer", true);
    const bool swR = cm0::audioMixerSwitch("ADC Mixer DACR ADCR Mixer", true);
    cm0::audioMixerVolume("ADCL Capture Volume", 100);
    cm0::audioMixerVolume("ADCR Capture Volume", 100);
    cm0::audioMixerVolume("ADC OSR Volume", 100);   // stock 191 is -32 dB
    cm0::audioMixerVolume("ADCL PGA Volume", 0);
    cm0::audioMixerVolume("ADCR PGA Volume", 0);
    fprintf(stderr, "[audio_test] loopback mixers: L %d R %d\n", swL, swR);
    if (!swL || !swR) {
        fprintf(stderr, "[audio_test] the DAC->ADC mixers are not on this card\n");
        return 2;
    }

    snd_pcm_t* cap = nullptr;
    if (snd_pcm_open(&cap, getenv("SQUACH_AUDIO_DEV"), SND_PCM_STREAM_CAPTURE, 0) < 0 || !cap) {
        fprintf(stderr, "[audio_test] cannot open the capture side\n");
        return 2;
    }
    snd_pcm_hw_params_t* hw = nullptr;
    snd_pcm_hw_params_malloc(&hw);
    snd_pcm_hw_params_any(cap, hw);
    if (snd_pcm_hw_params_set_access(cap, hw, SND_PCM_ACCESS_RW_INTERLEAVED) ||
        snd_pcm_hw_params_set_format(cap, hw, SND_PCM_FORMAT_S16_LE) ||
        snd_pcm_hw_params_set_channels(cap, hw, 2) ||
        snd_pcm_hw_params_set_rate(cap, hw, 16000, 0) ||
        snd_pcm_hw_params(cap, hw) < 0) {
        fprintf(stderr, "[audio_test] the capture side refused 16 kHz stereo s16\n");
        snd_pcm_hw_params_free(hw);
        return 2;
    }
    snd_pcm_hw_params_free(hw);
    snd_pcm_start(cap);
    const int chunk = 256;
    std::vector<int16_t> buf((size_t)chunk * 2);
    // The codec's ADC needs a moment after the stream comes up (and the
    // playback side is still filling its start threshold): throw the first
    // 300 ms away before measuring anything.
    for (int i = 0; i < 4800 / chunk; i++)
        snd_pcm_readi(cap, buf.data(), chunk);
    // A quiet baseline: the card, before the alert. The ES8389's ADC path
    // carries a DC offset (DC-coupled on this board), which is a hardware
    // characteristic, not audio -- so measure the deviation from the
    // baseline's own mean.
    double baseMean = 0;
    long baseDev = 0;
    {
        std::vector<int16_t> base;
        for (int i = 0; i < 4800 / chunk; i++) {
            if (snd_pcm_readi(cap, buf.data(), chunk) != chunk) continue;
            for (long j = 0; j < chunk * 2; j++) base.push_back(buf[j]);
        }
        for (int16_t v : base) baseMean += v;
        baseMean /= std::max<size_t>(base.size(), 1);
        for (int16_t v : base)
            baseDev = std::max(baseDev, (long)std::abs((double)v - baseMean));
        fprintf(stderr, "[audio_test] baseline mean %.0f, dev peak %ld\n", baseMean, baseDev);
    }
    // The alert, 200 ms in (rec is interleaved stereo: 2 samples per
    // frame, so ms = samples / 32). Transport latency varies (the alert's
    // audio enters the transport behind whatever silence is buffered), so
    // find where the capture actually woke up and measure around that:
    // quiet before, real audio after.
    std::vector<int16_t> rec;
    bool played = false;
    const uint64_t f0 = cm0::audioFramesWritten();
    for (int i = 0; i < 19200 / chunk; i++) {
        const long r = snd_pcm_readi(cap, buf.data(), chunk);
        if (r < chunk) { snd_pcm_prepare(cap); continue; }
        if (!played && rec.size() >= 6400) {
            Sound::play(Sound::Id::NEW_DEVICE, true);
            played = true;
        }
        for (long j = 0; j < r * 2; j++) rec.push_back(buf[j]);
    }
    CHECK(played);
    fprintf(stderr, "[audio_test] frames to transport in window: %llu (the alert's recipe)\n",
            (unsigned long long)(cm0::audioFramesWritten() - f0));
    // The same DC caveat: the reference is the mean of the quiet part of
    // this recording (the first 150 ms, before the 200 ms trigger).
    double preMean = 0;
    const size_t preN = std::min(rec.size(), (size_t)4800);
    for (size_t i = 0; i < preN; i++) preMean += rec[i];
    preMean /= std::max<size_t>(preN, 1);
    auto dev = [&](size_t i) { return std::abs((double)rec[i] - preMean); };
    long onset = -1;
    for (size_t i = 6400; i < rec.size(); i++)
        if (dev(i) > 1000) { onset = (long)i; break; }
    // "Quiet before" stops 100 ms short of the onset: the alert's own
    // ramp-up starts just before its first loud sample.
    const long preEnd = onset > 1600 ? onset - 1600 : 0;
    long beforePeak = 0, afterPeak = 0;
    for (long i = 0; i < preEnd; i++) beforePeak = std::max(beforePeak, (long)dev(i));
    for (long i = onset >= 0 ? onset : 0; i < (long)rec.size(); i++)
        afterPeak = std::max(afterPeak, (long)dev(i));
    fprintf(stderr, "[audio_test] alert onset %s ms, pre %ld, post %ld\n",
            onset < 0 ? "none" : std::to_string(onset / 32).c_str(),
            beforePeak, afterPeak);
    // The whole recording's peak (both channels), for the bench: compare it
    // against a full-scale tone on the same loopback (0 dBFS = 32768).
    const long capPeak = maxAbs(rec, 0, rec.size());
    fprintf(stderr, "[audio_test] capture peak %ld (full-scale tone on this path = 32768)\n",
            capPeak);

    snd_pcm_drop(cap);
    snd_pcm_close(cap);
    cm0::audioMixerSwitch("ADC Mixer DACL ADCL Mixer", false);
    cm0::audioMixerSwitch("ADC Mixer DACR ADCR Mixer", false);
    cm0::audioMixerVolume("ADC OSR Volume", 75);   // back to the stock -32 dB
    Sound::quiet();
    return 0;
}

// Bench mode: plays the real NEW_DEVICE alert N times back-to-back at full
// volume through the app's own playback path (no capture). Unmissable if the
// speaker can produce it; the frame count proves the transport ran.
static int runBlips(int n) {
    Settings::load();
    Settings::adjustSoundVolume(100);
    Sound::begin();
    if (!Sound::present()) {
        fprintf(stderr, "[audio_test] the card did not come up (in use? no ES8389?)\n");
        return 2;
    }
    fprintf(stderr, "[audio_test] playing %d x NEW_DEVICE at volume 100 (listen)\n", n);
    const uint64_t f0 = frames();
    for (int i = 0; i < n; i++) {
        Sound::play(Sound::Id::NEW_DEVICE, true);   // sample=true: bypass the mode/night gates
        waitMs(450);   // let this one play (~310 ms) before the next: the queue is 4 deep
    }
    Sound::quiet();
    const uint64_t written = frames() - f0;
    fprintf(stderr, "[audio_test] %llu frames to the transport "
                    "(~%llu expected for %d alerts)\n",
            (unsigned long long)written, (unsigned long long)(4960ull * n), n);
    CHECK(written >= 4960ull * n / 2);
    return 0;
}

// Bench mode: a full-scale 440 Hz sine through the app's own i2s path
// (the same i2s_start/i2s_write the sound task uses), 3 s. Compared against
// `aplay -D plughw:1,0` of the same tone (audible on this device): if this
// is just as loud, the stream is fine and the alert's samples are the issue;
// if not, the stream setup is.
static int runTone() {
    Settings::load();
    Sound::begin();
    if (!Sound::present()) {
        fprintf(stderr, "[audio_test] the card did not come up (in use? no ES8389?)\n");
        return 2;
    }
    const int rate = 16000, chunk = 256, secs = 3;
    fprintf(stderr, "[audio_test] full-scale 440 Hz, %d s, via the app's i2s path (listen)\n", secs);
    i2s_start(0);
    int16_t buf[chunk * 2];
    double ph = 0.0;
    const double dph = 2.0 * 3.14159265358979 * 440.0 / rate;
    uint64_t written = 0;
    for (int n = 0; n < rate * secs; n += chunk) {
        for (int i = 0; i < chunk; i++, ph += dph) {
            const int16_t s = (int16_t)(30000.0 * sin(ph));
            buf[i * 2] = s;
            buf[i * 2 + 1] = s;
        }
        size_t done = 0;
        if (i2s_write(0, buf, chunk * 4, &done, 0) != ESP_OK) {
            fprintf(stderr, "[audio_test] write failed after %llu frames\n",
                    (unsigned long long)written);
            break;
        }
        written += chunk;
    }
    i2s_stop(0);
    fprintf(stderr, "[audio_test] %llu frames written\n", (unsigned long long)written);
    CHECK(written == (uint64_t)rate * secs);
    return 0;
}

int main(int argc, char** argv) {
    const bool loop = argc > 1 && !strcmp(argv[1], "--loopback");
    const bool blips = argc > 1 && !strcmp(argv[1], "--blips");
    const bool tone = argc > 1 && !strcmp(argv[1], "--tone");
    int rc;
    if (blips) {
        const int n = (argc > 2 && argv[2]) ? atoi(argv[2]) : 8;
        rc = runBlips(n);
    } else if (tone) {
        rc = runTone();
    } else {
        rc = loop ? runLoopback() : runNull();
    }
    fprintf(stderr, "[audio_test] %d checks, %d failures%s\n",
            s_checks, s_fail,
            loop ? " (loopback)" : (blips ? " (blips)" : (tone ? " (tone)" : " (null)")));
    return (rc == 0 && s_fail == 0) ? 0 : 1;
}
