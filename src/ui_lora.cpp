// SquachWatch-CYD — the LORA screen. See include/ui_lora.h.
#include "ui_lora.h"
#include "ui_scroll.h"
#include "ui_fit.h"
#include "theme.h"
#include "lora_sniffer.h"
#include "lora_profiles.h"
#include "lora_classify.h"
#include "lora_nodes.h"
#include "lora_ident.h"
#include "lora_enrich.h"
#include "lora_feed.h"
#include "lora_meshcore.h"
#include "lora_meshtastic.h"
#include "settings.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace {

// ---- what is showing -------------------------------------------------------

LoraView s_view = LoraView::LIST;
LoraView s_under = LoraView::LIST;   // what the picker is covering, so BACK goes back
int      s_scroll[(int)LoraView::COUNT] = { 0 };
uint16_t s_open = 0;          // PACKET: which frame, newest first
uint32_t s_openTotal = 0;     // ...at which packetTotal(), so it tracks as new ones arrive

// LIST showing only MeshCore adverts. A flag and not a view of its own: it is
// the same ring, the same rows and the same tap, and the only difference is
// which frames are drawn (see advertFrame below).
bool     s_adverts = false;

// CHANMSG: which channel is open. The decoder's own (Proto, index) pair and NOT
// a CHANS row number, because the row order is MeshCore's list and Meshtastic's
// flattened together and it shifts as quiet presets start being listed --
// Lora::channelRowKey is what turns a tapped row into an identity
// (include/lora_sniffer.h), and findChanRow below walks back the other way when
// the header and the mute button need the row again.
Lora::Proto s_msgProto = Lora::Proto::MESHCORE;
uint8_t     s_msgChan = 0;
bool        s_msgAll  = false;   // every channel at once, newest first

// SURVEYCMP: which two runs. -1 until a pair is chosen or defaulted.
int8_t   s_cmpA = -1, s_cmpB = -1;

// SURVEYCMP: which run the delete panel is asking about, or -1 while it is down.
// The panel is modal (see uiLoraTap) and is raised by a HOLD on a run in the
// strip, never by a tap: these views are driven with a thumb while walking.
int8_t   s_delRun = -1;

// ---- geometry --------------------------------------------------------------

const int BODY_TOP = 16;

// A row a finger can hit, and the arithmetic behind the number.
//
// The panel is 800x480 over a 7.0" diagonal, which is 933 pixels of diagonal
// and so 133 pixels to the inch: 0.1905 mm to the physical pixel, 152.4 x
// 91.4 mm of glass (docs/CROWPANEL7.md). The logical canvas is 400x240 blitted
// doubled onto it (SQW_LOGICAL_W in include/crowpanel7_board.h), so ONE LOGICAL
// PIXEL IS 0.381 mm and every number in this file is in those.
//
// These rows were fontHeight() + 6 = 14 px. That is 5.33 mm, which is why the
// channel list could not be hit: the floors everyone publishes are 7.0 mm
// (Apple's 44 pt at 163 ppi) and 7.62 mm (Android's 48 dp at 160 dpi), and for
// a thumb rather than a pointing finger the usability literature asks for
// something closer to 9. fontHeight() + 12 = 20 px is 7.62 mm exactly --
// Android's number -- and it costs this screen four rows of the thirteen it
// used to draw. Fewer rows that can be hit beats more rows that cannot.
//
// The SURVEY view's rows are bigger again (SURVEY_ROW) because that one is read
// at arm's length and driven with a thumb while the other hand holds an
// antenna, and its own button is bigger than that.
int rowH(TFT_eSPI& t) { t.setTextSize(1); return t.fontHeight() + 12; }

// Where the body starts: under the title bar's corner icons and the status
// line. Drawing and hit-testing both come through here, because the old code
// spelled this arithmetic out twice and a third view would have made it three.
int statusBottom(TFT_eSPI& t) { t.setTextSize(1); return BODY_TOP + 1 + t.fontHeight() + 3; }

// Three buttons where every other screen has its bar: BACK, the picker, and
// the survey. The margins are tighter than Theme's own 8 and 8 on purpose.
// Theme::drawButton steps a size-1 label up to size 2 on a panel this wide and
// then keeps it only if it still fits in w - 6; "[ SURVEY ]" is ten characters,
// 120 px at size 2, so the box has to be 126 and Theme's margins give 122. At
// 4 and 4 it is 128 and the whole bar reads at size 2. Half-size lettering on
// the bar of the screen whose complaint was that things were too small to hit
// would be a poor joke.
struct Bar { int y, h, x[3], w; };
Bar bar(int screenW, int screenH) {
    Theme::ButtonBarGeom g = Theme::computeButtonBar(screenW, screenH);
    Bar b;
    b.y = g.y; b.h = g.h;
    const int margin = 4, gap = 4;
    b.w = (screenW - 2 * margin - 2 * gap) / 3;
    for (int i = 0; i < 3; i++) b.x[i] = margin + i * (b.w + gap);
    return b;
}

int bodyBottom(int screenW, int screenH) { return bar(screenW, screenH).y - 4; }

// ---- small shared pieces ---------------------------------------------------

uint16_t protoColor(Lora::Proto p) {
    switch (p) {
        case Lora::Proto::MESHTASTIC: return Theme::GREEN;
        case Lora::Proto::MESHCORE:   return Theme::CYAN;
        case Lora::Proto::LORAWAN:    return Theme::AMBER;
        case Lora::Proto::APRS:       return Theme::VAPOR_PINK;
        case Lora::Proto::MESHCOM:    return Theme::VAPOR_PURPLE;
        case Lora::Proto::FANET:      return Theme::WHITE;
        case Lora::Proto::RETICULUM:  return Theme::PURPLE;
        default:                      return Theme::RED;
    }
}

void ageText(uint32_t now, uint32_t ms, char* out, size_t cap) {
    if (!ms) { snprintf(out, cap, "-"); return; }
    const uint32_t s = (now - ms) / 1000;
    if (s < 60) snprintf(out, cap, "%lus", (unsigned long)s);
    else if (s < 3600) snprintf(out, cap, "%lum", (unsigned long)(s / 60));
    else snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
}

// A duration the way a person reads one at a bench, matching what LORA SURVEY
// prints on the console (src/lora_sniffer.cpp's surveySpan).
void spanText(uint32_t ms, char* out, size_t cap) {
    const uint32_t s = ms / 1000;
    if (s < 120)       snprintf(out, cap, "%lus", (unsigned long)s);
    else if (s < 3600) snprintf(out, cap, "%lum%02lus", (unsigned long)(s / 60), (unsigned long)(s % 60));
    // An hour form, because a run that walks a site for an afternoon read
    // "125m30s" -- seven glyphs where "2h05m" is five, in cells that are now
    // measured to the glyph (drawCmpStrip). The seconds go: at an hour they
    // are noise.
    else               snprintf(out, cap, "%luh%02lum", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60));
}

// snr4 is quarter-dB in an int8_t and C++ truncates toward zero, so -3 quarters
// would print as "0.75" with the sign lost. Sign off first, magnitude on its
// own -- the same fix lora_survey.cpp's verdictLine carries.
void snrText(int32_t q, char* out, size_t cap) {
    const char     sgn = q < 0 ? '-' : '+';
    const uint32_t mag = (uint32_t)(q < 0 ? -q : q);
    snprintf(out, cap, "%c%lu.%02lu", sgn, (unsigned long)(mag / 4), (unsigned long)((mag % 4) * 25));
}

// The name for a row. A node heard exactly once has no tag yet -- every decoder
// sets it after the frame is counted -- so the id in hex stands in rather than
// an empty column, exactly as the console does it.
void nodeName(Lora::Proto p, uint64_t id, const char* tag, char* out, size_t cap) {
    if (tag && tag[0]) { snprintf(out, cap, "%s", tag); return; }
    snprintf(out, cap, "%s:%08lx", Lora::protoShort(p), (unsigned long)(id & 0xFFFFFFFFu));
}

// Right-aligned print: several of the new views put a number at a fixed right
// edge so that rows line up whatever the number's width, which is the whole
// point of a column of dBm readings.
void printRight(TFT_eSPI& t, int right, int y, const char* s) {
    t.setCursor(right - t.textWidth(s), y);
    t.print(s);
}

// Variable-height lists -- the messages, whose blocks are one line or five --
// cannot use uiClampScroll's row arithmetic. They walk back by one item per
// redraw whenever the end of the list was drawn with room left under it: at
// this panel's frame rate that settles before a finger leaves the glass, and it
// needs no measurement of blocks nobody is looking at. The bug it avoids is the
// one include/ui_scroll.h was written for -- a list that scrolls on into empty
// space.
void easeBack(int& scroll, bool hitEnd, int spare) {
    if (hitEnd && spare > 0 && scroll > 0) scroll--;
}

const char* viewTitle(LoraView v) {
    switch (v) {
        case LoraView::PACKET:    return "LORA FRAME";
        case LoraView::NODES:     return "LORA NODES";
        case LoraView::STATS:     return "LORA STATS";
        case LoraView::CHANS:     return "LORA CHANNELS";
        case LoraView::CHANMSG:   return "LORA MESSAGES";
        case LoraView::TRAFFIC:   return "LORA TRAFFIC";
        case LoraView::SURVEY:    return "ANTENNA SURVEY";
        case LoraView::SURVEYCMP: return "SURVEY COMPARE";
        case LoraView::PICK:      return "LORA VIEWS";
        default:                  return "LORA";
    }
}

// WHAT EACH SLOT MEANS, AND THE ONE RULE THAT WAS BROKEN.
//
// Slot 0 is BACK and slot 1 is the picker, IN EVERY VIEW, with no exception.
// Slot 2 is the one thing the view showing is for, and its label always names
// it. That is what this screen's header comment has claimed all along -- and
// slot 1 did not obey it: in PACKET it was "[ < ]" and stepped to the newer
// frame, because uiLoraTap tested for PACKET before it tested for slot 1. So
// the second button was the views button in nine views and something else in
// the tenth, which is the whole of the complaint: "reagiert seltsam und fuehrt
// nicht immer direkt zu den views".
//
// WHAT PACKET LOSES BY THAT, SAID PLAINLY. It had two buttons for stepping
// frames and now has one, so the "newer" direction is gone: slot 2 walks
// towards OLDER frames only. The way back to a newer frame is BACK to the list
// -- which is newest-first, with the frame you were on still in it -- and a tap
// on the row you want. One extra tap, on a list that is already the way you got
// here. The alternatives were worse: a second gesture nobody would find, or a
// wrap from the oldest frame to the newest, which is the same kind of surprise
// this change exists to remove. At the oldest frame the button says so in a
// toast rather than doing nothing (uiLoraTap).
void barLabels(const char* l[3]) {
    l[0] = "[ BACK ]";
    l[1] = "[ VIEWS ]";
    l[2] = "[ SURVEY ]";
    switch (s_view) {
        // Nine characters at size 2 is 108 px in a 128 px box -- it reads at the
        // bar's full size, which "[ OLDER FRAME ]" would not (see bar()).
        case LoraView::PACKET:    l[2] = "[ OLDER ]"; break;
        // From the survey, the third button is the thing the survey is for.
        case LoraView::SURVEY:    l[2] = "[ CMP ]"; break;
        // The picker IS the views, and there is nothing under it to survey that
        // a cell of it does not already reach. Both are drawn as empty space
        // rather than as buttons, and uiLoraTap declines a tap on either.
        case LoraView::PICK:      l[1] = nullptr; l[2] = nullptr; break;
        default: break;
    }
}

// The view's name, drawn here rather than by Theme::drawTitleBar -- that helper
// takes a title and ignores it (src/theme.cpp: `(void)title;`), so this screen
// has been running without one. With ten views behind three buttons, which view
// is showing is not a thing to leave to the reader's memory.
//
// It goes in the band between the corner icons, which is the only row above the
// body that nothing else uses, and it is cleared first: the per-frame repaint
// starts at BODY_TOP, so a title that changed would otherwise print over the
// one before it.
void drawTitle(TFT_eSPI& t, int w, const char* title) {
    const int x0 = Theme::TITLE_ICON_W + 2;
    // Short of BOTH right-hand icons -- the rotate button and, when a PIN is
    // set, the padlock beside it.
    const int x1 = w - 2 * Theme::TITLE_ICON_W - 4;
    if (x1 <= x0) return;
    t.fillRect(x0, 0, x1 - x0, BODY_TOP, Theme::BG);
    t.setTextSize(1);
    t.setTextColor(Theme::WHITE, Theme::BG);
    const int tw = t.textWidth(title);
    t.setCursor(x0 + ((x1 - x0) - tw) / 2, 4);
    t.print(title);
}

void drawBar(TFT_eSPI& t, int w, int h) {
    const Bar b = bar(w, h);
    const char* l[3]; barLabels(l);
    for (int i = 0; i < 3; i++) {
        if (l[i]) { Theme::drawButton(t, b.x[i], b.y, b.w, b.h, l[i], false); continue; }
        // A slot barLabels() gives up is drawn as empty space -- and the space
        // has to be PAINTED. The per-frame repaint stops at bodyBottom and
        // never reaches the bar, so what was actually on the glass was the
        // button the view before had put there: the picker, the one view that
        // gives up two slots, kept showing [ VIEWS ] and [ SURVEY ] over a
        // screen that IS the views, and uiLoraTap declines both. Two live-
        // looking buttons that do nothing, on the screen whose whole complaint
        // was that the views button behaves oddly.
        t.fillRect(b.x[i], b.y, b.w, b.h, Theme::BG);
    }
}

// One line under the title: the mode, where the radio is, and the numbers that
// say whether it is hearing anything.
int drawStatus(TFT_eSPI& t, int w, int y) {
    t.setTextSize(1);
    t.setTextWrap(false);
    char line[96];
    if (!Lora::present()) {
        snprintf(line, sizeof line, "NO MODULE: set K1 to the wireless position and restart");
        t.setTextColor(Theme::AMBER, Theme::BG);
    } else {
        const Lora::Profile& p = Lora::profile(Lora::currentProfile());
        char mhz[12]; Lora::formatMHz(p.freqHz, mhz, sizeof mhz);
        const Lora::Stats& s = Lora::stats();
        snprintf(line, sizeof line, "%s %s %s SF%u  %lu pkts  noise %d", Lora::modeName(Lora::mode()), p.name, mhz,
                 (unsigned)p.sf, (unsigned long)Lora::packetTotal(), (int)s.noiseDbm);
        t.setTextColor(Theme::CYAN, Theme::BG);
    }
    t.fillRect(0, y, w, t.fontHeight() + 2, Theme::BG);
    t.setCursor(4, y + 1);
    t.print(line);
    return y + t.fontHeight() + 3;
}

// ---- LIST: every frame, and the same ring filtered to adverts ---------------

// Is this frame a MeshCore advert? Route and type out of the header plus a
// length check, and no crypto at all -- unlike Lora::summary, which calls
// MeshCore::decode and opens a group message with the channel key. That is what
// makes the test cheap enough to run over frames the filter only walks past.
//
// MeshCore only, and the view says so: an advert is a MeshCore frame type. The
// nearest Meshtastic equivalent is a NODEINFO_APP packet, which arrives
// encrypted on a channel and is not readable without the key, so it is not the
// same thing and is not quietly folded in here.
bool advertFrame(const Lora::Packet& pk, MeshCore::Frame& f) {
    if (pk.proto != Lora::Proto::MESHCORE) return false;
    if (pk.flags & Lora::PK_CRC_ERR) return false;          // a broken frame is not a sighting
    if (!MeshCore::parse(pk.data, pk.len, f)) return false;
    return f.type == MeshCore::TYPE_ADVERT;
}

// The ring rows the ADVERTS filter is showing, and the frame count they were
// found at.
//
// Rebuilt when packetTotal() moves -- a frame landed, and a new advert may
// belong at the top -- or when the scroll moves, and NOT once per redraw. The
// filter has to walk the ring to find its rows; the ring is 256 records of 288
// bytes in PSRAM, 73 kB, and a ring with no adverts in it at all is walked to
// the end. That is not a thing to copy out thirty times a second in order to
// draw nine lines. On this band a frame lands seconds apart and a redraw happens
// every frame, so the walk is rare and the drawing is nine packetAt calls.
//
// Row numbers and not copies of the frames: twelve uint16_t is 24 bytes of
// internal RAM, twelve Packets would be 3.5 kB, and the row number is also what
// a tap needs in order to open PACKET on the right frame.
static const uint8_t ADV_ROWS = 12;      // more than the body holds at rowH()
uint16_t s_advRow[ADV_ROWS];
uint8_t  s_advN = 0;
uint32_t s_advAt = 0;
int      s_advScroll = -1;
bool     s_advEnd = false;    // the walk reached the oldest frame in the ring
uint16_t s_advSeen = 0;       // frames examined, so the empty line can say how many

