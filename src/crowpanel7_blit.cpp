#if defined(CROWPANEL7)
#include "crowpanel7_blit.h"
#include "crowpanel7_rgb.h"
#include "crowpanel7_board.h"
#include "frame_push.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace CrowBlit {
namespace {

// RGB332 -> RGB565 in MEMORY order. FramePush::rgb332Wire() returns SPI WIRE
// order; a framebuffer word wants (hi << 8) | lo. One definition of what an
// 8-bit colour looks like in this firmware, so this board matches the rest.
uint16_t s_lut[256];
bool     s_lutReady = false;

const int32_t MAX_ROWS = PANEL_H;
uint32_t s_rowHash[MAX_ROWS];
bool     s_valid = false;
const uint8_t* s_lastSrc = nullptr;
int32_t  s_lastW = 0, s_lastH = 0;
uint32_t s_pushCount = 0;
int32_t  s_lastRows = 0;

// Rows are converted into internal RAM and handed to the driver in runs, so
// each esp_lcd_panel_draw_bitmap() moves a block rather than a line. The
// driver memcpy()s it into the PSRAM framebuffer and writes the cache back
// -- which is the part a direct pointer into the framebuffer silently skips.
const int STAGE_ROWS = 8;                      // panel rows per call
uint16_t* s_stage = nullptr;                   // STAGE_ROWS * PANEL_W

// A word-at-a-time hash: the frame is in PSRAM and hashing every row of it
// is most of the push, so four times fewer bus transactions than bytes.
inline uint32_t rowHash(const uint8_t* p, int32_t n) {
    uint32_t h = 2166136261u;
    const int32_t words = n >> 2;
    const uint32_t* w = reinterpret_cast<const uint32_t*>(p);
    for (int32_t i = 0; i < words; i++) { h ^= w[i]; h *= 16777619u; }
    for (int32_t i = words << 2; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

// Expands one source row into the staging buffer at stage row `sr` -- and,
// at scale 2, the row after it.
inline void expandRow(const uint8_t* srow, int32_t w, int32_t x, int sr) {
    uint16_t* d = s_stage + (size_t)sr * PANEL_W;
    memset(d, 0, PANEL_W * sizeof(uint16_t));
    uint16_t* p = d + SQW_BLIT_X0 + x * SQW_BLIT_SCALE;
#if SQW_BLIT_SCALE == 1
    for (int32_t i = 0; i < w; i++) p[i] = s_lut[srow[i]];
#else
    for (int32_t i = 0; i < w; i++) {
        uint16_t c = s_lut[srow[i]];
        for (int s = 0; s < SQW_BLIT_SCALE; s++) p[i * SQW_BLIT_SCALE + s] = c;
    }
    for (int s = 1; s < SQW_BLIT_SCALE; s++)
        memcpy(d + (size_t)s * PANEL_W, d, PANEL_W * sizeof(uint16_t));
#endif
}

}  // namespace

void begin() {
    for (int i = 0; i < 256; i++) {
        uint16_t wire = FramePush::rgb332Wire((uint8_t)i);
        s_lut[i] = (uint16_t)((wire >> 8) | (wire << 8));
    }
    if (!s_stage) s_stage = (uint16_t*)heap_caps_malloc(
        (size_t)STAGE_ROWS * PANEL_W * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_lutReady = (s_stage != nullptr);
    if (!s_lutReady) Serial.println(F("[blit] no internal RAM for the staging rows"));
    invalidate();
}

void invalidate() { s_valid = false; }
int32_t lastRows() { return s_lastRows; }

bool push(const uint8_t* src, int32_t w, int32_t h, int32_t x, int32_t y) {
    if (!crowPanelUp() || !src || !s_lutReady) return false;
    if (h > MAX_ROWS) return false;
    if (src != s_lastSrc || w != s_lastW || h != s_lastH) {
        s_valid = false;
        s_lastSrc = src; s_lastW = w; s_lastH = h;
    }
    // Every 64th push goes out whole, so a hash collision cannot leave a
    // wrong row on the panel for the rest of the session.
    const bool full = !s_valid || (s_pushCount % 64) == 0;
    s_pushCount++;

    int32_t rows = 0;
    int staged = 0;              // stage rows filled
    int stageY = -1;             // panel row the staging block starts at
    auto flush = [&]() {
        if (staged > 0) { crowPanelWriteRows(stageY, staged, s_stage); rows += staged; }
        staged = 0; stageY = -1;
    };
    for (int32_t r = 0; r < h; r++) {
        const uint8_t* srow = src + (size_t)r * w;
        uint32_t hash = rowHash(srow, w);
        bool changed = full || hash != s_rowHash[r];
        s_rowHash[r] = hash;
        if (!changed) { flush(); continue; }
        const int py = SQW_BLIT_Y0 + (y + r) * SQW_BLIT_SCALE;
        if (staged + SQW_BLIT_SCALE > STAGE_ROWS || (staged && stageY + staged != py)) flush();
        if (staged == 0) stageY = py;
        expandRow(srow, w, x, staged);
        staged += SQW_BLIT_SCALE;
    }
    flush();
    s_valid = true;
    s_lastRows = rows;
    return true;
}

}  // namespace CrowBlit
#endif
