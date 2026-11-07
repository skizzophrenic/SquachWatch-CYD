// SquachWatch-CYD — settings screen (theme, background, invert,
// brightness, alert confidence filter, calibration entry, reset stats)
#pragma once
#include <TFT_eSPI.h>
#include "detection.h"

// The speaker's two rows: on the boards with one, and in the emulator, where
// they are listed only with SQUACHSIM_SPEAKER set (for the release clip) so
// the web emulator never offers a sound it cannot make.
#if defined(FREENOVE_S3) || defined(FREENOVE_S3_35) || defined(TDECK_PLUS) || defined(WAVESHARE_C5) || !defined(ESP32)
#define SQW_SOUND_ROWS 1
#else
#define SQW_SOUND_ROWS 0
#endif

enum class SettingsRow : uint8_t {
    THEME = 0,
    BACKGROUND,
    BACKGROUND_LOCK,
    BRIGHTNESS,
    INVERT,
    RGB_SWAP,
    ROTATION_LOCK,
    BORING_MODE,
    CONFIDENCE,
    AUTO_QUIET,     // "AUTO SNOOZE": how often one device may interrupt
    DETECTION_FILTER,
    IGNORED_DEVICES,
    POWER_SAVER,
    SECURITY,
    CALIBRATE,
    CHECK_COLORS,
    DIAGNOSTICS,
    REPLAY_INTRO,
    SHOW_OFF,
    SHADES_COLOR,
    SQUACHY_SIZE,
    OUTFIT,
    PET,
    VIEW_DIARY,
    RESET_STATS,
    SQUACHY_NAME,   // opens the payphone; SquachMesh builds only
    SQUACHMESH,     // announce ourselves to other SquachWatches
    APPEARANCE,     // opens the APPEARANCE page: the display rows, and the aura
    AURA,           // on the APPEARANCE page, once he is a Legend
    SYSTEM,         // opens the SYSTEM page: calibrate, colours, diagnostics, reset
    WATCH_TARGET,   // "WATCHING: <name>", only while a watch is set. Taps clear it.
    HUNT_TARGET,    // "HUNTING: <name>", same deal
    UPDATE_FIRMWARE, // on the SYSTEM page: Bluetooth update, or switch slots
    UPDATE_CHECK,    // on the SYSTEM page: ask the site at boot, over saved WiFi
    WIFI_NETWORKS,   // on the SYSTEM page: the saved networks, up to six
    STATUS_LIGHT,    // on the APPEARANCE page: opens the RGB LED's screen
    BANTER,          // on the APPEARANCE page: how much he talks when nothing is happening
    TIME_ZONE,       // on the DESK MODE page: which zone the real clock shows
    BINGO,           // opens the bingo card
    DEX,             // opens the SQUACHY-DEX
    DESK_MODE,       // opens the DESK MODE page
    DESK_OPEN,       // on the DESK MODE page: go to the desk
    DESK_BACKGROUND, // on the DESK MODE page: the desk's own scene
    DESK_SQUAD,      // on the DESK MODE page: the squad under the clock, on or off
    DESK_CROWD,      // on the DESK MODE page: how many of them
    DESK_VISIT,      // on the DESK MODE page: one visitor chats, or does the whole visit
    CLOCK_FONT,      // on the DESK MODE page: segments or Bangers
    CLOCK_SIZE,      // on the DESK MODE page: small, medium, large
    CLOCK_BACKDROP,  // on the DESK MODE page: what plays inside the clock
    WATCH_BATTERY,   // the T-Watch only: charge and voltage, under WATCH
    WATCH_RADIO,     // the T-Watch only: the radio duty cycle, under WATCH
    WATCH_LISTEN,    // the T-Watch only: BLE LISTEN, how much of the time the receiver is open
    WATCH_IDLE_CPU,  // the T-Watch only: SLEEP CPU, the clock while the screen is asleep
    WATCH_BUZZ,      // the T-Watch only: buzz on an alert, OFF / HIGH / MED / LOW
    WATCH_SETTINGS,  // the T-Watch only: the row on the main list that opens the WATCH SETTINGS page
    WATCH_TEMP,      // the T-Watch only: the chip's temperature, a reading, under WATCH
    WATCH_WARDRIVE,  // the T-Watch only: WARDRIVE, every network and device with where, for WiGLE
#if SQW_HOME_SYNC
    HOME_SYNC,       // the GPS builds: hand the wardrive logs to a Tab5 base over home WiFi
#endif
    WATCH_QUIET_TAGS, // the T-Watch only: TAGS + RINGS, logged without waking the watch
    WATCH_LORA,      // the T-Watch only: LORA, which networks the LoRa radio listens to
    WATCH_LORA_CHATS, // the T-Watch only: LORA CHATS, what it decoded, one chat per network
    LORA_TX,         // the T-Deck only: LORA TX, off or the power the chats' compose line sends at
#if defined(CROWPANEL7)
    BUZZER,          // the CrowPanel 7 only, the one board with a buzzer: OFF, or NEW ONLY
#endif
#if SQW_SOUND_ROWS
    SOUND,           // the Freenove S3 boards, which have a speaker: OFF, ALERTS or ALL
    SOUND_VOLUME,    // and how loud: 0-100% on its - and + buttons
#endif
#if SQW_WIFI_5G
    WIFI_BANDS,      // a chip with 5 GHz only (the NM-CYD-C5): 2.4+5, or 2.4
#endif
    BOARD_BATTERY,   // Freenove S3s, Marauder V8 and T-Deck Plus only: known battery sensing, on SYSTEM
    CHARGE_MODE,     // the CYD boards: radios and screen off so a battery charges faster
    LAST_RUN,        // the CYD boards: how long the previous boot ran, on the SYSTEM page
    PRIVACY,         // PRIVACY MODE: addresses and names masked on screen, on the SYSTEM page
    EXT_SCREEN,      // the Cardputer ADV only: a second panel on the EXT header
    DET_STYLE,       // on the APPEARANCE page: XP or CLASSIC detection counters
    CLOCK_FORMAT,    // on the DESK MODE page: 12-hour or 24-hour time
    FONTS,           // on the APPEARANCE page: opens the FONTS page
    FONT_MENUS,      // on the FONTS page: list and settings rows
    FONT_TEXT,       // on the FONTS page: running text, headings, info panels
    FONT_BUTTONS,    // on the FONTS page: button labels
    FONT_BUBBLES,    // on the FONTS page: his bubble and the pets' balloons
    FONT_XP,         // on the FONTS page: the XP counter tiles, 1x or 2x
    FONT_PETS,       // on the FONTS page: the pets' balloons
    FONT_PREVIEW,    // on the FONTS page: a sample of the last kind moved, at its size
    BACK,
    COUNT,
    NONE = 255
};

