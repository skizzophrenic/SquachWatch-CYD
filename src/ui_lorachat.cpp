// SquachWatch-CYD — LORA CHATS. See include/ui_lorachat.h.
//
// The message ring lives in the sniffer, behind its lock, 256 deep and both
// networks mixed. This screen keeps its own copy of the newest few of the
// open tab and rebuilds it only when the ring has moved, so a quiet screen is
// not copying 70 KB out from under a mutex thirty times a second.
//
// Three tabs: MESHTASTIC and MESHCORE, the channels as before, and DMs
// (2026-10-23, the owner's pick): Meshtastic direct messages, a list of
// conversations, a tap opens one. A conversation's own messages carry a mark
// -- dots while it goes, a tick once their node ACKed it, a red cross when
// three tries brought nothing (include/lora_dm.h). A tap on somebody's line
// in the MESHTASTIC tab opens a conversation with them.
//
// SQUACH_LORA_FAKE: the PC emulator's one-shot renderer (the `lorachat`
// screen), off sim/lora_sim.cpp, with the T-Deck's compose line. No board
// defines it.
#if SQUACH_LORA || SQUACH_LORA_FAKE
#include "ui_lorachat.h"
#include "lora_sniffer.h"
#include "lora_meshtastic.h"
#include "lora_meshcore.h"
#include "lora_channels.h"
#include "lora_dm.h"
#include "settings.h"
#include "theme.h"
#include "privacy.h"
#if defined(TDECK_PLUS) || SQUACH_LORA_FAKE
#define LORACHAT_COMPOSE 1
#include "ui_lorasettings.h"   // uiLoraMyName(): what MeshCore shows before the colon
#else
#define LORACHAT_COMPOSE 0
#endif
#include <Arduino.h>
#include <string.h>