void advRebuild(int scroll, uint8_t want) {
    s_advN = 0; s_advEnd = false; s_advSeen = 0;
    if (want > ADV_ROWS) want = ADV_ROWS;
    if (!want) return;
    // Six records, 1,728 bytes of stack. A page rather than a record at a time
    // because push() runs on the radio task inside the sniffer's mutex, so every
    // acquisition is a chance to wait on a decoder (include/lora_sniffer.h); six
    // rather than sixteen because the Arduino loop task has an 8 kB stack and
    // this is a drawing path.
    Lora::Packet page[6];
    const uint16_t cap = (uint16_t)(sizeof page / sizeof page[0]);
    uint16_t from = 0, got = 0;
    int skip = scroll < 0 ? 0 : scroll;
    do {
        got = Lora::packetSnapshot(page, cap, from);
        for (uint16_t i = 0; i < got; i++) {
            s_advSeen++;
            MeshCore::Frame f;
            if (!advertFrame(page[i], f)) continue;
            if (skip > 0) { skip--; continue; }
            s_advRow[s_advN++] = (uint16_t)(from + i);
            if (s_advN >= want) return;      // full: the rest of the ring stays unread
        }
        from = (uint16_t)(from + got);
    } while (got == cap);
    s_advEnd = true;
}

void advSync(int scroll, uint8_t want) {
    const uint32_t total = Lora::packetTotal();
    if (s_advScroll == scroll && s_advAt == total) return;
    s_advAt = total; s_advScroll = scroll;
    advRebuild(scroll, want);
}

void advForget() { s_advScroll = -1; s_advAt = 0; s_advN = 0; }

// An advert's own row, rather than Lora::summary's generic line: the advert
// carries a name, what the node says it is, and where it says it is, and those
// are what somebody filtering to adverts came for.
void drawAdvertRow(TFT_eSPI& t, uint32_t now, int w, int y, int rh,
                   const Lora::Packet& pk, const MeshCore::Frame& f) {
    MeshCore::Advert adv;
    const bool body = MeshCore::parseAdvert(f, adv);
    // Bright for an advert off the node's own transmitter, dim for one a
    // repeater passed on. The difference is the whole premise of the survey next
    // door: a relayed copy's signal is the last repeater's link to here.
    const uint16_t col = f.hops ? Theme::VAPOR_PURPLE : Theme::CYAN;
    Theme::drawListRowPanel(t, w, y, rh);
    t.fillRect(4, y + 3, 4, rh - 6, col);
    char age[8]; ageText(now, pk.ms, age, sizeof age);
    char where[8] = "";
    // latE6 x 10 is at most 900,000,000 and lonE6 x 10 at most 1,800,000,000:
    // both inside an int32_t, so the unit change is safe.
    if (body && adv.hasLatLon) Lora::Ident::grid(adv.latE6 * 10, adv.lonE6 * 10, where, sizeof where);
    char hops[10];
    if (f.hops) snprintf(hops, sizeof hops, "%u hop%s", (unsigned)f.hops, f.hops == 1 ? "" : "s");
    else        snprintf(hops, sizeof hops, "direct");
    char line[128];
    snprintf(line, sizeof line, "%4d %4s %-16.16s %-8s %-7s %s", (int)pk.rssi, age,
             !body ? "(unreadable)" : adv.hasName && adv.name[0] ? adv.name : "(no name)",
             body ? MeshCore::nodeTypeName(adv.nodeType) : "", where, hops);
    t.setTextSize(1);
    t.setTextColor(col, Theme::BG);
    t.setCursor(12, y + (rh - t.fontHeight()) / 2);
    t.print(line);
}

void drawAdverts(TFT_eSPI& t, uint32_t now, int w, int top, int bottom) {
    const int rh = rowH(t);
    const int visible = (bottom - top) / rh;
    int& scroll = s_scroll[(int)LoraView::LIST];
    if (scroll < 0) scroll = 0;
    advSync(scroll, (uint8_t)(visible > 0 ? visible : 1));
    if (!s_advN) {
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor(8, top + 20);
        if (!Lora::present())      t.print("");
        else if (!Lora::packetCount()) t.print("nothing heard yet");
        else if (scroll > 0)       t.print("no older adverts");
        else {
            char line[96];
            snprintf(line, sizeof line, "no MeshCore adverts among the last %u frames", (unsigned)s_advSeen);
            t.print(line);
            t.setCursor(8, top + 20 + rh);
            t.print("an advert names a node, its role and where it says it is,");
            t.setCursor(8, top + 20 + 2 * rh);
            t.print("in the clear -- no key opens it and none is needed.");
        }
        // Nothing found where something was expected is also how the list gets
        // back to the top when the frames it was scrolled past have aged out.
        easeBack(scroll, true, 1);
        return;
    }
    int y = top;
    Lora::Packet pk;
    uint8_t drawn = 0;
    for (uint8_t i = 0; i < s_advN && y + rh <= bottom; i++) {
        if (!Lora::packetAt(s_advRow[i], pk)) break;
        MeshCore::Frame f;
        if (!advertFrame(pk, f)) continue;   // the ring moved under us; the next redraw rebuilds
        drawAdvertRow(t, now, w, y, rh, pk, f);
        y += rh; drawn++;
    }
    // No scrollbar: how many adverts the ring holds is not known without walking
    // all of it every redraw, and a thumb drawn from a guess is worse than no
    // thumb at all. The scroll still works -- the walk simply skips further in.
    if (s_advEnd) easeBack(scroll, true, visible - drawn);
}

void drawList(TFT_eSPI& t, uint32_t now, int w, int top, int bottom) {
    if (s_adverts) { drawAdverts(t, now, w, top, bottom); return; }
    const int rh = rowH(t);
    const uint16_t count = Lora::packetCount();
    uiClampScroll(s_scroll[0], count, bottom - top, rh);
    if (!count) {
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor(8, top + 20);
        t.print(Lora::present() ? "nothing heard yet" : "");
        return;
    }
    int y = top;
    Lora::Packet pk;
    for (int idx = s_scroll[0]; idx < count && y + rh <= bottom; idx++, y += rh) {
        if (!Lora::packetAt((uint16_t)idx, pk)) break;
        Theme::drawListRowPanel(t, w, y, rh);
        const uint16_t col = (pk.flags & Lora::PK_CRC_ERR) ? Theme::RED : protoColor(pk.proto);
        t.fillRect(4, y + 3, 4, rh - 6, col);
        char age[8]; ageText(now, pk.ms, age, sizeof age);
        char line[128], sum[96];
        Lora::summary(pk, sum, sizeof sum);
        snprintf(line, sizeof line, "%-4s %4d %4s %s", Lora::protoShort(pk.proto), (int)pk.rssi, age,
                 (pk.flags & Lora::PK_CRC_ERR) ? "CRC ERR" : sum);
        t.setTextSize(1);
        t.setTextColor(col, Theme::BG);
        t.setCursor(12, y + (rh - t.fontHeight()) / 2);
        t.print(line);
    }
    Theme::drawScrollbar(t, w - 6, top, bottom - top, count, (bottom - top) / rh, s_scroll[0]);
}

void drawPacket(TFT_eSPI& t, uint32_t now, int w, int top, int bottom) {
    // The frame keeps its place as newer ones land above it.
    const uint32_t total = Lora::packetTotal();
    uint32_t idx = s_open + (total - s_openTotal);
    if (idx >= Lora::packetCount()) idx = Lora::packetCount() ? Lora::packetCount() - 1 : 0;
    Lora::Packet pk;
    if (!Lora::packetAt((uint16_t)idx, pk)) {
        t.setTextColor(Theme::CYAN, Theme::BG); t.setCursor(8, top + 20); t.print("no frame");
        return;
    }
    t.setTextSize(1);
    const int lh = t.fontHeight() + 2;
    int y = top + 2;
    char mhz[12], bw[8], age[8], line[100];
    Lora::formatMHz(pk.freqHz, mhz, sizeof mhz); Lora::formatBw(pk.bwKhz10, bw, sizeof bw); ageText(now, pk.ms, age, sizeof age);
    t.setTextColor(protoColor(pk.proto), Theme::BG);
    snprintf(line, sizeof line, "%s  #%lu  %s ago", Lora::protoName(pk.proto), (unsigned long)(total - idx), age);
    t.setCursor(4, y); t.print(line); y += lh;
    t.setTextColor(Theme::WHITE, Theme::BG);
    snprintf(line, sizeof line, "%s MHz SF%u %s sync %02x%s%s  %s", mhz, (unsigned)pk.sf, bw, (unsigned)pk.sync,
             (pk.pflags & Lora::PF_INVERT) ? " iq-inv" : "", (pk.flags & Lora::PK_IMPLICIT) ? " implicit" : "",
             pk.profile < Lora::profileCount() ? Lora::profile(pk.profile).name : "custom");
    t.setCursor(4, y); t.print(line); y += lh;
    snprintf(line, sizeof line, "%d dBm  snr %d.%02d  ferr %+ld Hz  %u B  cr4/%u  %lu ms  %s", (int)pk.rssi,
             (int)(pk.snr4 / 4), (int)abs(pk.snr4 % 4) * 25, (long)pk.ferrHz, (unsigned)pk.len, (unsigned)pk.cr,
             (unsigned long)(pk.toaUs / 1000),
             (pk.flags & Lora::PK_CRC_ERR) ? "CRC ERR" : (pk.flags & Lora::PK_CRC_OK) ? "crc ok" : "no crc");
    t.setCursor(4, y); t.print(line); y += lh + 2;
    // The decoded line, wrapped by hand: it is one string and the panel is
    // narrow, and Theme::wrapText's 48-column lines are for bubbles.
    char sum[160];
    Lora::summary(pk, sum, sizeof sum);
    const int cols = (w - 8) / 6;
    t.setTextColor(protoColor(pk.proto), Theme::BG);
    for (const char* p = sum; *p && y + lh <= bottom; ) {
        int n = (int)strlen(p); if (n > cols) n = cols;
        if (n == cols) { int k = n; while (k > cols / 2 && p[k] != ' ') k--; if (k > cols / 2) n = k; }
        char seg[100]; memcpy(seg, p, (size_t)n); seg[n] = '\0';
        t.setCursor(4, y); t.print(seg); y += lh;
        p += n; while (*p == ' ') p++;
    }
    y += 2;
    // The bytes, sixteen a row, as far as the screen goes.
    t.setTextColor(Theme::CYAN, Theme::BG);
    const int per = cols >= 56 ? 16 : 8;
    for (uint8_t i = 0; i < pk.len && y + lh <= bottom; i = (uint8_t)(i + per)) {
        size_t o = (size_t)snprintf(line, sizeof line, "%02x: ", (unsigned)i);
        for (uint8_t j = i; j < pk.len && j < (uint8_t)(i + per) && o + 3 < sizeof line; j++)
            o += (size_t)snprintf(line + o, sizeof line - o, "%02x ", pk.data[j]);
        t.setCursor(4, y); t.print(line); y += lh;
    }
}

void drawNodes(TFT_eSPI& t, uint32_t now, int w, int top, int bottom) {
    const int rh = rowH(t);
    uint8_t idx[Lora::Nodes::CAP];
    const uint8_t count = Lora::nodeOrder(idx, sizeof idx);
    uiClampScroll(s_scroll[2], count, bottom - top, rh);
    if (!count) {
        t.setTextColor(Theme::CYAN, Theme::BG); t.setCursor(8, top + 20); t.print("no transmitters named yet");
        return;
    }
    int y = top;
    Lora::Nodes::Node n;
    for (int i = s_scroll[2]; i < count && y + rh <= bottom; i++, y += rh) {
        if (!Lora::nodeAt(idx[i], n)) break;
        Theme::drawListRowPanel(t, w, y, rh);
        const uint16_t col = protoColor(n.proto);
        t.fillRect(4, y + 3, 4, rh - 6, col);
        char age[8], flags[48], line[128];
        ageText(now, n.lastMs, age, sizeof age);
        Lora::Nodes::flagsText(n, flags, sizeof flags);
        // The signal column is a link measurement only for a frame this node
        // transmitted itself. A row heard only through repeaters gets the
        // relay's figure with a v in front of it: a bare "-73" in this column
        // reads as a distance from here, and for a node eleven hops out it is
        // the distance to the last repeater instead.
        // The grid square takes the place of the bare "@" this column used to
        // hold: six characters where two were, displacing the tail of the
        // flags text, which is the only thing on the line with slack in it.
        // Worth the trade -- "@" said a position had been decoded, a locator
        // says where, in the unit the operators of these networks speak, and it
        // is the node's OWN advertised position and never a licensee's.
        char where[8] = "";
        if (n.hasPos) Lora::Ident::grid(n.latE7, n.lonE7, where, sizeof where);
        // One character for an online answer, beside the one the flags use: a
        // dot when a lookup answered, a dash when it was asked and there was
        // nothing to find or nobody to answer. Blank means nothing was asked,
        // which is what every row says until a switch is turned on.
        Lora::Enrich::Record er;
        const bool asked = Lora::Enrich::cached(n.proto, n.id, er);
        const char* look = !asked ? "" : er.answer == Lora::Enrich::ANS_HIT ? "." : "-";
        // A name from the adverts feed, for a row that gave none on the air --
        // which is most relay-only MeshCore rows, whose name column otherwise
        // reads "repeater" for ever. The ~ says it was not heard here, the same
        // distinction "(claimed)" makes for an extracted callsign, and an
        // ambiguous match falls through to the role: a wrong name is worse than
        // no name, and a name that is only probably right is a wrong name 4.4 %
        // of the time (include/lora_feed.h).
        char named[26] = "";
        if (!n.name[0]) {
            Lora::Feed::Row fr;
            uint8_t cand = 0;
            // MATCH_ONE only, never MATCH_WEAK. A relay-only row pins down one
            // byte of a key, and lora_feed.h works out from 200 live rows that
            // a name printed on that much evidence is wrong between 27 and
            // 77 % of the time. This screen has one line per node and no room
            // to qualify anything, so it says nothing rather than something
            // plausible. LORA FEED on the console shows the candidates.
            if (Lora::Feed::match(n, fr, cand) == Lora::Feed::MATCH_ONE)
                snprintf(named, sizeof named, "~%s", fr.name);
        }
        char sig[10] = "--";
        if (n.directPackets)   snprintf(sig, sizeof sig, "%d", (int)n.rssi);
        else if (n.viaPackets) snprintf(sig, sizeof sig, "v%d", (int)n.viaRssi);
        // The hour bucket tumbles, so a single 0.7 s frame 1.5 s into a fresh
        // one is 46.6 % of it. Under five minutes there is no figure to show --
        // which is the same floor the NF_DUTY flag has always had under it.
        char dut[10] = "--";
        if (Lora::Nodes::dutyKnown(n, now)) {
            const uint16_t duty = Lora::Nodes::dutyPermille(n, now);
            snprintf(dut, sizeof dut, "%u.%u%%", duty / 10, duty % 10);
        }
        snprintf(line, sizeof line, "%-4s %-10.10s %-14.14s %5s %3u %4s %-5s %-6s %s%s%s", Lora::protoShort(n.proto), n.tag,
                 n.name[0] ? n.name : named[0] ? named : Lora::Nodes::roleText(n), sig, (unsigned)n.packets, age,
                 dut, where, look, flags[0] ? "!" : "", flags);
        t.setTextSize(1);
        t.setTextColor(n.flags ? Theme::AMBER : col, Theme::BG);
        t.setCursor(12, y + (rh - t.fontHeight()) / 2);
        t.print(line);
    }
    Theme::drawScrollbar(t, w - 6, top, bottom - top, count, (bottom - top) / rh, s_scroll[2]);
}

// ---- CHANS: the keys the decoders hold -------------------------------------
// Which are known, which have been heard, and which are switched off. A tap
// used to mute one; it opens what came through it now, which is what the whole
// list is for -- a list of locks is not a list of what came through them. Muting
// moved into that view, where there is room for a button a finger can hit
// instead of a hidden strip at the end of a 5 mm row.

