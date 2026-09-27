// SquachWatch-CYD — desk mode. See ui_desk.h.
#include "ui_desk.h"
#include "ui_fit.h"
#include "clock.h"
#include "theme.h"
#include "squachy.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>
#if SQUACH_MESH
#include "meshtalk.h"
#include "squachmesh.h"
#include "settings.h"
#include "ui_clear.h"   // the crowd is drawn by the main screen's renderer
#endif

namespace {

enum class Timer : uint8_t { IDLE, FOCUS, BREAK };
// Where the clock's plate was last drawn, for the taps that change what
// plays inside it.
int16_t s_plateX = 0, s_plateY = 0, s_plateW = 0, s_plateH = 0;
Timer    s_timer   = Timer::IDLE;
uint32_t s_timerEnd = 0;      // millis() when the running block ends
uint32_t s_chimeAt  = 0;      // when the last block ended, for the light
// Squachy's line while a block runs. He is drawn still then, without his
// idle chatter, so the timer's own lines carry their own bubble.
const char* s_line      = nullptr;
uint32_t    s_lineUntil = 0;
void sayFocus(const char* l, uint32_t now) { s_line = l; s_lineUntil = now + 6000; }

// The small alert card. Where it sits depends on the room beside the
// clock's plate, which the tick works out and leaves here for the hit test.
Detection s_alert       = {};
uint32_t  s_alertAt     = 0;
bool      s_alertOn     = false;   // rather than testing s_alertAt, which can be 0
int       s_cardX = 0, s_cardY = 0, s_cardW = 98;
constexpr int      CARD_W = 98, CARD_H = 40;
constexpr uint32_t CARD_MS = 9000;
constexpr uint32_t FOCUS_MS = 25u * 60u * 1000u;
constexpr uint32_t BREAK_MS =  5u * 60u * 1000u;
constexpr uint32_t CHIME_MS = 6000;

// Seven-segment digits, drawn from rectangles so they cost no font data.
// Segment bits: a top, b top-right, c bottom-right, d bottom, e bottom-left,
// f top-left, g middle.
const uint8_t SEG[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

// The seven segments of a cell, as rectangles, handed one at a time to `fn`
// with the bit that lights it. A pixel of air between segments so the
// corners read as a digit and not a block.
template <typename Fn>
void forEachSegment(int x, int y, int w, int h, int th, Fn fn) {
    const int mid = y + h / 2 - th / 2;
    fn(0x01, x + th + 1, y,            w - 2 * th - 2, th);                // a
    fn(0x02, x + w - th, y + th + 1,   th, h / 2 - th - 2);                // b
    fn(0x04, x + w - th, mid + th + 1, th, h / 2 - th - 2);                // c
    fn(0x08, x + th + 1, y + h - th,   w - 2 * th - 2, th);                // d
    fn(0x10, x,          mid + th + 1, th, h / 2 - th - 2);                // e
    fn(0x20, x,          y + th + 1,   th, h / 2 - th - 2);                // f
    fn(0x40, x + th + 1, mid,          w - 2 * th - 2, th);                // g
}

// The lit segments only. The unlit ones used to be drawn too, in a dim
// purple, and on the plain plate that "8" behind every digit read as black
// text; over a backdrop it hid what was playing.
void drawDigit(TFT_eSPI& t, int x, int y, int w, int h, int th, int d, uint16_t on) {
    const uint8_t s = (d >= 0 && d <= 9) ? SEG[d] : 0;
    forEachSegment(x, y, w, h, th, [&](uint8_t bit, int sx, int sy, int sw, int sh) {
        if (s & bit) t.fillRect(sx, sy, sw, sh, on);
    });
}

// The LIT segments of digit `d`, each grown by `o` on every side and filled
// in `col`: drawn first, it is the outline that keeps a digit legible over
// whatever plays behind the clock. Unlit segments get none, so nothing dark
// stands in the backdrop where no ink is.
void drawDigitOutline(TFT_eSPI& t, int x, int y, int w, int h, int th, int d, int o, uint16_t col) {
    const uint8_t s = (d >= 0 && d <= 9) ? SEG[d] : 0;
    forEachSegment(x, y, w, h, th, [&](uint8_t bit, int sx, int sy, int sw, int sh) {
        if (s & bit) t.fillRect(sx - o, sy - o, sw + 2 * o, sh + 2 * o, col);
    });
}

// Text with no box behind it: a one-pixel dark keyline, then the colour on
// top. For the date and AM/PM over a clock backdrop.
void printOutlined(TFT_eSPI& t, int x, int y, const char* s, uint16_t col) {
    t.setTextColor(Theme::BG);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            t.setCursor(x + dx, y + dy);
            t.print(s);
        }
    t.setTextColor(col);
    t.setCursor(x, y);
    t.print(s);
}

void timerRects(int screenW, int screenH, int& tx, int& ty, int& tw, int& th, int& bx, int& bw) {
    const Theme::ButtonBarGeom g = Theme::computeButtonBar(screenW, screenH);
    ty = g.y; th = g.h;
    // Two buttons out of the three slots: the timer takes the first two,
    // BACK the third, so the thing you tap most is the bigger target.
    tx = g.x[0]; tw = g.x[1] + g.w[1] - g.x[0];
    bx = g.x[2]; bw = g.w[2];
    // ...less a square on the left for the settings gear, with the same gap
    // the bar leaves between its own buttons.
    const int gap = g.x[1] - (g.x[0] + g.w[0]);
    tx += th + gap;
    tw -= th + gap;
}

// The gear's square: the bar's height, at the bar's left edge.
void gearRect(int screenW, int screenH, int& x, int& y, int& s) {
    const Theme::ButtonBarGeom g = Theme::computeButtonBar(screenW, screenH);
    x = g.x[0]; y = g.y; s = g.h;
}

// A cog: eight teeth round a ring, with a hole through the middle.
void drawGear(TFT_eSPI& t, int cx, int cy, int r, uint16_t col, uint16_t bg) {
    for (int k = 0; k < 8; k++) {
        const float a = (float)k * 0.785398f;
        const int tx = cx + (int)lroundf(cosf(a) * (float)r);
        const int ty = cy + (int)lroundf(sinf(a) * (float)r);
        t.fillRect(tx - 1, ty - 1, 3, 3, col);
    }
    t.fillCircle(cx, cy, r - 1, col);
    t.fillCircle(cx, cy, r / 3 + 1, bg);
}

void timerLabel(char* out, size_t n, uint32_t now) {
    if (s_timer == Timer::IDLE) { snprintf(out, n, "FOCUS 25"); return; }
    const uint32_t left = s_timerEnd > now ? s_timerEnd - now : 0;
    const uint32_t sec  = (left + 999) / 1000;
    snprintf(out, n, "%s %lu:%02lu", s_timer == Timer::FOCUS ? "FOCUS" : "BREAK",
             (unsigned long)(sec / 60), (unsigned long)(sec % 60));
}

}  // namespace

void uiDeskInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

bool uiDeskChime(uint32_t now) { return s_chimeAt && now - s_chimeAt < CHIME_MS; }

void uiDeskAlert(const Detection& d, uint32_t now) { s_alert = d; s_alertAt = now; s_alertOn = true; }
bool uiDeskAlertUp(uint32_t now) { return s_alertOn && now - s_alertAt < CARD_MS; }
const Detection* uiDeskAlertDetection() { return &s_alert; }
bool uiDeskHitAlert(int x, int y, uint32_t now) {
    return uiDeskAlertUp(now) && x >= s_cardX - 4 && x <= s_cardX + s_cardW + 4 &&
           y >= s_cardY - 4 && y <= s_cardY + CARD_H + 4;
}


// ---- the message card ------------------------------------------------------
#if SQUACH_MESH
constexpr uint32_t MSG_HOLD_MS = 20000;   // shown at least this long, and while unread
int s_msgX0 = 0, s_msgY0 = 0, s_msgX1 = 0, s_msgY1 = 0;   // the card's hit box
uint32_t s_msgSeenAt = 0;   // m.at of the message a tap dismissed, so it stays down
bool     s_msgSeen   = false;

static bool messageUp(uint32_t now) {
    const MeshTalk::Message& m = MeshTalk::inbox();
    return MeshTalk::ready() && m.have && !(s_msgSeen && m.at == s_msgSeenAt) &&
           (m.unread || (uint32_t)(now - m.at) < MSG_HOLD_MS);
}

// A rectangle turned a few degrees, from two triangles: TFT_eSPI has no
// rotated fill, and the tilt is the whole point of a polaroid.
static void fillTilted(TFT_eSPI& t, int cx, int cy, int w, int h, float deg, uint16_t c) {
    const float r = deg * 3.14159265f / 180.0f;
    const float cs = cosf(r), sn = sinf(r);
    const float hx = w / 2.0f, hy = h / 2.0f;
    int px[4], py[4];
    const float kx[4] = { -hx,  hx,  hx, -hx };
    const float ky[4] = { -hy, -hy,  hy,  hy };
    for (int i = 0; i < 4; i++) {
        px[i] = (int)lroundf(cx + kx[i] * cs - ky[i] * sn);
        py[i] = (int)lroundf(cy + kx[i] * sn + ky[i] * cs);
    }
    t.fillTriangle(px[0], py[0], px[1], py[1], px[2], py[2], c);
    t.fillTriangle(px[0], py[0], px[2], py[2], px[3], py[3], c);
}

// How many rows the message strip along the bottom wants, or 0 when there
// is no message up. The tick takes this off Squachy's band so his feet
// stay above the strip.
// The XP box: a blue title bar reading MESSAGE with a close button that is
// only decoration, a beige body with the message, the time in its corner.
constexpr int XP_LEFT   = 8 + 56 + 10;   // clear of the polaroid
constexpr int XP_TITLE  = 15;
constexpr int XP_W      = 160;   // the same box on either rotation
constexpr uint16_t XP_BLUE  = 0x0AFC;    // #0A5FE6
constexpr uint16_t XP_LIGHT = 0x3CBF;    // #3D95FF
constexpr uint16_t XP_BODY  = 0xEF5B;    // #ECE9D8
constexpr uint16_t XP_CLOSE = 0xD2A6;    // #D65434

