// SquachWatch-Sim — WebAssembly harness.
//
// Same job as main_live.cpp: compile the firmware's real main.cpp and run
// its actual setup()/loop(). The only difference is transport. Native
// builds talk over a stdin/stdout pipe to a Python server; here there is
// no pipe and no server, so the same operations become plain exported
// functions the page calls directly.
//
// That makes the whole emulator a static file. No process per visitor, no
// frame streaming over a socket -- the browser owns the device.
//
// This compiles the same sources the native emulator does. Everything
// platform-specific was already isolated behind the shim headers in this
// directory (Arduino.h, TFT_eSPI.h, Preferences.h, SPI.h, Wire.h), which
// is what makes the port small: there are no threads, no sockets and no
// filesystem to emulate, and the framebuffer was already a plain array.
#include <emscripten/emscripten.h>

// ---- NVS backing store ------------------------------------------------
// Defined here rather than in Preferences.h because EM_JS emits symbols
// into every translation unit that sees it, and that header is included
// all over the UI sources. Declarations live there; these are the only
// definitions.
//
// The buffer is passed in from C++ and the byte length returned, rather
// than handing back a malloc'd pointer -- ownership stays on one side of
// the boundary and nothing depends on _malloc being exported. Returns -1
// for "no such key", otherwise the length, which may exceed cap (the
// caller resizes and asks again).
EM_JS(int, squachsim_nvs_read, (const char* key, char* buf, int cap), {
    var v = null;
    try { v = localStorage.getItem(UTF8ToString(key)); } catch (e) { v = null; }
    if (v === null || v === undefined) return -1;
    var n = lengthBytesUTF8(v);
    if (n + 1 <= cap) stringToUTF8(v, buf, cap);
    return n;
});

// Quota errors and private-mode refusals are swallowed: failing to
// persist a setting is not worth taking the emulator down over.
EM_JS(void, squachsim_nvs_write, (const char* key, const char* val), {
    try { localStorage.setItem(UTF8ToString(key), UTF8ToString(val)); } catch (e) {}
});

#include <cstdint>
#include <cstring>
#include <vector>

#include <TFT_eSPI.h>
#include "state.h"
#include "detection.h"
#include "sim_touch.h"
#include "sim_detections.h"
#include "meshsim.h"
#include "squachy.h"
#include "frame_push.h"
#include "blackbox.h"
#include "clock.h"
#include "settings.h"
#if defined(TDECK_PLUS)
#include "lora_sim.h"
#endif

// Defined by the firmware's main.cpp, which this target compiles. Declared
// here, outside the extern "C" block below, so they link as the C++ symbols
// main.cpp defines.
void setup();
void loop();
void simResizeFrame(int w, int h);
extern TFT_eSPI tft;
extern AppState state;
extern uint8_t  screenRotation;
extern DetectionEngine engine;
extern volatile bool g_consoleRotate;
#if SQUACHSIM_BOOT
void simBootPress();
#endif
#if defined(TDECK_PLUS)
void simKeyPress(char c);
#endif
#if defined(TWATCH_S3)
#include "XPowersLib.h"
#endif
#if defined(CARDPUTER_ADV)
void simCardKey(int what, bool down);
#endif

// pollTouch()'s XPT2046 defaults. Static inside main.cpp, but the
// Preferences shim starts empty in the browser so loadOrDefaultCal()
// never finds a stored calibration and these are exactly what it uses.
#if defined(TDECK_PLUS)
// The T-Deck build reads a capacitive chip (sim/gt911_sim.cpp), so the fit is
// built from main.cpp's CAP_N* defaults instead; same mapping, other numbers.
static const long RAW_X_MIN = 32, RAW_X_MAX = 166;
static const long RAW_Y_MIN = 10, RAW_Y_MAX = 308;
#else
static const long RAW_X_MIN = 200, RAW_X_MAX = 3800;
static const long RAW_Y_MIN = 200, RAW_Y_MAX = 3800;
#endif