namespace {

struct Row {
    char     sender[24];
    char     chan[16];
    char     text[201];
    uint32_t ms;
    int16_t  rssi;
    uint32_t peer;       // DMs: the other node; a channel row: its sender's number (0 = ours)
    Lora::Proto proto;   // DMs: which network the conversation is on
    bool     mine;
    uint8_t  dmState;
    uint8_t  unread;     // the DMs list: that conversation's unread count
};

const uint8_t  KEEP  = 24;       // the newest this many of the open tab
Row            s_rows[KEEP];
uint8_t        s_rowN   = 0;
uint8_t        s_tab    = 0;     // 0 Meshtastic, 1 MeshCore, 2 DMs
uint32_t       s_peer   = 0;     // the DMs tab: the open conversation, 0 = the list
Lora::Proto    s_peerProto = Lora::Proto::MESHTASTIC;
int            s_scroll = 0;     // rows skipped from the newest
uint32_t       s_sig    = 0xFFFFFFFFu;
uint8_t        s_sigTab = 0xFF;
uint32_t       s_sigPeer = 0;
uint32_t       s_sigAt  = 0;
uint16_t       s_count[3] = { 0, 0, 0 };
uint16_t       s_dmUnread = 0;
// The ring's running total when the chats were last looked at; the unread
// count is what arrived since, for every tab.
uint32_t       s_seenTotal = 0;
// Where each drawn row is, for a tap: its top and bottom on screen.
int            s_rowTop[KEEP], s_rowBot[KEEP];
uint8_t        s_rowsDrawn = 0, s_rowsFrom = 0;
// The open conversation's header line: where "< ALL" was drawn.
int            s_headY = -100;

// When each conversation was last open, for its unread count. A small table:
// the oldest entry goes when it is full, and its node's old messages count
// as unread again -- a harmless way to be wrong.
struct Seen { uint32_t peer, ms; };
Seen           s_seen[16];

uint32_t seenMs(uint32_t peer) {
    for (auto& s : s_seen) if (s.peer == peer) return s.ms;
    return 0;
}
void markSeen(uint32_t peer, uint32_t now) {
    if (!peer) return;
    Seen* slot = nullptr;
    for (auto& s : s_seen) if (s.peer == peer) { slot = &s; break; }
    if (!slot) { slot = &s_seen[0]; for (auto& s : s_seen) if (s.ms < slot->ms) slot = &s; }
    slot->peer = peer; slot->ms = now ? now : 1;
}

const int TAB_H = 22;
#if LORACHAT_COMPOSE
// The compose line: what is typed, and a short status in its place after Enter.
const int COMPOSE_H = 22;
char     s_compose[161] = { 0 };
uint8_t  s_composeN = 0;
char     s_status[40] = { 0 };
uint32_t s_statusAt = 0;
// The channel chip at the compose line's right end, where it was drawn, for
// uiLoraChatHit; the longest name it shows before it cuts one and ends "..".
int      s_chipX = 0, s_chipY = -100, s_chipW = 0, s_chipH = 0;
const size_t CHIP_CHARS = 14;
#endif

uint16_t dmCol() { return Theme::VAPOR_YELLOW; }
uint16_t tabCol(uint8_t t) { return t == 2 ? dmCol() : t ? Theme::PINK : Theme::CYAN; }
Lora::Proto tabProto(uint8_t t) { return t == 1 ? Lora::Proto::MESHCORE : Lora::Proto::MESHTASTIC; }

uint32_t ringTotal() { return (uint32_t)Lora::msgCount() + Lora::msgDropped(); }

void chanName(Lora::Proto p, uint8_t idx, char* out, size_t cap) {
    if (p == Lora::Proto::MESHTASTIC) { Meshtastic::Channel c = Meshtastic::channel(idx); snprintf(out, cap, "%s", c.name[0] ? c.name : "?"); }
    else                              { MeshCore::Channel c = MeshCore::channel(idx);     snprintf(out, cap, "%s", c.name[0] ? c.name : "?"); }
}

// A DM on either network: Meshtastic's PKI ones and MeshCore's.
bool isDm(const Lora::Msgs::Msg& m) { return m.chan == Lora::Msgs::DM_CHAN; }
// A conversation's name in its network's colour, so two networks in one
// list stay told apart.
uint16_t netCol(Lora::Proto p) { return p == Lora::Proto::MESHCORE ? Theme::PINK : Theme::CYAN; }

// Walk the ring newest first, count every tab, and copy the open tab's
// newest KEEP. Only when something changed -- or, in a conversation, every
// half second, because an ACK moves a mark without moving the ring.
void refresh(uint32_t now) {
    const uint32_t sig = ringTotal();
    const bool stale = s_tab == 2 && now - s_sigAt > 500;
    if (sig == s_sig && s_sigTab == s_tab && s_sigPeer == s_peer && !stale) return;
    s_sig = sig; s_sigTab = s_tab; s_sigPeer = s_peer; s_sigAt = now;
    s_rowN = 0; s_count[0] = s_count[1] = s_count[2] = 0; s_dmUnread = 0;
    const uint16_t n = Lora::msgCount();
    Lora::Msgs::Msg m;
    for (uint16_t i = 0; i < n; i++) {
        if (!Lora::msgAt(i, m)) break;
        if (isDm(m)) {
            s_count[2]++;
            const bool unread = !m.mine && m.ms > seenMs(m.peer) && !(s_tab == 2 && s_peer == m.peer && s_peerProto == m.proto);
            if (unread) s_dmUnread++;
            if (s_tab != 2) continue;
            if (s_peer) {
                if (m.peer != s_peer || m.proto != s_peerProto || s_rowN >= KEEP) continue;
                Row& r = s_rows[s_rowN++];
                snprintf(r.sender, sizeof r.sender, "%s", m.mine ? "YOU" : (m.sender[0] ? m.sender : "?"));
                r.chan[0] = '\0';
                snprintf(r.text, sizeof r.text, "%s", m.text);
                r.ms = m.ms; r.rssi = m.rssi; r.peer = m.peer; r.mine = m.mine; r.dmState = m.dmState; r.unread = 0;
                r.proto = m.proto;
            } else {
                // The list: one row a conversation, its newest line.
                Row* r = nullptr;
                for (uint8_t k = 0; k < s_rowN; k++) if (s_rows[k].peer == m.peer && s_rows[k].proto == m.proto) { r = &s_rows[k]; break; }
                if (!r) {
                    if (s_rowN >= KEEP) continue;
                    r = &s_rows[s_rowN++];
                    memset(r, 0, sizeof *r);
                    r->peer = m.peer;
                    r->proto = m.proto;
                    if (m.mine) Lora::nodeName(m.proto, m.peer, r->sender, sizeof r->sender);
                    else        snprintf(r->sender, sizeof r->sender, "%s", m.sender[0] ? m.sender : "?");
                    snprintf(r->text, sizeof r->text, "%s%s", m.mine ? "you: " : "", m.text);
                    r->ms = m.ms; r->mine = m.mine; r->dmState = m.dmState;
                } else if (!m.mine && r->mine) {
                    // The newest line was ours; their name as they gave it.
                    snprintf(r->sender, sizeof r->sender, "%s", m.sender[0] ? m.sender : r->sender);
                }
                if (unread && r->unread < 99) r->unread++;
            }
            continue;
        }
        const uint8_t c = m.proto == Lora::Proto::MESHTASTIC ? 0 : m.proto == Lora::Proto::MESHCORE ? 1 : 0xFF;
        if (c > 1) continue;
        s_count[c]++;
        if (c != s_tab || s_rowN >= KEEP) continue;
        Row& r = s_rows[s_rowN++];
        snprintf(r.sender, sizeof r.sender, "%s", m.sender[0] ? m.sender : "?");
        chanName(m.proto, m.chan, r.chan, sizeof r.chan);
        snprintf(r.text, sizeof r.text, "%s", m.text);
        r.ms = m.ms;
        r.rssi = m.rssi;
        r.peer = m.proto == Lora::Proto::MESHTASTIC && strcmp(m.sender, "YOU") ? (uint32_t)m.senderId : 0;
        r.proto = m.proto;
        r.mine = false; r.dmState = 0; r.unread = 0;
    }
}

void age(uint32_t now, uint32_t ms, char* out, size_t cap) {
    const uint32_t s = (now - ms) / 1000;
    if (s < 60)         snprintf(out, cap, "%lus", (unsigned long)s);
    else if (s < 3600)  snprintf(out, cap, "%lum", (unsigned long)(s / 60));
    else if (s < 86400) snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
    else                snprintf(out, cap, "%lud", (unsigned long)(s / 86400));
}

// Word-wrap `text` into `maxW` pixels in the current font. Draws when
// `draw`, and returns the number of lines either way. A word too wide for a
// line on its own is cut where it runs out.
int wrap(TFT_eSPI& t, const char* text, int x, int y, int maxW, int lineH, int yMax, bool draw) {
    char line[64];
    int lines = 0;
    const char* p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        size_t len = 0, lastBreak = 0;
        const char* q = p;
        while (*q && len < sizeof line - 1) {
            line[len] = *q; line[len + 1] = '\0';
            if (t.textWidth(line) > maxW) break;
            if (*q == ' ') lastBreak = len;
            len++; q++;
        }
        if (*q && lastBreak && len < sizeof line - 1) len = lastBreak;
        if (!len) len = 1;
        line[len] = '\0';
        if (draw && y + lines * lineH + lineH <= yMax) {
            t.setCursor(x, y + lines * lineH + Theme::bubbleAscent());
            t.print(line);
        }
        lines++;
        p += len;
    }
    return lines ? lines : 1;
}

