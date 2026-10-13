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

const uint8_t SQUAD_MAX = MeshTalk::ROSTER_N, ROWS_MAX = 4;
Mesh::SquadMember s_members[SQUAD_MAX];
bool     s_here[SQUAD_MAX];       // roster mode: in range right now
uint16_t s_met[SQUAD_MAX];        // roster mode: how many times
uint8_t  s_n = 0, s_sel = 0;
bool     s_roster = false;
uint32_t s_sureUntil = 0;         // FORGET asks once; a second tap within this does it
// Who is showing, kept by ADDRESS: the list is re-read every frame and can
// reorder or shrink as boards come and go, and the carousel must not jump to
// somebody else under your finger when it does.
uint8_t  s_selMac[6] = { 0 };
bool     s_haveSel = false;
uint8_t  s_unreadAtOpen = 0;   // how many to mark as new, counted before reading
Rect     s_prev = { 0, 0, 0, 0 }, s_next = { 0, 0, 0, 0 };
Rect     s_invite = { 0, 0, 0, 0 }, s_add = { 0, 0, 0, 0 }, s_back = { 0, 0, 0, 0 };
Rect     s_forget = { 0, 0, 0, 0 }, s_hunt = { 0, 0, 0, 0 };
Rect     s_send = { 0, 0, 0, 0 };
Rect     s_rows[ROWS_MAX];
uint8_t  s_rowN = 0;

// The corner buttons, a size up on the large panels so their labels can be.
int16_t btnW() { return Theme::large() ? 96 : 68; }
int16_t btnH() { return Theme::large() ? 34 : 26; }
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
    if (s_sel != i) s_sureUntil = 0;
    s_sel = i;
    memcpy(s_selMac, s_members[i].mac, 6);
    s_haveSel = true;
}

// The roster, with whoever is in range marked and wearing what their advert
// says today rather than what it said last time. Those here come first,
// then the rest by how often they have been met, so the order only moves
// when somebody arrives or leaves.
uint8_t rosterList(uint32_t now) {
    Mesh::SquadMember near[8];
    const uint8_t nn = Mesh::squadList(now, near, 8);
    const uint8_t n = MeshTalk::rosterCount();
    for (uint8_t i = 0; i < n; i++) {
        const MeshTalk::Member& m = MeshTalk::rosterAt(i);
        memcpy(s_members[i].mac, m.mac, 6);
        s_members[i].peer = m.look;
        s_members[i].seen = 0;
        s_met[i]  = m.met;
        s_here[i] = false;
        for (uint8_t k = 0; k < nn; k++)
            if (memcmp(near[k].mac, m.mac, 6) == 0) { s_here[i] = true; s_members[i].peer = near[k].peer; break; }
    }
    for (uint8_t i = 1; i < n; i++)
        for (uint8_t j = i; j > 0; j--) {
            const bool before = (s_here[j] && !s_here[j - 1]) ||
                                (s_here[j] == s_here[j - 1] && s_met[j] > s_met[j - 1]);
            if (!before) break;
            Mesh::SquadMember tm = s_members[j]; s_members[j] = s_members[j - 1]; s_members[j - 1] = tm;
            const bool th = s_here[j]; s_here[j] = s_here[j - 1]; s_here[j - 1] = th;
            const uint16_t tk = s_met[j]; s_met[j] = s_met[j - 1]; s_met[j - 1] = tk;
        }
    return n;
}

}  // namespace

bool uiSquadRosterMode() { return s_roster; }

void uiSquadInit(TFT_eSPI& t, bool roster) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_roster   = roster;
    s_sureUntil = 0;
    s_haveSel = false;
    s_unreadAtOpen = 0;
    const MeshTalk::Message& last = MeshTalk::inbox();
    if (last.have && last.unread) s_unreadAtOpen = 1;
    // Opening the inbox is reading it, the same as the message screen.
    MeshTalk::markRead();
}

void uiSquadTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    const int w = t.width(), h = t.height();
    const bool port = h > w;

    s_n = s_roster ? rosterList(now) : Mesh::squadList(now, s_members, SQUAD_MAX);
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
    Theme::drawActiveBackground(t, now, 0, h, eng, advance);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 110);

    t.setTextWrap(false);
    t.setTextSize(2);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(8, 6);
    t.print("SQUAD");
    char cnt[24];
    if (s_roster) {
        uint8_t here = 0;
        for (uint8_t i = 0; i < s_n; i++) if (s_here[i]) here++;
        snprintf(cnt, sizeof cnt, "%u MEMBER%s, %u HERE", (unsigned)s_n, s_n == 1 ? "" : "S", (unsigned)here);
    } else {
        snprintf(cnt, sizeof cnt, "%u IN RANGE", (unsigned)s_n);
    }
    Theme::bodyFontOn(t);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(80, Theme::large() ? 6 : 12);   // level with SQUAD
    t.print(cnt);
    Theme::bodyFontOff(t);

    // ---- the carousel ---------------------------------------------------
    const int colW  = port ? w : 176;
    const int cx    = colW / 2;
    const int baseY = 146;
    s_prev = s_next = s_invite = s_add = s_forget = s_hunt = { 0, 0, 0, 0 };
    if (s_n == 0) {
        t.setTextColor(Theme::W95_LIGHT, Theme::BG);
        centred(t, s_roster ? "Nobody in your squad" : "Nobody in range", cx, 84);
        centred(t, s_roster ? "yet. ADD one nearby." : "right now.", cx, 96);
    } else {
        const Mesh::SquadMember& m = s_members[s_sel];
        Squachy::setOutfitPreview((int8_t)m.peer.outfit);
        Squachy::setShadesPreview((int8_t)m.peer.shade);
        Squachy::setAuraPreview(m.peer.aura ? 1 : 0);
        Squachy::drawWaving(t, cx, baseY, now, SCALE, nullptr, false, 0, true);
        Squachy::setAuraPreview(-1);
        Squachy::setShadesPreview(-1);
        Squachy::setOutfitPreview(-1);

        t.setTextSize(2);
        t.setTextColor(Theme::WHITE, Theme::BG);
        centred(t, memberName(m.peer), cx, baseY + 4);
        Theme::bodyFontOn(t);
        const int subH = t.fontHeight();
        t.setTextColor(Theme::CYAN, Theme::BG);
        char sub[40];
        if (s_roster)
            snprintf(sub, sizeof sub, "%s  MET %ux  %u/%u", Squachy::outfitNameAt(m.peer.outfit),
                     (unsigned)s_met[s_sel], (unsigned)(s_sel + 1), (unsigned)s_n);
        else
            snprintf(sub, sizeof sub, "%s  %u/%u", Squachy::outfitNameAt(m.peer.outfit),
                     (unsigned)(s_sel + 1), (unsigned)s_n);
        centred(t, sub, cx, baseY + 22);
        Theme::bodyFontOff(t);

        if (s_n > 1) {
            s_prev = { 0, 60, 34, 70 };
            s_next = { (int16_t)(colW - 34), 60, 34, 70 };
            t.fillTriangle(22, 84, 8, 95, 22, 106, Theme::VAPOR_PINK);
            t.fillTriangle(colW - 22, 84, colW - 8, 95, colW - 22, 106, Theme::CYAN);
        }
        // Three buttons under the name, 56 wide each: INVITE brings this
        // Squachy onto the screen, HUNT aims the signal gauge at their board
        // -- a fox hunt, with them as the fox -- and ADD hands them the
        // phrase (FORGET on the roster). Each says its state when it cannot
        // be pressed: VISITING, HUNTING, MEMBER, AWAY.
        const bool vis  = visiting(m.mac);
        const bool here = !s_roster || s_here[s_sel];
        const bool hunt = eng.isHunted(m.mac, true);
        const int  by   = baseY + 26 + subH;   // under the line above: 34 in the system font
        s_invite = { (int16_t)(cx - 88), (int16_t)by, 56, 22 };
        s_hunt   = { (int16_t)(cx - 28), (int16_t)by, 56, 22 };
        s_add    = { (int16_t)(cx + 32), (int16_t)by, 56, 22 };
        Theme::drawButton(t, s_invite.x, s_invite.y, s_invite.w, s_invite.h,
                          !here ? "AWAY" : vis ? "VISITING" : "INVITE", vis || !here);
        if (!here) s_invite = { 0, 0, 0, 0 };
        Theme::drawButton(t, s_hunt.x, s_hunt.y, s_hunt.w, s_hunt.h, hunt ? "HUNTING" : "HUNT", hunt);
        if (s_roster) {
            // FORGET drops them from the roster, after asking once; they come
            // back the next time they are heard with the phrase.
            s_forget = s_add;
            s_add    = { 0, 0, 0, 0 };
            const bool sure = (int32_t)(s_sureUntil - now) > 0;
            Theme::drawButton(t, s_forget.x, s_forget.y, s_forget.w, s_forget.h,
                              sure ? "SURE?" : "FORGET", sure);
        } else if (MeshTalk::inSquad(m.mac, now)) {
            // Heard holding our phrase: a member, so nothing to add.
            Theme::drawButton(t, s_add.x, s_add.y, s_add.w, s_add.h, "MEMBER", true);
            s_add = { 0, 0, 0, 0 };
        } else {
            Theme::drawButton(t, s_add.x, s_add.y, s_add.w, s_add.h, "ADD", false);
        }
    }

    // ---- the inbox ------------------------------------------------------
    const int ix = port ? 4 : colW + 2;
    const int iy = port ? baseY + 62 : 28;
    const int iw = port ? w - 8 : w - colW - 6;
    const int ih = (h - btnH() - 12) - iy;
    t.fillRect(ix, iy, iw, ih, Theme::BG);
    t.drawRect(ix, iy, iw, ih, Theme::PURPLE);
    // The inbox in the body font: 16-row lines on the large panels, the
    // system font's 8 everywhere else, and every step below follows fh.
    Theme::bodyFontOn(t);
    const int fh = t.fontHeight();
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(ix + 4, iy + 3);
    t.print("INBOX");

    const uint8_t have = MeshTalk::inboxCount();
    const int head = fh + 6;                      // 14 in the system font
    const int rowH = 3 * (fh + 1) + 5;            // 32 in the system font
    uint8_t rows = (uint8_t)((ih - head) / rowH);
    if (rows > ROWS_MAX) rows = ROWS_MAX;
    s_rowN = 0;
    if (!have) {
        t.setTextColor(Theme::W95_SHADOW, Theme::BG);
        t.setCursor(ix + 4, iy + fh + 10);
        t.print("No messages yet.");
    }
    for (uint8_t i = 0; i < have && i < rows; i++) {
        const MeshTalk::Message& msg = MeshTalk::inboxAt(i);
        const int y = iy + head + i * rowH;
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
            t.setCursor(ix + 4, y + fh + 2 + k * (fh + 1));
            t.print(lines[k]);
        }
    }
    Theme::bodyFontOff(t);

    s_back = { 4, (int16_t)(h - btnH() - 6), btnW(), btnH() };
    Theme::drawButton(t, s_back.x, s_back.y, s_back.w, s_back.h, "[ BACK ]", false);
    // SEND, bottom right under the inbox: write to the squad from here,
    // rather than only by tapping a message to reply to it. The in-range
    // page only; the roster is for managing members, not talking to them.
    s_send = { 0, 0, 0, 0 };
    if (!s_roster) {
        s_send = { (int16_t)(w - btnW() - 4), (int16_t)(h - btnH() - 6), btnW(), btnH() };
        Theme::drawButton(t, s_send.x, s_send.y, s_send.w, s_send.h, "[ SEND ]", false);
    }
}

