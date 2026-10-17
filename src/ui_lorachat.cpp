// SquachWatch-CYD — LORA CHATS. See include/ui_lorachat.h.
//
// The message ring lives in the sniffer, behind its lock, 256 deep and both
// networks mixed. This screen keeps its own copy of the newest few of the
// open tab's network and rebuilds it only when the ring has moved, so a quiet
// screen is not copying 70 KB out from under a mutex thirty times a second.
#if SQUACH_LORA
#include "ui_lorachat.h"
#include "lora_sniffer.h"
#include "lora_meshtastic.h"
#include "lora_meshcore.h"
#include "settings.h"
#include "theme.h"
#include "privacy.h"
#if defined(TDECK_PLUS)
#include "squachy.h"    // the nickname, for what MeshCore shows before the colon
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
};

const uint8_t  KEEP  = 24;       // the newest this many of the open tab
Row            s_rows[KEEP];
uint8_t        s_rowN   = 0;
uint8_t        s_tab    = 0;     // 0 Meshtastic, 1 MeshCore
int            s_scroll = 0;     // rows skipped from the newest
uint32_t       s_sig    = 0xFFFFFFFFu;
uint8_t        s_sigTab = 0xFF;
uint16_t       s_count[2] = { 0, 0 };
// The ring's running total when the chats were last looked at; the unread
// count is what arrived since, for both tabs.
uint32_t       s_seenTotal = 0;

const int TAB_H = 22;
#if defined(TDECK_PLUS)
// The compose line: what is typed, and a short status in its place after Enter.
const int COMPOSE_H = 22;
char     s_compose[161] = { 0 };
uint8_t  s_composeN = 0;
char     s_status[40] = { 0 };
uint32_t s_statusAt = 0;
#endif

Lora::Proto tabProto(uint8_t t) { return t ? Lora::Proto::MESHCORE : Lora::Proto::MESHTASTIC; }

uint32_t ringTotal() { return (uint32_t)Lora::msgCount() + Lora::msgDropped(); }

void chanName(Lora::Proto p, uint8_t idx, char* out, size_t cap) {
    if (p == Lora::Proto::MESHTASTIC) { Meshtastic::Channel c = Meshtastic::channel(idx); snprintf(out, cap, "%s", c.name[0] ? c.name : "?"); }
    else                              { MeshCore::Channel c = MeshCore::channel(idx);     snprintf(out, cap, "%s", c.name[0] ? c.name : "?"); }
}

// Walk the ring newest first, count both networks, and copy the open tab's
// newest KEEP. Only when something changed.
void refresh() {
    const uint32_t sig = ringTotal();
    if (sig == s_sig && s_sigTab == s_tab) return;
    s_sig = sig; s_sigTab = s_tab;
    s_rowN = 0; s_count[0] = s_count[1] = 0;
    const Lora::Proto want = tabProto(s_tab);
    const uint16_t n = Lora::msgCount();
    Lora::Msgs::Msg m;
    for (uint16_t i = 0; i < n; i++) {
        if (!Lora::msgAt(i, m)) break;
        if (m.proto == Lora::Proto::MESHTASTIC) s_count[0]++;
        else if (m.proto == Lora::Proto::MESHCORE) s_count[1]++;
        else continue;
        if (m.proto != want || s_rowN >= KEEP) continue;
        Row& r = s_rows[s_rowN++];
        snprintf(r.sender, sizeof r.sender, "%s", m.sender[0] ? m.sender : "?");
        chanName(m.proto, m.chan, r.chan, sizeof r.chan);
        snprintf(r.text, sizeof r.text, "%s", m.text);
        r.ms = m.ms;
        r.rssi = m.rssi;
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
#if defined(TLORA_PAGER)
        case 0:  return "LORA IS OFF (LORA + GPS > LORA)";
#else
        case 0:  return "LORA IS OFF (WATCH SETTINGS > LORA)";
#endif
        case 1:  return "LISTENING: MESHTASTIC";
        case 2:  return "LISTENING: MESHCORE";
        default: return "LISTENING: BOTH";
    }
}