int chansListTop(TFT_eSPI& t) { t.setTextSize(1); return statusBottom(t) + t.fontHeight() + 4; }

// ...and where it stops, which is short of the body's own bottom: the sentence
// at the foot of the view saying what a finger can do here is not a row. Here
// rather than spelled out in both places for the same reason as chansListTop --
// the hit test had no bottom at all, so that sentence, and the bare screen
// under the bar with it, opened whichever channel the row arithmetic landed on.
int chansListBottom(TFT_eSPI& t, int bottom) { t.setTextSize(1); return bottom - (t.fontHeight() + 2) - 2; }

void drawChans(TFT_eSPI& t, uint32_t now, int w, int top, int bottom) {
    t.setTextSize(1);
    const int rh = rowH(t);
    const int lh = t.fontHeight() + 2;
    uint8_t mcU = 0, mcM = 0, mtU = 0, mtM = 0;
    Lora::channelCapacity(mcU, mcM, mtU, mtM);
    char head[96];
    // The presets are counted rather than listed: fourteen radio profiles
    // times two key modes is twenty-eight rows of decoder capability, not
    // twenty-eight channels anybody chose. One appears in the list as soon as
    // it opens a frame.
    snprintf(head, sizeof head, "keys: %u/%u MC, %u/%u MT, %u quiet presets, %u held",
             (unsigned)mcU, (unsigned)mcM, (unsigned)mtU, (unsigned)mtM,
             (unsigned)Lora::channelsQuietBuiltIn(), (unsigned)Lora::msgCount());
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(6, top);
    t.print(head);

    const int listTop = chansListTop(t);
    const int listBottom = chansListBottom(t, bottom);
    const uint8_t count = Lora::channelRowCount();
    uiClampScroll(s_scroll[(int)LoraView::CHANS], count, listBottom - listTop, rh);
    int y = listTop;
    Lora::ChannelRow r;
    for (int i = s_scroll[(int)LoraView::CHANS]; i < (int)count && y + rh <= listBottom; i++, y += rh) {
        if (!Lora::channelRow((uint8_t)i, r)) break;
        Theme::drawListRowPanel(t, w, y, rh);
        // Dim for muted, the network's colour once it has opened something,
        // white for a key that has never been used: "known but never heard" is
        // exactly the state a sysop is looking for on this screen.
        const uint16_t col = !r.enabled ? Theme::VAPOR_PURPLE : r.frames ? protoColor(r.proto) : Theme::WHITE;
        t.fillRect(4, y + 3, 4, rh - 6, col);
        char age[8] = "-";
        if (r.lastMs) ageText(now, r.lastMs, age, sizeof age);
        // How many of that channel's messages are actually readable in there,
        // beside how many frames its key has opened. The two differ on purpose
        // and the gap is informative: a GRP_DATA opens and is not text, and the
        // message ring is shared, so a busy channel pushes a quiet one's older
        // lines out (include/lora_msgs.h). It also makes the tap worth making --
        // a row saying 12 is a row with something behind it.
        Lora::Proto kp; uint8_t ki;
        const unsigned held = Lora::channelRowKey((uint8_t)i, kp, ki) ? Lora::msgCountFor(kp, ki) : 0;
        char line[128];
        snprintf(line, sizeof line, "%-4s %-14.14s %02x %-8s %5lu %4u %4s %s", Lora::protoShort(r.proto), r.name,
                 (unsigned)r.hash, r.builtIn ? "built in" : r.derived ? "tag" : r.keyBits ? "key" : "plain",
                 (unsigned long)r.frames, held, age, r.enabled ? "" : "MUTED");
        t.setTextSize(1);
        t.setTextColor(col, Theme::BG);
        t.setCursor(12, y + (rh - t.fontHeight()) / 2);
        t.print(line);
    }
    Theme::drawScrollbar(t, w - 6, listTop, listBottom - listTop, count, (listBottom - listTop) / rh,
                         s_scroll[(int)LoraView::CHANS]);
    t.setTextColor(Theme::AMBER, Theme::BG);
    t.setCursor(6, bottom - lh);
    // With nothing of one's own in the list, the useful sentence is the one
    // that fills it; after that, the one that says what a finger can do.
    t.print(mcU + mtU ? "tap a channel to read it; the console adds keys: LORA CHAN"
                      : "no keys of your own: LORA CHAN GROUP lists ten sets");
}

// ---- CHANMSG: what came through one key ------------------------------------

// The CHANS row this channel is on RIGHT NOW. The row order is MeshCore's list
// and Meshtastic's flattened together and it shifts as quiet presets start being
// listed, so the row number a tap arrived from is re-found by identity on every
// redraw rather than kept and trusted.
bool findChanRow(Lora::Proto p, uint8_t chan, uint8_t& row, Lora::ChannelRow& out) {
    const uint8_t n = Lora::channelRowCount();
    for (uint8_t i = 0; i < n; i++) {
        Lora::Proto rp; uint8_t ri;
        if (!Lora::channelRowKey(i, rp, ri)) continue;
        if (rp != p || ri != chan) continue;
        if (!Lora::channelRow(i, out)) return false;
        row = i;
        return true;
    }
    return false;
}

// The mute button's box: the top right of the header, clear of the identity
// line. 104 x 20 logical pixels is 39 x 7.6 mm -- the same 7.62 mm floor the
// rows use, and wide enough that Theme::drawButton keeps "[ MUTE ]" at size 2.
void msgMuteBox(int w, int top, int& bx, int& by, int& bw, int& bh) {
    bw = 104; bh = 20; bx = w - bw - 6; by = top + 1;
}

void drawChanMsg(TFT_eSPI& t, uint32_t now, int w, int top, int bottom) {
    t.setTextSize(1);
    const int lh = t.fontHeight() + 2;
    uint8_t row = 0;
    Lora::ChannelRow r = {};
    const bool have = !s_msgAll && findChanRow(s_msgProto, s_msgChan, row, r);
    const uint16_t held = s_msgAll ? Lora::msgCount() : Lora::msgCountFor(s_msgProto, s_msgChan);

    // The identity line. A channel is a key and a name, and what it has opened
    // is a different number from what is readable in the ring.
    char head[96];
    if (s_msgAll)
        snprintf(head, sizeof head, "every channel: %u held of %u, %lu pushed out",
                 (unsigned)held, (unsigned)Lora::Msgs::CAP, (unsigned long)Lora::msgDropped());
    else if (have)
        snprintf(head, sizeof head, "%s %-14.14s %s  %lu opened  %u readable", Lora::protoShort(r.proto), r.name,
                 r.builtIn ? "built in" : r.derived ? "tag" : r.keyBits ? "key" : "plain",
                 (unsigned long)r.frames, (unsigned)held);
    else
        snprintf(head, sizeof head, "that channel is no longer in the list");
    // Cut to whatever is left of the row once the mute button has had its
    // corner: the button is drawn after this line and would otherwise paint
    // over the tail of it, which is how "12 readable" became "12 reada".
    int bx = w, by = 0, bw = 0, bh = 0;
    if (have) msgMuteBox(w, top, bx, by, bw, bh);
    const int headMax = (bx - 10) / 6;
    if (headMax > 0 && (int)strlen(head) > headMax) head[headMax] = '\0';
    t.setTextColor(have && !r.enabled ? Theme::VAPOR_PURPLE : Theme::CYAN, Theme::BG);
    t.setCursor(6, top + 2);
    t.print(head);

    // A toggle and not two labels: the button says MUTE always and is drawn
    // pressed while the channel is muted, which is one word to read instead of
    // two to tell apart, and stays at size 2 in both states.
    if (have) Theme::drawButton(t, bx, by, bw, bh, "[ MUTE ]", !r.enabled);
    // Theme::drawButton leaves the text size where it put the label -- size 2
    // on a panel this wide -- and everything below here is set at size 1.
    t.setTextSize(1);

    const int listTop = top + lh + 12;
    if (!held) {
        // What an empty channel means depends entirely on WHY it is empty, and
        // these are different facts about the world, not one message with
        // different wording.
        t.setTextColor(Theme::AMBER, Theme::BG);
        const char* l1 = "";
        const char* l2 = "";
        if (s_msgAll) {
            l1 = "nothing decoded yet.";
            l2 = "a channel with no key opens nothing, and a GRP_DATA is not text";
        } else if (!have) {
            l1 = "the channel is gone from the list.";
            l2 = "LORA CHAN on the console says which keys are held";
        } else if (!r.enabled) {
            l1 = "MUTED: this key is not being tried on anything.";
            l2 = "tap MUTE again and the decoders start offering frames to it";
        } else if (!r.frames) {
            l1 = "this key has never opened a frame.";
            l2 = r.keyBits ? "either nobody is using the channel, or the key is wrong"
                           : "nothing on it has been heard at all yet";
        } else {
            l1 = "frames opened on this key, none of them a message.";
            l2 = "a GRP_DATA is a position or a sensor reading, not text -- and the";
        }
        t.setCursor(8, listTop + 4);      t.print(l1);
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor(8, listTop + 4 + lh); t.print(l2);
        if (have && r.frames && r.enabled) {
            t.setCursor(8, listTop + 4 + 2 * lh);
            t.print("ring of 256 is shared, so a busy channel pushes old lines out");
        }
        return;
    }

    // The messages themselves, newest first. Variable-height blocks: a chat
    // line is usually one row and occasionally five, and a fixed block either
    // truncates the long one or spends the screen on the short ones.
    const int cols = (w - 22) / 6;
    // Short of the foot line, which is drawn last and would otherwise have a
    // message printed over it.
    const int listBottom = bottom - lh - 2;
    int& scroll = s_scroll[(int)LoraView::CHANMSG];
    if (scroll < 0) scroll = 0;
    if (scroll >= (int)held) scroll = held - 1;
    int y = listTop;
    bool hitEnd = false;
    for (uint16_t i = (uint16_t)scroll; y + lh * 2 <= listBottom; i++) {
        Lora::Msgs::Msg m;
        // One message at a time, which for this ring is one lock and one walk of
        // 256 comparisons either way: pageFor does the whole walk inside the
        // sniffer's mutex whatever the page size (include/lora_msgs.h), so a
        // page of one costs a walk and a page of four costs the same walk.
        const bool ok = s_msgAll ? Lora::msgAt(i, m) : (Lora::msgPageFor(s_msgProto, s_msgChan, &m, 1, i) == 1);
        if (!ok) { hitEnd = true; break; }
        const uint16_t col = protoColor(m.proto);
        // A Meshtastic critical alert is text on the same channel as the chat
        // and is labelled rather than mixed in: the port travels with the row
        // for exactly this (include/lora_msgs.h).
        const bool alert = m.proto == Lora::Proto::MESHTASTIC && m.port == Meshtastic::PORT_ALERT;
        char age[8], snr[10], meta[128];
        ageText(now, m.ms, age, sizeof age);
        snrText(m.snr4, snr, sizeof snr);
        // "via" rather than a bare RSSI when the copy came off a relay: the
        // figure is then the relay's link to here and says nothing about how far
        // away the sender is, the same distinction the NODES view makes with a v.
        snprintf(meta, sizeof meta, "%4s  %-18.18s %4d dBm snr %s %s%s", age,
                 m.sender[0] ? m.sender : "-", (int)m.rssi, snr,
                 m.direct ? "" : "via ", alert ? "ALERT" : "");
        t.setTextSize(1);
        t.setTextColor(alert ? Theme::AMBER : col, Theme::BG);
        t.setCursor(8, y);
        t.print(meta);
        y += lh;
        t.setTextColor(alert ? Theme::AMBER : Theme::WHITE, Theme::BG);
        for (const char* p = m.text; *p && y + lh <= listBottom; ) {
            int n = (int)strlen(p); if (n > cols) n = cols;
            if (n == cols) { int k = n; while (k > cols / 2 && p[k] != ' ') k--; if (k > cols / 2) n = k; }
            char seg[80]; if (n > (int)sizeof seg - 1) n = (int)sizeof seg - 1;
            memcpy(seg, p, (size_t)n); seg[n] = '\0';
            t.setCursor(14, y); t.print(seg); y += lh;
            p += n; while (*p == ' ') p++;
        }
        t.drawFastHLine(8, y + 1, w - 18, Theme::PURPLE);
        y += 5;
    }
    easeBack(scroll, hitEnd, listBottom - y);
    // The sender is a claim and the view says so once, at the foot, rather than
    // on every line: a MeshCore group message carries no signature over the name
    // in front of the colon, so anyone on the channel can type anyone's.
    t.setTextColor(Theme::AMBER, Theme::BG);
    t.setCursor(6, bottom - lh + 2);
    t.print("the sender is a claimed name: a group message is not signed");
}

// ---- TRAFFIC: the rate and the occupancy of the air, as they move ----------
//
// WHAT THIS ADDS OVER LIST, because the honest answer is "one thing, and it is
// the only thing that needed a second view". LIST already shows every frame
// newest first with its network, its signal and its decode, and no monitor is
// going to beat that at "what just arrived" -- the newest-frame line here is
// one row of the same thing and exists only so the screen is visibly alive.
// What LIST cannot show at any length is a SHAPE: whether the air is busier
// than it was a minute ago, whether a burst is a burst or the new normal, and
// how much of the time the receiver was listening it was actually occupied.
// Those are differences of the counters over time, so this view keeps its own
// history of them and draws it. It is also the view that says whether an
// antenna run is being taken on a band that is talking at all.
//
// It must not cost the frame. Per redraw it takes one Stats reference, one
// packetAt for the newest frame, and about 170 small fillRects -- src/frame_prof.cpp
// watches the budget and this panel has to keep drawing while a run is recording.

static const uint8_t  TR_BUCKETS = 84;     // 4 px each = 336, the graph's width
static const uint16_t TR_MS      = 2000;   // so the graph spans 2 min 48 s
// One byte for the frames and one for the occupancy: 168 bytes of internal RAM,
// which is what a picture of the last three minutes costs. The counters in
// Lora::Stats are totals since boot and keep no history at all -- a realtime
// monitor differences them itself (include/lora_sniffer.h) -- so this is the
// view's own memory and nothing else can be asked for it.
//
// A bucket cannot overflow a byte: frames saturate at 255 and at SF8/62.5 kHz a
// short frame is a couple of hundred milliseconds of air, so two seconds does
// not hold forty of them, let alone 255. Occupancy is per-mille capped at 255,
// which is 25.5 % of the listening time -- a figure this band does not reach,
// and the graph relabels its own scale when it does.
struct TrBucket { uint8_t frames, permille; };
TrBucket s_tr[TR_BUCKETS];
uint8_t  s_trHead = 0, s_trFilled = 0;
uint32_t s_trAt = 0, s_trPackets = 0, s_trAir = 0, s_trListen = 0;

// The headline is over the last 30 s -- fifteen buckets. Long enough that one
// frame does not swing it, short enough to answer "is it busy NOW".
static const uint8_t TR_WINDOW = 15;

void trafficReset() {
    memset(s_tr, 0, sizeof s_tr);
    s_trHead = 0; s_trFilled = 0; s_trAt = 0;
}

// Called every redraw whatever view is showing, so that opening TRAFFIC after a
// minute on LIST already has a minute of history behind it. Cheap: a compare,
// and once every two seconds three subtractions.
void trafficTick(uint32_t now) {
    const Lora::Stats& s = Lora::stats();
    if (!s_trAt) { s_trAt = now; s_trPackets = s.packets; s_trAir = s.airtimeMs; s_trListen = s.listenMs; return; }
    if (now - s_trAt < TR_MS) return;
    // More than one whole bucket went by without a redraw, which means this
    // screen was not the one showing. The history starts over rather than
    // drawing the hole as quiet air: nothing in the loop takes two seconds, so
    // a gap is always absence and never silence.
    if (now - s_trAt >= 2 * TR_MS) {
        trafficReset();
        s_trAt = now; s_trPackets = s.packets; s_trAir = s.airtimeMs; s_trListen = s.listenMs;
        return;
    }
    const uint32_t dp = s.packets - s_trPackets;
    const uint32_t da = s.airtimeMs - s_trAir;
    const uint32_t dl = s.listenMs - s_trListen;
    // Occupancy against the time the RECEIVER WAS ON a profile, not against the
    // wall clock. In Mode::SURVEY the radio spends most of a bucket on other
    // profiles, and dividing by two seconds there answers a question nobody
    // asked -- the STATS view's LISTEN line makes the same division for the same
    // reason.
    uint32_t pm = dl ? (da * 1000u) / dl : 0;
    if (pm > 255) pm = 255;
    s_tr[s_trHead].frames   = dp > 255 ? 255 : (uint8_t)dp;
    s_tr[s_trHead].permille = (uint8_t)pm;
    s_trHead = (uint8_t)((s_trHead + 1) % TR_BUCKETS);
    if (s_trFilled < TR_BUCKETS) s_trFilled++;
    s_trAt += TR_MS;
    s_trPackets = s.packets; s_trAir = s.airtimeMs; s_trListen = s.listenMs;
}

