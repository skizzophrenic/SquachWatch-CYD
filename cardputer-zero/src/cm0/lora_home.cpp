// SquachWatch-CM0 — the LORA page (the T-Deck's landing page, at 320x170).
//
// A port of upstream src/ui_lorahome.cpp (TDECK_PLUS-only there, 320x240):
// the drawing code is upstream's, nearly line for line -- the dish with its
// sweep, pings and nodes; the NOW/FLOOR/PEAK/MARGIN tiles; the strip's
// SPECTRUM field, PHOSPHOR noise line, packet spikes, peak hold and GHOST;
// the scanlines; the tabbed chat window with its CHAT RAIN; the compose
// line, channel chip, bar and double-height switch. Only the vertical layout
// is compressed (240 -> 170): a smaller dish, a shorter strip, three chat
// rows. What differs on purpose, because this board has no touch panel and
// no LoRa transmit:
//   * keys instead of taps: a cursor (TAB/arrows/ENTER) over BACK, the
//     tabs, the chat rows and the switch; BACK leaves (the bar says so);
//  * transmit is real: the compose line takes typed text and Enter sends
//    it with Lora::sendText. As upstream: the LORA TX power and what the
//    radio listens to live on SETTINGS > SYSTEM (rows LORA TX / LORA), and
//    the switch only picks which network the page shows and sends to
//    (uiLoraHomeToggleNet) -- it never retunes the radio.
// Compare against upstream's own render: firmware/sim `squachsim lora
// out.png --size 320x240` (build it from a copy, outside the submodule).
#include "lora_home.h"
#include "theme.h"
#include "lora_sniffer.h"
#include "lora_profiles.h"
#include "lora_channels.h"
#include "settings.h"
#include "privacy.h"
#include "gnss.h"
#include "ui_lorachat.h"
#include "lora_myname.h"
#include "squachy.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

// ---- the layout (upstream's, squeezed to 170 rows) ----------------------------
const int DISH_CX = 38, DISH_CY = 55, DISH_R = 31;    // upstream 58,66 R40
const int COL_X = 78, COL_W = 237;                     // the right column
const int TILE_Y = 24, TILE_H = 28, TILE_W = 57, TILE_GAP = 3;
const int STRIP_Y = 55, STRIP_H = 32;
const int INFO_Y = 90;
const int CHAT_Y = 100, CHAT_TAB_H = 12, CHAT_BOTTOM = 147, CHAT_ROW_H = 11;
const int CHAT_ROWS_Y = CHAT_Y + CHAT_TAB_H + 2;
const int CHAT_LINES = (CHAT_BOTTOM - CHAT_ROWS_Y) / CHAT_ROW_H;   // 3
const int TAB_W = 104;
const int COMPOSE_Y = 148, COMPOSE_H = 11;
const int BAR_Y = 160;

// ---- state -----------------------------------------------------------------------
bool     s_active = false;
uint8_t  s_net = 0;                      // 0 Meshtastic, 1 MeshCore
bool     s_dmTab = false;
uint16_t s_scroll = 0;                   // rows scrolled down the open tab
// The cursor's stops, top to bottom as they sit on the page.
enum Focus : uint8_t { F_BACK, F_TABS, F_CHATS, F_SWITCH, F_COUNT };
uint8_t  s_focus = F_CHATS;
int      s_chipX = 180, s_chipW = 56;
const size_t CHIP_CHARS = 10;

// The compose line: what is typed, and a short status in its place after
// Enter (upstream's s_compose/s_status).
char     s_compose[161] = { 0 };
uint8_t  s_composeN = 0;
char     s_status[64] = { 0 };
uint32_t s_statusAt = 0;

uint16_t dim(uint16_t col, uint16_t t) { return Theme::blend(Theme::BG, col, t); }
uint16_t netCol(Lora::Proto p) { return p == Lora::Proto::MESHCORE ? Theme::PINK : Theme::CYAN; }

void age(uint32_t now, uint32_t ms, char* out, size_t cap) {
    const uint32_t s = (now - ms) / 1000;
    if (s < 60)         snprintf(out, cap, "%lus", (unsigned long)s);
    else if (s < 3600)  snprintf(out, cap, "%lum", (unsigned long)(s / 60));
    else if (s < 86400) snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
    else                snprintf(out, cap, "%lud", (unsigned long)(s / 86400));
}

void small(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, uint16_t bg = Theme::BG) {
    t.setTextFont(1); t.setTextSize(1);
    t.setTextColor(col, bg);
    t.setCursor(x, y); t.print(s);
}
void smallClipBg(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, uint16_t bg, int maxW) {
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);
    t.setTextFont(1); t.setTextSize(1);
    size_t n = strlen(buf);
    while (n && t.textWidth(buf) > maxW) buf[--n] = '\0';
    small(t, x, y, buf, col, bg);
}
void smallClip(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, int maxW) {
    smallClipBg(t, x, y, s, col, Theme::BG, maxW);
}

// Every third row a shade darker over the instruments (upstream's scanlines).
void scanlines(TFT_eSPI& t, int x, int y, int w, int h) {
    for (int yy = y + (3 - (y % 3)) % 3; yy < y + h; yy += 3)
        for (int xx = x; xx < x + w; xx++) {
            const uint16_t c = t.readPixel(xx, yy);
            if (c != Theme::BG) t.drawPixel(xx, yy, Theme::blend(c, Theme::BLACK, 72));
        }
}

// The cursor: the app's key-cursor look (main.cpp stickDrawCursor), a frame
// whose purple > cyan > pink gradient slides round it. Two pixels thick.
void cursorFrame(TFT_eSPI& t, int x, int y, int w, int h, uint32_t now) {
    const int per = 2 * (w + h);
    const int slide = (int)((now / 35) % 24);
    auto col = [&](int i) {
        const float u = (float)(((i / 3) + slide) % 24) / 24.0f;
        const uint16_t stops[4] = { Theme::PURPLE, Theme::CYAN, Theme::PINK, Theme::PURPLE };
        const float xq = u * 3.0f; const int k = (int)xq; const float f = xq - k;
        return Theme::blend(stops[k], stops[k + 1], (uint16_t)(f * 256));
    };
    for (int i = 0; i < per; i++) {
        int px, py;
        if (i < w)               { px = x + i;                 py = y; }
        else if (i < w + h)      { px = x + w - 1;             py = y + (i - w); }
        else if (i < 2 * w + h)  { px = x + w - 1 - (i - w - h); py = y + h - 1; }
        else                     { px = x;                     py = y + h - 1 - (i - 2 * w - h); }
        const uint16_t c = col(i);
        t.drawPixel(px, py, c);
        // the inner ring
        const int ix = px == x ? px + 1 : px == x + w - 1 ? px - 1 : px;
        const int iy = py == y ? py + 1 : py == y + h - 1 ? py - 1 : py;
        t.drawPixel(ix, iy, c);
    }
}

