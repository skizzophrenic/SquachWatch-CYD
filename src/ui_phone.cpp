// The payphone -- where you type your Squachy's name, and a message.
//
// Twelve keys rather than a keyboard's twenty-nine, which is not only the
// better look. On a 320px screen twenty-nine keys means ten columns of about
// 30px; twelve means targets of 48x30. Fewer, larger targets on a resistive
// panel is the difference between typing and fighting, so the payphone is
// the more robust option as well as the more fun one.
//
// Multi-tap the way everyone over thirty already knows: 2 once is A, twice
// is B, three times is C. Uppercase only, which is free here -- every
// built-in nickname is caps and the display face is uppercase-only, so there
// is no shift key and no case handling anywhere in this file.
//
// A message gets what a phone gave a text: the digit after the letters on
// every key, punctuation on 1, and 0 after the space. A name keeps letters
// only -- it rides in the advert, and letters are what every decoder accepts.
//
// And for anybody who hates multi-tap regardless, a QWERTY board: one button
// away, and remembered once chosen. It is drawn by this same screen rather
// than a screen of its own because the text being typed lives here, and
// switching layouts mid-word keeps what you have typed -- which is the entire
// point of a bailout. Its geometry, hit test and touch filter are pure
// arithmetic in qwerty.cpp, host-tested against every pixel of both
// rotations and both boards.
#include "ui_phone.h"

#if SQUACH_MESH

#include "theme.h"
#include "settings.h"
#include "squachy.h"
#include "qwerty.h"
#include "meshmsg.h"
#include <Arduino.h>   // millis(), for the PIN pad's wrong-guess shake
#include <stdio.h>
#include <string.h>

