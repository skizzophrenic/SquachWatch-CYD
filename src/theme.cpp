// SquachWatch-CYD — theme implementation
#include "theme.h"
#include "caustic_tile.h"
#include "detection.h"
#include "bangers_font.h"
#include "squachy.h"
#include "settings.h"

namespace Theme {

// Default-initialized to the original SquachWare vaporwave values —
// applyPalette(0) (VAPRW4VE) reproduces these exactly.
uint16_t BG           = 0x0801;
uint16_t TASKBAR      = 0x0803;
uint16_t PURPLE       = 0xAC1F;
uint16_t CYAN         = 0x07FF;
uint16_t PINK         = 0xF96F;
uint16_t VAPOR_PINK   = 0xFB99;
uint16_t VAPOR_PURPLE = 0xBB5F;
uint16_t VAPOR_BLUE   = 0x067F;
uint16_t VAPOR_YELLOW = 0xFFD2;
uint16_t GREEN        = 0x07E0;
uint16_t AMBER        = 0xFD20;
uint16_t RED          = 0xF800;

// Six presets in the spirit of skizzophrenic/M5PORKCHOP_DualScreen's
// theme table (same leetspeak naming style). BG/TASKBAR stay dark
// across all of them (everything in the UI assumes a dark backdrop
// with light/colored text on top of it) — only the accent hues shift
// per theme. RED is kept literal "red" in most presets since it also
// reads as an alert-severity color, not just decoration.
const Palette kPalettes[PALETTE_COUNT] = {
    { "VAPRW4VE",   0x0801, 0x0803, 0xAC1F, 0x07FF, 0xF96F, 0xFB99, 0xBB5F, 0x067F, 0xFFD2, 0x07E0, 0xFD20, 0xF800 },
    { "CYB3RGR33N", 0x0000, 0x0120, 0x07E0, 0x2FE6, 0x8FE8, 0xAFEA, 0x5FE9, 0x07E8, 0xCFEA, 0x07E0, 0xFFE0, 0xF800 },
    { "AMB3RTERM",  0x0800, 0x1000, 0xFD20, 0xFEA0, 0xFCC0, 0xFDE0, 0xFB80, 0xFC40, 0xFFE0, 0xFEA0, 0xFD20, 0xF800 },
    { "BUBBL3GUM",  0x1002, 0x2004, 0xF81F, 0xFB9D, 0xF96F, 0xFB99, 0xE01F, 0xFA1F, 0xFFF0, 0xFB56, 0xFD20, 0xF800 },
    { "GH0ST",      0x0000, 0x2104, 0xFFFF, 0xF79E, 0xC638, 0xEF7D, 0xB5B6, 0xDEFB, 0xFFFF, 0xFFFF, 0xFFFF, 0xF800 },
    { "BL00D",      0x1000, 0x2000, 0xF800, 0xFB2C, 0xFAEB, 0xF9AB, 0xC0C4, 0xF9CB, 0xFC60, 0xF800, 0xFD20, 0xF800 },
};

void applyPalette(uint8_t idx) {
    if (idx >= PALETTE_COUNT) idx = 0;
    const Palette& p = kPalettes[idx];
    BG = p.bg; TASKBAR = p.taskbar; PURPLE = p.purple; CYAN = p.cyan;
    PINK = p.pink; VAPOR_PINK = p.vaporPink; VAPOR_PURPLE = p.vaporPurple;
    VAPOR_BLUE = p.vaporBlue; VAPOR_YELLOW = p.vaporYellow; GREEN = p.green;
    AMBER = p.amber; RED = p.red;
}

Palette dimPaletteForOverlay(uint16_t t) {
    Palette saved = { "", BG, TASKBAR, PURPLE, CYAN, PINK, VAPOR_PINK,
                      VAPOR_PURPLE, VAPOR_BLUE, VAPOR_YELLOW, GREEN, AMBER, RED };
    PURPLE       = blend(PURPLE, BG, t);
    CYAN         = blend(CYAN, BG, t);
    PINK         = blend(PINK, BG, t);
    VAPOR_PINK   = blend(VAPOR_PINK, BG, t);
    VAPOR_PURPLE = blend(VAPOR_PURPLE, BG, t);
    VAPOR_BLUE   = blend(VAPOR_BLUE, BG, t);
    VAPOR_YELLOW = blend(VAPOR_YELLOW, BG, t);
    GREEN        = blend(GREEN, BG, t);
    AMBER        = blend(AMBER, BG, t);
    RED          = blend(RED, BG, t);
    return saved;
}

void restorePalette(const Palette& saved) {
    BG = saved.bg; TASKBAR = saved.taskbar; PURPLE = saved.purple; CYAN = saved.cyan;
    PINK = saved.pink; VAPOR_PINK = saved.vaporPink; VAPOR_PURPLE = saved.vaporPurple;
    VAPOR_BLUE = saved.vaporBlue; VAPOR_YELLOW = saved.vaporYellow; GREEN = saved.green;
    AMBER = saved.amber; RED = saved.red;
}

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
        case DetectionType::SAMSUNG_TAG:
        case DetectionType::GOOGLE_TAG:
        case DetectionType::TILE:
            return VAPOR_PURPLE;
        case DetectionType::CAMERA:
        case DetectionType::RING:
            return CYAN;
        case DetectionType::DEAUTH:
        case DetectionType::EVILTWIN:
            return RED;
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
// circular arrow (a ~300 degree ring, cyan into magenta, with an
// arrowhead at the open end) instead of the previous "flip" glyph
// (vertical divider + two triangles) — reads as an actual rotate/
// refresh icon at a glance instead of an abstract shape. The tap
// target (ROTATE_HIT_*) is bigger than the visual icon and extends
// below the title bar into the content area — a finger needs a much
// bigger target than a stylus would.
static const int ROTATE_ICON_W = 22;
static const int ROTATE_HIT_W  = 44;
static const int ROTATE_HIT_H  = 40;

static void drawRotateIcon(TFT_eSPI& t, int w, int barH) {
    int x0 = w - ROTATE_ICON_W;
    t.fillRect(x0, 0, ROTATE_ICON_W, barH, BG);
    int cx = x0 + ROTATE_ICON_W / 2;
    int cy = barH / 2;
    int r  = 5;
    // Ring sweeps clockwise from 30 to 330 degrees (drawArc's 0 is 12
    // o'clock), leaving a 60 degree gap centered at the top for the
    // arrowhead to sit in.
    t.drawArc(cx, cy, r, r - 2, 30, 180, CYAN, BG, true);
    t.drawArc(cx, cy, r, r - 2, 180, 330, VAPOR_PINK, BG, true);
    // Arrowhead at the ring's clockwise end (330 degrees), pointing
    // further clockwise (i.e. back up towards the gap) to read as
    // motion, not just a stray triangle.
    t.fillTriangle(cx + 1, cy - r,
                    cx - 4, cy - 3,
                    cx - 2, cy - 1,
                    VAPOR_PINK);
}

bool rotateButtonHit(int x, int y, int w) {
    return x >= w - ROTATE_HIT_W && x < w && y >= 0 && y < ROTATE_HIT_H;
}

// Settings button, mirrored into the top-left corner of the title bar:
// a 3-bar "hamburger" glyph, same oversized tap target treatment as the
// rotate icon on the other side.
static const int SETTINGS_ICON_W = 22;
static const int SETTINGS_HIT_W  = 44;
static const int SETTINGS_HIT_H  = 40;

static void drawSettingsIcon(TFT_eSPI& t, int barH) {
    t.fillRect(0, 0, SETTINGS_ICON_W, barH, BG);
    int cx = SETTINGS_ICON_W / 2;
    int y0 = barH / 2 - 4;
    t.drawFastHLine(cx - 7, y0,     14, CYAN);
    t.drawFastHLine(cx - 7, y0 + 4, 14, VAPOR_PINK);
    t.drawFastHLine(cx - 7, y0 + 8, 14, CYAN);
}

bool settingsButtonHit(int x, int y) {
    return x >= 0 && x < SETTINGS_HIT_W && y >= 0 && y < SETTINGS_HIT_H;
}

static bool s_rotateIconVisible = true;

void setRotateIconVisible(bool visible) {
    s_rotateIconVisible = visible;
}

void drawTitleBar(TFT_eSPI& t, const char* title) {
    int w = t.width();
    for (int x = 0; x < w; x++) {
        t.drawFastVLine(x, 0, 14, titlebarColor(x, w));
    }
    t.drawFastHLine(0, 14, w, PURPLE);
    // Row 15. The bar itself is rows 0-13 plus the rule on 14, but every
    // screen in the app starts its body at y=16 -- so row 15 belonged to
    // nobody and kept whatever the last frame left there, which showed
    // up as a 1px strip of fragments under the rule. Clearing it here
    // means the title bar owns the full 16 rows its callers already
    // assume it does, and fixes the strip on every screen at once
    // rather than screen by screen.
    t.drawFastHLine(0, 15, w, BG);
    t.setTextSize(1);
    // Transparent background so the fade shows through between glyphs
    // instead of a mismatched solid-color block behind the text. Black
    // ink reads more like a print/stamp against the bright cyan/
    // magenta gradient than white did.
    t.setTextColor(BLACK);
    int tw = t.textWidth(title);
    t.setCursor((w - tw) / 2, 4);
    t.print(title);
    drawSettingsIcon(t, 14);
    if (s_rotateIconVisible) drawRotateIcon(t, w, 14);
}

void drawButton(TFT_eSPI& t, int x, int y, int w, int h,
                const char* label, bool pressed, uint8_t textSize) {
    uint16_t fill = pressed ? PURPLE : BG;
    uint16_t fg   = pressed ? WHITE  : CYAN;
    t.fillRect(x, y, w, h, fill);
    t.drawRect(x, y, w, h, PURPLE);
    t.setTextSize(textSize);
    t.setTextColor(fg, fill);
    int tw = t.textWidth(label);
    int th = t.fontHeight();
    t.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    t.print(label);
}

ButtonBarGeom computeButtonBar(int screenW, int screenH) {
    ButtonBarGeom g;
    // Half of the original 40px (which was sized to comfortably clear
    // ~9mm finger-touch-target guidance) — explicitly requested smaller
    // to free up more room above for content. Still tappable, just a
    // tighter target than the original guidance-driven size.
    g.h = 20;
    const int margin = 8, gap = 8;
    g.y = screenH - g.h - 6;
    int bw = (screenW - 2 * margin - 2 * gap) / 3;
    g.w[0] = g.w[1] = g.w[2] = bw;
    g.x[0] = margin;
    g.x[1] = g.x[0] + bw + gap;
    g.x[2] = g.x[1] + bw + gap;
    return g;
}

void drawButtonBar(TFT_eSPI& t, ButtonId highlighted, ButtonBarMode mode) {
    ButtonBarGeom g = computeButtonBar(t.width(), t.height());
    if (mode == ButtonBarMode::SCAN_PICKER) {
        drawButton(t, g.x[0], g.y, g.w[0], g.h, "[ BLE ]",  highlighted == ButtonId::SCAN);
        drawButton(t, g.x[1], g.y, g.w[1], g.h, "[ WIFI ]", highlighted == ButtonId::LOG);
        drawButton(t, g.x[2], g.y, g.w[2], g.h, "[ BACK ]", highlighted == ButtonId::CLR);
        return;
    }
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

void drawScrollbar(TFT_eSPI& t, int x, int y, int h,
                   int totalItems, int visibleItems, int scrollOffset) {
    if (totalItems <= visibleItems || visibleItems <= 0) return;
    t.drawFastVLine(x, y, h, PURPLE);
    int thumbH = h * visibleItems / totalItems;
    if (thumbH < 6) thumbH = 6;
    int maxScroll = totalItems - visibleItems;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
    if (scrollOffset < 0) scrollOffset = 0;
    int travel = h - thumbH;
    int thumbY = y + (maxScroll > 0 ? (travel * scrollOffset / maxScroll) : 0);
    t.fillRect(x - 1, thumbY, 3, thumbH, CYAN);
}

// Shared by a few of drawAlertFx's cases below: expanding rings from a
// center point that fade as they grow, like a location/acoustic ping.
// Three staggered so there's always at least one on screen instead of
// a single ring popping in and out.
static void alertFxPing(TFT_eSPI& t, int cx, int cy, uint32_t now, uint16_t col,
                        uint32_t periodMs, int maxR) {
    for (uint8_t i = 0; i < 3; i++) {
        uint32_t phase = (now + i * (periodMs / 3)) % periodMs;
        float p = (float)phase / (float)periodMs;
        int r = (int)(p * maxR);
        if (r < 1) continue;
        uint16_t c = blend(BG, col, (uint16_t)((1.0f - p) * 220.0f));
        t.drawCircle(cx, cy, r, c);
    }
}

void drawAlertFx(TFT_eSPI& t, DetectionType type, uint32_t now, int w, int h,
                 bool clearFirst) {
    // Full repaint every call — the old single scanline this replaced
    // never erased its own trail, so it just accumulated into a wash
    // across the screen the longer an alert stayed up. Same "always
    // fully repaint" discipline the CLEAR-screen backgrounds already
    // use, for the same reason.
    //
    // clearFirst=false is for a caller that has already painted
    // something it wants kept -- ALERT drawing the live background
    // behind these icons. The icons themselves are all opaque fills, so
    // they read fine over a busy backdrop; it is only the erase that has
    // to be skipped, and that caller takes on the job of repainting the
    // region itself (which the animated backgrounds all do anyway).
    if (clearFirst) t.fillRect(0, 0, w, h, BG);
    int cx = w / 2;
    // The rest of this screen's text layout is dense (title, target
    // type, confidence, vendor, MAC, RSSI, radar all stacked between
    // y=4 and y=220 at fixed pixel positions) — centering a new icon
    // at h/2 would sit right on top of the RSSI line and the radar
    // widget. Anchoring to the bottom of whatever height this rotation
    // actually has, with icons kept small, gives it real clearance on
    // the common portrait rotation without needing to redo the whole
    // screen's layout for this.
    int cy = h - 46;

    switch (type) {
        case DetectionType::AIRTAG: {
            // An apple — not the corporate logo, just a plain apple —
            // since that's the one everyone already associates with
            // AirTag/FindMy.
            float bob = sinf((float)(now % 2000) / 2000.0f * 6.2831853f) * 3.0f;
            int ay = cy + (int)bob;
            int r = 16;
            t.fillCircle(cx - 6, ay, r, RED);
            t.fillCircle(cx + 6, ay, r, RED);
            t.fillRect(cx - 6, ay - r, 12, r + 5, RED);
            t.fillCircle(cx + r - 3, ay - r + 5, 5, BG);           // bite notch
            t.fillRect(cx - 1, ay - r - 6, 2, 7, AMBER);           // stem
            t.fillTriangle(cx, ay - r - 3, cx + 10, ay - r - 8, cx + 6, ay - r - 1, GREEN); // leaf
            alertFxPing(t, cx, ay, now, VAPOR_PURPLE, 2200, 42);
            break;
        }
        case DetectionType::SAMSUNG_TAG:
        case DetectionType::GOOGLE_TAG:
        case DetectionType::TILE: {
            // Generic keyring tracker tag (not either company's real
            // logo) — same location-ping language as AirTag above, so
            // the tracker family reads as a family, minus the fruit.
            t.fillRoundRect(cx - 13, cy - 9, 26, 18, 5, VAPOR_PURPLE);
            t.fillCircle(cx - 8, cy, 3, BG);
            alertFxPing(t, cx, cy, now, VAPOR_PURPLE, 2200, 42);
            break;
        }
        case DetectionType::FLOCK:
        case DetectionType::AXON:
        case DetectionType::ALPR:
        case DetectionType::CAMERA:
        case DetectionType::RING: {
            // Camera body + lens, a blinking REC dot, and a real
            // shutter flash every couple seconds.
            uint16_t tint = colorFor(type);
            uint32_t fc = now % 2400;
            if (fc < 120) {
                float f = 1.0f - (float)fc / 120.0f;
                t.fillRect(0, 0, w, h, blend(BG, WHITE, (uint16_t)(f * 200.0f)));
            }
            t.fillRoundRect(cx - 20, cy - 13, 40, 26, 5, blend(BG, tint, 70));
            t.drawCircle(cx, cy, 11, tint);
            t.drawCircle(cx, cy, 6, tint);
            if ((now / 500) % 2 == 0) t.fillCircle(cx + 15, cy - 8, 2, RED);
            break;
        }
        case DetectionType::META: {
            // Sunglasses — matches Squachy's own look — with a glint
            // sweeping across the lenses.
            uint16_t tint = colorFor(type);
            t.fillRoundRect(cx - 20, cy - 5, 15, 11, 3, BLACK);
            t.fillRoundRect(cx + 5,  cy - 5, 15, 11, 3, BLACK);
            t.fillRect(cx - 5, cy - 1, 10, 2, BLACK);
            t.fillRoundRect(cx - 18, cy - 4, 11, 8, 2, blend(BLACK, tint, 60));
            t.fillRoundRect(cx + 7,  cy - 4, 11, 8, 2, blend(BLACK, tint, 60));
            float sweep = (float)(now % 1800) / 1800.0f;
            int gx = cx - 18 + (int)(sweep * 39);
            t.drawFastVLine(gx, cy - 4, 8, WHITE);
            break;
        }
        case DetectionType::SKIMMER: {
            // Card shape with a scanning line — something's wrong with
            // this one.
            t.fillRoundRect(cx - 21, cy - 13, 42, 26, 4, blend(BG, VAPOR_YELLOW, 60));
            t.drawRoundRect(cx - 21, cy - 13, 42, 26, 4, VAPOR_YELLOW);
            t.fillRect(cx - 21, cy - 6, 42, 4, BLACK);
            float sweep = (float)(now % 1200) / 1200.0f;
            int sy = cy - 13 + (int)(sweep * 26);
            t.drawFastHLine(cx - 21, sy, 42, RED);
            break;
        }
        case DetectionType::RAVEN: {
            // An acoustic event, not an object — concentric rings from
            // a burst point rather than any kind of icon.
            alertFxPing(t, cx, cy, now, RED, 1400, 46);
            alertFxPing(t, cx, cy, now, AMBER, 1400, 30);
            break;
        }
        case DetectionType::DRONE: {
            // Quadcopter silhouette, hovering, with alternating rotor
            // rings standing in for motion blur.
            float bob = sinf((float)(now % 1600) / 1600.0f * 6.2831853f) * 4.0f;
            int dy = cy + (int)bob;
            t.fillRoundRect(cx - 7, dy - 4, 14, 8, 2, VAPOR_PURPLE);
            int arm = 17;
            bool rotorPhase = ((now / 120) % 2) == 0;
            static const int8_t ox[4] = { -1, 1, -1, 1 };
            static const int8_t oy[4] = { -1, -1, 1, 1 };
            for (uint8_t i = 0; i < 4; i++) {
                int rx = cx + ox[i] * arm, ry = dy + oy[i] * (arm / 2);
                t.drawLine(cx, dy, rx, ry, VAPOR_PURPLE);
                t.drawCircle(rx, ry, rotorPhase ? 6 : 4, blend(BG, VAPOR_PURPLE, 150));
            }
            break;
        }
        default:
            // UNKNOWN and anything else — no specific icon makes sense,
            // so keep the plain sweep, just properly erased each frame
            // now instead of trailing.
            t.drawFastHLine(0, (int)(now / 90) % h, w, VAPOR_PURPLE);
            break;
    }
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

void drawDigitalRain(TFT_eSPI& t, uint32_t now, int yStart, int yEnd, bool advance) {
    // Dense columns with long, smoothly-decaying trails. Glyphs are plain
    // ASCII (the default GLCD font can't render UTF-8 katakana correctly)
    // from a dense symbol/letter/digit set. Column count adapts to width so
    // this works from 240px portrait through cyd35's 480px landscape.
    //
    // The rule here is ADD, never subtract. An earlier pass at this shortened
    // the trails and dimmed most columns, which bought 2.6ms of frame time
    // nobody had asked for and cost the thing the effect is actually for --
    // there was simply less rain. Trails are longer than the original now,
    // not shorter, and the cheap wins below (white-hot heads, per-drop
    // colour, shimmer, glow) all cost either nothing or O(cols).
    static const int  MAX_COLS = 96;
    static const int  SPACING  = 5;
    static const char GLYPHS[] =
        "01" "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "!@#$%^&*<>{}[]/\\|+=~" "SASQUACH";
    static const int  GLN      = sizeof(GLYPHS) - 1;
    static const int  MAXTRAIL = 24;
    static const int  MINTRAIL = 17;      // averages ~21, just under the old flat 22
    static const uint16_t HUES[4] = { VAPOR_PINK, CYAN, GREEN, VAPOR_PURPLE };

    int cols = t.width() / SPACING;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (cols < 1) cols = 1;

    static int16_t yPos[MAX_COLS];
    static uint8_t ySpeed[MAX_COLS];
    static uint8_t yTick[MAX_COLS];
    // Per-DROP, re-rolled every time a column recycles. Hue used to be
    // HUES[i % 3], which made column 0 permanently pink, column 1
    // permanently cyan and so on -- a fixed stripe pattern across the whole
    // screen that never changed for the life of the boot, and the most
    // obviously machine-made thing about the effect.
    static uint8_t colHue[MAX_COLS];
    static uint8_t colLen[MAX_COLS];
    // Depth, 0 far .. 255 near, driving brightness and fall speed together.
    // Deliberately a NARROW range: pushing the far end down to 27% made half
    // the screen look washed out rather than distant.
    static uint8_t colDepth[MAX_COLS];
    // A rare drop that is longer, faster and burns white most of the way
    // down. Costs nothing extra -- it is a column that already exists.
    static bool    colSurge[MAX_COLS];
    static uint8_t charBuf[MAX_COLS][MAXTRAIL];
    // Wind, lightning and splashes. All three are O(cols) or O(1), and none
    // of them draws an extra glyph cell -- cells are the only thing on this
    // screen that costs real pixels, so motion is where the budget goes.
    // Wind is precomputed per (depth band, row) rather than per cell. The
    // first version did windOff[j] * colDepth[i] / 255 inside the inner
    // loop -- a multiply and a divide on every one of ~1300 cells a frame,
    // which measured 10ms. Four depth bands is visually indistinguishable
    // and turns the inner loop back into a table lookup and an add.
    static const uint8_t WINDBANDS = 4;
    static int8_t   windTab[WINDBANDS][MAXTRAIL];
    // Glyphs that break loose, grow and fade. Drawn with a transparent
    // background so they read as rising OFF the rain rather than punching
    // holes in it -- at most five, so a handful of extra glyphs a frame.
    static const uint8_t NPOP = 5;
    static uint16_t popX[NPOP], popY[NPOP];
    static uint8_t  popAge[NPOP], popHue[NPOP], popCh[NPOP];
    static uint32_t boltAt = 0, boltNext = 0;
    static uint16_t splashX[6];
    static uint8_t  splashAge[6], splashHue[6];
    static bool     fxInit = false;
    static bool    initialized = false;
    static int     lastCols = -1;

    auto respawn = [&](int i, bool anywhere) {
        colSurge[i] = (random(0, 14) == 0);
        colDepth[i] = colSurge[i] ? 255 : (uint8_t)random(0, 256);
        // Far drops fall slower; a surge drops fastest of all. ySpeed is a
        // tick divider, so smaller is faster.
        ySpeed[i]   = colSurge[i] ? 2
                                  : (uint8_t)(5 - ((uint16_t)colDepth[i] * 3u) / 255u);
        if (ySpeed[i] < 1) ySpeed[i] = 1;
        colHue[i]   = (uint8_t)random(0, 4);
        colLen[i]   = colSurge[i] ? MAXTRAIL : (uint8_t)random(MINTRAIL, MAXTRAIL + 1);
        yTick[i]    = (uint8_t)random(0, ySpeed[i] ? ySpeed[i] : 1);
        yPos[i]     = anywhere ? (int16_t)random(yStart, yEnd)
                               : (int16_t)(yStart - random(0, 90));
    };

    if (!initialized || cols != lastCols) {
        for (int i = 0; i < cols; i++) {
            respawn(i, true);
            for (int j = 0; j < MAXTRAIL; j++) charBuf[i][j] = (uint8_t)random(0, GLN);
        }
        initialized = true;
        lastCols = cols;
    }

    if (!fxInit) {
        for (uint8_t k = 0; k < 6; k++) splashAge[k] = 0;
        for (uint8_t k = 0; k < NPOP; k++) popAge[k] = 0;
        boltNext = now + (uint32_t)random(14000, 32000);
        fxInit = true;
    }

    // WIND. One shared curve sampled per ROW of the trail, not per column,
    // so a drop bends into an S rather than the whole field sliding sideways
    // together. Twenty-four sinf calls a frame gives every cell on screen a
    // horizontal offset for one array lookup and one add each.
    const float gust = 2.4f + sinf((float)now / 5300.0f) * 1.8f;
    for (int j = 0; j < MAXTRAIL; j++) {
        const float w = sinf((float)now / 1450.0f + (float)j * 0.17f) * gust;
        for (uint8_t b = 0; b < WINDBANDS; b++)
            windTab[b][j] = (int8_t)(w * (float)(b + 1) / (float)WINDBANDS);
    }

    // LIGHTNING. Every 14-32s the field flares white for ~140ms and decays.
    // Costs one comparison: it scales an alpha that is already computed.
    if (advance && now >= boltNext) {
        boltAt   = now;
        boltNext = now + (uint32_t)random(14000, 32000);
    }
    const uint32_t boltAge = now - boltAt;
    const uint16_t boltAmt = (boltAt && boltAge < 140)
                             ? (uint16_t)(160u - (boltAge * 160u) / 140u) : 0;

    // Full-band clear every frame, same as every other background style.
    // Without it a column that just wrapped skips its narrow vertical strip
    // for several frames, and nothing else ever repaints that strip -- so
    // anything drawn over it last frame (Squachy included, since he no
    // longer erases his own footprint) is left behind as a stale smear.
    t.fillRect(0, yStart, t.width(), yEnd - yStart, BG);

    t.setTextSize(1);
    // Column advance is gated: yTick is a call-counted divider, not
    // now-based, so calling this twice per logical frame would otherwise
    // fall the rain at double speed.
    if (advance) {
        for (int i = 0; i < cols; i++) {
            // Two cells somewhere in the trail flicker to different glyphs.
            // Freezing characters entirely was the right fix for the old
            // reshuffle-every-frame noise but went a step too far and left
            // the trails completely static. Rolling a couple of cells per
            // column keeps the shimmer at O(cols) rather than O(cells).
            charBuf[i][random(1, colLen[i])] = (uint8_t)random(0, GLN);
            if (random(0, 2)) charBuf[i][random(1, colLen[i])] = (uint8_t)random(0, GLN);

            if (++yTick[i] >= ySpeed[i]) {
                yTick[i] = 0;
                yPos[i] += 8;
                // A fresh glyph enters at the head; everything already in
                // the buffer shifts one slot further from it.
                for (int j = MAXTRAIL - 1; j > 0; j--) charBuf[i][j] = charBuf[i][j - 1];
                charBuf[i][0] = (uint8_t)random(0, GLN);
                // SPLASH. A head reaching the floor throws a brief flare
                // sideways -- the one place the rain previously just
                // stopped existing. Six slots, oldest reused.
                if (yPos[i] >= yEnd - 8 && yPos[i] < yEnd) {
                    uint8_t slot = 0, oldest = 0;
                    for (uint8_t k = 0; k < 6; k++) {
                        if (splashAge[k] == 0) { slot = k; break; }
                        if (splashAge[k] > oldest) { oldest = splashAge[k]; slot = k; }
                    }
                    splashX[slot]   = (uint16_t)(3 + i * SPACING);
                    splashHue[slot] = colHue[i];
                    splashAge[slot] = 1;
                }
                // Occasionally a glyph breaks off the head and floats.
                if (random(0, 90) == 0 && yPos[i] > yStart + 20 && yPos[i] < yEnd - 20) {
                    for (uint8_t k = 0; k < NPOP; k++) {
                        if (popAge[k]) continue;
                        popX[k]   = (uint16_t)(3 + i * SPACING);
                        popY[k]   = (uint16_t)yPos[i];
                        popCh[k]  = charBuf[i][0];
                        popHue[k] = colHue[i];
                        popAge[k] = 1;
                        break;
                    }
                }
                if (yPos[i] > yEnd + colLen[i] * 8) respawn(i, false);
            }
        }
    }

    for (int i = 0; i < cols; i++) {
        const uint16_t hue   = HUES[colHue[i]];
        const int      len   = colLen[i];
        const bool     surge = colSurge[i];
        // Depth as a brightness ceiling, 170..255. Narrow on purpose.
        const uint16_t deep = (uint16_t)(170u + ((uint16_t)colDepth[i] * 85u) / 255u);
        const int xBase = 3 + i * SPACING;
        // Near drops lean further than far ones, so a gust reads with depth
        // instead of shunting the whole screen at once.
        const int8_t* windRow = windTab[colDepth[i] >> 6];
        for (int j = 0; j < len; j++) {
            const int16_t ry = (int16_t)(yPos[i] - j * 8);
            if (ry < yStart || ry >= yEnd) continue;
            // Near drops lean further than far ones, so a gust reads with
            // depth instead of shunting the whole screen at once.
            const int x = xBase + windRow[j];

            // Fade in across the top few rows. Drops used to appear at full
            // brightness the instant they crossed yStart, which popped.
            uint16_t edge = 255;
            if (ry < yStart + 16) edge = (uint16_t)(((ry - yStart) * 255) / 16);

            const char buf[2] = { GLYPHS[charBuf[i][j]], 0 };
            uint16_t fg, bg;
            // A white-hot core that decays INTO the hue, rather than the head
            // simply being the hue. The leading character is the brightest
            // thing on screen and the colour trails behind it. On a surge the
            // white runs three cells deep instead of one.
            const int hot = surge ? 3 : 1;
            if (j < hot) {
                const uint16_t a = (uint16_t)(((uint32_t)deep * edge) / 255u);
                fg = blend(BG, WHITE, a);
                (void)boltAmt;
                // The glow is free: the glyph's own opaque background fill is
                // drawn either way, so it is tinted rather than left at BG.
                bg = blend(BG, hue, (uint16_t)(a / (surge ? 3u : 4u)));
            } else {
                const float f = 1.0f - (float)(j - hot) / (float)(len - hot);
                uint32_t a = (uint32_t)((surge ? 245.0f : 225.0f) * f * f);
                a = (a * deep) / 255u;
                a = (a * edge) / 255u;
                // Lightning lifts the trail toward white without touching
                // the heads, which are already white -- so the flash reads
                // as the whole field catching the light, not as a fade.
                fg = boltAmt ? blend(blend(BG, hue, (uint16_t)a), WHITE, boltAmt)
                             : blend(BG, hue, (uint16_t)a);
                bg = (j <= hot + 1) ? blend(BG, hue, (uint16_t)(a / 6u)) : BG;
            }
            t.setTextColor(fg, bg);
            t.setCursor(x, ry);
            t.print(buf);
        }
    }

    // Grow-and-fade glyphs. Size steps 1 -> 2 -> 3 over the life while the
    // colour washes out, and each one drifts upward, so a character looks
    // like it is lifting off the screen toward the viewer. Transparent
    // background (single-argument setTextColor) is what makes it overlay
    // the rain instead of stamping a black box over it.
    if (advance) for (uint8_t k = 0; k < NPOP; k++) if (popAge[k]) {
        if (++popAge[k] > 15) popAge[k] = 0;
    }
    for (uint8_t k = 0; k < NPOP; k++) {
        if (!popAge[k]) continue;
        const uint8_t age = popAge[k];
        const uint8_t sz  = (age < 5) ? 1 : (age < 10 ? 2 : 3);
        const uint16_t a  = (uint16_t)(235u - (uint16_t)age * 15u);
        const char pb[2] = { GLYPHS[popCh[k]], 0 };
        t.setTextSize(sz);
        t.setTextColor(blend(BG, HUES[popHue[k]], a));
        // Re-centre as it grows so it swells about its own middle rather
        // than expanding down and to the right off its anchor.
        t.setCursor((int)popX[k] - (sz - 1) * 3, (int)popY[k] - age - (sz - 1) * 4);
        t.print(pb);
    }
    t.setTextSize(1);

    // Splashes last ~8 ticks, spreading and fading. Two short horizontal
    // strokes each, so the whole effect is at most twelve drawFastHLine
    // calls in a frame where any are alive at all.
    if (advance) for (uint8_t k = 0; k < 6; k++) if (splashAge[k]) {
        if (++splashAge[k] > 8) splashAge[k] = 0;
    }
    for (uint8_t k = 0; k < 6; k++) {
        if (!splashAge[k]) continue;
        const uint8_t  age  = splashAge[k];
        const int      sp   = age * 2;
        const uint16_t a    = (uint16_t)(200u - (uint16_t)age * 24u);
        const uint16_t col  = blend(BG, HUES[splashHue[k]], a);
        const int      sy   = yEnd - 2;
        t.drawFastHLine((int)splashX[k] - sp, sy, sp, col);
        t.drawFastHLine((int)splashX[k] + 1,  sy, sp, col);
    }
}

// Defined below, next to the aquarium that first needed it. Declared
// here because drawStarfield sits earlier in the file and its nebula
// gradient is exactly the kind of smooth dark ramp RGB332 bands worst.
static uint16_t ditherRGB(TFT_eSPI& t, float r, float g, float b, uint8_t cell);

// Hue helper for the nebula and the warp tint. Only used by the
// starfield, which is the one background that wants arbitrary hues
// rather than the fixed theme palette.
static void hsv2rgb(float h, float s, float v, float& r, float& g, float& b) {
    h -= floorf(h);
    const float i = floorf(h * 6.0f);
    const float f = h * 6.0f - i;
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float u = v * (1.0f - (1.0f - f) * s);
    switch ((int)i % 6) {
        case 0:  r = v; g = u; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = u; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = u; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

// The junk that comes through the portals. Everything is drawn from
// primitives at an arbitrary size, so the same routine covers a speck on
// the horizon and something filling a third of the screen -- which is
// the whole trick behind objects that fly AT you rather than across.
//
// Every object opens with a black underlay: the one or two shapes that
// define its outer contour, drawn a pixel or two oversized in black,
// before the real art goes on top. That single pass is the biggest
// readability win here -- without it these dissolve into a moving
// starfield, because nothing separates object from background. It is
// the same reason the boot subtitle carries a drop shadow.
//
// Below about 4px none of the detail survives, so they degrade to a
// coloured blob rather than a smear of overlapping circles.
static const uint8_t JUNK_KINDS = 8;
static void drawJunk(TFT_eSPI& t, uint8_t kind, int x, int y, int s, uint32_t now) {
    using namespace Theme;
    if (s < 4) {
        static const uint16_t FAR_TINT[JUNK_KINDS] = { 0 };
        (void)FAR_TINT;
        t.fillCircle(x, y, s < 1 ? 1 : s, VAPOR_PINK);
        return;
    }
    // Proportional helper: every offset below is a fraction of s, so the
    // art scales without a second set of numbers.
    auto P = [s](float f) { return (int)(s * f); };

    switch (kind) {
        case 0: {   // eyeball
            const uint16_t sclera = WHITE;
            const uint16_t shade  = t.color565(236, 226, 234);
            t.fillCircle(x, y, s + 1, BLACK);
            t.fillCircle(x, y, s, sclera);
            t.fillCircle(x - P(0.06f), y + P(0.10f), P(0.94f), shade);
            t.fillCircle(x, y - P(0.06f), P(0.90f), sclera);
            t.fillCircle(x + P(0.16f), y, P(0.58f), t.color565(10, 48, 120));
            t.fillCircle(x + P(0.16f), y, P(0.50f), t.color565(26, 112, 224));
            for (uint8_t k = 0; k < 12; k++) {          // iris spokes
                const float a = (float)k * 0.5236f;
                t.drawLine(x + P(0.16f) + (int)(cosf(a) * P(0.20f)),
                           y            + (int)(sinf(a) * P(0.20f)),
                           x + P(0.16f) + (int)(cosf(a) * P(0.48f)),
                           y            + (int)(sinf(a) * P(0.48f)),
                           t.color565(13, 74, 168));
            }
            t.fillCircle(x + P(0.16f), y, P(0.24f), BLACK);
            t.fillCircle(x - P(0.08f), y - P(0.36f), P(0.17f), sclera);
            t.fillCircle(x + P(0.40f), y + P(0.30f), P(0.07f) + 1, sclera);
            const uint16_t vein = t.color565(208, 32, 32);
            t.drawLine(x - P(0.96f), y - P(0.34f), x - P(0.34f), y - P(0.16f), vein);
            t.drawLine(x - P(0.90f), y + P(0.44f), x - P(0.28f), y + P(0.24f), vein);
            t.drawLine(x - P(0.62f), y - P(0.62f), x - P(0.30f), y - P(0.40f), vein);
            break;
        }
        case 1: {   // a face, mid-scream
            t.fillCircle(x, y, s + 1, BLACK);
            t.fillCircle(x, y, s, t.color565(255, 233, 92));
            t.fillCircle(x, y + P(0.10f), P(0.94f), t.color565(245, 197, 24));
            t.fillCircle(x, y - P(0.08f), P(0.86f), t.color565(255, 233, 92));
            t.fillCircle(x - P(0.60f), y + P(0.28f), P(0.20f), t.color565(240, 168, 0));
            t.fillCircle(x + P(0.60f), y + P(0.28f), P(0.20f), t.color565(240, 168, 0));
            t.fillEllipse(x - P(0.40f), y - P(0.24f), P(0.22f) + 1, P(0.30f) + 1, BLACK);
            t.fillEllipse(x + P(0.40f), y - P(0.24f), P(0.22f) + 1, P(0.30f) + 1, BLACK);
            t.fillCircle(x - P(0.34f), y - P(0.34f), P(0.08f), WHITE);
            t.fillCircle(x + P(0.46f), y - P(0.34f), P(0.08f), WHITE);
            const uint16_t brow = t.color565(122, 82, 0);
            t.drawWideLine(x - P(0.66f), y - P(0.62f), x - P(0.18f), y - P(0.48f), P(0.13f) + 1, brow);
            t.drawWideLine(x + P(0.18f), y - P(0.48f), x + P(0.66f), y - P(0.62f), P(0.13f) + 1, brow);
            t.fillEllipse(x, y + P(0.46f), P(0.42f), P(0.34f), BLACK);
            t.fillEllipse(x, y + P(0.60f), P(0.24f), P(0.16f), t.color565(208, 48, 74));
            t.fillRect(x - P(0.26f), y + P(0.16f), P(0.16f) + 1, P(0.13f) + 1, WHITE);
            t.fillRect(x + P(0.10f), y + P(0.16f), P(0.16f) + 1, P(0.13f) + 1, WHITE);
            t.fillCircle(x + P(0.92f), y - P(0.62f), P(0.13f), t.color565(127, 212, 255));
            break;
        }
        case 2: {   // saucer, with an occupant
            const uint16_t beam = blend(BG, t.color565(120, 240, 180), 80);
            t.fillTriangle(x - P(0.28f), y + P(0.24f), x + P(0.62f), y + P(1.05f),
                           x - P(0.62f), y + P(1.05f), beam);
            t.fillTriangle(x - P(0.28f), y + P(0.24f), x + P(0.28f), y + P(0.24f),
                           x + P(0.62f), y + P(1.05f), beam);
            t.fillEllipse(x, y + P(0.06f), s + 1, P(0.34f) + 2, BLACK);
            t.fillEllipse(x, y + P(0.34f), P(0.80f), P(0.26f), t.color565(58, 42, 96));
            t.fillEllipse(x, y + P(0.06f), s, P(0.34f), t.color565(125, 136, 168));
            t.fillEllipse(x, y - P(0.02f), P(0.96f), P(0.26f), t.color565(170, 182, 212));
            t.fillEllipse(x, y - P(0.08f), P(0.90f), P(0.16f), t.color565(214, 224, 244));
            t.fillCircle(x, y - P(0.34f), P(0.44f) + 1, BLACK);
            t.fillCircle(x, y - P(0.34f), P(0.44f), t.color565(42, 208, 255));
            t.fillCircle(x, y - P(0.32f), P(0.36f), t.color565(156, 240, 255));
            t.fillCircle(x, y - P(0.30f), P(0.17f), t.color565(26, 106, 80));
            t.fillCircle(x - P(0.07f), y - P(0.36f), P(0.05f) + 1, BLACK);
            t.fillCircle(x + P(0.07f), y - P(0.36f), P(0.05f) + 1, BLACK);
            t.fillCircle(x - P(0.16f), y - P(0.48f), P(0.10f), WHITE);
            for (int k = -2; k <= 2; k++) {
                t.fillCircle(x + k * P(0.36f), y + P(0.16f), P(0.10f) + 1,
                             (k & 1) ? AMBER : PINK);
            }
            t.drawWideLine(x - P(0.30f), y + P(0.30f), x - P(0.42f), y + P(0.62f),
                           P(0.09f) + 1, t.color565(92, 102, 132));
            break;
        }
        case 3: {   // CRT television
            const uint16_t chassis = t.color565(138, 138, 160);
            const uint16_t hi      = t.color565(198, 198, 222);
            const uint16_t lo      = t.color565(92, 96, 112);
            t.drawWideLine(x - P(0.26f), y - P(0.62f), x - P(0.86f), y - P(1.30f), 2, hi);
            t.drawWideLine(x + P(0.26f), y - P(0.62f), x + P(0.86f), y - P(1.30f), 2, hi);
            t.fillCircle(x - P(0.86f), y - P(1.30f), P(0.09f) + 1, WHITE);
            t.fillCircle(x + P(0.86f), y - P(1.30f), P(0.09f) + 1, WHITE);
            t.fillRect(x - P(0.62f), y + P(0.72f), P(0.20f) + 1, P(0.26f) + 1, lo);
            t.fillRect(x + P(0.42f), y + P(0.72f), P(0.20f) + 1, P(0.26f) + 1, lo);
            t.fillRect(x - s - 1, y - P(0.66f) - 1, 2 * s + 3, P(1.40f) + 3, BLACK);
            t.fillRect(x - s, y - P(0.66f), 2 * s, P(1.40f), chassis);
            t.fillRect(x - s, y - P(0.66f), 2 * s, P(0.14f) + 1, hi);
            t.fillRect(x - s, y + P(0.62f), 2 * s, P(0.12f) + 1, lo);
            t.fillRect(x - P(0.86f), y - P(0.52f), P(1.42f), P(1.08f), t.color565(16, 16, 32));
            static const uint16_t BAR[5] = { 0, 0, 0, 0, 0 };
            (void)BAR;
            const uint16_t bars[5] = { PINK, AMBER, VAPOR_YELLOW, CYAN, GREEN };
            for (uint8_t k = 0; k < 5; k++) {
                t.fillRect(x - P(0.82f) + k * P(0.27f), y - P(0.48f),
                           P(0.25f) + 1, P(1.00f), bars[k]);
            }
            t.fillRect(x - P(0.82f), y - P(0.20f), P(1.35f), P(0.10f) + 1, WHITE);
            t.fillRect(x + P(0.56f), y - P(0.52f), P(0.30f), P(1.08f), chassis);
            t.fillCircle(x + P(0.76f), y - P(0.24f), P(0.13f) + 1, t.color565(58, 58, 74));
            t.fillCircle(x + P(0.76f), y - P(0.24f), P(0.07f), hi);
            t.fillCircle(x + P(0.76f), y + P(0.10f), P(0.13f) + 1, t.color565(58, 58, 74));
            t.fillCircle(x + P(0.76f), y + P(0.10f), P(0.07f), hi);
            for (uint8_t k = 0; k < 3; k++) {
                t.fillRect(x + P(0.66f), y + P(0.34f) + k * (P(0.09f) + 1),
                           P(0.22f), P(0.05f) + 1, lo);
            }
            break;
        }
        case 4: {   // burger
            const uint16_t bunTop = t.color565(240, 180, 92);
            const uint16_t bunLo  = t.color565(217, 144, 56);
            t.fillEllipse(x, y - P(0.20f), s + 1, P(0.68f) + 1, BLACK);
            t.fillEllipse(x, y - P(0.20f), s, P(0.66f), bunTop);
            t.fillRect(x - s, y - P(0.20f), 2 * s, P(0.24f), bunLo);
            t.fillRect(x - s, y - P(0.36f), 2 * s, P(0.22f), bunTop);
            const float sx[5] = { -0.62f, -0.20f, 0.24f, 0.62f, 0.02f };
            const float sy[5] = { -0.62f, -0.72f, -0.68f, -0.56f, -0.50f };
            for (uint8_t k = 0; k < 5; k++) {
                t.fillEllipse(x + P(sx[k]), y + P(sy[k]), P(0.11f) + 1, P(0.07f) + 1,
                              t.color565(255, 242, 204));
            }
            const uint16_t lettuce = t.color565(63, 191, 95);
            t.fillRect(x - P(1.02f), y - P(0.16f), P(2.04f), P(0.16f) + 1, lettuce);
            for (int k = -3; k <= 3; k++) t.fillCircle(x + k * P(0.30f), y - P(0.04f), P(0.15f), lettuce);
            t.fillEllipse(x, y + P(0.06f), P(0.94f), P(0.14f) + 1, t.color565(216, 56, 40));
            t.fillEllipse(x, y + P(0.04f), P(0.72f), P(0.08f) + 1, t.color565(240, 96, 80));
            t.fillRect(x - P(0.90f), y + P(0.14f), P(1.80f), P(0.16f) + 1, t.color565(255, 192, 32));
            t.fillRect(x - P(0.58f), y + P(0.28f), P(0.20f), P(0.18f), t.color565(255, 192, 32));
            t.fillRect(x + P(0.34f), y + P(0.28f), P(0.20f), P(0.16f), t.color565(255, 192, 32));
            t.fillRect(x - P(0.94f), y + P(0.28f), P(1.88f), P(0.32f), t.color565(122, 61, 22));
            t.fillRect(x - P(0.94f), y + P(0.28f), P(1.88f), P(0.08f) + 1, t.color565(152, 81, 31));
            t.fillEllipse(x + P(0.74f), y + P(0.22f), P(0.22f), P(0.09f) + 1, t.color565(87, 176, 74));
            t.fillEllipse(x, y + P(0.56f), P(0.94f) + 1, P(0.32f) + 1, BLACK);
            t.fillEllipse(x, y + P(0.54f), P(0.94f), P(0.30f), t.color565(224, 162, 78));
            t.fillRect(x - P(0.94f), y + P(0.36f), P(1.88f), P(0.18f), t.color565(224, 162, 78));
            break;
        }
        case 5: {   // pizza
            t.fillTriangle(x, y - s - 1, x - P(0.92f), y + P(0.84f),
                           x + P(0.92f), y + P(0.84f), BLACK);
            t.fillTriangle(x, y - P(1.02f), x - P(0.90f), y + P(0.82f),
                           x + P(0.90f), y + P(0.82f), t.color565(232, 176, 64));
            t.fillTriangle(x, y - P(0.82f), x - P(0.72f), y + P(0.64f),
                           x + P(0.72f), y + P(0.64f), t.color565(192, 72, 40));
            t.fillTriangle(x, y - P(0.66f), x - P(0.60f), y + P(0.52f),
                           x + P(0.60f), y + P(0.52f), t.color565(248, 208, 96));
            t.fillTriangle(x, y - P(0.60f), x - P(0.34f), y + P(0.10f),
                           x + P(0.34f), y + P(0.10f), t.color565(255, 230, 148));
            t.fillEllipse(x, y + P(0.80f), P(0.94f), P(0.26f), t.color565(216, 152, 64));
            t.fillRect(x - P(0.92f), y + P(0.66f), P(1.84f), P(0.16f) + 1, t.color565(216, 152, 64));
            const float bx[3] = { -0.55f, 0.0f, 0.55f };
            for (uint8_t k = 0; k < 3; k++) {
                t.fillCircle(x + P(bx[k]), y + P(0.80f), P(0.09f) + 1, t.color565(168, 106, 32));
            }
            const float px[3] = {  0.00f, -0.28f,  0.30f };
            const float py[3] = { -0.20f,  0.26f,  0.22f };
            const float pr[3] = {  0.19f,  0.16f,  0.16f };
            for (uint8_t k = 0; k < 3; k++) {
                const int r = P(pr[k]) + 1;
                t.fillCircle(x + P(px[k]), y + P(py[k]), r, t.color565(142, 28, 28));
                t.fillCircle(x + P(px[k]), y + P(py[k]), (r * 72) / 100, t.color565(212, 58, 42));
                t.fillCircle(x + P(px[k]) - (r * 28) / 100, y + P(py[k]) - (r * 28) / 100,
                             (r * 24) / 100, t.color565(240, 106, 82));
            }
            const float hx[3] = { -0.14f, 0.20f, -0.34f };
            const float hy[3] = {  0.50f, -0.44f, -0.10f };
            for (uint8_t k = 0; k < 3; k++) {
                t.fillEllipse(x + P(hx[k]), y + P(hy[k]), P(0.09f) + 1, P(0.05f) + 1,
                              t.color565(47, 143, 58));
            }
            break;
        }
        case 6: {   // toilet, lid down
            const uint16_t porc = t.color565(228, 233, 242);
            const uint16_t lit  = WHITE;
            const uint16_t shad = t.color565(185, 194, 212);
            t.fillRect(x - P(0.78f), y - P(1.08f), P(1.56f), P(0.22f) + 2, BLACK);
            t.fillRect(x - P(0.76f), y - P(1.06f), P(1.52f), P(0.18f) + 1, porc);
            t.fillRect(x - P(0.76f), y - P(1.06f), P(1.52f), P(0.07f) + 1, lit);
            t.fillRect(x - P(0.76f), y - P(0.90f), P(1.52f), P(0.05f) + 1, shad);
            t.fillRect(x - P(0.68f), y - P(0.90f), P(1.36f), P(0.72f), BLACK);
            t.fillRect(x - P(0.66f), y - P(0.88f), P(1.32f), P(0.68f), porc);
            t.fillRect(x - P(0.66f), y - P(0.26f), P(1.32f), P(0.10f) + 1, shad);
            t.fillRect(x + P(0.50f), y - P(0.66f), P(0.26f), P(0.14f) + 1, t.color565(200, 160, 32));
            t.fillCircle(x - P(0.86f), y - P(0.60f), P(0.18f) + 1, lit);
            t.fillCircle(x - P(0.86f), y - P(0.60f), P(0.07f), shad);
            t.fillEllipse(x, y + P(0.14f), P(0.88f) + 1, P(0.48f) + 1, BLACK);
            t.fillEllipse(x, y + P(0.14f), P(0.88f), P(0.48f), porc);
            t.fillEllipse(x, y + P(0.06f), P(0.80f), P(0.40f), lit);
            t.fillEllipse(x, y + P(0.10f), P(0.62f), P(0.30f), t.color565(147, 163, 192));
            t.fillEllipse(x, y + P(0.12f), P(0.50f), P(0.23f), t.color565(47, 159, 216));
            t.fillEllipse(x - P(0.14f), y + P(0.06f), P(0.22f), P(0.09f) + 1, t.color565(143, 224, 255));
            t.fillRect(x - P(0.32f), y + P(0.50f), P(0.64f), P(0.42f), BLACK);
            t.fillRect(x - P(0.30f), y + P(0.52f), P(0.60f), P(0.40f), t.color565(223, 228, 238));
            t.fillRect(x - P(0.30f), y + P(0.52f), P(0.12f) + 1, P(0.40f), lit);
            t.fillEllipse(x, y + P(0.92f), P(0.56f), P(0.16f) + 1, porc);
            break;
        }
        default: {  // rubber duck
            const uint16_t body = t.color565(255, 200, 32);
            const uint16_t lit  = t.color565(255, 224, 96);
            const uint16_t shad = t.color565(240, 170, 0);
            t.fillEllipse(x - P(0.05f), y + P(0.66f), P(1.05f), P(0.22f) + 1,
                          blend(BG, t.color565(120, 200, 255), 90));
            t.fillTriangle(x - P(0.78f), y + P(0.10f), x - P(1.24f), y - P(0.28f),
                           x - P(0.66f), y - P(0.16f), t.color565(255, 180, 0));
            t.fillEllipse(x - P(0.08f), y + P(0.30f), P(0.94f) + 1, P(0.54f) + 1, BLACK);
            t.fillEllipse(x - P(0.08f), y + P(0.30f), P(0.94f), P(0.54f), body);
            t.fillEllipse(x - P(0.08f), y + P(0.16f), P(0.86f), P(0.36f), lit);
            t.fillEllipse(x - P(0.18f), y + P(0.30f), P(0.50f), P(0.28f), shad);
            for (int k = -2; k <= 2; k++) {
                t.fillCircle(x - P(0.18f) + k * P(0.17f), y + P(0.50f), P(0.10f) + 1, shad);
            }
            t.fillEllipse(x - P(0.22f), y + P(0.22f), P(0.34f), P(0.16f) + 1, t.color565(255, 210, 60));
            t.fillCircle(x + P(0.50f), y - P(0.42f), P(0.46f) + 1, BLACK);
            t.fillCircle(x + P(0.50f), y - P(0.42f), P(0.46f), body);
            t.fillCircle(x + P(0.44f), y - P(0.52f), P(0.34f), lit);
            t.fillRect(x + P(0.84f), y - P(0.38f), P(0.46f), P(0.22f) + 1, t.color565(255, 140, 16));
            t.fillRect(x + P(0.84f), y - P(0.24f), P(0.36f), P(0.11f) + 1, t.color565(216, 96, 0));
            t.fillCircle(x + P(1.02f), y - P(0.34f), P(0.04f) + 1, t.color565(160, 70, 0));
            t.fillCircle(x + P(0.56f), y - P(0.56f), P(0.13f) + 1, BLACK);
            t.fillCircle(x + P(0.60f), y - P(0.60f), P(0.05f) + 1, WHITE);
            t.drawWideLine(x + P(0.44f), y - P(0.74f), x + P(0.68f), y - P(0.72f),
                           P(0.07f) + 1, t.color565(201, 138, 0));
            t.fillCircle(x + P(0.26f), y - P(0.26f), P(0.11f) + 1, t.color565(255, 157, 176));
            break;
        }
    }
}

void drawStarfield(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    const int w     = t.width();
    const int bandH = yEnd - yStart;
    if (bandH < 8) return;

    // The tube wanders rather than staring down a fixed pipe. Everything
    // else in the scene -- stars, planets, junk -- is projected from the
    // same centre, so the whole thing banks together.
    const float wob = (float)now / 2860.0f;
    const int   cx  = w / 2              + (int)(sinf(wob) * 14.0f);
    const int   cy  = yStart + bandH / 2 + (int)(cosf(wob * 1.29f) * 9.0f);
    const float aspect = (float)bandH / (float)w * 1.25f;
    const float maxR   = sqrtf((float)(w * w + bandH * bandH)) * 0.62f;

    // Flat clear. The old nebula gradient was ~190 dithered drawFastHLine
    // calls a frame for a wash that mostly read as murk; the tunnel below
    // gives the band its colour now, for a fraction of the pixels.
    t.fillRect(0, yStart, w, bandH, BG);

    // ---- warp stars, behind the tube ------------------------------------
    static const uint8_t NS = 72;
    static float  sx[NS], sy[NS], sz[NS];
    static bool   starsInit = false;
    static uint32_t warpNext = 0;
    static float  warp = 1.0f;
    if (!starsInit) {
        for (uint8_t i = 0; i < NS; i++) {
            sx[i] = (float)random(-200, 201);
            sy[i] = (float)random(-160, 161);
            sz[i] = (float)random(20, 420);
        }
        starsInit = true;
        warpNext  = now + (uint32_t)random(4000, 9000);
    }
    float targetWarp = 1.0f;
    if (now >= warpNext) {
        if (now < warpNext + 1900) targetWarp = 7.0f;
        else                       warpNext   = now + (uint32_t)random(6000, 14000);
    }
    warp += (targetWarp - warp) * 0.07f;

    const float step = 1.4f + warp * 1.9f;
    for (uint8_t i = 0; i < NS; i++) {
        const float zPrev = sz[i];
        sz[i] -= step;
        if (sz[i] < 6.0f) {
            sx[i] = (float)random(-200, 201);
            sy[i] = (float)random(-160, 161);
            sz[i] = (float)random(330, 430);
            continue;
        }
        const float k = 110.0f / sz[i];
        const int   x = cx + (int)(sx[i] * k);
        const int   y = cy + (int)(sy[i] * k);
        if (x < 0 || x >= w || y < yStart || y >= yEnd) continue;
        const float near = 1.0f - sz[i] / 430.0f;
        const uint8_t bri = (uint8_t)(70.0f + 185.0f * (near < 0.0f ? 0.0f : near));
        const uint16_t col = t.color565(bri, bri, bri > 235 ? 255 : bri + 20);
        if (warp > 1.6f) {
            const float kp = 110.0f / zPrev;
            t.drawLine(cx + (int)(sx[i] * kp), cy + (int)(sy[i] * kp), x, y, col);
        } else {
            t.drawPixel(x, y, col);
        }
    }

    // ---- the tube --------------------------------------------------------
    // Concentric rings, the whole stack scrolling outward, drawn as
    // OUTLINES: the filled version matches the reference more exactly but
    // repaints the whole band every frame, which is the cost the nebula
    // was already paying. Seven sides keeps it angular; a rounder tube
    // stops reading as facets and starts reading as circles.
    //
    // Two things have to stay continuous across the scroll or the tube
    // visibly flashes once per cycle:
    //
    //  * COLOUR. The blue/violet alternation is a period-2 pattern, so the
    //    scroll phase has to run over 2 rings, not 1. On a period-1 wrap
    //    the geometry ring i had is inherited by ring i+1, whose parity is
    //    the opposite -- so the entire tube inverted its colours every
    //    1.8s. Over a period of 2 the geometry passes to ring i+2, which
    //    has the same parity, and nothing changes at the seam.
    //
    //  * ROTATION. Giving each ring its own extra twist made a spiral, but
    //    the twist was keyed to the index too, so the same wrap snapped
    //    the whole tube round by 0.13 rad. Every ring now shares one
    //    rotation: concentric rather than spiralled, and seamless.
    //
    // Because the phase now travels two ring-widths, the outermost ring
    // shrinks to 55% of the band before it recycles. The loop starts two
    // rings further out so there is always geometry beyond the corners.
    static const uint8_t RINGS = 15;
    static const uint8_t SIDES = 7;
    // Both the outward scroll and the rotation ride the same warp the
    // stars do, so a burst accelerates the whole scene together instead of
    // the stars streaking past a tube that carries on at its own pace.
    //
    // That means accumulating rather than deriving from `now`: a phase
    // computed straight from the clock cannot change speed without also
    // jumping, because the value has to stay continuous while its
    // derivative changes. dt is clamped so a long stall (a scan, a screen
    // that blocked) cannot fling the tunnel forward on the next frame.
    static uint32_t tunLast  = 0;
    static float    tunPhase = 0.0f, tunSpin = 0.0f;
    uint32_t dt = (tunLast && now > tunLast) ? (now - tunLast) : 16u;
    if (dt > 100u) dt = 100u;
    tunLast = now;
    // warp rests at 1.0 and peaks near 7.0, so this is 1x at rest and
    // about 3.7x flat out -- the tube pulls, without outrunning the stars.
    const float rush = 0.55f + warp * 0.45f;
    tunPhase = fmodf(tunPhase + (float)dt * 0.00055f * rush, 2.0f);
    tunSpin += (float)dt * 0.000555f * rush;
    const float phase = tunPhase;
    const float spin  = tunSpin;
    const float drift = sinf((float)now / 4000.0f) * 0.03f;

    for (int i = RINGS - 1; i >= -2; i--) {
        const float fi = (float)i + phase;
        const float r  = maxR * expf(-fi * 0.30f);
        if (r < 3.0f || r > maxR * 1.9f) continue;
        float d = fi / (float)RINGS;
        if (d < 0.0f) d = 0.0f;

        // Variant D: electric blue alternating with purple. The violet was
        // originally hue 0.74 at 55% value, which lands on (73,0,170) once
        // RGB332 has had it -- only two bits of blue and a red channel too
        // dark to survive, so it read as a dimmer blue rather than a
        // different hue. Pushing toward magenta and raising the value puts
        // red on a level that quantisation keeps: (146,0,170), which reads
        // as purple against the (0,0,255) rings.
        float rr, gg, bb;
        if (i & 1) hsv2rgb(0.66f + drift, 1.0f, 1.00f - d * 0.62f, rr, gg, bb);
        else       hsv2rgb(0.80f + drift, 1.0f, 0.72f - d * 0.42f, rr, gg, bb);
        const uint16_t col = t.color565((uint8_t)(rr * 255.0f),
                                        (uint8_t)(gg * 255.0f),
                                        (uint8_t)(bb * 255.0f));
        const int lw = (int)(4.2f * (1.0f - d));
        const float rot = spin;

        int px = cx + (int)(cosf(rot) * r);
        int py = cy + (int)(sinf(rot) * r * aspect);
        for (uint8_t k = 1; k <= SIDES; k++) {
            const float a = rot + (float)k * (6.2831853f / SIDES);
            const int nx = cx + (int)(cosf(a) * r);
            const int ny = cy + (int)(sinf(a) * r * aspect);
            // NOT drawWideLine: that is TFT_eSPI's anti-aliased wedge
            // routine, which alpha-blends every pixel it touches. With
            // 15 rings x 7 sides it measured 69ms of drawing per frame,
            // three times the entire rest of the scene. Parallel
            // Bresenham lines give the same visual weight for a small
            // fraction of that. Offset across the segment's minor axis
            // so near-vertical edges actually thicken.
            if (lw >= 2) {
                const bool horiz = (nx - px) * (nx - px) >= (ny - py) * (ny - py);
                for (int o = 0; o < lw; o++) {
                    const int d = o - lw / 2;
                    if (horiz) t.drawLine(px, py + d, nx, ny + d, col);
                    else       t.drawLine(px + d, py, nx + d, ny, col);
                }
            } else {
                t.drawLine(px, py, nx, ny, col);
            }
            px = nx; py = ny;
        }
    }

    // ---- planets ---------------------------------------------------------
    // Drawn as horizontal chords rather than stacked circles: one span per
    // row means the bands and the terminator come out of the same loop,
    // and a planet costs about 2r line draws instead of a pile of fills.
    static const uint8_t NP = 2;
    static float   px_[NP], py_[NP], pvx[NP];
    static uint8_t pr_[NP], ppal[NP];
    static bool    planetsInit = false;
    if (!planetsInit) {
        for (uint8_t i = 0; i < NP; i++) {
            px_[i]  = (float)random(0, w);
            py_[i]  = (float)(yStart + random(bandH / 6, bandH * 5 / 6));
            pvx[i]  = 0.10f + (float)random(0, 12) / 100.0f;
            pr_[i]  = (uint8_t)random(8, 20);
            ppal[i] = (uint8_t)random(0, 3);
        }
        planetsInit = true;
    }
    static const uint8_t PAL[3][9] = {
        { 200,106, 60,  224,138, 74,  168, 80, 44 },   // rust
        {  70,120,190,   96,160,220,   48, 84,150 },   // ice
        { 150, 90,180,  186,124,214,  110, 58,140 },   // violet
    };
    for (uint8_t i = 0; i < NP; i++) {
        px_[i] += pvx[i];
        if (px_[i] - pr_[i] > (float)w) {
            px_[i]  = -(float)pr_[i] - 2.0f;
            py_[i]  = (float)(yStart + random(bandH / 6, bandH * 5 / 6));
            pr_[i]  = (uint8_t)random(8, 20);
            pvx[i]  = 0.10f + (float)random(0, 12) / 100.0f;
            ppal[i] = (uint8_t)random(0, 3);
        }
        const int   R  = pr_[i];
        const int   ox = (int)px_[i];
        const int   oy = (int)py_[i];
        if (oy - R < yStart || oy + R >= yEnd) continue;
        const uint8_t* p = PAL[ppal[i]];
        for (int dy = -R; dy <= R; dy++) {
            const int hw = (int)sqrtf((float)(R * R - dy * dy));
            if (hw < 1) continue;
            // Three latitude bands, picked by row.
            const int b = ((dy + R) * 3) / (2 * R + 1);
            const uint16_t lit = t.color565(p[b * 3], p[b * 3 + 1], p[b * 3 + 2]);
            t.drawFastHLine(ox - hw, oy + dy, hw * 2 + 1, lit);
            // Terminator: the trailing third falls into shadow.
            const int sw = (hw * 2 + 1) / 3;
            if (sw > 0) {
                const uint16_t dark = t.color565(p[b * 3] / 3, p[b * 3 + 1] / 3, p[b * 3 + 2] / 3);
                t.drawFastHLine(ox + hw - sw, oy + dy, sw, dark);
            }
        }
    }

    // ---- junk ------------------------------------------------------------
    // Rare arrivals rather than a permanent crowd: at most two on screen,
    // one turning up every 4-7 seconds, flying out of the vanishing point
    // straight at the viewer. Five of them milling about was both busier
    // than the scene wanted and, measured on hardware, about 14ms a frame.
    static const uint8_t NJ = 2;
    static float   jx[NJ], jy[NJ], jz[NJ];
    static uint8_t jkind[NJ];
    static bool    jlive[NJ];
    static bool    junkInit = false;
    static uint32_t jNextAt = 0;
    if (!junkInit) {
        for (uint8_t i = 0; i < NJ; i++) jlive[i] = false;
        jNextAt  = now + (uint32_t)random(1500, 4000);
        junkInit = true;
    }
    if (now >= jNextAt) {
        for (uint8_t i = 0; i < NJ; i++) {
            if (jlive[i]) continue;
            // Just off the vanishing point, so it grows out of the tube
            // rather than fading in somewhere arbitrary.
            // Spawn on a ring, never near dead centre. A piece with a
            // small offset flies straight down the barrel at the viewer
            // and never clears the middle of the screen; giving every
            // one a real radial offset means they all peel outward and
            // leave by an edge.
            const float a   = (float)random(0, 628) / 100.0f;
            const float rad = (float)random(42, 88);
            jz[i]    = 300.0f;
            jx[i]    = cosf(a) * rad;
            jy[i]    = sinf(a) * rad * 0.78f;
            jkind[i] = (uint8_t)random(0, JUNK_KINDS);
            jlive[i] = true;
            break;
        }
        jNextAt = now + (uint32_t)random(6500, 11000);
    }
    for (uint8_t i = 0; i < NJ; i++) {
        if (!jlive[i]) continue;
        // Closing at a constant rate is the wrong curve: apparent size
        // goes as 1/z, so a piece stays a speck for most of its flight
        // and is only briefly big. Making the step proportional to the
        // remaining distance flips that -- it rushes out of the
        // vanishing point, then slows as it fills out, spending roughly
        // 40% of its life small and 60% large and heading for an edge.
        jz[i] -= (0.030f * jz[i] + 0.35f) * (1.0f + warp * 0.5f);
        if (jz[i] < 6.0f) { jlive[i] = false; continue; }
        const float k = 110.0f / jz[i];
        const int   x = cx + (int)(jx[i] * k);
        const int   y = cy + (int)(jy[i] * k);
        int s = (int)(60.0f * k);
        if (s < 2)  s = 2;
        if (s > 52) s = 52;
        if (x + s < 0 || x - s >= w || y + s < yStart || y - s >= yEnd) {
            // Gone past an edge: retire it now so the next arrival can
            // use the slot, rather than tracking an invisible object.
            if (jz[i] < 120.0f) jlive[i] = false;
            continue;
        }
        drawJunk(t, jkind[i], x, y, s, now);
    }

    // ---- channel change ---------------------------------------------------
    static uint32_t glitchAt = 0;
    static bool     glitchInit = false;
    if (!glitchInit) { glitchAt = now + (uint32_t)random(6000, 14000); glitchInit = true; }
    if (now >= glitchAt && now < glitchAt + 240) {
        for (int k = 0; k < 5; k++) {
            const int gy  = yStart + (int)random(0, bandH - 6);
            const int gh  = (int)random(2, 7);
            const int off = (int)random(-20, 21);
            t.fillRect(off, gy, w, gh,
                       blend(BG, (random(0, 2) ? CYAN : VAPOR_PINK), (uint16_t)random(90, 190)));
        }
    } else if (now >= glitchAt + 240) {
        glitchAt = now + (uint32_t)random(8000, 18000);
    }
}

// TFT_eSPI has fillTriangle but no polygon fill, and a wing lobe is an
// 18-vertex shape. Fanning it into triangles costs ~16 fillTriangle calls
// per wing, and each of those runs its own scanline pass over a bounding
// box that overlaps its neighbours'. One scanline pass over the whole
// polygon is a single drawFastHLine per row instead -- about 30 row fills
// for a wing, and a row fill is the cheapest thing the sprite can do.
//
// Even-odd rule, so a wing that curls back over itself still fills
// correctly. MAXHIT is generous: the curled variants cross a scanline
// four times at most.
static void fillPoly(TFT_eSPI& t, const int16_t* xs, const int16_t* ys,
                     uint8_t n, uint16_t col) {
    if (n < 3) return;
    int16_t ymin = ys[0], ymax = ys[0];
    for (uint8_t i = 1; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    static const uint8_t MAXHIT = 12;
    for (int16_t y = ymin; y <= ymax; y++) {
        int16_t xh[MAXHIT];
        uint8_t hits = 0;
        for (uint8_t i = 0; i < n && hits < MAXHIT; i++) {
            const uint8_t j = (uint8_t)((i + 1) % n);
            const int16_t y1 = ys[i], y2 = ys[j];
            if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y)) {
                xh[hits++] = (int16_t)(xs[i] +
                    (int32_t)(y - y1) * (xs[j] - xs[i]) / (y2 - y1));
            }
        }
        for (uint8_t a = 1; a < hits; a++) {          // tiny insertion sort
            const int16_t v = xh[a];
            int8_t b = (int8_t)a - 1;
            while (b >= 0 && xh[b] > v) { xh[b + 1] = xh[b]; b--; }
            xh[b + 1] = v;
        }
        for (uint8_t a = 0; (uint8_t)(a + 1) < hits; a += 2)
            t.drawFastHLine(xh[a], y, xh[a + 1] - xh[a] + 1, col);
    }
}

// Wing width along its length, sampled at the 9 points the wing is built
// from. Baked rather than evaluated because the real curve is
// (1-t^5)^0.45 * (0.4 + 0.6*min(1, t/0.3)), and two powf() calls per point
// per wing per sprite per frame is a lot of transcendental for a shape
// that never changes. The profile holds its width through the middle and
// drops only at the very end -- a width that falls off linearly gives a
// spike, and the tip has to read as rounded.
static const float WING_PROFILE[9] = {
    0.400f, 0.650f, 0.900f, 0.997f, 0.986f, 0.956f, 0.885f, 0.724f, 0.0f
};

// One rounded bird wing: a curved lobe with a few feather divisions drawn
// back onto it. It beats by swinging about the shoulder, which is what a
// bird does -- a wing that only slides up and down reads as being dragged.
void drawWing(TFT_eSPI& t, float sx, float sy, float len, float angDeg,
              float flap, float width, uint8_t ndiv,
              uint16_t body, uint16_t edge, float curl, float lift,
              uint16_t shade) {
    static const uint8_t N = 8;
    const float step = len / (float)N;
    const float da   = curl * 60.0f * 0.017453293f / (float)N;
    float a  = (angDeg + flap * lift) * 0.017453293f;
    float px = sx, py = sy;

    int16_t tx[N + 1], ty[N + 1], bx[N + 1], by[N + 1];
    int16_t cx[N + 1], cy[N + 1];
    for (uint8_t i = 0; i <= N; i++) {
        const float wgt = WING_PROFILE[i] * width * len;
        const float nx = -sinf(a), ny = cosf(a);
        cx[i] = (int16_t)px;              cy[i] = (int16_t)py;
        tx[i] = (int16_t)(px + nx * wgt * 0.58f);
        ty[i] = (int16_t)(py + ny * wgt * 0.58f);
        bx[i] = (int16_t)(px - nx * wgt * 0.44f);
        by[i] = (int16_t)(py - ny * wgt * 0.44f);
        px += cosf(a) * step;
        py += sinf(a) * step;
        a  += da;
    }

    int16_t hx[2 * (N + 1)], hy[2 * (N + 1)];
    uint8_t n = 0;
    for (uint8_t i = 0; i <= N; i++)      { hx[n] = tx[i]; hy[n] = ty[i]; n++; }
    for (int8_t i = (int8_t)N; i >= 0; i--) { hx[n] = bx[i]; hy[n] = by[i]; n++; }
    fillPoly(t, hx, hy, n, body);

    for (uint8_t i = 0; i < n; i++) {
        const uint8_t j = (uint8_t)((i + 1) % n);
        t.drawLine(hx[i], hy[i], hx[j], hy[j], edge);
    }
    // Shade whichever edge actually ends up LOWER on screen, rather than
    // always the same array. The flock only ever wears wings on one flank
    // so this never showed there, but Squachy wears a mirrored pair -- and
    // mirroring flips the sign of the normal, which swaps which of top/bot
    // is the lower edge. A fixed choice therefore put the shadow under one
    // wing and over the other, which reads as one wing being upside down.
    const bool shadeTop = ty[N / 2] > by[N / 2];
    const int16_t* sxArr = shadeTop ? tx : bx;
    const int16_t* syArr = shadeTop ? ty : by;

    // 2*(N+1), not N+3. The shade polygon walks the centreline out and the
    // shaded edge back, so it holds two runs of (N-1) points -- 14 at N=8,
    // where N+3 is 11. The three-entry overflow ran off the end of shx into
    // shy, so three coordinates became garbage and fillPoly drew spans to
    // wherever they landed: stray white lines trailing off the wings.
    int16_t shx[2 * (N + 1)], shy[2 * (N + 1)];
    uint8_t sn = 0;
    for (uint8_t i = 2; i <= N; i++) { shx[sn] = cx[i]; shy[sn] = cy[i]; sn++; }
    for (int8_t i = (int8_t)N; i >= 2; i--) { shx[sn] = sxArr[i]; shy[sn] = syArr[i]; sn++; }
    fillPoly(t, shx, shy, sn, shade);

    for (uint8_t d = 1; d <= ndiv; d++) {
        uint8_t i0 = (uint8_t)((float)N * (0.26f + 0.16f * (float)d));
        uint8_t i1 = (uint8_t)(i0 + 2);
        if (i1 > N) i1 = N;
        if (i1 <= i0) break;
        t.drawLine(cx[i0], cy[i0], sxArr[i1], syArr[i1], edge);
    }
}

// HIGH SWEEP: chrome body, two slots, and rounded wings mounted on the
// SIDES -- far wing, then the body, then the near wing, so the body sits
// between them the way a bird's does. Shoulders are set high and both
// wings sweep upward, which is the posture that reads most like a bird
// rather than a box with fins.
//
// The chrome ramp is chosen by what it QUANTISES to, not by how it looks
// unquantised. RGB332 gives blue only four levels, so an obvious-looking
// chrome like (200,205,228) lands on (219,219,255) -- lavender. These sit
// on (182,182,170), the closest neutral available at this lightness.
static void drawToasterAt(TFT_eSPI& t, int x, int y, uint32_t now, uint16_t bodyCol, float scale) {
    auto S = [scale](float v) { return v * scale; };
    const int bw = (int)S(44.0f), bh = (int)S(30.0f);

    const uint16_t chHi  = t.color565(250, 250, 250);
    // Shade toward a dark BLUE, not toward BG. RGB332 has no neutral
    // mid-grey: anything around 100-130 brightness has its blue snap down
    // to 85 while red and green hold at 109, giving (109,109,85) -- olive,
    // which is the exact cast this sprite exists to avoid. Blending
    // toward BG measured (146,146,170), (109,109,85), (36,36,0): two of
    // the three shade tones were olive. Carrying blue through the blend
    // keeps them on (146,146,170) and (109,109,170), and a slightly cool
    // chrome is right where a warm one is simply wrong.
    const uint16_t shadeTo = t.color565(48, 48, 168);
    const uint16_t chMid = blend(bodyCol, shadeTo, 70);
    const uint16_t chLo  = blend(bodyCol, shadeTo, 130);
    const uint16_t chDk  = blend(bodyCol, shadeTo, 200);
    // (52,52,60) quantises to (36,36,0) -- a dark olive keyline round a
    // chrome body. Blue needs to clear 64 to land on 85 at all.
    const uint16_t edge  = t.color565(52, 52, 96);
    const uint16_t slot  = t.color565(18, 18, 24);
    const uint16_t glow  = t.color565(150, 88, 30);
    const uint16_t glow2 = t.color565(110, 50, 20);
    const uint16_t wh    = t.color565(252, 252, 252);
    const uint16_t wh3   = t.color565(170, 174, 190);

    // 500 ms beat, offset per sprite so a flock does not pulse in unison.
    const float flap = sinf((float)((now + (uint32_t)x * 37u) % 500u) / 500.0f * 6.2831853f);

    // The flock climbs up and to the RIGHT (see drawFlyingToasters), so the
    // sprite has to face that way -- wings trailing behind on the left, the
    // lever on the leading edge. The art was authored facing left, matching
    // the source animation, so everything below is mirrored about the body
    // centre: an x becomes (x + bw - x), an angle becomes (180 - angle),
    // and the wing curl becomes its own negative.
    //
    // The far wing is deliberately NOT a straight mirror of the near one.
    // Mirrored literally it pointed up-and-FORWARD, ahead of the leading
    // edge, which read as a wing being shoved through the air rather than
    // one holding the toaster up. It now sits further back along the body
    // and sweeps up-and-back, so it reads as the far wing seen past the
    // shell instead of a second wing on the wrong side.
    //
    // Both wings take the SAME flap sign. Opposite signs made them
    // scissor -- spreading apart and closing again -- because the lift
    // term is added to each wing's own base angle, and the two base
    // angles already point different ways. Same sign walks both toward
    // 270 degrees together, which is a bird beating rather than a pair
    // of shears.
    drawWing(t, x + bw - S(13), y + S(15.5f), S(28), 248.0f, flap, 0.47f, 3,
             wh, wh3, -0.55f, 20.0f);

    t.fillRoundRect(x, y, bw, bh, (int)S(8), bodyCol);
    t.fillRect(x + (int)S(4), y + bh - (int)S(9),  bw - (int)S(8),  (int)S(4), chMid);
    t.fillRect(x + (int)S(4), y + bh - (int)S(5),  bw - (int)S(8),  (int)S(3), chLo);
    t.fillRect(x + (int)S(7), y + bh - (int)S(16), bw - (int)S(15), (int)S(2), chHi);

    // Two slots, running straight across. The body is drawn axis-aligned,
    // so its side edges are vertical and the slots have to be exactly
    // perpendicular to them -- the earlier slant was borrowed from the
    // source's 3/4 view and read as a mistake against straight sides.
    // Axis-aligned also means these are plain rects rather than polygons:
    // four fillRect instead of four scanline fills.
    for (uint8_t i = 0; i < 2; i++) {
        const int yy = y + (int)S(5.0f + (float)i * 7.0f);
        t.fillRect(x + (int)S(9),  yy,             bw - (int)S(17), (int)S(4), slot);
        t.fillRect(x + (int)S(11), yy + (int)S(1), bw - (int)S(21), (int)S(2),
                   i ? glow2 : glow);
    }

    t.fillRect(x + bw - (int)S(7), y + bh - (int)S(16), (int)S(4), (int)S(10), chDk);
    t.fillRect(x + bw - (int)S(6), y + bh - (int)S(15), (int)S(2), (int)S(8),  slot);

    // Against a black field an unedged chrome body has no silhouette at
    // all -- it just fades out along the bottom.
    t.drawRoundRect(x, y, bw, bh, (int)S(8), edge);

    drawWing(t, x + bw - S(17), y + S(16.5f), S(35), 214.0f, flap, 0.51f, 3,
             wh, wh3, -0.55f, 20.0f);
}

// A slice of toast, flying under its own power just like the toasters --
// the signature After Dark gag. INNER CRUMB: the source draws it as an
// isometric slab with real crust thickness, not as the upright rounded
// square this used to be. Two flat tones for the face (crust rim, pale
// middle) rather than a gradient, which would band in RGB332.
//
// x,y is the top-left of the whole slab including its thickness.
static void drawToastAt(TFT_eSPI& t, int x, int y, uint32_t now, bool hasFace,
                        float scale, bool burnt) {
    auto S = [scale](float v) { return v * scale; };
    const float hw = S(26.0f), hh = S(14.0f), th = S(7.0f);
    const float cx = x + hw, cy = y + hh;

    // Every so often one comes out cremated -- straight from the original,
    // where a blackened slice turns up among the golden ones. The burnt
    // ramp is picked for RGB332 the same way the chrome was: (120,90,20)
    // lands on (109,73,0) and (80,50,15) on (73,36,0), both real browns,
    // where an obvious-looking charcoal would collapse to flat black and
    // lose the slab's faces entirely.
    const uint16_t top   = burnt ? t.color565(120,  90, 20) : t.color565(233, 190, 120);
    const uint16_t crumb = burnt ? t.color565(150, 115, 30) : t.color565(246, 213, 158);
    const uint16_t crust = burnt ? t.color565( 80,  50, 15) : t.color565(198, 132, 56);
    const uint16_t edge  = burnt ? t.color565( 40,  14, 10) : t.color565(150,  90, 34);

    int16_t px[4], py[4];
    // Front-left crust wall, then front-right: the two faces you can see.
    px[0] = (int16_t)(cx - hw); py[0] = (int16_t)cy;
    px[1] = (int16_t)cx;        py[1] = (int16_t)(cy + hh);
    px[2] = (int16_t)cx;        py[2] = (int16_t)(cy + hh + th);
    px[3] = (int16_t)(cx - hw); py[3] = (int16_t)(cy + th);
    fillPoly(t, px, py, 4, edge);

    px[0] = (int16_t)cx;        py[0] = (int16_t)(cy + hh);
    px[1] = (int16_t)(cx + hw); py[1] = (int16_t)cy;
    px[2] = (int16_t)(cx + hw); py[2] = (int16_t)(cy + th);
    px[3] = (int16_t)cx;        py[3] = (int16_t)(cy + hh + th);
    fillPoly(t, px, py, 4, crust);

    // Top face, then the pale crumb inset -- the two-tone that actually
    // says "bread" rather than "gold tile".
    px[0] = (int16_t)cx;        py[0] = (int16_t)(cy - hh);
    px[1] = (int16_t)(cx + hw); py[1] = (int16_t)cy;
    px[2] = (int16_t)cx;        py[2] = (int16_t)(cy + hh);
    px[3] = (int16_t)(cx - hw); py[3] = (int16_t)cy;
    fillPoly(t, px, py, 4, top);

    px[0] = (int16_t)cx;              py[0] = (int16_t)(cy - hh + S(4));
    px[1] = (int16_t)(cx + hw - S(8)); py[1] = (int16_t)cy;
    px[2] = (int16_t)cx;              py[2] = (int16_t)(cy + hh - S(4));
    px[3] = (int16_t)(cx - hw + S(8)); py[3] = (int16_t)cy;
    fillPoly(t, px, py, 4, crumb);

    t.drawLine((int)cx, (int)(cy - hh), (int)(cx + hw), (int)cy, edge);
    t.drawLine((int)(cx + hw), (int)cy, (int)cx, (int)(cy + hh), edge);
    t.drawLine((int)cx, (int)(cy + hh), (int)(cx - hw), (int)cy, edge);
    t.drawLine((int)(cx - hw), (int)cy, (int)cx, (int)(cy - hh), edge);

    // A thread of smoke off a burnt one, drifting and thinning as it
    // rises. Phase is keyed off x so two burnt slices never smoke in step.
    if (burnt) {
        for (uint8_t k = 0; k < 5; k++) {
            const float up = (float)k * S(3.5f);
            const float sway = sinf((float)now / 240.0f + (float)k * 0.9f
                                    + (float)x * 0.13f) * S(2.6f) * ((float)k * 0.35f);
            const uint16_t a = (uint16_t)(120 - k * 22);
            t.drawPixel((int)(cx + sway), (int)(cy - hh - up), blend(BG, WHITE, a));
        }
    }

    // The fan-favourite face, kept for the minority of slices that already
    // got one, now sitting on the crumb panel.
    if (hasFace) {
        const uint16_t ink = t.color565(60, 36, 12);
        t.fillRect((int)(cx - S(7)), (int)(cy - S(3)), (int)S(3), (int)S(3), ink);
        t.fillRect((int)(cx + S(4)), (int)(cy - S(3)), (int)S(3), (int)S(3), ink);
        if (sinf((float)now / 500.0f + (float)x) > 0.0f) {
            t.drawLine((int)(cx - S(5)), (int)(cy + S(5)),
                       (int)(cx + S(5)), (int)(cy + S(5)), ink);
        } else {
            t.fillRect((int)(cx - S(2)), (int)(cy + S(3)), (int)S(4), (int)S(4), ink);
        }
    }
}

// Boris, Berkeley Systems' cat and the deepest cut in the whole After Dark
// catalogue -- he turned up across several of their modules. He drifts
// through batting at a slice of toast that travels just ahead of him, so
// the gag is self-contained rather than needing him to find real toast to
// interact with.
//
// Drawn as a side-on silhouette in two greys: at this size a cat reads by
// outline alone -- ears, back, haunch, tail -- and any interior detail
// beyond eyes and a nose just turns him into a smudge.
static void drawBorisAt(TFT_eSPI& t, int x, int y, uint32_t now, float scale, bool swipe) {
    auto S = [scale](float v) { return (int)(v * scale + 0.5f); };
    const uint16_t furD = t.color565(96, 96, 118);
    const uint16_t furL = t.color565(150, 150, 172);
    const uint16_t eye  = t.color565(210, 230, 90);
    const uint16_t pink = t.color565(230, 140, 160);

    // Tail: a swishing arc behind him, three segments so it curls.
    const float sw = sinf((float)now / 300.0f) * 0.55f;
    int tx0 = x + S(4), ty0 = y + S(17);
    for (uint8_t k = 0; k < 3; k++) {
        const float a = 3.3f + sw * (float)(k + 1) * 0.4f;
        const int nx = tx0 - (int)(cosf(a) * S(7));
        const int ny = ty0 - (int)(sinf(a) * S(7));
        t.drawLine(tx0, ty0, nx, ny, furD);
        t.drawLine(tx0, ty0 + 1, nx, ny + 1, furD);
        tx0 = nx; ty0 = ny;
    }

    // Body and haunch.
    t.fillRoundRect(x + S(4), y + S(10), S(24), S(12), S(5), furD);
    t.fillRoundRect(x + S(3), y + S(12), S(11), S(11), S(5), furL);
    // Head, with the ears as two triangles off the top.
    t.fillRoundRect(x + S(23), y + S(4), S(14), S(12), S(5), furL);
    t.fillTriangle(x + S(24), y + S(6), x + S(26), y + S(-1), x + S(30), y + S(5), furL);
    t.fillTriangle(x + S(32), y + S(5), x + S(36), y + S(-1), x + S(37), y + S(6), furL);
    t.fillTriangle(x + S(26), y + S(5), x + S(27), y + S(1),  x + S(29), y + S(5), pink);
    t.fillTriangle(x + S(33), y + S(5), x + S(35), y + S(1),  x + S(36), y + S(6), pink);
    // Eyes and nose.
    t.fillRect(x + S(27), y + S(8), S(3), S(3), eye);
    t.fillRect(x + S(33), y + S(8), S(3), S(3), eye);
    t.drawPixel(x + S(37), y + S(12), pink);

    // Front paw: tucked while drifting, thrown forward on the swipe.
    if (swipe) {
        t.fillRoundRect(x + S(34), y + S(13), S(12), S(5), S(2), furL);
        t.fillRect(x + S(45), y + S(13), S(2), S(2), pink);
    } else {
        t.fillRoundRect(x + S(24), y + S(18), S(9), S(5), S(2), furL);
    }
    // Back leg.
    t.fillRoundRect(x + S(6), y + S(19), S(9), S(5), S(2), furL);
}

// Mowin' Man, from the module of the same name -- a small figure who walks
// the bottom edge pushing a mower. The original mowed the desktop; there
// is no desktop here, so he mows a strip of grass that grows back behind
// him, which is the same joke without needing something to destroy.
static void drawMowinManAt(TFT_eSPI& t, int x, int baseY, uint32_t now, float scale) {
    auto S = [scale](float v) { return (int)(v * scale + 0.5f); };
    const uint16_t skin  = t.color565(232, 186, 140);
    const uint16_t shirt = t.color565(70, 120, 200);
    const uint16_t trous = t.color565(60, 60, 90);
    const uint16_t mower = t.color565(200, 60, 50);
    const uint16_t metal = t.color565(150, 150, 172);

    // Legs alternate on a walk cycle; the body bobs with it.
    const bool step = ((now / 180u) & 1u) != 0;
    const int bob = step ? 0 : S(1);

    t.fillRect(x + S(6), baseY - S(9) + bob,  S(3), S(9), trous);
    t.fillRect(x + (step ? S(10) : S(3)), baseY - S(9) + bob, S(3), S(9), trous);
    t.fillRoundRect(x + S(4), baseY - S(19) + bob, S(10), S(11), S(3), shirt);
    t.fillRect(x + S(13), baseY - S(17) + bob, S(7), S(3), skin);        // arms out to the handle
    t.fillCircle(x + S(9), baseY - S(22) + bob, S(4), skin);
    t.fillRect(x + S(5), baseY - S(26) + bob, S(9), S(3), t.color565(40, 40, 60));

    // Mower: handle up to his hands, deck on the ground, wheels.
    t.drawLine(x + S(19), baseY - S(16) + bob, x + S(27), baseY - S(4), metal);
    t.drawLine(x + S(20), baseY - S(16) + bob, x + S(28), baseY - S(4), metal);
    t.fillRoundRect(x + S(21), baseY - S(7), S(14), S(6), S(2), mower);
    t.fillCircle(x + S(23), baseY - S(1), S(2), trous);
    t.fillCircle(x + S(33), baseY - S(1), S(2), trous);
}

// Defined further down, next to backgroundTap() which consumes it.
static void publishGoldToaster(int cx, int cy, int hw, int hh, uint32_t now);

void drawFlyingToasters(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    static const uint8_t N = 5;
    static float    tx[N], ty[N], tscale[N];
    static uint16_t tcol[N];
    static const uint8_t NT = 6;
    static float    ox[NT], oy[NT], oscale[NT];
    static bool      oface[NT], oburnt[NT];
    static bool      inited = false;

    // Deep space behind the flock. The original art is just toasters on
    // black; this is a deliberate addition, so it stays quiet -- the
    // toasters are the subject and none of this is allowed to compete
    // with them for attention.
    static const uint8_t NSTAR = 44;
    static float   starX[NSTAR], starY[NSTAR];
    static uint8_t starMag[NSTAR], starPh[NSTAR];

    // One shooting star and one comet at a time, both usually absent.
    // Rarity is the whole point: something that happens continuously is
    // texture, and texture here would just be visual noise.
    static float    ssX = 0, ssY = 0, ssVX = 0, ssVY = 0;
    static uint16_t ssAge = 0, ssLife = 0;
    static uint32_t ssNext = 0;
    static float    cmX = 0, cmY = 0, cmVX = 0, cmVY = 0, cmTurn = 0;
    static bool     cmLive = false;
    static uint32_t cmNext = 0;
    // The tail is drawn through where the comet has actually BEEN, not
    // back along its current heading. A heading-derived tail is rigid: it
    // pivots as one piece and reads as a painted-on cone. A position
    // history lags, curves when the flight path curves, and straightens
    // out again behind -- which is the whole difference between a comet
    // and an arrow.
    static const uint8_t CMTRAIL = 30;
    static float    cmHx[CMTRAIL], cmHy[CMTRAIL];
    static uint8_t  cmHn = 0;

    // Two After Dark cameos, both rare enough to be a surprise rather than
    // scenery: Boris chasing a slice, and Mowin' Man working the bottom
    // edge. Neither is ever on screen at the same time as the other.
    static float    boX = 0, boY = 0, boS = 1.0f;
    static bool     boLive = false;
    static uint32_t boNext = 0;
    static float    mmX = 0;
    static bool     mmLive = false;
    static uint32_t mmNext = 0;
    // Grass Mowin' Man cuts. One byte per column: height now, regrowing
    // slowly behind him. A strip rather than a full lawn, because the flock
    // is the subject and a mown lawn across the whole band would take over.
    static const uint8_t GRASSW = 80;
    static uint8_t  grass[GRASSW];

    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 20) return;

    // The reference art is a flock on plain black. Stars, a shooting star
    // and a comet were added on top of that deliberately -- see the
    // starfield block below, which draws before the flock so everything
    // passes in front of it.
    // (190,190,150) quantises to (182,182,170) -- see drawToasterAt().
    uint16_t chromeCol = t.color565(190, 190, 150);

    if (!inited) {
        for (uint8_t i = 0; i < N; i++) {
            tx[i]     = (float)random(-w, w);
            ty[i]     = (float)random(yStart, yEnd - 12);
            tcol[i]   = chromeCol;
            tscale[i] = 0.55f + (float)random(0, 100) / 100.0f * 0.45f;
        }
        for (uint8_t i = 0; i < NT; i++) {
            ox[i]     = (float)random(-w, w);
            oy[i]     = (float)random(yStart, yEnd - 14);
            oface[i]  = random(0, 3) == 0;
            oburnt[i] = random(0, 7) == 0;
            oscale[i] = 0.50f + (float)random(0, 100) / 100.0f * 0.40f;
        }
        for (uint8_t i = 0; i < NSTAR; i++) {
            starX[i]   = (float)random(0, w);
            starY[i]   = (float)random(yStart, yEnd);
            // A handful of bright ones carry the field; the rest sit far
            // back. A uniform magnitude reads as a grid of dots.
            starMag[i] = (uint8_t)(random(0, 8) == 0 ? random(200, 256)
                                                     : random(70, 150));
            starPh[i]  = (uint8_t)random(0, 255);
        }
        ssNext = now + (uint32_t)random(2500, 7000);
        cmNext = now + (uint32_t)random(9000, 22000);
        boNext = now + (uint32_t)random(40000, 90000);
        mmNext = now + (uint32_t)random(55000, 120000);
        for (uint8_t i = 0; i < GRASSW; i++) grass[i] = (uint8_t)random(3, 7);
        inited = true;
    }

    t.fillRect(0, yStart, w, bandH, BG);

    // ---- starfield -------------------------------------------------------
    // Drifting down-left while the flock climbs up-right, which reads as
    // parallax for the cost of two adds. Twinkle is a sine on a per-star
    // phase rather than random(), so a star breathes instead of flickering.
    for (uint8_t i = 0; i < NSTAR; i++) {
        starX[i] -= 0.06f;
        starY[i] += 0.03f;
        if (starX[i] < 0.0f)          starX[i] = (float)w;
        if (starY[i] >= (float)yEnd)  starY[i] = (float)yStart;
        if (starY[i] < (float)yStart) starY[i] = (float)(yEnd - 1);

        const float tw = sinf((float)now / 900.0f + (float)starPh[i] * 0.0246f);
        int b = (int)starMag[i] + (int)(tw * 38.0f);
        if (b < 24)  b = 24;
        if (b > 255) b = 255;
        const uint16_t sc = t.color565((uint8_t)b, (uint8_t)b,
                                       (uint8_t)(b > 235 ? 255 : b + 20));
        t.drawPixel((int)starX[i], (int)starY[i], sc);
        // The brightest few get a one-pixel cross so they read as stars
        // rather than as dust.
        if (starMag[i] > 200) {
            const uint16_t dim = blend(BG, sc, 150);
            t.drawPixel((int)starX[i] - 1, (int)starY[i], dim);
            t.drawPixel((int)starX[i] + 1, (int)starY[i], dim);
            t.drawPixel((int)starX[i], (int)starY[i] - 1, dim);
            t.drawPixel((int)starX[i], (int)starY[i] + 1, dim);
        }
    }

    // ---- shooting star ---------------------------------------------------
    // Fast, short-lived, and gone. Drawn as three segments behind the head,
    // each dimmer than the last, so the streak tapers off instead of
    // ending in a hard stop.
    if (ssLife == 0 && now >= ssNext) {
        ssX    = (float)random(w / 4, w);
        ssY    = (float)random(yStart, yStart + bandH / 3);
        const float sp = 5.0f + (float)random(0, 40) / 10.0f;
        ssVX   = -sp * 0.86f;
        ssVY   =  sp * 0.50f;
        ssAge  = 0;
        ssLife = (uint16_t)random(16, 30);
    }
    if (ssLife > 0) {
        ssX += ssVX;
        ssY += ssVY;
        // Fade in over the first few frames and out over the last few, so
        // it neither appears nor vanishes as a hard pop.
        const uint16_t rem = (uint16_t)(ssLife - ssAge);
        uint16_t amp = 255;
        if (ssAge < 4) amp = (uint16_t)(64 * (ssAge + 1));
        if (rem  < 6)  amp = (uint16_t)(42 * rem);
        for (uint8_t k = 0; k < 3; k++) {
            const float t0 = (float)k * 2.4f, t1 = (float)(k + 1) * 2.4f;
            const uint16_t a = (uint16_t)((amp * (uint16_t)(200 - k * 62)) / 255);
            t.drawLine((int)(ssX - ssVX * t0), (int)(ssY - ssVY * t0),
                       (int)(ssX - ssVX * t1), (int)(ssY - ssVY * t1),
                       blend(BG, WHITE, a));
        }
        t.drawPixel((int)ssX, (int)ssY, blend(BG, WHITE, amp));
        if (++ssAge >= ssLife || ssX < -20.0f || ssY > (float)yEnd) {
            ssLife = 0;
            ssNext = now + (uint32_t)random(2500, 7000);
        }
    }

    // ---- comet -----------------------------------------------------------
    // Slower and much rarer than the shooting star, and built the other way
    // round: a solid head with a glow, and a trail that follows the path it
    // actually flew.
    if (!cmLive && now >= cmNext) {
        cmLive = true;
        cmX    = (float)(w + 30);
        cmY    = (float)random(yStart + 4, yEnd - bandH / 3);
        cmVX   = -(1.7f + (float)random(0, 120) / 100.0f);
        cmVY   =  (0.35f + (float)random(0, 55) / 100.0f);
        // A slow constant turn, direction and rate both random, so no two
        // passes trace the same arc.
        cmTurn = (float)random(-16, 17) / 12000.0f;
        cmHn   = 0;
    }
    if (cmLive) {
        // Curve the flight by rotating the velocity a little each frame.
        // Travelling dead straight is most of what made it look static.
        const float cs = cosf(cmTurn), sn = sinf(cmTurn);
        const float nvx = cmVX * cs - cmVY * sn;
        cmVY = cmVX * sn + cmVY * cs;
        cmVX = nvx;
        cmX += cmVX;
        cmY += cmVY;

        // Push the new position on, oldest falling off the end.
        for (uint8_t k = CMTRAIL - 1; k > 0; k--) {
            cmHx[k] = cmHx[k - 1];
            cmHy[k] = cmHy[k - 1];
        }
        cmHx[0] = cmX; cmHy[0] = cmY;
        if (cmHn < CMTRAIL) cmHn++;

        const uint16_t ICE = t.color565(190, 226, 255);
        // Walk the history from the far end forward, so brighter, wider
        // trail nearer the head simply paints over the dimmer tail behind
        // it -- no need to sort or blend anything.
        for (uint8_t k = (uint8_t)(cmHn - 1); k > 0; k--) {
            const float agef = 1.0f - (float)k / (float)CMTRAIL;   // 0 tail .. 1 head
            uint16_t a = (uint16_t)(20.0f + 200.0f * agef * agef);
            const uint16_t col = blend(BG, ICE, a);
            t.drawLine((int)cmHx[k], (int)cmHy[k],
                       (int)cmHx[k - 1], (int)cmHy[k - 1], col);
            // The trail thickens toward the head. Offset across the minor
            // axis so a near-horizontal trail actually gains height.
            if (agef > 0.58f) {
                const float dx = cmHx[k - 1] - cmHx[k], dy = cmHy[k - 1] - cmHy[k];
                const int off = (agef > 0.84f) ? 2 : 1;
                for (int o = 1; o <= off; o++) {
                    if (dx * dx >= dy * dy) {
                        t.drawLine((int)cmHx[k], (int)cmHy[k] - o,
                                   (int)cmHx[k - 1], (int)cmHy[k - 1] - o, col);
                        t.drawLine((int)cmHx[k], (int)cmHy[k] + o,
                                   (int)cmHx[k - 1], (int)cmHy[k - 1] + o, col);
                    } else {
                        t.drawLine((int)cmHx[k] - o, (int)cmHy[k],
                                   (int)cmHx[k - 1] - o, (int)cmHy[k - 1], col);
                        t.drawLine((int)cmHx[k] + o, (int)cmHy[k],
                                   (int)cmHx[k - 1] + o, (int)cmHy[k - 1], col);
                    }
                }
            }
        }

        // Head: a bright core that pulses, so it reads as burning rather
        // than as a drawn dot.
        const float pulse = sinf((float)now / 110.0f);
        t.fillCircle((int)cmX, (int)cmY, 3, WHITE);
        t.drawCircle((int)cmX, (int)cmY, 4, blend(BG, ICE, (uint16_t)(170 + pulse * 60.0f)));
        t.drawCircle((int)cmX, (int)cmY, 6, blend(BG, ICE, (uint16_t)(60 + pulse * 34.0f)));

        if (cmX < -70.0f || cmY > (float)yEnd + 24.0f || cmY < (float)yStart - 24.0f) {
            cmLive = false;
            cmNext = now + (uint32_t)random(9000, 22000);
        }
    }

    // Classic flight path: diagonally up and to the right, off the top
    // corner, re-entering from the lower-left. Every so often a
    // respawning toaster comes back gold-plated instead of chrome — a
    // rare shiny to spot, with a little sparkle trail while it lasts.
    uint16_t goldCol = t.color565(255, 215, 60);
    for (uint8_t i = 0; i < N; i++) {
        tx[i] += (0.6f + (float)(i % 3) * 0.25f) * tscale[i];
        ty[i] -= (0.15f + (float)(i % 2) * 0.1f) * tscale[i];
        if (tx[i] > w + 40 || ty[i] < (float)yStart - 44) {
            tx[i]     = (float)(-random(0, 40) - 70);
            ty[i]     = (float)random(yStart + 12, yEnd - 12);
            tcol[i]   = (random(0, 40) == 0) ? goldCol : chromeCol;
            tscale[i] = 0.55f + (float)random(0, 100) / 100.0f * 0.45f;
        }
        drawToasterAt(t, (int)tx[i], (int)ty[i], now, tcol[i], tscale[i]);
        if (tcol[i] == goldCol) {
            // Publish the body's centre so a tap can find it. Radius covers
            // the body, not the wings -- the wings sweep and a hit box that
            // tracked them would move under the finger.
            // Only while it is fully on screen: a toaster half off the
            // left edge still had a live hit box over the screen edge.
            if (tx[i] >= 0.0f && tx[i] + 44.0f * tscale[i] <= (float)w) {
                publishGoldToaster((int)tx[i] + (int)(22.0f * tscale[i]),
                                   (int)ty[i] + (int)(15.0f * tscale[i]),
                                   (int)(22.0f * tscale[i]),
                                   (int)(15.0f * tscale[i]), now);
            }
            for (uint8_t k = 1; k <= 3; k++) {
                int spx = (int)(tx[i] - k * 3.5f), spy = (int)(ty[i] + k * 0.9f + 6);
                t.drawPixel(spx, spy, blend(BG, goldCol, (uint16_t)(180 - k * 50)));
            }
        }
    }
    for (uint8_t i = 0; i < NT; i++) {
        ox[i] += (0.7f + (float)(i % 3) * 0.2f) * oscale[i];
        oy[i] -= (0.18f + (float)(i % 2) * 0.12f) * oscale[i];
        if (ox[i] > w + 40 || oy[i] < (float)yStart - 30) {
            ox[i]     = (float)(-random(0, 60) - 16);
            oy[i]     = (float)random(yStart + 14, yEnd - 14);
            oface[i]  = random(0, 3) == 0;
            oburnt[i] = random(0, 7) == 0;
            oscale[i] = 0.50f + (float)random(0, 100) / 100.0f * 0.40f;
        }
        drawToastAt(t, (int)ox[i], (int)oy[i], now, oface[i], oscale[i], oburnt[i]);
    }

    // ---- Boris -----------------------------------------------------------
    if (!boLive && now >= boNext) {
        boLive = true;
        boX    = (float)(-70);
        boY    = (float)random(yStart + 10, yEnd - 46);
        boS    = 0.65f + (float)random(0, 40) / 100.0f;
    }
    if (boLive) {
        boX += 1.1f;
        // He rises and falls gently as he drifts, and swipes on a cadence.
        const float bob = sinf((float)now / 620.0f) * 5.0f;
        const bool swipe = ((now / 900u) % 3u) == 0u;
        // The slice he is after, always just out of reach ahead of him.
        const int tx2 = (int)(boX + 52.0f * boS + (swipe ? 5.0f : 0.0f));
        const int ty2 = (int)(boY + bob - 4.0f + sinf((float)now / 300.0f) * 3.0f);
        drawToastAt(t, tx2, ty2, now, false, boS * 0.55f, false);
        drawBorisAt(t, (int)boX, (int)(boY + bob), now, boS, swipe);
        if (boX > (float)(w + 80)) {
            boLive = false;
            boNext = now + (uint32_t)random(40000, 90000);
        }
    }

    // ---- Mowin' Man ------------------------------------------------------
    // The grass only exists while he does. It grows in ahead of his arrival
    // and is gone once he leaves, so the band is plain black the rest of
    // the time -- a permanent lawn under a flock of toasters is a different
    // screensaver.
    if (!mmLive && now >= mmNext) {
        mmLive = true;
        mmX    = -50.0f;
        // Starts bare. Seeding the whole strip at once put a full-width
        // green bar across the screen the instant he spawned, which read
        // as a UI element rather than as a lawn.
        for (uint8_t i = 0; i < GRASSW; i++) grass[i] = 0;
    }
    if (mmLive) {
        mmX += 0.85f;
        const int gBase = yEnd - 1;
        const int colW  = (w + GRASSW - 1) / GRASSW;
        const uint16_t g1 = t.color565(60, 150, 70);
        const uint16_t g2 = t.color565(40, 110, 50);
        for (uint8_t i = 0; i < GRASSW; i++) {
            const int gx = (int)i * colW;
            if (gx > w) break;
            // A patch that travels with him: grows in ahead, gets cut as the
            // deck passes, holds as stubble just behind, and dies off once
            // he is well past. Nothing exists outside that window, so the
            // band is plain black except right where he is working.
            const float ahead = (float)gx - mmX;
            if (ahead > 26.0f && ahead < 150.0f) {
                if (grass[i] < 7 && ((now / 40u + i * 3u) % 5u) == 0u) grass[i]++;
            } else if (ahead <= 26.0f && ahead > -6.0f) {
                grass[i] = 1;                                  // under the deck
            } else if (ahead <= -6.0f && ahead > -110.0f) {
                if (grass[i] < 3 && ((now / 90u + i) % 29u) == 0u) grass[i]++;
            } else if (grass[i] > 0 && ((now / 60u + i) % 7u) == 0u) {
                grass[i]--;
            }
            const int gh = (int)grass[i];
            if (gh <= 0) continue;
            t.drawFastVLine(gx, gBase - gh, gh, (i & 1) ? g1 : g2);
        }
        drawMowinManAt(t, (int)mmX, gBase, now, 0.85f);
        if (mmX > (float)(w + 60)) {
            mmLive = false;
            mmNext = now + (uint32_t)random(55000, 120000);
        }
    }
}

// Ordered dither, so a gradient survives the frame buffer.
//
// The sprite is 8bpp RGB332: 3 bits of red, 3 of green, 2 of blue. Blue
// therefore has FOUR levels for the whole screen, and any smooth blue
// gradient quantises into flat slabs on the panel -- which is invisible
// in a 16-bit preview and extremely visible on the device.
//
// A 4x4 Bayer offset applied before quantisation makes neighbouring
// cells land on opposite sides of the boundary, and the eye integrates
// them back into the gradient. The per-channel step sizes below are the
// quantisation intervals RGB332 actually has (32/32/64), which is why
// blue gets twice the nudge: it is twice as coarse.
static uint16_t ditherRGB(TFT_eSPI& t, float r, float g, float b, uint8_t cell) {
    static const int8_t BAYER[16] = { -8,  0, -6,  2,
                                       4, -4,  6, -2,
                                      -5,  3, -7,  1,
                                       7, -1,  5, -3 };
    const int d = BAYER[cell & 15];
    int rr = (int)r + (d * 32) / 16;
    int gg = (int)g + (d * 32) / 16;
    int bb = (int)b + (d * 64) / 16;
    if (rr < 0) rr = 0; else if (rr > 255) rr = 255;
    if (gg < 0) gg = 0; else if (gg > 255) gg = 255;
    if (bb < 0) bb = 0; else if (bb > 255) bb = 255;
    return t.color565((uint8_t)rr, (uint8_t)gg, (uint8_t)bb);
}

// Water colour at a given row. The gradient was being open-coded in
// four places with the same magic numbers; a fish that hazes toward a
// slightly different blue than the water it is swimming in stops
// disappearing into the distance, which is the entire point of the
// haze, so they have to agree exactly.
static uint16_t aquaWaterAt(TFT_eSPI& t, int y, int yStart, int bandH) {
    float d = (float)(y - yStart) / (float)bandH;
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    const float lit = 1.0f - d;
    return t.color565((uint8_t)(4  + lit *  7),
                      (uint8_t)(30 + lit * 30),
                      (uint8_t)(44 + lit * 32));
}

// Body half-height at u (0 = snout, 1 = tail base), per species. Each
// silhouette has to stay recognisable at eight pixels tall, so these
// are deliberately exaggerated: the angelfish is taller than is
// reasonable, the puffer rounder, the minnow thinner.
static float fishProfile(uint8_t species, float u) {
    switch (species) {
        case 1:   // angelfish -- tall, laterally compressed
            return (u < 0.32f) ? (0.30f + (u / 0.32f) * 0.70f)
                               : (1.0f - powf((u - 0.32f) / 0.68f, 1.5f) * 0.88f);
        case 2:   // puffer -- near-spherical
            return sinf(u * 3.14159f) * 1.00f + 0.06f;
        default:  // minnow -- slim torpedo
            return (u < 0.28f) ? (0.28f + (u / 0.28f) * 0.72f)
                               : (1.0f - powf((u - 0.28f) / 0.72f, 1.35f) * 0.84f);
    }
}

// The same slice-and-flex construction the shark uses, scaled down.
// Head steady, tail sweeping, counter-shaded, with a forked caudal fin.
//
// `detailed` is what ties this to the depth haze: fins and an eye on a
// near fish, bare silhouette on a far one. That is not a shortcut --
// at four pixels tall the extra strokes turn to mush and read as
// noise, and dropping them makes distant fish look distant rather than
// just small.
static void drawFishAt(TFT_eSPI& t, int cx, int cy, int8_t swim, int s,
                       uint8_t species, uint16_t col, uint16_t waterC,
                       uint32_t now, float phase, bool detailed) {
    if (s < 2) s = 2;
    const int8_t fore = swim;
    const int8_t aft  = (int8_t)-swim;
    const float  L    = (species == 2) ? (float)s * 1.9f
                      : (species == 1) ? (float)s * 2.1f
                                       : (float)s * 2.9f;
    const int    NSL  = (int)L + 1;
    const float  ph   = (float)now / 150.0f + phase;

    // Counter-shading derived from the fish's own colour, so each keeps
    // its identity while gaining a lit top and a pale belly.
    const uint16_t back  = col;
    const uint16_t belly = blend(col, t.color565(255, 255, 255), 110);
    const int snoutX = cx + fore * (int)(L * 0.5f);

    auto waveAt = [&](float u) { return sinf(ph - u * 4.2f) * (0.25f + u * u * (float)s * 0.40f); };
    auto xAt    = [&](float u) { return (float)snoutX + (float)aft * u * L; };
    auto yAt    = [&](float u) { return (float)cy + waveAt(u); };

    for (int i = 0; i < NSL; i++) {
        const float u  = (float)i / (float)(NSL - 1);
        const float hh = fishProfile(species, u) * (float)s;
        if (hh < 0.5f) continue;
        const int x = (int)xAt(u), yc = (int)yAt(u), h = (int)hh;
        t.drawFastVLine(x, yc - h, h, back);
        t.drawFastVLine(x, yc, h + 1, belly);
    }

    // Forked caudal fin -- two lobes meeting at the peduncle, which is
    // what makes the notch without having to erase anything.
    {
        const int px = (int)xAt(1.0f), py = (int)yAt(1.0f);
        const int tx = px + aft * (int)(s * 1.05f + 1);
        const int lobe = (int)(s * 0.78f) + 1;
        t.fillTriangle(px, py, tx, py - lobe, px + aft * (int)(s * 0.35f), py - 1, back);
        t.fillTriangle(px, py, tx, py + lobe, px + aft * (int)(s * 0.35f), py + 1, back);
    }

    if (!detailed) return;

    // Dorsal fin. The angelfish gets the tall trailing one it is known
    // for; everything else gets a modest triangle.
    {
        const float u  = (species == 1) ? 0.34f : 0.40f;
        const int   px = (int)xAt(u);
        const int   py = (int)yAt(u) - (int)(fishProfile(species, u) * s);
        const int   hgt = (species == 1) ? (int)(s * 1.05f) : (int)(s * 0.55f);
        t.fillTriangle(px + fore * (int)(s * 0.30f), py,
                       px + aft  * (int)(s * 0.20f), py - hgt,
                       px + aft  * (int)(s * 0.45f), py, back);
    }
    // Pectoral fin, swept aft and low.
    {
        const float u  = 0.34f;
        const int   px = (int)xAt(u);
        const int   py = (int)yAt(u) + (int)(fishProfile(species, u) * s * 0.45f);
        t.fillTriangle(px, py,
                       px + aft * (int)(s * 0.55f), py + (int)(s * 0.50f),
                       px + aft * (int)(s * 0.15f), py + 1, belly);
    }
    // Puffer keeps its spines.
    if (species == 2) {
        for (uint8_t k = 0; k < 6; k++) {
            const float a = k * 1.047f + (float)now / 400.0f;
            t.drawPixel(cx + (int)(cosf(a) * (s + 2)), cy + (int)(sinf(a) * (s + 2)), back);
        }
    }
    // Eye. One pale pixel with a dark pupil is enough at this size, and
    // it is the single detail that makes a shape read as alive.
    {
        const float u  = 0.17f;
        const int   px = (int)xAt(u);
        const int   py = (int)yAt(u) - (int)(fishProfile(species, u) * s * 0.34f);
        t.drawPixel(px, py, t.color565(240, 240, 245));
        t.drawPixel(px + fore, py, BLACK);
    }
}

// Half-height of a shark's body at position u along it, 0 at the snout
// and 1 at the base of the tail. Rises fast from a pointed nose, peaks
// just behind the pectorals, then tapers to the narrow peduncle the
// tail hangs off. Three triangles never had this shape; a real shark is
// mostly a taper, and the taper is what the eye recognises.
static float sharkProfile(float u) {
    if (u < 0.30f) return 0.16f + (u / 0.30f) * 0.84f;
    const float k = (u - 0.30f) / 0.70f;
    return 1.0f - k * k * 0.88f;
}

// A shark drawn as a column of vertical slices rather than a fixed
// outline, which is what lets the body actually flex. Each slice takes
// its centreline from a wave travelling nose-to-tail with amplitude
// growing aft (u*u), so the head barely moves and the tail sweeps --
// which is how a real shark swims, and reads as swimming rather than
// sliding.
//
// Counter-shading does most of the remaining work: dark grey-blue over
// pale belly, split along the flexing centreline. It is the single most
// recognisable thing about a shark seen from the side, and here it
// costs one extra fill per slice.
//
// `swim` is the direction of travel. Every offset below is written in
// terms of `fore` (toward the nose) and `aft` (toward the tail) rather
// than raw signs, because the first version of this got that backwards
// -- the body was laid out *downstream* of the snout, so the animal
// swam tail-first. Naming the two directions makes that class of
// mistake visible at the call site instead of only on screen.
static void drawShark(TFT_eSPI& t, int snoutX, int cy, int8_t swim, int ss, uint32_t now) {
    const int8_t fore = swim;         // toward the nose
    const int8_t aft  = (int8_t)-swim; // toward the tail
    const float  L    = (float)ss * 2.1f;   // snout to tail base
    // One slice per pixel column, not a fixed count: at ss=20 a fixed 26
    // slices sit 1.7px apart and the body renders as a comb of separate
    // vertical lines with gaps between them. Deriving the count from the
    // length keeps it solid at any size.
    const int   NSL   = (int)L + 1;
    const float phase = (float)now / 190.0f;

    const uint16_t back  = t.color565(58, 72, 88);         // dorsal
    const uint16_t belly = t.color565(196, 202, 206);      // ventral
    const uint16_t edge  = t.color565(34, 42, 54);

    // Centreline and half-height for any u, shared by the body slices
    // and every fin so the fins stay attached while the body flexes.
    auto waveAt = [&](float u) {
        return sinf(phase - u * 4.6f) * (0.4f + u * u * 6.2f);
    };
    auto yAt = [&](float u) { return (float)cy + waveAt(u); };
    // u = 0 at the snout, 1 at the tail base -- so the body extends AFT
    // of the snout, which is the fix for the backwards swimming.
    auto xAt = [&](float u) { return (float)snoutX + (float)aft * u * L; };

    // ---- body ------------------------------------------------------
    for (int i = 0; i < NSL; i++) {
        const float u  = (float)i / (float)(NSL - 1);
        const float hh = sharkProfile(u) * (float)ss * 0.40f;
        if (hh < 0.5f) continue;
        const int x  = (int)xAt(u);
        const int yc = (int)yAt(u);
        const int h  = (int)hh;
        // Dorsal half dark, ventral half pale, meeting at the flexing
        // centreline rather than a straight one.
        t.drawFastVLine(x, yc - h, h, back);
        t.drawFastVLine(x, yc, h + 1, belly);
        t.drawPixel(x, yc - h, edge);
        t.drawPixel(x, yc + h, edge);
    }

    // ---- caudal fin -------------------------------------------------
    // Heterocercal: upper lobe clearly longer than the lower. Getting
    // this asymmetry right is most of what makes a silhouette read as
    // "shark" instead of "fish".
    {
        const int px = (int)xAt(1.0f), py = (int)yAt(1.0f);
        const int tx = px + aft * (int)(ss * 0.62f);
        t.fillTriangle(px, py, tx, py - (int)(ss * 0.95f), px + aft * (int)(ss * 0.18f), py - 2, back);
        t.fillTriangle(px, py, tx, py + (int)(ss * 0.42f), px + aft * (int)(ss * 0.16f), py + 2, back);
    }

    // ---- dorsal fin -------------------------------------------------
    // Raked aft, the way a shark's is.
    {
        const float u = 0.40f;
        const int px = (int)xAt(u), py = (int)yAt(u) - (int)(sharkProfile(u) * ss * 0.40f);
        t.fillTriangle(px + fore * (int)(ss * 0.16f), py,
                       px + aft  * (int)(ss * 0.10f), py - (int)(ss * 0.62f),
                       px + aft  * (int)(ss * 0.26f), py, back);
    }
    // Second dorsal, small, well aft.
    {
        const float u = 0.80f;
        const int px = (int)xAt(u), py = (int)yAt(u) - (int)(sharkProfile(u) * ss * 0.40f);
        t.fillTriangle(px + fore * 2, py, px + aft * 1, py - (int)(ss * 0.20f), px + aft * 5, py, back);
    }

    // ---- pectoral fin ----------------------------------------------
    // Swept aft and downward, the way a shark holds them level.
    {
        const float u = 0.30f;
        const int px = (int)xAt(u), py = (int)yAt(u) + (int)(sharkProfile(u) * ss * 0.30f);
        t.fillTriangle(px, py,
                       px + aft  * (int)(ss * 0.38f), py + (int)(ss * 0.44f),
                       px + fore * (int)(ss * 0.10f), py + 2, back);
    }

    // ---- head detail ------------------------------------------------
    // Five gill slits, then the eye. Small things, but they are where
    // the eye looks to decide whether a shape is an animal.
    for (uint8_t g = 0; g < 5; g++) {
        const float u  = 0.20f + g * 0.028f;
        const int   px = (int)xAt(u);
        const int   py = (int)yAt(u);
        const int   hh = (int)(sharkProfile(u) * ss * 0.40f);
        t.drawFastVLine(px, py - hh / 2, hh / 2 + 2, edge);
    }
    {
        const float u = 0.13f;
        const int px = (int)xAt(u), py = (int)yAt(u) - (int)(sharkProfile(u) * ss * 0.17f);
        t.fillCircle(px, py, 1, t.color565(240, 240, 245));
        t.drawPixel(px + fore, py, RED);      // the glint, kept
    }
    // Mouth: a short underslung line running aft from the snout.
    {
        const int x0 = (int)xAt(0.05f), y0 = (int)yAt(0.05f) + 1;
        const int x1 = (int)xAt(0.20f), y1 = (int)yAt(0.20f) + (int)(sharkProfile(0.20f) * ss * 0.22f);
        t.drawLine(x0, y0, x1, y1, edge);
    }
}

void drawAquarium(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    // species: 0 = minnow, 1 = angelfish, 2 = puffer, 3 = jellyfish
    // (its own drift-and-pulse motion instead of side-to-side swimming).
    // depth: 0 = right at the glass, 1 = far back. Drives size,
    // speed, haze and whether the fish gets drawn with fins at all.
    struct Fish { float x, y, speed, phase, depth; int8_t dir; uint8_t size, species; uint16_t col; };
    static const uint8_t N = 8;
    static Fish  fish[N];
    static bool  fInited = false;
    static const uint8_t NB = 10;
    static float bubX[NB], bubY[NB];
    static bool  bInited = false;
    // The rare big shark — mostly parked off-screen, only swims through
    // once in a while. sharkDir is fixed once at spawn (see the spawn
    // block below) -- it used to be recomputed every frame from
    // "which half of the screen is it currently on", which meant it
    // reversed the instant it crossed the midpoint and got stuck
    // oscillating around center forever instead of ever reaching an
    // edge and despawning. Committing to one direction for the whole
    // pass is what actually lets it cross the tank and leave.
    static float    sharkX, sharkY;
    static int8_t   sharkDir = 1;
    static bool     sharkActive = false;
    static uint32_t sharkNextAt = 0;
    static bool     sharkInited = false;

    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 20) return;

    if (!fInited) {
        // A single cool family rather than five saturated hues from
        // opposite sides of the wheel. CYAN/PINK/YELLOW/GREEN/PURPLE
        // each read as a separate accent competing for attention, which
        // is exactly wrong for something whose job is to sit behind the
        // UI. These stay distinguishable from each other while belonging
        // to the same water -- and muted colours survive RGB332
        // quantisation far better than saturated ones, which clamp to
        // the nearest primary and go garish.
        //
        // Deliberately fixed values rather than Theme constants: the
        // tank's water, sand and light are all fixed too, so pulling the
        // fish from the palette would make them the one element that
        // jumps hue when the theme changes.
        uint16_t cols[5] = { t.color565(126, 196, 194),   // pale aqua
                             t.color565( 92, 148, 178),   // steel blue
                             t.color565(142, 198, 168),   // seafoam
                             t.color565( 78, 132, 146),   // dim teal
                             t.color565(176, 168, 132) }; // muted sand, one warm note
        for (uint8_t i = 0; i < N; i++) {
            fish[i].x       = (float)random(0, w);
            fish[i].y       = (float)(yStart + random(10, bandH > 20 ? bandH - 10 : bandH));
            fish[i].speed   = 0.3f + (float)random(0, 100) / 100.0f * 0.7f;
            fish[i].phase   = (float)random(0, 6283) / 1000.0f;
            fish[i].dir     = random(0, 2) ? 1 : -1;
            fish[i].species = (uint8_t)((i == 0) ? 3 : random(0, 3));  // guarantee at least one jellyfish
            fish[i].size    = (uint8_t)(4 + random(0, 4));
            fish[i].col     = cols[i % 5];
            fish[i].depth   = (float)random(0, 100) / 100.0f;
        }
        fInited = true;
    }
    if (!bInited) {
        for (uint8_t i = 0; i < NB; i++) {
            bubX[i] = (float)random(0, w);
            bubY[i] = (float)(yStart + random(0, bandH));
        }
        bInited = true;
    }
    if (!sharkInited) { sharkNextAt = now + (uint32_t)random(6000, 16000); sharkInited = true; }

    // ---- water column ----------------------------------------------
    // Two constraints shape everything here, and they pull against each
    // other.
    //
    // First, the frame buffer is 8bpp RGB332: 3 bits of red, 3 of green,
    // 2 of blue. Its levels are evenly spaced over 0..255, so *dark*
    // colours get almost no resolution. The previous, darker water
    // spanned literally two green levels and two blue levels across the
    // whole tank -- which is why it landed on the panel as flat slabs no
    // amount of shading could fix. Lifting the ramp is not a style
    // choice, it is the only way to buy quantisation levels to shade
    // with.
    //
    // Second, UI text sits on top of this, so it cannot go far.
    //
    // The compromise: a brighter surface (spanning four green levels
    // instead of two) plus dithering *vertically only*. A 16-phase
    // ordered offset per row makes consecutive rows straddle the
    // quantisation boundary, and the eye integrates them into a smooth
    // ramp. Dithering horizontally as well was tried and looked worse --
    // at 4px cells it reads as brick-textured noise rather than
    // gradient, because with two levels to work with there is nothing
    // subtle for the pattern to interpolate between.
    static const uint8_t NRAY = 3;
    static float   rayX[NRAY], rayW[NRAY], raySpd[NRAY];
    static bool    rayInited = false;
    if (!rayInited) {
        for (uint8_t i = 0; i < NRAY; i++) {
            rayX[i]   = (float)random(0, w);
            rayW[i]   = 18.0f + (float)random(0, 22);
            raySpd[i] = 0.00012f + (float)random(0, 22) / 100000.0f;
        }
        rayInited = true;
    }
    for (int y = yStart; y < yEnd; y++) {
        const float d   = (float)(y - yStart) / (float)bandH;   // 0 surface, 1 floor
        const float lit = 1.0f - d;
        // A narrow ramp, deliberately. Widening it to buy quantisation
        // levels backfired: a broad range crosses several RGB332
        // boundaries, and each crossing is a visible plateau -- at one
        // point the mid-depth water landed on a green level with blue
        // quantised to zero and turned into a slab of dark green.
        //
        // Spanning roughly a single step instead means there is only one
        // boundary in the whole tank, and the dither hides that one. The
        // result is nearly flat, which is the correct answer for
        // something whose job is to sit behind the UI: this is a
        // background, not a showpiece gradient.
        const float baseR =  4.0f + lit *  7.0f;
        const float baseG = 30.0f + lit * 30.0f;
        const float baseB = 44.0f + lit * 32.0f;
        // One phase per row: the gradient is vertical, so this is where
        // the dither has to act.
        const uint8_t cell = (uint8_t)(y & 15);
        t.drawFastHLine(0, y, w, ditherRGB(t, baseR, baseG, baseB, cell));

        // Shafts, as a few nested spans per ray rather than one flat
        // band -- a single span gave each shaft a hard edge that the
        // quantisation then made into a visible rectangle.
        if (d < 0.60f) {
            const float dd    = (float)(y - yStart);
            const float fade  = (1.0f - d / 0.60f) * 0.20f;
            for (uint8_t i = 0; i < NRAY; i++) {
                const float rc = rayX[i] + sinf((float)now * raySpd[i] + (float)i * 1.7f) * 16.0f + dd * 0.30f;
                const float rh = rayW[i] + dd * 0.16f;
                for (uint8_t k = 0; k < 4; k++) {
                    const float frac = 1.0f - (float)k * 0.25f;     // outer -> inner
                    const float kk   = fade * (1.0f - frac) * (1.0f - frac) * 3.0f;
                    if (kk < 0.02f) continue;
                    const int hw = (int)(rh * frac);
                    int xs = (int)rc - hw, xe = (int)rc + hw;
                    if (xs < 0) xs = 0;
                    if (xe > w) xe = w;
                    if (xe <= xs) continue;
                    t.drawFastHLine(xs, y, xe - xs,
                                    ditherRGB(t, baseR + (180.0f - baseR) * kk,
                                                 baseG + (240.0f - baseG) * kk,
                                                 baseB + (255.0f - baseB) * kk, cell));
                }
            }
        }

        // Vignette, as a smooth-ish falloff in five steps rather than
        // the three hard bands this used to draw.
        for (uint8_t k = 0; k < 5; k++) {
            const int   ww = (w * (6 - k)) / 40;
            if (ww < 1) continue;
            const float v  = 0.05f + (float)k * 0.065f;
            const uint16_t vc = ditherRGB(t, baseR * (1.0f - v),
                                             baseG * (1.0f - v),
                                             baseB * (1.0f - v), cell);
            t.drawFastHLine(0, y, ww, vc);
            t.drawFastHLine(w - ww, y, ww, vc);
        }
    }

    // Shared by the waterline and the marine snow below -- both are
    // lit by the same surface light the shafts come from.
    // Tinted toward the water rather than near-white. A white shaft on
    // dark teal is the highest-contrast thing on the screen, which made
    // the lighting read as an effect laid over the scene instead of
    // light inside it.
    const uint16_t rayCol = t.color565(112, 190, 202);

    // Waterline. The surface itself rises and falls across the tank
    // rather than being ruled flat: two travelling waves at different
    // rates, so the crest never repeats on a fixed pitch. A dead-level
    // top edge is what was reading as "a rectangle of blue" instead of
    // the underside of water.
    for (int x = 0; x < w; x += 2) {
        const float surf = sinf((float)x * 0.055f + (float)now / 900.0f) * 1.6f
                         + sinf((float)x * 0.019f - (float)now / 1500.0f) * 1.1f;
        const int wy = yStart + 2 + (int)surf;
        for (uint8_t k = 0; k < 2; k++) {
            const int yy = wy + k;
            if (yy < yStart || yy >= yEnd) continue;
            const float ph  = (float)x * 0.09f + (float)now / (260.0f + k * 90.0f);
            const uint8_t a = (uint8_t)(26 + 46 * (0.5f + 0.5f * sinf(ph)));
            t.drawFastHLine(x, yy, 2, blend(t.color565(10, 70, 110), rayCol, a));
        }
    }

    // ---- marine snow ------------------------------------------------
    // Suspended particulate drifting down. Almost invisible individually
    // and the single strongest "this is a volume of water, not empty
    // space" cue there is.
    static const uint8_t NSNOW = 34;
    static float snowX[NSNOW], snowY[NSNOW], snowPh[NSNOW];
    static bool  snowInited = false;
    if (!snowInited) {
        for (uint8_t i = 0; i < NSNOW; i++) {
            snowX[i]  = (float)random(0, w);
            snowY[i]  = (float)(yStart + random(0, bandH));
            snowPh[i] = (float)random(0, 6283) / 1000.0f;
        }
        snowInited = true;
    }
    for (uint8_t i = 0; i < NSNOW; i++) {
        snowY[i] += 0.10f + (float)(i % 3) * 0.05f;
        if (snowY[i] > (float)yEnd) {
            snowY[i] = (float)yStart;
            snowX[i] = (float)random(0, w);
        }
        const int px = (int)(snowX[i] + sinf((float)now / 1400.0f + snowPh[i]) * 5.0f);
        const int py = (int)snowY[i];
        if (px < 0 || px >= w) continue;
        const float d = (float)(py - yStart) / (float)bandH;
        const uint16_t waterC = aquaWaterAt(t, py, yStart, bandH);
        t.drawPixel(px, py, blend(waterC, rayCol, (uint8_t)(90 + (i % 5) * 26)));
    }


    // ---- seabed with caustics ---------------------------------------
    // The dancing light net on the floor is the most recognisable
    // underwater cue there is, and it is the cheapest thing here: the
    // pattern is a 64x64 tile baked into flash (see caustic_tile.h), so
    // per pixel this is one array read, not five sinf calls.
    //
    // Projection is what makes it read as a floor rather than wallpaper.
    // Sampling at u = x*z and v = z, with z growing toward the back of
    // the tank, compresses the pattern with distance exactly the way
    // perspective does -- cells are broad and open at the front and
    // squeeze toward the back wall. The two scroll offsets drift at
    // different rates so the net crawls and shifts instead of sliding
    // rigidly.
    const int floorTop = yEnd - bandH / 4;
    if (floorTop > yStart + 4) {
        const float su = (float)now / 320.0f;
        const float sv = (float)now / 260.0f;
        // Caustics brighten and dim together as the surface above moves.
        const float breathe = 0.72f + 0.28f * sinf((float)now / 1700.0f);
        const uint16_t causticCol = t.color565(138, 202, 206);
        for (int y = floorTop; y < yEnd; y++) {
            const float near = (float)(y - floorTop) / (float)(yEnd - floorTop); // 0 back, 1 front
            const float z    = 1.0f / (0.20f + near * 0.80f);
            // Sand, seen through progressively more water toward the
            // back -- so the floor fades into the haze rather than
            // meeting the back wall at a hard line.
            const float d = (float)(y - yStart) / (float)bandH;
            const uint16_t waterC = aquaWaterAt(t, y, yStart, bandH);
            // Grey-green rather than warm tan. Sand against teal water
            // was the single biggest hue clash in the tank -- two
            // opposed temperatures meeting at a hard line across the
            // bottom of the screen.
            const uint16_t sand = blend(waterC, t.color565(104, 118, 110),
                                        (uint8_t)(22 + near * 88));
            t.drawFastHLine(0, y, w, sand);

            const int v = (int)(z * 14.0f + sv) & (CAUSTIC_N - 1);
            for (int x = 0; x < w; x += 2) {
                const int u = (int)((float)(x - w / 2) * z * 0.42f + su) & (CAUSTIC_N - 1);
                const uint8_t c = CAUSTIC_TILE[v * CAUSTIC_N + u];
                if (c < 30) continue;
                // Light reaching the floor falls off toward the back.
                const uint8_t a = (uint8_t)(c * breathe * (0.45f + near * 0.55f) * 0.26f);
                if (a < 8) continue;
                t.drawFastHLine(x, y, 2, blend(sand, causticCol, a));
            }
        }
        // Where the floor meets the water, a soft lip rather than a cut.
        t.drawFastHLine(0, floorTop, w, blend(t.color565(12, 40, 60),
                                              t.color565(76, 92, 88), 55));
    }

    // Kelp: jointed multi-segment strands, sway amplitude growing
    // toward the tip like real kelp anchored at the base, with little
    // leaf ticks along each segment.
    int weedBaseY = yEnd - 1;
    static const uint8_t NW = 6;
    for (uint8_t i = 0; i < NW; i++) {
        int baseX = 5 + (int)(i * (w - 10) / (float)(NW - 1));
        int segs = 5 + (i % 3);
        int segH = (bandH / 3) / segs; if (segH < 2) segH = 2;
        // Muted into the same family as everything else. Full-strength
        // GREEN was reading as a separate foreground object rather than
        // planting in the same water.
        uint16_t weedCol = (i % 2 == 0) ? t.color565(72, 128, 104)
                                        : t.color565(88, 142, 128);
        float ampGrow = 0.0f;
        int px = baseX, py = weedBaseY;
        for (int s = 0; s < segs; s++) {
            ampGrow += 0.9f;
            float sway = sinf((float)now / 850.0f + i * 1.7f + s * 0.5f) * ampGrow;
            int nx = baseX + (int)sway;
            int ny = py - segH;
            t.drawLine(px, py, nx, ny, weedCol);
            if (s % 2 == 0) t.drawLine(px, py - segH / 2, px + ((nx > px) ? 3 : -3), py - segH / 2 - 1, weedCol);
            px = nx; py = ny;
        }
    }

    for (uint8_t i = 0; i < NB; i++) {
        bubY[i] -= 0.6f;
        if (bubY[i] < yStart) { bubY[i] = (float)yEnd; bubX[i] = (float)random(0, w); }
        t.drawCircle((int)bubX[i], (int)bubY[i], 1, VAPOR_BLUE);
    }

    // Fish panic and speed up while the shark is out — a little
    // reactive touch that ties the tank together.
    float fleeMul = sharkActive ? 2.2f : 1.0f;

    for (uint8_t i = 0; i < N; i++) {
        Fish& f = fish[i];

        if (f.species == 3) {
            // Jellyfish: slow vertical bob + pulsing bell + trailing
            // tentacles, independent of the side-to-side swimmers.
            f.y += sinf((float)now / 1400.0f + f.phase) * 0.15f;
            f.x += sinf((float)now / 2600.0f + f.phase) * 0.06f;
            if (f.x < 0) f.x = 0;
            if (f.x > w) f.x = (float)w;
            if (f.y < yStart + 8) f.y = (float)(yStart + 8);
            if (f.y > yEnd - 8)   f.y = (float)(yEnd - 8);
            int jx = (int)f.x, jy = (int)f.y;
            int s = (int)(f.size * (1.0f - f.depth * 0.42f));
            if (s < 2) s = 2;
            const uint16_t jw = aquaWaterAt(t, jy, yStart, bandH);
            const uint16_t jc = blend(jw, f.col, (uint8_t)(200 - f.depth * 130));
            float pulse = 0.7f + 0.3f * sinf((float)now / 500.0f + f.phase);
            t.fillEllipse(jx, jy, s, (int)(s * 0.6f * pulse), jc);
            for (uint8_t k = 0; k < 4; k++) {
                int tx = jx - s + k * (2 * s) / 3;
                int ty = jy + (int)(s * 0.6f);
                int wob = (int)(sinf((float)now / 260.0f + k + f.phase) * 3.0f);
                t.drawLine(tx, ty, tx + wob, ty + s, blend(jw, f.col, (uint8_t)(130 - f.depth * 90)));
            }
            continue;
        }

        // Distant fish swim slower as well as smaller and hazier -- an
        // object further away subtends less angular motion, and matching
        // all three is what stops a far fish reading as a small near one.
        const float near = 1.0f - f.depth;
        f.x += f.dir * f.speed * fleeMul * (0.55f + near * 0.45f);
        if (f.dir > 0 && f.x > w + 12) f.x = -12;
        if (f.dir < 0 && f.x < -12)    f.x = (float)(w + 12);
        // Occasionally turn around mid-tank instead of only at the
        // edges — keeps the motion from feeling like a fixed loop.
        if (!sharkActive && random(0, 900) == 0) f.dir = (int8_t)-f.dir;
        float bob = sinf((float)now / 700.0f + f.phase) * 2.0f;

        const int fx = (int)f.x;
        const int fy = (int)(f.y + bob);
        int s = (int)(f.size * (0.55f + near * 0.45f));
        if (s < 2) s = 2;

        // Depth haze: water eats contrast and colour with distance --
        // red first -- so a far fish should be a dim blue-grey shape
        // rather than a crisp coloured one. Blending toward the water at
        // its own row is what makes the tank read as a volume with
        // things at different distances in it, instead of sprites on a
        // gradient. It is also the cue that lets the detail drop below
        // pass unnoticed.
        const uint16_t waterC = aquaWaterAt(t, fy, yStart, bandH);
        // Never fully un-hazed: at 255 a near fish is drawn in pure
        // body colour and pops off the background like a sticker. 225
        // leaves it clearly the sharpest thing in the tank while still
        // sharing the water's cast.
        const uint16_t hazed  = blend(waterC, f.col, (uint8_t)(225.0f - f.depth * 160.0f));
        drawFishAt(t, fx, fy, f.dir, s, f.species, hazed, waterC,
                   now, f.phase, f.depth < 0.55f);
    }


    // ---- the shoal --------------------------------------------------
    // Actual flocking, not a scripted formation: each small fish steers
    // by the three classic boids rules against whichever neighbours are
    // within range -- push apart when too close, match the local
    // heading, drift toward the local centre. Nothing tells the school
    // where to go; the shape it makes is emergent, which is why it
    // bends around itself and re-forms after being broken.
    //
    // It is also nearly free. Twenty-six fish is 650 neighbour tests a
    // frame, a few floating-point operations each, against a frame that
    // spends 22ms of its 29 waiting on SPI.
    //
    // The shark is what makes it worth having: the flee term below
    // scales as 1/d^2, so a pass through the middle blows the school
    // apart and the cohesion rule pulls it back together afterwards
    // without anyone scripting the recovery.
    static const uint8_t NS = 26;
    static float shX[NS], shY[NS], shVX[NS], shVY[NS];
    static bool  shInited = false;
    if (!shInited) {
        for (uint8_t i = 0; i < NS; i++) {
            shX[i]  = (float)random(0, w);
            shY[i]  = (float)(yStart + random(6, bandH > 12 ? bandH - 12 : bandH));
            shVX[i] = ((float)random(0, 200) - 100.0f) / 120.0f;
            shVY[i] = ((float)random(0, 200) - 100.0f) / 320.0f;
        }
        shInited = true;
    }
    {
        const float R2   = 28.0f * 28.0f;   // neighbourhood
        const float SEP2 =  7.0f *  7.0f;   // personal space
        const int   shFloor = floorTop - 5;
        for (uint8_t i = 0; i < NS; i++) {
            float ccx = 0, ccy = 0, avx = 0, avy = 0, spx = 0, spy = 0;
            uint8_t n = 0;
            for (uint8_t j = 0; j < NS; j++) {
                if (j == i) continue;
                const float dx = shX[j] - shX[i], dy = shY[j] - shY[i];
                const float d2 = dx * dx + dy * dy;
                if (d2 > R2) continue;
                n++;
                ccx += shX[j];  ccy += shY[j];
                avx += shVX[j]; avy += shVY[j];
                if (d2 < SEP2 && d2 > 0.5f) { spx -= dx / d2; spy -= dy / d2; }
            }
            if (n) {
                ccx /= n; ccy /= n; avx /= n; avy /= n;
                shVX[i] += (ccx - shX[i]) * 0.0015f + (avx - shVX[i]) * 0.055f + spx * 1.6f;
                shVY[i] += (ccy - shY[i]) * 0.0015f + (avy - shVY[i]) * 0.055f + spy * 1.6f;
            }
            // Stay in the tank.
            if (shX[i] < 10.0f)            shVX[i] += 0.06f;
            if (shX[i] > (float)(w - 10))  shVX[i] -= 0.06f;
            if (shY[i] < (float)(yStart + 8)) shVY[i] += 0.06f;
            if (shY[i] > (float)shFloor)      shVY[i] -= 0.06f;

            if (sharkActive) {
                const float dx = shX[i] - sharkX, dy = shY[i] - sharkY;
                const float d2 = dx * dx + dy * dy;
                if (d2 < 4200.0f && d2 > 1.0f) {
                    shVX[i] += dx / d2 * 34.0f;
                    shVY[i] += dy / d2 * 34.0f;
                }
            }

            float sp = sqrtf(shVX[i] * shVX[i] + shVY[i] * shVY[i]);
            const float cap = sharkActive ? 2.8f : 1.25f;
            if (sp > cap)                 { shVX[i] *= cap / sp;  shVY[i] *= cap / sp; }
            else if (sp < 0.30f && sp > 0.001f) { shVX[i] *= 0.30f / sp; shVY[i] *= 0.30f / sp; }
            shX[i] += shVX[i];
            shY[i] += shVY[i];
        }
        // Drawn small and tinted toward the water they are sitting in,
        // so the school reads as a cloud at middle distance rather than
        // 26 individually legible fish in the foreground.
        //
        // Shape matters more than detail at this size. These used to be
        // a solid triangle with a dot behind it, which reads as an
        // arrowhead -- a dart, not an animal. Two cues fix that, and
        // neither is "more pixels":
        //
        //   - a FORKED tail. The notch is the single strongest "fish"
        //     signal there is, and it costs nothing: two small triangles
        //     sharing the peduncle leave the notch between them rather
        //     than needing anything erased.
        //   - a TAPERED body. An ellipse has a round front and narrows
        //     to a waist, where a triangle is widest at the back and
        //     points the wrong way entirely.
        //
        // The tail also wags, out of phase per fish, so a school in
        // formation still looks like many animals rather than one
        // rigid flock of copies.
        for (uint8_t i = 0; i < NS; i++) {
            const int fx = (int)shX[i], fy = (int)shY[i];
            if (fx < -6 || fx > w + 6 || fy < yStart || fy >= yEnd) continue;
            const float dd = (float)(fy - yStart) / (float)bandH;
            const uint16_t waterC = aquaWaterAt(t, fy, yStart, bandH);
            const uint16_t c    = blend(waterC, t.color565(205, 232, 214), 205);
            const uint16_t cDim = blend(waterC, t.color565(205, 232, 214), 140);
            const int8_t fore = (shVX[i] >= 0.0f) ? 1 : -1;
            const int8_t aft  = (int8_t)-fore;

            // Body: widest just behind the head, tapering aft.
            t.fillEllipse(fx, fy, 3, 2, c);
            t.drawPixel(fx + fore * 3, fy, c);              // snout

            // Forked tail, wagging on its own phase.
            const int wag = (int)(sinf((float)now / 130.0f + (float)i * 0.9f) * 1.6f);
            const int px = fx + aft * 2;                    // peduncle
            const int tx = fx + aft * 5;
            t.fillTriangle(px, fy, tx, fy - 2 + wag, px + aft, fy - 1, c);
            t.fillTriangle(px, fy, tx, fy + 2 + wag, px + aft, fy + 1, c);

            // One dark pixel for an eye. At six pixels long it is the
            // difference between a shape and a creature.
            t.drawPixel(fx + fore * 2, fy - 1, cDim);
        }
    }

    // The shark: dormant most of the time, then commits to one straight
    // crossing of the tank before disappearing again — a little payoff
    // for watching the tank, now that it actually reaches the far edge
    // instead of getting stuck oscillating around center (see sharkDir's
    // comment above).
    if (!sharkActive && now >= sharkNextAt) {
        sharkActive = true;
        bool fromLeft = random(0, 2) == 0;
        sharkX   = fromLeft ? -40.0f : (float)(w + 40);
        sharkDir = fromLeft ? 1 : -1;
        sharkY   = (float)(yStart + random(8, bandH > 16 ? bandH - 8 : bandH));
    }
    if (sharkActive) {
        sharkX += sharkDir * 1.8f;
        int8_t dir = sharkDir;
        // Nearly double the old size (12 -> 20) -- unmistakably the
        // biggest thing in the tank instead of just another fish shape.
        int ss = 20;
        // A slow vertical prowl instead of a dead-flat line, so it
        // reads as hunting rather than sliding on a rail.
        float prowl = sinf((float)now / 900.0f) * 4.0f;
        int sx = (int)sharkX, sy = (int)(sharkY + prowl);
        // Cast a shadow on the seabed before drawing the animal, so it
        // sits under everything. Nothing else in the tank connects the
        // swimmers to the floor, and this one ellipse is what stops the
        // shark reading as a sticker on the glass -- it tracks him
        // horizontally, and softens and spreads the further above the
        // floor he is, the way a real shadow does.
        if (floorTop < yEnd - 2) {
            const float above = (float)(floorTop - sy) / (float)bandH;   // 0 = on the floor
            const float tight = 1.0f - (above > 0.0f ? (above < 1.0f ? above : 1.0f) : 0.0f);
            const int   shW   = (int)(ss * (1.5f + (1.0f - tight) * 1.1f));
            const int   shH   = 2 + (int)(ss * 0.16f * (1.0f - tight));
            const uint8_t a   = (uint8_t)(28 + tight * 62);
            t.fillEllipse(sx, floorTop + shH + 1, shW, shH,
                          blend(t.color565(70, 66, 52), t.color565(4, 10, 18), a));
        }
        // Snout LEADS the centre point: the body is laid out aft of
        // whatever x is passed here, so passing sx - dir*ss put the nose
        // behind the tail and the animal swam backwards.
        drawShark(t, sx + dir * ss, sy, dir, ss, now);

        // A close pass "gulps" any regular fish caught right at the
        // mouth -- not a real removal, just an instant respawn off the
        // far edge headed the other way, so it reads as startling off
        // after a near miss rather than anything grim. Jellyfish drift
        // independently of the swimmers and sit this out.
        int mouthX = sx + dir * ss, mouthY = sy;
        for (uint8_t i = 0; i < N; i++) {
            Fish& f = fish[i];
            if (f.species == 3) continue;
            float ddx = f.x - (float)mouthX, ddy = f.y - (float)mouthY;
            if (ddx * ddx + ddy * ddy < 100.0f) {
                f.x   = (dir > 0) ? (float)(w + 10) : -10.0f;
                f.y   = (float)(yStart + random(10, bandH > 20 ? bandH - 10 : bandH));
                f.dir = (int8_t)-dir;
            }
        }

        if (sx < -(ss * 3) || sx > w + ss * 3) {
            sharkActive = false;
            sharkNextAt = now + (uint32_t)random(10000, 25000);
        }
    }
}

// Two independently-scrolling columns (different add intervals so they
// never sync up) side by side, so the log fills the full screen width
// instead of a narrow strip down the left.
struct TermCol { char lines[20][40]; bool special[20]; uint8_t count; uint32_t lastAdd; };

static void termGenLine(char* out, size_t outSize) {
    static const char* VERBS[] = { "INIT", "PROBE", "SCAN", "MOUNT", "AUTH", "PARSE", "TRACE", "PING", "SYNC", "LOAD" };
    static const char* NOUNS[] = { "kernel", "rf-stack", "node", "socket", "buffer", "daemon", "cache", "uplink", "packet", "handshake" };
    static const char* TAILS[] = { "OK", "DONE", "0x%02X", "READY", "--", "FAIL", "..." };
    const char* v  = VERBS[random(0, 10)];
    const char* n  = NOUNS[random(0, 10)];
    const char* tl = TAILS[random(0, 7)];
    char tbuf[12];
    if (strcmp(tl, "0x%02X") == 0) {
        snprintf(tbuf, sizeof(tbuf), "0x%02X", (unsigned)random(0, 256));
        tl = tbuf;
    }
    snprintf(out, outSize, "%-5s %-9s %s", v, n, tl);
}

// Rare easter-egg lines — a small chance each new line is one of these
// instead of the usual generated boot chatter, rendered in a
// different color so it actually stands out if you're watching.
static const char* TERM_EGG_LINES[] = {
    "SQUACHY WAS HERE",
    "// hi mom",
    "sudo make me a squachwich",
    "ACCESS: GRANTED (nice)",
    "root@squachwatch: <3",
    "you found the secret line",
};
static const uint8_t TERM_EGG_COUNT = sizeof(TERM_EGG_LINES) / sizeof(TERM_EGG_LINES[0]);

static void termAdvance(TermCol& c, uint32_t now, uint32_t interval) {
    if (now - c.lastAdd <= interval) return;
    c.lastAdd = now;
    if (c.count < 20) c.count++;
    for (uint8_t i = 19; i > 0; i--) {
        memcpy(c.lines[i], c.lines[i - 1], sizeof(c.lines[i]));
        c.special[i] = c.special[i - 1];
    }
    if (random(0, 45) == 0) {
        strncpy(c.lines[0], TERM_EGG_LINES[random(0, TERM_EGG_COUNT)], sizeof(c.lines[0]) - 1);
        c.lines[0][sizeof(c.lines[0]) - 1] = '\0';
        c.special[0] = true;
    } else {
        termGenLine(c.lines[0], sizeof(c.lines[0]));
        c.special[0] = false;
    }
}

static void termRender(TFT_eSPI& t, const TermCol& c, int x, int yStart, int visLines) {
    for (uint8_t i = 0; i < visLines && i < c.count; i++) {
        uint16_t col;
        if (c.special[i]) {
            col = VAPOR_PINK;
        } else {
            uint8_t fade = 255 - i * (255 / (visLines > 0 ? visLines : 1));
            col = blend(BG, GREEN, fade);
        }
        t.setTextColor(col, BG);
        t.setCursor(x, yStart + i * 9);
        t.print(c.lines[i]);
    }
}

void drawTerminalLog(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    static TermCol cols[2];
    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 20) return;
    int visLines = bandH / 9;
    if (visLines > 20) visLines = 20;

    termAdvance(cols[0], now, 180);
    termAdvance(cols[1], now, 230);   // offset cadence so the two columns don't lock-step

    t.fillRect(0, yStart, w, bandH, BG);
    t.setTextSize(1);
    t.setTextWrap(false);
    termRender(t, cols[0], 4, yStart, visLines);
    termRender(t, cols[1], w / 2 + 4, yStart, visLines);
    t.drawFastVLine(w / 2, yStart, bandH, blend(BG, GREEN, 60));

    // Rare dramatic beat: a full-width banner line flashes across both
    // columns for under a second, breaking the indifferent scroll with
    // an "something just happened" moment instead of an endless,
    // personality-free log.
    static bool     flashOn = false;
    static uint32_t flashStart = 0, flashNextAt = 0;
    static uint8_t  flashLine = 0;
    static bool     flashInited = false;
    static const char* const FLASH_LINES[] = {
        "ACCESS GRANTED", "INTRUSION DETECTED", "CONNECTION ESTABLISHED",
        "ROOT SHELL OPEN", "TRACE COMPLETE",
    };
    if (!flashInited) { flashNextAt = now + (uint32_t)random(6000, 14000); flashInited = true; }
    if (!flashOn && now >= flashNextAt) {
        flashOn = true;
        flashStart = now;
        flashLine = (uint8_t)random(0, 5);
    }
    if (flashOn) {
        if (now - flashStart < 900) {
            uint16_t col = ((now / 120) % 2) ? GREEN : blend(BG, GREEN, 150);
            t.fillRect(0, yStart + bandH / 2 - 6, w, 12, BG);
            t.setTextColor(col, BG);
            int mw = t.textWidth(FLASH_LINES[flashLine]);
            t.setCursor((w - mw) / 2, yStart + bandH / 2 - 4);
            t.print(FLASH_LINES[flashLine]);
        } else {
            flashOn = false;
            flashNextAt = now + (uint32_t)random(9000, 20000);
        }
    }
}

void drawFireflies(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    static const uint8_t N = 40;
    static float   fx[N], fy[N], fvx[N], fvy[N], fphase[N], fpx[N], fpy[N];
    static uint8_t fcolor[N];
    static bool    inited = false;
    // Every so often, real fireflies of some species sync up and flash
    // together in a traveling ripple — a rare wave sweeping left to
    // right across the whole field.
    static bool     waveActive = false;
    static uint32_t waveStart = 0, waveNextAt = 0;
    static bool     waveInited = false;

    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 20) return;