const char* listenLine() {
    switch (Settings::loraListen()) {
        case 0:  return "LORA IS OFF (WATCH SETTINGS > LORA)";
        case 1:  return "LISTENING: MESHTASTIC";
        case 2:  return "LISTENING: MESHCORE";
        default: return "LISTENING: BOTH";
    }
}

// A sent DM's mark, 10 x 8 at (x, y): three dots while it goes (or waits for
// their key), a tick once ACKed, a red cross when it failed.
void drawMark(TFT_eSPI& t, int x, int y, uint8_t st) {
    switch (st) {
        case Lora::Dm::ST_DELIVERED:
            for (int k = 0; k < 2; k++) {
                t.drawLine(x, y + 4 + k, x + 3, y + 7 + k, Theme::GREEN);
                t.drawLine(x + 3, y + 7 + k, x + 9, y + 1 + k, Theme::GREEN);
            }
            break;
        case Lora::Dm::ST_FAILED:
            for (int k = 0; k < 2; k++) {
                t.drawLine(x + 1 + k, y + 1, x + 7 + k, y + 7, Theme::RED);
                t.drawLine(x + 7 + k, y + 1, x + 1 + k, y + 7, Theme::RED);
            }
            break;
        case Lora::Dm::ST_SENDING: case Lora::Dm::ST_WAIT_KEY: {
            const uint16_t c = Theme::blend(Theme::BG, Theme::WHITE, 150);
            for (int k = 0; k < 3; k++) t.fillRect(x + k * 4, y + 5, 2, 2, c);
            break;
        }
        default: break;
    }
}

