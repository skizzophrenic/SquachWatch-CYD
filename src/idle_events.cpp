// SquachWatch-CYD — rare decorative idle-screen flourishes
#include "idle_events.h"
#include "theme.h"
#include <Arduino.h>

namespace IdleEvents {

// GLITCH_LINE used to be here: a Bangers message drawn dead centre of
// the band for 1.5s every 60-180s. Centre of the band is exactly where
// Squachy stands and he draws after the background, so it was almost
// entirely hidden behind him -- you saw a couple of pixels of it poke
// out past his shoulders and nothing more. Removed rather than moved:
// the other three idle events all travel across the screen, which is
// what makes them read as events at all.
enum class Kind : uint8_t { UFO, SPARKLE, CRITTER, COUNT };

static uint32_t s_nextAt = 0;
static bool     s_active = false;
static Kind     s_kind   = Kind::UFO;
static uint32_t s_start  = 0;

static uint32_t durationFor(Kind k) {
    switch (k) {
        case Kind::UFO:         return 2400;
        case Kind::SPARKLE:     return 1300;
        case Kind::CRITTER:     return 2000;
        default:                return 1500;
    }
}

// UFO -- direction/height rolled once per play, crosses the whole
// width in one pass.
static int8_t s_ufoDir = 1;
static float  s_ufoY   = 0;

static void rollUfo(int y0, int y1) {
    s_ufoDir = random(0, 2) ? 1 : -1;
    int lo = y0 + 10, hi = y1 - 20;
    s_ufoY = (float)(hi > lo ? random(lo, hi) : lo);
}

static void drawUfo(TFT_eSPI& t, uint32_t elapsed, uint32_t dur, int x0, int x1) {
    using namespace Theme;
    float p = (float)elapsed / (float)dur;
    if (p > 1.0f) p = 1.0f;
    float startX = s_ufoDir > 0 ? (float)(x0 - 30) : (float)(x1 + 30);
    float endX   = s_ufoDir > 0 ? (float)(x1 + 30) : (float)(x0 - 30);
    int cx = (int)(startX + (endX - startX) * p);
    int cy = (int)s_ufoY;

    // Faint pulsing light beam underneath, drawn first so the hull
    // paints over the top of it.
    uint16_t beamCol = blend(BG, CYAN, 50 + (uint16_t)(20 * sinf((float)elapsed / 180.0f)));
    t.fillTriangle(cx - 10, cy + 4, cx + 10, cy + 4, cx, cy + 22, beamCol);

    // Saucer hull + dome.
    t.fillEllipse(cx, cy, 12, 4, blend(BG, WHITE, 220));
    t.fillEllipse(cx, cy - 3, 6, 5, blend(BG, CYAN, 200));

    // Alternating underside lights.
    uint16_t lightCol = ((elapsed / 150) % 2 == 0) ? RED : AMBER;
    t.fillCircle(cx - 7, cy + 3, 1, lightCol);
    t.fillCircle(cx,     cy + 4, 1, lightCol);
    t.fillCircle(cx + 7, cy + 3, 1, lightCol);
}

// Sparkle burst -- a handful of "+"-shaped twinkles at random points,
// staggered so they don't all fire in lockstep.
static const uint8_t  SPARK_N = 10;
static int      s_sparkX[SPARK_N], s_sparkY[SPARK_N];
static uint16_t s_sparkPhase[SPARK_N];
static const uint16_t SPARK_LIFE_MS = 700;

static void rollSparkle(int x0, int y0, int x1, int y1) {
    for (uint8_t i = 0; i < SPARK_N; i++) {
        s_sparkX[i]     = random(x0 + 6, x1 - 6);
        s_sparkY[i]     = random(y0 + 6, y1 - 6);
        s_sparkPhase[i] = (uint16_t)random(0, 600);
    }
}

static void drawSparkle(TFT_eSPI& t, uint32_t elapsed) {
    using namespace Theme;
    for (uint8_t i = 0; i < SPARK_N; i++) {
        if (elapsed < s_sparkPhase[i]) continue;
        uint32_t life = elapsed - s_sparkPhase[i];
        if (life > SPARK_LIFE_MS) continue;
        // 0 -> 1 -> 0 over its own lifetime, not a linear fade, so it
        // actually reads as a twinkle rather than a fade-in blob.
        float twinkle = sinf((float)life / (float)SPARK_LIFE_MS * 3.14159265f);
        uint16_t col = blend(BG, WHITE, (uint16_t)(twinkle * 255));
        int r = 1 + (int)(twinkle * 2);
        t.drawFastHLine(s_sparkX[i] - r, s_sparkY[i], 2 * r + 1, col);
        t.drawFastVLine(s_sparkX[i], s_sparkY[i] - r, 2 * r + 1, col);
    }
}

// Critter flyby -- a small bird/bat V-wing silhouette arcing across.
static int8_t s_critterDir    = 1;
static float  s_critterY      = 0;
static bool   s_critterIsBird = true;

static void rollCritter(int y0, int y1) {
    s_critterDir = random(0, 2) ? 1 : -1;
    int lo = y0 + 10, hi = y1 - 20;
    s_critterY = (float)(hi > lo ? random(lo, hi) : lo);
    s_critterIsBird = random(0, 2) == 0;
}

static void drawCritter(TFT_eSPI& t, uint32_t elapsed, uint32_t dur, int x0, int x1) {
    using namespace Theme;
    float p = (float)elapsed / (float)dur;
    if (p > 1.0f) p = 1.0f;
    float startX = s_critterDir > 0 ? (float)(x0 - 20) : (float)(x1 + 20);
    float endX   = s_critterDir > 0 ? (float)(x1 + 20) : (float)(x0 - 20);
    int cx = (int)(startX + (endX - startX) * p);
    // A gentle rise-and-fall arc rather than a flat line straight
    // across, so it reads as flight, not a slide.
    int cy = (int)(s_critterY - sinf(p * 3.14159265f) * 10.0f);

    bool flapUp = ((elapsed / 120) % 2) == 0;
    int wing = flapUp ? -4 : 2;
    uint16_t col = s_critterIsBird ? WHITE : blend(BG, VAPOR_PURPLE, 220);
    t.drawLine(cx - 6, cy + wing, cx, cy, col);
    t.drawLine(cx,     cy,        cx + 6, cy + wing, col);
}

// Glitch one-liner -- its own separate joke pool from Squachy's normal
// idle chatter, rendered like a corrupted transmission.
void tick(TFT_eSPI& t, uint32_t now, int x0, int y0, int x1, int y1, bool advance) {
    if (advance) {
        if (s_active) {
            if (now - s_start >= durationFor(s_kind)) {
                s_active = false;
                s_nextAt = now + (uint32_t)random(60000, 180001);
            }
        } else if (s_nextAt == 0) {
            // First roll ever -- give the screen a little while to
            // settle before the very first surprise.
            s_nextAt = now + (uint32_t)random(45000, 120001);
        } else if (now >= s_nextAt) {
            s_kind   = (Kind)random(0, (int)Kind::COUNT);
            s_start  = now;
            s_active = true;
            switch (s_kind) {
                case Kind::UFO:         rollUfo(y0, y1); break;
                case Kind::SPARKLE:     rollSparkle(x0, y0, x1, y1); break;
                case Kind::CRITTER:     rollCritter(y0, y1); break;
                default: break;
            }
        }
    }

    if (!s_active) return;
    uint32_t elapsed = now - s_start;
    switch (s_kind) {
        case Kind::UFO:         drawUfo(t, elapsed, durationFor(s_kind), x0, x1); break;
        case Kind::SPARKLE:     drawSparkle(t, elapsed); break;
        case Kind::CRITTER:     drawCritter(t, elapsed, durationFor(s_kind), x0, x1); break;
        default: break;
    }
}

}  // namespace IdleEvents
