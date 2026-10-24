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
// Playback is a small FreeRTOS task fed by a queue. It renders the whole
// sound into PSRAM, then streams it 256 frames at a time into the I2S
// driver's DMA buffers and blocks on the write, so a slow frame on the
// drawing side never puts a gap in a sound. Four 256-frame buffers is 64 ms
// of DMA, 4 KB of internal RAM.
//
// The Waveshare ESP32-C5-Touch-LCD-2.8 has the same ES8311 at 0x18, on the
// same bus as its touch chip, with BCK 24, WS 25, DOUT 26 (not the BSP's 27;
// see PIN_DOUT) and NO master clock
// line (Waveshare's BSP: BSP_I2S_MCLK is NC), so the codec takes its clock
// from BCK: 16-bit slots make that 32 x 16 kHz = 512 kHz, multiplied x8 in
// the codec to the 4.096 MHz (256 fs) the rest of the setup assumes. The NS4150B's enable is the CH32 helper's
// EXIO3, high = on. It uses IDF 5's I2S driver (i2s_std), not the legacy
// one: on the C5's core the two cannot be linked together.
#include "sound.h"
#if SQW_SOUND
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#if defined(WAVESHARE_C5)
#include <driver/i2s_std.h>
#include "waveshare_c5_board.h"
#else
#include <driver/i2s.h>
#endif
#include <esp_heap_caps.h>
#include "settings.h"
#include "clock.h"

namespace Sound {

#if defined(WAVESHARE_C5)
// The MCU's data out is GPIO26 -- CONFIRMED by ear on the board 2026-10-13.
// Waveshare's BSP says the opposite (`.dout = BSP_I2S_DSIN`, GPIO27, "MCU TX
// -> codec DSDIN"), and with that this board is silent.
static const int PIN_MCK = -1, PIN_BCK = 24, PIN_WS = 25, PIN_DOUT = 26;
static const int PIN_AMP = -1;   // the CH32's EXIO3: see amp()
#elif defined(FREENOVE_S3_35)
static const int PIN_MCK = 17, PIN_BCK = 18, PIN_WS = 21, PIN_DOUT = 15;
static const int PIN_AMP = 1;
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
static const int        CHUNK   = 256;        // frames per write, 16 ms

// ---- the I2S port: the legacy driver, or IDF 5's on the Waveshare ----------
#if defined(WAVESHARE_C5)
static i2s_chan_handle_t s_tx = nullptr;
static bool i2sInstall() {
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    cc.dma_desc_num  = 4;
    cc.dma_frame_num = CHUNK;
    cc.auto_clear    = true;
    if (i2s_new_channel(&cc, &s_tx, nullptr) != ESP_OK) return false;
    i2s_std_config_t sc = {};
    sc.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(RATE);
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    // 16-bit slots: BCK = 32 fs, which the codec multiplies x8 to 256 fs --
    // the layout esp_codec_dev runs on this board (16-bit stereo, use_mclk
    // false, pre_multi forced to x8).
    sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    sc.gpio_cfg.bclk = (gpio_num_t)PIN_BCK;
    sc.gpio_cfg.ws   = (gpio_num_t)PIN_WS;
    sc.gpio_cfg.dout = (gpio_num_t)PIN_DOUT;
    sc.gpio_cfg.din  = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(s_tx, &sc) != ESP_OK) return false;
    return i2s_channel_enable(s_tx) == ESP_OK;   // clocks on for the codec's power-up
}
// The clocks never stop here: with no MCLK line the codec's own clock IS the
// bit clock, and the codec does not come back from losing it mid-session.
// auto_clear keeps the line at zero between sounds.
static void i2sStart() {}
static void i2sStop()  {}
static void i2sZero()  {}   // auto_clear sends zeros once the data runs out
static void i2sWrite(const void* buf, size_t bytes) {
    size_t done = 0;
    i2s_channel_write(s_tx, buf, bytes, &done, portMAX_DELAY);
}
#else
static const i2s_port_t PORT    = I2S_NUM_0;
static bool i2sInstall() {
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
    if (i2s_driver_install(PORT, &cfg, 0, nullptr) != ESP_OK) return false;
    i2s_pin_config_t pins = {};
    pins.mck_io_num   = PIN_MCK >= 0 ? PIN_MCK : I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = PIN_BCK;
    pins.ws_io_num    = PIN_WS;
    pins.data_out_num = PIN_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    i2s_set_pin(PORT, &pins);
    i2s_zero_dma_buffer(PORT);
    return true;
}
static void i2sStart() { i2s_start(PORT); }
static void i2sStop()  { i2s_stop(PORT); }
static void i2sZero()  { i2s_zero_dma_buffer(PORT); }
static void i2sWrite(const void* buf, size_t bytes) {
    size_t done = 0;
    i2s_write(PORT, buf, bytes, &done, portMAX_DELAY);
}
#endif

