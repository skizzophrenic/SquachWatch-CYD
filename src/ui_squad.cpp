// SquachWatch-CYD — the SQUAD screen. See include/ui_squad.h.
#include "ui_squad.h"

#if SQUACH_MESH
#include "theme.h"
#include "squachy.h"
#include "squachmesh.h"
#include "meshtalk.h"
#include "detection.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

struct Rect { int16_t x, y, w, h; };
inline bool in(const Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

const uint8_t SQUAD_MAX = 8, ROWS_MAX = 4;
Mesh::SquadMember s_members[SQUAD_MAX];
uint8_t  s_n = 0, s_sel = 0;
// Who is showing, kept by ADDRESS: the list is re-read every frame and can
// reorder or shrink as boards come and go, and the carousel must not jump to
// somebody else under your finger when it does.
uint8_t  s_selMac[6] = { 0 };
bool     s_haveSel = false;
uint8_t  s_unreadAtOpen = 0;   // how many to mark as new, counted before reading
Rect     s_prev = { 0, 0, 0, 0 }, s_next = { 0, 0, 0, 0 };
Rect     s_invite = { 0, 0, 0, 0 }, s_back = { 0, 0, 0, 0 };
Rect     s_rows[ROWS_MAX];
uint8_t  s_rowN = 0;

const int BW = 68, BH = 26;
const float SCALE = 1.5f;

const char* memberName(const SquachMesh::Peer& p) {
    return (p.custom && p.name[0]) ? p.name : Squachy::nicknameAt(p.nick);
}

bool visiting(const uint8_t* mac) {
    return Mesh::peer() && memcmp(Mesh::peerMac(), mac, 6) == 0;
}

void centred(TFT_eSPI& t, const char* s, int cx, int y) {
    t.setCursor(cx - t.textWidth(s) / 2, y);
    t.print(s);
}

void select(uint8_t i) {
    s_sel = i;
    memcpy(s_selMac, s_members[i].mac, 6);
    s_haveSel = true;
}

}  // namespace

void uiSquadInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_haveSel = false;
    s_unreadAtOpen = 0;
    const MeshTalk::Message& last = MeshTalk::inbox();
    if (last.have && last.unread) s_unreadAtOpen = 1;
    // Opening the inbox is reading it, the same as the message screen.
    MeshTalk::markRead();
}

void uiSquadTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    const int w = t.width(), h = t.height();
    const bool port = h > w;

    s_n = Mesh::squadList(now, s_members, SQUAD_MAX);
    if (s_n) {
        int found = -1;
        for (uint8_t i = 0; i < s_n && s_haveSel; i++)
            if (!memcmp(s_members[i].mac, s_selMac, 6)) { found = i; break; }
        // First look: whoever is visiting, so the screen opens on a face
        // already on the main screen.
        if (found < 0 && !s_haveSel)
            for (uint8_t i = 0; i < s_n; i++) if (visiting(s_members[i].mac)) { found = i; break; }
        select(found < 0 ? 0 : (uint8_t)found);
    }

    Theme::Palette saved = Theme::dimPaletteForOverlay(150);
    Theme::drawActiveBackground(t, now, 0, h, eng);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 110);

    t.setTextWrap(false);
    t.setTextSize(2);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(8, 6);
    t.print("SQUAD");
    char cnt[20];
    snprintf(cnt, sizeof cnt, "%u IN RANGE", (unsigned)s_n);
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(80, 12);
    t.print(cnt);

    // ---- the carousel ---------------------------------------------------
    const int colW  = port ? w : 176;
    const int cx    = colW / 2;
    const int baseY = 146;
    s_prev = s_next = s_invite = { 0, 0, 0, 0 };
    if (s_n == 0) {
        t.setTextColor(Theme::W95_LIGHT, Theme::BG);
        centred(t, "Nobody in range", cx, 84);
        centred(t, "right now.", cx, 96);
    } else {
        const Mesh::SquadMember& m = s_members[s_sel];
        Squachy::setOutfitPreview((int8_t)m.peer.outfit);
        Squachy::setShadesPreview((int8_t)m.peer.shade);
        Squachy::drawWaving(t, cx, baseY, now, SCALE, nullptr, false, 0, true);
        Squachy::setShadesPreview(-1);
        Squachy::setOutfitPreview(-1);

        t.setTextSize(2);
        t.setTextColor(Theme::WHITE, Theme::BG);
        centred(t, memberName(m.peer), cx, baseY + 4);
        t.setTextSize(1);
        t.setTextColor(Theme::CYAN, Theme::BG);
        char sub[32];
        snprintf(sub, sizeof sub, "%s  %u/%u", Squachy::outfitNameAt(m.peer.outfit),
                 (unsigned)(s_sel + 1), (unsigned)s_n);
        centred(t, sub, cx, baseY + 22);

        if (s_n > 1) {
            s_prev = { 0, 60, 34, 70 };
            s_next = { (int16_t)(colW - 34), 60, 34, 70 };
            t.fillTriangle(22, 84, 8, 95, 22, 106, Theme::VAPOR_PINK);
            t.fillTriangle(colW - 22, 84, colW - 8, 95, colW - 22, 106, Theme::CYAN);
        }
        const bool vis = visiting(m.mac);
        s_invite = { (int16_t)(cx - 46), (int16_t)(baseY + 34), 92, 22 };
        Theme::drawButton(t, s_invite.x, s_invite.y, s_invite.w, s_invite.h,
                          vis ? "VISITING" : "[ INVITE ]", vis);
    }

    // ---- the inbox ------------------------------------------------------
    const int ix = port ? 4 : colW + 2;
    const int iy = port ? baseY + 62 : 28;
    const int iw = port ? w - 8 : w - colW - 6;
    const int ih = (h - BH - 12) - iy;
    t.fillRect(ix, iy, iw, ih, Theme::BG);
    t.drawRect(ix, iy, iw, ih, Theme::PURPLE);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(ix + 4, iy + 3);
    t.print("INBOX");

    const uint8_t have = MeshTalk::inboxCount();
    const int rowH = 32;
    uint8_t rows = (uint8_t)((ih - 14) / rowH);
    if (rows > ROWS_MAX) rows = ROWS_MAX;
    s_rowN = 0;
    if (!have) {
        t.setTextColor(Theme::W95_SHADOW, Theme::BG);
        t.setCursor(ix + 4, iy + 18);
        t.print("No messages yet.");
    }
    for (uint8_t i = 0; i < have && i < rows; i++) {
        const MeshTalk::Message& msg = MeshTalk::inboxAt(i);
        const int y = iy + 14 + i * rowH;
        s_rows[s_rowN++] = { (int16_t)ix, (int16_t)y, (int16_t)iw, (int16_t)(rowH - 2) };
        if (i) t.drawFastHLine(ix + 3, y - 2, iw - 6, Theme::W95_SHADOW);
        // Who, and how long ago -- red, as every real message is drawn.
        const bool fresh = i < s_unreadAtOpen;
        t.setTextColor(fresh ? Theme::RED : Theme::W95_LIGHT, Theme::BG);
        t.setCursor(ix + 4, y);
        t.print(msg.from);
        char ago[8];
        const uint32_t mins = (now - msg.at) / 60000u;
        if (mins == 0) snprintf(ago, sizeof ago, "now");
        else           snprintf(ago, sizeof ago, "%lum", (unsigned long)(mins > 999 ? 999 : mins));
        t.setCursor(ix + iw - 4 - t.textWidth(ago), y);
        t.print(ago);
        char lines[2][48];
        int maxW = iw - 8;
        if (maxW > 47 * t.textWidth("M")) maxW = 47 * t.textWidth("M");
        const uint8_t n = Theme::wrapText(t, MeshTalk::lineText(msg), maxW, lines, 2);
        t.setTextColor(Theme::WHITE, Theme::BG);
        for (uint8_t k = 0; k < n; k++) {
            t.setCursor(ix + 4, y + 10 + k * 9);
            t.print(lines[k]);
        }
    }

    s_back = { 4, (int16_t)(h - BH - 6), BW, BH };
    Theme::drawButton(t, s_back.x, s_back.y, s_back.w, s_back.h, "[ BACK ]", false);
}

SquadHit uiSquadTouch(int x, int y, uint32_t now) {
    (void)now;
    if (in(s_back, x, y)) return SquadHit::BACK;
    if (s_n > 1 && in(s_prev, x, y)) { select((uint8_t)((s_sel + s_n - 1) % s_n)); return SquadHit::NONE; }
    if (s_n > 1 && in(s_next, x, y)) { select((uint8_t)((s_sel + 1) % s_n)); return SquadHit::NONE; }
    if (s_n && in(s_invite, x, y) && !visiting(s_members[s_sel].mac)) {
        Mesh::preferPeer(s_members[s_sel].mac);
        return SquadHit::INVITED;
    }
    for (uint8_t i = 0; i < s_rowN; i++)
        if (in(s_rows[i], x, y)) return SquadHit::REPLY;
    return SquadHit::NONE;
}

#endif // SQUACH_MESH
