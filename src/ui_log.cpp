// SquachWatch-CYD — log screen implementation
#include "ui_log.h"
#include "ui_scroll.h"
#include "clock.h"
#include "theme.h"
#include "privacy.h"
#include "settings.h"
#include "blackbox.h"
#include "detection.h"
#include "log_index.h"
#include "regulars.h"
#include "ignore_list.h"
#include <Arduino.h>
#include <ctype.h>
#include <string.h>

static int g_scroll = 0;

// ---- where a row comes from ---------------------------------------------
// The first rows are the engine's ring in RAM -- what the board can see, or
// saw a moment ago. Past those, the list carries straight on into the black
// box's sightings in flash, which is the history the ring used to hold
// before it was cut from two hundred rows to sixty-four.
//
// Flash is read a page at a time and held until the page changes, because
// the list is redrawn every frame and a read per row per frame would put the
// cache off for both cores twenty times a second. PAGE is a screenful of
// rows plus room to scroll before it has to read again.
static const uint8_t PAGE = 12;
static BlackBox::DetRecord s_page[PAGE];
static uint16_t s_pageFrom = 0;
static uint8_t  s_pageGot  = 0;
static bool     s_pageOk   = false;

static void invalidatePage() { s_pageOk = false; }

// ---- one row per device -----------------------------------------------------
// The flash keeps a record every time a device is first seen or comes back,
// today's included, so the same device used to appear as its live row AND a
// grey KEPT copy, and once more per visit on other days. LogIndex keeps the
// newest of each (MAC + type) that the ring does not already show; see
// log_index.h. Rebuilt when the LOG opens and when the ring changes -- a
// device from yesterday seen again today moves to the live rows, and its grey
// copy has to go -- but at most once a second, since a rebuild reads the
// whole flash history and a busy room adds rows several times a second.
static bool     s_indexWanted = true;
static uint8_t  s_sigCount    = 0xFF;
static uint32_t s_sigNewest   = 0;
static uint16_t s_sigKept     = 0;
static uint32_t s_indexAt     = 0;

static void ensureIndex(const DetectionEngine& eng) {
    const uint8_t n = eng.logCount() > 64 ? 64 : eng.logCount();
    const Detection* newest = n ? eng.logAt(0) : nullptr;
    const uint32_t newestKey = newest ? LogIndex::key(newest->mac, (uint8_t)newest->type) : 0;
    const uint16_t kept = BlackBox::detectionsKept();
    const bool cleared = kept < s_sigKept;   // a CLR: positions mean nothing now
    const bool changed = n != s_sigCount || newestKey != s_sigNewest;
    if (!s_indexWanted && !cleared && !changed) return;
    if (!s_indexWanted && !cleared && millis() - s_indexAt < 1000) return;

    uint32_t keys[64];   // the ring's LOG_CAP; logCount() never exceeds it
    for (uint8_t i = 0; i < n; i++) {
        const Detection* d = eng.logAt(i);
        keys[i] = d ? LogIndex::key(d->mac, (uint8_t)d->type) : 0;
    }
    LogIndex::rebuild(keys, n);
    s_indexWanted = false;
    s_sigCount = n;
    s_sigNewest = newestKey;
    s_sigKept = kept;
    s_indexAt = millis();
    invalidatePage();
}

uint16_t uiLogRowCount(const DetectionEngine& eng) {
    ensureIndex(eng);
    return (uint16_t)(eng.logCount() + LogIndex::count());
}

