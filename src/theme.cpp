// SquachWatch-CYD — theme implementation
#include "theme.h"
#include "caustic_tile.h"
#include "lil_guy.h"
#include "detection.h"
#include "bangers_font.h"
#include "squachy.h"
#include "settings.h"

namespace Theme {

// Usable bottom of the background band, published by whichever screen
// owns the layout. -1 means nobody said, so backgrounds fall back to
// their own yEnd. Declared up here because drawFlyingToasters() reads
// it and sits well above the setters.
static int s_bgFloor = -1;

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
    // Ordered cool -> warm so that indexing them by depth gives distance:
    // violet sits at the back, pink at the front.
    static const uint16_t HUES[4] = { VAPOR_PURPLE, CYAN, GREEN, VAPOR_PINK };
    // One switch back to the old behaviour, where hue was rolled at random
    // per drop and had nothing to do with how far away it was.
    static const bool RAIN_DEPTH_HUE = true;

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
    // Glyphs that break loose, grow and fade. Drawn with a transparent
    // background so they read as rising OFF the rain rather than punching
    // holes in it -- at most five, so a handful of extra glyphs a frame.
    static const uint8_t NPOP = 5;
    static uint16_t popX[NPOP], popY[NPOP];
    static uint8_t  popAge[NPOP], popHue[NPOP], popCh[NPOP];
    static uint32_t boltAt = 0, boltNext = 0;
    // CRT roll bar and the VHS head-switch tear. Both are decided once per
    // frame and applied as a per-cell comparison, so neither draws a single
    // extra glyph.
    static uint32_t sweepAt = 0;
    static uint32_t tearAt = 0, tearNext = 0;
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
        // Hue follows depth rather than being rolled at random: far drops
        // run cool, near ones warm, so the field reads as having space in
        // it instead of four colours scattered arbitrarily. Set
        // RAIN_DEPTH_HUE to false for the old random roll.
        colHue[i]   = RAIN_DEPTH_HUE ? (uint8_t)(colDepth[i] >> 6)
                                     : (uint8_t)random(0, 4);
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

    // LIGHTNING. Every 14-32s the field flares white for ~140ms and decays.
    // Costs one comparison: it scales an alpha that is already computed.
    if (advance && now >= boltNext) {
        boltAt   = now;
        boltNext = now + (uint32_t)random(14000, 32000);
    }
    const uint32_t boltAge = now - boltAt;
    const uint16_t boltAmt = (boltAt && boltAge < 140)
                             ? (uint16_t)(160u - (boltAge * 160u) / 140u) : 0;

    // CRT SWEEP. One pass down the field every ten seconds, then nothing --
    // it used to roll continuously, which at three seconds a pass made it
    // scenery rather than an event.
    //
    // What makes the bar legible is the blanking gap: the beam is off
    // during retrace, so a few rows go DARK immediately ahead of the bright
    // edge. Dark-then-hard-bright-then-short-decay is the whole effect. An
    // earlier attempt was a pure brightness lift with a long falloff, which
    // on a black field reads as a vague haze -- there is nothing for a
    // bright edge to be bright AGAINST.
    //
    // Driven off elapsed time rather than an accumulator, so the pass takes
    // the same wall-clock time regardless of frame rate, and parking it far
    // off screen between passes means every per-cell test below is simply
    // false for the other nine seconds.
    static const uint32_t SWEEP_PERIOD_MS = 10000;
    static const uint32_t SWEEP_TRAVEL_MS = 1200;
    if (advance && (now - sweepAt) >= SWEEP_PERIOD_MS) sweepAt = now;
    const uint32_t sweepAge = now - sweepAt;
    const int      travel   = (yEnd - yStart) + 80;
    const int rollAt = (sweepAt && sweepAge < SWEEP_TRAVEL_MS)
                       ? (yStart - 40 + (int)(((uint32_t)travel * sweepAge)
                                              / SWEEP_TRAVEL_MS))
                       : -30000;

    // VHS HEAD-SWITCH TEAR. On tape the bottom few lines are written by a
    // different head and never quite line up, so they sit torn sideways.
    // Here it fires as an occasional glitch rather than permanently.
    if (advance && now >= tearNext) {
        tearAt   = now;
        tearNext = now + 5000u + (uint32_t)random(0, 9000);
    }
    const bool tearOn = (tearAt != 0) && (now - tearAt < 260u);
    const int  tearY  = yEnd - 14;
    const int  tearDx = tearOn ? (int)random(-7, 8) : 0;

