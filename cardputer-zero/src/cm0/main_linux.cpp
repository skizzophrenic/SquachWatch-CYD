// SquachWatch-CM0 -- squachd, the CM0 harness.
//
// Runs the firmware's own main.cpp (setup/loop) with the real detection.cpp
// behind the radio stubs, in two modes:
//
//   ./squachd                 real time: a borderless 320x170 SDL2 window on
//                             the stock labwc session, the card keys from
//                             the keyboard (holding ESC for a second exits
//                             the app), and the same line console main_live
//                             speaks (T/S/Q) on stdin.
//   ./squachd --fb            the same, on /dev/fb0 and the keyboard's event
//                             device: how the device's app launcher runs it
//                             (fb_io.h). Off a terminal, the log goes to
//                             ~/.local/state/squachwatch/squachd.log.
//   ./squachd --shot out.png  headless: virtual time, the real setup() and
//                             ~160 loop() steps (the 3 s boot splash is 91 of
//                             them), one frame -> RGB888 -> PNG. No display,
//                             no radio, no keyboard: a build + UI smoke test
//                             that needs no device.
//
// Why the harness owns the cadence: in real time the Arduino shim's delay()
// is a no-op, so one loop() per 33 ms tick is what the device's loop rate is.
// No catch-up spiral -- a slow frame is dropped, not repeated. Between ticks
// the thread sleeps (SDL_WaitEventTimeout), and the window is redrawn only
// when a loop() ran: with the software renderer there is no vsync to block
// on, and a spinning render loop would hold a CM0 core at 100%.
//
// Every loop() is preceded by cm0::wifiMonPump(): the sniffer's frames reach
// the firmware's rx callback on THIS thread, never on the feed thread (see
// wifi_mon.h for why).
//
// Panel geometry: the firmware's sim TFT_eSPI shim ignores TFT_WIDTH /
// TFT_HEIGHT and builds `tft` at 320x240 (the CYD); main.cpp's global is
// `TFT_eSPI tft = TFT_eSPI();`. Before setup() runs, the harness replaces it
// with a 320x170 one so every screen lays out for the real panel.
#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <linux/input.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "TFT_eSPI.h"
#include "state.h"
#include "detection.h"
#include "sim_detections.h"
#include "png_writer.h"
#include "wifi_mon.h"
#include "ble_scan.h"
#include "battery.h"
#include "prefs_store.h"
#include "user_fs.h"
#include "SD.h"
#include "fb_io.h"
#include "overlay.h"
#include "dimmer.h"
#include "shake_iio.h"
#include "coming_soon.h"
#include "keymap.h"
#include "i2s_alsa.h"
#include "ui_clear.h"
#include "sound.h"
#include "settings.h"

// Defined by the firmware's main.cpp, which this target compiles.
void setup();
void loop();
extern TFT_eSPI tft;
extern AppState state;
extern uint8_t  screenRotation;
extern DetectionEngine engine;

// The card keys the firmware's smallKeysRead() reports, with SQUACHSIM
// (main.cpp, simCardKey): 0 = level (uses down), 1 = next, 2 = previous,
// 3 = BACK (down edge only; HOME until v1.43 -- on the home screen BACK
// opens the menu, as ESC does on the Cardputer ADV).
extern void simCardKey(int what, bool down);
// Characters for the firmware's text screens and hotkeys (see "typing" below).
static void typeChar(char c);

// The firmware's own step rate (main_live.cpp), one loop() per frame.
static const uint32_t STEP_MS = 33;

// How long ESC must be held to quit the app: the on-device exit path (the
// stdin console is a developer channel, and there is no launcher menu to
// kill it from). One second: not an accidental BACK tap, not a wait.
static const uint32_t ESC_EXIT_MS = 1000;

// L's level key is held across the cadence (this thread owns the loop, so a
// sleep inside dispatchLine would never be sampled by loop()). The deadline
// arms in tickLive, whose clock (SDL ticks or monoMs) is per front end.
static bool     s_levelHeld = false, s_levelArmed = false;
static uint32_t s_levelMs = 0, s_levelUntil = 0;

