#if defined(M5_STOPWATCH)
#include "stopwatch_board.h"
#include "stopwatch_geometry.h"
#include "frame_push.h"
#include "settings.h"
#include <M5Unified.h>

namespace StopWatch {
namespace {
bool up = false;
int rows = 0;
int padX = 0, padY = 0;
uint16_t colors[256];
// A typed pixel buffer bypasses M5GFX's ambiguous uint16_t/swapBytes overload.
lgfx::rgb565_t row[StopWatchGeometry::PANEL];
uint16_t swapRedBlue(uint16_t c) {
    return (c & 0x07E0) | ((c & 0xF800) >> 11) | ((c & 0x001F) << 11);
}
}

bool begin() {
    if (up) return true;
    auto cfg = M5.config();
    cfg.serial_baudrate = 0; // main.cpp already configured native USB
    cfg.fallback_board = m5::board_t::board_M5StopWatch;
    cfg.external_display_value = 0;
    cfg.output_power = false;
    cfg.internal_mic = false;
    cfg.internal_spk = false;
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    M5.begin(cfg);
    // Do not claim success based on fallback_board: require a real panel.
    up = M5.Display.getBoard() == m5gfx::board_t::board_M5StopWatch
        && M5.Display.width() >= StopWatchGeometry::PANEL
        && M5.Display.height() >= StopWatchGeometry::PANEL;
    if (!up) {
        Serial.println("[stopwatch] M5GFX did not initialize the StopWatch panel");
        return false;
    }
    M5.Display.setRotation(0);
    // M5GFX 0.2.26 exposes 468x468 for the specified 466x466 glass.
    padX = StopWatchGeometry::driverPadding(M5.Display.width());
    padY = StopWatchGeometry::driverPadding(M5.Display.height());
    M5.Display.fillScreen(0);
    M5.Power.setVibration(0);
    for (int c = 0; c < 256; ++c) {
        const uint16_t wire = FramePush::rgb332Wire(static_cast<uint8_t>(c));
        colors[c] = (wire >> 8) | (wire << 8); // native RGB565 for M5GFX
    }
    Serial.printf("[stopwatch] CO5300 driver %dx%d, native 466x466 circular layout, touch %s\n",
                  M5.Display.width(), M5.Display.height(), hasTouch() ? "yes" : "NO");
    return true;
}

bool ready() { return up; }
bool hasTouch() { return up && M5.Display.touch() != nullptr; }
bool readTouch(int16_t& x, int16_t& y) {
    if (!hasTouch()) return false;
    lgfx::touch_point_t p;
    if (M5.Display.getTouch(&p, 1) < 1) return false;
    return StopWatchGeometry::toLogical(p.x, p.y, x, y, M5.Display.width(), M5.Display.height());
}
void brightness(uint8_t value) { if (up) M5.Display.setBrightness(value); }
void update() { if (up) M5.update(); }
void powerOff() {
    if (!up) return;
    M5.Power.setVibration(0);
    M5.Display.setBrightness(0);
    M5.Power.powerOff(); // StopWatch M5PM1 shutdown, with the library's sleep fallback.
}
bool buttonAPressed() { return up && M5.BtnA.wasPressed(); }
bool buttonADown() { return up && M5.BtnA.isPressed(); }
bool buttonBPressed() { return up && M5.BtnB.wasPressed(); }
bool buttonBDown() { return up && M5.BtnB.isPressed(); }
bool batteryState(uint8_t& percent, bool& charging) {
    if (!up) return false;
    static uint32_t sampled = 0;
    static int level = -1;
    static bool charge = false;
    if (!sampled || millis() - sampled >= 2000) {
        sampled = millis();
        level = M5.Power.getBatteryLevel();
        charge = M5.Power.isCharging() == m5::Power_Class::is_charging;
    }
    if (level < 0) return false;
    percent = static_cast<uint8_t>(level > 100 ? 100 : level);
    charging = charge;
    return true;
}
void fillRect(int x, int y, int w, int h, uint16_t color) {
    using namespace StopWatchGeometry;
    if (!up || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WIDTH) w = WIDTH - x;
    if (y + h > HEIGHT) h = HEIGHT - y;
    if (Settings::rgbSwapped()) color = swapRedBlue(color);
    if (w > 0 && h > 0) M5.Display.fillRect(padX + LEFT + x, padY + TOP + y, w, h, color);
}
void invert(bool enabled) { if (up) M5.Display.invertDisplay(enabled); }
void push(const uint8_t* pixels, int w, int h, int x, int y,
          const uint8_t* backdrop, uint16_t surround) {
    using namespace StopWatchGeometry;
    rows = 0;
    if (!up || !pixels || w <= 0 || h <= 0 || w > WIDTH || h > HEIGHT) return;
    const int x0 = x < 0 ? -x : 0;
    const int x1 = x + w > WIDTH ? WIDTH - x : w;
    if (x0 >= x1) return;
    const bool swap = Settings::rgbSwapped();
    M5.Display.startWrite();
    // rgb565_t selects the native-color converter regardless of swapBytes.
    // This applies equally to avatar pixels, backgrounds and partial updates.
    // A complete frame owns the entire glass, including transitions away
    // from the animated home screen. Never leave the previous scene's rim.
    if (x == 0 && y == 0 && w == WIDTH && h == HEIGHT) {
        for (int py = 0; py < PANEL; ++py) {
            for (int px = 0; px < PANEL; ++px) {
                uint16_t c = surround;
                if (px >= LEFT && px < LEFT + WIDTH && py >= TOP && py < TOP + HEIGHT)
                    c = colors[pixels[(py - TOP) * WIDTH + px - LEFT]];
                else if (backdrop) c = colors[backdrop[py * PANEL + px]];
                row[px] = swap ? swapRedBlue(c) : c;
            }
            M5.Display.pushImage(padX, padY + py, PANEL, 1, row);
            ++rows;
        }
        M5.Display.endWrite();
        return;
    }
    for (int sy = 0; sy < h; ++sy) {
        if (y + sy < 0 || y + sy >= HEIGHT) continue;
        for (int sx = x0; sx < x1; ++sx) {
            const uint16_t c = colors[pixels[sy * w + sx]];
            row[sx - x0] = swap ? swapRedBlue(c) : c;
        }
        M5.Display.pushImage(padX + LEFT + x + x0, padY + TOP + y + sy, x1 - x0, 1, row);
        ++rows;
    }
    M5.Display.endWrite();
}
int lastRows() { return rows; }
}
#endif
