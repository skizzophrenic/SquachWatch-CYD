// SquachWatch-CYD: shake and tap detection on the BMI270. See shake.h.
#include "shake.h"

#if defined(SQW_SMALL)
#include <Arduino.h>
#include <Wire.h>
#include "bmi270_config.h"

namespace Shake {
namespace {

const uint8_t ADDR = 0x68;

// Registers (BMI270 datasheet).
const uint8_t REG_CHIP_ID     = 0x00;   // 0x24
const uint8_t REG_INT_STATUS  = 0x21;   // INTERNAL_STATUS: 1 = config loaded
const uint8_t REG_FIFO_LEN0   = 0x24;
const uint8_t REG_FIFO_DATA   = 0x26;
const uint8_t REG_ACC_CONF    = 0x40;
const uint8_t REG_ACC_RANGE   = 0x41;
const uint8_t REG_FIFO_CONF0  = 0x48;
const uint8_t REG_FIFO_CONF1  = 0x49;
const uint8_t REG_INIT_CTRL   = 0x59;
const uint8_t REG_INIT_ADDR0  = 0x5B;
const uint8_t REG_INIT_DATA   = 0x5E;
const uint8_t REG_PWR_CONF    = 0x7C;
const uint8_t REG_PWR_CTRL    = 0x7D;
const uint8_t REG_CMD         = 0x7E;

// The accelerometer runs at 200 Hz into the FIFO (5 ms a sample), +-8 g so a
// hard knock does not clip: 4096 per g.
const uint32_t SAMPLE_US = 5000;
const float    LSB_PER_G = 4096.0f;

// The tuning, from a bench log (2026-11-03): a knock on the case is one to
// three spikes of 1.2 to 3.7 g, each 5 to 50 ms, inside about 200 ms (the
// case rings, and people knock twice). A shake is many, for longer.
//
// An "excursion" is a run of samples more than EXC_MG from 1 g. A TAP is a
// group of one to TAP_MAX_SPIKES short, sharp excursions with TAP_QUIET_MS
// of calm after the last, so a knock and a double knock are each one poke.
// It is reported that long after the knock. A SHAKE is SHAKE_EXCURSIONS
// big ones inside SHAKE_WINDOW_MS, and it swallows any tap in progress; then
// nothing for COOLDOWN_MS so one shake is one event.
const int32_t  EXC_MG              = 500;
const int32_t  TAP_PEAK_MG         = 1500;
const uint32_t TAP_MAX_MS          = 80;    // a longer excursion is a hand moving it
const uint8_t  TAP_MAX_SPIKES      = 3;
const uint32_t TAP_QUIET_MS        = 300;
const int32_t  SHAKE_PEAK_MG       = 900;
// A WIGGLE counts every excursion: 4 of them in the shake window. From the
// log, a person waking the fob jiggles it at 0.5 to 0.8 g for a second or two
// before ever reaching a real shake, and waited that long for the screen.
const uint8_t  WIGGLE_EXCURSIONS   = 4;
const uint32_t WIGGLE_REPEAT_MS    = 1000;
const uint8_t  SHAKE_EXCURSIONS    = 4;
const uint32_t SHAKE_WINDOW_MS     = 800;
const uint32_t COOLDOWN_MS         = 1200;

bool     s_ok = false, s_trace = false;
uint32_t s_traceAt = 0, s_traceN = 0;   // the trace's once-a-second heartbeat
int32_t  s_traceMg = 0;
uint32_t s_cooldownUntil = 0;
// The sample clock: the FIFO has no timestamps, so samples are spaced
// SAMPLE_US apart, anchored to when they were read.
uint64_t s_tUs = 0;

// The excursion in progress, if any.
bool     s_inExc = false;
uint32_t s_excStart = 0;
int32_t  s_excPeak = 0;
// Recent excursions, for SHAKE (start times of the big ones).
uint32_t s_bigAt[8];
uint8_t  s_bigN = 0;
uint32_t s_lastExcEnd = 0;           // when the last excursion of any size ended
uint32_t s_anyAt[8];                 // start times of recent excursions of any size, for WIGGLE
uint8_t  s_anyN = 0;
uint32_t s_wiggleAt = 0;
// A knock in progress: its sharp spikes so far, and when the last one ended.
uint8_t  s_tapSpikes = 0;
uint32_t s_tapAt = 0;
bool     s_tapPending = false;

bool wr(uint8_t reg, uint8_t v) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg); Wire.write(v);
    return Wire.endTransmission() == 0;
}
int rd(uint8_t reg) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(ADDR, (uint8_t)1) != 1) return -1;
    return Wire.read();
}
bool rdN(uint8_t reg, uint8_t* buf, uint8_t n) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(ADDR, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

// The 8 KB file goes up in pieces: the Arduino I2C buffer is far smaller.
// INIT_ADDR is in 16-bit words, split 4 bits / 8 bits across two registers.
bool uploadConfig() {
    const size_t CHUNK = 64;
    for (size_t at = 0; at < sizeof(BMI270_CONFIG_FILE); at += CHUNK) {
        const size_t word = at / 2;
        Wire.beginTransmission(ADDR);
        Wire.write(REG_INIT_ADDR0);
        Wire.write((uint8_t)(word & 0x0F));
        Wire.write((uint8_t)(word >> 4));
        if (Wire.endTransmission() != 0) return false;
        Wire.beginTransmission(ADDR);
        Wire.write(REG_INIT_DATA);
        const size_t n = min(CHUNK, sizeof(BMI270_CONFIG_FILE) - at);
        Wire.write(BMI270_CONFIG_FILE + at, n);
        if (Wire.endTransmission() != 0) return false;
    }
    return true;
}

// One sample, at time t (ms). Returns an event if this sample finishes one.
Event feed(uint32_t t, int32_t devMg) {
    const int32_t a = devMg < 0 ? -devMg : devMg;
    Event ev = Event::NONE;
    if (a > EXC_MG) {
        if (!s_inExc) { s_inExc = true; s_excStart = t; s_excPeak = 0; }
        if (a > s_excPeak) s_excPeak = a;
        return ev;
    }
    if (s_inExc) {
        // An excursion just ended.
        s_inExc = false;
        const uint32_t len = t - s_excStart;
        if (s_trace) Serial.printf("[imu] excursion %lu ms, peak %d mg\n", (unsigned long)len, (int)s_excPeak);
        {
            uint8_t k = 0;
            for (uint8_t i = 0; i < s_anyN; i++) if (t - s_anyAt[i] <= SHAKE_WINDOW_MS) s_anyAt[k++] = s_anyAt[i];
            s_anyN = k;
            if (s_anyN < 8) s_anyAt[s_anyN++] = s_excStart;
        }
        if (s_excPeak >= SHAKE_PEAK_MG) {
            uint8_t k = 0;
            for (uint8_t i = 0; i < s_bigN; i++) if (t - s_bigAt[i] <= SHAKE_WINDOW_MS) s_bigAt[k++] = s_bigAt[i];
            s_bigN = k;
            if (s_bigN < 8) s_bigAt[s_bigN++] = s_excStart;
            if (s_bigN >= SHAKE_EXCURSIONS) {
                s_bigN = 0;
                s_tapPending = false;
                ev = Event::SHAKE;
            }
        }
        if (ev == Event::NONE) {
            if (len > TAP_MAX_MS) {
                s_tapPending = false;              // a hand moving it, not a knock
            } else if (s_excPeak >= TAP_PEAK_MG) {
                if (!s_tapPending) { s_tapPending = true; s_tapSpikes = 0; }
                s_tapSpikes++;
                s_tapAt = t;
            }
        }
        s_lastExcEnd = t;
    }
    if (ev == Event::NONE && s_tapPending && t - s_lastExcEnd >= TAP_QUIET_MS) {
        s_tapPending = false;
        if (s_tapSpikes <= TAP_MAX_SPIKES) ev = Event::TAP;
    }
    // A wiggle does not swallow a tap (a knock rings into several small
    // excursions): it is its own event, at most once a second.
    if (ev == Event::NONE && s_anyN >= WIGGLE_EXCURSIONS && t - s_wiggleAt >= WIGGLE_REPEAT_MS) {
        s_wiggleAt = t;
        s_anyN = 0;
        ev = Event::WIGGLE;
    }
    return ev;
}

}  // namespace

