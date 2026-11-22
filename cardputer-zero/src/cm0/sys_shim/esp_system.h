// SquachWatch-CM0 -- esp_system.h for the device build.
//
// Shadows the firmware's sim/esp_system.h (this directory is first on the
// include path). Everything except the RNG is the sim's, verbatim: the
// reset-reason shim and the RTC attributes.
//
// WHY THIS FILE EXISTS: the sim's esp_random() is a fixed-seed LCG on
// purpose -- the emulator wants repeatable frames and "nothing in the
// emulator is secret". The CM0 build is not an emulator: security.cpp,
// meshtalk.cpp and meshcrypto.cpp (nonces, keys, phrases) draw from
// esp_random()/esp_fill_random(), so here they are the kernel CSPRNG
// (getrandom(2)), the counterpart of the ESP32's hardware RNG.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/random.h>

typedef enum {
    ESP_RST_UNKNOWN = 0, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW,
    ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT,
    ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO,
} esp_reset_reason_t;
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }

#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif
#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif

// getrandom() blocks only until the kernel pool is first initialised
// (seconds after boot at most) and never returns short for <= 256 bytes,
// but loop anyway. A failure other than EINTR means no kernel RNG at all:
// abort rather than hand out predictable key material.
inline void esp_fill_random(void* buf, size_t len) {
    uint8_t* p = (uint8_t*)buf;
    while (len) {
        const ssize_t n = getrandom(p, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            abort();
        }
        p += n;
        len -= (size_t)n;
    }
}

inline uint32_t esp_random() {
    uint32_t v;
    esp_fill_random(&v, sizeof v);
    return v;
}