static bool          s_ok      = false;
static bool          s_ampLow  = true;        // the amp plays with the pin LOW
static volatile bool s_hush    = false;
static QueueHandle_t s_q       = nullptr;
static TaskHandle_t  s_task    = nullptr;

// ---- the sounds ------------------------------------------------------------
// A sound is a short list of parts, played one after another unless a part is
// marked WITH, which starts it together with the part before (a clank under a
// spring); `off` nudges a part's start by that many ms, `gap` is silence after
// it. Every part has an attack (a, ms), a level held until h percent of the
// way (or a ringing fade, for a bell), a level lv in percent, and COS for a
// rounded fade instead of a straight one. The kinds:
//
//   TONE   a wave gliding through the pitches p[] (Hz, 0 = unused), with an
//          optional wobble: k0 depth, k1 rate (Hz), k2 the ms it fades over.
//          Two of its waves are not pitches: SWOOSH is noise through a
//          low-pass whose cutoff follows the glide, heard as air rushing past;
//          GRUNT is a saw roughened with noise, a nasal, raspy buzz
//   NOISE  the first Sound Lab's low-passed noise burst
//   VOICE  a throat buzzing through the pitches p[], shaped by the mouth
//          vowels v[]: k0 mouth size (under 1 = a bigger creature, 0 = 1),
//          k1 breath, k2 an "h" at the start (ms), k3 vibrato depth
//   BELL   two-operator FM, carrier p[0]: k0 the modulator's ratio, k1 its
//          brightness, k2 the ring (ms), k3 how fast the brightness goes, as a
//          share of the ring (0 = half)
//   AIR    noise through a band-pass gliding through the centres p[], k0 = Q
//
// Sound Lab 2 (2026-10-14) rendered these same recipes in the browser, sample
// for sample, and the picks below are the owner's. The ones kept from the
// first Sound Lab (2026-10-11) are written with BEEP and sound as before.
enum : uint8_t { TONE = 0, NOISE, VOICE, BELL, AIR };
enum : uint8_t { SQ = 0, TRI, SIN, SAW, SWOOSH, GRUNT };
enum : uint8_t { WITH = 1, COS = 2 };
enum : uint8_t { V_END = 0, OO, OH, AH, UH, EH, EE, IH, MM };
struct Part {
    uint8_t  kind, wave, flags;
    int16_t  off;
    uint16_t ms, gap;
    uint8_t  a, h, lv;
    uint16_t p[4];
    uint8_t  v[4];
    float    k[4];
};
struct Sound { const Part* p; uint8_t n; uint16_t echoMs; float fb, mix, gain; };