// ---- the header (upstream's, unchanged: it is 320 wide) ------------------------------
void drawHeader(TFT_eSPI& t, uint32_t now) {
    char line[40];
    const uint8_t nodes = Lora::nodeCount();
    const uint8_t L = Settings::loraListen();
    snprintf(line, sizeof line, "LORA  %s  %u NODES",
             L == 4 ? "LORAWAN" : L == 3 ? "BOTH" : L == 2 ? "MESHCORE" : "MESHTASTIC", (unsigned)nodes);
    t.setTextFont(1); t.setTextSize(1);
    const int titleX = (t.width() - t.textWidth(line)) / 2;
    small(t, titleX, 3, line, Theme::WHITE);
    int liveX = titleX - 6 - t.textWidth("LIVE");
    if (liveX > 60) liveX = 60;
    if (liveX < 24) liveX = 24;
    if (Lora::mode() != Lora::Mode::OFF && (now / 600) % 2) small(t, liveX, 3, "LIVE", Theme::GREEN);
    small(t, t.width() - 6 - 54, 3, "[ BACK ]", Theme::PURPLE);
    t.drawFastHLine(4, 2, 16, Theme::CYAN);
    t.drawFastHLine(4, 6, 16, Theme::VAPOR_PINK);
    t.drawFastHLine(4, 10, 16, Theme::CYAN);
    char mt[32] = "", mc[32] = "";
    for (int k = 0; k < 2; k++) {
        const Lora::Proto pr = k ? Lora::Proto::MESHCORE : Lora::Proto::MESHTASTIC;
        const int16_t i = Lora::meshPresetFind(pr, k ? nullptr : Settings::loraMtPreset());
        if (i < 0) continue;
        const Lora::Profile& p = Lora::profile((uint8_t)i);
        char mhz[12], bw[8];
        Lora::formatMHz(p.freqHz, mhz, sizeof mhz);
        Lora::formatBw(p.bwKhz10, bw, sizeof bw);
        snprintf(k ? mc : mt, 32, "%s %s SF%u BW%s", k ? "MC" : "MT", mhz, (unsigned)p.sf, bw);
    }
    if (!mt[0]) strcpy(mt, "MT off");
    if (!mc[0]) strcpy(mc, "MC off");
    small(t, 14, 14, mt, L & 1 ? Theme::CYAN : dim(Theme::CYAN, 110));
    small(t, 176, 14, mc, L & 2 ? Theme::PINK : dim(Theme::PINK, 110));
    if (Lora::mode() != Lora::Mode::OFF) t.fillCircle(7, 17, 2, Theme::GREEN);
}

// ---- one snapshot a frame (upstream's refreshSnapshot) ------------------------------
Lora::Hit  s_pHits[Lora::HIT_CAP];
uint8_t    s_pHitN = 0;
const uint8_t SNAP_NODES = 32;
// More rows kept than upstream's six, so the arrows can scroll back.
const uint8_t KEEP = 24;
struct PageSnap {
    Lora::Nodes::Node nodes[SNAP_NODES], nodesTmp[SNAP_NODES];
    Lora::Msgs::Msg   keep[3][KEEP], keepTmp[3][KEEP];
    Lora::Packet      pkt;
};
PageSnap*  s_ps = nullptr;
uint8_t    s_pNodeN = 0;
uint32_t   s_pNodesAt = 0;
bool       s_pHavePkt = false;
uint32_t   s_pPktAt = 0;
uint8_t    s_pKept[3] = { 0, 0, 0 };
uint16_t   s_pCount[3] = { 0, 0, 0 };
uint32_t   s_pMsgAt = 0;

void refreshSnapshot(uint32_t now, bool force) {
    if (!s_ps) s_ps = (PageSnap*)calloc(1, sizeof(PageSnap));
    if (!s_ps) return;
    {
        Lora::Hit tmp[Lora::HIT_CAP];
        const uint8_t n = Lora::hitTrail(tmp, Lora::HIT_CAP);
        if (n || !s_pHitN) { memcpy(s_pHits, tmp, sizeof(Lora::Hit) * n); s_pHitN = n; }
    }
    if (force || now - s_pNodesAt >= 400) {
        const uint8_t n = Lora::nodeSnapshot(s_ps->nodesTmp, SNAP_NODES);
        if (n || Lora::nodeCount() == 0) {
            memcpy(s_ps->nodes, s_ps->nodesTmp, sizeof(Lora::Nodes::Node) * n);
            s_pNodeN = n; s_pNodesAt = now;
        }
    }
    if (force || now - s_pPktAt >= 200) {
        if (Lora::packetAt(0, s_ps->pkt)) { s_pHavePkt = true; s_pPktAt = now; }
    }
    if (force || now - s_pMsgAt >= 500) {
        auto& keep = s_ps->keepTmp;
        uint8_t kept[3] = { 0, 0, 0 };
        uint16_t count[3] = { 0, 0, 0 };
        const uint16_t n = Lora::msgCount();
        Lora::Msgs::Msg m;
        bool ok = true;
        for (uint16_t i = 0; i < n; i++) {
            if (!Lora::msgAt(i, m)) { ok = false; break; }
            if (m.chan == Lora::Msgs::DM_CHAN) {
                count[2]++;
                bool have = false;
                for (uint8_t k = 0; k < kept[2] && !have; k++) have = keep[2][k].peer == m.peer && keep[2][k].proto == m.proto;
                if (!have && kept[2] < KEEP) {
                    if (m.mine) Lora::nodeName(m.proto, m.peer, m.sender, sizeof m.sender);
                    keep[2][kept[2]++] = m;
                }
                continue;
            }
            const int c = m.proto == Lora::Proto::MESHTASTIC ? 0 : m.proto == Lora::Proto::MESHCORE ? 1 : -1;
            if (c < 0) continue;
            count[c]++;
            if (kept[c] < KEEP) keep[c][kept[c]++] = m;
        }
        if (ok) {
            memcpy(s_ps->keep, keep, sizeof keep);
            for (int k = 0; k < 3; k++) { s_pKept[k] = kept[k]; s_pCount[k] = count[k]; }
            s_pMsgAt = now;
        }
    }
}

uint8_t openTab() { return s_dmTab ? 2 : s_net; }
uint16_t maxScroll() {
    const uint8_t k = s_pKept[openTab()];
    return k > CHAT_LINES ? (uint16_t)(k - CHAT_LINES) : 0;
}

// ---- the numbers and the tiles ---------------------------------------------------------
struct Numbers { int16_t now, floor, peak; int16_t margin; };

Numbers numbers(uint32_t nowMs) {
    Numbers n = {};
    int8_t trail[Lora::TRAIL_CAP];
    const uint16_t tn = Lora::rssiTrail(trail, Lora::TRAIL_CAP);
    n.now = tn ? trail[tn - 1] : -128;
    n.floor = Lora::stats().noiseDbm ? Lora::stats().noiseDbm : -128;
    n.peak = -128;
    for (uint8_t i = 0; i < s_pHitN; i++)
        if (nowMs - s_pHits[i].ms < 80000 && s_pHits[i].rssi > n.peak) n.peak = s_pHits[i].rssi;
    n.margin = (n.peak > -128 && n.floor > -128) ? (int16_t)(n.peak - n.floor) : -128;
    return n;
}

void drawTiles(TFT_eSPI& t, const Numbers& n) {
    const char* keys[4] = { "NOW", "FLOOR", "PEAK", "MARGIN" };
    const int16_t vals[4] = { n.now, n.floor, n.peak, n.margin };
    const uint16_t cols[4] = { Theme::WHITE, dim(Theme::WHITE, 120), Theme::AMBER,
                               n.margin <= -128 ? dim(Theme::WHITE, 120) : n.margin >= 15 ? Theme::GREEN : n.margin >= 6 ? Theme::AMBER : Theme::RED };
    for (int i = 0; i < 4; i++) {
        const int x = COL_X + i * (TILE_W + TILE_GAP);
        t.fillRect(x, TILE_Y, TILE_W, TILE_H, 0x0862);
        t.drawRect(x, TILE_Y, TILE_W, TILE_H, 0x2946);
        t.setTextFont(1); t.setTextSize(1);
        small(t, x + (TILE_W - t.textWidth(keys[i])) / 2, TILE_Y + 2, keys[i], dim(Theme::WHITE, 110), 0x0862);
        char v[8];
        if (vals[i] <= -128) snprintf(v, sizeof v, "--");
        else if (i == 3)     snprintf(v, sizeof v, "%+d", (int)vals[i]);
        else                 snprintf(v, sizeof v, "%d", (int)vals[i]);
        t.setTextSize(2);
        t.setTextColor(cols[i], 0x0862);
        t.setCursor(x + (TILE_W - t.textWidth(v)) / 2, TILE_Y + 11); t.print(v);
        t.setTextSize(1);
    }
}

