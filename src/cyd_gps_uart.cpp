#include "cyd_gps_uart.h"
#if defined(CYD_GPS_ELECROW)
#include <Arduino.h>
#include <sdkconfig.h>
#if !defined(ARDUINO_ARCH_ESP32) || !defined(CONFIG_IDF_TARGET_ESP32)
#error "P1 GPS console ownership supports only the classic ESP32 CYD"
#endif
#include <esp_log.h>
#include <esp32-hal-uart.h>
#include <driver/gpio.h>
#include <stdarg.h>
#include "gnss.h"
#if !defined(CYD) || !defined(CYD_GPS)
#error "CYD_GPS_ELECROW requires a CYD_GPS build"
#endif
#if !defined(CYD_GPS_RX) || CYD_GPS_RX != 1 || !defined(CYD_GPS_BAUD) || CYD_GPS_BAUD != 9600
#error "The verified Elecrow fixed-harness profile requires RX1 at 9600 baud"
#endif
#if !defined(CYD_GPS_SERIAL)
#error "The verified Elecrow fixed-harness profile requires Serial2"
#else
static_assert(&CYD_GPS_SERIAL == &Serial2, "The Elecrow harness must use UART2");
#endif

namespace CydGpsUart {
static int discard(const char*, va_list) { return 0; }
void silenceConsole() {
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_set_vprintf(discard);
    Serial.end();
    uartSetDebug(nullptr);
    uartDetachPins(0, 3, 1, -1, -1);
    pinMode(1, INPUT);
    pinMode(3, INPUT);
    gpio_pullup_dis(GPIO_NUM_1);
    gpio_pulldown_dis(GPIO_NUM_1);
    gpio_pullup_dis(GPIO_NUM_3);
    gpio_pulldown_dis(GPIO_NUM_3);
}
static uint32_t bytes = 0;
static bool bufferReady = false;
void begin() {
    // This UART has no other owner in this profile. End before setting its
    // buffer or pins so a future restart never retains a transmitter mapping.
    Serial2.end();
    bufferReady = Serial2.setRxBufferSize(1024) == 1024;
    bytes = 0;
    Serial2.begin(CYD_GPS_BAUD, SERIAL_8N1, CYD_GPS_RX, -1);
}
void poll(uint32_t now) {
    // 9600/8N1 is ~960 bytes/s. Bound each loop's parser work, leaving time
    // for scans and touch. The RX ring absorbs about one second of traffic;
    // this does not guarantee lossless input across longer application stalls.
    for (unsigned n = 0; n < 512 && Serial2.available(); ++n) {
        const int c = Serial2.read();
        if (c < 0) break;
        ++bytes;
        Gnss::feed((char)c, now);
    }
}
uint32_t receivedBytes() { return bytes; }
int rxPin() { return CYD_GPS_RX; }
unsigned uartNumber() { return 2; }
uint32_t baud() { return CYD_GPS_BAUD; }
bool rxBufferReady() { return bufferReady; }
bool receiving() { return (bool)Serial2; }
}
// initArduino() invokes the framework's weak C hook before setup(). The
// explicit C linkage overrides that hook; main.cpp also calls the helper,
// retaining this object in the app link and repeating suppression at setup.
// This is the earliest app hook, AFTER core/IDF startup. ROM/reset/panic or
// download-mode traffic and external USB-bridge drive remain hardware limits.
extern "C" void initVariant() { CydGpsUart::silenceConsole(); }
#endif