uint8_t uiSquadStops(int16_t* xs, int16_t* ys, int16_t* ws, int16_t* hs, uint8_t cap) {
    uint8_t n = 0;
    auto put = [&](const Rect& r) {
        if (r.w <= 0 || n >= cap) return;
        xs[n] = r.x; ys[n] = r.y; ws[n] = r.w; hs[n] = r.h; n++;
    };
    // The order a hand would go: the messages, the board showing and what to
    // do with it, then the two buttons along the bottom. Every rect is zeroed
    // by the draw unless it is shown, the same rule uiSquadTouch() relies on.
    for (uint8_t i = 0; i < s_rowN; i++) put(s_rows[i]);
    if (s_n > 1) { put(s_prev); put(s_next); }
    if (s_n) { put(s_invite); put(s_hunt); put(s_add); put(s_forget); }
    put(s_send);
    put(s_back);
    return n;
}

SquadHit uiSquadTouch(int x, int y, uint32_t now) {
    if (in(s_back, x, y)) return SquadHit::BACK;
    if (s_send.w && in(s_send, x, y)) return SquadHit::SEND;
    if (s_n > 1 && in(s_prev, x, y)) { select((uint8_t)((s_sel + s_n - 1) % s_n)); return SquadHit::NONE; }
    if (s_n > 1 && in(s_next, x, y)) { select((uint8_t)((s_sel + 1) % s_n)); return SquadHit::NONE; }
    if (s_n && s_invite.w && in(s_invite, x, y) && !visiting(s_members[s_sel].mac)) {
        Mesh::preferPeer(s_members[s_sel].mac);
        return SquadHit::INVITED;
    }
    if (s_n && s_forget.w && in(s_forget, x, y)) {
        if ((int32_t)(s_sureUntil - now) > 0) {
            MeshTalk::rosterForget(s_members[s_sel].mac);
            s_sureUntil = 0;
            s_haveSel   = false;
        } else {
            s_sureUntil = now + 3000;
        }
        return SquadHit::NONE;
    }
    if (s_n && s_add.w && in(s_add, x, y)) return SquadHit::ADD;
    if (s_n && s_hunt.w && in(s_hunt, x, y)) return SquadHit::HUNT;
    for (uint8_t i = 0; i < s_rowN; i++)
        if (in(s_rows[i], x, y)) return SquadHit::REPLY;
    return SquadHit::NONE;
}

const uint8_t* uiSquadSelectedMac()  { return s_n ? s_members[s_sel].mac : nullptr; }
const char*    uiSquadSelectedName() { return s_n ? memberName(s_members[s_sel].peer) : ""; }

#endif // SQUACH_MESH