    // SQUACHY DISPLACES THE RAIN. Columns behind him are knocked back and
    // pushed aside, so he stands IN the field rather than on top of it.
    // Per column, not per cell -- his footprint is one comparison against
    // each column's x.
    int sqCx = 0, sqHalf = 0, sqTop = 0, sqBot = 0;
    const bool sqHere = Squachy::lastFootprint(sqCx, sqHalf, sqTop, sqBot);

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
        // How far into his silhouette this column falls, 0 outside.
        int sqPush = 0, sqDim = 0;
        if (sqHere) {
            const int d = xBase - sqCx;
            const int a = d < 0 ? -d : d;
            if (a < sqHalf + 10) {
                // Nearest the middle of him gets pushed hardest and dimmed
                // most; it tapers off to nothing at the edge of the push
                // zone so there is no hard line down the screen.
                const int strength = ((sqHalf + 10) - a) * 255 / (sqHalf + 10);
                sqPush = ((d < 0 ? -1 : 1) * strength * 7) / 255;
                sqDim  = strength;
            }
        }
        for (int j = 0; j < len; j++) {
            const int16_t ry = (int16_t)(yPos[i] - j * 8);
            if (ry < yStart || ry >= yEnd) continue;
            // Near drops lean further than far ones, so a gust reads with
            // depth instead of shunting the whole screen at once.
            int x = xBase + sqPush;
            // Inside the roll bar the row is dragged sideways; on the tear
            // it is dragged further still.
            const int rd = ry - rollAt;
            // Rows inside the bright core get dragged sideways, the way a
            // tracking error smears the lines it passes through.
            if (rd >= 0 && rd < 4)       x += 5 - rd;
            if (tearOn && ry >= tearY)   x += tearDx;

            // Fade in across the top few rows. Drops used to appear at full
            // brightness the instant they crossed yStart, which popped.
            uint16_t edge = 255;
            if (ry < yStart + 16) edge = (uint16_t)(((ry - yStart) * 255) / 16);
            // Only dim behind him where he actually is vertically, so the
            // rain above and below his head is untouched.
            if (sqDim && ry >= sqTop && ry <= sqBot)
                edge = (uint16_t)((edge * (uint16_t)(255 - (sqDim * 3) / 4)) / 255u);

            const char buf[2] = { GLYPHS[charBuf[i][j]], 0 };
            uint16_t fg, bg;
            // A white-hot core that decays INTO the hue, rather than the head
            // simply being the hue. The leading character is the brightest
            // thing on screen and the colour trails behind it. On a surge the
            // white runs three cells deep instead of one.
            const int hot = surge ? 3 : 1;
            if (j < hot) {
                uint16_t a = (uint16_t)(((uint32_t)deep * edge) / 255u);
                // Heads blank across the retrace gap too -- a bar that only
                // suppressed the trails would leave the heads hanging in
                // the dark, which reads as a bug rather than a sweep.
                if (rd >= -5 && rd < 0) a = (uint16_t)(a / 7u);
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
                // Retrace gap: five rows immediately ahead of the bar are
                // knocked down to near nothing. This is what the bright
                // edge is read against.
                if (rd >= -5 && rd < 0) a = (a * 22u) / 255u;
                uint16_t lift = boltAmt;
                if (rd >= 0 && rd < 18) {
                    // Hard core for three rows, then a short decay -- short
                    // on purpose, since a long tail is exactly the haze the
                    // first version turned into.
                    const uint16_t rl = (rd < 3) ? 225u
                                                 : (uint16_t)(150u - (rd - 3) * 10u);
                    if (rl > lift) lift = rl;
                }
                fg = lift ? blend(blend(BG, hue, (uint16_t)a), WHITE, lift)
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

// The catchable eye in the Starfield junk field. Defined next to
// backgroundTap(), which is what consumes them, and declared here for the
// same reason ditherRGB is: drawStarfield sits earlier in the file.
//
// An eye only becomes a target once it has grown past this radius. Below it
// the thing is a speck at the vanishing point that nobody could deliberately
// hit, so it is neither tappable nor counted as one that got away. Half of
// drawStarfield's 52px size cap.
static const int EYE_CATCH_MIN_R = 26;

// Knock on the lodge door: five taps on the SNOWFALL lodge, same count and
// same window as the moon. Declared here because drawSnowfall sits earlier in
// the file than backgroundTap(), which is what consumes them.
static void publishLodge(int cx, int ridgeY, uint32_t now);
uint8_t lodgeKnocks();
static void publishBigEye(int cx, int cy, int r, int8_t slot, uint32_t now);
static void bigEyeGone(int8_t slot);
static void drawEyeCatchFx(TFT_eSPI& t, uint32_t now);

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
        if (jz[i] < 6.0f) { jlive[i] = false; bigEyeGone((int8_t)i); continue; }
        const float k = 110.0f / jz[i];
        const int   x = cx + (int)(jx[i] * k);
        const int   y = cy + (int)(jy[i] * k);
        int s = (int)(60.0f * k);
        if (s < 2)  s = 2;
        if (s > 52) s = 52;
        if (x + s < 0 || x - s >= w || y + s < yStart || y - s >= yEnd) {
            // Gone past an edge: retire it now so the next arrival can
            // use the slot, rather than tracking an invisible object.
            if (jz[i] < 120.0f) { jlive[i] = false; bigEyeGone((int8_t)i); }
            continue;
        }
        // Kind 0 is the eyeball, and it is catchable -- but only once it is
        // close enough to be a fair target. A two-pixel speck at the
        // vanishing point is not something anyone could deliberately hit, so
        // it is neither tappable nor counted as one that got away.
        if (jkind[i] == 0 && s >= EYE_CATCH_MIN_R) publishBigEye(x, y, s, (int8_t)i, now);
        drawJunk(t, jkind[i], x, y, s, now);
    }
    drawEyeCatchFx(t, now);

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

// The lil guy, walking the same line the Mowin' Man does. He was drawn for a
// background that got shelved, and this is the whole of him that survived --
// eight frames of a walk cycle in flash (see lil_guy.h) and a cameo once every
// five minutes.
//
// Two device pixels per art pixel, drawn as fillRect: at 10x10 art that is at
// most a hundred small fills, and only while he is actually on screen. The
// frame comes off the clock rather than off a frame counter, because 80 ms is
// slower than this board's own frame time and a counter would run him at
// whatever speed the rest of the scene happened to be managing.
static void drawLilGuyAt(TFT_eSPI& t, int x, int baseY, uint32_t now) {
    static const uint16_t PAL[4] = { 0, 0, 0, 0 };
    (void)PAL;
    const uint16_t hair = t.color565(0, 255, 245);
    const uint16_t skin = t.color565(255, 208, 240);
    const uint16_t body = t.color565(185, 103, 255);
    const uint8_t  f    = (uint8_t)((now / 80u) % LILGUY_FRAMES);
    const int      top  = baseY - LILGUY_H * 2;
    for (uint8_t y = 0; y < LILGUY_H; y++) {
        const uint32_t row = LILGUY[f * LILGUY_H + y];
        for (uint8_t xx = 0; xx < LILGUY_W; xx++) {
            const uint8_t c = (uint8_t)((row >> (xx * 2)) & 3u);
            if (!c) continue;
            t.fillRect(x + xx * 2, top + y * 2, 2, 2,
                       (c == 1) ? hair : (c == 2) ? skin : body);
        }
    }
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
        // Tell him it is coming. The reverse of the lastFootprint()
        // call this file already makes to find out where he is
        // standing -- he decides for himself whether it is close enough
        // to duck, and rate-limits his own reaction, so firing this for
        // every toaster on every frame costs a couple of compares.
        Squachy::toasterNear((int)tx[i], (int)ty[i]);
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
        // Stand him on the background floor when the screen has told us
        // where that is. At yEnd he mowed along the very bottom of the
        // band, which is where the detection counters are drawn on top of
        // him -- so he was working underneath the numbers.
        const int gBase = (s_bgFloor > yStart && s_bgFloor <= yEnd)
                          ? s_bgFloor - 1 : yEnd - 1;
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

    // ---- the lil guy -----------------------------------------------------
    // Same ground line as the mower, on his own five-minute clock, and
    // deliberately not synchronised with him: they can overlap, and the one
    // time they do is worth more than either of them alone.
    //
    // 1 art pixel per animation frame is the classic walk speed, which at two
    // device pixels an art pixel and 80 ms a frame is 25 px a second -- about
    // fourteen seconds to cross. He is off screen for the other four and
    // three quarter minutes.
    {
        static float    lgX = -40.0f;
        static bool     lgLive = false;
        static uint32_t lgNext = 0;
        static uint32_t lgLast = 0;
        if (!lgNext) lgNext = now + 20000u;          // first one soon after boot
        if (!lgLive && now >= lgNext) {
            lgLive = true;
            lgX = -(float)(LILGUY_W * 2) - 4.0f;
            lgLast = now;
        }
        if (lgLive) {
            uint32_t ld = now - lgLast;
            if (ld > 200u) ld = 200u;
            lgLast = now;
            lgX += 0.025f * (float)ld;               // 25 px a second
            const int gBase2 = (s_bgFloor > yStart && s_bgFloor <= yEnd)
                               ? s_bgFloor - 1 : yEnd - 1;
            drawLilGuyAt(t, (int)lgX, gBase2, now);
            if (lgX > (float)(w + 8)) {
                lgLive = false;
                lgNext = now + 300000u;              // once every five minutes
            }
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

// ---- FIREFLIES ---------------------------------------------------------
// A meadow at dusk, and the fireflies over it at three depths.
//
// This used to be forty single pixels on a flat black fill. It is a
// scene now, and the scene exists to give the light something to land
// on: far fireflies are sharp single points over the far field, mid
// ones are a small cross, and the near ones are soft blooms that light
// the grass beneath them. Sharpness is the distance cue.
//
// Everything soft here works BECAUSE of the palette rather than despite
// it. A firefly is yellow-green -- red and green -- and those are the
// two channels with eight levels on this panel. Every glow, halo and
// fog band that had to be fought through blue's four steps on the
// SNOWFALL sky gets a smooth falloff here for free.
static const uint8_t FF_FAR = 20, FF_MID = 14, FF_NEAR = 4;
// Each firefly has a home point it wanders around and a flight pattern
// that says how. They used to share one behaviour -- a drift and a
// bounce -- and forty of the same thing is a particle system, not a
// field of insects. Six patterns, assigned by hash, each with its own
// pulse period, is what makes them read as individuals.
struct Fly {
    float   x, y;         // where it is drawn
    float   hx, hy;       // the home point the pattern orbits
    float   vx;           // drift of the home point
    float   dx, dy;       // the darter's current dash
    float   ph, pk;       // phase, and a per-fly pulse period multiplier
    uint8_t tone, pat;
    bool    rolled;
};
static Fly      s_ffFar[FF_FAR], s_ffMid[FF_MID], s_ffNear[FF_NEAR];
static bool     s_ffInit = false;
static uint32_t s_ffLastMs = 0;
// Events, each on its own clock. 0 in a *Start means not running.
static uint32_t s_ffWaveAt = 0,  s_ffWaveStart = 0;   // the sync ripple
static uint32_t s_ffGustAt = 0,  s_ffGustStart = 0;   // wind through the grass
static uint32_t s_ffEyesAt = 0,  s_ffEyesStart = 0;   // something in the treeline
static uint32_t s_ffLantAt = 0,  s_ffLantStart = 0;   // one passes close to the lens
static int      s_ffEyesX = 0;
static float    s_ffLantY = 0.0f;

static void ffSpawn(Fly& f, int w, int y0, int y1, float spd) {
    f.x  = f.hx = (float)random(0, w);
    f.y  = f.hy = (float)random(y0, y1);
    f.vx = (float)random(-100, 101) / 100.0f * spd;
    f.dx = 0.0f; f.dy = 0.0f;
    f.ph = (float)random(0, 6283) / 1000.0f;
    f.pk = 0.75f + (float)random(0, 66) / 100.0f;
    f.tone = (uint8_t)random(0, 3);
    f.pat  = (uint8_t)random(0, 6);
    f.rolled = false;
}
// The six patterns. All of them move the HOME point slowly and put the
// drawn position somewhere relative to it, so the wander is bounded
// and the wrap is clean.
//   0 hover    figure-eight around a creeping home
//   1 cruiser  steady drift with a slow rise and fall
//   2 bobber   mostly vertical, nine pixels of bob
//   3 spiral   circles a centre that slides along -- a loose helix
//   4 darter   holds still, dashes, holds again
//   5 J-flash  the real Photinus signature: rises in a hook while lit
static void ffStep(Fly& f, int w, int y0, int y1, float ds, float wind, uint32_t now) {
    const float t = (float)now / 1000.0f;
    switch (f.pat) {
        case 0:
            f.hx += f.vx * 0.25f * ds;
            f.x = f.hx + sinf(t * 0.9f + f.ph) * 8.0f;
            f.y = f.hy + sinf(t * 1.8f + f.ph) * 4.0f;
            break;
        case 1:
            f.hx += f.vx * 1.2f * ds;
            f.x = f.hx;
            f.y = f.hy + sinf(t * 0.7f + f.ph) * 5.0f;
            break;
        case 2:
            f.hx += f.vx * 0.3f * ds;
            f.x = f.hx;
            f.y = f.hy + sinf(t * 2.2f + f.ph) * 9.0f;
            break;
        case 3:
            f.hx += f.vx * 0.5f * ds;
            f.x = f.hx + cosf(t * 1.6f + f.ph) * 10.0f;
            f.y = f.hy + sinf(t * 1.6f + f.ph) * 6.0f;
            break;
        case 4: {
            const float c = fmodf(t * 0.7f + f.ph, 1.0f);
            if (c < 0.22f) { f.hx += f.dx * ds * 2.2f; f.hy += f.dy * ds * 2.2f; }
            else if (c > 0.97f && !f.rolled) {
                f.dx = (float)random(-50, 51) / 60.0f;
                f.dy = (float)random(-50, 51) / 90.0f;
                f.rolled = true;
            }
            if (c < 0.9f) f.rolled = false;
            f.x = f.hx; f.y = f.hy;
            break;
        }
        default: {
            f.hx += f.vx * 0.5f * ds;
            const float pu = 0.5f + 0.5f * sinf((float)now / (700.0f * f.pk) + f.ph * 3.0f);
            f.x = f.hx + pu * 3.0f;
            f.y = f.hy - pu * 10.0f;
            break;
        }
    }
    // The wind nudges them at a quarter of what it did. At full strength
    // a gust swept every firefly off the screen, which is not what wind
    // does to an insect that weighs nothing and is flying on purpose --
    // the grass shows the wind; the fireflies mostly ignore it.
    f.hx += wind * 0.25f * ds;
    if (f.hx < -4.0f)           f.hx = (float)w + 3.0f;
    if (f.hx > (float)w + 4.0f) f.hx = -3.0f;
    if (f.hy < (float)y0 + 4.0f) f.hy = (float)y0 + 4.0f;
    if (f.hy > (float)y1 - 4.0f) f.hy = (float)y1 - 4.0f;
}
static inline uint16_t ffTone(TFT_eSPI& t, uint8_t k) {
    return (k == 0) ? t.color565(219, 255, 73)
         : (k == 1) ? t.color565(255, 219, 36)
                    : t.color565(146, 255, 109);
}
static inline float ffPulse(const Fly& f, uint32_t now, float per) {
    // per is the depth's base period; pk spreads it per fly so no two
    // in the same plane ever blink in step for long.
    return 0.5f + 0.5f * sinf((float)now / (per * f.pk) + f.ph * 3.0f);
}
// The bloom. Two spans per row -- a wide dim one and a narrow bright one
// over it -- with the falloff dithered per row so the edge is a fade and
// not a ring. Radial, not just vertical: the outer span narrows with the
// circle and the inner one is half of it.
static void ffGlow(TFT_eSPI& t, int x, int y, int r, uint16_t col, uint16_t bg,
                   int str, int y0, int y1) {
    static const int8_t GD[4] = { -7, 3, 7, -3 };
    for (int dy = -r; dy <= r; dy++) {
        const int yy = y + dy;
        if (yy < y0 || yy >= y1) continue;
        const int half = (int)sqrtf((float)(r * r - dy * dy));
        if (half < 1) continue;
        const float fr = 1.0f - fabsf((float)dy) / (float)(r + 1);
        int ao = (int)(fr * fr * (float)str * 0.42f) + GD[yy & 3];
        int ai = (int)(fr * fr * (float)str)         + GD[yy & 3];
        if (ao > 255) ao = 255;
        if (ai > 255) ai = 255;
        if (ao > 3) t.drawFastHLine(x - half, yy, half * 2 + 1, blend(bg, col, (uint16_t)ao));
        const int ih = half / 2;
        if (ai > 3) t.drawFastHLine(x - ih, yy, ih * 2 + 1, blend(bg, col, (uint16_t)ai));
    }
}

void drawFireflies(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    const int w = t.width();
    // Same floor rule as SNOWFALL: stop a pixel short of the counters,
    // which are plain text with no outline.
    const int yBot  = ((s_bgFloor > yStart + 40 && s_bgFloor <= yEnd) ? s_bgFloor : yEnd) - 1;
    const int bandH = yBot - yStart;
    if (bandH < 40) return;
    const int horizon  = yStart + (bandH * 58) / 100;   // treeline stands here
    const int grassTop = yStart + (bandH * 78) / 100;   // near grass starts here

    if (!s_ffInit) {
        for (uint8_t i = 0; i < FF_FAR;  i++) ffSpawn(s_ffFar[i],  w, horizon + 2, grassTop - 2, 0.10f);
        for (uint8_t i = 0; i < FF_MID;  i++) ffSpawn(s_ffMid[i],  w, horizon - 14, yBot - 8, 0.16f);
        for (uint8_t i = 0; i < FF_NEAR; i++) ffSpawn(s_ffNear[i], w, horizon + 4, yBot - 6, 0.22f);
        s_ffLastMs = now;
        s_ffWaveAt = now + (uint32_t)random(9000, 16000);
        s_ffGustAt = now + (uint32_t)random(18000, 30000);
        s_ffEyesAt = now + (uint32_t)random(14000, 26000);
        s_ffLantAt = now + (uint32_t)random(20000, 34000);
        s_ffInit = true;
    }
    uint32_t dt = now - s_ffLastMs;
    if (dt > 200u) dt = 200u;
    const bool  step = (now != s_ffLastMs);
    if (step) s_ffLastMs = now;
    const float ds = (float)dt / 16.0f;

    // ---- the slow things ----------------------------------------------
    // Dusk. 1 is the warm horizon just after sunset, 0 is full night;
    // it swings between them over about two and a half minutes, and the
    // stars come out as it goes.
    const float dusk = 0.5f + 0.5f * cosf((float)now / 26000.0f);
    // Wind: a small ambient breeze, and a gust every twenty-odd seconds
    // that ramps up over a second and a half and dies away again.
    float wind = sinf((float)now / 3000.0f) * 0.12f;
    if (step) {
        if (!s_ffGustStart && now >= s_ffGustAt) s_ffGustStart = now;
        if (!s_ffWaveStart && now >= s_ffWaveAt) s_ffWaveStart = now;
        if (!s_ffEyesStart && now >= s_ffEyesAt) { s_ffEyesStart = now; s_ffEyesX = random(24, w - 24); }
        if (!s_ffLantStart && now >= s_ffLantAt) { s_ffLantStart = now; s_ffLantY = (float)random(grassTop - 10, yBot - 14); }
    }
    if (s_ffGustStart) {
        const float gk = (float)(now - s_ffGustStart) / 3500.0f;
        if (gk >= 1.0f) { s_ffGustStart = 0; s_ffGustAt = now + (uint32_t)random(18000, 34000); }
        else wind += sinf(gk * 3.14159f) * 1.0f;
    }
    float waveT = -1.0f;
    if (s_ffWaveStart) {
        waveT = (float)(now - s_ffWaveStart) / 2400.0f;
        if (waveT > 1.15f) { s_ffWaveStart = 0; s_ffWaveAt = now + (uint32_t)random(11000, 20000); waveT = -1.0f; }
    }

    if (step) {
        for (uint8_t i = 0; i < FF_FAR;  i++) ffStep(s_ffFar[i],  w, horizon + 2,  grassTop - 2, ds * 0.5f, wind * 0.4f, now);
        for (uint8_t i = 0; i < FF_MID;  i++) ffStep(s_ffMid[i],  w, horizon - 14, yBot - 8,     ds,        wind,        now);
        for (uint8_t i = 0; i < FF_NEAR; i++) ffStep(s_ffNear[i], w, horizon + 4,  yBot - 6,     ds * 1.5f, wind * 1.4f, now);
    }

    // ---- sky ------------------------------------------------------------
    // Blue kept over 42 at both ends so it lands on 85 rather than 0 --
    // the same trap the SNOWFALL sky fell into first. The horizon swings
    // from a warm dusk purple to a cooler night blue with `dusk`.
    const uint16_t skyTop = t.color565(8, 10, 96);
    const uint16_t skyHor = blend(t.color565(40, 48, 140), t.color565(128, 44, 96),
                                  (uint16_t)(dusk * 255.0f));
    static const int8_t SKY_DITH[4] = { -9, 4, 9, -4 };
    const int skyH = horizon - yStart;
    for (int y = yStart; y < horizon; y++) {
        int k = ((y - yStart) * 255) / (skyH > 1 ? skyH - 1 : 1) + SKY_DITH[(y - yStart) & 3];
        if (k < 0) k = 0;
        if (k > 255) k = 255;
        t.drawFastHLine(0, y, w, blend(skyTop, skyHor, (uint16_t)k));
    }
    auto skyAt = [&](int y) -> uint16_t {
        int k = ((y - yStart) * 255) / (skyH > 1 ? skyH - 1 : 1);
        if (k < 0) k = 0;
        if (k > 255) k = 255;
        return blend(skyTop, skyHor, (uint16_t)k);
    };

    // Stars come out as the dusk fades. Twinkling on individual periods.
    const float starVis = (1.0f - dusk) * (1.0f - dusk);
    if (starVis > 0.04f) {
        for (uint8_t i = 0; i < 26; i++) {
            uint32_t h = (uint32_t)(i + 5) * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            const int sx = (int)(h % (uint32_t)w);
            const int sy = yStart + 2 + (int)((h >> 11) % (uint32_t)(skyH > 24 ? skyH - 20 : 4));
            const float tw = 0.5f + 0.5f * sinf((float)now / (700.0f + (float)(h % 800u)) + (float)i);
            t.drawPixel(sx, sy, blend(skyAt(sy), WHITE, (uint16_t)(starVis * (70.0f + tw * 150.0f))));
        }
    }

    // The moon, low and to the right, with a halo. The halo is per-row
    // -- sampled against the sky AT THAT ROW -- which is the thing the
    // SNOWFALL moon never got because its three attempts all sampled the
    // sky once. And it pushes red and green rather than blue, so the
    // falloff has eight steps to work with instead of four.
    {
        const int mx = (w * 79) / 100, my = yStart + (skyH * 22) / 100;
        const int hr = 13;
        for (int dy = -hr; dy <= hr; dy++) {
            const int yy = my + dy;
            if (yy < yStart || yy >= horizon) continue;
            const int half = (int)sqrtf((float)(hr * hr - dy * dy));
            const float fr = 1.0f - fabsf((float)dy) / (float)(hr + 1);
            int a = (int)(fr * fr * 70.0f) + SKY_DITH[yy & 3];
            if (a <= 2) continue;
            const uint16_t sk = skyAt(yy);
            const uint16_t warm = blend(sk, t.color565(255, 240, 200), (uint16_t)a);
            t.drawFastHLine(mx - half, yy, half * 2 + 1, warm);
        }
        t.fillCircle(mx, my, 5, t.color565(236, 236, 210));
        t.fillCircle(mx - 2, my - 1, 2, t.color565(200, 200, 176));
        t.drawCircle(mx, my, 5, t.color565(219, 219, 182));
    }

    // ---- treeline -----------------------------------------------------
    // Ragged black silhouette: a base row of wide blocks and a spike
    // above every other one. Pure silhouette -- no detail survives this
    // dark anyway, and the uneven top edge is all that reads.
    {
        const uint16_t tree = t.color565(3, 8, 10);
        for (int x = 0; x < w; x += 8) {
            uint32_t h = (uint32_t)(x / 8 + 40) * 2654435761u; h ^= h >> 13;
            const int hh = 6 + (int)(h % 14u);
            t.fillRect(x, horizon - hh, 8, hh + 2, tree);
            if ((h & 2u) != 0u) t.fillRect(x + 2 + (int)(h % 3u), horizon - hh - 5, 3, 6, tree);
        }
    }

    // Something in the trees. Two green eyes, for three seconds, with
    // one blink. Nothing else about it is ever shown.
    if (s_ffEyesStart) {
        const uint32_t ea = now - s_ffEyesStart;
        if (ea > 3200u) { s_ffEyesStart = 0; s_ffEyesAt = now + (uint32_t)random(16000, 30000); }
        else if (!(ea > 1700u && ea < 1850u)) {
            const uint16_t eye = t.color565(146, 255, 109);
            t.fillRect(s_ffEyesX - 4, horizon - 9, 2, 2, eye);
            t.fillRect(s_ffEyesX + 2, horizon - 9, 2, 2, eye);
        }
    }

    // ---- the far field, and the mist on it ----------------------------
    const uint16_t field = t.color565(8, 40, 12);
    t.fillRect(0, horizon, w, grassTop - horizon, field);
    // Two fog bands drifting at different speeds, thicker low down. A
    // firefly inside one stops being a point and becomes a halo.
    const uint16_t fog = t.color565(100, 104, 120);
    auto fogAt = [&](int y) -> float {
        float f = 0.0f;
        for (uint8_t b = 0; b < 2; b++) {
            const float cy = (float)grassTop - 9.0f - (float)b * 9.0f
                           + sinf((float)now / (2600.0f + (float)b * 900.0f)) * 3.0f;
            const float th = 6.0f + (float)b * 2.0f;
            const float d = fabsf((float)y - cy) / th;
            if (d < 1.0f) f += (1.0f - d) * (0.55f - (float)b * 0.2f);
        }
        return f > 1.0f ? 1.0f : f;
    };
    for (int y = horizon + 2; y < grassTop; y++) {
        const float f = fogAt(y);
        if (f < 0.04f) continue;
        int a = (int)(f * 120.0f) + SKY_DITH[y & 3] / 2;
        if (a < 2) continue;
        t.drawFastHLine(0, y, w, blend(field, fog, (uint16_t)a));
    }

    // Far fireflies: single pixels over the far field. In the fog they
    // bloom into small halos instead.
    for (uint8_t i = 0; i < FF_FAR; i++) {
        const Fly& f = s_ffFar[i];
        const float pu = ffPulse(f, now, 1000.0f);
        if (pu < 0.35f) continue;
        const int x = (int)f.x, y = (int)f.y;
        const float fg = fogAt(y);
        if (fg > 0.3f) ffGlow(t, x, y, 3, ffTone(t, f.tone), blend(field, fog, (uint16_t)(fg * 120.0f)),
                              (int)(pu * 150.0f), horizon, grassTop);
        else t.drawPixel(x, y, blend(field, ffTone(t, f.tone), (uint16_t)(pu * 230.0f)));
    }

    // ---- the near grass ------------------------------------------------
    const uint16_t soil  = t.color565(6, 30, 10);
    const uint16_t blade = t.color565(12, 64, 20);
    t.fillRect(0, grassTop, w, yBot - grassTop, soil);

    // Where the near light is, for the grass to read. The lantern counts
    // as a fifth, brighter source while it is passing.
    struct Lamp { int x, y; float s; uint8_t tone; };
    Lamp lamps[FF_NEAR + 1];
    uint8_t nl = 0;
    for (uint8_t i = 0; i < FF_NEAR; i++) {
        const float pu = ffPulse(s_ffNear[i], now, 650.0f);
        if (pu > 0.4f) lamps[nl++] = { (int)s_ffNear[i].x, (int)s_ffNear[i].y, pu, s_ffNear[i].tone };
    }
    float lantK = -1.0f;
    int   lantX = 0;
    if (s_ffLantStart) {
        lantK = (float)(now - s_ffLantStart) / 7000.0f;
        if (lantK >= 1.0f) { s_ffLantStart = 0; s_ffLantAt = now + (uint32_t)random(24000, 40000); lantK = -1.0f; }
        else {
            lantX = (int)(-20.0f + lantK * (float)(w + 40));
            lamps[nl++] = { lantX, (int)s_ffLantY, 1.3f, 1 };
        }
    }

    // Blades, one every four pixels, hashed heights. A gust leans them.
    // Each blade takes its colour from the nearest bright near firefly:
    // that tint, falling off with distance, is the light landing on the
    // grass, and it costs nothing because the blades were being drawn
    // anyway.
    for (int x = 0; x < w; x += 4) {
        uint32_t h = (uint32_t)(x / 4 + 9) * 2654435761u; h ^= h >> 13;
        const int bh = 3 + (int)(h % 7u);
        uint16_t c = blade;
        float best = 0.0f; uint8_t bt = 0;
        for (uint8_t i = 0; i < nl; i++) {
            const float dx = (float)(x - lamps[i].x), dy = (float)(yBot - bh - lamps[i].y);
            const float d = sqrtf(dx * dx + dy * dy) / 30.0f;
            if (d < 1.0f) { const float v = (1.0f - d) * lamps[i].s; if (v > best) { best = v; bt = lamps[i].tone; } }
        }
        if (best > 0.02f) c = blend(blade, ffTone(t, bt), (uint16_t)(best * 190.0f));
        const int lean = (int)(wind * (3.0f + (float)(h % 4u)));
        if (lean) t.drawLine(x, yBot, x + lean, yBot - bh, c);
        else      t.drawFastVLine(x, yBot - bh, bh, c);
    }

    // ---- mid fireflies: a small cross, over the grass ----------------
    for (uint8_t i = 0; i < FF_MID; i++) {
        const Fly& f = s_ffMid[i];
        const float pu = ffPulse(f, now, 800.0f);
        if (pu < 0.3f) continue;
        const int x = (int)f.x, y = (int)f.y;
        const uint16_t bg = (y < horizon) ? skyAt(y) : ((y < grassTop) ? field : soil);
        const uint16_t c = blend(bg, ffTone(t, f.tone), (uint16_t)(pu * 255.0f));
        if (fabsf(wind) > 0.4f) t.drawFastHLine(x - (int)(wind * 10.0f), y, (int)fabsf(wind * 10.0f), blend(bg, c, 110));
        t.drawFastHLine(x - 1, y, 3, c);
        t.drawFastVLine(x, y - 1, 3, c);
    }

    // ---- near fireflies: the blooms -----------------------------------
    for (uint8_t i = 0; i < FF_NEAR; i++) {
        const Fly& f = s_ffNear[i];
        const float pu = ffPulse(f, now, 650.0f);
        if (pu < 0.2f) continue;
        const int x = (int)f.x, y = (int)f.y;
        const uint16_t bg = (y < grassTop) ? field : soil;
        ffGlow(t, x, y, 5, ffTone(t, f.tone), bg, (int)(pu * 255.0f), horizon, yBot);
        t.drawFastHLine(x - 1, y, 3, blend(WHITE, ffTone(t, f.tone), 90));
        t.drawFastVLine(x, y - 1, 3, blend(WHITE, ffTone(t, f.tone), 90));
    }

    // One passes close to the lens now and then: a big soft bloom with a
    // faint horizontal flare across the frame.
    if (lantK >= 0.0f) {
        const float ed = (lantK < 0.15f) ? lantK / 0.15f : ((lantK > 0.85f) ? (1.0f - lantK) / 0.15f : 1.0f);
        const int ly = (int)s_ffLantY;
        const uint16_t lc = ffTone(t, 1);
        const uint16_t bg = (ly < grassTop) ? field : soil;
        t.drawFastHLine(lantX - 44, ly, 88, blend(bg, lc, (uint16_t)(ed * 60.0f)));
        ffGlow(t, lantX, ly, 9, lc, bg, (int)(ed * 255.0f), horizon, yBot);
        t.fillRect(lantX - 1, ly - 1, 3, 3, blend(WHITE, lc, 60));
    }

    // ---- the sync ripple ------------------------------------------------
    // Every so often the whole field flashes together in a travelling
    // wave. It was the one good idea in the old version, so it stays:
    // anything within the band gets a hot white core for a moment.
    if (waveT >= 0.0f) {
        auto hit = [&](const Fly& f, int cross) {
            const float xn = f.x / (float)w;
            if (fabsf(xn - waveT) >= 0.07f) return;
            const int x = (int)f.x, y = (int)f.y;
            if (cross) { t.drawFastHLine(x - 2, y, 5, WHITE); t.drawFastVLine(x, y - 2, 5, WHITE); }
            else t.drawPixel(x, y, WHITE);
        };
        for (uint8_t i = 0; i < FF_FAR;  i++) hit(s_ffFar[i],  0);
        for (uint8_t i = 0; i < FF_MID;  i++) hit(s_ffMid[i],  1);
        for (uint8_t i = 0; i < FF_NEAR; i++) hit(s_ffNear[i], 1);
    }

    if (yEnd > yBot) t.fillRect(0, yBot, w, yEnd - yBot, BG);
}

// ---- tappable background bits -----------------------------------------
// The werewolf's speech bubble is PUBLISHED rather than drawn where it
// is computed. drawFire is the background: ui_clear paints Squachy over
// it, then the idle-event flourishes, then the ALL CLEAR headline -- so
// a bubble drawn inside drawFire ends up underneath the mascot, and for
// text that does not read as depth, it reads as a rendering fault. It is
// the same reason ALL CLEAR is drawn after Squachy rather than before.
//
// Stale for the same reason the moon below is: if the background is
// switched away from FIRE mid-howl, nothing clears these, so the
// overlay checks the timestamp before it trusts them.
static int      s_wolfSayX = -1, s_wolfSayY = -1;
static int      s_wolfSayKind = 0;
static float    s_wolfHowlK = 0.0f;
static int      s_wolfSayW = 0, s_wolfSayTop = 0;
static uint32_t s_wolfSayAt = 0;

// drawFire publishes its moon here every frame it draws one, and
// backgroundTap() below tests against that. The timestamp matters: a
// moon position left over from a background that is no longer on screen
// must not stay tappable, and drawFire is simply not called once the
// user cycles away.
static int      s_moonX = -1, s_moonY = -1, s_moonR = 0;
static uint32_t s_moonAt = 0;

// Five taps, each within MOON_TAP_WINDOW of the one before, summon the
// werewolf. The window is what makes it a deliberate act rather than an
// accumulation: a tap now and a tap five minutes from now should not
// count toward the same thing. It was ten; five is still a drum roll and
// nobody hits it by accident, because the window does that work, not the
// count.
static const uint32_t MOON_TAP_WINDOW = 2500;
static const uint8_t  MOON_TAPS_NEEDED = 5;
static uint8_t  s_moonTaps  = 0;
static uint32_t s_moonTapAt = 0;
static uint32_t s_wolfAt    = 0;      // 0 = no werewolf on stage
static bool     s_wolfSummonPending = false;   // consumed by main.cpp

// Stage timings, all eased into each other. Nothing here pops: that is
// the whole lesson of the tree that used to strobe in this same scene.
static const uint32_t WOLF_EYES = 1300;   // eyes fade up in the dark
static const uint32_t WOLF_BODY = 1100;   // silhouette resolves around them
static const uint32_t WOLF_HOLD = 2600;   // it just stands there
// The howl is now a shaped move rather than a linear ramp: COIL is the
// crouch he gathers on, RISE is the snap up into the note, and whatever
// is left of WOLF_HOWL is the note held. Longer than it was because the
// SKID line no longer overlaps it -- the howl finally has the stage to
// itself and needs room to land, plus its echoes.
static const uint32_t WOLF_COIL = 420;    // crouch and gather
static const uint32_t WOLF_RISE = 380;    // snap up into the note
static const uint32_t WOLF_HOWL = 2400;   // head goes back
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

static char     s_toastHead[18] = {0};
static char     s_toastSub[22]  = {0};
static uint16_t s_toastAccent   = 0;
static uint32_t s_toastUntil    = 0;

void showToast(const char* head, const char* sub, uint16_t accent) {
    strncpy(s_toastHead, head ? head : "", sizeof(s_toastHead) - 1);
    s_toastHead[sizeof(s_toastHead) - 1] = 0;
    strncpy(s_toastSub, sub ? sub : "", sizeof(s_toastSub) - 1);
    s_toastSub[sizeof(s_toastSub) - 1] = 0;
    s_toastAccent = accent;
    s_toastUntil  = millis() + 1500u;
}

void drawToast(TFT_eSPI& t, uint32_t now) {
    if (!s_toastUntil) return;
    if ((int32_t)(now - s_toastUntil) >= 0) { s_toastUntil = 0; return; }

    const int w = t.width(), h = t.height();
    t.setTextSize(2);
    int bw = t.textWidth(s_toastHead) + 30;
    if (s_toastSub[0]) {
        t.setTextSize(1);
        const int sw = t.textWidth(s_toastSub) + 30;
        if (sw > bw) bw = sw;
    }
    if (bw > w - 20) bw = w - 20;
    const int bh = s_toastSub[0] ? 48 : 34;
    const int bx = (w - bw) / 2, by = (h - bh) / 2;

    t.fillRect(bx, by, bw, bh, BG);
    t.drawRect(bx, by, bw, bh, s_toastAccent);
    t.drawRect(bx + 1, by + 1, bw - 2, bh - 2, blend(s_toastAccent, BG, 160));

    t.setTextSize(2);
    t.setTextColor(s_toastAccent, BG);
    t.setCursor(bx + (bw - t.textWidth(s_toastHead)) / 2, by + 8);
    t.print(s_toastHead);
    if (s_toastSub[0]) {
        t.setTextSize(1);
        t.setTextColor(WHITE, BG);
        t.setCursor(bx + (bw - t.textWidth(s_toastSub)) / 2, by + 31);
        t.print(s_toastSub);
    }
}

void setBackgroundFloor(int y)  { s_bgFloor = y; }
void clearBackgroundFloor()     { s_bgFloor = -1; }

// Cost of the last background draw, exponentially smoothed. Measured
// HERE rather than in loop(), because loop()'s own frame average is
// whatever screen you are currently looking at -- and DIAGNOSTICS,
// which is where you read the number, draws no background at all. Its
// FRAME figure was therefore timing the diagnostics screen and saying
// nothing about the animation it was being consulted about. This one
// holds the last value from a screen that actually drew a backdrop, so
// it survives the walk over to go and read it.
static uint32_t s_bgUsAvg = 0;
uint32_t backgroundUs() { return s_bgUsAvg; }

// Single place that maps the Settings background choice onto a
// renderer. Lifted out of uiClearTick(), which owned it while CLEAR was
// the only screen with a live backdrop.
void drawActiveBackground(TFT_eSPI& t, uint32_t now, int yStart, int yEnd,
                          const DetectionEngine& eng, bool advance) {
    const uint32_t bgT0 = micros();
    switch (Settings::background()) {
        case Settings::Background::STARFIELD:  drawStarfield(t, now, yStart, yEnd); break;
        case Settings::Background::TOASTERS:   drawFlyingToasters(t, now, yStart, yEnd); break;
        case Settings::Background::AQUARIUM:   drawAquarium(t, now, yStart, yEnd); break;
        case Settings::Background::TERMINAL:   drawTerminalLog(t, now, yStart, yEnd); break;
        case Settings::Background::FIREFLIES:  drawFireflies(t, now, yStart, yEnd); break;
        case Settings::Background::FIRE:       drawFire(t, now, yStart, yEnd); break;
        case Settings::Background::SNOWFALL:   drawSnowfall(t, now, yStart, yEnd); break;
        case Settings::Background::SPECTRUM:   drawGibson(t, now, yStart, yEnd, eng); break;
        case Settings::Background::TUNNEL:     drawWireframeTunnel(t, now, yStart, yEnd); break;
        case Settings::Background::SYNTHWAVE:  drawSynthwave(t, now, yStart, yEnd); break;
        default:                               drawDigitalRain(t, now, yStart, yEnd, advance); break;
    }
    const uint32_t bgDt = micros() - bgT0;
    s_bgUsAvg = s_bgUsAvg ? s_bgUsAvg + ((int32_t)bgDt - (int32_t)s_bgUsAvg) / 8 : bgDt;
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

// ---- the Starfield eye ---------------------------------------------------
// Catch two in a row and the VOID EYE costume is yours. "In a row" is the
// whole mechanic: without a reset it quietly degrades into "two eyes ever",
// which is not the same game at all. An eye only counts -- as a catch or as
// a miss -- once it has grown past EYE_CATCH_MIN_R, because below that it was
// never a target anyone could have hit.
//
// One eye is tracked for hit-testing but the bookkeeping is per slot: the
// junk field can hold two pieces at once, and a second eyeball arriving must
// not cancel the first one's miss.
static const uint8_t EYE_PAIR_NEEDED = 2;

static int      s_eyeX = -1, s_eyeY = -1, s_eyeR = 0;
static uint32_t s_eyeAt = 0;
static int8_t   s_eyeSlot = -1;
static bool     s_eyeBig[2] = { false, false };   // grew past the threshold
static bool     s_eyeGot[2] = { false, false };   // and was caught before it left
static uint8_t  s_eyeStreak = 0;
static bool     s_eyePairPending = false;         // consumed by main.cpp

// The tell. Catching the first of the pair has to be visible or the second
// one is a coin flip: a ring snaps out from where it was, which costs three
// circle outlines for a third of a second.
static int      s_eyeFxX = 0, s_eyeFxY = 0;
static uint32_t s_eyeFxAt = 0;
static const uint32_t EYE_FX_MS = 340;

static void publishBigEye(int cx, int cy, int r, int8_t slot, uint32_t now) {
    if (slot < 0 || slot > 1) return;
    s_eyeBig[slot] = true;
    if (s_eyeGot[slot]) return;             // already caught; stop offering it
    s_eyeX = cx; s_eyeY = cy; s_eyeR = r; s_eyeSlot = slot; s_eyeAt = now;
}

static void bigEyeGone(int8_t slot) {
    if (slot < 0 || slot > 1) return;
    // It got away. Only a big one resets the streak -- see above.
    if (s_eyeBig[slot] && !s_eyeGot[slot]) s_eyeStreak = 0;
    s_eyeBig[slot] = false;
    s_eyeGot[slot] = false;
    if (s_eyeSlot == slot) s_eyeSlot = -1;
}

static void drawEyeCatchFx(TFT_eSPI& t, uint32_t now) {
    if (!s_eyeFxAt || (now - s_eyeFxAt) > EYE_FX_MS) return;
    const uint32_t age = now - s_eyeFxAt;
    const int  base = 14 + (int)((age * 44) / EYE_FX_MS);
    const uint8_t fade = (uint8_t)(255 - (age * 255) / EYE_FX_MS);
    const uint16_t col = blend(BG, VAPOR_PURPLE, fade);
    t.drawCircle(s_eyeFxX, s_eyeFxY, base, col);
    t.drawCircle(s_eyeFxX, s_eyeFxY, base + 3, blend(BG, WHITE, fade));
}

// ---- the lodge -----------------------------------------------------------
// The lodge is 40 wide and 27 tall, sitting ON the ridge line, and it drifts
// across at about 4.7 px a second on the far plane -- roughly a minute and a
// half on screen, then off for anywhere from ten seconds to three minutes.
// That rhythm is the whole reason it works as a target: generous while it is
// there, and impossible to stumble into while it is not.
static const uint8_t LODGE_KNOCKS_NEEDED = 5;
static int      s_lodgeHitX = -1, s_lodgeHitY = 0;
static uint32_t s_lodgeAt = 0;
static uint8_t  s_lodgeKnocks = 0;
static uint32_t s_lodgeKnockAt = 0;
static bool     s_lodgePending = false;

static void publishLodge(int cx, int ridgeY, uint32_t now) {
    s_lodgeHitX = cx; s_lodgeHitY = ridgeY; s_lodgeAt = now;
}

// How many windows should be lit deliberately rather than on their own
// cycle. snowLodge() reads this so each knock lights another one: the tell
// the Starfield eye taught us a multi-step trigger cannot do without.
uint8_t lodgeKnocks() { return s_lodgeKnocks; }

bool consumeLodgeKnock() {
    if (!s_lodgePending) return false;
    s_lodgePending = false;
    return true;
}

bool consumeEyeCatch() {
    if (!s_eyePairPending) return false;
    s_eyePairPending = false;
    return true;
}

bool backgroundTap(int x, int y, uint32_t now) {
    // The Starfield eye, while it is close. Circle hit test on the sphere it
    // actually draws -- no generous margin, the way the gold toaster's box
    // was tightened: this one is a big target already, and the whole point of
    // the egg is that it cannot be stumbled into.
    if (s_eyeSlot >= 0 && (now - s_eyeAt) <= 250) {
        const int edx = x - s_eyeX, edy = y - s_eyeY;
        if (edx * edx + edy * edy <= s_eyeR * s_eyeR) {
            s_eyeGot[s_eyeSlot] = true;
            s_eyeFxX = s_eyeX; s_eyeFxY = s_eyeY;
            s_eyeFxAt = now ? now : 1;
            s_eyeSlot = -1;                      // caught: stop accepting taps
            if (++s_eyeStreak >= EYE_PAIR_NEEDED) {
                s_eyeStreak = 0;
                s_eyePairPending = true;
            }
            return true;
        }
    }

    // Five knocks on the lodge door. Box, not circle: it is a building.
    // The window between taps is the moon's, because it is the same idea and
    // it should feel the same in the hand.
    if (s_lodgeHitX >= 0 && (now - s_lodgeAt) <= 250) {
        const int ldx = x - s_lodgeHitX, ldy = y - s_lodgeHitY;
        if (ldx >= -22 && ldx <= 22 && ldy >= -30 && ldy <= 4) {
            if ((now - s_lodgeKnockAt) > MOON_TAP_WINDOW) s_lodgeKnocks = 0;
            s_lodgeKnockAt = now;
            if (++s_lodgeKnocks >= LODGE_KNOCKS_NEEDED) {
                s_lodgeKnocks = 0;
                s_lodgePending = true;
            }
            return true;
        }
    }

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
    // The howl drives the fire. Computed up here rather than down in the
    // werewolf block because the seed row is written before that runs,
    // and a surge has to go into the FUEL -- painting brighter flame on
    // top would just be a tint, where feeding the seed lets the heat
    // propagate up through the sim on its own and settle afterwards.
    float wolfSurge = 0.0f;
    if (s_wolfAt) {
        const uint32_t we = now - s_wolfAt;
        const uint32_t hs = WOLF_EYES + WOLF_BODY + WOLF_HOLD + WOLF_COIL;
        const uint32_t he = WOLF_EYES + WOLF_BODY + WOLF_HOLD + WOLF_HOWL;
        if (we > hs && we < he) {
            const uint32_t hh = we - hs;
            wolfSurge = (hh < WOLF_RISE) ? (float)hh / (float)WOLF_RISE : 1.0f;
            const uint32_t tail = WOLF_HOWL - WOLF_COIL;
            if (hh > tail - 600u) wolfSurge *= 1.0f - (float)(hh - (tail - 600u)) / 600.0f;
            if (wolfSurge < 0.0f) wolfSurge = 0.0f;
        }
    }

    float litSum = 0.0f;
    for (int x = 0; x < fw; x++) {
        float v = acc[x];
        if (v > 1.25f) v = 1.25f;
        // 0.25, not 0.6. At 0.6 the surge roughly doubled the flame
        // height and washed straight up over the counter rows -- the
        // howl has to lift the fire, not set the whole screen on fire.
        float bf = 48.0f * (v / 1.25f) * (1.0f + wolfSurge * 0.25f);
        if (bf > (float)HEAT_MAX) bf = (float)HEAT_MAX;
        uint8_t base = (uint8_t)bf;
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
    // 1 = the SKID line during the hold, 2 = the howl. They no longer
    // overlap: his one spoken beat and his one physical beat each get a
    // moment instead of landing on top of each other.
    int   wolfSayKind = 0;
    float wolfHowlK   = 0.0f;

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
            float eyeF = 1.0f, bodyF = 1.0f, howl = 0.0f, coil = 0.0f;
            if (e < WOLF_EYES) {
                eyeF  = (float)e / (float)WOLF_EYES;
                bodyF = 0.0f;
            } else if (e < WOLF_EYES + WOLF_BODY) {
                bodyF = (float)(e - WOLF_EYES) / (float)WOLF_BODY;
            } else if (e >= WOLF_EYES + WOLF_BODY + WOLF_HOLD) {
                // Coil, then snap up, then hold the note. Anticipation is
                // what makes the rise read as force rather than as a
                // position change, and it costs one extra float.
                const uint32_t h = e - (WOLF_EYES + WOLF_BODY + WOLF_HOLD);
                if (h < WOLF_COIL) {
                    coil = (float)h / (float)WOLF_COIL;
                } else if (h < WOLF_COIL + WOLF_RISE) {
                    const float k = (float)(h - WOLF_COIL) / (float)WOLF_RISE;
                    coil = 1.0f - k;
                    howl = k;
                } else {
                    howl = 1.0f;
                }
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
            // Head goes back three times as far as it used to, and the
            // coil pulls it DOWN first. rise lifts the whole animal --
            // legs and neck stretch rather than the drawing translating.
            const int lift = (int)(howl * 14.0f) - (int)(coil * 5.0f);
            const int rise = (int)(howl * 6.0f)  - (int)(coil * 4.0f);

            // Everything below is authored at the original 1x offsets
            // and multiplied through Z, so the proportions stay locked
            // and resizing the creature is one number rather than forty.
            // At 1.6x the silhouette runs about 67px wide and 76 tall,
            // which still clears the mascot on the left (he ends around
            // x=208) and the right edge of a 320px panel.
            auto Z = [](int v) { return (v * 8) / 5; };

            const int cx  = wx;
            const int bob = breathe;
            const int hy  = gy - Z(48) + bob - lift - rise;   // top of the skull

            // Speaks from the moment the body has fully resolved right
            // through the howl, so the line is up while it rears back
            // and throws its head -- the animation is the delivery. Only
            // the fades are excluded, where the text would still be
            // perfectly legible while the speaker was not.
            // The line lands during the hold and is gone before he moves;
            // the howl then has the stage. Previously the SKID text was up
            // through the whole howl, so neither beat read.
            if (e >= WOLF_EYES + WOLF_BODY &&
                e <  WOLF_EYES + WOLF_BODY + WOLF_HOLD) {
                wolfSayX = cx;
                wolfSayY = hy - Z(15);
                wolfSayKind = 1;
            } else if (howl > 0.25f) {
                wolfSayX = cx;
                wolfSayY = hy - Z(20);
                wolfSayKind = 2;
                wolfHowlK = (howl - 0.25f) / 0.75f;
            }

            if (bodyF > 0.02f) {
                // Legs, planted wide, and heavy dark feet.
                // Legs stretch as he rears rather than sliding upward, so
                // his feet stay planted where they were.
                t.fillRect(cx - Z(10), gy - Z(19) - rise, Z(7),  Z(16) + rise, body);
                t.fillRect(cx + Z(4),  gy - Z(19) - rise, Z(7),  Z(16) + rise, body);
                t.fillRect(cx - Z(13), gy - Z(4),  Z(11), Z(4),  pelt);
                t.fillRect(cx + Z(3),  gy - Z(4),  Z(11), Z(4),  pelt);

                // Torso with a lighter chest panel -- the two-tone is
                // most of what stops this reading as one dark blob.
                t.fillRect(cx - Z(11), gy - Z(35) + bob - rise, Z(23), Z(17), body);
                t.fillRect(cx - Z(5),  gy - Z(34) + bob - rise, Z(11), Z(14), pelt);

                // Hunched shoulders, wider than the chest.
                t.fillRect(cx - Z(15), gy - Z(39) + bob - rise, Z(31), Z(6), body);

                // Arms swing from a fixed shoulder instead of standing as
                // two frozen rectangles. There is no rotation on this
                // display, but drawWideLine takes arbitrary endpoints and
                // costs the same as the fillRect it replaces -- so the
                // coil pulls them in and the note throws them wide, and
                // the hands and claws simply follow wherever the arm ends.
                const int shy = gy - Z(37) + bob - rise;
                const int ahL = cx - Z(17) + (int)(coil * Z(6)) - (int)(howl * Z(9));
                const int ahR = cx + Z(17) - (int)(coil * Z(6)) + (int)(howl * Z(9));
                const int ahy = gy - Z(18) + bob - rise
                                - (int)(coil * Z(4)) - (int)(howl * Z(7));
                t.drawWideLine(cx - Z(16), shy, ahL, ahy, Z(6), body);
                t.drawWideLine(cx + Z(16), shy, ahR, ahy, Z(6), body);
                t.fillRect(ahL - Z(4), ahy - Z(2), Z(8), Z(5), pelt);
                t.fillRect(ahR - Z(4), ahy - Z(2), Z(8), Z(5), pelt);
                for (int k = 0; k < 3; k++) {
                    t.fillRect(ahL - Z(4) + k * Z(3), ahy + Z(3), Z(2), Z(4), claw);
                    t.fillRect(ahR - Z(4) + k * Z(3), ahy + Z(3), Z(2), Z(4), claw);
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
                const int jaw = (int)(howl * 5.0f) * 8 / 5;
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

            // The note itself, as three rings rolling off the muzzle.
            // TFT_eSPI has no arc primitive and a full drawCircle centred
            // here would run straight down through his own body, so each
            // ring is a short run of line segments over a limited angle.
            // Twelve a ring is enough that the joins do not read at this
            // radius, and 36 drawLine calls on an 8-second easter egg is
            // not a number worth optimising.
            if (howl > 0.05f) {
                const int mx = cx, my = hy + Z(9);
                for (int r = 0; r < 3; r++) {
                    float k = howl * 1.6f + (float)r * 0.33f;
                    k -= (float)(int)k;                    // wrap to 0..1
                    const int rad = Z(10) + (int)(k * Z(34));
                    // Full strength at the near edge: these have to read
                    // against flame, which is the brightest thing in the
                    // scene, so anything dimmer than this is simply lost.
                    const uint16_t ring = blend(BG, t.color565(255, 219, 0),
                                                (uint16_t)(255.0f * (1.0f - k * 0.75f) * bodyF));
                    int px = 0, py = 0;
                    for (int i = 0; i <= 12; i++) {
                        // -145 deg to -35 deg: up and outward, never down
                        const float a = -2.53f + (float)i * (1.92f / 12.0f);
                        const int qx = mx + (int)(cosf(a) * (float)rad);
                        const int qy = my + (int)(sinf(a) * (float)rad);
                        if (i) t.drawLine(px, py, qx, qy, ring);
                        px = qx; py = qy;
                    }
                }
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
    // Published, not drawn. See s_wolfSayX's declaration up top: this is
    // the background, and Squachy lands on top of it a few lines later
    // in ui_clear. Theme::drawBackgroundOverlay() puts the bubble down
    // once the mascot and the flourishes are already there.
    s_wolfSayX    = wolfSayX;
    s_wolfSayY    = wolfSayY;
    s_wolfSayKind = wolfSayKind;
    s_wolfHowlK   = wolfHowlK;
    s_wolfSayW    = w;
    s_wolfSayTop  = yStart;
    s_wolfSayAt   = now;

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

// ---- SNOWFALL -------------------------------------------------------
// A night ski hill with a moon over it, riders on skis and boards coming
// down it, and a yeti who is usually not quite fast enough.
//
// PARALLAX. Three planes, and the rule that makes them work is that
// speed, SIZE and HEIGHT all move together. The version before this gave
// every tree the same height and the same ground line but a random
// speed, which is exactly what reads as out of sync -- identical objects
// at identical distance cannot be travelling at different rates. Here a
// slower tree is also smaller and also sits higher up the screen, which
// is what depth actually looks like.
//
// Trees are 2.1x the skier on his own plane. They were the same height
// as him before, which made him a giant.
//
// Everything that stands on the ground stands on s_bgFloor, not yEnd --
// the trap the Gibson calls the Mowin' Man bug.
static const uint8_t SNOW_COLS = 40;
static const uint8_t SNOW_N    = 56;
static const uint8_t TREE_FAR  = 6;
static const uint8_t TREE_ACT  = 5;
static const uint8_t PROP_N    = 4;      // rocks, stumps and jump ramps
static const uint8_t SNOW_MAXD = 24;
static const int     SNOW_RISE = 26;

// One world speed; every plane is a multiple of it. Changing the feel of
// the whole hill is this number and nothing else.
static const float   WORLD_SPD = 0.34f;
static const float   PL_FAR    = 0.22f;
static const float   PL_ACT    = 1.00f;
static const float   PL_NEAR   = 2.40f;

struct SnowFlake { float x, y; uint8_t layer, ph; };
struct SnowObj   { float x; uint8_t a, b; };      // a: height/kind, b: spare

static SnowFlake s_flk[SNOW_N];
static SnowObj   s_far[TREE_FAR];
static SnowObj   s_act[TREE_ACT];
static SnowObj   s_prop[PROP_N];                  // kind in .b: 0 rock 1 stump 2 jump
// The active trees carry a snow load in .b: 0..255, built up during a
// squall and shed in one dump when it gets too heavy. Loading them one
// at a time rather than all together is the whole point -- a hillside
// where every tree whitens on the same frame reads as a palette swap.
static float     s_lodgeX = 0.0f;                 // the lodge, on the far plane
static float     s_nearX = 0.0f;                  // one foreground tree
static uint8_t   s_bankD[SNOW_COLS];
static uint8_t   s_trackD[SNOW_COLS];             // carved tracks, fill back in
static bool      s_snowInit = false;

static float    s_wxInten = 0.18f, s_wxTarget = 0.35f, s_wxWind = 0.0f;
static uint32_t s_wxNextAt = 0, s_snLastMs = 0;

// Dawn runs on its own long clock -- minutes, not seconds. It warms the
// sky, melts the bank faster and keeps the yeti off the hill.
static const uint32_t DAWN_CYCLE = 210000;
static uint32_t s_dawnAt = 0;

// ---- skier -----------------------------------------------------------
// Three riders on the hill instead of one, each on its own timer and
// each on either skis or a board. One rider meant the slope stood empty
// most of the time and the yeti had nothing to chase whenever he felt
// like turning up, so he sat waiting on a target that was not there.
static const uint8_t RIDER_N = 3;
struct Rider {
    float    x, air, vy;
    uint32_t next;
    uint32_t crashAt;   // 0 = upright; otherwise when he went down
    const char* say;    // his own bubble, independent of the chase
    uint32_t sayTil;
    uint8_t  trick;
    uint8_t  kind;      // 0 skis, 1 board
    uint8_t  var;       // which outfit
    bool     live;
};
// What he says once the snow has settled. Short, and none of them a
// complaint that lasts longer than the crash did.
static const char* const CRASH_SAY[8] = {
    "oof", "ow.", "*crunch*", "I'm fine", "worth it", "my skis",
    "send it", "did you see" };
// How long he is down for. TUMBLE is gear still leaving, then he lies
// there, and the bubble only appears once he has stopped moving --
// a speech bubble over a body still cartwheeling is a caption, not a
// reaction.
static const uint32_t CRASH_TUMBLE = 420;
static const uint32_t CRASH_HOLD   = 1500;
static Rider  s_rid[RIDER_N];
static int8_t s_chaseTgt = -1;     // the rider the yeti has picked, or -1

// ---- the chase -------------------------------------------------------
// The outcome is rolled ONCE when he sets off, and his speed follows
// from it -- a yeti who is going to lose is visibly slower the whole way
// rather than teleporting to a decision at the last moment.
enum class ChaseEnd : uint8_t { GIVEUP, WINDED, TRIP, BEATEN, EAT };
enum class ChaseSt  : uint8_t { NONE, RUN, RESOLVE, LEAVE };
static ChaseSt  s_cSt  = ChaseSt::NONE;
static ChaseEnd s_cEnd = ChaseEnd::GIVEUP;
static uint32_t s_cAt = 0, s_cNext = 0;
// When the eat sequence itself began. Separate from s_cAt because that
// one is reset by every state change, and the EAT pose outlives its
// state: the draw keeps posing him for 400ms after RESOLVE hands over to
// LEAVE. Timed against s_cAt, the sequence restarted from frame zero at
// that hand-over -- the swallowed rider reappeared in his hands and his
// gut deflated, then both vanished when the pose expired.
static uint32_t s_eatAt = 0;
static float    s_yX = -60.0f;

// Wildlife. The dog trots the hill and breaks into a run alongside the
// skier when he passes; the hare bolts flat out the moment the yeti is
// anywhere on screen; birds burst off a tree when a jump goes off next
// to it. All three exist to make the hill feel occupied between chases.
static float    s_dogX = -60.0f, s_hareX = -60.0f;
static uint32_t s_birdAt = 0;
static float    s_birdX = 0.0f, s_birdY = 0.0f;

enum class YPose : uint8_t { RUN, EAT, WINDED, DOWN, RECOIL };

// ---- chase chatter ---------------------------------------------------
// They talk while it happens. Short lines only: the bubble uses the 6px
// font over a busy hill, and much past fourteen characters it stops
// being readable at a glance, which is the only glance it gets.
static const char* const SKI_TAUNT[6] = {
    "can't catch me", "too slow!", "nice legs", "later, furball",
    "eat my powder", "still hungry?" };
static const char* const YETI_TAUNT[6] = {
    "GRAAAH", "COME HERE", "MINE", "RRRAAA", "HUNGRY", "STOP RUNNING" };
static const char* const SAY_GIVEUP[2] = { "worth a try", "...fine" };
static const char* const SAY_WINDED[2] = { "*wheeze*", "one sec" };
static const char* const SAY_BEATEN[2] = { "BONK", "not today" };
// The eat talks in two beats now, because it has two beats. The first
// pair lands while he is holding the rider up and looking at him, and
// the second when he decides. Kept short: the bubble is 6px type over a
// busy hill and gets one glance.
static const char* const EAT_LOOK_Y[4] = {
    "hm.", "let me see", "checks out", "no lift pass" };
static const char* const EAT_LOOK_S[4] = {
    "hi.", "sir?", "I'm mostly bone", "we can talk" };
static const char* const EAT_BITE_Y[4] = {
    "yep", "five stars", "mm", "worth the run" };
static const char* const EAT_BITE_S[4] = {
    "oh COME on", "not the face", "tell my dog", "bye" };

static const char* s_saySkiTxt = nullptr;
static const char* s_sayYetTxt = nullptr;
static uint32_t    s_saySkiTil = 0, s_sayYetTil = 0, s_sayNextAt = 0;

// A small speech bubble with a tail, above whoever said it. Same paper
// and ink the werewolf uses on FIRE, at half the size.
static void snowSay(TFT_eSPI& t, int x, int topY, const char* txt) {
    if (!txt) return;
    t.setTextSize(1);
    const int bw = t.textWidth(txt) + 6, bh = 11;
    int bx = x - bw / 2;
    if (bx < 1) bx = 1;
    if (bx + bw > t.width() - 1) bx = t.width() - 1 - bw;
    const int by = topY - bh - 4;
    const uint16_t paper = t.color565(240, 238, 228), ink = t.color565(18, 14, 12);
    t.fillRect(bx, by, bw, bh, paper);
    t.drawRect(bx, by, bw, bh, ink);
    int tx = x - 2;
    if (tx < bx + 2) tx = bx + 2;
    if (tx > bx + bw - 5) tx = bx + bw - 5;
    t.fillRect(tx, by + bh, 3, 2, paper);
    t.fillRect(tx, by + bh + 2, 1, 2, paper);
    t.setTextColor(ink, paper);
    t.setCursor(bx + 3, by + 2);
    t.print(txt);
}

// THE EAT. The payoff, and it runs about one chase in six, so it has to
// be worth having waited for.
//
// It used to be grab-lift-chomp: the yeti being fast. It is an
// INSPECTION now -- he catches the rider, raises him to eye level, holds
// him there and looks at him, turns him over to check the other side,
// and only then eats him. The pause in the middle is the whole joke, and
// a pause was the one thing the old version had none of.
//
// It also reads better at this size for an unglamorous reason: a held
// pose gives the eye time to work out what it is looking at, where a
// fast one is over before it has been parsed.
static const uint32_t EAT_GRAB  = 300;    // hoisted off his skis, kicking
static const uint32_t EAT_RAISE = 760;    // lifted to eye level
static const uint32_t EAT_LOOK  = 1320;   // held there. nothing moves.
static const uint32_t EAT_TURN  = 1720;   // rotated over to head-first
static const uint32_t EAT_CHOMP = 2140;   // in, and the jaw closes
static const uint32_t EAT_GULP  = 2440;   // gone; the gut swells
static const uint32_t EAT_PAT   = 2980;   // satisfied
static const uint32_t EAT_TOTAL = EAT_PAT;

// The victim, drawn in front of the yeti. There is no rotation on this
// display, so "turning him over" is three discrete orientations --
// upright, tilted, horizontal -- which is the same lookup-table answer
// every rotating thing in this project has had to use.
static void snowVictim(TFT_eSPI& t, int x, int y, uint32_t age, uint32_t now,
                       uint8_t kind, uint8_t var) {
    if (age >= EAT_CHOMP) return;          // nothing left to show
    const uint16_t skin = t.color565(240, 192, 140), boot = t.color565(35, 40, 48);
    uint16_t coat = t.color565(47, 79, 208), coatL = t.color565(122, 160, 255);
    uint16_t pant = t.color565(47, 179, 30), hat = t.color565(224, 32, 10);
    uint16_t gear = t.color565(255, 210, 26);
    if (kind) {                            // a boarder: different kit
        coat = t.color565(146, 60, 210); coatL = t.color565(196, 140, 250);
        pant = t.color565(38, 42, 54);   hat  = t.color565(255, 210, 26);
        gear = t.color565(255, 96, 20);
    }
    if (var == 1) { coat = t.color565(224, 46, 30); coatL = t.color565(255, 140, 110);
                    pant = t.color565(30, 34, 46); hat = t.color565(255, 210, 26); }
    else if (var == 2) { coat = t.color565(236, 238, 244); coatL = WHITE;
                         pant = t.color565(146, 60, 210); hat = t.color565(47, 179, 30); }

    const int B = y - (int)(sinf((float)now / 90.0f) * 1.5f);
    // How hard he is struggling. Frantic on the way up, then almost
    // nothing while he is being looked at -- he has worked out how this
    // ends, and stillness there is what sells the beat.
    float fight = 1.0f;
    if (age >= EAT_RAISE && age < EAT_TURN) fight = 0.18f;
    else if (age >= EAT_TURN) fight = 0.55f;
    const int kick = (int)(sinf((float)now / 55.0f) * 3.0f * fight);
    const int wav  = (int)(sinf((float)now / 70.0f) * 3.0f * fight);

    if (age < EAT_TURN) {
        // ---- upright: caught, lifted, then held out to be looked at --
        int vx, vy;
        if (age < EAT_GRAB) { vx = x + 15; vy = B - 26; }
        else if (age < EAT_RAISE) {
            const float k = (float)(age - EAT_GRAB) / (float)(EAT_RAISE - EAT_GRAB);
            vx = x + 15 + (int)(k * 6.0f);
            vy = B - 26 - (int)(k * 22.0f);
        } else { vx = x + 21; vy = B - 48; }
        t.fillRect(vx - 5, vy + 12, 4, 8 + kick, pant);     // legs, dangling
        t.fillRect(vx + 1, vy + 12, 4, 8 - kick, pant);
        t.fillRect(vx - 6, vy + 19 + kick, 5, 3, boot);
        t.fillRect(vx + 1, vy + 19 - kick, 5, 3, boot);
        t.fillRect(vx - 6, vy, 12, 13, coat);
        t.fillRect(vx - 6, vy, 12, 3, coatL);
        t.fillRect(vx - 10, vy + 2 - wav, 5, 6, coat);      // arms
        t.fillRect(vx + 6,  vy + 2 + wav, 5, 6, coat);
        t.fillRect(vx - 3, vy - 7, 8, 7, skin);
        // Eyes: wide on the way up, then flat resignation while he is
        // being inspected. Two pixels of difference and it does the work.
        if (age < EAT_RAISE) {
            t.fillRect(vx - 2, vy - 5, 2, 3, t.color565(58, 42, 26));
            t.fillRect(vx + 2, vy - 5, 2, 3, t.color565(58, 42, 26));
        } else {
            t.fillRect(vx - 2, vy - 4, 2, 1, t.color565(58, 42, 26));
            t.fillRect(vx + 2, vy - 4, 2, 1, t.color565(58, 42, 26));
        }
        t.fillRect(vx - 4, vy - 11, 10, 4, hat);
        return;
    }

    // ---- turning over, then head first ------------------------------
    // Two frames of tilt between upright and horizontal. Three
    // orientations is the fewest that reads as a rotation rather than
    // as a cut, which is exactly what two of them looked like.
    const float k = (float)(age - EAT_TURN) / (float)(EAT_CHOMP - EAT_TURN);
    if (k < 0.34f) {
        const int vx = x + 19, vy = B - 46;
        t.fillRect(vx - 8, vy + 4, 13, 9, coat);            // body on the diagonal
        t.fillRect(vx - 8, vy + 4, 13, 3, coatL);
        t.fillRect(vx + 4, vy + 9, 9, 7, pant);
        t.fillRect(vx - 14, vy - 1, 8, 7, skin);            // head dropping
        t.fillRect(vx - 17, vy - 3, 5, 6, hat);
        t.fillRect(vx + 11, vy + 14 + kick, 5, 3, boot);
        t.fillRect(vx - 5, vy + 12 + wav, 5, 5, coat);
        if (kind) t.fillRect(vx + 13, vy + 12, 14, 3, gear);
        else { t.fillRect(vx + 13, vy + 11, 11, 2, gear);
               t.fillRect(vx + 13, vy + 16, 11, 2, gear); }
        return;
    }
    // Horizontal, head first at the teeth, sliding in as the jaw works.
    const int adv = (int)((k - 0.34f) / 0.66f * 11.0f);
    const int hx = x + 4 - adv, hy = B - 42;
    t.fillRect(hx + 2, hy, 6, 5, hat);                      // cap, at the teeth
    t.fillRect(hx + 7, hy + 2, 5, 5, skin);
    t.fillRect(hx + 10, hy + 5, 11, 8, coat);
    t.fillRect(hx + 10, hy + 5, 11, 2, coatL);
    t.fillRect(hx + 12, hy + 3, 5, 4, coat);
    t.fillRect(hx + 19, hy + 10, 9, 7, pant);
    t.fillRect(hx + 25, hy + 15 + kick, 5, 4, boot);
    t.fillRect(hx + 25, hy + 20 - kick, 5, 4, boot);
    if (kind) t.fillRect(hx + 27, hy + 16, 15, 3, gear);    // one plank
    else { t.fillRect(hx + 27, hy + 13 + kick, 11, 2, gear);
           t.fillRect(hx + 27, hy + 21 - kick, 11, 2, gear); }
}

// ---- sprites ---------------------------------------------------------

// An aurora, drawn as undulating horizontal ribbons rather than as
// vertical curtains. Two reasons, and the second is the real one.
//
// Cheapness: a ribbon is one drawFastHLine per row, where a curtain is
// several fillRects per COLUMN -- about 70 calls against 200 for the
// same band of sky.
//
// And the palette. This is the one large soft thing this display can
// actually render, because green has EIGHT levels where blue has four.
// Every other atmospheric effect on this hill has had to be fought
// through a four-step blue ramp; an aurora is mostly green with a warm
// base, so the falloff gets eight steps in the channel doing the work
// and the ordered dither only has to cover the gaps between them.
static void snowAurora(TFT_eSPI& t, int w, int yStart, int bandH,
                       uint32_t now, float strength, uint16_t skyTop, uint16_t skyLow) {
    if (strength <= 0.02f) return;
    const int top = yStart + 3;
    const int hgt = (bandH * 32) / 100;
    if (hgt < 10) return;
    static const int8_t ADITH[4] = { -7, 3, 7, -3 };
    for (uint8_t rb = 0; rb < 2; rb++) {
        const float ph = (float)now / (2600.0f + rb * 900.0f) + rb * 2.3f;
        const int   th = hgt - rb * (hgt / 4);
        // Top of this ribbon, not its centre. Centring it put half of the
        // first ribbon above yStart, where every row was thrown away by
        // the clamp below -- so the brightest part of the aurora, which
        // is its middle, was the part being clipped off.
        const int   cy = top + rb * (hgt / 5);
        for (int r = 0; r < th; r++) {
            const int y = cy + r;
            if (y < yStart + 1 || y >= yStart + bandH) continue;
            // The ribbon snakes sideways as it descends, which is what
            // makes it read as a sheet rather than as a stack of bars.
            const float sway = sinf((float)r * 0.16f + ph) * 44.0f
                             + sinf((float)r * 0.07f - ph * 0.7f) * 30.0f;
            const int cx = w / 2 + (int)sway;
            const int half = 52 + (int)(sinf((float)r * 0.11f + ph * 1.4f) * 26.0f);
            int x0 = cx - half, x1 = cx + half;
            if (x0 < 0) x0 = 0;
            if (x1 > w) x1 = w;
            if (x1 - x0 < 2) continue;
            // Brightest through the middle of the ribbon, dying at both
            // edges, plus the row dither so the boundary is not a line.
            float f = 1.0f - fabsf((float)r - th * 0.5f) / (th * 0.5f);
            f = f * f;
            int a = (int)(f * strength * 190.0f) + ADITH[y & 3];
            if (a <= 3) continue;
            if (a > 255) a = 255;
            // Green through the body, warming to red at the bottom edge
            // where a real one goes pink. Blue is left near the sky's own
            // value throughout: four levels cannot ramp, and trying makes
            // a band.
            const float low = (float)r / (float)th;
            const uint16_t hue = t.color565((uint8_t)(40 + low * 190.0f),
                                            (uint8_t)(255 - low * 60.0f),
                                            (uint8_t)(120 - low * 60.0f));
            const uint16_t sky = blend(skyTop, skyLow,
                                       (uint16_t)((y - yStart) * 255 / (bandH > 0 ? bandH : 1)));
            t.drawFastHLine(x0, y, x1 - x0, blend(sky, hue, (uint16_t)a));
        }
    }
}

// The lodge on the far ridge. Every window runs its own slow brightness
// cycle off a hash of its index, so they drift out of step: one dims
// while another comes up, one flickers like there is a fire in it, and
// now and then one goes out completely for a while and comes back. A row
// of windows all at one brightness reads as a decal; the same row
// varying independently reads as a building with people in it.
static void snowLodge(TFT_eSPI& t, int x, int ridgeY, uint32_t now,
                      float dawn, uint16_t haze) {
    const uint16_t beam  = blend(t.color565(84, 58, 40), haze, 60);
    const uint16_t beamD = blend(t.color565(52, 36, 26), haze, 60);
    const uint16_t roof  = blend(t.color565(232, 240, 248), haze, 40);
    const uint16_t roofE = blend(t.color565(176, 190, 206), haze, 40);
    const uint16_t frame = blend(t.color565(40, 30, 24), haze, 50);
    // Night is when the windows matter; they wash out as the sun comes up.
    const float lit = 1.0f - dawn * 0.85f;

    // Body: two storeys, with the upper one set back a little so the
    // roofline has a step in it instead of being one long wedge.
    t.fillRect(x - 17, ridgeY - 13, 34, 13, beam);
    t.fillRect(x - 17, ridgeY - 13, 34, 1,  beamD);
    t.fillRect(x - 12, ridgeY - 22, 24, 9,  beam);
    // Horizontal log courses -- three lines is enough to say timber.
    for (uint8_t i = 0; i < 3; i++) t.fillRect(x - 17, ridgeY - 10 + i * 4, 34, 1, beamD);
    // Roofs, snow-loaded, with an overhang and a shaded eave under it.
    t.fillRect(x - 20, ridgeY - 17, 40, 4, roof);
    t.fillRect(x - 20, ridgeY - 18, 40, 2, blend(roof, WHITE, 120));
    t.fillRect(x - 20, ridgeY - 13, 40, 1, roofE);
    t.fillRect(x - 15, ridgeY - 26, 30, 4, roof);
    t.fillRect(x - 15, ridgeY - 27, 30, 2, blend(roof, WHITE, 120));
    t.fillRect(x - 15, ridgeY - 22, 30, 1, roofE);

    // ---- the windows -------------------------------------------------
    // Five of them: three along the ground floor, two upstairs.
    static const int8_t WX[5] = { -13, -3, 7, -8, 3 };
    static const int8_t WY[5] = {  -9, -9, -9, -20, -20 };
    for (uint8_t i = 0; i < 5; i++) {
        uint32_t h = (uint32_t)(i + 3) * 2654435761u;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        // Each window on its own period, none of them harmonics of the
        // others, so the row never resynchronises into a pulse. The
        // periods are long on purpose: at two to seven seconds this read
        // as a string of fairy lights blinking, which is a decoration.
        // At six to seventeen it reads as rooms whose light is changing
        // because somebody in them moved.
        const float per = 6200.0f + (float)(h % 11000u);
        float k = 0.55f + 0.45f * sinf((float)now / per + (float)(h % 628u) * 0.01f);
        // One of them has a fire in it and flickers fast and shallow.
        if ((h & 3u) == 0u) k = 0.74f + 0.26f * sinf((float)now / 340.0f);
        // And occasionally a window is simply dark -- somebody went to
        // bed. A slow square wave with a long duty cycle, per window.
        const uint32_t slot = (uint32_t)(now / 21000u) + i * 7u;
        uint32_t hs = slot * 2246822519u; hs ^= hs >> 13;
        // Knocking overrides all of that. There are five windows and the door
        // takes five knocks, so each one lights a room and stays lit: by the
        // fifth the whole lodge is awake. That is the feedback the Starfield
        // eye taught us a multi-step trigger cannot do without -- without it
        // the player has no idea the first four did anything.
        const bool knocked = (i < lodgeKnocks());
        const bool out = !knocked && ((hs % 100u) < 22u);
        const float b = knocked ? 1.0f : (out ? 0.06f : k);
        const uint16_t glow = t.color565((uint8_t)(255 * lit),
                                         (uint8_t)((150 + 90 * b) * lit * b + 30),
                                         (uint8_t)(60 * b * lit));
        const int wx = x + WX[i], wy = ridgeY + WY[i];
        t.fillRect(wx - 1, wy - 1, 7, 7, frame);
        t.fillRect(wx, wy, 5, 5, blend(beam, glow, (uint16_t)(60 + b * 195.0f)));
        // Warm spill on the wall beneath a bright one. Two rectangles,
        // and it is what makes the light look like it is coming OUT.
        if (b > 0.55f && !out)
            t.fillRect(wx - 1, wy + 5, 7, 2, blend(beam, glow, (uint16_t)(b * 90.0f)));
    }
    // Door, with light spilling onto the snow in front of it.
    t.fillRect(x + 12, ridgeY - 8, 5, 8, frame);
    t.fillRect(x + 13, ridgeY - 7, 3, 7, blend(beam, t.color565(255, 190, 90),
                                               (uint16_t)(170 * lit)));
    t.fillRect(x + 11, ridgeY, 8, 2, blend(roof, t.color565(255, 200, 110),
                                           (uint16_t)(110 * lit)));
    // Chimney and its smoke, leaning with the same wind as everything else.
    t.fillRect(x - 9, ridgeY - 32, 4, 7, beamD);
    t.fillRect(x - 10, ridgeY - 33, 6, 2, blend(roof, WHITE, 90));
    for (uint8_t i = 0; i < 4; i++) {
        const float ag = fmodf((float)now / 700.0f + (float)i * 0.9f, 4.0f);
        const int sx = x - 7 + (int)(sinf(ag * 1.3f) * 5.0f + ag * 2.4f);
        const int sy = ridgeY - 35 - (int)(ag * 5.0f);
        if (sy < 2) continue;
        t.fillRect(sx, sy, 2 + (int)ag, 2, blend(haze, WHITE, (uint16_t)(90 - ag * 18.0f)));
    }
}

// A rider who has just lost it. Three beats: the tumble, where his gear
// is still leaving; the sit, where he is face down and not moving; and
// the pickup. The middle beat is the joke -- a crash that resolves in
// four frames reads as a glitch, and one that holds still for a second
// reads as a person having a bad time.
static void snowWreck(TFT_eSPI& t, int x, int y, uint32_t age, uint8_t kind, uint8_t var) {
    const uint16_t skin = t.color565(240, 192, 140);
    uint16_t coat = t.color565(47, 79, 208), pant = t.color565(47, 179, 30);
    uint16_t hat  = t.color565(224, 32, 10),  gear = t.color565(255, 210, 26);
    if (var == 1) { coat = t.color565(224, 46, 30); pant = t.color565(30, 34, 46);
                    hat = t.color565(255, 210, 26); }
    else if (var == 2) { coat = t.color565(236, 238, 244); pant = t.color565(146, 60, 210);
                         hat = t.color565(47, 179, 30); }
    if (kind) gear = t.color565(255, 96, 20);        // a board, not skis
    const uint16_t coatL = blend(coat, WHITE, 90), pantD = blend(pant, BLACK, 80);

    // How far the debris has travelled. It stops where it lands: gear
    // that keeps sliding while he sits there is a cartoon, and the
    // stillness is what sells this.
    const float fly = (age < 420u) ? (float)age / 420.0f : 1.0f;
    const int   d   = (int)(fly * 13.0f);
    const int   up  = (age < 420u) ? (int)((1.0f - fly) * 14.0f) : 0;

    // The gear, thrown clear.
    if (kind) {
        t.fillRect(x + 6 + d, y - 3 - up, 18, 3, gear);
        t.fillRect(x + 6 + d, y - up, 18, 1, blend(gear, BLACK, 80));
    } else {
        t.fillRect(x - 14 - d, y - 2, 14, 2, gear);
        t.fillRect(x + 7 + d, y - 9 - up, 3, 13, gear);
    }
    t.fillRect(x - 17 - d, y - 5, 8, 1, t.color565(58, 64, 73));    // a pole
    t.fillRect(x + 12 + d, y - 7 - up / 2, 1, 8, t.color565(58, 64, 73));

    // Him. Face down at first, then up on one elbow, then sitting.
    if (age < 420u) {
        t.fillRect(x - 9, y - 7, 18, 6, coat);
        t.fillRect(x - 9, y - 7, 18, 2, coatL);
        t.fillRect(x - 13, y - 4, 5, 3, pant);
        t.fillRect(x + 8,  y - 9, 6, 4, pantD);
        t.fillRect(x - 4, y - 11, 7, 5, skin);
        t.fillRect(x - 6 + (int)(fly * 8.0f), y - 20 - up, 8, 4, hat);   // hat in the air
    } else {
        t.fillRect(x - 8, y - 9, 15, 8, coat);
        t.fillRect(x - 8, y - 9, 15, 2, coatL);
        t.fillRect(x - 12, y - 4, 6, 3, pant);
        t.fillRect(x + 6,  y - 5, 6, 4, pant);
        t.fillRect(x - 3, y - 16, 8, 7, skin);
        // Eyes shut. Two dashes, and they do more than a whole face would.
        t.fillRect(x - 2, y - 13, 2, 1, t.color565(58, 42, 26));
        t.fillRect(x + 2, y - 13, 2, 1, t.color565(58, 42, 26));
        t.fillRect(x - 4, y - 20, 9, 4, hat);
        // An arm going up to check the head is still there.
        if (age > 900u) t.fillRect(x + 4, y - 22, 3, 7, coat);
    }
    // Snow still settling out of the air where he went in.
    for (uint8_t i = 0; i < 5; i++) {
        const int px = x - 12 + i * 6 + (int)(fly * ((i & 1) ? 4 : -4));
        const int py = y - 14 - (int)((1.0f - fly) * 10.0f) + (i % 3) * 4;
        t.fillRect(px, py, 2, 2, blend(WHITE, t.color565(196, 214, 236), (uint16_t)(fly * 160.0f)));
    }
}

static void snowPine(TFT_eSPI& t, int x, int y, int h, bool snowy, uint16_t haze, uint16_t mix,
                     float bend = 0.0f, uint8_t load = 0) {
    if (h < 8) return;
    // Blue kept under 30 on every green: this panel quantises blue to
    // 0/85/170/255, so anything near 42 rounds UP and the tree lands on
    // a teal slate instead of a colour a tree could be.
    const uint16_t deep = blend(t.color565( 8,  62, 10), haze, mix);
    const uint16_t dk   = blend(t.color565(14,  92, 14), haze, mix);
    const uint16_t md   = blend(t.color565(28, 124, 20), haze, mix);
    const uint16_t lt   = blend(t.color565(54, 162, 26), haze, mix);
    const uint16_t tip  = blend(t.color565(96, 200, 40), haze, mix);
    const uint16_t bark = blend(t.color565(96,  62, 30), haze, mix);
    const uint16_t snow = blend(t.color565(238, 244, 250), haze, mix);
    const int w = (h * 58) / 100;

    // Trunk, with its own shaded side.
    const int tw = (h > 40) ? 6 : 4;
    t.fillRect(x - tw / 2, y - (h * 22) / 100, tw, (h * 22) / 100, bark);
    t.fillRect(x - tw / 2, y - (h * 22) / 100, tw / 2, (h * 22) / 100,
               blend(bark, BLACK, 70));

    // Small trees on the far ridge get the cheap version -- at 24 px the
    // detail below is invisible and it is six of them every frame.
    if (h < 34) {
        const int base[3] = { 22, 48, 72 }, wid[3] = { 100, 74, 46 };
        for (int ti = 0; ti < 3; ti++) {
            const int bw = (w * wid[ti]) / 100, by = y - (h * base[ti]) / 100;
            const int th = (h * 36) / 100;
            for (int r = 0; r < 4; r++) {
                const int rw = bw - (bw * r * 88) / 400, ry = by - (th * r) / 4;
                if (rw < 2) continue;
                t.fillRect(x - rw / 2, ry - th / 4 - 1, rw, th / 4 + 2, (r < 2) ? dk : md);
            }
        }
        return;
    }

    // ---- the full version -------------------------------------------
    // Two changes do all the work. First, the rows do NOT taper cleanly:
    // the little jumps back out are branch tips catching light, and a
    // monotonic taper is exactly what made these read as a striped
    // triangle rather than as a tree.
    //
    // Second, the shading runs ACROSS the tree, not down it. Banding by
    // row is the thing the eye reads as stripes; banding by side is what
    // it reads as a lit object. The moon sits low on the left in this
    // scene, so the left face is lit and the right is in shadow.
    static const uint8_t ROW_W[6] = { 100, 86, 94, 72, 78, 52 };
    const int base[3] = { 20, 46, 70 }, wid[3] = { 100, 78, 52 };
    for (int ti = 0; ti < 3; ti++) {
        const int bw = (w * wid[ti]) / 100;
        const int by = y - (h * base[ti]) / 100;
        const int th = (h * 38) / 100;
        // WIND. The offset grows with height, which is the whole trick:
        // a uniform shift reads as three tiers sliding sideways, and a
        // height-weighted one reads as a trunk bending. Costs nothing --
        // it moves rectangles that were being drawn anyway.
        const int bx = x + (int)(bend * (float)(ti + 1) * 0.8f);
        // Lower tiers sit in their own shade; the crown gets the light.
        const uint16_t core  = (ti == 0) ? deep : ((ti == 1) ? dk : md);
        const uint16_t face  = (ti == 0) ? dk   : ((ti == 1) ? md : lt);
        const uint16_t edge  = (ti == 2) ? tip  : lt;
        for (int r = 0; r < 6; r++) {
            int rw = (bw * ROW_W[r]) / 100 - (bw * r * 62) / 600;
            if (rw < 3) continue;
            const int ry = by - (th * r) / 6;
            const int rh = th / 6 + 2;
            const int rx = bx - rw / 2;
            // shadow half, lit half, then a bright tip on the lit edge
            t.fillRect(rx, ry - rh, rw, rh, core);
            t.fillRect(rx, ry - rh, (rw * 46) / 100, rh, face);
            if (rw > 7) t.fillRect(rx, ry - rh, 2, rh, edge);
        }
        // A dark notch under each tier so the tiers separate instead of
        // merging into one cone.
        if (bw > 9) t.fillRect(bx - bw / 2 + 2, by - 1, bw - 4, 1, deep);
        if (snowy && bw > 9) {
            // Snow sits on the branch tops, not in an even bar across --
            // three short runs read as settled snow, one long one reads
            // as a stripe, which is the same mistake in a lighter colour.
            // SNOW LOAD. The runs grow with what the tree has caught, so
            // a hillside during a squall whitens tree by tree instead of
            // all at once. load is 0..255 and lives in SnowObj::b.
            const int L = 60 + (int)load * 195 / 255;
            t.fillRect(bx - bw / 2 + 1, by - 3, (bw * 30 * L) / 25500, 2, snow);
            t.fillRect(bx - (bw * 8) / 100, by - 4, (bw * 26 * L) / 25500, 2, snow);
            t.fillRect(bx + (bw * 22) / 100, by - 3, (bw * 22 * L) / 25500, 2, snow);
            if (load > 150) {
                // Heavily loaded: it starts to sit on top of the tier
                // rather than just along its front edge.
                t.fillRect(bx - bw / 2 + 3, by - 5, (bw * 34) / 100, 1, snow);
                t.fillRect(bx + (bw * 4) / 100, by - 6, (bw * 24) / 100, 1, snow);
            }
        }
    }
    // Leader at the very top -- it gets the full bend, being furthest up.
    const int lx = x + (int)(bend * 3.2f);
    t.fillRect(lx - 1, y - h - 2, 3, 5, lt);
    t.fillRect(lx - 1, y - h - 2, 1, 5, tip);
    if (load > 110) t.fillRect(lx - 1, y - h - 3, 3, 1, snow);
}

static void snowSkier(TFT_eSPI& t, int x, int y, int8_t carve, uint8_t trick,
                      uint8_t var = 0) {
    const int l = carve * 2;
    const uint16_t ski = t.color565(255, 210, 26), boot = t.color565(35, 40, 48);
    const uint16_t skin = t.color565(240, 192, 140);
    // Three of them share the hill now, so they cannot share a jacket.
    // Only the coat, trousers and hat change -- the silhouette is the
    // same person, which is the point; the colour is what says it is a
    // different one.
    uint16_t pant = t.color565(47, 179, 30), coat = t.color565(47, 79, 208);
    uint16_t coatL = t.color565(122, 160, 255), arm = t.color565(224, 85, 159);
    uint16_t hat = t.color565(224, 32, 10);
    if (var == 1) {
        coat = t.color565(224, 46, 30);  coatL = t.color565(255, 140, 110);
        pant = t.color565(30, 34, 46);   hat   = t.color565(255, 210, 26);
        arm  = t.color565(255, 196, 96);
    } else if (var == 2) {
        coat = t.color565(236, 238, 244); coatL = t.color565(255, 255, 255);
        pant = t.color565(146, 60, 210);  hat   = t.color565(47, 179, 30);
        arm  = t.color565(58, 64, 73);
    }
    const uint16_t pole = t.color565(58, 64, 73);
    if (trick >= 3) {
        // Backflip. No rotation on this display, so it is four discrete
        // tucked orientations -- the same trick the Squachy flip uses,
        // and the same reason: a lookup table is the only rotation there
        // is. trick carries the frame as 3 + (0..3).
        const uint8_t f = (uint8_t)(trick - 3);
        const int cy = y - 16;
        t.fillRect(x - 7, cy - 7, 15, 14, pant);            // tucked ball
        t.fillRect(x - 5, cy - 5, 11, 8,  coat);
        static const int8_t SK[4][4] = { {-11,-2, 11,2}, {-2,-11, 2,11},
                                         { 11, 2,-11,-2}, { 2, 11,-2,-11} };
        t.fillRect(x + SK[f][0] - 1, cy + SK[f][1] - 1, (f & 1) ? 3 : 11, (f & 1) ? 11 : 3, ski);
        t.fillRect(x + SK[f][2] - 1, cy + SK[f][3] - 1, (f & 1) ? 3 : 11, (f & 1) ? 11 : 3, ski);
        t.fillRect(x - 3, cy - 9, 7, 4, hat);
        return;
    }
    if (trick == 1) {                 // tuck: knees up, skis crossed
        t.fillRect(x - 10, y - 6, 13, 2, ski);
        t.fillRect(x - 3,  y - 9, 13, 2, ski);
        t.fillRect(x - 5, y - 14, 11, 7, pant);
    } else if (trick == 2) {          // spread eagle
        t.fillRect(x - 13, y - 4, 12, 2, ski);
        t.fillRect(x + 2,  y - 4, 12, 2, ski);
        t.fillRect(x - 6, y - 14, 12, 7, pant);
    } else {
        t.fillRect(x - 9 + l * 2, y - 2, 12, 2, ski);
        t.fillRect(x - 2 + l * 2, y,     12, 2, ski);
        t.fillRect(x - 4 + l, y - 8, 4, 6, boot);
        t.fillRect(x + 1 + l, y - 8, 4, 6, boot);
        t.fillRect(x - 5 + l, y - 15, 10, 8, pant);
    }
    t.fillRect(x - 5, y - 24, 11, 10, coat);
    t.fillRect(x - 5, y - 24, 11, 3,  coatL);
    if (trick == 2) { t.fillRect(x - 12, y - 25, 7, 4, arm); t.fillRect(x + 6, y - 25, 7, 4, arm); }
    else            { t.fillRect(x - 9,  y - 23, 4, 7, arm); t.fillRect(x + 6, y - 23, 4, 7, arm); }
    t.fillRect(x - 3, y - 30, 7, 6, skin);
    t.fillRect(x - 2, y - 28, 2, 2, t.color565(58, 42, 26));
    t.fillRect(x + 1, y - 28, 2, 2, t.color565(58, 42, 26));
    t.fillRect(x - 4, y - 34, 9, 4, hat);
    t.fillRect(x - 4, y - 31, 9, 2, t.color565(245, 247, 250));
    t.fillRect(x - 1, y - 37, 3, 3, WHITE);
    if (!trick) {
        t.drawLine(x - 8, y - 21, x - 13, y - 1, pole);
        t.drawLine(x + 9, y - 21, x + 13, y - 1, pole);
    }
}

// bulge > 0 fattens the gut -- he is full. Used by the eat sequence and
// nothing else, which is why it is a plain int rather than a pose.
// A snowboarder, at the same scale as the skier and deliberately a
// different silhouette: one plank instead of two, a wide stance with the
// shoulders square across the hill rather than pointed down it, no poles,
// and a beanie instead of a bobble hat. At 35 px tall the board and the
// stance are the entire difference, so both are drawn a shade larger than
// strict scale would ask for -- readable beats accurate at this size.
static void snowBoarder(TFT_eSPI& t, int x, int y, int8_t carve, uint8_t trick,
                        uint8_t var = 0) {
    const int l = carve * 2;
    const uint16_t skin = t.color565(240, 192, 140), boot = t.color565(24, 26, 34);
    const uint16_t eye  = t.color565(58, 42, 26);
    uint16_t deck = t.color565(255, 96, 20), deckD = t.color565(176, 58, 10);
    uint16_t coat = t.color565(146, 60, 210), coatL = t.color565(196, 140, 250);
    uint16_t pant = t.color565(38, 42, 54),  beanie = t.color565(255, 210, 26);
    if (var == 1) {
        deck = t.color565(36, 200, 190); deckD = t.color565(20, 130, 124);
        coat = t.color565(30, 34, 46);   coatL = t.color565(96, 104, 124);
        pant = t.color565(200, 202, 210); beanie = t.color565(255, 96, 20);
    } else if (var == 2) {
        deck = t.color565(255, 232, 40); deckD = t.color565(176, 150, 10);
        coat = t.color565(224, 46, 30);  coatL = t.color565(255, 140, 110);
        pant = t.color565(38, 42, 54);   beanie = t.color565(240, 240, 246);
    }

    if (trick >= 3) {
        // A spin, as four discrete orientations of the board around a
        // tucked rider. Same lookup table the skier's backflip uses, and
        // the same reason for it: nothing on this display rotates.
        const uint8_t f = (uint8_t)(trick - 3);
        const int cy = y - 17;
        t.fillRect(x - 7, cy - 7, 14, 14, coat);
        t.fillRect(x - 7, cy - 7, 14, 3, coatL);
        t.fillRect(x - 5, cy + 1, 10, 6, pant);
        static const int8_t BD[4][4] = { { -14,  4, 28,  4 }, { -10, -12, 20, 22 },
                                         { -14, -8, 28,  4 }, {  -9, -12, 18, 22 } };
        t.fillRect(x + BD[f][0], cy + BD[f][1], BD[f][2], BD[f][3],
                   ((f & 1) != 0) ? deckD : deck);
        t.fillRect(x - 3, cy - 11, 7, 4, beanie);
        return;
    }

    // The plank, with a lit top sheet and a dark edge under it.
    t.fillRect(x - 13 + l, y - 2, 27, 3, deck);
    t.fillRect(x - 13 + l, y + 1, 27, 1, deckD);
    t.fillRect(x - 15 + l, y - 4, 3, 4, deck);          // nose kick
    t.fillRect(x + 13 + l, y - 4, 3, 4, deck);          // tail kick
    // Bindings set wide apart. A stance this wide is most of what says
    // snowboard rather than ski from thirty pixels away.
    t.fillRect(x -  9 + l, y - 7, 6, 5, boot);
    t.fillRect(x +  4 + l, y - 7, 6, 5, boot);

    if (trick == 1) {
        // Indy grab: knees folded up, trailing hand down on the deck.
        t.fillRect(x - 6, y - 15, 13, 8, pant);
        t.fillRect(x - 6, y - 25, 13, 10, coat);
        t.fillRect(x - 6, y - 25, 13, 3, coatL);
        t.fillRect(x - 12, y - 16, 6, 4, coat);         // reaching arm
        t.fillRect(x + 7, y - 27, 5, 8, coat);          // other arm up
    } else if (trick == 2) {
        // Method: back arched, board pulled up behind him.
        t.fillRect(x - 6, y - 16, 13, 9, pant);
        t.fillRect(x - 7, y - 26, 14, 11, coat);
        t.fillRect(x - 7, y - 26, 14, 3, coatL);
        t.fillRect(x - 13, y - 28, 7, 4, coat);         // lead arm thrown out
        t.fillRect(x + 6, y - 20, 6, 4, coat);
    } else {
        t.fillRect(x - 6 + l / 2, y - 16, 13, 10, pant);
        t.fillRect(x - 7, y - 26, 14, 11, coat);
        t.fillRect(x - 7, y - 26, 14, 3, coatL);
        // Arms out across the hill for balance -- the boarder's tell.
        t.fillRect(x - 13, y - 25, 6, 4, coat);
        t.fillRect(x +  7, y - 23, 6, 4, coat);
    }
    t.fillRect(x - 4, y - 32, 9, 6, skin);
    t.fillRect(x - 2, y - 30, 2, 2, eye);
    t.fillRect(x + 2, y - 30, 2, 2, eye);
    t.fillRect(x - 5, y - 36, 11, 5, beanie);
    t.fillRect(x - 5, y - 37, 11, 2, blend(beanie, WHITE, 90));
}

// A rooster tail off the uphill edge. It scales with how hard he is
// actually turning -- strongest through the middle of a carve, nothing
// at all through the transition -- so it appears and disappears with the
// turn instead of trailing him like a permanent exhaust, which is what
// made the first version look like a smoke machine.
static void snowSpray(TFT_eSPI& t, int x, int gy, int8_t carve, float power, uint32_t now) {
    if (power < 0.45f) return;                 // nothing through the flat part
    const uint16_t pw = blend(WHITE, t.color565(206, 224, 244), 70);
    const int n = 2 + (int)(power * 2.6f);     // three or four, not eight
    for (int i = 0; i < n; i++) {
        const float ph = fmodf((float)now / 78.0f + (float)i * 2.1f, 5.0f);
        const int dx = -carve * (3 + (int)(ph * 2.4f));
        const int dy = -1 - (int)(ph * 1.05f) - ((i & 1) ? 1 : 0);
        const int sz = (ph < 2.2f) ? 2 : 1;
        t.fillRect(x + dx, gy + dy, sz, sz, ((i % 3) != 0) ? pw : WHITE);
    }
}

// holding: put the arms in the reaching pose regardless of what the
// face is doing. The inspection needs his hands out in front while his
// mouth is still SHUT -- the jaw only opens once he has decided -- and
// the arm branch used to key entirely off the pose, so there was no way
// to have one without the other.
static void snowYeti(TFT_eSPI& t, int x, int y, uint32_t now, YPose pose, int bulge = 0,
                     bool holding = false, int holdDX = 0, int holdDY = 0) {
    // Mid grey, not off-white. At (201,206,214) he quantised to
    // (219,219,255) -- within one palette step of the snow he stands on,
    // so his whole body vanished and he read as a floating head. The
    // reference sprite is plainly grey against white for the same reason.
    const uint16_t fur = t.color565(150, 155, 168), furHi = t.color565(205, 210, 222);
    const uint16_t furS = t.color565(100, 106, 128), mane = t.color565(120, 126, 140);
    const uint16_t head = t.color565(49, 54, 63),   limb = t.color565(43, 48, 56);
    const uint16_t eye = t.color565(255, 36, 20),   tooth = t.color565(255, 210, 26);
    const uint16_t claw = t.color565(232, 237, 243);

    if (pose == YPose::DOWN) {
        // Face down in the snow, legs still going. The funniest of the
        // five and the cheapest -- it is the same parts, on their side.
        const int kick = (int)(sinf((float)now / 90.0f) * 4.0f);
        t.fillRect(x - 18, y - 12, 34, 10, fur);
        t.fillRect(x - 18, y - 12, 34, 3,  furHi);
        t.fillRect(x + 14, y - 14, 14, 9,  head);
        t.fillRect(x + 12, y - 16, 6, 4,   mane);
        t.fillRect(x - 24, y - 10, 8, 4,   limb);
        t.fillRect(x - 22, y - 20 + kick, 4, 10, limb);
        t.fillRect(x - 14, y - 22 - kick, 4, 11, limb);
        t.fillRect(x + 18, y - 6, 6, 3, tooth);
        return;
    }
    const int bob = (int)(sinf((float)now / (pose == YPose::WINDED ? 260.0f : 90.0f))
                          * (pose == YPose::WINDED ? 2.5f : 1.5f));
    const int B = y - bob;
    const int lean = (pose == YPose::WINDED) ? 4 : ((pose == YPose::RECOIL) ? -6 : 0);
    // The legs were detached, twice over. The body's six rows start at
    // B-38 and are 22 tall, so the belly ends at B-16 -- and the legs
    // started at B-13, leaving three pixels of night between his hips
    // and his torso. Worse, `lean` moved the body and not the legs, so
    // WINDED and RECOIL slid the whole torso six pixels off them.
    //
    // Legs now start at B-17, one pixel INTO the bottom body row so
    // there is no seam at any bob offset, and they carry half the lean:
    // his hips move with him, just less than his shoulders do.
    const int hip = lean / 2;
    // A gait, in opposition. Free -- it moves rectangles that were being
    // drawn anyway -- and a running character with rigid legs was the
    // other half of why he read as a cardboard cutout.
    const int gait = (pose == YPose::RUN) ? (int)(sinf((float)now / 90.0f) * 4.0f) : 0;
    t.fillRect(x - 8 + hip, B - 17, 4, 15, limb);
    t.fillRect(x + 4 + hip, B - 17, 4, 15, limb);
    // Feet swing fore and aft, and the trailing one lifts off the snow.
    t.fillRect(x - 12 + hip + gait, B - 3 - ((gait > 2) ? 1 : 0), 8, 3, limb);
    t.fillRect(x +  4 + hip - gait, B - 3 - ((gait < -2) ? 1 : 0), 8, 3, limb);
    static const int8_t  gw[6] = { 8, 10, 11, 11, 10, 8 };
    static const uint8_t gh[6] = { 3, 3, 4, 5, 4, 3 };
    int yy = B - 38;
    for (int i = 0; i < 6; i++) {
        const int extra = (bulge && i >= 1 && i <= 4) ? bulge : 0;
        t.fillRect(x - gw[i] - extra + lean, yy, (gw[i] + extra) * 2, gh[i], fur);
        yy += gh[i];
    }
    t.fillRect(x - 11 + lean, B - 33, 3, 14, furS);
    t.fillRect(x + 6 + lean,  B - 35, 4, 12, furHi);
    // ---- arms: shoulder, elbow, hand ---------------------------------
    // What was here before was two dark bars floating beside him. The arm
    // rect ran y B-40..B-28 at x-20..x-16, while the torso's widest row
    // only reaches x-11 -- a five to six pixel channel of background down
    // the whole arm, with the top two rows above the torso entirely,
    // since the body does not start until B-38. Against white snow, with
    // the arms in near-black limb and the body in pale fur, they read as
    // two objects rather than as one animal.
    //
    // The joint is now at x +/- 7, B-34, which is INSIDE the second body
    // row (that one runs -10..10 across B-35..B-32), and the upper arm is
    // drawn in FUR rather than limb, so it emerges from the creature
    // instead of being bolted to the side of it. Only the forearm goes
    // dark. The colour change does as much work here as the geometry.
    const int jy = B - 34;
    for (int8_t sd = -1; sd <= 1; sd += 2) {
        int ex, ey, hx, hy;
        if (holding || pose == YPose::EAT || pose == YPose::RECOIL) {
            // Reaching. Held rather than cycling -- this is a gesture with
            // a victim on the end of it, not a loop. holdDX/holdDY let the
            // eat sequence walk the hands from the snow up to eye level
            // and back in to the mouth.
            ex = sd * 13;          ey = -46 + holdDY / 2;
            hx = sd * 11 + 4 + holdDX; hy = -56 + holdDY;
        } else if (pose == YPose::WINDED) {
            // Hands down on his knees, barely moving.
            ex = sd * 13; ey = -30;
            hx = sd * 11; hy = -22 + (int)(sinf((float)now / 260.0f) * 1.5f);
        } else {
            // TUBE MAN. The whole arm ripples: the elbow leads and the
            // hand follows about a quarter cycle behind it. That LAG is
            // the entire reason it reads as loose rather than as merely
            // fast -- an arm whose parts all move in phase is a rigid arm
            // being waved, which is what the old swing looked like.
            //
            // The two sides run on opposite phases so he is never
            // symmetrical, which is the other half of it.
            const float ph = (float)now / 294.0f + (float)sd * 2.0f;
            const float a  = sinf(ph);
            const float b  = sinf(ph - 1.5f);
            ex = (int)((float)sd * (15.0f + a * 6.0f));
            ey = (int)(-42.0f + a * 7.0f);
            hx = (int)((float)sd * (17.0f + b * 10.0f));
            hy = (int)(-55.0f + b * 9.0f);
        }
        const int sx  = x + sd * 7 + lean, sy = jy;
        const int aex = x + ex + lean,     aey = B + ey;
        const int ahx = x + hx + lean,     ahy = B + hy;
        // Upper arm: fur, with a shaded core down the middle so it has
        // the same two-tone the rest of him does.
        t.drawWideLine(sx, sy, aex, aey, 7, fur);
        t.drawWideLine(sx, sy, aex, aey, 4, furS);
        // Forearm, dark, and a patch over the elbow so the corner between
        // the two segments does not notch.
        t.drawWideLine(aex, aey, ahx, ahy, 5, limb);
        t.fillRect(aex - 3, aey - 3, 6, 6, fur);
        t.fillRect(ahx - 3, ahy - 2, 7, 5, limb);
        for (int c = 0; c < 3; c++) {
            const int e = (c == 1) ? 1 : 0;
            t.fillRect(ahx - 4 + c * 3, ahy + 3 - e, 2, 4 + e, claw);
        }
    }
    t.fillRect(x - 11 + lean, B - 52, 22, 4, mane);
    t.fillRect(x - 13 + lean, B - 50, 3, 7, mane);
    t.fillRect(x + 10 + lean, B - 50, 3, 7, mane);
    t.fillRect(x - 10 + lean, B - 49, 20, 13, head);
    t.fillRect(x - 7 + lean, B - 45, 6, 4, eye);
    t.fillRect(x + 1 + lean, B - 45, 6, 4, eye);
    t.fillRect(x - 7 + lean, B - 46, 3, 2, eye);
    t.fillRect(x + 4 + lean, B - 46, 3, 2, eye);
    const int th = (pose == YPose::EAT) ? 7 : ((pose == YPose::WINDED) ? 6 : 4);
    t.fillRect(x - 7 + lean, B - 40, 14, th, tooth);
    for (int i = 0; i < 4; i++) t.fillRect(x - 4 + i * 3 + lean, B - 40, 1, th, head);
    if (pose == YPose::EAT) t.fillRect(x - 7 + lean, B - 40 + th / 2, 14, 1, head);
}

// Anything the background needs drawn ON TOP of the mascot. Called by
// ui_clear after Squachy and the idle-event flourishes, before the title
// bar (which owns its own row and repaints it whole).
//
// Only the werewolf's bubble uses this so far. It is here rather than in
// drawFire because drawFire runs first by definition -- it is the
// background -- and a speech bubble is the one thing in a scene that
// cannot afford to be half-covered. Everything else the fire draws is
// happy to be occluded by the mascot; text is not.
void drawBackgroundOverlay(TFT_eSPI& t, uint32_t now) {
    // Stale guard, same shape as the moon's: a background switch does
    // not clear these, so an old position must not paint a ghost.
    if (s_wolfSayY < 0 || (now - s_wolfSayAt) > 250u) return;
    const int w = s_wolfSayW;
    const int yStart = s_wolfSayTop;
        auto wolfBubble = [&](const char* txt, int by, uint8_t dim, bool big) {
            t.setTextSize(big ? 2 : 1);
            const int bw = t.textWidth(txt) + (big ? 11 : 7);
            const int bh = big ? 20 : 11;
            int bx = w - 2 - bw;
            if (bx < 1)          bx = 1;
            if (by < yStart + 1) by = yStart + 1;
            const uint16_t paper = blend(BG, t.color565(236, 232, 218), dim);
            const uint16_t ink   = blend(BG, t.color565(16, 12, 10), dim);
            t.fillRect(bx, by, bw, bh, paper);
            t.drawRect(bx, by, bw, bh, ink);
            // Tail under the wolf, not under the corner of the bubble.
            int tailX = s_wolfSayX - 3;
            if (tailX < bx + 2)      tailX = bx + 2;
            if (tailX > bx + bw - 6) tailX = bx + bw - 6;
            t.drawFastHLine(tailX, by + bh,     4, paper);
            t.drawFastHLine(tailX, by + bh + 1, 2, paper);
            t.drawPixel(tailX - 1, by + bh, ink);
            t.setTextColor(ink, paper);
            t.setCursor(bx + 4, by + (big ? 4 : 2));
            t.print(txt);
            t.setTextSize(1);
        };

        if (s_wolfSayKind == 1) {
            wolfBubble("Don't Be a SKID!", s_wolfSayY, 255, false);
        } else if (s_wolfSayKind == 2) {
            // Echoes come in behind the note and step up and back as the
            // sound rolls off the valley -- drawn first so the loud one
            // sits in front of them.
            const float k = s_wolfHowlK;
            // One echo, not two. The howl puts s_wolfSayY near the top of
            // the band already, so a second one stacked above it just
            // clamped to the same line and the pair drew on top of each
            // other -- a bubble jammed under the title bar reads as a
            // fault, not as distance.
            if (k > 0.20f) wolfBubble("awooo...", s_wolfSayY - 22, 110, false);
            wolfBubble("AWOOOO!", s_wolfSayY, 255, true);
        }
    if (s_wolfSayKind == 1) {
        wolfBubble("Don't Be a SKID!", s_wolfSayY, 255, false);
    } else if (s_wolfSayKind == 2) {
        // Echoes come in behind the note and step up and back as the
        // sound rolls off the valley -- drawn first so the loud one sits
        // in front of them.
        const float k = s_wolfHowlK;
        if (k > 0.20f) wolfBubble("awooo...", s_wolfSayY - 22, 110, false);
        wolfBubble("AWOOOO!", s_wolfSayY, 255, true);
    }
}

void drawSnowfall(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    const int w = t.width();
    // One pixel of air above the counters. The bank already runs down
    // OVER the "ACTIVE DETECTIONS" row -- that text sits at
    // countersTop - 34 and survives on any background because it is
    // drawn with a 24-pass black outline first. What it must not do is
    // butt straight into the counter numbers below, which are a plain
    // t.print with no outline at all: cyan on white snow is the one
    // combination here that genuinely cannot be read.
    const int yBot  = ((s_bgFloor > yStart + 40 && s_bgFloor <= yEnd) ? s_bgFloor : yEnd) - 1;
    const int bandH = yBot - yStart;
    if (bandH < 50) return;
    const int ridgeY = yStart + (bandH * 52) / 100;      // where the far plane stands

    if (!s_snowInit) {
        for (uint8_t i = 0; i < SNOW_N; i++) {
            s_flk[i].x = (float)random(0, w); s_flk[i].y = (float)random(yStart, yBot);
            s_flk[i].layer = (uint8_t)(i % 3); s_flk[i].ph = (uint8_t)random(0, 255);
        }
        for (uint8_t i = 0; i < SNOW_COLS; i++) { s_bankD[i] = (uint8_t)random(4, 9); s_trackD[i] = 0; }
        for (uint8_t i = 0; i < TREE_FAR; i++) { s_far[i].x = (float)(i * (w + 60) / TREE_FAR); s_far[i].a = (uint8_t)random(18, 28); }
        for (uint8_t i = 0; i < TREE_ACT; i++) {
            s_act[i].x = (float)(i * (w + 90) / TREE_ACT);
            s_act[i].a = (uint8_t)random(52, 70);
            s_act[i].b = (uint8_t)random(0, 90);          // starting snow load
        }
        s_lodgeX = (float)(w + 140);
        for (uint8_t i = 0; i < PROP_N; i++)   { s_prop[i].x = (float)(i * (w + 70) / PROP_N); s_prop[i].b = (uint8_t)(i % 3); }
        s_nearX = (float)(w + 120);
        s_snLastMs = now; s_dawnAt = now;
        for (uint8_t i = 0; i < RIDER_N; i++) {
            s_rid[i].x = -40.0f; s_rid[i].air = 0.0f; s_rid[i].vy = 0.0f;
            s_rid[i].trick = 0;  s_rid[i].live = false;
            s_rid[i].crashAt = 0; s_rid[i].say = nullptr; s_rid[i].sayTil = 0;
            s_rid[i].kind = (uint8_t)((i == 1) ? 1 : 0);   // one boarder to open with
            s_rid[i].var  = i;
            // Staggered, so they come down the hill in a loose procession
            // rather than three abreast on the first pass.
            s_rid[i].next = now + 2200u + (uint32_t)i * 5200u;
        }
        s_cNext = now + 12000;
        s_snowInit = true;
    }

    uint32_t dt = now - s_snLastMs;
    if (dt > 200u) dt = 200u;
    const bool step = (now != s_snLastMs);
    if (step) s_snLastMs = now;
    const float ds = (float)dt / 16.0f;
    // ---- dawn --------------------------------------------------------
    const float dphase = (float)((now - s_dawnAt) % DAWN_CYCLE) / (float)DAWN_CYCLE;
    // 0 for most of the cycle, rising to 1 over the last third and back.
    float dawn = 0.0f;
    if (dphase > 0.62f) {
        dawn = (dphase < 0.80f) ? (dphase - 0.62f) / 0.18f : 1.0f - (dphase - 0.80f) / 0.20f;
        if (dawn < 0.0f) dawn = 0.0f;
    }

    // ---- weather -----------------------------------------------------
    if (step) {
        if (now >= s_wxNextAt) {
            const long r = random(0, 100);
            s_wxTarget = (r < 50) ? 0.12f + (float)random(0, 18) / 100.0f
                       : (r < 82) ? 0.36f + (float)random(0, 24) / 100.0f
                                  : 0.72f + (float)random(0, 26) / 100.0f;
            s_wxNextAt = now + (uint32_t)random(8000, 22000);
        }
        s_wxInten += (s_wxTarget - s_wxInten) * 0.0015f * ds;
        s_wxInten = (s_wxInten < 0.05f) ? 0.05f : (s_wxInten > 1.0f ? 1.0f : s_wxInten);
        s_wxWind = sinf((float)now / 5200.0f) * 0.5f + sinf((float)now / 1700.0f) * 0.22f * s_wxInten;
    }
    const float inten = s_wxInten * (1.0f - dawn * 0.55f);   // it clears at dawn
    const float wind  = s_wxWind;

    // ---- bank, and the tracks filling back in ------------------------
    if (step) {
        static float acc = 0.0f;
        acc += ds;
        if (acc > 6.0f) {
            acc = 0.0f;
            for (uint8_t i = 0; i < SNOW_COLS; i++) {
                const float lean = 1.0f + wind * 0.8f * ((float)i / SNOW_COLS - 0.5f) * 2.0f;
                // Dawn melts it: the sun is the other half of the weather.
                const float melt = ((inten < 0.25f) ? 0.16f : 0.0f) + dawn * 0.45f;
                float d = (float)s_bankD[i] + inten * inten * 0.5f * (lean > 0.2f ? lean : 0.2f) - melt;
                d = (d < 2.0f) ? 2.0f : (d > (float)SNOW_MAXD ? (float)SNOW_MAXD : d);
                s_bankD[i] = (uint8_t)d;
                if (s_trackD[i]) s_trackD[i]--;          // snow fills the cut
            }
            for (uint8_t i = 1; i + 1 < SNOW_COLS; i++)
                s_bankD[i] = (uint8_t)(((int)s_bankD[i-1] + (int)s_bankD[i]*2 + (int)s_bankD[i+1]) / 4);
        }
    }
    auto groundAt = [&](int x) -> int {
        int c = x * SNOW_COLS / (w > 0 ? w : 1);
        c = (c < 0) ? 0 : (c >= SNOW_COLS ? SNOW_COLS - 1 : c);
        return yBot - (int)s_bankD[c] - ((w - x) * SNOW_RISE) / (w > 0 ? w : 1);
    };

    // ---- sky ---------------------------------------------------------
    // Colours picked by where they LAND. Blue quantises to 0/85/170/255
    // here, so a night sky needs blue well over 42 or it collapses to a
    // pure-green band -- which is exactly what the first pass did.
    const uint8_t wl = (uint8_t)(inten * 70.0f);
    // Endpoints spread wider than before, deliberately. A ramp is only
    // as smooth as the number of quantisation levels it actually crosses:
    // green 26 -> 146 passed four, where 10 -> 190 passes six, and red
    // now moves too. More boundaries crossed means more places the dither
    // has something to mix, which is what actually buys smoothness here.
    uint8_t tr = (uint8_t)(8 + wl),  tg = (uint8_t)(10 + wl),  tb = (uint8_t)(96 + inten * 44.0f);
    uint8_t br = (uint8_t)(54 + wl), bg = (uint8_t)(190 - wl / 3), bb = (uint8_t)(190 + inten * 40.0f);
    if (dawn > 0.01f) {
        // Sunrise is allowed low blue -- that is what makes it warm.
        tr = (uint8_t)(tr + dawn * 120.0f); tg = (uint8_t)(tg + dawn * 40.0f);
        tb = (uint8_t)(tb + dawn * 60.0f);
        br = (uint8_t)(br + dawn * 210.0f); bg = (uint8_t)(bg * (1.0f - dawn * 0.30f) + dawn * 40.0f);
        bb = (uint8_t)(bb * (1.0f - dawn * 0.70f));
    }
    const uint16_t skyTop = t.color565(tr, tg, tb);
    const uint16_t skyLow = t.color565(br, bg, bb);
    // Ordered dither, one row at a time. Blue has FOUR levels on this
    // panel, so a straight eighteen-band ramp could only ever show four
    // flat steps no matter how many bands it was cut into -- which is
    // why the sky read as stripes. Nudging each row's interpolation by a
    // repeating 4-row threshold pushes alternate rows either side of the
    // quantisation boundary, so the eye mixes them: about thirteen
    // apparent shades out of four real ones.
    //
    // The pixel count is identical to the band version. It costs ~150
    // more drawFastHLine calls and buys the single biggest visual
    // improvement available in this whole background.
    // Gentle on purpose. At +/-11 the nudge was large enough to flip the
    // GREEN channel too, which has eight levels and did not need help --
    // that showed as teal streaks rather than as a smooth ramp. At +/-6
    // it only crosses a boundary where one is genuinely close, which is
    // what dithering is supposed to do.
    static const int8_t SKY_DITH[4] = { -9, 4, 9, -4 };
    for (int y = yStart; y < yBot; y++) {
        int k = ((y - yStart) * 255) / (bandH > 1 ? bandH - 1 : 1);
        k += SKY_DITH[(y - yStart) & 3];
        if (k < 0) k = 0;
        if (k > 255) k = 255;
        t.drawFastHLine(0, y, w, blend(skyTop, skyLow, (uint16_t)k));
    }
    // A helper for anything that has to sit ON the gradient: the sky is
    // dithered per row now, so a single sampled colour is wrong almost
    // everywhere. This gives the undithered value at a given height.
    auto skyAt = [&](int y) -> uint16_t {
        int k = ((y - yStart) * 255) / (bandH > 1 ? bandH - 1 : 1);
        if (k < 0) k = 0;
        if (k > 255) k = 255;
        return blend(skyTop, skyLow, (uint16_t)k);
    };

    // Aurora, over the stars and under everything solid. It wants a dark
    // clear sky: it fades out as dawn comes up and as the weather closes
    // in, because an aurora through a blizzard is neither.
    {
        const float au = (1.0f - dawn) * (1.0f - inten * 0.85f)
                       * (0.45f + 0.55f * sinf((float)now / 41000.0f));
        if (au > 0.05f) snowAurora(t, w, yStart, bandH, now, au, skyTop, skyLow);
    }

    // Stars: 34, at three brightnesses, and they twinkle. There were 14
    // and they never moved. Cost is 34 drawPixel, which is nothing.
    {
        const float vis = (1.0f - inten) * (1.0f - dawn);
        for (uint8_t i = 0; i < 34; i++) {
            uint32_t h = (uint32_t)(i + 1) * 2654435761u;
            h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            const int sx = (int)(h % (uint32_t)w);
            const int sy = yStart + 3 + (int)((h >> 11) % 52u);
            if (sy >= ridgeY - 4) continue;
            // Each star twinkles on its own period, or it reads as one
            // flashing sheet rather than as a sky.
            const float tw = 0.45f + 0.55f * sinf((float)now / (620.0f + (float)(h % 900u))
                                                  + (float)(h % 628u) * 0.01f);
            const uint16_t c = blend(skyAt(sy), WHITE, (uint16_t)(230.0f * vis * tw));
            t.drawPixel(sx, sy, c);
            if ((h & 7u) == 0) { t.drawPixel(sx + 1, sy, c); t.drawPixel(sx, sy + 1, c); }
        }
    }

    // Three cloud banks drifting at different speeds, lit along their
    // undersides. Red and green carry the underlight, and those are the
    // two channels with eight levels -- this is the sort of thing the
    // palette is actually good at, unlike a blue ramp.
    // Two, not three, and built as a stepped silhouette rather than a
    // bar. At nine pixels tall they were the same shape as the dither's
    // own row texture and simply added to the streaking instead of
    // reading as cloud -- a cloud needs a lumpy edge more than it needs
    // shading.
    for (uint8_t c = 0; c < 2; c++) {
        const float sp = 0.0035f + (float)c * 0.0030f;
        const int   cy = yStart + 11 + c * 21;
        const int   cw2 = 78 + (int)c * 22;
        const int   cx = (int)(fmodf((float)now * sp + (float)c * 240.0f,
                                     (float)(w + 220))) - 110;
        if (cx < -cw2 || cx > w + cw2) continue;
        const uint16_t sk   = skyAt(cy);
        const uint16_t body = blend(sk, BLACK, 78);
        const uint16_t topL = blend(sk, WHITE, 34);
        // Five stacked runs of different widths and offsets: the ragged
        // profile is what says cloud at this size.
        static const int8_t LW[5] = { 46, 74, 100, 84, 58 };
        static const int8_t LO[5] = { -6,  4,   0, -8,  6 };
        for (uint8_t r = 0; r < 5; r++) {
            const int rw = (cw2 * LW[r]) / 100;
            t.fillRect(cx - rw / 2 + LO[r], cy + r * 3, rw, 4,
                       (r == 0) ? topL : body);
        }
        // Lit underside, warming as dawn comes up.
        t.fillRect(cx - (cw2 * 58) / 200 + 6, cy + 15, (cw2 * 58) / 100, 2,
                   blend(sk, t.color565(255, (uint8_t)(206 - dawn * 60.0f), 150), 105));
    }

    // One satellite, crossing steadily and blinking. A surveillance
    // detector quietly tracking something overhead is about nine pixels
    // of work and the most on-the-nose thing in the whole project.
    {
        const float sp = fmodf((float)now / 26000.0f, 1.0f);
        if (sp < 0.80f && dawn < 0.5f) {
            const int sx = (int)(sp * (float)w * 1.25f) - 20;
            const int sy = yStart + 8 + (int)(sp * 30.0f);
            if (sx >= 0 && sx < w) {
                const bool on = ((now / 430u) & 1u) == 0;
                t.drawPixel(sx, sy, blend(skyAt(sy), WHITE, on ? 255 : 130));
                if (on) t.drawPixel(sx, sy - 1, blend(skyAt(sy), WHITE, 90));
            }
        }
    }

    // ---- moon, with a real glow --------------------------------------
    // No alpha on this panel, so the halo is three rings blended toward
    // the sky rather than one soft gradient. Sets as dawn comes up.
    if (dawn < 0.92f) {
        const int mx = (int)(w * 0.16f), my = yStart + 30 + (int)(dawn * 46.0f);
        const uint16_t moon = blend(t.color565(232, 240, 252), skyTop, (uint16_t)(dawn * 200.0f));
        // Halo blended against the sky AT THE MOON'S HEIGHT, not against
        // skyTop. Using skyTop drew a disc darker than its surroundings
        // -- a hole in the sky rather than a glow, because the gradient
        // has already lightened by the time it gets down here.
        const uint16_t skyHere = blend(skyTop, skyLow,
                                       (uint16_t)((my - yStart) * 255 / (bandH > 0 ? bandH : 1)));
        // No halo. Three attempts at one, all wrong for the same
        // underlying reason: a glow spans fifty-odd rows of a vertical
        // gradient, so ANY approximation built from a single sky sample
        // is too dark at one end and too light at the other, and reads
        // as a disc cut out of the sky. Per-ring sampling does not help
        // -- a circle itself spans many rows. It needs real alpha, which
        // this panel does not have, so the moon gets a crisp rim instead
        // and the sky behind it is left alone.
        t.fillCircle(mx, my, 15, moon);
        // Maria first -- big soft dark seas, overlapping so the edges are
        // irregular rather than three obvious circles.
        const uint16_t mare = blend(moon, skyHere, 74);
        t.fillCircle(mx - 5, my - 4, 5, mare);
        t.fillCircle(mx - 1, my - 7, 3, mare);
        t.fillCircle(mx + 4, my + 3, 4, mare);
        t.fillCircle(mx + 1, my + 6, 3, mare);
        t.fillCircle(mx - 7, my + 3, 2, mare);
        // Craters: a darker floor with a lit rim on the sunward side,
        // which is what actually makes a disc read as cratered rather
        // than as a blotchy circle.
        const uint16_t pit = blend(moon, skyHere, 120);
        const uint16_t rim = blend(moon, WHITE, 90);
        static const int8_t CR[5][3] = { {6,-6,3}, {-8,-1,2}, {2,-2,2}, {8,6,2}, {-3,8,2} };
        for (uint8_t i = 0; i < 5; i++) {
            t.fillCircle(mx + CR[i][0], my + CR[i][1], CR[i][2], pit);
            t.fillCircle(mx + CR[i][0] - 1, my + CR[i][1] - 1, CR[i][2] - 1, rim);
        }
        // Limb shading down the trailing edge so it reads as a sphere.
        t.drawCircle(mx, my, 15, blend(moon, skyHere, 60));
        t.drawCircle(mx, my, 14, blend(moon, skyHere, 30));
    }

    // ---- far plane: ridge, then small trees on it --------------------
    const uint16_t haze = skyLow;
    // Distant snow stays SNOW. This blended 120/256 toward the sky, which
    // is unobjectionable at night and turns the whole far slope PINK the
    // moment dawn warms the horizon. Snow takes a little of the light but
    // it does not become the light, so the haze is weaker and is pulled
    // most of the way to white before it is mixed in.
    const uint16_t farSnow = blend(t.color565(196, 214, 236),
                                   blend(haze, WHITE, 160), 70);
    t.fillRect(0, ridgeY, w, yBot - ridgeY, farSnow);
    // The lodge sits ON the ridge line and travels with the far plane, so
    // it obeys the same parallax rule as everything else: small, high,
    // and slow. It is off screen for most of a couple of minutes, which
    // is what makes it read as a landmark rather than as scenery.
    {
        const int lx = (int)s_lodgeX;
        if (lx > -40 && lx < w + 40) {
            snowLodge(t, lx, ridgeY + 2, now, dawn, haze);
            publishLodge(lx, ridgeY + 2, now);
        }
    }
    if (step) for (uint8_t i = 0; i < TREE_FAR; i++) {
        s_far[i].x -= WORLD_SPD * PL_FAR * ds;
        if (s_far[i].x < -20.0f) { s_far[i].x = (float)(w + 18); s_far[i].a = (uint8_t)random(18, 28); }
    }
    for (uint8_t i = 0; i < TREE_FAR; i++)
        snowPine(t, (int)s_far[i].x, ridgeY + 2, (int)s_far[i].a, inten > 0.4f, haze, 150,
                 wind * 1.1f, 0);

    // ---- the slope ---------------------------------------------------
    const uint16_t snowLit = blend(WHITE, VAPOR_BLUE, (uint16_t)(16 + inten * 26.0f));
    const uint16_t snowDim = blend(snowLit, VAPOR_BLUE, 74);
    const uint16_t trackC  = blend(snowDim, t.color565(120, 160, 205), 150);
    const int colW = (w + SNOW_COLS - 1) / SNOW_COLS;
    for (uint8_t i = 0; i < SNOW_COLS; i++) {
        const int x0 = i * w / SNOW_COLS, gy = groundAt(x0 + colW / 2);
        if (gy >= yBot) continue;
        t.fillRect(x0, gy, colW, 3, snowLit);
        if (yBot - gy > 3) t.fillRect(x0, gy + 3, colW, yBot - gy - 3, snowDim);
        if (s_trackD[i]) t.fillRect(x0, gy + 4, colW, 2, trackC);
    }
    if (yEnd > yBot) t.fillRect(0, yBot, w, yEnd - yBot, BG);

    // ---- action plane: props, then trees -----------------------------
    if (step) {
        for (uint8_t i = 0; i < PROP_N; i++) {
            s_prop[i].x -= WORLD_SPD * PL_ACT * ds;
            if (s_prop[i].x < -30.0f) { s_prop[i].x = (float)(w + 28); s_prop[i].b = (uint8_t)random(0, 3); }
        }
        for (uint8_t i = 0; i < TREE_ACT; i++) {
            s_act[i].x -= WORLD_SPD * PL_ACT * ds;
            if (s_act[i].x < -34.0f) {
                s_act[i].x = (float)(w + 32);
                s_act[i].a = (uint8_t)random(52, 70);
                s_act[i].b = (uint8_t)random(0, 60);
            }
            // Snow load. Each tree catches at its own rate, so they never
            // whiten together, and a loaded one sheds the lot at once --
            // which is what actually happens, and reads far better than
            // fading back down would.
            int ld = (int)s_act[i].b + (int)(inten * 1.7f * ds);
            if (ld > 232 && random(0, 100) < 3) ld = (int)random(0, 40);   // dumps
            if (inten < 0.2f) ld -= (int)(0.6f * ds);                      // slow melt
            s_act[i].b = (uint8_t)(ld < 0 ? 0 : (ld > 255 ? 255 : ld));
        }
        s_lodgeX -= WORLD_SPD * PL_FAR * ds;
        if (s_lodgeX < -60.0f) {
            s_lodgeX = (float)(w + 90 + random(0, 900));
            s_lodgeKnocks = 0;          // a fresh lodge, and a fresh count
        }
        s_nearX -= WORLD_SPD * PL_NEAR * ds;
        if (s_nearX < -60.0f) s_nearX = (float)(w + 60 + random(0, 700));
    }
    for (uint8_t i = 0; i < PROP_N; i++) {
        const int px = (int)s_prop[i].x;
        if (px < -30 || px > w + 30) continue;
        const int gy = groundAt(px) + 2;
        if (s_prop[i].b == 0) {
            t.fillRect(px - 7, gy - 8, 15, 8, t.color565(93, 106, 118));
            t.fillRect(px - 5, gy - 10, 9, 3, t.color565(123, 137, 150));
        } else if (s_prop[i].b == 1) {
            t.fillRect(px - 4, gy - 8, 9, 8, t.color565(96, 62, 30));
            t.fillRect(px - 4, gy - 9, 9, 2, t.color565(132, 88, 46));
        } else {
            // A jump. Wedge rising to the right, because that is the way
            // he is going -- a ramp facing the wrong way is a wall.
            for (int k = 0; k < 14; k++)
                t.fillRect(px - 7 + k, gy - 1 - k, 1, k + 2, snowLit);
            t.fillRect(px + 5, gy - 15, 3, 3, snowDim);
        }
    }
    for (uint8_t i = 0; i < TREE_ACT; i++) {
        const int px = (int)s_act[i].x;
        if (px < -34 || px > w + 34) continue;
        snowPine(t, px, groundAt(px) + 2, (int)s_act[i].a, inten > 0.4f, haze, 30,
                 wind * 3.4f, s_act[i].b);
    }

    // The gondola line is gone. It went from five small cabins to two
    // large ones with passengers inside, and at every size in between it
    // read as a separate scene happening above the hill rather than as
    // part of it -- a horizontal thing crossing a screen whose whole
    // subject is a slope running the other way. The sky carries weather,
    // stars and a moon, and that is enough for it to do.

    // ---- the riders --------------------------------------------------
    if (step) {
        for (uint8_t k = 0; k < RIDER_N; k++) {
            Rider& r = s_rid[k];
            if (!r.live) {
                if (now >= r.next) {
                    r.live = true; r.x = -26.0f; r.air = 0.0f; r.vy = 0.0f; r.trick = 0;
                    r.crashAt = 0; r.say = nullptr; r.sayTil = 0;
                    // Re-rolled on every run, so the mix keeps changing
                    // instead of the same one always being the boarder.
                    r.kind = (uint8_t)((random(0, 100) < 40) ? 1 : 0);
                    r.var  = (uint8_t)random(0, 3);
                }
                continue;
            }
            // A board runs a shade slower than skis, and each rider has
            // his own pace on top of that. It is a small thing and it is
            // the whole difference between three riders and one rider
            // drawn three times.
            // Down in the snow: he does not move at all, which is the
            // point of it. The bubble comes up once the tumble is over.
            if (r.crashAt) {
                const uint32_t ca = now - r.crashAt;
                if (ca > CRASH_TUMBLE && !r.say) {
                    r.say    = CRASH_SAY[random(0, 8)];
                    r.sayTil = now + CRASH_HOLD - CRASH_TUMBLE + 200u;
                }
                if (ca > CRASH_TUMBLE + CRASH_HOLD) { r.crashAt = 0; r.say = nullptr; }
                continue;                       // no movement, no jumps, no carving
            }
            const float wasX = r.x;             // for the crossing test below
            r.x += ((r.kind ? 0.70f : 0.82f) + (float)k * 0.06f) * ds;
            if (r.air > 0.0f || r.vy != 0.0f) {
                r.vy  += 0.105f * ds;
                r.air -= r.vy * ds;
                // A spin or a flip advances through its four frames while
                // he is off the ground.
                if (r.trick >= 3) r.trick = (uint8_t)(3 + (((int)(r.air) / 7) & 3));
                if (r.air <= 0.0f) {
                    // Touchdown. One landing in four goes wrong, and the
                    // harder the trick the likelier that is -- a spin has
                    // him facing the wrong way when the snow arrives.
                    const long odds = (r.trick >= 3) ? 34 : 20;
                    r.air = 0.0f; r.vy = 0.0f;
                    if (random(0, 100) < odds) {
                        r.crashAt = now;
                        r.say = nullptr;
                        s_birdAt = now;                 // the birds go again
                        s_birdX  = r.x;
                        s_birdY  = (float)(groundAt((int)r.x) - 26);
                    }
                    r.trick = 0;
                }
            } else {
                for (uint8_t i = 0; i < PROP_N; i++) {
                    if (s_prop[i].b != 2) continue;
                    // A CROSSING test, not a proximity one. The ramp moves
                    // left while he moves right, so they close at about
                    // 3px a frame at 22fps -- and on a long frame, where
                    // ds is clamped at 12.5, at fourteen. A +/-3px window
                    // gets stepped clean over and the jump silently never
                    // happens, which is exactly what it was doing.
                    const float px = s_prop[i].x;
                    if (wasX <= px && r.x >= px - 1.0f) {
                        // Big launch, light gravity: eight frames of air
                        // is not long enough to read a trick at all.
                        r.vy = -3.4f; r.air = 0.1f;
                        r.trick = (uint8_t)random(1, 4);   // grab, method or spin
                        s_birdAt = now;                    // startles the birds
                        s_birdX  = r.x;
                        s_birdY  = (float)(groundAt((int)r.x) - 30);
                        break;
                    }
                }
            }
            // Carving cuts a track into the bank wherever he actually is.
            const int c = (int)r.x * SNOW_COLS / (w > 0 ? w : 1);
            if (c >= 0 && c < SNOW_COLS && r.air < 2.0f) s_trackD[c] = 26;
            if (r.x > (float)w + 28.0f) {
                r.live = false;
                r.next = now + (uint32_t)random(5000, 15000);
                if (s_chaseTgt == (int8_t)k) s_chaseTgt = -1;   // he got away
            }
        }
    }
    for (uint8_t k = 0; k < RIDER_N; k++) {
        const Rider& r = s_rid[k];
        if (!r.live) continue;
        const int sx = (int)r.x;
        if (sx < -28 || sx > w + 28) continue;
        const int gy = groundAt(sx) - (int)r.air;
        if (r.crashAt) { snowWreck(t, sx, groundAt(sx), now - r.crashAt, r.kind, r.var); continue; }
        const float cs = sinf(r.x * 0.06f + (float)k * 1.7f);
        const int8_t carve = (cs > 0.0f) ? 1 : -1;
        // Powder first, so it comes off from BEHIND him rather than
        // spraying over his own boots.
        if (r.air < 1.0f) snowSpray(t, sx, groundAt(sx), carve, fabsf(cs), now);
        if (r.kind) snowBoarder(t, sx, gy, carve, r.trick, r.var);
        else        snowSkier  (t, sx, gy, carve, r.trick, r.var);
    }

    // ---- the chase ---------------------------------------------------
    if (step) {
        const uint32_t age = now - s_cAt;
        switch (s_cSt) {
            case ChaseSt::NONE: {
                // Dawn keeps him off the hill entirely.
                // Pick a rider: the one furthest along who still has most
                // of the hill left, so the chase has somewhere to happen.
                int8_t pick = -1;
                if (now >= s_cNext && dawn < 0.15f) {
                    for (uint8_t k = 0; k < RIDER_N; k++)
                        if (s_rid[k].live && s_rid[k].x < (float)w * 0.45f
                            && (pick < 0 || s_rid[k].x > s_rid[pick].x)) pick = (int8_t)k;
                }
                if (pick >= 0) {
                    s_chaseTgt = pick;
                    const long r = random(0, 100);
                    // Most of the time the skier gets away. The eat is the
                    // rare one, which is the only thing that makes it land.
                    s_cEnd = (r < 30) ? ChaseEnd::GIVEUP
                           : (r < 48) ? ChaseEnd::WINDED
                           : (r < 66) ? ChaseEnd::TRIP
                           : (r < 84) ? ChaseEnd::BEATEN
                                      : ChaseEnd::EAT;
                    s_cSt = ChaseSt::RUN; s_cAt = now; s_yX = s_rid[pick].x - 74.0f;
                    // He announces himself; the skier answers a beat
                    // later, from the RUN case below.
                    s_sayYetTxt = YETI_TAUNT[random(0, 6)];
                    s_sayYetTil = now + 1500u;
                    s_sayNextAt = now + 1100u;
                }
                break;
            }
            case ChaseSt::RUN: {
                if (s_chaseTgt < 0) { s_cSt = ChaseSt::LEAVE; s_cAt = now; break; }
                Rider& tg = s_rid[s_chaseTgt];
                // They trade lines the whole way down, alternating, so it
                // reads as an argument rather than as two monologues.
                if (now >= s_sayNextAt) {
                    if ((s_sayNextAt & 1u) != 0u) {
                        s_saySkiTxt = SKI_TAUNT[random(0, 6)];  s_saySkiTil = now + 1500u;
                    } else {
                        s_sayYetTxt = YETI_TAUNT[random(0, 6)]; s_sayYetTil = now + 1500u;
                    }
                    s_sayNextAt = now + (uint32_t)random(1300, 2100);
                }
                // A yeti who is going to lose runs slower the whole way,
                // rather than teleporting to a decision at the last frame.
                const bool fast = (s_cEnd == ChaseEnd::EAT || s_cEnd == ChaseEnd::TRIP
                                   || s_cEnd == ChaseEnd::BEATEN);
                s_yX += (fast ? 1.05f : 0.66f) * ds;
                if (s_cEnd == ChaseEnd::WINDED && age > 2600u) { s_cSt = ChaseSt::RESOLVE; s_cAt = now; }
                else if (!tg.live || s_yX > (float)w + 34.0f) { s_cSt = ChaseSt::LEAVE; s_cAt = now; }
                else if (fast && s_yX > tg.x - 13.0f) {
                    if (s_cEnd == ChaseEnd::EAT) {
                        tg.live = false;
                        tg.next = now + (uint32_t)random(9000, 20000);
                        s_eatAt = now;
                    }
                    s_cSt = ChaseSt::RESOLVE; s_cAt = now;
                }
                break;
            }
            case ChaseSt::RESOLVE:
                if (age < 60u) {
                    // The punchline, and who gets it, depends on how it
                    // went. The loser generally says less.
                    switch (s_cEnd) {
                        case ChaseEnd::WINDED:
                            s_sayYetTxt = SAY_WINDED[random(0, 2)]; s_sayYetTil = now + 1800u;
                            s_saySkiTxt = nullptr; break;
                        case ChaseEnd::TRIP:
                            s_sayYetTxt = "OOF"; s_sayYetTil = now + 1500u;
                            s_saySkiTxt = "HA";  s_saySkiTil = now + 1500u; break;
                        case ChaseEnd::BEATEN:
                            s_saySkiTxt = SAY_BEATEN[random(0, 2)]; s_saySkiTil = now + 1600u;
                            s_sayYetTxt = "ow";  s_sayYetTil = now + 1600u; break;
                        case ChaseEnd::EAT:
                            // Just the grab here. The rest of it is timed
                            // against the sequence, below.
                            s_saySkiTxt = "urk"; s_saySkiTil = now + 600u;
                            s_sayYetTxt = nullptr; break;
                        default:
                            s_sayYetTxt = SAY_GIVEUP[random(0, 2)]; s_sayYetTil = now + 1600u;
                            s_saySkiTxt = nullptr; break;
                    }
                }
                // The inspection has its own two beats to speak on, so it
                // does not get everything said at the top like the others.
                if (s_cEnd == ChaseEnd::EAT) {
                    if (age >= EAT_RAISE && age < EAT_RAISE + 70u) {
                        s_sayYetTxt = EAT_LOOK_Y[random(0, 4)]; s_sayYetTil = now + 1000u;
                        s_saySkiTxt = EAT_LOOK_S[random(0, 4)]; s_saySkiTil = now + 1000u;
                    } else if (age >= EAT_TURN && age < EAT_TURN + 70u) {
                        s_sayYetTxt = EAT_BITE_Y[random(0, 4)]; s_sayYetTil = now + 1300u;
                        s_saySkiTxt = EAT_BITE_S[random(0, 4)]; s_saySkiTil = now + 800u;
                    }
                }
                if (s_cEnd == ChaseEnd::BEATEN && age < 900u) s_yX -= 0.9f * ds;   // knocked back
                if (age > (s_cEnd == ChaseEnd::EAT ? EAT_TOTAL : 1900u)) { s_cSt = ChaseSt::LEAVE; s_cAt = now; }
                break;
            case ChaseSt::LEAVE:
                s_yX -= 1.5f * ds;
                if (s_yX < -50.0f) {
                    s_cSt = ChaseSt::NONE; s_chaseTgt = -1;
                    s_cNext = now + (uint32_t)random(16000, 34000);
                }
                break;
        }
    }
    if (s_cSt != ChaseSt::NONE) {
        const int yx = (int)s_yX;
        if (yx > -34 && yx < w + 34) {
            YPose p = YPose::RUN;
            if (s_cSt == ChaseSt::RESOLVE || (s_cSt == ChaseSt::LEAVE && now - s_cAt < 400u)) {
                p = (s_cEnd == ChaseEnd::EAT)    ? YPose::EAT
                  : (s_cEnd == ChaseEnd::TRIP)   ? YPose::DOWN
                  : (s_cEnd == ChaseEnd::BEATEN) ? YPose::RECOIL
                  : (s_cEnd == ChaseEnd::WINDED) ? YPose::WINDED : YPose::RUN;
            }
            if (p == YPose::EAT) {
                // s_eatAt, not s_cAt: this pose survives the RESOLVE ->
                // LEAVE hand-over, and s_cAt restarts there.
                const uint32_t age = now - s_eatAt;
                const int bulge = (age < EAT_GULP) ? 0
                                : (age < EAT_PAT)  ? (int)(3 + (age - EAT_GULP) / 140u) : 5;
                // Where his hands are through the sequence: down at the
                // snow for the grab, up at eye level and out in front for
                // the look, then back in toward the mouth to feed him in.
                int hdx = 4, hdy = 24;
                if (age < EAT_GRAB) { hdx = 2; hdy = 24; }
                else if (age < EAT_RAISE) {
                    const uint32_t d = age - EAT_GRAB, span = EAT_RAISE - EAT_GRAB;
                    hdy = 24 - (int)(d * 16u / span);
                    hdx = 2  + (int)(d *  8u / span);
                } else if (age < EAT_TURN) { hdx = 10; hdy = 8; }
                else if (age < EAT_CHOMP) {
                    const uint32_t d = age - EAT_TURN, span = EAT_CHOMP - EAT_TURN;
                    hdx = 10 - (int)(d * 9u / span);
                    hdy = 8  - (int)(d * 6u / span);
                } else { hdx = 1; hdy = 2; }
                // The mouth stays SHUT until he has made his mind up. Using
                // the EAT pose the whole way through had him examining the
                // rider with his jaw already hanging open, which gives the
                // ending away and wastes the pause.
                const YPose face = (age < EAT_TURN) ? YPose::RUN
                                 : ((age < EAT_GULP) ? YPose::EAT : YPose::RUN);
                const bool hold = (age < EAT_CHOMP);
                // A slight lean in while he is looking. Curiosity, and it
                // stops the held beat being completely static.
                snowYeti(t, yx, groundAt(yx), now, face, bulge, hold, hdx, hdy);
                const uint8_t vk = (s_chaseTgt >= 0) ? s_rid[s_chaseTgt].kind : 0;
                const uint8_t vv = (s_chaseTgt >= 0) ? s_rid[s_chaseTgt].var  : 0;
                snowVictim(t, yx, groundAt(yx), age, now, vk, vv);
                if (age >= EAT_GULP) {
                    // A hand on the belly. One rectangle, and it lands.
                    const int py = groundAt(yx) - 26 + (int)(sinf((float)now / 110.0f) * 2.0f);
                    t.fillRect(yx - 3, py, 8, 4, t.color565(232, 237, 243));
                }
            } else {
                snowYeti(t, yx, groundAt(yx), now, p);
            }
            // The skier turns and swings for himself.
            if (p == YPose::RECOIL && s_chaseTgt >= 0 && s_rid[s_chaseTgt].live) {
                const int sx = (int)s_rid[s_chaseTgt].x;
                t.drawLine(sx + 4, groundAt(sx) - 22, yx - 8, groundAt(yx) - 30,
                           t.color565(58, 64, 73));
            }
        }
    }

    // ---- what they are shouting at each other ------------------------
    // After both of them, so a bubble is never behind a body -- and
    // before the foreground tree, so it can still be occluded by it.
    // Crash bubbles first: they belong to a specific rider rather than to
    // the chase, and one of them can be up while a chase is running
    // somewhere else on the hill.
    for (uint8_t k = 0; k < RIDER_N; k++) {
        const Rider& r = s_rid[k];
        if (!r.live || !r.say || now >= r.sayTil) continue;
        const int sx = (int)r.x;
        if (sx > 8 && sx < w - 8) snowSay(t, sx, groundAt(sx) - 22, r.say);
    }
    // During the eat the rider is in the yeti's HANDS, so his bubble
    // cannot come from where he was standing -- and he is not live any
    // more either, which is what used to silence him entirely for the
    // one sequence where he has the best lines.
    const bool eating = (s_cSt == ChaseSt::RESOLVE && s_cEnd == ChaseEnd::EAT);
    if (s_saySkiTxt && now < s_saySkiTil) {
        int sx = -100, sy = 0;
        if (eating) { sx = (int)s_yX + 20; sy = groundAt((int)s_yX) - 62; }
        else if (s_chaseTgt >= 0 && s_rid[s_chaseTgt].live) {
            sx = (int)s_rid[s_chaseTgt].x; sy = groundAt(sx) - 34;
        }
        if (sx > 10 && sx < w - 10) snowSay(t, sx, sy, s_saySkiTxt);
    }
    if (s_sayYetTxt && now < s_sayYetTil && s_cSt != ChaseSt::NONE) {
        const int yx = (int)s_yX;
        // Pushed left and up during the eat so the two bubbles do not
        // land on top of each other while they are both at the yeti.
        const int bx = eating ? yx - 18 : yx;
        const int by = groundAt(yx) - (eating ? 78 : 56);
        if (bx > 10 && bx < w - 10) snowSay(t, bx, by, s_sayYetTxt);
    }

    // ---- near plane: one big tree, fast, low -------------------------
    {
        const int px = (int)s_nearX;
        if (px > -60 && px < w + 60)
            snowPine(t, px, yBot + 6, 96, inten > 0.4f, haze, 0, wind * 5.0f, 190);
    }

    // ---- wildlife ----------------------------------------------------
    if (step) {
        // The dog runs with the skier if he is close, otherwise trots.
        bool alongside = false;
        for (uint8_t k = 0; k < RIDER_N; k++)
            if (s_rid[k].live && fabsf(s_rid[k].x - s_dogX) < 46.0f) alongside = true;
        s_dogX += (alongside ? 0.80f : 0.30f) * ds;
        if (s_dogX > (float)w + 34.0f) s_dogX = -34.0f - (float)random(0, 1500);
        // The hare wants nothing to do with any of this.
        s_hareX -= ((s_cSt != ChaseSt::NONE) ? 1.7f : 0.55f) * ds;
        if (s_hareX < -30.0f) s_hareX = (float)w + 30.0f + (float)random(0, 2200);
    }
    if (s_dogX > -24.0f && s_dogX < (float)w + 24.0f) {
        const int dx = (int)s_dogX, dy = groundAt(dx);
        const uint16_t dog = t.color565(181, 112, 40), dogD = t.color565(132, 78, 26);
        const int gait = (int)(sinf((float)now / 95.0f) * 2.0f);
        t.fillRect(dx - 8, dy - 10, 15, 7, dog);
        t.fillRect(dx + 5, dy - 15, 8, 7, dog);              // head
        t.fillRect(dx + 11, dy - 16, 3, 4, dogD);            // ear
        t.fillRect(dx + 12, dy - 11, 3, 2, t.color565(40, 30, 24));
        t.fillRect(dx - 11, dy - 13, 4, 5, dogD);            // tail, up
        t.fillRect(dx - 6, dy - 4 + gait, 3, 4, dogD);
        t.fillRect(dx + 2, dy - 4 - gait, 3, 4, dogD);
    }
    if (s_hareX > -22.0f && s_hareX < (float)w + 22.0f) {
        const int hx = (int)s_hareX;
        const int hop = (int)(fabsf(sinf((float)now / 120.0f))
                              * ((s_cSt != ChaseSt::NONE) ? 9.0f : 4.0f));
        const int hy2 = groundAt(hx) - hop;
        t.fillRect(hx - 5, hy2 - 6, 10, 5, WHITE);
        t.fillRect(hx + 4, hy2 - 9, 5, 5, WHITE);            // head
        t.fillRect(hx + 6, hy2 - 14, 2, 6, WHITE);           // ears, laid back
        t.fillRect(hx + 3, hy2 - 14, 2, 6, WHITE);
        t.fillRect(hx - 6, hy2 - 3, 3, 3, WHITE);            // scut
        t.fillRect(hx + 7, hy2 - 8, 2, 2, t.color565(180, 60, 60));
    }
    if (s_birdAt && now - s_birdAt < 2600u) {
        // Startled off a pine by the jump. They climb and scatter.
        const float ba = (float)(now - s_birdAt) / 2600.0f;
        for (uint8_t i = 0; i < 5; i++) {
            const int bx = (int)(s_birdX + ba * (28.0f + (float)i * 13.0f));
            const int by = (int)(s_birdY - ba * (30.0f + (float)i * 6.0f)
                                 + sinf((float)now / 90.0f + (float)i) * 3.0f);
            if (bx < -6 || bx > w + 6 || by < yStart) continue;
            const uint16_t bc = blend(skyLow, BLACK, (uint16_t)(190 * (1.0f - ba)));
            const int flap = (((now / 110) + i) & 1) ? 1 : -1;
            t.fillRect(bx - 3, by, 3, 1, bc);
            t.fillRect(bx + 1, by, 3, 1, bc);
            t.fillRect(bx - 3, by - flap, 2, 1, bc);
            t.fillRect(bx + 2, by - flap, 2, 1, bc);
        }
    } else if (s_birdAt && now - s_birdAt >= 2600u) {
        if (step) s_birdAt = 0;
    }

    // ---- snow --------------------------------------------------------
    const uint8_t live = (uint8_t)(SNOW_N * (0.18f + 0.82f * inten));
    for (uint8_t i = 0; i < live; i++) {
        SnowFlake& f = s_flk[i];
        const uint8_t L = f.layer;
        if (step) {
            const float sp = (L == 0) ? 1.7f : (L == 1) ? 1.05f : 0.6f;
            f.y += sp * (0.55f + inten * 1.5f) * ds;
            f.x += wind * (1.5f + (float)(2 - L) * 0.5f) * (0.4f + inten) * ds;
            if (f.y > (float)yBot) { f.y = (float)yStart; f.x = (float)random(0, w); }
            if (f.x < -3.0f) f.x = (float)(w + 2); else if (f.x > (float)(w + 3)) f.x = -2.0f;
        }
        const uint16_t c = blend(skyLow, WHITE, (uint16_t)(L == 0 ? 235 : L == 1 ? 165 : 105));
        if (L == 0) t.fillRect((int)f.x, (int)f.y, 2, 2 + (int)(fabsf(wind) * 3.0f), c);
        else        t.drawPixel((int)f.x, (int)f.y, c);
    }
}

// The RF spectrum screen, rebuilt as a run down a corridor of mainframe towers
// -- the Gibson from Hackers, which is also what talkingsasquach.com throws up
// when you type "hack the planet" into its terminal. That version rotates a
// field of 120 gradient-filled buildings and smears the frame with a
// translucent black rect every tick. Two of those three things are affordable
// here.
//
// What makes it cheap at all is that a tower is a BILLBOARD: positioned in 3D
// and scaled by depth, but drawn as an axis-aligned rectangle. A whole building
// is two fillRects and three fast lines. Perspective side faces would mean
// diagonals per tower, and filled ones would mean a polygon scanline per tower.
//
// Two things that look like obvious wins are traps on this panel:
//
//   * Fading distant towers by dimming them. RGB332 gives red and green eight
//     levels and blue four, so anything under roughly 15% brightness quantises
//     to black -- a "subtle" far tower is not subtle, it is absent. Distance is
//     carried by the SHADE tone of each tower's own hue instead.
//
//   * The motion-blur trail, which is most of why the browser version glows.
//     That is an alpha blend over every pixel, and with no readable framebuffer
//     it costs a read-modify-write round trip each -- the same cliff that killed
//     the per-pixel dim on the starfield. Left out rather than faked badly.
//
// The trace across the middle is not decoration: x maps across WiFi channels
// 1-13 and its height is that channel's live activity, so real traffic deforms
// it. The slow wander underneath is what keeps the screen alive when nothing is
// on the air, which is nearly always. It is white because the towers own every
// other bright colour on screen -- a green trace disappeared into the green
// buildings, and the instrument has to stay separable from the scenery.
static const char* const GIB_FEED[] = {
    "ACCESS GRANTED",   "GIBSON MAINFRAME", "ELLINGSON MINERAL", "ZERO COOL ONLINE",
    "ACID BURN ONLINE", "TRACING... FAILED", "WORM DEPLOYED",    "UPLINK: ACTIVE",
    "DA VINCI VIRUS",   "COOKIE: YOURS",     "GARBAGE FILE LIVE", "THE PLAGUE SEEN"
};
static const uint8_t  GIB_FEED_N = 12;

static const uint8_t  GIB_N    = 28;      // towers in the ring
static_assert(GIB_N % 2 == 0, "towers alternate walls by index parity");
static const uint16_t GIB_SPAN = 1500;    // depth the ring wraps over
static const float    GIB_NEAR = 190.0f;  // reference depth: sizes below are quoted here
static const float    GIB_PASS =  30.0f;  // how close a tower gets before it recycles
static const uint8_t  GIB_CITY = 16;      // slots for the still rear skyline
static const float    GIB_CAMY =  70.0f;  // camera height above the floor
static const float    GIB_SPEED =  0.20f; // world units per millisecond

void drawGibson(TFT_eSPI& t, uint32_t now, int yStart, int yEnd,
                const DetectionEngine& eng) {
    const int w = t.width();
    if (yEnd - yStart < 48) return;

    // CLEAR paints its counter block on top of this band afterwards, so
    // anything laid out against yEnd ends up underneath the numbers -- the same
    // trap that had Mowin' Man mowing under the readout. s_bgFloor is where the
    // drawable area really stops; the band is still FILLED to yEnd so nothing
    // shows through beside the counters.
    const int yBot = (s_bgFloor > yStart + 40 && s_bgFloor <= yEnd) ? s_bgFloor : yEnd;
    const int bandH = yBot - yStart;
    if (bandH < 48) return;

    // Building colours are derived from the live palette rather than fixed
    // hex. Hardcoded constants ignored dimPaletteForOverlay() completely, which
    // left a full-brightness city glaring out from under the Settings menu
    // while every other background politely faded. Deriving them also means the
    // city follows whichever theme the user picked, like everything else here.
    //
    // In the default vaporwave palette these land on the site's own three
    // hues. CYAN and GREEN sit far enough apart to survive RGB332, which is
    // what the site's 190 and 160 did not -- those quantised to nearly the
    // same teal and left the city two-tone.
    const uint16_t face[3]  = { CYAN, VAPOR_PURPLE, GREEN };
    const uint16_t shade[3] = { blend(face[0], BG, 185),
                                blend(face[1], BG, 185),
                                blend(face[2], BG, 185) };
    const uint16_t edge[3]  = { blend(face[0], WHITE, 70),
                                blend(face[1], WHITE, 70),
                                blend(face[2], WHITE, 70) };
    const uint16_t win[3]   = { blend(face[0], WHITE, 175),
                                blend(face[1], WHITE, 175),
                                blend(face[2], WHITE, 175) };
    const uint16_t gridNear = blend(GREEN, BG,  70);
    const uint16_t gridFar  = blend(GREEN, BG, 175);
    // The trace has to stay separable from the scenery, so it is the brightest
    // thing on screen -- but derived from CYAN rather than literal white, so it
    // fades with the rest when the palette is dimmed for an overlay.
    const uint16_t traceLit = blend(CYAN, WHITE, 150);

    const int   hz     = yStart + bandH * 52 / 100;
    const float fHz    = (float)hz;
    const float floorH = (float)(yBot - 1 - hz);
    if (floorH < 10.0f) return;

    // The projection is derived from the band rather than fixed, because this
    // background is handed a different height on every screen -- CLEAR reserves
    // room for counters, LOG does not. With a constant focal length the towers
    // were composed for one of them and overflowed the others. Choosing f so
    // the nearest tower's base lands just past the bottom edge makes the whole
    // scene scale-invariant: tower sizes below are fractions of w and bandH,
    // and they hold whatever band we are given.
    const float f     = floorH * 1.15f * GIB_NEAR / GIB_CAMY;
    const float xUnit = (float)w     * GIB_NEAR / f;   // world units per screen width
    const float hUnit = (float)bandH * GIB_NEAR / f;   // world units per band height

    // ---- the corridor, laid out once ---------------------------------
    // xf/wf are thousandths of the panel width and hf thousandths of the band
    // height, measured at closest approach.
    struct Tw { int16_t xf; uint16_t wf, hf, z0; uint8_t hue; };
    static Tw   tw[GIB_N];
    static bool inited = false;
    if (!inited) {
        for (uint8_t i = 0; i < GIB_N; i++) {
            const uint32_t r = (uint32_t)(i + 1) * 2654435761u;
            tw[i].xf  = (int16_t) (((i & 1u) ? 1 : -1) * (int)(340 + ((r >>  3) % 380)));
            tw[i].wf  = (uint16_t)( 85 + ((r >> 11) % 105));
            tw[i].hf  = (uint16_t)(520 + ((r >> 17) % 900));
            tw[i].z0  = (uint16_t)((uint32_t)i * GIB_SPAN / GIB_N);
            tw[i].hue = (uint8_t) (       (r >> 25) %  3);
        }

        // No more than two towers of the same colour in a row down either wall.
        // The hash that assigns hues is uniform, not blue-noise, so runs of four
        // and five do turn up, and a wall going solid green for a stretch reads
        // as a palette bug rather than as a city.
        //
        // Walls alternate by index parity, so "in a row on the same side" means
        // i, i+2, i+4 -- and the ring wraps, so the last tower on a side
        // neighbours the first. The pass has to close the loop, not just walk
        // it, which is why the indices are taken modulo the per-side count.
        //
        // The replacement hue comes from the tower's OWN hash rather than from
        // its position. Breaking runs with an index-parity rule removes the runs
        // and leaves a 1,1,2,2,0,0 stripe marching down the wall in their place,
        // which is a more obviously artificial artefact than the one it fixed.
        for (uint8_t side = 0; side < 2; side++) {
            const uint8_t cnt = GIB_N / 2;
            for (uint8_t pass = 0; pass < 3; pass++) {
                bool clean = true;
                for (uint8_t k = 0; k < cnt; k++) {
                    const uint8_t i0 = (uint8_t)(((k + cnt - 2) % cnt) * 2 + side);
                    const uint8_t i1 = (uint8_t)(((k + cnt - 1) % cnt) * 2 + side);
                    const uint8_t i2 = (uint8_t)(k * 2 + side);
                    if (tw[i0].hue != tw[i1].hue || tw[i1].hue != tw[i2].hue) continue;
                    const uint32_t rr = (uint32_t)(i2 + 1) * 2654435761u;
                    tw[i2].hue = (uint8_t)((tw[i1].hue + 1u + ((rr >> 9) & 1u)) % 3u);
                    clean = false;
                }
                if (clean) break;
            }
        }
        inited = true;
    }

    // Frame-rate independent flight, with the same dt clamp the starfield uses
    // so one stalled frame does not teleport the camera down the corridor.
    static uint32_t lastMs = 0;
    static float    flyZ   = 0.0f;
    uint32_t dt = (lastMs && now > lastMs) ? (now - lastMs) : 16u;
    if (dt > 100u) dt = 100u;
    lastMs = now;
    flyZ = fmodf(flyZ + (float)dt * GIB_SPEED, (float)GIB_SPAN);

    auto depthOf = [&](uint8_t i) {
        // Recycling at the reference depth made towers pop out of existence
        // while still square in the middle of the screen. Letting them run in to
        // GIB_PASS carries them past the camera instead: the projection throws
        // them sideways far faster than it grows them, so they slide off the
        // edge of the panel and are long gone by the time the ring wraps.
        return fmodf((float)tw[i].z0 - flyZ + (float)GIB_SPAN, (float)GIB_SPAN) + GIB_PASS;
    };
    auto groundY = [&](float z) { return fHz + GIB_CAMY * f / z; };

    // ---- what the corridor is reacting to ----------------------------
    // A lock arms when a genuinely new entry reaches the front of the log.
    // lastSeen ticks on every repeat hit of the same device, so keying off that
    // would re-arm every few hundred ms for as long as something sat in range;
    // firstSeen plus the MAC is what separates "a new thing" from "that thing
    // again".
    static const uint32_t LOCK_MS = 4200;
    static uint32_t lockAt = 0, lockFirst = 0, lockMac = 0;
    static int8_t   lockIdx  = -1;
    static DetectionType lockType = DetectionType::UNKNOWN;

    if (eng.logCount()) {
        const Detection* d = eng.logAt(0);
        if (d) {
            const uint32_t mac4 = ((uint32_t)d->mac[2] << 24) | ((uint32_t)d->mac[3] << 16)
                                | ((uint32_t)d->mac[4] <<  8) |  (uint32_t)d->mac[5];
            if (mac4 != lockMac || d->firstSeen != lockFirst) {
                lockMac   = mac4;
                lockFirst = d->firstSeen;
                lockType  = d->type;
                lockAt    = now ? now : 1u;
                // The target is chosen once, here, so the lock stays on one
                // building for the whole hold. Re-picking the nearest tower
                // every frame would make it hop forward as the city moves,
                // which reads as a glitch rather than as a lock.
                // Not the nearest tower -- the one that will ARRIVE. Picking
                // the closest looked right in a still frame and was wrong in
                // motion: it swept past the camera inside the first second of a
                // four-second hold and spent the rest of the lock as a speck in
                // the distance. Targeting the building one hold-length away
                // instead means it grows the whole time it is lit and fills the
                // screen just as the lock lets go.
                const float want = GIB_NEAR * 0.6f + (float)LOCK_MS * GIB_SPEED;
                float best = 1e9f; int bi = -1;
                for (uint8_t i = 0; i < GIB_N; i++) {
                    const float d = fabsf(depthOf(i) - want);
                    if (d < best) { best = d; bi = i; }
                }
                lockIdx = (int8_t)bi;
            }
        }
    }
    const bool     locked  = lockAt && (now - lockAt) < LOCK_MS;
    const float    lockK   = locked ? (float)(now - lockAt) / (float)LOCK_MS : 1.0f;
    const float    lockA   = 1.0f - lockK * 0.7f;
    const uint16_t lockCol = colorFor(lockType);

    // ---- night sky ---------------------------------------------------
    t.fillRect(0, yStart, w, yEnd - yStart, BG);
    {
        // One deliberate band, not a gradient: with two bits of blue there is no
        // smooth ramp to be had, and a ramp bands anyway at whatever heights the
        // quantiser picks. Better to choose the band.
        //
        // The blend weights here look heavy-handed and are not. Measured, the
        // first attempt at this -- a tasteful 29% -- landed on (0,36,0): the
        // blue channel quantised away completely and a dark blue horizon came
        // out dark GREEN, competing with the floor grid instead of sitting
        // behind it. Blue has four levels on this panel and needs a component
        // of at least 64 to register at all, so a colour that is meant to read
        // as blue has to be chosen by where it lands, not by how restrained the
        // number looks.
        const int glowH = bandH / 8 + 2;
        if (hz - glowH >= yStart)
            t.fillRect(0, hz - glowH, w, glowH, blend(BG, VAPOR_PURPLE, 110));

        // Stars sit at infinity, so they do not travel with the corridor --
        // only twinkle. Anything that scrolls up here would read as the sky
        // being closer than the city, which is the one thing it cannot be.
        const uint16_t starA = traceLit, starB = blend(traceLit, BG, 130);
        const int skyH = hz - yStart - 3;
        if (skyH > 6) {
            for (uint8_t i = 0; i < 44; i++) {
                const uint32_t r = (uint32_t)(i + 7) * 1103515245u;
                const uint8_t  k = (uint8_t)(r & 7u);
                if (k == 7u && ((now / (700u + ((r >> 5) % 900u))) & 1u) == 0u) continue;
                t.drawPixel((int)((r >> 8) % (uint32_t)w),
                            yStart + 1 + (int)((r >> 17) % (uint32_t)skyH),
                            (k > 5u) ? starA : starB);
            }
        }

        // The moon. It sits behind the skyline and behind the corridor, so it
        // is routinely half-eclipsed by whatever passes in front of it -- which
        // is most of what sells it as being far away rather than as a circle
        // stuck on the glass. It also gives the sky glow band something to be
        // the glow of.
        {
            const int mr = (hz - yStart) / 5;
            const int mx = w * 3 / 4 + 6;
            const int my = yStart + (hz - yStart) / 3;
            if (mr >= 6 && my - mr - 2 >= yStart) {
                const uint16_t disc   = blend(VAPOR_YELLOW, WHITE, 130);
                const uint16_t crater = blend(disc, BG, 60);
                t.fillCircle(mx, my, mr + 2, blend(BG, disc, 60));   // halo
                t.fillCircle(mx, my, mr, disc);
                t.fillCircle(mx + mr / 3, my - mr / 3, mr / 4, crater);
                t.fillCircle(mx - mr / 3, my + mr / 5, mr / 3, crater);
                t.fillCircle(mx + mr / 5, my + mr / 2, mr / 5, crater);
            }
        }

        // Two searchlights sweeping the sky. Drawn BEFORE the skyline on
        // purpose: with no alpha to spend, a beam painted over the buildings
        // would simply erase them, so instead the buildings cut off its base and
        // the beam reads as coming up off the rooftops. Three nested triangles
        // give it a bright core without any per-pixel blending.
        //
        // The length is clamped rather than fixed, because the band we are drawn
        // into does not start at the top of the panel: an unclamped beam paints
        // straight through the title bar.
        {
            const uint16_t beam[3] = { blend(BG, CYAN, 96),
                                       blend(BG, CYAN, 66),
                                       blend(BG, CYAN, 44) };
            const float spMax = 0.055f * 3.0f;
            for (uint8_t L = 0; L < 2; L++) {
                const int   bx = L ? (w * 3 / 4) : (w / 5);
                const int   by = hz - (hz - yStart) * 3 / 10;
                const float a  = (L ? 2.20f : 0.95f)
                               + sinf((float)now / 1900.0f + (float)L * 2.1f) * 0.55f;
                float sm = sinf(a);
                const float s1 = sinf(a - spMax), s2 = sinf(a + spMax);
                if (s1 > sm) sm = s1;
                if (s2 > sm) sm = s2;
                if (sm < 0.15f) sm = 0.15f;
                float len = (float)(hz - yStart) * 1.6f;
                const float cap = ((float)by - (float)(yStart + 1)) / sm;
                if (len > cap) len = cap;
                if (len < 8.0f) continue;
                for (int q = 2; q >= 0; q--) {
                    const float sp = 0.055f * (float)(q + 1);
                    t.fillTriangle(bx, by,
                        bx + (int)(cosf(a - sp) * len), by - (int)(sinf(a - sp) * len),
                        bx + (int)(cosf(a + sp) * len), by - (int)(sinf(a + sp) * len),
                        beam[q]);
                }
                t.fillRect(bx - 1, by - 1, 3, 3, blend(CYAN, WHITE, 120));
            }
        }

        // The rear skyline: thin towers with real gaps between them, standing
        // still far behind the corridor. It does not scroll -- at that distance
        // parallax would be indistinguishable from a rendering fault, and
        // holding it still is most of what makes the moving towers read as
        // close. It sits LIGHTER than the sky band rather than darker, because a
        // distant city at night reads as lit rather than as a silhouette, and a
        // silhouette would disappear entirely above the band where the sky is
        // plain black.
        //
        // Laid out once, into per-mille fractions rather than pixels: the three
        // screens that draw this background each hand it a different band
        // height, so anything baked in pixels is right on one of them and wrong
        // on the other two.
        struct CityB { uint16_t xf, wf, hf; uint8_t hue, tone; };
        static CityB  city[GIB_CITY];
        static uint8_t cityN   = 0;
        static bool    cityGen = false;
        if (!cityGen) {
            cityGen = true;
            uint16_t cx = 6;
            for (uint8_t b = 0; b < GIB_CITY; b++) {
                // A bare multiplicative hash leaves neighbouring inputs
                // correlated inside any one bit field -- taking the widths from
                // it produced a near-monotonic ramp, a staircase rather than a
                // skyline. Two xorshift rounds decorrelate the fields.
                uint32_t r = (uint32_t)(b + 11) * 2654435761u;
                r ^= r >> 15; r *= 2246822519u; r ^= r >> 13;
                const uint16_t wf = (uint16_t)(34u + ((r >> 7) % 39u));
                if (cx + wf > 994u) break;
                city[cityN].xf   = cx;
                city[cityN].wf   = wf;
                city[cityN].hf   = (uint16_t)(150u + ((r >> 19) % 851u));
                city[cityN].tone = (uint8_t)((r >> 3) & 1u);
                cx = (uint16_t)(cx + wf + 12u + ((r >> 15) % 19u));
                cityN++;
            }
            // Rolling a hue per building leaves the colours badly skewed by
            // chance. Measured, this layout gave five teal, five green, two rose
            // and a SINGLE purple across thirteen buildings -- which is most of
            // why the purple did not read as a colour in the skyline at all.
            // Deal a balanced set and shuffle it instead, so each hue gets its
            // fair share of the row.
            for (uint8_t b = 0; b < cityN; b++) city[b].hue = (uint8_t)(b & 3u);
            for (uint8_t b = cityN; b > 1; b--) {
                uint32_t r = (uint32_t)(b + 41) * 2654435761u;
                r ^= r >> 15; r *= 2246822519u; r ^= r >> 13;
                const uint8_t j   = (uint8_t)(r % b);
                const uint8_t tmp = city[b - 1].hue;
                city[b - 1].hue = city[j].hue;
                city[j].hue     = tmp;
            }

            // No more than two of the same colour side by side, for the same
            // reason as the corridor: a uniform hash throws runs, and three
            // identical blocks in a row read as a mistake rather than as a city.
            // One forward pass is enough here -- unlike the corridor this row
            // does not wrap, and each fix is read back by the next check.
            for (uint8_t b = 2; b < cityN; b++) {
                if (city[b].hue != city[b - 1].hue ||
                    city[b - 1].hue != city[b - 2].hue) continue;
                uint32_t r = (uint32_t)(b + 29) * 2654435761u;
                r ^= r >> 15; r *= 2246822519u; r ^= r >> 13;
                city[b].hue = (uint8_t)((city[b].hue + 1u + (r % 3u)) % 4u);
            }

            // Neighbours have to differ in height by a wide margin, or the row
            // reads as a hedge. Generated freely, then any pair that landed too
            // close is pushed apart -- toward whichever side still has room.
            for (uint8_t pass = 0; pass < 4; pass++) {
                bool clean = true;
                for (uint8_t b = 1; b < cityN; b++) {
                    const int prev = (int)city[b - 1].hf;
                    int       cur  = (int)city[b].hf;
                    const int d    = (cur > prev) ? (cur - prev) : (prev - cur);
                    if (d >= 260) continue;
                    clean = false;
                    uint32_t r = (uint32_t)(b + 11) * 2654435761u;
                    r ^= r >> 15; r *= 2246822519u; r ^= r >> 13;
                    const int off = 260 + (int)((r >> 21) % 140u);
                    const int up  = prev + off, dn = prev - off;
                    cur = (cur >= prev) ? ((up <= 1000) ? up : dn)
                                        : ((dn >=  150) ? dn : up);
                    if (cur > 1000) cur = 1000;
                    if (cur <  150) cur =  150;
                    city[b].hf = (uint16_t)cur;
                }
                if (clean) break;
            }
            // Scale so the tallest reaches the very top of the sky band, just
            // under the readout. Scaling up can only widen the neighbour gaps,
            // so the contrast rule survives it.
            uint16_t top = 0;
            for (uint8_t b = 0; b < cityN; b++) if (city[b].hf > top) top = city[b].hf;
            if (top)
                for (uint8_t b = 0; b < cityN; b++)
                    city[b].hf = (uint16_t)((uint32_t)city[b].hf * 1000u / top);
        }

        // Each rear building takes its own hue. One shared teal across the
        // whole skyline made the corridor's three colours look like the only
        // palette in the scene, and a distant city is not monochrome.
        //
        // Purple is handled differently from the other three on purpose. It is
        // the darkest hue in the palette to begin with, so blending it toward
        // the background as far as the rest left a near-black smudge that read
        // as a gap in the skyline rather than as a building -- it gets the
        // shallowest blend of the four. And its windows are gold rather than a
        // tint of its own body: warm light against a cool wall is what makes a
        // block read as lit from inside, which is the whole trick a night
        // skyline runs on, and it is the one hue here cool enough to sell it.
        const uint16_t cityBase[4]   = { CYAN, VAPOR_PURPLE, GREEN, VAPOR_PINK };
        static const uint8_t CITY_DIM[4][2] = { {135,178}, {100,142}, {150,190}, {140,182} };
        const uint16_t cityWinC[4]   = { blend(CYAN,         BG, 40),
                                         blend(VAPOR_YELLOW, BG, 28),
                                         blend(GREEN,        BG, 45),
                                         blend(VAPOR_PINK,   BG, 40) };
        const int      skyTop   = yStart + 10;               // clear of the readout
        const int      maxH     = hz - skyTop;
        uint8_t        cityBudget = 140;
        if (maxH > 14) {
            for (uint8_t b = 0; b < cityN; b++) {
                const int bx   = (int)((uint32_t)city[b].xf * (uint32_t)w / 1000u);
                const int bwid = (int)((uint32_t)city[b].wf * (uint32_t)w / 1000u);
                const int bh   = (int)((uint32_t)city[b].hf * (uint32_t)maxH / 1000u);
                const int by   = hz - bh;
                if (bwid < 5 || bh < 7 || by < yStart) continue;
                const uint8_t  ch   = city[b].hue;
                const uint16_t base = cityBase[ch];
                t.fillRect(bx, by, bwid, bh, blend(base, BG, CITY_DIM[ch][city[b].tone]));
                t.drawFastHLine(bx, by, bwid, blend(base, BG, 70));
                const uint16_t wOn  = cityWinC[ch];
                const uint16_t wHot = blend(wOn, WHITE, 130);

                // At this size a building only reads as a building if its
                // windows sit on a grid and some of them are out; light them
                // evenly and it goes back to reading as a bar chart.
                // Aircraft warning lights, on the tall ones only. Each runs on
                // its own offset: real ones are not synchronised, and a row of
                // them blinking in step reads as a rendering artefact rather
                // than as a skyline.
                if (city[b].hf > 620) {
                    const int byy = by - 3;
                    if (byy >= yStart) {
                        const bool on = ((now + (uint32_t)b * 130u) % 1500u) < 480u;
                        t.fillRect(bx + bwid / 2 - 1, byy, 3, 3,
                                   on ? RED : blend(RED, BG, 190));
                    }
                }

                const int cols = (bwid - 3) / 5;
                const int rows = (bh   - 4) / 7;
                for (int c = 0; c < cols && cityBudget; c++) {
                    for (int rr = 0; rr < rows && cityBudget; rr++) {
                        const uint32_t q = ((uint32_t)(b  + 1) * 2654435761u)
                                         ^ ((uint32_t)(c  + 1) *      40503u)
                                         ^ ((uint32_t)(rr + 1) * 2246822519u);
                        const uint8_t k = (uint8_t)(q & 7u);
                        bool lit;
                        if      (k < 3u) lit = false;     // out, and staying out
                        else if (k < 6u) lit = true;      // on, and staying on
                        else {
                            // The quarter that changes. Each gets its OWN period
                            // and phase: run them off one shared clock and the
                            // whole skyline blinks at once, which reads as a
                            // display fault rather than as somebody turning a
                            // light off.
                            const uint32_t per = 2400u + ((q >> 5) % 6000u);
                            const uint32_t e   = (now + ((q >> 12) % per)) / per;
                            lit = ((q ^ (e * 2654435761u)) & 3u) != 0u;
                        }
                        if (!lit) continue;
                        t.fillRect(bx + 2 + c * 5, by + 4 + rr * 7, 2, 3,
                                   (k == 5u) ? wHot : wOn);
                        cityBudget--;
                    }
                }
            }
        }
    }

    // ---- floor -------------------------------------------------------
    {
        const float spacing = (float)GIB_SPAN / 17.0f;
        int last = 30000;
        for (int k = 1; k <= 24; k++) {
            const float z = (float)k * spacing - fmodf(flyZ, spacing);
            if (z < 30.0f) continue;
            const int gy = (int)(groundY(z) + 0.5f);
            if (gy > yBot - 1) continue;
            // Past a certain depth every row lands on the same screen line.
            // Drawing the rest does not add detail, it builds a solid slab
            // along the horizon.
            if (gy < hz + 3 || gy >= last - 1) break;
            last = gy;
            t.drawFastHLine(0, gy, w, (z > GIB_NEAR * 4.0f) ? gridFar : gridNear);
        }
        // Lanes to the vanishing point. A ground-plane lane projects to a
        // straight line out of the vanishing point, so these need no depth
        // maths of their own -- only the width they fan to at the bottom edge.
        const int fan = (int)((float)w * 0.124f);
        for (int g = -5; g <= 5; g++)
            t.drawLine(w / 2, hz, w / 2 + g * fan, yBot - 1, gridFar);

        // Packets running down the lanes toward you, one lane per WiFi channel,
        // and ONLY where that channel is actually carrying something. This is
        // the one part of the scene that goes quiet when the air does, which is
        // deliberate: the trace already keeps the screen alive with nothing on,
        // so the floor can afford to mean something instead.
        //
        // They travel in DEPTH rather than in screen space. A packet moving a
        // constant number of pixels per frame reads as sliding across a flat
        // picture; moving it through z and projecting makes it accelerate into
        // the foreground the way everything else here does.
        const uint16_t pktA = blend(CYAN,      WHITE, 100);
        const uint16_t pktB = blend(VAPOR_PINK, WHITE, 80);
        for (int g = -5; g <= 5; g++) {
            const uint8_t chn = (uint8_t)(1 + ((g + 5) * 12) / 10);
            const uint8_t act = eng.channelActivity(chn);
            if (act < 12) continue;
            const uint8_t  n   = (uint8_t)(1 + act / 40);
            const uint16_t per = (uint16_t)(2300u - (uint16_t)act * 14u);
            for (uint8_t k = 0; k < n; k++) {
                const float ph = fmodf((float)now / (float)per + (float)k / (float)n, 1.0f);
                const float z  = (float)GIB_SPAN * 0.8f * (1.0f - ph) + 60.0f;
                const int   gy = (int)groundY(z);
                if (gy > yBot - 3 || gy < hz + 2) continue;
                const float lane = (float)(gy - hz) / floorH;
                const int   px   = w / 2 + (int)((float)(g * fan) * lane);
                const int   sz   = (lane > 0.55f) ? 4 : ((lane > 0.25f) ? 3 : 2);
                if (px - sz / 2 < 0 || px + sz > w) continue;
                t.fillRect(px - sz / 2, gy - sz / 2, sz, sz, (k & 1u) ? pktA : pktB);
            }
        }
    }

    // ---- channel meter bridge ----------------------------------------
    // Thirteen live channel levels down the right edge, drawn BEFORE the
    // corridor so the towers pass in front of it.
    //
    // That ordering is the whole point. Painted on top it would win every
    // argument with the scene and take the right-hand quarter permanently,
    // which is exactly where the corridor is most worth looking at. Behind the
    // towers it reads as something standing in the city rather than a panel
    // stuck over the glass, and a near tower sweeping past simply eclipses a
    // few rows of it for a second.
    {
        const int rowH = (bandH - 6) / 13;
        if (rowH >= 5) {
            const int barW = (w >= 240) ? 11 : 8;
            const int barX = w - barW - 2;
            const int labX = barX - 14;

            // Peak hold on its own clock. channelActivity already holds a peak
            // and decays it 4 every 200 ms, which is fast enough that the bars
            // twitch; without a slower mark on top there is nothing steady
            // enough to actually read a level off.
            static uint8_t  peak[14] = {0};
            static uint32_t peakAt = 0;
            if (now - peakAt > 90u) {
                peakAt = now;
                for (uint8_t c = 1; c <= 13; c++) if (peak[c]) peak[c]--;
            }

            // 215 landed on (0,36,0) -- dark green, blue quantised away, the
            // same trap as the sky band. 180 keeps a blue component and reads
            // as an unlit track rather than as more scenery.
            const uint16_t track = blend(CYAN, BG, 180);
            const uint16_t mark  = blend(CYAN, WHITE, 140);
            t.setTextSize(1);
            for (uint8_t c = 1; c <= 13; c++) {
                const uint8_t a = eng.channelActivity(c);
                if (a > peak[c]) peak[c] = a;

                const int ry = yStart + 3 + (int)(c - 1) * rowH;
                const int rh = rowH - 2;
                if (ry + rh > yBot) break;

                t.fillRect(barX, ry, barW, rh, track);
                const int fw = (int)a * barW / 100;
                if (fw > 0)
                    t.fillRect(barX, ry, fw, rh,
                               (a > 72) ? RED : ((a > 42) ? AMBER : GREEN));
                const int pw = (int)peak[c] * barW / 100;
                if (pw > 1) t.drawFastVLine(barX + pw - 1, ry, rh, mark);

                // Single digits shift right so the column right-aligns.
                t.setTextColor(a > 42 ? blend(CYAN, WHITE, 160) : blend(CYAN, BG, 130));
                t.setCursor(labX + (c < 10 ? 6 : 0), ry + (rh - 8) / 2 + 1);
                char cb[3];
                snprintf(cb, sizeof(cb), "%u", (unsigned)c);
                t.print(cb);
            }
        }
    }

    // ---- towers, far to near -----------------------------------------
    uint8_t order[GIB_N];
    float   zs[GIB_N];
    for (uint8_t i = 0; i < GIB_N; i++) { order[i] = i; zs[i] = depthOf(i); }
    // Painter's order. At 22 items an insertion sort is both the clearest thing
    // to read and faster than anything cleverer.
    for (uint8_t i = 1; i < GIB_N; i++) {
        const uint8_t key = order[i];
        const float   kz  = zs[key];
        int j = (int)i - 1;
        while (j >= 0 && zs[order[j]] < kz) { order[j + 1] = order[j]; j--; }
        order[j + 1] = key;
    }

    // Distance is carried by haze as well as hue now. Fading a far tower toward
    // black is the obvious move and the wrong one -- black is exactly where dim
    // colours already land on this panel, so it reads as the tower switching off
    // rather than as it being far away. Tinting toward the sky's own colour
    // instead makes it recede into the sky. Same number of fills; one blend.
    const uint16_t haze = blend(BG, VAPOR_PURPLE, 110);

    int     ringX     = w / 2;
    uint8_t winBudget = 200;      // one very near tower can ask for more than this

    for (uint8_t n = 0; n < GIB_N; n++) {
        const uint8_t i = order[n];
        const float   z = zs[i];
        const float   s = f / z;
        const float  sw = (float)tw[i].wf * 0.001f * xUnit * s;
        const float  sh = (float)tw[i].hf * 0.001f * hUnit * s;
        const float sxc = (float)(w / 2) + (float)tw[i].xf * 0.001f * xUnit * s;

        int iw = (int)(sw + 0.5f); if (iw < 1) iw = 1;
        int ih = (int)(sh + 0.5f); if (ih < 1) ih = 1;
        const int x0 = (int)(sxc - sw * 0.5f + 0.5f);
        const int y0 = (int)(groundY(z) - sh  + 0.5f);
        if (x0 >= w || x0 + iw <= 0) continue;

        // Clip to the band by hand. fillRect clips to the panel, not to the
        // strip we were given, and a near tower is several times taller than
        // that strip -- unclipped it paints over the title bar.
        int clipX = x0, clipW = iw;
        if (clipX < 0)          { clipW += clipX; clipX = 0; }
        if (clipX + clipW > w)    clipW = w - clipX;
        int clipY = y0, clipH = ih;
        if (clipY < yStart)     { clipH -= (yStart - clipY); clipY = yStart; }
        if (clipY + clipH > yBot) clipH = yBot - clipY;
        if (clipW <= 0 || clipH <= 0) continue;

        if (locked && lockIdx >= 0 && (uint8_t)lockIdx == i) {
            ringX = (int)sxc;
            t.fillRect(clipX, clipY, clipW, clipH,
                       blend(BG, blend(lockCol, WHITE, 40), (uint16_t)(150.0f + 105.0f * lockA)));
            // The fill already carries the detection's own colour, and that
            // colour is frequently the same hue as the towers either side of it.
            // The outline and reticle go white, which appears nowhere else on
            // screen except the trace.
            if (x0 >= 0 && x0 < w)                   t.drawFastVLine(x0, clipY, clipH, traceLit);
            if (x0 + iw - 1 >= 0 && x0 + iw - 1 < w) t.drawFastVLine(x0 + iw - 1, clipY, clipH, traceLit);
            if (y0 >= yStart && y0 < yBot)           t.drawFastHLine(clipX, y0, clipW, traceLit);

            // Reticle. Each bracket is bounds-checked on its own: a near tower
            // is clipped at both the top and the bottom of the band, so gating
            // the whole reticle on both corners being inside meant the biggest
            // and most worth marking locks drew no reticle whatsoever.
            const int L   = (clipW < 26) ? (clipW / 2 + 1) : 12;
            const int rx0 = clipX - 3, rx1 = clipX + clipW + 2;
            const int ry0 = clipY - 3, ry1 = clipY + clipH + 2;
            if (L > 1) {
                if (ry0 >= yStart) {
                    t.drawFastHLine(rx0, ry0, L, traceLit); t.drawFastHLine(rx1 - L, ry0, L, traceLit);
                    t.drawFastVLine(rx0, ry0, L, traceLit); t.drawFastVLine(rx1, ry0, L, traceLit);
                }
                if (ry1 < yBot) {
                    t.drawFastHLine(rx0, ry1, L, traceLit);     t.drawFastHLine(rx1 - L, ry1, L, traceLit);
                    t.drawFastVLine(rx0, ry1 - L, L, traceLit); t.drawFastVLine(rx1, ry1 - L, L, traceLit);
                }
                if (ry0 < yStart && ry1 >= yBot) {
                    // Both ends off the band: mark the middle instead, so a
                    // full-height lock is still visibly bracketed.
                    const int my = clipY + clipH / 2;
                    t.drawFastHLine(rx0, my, L, traceLit); t.drawFastHLine(rx1 - L, my, L, traceLit);
                }
            }
            continue;
        }

        const uint8_t h = tw[i].hue;
        if (z > GIB_NEAR * 5.2f) {
            // Far tier: one flat block with a lit roof line, well into the haze.
            // Windows are sub-pixel out here and would buy nothing but calls.
            t.fillRect(clipX, clipY, clipW, clipH, blend(shade[h], haze, 150));
            if (y0 >= yStart && y0 < yBot)
                t.drawFastHLine(clipX, y0, clipW, blend(face[h], haze, 120));
            continue;
        }

        // Mid tier fades into the same haze on a ramp, so there is no step
        // between the two tiers.
        uint16_t hzT = 0;
        if (z > GIB_NEAR * 1.4f) {
            const float f2 = (z - GIB_NEAR * 1.4f) / (GIB_NEAR * 4.0f) * 140.0f;
            hzT = (f2 > 140.0f) ? 140u : (uint16_t)f2;
        }
        const uint16_t faceC  = hzT ? blend(face[h],  haze, hzT) : face[h];
        const uint16_t shadeC = hzT ? blend(shade[h], haze, hzT) : shade[h];
        const uint16_t edgeC  = hzT ? blend(edge[h],  haze, hzT) : edge[h];

        // The site paints each face with a horizontal gradient. RGB332 has no
        // smooth ramp to give, so it becomes two flat bands -- a lit side and a
        // shaded one -- which is also two fillRects instead of a per-column loop.
        int litW = (int)(sw * 0.58f + 0.5f); if (litW < 1) litW = 1;
        {
            int lx = x0, lw = litW;
            if (lx < 0)      { lw += lx; lx = 0; }
            if (lx + lw > w)   lw = w - lx;
            if (lw > 0) t.fillRect(lx, clipY, lw, clipH, faceC);
            int dx = x0 + litW, dw = iw - litW;
            if (dx < 0)      { dw += dx; dx = 0; }
            if (dx + dw > w)   dw = w - dx;
            if (dw > 0) t.fillRect(dx, clipY, dw, clipH, shadeC);
        }
        if (x0 >= 0 && x0 < w)                   t.drawFastVLine(x0, clipY, clipH, edgeC);
        if (x0 + iw - 1 >= 0 && x0 + iw - 1 < w) t.drawFastVLine(x0 + iw - 1, clipY, clipH, edgeC);
        if (y0 >= yStart && y0 < yBot)           t.drawFastHLine(clipX, y0, clipW, edgeC);

        // Rim glow: one dim pixel of spill just OUTSIDE the silhouette. Not a
        // real bloom -- that needs to read back the framebuffer, which this
        // panel will not do at any sane speed -- but against a black sky a dim
        // outline is what light spilling off an edge actually looks like, and
        // it costs three fast lines.
        if (z < GIB_NEAR * 4.0f) {
            const uint16_t halo = blend(edgeC, BG, 150);
            if (x0 - 1 >= 0)                     t.drawFastVLine(x0 - 1, clipY, clipH, halo);
            if (x0 + iw < w)                     t.drawFastVLine(x0 + iw, clipY, clipH, halo);
            if (y0 - 1 >= yStart && y0 - 1 < yBot) t.drawFastHLine(clipX, y0 - 1, clipW, halo);
        }

        // Mainframe lights. The site re-rolls Math.random() for every window on
        // every frame; at two pixels that reads as static rather than as
        // blinkenlights, so each cell gets a fixed role from a hash instead --
        // dark, steady, or a blinker with its own period. A lock divides every
        // blinker's period by three, so the racks visibly quicken on a catch.
        if (z < GIB_NEAR * 3.4f && iw >= 9 && ih >= 16 && winBudget) {
            int cols = (iw - 3) / 4; if (cols > 5) cols = 5;
            const uint16_t wcol = win[h];
            const uint16_t wlit = blend(win[h], WHITE, 165);   // the glitter
            // Start at the first row that is actually inside the band. Walking
            // down from the roof wasted the budget on rows far above the strip
            // and then ran out -- so the nearest and biggest towers, the ones
            // whose lights you can actually see, came out completely dark.
            int wr = (yStart - (y0 + 4)) / 5;
            if (wr < 0) wr = 0;
            for (int guard = 0; guard < 44 && winBudget; guard++, wr++) {
                const int wy = y0 + 4 + wr * 5;
                if (wy + 2 > yBot || wy > y0 + ih - 4) break;
                if (wy < yStart) continue;
                for (int c = 0; c < cols && winBudget; c++) {
                    const uint32_t hsh = ((uint32_t)(i  + 1) * 2654435761u)
                                       ^ ((uint32_t)(c  + 1) *      40503u)
                                       ^ ((uint32_t)(wr + 1) * 2246822519u);
                    const uint8_t kind = (uint8_t)(hsh & 7u);
                    if (kind < 1u) continue;                       // permanently dark
                    if (kind >= 3u) {                              // a blinker
                        uint16_t per = (uint16_t)(180u + ((hsh >> 3) % 820u));
                        if (locked) per = (uint16_t)(per / 3u + 45u);
                        if (((now / per) & 1u) == 0u) continue;
                    }
                    const int wx = x0 + 2 + c * 4;
                    if (wx < 0 || wx + 2 > w) continue;
                    t.fillRect(wx, wy, 2, 2, ((hsh >> 20) & 7u) ? wcol : wlit);
                    winBudget--;
                }
            }
        }
    }

    // ---- the instrument ----------------------------------------------
    const int      mid = yStart + bandH * 56 / 100;
    const float    lim = (float)bandH * 0.24f;
    const uint16_t traceCol = locked ? blend(traceLit, lockCol, (uint16_t)(90.0f * lockA))
                                     : traceLit;
    int prevY = mid;
    for (int x = 0; x < w; x += 2) {
        const float chf = 1.0f + (float)x * 12.0f / (float)(w - 1);
        int c0 = (int)chf; if (c0 < 1) c0 = 1; if (c0 > 13) c0 = 13;
        const int   c1 = (c0 < 13) ? c0 + 1 : 13;
        const float fr = chf - (float)c0;
        float act = ((float)eng.channelActivity((uint8_t)c0) * (1.0f - fr)
                   + (float)eng.channelActivity((uint8_t)c1) * fr) * 0.01f;
        if (act > 1.0f) act = 1.0f;

        float v = sinf((float)x * 0.062f + (float)now / 300.0f) * lim * 0.15f
                + sinf((float)x * 0.171f - (float)now / 190.0f) * lim * 0.10f;
        v -= act * lim * 0.85f;                    // real traffic pushes the trace up
        if (locked) {
            const float d = (float)(x - ringX);
            float bell = 1.0f / (1.0f + d * d * 0.0011f);
            bell *= bell;                       // tails tight enough to be local
            v += bell * sinf(d * 0.42f - (float)now / 38.0f) * lim * 1.7f * (1.0f - lockK);
        }
        if (v >  lim) v =  lim;
        if (v < -lim) v = -lim;

        const int y = mid + (int)v;
        int a = (y < prevY) ? y : prevY;
        int b = (y > prevY) ? y : prevY;
        if (a < yStart)   a = yStart;
        if (b + 2 > yBot) b = yBot - 2;
        if (b >= a) t.fillRect(x, a, 2, b - a + 2, traceCol);
        prevY = y;
    }

    // ---- the feed ----------------------------------------------------
    t.setTextSize(1);
    const uint8_t step = (uint8_t)((now / 620u) % GIB_FEED_N);
    for (int i = 0; i < 3; i++) {
        const int fy = yBot - 9 - i * 9;
        if (fy < hz + 2) break;
        t.setCursor(3, fy);
        if (i == 0 && locked) {
            t.setTextColor(lockCol);
            t.print("> ");
            t.print(detectionTypeName(lockType));
            t.print(" :: LOCKED");
        } else {
            t.setTextColor(blend(BG, GREEN, (uint16_t)(256 - i * 60)));
            t.print("> ");
            t.print(GIB_FEED[(step + GIB_FEED_N - (uint8_t)i) % GIB_FEED_N]);
            t.print("_");
        }
    }
    t.setTextColor(edge[0], BG);
    t.setCursor(3, yStart + 1);
    t.print("GIBSON // 2.4GHz");
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