namespace {

// The Win95 ramp, chosen for the IGNORE button because it survives RGB332
// where the authentic #c0c0c0 / #dfdfdf pair collapses into one colour. A
// steel payphone body inherits that solved problem for nothing.
const uint16_t STEEL_HI = Theme::W95_HILITE;
const uint16_t STEEL_LT = Theme::W95_LIGHT;
const uint16_t STEEL    = Theme::W95_FACE;
const uint16_t STEEL_SH = Theme::W95_SHADOW;
const uint16_t STEEL_DK = Theme::W95_DKSHADOW;

const int UX = 78, UY = 8, UW = 164, UH = 224;
const int KW = 48, KH = 30, KGAP = 3;
const int KX = UX + (UW - (KW * 3 + KGAP * 2)) / 2;
const int KY = UY + 60;

const char* const KEY_D[12] = { "1","2","3","4","5","6","7","8","9","*","0","#" };
// ITU E.161, not the original Bell layout. Authentic Bell keypads had no Q
// and no Z -- 7 was PRS and 9 was WXY -- and copying that faithfully would
// make QUINN and ZEKE literally untypeable. The chrome carries the period
// feel instead.
const char* const KEY_L[12] = { "", "ABC","DEF","GHI","JKL","MNO",
                                "PQRS","TUV","WXYZ","DEL","SPACE","OK" };
// What each key cycles through when typing a message. Every character here
// must be in MeshMsg::TEXT_CHARSET -- the keyboards are the only way text gets
// into a message, so they are what keeps an untypeable one from existing.
const char* const KEY_M[12] = { ".,?!'-1", "ABC2","DEF3","GHI4","JKL5","MNO6",
                                "PQRS7","TUV8","WXYZ9", "", " 0", "" };

enum class Mode : uint8_t { NAME, MESSAGE, PIN };
Mode     s_mode   = Mode::NAME;
// PIN mode: how many digits make a full PIN, whether BACK exists, the line
// above the dots, and whether a full PIN is sitting ready to be read.
uint8_t     s_pinLen     = 4;
bool        s_pinBack    = false;
const char* s_pinPrompt  = "";
bool        s_pinReady    = false;
const char* s_pinWaitMsg = nullptr;   // lockout banner, shown instead of dots
uint32_t    s_pinShakeAt = 0;          // a wrong-PIN shake
bool        s_pinForgot   = false;     // FORGOT offered (the lock screen)
uint8_t     s_forgotTaps  = 0;
uint32_t    s_forgotAt    = 0;
bool        s_pinForgotHit = false;
const uint32_t FORGOT_ARM_MS = 5000;
uint8_t  s_max    = Squachy::CUSTOM_NAME_MAX;
bool     s_msgOk  = false;      // a message ended on OK, not BACK

char     s_buf[MeshMsg::TEXT_MAX + 1];
// Filled by the draw, read by the hit test, so the two cannot disagree about
// where the button is -- the same reason every other row list here computes
// its geometry once.
int      s_backY   = 0;
uint8_t  s_len     = 0;
int8_t   s_liveKey = -1;      // key whose letter is still being cycled
uint8_t  s_tapIx   = 0;       // which letter of that key
uint32_t s_tapAt   = 0;
bool     s_done    = false;

// QWERTY. The keys are laid out by the draw and read by the hit test, for the
// same reason as s_backY below: one computation, so the two cannot disagree
// about where anything is.
Qwerty::Key         s_keys[Qwerty::KEY_N];
uint8_t             s_keyN    = 0;
int8_t              s_armed   = -1;     // the key a release would type
bool                s_sliding = false;  // a press that started on a key
Qwerty::TouchFilter s_filter;

// The one real tuning parameter. Too short and you cannot type a letter
// twice in a row; too long and typing feels stuck. 800ms is the traditional
// value, and it is traditional because it works.
const uint32_t MULTITAP_MS = 800;

// A resistive panel sometimes loses contact for a single frame in the middle
// of a press, and that arrives here as a release and a fresh press on the
// same spot. Measured typing on the QWERTY board: 35 ms apart, the exact same
// pixel, and a doubled letter. Nobody taps the same key twice that fast -- a
// real double letter is a tenth of a second or more -- so a press on the key
// just released, inside this window, is the same touch and is absorbed. On
// the keypad it matters more: there the second press would not double the
// letter, it would cycle it, A to B.
const uint32_t BOUNCE_MS = 60;
uint32_t s_lastUpAt   = 0;
int8_t   s_lastQwKey  = -1;     // what the last QWERTY release typed
int8_t   s_lastPadKey = -1;     // the last keypad key pressed

inline bool msg() { return s_mode == Mode::MESSAGE; }
inline bool pin() { return s_mode == Mode::PIN; }

void commitPending() { s_liveKey = -1; s_tapIx = 0; }

// OK, from either layout. One copy of the part with consequences, so the two
// boards cannot drift apart on it.
void saveAndClose() {
    commitPending();
    if (msg()) {
        // Trailing spaces are nothing anybody meant to send.
        while (s_len && s_buf[s_len - 1] == ' ') s_buf[--s_len] = '\0';
        s_msgOk = s_len > 0;
    } else {
        Squachy::setCustomName(s_len ? s_buf : nullptr);
    }
    s_done = true;
}
void deleteLast() { commitPending(); if (s_len) s_buf[--s_len] = '\0'; }
void appendChar(char c) {
    commitPending();
    if (s_len < s_max) { s_buf[s_len++] = c; s_buf[s_len] = '\0'; }
}

void bevel(TFT_eSPI& t, int x, int y, int w, int h, uint16_t face,
           uint16_t lit, uint16_t litSoft, uint16_t shd, uint16_t shdSoft, bool sunk) {
    t.fillRect(x + 2, y + 2, w - 4, h - 4, face);
    const uint16_t oTL = sunk ? shd : lit,         oBR = sunk ? lit : shd;
    const uint16_t iTL = sunk ? shdSoft : litSoft, iBR = sunk ? litSoft : shdSoft;
    t.drawFastHLine(x, y, w, oTL);         t.drawFastVLine(x, y, h, oTL);
    t.drawFastHLine(x, y + h - 1, w, oBR); t.drawFastVLine(x + w - 1, y, h, oBR);
    t.drawFastHLine(x + 1, y + 1, w - 2, iTL);     t.drawFastVLine(x + 1, y + 1, h - 2, iTL);
    t.drawFastHLine(x + 1, y + h - 2, w - 2, iBR); t.drawFastVLine(x + w - 2, y + 1, h - 2, iBR);
}

void steel(TFT_eSPI& t, int x, int y, int w, int h, bool sunk = false) {
    bevel(t, x, y, w, h, STEEL, STEEL_HI, STEEL_LT, STEEL_DK, STEEL_SH, sunk);
}

void start(const char* text) {
    s_len = 0;
    s_buf[0] = '\0';
    if (text) {
        while (s_len < s_max && text[s_len]) { s_buf[s_len] = text[s_len]; s_len++; }
        s_buf[s_len] = '\0';
    }
    commitPending();
    s_done    = false;
    s_msgOk   = false;
    s_armed   = -1;
    s_sliding = false;
}

} // namespace