uint8_t trBucketAt(uint8_t i) {   // i = 0 is the oldest held
    return (uint8_t)((s_trHead + TR_BUCKETS - s_trFilled + i) % TR_BUCKETS);
}

int drawBigNumber(TFT_eSPI& t, int x, int y, const char* caption, const char* value, uint16_t col) {
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(x, y);
    t.print(caption);
    t.setTextSize(3);
    t.setTextColor(col, Theme::BG);
    t.setCursor(x, y + 10);
    t.print(value);
    t.setTextSize(1);
    return y + 10 + 24;
}

void drawTraffic(TFT_eSPI& t, uint32_t now, int w, int top, int bottom) {
    t.setTextSize(1);
    const int lh = t.fontHeight() + 2;
    if (!Lora::present()) {
        t.setTextColor(Theme::AMBER, Theme::BG);
        t.setCursor(8, top + 20);
        t.print("no module: there is no air to watch");
        return;
    }
    // The window's figures, out of the buckets rather than out of the counters,
    // so the number on the screen and the picture under it are the same data.
    uint32_t wf = 0, wpm = 0;
    uint8_t  wn = 0;
    for (uint8_t i = 0; i < s_trFilled && i < TR_WINDOW; i++) {
        const TrBucket& b = s_tr[trBucketAt((uint8_t)(s_trFilled - 1 - i))];
        wf += b.frames; wpm += b.permille; wn++;
    }
    char v1[12], v2[12], v3[12];
    if (wn) snprintf(v1, sizeof v1, "%lu", (unsigned long)((wf * 60u) / (wn * (TR_MS / 1000))));
    else    snprintf(v1, sizeof v1, "-");
    if (wn) snprintf(v2, sizeof v2, "%lu.%lu%%", (unsigned long)(wpm / wn / 10), (unsigned long)((wpm / wn) % 10));
    else    snprintf(v2, sizeof v2, "-");
    snprintf(v3, sizeof v3, "%d", (int)Lora::stats().noiseDbm);
    // THE CAPTION SAYS HOW MUCH TIME IS UNDER THE FIGURE while there is less
    // than the window's worth. TR_WINDOW is fifteen buckets -- thirty seconds --
    // and the comment on it says the window is that long so that "one frame does
    // not swing it". That was true of the divisor and not of the fill: the loop
    // above averages over however many buckets EXIST, so a screen opened four
    // seconds ago divides by two, and one frame in one two-second bucket was
    // printed as "30" in 24-pixel type. Thirty frames a minute, from one frame.
    //
    // The survey next door withholds a median until it has three frames
    // (EVIDENCE_MIN, include/lora_survey.h) because a figure without its
    // evidence is the lie this whole phase exists to avoid. Withholding here
    // would blank the headline for half a minute every time the screen is
    // opened, so instead the figure stands and says what it rests on, in the
    // same "-8s" idiom the graph under it already labels its own left edge with.
    // Fifteen glyphs at size 1 is 90 px; the narrowest of the three columns is
    // 120 px wide (8, 150, 280 across 400), so it fits without moving anything.
    char c1[20], c2[20];
    const unsigned span = (unsigned)wn * (TR_MS / 1000u);
    // wn == 0 needs no qualifier: the figure is already "-", and "-0s" under a
    // dash is two ways of saying nothing.
    if (wn == 0 || wn >= TR_WINDOW) {
        snprintf(c1, sizeof c1, "frames/min");
        snprintf(c2, sizeof c2, "of the air");
    } else {
        snprintf(c1, sizeof c1, "frames/min -%us", span);
        snprintf(c2, sizeof c2, "of the air -%us", span);
    }
    drawBigNumber(t, 8,   top, c1, v1, Theme::WHITE);
    drawBigNumber(t, 150, top, c2, v2, Theme::WHITE);
    drawBigNumber(t, 280, top, "noise dBm", v3, Theme::VAPOR_PURPLE);

    // One row of what LIST does, so the screen is visibly live.
    const int liveY = top + 10 + 24 + 4;
    Lora::Packet pk;
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(6, liveY);
    if (Lora::packetAt(0, pk)) {
        char sum[96], age[8], line[128];
        Lora::summary(pk, sum, sizeof sum);
        ageText(now, pk.ms, age, sizeof age);
        snprintf(line, sizeof line, "last %4s  %-4s %4d dBm  %.44s", age, Lora::protoShort(pk.proto),
                 (int)pk.rssi, sum);
        t.setTextColor((pk.flags & Lora::PK_CRC_ERR) ? Theme::RED : protoColor(pk.proto), Theme::BG);
        t.print(line);
    } else {
        t.print("nothing heard yet");
    }

    // The graph. Two series in one box, told apart the way the SWEEP spectrum
    // tells its hold from its live reading: the dim column behind is how much of
    // the listening time was occupied, the bright tick in front is how many
    // frames landed. Never averaged into one figure -- one long frame and six
    // short ones are different air.
    const int gy = liveY + lh + 2;
    // 26, not 30: the right-hand scale label sits at gx + gw + 4, and at
    // "25.5%" that is five characters, which needs to end inside 400.
    const int gx = 26, gw = TR_BUCKETS * 4;
    const int gh = bottom - gy - 12;
    if (gh < 24) return;
    // Both scales have a floor, and that is the honest part. Autoscaling to
    // whatever the busiest bucket held would draw one frame on a silent band as
    // a full-height column and a quiet afternoon as a crisis.
    //
    // Six frames: at SF8/62.5 kHz -- the profile this board listens on -- a
    // short frame is a couple of hundred milliseconds of air, so a two-second
    // bucket cannot hold many more than that anyway.
    // Ten per-mille: 1 % is the duty-cycle ceiling most of the 868 MHz
    // sub-bands are regulated at, which makes it the natural full scale for
    // received air.
    uint8_t maxF = 6, maxPm = 10;
    for (uint8_t i = 0; i < s_trFilled; i++) {
        const TrBucket& b = s_tr[trBucketAt(i)];
        if (b.frames > maxF) maxF = b.frames;
        if (b.permille > maxPm) maxPm = b.permille;
    }
    t.drawRect(gx - 1, gy, gw + 2, gh + 2, Theme::PURPLE);
    for (uint8_t i = 0; i < s_trFilled; i++) {
        const TrBucket& b = s_tr[trBucketAt(i)];
        const int x = gx + i * 4;
        if (b.permille) {
            int hh = (int)b.permille * gh / maxPm;
            if (hh < 1) hh = 1;
            t.fillRect(x, gy + 1 + gh - hh, 4, hh, Theme::VAPOR_PURPLE);
        }
        if (b.frames) {
            int hh = (int)b.frames * gh / maxF;
            if (hh < 1) hh = 1;
            t.fillRect(x + 1, gy + 1 + gh - hh, 2, hh, Theme::CYAN);
        }
    }
    char lab[16];
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    snprintf(lab, sizeof lab, "%uf", (unsigned)maxF);
    printRight(t, gx - 3, gy, lab);
    t.setCursor(gx - 3 - t.textWidth("0"), gy + gh - t.fontHeight()); t.print("0");
    t.setTextColor(Theme::VAPOR_PURPLE, Theme::BG);
    snprintf(lab, sizeof lab, "%u.%u%%", (unsigned)(maxPm / 10), (unsigned)(maxPm % 10));
    t.setCursor(gx + gw + 4, gy); t.print(lab);
    t.setTextColor(Theme::CYAN, Theme::BG);
    snprintf(lab, sizeof lab, "-%us", (unsigned)(s_trFilled * (TR_MS / 1000)));
    t.setCursor(gx, gy + gh + 4); t.print(lab);
    printRight(t, gx + gw, gy + gh + 4, "now");
    if (!s_trFilled) {
        t.setTextColor(Theme::AMBER, Theme::BG);
        t.setCursor(gx + 8, gy + gh / 2);
        t.print("filling: one column every two seconds");
    }
}

// ---- SURVEY: the antenna test ----------------------------------------------
//
// The screen the whole phase is for, and it is used in one hand while the other
// holds an antenna, walking. So: few rows, big type, the signal drawn rather
// than tabulated, and one button the size of a thumb.
//
// ONLY NODES HEARD FIRST-HAND APPEAR HERE. A relayed node is not on this list at
// all, because the signal in a relayed copy is the last repeater's link to here
// and crediting it to the originator invents a distance. The packets a node
// repeated are not thrown away either -- they are how that repeater's OWN row
// gets its reading, and a repeater is a neighbour too. include/lora_nodes.h has
// the per-network rule and include/lora_survey.h has the reasoning; this view
// applies neither and re-derives nothing, it reads directPackets.
//
// THE MEASUREMENT IS NOT ON THIS SCREEN. What is here is live: who is around
// and how loud, which is what you watch while turning the antenna. The number
// that answers "is this antenna better" is a paired comparison of two runs and
// lives in SURVEYCMP, because an absolute RSSI carries the other station's
// power, antenna and distance -- so a row reading -60 next to a row reading -95
// says nothing about the connector, and this view must not invite that reading.

static const int SURVEY_ROW = 32;   // 12.2 mm: read at arm's length, hit with a thumb

// The live rows, cached.
//
// Rebuilt when packetTotal() moves and not per redraw: the node table holds up
// to 96 rows, walking it is the only way to find the handful heard first-hand,
// and the numbers on those rows change only when a frame arrives. Twelve rows
// because the premise of the feature is that direct reception at one spot is a
// handful of stations -- the same reasoning RUN_NODES = 32 carries in
// include/lora_survey.h -- and 384 bytes of internal RAM is what it costs.
// Widest field first: the same members in declaration order cost 40 bytes a row
// to alignment padding rather than 32, and twelve rows makes that 96 bytes of
// internal RAM for nothing.
struct LiveRow {
    uint64_t    id;
    uint32_t    lastMs;
    char        tag[12];
    int16_t     rssi;
    uint16_t    direct;
    Lora::Proto proto;
    int8_t      snr4;
};
static const uint8_t LIVE_MAX = 12;
LiveRow  s_live[LIVE_MAX];
uint8_t  s_liveN = 0;
uint32_t s_liveAt = 0;
bool     s_liveBuilt = false;

void liveForget() { s_liveN = 0; s_liveBuilt = false; s_liveAt = 0; }

void liveRebuild() {
    // THE ORDER IS THE ORDER THE NEIGHBOURS TURNED UP IN, and new ones append.
    // A list sorted by signal reorders itself while the antenna is being turned,
    // which is the one moment the row under the thumb must not move.
    bool seen[LIVE_MAX];
    memset(seen, 0, sizeof seen);
    // Four rows a page, 416 bytes of stack. A page rather than a nodeAt per row
    // because note() runs on the radio task inside the sniffer's mutex, so every
    // acquisition is a chance to wait on a decoder (include/lora_sniffer.h).
    Lora::Nodes::Node page[4];
    const uint8_t cap = (uint8_t)(sizeof page / sizeof page[0]);
    uint8_t from = 0, got = 0;
    do {
        got = Lora::nodeSnapshot(page, cap, from);
        for (uint8_t i = 0; i < got; i++) {
            const Lora::Nodes::Node& n = page[i];
            if (!n.directPackets) continue;
            uint8_t slot = LIVE_MAX;
            for (uint8_t k = 0; k < s_liveN; k++)
                if (s_live[k].proto == n.proto && s_live[k].id == n.id) { slot = k; break; }
            if (slot == LIVE_MAX) {
                if (s_liveN < LIVE_MAX) slot = s_liveN++;
                else {
                    // Full. The quietest row held gives way, so a louder
                    // neighbour that turns up on the eleventh minute is not shut
                    // out by one that has been at the noise floor all along.
                    uint8_t worst = 0;
                    for (uint8_t k = 1; k < s_liveN; k++)
                        if (s_live[k].rssi < s_live[worst].rssi) worst = k;
                    if (n.rssi <= s_live[worst].rssi) continue;
                    slot = worst;
                }
            }
            s_live[slot].proto = n.proto;
            s_live[slot].id    = n.id;
            memcpy(s_live[slot].tag, n.tag, sizeof s_live[slot].tag);
            s_live[slot].rssi   = n.rssi;
            s_live[slot].snr4   = n.snr4;
            s_live[slot].direct = n.directPackets;
            s_live[slot].lastMs = n.lastMs;
            seen[slot] = true;
        }
        from = (uint8_t)(from + got);
    } while (got == cap);
    // A row whose node has been pushed out of the table -- Lora::Nodes evicts
    // the oldest when its 96 fill -- goes, and the rest close up so the list
    // keeps its order.
    uint8_t keep = 0;
    for (uint8_t k = 0; k < s_liveN; k++)
        if (seen[k]) { if (keep != k) s_live[keep] = s_live[k]; keep++; }
    s_liveN = keep;
}

void liveSync() {
    const uint32_t total = Lora::packetTotal();
    if (s_liveBuilt && total == s_liveAt) return;
    s_liveAt = total; s_liveBuilt = true;
    liveRebuild();
}

// The trend, drawn as columns.
//
// Columns and not a joined line, because the readings are not evenly spaced in
// time -- traffic is sporadic -- and a line drawn between two of them draws a
// walk that was never taken. The x axis is reception number, oldest at the left,
// and the span printed beside it says how long those receptions took.
//
// THE SCALE HAS A FLOOR, and this is the part that matters. Autoscaling to the
// node's own min..max would draw two decibels of thermal noise as a mountain
// range, and an antenna that changed nothing would look dramatic. Ten decibels
// is the smallest window the picture is drawn in: it is a factor of ten in
// received power and the order of what a real antenna change does, so anything
// smaller than that draws as something small. The real span is printed either
// way, which is the same rule the console's ASCII ramp follows -- a trace with
// no scale under it says "it went up" and nothing about by how much.
static const int TREND_FLOOR_DB = 10;

void drawTrend(TFT_eSPI& t, int x, int y, int w, int h,
               const Lora::Survey::Sample* s, uint8_t n, char* span, size_t cap) {
    if (!n) { snprintf(span, cap, "no trend"); return; }
    int16_t lo = s[0].rssi, hi = s[0].rssi;
    for (uint8_t i = 1; i < n; i++) {
        if (s[i].rssi < lo) lo = s[i].rssi;
        if (s[i].rssi > hi) hi = s[i].rssi;
    }
    char sp[14]; spanText(s[n - 1].ms - s[0].ms, sp, sizeof sp);
    snprintf(span, cap, "%d..%d %s", (int)lo, (int)hi, sp);
    int16_t wlo = lo, whi = hi;
    if (whi - wlo < TREND_FLOOR_DB) {
        const int16_t grow = (int16_t)(TREND_FLOOR_DB - (whi - wlo));
        wlo = (int16_t)(wlo - grow / 2);
        whi = (int16_t)(wlo + TREND_FLOOR_DB);
    }
    const int slots = w / 2;
    const uint8_t first = n > slots ? (uint8_t)(n - slots) : 0;   // the newest that fit
    int cx = x;
    for (uint8_t i = first; i < n; i++, cx += 2) {
        int hh = ((int)s[i].rssi - wlo) * h / (whi - wlo);
        if (hh < 1) hh = 1;
        if (hh > h) hh = h;
        t.fillRect(cx, y + h - hh, 1, hh, Theme::CYAN);
    }
    t.drawFastHLine(x, y + h, w, Theme::PURPLE);
}

void surveyButtonBox(int w, int bottom, int& bx, int& by, int& bw, int& bh) {
    bh = 28; bw = w - 8; bx = 4; by = bottom - bh;
}