    if (!inited) {
        for (uint8_t i = 0; i < N; i++) {
            fx[i]     = (float)random(0, w);
            fy[i]     = (float)(yStart + random(0, bandH));
            fpx[i]    = fx[i]; fpy[i] = fy[i];
            fvx[i]    = (float)random(-50, 50) / 300.0f;
            fvy[i]    = (float)random(-50, 50) / 300.0f;
            fphase[i] = (float)random(0, 6283) / 1000.0f;
            fcolor[i] = (uint8_t)random(0, 4);
        }
        inited = true;
    }
    if (!waveInited) { waveNextAt = now + (uint32_t)random(7000, 14000); waveInited = true; }
    if (!waveActive && now >= waveNextAt) { waveActive = true; waveStart = now; }
    float waveT = -1.0f;
    if (waveActive) {
        waveT = (float)(now - waveStart) / 2200.0f;
        if (waveT > 1.15f) {
            waveActive = false;
            waveNextAt = now + (uint32_t)random(9000, 18000);
        }
    }

    uint16_t palette[4] = { VAPOR_YELLOW, VAPOR_PINK, CYAN, GREEN };

    t.fillRect(0, yStart, w, bandH, BG);
    for (uint8_t i = 0; i < N; i++) {
        fpx[i] = fx[i]; fpy[i] = fy[i];
        fx[i] += fvx[i];
        fy[i] += fvy[i];
        if (fx[i] < 0 || fx[i] > w)          fvx[i] = -fvx[i];
        if (fy[i] < yStart || fy[i] > yEnd)  fvy[i] = -fvy[i];
        float pulse = 0.5f + 0.5f * sinf((float)now / 900.0f + fphase[i]);
        bool waveHit = false;
        if (waveActive) {
            float xn = fx[i] / (float)w;
            if (fabsf(xn - waveT) < 0.07f) { pulse = 1.0f; waveHit = true; }
        }
        uint16_t base = palette[fcolor[i]];
        uint16_t col = blend(BG, base, (uint16_t)(60 + pulse * 195));
        int px = (int)fx[i], py = (int)fy[i];

        // Short fading motion trail from where it was last frame.
        uint16_t trailCol = blend(BG, base, 70);
        t.drawLine((int)fpx[i], (int)fpy[i], px, py, trailCol);

        t.drawPixel(px, py, col);
        if (pulse > 0.7f) {
            t.drawPixel(px - 1, py, col); t.drawPixel(px + 1, py, col);
            t.drawPixel(px, py - 1, col); t.drawPixel(px, py + 1, col);
        }
        // Rare courtship flash (or a guaranteed flash if the sync wave
        // is passing through) — a brief bright sparkle burst.
        if (waveHit || (pulse > 0.92f && random(0, 40) == 0)) {
            t.drawLine(px - 3, py, px + 3, py, WHITE);
            t.drawLine(px, py - 3, px, py + 3, WHITE);
            t.drawCircle(px, py, 2, col);
        }
    }
}

