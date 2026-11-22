#pragma once
#include <stdint.h>

namespace StopWatch {
bool begin();
bool ready();
bool hasTouch();
bool readTouch(int16_t& x, int16_t& y);
void brightness(uint8_t value);
void update();
void powerOff();
bool buttonAPressed();
bool buttonADown();
bool buttonBDown();
bool buttonBPressed();
bool batteryState(uint8_t& percent, bool& charging);
void fillRect(int x, int y, int w, int h, uint16_t color);
void invert(bool enabled);
void push(const uint8_t* pixels, int w, int h, int x, int y,
          const uint8_t* backdrop = nullptr, uint16_t surround = 0);
int lastRows();
}
