// SquachWatch-CYD — LORA SETTINGS. See include/ui_lorasettings.h.
//
// The rows are rebuilt every frame from the settings themselves (a few dozen
// snprintfs), so there is no copy of any value to fall out of step, and each
// row remembers where it was drawn for the tap that follows. The list is
// drawn first and the header and the bar over it, which is the clipping.
#if (SQUACH_LORA && defined(TDECK_PLUS)) || SQUACH_LORA_FAKE
#include "ui_lorasettings.h"
#include "lora_sniffer.h"
#include "lora_profiles.h"
#include "lora_myname.h"
#include "lora_channels.h"
#include "settings.h"
#include "theme.h"
#include "squachy.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace {

const int W = 320;
const int HEADER_H = 18;
const int BAR_Y = 226;
const int LIST_TOP = HEADER_H + 1, LIST_BOT = BAR_Y - 1;
const int ROW_H = 26, SEC_H = 16;
const int PILL_R = 310;                       // the value pills' right edge

enum Id : uint8_t {
    NONE = 0,
    // the main list
    LISTEN, PRESET, TX, HOPS, REGION, CHANNELS,
    NAME, SHORTNAME, ANNOUNCE,
    MSGSOUND,
    DETECT, ONSCREEN,
    LOOKUPS, LK_CALL, LK_OGN, LK_FEED,
    CLEARCHATS, FORGETNAMES,
    // the CHANNELS list
    ADD_MT, ADD_MC,
    CHAN_ROW,            // + the channelRow number in `arg`
};

struct Row {
    bool     section;
    Id       id;
    uint8_t  arg;
    char     label[28];
    char     value[20];
    char     hint[40];
    uint16_t valCol;
    bool     dim;          // a switch whose master is off, a built-in channel
    bool     editable;     // an edit line opens on this row
    // CHANNELS: the channel the compose line's chip sends on gets a SEND tag;
    // a section's hint is drawn at its right end ("SENDS ON LongFast").
    bool     sendMark;
};
const uint8_t MAX_ROWS = 64;
Row      s_rows[MAX_ROWS];
uint8_t  s_rowN = 0;
int16_t  s_rowY[MAX_ROWS];                    // where each was drawn, scroll applied

enum class View : uint8_t { MAIN, CHANNELS };
View     s_view = View::MAIN;
int      s_scroll[2] = { 0, 0 };
int      s_contentH = 0;

// The edit line.
enum class Edit : uint8_t { NONE, NAME, SHORTNAME, CH_NAME, CH_KEY };
Edit     s_edit = Edit::NONE;
char     s_buf[81] = { 0 };
uint8_t  s_bufN = 0;
Lora::Proto s_addProto = Lora::Proto::MESHTASTIC;
char     s_addName[24] = { 0 };

// CONFIRM? on a CLEAN UP row or a channel's REMOVE: the second tap does it.
Id       s_confirm = NONE;
uint8_t  s_confirmArg = 0;
char     s_confirmName[24] = { 0 };
uint32_t s_confirmAt = 0;
const uint32_t CONFIRM_MS = 4000;

// A line in the bar after something happened ("CHATS CLEARED").
char     s_status[48] = { 0 };
uint32_t s_statusAt = 0;
uint32_t s_now = 0;
bool     s_resting = false;

// REGION is read at boot: the one this boot runs on, to say RESTART TO APPLY.
int      s_bootRegion = -1;

uint16_t dim(uint16_t col, uint16_t t) { return Theme::blend(Theme::BG, col, t); }

void small(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, uint16_t bg) {
    t.setTextFont(1); t.setTextSize(1);
    t.setTextColor(col, bg);
    t.setCursor(x, y); t.print(s);
}
void clipText(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, uint16_t bg, int maxW) {
    char buf[64];
    snprintf(buf, sizeof buf, "%s", s);
    t.setTextFont(1); t.setTextSize(1);
    size_t n = strlen(buf);
    while (n && t.textWidth(buf) > maxW) buf[--n] = '\0';
    small(t, x, y, buf, col, bg);
}

void status(const char* s) { snprintf(s_status, sizeof s_status, "%s", s); s_statusAt = s_now; }

bool confirming(Id id, uint8_t arg) {
    return s_confirm == id && s_confirmArg == arg && s_now - s_confirmAt < CONFIRM_MS;
}

