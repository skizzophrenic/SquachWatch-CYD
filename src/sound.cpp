// SquachWatch-CYD — the speaker on the Freenove S3 boards. See sound.h.
//
// The hardware, from Freenove's schematics and their Sketch_07.1_Music: an
// ES8311 codec at I2C 0x18 on the touch bus, fed over I2S, driving a small
// mono amplifier whose SHUTDOWN pin is GPIO1 on both boards (AUDIO_EN on the
// schematic; their sketch drives it LOW to play). The I2S pins differ:
//
//   2.8" (FNK0104B): MCK 4, BCK 5, WS 7, DOUT 8, DIN 6
//   3.5" (FNK0104N): MCK 17, BCK 18, WS 21, DOUT 15, DIN 16
//
// None of them is used by anything else on either board (the 3.5"'s 15-18
// and 21 are free; the 2.8"'s 4-8 are free), checked against PINOUT.md.
//
// The codec is set up once, at boot, over Wire -- the loop task owns Wire
// (the touch controller is polled on it), so the player never touches I2C.
// Loudness is done in the samples; silence is the amplifier's enable pin,
// which also means an idle board spends nothing on sound: the amp is in
// shutdown (under a microamp) and the I2S clocks are stopped.
//
// Playback is a small FreeRTOS task fed by a queue. It renders one 256-frame
// chunk at a time straight into the I2S driver's DMA buffers and blocks on
// the write, so a slow frame on the drawing side never puts a gap in a
// sound. Four 256-frame buffers is 64 ms of DMA, 4 KB of internal RAM.
#include "sound.h"
#if SQW_SOUND
#include <Arduino.h>
#include <Wire.h>
#if defined(WAVESHARE_C5)
#include "ws_c5_board.h"
#endif
#include <math.h>
#include <driver/i2s.h>
#include "settings.h"
#include "clock.h"