const Detection* uiLogRow(const DetectionEngine& eng, int idx) {
    if (idx < 0) return nullptr;
    if (idx < (int)eng.logCount()) return eng.logAt((uint8_t)idx);

    const uint16_t want = (uint16_t)(idx - eng.logCount());
    if (want >= LogIndex::count()) return nullptr;
    if (!s_pageOk || want < s_pageFrom || want >= (uint16_t)(s_pageFrom + s_pageGot)) {
        uint16_t at[PAGE];
        uint8_t n = 0;
        for (uint16_t r = want; r < LogIndex::count() && n < PAGE; r++) {
            uint16_t p;
            if (!LogIndex::position(r, p)) break;
            at[n++] = p;
        }
        s_pageFrom = want;
        s_pageGot  = (uint8_t)BlackBox::readDetectionsAt(at, n, s_page);
        // Short only if records were worn away mid-list; anything past
        // them would land on the wrong row, so the page ends there.
        s_pageOk   = s_pageGot > 0;
        if (!s_pageOk) return nullptr;
    }
    if (want >= (uint16_t)(s_pageFrom + s_pageGot)) return nullptr;
    const BlackBox::DetRecord& r = s_page[want - s_pageFrom];

    // Built into one row of its own, because everything downstream -- the
    // drawing, the long press, the confirm panel -- speaks Detection.
    static Detection row;
    memset(&row, 0, sizeof row);
    memcpy(row.mac, r.mac, 6);
    row.rssi      = r.rssi;
    row.channel   = r.channel;
    row.type      = r.type < (uint8_t)DetectionType::COUNT ? (DetectionType)r.type
                                                           : DetectionType::UNKNOWN;
    row.conf      = r.conf <= (uint8_t)Confidence::HIGH_CONF ? (Confidence)r.conf
                                                             : Confidence::LOW_CONF;
    row.restored  = 1;
    row.firstSeen = r.epoch;        // a wall-clock second, not a millis() stamp
    row.hits      = r.hits ? r.hits : 1;
    row.prevRssi  = r.rssi;
    memcpy(row.name, r.name, sizeof row.name);
    row.name[sizeof row.name - 1] = '\0';
    // The vendor is a pointer into the signature tables everywhere else, and
    // the text read back from flash has nowhere to live -- so the row keeps
    // the name it was given and leaves the vendor empty, which vendorText()
    // already handles.
    return &row;
}