#define BEEP(f, to, ms, w, gap) { TONE, w, 0, 0, ms, gap, 4, 60, 100, { f, to } }
static const Part NEW_DEVICE[] = { BEEP(660, 0, 45, TRI, 25), BEEP(880, 0, 45, TRI, 25), BEEP(1100, 0, 60, TRI, 0) };
static const Part THROW[]      = { BEEP(300, 1500, 260, SIN, 0) };
static const Part LAND[]       = { { NOISE, 0, 0, 0, 30, 0, 4, 60, 100 }, BEEP(180, 70, 60, SQ, 0) };
static const Part TOAST_POP[]  = { BEEP(200, 1800, 80, SQ, 0) };
// A drone going overhead: the rush builds as it comes in, and falls away.
static const Part DRONE[]      = { BEEP(300, 2800, 220, SWOOSH, 0), BEEP(2800, 250, 320, SWOOSH, 0) };
// Oink oink: two short grunts, each sliding down, the second a touch higher.
static const Part OINK[]       = { BEEP(330, 190, 130, GRUNT, 45), BEEP(380, 210, 170, GRUNT, 0) };
#undef BEEP
// The Bigfoot whoop, twice, echoing through the trees.
static const Part WATCH_HIT[]  = {
    { VOICE, 0, COS, 0, 520, 120, 25, 70, 100, { 240, 300, 500 }, { OO, OO, OO, OH }, { 0, 0.12f } },
    { VOICE, 0, COS, 0, 560, 0,   25, 70, 100, { 240, 300, 520 }, { OO, OO, OO, OH }, { 0, 0.12f } } };
static const Part HEADS_UP[]   = {
    { VOICE, 0, COS, 0, 560, 0,   25, 70, 100, { 240, 300, 500 }, { OO, OO, OO, OH }, { 0, 0.12f } } };
// A toy winding down: a wobble that slows and sags.
static const Part LOW_BATT[]   = { { TONE, TRI, COS, 0, 650, 0, 5, 60, 100, { 520, 110 }, {}, { 0.12f, 9, 500 } } };
// Two glass chimes, overlapping.
static const Part MESSAGE[]    = {
    { BELL, 0, 0,    0,   450, 0, 2, 0, 100, { 1568 }, {}, { 3.5f, 1, 260 } },
    { BELL, 0, WITH, 110, 520, 0, 2, 0, 100, { 2093 }, {}, { 3.5f, 1, 320 } } };
// Two wooden knocks on the cave door.
static const Part VISITOR[]    = {
    { AIR,  0,   0,    0, 35, 0,   1, 10, 120, { 950 }, {}, { 2.5f } },
    { TONE, SIN, WITH, 0, 50, 110, 1, 10, 70,  { 230, 150 } },
    { AIR,  0,   0,    0, 35, 0,   1, 10, 120, { 900 }, {}, { 2.5f } },
    { TONE, SIN, 0,    0, 55, 0,   1, 10, 70,  { 220, 140 } } };
// A doorstop spring: one flick, and it wobbles down.
static const Part CLIPPY[]     = { { TONE, SIN, 0, 0, 380, 0, 2, 15, 100, { 420 }, {}, { 0.35f, 17, 150 } } };
// A round slide with a short echo, up for the cable and down without it.
static const Part PLUG_IN[]    = { { TONE, TRI, COS, 0, 260, 0, 10, 50, 100, { 220, 880 } } };
static const Part UNPLUG[]     = { { TONE, TRI, COS, 0, 300, 0, 10, 50, 100, { 880, 220 } } };
// T0@$TY lands: a tin box on the counter, and the lever rattles.
static const Part LAND_TOASTER[] = {
    { AIR,  0,   0,    0,  25,  0, 1, 10, 80,  { 2400 }, {}, { 3 } },
    { BELL, 0,   WITH, 0,  360, 0, 2, 0,  100, { 620 },  {}, { 2.76f, 2.5f, 180 } },
    { TONE, SIN, 0,    40, 220, 0, 2, 20, 35,  { 300 },  {}, { 0.25f, 22, 120 } } };
// The yeti lands: big and grumpy, "OOF." out loud.
static const Part LAND_YETI[]  = {
    { TONE,  SIN, 0,          0, 100, 0, 1, 15, 90,  { 100, 36 } },
    { VOICE, 0,   WITH | COS, 0, 300, 0, 8, 50, 100, { 110, 98, 80 }, { OH, OO }, { 0.85f, 0.25f } } };

