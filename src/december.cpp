// The December look. See december.h.
//
// Picked 2026-11-25 from the December Look lab: the "lean" version (snow,
// a drift, three strands of lights) with the CLASSIC hat, and no blizzard.
// Everything is worked out from the clock each frame, so it holds no memory
// while it shows and none at all the rest of the year.
#include "december.h"

#include <TFT_eSPI.h>
#include <math.h>
#include <time.h>

#include "clock.h"
#include "detection.h"
#include "settings.h"
#include "theme.h"

namespace December {

namespace {

uint8_t  s_force   = 0;      // 0 by the date, 1 on, 2 off
bool     s_on      = false;
uint32_t s_checkAt = 0;
bool     s_checked = false;
const uint8_t GUESS_BOOTS_MAX = 7;   // power-ons on a guessed clock before December stops believing it

// The desk clock's hash (theme.cpp), so the flakes fall the same way.
uint32_t hsh32(uint32_t v) {
    v ^= v >> 16; v *= 0x7feb352dU; v ^= v >> 15; v *= 0x846ca68bU; v ^= v >> 16;
    return v;
}

// Three depths: far ones small, slow and dim; near ones big and white.
const uint16_t FLAKE[3] = { 0xC69F, 0xE73F, 0xFFFF };

// The desk clock's snow (theme.cpp, kind 2), over the whole screen, flakes
// one to three pixels. `salt` gives the front layer flakes of its own.
void snow(TFT_eSPI& t, uint32_t now, int w, int h, uint32_t salt) {
    if (w <= 0 || h <= 0) return;
    const int N = 30;
    for (int i = 0; i < N; i++) {
        const uint32_t hs = hsh32((uint32_t)(i + salt) * 40503U + 7U);
        const int layer = i % 3;
        const uint32_t speed = (uint32_t)(6 + layer * 7 + hs % 5);          // px per second
        const int fy = (int)(((uint64_t)now * speed / 1000U + (hs >> 4)) % (uint32_t)(h + 4)) - 2;
        const float sway = sinf((float)now / (900.0f + (float)(hs % 700)) + (float)(hs % 628) / 100.0f);
        int fx = (int)((hs >> 12) % (uint32_t)w) + (int)(sway * (1.5f + (float)layer));
        if (fx < 0) fx += w; else if (fx >= w) fx -= w;
        t.fillRect(fx, fy, layer + 1, layer + 1, FLAKE[layer]);
    }
}

// The flare: the bulb the advert count lands on lights white for a moment
// each time a new advert comes in.
uint32_t s_advLast = 0, s_advAt = 0;

void lights(TFT_eSPI& t, uint32_t now, int w, int top) {
    static const DetectionType KINDS[6] = {
        DetectionType::FLOCK, DetectionType::META, DetectionType::DRONE,
        DetectionType::CAMERA, DetectionType::AIRTAG, DetectionType::RING,
    };
    uint16_t pal[6];
    for (int i = 0; i < 6; i++) pal[i] = Theme::colorFor(KINDS[i]);

    const uint32_t adv = advertsSeen();
    if (adv != s_advLast) { s_advLast = adv; s_advAt = now; }
    const bool flaring = now - s_advAt < 350;
    const int flareK = (int)(adv % 27U);
    const int chase = (int)(now / 200U);

    // Three swags across the width, as in the lab: ends a little off screen.
    const int xs[4] = { -4, w * 110 / 320, w * 214 / 320, w + 4 };
    const int ys[4] = { top + 2, top, top + 4, top };
    const int sag[3] = { 14, 16, 14 };
    const int SEG = 10;
    for (int s = 0; s < 3; s++) {
        int px = xs[s], py = ys[s];
        for (int i = 1; i <= SEG; i++) {
            const float u = (float)i / SEG;
            const int x = xs[s] + (int)((xs[s + 1] - xs[s]) * u);
            const int y = ys[s] + (int)((ys[s + 1] - ys[s]) * u + sag[s] * 4.0f * u * (1.0f - u));
            t.drawLine(px, py, x, y, 0x1A45);
            if (i < SEG) {
                const int k = s * SEG + i - s;               // 0..26 along all three
                const bool flare = flaring && k == flareK;
                const bool on = ((k + chase) % 3) != 0;
                if (flare) t.drawCircle(x, y + 3, 4, 0xFFFF);
                t.fillRect(x - 1, y, 3, 1, 0x2945);
                t.fillRect(x - 1, y + 1, 3, 4, flare ? 0xFFFF : on ? pal[k % 6] : 0x3986);
            }
            px = x; py = y;
        }
    }
}

// A drift along the floor, its height per column from two sine waves.
void drift(TFT_eSPI& t, int w, int floorY) {
    for (int x = 0; x < w; x += 2) {
        const int hgt = (int)lroundf(4.0f + 2.0f * sinf(x * 0.05f) + 1.5f * sinf(x * 0.13f + 1.0f));
        t.fillRect(x, floorY - hgt, 2, hgt, 0xF7BF);
        t.fillRect(x, floorY - hgt, 2, 1, 0xFFFF);
    }
}

}  // namespace

void setForce(uint8_t mode) { s_force = mode > 2 ? 0 : mode; s_checked = false; }
uint8_t force() { return s_force; }

bool active(uint32_t nowMs) {
    if (s_force == 1) return true;
    if (s_force == 2) return false;
    if (!Settings::seasonal()) return false;
    if (s_checked && nowMs - s_checkAt < 1000) return s_on;
    s_checked = true; s_checkAt = nowMs;
    s_on = false;
    if (!Clock::isSet()) return false;
    // A guessed clock (a cold boot with nothing to set it) stops where the
    // power went off, and nobody knows for how long. Trust it for a week of
    // power-ons; after that the date is as good as unknown, so no December:
    // late snow is fine, snow in January is not.
    if (Clock::guessed() && Clock::guessBoots() > GUESS_BOOTS_MAX) return false;
    struct tm tmv;
    const time_t e = (time_t)Clock::nowEpoch();
    localtime_r(&e, &tmv);
    s_on = tmv.tm_mon == 11 && tmv.tm_mday <= 26;
    return s_on;
}

void back(TFT_eSPI& t, uint32_t now, int w, int top, int floorY) {
    snow(t, now, w, floorY, 0);
    lights(t, now, w, top);
    drift(t, w, floorY);
}

void front(TFT_eSPI& t, uint32_t now, int w, int top, int floorY) {
    (void)top;
    snow(t, now, w, floorY, 1000);
}

// CLASSIC from the hat lab: a cone whose tip flops to his left with a
// swinging bobble, a white rolled brim. Inked like the outfits: every part
// once in black a pixel out each way, then in its colour.
void hat(TFT_eSPI& t, int cx, int hy, uint32_t now, float scale) {
    auto S = [scale](int v) { return (int)(v * scale); };
    const uint16_t ink = 0x0000, red = 0xD8A3, redD = 0x9000, wh = 0xFFFF, grey = 0xB5B6;
    const float sw = sinf((float)now / 380.0f);
    const int dx = (int)(sw * (float)S(2)), dy = (int)(fabsf(sw) * (float)S(1));
    const int tx = cx + S(19) + dx, ty = hy - S(5) + dy;
    static const int8_t O[5][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {0, 0} };
    for (int i = 0; i < 5; i++) {
        const int ox = O[i][0], oy = O[i][1];
        const uint16_t c = i < 4 ? ink : red;
        t.fillTriangle(cx - S(14) + ox, hy + oy, cx + S(13) + ox, hy + oy, cx + S(3) + ox, hy - S(15) + oy, c);
        t.fillTriangle(cx - S(1) + ox, hy - S(12) + oy, cx + S(7) + ox, hy - S(14) + oy, tx + ox, ty + oy, c);
    }
    t.fillTriangle(cx - S(14), hy, cx - S(6), hy, cx + S(3), hy - S(15), redD);
    const int br = S(3) < 1 ? 1 : S(3);
    for (int i = 0; i < 5; i++) {
        const int ox = O[i][0], oy = O[i][1];
        const uint16_t c = i < 4 ? ink : wh;
        t.fillRoundRect(cx - S(17) + ox, hy - S(2) + oy, S(34), S(7), br, c);
        t.fillCircle(tx + ox, ty + oy, br, c);
    }
    t.fillRect(cx - S(16), hy + S(3), S(32), S(1) > 0 ? S(1) : 1, grey);
}

// ---- the HOLIDAY MODE card -------------------------------------------------
// Laid out like the time zone card (ui_zone.cpp): a size up on the large panels.
namespace {
int cardW() { return Theme::large() ? 320 : 232; }
int cardH() { return Theme::large() ? 136 : 96; }
int btnH()  { return Theme::large() ? 30 : 20; }
void cardRect(int sw, int sh, int& x, int& y) { x = (sw - cardW()) / 2; y = (sh - cardH()) / 2 - 8; }
void btnRects(int x, int y, int& offX, int& keepX, int& bw, int& by) {
    by = y + cardH() - btnH() - 6;
    bw = (cardW() - 18) / 2;
    offX = x + 6;
    keepX = offX + bw + 6;
}
// The year the card is about; 1 while the clock is unset, so a forced
// December (XMAS 1) on a board with no time still asks once.
uint16_t cardYear() {
    if (!Clock::isSet()) return 1;
    struct tm tmv;
    const time_t e = (time_t)Clock::nowEpoch();
    localtime_r(&e, &tmv);
    return (uint16_t)(tmv.tm_year + 1900);
}
}  // namespace

bool cardWanted(uint32_t now) {
    return !Settings::boringMode() && Settings::seasonal() && active(now) &&
           Settings::seasonAskedYear() != cardYear();
}

void cardDraw(TFT_eSPI& t, uint32_t now) {
    int x, y;
    cardRect(t.width(), t.height(), x, y);
    const uint16_t red = 0xD8A3, green = 0x2E8A;
    t.fillRoundRect(x, y, cardW(), cardH(), 6, Theme::BG);
    t.drawRoundRect(x, y, cardW(), cardH(), 6, red);
    t.drawRoundRect(x + 1, y + 1, cardW() - 2, cardH() - 2, 5, green);
    // A strand of bulbs along the top edge, chasing like the ones outside.
    static const uint16_t BULB[4] = { 0xD8A3, 0x2E8A, 0xFEA0, 0x4D1F };
    const int n = cardW() / 16;
    for (int i = 0; i < n; i++) {
        const int bx = x + 8 + i * 16;
        const bool on = ((i + (int)(now / 250U)) % 3) != 0;
        t.fillRect(bx, y - 2, 3, 1, 0x2945);
        t.fillRect(bx, y - 1, 3, 4, on ? BULB[i % 4] : 0x3986);
    }
    t.setTextWrap(false);
    t.setTextSize(Theme::upSize(2));
    const char* head = "HOLIDAY MODE!";
    t.setTextColor(Theme::WHITE, Theme::BG);
    int ly = y + 9;
    t.setCursor(x + (cardW() - t.textWidth(head)) / 2, ly);
    t.print(head);
    ly += t.fontHeight() + 5;
    Theme::bodyFontOn(t);
    const char* l1 = "Snow, lights and a Santa hat";
    const char* l2 = "until December 26.";
    t.setTextColor(Theme::VAPOR_YELLOW, Theme::BG);
    t.setCursor(x + (cardW() - t.textWidth(l1)) / 2, ly);
    t.print(l1);
    ly += t.fontHeight() + 1;
    t.setCursor(x + (cardW() - t.textWidth(l2)) / 2, ly);
    t.print(l2);
    Theme::bodyFontOff(t);
    int offX, keepX, bw, by;
    btnRects(x, y, offX, keepX, bw, by);
    Theme::drawButton(t, offX,  by, bw, btnH(), "TURN OFF", false);
    Theme::drawButton(t, keepX, by, bw, btnH(), "KEEP IT", false);
}

CardHit cardHit(int tx, int ty, int sw, int sh) {
    int x, y;
    cardRect(sw, sh, x, y);
    if (tx < x || tx >= x + cardW() || ty < y - 3 || ty >= y + cardH()) return CardHit::NONE;
    int offX, keepX, bw, by;
    btnRects(x, y, offX, keepX, bw, by);
    if (ty < by - 4) return CardHit::CARD;
    return tx < keepX - 3 ? CardHit::OFF : CardHit::KEEP;
}

void cardStops(int sw, int sh, int16_t xs[2], int16_t ys[2]) {
    int x, y;
    cardRect(sw, sh, x, y);
    int offX, keepX, bw, by;
    btnRects(x, y, offX, keepX, bw, by);
    xs[0] = (int16_t)(offX + bw / 2);  ys[0] = (int16_t)(by + btnH() / 2);
    xs[1] = (int16_t)(keepX + bw / 2); ys[1] = (int16_t)(by + btnH() / 2);
}

void cardAnswer(bool keep) {
    Settings::setSeasonAskedYear(cardYear());
    if (keep) return;
    Settings::setSeasonal(false);
    if (s_force == 1) s_force = 0;      // a forced December goes too
    Theme::showToast("HOLIDAY MODE OFF", "SETTINGS > APPEARANCE", 0xD8A3, 2500);
}

}  // namespace December
