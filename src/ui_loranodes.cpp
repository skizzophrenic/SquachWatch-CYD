// SquachWatch-CYD — LORA > NODES. See include/ui_loranodes.h.
//
// Laid out like LORA SETTINGS, in the same 6x8 font: the header with BACK at
// the top, the three tabs under it, one 26-pixel row a node, the help bar at
// 226. The node table lives in the sniffer behind its lock; this page copies
// it out (about 11 KB, into PSRAM) twice a second rather than every frame.
#if (SQUACH_LORA && defined(TDECK_PLUS)) || SQUACH_LORA_FAKE
#include "ui_loranodes.h"
#include "lora_sniffer.h"
#include "lora_nodes.h"
#include "lora_meshtastic.h"
#include "lora_meshcore.h"
#include "theme.h"
#include "privacy.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdio.h>

namespace {

const int W = 320;
const int HEADER_H = 18;
const int TABS_Y = HEADER_H + 2, TABS_H = 14, TAB_W = 104;
const int LIST_TOP = TABS_Y + TABS_H + 2;
const int BAR_Y = 226;
const int LIST_BOT = BAR_Y - 1;
const int ROW_H = 26;
const uint8_t CAP = Lora::Nodes::CAP;

enum Tab : uint8_t { T_ALL = 0, T_MT, T_MC };

Lora::Nodes::Node* s_snap = nullptr;   // PSRAM, CAP rows
Lora::Nodes::Node* s_tmp  = nullptr;
uint8_t  s_snapN = 0;
uint32_t s_snapAt = 0;
uint8_t  s_order[CAP];                 // the shown rows, newest first
uint8_t  s_shownN = 0;
uint8_t  s_tab = T_ALL;
int      s_scroll = 0;
int16_t  s_rowY[CAP];
uint32_t s_now = 0;
char     s_status[44] = { 0 };
uint32_t s_statusAt = 0;
uint32_t s_peer = 0;
Lora::Proto s_peerProto = Lora::Proto::MESHTASTIC;

uint16_t dim(uint16_t col, uint16_t t) { return Theme::blend(Theme::BG, col, t); }
uint16_t netCol(Lora::Proto p) {
    if (p == Lora::Proto::MESHCORE) return Theme::PINK;
    if (p == Lora::Proto::MESHTASTIC) return Theme::CYAN;
    return dim(Theme::WHITE, 150);
}

void small(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, uint16_t bg = Theme::BG) {
    t.setTextFont(1); t.setTextSize(1);
    t.setTextColor(col, bg);
    t.setCursor(x, y); t.print(s);
}
void clipText(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, int maxW, uint16_t bg = Theme::BG) {
    char buf[72];
    snprintf(buf, sizeof buf, "%s", s);
    t.setTextFont(1); t.setTextSize(1);
    size_t n = strlen(buf);
    while (n && t.textWidth(buf) > maxW) buf[--n] = '\0';
    small(t, x, y, buf, col, bg);
}

void age(uint32_t now, uint32_t ms, char* out, size_t cap) {
    const uint32_t s = (now - ms) / 1000;
    if (s < 60)         snprintf(out, cap, "%lus", (unsigned long)s);
    else if (s < 3600)  snprintf(out, cap, "%lum", (unsigned long)(s / 60));
    else if (s < 86400) snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
    else                snprintf(out, cap, "%lud", (unsigned long)(s / 86400));
}

void status(const char* s) { snprintf(s_status, sizeof s_status, "%s", s); s_statusAt = s_now; }

bool inTab(const Lora::Nodes::Node& n, uint8_t tab) {
    if (tab == T_MT) return n.proto == Lora::Proto::MESHTASTIC;
    if (tab == T_MC) return n.proto == Lora::Proto::MESHCORE;
    return true;
}

uint8_t countIn(uint8_t tab) {
    uint8_t c = 0;
    for (uint8_t i = 0; i < s_snapN; i++) if (inTab(s_snap[i], tab)) c++;
    return c;
}

void refresh(uint32_t now, bool force) {
    if (!s_snap) s_snap = (Lora::Nodes::Node*)heap_caps_calloc(CAP, sizeof(Lora::Nodes::Node), MALLOC_CAP_SPIRAM);
    if (!s_tmp)  s_tmp  = (Lora::Nodes::Node*)heap_caps_calloc(CAP, sizeof(Lora::Nodes::Node), MALLOC_CAP_SPIRAM);
    if (!s_snap || !s_tmp) return;
    if (!force && now - s_snapAt < 500) return;
    const uint8_t n = Lora::nodeSnapshot(s_tmp, CAP);
    // An empty answer while the table has rows is the lock timing out: keep
    // the last good copy.
    if (n || Lora::nodeCount() == 0) {
        memcpy(s_snap, s_tmp, sizeof(Lora::Nodes::Node) * n);
        s_snapN = n;
        s_snapAt = now;
    }
}

// The shown rows, the most recently heard first.
void buildOrder() {
    s_shownN = 0;
    for (uint8_t i = 0; i < s_snapN; i++) if (inTab(s_snap[i], s_tab)) s_order[s_shownN++] = i;
    for (uint8_t i = 1; i < s_shownN; i++)
        for (uint8_t j = i; j > 0 && s_snap[s_order[j]].lastMs > s_snap[s_order[j - 1]].lastMs; j--) {
            const uint8_t k = s_order[j]; s_order[j] = s_order[j - 1]; s_order[j - 1] = k;
        }
}

int maxScroll() {
    const int m = s_shownN * ROW_H - (LIST_BOT - LIST_TOP);
    return m > 0 ? m : 0;
}
void clampScroll() {
    if (s_scroll > maxScroll()) s_scroll = maxScroll();
    if (s_scroll < 0) s_scroll = 0;
}

// What a row's second line says about the node: its handle, its role, and how
// it was heard.
void detail(const Lora::Nodes::Node& n, char* out, size_t cap) {
    char role[16] = "";
    if (n.proto == Lora::Proto::MESHTASTIC && n.name[0]) snprintf(role, sizeof role, "  %s", Meshtastic::roleName(n.role));
    else if (n.proto == Lora::Proto::MESHCORE && n.role) snprintf(role, sizeof role, "  %s", MeshCore::nodeTypeName(n.role));
    char how[40];
    if (n.directPackets && n.hops == 0 && n.rssi > -200)
        snprintf(how, sizeof how, "direct %d dBm %.1f dB", (int)n.rssi, n.snr4 / 4.0f);
    else if (n.hops)
        snprintf(how, sizeof how, "%u hop%s", (unsigned)n.hops, n.hops == 1 ? "" : "s");
    else
        snprintf(how, sizeof how, "relayed");
    snprintf(out, cap, "%s%s  %s", n.tag, role, how);
}

// The conversation a tap on this row opens, if it can have one.
bool dmPeer(const Lora::Nodes::Node& n, uint32_t& peer) {
    if (n.proto == Lora::Proto::MESHTASTIC && n.id && n.id <= 0xFFFFFFFFull) { peer = (uint32_t)n.id; return true; }
    if (n.proto == Lora::Proto::MESHCORE) {
        // A MeshCore row's id is its key's first eight bytes, the
        // conversation the first four; only a contact can be messaged.
        const uint32_t p = (uint32_t)(n.id >> 32);
        if (p && Lora::dmHasKey(Lora::Proto::MESHCORE, p)) { peer = p; return true; }
    }
    return false;
}

void drawHeader(TFT_eSPI& t) {
    t.fillRect(0, 0, W, HEADER_H, Theme::BG);
    char title[32];
    snprintf(title, sizeof title, "LORA > NODES  %u", (unsigned)s_snapN);
    small(t, 8, 4, title, Theme::WHITE);
    small(t, W - 6 - 54, 4, "[ BACK ]", Theme::PURPLE);
    t.drawFastHLine(0, HEADER_H - 1, W, dim(Theme::CYAN, 70));
}

void drawTabs(TFT_eSPI& t) {
    static const char* const NAME[3] = { "ALL", "MESHTASTIC", "MESHCORE" };
    const uint16_t COL[3] = { Theme::WHITE, Theme::CYAN, Theme::PINK };
    for (uint8_t i = 0; i < 3; i++) {
        const int x = 4 + i * TAB_W;
        const bool on = i == s_tab;
        char s[24];
        snprintf(s, sizeof s, "%s %u", NAME[i], (unsigned)countIn(i));
        if (on) t.fillRoundRect(x, TABS_Y, TAB_W - 4, TABS_H, 3, dim(COL[i], 60));
        else    t.drawRoundRect(x, TABS_Y, TAB_W - 4, TABS_H, 3, dim(COL[i], 70));
        t.setTextFont(1); t.setTextSize(1);
        const int tw = t.textWidth(s);
        small(t, x + (TAB_W - 4 - tw) / 2, TABS_Y + 3, s, on ? COL[i] : dim(COL[i], 150), on ? dim(COL[i], 60) : Theme::BG);
    }
}

void drawRows(TFT_eSPI& t) {
    if (!s_shownN) {
        const char* a = s_snapN ? "NONE ON THIS NETWORK YET" : "NOBODY HEARD SINCE THE RADIO STARTED";
        small(t, 10, LIST_TOP + 12, a, dim(Theme::WHITE, 160));
        small(t, 10, LIST_TOP + 26, "A NODE SHOWS UP WHEN IT NEXT TRANSMITS:", dim(Theme::WHITE, 120));
        small(t, 10, LIST_TOP + 36, "A NODEINFO, POSITION, ADVERT OR MESSAGE", dim(Theme::WHITE, 120));
        return;
    }
    int y = LIST_TOP - s_scroll;
    for (uint8_t k = 0; k < s_shownN; k++, y += ROW_H) {
        s_rowY[k] = (int16_t)y;
        if (y + ROW_H <= LIST_TOP || y >= LIST_BOT) continue;
        const Lora::Nodes::Node& n = s_snap[s_order[k]];
        const uint16_t c = netCol(n.proto);
        t.fillRect(4, y + 3, 3, ROW_H - 6, c);
        // Line one: the name, its network on the right, and the age.
        char a[8];
        age(s_now, n.lastMs, a, sizeof a);
        const char* net = Lora::protoShort(n.proto);
        t.setTextFont(1); t.setTextSize(1);
        const int aw = t.textWidth(a), nw = t.textWidth(net);
        char pv[28];
        const char* nm = Privacy::name(n.name[0] ? n.name : n.tag, pv, sizeof pv);
        clipText(t, 12, y + 4, nm, Theme::WHITE, W - 12 - nw - aw - 30);
        small(t, W - 8 - aw - 8 - nw, y + 4, net, c);
        small(t, W - 8 - aw, y + 4, a, dim(Theme::WHITE, 150));
        // Line two: handle, role, and how it was heard.
        char d[64];
        detail(n, d, sizeof d);
        clipText(t, 12, y + 15, d, dim(Theme::WHITE, 130), W - 24);
        t.drawFastHLine(10, y + ROW_H - 1, W - 20, dim(Theme::WHITE, 16));
    }
}

void drawBar(TFT_eSPI& t) {
    t.fillRect(0, BAR_Y, W, 240 - BAR_Y, 0x0862);
    const char* s = "TAP A NODE TO MESSAGE IT.  DRAG OR W / S SCROLLS";
    uint16_t c = dim(Theme::WHITE, 150);
    if (s_status[0] && s_now - s_statusAt < 2500) { s = s_status; c = Theme::VAPOR_YELLOW; }
    clipText(t, 6, BAR_Y + 3, s, c, W - 12, 0x0862);
}

void drawScrollbar(TFT_eSPI& t) {
    const int m = maxScroll();
    if (!m) return;
    const int track = LIST_BOT - LIST_TOP, content = s_shownN * ROW_H;
    const int th = track * track / content < 12 ? 12 : track * track / content;
    const int ty = LIST_TOP + (track - th) * s_scroll / m;
    t.fillRect(W - 3, LIST_TOP, 2, track, dim(Theme::WHITE, 18));
    t.fillRect(W - 3, ty, 2, th, dim(Theme::CYAN, 160));
}

void selectTab(uint8_t tab) {
    if (tab == s_tab) return;
    s_tab = tab;
    s_scroll = 0;
    buildOrder();
}

}  // namespace