#define SND(a) a, (uint8_t)(sizeof(a) / sizeof(a[0]))
static const Sound SOUNDS[(int)Id::COUNT] = {
    { SND(NEW_DEVICE) }, { SND(WATCH_HIT), 190, 0.38f, 0.45f, 0.75f }, { SND(LOW_BATT) }, { SND(MESSAGE) },
    { SND(VISITOR), 70, 0.2f, 0.25f }, { SND(THROW) }, { SND(LAND) }, { SND(CLIPPY) }, { SND(TOAST_POP) },
    { SND(PLUG_IN), 90, 0.3f, 0.35f }, { SND(UNPLUG), 90, 0.3f, 0.35f },
    { SND(LAND_TOASTER) }, { SND(LAND_YETI) }, { SND(HEADS_UP), 190, 0.38f, 0.45f, 0.75f },
    { SND(DRONE) }, { SND(OINK) },
};
#undef SND

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
#if defined(WAVESHARE_C5)
// The Waveshare board gets esp_codec_dev's own es8311 sequence, verbatim and
// in its order -- open(), set_fs() for 16 kHz 16-bit with use_mclk false,
// start() in DAC mode, unmute -- the sequence Waveshare's BSP runs.
static bool codecInitWs() {
    if (!wr(0x44, 0x08)) return false;      // open()
    wr(0x01, 0x30);
    wr(0x02, 0x00);
    wr(0x03, 0x10);
    wr(0x16, 0x24);
    wr(0x04, 0x10);
    wr(0x05, 0x00);
    wr(0x0B, 0x00);
    wr(0x0C, 0x00);
    wr(0x10, 0x1F);
    wr(0x11, 0x7F);
    wr(0x00, 0x80);                         // slave
    wr(0x01, 0xBF);                         // clocks on, MCLK from SCLK (no MCLK line)
    wr(0x13, 0x10);
    wr(0x1B, 0x0A);
    wr(0x1C, 0x6A);
    wr(0x44, 0x58);
    // set_fs(): the 4.096 MHz / 16 kHz row, pre_multi forced to x8 for SCLK.
    wr(0x02, (uint8_t)((rd(0x02) & 0x07) | 0x18));
    wr(0x05, 0x00);
    wr(0x03, 0x10);
    wr(0x04, 0x20);
    wr(0x07, (uint8_t)(rd(0x07) & 0xC0));
    wr(0x08, 0xFF);
    wr(0x06, (uint8_t)((rd(0x06) & 0xE0) | 0x03));
    wr(0x09, (uint8_t)(((rd(0x09) & ~0x1C) | 0x0C) & 0xFC));   // 16-bit, I2S
    wr(0x0A, (uint8_t)(((rd(0x0A) & ~0x1C) | 0x0C) & 0xFC));
    // start(), DAC mode.
    wr(0x00, 0x80);
    wr(0x01, 0xBF);
    wr(0x09, (uint8_t)(rd(0x09) & ~0x40));  // SDP in un-muted
    wr(0x0A, (uint8_t)(rd(0x0A) | 0x40));   // SDP out muted: no ADC here
    wr(0x17, 0xBF);
    wr(0x0E, 0x02);
    wr(0x12, 0x00);
    wr(0x14, 0x1A);
    wr(0x0D, 0x01);
    wr(0x15, 0x40);
    wr(0x37, 0x08);
    wr(0x45, 0x00);
    wr(0x31, (uint8_t)(rd(0x31) & 0x9F));   // unmute
    wr(0x32, 0xD8);                         // volume: loudness is in the samples
    return true;
}
#endif

static bool codecInit() {
#if defined(WAVESHARE_C5)
    return codecInitWs();
#else
    if (!wr(0x00, 0x1F)) return false;      // reset everything
    delay(20);
    wr(0x00, 0x00);
    wr(0x00, 0x80);                         // power on
    wr(0x01, 0x3F);                         // MCLK from the MCLK pin, every clock on
    wr(0x02, (uint8_t)(rd(0x02) & 0x07));   // pre-divider 1, multiplier 1x
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
#endif
}

