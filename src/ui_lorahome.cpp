// SquachWatch-CYD — LORA, the T-Deck's landing page. See include/ui_lorahome.h.
//
// Laid out for 320x240 exactly: the dish in a 104-wide column on the left,
// the tiles and the strip in the 210 to its right, one info line at 109,
// the tabbed chat window from 120 down, the compose line at 213, the bar at
// 226.
// SQUACH_LORA_FAKE: the PC emulator's one-shot renderer, which drives this
// page off sim/lora_sim.cpp. No board defines it.
#if (SQUACH_LORA && defined(TDECK_PLUS)) || SQUACH_LORA_FAKE
#include "ui_lorahome.h"
#include "ui_lorachat.h"
#include "ui_lorasettings.h"
#include "lora_sniffer.h"
#include "lora_profiles.h"
#include "lora_radio.h"
#include "lora_meshtastic.h"
#include "lora_meshcore.h"
#include "lora_channels.h"
#include "settings.h"
#include "theme.h"
#include "privacy.h"
#include "gnss.h"
#include "squachy.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <esp_heap_caps.h>

namespace {

// ---- the layout ------------------------------------------------------------
const int DISH_CX = 58, DISH_CY = 66, DISH_R = 40;
const int COL_X = 108, COL_W = 210;            // the right column
const int TILE_Y = 26, TILE_H = 30, TILE_W = 50, TILE_GAP = 3;
const int STRIP_Y = 59, STRIP_H = 46;
// The waterfall came out on 2026-10-16 (the owner: too busy, and the chats
// need the room). One line under the dish, then one chat window, full
// width, tabbed by network, down to the compose line.
const int INFO_Y = 109;
const int CHAT_Y = 120, CHAT_TAB_H = 14, CHAT_BOTTOM = 212, CHAT_ROW_H = 12;
const int CHAT_ROWS_Y = CHAT_Y + CHAT_TAB_H + 2;
const int TAB_W = 104;                         // three tabs across the 312-wide window
const int COMPOSE_Y = 213, COMPOSE_H = 12;
const int BAR_Y = 226;

// ---- state -----------------------------------------------------------------
uint8_t  s_net = 0;                      // the switch: 0 Meshtastic, 1 MeshCore
bool     s_dmTab = false;                // the chat window shows the DM conversations
char     s_compose[161] = { 0 };
uint8_t  s_composeN = 0;
char     s_status[40] = { 0 };
uint32_t s_statusAt = 0;
// Where the chat columns' tap zones are, for uiLoraHomeHit.
int      s_switchX = 240, s_switchW = 76;
// The channel chip, at the compose line's right end: where it was drawn.
int      s_chipX = 180, s_chipW = 56;
// The chip's longest name in characters; a longer one is cut and ends "..".
// Ten keeps two-thirds of the line for typing with the widest chip.
const size_t CHIP_CHARS = 10;

uint16_t dim(uint16_t col, uint16_t t) { return Theme::blend(Theme::BG, col, t); }
uint16_t netCol(Lora::Proto p) { return p == Lora::Proto::MESHCORE ? Theme::PINK : Theme::CYAN; }

void age(uint32_t now, uint32_t ms, char* out, size_t cap) {
    const uint32_t s = (now - ms) / 1000;
    if (s < 60)         snprintf(out, cap, "%lus", (unsigned long)s);
    else if (s < 3600)  snprintf(out, cap, "%lum", (unsigned long)(s / 60));
    else if (s < 86400) snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
    else                snprintf(out, cap, "%lud", (unsigned long)(s / 86400));
}

// Text in the 6x8 font, size 1: the whole page is drawn in it, like the lab.
void small(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, uint16_t bg = Theme::BG) {
    t.setTextFont(1); t.setTextSize(1);
    t.setTextColor(col, bg);
    t.setCursor(x, y); t.print(s);
}
// The same on a background of one's own (the compose line's).
void smallClipBg(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, uint16_t bg, int maxW) {
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);
    t.setTextFont(1); t.setTextSize(1);
    size_t n = strlen(buf);
    while (n && t.textWidth(buf) > maxW) buf[--n] = '\0';
    small(t, x, y, buf, col, bg);
}
void smallClip(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, int maxW) {
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);
    t.setTextFont(1); t.setTextSize(1);
    size_t n = strlen(buf);
    while (n && t.textWidth(buf) > maxW) buf[--n] = '\0';
    t.setTextColor(col, Theme::BG);
    t.setCursor(x, y); t.print(buf);
}

// Every third row a shade darker over a region: the lab's scanlines, kept
// to the instruments so the text stays crisp. Read-modify-write on the
// sprite, about seven thousand pixels a frame.
void scanlines(TFT_eSPI& t, int x, int y, int w, int h) {
    for (int yy = y + (3 - (y % 3)) % 3; yy < y + h; yy += 3)
        for (int xx = x; xx < x + w; xx++) {
            const uint16_t c = t.readPixel(xx, yy);
            if (c != Theme::BG) t.drawPixel(xx, yy, Theme::blend(c, Theme::BLACK, 72));
        }
}

// ---- the header --------------------------------------------------------------
void drawHeader(TFT_eSPI& t, uint32_t now) {
    char line[40];
    const uint8_t nodes = Lora::nodeCount();
    snprintf(line, sizeof line, "LORA  %s  %u NODES",
             Settings::loraListen() == 3 ? "BOTH" : Settings::loraListen() == 2 ? "MESHCORE" : "MESHTASTIC", (unsigned)nodes);
    t.setTextFont(1); t.setTextSize(1);
    small(t, (t.width() - t.textWidth(line)) / 2, 3, line, Theme::WHITE);
    if (Lora::mode() != Lora::Mode::OFF && (now / 600) % 2) small(t, 60, 3, "LIVE", Theme::GREEN);
    // BACK in the top-right corner, away from the compose line's switch: at
    // the bottom it was a thumb's slip from MESHCORE (asked for 2026-10-15).
    small(t, t.width() - 6 - 54, 3, "[ BACK ]", Theme::PURPLE);
    // LORA SETTINGS in the top-left corner, where the main screen keeps its
    // settings button and in its look: the three bars, cyan, pink, cyan
    // (Theme's drawSettingsIcon), drawn small to clear the preset line.
    t.drawFastHLine(4, 2, 16, Theme::CYAN);
    t.drawFastHLine(4, 6, 16, Theme::VAPOR_PINK);
    t.drawFastHLine(4, 10, 16, Theme::CYAN);
    // Each network's preset, under the title, in its colour: the one the
    // radio listens to and sends on -- Meshtastic's as chosen on LORA
    // SETTINGS (LongFast by default), MeshCore's regional one.
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
    small(t, 14, 14, mt, Settings::loraListen() & 1 ? Theme::CYAN : dim(Theme::CYAN, 110));
    small(t, 176, 14, mc, Settings::loraListen() & 2 ? Theme::PINK : dim(Theme::PINK, 110));
    // The radio-busy dot: lit while the radio is on, at the left end of the
    // preset line (at the right end it sat on MeshCore's bandwidth).
    if (Lora::mode() != Lora::Mode::OFF) t.fillCircle(7, 17, 2, Theme::GREEN);
}

