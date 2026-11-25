// SPDX-License-Identifier: MIT
// See ws_c5_gps.h. Transaction, per Waveshare's LC76G_RPI_I2C demo:
//   0. 1-byte dummy write to 0x50 (wake / presence)
//   1. write {reg 0xAA510008, len 4} (LE) to 0x50   -> "how much?"
//   2. read 4 bytes from 0x54                       -> length (LE)
//   3. write {reg 0xAA512000, len N} (LE) to 0x50   -> "send N"
//   4. read N bytes from 0x54                       -> NMEA text
#include "ws_c5_gps.h"
#if defined(WS_C5_35) && defined(CYD_GPS)
#include <Arduino.h>
#include <Wire.h>

namespace WsGps {

static const uint8_t  SDA_PIN = 27, SCL_PIN = 26;   // the touch/codec/PMU bus
static const uint8_t  CR_ADDR = 0x50;   // command/register writes
static const uint8_t  RD_ADDR = 0x54;   // data reads
static const uint8_t  WR_ADDR = 0x58;   // data write (wake); nothing else uses it here
static const uint32_t CHUNK     = 1024; // bytes per cycle; bounds the loop hitch
static const uint32_t PERIOD_MS = 500;  // ~2 Hz
static const uint32_t STEP_MS   = 3;    // inter-step settle (the demo uses 10)

static uint32_t s_cycles = 0, s_fails = 0;

static inline void put32le(uint8_t* b, uint32_t v) {
    b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24;
}
static uint8_t writeCr(uint32_t reg, uint32_t len) {   // 0 = ok
    uint8_t b[8]; put32le(b, reg); put32le(b + 4, len);
    Wire.beginTransmission(CR_ADDR); Wire.write(b, 8); return Wire.endTransmission();
}
static size_t readBytes(uint8_t* buf, size_t n) {
    const size_t got = Wire.requestFrom((uint8_t)RD_ADDR, n);
    for (size_t i = 0; i < got; i++) buf[i] = Wire.read();
    return got;
}
static uint8_t dummy(uint8_t addr) {   // 1-byte write; 0 = ACKed
    Wire.beginTransmission(addr); Wire.write((uint8_t)0); return Wire.endTransmission();
}
static uint32_t le32(const uint8_t* b) {
    return b[0] | (b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

// The C5 read quirk WsTouch::read() works around: re-open the bus once per
// cycle, never between steps. The module clock-stretches, so the timeout is
// long for the cycle and put back after it, for the touch reads.
struct Cycle {
    uint16_t was;
    Cycle() : was(Wire.getTimeOut()) {
        Wire.end();
        Wire.begin(SDA_PIN, SCL_PIN, 100000U);
        Wire.setTimeOut(300);
    }
    ~Cycle() { Wire.setTimeOut(was); }
};

// Returns bytes read, 0 if nothing waiting, -1 on a bus fail.
static int poll(void (*sink)(char, uint32_t), uint32_t now) {
    static uint8_t buf[CHUNK];
    Cycle c;
    delay(STEP_MS);
    if (dummy(CR_ADDR) != 0) {   // the demo's recovery: nudge 0x54, then 0x58
        if (dummy(RD_ADDR) != 0) dummy(WR_ADDR);
        return -1;
    }
    delay(STEP_MS);
    if (writeCr(0xAA510008UL, 4) != 0) return -1;
    delay(STEP_MS);
    uint8_t lb[4];
    if (readBytes(lb, 4) != 4) return -1;
    uint32_t len = le32(lb);
    if (len == 0) return 0;
    if (len >= 35 * 1024) return -1;   // the demo treats this as illegal
    if (len > CHUNK) len = CHUNK;      // the rest drains on later cycles
    delay(STEP_MS);
    if (writeCr(0xAA512000UL, len) != 0) return -1;
    delay(STEP_MS);
    const size_t got = readBytes(buf, len);
    if (got != len) return -1;
    for (size_t i = 0; i < got; i++) sink((char)buf[i], now);
    return (int)got;
}

// The demo's Ql_Wake_I2C: nudge the module's transmitter after it idles.
static void wake() {
    Cycle c;
    delay(STEP_MS); if (dummy(CR_ADDR) != 0) return;
    delay(STEP_MS); if (writeCr(0xAA510004UL, 4) != 0) return;
    delay(STEP_MS);
    uint8_t fb[4];
    if (readBytes(fb, 4) != 4 || le32(fb) < 1) return;
    delay(STEP_MS); if (writeCr(0xAA531000UL, 1) != 0) return;
    delay(STEP_MS); dummy(WR_ADDR);
}

void begin() {
    Wire.setBufferSize(CHUNK + 16);   // persists across Wire.end()/begin()
}

uint32_t tick(void (*sink)(char, uint32_t), uint32_t now) {
    static uint32_t s_lastAt = 0, s_empty = 0;
    if (s_lastAt && now - s_lastAt < PERIOD_MS) return 0;
    s_lastAt = now ? now : 1;
    const int r = poll(sink, now);
    s_cycles++;
    if (r < 0) { s_fails++; return 0; }
    if (r == 0) {
        if (++s_empty >= 8) { s_empty = 0; wake(); }
        return 0;
    }
    s_empty = 0;
    return (uint32_t)r;
}

uint32_t cycles() { return s_cycles; }
uint32_t fails()  { return s_fails; }

}  // namespace WsGps
#endif