// The phone body occupies x 78..242, so the strip down the left of the screen
// is free at any rotation. Bottom-left because that is where every other
// screen in this firmware puts BACK.
const int BX = 4, BW = 68, BH = 26;
static int backY(int screenH) { return screenH - BH - 6; }
static_assert(BH + 6 + 6 == Qwerty::BAND_BOTTOM_INSET,
              "the QWERTY band must end above the BACK row -- see qwerty.h");

// The layout switch. It sits beside BACK in both layouts, because that is
// where the hand already goes to leave, and it always names the OTHER layout:
// the button says where it takes you, not where you are.
//
// On the keypad it stacks above BACK in the free left strip, the only space
// the phone body leaves at both rotations. On QWERTY the keyboard takes the
// full width, so it moves to the far end of BACK's own row.
static int s_toggleX = 0, s_toggleY = 0;
static void toggleRect(int w, int h, bool qwerty) {
    if (qwerty) { s_toggleX = w - BX - BW; s_toggleY = backY(h); }
    else        { s_toggleX = BX;          s_toggleY = backY(h) - BH - 6; }
}

// How far outside the board a press can land and still count: the gutters
// and a hair past the outer keys, so a press on the readout or the chrome is
// never mistaken for a letter.
static const int PRESS_REACH = 6;
// How far a slide can drift off the whole board before the preview clears.
// Releasing out there types nothing -- a way to change your mind that costs
// nothing to learn.
static const int SLIDE_REACH = 12;

// What a key says. Letters are themselves; the controls borrow the keypad's
// own words, DEL rather than an arrow the 5x7 font does not have.
static const char* keyLabel(char c) {
    static char one[2];
    switch (c) {
        case Qwerty::BKSP: return "DEL";
        case Qwerty::CLR:  return "CLR";
        case Qwerty::OK:   return "OK";
        case ' ':          return "SPACE";
        default: one[0] = c; one[1] = '\0'; return one;
    }
}

// The small print under a keypad digit.
static const char* padLabel(int i) {
    if (msg() && i == 0) return ".,?!'-";
    return KEY_L[i];
}

// QWERTY_TRACE: every press, every slide sample (raw, filtered, the filter's
// agreement count, the key under it) and every release, over serial -- the
// data the touch filter's three constants are tuned against. Off in every
// shipping build; switched on for a bench session with
//   PLATFORMIO_BUILD_FLAGS=-DQWERTY_TRACE pio run -e cyd-fast -t upload ...
#ifdef QWERTY_TRACE
#include <Arduino.h>
static const char* traceKey(int k) { return k >= 0 ? keyLabel(s_keys[k].ch) : "-"; }
#define QW_TRACE(...) Serial.printf(__VA_ARGS__)
#else
#define QW_TRACE(...) do {} while (0)
#endif

