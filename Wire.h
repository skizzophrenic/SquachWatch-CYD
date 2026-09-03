// SquachWatch-Sim — I2C shim. Reports no device on the bus, which is
// what makes CapTouch::begin() fail and pollTouch() fall through to the
// resistive XPT2046 path -- matching the real board, whose boot log
// reads "No capacitive touch found".
#pragma once
#include <cstdint>
#include <cstddef>

class TwoWire {
public:
    TwoWire(uint8_t = 0) {}
    bool begin(int = -1, int = -1, uint32_t = 0) { return true; }
    void end() {}
    void setClock(uint32_t) {}
    void beginTransmission(uint8_t) {}
    uint8_t endTransmission(bool = true) { return 2; }   // 2 = NACK on address: nothing there
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t*, size_t n) { return n; }
    uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
    int available() { return 0; }
    int read() { return -1; }
};
inline TwoWire Wire;