void drawSurveyRow(TFT_eSPI& t, uint32_t now, int w, int y, const LiveRow& r, int8_t rec) {
    Theme::drawListRowPanel(t, w, y, SURVEY_ROW);
    const uint16_t col = protoColor(r.proto);
    t.fillRect(4, y + 3, 4, SURVEY_ROW - 6, col);
    // The RSSI is the number this screen exists for, so it is the biggest thing
    // on the row: 24 logical pixels is 9 mm of digit, read at arm's length.
    char rs[8]; snprintf(rs, sizeof rs, "%d", (int)r.rssi);
    t.setTextSize(3);
    t.setTextColor(Theme::WHITE, Theme::BG);
    printRight(t, 88, y + 2, rs);
    t.setTextSize(1);
    t.setTextColor(col, Theme::BG);
    t.setCursor(92, y + 18); t.print("dBm");
    // The middle column: the name at size 2, cut to eleven characters. Eleven
    // is what the column holds -- 11 x 12 px ends at 256, and the trend starts
    // at 264 -- and it is also the longest a tag gets (Nodes::Node::tag is
    // twelve bytes with its terminator). Only the hex fallback for a node heard
    // exactly once is ever cut, and the NODES view has it in full.
    char who[14], big[12];
    nodeName(r.proto, r.id, r.tag, who, sizeof who);
    snprintf(big, sizeof big, "%.11s", who);
    t.setTextSize(2);
    t.setTextColor(col, Theme::BG);
    t.setCursor(124, y + 3); t.print(big);
    char snr[10], meta[40];
    snrText(r.snr4, snr, sizeof snr);
    char age[8]; ageText(now, r.lastMs, age, sizeof age);
    snprintf(meta, sizeof meta, "snr %s %ud %s", snr, (unsigned)r.direct, age);
    t.setTextSize(1);
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(124, y + 21); t.print(meta);
    // While a run is recording, how much evidence this node has given it --
    // amber until the median is a middle reading rather than the average of two
    // extremes, which is what EVIDENCE_MIN is (include/lora_survey.h). Right
    // against the trend's left edge rather than after the meta, because the meta
    // is as long as the numbers in it happen to be.
    if (rec >= 0) {
        Lora::Survey::NodeStats ns;
        if (Lora::surveyRunNode((uint8_t)rec, r.proto, r.id, ns)) {
            char f[10]; snprintf(f, sizeof f, "%uf", (unsigned)ns.frames);
            t.setTextColor(ns.enough ? Theme::GREEN : Theme::AMBER, Theme::BG);
            printRight(t, 258, y + 21, f);
        }
    }
    // The trend: 64 readings at two pixels each is 128, which is what the right
    // of the row has once the big number and the name have had theirs.
    Lora::Survey::Sample tr[Lora::Survey::TREND_LEN];
    const uint8_t n = Lora::surveyTrend(r.proto, r.id, tr, (uint8_t)Lora::Survey::TREND_LEN);
    char span[24];
    drawTrend(t, 264, y + 3, 128, 16, tr, n, span, sizeof span);
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    printRight(t, w - 8, y + 21, span);
}

void drawSurvey(TFT_eSPI& t, uint32_t now, int w, int top, int bottom) {
    t.setTextSize(1);
    const int lh = t.fontHeight() + 2;
    const int8_t rec = Lora::surveyRecording();
    int bx, by, bw, bh;
    surveyButtonBox(w, bottom, bx, by, bw, bh);

    // One line for the state of the run, and which line it is depends on what
    // the owner needs to know next.
    char line[128];
    uint16_t col = Theme::CYAN;
    // Nothing here when there is no module: the status line one row up has
    // already said so, and saying it twice reads as two different faults.
    if (!Lora::present()) {
        line[0] = '\0';
    } else if (rec >= 0) {
        Lora::Survey::Run run;
        Lora::surveyRun((uint8_t)rec, run);
        char span[14]; spanText(now - run.startMs, span, sizeof span);
        if (run.hopping) {
            // The louder fact wins the line. A hopping radio hears any one
            // network for a fraction of its wall time, and which fraction
            // depends on where the walk happened to be -- so this run cannot be
            // compared with a parked one and the comparison will refuse it.
            // The label is cut to ten characters here and nowhere else: the
            // warning is the part of this line that must not fall off the edge.
            snprintf(line, sizeof line, "REC \"%.10s\" %s -- HOPPING: park with LORA FOCUS", run.label, span);
            col = Theme::AMBER;
        } else {
            snprintf(line, sizeof line, "REC \"%.16s\"  %s  %lu frames  %u nodes", run.label, span,
                     (unsigned long)run.frames, (unsigned)run.nodes);
            col = Theme::RED;
        }
    } else if (Lora::mode() == Lora::Mode::OFF) {
        snprintf(line, sizeof line, "the radio is OFF -- tap the line above until it says FOCUS");
        col = Theme::AMBER;
    } else if (Lora::mode() != Lora::Mode::FOCUS) {
        // THE WORD ON THE LINE ABOVE IS NOT THIS FEATURE. Lora::Mode::SURVEY is
        // the radio walking its profile table, and the status line calls it
        // SURVEY -- so somebody standing on this screen with "SURVEY" a row
        // above it has every reason to think the radio is set up for what this
        // view does. It is the exact opposite: a hopping radio hears any one
        // network for a fraction of its wall time, so two runs taken under it
        // are not comparable and Compare::incomparable will refuse them. The
        // collision is named at the top of include/lora_survey.h; this is the
        // one place on the device where it could cost a walk round the block.
        snprintf(line, sizeof line, "mode %s: not parked on one profile -- LORA FOCUS <n> first",
                 Lora::modeName(Lora::mode()));
        col = Theme::AMBER;
    } else if (const uint8_t runs = Lora::surveyRunCount()) {
        // The hold is where a gesture goes to be discovered: nothing on the glass
        // can advertise it, the way a button advertises itself, so the one line
        // this view already spends on the runs says it. 62 characters at four
        // runs, which is inside the 65 this panel's width holds (see the other
        // lines in this view).
        snprintf(line, sizeof line, "%u run%s held. CMP is the measurement; hold one there to drop it",
                 (unsigned)runs, runs == 1 ? "" : "s");
    } else {
        snprintf(line, sizeof line, "START, walk, STOP, swap the antenna, START again -- then CMP");
    }
    t.setTextColor(col, Theme::BG);
    t.setCursor(6, top + 1);
    t.print(line);

    const int listTop = top + lh + 2;
    const int listBottom = by - 4;
    if (Lora::present()) liveSync();
    if (!s_liveN) {
        t.setTextColor(Theme::AMBER, Theme::BG);
        t.setCursor(8, listTop + 6);
        t.print(Lora::present() ? "nothing heard first-hand yet." : "");
        if (Lora::present()) {
            t.setTextColor(Theme::CYAN, Theme::BG);
            t.setCursor(8, listTop + 6 + lh);
            t.print("every NODES row with a v in its signal column is a relayed copy:");
            t.setCursor(8, listTop + 6 + 2 * lh);
            t.print("that reading is the repeater's link to here, not the");
            t.setCursor(8, listTop + 6 + 3 * lh);
            t.print("node's, so it cannot be on a list about this antenna.");
        }
    } else {
        const int rows = (listBottom - listTop) / SURVEY_ROW;
        uiClampScroll(s_scroll[(int)LoraView::SURVEY], s_liveN, listBottom - listTop, SURVEY_ROW, 1);
        int y = listTop;
        for (int i = s_scroll[(int)LoraView::SURVEY]; i < (int)s_liveN && y + SURVEY_ROW <= listBottom;
             i++, y += SURVEY_ROW)
            drawSurveyRow(t, now, w, y, s_live[i], rec);
        Theme::drawScrollbar(t, w - 4, listTop, listBottom - listTop, s_liveN, rows,
                             s_scroll[(int)LoraView::SURVEY]);
    }

    // 392 x 28 logical pixels: 149 x 10.7 mm. It is pressed with a thumb while
    // the other hand is holding an antenna, and it is drawn filled while a run
    // is recording so that the state is the button rather than a word beside it.
    Theme::drawButton(t, bx, by, bw, bh, rec >= 0 ? "[ STOP ]" : "[ START ]", rec >= 0, 3);
}

// ---- SURVEYCMP: two runs against each other, which is the measurement ------

static const int PAIR_FULL_DB = 10;

// One box per run slot, across the width. Sized from Survey::RUNS rather than
// from the four there happen to be, so the strip follows the store rather than
// having to be found and fixed if it ever holds more.
void cmpStripBox(int w, int top, uint8_t i, int& bx, int& by, int& bw, int& bh) {
    const int margin = 4, gap = 4, n = (int)Lora::Survey::RUNS;
    bw = (w - 2 * margin - (n - 1) * gap) / n;
    bh = 30;
    bx = margin + (int)i * (bw + gap);
    by = top + 1;
}

// A default pair: the two most recent FINISHED runs, the older of them as A, so
// that B is the antenna on the board now and "B BETTER" reads forwards. A run
// still recording is not defaulted to -- its figures move while you look at them,
// which is useful and is what the SURVEY view is for -- but it can be chosen.
void cmpDefault() {
    int8_t   newest = -1, second = -1;
    uint32_t newestAt = 0, secondAt = 0;
    for (uint8_t i = 0; i < Lora::Survey::RUNS; i++) {
        Lora::Survey::Run r;
        if (!Lora::surveyRun(i, r) || !r.stopMs) continue;
        if (newest < 0 || r.stopMs > newestAt) {
            second = newest;     secondAt = newestAt;
            newest = (int8_t)i;  newestAt = r.stopMs;
        } else if (second < 0 || r.stopMs > secondAt) {
            second = (int8_t)i;  secondAt = r.stopMs;
        }
    }
    s_cmpB = newest;
    s_cmpA = second;
}

void cmpPick(uint8_t run) {
    // Tapping a run makes it B and pushes the old B down to A, so walking the
    // strip walks the comparison forward: the run you touched last is always the
    // one being judged, which is the antenna currently on the connector.
    if (s_cmpB == (int8_t)run) return;
    s_cmpA = s_cmpB;
    s_cmpB = (int8_t)run;
}

void drawCmpStrip(TFT_eSPI& t, uint32_t now, int w, int top) {
    const int8_t rec = Lora::surveyRecording();
    for (uint8_t i = 0; i < Lora::Survey::RUNS; i++) {
        int bx, by, bw, bh;
        cmpStripBox(w, top, i, bx, by, bw, bh);
        Lora::Survey::Run r;
        const bool used = Lora::surveyRun(i, r);
        const char role = rec == (int8_t)i ? '*' : s_cmpA == (int8_t)i ? 'A' : s_cmpB == (int8_t)i ? 'B' : ' ';
        const uint16_t col = rec == (int8_t)i ? Theme::RED
                           : s_cmpB == (int8_t)i ? Theme::GREEN
                           : s_cmpA == (int8_t)i ? Theme::VAPOR_PURPLE
                           : used ? Theme::CYAN : Theme::PURPLE;
        t.fillRect(bx, by, bw, bh, Theme::BG);
        t.drawRect(bx, by, bw, bh, col);
        t.setTextSize(2);
        t.setTextColor(col, Theme::BG);
        char c[2] = { role, '\0' };
        t.setCursor(bx + 3, by + 3); t.print(c);
        t.setTextSize(1);
        // WHAT THE LABEL GETS, measured from the cell instead of guessed. The
        // line starts at bx + 18 -- right of the size-2 role letter -- and stops
        // three pixels short of the border, so on the CrowPanel's 95 px box it
        // is 74 px, twelve glyphs of the fixed-pitch built-in font (ui_fit.h),
        // two of which are the "N " that names the run: the same number the
        // DELETE panel and LORA SURVEY DROP use, and the only way to say which
        // box you mean. Ten for the label, against a flat nine before -- and
        // nine was flat whatever the panel measured.
        //
        // The cut is a MIDDLE cut, which is the part that matters more than the
        // extra glyph. Labels are typed by a person and people prefix by
        // category: "dipole at the mast" and "dipole at the balcony rail" agree
        // for thirteen characters, and the simulator's own fixture is "sim walk
        // A" and "sim walk B". Cut from the head, five different antennas draw
        // the same cell in the one strip whose whole job is telling two runs
        // apart. Head and tail with '>' between them keeps what differs.
        const int cell = UiFit::chars(bw - 18 - 3);
        char l1[32], l2[24], lab[24];
        if (!used) {
            snprintf(l1, sizeof l1, "%u -", (unsigned)(i + 1));
            l2[0] = '\0';
        } else {
            char span[14]; spanText((r.stopMs ? r.stopMs : now) - r.startMs, span, sizeof span);
            UiFit::fitMid(lab, sizeof lab, r.label, cell - 2);
            snprintf(l1, sizeof l1, "%u %s", (unsigned)(i + 1), lab);
            // THE SECOND LINE IS MEASURED TOO. It starts at bx + 3 and stops
            // three short of the border: bw - 6, fourteen glyphs on the
            // CrowPanel's 95 px cell, eleven on the CYD's 75. Frames and span
            // grow with a long walk -- "12345f 42n 1h05m" is sixteen -- and
            // wrap is off for this screen, so an unmeasured line ran through
            // the border. The node count is the first to go: frames and span
            // are what say how much evidence a run holds and how long it took,
            // and the nodes are on the compare view proper.
            const int room = UiFit::chars(bw - 6);
            snprintf(l2, sizeof l2, "%luf %un %s", (unsigned long)r.frames, (unsigned)r.nodes, span);
            if ((int)strlen(l2) > room) snprintf(l2, sizeof l2, "%luf %s", (unsigned long)r.frames, span);
            if ((int)strlen(l2) > room) { char full[24]; memcpy(full, l2, sizeof full); UiFit::fitHead(l2, sizeof l2, full, room); }
        }
        t.setCursor(bx + 18, by + 4);  t.print(l1);
        t.setCursor(bx + 3,  by + 18); t.print(l2);
    }
}

void drawPairBar(TFT_eSPI& t, int x, int y, int w, int h, int16_t dRssi) {
    // The paired delta as a bar out of a zero line: right and green is B
    // louder, left and red is B quieter. Ten decibels each way fills it, and a
    // delta past that gets an arrow rather than a longer bar -- the scale is
    // what makes two rows comparable at a glance, and a bar that grows off the
    // end of its own scale is not a scale.
    const int mid = x + w / 2;
    t.drawFastVLine(mid, y, h, Theme::PURPLE);
    int px = (int)dRssi * (w / 2) / PAIR_FULL_DB;
    const bool over = px > w / 2 || px < -(w / 2);
    if (px > w / 2)  px = w / 2;
    if (px < -(w / 2)) px = -(w / 2);
    const uint16_t col = dRssi > 0 ? Theme::GREEN : dRssi < 0 ? Theme::RED : Theme::CYAN;
    if (px > 0)      t.fillRect(mid + 1, y + 1, px, h - 2, col);
    else if (px < 0) t.fillRect(mid + px, y + 1, -px, h - 2, col);
    if (over) {
        t.setTextSize(1);
        t.setTextColor(col, Theme::BG);
        t.setCursor(dRssi > 0 ? x + w - 6 : x, y);
        t.print(dRssi > 0 ? ">" : "<");
    }
}