#if LORACHAT_COMPOSE
// The compose line, above the BACK bar: the typed text with a cursor, the
// tail of it when it is longer than the line; a status for a moment after
// Enter; the way to switch it on when LORA TX is off. At its right end, the
// chip: on a channel tab, the channel Enter sends on, outlined on the
// network's default, filled in its colour on any other, a tap steps it; in a
// conversation, PRIVATE, filled, which no tap changes.
void drawCompose(TFT_eSPI& t, int w, int y, uint32_t now) {
    const uint16_t accent = tabCol(s_tab);
    const uint16_t bg = Theme::blend(Theme::BG, Theme::WHITE, 18);
    t.fillRoundRect(4, y, w - 8, COMPOSE_H, 4, bg);
    t.drawRoundRect(4, y, w - 8, COMPOSE_H, 4, Theme::blend(Theme::BG, accent, 150));
    t.setTextSize(1);
    const int ty = y + (COMPOSE_H - t.fontHeight()) / 2;

    char chip[24];
    bool filled;
    if (s_tab == 2) {
        snprintf(chip, sizeof chip, "PRIVATE");
        filled = s_peer != 0;
    } else {
        const Lora::Proto p = tabProto(s_tab);
        Lora::Chan::txName(p, Lora::Chan::txPick(p), chip, sizeof chip);
        if (strlen(chip) > CHIP_CHARS) { chip[CHIP_CHARS - 2] = '.'; chip[CHIP_CHARS - 1] = '.'; chip[CHIP_CHARS] = '\0'; }
        filled = !Lora::Chan::txIsDefault(p);
    }
    s_chipH = COMPOSE_H - 6;
    s_chipW = t.textWidth(chip) + 12;
    if (s_chipW < 36) s_chipW = 36;
    s_chipX = w - 4 - 3 - s_chipW;
    s_chipY = y + 3;
    const uint16_t cbg = filled ? accent : Theme::blend(Theme::BG, accent, 40);
    t.fillRoundRect(s_chipX, s_chipY, s_chipW, s_chipH, 3, cbg);
    t.drawRoundRect(s_chipX, s_chipY, s_chipW, s_chipH, 3, accent);
    t.setTextColor(filled ? Theme::BG : accent, cbg);
    t.setCursor(s_chipX + (s_chipW - t.textWidth(chip)) / 2, ty);
    t.print(chip);

    // The text, kept left of the chip.
    const int maxW = s_chipX - 6 - 10;
    char line[168];
    const char* msg = nullptr;
    uint16_t col = Theme::WHITE;
    if (s_status[0] && now - s_statusAt < 2500) { msg = s_status; col = Theme::VAPOR_YELLOW; }
    else if (!Settings::loraTxDbm() && !s_composeN) { msg = "TYPE HERE  (LORA TX IS OFF: SETTINGS)"; col = Theme::blend(Theme::BG, Theme::WHITE, 120); }
    else if (s_tab == 2 && !s_peer) { msg = "TAP A NAME TO MESSAGE THEM"; col = Theme::blend(Theme::BG, accent, 170); }
    if (msg) {
        snprintf(line, sizeof line, "%s", msg);
        size_t n = strlen(line);
        while (n && t.textWidth(line) > maxW) line[--n] = '\0';
        t.setTextColor(col, bg);
        t.setCursor(10, ty); t.print(line);
        return;
    }
    snprintf(line, sizeof line, "%s_", s_compose);
    const char* q = line;
    while (*q && t.textWidth(q) > maxW) q++;           // the tail that fits
    t.setTextColor(Theme::WHITE, bg);
    t.setCursor(10, ty); t.print(q);
}
#endif

}  // namespace

