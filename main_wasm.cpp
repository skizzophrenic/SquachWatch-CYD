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

// Defined by the firmware's main.cpp, which this target compiles.
void setup();
void loop();
extern TFT_eSPI tft;
extern AppState state;
extern uint8_t  screenRotation;
extern DetectionEngine engine;

// pollTouch()'s XPT2046 defaults. Static inside main.cpp, but the
// Preferences shim starts empty in the browser so loadOrDefaultCal()
// never finds a stored calibration and these are exactly what it uses.
static const long RAW_X_MIN = 200, RAW_X_MAX = 3800;
static const long RAW_Y_MIN = 200, RAW_Y_MAX = 3800;

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
        loop();
    }
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
EMSCRIPTEN_KEEPALIVE int sw_detect(int type, int rssi) {
    if (type <= 0 || type >= (int)DetectionType::COUNT) return 0;
    static uint16_t serial = 0;
    Detection d;
    if (!simMakeDetection(d, (DetectionType)type, millis(), rssi, serial++)) return 0;
    engine.postBle(d);
    return 1;
}

EMSCRIPTEN_KEEPALIVE int sw_state()    { return (int)state; }
EMSCRIPTEN_KEEPALIVE int sw_rotation() { return (int)screenRotation; }

}  // extern "C"
