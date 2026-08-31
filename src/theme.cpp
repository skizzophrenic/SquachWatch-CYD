// SquachWatch-CYD — theme implementation
#include "theme.h"
#include "detection.h"
#include "bangers_font.h"

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

void drawTitleBar(TFT_eSPI& t, const char* title) {
    int w = t.width();
    for (int x = 0; x < w; x++) {
        t.drawFastVLine(x, 0, 14, titlebarColor(x, w));
    }
    t.drawFastHLine(0, 14, w, PURPLE);
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

void drawAlertFx(TFT_eSPI& t, DetectionType type, uint32_t now, int w, int h) {
    // Full repaint every call — the old single scanline this replaced
    // never erased its own trail, so it just accumulated into a wash
    // across the screen the longer an alert stayed up. Same "always
    // fully repaint" discipline the CLEAR-screen backgrounds already
    // use, for the same reason.
    t.fillRect(0, 0, w, h, BG);
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

void drawMatrixRain(TFT_eSPI& t, uint32_t now, int yStart, int yEnd, bool advance) {
    // Dense columns (narrow gutters) with long, smoothly-decaying
    // trails so this reads as a code-rain backdrop rather than a
    // handful of isolated falling raindrops. Glyphs are plain ASCII
    // (the default GLCD font can't render the old UTF-8 katakana bytes
    // correctly) drawn from a dense symbol/letter/digit set. Column
    // count adapts to the current width so it works in portrait
    // (240px) as well as landscape (320px) without overflowing.
    static const int  MAX_COLS = 64;
    static const int  SPACING  = 5;
    static const char GLYPHS[] =
        "01" "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "!@#$%^&*<>{}[]/\\|+=~" "SASQUACH";
    static const int  GLN   = sizeof(GLYPHS) - 1;
    static const int  TRAIL = 22;
    static const uint16_t HEADS[3] = { VAPOR_PINK, CYAN, GREEN };

    int cols = t.width() / SPACING;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (cols < 1) cols = 1;

    static int16_t yPos[MAX_COLS];
    static uint8_t ySpeed[MAX_COLS];
    static uint8_t yTick[MAX_COLS] = {0};
    // The glyph at each trailing position, so a character keeps
    // decaying (dimming) as it falls instead of reshuffling every
    // frame — that reshuffle is what read as noisy "raindrops" rather
    // than a settled backdrop.
    static uint8_t charBuf[MAX_COLS][TRAIL];
    static bool    initialized = false;
    static int     lastCols = -1;

    if (!initialized || cols != lastCols) {
        for (int i = 0; i < cols; i++) {
            yPos[i]   = (int16_t)(yStart - random(0, 80));
            ySpeed[i] = 2 + (uint8_t)random(0, 3);
            for (int j = 0; j < TRAIL; j++) charBuf[i][j] = (uint8_t)random(0, GLN);
        }
        initialized = true;
        lastCols = cols;
    }

    // Full-band clear every frame, same as every other background
    // style. Without it, a column that just wrapped (its whole trail
    // still above yStart, see the respawn branch below) skips its
    // narrow vertical strip entirely for several frames — nothing else
    // ever repaints that strip, so anything drawn over it last frame
    // (Squachy included, since he no longer erases his own footprint —
    // see squachy.cpp) is left behind as a stale smear instead of
    // being cleaned up.
    t.fillRect(0, yStart, t.width(), yEnd - yStart, BG);

    t.setTextSize(1);
    // Column advance is gated (see the header comment) -- yTick is a
    // call-counted divider, not now-based, so calling this twice per
    // logical frame would fall the rain at double speed otherwise.
    if (advance) {
        for (int i = 0; i < cols; i++) {
            if (++yTick[i] >= ySpeed[i]) {
                yTick[i] = 0;
                yPos[i] += 8;
                // A fresh glyph enters at the head; everything already in
                // the buffer shifts one slot further from it (still the
                // same characters, just older/dimmer).
                for (int j = TRAIL - 1; j > 0; j--) charBuf[i][j] = charBuf[i][j - 1];
                charBuf[i][0] = (uint8_t)random(0, GLN);
                if (yPos[i] > yEnd + TRAIL * 8) {
                    yPos[i] = (int16_t)(yStart - random(0, 80));
                    ySpeed[i] = 2 + (uint8_t)random(0, 3);
                }
            }
        }
    }
    for (int i = 0; i < cols; i++) {
        uint16_t head = HEADS[i % 3];
        int x = 3 + i * SPACING;
        for (int j = 0; j < TRAIL; j++) {
            int16_t ry = yPos[i] - j * 8;
            if (ry < yStart || ry >= yEnd) continue;
            char buf[2] = { GLYPHS[charBuf[i][j]], 0 };
            if (j == 0) {
                t.setTextColor(head, BG);
            } else {
                // Eased falloff (stays brighter a little longer right
                // behind the head, then tails off) instead of a flat
                // linear ramp — a longer, softer decay.
                float f = 1.0f - (float)j / (float)TRAIL;
                uint8_t b = (uint8_t)(210.0f * f * f);
                t.setTextColor(blend(BG, head, b), BG);
            }
            t.setCursor(x, ry);
            t.print(buf);
        }
    }

    // Rare glitch-in message — a message flashes through the rain for
    // under a second every 10-25s, like a signal briefly cutting
    // through the noise, then goes back to plain code-rain.
    static const char* GLITCH_MSGS[] = { "SQUACHWATCH", "THEY SEE YOU", "STAY AWARE", "NOT TODAY" };
    static bool     glitchActive = false;
    static uint32_t glitchNextAt = 0, glitchStart = 0;
    static uint8_t  glitchIdx = 0;
    static bool     glitchInited = false;
    // Jitter is re-rolled once per logical frame and reused by every
    // band's render below -- otherwise each band would pick its own
    // random offset for the same frame, visibly tearing the message.
    static int      glitchJitter = 0;
    if (advance) {
        if (!glitchInited) { glitchNextAt = now + (uint32_t)random(10000, 20000); glitchInited = true; }
        if (!glitchActive && now >= glitchNextAt) {
            glitchActive = true;
            glitchStart  = now;
            glitchIdx    = (uint8_t)random(0, 4);
        }
        if (glitchActive) {
            if (now - glitchStart < 850) {
                glitchJitter = (int)random(-2, 3);
            } else {
                glitchActive = false;
                glitchNextAt = now + (uint32_t)random(12000, 25000);
            }
        }
    }
    if (glitchActive) {
        uint32_t age = now - glitchStart;
        if (age < 850) {
            t.setTextSize(2);
            const char* msg = GLITCH_MSGS[glitchIdx];
            int mw = t.textWidth(msg);
            int mx = (t.width() - mw) / 2 + glitchJitter;
            int my = yStart + (yEnd - yStart) / 2 - 8;
            t.setTextColor(blend(BG, VAPOR_PINK, (age < 700) ? 230 : (uint16_t)(230 - (age - 700) * 2)), BG);
            t.setCursor(mx, my);
            t.print(msg);
        }
    }
}

void drawStarfield(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    static const uint8_t N = 44;
    static float   sa[N];   // angle, radians
    static float   sd[N];   // distance from center, 0..maxD
    static float   sspeed[N];
    static bool    inited = false;
    static uint32_t lastMs = 0;

    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 4) return;
    int cx = w / 2;
    int cy = yStart + bandH / 2;
    float maxD = sqrtf((float)(cx * cx + (bandH / 2) * (bandH / 2))) + 4.0f;

    if (!inited) {
        for (int i = 0; i < N; i++) {
            sa[i]     = (float)random(0, 6283) / 1000.0f;
            sd[i]     = (float)random(0, (int)maxD);
            sspeed[i] = 0.6f + (float)random(0, 100) / 100.0f;
        }
        inited = true;
        lastMs = now;
    }
    float dt = (float)(now - lastMs) / 16.0f;   // ~1.0 at 60fps
    if (dt > 4.0f) dt = 4.0f;
    lastMs = now;

    t.fillRect(0, yStart, w, bandH, BG);
    for (int i = 0; i < N; i++) {
        sd[i] += sspeed[i] * dt * (0.4f + sd[i] / maxD * 1.6f);
        if (sd[i] > maxD) {
            sa[i] = (float)random(0, 6283) / 1000.0f;
            sd[i] = 0;
            sspeed[i] = 0.6f + (float)random(0, 100) / 100.0f;
        }
        int x = cx + (int)(cosf(sa[i]) * sd[i]);
        int y = cy + (int)(sinf(sa[i]) * sd[i]);
        if (x < 0 || x >= w || y < yStart || y >= yEnd) continue;
        float near = sd[i] / maxD;               // 0 = center (far), 1 = edge (near)
        uint8_t bri = (uint8_t)(60.0f + 195.0f * near);
        uint16_t col = t.color565(bri, bri, bri);
        if (near > 0.72f) {
            t.drawPixel(x, y, col);
            t.drawPixel(x + 1, y, col);
        } else {
            t.drawPixel(x, y, col);
        }
    }

    // Meteor: a rare bright diagonal streak with a fading trail,
    // crossing from one random corner-ish edge to the opposite side.
    static bool     metActive = false;
    static uint32_t metNextAt = 0;
    static float    metX, metY, metVx, metVy;
    static bool     metInited = false;
    if (!metInited) { metNextAt = now + (uint32_t)random(2500, 7000); metInited = true; }
    if (!metActive && now >= metNextAt) {
        metActive = true;
        bool fromLeft = random(0, 2);
        metX  = fromLeft ? (float)-10 : (float)(w + 10);
        metY  = (float)(yStart + random(0, bandH / 3));
        float spd = 4.0f + (float)random(0, 100) / 100.0f * 3.0f;
        metVx = (fromLeft ? 1.0f : -1.0f) * spd;
        metVy = spd * 0.55f;
    }
    if (metActive) {
        for (int k = 6; k >= 1; k--) {
            int tx = (int)(metX - metVx * k * 0.5f), ty = (int)(metY - metVy * k * 0.5f);
            uint16_t col = blend(BG, WHITE, (uint16_t)(200 * (1.0f - (float)k / 6.0f)));
            t.drawPixel(tx, ty, col);
        }
        t.drawPixel((int)metX, (int)metY, WHITE);
        t.drawPixel((int)metX + 1, (int)metY, WHITE);
        metX += metVx; metY += metVy;
        if (metX < -20 || metX > w + 20 || metY > yEnd + 20) {
            metActive = false;
            metNextAt = now + (uint32_t)random(3000, 9000);
        }
    }

    // UFO: a rare little saucer drifting across with blinking lights.
    static bool     ufoActive = false;
    static uint32_t ufoNextAt = 0;
    static float    ufoX, ufoY;
    static int8_t   ufoDir;
    static bool     ufoInited = false;
    if (!ufoInited) { ufoNextAt = now + (uint32_t)random(6000, 14000); ufoInited = true; }
    if (!ufoActive && now >= ufoNextAt) {
        ufoActive = true;
        ufoDir = random(0, 2) ? 1 : -1;
        ufoX   = (ufoDir > 0) ? -20.0f : (float)(w + 20);
        ufoY   = (float)(yStart + random(bandH / 6, bandH / 2));
    }
    if (ufoActive) {
        ufoX += ufoDir * 1.1f;
        ufoY += sinf(ufoX * 0.05f) * 0.4f;
        int ux = (int)ufoX, uy = (int)ufoY;
        t.fillEllipse(ux, uy, 11, 4, blend(BG, CYAN, 200));
        t.fillEllipse(ux, uy - 3, 5, 4, blend(BG, WHITE, 220));
        for (uint8_t k = 0; k < 3; k++) {
            uint16_t lcol = (((now / 150) + k) % 3 == 0) ? VAPOR_YELLOW : blend(BG, VAPOR_YELLOW, 90);
            t.drawPixel(ux - 7 + k * 7, uy + 3, lcol);
        }
        if (ufoX < -30 || ufoX > w + 30) {
            ufoActive = false;
            ufoNextAt = now + (uint32_t)random(8000, 18000);
        }
    }
}

// Toaster glyph matched against the actual reference art: a boxy
// isometric-ish body (not a loaf — that was still wrong), olive-green
// sides with a chrome "dome" across the top-front carrying two red
// racing stripes, a front slot/lever, and big layered feathered wings
// with a dark outline stroke, cartoon-icon style.
static void drawToasterAt(TFT_eSPI& t, int x, int y, uint32_t now, uint16_t domeCol, float scale) {
    auto S = [scale](int v) { return (int)(v * scale + 0.5f); };
    int bw = S(23), bh = S(17);
    int cx = x + bw / 2, cy = y + bh / 2;

    uint16_t olive    = t.color565(95, 100, 58);
    uint16_t oliveDk  = t.color565(70, 74, 42);
    uint16_t stripe   = t.color565(168, 36, 36);
    uint16_t outline  = t.color565(25, 25, 22);
    uint16_t feather2 = t.color565(230, 226, 214);

    // Wings first (behind the body) — one above, one below (not side
    // by side), two layered shapes each for a feathered look, each
    // with a dark outline stroke. Both trail backward (toward -x, the
    // direction they just flew in from).
    float wing = sinf((float)((now + (uint32_t)x * 37) % 500) / 500.0f * 6.2831853f);
    int wx = (int)(wing * S(5));
    t.fillTriangle(cx, cy - S(2), cx - S(12) - wx, cy - S(22), cx + S(6) - wx, cy - S(8), TFT_WHITE);
    t.fillTriangle(cx - S(1), cy - S(4), cx - S(9) - wx, cy - S(16), cx + S(2) - wx, cy - S(7), feather2);
    t.drawTriangle(cx, cy - S(2), cx - S(12) - wx, cy - S(22), cx + S(6) - wx, cy - S(8), outline);
    t.fillTriangle(cx, cy + S(2), cx - S(12) + wx, cy + S(22), cx + S(6) + wx, cy + S(8), TFT_WHITE);
    t.fillTriangle(cx - S(1), cy + S(4), cx - S(9) + wx, cy + S(16), cx + S(2) + wx, cy + S(7), feather2);
    t.drawTriangle(cx, cy + S(2), cx - S(12) + wx, cy + S(22), cx + S(6) + wx, cy + S(8), outline);

    // Olive box body, darker base shading along the bottom edge.
    t.fillRoundRect(x, y, bw, bh, S(3), olive);
    t.fillRect(x, y + bh - S(3), bw, S(3), oliveDk);

    // Chrome dome across the top-front (olive shows through as a
    // border around it), carrying the two red racing stripes.
    t.fillRoundRect(x + S(2), y + S(1), bw - S(4), S(10), S(3), domeCol);
    t.fillRect(cx - S(7), y + S(3), S(14), S(2), stripe);
    t.fillRect(cx - S(7), y + S(6), S(14), S(2), stripe);

    // Front slot/lever.
    t.fillRect(x + S(3), y + bh - S(7), S(3), S(6), BLACK);

    t.drawRoundRect(x, y, bw, bh, S(3), outline);
}

// A slice of toast, flying under its own power just like the
// toasters — the signature After Dark gag. A minority get a little
// smiley face, the fan-favorite detail from the original.
static void drawToastAt(TFT_eSPI& t, int x, int y, uint32_t now, bool hasFace, float scale) {
    auto S = [scale](int v) { return (int)(v * scale + 0.5f); };
    uint16_t toastCol = t.color565(214, 163, 74);
    uint16_t crustCol = t.color565(150, 100, 40);
    int bw = S(14), bh = S(16);
    t.fillRoundRect(x, y, bw, bh, S(4), toastCol);
    t.drawRoundRect(x, y, bw, bh, S(4), crustCol);
    t.drawPixel(x + S(3), y + S(4), crustCol);
    t.drawPixel(x + S(9), y + S(6), crustCol);
    t.drawPixel(x + S(5), y + S(10), crustCol);
    if (hasFace) {
        t.drawPixel(x + S(4), y + S(6), 0x0000);
        t.drawPixel(x + S(9), y + S(6), 0x0000);
        float mood = sinf((float)now / 500.0f + x);
        if (mood > 0.0f) {
            t.drawLine(x + S(4), y + S(10), x + S(9), y + S(10), 0x0000);   // content smile
        } else {
            t.drawPixel(x + S(4), y + S(10), 0x0000);                      // startled 'o'
            t.drawPixel(x + S(9), y + S(10), 0x0000);
        }
    }
}

void drawFlyingToasters(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    static const uint8_t N = 5;
    static float    tx[N], ty[N], tscale[N];
    static uint16_t tcol[N];
    static const uint8_t NT = 6;
    static float    ox[NT], oy[NT], oscale[NT];
    static bool      oface[NT];
    static bool      inited = false;

    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 20) return;

    // Plain black space — the actual reference art has no starfield at
    // all, just the flock against solid black.
    uint16_t chromeCol = t.color565(200, 202, 208);

    if (!inited) {
        for (uint8_t i = 0; i < N; i++) {
            tx[i]     = (float)random(-w, w);
            ty[i]     = (float)random(yStart, yEnd - 12);
            tcol[i]   = chromeCol;
            tscale[i] = 0.7f + (float)random(0, 100) / 100.0f * 0.7f;
        }
        for (uint8_t i = 0; i < NT; i++) {
            ox[i]     = (float)random(-w, w);
            oy[i]     = (float)random(yStart, yEnd - 14);
            oface[i]  = random(0, 3) == 0;
            oscale[i] = 0.7f + (float)random(0, 100) / 100.0f * 0.7f;
        }
        inited = true;
    }

    t.fillRect(0, yStart, w, bandH, BG);

    // Classic flight path: diagonally up and to the right, off the top
    // corner, re-entering from the lower-left. Every so often a
    // respawning toaster comes back gold-plated instead of chrome — a
    // rare shiny to spot, with a little sparkle trail while it lasts.
    uint16_t goldCol = t.color565(255, 215, 60);
    for (uint8_t i = 0; i < N; i++) {
        tx[i] += (0.6f + (float)(i % 3) * 0.25f) * tscale[i];
        ty[i] -= (0.15f + (float)(i % 2) * 0.1f) * tscale[i];
        if (tx[i] > w + 30 || ty[i] < (float)yStart - 12) {
            tx[i]     = (float)(-random(0, 40) - 20);
            ty[i]     = (float)random(yStart + 12, yEnd - 12);
            tcol[i]   = (random(0, 15) == 0) ? goldCol : chromeCol;
            tscale[i] = 0.7f + (float)random(0, 100) / 100.0f * 0.7f;
        }
        drawToasterAt(t, (int)tx[i], (int)ty[i], now, tcol[i], tscale[i]);
        if (tcol[i] == goldCol) {
            for (uint8_t k = 1; k <= 3; k++) {
                int spx = (int)(tx[i] - k * 3.5f), spy = (int)(ty[i] + k * 0.9f + 6);
                t.drawPixel(spx, spy, blend(BG, goldCol, (uint16_t)(180 - k * 50)));
            }
        }
    }
    for (uint8_t i = 0; i < NT; i++) {
        ox[i] += (0.7f + (float)(i % 3) * 0.2f) * oscale[i];
        oy[i] -= (0.18f + (float)(i % 2) * 0.12f) * oscale[i];
        if (ox[i] > w + 20 || oy[i] < (float)yStart - 14) {
            ox[i]     = (float)(-random(0, 60) - 16);
            oy[i]     = (float)random(yStart + 14, yEnd - 14);
            oface[i]  = random(0, 3) == 0;
            oscale[i] = 0.7f + (float)random(0, 100) / 100.0f * 0.7f;
        }
        drawToastAt(t, (int)ox[i], (int)oy[i], now, oface[i], oscale[i]);
    }
}