// Three lines: a full 48-character message needs them on a portrait screen,
// and wrapText runs the last row long rather than dropping words.
static uint8_t messageLines(TFT_eSPI& t, char rows[][48]) {
    Theme::bubbleFontOn(t);
    const uint8_t n = Theme::wrapText(t, MeshTalk::lineText(MeshTalk::inbox()), XP_W - 8 - 14, rows, 3);
    Theme::bubbleFontOff(t);
    return n;
}

// Rows the box takes, from its top edge to its bottom: title bar, the
// message, the time's row.
static int messageBoxH(TFT_eSPI& t) {
    char rows[3][48];
    const uint8_t n = messageLines(t, rows);
    return XP_TITLE + 6 + n * (Theme::bubbleTextH() + 1) + 10 + 2;
}

// The box drops out from behind the clock's plate when a message lands and
// climbs back behind it when the message is read or its time is up; the
// polaroid slides in from the left edge alongside. p is how far out it is,
// 0 hidden to 1 at rest; the out journey runs a little quicker.
constexpr uint32_t MSG_IN_MS = 340, MSG_OUT_MS = 260;
uint32_t s_msgInAt = 0, s_msgOutAt = 0;
bool     s_msgWasUp = false;

static float messageProgress(uint32_t now) {
    const bool up = messageUp(now);
    if (up) {
        if (!s_msgWasUp) { s_msgWasUp = true; s_msgInAt = now; }
        float q = (float)(now - s_msgInAt) / (float)MSG_IN_MS;
        if (q > 1.0f) q = 1.0f;
        return 1.0f - (1.0f - q) * (1.0f - q);          // ease out: fast, then settles
    }
    if (s_msgWasUp) { s_msgWasUp = false; s_msgOutAt = now; }
    if (now - s_msgOutAt < MSG_OUT_MS) {
        const float q = (float)(now - s_msgOutAt) / (float)MSG_OUT_MS;
        return (1.0f - q) * (1.0f - q);                  // ease in: picks up speed as it goes
    }
    return 0.0f;
}

// The XP box, right of the polaroid, hung under the clock's plate. Drawn
// BEFORE the plate so that on its way in and out the part still behind
// the plate is covered by it.
static void drawMessageBox(TFT_eSPI& t, int restTop, float p, uint32_t now) {
    (void)now;
    const MeshTalk::Message& m = MeshTalk::inbox();
    const int bh = messageBoxH(t);
    const int bx = XP_LEFT, bw = XP_W;
    const int by = restTop - (int)((1.0f - p) * (float)(bh + 8));
    Theme::bubbleFontOff(t);
    t.fillRoundRect(bx, by, bw, bh, 4, XP_BLUE);
    // The title bar: two blues for the XP sheen, then MESSAGE and the button.
    t.fillRect(bx + 1, by + 2, bw - 2, 3, XP_LIGHT);
    t.setTextSize(1);
    t.setTextColor(Theme::WHITE, XP_BLUE);
    // A small envelope where XP puts the window icon.
    t.fillRect(bx + 5, by + 5, 9, 6, Theme::WHITE);
    t.drawLine(bx + 5, by + 5, bx + 9, by + 9, XP_BLUE);
    t.drawLine(bx + 13, by + 5, bx + 9, by + 9, XP_BLUE);
    t.setCursor(bx + 18, by + 4);
    t.print("MESSAGE");
    t.fillRoundRect(bx + bw - 15, by + 2, 12, 11, 2, XP_CLOSE);
    t.setTextColor(Theme::WHITE, XP_CLOSE);
    t.setCursor(bx + bw - 12, by + 4);
    t.print("x");
    // The body, and the message in it.
    const int cy0 = by + XP_TITLE;
    t.fillRect(bx + 2, cy0, bw - 4, bh - XP_TITLE - 2, XP_BODY);
    char stamp[24];
    if (Clock::trusted()) {
        char tm[8], date[20];
        Clock::formatStamp(m.at, tm, sizeof tm);
        Clock::formatDate(date, sizeof date);
        snprintf(stamp, sizeof stamp, "%s %s", tm, date + 4);   // drop the weekday
    } else {
        Clock::formatStamp(m.at, stamp, sizeof stamp);
    }
    char rows[3][48];
    const uint8_t n = messageLines(t, rows);
    Theme::bubbleFontOn(t);
    const int lineH = Theme::bubbleTextH() + 1;
    t.setTextColor(Theme::BLACK, XP_BODY);
    for (uint8_t i = 0; i < n; i++) {
        t.setCursor(bx + 8, cy0 + 4 + i * lineH + Theme::bubbleAscent());
        t.print(rows[i]);
    }
    Theme::bubbleFontOff(t);
    // The time, bottom right of the body, in the orange of a film camera.
    t.setTextSize(1);
    t.setTextColor(XP_CLOSE, XP_BODY);
    t.setCursor(bx + bw - 6 - t.textWidth(stamp), by + bh - 11);
    t.print(stamp);
    // The tap target: the box where it will come to rest, plus the polaroid.
    s_msgX0 = 0; s_msgY0 = restTop; s_msgX1 = bx + bw; s_msgY1 = restTop + bh;
}