// ---- the amplifier ---------------------------------------------------------
#if defined(WAVESHARE_C5)
// The enable is the helper chip's EXIO3, high = on. Waveshare's BSP turns it
// on once and leaves it, and so did this -- until the battery gauge started
// reading the same helper every two seconds and the alerts went quiet while
// the test sounds still played after SOUND AMP HIGH (2026-10-18). So it is
// sent again at the start of every sound (setAmp() is safe from this task),
// and never turned off: silence between sounds is the clocks' zeros.
static void amp(bool on) { if (on) WsC5::setAmp(true); }
#else
static void amp(bool on) { if (PIN_AMP >= 0) digitalWrite(PIN_AMP, (on == s_ampLow) ? LOW : HIGH); }
#endif

// ---- rendering -------------------------------------------------------------
// The whole sound is rendered into a float buffer in PSRAM first (every board
// with a speaker has it; the longest sound, the whoop with its echo, is about
// 150 KB), parts added on top of each other, then the echo and a soft limiter,
// then it streams out 256 frames at a time, the same sample in both slots
// (the codec takes the left). Rendering the longest one takes a few ms.
static const float TAU_F = 6.2831853f;

// Mouth shapes: the three loudest resonances of a throat, in Hz, and their widths.
static const uint16_t VOWEL[][3] = {
    { 0, 0, 0 }, { 300, 870, 2240 }, { 570, 840, 2410 }, { 730, 1090, 2440 }, { 640, 1190, 2390 },
    { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 390, 1990, 2550 }, { 260, 1050, 2200 } };
static const float BANDW[3] = { 80, 110, 160 };
static const float FORM[3]  = { 1.0f, 0.55f, 0.28f };

// A seeded noise source, so a sound comes out the same every time: -1..1.
struct Rng { uint32_t s; float next() { s = s * 1664525u + 1013904223u; return (float)(s >> 8) / 8388608.0f - 1.0f; } };

// A band-pass with a peak of exactly 1 (the Audio EQ Cookbook's), for the
// mouth and for filtered air. c = b0, b2, a1, a2; z = x1, x2, y1, y2.
static void bpSet(float* c, float f, float bw) {
    if (f > RATE * 0.45f) f = RATE * 0.45f;
    const float w = TAU_F * f / (float)RATE, q = fmaxf(0.5f, f / bw), al = sinf(w) / (2 * q), a0 = 1 + al;
    c[0] = al / a0; c[1] = -al / a0; c[2] = -2 * cosf(w) / a0; c[3] = (1 - al) / a0;
}
static float bpRun(const float* c, float* z, float x) {
    const float y = c[0] * x + c[1] * z[1] - c[2] * z[2] - c[3] * z[3];
    z[1] = z[0]; z[0] = x; z[3] = z[2]; z[2] = y;
    return y;
}

// How many points a list holds: pitches end at the first 0, vowels at V_END.
static int pts(const uint16_t* p) { int n = 0; while (n < 4 && p[n]) n++; return n ? n : 1; }
static int pts(const uint8_t* v)  { int n = 0; while (n < 4 && v[n]) n++; return n ? n : 1; }
// Glide along the points, u = 0..1 through the part, in equal musical steps.
static float along(const uint16_t* p, float u) {
    const int n = pts(p);
    if (n == 1) return (float)p[0];
    const float x = u * (n - 1);
    const int i = x >= n - 1 ? n - 2 : (int)x;
    return (float)p[i] * powf((float)p[i + 1] / (float)p[i], x - i);
}

static int attackFrames(const Part& st) { const int a = st.a * (int)RATE / 1000; return a > 0 ? a : 1; }
// Attack, then level until h percent of the way, then down to nothing.
static float envLin(const Part& st, int n, int N) {
    const int a = attackFrames(st), h = N * st.h / 100;
    if (n < a) return (float)n / a;
    if (n < h) return 1.0f;
    const float r = (float)(N - n) / (float)(N - h);
    return (st.flags & COS) ? 0.5f - 0.5f * cosf(3.1415927f * r) : r;
}
// Attack, then a ring that dies away, with a short fade at the very end.
static float envRing(const Part& st, int n, int N, float ringS) {
    const int a = attackFrames(st), tail = RATE * 12 / 1000;
    if (n < a) return (float)n / a;
    float e = expf(-(float)(n - a) / (float)RATE / ringS);
    if (n > N - tail) e *= (float)(N - n) / tail;
    return e;
}