void uiSettingsInit(TFT_eSPI& t);
void uiSettingsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);
void uiSettingsScroll(int delta);     // positive = scroll down

// Sub-pages are this same screen with a different list on it, so their rows
// draw and hit-test exactly as they always did. uiSettingsInit() always comes
// back to the main page.
//
// Each page keeps its OWN scroll position, so leaving a page and coming back
// puts you where you were instead of at the top -- the list is long enough
// that losing your place was the most-felt annoyance on this screen.
enum class SettingsPage : uint8_t { MAIN = 0, APPEARANCE = 1, SYSTEM = 2, DESK = 3, WATCH = 4, FONTS = 5 };
// Where UP goes from a page: FONTS sits inside APPEARANCE, the rest inside MAIN.
SettingsPage uiSettingsParentPage();
void         uiSettingsOpenPage(SettingsPage p);
SettingsPage uiSettingsCurrentPage();

// Kept so existing callers read the same. APPEARANCE only.
void uiSettingsOpenAppearance(bool open);


// True when a mode has switched this row off (boring mode, today). A tap on
// one says why rather than silently doing nothing.
bool        uiSettingsRowIsOff(SettingsRow r);

// BACK is pinned to the bottom edge rather than living at the end of the list,
// so leaving never means scrolling to find the way out. True when (x,y) is on
// it -- checked before the row hit test, which cannot see it.
bool uiSettingsTapPinnedBack(TFT_eSPI& t, int x, int y, int screenW, int screenH);
// The DESK MODE page's strip has OK on its left half: out of Settings to the
// desk or the main screen, whichever it was opened from. Check before BACK.
bool uiSettingsTapPinnedOk(int x, int y, int screenW, int screenH);

// Row layout matches whatever uiSettingsTick just drew (same geometry
// function underneath), so call this only against a screen that's
// already showing the settings screen. Needs a live TFT_eSPI& (not
// just the screen dimensions) since row height now depends on actual
// font metrics -- same reasoning as ui_rawscan.cpp's uiRawScanRowAt().
SettingsRow uiSettingsHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH);
// The stepper rows (BRIGHT, VOLUME) carry a real - and + button each. For a
// tap that uiSettingsHitTest put on one of those rows: -1 on the minus, +1
// on the plus, 0 anywhere else on the row -- which does nothing, so a thumb
// resting on the label never nudges the value.
int uiSettingsStepHit(TFT_eSPI& t, int x, int y, int screenW, int screenH);
bool uiSettingsIsStepper(SettingsRow r);
// The FONTS page's sliders: a tap on a FONT_ row's - or + steps it, a tap on
// its track jumps to the nearest stop. Call for a tap uiSettingsHitTest put
// on one of those rows.
void uiSettingsFontTap(TFT_eSPI& t, int x, int y, int screenW, int screenH);
// Which kind the preview box shows (Settings::FontKind as a number); a slider
// tap sets it. For the emulator's shots of each.
void uiSettingsSetFontPreview(uint8_t kind);

// ---- confirmation panel ---------------------------------------------------
// Two rows here cannot be taken back by tapping them again: CALIBRATE TOUCH
// throws away a working calibration before it knows the new one is any good,
// and RESET STATS wipes a lifetime count that also gates most of the outfits.
// Both now put a panel up first. It is drawn over the list by uiSettingsTick()
// and hit-tested the same way the log screen's confirm panel is, except that
// the pending row lives in this module rather than in main.cpp -- there is
// nothing main.cpp needs it for between the tap that sets it and the tap that
// answers it.
enum class SettingsConfirmTap { NONE, CONFIRM, CANCEL };

// SettingsRow::NONE clears it. Anything without its own panel text is ignored.
void               uiSettingsSetConfirm(SettingsRow r);
SettingsRow        uiSettingsConfirmRow();
SettingsConfirmTap uiSettingsHitConfirm(int x, int y, int screenW, int screenH);