// ---- tappable background bits -----------------------------------------
// drawFire publishes its moon here every frame it draws one, and
// backgroundTap() below tests against that. The timestamp matters: a
// moon position left over from a background that is no longer on screen
// must not stay tappable, and drawFire is simply not called once the
// user cycles away.
static int      s_moonX = -1, s_moonY = -1, s_moonR = 0;
static uint32_t s_moonAt = 0;

// Ten taps, each within MOON_TAP_WINDOW of the one before, summon the
// werewolf. The window is what makes it a deliberate act rather than an
// accumulation: a tap now and a tap five minutes from now should not
// count toward the same thing.
static const uint32_t MOON_TAP_WINDOW = 2500;
static const uint8_t  MOON_TAPS_NEEDED = 10;
static uint8_t  s_moonTaps  = 0;
static uint32_t s_moonTapAt = 0;
static uint32_t s_wolfAt    = 0;      // 0 = no werewolf on stage
static bool     s_wolfSummonPending = false;   // consumed by main.cpp

// Stage timings, all eased into each other. Nothing here pops: that is
// the whole lesson of the tree that used to strobe in this same scene.
static const uint32_t WOLF_EYES = 1300;   // eyes fade up in the dark
static const uint32_t WOLF_BODY = 1100;   // silhouette resolves around them
static const uint32_t WOLF_HOLD = 2600;   // it just stands there
static const uint32_t WOLF_HOWL = 1400;   // head goes back
static const uint32_t WOLF_GONE = 1500;   // fades back into the dark
static const uint32_t WOLF_TOTAL = WOLF_EYES + WOLF_BODY + WOLF_HOLD +
                                   WOLF_HOWL + WOLF_GONE;