void drawSurveyCmp(TFT_eSPI& t, uint32_t now, int w, int top, int bottom) {
    t.setTextSize(1);
    const int lh = t.fontHeight() + 2;
    if (s_cmpA < 0 || s_cmpB < 0) cmpDefault();
    drawCmpStrip(t, now, w, top);
    int y = top + 32 + 2;

    if (Lora::surveyRunCount() < 2 || s_cmpA < 0 || s_cmpB < 0) {
        t.setTextColor(Theme::AMBER, Theme::BG);
        t.setCursor(8, y + 4);
        t.print("two finished runs are needed: this is a PAIRED measurement.");
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor(8, y + 4 + lh);
        t.print("an absolute RSSI carries the other station's power, antenna and");
        t.setCursor(8, y + 4 + 2 * lh);
        t.print("distance, so the same node compared against itself across");
        t.setCursor(8, y + 4 + 3 * lh);
        t.print("two runs is the only thing that measures the connector.");
        t.setCursor(8, y + 4 + 5 * lh);
        t.print("SURVEY: START, walk, STOP. Swap the antenna. START, walk, STOP.");
        t.setCursor(8, y + 4 + 6 * lh);
        t.print("Then tap two runs above -- the last one tapped is B.");
        // Both of the things a finger cannot guess: the hold that drops a run,
        // and the console line that names one. This is the only view where there
        // is room to say them.
        t.setCursor(8, y + 4 + 7 * lh);
        t.print("HOLD a run to drop it. LORA SURVEY LABEL names one.");
        return;
    }

    Lora::Survey::Compare c;
    if (!Lora::surveyCompare((uint8_t)s_cmpA, (uint8_t)s_cmpB, c)) {
        t.setTextColor(Theme::AMBER, Theme::BG);
        t.setCursor(8, y + 4);
        t.print("no such pair of runs");
        return;
    }
    // The verdict in one glance, and the dB figure ONLY where it means
    // something. V_BETTER and V_WORSE are the two verdicts that rest on the
    // median delta, so those get the number in 24-pixel type beside the word.
    // TOO THIN, UNEVEN RUNS, MIXED and NO DATA do not: "+3 dB" that large next
    // to "TOO THIN" is exactly the reading this feature exists to prevent. The
    // number is still in the evidence line under it for anyone who wants it.
    const uint16_t vcol = c.verdict == Lora::Survey::V_BETTER ? Theme::GREEN
                        : c.verdict == Lora::Survey::V_WORSE  ? Theme::RED
                        : c.verdict == Lora::Survey::V_SAME   ? Theme::CYAN
                        : c.verdict == Lora::Survey::V_NO_DATA ? Theme::VAPOR_PURPLE
                                                               : Theme::AMBER;
    t.setTextSize(3);
    t.setTextColor(vcol, Theme::BG);
    t.setCursor(6, y);
    t.print(Lora::surveyVerdictText(c.verdict));
    if (c.verdict == Lora::Survey::V_BETTER || c.verdict == Lora::Survey::V_WORSE) {
        char db[12]; snprintf(db, sizeof db, "%+d dB", (int)c.medDRssi);
        t.setTextColor(Theme::WHITE, Theme::BG);
        printRight(t, w - 6, y, db);
    }
    y += 26;

    // The evidence, always, in words -- the function exists so that no caller
    // can forget to print it (include/lora_survey.h).
    char words[200];
    Lora::surveyVerdictLine(c, words, sizeof words);
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    const int cols = (w - 12) / 6;
    int lines = 0;
    for (const char* p = words; *p && lines < 3; lines++) {
        int n = (int)strlen(p); if (n > cols) n = cols;
        if (n == cols) { int k = n; while (k > cols / 2 && p[k] != ' ') k--; if (k > cols / 2) n = k; }
        char seg[80]; if (n > (int)sizeof seg - 1) n = (int)sizeof seg - 1;
        memcpy(seg, p, (size_t)n); seg[n] = '\0';
        t.setCursor(6, y); t.print(seg); y += lh;
        p += n; while (*p == ' ') p++;
    }
    y += 2;

    // The rows behind the figure, biggest improvement first. A row whose
    // evidence is thin on either side is SHOWN and says so instead of carrying a
    // confident bar: three frames from one node is a reading, not a measurement.
    const int rh = rowH(t);
    Lora::Survey::Pair pr[4];
    int& scroll = s_scroll[(int)LoraView::SURVEYCMP];
    if (scroll < 0) scroll = 0;
    uint8_t from = (uint8_t)scroll, drawn = 0;
    bool hitEnd = false;
    while (y + rh <= bottom) {
        const uint8_t got = Lora::surveyPairs((uint8_t)s_cmpA, (uint8_t)s_cmpB, pr,
                                              (uint8_t)(sizeof pr / sizeof pr[0]), from);
        if (!got) { hitEnd = true; break; }
        for (uint8_t i = 0; i < got && y + rh <= bottom; i++, y += rh, drawn++) {
            const Lora::Survey::Pair& p = pr[i];
            Theme::drawListRowPanel(t, w, y, rh);
            char who[16]; nodeName(p.proto, p.id, p.tag, who, sizeof who);
            const int ty = y + (rh - t.fontHeight()) / 2;
            t.setTextSize(1);
            t.setTextColor(p.enough ? protoColor(p.proto) : Theme::AMBER, Theme::BG);
            t.setCursor(8, ty); t.print(who);
            char db[12], fr[16];
            snprintf(db, sizeof db, "%+d dB", (int)p.dRssi);
            snprintf(fr, sizeof fr, "%u/%u", (unsigned)p.framesA, (unsigned)p.framesB);
            printRight(t, 170, ty, db);
            if (p.enough) drawPairBar(t, 180, y + 4, 160, rh - 8, p.dRssi);
            else { t.setTextColor(Theme::AMBER, Theme::BG); t.setCursor(184, ty); t.print("thin"); }
            t.setTextColor(Theme::WHITE, Theme::BG);
            printRight(t, w - 8, ty, fr);
        }
        from = (uint8_t)(from + got);
        if (got < (uint8_t)(sizeof pr / sizeof pr[0])) { hitEnd = true; break; }
    }
    easeBack(scroll, hitEnd, bottom - y);
    if (!drawn) {
        t.setTextColor(Theme::AMBER, Theme::BG);
        t.setCursor(8, y + 2);
        t.print("no node appears in both runs");
    }
}

// ---- dropping a run, and resetting the survey ------------------------------
//
// WHY THIS EXISTS. A survey holds four run slots and the fifth start reuses the
// oldest FINISHED one (include/lora_survey.h's start()). Until now nothing on
// the glass could empty a slot and the only console command was LORA SURVEY
// CLEAR, which takes everything -- so a run started by mistake, which happened
// twice on the bench on 2026-09-26, sat in the list with two frames in it and
// confused every comparison drawn afterwards. There was no way to get the slot
// back short of throwing away the runs that were worth keeping.
//
// WHY A HOLD AND A PANEL, AND NOT A BUTTON. A tap is wrong here: this view is
// read at arm's length and driven with a thumb while the other hand holds an
// antenna, and the run boxes in the strip are also the control that CHOOSES the
// pair -- so the same 94x30 box a walking thumb aims at to pick B cannot also
// be the one that destroys it. The firmware already has an idiom for exactly
// this: the raw-scan screen long-presses a result row (500 ms, 12 px of
// tolerance) and puts up a modal panel whose buttons commit -- src/main.cpp's
// RAWSCAN case and ui_rawscan.cpp's drawConfirmPanel(). This follows it, with
// the same threshold and the same "the touch that opened the panel cannot also
// press it" rule, so there is one gesture on the device for "I mean this", not
// two.
//
// THE BUTTON ORDER IS BY FREQUENCY AND BY HARM. A thumb comes up from the
// bottom of the panel, so the bottom button is the one that gets pressed most
// and should cost least if it is pressed by accident: DELETE THIS RUN, which
// loses one run. CANCEL sits in the middle, where a miss in either direction
// lands on it. RESET is furthest away at the top, because it takes all four
// runs and the live trend, and nobody does it twice a session.
static const int DEL_BTN_H = 26;

void delPanelBox(int screenW, int screenH, int& px, int& py, int& pw, int& ph) {
    pw = screenW - 40;
    if (pw > 300) pw = 300;
    // Two text rows, three buttons, the gaps between them, and a margin.
    ph = 8 + 2 * 11 + 3 * DEL_BTN_H + 2 * 8 + 10;
    px = (screenW - pw) / 2;
    py = (screenH - ph) / 2;
    if (py < BODY_TOP) py = BODY_TOP;
}

// 0 = RESET (top), 1 = CANCEL, 2 = DELETE (bottom). See the order's reasoning
// above; the drawing and the hit test both come through here so the two cannot
// drift, which is the rule ui_rawscan.cpp's confirmRects() was written for.
void delBtnBox(int screenW, int screenH, uint8_t i, int& bx, int& by, int& bw, int& bh) {
    int px, py, pw, ph;
    delPanelBox(screenW, screenH, px, py, pw, ph);
    const int margin = 10, gap = 8;
    bw = pw - 2 * margin;
    bh = DEL_BTN_H;
    bx = px + margin;
    by = py + ph - margin - (3 - (int)i) * bh - (2 - (int)i) * gap;
}

void drawDelPanel(TFT_eSPI& t, int w, int h, uint32_t now) {
    if (s_delRun < 0) return;
    Lora::Survey::Run r;
    if (!Lora::surveyRun((uint8_t)s_delRun, r)) { s_delRun = -1; return; }
    int px, py, pw, ph;
    delPanelBox(w, h, px, py, pw, ph);
    t.fillRoundRect(px, py, pw, ph, 6, Theme::BG);
    t.drawRoundRect(px, py, pw, ph, 6, Theme::RED);
    t.setTextSize(1);
    t.setTextWrap(false);
    const bool rec = Lora::surveyRecording() == s_delRun;
    char line[80];
    // DELETE on the glass and DROP on the console, each matching the words
    // around it: the button under this asks in the owner's own word ("loeschen"),
    // and LORA SURVEY DROP matches LORA CHAN DROP, which is the console file's
    // idiom for taking one row out of a list.
    snprintf(line, sizeof line, "DELETE RUN %u?", (unsigned)(s_delRun + 1));
    t.setTextColor(Theme::RED, Theme::BG);
    t.setCursor(px + 10, py + 8);
    t.print(line);
    // What is about to go, in the same figures the strip and the console print,
    // so that the panel is enough to decide on: the label, the evidence, and --
    // the one that changes the answer -- whether this is the run RECORDING.
    char span[14]; spanText((r.stopMs ? r.stopMs : now) - r.startMs, span, sizeof span);
    // Cut to the panel rather than to the screen. Wrap is off here, so a long
    // line would print straight through the border and out the other side --
    // which on a 240 px panel, where the box is 200 wide and this line can reach
    // 32 characters, it does. Six pixels a glyph at size 1, the same arithmetic
    // the frame view and the comparison use for their own wrapping.
    //
    // The REST of the line is built first so the label can be given what is
    // actually left, instead of the flat twelve characters this used to take
    // off a 23-character label. On the CrowPanel the panel is 300 px wide, 46
    // glyphs, and the counts and span are about twelve of them: the label gets
    // its whole 23 when nothing else is competing, and gives way -- not the
    // other way round -- to RECORDING, which is eleven characters and is the
    // one word on this panel that changes the answer.
    char rest[40];
    snprintf(rest, sizeof rest, "  %luf %un %s%s", (unsigned long)r.frames,
             (unsigned)r.nodes, span, rec ? "  RECORDING" : "");
    const int cols = (pw - 20) / 6;
    int room = cols - 2 - (int)strlen(rest);   // 2 for the quotes around it
    if (room < 6) room = 6;                    // a panel this narrow is not this feature's problem
    char shown[28];
    UiFit::fitMid(shown, sizeof shown, r.label, room);
    snprintf(line, sizeof line, "\"%s\"%s", shown, rest);
    if (cols > 0 && (int)strlen(line) > cols) line[cols] = '\0';
    t.setTextColor(rec ? Theme::AMBER : Theme::WHITE, Theme::BG);
    t.setCursor(px + 10, py + 8 + 11);
    t.print(line);
    int bx, by, bw, bh;
    char lab[24];
    delBtnBox(w, h, 0, bx, by, bw, bh);
    // The count, because "all" on its own does not say how much is about to go,
    // and this is the button that also takes the live trend with it.
    const uint8_t held = Lora::surveyRunCount();
    snprintf(lab, sizeof lab, "RESET ALL %u RUN%s", (unsigned)held, held == 1 ? "" : "S");
    Theme::drawButton(t, bx, by, bw, bh, lab, false);
    delBtnBox(w, h, 1, bx, by, bw, bh);
    Theme::drawButton(t, bx, by, bw, bh, "CANCEL", false);
    delBtnBox(w, h, 2, bx, by, bw, bh);
    snprintf(lab, sizeof lab, "DELETE RUN %u", (unsigned)(s_delRun + 1));
    Theme::drawButton(t, bx, by, bw, bh, lab, false);
}

// Forget a run the pair was pointing at. A pair that did not include it is left
// exactly as the owner set it; a pair that did loses that side, and the next
// redraw finds a half-chosen pair and calls cmpDefault(), which re-picks BOTH
// from the newest two finished runs. That is deliberate rather than clever:
// keeping the surviving side and hunting for a new partner would silently
// compare the owner's antenna against a run they did not choose.
void delForgetPair(int8_t run) {
    if (s_cmpA == run) s_cmpA = -1;
    if (s_cmpB == run) s_cmpB = -1;
    s_scroll[(int)LoraView::SURVEYCMP] = 0;
}

// ---- PICK: the grid that reaches everything --------------------------------
//
// Nine boxes of 125 x 56 logical pixels -- 47 x 21 mm each -- so no target on
// this screen is ever the problem again, and every view is on the glass at once
// with a live figure under its name. That is the answer to "how are ten views
// reached without burying any of them": not a ring of buttons that names its
// neighbours, which for ten views is five taps wide and tells you nothing about
// the other eight, but one tap to see them all and one more to arrive.

struct PickCell {
    const char* name;
    LoraView    view;
    bool        adverts;    // LIST with the MeshCore-advert filter on
    bool        msgAll;     // CHANMSG across every channel at once
};
const PickCell PICKS[9] = {
    { "FRAMES",   LoraView::LIST,      false, false },
    { "ADVERTS",  LoraView::LIST,      true,  false },
    { "TRAFFIC",  LoraView::TRAFFIC,   false, false },
    { "NODES",    LoraView::NODES,     false, false },
    { "CHANNELS", LoraView::CHANS,     false, false },
    { "MESSAGES", LoraView::CHANMSG,   false, true  },
    { "SURVEY",   LoraView::SURVEY,    false, false },
    { "COMPARE",  LoraView::SURVEYCMP, false, false },
    { "STATS",    LoraView::STATS,     false, false },
};

void pickBox(int w, int top, int bottom, uint8_t i, int& bx, int& by, int& bw, int& bh) {
    const int mx = 6, my = 2, gx = 6, gy = 4;
    bw = (w - 2 * mx - 2 * gx) / 3;
    bh = (bottom - top - 2 * my - 2 * gy) / 3;
    bx = mx + (int)(i % 3) * (bw + gx);
    by = top + my + (int)(i / 3) * (bh + gy);
}

// The figure under each name, which is the whole reason the picker beats a ring
// of buttons: it says what is behind a door before the door is opened.
void pickSub(uint8_t i, char* out, size_t cap) {
    switch (i) {
        case 0: snprintf(out, cap, "%u in the ring", (unsigned)Lora::packetCount()); break;
        case 1: snprintf(out, cap, "MeshCore only"); break;
        case 2: snprintf(out, cap, "rate and air"); break;
        case 3: snprintf(out, cap, "%u heard", (unsigned)Lora::nodeCount()); break;
        case 4: snprintf(out, cap, "%u keys", (unsigned)Lora::channelRowCount()); break;
        case 5: snprintf(out, cap, "%u held", (unsigned)Lora::msgCount()); break;
        case 6: {
            const int8_t rec = Lora::surveyRecording();
            if (rec >= 0) snprintf(out, cap, "RECORDING");
            else          snprintf(out, cap, "%u direct", (unsigned)s_liveN);
            break;
        }
        case 7: {
            const uint8_t n = Lora::surveyRunCount();
            if (n < 2) snprintf(out, cap, "needs 2 runs");
            else       snprintf(out, cap, "%u runs", (unsigned)n);
            break;
        }
        default: snprintf(out, cap, "noise %d dBm", (int)Lora::stats().noiseDbm); break;
    }
}

void drawPick(TFT_eSPI& t, int w, int top, int bottom) {
    for (uint8_t i = 0; i < 9; i++) {
        int bx, by, bw, bh;
        pickBox(w, top, bottom, i, bx, by, bw, bh);
        const PickCell& c = PICKS[i];
        const bool here = c.view == s_under && c.adverts == s_adverts &&
                          (c.view != LoraView::CHANMSG || c.msgAll == s_msgAll);
        // The survey is the one cell that changes colour on its own: a run
        // recording is a state the owner must be able to see from the picker,
        // because forgetting to STOP is how two runs become one.
        const bool hot = i == 6 && Lora::surveyRecording() >= 0;
        const uint16_t edge = hot ? Theme::RED : here ? Theme::CYAN : Theme::PURPLE;
        t.fillRect(bx, by, bw, bh, here ? Theme::PURPLE : Theme::BG);
        t.drawRect(bx, by, bw, bh, edge);
        if (hot) t.drawRect(bx + 1, by + 1, bw - 2, bh - 2, edge);
        const uint16_t fg = here ? Theme::labelOn(Theme::PURPLE) : hot ? Theme::RED : Theme::CYAN;
        t.setTextSize(2);
        t.setTextColor(fg, here ? Theme::PURPLE : Theme::BG);
        t.setCursor(bx + (bw - t.textWidth(c.name)) / 2, by + bh / 2 - 14);
        t.print(c.name);
        char sub[24];
        pickSub(i, sub, sizeof sub);
        t.setTextSize(1);
        t.setTextColor(here ? Theme::labelOn(Theme::PURPLE) : Theme::WHITE, here ? Theme::PURPLE : Theme::BG);
        t.setCursor(bx + (bw - t.textWidth(sub)) / 2, by + bh / 2 + 8);
        t.print(sub);
    }
}

