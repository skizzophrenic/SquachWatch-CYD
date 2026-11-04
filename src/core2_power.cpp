// SquachWatch-CYD -- the M5Stack Core2's power chip. See core2_power.h.
#include "core2_power.h"

#if defined(CORE2)
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "clock.h"

namespace Core2Power {

static const uint8_t ADDR = 0x34;
static int s_chip = 0;

static bool rd(uint8_t reg, uint8_t& v) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(ADDR, (uint8_t)1) != 1) return false;
    v = (uint8_t)Wire.read();
    return true;
}
static bool wr(uint8_t reg, uint8_t v) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}
// M5GFX's register triples: the register becomes (old & mask) | bits.
static void wrMasked(uint8_t reg, uint8_t bits, uint8_t mask) {
    uint8_t v = 0;
    if (mask != 0 && !rd(reg, v)) return;
    wr(reg, (uint8_t)((v & mask) | bits));
}
struct Triple { uint8_t reg, bits, mask; };
static void apply(const Triple* t, int n) {
    for (int i = 0; i < n; i++) wrMasked(t[i].reg, t[i].bits, t[i].mask);
}

bool begin() {
    // Chip selects high before anything can clock the shared bus: the panel's
    // (5) and the microSD's (4).
    pinMode(5, OUTPUT); digitalWrite(5, HIGH);
    pinMode(4, OUTPUT); digitalWrite(4, HIGH);

    Wire.begin(21, 22, 400000);
    uint8_t id = 0;
    if (!rd(0x03, id)) { Serial.println("[pmu] no answer at 0x34 -- screen will stay dark"); return false; }
    if (id == 0x03)      s_chip = 192;
    else if (id == 0x4A) s_chip = 2101;
    else { Serial.printf("[pmu] unknown chip id 0x%02X at 0x34\n", id); return false; }

    if (s_chip == 192) {
        // LDO2 3.3 V is the LCD and microSD supply; GPIO4 is the LCD (and
        // touch) reset, driven as an output.
        static const Triple first[]  = { {0x95, 0x84, 0x72}, {0x28, 0xF0, 0xFF}, {0x12, 0x04, 0xFF} };
        static const Triple reset[]  = { {0x96, 0x00, 0xFD} };
        static const Triple second[] = { {0x96, 0x02, 0xFF} };
        apply(first, 3);
        apply(second, 1);
        delay(5);
        apply(reset, 1);
        delay(1);
        apply(second, 1);
        delay(10);
        wrMasked(0x12, 0x00, 0xF7);   // LDO3 off: the vibration motor
        wr(0x93, 0x05);               // GPIO2 low: the speaker amplifier off
        wr(0x82, 0xFF);               // every ADC channel on, for the battery
    } else {
        // ALDO2 is the LCD and touch reset line, ALDO4 the LCD, touch and
        // microSD supply; ALDO3 (the speaker amplifier) and DLDO1 (the
        // vibration motor) stay off. DCDC1 is the ESP32's own rail.
        static const Triple first[]  = { {0x23, 0x00, 0xE0}, {0x90, 0x08, 0x7B}, {0x80, 0x05, 0xFF},
                                         {0x82, 0x12, 0x00}, {0x84, 0x6A, 0x00} };
        static const Triple reset[]  = { {0x90, 0x00, 0xFD} };
        static const Triple second[] = { {0x90, 0x02, 0xFF} };
        apply(first, 5);
        apply(second, 1);
        delay(5);
        apply(reset, 1);
        delay(1);
        apply(second, 1);
        delay(10);
        wr(0x68, 0x01);   // battery detection on
        wr(0x30, 0x0F);   // ADC on, for the voltage reads
    }
    Serial.printf("[pmu] AXP%d up\n", s_chip);
    return true;
}

int chip() { return s_chip; }

void setBacklight(uint8_t duty) {
    if (s_chip == 192) {
        // DCDC3 is the backlight, 2.5..3.3 V in 25 mV steps (reg 0x27, top bit
        // is not ours).
        if (duty) {
            wrMasked(0x27, (uint8_t)((duty >> 3) + 72), 0x80);
            wrMasked(0x12, 0x02, 0xFF);
        } else {
            wrMasked(0x12, 0x00, 0xFD);
        }
    } else if (s_chip == 2101) {
        // BLDO1 is the backlight, 0.5 V + 100 mV a step (reg 0x96).
        if (duty) {
            wr(0x96, (uint8_t)((duty + 641) >> 5));
            wrMasked(0x90, 0x10, 0xFF);
        } else {
            wrMasked(0x90, 0x00, 0xEF);
        }
    }
}