// ---- the strip: SPECTRUM, PHOSPHOR, spikes, peak hold, GHOST (upstream's) ------------
const uint32_t QUIET_MS = 8000, GHOST_MS = 120000;
const int GLOW_W = COL_W - 2, GLOW_H = STRIP_H - 2;
const int SP_CW = 4, SP_CH = 2;
const int SP_COLS = GLOW_W / SP_CW, SP_ROWS = GLOW_H / SP_CH;      // 58 x 15
const uint32_t SP_COL_MS = 80000 / SP_COLS;                         // the strip's 80 s
struct SpecHeat { float heat[SP_COLS][SP_ROWS]; uint8_t kind[SP_COLS][SP_ROWS]; uint8_t glow[SP_COLS][SP_ROWS]; };
SpecHeat* s_spec = nullptr;
uint32_t  s_colAt = 0, s_hitSeen = 0;
uint8_t*  s_glow = nullptr;
uint32_t  s_lastFrame = 0, s_openedAt = 0;
uint16_t  s_beamIdx = 0;

int yForDbm(int16_t dbm, int top, int h) {
    int v = 120 + dbm; if (v < 0) v = 0; if (v > 80) v = 80;
    return top + h - 3 - (v * (h - 6)) / 80;
}
uint8_t kindOf(Lora::Proto p) { return p == Lora::Proto::MESHTASTIC ? 1 : p == Lora::Proto::MESHCORE ? 2 : 3; }
uint16_t kindCol(uint8_t k) { return k == 1 ? Theme::CYAN : k == 2 ? Theme::PINK : Theme::PURPLE; }
uint16_t scale565(uint16_t c, float a) {
    if (a <= 0) return 0;
    if (a > 1) a = 1;
    return Theme::blend(0x0000, c, (uint16_t)(a * 256));
}
uint32_t lastPacketMs(const Lora::Hit* hits, uint8_t hn) {
    uint32_t last = 0;
    for (uint8_t i = 0; i < hn; i++) if (!last || (int32_t)(hits[i].ms - last) > 0) last = hits[i].ms;
    return last ? last : s_openedAt;
}
uint16_t themeRamp(float u, float a) {
    const uint16_t stops[6] = { Theme::CYAN, Theme::VAPOR_BLUE, Theme::PURPLE, Theme::VAPOR_PURPLE, Theme::PINK, Theme::VAPOR_PINK };
    u -= floorf(u);
    const float x = u * 6.0f; const int i = (int)x; const float f = x - i;
    const uint16_t c = Theme::blend(stops[i % 6], stops[(i + 1) % 6], (uint16_t)(f * 256));
    return scale565(c, a);
}
struct Spark { float x, y, vx, vy; int16_t life, full; };
Spark    s_sparks[40];
uint32_t s_sparkSeed = 0x5EED;
float    sparkRand() { s_sparkSeed = s_sparkSeed * 1664525u + 1013904223u; return (s_sparkSeed >> 8) / 16777216.0f; }
uint32_t mix32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
float sparkleLevel(uint32_t quiet) { return quiet > QUIET_MS ? fminf(1.0f, (quiet - QUIET_MS) / 2000.0f) : 0.0f; }
int spRow(int16_t dbm) {
    int v = 120 + dbm; if (v < 0) v = 0; if (v > 79) v = 79;
    return SP_ROWS - 1 - (v * SP_ROWS) / 80;
}
void heatPacket(int col, int16_t rssi, Lora::Proto p, float scale) {
    if (!s_spec || col < 0 || col >= SP_COLS) return;
    const int row = spRow(rssi);
    const uint8_t k = kindOf(p);
    for (int r = row; r < SP_ROWS; r++) {
        const float h = (r == row ? 1.5f : 0.55f / (float)(r - row)) * scale;
        if (h > s_spec->heat[col][r]) { s_spec->heat[col][r] = h; s_spec->kind[col][r] = k; }
    }
}
void heatPrefill(uint32_t now) {
    if (!s_spec) s_spec = (SpecHeat*)calloc(1, sizeof(SpecHeat));
    if (!s_spec) return;
    memset(s_spec, 0, sizeof(SpecHeat));
    s_colAt = now; s_hitSeen = 0;
    for (uint8_t i = 0; i < s_pHitN; i++) {
        const Lora::Hit& h = s_pHits[i];
        if (!s_hitSeen || (int32_t)(h.ms - s_hitSeen) > 0) s_hitSeen = h.ms;
        const uint32_t ago = now - h.ms;
        if (ago >= 80000) continue;
        heatPacket(SP_COLS - 1 - (int)(ago / SP_COL_MS), h.rssi, h.proto, expf(-(float)ago / 12000.0f));
    }
}

// ---- the CHAT RAIN (upstream's) ---------------------------------------------------------
const char RAIN_GLYPHS[] = "01ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*<>{}[]/\\|+=~SASQUACH";
const int  RAIN_GN = sizeof(RAIN_GLYPHS) - 1;
const float CHAT_RAIN_A = 0.3f;
struct RainCol { float y, v; uint8_t len, depth; bool active; char g[16]; };
struct Rain { RainCol c[54]; uint8_t n; int16_t x0, spacing, top, h; float density; uint32_t seed; bool ready; };
Rain s_chatRain = {};

float rainRand(Rain& R) { R.seed = R.seed * 1664525u + 1013904223u; return (R.seed >> 8) / 16777216.0f; }
char  rainGlyph(Rain& R) { return RAIN_GLYPHS[(int)(rainRand(R) * RAIN_GN) % RAIN_GN]; }

void rainInit(Rain& R, int x0, int x1, int spacing, int top, int h, float density, uint32_t seed) {
    R.x0 = x0; R.spacing = spacing; R.top = top; R.h = h; R.density = density; R.seed = seed;
    R.n = (uint8_t)((x1 - x0) / spacing); if (R.n > 54) R.n = 54;
    for (uint8_t i = 0; i < R.n; i++) {
        RainCol& c = R.c[i];
        c.len = (uint8_t)(6 + rainRand(R) * 10);
        c.y = top + rainRand(R) * (h + c.len * 8);
        c.v = 0.025f + rainRand(R) * 0.04f;
        c.depth = (uint8_t)(rainRand(R) * 4);
        c.active = rainRand(R) < density;
        for (int k = 0; k < 16; k++) c.g[k] = rainGlyph(R);
    }
    R.ready = true;
}
void rainStep(Rain& R, uint32_t dt, float speed) {
    for (uint8_t i = 0; i < R.n; i++) {
        RainCol& c = R.c[i];
        if (!c.active) {
            if (rainRand(R) < 0.0015f * dt * R.density) { c.active = true; c.y = R.top - 8; }
            continue;
        }
        c.y += c.v * speed * dt;
        if (rainRand(R) < 0.02f) c.g[(int)(rainRand(R) * 16) & 15] = rainGlyph(R);
        if (c.y - c.len * 8 > R.top + R.h) {
            c.y = R.top - 8 - rainRand(R) * 30;
            c.len = (uint8_t)(6 + rainRand(R) * 10);
            c.depth = (uint8_t)(rainRand(R) * 4);
            c.active = rainRand(R) < R.density;
        }
    }
}
void rainDraw(TFT_eSPI& t, Rain& R, float alpha, uint16_t trail) {
    for (uint8_t i = 0; i < R.n; i++) {
        const RainCol& c = R.c[i];
        if (!c.active) continue;
        const int x = R.x0 + i * R.spacing;
        for (int k = 0; k < c.len; k++) {
            const int y = (int)c.y - k * 8;
            if (y < R.top - 8 || y > R.top + R.h) continue;
            const float a = alpha * (k == 0 ? 1.0f : (1.0f - (float)k / c.len) * 0.75f) * (0.55f + c.depth * 0.15f);
            if (a < 0.03f) continue;
            const uint16_t fg = scale565(k == 0 ? Theme::WHITE : trail, a);
            t.drawChar(x, y, (uint8_t)c.g[k & 15], fg, fg, 1);
        }
    }
}