// The polaroid at the left of his band: a white frame tilted four degrees,
// its shadow first, then the photo, square and level inside the margin.
static void drawPolaroid(TFT_eSPI& t, int top, float p, uint32_t now) {
    const MeshTalk::Message& m = MeshTalk::inbox();
    const int fw = 56, fh = 64;
    const int slide = (int)((1.0f - p) * 76.0f);
    const int fcx = 8 + fw / 2 - slide, fcy = top + 2 + fh / 2;
    fillTilted(t, fcx + 2, fcy + 2, fw, fh, -4.0f, Theme::BLACK);
    fillTilted(t, fcx,     fcy,     fw, fh, -4.0f, Theme::WHITE);
    const int pw = 40, ph = 40;
    const int px = fcx - pw / 2, py = fcy - fh / 2 + 7;
    t.fillRect(px, py, pw, ph, Theme::TASKBAR);
    // The sender, in his own outfit, waving. His look comes from the last
    // advert we heard from that board; a stranger gets the default.
    {
        SquachMesh::Peer look{};
        const bool known = Mesh::peerLook(m.mac, look);
        Squachy::setOutfitPreview(known ? (int8_t)look.outfit : 0);
        Squachy::setShadesPreview(known ? (int8_t)look.shade  : 0);
        Squachy::drawWaving(t, px + pw / 2, py + ph - 2, now, 0.5f, nullptr, false, 0, true, 34);
        Squachy::setShadesPreview(-1);
        Squachy::setOutfitPreview(-1);
    }
    // Four letters, bold: the face has no bold weight, so it is printed
    // twice a pixel apart, which is how a marker pen would do it too.
    if (p >= 1.0f) {
        Theme::bubbleFontOn(t);
        t.setTextColor(Theme::BLACK, Theme::WHITE);
        char name[5];
        snprintf(name, sizeof name, "%.4s", m.from);
        const int nw = t.textWidth(name) + 1;
        const int nx0 = fcx - nw / 2, ny0 = py + ph + 2 + Theme::bubbleAscent();
        t.setCursor(nx0,     ny0); t.print(name);
        t.setCursor(nx0 + 1, ny0); t.print(name);
        Theme::bubbleFontOff(t);
    }
    if (s_msgY0 == 0) { s_msgX0 = 0; s_msgY0 = top; s_msgX1 = 8 + fw; s_msgY1 = top + fh + 4; }
}
#endif

// compact: the narrow version that fits beside the clock's plate at the
// top right, for when the message box has the bottom of the screen.
static void drawAlertCard(TFT_eSPI& t, int barY, uint32_t now, bool compact, int leftX) {
    if (!uiDeskAlertUp(now)) return;
    const Detection& d = s_alert;
    const uint16_t c = Theme::colorFor(d.type);
    // Bottom left, just above the buttons: out of the way of his bubble,
    // which lives at the top over the clock. He is the one thing on this
    // screen that can stand being partly covered for nine seconds.
    s_cardW = compact ? 72 : CARD_W;
    s_cardX = compact ? t.width() - s_cardW - 4 : leftX;
    s_cardY = compact ? 18 : barY - CARD_H - 4;
    const int x = s_cardX, y = s_cardY;
    const int cw = s_cardW;
    // The border blinks for the first two seconds, then holds.
    const bool lit = (now - s_alertAt > 2000) || ((now / 250) & 1);
    t.fillRoundRect(x, y, cw, CARD_H, 4, Theme::BG);
    t.drawRoundRect(x, y, cw, CARD_H, 4, lit ? c : Theme::W95_SHADOW);
    Theme::drawTypeIcon(t, d.type, x + (compact ? 10 : 12), y + CARD_H / 2, compact ? 7 : 8);
    t.setTextSize(1);
    // The three lines sit against the card's right edge, so whatever their
    // length the icon on the left keeps its own room; the caps below keep
    // the longest line clear of it. Seven glyphs on the compact card, eleven
    // on the full one -- no more room exists there -- and a name that is
    // longer spends the last of them on the '>' mark rather than being cut
    // without one: "%.7s" drew "TRACKER" and "TRACKEX" the same, and two
    // "Flipper Zero ..." devices as one (include/ui_fit.h has the argument).
    const int right = x + cw - 4;
    auto printRight = [&](const char* s, int ty, uint16_t color) {
        t.setTextColor(color, Theme::BG);
        t.setCursor(right - t.textWidth(s), ty);
        t.print(s);
    };
    const int glyphs = compact ? 7 : 11;
    char ty[13];
    UiFit::fitHead(ty, sizeof ty, detectionTypeName(d.type), glyphs);
    printRight(ty, y + 4, c);
    // The device's own name where it has one, else the vendor; what fits.
    char who[13];
    UiFit::fitHead(who, sizeof who, d.name[0] ? d.name : vendorText(d), glyphs);
    printRight(who, y + 15, Theme::WHITE);
    char sig[16];
    snprintf(sig, sizeof sig, "%d dBm", d.rssi);
    printRight(sig, y + 26, Theme::CYAN);
}