// RGB565 -> 8-bit-per-channel, so the metaball blend math below can do
// plain weighted averages. Standard 5/6/5 -> 8/8/8 expansion.
static void rgb565to888(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
    g = (uint8_t)(((c >> 5)  & 0x3F) * 255 / 63);
    b = (uint8_t)((c         & 0x1F) * 255 / 31);
}

// Real metaball field, same technique as the "Lava Lamp" desktop app at
// talkingsasquach.com (canvas + per-pixel field sum, r^2/d^2 falloff,
// merge where the field crosses a threshold) — coarsened to a small
// grid instead of per-pixel and rendered as filled blocks so it's
// cheap enough for this MCU. Blob colors are read from the live Theme
// palette (PURPLE/CYAN/PINK/VAPOR_*) each call, so switching the color
// theme re-tints the lamp too.
void drawLavaLamp(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    struct Blob { float x, y, vx, vy, r, pulse, px, py, sp, wb, rise; };
    static const uint8_t N = 9;
    static Blob    blobs[N];
    static bool    inited = false;
    static uint32_t lastMs = 0;

    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 20) return;

    if (!inited) {
        for (uint8_t i = 0; i < N; i++) {
            Blob& b = blobs[i];
            b.x    = w / 2.0f + ((float)random(-100, 100) / 100.0f) * w * 0.3f;
            b.y    = yStart + bandH / 2.0f + ((float)random(-100, 100) / 100.0f) * bandH * 0.3f;
            b.r    = bandH * 0.10f + (float)random(0, 100) / 100.0f * bandH * 0.08f;
            b.vx   = (float)random(-50, 50) / 100.0f;
            b.vy   = (float)random(-50, 50) / 100.0f;
            b.px   = (float)random(0, 6283) / 1000.0f;
            b.py   = (float)random(0, 6283) / 1000.0f;
            b.sp   = 0.012f + (float)random(0, 100) / 100.0f * 0.016f;
            b.wb   = 0.014f + (float)random(0, 100) / 100.0f * 0.018f;
            b.rise = (random(0, 2) ? 1.0f : -1.0f) * (0.04f + (float)random(0, 100) / 100.0f * 0.06f);
            b.pulse = b.r;
        }
        inited = true;
        lastMs = now;
    }
    float dt = (float)(now - lastMs) / 16.0f;   // ~1.0 at 60fps
    if (dt <= 0.0f || dt > 4.0f) dt = 1.0f;
    lastMs = now;

    // Blob color palette — decomposed once per call, not once per grid
    // cell, since it never changes mid-frame.
    uint8_t cr[N], cg[N], cb[N];
    static const uint16_t colSrc[6] = { PURPLE, CYAN, PINK, VAPOR_PURPLE, VAPOR_BLUE, VAPOR_PINK };
    for (uint8_t i = 0; i < N; i++) rgb565to888(colSrc[i % 6], cr[i], cg[i], cb[i]);

    for (uint8_t i = 0; i < N; i++) {
        Blob& b = blobs[i];
        b.px += b.sp * dt; b.py += b.sp * 1.3f * dt;
        b.vx += sinf(b.px) * b.wb * dt;
        b.vy += (cosf(b.py) * b.wb + b.rise * 0.04f) * dt;
        b.vx *= 0.978f; b.vy *= 0.978f;
        b.x  += b.vx * dt; b.y += b.vy * dt;
        if (b.x < b.r)          { b.x = b.r;          b.vx =  fabsf(b.vx) * 0.6f; }
        if (b.x > w - b.r)      { b.x = w - b.r;      b.vx = -fabsf(b.vx) * 0.6f; }
        if (b.y < yStart + b.r) { b.y = yStart + b.r; b.vy =  fabsf(b.vy) * 0.5f; b.rise =  fabsf(b.rise); }
        if (b.y > yEnd - b.r)   { b.y = yEnd - b.r;   b.vy = -fabsf(b.vy) * 0.5f; b.rise = -fabsf(b.rise); }
        b.pulse = b.r * (1.0f + sinf(b.px * 1.8f) * 0.1f);
    }

    static const int   GRID      = 4;
    static const float THRESHOLD = 0.8f;   // lower = blobs merge more readily ("more lava")
    for (int gy = 0; gy * GRID < bandH; gy++) {
        int wy = yStart + gy * GRID + GRID / 2;
        for (int gx = 0; gx * GRID < w; gx++) {
            int wx = gx * GRID + GRID / 2;
            float sum = 0, wr = 0, wg = 0, wb2 = 0;
            for (uint8_t i = 0; i < N; i++) {
                float dx = wx - blobs[i].x, dy = wy - blobs[i].y;
                float d2 = dx * dx + dy * dy;
                if (d2 < 1.0f) d2 = 1.0f;
                float contrib = (blobs[i].pulse * blobs[i].pulse) / d2;
                sum += contrib;
                wr  += cr[i] * contrib; wg += cg[i] * contrib; wb2 += cb[i] * contrib;
            }
            int bw = GRID, bh = GRID;
            if (gx * GRID + GRID > w)     bw = w - gx * GRID;
            if (gy * GRID + GRID > bandH) bh = bandH - gy * GRID;
            if (sum < THRESHOLD) {
                t.fillRect(gx * GRID, yStart + gy * GRID, bw, bh, BG);
                continue;
            }
            float over = (sum - THRESHOLD) * 3.0f;
            if (over > 1.0f) over = 1.0f;
            float brf = 0.55f + over * 0.45f;
            float rr = wr / sum * brf, gg = wg / sum * brf, bb2 = wb2 / sum * brf;
            if (rr  > 255.0f) rr  = 255.0f;
            if (gg  > 255.0f) gg  = 255.0f;
            if (bb2 > 255.0f) bb2 = 255.0f;
            t.fillRect(gx * GRID, yStart + gy * GRID, bw, bh,
                       t.color565((uint8_t)rr, (uint8_t)gg, (uint8_t)bb2));
        }
    }

    // Specular highlight per blob — same offset-radial-gradient trick
    // as the reference JS lamp, approximated as a flat lightened disc
    // (no true alpha blending here). This is what actually reads as
    // "surface tension" / wet glossy lava rather than flat colored
    // blobs.
    for (uint8_t i = 0; i < N; i++) {
        Blob& b = blobs[i];
        int hx = (int)(b.x - b.pulse * 0.28f);
        int hy = (int)(b.y - b.pulse * 0.28f);
        int hr = (int)(b.pulse * 0.32f);
        if (hr < 1) hr = 1;
        uint16_t hcol = t.color565((uint8_t)((cr[i] + 255) / 2),
                                   (uint8_t)((cg[i] + 255) / 2),
                                   (uint8_t)((cb[i] + 255) / 2));
        t.fillCircle(hx, hy, hr, hcol);
    }
}

