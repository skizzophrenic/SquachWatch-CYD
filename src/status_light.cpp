// SquachWatch-CYD — status light implementation. See status_light.h for the
// rules; this file is the rules, in order, plus the PWM underneath them.
#include "status_light.h"
#include "settings.h"
#include "theme.h"
#include <Arduino.h>

// The 2.8" CYD's LED, and the RL Phantom's: same Sunton family, same three
// pins, and nothing else on the Phantom's build uses 4, 16 or 17. The AWOK
// and the 3.5" are unverified and get nothing -- see the header for why that
// is a rule and not a shortcut. Nor does the watch, which has no light.
//
// The Freenove S3 2.8" has one, but not three pins: a WS2812 on GPIO42, from
// its schematic (and its 16 and 17 are the touch controller's SDA and
// interrupt -- PWM on them would be dead touch). STATUS_LIGHT_HW 2 is that
// kind: the same rules and levels, handed to the core's neopixelWrite().
#if defined(ESP32) && !defined(CYD35) && !defined(AWOK) && !defined(SQW_S3)
#define STATUS_LIGHT_HW 1
#elif defined(FREENOVE_S3)
#define STATUS_LIGHT_HW 2
#else
#define STATUS_LIGHT_HW 0
#endif

namespace StatusLight {

#if defined(FREENOVE32)
// The Freenove 3.2": its schematic puts the RGB LED on 22, 16 and 17 (common
// anode, lit low, like the Sunton's) and GPIO4 on the audio amplifier's
// enable. Red on 4 would have switched the amp with every breath of the light.
// Pins from PR #7 (DevOpsDAdams), checked against Freenove's schematic.
static const int     PIN_R = 22, PIN_G = 16, PIN_B = 17;
#else
static const int     PIN_R = 4, PIN_G = 16, PIN_B = 17;
#endif
static const uint8_t CH_R  = 3, CH_G  = 4,  CH_B  = 5;
#if STATUS_LIGHT_HW == 2
static const uint8_t PIN_WS2812 = 42;
#endif

static const uint32_t TICK_MS        = 20;
static const uint32_t BOOT_SWEEP_MS  = 600;
static const uint32_t ALERT_FLASH_MS = 600;    // three 100 ms flashes
static const uint32_t FADE_MS        = 1000;
static const uint32_t BREATHE_MS     = 6000;
static const uint32_t MSG_PERIOD_MS  = 3000;
static const uint32_t VISIT_MS       = 300;

// The five BRIGHTNESS steps, as a duty cap out of 4095. Perceptual, not
// linear: the LED is far brighter than a status light needs to be, and 2 of
// 5 is a bedside glow, not a flashlight.
//
// Twelve-bit PWM, not eight. At eight bits a purple at brightness 2 came out
// as (0, 0, 1): the red and green rounded to nothing and only the blue was
// left, which is what "blue, not purple" on the first Phantom flash was.
static const uint16_t CAP[5] = { 160, 512, 1280, 2560, 4095 };
static const uint8_t  PWM_BITS = 12;
static const uint16_t PWM_MAX  = 4095;

// One colour per background, for IDLE COLOR = BACKGROUND, in the order of
// Settings::Background. The colour each scene is mostly made of; BLACK is
// off, since a black backdrop with a glowing light behind it is not black.
static const uint8_t BG_COLOR[12][3] = {
    { 0, 255, 0 },        // DIGITAL RAIN
    { 255, 255, 255 },    // STARFIELD
    { 255, 140, 0 },      // FLYING TOASTERS: chrome and toast
    { 0, 128, 255 },      // AQUARIUM
    { 0, 255, 64 },       // TERMINAL LOG
    { 200, 255, 0 },      // FIREFLIES
    { 255, 64, 0 },       // FIRE
    { 160, 220, 255 },    // SNOWFALL
    { 0, 255, 255 },      // THE GIBSON
    { 128, 0, 255 },      // WIREFRAME TUNNEL -- retired, row kept to hold the index
    { 255, 0, 128 },      // SYNTHWAVE
    { 0, 0, 0 },          // BLACK
};

// Fixed idle colours, in the order Settings names them. Index 0 is THEME.
static const uint8_t FIXED[10][3] = {
    { 0, 0, 0 },          // THEME: replaced with the palette's accent
    { 255, 0, 0 },        // RED
    { 255, 80, 0 },       // ORANGE
    { 255, 192, 0 },      // YELLOW
    { 0, 255, 0 },        // GREEN
    { 0, 255, 255 },      // CYAN
    { 0, 48, 255 },       // BLUE
    { 128, 0, 255 },      // PURPLE
    { 255, 16, 128 },     // PINK
    { 255, 255, 255 },    // WHITE
};

static uint32_t s_lastTick  = 0;
static uint32_t s_bootAt    = 0;   static bool s_booting = false;
static uint32_t s_testAt    = 0;   static bool s_testing = false;
static uint32_t s_visitAt   = 0;   static bool s_visitOn = false;
static bool     s_prevVisiting = false;
static bool     s_prevAlert    = false;
static uint16_t s_prevAlertCol = 0;
static uint32_t s_alertSince   = 0;
static uint32_t s_fadeAt    = 0;   static bool s_fading  = false;
static uint8_t  s_fadeR = 0, s_fadeG = 0, s_fadeB = 0;
static uint16_t s_outR = PWM_MAX, s_outG = PWM_MAX, s_outB = PWM_MAX;   // last duties written (inverted)

bool available() { return STATUS_LIGHT_HW != 0; }

#if STATUS_LIGHT_HW == 2
// Last 8-bit colour sent; 0xFFFF forces the first write through.
static uint16_t s_pixR = 0xFFFF, s_pixG = 0xFFFF, s_pixB = 0xFFFF;
// 12-bit duty to the WS2812's 8 bits, rounded -- but never to 0 from a lit
// channel. At the default cap the idle breathe spans 1..8 on blue and 0..4
// on red: rounding the small channels away turned purple into blue at the
// bottom of every breath, and the steps read as a slow flicker (seen on the
// first board). A lit channel keeps at least one count, so the hue holds.
static inline uint8_t to8(uint16_t v) {
    const uint32_t q = ((uint32_t)v * 255 + PWM_MAX / 2) / PWM_MAX;
    return (uint8_t)(v && !q ? 1 : q);
}
#endif

static void write(uint16_t r, uint16_t g, uint16_t b) {
#if STATUS_LIGHT_HW == 2
    // One pixel over RMT (initialised on the first call). Same rule as the
    // PWM path: only when the colour actually changes.
    const uint8_t pr = to8(r), pg = to8(g), pb = to8(b);
    if (pr == s_pixR && pg == s_pixG && pb == s_pixB) return;
    neopixelWrite(PIN_WS2812, pr, pg, pb);
    s_pixR = pr; s_pixG = pg; s_pixB = pb;
#elif STATUS_LIGHT_HW
    // Common anode: full duty is off. Only touch the peripheral when a value
    // actually changes, which during a steady alert is never.
    const uint16_t ir = PWM_MAX - r, ig = PWM_MAX - g, ib = PWM_MAX - b;
    if (ir != s_outR) { ledcWrite(CH_R, ir); s_outR = ir; }
    if (ig != s_outG) { ledcWrite(CH_G, ig); s_outG = ig; }
    if (ib != s_outB) { ledcWrite(CH_B, ib); s_outB = ib; }
#else
    (void)r; (void)g; (void)b;
#endif
}

void begin() {
#if STATUS_LIGHT_HW == 2
    write(0, 0, 0);
#elif STATUS_LIGHT_HW
    ledcSetup(CH_R, 5000, PWM_BITS); ledcAttachPin(PIN_R, CH_R);
    ledcSetup(CH_G, 5000, PWM_BITS); ledcAttachPin(PIN_G, CH_G);
    ledcSetup(CH_B, 5000, PWM_BITS); ledcAttachPin(PIN_B, CH_B);
    s_outR = s_outG = s_outB = 0;   // force the first write through
    write(0, 0, 0);
#endif
}

// Armed here, started on the first tick: setup() goes on for a couple of
// seconds after this (the radios), and a sweep timed from here was over
// before the loop ever ran.
void boot(uint32_t now) { (void)now; s_bootAt = 0; s_booting = true; }
void test(uint32_t now) { s_testAt = now; s_testing = true; }
void off() { write(0, 0, 0); }

// A colour and a level, 0..255, before the brightness cap and the gamma.
struct Want { uint8_t r, g, b; uint16_t level; };

// A theme colour, pushed to full saturation. The palette's colours are
// pastel on purpose -- VAPRW4VE's purple is (173, 130, 255) -- and an LED
// mixing that much of every channel just reads as a whitish blue. Taking
// the common part out leaves the hue the screen means. White stays white.
static Want fromRgb565(uint16_t c, uint16_t level) {
    int r = ((c >> 11) & 0x1F) * 255 / 31;
    int g = ((c >> 5)  & 0x3F) * 255 / 63;
    int b = (c & 0x1F) * 255 / 31;
    int lo = r < g ? r : g; if (b < lo) lo = b;
    int hi = r > g ? r : g; if (b > hi) hi = b;
    if (hi - lo > 24) {
        r = (r - lo) * 255 / (hi - lo);
        g = (g - lo) * 255 / (hi - lo);
        b = (b - lo) * 255 / (hi - lo);
    }
    Want w = { (uint8_t)r, (uint8_t)g, (uint8_t)b, level };
    return w;
}

static Want white(uint16_t level) { Want w = { 255, 255, 255, level }; return w; }

// Boring mode keeps the patterns and drops the colour, same spirit as the
// rest of it.
static Want tinted(uint16_t c, uint16_t level) {
    return Settings::boringMode() ? white(level) : fromRgb565(c, level);
}

static Want idleColour(uint16_t level) {
    if (Settings::boringMode()) return white(level);
    const uint8_t ix = Settings::lightColor();
    if (ix == 10) {
        const uint8_t bg = (uint8_t)Settings::background();
        if (bg < 12) { Want w = { BG_COLOR[bg][0], BG_COLOR[bg][1], BG_COLOR[bg][2], level }; return w; }
    }
    if (ix == 0 || ix >= 10) return fromRgb565(Theme::PURPLE, level);
    Want w = { FIXED[ix][0], FIXED[ix][1], FIXED[ix][2], level };
    return w;
}

// Rungs 4 to 6 of the ladder: what shows once nothing louder is happening.
static Want lower(uint32_t now, const Context& c) {
    if (c.unread && Settings::lightMessages()) {
        const uint32_t ph = now % MSG_PERIOD_MS;
        if (ph < 120 || (ph >= 240 && ph < 360)) return tinted(Theme::VAPOR_PINK, 204);
        if (ph < 400) return tinted(Theme::VAPOR_PINK, 0);
    }
    if (s_visitOn) {
        if (now - s_visitAt < VISIT_MS) return tinted(Theme::GREEN, 255);
        s_visitOn = false;
    }
    if (Settings::boringMode() || c.screenDark) return idleColour(0);
    uint16_t level = 0;
    switch (Settings::lightIdle()) {
        case 1: {
            // Half the cap at the peak and a third of that in the trough:
            // it breathes, it never goes out. Cosine so the turn-arounds are
            // soft, gamma below so the low end does not step. Six seconds a
            // breath; four read as panting.
            const float ph = (float)(now % BREATHE_MS) / (float)BREATHE_MS;
            const float b  = 0.5f - 0.5f * cosf(ph * 6.2831853f);
            level = (uint16_t)(128.0f * (0.35f + 0.65f * b));
            break;
        }
        case 2: level = 128; break;
        default: level = 0; break;
    }
    if (c.screenDimmed) level /= 2;
    return idleColour(level);
}

static Want evaluate(uint32_t now, const Context& c) {
    if (!Settings::lightOn() || c.quiet) return white(0);

    if (s_booting) {
        if (s_bootAt == 0) s_bootAt = now;
        const uint32_t dt = now - s_bootAt;
        if (dt < BOOT_SWEEP_MS) {
            const uint8_t step = (uint8_t)(dt / (BOOT_SWEEP_MS / 3));
            Want w = { (uint8_t)(step == 0 ? 255 : 0), (uint8_t)(step == 1 ? 255 : 0),
                       (uint8_t)(step >= 2 ? 255 : 0), 255 };
            return w;
        }
        s_booting = false;
    }

    if (c.update == 1) return tinted(Theme::CYAN, ((now / 250) & 1) ? 0 : 255);
    if (c.update == 2) { Want w = { 0, 255, 0, 255 }; return w; }
    if (c.update == 3) { Want w = { 255, 0, 0, 255 }; return w; }

    if (c.alert && Settings::lightAlerts()) {
        const uint32_t dt = now - s_alertSince;
        if (dt < ALERT_FLASH_MS) return tinted(c.alertColor, ((dt / 100) & 1) ? 0 : 255);
        return tinted(c.alertColor, 255);
    }

    Want under = lower(now, c);
    if (s_fading) {
        const uint32_t dt = now - s_fadeAt;
        if (dt < FADE_MS) {
            // Mix towards whatever is underneath, colour and level both.
            const uint32_t k = dt * 256 / FADE_MS;   // 0..255
            Want w;
            w.r = (uint8_t)((s_fadeR * (256 - k) + under.r * k) >> 8);
            w.g = (uint8_t)((s_fadeG * (256 - k) + under.g * k) >> 8);
            w.b = (uint8_t)((s_fadeB * (256 - k) + under.b * k) >> 8);
            w.level = (uint16_t)((255u * (256 - k) + under.level * k) >> 8);
            return w;
        }
        s_fading = false;
    }
    return under;
}

void tick(uint32_t now, const Context& cIn) {
    if (!available()) return;
    if (now - s_lastTick < TICK_MS) return;
    s_lastTick = now;

    Context c = cIn;

    // The TEST row plays a script over the real context: an alert, then a
    // message, then a visit, then whatever idle is set to.
    if (s_testing) {
        const uint32_t dt = now - s_testAt;
        if (dt < 2000)      { c.alert = true; c.alertColor = Theme::PINK; c.update = 0; }
        else if (dt < 4500) { c.alert = false; c.unread = true; }
        else if (dt < 5000) { c.alert = false; c.unread = false; c.visiting = true; }   // rising edge below blips once
        else                { s_testing = false; }
        c.quiet = false;
    }

    // Edges the rules need: when an alert went up (for the flashes), when it
    // came down (for the fade), when a visitor arrived (for the blip).
    if (c.alert && !s_prevAlert) s_alertSince = now;
    if (!c.alert && s_prevAlert && Settings::lightAlerts() && !c.quiet) {
        Want last = tinted(s_prevAlertCol, 255);
        s_fadeR = last.r; s_fadeG = last.g; s_fadeB = last.b;
        s_fadeAt = now; s_fading = true;
    }
    if (c.alert) s_prevAlertCol = c.alertColor;
    s_prevAlert = c.alert;
    if (c.visiting && !s_prevVisiting) { s_visitOn = true; s_visitAt = now; }
    s_prevVisiting = c.visiting;

    Want w = evaluate(now, c);

    // Per channel: level, then gamma (squaring is close enough to the eye's
    // curve and costs nothing), then the brightness cap, in twelve bits so
    // the small channels of a dim colour survive.
    uint8_t bi = Settings::lightBrightness();
    if (bi < 1) bi = 1; else if (bi > 5) bi = 5;
#if STATUS_LIGHT_HW == 2
    // Twice the PWM LED's caps: eight bits leave the low ones only a handful
    // of steps to breathe through. Still a glow at the default (about 16 of
    // 255 at the peak), and the top rung stays at full.
    uint32_t cap = CAP[bi - 1] * 2; if (cap > PWM_MAX) cap = PWM_MAX;
#else
    const uint32_t cap = CAP[bi - 1];
#endif
    auto chan = [&](uint8_t c) -> uint16_t {
        uint32_t v = (uint32_t)c * w.level / 255;      // 0..255
        v = (v * v) / 255;                              // gamma
        uint32_t d = v * cap / 255;                     // 0..cap
        if (v > 0 && d == 0) d = 1;
        return (uint16_t)d;
    };
    write(chan(w.r), chan(w.g), chan(w.b));
}

} // namespace StatusLight