void uiDeskTapTimer(uint32_t now) {
    if (s_timer == Timer::IDLE) {
        s_timer = Timer::FOCUS;
        s_timerEnd = now + FOCUS_MS;
        sayFocus("Twenty-five minutes. I'll keep the time. You keep the focus.", now);
    } else {
        // A running block is stopped by a tap, no confirmation: the cost of
        // a stray tap is one more tap.
        s_timer = Timer::IDLE;
        Squachy::announce("Timer off. No judgement.");
    }
}

void uiDeskTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    const int w = t.width(), h = t.height();
    const Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    const bool running = s_timer != Timer::IDLE;
    // Idle, it is the main screen with a clock on it: the chosen background
    // and Squachy's usual chatter. A running block clears the scene so the
    // timer is the only thing moving.
    if (running) {
        t.fillRect(0, 0, w, h, Theme::BG);
    } else {
        // To the bottom edge, around and under the buttons, as on the main
        // screen. The floor stays above them: what stands on the ground
        // still stands where it did.
        Theme::setBackgroundFloor(bar.y - 2);
        Theme::drawActiveBackground(t, now, 0, h, eng, advance);
        Theme::clearBackgroundFloor();
    }
    // The block runs out on its own; the announcement is what the user is
    // waiting for, so it is the first thing that happens.
    if (s_timer != Timer::IDLE && now >= s_timerEnd) {
        s_chimeAt = now;
        if (s_timer == Timer::FOCUS) {
            s_timer = Timer::BREAK;
            s_timerEnd = now + BREAK_MS;
            sayFocus("Time. Stand up, look at something far away. Five minutes.", now);
        } else {
            s_timer = Timer::IDLE;
            Squachy::announce("Break's over. Back to it, or don't, I'm a screen.");
        }
    }

    // The time: four digits, a colon, and the half of the day. Sized to the
    // screen's width so the portrait rotation keeps the same look.
    bool pm = false;
    char tm[8];
    Clock::formatTime(tm, sizeof tm, true, &pm);
    const bool set = Clock::trusted();
    // SIZE scales every measurement of the clock together. LARGE is held back
    // wherever it would not fit across the screen with its AM/PM and plate.
    static const float CLOCK_K[3] = { 0.78f, 1.0f, 1.3f };
    float k = CLOCK_K[Settings::clockSize() < 3 ? Settings::clockSize() : 1];
    const bool bangers = Settings::clockFont() == 1;
    // A backdrop gets a few more pixels of plate round the time, so the
    // digits are framed by it rather than sitting on its edge.
    const int  backdrop = running ? 0 : Settings::clockBackdrop();
    const int  pad = backdrop ? 5 : 0;
    {
        // The widest the plate gets: a two-digit hour with AM/PM beside it,
        // its air and its padding. It has to fit between the corner
        // icons, since it reaches up into their row; a size that would not
        // is held back to one that does, so nothing ends up on the border.
        const float scaled = 4.0f * 30.0f + 3.0f * 6.0f + 10.0f + 14.0f;
        const float fixedW = 8.0f + 2.0f * (float)pad;
        const float room   = (float)(w - (2 * Theme::TITLE_ICON_W + 2));
        if (k * scaled + fixedW > room) k = (room - fixedW) / scaled;
    }
    const int dh = (int)(42.0f * k + 0.5f), dw = (int)(30.0f * k + 0.5f);
    const int th = (int)(7.0f * k + 0.5f) < 3 ? 3 : (int)(7.0f * k + 0.5f);
    const int gap = (int)(6.0f * k + 0.5f), colonW = (int)(10.0f * k + 0.5f);
    // "H:MM" or "HH:MM": lay the digits out from the string so the leading
    // hour digit is simply absent, not a dark ghost.
    int n = 0; char digits[4]; bool colonAfter[4] = {false, false, false, false};
    for (const char* p = tm; *p && n < 4; p++) {
        if (*p == ':') { if (n) colonAfter[n - 1] = true; continue; }
        digits[n++] = *p;
    }
    // What is centred is what SHOWS: from the first digit's ink to the end of
    // AM/PM, in the plate and on the screen, the same way the date is. Cells
    // alone are not enough -- a segment 1 lights only its right-hand bars, so
    // "10:01" centred by its cells sat visibly right of the date, in a plate
    // with a gap down one side and none down the other.
    const int digitsW = n * dw + (n - 1) * gap + colonW;
    const int AMPM_W  = 14;                    // 2 px of air and "AM" at 12
    int   bInkTop = 0, bInkH = 1;
    float bScale  = 1.0f;
    if (bangers) {
        Theme::bangersDigitInk(bInkTop, bInkH);
        bScale = (float)dh / (float)bInkH;
    }
    int inkLead = 0;                           // blank cell before the first ink
    if (n > 0) {
        if (bangers) {
            const int gw = (int)((float)Theme::bangersGlyphAdvance(digits[0]) * bScale + 0.5f);
            inkLead = (gw < dw ? (dw - gw) / 2 : 0) +
                      (int)lroundf((float)Theme::bangersGlyphInkLeft(digits[0]) * bScale);
        } else if (digits[0] == '1') {
            inkLead = dw - th;
        }
    }
    const int totalW  = digitsW + AMPM_W - inkLead;
    int x = (w - totalW) / 2 - inkLead;
    // The plate sits high -- 3 px under the top edge, digits from 26 -- so
    // the room under it is Squachy's, with a little air between his bubble
    // and the clock's foot rather than the overlap the first cut had.
    const int plateTop = 3;
    const int y = 26 + pad;
    const uint16_t on  = set ? Theme::VAPOR_PINK : Theme::W95_SHADOW;

    // The message box, first, so the plate drawn over it hides whatever
    // part is still behind the plate on its way in or out.
    const int plateBottom = y + dh + 5 + pad;
    float msgP = 0.0f;
    int   boxH = 0;
