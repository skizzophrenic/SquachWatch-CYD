// SquachWatch-CYD — theme implementation
#include "theme.h"

namespace Theme {

uint16_t colorFor(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:
        case DetectionType::AXON:
        case DetectionType::META:
            return PINK;
        case DetectionType::SKIMMER:
            return VAPOR_YELLOW;
        case DetectionType::RAVEN:
        case DetectionType::ALPR:
            return AMBER;
        case DetectionType::AIRTAG:
        case DetectionType::DRONE:
            return VAPOR_PURPLE;
        case DetectionType::CAMERA:
            return CYAN;
        default:
            return GREEN;
    }
}

uint16_t blend(uint16_t a, uint16_t b, uint16_t t) {
    // 8.8 fixed-point t, 0..256
    uint8_t ar = (a >> 8) & 0xF8;
    uint8_t ag = (a >> 3) & 0xFC;
    uint8_t ab = (a << 3) & 0xF8;
    uint8_t br = (b >> 8) & 0xF8;
    uint8_t bg = (b >> 3) & 0xFC;
    uint8_t bb = (b << 3) & 0xF8;
    uint8_t rr = (uint8_t)(((uint16_t)ar * (256 - t) + (uint16_t)br * t) >> 8) & 0xF8;
    uint8_t rg = (uint8_t)(((uint16_t)ag * (256 - t) + (uint16_t)bg * t) >> 8) & 0xFC;
    uint8_t rb = (uint8_t)(((uint16_t)ab * (256 - t) + (uint16_t)bb * t) >> 8) & 0xF8;
    return (uint16_t)((rr << 8) | (rg << 3) | (rb >> 3));
}

uint16_t titlebarColor(int x, int w) {
    if (w <= 1) return CYAN;
    // Clean two-stop cyan -> magenta fade across the full bar.
    return blend(CYAN, VAPOR_PINK, (uint16_t)(((uint32_t)x * 256) / w));
}

// Rotate button drawn in the top-right corner of the title bar: a
// vertical divider flanked by two triangles pointing away from each
// other (a "flip" glyph), in the same cyan/magenta pair as the fade.
// The tap target (ROTATE_HIT_*) is bigger than the visual icon and
// extends below the title bar into the content area — a finger needs
// a much bigger target than a stylus would.
static const int ROTATE_ICON_W = 22;
static const int ROTATE_HIT_W  = 44;
static const int ROTATE_HIT_H  = 40;

static void drawRotateIcon(TFT_eSPI& t, int w, int barH) {
    int x0 = w - ROTATE_ICON_W;
    t.fillRect(x0, 0, ROTATE_ICON_W, barH, BG);
    int cx = x0 + ROTATE_ICON_W / 2;
    int cy = barH / 2;
    t.drawFastVLine(cx, 2, barH - 4, WHITE);
    t.fillTriangle(cx - 4, cy, cx - 9, cy - 3, cx - 9, cy + 3, VAPOR_PINK);
    t.fillTriangle(cx + 4, cy, cx + 9, cy - 3, cx + 9, cy + 3, CYAN);
}

bool rotateButtonHit(int x, int y, int w) {
    return x >= w - ROTATE_HIT_W && x < w && y >= 0 && y < ROTATE_HIT_H;
}

void drawTitleBar(TFT_eSPI& t, const char* title) {
    int w = t.width();
    for (int x = 0; x < w; x++) {
        t.drawFastVLine(x, 0, 14, titlebarColor(x, w));
    }
    t.drawFastHLine(0, 14, w, PURPLE);
    t.setTextSize(1);
    // Transparent background so the fade shows through between glyphs
    // instead of a mismatched solid-color block behind the text.
    t.setTextColor(WHITE);
    int tw = t.textWidth(title);
    t.setCursor((w - tw) / 2, 4);
    t.print(title);
    drawRotateIcon(t, w, 14);
}

void drawButton(TFT_eSPI& t, int x, int y, int w, int h,
                const char* label, bool pressed) {
    uint16_t fill = pressed ? PURPLE : BG;
    uint16_t fg   = pressed ? WHITE  : CYAN;
    t.fillRect(x, y, w, h, fill);
    t.drawRect(x, y, w, h, PURPLE);
    t.setTextSize(1);
    t.setTextColor(fg, fill);
    int tw = t.textWidth(label);
    int th = t.fontHeight(1);
    t.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    t.print(label);
}

