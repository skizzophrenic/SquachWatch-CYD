// SquachWatch-CYD — settings screen implementation
#include "ui_settings.h"
#include "ota_core.h"
#include "ota_wifi.h"
#include "theme.h"
#include "settings.h"
#include "security.h"
#include "ignore_list.h"
#include "squachy.h"
#include "bingo.h"
#include "dex.h"
#include "ui_dex.h"
#include <Arduino.h>

// Was 16, which put the first row's top edge 4px ABOVE the bottom of the
// title bar's 20px icon box (Theme::ICON_BOX_H) -- so the strip a thumb
// reaches for to open/close this screen overlapped the first row's tap
// target, and the miss landed on whatever sat at the top of the list.
// 32 leaves 12px of clear space under the icons. Shared by computeGeom()
// for both drawing and hit-testing, so the rows and their tap targets
// move together.
static const int TOP_MARGIN = 32;

// One scroll position per page, not one shared: see SettingsPage in the header.
static SettingsPage s_page = SettingsPage::MAIN;
static int g_scrollFor[5] = { 0, 0, 0, 0, 0 };
#define g_scroll (g_scrollFor[(uint8_t)s_page])

// BACK is pinned along the bottom now. Height of that strip, reserved out of
// the body so the last row cannot hide underneath it.
// Its own copy, kept in step with Theme's -- the list stops above this
// strip, so a disagreement puts the last row under it, untappable.
#define PINNED_BACK_H (Theme::pinnedBackH(screenW))

// Which groups are folded shut. Session-only on purpose: a fold is a "get this
// out of my way for a minute", not a preference worth surviving a reboot.
static bool s_folded[7] = { false, false, false, false, false, false, false };

// Whether a watch/hunt target exists. Set every tick from the engine, read by
// buildDisplayList() -- which has no engine of its own, and is called by the
// hit test as well as the draw. Same pattern ui_clear.cpp uses for its crowd.
static bool s_hasWatch = false, s_hasHunt = false;
static char s_watchLabel[24] = "", s_huntLabel[24] = "";
// The DEX row's count, for the same reason.
static uint8_t s_dexCaught = 0;

// Fixed display order, grouped so a colored section header can sit
// above each cluster (see groupFor()/RowGroupId below) -- CALIBRATE/
// DIAGNOSTICS used to sit between BEHAVIOR and SQUACHY rows, which
// only worked because nothing before this cared about contiguous
// groups. Squachy-only rows (customizing a character "boring mode"
// has turned off) are filtered out by visibleRows() below rather than
// removed here, so their SettingsRow values stay stable regardless of
// which mode is active.
#if defined(TWATCH_S3)
void twatchBatteryLine(char* out, size_t n);   // main.cpp, where the power chip lives
bool twatchRadioResetArmed();                  // main.cpp: the first tap of two
int  twatchChipC();                            // main.cpp: the chip's temperature, cached
void twatchXtalLine(char* out, size_t n);      // main.cpp: CLOCK CHECK's state or result
#endif

static const SettingsRow ALL_ROWS[] = {
#if defined(TWATCH_S3)
    // The watch's own page, first: what only a watch has to think about grew
    // to ten rows, so the main list carries one row that opens it.
    SettingsRow::WATCH_SETTINGS,
#endif
    SettingsRow::BORING_MODE, SettingsRow::CONFIDENCE, SettingsRow::AUTO_QUIET,
    SettingsRow::DETECTION_FILTER,
    SettingsRow::IGNORED_DEVICES,
    // APPEARANCE opens the display page -- see APPEARANCE_ROWS. It sat at the
    // very top of this list, which put it under the first thumb that opened
    // the screen and got pressed by accident. Down here with SQUACHMESH it is
    // beside the other row that opens a page rather than changing a value.
    SettingsRow::APPEARANCE,
#if SQUACH_MESH
    // One row, not two: NAME moved inside the SquachMesh menu, which is a
    // row back on a list carrying twenty-five with three colliding in
    // portrait.
    SettingsRow::SQUACHMESH,
#endif
    // SIZE, OUTFIT, PET and SHADES COLOR all live on the APPEARANCE page now
    // -- see APPEARANCE_ROWS. Everything about how he LOOKS is on one page;
    // what stays here is what he DOES.
    SettingsRow::REPLAY_INTRO, SettingsRow::SHOW_OFF, SettingsRow::VIEW_DIARY,
    SettingsRow::BINGO, SettingsRow::DEX, SettingsRow::DESK_MODE,
    SettingsRow::POWER_SAVER,
    SettingsRow::SECURITY,
    // CALIBRATE, CHECK COLORS, DIAGNOSTICS and RESET STATS moved behind the
    // SYSTEM row -- see SYSTEM_ROWS. They are the four you touch once a year,
    // and they were sitting below everything you actually adjust.
    SettingsRow::SYSTEM,
    // No BACK here: it is pinned to the bottom edge instead, so it is reachable
    // from anywhere in the list rather than only from the end of it.
};
static const uint8_t ALL_ROWS_N = sizeof(ALL_ROWS) / sizeof(ALL_ROWS[0]);

// The APPEARANCE page: everything about how HE looks, then everything about
// how the SCREEN looks. The Legend top hat only gets a row once he has a hat
// to take off.
static const SettingsRow APPEARANCE_ROWS[] = {
    // How HE looks comes first -- these are the rows people open this page to
    // change, and they were a scroll away on the main list. TOP HAT only
    // appears once it has been earned; see buildDisplayList().
    SettingsRow::SQUACHY_SIZE, SettingsRow::OUTFIT, SettingsRow::PET,
    SettingsRow::SHADES_COLOR, SettingsRow::BANTER, SettingsRow::TOP_HAT,
    // Then how the SCREEN looks.
    SettingsRow::THEME, SettingsRow::BACKGROUND, SettingsRow::BACKGROUND_LOCK, SettingsRow::BRIGHTNESS,
    SettingsRow::INVERT, SettingsRow::RGB_SWAP, SettingsRow::ROTATION_LOCK,
    // And the one light that is not on the screen at all.
    SettingsRow::STATUS_LIGHT,
};

#if defined(TWATCH_S3)
// The WATCH SETTINGS page: the battery first (a reading), then the knobs that
// decide how long it lasts, then the buzz, then the tools.
static const SettingsRow WATCH_ROWS[] = {
    SettingsRow::WATCH_BATTERY, SettingsRow::WATCH_RADIO, SettingsRow::WATCH_LISTEN,
    SettingsRow::WATCH_IDLE_CPU, SettingsRow::WATCH_BUZZ,
    SettingsRow::WATCH_RADIO_RESET, SettingsRow::WATCH_STEADY, SettingsRow::WATCH_TEMP,
    SettingsRow::WATCH_XTAL,
};
static const uint8_t WATCH_ROWS_N = sizeof(WATCH_ROWS) / sizeof(WATCH_ROWS[0]);
#endif