#if SQUACH_MESH
    msgP = messageProgress(now);
    if (msgP > 0.0f) {
        boxH = messageBoxH(t);
        drawMessageBox(t, plateBottom + 4, msgP, now);
    } else {
        s_msgX1 = s_msgY1 = 0;
    }
#endif

    // The date, small, above the time. Over a scene the two sit on a dark
    // plate: pink digits over the synthwave sun were pink on pink.
    char date[20];
    Clock::formatDate(date, sizeof date);
    t.setTextSize(2);
    const int dateW = t.textWidth(date);
    // Four px of air a side, not ten: the plate hugs the time. It still grows
    // when the hour gets its second digit at ten, but by the digit alone, and
    // in portrait a two-digit hour no longer runs it into the corner icons.
    int pw = (dateW > totalW ? dateW : totalW) + 8 + 2 * pad;
    // The title bar's corner icons are painted after the plate and blank
    // their own boxes at each end of the top rows; a plate that reaches
    // up into that band stays between them (portrait with a two-digit
    // hour is the case that bit).
    const int iconsW = 2 * Theme::TITLE_ICON_W + 2;
    if (plateTop < Theme::TITLE_ICON_BAND_H && pw > w - iconsW) pw = w - iconsW;
    const int px = (w - pw) / 2;
    const int plateH = plateBottom - plateTop;
    s_plateX = (int16_t)px; s_plateY = (int16_t)plateTop;
    s_plateW = (int16_t)pw; s_plateH = (int16_t)plateH;
    if (!running) {
        t.fillRoundRect(px, plateTop, pw, plateH, 5, Theme::BG);
        // Inside the outline, and under everything written on the plate.
        if (backdrop) Theme::drawClockBackdrop(t, now, px + 2, plateTop + 2, pw - 4, plateH - 4, (uint8_t)backdrop);
        t.drawRoundRect(px, plateTop, pw, plateH, 5, Theme::VAPOR_PURPLE);
    }
    if (running) t.fillRect(px, plateTop, pw, plateH, Theme::BG);   // over the box, on a plain ground
    // Over a backdrop the date keeps a dark strip behind it; the digits get
    // an outline of their own below.
    // Again: the rain draws its glyphs at size 1 and leaves it there, which
    // printed the date small on that backdrop.
    t.setTextSize(2);
    if (backdrop) {
        printOutlined(t, (w - dateW) / 2, plateTop + 3 + pad, date, Theme::CYAN);
    } else {
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor((w - dateW) / 2, plateTop + 3 + pad);
        t.print(date);
    }
    t.setTextSize(1);
    Theme::drawTitleBar(t, ">> DESK <<");

    // Bangers draws into the same cells the segments use, so the plate, the
    // AM/PM and everything measured off them stay where they are. Each digit
    // is centred in its cell: a proportional 1 would shuffle the time sideways
    // every minute it came and went.
    const int dot = (int)(6.0f * k + 0.5f) < 3 ? 3 : (int)(6.0f * k + 0.5f);
    for (int i = 0; i < n; i++) {
        const int d = (digits[i] >= '0' && digits[i] <= '9') ? digits[i] - '0' : -1;
        if (bangers) {
            if (d >= 0) {
                const char ch = digits[i];
                const int gw = (int)((float)Theme::bangersGlyphAdvance(ch) * bScale + 0.5f);
                const int gx = x + (dw - gw) / 2;
                const int gy = y - (int)((float)bInkTop * bScale + 0.5f);
                // With a dark keyline over a backdrop, so the digit reads.
                Theme::drawBangersGlyphScaled(t, gx, gy, ch, on, bScale, backdrop ? 2 : 0, Theme::BG);
            }
        } else {
            if (backdrop) drawDigitOutline(t, x, y, dw, dh, th, d, 2, Theme::BG);
            drawDigit(t, x, y, dw, dh, th, d, on);
        }
        x += dw;
        if (colonAfter[i]) {
            const bool blink = ((now / 500) & 1) == 0;
            // Bangers leans, so its colon does too: the lower dot sits a
            // little left of the upper one.
            const int lean = bangers ? (int)(3.0f * k + 0.5f) : 0;
            const int cx0 = x + (colonW - dot) / 2;
            if (backdrop && blink) {
                t.fillRect(cx0 + lean - 2, y + dh / 3 - dot / 2 - 2, dot + 4, dot + 4, Theme::BG);
                t.fillRect(cx0 - 2, y + 2 * dh / 3 - dot / 2 - 2, dot + 4, dot + 4, Theme::BG);
            }
            if (blink) {
                t.fillRect(cx0 + lean, y + dh / 3 - dot / 2, dot, dot, on);
                t.fillRect(cx0, y + 2 * dh / 3 - dot / 2, dot, dot, on);
            }
            x += colonW;
        }
        x += gap;
    }
    // AM / PM beside the last digit, at its foot.
    {
        const uint16_t ac = set ? Theme::VAPOR_YELLOW : Theme::W95_SHADOW;
        const char* half = set ? (pm ? "PM" : "AM") : "--";
        if (backdrop) {
            printOutlined(t, x - gap + 2, y + dh - 8, half, ac);
        } else {
            t.setTextColor(ac, Theme::BG);
            t.setCursor(x - gap + 2, y + dh - 8);
            t.print(half);
        }
    }

    if (!set) {
        const char* m = Clock::guessed() ? "NOT SINCE THE POWER WENT. WIFI AT BOOT SETS IT"
                                         : "SET BY WIFI AT BOOT, OR TIME <EPOCH> ON SERIAL";
        t.setTextColor(Theme::W95_SHADOW, Theme::BG);
        t.setCursor((w - t.textWidth(m)) / 2, y + dh + 4);
        t.print(m);
    }

    // The timer's progress, a thin bar under the digits while a block runs.
    if (s_timer != Timer::IDLE) {
        const uint32_t total = s_timer == Timer::FOCUS ? FOCUS_MS : BREAK_MS;
        const uint32_t left  = s_timerEnd > now ? s_timerEnd - now : 0;
        const int bw = w - 40;
        const int fill = (int)((uint64_t)bw * (total - left) / total);
        const uint16_t c = s_timer == Timer::FOCUS ? Theme::AMBER : Theme::GREEN;
        t.drawRect(20, y + dh + 6, bw, 5, c);
        if (fill > 0) t.fillRect(21, y + dh + 7, fill > bw - 2 ? bw - 2 : fill, 3, c);
    }

    // Squachy, in whatever is left. His bubble rises 16 px above topY
    // whatever its width, and it is drawn after the plate: it overlaps
    // the foot of the clock and sits on top of it, which is the point.
    //
    // With a message up, landscape has room for him beside the box at full
    // height; portrait puts him under it. Both measured from where the box
    // rests, not where it is mid-drop, so he does not jitter.
    const bool msgOn = msgP > 0.0f;
    const bool side  = msgOn && (w - (XP_LEFT + XP_W)) >= 80;
    // 25 under the plate's foot: his bubble rises 16 above his top, which
    // leaves 4 px of air between bubble and plate.
    const int  top   = (msgOn && !side) ? plateBottom + 4 + boxH + 18 : y + dh + 25;
    const int  cx    = side ? XP_LEFT + XP_W + (w - XP_LEFT - XP_W) / 2 : w / 2;
    const int  feet  = bar.y - 2;
    // Beside the box his bubble would lie across it, so it waits.
    Squachy::holdBubble(side);
