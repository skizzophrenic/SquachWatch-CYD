// SquachWatch-CYD — the MORE INFO card. See include/info_card.h.
#include "info_card.h"
#include "theme.h"
#include "alert_skin.h"
#include "settings.h"
#include <Arduino.h>
#include <string.h>

namespace InfoCard {

static Card     s_card;
static uint32_t s_fedAt = 0;
static uint8_t  s_page = 0, s_pages = 1;
static const char* s_lastName = nullptr;

// Where the last draw put the buttons, for the tap.
struct Rect { int16_t x, y, w, h; bool in(int px, int py) const { return w > 0 && px >= x && px < x + w && py >= y && py < y + h; } };
static Rect s_back, s_more, s_got;

void feed(const Card& c, uint32_t now) {
    // A different card, or one opened again after a pause: page one.
    if (c.name != s_lastName || now - s_fedAt > 500) s_page = 0;
    s_lastName = c.name;
    s_card = c;
    s_fedAt = now ? now : 1;
}
bool fresh(uint32_t now) { return s_fedAt && now - s_fedAt < 500; }

// ---- sizes per panel (the studio's picks, mapped onto the board's fonts) ----
// The studio set pixel sizes on a stand-in font; the board has three Bangers
// sizes (SM 22 pt, MD 30, LG 34), the 6x8 GLCD font and the 16 px font 2, so
// each pick lands on the nearest of those.
struct Size {
    Theme::BangersSize name;   // the banner's name
    uint8_t bodyFont, bodyLh;  // description and TIP: GLCD 6x8 (10 px rows) or font 2 (16)
    Theme::BangersSize tile;   // the tiles' numbers
    int8_t  tileH, btnH;
};
static Size sizeFor(int w, int h) {
    if (w >= 400 || h >= 400) return { Theme::BangersSize::LG, 2, 17, Theme::BangersSize::SM, 30, 32 };   // 3.5in (SM carries the colon)
    if (h > w)                return { Theme::BangersSize::MD, 2, 16, Theme::BangersSize::SM, 24, 26 };   // 2.8in upright
    return                           { Theme::BangersSize::SM, 1, 10, Theme::BangersSize::SM, 24, 24 };   // T-Deck, 2.8in, watch
}
static int ascentOf(Theme::BangersSize s) { return s == Theme::BangersSize::LG ? 31 : s == Theme::BangersSize::MD ? 27 : 20; }

// The pill's three states, in colours that read on every banner.
static const uint16_t PILL_C[3] = { 0xFCCD, 0xFE89, 0x37F3 };   // #ff9a6a, #ffd04a, #33ff99
static const char*    PILL_W[3] = { "MAYBE", "LIKELY", "SURE" };

static void font(TFT_eSPI& t, uint8_t f) { t.setTextFont(f); t.setTextSize(1); }

// The pill: black, rounded, the word and three dots. Returns its width.
static int pillWidth(TFT_eSPI& t, uint8_t conf) {
    font(t, 1);
    return 6 + t.textWidth(PILL_W[conf]) + 4 + 3 * 7 + 4;
}
static void drawPill(TFT_eSPI& t, int x, int y, uint8_t conf) {
    const int pw = pillWidth(t, conf), ph = 14;
    t.fillRoundRect(x, y, pw, ph, 7, TFT_BLACK);
    font(t, 1);
    t.setTextColor(PILL_C[conf]);
    t.setCursor(x + 6, y + 3);
    t.print(PILL_W[conf]);
    int dx = x + 6 + t.textWidth(PILL_W[conf]) + 6;
    for (int i = 0; i < 3; i++, dx += 7) {
        if (i <= conf) t.fillCircle(dx, y + 7, 2, PILL_C[conf]);
        else           t.drawCircle(dx, y + 7, 2, PILL_C[conf]);
    }
}

// The name, as the font has it: capitals, no underscores; LG/MD step down to
// a size that fits, and SM drops letters off the end if even that is too wide.
static Theme::BangersSize fitName(char* name, Theme::BangersSize want, int room) {
    Theme::BangersSize sz = want;
    for (;;) {
        if (Theme::bangersTextWidth(name, sz) <= room) return sz;
        if (sz == Theme::BangersSize::LG) sz = Theme::BangersSize::MD;
        else if (sz == Theme::BangersSize::MD) sz = Theme::BangersSize::SM;
        else break;
    }
    size_t n = strlen(name);
    while (n > 1 && Theme::bangersTextWidth(name, sz) > room) name[--n] = 0;
    return sz;
}

static char s_rows[32][80];

// Word wrap for the description. With per > 0 the last line of every page
// is kept to shortW, so the MORE n/m label sits beside it, not on top of it.
static uint8_t wrapPaged(TFT_eSPI& t, const char* text, int w, int per, int shortW) {
    char buf[640];
    strncpy(buf, text, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    uint8_t n = 0;
    char line[80] = "";
    for (char* word = strtok(buf, " "); word; word = strtok(nullptr, " ")) {
        char trial[80];
        snprintf(trial, sizeof trial, line[0] ? "%s %s" : "%s%s", line, word);
        const int maxW = (per > 0 && n % per == per - 1) ? shortW : w;
        if (line[0] && (t.textWidth(trial) > maxW || strlen(trial) >= sizeof line - 1)) {
            if (n >= 31) break;
            strcpy(s_rows[n++], line);
            snprintf(line, sizeof line, "%s", word);
        } else {
            strcpy(line, trial);
        }
    }
    if (line[0] && n < 32) strcpy(s_rows[n++], line);
    return n;
}

// The card's middle, everything but the frame and the buttons, in the box
// given: on its own (drawTrail) or in a window skin's content box. Returns
// the buttons it needs: 3 with MORE when the description pages, else 2.
static uint8_t drawBody(TFT_eSPI& t, int bx, int by, int bw, int bh, const Size& S) {
    const Card& c = s_card;
    const uint16_t A = c.kindA, B = c.kindB;
    (void)B;
    t.setTextWrap(false);
    const int cx = bx, cy = by, cw = bw, ch = bh;
    t.fillRect(cx, cy, cw, ch, Theme::BG);
    const int ix = cx + 5, iw = cw - 10;

    // The banner: a left-to-right blend of the kind's two colours.
    const int bannerH = ascentOf(S.name) + 6;
    for (int x = cx; x < cx + cw; x++) {
        const uint16_t col = Theme::blend(A, B, (uint16_t)((x - cx) * 255 / (cw > 1 ? cw - 1 : 1)));
        t.drawFastVLine(x, cy, bannerH, col);
    }
    const uint8_t conf = c.conf > 2 ? 2 : c.conf;
    const int pw = pillWidth(t, conf);
    drawPill(t, cx + cw - pw - 5, cy + (bannerH - 14) / 2, conf);
    char name[40];
    snprintf(name, sizeof name, "%s", c.name ? c.name : "");
    for (char* p = name; *p; p++) { if (*p == '_') *p = ' '; else if (*p >= 'a' && *p <= 'z') *p -= 32; }
    const Theme::BangersSize nsz = fitName(name, S.name, iw - pw - 8);
    Theme::drawBangersText(t, ix, cy + (bannerH - ascentOf(nsz)) / 2 + 1, name, TFT_BLACK, nsz);
    int y = cy + bannerH + 4;

    // The headline (font 2, up to two lines), then WHY.
    font(t, 2);
    uint8_t n = Theme::wrapText(t, c.head ? c.head : "", iw, s_rows, 2);
    t.setTextColor(Theme::CYAN);
    for (uint8_t i = 0; i < n; i++) { t.setCursor(ix, y); t.print(s_rows[i]); y += 16; }
    y += 1;
    font(t, 1);
    t.setTextColor(A);
    char why[48];
    snprintf(why, sizeof why, "WHY: %s", c.why ? c.why : "");
    n = Theme::wrapText(t, why, iw, s_rows, 1);
    if (n) { t.setCursor(ix, y); t.print(s_rows[0]); }
    y += 12;

    // From the bottom up: the tiles, then the TIP box sitting on them.
    const int tileY = c.history ? cy + ch - 2 - S.tileH : cy + ch - 3;
    font(t, S.bodyFont);
    char tip[160];
    snprintf(tip, sizeof tip, "TIP  %s", c.tip ? c.tip : "");
    const uint8_t tn = Theme::wrapText(t, tip, iw - 12, s_rows, 4);
    const int tipH = tn * S.bodyLh + 6;
    const int tipY = tileY - 4 - tipH;
    t.fillRect(ix, tipY, iw, tipH, Theme::blend(Theme::BG, A, 40));
    t.fillRect(ix, tipY, 3, tipH, A);
    for (uint8_t i = 0; i < tn; i++) {
        t.setTextColor(TFT_WHITE);
        t.setCursor(ix + 9, tipY + 3 + i * S.bodyLh + (S.bodyFont == 1 ? 1 : 0));
        t.print(s_rows[i]);
        if (i == 0) {   // the label over its own letters, in the kind's colour
            t.setTextColor(A);
            t.setCursor(ix + 9, tipY + 3 + (S.bodyFont == 1 ? 1 : 0));
            t.print("TIP");
        }
    }

    // The description, a page at a time in the room that is left.
    font(t, S.bodyFont);
    int per = (tipY - 3 - y) / S.bodyLh;
    if (per < 1) per = 1;
    uint8_t dn = wrapPaged(t, c.body ? c.body : "", iw, 0, iw);
    if (dn > per) {
        // It pages: wrap again with room for the label on each last line.
        font(t, 1);
        const int lw = t.textWidth(" MORE 9/9 >") + 4;
        font(t, S.bodyFont);
        dn = wrapPaged(t, c.body ? c.body : "", iw, per, iw - lw);
    }
    s_pages = (uint8_t)((dn + per - 1) / per);
    if (!s_pages) s_pages = 1;
    if (s_page >= s_pages) s_page = 0;
    t.setTextColor(TFT_WHITE);
    const int first = s_page * per;
    for (int i = first; i < dn && i < first + per; i++) {
        t.setCursor(ix, y + (i - first) * S.bodyLh);
        t.print(s_rows[i]);
    }
    if (s_pages > 1) {
        font(t, 1);
        char more[16];
        snprintf(more, sizeof more, s_page + 1 < s_pages ? " MORE %u/%u >" : " %u/%u", s_page + 1, s_pages);
        const int mw = t.textWidth(more);
        const int my = y + (per - 1) * S.bodyLh + (S.bodyFont == 1 ? 1 : 4);
        t.fillRect(ix + iw - mw - 2, my - 1, mw + 2, 10, Theme::BG);
        t.setTextColor(A);
        t.setCursor(ix + iw - mw, my);
        t.print(more);
    }

    // The tiles, docked on the frame's bottom edge.
    if (c.history) {
        t.fillRect(cx + 2, tileY, cw - 4, S.tileH, Theme::blend(Theme::BG, A, 25));
        t.drawFastHLine(cx + 2, tileY, cw - 4, A);
        const int cellW = (cw - 4) / 4;
        const char* vals[3] = { c.seen, c.places, c.first };
        const char* labs[3] = { "SEEN", "PLACES", "FIRST" };
        for (int k = 0; k < 4; k++) {
            const int tx = cx + 2 + k * cellW;
            if (k) t.drawFastVLine(tx, tileY + 4, S.tileH - 8, Theme::blend(Theme::BG, A, 110));
            if (k < 3) {
                const int ty = tileY + (S.tileH - ascentOf(S.tile)) / 2;
                Theme::drawBangersText(t, tx + 5, ty, vals[k], TFT_WHITE, S.tile);
                font(t, 1);
                t.setTextColor(Theme::blend(Theme::BG, TFT_WHITE, 150));
                const int lx = tx + 5 + Theme::bangersTextWidth(vals[k], S.tile) + 4;
                if (lx + t.textWidth(labs[k]) < tx + cellW - 2) { t.setCursor(lx, tileY + S.tileH / 2 - 2); t.print(labs[k]); }
            } else {
                font(t, 1);
                // The long words where the tile has room, the short ones on the watch.
                const char* st = c.following ? "FOLLOWING" : "STAYS PUT";
                if (5 + t.textWidth(st) > cellW - 2) st = c.following ? "FOLLOWS" : "STAYS";
                t.setTextColor(c.following ? 0xFA6D : Theme::blend(Theme::BG, TFT_WHITE, 150));   // #ff4d6a
                t.setCursor(tx + 5, tileY + S.tileH / 2 - 4);
                t.print(st);
            }
        }
    }

    return s_pages > 1 ? 3 : 2;
}

// The card on its own: a rounded frame in the kind's colour, its own buttons.
static void drawTrail(TFT_eSPI& t, int w, int h, const Size& S) {
    const Card& c = s_card;
    const uint16_t A = c.kindA, B = c.kindB;
    const int cx = 4, cy = 4, cw = w - 8, btnY = h - S.btnH - 3, ch = btnY - 4 - cy;
    t.fillRect(0, 0, w, h, Theme::BG);
    t.drawRoundRect(cx, cy, cw, ch, 8, A);
    t.drawRoundRect(cx + 1, cy + 1, cw - 2, ch - 2, 7, A);
    drawBody(t, cx + 2, cy + 2, cw - 4, ch - 4, S);
    // The buttons: BACK in the kind's colour, MORE only when the text pages, GOT IT.
    const char* labels[3] = { "BACK", "MORE", "GOT IT" };
    const bool more = s_pages > 1;
    const int nb = more ? 3 : 2, gap = 4, bw = (w - 8 - (nb - 1) * gap) / nb;
    s_back = s_more = s_got = { 0, 0, 0, 0 };
    int bx = 4;
    for (int i = 0; i < 3; i++) {
        if (i == 1 && !more) continue;
        const bool hot = i == 0;
        t.fillRoundRect(bx, btnY, bw, S.btnH, 4, hot ? B : Theme::BG);
        t.drawRoundRect(bx, btnY, bw, S.btnH, 4, B);
        font(t, 2);
        t.setTextColor(hot ? TFT_BLACK : TFT_WHITE);
        t.setCursor(bx + (bw - t.textWidth(labels[i])) / 2, btnY + (S.btnH - 16) / 2 + 1);
        t.print(labels[i]);
        const Rect r = { (int16_t)bx, (int16_t)btnY, (int16_t)bw, (int16_t)S.btnH };
        if (i == 0) s_back = r; else if (i == 1) s_more = r; else s_got = r;
        bx += bw + gap;
    }
    font(t, 1);
}

// The size the skin's body draws at (the panel's), for the thunk below.
static Size s_skinSize;
static uint8_t bodyThunk(TFT_eSPI& t, int x, int y, int w, int h) { return drawBody(t, x, y, w, h, s_skinSize); }

void draw(TFT_eSPI& t, int w, int h, uint32_t now) {
    const Size S = sizeFor(w, h);
    const uint8_t skin = Settings::alertSkin();
    if (skin != AlertSkin::TRAIL && AlertSkin::available(skin) && AlertSkin::fits(w, h)) {
        s_skinSize = S;
        AlertSkin::drawInfo(t, skin, s_card.name, s_card.type, bodyThunk, now,
                            Settings::alertSunset(), Settings::paletteIndex());
        // The skin's buttons, for the tap and the cursor.
        s_back = s_more = s_got = { 0, 0, 0, 0 };
        int x, y, bw, bh;
        const bool three = s_pages > 1;
        if (AlertSkin::buttonRect(0, x, y, bw, bh)) s_back = { (int16_t)x, (int16_t)y, (int16_t)bw, (int16_t)bh };
        if (AlertSkin::buttonRect(1, x, y, bw, bh)) (three ? s_more : s_got) = { (int16_t)x, (int16_t)y, (int16_t)bw, (int16_t)bh };
        if (three && AlertSkin::buttonRect(2, x, y, bw, bh)) s_got = { (int16_t)x, (int16_t)y, (int16_t)bw, (int16_t)bh };
        return;
    }
    drawTrail(t, w, h, S);
}



bool buttonCenter(uint8_t i, int& x, int& y) {
    const Rect& r = i == 0 ? s_back : i == 1 ? s_more : s_got;
    if (r.w <= 0) return false;
    x = r.x + r.w / 2; y = r.y + r.h / 2;
    return true;
}

bool tapCloses(int x, int y, int w, int h) {
    (void)w; (void)h;
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (now - last < 250) return false;     // one press, one page
    if (s_more.in(x, y)) { last = now; s_page = (uint8_t)((s_page + 1) % (s_pages ? s_pages : 1)); return false; }
    if (s_back.in(x, y) || s_got.in(x, y)) { last = now; s_page = 0; return true; }
    return false;
}

}  // namespace InfoCard