static uint16_t readMv() {
    uint8_t a = 0, b = 0;
    if (s_chip == 192) {
        // 12 bits, 1.1 mV a count: reg 0x78 is the high byte, 0x79 the low nibble.
        if (!rd(0x78, a) || !rd(0x79, b)) return 0;
        return (uint16_t)((((uint16_t)a << 4) | (b & 0x0F)) * 11 / 10);
    }
    // 14 bits, 1 mV a count.
    if (!rd(0x34, a) || !rd(0x35, b)) return 0;
    return (uint16_t)((((uint16_t)(a & 0x3F)) << 8) | b);
}

uint16_t batteryMv() { return s_chip ? readMv() : 0; }

bool battery(uint8_t& pct, bool& charging) {
    if (!s_chip) return false;
    static uint32_t at = 0;
    static bool     have = false, chg = false;
    static int      p = 0;
    const uint32_t now = millis();
    if (!at || now - at >= 2000) {
        at = now ? now : 1;
        uint8_t st0 = 0, st1 = 0;
        have = false;
        if (s_chip == 192) {
            if (rd(0x00, st0) && rd(0x01, st1)) {
                have = (st1 & 0x20) != 0;                 // a cell is fitted
                chg  = (st0 & 0x04) != 0 || (st0 & 0x20); // charging, or the cable is in
                const int v = (int)(readMv() * 10 / 11);  // back to raw counts, M5Unified's scale
                p = (v > 3150) ? (int)((v - 3075) * 0.16f) : (v > 2690) ? (int)((v - 2690) * 0.027f) : 0;
            }
        } else {
            if (rd(0x00, st0)) {
                have = (st0 & 0x08) != 0;                 // a cell is fitted
                chg  = (st0 & 0x20) != 0;                 // the cable is in
                uint8_t lvl = 0;
                if (rd(0xA4, lvl)) p = lvl; else have = false;
            }
        }
        if (p < 0) p = 0;
        if (p > 100) p = 100;
    }
    if (!have) return false;
    pct = (uint8_t)p;
    charging = chg;
    return true;
}

void speaker(bool on) {
    if (s_chip == 192) {
        wr(0x93, on ? 0x06 : 0x05);   // floating (the board pulls it up) or low
    } else if (s_chip == 2101) {
        if (on) { wr(0x94, 28); wrMasked(0x90, 0x04, 0xFF); }   // ALDO3 3.3 V on
        else    wrMasked(0x90, 0x00, 0xFB);
    }
}

// The M-Bus 5 V pin, which feeds a stacked module (the GNSS module). Sequences
// are M5Unified's setExtOutput/setUSBOutput as best recalled -- UNVERIFIED on
// hardware: read the [gps] lines to see whether the module came up.
void busPower(bool on) {
    if (s_chip == 192) {
        wrMasked(0x10, on ? 0x04 : 0x00, 0xFB);   // EXTEN: the 5 V boost
        wrMasked(0x91, on ? 0xF0 : 0x00, 0x0F);   // GPIO0 LDO output 3.3 V
        wrMasked(0x90, on ? 0x02 : 0x07, 0xF8);   // GPIO0: LDO output / floating
    } else if (s_chip == 2101) {
        wrMasked(0x90, on ? 0x40 : 0x00, 0xBF);   // BLDO2 (the bus 5 V enable)
        if (on) wr(0x97, 28);                     // BLDO2 3.3 V
    }
    Serial.printf("[pmu] bus 5V %s\n", on ? "on" : "off");
}

// ---- the clock chip ---------------------------------------------------------
// The same register map and the same "mark" as the T-Watch's PCF8563 code in
// main.cpp: our mark in the minute-alarm register, so a chip holding somebody
// else's time (M5's factory firmware leaves local time in it) is not believed.
static const uint8_t RTC_ADDR = 0x51;
static const uint8_t RTC_MARK = 0xC2;
static bool s_rtcLoading = false;

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int32_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}
static void civilFromDays(int32_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (int)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
}