static void renderTone(const Part& st, float* out, int N, uint32_t& lcg, float& lp) {
    const float lv = st.lv / 100.0f;
    float ph = 0;
    for (int n = 0; n < N; n++) {
        const float t = (float)n / RATE;
        float f = along(st.p, (float)n / N);
        if (st.k[0] > 0) f *= 1 + st.k[0] * sinf(TAU_F * st.k[1] * t) * (st.k[2] > 0 ? expf(-t * 1000 / st.k[2]) : 1);
        float v;
        if (st.kind == NOISE) {
            lcg = lcg * 1664525u + 1013904223u;
            lp += 0.25f * (((int32_t)(lcg >> 8) / 8388608.0f) - 1.0f - lp);   // one-pole low-pass
            v = lp * 0.9f;
        } else switch (st.wave) {
            case SQ:  v = ph < 0.5f ? 0.35f : -0.35f; break;
            case TRI: v = (ph < 0.5f ? (ph * 4.0f - 1.0f) : (3.0f - ph * 4.0f)) * 0.7f; break;
            case SIN: v = sinf(ph * TAU_F) * 0.7f; break;
            case SWOOSH: {
                lcg = lcg * 1664525u + 1013904223u;
                const float r = ((int32_t)(lcg >> 8) / 8388608.0f) - 1.0f;
                float a = f / 4000.0f;                                   // the cutoff rides the glide
                if (a < 0.03f) a = 0.03f; else if (a > 0.9f) a = 0.9f;
                lp += a * (r - lp);
                v = lp * 1.1f;
                break;
            }
            case GRUNT: {
                lcg = lcg * 1664525u + 1013904223u;
                const float r = ((int32_t)(lcg >> 8) / 8388608.0f) - 1.0f;
                v = (ph * 2.0f - 1.0f) * 0.45f + r * 0.12f;
                break;
            }
            default:  v = (ph * 2.0f - 1.0f) * 0.4f; break;
        }
        out[n] += v * envLin(st, n, N) * lv;
        ph += f / RATE;
        if (ph >= 1.0f) ph -= 1.0f;
    }
}

// A throat: a Rosenberg glottal pulse at the pitch, its slope (what the mouth
// hears) through three resonances that glide between the vowels, plus breath.
static void renderVoice(const Part& st, float* out, int N) {
    Rng r = { 777 };
    const float sz = st.k[0] > 0 ? st.k[0] : 1.0f, br = st.k[1], vd = st.k[3], lv = st.lv / 100.0f;
    const int h0 = (int)(st.k[2] * RATE / 1000);
    const int nv = pts(st.v);
    float ph = 0, prev = 0, jit = 0, c[3][4] = {}, z[3][4] = {};
    for (int n = 0; n < N; n++) {
        const float u = (float)n / N, t = (float)n / RATE;
        jit += (r.next() * 0.5f - jit) * 0.02f;
        const float f0 = along(st.p, u) * (1 + 0.012f * sinf(TAU_F * 5.2f * t) + 0.01f * jit + vd * sinf(TAU_F * 6 * t));
        ph += f0 / RATE;
        if (ph >= 1.0f) ph -= 1.0f;
        const float pu = ph < 0.4f ? 0.5f - 0.5f * cosf(3.1415927f * ph / 0.4f)
                       : ph < 0.56f ? cosf(1.5707963f * (ph - 0.4f) / 0.16f) : 0.0f;
        float e = (pu - prev) * RATE / (f0 * 7.85f);
        prev = pu;
        const float hs = n < h0 ? 1.0f - (float)n / h0 : 0.0f;    // the h of "hey"
        e = e * (1 - hs * 0.85f) + r.next() * (br * 0.35f + hs * 0.9f);
        if ((n & 15) == 0) {
            const float x = u * (nv - 1);
            const int i = nv < 2 ? 0 : (x >= nv - 1 ? nv - 2 : (int)x);
            const float fr = x - i;
            const uint16_t* A = VOWEL[st.v[i]];
            const uint16_t* B = VOWEL[st.v[nv < 2 ? i : i + 1]];
            for (int k = 0; k < 3; k++) bpSet(c[k], (A[k] + (B[k] - A[k]) * fr) * sz, BANDW[k]);
        }
        float y = 0;
        for (int k = 0; k < 3; k++) y += bpRun(c[k], z[k], e) * FORM[k];
        out[n] += y * 1.7f * envLin(st, n, N) * lv;
    }
}