void uiLoraChatInit(TFT_eSPI& t) {
    s_scroll = 0;
    s_sig = 0xFFFFFFFFu;
    s_seenTotal = ringTotal();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiLoraChatSelect(uint8_t tab) {
    if (tab > 2) tab = 2;
    if (tab != s_tab) { s_tab = tab; s_scroll = 0; }
    if (tab != 2) s_peer = 0;
}

void uiLoraChatOpenDm(uint32_t peer, Lora::Proto p) {
    s_tab = 2;
    s_peer = peer;
    s_peerProto = p;
    s_scroll = 0;
    s_sig = 0xFFFFFFFFu;
    markSeen(peer, millis());
}

uint32_t uiLoraChatDmSeenMs(uint32_t peer) { return seenMs(peer); }

uint16_t uiLoraChatDmUnread() {
    refresh(millis());
    return s_dmUnread;
}

void uiLoraChatScroll(int delta) {
    s_scroll += delta;
    if (s_scroll > (int)s_rowN - 1) s_scroll = (int)s_rowN - 1;
    if (s_scroll < 0) s_scroll = 0;
}

#if !SQUACH_LORA_FAKE   // the emulator's own drives the LORA page's badge
uint16_t uiLoraChatUnread() {
    const uint32_t tot = ringTotal();
    // The ring was cleared (LORA SETTINGS > CLEAR CHATS): start counting again.
    if (tot < s_seenTotal) s_seenTotal = tot;
    return (uint16_t)(tot > s_seenTotal ? (tot - s_seenTotal > 999 ? 999 : tot - s_seenTotal) : 0);
}
#endif

void uiLoraChatTick(TFT_eSPI& t, uint32_t now) {
    refresh(now);
    s_seenTotal = ringTotal();   // looking at it is reading it
    if (s_tab == 2 && s_peer) markSeen(s_peer, now);
    const int w = t.width(), h = t.height();
    int bodyBottom = h - Theme::pinnedBackH(w) - 2;
#if LORACHAT_COMPOSE
    bodyBottom -= COMPOSE_H + 4;
    drawCompose(t, w, bodyBottom + 2, now);
#endif
    t.fillRect(0, 0, w, bodyBottom, Theme::BG);
    s_rowsDrawn = 0; s_rowsFrom = (uint8_t)s_scroll; s_headY = -100;

    // The three tabs, the open one filled. A narrow screen drops the counts,
    // then the long names.
    const int tabY = Theme::LIST_TOP;
    const char* names[3] = { "MESHTASTIC", "MESHCORE", "DMs" };
    const char* shortNames[3] = { "MESHT", "MCORE", "DMs" };
    t.setTextSize(1);
    const int tw = (w - 8) / 3;
    for (uint8_t i = 0; i < 3; i++) {
        const int x0 = 4 + i * tw + (i ? 2 : 0), x1 = 4 + (i + 1) * tw - 2;
        const bool on = (i == s_tab);
        const uint16_t col = tabCol(i);
        if (on) t.fillRoundRect(x0, tabY, x1 - x0, TAB_H, 4, col);
        else    t.drawRoundRect(x0, tabY, x1 - x0, TAB_H, 4, col);
        char lab[24];
        const uint16_t cnt = i == 2 ? (s_dmUnread ? s_dmUnread : s_count[2]) : s_count[i];
        snprintf(lab, sizeof lab, "%s %u", names[i], (unsigned)cnt);
        if (t.textWidth(lab) > x1 - x0 - 4) snprintf(lab, sizeof lab, "%s", names[i]);
        if (t.textWidth(lab) > x1 - x0 - 4) snprintf(lab, sizeof lab, "%s", shortNames[i]);
        // Unread DMs: the tab's label in white on yellow even when it is shut.
        const bool hot = i == 2 && s_dmUnread && !on;
        if (hot) t.fillRoundRect(x0, tabY, x1 - x0, TAB_H, 4, col);
        t.setTextColor(on || hot ? Theme::BG : col, on || hot ? col : Theme::BG);
        t.setCursor(x0 + ((x1 - x0) - t.textWidth(lab)) / 2, tabY + (TAB_H - t.fontHeight()) / 2);
        t.print(lab);
    }

    int y = tabY + TAB_H + 4;
    if (s_tab == 2 && s_peer) {
        // The conversation's header: the way back to the list, and whose it is.
        char nm[24], who[48];
        Lora::nodeName(s_peerProto, s_peer, nm, sizeof nm);
        char pv[40];
        const bool mc = s_peerProto == Lora::Proto::MESHCORE;
        snprintf(who, sizeof who, "%s%s", Privacy::name(nm, pv, sizeof pv),
                 Lora::dmHasKey(s_peerProto, s_peer) ? (mc ? "  MESHCORE" : "  MESHTASTIC") : mc ? "  (NOT A CONTACT)" : "  (NO KEY YET)");
        s_headY = y;
        t.setTextColor(dmCol(), Theme::BG);
        t.setCursor(6, y); t.print("< ALL DMs");
        t.setTextColor(netCol(s_peerProto), Theme::BG);
        t.setCursor(w - 8 - t.textWidth(who), y); t.print(who);
    } else {
        t.setTextColor(Theme::blend(Theme::BG, Theme::WHITE, 140), Theme::BG);
        t.setCursor(6, y);
        t.print(s_tab == 2 ? "PRIVATE MESSAGES: MESHTASTIC + MESHCORE" : listenLine());
    }
    y += t.fontHeight() + 5;

    if (!s_rowN) {
        Theme::bubbleFontOn(t);
        t.setTextColor(Theme::blend(Theme::BG, Theme::WHITE, 170), Theme::BG);
        const char* msg = s_tab == 2 ? (s_peer ? "Nothing yet. Type below to send them a private message."
                                               : "No private messages yet. Tap somebody's line in the MESHTASTIC tab to message them.")
                        : s_tab ? "No MeshCore messages yet. Public and hashtag channels show up here."
                                : "No Meshtastic messages yet. The public LongFast channel shows up here.";
        wrap(t, msg, 8, y + 6, w - 16, Theme::bubbleTextH() + 2, bodyBottom, true);
        Theme::bubbleFontOff(t);
        Theme::drawPinnedBack(t, "[ BACK ]");
        return;
    }

    const uint16_t accent = tabCol(s_tab);
    const int lineH = Theme::bubbleTextH() + 1;
    for (int i = s_scroll; i < s_rowN && y < bodyBottom - 12; i++) {
        const Row& r = s_rows[i];
        const int top = y;
        t.setTextSize(1);
        char ago[8];
        age(now, r.ms, ago, sizeof ago);
        char pv[40];
        if (s_tab == 2 && !s_peer) {
            // The list: who, how long ago, their newest line, an unread count.
            if (r.unread) t.fillRect(4, y - 2, w - 8, t.fontHeight() + lineH + 6, Theme::blend(Theme::BG, accent, 28));
            const uint16_t rb = r.unread ? Theme::blend(Theme::BG, accent, 28) : Theme::BG;
            t.setTextColor(netCol(r.proto), rb);
            t.setCursor(6, y); t.print(Privacy::name(r.sender, pv, sizeof pv));
            if (r.unread) { char u[8]; snprintf(u, sizeof u, "  %u NEW", (unsigned)r.unread); t.setTextColor(Theme::WHITE, rb); t.print(u); }
            t.setTextColor(Theme::blend(Theme::BG, Theme::WHITE, 120), rb);
            t.setCursor(w - 8 - t.textWidth(ago), y); t.print(ago);
            y += t.fontHeight() + 2;
            Theme::bubbleFontOn(t);
            char one[64]; snprintf(one, sizeof one, "%s", r.text);
            size_t n = strlen(one);
            while (n && t.textWidth(one) > w - 20) one[--n] = '\0';
            t.setTextColor(Theme::WHITE, rb);
            t.setCursor(10, y + Theme::bubbleAscent()); t.print(one);
            Theme::bubbleFontOff(t);
            y += lineH + 6;
        } else {
            // A message: who, which channel (or the mark on one of ours), how long ago.
            t.setTextColor(r.mine ? Theme::blend(Theme::BG, Theme::WHITE, 200) : s_tab == 2 ? netCol(r.proto) : accent, Theme::BG);
            t.setCursor(6, y);
            t.print(Privacy::name(r.sender, pv, sizeof pv));
            t.setTextColor(Theme::blend(Theme::BG, Theme::WHITE, 120), Theme::BG);
            if (r.chan[0]) { t.print("  "); t.print(r.chan); }
            const int agoX = w - 8 - t.textWidth(ago);
            t.setCursor(agoX, y);
            t.print(ago);
            if (r.mine) drawMark(t, agoX - 14, y, r.dmState);
            y += t.fontHeight() + 2;
            Theme::bubbleFontOn(t);
            t.setTextColor(Theme::WHITE, Theme::BG);
            const int n = wrap(t, r.text, 10, y, w - 20, lineH, bodyBottom, true);
            Theme::bubbleFontOff(t);
            y += n * lineH + 6;
        }
        if (s_rowsDrawn < KEEP) { s_rowTop[s_rowsDrawn] = top - 2; s_rowBot[s_rowsDrawn] = y - 3; s_rowsDrawn++; }
        if (y < bodyBottom) t.drawFastHLine(6, y - 3, w - 12, Theme::blend(Theme::BG, accent, 60));
    }
    Theme::drawPinnedBack(t, "[ BACK ]");
}

bool uiLoraChatTap(int x, int y, uint32_t now) {
    (void)x; (void)now;
    // "< ALL DMs": back to the list.
    if (s_tab == 2 && s_peer && y >= s_headY - 4 && y < s_headY + 12) { s_peer = 0; s_scroll = 0; s_sig = 0xFFFFFFFFu; return true; }
    for (uint8_t k = 0; k < s_rowsDrawn; k++) {
        if (y < s_rowTop[k] || y > s_rowBot[k]) continue;
        const uint8_t i = (uint8_t)(s_rowsFrom + k);
        if (i >= s_rowN) return false;
        const Row& r = s_rows[i];
        // The DMs list: open that conversation. The MESHTASTIC tab: start (or
        // reopen) one with whoever said that line.
        if (s_tab == 2 && !s_peer) { uiLoraChatOpenDm(r.peer, r.proto); return true; }
        if (s_tab == 0 && r.peer)  { uiLoraChatOpenDm(r.peer, Lora::Proto::MESHTASTIC); return true; }
        return false;
    }
    return false;
}

#if LORACHAT_COMPOSE
void uiLoraChatStepChannel(uint32_t now) {
    if (s_tab == 2) {
        snprintf(s_status, sizeof s_status, s_peer ? "PRIVATE: ONLY THEY CAN READ IT" : "TAP A NAME FIRST");
        s_statusAt = now;
        return;
    }
    const Lora::Proto p = tabProto(s_tab);
    const uint8_t was = Lora::Chan::txPick(p);
    if (Lora::Chan::txStep(p) == was) {
        snprintf(s_status, sizeof s_status, "NO OTHER CHANNEL: LORA SETTINGS");
        s_statusAt = now;
    }
}

void uiLoraChatKey(char c, uint32_t now) {
    if (c == '\b') { if (s_composeN) s_compose[--s_composeN] = '\0'; return; }
    if (c == '\n') {
        if (!s_composeN) return;
        if (!Settings::loraTxDbm()) { snprintf(s_status, sizeof s_status, "LORA TX IS OFF: SETTINGS"); s_statusAt = now; return; }
        if (s_tab == 2) {
            if (!s_peer) { snprintf(s_status, sizeof s_status, "TAP A NAME FIRST"); s_statusAt = now; return; }
            // Queued: the row shows at once with its dots; the sniffer sends it
            // and the mark follows the ACK.
            const bool key = Lora::dmHasKey(s_peerProto, s_peer);
            const uint32_t id = Lora::sendDm(s_peerProto, s_peer, s_compose);
            // MeshCore needs them as a contact first: there is no asking for a key.
            snprintf(s_status, sizeof s_status, !id ? (s_peerProto == Lora::Proto::MESHCORE ? "NOT A CONTACT YET" : "SEND FAILED")
                                                    : key ? "SENDING..." : "ASKING FOR THEIR KEY...");
            s_statusAt = now;
            if (id) { s_composeN = 0; s_compose[0] = '\0'; s_sig = 0xFFFFFFFFu; }
            return;
        }
        snprintf(s_status, sizeof s_status, "SENDING...");
        // LORA SETTINGS' NAME, else the squad nickname (include/lora_myname.h),
        // on the channel the chip shows.
        const Lora::Proto p = tabProto(s_tab);
        const bool ok = Lora::sendText(p, uiLoraMyName(), s_compose, (int8_t)Settings::loraTxDbm(), (int16_t)Lora::Chan::txPick(p));
        snprintf(s_status, sizeof s_status, ok ? "SENT" : "SEND FAILED");
        s_statusAt = now;
        if (ok) { s_composeN = 0; s_compose[0] = '\0'; s_sig = 0xFFFFFFFFu; }
        return;
    }
    if (c < 32 || c > 126) return;
    const uint8_t cap = s_tab == 1 ? 120 : 160;      // MeshCore's group text is shorter; a DM takes 160 on either
    if (s_composeN < cap) { s_compose[s_composeN++] = c; s_compose[s_composeN] = '\0'; }
}
#else
void uiLoraChatKey(char, uint32_t) {}
void uiLoraChatStepChannel(uint32_t) {}
#endif

LoraChatHit uiLoraChatHit(TFT_eSPI& t, int x, int y) {
#if LORACHAT_COMPOSE
    // The chip, with a few pixels of slack round it.
    if (x >= s_chipX - 4 && x < s_chipX + s_chipW + 4 && y >= s_chipY - 4 && y < s_chipY + s_chipH + 4)
        return LoraChatHit::CHANNEL;
#endif
    const int tabY = Theme::LIST_TOP;
    if (y < tabY || y > tabY + TAB_H + 4) return LoraChatHit::NONE;
    const int tw = (t.width() - 8) / 3;
    const int k = (x - 4) / tw;
    return k <= 0 ? LoraChatHit::TAB_MESHTASTIC : k == 1 ? LoraChatHit::TAB_MESHCORE : LoraChatHit::TAB_DMS;
}

#endif