static void qwertyPress(int x, int y, uint32_t now) {
    s_filter.down(x, y);
    s_armed   = (int8_t)Qwerty::keyAt(s_keys, s_keyN, x, y, PRESS_REACH);
    s_sliding = (s_armed >= 0);
    // The panel dropping out for a frame -- see BOUNCE_MS. Armed to
    // nothing, so this press's own release types nothing.
    if (s_armed >= 0 && s_armed == s_lastQwKey && now - s_lastUpAt < BOUNCE_MS) {
        QW_TRACE("[qw] bounce absorbed on %s\n", traceKey(s_armed));
        s_armed   = -1;
        s_sliding = false;
        return;
    }
    QW_TRACE("[qw] D %d,%d %lu key=%s\n", x, y, (unsigned long)millis(), traceKey(s_armed));
}

static void qwertyFollow(int x, int y) {
    s_filter.move(x, y);
    s_armed = (int8_t)Qwerty::keyAt(s_keys, s_keyN, s_filter.x, s_filter.y, SLIDE_REACH);
    QW_TRACE("[qw] M %d,%d f=%d,%d cn=%u key=%s\n", x, y, s_filter.x, s_filter.y,
             (unsigned)s_filter.cn, traceKey(s_armed));
}

static void qwertyRelease() {
    // Nothing is read from the touch here, deliberately. The finger has gone,
    // and the samples just before it went are the ones the filter exists to
    // throw away. Whatever was armed is what gets typed.
    s_sliding = false;
    const int k = s_armed;
    s_armed = -1;
    QW_TRACE("[qw] U %lu typed=%s\n", (unsigned long)millis(), traceKey(k));
    s_lastQwKey = (int8_t)k;
    if (k < 0) return;
    const char c = s_keys[k].ch;
    if      (c == Qwerty::BKSP) deleteLast();
    else if (c == Qwerty::CLR)  { commitPending(); s_len = 0; s_buf[0] = '\0'; }
    else if (c == Qwerty::OK)   saveAndClose();
    else                        appendChar(c);
}

void uiPhoneInit(TFT_eSPI& t) {
    (void)t;
    s_mode = Mode::NAME;
    s_max  = Squachy::CUSTOM_NAME_MAX;
    start(Squachy::customName());
}

void uiPhoneInitMessage(TFT_eSPI& t, const char* text) {
    (void)t;
    s_mode = Mode::MESSAGE;
    s_max  = MeshMsg::TEXT_MAX;
    start(text);
}

bool        uiPhoneDone()        { return s_done; }
bool        uiPhoneMessageMode() { return msg(); }
const char* uiPhoneMessage()      { return (msg() && s_msgOk) ? s_buf : nullptr; }

void uiPhoneInitPin(TFT_eSPI& t, uint8_t len, const char* prompt, bool allowBack) {
    (void)t;
    s_mode      = Mode::PIN;
    s_max       = len;
    s_pinLen    = len;
    s_pinBack   = allowBack;
    s_pinPrompt = prompt ? prompt : "";
    s_pinReady  = false;
    s_pinWaitMsg = nullptr;
    s_pinShakeAt = 0;
    s_pinForgot  = false;
    s_forgotTaps = 0;
    s_pinForgotHit = false;
    start(nullptr);
}
bool        uiPhonePinReady()  { return s_pinReady; }
const char* uiPhonePinDigits() { return s_buf; }
void        uiPhonePinReject() { s_len = 0; s_buf[0] = '\0'; s_pinReady = false; s_done = false; s_pinShakeAt = millis(); }
void        uiPhonePinPrompt(const char* p) { s_pinPrompt = p ? p : ""; }
void        uiPhonePinAllowForgot(bool a) { s_pinForgot = a; s_forgotTaps = 0; s_pinForgotHit = false; }
bool        uiPhonePinForgot() { return s_pinForgotHit; }
static bool forgotArmed(uint32_t now) { return s_pinForgot && s_forgotTaps == 1 && now - s_forgotAt < FORGOT_ARM_MS; }
void        uiPhonePinWait(const char* m) { s_pinWaitMsg = m; if (m) { s_len = 0; s_buf[0] = '\0'; } }