static void renderBell(const Part& st, float* out, int N) {
    const float fc = st.p[0], fm = fc * st.k[0], I = st.k[1], ring = st.k[2] / 1000.0f;
    const float ie = ring * (st.k[3] > 0 ? st.k[3] : 0.5f), lv = st.lv / 100.0f;
    for (int n = 0; n < N; n++) {
        const float t = (float)n / RATE;
        const float v = sinf(TAU_F * fc * t + I * expf(-t / ie) * sinf(TAU_F * fm * t));
        out[n] += v * 0.55f * envRing(st, n, N, ring) * lv;
    }
}

static void renderAir(const Part& st, float* out, int N) {
    Rng r = { 4242 };
    const float q = st.k[0] > 0 ? st.k[0] : 1.0f, lv = st.lv / 100.0f;
    float c[4] = {}, z[4] = {};
    for (int n = 0; n < N; n++) {
        if ((n & 15) == 0) { const float fc = along(st.p, (float)n / N); bpSet(c, fc, fc / q); }
        out[n] += bpRun(c, z, r.next()) * 1.4f * envLin(st, n, N) * lv;
    }
}

static void writeFrames(int16_t* buf, int frames) {
    i2sWrite(buf, (size_t)frames * 4);
}

static void silence(int16_t* buf, int ms) {
    memset(buf, 0, CHUNK * 4);
    for (int left = ms * (int)RATE / 1000; left > 0; left -= CHUNK)
        writeFrames(buf, left < CHUNK ? left : CHUNK);
}

static void* bigAlloc(size_t bytes) {
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(bytes);
}

