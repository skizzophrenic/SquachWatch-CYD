// SquachWatch-CYD — ALERT screen: the Trail card (see ui_alert.h).
//
// Laid out in a 320x240 design space and mapped onto the panel: a landscape
// panel 300 or wider scales it (the 2.8in exactly, the 3.5in by 1.5, the Tab5
// by 2 across and 1.5 down); a narrow panel (the watch's 240x240, the 2.8in
// turned portrait) keeps the trail at full width and stacks the header's
// chips under the tabs; the 240x135 boards get their own small cut.
#include "ui_alert.h"
#include "theme.h"
#include "privacy.h"
#include "signatures.h"
#include "detection.h"
#include "ignore_list.h"
#include "tag_ring.h"
#include "trail.h"
#include "places.h"
#include "regulars.h"
#include "clock.h"
#include "squachy.h"
#include "settings.h"
#include "alert_skin.h"
#include <Arduino.h>

static const char* targetLabel(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:   return "FLOCK CAM";
        case DetectionType::AXON:    return "AXON BODY";
        case DetectionType::META:    return "META GLASSES";
        case DetectionType::SKIMMER: return "CARD SKIMMER";
        case DetectionType::RAVEN:   return "RAVEN";
        case DetectionType::AIRTAG:  return "AIRTAG";
        case DetectionType::DRONE:   return "DRONE";
        case DetectionType::ALPR:    return "ALPR";
        case DetectionType::CAMERA:  return "CAMERA";
        case DetectionType::SAMSUNG_TAG: return "SAMSUNG TAG";
        case DetectionType::GOOGLE_TAG:  return "GOOGLE TAG";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING CAM";
        case DetectionType::DEAUTH:      return "DEAUTH FLOOD";
        case DetectionType::EVILTWIN:    return "EVIL TWIN AP";
        case DetectionType::IBEACON:     return "PROXIMITY BEACON";
        case DetectionType::HACKER:      return "HACKER HARDWARE";
        case DetectionType::LORAWAN:     return "LORAWAN NETWORK";
        case DetectionType::MINE:        return "MY ALERT";
        case DetectionType::LISTENER:    return "SMART SPEAKER";
        // A device opened from SCAN's list, which the scan does not classify.
        case DetectionType::UNKNOWN:     return "DEVICE";
        default:                     return "UNKNOWN";
    }
}
// A shorter name for the tab when the long one and the verdict will not both
// fit the row.
static const char* shortLabel(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:   return "FLOCK";
        case DetectionType::AXON:    return "AXON";
        case DetectionType::META:    return "GLASSES";
        case DetectionType::SKIMMER: return "SKIMMER";
        case DetectionType::SAMSUNG_TAG: return "SMARTTAG";
        case DetectionType::GOOGLE_TAG:  return "FIND HUB";
        case DetectionType::RING:        return "RING";
        case DetectionType::DEAUTH:      return "DEAUTH";
        case DetectionType::EVILTWIN:    return "EVIL TWIN";
        case DetectionType::IBEACON:     return "IBEACON";
        case DetectionType::HACKER:      return "HACKER";
        case DetectionType::LORAWAN:     return "LORAWAN";
        case DetectionType::LISTENER:    return "SPEAKER";
        default: return targetLabel(t);
    }
}

// ---- state ------------------------------------------------------------
static Detection s_last;
static bool s_touched = false;
static bool s_ringing = false;
static bool s_watching = false;
static bool s_redacted = false;
static bool s_first = false, s_night = false, s_lastFree = false;
static bool s_spam = false;
static uint16_t s_spamFakes = 0;
static uint8_t  s_queue = 0;
static bool     s_picking = false;
static uint32_t s_alertStart = 0;
static uint8_t  s_glitchStep = 0;
static uint32_t s_queueAt = 0;     // when the last catch joined the queue, for the skins' flourish
static bool     s_preview = false; // ALERT CARD's PREVIEW
static bool     s_skinned = false; // the last frame was drawn by a skin: taps go by its rects
static bool     s_byHand = false;  // opened from a LOG row: HUNT takes SNOOZE's slot on a tracker
static int      browseH(int h);    // the style browser's strip, further down
static void     drawBrowseStrip(TFT_eSPI& t, int w, int h);
static void     ownerRowText(char* out, size_t n);   // the OWNER fact row, further down

void uiAlertSetRinging(bool on)       { s_ringing = on; }
void uiAlertSetWatching(bool on)      { s_watching = on; }
void uiAlertSetRedacted(bool r)       { s_redacted = r; }
void uiAlertSetFirst(bool first)      { s_first = first; }
void uiAlertSetNight(bool night)      { s_night = night; }
void uiAlertSetLastFree(bool lf)      { s_lastFree = lf; }
void uiAlertSetSpam(bool spam, uint16_t fakes) { s_spam = spam; s_spamFakes = fakes; }
void uiAlertSetQueue(uint8_t n)       { if (n > s_queue) s_queueAt = millis(); s_queue = n; }
void uiAlertSetPreview(bool on)       { s_preview = on; }
void uiAlertSetByHand(bool on)        { s_byHand = on; }
bool uiAlertPreviewing()              { return s_preview; }
void uiAlertSetFrame(uint8_t* buf, int w, int h) { AlertSkin::setFrame(buf, w, h); }
void uiAlertSetSnoozePicking(bool on) { s_picking = on; }
bool uiAlertSnoozePicking()           { return s_picking; }
void uiAlertSetSnoozePick(const char*, uint16_t) {}
#if defined(CYD_GPS)
void uiAlertSetPlace(bool, int32_t, int32_t, bool) {}
#endif
bool uiAlertTouched()   { return s_touched; }
void uiAlertNoteTouch() { s_touched = true; }

// ALERT's glitch cadence: one burst right away, then louder at 2.5 / 4 / 5 s,
// then the loudest every 1.25 s for as long as it stays up.
static uint32_t glitchStepOffsetMs(uint8_t step) {
    switch (step) { case 0: return 0; case 1: return 2500; case 2: return 4000; case 3: return 5000;
                    default: return 5000 + (uint32_t)(step - 3) * 1250; }
}
static uint8_t glitchStepLevel(uint8_t step) { return (uint8_t)(step + 1 > 4 ? 4 : step + 1); }