bool begin() {
    s_ok = false;
    if (rd(REG_CHIP_ID) != 0x24) {
        Serial.println("[imu] no BMI270 at 0x68: no shake or tap");
        return false;
    }
    wr(REG_CMD, 0xB6);                       // soft reset
    delay(5);
    wr(REG_PWR_CONF, 0x00);                  // advanced power save off, for the upload
    delayMicroseconds(500);
    wr(REG_INIT_CTRL, 0x00);
    const bool up = uploadConfig();
    wr(REG_INIT_CTRL, 0x01);
    int st = 0;
    for (int i = 0; i < 30 && (st = rd(REG_INT_STATUS)) >= 0 && (st & 0x0F) != 0x01; i++) delay(2);
    wr(REG_PWR_CTRL, 0x04);                  // accelerometer only
    wr(REG_ACC_CONF, 0xA9);                  // 200 Hz, normal mode, performance filter
    wr(REG_ACC_RANGE, 0x02);                 // +-8 g
    wr(REG_FIFO_CONF0, 0x00);                // keep the newest when full
    wr(REG_FIFO_CONF1, 0x40);                // accelerometer frames, headerless
    wr(REG_CMD, 0xB0);                       // flush
    s_tUs = (uint64_t)micros();
    s_ok = up && (st & 0x0F) == 0x01;
    Serial.printf("[imu] BMI270 %s (config %s, status 0x%02X)\n",
                  s_ok ? "ready: shake and tap are on" : "NOT ready", up ? "sent" : "FAILED", st);
    return s_ok;
}