static void rtcWrite(uint32_t epoch) {
    if (s_rtcLoading || !s_chip) return;
    int y; unsigned mo, d;
    civilFromDays((int32_t)(epoch / 86400u), y, mo, d);
    const uint32_t sod = epoch % 86400u;
    Wire.beginTransmission(RTC_ADDR);
    Wire.write((uint8_t)0x00);
    Wire.write((uint8_t)0x00);                              // control 1: running
    Wire.write((uint8_t)0x00);                              // control 2: no alarms
    Wire.write(bin2bcd((uint8_t)(sod % 60)));               // seconds, VL cleared
    Wire.write(bin2bcd((uint8_t)(sod / 60 % 60)));
    Wire.write(bin2bcd((uint8_t)(sod / 3600)));
    Wire.write(bin2bcd((uint8_t)d));
    Wire.write((uint8_t)((epoch / 86400u + 4) % 7));        // 1970-01-01 was a Thursday
    Wire.write(bin2bcd((uint8_t)mo));
    Wire.write(bin2bcd((uint8_t)(y % 100)));
    bool ok = Wire.endTransmission() == 0;
    Wire.beginTransmission(RTC_ADDR);
    Wire.write((uint8_t)0x09);
    Wire.write(RTC_MARK);
    ok = (Wire.endTransmission() == 0) && ok;
    Serial.printf("[rtc] clock chip %s %04d-%02u-%02u %02lu:%02lu UTC\n", ok ? "set to" : "FAILED at",
                  y, mo, d, (unsigned long)(sod / 3600), (unsigned long)(sod / 60 % 60));
}

// 0 when the chip is missing, has lost the time (VL), or holds somebody else's.
static uint32_t rtcRead(bool* lost, bool* foreign) {
    *lost = false; *foreign = false;
    if (!s_chip) return 0;
    uint8_t mark = 0;
    Wire.beginTransmission(RTC_ADDR);
    Wire.write((uint8_t)0x09);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(RTC_ADDR, (uint8_t)1) != 1) return 0;
    mark = (uint8_t)Wire.read();
    if (mark != RTC_MARK) { *foreign = true; return 0; }
    Wire.beginTransmission(RTC_ADDR);
    Wire.write((uint8_t)0x02);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom(RTC_ADDR, (uint8_t)7) != 7) return 0;
    uint8_t r[7];
    for (uint8_t i = 0; i < 7; i++) r[i] = (uint8_t)Wire.read();
    if (r[0] & 0x80) { *lost = true; return 0; }
    const int y = 2000 + bcd2bin(r[6]);
    const unsigned mo = bcd2bin(r[5] & 0x1F), d = bcd2bin(r[3] & 0x3F);
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    return (uint32_t)daysFromCivil(y, mo, d) * 86400u + bcd2bin(r[2] & 0x3F) * 3600u +
           bcd2bin(r[1] & 0x7F) * 60u + bcd2bin(r[0] & 0x7F);
}

void rtcBegin() {
    bool lost = false, foreign = false;
    const uint32_t t = rtcRead(&lost, &foreign);
    if (t) {
        s_rtcLoading = true;
        const bool ok = Clock::setEpoch(t) && Clock::isSet();
        s_rtcLoading = false;
        Serial.printf("[rtc] clock chip says %lu: %s\n", (unsigned long)t, ok ? "clock set from it" : "not believable");
    } else {
        Serial.printf("[rtc] clock chip has no time (%s)\n", lost ? "backup ran out" : foreign ? "not set by SquachWatch" : "not answering");
    }
    Clock::onSet(rtcWrite);
    // A clock that already knew the time goes to a chip that did not.
    if (!t && Clock::trusted()) rtcWrite(Clock::nowEpoch());
}

// ---- the motion sensor ------------------------------------------------------
static const uint8_t IMU_ADDR = 0x68;
static bool s_imu = false;

bool imuBegin() {
    uint8_t who = 0;
    Wire.beginTransmission(IMU_ADDR);
    Wire.write((uint8_t)0x75);                      // WHO_AM_I: 0x19 on an MPU6886
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(IMU_ADDR, (uint8_t)1) != 1) { Serial.println("[imu] no answer at 0x68"); return false; }
    who = (uint8_t)Wire.read();
    if (who != 0x19) { Serial.printf("[imu] unexpected id 0x%02X\n", who); return false; }
    Wire.beginTransmission(IMU_ADDR); Wire.write((uint8_t)0x6B); Wire.write((uint8_t)0x00); Wire.endTransmission();   // out of sleep
    delay(10);
    Wire.beginTransmission(IMU_ADDR); Wire.write((uint8_t)0x1C); Wire.write((uint8_t)0x10); Wire.endTransmission();   // accel +-8 g, 4096 LSB/g
    Wire.beginTransmission(IMU_ADDR); Wire.write((uint8_t)0x6C); Wire.write((uint8_t)0x07); Wire.endTransmission();   // gyro axes off: accel only
    s_imu = true;
    Serial.println("[imu] MPU6886 ready (shake to wake)");
    return true;
}

