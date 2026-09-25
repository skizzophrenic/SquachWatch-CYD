// SquachWatch-CYD — persisted user settings implementation
#include "settings.h"
#include "clock.h"
#include "theme.h"
#include <Preferences.h>

namespace Settings {

static Preferences s_prefs;
static uint8_t     s_palette    = 0;
static Background  s_background = Background::DIGITAL;
static uint16_t    s_mascotPace   = 120;   // PACE: ms between the mascot's steps
static uint8_t     s_mascotTempo  = 70;    // TEMPO: percent on his durations
static bool        s_inverted   = false;
static bool        s_rgbSwapped = false;
static bool        s_colorChecked = false;
#if SQUACH_MESH
// Declared with the other flags: load() reads them long before the accessors
// below are defined.
static bool        s_meshDetect   = false;
static bool        s_meshTransmit = false;
static bool        s_meshConsent  = false;
static bool        s_phoneQwerty  = false;
static bool        s_messagesOn   = false;
static bool        s_msgTutor     = false;
#endif
static bool        s_infoPrimerShown = false;
// Locked on the watch: a square screen with a crown has one way up, and a
// corner button that spins it is a thing to hit by accident on a wrist.
// Locked on the CrowPanel too, for a different reason: an RGB panel has no
// MADCTL, so rotating it would be a per-pixel software transform of an
// 800x480 buffer every frame -- and the panel is natively landscape anyway.
#if defined(TWATCH_S3) || defined(CROWPANEL7)
static const bool DEFAULT_ROTATION_LOCK = true;
#else
static const bool DEFAULT_ROTATION_LOCK = false;
#endif
static bool        s_rotationLocked = DEFAULT_ROTATION_LOCK;
static bool        s_topHat = true;
// Eight is the ceiling because the radio's own squad ring holds eight (see
// SQUAD_N in mesh.cpp). A menu that offered thirty would be offering something
// the hardware cannot hear: the ninth board in the room evicts the first, and
// the screen would still show eight.
static const uint8_t CROWD_MAX = 8;
static uint8_t     s_meshCrowd = 1;   // how many on screen at once, 1..CROWD_MAX
static bool        s_deskSquad     = false;   // the squad on the desk clock as well
static uint8_t     s_deskCrowd     = 1;       // the desk's own HOW MANY
static bool        s_deskFullVisit = false;   // one visitor: the whole visit, not a chat
// The screen's starting rotation: landscape on every CYD; the watch reads
// one step round from that with its crown on the right (LilyGo's default).
#if defined(TWATCH_S3)
static const uint8_t DEFAULT_ROTATION = 2;
#elif defined(CROWPANEL7)
static const uint8_t DEFAULT_ROTATION = 0;   // the panel is landscape as wired
#else
static const uint8_t DEFAULT_ROTATION = 1;
#endif
static uint8_t     s_rotation = DEFAULT_ROTATION;
// OFF / 5 / 10. Stored as the number itself rather than an index, so the
// value in NVS still means something if the choices ever change.
static uint8_t s_autoQuiet = 0;
static bool        s_backgroundLocked = false;
static uint8_t     s_deskBg     = 255;     // 255: not picked yet, follow s_background
static bool        s_deskActive = false;
static uint8_t     s_clockFont     = 0;   // 0 segments, 1 Bangers
static uint8_t     s_clockSize     = 1;   // 0 small, 1 medium, 2 large
static uint8_t     s_clockBackdrop = 0;   // 0 plain, 1 rain, 2 snow, 3 toasters, 4 fire, 5 stars, 6 fireflies
static const uint8_t CLOCK_BG_N  = 7;
// Bit N = DetectionType N enabled. UNKNOWN (0) is never included -- see
// Types that arrive switched OFF, on a fresh install and on upgrade alike.
//
// Only IBEACON so far, and it is not a judgement about how interesting they
// are -- it is about how MANY. Proximity beacons are bolted to shelves in
// their dozens; one shop can put more of them in range than this device
// would otherwise see all week, and the ALERT screen is gated on confidence
// rather than on type, so with an exact-match signature every one of them
// would take over the display. Off by default, one tap away in DETECTION
// FILTER, and everything about the detection itself is honest either way.
static const uint32_t DEFAULT_OFF = (1u << (uint8_t)DetectionType::IBEACON);

// typeEnabled()'s comment. Default has bits 1..(COUNT-1) set (every real
// type on), computed once at namespace-init time rather than a hand-
// maintained literal so it can never drift out of sync with COUNT.
// 32-bit, not 16. DetectionType::COUNT reached 17 when IBEACON was added
// (and 18 with HACKER),
// and bit 16 does not exist in a uint16_t -- the shift is undefined and the
// last type silently loses its switch. NVS has always stored this through
// putUInt/getUInt, so the saved format is unchanged and nothing migrates.
static uint32_t    s_typeMask = 0;
// MEDIUM (85%). Only ever consulted on a board that has never been told
// otherwise -- anyone who has touched the SIZE row has a stored value and
// keeps it, which is why changing this default is safe.
static uint8_t     s_sqSizeIx = 1;
static uint8_t     s_brightness = 255;
static Confidence  s_minConf    = Confidence::LOW_CONF;
static bool        s_boringMode = false;

// ---- power saver ---------------------------------------------------------
// Indices into the tables below rather than raw values, so the menu can
// cycle them without knowing what the steps are and a saved index stays
// valid if a step is ever inserted.
static const uint16_t SCREEN_TIMEOUTS[] = { 0, 15, 30, 60, 120, 300 };
static const uint8_t  SCREEN_TIMEOUTS_N = sizeof(SCREEN_TIMEOUTS) / sizeof(SCREEN_TIMEOUTS[0]);
static const uint8_t  IDLE_FPS[]        = { 0, 20, 12, 8, 5 };
static const uint8_t  IDLE_FPS_N        = sizeof(IDLE_FPS) / sizeof(IDLE_FPS[0]);
static const uint16_t IDLE_AFTER[]      = { 5, 10, 20, 30, 60 };
static const uint8_t  IDLE_AFTER_N      = sizeof(IDLE_AFTER) / sizeof(IDLE_AFTER[0]);
static const uint16_t CPU_MHZ[]         = { 240, 160, 80 };
static const uint8_t  CPU_MHZ_N         = sizeof(CPU_MHZ) / sizeof(CPU_MHZ[0]);

// On by default on the watch: a screen timeout is what makes a watch a
// watch, and the rest of the saver's rows stay at their stock values until
// someone changes them.
#if defined(TWATCH_S3)
static const bool DEFAULT_POWER_SAVER = true;
#else
static const bool DEFAULT_POWER_SAVER = false;
#endif
static bool     s_powerSaver   = DEFAULT_POWER_SAVER;
static uint8_t  s_scrTimeoutIx = 2;    // 30 s
static uint8_t  s_dimLevel     = 16;   // ~6%, dim but not off
static uint8_t  s_idleFpsIx    = 2;    // 12 fps
static uint8_t  s_idleAfterIx  = 1;    // 10 s
static uint8_t  s_cpuIx        = 0;    // 240 MHz, the stock clock
static const uint8_t  BLE_LISTEN[]   = { 25, 50, 75 };
static const uint16_t IDLE_CPU[]     = { 80, 160, 240 };
#if defined(TWATCH_S3)
static const uint8_t  BLE_LISTEN_DEFAULT = 0;   // 25%
static const uint8_t  IDLE_CPU_DEFAULT   = 0;   // 80 MHz asleep
#else
static const uint8_t  BLE_LISTEN_DEFAULT = 2;   // 75%, as always
static const uint8_t  IDLE_CPU_DEFAULT   = 2;   // no change asleep
#endif
static uint8_t  s_bleIx        = BLE_LISTEN_DEFAULT;
static uint8_t  s_idleCpuIx    = IDLE_CPU_DEFAULT;
static bool     s_wakeOnAlert  = true;
static uint8_t  s_buzzMode     = 2;    // 0 OFF, 1 HIGH, 2 MED, 3 LOW; MED by default
static bool     s_steady       = false;
// The watch's radio duty cycle: on for a few seconds, resting for the rest.
// 0 ALWAYS, 1 five seconds of thirty, 2 ten of sixty, 3 BLE always on with
// WiFi five of thirty. Watch only; the CYDs never read it.
static const char* const RADIO_DUTY_NAMES[] = { "ALWAYS", "5/30", "10/60", "BLE+5/30" };
static const uint8_t     RADIO_DUTY_N       = 4;
#if defined(TWATCH_S3)
// BLE+5/30: Bluetooth listens all the time, WiFi rests 25 s in every 30.
// Trackers walking past are the catches that cannot wait; WiFi is the
// radio that costs the most to keep on.
static const uint8_t RADIO_DUTY_DEFAULT = 3;
#else
static const uint8_t RADIO_DUTY_DEFAULT = 1;
#endif
static uint8_t  s_radioDutyIx  = RADIO_DUTY_DEFAULT;

// ---- buzzer --------------------------------------------------------------
// Opt in. The one setting here that can make a sound, so it starts off and
// stays off until somebody finds the row. Only the CrowPanel 7 shows it.
static bool     s_buzzer       = false;

// ---- status light --------------------------------------------------------
static bool    s_lightOn     = true;
static bool    s_lightAlerts = true;
static bool    s_lightMsgs   = true;
static uint8_t s_lightIdle   = 1;    // BREATHE
static uint8_t s_lightColor  = 0;    // THEME
static uint8_t s_lightBright = 2;    // of 5
static bool    s_remoteUpdate = false;
static bool    s_phraseShown  = true;
static bool    s_updateCheck  = true;
static uint8_t s_timeZone     = 10;   // UTC in Clock's table
static bool    s_tzChosen     = false;
static const char* const LIGHT_IDLE_NAMES[]  = { "OFF", "BREATHE", "SOLID" };
static const char* const BANTER_NAMES[]      = { "IMPORTANT", "LESS", "NORMAL", "MORE" };
static const float       BANTER_SCALE[]      = { 2.5f, 2.5f, 1.0f, 0.5f };
static uint8_t s_banter = 2;   // NORMAL: what every board did before the row existed
// BACKGROUND is stored as 10, after the fixed colours, so the indices saved
// by the first build stay what they were; the cycle below still visits it
// second, next to THEME, which is where it belongs on the screen.
static const char* const LIGHT_COLOR_NAMES[] = { "THEME", "RED", "ORANGE", "YELLOW", "GREEN",
                                                 "CYAN", "BLUE", "PURPLE", "PINK", "WHITE",
                                                 "BACKGROUND" };
static const uint8_t LIGHT_COLOR_N = sizeof(LIGHT_COLOR_NAMES) / sizeof(LIGHT_COLOR_NAMES[0]);

const char* backgroundName(Background b) {
    switch (b) {
        case Background::DIGITAL:    return "DIGITAL RAIN";
        case Background::STARFIELD: return "STARFIELD";
        case Background::TOASTERS:  return "FLYING TOASTERS";
        case Background::AQUARIUM:  return "AQUARIUM";
        case Background::TERMINAL:  return "TERMINAL LOG";
        case Background::FIREFLIES: return "FIREFLIES";
        case Background::FIRE:      return "FIRE";
        case Background::SNOWFALL:  return "SNOWFALL";
        case Background::SPECTRUM:  return "THE GIBSON";
        case Background::TUNNEL:    return "WIREFRAME TUNNEL";
        case Background::SYNTHWAVE: return "SYNTHWAVE";
        case Background::BLACK:     return "BLACK";
        default:                    return "?";
    }
}

// Every getter reports the STOCK value when the master switch is off, so
// the rest of the firmware never has to ask twice: one call tells it both
// whether the feature is on and what to do.
bool     powerSaver()       { return s_powerSaver; }
#if defined(TWATCH_S3)
// On the watch the screen times out POWER SAVER or not: a screen left on is
// never what a wrist wants. The two exceptions are the cable (main.cpp
// keeps it lit on USB) and NEVER, kept as the deliberate override.
uint16_t screenTimeoutSec() { return SCREEN_TIMEOUTS[s_scrTimeoutIx]; }
#else
uint16_t screenTimeoutSec() { return s_powerSaver ? SCREEN_TIMEOUTS[s_scrTimeoutIx] : 0; }
#endif
uint8_t  dimLevel()         { return s_dimLevel; }
uint8_t  idleFps()          { return s_powerSaver ? IDLE_FPS[s_idleFpsIx] : 0; }
uint16_t idleAfterSec()     { return IDLE_AFTER[s_idleAfterIx]; }
uint16_t cpuMhz()           { return s_powerSaver ? CPU_MHZ[s_cpuIx] : 240; }
bool     wakeOnAlert()      { return s_wakeOnAlert; }
bool     buzz()             { return s_buzzMode != 0; }
bool     steadyPower()      { return s_steady; }
// Only while POWER SAVER is on. Either RADIO DUTY row (the Power screen,
// or WATCH in settings) picks the mode; neither turns the saver on.
uint8_t  radioDuty()        { return s_powerSaver ? s_radioDutyIx : 0; }
uint8_t  radioDutyRaw()     { return s_radioDutyIx; }
const char* radioDutyName(uint8_t ix) { return RADIO_DUTY_NAMES[ix < RADIO_DUTY_N ? ix : 0]; }
void cycleRadioDuty() {
    s_radioDutyIx = (uint8_t)((s_radioDutyIx + 1) % RADIO_DUTY_N);
    s_prefs.putUChar("pwrRadio", s_radioDutyIx);
}

uint16_t screenTimeoutSecRaw() { return SCREEN_TIMEOUTS[s_scrTimeoutIx]; }
uint8_t  idleFpsRaw()          { return IDLE_FPS[s_idleFpsIx]; }
uint16_t cpuMhzRaw()           { return CPU_MHZ[s_cpuIx]; }
uint8_t  bleListen()           { return s_powerSaver ? BLE_LISTEN[s_bleIx] : 75; }
uint8_t  bleListenRaw()        { return BLE_LISTEN[s_bleIx]; }
void cycleBleListen() {
    s_bleIx = (uint8_t)((s_bleIx + 1) % 3);
    s_prefs.putUChar("bleWin", s_bleIx);
}
uint16_t idleCpuMhz()          { return s_powerSaver ? IDLE_CPU[s_idleCpuIx] : 240; }
uint16_t idleCpuMhzRaw()       { return IDLE_CPU[s_idleCpuIx]; }
void cycleIdleCpu() {
    s_idleCpuIx = (uint8_t)((s_idleCpuIx + 1) % 3);
    s_prefs.putUChar("idleCpu", s_idleCpuIx);
}

void togglePowerSaver() {
    s_powerSaver = !s_powerSaver;
    s_prefs.putBool("pwrOn", s_powerSaver);
}
void cycleScreenTimeout() {
    s_scrTimeoutIx = (uint8_t)((s_scrTimeoutIx + 1) % SCREEN_TIMEOUTS_N);
    s_prefs.putUChar("pwrScrnT", s_scrTimeoutIx);
}
void adjustDimLevel(int8_t delta) {
    int v = (int)s_dimLevel + delta;
    if (v < 0)   v = 0;
    if (v > 128) v = 128;          // past half brightness it is not dim any more
    s_dimLevel = (uint8_t)v;
    s_prefs.putUChar("pwrDim", s_dimLevel);
}
void cycleIdleFps() {
    s_idleFpsIx = (uint8_t)((s_idleFpsIx + 1) % IDLE_FPS_N);
    s_prefs.putUChar("pwrFps", s_idleFpsIx);
}
void cycleIdleAfter() {
    s_idleAfterIx = (uint8_t)((s_idleAfterIx + 1) % IDLE_AFTER_N);
    s_prefs.putUChar("pwrIdleT", s_idleAfterIx);
}
void cycleCpuMhz() {
    s_cpuIx = (uint8_t)((s_cpuIx + 1) % CPU_MHZ_N);
    s_prefs.putUChar("pwrCpu", s_cpuIx);
}
void toggleSteadyPower() {
    s_steady = !s_steady;
    s_prefs.putBool("steady", s_steady);
}

uint8_t buzzStrength() { return s_buzzMode == 0 ? 0 : (uint8_t)(3 - s_buzzMode); }
const char* buzzModeName() {
    static const char* const N[] = { "OFF", "HIGH", "MED", "LOW" };
    return N[s_buzzMode < 4 ? s_buzzMode : 2];
}
void cycleBuzz() {
    s_buzzMode = (uint8_t)((s_buzzMode + 1) % 4);
    s_prefs.putUChar("buzzMode", s_buzzMode);
}
void toggleWakeOnAlert() {
    s_wakeOnAlert = !s_wakeOnAlert;
    s_prefs.putBool("pwrWake", s_wakeOnAlert);
}
bool buzzerOn()     { return s_buzzer; }
void toggleBuzzer() { s_buzzer = !s_buzzer; s_prefs.putBool("buzzer", s_buzzer); }

// ---- easter-egg hunt progress ----------------------------------------
// Packed into one NVS entry rather than one each: the store has a few
// hundred entries free and this is not where they should go.
static uint32_t s_hunt = 0;

uint8_t huntProgress(Hunt h) {
    const uint8_t i = (uint8_t)h;
    if (i >= (uint8_t)Hunt::COUNT) return 0;
    return (uint8_t)((s_hunt >> (8 * i)) & 0xFFu);
}

void setHuntProgress(Hunt h, uint8_t v) {
    const uint8_t i = (uint8_t)h;
    if (i >= (uint8_t)Hunt::COUNT) return;
    const uint32_t next = (s_hunt & ~(0xFFu << (8 * i))) | ((uint32_t)v << (8 * i));
    if (next == s_hunt) return;            // nothing changed, nothing written
    s_hunt = next;
    s_prefs.putUInt("hunt", s_hunt);
}

void load() {
    s_prefs.begin("settings", false);
    s_hunt       = s_prefs.getUInt("hunt", 0);
    s_palette    = (uint8_t)s_prefs.getUChar("pal", 0);
    if (s_palette >= Theme::PALETTE_COUNT) s_palette = 0;
    // SYNTHWAVE is the default a fresh device comes up on -- it is the
    // scene the boot splash already uses, so first power-on flows from
    // the splash into the same sunset instead of switching to something
    // else the moment onboarding ends. Only affects installs with no
    // saved value; anyone who has ever picked a background keeps theirs.
    s_background = (Background)s_prefs.getUChar("bg", (uint8_t)Background::SYNTHWAVE);
    if ((uint8_t)s_background >= BACKGROUND_COUNT) s_background = Background::DIGITAL;
    s_inverted   = s_prefs.getBool("inv", false);
    s_rgbSwapped = s_prefs.getBool("rgbswap", false);
    s_colorChecked = s_prefs.getBool("colorchk", false);
    // 120 and 70: chosen by eye on two boards the day the frame rate doubled
    // (2026-09-20), against 62 and 100, and kept.
    s_mascotPace   = (uint16_t)s_prefs.getUInt("pace", 120);
    s_mascotTempo  = s_prefs.getUChar("tempo", 70);
    if (s_mascotPace < 10 || s_mascotPace > 500) s_mascotPace = 120;
    if (s_mascotTempo < 50 || s_mascotTempo > 200) s_mascotTempo = 70;
#if SQUACH_MESH
    // Both off unless asked for. See the note in settings.h.
    s_meshDetect   = s_prefs.getBool("meshrx", false);
    s_meshCrowd    = s_prefs.getUChar("crowd", 1);
    if (s_meshCrowd < 1 || s_meshCrowd > CROWD_MAX) s_meshCrowd = 1;
    // Two draws exactly what one does -- see cycleMeshCrowd(). An older build
    // could not have stored it, but a hand-edited NVS can, and a setting that
    // does nothing is worse than one that moved.
    if (s_meshCrowd == 2) s_meshCrowd = 3;
    s_deskSquad     = s_prefs.getBool("crwdesk", false);
    // Follows the main screen's until the desk has one of its own: the two
    // were one setting, and a board upgraded mid-use should look the same.
    s_deskCrowd     = s_prefs.getUChar("dskcrwd", s_meshCrowd);
    if (s_deskCrowd < 1 || s_deskCrowd > CROWD_MAX || s_deskCrowd == 2) s_deskCrowd = s_meshCrowd;
    s_deskFullVisit = s_prefs.getBool("dskvisit", false);
    s_meshTransmit = s_prefs.getBool("meshtx", false);
    s_meshConsent  = s_prefs.getBool("meshok", false);
    s_phoneQwerty  = s_prefs.getBool("qwerty", false);
    s_messagesOn   = s_prefs.getBool("msgon", false);
    s_msgTutor     = s_prefs.getBool("msgtut", false);
#endif
    s_infoPrimerShown = s_prefs.getBool("infoprimer", false);
    s_rotationLocked = s_prefs.getBool("rotlock", DEFAULT_ROTATION_LOCK);
    s_topHat         = s_prefs.getBool("tophat", true);
    s_rotation = s_prefs.getUChar("rot", DEFAULT_ROTATION);
    if (s_rotation > 3) s_rotation = DEFAULT_ROTATION;
    s_backgroundLocked = s_prefs.getBool("bglock", false);
    s_deskBg = s_prefs.getUChar("deskBg", 255);
    s_clockFont     = s_prefs.getUChar("clkfont", 0);
    s_clockSize     = s_prefs.getUChar("clksize", 1);
    s_clockBackdrop = s_prefs.getUChar("clkbg", 0);
    if (s_clockFont > 1)     s_clockFont = 0;
    if (s_clockSize > 2)     s_clockSize = 1;
    if (s_clockBackdrop >= CLOCK_BG_N) s_clockBackdrop = 0;
    if (s_deskBg != 255 && s_deskBg >= BACKGROUND_COUNT) s_deskBg = 255;
    s_brightness = s_prefs.getUChar("bri", 255);
    if (s_brightness < 32) s_brightness = 32;
    s_autoQuiet  = s_prefs.getUChar("autoquiet", 0);
    if (s_autoQuiet != 0 && s_autoQuiet != 5 && s_autoQuiet != 10) s_autoQuiet = 0;
    s_minConf    = (Confidence)s_prefs.getUChar("conf", (uint8_t)Confidence::LOW_CONF);
    if ((uint8_t)s_minConf > (uint8_t)Confidence::HIGH_CONF) s_minConf = Confidence::LOW_CONF;
    s_boringMode = s_prefs.getBool("boring", false);
    // BLACK only exists while boring mode does. A board that saved it and
    // then had boring mode turned off -- or one restored from someone
    // else's settings -- would otherwise boot to a flat screen with the
    // option missing from the ring, and no way to cycle out of it.
    if (!s_boringMode && s_background == Background::BLACK) {
        s_background = Background::DIGITAL;
    }
    // Same problem, permanent version: TUNNEL is gone from the ring, so a
    // board that saved it would boot to an unpainted band with no way to
    // cycle out. Moved to the default rather than to DIGITAL -- it is what a
    // fresh device shows, and the two look nothing alike.
    if (s_background == Background::TUNNEL) s_background = Background::SYNTHWAVE;
    s_powerSaver   = s_prefs.getBool("pwrOn", DEFAULT_POWER_SAVER);
#if defined(TWATCH_S3)
    // Once per watch: POWER SAVER on. The default only reaches a watch that
    // never saved the switch, and every watch that went through the bench
    // builds had it saved OFF -- the radios then never rested and a night on
    // the wrist lasted 2.2 hours. After this one pass, OFF sticks as usual.
    if (!s_prefs.getBool("pwrWatch1", false)) {
        s_powerSaver = true;
        s_prefs.putBool("pwrOn", true);
        s_prefs.putBool("pwrWatch1", true);
    }
#endif
    s_scrTimeoutIx = s_prefs.getUChar("pwrScrnT", 2);
    s_dimLevel     = s_prefs.getUChar("pwrDim", 16);
    s_idleFpsIx    = s_prefs.getUChar("pwrFps", 2);
    s_idleAfterIx  = s_prefs.getUChar("pwrIdleT", 1);
    s_cpuIx        = s_prefs.getUChar("pwrCpu", 0);
    s_bleIx        = s_prefs.getUChar("bleWin", BLE_LISTEN_DEFAULT);
    s_idleCpuIx    = s_prefs.getUChar("idleCpu", IDLE_CPU_DEFAULT);
    if (s_bleIx > 2)     s_bleIx = BLE_LISTEN_DEFAULT;
    if (s_idleCpuIx > 2) s_idleCpuIx = IDLE_CPU_DEFAULT;
    s_wakeOnAlert  = s_prefs.getBool("pwrWake", true);
    // The old on/off switch carries over: a watch that had BUZZ off stays off.
    s_buzzMode     = s_prefs.getUChar("buzzMode", s_prefs.getBool("buzz", true) ? 2 : 0);
    if (s_buzzMode > 3) s_buzzMode = 2;
    s_steady       = s_prefs.getBool("steady", false);
    s_radioDutyIx  = s_prefs.getUChar("pwrRadio", RADIO_DUTY_DEFAULT);
    if (s_radioDutyIx >= RADIO_DUTY_N) s_radioDutyIx = RADIO_DUTY_DEFAULT;
    // The T-Watch's BUZZ (haptics on an alert) already owns "buzz", and with
    // the opposite default, so the CrowPanel's buzzer keeps its own key.
    s_buzzer       = s_prefs.getBool("buzzer", false);
    s_lightOn      = s_prefs.getBool("ltOn", true);
    s_lightAlerts  = s_prefs.getBool("ltAlert", true);
    s_lightMsgs    = s_prefs.getBool("ltMsg", true);
    s_lightIdle    = s_prefs.getUChar("ltIdle", 1);
    s_banter       = s_prefs.getUChar("banter", 2);
    if (s_banter > 3) s_banter = 2;
    s_lightColor   = s_prefs.getUChar("ltColor", 0);
    s_lightBright  = s_prefs.getUChar("ltBright", 2);
    // On by default since v1.7.7: a squad member can only ever make this board
    // install a signed release newer than the one it runs, with a countdown
    // and SKIP, and the trust is the phrase they already hold. Off is for
    // anyone who wants it.
    s_remoteUpdate = s_prefs.getBool("rmtUpd", true);
    s_phraseShown  = s_prefs.getBool("phrShow", true);
    s_updateCheck  = s_prefs.getBool("updChk", true);
    s_timeZone     = s_prefs.getUChar("tz", 10);
    s_tzChosen     = s_prefs.getBool("tzSet", false);
    if (s_timeZone >= Clock::zoneCount()) s_timeZone = 10;
    Clock::applyZone(s_timeZone);
    if (s_lightIdle > 2)                 s_lightIdle = 1;
    if (s_lightColor >= LIGHT_COLOR_N)   s_lightColor = 0;
    if (s_lightBright < 1 || s_lightBright > 5) s_lightBright = 2;
    // A saved index from a build with more steps than this one must not walk
    // off the end of the table.
    if (s_scrTimeoutIx >= SCREEN_TIMEOUTS_N) s_scrTimeoutIx = 2;
    if (s_idleFpsIx    >= IDLE_FPS_N)        s_idleFpsIx    = 2;
    if (s_idleAfterIx  >= IDLE_AFTER_N)      s_idleAfterIx  = 1;
    if (s_cpuIx        >= CPU_MHZ_N)         s_cpuIx        = 0;
    if (s_dimLevel     > 128)                s_dimLevel     = 16;

    // Every real type defaults ON except the ones in DEFAULT_OFF below.
    uint32_t allTypesOn = 0;
    for (uint8_t t = 1; t < (uint8_t)DetectionType::COUNT; t++) allTypesOn |= (1u << t);
    allTypesOn &= ~DEFAULT_OFF;
    s_typeMask = s_prefs.getUInt("typemask", allTypesOn);
    // A mask saved by an older build only has bits for the types that
    // existed then, so every type added since would come back OFF for
    // anyone who had ever touched the TYPE FILTER screen -- a new
    // detection silently disabled on upgrade, which is the worst way
    // for it to fail. "typecount" records how many types the saved mask
    // was written against; anything above that is a type the user has
    // never had the chance to express an opinion about, so it defaults
    // on like it would for a fresh install.
    s_sqSizeIx = (uint8_t)s_prefs.getUInt("sqsize", 1);
    if (s_sqSizeIx > 2) s_sqSizeIx = 2;

    uint8_t savedCount = (uint8_t)s_prefs.getUInt("typecount", 0);
    if (savedCount && savedCount < (uint8_t)DetectionType::COUNT) {
        for (uint8_t t = savedCount; t < (uint8_t)DetectionType::COUNT; t++) {
            if (DEFAULT_OFF & (1u << t)) continue;   // arrives off, like a fresh install
            s_typeMask |= (1u << t);
        }
        s_prefs.putUInt("typemask", s_typeMask);
        s_prefs.putUInt("typecount", (uint32_t)DetectionType::COUNT);
    }

    Theme::applyPalette(s_palette);
}

uint8_t paletteIndex() { return s_palette; }

void cyclePalette() {
    s_palette = (uint8_t)((s_palette + 1) % Theme::PALETTE_COUNT);
    s_prefs.putUChar("pal", s_palette);
    Theme::applyPalette(s_palette);
}

Background background() {
    if (s_deskActive && s_deskBg != 255 && backgroundSelectable((Background)s_deskBg)) return (Background)s_deskBg;
    return s_background;
}
void deskActive(bool on) {
    s_deskActive = on;
    // Written only on a change: this is called every time CLEAR is entered.
    if (s_prefs.getBool("deskOn", false) != on) s_prefs.putBool("deskOn", on);
}
bool deskWanted() { return s_prefs.getBool("deskOn", false); }
static void stepDeskBackground(int dir) {
    uint8_t b = (s_deskBg == 255) ? (uint8_t)s_background : s_deskBg;
    for (uint8_t i = 0; i < BACKGROUND_COUNT; i++) {
        b = (uint8_t)((b + BACKGROUND_COUNT + dir) % BACKGROUND_COUNT);
        if (backgroundSelectable((Background)b)) break;
    }
    s_deskBg = b;
    s_prefs.putUChar("deskBg", s_deskBg);
}
uint8_t     clockFont()         { return s_clockFont; }
const char* clockFontName()     { return s_clockFont ? "BANGERS" : "DIGITAL"; }
void        cycleClockFont()    { s_clockFont = (uint8_t)((s_clockFont + 1) % 2); s_prefs.putUChar("clkfont", s_clockFont); }
uint8_t     clockSize()         { return s_clockSize; }
const char* clockSizeName()     { static const char* const N[3] = { "SMALL", "MEDIUM", "LARGE" }; return N[s_clockSize]; }
void        cycleClockSize()    { s_clockSize = (uint8_t)((s_clockSize + 1) % 3); s_prefs.putUChar("clksize", s_clockSize); }
uint8_t     clockBackdrop()     { return s_clockBackdrop; }
const char* clockBackdropName() {
    static const char* const N[CLOCK_BG_N] = { "PLAIN", "RAIN", "SNOW", "TOASTERS", "FIRE", "STARFIELD", "FIREFLIES" };
    return N[s_clockBackdrop];
}
void        cycleClockBackdrop() { s_clockBackdrop = (uint8_t)((s_clockBackdrop + 1) % CLOCK_BG_N); s_prefs.putUChar("clkbg", s_clockBackdrop); }
void        cyclePrevClockBackdrop() { s_clockBackdrop = (uint8_t)((s_clockBackdrop + CLOCK_BG_N - 1) % CLOCK_BG_N); s_prefs.putUChar("clkbg", s_clockBackdrop); }

Background deskBackground() {
    if (s_deskBg != 255 && backgroundSelectable((Background)s_deskBg)) return (Background)s_deskBg;
    return s_background;
}
void cycleDeskBackground()     { stepDeskBackground(1); }
void cyclePrevDeskBackground() { stepDeskBackground(-1); }

bool backgroundSelectable(Background b) {
    // TUNNEL is retired and never selectable again -- see the note on the
    // enum. Everything else about it is deleted; only the number survives,
    // so that saved bytes keep meaning what they meant.
    if (b == Background::TUNNEL) return false;
    // BLACK is the only conditional one, and it is gated on boring mode
    // rather than hidden behind a second setting: somebody who has already
    // turned the mascot off is exactly the person who wants the option, and
    // nobody else would go looking for it.
    return (b != Background::BLACK) || s_boringMode;
}

// Both directions skip anything not currently selectable, so BLACK simply
// is not in the ring until boring mode puts it there. Bounded by
// BACKGROUND_COUNT rather than looping until it finds one: if a future
// change ever made everything unselectable this would spin forever, and a
// hung UI is a worse failure than a background that will not change.
bool previewBackground(Background b) {
    if ((uint8_t)b >= BACKGROUND_COUNT || !backgroundSelectable(b)) return false;
    s_background = b;
    return true;
}

void cycleBackground() {
    for (uint8_t i = 0; i < BACKGROUND_COUNT; i++) {
        s_background = (Background)(((uint8_t)s_background + 1) % BACKGROUND_COUNT);
        if (backgroundSelectable(s_background)) break;
    }
    s_prefs.putUChar("bg", (uint8_t)s_background);
}

void cyclePrevBackground() {
    for (uint8_t i = 0; i < BACKGROUND_COUNT; i++) {
        s_background = (Background)(((uint8_t)s_background + BACKGROUND_COUNT - 1) % BACKGROUND_COUNT);
        if (backgroundSelectable(s_background)) break;
    }
    s_prefs.putUChar("bg", (uint8_t)s_background);
}

bool inverted() { return s_inverted; }

void toggleInvert() {
    s_inverted = !s_inverted;
    s_prefs.putBool("inv", s_inverted);
}

bool rgbSwapped() { return s_rgbSwapped; }

void toggleRgbSwap() {
    s_rgbSwapped = !s_rgbSwapped;
    s_prefs.putBool("rgbswap", s_rgbSwapped);
}

bool colorChecked() { return s_colorChecked; }

void markColorChecked() {
    s_colorChecked = true;
    s_prefs.putBool("colorchk", true);
}

bool infoPrimerShown() { return s_infoPrimerShown; }

void markInfoPrimerShown() {
    s_infoPrimerShown = true;
    s_prefs.putBool("infoprimer", true);
}

bool topHatShown() { return s_topHat; }

void toggleTopHat() {
    s_topHat = !s_topHat;
    s_prefs.putBool("tophat", s_topHat);
}

bool rotationLocked() { return s_rotationLocked; }

void toggleRotationLock() {
    s_rotationLocked = !s_rotationLocked;
    s_prefs.putBool("rotlock", s_rotationLocked);
}

uint8_t rotation() { return s_rotation; }

void saveRotation(uint8_t r) {
    s_rotation = r;
    s_prefs.putUChar("rot", r);
}

bool backgroundLocked() { return s_backgroundLocked; }

void toggleBackgroundLocked() {
    s_backgroundLocked = !s_backgroundLocked;
    s_prefs.putBool("bglock", s_backgroundLocked);
}

bool        lightOn()         { return s_lightOn; }
bool        lightAlerts()     { return s_lightAlerts; }
bool        lightMessages()   { return s_lightMsgs; }
uint8_t     lightIdle()       { return s_lightIdle; }
uint8_t     lightColor()      { return s_lightColor; }
uint8_t     lightBrightness() { return s_lightBright; }
const char* lightIdleName()   { return LIGHT_IDLE_NAMES[s_lightIdle > 2 ? 1 : s_lightIdle]; }
const char* lightColorName()  { return LIGHT_COLOR_NAMES[s_lightColor < LIGHT_COLOR_N ? s_lightColor : 0]; }
void toggleLight()         { s_lightOn = !s_lightOn;         s_prefs.putBool("ltOn", s_lightOn); }
void toggleLightAlerts()   { s_lightAlerts = !s_lightAlerts; s_prefs.putBool("ltAlert", s_lightAlerts); }
void toggleLightMessages() { s_lightMsgs = !s_lightMsgs;     s_prefs.putBool("ltMsg", s_lightMsgs); }
void cycleLightIdle()      { s_lightIdle = (uint8_t)((s_lightIdle + 1) % 3);            s_prefs.putUChar("ltIdle", s_lightIdle); }
uint8_t     banter()       { return s_banter; }
const char* banterName()   { return BANTER_NAMES[s_banter > 3 ? 2 : s_banter]; }
float       banterScale()  { return BANTER_SCALE[s_banter > 3 ? 2 : s_banter]; }
// Quiet to loud: IMPORTANT, LESS, NORMAL, MORE, round again.
void cycleBanter()         { s_banter = (uint8_t)((s_banter + 1) % 4);                 s_prefs.putUChar("banter", s_banter); }
void cycleLightColor() {
    // THEME -> BACKGROUND -> RED ... WHITE -> THEME.
    if      (s_lightColor == 0)  s_lightColor = 10;
    else if (s_lightColor == 10) s_lightColor = 1;
    else if (s_lightColor >= 9)  s_lightColor = 0;
    else                         s_lightColor++;
    s_prefs.putUChar("ltColor", s_lightColor);
}
void cycleLightBrightness() { s_lightBright = (uint8_t)(s_lightBright % 5 + 1);          s_prefs.putUChar("ltBright", s_lightBright); }
bool remoteUpdate()         { return s_remoteUpdate; }
void toggleRemoteUpdate()   { s_remoteUpdate = !s_remoteUpdate; s_prefs.putBool("rmtUpd", s_remoteUpdate); }
bool phraseShown()          { return s_phraseShown; }
void togglePhraseShown()    { s_phraseShown = !s_phraseShown; s_prefs.putBool("phrShow", s_phraseShown); }
bool updateCheck()          { return s_updateCheck; }
void toggleUpdateCheck()    { s_updateCheck = !s_updateCheck; s_prefs.putBool("updChk", s_updateCheck); }
uint8_t     timeZone()      { return s_timeZone; }
const char* timeZoneName()  { return Clock::zoneName(s_timeZone); }
bool        timeZoneChosen(){ return s_tzChosen; }
void markTimeZoneChosen() {
    if (!s_tzChosen) { s_tzChosen = true; s_prefs.putBool("tzSet", true); }
}
void setTimeZone(uint8_t i) {
    if (i >= Clock::zoneCount()) return;
    s_timeZone = i;
    s_prefs.putUChar("tz", s_timeZone);
    markTimeZoneChosen();
    Clock::applyZone(s_timeZone);
}
// The arrows on the card and the SYSTEM row step without marking the zone
// chosen: only THIS IS RIGHT, the row's own tap, or the flasher does that.
void cycleTimeZone()     { stepTimeZone(1); markTimeZoneChosen(); }
void stepTimeZone(int dir) {
    s_timeZone = (uint8_t)((s_timeZone + Clock::zoneCount() + dir) % Clock::zoneCount());
    s_prefs.putUChar("tz", s_timeZone);
    Clock::applyZone(s_timeZone);
}

bool boringMode() { return s_boringMode; }

void toggleBoringMode() {
    s_boringMode = !s_boringMode;
    s_prefs.putBool("boring", s_boringMode);
    // Turning boring mode off strands anyone sitting on BLACK: the option
    // is gone from the ring, and without this they would be looking at a
    // flat screen with no way to cycle out of it.
    if (!s_boringMode && s_background == Background::BLACK) {
        s_background = Background::DIGITAL;
        s_prefs.putUChar("bg", (uint8_t)s_background);
    }
}

uint8_t brightness() { return s_brightness; }

void adjustBrightness(int8_t delta) {
    int16_t v = (int16_t)s_brightness + delta;
    if (v < 32) v = 32;
    if (v > 255) v = 255;
    s_brightness = (uint8_t)v;
    s_prefs.putUChar("bri", s_brightness);
}

Confidence minConfidence() { return s_minConf; }

// On the watch, POWER SAVER holds AUTO SNOOZE at five at most. Every alert
// lights the screen and buzzes, and a night of two Ring cameras coming back
// every two and a half minutes was about ninety of them: most of a battery.
#if defined(TWATCH_S3)
static bool autoQuietForced() { return s_powerSaver && (s_autoQuiet == 0 || s_autoQuiet > 5); }
uint8_t autoQuietAfter() { return autoQuietForced() ? 5 : s_autoQuiet; }
#else
uint8_t autoQuietAfter() { return s_autoQuiet; }
#endif

const char* autoQuietLabel() {
#if defined(TWATCH_S3)
    if (autoQuietForced()) return "SAVER 5";
#endif
    switch (s_autoQuiet) {
        case 5:  return "AFTER 5";
        case 10: return "AFTER 10";
        default: return "OFF";
    }
}

void cycleAutoQuiet() {
    s_autoQuiet = (s_autoQuiet == 0) ? 5 : (s_autoQuiet == 5 ? 10 : 0);
    s_prefs.putUChar("autoquiet", s_autoQuiet);
}

void cycleMinConfidence() {
    uint8_t next = (uint8_t)s_minConf + 1;
    if (next > (uint8_t)Confidence::HIGH_CONF) next = 0;
    s_minConf = (Confidence)next;
    s_prefs.putUChar("conf", next);
}

const char* minConfidenceLabel() {
    switch (s_minConf) {
        case Confidence::LOW_CONF:  return "ALL";
        case Confidence::MED_CONF:  return "MED+";
        case Confidence::HIGH_CONF: return "HIGH ONLY";
        default:                    return "?";
    }
}

// SMALL / MEDIUM / LARGE. LARGE is 100 and is the default, so a board that
// has never been told otherwise draws him exactly as it always did.
static const uint8_t     SQ_SIZE_PCT[3]   = { 70, 85, 100 };
static const char* const SQ_SIZE_LABEL[3] = { "SMALL", "MEDIUM", "LARGE" };
static const uint8_t     SQ_SIZE_N        = 3;

#if SQUACH_MESH
bool meshDetect()   { return s_meshDetect; }
bool meshTransmit() { return s_meshTransmit && s_meshConsent; }
bool meshConsent()  { return s_meshConsent; }
void setMeshConsent(bool v) {
    s_meshConsent = v;
    s_prefs.putBool("meshok", v);
}
const char* meshDetectLabel()   { return s_meshDetect   ? "ON" : "OFF"; }
// Reports what the RADIO is doing, not what the flag holds -- meshTransmit()
// is the same answer the advertiser gets, so the row cannot say ON while
// nothing is going out.
const char* meshTransmitLabel() { return meshTransmit() ? "ON" : "OFF"; }
uint8_t meshCrowd() { return s_meshCrowd; }
bool    deskSquad() { return s_deskSquad; }
void    toggleDeskSquad() {
    s_deskSquad = !s_deskSquad;
    s_prefs.putBool("crwdesk", s_deskSquad);
}
bool    deskFullVisit() { return s_deskFullVisit; }
void    toggleDeskFullVisit() {
    s_deskFullVisit = !s_deskFullVisit;
    s_prefs.putBool("dskvisit", s_deskFullVisit);
}
uint8_t deskCrowd() { return s_deskCrowd; }

static const char* crowdLabel(uint8_t n) {
    // ONE is a different thing, not a count of one: it is the ordinary visit,
    // with the set pieces and the emotes that a crowd stands down.
    if (n <= 1) return "ONE";
    static char b[10];
    snprintf(b, sizeof b, "UP TO %u", (unsigned)n);
    return b;
}
const char* meshCrowdLabel() { return crowdLabel(s_meshCrowd); }
const char* deskCrowdLabel() { return crowdLabel(s_deskCrowd); }

// Every number from one to eight, one per tap, wrapping -- except two. It used
// to offer only 1, 4 and 8 on the grounds that eight values would be eight
// taps, which is true, but how many to put on screen is a matter of taste and
// of screen size, and that is the owner's call rather than ours.
//
// TWO IS SKIPPED because it is the one number that would change nothing. The
// crowd needs two PEERS before it draws anything, so "up to 2" is one visitor
// -- which is the ordinary visit ONE already gives, and gives better: the
// arrivals, the high fives, the rock-paper-scissors and every other set piece
// written for exactly two Squachys, all of which a crowd stands down. A menu
// value that silently does nothing reads as a bug.
static uint8_t nextCrowd(uint8_t c) {
    uint8_t n = (uint8_t)(c >= CROWD_MAX ? 1 : c + 1);
    if (n == 2) n = 3;
    return n;
}
void cycleMeshCrowd() {
    s_meshCrowd = nextCrowd(s_meshCrowd);
    s_prefs.putUChar("crowd", s_meshCrowd);
}
void cycleDeskCrowd() {
    s_deskCrowd = nextCrowd(s_deskCrowd);
    s_prefs.putUChar("dskcrwd", s_deskCrowd);
}

void cycleMeshDetect()   { s_meshDetect   = !s_meshDetect;   s_prefs.putBool("meshrx", s_meshDetect); }
void cycleMeshTransmit() { s_meshTransmit = !s_meshTransmit; s_prefs.putBool("meshtx", s_meshTransmit); }

const char* meshSummary() {
    // Never bare "ON"/"OFF", and always with the arrow.
    //
    // The row opens a screen, but it looked like a switch: every toggle on
    // that list says ON or OFF, so a row saying ON reads as one you tap to
    // flip. The rows that DO open screens show data instead -- "14/14",
    // "3" -- because a number cannot be mistaken for a toggle state.
    //
    // RX/TX rather than DETECT/SEND for width: "SQUACHMESH" plus
    // "DETECT+SEND" collides with itself on the 240px portrait rotation at
    // this row's size-2 text, which is the same trap already documented on
    // TYPE FILTER and IGNORED.
    if (s_meshDetect && meshTransmit()) return "RX+TX >";
    if (s_meshDetect)                   return "RX >";
    if (meshTransmit())                 return "TX >";
    return "OFF >";
}

bool phoneQwerty() { return s_phoneQwerty; }
void togglePhoneQwerty() {
    s_phoneQwerty = !s_phoneQwerty;
    s_prefs.putBool("qwerty", s_phoneQwerty);
}
bool messagesOn() { return s_messagesOn; }
void toggleMessages() {
    s_messagesOn = !s_messagesOn;
    s_prefs.putBool("msgon", s_messagesOn);
}
bool meshTutorSeen()    { return s_msgTutor; }
void setMeshTutorSeen() { s_msgTutor = true; s_prefs.putBool("msgtut", true); }
#endif

uint16_t mascotPaceMs() { return s_mascotPace; }
void setMascotPaceMs(uint16_t ms) {
    s_mascotPace = ms < 10 ? 10 : (ms > 500 ? 500 : ms);
    s_prefs.putUInt("pace", s_mascotPace);
}
uint8_t mascotTempoPct() { return s_mascotTempo; }
void setMascotTempoPct(uint8_t pct) {
    s_mascotTempo = pct < 50 ? 50 : (pct > 200 ? 200 : pct);
    s_prefs.putUChar("tempo", s_mascotTempo);
}

uint8_t squachySizePct() {
    return SQ_SIZE_PCT[s_sqSizeIx < SQ_SIZE_N ? s_sqSizeIx : (uint8_t)(SQ_SIZE_N - 1)];
}
const char* squachySizeLabel() {
    return SQ_SIZE_LABEL[s_sqSizeIx < SQ_SIZE_N ? s_sqSizeIx : (uint8_t)(SQ_SIZE_N - 1)];
}
void cycleSquachySize() {
    s_sqSizeIx = (uint8_t)((s_sqSizeIx + 1) % SQ_SIZE_N);
    s_prefs.putUInt("sqsize", s_sqSizeIx);
}

bool typeEnabled(DetectionType t) {
    uint8_t idx = (uint8_t)t;
    if (idx == 0 || idx >= (uint8_t)DetectionType::COUNT) return true;  // UNKNOWN, or out of range -- never gated
    return (s_typeMask & (1u << idx)) != 0;
}

void toggleType(DetectionType t) {
    uint8_t idx = (uint8_t)t;
    if (idx == 0 || idx >= (uint8_t)DetectionType::COUNT) return;
    s_typeMask ^= (1u << idx);
    s_prefs.putUInt("typemask", s_typeMask);
    // Stamped alongside the mask so a later firmware can tell which
    // types this mask was written against -- see load()'s upgrade path.
    s_prefs.putUInt("typecount", (uint32_t)DetectionType::COUNT);
}

uint8_t enabledTypeCount() {
    uint8_t n = 0;
    for (uint8_t t = 1; t < (uint8_t)DetectionType::COUNT; t++) {
        if (s_typeMask & (1u << t)) n++;
    }
    return n;
}

}