namespace Sound {

#if defined(FREENOVE_S3_35)
static const int PIN_MCK = 17, PIN_BCK = 18, PIN_WS = 21, PIN_DOUT = 15;
static const int PIN_AMP = 1;
#elif defined(WAVESHARE_C5)
// The Waveshare C5 3.5": the same ES8311 (0x18, on the expander's bus) and an
// NS4150B amplifier, from Waveshare's BSP (BSP_I2S_*) and schematic. Two
// differences, both read off the schematic: the codec's MCLK pin is not
// wired (so it clocks itself from BCLK -- SOUND_BCLK_MCLK below), and the
// amplifier's enable (PA_CTRL, high = on) is the I/O expander's EXIO5, not a
// GPIO, so amp() goes through WsC5::amp().
static const int PIN_MCK = -1, PIN_BCK = 23, PIN_WS = 10, PIN_DOUT = 25;
static const int PIN_AMP = -1;
#define SOUND_BCLK_MCLK 1
#elif defined(TDECK_PLUS)
// The T-Deck Plus: a MAX98357A class-D amp straight on I2S (LilyGo's
// BOARD_I2S_BCK 7 / WS 5 / DOUT 6). No codec to set up, no master clock, and
// no enable pin of its own -- it sleeps when the clocks stop.
static const int PIN_MCK = -1, PIN_BCK = 7, PIN_WS = 5, PIN_DOUT = 6;
static const int PIN_AMP = -1;
#define SOUND_NO_CODEC 1
#else
static const int PIN_MCK = 4,  PIN_BCK = 5,  PIN_WS = 7,  PIN_DOUT = 8;
static const int PIN_AMP = 1;
#endif
#ifndef SOUND_NO_CODEC
#define SOUND_NO_CODEC 0
#endif
static const uint8_t    CODEC   = 0x18;
static const uint32_t   RATE    = 16000;      // plenty for tones under 4 kHz
static const i2s_port_t PORT    = I2S_NUM_0;
// Core 1 on the dual-core S3s, as before. The C5 has one core, and FreeRTOS
// asserts on a task pinned to a core that does not exist: the Waveshare C5's
// first speaker build rebooted at Sound::begin() on exactly that.
#define SOUND_CORE ((portNUM_PROCESSORS > 1) ? 1 : tskNO_AFFINITY)
static const int        CHUNK   = 256;        // frames per write, 16 ms

static bool          s_ok      = false;
static bool          s_ampLow  = true;        // the amp plays with the pin LOW
static volatile bool s_hush    = false;
static QueueHandle_t s_q       = nullptr;
static TaskHandle_t  s_task    = nullptr;

// ---- the sounds ------------------------------------------------------------
// One step: a tone from f to `to` (0 = steady) for ms, then a gap. Wave 0 is
// square, 1 triangle, 2 sine, 3 sawtooth, 4 a low-passed noise burst. The
// numbers are the Sound Lab's, where the browser played the same recipe.
enum : uint8_t { SQ = 0, TRI, SIN, SAW, NOISE };
struct Step { uint16_t f, to, ms; uint8_t wave, gap; };

static const Step NEW_DEVICE[] = { {660, 0, 45, TRI, 25}, {880, 0, 45, TRI, 25}, {1100, 0, 60, TRI, 0} };
static const Step WATCH_HIT[]  = { {620, 0, 90, SQ, 0}, {930, 0, 90, SQ, 0}, {620, 0, 90, SQ, 0},
                                   {930, 0, 90, SQ, 0}, {620, 0, 90, SQ, 0}, {930, 0, 120, SQ, 0} };
static const Step LOW_BATT[]   = { {440, 0, 160, TRI, 40}, {330, 0, 260, TRI, 0} };
static const Step MESSAGE[]    = { {1046, 0, 120, TRI, 20}, {1568, 0, 200, TRI, 0} };
static const Step VISITOR[]    = { {784, 0, 140, SIN, 20}, {659, 0, 200, SIN, 0} };
static const Step THROW[]      = { {300, 1500, 260, SIN, 0} };
static const Step LAND[]       = { {0, 0, 30, NOISE, 0}, {180, 70, 60, SQ, 0} };
static const Step CLIPPY[]     = { {300, 900, 40, SIN, 0}, {900, 400, 60, SIN, 0}, {400, 700, 60, SIN, 0} };
static const Step TOAST_POP[]  = { {200, 1800, 80, SQ, 0} };
static const Step PLUG_IN[]    = { {523, 0, 60, SQ, 10}, {659, 0, 60, SQ, 10}, {784, 0, 120, SQ, 0} };
static const Step UNPLUG[]     = { {1000, 200, 160, SQ, 0} };

struct Seq { const Step* s; uint8_t n; };
#define SEQ(a) { a, (uint8_t)(sizeof(a) / sizeof(a[0])) }
static const Seq SEQS[(int)Id::COUNT] = {
    SEQ(NEW_DEVICE), SEQ(WATCH_HIT), SEQ(LOW_BATT), SEQ(MESSAGE), SEQ(VISITOR), SEQ(THROW),
    SEQ(LAND), SEQ(CLIPPY), SEQ(TOAST_POP), SEQ(PLUG_IN), SEQ(UNPLUG),
};
#undef SEQ

// ---- the codec -------------------------------------------------------------
static bool wr(uint8_t r, uint8_t v) {
    Wire.beginTransmission(CODEC); Wire.write(r); Wire.write(v);
    return Wire.endTransmission() == 0;
}
static uint8_t rd(uint8_t r) {
    Wire.beginTransmission(CODEC); Wire.write(r);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom(CODEC, (uint8_t)1) != 1) return 0;
    return (uint8_t)Wire.read();
}
// Espressif's es8311 driver boiled down to one case: slave on the bus, MCLK
// from its pin at 256 x 16 kHz, 16-bit I2S in, the DAC on, the ADC left
// asleep. The divider registers (02-08) only matter to the codec as a bus
// master, but they are set to the table's 4.096 MHz / 16 kHz row anyway, as
// the driver does. Volume register 32 sits at the driver's 85%.
static bool codecInit() {
    if (!wr(0x00, 0x1F)) return false;      // reset everything
    delay(20);
    wr(0x00, 0x00);
    wr(0x00, 0x80);                         // power on
#if SOUND_BCLK_MCLK
    // No MCLK wire: the internal clock comes from BCLK, multiplied 8x --
    // 16 kHz x 32 bits x 8 = 4.096 MHz, the same 256 fs as the row below.
    // Espressif's es8311 driver does exactly this when use_mclk is false.
    wr(0x01, 0xBF);                         // MCLK from BCLK, every clock on
    wr(0x02, (uint8_t)((rd(0x02) & 0x07) | (3 << 3)));   // pre-divider 1, multiplier 8x
#else
    wr(0x01, 0x3F);                         // MCLK from the MCLK pin, every clock on
    wr(0x02, (uint8_t)(rd(0x02) & 0x07));   // pre-divider 1, multiplier 1x
#endif
    wr(0x03, 0x10);                         // single speed, ADC OSR
    wr(0x04, 0x10);                         // DAC OSR
    wr(0x05, 0x00);                         // ADC and DAC dividers 1
    wr(0x06, (uint8_t)((rd(0x06) & 0xC0) | 0x03));   // BCLK not inverted, divider 4
    wr(0x07, (uint8_t)(rd(0x07) & 0xC0));   // LRCK divider high
    wr(0x08, 0xFF);                         // LRCK divider low
    wr(0x00, (uint8_t)(rd(0x00) & 0xBF));   // serial port slave
    wr(0x09, 0x0C);                         // SDP in: 16-bit I2S
    wr(0x0A, 0x0C);                         // SDP out: 16-bit I2S
    wr(0x0D, 0x01);                         // analog circuitry up
    wr(0x0E, 0x02);                         // PGA and ADC modulator
    wr(0x12, 0x00);                         // DAC up
    wr(0x13, 0x10);                         // output to the HP drive (the amp's input)
    wr(0x1C, 0x6A);                         // ADC equalizer bypass
    wr(0x37, 0x08);                         // DAC equalizer bypass
    wr(0x32, 0xD8);                         // DAC volume, 85%
    return true;
}