bool consumeWerewolfSummon() {
    if (!s_wolfSummonPending) return false;
    s_wolfSummonPending = false;
    return true;
}

void dimRegion(TFT_eSPI& t, int x, int y, int w, int h, uint8_t amount) {
    if (amount == 0) return;
    if (amount >= 250) { t.fillRect(x, y, w, h, BG); return; }
    // amount -> row spacing: 128 blanks every other row, 85 every third,
    // 64 every fourth, and so on. Below ~50 the effect stops being worth
    // the pass at all.
    const int step = 255 / (int)amount + 1;
    if (step < 2) { t.fillRect(x, y, w, h, BG); return; }
    for (int yy = y + (step - 1); yy < y + h; yy += step)
        t.drawFastHLine(x, yy, w, BG);
}

// Single place that maps the Settings background choice onto a
// renderer. Lifted out of uiClearTick(), which owned it while CLEAR was
// the only screen with a live backdrop.
void drawActiveBackground(TFT_eSPI& t, uint32_t now, int yStart, int yEnd,
                          const DetectionEngine& eng, bool advance) {
    switch (Settings::background()) {
        case Settings::Background::STARFIELD:  drawStarfield(t, now, yStart, yEnd); break;
        case Settings::Background::TOASTERS:   drawFlyingToasters(t, now, yStart, yEnd); break;
        case Settings::Background::AQUARIUM:   drawAquarium(t, now, yStart, yEnd); break;
        case Settings::Background::TERMINAL:   drawTerminalLog(t, now, yStart, yEnd); break;
        case Settings::Background::FIREFLIES:  drawFireflies(t, now, yStart, yEnd); break;
        case Settings::Background::FIRE:       drawFire(t, now, yStart, yEnd); break;
        case Settings::Background::SNOWFALL:   drawSnowfall(t, now, yStart, yEnd); break;
        case Settings::Background::SPECTRUM:   drawSpectrumWaterfall(t, now, yStart, yEnd, eng); break;
        case Settings::Background::TUNNEL:     drawWireframeTunnel(t, now, yStart, yEnd); break;
        case Settings::Background::SYNTHWAVE:  drawSynthwave(t, now, yStart, yEnd); break;
        default:                               drawDigitalRain(t, now, yStart, yEnd, advance); break;
    }
}