// ---- one snapshot a frame -------------------------------------------------------
// Everything the page shows from the radio lives behind the sniffer's lock,
// and each read gives up after 10-20 ms when the radio task is holding it --
// mid-frame, on a busy band. The page used to read straight through and draw
// whatever came back, so a timed-out read drew a frame with no nodes, no
// spikes or no chats, and the radar flashed (2026-10-16). Now the page takes
// its own copies, the hits every frame and the slower things on their own
// clocks, and keeps the last good copy whenever a read comes back empty
// because it timed out rather than because there is nothing.
Lora::Hit  s_pHits[Lora::HIT_CAP];
uint8_t    s_pHitN = 0;
const uint8_t SNAP_NODES = 32;                    // plenty for a dish this size
// The big copies, about 13 KB, in PSRAM: the T-Deck has 7.8 MB of it free
// and its internal RAM is what the radios and the frame push live on.
struct PageSnap {
    Lora::Nodes::Node nodes[SNAP_NODES], nodesTmp[SNAP_NODES];
    // [0] Meshtastic, [1] MeshCore, [2] the DM conversations, one row each.
    Lora::Msgs::Msg   keep[3][6], keepTmp[3][6];
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
    if (!s_ps) s_ps = (PageSnap*)heap_caps_calloc(1, sizeof(PageSnap), MALLOC_CAP_SPIRAM);
    if (!s_ps) return;
    {   // The hits: the ring only ever grows, so an empty answer after a full
        // one is a timeout.
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
        // A walk that hits a timeout part-way is thrown away whole.
        auto& keep = s_ps->keepTmp;
        uint8_t kept[3] = { 0, 0, 0 };
        uint16_t count[3] = { 0, 0, 0 };
        const int lines = (CHAT_BOTTOM - CHAT_ROWS_Y) / CHAT_ROW_H;
        const uint16_t n = Lora::msgCount();
        Lora::Msgs::Msg m;
        bool ok = true;
        for (uint16_t i = 0; i < n; i++) {
            if (!Lora::msgAt(i, m)) { ok = false; break; }
            if (m.chan == Lora::Msgs::DM_CHAN) {
                // A DM: one row a conversation, its newest line; ours named
                // by whom it went to.
                count[2]++;
                bool have = false;
                for (uint8_t k = 0; k < kept[2] && !have; k++) have = keep[2][k].peer == m.peer && keep[2][k].proto == m.proto;
                if (!have && kept[2] < lines && kept[2] < 6) {
                    if (m.mine) Lora::nodeName(m.proto, m.peer, m.sender, sizeof m.sender);
                    keep[2][kept[2]++] = m;
                }
                continue;
            }
            const int c = m.proto == Lora::Proto::MESHTASTIC ? 0 : m.proto == Lora::Proto::MESHCORE ? 1 : -1;
            if (c < 0) continue;
            count[c]++;
            if (kept[c] < lines && kept[c] < 6) keep[c][kept[c]++] = m;
        }
        if (ok) {
            memcpy(s_ps->keep, keep, sizeof keep);
            for (int k = 0; k < 3; k++) { s_pKept[k] = kept[k]; s_pCount[k] = count[k]; }
            s_pMsgAt = now;
        }
    }
}

// ---- the numbers -------------------------------------------------------------
struct Numbers { int16_t now, floor, peak; int16_t margin; };

Numbers numbers(uint32_t nowMs) {
    Numbers n = {};
    int8_t trail[Lora::TRAIL_CAP];
    const uint16_t tn = Lora::rssiTrail(trail, Lora::TRAIL_CAP);
    n.now = tn ? trail[tn - 1] : -128;
    n.floor = Lora::stats().noiseDbm ? Lora::stats().noiseDbm : -128;
    n.peak = -128;
    const Lora::Hit* hits = s_pHits;
    const uint8_t hn = s_pHitN;
    for (uint8_t i = 0; i < hn; i++) if (nowMs - hits[i].ms < 80000 && hits[i].rssi > n.peak) n.peak = hits[i].rssi;
    // How far the loudest packet of the last 80 s stood above the floor: a
    // link quality. NOW over the floor sits near zero on a quiet band, and a
    // quiet band is not a bad one (the first walk, 2026-10-15).
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
        small(t, x + (TILE_W - t.textWidth(keys[i])) / 2, TILE_Y + 3, keys[i], dim(Theme::WHITE, 110), 0x0862);
        char v[8];
        if (vals[i] <= -128) snprintf(v, sizeof v, "--");
        else if (i == 3)     snprintf(v, sizeof v, "%+d", (int)vals[i]);
        else                 snprintf(v, sizeof v, "%d", (int)vals[i]);
        t.setTextSize(2);
        t.setTextColor(cols[i], 0x0862);
        t.setCursor(x + (TILE_W - t.textWidth(v)) / 2, TILE_Y + 13); t.print(v);
        t.setTextSize(1);
    }
}

// ---- the strip ------------------------------------------------------------------
// It reads two rings: the noise samples (one a second, 80 of them) and the
// packet hits (when, how loud, which network): the noise as a line, the hits
// as spikes. (A waterfall under it showed the same seconds as colour until
// 2026-10-16, when it made way for the chats; its colours came back the same
// day as the strip's own background, and the code rain went.)
//
// And four things to keep it alive on a quiet hill, from the Quiet Air Lab
// (2026-10-15) and the owner's notes on the first go:
//   SPECTRUM  behind everything, the old waterfall's field: a grid of cells
//             twinkling in the theme's own colours (Settings > APPEARANCE),
//             drifting left, a little brighter when it is quiet. Each packet
//             leaves heat in its network's colour at its own level, under its
//             spike, that drifts left with it and cools over about twelve
//             seconds. Dim, so the line and the spikes always read over it.
//   PHOSPHOR  the noise is a solid line edge to edge, and a meteor sweeps it
//             from the left: a white-hot head, a tail fading into an
//             afterglow layer, sparks thrown off behind it. Real data, slow
//             glass, a little showing off.
//   GHOST     the newest packet keeps echoing: its spike repeats fainter for
//             two minutes. Real.
const uint32_t QUIET_MS = 8000, GHOST_MS = 120000;
const int GLOW_W = 208, GLOW_H = 44;              // inside the strip's border
// The SPECTRUM's grid: 52 cells of 4 x 2 px fill the 208 x 44 inside, and a
// column every 1.54 s makes 52 columns the strip's own 80 seconds.
const int SP_COLS = 52, SP_ROWS = 22, SP_CW = 4, SP_CH = 2;
const uint32_t SP_COL_MS = 1540;
struct SpecHeat { float heat[SP_COLS][SP_ROWS]; uint8_t kind[SP_COLS][SP_ROWS]; uint8_t glow[SP_COLS][SP_ROWS]; };
SpecHeat* s_spec = nullptr;                       // PSRAM, ~6.9 KB
uint32_t  s_colAt = 0, s_hitSeen = 0;

