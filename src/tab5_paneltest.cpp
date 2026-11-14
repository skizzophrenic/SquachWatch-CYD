// SquachWatch-CYD -- Tab5 bench build (TAB5_PANELTEST): M5GFX alone.
//
// Proves the panel and the touch before the app is on the board: colour bars
// in a known order, a one-pixel grid, the panel's size on the console, then
// every touch as x,y. Not part of the product build ([env:tab5] filters it
// out); [env:tab5-paneltest] builds only this file.
#if defined(TAB5_PANELTEST)
#include <Arduino.h>
#include <M5GFX.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <string.h>

static M5GFX display;

static void bars() {
    const int w = display.width(), h = display.height();
    const uint16_t c[8] = { TFT_WHITE, TFT_YELLOW, TFT_CYAN, TFT_GREEN, TFT_MAGENTA, TFT_RED, TFT_BLUE, TFT_BLACK };
    display.startWrite();
    for (int i = 0; i < 8; i++) display.fillRect(i * w / 8, 0, w / 8 + 1, h * 2 / 3, c[i]);
    display.fillRect(0, h * 2 / 3, w, h / 3, TFT_DARKGREY);
    for (int x = 0; x < w; x += 40) display.drawFastVLine(x, h * 2 / 3, h / 3, TFT_WHITE);
    for (int y = h * 2 / 3; y < h; y += 40) display.drawFastHLine(0, y, w, TFT_WHITE);
    display.setTextColor(TFT_WHITE, TFT_DARKGREY);
    display.setTextSize(4);
    display.setCursor(20, h * 2 / 3 + 20);
    display.printf("TAB5 %dx%d rot %d", w, h, display.getRotation());
    display.endWrite();
}