// The Cardputer Zero's panel, landscape: the long side of the two
// cardputer_zero_user_setup.h dimensions across, the short side down.
static const int PANEL_W = TFT_WIDTH > TFT_HEIGHT ? TFT_WIDTH : TFT_HEIGHT;
static const int PANEL_H = TFT_WIDTH > TFT_HEIGHT ? TFT_HEIGHT : TFT_WIDTH;

static void usePanelGeometry() {
    tft = TFT_eSPI(PANEL_W, PANEL_H);
}

// One firmware step: queued sniffer frames and BLE adverts first, then
// the loop.
static void step() {
    cm0::wifiMonPump();
    cm0::blePump();
    loop();
}

// --------------------------------------------------------------- the frame

// The firmware pushes its sprite into tft's buffer at the end of every
// loop(); blit whatever is there.
static void renderFrame(SDL_Renderer* ren, SDL_Texture* tex, const std::vector<uint16_t>& px) {
    const int w = tft.width(), h = tft.height();
    if (px.size() < (size_t)w * h) return;   // buffer not full yet
    SDL_UpdateTexture(tex, nullptr, px.data(), w * 2);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
}

// ------------------------------------------------------------------ stdin

// Line-by-line, non-blocking: the console is a side channel, and select()
// with a zero timeout means the loop cadence never waits for a human. One
// read() per call, of whatever select() said is there -- a second read on
// the (blocking) fd would stall the UI until the rest of a partial line
// arrived. stdin is not made O_NONBLOCK: that flag would leak to the shell
// sharing the terminal. EOF (stdin = /dev/null from a launcher) closes the
// console for good.
static std::string s_stdinBuf;
static bool        s_stdinOpen = true;

static bool readLineNonblock(char* out, size_t outLen) {
    if (s_stdinOpen) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        timeval tv = { 0, 0 };
        if (select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv) > 0) {
            char chunk[256];
            const ssize_t n = read(STDIN_FILENO, chunk, sizeof chunk);
            if (n == 0) s_stdinOpen = false;
            else if (n > 0) s_stdinBuf.append(chunk, (size_t)n);
        }
    }
    for (;;) {
        const size_t eol = s_stdinBuf.find_first_of("\r\n");
        if (eol == std::string::npos) {
            if (s_stdinBuf.size() > 120) s_stdinBuf.clear();   // a stuck line
            return false;
        }
        std::string line = s_stdinBuf.substr(0, eol);
        s_stdinBuf.erase(0, eol + 1);
        if (line.empty()) continue;   // the \n of a \r\n, or a bare Enter
        const size_t n = line.size() < outLen - 1 ? line.size() : outLen - 1;
        memcpy(out, line.data(), n);
        out[n] = 0;
        return true;
    }
}

// A profile index (what Y lists) or a type name as the LOG screen spells
// it -- same resolver as main_live, so hand-driving the harness does not
// drift from the GUI's protocol.
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

