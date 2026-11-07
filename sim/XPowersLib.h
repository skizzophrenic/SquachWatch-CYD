// SquachWatch-CYD browser emulator — the T-Watch S3's AXP2101 power chip.
//
// The watch build (make wasm-twatch) talks to its PMU for the rails, the
// battery and the crown. Here the rails do nothing, the battery is a full
// cell on the cable, and the crown is SimPmu::crown: sw_boot() sets it, and
// the firmware's own twatchCrownTick() reads it as the PMU's short-press IRQ,
// so the press runs through the same pocketPress() the watch's does.
#pragma once
#include <cstdint>

namespace SimPmu {
    inline bool crown = false;   // a short press waiting to be read
}

#define XPOWERS_AXP2101_CHG_CUR_200MA     4
#define XPOWERS_AXP2101_CHG_CUR_400MA     8
#define XPOWERS_AXP2101_VBUS_CUR_LIM_500MA 3
#define XPOWERS_AXP2101_PKEY_SHORT_IRQ    (1ULL << 0)
#define XPOWERS_AXP2101_ALL_IRQ           (0xFFFFFFFFULL)
#ifndef AXP2101_SLAVE_ADDRESS
#define AXP2101_SLAVE_ADDRESS 0x34
#endif

class XPowersAXP2101 {
public:
    template <typename... A> bool begin(A...) { return true; }

    // Rails: on, off, at whatever voltage -- nothing to drive. Named the way
    // XPowersLib names them, one call per rail.
#define SIMPMU_RAIL(N) \
    bool set##N##Voltage(uint16_t) { return true; } \
    uint16_t get##N##Voltage()     { return 3300; } \
    bool enable##N()               { return true; } \
    bool disable##N()              { return true; } \
    bool isEnable##N()             { return true; }
    SIMPMU_RAIL(DC1) SIMPMU_RAIL(DC2) SIMPMU_RAIL(DC3) SIMPMU_RAIL(DC4) SIMPMU_RAIL(DC5)
    SIMPMU_RAIL(ALDO1) SIMPMU_RAIL(ALDO2) SIMPMU_RAIL(ALDO3) SIMPMU_RAIL(ALDO4)
    SIMPMU_RAIL(BLDO1) SIMPMU_RAIL(BLDO2) SIMPMU_RAIL(DLDO1) SIMPMU_RAIL(DLDO2)
#undef SIMPMU_RAIL
    bool settDC1WorkModeToPwm(uint8_t) { return true; }

    // The cell and the cable: always on USB, full, not charging.
    bool     isVbusIn()           { return true; }
    bool     isCharging()         { return false; }
    bool     isBatteryConnect()   { return true; }
    int      getBatteryPercent()  { return 100; }
    uint16_t getBattVoltage()     { return 4180; }
    uint16_t getVbusVoltage()     { return 5000; }
    uint16_t getSystemVoltage()   { return 4200; }
    bool     setChargerConstantCurr(uint8_t c) { _chg = c; return true; }
    uint8_t  getChargerConstantCurr()          { return _chg; }
    bool     setVbusCurrentLimit(uint8_t v)    { _vbus = v; return true; }
    uint8_t  getVbusCurrentLimit()             { return _vbus; }
    bool     setSysPowerDownVoltage(uint16_t)  { return true; }
    bool     setButtonBatteryChargeVoltage(uint16_t) { return true; }
    bool     enableButtonBatteryCharge()       { return true; }
    bool     enableVbusVoltageMeasure()        { return true; }
    bool     enableSystemVoltageMeasure()      { return true; }
    bool     enableBattVoltageMeasure()        { return true; }

    // Interrupts: only the crown's short press is ever raised.
    bool     enableIRQ(uint64_t)  { return true; }
    bool     disableIRQ(uint64_t) { return true; }
    uint64_t getIrqStatus()       { _irq = SimPmu::crown; return _irq ? XPOWERS_AXP2101_PKEY_SHORT_IRQ : 0; }
    bool     isPekeyShortPressIrq() { return _irq; }
    void     clearIrqStatus()     { if (_irq) SimPmu::crown = false; _irq = false; }

private:
    uint8_t _chg = XPOWERS_AXP2101_CHG_CUR_200MA, _vbus = XPOWERS_AXP2101_VBUS_CUR_LIM_500MA;
    bool _irq = false;
};