void drawStrip(TFT_eSPI& t, uint32_t now, const Numbers& n, uint32_t dt) {
    const int x0 = COL_X + 1, w = COL_W - 2;
    t.fillRect(COL_X, STRIP_Y, COL_W, STRIP_H, 0x0000);
    for (int g = 1; g < 4; g++) t.drawFastHLine(x0, STRIP_Y + g * STRIP_H / 4, w, 0x0841);
    const Lora::Hit* hits = s_pHits;
    const uint8_t hn = s_pHitN;
    const uint32_t quiet = now - lastPacketMs(hits, hn);

    if (!s_spec) heatPrefill(now);
    if (s_spec) {
        while (now - s_colAt >= SP_COL_MS) {
            s_colAt += SP_COL_MS;
            memmove(&s_spec->heat[0][0], &s_spec->heat[1][0], sizeof(float) * SP_ROWS * (SP_COLS - 1));
            memmove(&s_spec->kind[0][0], &s_spec->kind[1][0], SP_ROWS * (SP_COLS - 1));
            for (int r = 0; r < SP_ROWS; r++) { s_spec->heat[SP_COLS - 1][r] = 0; s_spec->kind[SP_COLS - 1][r] = 0; }
        }
        for (uint8_t i = 0; i < hn; i++) if ((int32_t)(hits[i].ms - s_hitSeen) > 0) {
            s_hitSeen = hits[i].ms;
            heatPacket(SP_COLS - 1, hits[i].rssi, hits[i].proto, 1.0f);
        }
        const float cool = expf(-(float)dt / 12000.0f);
        for (int c = 0; c < SP_COLS; c++) for (int r = 0; r < SP_ROWS; r++) s_spec->heat[c][r] *= cool;

        const float spk = (0.6f + 0.4f * sparkleLevel(quiet)) * 0.55f;
        const uint32_t shift = now / 400;
        const uint16_t keep = (uint16_t)(expf(-(float)dt / 450.0f) * 256);
        for (int c = 0; c < SP_COLS; c++) for (int r = 0; r < SP_ROWS; r++) {
            uint8_t& g = s_spec->glow[c][r];
            g = (uint8_t)((g * keep) >> 8);
            const uint32_t h = mix32((uint32_t)(c + shift) * 73856093u ^ (uint32_t)r * 19349663u);
            if ((h & 3) == 0) {
                const float tw = sinf(now * (0.0015f + (h >> 4 & 7) * 0.0005f) + (float)(h >> 8 & 255) * 0.0246f) * 0.5f + 0.5f;
                const uint8_t v = (uint8_t)((0.12f + 0.88f * tw * tw * tw) * 255);
                if (v > g) g = v;
            }
            if (g < 14) continue;
            const float u = (float)c / SP_COLS + r * 0.018f + now / 9000.0f;
            t.fillRect(x0 + c * SP_CW, STRIP_Y + 1 + r * SP_CH, SP_CW - 1, SP_CH, themeRamp(u, g / 255.0f * spk));
        }
        for (int c = 0; c < SP_COLS; c++) for (int r = 0; r < SP_ROWS; r++) {
            const float h = s_spec->heat[c][r];
            if (h < 0.03f || !s_spec->kind[c][r]) continue;
            t.fillRect(x0 + c * SP_CW, STRIP_Y + 1 + r * SP_CH, SP_CW - 1, SP_CH,
                       scale565(kindCol(s_spec->kind[c][r]), fminf(1.0f, h) * 0.6f));
        }
    }

    if (!s_glow) s_glow = (uint8_t*)calloc(GLOW_W * GLOW_H, 1);
    int8_t trail[Lora::TRAIL_CAP];
    const uint16_t tn = Lora::rssiTrail(trail, Lora::TRAIL_CAP);
    if (s_glow) {
        const float keep = expf(-(float)dt / 350.0f);
        const uint16_t k8 = (uint16_t)(keep * 256);
        for (int i = 0; i < GLOW_W * GLOW_H; i++) if (s_glow[i]) s_glow[i] = (uint8_t)((s_glow[i] * k8) >> 8);
        const float fl = n.floor > -128 ? (float)n.floor : -110.0f;
        auto gy = [&](int8_t v) { float f = ((float)v - (fl - 10)) / 20.0f; if (f < 0) f = 0; if (f > 1) f = 1; return (int)((1 - f) * (GLOW_H - 3)) + 1; };
        auto gx = [&](uint16_t k) { return tn > 1 ? (int)((uint32_t)k * (GLOW_W - 1) / (tn - 1)) : GLOW_W - 1; };
        auto plot = [&](int x, int y, uint8_t v) { if (x >= 0 && x < GLOW_W && y >= 0 && y < GLOW_H && s_glow[y * GLOW_W + x] < v) s_glow[y * GLOW_W + x] = v; };
        int headX = -1, headY = 0;
        if (tn > 1) {
            const uint16_t idx = (uint16_t)(((now % 1800) * (uint32_t)(tn - 1)) / 1800);
            uint16_t from = s_beamIdx <= idx ? s_beamIdx : 0;
            if (idx - from > 6) from = idx - 6;
            for (uint16_t k = from ? from - 1 : 0; k < idx; k++) {
                int xa = gx(k), ya = gy(trail[k]); const int xb = gx(k + 1), yb = gy(trail[k + 1]);
                const int dx = abs(xb - xa), dy = -abs(yb - ya), sx = xa < xb ? 1 : -1, sy = ya < yb ? 1 : -1;
                int err = dx + dy;
                for (;;) { plot(xa, ya, 170); if (xa == xb && ya == yb) break; const int e2 = 2 * err; if (e2 >= dy) { err += dy; xa += sx; } if (e2 <= dx) { err += dx; ya += sy; } }
            }
            s_beamIdx = idx;
            const float p = ((now % 1800) * (float)(tn - 1)) / 1800.0f, fr = p - idx;
            const uint16_t nx = idx + 1 < tn ? idx + 1 : idx;
            headX = (int)(gx(idx) + (gx(nx) - gx(idx)) * fr);
            headY = (int)(gy(trail[idx]) + (gy(trail[nx]) - gy(trail[idx])) * fr);
            for (int dy = -1; dy <= 1; dy++) for (int dx = -2; dx <= 1; dx++) plot(headX + dx, headY + dy, (dx == 0 && dy == 0) ? 255 : 215);
            int spawn = 1 + (int)(dt / 14);
            for (Spark& sp : s_sparks) {
                if (!spawn) break;
                if (sp.life > 0) continue;
                sp.x = (float)headX; sp.y = (float)headY;
                sp.vx = -(0.015f + sparkRand() * 0.05f);
                sp.vy = (sparkRand() - 0.5f) * 0.06f;
                sp.full = sp.life = (int16_t)(350 + sparkRand() * 550);
                spawn--;
            }
        }
        const uint16_t lineCol = scale565(Theme::CYAN, 0.30f);
        for (uint16_t k = 0; k + 1 < tn; k++)
            t.drawLine(x0 + gx(k), STRIP_Y + 1 + gy(trail[k]), x0 + gx(k + 1), STRIP_Y + 1 + gy(trail[k + 1]), lineCol);
        const uint16_t hot = Theme::blend(Theme::CYAN, Theme::WHITE, 170);
        for (int y = 0; y < GLOW_H; y++) for (int x = 0; x < GLOW_W; x++) {
            const uint8_t v = s_glow[y * GLOW_W + x];
            if (v <= 8) continue;
            const uint16_t c = v > 200 ? Theme::blend(Theme::CYAN, hot, (uint16_t)((v - 200) * 4.6f))
                                       : scale565(Theme::CYAN, v / 200.0f);
            t.drawPixel(x0 + x, STRIP_Y + 1 + y, c);
        }
        for (Spark& sp : s_sparks) {
            if (sp.life <= 0) continue;
            sp.life -= (int16_t)dt;
            sp.x += sp.vx * dt; sp.y += sp.vy * dt; sp.vy += 0.00012f * dt;
            if (sp.life <= 0 || sp.x < 0 || sp.x >= GLOW_W || sp.y < 0 || sp.y >= GLOW_H) { sp.life = 0; continue; }
            const float lf = (float)sp.life / sp.full;
            const uint16_t c = lf > 0.7f ? Theme::blend(Theme::CYAN, Theme::WHITE, (uint16_t)((lf - 0.7f) * 850))
                                         : themeRamp(0.35f * (1 - lf), lf * 1.3f);
            t.drawPixel(x0 + (int)sp.x, STRIP_Y + 1 + (int)sp.y, c);
            if (lf > 0.75f) t.drawPixel(x0 + (int)sp.x + 1, STRIP_Y + 1 + (int)sp.y, scale565(Theme::CYAN, lf * 0.5f));
        }
        if (headX >= 0) {
            t.fillCircle(x0 + headX, STRIP_Y + 1 + headY, 3, scale565(Theme::CYAN, 0.35f));
            t.fillRect(x0 + headX - 1, STRIP_Y + headY, 2, 2, Theme::WHITE);
        }
    }

    int16_t peakHold[8] = { -128, -128, -128, -128, -128, -128, -128, -128 };
    for (uint8_t i = 0; i < hn; i++) {
        const uint32_t ago = now - hits[i].ms;
        if (ago > 80000) continue;
        const int x = x0 + w - 1 - (int)(ago * (uint32_t)w / 80000);
        const int y = yForDbm(hits[i].rssi, STRIP_Y, STRIP_H);
        t.fillRect(x - 1, y, 3, STRIP_Y + STRIP_H - 3 - y, netCol(hits[i].proto));
        const int bucket = (int)(ago / 10000); if (bucket < 8 && hits[i].rssi > peakHold[bucket]) peakHold[bucket] = hits[i].rssi;
    }
    for (int b = 7; b >= 0; b--) {
        if (peakHold[b] <= -128) continue;
        const int xa = x0 + w - 1 - (int)((b + 1) * w / 8), xb = x0 + w - 1 - (int)(b * w / 8);
        t.drawFastHLine(xa, yForDbm(peakHold[b], STRIP_Y, STRIP_H), xb - xa, dim(Theme::AMBER, 150));
    }
    if (s_pHavePkt) {
        const Lora::Packet& pk = s_ps->pkt;
        const uint32_t ago = now - pk.ms;
        if (ago > 1500 && ago < GHOST_MS) {
            const float fade = fmaxf(0.08f, 1.0f - (float)ago / GHOST_MS);
            const float e = (ago % 3500) / 3500.0f;
            const int x = x0 + w - 1 - (int)(e * 60);
            const int y = yForDbm(pk.rssi, STRIP_Y, STRIP_H);
            t.fillRect(x - 1, y, 3, STRIP_Y + STRIP_H - 3 - y, scale565(kindCol(kindOf(pk.proto)), 0.35f * fade * (1 - e)));
        }
    }
    t.drawRect(COL_X, STRIP_Y, COL_W, STRIP_H, 0x2946);
}