uint8_t* s_glow = nullptr;                        // PHOSPHOR afterglow, PSRAM
uint32_t s_lastFrame = 0, s_openedAt = 0;
uint16_t s_beamIdx = 0;

int yForDbm(int16_t dbm, int top, int h) {
    // -120 at the bottom, -40 at the top.
    int v = 120 + dbm; if (v < 0) v = 0; if (v > 80) v = 80;
    return top + h - 3 - (v * (h - 6)) / 80;
}
uint8_t kindOf(Lora::Proto p) { return p == Lora::Proto::MESHTASTIC ? 1 : p == Lora::Proto::MESHCORE ? 2 : 3; }
uint16_t kindCol(uint8_t k) { return k == 1 ? Theme::CYAN : k == 2 ? Theme::PINK : Theme::PURPLE; }
uint16_t scale565(uint16_t c, float a) {           // c at brightness a (0..1) over black
    if (a <= 0) return 0; if (a > 1) a = 1;
    return Theme::blend(0x0000, c, (uint16_t)(a * 256));
}

// When the last packet landed, or when the page opened if none has.
uint32_t lastPacketMs(const Lora::Hit* hits, uint8_t hn) {
    uint32_t last = 0;
    for (uint8_t i = 0; i < hn; i++) if (!last || (int32_t)(hits[i].ms - last) > 0) last = hits[i].ms;
    return last ? last : s_openedAt;
}


// A ramp through the theme's own accent colours, so the field follows the
// owner's palette (Settings > APPEARANCE): u 0..1 round the loop, at
// brightness a.
uint16_t themeRamp(float u, float a) {
    const uint16_t stops[6] = { Theme::CYAN, Theme::VAPOR_BLUE, Theme::PURPLE, Theme::VAPOR_PURPLE, Theme::PINK, Theme::VAPOR_PINK };
    u -= floorf(u);
    const float x = u * 6.0f; const int i = (int)x; const float f = x - i;
    const uint16_t c = Theme::blend(stops[i % 6], stops[(i + 1) % 6], (uint16_t)(f * 256));
    return scale565(c, a);
}
// The meteor's sparks: thrown off the head, drifting back and down, fading.
struct Spark { float x, y, vx, vy; int16_t life, full; };
Spark    s_sparks[40];
uint32_t s_sparkSeed = 0x5EED;
float    sparkRand() { s_sparkSeed = s_sparkSeed * 1664525u + 1013904223u; return (s_sparkSeed >> 8) / 16777216.0f; }

// A cheap, steady hash: the same cell twinkles the same way every pass.
uint32_t mix32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
// How lit the sparkle field is: nothing while packets are arriving, then a
// two-second fade in once it has been quiet for QUIET_MS.
float sparkleLevel(uint32_t quiet) { return quiet > QUIET_MS ? fminf(1.0f, (quiet - QUIET_MS) / 2000.0f) : 0.0f; }