static long imap(long x, long a, long b, long c, long d) {
    return (x - a) * (d - c) / (b - a) + c;
}

// Screen coordinates -> raw driver values, inverting pollTouch()'s own
// mapping so the firmware's rotation/axis-swap/clamp logic is what runs.
// Identical to the native harness; see main_live.cpp for why this mirrors
// firmware code rather than calling it.
static void screenToRaw(int sx, int sy, uint16_t& rawX, uint16_t& rawY) {
    const int w = tft.width(), h = tft.height();
    const bool landscape = (screenRotation % 2) == 1;
    const bool flipped   = screenRotation >= 2;
    long px, py;
    if (!landscape) {
        px = flipped ? imap(sx, w, 0, RAW_X_MIN, RAW_X_MAX) : imap(sx, 0, w, RAW_X_MIN, RAW_X_MAX);
        py = flipped ? imap(sy, h, 0, RAW_Y_MIN, RAW_Y_MAX) : imap(sy, 0, h, RAW_Y_MIN, RAW_Y_MAX);
    } else {
        py = flipped ? imap(sx, w, 0, RAW_Y_MIN, RAW_Y_MAX) : imap(sx, 0, w, RAW_Y_MIN, RAW_Y_MAX);
        px = flipped ? imap(sy, 0, h, RAW_X_MIN, RAW_X_MAX) : imap(sy, h, 0, RAW_X_MIN, RAW_X_MAX);
    }
    if (px < 0) px = 0;
    if (px > 4095) px = 4095;
    if (py < 0) py = 0;
    if (py > 4095) py = 4095;
    rawX = (uint16_t)px;
    rawY = (uint16_t)py;
}

// RGBA for the canvas. Kept as a module-level buffer whose address is
// handed to JS once: the page wraps it in a Uint8ClampedArray view over
// the WASM heap, so a frame costs no copy and no allocation.
static std::vector<uint8_t> g_rgba;

static const uint32_t STEP_MS = 33;   // ~= the device's real loop rate

