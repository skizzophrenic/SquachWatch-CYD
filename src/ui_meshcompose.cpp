// SquachWatch-CYD — the SquachMesh message screen. See include/ui_meshcompose.h.
#include "ui_meshcompose.h"

#if SQUACH_MESH
#include "theme.h"
#include "meshtalk.h"
#include "meshmsg.h"
#include "settings.h"
#include "detection.h"
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
const char* s_status = nullptr;
uint16_t    s_statusCol = 0;

const int BW = 68, BH = 26, AW = 44;

// Held to 36 characters. Portrait is 240 wide and these start 8 in, so 38 is
// the most that fits; the first versions ran to 41 and lost their last words
// off the edge -- "you can read, not repl" -- which the emulator caught.
const char* const NEED_PHRASE  = "Set a phrase: SQUACHMESH > PHRASE.";
const char* const TRANSMIT_OFF = "TRANSMIT off: can read, not reply.";
const char* const SENDING      = "Sending, for thirty seconds.";
const char* const SEND_FAILED  = "Could not send. Try again.";

} // namespace

void uiMeshComposeInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_page   = 0;
    s_status = nullptr;
    // Opening this screen is reading the message, so the bubble on the main
    // screen stops calling for attention.
    MeshTalk::markRead();
}

void uiMeshComposeTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width(), h = t.height();
    const bool port = h > w;

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
    const MeshTalk::Message& m = MeshTalk::inbox();
    const int bx = 4, by = 26, bw = w - 8, bh = 22;
    t.setTextSize(1);
    if (m.have) {
        char line[64];
        snprintf(line, sizeof line, "%s: %s", m.from, MeshTalk::lineText(m));
        t.fillRoundRect(bx, by, bw, bh, 4, Theme::RED);
        t.setTextColor(Theme::WHITE, Theme::RED);
        t.setCursor(bx + 6, by + (bh - 8) / 2);
        t.print(line);
    } else {
        t.drawRoundRect(bx, by, bw, bh, 4, Theme::W95_SHADOW);
        t.setTextColor(Theme::W95_SHADOW, Theme::BG);
        t.setCursor(bx + 6, by + (bh - 8) / 2);
        t.print("No messages yet.");
    }

    // ---- the lines you can send -------------------------------------------
    // Two columns in landscape, one in portrait -- the longest line is twenty
    // characters, which is wider than half of 240.
    const int cols = port ? 1 : 2, rows = port ? 9 : 6;
    const uint8_t perPage = (uint8_t)(cols * rows);
    s_pages = (uint8_t)((MeshMsg::CANNED_N + perPage - 1) / perPage);
    if (s_page >= s_pages) s_page = 0;
    const int cw = (w - 8 - (cols - 1) * 6) / cols, ch = 20;
    const int y0 = by + bh + 6;
    s_lineN = 0;
    for (int i = 0; i < perPage; i++) {
        const int idx = s_page * perPage + i;
        if (idx >= MeshMsg::CANNED_N) break;
        const int x = 4 + (i % cols) * (cw + 6), y = y0 + (i / cols) * (ch + 3);
        s_lineRect[s_lineN] = { (int16_t)x, (int16_t)y, (int16_t)cw, (int16_t)ch };
        s_lineIdx[s_lineN]  = (uint8_t)idx;
        s_lineN++;
        t.fillRect(x, y, cw, ch, Theme::BG);
        t.drawRect(x, y, cw, ch, Theme::CYAN);
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(x + 6, y + (ch - 8) / 2);
        t.print(MeshMsg::CANNED[idx]);
    }

    // ---- what state you are in --------------------------------------------
    const char* st = s_status;
    uint16_t sc = s_statusCol;
    if (!st) {
        if (!MeshTalk::ready())             { st = NEED_PHRASE;  sc = Theme::AMBER; }
        else if (!Settings::meshTransmit()) { st = TRANSMIT_OFF; sc = Theme::AMBER; }
        else if (MeshTalk::sending(now))    { st = SENDING;      sc = Theme::GREEN; }
    }
    if (st) {
        t.setTextColor(sc, Theme::BG);
        t.setCursor(8, h - BH - 6 - 12);
        t.print(st);
    }

    // ---- chrome -------------------------------------------------------------
    s_back = { 4, (int16_t)(h - BH - 6), BW, BH };
    Theme::drawButton(t, s_back.x, s_back.y, s_back.w, s_back.h, "[ BACK ]", false);
    if (s_pages > 1) {
        s_prev = { (int16_t)(w - 4 - 2 * AW - 6), (int16_t)(h - BH - 6), AW, BH };
        s_next = { (int16_t)(w - 4 - AW),         (int16_t)(h - BH - 6), AW, BH };
        Theme::drawButton(t, s_prev.x, s_prev.y, s_prev.w, s_prev.h, "<", false);
        Theme::drawButton(t, s_next.x, s_next.y, s_next.w, s_next.h, ">", false);
        char pg[8];
        snprintf(pg, sizeof pg, "%u/%u", (unsigned)(s_page + 1), (unsigned)s_pages);
        t.setTextColor(Theme::W95_LIGHT, Theme::BG);
        const int mid = (s_back.x + s_back.w + s_prev.x) / 2;
        t.setCursor(mid - t.textWidth(pg) / 2, s_back.y + (BH - 8) / 2);
        t.print(pg);
    }
}

ComposeHit uiMeshComposeTouch(int x, int y, uint32_t now) {
    if (inRect(s_back, x, y)) return ComposeHit::BACK;
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
    for (uint8_t i = 0; i < s_lineN; i++) {
        if (!inRect(s_lineRect[i], x, y)) continue;
        switch (MeshTalk::send(s_lineIdx[i], now)) {
            case MeshTalk::Send::OK:           return ComposeHit::SENT;
            case MeshTalk::Send::NOT_READY:    s_status = NEED_PHRASE;  s_statusCol = Theme::AMBER; break;
            case MeshTalk::Send::TRANSMIT_OFF: s_status = TRANSMIT_OFF; s_statusCol = Theme::AMBER; break;
            default:                           s_status = SEND_FAILED;  s_statusCol = Theme::RED;   break;
        }
        return ComposeHit::NONE;
    }
    return ComposeHit::NONE;
}

#endif // SQUACH_MESH