// The SPECTRUM row for a level, on the spikes' own -120..-40 scale.
int spRow(int16_t dbm) {
    int v = 120 + dbm; if (v < 0) v = 0; if (v > 79) v = 79;
    return SP_ROWS - 1 - (v * SP_ROWS) / 80;
}
// A packet's heat in one column: hot at its row and a cooler tail below it,
// the shape of its spike.
void heatPacket(int col, int16_t rssi, Lora::Proto p, float scale) {
    if (!s_spec || col < 0 || col >= SP_COLS) return;
    const int row = spRow(rssi);
    const uint8_t k = kindOf(p);
    for (int r = row; r < SP_ROWS; r++) {
        const float h = (r == row ? 1.5f : 0.55f / (float)(r - row)) * scale;
        if (h > s_spec->heat[col][r]) { s_spec->heat[col][r] = h; s_spec->kind[col][r] = k; }
    }
}
// The page opening: the last 80 seconds of packets laid down already cooled
// by their age, so the strip opens with them in it rather than empty.
void heatPrefill(uint32_t now) {
    if (!s_spec) s_spec = (SpecHeat*)heap_caps_calloc(1, sizeof(SpecHeat), MALLOC_CAP_SPIRAM);
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

// ---- the rain (the Rain Lab's picks, 2026-10-15) -----------------------------------
// The DIGITAL background's rain, brought in quietly, the same way it draws:
// ASCII from the 6x8 font, a white-hot head, a trail that fades.
//   CHAT RAIN  a dim rain inside the chat window in the open tab's colour,
//              cyan for Meshtastic and pink for MeshCore, under the lines.
// (It fell behind the strip and through the waterfall as well, and behind the
// page itself, until 2026-10-16: the owner kept it in the chat window only,
// and gave the strip the SPECTRUM instead.)
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
        c.y = top + rainRand(R) * (h + c.len * 8);        // already falling when the page opens
        c.v = 0.025f + rainRand(R) * 0.04f;               // 25-65 px a second
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

// Draw the columns. `ok` says whether a glyph's 6x8 box may be drawn there
// (every caller clips with a viewport instead, today); `col` is the trail's
// colour for a column. The
// glyph is drawn with a transparent background, so only its pixels land.
template <typename OK, typename COL>
void rainDraw(TFT_eSPI& t, Rain& R, float alpha, OK ok, COL col) {
    for (uint8_t i = 0; i < R.n; i++) {
        const RainCol& c = R.c[i];
        if (!c.active) continue;
        const int x = R.x0 + i * R.spacing;
        const uint16_t trail = col(c, x);
        for (int k = 0; k < c.len; k++) {
            const int y = (int)c.y - k * 8;
            if (y < R.top - 8 || y > R.top + R.h) continue;
            if (!ok(x, y)) continue;
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

    // SPECTRUM, behind everything. A new column every 1.54 s; new packets
    // heat the newest one; everything cools.
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

        // The field: each cell twinkles on its own steady rhythm, about one
        // in four lit at a time, drifting left a column every 0.4 s. A cell
        // the field has moved on from, or that has twinkled down, fades out
        // over about half a second instead of vanishing (asked for
        // 2026-10-16), so the drift leaves a short glow behind it.
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
        // The heat, in the network's colour, over the field and under the spikes.
        for (int c = 0; c < SP_COLS; c++) for (int r = 0; r < SP_ROWS; r++) {
            const float h = s_spec->heat[c][r];
            if (h < 0.03f || !s_spec->kind[c][r]) continue;
            t.fillRect(x0 + c * SP_CW, STRIP_Y + 1 + r * SP_CH, SP_CW - 1, SP_CH,
                       scale565(kindCol(s_spec->kind[c][r]), fminf(1.0f, h) * 0.6f));
        }
    }

    // PHOSPHOR: the afterglow fades, then the beam draws the newest part of
    // the noise into it. Scaled to the ten dB either side of the floor, so
    // the radio's own hiss fills the strip rather than lying flat on the
    // bottom; the packets keep the full -120..-40 scale below.
    if (!s_glow) s_glow = (uint8_t*)heap_caps_calloc(GLOW_W * GLOW_H, 1, MALLOC_CAP_SPIRAM);
    int8_t trail[Lora::TRAIL_CAP];
    const uint16_t tn = Lora::rssiTrail(trail, Lora::TRAIL_CAP);
    if (s_glow) {
        const float keep = expf(-(float)dt / 350.0f);
        const uint16_t k8 = (uint16_t)(keep * 256);
        for (int i = 0; i < GLOW_W * GLOW_H; i++) if (s_glow[i]) s_glow[i] = (uint8_t)((s_glow[i] * k8) >> 8);
        const float fl = n.floor > -128 ? (float)n.floor : -110.0f;
        auto gy = [&](int8_t v) { float f = ((float)v - (fl - 10)) / 20.0f; if (f < 0) f = 0; if (f > 1) f = 1; return (int)((1 - f) * (GLOW_H - 3)) + 1; };
        // Edge to edge whatever the trail holds: a page opened a minute after
        // boot has fewer than 80 samples, and they used to start partway in.
        auto gx = [&](uint16_t k) { return tn > 1 ? (int)((uint32_t)k * (GLOW_W - 1) / (tn - 1)) : GLOW_W - 1; };
        auto plot = [&](int x, int y, uint8_t v) { if (x >= 0 && x < GLOW_W && y >= 0 && y < GLOW_H && s_glow[y * GLOW_W + x] < v) s_glow[y * GLOW_W + x] = v; };
        int headX = -1, headY = 0;
        if (tn > 1) {
            const uint16_t idx = (uint16_t)(((now % 1800) * (uint32_t)(tn - 1)) / 1800);
            uint16_t from = s_beamIdx <= idx ? s_beamIdx : 0;
            if (idx - from > 6) from = idx - 6;
            for (uint16_t k = from ? from - 1 : 0; k < idx; k++) {        // Bresenham, segment by segment
                int xa = gx(k), ya = gy(trail[k]); const int xb = gx(k + 1), yb = gy(trail[k + 1]);
                const int dx = abs(xb - xa), dy = -abs(yb - ya), sx = xa < xb ? 1 : -1, sy = ya < yb ? 1 : -1;
                int err = dx + dy;
                for (;;) { plot(xa, ya, 170); if (xa == xb && ya == yb) break; const int e2 = 2 * err; if (e2 >= dy) { err += dy; xa += sx; } if (e2 <= dx) { err += dx; ya += sy; } }
            }
            s_beamIdx = idx;
            // The head, between this sample and the next, so it glides.
            const float p = ((now % 1800) * (float)(tn - 1)) / 1800.0f, fr = p - idx;
            const uint16_t nx = idx + 1 < tn ? idx + 1 : idx;
            headX = (int)(gx(idx) + (gx(nx) - gx(idx)) * fr);
            headY = (int)(gy(trail[idx]) + (gy(trail[nx]) - gy(trail[idx])) * fr);
            for (int dy = -1; dy <= 1; dy++) for (int dx = -2; dx <= 1; dx++) plot(headX + dx, headY + dy, (dx == 0 && dy == 0) ? 255 : 215);
            // Sparks: one or two a frame, thrown back and out from the head.
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
        // The solid line: every sample joined, always there, a dim accent.
        const uint16_t lineCol = scale565(Theme::CYAN, 0.30f);
        for (uint16_t k = 0; k + 1 < tn; k++)
            t.drawLine(x0 + gx(k), STRIP_Y + 1 + gy(trail[k]), x0 + gx(k + 1), STRIP_Y + 1 + gy(trail[k + 1]), lineCol);
        // The tail: the afterglow, white-hot nearest the head, the theme's
        // accent behind it, fading out.
        const uint16_t hot = Theme::blend(Theme::CYAN, Theme::WHITE, 170);
        for (int y = 0; y < GLOW_H; y++) for (int x = 0; x < GLOW_W; x++) {
            const uint8_t v = s_glow[y * GLOW_W + x];
            if (v <= 8) continue;
            const uint16_t c = v > 200 ? Theme::blend(Theme::CYAN, hot, (uint16_t)((v - 200) * 4.6f))
                                       : scale565(Theme::CYAN, v / 200.0f);
            t.drawPixel(x0 + x, STRIP_Y + 1 + y, c);
        }
        // The sparks, falling back off the head and cooling from white
        // through the accent to nothing.
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
        // And the head itself, on top: a small halo and a white core.
        if (headX >= 0) {
            t.fillCircle(x0 + headX, STRIP_Y + 1 + headY, 3, scale565(Theme::CYAN, 0.35f));
            t.fillRect(x0 + headX - 1, STRIP_Y + headY, 2, 2, Theme::WHITE);
        }
    }

    // The packets: a spike each, in its network's colour, on the full scale.
    int16_t peakHold[8] = { -128, -128, -128, -128, -128, -128, -128, -128 };
    for (uint8_t i = 0; i < hn; i++) {
        const uint32_t ago = now - hits[i].ms;
        if (ago > 80000) continue;
        const int x = x0 + w - 1 - (int)(ago * (uint32_t)w / 80000);
        const int y = yForDbm(hits[i].rssi, STRIP_Y, STRIP_H);
        t.fillRect(x - 1, y, 3, STRIP_Y + STRIP_H - 3 - y, netCol(hits[i].proto));
        const int bucket = (int)(ago / 10000); if (bucket < 8 && hits[i].rssi > peakHold[bucket]) peakHold[bucket] = hits[i].rssi;
    }
    // Peak hold: the loudest packet of each ten seconds, in amber.
    for (int b = 7; b >= 0; b--) {
        if (peakHold[b] <= -128) continue;
        const int xa = x0 + w - 1 - (int)((b + 1) * w / 8), xb = x0 + w - 1 - (int)(b * w / 8);
        t.drawFastHLine(xa, yForDbm(peakHold[b], STRIP_Y, STRIP_H), xb - xa, dim(Theme::AMBER, 150));
    }

    // GHOST: the newest packet's spike, echoing leftward every 3.5 s and
    // dimmer each time, for two minutes.
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
    // No labels on the instruments (asked for 2026-10-15): the picture is the
    // reading, and the numbers that matter are in the tiles above.
}


// ---- the dish ------------------------------------------------------------------
struct Blip { float x, y; bool known, direct; Lora::Proto proto; float miles; const char* tag; float bearing; uint64_t id; };

// Where each dot was drawn this frame, for a tap on the dish: the nearest
// Meshtastic dot within a fingertip opens a private conversation with it
// (asked for 2026-10-23). Dots are 9 px across; the reach is wider because a
// finger is.
struct Dot { int16_t x, y; Lora::Proto proto; uint64_t id; };
Dot      s_dots[32];
uint8_t  s_dotN = 0;
const int DOT_REACH = 14;
uint32_t s_tapPeer = 0;
Lora::Proto s_tapProto = Lora::Proto::MESHTASTIC;
// A dot just tapped rings for a moment: on MeshCore, where there are no DMs
// yet, that and a line in the compose box are all a tap does.
int16_t  s_ringX = 0, s_ringY = 0;
uint32_t s_ringAt = 0;

const char* compass(float deg) {
    static const char* N[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    int i = (int)((deg + 22.5f) / 45.0f) % 8; if (i < 0) i += 8;
    return N[i];
}

// The ping (asked for 2026-10-16): rings that open out from the middle of
// the dish. A soft sonar pulse once a sweep, in the theme's accent, and a
// bright double ripple in the network's colour whenever a packet lands.
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
    // New packets, and the start of each sweep.
    const Lora::Hit* hits = s_pHits;
    const uint8_t hn = s_pHitN;
    for (uint8_t i = 0; i < hn; i++) if ((int32_t)(hits[i].ms - s_pingSeen) > 0) {
        s_pingSeen = hits[i].ms;
        if (now - hits[i].ms < 2000) pingStart(now, netCol(hits[i].proto), true);
    }
    if (now / 4000 != s_pingSweep) { s_pingSweep = now / 4000; pingStart(now, Theme::CYAN, false); }
    for (const Ping& p : s_pings) {
        if (!p.at) continue;
        const uint32_t age = now - p.at;
        if (age >= PING_MS) continue;
        const float u = (float)age / PING_MS;
        const float ease = 1.0f - (1.0f - u) * (1.0f - u);          // fast out, slowing at the rim
        const int r = (int)(2 + ease * (R - 3));
        const float a = (1.0f - u) * (p.loud ? 1.0f : 0.35f);
        t.drawCircle(cx, cy, r, scale565(p.col, a));
        if (r > 2) t.drawCircle(cx, cy, r - 1, scale565(p.col, a * 0.45f));      // a soft inner edge
        if (r > 4) t.drawCircle(cx, cy, r - 3, scale565(p.col, a * 0.15f));
        // A loud ping rings twice: a second, thinner ring a beat behind.
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
    for (int a = 0; a < 360; a += 6) {   // the dotted rim: where a node with no bearing sits
        const float r = a * 0.0174533f;
        t.drawPixel(cx + (int)lroundf((R - 4) * cosf(r)), cy + (int)lroundf((R - 4) * sinf(r)), 0x4208);
    }
    t.drawFastHLine(cx - R, cy, 2 * R, 0x0A45);
    t.drawFastVLine(cx, cy - R, 2 * R, 0x0A45);
    drawPings(t, now, cx, cy, R);
    // The sweep: a line going round every four seconds, with a fading trail.
    const float sweep = ((now % 4000) / 4000.0f) * 6.2831853f - 1.5707963f;
    for (int k = 3; k >= 0; k--) {
        const float a = sweep - k * 0.12f;
        t.drawLine(cx, cy, cx + (int)lroundf(R * cosf(a)), cy + (int)lroundf(R * sinf(a)), dim(Theme::CYAN, (uint16_t)(k ? 60 - k * 14 : 180)));
    }
    // The nodes.
    // The page's own copy (see refreshSnapshot); the table holds 96.
    const uint8_t SNAP = SNAP_NODES;
    const Lora::Nodes::Node* nodes = s_ps ? s_ps->nodes : nullptr;
    const uint8_t nn = s_ps ? s_pNodeN : 0;
    const Gnss::Fix& fix = Gnss::fix();
    const bool haveFix = fix.valid && Gnss::fresh(now);
    Blip blips[SNAP];
    uint8_t nb = 0, unknownN = 0;
    for (uint8_t i = 0; i < nn; i++) {
        const Lora::Nodes::Node& n = nodes[i];
        if (n.proto != Lora::Proto::MESHTASTIC && n.proto != Lora::Proto::MESHCORE) continue;
        if (now - n.lastMs > 3600000u) continue;       // an hour: the dish is who is around now
        Blip& b = blips[nb];
        b.proto = n.proto; b.direct = n.directPackets > 0; b.tag = n.name[0] ? n.name : n.tag; b.known = false; b.miles = 0; b.bearing = 0;
        b.id = n.id;
        if (haveFix && n.hasPos) {
            const float lat0 = fix.lat7 / 1e7f, lat1 = n.latE7 / 1e7f;
            const float dy = (lat1 - lat0) * 69.0f;                                  // miles per degree
            const float dx = (n.lonE7 - fix.lon7) / 1e7f * 69.0f * cosf(lat0 * 0.0174533f);
            b.miles = sqrtf(dx * dx + dy * dy);
            float deg = atan2f(dx, dy) * 57.29578f; if (deg < 0) deg += 360;
            b.bearing = deg;
            const float d = (R - 8) * fminf(1.0f, log10f(1.0f + b.miles) / log10f(11.0f));   // 10 miles at the rim
            const float a = (deg - 90) * 0.0174533f;
            b.x = cx + d * cosf(a); b.y = cy + d * sinf(a); b.known = true;
        } else unknownN++;
        nb++;
    }
    // The unknown ones round the rim, spaced.
    uint8_t k = 0;
    for (uint8_t i = 0; i < nb; i++) if (!blips[i].known) {
        const float a = 3.5f + (unknownN ? (6.2831853f * k) / unknownN : 0); k++;
        blips[i].x = cx + (R - 4) * cosf(a); blips[i].y = cy + (R - 4) * sinf(a);
    }
    // Nearest first, so its name wins the label.
    uint8_t order[SNAP];
    for (uint8_t i = 0; i < nb; i++) order[i] = i;
    for (uint8_t i = 1; i < nb; i++) for (uint8_t j = i; j > 0; j--) {
        const Blip& a = blips[order[j - 1]]; const Blip& b = blips[order[j]];
        const bool swap = (!a.known && b.known) || (a.known && b.known && b.miles < a.miles);
        if (swap) { const uint8_t tmp = order[j]; order[j] = order[j - 1]; order[j - 1] = tmp; } else break;
    }
    uint8_t labels = 0;
    int placedY[3] = { -99, -99, -99 };
    s_dotN = 0;
    for (uint8_t oi = 0; oi < nb; oi++) {
        const Blip& b = blips[order[oi]];
        const uint16_t c = netCol(b.proto);
        const int x = (int)lroundf(b.x), y = (int)lroundf(b.y);
        if (s_dotN < sizeof s_dots / sizeof s_dots[0]) s_dots[s_dotN++] = Dot{ (int16_t)x, (int16_t)y, b.proto, b.id };
        if (b.known) {
            t.fillCircle(x, y, 4, dim(c, 60));
            if (b.direct) t.fillCircle(x, y, 2, c); else t.drawCircle(x, y, 2, c);
            if (labels < 3) {
                char pv[24];
                const char* nm = Privacy::name(b.tag, pv, sizeof pv);
                t.setTextFont(1); t.setTextSize(1);
                const int tw = t.textWidth(nm);
                int lx = x > cx ? x + 5 : x - 5 - tw;
                if (lx + tw > cx + R + 6) lx = x - 5 - tw;
                if (lx < 2) lx = 2;
                int ly = y - 4;
                for (int q = 0; q < 3; q++) if (placedY[q] > -99 && abs(placedY[q] - ly) < 9) ly = placedY[q] + 9;
                placedY[labels++] = ly;
                smallClip(t, lx, ly, nm, c, 60);
            }
        } else {
            if (b.direct) t.fillCircle(x, y, 1, dim(c, 190)); else t.drawCircle(x, y, 2, dim(c, 150));
        }
    }
    // The ring round a dot just tapped.
    if (s_ringAt && now - s_ringAt < 600) {
        const int r = 5 + (int)((now - s_ringAt) / 60);
        t.drawCircle(s_ringX, s_ringY, r, Theme::VAPOR_YELLOW);
    }
    t.fillCircle(cx, cy, 2, Theme::WHITE);
    // Just inside the rim: above it, it sat on the Meshtastic preset line.
    small(t, cx - 3, cy - R + 2, "N", dim(Theme::WHITE, 110));
    small(t, cx - R + 2, cy + R - 8, "10mi", dim(Theme::WHITE, 110), 0x0041);
    // Under the dish, one line across the page now the waterfall is gone:
    // the GPS's own state in words (the legend once ended "NO POS" and a
    // tester read it as his own fix, 2026-10-15), the nearest node that has
    // said where it is, and the key.
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
        char pv[24], line[32];
        const char* nm = Privacy::name(b.tag, pv, sizeof pv);
        if (b.miles < 10) snprintf(line, sizeof line, "NEAR %s %u.%umi %s", nm, (unsigned)b.miles, (unsigned)(b.miles * 10) % 10, compass(b.bearing));
        else              snprintf(line, sizeof line, "NEAR %s %umi %s", nm, (unsigned)b.miles, compass(b.bearing));
        smallClip(t, 92, INFO_Y, line, netCol(b.proto), 132);
    } else {
        // Heard, but none has sent a position yet (Meshtastic's default is one
        // every 15-30 min, and many owners turn it off), or no fix to place them.
        smallClip(t, 92, INFO_Y, !nb ? "NOBODY HEARD YET" : haveFix ? "NO NODE POS YET" : "NEEDS A GPS FIX", dim(Theme::WHITE, 150), 132);
    }
    // The key: filled heard direct, hollow through relays.
    t.fillCircle(232, INFO_Y + 3, 1, Theme::WHITE);  small(t, 236, INFO_Y, "DIRECT", dim(Theme::WHITE, 150));
    t.drawCircle(279, INFO_Y + 3, 2, Theme::WHITE);  small(t, 284, INFO_Y, "RELAY", dim(Theme::WHITE, 150));
}

// ---- the chats -------------------------------------------------------------------
// CHAT RAIN: inside the window, under its lines, in the open tab's colour.
void drawChatRain(TFT_eSPI& t) {
    t.setTextFont(1); t.setTextSize(1);
    t.setViewport(5, CHAT_ROWS_Y, 310, CHAT_BOTTOM - CHAT_ROWS_Y - 1, false);
    const uint16_t c = s_net ? Theme::PINK : Theme::CYAN;
    rainDraw(t, s_chatRain, CHAT_RAIN_A, [](int, int) { return true; }, [c](const RainCol&, int) { return c; });
    t.resetViewport();
}

// The window: a tab per network along the top (the open one filled, each
// with its count), then the open network's newest lines, newest first. The
// open tab is the network the compose line sends to: one switch for both.
void drawChats(TFT_eSPI& t, uint32_t now) {
    if (!s_ps) return;
    const uint16_t cols[3] = { Theme::CYAN, Theme::PINK, Theme::VAPOR_YELLOW };
    const char* names[3] = { "MESHTASTIC", "MESHCORE", "DMs" };
    const uint8_t c = s_dmTab ? 2 : s_net;
    const uint16_t dmUnread = uiLoraChatDmUnread();
    t.drawRect(4, CHAT_Y, 312, CHAT_BOTTOM - CHAT_Y, dim(cols[c], 110));
    t.setTextFont(1); t.setTextSize(1);
    for (int k = 0; k < 3; k++) {
        const int x0 = 4 + k * TAB_W, w = TAB_W;
        const bool on = k == c;
        // Unread DMs light their tab even while another is open.
        const bool hot = k == 2 && dmUnread && !on;
        const uint16_t fill = on ? dim(cols[k], 60) : hot ? dim(cols[k], 110) : dim(cols[k], 14);
        t.fillRect(x0 + 1, CHAT_Y + 1, w - 2, CHAT_TAB_H - 1, fill);
        char lab[24];
        snprintf(lab, sizeof lab, "%s %u", names[k], (unsigned)(k == 2 && dmUnread ? dmUnread : s_pCount[k]));
        small(t, x0 + (w - t.textWidth(lab)) / 2, CHAT_Y + 4, lab, on || hot ? Theme::WHITE : dim(cols[k], 150), fill);
    }
    t.drawFastHLine(5, CHAT_Y + CHAT_TAB_H, 310, dim(cols[c], 110));
    t.drawFastVLine(4 + TAB_W, CHAT_Y + 1, CHAT_TAB_H - 1, dim(Theme::WHITE, 40));
    t.drawFastVLine(4 + 2 * TAB_W, CHAT_Y + 1, CHAT_TAB_H - 1, dim(Theme::WHITE, 40));
    // Something unread: a small badge at the far end of the tabs.
    const uint16_t unread = uiLoraChatUnread();
    if (unread) {
        char badge[12]; snprintf(badge, sizeof badge, "%u NEW", (unsigned)unread);
        const int bw = t.textWidth(badge) + 6;
        t.fillRect(316 - 3 - bw, CHAT_Y + 2, bw, 10, Theme::VAPOR_YELLOW);
        small(t, 316 - bw, CHAT_Y + 3, badge, Theme::BLACK, Theme::VAPOR_YELLOW);
    }
    if (!s_pKept[c]) {
        small(t, 10, CHAT_ROWS_Y + 4, c == 2 ? "no private messages yet: tap a Meshtastic line to start one"
                                     : c ? "no MeshCore messages yet" : "no Meshtastic messages yet", dim(Theme::WHITE, 120));
        return;
    }
    for (uint8_t i = 0; i < s_pKept[c]; i++) {
        const Lora::Msgs::Msg& r = s_ps->keep[c][i];
        const int y = CHAT_ROWS_Y + i * CHAT_ROW_H;
        if (i == 0) t.fillRect(5, y - 1, 310, CHAT_ROW_H, dim(cols[c], 22));
        char pv[24], ago[8];
        const char* who = Privacy::name(r.sender[0] ? r.sender : "?", pv, sizeof pv);
        age(now, r.ms, ago, sizeof ago);
        const uint16_t bg = i == 0 ? dim(cols[c], 22) : Theme::BG;
        // In DMs, each name in its own network's colour.
        t.setTextColor(c == 2 ? netCol(r.proto) : cols[c], bg); t.setCursor(8, y + 1); t.print(who);
        // A conversation with something we have not read: NEW after the name.
        if (c == 2 && !r.mine && r.ms > uiLoraChatDmSeenMs(r.peer)) { t.setTextColor(Theme::WHITE, bg); t.print(" NEW"); }
        const int tx = t.getCursorX() + 6;
        const int agoW = t.textWidth(ago);
        char text[64]; snprintf(text, sizeof text, "%s%s", c == 2 && r.mine ? "you: " : "", r.text);
        size_t tn = strlen(text);
        while (tn && tx + t.textWidth(text) > 316 - 8 - agoW) text[--tn] = '\0';
        t.setTextColor(Theme::WHITE, bg); t.setCursor(tx, y + 1); t.print(text);
        t.setTextColor(dim(Theme::WHITE, 110), bg); t.setCursor(316 - 4 - agoW, y + 1); t.print(ago);
    }
}

// ---- the compose line, its channel chip, and the bar -----------------------------
Lora::Proto netProto() { return s_net ? Lora::Proto::MESHCORE : Lora::Proto::MESHTASTIC; }

// The chip's words: the channel Enter sends on, cut to CHIP_CHARS.
void chipLabel(char* out, size_t cap) {
    if (s_dmTab) { snprintf(out, cap, "PRIVATE"); return; }
    const Lora::Proto p = netProto();
    char name[24];
    Lora::Chan::txName(p, Lora::Chan::txPick(p), name, sizeof name);
    if (strlen(name) > CHIP_CHARS) { name[CHIP_CHARS - 2] = '.'; name[CHIP_CHARS - 1] = '.'; name[CHIP_CHARS] = '\0'; }
    snprintf(out, cap, "%s", name);
}

// The compose line now ends where the chip begins: the chip's width is set
// by its name, so the line is measured first and drawn second.
void drawCompose(TFT_eSPI& t, uint32_t now) {
    const uint16_t accent = s_dmTab ? Theme::VAPOR_YELLOW : s_net ? Theme::PINK : Theme::CYAN;
    char chip[24];
    chipLabel(chip, sizeof chip);
    t.setTextFont(1); t.setTextSize(1);
    s_chipW = t.textWidth(chip) + 8;
    if (s_chipW < 28) s_chipW = 28;
    s_chipX = 236 - s_chipW;                     // flush with the old line's right end
    const int w = s_chipX - 2 - 4;               // the typing box, x 4 to the chip
    const uint16_t bg = dim(Theme::WHITE, 18);
    t.fillRoundRect(4, COMPOSE_Y, w, COMPOSE_H, 2, bg);
    t.drawRoundRect(4, COMPOSE_Y, w, COMPOSE_H, 2, dim(accent, 150));
    if (s_status[0] && now - s_statusAt < 2500) {
        smallClipBg(t, 8, COMPOSE_Y + 2, s_status, Theme::VAPOR_YELLOW, bg, w - 8);
    } else if (s_dmTab) {
        smallClipBg(t, 8, COMPOSE_Y + 2, "TAP A NAME TO MESSAGE THEM", dim(Theme::VAPOR_YELLOW, 170), bg, w - 8);
    } else if (!Settings::loraTxDbm() && !s_composeN) {
        smallClipBg(t, 8, COMPOSE_Y + 2, "LORA TX IS OFF: SETTINGS", dim(Theme::WHITE, 120), bg, w - 8);
    } else if (!s_composeN) {
        smallClipBg(t, 8, COMPOSE_Y + 2, "TYPE TO SEND, ENTER SENDS", dim(accent, 170), bg, w - 8);
    } else {
        char line[168]; snprintf(line, sizeof line, "%s_", s_compose);
        const char* p = line;
        while (*p && t.textWidth(p) > w - 10) p++;
        small(t, 8, COMPOSE_Y + 2, p, Theme::WHITE, bg);
    }
    // The chip: outlined on the network's default channel, filled in the
    // network's colour on any other, so a text about to leave on a private
    // channel (or not) is plain at a glance. A tap steps it.
    const bool def = Lora::Chan::txIsDefault(netProto());
    const uint16_t cbg = def ? dim(accent, 40) : accent;
    t.fillRoundRect(s_chipX, COMPOSE_Y, s_chipW, COMPOSE_H, 2, cbg);
    t.drawRoundRect(s_chipX, COMPOSE_Y, s_chipW, COMPOSE_H, 2, accent);
    small(t, s_chipX + (s_chipW - t.textWidth(chip)) / 2, COMPOSE_Y + 2, chip, def ? accent : Theme::BG, cbg);
}

void drawBar(TFT_eSPI& t) {
    t.fillRect(0, BAR_Y, t.width(), t.height() - BAR_Y, 0x0862);
    small(t, 6, BAR_Y + 3, "[ CHATS ]", Theme::CYAN, 0x0862);
    small(t, 96, BAR_Y + 3, "ENTER SENDS", dim(Theme::WHITE, 110), 0x0862);
}

// The switch: which network Enter sends to. Twice the compose line's height
// (asked for 2026-10-15), so it reaches down over the bar's empty right end
// rather than taking a line from the chats; drawn after the bar for that.
void drawSwitch(TFT_eSPI& t) {
    const uint16_t accent = s_net ? Theme::PINK : Theme::CYAN;
    s_switchX = 240; s_switchW = 76;
    const int h = COMPOSE_H * 2;
    t.fillRoundRect(s_switchX, COMPOSE_Y, s_switchW, h, 3, dim(accent, 40));
    t.drawRoundRect(s_switchX, COMPOSE_Y, s_switchW, h, 3, accent);
    const char* lab = s_net ? "> MESHCORE" : "> MESHTASTIC";
    t.setTextFont(1); t.setTextSize(1);
    small(t, s_switchX + (s_switchW - t.textWidth(lab)) / 2, COMPOSE_Y + (h - 8) / 2, lab, accent, dim(accent, 40));
}

}  // namespace

// ---- the API ------------------------------------------------------------------------
void uiLoraHomeInit(TFT_eSPI& t) {
    // The switch follows what the radio listens to; BOTH keeps the last choice.
    if (Settings::loraListen() == 1) s_net = 0;
    else if (Settings::loraListen() == 2) s_net = 1;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_openedAt = millis();
    s_lastFrame = 0;
    refreshSnapshot(s_openedAt, true);
    heatPrefill(s_openedAt);
    s_beamIdx = 0;

}

void uiLoraHomeToggleNet() { s_net = (uint8_t)(s_net ? 0 : 1); s_dmTab = false; }
void uiLoraHomeShowDms() { s_dmTab = true; }
uint32_t uiLoraHomeTappedPeer() { return s_tapPeer; }
Lora::Proto uiLoraHomeTappedProto() { return s_tapProto; }
uint32_t uiLoraHomeDmPeerAt(int y, Lora::Proto* p) {
    if (!s_ps || !s_dmTab || y < CHAT_ROWS_Y) return 0;
    const int i = (y - CHAT_ROWS_Y) / CHAT_ROW_H;
    if (i >= s_pKept[2]) return 0;
    if (p) *p = s_ps->keep[2][i].proto;
    return s_ps->keep[2][i].peer;
}
void uiLoraHomeStepChannel(uint32_t now) {
    if (s_dmTab) { snprintf(s_status, sizeof s_status, "TAP A NAME FIRST"); s_statusAt = now; return; }
    const Lora::Proto p = netProto();
    const uint8_t was = Lora::Chan::txPick(p);
    if (Lora::Chan::txStep(p) == was) {
        // Nothing else to step to: say where channels come from.
        snprintf(s_status, sizeof s_status, "NO OTHER CHANNEL: SETTINGS");
        s_statusAt = now;
    }
}
void uiLoraHomeSetNet(uint8_t net) { s_net = net ? 1 : 0; s_dmTab = false; }
uint8_t uiLoraHomeNet() { return s_net; }

void uiLoraHomeTick(TFT_eSPI& t, uint32_t now) {
    refreshSnapshot(now, false);
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    const uint32_t dt = s_lastFrame ? (now - s_lastFrame > 200 ? 200 : now - s_lastFrame) : 16;
    s_lastFrame = now;
    // The CHAT RAIN, drawn inside the chat window, clipped.
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
}

LoraHomeHit uiLoraHomeHit(TFT_eSPI& t, int x, int y) {
    // BACK is the top-right corner, with a finger's worth of slack; the bar's
    // right half does nothing, so a slip below the switch goes nowhere.
    if (y < 14 && x >= t.width() - 80) return LoraHomeHit::BACK;
    // LORA SETTINGS, the icon top-left: a finger's box over it, clear of the
    // LIVE tag and above the dish.
    if (y < 22 && x < 50) return LoraHomeHit::SETTINGS;
    // A dot on the dish: the nearest within reach.
    {
        const int dx0 = x - DISH_CX, dy0 = y - DISH_CY;
        if (dx0 * dx0 + dy0 * dy0 <= (DISH_R + DOT_REACH) * (DISH_R + DOT_REACH)) {
            int best = -1, bestD = DOT_REACH * DOT_REACH + 1;
            for (uint8_t i = 0; i < s_dotN; i++) {
                const int ddx = x - s_dots[i].x, ddy = y - s_dots[i].y, d = ddx * ddx + ddy * ddy;
                if (d < bestD) { bestD = d; best = i; }
            }
            if (best >= 0) {
                const Dot& d = s_dots[best];
                s_ringX = d.x; s_ringY = d.y; s_ringAt = millis();
                if (d.proto == Lora::Proto::MESHTASTIC && d.id && d.id <= 0xFFFFFFFFull) {
                    s_tapPeer = (uint32_t)d.id; s_tapProto = d.proto;
                    return LoraHomeHit::RADAR_DOT;
                }
                // A MeshCore dot: its row's id is the key's first eight bytes,
                // the conversation the first four. Only a contact can be messaged.
                const uint32_t peer = (uint32_t)(d.id >> 32);
                if (d.proto == Lora::Proto::MESHCORE && peer && Lora::dmHasKey(Lora::Proto::MESHCORE, peer)) {
                    s_tapPeer = peer; s_tapProto = d.proto;
                    return LoraHomeHit::RADAR_DOT;
                }
                snprintf(s_status, sizeof s_status, "NOT A CONTACT YET: NO ADVERT HEARD");
                s_statusAt = millis();
                return LoraHomeHit::NONE;
            }
        }
    }
    // The switch, all of its double height and a little round it.
    if (y >= COMPOSE_Y - 2 && x >= s_switchX - 6) return LoraHomeHit::SWITCH;
    // The channel chip, its own height and a little round it, up to where
    // the switch's slack begins.
    if (y >= COMPOSE_Y - 3 && y < COMPOSE_Y + COMPOSE_H + 2 && x >= s_chipX - 3) return LoraHomeHit::CHANNEL;
    if (y >= BAR_Y - 4) return x < 80 ? LoraHomeHit::CHATS : LoraHomeHit::NONE;
    if (y >= COMPOSE_Y - 2) return LoraHomeHit::NONE;
    if (y >= CHAT_Y - 2 && y < CHAT_Y + CHAT_TAB_H + 2)
        return x < 4 + TAB_W ? LoraHomeHit::TAB_MESHTASTIC : x < 4 + 2 * TAB_W ? LoraHomeHit::TAB_MESHCORE : LoraHomeHit::TAB_DMS;
    if (y >= CHAT_Y + CHAT_TAB_H + 2 && y < COMPOSE_Y - 2)
        return s_dmTab ? LoraHomeHit::CHAT_DMS : s_net ? LoraHomeHit::CHAT_MESHCORE : LoraHomeHit::CHAT_MESHTASTIC;
    return LoraHomeHit::NONE;
}

void uiLoraHomeKey(char c, uint32_t now) {
    if (c == '\b') { if (s_composeN) s_compose[--s_composeN] = '\0'; return; }
    if (c == '\n') {
        if (!s_composeN) return;
        if (s_dmTab) { snprintf(s_status, sizeof s_status, "TAP A NAME FIRST"); s_statusAt = now; return; }
        if (!Settings::loraTxDbm()) { snprintf(s_status, sizeof s_status, "LORA TX IS OFF: SETTINGS"); s_statusAt = now; return; }
        // LORA SETTINGS' NAME, else the squad nickname (include/lora_myname.h),
        // on the channel the chip shows.
        const Lora::Proto p = netProto();
        const bool ok = Lora::sendText(p, uiLoraMyName(), s_compose, (int8_t)Settings::loraTxDbm(), (int16_t)Lora::Chan::txPick(p));
        snprintf(s_status, sizeof s_status, ok ? "SENT" : "SEND FAILED");
        s_statusAt = now;
        if (ok) { s_composeN = 0; s_compose[0] = '\0'; }
        return;
    }
    if (c < 32 || c > 126) return;
    const uint8_t cap = s_net ? 120 : 160;
    if (s_composeN < cap) { s_compose[s_composeN++] = c; s_compose[s_composeN] = '\0'; }
}

#endif  // (SQUACH_LORA && TDECK_PLUS) || SQUACH_LORA_FAKE