ButtonBarGeom computeButtonBar(int screenW, int screenH) {
    ButtonBarGeom g;
    // 40px is comfortably above common finger-touch-target guidance
    // (~9mm, which is ~40px on this panel's ~143 DPI) — this bar is
    // meant for fingers, not a stylus, regardless of orientation.
    g.h = 40;
    const int margin = 8, gap = 8;
    g.y = screenH - g.h - 6;
    int bw = (screenW - 2 * margin - 2 * gap) / 3;
    g.w[0] = g.w[1] = g.w[2] = bw;
    g.x[0] = margin;
    g.x[1] = g.x[0] + bw + gap;
    g.x[2] = g.x[1] + bw + gap;
    return g;
}

void drawButtonBar(TFT_eSPI& t, ButtonId highlighted) {
    ButtonBarGeom g = computeButtonBar(t.width(), t.height());
    drawButton(t, g.x[0], g.y, g.w[0], g.h, "[ SCAN ]", highlighted == ButtonId::SCAN);
    drawButton(t, g.x[1], g.y, g.w[1], g.h, "[ LOG ]",  highlighted == ButtonId::LOG);
    drawButton(t, g.x[2], g.y, g.w[2], g.h, "[ CLR ]",  highlighted == ButtonId::CLR);
}

ButtonId hitTestButtonBar(int x, int y, int screenW, int screenH) {
    ButtonBarGeom g = computeButtonBar(screenW, screenH);
    if (y < g.y || y > g.y + g.h) return ButtonId::NONE;
    if (x >= g.x[0] && x <= g.x[0] + g.w[0]) return ButtonId::SCAN;
    if (x >= g.x[1] && x <= g.x[1] + g.w[1]) return ButtonId::LOG;
    if (x >= g.x[2] && x <= g.x[2] + g.w[2]) return ButtonId::CLR;
    return ButtonId::NONE;
}

void drawScanline(TFT_eSPI& t, int y, uint16_t color) {
    t.drawFastHLine(0, y, t.width(), color);
}

void drawPulsingBorder(TFT_eSPI& t, uint32_t now, uint16_t a, uint16_t b,
                       uint8_t thick) {
    // 1.5 s sine pulse, fade between a and b
    float phase = (float)((now / 10) % 1500) / 1500.0f * 6.2831853f;
    float s = 0.5f + 0.5f * sinf(phase);
    uint16_t col = blend(a, b, (uint16_t)(s * 256.0f));
    int w = t.width();
    int h = t.height();
    for (int i = 0; i < thick; i++) {
        t.drawFastHLine(0, i, w, col);
        t.drawFastHLine(0, h - 1 - i, w, col);
        t.drawFastVLine(i, 0, h, col);
        t.drawFastVLine(w - 1 - i, 0, h, col);
    }
}

void drawSasquatchSilhouette(TFT_eSPI& t, int cx, int baseY) {
    // Decorative sasquatch silhouette (the cryptid, NOT the brand).
    // ~40 px tall, drawn at (cx, baseY) — baseY is the feet line.
    // Outline only, in PURPLE.
    int h = 40;
    int top = baseY - h;
    // Head
    t.fillEllipse(cx, top + 8, 6, 7, PURPLE);
    // Shoulders
    t.fillRoundRect(cx - 14, top + 14, 28, 10, 3, PURPLE);
    // Body
    t.fillRect(cx - 11, top + 22, 22, 14, PURPLE);
    // Arms (hanging)
    t.fillRect(cx - 16, top + 18, 4, 16, PURPLE);
    t.fillRect(cx + 12, top + 18, 4, 16, PURPLE);
    // Legs
    t.fillRect(cx - 8, top + 34, 6, 6, PURPLE);
    t.fillRect(cx + 2, top + 34, 6, 6, PURPLE);
    // Eye glints (cyan, vapor style)
    t.drawPixel(cx - 2, top + 7, CYAN);
    t.drawPixel(cx + 2, top + 7, CYAN);
}