// The SYSTEM page: the rarely-needed machinery, off the main list.
static const SettingsRow SYSTEM_ROWS[] = {
    SettingsRow::CALIBRATE, SettingsRow::CHECK_COLORS,
    SettingsRow::DIAGNOSTICS, SettingsRow::UPDATE_FIRMWARE, SettingsRow::UPDATE_CHECK, SettingsRow::WIFI_NETWORKS,
    SettingsRow::RESET_STATS,
};
static const uint8_t SYSTEM_ROWS_N = sizeof(SYSTEM_ROWS) / sizeof(SYSTEM_ROWS[0]);

// The DESK MODE page: everything about the desk in one place, so it can grow.
// The way in comes first; then how it looks; then who is on it.
static const SettingsRow DESK_ROWS[] = {
    SettingsRow::DESK_OPEN, SettingsRow::DESK_BACKGROUND,
    SettingsRow::CLOCK_FONT, SettingsRow::CLOCK_SIZE, SettingsRow::CLOCK_BACKDROP,
    SettingsRow::TIME_ZONE,
#if SQUACH_MESH
    SettingsRow::DESK_SQUAD, SettingsRow::DESK_CROWD, SettingsRow::DESK_VISIT,
#endif
};
static const uint8_t DESK_ROWS_N = sizeof(DESK_ROWS) / sizeof(DESK_ROWS[0]);
static const uint8_t APPEARANCE_ROWS_N = sizeof(APPEARANCE_ROWS) / sizeof(APPEARANCE_ROWS[0]);
// The display buffers below are sized off the longest of the lists (the
// DESK MODE page's is checked against it just below).
// It used to be the main one, until that list lost its NICKNAME row and
// the APPEARANCE page outgrew it.
static const uint8_t LIST_MAX_N = APPEARANCE_ROWS_N > ALL_ROWS_N ? APPEARANCE_ROWS_N : ALL_ROWS_N;
static_assert(SYSTEM_ROWS_N <= LIST_MAX_N, "the display list is sized off LIST_MAX_N");
static_assert(DESK_ROWS_N <= LIST_MAX_N, "the display list is sized off LIST_MAX_N");
#if defined(TWATCH_S3)
static_assert(WATCH_ROWS_N <= LIST_MAX_N, "the display list is sized off LIST_MAX_N");
#endif
// Kept as a thin shim over s_page so nothing that reads it has to change.
#define s_appearance (s_page == SettingsPage::APPEARANCE)

// Rows that boring mode switches off. They are now DRAWN, greyed, with the
// reason in place of the value -- a hidden row and a row that was never there
// look identical, so somebody looking for OUTFIT after turning boring mode on
// had no way to learn where it went.
//
// This is deliberately NOT how unearned things behave: PET and TOP HAT stay
// hidden entirely (see buildDisplayList), because a greyed-out row saying
// "not found yet" hands over the existence of a secret.
static bool isSquachyOnlyRow(SettingsRow r) {
    return r == SettingsRow::REPLAY_INTRO || r == SettingsRow::SHOW_OFF ||
           r == SettingsRow::SQUACHY_NAME ||
           // NOT Appearance: it is the only way to theme, background,
           // brightness, invert, colour order and rotation lock, none of which
           // are about Squachy. Listed here for one hour and boring mode lost
           // its brightness control -- see groupFor(), which moves the row to
           // SYSTEM in that mode instead of hiding it.
           r == SettingsRow::SQUACHMESH ||
           r == SettingsRow::SHADES_COLOR || r == SettingsRow::SQUACHY_SIZE ||
           r == SettingsRow::OUTFIT ||
           r == SettingsRow::PET || r == SettingsRow::TOP_HAT;
}

enum class RowGroupId : uint8_t { APPEARANCE, BEHAVIOR, SQUACHY, SYSTEM, DESK, SQUAD, WATCH };

static RowGroupId groupFor(SettingsRow r) {
    // Appearance sits with the Squachy rows because that is where it was asked
    // for and where a thumb opening this screen will not hit it by accident.
    // Boring mode filters every OTHER row in that group, though, and a lone
    // "APPEARANCE" under a SQUACHY heading in a mode with no Squachy reads as
    // a leftover. It goes to SYSTEM there -- still reachable, which is the
    // part that matters, since the display rows live behind it.
    if (r == SettingsRow::APPEARANCE && Settings::boringMode()) return RowGroupId::SYSTEM;
    switch (r) {
        case SettingsRow::WATCH_BATTERY:
        case SettingsRow::WATCH_RADIO:
        case SettingsRow::WATCH_LISTEN:
        case SettingsRow::WATCH_IDLE_CPU:
        case SettingsRow::WATCH_BUZZ:
        case SettingsRow::WATCH_SETTINGS:
        case SettingsRow::WATCH_RADIO_RESET:
        case SettingsRow::WATCH_STEADY:
        case SettingsRow::WATCH_TEMP:
        case SettingsRow::WATCH_XTAL:
            return RowGroupId::WATCH;
        // TIME ZONE sat on the SYSTEM page too, the same setting twice. Only
        // the clock reads it, so it lives with the clock.
        case SettingsRow::TIME_ZONE:
        case SettingsRow::DESK_OPEN:
        case SettingsRow::DESK_BACKGROUND:
        case SettingsRow::CLOCK_FONT:
        case SettingsRow::CLOCK_SIZE:
        case SettingsRow::CLOCK_BACKDROP:
            return RowGroupId::DESK;
        case SettingsRow::DESK_SQUAD:
        case SettingsRow::DESK_CROWD:
        case SettingsRow::DESK_VISIT:
            return RowGroupId::SQUAD;
        case SettingsRow::THEME:
        case SettingsRow::BACKGROUND:
        case SettingsRow::BACKGROUND_LOCK:
        case SettingsRow::BRIGHTNESS:
        case SettingsRow::INVERT:
        case SettingsRow::RGB_SWAP:
        case SettingsRow::ROTATION_LOCK:
        case SettingsRow::STATUS_LIGHT:
        case SettingsRow::SHADES_COLOR:
        case SettingsRow::TOP_HAT:
        // SIZE, OUTFIT and PET moved onto the APPEARANCE page with the rest of
        // how he looks. They answer APPEARANCE rather than SQUACHY so the page
        // draws under one header instead of splitting in two.
        case SettingsRow::SQUACHY_SIZE:
        case SettingsRow::OUTFIT:
        case SettingsRow::PET:
        case SettingsRow::BANTER:
            return RowGroupId::APPEARANCE;
        case SettingsRow::BORING_MODE:
        case SettingsRow::CONFIDENCE:
        case SettingsRow::AUTO_QUIET:
        case SettingsRow::DETECTION_FILTER:
        case SettingsRow::IGNORED_DEVICES:
            return RowGroupId::BEHAVIOR;
        case SettingsRow::SQUACHY_NAME:
        case SettingsRow::APPEARANCE:
        case SettingsRow::SQUACHMESH:
        case SettingsRow::REPLAY_INTRO:
        case SettingsRow::VIEW_DIARY:
        case SettingsRow::BINGO:
        case SettingsRow::DEX:
        case SettingsRow::DESK_MODE:
        case SettingsRow::SHOW_OFF:
            return RowGroupId::SQUACHY;
        default:  // CALIBRATE, CHECK_COLORS, DIAGNOSTICS, RESET_STATS, BACK
            return RowGroupId::SYSTEM;
    }
}