#if SQUACH_MESH
    // The squad under the clock, when the DESK MODE page says so. It is the band
    // Squachy would have had to himself, and he is in it -- the layout keeps
    // the most central seat for him, which is what makes him findable in a
    // crowd where everything else is drifting. Never over a focus block:
    // that clears the scene on purpose, so the timer is the only thing
    // moving. Zero inset at the bottom, unlike the main screen, because the
    // desk has no squad badge and no counters under its band.
    uint8_t crowdN = 0;
    Mesh::SquadMember crowd[8];
    // HOW MANY means what it means on the main screen. ONE there is Squachy
    // and one visitor, so ONE here is Squachy and one member too; UP TO N
    // is up to N bodies, ours among them. The desk used to require more
    // than ONE, so a board set to ONE showed Squachy alone under the clock
    // while the main screen beside it showed him with company.
    const bool squadOn = !running && Settings::deskSquad();
    if (squadOn) {
        const uint8_t bodies = Settings::deskCrowd() > 8 ? 8 : Settings::deskCrowd();
        const uint8_t peers  = bodies > 1 ? (uint8_t)(bodies - 1) : 1;
        crowdN = Mesh::squadList(now, crowd, peers);
        // The main screen's visit machine: who is visiting, what they say,
        // when they laugh. Without it the visitor had nothing to do on the
        // desk but stand there waving.
        uiClearVisitTick(now);
    }
    // From ONE member up. With one, VISITOR decides: the full visit below,
    // or the two of them chatting in place, bigger, through the crowd's
    // layout. The desk used to wait for two, and a squad of two boards --
    // each seeing one -- never showed up under the clock at all.
    // One visitor with VISITOR set to FULL VISIT: the main screen's own visit,
    // walk-in, high five, set pieces and all, standing on the desk's floor.
    if (squadOn && crowdN == 1 && Settings::deskFullVisit() &&
        uiClearDrawVisit(t, now, top, feet, true)) {
        Theme::drawBackgroundOverlay(t, now);
    } else if (crowdN >= 1) {
        // Just the two of them: a fifth bigger, and what they say goes up on
        // the clock, the lower half of the plate, with the tails reaching
        // down to them. Their bubbles no longer need the room over their
        // heads, which is the room the extra size takes.
        const bool pair = crowdN == 1;
        const int  bubY = plateBottom - (Theme::bubbleTextH() + 6) - 2;
        uiClearDrawCrowd(t, now, crowd, crowdN, top, feet, uiMascotStep(now, true), false, 0,
                         pair ? 1.2f : 1.0f, pair ? bubY : -1);
        Theme::drawBackgroundOverlay(t, now);
    } else