void drawMatrixRain(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    // Independent fall speeds, 7-char trails. Glyphs are plain ASCII
    // (the default GLCD font can't render the old UTF-8 katakana bytes
    // correctly) drawn from a dense symbol/letter/digit set so the
    // rain reads as glitchy code rather than plain text. Column count
    // adapts to the current width so it works in portrait (240px) as
    // well as landscape (320px) without overflowing.
    static const int  MAX_COLS = 22;
    static const int  SPACING  = 15;
    static const char GLYPHS[] =
        "01" "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "!@#$%^&*<>{}[]/\\|+=~" "SASQUACH";
    static const int  GLN   = sizeof(GLYPHS) - 1;
    static const int  TRAIL = 7;
    static const uint16_t HEADS[3] = { VAPOR_PINK, CYAN, GREEN };

    int cols = t.width() / SPACING;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (cols < 1) cols = 1;

    static int16_t yPos[MAX_COLS];
    static uint8_t ySpeed[MAX_COLS];
    static uint8_t yTick[MAX_COLS] = {0};
    static bool    initialized = false;
    static int     lastCols = -1;

    if (!initialized || cols != lastCols) {
        for (int i = 0; i < cols; i++) {
            yPos[i]   = (int16_t)(yStart - random(0, 60));
            ySpeed[i] = 2 + (uint8_t)(i % 2);
        }
        initialized = true;
        lastCols = cols;
    }

    t.setTextSize(1);
    for (int i = 0; i < cols; i++) {
        if (++yTick[i] >= ySpeed[i]) {
            yTick[i] = 0;
            yPos[i] += 8;
            if (yPos[i] > yEnd + TRAIL * 8) {
                yPos[i] = (int16_t)(yStart - random(0, 60));
                ySpeed[i] = 2 + (uint8_t)random(0, 3);
            }
        }
        uint16_t head = HEADS[i % 3];
        int x = 3 + i * SPACING;
        for (int j = TRAIL - 1; j >= 0; j--) {
            int16_t ry = yPos[i] - j * 8;
            if (ry < yStart || ry >= yEnd) continue;
            uint32_t cell = (uint32_t)(i * 13 + j * 7) + now / 350;
            char buf[2] = { GLYPHS[cell % GLN], 0 };
            if (j == 0) {
                t.setTextColor(head, BG);
            } else {
                uint8_t b = (uint8_t)(200 * (TRAIL - j) / TRAIL);
                t.setTextColor(blend(BG, head, b), BG);
            }
            t.setCursor(x, ry);
            t.print(buf);
        }
    }
}

// Sky gradient color at a given y, sampled by both drawSunsetSky (to
// paint the sky) and drawSunsetSun (to paint its horizon-cutout
// stripes with the matching sky color instead of a flat erase) so the
// sun reads as sinking INTO the sky rather than punching a hole in it.
static uint16_t sunsetSkyColorAt(TFT_eSPI& t, int y, int yTop, int yHoriz) {
    float tt = (float)(y - yTop) / (float)(yHoriz - yTop);
    if (tt < 0.0f) tt = 0.0f;
    if (tt > 1.0f) tt = 1.0f;
    uint8_t r, g, b;
    if (tt < 0.5f) {
        float bl = tt * 2.0f;
        r = (uint8_t)(15 + bl * 150); g = 0; b = (uint8_t)(55 - bl * 20);
    } else {
        float bl = (tt - 0.5f) * 2.0f;
        r = (uint8_t)(165 + bl * 90); g = (uint8_t)(bl * 75); b = (uint8_t)(35 - bl * 15);
    }
    return t.color565(r, g, b);
}

void drawSunsetSky(TFT_eSPI& t, uint32_t now, int yTop, int yHoriz) {
    int w = t.width();
    for (int y = yTop; y < yHoriz; y++) {
        t.drawFastHLine(0, y, w, sunsetSkyColorAt(t, y, yTop, yHoriz));
    }

    // A handful of twinkling stars in the upper sky.
    static const uint8_t N = 10;
    static uint8_t sx[N], sy[N], sph[N];
    static bool inited = false;
    if (!inited) {
        for (int i = 0; i < N; i++) {
            sx[i]  = (uint8_t)random(4, 236);
            sy[i]  = (uint8_t)(yTop + 2 + random(0, (yHoriz - yTop) * 2 / 3));
            sph[i] = (uint8_t)random(0, 256);
        }
        inited = true;
    }
    for (int i = 0; i < N; i++) {
        uint32_t tw = (now / 10 + (uint32_t)sph[i] * 22) % 300;
        if (tw > 220) continue;
        uint8_t bri = (tw < 100) ? 255 : (uint8_t)(255 - (tw - 100) * 3);
        t.drawPixel(sx[i], sy[i], t.color565(bri, bri, (uint8_t)(bri * 0.88f)));
    }
}

void drawSunsetSun(TFT_eSPI& t, int cx, int cy, int r, int yTop, int yHoriz) {
    t.fillCircle(cx, cy, r,      t.color565(255,  50,  80));
    t.fillCircle(cx, cy, r -  6, t.color565(255, 100,  40));
    t.fillCircle(cx, cy, r - 13, t.color565(255, 165,  20));
    t.fillCircle(cx, cy, r - 20, t.color565(255, 215,  70));
    t.fillCircle(cx, cy, r - 27 > 0 ? r - 27 : 1, t.color565(255, 240, 150));

    // Horizon cutout stripes, widening toward the bottom, each painted
    // with the sky color at that exact row so the sun blends into the
    // gradient behind it instead of showing a flat erased slit.
    for (int stripe = 1; stripe <= 9; stripe++) {
        int sy2 = cy + stripe * 4;
        if (sy2 >= cy + r) break;
        int delta = sy2 - cy;
        if (delta <= 0 || delta >= r) continue;
        int half = (int)sqrtf((float)(r * r - delta * delta));
        t.fillRect(cx - half, sy2, half * 2, 2, sunsetSkyColorAt(t, sy2, yTop, yHoriz));
    }
    t.drawCircle(cx, cy, r, t.color565(255, 235, 235));
}

