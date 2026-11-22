// The alert card's skins: the colour roles' two promises (alert_skin.h).
//
// Every role, in every theme, is a colour an 8-bit frame holds exactly; a
// built-in theme's background stays that theme's background; DU5K keeps its
// hand-picked dusk and its light windows. The palettes are theme.cpp's own
// rows, copied: a change there that breaks a promise should fail here too.
#include "test_util.h"
#include "alert_skin.h"
#include <stdint.h>

using namespace AlertSkin;

static const uint16_t PALS[7][12] = {
    { 0x0801, 0x0803, 0xAC1F, 0x07FF, 0xF96F, 0xFB99, 0xBB5F, 0x067F, 0xFFD2, 0x07E0, 0xFD20, 0xF800 },   // VAPRW4VE
    { 0x0000, 0x0120, 0x07E0, 0x2FE6, 0x8FE8, 0xAFEA, 0x5FE9, 0x07E8, 0xCFEA, 0x07E0, 0xFFE0, 0xF800 },   // CYB3RGR33N
    { 0x0800, 0x1000, 0xFD20, 0xFEA0, 0xFCC0, 0xFDE0, 0xFB80, 0xFC40, 0xFFE0, 0xFEA0, 0xFD20, 0xF800 },   // AMB3RTERM
    { 0x1002, 0x2004, 0xF81F, 0xFB9D, 0xF96F, 0xFB99, 0xE01F, 0xFA1F, 0xFFF0, 0xFB56, 0xFD20, 0xF800 },   // BUBBL3GUM
    { 0x0000, 0x2104, 0xFFFF, 0xF79E, 0xC638, 0xEF7D, 0xB5B6, 0xDEFB, 0xFFFF, 0xFFFF, 0xFFFF, 0xF800 },   // GH0ST
    { 0x1000, 0x2000, 0xF800, 0xFB2C, 0xFAEB, 0xF9AB, 0xC0C4, 0xF9CB, 0xFC60, 0xF800, 0xFD20, 0xF800 },   // BL00D
    { 0x200A, 0x000A, 0x4935, 0x6EDF, 0xDB75, 0xFDB5, 0xB5BF, 0x05B5, 0xFECA, 0x6ED5, 0xFDAA, 0xDA4A },   // DU5K
};

static int greenness(uint16_t c) { const int r = c >> 11, g = (c >> 6) & 31, b = c & 31; return g - (r > b ? r : b); }

int main() {
    suite("Every role is a colour an 8-bit frame holds exactly");
    {
        int bad = 0;
        for (int p = 0; p < 6; p++) { const Roles R = deriveRoles(PALS[p]); for (uint8_t i = 0; i < ROLE_COUNT; i++) if (!is332(roleAt(R, i))) bad++; }
        const Roles D = duskRoles(); for (uint8_t i = 0; i < ROLE_COUNT; i++) if (!is332(roleAt(D, i))) bad++;
        ck("all 21 roles, all seven themes", bad == 0);
        int palBad = 0; for (int i = 0; i < 12; i++) if (!is332(PALS[6][i])) palBad++;
        ck("DU5K's own palette row is exact too", palBad == 0);
        ck("snap565 of a level is itself", snap565(73, 36, 170) == 0x4935);
        ck("is332 says no to a 565 in-between", !is332(0x4936));
    }

    suite("A built-in theme keeps its own background");
    {
        bool ok = true;
        for (int p = 0; p < 6; p++) {
            const Roles R = deriveRoles(PALS[p]);
            const uint16_t bg = snap565((PALS[p][0] >> 11) * 255 / 31, ((PALS[p][0] >> 5) & 63) * 255 / 63, (PALS[p][0] & 31) * 255 / 31);
            if (R.deep != bg || R.night != bg || R.indigo != bg) ok = false;
            if (R.light) ok = false;
        }
        ck("desk, window and panel are the theme's background", ok);
        ck("CYB3RGR33N's background is black", deriveRoles(PALS[1]).night == 0x0000);
    }

    suite("A theme's hue carries through");
    {
        const Roles G = deriveRoles(PALS[1]);
        ck("CYB3RGR33N's text is green-tinted", greenness(G.pale) > 0);
        ck("...its sun is green, not orange", greenness(G.sun2) > 0);
        ck("...and its highlight", greenness(G.peach) > 0);
        const Roles A = deriveRoles(PALS[2]);
        ck("AMB3RTERM's sun has no blue", (A.sun1 & 31) == 0);
    }

    suite("DU5K is the opener's dusk, by hand");
    {
        const Roles D = duskRoles();
        ck("its night is indigo, not black", D.night != 0 && (D.night & 31) != 0);
        ck("its light windows stay light", D.light);
        ck("peach is (255,182,170)", D.peach == snap565(255, 182, 170));
    }

    suite("Eleven skins and TRAIL, all named");
    {
        bool named = true;
        for (uint8_t i = 0; i < COUNT; i++) if (!name(i) || !name(i)[0]) named = false;
        ck("twelve names", COUNT == 12 && named);
        ck("an out-of-range id names TRAIL", name(200)[0] == 'T');
        ck("a 320x240 panel takes a window", fits(320, 240));
        ck("a 240x240 watch does not", !fits(240, 240));
        ck("nor a 240x135 stick", !fits(240, 135));
        ck("nor a portrait panel", !fits(240, 320));
    }

    suite("Which skins a board carries");
    {
        ck("TRAIL on every board", available(TRAIL));
        ck("SYSTEM 7 and GLASS, the defaults, on every board", available(SYSTEM7) && available(GLASS));
        ck("nothing past the list", !available(COUNT));
        ck("TRAIL plus seven, or plus eleven", availableCount() == 8 || availableCount() == 12);
        ck("the last one counts them all", availableRank(TERMINAL) == availableCount());
    }
    return report();
}
