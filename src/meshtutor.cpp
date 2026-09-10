// SquachWatch-CYD — the messages tutorial. See include/meshtutor.h.
#include "meshtutor.h"

#if SQUACH_MESH
#include "theme.h"
#include <math.h>
#include <stdio.h>

namespace MeshTutor {
namespace {

Step s_step = Step::OFF;

// Where the card and its SKIP corner were last drawn.
bool    s_cardOn = false;
int16_t s_cx = 0, s_cy = 0, s_cw = 0, s_ch = 0;
int16_t s_kx = 0, s_ky = 0, s_kw = 0, s_kh = 0;

// A costume nobody wears by default, and a name that says what it is.
SquachMesh::Peer s_demo = { 0, 12, 1, true, "DEMO" };

// Every card is written to be read in one go, and the three on the message
// screen are held to about 80 characters: they share the top of that screen
// with nothing, but only 52 pixels of it.
const char* body(Step s) {
    switch (s) {
        case Step::VISIT:
            return "When another SquachWatch is near, its Squachy comes to visit. "
                   "This one is a demo: nothing you do here goes on the air.";
        case Step::TAP_ICON:
            return "To send a message, tap the speech bubble beside your visitor.";
        case Step::PICK_LINE:
            return "Messages are ready-made lines, small enough for one radio packet. Tap one.";
        case Step::PRESS_SEND:
            return "It asks first, so a wrong tap costs nothing. Check the line, then press SEND.";
        case Step::REPLY:
            return "It answered. A message from a person is red and carries their name. "
                   "The other chat is just the two Squachys.";
        case Step::PHRASE:
            return "Only boards that share a five-word phrase can read each other. One of you "
                   "ROLLs a phrase and reads it out; the other ENTERs the same words. "
                   "SQUACHMESH > PHRASE.";
        case Step::PRIVACY:
            return "Messages are encrypted, but anyone scanning can see that you sent one. "
                   "Reading needs DETECT; sending needs TRANSMIT.";
        default:
            return "";
    }
}

const char* hint(Step s) {
    switch (s) {
        case Step::VISIT:
        case Step::REPLY:
        case Step::PHRASE:  return "TAP TO CONTINUE";
        case Step::PRIVACY: return "TAP TO FINISH";
        default:            return nullptr;          // the arrow says what to tap
    }
}

inline int imin(int a, int b) { return a < b ? a : b; }
inline int iabs(int a) { return a < 0 ? -a : a; }

} // namespace

void start()           { s_step = Step::VISIT; }
void stop()            { s_step = Step::OFF; s_cardOn = false; }
bool active()          { return s_step != Step::OFF; }
Step step()            { return s_step; }
void setStep(Step s)   { s_step = s; }

void next() {
    switch (s_step) {
        case Step::VISIT:      s_step = Step::TAP_ICON;   break;
        case Step::TAP_ICON:   s_step = Step::PICK_LINE;  break;
        case Step::PICK_LINE:  s_step = Step::PRESS_SEND; break;
        case Step::PRESS_SEND: s_step = Step::REPLY;      break;
        case Step::REPLY:      s_step = Step::PHRASE;     break;
        case Step::PHRASE:     s_step = Step::PRIVACY;    break;
        default:               stop();                    break;
    }
}

bool waitsForTap() { return hint(s_step) != nullptr; }

const SquachMesh::Peer* guest() { return active() ? &s_demo : nullptr; }

void drawCard(TFT_eSPI& t, bool atTop) {
    s_cardOn = false;
    if (!active()) return;
    t.setTextSize(1);
    t.setTextWrap(false);
    const int w = t.width(), h = t.height();
    const int cw = w - 8;
    // Theme::wrapText fills 48-character rows; cap the width at 47 of them.
    const int charW = t.textWidth("M");
    int maxW = cw - 16;
    if (maxW > 47 * charW) maxW = 47 * charW;
    char lines[6][48];
    const uint8_t n = Theme::wrapText(t, body(s_step), maxW, lines, 6);
    const char* hn = hint(s_step);
    const int lh = t.fontHeight() + 1;
    const int ch = 16 + n * lh + (hn ? lh + 2 : 0) + 4;
    const int cx = 4, cy = atTop ? 2 : h - ch - 2;

    t.fillRect(cx, cy, cw, ch, Theme::BG);
    t.drawRect(cx, cy, cw, ch, Theme::VAPOR_PINK);
    t.drawRect(cx + 1, cy + 1, cw - 2, ch - 2, Theme::PURPLE);

    char hd[20];
    snprintf(hd, sizeof hd, "MESSAGES %u/%u", (unsigned)s_step, (unsigned)STEPS);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(cx + 6, cy + 5);
    t.print(hd);
    const int kw = t.textWidth("SKIP");
    t.setTextColor(Theme::W95_LIGHT, Theme::BG);
    t.setCursor(cx + cw - 6 - kw, cy + 5);
    t.print("SKIP");
    // A finger-sized corner rather than the four letters.
    s_kx = (int16_t)(cx + cw - kw - 22);
    s_ky = (int16_t)cy;
    s_kw = (int16_t)(kw + 22);
    s_kh = 22;

    int y = cy + 16;
    t.setTextColor(Theme::WHITE, Theme::BG);
    for (uint8_t i = 0; i < n; i++) {
        t.setCursor(cx + 6, y);
        t.print(lines[i]);
        y += lh;
    }
    if (hn) {
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor(cx + cw - 6 - t.textWidth(hn), y + 2);
        t.print(hn);
    }
    s_cx = (int16_t)cx; s_cy = (int16_t)cy; s_cw = (int16_t)cw; s_ch = (int16_t)ch;
    s_cardOn = true;
}

Tap cardTap(int x, int y) {
    if (!s_cardOn) return Tap::NONE;
    if (x >= s_kx && x < s_kx + s_kw && y >= s_ky && y < s_ky + s_kh) return Tap::SKIP;
    if (x >= s_cx && x < s_cx + s_cw && y >= s_cy && y < s_cy + s_ch) return Tap::CARD;
    return Tap::NONE;
}

void drawArrow(TFT_eSPI& t, uint32_t now, int x, int y, Dir d) {
    const int HEAD = 9, HALF = 7, STEM = 12;
    int ux = 0, uy = 0;                               // the way it points
    switch (d) {
        case Dir::UP:    uy = -1; break;
        case Dir::DOWN:  uy =  1; break;
        case Dir::LEFT:  ux = -1; break;
        case Dir::RIGHT: ux =  1; break;
    }
    // Bobs toward the target and back, about once a second.
    const int off = 3 + (int)lroundf(3.0f * sinf((float)now * 0.007f));
    const int tipX = x - ux * off, tipY = y - uy * off;
    const int bx = tipX - ux * HEAD, by = tipY - uy * HEAD;   // centre of the head's base
    const int px = -uy, py = ux;                              // across the arrow

    // Stem, outlined, then the head over it.
    const int ex = bx - ux * STEM, ey = by - uy * STEM;
    const int rx = imin(bx, ex) - iabs(px) * 3, ry = imin(by, ey) - iabs(py) * 3;
    const int rw = iabs(ex - bx) + iabs(px) * 6 + 1, rh = iabs(ey - by) + iabs(py) * 6 + 1;
    t.fillRect(rx - 1, ry - 1, rw + 2, rh + 2, Theme::BG);
    t.fillRect(rx, ry, rw, rh, Theme::VAPOR_YELLOW);
    const int ax = bx + px * HALF, ay = by + py * HALF;
    const int cx = bx - px * HALF, cy = by - py * HALF;
    t.fillTriangle(tipX, tipY, ax, ay, cx, cy, Theme::VAPOR_YELLOW);
    t.drawTriangle(tipX, tipY, ax, ay, cx, cy, Theme::BG);
}

void drawFrame(TFT_eSPI& t, uint32_t now, int x, int y, int w, int h) {
    if ((now / 350) % 2) return;                      // blinks, so it is seen
    t.drawRect(x - 3, y - 3, w + 6, h + 6, Theme::VAPOR_YELLOW);
    t.drawRect(x - 2, y - 2, w + 4, h + 4, Theme::VAPOR_YELLOW);
}

} // namespace MeshTutor
#endif // SQUACH_MESH