// ---- building the rows -----------------------------------------------------------
Row& add(Id id, const char* label, uint8_t arg = 0) {
    Row& r = s_rows[s_rowN < MAX_ROWS ? s_rowN : MAX_ROWS - 1];
    if (s_rowN < MAX_ROWS) s_rowN++;
    memset(&r, 0, sizeof r);
    r.id = id; r.arg = arg;
    snprintf(r.label, sizeof r.label, "%s", label);
    r.valCol = Theme::CYAN;
    return r;
}
void section(const char* title) { Row& r = add(NONE, title); r.section = true; }
void onOff(Row& r, bool on) {
    snprintf(r.value, sizeof r.value, "%s", on ? "ON" : "OFF");
    r.valCol = on ? Theme::GREEN : dim(Theme::WHITE, 150);
}

void buildMain() {
    s_rowN = 0;
    section("NETWORK");
    {
        Row& r = add(LISTEN, "LISTEN");
        snprintf(r.value, sizeof r.value, "%s", Settings::loraListenName());
        r.valCol = Settings::loraListen() ? Theme::CYAN : dim(Theme::WHITE, 150);
        snprintf(r.hint, sizeof r.hint, "WHICH NETWORKS THE RADIO HEARS");
    }
    {
        Row& r = add(PRESET, "MESHTASTIC PRESET");
        const int16_t i = Lora::meshPresetFind(Lora::Proto::MESHTASTIC, Settings::loraMtPreset());
        if (i >= 0) {
            const Lora::Profile& p = Lora::profile((uint8_t)i);
            snprintf(r.value, sizeof r.value, "%s", strncmp(p.name, "MT ", 3) == 0 ? p.name + 3 : p.name);
            char mhz[12], bw[8];
            Lora::formatMHz(p.freqHz, mhz, sizeof mhz);
            Lora::formatBw(p.bwKhz10, bw, sizeof bw);
            snprintf(r.hint, sizeof r.hint, "%s MHz  SF%u  BW %s", mhz, (unsigned)p.sf, bw);
        } else {
            snprintf(r.value, sizeof r.value, "NONE");
            snprintf(r.hint, sizeof r.hint, "NO MESHTASTIC IN THIS BAND PLAN");
        }
        r.valCol = Theme::CYAN;
    }
    {
        Row& r = add(TX, "TX POWER");
        snprintf(r.value, sizeof r.value, "%s", Settings::loraTxName());
        r.valCol = Settings::loraTxDbm() ? Theme::AMBER : dim(Theme::WHITE, 150);
        if (!Settings::loraTxDbm())                                  snprintf(r.hint, sizeof r.hint, "OFF: THE CHATS CANNOT SEND");
        else if (Lora::region() == Lora::REGION_EU && Settings::loraTxDbm() > 14) snprintf(r.hint, sizeof r.hint, "EU 868: SENT AT 14 dBm");
        else                                                         snprintf(r.hint, sizeof r.hint, "WHAT THE CHATS SEND AT");
    }
    {
        Row& r = add(HOPS, "HOPS");
        snprintf(r.value, sizeof r.value, "%u", (unsigned)Settings::loraHops());
        snprintf(r.hint, sizeof r.hint, "MESHTASTIC HOP LIMIT (3 = STOCK)");
    }
    {
        Row& r = add(REGION, "REGION");
        static const char* const N[3] = { "AUTO", "EU", "US" };
        const uint8_t g = Settings::loraRegion();
        snprintf(r.value, sizeof r.value, "%s", N[g < 3 ? g : 0]);
        if (s_bootRegion >= 0 && g != (uint8_t)s_bootRegion) { snprintf(r.hint, sizeof r.hint, "RESTART TO APPLY"); r.valCol = Theme::AMBER; }
        else snprintf(r.hint, sizeof r.hint, "NOW %s", Lora::regionName());
    }
    {
        Row& r = add(CHANNELS, "CHANNELS");
        snprintf(r.value, sizeof r.value, "OPEN >");
        uint8_t mcU, mcM, mtU, mtM;
        Lora::channelCapacity(mcU, mcM, mtU, mtM);
        snprintf(r.hint, sizeof r.hint, "YOUR KEYS: %u MESHCORE, %u MESHTASTIC", (unsigned)mcU, (unsigned)mtU);
        r.valCol = Theme::PURPLE;
    }

    section("NAME");
    {
        Row& r = add(NAME, "NAME");
        r.editable = true;
        if (Settings::loraName()[0]) { snprintf(r.value, sizeof r.value, "%s", Settings::loraName()); r.valCol = Theme::WHITE; }
        else { snprintf(r.value, sizeof r.value, "%s (default)", uiLoraMyName()); r.valCol = dim(Theme::WHITE, 140); }
        snprintf(r.hint, sizeof r.hint, "BOTH NETWORKS. TAP TO TYPE");
    }
    {
        Row& r = add(SHORTNAME, "SHORT NAME");
        r.editable = true;
        char sn[8];
        Lora::MyName::shortName(Settings::loraShortName(), uiLoraMyName(), sn, sizeof sn);
        if (Settings::loraShortName()[0]) { snprintf(r.value, sizeof r.value, "%s", sn); r.valCol = Theme::WHITE; }
        else { snprintf(r.value, sizeof r.value, "%s (default)", sn); r.valCol = dim(Theme::WHITE, 140); }
        snprintf(r.hint, sizeof r.hint, "MESHTASTIC'S FOUR LETTERS");
    }
    {
        Row& r = add(ANNOUNCE, "ANNOUNCE NAME");
        onOff(r, Settings::loraAnnounce());
        snprintf(r.hint, sizeof r.hint, Settings::loraAnnounce() ? "NODEINFO BEFORE THE FIRST TEXT" : "MESHTASTIC SEES A NUMBER");
    }

    section("CHATS");
    {
        Row& r = add(MSGSOUND, "MESSAGE SOUND");
        onOff(r, Settings::loraMsgSound());
        snprintf(r.hint, sizeof r.hint, "A DING FOR A NEW LORA MESSAGE");
    }

    section("DETECTIONS");
    {
        Row& r = add(DETECT, "SQUACHWATCH DETECTIONS");
        const uint8_t d = Settings::loraDetect();
        snprintf(r.value, sizeof r.value, "%s", Settings::loraDetectName());
        r.valCol = d == 0 ? Theme::GREEN : d == 1 ? Theme::AMBER : Theme::RED;
        // What the radios are doing now, not what the switch wishes.
        if (s_resting)  snprintf(r.hint, sizeof r.hint, d == 1 ? "RESTING. WAKES WHEN YOU LEAVE" : "WIFI + BLUETOOTH RESTING NOW");
        else if (d)     snprintf(r.hint, sizeof r.hint, "WAITING: THE RADIO IS BUSY");
        else            snprintf(r.hint, sizeof r.hint, "WIFI + BLUETOOTH SCANNING");
    }
    {
        Row& r = add(ONSCREEN, "ON-SCREEN DETECTIONS");
        onOff(r, Settings::loraAlerts());
        snprintf(r.hint, sizeof r.hint, Settings::loraAlerts() ? "ALERTS POP UP OVER THESE PAGES" : "STILL CAUGHT, LOGGED, COUNTED");
    }

    section("LOOKUPS");
    const bool master = Settings::loraLookups();
    {
        Row& r = add(LOOKUPS, "ONLINE LOOKUPS");
        onOff(r, master);
        snprintf(r.hint, sizeof r.hint, "ASK ONLINE SERVICES ABOUT NODES");
    }
    struct Sub { Id id; const char* label; bool on; const char* what; };
    const Sub subs[3] = {
        { LK_CALL, "  CALLSIGNS",     Settings::loraLookupCall(), "HAMRIG.COM, APRS + MESHCOM" },
        { LK_OGN,  "  OGN",           Settings::loraLookupOgn(),  "THE OGN DATABASE, FANET" },
        { LK_FEED, "  MESHCORE FEED", Settings::loraLookupFeed(), "NAMES MESHCORE ROWS" },
    };
    for (const Sub& s : subs) {
        Row& r = add(s.id, s.label);
        onOff(r, s.on);
        r.dim = !master;
        snprintf(r.hint, sizeof r.hint, "%s", master ? s.what : "NEEDS ONLINE LOOKUPS ON");
    }

    section("CLEAN UP");
    {
        Row& r = add(CLEARCHATS, "CLEAR CHATS");
        const bool c = confirming(CLEARCHATS, 0);
        snprintf(r.value, sizeof r.value, c ? "CONFIRM?" : "CLEAR");
        r.valCol = c ? Theme::RED : Theme::PINK;
        snprintf(r.hint, sizeof r.hint, "%u MESSAGES HELD", (unsigned)Lora::msgCount());
    }
    {
        Row& r = add(FORGETNAMES, "FORGET NAMES");
        const bool c = confirming(FORGETNAMES, 0);
        snprintf(r.value, sizeof r.value, c ? "CONFIRM?" : "FORGET");
        r.valCol = c ? Theme::RED : Theme::PINK;
        snprintf(r.hint, sizeof r.hint, "MESHTASTIC NAMES LEARNED");
    }
}