// A shake is two hard jolts inside half a second: the length of the
// acceleration vector more than 1.2 g away from the 1 g of sitting still.
// One bump, or setting the board down, is one jolt and does nothing.
bool shaken(uint32_t now) {
    if (!s_imu) return false;
    static uint32_t polledAt = 0, firstAt = 0, quietUntil = 0;
    if (now - polledAt < 50) return false;
    polledAt = now;
    Wire.beginTransmission(IMU_ADDR);
    Wire.write((uint8_t)0x3B);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(IMU_ADDR, (uint8_t)6) != 6) return false;
    int16_t v[3];
    for (int i = 0; i < 3; i++) { const uint8_t h = (uint8_t)Wire.read(), l = (uint8_t)Wire.read(); v[i] = (int16_t)((h << 8) | l); }
    const float g = sqrtf((float)v[0] * v[0] + (float)v[1] * v[1] + (float)v[2] * v[2]) / 4096.0f;
    if (fabsf(g - 1.0f) < 1.2f || (int32_t)(quietUntil - now) > 0) return false;
    if (firstAt && now - firstAt < 500 && now - firstAt > 80) { firstAt = 0; quietUntil = now + 1500; return true; }
    firstAt = now;
    return false;
}

// ---- the vibration motor ---------------------------------------------------
// Motor voltage by BUZZ STRENGTH. A 3 V coin ERM barely turns under about 2 V.
static const uint16_t MOTOR_MV[3] = { 2000, 2600, 3300 };
static void motor(bool on, uint16_t mv) {
    if (s_chip == 192) {
        // LDO3: 1.8 V + 100 mV a step in the low nibble of reg 0x28, enable bit 3 of 0x12.
        if (on) {
            uint8_t step = (uint8_t)((mv - 1800) / 100);
            wrMasked(0x28, step & 0x0F, 0xF0);
            wrMasked(0x12, 0x08, 0xFF);
        } else {
            wrMasked(0x12, 0x00, 0xF7);
        }
    } else if (s_chip == 2101) {
        // DLDO1: 0.5 V + 100 mV a step (reg 0x99), enable bit 7 of 0x90.
        if (on) {
            wr(0x99, (uint8_t)((mv - 500) / 100));
            wrMasked(0x90, 0x80, 0xFF);
        } else {
            wrMasked(0x90, 0x00, 0x7F);
        }
    }
}

// on-ms, then off-ms, pairs; 0 on-ms ends the pattern.
struct Seg { uint16_t on, off; };
static const Seg P_ALERT[]   = { {90, 90}, {90, 0}, {0, 0} };
static const Seg P_WATCH[]   = { {320, 160}, {320, 160}, {320, 0}, {0, 0} };
static const Seg P_MESSAGE[] = { {70, 0}, {0, 0} };
static const Seg* s_seg = nullptr;
static uint8_t    s_segI = 0, s_level = 1;
static bool       s_segOn = false;
static uint32_t   s_segAt = 0;

void vibrate(Pattern p, uint8_t level) {
    if (!s_chip) return;
    s_seg   = p == Pattern::WATCH ? P_WATCH : p == Pattern::MESSAGE ? P_MESSAGE : P_ALERT;
    s_level = level > 2 ? 2 : level;
    s_segI  = 0;
    s_segOn = true;
    s_segAt = millis();
    motor(true, MOTOR_MV[s_level]);
}

void tick(uint32_t now) {
    if (!s_seg) return;
    const Seg& g = s_seg[s_segI];
    if (s_segOn) {
        if (now - s_segAt < g.on) return;
        motor(false, 0);
        s_segOn = false;
        s_segAt = now;
        if (g.off == 0) { s_segI++; if (!s_seg[s_segI].on) { s_seg = nullptr; return; } s_segOn = true; motor(true, MOTOR_MV[s_level]); }
    } else {
        if (now - s_segAt < g.off) return;
        s_segI++;
        if (!s_seg[s_segI].on) { s_seg = nullptr; return; }
        s_segOn = true;
        s_segAt = now;
        motor(true, MOTOR_MV[s_level]);
    }
}

}  // namespace Core2Power

// The SYSTEM page's BATTERY row (ui_settings.cpp declares it for the boards that have one).
void boardBatteryLine(char* out, size_t n) {
    uint8_t pct; bool chg;
    if (!Core2Power::battery(pct, chg)) { snprintf(out, n, "NONE"); return; }
    const uint16_t mv = Core2Power::batteryMv();
    snprintf(out, n, "%u%% %u.%02uV%s", (unsigned)pct, (unsigned)(mv / 1000), (unsigned)(mv % 1000 / 10), chg ? "+" : "");
}
#endif  // CORE2
