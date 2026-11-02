#pragma once
#include <esp_bt.h>

// ESP_PWR_LVL_P21 (+21 dBm) only exists in the esp_power_level_t enum on the
// ESP32-S3/C3/C6. The classic ESP32 used by the CYD 2.8"/3.5" and AWOK boards
// tops out at ESP_PWR_LVL_P9 (+9 dBm), so referencing P21 there fails to
// compile. Use the strongest level the current target actually defines.
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || \
    defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32C2)
  #define SQW_BLE_MAX_PWR ESP_PWR_LVL_P21
#else
  #define SQW_BLE_MAX_PWR ESP_PWR_LVL_P9
#endif