// The same commands main_live takes, minus the touch and mesh ones the
// device drives itself.
static void dispatchLine(const char* in, bool& quit) {
    char line[128];
    strncpy(line, in, sizeof line - 1);
    line[sizeof line - 1] = 0;

    char* l = line;
    while (*l == ' ' || *l == '\t') l++;
    const char cmd = *l;

    if (cmd == 'Q') {
        quit = true;
    } else if (cmd == 'S') {
        int n = 1;
        sscanf(l + 1, "%d", &n);
        if (n < 1) n = 1;
        for (int i = 0; i < n; i++) step();
    } else if (cmd == 'T') {
        char* arg = l + 1;
        while (*arg == ' ' || *arg == '\t') arg++;
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
    } else if (cmd == 'Y') {
        // Type text as on the keyboard (a hotkey off a text screen); '\n'
        // and '\b' are written as \n and \b.
        const char* t = l + 1;
        if (*t == ' ') t++;
        for (; *t && *t != '\n'; t++) {
            if (t[0] == '\\' && t[1] == 'n') { typeChar('\n'); t++; }
            else if (t[0] == '\\' && t[1] == 'b') { typeChar('\b'); t++; }
            else typeChar(*t);
        }
    } else if (cmd == 'N') {
        simCardKey(1, true);   // next: a down edge, consumed at the next loop()
    } else if (cmd == 'P') {
        simCardKey(2, true);   // prev
    } else if (cmd == 'H') {
        simCardKey(3, true);   // back (the console letter predates v1.43, when this was home)
    } else if (cmd == 'L') {
        // Level is a *state*, not an edge: press it here, release it in
        // tickLive() when the clock passes the deadline. A nanosleep here
        // would block the shared cadence and the press never sampled.
        int ms = 120;
        sscanf(l + 1, "%d", &ms);
        if (ms < 1) ms = 120;
        if (!s_levelHeld) {
            simCardKey(0, true);
            s_levelHeld = true;
            s_levelArmed = false;
            s_levelMs = (uint32_t)ms;
        }
    } else {
        fprintf(stderr,
            "[console] unknown command (T <n|type> [rssi] / S [n] / Q / N / P / H / L / Y <text>)\n");
    }
}

// -------------------------------------------------------------- keyboard

// The card keys, as the labwc session delivers them to the window. The
// mapping is the firmware's own smallKeysRead() (main.cpp, the TCA8418
// branch): ENTER or space = the level key, down/right/TAB = next, up/left =
// previous, ESC or DEL = back (on the home screen: the menu). The TCA8418
// does not auto-repeat, so the compositor's key repeat is ignored: holding
// an arrow is one step, as on the Cardputer ADV.
//
// ESC is also the exit: its key-down starts a clock (s_escDownAt), the
// key-up cancels it, and the main loop quits once it has run ESC_EXIT_MS.
// The back key-press has already gone out at key-down -- by the time the
// app quits the user has watched the UI step back (or the menu open),
// which is the cue.
static uint32_t s_escDownAt = 0;   // 0 = ESC not held


// ------------------------------------------------------------------ main

// Headless: virtual time, boot the real setup(), run the loop until the
// boot splash has long since finished, and write one frame to a PNG.
static int runShot(const char* outPath) {
    unsetenv("SQUACHSIM_SD");   // no card: a shot is the same on every machine
    SimClock::virtualTime = true;
    usePanelGeometry();
    cm0::wifiMonStart();   // false = no adapter: the stub radio, as Phase 1
    setup();
    uiClearSetBatterySource(cm0::batteryState);   // the lava lamp (battery.h)
    for (int i = 0; i < 160; i++) {
        SimClock::nowMs += STEP_MS;
        step();
    }
    cm0::bleScanShutdown();
    cm0::wifiMonStop();
    const int w = tft.width(), h = tft.height();
    const std::vector<uint16_t>& px = tft.pixelsRGB565();
    if (px.size() < (size_t)w * h) {
        fprintf(stderr, "[shot] framebuffer not full yet (%zu px)\n", px.size());
        return 1;
    }
    std::vector<uint8_t> rgb;
    rgb.reserve((size_t)w * h * 3);
    for (uint16_t v : px) {
        const uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        rgb.push_back((uint8_t)((r << 3) | (r >> 2)));
        rgb.push_back((uint8_t)((g << 2) | (g >> 4)));
        rgb.push_back((uint8_t)((b << 3) | (b >> 2)));
    }
    if (!PngWriter::write(outPath, w, h, rgb.data())) {
        fprintf(stderr, "[shot] cannot write %s\n", outPath);
        return 1;
    }
    printf("[shot] %dx%d -> %s\n", w, h, outPath);
    return 0;
}