#if defined(TDECK_PLUS)
// The compose line, above the BACK bar: the typed text with a cursor, the
// tail of it when it is longer than the line; a status for a moment after
// Enter; the way to switch it on when LORA TX is off.
void drawCompose(TFT_eSPI& t, int w, int y, uint32_t now) {
    const uint16_t accent = s_tab ? Theme::PINK : Theme::CYAN;
    t.fillRoundRect(4, y, w - 8, COMPOSE_H, 4, Theme::blend(Theme::BG, Theme::WHITE, 18));
    t.drawRoundRect(4, y, w - 8, COMPOSE_H, 4, Theme::blend(Theme::BG, accent, 150));
    t.setTextSize(1);
    const int ty = y + (COMPOSE_H - t.fontHeight()) / 2;
    if (s_status[0] && now - s_statusAt < 2500) {
        t.setTextColor(Theme::VAPOR_YELLOW, Theme::blend(Theme::BG, Theme::WHITE, 18));
        t.setCursor(10, ty); t.print(s_status);
        return;
    }
    if (!Settings::loraTxDbm() && !s_composeN) {
        t.setTextColor(Theme::blend(Theme::BG, Theme::WHITE, 120), Theme::blend(Theme::BG, Theme::WHITE, 18));
        t.setCursor(10, ty); t.print("TYPE HERE  (LORA TX IS OFF: SETTINGS > LORA)");
        return;
    }
    char line[168];
    snprintf(line, sizeof line, "%s_", s_compose);
    const char* p = line;
    while (*p && t.textWidth(p) > w - 20) p++;           // the tail that fits
    t.setTextColor(Theme::WHITE, Theme::blend(Theme::BG, Theme::WHITE, 18));
    t.setCursor(10, ty); t.print(p);
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
    if (tab > 1) tab = 1;
    if (tab != s_tab) { s_tab = tab; s_scroll = 0; }
}

void uiLoraChatScroll(int delta) {
    s_scroll += delta;
    if (s_scroll > (int)s_rowN - 1) s_scroll = (int)s_rowN - 1;
    if (s_scroll < 0) s_scroll = 0;
}

uint16_t uiLoraChatUnread() {
    const uint32_t tot = ringTotal();
    return (uint16_t)(tot > s_seenTotal ? (tot - s_seenTotal > 999 ? 999 : tot - s_seenTotal) : 0);
}

