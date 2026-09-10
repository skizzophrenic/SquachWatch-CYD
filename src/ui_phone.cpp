// The payphone -- where you type your Squachy's name.
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
// And for anybody who hates multi-tap regardless, a QWERTY board: one button
// away, and remembered once chosen. It is drawn by this same screen rather
// than a screen of its own because the name being typed lives here, and
// switching layouts mid-word keeps what you have typed -- which is the entire
// point of a bailout. Its geometry, hit test and touch filter are pure
// arithmetic in qwerty.cpp, host-tested against every pixel of both
// rotations.
#include "ui_phone.h"

#if SQUACH_MESH

#include "theme.h"
#include "settings.h"
#include "squachy.h"
#include "qwerty.h"
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

char     s_buf[Squachy::CUSTOM_NAME_MAX + 1];
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

void commitPending() { s_liveKey = -1; s_tapIx = 0; }

// OK, from either layout. One copy of the part that writes to NVS, so the two
// boards cannot drift apart on the one thing that has consequences.
void saveAndClose() {
    commitPending();
    Squachy::setCustomName(s_len ? s_buf : nullptr);
    s_done = true;
}
void deleteLast() { commitPending(); if (s_len) s_buf[--s_len] = '\0'; }
void appendChar(char c) {
    commitPending();
    if (s_len < Squachy::CUSTOM_NAME_MAX) { s_buf[s_len++] = c; s_buf[s_len] = '\0'; }
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

static void qwertyPress(int x, int y) {
    s_filter.down(x, y);
    s_armed   = (int8_t)Qwerty::keyAt(s_keys, s_keyN, x, y, PRESS_REACH);
    s_sliding = (s_armed >= 0);
}

static void qwertyFollow(int x, int y) {
    s_filter.move(x, y);
    s_armed = (int8_t)Qwerty::keyAt(s_keys, s_keyN, s_filter.x, s_filter.y, SLIDE_REACH);
}

static void qwertyRelease() {
    // Nothing is read from the touch here, deliberately. The finger has gone,
    // and the samples just before it went are the ones the filter exists to
    // throw away. Whatever was armed is what gets typed.
    s_sliding = false;
    const int k = s_armed;
    s_armed = -1;
    if (k < 0) return;
    const char c = s_keys[k].ch;
    if      (c == Qwerty::BKSP) deleteLast();
    else if (c == Qwerty::CLR)  { commitPending(); s_len = 0; s_buf[0] = '\0'; }
    else if (c == Qwerty::OK)   saveAndClose();
    else                        appendChar(c);
}

void uiPhoneInit(TFT_eSPI& t) {
    (void)t;
    const char* cur = Squachy::customName();
    s_len = 0;
    s_buf[0] = '\0';
    if (cur) {
        while (s_len < Squachy::CUSTOM_NAME_MAX && cur[s_len]) { s_buf[s_len] = cur[s_len]; s_len++; }
        s_buf[s_len] = '\0';
    }
    commitPending();
    s_done = false;
    s_armed = -1;
    s_sliding = false;
}

bool uiPhoneDone() { return s_done; }

void uiPhoneTouch(int x, int y, uint32_t now, PhoneTouch phase) {
    // Only a QWERTY press that landed on a key has any use for the rest of a
    // gesture. The keypad never sets s_sliding, so for it these are the no-ops
    // they have always been, and a gesture that starts on chrome never turns
    // into a letter however it moves afterwards.
    if (phase == PhoneTouch::MOVE) { if (s_sliding) qwertyFollow(x, y); return; }
    if (phase == PhoneTouch::UP)   { if (s_sliding) qwertyRelease();    return; }

    // BACK leaves WITHOUT saving, which is the whole reason it exists: OK was
    // the only way out, so backing away from a half-typed name meant
    // committing it. Checked before the keypad, since it is outside the pad
    // and cannot collide.
    if (x >= BX && x <= BX + BW && y >= s_backY && y <= s_backY + BH) {
        commitPending();
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

    if (Settings::phoneQwerty()) { qwertyPress(x, y); return; }

    for (int i = 0; i < 12; i++) {
        const int kx = KX + (i % 3) * (KW + KGAP);
        const int ky = KY + (i / 3) * (KH + KGAP);
        if (x < kx || x > kx + KW || y < ky || y > ky + KH) continue;

        if (i == 9) {                                   // DEL
            commitPending();
            if (s_len) s_buf[--s_len] = '\0';
            return;
        }
        if (i == 11) {                                  // OK
            commitPending();
            // An empty name is not a custom name: it clears back to the
            // curated one rather than storing nothing. That is also what
            // keeps a zero-length value away from NVS, which returns early
            // on one without writing -- the bug that made a deleted ignore
            // entry come back on the next reboot.
            saveAndClose();
            return;
        }
        if (i == 10) {                                  // SPACE
            commitPending();
            if (s_len < Squachy::CUSTOM_NAME_MAX) { s_buf[s_len++] = ' '; s_buf[s_len] = '\0'; }
            return;
        }
        const char* letters = KEY_L[i];
        if (!letters[0]) return;                        // 1 carries nothing

        if (s_liveKey == i && (now - s_tapAt) < MULTITAP_MS && s_len) {
            // Same key inside the window: cycle in place rather than append.
            s_tapIx = (uint8_t)((s_tapIx + 1) % strlen(letters));
            s_buf[s_len - 1] = letters[s_tapIx];
        } else {
            // A different key commits whatever was pending immediately. That
            // is what lets you type two letters off one key by waiting, and
            // two off different keys without waiting at all.
            if (s_len >= Squachy::CUSTOM_NAME_MAX) return;
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
    // Right-aligned once it outgrows the window, so the END of the name --
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

    s_backY = backY(h);

    if (qw) {
        // ---- preview ------------------------------------------------------
        // What a release would type, in a fixed box beside the readout rather
        // than a bubble floating over the key. Phones float it because your
        // eye is somewhere else; this screen is 42mm tall and the whole board
        // is inside one glance. A fixed box also never runs off the top on
        // row one, which a floating one does on every press there.
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
        }

        // ---- keys ---------------------------------------------------------
        // Laid out here, every frame, and read by the hit test -- see s_keys.
        s_keyN = Qwerty::layout(w, Qwerty::BAND_TOP, h - Qwerty::BAND_BOTTOM_INSET, s_keys);
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
            if (KEY_L[i][0]) {
                t.setTextSize(1);
                t.setTextColor(lit ? Theme::VAPOR_YELLOW : STEEL_LT);
                t.setCursor(kx + (KW - t.textWidth(KEY_L[i])) / 2, ky + KH - 9);
                t.print(KEY_L[i]);
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