void buildChannels() {
    s_rowN = 0;
    section("ADD A CHANNEL");
    {
        Row& r = add(ADD_MT, "+ MESHTASTIC");
        r.editable = true;
        snprintf(r.value, sizeof r.value, "ADD");
        r.valCol = Theme::CYAN;
        snprintf(r.hint, sizeof r.hint, "A NAME, THEN ITS BASE64 KEY");
    }
    {
        Row& r = add(ADD_MC, "+ MESHCORE");
        r.editable = true;
        snprintf(r.value, sizeof r.value, "ADD");
        r.valCol = Theme::PINK;
        snprintf(r.hint, sizeof r.hint, "#TAG NEEDS NO KEY; ELSE HEX");
    }
    uint8_t mcU, mcM, mtU, mtM;
    Lora::channelCapacity(mcU, mcM, mtU, mtM);
    const uint8_t n = Lora::channelRowCount();
    for (int pass = 0; pass < 2; pass++) {
        const Lora::Proto want = pass ? Lora::Proto::MESHTASTIC : Lora::Proto::MESHCORE;
        char title[40];
        if (pass) snprintf(title, sizeof title, "MESHTASTIC  %u OF %u YOURS", (unsigned)mtU, (unsigned)mtM);
        else      snprintf(title, sizeof title, "MESHCORE  %u OF %u YOURS", (unsigned)mcU, (unsigned)mcM);
        section(title);
        // Which channel the chip sends on, said on the section line too: the
        // network's default may be a quiet preset that has no row below.
        const uint8_t pick = Lora::Chan::txPick(want);
        {
            Row& sec = s_rows[s_rowN - 1];
            char nm[24];
            Lora::Chan::txName(want, pick, nm, sizeof nm);
            snprintf(sec.hint, sizeof sec.hint, "SENDS ON %s", nm);
            sec.valCol = pass ? Theme::CYAN : Theme::PINK;
        }
        for (uint8_t i = 0; i < n && s_rowN < MAX_ROWS - 1; i++) {
            Lora::ChannelRow c;
            if (!Lora::channelRow(i, c) || c.proto != want) continue;
            Row& r = add(CHAN_ROW, c.name, i);
            Lora::Proto rp; uint8_t ri = 0;
            r.sendMark = Lora::channelRowKey(i, rp, ri) && rp == want && ri == pick;
            char key[24];
            Lora::channelKeyPreview(i, key, sizeof key);
            if (c.builtIn) {
                snprintf(r.value, sizeof r.value, "BUILT IN");
                r.valCol = dim(Theme::WHITE, 130);
                snprintf(r.hint, sizeof r.hint, "%lu FRAMES OPENED", (unsigned long)c.frames);
            } else {
                const bool cf = confirming(CHAN_ROW, i) && !strcmp(s_confirmName, c.name);
                snprintf(r.value, sizeof r.value, cf ? "CONFIRM?" : "REMOVE");
                r.valCol = cf ? Theme::RED : Theme::PINK;
                snprintf(r.hint, sizeof r.hint, "%s%s  %lu FR", key, c.enabled ? "" : " MUTED", (unsigned long)c.frames);
            }
        }
    }
}

