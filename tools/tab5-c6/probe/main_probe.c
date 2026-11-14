/* SPDX-License-Identifier: Apache-2.0
 *
 * Read-only probe of the Tab5's ESP32-C6 over SDIO: powers the C6 the way
 * M5Stack's BSP does (both IO expanders), waits, brings the ESP-Hosted link
 * up and reads the co-processor's version. Writes NOTHING to the C6. Built
 * in variants (reset polarity, bus width, clock) to find what this unit
 * answers to, before the OTA tool is trusted with it. Replaces main.c in a
 * copy of tab5-p4-c6-sdio-ota (shadowlab, Apache-2.0).
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "probe";

#define I2C_SDA GPIO_NUM_31
#define I2C_SCL GPIO_NUM_32
#define EXP1 0x43
#define EXP2 0x44
#define REG_CHIP_RESET 0x01
#define REG_IO_DIR     0x03
#define REG_OUT_SET    0x05
#define REG_OUT_H_IM   0x07
#define REG_IN_DEF_STA 0x09
#define REG_PULL_EN    0x0B
#define REG_PULL_SEL   0x0D
#define REG_INT_MASK   0x11

static i2c_master_bus_handle_t s_bus;

static esp_err_t wr(i2c_master_dev_handle_t d, uint8_t reg, uint8_t v) { uint8_t b[2] = { reg, v }; return i2c_master_transmit(d, b, 2, 100); }
static int rd(i2c_master_dev_handle_t d, uint8_t reg) { uint8_t v = 0; return i2c_master_transmit_receive(d, &reg, 1, &v, 1, 100) == ESP_OK ? v : -1; }

/* Exactly M5Stack's bsp_io_expander_pi4ioe_init (as in M5MonsterC5-Tab5's
 * m5stack_tab5.c), both expanders, so the C6 sees the same world it sees
 * under the apps that are known to reach it. */
static void expanders_like_bsp(void) {
    const i2c_master_bus_config_t bc = { .i2c_port = -1, .sda_io_num = I2C_SDA, .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &s_bus));
    i2c_master_dev_handle_t e1, e2;
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = EXP1, .scl_speed_hz = 400000 };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dc, &e1));
    dc.device_address = EXP2;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dc, &e2));

    wr(e1, REG_CHIP_RESET, 0xFF); rd(e1, REG_CHIP_RESET);
    wr(e1, REG_IO_DIR, 0b01111111); wr(e1, REG_OUT_H_IM, 0); wr(e1, REG_PULL_SEL, 0b01111111); wr(e1, REG_PULL_EN, 0b01111111);
    wr(e1, REG_OUT_SET, 0b01110110);                                  /* SPK, EXT5V, LCD_RST, TP_RST, CAM_RST high */

    wr(e2, REG_CHIP_RESET, 0xFF); rd(e2, REG_CHIP_RESET);
    wr(e2, REG_IO_DIR, 0b10111001); wr(e2, REG_OUT_H_IM, 0b00000110); wr(e2, REG_PULL_SEL, 0b10111001);
    wr(e2, REG_PULL_EN, 0b11111001); wr(e2, REG_IN_DEF_STA, 0b01000000); wr(e2, REG_INT_MASK, 0b10111111);
    wr(e2, REG_OUT_SET, 0b00001001);                                  /* WLAN_PWR_EN (P0) + USB5V_EN (P3) high */
    ESP_LOGI(TAG, "[DIAG] expander1 out=0x%02x dir=0x%02x  expander2 out=0x%02x dir=0x%02x (WLAN_PWR_EN is bit0)",
             rd(e1, REG_OUT_SET), rd(e1, REG_IO_DIR), rd(e2, REG_OUT_SET), rd(e2, REG_IO_DIR));
}

static bool version(const char *when) {
    esp_hosted_coprocessor_fwver_t v; memset(&v, 0, sizeof v);
    const esp_err_t e = esp_hosted_get_coprocessor_fwversion(&v);
    if (e == ESP_OK) ESP_LOGI(TAG, "[PASS] %s: C6 firmware %" PRIu32 ".%" PRIu32 ".%" PRIu32, when, v.major1, v.minor1, v.patch1);
    else ESP_LOGW(TAG, "[....] %s: no version (%s)", when, esp_err_to_name(e));
    return e == ESP_OK;
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); err = nvs_flash_init(); }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGW(TAG, "=== C6 probe: %s, %s, %d kHz ===",
#ifdef CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH
             "reset ACTIVE-HIGH",
#else
             "reset ACTIVE-LOW",
#endif
#ifdef CONFIG_ESP_HOSTED_SDIO_1_BIT_BUS
             "1-bit",
#else
             "4-bit",
#endif
             CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ);

    expanders_like_bsp();
    /* Let the C6 come up on its own for a while before anyone resets it:
     * the apps that reach it spend seconds on other hardware first. */
    ESP_LOGI(TAG, "[DIAG] C6 powered; waiting 4 s before the link");
    vTaskDelay(pdMS_TO_TICKS(4000));

    for (int attempt = 1; attempt <= 3; attempt++) {
        ESP_LOGW(TAG, "[PHASE] link attempt %d", attempt);
        err = attempt == 1 ? esp_hosted_init() : esp_hosted_connect_to_slave();
        ESP_LOGI(TAG, "[DIAG] init/connect: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(3000));
        if (version("after link")) { ESP_LOGW(TAG, "[DONE] link up"); for (;;) vTaskDelay(1000); }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    ESP_LOGE(TAG, "[DONE] no answer from the C6 in this variant");
    for (;;) vTaskDelay(1000);
}
