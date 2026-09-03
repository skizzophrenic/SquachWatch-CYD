// SquachWatch-Sim — SPI shim. Nothing here talks to a bus; the display
// and touch shims hold their state in memory, so every call is inert.
#pragma once
#include <cstdint>

// Bus selectors main.cpp names when constructing its own SPIClass.
#define FSPI 0
#define HSPI 2
#define VSPI 3

class SPIClass {
public:
    SPIClass(uint8_t = 0) {}
    void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1) {}
    void end() {}
    void setFrequency(uint32_t) {}
    uint8_t transfer(uint8_t v) { return v; }
};
inline SPIClass SPI;