void drawSeagulls(TFT_eSPI& t, uint32_t now, int yTop, int yHoriz) {
    struct Bird { int16_t x, y; uint8_t speed; bool dir; };
    static const uint8_t N = 3;
    static Bird birds[N];
    static bool inited = false;
    static uint32_t lastStep = 0;
    int w = t.width();

    if (!inited) {
        for (int i = 0; i < N; i++) {
            birds[i].x     = (int16_t)random(0, w);
            birds[i].y     = (int16_t)(yTop + 4 + random(0, yHoriz - yTop - 12));
            birds[i].speed = (uint8_t)(1 + random(0, 3));
            birds[i].dir   = (random(0, 2) == 0);
        }
        inited = true;
        lastStep = now;
    }

    // Advance roughly every 40ms regardless of how often we're called,
    // so wing-flap speed doesn't depend on the caller's frame rate.
    if (now - lastStep >= 40) {
        lastStep = now;
        for (int i = 0; i < N; i++) {
            if (birds[i].dir) {
                birds[i].x += birds[i].speed;
                if (birds[i].x > w + 12) {
                    birds[i].x = -12;
                    birds[i].y = (int16_t)(yTop + 4 + random(0, yHoriz - yTop - 12));
                }
            } else {
                birds[i].x -= birds[i].speed;
                if (birds[i].x < -12) {
                    birds[i].x = (int16_t)(w + 12);
                    birds[i].y = (int16_t)(yTop + 4 + random(0, yHoriz - yTop - 12));
                }
            }
        }
    }

    uint16_t col = t.color565(25, 10, 45);
    for (int i = 0; i < N; i++) {
        int bx = birds[i].x, by = birds[i].y;
        if (bx < -8 || bx > w + 8 || by < yTop || by >= yHoriz) continue;
        t.drawLine(bx - 5, by - 2, bx,     by,     col);
        t.drawLine(bx,     by,     bx + 5, by - 2, col);
    }
}

void drawRetroFloor(TFT_eSPI& t, uint32_t now, int yHoriz, int yBottom) {
    int w = t.width();
    for (int y = yHoriz; y < yBottom; y++) {
        float tt = (float)(y - yHoriz) / (float)(yBottom - yHoriz);
        uint8_t b = (uint8_t)(15 + 30 * tt);
        t.drawFastHLine(0, y, w, t.color565(b, 0, (uint8_t)(b * 0.7f)));
    }
    t.drawFastHLine(0, yHoriz - 1, w, t.color565(255, 90, 130));
    t.drawFastHLine(0, yHoriz,     w, t.color565(140, 25,  70));

    int vanishX = w / 2;
    uint16_t gridCol = blend(BG, CYAN, 55);
    for (int i = 0; i <= 8; i++) {
        int xBot = (int)(((float)i / 8.0f) * w);
        t.drawLine(xBot, yBottom, vanishX, yHoriz, gridCol);
    }

    // Horizontal rungs scroll outward from the horizon toward the
    // viewer on a loop — a static grid read as the animation being
    // absent entirely down here, next to the moving sky above it.
    const uint32_t period = 900;
    float basePhase = (float)(now % period) / (float)period;
    for (int i = 0; i < 5; i++) {
        float tt = fmodf(basePhase + (float)i / 5.0f, 1.0f);
        int y = yHoriz + (int)(tt * tt * (yBottom - yHoriz));
        uint8_t fade = (uint8_t)(255 - tt * 120);
        t.drawFastHLine(0, y, w, blend(BG, gridCol, fade));
    }
}

void drawGlitchText(TFT_eSPI& t, int y, const char* text,
                    uint16_t color, uint32_t now) {
    int8_t jitter = (int8_t)((now / 150) % 3) - 1;  // -1, 0, +1
    int w = t.width();
    t.setTextSize(1);
    t.setTextColor(color, BG);
    int tw = t.textWidth(text);
    t.setCursor((w - tw) / 2 + jitter * 2, y);
    t.print(text);
}

}  // namespace Theme
