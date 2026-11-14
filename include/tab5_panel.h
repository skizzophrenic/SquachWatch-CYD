// SquachWatch-CYD — the M5Stack Tab5's panel, touch and backlight, through M5GFX.
//
// M5GFX (>= 0.2.30) knows this board: it probes the expanders, reads the
// touch controller's firmware version to tell an ST7123 from an ST7121 or an
// ILI9881C, brings up the DSI bus and the panel, and hands out a 720x1280
// RGB565 framebuffer in PSRAM that the DSI peripheral scans out on its own.
// This module owns the one M5GFX object and exposes the three things the
// rest of the board layer needs: the framebuffer (for tab5_blit), the
// touch (in panel pixels, rotated) and the backlight.
#pragma once
#include <stdint.h>

namespace m5gfx { class M5GFX; }

namespace Tab5Panel {
    // Brings the panel up at TAB5_ROTATION. False if M5GFX did not find it.
    bool begin();
    bool up();
    m5gfx::M5GFX& gfx();
    bool hasTouch();

    // The DSI framebuffer: TAB5_NATIVE_H rows of TAB5_NATIVE_W RGB565 pixels,
    // lineBytes() apart. nullptr until begin() succeeded.
    uint16_t* frameBuffer();
    uint32_t  lineBytes();

    // Write the cache back for `bytes` from `p` so the DSI engine sees it.
    void sync(const void* p, uint32_t bytes);

    // 0..255, 255 brightest -- the firmware's own convention.
    void backlight(uint8_t duty);
}

namespace Tab5Touch {
    // One finger, in panel pixels of the rotated panel (0..1279, 0..719 in
    // landscape). False when nothing is touching.
    bool read(uint16_t& x, uint16_t& y);
}
