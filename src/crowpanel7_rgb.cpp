#if defined(CROWPANEL7)
#include "crowpanel7_rgb.h"
#include <Arduino.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_attr.h>
#include <soc/io_mux_reg.h>
#include <string.h>

static esp_lcd_panel_handle_t s_panel = nullptr;
// One panel line of RGB565 in internal RAM, for fills and the direct-draw net.
static uint16_t* s_line = nullptr;

bool crowPanelUp() { return s_panel != nullptr; }

bool crowPanelBegin() {
    if (s_panel) return true;

    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_PLL160M;     // 160 / 16 MHz = 10, an integer divider
    cfg.timings.pclk_hz = LCD_PCLK_HZ;
    cfg.timings.h_res = PANEL_W;
    cfg.timings.v_res = PANEL_H;
    cfg.timings.hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH;
    cfg.timings.hsync_back_porch  = LCD_HSYNC_BACK_PORCH;
    cfg.timings.hsync_front_porch = LCD_HSYNC_FRONT_PORCH;
    cfg.timings.vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH;
    cfg.timings.vsync_back_porch  = LCD_VSYNC_BACK_PORCH;
    cfg.timings.vsync_front_porch = LCD_VSYNC_FRONT_PORCH;
    cfg.timings.flags.hsync_idle_low = LCD_HSYNC_POLARITY;
    cfg.timings.flags.vsync_idle_low = LCD_VSYNC_POLARITY;
    cfg.timings.flags.de_idle_high   = LCD_DE_IDLE_HIGH;
    cfg.timings.flags.pclk_active_neg = LCD_PCLK_ACTIVE_NEG;
    cfg.timings.flags.pclk_idle_high  = 0;
    cfg.data_width = 16;
    cfg.sram_trans_align  = 8;
    cfg.psram_trans_align = 64;           // 64-byte DMA bursts out of PSRAM
    cfg.hsync_gpio_num = LCD_HSYNC;
    cfg.vsync_gpio_num = LCD_VSYNC;
    cfg.de_gpio_num    = LCD_DE;
    cfg.pclk_gpio_num  = LCD_PCLK;
    cfg.disp_gpio_num  = -1;
    // Blue first: d0..d4 = B0..B4, d5..d10 = G0..G5, d11..d15 = R0..R4.
    const int data[16] = { LCD_B0, LCD_B1, LCD_B2, LCD_B3, LCD_B4,
                           LCD_G0, LCD_G1, LCD_G2, LCD_G3, LCD_G4, LCD_G5,
                           LCD_R0, LCD_R1, LCD_R2, LCD_R3, LCD_R4 };
    for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
    cfg.flags.fb_in_psram = 1;
    cfg.flags.relax_on_idle = 0;          // stream mode: keeps scanning

    esp_err_t e = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if (e != ESP_OK || !s_panel) {
        Serial.printf("[panel] esp_lcd_new_rgb_panel: %s - is PSRAM set to OPI (qio_opi)?\n",
                      esp_err_to_name(e));
        s_panel = nullptr;
        return false;
    }
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);

    s_line = (uint16_t*)heap_caps_malloc(PANEL_W * sizeof(uint16_t),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    crowPanelFillRect(0, 0, PANEL_W, PANEL_H, 0x0000);
    Serial.printf("[panel] %dx%d RGB via esp_lcd (IDF driver), pclk %ld Hz\n",
                  PANEL_W, PANEL_H, (long)LCD_PCLK_HZ);
    return true;
}

void crowPanelWriteRows(int y, int n, const uint16_t* data) {
    if (!s_panel || n <= 0 || y < 0 || y + n > PANEL_H) return;
    // x_end / y_end are EXCLUSIVE in this driver.
    esp_lcd_panel_draw_bitmap(s_panel, 0, y, PANEL_W, y + n, data);
}

void crowPanelWriteRect(int x, int y, int w, int h, const uint16_t* data) {
    if (!s_panel || w <= 0 || h <= 0) return;
    if (x < 0 || y < 0 || x + w > PANEL_W || y + h > PANEL_H) return;
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, data);
}

void crowPanelFillRect(int x, int y, int w, int h, uint16_t color) {
    if (!s_panel || !s_line || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > PANEL_W) w = PANEL_W - x;
    if (y + h > PANEL_H) h = PANEL_H - y;
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < w; i++) s_line[i] = color;
    for (int r = 0; r < h; r++) esp_lcd_panel_draw_bitmap(s_panel, x, y + r, x + w, y + r + 1, s_line);
}