void build() { if (s_view == View::MAIN) buildMain(); else buildChannels(); }

int contentHeight() {
    int h = 0;
    for (uint8_t i = 0; i < s_rowN; i++) h += s_rows[i].section ? SEC_H : ROW_H;
    return h;
}
int maxScroll() {
    const int m = s_contentH - (LIST_BOT - LIST_TOP);
    return m > 0 ? m : 0;
}
void clampScroll() {
    int& s = s_scroll[(int)s_view];
    if (s > maxScroll()) s = maxScroll();
    if (s < 0) s = 0;
}

// Which row the edit line is on.
bool editOn(const Row& r) {
    switch (s_edit) {
        case Edit::NAME:      return r.id == NAME;
        case Edit::SHORTNAME: return r.id == SHORTNAME;
        case Edit::CH_NAME:
        case Edit::CH_KEY:    return r.id == (s_addProto == Lora::Proto::MESHCORE ? ADD_MC : ADD_MT);
        default:              return false;
    }
}

// ---- drawing -----------------------------------------------------------------------
void drawPill(TFT_eSPI& t, int y, const char* v, uint16_t col) {
    t.setTextFont(1); t.setTextSize(1);
    const int w = t.textWidth(v) + 12;
    const int x = PILL_R - w;
    t.fillRoundRect(x, y + 6, w, 14, 3, dim(col, 40));
    t.drawRoundRect(x, y + 6, w, 14, 3, col);
    small(t, x + 6, y + 9, v, col, dim(col, 40));
}
int pillX(TFT_eSPI& t, const char* v) { t.setTextFont(1); t.setTextSize(1); return PILL_R - (t.textWidth(v) + 12); }