static void playNow(Id id) {
    const Sound& snd = SOUNDS[(int)id];
    // Lay the parts out in time, the way the Lab did.
    const uint8_t parts = snd.n < 8 ? snd.n : 8;
    int start[8], t = 0, last = 0;
    for (uint8_t i = 0; i < parts; i++) {
        const Part& st = snd.p[i];
        start[i] = ((st.flags & WITH) ? last : t) + st.off;
        last = start[i];
        if (start[i] + st.ms + st.gap > t) t = start[i] + st.ms + st.gap;
    }
    const int total = (t + snd.echoMs * 6 + 30) * (int)RATE / 1000;
    float* mix = (float*)bigAlloc((size_t)total * sizeof(float));
    if (!mix) { Serial.println("[sound] no memory for the sound"); return; }
    memset(mix, 0, (size_t)total * sizeof(float));

    uint32_t lcg = 0x5EED1234u;
    float lp = 0;
    for (uint8_t i = 0; i < parts; i++) {
        const Part& st = snd.p[i];
        const int at = start[i] * (int)RATE / 1000, N = st.ms * (int)RATE / 1000;
        if (at < 0 || at + N > total) continue;
        switch (st.kind) {
            case VOICE: renderVoice(st, mix + at, N); break;
            case BELL:  renderBell(st, mix + at, N);  break;
            case AIR:   renderAir(st, mix + at, N);   break;
            default:    renderTone(st, mix + at, N, lcg, lp); break;
        }
    }
    if (snd.echoMs) {
        const int D = snd.echoMs * (int)RATE / 1000;
        float* line = (float*)bigAlloc((size_t)D * sizeof(float));
        if (line) {
            memset(line, 0, (size_t)D * sizeof(float));
            for (int n = 0, i = 0; n < total; n++) {
                const float z = line[i];
                line[i] = mix[n] + z * snd.fb;
                mix[n] += z * snd.mix;
                if (++i == D) i = 0;
            }
            free(line);
        }
    }

    // The percent squared: the ear hears loudness on a curve, and squaring
    // the slider gives the bottom half of it real steps instead of a jump.
    const float pct = (float)Settings::soundVolume() / 100.0f;
    const float vol = pct * pct * 32000.0f, g = snd.gain > 0 ? snd.gain : 1.0f;
    int16_t buf[CHUNK * 2];

    i2sStart();
    silence(buf, 24);         // clocks and zeros before the amp wakes: no pop
    amp(true);
    silence(buf, 16);
    for (int at = 0; at < total; ) {
        const int n = (total - at) < CHUNK ? (total - at) : CHUNK;
        for (int i = 0; i < n; i++, at++) {
            // A soft limiter: untouched under 0.7, then rounding off toward 1.
            const float v = mix[at] * g, a = fabsf(v);
            const float o = a < 0.7f ? v : copysignf(0.7f + 0.3f * tanhf((a - 0.7f) / 0.3f), v);
            const int16_t s = (int16_t)(o * vol);
            buf[i * 2] = s; buf[i * 2 + 1] = s;
        }
        writeFrames(buf, n);
    }
    free(mix);

    silence(buf, 16);
    amp(false);
    silence(buf, 24);         // let the DMA drain before the clocks stop
    i2sZero();
    i2sStop();
}

// Core 1 where there is one; the C5 has a single core.
static void soundTask(void*);
static void startTask() {
    s_q = xQueueCreate(4, sizeof(uint8_t));
    xTaskCreatePinnedToCore(soundTask, "sound", 4096, nullptr, 2, &s_task, portNUM_PROCESSORS > 1 ? 1 : 0);
    s_ok = s_q && s_task;
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

    if (!i2sInstall()) { Serial.println("[sound] I2S driver refused"); return; }
    // The codec's power-up wants its clocks running; the driver starts them
    // on install. Set the codec up, then park the clocks until a sound.
#if SOUND_NO_CODEC
    i2sStop();
    startTask();
    Serial.printf("[sound] I2S amp ready: BCK %d WS %d DOUT %d%s\n", PIN_BCK, PIN_WS, PIN_DOUT, s_ok ? "" : " -- task failed");
#else
    const bool ok = codecInit();
    const uint8_t id1 = rd(0xFD), id2 = rd(0xFE);
    i2sStop();
    if (!ok) { Serial.println("[sound] ES8311 setup failed"); return; }
#if defined(WAVESHARE_C5)
    WsC5::setAmp(true);
#endif
    startTask();
    Serial.printf("[sound] ES8311 id %02X%02X ready: MCK %d BCK %d WS %d DOUT %d, amp on GPIO%d%s\n",
                  id1, id2, PIN_MCK, PIN_BCK, PIN_WS, PIN_DOUT, PIN_AMP, s_ok ? "" : " -- task failed");
#endif
}

void play(Id id, bool sample) {
    if (!s_ok || id >= Id::COUNT) return;
    if (!sample) {
        const uint8_t mode = Settings::soundMode();          // 0 OFF, 1 ALERTS, 2 ALL
        const bool alert = id == Id::NEW_DEVICE || id == Id::WATCH_HIT || id == Id::LOW_BATTERY
                        || id == Id::HEADS_UP || id == Id::DRONE || id == Id::OINK;
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

#if defined(WAVESHARE_C5)
// The bench here: SOUND AMP HIGH turns the amplifier on, SOUND AMP LOW off.
void ampLow(bool activeLow) { WsC5::setAmp(!activeLow); }
#else
void ampLow(bool activeLow) { s_ampLow = activeLow; amp(false); }
#endif

}  // namespace Sound
#endif  // SQW_SOUND
