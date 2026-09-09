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
#include "ui_phone.h"

#if SQUACH_MESH

#include "theme.h"
#include "settings.h"
#include "squachy.h"
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
uint8_t  s_len     = 0;
int8_t   s_liveKey = -1;      // key whose letter is still being cycled
uint8_t  s_tapIx   = 0;       // which letter of that key
uint32_t s_tapAt   = 0;
bool     s_done    = false;

// The one real tuning parameter. Too short and you cannot type a letter
// twice in a row; too long and typing feels stuck. 800ms is the traditional
// value, and it is traditional because it works.
const uint32_t MULTITAP_MS = 800;

void commitPending() { s_liveKey = -1; s_tapIx = 0; }

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
}

bool uiPhoneDone() { return s_done; }

void uiPhoneTouch(int x, int y, uint32_t now) {
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
            Squachy::setCustomName(s_len ? s_buf : nullptr);
            s_done = true;
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
    t.fillRect(UX + 4, UY + 5, UW, UH, Theme::BLACK);
    steel(t, UX, UY, UW, UH);

    // ---- readout ----------------------------------------------------
    const int dX = UX + 12, dY = UY + 10, dW = UW - 24, dH = 26;
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

    // ---- keypad ------------------------------------------------------
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

    // ---- coin return and plate --------------------------------------
    const int pY = KY + 4 * (KH + KGAP) + 4;
    steel(t, UX + 30, pY, UW - 60, 9, true);
    t.setTextSize(1);
    t.setTextColor(STEEL_DK);
    t.setCursor(UX + (UW - t.textWidth("CYBERDELIA")) / 2, pY + 12);
    t.print("CYBERDELIA");

}

#endif // SQUACH_MESH
