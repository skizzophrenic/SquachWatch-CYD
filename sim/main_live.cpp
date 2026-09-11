// SquachWatch-Sim — interactive harness.
//
// Unlike squachsim (which calls one screen's Tick() directly and exits),
// this compiles the firmware's *actual* main.cpp and runs its real
// setup()/loop(). Every screen transition, gesture threshold, debounce
// timer and long-press rule is the firmware's own -- nothing about
// navigation is reimplemented here, so it can't drift.
//
// Protocol: line commands on stdin, raw RGB888 frames on stdout.
// (Serial output from the firmware goes to stderr -- see Arduino.h --
// so it can never corrupt the frame stream.)
//
//   D <x> <y>   press down at screen coords, and hold
//   M <x> <y>   move while held
//   U           release
//   S [n]       step n loop() iterations (default 1), then emit a frame
//   T <n|type>  inject a synthetic detection: a profile index from Y, or a
//               type name for that type's first profile ("T AIRTAG")
//   Y           every profile: one "DETS <json>" line on stdout
//   R           report current AppState on stderr
//   P <cmd>     the virtual SquachMesh peer -- see meshsim.h for <cmd>.
//               (P for peer: M was already touch-move.)
//   K           the peer's pickers: one "CAT <json>" line on stdout
//   Q           quit
//
// Each emitted frame is:  "FRM <w> <h> <bytes> <state> <mesh>\n" followed by
// <bytes> of RGB888, row-major. The state rides along in the header
// rather than being asked for separately, so a caller never has to
// correlate a stdout frame with a stderr reply -- they'd race, and the
// firmware's own Serial output shares that stderr channel.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>   // strcasecmp, for the T command name lookup
#include <string>
#include <vector>

#include <TFT_eSPI.h>
#include "state.h"
#include "detection.h"
#include "sim_touch.h"
#include "sim_detections.h"
#include "meshsim.h"

// Defined by the firmware's main.cpp, which this target compiles.
void setup();
void loop();
extern TFT_eSPI tft;
extern AppState state;
extern uint8_t  screenRotation;
extern DetectionEngine engine;

// pollTouch()'s XPT2046 defaults. These are `static` inside main.cpp so
// they can't be read from here -- but the sim's Preferences shim starts
// empty, so loadOrDefaultCal() never finds a saved calibration and
// these factory defaults are exactly what it uses.
static const long RAW_X_MIN = 200, RAW_X_MAX = 3800;
static const long RAW_Y_MIN = 200, RAW_Y_MAX = 3800;