// Where the gold toaster was last drawn, and how big. Published by
// drawFlyingToasters() every frame it is on screen so backgroundTap() has
// something to hit-test against -- the same shape as the moon's
// s_moonX/s_moonY, and stale for the same reason: if it has not been
// refreshed in the last few frames the toaster is gone.
static int      s_goldX = -1, s_goldY = -1, s_goldHW = 0, s_goldHH = 0;
static uint32_t s_goldAt = 0;
static bool     s_goldCaught = false;

// The body box, not a generous circle around it. The first version used a
// circle of r = 24*scale + 10 -- about 29px, 4.3% of the band -- and since
// a gold toaster is on screen roughly a third of the time (they take ~35s
// to cross and there are five of them), a random background tap caught one
// about 1.4% of the time. Over forty exploratory taps that is a 44% chance
// of "unlocking" a costume you never knew you were reaching for.
static void publishGoldToaster(int cx, int cy, int hw, int hh, uint32_t now) {
    s_goldX = cx; s_goldY = cy; s_goldHW = hw; s_goldHH = hh; s_goldAt = now;
}

bool consumeToasterCatch() {
    if (!s_goldCaught) return false;
    s_goldCaught = false;
    return true;
}

bool backgroundTap(int x, int y, uint32_t now) {
    // The rare gold toaster is catchable. One tap, unlike the moon's three:
    // it is only on screen for a few seconds at a time and moving, which is
    // difficulty enough without also demanding a triple-tap on a target
    // that will not still be there.
    if (s_goldX >= 0 && (now - s_goldAt) <= 250) {
        const int gdx = x - s_goldX, gdy = y - s_goldY;
        if (gdx <= s_goldHW && gdx >= -s_goldHW &&
            gdy <= s_goldHH && gdy >= -s_goldHH) {
            s_goldCaught = true;
            s_goldX = -1;               // caught: stop accepting taps on it
            return true;
        }
    }

    // Not drawn recently means not on screen.
    if (s_moonX < 0 || (now - s_moonAt) > 250) return false;
    const int dx = x - s_moonX, dy = y - s_moonY;
    const int r  = s_moonR + 7;            // generous: it is a small target
    if (dx * dx + dy * dy > r * r) return false;
    if (s_wolfAt) return true;             // already out; eat the tap, do nothing
    if ((now - s_moonTapAt) > MOON_TAP_WINDOW) s_moonTaps = 0;
    s_moonTapAt = now;
    if (++s_moonTaps >= MOON_TAPS_NEEDED) {
        s_moonTaps = 0;
        s_wolfAt   = now ? now : 1;        // never 0, that means "none"
        s_wolfSummonPending = true;
    }
    return true;
}