void drawAquarium(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    // species: 0 = minnow, 1 = angelfish, 2 = puffer, 3 = jellyfish
    // (its own drift-and-pulse motion instead of side-to-side swimming).
    struct Fish { float x, y, speed, phase; int8_t dir; uint8_t size, species; uint16_t col; };
    static const uint8_t N = 8;
    static Fish  fish[N];
    static bool  fInited = false;
    static const uint8_t NB = 10;
    static float bubX[NB], bubY[NB];
    static bool  bInited = false;
    // The rare big shark — mostly parked off-screen, only swims through
    // once in a while.
    static float    sharkX, sharkY;
    static bool     sharkActive = false;
    static uint32_t sharkNextAt = 0;
    static bool     sharkInited = false;

    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 20) return;

    if (!fInited) {
        uint16_t cols[5] = { CYAN, VAPOR_PINK, VAPOR_YELLOW, GREEN, VAPOR_PURPLE };
        for (uint8_t i = 0; i < N; i++) {
            fish[i].x       = (float)random(0, w);
            fish[i].y       = (float)(yStart + random(10, bandH > 20 ? bandH - 10 : bandH));
            fish[i].speed   = 0.3f + (float)random(0, 100) / 100.0f * 0.7f;
            fish[i].phase   = (float)random(0, 6283) / 1000.0f;
            fish[i].dir     = random(0, 2) ? 1 : -1;
            fish[i].species = (uint8_t)((i == 0) ? 3 : random(0, 3));  // guarantee at least one jellyfish
            fish[i].size    = (uint8_t)(4 + random(0, 4));
            fish[i].col     = cols[i % 5];
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

    t.fillRect(0, yStart, w, bandH, BG);

    // Kelp: jointed multi-segment strands, sway amplitude growing
    // toward the tip like real kelp anchored at the base, with little
    // leaf ticks along each segment.
    int weedBaseY = yEnd - 1;
    static const uint8_t NW = 6;
    for (uint8_t i = 0; i < NW; i++) {
        int baseX = 5 + (int)(i * (w - 10) / (float)(NW - 1));
        int segs = 5 + (i % 3);
        int segH = (bandH / 3) / segs; if (segH < 2) segH = 2;
        uint16_t weedCol = (i % 2 == 0) ? GREEN : blend(GREEN, CYAN, 90);
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
            if (f.x < 0) f.x = 0; if (f.x > w) f.x = (float)w;
            if (f.y < yStart + 8) f.y = (float)(yStart + 8);
            if (f.y > yEnd - 8)   f.y = (float)(yEnd - 8);
            int jx = (int)f.x, jy = (int)f.y, s = f.size;
            float pulse = 0.7f + 0.3f * sinf((float)now / 500.0f + f.phase);
            t.fillEllipse(jx, jy, s, (int)(s * 0.6f * pulse), blend(BG, f.col, 160));
            for (uint8_t k = 0; k < 4; k++) {
                int tx = jx - s + k * (2 * s) / 3;
                int ty = jy + (int)(s * 0.6f);
                int wob = (int)(sinf((float)now / 260.0f + k + f.phase) * 3.0f);
                t.drawLine(tx, ty, tx + wob, ty + s, blend(BG, f.col, 110));
            }
            continue;
        }

        f.x += f.dir * f.speed * fleeMul;
        if (f.dir > 0 && f.x > w + 10) f.x = -10;
        if (f.dir < 0 && f.x < -10)    f.x = (float)(w + 10);
        // Occasionally turn around mid-tank instead of only at the
        // edges — keeps the motion from feeling like a fixed loop.
        if (!sharkActive && random(0, 900) == 0) f.dir = (int8_t)-f.dir;
        float bob = sinf((float)now / 700.0f + f.phase) * 2.0f;

        int fx = (int)f.x, fy = (int)(f.y + bob), s = f.size;
        // Tail wag: a small alternating offset on the tail tip, giving
        // a swimming flick instead of a static triangle. Flicks faster
        // while fleeing.
        int wag = (int)(sinf((float)now / (sharkActive ? 70.0f : 130.0f) + f.phase) * (s / 2 + 1));

        if (f.species == 1) {
            // Angelfish: taller diamond body + a long dorsal spike.
            t.fillTriangle(fx + f.dir * s, fy, fx - f.dir * (s / 2), fy - s, fx - f.dir * (s / 2), fy + s, f.col);
            t.drawLine(fx - f.dir * (s / 4), fy - s, fx - f.dir * (s / 4), fy - s - s / 2, f.col);
            int tailX = fx - f.dir * (s / 2);
            t.fillTriangle(tailX, fy, tailX - f.dir * s, fy - s / 2 + wag, tailX - f.dir * s, fy + s / 2 + wag, f.col);
        } else if (f.species == 2) {
            // Puffer: round body, tiny tail flick, spikes.
            t.fillCircle(fx, fy, s, f.col);
            for (uint8_t k = 0; k < 6; k++) {
                float a = k * 1.047f + (float)now / 400.0f;
                t.drawPixel(fx + (int)(cosf(a) * (s + 2)), fy + (int)(sinf(a) * (s + 2)), f.col);
            }
            t.fillTriangle(fx - f.dir * s, fy, fx - f.dir * (s + 4), fy - s / 2 + wag, fx - f.dir * (s + 4), fy + s / 2 + wag, f.col);
        } else {
            // Minnow: simple tapered body + wagging tail.
            int tailX = fx - f.dir * s;
            t.fillTriangle(fx - f.dir * s, fy, fx + f.dir * s, fy - s / 2, fx + f.dir * s, fy + s / 2, f.col);
            t.fillTriangle(tailX, fy, tailX - f.dir * (s / 2), fy - s / 2 + wag, tailX - f.dir * (s / 2), fy + s / 2 + wag, f.col);
        }
        t.drawPixel(fx + f.dir * (s / 2), fy - 1, BLACK);
    }

    // The shark: dormant most of the time, then glides straight across
    // once in a while — a little payoff for watching the tank.
    if (!sharkActive && now >= sharkNextAt) {
        sharkActive = true;
        sharkX = (random(0, 2) ? -30.0f : (float)(w + 30));
        sharkY = (float)(yStart + random(8, bandH > 16 ? bandH - 8 : bandH));
    }
    if (sharkActive) {
        int8_t dir = (sharkX < w / 2) ? 1 : -1;
        sharkX += dir * 1.6f;
        int sx = (int)sharkX, sy = (int)sharkY, ss = 12;
        uint16_t sc = blend(BG, WHITE, 90);
        t.fillTriangle(sx - dir * ss, sy, sx + dir * ss, sy - ss / 2, sx + dir * ss, sy + ss / 2, sc);
        t.fillTriangle(sx - dir * ss, sy, sx - dir * (ss + ss / 2), sy - ss / 3, sx - dir * (ss + ss / 2), sy + ss / 3, sc);
        t.fillTriangle(sx, sy - ss / 2, sx - dir * 3, sy - ss, sx + dir * 3, sy - ss / 2, sc);
        t.drawPixel(sx + dir * (ss / 2), sy - 2, BLACK);
        if (sx < -40 || sx > w + 40) {
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

void drawFire(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    static const int CW = 4;
    static const int MAXFW = 110, MAXFH = 80;
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
    for (int x = 0; x < fw; x++) {
        float v = acc[x];
        if (v > 1.25f) v = 1.25f;
        uint8_t base = (uint8_t)(48.0f * (v / 1.25f));
        heat[(fh - 1) * MAXFW + x] = (random(0, 6) == 0) ? 0 : base;
    }

    // Propagate upward with random decay and a little horizontal drift
    // — the classic Doom-fire trick. Decay range (was 0-3, now 0-2) is
    // the actual height control: lower average decay means more rows
    // of upward travel before a column's heat hits zero, so flames
    // reach further up the band. Left the seed intensity (48, just
    // below) alone -- the color-tier math below it (v<9/22/36 bands,
    // "f = v - 36" at the top) is calibrated against that exact max;
    // raising it would let f overflow uint8_t in the brightest tier.
    for (int y = 0; y < fh - 1; y++) {
        for (int x = 0; x < fw; x++) {
            int decay = random(0, 3);
            int nx = x + random(-1, 2);
            if (nx < 0) nx = 0;
            if (nx >= fw) nx = fw - 1;
            int val = heat[(y + 1) * MAXFW + nx] - decay;
            if (val < 0) val = 0;
            heat[y * MAXFW + x] = (uint8_t)val;
        }
    }

    for (int y = 0; y < fh; y++) {
        for (int x = 0; x < fw; x++) {
            uint8_t v = heat[y * MAXFW + x];
            uint16_t col;
            if (v == 0) {
                col = BG;
            } else if (v < 9) {
                // Faint drifting smoke fringe instead of a hard cutoff
                // straight to background — this is what actually reads
                // as smoke rather than the flame just vanishing.
                col = blend(BG, t.color565(60, 60, 75), (uint16_t)(v * 28));
            } else if (v < 22) {
                col = t.color565((uint8_t)(60 + (v - 9) * 15), 0, 0);
            } else if (v < 36) {
                col = t.color565(255, (uint8_t)((v - 22) * 18), 0);
            } else {
                uint8_t f = v - 36;
                col = t.color565(255, (uint8_t)(200 + f * 4), (uint8_t)(f * 18));
            }
            t.fillRect(x * CW, yStart + y * CW, CW, CW, col);
        }
    }

    // Spooky night sky showing through wherever the fire isn't — stars
    // only draw where the heat grid says that cell is genuinely dark
    // (below 9, the same smoke-fringe threshold the color tiers above
    // use), so they never appear to shine through visible flame or
    // smoke. Same twinkle mechanic as drawSunsetSky's stars.
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
        int gx = skyX[i] / CW, gy = (skyY[i] - yStart) / CW;
        if (gx < 0 || gx >= fw || gy < 0 || gy >= fh) continue;
        if (heat[gy * MAXFW + gx] >= 9) continue;  // occluded by flame/smoke
        uint32_t tw = (now / 10 + (uint32_t)skyPh[i] * 22) % 300;
        if (tw > 220) continue;
        uint8_t bri = (tw < 100) ? 200 : (uint8_t)(200 - (tw - 100) * 2);
        t.drawPixel(skyX[i], skyY[i], t.color565((uint8_t)(bri * 0.85f), (uint8_t)(bri * 0.9f), bri));
    }

    // A pale, slightly sickly moon in a top corner — a crescent via one
    // full circle then a BG-colored circle biting a chunk out of it,
    // the same trick used elsewhere in this file for shapes without a
    // smooth-arc primitive to reach for. Only checks the heat at its
    // own center cell (not its whole footprint) before drawing, which
    // is enough given it sits high in the band where flames rarely
    // reach — occasionally getting clipped by a tall flame tongue if
    // one does get up there is a fine, minor cosmetic edge case.
    {
        int mx = w - 22, my = yStart + 16, mr = 9;
        int mgx = mx / CW, mgy = (my - yStart) / CW;
        bool clearSky = !(mgx >= 0 && mgx < fw && mgy >= 0 && mgy < fh) || heat[mgy * MAXFW + mgx] < 9;
        if (clearSky) {
            t.fillCircle(mx, my, mr, t.color565(210, 235, 200));
            t.fillCircle(mx + 5, my - 3, mr - 1, BG);
        }
    }

    // A gnarled dead tree off to one side, like it's standing right at
    // the edge of the firelight — drawn last among the sky-layer
    // elements so it silhouettes over any stars behind it. One
    // occlusion check at its base (not per-branch) rather than a full
    // footprint check: if the fire's right up against its trunk this
    // frame, the whole tree skips drawing that frame instead of
    // rendering a half-erased, glitchy-looking partial tree — reads as
    // a tall flame tongue briefly eclipsing it, which fits the scene.
    {
        int tx = w / 5;
        int groundY = yEnd - 2;
        int trunkTopY = yStart + bandH * 3 / 10;
        int tgx = tx / CW, tgy = (groundY - 4 - yStart) / CW;
        bool clearTree = !(tgx >= 0 && tgx < fw && tgy >= 0 && tgy < fh) || heat[tgy * MAXFW + tgx] < 14;
        if (clearTree && trunkTopY < groundY - 8) {
            uint16_t bark = t.color565(15, 8, 5);
            t.fillRect(tx - 4, trunkTopY, 8, groundY - trunkTopY, bark);
            t.fillRect(tx - 5, groundY - 10, 10, 10, bark);
            // A couple of gnarled main limbs, each forking into a
            // smaller twig near the tip — the classic bare-winter-tree
            // silhouette shape.
            t.drawLine(tx - 2, trunkTopY + 6, tx - 16, trunkTopY - 10, bark);
            t.drawLine(tx - 16, trunkTopY - 10, tx - 22, trunkTopY - 18, bark);
            t.drawLine(tx - 16, trunkTopY - 10, tx - 12, trunkTopY - 20, bark);
            t.drawLine(tx + 2, trunkTopY + 4, tx + 14, trunkTopY - 8, bark);
            t.drawLine(tx + 14, trunkTopY - 8, tx + 20, trunkTopY - 16, bark);
            t.drawLine(tx + 14, trunkTopY - 8, tx + 10, trunkTopY - 18, bark);
            t.drawLine(tx, trunkTopY, tx - 3, trunkTopY - 16, bark);
            t.drawLine(tx - 3, trunkTopY - 16, tx - 8, trunkTopY - 24, bark);
        }
    }

    // Embers: a handful of sparks pop free of the flame and drift
    // upward, cooling from bright yellow through orange to nothing —
    // sells the "fire" far more than the heat grid alone.
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
                ex[i]  = (float)random(0, w);
                ey[i]  = (float)(yEnd - 6);
                evy[i] = 0.6f + (float)random(0, 100) / 100.0f * 0.8f;
            }
            continue;
        }
        ey[i] -= evy[i];
        ex[i] += sinf((float)now / 260.0f + i) * 0.4f;
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

void drawWireframeTunnel(TFT_eSPI& t, uint32_t now, int yStart, int yEnd) {
    int w = t.width();
    int bandH = yEnd - yStart;
    if (bandH < 20) return;
    t.fillRect(0, yStart, w, bandH, BG);

    static const uint8_t RINGS = 12;
    static const uint8_t SIDES = 6;
    float aspect = (float)bandH / (float)w;

    // Wandering center — the tunnel banks and curves instead of
    // staring straight down a fixed pipe, like actually flying through
    // something winding rather than a static painted backdrop.
    float wobT    = (float)now / 2600.0f;
    float wobAmt  = (float)((w < bandH) ? w : bandH) * 0.10f;
    int cx = w / 2       + (int)(sinf(wobT) * wobAmt);
    int cy = yStart + bandH / 2 + (int)(cosf(wobT * 1.3f) * wobAmt * aspect);
    float maxR = sqrtf((float)(w * w + bandH * bandH)) * 0.5f + 12.0f;

    // Speed "breathes" via a slow phase-modulated time warp (surges
    // and eases instead of one constant scroll rate) — cheap fake for
    // real acceleration, but reads the same.
    float warped = (float)now + 3200.0f * sinf((float)now / 5200.0f);

    // The whole tunnel's hue slowly rotates through the vaporwave set,
    // rather than a fixed two-color gradient — the color itself is
    // part of the motion now, not just ring position.
    static const uint16_t hueStops[4] = { CYAN, VAPOR_PURPLE, VAPOR_PINK, VAPOR_BLUE };
    float huePos = fmodf((float)now / 5000.0f, 4.0f);
    int   h0 = (int)huePos % 4, h1 = (h0 + 1) % 4;
    uint16_t tunnelHue = blend(hueStops[h0], hueStops[h1], (uint16_t)((huePos - (int)huePos) * 255));

    float outerRot = (float)now / 1800.0f;
    for (uint8_t s = 0; s < SIDES; s++) {
        float a = outerRot + s * (6.2831853f / SIDES);
        int px = cx + (int)(cosf(a) * maxR);
        int py = cy + (int)(sinf(a) * maxR * aspect);
        t.drawLine(cx, cy, px, py, blend(BG, tunnelHue, 55));
    }

    // A shockwave ring travels down the tunnel every few seconds,
    // flashing whichever ring it currently overlaps much brighter —
    // a periodic beat with nothing to actually beat to.
    static bool     pulseOn = false;
    static uint32_t pulseStart = 0, pulseNextAt = 0;
    static bool     pulseInited = false;
    if (!pulseInited) { pulseNextAt = now + (uint32_t)random(2000, 4000); pulseInited = true; }
    if (!pulseOn && now >= pulseNextAt) { pulseOn = true; pulseStart = now; }
    float pulseDepth = -10.0f;
    if (pulseOn) {
        float prog = (float)(now - pulseStart) / 700.0f;
        if (prog >= 1.0f) {
            pulseOn = false;
            pulseNextAt = now + (uint32_t)random(2000, 4500);
        } else {
            pulseDepth = prog * RINGS;
        }
    }

    for (int i = RINGS - 1; i >= 0; i--) {
        // depth cycles 0 (mouth of the tunnel) .. RINGS (vanishing
        // point) as time advances, wrapping — that's the "flying
        // forward" motion.
        float depth = fmodf((float)i - warped / 220.0f, (float)RINGS);
        if (depth < 0.0f) depth += RINGS;
        float persp = 1.0f / (depth * 0.55f + 0.6f);
        float r = maxR * persp * 0.42f;
        if (r > maxR) r = maxR;
        // Adjacent rings twist opposite directions — an interleaved
        // counter-rotating drill look instead of everything spinning
        // in lockstep.
        float twist = depth * 0.25f * ((i % 2 == 0) ? 1.0f : -1.0f);
        float rot   = outerRot + twist;
        float fade  = 1.0f - depth / RINGS;
        uint16_t ringCol = blend(BG, tunnelHue, (uint16_t)(230 * fade));
        bool hit = fabsf(depth - pulseDepth) < 0.6f;
        if (hit) ringCol = blend(ringCol, WHITE, 200);

        int px[SIDES], py[SIDES];
        for (uint8_t s = 0; s < SIDES; s++) {
            float a = rot + s * (6.2831853f / SIDES);
            px[s] = cx + (int)(cosf(a) * r);
            py[s] = cy + (int)(sinf(a) * r * aspect);
        }
        for (uint8_t s = 0; s < SIDES; s++) {
            t.drawLine(px[s], py[s], px[(s + 1) % SIDES], py[(s + 1) % SIDES], ringCol);
            if (hit) t.drawLine(px[s] + 1, py[s], px[(s + 1) % SIDES] + 1, py[(s + 1) % SIDES], ringCol);
        }
    }

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

void drawBangersText(TFT_eSPI& t, int x, int y, const char* s, uint16_t color, BangersSize size) {
    int cursorX = x;
    for (const char* p = s; *p; p++) {
        const BangersFont::Glyph* g = bangersFind(*p, size);
        if (!g) continue;
        if (g->bitmap) {
            int rowBytes = (g->w + 7) / 8;
            for (int row = 0; row < g->h; row++) {
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

}  // namespace Theme