// Headless detection self-test (selftest.cpp): virtual time, the real
// setup() and boot splash, then synthetic deauth frames and tracker adverts
// through the port's radio paths into the real engine. No sniffer.
int runSelftest(void (*step)());
static int runSelftestMode() {
    unsetenv("SQUACHSIM_SD");   // no card, no stray wardrive files
    SimClock::virtualTime = true;
    usePanelGeometry();
    setup();
    for (int i = 0; i < 160; i++) {
        SimClock::nowMs += STEP_MS;
        step();
    }
    const int rc = runSelftest(step);
    cm0::bleScanShutdown();
    return rc;
}

// What goes to the screen: the firmware's frame, with the exit modal on
// top while ESC is being held (overlay.h). `now` is on the same clock as
// s_escDownAt (SDL ticks or monoMs, per front end).
static std::vector<uint16_t> s_composed;
static const std::vector<uint16_t>& frameFor(uint32_t now) {
    const std::vector<uint16_t>& px = tft.pixelsRGB565();
    if (s_escDownAt && (int32_t)(now - s_escDownAt) >= (int32_t)cm0::EXIT_HINT_MS) {
        const float p = (float)(now - s_escDownAt) / (float)ESC_EXIT_MS;
        cm0::composeExitHint(px, tft.width(), tft.height(), p, s_composed);
        return s_composed;
    }
    return px;
}

// ------------------------------------------------------------ live modes
//
// Two front ends over one live run:
//   (default)  an SDL2 window -- the labwc session, a desktop, development.
//   --fb       /dev/fb0 + the keyboard's event device -- how the device's
//              app launcher runs its apps (fb_io.h).

// The card keys, from either front end. ESC also arms the long-press exit.
enum class CardKey { NONE, LEVEL, NEXT, PREV, BACK };

static void cardKeyEvent(CardKey k, bool isEsc, bool down, uint32_t nowMs) {
    if (isEsc) s_escDownAt = down ? (nowMs ? nowMs : 1) : 0;
    switch (k) {
        case CardKey::LEVEL: simCardKey(0, down); break;   // a state: down and up
        case CardKey::NEXT:  if (down) simCardKey(1, true); break;
        case CardKey::PREV:  if (down) simCardKey(2, true); break;
        case CardKey::BACK:  if (down) simCardKey(3, true); break;
        default: break;
    }
}

static CardKey fromSdl(SDL_Keycode s) {
    switch (s) {
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: return CardKey::LEVEL;
        case SDLK_DOWN: case SDLK_RIGHT: case SDLK_TAB:        return CardKey::NEXT;
        case SDLK_UP: case SDLK_LEFT:                          return CardKey::PREV;
        case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_DELETE: return CardKey::BACK;
        default: return CardKey::NONE;
    }
}

// ---- typing --------------------------------------------------------------
// The firmware's text screens -- the squad name and messages (PHONE), a WiFi
// password, the PIN and the lock screen -- take characters from the
// Cardputer ADV's keyboard on CARDPUTER_ADV builds, and switch the cursor
// off while they are up (main.cpp, cardTyping()). simCardKey() has no way to
// carry a character, so the harness feeds the firmware's own typed-character
// channel instead: g_devType, read one character a loop by devKeysMerge()
// (compiled in by -DSQW_DEVTOOLS=1, see the Makefile). On a text screen the
// character is typed; anywhere else a letter is the screen's hotkey (on the
// home screen: S scan, L log, D desk, B background), as on the real keyboard.
extern char g_devType[48];
extern volatile uint8_t g_devTypeAt, g_devTypeN;

// The firmware's cardTyping(), which is static in main.cpp: kept in step with
// it by hand (the list has not changed since v1.43).
static bool typingScreen() {
    return state == AppState::WIFI_PASS || state == AppState::PIN_ENTRY ||
           state == AppState::LOCKED || state == AppState::PHONE;
}