bool present() { return s_ok; }
void setTrace(bool on) { s_trace = on; }

Event poll(uint32_t now) {
    if (!s_ok) return Event::NONE;
    uint8_t lb[2];
    if (!rdN(REG_FIFO_LEN0, lb, 2)) return Event::NONE;
    uint32_t len = (uint32_t)(lb[0] | ((lb[1] & 0x3F) << 8));
    len -= len % 6;
    // A long stall (a WiFi update, a flash write): too old to mean anything.
    if (len > 6 * 200) { wr(REG_CMD, 0xB0); s_tUs = (uint64_t)micros(); return Event::NONE; }
    // The samples in the FIFO end now; walk them forward from there.
    const uint32_t nSamples = len / 6;
    const uint64_t endUs = (uint64_t)micros();
    uint64_t tUs = endUs - (uint64_t)nSamples * SAMPLE_US;
    if (tUs < s_tUs) tUs = s_tUs;
    Event out = Event::NONE;
    uint8_t buf[120];
    while (len) {
        const uint8_t n = (uint8_t)min<uint32_t>(len, sizeof(buf));
        if (!rdN(REG_FIFO_DATA, buf, n)) break;
        len -= n;
        for (uint8_t i = 0; i + 6 <= n; i += 6) {
            const int32_t x = (int16_t)(buf[i] | (buf[i + 1] << 8));
            const int32_t y = (int16_t)(buf[i + 2] | (buf[i + 3] << 8));
            const int32_t z = (int16_t)(buf[i + 4] | (buf[i + 5] << 8));
            if (x == -32768 && y == -32768 && z == -32768) continue;   // the FIFO's "empty" frame
            const float mag = sqrtf((float)(x * x + y * y + z * z)) * 1000.0f / LSB_PER_G;
            tUs += SAMPLE_US;
            s_traceN++; s_traceMg = (int32_t)mag;
            const Event e = feed((uint32_t)(tUs / 1000), (int32_t)mag - 1000);
            if (e != Event::NONE && out == Event::NONE) out = e;
        }
    }
    s_tUs = tUs;
    if (s_trace && now - s_traceAt >= 1000) {
        Serial.printf("[imu] %lu samples/s, |a| %d mg\n", (unsigned long)s_traceN, (int)s_traceMg);
        s_traceAt = now; s_traceN = 0;
    }
    // A tap still waiting for quiet when the samples stop: let time pass.
    if (out == Event::NONE && s_tapPending && !s_inExc && now - s_lastExcEnd >= TAP_QUIET_MS) {
        s_tapPending = false;
        if (s_tapSpikes <= TAP_MAX_SPIKES) out = Event::TAP;
    }
    if (out == Event::WIGGLE) return out;     // no cooldown: it only ever wakes
    if (out != Event::NONE) {
        if ((int32_t)(now - s_cooldownUntil) < 0) return Event::NONE;
        s_cooldownUntil = now + (out == Event::SHAKE ? COOLDOWN_MS : 300);
        Serial.println(out == Event::SHAKE ? "[imu] SHAKE" : "[imu] TAP");
    }
    return out;
}

}  // namespace Shake

#else
namespace Shake {
bool begin() { return false; }
bool present() { return false; }
Event poll(uint32_t) { return Event::NONE; }
void setTrace(bool) {}
}
#endif