// ---- the dish (upstream's, R 40 -> 31) ------------------------------------------------
struct Blip { float x, y; bool known, direct; Lora::Proto proto; float miles; const char* tag; float bearing; };

const char* compass(float deg) {
    static const char* N[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    int i = (int)((deg + 22.5f) / 45.0f) % 8; if (i < 0) i += 8;
    return N[i];
}

struct Ping { uint32_t at; uint16_t col; bool loud; };
Ping     s_pings[6];
uint8_t  s_pingNext = 0;
uint32_t s_pingSeen = 0, s_pingSweep = 0;
const uint32_t PING_MS = 1400;

void pingStart(uint32_t now, uint16_t col, bool loud) {
    s_pings[s_pingNext] = Ping{ now ? now : 1, col, loud };
    s_pingNext = (uint8_t)((s_pingNext + 1) % 6);
}

void drawPings(TFT_eSPI& t, uint32_t now, int cx, int cy, int R) {
    for (uint8_t i = 0; i < s_pHitN; i++) if ((int32_t)(s_pHits[i].ms - s_pingSeen) > 0) {
        s_pingSeen = s_pHits[i].ms;
        if (now - s_pHits[i].ms < 2000) pingStart(now, netCol(s_pHits[i].proto), true);
    }
    if (now / 4000 != s_pingSweep) { s_pingSweep = now / 4000; pingStart(now, Theme::CYAN, false); }
    for (const Ping& p : s_pings) {
        if (!p.at) continue;
        const uint32_t age = now - p.at;
        if (age >= PING_MS) continue;
        const float u = (float)age / PING_MS;
        const float ease = 1.0f - (1.0f - u) * (1.0f - u);
        const int r = (int)(2 + ease * (R - 3));
        const float a = (1.0f - u) * (p.loud ? 1.0f : 0.35f);
        t.drawCircle(cx, cy, r, scale565(p.col, a));
        if (r > 2) t.drawCircle(cx, cy, r - 1, scale565(p.col, a * 0.45f));
        if (r > 4) t.drawCircle(cx, cy, r - 3, scale565(p.col, a * 0.15f));
        if (p.loud && age > 220) {
            const float u2 = (float)(age - 220) / PING_MS, e2 = 1.0f - (1.0f - u2) * (1.0f - u2);
            const int r2 = (int)(2 + e2 * (R - 3));
            if (r2 < R) t.drawCircle(cx, cy, r2, scale565(p.col, (1.0f - u2) * 0.5f));
        }
    }
}

void drawDish(TFT_eSPI& t, uint32_t now) {
    const int cx = DISH_CX, cy = DISH_CY, R = DISH_R;
    t.fillCircle(cx, cy, R, 0x0041);
    t.drawCircle(cx, cy, R, 0x0C67);
    t.drawCircle(cx, cy, R * 2 / 3, 0x0A45);
    t.drawCircle(cx, cy, R / 3, 0x0A45);
    for (int a = 0; a < 360; a += 8) {
        const float r = a * 0.0174533f;
        t.drawPixel(cx + (int)lroundf((R - 4) * cosf(r)), cy + (int)lroundf((R - 4) * sinf(r)), 0x4208);
    }
    t.drawFastHLine(cx - R, cy, 2 * R, 0x0A45);
    t.drawFastVLine(cx, cy - R, 2 * R, 0x0A45);
    drawPings(t, now, cx, cy, R);
    const float sweep = ((now % 4000) / 4000.0f) * 6.2831853f - 1.5707963f;
    for (int k = 3; k >= 0; k--) {
        const float a = sweep - k * 0.12f;
        t.drawLine(cx, cy, cx + (int)lroundf(R * cosf(a)), cy + (int)lroundf(R * sinf(a)), dim(Theme::CYAN, (uint16_t)(k ? 60 - k * 14 : 180)));
    }
    const Lora::Nodes::Node* nodes = s_ps ? s_ps->nodes : nullptr;
    const uint8_t nn = s_ps ? s_pNodeN : 0;
    const Gnss::Fix& fix = Gnss::fix();
    const bool haveFix = fix.valid && Gnss::fresh(now);
    Blip blips[SNAP_NODES];
    uint8_t nb = 0, unknownN = 0;
    for (uint8_t i = 0; i < nn; i++) {
        const Lora::Nodes::Node& n = nodes[i];
        if (n.proto != Lora::Proto::MESHTASTIC && n.proto != Lora::Proto::MESHCORE) continue;
        if (now - n.lastMs > 3600000u) continue;
        Blip& b = blips[nb];
        b.proto = n.proto; b.direct = n.directPackets > 0; b.tag = n.name[0] ? n.name : n.tag; b.known = false; b.miles = 0; b.bearing = 0;
        if (haveFix && n.hasPos) {
            const float lat0 = fix.lat7 / 1e7f, lat1 = n.latE7 / 1e7f;
            const float dy = (lat1 - lat0) * 69.0f;
            const float dx = (n.lonE7 - fix.lon7) / 1e7f * 69.0f * cosf(lat0 * 0.0174533f);
            b.miles = sqrtf(dx * dx + dy * dy);
            float deg = atan2f(dx, dy) * 57.29578f; if (deg < 0) deg += 360;
            b.bearing = deg;
            const float d = (R - 7) * fminf(1.0f, log10f(1.0f + b.miles) / log10f(11.0f));
            const float a = (deg - 90) * 0.0174533f;
            b.x = cx + d * cosf(a); b.y = cy + d * sinf(a); b.known = true;
        } else unknownN++;
        nb++;
    }
    uint8_t k = 0;
    for (uint8_t i = 0; i < nb; i++) if (!blips[i].known) {
        const float a = 3.5f + (unknownN ? (6.2831853f * k) / unknownN : 0); k++;
        blips[i].x = cx + (R - 4) * cosf(a); blips[i].y = cy + (R - 4) * sinf(a);
    }
    uint8_t order[SNAP_NODES];
    for (uint8_t i = 0; i < nb; i++) order[i] = i;
    for (uint8_t i = 1; i < nb; i++) for (uint8_t j = i; j > 0; j--) {
        const Blip& a = blips[order[j - 1]]; const Blip& b = blips[order[j]];
        const bool swap = (!a.known && b.known) || (a.known && b.known && b.miles < a.miles);
        if (swap) { const uint8_t tmp = order[j]; order[j] = order[j - 1]; order[j - 1] = tmp; } else break;
    }
    // Two labels at most on this smaller dish (upstream: three at R40), and a
    // label that would sit on another is skipped rather than pushed down onto
    // the rim and the "10mi" mark.
    uint8_t labels = 0;
    int placedY[2] = { -99, -99 };
    for (uint8_t oi = 0; oi < nb; oi++) {
        const Blip& b = blips[order[oi]];
        const uint16_t c = netCol(b.proto);
        const int x = (int)lroundf(b.x), y = (int)lroundf(b.y);
        if (b.known) {
            t.fillCircle(x, y, 3, dim(c, 60));
            if (b.direct) t.fillCircle(x, y, 2, c); else t.drawCircle(x, y, 2, c);
            bool clash = false;
            for (int q = 0; q < 2; q++) if (placedY[q] > -99 && abs(placedY[q] - (y - 4)) < 9) clash = true;
            if (labels < 2 && !clash) {
                char pv[24];
                const char* nm = Privacy::name(b.tag, pv, sizeof pv);
                t.setTextFont(1); t.setTextSize(1);
                const int tw = t.textWidth(nm);
                int lx = x > cx ? x + 5 : x - 5 - tw;
                if (lx + tw > cx + R + 6) lx = x - 5 - tw;
                if (lx < 2) lx = 2;
                const int ly = y - 4;
                placedY[labels++] = ly;
                smallClip(t, lx, ly, nm, c, 48);
            }
        } else {
            if (b.direct) t.fillCircle(x, y, 1, dim(c, 190)); else t.drawCircle(x, y, 2, dim(c, 150));
        }
    }
    t.fillCircle(cx, cy, 2, Theme::WHITE);
    small(t, cx - 2, cy - R + 2, "N", dim(Theme::WHITE, 110), 0x0041);
    small(t, cx - R + 1, cy + R - 9, "10mi", dim(Theme::WHITE, 110), 0x0041);
    // The info line under the dish, as upstream's: the GPS in words, the
    // nearest placed node, the key.
    {
        char g[24];
        uint16_t gc;
        if (haveFix)                 { snprintf(g, sizeof g, "GPS %u SATS", (unsigned)fix.used); gc = Theme::GREEN; }
        else if (Gnss::good() > 0)   { snprintf(g, sizeof g, "GPS SEARCHING"); gc = Theme::AMBER; }
        else                         { snprintf(g, sizeof g, "GPS NOT HEARD"); gc = dim(Theme::WHITE, 150); }
        smallClip(t, 4, INFO_Y, g, gc, 84);
    }
    if (nb && blips[order[0]].known) {
        const Blip& b = blips[order[0]];
        char pv[24], line[48];
        const char* nm = Privacy::name(b.tag, pv, sizeof pv);
        if (b.miles < 10) snprintf(line, sizeof line, "NEAR %s %u.%umi %s", nm, (unsigned)b.miles, (unsigned)(b.miles * 10) % 10, compass(b.bearing));
        else              snprintf(line, sizeof line, "NEAR %s %umi %s", nm, (unsigned)b.miles, compass(b.bearing));
        smallClip(t, 92, INFO_Y, line, netCol(b.proto), 132);
    } else {
        smallClip(t, 92, INFO_Y, !nb ? "NOBODY HEARD YET" : haveFix ? "NO NODE POS YET" : "NEEDS A GPS FIX", dim(Theme::WHITE, 150), 132);
    }
    t.fillCircle(232, INFO_Y + 3, 1, Theme::WHITE);  small(t, 236, INFO_Y, "DIRECT", dim(Theme::WHITE, 150));
    t.drawCircle(279, INFO_Y + 3, 2, Theme::WHITE);  small(t, 284, INFO_Y, "RELAY", dim(Theme::WHITE, 150));
}

// ---- the chats ------------------------------------------------------------------------
void drawChatRain(TFT_eSPI& t) {
    t.setTextFont(1); t.setTextSize(1);
    t.setViewport(5, CHAT_ROWS_Y, 310, CHAT_BOTTOM - CHAT_ROWS_Y - 1, false);
    rainDraw(t, s_chatRain, CHAT_RAIN_A, s_net ? Theme::PINK : Theme::CYAN);
    t.resetViewport();
}

void drawChats(TFT_eSPI& t, uint32_t now) {
    if (!s_ps) return;
    const uint16_t cols[3] = { Theme::CYAN, Theme::PINK, Theme::VAPOR_YELLOW };
    const char* names[3] = { "MESHTASTIC", "MESHCORE", "DMs" };
    const uint8_t c = openTab();
    const uint16_t dmUnread = uiLoraChatDmUnread();
    t.drawRect(4, CHAT_Y, 312, CHAT_BOTTOM - CHAT_Y, dim(cols[c], 110));
    t.setTextFont(1); t.setTextSize(1);
    for (int k = 0; k < 3; k++) {
        const int x0 = 4 + k * TAB_W, w = TAB_W;
        const bool on = k == c;
        const bool hot = k == 2 && dmUnread && !on;
        const uint16_t fill = on ? dim(cols[k], 60) : hot ? dim(cols[k], 110) : dim(cols[k], 14);
        t.fillRect(x0 + 1, CHAT_Y + 1, w - 2, CHAT_TAB_H - 1, fill);
        char lab[24];
        snprintf(lab, sizeof lab, "%s %u", names[k], (unsigned)(k == 2 && dmUnread ? dmUnread : s_pCount[k]));
        small(t, x0 + (w - t.textWidth(lab)) / 2, CHAT_Y + 3, lab, on || hot ? Theme::WHITE : dim(cols[k], 150), fill);
    }
    t.drawFastHLine(5, CHAT_Y + CHAT_TAB_H, 310, dim(cols[c], 110));
    t.drawFastVLine(4 + TAB_W, CHAT_Y + 1, CHAT_TAB_H - 1, dim(Theme::WHITE, 40));
    t.drawFastVLine(4 + 2 * TAB_W, CHAT_Y + 1, CHAT_TAB_H - 1, dim(Theme::WHITE, 40));
    const uint16_t unread = uiLoraChatUnread();
    if (unread) {
        char badge[12]; snprintf(badge, sizeof badge, "%u NEW", (unsigned)unread);
        const int bw = t.textWidth(badge) + 6;
        t.fillRect(316 - 3 - bw, CHAT_Y + 1, bw, 10, Theme::VAPOR_YELLOW);
        small(t, 316 - bw, CHAT_Y + 2, badge, Theme::BLACK, Theme::VAPOR_YELLOW);
    }
    if (!s_pKept[c]) {
        small(t, 10, CHAT_ROWS_Y + 4, c == 2 ? "no private messages yet"
                                     : c ? "no MeshCore messages yet" : "no Meshtastic messages yet", dim(Theme::WHITE, 120));
        return;
    }
    if (s_scroll > maxScroll()) s_scroll = maxScroll();
    // With more rows than fit, a 6 px gutter on the right carries the ^/v marks.
    const int right = maxScroll() ? 316 - 7 : 316;
    for (uint8_t i = 0; i < CHAT_LINES && s_scroll + i < s_pKept[c]; i++) {
        const uint8_t ri = (uint8_t)(s_scroll + i);
        const Lora::Msgs::Msg& r = s_ps->keep[c][ri];
        const int y = CHAT_ROWS_Y + i * CHAT_ROW_H;
        const bool newest = ri == 0;
        if (newest) t.fillRect(5, y - 1, 310, CHAT_ROW_H, dim(cols[c], 22));
        char pv[24], ago[8];
        const char* who = Privacy::name(r.sender[0] ? r.sender : "?", pv, sizeof pv);
        age(now, r.ms, ago, sizeof ago);
        const uint16_t bg = newest ? dim(cols[c], 22) : Theme::BG;
        t.setTextColor(c == 2 ? netCol(r.proto) : cols[c], bg); t.setCursor(8, y + 1); t.print(who);
        if (c == 2 && !r.mine && r.ms > uiLoraChatDmSeenMs(r.peer)) { t.setTextColor(Theme::WHITE, bg); t.print(" NEW"); }
        const int tx = t.getCursorX() + 6;
        const int agoW = t.textWidth(ago);
        char text[64]; snprintf(text, sizeof text, "%s%s", c == 2 && r.mine ? "you: " : "", r.text);
        size_t tn = strlen(text);
        while (tn && tx + t.textWidth(text) > right - 8 - agoW) text[--tn] = '\0';
        t.setTextColor(Theme::WHITE, bg); t.setCursor(tx, y + 1); t.print(text);
        t.setTextColor(dim(Theme::WHITE, 110), bg); t.setCursor(right - 4 - agoW, y + 1); t.print(ago);
    }
    // More above or below the three rows: a small marker at the right edge.
    if (maxScroll()) {
        small(t, 309, CHAT_ROWS_Y, "^", s_scroll > 0 ? cols[c] : dim(cols[c], 50));
        small(t, 309, CHAT_BOTTOM - 10, "v", s_scroll < maxScroll() ? cols[c] : dim(cols[c], 50));
    }
}

// ---- the compose line, its channel chip, the bar and the switch --------------------------
Lora::Proto netProto() { return s_net ? Lora::Proto::MESHCORE : Lora::Proto::MESHTASTIC; }

void chipLabel(char* out, size_t cap) {
    if (s_dmTab) { snprintf(out, cap, "PRIVATE"); return; }
    const Lora::Proto p = netProto();
    char name[24];
    Lora::Chan::txName(p, Lora::Chan::txPick(p), name, sizeof name);
    if (strlen(name) > CHIP_CHARS) { name[CHIP_CHARS - 2] = '.'; name[CHIP_CHARS - 1] = '.'; name[CHIP_CHARS] = '\0'; }
    snprintf(out, cap, "%s", name);
}

void drawCompose(TFT_eSPI& t, uint32_t now) {
    const uint16_t accent = s_dmTab ? Theme::VAPOR_YELLOW : s_net ? Theme::PINK : Theme::CYAN;
    char chip[24];
    chipLabel(chip, sizeof chip);
    t.setTextFont(1); t.setTextSize(1);
    s_chipW = t.textWidth(chip) + 8;
    if (s_chipW < 28) s_chipW = 28;
    s_chipX = 236 - s_chipW;
    const int w = s_chipX - 2 - 4;
    const uint16_t bg = dim(Theme::WHITE, 18);
    t.fillRoundRect(4, COMPOSE_Y, w, COMPOSE_H, 2, bg);
    t.drawRoundRect(4, COMPOSE_Y, w, COMPOSE_H, 2, dim(accent, 150));
    // Upstream's rule: a fresh status after Enter, else the typed text, else
    // the TX-off line (no text) or the placeholder.
    if (s_status[0] && s_statusAt && now - s_statusAt < 2500)
        smallClipBg(t, 8, COMPOSE_Y + 2, s_status, Theme::VAPOR_YELLOW, bg, w - 8);
    else if (!Settings::loraTxDbm() && !s_composeN)
        smallClipBg(t, 8, COMPOSE_Y + 2, "LORA TX IS OFF: SETTINGS", dim(Theme::WHITE, 120), bg, w - 8);
    else if (!s_composeN)
        smallClipBg(t, 8, COMPOSE_Y + 2, "TYPE TO SEND, ENTER SENDS", dim(accent, 170), bg, w - 8);
    else {
        char line[168]; snprintf(line, sizeof line, "%s_", s_compose);
        const char* p = line;
        while (*p && t.textWidth(p) > w - 10) p++;
        small(t, 8, COMPOSE_Y + 2, p, Theme::WHITE, bg);
    }
    const bool def = s_dmTab || Lora::Chan::txIsDefault(netProto());
    const uint16_t cbg = def ? dim(accent, 40) : accent;
    t.fillRoundRect(s_chipX, COMPOSE_Y, s_chipW, COMPOSE_H, 2, cbg);
    t.drawRoundRect(s_chipX, COMPOSE_Y, s_chipW, COMPOSE_H, 2, accent);
    small(t, s_chipX + (s_chipW - t.textWidth(chip)) / 2, COMPOSE_Y + 2, chip, def ? accent : Theme::BG, cbg);
}

// The bar: upstream's look, this board's keys (no touch panel here).
void drawBar(TFT_eSPI& t) {
    t.fillRect(0, BAR_Y, t.width(), t.height() - BAR_Y, 0x0862);
    small(t, 6, BAR_Y + 2, "[ TAB MOVES ]", Theme::CYAN, 0x0862);
    small(t, 96, BAR_Y + 2, "ENTER SENDS", dim(Theme::WHITE, 110), 0x0862);
}

// The switch: which network the page shows and Enter sends to (upstream's
// s_net), twice the compose line's height, over the bar's right end.
void drawSwitch(TFT_eSPI& t) {
    const bool mc = s_net != 0;
    const uint16_t accent = mc ? Theme::PINK : Theme::CYAN;
    const int sx = 240, sw = 76, h = t.height() - COMPOSE_Y;
    t.fillRoundRect(sx, COMPOSE_Y, sw, h, 3, dim(accent, 40));
    t.drawRoundRect(sx, COMPOSE_Y, sw, h, 3, accent);
    const char* lab = mc ? "> MESHCORE" : "> MESHTASTIC";
    t.setTextFont(1); t.setTextSize(1);
    small(t, sx + (sw - t.textWidth(lab)) / 2, COMPOSE_Y + (h - 8) / 2, lab, accent, dim(accent, 40));
}

void syncNetToListen() {
    if (Settings::loraListen() == 1) s_net = 0;
    else if (Settings::loraListen() == 2) s_net = 1;
}

}  // namespace

