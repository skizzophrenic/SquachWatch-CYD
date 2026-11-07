// SquachWatch-CYD — settings screen implementation
#include "ui_settings.h"
#if !defined(ESP32)
#include <stdlib.h>     // getenv: the emulator's SQUACHSIM_SPEAKER
#endif
#include "wardrive.h"
#if SQUACH_LORA
#include "ui_lorachat.h"
#endif
#include "ota_core.h"
#include "ota_wifi.h"
#include "theme.h"
#include "privacy.h"
#include "settings.h"
#include "security.h"
#include "ignore_list.h"
#include "squachy.h"
#include "bingo.h"
#include "dex.h"
#include "ui_dex.h"
#include "ui_clear.h"   // uiClearSampleTile(), for the FONTS preview
#include "pet.h"        // Pet::drawSampleBalloon(), the same
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
static int g_scrollFor[6] = { 0, 0, 0, 0, 0, 0 };
#define g_scroll (g_scrollFor[(uint8_t)s_page])

// BACK is pinned along the bottom now. Height of that strip, reserved out of
// the body so the last row cannot hide underneath it.
// Its own copy, kept in step with Theme's -- the list stops above this
// strip, so a disagreement puts the last row under it, untappable.
#define PINNED_BACK_H (Theme::pinnedBackH(screenW))

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
#if defined(FREENOVE_S3) || defined(FREENOVE_S3_35) || defined(MARAUDER_V8) || defined(TDECK_PLUS) || defined(WAVESHARE_C5)
void boardBatteryLine(char* out, size_t n);    // main.cpp: the cell, however the board reads it
#endif
#if defined(TWATCH_S3)
void twatchBatteryLine(char* out, size_t n);   // main.cpp, where the power chip lives
uint8_t twatchGpsState();                      // main.cpp: 0 off, 1 starting, 2 no fix, 3 fix
int  twatchChipC();                            // main.cpp: the chip's temperature, cached
#endif

