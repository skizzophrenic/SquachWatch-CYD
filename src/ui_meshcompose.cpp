// SquachWatch-CYD — the SquachMesh message screen. See include/ui_meshcompose.h.
#include "ui_meshcompose.h"

#if SQUACH_MESH
#include "theme.h"
#include "meshtalk.h"
#include "meshmsg.h"
#include "meshtutor.h"
#include "settings.h"
#include "detection.h"
#include "ui_clear.h"
#include "emote_script.h"
#include "squachy.h"
#include <Arduino.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>

namespace {

struct Rect { int16_t x, y, w, h; };
inline bool inRect(const Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

uint8_t     s_page  = 0;
uint8_t     s_pages = 1;
Rect        s_lineRect[12];
uint8_t     s_lineIdx[12];
uint8_t     s_lineN = 0;
Rect        s_back = { 0, 0, 0, 0 }, s_prev = { 0, 0, 0, 0 }, s_next = { 0, 0, 0, 0 };
Rect        s_send = { 0, 0, 0, 0 };
Rect        s_help = { 0, 0, 0, 0 };
Rect        s_type = { 0, 0, 0, 0 };
const char* s_status = nullptr;
uint16_t    s_statusCol = 0;
// The line waiting to be confirmed, or -1. A tap on a line only chooses it;
// SEND sends it. One wrong tap on a list of two dozen small targets should
// cost a second tap, not a message you did not mean.
int8_t      s_sel = -1;
char        s_confirm[40];
// A typed message back from the keyboard, waiting to be sent -- the same
// confirmation as a line, with the whole text shown large instead of quoted
// on the status row, which is too narrow for forty-eight characters.
char        s_typed[MeshMsg::TEXT_MAX + 1] = "";
bool        s_typedOn = false;
// Emotes: the smiley at the top swaps the lines for things to DO -- thirty-six
// of them, six tabs of six. One tap sends one -- no confirmation, unlike a
// message: it is over in a few seconds and a wrong one costs a laugh, not
// something said that was not meant. RANDOM picks for you.
bool        s_emoteOn  = false;
Rect        s_emoteBtn = { 0, 0, 0, 0 };
Rect        s_emoteRect[EmoteScript::PER_TAB];
Rect        s_tabRect[EmoteScript::TABS];
Rect        s_random = { 0, 0, 0, 0 };
// Kept across visits to this screen, not across a reboot: whoever sends a lot
// of pranks should find the pranks where they left them.
uint8_t     s_tab = 0;

const int BW = 68, BH = 26, AW = 44, TW = 56;

// Held to 36 characters. Portrait is 240 wide and these start 8 in, so 38 is
// the most that fits; the first versions ran to 41 and lost their last words
// off the edge -- "you can read, not repl" -- which the emulator caught.
const char* const NEED_PHRASE  = "Set a phrase: SQUACHMESH > PHRASE.";
const char* const TRANSMIT_OFF = "TRANSMIT off: can read, not reply.";
const char* const SENDING      = "Sending, for thirty seconds.";
const char* const SEND_FAILED  = "Could not send. Try again.";
const char* const NOBODY_HERE  = "Emotes need a visitor on screen.";
const char* const STILL_ON_AIR = "Your message is still sending.";
const char* const EMOTE_HINT   = "Tap one: you both act it out.";

// The reasons a message could not go out, given BEFORE anything is chosen or
// typed rather than after the person has confirmed it. Null when it can.
const char* cannotSend() {
    if (!MeshTalk::ready())             return NEED_PHRASE;
    if (!Settings::meshTransmit())      return TRANSMIT_OFF;
    return nullptr;
}

// The tutorial's rules for this screen: only the step's target does anything,
// and nothing is ever sent. SEND moves the lesson on and goes back to the main
// screen for the pretend reply; MeshTalk is never called.
ComposeHit tutorTouch(int x, int y) {
    if (MeshTutor::cardTap(x, y) == MeshTutor::Tap::SKIP) {
        MeshTutor::stop();
        s_sel = -1;
        return ComposeHit::NONE;       // the screen is theirs again, as it is
    }
    const MeshTutor::Step st = MeshTutor::step();
    if (st == MeshTutor::Step::PRESS_SEND) {
        if (inRect(s_send, x, y)) { s_sel = -1; MeshTutor::next(); return ComposeHit::SENT; }
        if (inRect(s_back, x, y)) {    // CANCEL: back a step, as it would be
            s_sel = -1;
            MeshTutor::setStep(MeshTutor::Step::PICK_LINE);
            return ComposeHit::NONE;
        }
    }
    if (st == MeshTutor::Step::PICK_LINE || st == MeshTutor::Step::PRESS_SEND) {
        for (uint8_t i = 0; i < s_lineN; i++) {
            if (!inRect(s_lineRect[i], x, y)) continue;
            s_sel = (int8_t)s_lineIdx[i];
            if (st == MeshTutor::Step::PICK_LINE) MeshTutor::next();
            return ComposeHit::NONE;
        }
    }
    return ComposeHit::NONE;
}

void heart(TFT_eSPI& t, int cx, int cy, int r, uint16_t c) {
    t.fillCircle(cx - r / 2, cy - 1, r / 2 + 1, c);
    t.fillCircle(cx + r / 2, cy - 1, r / 2 + 1, c);
    t.fillTriangle(cx - r - 1, cy, cx + r + 1, cy, cx, cy + r + 2, c);
}

void drawSmiley(TFT_eSPI& t, int cx, int cy) {
    t.fillCircle(cx, cy, 7, Theme::VAPOR_YELLOW);
    t.fillRect(cx - 3, cy - 3, 2, 2, Theme::BG);
    t.fillRect(cx + 2, cy - 3, 2, 2, Theme::BG);
    t.drawFastHLine(cx - 2, cy + 3, 5, Theme::BG);
    t.drawPixel(cx - 3, cy + 2, Theme::BG);
    t.drawPixel(cx + 3, cy + 2, Theme::BG);
}

// Each emote's picture, about sixteen pixels square, centred on (cx, cy). Only
// rectangles, circles and pixels: this draws into the frame sprite.
void drawEmoteIcon(TFT_eSPI& t, uint8_t e, int cx, int cy) {
    const uint16_t Y = Theme::VAPOR_YELLOW, W = Theme::WHITE, K = Theme::BG, P = Theme::VAPOR_PINK;
    switch ((MeshMsg::Emote)e) {
    case MeshMsg::Emote::WAVE:                          // an open hand, and the motion
        t.fillRoundRect(cx - 5, cy - 1, 10, 8, 2, Y);
        t.fillRect(cx - 5, cy - 5, 2, 5, Y);
        t.fillRect(cx - 2, cy - 7, 2, 7, Y);
        t.fillRect(cx + 1, cy - 7, 2, 7, Y);
        t.fillRect(cx + 4, cy - 5, 2, 5, Y);
        t.fillRect(cx - 8, cy + 1, 3, 2, Y);
        t.drawFastVLine(cx + 8, cy - 6, 3, W);
        t.drawFastVLine(cx + 10, cy - 3, 3, W);
        break;
    case MeshMsg::Emote::HIGH_FIVE:                     // two hands meeting, and the spark
        t.fillRoundRect(cx - 10, cy - 1, 7, 7, 2, Y);
        t.fillRect(cx - 10, cy - 5, 7, 4, Y);
        t.fillRoundRect(cx + 3, cy - 1, 7, 7, 2, Y);
        t.fillRect(cx + 3, cy - 5, 7, 4, Y);
        t.drawFastVLine(cx, cy - 9, 5, W);
        t.drawFastHLine(cx - 2, cy - 7, 5, W);
        break;
    case MeshMsg::Emote::DANCE:                         // a note
        t.fillCircle(cx - 3, cy + 4, 3, P);
        t.fillRect(cx - 1, cy - 7, 2, 11, P);
        t.fillRect(cx + 1, cy - 7, 4, 2, P);
        t.fillRect(cx + 3, cy - 5, 2, 3, P);
        break;
    case MeshMsg::Emote::RPS:                           // one of each
        t.fillCircle(cx - 7, cy + 2, 3, Theme::W95_LIGHT);
        t.fillRect(cx - 2, cy - 4, 5, 8, W);
        for (int i = 0; i < 5; i++) {
            t.drawPixel(cx + 5 + i, cy - 4 + i, W);
            t.drawPixel(cx + 9 - i, cy - 4 + i, W);
        }
        t.fillRect(cx + 4, cy + 1, 2, 2, P);
        t.fillRect(cx + 9, cy + 1, 2, 2, P);
        break;
    case MeshMsg::Emote::SNOWBALL:
        t.fillCircle(cx, cy, 6, Theme::W95_SHADOW);
        t.fillCircle(cx - 1, cy - 1, 5, W);
        break;
    case MeshMsg::Emote::BOO:                           // a ghost
        t.fillCircle(cx, cy - 2, 6, W);
        t.fillRect(cx - 6, cy - 2, 13, 8, W);
        for (int i = 0; i < 3; i++) t.fillRect(cx - 4 + i * 4, cy + 5, 2, 1, K);
        t.fillRect(cx - 3, cy - 4, 2, 3, K);
        t.fillRect(cx + 2, cy - 4, 2, 3, K);
        t.fillRect(cx - 1, cy + 1, 3, 2, K);
        break;
    case MeshMsg::Emote::FIST_BUMP:                     // two fists, meeting
        t.fillRoundRect(cx - 11, cy - 3, 8, 7, 2, Y);
        t.fillRoundRect(cx + 3, cy - 3, 8, 7, 2, Y);
        t.drawFastVLine(cx, cy - 8, 4, W);
        t.drawLine(cx - 4, cy - 7, cx - 2, cy - 5, W);
        t.drawLine(cx + 4, cy - 7, cx + 2, cy - 5, W);
        break;
    case MeshMsg::Emote::HANDSHAKE:                     // hands clasped, two cuffs
        t.fillRoundRect(cx - 8, cy - 3, 16, 7, 3, Y);
        t.drawFastVLine(cx - 1, cy - 3, 7, K);
        t.fillRect(cx - 12, cy - 4, 4, 9, Theme::CYAN);
        t.fillRect(cx + 8, cy - 4, 4, 9, P);
        break;
    case MeshMsg::Emote::SALUTE:                        // two chevrons
        for (int i = 0; i < 2; i++) {
            t.drawWideLine(cx - 7, cy - 5 + i * 5, cx, cy + i * 5, 2, Y);
            t.drawWideLine(cx, cy + i * 5, cx + 7, cy - 5 + i * 5, 2, Y);
        }
        break;
    case MeshMsg::Emote::BOW:                           // a top hat, doffed
        t.fillRect(cx - 7, cy + 3, 14, 2, P);
        t.fillRect(cx - 4, cy - 6, 8, 9, P);
        t.fillRect(cx - 4, cy, 8, 2, Y);
        break;
    case MeshMsg::Emote::HUG:                           // arms round a heart
        heart(t, cx, cy, 3, P);
        t.drawWideLine(cx - 9, cy - 4, cx - 5, cy + 6, 2, Y);
        t.drawWideLine(cx + 9, cy - 4, cx + 5, cy + 6, 2, Y);
        break;
    case MeshMsg::Emote::COIN:
        t.fillCircle(cx, cy, 7, Y);
        t.drawCircle(cx, cy, 7, Theme::AMBER);
        t.drawCircle(cx, cy, 4, Theme::AMBER);
        break;
    case MeshMsg::Emote::DICE:
        t.fillRoundRect(cx - 7, cy - 7, 14, 14, 2, W);
        t.fillCircle(cx - 4, cy - 4, 1, Theme::BLACK);
        t.fillCircle(cx, cy, 1, Theme::BLACK);
        t.fillCircle(cx + 4, cy + 4, 1, Theme::BLACK);
        break;
    case MeshMsg::Emote::ARM_WRESTLE:                   // a flexed arm
        t.fillRect(cx - 9, cy + 1, 12, 5, Y);
        t.fillCircle(cx + 3, cy - 2, 5, Y);
        t.fillCircle(cx + 4, cy - 7, 3, Y);
        break;
    case MeshMsg::Emote::TUG:                           // a rope and its flag
        t.drawWideLine(cx - 10, cy - 2, cx + 10, cy - 2, 2, Theme::AMBER);
        t.fillTriangle(cx - 3, cy - 1, cx + 3, cy - 1, cx, cy + 6, Theme::RED);
        break;
    case MeshMsg::Emote::LEAPFROG: {                    // a frog
        const uint16_t G = Theme::GREEN;
        t.fillEllipse(cx, cy + 2, 8, 5, G);
        t.fillCircle(cx - 4, cy - 3, 3, G);
        t.fillCircle(cx + 4, cy - 3, 3, G);
        t.fillRect(cx - 5, cy - 4, 2, 2, K);
        t.fillRect(cx + 3, cy - 4, 2, 2, K);
        break;
    }
    case MeshMsg::Emote::PIE:
        t.fillEllipse(cx, cy + 3, 9, 4, Theme::W95_SHADOW);
        t.fillEllipse(cx, cy, 8, 4, W);
        t.fillCircle(cx, cy - 3, 2, Theme::RED);
        break;
    case MeshMsg::Emote::BALLOON:
        t.fillCircle(cx, cy - 2, 6, Theme::VAPOR_BLUE);
        t.fillCircle(cx - 2, cy - 4, 1, W);
        t.drawLine(cx, cy + 4, cx + 2, cy + 8, W);
        break;
    case MeshMsg::Emote::PLANE:
        t.fillTriangle(cx + 9, cy - 4, cx - 8, cy - 1, cx - 3, cy + 5, W);
        t.drawLine(cx + 9, cy - 4, cx - 3, cy + 1, Theme::W95_SHADOW);
        break;
    case MeshMsg::Emote::PILLOW:
        t.fillRoundRect(cx - 9, cy - 5, 18, 11, 4, W);
        t.drawRoundRect(cx - 9, cy - 5, 18, 11, 4, Theme::W95_SHADOW);
        break;
    case MeshMsg::Emote::TICKLE:                        // a feather
        t.drawWideLine(cx - 7, cy + 7, cx + 6, cy - 7, 2, W);
        for (int i = 0; i < 4; i++) t.drawLine(cx - 3 + i * 3, cy + 3 - i * 3, cx - 7 + i * 3, cy - 1 - i * 3, W);
        break;
    case MeshMsg::Emote::GIFT:
        t.fillRect(cx - 7, cy - 5, 14, 12, P);
        t.fillRect(cx - 1, cy - 5, 2, 12, Y);
        t.fillRect(cx - 7, cy, 14, 2, Y);
        t.fillCircle(cx - 3, cy - 7, 2, Y);
        t.fillCircle(cx + 3, cy - 7, 2, Y);
        break;
    case MeshMsg::Emote::SNACK:                         // a slice
        t.fillTriangle(cx - 7, cy - 6, cx + 7, cy - 6, cx, cy + 8, Y);
        t.fillRect(cx - 7, cy - 8, 14, 3, Theme::AMBER);
        t.fillCircle(cx - 2, cy - 2, 1, Theme::RED);
        t.fillCircle(cx + 2, cy + 1, 1, Theme::RED);
        break;
    case MeshMsg::Emote::CHEERS:                        // two mugs
        for (int i = 0; i < 2; i++) {
            const int x = cx - 9 + i * 11;
            t.fillRect(x, cy - 3, 7, 9, Theme::AMBER);
            t.fillRect(x, cy - 5, 7, 3, W);
        }
        t.drawFastVLine(cx, cy - 9, 3, W);
        break;
    case MeshMsg::Emote::CONFETTI: {
        const uint16_t c[4] = { P, Theme::CYAN, Y, Theme::GREEN };
        static const int8_t px[8] = { -8, -3, 3, 8, -6, 0, 6, -1 }, py[8] = { -6, -8, -5, -7, 1, -1, 2, 6 };
        for (int i = 0; i < 8; i++) t.fillRect(cx + px[i], cy + py[i], 3, 2, c[i % 4]);
        break;
    }
    case MeshMsg::Emote::FIREWORKS:
        for (int i = 0; i < 8; i++) {
            const float a = (float)i * 0.785398f;
            t.drawLine(cx + (int)(cosf(a) * 3), cy + (int)(sinf(a) * 3),
                       cx + (int)(cosf(a) * 8), cy + (int)(sinf(a) * 8), (i & 1) ? P : Y);
        }
        break;
    case MeshMsg::Emote::HEART:
        heart(t, cx, cy, 6, Theme::RED);
        break;
    case MeshMsg::Emote::LAUGH:
        t.fillCircle(cx, cy, 7, Y);
        t.drawLine(cx - 4, cy - 2, cx - 2, cy - 4, K);
        t.drawLine(cx + 4, cy - 2, cx + 2, cy - 4, K);
        t.fillEllipse(cx, cy + 3, 4, 2, K);
        break;
    case MeshMsg::Emote::SAD:
        t.fillCircle(cx, cy, 7, Y);
        t.fillRect(cx - 3, cy - 3, 2, 2, K);
        t.fillRect(cx + 2, cy - 3, 2, 2, K);
        t.drawLine(cx - 3, cy + 4, cx, cy + 2, K);
        t.drawLine(cx, cy + 2, cx + 3, cy + 4, K);
        t.fillCircle(cx - 3, cy + 1, 1, Theme::CYAN);
        break;
    case MeshMsg::Emote::GRR:
        t.fillCircle(cx, cy, 7, Theme::RED);
        t.drawWideLine(cx - 5, cy - 4, cx - 1, cy - 2, 2, K);
        t.drawWideLine(cx + 5, cy - 4, cx + 1, cy - 2, 2, K);
        t.fillRect(cx - 3, cy + 2, 7, 2, W);
        break;
    case MeshMsg::Emote::SLEEPY:                        // Zz
        t.drawLine(cx - 7, cy - 5, cx + 1, cy - 5, Theme::CYAN);
        t.drawLine(cx + 1, cy - 5, cx - 7, cy + 3, Theme::CYAN);
        t.drawLine(cx - 7, cy + 3, cx + 1, cy + 3, Theme::CYAN);
        t.drawLine(cx + 3, cy, cx + 8, cy, Theme::CYAN);
        t.drawLine(cx + 8, cy, cx + 3, cy + 5, Theme::CYAN);
        t.drawLine(cx + 3, cy + 5, cx + 8, cy + 5, Theme::CYAN);
        break;
    case MeshMsg::Emote::TINFOIL:
        t.fillTriangle(cx - 8, cy + 6, cx + 8, cy + 6, cx + 2, cy - 8, Theme::W95_LIGHT);
        t.drawLine(cx - 3, cy + 3, cx + 1, cy - 4, W);
        t.drawLine(cx + 4, cy + 4, cx + 2, cy - 1, Theme::W95_SHADOW);
        break;
    case MeshMsg::Emote::CAMERA:
        t.fillRoundRect(cx - 8, cy - 5, 16, 11, 2, Theme::W95_LIGHT);
        t.fillCircle(cx, cy, 3, K);
        t.fillCircle(cx + 5, cy - 3, 1, Theme::RED);
        break;
    case MeshMsg::Emote::SPOTTED:                       // wide eyes, and a "!"
        t.fillEllipse(cx - 5, cy, 3, 4, W);
        t.fillEllipse(cx + 3, cy, 3, 4, W);
        t.fillRect(cx - 5, cy, 2, 2, K);
        t.fillRect(cx + 3, cy, 2, 2, K);
        t.fillRect(cx + 9, cy - 5, 2, 6, Y);
        t.fillRect(cx + 9, cy + 3, 2, 2, Y);
        break;
    case MeshMsg::Emote::HOWL:                          // a crescent moon
        t.fillCircle(cx, cy, 7, Y);
        t.fillCircle(cx + 4, cy - 2, 6, K);
        break;
    case MeshMsg::Emote::SELFIE:                        // a phone
        t.fillRoundRect(cx - 5, cy - 8, 11, 17, 2, Theme::W95_LIGHT);
        t.fillRect(cx - 3, cy - 6, 7, 11, Theme::CYAN);
        t.fillCircle(cx, cy + 7, 1, K);
        break;
    default: break;
    }
}

// Sends one, and has our own pair act it out. Shared by the tiles and RANDOM.
ComposeHit sendEmoteNow(MeshMsg::Emote e, uint32_t now, bool (*sent)(MeshTalk::Send)) {
    // Rolled HERE, once, and sent: both boards act out the same result.
    const uint8_t setup = EmoteScript::roll(e, esp_random(), (uint8_t)Squachy::lastCaught());
    if (!sent(MeshTalk::sendEmote((uint8_t)e, setup, now))) return ComposeHit::NONE;
    uiClearEmote((uint8_t)e, setup, false);
    s_emoteOn = false;
    return ComposeHit::SENT;
}

} // namespace

void uiMeshComposeInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_page    = 0;
    s_status  = nullptr;
    s_sel     = -1;
    s_typedOn = false;
    s_typed[0] = '\0';
    s_emoteOn = false;
    // Opening this screen is reading the message, so the bubble on the main
    // screen stops calling for attention.
    MeshTalk::markRead();
}