void drawEditLine(TFT_eSPI& t, const Row& r, int y) {
    const char* lab = s_edit == Edit::CH_NAME ? "NAME" : s_edit == Edit::CH_KEY ? "KEY" : r.label;
    t.setTextFont(1); t.setTextSize(1);
    const uint16_t acc = s_edit == Edit::CH_NAME || s_edit == Edit::CH_KEY
                         ? (s_addProto == Lora::Proto::MESHCORE ? Theme::PINK : Theme::CYAN) : Theme::VAPOR_YELLOW;
    small(t, 10, y + 4, lab, acc, Theme::BG);
    const int x0 = 10 + t.textWidth(lab) + 8, w = PILL_R - x0;
    t.fillRoundRect(x0, y + 2, w, 13, 2, dim(Theme::WHITE, 22));
    t.drawRoundRect(x0, y + 2, w, 13, 2, acc);
    char line[96];
    snprintf(line, sizeof line, "%s%s", s_buf, (s_now / 450) % 2 ? "_" : " ");
    const char* p = line;
    while (*p && t.textWidth(p) > w - 8) p++;           // the tail shows while typing
    small(t, x0 + 4, y + 5, p, Theme::WHITE, dim(Theme::WHITE, 22));
    char hint[48];
    switch (s_edit) {
        case Edit::NAME:      snprintf(hint, sizeof hint, "ENTER SAVES. EMPTY = %s", Squachy::nickname() && *Squachy::nickname() ? "THE NICKNAME" : "SQUACHY"); break;
        case Edit::SHORTNAME: snprintf(hint, sizeof hint, "UP TO 4. ENTER SAVES. EMPTY = DEFAULT"); break;
        case Edit::CH_NAME:   snprintf(hint, sizeof hint, s_addProto == Lora::Proto::MESHCORE ? "#TAG OR A NAME, THEN ENTER" : "THE CHANNEL'S NAME, THEN ENTER"); break;
        default:              snprintf(hint, sizeof hint, s_addProto == Lora::Proto::MESHCORE ? "HEX KEY; EMPTY FOR A #TAG. ENTER" : "BASE64 KEY, THEN ENTER"); break;
    }
    small(t, 10, y + 17, hint, dim(Theme::WHITE, 150), Theme::BG);
}

void drawRows(TFT_eSPI& t) {
    int y = LIST_TOP - s_scroll[(int)s_view];
    for (uint8_t i = 0; i < s_rowN; i++) {
        const Row& r = s_rows[i];
        const int h = r.section ? SEC_H : ROW_H;
        s_rowY[i] = (int16_t)y;
        if (y + h <= LIST_TOP || y >= LIST_BOT) { y += h; continue; }
        if (r.section) {
            small(t, 6, y + 6, r.label, Theme::PURPLE, Theme::BG);
            const int tw = t.textWidth(r.label);
            int lineEnd = W - 6;
            if (r.hint[0]) {
                // Right-aligned, and cut from its end when a long title and
                // a long channel name will not both fit.
                char h[48];
                snprintf(h, sizeof h, "%s", r.hint);
                size_t hn = strlen(h);
                const int room = (W - 6) - (6 + tw + 12);
                while (hn && t.textWidth(h) > room) h[--hn] = '\0';
                if (hn) {
                    const int hw = t.textWidth(h);
                    small(t, W - 6 - hw, y + 6, h, r.valCol, Theme::BG);
                    lineEnd = W - 6 - hw - 6;
                }
            }
            t.drawFastHLine(6 + tw + 6, y + 9, lineEnd - (6 + tw + 6), dim(Theme::PURPLE, 90));
        } else if (editOn(r)) {
            t.fillRect(4, y, W - 8, ROW_H - 1, Theme::BG);
            drawEditLine(t, r, y);
        } else {
            const uint16_t lc = r.dim ? dim(Theme::WHITE, 120) : Theme::WHITE;
            const int px = pillX(t, r.value);
            if (r.sendMark) {
                // The SEND tag after the name: the channel the chip sends on.
                const int tagW = 6 * 4 + 6;
                clipText(t, 10, y + 4, r.label, lc, Theme::BG, px - 16 - tagW - 6);
                char cut[64];
                snprintf(cut, sizeof cut, "%s", r.label);
                size_t cn = strlen(cut);
                while (cn && t.textWidth(cut) > px - 16 - tagW - 6) cut[--cn] = '\0';
                const int tx = 10 + t.textWidth(cut) + 6;
                t.fillRoundRect(tx, y + 3, tagW, 10, 2, Theme::VAPOR_YELLOW);
                small(t, tx + 3, y + 4, "SEND", Theme::BG, Theme::VAPOR_YELLOW);
            } else {
                clipText(t, 10, y + 4, r.label, lc, Theme::BG, px - 16);
            }
            clipText(t, 10, y + 15, r.hint, dim(Theme::WHITE, r.dim ? 90 : 130), Theme::BG, px - 16);
            drawPill(t, y, r.value, r.dim ? dim(r.valCol, 130) : r.valCol);
            t.drawFastHLine(10, y + ROW_H - 1, W - 20, dim(Theme::WHITE, 16));
        }
        y += h;
    }
}