static const SettingsRow ALL_ROWS[] = {
#if defined(TWATCH_S3)
    // The watch's own page, first: what only a watch has to think about grew
    // to ten rows, so the main list carries one row that opens it.
    SettingsRow::WATCH_SETTINGS,
#endif
    SettingsRow::BORING_MODE, SettingsRow::CONFIDENCE, SettingsRow::AUTO_QUIET,
#if defined(CROWPANEL7)
    // Beside ALERT FILTER and AUTO SNOOZE because it is about alerts: one
    // switch, not a page of knobs like the light. Compiled only for the
    // board with a buzzer, so the emulator (no board macro) never lists it.
    SettingsRow::BUZZER,
#endif
#if SQW_SOUND_ROWS
    // The speaker, in the buzzer's seat: what it announces, then how loud.
    SettingsRow::SOUND, SettingsRow::SOUND_VOLUME,
#endif
#if SQW_WIFI_5G
    // What the sniffer listens to, beside what it does with what it hears.
    // Only on a chip that has 5 GHz, so every other board's list is as it was.
    SettingsRow::WIFI_BANDS,
#endif
    SettingsRow::DETECTION_FILTER,
    SettingsRow::IGNORED_DEVICES,
#if defined(CARDPUTER_ADV)
    SettingsRow::EXT_SCREEN,
#endif
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
#if defined(CYD_GPS)
    SettingsRow::WATCH_WARDRIVE,   // the watch's row and setting, on the CYD GPS builds
#endif
#if defined(ESP32) && !defined(TWATCH_S3)
    SettingsRow::CHARGE_MODE,
#endif
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
// how the SCREEN looks. The Legend's aura only gets a row once he has an aura
// to put out.
static const SettingsRow APPEARANCE_ROWS[] = {
    // How HE looks comes first -- these are the rows people open this page to
    // change, and they were a scroll away on the main list. AURA only
    // appears once it has been earned; see buildDisplayList().
    SettingsRow::SQUACHY_SIZE, SettingsRow::OUTFIT, SettingsRow::PET,
    SettingsRow::SHADES_COLOR, SettingsRow::BANTER, SettingsRow::AURA,
    // Then how the SCREEN looks.
    SettingsRow::THEME, SettingsRow::BACKGROUND, SettingsRow::BACKGROUND_LOCK, SettingsRow::DET_STYLE,
    SettingsRow::BRIGHTNESS,
    SettingsRow::INVERT, SettingsRow::RGB_SWAP, SettingsRow::ROTATION_LOCK,
    // And the one light that is not on the screen at all.
    SettingsRow::STATUS_LIGHT,
    // How big the words are, on a page of its own.
    SettingsRow::FONTS,
};

// The FONTS page: one row per kind of text. A kind this board can show at
// only one size is left off (see buildDisplayList()).
static const SettingsRow FONT_ROWS[] = {
    SettingsRow::FONT_MENUS, SettingsRow::FONT_TEXT, SettingsRow::FONT_BUTTONS, SettingsRow::FONT_BUBBLES,
    SettingsRow::FONT_PETS, SettingsRow::FONT_XP,
};
static const uint8_t FONT_ROWS_N = sizeof(FONT_ROWS) / sizeof(FONT_ROWS[0]);
static bool isFontRow(SettingsRow r) {
    return r == SettingsRow::FONT_MENUS || r == SettingsRow::FONT_TEXT ||
           r == SettingsRow::FONT_BUTTONS || r == SettingsRow::FONT_BUBBLES || r == SettingsRow::FONT_XP || r == SettingsRow::FONT_PETS;
}
// Which kind the preview shows: the one moved last.
static Settings::FontKind s_previewKind = Settings::FontKind::MENUS;
static Settings::FontKind fontKindFor(SettingsRow r) {
    switch (r) {
        case SettingsRow::FONT_TEXT:    return Settings::FontKind::TEXT;
        case SettingsRow::FONT_BUTTONS: return Settings::FontKind::BUTTONS;
        case SettingsRow::FONT_BUBBLES: return Settings::FontKind::BUBBLES;
        case SettingsRow::FONT_XP:      return Settings::FontKind::XP_TILES;
        case SettingsRow::FONT_PETS:    return Settings::FontKind::PETS;
        default:                        return Settings::FontKind::MENUS;
    }
}

#if defined(TWATCH_S3)
// The WATCH SETTINGS page: the battery first (a reading), then the knobs that
// decide how long it lasts, then the buzz, then the tools.
static const SettingsRow WATCH_ROWS[] = {
    SettingsRow::WATCH_BATTERY, SettingsRow::WATCH_WARDRIVE, SettingsRow::WATCH_QUIET_TAGS,
#if SQUACH_LORA
    SettingsRow::WATCH_LORA_CHATS, SettingsRow::WATCH_LORA,
#endif
    SettingsRow::WATCH_RADIO, SettingsRow::WATCH_LISTEN,
    SettingsRow::WATCH_IDLE_CPU, SettingsRow::WATCH_BUZZ,
    SettingsRow::WATCH_TEMP,
};
static const uint8_t WATCH_ROWS_N = sizeof(WATCH_ROWS) / sizeof(WATCH_ROWS[0]);
#endif

// The SYSTEM page: the rarely-needed machinery, off the main list.
static const SettingsRow SYSTEM_ROWS[] = {
    SettingsRow::PRIVACY,
#if defined(FREENOVE_S3) || defined(FREENOVE_S3_35) || defined(MARAUDER_V8) || defined(TDECK_PLUS) || defined(WAVESHARE_C5)
    SettingsRow::BOARD_BATTERY,
#endif
#if SQUACH_LORA && defined(TDECK_PLUS)
    // The T-Deck's LoRa radio: the watch's two rows, on the page it has, and
    // its own LORA TX (it has a keyboard to type with).
    SettingsRow::WATCH_LORA_CHATS, SettingsRow::WATCH_LORA, SettingsRow::LORA_TX,
#endif
#if defined(ESP32) && !defined(TWATCH_S3)
    SettingsRow::LAST_RUN,
#endif
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
    SettingsRow::CLOCK_FORMAT,
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
// This is deliberately NOT how unearned things behave: PET and AURA stay
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
           r == SettingsRow::PET || r == SettingsRow::AURA;
}

enum class RowGroupId : uint8_t { APPEARANCE, BEHAVIOR, SQUACHY, SYSTEM, DESK, SQUAD, WATCH, FONTS };

static RowGroupId groupFor(SettingsRow r) {
    // Appearance sits with the Squachy rows because that is where it was asked
    // for and where a thumb opening this screen will not hit it by accident.
    // Boring mode filters every OTHER row in that group, though, and a lone
    // "APPEARANCE" under a SQUACHY heading in a mode with no Squachy reads as
    // a leftover. It goes to SYSTEM there -- still reachable, which is the
    // part that matters, since the display rows live behind it.
    if (r == SettingsRow::APPEARANCE && Settings::boringMode()) return RowGroupId::SYSTEM;
#if defined(CYD_GPS)
    // Under its neighbor's heading: a CYD has no WATCH heading.
    if (r == SettingsRow::WATCH_WARDRIVE) return groupFor(SettingsRow::POWER_SAVER);
#endif
    switch (r) {
        case SettingsRow::WATCH_BATTERY:
        case SettingsRow::WATCH_RADIO:
        case SettingsRow::WATCH_LISTEN:
        case SettingsRow::WATCH_IDLE_CPU:
        case SettingsRow::WATCH_BUZZ:
        case SettingsRow::WATCH_SETTINGS:
        case SettingsRow::WATCH_TEMP:
        case SettingsRow::WATCH_WARDRIVE:
        case SettingsRow::WATCH_LORA:
        case SettingsRow::WATCH_LORA_CHATS:
        case SettingsRow::LORA_TX:
        case SettingsRow::WATCH_QUIET_TAGS:
            return RowGroupId::WATCH;
        // TIME ZONE sat on the SYSTEM page too, the same setting twice. Only
        // the clock reads it, so it lives with the clock.
        case SettingsRow::TIME_ZONE:
        case SettingsRow::DESK_OPEN:
        case SettingsRow::DESK_BACKGROUND:
        case SettingsRow::CLOCK_FONT:
        case SettingsRow::CLOCK_SIZE:
        case SettingsRow::CLOCK_BACKDROP:
        case SettingsRow::CLOCK_FORMAT:
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
        case SettingsRow::AURA:
        // SIZE, OUTFIT and PET moved onto the APPEARANCE page with the rest of
        // how he looks. They answer APPEARANCE rather than SQUACHY so the page
        // draws under one header instead of splitting in two.
        case SettingsRow::SQUACHY_SIZE:
        case SettingsRow::OUTFIT:
        case SettingsRow::PET:
        case SettingsRow::BANTER:
        case SettingsRow::DET_STYLE:
        case SettingsRow::FONTS:
            return RowGroupId::APPEARANCE;
        case SettingsRow::FONT_MENUS:
        case SettingsRow::FONT_TEXT:
        case SettingsRow::FONT_BUTTONS:
        case SettingsRow::FONT_BUBBLES:
        case SettingsRow::FONT_XP:
        case SettingsRow::FONT_PETS:
        case SettingsRow::FONT_PREVIEW:
            return RowGroupId::FONTS;
        case SettingsRow::BORING_MODE:
        case SettingsRow::CONFIDENCE:
        case SettingsRow::AUTO_QUIET:
#if defined(CROWPANEL7)
        case SettingsRow::BUZZER:
#endif
#if SQW_SOUND_ROWS
        case SettingsRow::SOUND:
        case SettingsRow::SOUND_VOLUME:
#endif
#if SQW_WIFI_5G
        case SettingsRow::WIFI_BANDS:
#endif
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
        case RowGroupId::FONTS:      return "TEXT SIZE";
#if defined(TDECK_PLUS)
        case RowGroupId::WATCH:      return "LORA";    // the T-Deck has the watch's LoRa rows and nothing else of the watch's
#else
        case RowGroupId::WATCH:      return "WATCH";
#endif
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
        case RowGroupId::FONTS:      return Theme::CYAN;
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
    else if (s_page == SettingsPage::FONTS)  { src = FONT_ROWS;      srcN = FONT_ROWS_N; }
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
        // PET and AURA are hidden by not being EARNED rather than by a
        // mode. Showing a permanently-off row for something you have never
        // seen would give the secret away -- and a switch for an aura he
        // does not have yet would be a switch that does nothing.
        if (r == SettingsRow::PET && !Squachy::anyPetUnlocked()) continue;
        if (r == SettingsRow::AURA && !Squachy::hasAura()) continue;
        // Not a secret, just impossible: a board without a second app slot or
        // a Bluetooth server has nothing to update into.
        if ((r == SettingsRow::UPDATE_FIRMWARE || r == SettingsRow::UPDATE_CHECK) && !OtaCore::available()) continue;
        // TEXT has one size off the large panels (it never grows): no
        // slider, rather than one with a single stop.
        if (r == SettingsRow::FONT_TEXT && !Theme::large()) continue;
        // A compact screen draws its own mini tiles, at one size.
        if (r == SettingsRow::FONT_XP && Theme::compact()) continue;
#if SQW_SOUND_ROWS && !defined(ESP32)
        // The emulator has no speaker: the rows are there for the release
        // clip to show, and only when it asks.
        if ((r == SettingsRow::SOUND || r == SettingsRow::SOUND_VOLUME) && !getenv("SQUACHSIM_SPEAKER")) continue;
#endif
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

// The preview's height, from computeGeom(): it depends on the kind shown and
// its size, both of which only a live display can measure.
static int s_previewH = 40;
// The slider rows keep the height a row has at the built size, whatever
// MENUS says: the controls stay put while the preview shows the change.
static int s_fontRowH = 26;
static int itemHeight(const DisplayItem& it, int rowH, int headerH, int tallH) {
    if (it.isHeader) return headerH;
    if (it.row == SettingsRow::FONT_PREVIEW) return s_previewH;
    if (isFontRow(it.row)) return s_fontRowH;
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

// ---- the FONTS page ----------------------------------------------------------
static void rowPanel(TFT_eSPI& t, int w, int y, int hgt);   // below, with the other rows
// The preview: a box with a sample of the kind moved last, drawn by the same
// code that draws the real thing, so what it shows is what the screens get.
static const char* const PREVIEW_TEXT = "Squachy spotted a tracker tagging along. Tap a row to see where it went.";
static const char* const PREVIEW_SAY  = "Like this?";
static const char* const PREVIEW_PET  = "Me next!";
static const int PREVIEW_CAP = 12;    // the "PREVIEW" caption's rows
static int previewButtonH() { return Theme::dialogBtnH(24); }
static int previewHeight(TFT_eSPI& t, int w) {
    int sample = 0;
    switch (s_previewKind) {
        case Settings::FontKind::MENUS:
            Theme::menuFontOn(t, t.height() > t.width());
            sample = t.fontHeight() + Theme::listRowPad();
            break;
        case Settings::FontKind::TEXT: {
            Theme::bodyFontOn(t);
            char lines[4][48];
            const uint8_t n = Theme::wrapText(t, PREVIEW_TEXT, w - 34, lines, 4);
            sample = n * (t.fontHeight() + 2);
            break;
        }
        case Settings::FontKind::BUTTONS:  sample = previewButtonH(); break;
        case Settings::FontKind::XP_TILES: sample = 12 * Theme::xpTileScale() + 4; break;
        case Settings::FontKind::PETS: {
            Theme::petFontOn(t);
            const int fh = t.fontHeight();
            sample = (fh > 8 ? fh - 1 : 9) + 6 + 4 + 4;   // a line, its margins, the tail and a gap
            break;
        }
        default:                           sample = Squachy::bubbleHeight(t, PREVIEW_SAY); break;
    }
    t.setTextFont(1);
    t.setTextSize(1);
    return PREVIEW_CAP + sample + 10;
}
static void drawFontPreview(TFT_eSPI& t, int w, int y, int hgt) {
    const int x0 = 3, ww = w - 10;
    t.fillRect(0, y - 2, w, hgt + 2, Theme::BG);   // its own strip, below the list's backdrop
    t.fillRect(x0, y + 1, ww, hgt - 4, Theme::BG);
    t.drawRect(x0, y + 1, ww, hgt - 4, Theme::CYAN);
    t.setTextFont(1);
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(x0 + 5, y + 4);
    static const char* const KIND[6] = { "MENUS", "TEXT", "BUTTONS", "BUBBLES", "XP TILES", "PETS" };
    int8_t lo, hi;
    Theme::fontRange(t, s_previewKind, lo, hi);
    int8_t cur = Theme::fontDelta(s_previewKind);
    if (cur < lo) cur = lo;
    if (cur > hi) cur = hi;
    char cap[40];
    if (Theme::fontHasFace(s_previewKind))
        snprintf(cap, sizeof cap, "PREVIEW  %s: %s, %s", KIND[(int)s_previewKind], Theme::fontStepName(cur),
                 Theme::fontSmooth(s_previewKind) ? "SMOOTH" : "PIXEL");
    else
        snprintf(cap, sizeof cap, "PREVIEW  %s: %s", KIND[(int)s_previewKind], Theme::fontStepName(cur));
    t.print(cap);
    const int sy = y + 1 + PREVIEW_CAP;
    switch (s_previewKind) {
        case Settings::FontKind::MENUS: {
            // One of this screen's own rows.
            const int rh = hgt - PREVIEW_CAP - 10;
            Theme::drawListRowPanel(t, ww - 2, sy, rh);
            Theme::menuFontOn(t, t.height() > t.width());
            while (t.textWidth("ALERT FILTER") + t.textWidth("ALL") + 8 > ww - 34 && Theme::menuFontSmaller(t)) {}
            t.setTextColor(Theme::AMBER, Theme::BG);
            t.setCursor(x0 + 8, sy + (rh - t.fontHeight()) / 2);
            t.print("ALERT FILTER");
            t.setTextColor(Theme::WHITE, Theme::BG);
            t.setCursor(x0 + ww - 18 - t.textWidth("ALL"), sy + (rh - t.fontHeight()) / 2);
            t.print("ALL");
            break;
        }
        case Settings::FontKind::TEXT: {
            Theme::bodyFontOn(t);
            char lines[4][48];
            const uint8_t n = Theme::wrapText(t, PREVIEW_TEXT, w - 34, lines, 4);
            t.setTextColor(Theme::WHITE, Theme::BG);
            for (uint8_t i = 0; i < n; i++) {
                t.setCursor(x0 + 10, sy + 2 + i * (t.fontHeight() + 2));
                t.print(lines[i]);
            }
            break;
        }
        case Settings::FontKind::BUTTONS: {
            // A screen's own button and a window's, side by side.
            const int bh = previewButtonH(), gap = 8;
            const int bw = (ww - 16 - gap) / 2;
            Theme::drawButton(t, x0 + 8, sy + 2, bw, bh, "[ GOT IT ]", false);
            Theme::drawWin95Button(t, x0 + 8 + bw + gap, sy + 2, bw, bh, "YES", false);
            break;
        }
        case Settings::FontKind::PETS:
            // A pet's balloon, its tail on a pet-sized box just under it.
            Pet::drawSampleBalloon(t, x0 + ww / 2 - 11, y + hgt - 8, 22, PREVIEW_PET);
            break;
        case Settings::FontKind::XP_TILES: {
            // Two of the main screen's own counter tiles.
            int tx = x0 + 8;
            tx += uiClearSampleTile(t, tx, sy + 2, "FLOCK 2", Theme::colorFor(DetectionType::FLOCK)) + 4 * Theme::xpTileScale();
            uiClearSampleTile(t, tx, sy + 2, "RING 5", Theme::colorFor(DetectionType::RING));
            break;
        }
        default:
            Squachy::drawSampleBubble(t, x0 + ww / 2, sy + 2, PREVIEW_SAY);
            break;
    }
    t.setTextFont(1);
    t.setTextSize(1);
}

// A slider row, one line: the label, then - [track] +. The stops are the
// sizes this board has for the kind; the one it was built with is marked
// taller. The label is in the face the row had at the built size, so nothing
// on these rows moves while you slide them. Shared by the draw and the tap.
struct SliderGeom { int bh, bw, by, minusX, plusX, x0, x1; };
// MENUS, BUTTONS and BUBBLES also have a face: their label is a box, drawn
// in the face the kind is set to, and a tap on it swaps PIXEL and SMOOTH.
static void sliderLabelFace(TFT_eSPI& t) { t.setTextFont(Theme::large() ? 2 : 1); t.setTextSize(1); }
static SliderGeom sliderGeom(TFT_eSPI& t, int w, int y, int hgt) {
    SliderGeom g;
    sliderLabelFace(t);
    int labelW = t.textWidth("XP TILES");
    t.setTextFont(2);
    if (t.textWidth("BUBBLES") > labelW) labelW = t.textWidth("BUBBLES");
    labelW += 14;
    t.setTextFont(1);
    g.bh = hgt - 8;
    if (g.bh < 10) g.bh = 10;
    g.bw = g.bh + 6;
    g.by = y + (hgt - g.bh) / 2;
    g.minusX = 8 + labelW;
    g.plusX  = w - 18 - g.bw;
    g.x0 = g.minusX + g.bw + 8;
    g.x1 = g.plusX - 8;
    return g;
}
static void drawFontRow(TFT_eSPI& t, int w, int y, int hgt, const char* label,
                        Settings::FontKind k, uint16_t labelColor) {
    rowPanel(t, w, y, hgt);
    int8_t lo, hi;
    Theme::fontRange(t, k, lo, hi);
    int8_t cur = Theme::fontDelta(k);
    if (cur < lo) cur = lo;
    if (cur > hi) cur = hi;
    const SliderGeom g = sliderGeom(t, w, y, hgt);
    if (Theme::fontHasFace(k)) {
        t.drawRect(5, y + 2, g.minusX - 10, hgt - 4, labelColor);
        t.setTextFont(Theme::fontSmooth(k) ? 2 : 1);
        t.setTextSize(1);
    } else {
        sliderLabelFace(t);
    }
    t.setTextColor(labelColor, Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(label);
    t.setTextFont(1);

    Theme::drawWin95Button(t, g.minusX, g.by, g.bw, g.bh, "-", false, true);
    Theme::drawWin95Button(t, g.plusX,  g.by, g.bw, g.bh, "+", false, true);
    const int cy = g.by + g.bh / 2;
    t.fillRect(g.x0, cy - 1, g.x1 - g.x0 + 1, 3, Theme::PURPLE);
    const int n = hi - lo;
    for (int s = lo; s <= hi; s++) {
        const int sx = n ? g.x0 + (g.x1 - g.x0) * (s - lo) / n : (g.x0 + g.x1) / 2;
        const int tick = s == 0 ? g.bh / 2 - 1 : g.bh / 4;
        t.fillRect(sx - 1, cy - tick, 3, 2 * tick + 1, s == 0 ? Theme::CYAN : Theme::PURPLE);
    }
    const int kx = n ? g.x0 + (g.x1 - g.x0) * (cur - lo) / n : (g.x0 + g.x1) / 2;
    const int r = g.bh / 2 - 3 > 3 ? g.bh / 2 - 3 : 3;
    t.fillCircle(kx, cy, r, Theme::VAPOR_PINK);
    t.drawCircle(kx, cy, r, Theme::WHITE);
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
    rowH = t.fontHeight() + Theme::listRowPad();
    const int big = t.fontHeight();
    t.setTextSize(Theme::uiTextSize(t, 1));
    headerH = t.fontHeight() + 6;
    Theme::bodyFontOn(t);   // a two-line row's label line (drawTwoLineRow)
    tallH   = t.fontHeight() + big + 7;
    Theme::bodyFontOff(t);
    if (s_page == SettingsPage::FONTS) {
        // Shorter than a list row: six of them and a preview share the page.
        s_fontRowH = Theme::large() ? 28 : 22;
        // The preview is pinned above the UP strip, outside the list, so it
        // is always on screen whatever size it shows; the sliders scroll
        // in what is left.
        s_previewH = previewHeight(t, t.width());
        bodyBottom -= s_previewH + 2;
    }
}

// Row height is decided in exactly one place, itemHeight() below, shared by
// drawing and hit testing -- if those two ever disagree, a tap lands on the
// row above the one you pressed.

void uiSettingsInit(TFT_eSPI& t) {
    // Arriving at Settings is arriving at its main page, at the top. Scroll memory is for moving BETWEEN pages inside one
    // visit -- carrying it across a fresh entry would drop you mid-list with
    // no idea why.
    s_page = SettingsPage::MAIN;
    for (uint8_t i = 0; i < 6; i++) g_scrollFor[i] = 0;
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
    // A size up on the large panels, for the body font and taller buttons.
    const bool big = Theme::large();
    pw = screenW - 40;
    if (pw > (big ? 320 : 240)) pw = big ? 320 : 240;
    ph = big ? 168 : 128;
    px = (screenW - pw) / 2;
    py = (screenH - ph) / 2;
    const int margin = 10, gap = 8, btnH = big ? 32 : 24;
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
    Theme::bodyFontOn(t);
    t.setTextColor(Theme::WHITE, Theme::BG);
    const char* lines[2] = { ct->line1, ct->line2 };
    for (uint8_t i = 0; i < 2; i++) {
        int lw = t.textWidth(lines[i]);
        t.setCursor(px + (pw - lw) / 2, py + 34 + i * (t.fontHeight() + 3));
        t.print(lines[i]);
    }
    Theme::bodyFontOff(t);

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
SettingsPage uiSettingsParentPage() {
    return s_page == SettingsPage::FONTS ? SettingsPage::APPEARANCE : SettingsPage::MAIN;
}

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
    t.setTextSize(Theme::compact() ? 1 : Theme::uiMenuTextSize(t));
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
    Theme::bodyFontOn(t);
    t.setTextColor(labelColor, Theme::BG);
    t.setCursor(8, y + 2);
    t.print(label);
    const int lineY = y + 2 + t.fontHeight() + 1;
    Theme::bodyFontOff(t);

    // Size 2, moved by FONTS > MENUS; a name too long for the room between
    // the arrows steps back down.
    int vs = 2 + Theme::fontDelta(Settings::FontKind::MENUS);
    if (vs < 1) vs = 1;
    if (vs > 4) vs = 4;
    Theme::menuFontSized(t, vs);
    while (vs > 2 && value && t.textWidth(value) > w - 18 - 8 - 2 * (t.textWidth(">") + 4)) Theme::menuFontSized(t, --vs);
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
    t.setTextFont(1);
}

static void drawRow(TFT_eSPI& t, int w, int y, int hgt, const char* label,
                    const char* value, bool danger, uint16_t labelColor,
                    bool compact) {
    // The full-row fill is back, as a card -- see rowPanel. The glyph-cell
    // backing below stays anyway: it costs nothing now and it is what keeps
    // text crisp if a row is ever drawn without a panel behind it.
    rowPanel(t, w, y, hgt);
    // A compact row on a large panel (the 3.5" in portrait) takes the body
    // font: about as wide a letter as size 1, so every row still fits, and
    // half again as tall. FONTS > MENUS moves both; a row the bigger size
    // would run off the screen steps back down, label and value together.
    Theme::menuFontOn(t, compact);
    while (t.textWidth(label) + (value ? t.textWidth(value) + 8 : 0) > w - 8 - 18
           && Theme::menuFontSmaller(t)) {}
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
    t.setTextFont(1);
}

// ---- the stepper rows ---------------------------------------------------------
// BRIGHT and VOLUME: a percent between a real - and a real + button, at the
// row's right-hand end where a value sits. BRIGHT used to be "BRIGHT -  +"
// in the label with the row split down the middle, which meant a thumb
// landing anywhere on it moved the value; now only the buttons do. The draw
// and the hit test both go through stepperLayout(), so the buttons are
// exactly where they look.
static bool isStepperRow(SettingsRow r) {
    return r == SettingsRow::BRIGHTNESS
#if SQW_SOUND_ROWS
        || r == SettingsRow::SOUND_VOLUME
#endif
        ;
}
bool uiSettingsIsStepper(SettingsRow r) { return isStepperRow(r); }
static const char* stepperValue(SettingsRow r, char* buf, size_t n) {
    unsigned pct = 0;
    if (r == SettingsRow::BRIGHTNESS) pct = (unsigned)(Settings::brightness() * 100 / 255);
#if SQW_SOUND_ROWS
    else if (r == SettingsRow::SOUND_VOLUME) pct = Settings::soundVolume();
#endif
    snprintf(buf, n, "%u%%", pct);
    return buf;
}
// Where the pieces go: the + against the right margin, the value before it,
// the - before that. Buttons are nearly the row's height, finger-sized on
// every panel; the value's width is measured in the row's own font.
static void stepperLayout(TFT_eSPI& t, int w, int y, int hgt, bool compact, const char* value,
                          int& minusX, int& plusX, int& valX, int& bw, int& bh, int& by) {
    Theme::menuFontOn(t, compact);
    const int vw = t.textWidth(value);
    t.setTextFont(1);
    bh = hgt - 4;
    bw = bh + 10;
    by = y + (hgt - bh) / 2;
    plusX  = w - 18 - bw;
    valX   = plusX - 8 - vw;
    minusX = valX - 8 - bw;
}
static void drawStepperRow(TFT_eSPI& t, int w, int y, int hgt, const char* label, const char* value,
                           uint16_t labelColor, bool compact) {
    drawRow(t, w, y, hgt, label, nullptr, false, labelColor, compact);
    int mx, px, vx, bw, bh, by;
    stepperLayout(t, w, y, hgt, compact, value, mx, px, vx, bw, bh, by);
    Theme::menuFontOn(t, compact);
    t.setTextColor(Theme::WHITE, Theme::BG);
    t.setCursor(vx, y + (hgt - t.fontHeight()) / 2);
    t.print(value);
    t.setTextFont(1);
    Theme::drawWin95Button(t, mx, by, bw, bh, "-", false, true);
    Theme::drawWin95Button(t, px, by, bw, bh, "+", false, true);
    t.setTextSize(1);
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
        case SettingsRow::CLOCK_FORMAT:
            label = "CLOCK FORMAT"; value = Settings::clock24Hour() ? "24 HOUR" : "12 HOUR";
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
            label = "BRIGHT"; value = stepperValue(r, valBuf, valBufN);
            break;
#if SQW_SOUND_ROWS
        case SettingsRow::SOUND_VOLUME:
            label = "VOLUME"; value = stepperValue(r, valBuf, valBufN);
            break;
#endif
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
#if defined(CROWPANEL7)
        case SettingsRow::BUZZER:
            // NEW ONLY rather than ON: the value says what the switch does,
            // the way AT BOOT and AFTER 5 do, so nobody expects a beep per
            // alert. The rules are in crowpanel7_buzzer.h.
            label = "BUZZER"; value = Settings::buzzerOn() ? "NEW ONLY" : "OFF";
            break;
#endif
#if SQW_SOUND_ROWS
        case SettingsRow::SOUND:
            // ALERTS is a new device, a watch-list hit and a low battery; ALL
            // adds the squad, the throws, the pets and the lamp's cable. The
            // rules are in sound.h.
            label = "SOUND"; value = Settings::soundModeName();
            break;
#endif
#if SQW_WIFI_5G
        case SettingsRow::WIFI_BANDS:
            // Short values: the label and its value share 240px in portrait
            // at size 2, the squeeze TYPE FILTER below is named for.
            label = "WIFI BANDS"; value = Settings::wifi5() ? "2.4+5" : "2.4";
            break;
#endif
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
#if defined(FREENOVE_S3) || defined(FREENOVE_S3_35) || defined(MARAUDER_V8) || defined(TDECK_PLUS) || defined(WAVESHARE_C5)
        case SettingsRow::BOARD_BATTERY:
            label = "BATTERY"; boardBatteryLine(valBuf, valBufN); value = valBuf;
            break;
#endif
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
        case SettingsRow::WATCH_TEMP:
            label = "CHIP TEMP"; snprintf(valBuf, valBufN, "%d C", twatchChipC()); value = valBuf;
            break;
        case SettingsRow::WATCH_WARDRIVE: {
            // OFF, or what it is doing: finding the GPS, waiting for a fix, or
            // logging, with how many sightings are kept.
            label = "WARDRIVE";
            const uint8_t g = twatchGpsState();
            if (!Wardrive::enabled())  value = "OFF";
            else if (g == 4)           value = "GPS RESTING";
            else if (g <= 1)           value = "GPS STARTING";
            else if (g == 2)           value = "NO FIX YET";
            else { snprintf(valBuf, valBufN, "ON  %lu", (unsigned long)Wardrive::count()); value = valBuf; }
            break;
        }
        case SettingsRow::WATCH_QUIET_TAGS:
            label = "TAGS + RINGS"; value = Settings::quietTrackers() ? "LOG ONLY" : "ALERT";
            break;
#endif
        // The LoRa rows: the watch's and the T-Deck's (the T-Deck lists them on
        // its SYSTEM page), so outside the watch-only block above -- inside it
        // they fell through to "?" on the T-Deck (2026-10-13).
#if SQUACH_LORA
        case SettingsRow::WATCH_LORA:
            label = "LORA"; value = Settings::loraListenName();
            break;
        case SettingsRow::WATCH_LORA_CHATS: {
            label = "LORA CHATS";
            const uint16_t u = uiLoraChatUnread();
            if (u) { snprintf(valBuf, valBufN, "%u NEW", (unsigned)u); value = valBuf; }
            else value = "OPEN";
            break;
        }
        case SettingsRow::LORA_TX:
            label = "LORA TX"; value = Settings::loraTxName();
            break;
#endif
#if defined(CYD_GPS)
        case SettingsRow::WATCH_WARDRIVE:
            // OFF, or what stands between it and a row, or the rows written.
            label = "WARDRIVE";
            switch (Wardrive::sdState(millis())) {
                case Wardrive::SdState::OFF:             value = "OFF"; break;
                case Wardrive::SdState::NO_CARD:         value = "NO CARD"; break;
                case Wardrive::SdState::LOW_MEMORY:      value = "LOW MEMORY"; break;
                case Wardrive::SdState::WAITING_FOR_FIX: value = "WAITING FOR FIX"; break;
                case Wardrive::SdState::LOGGING:
                    snprintf(valBuf, valBufN, "ON  %lu", (unsigned long)Wardrive::written());
                    value = valBuf;
                    break;
            }
            break;
#endif
        case SettingsRow::EXT_SCREEN:
            label = "EXT SCREEN"; value = Settings::extScreen() ? "ON" : "OFF";
            break;
        case SettingsRow::PRIVACY:
            label = "PRIVACY MODE"; value = Settings::privacyMode() ? "ON" : "OFF";
            break;
        case SettingsRow::CHARGE_MODE:
            label = "CHARGE MODE"; value = "START";
            break;
        case SettingsRow::LAST_RUN: {
            // The boot before this one: how long it ran before it stopped.
            label = "LAST RUN";
            uint16_t b[2], m[2];
            const uint8_t n = Settings::runHistory(b, m, 2);
            if (n < 2) value = "--";
            else { snprintf(valBuf, valBufN, "%uh %02um", (unsigned)(m[1] / 60), (unsigned)(m[1] % 60)); value = valBuf; }
            break;
        }
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
            // Two arrows like BRIGHT's minus and plus: the left half of the
            // row steps west, the right half east, so going one past your
            // zone is one tap back rather than a lap of the world.
            label = "ZONE <  >"; value = Settings::timeZoneName();
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
        case SettingsRow::PET:
            label = r == SettingsRow::OUTFIT ? "OUTFIT" : "PET";
            if (Squachy::jailed()) {
                // Serving time: both rows say how long, and neither moves.
                const uint32_t left = (Squachy::jailLeftMs() + 999) / 1000;
                snprintf(valBuf, valBufN, "SERVING %u:%02u", (unsigned)(left / 60), (unsigned)(left % 60));
                value = valBuf;
            } else if (r == SettingsRow::OUTFIT) {
                snprintf(valBuf, valBufN, "%s (%u/%u)", Squachy::outfitName(),
                         (unsigned)Squachy::unlockedOutfitCount(), (unsigned)Squachy::outfitCount());
                value = valBuf;
            } else {
                value = Squachy::petName();
            }
            break;
        case SettingsRow::BANTER:
            label = "BANTER";
            value = Settings::banterName();
            break;
        case SettingsRow::DET_STYLE:
            label = "DETECTIONS";
            value = Settings::detXp() ? "XP" : "CLASSIC";
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
        case SettingsRow::AURA:
            label = "AURA"; value = Settings::auraShown() ? "LIT" : "OUT";
            break;
        case SettingsRow::FONTS:
            label = "FONTS"; value = ">";
            break;
        case SettingsRow::FONT_MENUS:   label = "MENUS";   value = nullptr; break;
        case SettingsRow::FONT_TEXT:    label = "TEXT";    value = nullptr; break;
        case SettingsRow::FONT_BUTTONS: label = "BUTTONS"; value = nullptr; break;
        case SettingsRow::FONT_BUBBLES: label = "BUBBLES"; value = nullptr; break;
        case SettingsRow::FONT_XP:      label = "XP TILES"; value = nullptr; break;
        case SettingsRow::FONT_PETS:    label = "PETS"; value = nullptr; break;
        case SettingsRow::FONT_PREVIEW: label = "PREVIEW"; value = nullptr; break;
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
    char pv[40];
    if (s_hasWatch) { strncpy(s_watchLabel, Privacy::name(eng.watchLabel(), pv, sizeof pv), sizeof(s_watchLabel) - 1); s_watchLabel[sizeof(s_watchLabel) - 1] = 0; }
    if (s_hasHunt)  { strncpy(s_huntLabel,  Privacy::name(eng.huntLabel(),  pv, sizeof pv), sizeof(s_huntLabel)  - 1); s_huntLabel[sizeof(s_huntLabel)  - 1] = 0; }

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

    const char* pageTitle = ">> SETTINGS <<";
    if (s_page == SettingsPage::APPEARANCE) pageTitle = ">> APPEARANCE <<";
    else if (s_page == SettingsPage::SYSTEM) pageTitle = ">> SYSTEM <<";
    else if (s_page == SettingsPage::DESK)   pageTitle = ">> DESK MODE <<";
    else if (s_page == SettingsPage::WATCH)  pageTitle = ">> WATCH SETTINGS <<";
    else if (s_page == SettingsPage::FONTS)  pageTitle = ">> FONTS <<";
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
            } else if (isFontRow(items[idx].row)) {
                drawFontRow(t, w, y, itemH, label, fontKindFor(items[idx].row), groupColor(items[idx].group));
            } else if (isStepperRow(items[idx].row)) {
                drawStepperRow(t, w, y, itemH, label, value, groupColor(items[idx].group), h > w);
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
    if (s_page == SettingsPage::FONTS) drawFontPreview(t, w, bodyBottom + 2, s_previewH);

    // Over the top of everything, so the list is still visible around it and
    // it is obvious which screen you are being asked about.
    drawSettingsConfirm(t, w, h);
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

void uiSettingsSetFontPreview(uint8_t kind) {
    if (kind < (uint8_t)Settings::FontKind::COUNT) s_previewKind = (Settings::FontKind)kind;
}

void uiSettingsFontTap(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    int top, bodyBottom, rowH, headerH, tallH;
    computeGeom(t, screenH, top, bodyBottom, rowH, headerH, tallH);
    DisplayItem items[LIST_MAX_N + 7];
    uint8_t n = buildDisplayList(items);
    { const int m = maxScroll(items, n, top, bodyBottom, rowH, headerH, tallH);
      if (g_scroll > m) g_scroll = m; }
    int cy = top;
    for (int idx = g_scroll; idx < n; idx++) {
        const int itemH = itemHeight(items[idx], rowH, headerH, tallH);
        if (cy + itemH > bodyBottom) break;
        if (y >= cy && y < cy + itemH) {
            if (items[idx].isHeader || !isFontRow(items[idx].row)) return;
            const Settings::FontKind k = fontKindFor(items[idx].row);
            int8_t lo, hi;
            Theme::fontRange(t, k, lo, hi);
            int cur = Theme::fontDelta(k);
            if (cur < lo) cur = lo;
            if (cur > hi) cur = hi;
            const SliderGeom g = sliderGeom(t, screenW, cy, itemH);
            // The buttons step; anywhere along the track jumps to the
            // nearest stop -- a few pixels of slack either way.
            if (x < g.minusX - 4) {                     // the label: the face, where it has one
                if (Theme::fontHasFace(k)) { Theme::toggleFontFace(k); s_previewKind = k; }
                return;
            }
            if (x < g.minusX + g.bw + 4)      cur--;
            else if (x >= g.plusX - 4)        cur++;
            else if (hi > lo) {
                const int span = g.x1 - g.x0;
                int px = x - g.x0;
                if (px < 0) px = 0;
                if (px > span) px = span;
                cur = lo + (px * (hi - lo) + span / 2) / span;
            }
            Theme::setFontDelta(t, k, cur);
            s_previewKind = k;
            return;
        }
        cy += itemH;
    }
}

int uiSettingsStepHit(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    int top, bodyBottom, rowH, headerH, tallH;
    computeGeom(t, screenH, top, bodyBottom, rowH, headerH, tallH);
    DisplayItem items[LIST_MAX_N + 7];
    uint8_t n = buildDisplayList(items);
    { const int m = maxScroll(items, n, top, bodyBottom, rowH, headerH, tallH);
      if (g_scroll > m) g_scroll = m; }
    int cy = top;
    int idx = g_scroll;
    while (idx < n) {
        const int itemH = itemHeight(items[idx], rowH, headerH, tallH);
        if (cy + itemH > bodyBottom) break;
        if (y >= cy && y < cy + itemH) {
            if (items[idx].isHeader || !isStepperRow(items[idx].row)) return 0;
            char valBuf[24];
            const char* value = stepperValue(items[idx].row, valBuf, sizeof valBuf);
            int mx, px, vx, bw, bh, by;
            stepperLayout(t, screenW, cy, itemH, screenH > screenW, value, mx, px, vx, bw, bh, by);
            // A few pixels of slack either side: the buttons are the row's
            // height already, and a near miss should count, not do nothing.
            if (x >= mx - 4 && x < mx + bw + 4) return -1;
            if (x >= px - 4 && x < px + bw + 4) return 1;
            return 0;
        }
        cy += itemH;
        idx++;
    }
    return 0;
}
