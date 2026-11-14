#if defined(TAB5)
#include "tab5_blit.h"
#include "tab5_panel.h"
#include "tab5_board.h"
#include "frame_push.h"
#include <Arduino.h>
#include <string.h>

// THE MAPPING. M5GFX at rotation 1 puts logical landscape pixel (X, Y) of the
// 1280x720 picture at native (nx, ny) = (719 - Y, X) in the 720x1280
// framebuffer (Panel_FrameBufferBase::drawPixelPreclipped: for r=1, y is
// flipped then x and y swap). Checked on the board against M5GFX's own
// drawPixel/readPixel before the app was flashed (tab5_paneltest.cpp's self
// test). So a logical column becomes a native ROW, read top-down and laid
// right-to-left, which is exactly the direction a PSRAM framebuffer likes to
// be written in.

namespace Tab5Blit {
namespace {

uint16_t s_lut[256];
bool     s_lutReady = false;

// One hash per PAIR of logical columns: the frame is 8-bit and the two bytes
// at (row, x) and (row, x+1) are one aligned 16-bit load, so a pair costs the
// same strided walk as a column would. Columns outside the frame hash as 0.
const int32_t MAX_PAIRS = TAB5_LOGICAL_W / 2 + 1;
uint32_t s_pairHash[MAX_PAIRS];
bool     s_valid = false;
const uint8_t* s_lastSrc = nullptr;
int32_t  s_lastW = 0, s_lastH = 0;
uint32_t s_pushCount = 0;
int32_t  s_lastRows = 0;

// One native row, 720 pixels: 360 words, each holding the same colour twice
// (the two native pixels one logical pixel covers across). Static: internal
// RAM, no allocation to fail.
uint32_t s_stage[TAB5_NATIVE_W / 2];

inline uint32_t pairHash(const uint8_t* p, int32_t stride, int32_t h, bool two) {
    uint32_t hsh = 2166136261u;
    if (two) {
        for (int32_t r = 0; r < h; r++) {
            uint16_t v; memcpy(&v, p + (size_t)r * stride, 2);
            hsh ^= v; hsh *= 16777619u;
        }
    } else {
        for (int32_t r = 0; r < h; r++) { hsh ^= p[(size_t)r * stride]; hsh *= 16777619u; }
    }
    return hsh;
}

// Logical column `fx` of the frame (h rows, placed at logical y) into the
// staging row. Panel row Y = (y + r) * SCALE + s for s in 0..SCALE-1 lands at
// native x = 719 - Y; the SCALE native pixels of one logical pixel are
// adjacent, so with SCALE 2 they are one word at index 359 - (y + r).
inline void expandCol(const uint8_t* src, int32_t w, int32_t h, int32_t fx, int32_t y) {
    memset(s_stage, 0, sizeof s_stage);
    const uint8_t* p = src + fx;
    for (int32_t r = 0; r < h; r++) {
        const int32_t ly = y + r;
        if (ly < 0 || ly >= TAB5_LOGICAL_H) continue;
        const uint32_t c = s_lut[p[(size_t)r * w]];
        s_stage[(TAB5_NATIVE_W / TAB5_SCALE - 1) - ly] = c | (c << 16);
    }
}

}  // namespace

void begin() {
    for (int i = 0; i < 256; i++) {
        // rgb332Wire() is SPI wire order (high byte first); a framebuffer
        // word wants the plain value.
        const uint16_t wire = FramePush::rgb332Wire((uint8_t)i);
        s_lut[i] = (uint16_t)((wire >> 8) | (wire << 8));
    }
    s_lutReady = true;
    invalidate();
}

void invalidate() { s_valid = false; }
int32_t lastRows() { return s_lastRows; }

bool push(const uint8_t* src, int32_t w, int32_t h, int32_t x, int32_t y) {
    if (!Tab5Panel::up() || !src || !s_lutReady) return false;
    if (w <= 0 || h <= 0 || h > TAB5_LOGICAL_H) return false;
    if (src != s_lastSrc || w != s_lastW || h != s_lastH) {
        s_valid = false;
        s_lastSrc = src; s_lastW = w; s_lastH = h;
    }
    // Every 64th push goes out whole, so a hash collision cannot leave a
    // wrong column on the panel for the rest of the session.
    const bool full = !s_valid || (s_pushCount % 64) == 0;
    s_pushCount++;

    uint8_t* const fb = reinterpret_cast<uint8_t*>(Tab5Panel::frameBuffer());
    const uint32_t lineBytes = Tab5Panel::lineBytes();
    int32_t rows = 0;
    int32_t runStart = -1, runRows = 0;   // the native rows written since the last sync
    auto flush = [&]() {
        if (runRows > 0) Tab5Panel::sync(fb + (size_t)runStart * lineBytes, (uint32_t)runRows * lineBytes);
        runStart = -1; runRows = 0;
    };
    for (int32_t fx = 0; fx < w; fx += 2) {
        const bool two = fx + 1 < w;
        const int32_t pi = fx >> 1;
        const uint32_t hsh = pairHash(src + fx, w, h, two);
        const bool changed = full || pi >= MAX_PAIRS || hsh != s_pairHash[pi];
        if (pi < MAX_PAIRS) s_pairHash[pi] = hsh;
        if (!changed) { flush(); continue; }
        for (int32_t c = fx; c < fx + (two ? 2 : 1); c++) {
            const int32_t lx = x + c;
            if (lx < 0 || lx >= TAB5_LOGICAL_W) continue;
            expandCol(src, w, h, c, y);
            const int32_t ny0 = lx * TAB5_SCALE;
            if (runRows && runStart + runRows != ny0) flush();
            if (runRows == 0) runStart = ny0;
            for (int s = 0; s < TAB5_SCALE; s++)
                memcpy(fb + (size_t)(ny0 + s) * lineBytes, s_stage, TAB5_NATIVE_W * 2);
            runRows += TAB5_SCALE;
            rows += TAB5_SCALE;
        }
    }
    flush();
    s_valid = true;
    s_lastRows = rows;
    return true;
}

}  // namespace Tab5Blit
#endif  // TAB5