void uiMeshComposeSetTyped(const char* text) {
    size_t n = 0;
    if (text) for (; n < MeshMsg::TEXT_MAX && text[n]; n++) s_typed[n] = text[n];
    while (n && s_typed[n - 1] == ' ') n--;
    s_typed[n] = '\0';
    s_typedOn = MeshMsg::textParts(s_typed) > 0;
    s_emoteOn = false;
    s_sel     = -1;
    s_status  = nullptr;
}

const char* uiMeshComposeTyped() { return s_typed; }

void uiMeshComposeTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width(), h = t.height();
    const bool port = h > w;
    const bool tut = MeshTutor::active();

    Theme::Palette saved = Theme::dimPaletteForOverlay(150);
    Theme::drawActiveBackground(t, now, 0, h, eng);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 110);

    t.setTextWrap(false);
    t.setTextSize(2);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(8, 6);
    t.print("MESSAGE");

    // ---- the last one received, in red ------------------------------------
    // Two rows inside the same 22 pixels a canned line had to itself: a
    // typed message and its sender's name run past one row, and the list
    // below cannot move down to make room.
    const MeshTalk::Message& m = MeshTalk::inbox();
    const int bx = 4, by = 26, bw = w - 8, bh = 22;
    t.setTextSize(1);
    if (m.have) {
        char line[80];
        snprintf(line, sizeof line, "%s: %s", m.from, MeshTalk::lineText(m));
        t.fillRoundRect(bx, by, bw, bh, 4, Theme::RED);
        int maxW = bw - 12;
        if (maxW > 47 * t.textWidth("M")) maxW = 47 * t.textWidth("M");
        char rows[2][48];
        const uint8_t n = Theme::wrapText(t, line, maxW, rows, 2);
        t.setTextColor(Theme::WHITE, Theme::RED);
        for (uint8_t i = 0; i < n; i++) {
            t.setCursor(bx + 6, n == 1 ? by + (bh - 8) / 2 : by + 2 + i * 10);
            t.print(rows[i]);
        }
    } else {
        t.drawRoundRect(bx, by, bw, bh, 4, Theme::W95_SHADOW);
        t.setTextColor(Theme::W95_SHADOW, Theme::BG);
        t.setCursor(bx + 6, by + (bh - 8) / 2);
        t.print("No messages yet.");
    }

    // ---- the lines you can send, or the one you typed ------------------------
    // Two columns in landscape, one in portrait -- the longest line is twenty
    // characters, which is wider than half of 240.
    const int cols = port ? 1 : 2, rows = port ? 9 : 6;
    const uint8_t perPage = (uint8_t)(cols * rows);
    s_pages = (uint8_t)((MeshMsg::CANNED_N + perPage - 1) / perPage);
    if (s_page >= s_pages) s_page = 0;
    const int cw = (w - 8 - (cols - 1) * 6) / cols, ch = 20;
    const int y0 = by + bh + 6;
    s_lineN = 0;
    if (s_typedOn) {
        // The whole message, large, where the lines were -- read once more
        // before it goes, which is the point of asking.
        const int px = 4, py = y0, pw = w - 8, ph = (h - BH - 6 - 16) - y0;
        t.fillRect(px, py, pw, ph, Theme::BG);
        t.drawRect(px, py, pw, ph, Theme::VAPOR_PINK);
        t.setTextColor(Theme::W95_LIGHT, Theme::BG);
        t.setCursor(px + 6, py + 5);
        t.print("YOUR MESSAGE");
        t.setTextSize(2);
        char lines[4][48];
        const uint8_t n = Theme::wrapText(t, s_typed, pw - 12, lines, 4);
        t.setTextColor(Theme::WHITE, Theme::BG);
        for (uint8_t i = 0; i < n; i++) {
            t.setCursor(px + 6, py + 18 + i * 19);
            t.print(lines[i]);
        }
        t.setTextSize(1);
    } else if (s_emoteOn) {
        // A row of tabs, then the tab's six tiles where the lines were.
        const int TH = 18;
        const int tw = (w - 8 - (EmoteScript::TABS - 1) * 3) / EmoteScript::TABS;
        for (uint8_t i = 0; i < EmoteScript::TABS; i++) {
            const int x = 4 + i * (tw + 3);
            const bool on = i == s_tab;
            s_tabRect[i] = { (int16_t)x, (int16_t)y0, (int16_t)tw, (int16_t)TH };
            t.fillRect(x, y0, tw, TH, on ? Theme::PURPLE : Theme::BG);
            t.drawRect(x, y0, tw, TH, on ? Theme::VAPOR_PINK : Theme::W95_SHADOW);
            const char* nm = EmoteScript::TAB_NAME[i];
            t.setTextColor(on ? Theme::WHITE : Theme::W95_LIGHT, on ? Theme::PURPLE : Theme::BG);
            t.setCursor(x + (tw - t.textWidth(nm)) / 2, y0 + (TH - 8) / 2);
            t.print(nm);
        }
        const int ty0 = y0 + TH + 5;
        const uint8_t n = EmoteScript::PER_TAB;
        const int ecols = port ? 2 : 3, erows = (n + ecols - 1) / ecols;
        const int ew = (w - 8 - (ecols - 1) * 6) / ecols;
        int eh = ((h - BH - 6 - 16) - ty0 - (erows - 1) * 5) / erows;
        if (eh > 44) eh = 44;
        for (uint8_t i = 0; i < n; i++) {
            const MeshMsg::Emote e = EmoteScript::atTab(s_tab, i);
            const int x = 4 + (i % ecols) * (ew + 6), y = ty0 + (i / ecols) * (eh + 5);
            s_emoteRect[i] = { (int16_t)x, (int16_t)y, (int16_t)ew, (int16_t)eh };
            t.fillRect(x, y, ew, eh, Theme::BG);
            t.drawRect(x, y, ew, eh, Theme::VAPOR_YELLOW);
            drawEmoteIcon(t, (uint8_t)e, x + 15, y + eh / 2);
            const char* sub = EmoteScript::sub(e);
            const bool two = sub[0] != '\0';
            t.setTextColor(Theme::WHITE, Theme::BG);
            t.setCursor(x + 30, y + eh / 2 - (two ? 9 : 4));
            t.print(EmoteScript::name(e));
            if (two) {
                t.setCursor(x + 30, y + eh / 2 + 1);
                t.print(sub);
            }
        }
    } else {
        for (int i = 0; i < perPage; i++) {
            const int idx = s_page * perPage + i;
            if (idx >= MeshMsg::CANNED_N) break;
            const int x = 4 + (i % cols) * (cw + 6), y = y0 + (i / cols) * (ch + 3);
            s_lineRect[s_lineN] = { (int16_t)x, (int16_t)y, (int16_t)cw, (int16_t)ch };
            s_lineIdx[s_lineN]  = (uint8_t)idx;
            s_lineN++;
            const bool sel = idx == s_sel;
            const uint16_t bg = sel ? Theme::PURPLE : Theme::BG;
            t.fillRect(x, y, cw, ch, bg);
            t.drawRect(x, y, cw, ch, sel ? Theme::VAPOR_PINK : Theme::CYAN);
            t.setTextColor(Theme::WHITE, bg);
            t.setCursor(x + 6, y + (ch - 8) / 2);
            t.print(MeshMsg::CANNED[idx]);
        }
    }

    // ---- what state you are in --------------------------------------------
    const char* st = s_status;
    uint16_t sc = s_statusCol;
    if (!st && s_typedOn) {
        st = "Send it?";
        sc = Theme::VAPOR_YELLOW;
    } else if (!st && s_sel >= 0) {
        // Quoted back in full, so what is about to go out is read once more
        // before it does.
        snprintf(s_confirm, sizeof s_confirm, "Send \"%s\"?", MeshMsg::CANNED[s_sel]);
        st = s_confirm;
        sc = Theme::VAPOR_YELLOW;
    } else if (!st && s_emoteOn) {
        st = cannotSend();
        sc = Theme::AMBER;
        if (!st) {
            const bool here = uiClearGuest() != nullptr;
            st = here ? EMOTE_HINT : NOBODY_HERE;
            sc = here ? Theme::VAPOR_YELLOW : Theme::AMBER;
        }
    } else if (!st && !tut) {
        // Not during the tutorial, which runs before there is a phrase and
        // would otherwise open with a warning about not having one.
        st = cannotSend();
        sc = Theme::AMBER;
        if (!st && MeshTalk::sending(now)) { st = SENDING; sc = Theme::GREEN; }
    }
    if (st) {
        t.setTextColor(sc, Theme::BG);
        t.setCursor(8, h - BH - 6 - 12);
        t.print(st);
    }

    // ---- chrome -------------------------------------------------------------
    // While something waits for confirmation the row becomes the way back and
    // SEND -- at opposite ends, so the one cannot be hit reaching for the
    // other -- and the page arrows go, so the choice cannot scroll out of
    // sight. A typed message's way back is EDIT: to the keyboard, text kept.
    s_back = { 4, (int16_t)(h - BH - 6), BW, BH };
    s_send = { 0, 0, 0, 0 };
    s_help = { 0, 0, 0, 0 };
    s_type = { 0, 0, 0, 0 };
    s_emoteBtn = { 0, 0, 0, 0 };
    s_random   = { 0, 0, 0, 0 };
    if (s_typedOn || s_sel >= 0) {
        s_send = { (int16_t)(w - 4 - BW), (int16_t)(h - BH - 6), BW, BH };
        Theme::drawButton(t, s_back.x, s_back.y, s_back.w, s_back.h,
                          s_typedOn ? "[ EDIT ]" : "[ CANCEL ]", false);
        Theme::drawButton(t, s_send.x, s_send.y, s_send.w, s_send.h, "[ SEND ]", false);
    } else if (s_emoteOn) {
        // Back to the lines, not off the screen; the smiley, lit, does the same.
        Theme::drawButton(t, s_back.x, s_back.y, s_back.w, s_back.h, "[ LINES ]", false);
        s_random = { (int16_t)(w - 4 - BW - 8), (int16_t)(h - BH - 6), (int16_t)(BW + 8), BH };
        Theme::drawButton(t, s_random.x, s_random.y, s_random.w, s_random.h, "[ RANDOM ]", false);
        s_emoteBtn = { (int16_t)(w - 4 - 30), 2, 30, 20 };
        Theme::drawButton(t, s_emoteBtn.x, s_emoteBtn.y, s_emoteBtn.w, s_emoteBtn.h, "", true);
        drawSmiley(t, s_emoteBtn.x + 15, s_emoteBtn.y + 10);
    } else {
        Theme::drawButton(t, s_back.x, s_back.y, s_back.w, s_back.h, "[ BACK ]", false);
        int right = w - 4;                  // where the page arrows start
        if (s_pages > 1) {
            s_prev = { (int16_t)(w - 4 - 2 * AW - 6), (int16_t)(h - BH - 6), AW, BH };
            s_next = { (int16_t)(w - 4 - AW),         (int16_t)(h - BH - 6), AW, BH };
            Theme::drawButton(t, s_prev.x, s_prev.y, s_prev.w, s_prev.h, "<", false);
            Theme::drawButton(t, s_next.x, s_next.y, s_next.w, s_next.h, ">", false);
            right = s_prev.x;
        }
        // TYPE between BACK and the arrows, where the page count used to sit;
        // the count moved up beside "?".
        const int mid = (s_back.x + s_back.w + right) / 2;
        s_type = { (int16_t)(mid - TW / 2), (int16_t)(h - BH - 6), TW, BH };
        Theme::drawButton(t, s_type.x, s_type.y, s_type.w, s_type.h, "[ TYPE ]", false);
        // "?" replays the tutorial. Top right, level with the title, where
        // nothing else on this screen can be reached for by mistake.
        int pgRight = w - 4;
        if (!tut) {
            s_help = { (int16_t)(w - 4 - 30), 2, 30, 20 };
            Theme::drawButton(t, s_help.x, s_help.y, s_help.w, s_help.h, "?", false);
            // The emotes, beside it: a smiley, since that is what they are.
            s_emoteBtn = { (int16_t)(s_help.x - 6 - 30), 2, 30, 20 };
            Theme::drawButton(t, s_emoteBtn.x, s_emoteBtn.y, s_emoteBtn.w, s_emoteBtn.h, "", false);
            drawSmiley(t, s_emoteBtn.x + 15, s_emoteBtn.y + 10);
            pgRight = s_emoteBtn.x - 6;
        }
        if (s_pages > 1) {
            char pg[8];
            snprintf(pg, sizeof pg, "%u/%u", (unsigned)(s_page + 1), (unsigned)s_pages);
            t.setTextColor(Theme::W95_LIGHT, Theme::BG);
            t.setCursor(pgRight - t.textWidth(pg), 8);
            t.print(pg);
        }
    }

    // ---- the tutorial, over everything ------------------------------------
    if (tut) {
        const MeshTutor::Step ts = MeshTutor::step();
        if (ts == MeshTutor::Step::PICK_LINE && s_lineN) {
            // The first line, with the arrow inside its own box so it lands
            // on nothing else in either rotation.
            const Rect& r = s_lineRect[0];
            MeshTutor::drawFrame(t, now, r.x, r.y, r.w, r.h);
            MeshTutor::drawArrow(t, now, r.x + r.w - 28, r.y + r.h / 2, MeshTutor::Dir::LEFT);
        } else if (ts == MeshTutor::Step::PRESS_SEND && s_sel >= 0) {
            MeshTutor::drawFrame(t, now, s_send.x, s_send.y, s_send.w, s_send.h);
            MeshTutor::drawArrow(t, now, s_send.x - 4, s_send.y + s_send.h / 2, MeshTutor::Dir::RIGHT);
        }
        MeshTutor::drawCard(t, true);
    }
}