#endif
    if (!running) {
        Squachy::tick(t, cx, top, feet - top, now, uiMascotStep(now, advance), 0.5f);
        Theme::drawBackgroundOverlay(t, now);
    } else {
        // Still, and quiet: no idle chatter over a focus block. His own
        // height is about 58 rows at scale 1, so he fills the band.
        const int band = feet - top;
        float sc = (float)(band - 12) / 58.0f;
        if (sc > 1.6f) sc = 1.6f;
        if (sc < 0.6f) sc = 0.6f;
        const char* line = (s_line && now < s_lineUntil) ? s_line : nullptr;
        Squachy::drawWaving(t, cx, feet, now, sc, line, line != nullptr, 0, false, 18);
    }
    Squachy::holdBubble(false);
#if SQUACH_MESH
    if (msgOn) drawPolaroid(t, y + dh + 18, msgP, now);
#endif

    // The buttons, over the background. It used to stop at the bar and the
    // strip was cleared here, because the starfield's space junk drew past
    // the band it was handed and showed round the buttons. The band is the
    // whole screen now, so the edge of the panel clips the junk, and each
    // button fills its own box.
    int tx, ty, tw, tth, bx, bw;
    timerRects(w, h, tx, ty, tw, tth, bx, bw);
    char lbl[16];
    timerLabel(lbl, sizeof lbl, now);
    Theme::drawButton(t, tx, ty, tw, tth, lbl, s_timer != Timer::IDLE);
    Theme::drawButton(t, bx, ty, bw, tth, "BACK", false);
    {
        int gx, gy, gs;
        gearRect(w, h, gx, gy, gs);
        Theme::drawButton(t, gx, gy, gs, gs, "", false);
        drawGear(t, gx + gs / 2, gy + gs / 2, gs / 2 - 5, Theme::CYAN, Theme::BG);
    }

    // Over everything, so it is never behind his bubble. Bottom left is
    // clear of the polaroid and of the box in either rotation.
    drawAlertCard(t, bar.y, now, false, 4);
}

bool uiDeskHitMessage(int x, int y) {
#if SQUACH_MESH
    if (s_msgX1 > s_msgX0 && x >= s_msgX0 && x < s_msgX1 && y >= s_msgY0 && y < s_msgY1) {
        MeshTalk::markRead();
        s_msgSeenAt = MeshTalk::inbox().at;
        s_msgSeen   = true;
        s_msgX1 = s_msgY1 = 0;
        return true;
    }
#else
    (void)x; (void)y;
#endif
    return false;
}

bool uiDeskHitTimer(int x, int y, int screenW, int screenH) {
    int tx, ty, tw, th, bx, bw;
    timerRects(screenW, screenH, tx, ty, tw, th, bx, bw);
    return x >= tx && x <= tx + tw && y >= ty && y <= ty + th;
}

int uiDeskHitClockEdge(int x, int y) {
    if (s_plateW <= 0 || y < s_plateY || y >= s_plateY + s_plateH) return 0;
    const int edge = s_plateW / 5;
    if (x >= s_plateX && x < s_plateX + edge) return -1;
    if (x >= s_plateX + s_plateW - edge && x < s_plateX + s_plateW) return 1;
    return 0;
}

bool uiDeskHitSettings(int x, int y, int screenW, int screenH) {
    int gx, gy, gs;
    gearRect(screenW, screenH, gx, gy, gs);
    return x >= gx && x <= gx + gs && y >= gy && y <= gy + gs;
}

bool uiDeskHitBack(int x, int y, int screenW, int screenH) {
    int tx, ty, tw, th, bx, bw;
    timerRects(screenW, screenH, tx, ty, tw, th, bx, bw);
    return x >= bx && x <= bx + bw && y >= ty && y <= ty + th;
}
