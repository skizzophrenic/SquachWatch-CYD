// SquachWatch-CYD — FT6336 capacitive touch, Waveshare C5 3.5". See ws_c5_touch.h.
// Adapted from the verified PorkChop/Marauder C5 driver (ft6336.h).
#if defined(WS_C5_35)
#include "ws_c5_touch.h"
#include "ws_c5_expander.h"
#include <Arduino.h>
#include <Wire.h>

namespace WsTouch {

static const int     SDA_PIN = 27;
static const int     SCL_PIN = 26;
static const uint8_t ADDR      = 0x38;
static const uint8_t TD_STATUS = 0x02;   // then 6 bytes of point data from 0x03

static bool s_present = false;

static bool ftRead(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom((int)ADDR, (int)len);
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.available() ? Wire.read() : 0;
    return true;
}

bool present() { return s_present; }

bool begin() {
    // Panel power, LCD reset and touch reset all live on the expander, which
    // also opens the shared I2C bus. begin() there is idempotent, so this is
    // safe even though setup() already ran it before tft.init().
    WsExio::begin();

    uint8_t chipId = 0;
    Wire.beginTransmission(ADDR);
    Wire.write(0xA3);                       // CHIPER register
    if (Wire.endTransmission(false) == 0) {
        Wire.requestFrom((int)ADDR, 1);
        if (Wire.available()) chipId = Wire.read();
    }
    s_present = (chipId == 0x64);           // FT6336U reports 0x64
    Serial.printf("[Touch] FT6336 ID 0x%02X %s\n", chipId, s_present ? "(OK)" : "(not found)");

    // Sensitivity: reg 0x80 (IDTHRESHOLD), LOWER = more sensitive. Default 22;
    // 40 (panel-in-case) makes a bare panel miss light taps. 12 gives a
    // responsive bare-glass feel; read() validates each sample so a low
    // threshold can't let noise register. Raise toward 30-40 if a case/film is
    // added and phantom touches appear.
    Wire.beginTransmission(ADDR);
    Wire.write(0x80);
    Wire.write(12);
    Wire.endTransmission();
    return s_present;
}

bool read(uint16_t& x, uint16_t& y) {
    // The C5's early Wire.begin() leaves the I2C controller unable to complete
    // reads (writes ACK, reads return 0), so re-open the bus on each read and
    // validate the result (1-2 points, on-panel coords). This was the single
    // hardest bug in the C5 bring-up — do not remove.
    //
    // The read is ALSO intermittently flaky even with the re-open: a finger
    // held still occasionally comes back as no points / a failed transaction,
    // which made the mascot grab (a 600 ms still-hold) keep losing its timer.
    // The glitch is transient, so retry a couple times within this one poll
    // before reporting "no touch". A genuine finger-up just fails all tries
    // (a few extra reads at the loop rate, no release latency).
    for (int attempt = 0; attempt < 3; attempt++) {
        Wire.end();
        Wire.begin(SDA_PIN, SCL_PIN, 100000U);

        uint8_t data[7];
        if (ftRead(TD_STATUS, data, 7)) {
            const uint8_t points = data[0] & 0x0F;
            if (points >= 1 && points <= 2) {
                const uint16_t rx = ((uint16_t)(data[1] & 0x0F) << 8) | data[2];
                const uint16_t ry = ((uint16_t)(data[3] & 0x0F) << 8) | data[4];
                if (rx < TFT_WIDTH && ry < TFT_HEIGHT) { x = rx; y = ry; return true; }
            }
        }
        // failed transaction / no or off-panel points: ride out the glitch
    }
    return false;
}

}  // namespace WsTouch
#endif  // WS_C5_35