void drawHeader(TFT_eSPI& t) {
    t.fillRect(0, 0, W, HEADER_H, Theme::BG);
    const char* title = s_view == View::MAIN ? "LORA SETTINGS" : "LORA SETTINGS > CHANNELS";
    small(t, 8, 4, title, Theme::WHITE, Theme::BG);
    small(t, W - 6 - 54, 4, "[ BACK ]", Theme::PURPLE, Theme::BG);
    t.drawFastHLine(0, HEADER_H - 1, W, dim(Theme::CYAN, 70));
}

void drawBar(TFT_eSPI& t) {
    t.fillRect(0, BAR_Y, W, 240 - BAR_Y, 0x0862);
    const char* s;
    uint16_t c = dim(Theme::WHITE, 150);
    if (s_status[0] && s_now - s_statusAt < 2500) { s = s_status; c = Theme::VAPOR_YELLOW; }
    else if (s_edit != Edit::NONE) s = "TYPE. ENTER SAVES, DEL ERASES, TAP AWAY = CANCEL";
    else s = "DRAG OR W / S TO SCROLL.  TAP A ROW TO CHANGE IT";
    clipText(t, 6, BAR_Y + 3, s, c, 0x0862, W - 12);
}

void drawScrollbar(TFT_eSPI& t) {
    const int m = maxScroll();
    if (!m) return;
    const int track = LIST_BOT - LIST_TOP;
    const int th = track * track / s_contentH < 12 ? 12 : track * track / s_contentH;
    const int ty = LIST_TOP + (track - th) * s_scroll[(int)s_view] / m;
    t.fillRect(W - 3, LIST_TOP, 2, track, dim(Theme::WHITE, 18));
    t.fillRect(W - 3, ty, 2, th, dim(Theme::CYAN, 160));
}

// ---- doing -------------------------------------------------------------------------
void startEdit(Edit e, const char* initial) {
    s_edit = e;
    snprintf(s_buf, sizeof s_buf, "%s", initial ? initial : "");
    s_bufN = (uint8_t)strlen(s_buf);
}
void cancelEdit() { s_edit = Edit::NONE; s_bufN = 0; s_buf[0] = '\0'; }

uint8_t editMax() {
    switch (s_edit) {
        case Edit::NAME:      return (uint8_t)Lora::MyName::NAME_LEN;
        case Edit::SHORTNAME: return (uint8_t)Lora::MyName::SHORT_LEN;
        case Edit::CH_NAME:   return 23;
        default:              return 80;
    }
}