static void typeChar(char c) {
    // devKeysMerge() reads '~' as ENTER and '^' as DEL (its console encoding),
    // so those two cannot be typed literally; ENTER and DEL go as '\n' / '\b'.
    if (c == '~' || c == '^') return;
    if (g_devTypeAt >= g_devTypeN) { g_devTypeAt = 0; g_devTypeN = 0; }   // all read: reuse
    if (g_devTypeN < sizeof g_devType) g_devType[g_devTypeN++] = c;
}

static CardKey fromEvdev(uint16_t c) {
    switch (c) {
        case KEY_ENTER: case KEY_KPENTER: case KEY_SPACE: return CardKey::LEVEL;
        case KEY_DOWN: case KEY_RIGHT: case KEY_TAB:      return CardKey::NEXT;
        case KEY_UP: case KEY_LEFT:                       return CardKey::PREV;
        case KEY_ESC: case KEY_BACKSPACE: case KEY_DELETE: return CardKey::BACK;
        default: return CardKey::NONE;
    }
}

// One key from the device keyboard (--fb). SHIFT is tracked by the caller.
static void evdevKey(uint16_t code, bool down, bool shift, uint32_t nowMs) {
    if (code == KEY_ESC) { cardKeyEvent(CardKey::BACK, true, down, nowMs); return; }
    if (typingScreen()) {
        if (code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT) {
            cardKeyEvent(fromEvdev(code), false, down, nowMs);   // arrows still move
            return;
        }
        if (!down) return;
        if (code == KEY_ENTER || code == KEY_KPENTER)      typeChar('\n');
        else if (code == KEY_BACKSPACE || code == KEY_DELETE) typeChar('\b');
        else if (code == KEY_TAB)                          typeChar('\t');   // SHOW/HIDE a password
        else if (const char c = cm0::evdevChar(code, shift)) typeChar(c);
        return;
    }
    if (down && !shift) {
        const char c = cm0::evdevChar(code, false);
        if (c >= 'a' && c <= 'z') { typeChar(c); return; }   // a hotkey
    }
    cardKeyEvent(fromEvdev(code), false, down, nowMs);
}

static uint32_t monoMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

// Started from the launcher, stderr is a socket nobody reads. Then the log
// goes to ~/.local/state/squachwatch/squachd.log (the previous run kept as
// squachd.prev.log), created like every other file of the user's: through
// a no-follow directory descriptor, owned by them (user_fs.h).
static void logToFileIfNoTerminal() {
    if (isatty(STDERR_FILENO)) return;
    const cm0::UserIds& u = cm0::invokingUser();
    if (u.home.empty()) return;
    const std::string dir = u.home + "/.local/state/squachwatch";
    if (!cm0::makeUserDirs(dir)) return;
    const int dfd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dfd < 0) return;
    struct stat st;
    if (fstat(dfd, &st) != 0 || st.st_uid != u.uid || (st.st_mode & 022)) { close(dfd); return; }
    (void)renameat(dfd, "squachd.log", dfd, "squachd.prev.log");
    const int fd = openat(dfd, "squachd.log", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    close(dfd);
    if (fd < 0) return;
    if (u.sudo) (void)fchown(fd, u.uid, u.gid);
    dup2(fd, STDERR_FILENO);
    dup2(fd, STDOUT_FILENO);
    close(fd);
    setvbuf(stderr, nullptr, _IONBF, 0);
    setvbuf(stdout, nullptr, _IOLBF, 0);
}