void uiAlertInit(TFT_eSPI& t, const Detection& d) {
    s_last = d;
    s_first = s_night = s_lastFree = s_spam = false;
    s_queue = 0;
    s_picking = false;
    s_watching = false;
    s_touched = false;
    s_alertStart = millis();
    s_glitchStep = 0;
    s_queueAt = 0;
    s_preview = false;
    s_skinned = false;
    s_byHand = false;
    AlertSkin::forget();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

// ---- layout -------------------------------------------------------------
// Design-space numbers (320x240) and the map onto the panel.
struct Lay {
    int  w, h;
    bool compact;   // 240x135: its own cut
    bool narrow;    // w < 300: trail at full width, chips on their own row
    float kx, ky;   // design -> panel
    int  ts;        // the 5x7 text size for the small print: 1, or 2 on the large panels
    int  tabsH;     // the header's height (panel px)
    int  btnY, btnH, btnGap, btnW;   // the five buttons (panel px)
};
static Lay layoutFor(int w, int h) {
    Lay L;
    L.w = w; L.h = h;
    L.compact = h < 200;
    L.narrow  = !L.compact && w < 300;
    L.kx = L.compact ? 1.0f : (float)w / 320.0f;
    L.ky = L.compact ? 1.0f : (float)h / 240.0f;
    if (L.narrow) { L.ky = (float)h / 240.0f; L.kx = 1.0f; }
    const float k = L.kx < L.ky ? L.kx : L.ky;
    L.ts   = (!L.compact && (k >= 1.5f || L.w >= 480 || L.ky >= 1.8f)) ? 2 : 1;
    L.tabsH = L.compact ? 20 : (int)(30 * L.ky + 0.5f);
    if (L.tabsH < 30 && !L.compact) L.tabsH = 30;
    L.btnH  = L.compact ? 22 : (int)(36 * L.ky + 0.5f);
    L.btnGap = L.compact ? 2 : 3;
    L.btnY  = h - L.btnH - (L.compact ? 2 : 4);
    L.btnW  = (w - L.btnGap * 6) / 5;
    return L;
}
static inline int X(const Lay& L, int x) { return (int)(x * L.kx + 0.5f); }
static inline int Y(const Lay& L, int y) { return (int)(y * L.ky + 0.5f); }

static void btnRect(const Lay& L, int i, int& bx, int& by, int& bw, int& bh) {
    bx = L.btnGap + i * (L.btnW + L.btnGap);
    by = L.btnY; bw = L.btnW; bh = L.btnH;
}
static bool inRect(int x, int y, int bx, int by, int bw, int bh, int slop = 0) {
    return x >= bx - slop && x <= bx + bw + slop && y >= by - slop && y <= by + bh + slop;
}
static bool hitSlot(int x, int y, int w, int h, int i) {
    if (s_skinned) { int bx, by, bw, bh; if (AlertSkin::buttonRect((uint8_t)i, bx, by, bw, bh)) return inRect(x, y, bx, by, bw, bh, 2); }
    const Lay L = layoutFor(w, h);
    int bx, by, bw, bh; btnRect(L, i, bx, by, bw, bh);
    return inRect(x, y, bx, by, bw, bh, 2);
}

static bool ringShown() { return TagRing::canRing(s_last.type); }
bool uiAlertRingShown() { return ringShown(); }
bool uiAlertHitRing(int x, int y, int w, int h)     { return ringShown() && hitSlot(x, y, w, h, 0); }
// HUNT lives in slot 3 on a tracker's card opened by hand (slot 1 is RING).
static bool huntInSnoozeSlot() { return s_byHand && ringShown(); }
bool uiAlertHitHunt(int x, int y, int w, int h)     { return (!ringShown() && hitSlot(x, y, w, h, 0)) || (huntInSnoozeSlot() && hitSlot(x, y, w, h, 2)); }
bool uiAlertHitWatch(int x, int y, int w, int h)    { return hitSlot(x, y, w, h, 1); }
bool uiAlertHitSnooze(int x, int y, int w, int h)   { return !huntInSnoozeSlot() && hitSlot(x, y, w, h, 2); }
bool uiAlertHitIgnore(int x, int y, int w, int h)   { return hitSlot(x, y, w, h, 3); }
bool uiAlertHitMoreInfo(int x, int y, int w, int h) { return hitSlot(x, y, w, h, 4); }
bool uiAlertRingCentre(int w, int h, int& cx, int& cy) {
    if (!ringShown()) return false;
    const Lay L = layoutFor(w, h);
    int bx, by, bw, bh; btnRect(L, 0, bx, by, bw, bh);
    if (s_skinned) AlertSkin::buttonRect(0, bx, by, bw, bh);
    cx = bx + bw / 2; cy = by + bh / 2;
    return true;
}

// The +N MORE chip: top right of the header (its own row when narrow).
static void queueRect(const Lay& L, int& bx, int& by, int& bw, int& bh) {
    bw = L.compact ? 44 : X(L, 52); bh = L.compact ? 12 : Y(L, 14);
    bx = L.w - bw - 3;
    by = L.narrow ? L.tabsH + 3 : (L.compact ? 4 : Y(L, 3));
}
bool uiAlertHitQueue(int x, int y, int w, int h) {
    if (!s_queue) return false;
    if (s_skinned) { int bx, by, bw, bh; return AlertSkin::queueRect(bx, by, bw, bh) && inRect(x, y, bx, by, bw, bh, 6); }
    const Lay L = layoutFor(w, h);
    int bx, by, bw, bh; queueRect(L, bx, by, bw, bh);
    return inRect(x, y, bx, by, bw, bh, 6);
}

// The snooze chips, over the facts block.
static const char* const SNZ[4] = { "10 MIN", "30 MIN", "1 HOUR", "4 HOURS" };
static void chipRect(const Lay& L, int i, int& bx, int& by, int& bw, int& bh) {
    const int gap = L.compact ? 2 : 4;
    bw = (L.w - gap * 5) / 4;
    bh = L.compact ? 18 : Y(L, 30);
    bx = gap + i * (bw + gap);
    by = L.compact ? L.btnY - bh - 4 : Y(L, 134);
}
int uiAlertHitSnoozeChip(int x, int y, int w, int h) {
    if (!s_picking) return -1;
    if (s_skinned) {
        for (int i = 0; i < 4; i++) { int bx, by, bw, bh; if (AlertSkin::chipRect((uint8_t)i, bx, by, bw, bh) && inRect(x, y, bx, by, bw, bh, 3)) return i; }
        return -1;
    }
    const Lay L = layoutFor(w, h);
    for (int i = 0; i < 4; i++) {
        int bx, by, bw, bh; chipRect(L, i, bx, by, bw, bh);
        if (inRect(x, y, bx, by, bw, bh, 3)) return i;
    }
    return -1;
}

uint8_t uiAlertStops(int w, int h, int16_t* xs, int16_t* ys, uint8_t cap) {
    if (s_preview && cap >= 3) {   // the browser: next, before, and back -- OUTFIT's three
        const int y = h - browseH(h) / 2;
        xs[0] = (int16_t)(w - 17); ys[0] = (int16_t)y;
        xs[1] = 17;               ys[1] = (int16_t)y;
        xs[2] = (int16_t)(w / 2); ys[2] = (int16_t)(h / 2);
        return 3;
    }
    if (s_skinned) {
        uint8_t n = 0; int bx, by, bw, bh;
        for (uint8_t i = 0; i < 5 && n < cap; i++) if (AlertSkin::buttonRect(i, bx, by, bw, bh)) { xs[n] = (int16_t)(bx + bw / 2); ys[n] = (int16_t)(by + bh / 2); n++; }
        if (s_queue && n < cap && AlertSkin::queueRect(bx, by, bw, bh)) { xs[n] = (int16_t)(bx + bw / 2); ys[n] = (int16_t)(by + bh / 2); n++; }
        return n;
    }
    const Lay L = layoutFor(w, h);
    uint8_t n = 0;
    for (int i = 0; i < 5 && n < cap; i++) {
        int bx, by, bw, bh; btnRect(L, i, bx, by, bw, bh);
        xs[n] = (int16_t)(bx + bw / 2); ys[n] = (int16_t)(by + bh / 2); n++;
    }
    if (s_queue && n < cap) {
        int bx, by, bw, bh; queueRect(L, bx, by, bw, bh);
        xs[n] = (int16_t)(bx + bw / 2); ys[n] = (int16_t)(by + bh / 2); n++;
    }
    return n;
}

// ---- the verdict ---------------------------------------------------------
enum class Verdict : uint8_t { NEW, MET, FOLLOWING, SPAM };
static Verdict verdictOf(const Detection& d) {
    if (s_preview) return Verdict::FOLLOWING;
    if (s_spam) return Verdict::SPAM;
    if (Trail::following(d)) return Verdict::FOLLOWING;
    if (Regulars::nameFor(d.mac)) return Verdict::MET;
    return Verdict::NEW;
}
static const char* verdictWord(Verdict v) {
    switch (v) {
        case Verdict::FOLLOWING: return "FOLLOWING";
        case Verdict::MET:       return "MET BEFORE";
        case Verdict::SPAM:      return "SPAM FLOOD";
        default:                 return s_first ? "FIRST EVER" : "NEW";
    }
}
static uint16_t verdictColour(Verdict v) {
    switch (v) {
        case Verdict::FOLLOWING: return Theme::RED;
        case Verdict::MET:       return Theme::VAPOR_BLUE;
        case Verdict::SPAM:      return Theme::AMBER;
        default:                 return Theme::CYAN;
    }
}

// ---- small drawing helpers ------------------------------------------------
static void sysText(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, uint16_t bg, int size = 1) {
    t.setTextFont(1); t.setTextSize(size); t.setTextWrap(false);
    t.setTextColor(col, bg); t.setCursor(x, y); t.print(s);
}
static int sysW(TFT_eSPI& t, const char* s, int size) { t.setTextFont(1); t.setTextSize(size); return t.textWidth(s); }
static void sysTextC(TFT_eSPI& t, int cx, int y, const char* s, uint16_t col, uint16_t bg, int size = 1) {
    sysText(t, cx - sysW(t, s, size) / 2, y, s, col, bg, size);
}
static void sysTextR(TFT_eSPI& t, int rx, int y, const char* s, uint16_t col, uint16_t bg, int size = 1) {
    sysText(t, rx - sysW(t, s, size), y, s, col, bg, size);
}
// A little filled chip with a label; returns its width.
static int chip(TFT_eSPI& t, int x, int y, const char* s, uint16_t fill, uint16_t ink, int size, int h) {
    const int w = sysW(t, s, size) + 8;
    t.fillRect(x, y, w, h, fill);
    sysText(t, x + 4, y + (h - 8 * size) / 2, s, ink, fill, size);
    return w;
}
// A tick box with its word: green when it holds, grey when it does not.
static int tick(TFT_eSPI& t, int x, int y, const char* s, bool on, int size) {
    const int b = 7 * size;
    const uint16_t box = on ? Theme::GREEN : (uint16_t)0x4208;
    t.fillRect(x, y, b, b, box);
    if (on) {
        t.drawLine(x + 1 * size, y + 3 * size, x + 3 * size, y + 5 * size, Theme::BLACK);
        t.drawLine(x + 3 * size, y + 5 * size, x + 6 * size, y + 1 * size, Theme::BLACK);
    }
    sysText(t, x + b + 3 * size, y, s, on ? Theme::WHITE : (uint16_t)0x8410, Theme::BG, size);
    return x + b + 3 * size + sysW(t, s, size) + 9 * size;
}
static void stampOf(uint16_t minOfUptime, char* out, size_t n) {
    Clock::formatStamp((uint32_t)minOfUptime * 60000u, out, n);
}
static void kmText(uint16_t m, char* out, size_t n) {
    if (m >= 1000) snprintf(out, n, "%u.%u KM", (unsigned)(m / 1000), (unsigned)((m % 1000) / 100));
    else           snprintf(out, n, "%u M", (unsigned)m);
}
static const char* trendWord(int8_t tr) { return tr > 0 ? "CLOSER" : tr < 0 ? "FURTHER" : "HOLDING"; }

// The row this card is about, re-read from the engine so the ladder and the
// trail keep moving while it is up.
static void refresh(const DetectionEngine& eng) {
    for (uint8_t i = 0; i < eng.logCount(); i++) {
        const Detection* d = eng.logAt(i);
        if (d && memcmp(d->mac, s_last.mac, 6) == 0 && d->type == s_last.type) { s_last = *d; return; }
    }
}

// ---- the header ------------------------------------------------------------
static void drawHeader(TFT_eSPI& t, const Lay& L, Verdict v, uint16_t typeCol) {
    const int H = L.tabsH;
    const uint16_t vcol = verdictColour(v);
    const char* vw = verdictWord(v);
    const int w = L.w;
    // Bangers MD is a 27-row face: it fits a 30-row tab. The compact boards
    // get the system font at size 2.
    const bool bang = !L.compact;
    const char* tl = targetLabel(s_last.type);
    int vwid, twid;
    if (bang) {
        vwid = Theme::bangersTextWidth(vw, Theme::BangersSize::MD) + 14;
        twid = Theme::bangersTextWidth(tl, Theme::BangersSize::MD) + 14 + 26;   // and the icon after the name
        // Chips need about 110 px beside the tabs on a 320 panel; a long type
        // name takes its short form, then the verdict loses its tab to a word.
        const int room = L.narrow ? w : w - X(L, 112);
        if (vwid + twid > room) { tl = shortLabel(s_last.type); twid = Theme::bangersTextWidth(tl, Theme::BangersSize::MD) + 14 + 26; }
    } else {
        vwid = sysW(t, vw, 2) + 8;
        tl = shortLabel(s_last.type);
        twid = sysW(t, tl, 2) + 8;
        if (vwid + twid > w) { vwid = sysW(t, vw, 1) + 8; twid = sysW(t, tl, 1) + 8; }
    }
    // The verdict tab, then the type tab.
    t.fillRect(0, 0, vwid, H, vcol);
    t.fillRect(vwid, 0, twid, H, typeCol);
    if (bang) {
        Theme::drawBangersText(t, 7, (H - 27) / 2, vw, Theme::labelOn(vcol), Theme::BangersSize::MD);
        Theme::drawBangersText(t, vwid + 7, (H - 27) / 2, tl, Theme::labelOn(typeCol), Theme::BangersSize::MD);
        // The type's icon after its name, as tall as the name. Its bite is cut
        // in Theme::BG, lent the tab's colour for the one call.
        const int sz = 9, ix = vwid + 7 + Theme::bangersTextWidth(tl, Theme::BangersSize::MD) + 4 + sz * 135 / 100;
        const uint16_t keep = Theme::BG; Theme::BG = typeCol;
        Theme::drawTypeIcon(t, s_last.type, ix, (H - 27) / 2 + 24 - sz * 117 / 100, sz);
        Theme::BG = keep;
    } else {
        const int sz = (vwid + twid > w) ? 1 : (sysW(t, vw, 2) + 8 == vwid ? 2 : 1);
        sysText(t, 4, (H - 8 * sz) / 2, vw, Theme::labelOn(vcol), vcol, sz);
        sysText(t, vwid + 4, (H - 8 * sz) / 2, tl, Theme::labelOn(typeCol), typeCol, sz);
    }
    t.drawFastHLine(0, H, w, typeCol);

    // The chips: owner state, met before, how many wait. Beside the tabs on a
    // wide panel, on their own row under them when narrow.
    char own[16] = "", met[24] = "";
    uint16_t ownCol = Theme::AMBER;
    if (s_last.owner == OWNER_AWAY) {
        const uint16_t mins = (uint16_t)((uint16_t)(millis() / 60000u) - s_last.ownerMin);
        if (mins >= 120) snprintf(own, sizeof own, "AWAY %u H", (unsigned)(mins / 60));
        else if (mins >= 2) snprintf(own, sizeof own, "AWAY %u MIN", (unsigned)mins);
        else snprintf(own, sizeof own, "AWAY FROM OWNER");
    } else if (s_last.owner == OWNER_WITH) {
        snprintf(own, sizeof own, "WITH OWNER"); ownCol = (uint16_t)0x4208;
    }
    if (v == Verdict::SPAM) snprintf(met, sizeof met, "%u FAKES", (unsigned)s_spamFakes);
    else if (const char* nm = Regulars::nameFor(s_last.mac)) snprintf(met, sizeof met, "%s, %u DAYS", nm, (unsigned)Regulars::daysFor(s_last.mac));
    else if (s_night) snprintf(met, sizeof met, "AT NIGHT");
    else if (s_lastFree) snprintf(met, sizeof met, "LAST FREE ALERT");
    else snprintf(met, sizeof met, "MET NEVER");
    for (char* p = met; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    if (L.compact) { if (s_last.owner == OWNER_AWAY) snprintf(own, sizeof own, "AWAY"); met[0] = 0; }
    const int ch = L.compact ? 10 : Y(L, 12);
    int cx = vwid + twid + 5, cy = L.compact ? 5 : Y(L, 3);
    int row2 = L.compact ? 11 : Y(L, 17);
    if (L.narrow) { cx = 4; cy = H + 3; row2 = cy; }
    int qx, qy, qw, qh; queueRect(L, qx, qy, qw, qh);
    if (own[0] && (!L.narrow || true)) { const int cw = chip(t, cx, cy, own, ownCol, Theme::labelOn(ownCol), 1, ch); if (L.narrow) cx += cw + 4; }
    if (met[0]) {
        const uint16_t mc = (uint16_t)0x2945;
        const int mw = sysW(t, met, 1) + 8;
        if (!L.narrow || cx + mw <= (s_queue ? qx - 4 : L.w)) chip(t, cx, row2, met, mc, Theme::WHITE, 1, ch);
    }
    if (s_queue) {
        char q[12]; snprintf(q, sizeof q, "+%u MORE", (unsigned)s_queue);
        t.fillRect(qx, qy, qw, qh, Theme::BLACK);
        t.drawRect(qx, qy, qw, qh, Theme::WHITE);
        sysTextC(t, qx + qw / 2, qy + (qh - 8) / 2, q, Theme::WHITE, Theme::BLACK, 1);
        Theme::hotButton(qx, qy, qw, qh, "NEXT");
    }
}

// ---- the trail -------------------------------------------------------------
static void drawTrail(TFT_eSPI& t, const Lay& L, uint32_t now, uint16_t typeCol, bool wide) {
    const int ay = L.compact ? 24 : L.narrow ? L.tabsH + 22 : Y(L, 36);   // the times' row
    const int ly = L.compact ? 46 : L.narrow ? ay + 30 : Y(L, 66);       // the line
    const int left = X(L, 20), right = wide ? X(L, 240) : L.w - X(L, 20);
    const uint8_t n = s_last.stops;
    const uint8_t cur = Places::current();
    // Where the stops go along the line: to scale when every leg has a
    // distance, evenly otherwise.
    int xs[Detection::STOPS_N];
    uint16_t legM[Detection::STOPS_N] = {0, 0, 0};
    bool scaled = n >= 2;
    uint32_t total = 0;
    for (uint8_t i = 1; i < n; i++) {
        legM[i] = Places::distanceM(s_last.stopPlace[i - 1], s_last.stopPlace[i]);
        if (!legM[i]) scaled = false;
        total += legM[i];
    }
    const int x0 = left + X(L, 14), x1 = right - X(L, 20);
    if (n <= 1) { xs[0] = x0; }
    else if (scaled && total) {
        uint32_t acc = 0; xs[0] = x0;
        for (uint8_t i = 1; i < n; i++) { acc += legM[i]; xs[i] = x0 + (int)((x1 - x0) * acc / total); }
    } else {
        for (uint8_t i = 0; i < n; i++) xs[i] = n == 1 ? x0 : x0 + (x1 - x0) * i / (n - 1);
    }
    const int lx1 = n ? xs[n - 1] + X(L, 6) : x0;
    // The line, and the dotted way on past the last stop.
    t.fillRect(left, ly - 2, lx1 - left, 5, typeCol);
    t.fillRect(left, ly - 2, lx1 - left, 1, Theme::blend(typeCol, Theme::WHITE, 100));
    for (int xx = lx1 + 2; xx < right; xx += 5) t.fillRect(xx, ly, 2, 1, (uint16_t)0x8410);
    const int ts = L.ts;
    if (!n) {
        // Nothing heard anywhere yet: just the line and a word.
        sysText(t, left, ly + 14, "JUST HEARD. NO TRAIL YET.", (uint16_t)0x8410, Theme::BG, ts);
        return;
    }
    const bool hereLast = s_last.stopPlace[n - 1] == cur;
    for (uint8_t i = 0; i < n; i++) {
        const bool last = i == n - 1;
        const bool here = last && hereLast && !L.compact;
        const int nx = xs[i], r = (last ? 10 : 8) * (L.kx > 1.4f ? 2 : 1) / (L.kx > 1.4f ? 1 : 1);
        t.fillCircle(nx, ly, r, Theme::BG);
        t.drawCircle(nx, ly, r, typeCol);
        t.drawCircle(nx, ly, r - 1, typeCol);
        t.drawCircle(nx, ly, r - 2, typeCol);
        t.fillCircle(nx, ly, last ? 5 : 4, here ? Theme::PINK : typeCol);
        // The time above, the place and the stay below. HERE's time goes
        // below with it, because Squachy is standing on the line there.
        char tm[12]; stampOf(s_last.stopFirstMin[i], tm, sizeof tm);
        const uint16_t tcol = here ? Theme::PINK : Theme::WHITE;
        const int big = L.compact ? 1 : 2;
        if (!here) sysTextC(t, nx, ay, tm, last && hereLast ? Theme::PINK : tcol, Theme::BG, big);
        char place[12];
        if (Places::isHome(s_last.stopPlace[i])) snprintf(place, sizeof place, "HOME");
        else if (last && hereLast)               snprintf(place, sizeof place, "HERE");
        else                                     snprintf(place, sizeof place, "PLACE %u", (unsigned)(i + 1));
        int yy = ly + (L.compact ? 11 : 16);
        sysTextC(t, nx, yy, place, last && hereLast ? Theme::PINK : Theme::W95_FACE, Theme::BG, ts); yy += 10 * ts;
        if (here) { sysTextC(t, nx, yy, tm, Theme::PINK, Theme::BG, ts); yy += 10 * ts; }
        if (!L.compact) {
            const uint16_t stay = (uint16_t)(s_last.stopLastMin[i] - s_last.stopFirstMin[i]);
            char dw[12];
            if (here) snprintf(dw, sizeof dw, "%u MIN+", (unsigned)stay);
            else      snprintf(dw, sizeof dw, "%u MIN", (unsigned)stay);
            sysTextC(t, nx, yy, dw, (uint16_t)0x8410, Theme::BG, ts);
        }
        // The leg before this stop: its distance, when known.
        if (i && legM[i]) {
            char km[12]; kmText(legM[i], km, sizeof km);
            sysTextC(t, (xs[i - 1] + nx) / 2, ly - 6 - 8 * ts, km, (uint16_t)0x8410, Theme::BG, ts);
        }
    }
    // Squachy at HERE with the tag on the line right behind him; or, when the
    // device was last heard somewhere you have since left, the tag at its
    // last stop and nobody standing there.
    // A YOU pin standing on HERE (Squachy used to stand there, too small to
    // read); the type's icon lives after its name in the header now.
    const int hx = xs[n - 1];
    if (hereLast && !L.compact) {
        const int bob = (int)lroundf(sinf(now / 260.0f) * 1.2f);
        t.fillCircle(hx, ly - 17 + bob, 7, Theme::PINK);
        t.fillTriangle(hx - 6, ly - 14 + bob, hx + 6, ly - 14 + bob, hx, ly - 4, Theme::PINK);
        t.fillCircle(hx, ly - 17 + bob, 3, Theme::BG);
    }
}

// ---- the ticks ---------------------------------------------------------------
static void drawWhy(TFT_eSPI& t, const Lay& L, int y) {
    const uint16_t span = Trail::spanMin(s_last);
    char mins[12]; snprintf(mins, sizeof mins, "%u MIN", (unsigned)span);
    int x = X(L, 8);
    x = tick(t, x, y, "AWAY", s_last.owner == OWNER_AWAY, 1);
    x = tick(t, x, y, "EVERY STOP", s_last.stops >= 2 && Trail::atEveryStop(s_last), 1);
    x = tick(t, x, y, mins, span >= Trail::FOLLOW_MIN, 1);
    tick(t, x, y, trendWord(Trail::trend(s_last)), Trail::trend(s_last) > 0, 1);
}

// ---- the ladder ----------------------------------------------------------------
static void drawLadder(TFT_eSPI& t, const Lay& L) {
    const int sx = X(L, 246), sy = Y(L, 34), sw = L.w - sx - 4, sh = Y(L, 96);
    t.fillRect(sx, sy, sw, sh, (uint16_t)0x1108);
    sysText(t, sx + 3, sy + 2, "LAST 60 S", (uint16_t)0x8410, (uint16_t)0x1108, 1);
    const uint8_t n = s_last.sparkCount;
    const int rowH = (sh - 48) / Detection::SPARK_N > 4 ? (sh - 48) / Detection::SPARK_N : 4;
    for (uint8_t i = 0; i < n; i++) {
        // Oldest at the top, newest at the bottom in pink.
        const int8_t v = Trail::sparkAt(s_last, (uint8_t)(n - 1 - i));
        int len = ((int)v + 100) * (sw - 8) / 60;
        if (len < 2) len = 2; if (len > sw - 8) len = sw - 8;
        const bool newest = i == n - 1;
        const uint16_t col = newest ? Theme::PINK : Theme::blend((uint16_t)0x1108, Theme::CYAN, (uint16_t)(60 + 190 * (i + 1) / n));
        t.fillRect(sx + 4, sy + 12 + i * rowH, len, rowH > 3 ? 2 : 1, col);
    }
    char db[8]; snprintf(db, sizeof db, "%d", (int)s_last.rssi);
    const int dy = sy + sh - 38;
    Theme::drawBangersText(t, sx + 4, dy, db, Theme::WHITE, Theme::BangersSize::MD);
    char tr[16]; snprintf(tr, sizeof tr, "dBm %s", trendWord(Trail::trend(s_last)));
    sysText(t, sx + 4, sy + sh - 10, tr, Trail::trend(s_last) > 0 ? Theme::GREEN : Theme::W95_FACE, (uint16_t)0x1108, 1);
}

// ---- the facts ---------------------------------------------------------------
static void drawFacts(TFT_eSPI& t, const Lay& L, int y, int maxY) {
    const int ts = L.ts, rh = 10 * ts;
    const int kx = X(L, 6), vx = kx + 5 * 6 * ts + 4;
    t.drawFastHLine(kx, y - 4, L.w - 2 * kx, (uint16_t)0x2945);
    char who[48], up[20];
    const char* src = s_redacted ? "LOCKED" : vendorText(s_last);
    size_t i = 0; for (; src[i] && i + 1 < sizeof up; i++) up[i] = (src[i] >= 'a' && src[i] <= 'z') ? (char)(src[i] - 32) : src[i]; up[i] = 0;
    char pv[40];
    const char* nm = (s_last.name[0] && !s_redacted) ? Privacy::name(s_last.name, pv, sizeof pv) : "";
    snprintf(who, sizeof who, "%s %s", up, nm);
    char mac[24];
    if (s_redacted) snprintf(mac, sizeof mac, "-- LOCKED --"); else Privacy::mac(mac, sizeof mac, s_last.mac);
    char seen[48], first[12], last[12];
    const uint32_t now = millis();
    if (s_last.restored) Clock::formatEpochStamp(s_last.firstSeen, first, sizeof first);
    else                 Clock::formatStamp(s_last.firstSeen, first, sizeof first);
    if (s_last.active && now - s_last.lastSeen < 5000) snprintf(last, sizeof last, "NOW");
    else Clock::formatStamp(s_last.lastSeen, last, sizeof last);
    if (s_last.type == DetectionType::LORAWAN || s_last.channel == 0)
        snprintf(seen, sizeof seen, "FIRST %s  LAST %s  x%u", first, last, (unsigned)s_last.hits);
    else
        snprintf(seen, sizeof seen, "FIRST %s  LAST %s  x%u  CH %u", first, last, (unsigned)s_last.hits, (unsigned)s_last.channel);
    const uint16_t confCol = s_last.conf == Confidence::HIGH_CONF ? Theme::GREEN : s_last.conf == Confidence::MED_CONF ? Theme::AMBER : Theme::RED;
    char ownerRow[20]; ownerRowText(ownerRow, sizeof ownerRow);
    const bool hasOwner = ownerRow[0] != 0;
    struct Row { const char* k; const char* v; uint16_t c; } rows[5] = {
        { "WHO",  who,  Theme::WHITE }, { "MAC", mac, Theme::WHITE },
        { "SEEN", seen, Theme::WHITE },
        { hasOwner ? "OWNER" : "CONF", hasOwner ? ownerRow : confidenceLabel(s_last.conf), hasOwner ? (s_last.owner == OWNER_AWAY ? Theme::AMBER : Theme::WHITE) : confCol },
        { "CONF", confidenceLabel(s_last.conf), confCol },
    };
    // When they do not all fit, CONF goes first, then SEEN, then MAC: a
    // tracker's OWNER row always shows (alert_skin.cpp's factRows() does the same).
    const int want = hasOwner ? 5 : 4;
    int fit = 0; for (int yy = y; fit < want && yy + rh <= maxY; yy += rh) fit++;
    static const uint8_t byRank[5] = { 0, 3, 1, 2, 4 };   // WHO, OWNER, MAC, SEEN, CONF
    bool on[5] = { false, false, false, false, false };
    for (int r = 0; r < fit; r++) on[hasOwner ? byRank[r] : r] = true;
    for (int r = 0; r < want; r++) {
        if (!on[r]) continue;
        sysText(t, kx, y, rows[r].k, (uint16_t)0x8410, Theme::BG, ts);
        sysText(t, vx, y, rows[r].v, rows[r].c, Theme::BG, ts);
        y += rh;
    }
}

static void drawSnoozeChips(TFT_eSPI& t, const Lay& L) {
    int bx, by, bw, bh; chipRect(L, 0, bx, by, bw, bh);
    t.fillRect(0, by - 14, L.w, bh + 18, Theme::BG);
    sysText(t, bx, by - 11, "QUIET THIS ONE FOR:", Theme::AMBER, Theme::BG, 1);
    for (int i = 0; i < 4; i++) {
        chipRect(L, i, bx, by, bw, bh);
        Theme::drawButton(t, bx, by, bw, bh, SNZ[i], false, 1);
    }
}

static void drawButtons(TFT_eSPI& t, const Lay& L, uint16_t typeCol) {
    const char* labels[5] = { ringShown() ? "RING" : "HUNT", "WATCH", huntInSnoozeSlot() ? "HUNT" : "SNOOZE", "IGNORE", "INFO" };
    const bool already = IgnoreList::contains(s_last.mac);
    for (int i = 0; i < 5; i++) {
        int bx, by, bw, bh; btnRect(L, i, bx, by, bw, bh);
        const bool sunk = (i == 0 && s_ringing) || (i == 1 && s_watching) || (i == 3 && already);
        const char* lab = (i == 3 && already) ? "MUTED" : (i == 1 && s_watching) ? "WATCHING" : labels[i];
        Theme::drawWin95Button(t, bx, by, bw, bh, lab, sunk, i == 0);
        if (i == 0 && !sunk) {
            // The first slot in the type's own colour: it is the one that does
            // something to the device out there.
            t.fillRect(bx + 2, by + 2, bw - 4, bh - 4, typeCol);
            const uint16_t ink = Theme::labelOn(typeCol);
            t.setTextFont(1); t.setTextSize(1); t.setTextColor(ink, typeCol);
            const int tw = t.textWidth(lab);
            t.setCursor(bx + (bw - tw) / 2, by + (bh - 8) / 2); t.print(lab);
            t.setCursor(bx + (bw - tw) / 2 + 1, by + (bh - 8) / 2); t.print(lab);
        }
        Theme::hotButton(bx, by, bw, bh, labels[i]);
    }
}

// ---- the card as data, for the skins (alert_skin.h) ---------------------------------
// The OWNER fact row, trackers only: what the tag's own advert says. Empty for
// anything that is not a tracker (phones and cameras have no owner state).
static void ownerRowText(char* out, size_t n) {
    out[0] = 0;
    if (!TagRing::canRing(s_last.type) && s_last.type != DetectionType::AIRTAG && s_last.type != DetectionType::SAMSUNG_TAG &&
        s_last.type != DetectionType::GOOGLE_TAG && s_last.type != DetectionType::TILE) return;
    if (s_last.owner == OWNER_AWAY) {
        const uint16_t mins = (uint16_t)((uint16_t)(millis() / 60000u) - s_last.ownerMin);
        if (mins >= 120) snprintf(out, n, "AWAY %u H", (unsigned)(mins / 60));
        else if (mins >= 2) snprintf(out, n, "AWAY %u MIN", (unsigned)mins);
        else snprintf(out, n, "AWAY");
    } else if (s_last.owner == OWNER_WITH) snprintf(out, n, "WITH OWNER");
    else if (s_last.type == DetectionType::TILE) snprintf(out, n, "NOT BROADCAST");   // Tile says nothing about it
    else if (s_last.type == DetectionType::SAMSUNG_TAG) snprintf(out, n, "NOT READ YET");   // its bits are not decoded yet
    else snprintf(out, n, "NOT SET UP");   // an AirTag or Find Hub tag with no owner frame: never paired
}
static void ownerAndMet(Verdict v, char* own, size_t on, bool& away, char* met, size_t mn) {
    own[0] = 0; away = false;
    if (s_last.owner == OWNER_AWAY) {
        away = true;
        const uint16_t mins = (uint16_t)((uint16_t)(millis() / 60000u) - s_last.ownerMin);
        if (mins >= 120) snprintf(own, on, "AWAY %u H", (unsigned)(mins / 60));
        else if (mins >= 2) snprintf(own, on, "AWAY %u MIN", (unsigned)mins);
        else snprintf(own, on, "AWAY");
    } else if (s_last.owner == OWNER_WITH) snprintf(own, on, "WITH OWNER");
    if (v == Verdict::SPAM) snprintf(met, mn, "%u FAKES", (unsigned)s_spamFakes);
    else if (const char* nm = Regulars::nameFor(s_last.mac)) snprintf(met, mn, "%s, %u DAYS", nm, (unsigned)Regulars::daysFor(s_last.mac));
    else if (s_night) snprintf(met, mn, "AT NIGHT");
    else if (s_lastFree) snprintf(met, mn, "LAST FREE ALERT");
    else snprintf(met, mn, "MET NEVER");
    for (char* p = met; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
}
static void buildCard(AlertSkin::Card& c, Verdict v) {
    memset(&c, 0, sizeof c);
    c.verdict = (AlertSkin::Verdict)v;
    c.verdictWord = verdictWord(v);
    c.typeLabel = targetLabel(s_last.type);
    c.typeShort = shortLabel(s_last.type);
    c.type = s_last.type;
    ownerAndMet(v, c.owner, sizeof c.owner, c.ownerAway, c.met, sizeof c.met);
    ownerRowText(c.ownerRow, sizeof c.ownerRow);
    // the stops: to scale when every leg has a distance, evenly otherwise
    const uint8_t n = s_last.stops;
    c.nStops = n;
    c.hereLast = n && s_last.stopPlace[n - 1] == Places::current();
    uint16_t legM[Detection::STOPS_N] = { 0, 0, 0 };
    bool scaled = n >= 2; uint32_t total = 0;
    for (uint8_t i = 1; i < n; i++) { legM[i] = Places::distanceM(s_last.stopPlace[i - 1], s_last.stopPlace[i]); if (!legM[i]) scaled = false; total += legM[i]; }
    uint32_t acc = 0;
    for (uint8_t i = 0; i < n; i++) {
        AlertSkin::Stop& st = c.stops[i];
        if (n == 1) st.pos = 1000;
        else if (scaled && total) { acc += legM[i]; st.pos = (int16_t)(acc * 1000 / total); }
        else st.pos = (int16_t)(i * 1000 / (n - 1));
        stampOf(s_last.stopFirstMin[i], st.time, sizeof st.time);
        const bool here = i == n - 1 && c.hereLast;
        if (Places::isHome(s_last.stopPlace[i])) snprintf(st.place, sizeof st.place, "HOME");
        else if (here) snprintf(st.place, sizeof st.place, "HERE");
        else snprintf(st.place, sizeof st.place, "PLACE %u", (unsigned)(i + 1));
        const uint16_t stay = (uint16_t)(s_last.stopLastMin[i] - s_last.stopFirstMin[i]);
        snprintf(st.dwell, sizeof st.dwell, here ? "%u MIN+" : "%u MIN", (unsigned)stay);
        if (i && legM[i]) kmText(legM[i], st.leg, sizeof st.leg);
    }
    // the reasons
    const uint16_t span = Trail::spanMin(s_last);
    const int8_t tr = Trail::trend(s_last);
    snprintf(c.ticks[0].label, sizeof c.ticks[0].label, "AWAY");
    c.ticks[0].on = s_preview || s_last.owner == OWNER_AWAY;
    snprintf(c.ticks[1].label, sizeof c.ticks[1].label, "EVERY STOP");
    c.ticks[1].on = s_preview || (s_last.stops >= 2 && Trail::atEveryStop(s_last));
    snprintf(c.ticks[2].label, sizeof c.ticks[2].label, "%u MIN", (unsigned)span);
    c.ticks[2].on = s_preview || span >= Trail::FOLLOW_MIN;
    snprintf(c.ticks[3].label, sizeof c.ticks[3].label, "%s", trendWord(tr));
    c.ticks[3].on = tr > 0;
    // the last minute of signal, oldest first
    c.sparkN = s_last.sparkCount;
    for (uint8_t i = 0; i < c.sparkN; i++) c.spark[i] = Trail::sparkAt(s_last, (uint8_t)(c.sparkN - 1 - i));
    c.rssi = s_last.rssi;
    c.trend = trendWord(tr);
    c.trendUp = tr > 0;
    // the facts
    char up[20];
    const char* src = s_redacted ? "LOCKED" : vendorText(s_last);
    size_t i = 0; for (; src[i] && i + 1 < sizeof up; i++) up[i] = (src[i] >= 'a' && src[i] <= 'z') ? (char)(src[i] - 32) : src[i]; up[i] = 0;
    char pv[40];
    const char* nm = (s_last.name[0] && !s_redacted) ? Privacy::name(s_last.name, pv, sizeof pv) : "";
    snprintf(c.who, sizeof c.who, "%s %s", up, nm);
    if (s_redacted) snprintf(c.mac, sizeof c.mac, "-- LOCKED --"); else Privacy::mac(c.mac, sizeof c.mac, s_last.mac);
    char first[12], last[12];
    const uint32_t now = millis();
    if (s_last.restored) Clock::formatEpochStamp(s_last.firstSeen, first, sizeof first);
    else                 Clock::formatStamp(s_last.firstSeen, first, sizeof first);
    if ((s_last.active && now - s_last.lastSeen < 5000) || s_preview) snprintf(last, sizeof last, "NOW");
    else Clock::formatStamp(s_last.lastSeen, last, sizeof last);
    if (s_last.type == DetectionType::LORAWAN || s_last.channel == 0)
        snprintf(c.seen, sizeof c.seen, "FIRST %s  LAST %s  x%u", first, last, (unsigned)s_last.hits);
    else
        snprintf(c.seen, sizeof c.seen, "FIRST %s  LAST %s  x%u  CH %u", first, last, (unsigned)s_last.hits, (unsigned)s_last.channel);
    c.conf = confidenceLabel(s_last.conf);
    c.confLevel = s_last.conf == Confidence::HIGH_CONF ? 2 : s_last.conf == Confidence::MED_CONF ? 1 : 0;
    // the buttons, as the TRAIL card labels them
    const bool already = IgnoreList::contains(s_last.mac);
    static const char* const HOT[5] = { nullptr, "WATCH", "SNOOZE", "IGNORE", "INFO" };
    c.queue = s_queue;
    c.btn[0] = ringShown() ? "RING" : "HUNT"; c.hot[0] = c.btn[0];
    c.btn[1] = s_watching ? "WATCHING" : "WATCH";
    c.btn[2] = huntInSnoozeSlot() ? "HUNT" : "SNOOZE";
    c.btn[3] = already ? "MUTED" : "IGNORE";
    c.btn[4] = "INFO";
    for (int k = 1; k < 5; k++) c.hot[k] = HOT[k];
    c.hot[2] = c.btn[2];
    c.sunk[0] = s_ringing; c.sunk[1] = s_watching; c.sunk[3] = already;
    c.picking = s_picking;
}

// ALERT CARD's PREVIEW: an AirTag three stops into following, made up whole.
// A locally administered address, so it can never be a real device's row.
void uiAlertMakeSample(Detection& d) {
    memset(&d, 0, sizeof d);
    const uint8_t mac[6] = { 0x02, 0x51, 0x55, 0x41, 0x43, 0x48 };
    memcpy(d.mac, mac, 6);
    d.type = DetectionType::AIRTAG;
    d.conf = Confidence::MED_CONF;
    d.rssi = -58;
    d.hits = 7;
    d.channel = 0;
    d.vendor = "Apple";
    snprintf(d.name, sizeof d.name, "AirTag");
    const uint32_t now = millis();
    const uint16_t nowMin = (uint16_t)(now / 60000u);
    d.firstSeen = now > 41u * 60000u ? now - 41u * 60000u : 0;
    d.lastSeen = now;
    d.active = true;
    d.owner = OWNER_AWAY;
    d.ownerMin = (uint16_t)(nowMin - 120u);
    const uint8_t here = Places::current();
    d.stops = 3;
    d.stopPlace[0] = (uint8_t)(here + 1); d.stopPlace[1] = (uint8_t)(here + 2); d.stopPlace[2] = here;
    const uint16_t firsts[3] = { 41, 24, 7 }, stays[3] = { 4, 5, 7 };
    for (int i = 0; i < 3; i++) {
        d.stopFirstMin[i] = (uint16_t)(nowMin - (nowMin >= firsts[i] ? firsts[i] : nowMin));
        d.stopLastMin[i] = (uint16_t)(d.stopFirstMin[i] + stays[i]);
    }
    for (uint8_t i = 0; i < Detection::SPARK_N; i++) d.spark[i] = (int8_t)(-80 + i * 2);
    d.sparkHead = 0; d.sparkCount = Detection::SPARK_N;
}

// ---- the style browser (ALERT CARD > STYLE) -----------------------------------------
// OUTFIT's footer, over the card's buttons (which do nothing on a made-up card):
// the style's name, which of the twelve, an arrow in each corner.
static int browseH(int h) { return h >= 300 ? 48 : 36; }
static void drawBrowseStrip(TFT_eSPI& t, int w, int h) {
    const int fh = browseH(h), y = h - fh;
    t.fillRect(0, y, w, fh, Theme::BG);
    t.drawFastHLine(0, y, w, Theme::CYAN);
    const uint8_t id = Settings::alertSkin();
    const char* name = AlertSkin::name(id);
    t.setTextFont(1); t.setTextSize(2); t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor((w - t.textWidth(name)) / 2, y + 4); t.print(name);
    char buf[32];
    const unsigned at = AlertSkin::availableRank(id), of = AlertSkin::availableCount();
    if (!AlertSkin::fits(w, h)) snprintf(buf, sizeof buf, "%u / %u  (TRAIL ON THIS SCREEN)", at, of);
    else snprintf(buf, sizeof buf, "%u / %u", at, of);
    t.setTextSize(1); t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor((w - t.textWidth(buf)) / 2, y + 24); t.print(buf);
    const int ay = y + fh / 2 - 10;
    t.fillTriangle(24, ay, 10, ay + 10, 24, ay + 20, Theme::VAPOR_PINK);
    t.fillTriangle(w - 24, ay, w - 10, ay + 10, w - 24, ay + 20, Theme::CYAN);
}
bool uiAlertPreviewArrow(int x, int y, int w, int h) {
    if (!s_preview || y < h - browseH(h) - 8) return false;
    const int zone = w / 5;
    if (x >= zone && x <= w - zone) return false;
    Settings::cycleAlertSkin(x < zone);
    s_alertStart = millis();
    s_queueAt = 0;
    s_skinned = false;
    AlertSkin::forget();
    return true;
}

// ---- the card -------------------------------------------------------------------
void uiAlertTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                 bool infoPending, const char* infoTypeName, const char* infoText) {
    const int w = t.width(), h = t.height();
    const Lay L = layoutFor(w, h);
    refresh(eng);

    const uint32_t elapsed = now - s_alertStart;
    const uint8_t skin = Settings::alertSkin();
    const bool skinned = skin != AlertSkin::TRAIL && AlertSkin::available(skin) && AlertSkin::fits(w, h);
    // The glitch bursts are TRAIL's way of shouting. A skin opens with its own
    // flourish instead, and static flashing over a sunset just reads as flicker.
    while (!skinned && s_glitchStep < 200 && elapsed >= glitchStepOffsetMs(s_glitchStep)) {
        Theme::triggerGlitchBurst(glitchStepLevel(s_glitchStep));
        s_glitchStep++;
    }

    const Verdict v = verdictOf(s_last);
    if (skinned) {
        AlertSkin::Card c;
        buildCard(c, v);
        AlertSkin::draw(t, skin, c, now, s_alertStart, s_queueAt, Settings::alertAnimate() ? 0 : 2, Settings::alertSunset(), Settings::paletteIndex());
        s_skinned = true;
        if (s_preview) drawBrowseStrip(t, w, h);
        if (infoPending) Theme::drawInfoPanel(t, w, h, now, infoTypeName, infoText);
        return;
    }
    s_skinned = false;
    t.fillRect(0, 0, w, h, Theme::BG);
    const uint16_t typeCol = Theme::colorFor(s_last.type);
    drawHeader(t, L, v, typeCol);

    const bool wide = !L.narrow && !L.compact;
    drawTrail(t, L, now, typeCol, wide);
    if (L.compact) {
        // 240x135: the line, the ticks, one line of signal, one of facts.
        const int wy = 72;
        drawWhy(t, L, wy);
        char sig[40]; snprintf(sig, sizeof sig, "%d dBm %s  x%u", (int)s_last.rssi, trendWord(Trail::trend(s_last)), (unsigned)s_last.hits);
        sysText(t, 8, wy + 12, sig, Theme::WHITE, Theme::BG, 1);
        char mac[24]; if (s_redacted) snprintf(mac, sizeof mac, "-- LOCKED --"); else Privacy::mac(mac, sizeof mac, s_last.mac);
        sysTextR(t, w - 8, wy + 12, mac, (uint16_t)0x8410, Theme::BG, 1);
    } else {
        const int wy = L.narrow ? Y(L, 66) + 30 + 50 : Y(L, 116);
        drawWhy(t, L, wy);
        if (wide) drawLadder(t, L);
        else {
            // No room beside the trail: the signal as a line of its own.
            char sig[40]; snprintf(sig, sizeof sig, "SIGNAL %d dBm, %s", (int)s_last.rssi, trendWord(Trail::trend(s_last)));
            sysText(t, X(L, 8), wy + 12, sig, Trail::trend(s_last) > 0 ? Theme::GREEN : Theme::WHITE, Theme::BG, 1);
        }
        drawFacts(t, L, L.narrow ? wy + 26 : Y(L, 132), L.btnY - 4);
        if (s_picking) drawSnoozeChips(t, L);
    }
    drawButtons(t, L, typeCol);
    if (s_preview) drawBrowseStrip(t, w, h);

    Theme::drawGlitchStatic(t, 0, 0, w, h);
    if (infoPending) Theme::drawInfoPanel(t, w, h, now, infoTypeName, infoText);
}