extern "C" {

EMSCRIPTEN_KEEPALIVE void sw_setup() {
    SimClock::virtualTime = true;
#if defined(TDECK_PLUS)
    // The made-up LoRa band (sim/lora_sim.cpp), laid out from the boot on:
    // a packet every five or six seconds, Meshtastic and MeshCore in turn.
    SimLora::begin(SimLora::Config());
#endif
    setup();
    g_rgba.assign((size_t)tft.width() * tft.height() * 4, 0);
}

EMSCRIPTEN_KEEPALIVE int sw_width()  { return tft.width(); }
EMSCRIPTEN_KEEPALIVE int sw_height() { return tft.height(); }

// One loop() iteration, advancing virtual time by the device's frame
// interval. The page drives this from requestAnimationFrame.
EMSCRIPTEN_KEEPALIVE void sw_step(int n) {
    if (n < 1) n = 1;
    for (int i = 0; i < n; i++) {
        SimClock::nowMs += STEP_MS;
#if defined(TDECK_PLUS)
        SimLora::step(millis());
#endif
        loop();
    }
}

// One loop() iteration that moves the clock on by the REAL time since the
// last one, for a host that cannot keep up with the device's 33 fps (a
// slow phone manages about 22): the board's animations already scale to
// elapsed time, so this keeps Squachy at the board's speed, just with fewer
// frames. Capped so a backgrounded page does not come back a minute ahead.
EMSCRIPTEN_KEEPALIVE void sw_step_ms(int ms) {
    if (ms < 1) ms = 1;
    if (ms > 100) ms = 100;
    SimClock::nowMs += (uint32_t)ms;
    loop();
}

// Converts the sprite's RGB565 to RGBA and returns the buffer address.
// Sized on every call because a rotation changes the geometry.
EMSCRIPTEN_KEEPALIVE uint8_t* sw_frame() {
    const int w = tft.width(), h = tft.height();
    const size_t need = (size_t)w * h * 4;
    if (g_rgba.size() != need) g_rgba.assign(need, 0);
    const std::vector<uint16_t>& src = tft.pixelsRGB565();
    size_t o = 0;
    for (uint16_t v : src) {
        const uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        g_rgba[o++] = (uint8_t)((r << 3) | (r >> 2));
        g_rgba[o++] = (uint8_t)((g << 2) | (g >> 4));
        g_rgba[o++] = (uint8_t)((b << 3) | (b >> 2));
        g_rgba[o++] = 255;
        if (o >= need) break;
    }
    return g_rgba.data();
}

// Touch, injected in raw driver space exactly as the native harness does.
EMSCRIPTEN_KEEPALIVE void sw_touch(int down, int x, int y) {
    if (down) {
        screenToRaw(x, y, SimTouch::rawX, SimTouch::rawY);
        SimTouch::down = true;
    } else {
        SimTouch::down = false;
    }
}

// Hand the page a synthetic detection. The emulator has no radios, so
// this is the only way a visitor sees the alert path at all -- and on a
// demo it is better than waiting: a Trigger button summons an AirTag
// instead of leaving someone staring at ALL CLEAR.
//
// By DEVICE: `profile` indexes sim_detections.h's kSimProfiles, which has one
// entry per device the firmware can name -- see sw_det_catalog().
EMSCRIPTEN_KEEPALIVE int sw_detect(int profile, int rssi) {
    if (profile < 0 || (size_t)profile >= kSimProfileCount) return 0;
    static uint16_t serial = 0;
    Detection d;
    simMakeDetection(d, kSimProfiles[profile], millis(), rssi, serial++);
    engine.postBle(d);
    return 1;
}

// A catch the phone app kept, into the LOG without an alert: the app replays
// its saved catches when the emulator starts, so the band's LOG is the phone's
// history rather than whatever arrived since. `tail` is the last three bytes of
// the address, which is all a heads-up carries; the profile supplies the rest.
EMSCRIPTEN_KEEPALIVE int sw_log_catch(int profile, int rssi, int tail, double epoch,
                                      int hasPos, double lat, double lon) {
    if (profile < 0 || (size_t)profile >= kSimProfileCount) return 0;
    Detection d;
    simMakeDetection(d, kSimProfiles[profile], millis(), rssi, 0);
    d.mac[3] = (uint8_t)(tail >> 16); d.mac[4] = (uint8_t)(tail >> 8); d.mac[5] = (uint8_t)tail;
    BlackBox::noteDetectionAt(d, false, (uint32_t)epoch, hasPos != 0,
                              (int32_t)(lat * 1e7), (int32_t)(lon * 1e7));
    return 1;
}

// The phone's clock and zone (an index into clock.cpp's ZONES, or -1), so the
// emulator's times -- the LOG's, the desk clock's -- are the phone's.
EMSCRIPTEN_KEEPALIVE void sw_set_time(double epoch, int zone) {
    Clock::setEpoch((uint32_t)epoch);
    if (zone >= 0) Settings::setTimeZone((uint8_t)zone);
}

// [[index, "TYPE", "label"], ...] -- the page's picker, from the same table.
EMSCRIPTEN_KEEPALIVE const char* sw_det_catalog() { return simProfileCatalog(); }

// The virtual SquachMesh peer (meshsim.h). The same command language the
// native harness's P line takes, so the two emulators cannot disagree about
// what "outfit 12" means. Strings in, JSON out.
EMSCRIPTEN_KEEPALIVE int         sw_mesh(const char* cmd) { return MeshSim::command(cmd) ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE const char* sw_mesh_status()        { return MeshSim::status(); }
EMSCRIPTEN_KEEPALIVE const char* sw_mesh_catalog()       { return MeshSim::catalog(); }

// For a host that is not a 320x240 board -- the phone app. Call before
// sw_setup(): `w` x `h` is the panel's landscape shape (w >= h), and from
// then on the rotate icon swaps the two sides the way a real panel does.
// Every screen already composes itself against tft.width()/height(), the
// same property the native emulator's --size leans on. The web page never
// calls this, so it keeps the board's own 320x240 and its fixed landscape.
EMSCRIPTEN_KEEPALIVE void sw_panel(int w, int h) {
    if (w < h) { const int t = w; w = h; h = t; }
    if (h >= 64 && w <= 1024) tft = TFT_eSPI(w, h);
    TFT_eSPI::rotates = true;
}

// The firmware's own rotate path (the console ROTATE command), so the
// frame buffer, the saved rotation and Squachy's reaction all happen as
// they do for a tap on the icon.
EMSCRIPTEN_KEEPALIVE void sw_rotate() { g_consoleRotate = true; }

// The BOOT button: one short press, as a board's bootButtonTick() reports it
// -- POCKET MODE on, or off. The page draws the board's button and calls this.
#if SQUACHSIM_BOOT
EMSCRIPTEN_KEEPALIVE void sw_boot() { simBootPress(); }
#elif defined(TWATCH_S3)
// The watch's crown: a short press, raised as the AXP2101's interrupt
// (sim/XPowersLib.h), which the firmware's own crown poll picks up.
EMSCRIPTEN_KEEPALIVE void sw_boot() { SimPmu::crown = true; }
#endif

#if defined(CARDPUTER_ADV)
// The Cardputer's keyboard, as the firmware reads it (smallKeysRead): 0 ENTER
// (down and up: it presses where the cursor is, for as long as it is held),
// 1 next stop (down, right, TAB), 2 previous (up, left), 3 home (ESC, DEL).
EMSCRIPTEN_KEEPALIVE void sw_cardkey(int what, int down) { simCardKey(what, down != 0); }
#endif

#if defined(TDECK_PLUS)
// The T-Deck's keyboard: one key, as its helper chip hands it over -- a
// character, a newline for enter, a backspace for backspace. Goes to whichever text
// screen is up (the LORA page, the chats, the WiFi password), the way the
// board's own keyboard does; elsewhere it is dropped, and enter on the home
// screen opens the LORA page.
EMSCRIPTEN_KEEPALIVE void sw_key(int c) { if (c > 0 && c < 256) simKeyPress((char)c); }
#endif

// The backlight's last duty, 0-255 (-1: not set yet), so the page can show
// POCKET MODE, the power saver's dim and the BRIGHT setting on the canvas.
EMSCRIPTEN_KEEPALIVE int sw_backlight() { return g_simBacklight; }

// A new panel while running, `w` x `h` AS SHOWN: the phone app's band is the
// board's own screen upright (240x320), its SQUACHY tab is phone-shaped. A
// board could never do this (the frame buffer is allocated once, and only a
// rotation's same-count reshape is ever asked of it); the sim's sprite
// re-buffers on a resize, so here it is one assignment and a reshape. The
// rotation is set to match the shape, odd being landscape on these boards,
// and is not saved: it is the host's choice, not the person's.
EMSCRIPTEN_KEEPALIVE void sw_resize(int w, int h) {
    if (w < 64 || h < 64 || w > 1024 || h > 1024) return;
    const bool land = w >= h;
    tft = TFT_eSPI(land ? w : h, land ? h : w);
    TFT_eSPI::rotates = true;
    screenRotation = land ? 1 : 0;
    tft.setRotation(screenRotation);
    simResizeFrame(tft.width(), tft.height());
    FramePush::invalidate();
}

// A line from the host, said once in his bubble: the phone app's status and
// setup lines ("Let me use Bluetooth?"). Copied, because say() keeps the
// pointer for as long as the bubble is up and JS strings do not outlive the
// call.
EMSCRIPTEN_KEEPALIVE void sw_say(const char* text) {
    static char line[96];
    if (!text) return;
    size_t i = 0;
    for (; i < sizeof line - 1 && text[i]; i++) line[i] = text[i];
    line[i] = 0;
    Squachy::announce(line);
}

EMSCRIPTEN_KEEPALIVE int sw_state()    { return (int)state; }
EMSCRIPTEN_KEEPALIVE int sw_rotation() { return (int)screenRotation; }

}  // extern "C"