// A tap on the PIN pad: digits fill the dots, DEL rubs one out, and the PIN
// submits itself the instant the last dot lands -- no OK to hunt for. During a
// lockout wait nothing is accepted.
static void pinTouch(int x, int y, uint32_t now) {
    // FORGOT works during a lockout wait too: that is exactly when somebody
    // who has forgotten the PIN is standing there.
    if (s_pinForgot && x >= BX && x <= BX + BW && y >= s_backY && y <= s_backY + BH) {
        if (forgotArmed(now)) { s_pinForgotHit = true; s_pinReady = false; s_done = true; }
        else                  { s_forgotTaps = 1; s_forgotAt = now; }
        return;
    }
    if (s_pinWaitMsg) return;
    if (s_pinBack && x >= BX && x <= BX + BW && y >= s_backY && y <= s_backY + BH) {
        s_pinReady = false;
        s_done = true;
        return;
    }
    for (int i = 0; i < 12; i++) {
        const int kx = KX + (i % 3) * (KW + KGAP);
        const int ky = KY + (i / 3) * (KH + KGAP);
        if (x < kx || x > kx + KW || y < ky || y > ky + KH) continue;
        if (i == 9) { if (s_len) s_buf[--s_len] = '\0'; return; }   // DEL
        char d = 0;
        if (i <= 8)       d = (char)('1' + i);      // 1..9
        else if (i == 10) d = '0';                  // 0
        else return;                                // * / # unused
        if (s_len < s_pinLen) { s_buf[s_len++] = d; s_buf[s_len] = '\0'; }
        if (s_len == s_pinLen) { s_pinReady = true; s_done = true; }
        return;
    }
}

// The PIN pad. Its own compact layout -- dots where the readout is, a keypad
// of bare digits, no letters, no QWERTY toggle, no message counter.
static void drawPinPad(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width(), h = t.height();
    Theme::Palette saved = Theme::dimPaletteForOverlay(120);
    Theme::drawActiveBackground(t, now, 0, h, eng);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 140);
    s_backY = backY(h);

    const int ux = UX, uy = UY;
    t.fillRect(ux + 4, uy + 5, UW, UH, Theme::BLACK);
    steel(t, ux, uy, UW, UH);

    // Prompt -- or, with FORGOT armed, what a second tap will do.
    const bool armed = forgotArmed(now);
    const char* pr = armed ? "TAP AGAIN: WIPE + UNLOCK" : s_pinPrompt;
    t.setTextSize(1);
    t.setTextColor(armed ? Theme::RED : STEEL_LT);
    t.setCursor(ux + (UW - t.textWidth(pr)) / 2, uy + 8);
    t.print(pr);

    // The dots, or the wait banner in their place. A shake nudges them for a
    // moment after a wrong PIN.
    const int dY = uy + 22, dH = 26;
    steel(t, ux + 9, dY - 3, UW - 18, dH + 6, true);
    t.fillRect(ux + 12, dY, UW - 24, dH, Theme::BLACK);
    if (s_pinWaitMsg) {
        t.setTextColor(Theme::RED);
        t.setTextSize(1);
        t.setCursor(ux + (UW - t.textWidth(s_pinWaitMsg)) / 2, dY + (dH - 8) / 2);
        t.print(s_pinWaitMsg);
    } else {
        int shake = 0;
        if (s_pinShakeAt && now - s_pinShakeAt < 300) shake = ((now / 40) % 2) ? 3 : -3;
        const int gap = 18, tot = (s_pinLen - 1) * gap;
        int cx = ux + UW / 2 - tot / 2 + shake, cy = dY + dH / 2;
        for (uint8_t i = 0; i < s_pinLen; i++) {
            const bool filled = i < s_len;
            if (filled) t.fillCircle(cx + i * gap, cy, 4, Theme::GREEN);
            else        t.drawCircle(cx + i * gap, cy, 4, STEEL_LT);
        }
    }

    // Digits, reusing the payphone keypad geometry.
    for (int i = 0; i < 12; i++) {
        const int kx = KX + (i % 3) * (KW + KGAP);
        const int ky = KY + (i / 3) * (KH + KGAP);
        const char* lab = (i == 9) ? "DEL" : (i <= 8) ? KEY_D[i] : (i == 10) ? "0" : "";
        if (!lab[0]) continue;                       // * and # left blank
        bevel(t, kx, ky, KW, KH, Theme::TASKBAR, STEEL_LT, STEEL, STEEL_DK, STEEL_SH, false);
        t.setTextSize(2);
        if (t.textWidth(lab) > KW - 6) t.setTextSize(1);
        t.setTextColor(Theme::WHITE);
        t.setCursor(kx + (KW - t.textWidth(lab)) / 2, ky + (KH - t.fontHeight()) / 2);
        t.print(lab);
    }

    if (s_pinBack)
        Theme::drawButton(t, BX, s_backY, BW, BH, "[ BACK ]", false);
    else if (s_pinForgot)
        Theme::drawButton(t, BX, s_backY, BW, BH, armed ? "[ WIPE? ]" : "[ FORGOT ]", armed);
}