void uiLoraChatTick(TFT_eSPI& t, uint32_t now) {
    refresh();
    s_seenTotal = ringTotal();   // looking at it is reading it
    const int w = t.width(), h = t.height();
    int bodyBottom = h - Theme::pinnedBackH(w) - 2;
#if defined(TDECK_PLUS)
    bodyBottom -= COMPOSE_H + 4;
    drawCompose(t, w, bodyBottom + 2, now);
#endif
    t.fillRect(0, 0, w, bodyBottom, Theme::BG);

    // The two tabs, the open one filled.
    const int tabY = Theme::LIST_TOP;
    const char* names[2] = { "MESHTASTIC", "MESHCORE" };
    t.setTextSize(1);
    for (uint8_t i = 0; i < 2; i++) {
        const int x0 = i ? w / 2 + 2 : 4, x1 = i ? w - 4 : w / 2 - 2;
        const bool on = (i == s_tab);
        const uint16_t col = i ? Theme::PINK : Theme::CYAN;
        if (on) t.fillRoundRect(x0, tabY, x1 - x0, TAB_H, 4, col);
        else    t.drawRoundRect(x0, tabY, x1 - x0, TAB_H, 4, col);
        char lab[20];
        snprintf(lab, sizeof lab, "%s %u", names[i], (unsigned)s_count[i]);
        t.setTextColor(on ? Theme::BG : col, on ? col : Theme::BG);
        t.setCursor(x0 + ((x1 - x0) - t.textWidth(lab)) / 2, tabY + (TAB_H - t.fontHeight()) / 2);
        t.print(lab);
    }

    int y = tabY + TAB_H + 4;
    t.setTextColor(Theme::blend(Theme::BG, Theme::WHITE, 140), Theme::BG);
    t.setCursor(6, y);
    t.print(listenLine());
    y += t.fontHeight() + 5;

    if (!s_rowN) {
        Theme::bubbleFontOn(t);
        t.setTextColor(Theme::blend(Theme::BG, Theme::WHITE, 170), Theme::BG);
        const char* msg = s_tab ? "No MeshCore messages yet. Public and hashtag channels show up here."
                                : "No Meshtastic messages yet. The public LongFast channel shows up here.";
        wrap(t, msg, 8, y + 6, w - 16, Theme::bubbleTextH() + 2, bodyBottom, true);
        Theme::bubbleFontOff(t);
        Theme::drawPinnedBack(t, "[ BACK ]");
        return;
    }

    const uint16_t accent = s_tab ? Theme::PINK : Theme::CYAN;
    const int lineH = Theme::bubbleTextH() + 1;
    for (int i = s_scroll; i < s_rowN && y < bodyBottom - 12; i++) {
        const Row& r = s_rows[i];
        // Who, which channel, and how long ago.
        t.setTextSize(1);
        char ago[8];
        age(now, r.ms, ago, sizeof ago);
        t.setTextColor(accent, Theme::BG);
        t.setCursor(6, y);
        {
            char pv[40];
            t.print(Privacy::name(r.sender, pv, sizeof pv));
        }
        t.setTextColor(Theme::blend(Theme::BG, Theme::WHITE, 120), Theme::BG);
        t.print("  ");
        t.print(r.chan);
        t.setCursor(w - 8 - t.textWidth(ago), y);
        t.print(ago);
        y += t.fontHeight() + 2;
        // What they said.
        Theme::bubbleFontOn(t);
        t.setTextColor(Theme::WHITE, Theme::BG);
        const int n = wrap(t, r.text, 10, y, w - 20, lineH, bodyBottom, true);
        Theme::bubbleFontOff(t);
        y += n * lineH + 6;
        if (y < bodyBottom) t.drawFastHLine(6, y - 3, w - 12, Theme::blend(Theme::BG, accent, 60));
    }
    Theme::drawPinnedBack(t, "[ BACK ]");
}

#if defined(TDECK_PLUS)
void uiLoraChatKey(char c, uint32_t now) {
    if (c == '\b') { if (s_composeN) s_compose[--s_composeN] = '\0'; return; }
    if (c == '\n') {
        if (!s_composeN) return;
        if (!Settings::loraTxDbm()) { snprintf(s_status, sizeof s_status, "LORA TX IS OFF: SETTINGS > LORA"); s_statusAt = now; return; }
        const char* nick = Squachy::nickname();
        if (!nick || !*nick) nick = "SQUACHY";
        snprintf(s_status, sizeof s_status, "SENDING...");
        const bool ok = Lora::sendText(tabProto(s_tab), nick, s_compose, (int8_t)Settings::loraTxDbm());
        snprintf(s_status, sizeof s_status, ok ? "SENT" : "SEND FAILED");
        s_statusAt = now;
        if (ok) { s_composeN = 0; s_compose[0] = '\0'; s_sig = 0xFFFFFFFFu; }
        return;
    }
    if (c < 32 || c > 126) return;
    const uint8_t cap = s_tab ? 120 : 160;      // MeshCore's text is shorter
    if (s_composeN < cap) { s_compose[s_composeN++] = c; s_compose[s_composeN] = '\0'; }
}
#else
void uiLoraChatKey(char, uint32_t) {}
#endif

LoraChatHit uiLoraChatHit(TFT_eSPI& t, int x, int y) {
    const int tabY = Theme::LIST_TOP;
    if (y < tabY || y > tabY + TAB_H + 4) return LoraChatHit::NONE;
    return x < t.width() / 2 ? LoraChatHit::TAB_MESHTASTIC : LoraChatHit::TAB_MESHCORE;
}

#endif
