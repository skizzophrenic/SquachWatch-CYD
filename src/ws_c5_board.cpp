// SquachWatch-CYD — Waveshare ESP32-C5-Touch-LCD-3.5: the I/O expander.
// See include/ws_c5_board.h for the register map and where it came from.
#if defined(WAVESHARE_C5)
#include "ws_c5_board.h"
#include <Arduino.h>
#include <Wire.h>
#include "clock.h"
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

namespace WsC5 {

static const uint8_t EXIO_ADDR  = 0x24;
static const uint8_t REG_MODE   = 0x02;
static const uint8_t REG_OUTPUT = 0x03;
static const uint8_t REG_PWM    = 0x05;

static const uint8_t EXIO_TP_RST  = 0;
static const uint8_t EXIO_LCD_RST = 1;
static const uint8_t EXIO_AMP     = 5;   // PA_CTRL

static bool    s_ok     = false;
static uint8_t s_out    = 0;
static int     s_lastBl = -1;

static bool writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(EXIO_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool begin() {
    // The card shares the panel's bus: hold its chip select off before the
    // first byte TFT_eSPI sends, or the card can answer a panel command.
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);

    // Waveshare's order: everything an input with outputs pre-loaded low and
    // the backlight off, then the three outputs enabled, then both resets
    // pulsed high-low-high. Waveshare raise EXIO5 at the end too; it is the
    // speaker amplifier, so here it stays low until a sound wants it.
    const uint8_t outputs = (1u << EXIO_TP_RST) | (1u << EXIO_LCD_RST) | (1u << EXIO_AMP);
    s_ok = writeReg(REG_MODE, 0xFF) &&
           writeReg(REG_OUTPUT, 0x00) &&
           writeReg(REG_PWM, 0x00) &&
           writeReg(REG_MODE, outputs);
    if (!s_ok) {
        // Once more after a breath: the CH32V003 boots off the same rail and
        // can still be starting when the C5 gets here on a cold power-up.
        delay(50);
        s_ok = writeReg(REG_MODE, 0xFF) &&
               writeReg(REG_OUTPUT, 0x00) &&
               writeReg(REG_PWM, 0x00) &&
               writeReg(REG_MODE, outputs);
    }
    if (!s_ok) {
        Serial.println("[ws-c5] I/O expander (CH32V003 @0x24) did not answer: panel may stay in reset");
        return false;
    }
    s_lastBl = 0;

    s_out = (1u << EXIO_TP_RST) | (1u << EXIO_LCD_RST);
    writeReg(REG_OUTPUT, s_out);
    delay(50);
    s_out = 0;
    writeReg(REG_OUTPUT, s_out);
    delay(50);
    s_out = (1u << EXIO_TP_RST) | (1u << EXIO_LCD_RST);   // resets released, amp off
    writeReg(REG_OUTPUT, s_out);
    // The ST7796 wants 120 ms after reset before it takes SLPOUT; the FT6336
    // a little less. tft.init() comes straight after this.
    delay(120);
    Serial.println("[ws-c5] I/O expander up: panel and touch out of reset, amp off");
    return true;
}

void backlight(uint8_t duty) {
    if (!s_ok || (int)duty == s_lastBl) return;
    if (writeReg(REG_PWM, duty)) s_lastBl = duty;
}

bool ok() { return s_ok; }

void amp(bool on) {
    if (!s_ok) return;
    const uint8_t want = on ? (uint8_t)(s_out | (1u << EXIO_AMP)) : (uint8_t)(s_out & ~(1u << EXIO_AMP));
    if (want == s_out) return;
    if (writeReg(REG_OUTPUT, want)) s_out = want;
}

// ---- AXP2101 --------------------------------------------------------------
static XPowersAXP2101 s_pmu;
static bool           s_pmuOk = false;

bool pmuBegin() {
    s_pmuOk = s_pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
    if (!s_pmuOk) { Serial.println("[pmu] AXP2101 did not answer: no battery readout"); return false; }
    Wire.setClock(400000);   // begin() may have reset it
    // Measurements only, as Waveshare's 06_axp2101_example sets them up.
    // TS off: there is no thermistor on this board's cell connector, and an
    // unconnected TS pin can stop the charger.
    s_pmu.disableTSPinMeasure();
    s_pmu.enableBattDetection();
    s_pmu.enableVbusVoltageMeasure();
    s_pmu.enableBattVoltageMeasure();
    s_pmu.enableSystemVoltageMeasure();
    Serial.printf("[pmu] AXP2101 up: %s, battery %s\n", s_pmu.isVbusIn() ? "on USB" : "on battery",
                  s_pmu.isBatteryConnect() ? "connected" : "not connected");
    return true;
}

bool battery(uint8_t& pct, bool& charging, uint16_t& mv) {
    if (!s_pmuOk) return false;
    static uint32_t at = 0;
    static bool     have = false, chg = false;
    static int      p = -1;
    static uint16_t v = 0;
    const uint32_t now = millis();
    if (!at || now - at >= 2000) {   // a few I2C reads, not a few per frame
        at = now ? now : 1;
        have = s_pmu.isBatteryConnect();
        if (have) {
            p   = s_pmu.getBatteryPercent();
            v   = s_pmu.getBattVoltage();
            chg = s_pmu.isCharging() || s_pmu.isVbusIn();
        }
    }
    if (!have || p < 0) return false;
    pct = (uint8_t)(p > 100 ? 100 : p);
    charging = chg;
    mv = v;
    return true;
}

// ---- PCF85063 -------------------------------------------------------------
static const uint8_t RTC_ADDR = 0x51;
// The chip's free RAM byte (0x03) holds this once SquachWatch has set the
// time. Without it the chip holds whatever the factory demo left there, in
// whatever zone it used, and believing that would move the clock by hours.
static const uint8_t RTC_RAM  = 0x03;
static const uint8_t RTC_MARK = 0xC2;
static bool s_rtcOk = false;
static bool s_rtcLoading = false;   // our own set at boot: nothing to write back

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

// Days since 1970-01-01 for a civil date, and back (Howard Hinnant's).
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
    if (s_rtcLoading || !s_rtcOk) return;
    int y; unsigned mo, d;
    civilFromDays((int32_t)(epoch / 86400u), y, mo, d);
    const uint32_t sod = epoch % 86400u;
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(0x00);
    Wire.write(0x00);                                   // control 1: running, 24-hour
    bool ok = Wire.endTransmission() == 0;
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(0x04);
    Wire.write(bin2bcd((uint8_t)(sod % 60)));           // seconds, OS flag cleared
    Wire.write(bin2bcd((uint8_t)(sod / 60 % 60)));
    Wire.write(bin2bcd((uint8_t)(sod / 3600)));
    Wire.write(bin2bcd((uint8_t)d));
    Wire.write((uint8_t)((epoch / 86400u + 4) % 7));    // 1970-01-01 was a Thursday
    Wire.write(bin2bcd((uint8_t)mo));
    Wire.write(bin2bcd((uint8_t)(y % 100)));
    ok = (Wire.endTransmission() == 0) && ok;
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(RTC_RAM);
    Wire.write(RTC_MARK);
    ok = (Wire.endTransmission() == 0) && ok;
    Serial.printf("[rtc] clock chip %s %04d-%02u-%02u %02lu:%02lu UTC\n", ok ? "set to" : "FAILED at",
                  y, mo, d, (unsigned long)(sod / 3600), (unsigned long)(sod / 60 % 60));
}