// ---- STATS -----------------------------------------------------------------

// The room a stat line's text has after its label, in glyphs of the built-in
// font at size 1: the label starts at x 6, the text 6 past its end, and the
// text runs to 6 short of the right edge. "HEARD:" is six glyphs, so on the
// 400 px canvas that is (400 - 6 - 36 - 6 - 6) / 6 = 57 glyphs.
int statRoom(TFT_eSPI& t, int w, const char* label) {
    return UiFit::chars(w - 6 - t.textWidth(label) - 6 - 6, 1);
}

int statLine(TFT_eSPI& t, int y, uint16_t col, const char* label, const char* text) {
    t.setTextColor(col, Theme::BG);
    t.setCursor(6, y);
    t.print(label);
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(6 + t.textWidth(label) + 6, y);
    // Wrap is off on this screen, so a line that does not fit would print
    // through the right edge; one that does not is fitted with the mark.
    char fit[96];
    UiFit::fitHead(fit, sizeof fit, text, statRoom(t, t.width(), label));
    t.print(fit);
    return y + t.fontHeight() + 2;
}

// The spectrum from a SWEEP, when there has been one: the hold in the dim
// colour, the live reading bright, 863 to 870 MHz across the width.
int drawSpectrum(TFT_eSPI& t, int w, int y, int bottom) {
    uint8_t live[Lora::SPECTRUM_BINS], hold[Lora::SPECTRUM_BINS];
    const uint8_t n = Lora::spectrum(live, hold, Lora::SPECTRUM_BINS);
    if (!n || !Lora::spectrumSweeps()) return y;
    const int gh = bottom - y - 12;
    if (gh < 30) return y;
    const int x0 = 24, gw = w - x0 - 6;
    // The floor of the scale is -140 dBm (value 10), the top -60 (value 90).
    auto bar = [&](uint8_t v) { int h = ((int)v - 10) * gh / 80; return h < 0 ? 0 : h > gh ? gh : h; };
    t.drawRect(x0 - 1, y, gw + 2, gh + 2, Theme::PURPLE);
    for (uint8_t i = 0; i < n; i++) {
        const int x = x0 + (int)((long)i * gw / n);
        const int bw = (int)((long)(i + 1) * gw / n) - (int)((long)i * gw / n);
        const int hh = bar(hold[i]), lh = bar(live[i]);
        if (hh) t.fillRect(x, y + 1 + gh - hh, bw > 1 ? bw - 1 : 1, hh, Theme::VAPOR_PURPLE);
        if (lh) t.fillRect(x, y + 1 + gh - lh, bw > 1 ? bw - 1 : 1, lh, Theme::CYAN);
    }
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(0, y); t.print("-60");
    t.setCursor(0, y + gh - t.fontHeight()); t.print("-140");
    const int ly = y + gh + 3;
    for (int mhz = 863; mhz <= 870; mhz++) {
        const int x = x0 + (mhz - 863) * gw / 7;
        t.drawFastVLine(x, y + gh - 3, 3, Theme::WHITE);
        if (mhz % 2 == 1 || mhz == 870) {
            char l[8]; snprintf(l, sizeof l, "%d", mhz);
            t.setCursor(x - (mhz == 870 ? t.textWidth(l) : t.textWidth(l) / 2), ly); t.print(l);
        }
    }
    return bottom;
}

void drawStats(TFT_eSPI& t, uint32_t now, int w, int top, int bottom) {
    (void)now;
    t.setTextSize(1);
    const Lora::Stats& s = Lora::stats();
    char b[96];
    int y = top + 2;
    if (!Lora::present()) {
        char st[96]; Lora::statusLine(st, sizeof st);
        y = statLine(t, y, Theme::AMBER, "SLOT:", st);
        return;
    }
    Lora::statusLine(b, sizeof b);
    y = statLine(t, y, Theme::CYAN, "RADIO:", b);
    // The long words while they fit, the short ones when they do not: the long
    // form is 45 glyphs of words plus the digits, so four four-digit counters
    // already make 61 against the 57 the 400 px canvas leaves after "HEARD:"
    // (statRoom) -- the short form is what a receiver that has run a day
    // shows -- and it is the words that give, never the numbers. The short form
    // is 51 with six digits each and 63 with nine, at which point statLine's
    // mark takes over.
    snprintf(b, sizeof b, "%lu frames, %lu crc err, %lu hdr err, %lu stray preambles",
             (unsigned long)s.packets, (unsigned long)s.crcErrors, (unsigned long)s.headerErrors, (unsigned long)s.preambles);
    if ((int)strlen(b) > statRoom(t, w, "HEARD:"))
        snprintf(b, sizeof b, "%lu frames, %lu crc, %lu hdr, %lu stray",
                 (unsigned long)s.packets, (unsigned long)s.crcErrors, (unsigned long)s.headerErrors, (unsigned long)s.preambles);
    y = statLine(t, y, Theme::CYAN, "HEARD:", b);
    snprintf(b, sizeof b, "%lu rounds, %lu hits", (unsigned long)s.cadRounds, (unsigned long)s.cadHits);
    y = statLine(t, y, Theme::CYAN, "CAD:", b);
    const uint32_t up = millis() / 1000;
    snprintf(b, sizeof b, "%lu.%lu s of time on air heard; noise %d dBm",
             (unsigned long)(s.airtimeMs / 1000), (unsigned long)((s.airtimeMs / 100) % 10), (int)s.noiseDbm);
    y = statLine(t, y, Theme::CYAN, "AIR:", b);
    // Airtime over the time the receiver was actually on a profile -- which is
    // the occupancy of that air while we were listening to it. Dividing by
    // uptime instead, as this line used to, divides by the wall clock the
    // survey spent on the other thirty-odd profiles and answers nothing.
    if (s.listenMs) {
        const uint32_t pct100 = (uint32_t)(((uint64_t)s.airtimeMs * 10000ull) / s.listenMs);
        snprintf(b, sizeof b, "%lu s receiving, %lu%% of uptime; %lu.%02lu%% of it was frames",
                 (unsigned long)(s.listenMs / 1000), (unsigned long)(up ? s.listenMs / 10 / up : 0),
                 (unsigned long)(pct100 / 100), (unsigned long)(pct100 % 100));
    } else {
        snprintf(b, sizeof b, "the receiver has not been on yet");
    }
    y = statLine(t, y, Theme::CYAN, "LISTEN:", b);
    size_t o = 0;
    for (int p = 1; p < (int)Lora::Proto::COUNT && o + 12 < sizeof b; p++)
        if (s.byProto[p]) o += (size_t)snprintf(b + o, sizeof b - o, "%s%s %u", o ? "  " : "", Lora::protoShort((Lora::Proto)p), (unsigned)s.byProto[p]);
    if (s.byProto[0]) snprintf(b + o, sizeof b - o, "%s?? %u", o ? "  " : "", (unsigned)s.byProto[0]);
    y = statLine(t, y, Theme::CYAN, "BY NET:", o || s.byProto[0] ? b : "-");
    // What has left this board, and what may. Amber when anything may leave,
    // cyan when nothing can: the unusual state is the one that gets the colour,
    // and here the unusual state is being allowed to talk about other people.
    // The full log -- every identifier, every host, every status -- is LORA
    // LOOKUPS on the console; there is no room for sixteen rows here and no
    // screen to put them on yet.
    {
        Lora::Enrich::Progress pr;
        Lora::Enrich::progress(pr, now);
        if (!Settings::loraLookups()) {
            snprintf(b, sizeof b, "off: nothing about a node leaves this board");
        } else {
            // The feed is counted apart from the queue's three answers, because
            // its answers are not the queue's: it fetched rows and the matching
            // happened here, so "12 of 96 rows" is the honest figure and a hit
            // count would not be.
            char feed[40] = "";
            if (Settings::loraLookupFeed())
                snprintf(feed, sizeof feed, "; adverts %u rows", (unsigned)Lora::Feed::rowCount());
            snprintf(b, sizeof b, "%s%s%s: %u sent, %u known, %u not listed, %u no answer%s%s",
                     Settings::loraLookupCall() ? "hamrig.com" : "",
                     (Settings::loraLookupCall() && Settings::loraLookupOgn()) ? " + " : "",
                     Settings::loraLookupOgn() ? "glidernet.org" : "",
                     (unsigned)pr.sent, (unsigned)pr.hit, (unsigned)pr.miss, (unsigned)pr.noAnswer,
                     pr.queued ? ", more waiting for WiFi" : "", feed);
            if (!Settings::loraLookupCall() && !Settings::loraLookupOgn() && !Settings::loraLookupFeed())
                snprintf(b, sizeof b, "on, but every source is off: nothing leaves");
            else if (!Settings::loraLookupCall() && !Settings::loraLookupOgn())
                snprintf(b, sizeof b, "meshcore.df0x.de: %lu polls, %u of %u rows held",
                         (unsigned long)Lora::Feed::polls(), (unsigned)Lora::Feed::rowCount(),
                         (unsigned)Lora::Feed::ROW_MAX);
        }
        y = statLine(t, y, Settings::loraLookups() ? Theme::AMBER : Theme::CYAN, "LOOKUP:", b);
    }
    y += 4;
    if (Lora::mode() == Lora::Mode::SWEEP || Lora::spectrumSweeps()) {
        snprintf(b, sizeof b, "%lu passes over 863-870 MHz%s", (unsigned long)Lora::spectrumSweeps(),
                 Lora::mode() == Lora::Mode::SWEEP ? "; tap the status line to stop" : "");
        y = statLine(t, y, Theme::CYAN, "SWEEP:", b);
        drawSpectrum(t, w, y + 2, bottom);
        return;
    }
    // The busiest profiles as bars.
    uint8_t top8[8]; uint8_t n8 = 0;
    for (uint8_t i = 0; i < Lora::profileCount() && i < 64; i++) {
        if (!s.byProfile[i]) continue;
        uint8_t k = n8 < 8 ? n8++ : 7;
        while (k > 0 && s.byProfile[top8[k - 1]] < s.byProfile[i]) { top8[k] = top8[k - 1]; k--; }
        if (k < 8) top8[k] = i;
    }
    uint16_t most = n8 ? s.byProfile[top8[0]] : 1;
    const int lh = t.fontHeight() + 2;
    for (uint8_t i = 0; i < n8 && y + lh <= bottom; i++) {
        const Lora::Profile& p = Lora::profile(top8[i]);
        snprintf(b, sizeof b, "%-14s %4u", p.name, (unsigned)s.byProfile[top8[i]]);
        t.setTextColor(Theme::WHITE, Theme::BG); t.setCursor(6, y); t.print(b);
        const int bx = 6 + 20 * 6, bw = w - bx - 12;
        t.fillRect(bx, y + 1, (int)((long)bw * s.byProfile[top8[i]] / most), t.fontHeight() - 2, Theme::CYAN);
        y += lh;
    }
}

void openView(LoraView v, bool adverts, bool msgAll) {
    s_adverts = adverts;
    if (v == LoraView::CHANMSG) { s_msgAll = msgAll; s_scroll[(int)LoraView::CHANMSG] = 0; }
    if (v == LoraView::LIST) { advForget(); s_scroll[(int)LoraView::LIST] = 0; }
    s_view = v;
}

// ---- the redraw gate ---------------------------------------------------------
//
// Measured before this existed, in squachsim-live on the frame list with the
// fake radio quiet: 30 pushes a second, one per loop(), of a picture that had
// not changed -- and on the CrowPanel each of those is a 96 KB hash of the
// sprite and, every 64th, 768,000 bytes into the framebuffer. The frame is
// kept between loops on every board that draws through a sprite, so a tick
// that has nothing new to say can leave it alone.
//
// sceneSig is a number that changes whenever the picture would: every piece
// of this file's own state a tap can move, every counter a view prints, the
// toast (up, and gone again), and the clock's second -- which is what redraws
// the ages ("12 s ago"), the survey's running span, the TRAFFIC graph and the
// sweep at 1 Hz, and is the safety net for anything this list has missed:
// a stale input costs one second, never a stuck screen. What it does NOT
// include is a finger: main.cpp calls uiLoraDirty() on any touch, so a tap is
// reflected in the frame after it exactly as it was.
uint32_t s_drawnSig  = 0;
bool     s_forceDraw = true;    // the frame holds another screen's picture until the first draw
bool     s_lastDrew  = false;   // for the second band of a two-band draw (drawTwoBand)

uint32_t sceneSig(uint32_t now) {
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { h ^= v; h *= 16777619u; };
    mix((uint32_t)s_view); mix((uint32_t)s_under); mix(s_adverts); mix(s_msgAll);
    mix((uint32_t)s_msgProto); mix(s_msgChan);
    for (int i = 0; i < (int)LoraView::COUNT; i++) mix((uint32_t)s_scroll[i]);
    mix(s_open); mix(s_openTotal);
    mix((uint32_t)(int32_t)s_cmpA); mix((uint32_t)(int32_t)s_cmpB); mix((uint32_t)(int32_t)s_delRun);
    mix(s_liveN);
    mix(Lora::present()); mix((uint32_t)Lora::mode()); mix(Lora::currentProfile());
    mix(Lora::packetTotal()); mix(Lora::nodeCount()); mix(Lora::channelRowCount());
    mix(Lora::channelsQuietBuiltIn()); mix(Lora::msgCount()); mix(Lora::msgDropped());
    mix(Lora::surveyRunCount()); mix((uint32_t)(int32_t)Lora::surveyRecording()); mix(Lora::spectrumSweeps());
    const Lora::Stats& st = Lora::stats();
    mix(st.crcErrors); mix(st.headerErrors); mix(st.preambles); mix(st.cadRounds); mix(st.cadHits);
    mix((uint32_t)(int32_t)st.noiseDbm);
    {
        Lora::Enrich::Progress pr;
        Lora::Enrich::progress(pr, now);
        mix(pr.queued); mix(pr.sent); mix(pr.hit); mix(pr.miss); mix(pr.noAnswer);
    }
    mix(Settings::loraLookups()); mix(Settings::loraLookupCall()); mix(Settings::loraLookupOgn());
    mix(Settings::loraLookupFeed()); mix(Lora::Feed::rowCount()); mix(Lora::Feed::polls());
    mix(Theme::toastUp(now));
    mix(now / 1000);
    return h;
}

}  // namespace

void uiLoraDirty() { s_forceDraw = true; }

void uiLoraInit(TFT_eSPI& t, LoraView v) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_forceDraw = true;
    // PACKET needs a frame chosen first, CHANMSG a channel, and the picker
    // something to cover.
    s_view = (v == LoraView::PACKET || v == LoraView::CHANMSG || v == LoraView::PICK || v >= LoraView::COUNT)
             ? LoraView::LIST : v;
    s_under = s_view;
    s_adverts = false;
    s_msgAll  = false;
    // A panel is a question about right now. Leaving the screen with one up and
    // coming back to it later would be answering a question nobody remembers.
    s_delRun  = -1;
    advForget();
    liveForget();
    // The traffic history starts over with the screen, because a monitor that
    // draws the minutes it was not watching as quiet air is lying.
    trafficReset();
}

LoraView uiLoraView() { return s_view; }

void uiLoraScroll(int delta) {
    // Nothing underneath a modal panel moves, scrolling included: the list the
    // panel is asking about must still be the list it was asking about when the
    // answer comes.
    if (s_delRun >= 0) return;
    int& s = s_scroll[(int)s_view];
    s += delta;
    if (s < 0) s = 0;
}

int uiLoraDragStep(TFT_eSPI& t) {
    return s_view == LoraView::SURVEY ? SURVEY_ROW : rowH(t);
}