ComposeHit uiMeshComposeTouch(int x, int y, uint32_t now) {
    if (MeshTutor::active()) return tutorTouch(x, y);

    auto sent = [](MeshTalk::Send r) {
        switch (r) {
            case MeshTalk::Send::OK:           return true;
            case MeshTalk::Send::NOT_READY:    s_status = NEED_PHRASE;  s_statusCol = Theme::AMBER; break;
            case MeshTalk::Send::TRANSMIT_OFF: s_status = TRANSMIT_OFF; s_statusCol = Theme::AMBER; break;
            default:                           s_status = SEND_FAILED;  s_statusCol = Theme::RED;   break;
        }
        return false;
    };

    if (s_typedOn) {
        if (inRect(s_back, x, y)) return ComposeHit::TYPE;          // EDIT
        if (inRect(s_send, x, y)) {
            if (!sent(MeshTalk::sendText(s_typed, now))) return ComposeHit::NONE;
            s_typedOn = false;
            s_typed[0] = '\0';
            return ComposeHit::SENT;
        }
        return ComposeHit::NONE;
    }
    if (s_sel >= 0) {
        if (inRect(s_back, x, y)) { s_sel = -1; return ComposeHit::NONE; }
        if (inRect(s_send, x, y)) {
            const uint8_t line = (uint8_t)s_sel;
            s_sel = -1;
            return sent(MeshTalk::send(line, now)) ? ComposeHit::SENT : ComposeHit::NONE;
        }
        // Anything else falls through: another line changes the choice.
    } else {
        if (inRect(s_emoteBtn, x, y)) {
            s_emoteOn = !s_emoteOn;
            s_status  = nullptr;
            return ComposeHit::NONE;
        }
        if (s_emoteOn) {
            if (inRect(s_back, x, y)) { s_emoteOn = false; s_status = nullptr; return ComposeHit::NONE; }
            for (uint8_t i = 0; i < EmoteScript::TABS; i++)
                if (inRect(s_tabRect[i], x, y)) { s_tab = i; s_status = nullptr; return ComposeHit::NONE; }
            int pick = -1;
            if (inRect(s_random, x, y)) pick = (int)(esp_random() % (uint32_t)MeshMsg::Emote::COUNT);
            for (uint8_t i = 0; i < EmoteScript::PER_TAB && pick < 0; i++)
                if (inRect(s_emoteRect[i], x, y)) pick = (int)EmoteScript::atTab(s_tab, i);
            if (pick < 0) return ComposeHit::NONE;
            if (const char* why = cannotSend()) { s_status = why; s_statusCol = Theme::AMBER; return ComposeHit::NONE; }
            if (!uiClearGuest())            { s_status = NOBODY_HERE;  s_statusCol = Theme::AMBER; return ComposeHit::NONE; }
            // An emote would take over the scan response and cut the
            // message short -- the one thing that should not happen.
            if (MeshTalk::sendingMessage(now)) { s_status = STILL_ON_AIR; s_statusCol = Theme::AMBER; return ComposeHit::NONE; }
            return sendEmoteNow((MeshMsg::Emote)pick, now, sent);
        }
        if (inRect(s_help, x, y)) return ComposeHit::HELP;
        if (inRect(s_back, x, y)) return ComposeHit::BACK;
        if (inRect(s_type, x, y)) {
            if (const char* why = cannotSend()) { s_status = why; s_statusCol = Theme::AMBER; return ComposeHit::NONE; }
            s_typed[0] = '\0';
            return ComposeHit::TYPE;
        }
        if (s_pages > 1 && inRect(s_prev, x, y)) {
            s_page = (uint8_t)((s_page + s_pages - 1) % s_pages);
            s_status = nullptr;
            return ComposeHit::NONE;
        }
        if (s_pages > 1 && inRect(s_next, x, y)) {
            s_page = (uint8_t)((s_page + 1) % s_pages);
            s_status = nullptr;
            return ComposeHit::NONE;
        }
    }
    for (uint8_t i = 0; i < s_lineN; i++) {
        if (!inRect(s_lineRect[i], x, y)) continue;
        if (const char* why = cannotSend()) { s_sel = -1; s_status = why; s_statusCol = Theme::AMBER; }
        else                                { s_sel = (int8_t)s_lineIdx[i]; s_status = nullptr; }
        return ComposeHit::NONE;
    }
    return ComposeHit::NONE;
}

#endif // SQUACH_MESH