static bool rtcReg(uint8_t reg, uint8_t* out, uint8_t n) {
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(RTC_ADDR, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) out[i] = Wire.read();
    return true;
}

// 0 when the chip is missing, stopped at some point (its OS flag: the power
// and any backup both ran out), or holds a time SquachWatch did not write.
static uint32_t rtcRead(bool* lost, bool* foreign) {
    *lost = false; *foreign = false;
    uint8_t mark;
    if (!rtcReg(RTC_RAM, &mark, 1)) return 0;
    s_rtcOk = true;
    if (mark != RTC_MARK) { *foreign = true; return 0; }
    uint8_t r[7];
    if (!rtcReg(0x04, r, 7)) return 0;
    if (r[0] & 0x80) { *lost = true; return 0; }
    const int y = 2000 + bcd2bin(r[6]);
    const unsigned mo = bcd2bin(r[5] & 0x1F), d = bcd2bin(r[3] & 0x3F);
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    const uint32_t days = (uint32_t)daysFromCivil(y, mo, d);
    return days * 86400u + bcd2bin(r[2] & 0x3F) * 3600u + bcd2bin(r[1] & 0x7F) * 60u + bcd2bin(r[0] & 0x7F);
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
        Serial.printf("[rtc] clock chip has no time (%s)\n",
                      !s_rtcOk ? "not answering" : lost ? "it stopped: no power" : foreign ? "not set by SquachWatch" : "unreadable");
    }
    if (!s_rtcOk) return;
    Clock::onSet(rtcWrite);
    // A clock that already knew the time (kept across a soft reset) goes to
    // a chip that did not.
    if (!t && Clock::trusted()) rtcWrite(Clock::nowEpoch());
}

}  // namespace WsC5
#endif
