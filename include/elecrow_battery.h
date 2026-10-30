#pragma once
#include <stdint.h>

namespace ElecrowBattery {

struct Reading {
    bool valid = false;
    uint16_t millivolts = 0;
    uint8_t percent = 0;
    bool rising = false;
};

// MAX17048 register reads only: no reset, quick-start or model changes.
// The caller supplies bounded I2C reads. UI callbacks use the cached reading
// so rendering the screen in bands cannot trigger duplicate transactions.
class Gauge {
public:
    using ReadRegister = bool (*)(uint8_t reg, uint16_t& value);
    const Reading& reading() const { return value_; }

    void tick(uint32_t now, ReadRegister read) {
        const uint32_t interval = value_.valid ? 2000 : 10000;
        if (sampled_ && now - at_ < interval) return;
        sampled_ = true;
        at_ = now;
        value_ = Reading{};
        uint16_t cell = 0, soc = 0, rate = 0;
        if (!read || !read(0x02, cell) || !read(0x04, soc)) return;
        // VCELL: 78.125 uV per bit; SOC: 1/256 percent per bit.
        const uint32_t mv = (uint32_t)((uint64_t)cell * 78125u / 1000000u);
        if (mv < 2500 || mv > 4500 || soc > 105u * 256u) return;
        value_.millivolts = (uint16_t)mv;
        value_.percent = soc >= 100u * 256u ? 100 : (uint8_t)(soc >> 8);
        value_.valid = true;
        // A rising state of charge is an indication, not USB detection.
        value_.rising = read(0x16, rate) && (int16_t)rate > 5;
    }

private:
    Reading value_;
    uint32_t at_ = 0;
    bool sampled_ = false;
};

} // namespace ElecrowBattery