void drawFire(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    static const int CW = 4;
    // Grid is sized to the widest panel the build can actually run on.
    // fw below is w/CW, so the cap only ever binds at the panel's own
    // width: 480px on cyd35 needs 120 cells, but every shipping board
    // is a 240x320 panel whose longest side is 320 -- 80 cells. Sizing
    // all builds for cyd35 left columns 80..119 of the heat grid (3200
    // bytes) allocated and never once written or read, since fw simply
    // never reaches them there. MAXFH stays 80 for both: the tallest
    // band any board renders is 320px, which is the same 80 cells.
#if defined(CYD35)
    static const int MAXFW = 120, MAXFH = 80;
#else
    static const int MAXFW = 80, MAXFH = 80;
#endif
    static uint8_t   heat[MAXFW * MAXFH];
    static float     acc[MAXFW];
    static bool      inited = false;
    // Independent flame sources, each with its own flicker rate and
    // phase — a shared single traveling wave here is exactly what made
    // the old version look like one repeating pattern sliding sideways
    // instead of separate flames. Each source also jitters its own
    // base position a little so the flames don't sit glued in place.
    static const uint8_t NSRC = 7;
    static float srcX[NSRC], srcPhase[NSRC], srcFreq[NSRC], srcJitterPh[NSRC];
    static bool  srcInited = false;

    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 20) return;
    int fw = w / CW;     if (fw > MAXFW) fw = MAXFW;
    int fh = bandH / CW; if (fh > MAXFH) fh = MAXFH;
    if (fw < 2 || fh < 2) return;

    if (!inited) { memset(heat, 0, sizeof(heat)); inited = true; }
    if (!srcInited) {
        for (uint8_t i = 0; i < NSRC; i++) {
            srcX[i]       = (i + 0.5f) * fw / (float)NSRC + (float)random(-2, 3);
            srcPhase[i]   = (float)random(0, 6283) / 1000.0f;
            srcFreq[i]    = 180.0f + (float)random(0, 220);
            srcJitterPh[i]= (float)random(0, 6283) / 1000.0f;
        }
        srcInited = true;
    }

    // Colour ramp as a table instead of a branch chain plus color565
    // arithmetic per cell. Heat only spans 0..HEAT_MAX, so the whole
    // ramp is 49 RGB triples -- 147 bytes -- and the draw loop below
    // becomes a single indexed load with no arithmetic at all. Stored
    // packed rather than as components because the dither below works
    // in heat space, not colour space, so nothing downstream needs the
    // channels. Rebuilt when the palette changes, since the smoke tier
    // fades to BG and BG is a runtime theme variable, not a constant.
    // Same 4x4 ordered pattern ditherRGB uses; >>2 in the draw loop
    // scales it to roughly -2..+1 heat units, which is a fraction of a
    // colour step on the ramp.
    static const int8_t FIRE_BAYER[16] = { -8,  0, -6,  2,
                                            4, -4,  6, -2,
                                           -5,  3, -7,  1,
                                            7, -1,  5, -3 };
    static const uint8_t HEAT_MAX = 48;
    static uint16_t fireLUT[HEAT_MAX + 1];
    static uint16_t lutBG = 0xFFFF;
    if (lutBG != BG) {
        const float bgR = (float)((BG >> 8) & 0xF8);
        const float bgG = (float)((BG >> 3) & 0xFC);
        const float bgB = (float)((BG << 3) & 0xF8);
        for (int v = 0; v <= HEAT_MAX; v++) {
            float r, g, b;
            if (v < 9) {
                // Faint drifting smoke fringe instead of a hard cutoff
                // straight to background — this is what actually reads
                // as smoke rather than the flame just vanishing.
                const float k = (float)(v * 28) / 255.0f;
                r = bgR + (60.0f - bgR) * k;
                g = bgG + (60.0f - bgG) * k;
                b = bgB + (75.0f - bgB) * k;
            } else if (v < 22) {
                r = 60.0f + (v - 9) * 15.0f; g = 0.0f; b = 0.0f;
            } else if (v < 36) {
                r = 255.0f; g = (v - 22) * 18.0f; b = 0.0f;
            } else {
                r = 255.0f; g = 200.0f + (v - 36) * 4.0f; b = (v - 36) * 18.0f;
            }
            fireLUT[v] = t.color565((uint8_t)(r < 0.0f ? 0.0f : (r > 255.0f ? 255.0f : r)),
                                    (uint8_t)(g < 0.0f ? 0.0f : (g > 255.0f ? 255.0f : g)),
                                    (uint8_t)(b < 0.0f ? 0.0f : (b > 255.0f ? 255.0f : b)));
        }
        lutBG = BG;
    }

    // Wind. The propagation step below used to drift each cell by a
    // symmetric random(-1,2), so the fire had no net lean at any
    // moment -- which is most of why it read as an effect rather than
    // a fire. Three sines at different periods give a slow prevailing
    // direction with gusts riding on it, and no repeat on a period
    // anyone is going to notice.
    float windF = sinf((float)now / 4300.0f) * 0.55f
                + sinf((float)now / 1600.0f + 1.7f) * 0.30f
                + sinf((float)now /  610.0f + 3.1f) * 0.15f;
    if (windF >  1.0f) windF =  1.0f;
    if (windF < -1.0f) windF = -1.0f;
    // 72 here originally, which was a smear rather than a lean: at that
    // rate most cells in a row stepped the same way every row, and heat
    // travelled sideways faster than it rose, breaking the flames into
    // horizontal streaks. A fifth of cells is enough to read as wind.
    const int windChance = (int)(fabsf(windF) * 22.0f);
    const int windDir    = (windF > 0.0f) ? 1 : -1;

    // Seed the bottom row from the sum of all independent sources
    // (Gaussian-ish falloff around each), instead of one shared wave —
    // this is what makes each flame flicker on its own schedule.
    memset(acc, 0, sizeof(float) * fw);
    float sigma = (fw / (float)NSRC) * 0.6f;
    if (sigma < 1.0f) sigma = 1.0f;
    for (uint8_t i = 0; i < NSRC; i++) {
        float jitter = sinf((float)now / 900.0f + srcJitterPh[i] * 3.1f) * 1.4f;
        float sx = srcX[i] + jitter;
        float flick = 0.5f + 0.5f * sinf((float)now / srcFreq[i] + srcPhase[i]);
        for (int x = 0; x < fw; x++) {
            float dx = x - sx;
            float g = 1.0f - fabsf(dx) / sigma;
            if (g > 0.0f) acc[x] += g * flick;
        }
    }
    float litSum = 0.0f;
    for (int x = 0; x < fw; x++) {
        float v = acc[x];
        if (v > 1.25f) v = 1.25f;
        uint8_t base = (uint8_t)(48.0f * (v / 1.25f));
        heat[(fh - 1) * MAXFW + x] = (random(0, 6) == 0) ? 0 : base;
        litSum += (float)base;
    }

    // How hard the fire is burning this frame, 0..1, smoothed so the
    // scene lighting below flickers rather than strobes. Taken from
    // the seed row because that is the fuel, and it leads the visible
    // flame by the few frames heat takes to propagate up.
    static float litSmooth = 0.0f;
    float lit = litSum / ((float)fw * 34.0f);
    if (lit > 1.0f) lit = 1.0f;
    litSmooth += (lit - litSmooth) * 0.25f;

    // Propagate upward with random decay and a little horizontal drift
    // — the classic Doom-fire trick. Decay range is the actual height
    // control: lower average decay means more rows of upward travel
    // before a column's heat hits zero, so flames reach further up the
    // band. Leave the seed intensity (48, just above) alone -- the
    // colour ramp is calibrated against that exact max and raising it
    // would run off the end of the table.
    //
    // Decay is heat-dependent, and deliberately only at the bottom of
    // the range: anything with life left in it keeps the original
    // random(0,3) and only the already-dying fringe below 10 cools
    // faster. Two earlier attempts got this wrong in both directions --
    // slowing the hot core raised flame height everywhere and the fire
    // climbed the whole band, then speeding up every cool cell thinned
    // the flames out because most cells are cool most of the time.
    // Touching only the fringe tightens the haze between tongues and
    // leaves the flame body exactly as it was.
    for (int y = 0; y < fh - 1; y++) {
        for (int x = 0; x < fw; x++) {
            int drift = random(-1, 2);
            if (windChance > 0 && random(0, 100) < windChance) drift += windDir;
            // Reflect at the edges rather than clamp. Clamping means a
            // cell at the downwind edge samples nx == itself every row,
            // so that column stops mixing sideways and becomes a solid
            // vertical bar that flashes on and off as the wind reverses.
            // Symmetric drift hid this; a prevailing wind exposes it.
            // Reflection keeps sampling inward, so the edge mixes like
            // everywhere else.
            int nx = x + drift;
            if (nx < 0)   nx = -nx;
            if (nx >= fw) nx = 2 * fw - 2 - nx;
            if (nx < 0)   nx = 0;
            if (nx >= fw) nx = fw - 1;
            int src = heat[(y + 1) * MAXFW + nx];
            int decay = (src < 10) ? random(0, 4) : random(0, 3);
            int val = src - decay;
            if (val < 0) val = 0;
            heat[y * MAXFW + x] = (uint8_t)val;
        }
    }

    // One clear for the whole band, so everything below can be drawn in
    // depth order -- sky, then the tree, then the flames over both --
    // and the fire loop can skip cold cells instead of painting them.
    t.fillRect(0, yStart, w, bandH, BG);

    // Night sky. Drawn before the flames, so a star is hidden by fire
    // or smoke simply because the fire paints over it later -- no heat
    // test needed. Same twinkle mechanic as drawSunsetSky.
    static const uint8_t NSTARS = 12;
    static uint8_t skyX[NSTARS], skyY[NSTARS], skyPh[NSTARS];
    static bool    skyInited = false;
    if (!skyInited) {
        for (uint8_t i = 0; i < NSTARS; i++) {
            skyX[i]  = (uint8_t)random(2, w > 2 ? w - 2 : w);
            skyY[i]  = (uint8_t)(yStart + random(0, bandH * 2 / 3));
            skyPh[i] = (uint8_t)random(0, 256);
        }
        skyInited = true;
    }
    for (uint8_t i = 0; i < NSTARS; i++) {
        uint32_t tw = (now / 10 + (uint32_t)skyPh[i] * 22) % 300;
        if (tw > 220) continue;
        uint8_t bri = (tw < 100) ? 200 : (uint8_t)(200 - (tw - 100) * 2);
        int gx = skyX[i] / CW, gy = (skyY[i] - yStart) / CW;
        // Heat haze: a star with fire anywhere in the column below it
        // wobbles by a pixel or so. Hot air genuinely does this to
        // anything seen through it, and it costs one column scan.
        int drawX = skyX[i];
        if (gx >= 0 && gx < fw && gy >= 0 && gy < fh) {
            for (int hy = gy + 1; hy < fh; hy++) {
                if (heat[hy * MAXFW + gx] > 24) {
                    drawX += (int)(sinf((float)now / 90.0f + (float)i) * 1.6f);
                    break;
                }
            }
        }
        if (drawX < 0) drawX = 0;
        if (drawX >= w) drawX = w - 1;
        t.drawPixel(drawX, skyY[i], t.color565((uint8_t)(bri * 0.85f), (uint8_t)(bri * 0.9f), bri));
    }

    // A pale, slightly sickly moon in a top corner — a crescent via one
    // full circle then a BG-coloured circle biting a chunk out of it,
    // the same trick used elsewhere in this file for shapes without a
    // smooth-arc primitive. Drawn before the flames, so a tall tongue
    // reaching it occludes it naturally.
    {
        // Pulled in from w-22. The right tenth of the screen is the
        // CLEAR gesture that cycles the background, and at w-22 the
        // whole moon sat inside it -- so tapping the moon changed the
        // scene out from under you, which is fatal for an egg that
        // needs ten taps in a row.
        const int mx = w - 46, my = yStart + 16, mr = 9;
        s_moonX = mx; s_moonY = my; s_moonR = mr; s_moonAt = now;

        // The crescent is a full disc with a background disc bitten out
        // of it. Sliding that bite off the edge is all "full moon"
        // takes, which makes the tap counter free to display: the moon
        // waxes as taps land, and is full while the werewolf is out.
        int bite = 5;
        if (s_wolfAt) {
            bite = 40;                                  // full
        } else if (s_moonTaps) {
            bite = 5 + (int)(s_moonTaps * 35 / MOON_TAPS_NEEDED);
        }
        t.fillCircle(mx, my, mr, t.color565(210, 235, 200));
        if (bite < 2 * mr + 4) t.fillCircle(mx + bite, my - 3, mr - 1, BG);
    }

    // Where the owl ended up, so the quip bubble further down can find
    // it. -1 means the band was too short to draw a tree at all.
    int owlX = -1, owlY = -1;
    // Same idea for the werewolf: its bubble is drawn after the flames,
    // so the draw pass needs to hand its position forward. -1 means it
    // is not on stage, or is on stage but not in its speaking beat.
    int wolfSayX = -1, wolfSayY = -1;

    // Spooky tree, standing BEHIND the fire. Being behind is the whole
    // point: it is drawn before the flames, so they cover it a pixel at
    // a time. The previous tree was drawn last and faked that with a
    // heat test that skipped the entire tree on any frame where the fire
    // reached its base, which at 33fps is a hard on/off strobe -- a
    // flashing bar rather than a tree.
    //
    // Shape-wise it is a stack of horizontal spans whose width shrinks
    // and whose centre follows a shallow S-curve, rather than one
    // rectangle. The taper and the lean are most of what separates a
    // tree from a post.
    {
        const int   tx        = w / 6;
        const int   groundY   = yEnd - 2;
        const int   trunkTopY = yStart + bandH / 6;
        const float span      = (float)(groundY - trunkTopY);
        if (span > 24.0f) {
            // Flat, unlit bark. Tying this to litSmooth meant the whole
            // tree pulsed with the fire, which read as the tree itself
            // flickering rather than as light falling on it -- and it is
            // what made the trunk obvious enough to look like a bar in
            // the first place. A silhouette behind the fire wants to sit
            // still and stay dark.
            const uint16_t bark = t.color565(68, 47, 30);

            // Trunk. leanAt is reused by the limbs and the owl so they
            // all attach to the same curve.
            for (int y = groundY; y >= trunkTopY; y--) {
                const float f = (float)(groundY - y) / span;
                int halfW = (int)(5.0f - 3.6f * f);
                if (halfW < 1) halfW = 1;
                const int cxT = tx + (int)(sinf(f * 2.4f) * 6.0f - f * 3.0f);
                t.drawFastHLine(cxT - halfW, y, halfW * 2 + 1, bark);
            }
            // Root flare, so it grows out of the ground instead of being
            // planted in it.
            t.drawFastHLine(tx - 9, groundY,     19, bark);
            t.drawFastHLine(tx - 7, groundY - 1, 15, bark);

            // Bare limbs: three segments each, thinning as they go, with
            // one fork. Alternating sides up the trunk.
            static const struct { float f; int8_t dir; float len; } LIMB[5] = {
                { 0.50f, -1, 1.00f }, { 0.63f,  1, 0.88f },
                { 0.75f, -1, 0.70f }, { 0.85f,  1, 0.60f },
                { 0.93f, -1, 0.44f }
            };
            for (uint8_t i = 0; i < 5; i++) {
                const float f = LIMB[i].f;
                const int   d = LIMB[i].dir;
                const int  by = groundY - (int)(span * f);
                const int  bx = tx + (int)(sinf(f * 2.4f) * 6.0f - f * 3.0f);
                const float L = span * 0.30f * LIMB[i].len;
                const int x1 = bx + (int)(L * 0.55f) * d, y1 = by - (int)(L * 0.34f);
                const int x2 = x1 + (int)(L * 0.42f) * d, y2 = y1 - (int)(L * 0.50f);
                const int x3 = x2 + (int)(L * 0.26f) * d, y3 = y2 - (int)(L * 0.20f);
                t.drawLine(bx, by,     x1, y1, bark);
                t.drawLine(bx, by + 1, x1, y1 + 1, bark);   // thicken at the trunk
                t.drawLine(x1, y1, x2, y2, bark);
                t.drawLine(x2, y2, x3, y3, bark);
                t.drawLine(x1, y1, x1 + (int)(L * 0.12f) * d, y1 - (int)(L * 0.55f), bark);
            }

            // Owl, on a LEFT-hand limb and high up the trunk, where the
            // flames drawn later rarely reach. Its eyes are the only
            // saturated thing in the upper band, which is what makes it
            // read at this size. The blink is deliberate and slow --
            // about 180ms every four seconds -- rather than the
            // frame-rate flicker the old tree had.
            {
                // Authored at the original 1x offsets and multiplied
                // through O, same trick as the werewolf: proportions stay
                // locked and resizing is one number. The perch offset
                // scales with him so he stays sat ON the limb rather than
                // hovering above it as he grows.
                auto O = [](int v) { return (v * 3) / 2; };

                const uint8_t LI = 2;                   // left-pointing limb
                const float f = LIMB[LI].f;
                const int  by = groundY - (int)(span * f);
                const int  bx = tx + (int)(sinf(f * 2.4f) * 6.0f - f * 3.0f);
                const float L = span * 0.30f * LIMB[LI].len;
                const int  ox = bx - (int)(L * 0.55f);  // out along it, leftward
                const int  oy = by - (int)(L * 0.34f) - O(9);
                owlX = ox; owlY = oy;

                const uint16_t owlBody = t.color565(158, 140, 116);
                const uint16_t owlDark = t.color565(12, 10, 8);

                t.fillRect(ox - O(4), oy,        O(9),  O(9), owlBody);   // body
                t.fillRect(ox - O(3), oy + O(9), O(7),  2,     owlBody);  // tail
                t.fillRect(ox - O(5), oy - O(4), O(11), O(5), owlBody);   // head
                // Ear tufts as filled wedges rather than 1px lines --
                // a hairline stayed a hairline when everything around it
                // grew, which is what made the werewolf claws look
                // spindly at 1.6x.
                t.fillRect(ox - O(5), oy - O(6), 2, O(3), owlBody);
                t.fillRect(ox - O(6), oy - O(7), 2, O(2), owlBody);
                t.fillRect(ox + O(4), oy - O(6), 2, O(3), owlBody);
                t.fillRect(ox + O(5), oy - O(7), 2, O(2), owlBody);

                if ((now % 4000) < 180) {
                    t.fillRect(ox - O(4), oy - O(1), O(3), 2, owlDark);
                    t.fillRect(ox + O(2), oy - O(1), O(3), 2, owlDark);
                } else {
                    const uint16_t eye = t.color565(255, 196, 44);
                    t.fillRect(ox - O(4), oy - O(2), O(3), O(3), eye);
                    t.fillRect(ox + O(2), oy - O(2), O(3), O(3), eye);
                    t.fillRect(ox - O(3), oy - O(1), 2, 2, owlDark);
                    t.fillRect(ox + O(3), oy - O(1), 2, 2, owlDark);
                }
                t.fillRect(ox, oy, 2, O(2), owlDark);        // beak
            }
        }
    }

    // ---- werewolf ------------------------------------------------------
    // Summoned by ten taps on the moon (see backgroundTap above). Drawn
    // here, between the tree and the flames, so fire crosses in front of
    // it exactly like the tree -- it is standing back at the treeline,
    // not in the fire.
    //
    // Every stage cross-fades. The eyes arrive first, alone in the dark,
    // and the body resolves around them a beat later; at this size two
    // saturated points read long before a silhouette does, which is the
    // same reason the owl works.
    if (s_wolfAt) {
        const uint32_t e = now - s_wolfAt;
        if (e >= WOLF_TOTAL) {
            s_wolfAt = 0;
        } else {
            float eyeF = 1.0f, bodyF = 1.0f, howl = 0.0f;
            if (e < WOLF_EYES) {
                eyeF  = (float)e / (float)WOLF_EYES;
                bodyF = 0.0f;
            } else if (e < WOLF_EYES + WOLF_BODY) {
                bodyF = (float)(e - WOLF_EYES) / (float)WOLF_BODY;
            } else if (e >= WOLF_EYES + WOLF_BODY + WOLF_HOLD) {
                const uint32_t h = e - (WOLF_EYES + WOLF_BODY + WOLF_HOLD);
                howl = (h < WOLF_HOWL) ? (float)h / (float)WOLF_HOWL : 1.0f;
            }
            // Common fade-out over the tail of the whole sequence.
            if (e > WOLF_TOTAL - WOLF_GONE) {
                const float k = 1.0f - (float)(e - (WOLF_TOTAL - WOLF_GONE)) / (float)WOLF_GONE;
                eyeF *= k; bodyF *= k;
            }

            // Front-facing, two-tone, snarling: modelled on a reference
            // sprite rather than invented. The side-on silhouette that
            // preceded this read as a wolf but not as a WEREwolf -- what
            // sells the difference is facing the viewer with a lit face,
            // red eyes and a mouthful of teeth, none of which a profile
            // can show.
            //
            // Palette is chosen around RGB332, not despite it. Red has 3
            // bits (0/36/73/109/146/182/219/255) and blue only 2
            // (0/85/170/255), so every colour below already sits on a
            // representable value and none of them drift on quantisation.
            // The body maroon is deliberately r=109 rather than 73: at 73
            // it collides with the tree bark and the two silhouettes
            // merge into one shape when they overlap.
            const uint16_t body  = blend(BG, t.color565(109,  36,   0), (uint16_t)(255.0f * bodyF));
            const uint16_t pelt  = blend(BG, t.color565( 73,  73,  85), (uint16_t)(255.0f * bodyF));
            const uint16_t lit   = blend(BG, t.color565(146, 146, 128), (uint16_t)(255.0f * bodyF));
            const uint16_t claw  = blend(BG, t.color565(255,   0,   0), (uint16_t)(255.0f * bodyF));
            const uint16_t maw   = blend(BG, t.color565(146,   0,   0), (uint16_t)(255.0f * bodyF));
            const uint16_t tooth = blend(BG, t.color565(255, 255, 255), (uint16_t)(255.0f * bodyF));
            const uint16_t eyeR  = blend(BG, t.color565(255,   0,   0), (uint16_t)(255.0f * eyeF));
            const uint16_t eyeC  = blend(BG, t.color565(255, 219,   0), (uint16_t)(255.0f * eyeF));

            // Standing back up the slope, not on the fire's own ground
            // line: the counter rows and the densest flames both live at
            // the bottom of this band and both beat a background to the
            // pixel. Placed down there it drew correctly and was never
            // once visible. x keeps it clear of the mascot, who is drawn
            // over the background afterwards.
            const int gy = yStart + (int)((yEnd - yStart) * 0.66f);
            const int wx = (int)(w * 0.80f);
            const int breathe = (int)(sinf((float)now / 520.0f) * 1.0f);
            const int lift    = (int)(howl * 4.0f);

            // Everything below is authored at the original 1x offsets
            // and multiplied through Z, so the proportions stay locked
            // and resizing the creature is one number rather than forty.
            // At 1.6x the silhouette runs about 67px wide and 76 tall,
            // which still clears the mascot on the left (he ends around
            // x=208) and the right edge of a 320px panel.
            auto Z = [](int v) { return (v * 8) / 5; };

            const int cx  = wx;
            const int bob = breathe;
            const int hy  = gy - Z(48) + bob - lift;   // top of the skull

            // Speaks from the moment the body has fully resolved right
            // through the howl, so the line is up while it rears back
            // and throws its head -- the animation is the delivery. Only
            // the fades are excluded, where the text would still be
            // perfectly legible while the speaker was not.
            if (e >= WOLF_EYES + WOLF_BODY &&
                e <  WOLF_EYES + WOLF_BODY + WOLF_HOLD + WOLF_HOWL) {
                wolfSayX = cx;
                wolfSayY = hy - Z(15);
            }

            if (bodyF > 0.02f) {
                // Legs, planted wide, and heavy dark feet.
                t.fillRect(cx - Z(10), gy - Z(19), Z(7),  Z(16), body);
                t.fillRect(cx + Z(4),  gy - Z(19), Z(7),  Z(16), body);
                t.fillRect(cx - Z(13), gy - Z(4),  Z(11), Z(4),  pelt);
                t.fillRect(cx + Z(3),  gy - Z(4),  Z(11), Z(4),  pelt);

                // Torso with a lighter chest panel -- the two-tone is
                // most of what stops this reading as one dark blob.
                t.fillRect(cx - Z(11), gy - Z(35) + bob, Z(23), Z(17), body);
                t.fillRect(cx - Z(5),  gy - Z(34) + bob, Z(11), Z(14), pelt);

                // Hunched shoulders, wider than the chest.
                t.fillRect(cx - Z(15), gy - Z(39) + bob, Z(31), Z(6), body);

                // Arms hanging long and slightly out, with black hands
                // and red claws at the tips.
                t.fillRect(cx - Z(20), gy - Z(38) + bob, Z(6), Z(21), body);
                t.fillRect(cx + Z(15), gy - Z(38) + bob, Z(6), Z(21), body);
                t.fillRect(cx - Z(21), gy - Z(18) + bob, Z(8), Z(5),  pelt);
                t.fillRect(cx + Z(14), gy - Z(18) + bob, Z(8), Z(5),  pelt);
                for (int k = 0; k < 3; k++) {
                    t.fillRect(cx - Z(20) + k * Z(3), gy - Z(13) + bob, Z(2), Z(4), claw);
                    t.fillRect(cx + Z(15) + k * Z(3), gy - Z(13) + bob, Z(2), Z(4), claw);
                }

                // Neck, sized from lift so the head stays attached when
                // it goes back for the howl.
                t.fillRect(cx - Z(5), hy + Z(11), Z(11), Z(8) + lift, body);

                // Skull, with a lighter muzzle mask over it.
                t.fillRect(cx - Z(10), hy,        Z(21), Z(13), pelt);
                t.fillRect(cx - Z(5),  hy + Z(4), Z(11), Z(10), lit);

                // Ears taper to a point over three steps and stand well
                // proud of the crest. An earlier version had ears and
                // crest tufts at the same height and even spacing, which
                // turned the whole skull into a crown.
                t.fillRect(cx - Z(11), hy - Z(4), Z(4), Z(4), pelt);
                t.fillRect(cx - Z(10), hy - Z(7), Z(3), Z(3), pelt);
                t.fillRect(cx - Z(9),  hy - Z(9), Z(2), Z(2), pelt);
                t.fillRect(cx + Z(8),  hy - Z(4), Z(4), Z(4), pelt);
                t.fillRect(cx + Z(8),  hy - Z(7), Z(3), Z(3), pelt);
                t.fillRect(cx + Z(8),  hy - Z(9), Z(2), Z(2), pelt);
                // Ragged crest: short, uneven, and well below the ears.
                t.fillRect(cx - Z(5), hy - Z(2), Z(2), Z(2), pelt);
                t.fillRect(cx - Z(1), hy - Z(3), Z(2), Z(3), pelt);
                t.fillRect(cx + Z(3), hy - Z(2), Z(2), Z(2), pelt);

                // Nose, then the open snarl. The howl drops the jaw
                // further and the teeth go with it.
                const int jaw = (int)(howl * 3.0f) * 8 / 5;
                t.fillRect(cx - Z(2), hy + Z(6),  Z(4),  Z(3), pelt);
                t.fillRect(cx - Z(5), hy + Z(10), Z(11), Z(4) + jaw, maw);
                t.fillRect(cx - Z(5), hy + Z(10), Z(11), Z(1) + 1, tooth);
                t.fillRect(cx - Z(5), hy + Z(13) + jaw, Z(11), Z(1) + 1, tooth);
            }

            // Eyes last so nothing paints over them: red, angled inward
            // along the top edge, with a hot centre. A plain rectangle
            // pair read as goggles. These arrive before the body does
            // and are the whole of the first beat.
            t.fillRect(cx - Z(8), hy + Z(5), Z(6), Z(3), eyeR);
            t.fillRect(cx + Z(3), hy + Z(5), Z(6), Z(3), eyeR);
            t.fillRect(cx - Z(5), hy + Z(4), Z(3), Z(1) + 1, eyeR);
            t.fillRect(cx + Z(3), hy + Z(4), Z(3), Z(1) + 1, eyeR);
            if (howl > 0.55f) {
                t.fillRect(cx - Z(7), hy + Z(6), Z(4), Z(1) + 1, eyeC);
                t.fillRect(cx + Z(4), hy + Z(6), Z(4), Z(1) + 1, eyeC);
            } else {
                t.fillRect(cx - Z(6), hy + Z(6), Z(2), Z(2), eyeC);
                t.fillRect(cx + Z(5), hy + Z(6), Z(2), Z(2), eyeC);
            }
        }
    }

    // Draw the flames over the sky and tree above, coalescing runs of
    // identical colour into one fillRect. Cold cells are SKIPPED
    // rather than painted with BG, which is what lets anything sit
    // behind the fire at all: the band is cleared once further up and
    // the flames paint over whatever was drawn into it. The old loop
    // painted every cell, so nothing could ever be behind it -- the
    // tree had to be drawn last and fake occlusion with a per-frame
    // heat test, which strobed. Draw order does that job correctly
    // and for free.
    for (int y = 0; y < fh; y++) {
        const uint8_t* row = &heat[y * MAXFW];
        int      runStart = -1;              // -1 == no run open
        uint16_t runCol   = 0;
        for (int x = 0; x < fw; x++) {
            uint8_t v = row[x];
            if (v > HEAT_MAX) v = HEAT_MAX;
            if (v == 0) {                    // cold: leave whatever is behind
                if (runStart >= 0) {
                    t.fillRect(runStart * CW, yStart + y * CW,
                               (x - runStart) * CW, CW, runCol);
                    runStart = -1;
                }
                continue;
            }
            // Dither along the heat ramp rather than in RGB. The flame
            // body below heat 36 is pure red, so an RGB dither can only
            // ever ADD green and blue that do not belong: in RGB332
            // those are 3- and 2-bit channels, so the offsets crossed
            // whole levels and turned dark red into olive-brown. Nudging
            // the heat index keeps every cell on the calibrated ramp,
            // breaks banding the same, and costs an add rather than
            // three float clamps and a colour conversion.
            int vd = (int)v + (FIRE_BAYER[(x & 3) | ((y & 3) << 2)] >> 2);
            if (vd < 1)        vd = 1;       // never dither a lit cell dark
            if (vd > HEAT_MAX) vd = HEAT_MAX;
            uint16_t col = fireLUT[vd];
            if (runStart < 0) { runStart = x; runCol = col; continue; }
            if (col != runCol) {
                t.fillRect(runStart * CW, yStart + y * CW,
                           (x - runStart) * CW, CW, runCol);
                runStart = x;
                runCol   = col;
            }
        }
        if (runStart >= 0) {
            t.fillRect(runStart * CW, yStart + y * CW,
                       (fw - runStart) * CW, CW, runCol);
        }
    }

    // The werewolf has exactly one thing to say. Right-aligned to the
    // screen rather than centred on the wolf: the line is wide, the wolf
    // stands at 80% across, and centring it would run the left end back
    // under the mascot -- who is drawn over this background by ui_clear
    // and would clip the first few letters off mid-word.
    if (wolfSayY >= 0) {
        static const char SKID[] = "Don't Be a SKID!";
        t.setTextSize(1);
        const int bw = t.textWidth(SKID) + 7;
        const int bh = 11;
        int bx = w - 2 - bw;
        int by = wolfSayY;
        if (bx < 1)          bx = 1;
        if (by < yStart + 1) by = yStart + 1;
        const uint16_t paper = t.color565(236, 232, 218);
        const uint16_t ink   = t.color565(16, 12, 10);
        t.fillRect(bx, by, bw, bh, paper);
        t.drawRect(bx, by, bw, bh, ink);
        // Tail under the wolf, not under the corner of the bubble.
        int tailX = wolfSayX - 3;
        if (tailX < bx + 2)      tailX = bx + 2;
        if (tailX > bx + bw - 6) tailX = bx + bw - 6;
        t.drawFastHLine(tailX, by + bh,     4, paper);
        t.drawFastHLine(tailX, by + bh + 1, 2, paper);
        t.drawPixel(tailX - 1, by + bh, ink);
        t.setTextColor(ink, paper);
        t.setCursor(bx + 4, by + 2);
        t.print(SKID);
    }

    // Owl quips. Drawn AFTER the flames, unlike the owl itself: a
    // half-occluded glyph reads as a rendering fault rather than as
    // depth, and text is the one thing in this scene that cannot afford
    // to look broken. The owl is high enough that flames seldom reach it
    // anyway, so the bubble rarely sits over fire.
    //
    // Timing is deliberately slow. A quip every eight and a half seconds
    // held for two and a half is a character doing a bit; anything
    // faster is a flicker, which is exactly the mistake the old tree
    // made.
    if (owlX >= 0) {
        // Same deadpan surveillance-paranoia the rest of the project
        // speaks in, not owl noises. The joke is that the one thing in
        // this scene actually watching you is the bird.
        //
        // Kept short for a hard layout reason as much as a comic one:
        // the bubble is drawn by the background, and the mascot is drawn
        // over the background afterwards, so a wide bubble gets its last
        // letters clipped by Squachy. See the placement below.
        static const char* const QUIPS[] = {
            "it's always DNS", "RTFM",          "rm -rf /",
            "ROT13 twice",       "allegedly",     "flag{h00t}",
            "salt your hash",    "0 days since"
        };
        static const uint8_t NQUIP = sizeof(QUIPS) / sizeof(QUIPS[0]);
        // While the werewolf is on stage the owl has other priorities.
        // Reuses the same bubble, just a different list and without
        // waiting for the 30s slot to come round.
        static const char* const SCARED[] = { "nope", "brb", "eep", "shh", "bye" };
        const bool wolfOut = (s_wolfAt != 0);
        const uint32_t CYCLE = 30000, SHOW = 3400;
        if (wolfOut || (now % CYCLE) < SHOW) {
            // Stride of 5 against 8 entries: coprime, so it still visits
            // every line, just not in list order (2,7,4,1,6,3,0,5).
            // Straight sequential reads as a loop once you have watched
            // it a few times. Check this stays coprime if the list
            // length changes -- a stride sharing a factor with the count
            // silently hides some of the lines forever.
            const char* q = wolfOut
                ? SCARED[((now - s_wolfAt) / 1400) % (sizeof(SCARED) / sizeof(SCARED[0]))]
                : QUIPS[((now / CYCLE) * 5 + 2) % NQUIP];
            t.setTextSize(1);
            const int bw = t.textWidth(q) + 7;
            const int bh = 11;
            // Placement has to dodge the mascot. drawFire is the
            // BACKGROUND: ui_clear draws Squachy on top of it afterwards,
            // so anything of ours reaching into the middle third gets
            // overpainted mid-word. Preferred spot is to the right of the
            // owl, back toward the centre; when the line is too wide for
            // that gap the bubble goes ABOVE the owl instead, where it
            // clears the top of his head, and only then falls back to
            // clamping against the screen edge.
            const int squachyLeft = (int)(w * 0.34f);
            int bx = owlX + 10;
            int by = owlY - 14;
            if (bx + bw > squachyLeft) {
                bx = owlX - bw / 2 + 4;      // centred over the owl
                by = owlY - 26;
            }
            if (bx + bw > w - 2)    bx = w - 2 - bw;
            if (bx < 1)             bx = 1;
            if (by < yStart + 1)    by = yStart + 1;
            const uint16_t paper = t.color565(236, 232, 218);
            const uint16_t ink   = t.color565(16, 12, 10);
            t.fillRect(bx, by, bw, bh, paper);
            t.drawRect(bx, by, bw, bh, ink);
            // Tail points at the owl, not at the corner of the bubble.
            // When a wide line pushes the bubble above the owl it ends up
            // centred over him, and a tail pinned to the left edge then
            // points at empty branch, which reads as two unrelated
            // objects rather than one speaking.
            int tailX = owlX - 1;
            if (tailX < bx + 2)      tailX = bx + 2;
            if (tailX > bx + bw - 6) tailX = bx + bw - 6;
            t.drawFastHLine(tailX, by + bh,     4, paper);
            t.drawFastHLine(tailX, by + bh + 1, 2, paper);
            t.drawPixel(tailX - 1, by + bh, ink);
            t.setTextColor(ink, paper);
            t.setCursor(bx + 4, by + 2);
            t.print(q);
        }
    }

    // The fuel. Flames used to rise out of the bottom edge from
    // nothing, which is the sort of thing nobody consciously notices
    // but which stops the scene reading as a campfire. Drawn after the
    // heat grid so flames come off the logs rather than through them,
    // and lit by litSmooth along with the rest of the scene.
    {
        uint16_t logCol = t.color565((uint8_t)(30.0f + litSmooth * 62.0f),
                                     (uint8_t)(16.0f + litSmooth * 26.0f),
                                     (uint8_t)(10.0f + litSmooth * 10.0f));
        uint16_t logLit = t.color565((uint8_t)(150.0f + litSmooth * 105.0f),
                                     (uint8_t)( 40.0f + litSmooth *  90.0f),
                                     (uint8_t)( 10.0f + litSmooth *  20.0f));
        int cx = w / 2, gy = yEnd - 6;
        int halfA = w / 5, halfB = w / 7;
        if (halfB < 4) halfB = 4;
        t.fillRect(cx - halfA, gy,     halfA * 2, 5, logCol);
        t.fillRect(cx - halfB, gy - 4, halfB * 2, 4, logCol);
        // Glowing gaps between the logs, flickering with the fire.
        for (int k = -2; k <= 2; k++) {
            int gxp = cx + k * (halfB / 2);
            if (((now / 120) + (uint32_t)(k + 2)) % 3) t.drawFastHLine(gxp - 3, gy - 1, 6, logLit);
        }
    }

    // Embers: a handful of sparks pop free of the flame and drift
    // upward, cooling from bright yellow through orange to nothing —
    // sells the "fire" far more than the heat grid alone. They spawn
    // at a randomly chosen flame source rather than anywhere across
    // the width, which is what the old version did: an ember could pop
    // out of bare ground where there was no flame at all.
    static const uint8_t NE = 8;
    static float    ex[NE], ey[NE], evy[NE];
    static bool     emberInited = false;
    if (!emberInited) {
        for (uint8_t i = 0; i < NE; i++) { ex[i] = -1; ey[i] = -1; evy[i] = 0; }
        emberInited = true;
    }
    for (uint8_t i = 0; i < NE; i++) {
        if (ey[i] < (float)yStart) {
            if (random(0, 30) == 0) {
                uint8_t si = (uint8_t)random(0, NSRC);
                ex[i]  = srcX[si] * (float)CW + (float)random(-6, 7);
                if (ex[i] < 0.0f) ex[i] = 0.0f;
                if (ex[i] > (float)(w - 1)) ex[i] = (float)(w - 1);
                ey[i]  = (float)(yEnd - 6);
                evy[i] = 0.6f + (float)random(0, 100) / 100.0f * 0.8f;
            }
            continue;
        }
        ey[i] -= evy[i];
        // Embers are light enough that the wind moves them noticeably
        // more than it bends the flame body.
        ex[i] += windF * 0.85f + sinf((float)now / 260.0f + i) * 0.4f;
        if (ex[i] < 0.0f || ex[i] > (float)(w - 1)) { ey[i] = (float)(yStart - 1); continue; }
        float lifeFrac = (ey[i] - (float)yStart) / (float)bandH;
        if (lifeFrac < 0.0f) lifeFrac = 0.0f;
        uint16_t emberCol = (lifeFrac > 0.6f)
            ? t.color565(255, 220, 80)
            : blend(BG, t.color565(255, 120, 20), (uint16_t)(255 * (lifeFrac / 0.6f)));
        t.drawPixel((int)ex[i], (int)ey[i], emberCol);
    }
}