void uiLoraNodesInit(TFT_eSPI& t, bool fresh) {
    if (fresh) { s_tab = T_ALL; s_scroll = 0; }
    s_status[0] = '\0';
    s_now = millis();
    refresh(s_now, true);
    buildOrder();
    clampScroll();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiLoraNodesTick(TFT_eSPI& t, uint32_t now) {
    s_now = now;
    refresh(now, false);
    buildOrder();
    clampScroll();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    drawRows(t);
    t.fillRect(0, HEADER_H, W, LIST_TOP - HEADER_H, Theme::BG);   // rows scrolled up under the tabs
    drawHeader(t);
    drawTabs(t);
    drawBar(t);
    drawScrollbar(t);
}

LoraNodesHit uiLoraNodesTap(int x, int y, uint32_t now) {
    s_now = now;
    if (y < HEADER_H + 2) return x >= W - 80 ? LoraNodesHit::BACK : LoraNodesHit::NONE;
    if (y < LIST_TOP) {
        const int i = (x - 4) / TAB_W;
        if (i >= 0 && i < 3) selectTab((uint8_t)i);
        return LoraNodesHit::NONE;
    }
    if (y >= BAR_Y) return LoraNodesHit::NONE;
    for (uint8_t k = 0; k < s_shownN; k++) {
        if (y < s_rowY[k] || y >= s_rowY[k] + ROW_H) continue;
        const Lora::Nodes::Node& n = s_snap[s_order[k]];
        uint32_t peer = 0;
        if (dmPeer(n, peer)) { s_peer = peer; s_peerProto = n.proto; return LoraNodesHit::OPEN_DM; }
        if (n.proto == Lora::Proto::MESHCORE) status("NOT A CONTACT YET: NO ADVERT HEARD");
        else status("NO PRIVATE MESSAGES ON THIS NETWORK");
        return LoraNodesHit::NONE;
    }
    return LoraNodesHit::NONE;
}

uint32_t    uiLoraNodesPeer() { return s_peer; }
Lora::Proto uiLoraNodesPeerProto() { return s_peerProto; }

void uiLoraNodesScrollBy(int px) {
    s_scroll += px;
    clampScroll();
}

void uiLoraNodesKey(char c, uint32_t now) {
    s_now = now;
    if (c == 'w' || c == 'W') uiLoraNodesScrollBy(-ROW_H);
    else if (c == 's' || c == 'S') uiLoraNodesScrollBy(ROW_H);
    else if (c == 'a' || c == 'A') selectTab(T_ALL);
    else if (c == 't' || c == 'T') selectTab(T_MT);
    else if (c == 'c' || c == 'C') selectTab(T_MC);
}

#endif  // (SQUACH_LORA && TDECK_PLUS) || SQUACH_LORA_FAKE