// Everything a live run needs before the first loop(), in the order the
// privileges require: the radios and every device node first (root under
// sudo, or the user's groups), then this thread's file access becomes the
// user's, then the firmware boots.
static bool startLive(bool fb) {
    SimClock::virtualTime = false;   // real time: the harness paces loop()
    TFT_eSPI::rotates = getenv("SQUACHSIM_ROTATE") != nullptr;
    usePanelGeometry();
    // Settings survive a restart (the live app only: --shot and --selftest
    // stay in memory). Before setup(): namespaces load at begin().
    {
        const std::string pd = cm0::prefsDefaultDir();
        cm0::prefsSetDir(pd);
        fprintf(stderr, "[prefs] %s\n", pd.empty() ? "not saved: no home directory" : pd.c_str());
    }
    // The "SD card": a folder (the CM0's card is its root fs). SdLog goes
    // through the SD class; the wardrive CSV through the firmware's own
    // non-ESP32 branch, which reads $SQUACHSIM_SD -- same folder.
    {
        const std::string sd = cm0::sdDefaultRoot();
        cm0::sdSetRoot(sd);
        if (!sd.empty()) setenv("SQUACHSIM_SD", sd.c_str(), 1);
        else unsetenv("SQUACHSIM_SD");
    }
    // The panel and keyboard (root:video / root:input) before the fs switch.
    if (fb && !cm0::fbIoOpen()) return false;
    // The backlight (root:video), for auto-dim while we are in front -- the
    // launcher pauses its own screen-off for us (dimmer.h). --fb only: in a
    // terminal run the launcher's screensaver still owns the panel.
    if (fb) cm0::dimmerOpen(monoMs());
    cm0::wifiMonStart();   // false = no adapter: the stub radio, as Phase 1
    // The GPS UART (root:dialout): opened before the fs switch below, held
    // for the run (gps_serial.h).
    cm0::gpsSerialPreopen();
    // Radios up (they needed root); from here this thread's file access is
    // the user's own -- the firmware's fopen()s included (user_fs.h).
    cm0::useUserFsIds();
    setup();
    uiClearSetBatterySource(cm0::batteryState);   // the lava lamp (battery.h)
    // The speaker: since v1.43 setup() itself calls Sound::begin() on
    // CARDPUTER_ADV builds ("the ES8311 is on the keyboard's bus"), which
    // this one is -- so the harness no longer does (a second begin() would
    // start a second sound task). A missing or busy card is the firmware's
    // own "no speaker" state: present() false, everything silent.
    // The firmware's row gate (ui_settings.cpp) treats every non-ESP32
    // build as an emulator and shows the SOUND/VOLUME rows only when this
    // environment asks for them. The CM0 is not the emulator: when the
    // card actually came up, the rows belong on the settings screen.
    if (Sound::present()) setenv("SQUACHSIM_SPEAKER", "1", 1);
    return true;
}

static void stopLive() {
    cm0::bleScanShutdown();   // app, not kiosk: LE scan + adverts off
    cm0::wifiMonStop();       // ... and the adapter goes back to rest
    cm0::shakeClose();        // the IMU reader thread (shake_iio.h)
    cm0::audioKeepAwake(false);   // the codec may sleep again
    fprintf(stderr, "[prefs] %lu saves this run\n", cm0::prefsSaveCount());
}

// After each live loop():
//  - a coming-soon toast raised from SETTINGS (coming_soon.h): the firmware
//    never draws a toast over the menu, so close it -- BACK, as the ESC key
//    sends it, again while still in SETTINGS (a sub-page's BACK lands on the
//    main list first), at most four, 300 ms apart (a BACK is a 120 ms tap).
//  - the codec held awake while SOUND is on (i2s_alsa.h, audioKeepAwake).
//  - every alert the firmware raises, in the log, with whether the device
//    is new to the log (the only kind the firmware beeps for) or back in
//    range (silent by its design) -- the other half of sound_log.cpp.
static void afterStep(uint32_t now) {
    static uint8_t  leaveTries = 0;
    static uint32_t leaveAt = 0;
    if (cm0TakeLeaveSettings()) { leaveTries = 4; leaveAt = now - 1000; }
    if (leaveTries) {
        if (state != AppState::SETTINGS) {
            leaveTries = 0;
        } else if ((int32_t)(now - leaveAt) >= 300) {
            simCardKey(3, true);
            leaveAt = now;
            leaveTries--;
        }
    }

    // The codec held awake only while sound can play (i2s_alsa.h): SOUND is
    // ALERTS or ALL and the speaker came up. Cheap when nothing changes.
    cm0::audioKeepAwake(Sound::present() && Settings::soundMode() != 0);

    static AppState prev = state;
    if (state != prev) {
        fprintf(stderr, "[state] %u -> %u\n", (unsigned)prev, (unsigned)state);
        if (state == AppState::ALERT || state == AppState::WATCH_ALERT) {
            const Detection* d = engine.latest();
            fprintf(stderr, "[alert] %s: %s%s%s %ddBm (%s)\n",
                    state == AppState::WATCH_ALERT ? "WATCH" : "ALERT",
                    d ? detectionTypeName(d->type) : "?", d && d->name[0] ? " " : "",
                    d ? d->name : "", d ? (int)d->rssi : 0,
                    engine.latestIsNew() ? "new to the log: beeps" : "back in range: silent by design");
        }
        prev = state;
    }
}