void drawSnowfall(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    static const uint8_t N = 70;
    static float sx[N], sy[N], sspeed[N], sphase[N];
    static bool  inited = false;
    // A separate, much smaller set of big flakes — slower, wider sway,
    // drawn as a little snowflake glyph instead of a single dim pixel.
    static const uint8_t NB = 7;
    static float bx[NB], by[NB], bspeed[NB], bphase[NB];
    static bool  bInited = false;

    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 20) return;

    if (!inited) {
        for (uint8_t i = 0; i < N; i++) {
            sx[i]     = (float)random(0, w);
            sy[i]     = (float)random(yStart, yEnd);
            sspeed[i] = 0.4f + (float)random(0, 100) / 100.0f * 0.8f;
            sphase[i] = (float)random(0, 6283) / 1000.0f;
        }
        inited = true;
    }
    if (!bInited) {
        for (uint8_t i = 0; i < NB; i++) {
            bx[i]     = (float)random(0, w);
            by[i]     = (float)random(yStart, yEnd);
            bspeed[i] = 0.5f + (float)random(0, 100) / 100.0f * 0.5f;
            bphase[i] = (float)random(0, 6283) / 1000.0f;
        }
        bInited = true;
    }

    t.fillRect(0, yStart, w, bandH, BG);
    for (uint8_t i = 0; i < N; i++) {
        sy[i] += sspeed[i];
        sx[i] += sinf((float)now / 600.0f + sphase[i]) * 0.3f;
        if (sy[i] > yEnd) { sy[i] = (float)yStart; sx[i] = (float)random(0, w); }
        t.drawPixel((int)sx[i], (int)sy[i], blend(BG, WHITE, 140));
    }
    for (uint8_t i = 0; i < NB; i++) {
        by[i] += bspeed[i];
        bx[i] += sinf((float)now / 500.0f + bphase[i]) * 0.7f;
        if (by[i] > yEnd) { by[i] = (float)yStart; bx[i] = (float)random(0, w); }
        int px = (int)bx[i], py = (int)by[i];
        // Six-point sparkle: a plus and an X through the same center.
        t.drawLine(px - 3, py, px + 3, py, WHITE);
        t.drawLine(px, py - 3, px, py + 3, WHITE);
        t.drawLine(px - 2, py - 2, px + 2, py + 2, WHITE);
        t.drawLine(px - 2, py + 2, px + 2, py - 2, WHITE);
    }

    // Snowman easter egg: a classic three-ball snowman pops up at a
    // random spot on the ground every so often, sits there a while,
    // then fades away again — a still surprise to spot, not another
    // biped trudging across the screen.
    static bool     manActive = false;
    static uint32_t manNextAt = 0, manShownAt = 0;
    static bool     manInited = false;
    static float    manX;
    static const uint32_t MAN_DUR_MS = 9000, MAN_FADE_MS = 700;
    if (!manInited) { manNextAt = now + (uint32_t)random(12000, 30000); manInited = true; }
    if (!manActive && now >= manNextAt) {
        manActive = true;
        manShownAt = now;
        manX = (float)random(20, w > 40 ? w - 20 : w);
    }
    if (manActive) {
        uint32_t age = now - manShownAt;
        float alpha = 1.0f;
        if (age < MAN_FADE_MS) alpha = (float)age / MAN_FADE_MS;
        else if (age > MAN_DUR_MS - MAN_FADE_MS) alpha = (float)(MAN_DUR_MS - age) / MAN_FADE_MS;
        if (alpha < 0.0f) alpha = 0.0f;
        if (alpha > 1.0f) alpha = 1.0f;

        int groundY = yEnd - 2;
        int sx = (int)manX;
        int r1 = 13, r2 = 10, r3 = 7;  // was 9,7,5 -- ~1.4x
        int y1 = groundY - r1;
        int y2 = y1 - r1 - r2 + 4;
        int y3 = y2 - r2 - r3 + 4;
        uint16_t body = blend(BG, WHITE, (uint16_t)(255 * alpha));
        t.fillCircle(sx, y1, r1, body);
        t.fillCircle(sx, y2, r2, body);
        t.fillCircle(sx, y3, r3, body);

        if (alpha > 0.5f) {
            uint16_t detail = blend(BG, BLACK, (uint16_t)(255 * alpha));
            t.drawPixel(sx - 3, y3 - 1, detail);
            t.drawPixel(sx + 3, y3 - 1, detail);
            t.drawPixel(sx, y2 - 3, detail);
            t.drawPixel(sx, y2,     detail);
            t.drawPixel(sx, y2 + 3, detail);
            uint16_t carrot = blend(BG, t.color565(235, 130, 30), (uint16_t)(255 * alpha));
            t.fillTriangle(sx, y3, sx + 8, y3 + 1, sx, y3 + 3, carrot);
            uint16_t stick = blend(BG, t.color565(100, 65, 30), (uint16_t)(255 * alpha));
            t.drawLine(sx - r2 - 1, y2, sx - r2 - 11, y2 - 8, stick);
            t.drawLine(sx + r2 + 1, y2, sx + r2 + 11, y2 - 8, stick);
            uint16_t hat = blend(BG, BLACK, (uint16_t)(255 * alpha));
            t.fillRect(sx - 8, y3 - r3 - 3, 17, 3, hat);
            t.fillRect(sx - 6, y3 - r3 - 14, 11, 13, hat);
            uint16_t scarf = blend(BG, RED, (uint16_t)(255 * alpha));
            t.fillRect(sx - 7, y2 - r2, 14, 4, scarf);
        }

        if (age > MAN_DUR_MS) {
            manActive = false;
            manNextAt = now + (uint32_t)random(15000, 35000);
        }
    }
}

static uint16_t waterfallColor(uint16_t bg, uint16_t vaporBlue, uint16_t vaporPink,
                               uint16_t white, uint8_t level) {
    if (level < 40) return blend(bg, vaporBlue, (uint16_t)(level * 255 / 40));
    if (level < 75) return blend(vaporBlue, vaporPink, (uint16_t)((level - 40) * 255 / 35));
    return blend(vaporPink, white, (uint16_t)((level - 75) * 255 / 25));
}

void drawSpectrumWaterfall(TFT_eSPI& t, uint32_t now, int yStart, int yEnd,
                           const DetectionEngine& eng) {
    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 30) return;

    static const uint8_t MAXROWS = 56;
    static uint8_t   hist[MAXROWS][13];
    static bool      inited = false;
    static uint32_t  lastTick = 0;
    if (!inited) { memset(hist, 0, sizeof(hist)); inited = true; }

    int labelH  = 10;
    int gridTop = yStart + labelH;
    int gridH   = yEnd - gridTop;
    if (gridH < 8) return;
    int rowH  = 4;
    int rows  = gridH / rowH;
    if (rows > MAXROWS) rows = MAXROWS;
    int colW  = w / 13;

    // New row every ~160ms, scrolling everything else down one slot —
    // classic waterfall motion, newest activity always enters at top.
    if (now - lastTick > 160) {
        lastTick = now;
        for (int r = MAXROWS - 1; r > 0; r--) memcpy(hist[r], hist[r - 1], 13);
        for (uint8_t ch = 1; ch <= 13; ch++) hist[0][ch - 1] = eng.channelActivity(ch);
    }

    t.fillRect(0, yStart, w, bandH, BG);

    t.setTextSize(1);
    t.setTextColor(CYAN, BG);
    for (uint8_t ch = 1; ch <= 13; ch++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%u", ch);
        int lx = (ch - 1) * colW + colW / 2 - t.textWidth(buf) / 2;
        t.setCursor(lx, yStart);
        t.print(buf);
    }

    for (int r = 0; r < rows; r++) {
        int py = gridTop + r * rowH;
        if (py >= yEnd) break;
        for (uint8_t ch = 0; ch < 13; ch++) {
            uint16_t col = waterfallColor(BG, VAPOR_BLUE, VAPOR_PINK, WHITE, hist[r][ch]);
            t.fillRect(ch * colW, py, colW - 1, rowH - 1, col);
        }
    }
}

// Vector tube, Tempest-style: bright thin lines on black, a web of
// rings and lanes, a claw riding the rim and enemies climbing toward
// you out of the vanishing point.
//
// The structural change from the earlier version is that every ring now
// shares one rotation. Adjacent rings used to twist in opposite
// directions, which looked good as an abstract drill but meant ring
// vertices never lined up -- and with nothing to connect, the tube had
// no lanes, only floating hoops. Aligning them is what turns it into a
// web you could travel along, which is the whole read of the original
// game.
void drawWireframeTunnel(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 20) return;
    t.fillRect(0, yStart, w, bandH, BG);

    static const uint8_t RINGS = 15;
    static const uint8_t SIDES = 14;      // rounder than the old hexagon
    float aspect = (float)bandH / (float)w;

    // Wandering center -- the tube banks and curves instead of staring
    // straight down a fixed pipe.
    float wobT   = (float)now / 2600.0f;
    float wobAmt = (float)((w < bandH) ? w : bandH) * 0.10f;
    int cx = w / 2              + (int)(sinf(wobT) * wobAmt);
    int cy = yStart + bandH / 2 + (int)(cosf(wobT * 1.3f) * wobAmt * aspect);
    float maxR = sqrtf((float)(w * w + bandH * bandH)) * 0.5f + 12.0f;

    // Speed breathes via a slow phase-modulated time warp -- surges and
    // eases rather than one constant scroll rate.
    float warped = (float)now + 3200.0f * sinf((float)now / 5200.0f);
    float rot    = (float)now / 1800.0f;

    // Radius for any continuous depth, shared by the rings, the lanes,
    // the claw and the enemies so they all sit on the same tube.
    auto radiusAt = [&](float d) {
        float r = maxR * (1.0f / (d * 0.55f + 0.6f)) * 0.42f;
        return r > maxR ? maxR : r;
    };
    auto vertX = [&](float d, float s) { return cx + (int)(cosf(rot + s * (6.2831853f / SIDES)) * radiusAt(d)); };
    auto vertY = [&](float d, float s) { return cy + (int)(sinf(rot + s * (6.2831853f / SIDES)) * radiusAt(d) * aspect); };

    // Hue rotates slowly through the vaporwave set, so colour is part of
    // the motion rather than a fixed gradient.
    static const uint16_t hueStops[4] = { CYAN, VAPOR_PURPLE, VAPOR_PINK, VAPOR_BLUE };
    float huePos = fmodf((float)now / 5000.0f, 4.0f);
    int   h0 = (int)huePos % 4, h1 = (h0 + 1) % 4;
    uint16_t tunnelHue = blend(hueStops[h0], hueStops[h1], (uint16_t)((huePos - (int)huePos) * 255));

    // A shockwave travels down the tube every few seconds, flashing
    // whichever ring it overlaps -- a periodic beat with nothing to
    // actually beat to.
    static bool     pulseOn = false;
    static uint32_t pulseStart = 0, pulseNextAt = 0;
    static bool     pulseInited = false;
    if (!pulseInited) { pulseNextAt = now + (uint32_t)random(2000, 4000); pulseInited = true; }
    if (!pulseOn && now >= pulseNextAt) { pulseOn = true; pulseStart = now; }
    float pulseDepth = -10.0f;
    if (pulseOn) {
        float prog = (float)(now - pulseStart) / 700.0f;
        if (prog >= 1.0f) { pulseOn = false; pulseNextAt = now + (uint32_t)random(2000, 4500); }
        else              { pulseDepth = prog * RINGS; }
    }

    // Depth of each ring this frame, cycling so the tube flies forward.
    float depthOf[RINGS];
    for (uint8_t i = 0; i < RINGS; i++) {
        float d = fmodf((float)i - warped / 220.0f, (float)RINGS);
        depthOf[i] = (d < 0.0f) ? d + RINGS : d;
    }

    // ---- lanes: the tube walls, drawn first so rings sit on top -----
    // Each lane runs the full depth of the tube. Dimmer than the rings,
    // the way the original's web reads: the hoops are the bright part,
    // the connecting lines are structure.
    for (uint8_t s = 0; s < SIDES; s++) {
        for (uint8_t i = 0; i + 1 < RINGS; i++) {
            float dA = (float)i, dB = (float)(i + 1);
            float fade = 1.0f - dA / RINGS;
            t.drawLine(vertX(dA, s), vertY(dA, s), vertX(dB, s), vertY(dB, s),
                       blend(BG, tunnelHue, (uint16_t)(110 * fade)));
        }
    }

    // ---- rings ------------------------------------------------------
    for (int i = RINGS - 1; i >= 0; i--) {
        float depth = depthOf[i];
        float r = radiusAt(depth);
        float fade = 1.0f - depth / RINGS;
        // Near rings go to full brightness rather than a blend toward
        // the background -- vector hardware did not do subtle, and the
        // contrast is most of the look.
        uint16_t ringCol = blend(BG, tunnelHue, (uint16_t)(60 + 195 * fade));
        bool hit = fabsf(depth - pulseDepth) < 0.6f;
        if (hit) ringCol = blend(ringCol, WHITE, 200);

        int px[SIDES], py[SIDES];
        for (uint8_t s = 0; s < SIDES; s++) {
            float a = rot + s * (6.2831853f / SIDES);
            px[s] = cx + (int)(cosf(a) * r);
            py[s] = cy + (int)(sinf(a) * r * aspect);
        }
        for (uint8_t s = 0; s < SIDES; s++) {
            uint8_t n = (uint8_t)((s + 1) % SIDES);
            t.drawLine(px[s], py[s], px[n], py[n], ringCol);
            if (hit) t.drawLine(px[s], py[s] + 1, px[n], py[n] + 1, ringCol);
        }
    }

    // ---- enemies climbing the lanes ---------------------------------
    // Small spikes rising out of the vanishing point toward the rim,
    // each locked to one lane. Respawn deep once they arrive.
    static const uint8_t NE = 4;
    static float   enDepth[NE];
    static uint8_t enLane[NE];
    static bool    enInited = false;
    if (!enInited) {
        for (uint8_t i = 0; i < NE; i++) {
            enDepth[i] = (float)random(0, RINGS * 100) / 100.0f;
            enLane[i]  = (uint8_t)random(0, SIDES);
        }
        enInited = true;
    }
    for (uint8_t i = 0; i < NE; i++) {
        enDepth[i] -= 0.055f;
        if (enDepth[i] <= 0.2f) {
            enDepth[i] = (float)RINGS - 0.5f;
            enLane[i]  = (uint8_t)random(0, SIDES);
        }
        float d = enDepth[i];
        float fade = 1.0f - d / RINGS;
        int ex = vertX(d, (float)enLane[i]);
        int ey = vertY(d, (float)enLane[i]);
        int sz = 2 + (int)(fade * 5.0f);
        uint16_t ec = blend(BG, VAPOR_YELLOW, (uint16_t)(80 + 175 * fade));
        // A flipper: two crossed strokes, which is about all the
        // original could afford either.
        t.drawLine(ex - sz, ey - sz, ex + sz, ey + sz, ec);
        t.drawLine(ex - sz, ey + sz, ex + sz, ey - sz, ec);
    }

    // ---- the claw ---------------------------------------------------
    // Rides the outer rim, straddling one lane. Bright, because in the
    // original it is the one thing you are actually looking at.
    float clawPos = fmodf((float)now / 900.0f, (float)SIDES);
    float rOuter  = radiusAt(0.0f);
    int ax = cx + (int)(cosf(rot + clawPos * (6.2831853f / SIDES)) * rOuter);
    int ay = cy + (int)(sinf(rot + clawPos * (6.2831853f / SIDES)) * rOuter * aspect);
    int bx = cx + (int)(cosf(rot + (clawPos + 1.0f) * (6.2831853f / SIDES)) * rOuter);
    int by = cy + (int)(sinf(rot + (clawPos + 1.0f) * (6.2831853f / SIDES)) * rOuter * aspect);
    int mx = (ax + bx) / 2, my = (ay + by) / 2;
    int ix = cx + (int)((mx - cx) * 0.78f), iy = cy + (int)((my - cy) * 0.78f);
    t.drawLine(ax, ay, ix, iy, VAPOR_YELLOW);
    t.drawLine(bx, by, ix, iy, VAPOR_YELLOW);
    t.drawLine(ax, ay, bx, by, blend(BG, VAPOR_YELLOW, 150));

    float centerPulse = 0.5f + 0.5f * sinf((float)now / 400.0f);
    t.fillCircle(cx, cy, 2, blend(tunnelHue, WHITE, (uint16_t)(centerPulse * 255)));
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