static const char* groupName(RowGroupId g) {
    switch (g) {
        case RowGroupId::APPEARANCE: return "APPEARANCE";
        case RowGroupId::BEHAVIOR:   return "BEHAVIOR";
        case RowGroupId::SQUACHY:    return "SQUACHY";
        case RowGroupId::DESK:       return "DESK";
        case RowGroupId::SQUAD:      return "SQUAD";
        case RowGroupId::WATCH:      return "WATCH";
        default:                     return "SYSTEM";
    }
}

// Picked from colors already in the palette -- nothing new to invent,
// and it means a custom theme preset re-tints these along with
// everything else instead of clashing with it.
static uint16_t groupColor(RowGroupId g) {
    switch (g) {
        case RowGroupId::APPEARANCE: return Theme::CYAN;
        case RowGroupId::BEHAVIOR:   return Theme::AMBER;
        case RowGroupId::SQUACHY:    return Theme::VAPOR_PINK;
        case RowGroupId::DESK:       return Theme::CYAN;
        case RowGroupId::SQUAD:      return Theme::GREEN;
        case RowGroupId::WATCH:      return Theme::AMBER;
        default:                     return Theme::VAPOR_PURPLE;
    }
}

// One entry per thing actually drawn -- a header or a row -- built
// fresh each tick from whatever visibleRows() currently allows (mode-
// dependent) so a header only ever appears above a group that
// actually has visible rows in it. Shared by drawing and hit-testing
// so a tap always lands on whatever's actually on screen.
struct DisplayItem {
    bool       isHeader;
    RowGroupId group;
    SettingsRow row;   // only meaningful when !isHeader
};

static uint8_t buildDisplayList(DisplayItem* out) {
    SettingsRow rows[LIST_MAX_N + 2];   // + the two tracking rows
    uint8_t n = 0;
    const SettingsRow* src = ALL_ROWS;
    uint8_t            srcN = ALL_ROWS_N;
    if (s_page == SettingsPage::APPEARANCE) { src = APPEARANCE_ROWS; srcN = APPEARANCE_ROWS_N; }
    else if (s_page == SettingsPage::SYSTEM) { src = SYSTEM_ROWS;    srcN = SYSTEM_ROWS_N; }
    else if (s_page == SettingsPage::DESK)   { src = DESK_ROWS;      srcN = DESK_ROWS_N; }
#if defined(TWATCH_S3)
    else if (s_page == SettingsPage::WATCH)  { src = WATCH_ROWS;     srcN = WATCH_ROWS_N; }
#endif
    // The tracking rows come first on the main page, and only when a target is
    // actually set -- the whole point is that a watch stops being invisible.
    if (s_page == SettingsPage::MAIN) {
        if (s_hasWatch) rows[n++] = SettingsRow::WATCH_TARGET;
        if (s_hasHunt)  rows[n++] = SettingsRow::HUNT_TARGET;
    }
    for (uint8_t i = 0; i < srcN; i++) {
        const SettingsRow r = src[i];
        // Boring mode greys these instead of hiding them -- see
        // isSquachyOnlyRow(). They stay in the list; drawing handles the rest.
        // PET and TOP HAT are hidden by not being EARNED rather than by a
        // mode. Showing a permanently-off row for something you have never
        // seen would give the secret away -- and a switch for a hat he is
        // not wearing yet would be a switch that does nothing.
        if (r == SettingsRow::PET && !Squachy::petUnlocked()) continue;
        if (r == SettingsRow::TOP_HAT && !Squachy::hasTopHat()) continue;
        // Not a secret, just impossible: a board without a second app slot or
        // a Bluetooth server has nothing to update into.
        if ((r == SettingsRow::UPDATE_FIRMWARE || r == SettingsRow::UPDATE_CHECK) && !OtaCore::available()) continue;
        rows[n++] = r;
    }

    uint8_t count = 0;
    bool haveLastGroup = false;
    RowGroupId lastGroup = RowGroupId::APPEARANCE;
    for (uint8_t i = 0; i < n; i++) {
        RowGroupId g = groupFor(rows[i]);
        // No SYSTEM header over the APPEARANCE page's own BACK.
        //
        // The APPEARANCE row used to be exempt too, back when it sat at the
        // top of the list and read as a heading in its own right. It lives in
        // the SQUACHY cluster now, so that exemption would suppress the
        // SQUACHY header whenever APPEARANCE happened to come first in it.
        const bool headerless = s_appearance && rows[i] == SettingsRow::BACK;
        if (!headerless && (!haveLastGroup || g != lastGroup)) {
            out[count].isHeader = true;
            out[count].group = g;
            count++;
            lastGroup = g;
            haveLastGroup = true;
        }
        // Folded: the heading is still drawn (that is what you tap to unfold),
        // its rows are not.
        if (s_folded[(uint8_t)g]) continue;
        out[count].isHeader = false;
        out[count].group = g;
        out[count].row = rows[i];
        count++;
    }
    return count;
}

// Fixed heights at the bigger text size, not "shrink to fit everything
// on one screen" the way this used to work -- doubling the text size
// (the only step available with the built-in GLCD font; no fractional
// sizes) meant not everything could fit anymore regardless, so this
// scrolls now instead, same pattern LOG/raw-scan already use. Headers
// use the smaller size-1 text, both to distinguish them from real rows
// and to keep them from eating too much vertical space. Needs a live
// TFT_eSPI& since heights depend on actual font metrics -- shared by
// drawing and hit-testing so they can't drift apart.
// BACKGROUND and OUTFIT are the only two rows whose value can collide with
// its own label, and they are worth measuring rather than guessing about.
// The built-in font advances 12px a character at size 2, the label starts at
// x=8, and the value is right-aligned with 18px held back for the scrollbar.
// "WIREFRAME TUNNEL" is sixteen characters, so on a 320-wide screen it ran
// 18px INTO "BACKGROUND" -- and in portrait, at 240 wide, it ran 98px in and
// even "SYNTHWAVE" collided. drawRow() measures neither and clips nothing,
// so they simply overprinted each other.
//
// Giving the value its own line makes that impossible for any name, now or
// later, which is why this beats shrinking the text or truncating it.
static bool isTwoLineRow(SettingsRow r) {
    return r == SettingsRow::BACKGROUND || r == SettingsRow::DESK_BACKGROUND ||
           r == SettingsRow::OUTFIT;
}