// The facts tab5_blit.cpp is built on, checked on the board rather than
// assumed: where M5GFX at rotation 1 puts a logical pixel in the native
// framebuffer, what a red pixel looks like as a framebuffer word, and how
// long a whole framebuffer's worth of row writes plus cache writeback takes.
static void selfTest() {
    auto* dsi = static_cast<lgfx::Panel_DSI*>(display.getPanel());
    uint16_t* fb = static_cast<uint16_t*>(dsi->config_detail().buffer);
    Serial.printf("[self] framebuffer %p\n", fb);
    if (!fb) return;
    struct { int x, y; uint16_t c; } probes[] = { { 100, 50, TFT_RED }, { 1200, 700, TFT_GREEN }, { 0, 0, TFT_BLUE }, { 1279, 719, TFT_WHITE } };
    for (auto& p : probes) {
        display.drawPixel(p.x, p.y, p.c);
        // Expected for rotation 1: native (nx, ny) = (719 - y, x); row stride 720 words.
        const int nx = 719 - p.y, ny = p.x;
        const uint16_t raw = fb[ny * 720 + nx];
        const uint16_t back = display.readPixel(p.x, p.y);
        // Where else could it be? The other three quarter-turn candidates.
        const uint16_t alt1 = fb[(719 - p.y) * 720 + (p.x % 720)];
        const uint16_t alt2 = fb[p.x * 720 + p.y];
        Serial.printf("[self] (%d,%d) colour %04x: raw at (719-y, x) = %04x, readPixel %04x, alt(ny=719-y,nx=x) %04x, alt(ny=x,nx=y) %04x\n",
                      p.x, p.y, p.c, raw, back, alt1, alt2);
    }
    // A full frame the blit's way: 1280 native rows of 1440 bytes from a
    // staging row, cache written back in runs of 64 rows.
    static uint32_t stage[360];
    for (int i = 0; i < 360; i++) stage[i] = 0x07E007E0u;   // green
    uint8_t* b = reinterpret_cast<uint8_t*>(fb);
    const uint32_t t0 = micros();
    for (int ny = 0; ny < 1280; ny++) {
        memcpy(b + (size_t)ny * 1440, stage, 1440);
        if ((ny & 63) == 63)
            esp_cache_msync(b + (size_t)(ny - 63) * 1440, 64 * 1440,
                            ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
    const uint32_t t1 = micros();
    // The same without the writeback, then one writeback for the whole buffer:
    // which half of the 30 ms is the copy and which the cache.
    for (int i = 0; i < 360; i++) stage[i] = 0x001F001Fu;   // blue
    const uint32_t tb0 = micros();
    for (int ny = 0; ny < 1280; ny++) memcpy(b + (size_t)ny * 1440, stage, 1440);
    const uint32_t tb1 = micros();
    esp_cache_msync(b, 1280 * 1440, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    const uint32_t tb2 = micros();
    // And plain 32-bit stores instead of memcpy, in case the library copy is the cost.
    for (int i = 0; i < 360; i++) stage[i] = 0xF800F800u;   // red
    const uint32_t tc0 = micros();
    for (int ny = 0; ny < 1280; ny++) {
        uint32_t* d = reinterpret_cast<uint32_t*>(b + (size_t)ny * 1440);
        for (int i = 0; i < 360; i++) d[i] = stage[i];
    }
    const uint32_t tc1 = micros();
    esp_cache_msync(b, 1280 * 1440, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    const uint32_t tc2 = micros();
    Serial.printf("[self] memcpy only %lu us + one writeback %lu us; word stores %lu us + writeback %lu us\n",
                  (unsigned long)(tb1 - tb0), (unsigned long)(tb2 - tb1), (unsigned long)(tc1 - tc0), (unsigned long)(tc2 - tc1));
    // And the column walk the blit's hash does, over a 640x360 frame in internal RAM.
    uint8_t* frame = (uint8_t*)heap_caps_malloc(640 * 360, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint32_t hsum = 0;
    const uint32_t t2 = micros();
    if (frame) {
        for (int fx = 0; fx < 640; fx += 2) {
            uint32_t h = 2166136261u;
            for (int r = 0; r < 360; r++) { uint16_t v; memcpy(&v, frame + r * 640 + fx, 2); h ^= v; h *= 16777619u; }
            hsum += h;
        }
    }
    const uint32_t t3 = micros();
    Serial.printf("[self] full framebuffer write + writeback %lu us; column-pair hash of a 640x360 frame %lu us (%s, %08lx)\n",
                  (unsigned long)(t1 - t0), (unsigned long)(t3 - t2), frame ? "internal" : "NO FRAME", (unsigned long)hsum);
    free(frame);
    delay(1500);
    bars();
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1500);
    Serial.println("[tab5] paneltest boot");
    const bool ok = display.init();
    Serial.printf("[tab5] display.init %s board=%d panel %dx%d\n", ok ? "ok" : "FAILED",
                  (int)display.getBoard(), (int)display.width(), (int)display.height());
    display.setRotation(1);   // landscape, 1280x720
    display.setBrightness(200);
    Serial.printf("[tab5] rotation 1 -> %dx%d, touch %s\n", (int)display.width(), (int)display.height(),
                  display.touch() ? "present" : "ABSENT");
    bars();
    Serial.printf("[tab5] heap %u, psram %u, largest %u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    selfTest();
}

void loop() {
    static uint32_t lastReport = 0;
    static bool wasDown = false;
    lgfx::touch_point_t tp;
    const int n = display.getTouch(&tp, 1);
    const bool down = n > 0;
    if (down) {
        display.fillCircle(tp.x, tp.y, 12, TFT_ORANGE);
        if (!wasDown || millis() - lastReport > 200) {
            Serial.printf("[touch] x=%d y=%d size=%d id=%d\n", tp.x, tp.y, tp.size, tp.id);
            lastReport = millis();
        }
    } else if (wasDown) {
        Serial.println("[touch] up");
    }
    wasDown = down;
    static uint32_t lastBeat = 0;
    if (millis() - lastBeat > 5000) {
        lastBeat = millis();
        Serial.printf("[beat] %lu ms heap %u panel %dx%d touch %s board %d\n", (unsigned long)millis(),
                      (unsigned)ESP.getFreeHeap(), (int)display.width(), (int)display.height(),
                      display.touch() ? "present" : "ABSENT", (int)display.getBoard());
    }
    // Console: r = restart (to read the boot log), b = redraw the bars.
    while (Serial.available()) {
        const int c = Serial.read();
        if (c == 'r') { Serial.println("[tab5] restart"); delay(50); ESP.restart(); }
        if (c == 'b') bars();
    }
    delay(10);
}
#endif