void uiPhoneTouch(int x, int y, uint32_t now, PhoneTouch phase) {
    if (pin()) { if (phase == PhoneTouch::DOWN) pinTouch(x, y, now); return; }
    // Only a QWERTY press that landed on a key has any use for the rest of a
    // gesture. The keypad never sets s_sliding, so for it these are the no-ops
    // they have always been, and a gesture that starts on chrome never turns
    // into a letter however it moves afterwards.
    if (phase == PhoneTouch::MOVE) { if (s_sliding) qwertyFollow(x, y); return; }
    if (phase == PhoneTouch::UP) {
        s_lastUpAt = now;
        if (s_sliding) qwertyRelease();
        return;
    }

    // BACK leaves WITHOUT saving, which is the whole reason it exists: OK was
    // the only way out, so backing away from a half-typed name meant
    // committing it. Checked before the keypad, since it is outside the pad
    // and cannot collide.
    if (x >= BX && x <= BX + BW && y >= s_backY && y <= s_backY + BH) {
        commitPending();
        s_msgOk = false;
        s_done = true;
        return;
    }
    // The layout switch. Pending multi-tap letters are committed first, so
    // switching mid-letter keeps the letter rather than half of a cycle.
    if (x >= s_toggleX && x <= s_toggleX + BW && y >= s_toggleY && y <= s_toggleY + BH) {
        commitPending();
        s_sliding = false;
        s_armed = -1;
        Settings::togglePhoneQwerty();
        return;
    }

    if (Settings::phoneQwerty()) { qwertyPress(x, y, now); return; }

    for (int i = 0; i < 12; i++) {
        const int kx = KX + (i % 3) * (KW + KGAP);
        const int ky = KY + (i / 3) * (KH + KGAP);
        if (x < kx || x > kx + KW || y < ky || y > ky + KH) continue;
        // The panel dropping out for a frame -- see BOUNCE_MS.
        if (i == s_lastPadKey && now - s_lastUpAt < BOUNCE_MS) return;
        s_lastPadKey = (int8_t)i;

        if (i == 9) {                                   // DEL
            commitPending();
            if (s_len) s_buf[--s_len] = '\0';
            return;
        }
        if (i == 11) {                                  // OK
            // An empty name is not a custom name: it clears back to the
            // curated one rather than storing nothing. That is also what
            // keeps a zero-length value away from NVS, which returns early
            // on one without writing -- the bug that made a deleted ignore
            // entry come back on the next reboot.
            saveAndClose();
            return;
        }
        if (i == 10 && !msg()) {                        // SPACE, for a name
            commitPending();
            if (s_len < s_max) { s_buf[s_len++] = ' '; s_buf[s_len] = '\0'; }
            return;
        }
        const char* letters = msg() ? KEY_M[i] : KEY_L[i];
        if (!letters[0]) return;                        // 1 carries nothing, for a name

        if (s_liveKey == i && (now - s_tapAt) < MULTITAP_MS && s_len) {
            // Same key inside the window: cycle in place rather than append.
            s_tapIx = (uint8_t)((s_tapIx + 1) % strlen(letters));
            s_buf[s_len - 1] = letters[s_tapIx];
        } else {
            // A different key commits whatever was pending immediately. That
            // is what lets you type two letters off one key by waiting, and
            // two off different keys without waiting at all.
            if (s_len >= s_max) return;
            s_tapIx = 0;
            s_buf[s_len++] = letters[0];
            s_buf[s_len] = '\0';
            s_liveKey = (int8_t)i;
        }
        s_tapAt = now;
        return;
    }
}

void uiPhoneTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    (void)eng;
    if (pin()) { drawPinPad(t, now, eng); return; }
    const int w = t.width(), h = t.height();

    // The window closing is what commits a letter, so it is checked every
    // frame rather than only on the next tap -- otherwise the caret keeps
    // claiming the last letter is editable long after it stopped being.
    if (s_liveKey >= 0 && (now - s_tapAt) >= MULTITAP_MS) commitPending();

    // The environment, knocked back so an object can stand in front of it.
    Theme::Palette saved = Theme::dimPaletteForOverlay(120);
    Theme::drawActiveBackground(t, now, 0, h, eng);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 110);

    // Contact shadow. Without one it reads as a sticker on the glass rather
    // than a thing standing in the room.
    //
    // QWERTY takes the full width: the same steel and the same shadow, the
    // same object reshaped to hold ten columns instead of three. It ends just
    // above BACK's row, which is also where the keyboard's band ends.
    const bool qw = Settings::phoneQwerty();
    const int ux = qw ? 2 : UX, uy = qw ? 4 : UY;
    const int uw = qw ? w - 4 : UW;
    const int uh = qw ? (h - Qwerty::BAND_BOTTOM_INSET + 4) - uy : UH;
    t.fillRect(ux + 4, uy + 5, uw, uh, Theme::BLACK);
    steel(t, ux, uy, uw, uh);

    // ---- readout ----------------------------------------------------
    // On QWERTY the readout spans the width, less the preview box to its
    // right. Everything below -- the right-alignment and the caret -- is the
    // same code for both boards.
    const int dX = qw ? 10 : UX + 12, dY = qw ? 10 : UY + 10;
    const int dW = qw ? w - 66 : UW - 24, dH = 26;
    steel(t, dX - 3, dY - 3, dW + 6, dH + 6, true);
    t.fillRect(dX, dY, dW, dH, Theme::BLACK);
    t.drawRect(dX, dY, dW, dH, Theme::GREEN);
    t.setTextSize(2);
    t.setTextWrap(false);
    t.setTextColor(Theme::GREEN);
    // Right-aligned once it outgrows the window, so the END of the text --
    // the part being typed -- is always the part you can see.
    const int tw = t.textWidth(s_buf);
    int tx = dX + 7;
    if (tw > dW - 20) tx = dX + dW - 13 - tw;
    t.setCursor(tx, dY + (dH - 14) / 2);
    t.print(s_buf);
    // A caret that stops blinking while a letter is still editable is one
    // glyph doing two jobs: it also says "this one can still change".
    if (s_liveKey >= 0 || ((now / 400) % 2) == 0) {
        t.setTextColor(s_liveKey >= 0 ? Theme::VAPOR_YELLOW : Theme::GREEN);
        t.print("_");
    }
    // A message has a limit worth seeing coming; a name's twelve is its own
    // readout.
    char rem[8];
    snprintf(rem, sizeof rem, "%u LEFT", (unsigned)(s_max - s_len));

    s_backY = backY(h);

    if (qw) {
        // ---- preview ------------------------------------------------------
        // What a release would type, in a fixed box beside the readout rather
        // than a bubble floating over the key. Phones float it because your
        // eye is somewhere else; this screen is 42mm tall and the whole board
        // is inside one glance. A fixed box also never runs off the top on
        // row one, which a floating one does on every press there. Idle, it
        // says how much of a message is left.
        const int pw = 34, px = w - 10 - pw;
        steel(t, px - 3, dY - 3, pw + 6, dH + 6, true);
        t.fillRect(px, dY, pw, dH, Theme::BLACK);
        if (s_armed >= 0) {
            const char* lab = keyLabel(s_keys[s_armed].ch);
            t.setTextSize(2);
            if (t.textWidth(lab) > pw - 4) t.setTextSize(1);
            t.setTextColor(Theme::VAPOR_YELLOW);
            t.setCursor(px + (pw - t.textWidth(lab)) / 2, dY + (dH - t.fontHeight()) / 2);
            t.print(lab);
        } else if (msg()) {
            char n[4];
            snprintf(n, sizeof n, "%u", (unsigned)(s_max - s_len));
            t.setTextSize(1);
            t.setTextColor(STEEL_LT);
            t.setCursor(px + (pw - t.textWidth(n)) / 2, dY + (dH - t.fontHeight()) / 2);
            t.print(n);
        }

        // ---- keys ---------------------------------------------------------
        // Laid out here, every frame, and read by the hit test -- see s_keys.
        s_keyN = Qwerty::layout(w, Qwerty::BAND_TOP, h - Qwerty::BAND_BOTTOM_INSET, s_keys, msg());
        for (uint8_t i = 0; i < s_keyN; i++) {
            const Qwerty::Key& k = s_keys[i];
            const bool lit = (s_armed == (int8_t)i);
            bevel(t, k.x, k.y, k.w, k.h, lit ? Theme::PURPLE : Theme::TASKBAR,
                  STEEL_LT, STEEL, STEEL_DK, STEEL_SH, false);
            const char* lab = keyLabel(k.ch);
            t.setTextSize(2);
            if (t.textWidth(lab) > k.w - 6) t.setTextSize(1);
            t.setTextColor(lit ? Theme::VAPOR_YELLOW : Theme::WHITE);
            t.setCursor(k.x + (k.w - t.textWidth(lab)) / 2, k.y + (k.h - t.fontHeight()) / 2);
            t.print(lab);
        }
    } else {
        if (msg()) {
            t.setTextSize(1);
            t.setTextColor(STEEL_DK);
            t.setCursor(UX + UW - 12 - t.textWidth(rem), dY + dH + 6);
            t.print(rem);
        }
        // ---- keypad -------------------------------------------------------
        for (int i = 0; i < 12; i++) {
            const int kx = KX + (i % 3) * (KW + KGAP);
            const int ky = KY + (i / 3) * (KH + KGAP);
            const bool lit = (s_liveKey == i);
            bevel(t, kx, ky, KW, KH, lit ? Theme::PURPLE : Theme::TASKBAR,
                  STEEL_LT, STEEL, STEEL_DK, STEEL_SH, false);
            t.setTextSize(2);
            t.setTextColor(Theme::WHITE);
            t.setCursor(kx + (KW - t.textWidth(KEY_D[i])) / 2, ky + 5);
            t.print(KEY_D[i]);
            const char* small = padLabel(i);
            if (small[0]) {
                t.setTextSize(1);
                t.setTextColor(lit ? Theme::VAPOR_YELLOW : STEEL_LT);
                t.setCursor(kx + (KW - t.textWidth(small)) / 2, ky + KH - 9);
                t.print(small);
            }
        }

        // ---- coin return and plate ----------------------------------------
        const int pY = KY + 4 * (KH + KGAP) + 4;
        steel(t, UX + 30, pY, UW - 60, 9, true);
        t.setTextSize(1);
        t.setTextColor(STEEL_DK);
        t.setCursor(UX + (UW - t.textWidth("CYBERDELIA")) / 2, pY + 12);
        t.print("CYBERDELIA");
    }

    // ---- back -------------------------------------------------------------
    Theme::drawButton(t, BX, s_backY, BW, BH, "[ BACK ]", false);

    // ---- layout switch ----------------------------------------------------
    toggleRect(w, h, qw);
    Theme::drawButton(t, s_toggleX, s_toggleY, BW, BH, qw ? "[ KEYPAD ]" : "[ QWERTY ]", false);

}

#endif // SQUACH_MESH