static int itemHeight(const DisplayItem& it, int rowH, int headerH, int tallH) {
    if (it.isHeader) return headerH;
    return isTwoLineRow(it.row) ? tallH : rowH;
}

// How far down the list is allowed to go. Scrolling was clamped at the top
// and nowhere else, so the list ran on into empty space: past the end you
// were left looking at background with the rows you wanted somewhere above
// the fold, and the pinned BACK strip sitting under a thumb that had nothing
// left to aim at.
//
// The stop is two rows of slack under the last item rather than a hard
// bottom edge. A list that halts with its last row jammed against the strip
// looks stuck; two rows of air reads as "that is the end" and leaves the
// bottom rows somewhere comfortable to press.
//
// Measured from the END of the list backwards, because the items are not all
// the same height -- BACKGROUND and OUTFIT take two lines, group headers take
// less than a row -- so the answer is not arithmetic on a row count.
static int maxScroll(const DisplayItem* items, uint8_t n,
                     int top, int bodyBottom, int rowH, int headerH, int tallH) {
    int want = (bodyBottom - top) - 2 * rowH;
    if (want < 1) want = 1;            // a screen too short for the slack
    int s = n, used = 0;
    while (s > 0 && used < want) {
        s--;
        used += itemHeight(items[s], rowH, headerH, tallH);
    }
    return s;
}

static void computeGeom(TFT_eSPI& t, int screenH, int& top, int& bodyBottom,
                        int& rowH, int& headerH, int& tallH) {
    top = TOP_MARGIN;
    // The pinned BACK strip owns the bottom of the screen, so the list stops
    // above it -- otherwise the last row draws underneath and cannot be tapped.
    bodyBottom = screenH - Theme::pinnedBackH(t.width()) - 2;
    t.setTextSize(Theme::uiMenuTextSize(t));
    // Two pixels taller than the text strictly needs on each side: a 24 px
    // row was a near miss for a thumb, 26 is not, and seven of them still
    // fit above the BACK strip in landscape. A wide panel draws the row a
    // size bigger, and this carries it: taller letters, taller row, bigger
    // thumb target, all off the one number.
    rowH = t.fontHeight() + 10;
    const int big = t.fontHeight();
    t.setTextSize(Theme::uiTextSize(t, 1));
    headerH = t.fontHeight() + 6;
    t.setTextSize(1);
    tallH   = t.fontHeight() + big + 7;
}

// Row height is decided in exactly one place, itemHeight() below, shared by
// drawing and hit testing -- if those two ever disagree, a tap lands on the
// row above the one you pressed.