// ---- VSYNC watch ----------------------------------------------------------
static volatile uint32_t s_vsLastUs = 0, s_vsPeriodUs = 0;
static volatile uint32_t s_vsMin = 0xFFFFFFFF, s_vsMax = 0, s_vsCount = 0;

static void IRAM_ATTR crowVsyncIsr() {
    uint32_t now = (uint32_t)esp_timer_get_time();
    uint32_t last = s_vsLastUs;
    s_vsLastUs = now;
    if (!last) return;
    uint32_t d = now - last;
    s_vsPeriodUs = d;
    if (d < s_vsMin) s_vsMin = d;
    if (d > s_vsMax) s_vsMax = d;
    s_vsCount++;
}

void crowVsyncWatchBegin() {
    // NOT pinMode(INPUT): that detaches the LCD peripheral's output from the
    // pin. Enabling the input path on the IO-MUX register reads back a pin
    // the peripheral is still driving.
    PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[LCD_VSYNC]);
    attachInterrupt(LCD_VSYNC, crowVsyncIsr, RISING);
}
uint32_t crowVsyncPeriodUs() { return s_vsPeriodUs; }
void crowVsyncStatsReset() { noInterrupts(); s_vsMin = 0xFFFFFFFF; s_vsMax = 0; s_vsCount = 0; interrupts(); }
void crowVsyncStats(uint32_t& lo, uint32_t& hi, uint32_t& n) { noInterrupts(); lo = s_vsMin; hi = s_vsMax; n = s_vsCount; interrupts(); }

#if defined(CROWPANEL7_PANELTEST)
#include "crowpanel7_backlight.h"
#include <Wire.h>
#include <Preferences.h>

void crowPanelTest() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_HZ);
    delay(20);
    Serial.println(F("[test] I2C scan:"));
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) Serial.printf("[test]   0x%02X answered\n", a);
    }
    CrowBL::begin();
    CrowBL::set(255);
    if (!crowPanelUp()) { Serial.println(F("[test] panel never came up")); for (;;) delay(1000); }

    // Bars left to right: RED GREEN BLUE WHITE BLACK. BLUE GREEN RED means
    // the data-line order is reversed -- blue must be d0.
    const uint16_t bars[5] = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000 };
    const int bw = PANEL_W / 5;
    for (int i = 0; i < 5; i++) crowPanelFillRect(i * bw, 0, bw, PANEL_H - 80, bars[i]);
    Serial.println(F("[test] bars, left to right: RED GREEN BLUE WHITE BLACK"));
    // A one-pixel grid: a drifting moire here is a pixel clock running away.
    crowPanelFillRect(0, PANEL_H - 80, PANEL_W, 80, 0x0000);
    for (int x = 0; x < PANEL_W; x += 2) crowPanelFillRect(x, PANEL_H - 80, 1, 40, 0xFFFF);
    for (int y = PANEL_H - 40; y < PANEL_H; y += 2) crowPanelFillRect(0, y, PANEL_W, 1, 0xFFFF);

    crowVsyncWatchBegin();
    delay(200);
    Preferences prefs;
    prefs.begin("sqwtest", false);
    uint32_t n = 0;
    // A flash write disables the cache the panel's DMA reads through; this
    // is what that costs, in microseconds of frame period.
    Serial.println(F("[test] frame period spread, quiet vs writing flash:"));
    for (;;) {
        for (int phase = 0; phase < 2; phase++) {
            const bool writing = (phase == 1);
            crowVsyncStatsReset();
            uint32_t t0 = millis();
            while (millis() - t0 < 3000) { if (writing) prefs.putULong("n", n++); delay(20); }
            uint32_t lo, hi, cnt; crowVsyncStats(lo, hi, cnt);
            Serial.printf("[test] %-8s frames %3lu  min %lu us  max %lu us  spread %lu us  psram free %u\n",
                          writing ? "WRITING" : "quiet", (unsigned long)cnt, (unsigned long)lo,
                          (unsigned long)hi, (unsigned long)(cnt ? hi - lo : 0),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
    }
}
#endif
#endif  // CROWPANEL7
