#pragma once
#include <stdint.h>

// P1 is shared with the classic ESP32 console. This accessory build must
// release it before setup and leave it released for the whole application.
#if defined(CYD_GPS_ELECROW)
namespace CydGpsUart {
void silenceConsole();
void begin();
void poll(uint32_t now);
uint32_t receivedBytes();
int rxPin();
unsigned uartNumber();
uint32_t baud();
bool rxBufferReady();
bool receiving();
}
#endif