void uiLogInit(TFT_eSPI& t) {
    g_scroll = 0;
    invalidatePage();
    s_indexWanted = true;
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiLogScroll(int delta) {
    invalidatePage();
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

// Confirm panel geometry/drawing/hit-test. A 2x2 grid (WATCH/HUNT on
// top, INFO/CANCEL below) rather than the single row of three
// ui_rawscan.cpp's identical-minus-INFO panel uses (see that module's
// own comment for why it doesn't get a fourth button) -- a single row
// of four got too cramped on the narrowest 240px rotation once CANCEL
// had to share the row with three other labels.
static void confirmRects(int screenW, int screenH,
                          int& px, int& py, int& pw, int& ph,
                          int& wX, int& wY, int& wW, int& wH,
                          int& huX, int& huY, int& huW, int& huH,
                          int& infX, int& infY, int& infW, int& infH,
                          int& igX, int& igY, int& igW, int& igH,
                          int& cnX, int& cnY, int& cnW, int& cnH) {
    pw = screenW - 40;
    if (pw > 240) pw = 240;
    if (Theme::compact()) {
        // 135 rows: no question line, the name in the built-in face, and
        // shorter buttons closer together. Same five, same places.
        pw = screenW - 16;
        ph = 104;
        px = (screenW - pw) / 2;
        py = (screenH - ph) / 2;
        const int margin = 6, gap = 4, btnH = 20;
        const int btnW = (pw - 2 * margin - gap) / 2;
        cnY = py + ph - btnH - margin; cnH = btnH; cnX = px + margin; cnW = pw - 2 * margin;
        igY = infY = cnY - gap - btnH; igH = infH = btnH;
        igX = px + margin; igW = btnW; infX = igX + btnW + gap; infW = btnW;
        wY = huY = igY - gap - btnH; wH = huH = btnH;
        wX = px + margin; wW = btnW; huX = wX + btnW + gap; huW = btnW;
        return;
    }
    // Was 132 for a 2x2 grid. IGNORE makes it three rows: WATCH/HUNT,
    // IGNORE/MORE INFO, then CANCEL alone across the bottom. CANCEL gets
    // the full width because it is the one button you hit by reflex and
    // the one that must never be mistaken for its neighbour.
    ph = 164;
    px = (screenW - pw) / 2;
    py = (screenH - ph) / 2;
    const int margin = 10, gap = 8, btnH = 24;
    int btnW = (pw - 2 * margin - gap) / 2;

    cnY = py + ph - btnH - margin;
    cnH = btnH;
    cnX = px + margin;
    cnW = pw - 2 * margin;

    igY = infY = cnY - gap - btnH;
    igH = infH = btnH;
    igX  = px + margin;       igW  = btnW;
    infX = igX + btnW + gap;  infW = btnW;

    wY = huY = igY - gap - btnH;
    wH = huH = btnH;
    wX  = px + margin;      wW  = btnW;
    huX = wX + btnW + gap;  huW = btnW;
}

LogConfirmTap uiLogHitConfirm(int x, int y, int screenW, int screenH) {
    int px, py, pw, ph, wX, wY, wW, wH, huX, huY, huW, huH, infX, infY, infW, infH,
        igX, igY, igW, igH, cnX, cnY, cnW, cnH;
    confirmRects(screenW, screenH, px, py, pw, ph, wX, wY, wW, wH, huX, huY, huW, huH,
                 infX, infY, infW, infH, igX, igY, igW, igH, cnX, cnY, cnW, cnH);
    if (x >= wX && x <= wX + wW && y >= wY && y <= wY + wH) return LogConfirmTap::WATCH;
    if (x >= huX && x <= huX + huW && y >= huY && y <= huY + huH) return LogConfirmTap::HUNT;
    if (x >= igX && x <= igX + igW && y >= igY && y <= igY + igH) return LogConfirmTap::IGNORE;
    if (x >= infX && x <= infX + infW && y >= infY && y <= infY + infH) return LogConfirmTap::INFO;
    if (x >= cnX && x <= cnX + cnW && y >= cnY && y <= cnY + cnH) return LogConfirmTap::CANCEL;
    return LogConfirmTap::NONE;
}

static bool    s_whereHas = false;
static int32_t s_whereLat = 0, s_whereLon = 0;
void uiLogSetConfirmWhere(bool has, int32_t lat7, int32_t lon7) {
    s_whereHas = has; s_whereLat = lat7; s_whereLon = lon7;
}

// "SEEN AT 41.76870,-72.30599": five decimals is about a metre, more than any
// fix this board is given can claim.
static void whereText(char* out, size_t n) {
    const long la = (long)s_whereLat, lo = (long)s_whereLon;
    snprintf(out, n, "SEEN AT %s%ld.%05ld,%s%ld.%05ld",
             la < 0 ? "-" : "", labs(la) / 10000000L, (labs(la) % 10000000L) / 100L,
             lo < 0 ? "-" : "", labs(lo) / 10000000L, (labs(lo) % 10000000L) / 100L);
}

static void drawConfirmPanel(TFT_eSPI& t, int w, int h, const char* label, bool watched, bool hunted, bool ignored) {
    int px, py, pw, ph, wX, wY, wW, wH, huX, huY, huW, huH, infX, infY, infW, infH,
        igX, igY, igW, igH, cnX, cnY, cnW, cnH;
    confirmRects(w, h, px, py, pw, ph, wX, wY, wW, wH, huX, huY, huW, huH,
                 infX, infY, infW, infH, igX, igY, igW, igH, cnX, cnY, cnW, cnH);
    t.fillRoundRect(px, py, pw, ph, 6, Theme::BG);
    t.drawRoundRect(px, py, pw, ph, 6, Theme::PURPLE);

    if (Theme::compact()) {
        t.setTextWrap(false);
        t.setTextSize(2);
        char nm[24];
        snprintf(nm, sizeof nm, "%s", label);
        while (nm[0] && t.textWidth(nm) > pw - 12) nm[strlen(nm) - 1] = '\0';
        t.setTextColor(Theme::RED, Theme::BG);
        t.setCursor(px + (pw - t.textWidth(nm)) / 2, py + 6);
        t.print(nm);
        Theme::drawButton(t, wX, wY, wW, wH, watched ? "UNWATCH" : "WATCH", watched, 1);
        Theme::drawButton(t, huX, huY, huW, huH, hunted ? "REMOVE HUNT" : "ADD HUNT", hunted, 1);
        Theme::drawButton(t, igX, igY, igW, igH, ignored ? "UN-IGNORE" : "IGNORE", ignored, 1);
        Theme::drawButton(t, infX, infY, infW, infH, "MORE INFO", false, 1);
        Theme::drawButton(t, cnX, cnY, cnW, cnH, "CANCEL", false, 1);
        return;
    }

    t.setTextWrap(false);
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    char where[40];
    if (s_whereHas) whereText(where, sizeof where);
    const char* q = s_whereHas ? where : "TRACK THIS TARGET?";
    int qw = t.textWidth(q);
    t.setCursor(px + (pw - qw) / 2, py + 8);
    t.print(q);

    // The Bangers glyph table is uppercase-only (it was built for
    // hardcoded shout-caps strings like "RING"/"FLOCK") -- lowercase
    // letters have no glyph and silently vanish (bangersFind() returns
    // null, drawBangersPass() skips it), which is why a real device
    // label like "Apple" rendered as just "A". Upper-case a local copy
    // before measuring/drawing rather than touching the caller's label.
    char upperLabel[32];
    uint8_t li = 0;
    for (; label[li] && li < sizeof(upperLabel) - 1; li++) upperLabel[li] = toupper((unsigned char)label[li]);
    upperLabel[li] = 0;

    int lw = Theme::bangersTextWidth(upperLabel, Theme::BangersSize::MD);
    int maxLw = pw - 16;
    if (lw > maxLw) lw = maxLw; // clipped, not shrunk -- real labels fit comfortably as-is
    Theme::drawBangersText(t, px + (pw - lw) / 2, py + 26, upperLabel, Theme::RED, Theme::BangersSize::MD);

    // See ui_rawscan.cpp's copy of this panel: toggling, so the label names
    // the next tap rather than the thing already done.
    Theme::drawButton(t, wX, wY, wW, wH, watched ? "UNWATCH" : "WATCH", watched);
    // Toggles like WATCH beside it -- see that button's comment.
    Theme::drawButton(t, huX, huY, huW, huH, hunted ? "REMOVE HUNT" : "ADD HUNT", hunted);
    // Toggles too (issue #18): the only way to tell a device was ignored used
    // to be tapping IGNORE again and reading which toast came back.
    Theme::drawButton(t, igX, igY, igW, igH, ignored ? "UN-IGNORE" : "IGNORE", ignored);
    Theme::drawButton(t, infX, infY, infW, infH, "MORE INFO", false);
    Theme::drawButton(t, cnX, cnY, cnW, cnH, "CANCEL", false);
}

// Row geometry, shared by uiLogTick() (drawing) and uiLogRowAt() (hit
// testing) so the two can never drift apart -- same reasoning as
// ui_rawscan.cpp's rowLayout(): rowH depends on live font metrics, not
// a compile-time constant.
static void rowLayout(TFT_eSPI& t, int bodyTop, int& detailY, int& rowH) {
    if (Theme::compact()) {
        // One line a row on a 135-row screen: type, name or address,
        // signal, time. The address and the count are a press away, on
        // MORE INFO.
        t.setTextSize(1);
        detailY = 3;
        rowH = t.fontHeight() + 7;
        (void)bodyTop;
        return;
    }
    t.setTextSize(2);
    int nameH = t.fontHeight();
    t.setTextSize(1);
    int detailH = t.fontHeight();
    // Each row is a card now, the same panel Settings draws: two pixels of
    // air above the type label, three below the detail line, and the card's
    // own two-pixel gap under that.
    const int topPad = 3;
    detailY = topPad + nameH;
    rowH = topPad + nameH + detailH + 3 + 2;
    (void)bodyTop;
}

int uiLogRowAt(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    Theme::ButtonBarGeom bar = Theme::computeButtonBar(screenW, screenH);
    const int bodyTop = 16, bodyBottom = bar.y - 4;
    if (y < bodyTop || y >= bodyBottom) return -1;
    int detailY, rowH;
    rowLayout(t, bodyTop, detailY, rowH);
    return g_scroll + (y - bodyTop) / rowH;
}

void uiLogTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, int scrollOffset,
               bool confirmPending, const char* confirmLabel,
               bool infoPending, const char* infoTypeName, const char* infoText,
               bool confirmWatched, bool confirmHunted, bool confirmIgnored) {
    int w = t.width();
    int h = t.height();

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    const int bodyTop    = 16;
    const int bodyBottom = bar.y - 4;
    const int bodyH      = bodyBottom - bodyTop;


    // Body -- whatever background style CLEAR is showing, drawn at
    // reduced strength behind the list, same treatment Settings' body
    // got (see Theme::dimPaletteForOverlay()'s comment). Rows never had
    // a full-row fillRect of their own to begin with (just a
    // drawFastHLine separator plus per-field two-arg setTextColor()
    // calls, which already erase their own glyph cells against
    // Theme::BG), so this drops in with no further changes needed.
    Theme::Palette saved = Theme::dimPaletteForOverlay(179);
        // The background starts at the TOP OF THE SCREEN, not at the body's own
    // top. Those first sixteen rows used to be the title bar's; nothing owns
    // them now except the two corner buttons, which draw their own opaque
    // boxes over whatever is behind them. Leaving the animation to start
    // below them left a flat dead strip across the top of this screen --
    // the same relic CLEAR had, and the same fix.
    //
    // Only the BACKGROUND moves. Everything else on this screen still
    // begins where it did, so no content shifts.
    const int bgTop = 0;
if (!Theme::stillBackdrop(t)) switch (Settings::background()) {
        case Settings::Background::STARFIELD: Theme::drawStarfield(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TOASTERS:   Theme::drawFlyingToasters(t, now, bgTop, bodyBottom); break;
        case Settings::Background::AQUARIUM:   Theme::drawAquarium(t, now, bgTop, bodyBottom); break;
        case Settings::Background::TERMINAL:   Theme::drawTerminalLog(t, now, bgTop, bodyBottom); break;
        case Settings::Background::FIREFLIES:  Theme::drawFireflies(t, now, bgTop, bodyBottom); break;
        case Settings::Background::FIRE:       Theme::drawFire(t, now, bgTop, bodyBottom); break;
        case Settings::Background::SNOWFALL:   Theme::drawSnowfall(t, now, bgTop, bodyBottom); break;
        case Settings::Background::SPECTRUM:   Theme::drawGibson(t, now, bgTop, bodyBottom, eng); break;
        case Settings::Background::SYNTHWAVE: Theme::drawSynthwave(t, now, bgTop, bodyBottom); break;
        // Fills rather than skips -- see the note in drawActiveBackground.
        case Settings::Background::BLACK:      t.fillRect(0, bgTop, t.width(), bodyBottom - bgTop, Theme::BG); break;
        default:                               Theme::drawDigitalRain(t, now, bgTop, bodyBottom, true); break;
    }
    Theme::restorePalette(saved);
    // Title bar
    char title[32];
    snprintf(title, sizeof(title), ">> LOG  (%u) <<", (unsigned)uiLogRowCount(eng));
    Theme::drawTitleBar(t, title);

    const uint16_t count = uiLogRowCount(eng);

    if (count == 0) {
        // Make the empty state impossible to mistake for a broken screen.
        float pulse = 0.55f + 0.45f * sinf((float)(now % 2000) / 2000.0f * 6.2831853f);
        uint16_t col = Theme::blend(Theme::GREEN, Theme::CYAN, (uint16_t)(pulse * 200.0f));
        t.setTextSize(3);
        t.setTextColor(col, Theme::BG);
        const char* msg = "LOG EMPTY";
        int mw = t.textWidth(msg);
        t.setCursor((w - mw) / 2, bodyTop + bodyH / 3);
        t.print(msg);

        t.setTextSize(1);
        t.setTextColor(Theme::CYAN, Theme::BG);
        const char* sub = "no detections yet";
        int sw = t.textWidth(sub);
        t.setCursor((w - sw) / 2, bodyTop + bodyH / 3 + 35);
        t.print(sub);

        Theme::drawButtonBar(t, ButtonId::LOG, Theme::ButtonBarMode::LOG);
        if (infoPending)        Theme::drawInfoPanel(t, w, h, now, infoTypeName, infoText);
        else if (confirmPending) drawConfirmPanel(t, w, h, confirmLabel, confirmWatched, confirmHunted, confirmIgnored);
        return;
    }

    // Row height derived from the actual rendered text heights, not a
    // guessed constant -- the size-2 type label is 16px tall, but the
    // MAC/RSSI/hits line below it used to start at a fixed y+14,
    // running the two lines into each other (confirmed on real
    // hardware: the label's bottom smeared into the MAC line above
    // it).
    // fontHeight() with NO argument -- fontHeight(int) takes a font
    // INDEX (2 means "built-in font #2", not "current font at size
    // 2"), which was silently querying an unrelated font's metrics
    // instead of the GLCD font actually being drawn here.
    t.setTextSize(2);
    int nameH = t.fontHeight();
    t.setTextSize(1);
    int detailH = t.fontHeight();
    int detailY, rowH;
    rowLayout(t, bodyTop, detailY, rowH);     // the hit test's numbers, exactly
    const int topPad = detailY - nameH;

    uiClampScroll(g_scroll, count, bodyH, rowH);
    int y = bodyTop;
    int idx = g_scroll;
    int max = (bodyH / rowH);

    for (int i = 0; i < max && idx < count; i++, idx++) {
        const Detection* d = uiLogRow(eng, idx);
        if (!d) break;
        // The card: Settings' row panel, so the two screens are one family.
        // It also puts a solid ground under the text, which the synthwave
        // sun used to shine through.
        Theme::drawListRowPanel(t, w, y, rowH);
        if (Theme::compact()) {
            const bool kept = d->restored;
            const uint16_t dim = Theme::W95_SHADOW;
            t.setTextSize(1);
            const int ty = y + detailY;
            t.setTextColor(kept ? dim : Theme::colorFor(d->type), Theme::BG);
            t.setCursor(6, ty);
            t.print(detectionTypeName(d->type));
            char ts[12];
            if (d->restored) Clock::formatEpochStamp(d->firstSeen, ts, sizeof(ts));
            else             Clock::formatStamp(d->firstSeen, ts, sizeof(ts));
            const int tsX = w - 10 - t.textWidth(ts);
            t.setTextColor(kept ? dim : Theme::VAPOR_PINK, Theme::BG);
            t.setCursor(tsX, ty);
            t.print(ts);
            char rs[8];
            snprintf(rs, sizeof rs, "%d", d->rssi);
            const int rsX = tsX - 6 - t.textWidth("-100");
            t.setTextColor(kept ? dim : Theme::CYAN, Theme::BG);
            t.setCursor(rsX + t.textWidth("-100") - t.textWidth(rs), ty);
            t.print(rs);
            // The name, or the regulars' name for it, or the address's tail.
            char nm[40], pv[40];
            const char* reg = Regulars::nameFor(d->mac);
            if (reg)             snprintf(nm, sizeof nm, "%s", reg);
            else if (d->name[0]) snprintf(nm, sizeof nm, "%s", Privacy::name(d->name, pv, sizeof pv));
            else { char mac[24]; Privacy::mac(mac, sizeof mac, d->mac); const size_t ml = strlen(mac);
                   snprintf(nm, sizeof nm, "%s", ml > 8 ? mac + ml - 8 : mac); }
            if (IgnoreList::contains(d->mac)) { char tmp[40]; snprintf(tmp, sizeof tmp, "(IGN) %s", nm); snprintf(nm, sizeof nm, "%s", tmp); }
            const int nx = 6 + t.textWidth("EVIL TWIN") + 6, nmax = rsX - 6 - nx;
            while (nm[0] && t.textWidth(nm) > nmax) nm[strlen(nm) - 1] = '\0';
            t.setTextColor(kept ? dim : (reg ? Theme::VAPOR_PINK : Theme::WHITE), Theme::BG);
            t.setCursor(nx, ty);
            t.print(nm);
            y += rowH;
            continue;
        }

        // A row the black box kept from an earlier boot is drawn faded: it
        // is history, and it sat beside live rows looking exactly like one.
        const bool kept = d->restored;
        const uint16_t dim = Theme::W95_SHADOW;

        // Type label (colored)
        t.setTextSize(2);
        t.setTextColor(kept ? dim : Theme::colorFor(d->type), Theme::BG);
        t.setCursor(8, y + topPad);
        t.print(detectionTypeName(d->type));
        const int labelEnd = 8 + t.textWidth(detectionTypeName(d->type));

        // MAC + RSSI line
        t.setTextSize(1);
        t.setTextColor(kept ? dim : Theme::WHITE, Theme::BG);
        char mac[24];
        Privacy::mac(mac, sizeof mac, d->mac);
        t.setCursor(8, y + detailY);
        t.print(mac);

        // RSSI — MAC above runs "XX:XX:XX:XX:XX:XX" (17 chars, 102px
        // at this font size) starting from x=8, so this column can't
        // start before ~114 without drawing on top of it.
        t.setTextColor(kept ? dim : Theme::CYAN, Theme::BG);
        t.setCursor(116, y + detailY);
        t.printf("%ddBm", d->rssi);
        // Closer or further since a couple of seconds ago, for a row that is
        // actually here: green up for nearer, red down for further, nothing
        // for the few dB of wobble a still device makes. The same arrow, and
        // the same deadband, as the raw scan's NEARBY list.
        if (!kept && d->active) {
            const int delta = (int)d->rssi - (int)d->prevRssi;
            const int ax = 116 + t.textWidth("-100dBm") + 3, ay = y + detailY;
            if (delta >= 4)       t.fillTriangle(ax, ay + 6, ax + 6, ay + 6, ax + 3, ay, Theme::GREEN);
            else if (delta <= -4) t.fillTriangle(ax, ay, ax + 6, ay, ax + 3, ay + 6, Theme::RED);
        }

        // Hits
        t.setTextColor(kept ? dim : Theme::VAPOR_PURPLE, Theme::BG);
        t.setCursor(168, y + detailY);
        char hitsTxt[8];
        snprintf(hitsTxt, sizeof hitsTxt, "x%u", d->hits);
        t.print(hitsTxt);
        const int hitsEnd = 168 + t.textWidth(hitsTxt);

        // An ignored device says so on its row (issue #18): a grey tag at the
        // end of the detail line, IGNORED where it fits and IGN where the
        // screen is narrow. Grey, because an ignored device is one that has
        // been told to be quiet.
        if (IgnoreList::contains(d->mac)) {
            const char* tag = "IGNORED";
            if (w - 14 - t.textWidth(tag) - 4 < hitsEnd + 6) tag = "IGN";
            const int tw2 = t.textWidth(tag) + 4;
            const int tx  = w - 14 - tw2;
            if (tx >= hitsEnd + 6) {
                t.fillRoundRect(tx, y + detailY - 1, tw2, detailH + 1, 2, Theme::W95_SHADOW);
                t.setTextColor(Theme::WHITE, Theme::W95_SHADOW);
                t.setCursor(tx + 2, y + detailY);
                t.print(tag);
            }
        }

        // Timestamp (right edge)
        // Wall-clock once somebody has set it, minutes-since-boot until
        // then -- which is what this column always was. A row reading
        // "71581:47" was 71581 minutes of uptime, technically an ordering
        // and nothing more.
        char ts[12];
        // A row the black box kept from an earlier boot carries its
        // wall-clock second instead of a millis() stamp.
        if (d->restored) Clock::formatEpochStamp(d->firstSeen, ts, sizeof(ts));
        else             Clock::formatStamp(d->firstSeen, ts, sizeof(ts));
        int tw = t.textWidth(ts);
        t.setTextColor(kept ? dim : Theme::VAPOR_PINK, Theme::BG);
        t.setCursor(w - tw - 14, y + topPad);
        t.print(ts);

        // The device's own name, in the gap between the type label and the
        // timestamp. Detection has carried this field all along and nothing
        // ever drew it, so a Flipper called "Ozzyx", a Pwnagotchi's name,
        // an iBeacon's deployment and a drone's serial were all being
        // captured and then thrown away at the last step.
        //
        // Truncated to whatever actually fits rather than clipped by the
        // driver: the timestamp is drawn already and text written past it
        // would land on top of it. Two characters of margin at each end
        // keep the columns visibly separate at a glance.
        // A regular's neighbour-name comes first, in pink, then whatever the
        // device calls itself. "Gary" on its own when it calls itself nothing.
        const char* reg = Regulars::nameFor(d->mac);
        if (d->name[0] || reg) {
            const int nameX   = labelEnd + 8;
            const int nameMax = (w - tw - 14) - 6 - nameX;
            if (nameMax > 0) {
                char nm[sizeof(d->name) + 16];
                char pv[40];
                const char* own = Privacy::name(d->name, pv, sizeof pv);
                if (reg && d->name[0]) snprintf(nm, sizeof nm, "%s: %s", reg, own);
                else if (reg)          snprintf(nm, sizeof nm, "%s", reg);
                else                   snprintf(nm, sizeof nm, "%s", own);
                while (nm[0] && t.textWidth(nm) > nameMax) nm[strlen(nm) - 1] = '\0';
                if (nm[0]) {
                    // Centred against the size-2 type label beside it.
                    // Sharing its top edge would leave the small text
                    // hanging off the cap height of the big text.
                    t.setTextColor(kept ? dim : (reg ? Theme::VAPOR_PINK : Theme::WHITE), Theme::BG);
                    t.setCursor(nameX, y + topPad + (nameH - detailH) / 2);
                    t.print(nm);
                }
            }
        }

        y += rowH;
    }

    // Scroll position indicator -- reserved 10px on the right edge
    // (the timestamp column above already stops short of the true
    // edge to make room for it) so there's finally a visual hint this
    // list can hold more than what's on screen.
    Theme::drawScrollbar(t, w - 4, bodyTop, bodyH, count, max, g_scroll);

    // Bottom soft buttons
    Theme::drawButtonBar(t, ButtonId::LOG, Theme::ButtonBarMode::LOG);

    if (infoPending)        Theme::drawInfoPanel(t, w, h, now, infoTypeName, infoText);
    else if (confirmPending) drawConfirmPanel(t, w, h, confirmLabel, confirmWatched, confirmHunted, confirmIgnored);
}
