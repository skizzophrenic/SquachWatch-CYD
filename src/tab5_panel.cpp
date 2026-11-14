#if defined(TAB5)
#include "tab5_panel.h"
#include "tab5_board.h"
#include <Arduino.h>
#include <M5GFX.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>
#include <esp_cache.h>

namespace {
M5GFX     s_gfx;
bool      s_up = false;
uint16_t* s_fb = nullptr;
uint32_t  s_lineBytes = 0;
}

namespace Tab5Panel {

bool begin() {
    if (s_up) return true;
    // M5GFX's autodetect: expanders, the touch FW version probe that tells
    // the ST7123 from the ST7121 (lane speed differs, the DSI ID does not),
    // the DSI bus, the panel's own init list, the framebuffer in PSRAM.
    if (!s_gfx.init() || s_gfx.getBoard() != m5gfx::board_t::board_M5Tab5) {
        Serial.printf("[tab5] M5GFX did not find the panel (board %d)\n", (int)s_gfx.getBoard());
        return false;
    }
    s_gfx.setRotation(TAB5_ROTATION);
    // The panel object is Panel_ST7123 : Panel_DSI, whose config_detail holds
    // the DPI framebuffer the DSI engine scans out. Rows are the native
    // (portrait) width, padded to a word, as Panel_DSI::init() lays them.
    auto* dsi = static_cast<lgfx::Panel_DSI*>(s_gfx.getPanel());
    s_fb = static_cast<uint16_t*>(dsi->config_detail().buffer);
    s_lineBytes = ((TAB5_NATIVE_W * 16 / 8) + 3) & ~3u;
    if (!s_fb) {
        Serial.println("[tab5] M5GFX gave no framebuffer");
        return false;
    }
    s_up = true;
    // The radio's power switch is bit 0 of the second expander's output
    // register, and M5GFX's init leaves it ON. Off here, so the ESP32-C6
    // starts fresh when Tab5Radio::begin() powers it, and draws nothing until
    // then. A P4 reset alone does not reset the C6: after a brownout
    // (2026-10-19, on a hub port) it kept sniffing through every reboot and
    // each boot browned out sooner, during this very init.
    if (!lgfx::i2c::bitOff(1, 0x44, 0x05, 1u << 0, 100000).has_value())
        Serial.println("[tab5] the radio's power bit would not clear");
    Serial.printf("[tab5] panel up: %dx%d rot %d, framebuffer %p (%u B/line), touch %s\n",
                  (int)s_gfx.width(), (int)s_gfx.height(), (int)s_gfx.getRotation(), s_fb,
                  (unsigned)s_lineBytes, s_gfx.touch() ? "yes" : "NO");
    return true;
}

bool      up()          { return s_up; }
M5GFX&    gfx()         { return s_gfx; }
bool      hasTouch()    { return s_up && s_gfx.touch() != nullptr; }
uint16_t* frameBuffer() { return s_fb; }
uint32_t  lineBytes()   { return s_lineBytes; }

void sync(const void* p, uint32_t bytes) {
    // The framebuffer is in PSRAM behind the P4's cache; the DSI DMA reads
    // memory. Rows are 1440 bytes and not cache-line aligned, so UNALIGNED
    // lets the call round the range out to whole lines.
    esp_cache_msync((void*)p, bytes,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

void backlight(uint8_t duty) {
    if (s_up) s_gfx.setBrightness(duty);
}

}  // namespace Tab5Panel

namespace Tab5Touch {

bool read(uint16_t& x, uint16_t& y) {
    if (!s_up) return false;
    lgfx::touch_point_t tp;
    if (s_gfx.getTouch(&tp, 1) < 1) return false;
    if (tp.x < 0 || tp.y < 0) return false;
    x = (uint16_t)tp.x;
    y = (uint16_t)tp.y;
    return true;
}

}  // namespace Tab5Touch
#endif  // TAB5