void uiSettingsInit(TFT_eSPI& t) {
    // Arriving at Settings is arriving at its main page, at the top, with
    // nothing folded. Scroll memory is for moving BETWEEN pages inside one
    // visit -- carrying it across a fresh entry would drop you mid-list with
    // no idea why.
    s_page = SettingsPage::MAIN;
    for (uint8_t i = 0; i < 5; i++) g_scrollFor[i] = 0;
    for (uint8_t i = 0; i < 7; i++) s_folded[i] = false;
    // Any pending question dies with the screen. Coming back to Settings and
    // finding a confirm panel still up from last time would be answering
    // something you no longer remember asking.
    uiSettingsSetConfirm(SettingsRow::NONE);
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

// ---- confirmation panel ---------------------------------------------------
// Which row is waiting to be answered, or NONE. Cleared by main.cpp on either
// button and by uiSettingsInit(), so leaving the screen and coming back never
// lands you on a stale question.
static SettingsRow s_confirmRow = SettingsRow::NONE;

// The text for each row that has a panel. Anything not listed here has no
// panel and uiSettingsSetConfirm() refuses it, which is what stops a future
// row from silently getting a blank dialog.
struct ConfirmText {
    SettingsRow row;
    const char* title;    // Bangers, so uppercase only
    const char* line1;
    const char* line2;
    const char* verb;     // the button that goes through with it
};
static const ConfirmText CONFIRMS[] = {
    { SettingsRow::CALIBRATE, "RECALIBRATE?",
      "Replaces the touch calibration",
      "you are using right now.", "CALIBRATE" },
    // The second line is the part worth stopping for. Outfits earned by
    // detection count are gated on the lifetime total, so zeroing it takes
    // them away again; the ones earned by finding something are not, and
    // stay put.
    { SettingsRow::RESET_STATS, "RESET STATS?",
      "Clears every detection total.",
      "Outfits earned by count re-lock.", "RESET" },
    // The second line is the whole reason this row has a panel: boring mode
    // hides every Squachy row, so the first thing it does is take away the
    // OUTFIT row, and the way back is not obvious once it has.
    { SettingsRow::BORING_MODE, "BORING MODE?",
      "Hides Squachy and his rows,",
      "OUTFIT included. Same row undoes.", "TURN ON" },
    { SettingsRow::REPLAY_INTRO, "REPLAY INTRO?",
      "Runs the first-boot walkthrough",
      "again, from the top.", "REPLAY" },
};
static const uint8_t CONFIRMS_N = sizeof(CONFIRMS) / sizeof(CONFIRMS[0]);

static const ConfirmText* confirmTextFor(SettingsRow r) {
    for (uint8_t i = 0; i < CONFIRMS_N; i++) if (CONFIRMS[i].row == r) return &CONFIRMS[i];
    return nullptr;
}

// Geometry shared by the drawing and the hit test, same reason every other
// panel in here shares one: two copies drift.
//
// The going-through-with-it button sits on its own row and CANCEL takes the
// full width underneath it. That is the log screen's rule and it applies more
// strongly here: CANCEL is the one you reach for by reflex, and it must not be
// possible to hit the other one instead.
static void settingsConfirmRects(int screenW, int screenH,
                                 int& px, int& py, int& pw, int& ph,
                                 int& okX, int& okY, int& okW, int& okH,
                                 int& cnX, int& cnY, int& cnW, int& cnH) {
    pw = screenW - 40;
    if (pw > 240) pw = 240;
    ph = 128;
    px = (screenW - pw) / 2;
    py = (screenH - ph) / 2;
    const int margin = 10, gap = 8, btnH = 24;
    cnH = okH = btnH;
    cnY = py + ph - btnH - margin;
    cnX = px + margin;
    cnW = pw - 2 * margin;
    okY = cnY - gap - btnH;
    okX = px + margin;
    okW = pw - 2 * margin;
}

void uiSettingsSetConfirm(SettingsRow r) {
    s_confirmRow = (r == SettingsRow::NONE || confirmTextFor(r)) ? r : SettingsRow::NONE;
}

SettingsRow uiSettingsConfirmRow() { return s_confirmRow; }

SettingsConfirmTap uiSettingsHitConfirm(int x, int y, int screenW, int screenH) {
    if (s_confirmRow == SettingsRow::NONE) return SettingsConfirmTap::NONE;
    int px, py, pw, ph, okX, okY, okW, okH, cnX, cnY, cnW, cnH;
    settingsConfirmRects(screenW, screenH, px, py, pw, ph, okX, okY, okW, okH, cnX, cnY, cnW, cnH);
    if (x >= okX && x <= okX + okW && y >= okY && y <= okY + okH) return SettingsConfirmTap::CONFIRM;
    if (x >= cnX && x <= cnX + cnW && y >= cnY && y <= cnY + cnH) return SettingsConfirmTap::CANCEL;
    // A tap anywhere else on the panel is swallowed rather than falling
    // through to the row underneath it, which would be the row you were
    // trying to think about.
    if (x >= px && x <= px + pw && y >= py && y <= py + ph) return SettingsConfirmTap::NONE;
    return SettingsConfirmTap::CANCEL;
}

static void drawSettingsConfirm(TFT_eSPI& t, int w, int h) {
    const ConfirmText* ct = confirmTextFor(s_confirmRow);
    if (!ct) return;
    int px, py, pw, ph, okX, okY, okW, okH, cnX, cnY, cnW, cnH;
    settingsConfirmRects(w, h, px, py, pw, ph, okX, okY, okW, okH, cnX, cnY, cnW, cnH);

    t.fillRoundRect(px, py, pw, ph, 6, Theme::BG);
    t.drawRoundRect(px, py, pw, ph, 6, Theme::RED);

    int tw = Theme::bangersTextWidth(ct->title, Theme::BangersSize::MD);
    if (tw > pw - 16) tw = pw - 16;
    Theme::drawBangersText(t, px + (pw - tw) / 2, py + 8, ct->title, Theme::RED, Theme::BangersSize::MD);

    t.setTextWrap(false);
    t.setTextSize(1);
    t.setTextColor(Theme::WHITE, Theme::BG);
    const char* lines[2] = { ct->line1, ct->line2 };
    for (uint8_t i = 0; i < 2; i++) {
        int lw = t.textWidth(lines[i]);
        t.setCursor(px + (pw - lw) / 2, py + 34 + i * 11);
        t.print(lines[i]);
    }

    Theme::drawButton(t, okX, okY, okW, okH, ct->verb, false);
    Theme::drawButton(t, cnX, cnY, cnW, cnH, "CANCEL", false);
}

void uiSettingsScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

void uiSettingsOpenPage(SettingsPage p) {
    s_page = p;
    // Deliberately NOT resetting g_scroll: each page keeps its own position,
    // so coming back to a page puts you where you left it. That is the whole
    // point of g_scrollFor[] -- see the header.
    uiSettingsSetConfirm(SettingsRow::NONE);
}

SettingsPage uiSettingsCurrentPage() { return s_page; }

void uiSettingsOpenAppearance(bool open) {
    uiSettingsOpenPage(open ? SettingsPage::APPEARANCE : SettingsPage::MAIN);
}

// True when a mode has switched this row off. main.cpp asks so a tap on a
// greyed row says why instead of doing nothing.
bool uiSettingsRowIsOff(SettingsRow r) {
    return Settings::boringMode() && isSquachyOnlyRow(r);
}

// ---- the pinned BACK strip ---------------------------------------------------
static void pinnedBackRect(int screenW, int screenH, int& x, int& y, int& w, int& h) {
    x = 0;
    w = screenW;
    h = Theme::pinnedBackH(screenW);
    y = screenH - h;
}

static void drawPinnedBack(TFT_eSPI& t, int screenW, int screenH) {
    int x, y, w, h;
    pinnedBackRect(screenW, screenH, x, y, w, h);
    t.fillRect(x, y, w, h, Theme::BG);
    t.drawFastHLine(x, y, w, Theme::PURPLE);
    t.setTextFont(1);
    t.setTextSize(Theme::uiMenuTextSize(t));
    t.setTextColor(Theme::CYAN, Theme::BG);
    // The DESK MODE page splits the strip: OK on the left goes straight out
    // to wherever Settings was opened from, the desk or the main screen; UP
    // on the right is the usual one level up.
    if (s_page == SettingsPage::DESK) {
        const int half = w / 2;
        t.drawFastVLine(x + half, y + 4, h - 8, Theme::PURPLE);
        t.setCursor(x + (half - t.textWidth("[ OK ]")) / 2, y + (h - t.fontHeight()) / 2);
        t.print("[ OK ]");
        t.setCursor(x + half + (half - t.textWidth("[ UP ]")) / 2, y + (h - t.fontHeight()) / 2);
        t.print("[ UP ]");
        return;
    }
    const char* lbl = (s_page == SettingsPage::MAIN) ? "[ BACK ]" : "[ UP ]";
    t.setCursor(x + (w - t.textWidth(lbl)) / 2, y + (h - t.fontHeight()) / 2);
    t.print(lbl);
}

bool uiSettingsTapPinnedOk(int x, int y, int screenW, int screenH) {
    if (s_page != SettingsPage::DESK) return false;
    int bx, by, bw, bh;
    pinnedBackRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x < bx + bw / 2 && y >= by && y < by + bh;
}

bool uiSettingsTapPinnedBack(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)t;
    int bx, by, bw, bh;
    pinnedBackRect(screenW, screenH, bx, by, bw, bh);
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

static void drawHeader(TFT_eSPI& t, int w, int y, int hgt, RowGroupId g) {
    // One short word, alone on its row: the easiest thing on the screen to
    // step up, and the thing that vanished most on a 480-wide panel.
    t.setTextSize(Theme::uiTextSize(t, 1));
    t.setTextColor(groupColor(g), Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(groupName(g));
}

// `compact` drops the row text from size 2 to size 1. Used in portrait,
// where 240px of width isn't enough for a long label and its value at
// size 2's 12px-per-glyph: "BACKGROUND" ran straight into "MATRIX
// RAIN", and "LOCK BACKGROUND" into its "OFF", with the label drawn
// left-aligned and the value right-aligned into the same pixels. Size 1
// halves glyph width and gives every current row room to spare.
//
// Row *height* deliberately doesn't change with it -- rowH stays keyed
// to size 2 metrics in computeGeom() so tap targets keep their full
// height, and so hit-testing (which shares computeGeom) can't drift
// away from what was drawn.
// Label small on top, value big underneath. That way round on purpose: you
// tap these rows to change the value, so the value is the thing being read,
// and the label is the part you already know.
//
// The label keeps its group colour rather than going grey. This screen draws
// a dimmed animated background behind itself, and a low-contrast label on a
// moving backdrop is the one thing it cannot afford.
//
// `cycles` decides the arrows, and it is not decoration. BACKGROUND cycles
// in place when tapped, so it gets a pair. OUTFIT opens the outfit chooser
// instead, so it gets a single right chevron -- the same grammar the rest of
// the UI uses for "this opens something". Showing a left arrow on a row that
// cannot go left would be a lie told in pixels.
// A solid card behind every option, and a hairline around it.
//
// Rows used to rely on each print() giving its own glyph cells an opaque
// backing, so the live background showed through everywhere between the
// letters. That reads fine over the digital rain and badly over anything
// with big bright shapes -- the synthwave sun and the fire both come
// straight through the gaps in a word.
//
// Filling the row first costs nothing (the dimmed background behind this
// screen already repaints the whole body every frame, so there is no
// erase problem either way) and the 1px edge does the job the old bottom
// hairline was doing, while also closing the row off as an object.
//
// Two pixels short of the row height leaves a gap between neighbours so
// the edges read as separate cards rather than one long grid, and the
// right inset clears the scrollbar at w-4, which draws later and would
// otherwise sit on top of the border.
static void rowPanel(TFT_eSPI& t, int w, int y, int hgt) {
    // The same tile Theme::drawListRowPanel draws: four rows of backdrop
    // between neighbours, inset a row from the top so the text stays centred.
    const int x0 = 3, ww = w - 10, hh = hgt - 4;
    if (ww <= 0 || hh <= 0) return;
    t.fillRect(x0, y + 1, ww, hh, Theme::BG);
    t.drawRect(x0, y + 1, ww, hh, Theme::PURPLE);
}
static void drawTwoLineRow(TFT_eSPI& t, int w, int y, int hgt, const char* label,
                           const char* value, uint16_t labelColor, bool cycles) {
    rowPanel(t, w, y, hgt);
    t.setTextSize(1);
    t.setTextColor(labelColor, Theme::BG);
    t.setCursor(8, y + 2);
    t.print(label);
    const int lineY = y + 2 + t.fontHeight() + 1;

    t.setTextSize(2);
    const int chev = t.textWidth(">");
    const int lo = 8 + chev + 4;
    const int hi = w - 18 - chev - 4;
    const int vw = value ? t.textWidth(value) : 0;
    // Centred between the arrows rather than in the row, so the longest name
    // still cannot slide underneath one of them.
    int vx = lo + ((hi - lo) - vw) / 2;
    if (vx < lo) vx = lo;
    if (value) {
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(vx, lineY);
        t.print(value);
    }
    t.setTextColor(Theme::PURPLE, Theme::BG);
    if (cycles) { t.setCursor(8, lineY); t.print("<"); }
    t.setCursor(w - 18 - chev, lineY);
    t.print(">");

    t.setTextSize(1);
}

static void drawRow(TFT_eSPI& t, int w, int y, int hgt, const char* label,
                    const char* value, bool danger, uint16_t labelColor,
                    bool compact) {
    // The full-row fill is back, as a card -- see rowPanel. The glyph-cell
    // backing below stays anyway: it costs nothing now and it is what keeps
    // text crisp if a row is ever drawn without a panel behind it.
    rowPanel(t, w, y, hgt);
    t.setTextSize(compact ? 1 : Theme::uiMenuTextSize(t));
    t.setTextColor(danger ? Theme::RED : labelColor, Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(label);
    if (value) {
        t.setTextColor(Theme::WHITE, Theme::BG);
        int vw = t.textWidth(value);
        // 18px, not 8px, reserved on the right -- leaves room for the
        // scroll indicator without it overlapping right-aligned value
        // text.
        t.setCursor(w - 18 - vw, y + (hgt - t.fontHeight()) / 2);
        t.print(value);
    }
}

// Fills in what a row actually shows. valBuf is scratch space for
// rows that need to format a number — only valid for the duration of
// the caller's own loop iteration, not held onto afterward.
static void rowContent(SettingsRow r, const DetectionEngine& eng, char* valBuf, size_t valBufN,
                       const char*& label, const char*& value, bool& danger) {
    danger = false;
    value  = nullptr;
    switch (r) {
        case SettingsRow::THEME:
            label = "THEME"; value = Theme::kPalettes[Settings::paletteIndex()].name;
            break;
        case SettingsRow::SYSTEM:
            label = "SYSTEM"; value = OtaCore::availableVersion()[0] ? "UPDATE >" : ">";
            break;
        // The tracking rows. Their whole reason to exist is naming the thing,
        // so the value is the target's own label rather than a state word.
        case SettingsRow::WATCH_TARGET:
            label = "WATCHING"; value = s_watchLabel;
            break;
        case SettingsRow::HUNT_TARGET:
            label = "HUNTING"; value = s_huntLabel;
            break;
        case SettingsRow::BACKGROUND:
            label = "BACKGROUND"; value = Settings::backgroundName(Settings::background());
            break;
        case SettingsRow::DESK_OPEN:
            label = "OPEN DESK"; value = ">";
            break;
        case SettingsRow::DESK_BACKGROUND:
            label = "BACKGROUND"; value = Settings::backgroundName(Settings::deskBackground());
            break;
        case SettingsRow::CLOCK_FONT:
            label = "CLOCK FONT"; value = Settings::clockFontName();
            break;
        case SettingsRow::CLOCK_SIZE:
            label = "CLOCK SIZE"; value = Settings::clockSizeName();
            break;
        case SettingsRow::CLOCK_BACKDROP:
            label = "CLOCK BG"; value = Settings::clockBackdropName();
            break;
#if SQUACH_MESH
        case SettingsRow::DESK_SQUAD:
            label = "SQUAD ON DESK"; value = Settings::deskSquad() ? "ON" : "OFF";
            break;
        case SettingsRow::DESK_CROWD:
            label = "HOW MANY"; value = Settings::deskCrowdLabel();
            break;
        // With one visitor. A crowd of them always chats, as on the main screen.
        case SettingsRow::DESK_VISIT:
            label = "VISITOR"; value = Settings::deskFullVisit() ? "FULL VISIT" : "CHATS";
            break;
#endif
        case SettingsRow::BACKGROUND_LOCK:
            label = "LOCK BACKGROUND"; value = Settings::backgroundLocked() ? "ON" : "OFF";
            break;
        case SettingsRow::BRIGHTNESS:
            label = "BRIGHT -  +";
            snprintf(valBuf, valBufN, "%u%%", (unsigned)(Settings::brightness() * 100 / 255));
            value = valBuf;
            break;
        case SettingsRow::INVERT:
            label = "INVERT COLORS"; value = Settings::inverted() ? "ON" : "OFF";
            break;
        case SettingsRow::RGB_SWAP:
            label = "COLOR ORDER"; value = Settings::rgbSwapped() ? "SWAPPED" : "NORMAL";
            break;
        case SettingsRow::ROTATION_LOCK:
            label = "ROTATION LOCK"; value = Settings::rotationLocked() ? "ON" : "OFF";
            break;
        case SettingsRow::BORING_MODE:
            label = "BORING MODE"; value = Settings::boringMode() ? "ON" : "OFF";
            break;
        case SettingsRow::CONFIDENCE:
            label = "ALERT FILTER"; value = Settings::minConfidenceLabel();
            break;
        case SettingsRow::AUTO_QUIET:
            label = "AUTO SNOOZE"; value = Settings::autoQuietLabel();
            break;
        case SettingsRow::DETECTION_FILTER:
            // "DETECTION FILTER" (the row's own screen title, no width
            // constraint there) overlaps its own "14/14" value in
            // portrait's 240px width at this row's size-2 text --
            // confirmed with the emulator before ever touching
            // hardware. Shortened here only; the destination screen
            // keeps the full name in its title bar.
            label = "TYPE FILTER";
            snprintf(valBuf, valBufN, "%u/%u", (unsigned)Settings::enabledTypeCount(),
                     (unsigned)DetectionType::COUNT - 1);
            value = valBuf;
            break;
        case SettingsRow::IGNORED_DEVICES:
            // "IGNORED DEVICES" is the destination screen's title; the row
            // itself is shortened for the same reason TYPE FILTER above is
            // -- the full name collides with its own value on the 240px
            // portrait rotation at this row's size-2 text.
            label = "IGNORED";
            snprintf(valBuf, valBufN, "%u", (unsigned)IgnoreList::count());
            value = valBuf;
            break;
#if defined(TWATCH_S3)
        case SettingsRow::WATCH_BATTERY:
            label = "BATTERY"; twatchBatteryLine(valBuf, valBufN); value = valBuf;
            break;
        case SettingsRow::WATCH_RADIO:
            label = "RADIOS"; value = Settings::radioDutyName(Settings::radioDutyRaw());
            break;
        case SettingsRow::WATCH_LISTEN:
            label = "BLE LISTEN"; snprintf(valBuf, valBufN, "%u%%", (unsigned)Settings::bleListenRaw()); value = valBuf;
            break;
        case SettingsRow::WATCH_IDLE_CPU:
            label = "SLEEP CPU"; snprintf(valBuf, valBufN, "%u MHz", (unsigned)Settings::idleCpuMhzRaw()); value = valBuf;
            break;
        case SettingsRow::WATCH_BUZZ:
            label = "BUZZ"; value = Settings::buzzModeName();
            break;
        case SettingsRow::WATCH_SETTINGS:
            label = "WATCH SETTINGS"; value = ">";
            break;
        case SettingsRow::WATCH_RADIO_RESET:
            label = "RADIO RESET"; value = twatchRadioResetArmed() ? "SURE?" : "GO";
            break;
        case SettingsRow::WATCH_STEADY:
            label = "STEADY POWER"; value = Settings::steadyPower() ? "ON" : "OFF";
            break;
        case SettingsRow::WATCH_TEMP:
            label = "CHIP TEMP"; snprintf(valBuf, valBufN, "%d C", twatchChipC()); value = valBuf;
            break;
        case SettingsRow::WATCH_XTAL:
            label = "CLOCK CHECK"; twatchXtalLine(valBuf, valBufN); value = valBuf;
            break;
#endif
        case SettingsRow::POWER_SAVER:
            label = "POWER SAVER"; value = Settings::powerSaver() ? "ON" : "OFF";
            break;
        case SettingsRow::STATUS_LIGHT:
            label = "STATUS LIGHT"; value = Settings::lightOn() ? "ON" : "OFF";
            break;
        case SettingsRow::SECURITY:
            label = "SECURITY"; value = Security::enabled() ? "PIN ON" : "OFF";
            break;
        case SettingsRow::CALIBRATE:
            label = "CALIBRATE TOUCH";
            break;
        case SettingsRow::CHECK_COLORS:
            label = "CHECK COLORS";
            break;
        case SettingsRow::DIAGNOSTICS:
            label = "DIAGNOSTICS";
            break;
        case SettingsRow::UPDATE_FIRMWARE:
            // The row names the newer version when one is known, so the boot
            // check and a member's hello have somewhere to point.
            label = "UPDATE FIRMWARE";
            if (OtaCore::availableVersion()[0]) { snprintf(valBuf, valBufN, "v%s >", OtaCore::availableVersion()); value = valBuf; }
            else value = ">";
            break;
        case SettingsRow::UPDATE_CHECK:
            label = "UPDATE CHECK"; value = Settings::updateCheck() ? "AT BOOT" : "OFF";
            break;
        case SettingsRow::WIFI_NETWORKS:
            label = "WIFI NETWORKS";
            if (OtaWifi::savedCount()) { snprintf(valBuf, valBufN, "%u SAVED >", (unsigned)OtaWifi::savedCount()); value = valBuf; }
            else value = "NONE >";
            break;
        case SettingsRow::TIME_ZONE:
            label = "TIME ZONE"; value = Settings::timeZoneName();
            break;
        case SettingsRow::REPLAY_INTRO:
            label = "REPLAY INTRO";
            break;
        case SettingsRow::SHOW_OFF:
            label = "SHOW OFF";
            break;
        case SettingsRow::SHADES_COLOR:
            label = "SHADES COLOR"; value = Squachy::shadesColorName();
            break;
        // "SIZE" rather than "SQUACHY SIZE": this row is already under the
        // SQUACHY heading, and the longer label plus "MEDIUM" overruns a
        // 240px portrait row by two pixels at text size 2.
        case SettingsRow::SQUACHY_SIZE:
            label = "SIZE"; value = Settings::squachySizeLabel();
            break;
#if SQUACH_MESH
        case SettingsRow::SQUACHY_NAME: {
            // What he is actually called: the typed name if there is one,
            // else the curated one. Squachy::nickname() resolves that, so
            // this row cannot disagree with the nameplate. There used to be
            // a second row, NICKNAME, cycling the curated list on its own;
            // once a name was typed it changed something nothing showed.
            label = "NAME";
            value = Squachy::nickname();
            break;
        }
        case SettingsRow::SQUACHMESH:
            label = "SQUACHMESH";
            value = Settings::meshSummary();
            break;
#endif
        case SettingsRow::OUTFIT:
            label = "OUTFIT";
            snprintf(valBuf, valBufN, "%s (%u/%u)", Squachy::outfitName(),
                     (unsigned)Squachy::unlockedOutfitCount(), (unsigned)Squachy::outfitCount());
            value = valBuf;
            break;
        case SettingsRow::PET:
            label = "PET";
            value = Squachy::petName();
            break;
        case SettingsRow::BANTER:
            label = "BANTER";
            value = Settings::banterName();
            break;
        case SettingsRow::VIEW_DIARY:
            label = "SQUACHY'S DIARY";
            break;
        case SettingsRow::BINGO: {
            label = "BINGO";
            static char bingoVal[12];
            snprintf(bingoVal, sizeof bingoVal, "%u/16 >", (unsigned)Bingo::markedCount());
            value = bingoVal;
            break;
        }
        case SettingsRow::DEX: {
            label = "SQUACHY-DEX";
            static char dexVal[12];
            snprintf(dexVal, sizeof dexVal, "%u/%u >", (unsigned)s_dexCaught, (unsigned)Dex::ENTRIES);
            value = dexVal;
            break;
        }
        case SettingsRow::DESK_MODE:
            label = "DESK MODE";
            value = ">";
            break;
        case SettingsRow::RESET_STATS:
            label = "RESET STATS";
            snprintf(valBuf, valBufN, "%lu total", (unsigned long)eng.lifetimeTotal());
            value = valBuf;
            danger = true;
            break;
        case SettingsRow::APPEARANCE:
            label = "APPEARANCE"; value = ">";
            break;
        case SettingsRow::TOP_HAT:
            label = "TOP HAT"; value = Settings::topHatShown() ? "SHOWN" : "HIDDEN";
            break;
        case SettingsRow::BACK:
            label = "< BACK";
            break;
        default:
            label = "?";
            break;
    }
}

void uiSettingsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    int w = t.width(), h = t.height();

    // buildDisplayList() has no engine of its own and is called by the hit test
    // too, so the answer is cached here once a frame. Same pattern ui_clear.cpp
    // uses for the crowd's tap targets.
    s_hasWatch = eng.watchKind() != DetectionEngine::WatchKind::NONE;
    s_hasHunt  = eng.huntKind()  != DetectionEngine::WatchKind::NONE;
    s_dexCaught = uiDexCaught(eng);
    if (s_hasWatch) { strncpy(s_watchLabel, eng.watchLabel(), sizeof(s_watchLabel) - 1); s_watchLabel[sizeof(s_watchLabel) - 1] = 0; }
    if (s_hasHunt)  { strncpy(s_huntLabel,  eng.huntLabel(),  sizeof(s_huntLabel)  - 1); s_huntLabel[sizeof(s_huntLabel)  - 1] = 0; }

    int top, bodyBottom, rowH, headerH, tallH;
    computeGeom(t, h, top, bodyBottom, rowH, headerH, tallH);

    // Whatever background style CLEAR is showing, drawn at reduced
    // strength behind this screen's own rows -- see
    // Theme::dimPaletteForOverlay()'s comment for how (temporarily
    // blending the shared palette toward BG, not per-pixel alpha,
    // which TFT_eSPI can't do cheaply). 179/256 =~ 70% toward BG, i.e.
    // the effect reads at roughly 30% of its normal strength.
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
switch (Settings::background()) {
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

    const char* pageTitle = ">> SETTINGS <<";
    if (s_page == SettingsPage::APPEARANCE) pageTitle = ">> APPEARANCE <<";
    else if (s_page == SettingsPage::SYSTEM) pageTitle = ">> SYSTEM <<";
    else if (s_page == SettingsPage::DESK)   pageTitle = ">> DESK MODE <<";
    else if (s_page == SettingsPage::WATCH)  pageTitle = ">> WATCH SETTINGS <<";
    Theme::drawTitleBar(t, pageTitle);

    // +6, not +4: four group headers plus the two tracking rows.
    DisplayItem items[LIST_MAX_N + 7];
    uint8_t n = buildDisplayList(items);
    // Clamped here rather than in uiSettingsScroll(): row heights come from
    // live font metrics, which that function has no display to ask.
    { const int m = maxScroll(items, n, top, bodyBottom, rowH, headerH, tallH);
      if (g_scroll > m) g_scroll = m; }

    int y = top;
    int idx = g_scroll;
    int visibleCount = 0;
    while (idx < n) {
        int itemH = itemHeight(items[idx], rowH, headerH, tallH);
        if (y + itemH > bodyBottom) break;
        if (items[idx].isHeader) {
            drawHeader(t, w, y, itemH, items[idx].group);
        } else {
            char valBuf[24];
            const char* label;
            const char* value;
            bool danger;
            rowContent(items[idx].row, eng, valBuf, sizeof(valBuf), label, value, danger);
            // Switched off by boring mode: greyed, with the reason where the
            // value goes, rather than gone. See isSquachyOnlyRow().
            const bool off = Settings::boringMode() && isSquachyOnlyRow(items[idx].row);
            if (off) {
                drawRow(t, w, y, itemH, label, "boring mode", false, Theme::W95_SHADOW, h > w);
            } else if (isTwoLineRow(items[idx].row)) {
                drawTwoLineRow(t, w, y, itemH, label, value, groupColor(items[idx].group),
                               items[idx].row == SettingsRow::BACKGROUND ||
                               items[idx].row == SettingsRow::DESK_BACKGROUND);
            } else {
                drawRow(t, w, y, itemH, label, value, danger, groupColor(items[idx].group),
                        h > w);
            }
        }
        y += itemH;
        idx++;
        visibleCount++;
    }

    Theme::drawScrollbar(t, w - 4, top, bodyBottom - top, n, visibleCount, g_scroll);

    // The way out, always on screen. Drawn before the panels so a confirm or
    // an explanation sits over it rather than under.
    drawPinnedBack(t, w, h);

    // Over the top of everything, so the list is still visible around it and
    // it is obvious which screen you are being asked about.
    drawSettingsConfirm(t, w, h);
}

bool uiSettingsTapHeader(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)x; (void)screenW;
    int top, bodyBottom, rowH, headerH, tallH;
    computeGeom(t, screenH, top, bodyBottom, rowH, headerH, tallH);

    DisplayItem items[LIST_MAX_N + 7];
    uint8_t n = buildDisplayList(items);
    // Same clamp the draw applies, so a tap can never be tested against a
    // scroll position the screen is not actually showing.
    { const int m = maxScroll(items, n, top, bodyBottom, rowH, headerH, tallH);
      if (g_scroll > m) g_scroll = m; }

    int cy = top;
    int idx = g_scroll;
    while (idx < n) {
        int itemH = itemHeight(items[idx], rowH, headerH, tallH);
        if (cy + itemH > bodyBottom) break;
        if (y >= cy && y < cy + itemH && items[idx].isHeader) {
            const uint8_t g = (uint8_t)items[idx].group;
            s_folded[g] = !s_folded[g];
            // Folding shortens the list under your finger; an old scroll
            // offset would leave you staring at blank space below the end.
            g_scroll = 0;
            return true;
        }
        cy += itemH;
        idx++;
    }
    return false;
}

SettingsRow uiSettingsHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)x; (void)screenW;
    int top, bodyBottom, rowH, headerH, tallH;
    computeGeom(t, screenH, top, bodyBottom, rowH, headerH, tallH);

    DisplayItem items[LIST_MAX_N + 7];
    uint8_t n = buildDisplayList(items);
    // Same clamp the draw applies, so a tap can never be tested against a
    // scroll position the screen is not actually showing.
    { const int m = maxScroll(items, n, top, bodyBottom, rowH, headerH, tallH);
      if (g_scroll > m) g_scroll = m; }

    int cy = top;
    int idx = g_scroll;
    while (idx < n) {
        int itemH = itemHeight(items[idx], rowH, headerH, tallH);
        if (cy + itemH > bodyBottom) break;
        if (y >= cy && y < cy + itemH) {
            return items[idx].isHeader ? SettingsRow::NONE : items[idx].row;
        }
        cy += itemH;
        idx++;
    }
    return SettingsRow::NONE;
}
