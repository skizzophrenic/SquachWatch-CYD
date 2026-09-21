#pragma once
#include <stdint.h>
namespace CardputerBoard {
void begin();
void tick(uint32_t now);
// Returns true if input woke a dimmed display; consume that first key.
bool input(uint32_t now);
void alert(uint32_t now);
bool dimmed();
uint16_t batteryMillivolts();
uint8_t scanMode();
const char* scanModeName();
void cycleScanMode();
}