// Colour of the sun at a normalised radius (0 = core, 1 = rim),
// matching drawSunsetSun's five concentric bands. Split out so the
// water reflection below can shade itself the same way without
// re-drawing circles it would then have to distort.
static uint16_t sunBandColor(TFT_eSPI& t, float rr) {
    if (rr > 1.0f) rr = 1.0f;
    if (rr > 0.88f) return t.color565(255,  50,  80);
    if (rr > 0.72f) return t.color565(255, 100,  40);
    if (rr > 0.54f) return t.color565(255, 165,  20);
    if (rr > 0.36f) return t.color565(255, 215,  70);
    return                 t.color565(255, 240, 150);
}

// ---- SYNTHWAVE ------------------------------------------------------
// The boot splash already composed a sunset sky, a scanline-cut sun,
// gulls and a neon grid -- and then threw it away after three seconds.
// This is that scene promoted to a real background (and now used by the
// splash too), plus the parts that make it read as a *place* rather
// than a backdrop:
//
//   - a reflection in the floor, so the ground is a wet surface rather
//     than a flat plane.
//   - the sun's reflection shimmering with its own travelling gaps and
//     horizontal displacement, so the surface never slides as one
//     rigid sheet.
//   - a two-layer parallax ridgeline for depth at the horizon.
//   - a bloom band where the sun meets the waterline.
//
// Deliberately none of this reads pixels back. A true mirror would
// sample the framebuffer and warp it: ~32,000 readPixel/drawPixel pairs
// per frame, which on this hardware costs more than the entire rest of
// the frame does. Instead the reflection is recomputed from the same
// gradient function and circle equation the sky and sun were drawn
// from, one span per row -- a few hundred draw calls rather than tens
// of thousands, and it looks identical because it is the same maths.
void drawSynthwave(TFT_eSPI& t, uint32_t now, int yTop, int yBottom,
                   float horizonFrac) {
    const int w = t.width();
    const int band = yBottom - yTop;
    if (band < 48 || w < 32) return;

    if (horizonFrac < 0.15f) horizonFrac = 0.15f;
    if (horizonFrac > 0.85f) horizonFrac = 0.85f;
    const int yHoriz = yTop + (int)(band * horizonFrac);
    const int skyH   = yHoriz - yTop;
    const int seaH   = yBottom - yHoriz;
    if (skyH < 8 || seaH < 8) return;

    // ---- sky -------------------------------------------------------
    for (int y = yTop; y < yHoriz; y++) {
        t.drawFastHLine(0, y, w, sunsetSkyColorAt(t, y, yTop, yHoriz));
    }

    // Stars spread across the real panel width rather than a fixed 240
    // -- the boot splash star field predates this scene being used on a
    // 320px-wide rotation, and bunches to the left there. Re-seeded if
    // the band changes shape, i.e. on rotate.
    static const uint8_t NSTAR = 18;
    static uint16_t stx[NSTAR];
    static uint8_t  sty[NSTAR], stph[NSTAR];
    static bool     starsInited = false;
    static int      starW = 0, starH = 0;
    if (!starsInited || starW != w || starH != skyH) {
        for (uint8_t i = 0; i < NSTAR; i++) {
            stx[i]  = (uint16_t)random(2, w - 2);
            sty[i]  = (uint8_t)random(0, skyH * 3 / 4);
            stph[i] = (uint8_t)random(0, 256);
        }
        starsInited = true; starW = w; starH = skyH;
    }
    for (uint8_t i = 0; i < NSTAR; i++) {
        uint32_t tw = (now / 10 + (uint32_t)stph[i] * 22) % 300;
        if (tw > 220) continue;
        uint8_t bri = (tw < 100) ? 255 : (uint8_t)(255 - (tw - 100) * 3);
        t.drawPixel(stx[i], yTop + sty[i], t.color565(bri, bri, (uint8_t)(bri * 0.88f)));
    }

    // ---- sun -------------------------------------------------------
    const int sunR  = (skyH * 4) / 5;
    const int sunCx = w / 2;
    // Sunk further as it grew: at this radius a third of it above the
    // horizon put the top edge off the band entirely.
    const int sunCy = yHoriz - (sunR / 4);
    drawSunsetSun(t, sunCx, sunCy, sunR, yTop, yHoriz);

    // ---- ridgeline -------------------------------------------------
    // Two layers, paler and taller behind, darker in front, so the
    // horizon has depth instead of being a bare line. Fixed profiles
    // generated once: a ridgeline that reshuffled every frame would
    // read as noise rather than landscape.
    static const uint8_t NPEAK = 9;
    static uint8_t farPk[NPEAK], nearPk[NPEAK];
    static bool ridgeInited = false;
    if (!ridgeInited) {
        for (uint8_t i = 0; i < NPEAK; i++) {
            farPk[i]  = (uint8_t)random(30, 100);
            nearPk[i] = (uint8_t)random(14,  58);
        }
        ridgeInited = true;
    }
    const int farMax  = skyH / 4;
    const int nearMax = skyH / 6;
    for (uint8_t layer = 0; layer < 2; layer++) {
        const uint8_t* pk = layer ? nearPk : farPk;
        int      maxH = layer ? nearMax : farMax;
        uint16_t c    = layer ? t.color565(28, 4, 38) : t.color565(60, 12, 68);
        int step = w / (NPEAK - 1);
        if (step < 1) step = 1;
        for (uint8_t i = 0; i + 1 < NPEAK; i++) {
            int x0 = i * step, x1 = (i + 1) * step;
            int h0 = (pk[i]     * maxH) / 100;
            int h1 = (pk[i + 1] * maxH) / 100;
            for (int x = x0; x <= x1 && x < w; x++) {
                float f = (x1 > x0) ? (float)(x - x0) / (float)(x1 - x0) : 0.0f;
                int hh = h0 + (int)((h1 - h0) * f);
                if (hh > 0) t.drawFastVLine(x, yHoriz - hh, hh, c);
            }
        }
    }

    // ---- birds -----------------------------------------------------
    // Drawn after the ridgeline so nothing occludes them: they are the
    // nearest thing in the scene. Position is derived straight from
    // `now` rather than integrated frame to frame -- no accumulated
    // state means no drift, and no dt to clamp when the frame rate
    // moves around.
    //
    // The silhouette colour is deliberately close to the darkest part of
    // the sky. Crossing the sun they read as hard cut-outs, which is the
    // shot; out over open sky they nearly vanish, which is both cheaper
    // to look at and roughly what a distant bird actually does.
    {
        static const uint8_t NBIRD = 5;
        static uint8_t birdY[NBIRD], birdSz[NBIRD];
        static bool birdsInited = false;
        static int  birdW = 0, birdH = 0;
        if (!birdsInited || birdW != w || birdH != skyH) {
            for (uint8_t i = 0; i < NBIRD; i++) {
                // Upper two thirds of the sky: low enough to cross the
                // sun, high enough to clear the ridgeline.
                birdY[i]  = (uint8_t)random(2, (skyH * 2) / 3);
                birdSz[i] = (uint8_t)random(3, 6);
            }
            birdsInited = true; birdW = w; birdH = skyH;
        }
        const uint16_t birdCol = t.color565(26, 6, 34);
        const float    span    = (float)(w + 28);
        for (uint8_t i = 0; i < NBIRD; i++) {
            const float spd = 0.009f + (float)(i % 3) * 0.005f;   // px per ms
            const float fx  = fmodf((float)now * spd + (float)i * 63.0f, span) - 14.0f;
            const int   x   = (int)fx;
            const int   y   = yTop + birdY[i];
            if (y < yTop + 1 || y >= yHoriz - 1) continue;
            const int   sz  = birdSz[i];
            // Wing beat. Each bird carries its own phase so the flock
            // does not pulse in unison, which reads as one object.
            const float flap = sinf((float)now / (120.0f + i * 17.0f) + (float)i * 1.7f);
            const int   dy   = (int)(flap * (float)sz * 0.7f);
            if (x - sz < 0 || x + sz >= w) continue;
            // Two shallow strokes per wing give the gull kink; a single
            // straight V reads as a chevron, not a bird.
            t.drawLine(x - sz,     y - dy,     x - sz / 2, y,          birdCol);
            t.drawLine(x - sz / 2, y,          x,          y - dy / 2, birdCol);
            t.drawLine(x + sz,     y - dy,     x + sz / 2, y,          birdCol);
            t.drawLine(x + sz / 2, y,          x,          y - dy / 2, birdCol);
        }
    }

    // ---- horizon bloom ---------------------------------------------
    for (int dy = -2; dy <= 1; dy++) {
        int y = yHoriz + dy;
        if (y < yTop || y >= yBottom) continue;
        uint8_t a = (dy == -1 || dy == 0) ? 235 : 120;
        t.drawFastHLine(0, y, w, blend(t.color565(90, 10, 60), t.color565(255, 150, 190), a));
    }

    // ---- water -----------------------------------------------------
    // Sky and sun, mirrored and foreshortened, in a single pass per row:
    // the surface colour first, then the sun's reflection blended over
    // it where the mirrored row crosses the sun's circle.
    //
    // The sky reflection is not displaced horizontally, because it is a
    // flat horizontal gradient -- sliding it sideways would cost time
    // and show nothing. What reads as a moving surface is a travelling
    // *brightness* ripple, one sinf per row. The sun's reflection does
    // get displaced, because there the shape is visible.
    const uint16_t waterBase = t.color565(10, 0, 30);
    for (int y = yHoriz; y < yBottom; y++) {
        const float d = (float)(y - yHoriz) / (float)seaH;   // 0 horizon, 1 viewer
        const int srcY = yHoriz - (int)(d * skyH * 0.82f);

        // Two ripple rates so the surface never pulses uniformly.
        float rip = sinf(d * 34.0f - (float)now / 210.0f)
                  + 0.5f * sinf(d * 61.0f + (float)now / 130.0f);
        int dim = (int)(196.0f - d * 118.0f + rip * 17.0f);
        if (dim < 24)  dim = 24;
        if (dim > 255) dim = 255;
        const uint16_t waterC =
            blend(waterBase, sunsetSkyColorAt(t, srcY, yTop, yHoriz), (uint8_t)dim);
        t.drawFastHLine(0, y, w, waterC);

        // Sun reflection: same circle equation as the real sun, taken on
        // the mirrored row.
        const int delta = srcY - sunCy;
        if (delta <= -sunR || delta >= sunR) continue;
        const int half = (int)sqrtf((float)(sunR * sunR - delta * delta));
        if (half <= 0) continue;

        // Two incommensurate rates, not one. A single sine gives gaps at
        // a perfectly fixed pitch, which the eye reads as banding --
        // regular stripes rather than water. Summing a second, unrelated
        // frequency (and a different time rate) means the pattern never
        // repeats over the surface. They also open up toward the viewer,
        // where the water is choppier.
        const float gapPhase = sinf(d * 62.0f - (float)now / 190.0f)
                             + 0.55f * sinf(d * 23.0f + (float)now / 310.0f);
        if (gapPhase > 0.05f - d * 0.40f) continue;

        const float ph  = (float)now / 260.0f;
        const float wob = sinf(d * 11.0f + ph)        * (2.0f + d * 11.0f)
                        + sinf(d * 27.0f - ph * 1.7f) * (1.0f + d *  4.5f);

        const float rr = (float)(delta < 0 ? -delta : delta) / (float)sunR;
        // Blended over the water colour for this row, not over a flat
        // dark: reflected light sits *in* the surface. Blending the
        // sun's yellows against near-black was turning them olive.
        const uint8_t a = (uint8_t)(228 - d * 128);
        const uint16_t c = blend(waterC, sunBandColor(t, rr), a);

        int x0 = sunCx - half + (int)wob;
        int xs = x0 < 0 ? 0 : x0;
        int xe = x0 + half * 2; if (xe > w) xe = w;
        if (xe > xs) t.drawFastHLine(xs, y, xe - xs, c);
    }

    // ---- grid ------------------------------------------------------
    // Rungs only. The converging lines to the vanishing point were
    // fighting the reflection: they cut across the sun's bands and
    // reasserted a hard flat plane exactly where the water was doing
    // the work of looking like a surface. The scrolling rungs alone
    // still give the floor motion and depth.
    const uint16_t gridCol = blend(BG, CYAN, 70);
    const uint32_t period = 1100;
    float basePhase = (float)(now % period) / (float)period;
    for (int i = 0; i < 7; i++) {
        float tt = fmodf(basePhase + (float)i / 7.0f, 1.0f);
        int y = yHoriz + (int)(tt * tt * seaH);
        if (y <= yHoriz || y >= yBottom) continue;
        uint8_t fade = (uint8_t)(70 + tt * 185);   // brighter as it nears the viewer
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

void drawTransitionGlitch(TFT_eSPI& t, uint32_t elapsedMs, uint32_t totalMs) {
    if (elapsedMs >= totalMs) return;
    int w = t.width();
    int h = t.height();
    float fade = 1.0f - (float)elapsedMs / (float)totalMs;
    int bands = 2 + (int)(fade * 5);

    static const int MAX_W = 400;
    static uint16_t rowBuf[MAX_W];
    int useW = (w < MAX_W) ? w : MAX_W;

    for (int i = 0; i < bands; i++) {
        int maxY = (h > 3) ? h - 3 : 1;
        int by   = random(0, maxY);
        int bh   = 1 + random(0, 2);
        int xoff = random(-10, 11);
        if (xoff == 0) xoff = 4;
        for (int row = 0; row < bh && (by + row) < h; row++) {
            int y = by + row;
            for (int x = 0; x < useW; x++) rowBuf[x] = t.readPixel(x, y);
            for (int x = 0; x < useW; x++) {
                int sx = x - xoff;
                if (sx < 0) sx = 0;
                if (sx >= useW) sx = useW - 1;
                t.drawPixel(x, y, rowBuf[sx]);
            }
        }
    }
    if (random(0, 3) == 0) {
        t.drawFastHLine(0, random(0, h), w, blend(BG, WHITE, (uint16_t)(fade * 200)));
    }
}

void drawSignalRadar(TFT_eSPI& t, int cx, int cy, int r, uint32_t now,
                     int8_t rssi, float bearingRad) {
    t.drawCircle(cx, cy, r,           blend(BG, CYAN, 90));
    t.drawCircle(cx, cy, r * 2 / 3,   blend(BG, CYAN, 60));
    t.drawCircle(cx, cy, r / 3,       blend(BG, CYAN, 40));
    t.drawFastHLine(cx - r, cy, 2 * r, blend(BG, CYAN, 30));
    t.drawFastVLine(cx, cy - r, 2 * r, blend(BG, CYAN, 30));

    // Continuously rotating sweep line.
    float sweep = (float)(now % 2000) / 2000.0f * 6.2831853f;
    int sx = cx + (int)(sinf(sweep) * r);
    int sy = cy - (int)(cosf(sweep) * r);
    t.drawLine(cx, cy, sx, sy, blend(BG, GREEN, 200));

    // Blip: stronger signal (less negative rssi) sits closer to center.
    float sigT = (float)(rssi + 90) / 60.0f;   // -90dBm..-30dBm -> 0..1
    if (sigT < 0.0f) sigT = 0.0f;
    if (sigT > 1.0f) sigT = 1.0f;
    float blipR = r * (1.0f - sigT * 0.85f);
    int bx = cx + (int)(sinf(bearingRad) * blipR);
    int by = cy - (int)(cosf(bearingRad) * blipR);
    uint16_t blipCol = (sigT > 0.66f) ? RED : (sigT > 0.33f ? AMBER : GREEN);
    t.fillCircle(bx, by, 3, blipCol);
    t.drawCircle(bx, by, 5, blend(BG, blipCol, 120));

    t.drawCircle(cx, cy, r, VAPOR_PINK);
}

static const BangersFont::Glyph* bangersFind(char c, BangersSize size) {
    const BangersFont::Glyph* table = (size == BangersSize::LG) ? BangersFont::LG_GLYPHS : BangersFont::MD_GLYPHS;
    uint8_t count = (size == BangersSize::LG) ? BangersFont::LG_GLYPH_COUNT : BangersFont::MD_GLYPH_COUNT;
    for (uint8_t i = 0; i < count; i++) {
        if (table[i].ch == c) return &table[i];
    }
    return nullptr;
}

int bangersTextWidth(const char* s, BangersSize size) {
    int w = 0;
    for (const char* p = s; *p; p++) {
        const BangersFont::Glyph* g = bangersFind(*p, size);
        if (g) w += g->advance;
    }
    return w;
}

// Random brief "signal corruption" glitch on Bangers headline text --
// a whole-text x-jitter plus scattered horizontal scanline dropouts,
// refreshed every ~40ms during a short burst that fires every 5-10s.
// Deterministic from `now` (a cheap multiplicative hash, not repeated
// random() calls) rather than rolling fresh dice per row: several
// callers redraw the same string many times per frame at tiny offsets
// to backfill a solid-color outline behind the real fill color (see
// ui_alert.cpp/ui_watchalert.cpp) -- if each of those passes rolled
// its own random jitter they'd all land differently and the outline
// would smear apart from the fill instead of tearing together like
// one corrupted signal.
static uint32_t s_glitchNextAt   = 0;
static uint32_t s_glitchUntil    = 0;
static uint8_t  s_glitchLevel    = 1;   // 0..4, see triggerGlitchBurst()'s comment

// Per-level tuning -- index is the clamped 0..4 intensity. Deliberately
// NOT a smooth curve: level 3 is where a full-screen tear (see
// drawGlitchStatic()) joins in on top of everything else, so levels 3
// and 4 jump harder than the 0->1->2 ramp does.
static const uint32_t BURST_MS_BY_LEVEL[]    = { 150, 200, 260, 320, 420 };
static const int      SPECKLE_N_BY_LEVEL[]   = {  60, 110, 170, 240, 320 };
static const int      JITTER_MAX_BY_LEVEL[]  = {   1,   2,   3,   4,   5 };
static const int      DROPOUT_MOD_BY_LEVEL[] = {  10,   6,   4,   3,   2 };  // 1-in-N rows drop

static uint32_t glitchHash(uint32_t x) {
    x *= 2654435761u;
    x ^= x >> 15;
    return x;
}

// Advances the shared burst timer and reports whether `now` falls
// inside one. Safe to call more than once for the same `now` (e.g.
// once from drawGlitchStatic() and again from several drawBangersText()
// calls within one frame) -- once the first call arms a burst,
// `now < s_glitchUntil` makes every later call in that same instant
// see it's already armed instead of re-rolling.
static bool updateGlitchState(uint32_t now) {
    if (s_glitchNextAt == 0) s_glitchNextAt = now + (uint32_t)random(5000, 10001);
    if (now >= s_glitchNextAt && now >= s_glitchUntil) {
        // Ambient, nobody-asked-for-it bursts always stay mild (level
        // 1) so idle screens read as consistent flavor, not a ramping
        // spectacle -- only an explicit triggerGlitchBurst() call asks
        // for something louder.
        s_glitchLevel  = 1;
        s_glitchUntil  = now + BURST_MS_BY_LEVEL[s_glitchLevel];
        s_glitchNextAt = s_glitchUntil + (uint32_t)random(5000, 10001);
    }
    return now < s_glitchUntil;
}

bool glitchActive() {
    return updateGlitchState(millis());
}

void triggerGlitchBurst(uint8_t intensity) {
    if (intensity > 4) intensity = 4;
    uint32_t now = millis();
    s_glitchLevel  = intensity;
    s_glitchUntil  = now + BURST_MS_BY_LEVEL[intensity];
    s_glitchNextAt = s_glitchUntil + (uint32_t)random(5000, 10001);
}

// Real per-frame TV-static snow, not the deterministic per-bucket
// jitter drawBangersText() uses -- this has no multi-pass outline to
// stay in sync with, so a fresh random() scatter every call (i.e.
// every frame it's active) gives the authentic flickering-snow look
// instead of a held static pattern.
void drawGlitchStatic(TFT_eSPI& t, int x0, int y0, int x1, int y1) {
    if (!glitchActive()) return;
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    static const uint16_t SPECKLE_COLORS[] = {
        WHITE, CYAN, VAPOR_PINK, VAPOR_PURPLE, VAPOR_BLUE, PURPLE,
    };
    int speckleN = SPECKLE_N_BY_LEVEL[s_glitchLevel];
    for (int i = 0; i < speckleN; i++) {
        int sx = x0 + random(0, rw);
        int sy = y0 + random(0, rh);
        uint16_t col = SPECKLE_COLORS[random(0, 6)];
        if (random(0, 3) == 0) t.drawFastHLine(sx, sy, 2, col);
        else                   t.drawPixel(sx, sy, col);
    }
    // The big payoff at the top two levels -- a genuine pixel-shifted
    // screen tear layered on top of the speckle/text glitch already
    // drawn this frame, reusing the same tear drawTransitionGlitch()
    // already does for screen-change transitions (fade=1.0 at level 4,
    // a smaller half-strength tear at level 3) rather than a second
    // bespoke tear implementation.
    if (s_glitchLevel >= 3) {
        drawTransitionGlitch(t, (s_glitchLevel >= 4) ? 0 : 50, 100);
    }
}

// One actual render pass -- shared by the real draw and the ghost copy
// below so both respect the exact same per-row dropout decisions
// (same bucket/row inputs) and tear together instead of independently.
static void drawBangersPass(TFT_eSPI& t, const char* s, int x, int y, uint16_t color,
                             BangersSize size, bool glitching, uint32_t bucket, uint8_t level) {
    int cursorX = x;
    int dropoutMod = DROPOUT_MOD_BY_LEVEL[level];
    for (const char* p = s; *p; p++) {
        const BangersFont::Glyph* g = bangersFind(*p, size);
        if (!g) continue;
        if (g->bitmap) {
            int rowBytes = (g->w + 7) / 8;
            for (int row = 0; row < g->h; row++) {
                // Same dropout decision for a given (bucket, row) no
                // matter which glyph or which pass is drawing it, so a
                // dropped scanline tears across the whole word at once
                // instead of a random per-letter speckle.
                if (glitching && (glitchHash(bucket * 131u + row) % dropoutMod) == 0) continue;
                const uint8_t* rowPtr = g->bitmap + row * rowBytes;
                int runStart = -1;
                for (int col = 0; col <= g->w; col++) {
                    bool bit = false;
                    if (col < g->w) {
                        uint8_t byte = rowPtr[col / 8];
                        bit = (byte >> (7 - (col % 8))) & 1;
                    }
                    if (bit && runStart < 0) runStart = col;
                    if (!bit && runStart >= 0) {
                        t.drawFastHLine(cursorX + g->xoff + runStart, y + g->yoff + row, col - runStart, color);
                        runStart = -1;
                    }
                }
            }
        }
        cursorX += g->advance;
    }
}

void drawBangersText(TFT_eSPI& t, int x, int y, const char* s, uint16_t color, BangersSize size) {
    uint32_t now = millis();
    bool glitching = updateGlitchState(now);
    uint8_t level = s_glitchLevel;
    uint32_t bucket = now / 40;
    int jitterMax = JITTER_MAX_BY_LEVEL[level];
    int jitterX = glitching ? (int)(glitchHash(bucket) % (2 * jitterMax + 1)) - jitterMax : 0;

    // Chromatic-split ghost -- a faint offset copy in a contrasting
    // color, drawn first so the real pass paints over/beside it.
    // Skipped for BLACK: that's the color the outline trick
    // (ui_clear.cpp/ui_watchalert.cpp) uses for its 24-pass solid
    // backing behind the one real colored pass that follows -- ghosting
    // each of those 24 would be wasted work and visual mud, not fringe.
    // Offset grows with level too, so the fringe visibly widens along
    // with everything else instead of staying a fixed 2px at any
    // intensity.
    if (glitching && color != BLACK) {
        uint16_t ghostColor = blend(CYAN, VAPOR_PINK, (uint16_t)(glitchHash(bucket + 7) % 256));
        int ghostOfs = 2 + level;
        drawBangersPass(t, s, x + jitterX + ghostOfs, y - 1, ghostColor, size, glitching, bucket, level);
    }

    drawBangersPass(t, s, x + jitterX, y, color, size, glitching, bucket, level);
}

// Ported from squachy.cpp verbatim (was a private static there,
// duplicated for LOG's MORE INFO panel until this promotion) -- see
// its declaration in theme.h for the full behavior notes.
//
// lines[][48], not [40]: confirmed on real hardware that a wide-enough
// maxW (a caller with real screen width to spare) let a line accumulate
// several words -- each individually well under maxW in pixels -- past
// the buffer's own char capacity before the width check ever tripped,
// silently truncating mid-word ("...Amazon's vid" instead of "video").
// The strlen() check added below is the actual fix (forces a break the
// moment the buffer itself would fill, independent of maxW); the wider
// buffer just means that happens less often in the first place.
uint8_t wrapText(TFT_eSPI& t, const char* text, int maxW,
                 char lines[][48], uint8_t maxLines) {
    // 320, not the original 160 -- fine for every short quip/bubble
    // this ran on originally, but LOG's MORE INFO panel passes real
    // paragraph-length explanations (the RSSI/confidence primer alone
    // is ~290 chars), which strncpy was silently truncating before a
    // single word ever got wrapped.
    char buf[320];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    uint8_t n = 0;
    char lineBuf[48] = "";
    char* word = strtok(buf, " ");
    while (word) {
        char trial[48];
        if (lineBuf[0]) snprintf(trial, sizeof(trial), "%s %s", lineBuf, word);
        else            snprintf(trial, sizeof(trial), "%s", word);
        bool tooWide = lineBuf[0] &&
                       (t.textWidth(trial) > maxW || strlen(trial) >= sizeof(lineBuf) - 1);
        if (tooWide) {
            if (n >= maxLines - 1) break; // out of lines -- let the rest go rather than drop it silently
            strncpy(lines[n], lineBuf, sizeof(lines[n]) - 1); lines[n][sizeof(lines[n]) - 1] = 0; n++;
            strncpy(lineBuf, word, sizeof(lineBuf) - 1); lineBuf[sizeof(lineBuf) - 1] = 0;
        } else {
            strncpy(lineBuf, trial, sizeof(lineBuf) - 1); lineBuf[sizeof(lineBuf) - 1] = 0;
        }
        word = strtok(nullptr, " ");
    }
    if (lineBuf[0] && n < maxLines) {
        strncpy(lines[n], lineBuf, sizeof(lines[n]) - 1); lines[n][sizeof(lines[n]) - 1] = 0; n++;
    }
    return n;
}

// Info panel geometry -- see theme.h's comment on drawInfoPanel() for
// what this is/who uses it.
static const uint8_t INFO_MAX_LINES = 7;

static void infoRects(int screenW, int screenH,
                       int& px, int& py, int& pw, int& ph,
                       int& headingY,
                       int& squachyCx, int& squachyBaseY, float& squachyScale, int& squachyWander,
                       int& textTop, int& textMaxW,
                       int& btnX, int& btnY, int& btnW, int& btnH) {
    // As much of the screen as the panel can reasonably use -- the
    // description text (size 1, see drawInfoPanel()) still needs real
    // room, and a small-margin modal reads fine here since it's the
    // only thing on screen while it's up.
    pw = screenW - 16;
    if (pw > 300) pw = 300;
    ph = screenH - 8;
    if (ph > 260) ph = 260;
    px = (screenW - pw) / 2;
    py = (screenH - ph) / 2;

    headingY = py + 6;
    squachyCx = px + pw / 2;
    squachyScale = 0.78f;
    squachyBaseY = py + 88;
    squachyWander = pw / 2 - 40;
    if (squachyWander < 0) squachyWander = 0;
    textTop = py + 104;
    textMaxW = pw - 16;

    btnH = 26;
    btnW = pw - 20;
    btnX = px + 10;
    btnY = py + ph - btnH - 8;
}

bool infoPanelHitDismiss(int x, int y, int screenW, int screenH) {
    int px, py, pw, ph, headingY, squachyCx, squachyBaseY, squachyWander, textTop, textMaxW, btnX, btnY, btnW, btnH;
    float squachyScale;
    infoRects(screenW, screenH, px, py, pw, ph, headingY, squachyCx, squachyBaseY, squachyScale, squachyWander,
              textTop, textMaxW, btnX, btnY, btnW, btnH);
    return x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH;
}

void drawInfoPanel(TFT_eSPI& t, int w, int h, uint32_t now,
                   const char* typeName, const char* text) {
    int px, py, pw, ph, headingY, squachyCx, squachyBaseY, squachyWander, textTop, textMaxW, btnX, btnY, btnW, btnH;
    float squachyScale;
    infoRects(w, h, px, py, pw, ph, headingY, squachyCx, squachyBaseY, squachyScale, squachyWander,
              textTop, textMaxW, btnX, btnY, btnW, btnH);

    t.fillRoundRect(px, py, pw, ph, 6, BG);
    t.drawRoundRect(px, py, pw, ph, 6, PURPLE);

    // No heading during the one-time RSSI/confidence primer page --
    // typeName is null then since that page isn't about any one type.
    if (typeName) {
        int tw = bangersTextWidth(typeName, BangersSize::MD);
        int maxTw = pw - 16;
        if (tw > maxTw) tw = maxTw; // clipped, not shrunk -- every real type name fits comfortably as-is
        drawBangersText(t, px + (pw - tw) / 2, headingY, typeName, VAPOR_PINK, BangersSize::MD);
    }

    Squachy::drawWaving(t, squachyCx, squachyBaseY, now, squachyScale, nullptr, true, squachyWander);

    t.setTextSize(1);
    t.setTextWrap(false);
    t.setTextColor(WHITE, BG);
    char lines[INFO_MAX_LINES][48];
    uint8_t n = wrapText(t, text, textMaxW, lines, INFO_MAX_LINES);
    int ly = textTop;
    for (uint8_t i = 0; i < n; i++) {
        int lw = t.textWidth(lines[i]);
        t.setCursor(px + (pw - lw) / 2, ly);
        t.print(lines[i]);
        ly += 12;
    }

    drawButton(t, btnX, btnY, btnW, btnH, "[ GOT IT ]", false, 2);
}

}  // namespace Theme