void commitEdit() {
    switch (s_edit) {
        case Edit::NAME:
            Settings::setLoraName(s_buf);
            Lora::identityChanged();
            status(Settings::loraName()[0] ? "NAME SAVED. THE NEXT TEXT ANNOUNCES IT" : "NAME BACK TO THE DEFAULT");
            cancelEdit();
            break;
        case Edit::SHORTNAME:
            Settings::setLoraShortName(s_buf);
            Lora::identityChanged();
            status(Settings::loraShortName()[0] ? "SHORT NAME SAVED" : "SHORT NAME BACK TO THE DEFAULT");
            cancelEdit();
            break;
        case Edit::CH_NAME: {
            // Spaces end a name on the console (LORA CHAN MT <name> <key>), so
            // they are not part of one here either.
            size_t w = 0;
            for (size_t r = 0; s_buf[r]; r++) if (s_buf[r] != ' ') s_buf[w++] = s_buf[r];
            s_buf[w] = '\0';
            if (!s_buf[0]) { status("A CHANNEL NEEDS A NAME"); return; }
            snprintf(s_addName, sizeof s_addName, "%s", s_buf);
            startEdit(Edit::CH_KEY, "");
            break;
        }
        case Edit::CH_KEY: {
            size_t w = 0;
            for (size_t r = 0; s_buf[r]; r++) if (s_buf[r] != ' ') s_buf[w++] = s_buf[r];
            s_buf[w] = '\0';
            char msg[40];
            const bool ok = Lora::channelAdd(s_addProto, s_addName, s_buf, msg, sizeof msg);
            char line[48];
            snprintf(line, sizeof line, "%s: %s", s_addName, msg);
            status(line);
            // Wiped either way: the key is not kept on this page.
            memset(s_buf, 0, sizeof s_buf);
            if (ok) cancelEdit();
            else    { s_bufN = 0; }
            break;
        }
        default: break;
    }
}

void tapRow(const Row& r) {
    const bool sameConfirm = confirming(r.id, r.arg);
    if (!sameConfirm) s_confirm = NONE;
    switch (r.id) {
        case LISTEN:
            Settings::cycleLoraListen();
            Lora::applyListen(Settings::loraListen());
            break;
        case PRESET:   uiLoraSettingsNextPreset(); break;
        case TX:       Settings::cycleLoraTx(); break;
        case HOPS:     Settings::cycleLoraHops(); break;
        case REGION: {
            const uint8_t g = Settings::loraRegion();
            Settings::setLoraRegion(g == 0 ? 2 : g == 2 ? 1 : 0);      // AUTO -> US -> EU -> AUTO
            break;
        }
        case CHANNELS: s_view = View::CHANNELS; s_scroll[1] = 0; break;
        case NAME:      startEdit(Edit::NAME, Settings::loraName()); break;
        case SHORTNAME: startEdit(Edit::SHORTNAME, Settings::loraShortName()); break;
        case ANNOUNCE:
            Settings::toggleLoraAnnounce();
            if (Settings::loraAnnounce()) Lora::identityChanged();
            break;
        case MSGSOUND: Settings::toggleLoraMsgSound(); break;
        case DETECT:   Settings::cycleLoraDetect(); break;
        case ONSCREEN: Settings::toggleLoraAlerts(); break;
        case LOOKUPS:  Settings::toggleLoraLookups(); break;
        case LK_CALL:  Settings::toggleLoraLookupCall(); break;
        case LK_OGN:   Settings::toggleLoraLookupOgn(); break;
        case LK_FEED:  Settings::toggleLoraLookupFeed(); break;
        case CLEARCHATS:
        case FORGETNAMES:
            if (sameConfirm) {
                s_confirm = NONE;
                if (r.id == CLEARCHATS) { Lora::clearChats(); status("CHATS CLEARED"); }
                else                    { Lora::forgetNames(); status("NAMES FORGOTTEN"); }
            } else { s_confirm = r.id; s_confirmArg = 0; s_confirmAt = s_now; }
            break;
        case ADD_MT:
        case ADD_MC:
            s_addProto = r.id == ADD_MC ? Lora::Proto::MESHCORE : Lora::Proto::MESHTASTIC;
            s_addName[0] = '\0';
            startEdit(Edit::CH_NAME, "");
            break;
        case CHAN_ROW: {
            Lora::ChannelRow c;
            if (!Lora::channelRow(r.arg, c) || c.builtIn) break;
            if (sameConfirm && !strcmp(s_confirmName, c.name)) {
                s_confirm = NONE;
                char line[48];
                snprintf(line, sizeof line, Lora::channelDropRow(r.arg) ? "%s REMOVED" : "%s: NOT REMOVED", c.name);
                status(line);
            } else {
                s_confirm = CHAN_ROW; s_confirmArg = r.arg; s_confirmAt = s_now;
                snprintf(s_confirmName, sizeof s_confirmName, "%s", c.name);
            }
            break;
        }
        default: break;
    }
}

}  // namespace