// ---- public API ----------------------------------------------------------------------------
void cm0::loraHomeEnter() {
    s_composeN = 0; s_compose[0] = '\0';
    s_status[0] = '\0'; s_statusAt = 0;
    s_active = true;
    s_dmTab = false;
    s_scroll = 0;
    s_focus = F_CHATS;
    syncNetToListen();
    s_lastFrame = 0;
    s_openedAt = 0;      // set by the first draw, on the caller's clock
}

void cm0::loraHomeLeave() { s_active = false; }

bool cm0::loraHomeActive() { return s_active; }

void cm0::loraHomeDraw(TFT_eSPI& t, uint32_t now) {
    if (!s_openedAt) {
        s_openedAt = now ? now : 1;
        refreshSnapshot(now, true);
        heatPrefill(now);
        s_beamIdx = 0;
    } else {
        refreshSnapshot(now, false);
    }
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    const uint32_t dt = s_lastFrame ? (now - s_lastFrame > 200 ? 200 : now - s_lastFrame) : 16;
    s_lastFrame = now;
    if (!s_chatRain.ready) rainInit(s_chatRain, 7, 313, 6, CHAT_ROWS_Y, CHAT_BOTTOM - CHAT_ROWS_Y, 0.5f, 0xCA75);
    rainStep(s_chatRain, dt, 0.4f);
    const Numbers n = numbers(now);
    drawHeader(t, now);
    drawTiles(t, n);
    drawStrip(t, now, n, dt);
    drawDish(t, now);
    scanlines(t, COL_X, STRIP_Y, COL_W, STRIP_H);
    scanlines(t, DISH_CX - DISH_R, DISH_CY - DISH_R, 2 * DISH_R + 1, 2 * DISH_R + 1);
    drawChatRain(t);
    drawChats(t, now);
    drawCompose(t, now);
    drawBar(t);
    drawSwitch(t);
    // The cursor, last, over whatever it sits on. Typed text overrides: the
    // box with the cursor sits on.
    if (s_composeN)
        cursorFrame(t, 4, COMPOSE_Y, (s_chipX > 4 ? s_chipX - 2 : 232) - 4, COMPOSE_H, now);
    switch (s_focus) {
        case F_BACK:   cursorFrame(t, t.width() - 6 - 58, 0, 62, 13, now); break;
        case F_TABS:   cursorFrame(t, 4 + openTab() * TAB_W, CHAT_Y - 1, TAB_W + 1, CHAT_TAB_H + 2, now); break;
        case F_CHATS:  if (!s_composeN) cursorFrame(t, 4, CHAT_Y + CHAT_TAB_H, 312, CHAT_BOTTOM - CHAT_Y - CHAT_TAB_H, now); break;
        case F_SWITCH: if (!s_composeN) cursorFrame(t, 240, COMPOSE_Y, 76, t.height() - COMPOSE_Y, now); break;
        default: break;
    }
}