// One tick of the shared cadence: console lines, the ESC exit, a loop()
// when due. Returns true when a loop() ran (a new frame).
static bool tickLive(uint32_t now, uint32_t& next, bool& quit) {
    char line[128];
    bool ran = false;
    while (!quit && readLineNonblock(line, sizeof line)) {
        dispatchLine(line, quit);
        ran = true;
    }
    if (s_escDownAt && (int32_t)(now - s_escDownAt) >= (int32_t)ESC_EXIT_MS) {
        fprintf(stderr, "[cm0] ESC held %u ms: exiting\n", now - s_escDownAt);
        quit = true;
    }
    if (s_levelHeld) {
        if (!s_levelArmed) { s_levelUntil = now + s_levelMs; s_levelArmed = true; }
        if ((int32_t)(now - s_levelUntil) >= 0) {
            simCardKey(0, false);
            s_levelHeld = s_levelArmed = false;
        }
    }
    if ((int32_t)(now - next) >= 0) {
        step();
        afterStep(now);
        ran = true;
        next = now + STEP_MS;
    }
    return ran;
}

static int runSdl() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!startLive(false)) { SDL_Quit(); return 1; }

    const int w = tft.width(), h = tft.height();
    SDL_Window* win = SDL_CreateWindow(
        "SquachWatch", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w, h, SDL_WINDOW_BORDERLESS | SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = nullptr;
    SDL_Texture* tex = nullptr;
    if (win) {
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (ren) tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!win || !ren || !tex) {
        fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        if (tex) SDL_DestroyTexture(tex);
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        stopLive();
        SDL_Quit();
        return 1;
    }
    SDL_StartTextInput();   // characters for the text screens (SDL_TEXTINPUT)

    step();   // the first frame, immediately
    bool dirty = true;
    uint32_t next = SDL_GetTicks() + STEP_MS;
    bool quit = false;
    while (!quit) {
        // Sleep until the next tick, waking early for input.
        const uint32_t before = SDL_GetTicks();
        const int wait = (int32_t)(next - before) > 0 ? (int)(next - before) : 0;
        SDL_Event ev;
        if (SDL_WaitEventTimeout(&ev, wait)) {
            do {
                if (ev.type == SDL_QUIT) quit = true;
                else if (ev.type == SDL_TEXTINPUT) {
                    // Characters (with SHIFT and the desktop's layout applied):
                    // typed on a text screen, a hotkey letter anywhere else.
                    for (const char* t = ev.text.text; *t; t++) {
                        const char c = *t;
                        if (c < ' ' || c > '~') continue;
                        if (typingScreen() || (c >= 'a' && c <= 'z')) typeChar(c);
                    }
                    dirty = true;
                }
                else if ((ev.type == SDL_KEYDOWN && !ev.key.repeat) || ev.type == SDL_KEYUP) {
                    const SDL_Keycode sym = ev.key.keysym.sym;
                    const bool down = ev.type == SDL_KEYDOWN;
                    if (typingScreen() && sym != SDLK_ESCAPE && sym != SDLK_UP && sym != SDLK_DOWN &&
                        sym != SDLK_LEFT && sym != SDLK_RIGHT) {
                        // The text arrives as SDL_TEXTINPUT; only the editing keys here.
                        if (down && (sym == SDLK_RETURN || sym == SDLK_KP_ENTER)) typeChar('\n');
                        else if (down && (sym == SDLK_BACKSPACE || sym == SDLK_DELETE)) typeChar('\b');
                        else if (down && sym == SDLK_TAB) typeChar('\t');
                    } else {
                        cardKeyEvent(fromSdl(sym), sym == SDLK_ESCAPE, down, SDL_GetTicks());
                    }
                    dirty = true;   // the exit modal may open or close
                }
                else if (ev.type == SDL_WINDOWEVENT) dirty = true;   // exposed/resized
            } while (SDL_PollEvent(&ev));
        }
        if (tickLive(SDL_GetTicks(), next, quit)) dirty = true;
        if (dirty) {
            renderFrame(ren, tex, frameFor(SDL_GetTicks()));
            dirty = false;
        }
    }

    stopLive();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

static int runFb() {
    if (!startLive(true)) return 1;
    step();
    cm0::fbPush(frameFor(monoMs()), tft.width(), tft.height());
    uint32_t next = monoMs() + STEP_MS;
    bool quit = false;
    std::vector<cm0::KeyEvent> keys;
    while (!quit) {
        const uint32_t before = monoMs();
        const int wait = (int32_t)(next - before) > 0 ? (int)(next - before) : 0;
        struct pollfd pfd = { cm0::kbdFd(), POLLIN, 0 };
        if (poll(&pfd, 1, wait) > 0 && (pfd.revents & POLLIN)) {
            keys.clear();
            cm0::kbdRead(keys);
            const uint32_t now = monoMs();
            static bool shift = false;
            for (const cm0::KeyEvent& k : keys) {
                if (k.code == KEY_LEFTSHIFT || k.code == KEY_RIGHTSHIFT) { shift = k.down; continue; }
                if (cm0::dimmerKey(k.code, k.down, now)) continue;   // woke the panel: nothing else
                if (!k.repeat) evdevKey(k.code, k.down, shift, now);
            }
            cm0::fbPush(frameFor(now), tft.width(), tft.height());   // the modal may open or close
        }
        cm0::dimmerTick(monoMs());
        // The auto-dim does NOT mute: an idle board is exactly when an
        // alert has to be heard (user decision, 2026-11-20). What keeps it
        // quiet is the firmware's own rules in Sound::play() -- the SOUND
        // mode, the night hours (WATCH_HIT excepted, by its design) and
        // its POWER SAVER hush.
        if (tickLive(monoMs(), next, quit) && !quit)
            cm0::fbPush(frameFor(monoMs()), tft.width(), tft.height());
        // The firmware's own backlight -- SETTINGS > BRIGHTNESS, or POWER
        // SAVER's dim level -- arrives as the emulator's ledcWrite on
        // channel 0 (applyBrightness in main.cpp); the dimmer makes it the
        // panel's real level whenever it is not dimming (dimmer.h).
        if (g_simBacklight >= 0) cm0::dimmerSetAppDuty(g_simBacklight);
    }
    stopLive();
    cm0::dimmerClose();   // the launcher's brightness back before it returns
    cm0::fbClear();
    cm0::fbIoClose();
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && strcmp(argv[1], "--shot") == 0)
        return runShot(argv[2]);
    if (argc == 2 && strcmp(argv[1], "--selftest") == 0)
        return runSelftestMode();
    if (argc == 2 && strcmp(argv[1], "--fb") == 0) {
        logToFileIfNoTerminal();
        return runFb();
    }
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--fb] | %s --shot <out.png> | %s --selftest\n",
                argv[0], argv[0], argv[0]);
        return 1;
    }
    logToFileIfNoTerminal();
    return runSdl();
}