bool uiLoraTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    (void)eng;
    const int w = t.width(), h = t.height();
    const int bottom = bodyBottom(w, h);
    // The bookkeeping runs every tick, drawn or not. Every view, not just
    // TRAFFIC: a monitor that only counts while it is the one showing opens
    // with an empty graph every time.
    trafficTick(now);
    // Likewise the survey's live rows. It costs a walk of the node table per
    // frame RECEIVED (liveSync gates on packetTotal, not on the redraw), and it
    // buys two things: the picker's SURVEY cell can say how many stations are
    // heard first-hand before the view is opened, and the view opens with them
    // already on it rather than filling in on the next frame off the air --
    // which on a quiet band is a minute of blank screen.
    if (Lora::present()) liveSync();
    // The gate (see sceneSig). Only the first band of a two-band draw asks it;
    // the second band draws whatever the first did, or it would half-skip.
    if (advance) {
        const uint32_t sig = sceneSig(now);
        s_lastDrew = s_forceDraw || sig != s_drawnSig;
        if (!s_lastDrew) return false;
        s_forceDraw = false;
        s_drawnSig  = sig;
    } else if (!s_lastDrew) {
        return false;
    }
    t.fillRect(0, BODY_TOP, w, bottom - BODY_TOP, Theme::BG);
    char title[48];
    switch (s_view) {
        case LoraView::LIST:
            if (s_adverts) snprintf(title, sizeof title, ">> LORA ADVERTS <<");
            else           snprintf(title, sizeof title, ">> LORA  (%u) <<", (unsigned)Lora::packetCount());
            break;
        case LoraView::NODES:
            snprintf(title, sizeof title, ">> LORA NODES  (%u) <<", (unsigned)Lora::nodeCount()); break;
        case LoraView::CHANS:
            snprintf(title, sizeof title, ">> LORA CHANNELS  (%u) <<", (unsigned)Lora::channelRowCount()); break;
        case LoraView::CHANMSG:
            snprintf(title, sizeof title, ">> %s  (%u) <<", s_msgAll ? "ALL MESSAGES" : "CHANNEL",
                     (unsigned)(s_msgAll ? Lora::msgCount() : Lora::msgCountFor(s_msgProto, s_msgChan))); break;
        case LoraView::SURVEY:
            snprintf(title, sizeof title, ">> ANTENNA SURVEY  (%u) <<", (unsigned)s_liveN); break;
        default:
            snprintf(title, sizeof title, ">> %s <<", viewTitle(s_view)); break;
    }
    Theme::drawTitleBar(t, title);
    drawTitle(t, w, title);
    int top = drawStatus(t, w, BODY_TOP + 1);
    switch (s_view) {
        case LoraView::LIST:      drawList(t, now, w, top, bottom); break;
        case LoraView::PACKET:    drawPacket(t, now, w, top, bottom); break;
        case LoraView::NODES:     drawNodes(t, now, w, top, bottom); break;
        case LoraView::CHANS:     drawChans(t, now, w, top, bottom); break;
        case LoraView::CHANMSG:   drawChanMsg(t, now, w, top, bottom); break;
        case LoraView::TRAFFIC:   drawTraffic(t, now, w, top, bottom); break;
        case LoraView::SURVEY:    drawSurvey(t, now, w, top, bottom); break;
        case LoraView::SURVEYCMP: drawSurveyCmp(t, now, w, top, bottom); break;
        case LoraView::PICK:      drawPick(t, w, top, bottom); break;
        default:                  drawStats(t, now, w, top, bottom); break;
    }
    drawBar(t, w, h);
    // Over everything this frame drew, the bar included: it is modal, and a
    // panel with live buttons showing underneath it invites a tap on one of
    // them. ui_rawscan.cpp draws its confirm panel last for the same reason.
    drawDelPanel(t, w, h, now);
    Theme::drawToast(t, now);
    return true;
}

bool uiLoraHold(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    if (s_delRun >= 0) return false;              // already up: nothing to raise
    if (s_view != LoraView::SURVEYCMP) return false;
    const int top = statusBottom(t);
    for (uint8_t i = 0; i < Lora::Survey::RUNS; i++) {
        int bx, by, bw, bh;
        cmpStripBox(screenW, top, i, bx, by, bw, bh);
        if (x < bx || x > bx + bw || y < by || y > by + bh) continue;
        Lora::Survey::Run r;
        // An empty slot has nothing to drop, and the panel must not offer RESET
        // from a box that means nothing: the hold is declined and the release
        // becomes the ordinary tap, which says EMPTY SLOT.
        if (!Lora::surveyRun(i, r)) return false;
        s_delRun = (int8_t)i;
        return true;
    }
    return false;
}

LoraTap uiLoraTap(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    const Bar b = bar(screenW, screenH);
    const int bottom = bodyBottom(screenW, screenH);
    t.setTextSize(1);

    // ---- the drop/reset panel, which owns every tap while it is up
    //
    // Modal in the strong sense: a tap that misses all three buttons is
    // swallowed rather than falling through to the strip or the bar underneath.
    // The alternative -- dismiss on a tap outside -- puts "throw the panel away"
    // on the biggest target on the screen, next to a button that deletes a run.
    if (s_delRun >= 0) {
        const int8_t run = s_delRun;
        for (uint8_t i = 0; i < 3; i++) {
            int bx, by, bw, bh;
            delBtnBox(screenW, screenH, i, bx, by, bw, bh);
            if (x < bx || x > bx + bw || y < by || y > by + bh) continue;
            if (i == 1) { s_delRun = -1; return LoraTap::HANDLED; }   // CANCEL
            s_delRun = -1;
            if (i == 2) {
                const bool rec = Lora::surveyRecording() == run;
                if (Lora::surveyDropRun((uint8_t)run)) {
                    delForgetPair(run);
                    // The subs on this screen run to 43 characters ("walk,
                    // then STOP. LORA SURVEY LABEL names it"); Theme's toast
                    // holds 47 and word-wraps what a narrow panel cannot fit
                    // on one line (src/theme.cpp's drawToast).
                    Theme::showToast(rec ? "RUN ABORTED" : "RUN DELETED",
                                     "run numbers unchanged", Theme::CYAN);
                } else {
                    Theme::showToast("STILL THERE", "the store was busy", Theme::AMBER);
                }
            } else {
                Lora::surveyClear();
                s_cmpA = s_cmpB = -1;
                s_scroll[(int)LoraView::SURVEYCMP] = 0;
                Theme::showToast("SURVEY RESET", "runs and trend gone", Theme::CYAN);
            }
            return LoraTap::HANDLED;
        }
        return LoraTap::HANDLED;
    }

    // ---- the bar, which owns EVERY pixel under the body
    //
    // Not just the twenty the buttons are drawn in, and this is the bug the
    // owner reported from the bench: press [ VIEWS ] on the frame list and get
    // a single FRAME; press it again and get the picker.
    //
    // Theme::computeButtonBar puts the bar at screenH - h - 6 and bodyBottom
    // stops four pixels above it, so on the CrowPanel's 400x240 canvas the
    // buttons are y 214..233 with 211..213 of bare screen above them and
    // 235..239 below: 1.1 mm and 1.9 mm of nothing flanking a 7.6 mm target
    // (0.381 mm to the logical pixel, see rowH). Those eight rows used to fall
    // straight through to the body's hit tests, and the LIST view's row
    // arithmetic -- (y - top) / rowH, bounded by the frame count and by nothing
    // else -- read them as row 9 and row 10 and opened a frame. A finger aimed
    // at a button and landing two millimetres low must not open another screen,
    // and there is nothing under the body to hit but this bar, so it takes the
    // lot. `y > bottom`, not `>=`: the SURVEY view's START button ends exactly
    // ON bottom (surveyButtonBox) and that pixel row is still the button's.
    // And `y < screenH`, so that this bar and Theme::hitTestButtonBar agree
    // about the pixels past the last one as well as about the last one.
    if (y > bottom && y < screenH) {
        int which = -1;
        for (int i = 0; i < 3; i++) if (x >= b.x[i] && x <= b.x[i] + b.w) which = i;
        if (which < 0) return LoraTap::NONE;
        const char* l[3]; barLabels(l);
        if (!l[which]) return LoraTap::NONE;
        if (which == 0) {
            // BACK goes up one level where there is one, and off the screen
            // where there is not.
            switch (s_view) {
                case LoraView::PACKET:    s_view = LoraView::LIST;   return LoraTap::HANDLED;
                case LoraView::CHANMSG:   s_view = LoraView::CHANS;  return LoraTap::HANDLED;
                case LoraView::SURVEYCMP: s_view = LoraView::SURVEY; return LoraTap::HANDLED;
                case LoraView::PICK:      s_view = s_under;          return LoraTap::HANDLED;
                default: break;
            }
            return LoraTap::BACK;
        }
        // SLOT 1 IS THE PICKER, FIRST AND WITHOUT EXCEPTION. This test used to
        // sit BELOW the PACKET branch, which is what made the views button mean
        // "newer frame" in one view out of ten. Nothing view-specific may be
        // tested above this line -- see barLabels() for the rule and for what
        // PACKET gave up to keep it.
        if (which == 1) { s_under = s_view; s_view = LoraView::PICK; return LoraTap::HANDLED; }
        // ---- slot 2: the one thing THIS view is for
        if (s_view == LoraView::PACKET) {
            // Towards the older frame, in the list's order, and only that way.
            const uint32_t total = Lora::packetTotal();
            uint32_t idx = s_open + (total - s_openTotal);
            if (idx + 1 >= Lora::packetCount()) {
                // Nothing older is held. Said out loud, because a button that
                // does nothing when pressed is the same complaint in a smaller
                // size. Both strings are short of Theme's toast buffers (24
                // and 48, src/theme.cpp), and anything longer is marked '>'
                // or wrapped there, never cut in silence.
                Theme::showToast("OLDEST FRAME", "BACK for the list", Theme::AMBER);
                return LoraTap::HANDLED;
            }
            idx++;
            s_open = (uint16_t)idx; s_openTotal = total;
            return LoraTap::HANDLED;
        }
        if (s_view == LoraView::SURVEY) { s_view = LoraView::SURVEYCMP; return LoraTap::HANDLED; }
        openView(LoraView::SURVEY, false, false);
        return LoraTap::HANDLED;
    }

    if (y < BODY_TOP) return LoraTap::NONE;

    // ---- the picker, which owns its whole body
    if (s_view == LoraView::PICK) {
        const int top = statusBottom(t);
        for (uint8_t i = 0; i < 9; i++) {
            int bx, by, bw, bh;
            pickBox(screenW, top, bottom, i, bx, by, bw, bh);
            if (x < bx || x > bx + bw || y < by || y > by + bh) continue;
            openView(PICKS[i].view, PICKS[i].adverts, PICKS[i].msgAll);
            return LoraTap::HANDLED;
        }
        return LoraTap::NONE;
    }

    // ---- the status line cycles the radio's mode: OFF, FOCUS, SURVEY, SWEEP.
    // The first three are the setting; SWEEP is for now and is not kept.
    if (y < statusBottom(t)) {
        if (!Lora::present()) return LoraTap::NONE;
        // Not while an antenna run is recording. Walking the profiles or
        // sweeping the band mid-run would make the run's frame counts mean
        // something different from the run it is going to be compared with --
        // and the mode is one tap away from every view, including the survey's.
        if (Lora::surveyRecording() >= 0) {
            Theme::showToast("RUN RECORDING", "the radio stays put -- STOP the run first", Theme::AMBER);
            return LoraTap::HANDLED;
        }
        const uint8_t next = (uint8_t)(((uint8_t)Lora::mode() + 1) % (uint8_t)Lora::Mode::COUNT);
        Lora::setMode((Lora::Mode)next);
        if (next < 3) { while (Settings::loraMode() != next) Settings::cycleLoraMode(); }
        return LoraTap::HANDLED;
    }

    const int top = statusBottom(t);
    const int rh  = rowH(t);

    // ---- the survey: one button, and the rows are read rather than tapped
    if (s_view == LoraView::SURVEY) {
        int bx, by, bw, bh;
        surveyButtonBox(screenW, bottom, bx, by, bw, bh);
        if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) {
            if (Lora::surveyRecording() >= 0) {
                if (Lora::surveyStop()) Theme::showToast("RUN STOPPED", "swap the antenna and START again", Theme::CYAN);
            } else if (Lora::surveyStart("") >= 0) {
                // The label is left to start() to fill in ("run 3"): a panel
                // with no keyboard cannot type "dipole at the balcony rail",
                // and LORA SURVEY LABEL <text> on the console renames the run
                // that is recording. Saying so beats an on-screen keyboard
                // nobody can hit.
                Theme::showToast("RECORDING", "walk, then STOP. LORA SURVEY LABEL names it", Theme::RED);
            } else {
                Theme::showToast("NO STORE", "there was no PSRAM for the survey at boot", Theme::AMBER);
            }
            return LoraTap::HANDLED;
        }
        return LoraTap::NONE;
    }

    // ---- the comparison: the strip chooses which two runs
    if (s_view == LoraView::SURVEYCMP) {
        for (uint8_t i = 0; i < Lora::Survey::RUNS; i++) {
            int bx, by, bw, bh;
            cmpStripBox(screenW, top, i, bx, by, bw, bh);
            if (x < bx || x > bx + bw || y < by || y > by + bh) continue;
            Lora::Survey::Run r;
            if (!Lora::surveyRun(i, r)) {
                Theme::showToast("EMPTY SLOT", "SURVEY: START begins a run here", Theme::AMBER);
                return LoraTap::HANDLED;
            }
            cmpPick(i);
            s_scroll[(int)LoraView::SURVEYCMP] = 0;
            return LoraTap::HANDLED;
        }
        return LoraTap::NONE;
    }

    // ---- a channel's messages: the mute toggle
    if (s_view == LoraView::CHANMSG) {
        if (!s_msgAll) {
            int bx, by, bw, bh;
            msgMuteBox(screenW, top, bx, by, bw, bh);
            if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) {
                Lora::ChannelRow r;
                uint8_t row = 0;
                if (!findChanRow(s_msgProto, s_msgChan, row, r)) return LoraTap::NONE;
                if (Lora::toggleChannelRow(row))
                    Theme::showToast(r.enabled ? "MUTED" : "LISTENING", r.name,
                                     r.enabled ? Theme::VAPOR_PURPLE : Theme::CYAN);
                else
                    Theme::showToast("BUILT IN", "stays on -- yours go in over LORA CHAN", Theme::AMBER);
                return LoraTap::HANDLED;
            }
        }
        return LoraTap::NONE;
    }

    // ---- the channel list: a tap reads the channel
    if (s_view == LoraView::CHANS) {
        const int listTop = chansListTop(t), listBottom = chansListBottom(t, bottom);
        if (y < listTop) return LoraTap::NONE;
        const int vrow = (y - listTop) / rh;
        if (vrow >= (listBottom - listTop) / rh) return LoraTap::NONE;   // past the last drawn row
        const int row = s_scroll[(int)LoraView::CHANS] + vrow;
        Lora::ChannelRow r;
        if (row < 0 || !Lora::channelRow((uint8_t)row, r)) return LoraTap::NONE;
        Lora::Proto p; uint8_t idx;
        if (!Lora::channelRowKey((uint8_t)row, p, idx)) return LoraTap::NONE;
        s_msgProto = p; s_msgChan = idx;
        openView(LoraView::CHANMSG, false, false);
        return LoraTap::HANDLED;
    }

    // ---- the frame list: a tap opens the frame
    if (s_view == LoraView::LIST) {
        const int vrow = (y - top) / rh;
        // Only the rows that are on the glass. drawList stops as soon as a
        // whole row no longer fits (y + rh <= bottom), so the last few pixels
        // of the body are under the END of the list, not under a row -- and
        // without this they opened the frame that did not fit, which is a
        // frame the list never showed.
        if (vrow < 0 || vrow >= (bottom - top) / rh) return LoraTap::NONE;
        if (s_adverts) {
            // The filter's rows are ring positions found at s_advAt frames, and
            // PACKET tracks its frame by exactly that pair.
            if (vrow >= (int)s_advN) return LoraTap::NONE;
            s_open = s_advRow[vrow];
            s_openTotal = s_advAt;
        } else {
            const int row = s_scroll[0] + vrow;
            if (row >= (int)Lora::packetCount()) return LoraTap::NONE;
            s_open = (uint16_t)row;
            s_openTotal = Lora::packetTotal();
        }
        s_view = LoraView::PACKET;
        return LoraTap::HANDLED;
    }
    return LoraTap::NONE;
}