namespace {
// The tabs in order: MESHTASTIC, MESHCORE, DMs.
void stepTab(int d) {
    int k = openTab() + d;
    if (k < 0) k = 0;
    if (k > 2) k = 2;
    s_dmTab = k == 2;
    if (k < 2) s_net = (uint8_t)k;
    s_scroll = 0;
}
}  // namespace

bool cm0::loraHomeKey(LmKey k, uint32_t now) {
    switch (k) {
        case LmKey::Back: return true;
        case LmKey::Tab:  s_focus = (uint8_t)((s_focus + 1) % F_COUNT); return false;
        case LmKey::Left:
        case LmKey::Right: {
            const int d = k == LmKey::Right ? 1 : -1;
            if (s_focus == F_TABS || s_focus == F_CHATS) stepTab(d);   // the rows follow the tab
            else if (s_focus == F_SWITCH) s_focus = d < 0 ? F_CHATS : F_BACK;   // the bottom row, then BACK
            else s_focus = F_SWITCH;
            return false;
        }
        case LmKey::Up:
            if (s_focus == F_CHATS && s_scroll > 0) s_scroll--;
            else if (s_focus > F_BACK) s_focus--;
            return false;
        case LmKey::Down:
            if (s_focus == F_CHATS && s_scroll < maxScroll()) s_scroll++;
            else if (s_focus + 1 < F_COUNT) s_focus++;
            return false;
        case LmKey::Enter:
            switch (s_focus) {
                case F_BACK:   return true;
                case F_TABS:   stepTab(openTab() == 2 ? -2 : 1); break;   // cycles round
                case F_CHATS:  s_scroll = 0; break;                        // back to the newest
                case F_SWITCH: cm0::loraHomeToggleNet(); break;
                default: break;
            }
            return false;
        default: return false;
    }
}

