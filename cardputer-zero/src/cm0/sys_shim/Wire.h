// SquachWatch-CM0 -- the I2C shim, shadowing the sim's Wire.h (which
// reports an empty bus).
//
// The sim's version NACKs everything. On the CM0 that is exactly right
// for the TCA8418 keyboard (0x34, an evdev node here) and the touch
// probe (0x15) -- both fall through to the firmware's no-device paths,
// as they always did -- but it is wrong for the one real I2C consumer
// the speaker build adds: sound.cpp's ES8311 codec init, at 0x18. The
// Cardputer Zero's codec at that role is the ES8389, already configured
// by the kernel's ASoC driver, so the firmware's register writes are
// deliberate no-ops: this shim ACKs 0x18 and NACKs every other address,
// like a bus with only the codec on it.
#pragma once
#include <cstdint>
#include <cstddef>

class TwoWire {
public:
    TwoWire(uint8_t = 0) {}
    bool begin(int = -1, int = -1, uint32_t = 0) { return true; }
    void end() {}
    void setClock(uint32_t) {}
    void beginTransmission(uint8_t addr) { s_addr = addr; }
    // 0 = ACK, 2 = NACK on address: only the codec answers.
    uint8_t endTransmission(bool = true) { return s_addr == 0x18 ? 0 : 2; }
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t*, size_t n) { return n; }
    uint8_t requestFrom(uint8_t addr, uint8_t n) {
        s_addr = addr;
        return (s_addr == 0x18 && n == 1) ? 1 : 0;
    }
    int available() { return s_addr == 0x18 ? 1 : 0; }
    int read() { return 0x00; }   // the codec's id registers, for the boot log
private:
    uint8_t s_addr = 0;
};
inline TwoWire Wire;
inline TwoWire Wire1;   // the T-Watch's second bus (PMU, haptics, RTC)