static long imap(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Screen coordinates -> raw driver values, by inverting the mapping in
// pollTouch()'s XPT2046 branch. map() inverts cleanly: if
// out = map(in, a, b, c, d) then in = map(out, c, d, a, b).
//
// This mirrors firmware code rather than calling it, because pollTouch()
// only runs in the read direction. If that mapping ever changes, taps
// land visibly in the wrong place -- a loud failure, not a silent one.
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

static const char* stateName(AppState s) {
    switch (s) {
        case AppState::BOOT: return "BOOT";
        case AppState::CLEAR: return "CLEAR";
        case AppState::ALERT: return "ALERT";
        case AppState::LOG: return "LOG";
        case AppState::SETTINGS: return "SETTINGS";
        case AppState::DIARY: return "DIARY";
        case AppState::OUTFIT: return "OUTFIT";
        case AppState::RAWSCAN: return "RAWSCAN";
        case AppState::WATCH_ALERT: return "WATCH_ALERT";
        case AppState::DIAGNOSTICS: return "DIAGNOSTICS";
        case AppState::HUNT: return "HUNT";
        case AppState::COLOR_CHECK: return "COLOR_CHECK";
        case AppState::DETECTION_FILTER: return "DETECTION_FILTER";
        case AppState::MESH_MENU: return "MESH_MENU";
        case AppState::MESH_WARN: return "MESH_WARN";
        case AppState::BEACON_WARN: return "BEACON_WARN";
        case AppState::SECURITY: return "SECURITY";
        case AppState::LOCKED: return "LOCKED";
        case AppState::PIN_ENTRY: return "PIN_ENTRY";
        case AppState::SQUAD: return "SQUAD";
        case AppState::MESH_PHRASE: return "MESH_PHRASE";
        case AppState::MESH_COMPOSE: return "MESH_COMPOSE";
        default: return "?";
    }
}

// The firmware pushes its sprite into tft's buffer at the end of every
// loop(), so by the time control returns here tft holds the frame that
// would have gone to the panel.
static void emitFrame() {
    const int w = tft.width(), h = tft.height();
    const std::vector<uint16_t>& src = tft.pixelsRGB565();

    static std::vector<uint8_t> rgb;
    rgb.clear();
    rgb.reserve(src.size() * 3);
    for (uint16_t v : src) {
        uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        rgb.push_back((uint8_t)((r << 3) | (r >> 2)));
        rgb.push_back((uint8_t)((g << 2) | (g >> 4)));
        rgb.push_back((uint8_t)((b << 3) | (b >> 2)));
    }
    // The virtual peer's status rides on the header for the same reason the
    // state does, and last, because it is the one field with spaces in it.
    printf("FRM %d %d %zu %s %s\n", w, h, rgb.size(), stateName(state), MeshSim::status());
    fwrite(rgb.data(), 1, rgb.size(), stdout);
    fflush(stdout);
}

// A profile index (what the GUI sends -- see the Y command) or a type name as
// the LOG screen spells it, which gets that type's first profile, so
// `T AIRTAG` still works when driving this by hand from a shell.
static const SimDetectionProfile* parseProfile(const char* s) {
    while (*s == ' ') s++;
    if (!*s) return nullptr;
    if (*s >= '0' && *s <= '9') {
        const int n = atoi(s);
        return (n >= 0 && (size_t)n < kSimProfileCount) ? &kSimProfiles[n] : nullptr;
    }
    for (uint8_t i = 1; i < (uint8_t)DetectionType::COUNT; i++) {
        const char* nm = detectionTypeName((DetectionType)i);
        if (nm && strcasecmp(nm, s) == 0) return simProfileFor((DetectionType)i);
    }
    return nullptr;
}

// Firmware time advanced per step. 33ms ~= the device's real loop rate,
// so animation speed matches hardware when the GUI steps at ~30fps.
static const uint32_t STEP_MS = 33;

int main() {
    SimClock::virtualTime = true;
    setup();

    char line[128];
    while (fgets(line, sizeof(line), stdin)) {
        // Trimmed up front so a trailing newline can't end up inside a
        // string argument (T takes a type *name*, not just a number).
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        char cmd = line[0];
        if (cmd == 'Q') break;

        if (cmd == 'D' || cmd == 'M') {
            int x = 0, y = 0;
            if (sscanf(line + 1, "%d %d", &x, &y) == 2) {
                screenToRaw(x, y, SimTouch::rawX, SimTouch::rawY);
                SimTouch::down = true;
            }
        } else if (cmd == 'U') {
            SimTouch::down = false;
        } else if (cmd == 'T') {
            // Posted through the engine's own postBle() rather than
            // written into the log directly, so the trigger goes in by
            // the same door a real BLE hit would. (In this build that
            // door is a stub -- see sim_detections.h for what that
            // does and doesn't test.)
            char* arg = line + 1;
            while (*arg == ' ' || *arg == '\t') arg++;   // before splitting, not after
            char* sp = strpbrk(arg, " \t");
            int rssi = 0;
            if (sp) { *sp = 0; rssi = atoi(sp + 1); }
            if (const SimDetectionProfile* p = parseProfile(arg)) {
                static uint16_t serial = 0;
                Detection d;
                simMakeDetection(d, *p, millis(), rssi, serial++);
                engine.postBle(d);
                fprintf(stderr, "[detect] %s (%s) %s %ddBm\n",
                        detectionTypeName(p->type), p->label, d.name, d.rssi);
            } else {
                fprintf(stderr, "[detect] unknown profile: %s\n", arg);
            }
        } else if (cmd == 'P') {
            MeshSim::command(line + 1);
        } else if (cmd == 'K') {
            // Answered at once on stdout, so a caller reads it like a header.
            printf("CAT %s\n", MeshSim::catalog());
            fflush(stdout);
        } else if (cmd == 'Y') {
            printf("DETS %s\n", simProfileCatalog());
            fflush(stdout);
        } else if (cmd == 'R') {
            fprintf(stderr, "[state] %s\n", stateName(state));
        } else if (cmd == 'S') {
            int n = 1;
            sscanf(line + 1, "%d", &n);
            if (n < 1) n = 1;
            for (int i = 0; i < n; i++) { SimClock::nowMs += STEP_MS; loop(); }
            emitFrame();
        }
    }
    return 0;
}