// ---- the amplifier ---------------------------------------------------------
#if defined(WAVESHARE_C5)
// One I2C write to the expander. Wire locks per transaction, so this is safe
// from the sound task while the loop polls touch on the same bus.
static void amp(bool on) { WsC5::amp(on); }
#else
static void amp(bool on) { if (PIN_AMP >= 0) digitalWrite(PIN_AMP, (on == s_ampLow) ? LOW : HIGH); }
#endif

// ---- rendering -------------------------------------------------------------
// Per-chunk synthesis into a stereo 16-bit buffer, the same sample in both
// slots (the codec takes the left). The envelope is the Lab's: a 4 ms attack,
// level until 60% of the step, then down to nothing at the end. Square and
// saw are quieter than the round waves: on a thumbnail speaker they bite.
static float waveSample(uint8_t w, float ph, uint32_t& lcg, float& lp) {
    switch (w) {
        case SQ:  return ph < 0.5f ? 0.35f : -0.35f;
        case TRI: return (ph < 0.5f ? (ph * 4.0f - 1.0f) : (3.0f - ph * 4.0f)) * 0.7f;
        case SIN: return sinf(ph * 6.2831853f) * 0.7f;
        case SAW: return (ph * 2.0f - 1.0f) * 0.4f;
        default: {
            lcg = lcg * 1664525u + 1013904223u;
            const float n = ((int32_t)(lcg >> 8) / 8388608.0f) - 1.0f;   // -1..1
            lp += 0.25f * (n - lp);                                      // one-pole low-pass
            return lp * 0.9f;
        }
    }
}

static void writeFrames(int16_t* buf, int frames) {
    size_t done = 0;
    i2s_write(PORT, buf, (size_t)frames * 4, &done, portMAX_DELAY);
}

static void silence(int16_t* buf, int ms) {
    memset(buf, 0, CHUNK * 4);
    for (int left = ms * (int)RATE / 1000; left > 0; left -= CHUNK)
        writeFrames(buf, left < CHUNK ? left : CHUNK);
}

static void playNow(Id id) {
    const Seq& q = SEQS[(int)id];
    // The percent squared: the ear hears loudness on a curve, and squaring
    // the slider gives the bottom half of it real steps instead of a jump.
    const float pct = (float)Settings::soundVolume() / 100.0f;
    const float vol = pct * pct;
    int16_t buf[CHUNK * 2];

    i2s_start(PORT);
    silence(buf, 24);         // clocks and zeros before the amp wakes: no pop
    amp(true);
    silence(buf, 16);

    uint32_t lcg = 0x5EED1234u;
    float lp = 0;
    for (uint8_t si = 0; si < q.n; si++) {
        const Step& st = q.s[si];
        const int total = (int)(st.ms * RATE / 1000);
        const int atk   = (int)(RATE * 4 / 1000);
        const int hold  = total * 6 / 10;
        float f = (float)st.f;
        const float ratio = (st.to && st.f) ? powf((float)st.to / (float)st.f, 1.0f / (float)total) : 1.0f;
        float ph = 0;
        for (int at = 0; at < total; ) {
            const int n = (total - at) < CHUNK ? (total - at) : CHUNK;
            for (int i = 0; i < n; i++, at++) {
                float env;
                if (at < atk)        env = (float)at / (float)atk;
                else if (at < hold)  env = 1.0f;
                else                 env = (float)(total - at) / (float)(total - hold);
                const float v = waveSample(st.wave, ph, lcg, lp) * env * vol;
                const int16_t s = (int16_t)(v * 32000.0f);
                buf[i * 2] = s; buf[i * 2 + 1] = s;
                ph += f / (float)RATE;
                if (ph >= 1.0f) ph -= 1.0f;
                f *= ratio;
            }
            writeFrames(buf, n);
        }
        if (st.gap) silence(buf, st.gap);
    }

    silence(buf, 16);
    amp(false);
    silence(buf, 24);         // let the DMA drain before the clocks stop
    i2s_zero_dma_buffer(PORT);
    i2s_stop(PORT);
}