// ---- the API -------------------------------------------------------------------------
void uiLoraSettingsNextPreset() {
    uint8_t rows[24];
    const uint8_t n = Lora::meshPresetRows(Lora::Proto::MESHTASTIC, rows, 24);
    if (!n) return;
    const int16_t cur = Lora::meshPresetFind(Lora::Proto::MESHTASTIC, Settings::loraMtPreset());
    uint8_t k = 0;
    for (uint8_t i = 0; i < n; i++) if (rows[i] == cur) { k = (uint8_t)((i + 1) % n); break; }
    const Lora::Profile& p = Lora::profile(rows[k]);
    Settings::setLoraMtPreset(strncmp(p.name, "MT ", 3) == 0 ? p.name + 3 : p.name);
    // Re-tuned now, and the new mesh hears the name before the first text.
    Lora::applyListen(Settings::loraListen());
    Lora::identityChanged();
}

const char* uiLoraMyName() {
    return Lora::MyName::longName(Settings::loraName(), Squachy::nickname());
}

void uiLoraSettingsInit(TFT_eSPI& t, bool fresh) {
    if (s_bootRegion < 0) s_bootRegion = Settings::loraRegion();
    if (fresh) { s_view = View::MAIN; s_scroll[0] = s_scroll[1] = 0; }
    cancelEdit();
    s_confirm = NONE;
    s_status[0] = '\0';
    // The rows once now, so a scroll before the first frame knows the height.
    build();
    s_contentH = contentHeight();
    clampScroll();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiLoraSettingsTick(TFT_eSPI& t, uint32_t now, bool resting) {
    s_now = now;
    s_resting = resting;
    build();
    s_contentH = contentHeight();
    // An open edit line is always fully on screen: a NAME row tapped at the
    // bottom edge scrolls up into view rather than typing under the bar.
    if (s_edit != Edit::NONE) {
        int off = 0;
        for (uint8_t i = 0; i < s_rowN; i++) {
            if (!s_rows[i].section && editOn(s_rows[i])) {
                int& sc = s_scroll[(int)s_view];
                if (off < sc) sc = off;
                if (off + ROW_H > sc + (LIST_BOT - LIST_TOP)) sc = off + ROW_H - (LIST_BOT - LIST_TOP);
                break;
            }
            off += s_rows[i].section ? SEC_H : ROW_H;
        }
    }
    clampScroll();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    drawRows(t);
    drawHeader(t);
    drawBar(t);
    drawScrollbar(t);
}

LoraSetHit uiLoraSettingsTap(int x, int y, uint32_t now) {
    s_now = now;
    if (y < HEADER_H + 2) {
        if (x >= W - 80) {
            cancelEdit();
            s_confirm = NONE;
            if (s_view == View::CHANNELS) { s_view = View::MAIN; return LoraSetHit::NONE; }
            return LoraSetHit::BACK;
        }
        return LoraSetHit::NONE;
    }
    if (y >= BAR_Y) return LoraSetHit::NONE;
    for (uint8_t i = 0; i < s_rowN; i++) {
        const Row& r = s_rows[i];
        const int h = r.section ? SEC_H : ROW_H;
        if (y < s_rowY[i] || y >= s_rowY[i] + h) continue;
        if (s_edit != Edit::NONE) {
            // A tap on the edit line itself keeps it; anywhere else cancels.
            if (!editOn(r)) { cancelEdit(); status("NOT SAVED"); }
            return LoraSetHit::NONE;
        }
        if (!r.section) tapRow(r);
        return LoraSetHit::NONE;
    }
    if (s_edit != Edit::NONE) { cancelEdit(); status("NOT SAVED"); }
    return LoraSetHit::NONE;
}

void uiLoraSettingsScrollBy(int px) {
    s_scroll[(int)s_view] += px;
    clampScroll();
}

void uiLoraSettingsKey(char c, uint32_t now) {
    s_now = now;
    if (s_edit == Edit::NONE) {
        if (c == 'w' || c == 'W') uiLoraSettingsScrollBy(-ROW_H);
        else if (c == 's' || c == 'S') uiLoraSettingsScrollBy(ROW_H);
        return;
    }
    if (c == '\b') { if (s_bufN) s_buf[--s_bufN] = '\0'; return; }
    if (c == '\n') { commitEdit(); return; }
    if (c < 32 || c > 126) return;
    if (s_bufN < editMax() && s_bufN < sizeof s_buf - 1) { s_buf[s_bufN++] = c; s_buf[s_bufN] = '\0'; }
}

bool uiLoraSettingsEditing() { return s_edit != Edit::NONE; }

#endif  // (SQUACH_LORA && TDECK_PLUS) || SQUACH_LORA_FAKE
