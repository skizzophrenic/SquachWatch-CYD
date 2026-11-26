// SquachWatch-CYD — the alert card's skins: one card, eleven operating systems.
//
// The card's content (verdict, the trail of stops, the reasons, the facts, the
// signal ladder, the five buttons) is the same in every skin and is drawn by
// one routine inside whatever window the skin puts round it. A skin only
// decides the chrome: the desk, the frame, the title bar, the buttons' faces
// and how the window opens. TRAIL (id 0) is not a skin here: it is the card
// ui_alert.cpp has always drawn, and it is what every panel too small for a
// window gets whatever the pick (see fits()).
//
// Colours come from the theme. Each skin draws with 21 colour roles (night,
// plum, rose, peach, sun, aqua and the rest); DU5K, the dusk theme, fills them
// by hand off the Ring Ding Dong opener, and every other theme fills them by
// one recipe from its own 12 colours, keeping its own background. Every role
// is snapped to a colour an 8-bit frame holds exactly, so nothing bands.
//
// Laid out in a 320x240 design space, scaled evenly onto the panel and
// centred; the desk fills the whole panel.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "state.h"

namespace AlertSkin {

enum Id : uint8_t { TRAIL = 0, SUNSET95, DESKTOP, SYSTEM7, GLASS, TILING, BEOS, AMIGA, NEXT, XP, AQUA, TERMINAL, COUNT };
const char* name(uint8_t id);

// Seven of the eleven on the boards whose flash is tightest -- the ESP32 CYDs
// and the C5s, about 1.5 KB a skin -- and in the emulator built as one of them;
// all eleven on the S3 boards and the Tab5. The ids stay the same everywhere,
// so a saved pick means the same skin on every board.
// ALERT_SKINS_FORCE_ALL: all eleven whatever the board, for rendering clips.
#if defined(ALERT_SKINS_FORCE_ALL)
#define ALERT_SKINS_ALL 1
#elif defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32C5) || \
    (!defined(ESP32) && !defined(TDECK_PLUS) && !defined(TWATCH_S3) && !defined(CARDPUTER_ADV) && !defined(TAB5))
#define ALERT_SKINS_ALL 0
#else
#define ALERT_SKINS_ALL 1
#endif
inline bool available(uint8_t id) {
#if ALERT_SKINS_ALL
    return id < COUNT;
#else
    // Picked by the owner, 2026-11-21.
    return id == TRAIL || id == SUNSET95 || id == SYSTEM7 || id == GLASS || id == AMIGA || id == XP || id == AQUA || id == TERMINAL;
#endif
}
// How many this board has (TRAIL included), and where id falls among them, from 1.
inline uint8_t availableCount() { uint8_t n = 0; for (uint8_t i = 0; i < COUNT; i++) if (available(i)) n++; return n; }
inline uint8_t availableRank(uint8_t id) { uint8_t n = 0; for (uint8_t i = 0; i <= id && i < COUNT; i++) if (available(i)) n++; return n; }

// ---- colour roles (alert_skin_roles.cpp, no drawing: the host tests read it) ----
struct Roles {
    uint16_t deep, night, indigo, plum, plum2, mauve, rose, rose2, peach,
             sun1, sun2, sun3, lav, pale, dim, slate, slate2, aqua, teal, mint, gold;
    bool light;   // the skins with light windows (XP, Aqua, the Amiga's blue) keep them light
};
static const uint8_t ROLE_COUNT = 21;
Roles duskRoles();
// pal: a Theme::Palette's 12 colours in order (bg, taskbar, purple, cyan, pink,
// vaporPink, vaporPurple, vaporBlue, vaporYellow, green, amber, red).
Roles deriveRoles(const uint16_t pal[12]);
// The nearest colour an RGB332 frame holds, as 565; and whether a 565 colour is one.
uint16_t snap565(int r, int g, int b);
bool     is332(uint16_t c565);
inline uint16_t roleAt(const Roles& r, uint8_t i) { return (&r.deep)[i]; }

// ---- the card, as plain data (ui_alert.cpp fills it from the live row) ----
enum class Verdict : uint8_t { NEW, MET, FOLLOWING, SPAM };
struct Stop {
    int16_t pos;        // where along the line, 0..1000
    char time[8];
    char place[12];
    char dwell[10];
    char leg[10];       // the distance from the stop before, or empty
};
struct Tick { char label[12]; bool on; };
struct Card {
    Verdict verdict;
    const char* verdictWord;
    const char* typeLabel;
    const char* typeShort;
    DetectionType type;
    char owner[18];     // the chip: "AWAY 2 H", "WITH OWNER", or empty
    bool ownerAway;
    char ownerRow[20];  // the fact row, trackers only: "WITH OWNER", "AWAY 2 H", "NOT SET UP"; empty otherwise
    char met[24];       // "MET NEVER", a regular's name, "AT NIGHT"...
    uint8_t nStops;
    bool hereLast;      // the last stop is where the board is now
    Stop stops[Detection::STOPS_N];
    Tick ticks[4];
    int8_t spark[Detection::SPARK_N];   // oldest first
    uint8_t sparkN;
    int8_t rssi;
    const char* trend;
    bool trendUp;
    char who[48];
    char mac[24];
    char seen[48];
    const char* conf;
    uint8_t confLevel;  // 0 low, 1 medium, 2 high
    uint8_t queue;      // catches waiting behind this one
    const char* btn[5];
    const char* hot[5]; // the hotkey labels (the buttons' own words)
    bool sunk[5];
    bool picking;       // the four snooze chips are up
};

// The 8-bit frame the board draws into, when it draws the whole screen in one:
// the glass, the scanlines and the dissolve read it back directly. nullptr when
// the board draws in bands, and in the emulator, which reads pixels instead.
void setFrame(uint8_t* buf, int w, int h);

// Big enough for a window: anything smaller draws TRAIL.
bool fits(int w, int h);

// motion: 0 FULL, 1 OPENING ONLY, 2 OFF. sunset: the desk behind the window.
void draw(TFT_eSPI& t, uint8_t id, const Card& c, uint32_t now, uint32_t openedAt,
          uint32_t queueAt, uint8_t motion, bool sunset, uint8_t palette);

// Where the last draw() put things, in panel pixels. False before the first draw.
bool buttonRect(uint8_t i, int& x, int& y, int& w, int& h);
bool queueRect(int& x, int& y, int& w, int& h);
bool chipRect(uint8_t i, int& x, int& y, int& w, int& h);
void forget();   // a new card: nothing drawn yet

// The MORE INFO card inside the picked window (info_card.cpp). The window and
// its buttons are the skin's; the middle is `body`, which draws the card in
// panel pixels and returns how many buttons it wants: 2 (BACK, GOT IT) or 3
// (BACK, MORE, GOT IT). The title says ABOUT and the device's name. No
// opening animation: the window is already up behind it.
typedef uint8_t (*InfoBody)(TFT_eSPI& t, int x, int y, int w, int h);
void drawInfo(TFT_eSPI& t, uint8_t id, const char* name, DetectionType type, InfoBody body,
              uint32_t now, bool sunset, uint8_t palette);

}  // namespace AlertSkin