static void soundTask(void*) {
    uint8_t id;
    for (;;) {
        if (xQueueReceive(s_q, &id, portMAX_DELAY) != pdTRUE) continue;
        if (id < (uint8_t)Id::COUNT) playNow((Id)id);
    }
}

// ---- the API ---------------------------------------------------------------
void begin() {
    if (PIN_AMP >= 0) pinMode(PIN_AMP, OUTPUT);
    amp(false);
#if !SOUND_NO_CODEC
    Wire.beginTransmission(CODEC);
    if (Wire.endTransmission() != 0) { Serial.println("[sound] ES8311 did not answer; no speaker"); return; }
#endif

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 4;
    cfg.dma_buf_len          = CHUNK;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    cfg.fixed_mclk           = 0;
    cfg.mclk_multiple        = I2S_MCLK_MULTIPLE_256;
    cfg.bits_per_chan        = I2S_BITS_PER_CHAN_16BIT;
    if (i2s_driver_install(PORT, &cfg, 0, nullptr) != ESP_OK) { Serial.println("[sound] I2S driver refused"); return; }
    i2s_pin_config_t pins = {};
    pins.mck_io_num   = PIN_MCK >= 0 ? PIN_MCK : I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = PIN_BCK;
    pins.ws_io_num    = PIN_WS;
    pins.data_out_num = PIN_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    i2s_set_pin(PORT, &pins);
    i2s_zero_dma_buffer(PORT);
    // The codec's power-up wants its clocks running; the driver starts them
    // on install. Set the codec up, then park the clocks until a sound.
#if SOUND_NO_CODEC
    i2s_stop(PORT);
    s_q = xQueueCreate(4, sizeof(uint8_t));
    xTaskCreatePinnedToCore(soundTask, "sound", 3072, nullptr, 2, &s_task, SOUND_CORE);
    s_ok = s_q && s_task;
    Serial.printf("[sound] I2S amp ready: BCK %d WS %d DOUT %d%s\n", PIN_BCK, PIN_WS, PIN_DOUT, s_ok ? "" : " -- task failed");
#else
    const bool ok = codecInit();
    const uint8_t id1 = rd(0xFD), id2 = rd(0xFE);
    i2s_stop(PORT);
    if (!ok) { Serial.println("[sound] ES8311 setup failed"); return; }
    s_q = xQueueCreate(4, sizeof(uint8_t));
    xTaskCreatePinnedToCore(soundTask, "sound", 3072, nullptr, 2, &s_task, SOUND_CORE);
    s_ok = s_q && s_task;
    Serial.printf("[sound] ES8311 id %02X%02X ready: MCK %d BCK %d WS %d DOUT %d, amp on GPIO%d%s\n",
                  id1, id2, PIN_MCK, PIN_BCK, PIN_WS, PIN_DOUT, PIN_AMP, s_ok ? "" : " -- task failed");
#endif
}

void play(Id id, bool sample) {
    if (!s_ok || id >= Id::COUNT) return;
    if (!sample) {
        const uint8_t mode = Settings::soundMode();          // 0 OFF, 1 ALERTS, 2 ALL
        const bool alert = id == Id::NEW_DEVICE || id == Id::WATCH_HIT || id == Id::LOW_BATTERY;
        if (mode == 0 || (!alert && mode < 2)) return;
        if (s_hush) return;
        if (Clock::night() && id != Id::WATCH_HIT) return;
    }
    const uint8_t b = (uint8_t)id;
    xQueueSend(s_q, &b, 0);        // a full queue drops the sound, not the frame
}

void hush(bool on) { s_hush = on; }

void quiet() {
    if (!s_ok) return;
    xQueueReset(s_q);
    amp(false);
}

bool present() { return s_ok; }

void ampLow(bool activeLow) { s_ampLow = activeLow; amp(false); }

}  // namespace Sound
#endif  // SQW_SOUND