void cm0::loraHomeChar(char c, uint32_t now) {
    if (c == '\b') { if (s_composeN) s_compose[--s_composeN] = '\0'; return; }
    if (c == '\n') {
        if (!s_composeN) return;
        if (!Settings::loraTxDbm()) {
            snprintf(s_status, sizeof s_status, "LORA TX IS OFF"); s_statusAt = now;
            return;
        }
        const Lora::Proto p = netProto();
        const char* name = Lora::MyName::longName(Settings::loraName(), Squachy::nickname());
        const bool ok = Lora::sendText(p, name, s_compose,
                                       (int8_t)Settings::loraTxDbm(), (int16_t)Lora::Chan::txPick(p));
        snprintf(s_status, sizeof s_status, ok ? "SENT" : "SEND FAILED");
        s_statusAt = now;
        if (ok) { s_composeN = 0; s_compose[0] = '\0'; }
        return;
    }
    if (c < 32 || c > 126) return;
    const uint8_t cap = s_net ? 120 : 160;
    if (s_composeN < cap) { s_compose[s_composeN++] = c; s_compose[s_composeN] = '\0'; }
}

bool cm0::loraHomeComposing() { return s_composeN > 0; }

// Upstream's uiLoraHomeToggleNet: the page's network only. The radio's
// listen target is the LORA row on SETTINGS > SYSTEM (saved), as upstream.
void cm0::loraHomeToggleNet() {
    s_net = (uint8_t)(s_net ? 0 : 1);
    s_dmTab = false;
    s_scroll = 0;
}

// ---- the LORA pill on the home screen ------------------------------------------------
// Upstream's twatchLoraBadge (main.cpp), the T-Deck's branch: a pink pill up
// whenever the radio is on -- "LORA", or "LORA n NEW" with chats unread. The
// firmware defines it only for the T-Watch and the T-Deck; ui_clear.cpp is
// built from a patched copy (Makefile) that calls it here too.
// Placed on the top row beside the counter tiles (user, 2026-11-25), not top
// centre with the tiles dropped under it as on the T-Deck: left, just past
// the menu icon. The tiles keep the row and are centred in what is left --
// cm0LoraPillReserve() is the width they leave it, read by the patched
// ui_clear.cpp the way it reserves the corner GPS label's. The pill is drawn
// first in the frame, so the tiles see this frame's width. No touch on this
// board: R opens the LORA page, as the pill's tap does on the T-Deck.
namespace { int s_loraPillW = 0; }

void twatchLoraBadge(TFT_eSPI& t) {
    s_loraPillW = 0;
    if (Settings::loraListen() == 0) return;
    const uint16_t u = uiLoraChatUnread();
    char txt[20];
    if (u) snprintf(txt, sizeof txt, "LORA %u NEW", (unsigned)u);
    else   snprintf(txt, sizeof txt, "LORA");
    t.setTextSize(Theme::uiTextSize(t, 1));
    const int w = t.textWidth(txt) + 10, h = t.fontHeight() + 6;
    const int x = Theme::TITLE_ICON_W + 3, y = 4;
    t.fillRoundRect(x, y, w, h, 4, Theme::BG);
    t.drawRoundRect(x, y, w, h, 4, Theme::PINK);
    t.setTextColor(Theme::PINK, Theme::BG);
    t.setCursor(x + 5, y + 3);
    t.print(txt);
    s_loraPillW = w;
}

int cm0LoraPillReserve() { return s_loraPillW ? s_loraPillW + 4 : 0; }
