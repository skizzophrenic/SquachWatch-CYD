// SquachWatch-Sim — reset-reason shim (diagnostics screen only).
#pragma once
typedef enum {
    ESP_RST_UNKNOWN = 0, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW,
    ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT,
    ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO,
} esp_reset_reason_t;
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }

// RTC memory attributes. On the ESP32 these place a variable in RTC RAM so it
// survives a software reset; on a host there is no such thing and no reset to
// survive, so they are no-ops and the variable is an ordinary static. Defined
// here because main.cpp uses them for the crash breadcrumb, and the live sim
// compiles main.cpp.
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif
#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif

// The ESP32's hardware random number generator. Deterministic here on
// purpose: the emulator should render the same frame every run, so a phrase
// rolled in it comes out the same too. Nothing in the emulator is secret.
#include <stdint.h>
inline uint32_t esp_random() {
    static uint32_t s = 0x5A17C0DEu;
    s = s * 1664525u + 1013904223u;
    return s;
}
