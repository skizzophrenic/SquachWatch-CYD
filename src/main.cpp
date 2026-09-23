// SquachWatch-CYD — main firmware
// Wires the state machine (DESIGN.md §9) across the UI modules
// and the DetectionEngine.

#include <Arduino.h>
#include "serial_flush.h"
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#if defined(CROWPANEL7)
#include "crowpanel7_board.h"
#include "crowpanel7_display.h"
#include "crowpanel7_rgb.h"
#include "crowpanel7_blit.h"
#include "crowpanel7_backlight.h"
#include "gt911_touch.h"
#include "crowpanel7_probe.h"
// The [frame] line's "rows" is whoever actually wrote them.
#define SQW_PUSH_ROWS() CrowBlit::lastRows()
#else
#define SQW_PUSH_ROWS() FramePush::lastRows()
#endif
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>  // AWOK's own per-rotation touch-cal storage; see the AWOK block below pollTouch()'s globals
#include <esp_heap_caps.h>   // heap_caps_get_largest_free_block() -- diagnostics screen
#include <esp_system.h>      // esp_reset_reason() -- diagnostics screen
// The core dump's own summary -- which task, and where. The emulator has
// neither header, and nothing to summarise.
#if __has_include(<esp_core_dump.h>)
#include <esp_core_dump.h>
#include <esp_ota_ops.h>
#define HAVE_COREDUMP_SUMMARY (CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF)
#else
#define HAVE_COREDUMP_SUMMARY 0
#endif
// The whole NVS store, for a wipe that really erases -- see physicalNvsWipe().
// The emulator has no flash and gets the logical wipe instead.
#if __has_include(<nvs_flash.h>)
#include <nvs_flash.h>
#include <nvs.h>
#include <string>
#include <vector>
#define HAVE_NVS_ERASE 1
#else
#define HAVE_NVS_ERASE 0
#endif
#include <esp_heap_caps.h>
#include "ui_diagnostics.h"   // CrashReport, used by the breadcrumb below
#include "blackbox.h"
#include "ui_bingo.h"
#include "bingo.h"
#include "dex.h"
#include "ui_dex.h"
#include "regulars.h"
#include "notices.h"
#include "theme.h"            // the crash card on the splash
#include "clock.h"            // ...and the ten-minute IGNORE on it

// Written every second, read once on the next boot. RTC_NOINIT_ATTR is the
// point: it survives a software reset WITHOUT being zeroed on the way back
// up, which is exactly what a post-mortem needs and what a normal static
// cannot do. The magic is how we tell a real breadcrumb from whatever was in
// RTC RAM after a cold boot.
static const uint32_t CRUMB_MAGIC = 0x5175A0FEu;
RTC_NOINIT_ATTR static struct {
    uint32_t magic;
    uint32_t uptimeMs;
    uint32_t heapFree;
    uint32_t heapBlock;
    uint32_t lifetime;
    uint8_t  screen;
} g_crumb;
// Why this boot happened, for the black box's boot record.
static esp_reset_reason_t g_resetReason = ESP_RST_UNKNOWN;

// Snapshotted at boot, before the live breadcrumb starts overwriting it.
static CrashReport g_lastCrash = {};

// A wipe restarts the board -- see performWipe() -- and this says how it is to
// come back: unlocked and straight to the main screen after a duress PIN or a
// forgotten-PIN wipe, so the unlock looks like an unlock; locked after the
// tenth wrong guess. RTC RAM survives the restart and nothing else, and it is
// only believed after a software reset, never after a power-on.
static const uint32_t WIPEBOOT_MAGIC = 0x57A1E000u;
enum class WipeBoot : uint8_t { NONE, UNLOCKED, LOCKED };
RTC_NOINIT_ATTR static uint32_t g_wipeBoot;
// The boot update check ran and the frame buffer then failed to allocate:
// restart once without the check rather than run on with no frame buffer.
// Same RTC-memory shape as the wipe flag, and believed only after a software
// reset.
static const uint32_t CHKSKIP_MAGIC = 0x5C1BB00Cu;
RTC_NOINIT_ATTR static uint32_t g_bootCheckSkip;
static bool takeBootCheckSkip() {
    const uint32_t v = g_bootCheckSkip;
    g_bootCheckSkip = 0;
    return esp_reset_reason() == ESP_RST_SW && v == CHKSKIP_MAGIC;
}
#if defined(TWATCH_S3)
// The watch's radio self-heal: how many restarts in a row it has made because
// it heard nothing at all. Same RTC-memory shape as the flags above: a magic
// in the top bits, believed only after the heal's own deep-sleep wake (or a
// software reset, the older way). See
// twatchRadioHealTick().
static const uint32_t HEAL_MAGIC = 0x4EA10000u;
RTC_NOINIT_ATTR static uint32_t g_healWord;
static uint8_t s_healCount = 0;   // this boot's view of it, read once in setup
static void takeHealCount() {
    const uint32_t v = g_healWord;
    g_healWord = 0;
    s_healCount = ((esp_reset_reason() == ESP_RST_DEEPSLEEP || esp_reset_reason() == ESP_RST_SW) && (v & 0xFFFF0000u) == HEAL_MAGIC) ? (uint8_t)(v & 0xFF) : 0;
}
#endif
static WipeBoot takeWipeBoot() {
    const uint32_t v = g_wipeBoot;
    g_wipeBoot = 0;
    if (esp_reset_reason() != ESP_RST_SW) return WipeBoot::NONE;
    if ((v & 0xFFFFFF00u) != WIPEBOOT_MAGIC) return WipeBoot::NONE;
    const uint8_t m = (uint8_t)(v & 0xFF);
    if (m == (uint8_t)WipeBoot::UNLOCKED) return WipeBoot::UNLOCKED;
    if (m == (uint8_t)WipeBoot::LOCKED)   return WipeBoot::LOCKED;
    return WipeBoot::NONE;
}

static void crashReportInit() {
    const esp_reset_reason_t r = esp_reset_reason();
#if defined(TWATCH_S3)
    takeHealCount();
#endif
    g_resetReason = r;
    const bool panicked = (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
                           r == ESP_RST_TASK_WDT || r == ESP_RST_WDT);
    // A magic that survived with garbage behind it -- 31 days up and 20 MB
    // free, on a photo -- is RTC RAM that half-survived a power dip.
    if (g_crumb.magic == CRUMB_MAGIC &&
        (g_crumb.uptimeMs > 30UL * 24 * 3600 * 1000 || g_crumb.heapFree > 400000)) g_crumb.magic = 0;
    if (panicked && g_crumb.magic == CRUMB_MAGIC) {
        g_lastCrash.valid     = true;
        g_lastCrash.uptimeMs  = g_crumb.uptimeMs;
        g_lastCrash.heapFree  = g_crumb.heapFree;
        g_lastCrash.heapBlock = g_crumb.heapBlock;
        g_lastCrash.lifetime  = g_crumb.lifetime;
        g_lastCrash.screen    = g_crumb.screen;
    }
#if HAVE_COREDUMP_SUMMARY
    // The resets that write a core dump are the panics and the two watchdogs
    // that panic -- not the RTC watchdog, after which the dump in flash is an
    // earlier crash's. The summary is what the esp-coredump tool leads with.
    if (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT) {
        // On the stack: it is read once, here, and ~200 bytes of BSS for
        // the life of the board would be paying for it forever.
        esp_core_dump_summary_t s;
        if (esp_core_dump_get_summary(&s) == ESP_OK) {
            g_lastCrash.haveDump = true;
            strncpy(g_lastCrash.task, s.exc_task, sizeof g_lastCrash.task - 1);
            g_lastCrash.task[sizeof g_lastCrash.task - 1] = '\0';
            g_lastCrash.pc    = s.exc_pc;
            g_lastCrash.cause = s.ex_info.exc_cause;
            g_lastCrash.vaddr = s.ex_info.exc_vaddr;
            // The backtrace usually starts at the faulting PC itself; the
            // frames above it are the ones worth the screen space.
            uint32_t i = (s.exc_bt_info.depth && s.exc_bt_info.bt[0] == s.exc_pc) ? 1 : 0;
            uint8_t  n = 0;
            for (; i < s.exc_bt_info.depth && i < 16 && n < 4; i++) g_lastCrash.bt[n++] = s.exc_bt_info.bt[i];
            g_lastCrash.btN = n;
            char running[APP_ELF_SHA256_SZ] = { 0 };
            esp_ota_get_app_elf_sha256(running, sizeof running);
            g_lastCrash.dumpOlder =
                strncmp((const char*)s.app_elf_sha256, running, sizeof running - 1) != 0;
        }
    }
#endif
    g_crumb.magic = CRUMB_MAGIC;
}

// Once a second is plenty: this is for telling a slow heap death from a
// sudden one, and a second's resolution answers that.
static void crashCrumbTick(uint32_t now, uint32_t lifetime, uint8_t screen) {
    static uint32_t last = 0;
    if (now - last < 1000) return;
    last = now;
    g_crumb.uptimeMs  = now;
    g_crumb.heapFree  = ESP.getFreeHeap();
    g_crumb.heapBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    g_crumb.lifetime  = lifetime;
    g_crumb.screen    = screen;
}

// The crash, on the splash. DIAGNOSTICS has shown the last crash since
// v1.7.x, and a user in a boot loop cannot reach DIAGNOSTICS: the board
// dies before the menu. So on a boot after a panic the splash holds for
// nine seconds with the same lines in a box at the bottom, and a photo of
// the splash is the bug report.
static bool crashCardWanted() { return g_lastCrash.valid || g_lastCrash.haveDump; }
static const char* resetReasonName();
static void drawCrashCard(TFT_eSPI& t) {
    const int w = t.width(), h = t.height();
    const int lines = 1 + (g_lastCrash.haveDump ? 2 : (g_lastCrash.valid ? 1 : 0));
    const int bh = 8 + lines * 11;
    const int y0 = h - bh - 4;
    t.fillRoundRect(4, y0, w - 8, bh, 4, Theme::BG);
    t.drawRoundRect(4, y0, w - 8, bh, 4, Theme::RED);
    t.setTextSize(1);
    t.setTextWrap(false);
    t.setTextColor(Theme::RED, Theme::BG);
    int y = y0 + 4;
    char line[64];
    if (g_lastCrash.valid)
        snprintf(line, sizeof line, "LAST CRASH: %lum%02lus up, %lu free, %lu block",
                 (unsigned long)(g_lastCrash.uptimeMs / 60000), (unsigned long)((g_lastCrash.uptimeMs / 1000) % 60),
                 (unsigned long)g_lastCrash.heapFree, (unsigned long)g_lastCrash.heapBlock);
    else
        snprintf(line, sizeof line, "LAST RESET: %s", resetReasonName());
    t.setCursor(10, y); t.print(line); y += 11;
    if (g_lastCrash.haveDump) {
        snprintf(line, sizeof line, "IN: %s @ %08lX%s", g_lastCrash.task, (unsigned long)g_lastCrash.pc,
                 g_lastCrash.dumpOlder ? " (old fw)" : "");
        t.setCursor(10, y); t.print(line); y += 11;
        size_t o = snprintf(line, sizeof line, "BT:");
        for (uint8_t i = 0; i < g_lastCrash.btN && i < 4; i++)
            o += snprintf(line + o, sizeof line - o, " %08lX", (unsigned long)g_lastCrash.bt[i]);
        t.setCursor(10, y); t.print(line); y += 11;
    } else if (g_lastCrash.valid) {
        snprintf(line, sizeof line, "ON: screen %u, %lu detections", (unsigned)g_lastCrash.screen,
                 (unsigned long)g_lastCrash.lifetime);
        t.setCursor(10, y); t.print(line); y += 11;
    }
}
#include "state.h"
#include "theme.h"
#include "detection.h"
#include "clock.h"
#include "ui_desk.h"
#include "ui_zone.h"
#include "ui_wifinets.h"
#include "flood_bench.h"
#include "ui_boot.h"
#include "ui_clear.h"
#include "crowd_bench.h"
#include "ui_alert.h"
#include "ui_log.h"
#include "ui_rawscan.h"
#include "ui_watchalert.h"
#include "ui_settings.h"
#include "ui_diagnostics.h"
#include "ui_hunt.h"
#include "ui_colorcheck.h"
#include "detection_info.h"
#include "ui_diary.h"
#include "ui_outfit.h"
#include "ui_outfit_unlock.h"
#include "ui_ignorelist.h"
#include "frame_push.h"
#include "draw_band.h"
#include "frame_prof.h"
#include "fast_sprite.h"
#if SQUACH_MESH
#include "ui_phone.h"
#include "ui_meshmenu.h"
#include "ui_meshwarn.h"
#include "meshtalk.h"
#include "ui_meshphrase.h"
#include "ui_meshcompose.h"
#include "meshtutor.h"
#include "ui_squad.h"
#include "ui_nudge.h"
#include "ui_squadupdate.h"
#include "ui_invite.h"
#include "meshmsg.h"
#endif
#include "ignore_list.h"
#include "ignore_list.h"
#include "ui_detfilter.h"
#include "ui_beaconwarn.h"
#include "ui_power.h"
#include "security.h"
#include "ui_security.h"
#include "squachy.h"
#include "cap_touch.h"
#if defined(TWATCH_S3)
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#endif
#include "touch_cal.h"
#include "settings.h"
#include "signatures.h"
#include "ota_core.h"
#include "ota_ble.h"
#include "ota_wifi.h"
#include "ui_update.h"
#include "ui_wifipass.h"
#include "ui_sysprops.h"
#if SQUACH_MESH
#endif
#include "status_light.h"
#include "ui_light.h"

// Two CYD board variants are supported from this one firmware:
//   - jczn_2432s028r (original): resistive XPT2046 touch on its own
//     dedicated SPI bus, backlight on GPIO21. Confirmed against
//     Espressif's official board-variant file for this exact board
//     (arduino-esp32 variants/jczn_2432s028r/pins_arduino.h):
//       display: DC=2 MISO=12 MOSI=13 SCK=14 CS=15 BL=21  (VSPI, via TFT_eSPI)
//       touch:   CS=33 IRQ=36 SCK=25  MOSI=32 MISO=39      (independent bus)
//   - JC2432W328C: capacitive CST816/CST820 touch over I2C, backlight
//     on GPIO27. Confirmed empirically against a physical unit: same
//     display driver/pins as above, touch chip answers at I2C 0x15 on
//     SDA=33/SCL=32 with a reset pulse on GPIO25.
// Both variants route their touch controller through the same
// GPIO25/32/33 trio (SPI vs I2C), so probing for the I2C chip at boot
// tells us which board this is — see setup().
//   - ESP32-3248S035R (3.5", built separately as env:cyd35): resistive
//     XPT2046 again, CS=33 sharing the *display's* SPI bus (SCK/MOSI/
//     MISO = 14/13/12) instead of getting a dedicated peripheral —
//     this board has no capacitive-touch chip at all, so the I2C probe
//     below is skipped entirely rather than just failing. Raw reads go
//     through TFT_eSPI's own touch accessors, same as AWOK below, not
//     the standalone XPT2046_Touchscreen library — a separate SPIClass
//     on the same physical bus as TFT_eSPI produced constant garbage
//     reads and a free-running IRQ when that was tried.
//   - AWOK 2.4" (Marauder V6.1, built separately as env:awok): resistive
//     XPT2046 sharing the display's VSPI bus like cyd35, read the same
//     way. The constructor still gets
//     built below regardless of board (it costs nothing unused), but
//     neither AWOK nor cyd35 ever calls touch.begin() or
//     touchSPI.begin() on it.
// Boards whose resistive touch chip shares the DISPLAY's SPI bus rather than
// getting a dedicated one. Two peripherals driving one set of pins is the
// thing this avoids: the XPT2046_Touchscreen library is never begin()'d on
// any of them, and raw reads go through TFT_eSPI's own accessors.
//
// Every board's raw values then go through the same TouchFit mapping (see
// pollTouch()). The two names below survive for what still differs:
//
//   TOUCH_ON_DISPLAY_BUS  -- AWOK. Reads through the same filter TFT_eSPI's
//     getTouch() applied (rawReadFiltered()), as the 3.5" does, and may have
//     an old TFT_eSPI calibration blob to convert for SKIP.
//
//   TOUCH_RAW_SHARED_BUS  -- the RL Phantom's resistive variant. A plain
//     pressure-gated raw read, and the 2.8"-style old calibration.
#if defined(AWOK)
    #define TOUCH_ON_DISPLAY_BUS 1
#endif
#if defined(RLPHANTOM_R)
    #define TOUCH_RAW_SHARED_BUS 1
#endif
// Everything that is true of BOTH: no dedicated touch peripheral, so nothing
// ever calls touch.begin()/touchSPI.begin().
#if defined(TOUCH_ON_DISPLAY_BUS) || defined(TOUCH_RAW_SHARED_BUS)
    #define TOUCH_SHARES_DISPLAY_BUS 1
#endif

#if defined(CYD35)
    #define TOUCH_SCK  TFT_SCLK
    #define TOUCH_MOSI TFT_MOSI
    #define TOUCH_MISO TFT_MISO
#elif defined(TOUCH_SHARES_DISPLAY_BUS)
    // No dedicated touch bus on AWOK — TFT_eSPI drives touch on the
    // display's own VSPI. Values below are placeholders so the compile
    // still works; the AWOK branches skip touchSPI.begin() entirely.
    #define TOUCH_SCK  TFT_SCLK
    #define TOUCH_MOSI TFT_MOSI
    #define TOUCH_MISO TFT_MISO
#else
    #define TOUCH_SCK  25
    #define TOUCH_MOSI 32
    #define TOUCH_MISO 39
#endif
// AWOK's user setup already #defines TOUCH_CS=21 (pulled in via the
// -include in platformio.ini) — awok_user_setup.h's TFT_eSPI touch
// path needs that value armed, so this can't unconditionally redefine
// it to 33 the way the other two boards share.
#ifndef TOUCH_CS
#define TOUCH_CS   33
#endif
#define TOUCH_IRQ  36
#define CAP_SDA    33
#define CAP_SCL    32
#define CAP_RST    25
// Backlight brightness (Settings menu): all three boards' backlight
// pins are driven at boot regardless of which one is actually wired
// (see the digitalWrite(HIGH) comment in setup() — same reasoning
// applies here), each on its own LEDC channel so ledcWrite can dim
// whichever one is real without needing to know which board this is.
// AWOK's BL sits on GPIO32; the other boards' pins (21, 27) are simply
// unused GPIOs on AWOK, so driving all three is harmless.
#define BL_PIN_ORIG 21
#define BL_PIN_CAP  27
#define BL_PIN_AWOK 32
#define BL_PIN_TWATCH 45
#define BL_CH_ORIG  0
#define BL_CH_CAP   1
#define BL_CH_AWOK  2

// invertDisplay() sets an ABSOLUTE panel state -- it doesn't toggle
// relative to whatever TFT_INVERSION_ON/OFF a board's user-setup header
// set at compile time, it just overwrites it. That header define is
// therefore dead code for actually choosing a board's polarity -- a
// real bug found on real hardware (AWOK), several rounds of flipping
// the header setting with zero visible effect, before finding this is
// the actual control point. Settings::inverted() is a cosmetic
// per-user theme toggle, unrelated to a given panel's actual required
// polarity, so every invertDisplay() call XORs the two: this constant
// is the panel's own baseline, and the user's cosmetic toggle flips
// relative to it. File-scope (not local to setup()) so the Settings >
// INVERT row handler in loop() can use the same XOR instead of
// clobbering this baseline with an absolute call.
#if defined(TWATCH_S3)
// Confirmed on the watch 2026-09-22: the ST7789 wants inversion on (as
// LilyGo's own setup says); false showed every colour inverted.
constexpr bool PANEL_NEEDS_INVERSION = true;
#elif defined(FREENOVE32)
// Freenove's own setup for the 3.2" turns inversion on. UNCONFIRMED here
// until the board is looked at; INVERT in Settings flips it if wrong.
constexpr bool PANEL_NEEDS_INVERSION = true;
#elif defined(CYD35)
// UNCONFIRMED on real hardware post-fix: the original port's "true"
// guess predates discovering the override bug above, so whatever
// testing produced that value was toggling a header define that does
// nothing -- not a real confirmation. Trying false first, same as
// AWOK's ILI9341 (this panel's ST7796 may differ; adjust from live
// observation).
constexpr bool PANEL_NEEDS_INVERSION = false;
#elif defined(AWOK)
// Confirmed on real hardware: true visibly changed something (proving
// this, not the dead awok_user_setup.h define, is the actual control
// point) but looked inverted/wrong. false is the ILI9341's normal
// (non-inverted) polarity, matching the original CYD board this panel
// shares a driver with.
constexpr bool PANEL_NEEDS_INVERSION = false;
#elif defined(CROWPANEL7)
// An RGB panel has no inversion register at all; Panel_RGB's invertDisplay()
// is a no-op and CrowPanelTFT hides it. False keeps the XOR in the Settings
// handler meaningful rather than pretending a control exists.
constexpr bool PANEL_NEEDS_INVERSION = false;
#else
constexpr bool PANEL_NEEDS_INVERSION = false;
#endif

// TFT_eSprite::createSprite() no-ops (returns the existing buffer
// untouched) if the sprite is already created, so the only way to
// reuse an already-allocated buffer at a new width/height is to poke
// its own bookkeeping fields directly -- createSprite() itself does
// nothing more than this plus the calloc. Both are protected in
// TFT_eSPI/TFT_eSprite, reachable from a subclass. This only produces
// a *valid* buffer when the new w*h matches what was actually
// allocated -- true here because every rotation of a rectangular
// panel needs the same total pixel count (320x240 and 240x320 are
// both 76800 pixels), so one boot-time allocation covers every
// orientation forever and rotate never needs to free/realloc again.
// ...and it sits on FastSprite, which takes over the sprite's slow
// primitives; see fast_sprite.h.
class ResizableSprite : public FastSprite {
public:
    explicit ResizableSprite(TFT_eSPI* tft) : FastSprite(tft) {}
    void resizeInPlace(int16_t w, int16_t h) {
        if (!_created) return;
        _iwidth = _dwidth = _bitwidth = w;
        _iheight = _dheight = h;
        cursor_x = 0;
        cursor_y = 0;
        _sx = 0;
        _sy = 0;
        _sw = w;
        _sh = h;
        rotation = 0;
        setViewport(0, 0, _dwidth, _dheight);
        setPivot(_iwidth / 2, _iheight / 2);
    }
};

// ---- Globals ----
#if defined(CROWPANEL7)
// Not a TFT_eSPI: an 800x480 RGB parallel panel driven by ESP-IDF's esp_lcd, wearing
// TFT_eSPI's interface because `canvas` below upcasts the sprite to the
// device type and only this hierarchy gives the two a common base. Nothing
// is ever sent to an SPI display bus on this board -- see the header.
CrowPanelTFT        tft;
#else
TFT_eSPI            tft = TFT_eSPI();
#endif
// All screens draw into this off-screen buffer, pushed to the physical
// display in one shot at the end of each loop(). Without it, every
// screen's erase-then-redraw sequence is briefly visible on real
// hardware — most noticeable as flickering text.
//
// cyd35 exception: at 320x480 this sprite needs ~150KB contiguous heap
// (8-bit), and that board's largest free block measured ~110KB on real
// hardware (no PSRAM, and WiFi/BLE fragment the heap before setup()
// even runs) -- createSprite() reliably fails there. Rather than a
// banded/partial-height rewrite of every draw call, cyd35 skips the
// double buffer entirely and draws straight to the panel via `canvas`
// below, accepting the erase/redraw flicker the buffer normally hides.
ResizableSprite     frame = ResizableSprite(&tft);
// A pointer, not a reference: confirmed on real hardware that
// re-createSprite()'ing `frame` after a rotation can fail even with
// generous total free heap (NimBLE/WiFi churn fragments it -- see the
// rotate handler in loop()). When that happens we permanently fail
// over to drawing straight into `tft` for the rest of the session,
// same tradeoff cyd35 already accepts by default -- which needs
// `canvas` to be reseatable at runtime, not bound once at startup.
#if defined(CYD35)
    TFT_eSPI*        canvas = &tft;
#else
    TFT_eSPI*        canvas = &frame;
#endif
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
// cyd35's touch bus IS the display's bus, shared via CS rather than a
// separate peripheral — see the CYD35 touch-init branch in setup(),
// which uses tft.getSPIinstance() instead of this object. A second,
// independent SPIClass(VSPI) pointed at the same pins TFT_eSPI already
// owns fights it at the GPIO-matrix level rather than sharing cleanly;
// touchSPI here is only ever used on the original (HSPI) board.
SPIClass         touchSPI(HSPI);
// Set once in setup() by probing for the capacitive controller —
// decides which branch pollTouch() takes for the rest of the run.
bool                usingCapTouch = false;
// Set false if a post-boot frame.createSprite() ever fails (rotate —
// see loop()). cyd35's CLEAR screen checks this to fall back from its
// banded render to direct-to-tft; the other board's `canvas` pointer
// gets reseated to &tft directly at the point of failure instead.
bool                frameBufferOk = true;
DetectionEngine     engine;
AppState            state     = AppState::BOOT;
uint32_t            bootStart = 0;
uint32_t            alertStart= 0;
uint32_t            watchAlertStart = 0;
uint32_t            lastTouch = 0;
bool                prevTouchValid = false; // last frame's tp.valid, for true press/release edge detection (see loop())
DetectionType       lastAlertType = DetectionType::UNKNOWN;
Confidence          lastAlertConf = Confidence::HIGH_CONF;
uint32_t            lastAlertHits = 1; // times this exact MAC+type has ever matched — see Squachy's "seen before" reaction
int8_t              lastAlertRssi = 0; // signal strength of that hit — scales how hard Squachy reacts to it
const uint16_t      TOUCH_DEBOUNCE_MS = 200;
// The longest a touch can last and still be a tap on a list. A thumb that
// rests on a row for longer was reaching in to scroll, not choosing -- people
// kept opening rows they were only scrolling past -- so a long touch that
// never moved is spent, not acted on. Long presses that mean something (a
// LOG row's WATCH/HUNT) are timed separately and unaffected.
static const uint32_t TAP_MAX_MS = 500;

// Hidden "unlock every Squachy outfit" gesture: hold the third button (DESK)
// for CLR_UNLOCK_HOLD_MS on the CLEAR screen (see the CLEAR case's touch
// handling in loop()) -- replaces a fragile 11-tap sequence that broke once
// SCAN stopped being a no-op there. The name is from when that button was CLR.
// The ALERT screen carries real information (type, confidence, MAC,
// RSSI) — tapping it away is the expected dismiss, but the automatic
// fallback still needs to actually clear itself in a reasonable time
// if nobody's there to tap it.
const uint32_t      ALERT_AUTO_DISMISS_MS = 10000;
// Longer than an alert's: this one is a reward, not a warning, and the
// reveal animation alone eats the first second of it.
const uint32_t      OUTFIT_UNLOCK_AUTO_MS = 12000;
// TFT_eSPI rotation: all four orientations are supported (0/2 portrait,
// 1/3 landscape), cycled in order by the rotate button in the title
// bar -- except AWOK, which has no rotate button (see loop()) and
// stays fixed at its case's one physical orientation, confirmed on
// real hardware to be portrait/rotation 0.
#if defined(AWOK)
uint8_t             screenRotation = 0;
#else
uint8_t             screenRotation = 1;
#endif
// Timestamp of the last screen/rotation change — drives a brief CRT
// tear/glitch overlay on the new frame so transitions have some punch
// instead of just snapping straight to the next screen.
uint32_t            transitionStart = 0;
static const uint32_t TRANSITION_MS = 220;

// RGB/BGR color order (Settings menu): setRotation() bakes the
// compile-time TFT_RGB_ORDER into the MADCTL byte it writes, same as
// every rotation -- there's no library API to change just that one bit
// afterward, so this reissues MADCTL by hand. The 0x36/0x80/0x40/0x20/
// 0x08 values are the standard MIPI DCS MADCTL command and MY/MX/MV/BGR
// bits, confirmed identical across this project's own vendored
// TFT_eSPI driver headers for ST7789, ILI9341, and ST7796 -- not
// reachable as named macros from here (they live in each driver's own
// TFT_Drivers/<X>_Defines.h, which main.cpp has no normal include path
// to), so hardcoded directly rather than re-derived by guesswork.
static const uint8_t MADCTL_CMD = 0x36;
static const uint8_t MADCTL_MY  = 0x80;
static const uint8_t MADCTL_MX  = 0x40;
static const uint8_t MADCTL_MV  = 0x20;
static const uint8_t MADCTL_BGR = 0x08;

// Per-rotation MX/MY/MV bits (color-order bit excluded, ORed in
// separately below) -- copied verbatim from this project's vendored
// TFT_Drivers/<X>_Rotation.h for whichever driver this board actually
// uses, not re-derived, so a transcription slip can't quietly rotate
// or mirror the image on some rotation instead of just its color.
// ILI9341 and ST7796's tables happen to be identical; ST7789's is its
// own.
static uint8_t madctlRotationBits(uint8_t rotation) {
#if defined(ST7789_DRIVER)
    static const uint8_t BITS[4] = {
        0, MADCTL_MX | MADCTL_MV, MADCTL_MX | MADCTL_MY, MADCTL_MV | MADCTL_MY
    };
#else  // ILI9341_DRIVER or ST7796_DRIVER -- identical rotation table
    static const uint8_t BITS[4] = {
        MADCTL_MX, MADCTL_MV, MADCTL_MY, MADCTL_MX | MADCTL_MY | MADCTL_MV
    };
#endif
    return BITS[rotation % 4];
}

// Same XOR-against-a-compile-time-baseline convention
// PANEL_NEEDS_INVERSION/Settings::inverted() already uses, for the same
// reason: the header's TFT_RGB_ORDER is this panel's own correct
// default, and Settings::rgbSwapped() is a "no, actually swap it" user
// override on top for boards whose panel batch disagrees with the
// header's guess -- not a replacement for it. Call after every
// setRotation(), since the rotation-dependent bits above have to be
// reissued alongside it either way.
static void applyColorOrder() {
#ifdef TFT_RGB_ORDER
    bool baselineBgr = (TFT_RGB_ORDER != TFT_RGB);
#else
    bool baselineBgr = true;
#endif
    bool wantBgr = baselineBgr != Settings::rgbSwapped();
    uint8_t bits = madctlRotationBits(screenRotation) | (wantBgr ? MADCTL_BGR : 0);
    tft.writecommand(MADCTL_CMD);
    tft.writedata(bits);
}

// ---- Touch helpers ----
struct TouchPoint { bool valid; int x; int y; };

// Defined further down with the other raw readers, but pollTouch() needs it
// above them on the boards whose touch shares the display bus: there is no
// dedicated peripheral to read from, so the raw values come through here.
static bool rawReadResistive(int16_t& a, int16_t& b);

// CST816/CST820 native coordinate range — factory defaults measured
// against one specific JC2432W328C unit's 4 corners in landscape
// (rotation 1). Not const: overwritten at boot if a saved calibration
// exists (see loadOrDefaultCal()/TouchCal), and by the long-press
// calibration flow (see checkCalibrationTrigger()).
#if defined(CROWPANEL7)
// The GT911 reports panel pixels, so these make initTouchFit()'s affine map a
// plain divide by CROWPANEL_SCALE (2 here, 1 in the -native build) -- no
// calibration is needed or wanted.
static uint16_t CAP_NX_MIN = CROW_CAP_NX_MIN, CAP_NX_MAX = CROW_CAP_NX_MAX;
static uint16_t CAP_NY_MIN = CROW_CAP_NY_MIN, CAP_NY_MAX = CROW_CAP_NY_MAX;
#else
static uint16_t CAP_NX_MIN = 32,  CAP_NX_MAX = 166;
static uint16_t CAP_NY_MIN = 10,  CAP_NY_MAX = 308;
#endif
// Resistive XPT2046 raw ADC range — same idea, factory default was a
// flat 200-3800 for both axes; not const for the same reason.
static uint16_t RAW_X_MIN = 200, RAW_X_MAX = 3800;
static uint16_t RAW_Y_MIN = 200, RAW_Y_MAX = 3800;

// Minimum acceptable raw-unit spread for a calibration axis to count
// as valid -- see TouchCal::load()/runInteractive()'s header comment
// for the full reasoning. Capacitive touch's whole legitimate range is
// naturally small (factory default X span is just 134 units,
// CAP_NX_MIN/MAX above), so it needs a much looser floor than
// resistive, whose real range should span most of a 12-bit ADC (up to
// 4095). Confirmed on real hardware: a resistive calibration with a
// spread of ~180-190 -- comfortably clearing a capacitive-safe
// threshold -- was still nowhere near a real full-range calibration
// and left touch unusable.
static const int16_t CAP_TOUCH_MIN_SPREAD = 50;
static const int16_t RESISTIVE_MIN_SPREAD = 800;

#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35)
// TFT_eSPI::setTouch()'s own calibration format, as older firmware saved it
// on AWOK and the 3.5": [0] and [2] are the raw readings at the low edge of
// each axis, [1] and [3] the SPANS from there (calibrateTouch() subtracts
// before it exports), and [4] a bitflag byte where only bits 0-2
// (swap/invert_x/invert_y) mean anything -- see Touch.cpp. Checked before it
// is trusted, since an interrupted NVS write is a confirmed real-world way
// to get garbage here. Both boards are resistive, so the resistive floor.
static bool touchCalPlausible(const uint16_t* p) {
    if (p[4] > 7) return false;
    auto ok = [](uint16_t lo, uint16_t span) {
        return lo >= 1 && lo <= 4095 && span >= RESISTIVE_MIN_SPREAD && lo + span <= 4200;
    };
    return ok(p[0], p[1]) && ok(p[2], p[3]);
}
#endif

// ---- The mapping in force: raw touch -> screen, every board ----
// A TouchFit::Fit (touch_fit.h) from the five-target calibration, or, until
// a board has one, whatever it used before -- see initTouchFit() below.
// Rotation is applied after the Fit, so one calibration serves all four.
static TouchFit::Fit s_touchFit = TouchFit::fromRanges(RAW_X_MIN, RAW_X_MAX, RAW_Y_MIN, RAW_Y_MAX, 240, 320);

// Where s_touchFit came from, for the diagnostics screen and for deciding
// whether boot has to ask for a calibration.
enum class CalSource : uint8_t { BUILT_IN, OLD_SAVED, SAVED };
static CalSource s_calSource = CalSource::BUILT_IN;

#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35)
// ---- Calibrations older firmware saved through TFT_eSPI ----
// AWOK and the 3.5" used to hand touch to TFT_eSPI's calibrateTouch() /
// getTouch(). Its blob is only read now, once, so an owner who SKIPs the
// new calibration keeps the touch they had: fromTftEspi() turns it into a
// Fit. The 3.5" kept one per rotation, because the blob bakes in the
// rotation it was taken at; any one of them is enough for a Fit, which
// does not.
#if defined(CYD35)
static const char* TFT_ESPI_TOUCH_NS = "cyd35touch";
#else
static const char* TFT_ESPI_TOUCH_NS = "awoktouch";
#endif

static bool loadTftEspiBlob(const char* key, uint16_t* out) {
    Preferences p;
    p.begin(TFT_ESPI_TOUCH_NS, true);
    bool has = p.isKey(key) && p.getBytes(key, out, 5 * sizeof(uint16_t)) == 5 * sizeof(uint16_t);
    p.end();
    return has && touchCalPlausible(out);
}

// The hold-at-boot reset clears these too, or SKIP would bring back the
// very calibration the owner was trying to get rid of.
static void clearTftEspiBlobs() {
    Preferences p;
    p.begin(TFT_ESPI_TOUCH_NS, false);
    p.clear();
    p.end();
}

// Blob -> Fit. True if a usable blob was found.
static bool fitFromTftEspiBlobs(TouchFit::Fit& out) {
    // The screen size rotation `r` has, from the one we are in now.
    const int w = tft.width(), h = tft.height();
    auto dims = [&](uint8_t r, int& rw, int& rh) {
        const bool same = (r & 1) == (screenRotation & 1);
        rw = same ? w : h;
        rh = same ? h : w;
    };
    uint16_t blob[5];
#if defined(CYD35)
    for (uint8_t i = 0; i < 4; i++) {
        const uint8_t r = (uint8_t)((screenRotation + i) & 3);   // this rotation's first
        char key[8];
        snprintf(key, sizeof(key), "cal5_%u", r);
        if (!loadTftEspiBlob(key, blob)) continue;
        int rw, rh;
        dims(r, rw, rh);
        if (TouchFit::fromTftEspi(blob, r, rw, rh, out)) return true;
    }
    return false;
#else
    // AWOK has no rotate button, so its one blob was taken in the rotation
    // it is in now.
    if (!loadTftEspiBlob("cal5", blob)) return false;
    int rw, rh;
    dims(screenRotation, rw, rh);
    return TouchFit::fromTftEspi(blob, screenRotation, rw, rh, out);
#endif
}
#endif  // TOUCH_ON_DISPLAY_BUS || CYD35

static bool readTouchRaw(int16_t& a, int16_t& b);

// Raw touch -> screen, the same way on every board: the Fit gives a point
// in the panel's native (rotation 0) frame, and the rotation step turns it
// into this rotation's coordinates. No board has its own maths any more.
static TouchPoint pollTouch() {
    TouchPoint tp = { false, 0, 0 };
    int16_t a, b;
    if (!readTouchRaw(a, b)) return tp;
    const int w = tft.width(), h = tft.height();
    float sx, sy;
    TouchFit::toScreen(s_touchFit, a, b, screenRotation, sx, sy);
    // Truncated, as the map() calls this replaced were.
    tp.x = (int)sx;
    tp.y = (int)sy;
    // Clamp into bounds instead of invalidating -- the map extrapolates, it
    // doesn't clip, so a touch landing just past a calibrated edge
    // (completely normal: fingers don't land exactly on the pixel a
    // calibration sampled) used to read as "no touch at all" rather than
    // "slightly imprecise at the edge". Confirmed on real hardware that even
    // a technically-valid calibration could leave EVERY touch just outside
    // bounds and the whole screen unresponsive, since a rejected touch and
    // no touch look identical downstream. A hard sanity cap (2x the screen
    // dimension either direction) still rejects genuinely wild readings.
    bool sane = tp.x > -w && tp.x < 2 * w && tp.y > -h && tp.y < 2 * h;
    if (sane) {
        if (tp.x < 0) tp.x = 0; else if (tp.x >= w) tp.x = w - 1;
        if (tp.y < 0) tp.y = 0; else if (tp.y >= h) tp.y = h - 1;
    }
    tp.valid = sane;
    return tp;
}

// ---- Raw readers ----
static bool rawReadCap(int16_t& a, int16_t& b) {
    uint16_t nx, ny;
    if (!CapTouch::read(nx, ny)) return false;
    a = (int16_t)nx;
    b = (int16_t)ny;
    return true;
}

static bool rawReadResistive(int16_t& a, int16_t& b) {
#if defined(TOUCH_SHARES_DISPLAY_BUS) || defined(CYD35)
    // None of these boards' `touch` (XPT2046_Touchscreen) object is ever
    // begin()'d -- a second SPI driver on the display's own bus produced
    // garbage -- so this goes through TFT_eSPI's raw-touch accessors.
    // getTouchRaw() alone always returns true in TFT_eSPI 2.5.43, so the
    // actual "is a finger down" gate is the pressure threshold.
    if (tft.getTouchRawZ() < 350) return false;
    uint16_t rx, ry;
    tft.getTouchRaw(&rx, &ry);
    a = (int16_t)rx;
    b = (int16_t)ry;
    return true;
#else
    if (!touch.tirqTouched() || !touch.touched()) return false;
    TS_Point p = touch.getPoint();
    a = (int16_t)p.x;
    b = (int16_t)p.y;
    return true;
#endif
}

#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35)
// AWOK and the 3.5" used TFT_eSPI's getTouch(), whose filtering is part of
// how their touch feels. It is private to the library (validTouch()), so it
// is repeated here step for step: wait for the pressure to stop rising,
// then two reads that must agree within _RAWERR (20).
//
// A cheap pressure read comes FIRST. The debounce loop pays a delay(1) per
// turn even when nobody is touching, and on the 3.5" that measured 12.9 ms
// of a 106 ms frame -- more than a tenth of the device's time asking a
// screen nobody was touching. Same 600 threshold the library uses, so a
// touch it would have seen is a touch this sees.
static bool rawReadFiltered(int16_t& a, int16_t& b) {
    const uint16_t THRESHOLD = 600, RAWERR = 20;
    if (tft.getTouchRawZ() <= THRESHOLD) return false;
    uint16_t z1 = 1, z2 = 0;
    while (z1 > z2) { z2 = z1; z1 = tft.getTouchRawZ(); delay(1); }
    if (z1 <= THRESHOLD) return false;
    uint16_t x1, y1, x2, y2;
    tft.getTouchRaw(&x1, &y1);
    delay(1);
    if (tft.getTouchRawZ() <= THRESHOLD) return false;
    delay(2);
    tft.getTouchRaw(&x2, &y2);
    if (abs((int)x1 - (int)x2) > RAWERR || abs((int)y1 - (int)y2) > RAWERR) return false;
    a = (int16_t)x1;
    b = (int16_t)y1;
    return true;
}
#endif

// The one reader pollTouch(), the calibration and the diagnostics screen all
// use, so what the calibration measures is exactly what touch then reads.
static bool readTouchRaw(int16_t& a, int16_t& b) {
#if defined(CROWPANEL7)
    // The GT911 already reports panel pixels; TouchFit divides by the scale,
    // so nothing else differs.
    uint16_t x, y;
    if (!Gt911::read(x, y)) return false;
    a = (int16_t)x; b = (int16_t)y;
    return true;
#else
    if (usingCapTouch) return rawReadCap(a, b);
#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35)
    return rawReadFiltered(a, b);
#else
    return rawReadResistive(a, b);
#endif
#endif
}

// Set by the power-saver timer in loop(). Kept out of Settings on purpose:
// it is a live state, not a preference, and it must never be persisted --
// coming back from a reboot into a dimmed screen with no memory of why would
// look exactly like a broken backlight.
static bool s_screenDimmed = false;
#if defined(TWATCH_S3)
// A watch goes dark, not dim. When the screen timeout lands, the backlight
// goes off and the ST7789 is put to sleep (DISPOFF, SLPIN: about a milliamp
// instead of tens), and pushFrame() stops sending frames it would not show.
// A tap or the crown wakes it: SLPOUT needs 120 ms before DISPON, which is
// the one delay a person can feel here, and it is once per wake.
static bool s_panelAsleep = false;
#if defined(TWATCH_S3)
// A crown press with the screen on turns it off at once, cable or not.
// It stays off until the next touch, crown press or alert: lastTouch
// moving past this moment is what ends it.
static bool     s_crownDark   = false;
static uint32_t s_crownDarkAt = 0;
// After an alert lights a crown-darkened screen, it stays lit until this
// moment -- the screen timeout, counted from the alert's end -- then goes
// dark again. 0 = no alert has lit it.
static uint32_t s_crownLitUntil = 0;
// On the cable, read every two seconds in twatchRadioTick(). The screen
// stays lit while it is true: a watch on its charger is a desk clock.
static bool     s_onUsb = true;
#endif
static bool s_radiosResting = false;   // the duty cycle's state; see twatchRadioTick()
#endif

static void applyBrightness() {
    uint8_t duty = s_screenDimmed ? Settings::dimLevel() : Settings::brightness();
#if defined(TWATCH_S3)
    if (s_screenDimmed) {
        if (!s_panelAsleep) {
            ledcWrite(BL_CH_ORIG, 0);
            tft.writecommand(0x28);   // DISPOFF
            tft.writecommand(0x10);   // SLPIN
            s_panelAsleep = true;
            Serial.println("[panel] asleep");
        }
        return;
    }
    if (s_panelAsleep) {
        tft.writecommand(0x11);   // SLPOUT
        delay(120);
        tft.writecommand(0x29);   // DISPON
        s_panelAsleep = false;
        FramePush::invalidate();
        Serial.println("[panel] awake");
    }
#endif
#if defined(CROWPANEL7)
    // No backlight GPIO on this board: an STC8H1K28 helper MCU owns it and
    // takes a bare I2C command byte on an inverted scale. CrowBL::set()
    // keeps the firmware's own 0..255-with-255-brightest convention.
    CrowBL::set(duty);
#else
    ledcWrite(BL_CH_ORIG, duty);
    ledcWrite(BL_CH_CAP,  duty);
    ledcWrite(BL_CH_AWOK, duty);
#endif
}

// 240, 160 or 80 MHz. Never lower: the radio needs an 80 MHz APB clock, and
// the display's SPI divisor and the console's baud divisor are both derived
// from it, so going under would take out scanning, the panel and the serial
// console in one move. Only called when the value actually changes --
// setCpuFrequencyMhz() reconfigures peripherals, so calling it every frame
// would be both wasteful and a good way to find a driver's re-entrancy bug.
static uint16_t s_cpuMhzApplied = 240;
static void applyCpuClock() {
    const uint16_t want = Settings::cpuMhz();
    if (want == s_cpuMhzApplied) return;
    setCpuFrequencyMhz(want);
    s_cpuMhzApplied = want;
}

static int16_t minSpread() {
    return usingCapTouch ? CAP_TOUCH_MIN_SPREAD : RESISTIVE_MIN_SPREAD;
}

// Loads the saved Fit, or builds the mapping this board used before the
// five-target calibration existed, so a board that has not calibrated yet
// (or skips it) behaves exactly as it did.
//   - 2.8" boards and the Phantom: their old min/max calibration if one is
//     saved, else the compiled-in ranges. The old pollTouch() maths was
//     exactly fromRanges() plus rotation; test/touchfit_test.cpp checks it.
//   - AWOK and 3.5": their old TFT_eSPI blob, converted.
static void initTouchFit() {
    const bool portrait = (screenRotation & 1) == 0;
    const int w0 = portrait ? tft.width() : tft.height();
    const int h0 = portrait ? tft.height() : tft.width();

    TouchFit::Fit saved;
    if (TouchCal::loadFit(saved) && saved.w0 == w0 && saved.h0 == h0) {
        s_touchFit = saved;
        s_calSource = CalSource::SAVED;
        Serial.println("Loaded saved touch calibration.");
        return;
    }

    s_calSource = CalSource::BUILT_IN;
#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35)
    TouchFit::Fit old;
    if (fitFromTftEspiBlobs(old)) {
        s_touchFit = old;
        s_calSource = CalSource::OLD_SAVED;
        Serial.println("Touch: using the calibration older firmware saved.");
        return;
    }
#endif
    TouchCal::Cal cal;
    if (TouchCal::load(cal, minSpread())) {
        // How older firmware's applyCal() read the four numbers.
        if (usingCapTouch) {
            CAP_NX_MAX = (uint16_t)cal.aTop;  CAP_NX_MIN = (uint16_t)cal.aBottom;
            CAP_NY_MIN = (uint16_t)cal.bLeft; CAP_NY_MAX = (uint16_t)cal.bRight;
        } else {
            RAW_X_MAX = (uint16_t)cal.aTop;   RAW_X_MIN = (uint16_t)cal.aBottom;
            RAW_Y_MIN = (uint16_t)cal.bLeft;  RAW_Y_MAX = (uint16_t)cal.bRight;
        }
        s_calSource = CalSource::OLD_SAVED;
        Serial.println("Touch: using the calibration older firmware saved.");
    }
    s_touchFit = usingCapTouch
        ? TouchFit::fromRanges(CAP_NX_MIN, CAP_NX_MAX, CAP_NY_MIN, CAP_NY_MAX, w0, h0)
        : TouchFit::fromRanges(RAW_X_MIN, RAW_X_MAX, RAW_Y_MIN, RAW_Y_MAX, w0, h0);
}

// esp_reset_reason() as a short, human-readable string -- the
// diagnostics screen's answer to "did it actually crash", the exact
// question a live serial monitor was needed for earlier today.
static const char* resetReasonName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "PANIC/crash";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep wake";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "unknown";
    }
}

// ---- State transitions ----
#if defined(CYD35)
// BOOT/CLEAR/ALERT all share the one half-height `frame` sprite for
// their two-pass rendering (see each state's case in loop()), but each
// screen's own xxxInit() clears *canvas -- the real display for this
// board, not `frame` itself -- so it never actually touches the
// buffer the two-pass rendering reads from. Harmless back when only
// CLEAR ever used `frame` (its own content was always self-consistent
// frame to frame), but once BOOT/ALERT started reusing that same
// buffer, switching screens could leave one screen's leftover pixels
// sitting in whatever region the next screen's own drawing never
// touches (confirmed on real hardware: CLEAR's background animation
// stops short of the button bar row's gaps, so BOOT's last frame was
// showing through there as colored noise after switching to CLEAR).
// One full clear of the real physical buffer on entry to any of these
// three screens is enough -- nothing after that leaves stray pixels
// behind on its own.
static void clearSharedFrameBuffer() {
    if (frameBufferOk) frame.fillRect(0, 0, frame.width(), frame.height(), Theme::BG);
}
#endif

// Long-press-to-watch/hunt confirmation -- see the LOG and RAWSCAN
// cases in loop(). Owned here rather than in ui_log.cpp/ui_rawscan.cpp
// so main.cpp can decide what WATCH/HUNT actually do (call
// DetectionEngine::watchBle/watchWifi/huntBle/huntWifi) without those
// modules needing to know about DetectionEngine's tracking API at all,
// just how to draw/hit-test the panel they're given. Shared by both
// screens since only one can ever be showing at a time.
static bool    s_confirmPending = false;
static uint8_t s_confirmMac[6];
// Which DEVICE the MORE INFO page is about, not just which type: the label
// and name off the log entry, which is what device_info.cpp matches on. The
// alert keeps its own copy for when its MORE INFO is tapped.
static char s_confirmVendor[16] = "";
static char s_confirmName[sizeof(Detection::name)]     = "";
static char s_alertVendor[16]   = "";
static char s_alertName[sizeof(Detection::name)]       = "";
static char    s_confirmLabel[24];
// LOG's long-press sets this per-row (BLE vs WiFi isn't implied by a
// "current mode" the way it is for RAWSCAN, which already knows that
// from s_rawScanIsBle) -- RAWSCAN's own WATCH/HUNT branches don't
// touch this, only LOG's do.
static bool    s_confirmIsBle = true;
static bool s_alertLastFree = false;

// Whether a sighting may take the screen, and AUTO SNOOZE's bookkeeping
// with it. Call it once for each alert about to be raised and obey the
// answer -- it counts, so asking twice spends two of the device's allowance.
//
// The WATCH target is exempt outright. Asking to be told about one device is
// the clearest statement of intent this board takes, and a feature whose
// whole job is to interrupt less must never be the thing that overrides it.
#if defined(TWATCH_S3)
static bool twatchStill();
#endif
static bool alertMayInterrupt(const Detection& d) {
    const bool exempt = engine.isWatched(d.mac, true) || engine.isWatched(d.mac, false);
#if defined(TWATCH_S3)
    const bool still = twatchStill();
#else
    const bool still = false;
#endif
    const DetectionEngine::AlertGate g = engine.alertGate(d.mac, Settings::autoQuietAfter(), exempt, still);
#if defined(TWATCH_S3)
    if (g == DetectionEngine::AlertGate::HOLD)
        Serial.printf("[snooze] %02x:%02x held, watch %s\n", d.mac[4], d.mac[5], still ? "still" : "moving");
#endif
    switch (g) {
        case DetectionEngine::AlertGate::HOLD:       return false;
        case DetectionEngine::AlertGate::ALLOW_LAST: s_alertLastFree = true;  return true;
        default:                                     s_alertLastFree = false; return true;
    }
}

// The current alert's target, captured in enterAlert(). Kept separate
// from the s_confirm* trio above on purpose: those belong to LOG's
// long-press confirm panel, and an alert arriving while that panel is
// open would otherwise redirect a WATCH/HUNT the user was part-way
// through choosing at a completely different device.
static uint8_t s_alertMac[6];
static char    s_alertLabel[24];
static bool    s_alertIsBle = true;
// Captured alongside mac/label at row-hold time (LOG) or straight from
// the current alert (ALERT, see enterAlert()) so MORE INFO knows what
// to explain without needing the original Detection* to still be valid
// by the time it's tapped. RAWSCAN's own results aren't necessarily
// matched to any known type at all, so its panel has no INFO button
// and never touches this.
static DetectionType s_confirmType = DetectionType::UNKNOWN;

// The MORE INFO explanation panel -- opened by LOG's confirm panel or
// ALERT's own MORE INFO button (mutually exclusive with LOG's confirm
// panel and with everything else on whichever screen is showing it,
// since only one state is ever active). s_infoShowingPrimer tracks
// which of the (at most) two pages is up: the one-time RSSI/confidence
// primer first if it's never been shown (see Settings::infoPrimerShown()),
// then s_confirmType's own explanation either way.
static bool          s_infoPending       = false;
static bool          s_infoShowingPrimer = false;
// Same "ignore the touch that opened this" gate s_confirmArmed uses,
// applied to the info panel's own GOT IT button.
static bool          s_infoArmed         = false;
// True once the touch that triggered the long-press has actually been
// released. The confirm panel appears mid-hold (finger still down at
// whatever row/result was long-pressed), and without this gate the
// panel's own tap handling -- which only checks "is a touch down past
// the debounce window", not "did a NEW touch just start" -- could fire
// immediately using that still-held position, landing on whichever
// button happens to sit under it. Reset to false every time a fresh
// long-press opens the panel; only tap handling that runs after this
// flips true is ever allowed to register a WATCH/HUNT/CANCEL tap.
static bool    s_confirmArmed = false;

// Every way into touch calibration goes through here: first boot, the
// title-bar long-press, and Settings > CALIBRATE TOUCH. Callers decide where
// to go afterwards, which is the only thing that ever differed between them.
//
// Drawn straight onto tft, not *canvas -- canvas is an offscreen sprite on
// most boards and nothing in the flow pushes it. The frame push remembers
// what it last sent, so it is told the panel changed under it.
//
// SKIP (offered whenever there is a mapping to fall back on) keeps the
// current one, and saves it as the calibration so first boot never asks
// again; from SETTINGS it is simply a cancel.
// The compiled-in ranges are a 2.8" board's. Anywhere else -- the digitisers
// on the display's own bus -- they put taps nowhere near the finger, so a
// board with nothing better has no SKIP to offer.
#if defined(TOUCH_SHARES_DISPLAY_BUS) || defined(CYD35) || defined(TWATCH_S3)
static const bool DEFAULT_TOUCH_USABLE = false;
#else
static const bool DEFAULT_TOUCH_USABLE = true;
#endif

static bool s_calRanThisBoot = false;   // for the RADIO report on the watch
static void runTouchCalibration() {
    s_calRanThisBoot = true;
    TouchFit::Fit fit;
#if defined(TWATCH_S3)
    TouchCal::setDensityScale(1.6f);   // 240 px across 27 mm, against the 2.8" board's 5.6 px/mm
#endif
    const bool canSkip = s_calSource != CalSource::BUILT_IN || DEFAULT_TOUCH_USABLE;
    const TouchCal::Outcome r =
        TouchCal::runInteractive(tft, readTouchRaw, screenRotation,
                                 canSkip ? &s_touchFit : nullptr, minSpread(),
                                 Theme::BG, Theme::WHITE, Theme::CYAN, fit);
    if (r == TouchCal::Outcome::SAVED) {
        TouchCal::saveFit(fit);
        s_touchFit = fit;
        s_calSource = CalSource::SAVED;
    } else if (r == TouchCal::Outcome::SKIPPED && s_calSource != CalSource::SAVED) {
        TouchCal::saveFit(s_touchFit);
        s_calSource = CalSource::SAVED;
    }
    FramePush::invalidate();
    // The finger that just did all that counts as a touch: lastTouch was
    // still 0 from boot, so with the power saver on, a calibration longer
    // than the screen timeout came back to a screen already dimmed.
    lastTouch = millis();
}

static void enterBoot() {
    state = AppState::BOOT;
    bootStart = millis();
    transitionStart = bootStart;
    uiBootInit(*canvas);
#if defined(CYD35)
    clearSharedFrameBuffer();
#endif
}

// True while CLEAR's SCAN button has swapped the bottom bar to the
// [BLE][WIFI][BACK] picker (see the CLEAR case's touch handling in
// loop()). Reset on every enterClear() so returning here from
// anywhere else never leaves a stale picker showing.
static bool s_scanPickerOpen = false;

static void enterLocked();
// The frame buffer lent to a WiFi download, and taken back; defined with the update code below.
static void lendFrameToDownload();
static void restoreFrameBuffer();
// Every way home goes through here, which makes it the one place the lock has
// to be honoured: while locked, "home" is the lock screen.
// An ALERT opened from the desk's small card goes back to the desk when it
// is dismissed, not to CLEAR. Set in enterAlert(), spent here.
static bool s_backToDesk = false;
static void enterDesk();

// The screens that run the raw scan, which switches detection off while it
// runs. Leaving one by anything but its own BACK -- the lock icon, the gear,
// an auto-lock -- has to stop it, or detection stays off until a restart.
static bool onRawScanScreen() {
    return state == AppState::RAWSCAN || state == AppState::WIFI_ADD
#if SQUACH_MESH
        || state == AppState::SQUAD_UPDATE
#endif
        ;
}
static void enterClear() {
    if (Security::locked()) { enterLocked(); return; }
    restoreFrameBuffer();   // lent to a download that did not end in a restart
    if (s_backToDesk) { s_backToDesk = false; enterDesk(); return; }
    Settings::deskActive(false);
    Theme::releaseClockBackdrop();   // the clock fire's heat, if the desk had one
    state = AppState::CLEAR;
    transitionStart = millis();
    s_scanPickerOpen = false;
    uiClearInit(*canvas);
#if defined(CYD35)
    clearSharedFrameBuffer();
#endif
}

static uint32_t outfitUnlockStart = 0;

static void enterOutfitUnlock(uint8_t idx) {
    state = AppState::OUTFIT_UNLOCK;
    outfitUnlockStart = millis();
    transitionStart = outfitUnlockStart;
    uiOutfitUnlockInit(*canvas, idx);
}

// The companion's card. Same screen state and the same dismiss path -- it is
// the same card in a different mode, so there is nothing here for the rest
// of the loop to learn.
static void enterPetUnlock() {
    state = AppState::OUTFIT_UNLOCK;
    outfitUnlockStart = millis();
    transitionStart = outfitUnlockStart;
    uiPetUnlockInit(*canvas);
}

// Pops the celebration if Squachy has earned anything that has not been
// shown yet. Polled from CLEAR rather than pushed from the unlock sites:
// an outfit can be earned mid-ALERT (the detection that crossed the
// threshold is the one being alerted about) or from the werewolf summon
// on the FIRE background, and CLEAR is the one screen both of those
// paths land back on. Returns true if the screen changed.
static bool maybeEnterOutfitUnlock() {
    uint8_t idx;
    if (Squachy::consumeOutfitUnlock(idx)) { enterOutfitUnlock(idx); return true; }
    // The pet after the outfits, not before: catching the lil guy can only
    // earn the companion, but a detection that crosses a threshold at the
    // same moment would have its costume pushed behind a card it has nothing
    // to do with. Whichever is left is picked up on the next poll.
    if (Squachy::consumePetUnlockCard()) { enterPetUnlock(); return true; }
    return false;
}

// The alert card takes no touch until the finger that opened it has lifted.
// Opened by a press-and-hold on NEARBY, the card appeared under a finger that
// was still down, and the first frame read that as a tap and closed it.
static bool s_alertArmed = true;

// Squachy's line for a first-of-its-kind catch, said once he is back on the
// main screen: his bubble lives there, not on the card.
static char s_firstLine[64] = "";

// Every catch Squachy hears about goes through here, so the things the
// engine's numbers do not carry -- the device's neighbour-name, whether the
// type is his nemesis, whether it came closer than ever -- reach him too.
static void squachyCatch(DetectionType type, const uint8_t* mac, uint32_t hits, int8_t rssi, Confidence conf) {
    Squachy::catchContext(Regulars::nameFor(mac), Regulars::takeNewRegular(mac),
                          Dex::nemesis(engine) == type, Dex::takeNewClosest(type));
    Squachy::trigger(Squachy::Event::DETECTION, type, engine.lifetimeTotal(), hits, rssi, conf);
}

#if defined(TWATCH_S3)
enum class Buzz : uint8_t { ALERT, WATCH, SAMPLE };
static void twatchBuzz(Buzz kind);
#endif
static void enterAlert(const Detection& d) {
    s_backToDesk = (state == AppState::DESK);
    state = AppState::ALERT;
#if defined(TWATCH_S3)
    twatchBuzz(Buzz::ALERT);
#endif
    // FIRST. uiAlertInit() clears the card's banner flags, and it used to run
    // at the END of this function -- after the two uiAlertSet* calls below --
    // so it wiped them both every time. FIRST OF ITS KIND and AT NIGHT have
    // therefore never once appeared on an alert card. The spoken line still
    // worked, which is presumably why nobody noticed the banner was missing.
    s_infoPending = false;
    uiAlertInit(*canvas, d);
    // The lifetime count for the type includes this one, so one means first.
    {
        const bool first = engine.lifetimeTypeCount(d.type) == 1;
        uiAlertSetFirst(first);
        // The hour, from the real clock: a tracker at two in the morning
        // is a different thing from one at lunch, and the card says so.
        const bool night = Clock::night();
        uiAlertSetNight(night);
        uiAlertSetLastFree(s_alertLastFree);
        if (night && !first) {
            static const char* const NIGHT_LINES[] = {
                "A %s at this hour. That's not nothing.", "%s. At night. I don't love it.",
                "Who runs a %s past midnight? Noted.",   "A %s while the street's asleep. Hm.",
            };
            snprintf(s_firstLine, sizeof s_firstLine, NIGHT_LINES[random(0, 4)],
                     detectionTypeName(d.type));
        }
        if (first) {
            static const char* const FIRST_LINES[] = {
                "A %s! Never had one of those.", "First %s ever. Mark the date.",
                "New one for the book: %s.",     "A %s. So that's what they look like.",
            };
            snprintf(s_firstLine, sizeof s_firstLine, FIRST_LINES[random(0, 4)],
                     detectionTypeName(d.type));
        }
    }
    s_alertArmed = false;
    alertStart = millis();
    transitionStart = alertStart;
    lastAlertType = d.type;
    snprintf(s_alertVendor, sizeof s_alertVendor, "%s", vendorText(d));
    memcpy(s_alertName,   d.name,   sizeof s_alertName);
    lastAlertConf = d.conf;
    lastAlertHits = d.hits;
    lastAlertRssi = d.rssi;
    // Copied rather than kept as a Detection* -- the log is a ring
    // buffer that keeps being written while the alert is up, so the
    // entry this came from can be overwritten before HUNT is tapped.
    memcpy(s_alertMac, d.mac, 6);
    s_alertIsBle = (d.channel == 0);   // same discriminator LOG's long-press uses
    {
        const char* lbl = d.name[0] ? d.name : vendorText(d);
        strncpy(s_alertLabel, lbl, sizeof(s_alertLabel) - 1);
        s_alertLabel[sizeof(s_alertLabel) - 1] = 0;
    }
#if defined(CYD35)
    clearSharedFrameBuffer();
#endif
}

static void enterWatchAlert() {
    state = AppState::WATCH_ALERT;
#if defined(TWATCH_S3)
    twatchBuzz(Buzz::WATCH);
#endif
    watchAlertStart = millis();
    transitionStart = watchAlertStart;
    uiWatchAlertInit(*canvas);
#if defined(CYD35)
    clearSharedFrameBuffer();
#endif
}

static void enterLog() {
    state = AppState::LOG;
    transitionStart = millis();
    s_confirmPending = false;
    s_infoPending    = false;
    uiLogInit(*canvas);
}

static bool    s_rawScanIsBle = true;

static void enterRawScan(bool isBle) {
    state = AppState::RAWSCAN;
    transitionStart = millis();
    s_rawScanIsBle = isBle;
    s_confirmPending = false;
    if (isBle) engine.startRawBleScan();
    else       engine.startRawWifiScan();
    uiRawScanInit(*canvas, isBle);
}

static void enterSettings() {
    restoreFrameBuffer();   // lent to a download that did not end in a restart
    Settings::deskActive(false);
    Theme::releaseClockBackdrop();
    state = AppState::SETTINGS;
    transitionStart = millis();
    uiSettingsInit(*canvas);
}

static void enterDesk() {
    Settings::deskActive(true);
    state = AppState::DESK;
    transitionStart = millis();
    uiDeskInit(*canvas);
}

static void enterDiagnostics() {
    state = AppState::DIAGNOSTICS;
    transitionStart = millis();
    OtaCore::refreshOther();    // reads flash: once on the way in, not per frame
    uiDiagnosticsInit(*canvas);
}

static void enterUpdate() {
    Theme::releaseClockBackdrop();   // the download wants every byte
    state = AppState::UPDATE;
    transitionStart = millis();
    uiUpdateInit(*canvas);
}

#if SQUACH_MESH
// ---- the squad update -----------------------------------------------------
// A nudge heard on the mesh waits here until the main screen is showing,
// then becomes the countdown. Three minutes and it is forgotten: a nudge
// from a while ago is not something to act on when a menu finally closes.
static MeshTalk::NudgeIn s_nudge = {};
static bool              s_nudgePending = false;
static const uint32_t    NUDGE_HOLD_MS  = 180000;
static const uint16_t    NUDGE_COUNT_S  = 30;

// The update a nudge started, driven from the UPDATE state's tick in place
// of the taps the manual flow takes. The shared network is used once and
// wiped the moment it has been handed to the radio.
static struct {
    bool     active = false, connected = false, installed = false, haveCreds = false;
    uint32_t failAt = 0;
    char     ssid[MeshMsg::WIFI_SSID_MAX + 1] = "";
    char     pass[MeshMsg::WIFI_PASS_MAX + 1] = "";
} s_auto;

static void enterNudge() {
    state = AppState::NUDGE;
    transitionStart = millis();
    lastTouch = transitionStart;     // undims a sleeping screen, like an alert
    if (s_screenDimmed) { s_screenDimmed = false; applyBrightness(); }
    uiNudgeInit(*canvas, s_nudge.from, s_nudge.ver, NUDGE_COUNT_S, transitionStart);
}

static void enterBingo() {
    state = AppState::BINGO;
    transitionStart = millis();
    uiBingoInit(*canvas);
}

static void enterDex() {
    state = AppState::DEX;
    transitionStart = millis();
    uiDexInit(*canvas);
}

static void enterSquadUpdate() {
    state = AppState::SQUAD_UPDATE;
    transitionStart = millis();
    // The scan behind SHARE WIFI: which saved network is in this room. Mesh
    // keeps its radio through a WiFi scan (only an update takes that), so
    // the nudge can still go out while this is running.
    engine.startRawWifiScan();
    uiSquadUpdateInit(*canvas);
}

static void enterInvite() {
    state = AppState::INVITE;
    transitionStart = millis();
    lastTouch = transitionStart;
    if (s_screenDimmed) { s_screenDimmed = false; applyBrightness(); }
    uiInviteInit(*canvas);
}

// INVERT and ROT on the console -- see clock.cpp. Consumed in loop().
volatile bool g_consoleInvert = false;
volatile bool g_consoleRotate = false;
volatile bool g_consoleRadioTest = false; // RADIO TEST: cycle even on the cable with the screen on (bench)
volatile bool g_consoleBatt    = false;   // BATT: one reading, now
volatile bool g_consoleBattLog = false;   // BATTLOG: every sample kept, newest first
volatile bool g_consoleHeal = false;  // RADIO HEAL: take the self-heal path now, as if deaf (watch)
volatile bool g_consoleXtal = false;   // XTAL: CLOCK CHECK from the console (watch)
volatile bool g_consoleMotion = false; // MOTION: the motion sensor's reading and verdict (watch)
volatile bool g_consoleBuzz = false;  // BUZZ: play the alert pattern now and report the driver (watch)
volatile bool g_consoleRtc = false;   // RTC: read the clock chip (watch)
volatile bool g_consolePmu = false;   // PMU: dump the power chip's registers (watch)

#ifdef BENCH_TOOLS
// UPDATE NOW and UPDATE STOP on the console, for the bench: the unattended
// version of the update the squad nudge does, and of the CANCEL button.
// Bench builds only.
volatile bool g_benchUpdateNow  = false;
volatile bool g_benchUpdateStop = false;
#endif

// Why OtaWifi::begin() said no. It refuses while the board is locked, and
// also while a cancelled attempt's task is still unwinding the request it was
// waiting on -- a second or two, and nothing the owner did wrong.
static const char* updateRefusedWhy() {
    return Security::locked() ? "Unlock the board first" : "Try again in a moment";
}

static void startNudgedUpdate() {
    s_auto = {};
    if (!OtaWifi::hasSaved())
        s_auto.haveCreds = MeshTalk::takeNudgeWifi(s_nudge, s_auto.ssid, s_auto.pass);
    if (!OtaWifi::hasSaved() && !s_auto.haveCreds) {
        Theme::showToast("NO WIFI TO USE", "Do one WiFi update by hand first", Theme::AMBER);
        enterClear();
        return;
    }
    enterUpdate();
    engine.startUpdateRadio();
    if (!OtaWifi::begin()) {
        engine.stopUpdateRadio();
        memset(s_auto.pass, 0, sizeof s_auto.pass);
        Theme::showToast("CAN'T START UPDATE", nullptr, Theme::AMBER);
        enterClear();
        return;
    }
    s_auto.active = true;
    Serial.printf("[nudge] updating on %s's word, network %s\n", s_nudge.from,
                  OtaWifi::hasSaved() ? "saved" : "shared");
}

// The taps the manual flow would make, made by the tick instead.
static void autoUpdateTick(uint32_t now) {
    if (!s_auto.active) return;
    const OtaWifi::State ws = OtaWifi::state();
    if (ws == OtaWifi::State::PICK && !s_auto.connected) {
        s_auto.connected = true;
        if (OtaWifi::hasSaved()) OtaWifi::connectSaved();
        else                     OtaWifi::connect(s_auto.ssid, s_auto.pass, false);
        memset(s_auto.pass, 0, sizeof s_auto.pass);
    } else if (ws == OtaWifi::State::READY && !s_auto.installed) {
        s_auto.installed = true;
        if (OtaWifi::upToDate()) {
            // The nudge said newer; the site disagrees. Nothing to do, and
            // Bluetooth is already gone, so this restarts the board.
            s_auto.active = false;
            if (!OtaWifi::end()) { engine.stopUpdateRadio(); enterClear(); }
        } else {
            MeshTalk::markNudged();
            lendFrameToDownload();
            OtaWifi::install();
        }
    } else if (ws == OtaWifi::State::FAILED) {
        // On screen for a minute, then back to work on the old version.
        if (!s_auto.failAt) s_auto.failAt = now;
        else if (now - s_auto.failAt > 60000) {
            s_auto.active = false;
            if (!OtaWifi::end()) { engine.stopUpdateRadio(); enterClear(); }
            else uiUpdateInit(*canvas);
        }
    } else if (ws == OtaWifi::State::OFF && !OtaCore::restartPending()) {
        s_auto.active = false;      // somebody tapped CANCEL
    }
}
#endif

// The frame buffer, handed back: 77 KB at 320x240, the biggest single
// allocation on the heap. The duress wipe does it because the board is
// about to restart and the copying the wipe does ran the heap dry without
// it. Afterwards the screens draw straight to the panel, the fallback a
// failed rotate already uses.
static void releaseFrameBuffer(const char* who) {
#if !defined(CYD35)
    if (!frameBufferOk) return;
    frame.deleteSprite();
    frameBufferOk = false;
    canvas = &tft;
    tft.fillScreen(Theme::BG);
    Serial.printf("[%s] frame buffer released: largest block %lu\n", who,
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#else
    (void)who;
#endif
}

// A WiFi download lends it out too. The buffer went back to being kept
// through updates in v1.12.0, once plain HTTP had done away with the one
// 17 KB block TLS wanted -- but it is not one block the download needs, it
// is room: the WiFi driver and the TCP stack take every packet out of the
// heap, and by v1.19.0 the board had 18 KB free and a 6 KB largest block
// the moment the download began. The first 16-30 KB arrived, then nothing
// could be received, and the connection closed. So the buffer is lent from
// INSTALL until the board is back on an ordinary screen; the update screen
// already knows how to draw without it. A successful update restarts; a
// failed or cancelled one gets the buffer back on its way out, or, if the
// heap will not give a 77 KB block again, draws unbuffered until a restart
// (the same fallback a failed rotate takes).
static bool s_frameLent = false;
static void lendFrameToDownload() {
    if (!frameBufferOk) return;
    releaseFrameBuffer("ota");
    s_frameLent = true;
}
static void restoreFrameBuffer() {
    if (!s_frameLent) return;
    s_frameLent = false;
#if !defined(CYD35)
    if (frameBufferOk) return;
    frame.setColorDepth(8);
#if defined(CROWPANEL7) && CROWPANEL_FRAME_INTERNAL
    heap_caps_malloc_extmem_enable(1u << 20);   // back into internal RAM, see setup()
#endif
    const bool back = frame.createSprite(tft.width(), tft.height());
#if defined(CROWPANEL7) && CROWPANEL_FRAME_INTERNAL
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
#endif
    if (back) {
        frame.setTextSize(1);
        frameBufferOk = true;
        canvas = &frame;
        Serial.println("[ota] frame buffer back");
    } else {
        Serial.printf("[ota] frame buffer could not come back (largest block %lu); unbuffered until restart\n",
                      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
#endif
}

static void enterWifiPass(const char* ssid) {
    state = AppState::WIFI_PASS;
    transitionStart = millis();
    uiWifiPassInit(*canvas, ssid);
}

// WIFI NETWORKS, and the scan it adds from. The password keyboard is the
// update flow's; this flag says whose turn it is when it comes back.
static bool s_passForNets = false;
// The screen this board lives on: the desk for a board on a desk, the main
// screen for every other one. Where anything that took the screen over goes
// when it is done -- the update window, and BINGO's OK.
static void goHome() {
    if (Settings::deskWanted()) enterDesk();
    else                        enterClear();
}
static void enterSysProps() {
    state = AppState::SYS_PROPS;
    transitionStart = millis();
    // Squachy said this line out loud until now, and two boards talking to
    // each other painted over it. Taken here so he does not say it twice.
    (void)OtaCore::takeAvailableNotice();
    uiSysPropsInit(*canvas);
}

static void enterWifiNets() {
    state = AppState::WIFI_NETS;
    transitionStart = millis();
    uiWifiNetsInit(*canvas);
}
static void enterWifiAdd() {
    state = AppState::WIFI_ADD;
    transitionStart = millis();
    engine.startRawWifiScan();
    uiWifiAddInit(*canvas);
}

static void enterHunt() {
    state = AppState::HUNT;
    transitionStart = millis();
    uiHuntInit(*canvas);
}

// true when reached via the first-boot flow (DONE -> CLEAR, straight
// into the normal onboarding overlay if this is also a first boot),
// false when reached later via Settings' "CHECK COLORS" row
// (DONE -> back to SETTINGS, matching OUTFIT's same back-to-where-you-
// came-from feel).
static bool s_colorCheckFromSettings = false;

static void enterColorCheck(bool fromSettings) {
    state = AppState::COLOR_CHECK;
    transitionStart = millis();
    s_colorCheckFromSettings = fromSettings;
    uiColorCheckInit(*canvas);
}

static void enterDiary() {
    state = AppState::DIARY;
    transitionStart = millis();
    uiDiaryInit(*canvas);
}

static void enterOutfit() {
    state = AppState::OUTFIT;
    transitionStart = millis();
    uiOutfitInit(*canvas);
}

static void enterIgnoreList() {
    state = AppState::IGNORE_LIST;
    transitionStart = millis();
    uiIgnoreListInit(*canvas);
}

#if SQUACH_MESH
static void enterMeshPhrase() {
    state = AppState::MESH_PHRASE;
    transitionStart = millis();
    uiMeshPhraseInit(*canvas);
}

static void enterMeshCompose() {
    state = AppState::MESH_COMPOSE;
    transitionStart = millis();
    uiMeshComposeInit(*canvas);
}

static void enterSquad(bool roster = false) {
    state = AppState::SQUAD;
    transitionStart = millis();
    uiSquadInit(*canvas, roster);
}

static void enterMeshWarn() {
    state = AppState::MESH_WARN;
    transitionStart = millis();
    uiMeshWarnInit(*canvas);
}

static void enterMeshMenu() {
    state = AppState::MESH_MENU;
    transitionStart = millis();
    uiMeshMenuInit(*canvas);
}

static void enterPhone() {
    state = AppState::PHONE;
    transitionStart = millis();
    uiPhoneInit(*canvas);
}

// The same payphone, typing a SquachMesh message instead of a name.
static void enterPhoneMessage(const char* text) {
    state = AppState::PHONE;
    transitionStart = millis();
    uiPhoneInitMessage(*canvas, text);
}
#endif

static void enterDetFilter(bool keepScroll = false) {
    state = AppState::DETECTION_FILTER;
    transitionStart = millis();
    uiDetFilterInit(*canvas, keepScroll);
}

static void enterBeaconWarn() {
    state = AppState::BEACON_WARN;
    transitionStart = millis();
    uiBeaconWarnInit(*canvas);
}

// ---- the PIN lock ------------------------------------------------------------
// SECURITY is the menu; PIN_ENTRY is the payphone asking for a PIN on its
// behalf, with BACK; LOCKED is the payphone as the lock screen, without.
enum class PinFlow : uint8_t { SET_NEW, SET_AGAIN, OFF_VERIFY, CHANGE_CUR, CHANGE_NEW, CHANGE_AGAIN,
                               DURESS_CUR, DURESS_NEW, DURESS_AGAIN, DURESS_OFF };
static PinFlow s_pinFlow = PinFlow::SET_NEW;
static char    s_pinFirst[9]  = "";   // the first entry of a confirm-by-repeating pair
static char    s_pinPromptBuf[28] = "";

static void enterSecurity() {
    state = AppState::SECURITY;
    transitionStart = millis();
    uiSecurityInit(*canvas);
}

static const char* pinFlowPrompt(PinFlow f) {
    switch (f) {
        case PinFlow::SET_NEW:
            snprintf(s_pinPromptBuf, sizeof s_pinPromptBuf, "NEW %u-DIGIT PIN", (unsigned)Security::pinLength());
            return s_pinPromptBuf;
        case PinFlow::SET_AGAIN:
        case PinFlow::CHANGE_AGAIN:
        case PinFlow::DURESS_AGAIN: return "AGAIN TO CONFIRM";
        case PinFlow::CHANGE_NEW:   return "NEW PIN";
        case PinFlow::DURESS_NEW:   return "DURESS PIN";
        default:                    return "CURRENT PIN";
    }
}

static void startPinFlow(PinFlow f, const char* prompt = nullptr) {
    s_pinFlow = f;
    state = AppState::PIN_ENTRY;
    transitionStart = millis();
    uiPhoneInitPin(*canvas, Security::pinLength(), prompt ? prompt : pinFlowPrompt(f), true);
}

static void enterLocked() {
    Settings::deskActive(false);
    state = AppState::LOCKED;
    transitionStart = millis();
    s_scanPickerOpen = false;
    uiPhoneInitPin(*canvas, Security::pinLength(), "LOCKED", false);
    uiPhonePinAllowForgot(true);
}

#if HAVE_NVS_ERASE
// The NVS erase that actually erases. Deleting a key only marks its entry
// erased -- the bytes sit in flash until that page is reused, and a USB cable
// can read them -- so the secrets are removed by erasing the WHOLE store and
// writing everything worth keeping back. Everything is kept except the two
// namespaces that hold secrets: the message phrase and key, and the ignore
// list. Settings, calibration, outfits, stats, the PIN itself: all restored.
struct KeptEntry {
    std::string ns, key;
    nvs_type_t  type;
    uint64_t    num;
    std::vector<uint8_t> bytes;
};

static bool secretNamespace(const char* ns) {
    // "otawifi" is the saved WiFi password for firmware updates.
    return !strcmp(ns, "meshtalk") || !strcmp(ns, "ignore") || !strcmp(ns, "otawifi");
}

static void physicalNvsWipe() {
    std::vector<KeptEntry> kept;
    // Grown once, up front. Grown by doubling as entries arrived, the last
    // step asked for more than the heap had left and the board panicked
    // between the erase and the restart -- with the secrets already gone,
    // which is the one part that mattered, but as a crash, not a quiet reboot.
    kept.reserve(128);
    Serial.printf("[wipe] keeping settings: heap %lu\n", (unsigned long)ESP.getFreeHeap());
    nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY);
    while (it) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        nvs_handle_t h;
        if (!secretNamespace(info.namespace_name) &&
            nvs_open(info.namespace_name, NVS_READONLY, &h) == ESP_OK) {
            KeptEntry e;
            e.ns = info.namespace_name; e.key = info.key; e.type = info.type; e.num = 0;
            bool ok = true;
            switch (info.type) {
                case NVS_TYPE_U8:  { uint8_t  v; ok = nvs_get_u8 (h, info.key, &v) == ESP_OK; e.num = v; break; }
                case NVS_TYPE_I8:  { int8_t   v; ok = nvs_get_i8 (h, info.key, &v) == ESP_OK; e.num = (uint64_t)(int64_t)v; break; }
                case NVS_TYPE_U16: { uint16_t v; ok = nvs_get_u16(h, info.key, &v) == ESP_OK; e.num = v; break; }
                case NVS_TYPE_I16: { int16_t  v; ok = nvs_get_i16(h, info.key, &v) == ESP_OK; e.num = (uint64_t)(int64_t)v; break; }
                case NVS_TYPE_U32: { uint32_t v; ok = nvs_get_u32(h, info.key, &v) == ESP_OK; e.num = v; break; }
                case NVS_TYPE_I32: { int32_t  v; ok = nvs_get_i32(h, info.key, &v) == ESP_OK; e.num = (uint64_t)(int64_t)v; break; }
                case NVS_TYPE_U64: { uint64_t v; ok = nvs_get_u64(h, info.key, &v) == ESP_OK; e.num = v; break; }
                case NVS_TYPE_I64: { int64_t  v; ok = nvs_get_i64(h, info.key, &v) == ESP_OK; e.num = (uint64_t)v; break; }
                case NVS_TYPE_STR: {
                    size_t n = 0;
                    ok = nvs_get_str(h, info.key, nullptr, &n) == ESP_OK;
                    if (ok) { e.bytes.resize(n); ok = nvs_get_str(h, info.key, (char*)e.bytes.data(), &n) == ESP_OK; }
                    break;
                }
                case NVS_TYPE_BLOB: {
                    size_t n = 0;
                    ok = nvs_get_blob(h, info.key, nullptr, &n) == ESP_OK;
                    if (ok) { e.bytes.resize(n); ok = nvs_get_blob(h, info.key, e.bytes.data(), &n) == ESP_OK; }
                    break;
                }
                default: ok = false; break;
            }
            nvs_close(h);
            if (ok) kept.push_back(std::move(e));
        }
        it = nvs_entry_next(it);
    }
    nvs_release_iterator(it);
    Serial.printf("[wipe] %u entries kept, heap %lu; erasing\n", (unsigned)kept.size(), (unsigned long)ESP.getFreeHeap());
    serialFlush();

    nvs_flash_erase();       // de-initialises, then erases every page
    nvs_flash_init();

    for (const KeptEntry& e : kept) {
        nvs_handle_t h;
        if (nvs_open(e.ns.c_str(), NVS_READWRITE, &h) != ESP_OK) continue;
        const char* k = e.key.c_str();
        switch (e.type) {
            case NVS_TYPE_U8:   nvs_set_u8 (h, k, (uint8_t)e.num);  break;
            case NVS_TYPE_I8:   nvs_set_i8 (h, k, (int8_t)e.num);   break;
            case NVS_TYPE_U16:  nvs_set_u16(h, k, (uint16_t)e.num); break;
            case NVS_TYPE_I16:  nvs_set_i16(h, k, (int16_t)e.num);  break;
            case NVS_TYPE_U32:  nvs_set_u32(h, k, (uint32_t)e.num); break;
            case NVS_TYPE_I32:  nvs_set_i32(h, k, (int32_t)e.num);  break;
            case NVS_TYPE_U64:  nvs_set_u64(h, k, e.num);           break;
            case NVS_TYPE_I64:  nvs_set_i64(h, k, (int64_t)e.num);  break;
            case NVS_TYPE_STR:  nvs_set_str(h, k, (const char*)e.bytes.data()); break;
            case NVS_TYPE_BLOB: nvs_set_blob(h, k, e.bytes.data(), e.bytes.size()); break;
            default: break;
        }
        nvs_commit(h);
        nvs_close(h);
    }
}
#endif

// The wipe: the secrets in NVS, the SD log, and -- on the device -- a real
// erase of the store and a restart, which also takes the RAM log, the inbox
// and every open handle to the old store with it. `after` is how the board
// comes back; see takeWipeBoot().
static void performWipe(WipeBoot after) {
    // Dark first. A duress restart has to look like any other restart, and
    // the light is the one thing visible from the back of the board.
    StatusLight::off();
    Security::wipeSecrets();
    engine.sd().wipe();
    BlackBox::wipe();        // the log and the crash history kept in flash
#if HAVE_NVS_ERASE
    // The frame buffer is 77 KB the wipe can have: the board restarts in a
    // moment and the screen is meant to go quiet anyway. Without it the copy
    // of the kept settings ran the heap dry.
    releaseFrameBuffer("wipe");
    physicalNvsWipe();
    Serial.println("[wipe] done, restarting");
    serialFlush();
    g_wipeBoot = WIPEBOOT_MAGIC | (uint8_t)after;
    delay(20);
    esp_restart();
#else
#if SQUACH_MESH
    MeshTalk::forget();
#endif
    engine.clearLog();
    engine.clearWatch();
    engine.clearHunt();
    if (after == WipeBoot::LOCKED) Security::lock();
    enterClear();
#endif
}

static void enterPower() {
    state = AppState::POWER_SAVER;
    transitionStart = millis();
    uiPowerInit(*canvas);
}

static void enterLight() {
    state = AppState::STATUS_LIGHT;
    transitionStart = millis();
    uiLightInit(*canvas);
}

#if defined(TWATCH_S3)
// The T-Watch S3's AXP2101 gates the screen backlight (ALDO2), the touch
// chip (ALDO3), the RTC's supply (ALDO1), the radio (ALDO4) and the haptic
// driver (BLDO2). Nothing on the display or touch bus answers until these
// are on, so this runs first thing in setup(). Rails and volts are LilyGo's
// own from LilyGoLib.cpp; the charger is left at the chip's defaults.
static XPowersAXP2101 s_pmu;
static bool           s_pmuOk = false;
static void twatchPowerUp() {
    Wire1.begin(10, 11);
    s_pmuOk = s_pmu.begin(Wire1, AXP2101_SLAVE_ADDRESS, 10, 11);
    if (!s_pmuOk) { Serial.println("[pmu] AXP2101 did not answer -- screen may stay dark"); return; }
    // The chip's own rail, set rather than assumed: a full power-off (a dead
    // battery) puts the PMU back to its register defaults. It read 3300
    // either way on 2026-09-23; the deaf radios that morning were not this.
    s_pmu.enableVbusVoltageMeasure();
    s_pmu.enableSystemVoltageMeasure();
    s_pmu.enableBattVoltageMeasure();
    Serial.printf("[pmu] DC1 %u mV (%s), ALDO4 %u mV, VBUS %u mV, SYS %u mV, VBUS limit code %u, charge code %u\n",
                  (unsigned)s_pmu.getDC1Voltage(), s_pmu.isEnableDC1() ? "on" : "off",
                  (unsigned)s_pmu.getALDO4Voltage(), (unsigned)s_pmu.getVbusVoltage(),
                  (unsigned)s_pmu.getSystemVoltage(), (unsigned)s_pmu.getVbusCurrentLimit(),
                  (unsigned)s_pmu.getChargerConstantCurr());
    s_pmu.setDC1Voltage(3300);
    // The USB input limit and the charge current. After a full power-off the
    // PMU is back to its register defaults, and with the cell flat the
    // system rail is whatever the USB path is allowed to pass: not enough
    // for a radio burst. 500 mA from USB, 200 mA into the cell (LilyGo's
    // own example uses 1500 and 200).
    s_pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_500MA);
    s_pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_200MA);
    s_pmu.setSysPowerDownVoltage(2600);
    s_pmu.setALDO1Voltage(3300); s_pmu.enableALDO1();   // RTC
    s_pmu.setALDO2Voltage(3300); s_pmu.enableALDO2();   // backlight supply
    s_pmu.setALDO3Voltage(3300); s_pmu.enableALDO3();   // touch
    s_pmu.setALDO4Voltage(3300); s_pmu.enableALDO4();   // radio
    s_pmu.setBLDO2Voltage(3300); s_pmu.enableBLDO2();   // DRV2605 haptics

    s_pmu.disableDC2(); s_pmu.disableDC4(); s_pmu.disableDC5();
    s_pmu.disableDLDO2();
    // The crown is the PMU's power key. A short press is read as an IRQ flag
    // from the loop (see twatchCrownTick); a long press powers the watch off
    // in the chip itself, which is what a watch's crown should do.
    s_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    s_pmu.clearIrqStatus();
    s_pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
    // The clock chip's backup cell. LilyGo's own setup charges it; ours did
    // not, so the chip would lose the time the first time the battery ran
    // flat.
    s_pmu.setButtonBatteryChargeVoltage(3300);
    s_pmu.enableButtonBatteryCharge();
    delay(20);
    Serial.printf("[pmu] AXP2101 up: battery %d%%, %s\n", s_pmu.getBatteryPercent(),
                  s_pmu.isVbusIn() ? "on USB" : "on battery");
}
#endif

#if defined(TWATCH_S3)
// One battery sample into the black box. See BattRecord for what it holds
// and why. Printed too, so a bench run shows the same line the ring keeps.
// ---- the haptic motor: a DRV2605 on the power chip's bus ------------------
// Driven by register, no library: out of standby, the ERM effect library,
// internal trigger, then a sequence of effect numbers and waits and GO.
// Effect 1 is a strong click, 47 a full buzz; a byte with the top bit set is
// a wait of that many tens of milliseconds.
static const uint8_t DRV_ADDR = 0x5A;
static bool s_drvOk = false;
static uint32_t s_drvAwakeAt = 0;   // when the last buzz woke it; 0 = in standby

static bool drvWrite(uint8_t reg, uint8_t v) {
    Wire1.beginTransmission(DRV_ADDR);
    Wire1.write(reg);
    Wire1.write(v);
    return Wire1.endTransmission() == 0;
}
static uint8_t drvRead(uint8_t reg) {
    Wire1.beginTransmission(DRV_ADDR);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return 0;
    if (Wire1.requestFrom(DRV_ADDR, (uint8_t)1) != 1) return 0;
    return Wire1.read();
}

static void twatchHapticBegin() {
    if (!s_pmuOk) return;
    s_drvOk = drvWrite(0x01, 0x00);          // MODE: out of standby, internal trigger
    if (!s_drvOk) { Serial.println("[buzz] DRV2605 did not answer"); return; }
    drvWrite(0x02, 0x00);                    // no real-time input
    drvWrite(0x03, 0x01);                    // effect library 1: ERM
    drvWrite(0x1A, drvRead(0x1A) & 0x7F);    // FEEDBACK: ERM, not LRA
    drvWrite(0x1D, drvRead(0x1D) | 0x20);    // CONTROL3: ERM open loop
    drvWrite(0x01, 0x40);                    // STANDBY until a buzz: a few uA instead of ~0.5 mA
    Serial.println("[buzz] DRV2605 ready");
}

static void twatchBuzz(Buzz kind) {
    if (!s_drvOk) return;
    if (kind != Buzz::SAMPLE && !Settings::buzz()) { Serial.println("[buzz] alert, but BUZZ is off"); return; }
    // A room of Ring cameras alerts every few seconds. One buzz in ten
    // seconds for those; a device you asked to WATCH always gets through.
    static uint32_t lastAt = 0;
    const uint32_t now = millis();
    if (kind == Buzz::ALERT && lastAt && now - lastAt < 10000) return;
    lastAt = now;
    uint8_t seq[8] = { 0 };
    if (kind == Buzz::WATCH) {               // three long buzzes
        seq[0] = 47; seq[1] = 0x80 | 12; seq[2] = 47; seq[3] = 0x80 | 12; seq[4] = 47;
    } else {                                 // a double click
        seq[0] = 1; seq[1] = 0x80 | 10; seq[2] = 1;
    }
    drvWrite(0x01, 0x00);                    // out of standby for this one
    s_drvAwakeAt = millis();
    for (uint8_t i = 0; i < 8; i++) drvWrite(0x04 + i, seq[i]);
    const bool go = drvWrite(0x0C, 0x01);    // GO
    Serial.printf("[buzz] %s%s\n", kind == Buzz::WATCH ? "watch alert" : kind == Buzz::ALERT ? "alert" : "sample",
                  go ? "" : " -- the motor driver did not answer");
}

// ---- the motion sensor: a BMA423 on the same bus --------------------------
// Used for one thing so far: whether the watch is sitting still, which is
// what tells a nightstand from a wrist. Raw readings only -- the step counter
// and tilt features need a program loaded into the chip, and "has it moved"
// does not. Twice a second, one six-byte read.
static const uint8_t BMA_ADDR = 0x19;
static bool     s_bmaOk = false;
static uint64_t s_moveBits[2] = { 0, 0 };   // one bit a second, the last 128 s; bit 0 is now
static uint32_t s_moveSec = 0;              // the second bit 0 stands for
static int16_t  s_acc[3];
static bool     s_accHave = false;
// A reading this far from the last one is movement. +-4 g at twelve bits is
// 512 a g, so about 0.06 g: well above the chip's noise, well below a wrist.
static const int16_t  MOVE_LSB = 30;
// Still is fewer than this many seconds with movement in the last two
// minutes. A sleeper turning over is a few; a walk is most of them.
static const uint8_t  MOVE_SECS = 10;

static bool bmaWrite(uint8_t reg, uint8_t v) {
    Wire1.beginTransmission(BMA_ADDR);
    Wire1.write(reg);
    Wire1.write(v);
    const bool ok = Wire1.endTransmission() == 0;
    delay(1);   // in its power-save mode the chip wants a gap between writes
    return ok;
}
static bool bmaRead(uint8_t reg, uint8_t* buf, uint8_t n) {
    Wire1.beginTransmission(BMA_ADDR);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return false;
    if (Wire1.requestFrom(BMA_ADDR, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire1.read();
    return true;
}

static void twatchMotionBegin() {
    if (!s_pmuOk) return;
    uint8_t id = 0;
    if (!bmaRead(0x00, &id, 1) || id != 0x13) { Serial.printf("[motion] BMA423 did not answer (id %02X)\n", id); return; }
    bmaWrite(0x7C, 0x00);    // PWR_CONF: power save off while it is set up
    bmaWrite(0x40, 0x15);    // ACC_CONF: averaging mode, 2 samples, 12.5 Hz
    bmaWrite(0x41, 0x01);    // ACC_RANGE: +-4 g
    bmaWrite(0x7D, 0x04);    // PWR_CTRL: accelerometer on
    bmaWrite(0x7C, 0x03);    // PWR_CONF: power save back on, a few uA
    s_bmaOk = true;
    Serial.println("[motion] BMA423 ready");
}

static uint8_t movedSecs() {
    return (uint8_t)(__builtin_popcountll(s_moveBits[0]) + __builtin_popcountll(s_moveBits[1]));
}

// No sensor, no verdict: without one the watch counts as moving, which is
// how AUTO SNOOZE has always behaved.
static bool twatchStill() { return s_bmaOk && s_accHave && movedSecs() < MOVE_SECS; }

static void twatchMotionTick(uint32_t now) {
    if (!s_bmaOk) return;
    static uint32_t last = 0;
    if (now - last < 500) return;
    last = now;
    uint8_t b[6];
    if (!bmaRead(0x12, b, 6)) return;
    bool moved = false;
    for (uint8_t k = 0; k < 3; k++) {
        const int16_t a = (int16_t)((uint16_t)b[2 * k + 1] << 8 | b[2 * k]) >> 4;
        if (s_accHave && abs(a - s_acc[k]) > MOVE_LSB) moved = true;
        s_acc[k] = a;
    }
    const bool wasStill = twatchStill();
    // Slide the history along to this second.
    const uint32_t sec = now / 1000, n = sec - s_moveSec;
    s_moveSec = sec;
    if (n >= 128)      { s_moveBits[0] = s_moveBits[1] = 0; }
    else if (n >= 64)  { s_moveBits[1] = s_moveBits[0] << (n - 64); s_moveBits[0] = 0; }
    else if (n)        { s_moveBits[1] = (s_moveBits[1] << n) | (s_moveBits[0] >> (64 - n)); s_moveBits[0] <<= n; }
    if (moved) s_moveBits[0] |= 1;
    if (!s_accHave) { s_accHave = true; return; }
    const bool isStill = twatchStill();
    if (isStill != wasStill) Serial.printf("[motion] %s\n", isStill ? "still" : "moving");
}

// ---- the clock chip: a PCF8563 on the same bus, with a backup cell ---------
// Kept in UTC. Read once at boot, after Clock::begin(); written whenever the
// clock learns the real time from anywhere, through Clock::onSet().
static const uint8_t RTC_ADDR = 0x51;
// Our mark in the chip's minute-alarm register, with its disabled bit set
// (and alarm interrupts off in control 2), so nothing acts on it. A chip without it holds somebody else's time:
// the factory firmware leaves China time in it, eight hours off UTC, and
// believing that moved the clock by eight hours.
static const uint8_t RTC_MARK = 0xC2;   // alarm disabled, minute 42
static bool s_rtcLoading = false;   // our own set at boot: nothing to write back

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

// Days since 1970-01-01 for a civil date, and back (Howard Hinnant's).
static int32_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}
static void civilFromDays(int32_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (int)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
}

static void rtcWrite(uint32_t epoch) {
    if (s_rtcLoading || !s_pmuOk) return;
    int y; unsigned mo, d;
    civilFromDays((int32_t)(epoch / 86400u), y, mo, d);
    const uint32_t sod = epoch % 86400u;
    Wire1.beginTransmission(RTC_ADDR);
    Wire1.write(0x00);
    Wire1.write(0x00);                                   // control 1: running
    Wire1.write(0x00);                                   // control 2: no alarms
    Wire1.write(bin2bcd((uint8_t)(sod % 60)));           // seconds, VL cleared
    Wire1.write(bin2bcd((uint8_t)(sod / 60 % 60)));
    Wire1.write(bin2bcd((uint8_t)(sod / 3600)));
    Wire1.write(bin2bcd((uint8_t)d));
    Wire1.write((uint8_t)((epoch / 86400u + 4) % 7));    // 1970-01-01 was a Thursday
    Wire1.write(bin2bcd((uint8_t)mo));
    Wire1.write(bin2bcd((uint8_t)(y % 100)));
    bool ok = Wire1.endTransmission() == 0;
    Wire1.beginTransmission(RTC_ADDR);
    Wire1.write(0x09);
    Wire1.write(RTC_MARK);                               // minute alarm: off, and our mark
    ok = (Wire1.endTransmission() == 0) && ok;
    Serial.printf("[rtc] clock chip %s %04d-%02u-%02u %02lu:%02lu UTC\n", ok ? "set to" : "FAILED at",
                  y, mo, d, (unsigned long)(sod / 3600), (unsigned long)(sod / 60 % 60));
}

// 0 when the chip is missing, has lost the time (its VL flag), or holds
// something no build could believe.
static uint32_t rtcRead(bool* lost = nullptr, bool* foreign = nullptr) {
    if (lost) *lost = false;
    if (foreign) *foreign = false;
    if (!s_pmuOk) return 0;
    Wire1.beginTransmission(RTC_ADDR);
    Wire1.write(0x09);
    if (Wire1.endTransmission(false) != 0 || Wire1.requestFrom(RTC_ADDR, (uint8_t)1) != 1) return 0;
    if (Wire1.read() != RTC_MARK) { if (foreign) *foreign = true; return 0; }
    Wire1.beginTransmission(RTC_ADDR);
    Wire1.write(0x02);
    if (Wire1.endTransmission(false) != 0) return 0;
    if (Wire1.requestFrom(RTC_ADDR, (uint8_t)7) != 7) return 0;
    uint8_t r[7];
    for (uint8_t i = 0; i < 7; i++) r[i] = Wire1.read();
    if (r[0] & 0x80) { if (lost) *lost = true; return 0; }   // VL: the backup ran out
    const int y = 2000 + bcd2bin(r[6]);
    const unsigned mo = bcd2bin(r[5] & 0x1F), d = bcd2bin(r[3] & 0x3F);
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    const uint32_t days = (uint32_t)daysFromCivil(y, mo, d);
    return days * 86400u + bcd2bin(r[2] & 0x3F) * 3600u + bcd2bin(r[1] & 0x7F) * 60u + bcd2bin(r[0] & 0x7F);
}

// At boot: the chip's time, when it has one worth believing, sets the clock
// as a real answer -- trusted, not the ten-minute note's guess.
static void twatchRtcBegin() {
    bool lost = false, foreign = false;
    const uint32_t t = rtcRead(&lost, &foreign);
    if (t) {
        s_rtcLoading = true;
        const bool ok = Clock::setEpoch(t) && Clock::isSet();
        s_rtcLoading = false;
        Serial.printf("[rtc] clock chip says %lu: %s\n", (unsigned long)t, ok ? "clock set from it" : "not believable");
    } else {
        Serial.printf("[rtc] clock chip has no time (%s)\n", lost ? "backup ran out" : foreign ? "not set by SquachWatch" : "not answering");
    }
    Clock::onSet(rtcWrite);
    // A clock that already knew the time (kept by the ESP32 across a soft
    // reset) goes to a chip that did not.
    if (!t && Clock::trusted()) rtcWrite(Clock::nowEpoch());
}

// The WATCH page's BATTERY row: "81% 4.12V", or "CHG 81%" on the cable.
void twatchBatteryLine(char* out, size_t n) {
    if (!s_pmuOk) { snprintf(out, n, "?"); return; }
    static uint32_t at = 0;
    static char     line[16] = "";
    const uint32_t now = millis();
    if (!line[0] || now - at >= 2000) {   // two I2C reads, not one per frame
        at = now;
        const int pct = s_pmu.getBatteryPercent();
        if (s_pmu.isCharging()) snprintf(line, sizeof line, "CHG %d%%", pct);
        else snprintf(line, sizeof line, "%d%% %u.%02uV", pct, (unsigned)(s_pmu.getBattVoltage() / 1000),
                      (unsigned)(s_pmu.getBattVoltage() % 1000 / 10));
    }
    snprintf(out, n, "%s", line);
}

// The chip's own temperature sensor, read at most every two seconds: the
// settings row asks every frame, and each read starts and stops the sensor.
int twatchChipC() {
    static uint32_t at = 0;
    static int c = 0;
    const uint32_t now = millis();
    if (!at || now - at >= 2000) { at = now ? now : 1; c = (int)lroundf(temperatureRead()); }
    return c;
}

// CLOCK CHECK: the ESP32's 40 MHz crystal against the clock chip's own
// 32 kHz one. Both radios tune off the ESP32's crystal, and so does micros();
// a crystal that started up off-frequency would leave the CPU, the screen and
// USB working and both radios deaf -- and only a power cut would restart it,
// which is exactly how the deaf spells behave (2026-09-24). The clock chip's
// seconds tick is the yardstick: catch one tick, count XTAL_SECS of them,
// catch the last, and compare with micros(). Catching a tick means polling
// the chip for up to a second, so the screen stalls twice, briefly.
// Healthy is within about +-30 ppm (both crystals together); the radios need
// the ESP32's within about +-25 ppm of true.
static const uint32_t XTAL_SECS = 60;
static uint8_t  s_xtalState = 0;          // 0 never run, 1 counting, 2 done, 3 failed
static uint32_t s_xtalT0 = 0, s_xtalStartMs = 0;
static uint8_t  s_xtalSec0 = 0;
static int32_t  s_xtalPpm = 0;
static int      s_xtalChipC = 0;
// Polls the clock chip's seconds until they change; the micros() of the
// change and the new second. False if the chip did not answer or never ticked.
static bool rtcTick(uint32_t& atUs, uint8_t& sec) {
    auto readSec = [](uint8_t& s) -> bool {
        Wire1.beginTransmission(RTC_ADDR);
        Wire1.write(0x02);
        if (Wire1.endTransmission(false) != 0) return false;
        if (Wire1.requestFrom(RTC_ADDR, (uint8_t)1) != 1) return false;
        s = bcd2bin(Wire1.read() & 0x7F);
        return true;
    };
    uint8_t first;
    if (!readSec(first)) return false;
    const uint32_t t0 = millis();
    while (millis() - t0 < 1500) {
        uint8_t s;
        if (!readSec(s)) return false;
        if (s != first) { atUs = micros(); sec = s; return true; }
    }
    return false;
}
static void twatchXtalStart() {
    if (s_xtalState == 1) return;
    if (!s_pmuOk || !rtcTick(s_xtalT0, s_xtalSec0)) {
        s_xtalState = 3;
        Serial.println("[xtal] the clock chip did not tick; no check");
        return;
    }
    s_xtalState = 1;
    s_xtalStartMs = millis();
    Serial.printf("[xtal] counting %lu clock-chip seconds against micros()...\n", (unsigned long)XTAL_SECS);
}
static void twatchXtalTick(uint32_t now) {
    if (s_xtalState != 1 || now - s_xtalStartMs < (XTAL_SECS - 1) * 1000u) return;
    uint32_t t1; uint8_t s1;
    if (!rtcTick(t1, s1)) { s_xtalState = 3; Serial.println("[xtal] lost the clock chip's tick"); return; }
    const uint32_t us = t1 - s_xtalT0;
    const uint32_t n  = (us + 500000u) / 1000000u;   // whole seconds the chip counted
    if (!n || (s_xtalSec0 + n) % 60 != s1) { s_xtalState = 3; Serial.println("[xtal] the seconds did not add up"); return; }
    s_xtalPpm   = (int32_t)(((int64_t)us - (int64_t)n * 1000000) * 1000000 / ((int64_t)n * 1000000));
    s_xtalChipC = twatchChipC();
    s_xtalState = 2;
    Serial.printf("[xtal] %lu s by the clock chip took %lu us by the ESP32: %+ld ppm (chip %d C; radios heard %lu adverts, %lu frames since boot)\n",
                  (unsigned long)n, (unsigned long)us, (long)s_xtalPpm, s_xtalChipC,
                  (unsigned long)advertsSeen(), (unsigned long)wifiFramesSeen());
}
void twatchXtalLine(char* out, size_t n) {
    switch (s_xtalState) {
        case 1: {
            const uint32_t el = (millis() - s_xtalStartMs) / 1000u;
            snprintf(out, n, "%lus", (unsigned long)(el < XTAL_SECS ? XTAL_SECS - el : 0));
            break;
        }
        case 2:  snprintf(out, n, "%+ld PPM", (long)s_xtalPpm); break;
        case 3:  snprintf(out, n, "FAILED"); break;
        default: snprintf(out, n, "GO"); break;
    }
}

// STEADY POWER: DC1 -- the ESP32 and its radio -- in forced PWM, or back to
// the chip's automatic PWM/PFM. Applied at boot and on every toggle.
static void twatchApplySteady() {
    if (!s_pmuOk) return;
    s_pmu.settDC1WorkModeToPwm(Settings::steadyPower() ? 1 : 0);
    Serial.printf("[pmu] DC1 %s (reg 0x81 = %02X)\n", Settings::steadyPower() ? "forced PWM (STEADY POWER)" : "automatic PWM/PFM",
                  (unsigned)s_pmu.readRegister(0x81));
}

static void twatchBatterySample(uint8_t why) {
    if (!s_pmuOk) return;
    BlackBox::BattRecord r;
    memset(&r, 0, sizeof r);
    r.mv       = s_pmu.getBattVoltage();
    r.pct      = (uint8_t)s_pmu.getBatteryPercent();
    r.why      = why;
    r.epoch    = Clock::trusted() ? Clock::nowEpoch() : 0;
    r.upSec    = millis() / 1000u;
    r.cpuMhz10 = (uint8_t)(getCpuFrequencyMhz() / 10);
    if (s_pmu.isVbusIn())   r.flags |= BlackBox::BATT_USB;
    if (s_pmu.isCharging()) r.flags |= BlackBox::BATT_CHARGING;
    if (!s_panelAsleep)     r.flags |= BlackBox::BATT_SCREEN_ON;
    if (!s_radiosResting)   r.flags |= BlackBox::BATT_RADIOS_ON;
    r.chipC   = (int8_t)twatchChipC();
    r.steady  = Settings::steadyPower() ? 1 : 0;
    r.adverts = advertsSeen();
    r.frames  = wifiFramesSeen();
    BlackBox::noteBattery(r);
    static const char* const WHY[] = { "timer", "boot", "usb", "screen", "radio reset", "self-heal" };
    Serial.printf("[batt] %u mV  %u%%  %s%s  screen %s  cpu %u MHz  up %lu s  chip %d C  heard %lu/%lu  (%s)\n",
                  (unsigned)r.mv, (unsigned)r.pct, (r.flags & BlackBox::BATT_USB) ? "on USB" : "on battery",
                  (r.flags & BlackBox::BATT_CHARGING) ? ", charging" : "", s_panelAsleep ? "off" : "on",
                  (unsigned)getCpuFrequencyMhz(), (unsigned long)r.upSec, (int)r.chipC,
                  (unsigned long)r.adverts, (unsigned long)r.frames, WHY[why < 6 ? why : 0]);
}

// Every ten minutes, at boot, and whenever the cable or the screen changes
// state: the points where the slope of the curve changes.
static void twatchBatteryTick(uint32_t now) {
    if (!s_pmuOk) return;
    static uint32_t lastAt = 0;
    static bool first = true, wasUsb = false, wasAsleep = false;
    if (first) {
        first = false; lastAt = now;
        wasUsb = s_pmu.isVbusIn(); wasAsleep = s_panelAsleep;
        twatchBatterySample(BlackBox::BATT_WHY_BOOT);
        return;
    }
    if (wasAsleep != s_panelAsleep) {
        wasAsleep = s_panelAsleep; lastAt = now;
        twatchBatterySample(BlackBox::BATT_WHY_SCREEN);
        return;
    }
    if (now - lastAt < 600000u) return;
    lastAt = now;
    const bool usb = s_pmu.isVbusIn();   // one I2C read, ten minutes apart
    if (usb != wasUsb) { wasUsb = usb; twatchBatterySample(BlackBox::BATT_WHY_USB); return; }
    twatchBatterySample(BlackBox::BATT_WHY_TIMER);
}

// The radio duty cycle. The radios are on the cable, on with the screen
// (someone is looking), on through an alert, and on whenever anything else
// has the radio (an update, a raw scan). Otherwise they run for the on part
// of the cycle and rest for the rest of it. What "rest" means is the
// engine's business: see DetectionEngine::restRadios().
static void twatchRadioTick(uint32_t now) {
    static uint32_t phaseAt = 0, usbAt = 0;
    static bool     usb = true;
    // A rest cut short by a Bluetooth arrival is paid back on the next one,
    // so the WiFi on-time is the same as the plain cycle: the window just
    // lands when something turned up instead of on the clock.
    static uint32_t owedMs = 0, arrivalsAt = 0;
    bleArrivalsRoll(now);
    if (now - usbAt >= 2000) { usbAt = now; usb = s_pmuOk && s_pmu.isVbusIn(); s_onUsb = usb; }
    // RADIO TEST on the console forces BLE+5/30 whatever the settings say.
    const uint8_t duty = g_consoleRadioTest ? 3 : Settings::radioDuty();
    // Only a device you asked to WATCH holds the radios awake. Every alert
    // did, and a room with a Ring camera and a Flipper in it alerts every
    // few seconds, so WiFi never got a rest at home.
    const bool alerting = (state == AppState::WATCH_ALERT);
    const bool wantOn = duty == 0 || (!g_consoleRadioTest && (usb || !s_panelAsleep)) || alerting ||
                        state == AppState::UPDATE || OtaWifi::state() != OtaWifi::State::OFF;
    uint32_t onMs = 5000, offMs = 25000;
    if (duty == 2) { onMs = 10000; offMs = 50000; }
    if (wantOn) {
        static uint32_t saidAt = 0;
        if (g_consoleRadioTest && now - saidAt >= 10000) {
            saidAt = now;
            Serial.printf("[radio] held awake: duty %u, alerting %d, app state %u, update state %u\n",
                          (unsigned)duty, (int)alerting, (unsigned)state, (unsigned)OtaWifi::state());
        }
        if (s_radiosResting) { engine.wakeRadios(); s_radiosResting = false; Serial.println("[radio] awake"); }
        phaseAt = now;
        return;
    }
    if (!s_radiosResting) {
        if (now - phaseAt < onMs) return;
        if (engine.restRadios(duty != 3)) {
            s_radiosResting = true;
            arrivalsAt = bleArrivals();
            Serial.printf("[radio] resting: %s\n", duty == 3 ? (owedMs ? "WiFi off, BLE on, paying back an early window" : "WiFi off, BLE on") : "WiFi and BLE off");
        }
        phaseAt = now;   // either way: a refused rest tries again after another on-window
    } else {
        const uint32_t rested = now - phaseAt;
        // Only with Bluetooth listening, only once a rest has settled, and
        // not on a rest that is paying back the last early one.
        const bool arrival = duty == 3 && !owedMs && rested >= 3000 && rested < offMs &&
                             bleArrivals() != arrivalsAt;
        if (arrival || rested >= offMs + owedMs) {
            owedMs = arrival ? offMs - rested : 0;
            engine.wakeRadios(); s_radiosResting = false; phaseAt = now;
            if (arrival) Serial.printf("[radio] awake early: a new Bluetooth device, %lu s into the rest\n", (unsigned long)(rested / 1000));
            else         Serial.println("[radio] awake (timed)");
        }
    }
}

// The radio self-heal. The watch's radios have gone deaf more than once --
// started, configured, hearing nothing -- sometimes both, sometimes only
// Bluetooth (2026-09-24 at work: WiFi came back by itself, Bluetooth only
// after the crown switched the watch off and on). Bluetooth is the tell: it
// listens nearly all the time, and there is hardly a place with no adverts in
// it. So: two minutes of Bluetooth listening without one advert, at any point
// in the day and not just after boot, and the watch restarts itself.
//
// Never the software kind (ESP.restart): parts of the chip, the radio's
// analog side among them, stay powered through that, and every cure seen so
// far was deeper -- the cable reset of an esptool MAC check (a whole-chip
// reset) and the crown's off-and-on. Firmware cannot reach either. The PMU's
// own "restart the SoC" (reg 0x10 bit 1) was tried first and HUNG the watch
// on the wrist until a crown hard-off (2026-09-24), so it is out. What is
// left is a second of deep sleep: the radio, the CPU and the digital core are
// powered down, and the chip's RTC timer, which needs nothing from the PMU,
// wakes it into a full boot. At most HEAL_MAX in a row: the count clears the
// moment an advert arrives, so a field with no radios in it costs two
// restarts and then waits, rather than looping.
static const uint8_t  HEAL_MAX       = 2;
static const uint32_t HEAL_DEAF_MS   = 120000;   // of Bluetooth listening, not of clock
// Power the chip down for a second and let its own timer bring it back into
// a full boot, leaving `count` in RTC memory (which deep sleep keeps) for the
// next boot to read. The black box gets a battery line first, with why, so a
// morning read-out shows every one of these.
static void twatchPowerCycle(uint8_t count, uint8_t why) {
    twatchBatterySample(why);
    Serial.println("[heal] a second of deep sleep, then a full boot");
    serialFlush();
    g_healWord = HEAL_MAGIC | (uint32_t)count;
    engine.restRadios(true);   // WiFi stopped and Bluetooth's scan asked to stop, as a rest does
    delay(100);
    esp_sleep_enable_timer_wakeup(1000000ULL);
    esp_deep_sleep_start();
}

// RADIO RESET under WATCH: two taps within three seconds, so a stray one
// does not blank the watch. Does not spend the self-heal's allowance.
static uint32_t s_radioResetArmedAt = 0;
bool twatchRadioResetArmed() {
    return s_radioResetArmedAt && millis() - s_radioResetArmedAt < 3000;
}
static void twatchRadioResetTap() {
    if (!twatchRadioResetArmed()) { s_radioResetArmedAt = millis(); if (!s_radioResetArmedAt) s_radioResetArmedAt = 1; return; }
    s_radioResetArmedAt = 0;
    Serial.printf("[heal] RADIO RESET tapped: %lu adverts and %lu WiFi frames since boot; restarting\n",
                  (unsigned long)advertsSeen(), (unsigned long)wifiFramesSeen());
    twatchPowerCycle(0, BlackBox::BATT_WHY_RESET);
}

static bool bleShouldHear() {
    // Resting with Bluetooth off (the 5/30 and 10/60 duties), a raw scan, an
    // update: none of those are the radio's fault.
    if (s_radiosResting && Settings::radioDuty() != 3) return false;
    if (onRawScanScreen() || state == AppState::UPDATE || OtaWifi::state() != OtaWifi::State::OFF) return false;
    return true;
}
static void twatchRadioHealTick(uint32_t now) {
    static bool     said = false, gaveUp = false;
    static uint32_t lastAdv = 0, lastAt = 0, deafMs = 0;
    if (!said) {
        said = true;
        lastAt = now; lastAdv = advertsSeen();
        if (s_healCount) Serial.printf("[heal] this boot is self-heal restart %u of %u\n", (unsigned)s_healCount, (unsigned)HEAL_MAX);
    }
    const bool forced = g_consoleHeal;
    g_consoleHeal = false;
    if (!forced) {
        if (now - lastAt < 1000) return;
        const uint32_t adv = advertsSeen();
        if (adv != lastAdv) {
            lastAdv = adv; deafMs = 0; gaveUp = false;
            if (s_healCount) {
                Serial.printf("[heal] hearing again after %u self-heal restart(s)\n", (unsigned)s_healCount);
                s_healCount = 0;   // the next deaf spell starts from one
            }
        } else if (bleShouldHear()) {
            deafMs += now - lastAt;
        }
        lastAt = now;
        if (deafMs < HEAL_DEAF_MS) return;
        deafMs = 0;
    }
    if (s_healCount >= HEAL_MAX) {
        if (!gaveUp) Serial.printf("[heal] still deaf after %u restarts; waiting until it hears something\n", (unsigned)s_healCount);
        gaveUp = true;
        return;
    }
    const uint8_t next = (uint8_t)(s_healCount + 1);
    Serial.printf("[heal] Bluetooth deaf: no adverts in %lu s of listening (%lu WiFi frames since boot)%s; restarting (%u of %u)\n",
                  (unsigned long)(HEAL_DEAF_MS / 1000), (unsigned long)wifiFramesSeen(), forced ? " (bench test)" : "",
                  (unsigned)next, (unsigned)HEAL_MAX);
    twatchPowerCycle(next, BlackBox::BATT_WHY_HEAL);
}

// Polled ten times a second: one I2C read of the PMU's interrupt flags. A
// short press of the crown counts as a touch, which is what wakes the screen
// and holds off the timeout; nothing else is bound to it yet.
static void twatchCrownTick(uint32_t now) {
    static uint32_t at = 0;
    if (!s_pmuOk || now - at < 100) return;
    at = now;
    // The haptic driver back to standby once the longest pattern (three
    // buzzes, about 1.5 s) is surely over.
    if (s_drvAwakeAt && now - s_drvAwakeAt > 3000) { s_drvAwakeAt = 0; drvWrite(0x01, 0x40); }
    s_pmu.getIrqStatus();
    if (s_pmu.isPekeyShortPressIrq()) {
        if (!s_screenDimmed) {
            s_crownDark = true;
            s_crownDarkAt = now;
            s_crownLitUntil = 0;
            Serial.println("[crown] screen off");
        } else {
            s_crownDark = false;
            lastTouch = now;
            Serial.println("[crown] screen on");
        }
    }
    s_pmu.clearIrqStatus();
}
#endif

// Runtime UART speed -- set per-board in platformio.ini (-DSERIAL_BAUD=...)
// for hardware confirmed to hold a faster rate cleanly; boards without
// an explicit override fall back to this conservative default rather
// than inheriting cyd's verified-fast rate on unverified hardware.
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 921600
#endif

// The serial boot banner. Box-drawing and block characters, so it wants a
// UTF-8 terminal -- every monitor used with this board is one, and the
// console runs at 2,000,000 baud where a few hundred extra bytes cost
// nothing. Written out as literal characters rather than \u escapes so the
// art is legible here, which is the only place anyone will edit it.
//
// The version is NOT typed in. The line this replaced said "v1.0" from the
// day it was written to the day it was deleted, which is what happens to a
// hand-written version string. FIRMWARE_VERSION is stamped from the git tag
// at build time by extra_script.py -- the same one the boot screen and the
// diary already show.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
static void printBootBanner() {
    Serial.println("╔══════════════════════════════════════════════════╗");
    Serial.println("║   .-\"\"\"-.                                        ║");
    Serial.println("║  /  ^ ^  \\     ███ S Q U A C H W A T C H ███     ║");
    Serial.println("║  | [o|o] |     surveillance detector             ║");
    // %-13.13s holds the right border in place whatever the tag turns out
    // to be: the fixed text ahead of it is 37 columns and the box is 50.
    // The precision matters as much as the width -- a working tree builds as
    // "v1.5.16-dirty" and a commit past a tag as "v1.5.16-3-g554330d", both
    // of which walk the border off the end of the line. Truncated here only;
    // the boot screen and the diary still show the version in full.
    Serial.printf ("║  |   -   |     TALKING SASQUACH  .  %-13.13s║\n", FIRMWARE_VERSION);
    // Same %-34s trick as the version line above: the reason is variable
    // length ("interrupt watchdog" is the longest at eighteen characters)
    // and the right border has to stay put.
    char rst[40];
    snprintf(rst, sizeof(rst), "last reset: %s", resetReasonName());
    Serial.printf ("║  \\  \\_/  /     %-34s║\n", rst);
    Serial.println("║   )     (      2.4 GHz  .  ESP32  .  CYD         ║");
    Serial.println("║  /_/   \\_\\                                       ║");
    Serial.println("╚══════════════════════════════════════════════════╝");

    // A panic, a watchdog or a brownout is worth shouting about rather than
    // leaving as one word inside a box. The crash itself is already sitting
    // in the coredump partition (0x3F0000, 64K -- see the partition table in
    // platformio.ini), and nobody goes looking for it unless they are told
    // it is there. This is the difference between a bug report that says
    // "it keeps restarting" and one that says which task died.
    switch (esp_reset_reason()) {
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            Serial.println("*** That was not a clean boot.");
            Serial.println("*** The crash is saved in flash. To read it out:");
            Serial.println("***   esptool read_flash 0x3F0000 0x10000 core.bin");
            Serial.println("***   espcoredump.py info_corefile -c core.bin firmware.elf");
            if (g_lastCrash.haveDump) {
                Serial.printf("*** In task %s at 0x%08lx (cause %lu, address 0x%08lx)%s\n",
                              g_lastCrash.task, (unsigned long)g_lastCrash.pc,
                              (unsigned long)g_lastCrash.cause, (unsigned long)g_lastCrash.vaddr,
                              g_lastCrash.dumpOlder ? " -- written by other firmware" : "");
                Serial.print("*** Backtrace:");
                for (uint8_t i = 0; i < g_lastCrash.btN; i++)
                    Serial.printf(" 0x%08lx", (unsigned long)g_lastCrash.bt[i]);
                Serial.println();
            }
            break;
        default:
            break;
    }
}

// ---- Arduino setup / loop ----
void setup() {
    // Before anything else can allocate: the breadcrumb has to be read out
    // while it is still the previous life's, not this one's.
    crashReportInit();
    Serial.begin(SERIAL_BAUD);
#if defined(TWATCH_S3)
    // Native USB: with nothing reading the port, every print would otherwise
    // wait its full timeout for a host, and after the chatty first-boot
    // calibration the loop crawled so slowly the screen looked frozen black.
    // Drop the bytes instead when nobody is listening.
    Serial.setTxTimeoutMs(0);
#endif
    delay(200);
    Serial.println();
    printBootBanner();
#if defined(TWATCH_S3)
    twatchPowerUp();
#endif
#if defined(CYD35)
    // One-time diagnostic: is PSRAM actually present on this unit? The
    // "no PSRAM" conclusion driving the no-full-framebuffer tradeoff
    // (see `frame`'s declaration up top) was from an earlier pass --
    // worth confirming directly before deciding whether a PSRAM-backed
    // full double buffer is even on the table.
    Serial.printf("PSRAM found: %s (%u bytes)\n", psramFound() ? "yes" : "no", (unsigned)ESP.getPsramSize());
#endif

    // Backlight: the original board uses GPIO21 for this, the
    // JC2432W328C uses GPIO27 (confirmed by sweeping candidate pins on
    // a physical unit). Driving both HIGH is harmless either way —
    // each is simply an unused GPIO on the "other" board — and means
    // the screen lights up before we've even figured out which board
    // this is.
    //
    // GPIO21 exception on AWOK: it's TOUCH_CS there, not a spare. A
    // brief digitalWrite HIGH pre-init just holds CS deasserted and
    // would be harmless on its own, but the LEDC attach further down
    // would fight TFT_eSPI's control of the pin, so pin 21 is skipped
    // off entirely on AWOK for consistency with that.
// Not on AWOK (TOUCH_CS there) and not on either RL Phantom, where GPIO21 is
// the capacitive controller's INTERRUPT line. Driving it high at boot is the
// same mistake as the LEDC attach further down, just earlier.
// ... and not on the CrowPanel 7, where GPIO21 is the panel's BLUE-0 data
// line. Driving it high before the panel driver claims it is a stripe down the
// picture at best.
#if !defined(AWOK) && !defined(RLPHANTOM) && !defined(RLPHANTOM_R) && !defined(TWATCH_S3) && !defined(CROWPANEL7)
    pinMode(21, OUTPUT); digitalWrite(21, HIGH);
#endif
#if defined(TWATCH_S3)
    // GPIO27 and GPIO32 are the S3's PSRAM lines: touching either hangs the
    // chip until the watchdog reboots it. The watch's backlight is GPIO45.
    pinMode(45, OUTPUT); digitalWrite(45, HIGH);
#elif defined(CROWPANEL7)
    // Nothing at all. This board's backlight is not a GPIO -- it is an I2C
    // command to the helper MCU, sent once Wire is up. GPIO27/32 are the same
    // OPI PSRAM lines the watch avoids, and every other candidate here is an
    // RGB data line.
#else
    pinMode(27, OUTPUT); digitalWrite(27, HIGH);
    pinMode(32, OUTPUT); digitalWrite(32, HIGH);  // AWOK's real BL pin; unused GPIO on the other two boards
#endif

    tft.init();

#if defined(CROWPANEL7_PANELTEST)
    // Bring-up only: prove the panel before anything else can be blamed for
    // it. Colour bars in a known order (so a red/blue swap in the pin map is
    // visible rather than merely wrong), a one-pixel grid (so a pixel clock
    // that is running away shows as a moving moire instead of "looks fine"),
    // and the measured frame period -- quiet, then with a flash write every
    // 20 ms in alternate three-second phases, which is what flash costs the
    // picture in numbers.
    crowPanelTest();
#endif

    // Load persisted settings before the first real pixel is drawn, so
    // boot itself already reflects the saved theme/invert choice
    // instead of flashing the defaults for a moment first. Moved ahead
    // of tft.setRotation() below so that call can already use the
    // saved rotation instead of always starting from the board default.
    Settings::load();
    Clock::begin();   // after Settings: the zone is applied there, the history here
#if defined(TWATCH_S3)
    twatchRtcBegin();      // after Clock::begin(): a real time beats the note's guess
    twatchHapticBegin();
    twatchMotionBegin();
    twatchApplySteady();   // after Settings::load(), which it reads
#endif
    Security::begin();
    // Which version lives in this slot, and whether this boot is a fresh
    // update on probation or the aftermath of one that was rolled back.
    OtaCore::boot();
#if !defined(AWOK)
    // AWOK has no rotate button and stays fixed at its one physical
    // orientation (see screenRotation's own comment above) -- only
    // boards that can actually rotate restore a saved orientation.
    screenRotation = Settings::rotation();
#endif
    // Landscape (320 wide × 240 tall) — the CYD's natural orientation
    // with the ST7789 driver. The 0xC2 unlock that was here was for
    // ST7796U and will lock up an ST7789 panel; do not re-add it unless
    // we confirm the panel is actually ST7796.
    tft.setRotation(screenRotation);
#if defined(AWOK)
    // No rotate button on this board (see the rotate handler in
    // loop(), not even compiled in on AWOK) -- hide the icon too so
    // there's nothing dead-looking left in the title bar.
    Theme::setRotateIconVisible(false);
#endif
    // See PANEL_NEEDS_INVERSION's definition up top for why this XORs
    // against a per-board baseline instead of calling invertDisplay()
    // with Settings::inverted() directly.
    tft.invertDisplay(PANEL_NEEDS_INVERSION != Settings::inverted());
    applyColorOrder();
    tft.fillScreen(Theme::BG);

    // Hand the digitalWrite(HIGH) backlight pins above off to LEDC PWM
    // so the settings-menu brightness slider can dim them — same
    // "drive both boards' pin, only one is really wired" reasoning as
    // the digitalWrite call, just with a duty cycle instead of a flat
    // HIGH. BL_CH_ORIG/GPIO21 is skipped on AWOK because it's TOUCH_CS
    // there (same reasoning as the digitalWrite skip above) — attaching
    // LEDC to it would fight TFT_eSPI's control of the pin.
// GPIO21 is the backlight on the 2.8" boards and NOT on two others: it is
// TOUCH_CS on AWOK, and the capacitive controller's INTERRUPT line on the RL
// Phantom. Driving a 5 kHz PWM onto either is the kind of fault that looks
// like dead touch, which is exactly how it presented on the Phantom.
#if defined(CROWPANEL7)
    // No LEDC at all: the backlight is an I2C command byte. Claiming channels
    // here would only take them away from something that has a pin.
#elif defined(TWATCH_S3)
    // One backlight, GPIO45, on the first channel. The CYD pins below are
    // flash/PSRAM lines and the power chip's interrupt on an S3.
    ledcSetup(BL_CH_ORIG, 5000, 8);
    ledcAttachPin(BL_PIN_TWATCH, BL_CH_ORIG);
#else
#if !defined(AWOK) && !defined(RLPHANTOM) && !defined(RLPHANTOM_R)
    ledcSetup(BL_CH_ORIG, 5000, 8);
    ledcAttachPin(BL_PIN_ORIG, BL_CH_ORIG);
#endif
    ledcSetup(BL_CH_CAP, 5000, 8);
    ledcAttachPin(BL_PIN_CAP, BL_CH_CAP);
    ledcSetup(BL_CH_AWOK, 5000, 8);
    ledcAttachPin(BL_PIN_AWOK, BL_CH_AWOK);
#endif
    applyBrightness();
    // A saved core clock has to be restored here too, or the setting silently
    // reverts to 240 MHz on every reboot and looks like it never took.
    applyCpuClock();
    // The light on the back, and its half-second sweep during the splash.
    StatusLight::begin();
    StatusLight::boot(millis());

    // The boot check: a few seconds on the saved WiFi asking the site whether
    // there is a newer release, and only ever HERE, before Bluetooth exists.
    // Joining WiFi on a running board means giving Bluetooth up until the
    // next restart, so on a running board the same question is a whole mode
    // (UPDATE OVER WIFI). And before the frame buffer, too: the TLS
    // handshake wants about 40 KB in one piece, and with the buffer in place
    // the largest block is 35 KB -- measured, the first try returned -1.
    // Tells, never installs. Skipped with no saved network, with the board
    // locked, or with UPDATE CHECK off.
    bool bootCheckRan = false;
    if (takeBootCheckSkip()) {
        Serial.println("[ota] boot check skipped: the frame buffer failed after the last one");
    } else if (Settings::updateCheck() && !Security::locked() && OtaCore::available() && OtaWifi::hasSaved()) {
        bootCheckRan = true;
        // The backlight down first, for the same reason it goes down at the
        // radio start below: WiFi's RF calibration plus a full backlight is
        // more than a weak USB port holds, and the first run of this check
        // browned the Phantom out into a second boot.
#if defined(CROWPANEL7)
        CrowBL::set(24);
#else
        ledcWrite(BL_CH_ORIG, 24);
        ledcWrite(BL_CH_CAP,  24);
        ledcWrite(BL_CH_AWOK, 24);
#endif
        tft.fillScreen(Theme::BG);
        tft.setTextSize(1);
        tft.setTextWrap(false);
        tft.setTextColor(Theme::CYAN, Theme::BG);
        const char* m = "CHECKING FOR UPDATES...";
        tft.setCursor((tft.width() - tft.textWidth(m)) / 2, tft.height() / 2 - 4);
        tft.print(m);
        OtaWifi::bootCheck(9000);
        tft.fillScreen(Theme::BG);
    }


#if defined(CYD35)
    // No FULL-screen double buffer on this board — confirmed on real
    // hardware that the 320x480 panel's ~150KB sprite need exceeds the
    // largest contiguous free heap block (~110KB, no PSRAM). `canvas`
    // aliases `tft` directly for this build (see its declaration up
    // top) and is what most screens draw into, unbuffered.
    //
    // The CLEAR screen is the exception: it's the most visibly animated
    // (Squachy + a background effect), so it gets a real half-height
    // sprite (~76KB at 8-bit, comfortably fits) and renders in two
    // bands via setViewport() -- see the AppState::CLEAR case in
    // loop(). `advance` on uiClearTick()/Squachy::tick()/
    // Theme::drawDigitalRain() gates state mutation to the first band
    // only, so calling them twice per logical frame doesn't double
    // animation speed.
    canvas->setTextSize(1);
    frame.setColorDepth(8);
    if (!frame.createSprite(tft.width(), tft.height() / 2)) {
        Serial.println("ERROR: cyd35 half-height frame buffer allocation failed");
    }
    frame.setTextSize(1);
#else
    // 8-bit (palette) mode: 320x240 needs ~75KB instead of ~150KB at
    // 16-bit — the full 16-bit buffer didn't fit in the available
    // contiguous heap on this board.
    frame.setColorDepth(8);
#if defined(TWATCH_S3) || (defined(CROWPANEL7) && CROWPANEL_FRAME_INTERNAL)
    // 57.6 KB fits in internal RAM with room to spare, and a sprite in
    // PSRAM pushes slower and cannot go by DMA. Keep it inside.
    //
    // CrowPanel: not an economy but the fix for a twitching picture -- the
    // panel's DMA reads its framebuffer from PSRAM, and the UI's scattered
    // writes into a PSRAM sprite starve it (see crowpanel7_board.h).
    frame.setAttribute(PSRAM_ENABLE, false);
#endif
#if defined(CROWPANEL7) && CROWPANEL_FRAME_INTERNAL
    // PSRAM_ENABLE=false only makes TFT_eSPI call plain calloc() -- and on a
    // PSRAM-enabled Arduino core plain calloc() of anything over
    // CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (4 KB here) is served from PSRAM
    // regardless. Measured: the frame landed at 0x3D8BC308, which is PSRAM.
    // Raise that threshold for this one allocation and put it straight back.
    heap_caps_malloc_extmem_enable(1u << 20);
#endif
    const bool frameOk = frame.createSprite(tft.width(), tft.height());
#if defined(CROWPANEL7) && CROWPANEL_FRAME_INTERNAL
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
    // Say where it landed: internal SRAM sits at 0x3FC8xxxx on the S3, PSRAM
    // at 0x3C000000..0x3DFFFFFF, and a frame in the wrong one is the twitch.
    if (frameOk) {
        const uintptr_t a = (uintptr_t)frame.buf();
        Serial.printf("[boot] frame %ux%u at %p (%s)\n", (unsigned)frame.bufW(), (unsigned)frame.bufH(), (void*)a,
                      (a >= 0x3C000000u && a < 0x3E000000u) ? "PSRAM" : "internal");
    }
#endif
    if (!frameOk) {
        Serial.println("ERROR: frame buffer allocation failed (low memory)");
#if HAVE_NVS_ERASE
        if (bootCheckRan) {
            // The check's leftovers took the block. Once more, without it.
            g_bootCheckSkip = CHKSKIP_MAGIC;
            serialFlush();
            delay(20);
            esp_restart();
        }
#else
        (void)bootCheckRan;
#endif
    }
    frame.setTextSize(1);
#endif

    // Builds a 512-byte lookup table and nothing else; it touches no bus
    // and can go anywhere after the display is up.
    FramePush::begin();

#if defined(CYD35)
    // The standalone XPT2046_Touchscreen library (own SPIClass, own
    // IRQ pin) produced constant garbage reads and a free-running IRQ
    // here -- not a wrong-pin problem, a second SPI master fighting
    // TFT_eSPI for the same physical bus. Same wiring shape as AWOK
    // (touch shares the display's own SPI bus, no dedicated
    // peripheral), so this uses the same fix: drive touch entirely
    // through TFT_eSPI's own calibrateTouch()/setTouch()/getTouch()
    // path instead, which shares the bus properly. No I2C cap-touch
    // chip on this board either, so no probe -- no touch.begin(), no
    // touchSPI. All subsequent touch reads go through pollTouch()'s
    // CYD35 branch (tft.getTouch()).
    usingCapTouch = false;
    Serial.println("cyd35 build -- XPT2046 on shared VSPI bus via TFT_eSPI.");
#elif defined(TOUCH_RAW_SHARED_BUS)
    // RL Phantom, resistive variant. The chip sits on the display's own bus
    // (TOUCH_CS=33, armed by TFT_eSPI once rlphantom_r_user_setup.h is in
    // scope), so there is no probe, no touch.begin() and no touchSPI -- but
    // unlike AWOK the raw values come back to us and pollTouch() does its own
    // rotation maths on them, so touch follows the screen round.
    usingCapTouch = false;
    Serial.println("RL Phantom (resistive) -- XPT2046 on shared bus, raw reads + rotation maths.");
#elif defined(CROWPANEL7)
    // The GT911, on the same I2C bus as the backlight helper -- which is why
    // there is no Wire.end() anywhere on this path. It will not answer until
    // the helper has been told to wake it and its INT line pulsed low, so
    // Gt911::begin() does that dance before it gives up.
    usingCapTouch = Gt911::begin();
    Serial.println(usingCapTouch ? "CrowPanel 7 -- GT911 capacitive touch answered."
                                 : "CrowPanel 7 -- GT911 did not answer; no touch.");
#elif defined(TWATCH_S3)
    // The T-Watch's FT6336, on I2C SDA 39 / SCL 40 at 0x38. No reset line;
    // the AXP2101 powers it (ALDO3) in twatchPowerUp(), before this runs.
    CapTouch::begin(39, 40, -1, 0x38);
    usingCapTouch = CapTouch::probe();
    Serial.println(usingCapTouch ? "T-Watch S3 -- FT6336 capacitive touch answered."
                                 : "T-Watch S3 -- FT6336 did not answer; no touch.");
#elif defined(TOUCH_ON_DISPLAY_BUS)
    // AWOK's XPT2046 sits on the display's own shared VSPI bus (TOUCH_CS=21,
    // already armed by TFT_eSPI itself once awok_user_setup.h's #define
    // is in scope) and is driven entirely through TFT_eSPI's own touch
    // path -- no I2C cap-touch probe (this board has no cap-touch chip
    // at all), no touch.begin(), no touchSPI. All subsequent touch
    // reads go through pollTouch()'s AWOK branch (tft.getTouch()).
    usingCapTouch = false;
    Serial.println("AWOK build -- XPT2046 on shared VSPI bus via TFT_eSPI.");
#else
    // Touch: probe for the capacitive controller first (I2C 0x15 on
    // SDA=33/SCL=32, reset on GPIO25 — the JC2432W328C). If it doesn't
    // answer, release the I2C bus and fall back to the resistive
    // XPT2046 on its own dedicated SPI bus (the original board) — a
    // genuinely separate HSPI peripheral on pins that don't overlap
    // the display's VSPI pins at all, so there's no GPIO-matrix
    // conflict either way.
    CapTouch::begin(CAP_SDA, CAP_SCL, CAP_RST);
    usingCapTouch = CapTouch::probe();
    if (usingCapTouch) {
        Serial.println("Capacitive touch (CST816/820) detected -- JC2432W328C-style board.");
    } else {
        Wire.end();
        Serial.println("No capacitive touch found -- assuming resistive XPT2046.");
        touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
        touch.begin(touchSPI);
        touch.setRotation(0);
    }
#endif

    // Recovery escape hatch: hold touch ANYWHERE for ~1s right here to
    // wipe a saved calibration back to defaults. A bad calibration can
    // make touch too inaccurate to reliably re-tap a "recalibrate"
    // button, so this needs no precision at all — just a hold anywhere
    // during the window right after boot.
    tft.fillScreen(Theme::BG);
    tft.setTextColor(Theme::AMBER, Theme::BG);
    tft.setTextSize(1);
    tft.setCursor(4, 4);
    tft.print("Hold anywhere now to reset touch calibration...");
    {
        uint32_t holdStart = 0;
        uint32_t windowStart = millis();
        while (millis() - windowStart < 1200) {
            int16_t a, b;
#if defined(CROWPANEL7)
            // The GT911 is neither of the two below; the two-way dispatch
            // polled a capacitive controller that is not on this bus.
            bool down = readTouchRaw(a, b);
#else
            bool down = usingCapTouch ? rawReadCap(a, b) : rawReadResistive(a, b);
#endif
            if (down) {
                if (holdStart == 0) holdStart = millis();
                else if (millis() - holdStart > 800) {
                    TouchCal::reset();
#if defined(TOUCH_ON_DISPLAY_BUS) || defined(CYD35)
                    // TouchCal::reset() clears the Fit and the 2.8"-style
                    // calibration; these boards' old TFT_eSPI blobs live in
                    // a namespace of their own, and SKIP would bring them
                    // straight back.
                    clearTftEspiBlobs();
#endif
                    tft.fillScreen(Theme::BG);
                    tft.setTextColor(Theme::AMBER, Theme::BG);
                    tft.setTextSize(2);
                    const char* msg = "CALIBRATION RESET";
                    tft.setCursor((tft.width() - tft.textWidth(msg)) / 2, tft.height() / 2 - 8);
                    tft.print(msg);
                    delay(1200);
                    break;
                }
            } else {
                holdStart = 0;
            }
            delay(10);
        }
    }
    tft.fillScreen(Theme::BG);

    // Touch calibration. Every board gets the five-target calibration once:
    // at first boot, and once more after updating from a firmware that used
    // the old corner calibrations, which were wrong in portrait on the 2.8"
    // and sat under the lip of a case on AWOK. A board with a mapping worth
    // keeping is offered SKIP, which keeps it and stops asking.
    //
    // Nobody touching the screen (a board on a desk, powered from USB) is
    // not an answer: the flow times out, boot carries on with the old
    // mapping, and it asks again next boot.
    //
    // Hardware only. The emulators build this file too, and their taps are
    // injected against the compiled-in ranges -- a calibration screen would
    // just sit there waiting for a finger.
    initTouchFit();
#if defined(ESP32) && !defined(CROWPANEL7)
    if (s_calSource != CalSource::SAVED) {
        Serial.println("Touch: no five-target calibration yet -- running it now.");
        runTouchCalibration();
    }
#endif
#if defined(CROWPANEL7)
    // Never on first boot here. The GT911 reports panel pixels, so the
    // compiled-in fit is already the identity and there is nothing to
    // calibrate -- making someone tap five targets would only replace an
    // exact mapping with a hand-aimed one. SETTINGS can still run it.
    Serial.println("Touch: GT911 reports panel pixels; no calibration needed.");
#endif

    // Seed the PRNG so the digital rain starts in a fresh-looking state
    // on every boot.
#if defined(CROWPANEL7)
    // Not analogRead(34) here: GPIO34 is an OPI PSRAM data line on this
    // board's N16R8 module, and not an ADC pin on the S3 at all. The
    // hardware RNG is a fine seed and is already fed by the radios.
    randomSeed(esp_random());
#else
    // Analog read on a floating pin is plenty.
    randomSeed(analogRead(34));
#endif

    // The backlight goes down while the radios come up, and back to your
    // setting once they are running.
    //
    // WiFi calibrates its RF front end when it starts and Bluetooth does the
    // same a moment later, and together they are the largest current the
    // board ever draws. On top of a full-brightness backlight that was more
    // than a weak USB port could hold: the JC2432W328C capacitive board
    // browned out at exactly this point on every boot -- three seconds a
    // cycle, forever -- off any supply short of a powered hub. The backlight
    // is the one large load that nobody misses for a second at boot.
#if defined(CROWPANEL7)
    CrowBL::set(24);
#else
    ledcWrite(BL_CH_ORIG, 24);
    ledcWrite(BL_CH_CAP,  24);
    ledcWrite(BL_CH_AWOK, 24);
#endif

    // The black box, before the radios: this boot's record -- with the crash
    // in it when there was one -- then the log as the last boot left it, so
    // nothing live has landed in the log yet. After the boot check, so the
    // record has the time when there is a network to ask.
    if (BlackBox::begin()) {
        BlackBox::BootRecord br;
        memset(&br, 0, sizeof br);
        br.reason = (uint8_t)g_resetReason;
        br.epoch  = Clock::trusted() ? Clock::nowEpoch() : 0;
        if (g_lastCrash.valid) {
            br.flags    |= BlackBox::BOOT_CRUMB;
            br.upSec     = g_lastCrash.uptimeMs / 1000u;
            br.heapFree  = g_lastCrash.heapFree;
            br.heapBlock = g_lastCrash.heapBlock;
            br.screen    = g_lastCrash.screen;
        }
        if (g_lastCrash.haveDump) {
            br.flags |= BlackBox::BOOT_DUMP;
            if (g_lastCrash.dumpOlder) br.flags |= BlackBox::BOOT_DUMP_OLDER;
            br.pc    = g_lastCrash.pc;
            br.cause = g_lastCrash.cause;
            strncpy(br.task, g_lastCrash.task, sizeof br.task - 1);
        }
        BlackBox::noteBoot(br);
    }

#if defined(TWATCH_S3)
    // What the watch is like at the moment the radios start. A boot that
    // ran the touch calibration first hears the room; a plain boot comes up
    // deaf. Logged so the two kinds of boot can be compared line by line.
    {
        nvs_stats_t st = {};
        nvs_get_stats(NULL, &st);
        snprintf(g_bootRadioLine, sizeof g_bootRadioLine,
                 "radios started at %lu ms; chip %.1f C; heap %lu; nvs used %u free %u; calibration %s; self-heal restart %u",
                 (unsigned long)millis(), temperatureRead(), (unsigned long)ESP.getFreeHeap(),
                 (unsigned)st.used_entries, (unsigned)st.free_entries,
                 s_calRanThisBoot ? "RAN this boot" : "skipped", (unsigned)s_healCount);
        Serial.printf("[boot] %s\n", g_bootRadioLine);
    }
#endif
    engine.init();
#if defined(CROWPANEL7_PERIPH_PROBE)
    crowPeriphProbe();
#endif
    // After the engine: the card leans on the lifetime counts to pick which
    // type sits out, and those are read in init().
    Bingo::begin(engine);
    Dex::begin();
    Regulars::begin();
    Squachy::setIdleProvider([]() { return Notices::idleLine(engine); });
#if SQUACH_MESH
    // After the radio is up and before anything can ask whether messages are
    // ready: this is where the crypto self-test runs, on the real cipher,
    // against a frame built by an independent implementation.
    MeshTalk::begin();
#endif
    applyBrightness();
    // After a wipe the board comes back the way the wipe asked: straight to the
    // main screen, unlocked, with no splash and no boot quip, after a duress
    // PIN -- so it reads as an unlock and not a restart -- or locked after the
    // tenth wrong guess.
    const WipeBoot wb = takeWipeBoot();
    if (wb == WipeBoot::UNLOCKED) {
        Security::forceUnlock();
        enterClear();
    } else {
        if (wb == WipeBoot::LOCKED) Security::lock();
        Squachy::trigger(Squachy::Event::BOOTED, DetectionType::UNKNOWN, engine.lifetimeTotal());
        enterBoot();
    }
}

// ---- PRIM: what each drawing primitive costs on the real sprite ----
// Every framerate conversation before this was a guess about whether a
// fillCircle is expensive or a gradient is, made without a single number.
// PRIM on the console draws each primitive N times into the frame buffer
// and prints the microseconds per call. The frame it scribbles on is
// repainted on the very next pass, so nothing is visible.
volatile bool g_benchPrimNow = false;
#if defined(ARDUINO_ARCH_ESP32)   // the board only: the emulator's sprite has none of this
template <typename F>
static void primTime(const char* name, uint32_t n, F body) {
    const uint32_t t0 = micros();
    for (uint32_t i = 0; i < n; i++) body(i);
    const uint32_t us = micros() - t0;
    Serial.printf("[prim] %-28s %6lu.%02lu us/call  (%lu x)\n", name,
                  (unsigned long)(us / n), (unsigned long)((us * 100UL / n) % 100), (unsigned long)n);
}
static void runPrimBench() {
    if (!frameBufferOk) { Serial.println("[prim] no frame buffer"); return; }
    ResizableSprite& f = frame;
    const int w = f.width(), h = f.height();
    volatile uint32_t sink = 0;
    Serial.printf("[prim] sprite %dx%d, 8-bit\n", w, h);
    primTime("drawFastHLine 320",   2000, [&](uint32_t i){ f.drawFastHLine(0, (int)(i & 127), w, (uint16_t)i); });
    primTime("drawFastVLine 200",   2000, [&](uint32_t i){ f.drawFastVLine((int)(i & 255), 0, 200, (uint16_t)i); });
    primTime("fillRect 20x20",      2000, [&](uint32_t i){ f.fillRect((int)(i & 255), (int)((i >> 2) & 127), 20, 20, (uint16_t)i); });
    primTime("fillRect full screen",  20, [&](uint32_t i){ f.fillRect(0, 0, w, h, (uint16_t)i); });
    primTime("drawPixel",          20000, [&](uint32_t i){ f.drawPixel((int)(i & 255), (int)((i >> 4) & 127), (uint16_t)i); });
    primTime("drawLine 100x60",     1000, [&](uint32_t i){ f.drawLine((int)(i & 127), 0, (int)(i & 127) + 100, 60, (uint16_t)i); });
    primTime("fillCircle r10",      1000, [&](uint32_t i){ f.fillCircle(20 + (int)(i & 255), 20 + (int)((i >> 2) & 127), 10, (uint16_t)i); });
    primTime("fillCircle r30",       300, [&](uint32_t i){ f.fillCircle(40 + (int)(i & 127), 40 + (int)((i >> 1) & 127), 30, (uint16_t)i); });
    primTime("fillRoundRect 30x20 r5",1000,[&](uint32_t i){ f.fillRoundRect((int)(i & 255), (int)((i >> 2) & 127), 30, 20, 5, (uint16_t)i); });
    primTime("fillEllipse 12x8",    1000, [&](uint32_t i){ f.fillEllipse(20 + (int)(i & 255), 20 + (int)((i >> 2) & 127), 12, 8, (uint16_t)i); });
    primTime("fillTriangle 30x20",  1000, [&](uint32_t i){ int x = (int)(i & 255), y = (int)((i >> 2) & 127); f.fillTriangle(x, y + 20, x + 15, y, x + 30, y + 20, (uint16_t)i); });
    f.setTextSize(1); f.setTextColor(Theme::CYAN, Theme::BG);
    primTime("print 11 chars size 1", 300, [&](uint32_t i){ f.setCursor((int)(i & 127), (int)((i >> 1) & 127)); f.print("HELLO WORLD"); });
    f.setTextSize(2);
    primTime("print 11 chars size 2", 300, [&](uint32_t i){ f.setCursor((int)(i & 63), (int)((i >> 1) & 127)); f.print("HELLO WORLD"); });
    f.setTextSize(1);
    f.setTextFont(2);
    primTime("print 11 chars font 2",  300, [&](uint32_t i){ f.setCursor((int)(i & 127), (int)((i >> 1) & 127)); f.print("HELLO WORLD"); });
    f.setTextFont(1);
    primTime("Bangers NEARBY LG",      50, [&](uint32_t i){ Theme::drawBangersText(f, 40, 60, "NEARBY", Theme::AMBER, Theme::BangersSize::LG); });
    primTime("Bangers NEARBY MD",      50, [&](uint32_t i){ Theme::drawBangersText(f, 40, 60, "NEARBY", Theme::AMBER, Theme::BangersSize::MD); });
    primTime("Theme::blend",        20000, [&](uint32_t i){ sink += Theme::blend((uint16_t)i, (uint16_t)(i * 3), (uint16_t)(i & 255)); });
    primTime("color565",            20000, [&](uint32_t i){ sink += f.color565((uint8_t)i, (uint8_t)(i >> 2), (uint8_t)(i >> 4)); });
    primTime("sinf",                20000, [&](uint32_t i){ sink += (uint32_t)(sinf((float)i * 0.01f) * 100.0f); });
    primTime("float mul+add",       20000, [&](uint32_t i){ sink += (uint32_t)((float)i * 1.7f + 3.2f); });
    primTime("int mul+add",         20000, [&](uint32_t i){ sink += i * 17u + 3u; });
    // The library's own versions of the four the sprite subclass takes over,
    // so the gain is on the record next to the cost. And then the proof.
    primTime("drawPixel (library)",  20000, [&](uint32_t i){ f.basePixel((int)(i & 255), (int)((i >> 4) & 127), (uint16_t)i); });
    primTime("drawFastVLine 200 (library)", 2000, [&](uint32_t i){ f.baseVLine((int)(i & 255), 0, 200, (uint16_t)i); });
    primTime("drawLine 100x60 (library)",   1000, [&](uint32_t i){ f.baseLine((int)(i & 127), 0, (int)(i & 127) + 100, 60, (uint16_t)i); });
    primTime("11 chars size 1 (library)",    300, [&](uint32_t i){ for (int k = 0; k < 11; k++) f.baseChar((int)(i & 127) + k * 6, (int)((i >> 1) & 127), (uint16_t)('A' + k), Theme::CYAN, Theme::BG, 1); });
    primTime("11 chars size 1 (fast)",       300, [&](uint32_t i){ for (int k = 0; k < 11; k++) f.drawChar((int)(i & 127) + k * 6, (int)((i >> 1) & 127), (uint16_t)('A' + k), Theme::CYAN, Theme::BG, 1); });
    primTime("11 chars size 2 (library)",    300, [&](uint32_t i){ for (int k = 0; k < 11; k++) f.baseChar((int)(i & 63) + k * 12, (int)((i >> 1) & 127), (uint16_t)('A' + k), Theme::CYAN, Theme::BG, 2); });
    primTime("11 chars size 2 (fast)",       300, [&](uint32_t i){ for (int k = 0; k < 11; k++) f.drawChar((int)(i & 63) + k * 12, (int)((i >> 1) & 127), (uint16_t)('A' + k), Theme::CYAN, Theme::BG, 2); });
    const int bad = f.selfCheck(Serial);
    Serial.printf("[check] %s\n", bad == 0 ? "every fast path is pixel-identical to the library" : "FAST PATHS DIFFER -- do not ship");
    primTime("index arithmetic only",20000, [&](uint32_t i){ sink += (uint32_t)((int)(i & 255) + (int)((i >> 4) & 127)); });
    Serial.printf("[prim] done (%lu)\n", (unsigned long)sink);
}
#endif

// ---- Frame timing ----
// Wall-clock cost of the two things that actually set the frame rate:
// pushing the sprite over SPI, and everything else put together. Both
// are exponentially smoothed (1/8 per frame) because the raw per-frame
// numbers jitter by several ms and are unreadable on a live screen.
//
// The push is the interesting one: it's a fixed 153,600 bytes at
// 16bpp, so it costs whatever SPI_FREQUENCY says it costs and nothing
// else in the firmware can move it. At 40MHz that's ~30.7ms, which is
// most of a frame -- reading the real number here is the only way to
// tell whether a change to that clock did what the arithmetic claims.
static uint32_t s_pushUsAvg  = 0;
static uint32_t s_frameUsAvg = 0;
static uint32_t s_loopsSinceSay = 0;
static uint32_t s_pushAccumUs = 0;   // summed within a frame: cyd35 pushes twice
#if defined(CYD35)
static uint32_t s_bandUs[2] = {0, 0};      // measurement: the 3.5"'s two draw passes
#endif

static inline void pushFrame(int x, int y) {
#if defined(TWATCH_S3)
    if (s_panelAsleep) return;   // nothing to show it to; see applyBrightness()
#endif
    uint32_t t0 = micros();
    // The overlapped push converts the next 64 bytes while the previous 64
    // are on the wire, instead of spinning -- see frame_push.h. It declines
    // rather than half-draws, and the ordinary push is what it declines to;
    // both leave the frame fully on the panel before the clock below stops,
    // so DIAGNOSTICS and the [frame] line measure the same thing either way.
    // The buffer and its REAL size: with a viewport set the sprite reports the
    // viewport's size, not the buffer's, and cyd35 pushes through one.
#if defined(CROWPANEL7)
    // No wire to overlap and no pushSprite fallback: the framebuffer is
    // memory, and CrowBlit writes the changed rows straight into it, doing
    // the 8->16 bit conversion on the way. It declines only if the panel
    // never came up, in which case there is nowhere to put the frame at all.
    CrowBlit::push(frame.buf(), frame.bufW(), frame.bufH(), x, y);
#else
    if (frame.getColorDepth() != 8 ||
        !FramePush::push(tft, frame.buf(), frame.bufW(), frame.bufH(), x, y)) {
        frame.pushSprite(x, y);
        FramePush::invalidate();   // the panel now holds something push() did not record
    }
#endif
    s_pushAccumUs += micros() - t0;
}

// Draw a whole screen the way the 3.5" has to: into the half-height sprite
// twice, once for each band, and push each band as it is finished.
//
// Every screen except the main one, boot and the alerts has always drawn
// STRAIGHT AT THE PANEL on that board -- canvas points at tft there, because
// the sprite is only half a screen -- so you watch each rectangle and each
// line land, one at a time. That is the flicker. It also means a menu, which
// does not change from one frame to the next, is re-sent to the panel in full
// forever: on every other board it goes through the sprite, the row hashes
// all match and it sends nothing at all.
//
// `draw` is called once per band and handed the sprite and an `advance` flag,
// false on the second pass, for anything that steps on its own clock. A
// screen has to be safe to draw twice with the same `now` to come through
// here: no random(), no state that moves by the call rather than by the
// clock. The UI screens use no random() at all, and the two things that do
// move by the call -- the animated background and the mascot -- both already
// take the flag, because the main screen needed exactly this.
//
// Everywhere else this is one call on the full-screen sprite, which is what
// those boards already did, and loop()'s own push at the end still ships it.
template <typename F>
static inline void drawTwoBand(F&& draw) {
#if defined(CYD35)
    if (frameBufferOk) {
        const int halfH = tft.height() / 2;
        DrawBand::set(0, halfH);
        frame.setViewport(0, 0, tft.width(), tft.height(), true);
        draw((TFT_eSPI&)frame, true);
        pushFrame(0, 0);
        DrawBand::set(halfH, tft.height());
        frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
        draw((TFT_eSPI&)frame, false);
        pushFrame(0, halfH);
        DrawBand::all();
        frame.resetViewport();
        return;
    }
#endif
    draw(*canvas, true);
}

static inline uint32_t emaUpdate(uint32_t avg, uint32_t sample) {
    return avg ? avg + ((int32_t)sample - (int32_t)avg) / 8 : sample;
}

// The frame time of the last screen worth timing, for DIAGNOSTICS: the whole
// frame (drawing, push and everything else in the loop), not the backdrop
// alone, and not the menu the reading is taken from. Its own average, begun
// afresh on each such screen once its entry glitch is over, so none of the
// screen before it is mixed in.
static const char* timedScreenName(AppState s) {
    switch (s) {
        case AppState::CLEAR:       return "MAIN";
        case AppState::LOG:         return "LOG";
        case AppState::DESK:        return "DESK";
        case AppState::RAWSCAN:     return "SCAN";
        case AppState::HUNT:        return "HUNT";
        case AppState::ALERT:       return "ALERT";
        case AppState::WATCH_ALERT: return "WATCH";
        default:                    return nullptr;
    }
}
static const char* s_lastScreenName = nullptr;
static uint32_t    s_lastScreenUs   = 0;
static uint32_t    s_lastScreenAt   = 0;     // transitionStart of the screen being timed

void loop() {
    // Cheap and unconditional: available() is a register read, and this
    // is the only way in for the one serial command the firmware takes.
    Clock::pollSerial();
    uint32_t frameStartUs = micros();
    s_loopsSinceSay++;   // the real loop rate, pacing delays included; on the [frame] line
    FrameProf::begin();
    s_pushAccumUs = 0;
    FramePush::newFrame();
    uint32_t now = millis();
#if defined(TWATCH_S3)
    twatchCrownTick(now);
    twatchMotionTick(now);
    twatchXtalTick(now);
    if (g_consoleXtal) { g_consoleXtal = false; twatchXtalStart(); }
    twatchRadioTick(now);
    twatchRadioHealTick(now);
    twatchBatteryTick(now);
    if (g_consoleBatt) { g_consoleBatt = false; twatchBatterySample(BlackBox::BATT_WHY_TIMER); }
    if (g_consoleBattLog) {
        g_consoleBattLog = false;
        Serial.println("[battlog] newest first: boot  up(s)  epoch  mV  %  flags(usb/chg/scr/radio)  cpu  why  chipC  steady  adverts  frames");
        BlackBox::forEachBattery([](const BlackBox::BattRecord& r, void*) {
            Serial.printf("[battlog] %u  %lu  %lu  %u  %u  %c%c%c%c  %u  %u\n", (unsigned)r.boot,
                          (unsigned long)r.upSec, (unsigned long)r.epoch, (unsigned)r.mv, (unsigned)r.pct,
                          (r.flags & BlackBox::BATT_USB) ? 'U' : '-', (r.flags & BlackBox::BATT_CHARGING) ? 'C' : '-',
                          (r.flags & BlackBox::BATT_SCREEN_ON) ? 'S' : '-', (r.flags & BlackBox::BATT_RADIOS_ON) ? 'R' : '-',
                          (unsigned)r.cpuMhz10 * 10, (unsigned)r.why);
            Serial.printf("[battlog]    %d C  %u  %lu  %lu\n", (int)r.chipC, (unsigned)r.steady,
                          (unsigned long)r.adverts, (unsigned long)r.frames);
            return true;
        }, nullptr);
    }
#endif
    Clock::tick(now);   // the note to self, when it is due
#if SQUACH_MESH && defined(BENCH_TOOLS)
    if (g_benchUpdateNow && (state == AppState::CLEAR || state == AppState::DESK)) {
        // Waits for the main screen rather than barging in from wherever the
        // board happens to be -- the same place a person would start from.
        g_benchUpdateNow = false;
        memset(&s_nudge, 0, sizeof s_nudge);
        strncpy(s_nudge.from, "the bench", sizeof s_nudge.from - 1);
        startNudgedUpdate();
    }
    if (g_benchUpdateStop) {
        g_benchUpdateStop = false;
        s_auto.active = false;
        if (OtaWifi::state() != OtaWifi::State::OFF) {
            if (!OtaWifi::end()) { engine.stopUpdateRadio(); enterClear(); }
        }
    }
#endif
    // The bingo card: marks the radio task handed over, the week turning
    // over, and the one flash write that follows a batch of marks.
#if defined(ARDUINO_ARCH_ESP32)
    if (g_benchPrimNow) { g_benchPrimNow = false; runPrimBench(); }
#endif
    Bingo::tick(now);
    Dex::tick(now);
    Regulars::tick(now);
    {
        DetectionType bt = DetectionType::UNKNOWN;
        char sub[40];
        switch (Bingo::takeEvent(bt)) {
            case Bingo::Event::MARKED:
                snprintf(sub, sizeof sub, "%s  (%u of 16)", detectionTypeName(bt),
                         (unsigned)Bingo::markedCount());
                Theme::showToast("SQUARE MARKED", sub, Theme::GREEN, 2200);
                break;
            case Bingo::Event::LINE:
                snprintf(sub, sizeof sub, "%u line%s called", (unsigned)Bingo::linesCalled(),
                         Bingo::linesCalled() == 1 ? "" : "s");
                Theme::showToast("BINGO!", sub, Theme::AMBER, 3000);
                break;
            case Bingo::Event::FULL:
                Theme::showToast("FULL CARD", "Sixteen for sixteen", Theme::AMBER, 4000);
                break;
            case Bingo::Event::NEW_CARD:
                Theme::showToast("NEW BINGO CARD", "A fresh sixteen", Theme::CYAN, 2500);
                break;
            default: break;
        }
    }

    TouchPoint tp = pollTouch();
    // True only on the exact frame a touch begins/ends -- unlike
    // TOUCH_DEBOUNCE_MS below (a cooldown timer that still re-fires on a
    // long-held finger once the cooldown elapses), these compare this
    // frame's tp.valid against last frame's, so they each fire exactly
    // once per physical press no matter how long it's held.
    bool touchJustDown = tp.valid && !prevTouchValid;
    bool touchJustUp    = !tp.valid && prevTouchValid;

    // The tap that wakes a dimmed screen only wakes it. Without this, feeling
    // for the device in the dark cycles a background or opens a menu on the
    // way, because every screen's own handlers see that first touch as a real
    // press. Swallowed for the whole gesture, not just the frame it lands on,
    // so a wake tap that turns into a drag cannot scroll a list either.
    //
    // lastTouch is still updated here, because that is what actually undims
    // on the next frame -- the touch is being consumed, not ignored.
    static bool s_swallowTouch = false;
    if (s_screenDimmed && touchJustDown) {
        s_swallowTouch = true;
        lastTouch = now;
    }
    if (!tp.valid) s_swallowTouch = false;
    if (s_swallowTouch) {
        tp.valid = false;
        touchJustDown = false;
        touchJustUp = false;
    }
    engine.loop();
    floodTick();   // nothing outside a FLOOD_BENCH build
    // The heap at the first pass of loop(), for DIAGNOSTICS' BOOT line.
    static uint32_t s_loopHeapFree = 0, s_loopHeapLargest = 0;
    if (!s_loopHeapFree) {
        s_loopHeapFree    = ESP.getFreeHeap();
        s_loopHeapLargest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        Serial.printf("[boot] heap at the first loop: %lu free, %lu largest\n",
                      (unsigned long)s_loopHeapFree, (unsigned long)s_loopHeapLargest);
    }
    crashCrumbTick(now, engine.lifetimeTotal(), (uint8_t)state);
    // Confirms a probationary image once it has run long enough, and drives a
    // Bluetooth update's flash work -- the BLE callbacks only hand it jobs.
    OtaCore::tick(now);
    OtaBle::tick(now);
    OtaWifi::tick(now);
    if (state == AppState::CLEAR) {
        const char* sub = nullptr;
        bool good = false;
        if (const char* head = OtaCore::takeBootNote(&sub, &good))
            Theme::showToast(head, sub, good ? Theme::CYAN : Theme::AMBER);
    }
#if SQUACH_MESH
    MeshProbe::tick(now);
    Mesh::tick(now);
    MeshTalk::tick(now);
    {
        char who[13];
        if (MeshTalk::takeRead(who, sizeof who)) {
            static char sub[40];
            snprintf(sub, sizeof sub, "%s opened your message", who);
            Theme::showToast("READ", sub, Theme::GREEN, 3000);   // a solid three seconds: it is the whole reply
            // And from the messenger himself, when he is on screen to say it.
            static char line[32];
            snprintf(line, sizeof line, "%s read it.", who);
            if (state == AppState::CLEAR) Squachy::announce(line);
        }
    }
    {
        MeshTalk::NudgeIn n;
        if (MeshTalk::takeNudge(n)) {
            uint8_t mine[3] = { 0, 0, 0 };
            MeshMsg::parseVersion(OtaCore::runningVersion(), mine);
            if (!Settings::remoteUpdate())            Serial.println("[nudge] ignored: REMOTE UPDATE is off");
            else if (Security::locked())              Serial.println("[nudge] ignored: locked");
            else if (!MeshMsg::versionNewer(n.ver, mine)) Serial.println("[nudge] ignored: not newer than this build");
            else { s_nudge = n; s_nudgePending = true; }
        }
        if (s_nudgePending && (int32_t)(now - s_nudge.at) > (int32_t)NUDGE_HOLD_MS) s_nudgePending = false;
        if (s_nudgePending && state == AppState::CLEAR && !Security::locked()) {
            s_nudgePending = false;
            enterNudge();
        }
        MeshTalk::UpdatedIn u;
        if (MeshTalk::takeUpdated(u) && state == AppState::SQUAD_UPDATE) uiSquadUpdateReported(u.from);
        // Somebody offered us their squad. Asked from the main screen only,
        // and never while locked; the offer stays on the air a minute.
        if (MeshTalk::inviteState() == MeshTalk::InviteState::ASKED && state == AppState::CLEAR &&
            !Security::locked()) enterInvite();
    }
    // Beside the radio, not inside the draw: an emote arriving while any other
    // screen is up -- or while boring mode has turned Squachy off -- still has
    // to be taken off the queue and acted on when CLEAR comes back.
    uiClearEmoteTick(now);
#endif

    // The padlock, left of the rotate icon while a PIN is set, on the screens
    // that draw the corner icons. Ahead of the rotate handler, whose oversized
    // target overlaps it, and it takes the whole gesture -- the same swallow a
    // wake tap gets -- so nothing underneath sees the finger.
    if (touchJustDown && Security::enabled() && !Security::locked() &&
        (state == AppState::CLEAR || state == AppState::LOG || state == AppState::SETTINGS ||
         state == AppState::OUTFIT || state == AppState::RAWSCAN || state == AppState::DETECTION_FILTER ||
         state == AppState::IGNORE_LIST || state == AppState::POWER_SAVER || state == AppState::SECURITY ||
         state == AppState::STATUS_LIGHT ||
         state == AppState::DIARY || state == AppState::HUNT || state == AppState::DIAGNOSTICS ||
         state == AppState::DESK) &&
        Theme::lockButtonHit(tp.x, tp.y, tft.width())) {
        lastTouch = now;
        if (onRawScanScreen()) engine.stopRawScan();
        Security::lock();
        enterLocked();
        s_swallowTouch = true;
        tp.valid = false;
        touchJustDown = false;
        touchJustUp = false;
    }

    // Rotate button lives in the title bar's top-right corner, shown on
    // the CLEAR, LOG, SETTINGS and OUTFIT screens (drawTitleBar always
    // draws it on the two boards that have one — gating the hit-test
    // to these keeps it inert wherever there's no title bar drawn at
    // all, i.e. BOOT/ALERT). Not built at all on AWOK: confirmed on
    // real hardware that board's case only holds the panel in one
    // orientation (portrait), so rotation was pure unused complexity
    // there — see Theme::setRotateIconVisible(false) in setup(), which
    // also hides the icon itself, not just this handler. When
    // Settings::rotationLocked() is on, the icon is not drawn either (see
    // drawTitleBar) and this handler is skipped — so there is no control
    // on screen that looks live and does nothing.
#if !defined(AWOK)
    // Neither corner button answers a finger that is carrying Squachy: dragged
    // into a corner, he used to open Settings or rotate the screen mid-carry,
    // and the release that would have dropped him never came.
#if defined(TWATCH_S3)
    // Every AXP2101 register, sixteen to a line, so a deaf boot and a
    // hearing boot can be compared register by register.
    if (g_consoleMotion) {
        g_consoleMotion = false;
        Serial.printf("[motion] %s: x %d y %d z %d, moved %u of the last 128 s, %s; snooze after %u\n",
                      s_bmaOk ? "BMA423" : "no sensor", s_acc[0], s_acc[1], s_acc[2], (unsigned)movedSecs(),
                      twatchStill() ? "STILL" : "MOVING", (unsigned)Settings::autoQuietAfter());
    }
    if (g_consoleBuzz) {
        g_consoleBuzz = false;
        Serial.printf("[buzz] test: screen %s, cpu %u MHz, driver status %02X mode %02X lib %02X\n",
                      s_panelAsleep ? "asleep" : "awake", (unsigned)getCpuFrequencyMhz(),
                      drvRead(0x00), drvRead(0x01), drvRead(0x03));
        twatchBuzz(Buzz::SAMPLE);
        const uint8_t go0 = drvRead(0x0C);
        delay(60);
        const uint8_t go1 = drvRead(0x0C);
        delay(400);
        Serial.printf("[buzz] test: GO %u right after, %u at 60 ms, %u at 460 ms; status %02X\n",
                      go0, go1, drvRead(0x0C), drvRead(0x00));
    }
    if (g_consoleRtc) {
        g_consoleRtc = false;
        bool lost = false, foreign = false;
        const uint32_t t = rtcRead(&lost, &foreign);
        Serial.printf("[rtc] chip %lu, system %lu, %s\n", (unsigned long)t, (unsigned long)Clock::nowEpoch(),
                      lost ? "LOST (backup ran out)" : foreign ? "not set by SquachWatch" : t ? (Clock::trusted() ? "clock trusted" : "clock not trusted") : "no time");
    }
    if (g_consolePmu) {
        g_consolePmu = false;
        for (uint8_t base = 0x00; base < 0xA0; base += 16) {
            char line[80];
            int n = snprintf(line, sizeof line, "[pmu] %02X:", base);
            for (uint8_t k = 0; k < 16; k++) {
                Wire1.beginTransmission(0x34);
                Wire1.write((uint8_t)(base + k));
                uint8_t v = 0xEE;
                if (Wire1.endTransmission(false) == 0 && Wire1.requestFrom((uint8_t)0x34, (uint8_t)1) == 1) v = Wire1.read();
                n += snprintf(line + n, sizeof line - n, " %02X", v);
            }
            Serial.println(line);
        }
    }
#endif
    if (g_consoleInvert) {
        g_consoleInvert = false;
        Settings::toggleInvert();
        tft.invertDisplay(PANEL_NEEDS_INVERSION != Settings::inverted());
        Serial.printf("[console] invert setting %s (panel baseline %s)\n", Settings::inverted() ? "ON" : "OFF",
                      PANEL_NEEDS_INVERSION ? "inverted" : "normal");
    }
    if (g_consoleRotate || (tp.valid && !Settings::rotationLocked() && !Squachy::isHeld() &&
        (state == AppState::CLEAR || state == AppState::LOG ||
                      state == AppState::SETTINGS || state == AppState::OUTFIT ||
                      state == AppState::RAWSCAN || state == AppState::DETECTION_FILTER ||
                      state == AppState::IGNORE_LIST || state == AppState::DESK) &&
        Theme::rotateButtonHit(tp.x, tp.y, tft.width()) &&
        (now - lastTouch) > TOUCH_DEBOUNCE_MS)) {
        if (g_consoleRotate) { g_consoleRotate = false; Serial.printf("[console] rotation -> %u\n", (unsigned)((screenRotation + 1) % 4)); }
        lastTouch = now;
        Squachy::trigger(Squachy::Event::ROTATED);
        screenRotation = (screenRotation + 1) % 4;
        Settings::saveRotation(screenRotation);
        tft.setRotation(screenRotation);
        FramePush::invalidate();   // the panel was re-initialised; send every row next
        // The MX/MY/MV bits applyColorOrder() writes are rotation-
        // dependent, so it has to be reissued alongside every
        // setRotation() call, not just at boot.
        applyColorOrder();
        // Landscape and portrait need differently-*shaped* buffers, but
        // not differently-*sized* ones -- a rectangular panel has the
        // same total pixel count either way (320x240 and 240x320 are
        // both 76800 px), so the buffer allocated once at boot already
        // fits every orientation. resizeInPlace() (see ResizableSprite
        // above) just re-points the sprite's own width/height/stride
        // bookkeeping at the new shape; it never frees or reallocates.
        //
        // This replaces an earlier delete+recreate-every-rotate
        // approach that looked safe (retried 3x with delays between
        // attempts) but confirmed-failed on real hardware despite 123KB
        // of TOTAL free heap -- the largest contiguous block was only
        // 73.7KB against a 76.8KB need, pure fragmentation from
        // WiFi/BLE churn, not a shortage retries could out-wait (delay()
        // doesn't make the ESP32 heap allocator compact anything). Since
        // resizeInPlace() never frees the buffer, that failure mode is
        // now structurally impossible after the first successful boot
        // allocation -- there's no more free/realloc cycle left to lose
        // the fragmentation gamble against.
        if (frame.created()) {
#if defined(CYD35)
            frame.resizeInPlace(tft.width(), tft.height() / 2);
#else
            frame.resizeInPlace(tft.width(), tft.height());
#endif
        } else if (frameBufferOk) {
            // Never got its one-time boot-time allocation in the first
            // place (see setup()) -- same permanent fallback a failed
            // rotate used to trigger, since there's no buffer to resize.
            Serial.println("[rotate] frame buffer was never allocated -- falling back to unbuffered rendering");
            frameBufferOk = false;
#if !defined(CYD35)
            canvas = &tft;
#endif
        }
        transitionStart = now;
    }
#endif  // !AWOK

    // Settings button lives in the title bar's top-left corner, shown
    // on the same screens as the rotate button. Tapping it while
    // already on the SETTINGS screen backs out to CLEAR instead of
    // re-entering itself — same toggle-off feel as the LOG button. From
    // OUTFIT or DETECTION_FILTER (only reachable from SETTINGS' own
    // rows) it backs out to SETTINGS instead of CLEAR, matching where
    // it was entered from — this is the only way back out of either
    // screen, since OUTFIT's own taps are all claimed by the arrows and
    // DETECTION_FILTER's are all claimed by row toggles.
    if (tp.valid && !Squachy::isHeld() &&
        (state == AppState::CLEAR || state == AppState::LOG ||
                      state == AppState::SETTINGS || state == AppState::OUTFIT ||
                      state == AppState::RAWSCAN || state == AppState::DETECTION_FILTER ||
                      state == AppState::IGNORE_LIST || state == AppState::POWER_SAVER ||
                      state == AppState::SECURITY || state == AppState::STATUS_LIGHT ||
                      state == AppState::DESK) &&
        Theme::settingsButtonHit(tp.x, tp.y) &&
        // ...but not where the watch/hunt pill is sitting. The gear's tap box
        // is 55x50, much larger than its 28px glyph, so it reaches into the
        // title bar's middle where the pill lives. The pill is only ever drawn
        // while a target is set, so this gives up nothing the rest of the time.
        !(state == AppState::CLEAR && uiClearWatchPillHit(tp.x, tp.y)) &&
        (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
        lastTouch = now;
        if (state == AppState::STATUS_LIGHT) {
            // Back to the APPEARANCE page it was opened from, not the top.
            enterSettings();
            uiSettingsOpenAppearance(true);
        }
        else if (state == AppState::OUTFIT || state == AppState::DETECTION_FILTER ||
            state == AppState::IGNORE_LIST || state == AppState::POWER_SAVER ||
            state == AppState::SECURITY) enterSettings();
        else if (state == AppState::SETTINGS) {
            // The gear on a sub-page goes back up a level, the way it does
            // from every other screen Settings opens.
            if (uiSettingsCurrentPage() != SettingsPage::MAIN) uiSettingsOpenPage(SettingsPage::MAIN);
            else                                               enterClear();
        }
        else {
            // Leaving RAWSCAN via the settings icon, same as BACK does
            // -- otherwise the raw scan (and the continuous detection
            // scan it's pausing) would just sit there indefinitely
            // while the user is off in Settings.
            if (onRawScanScreen()) engine.stopRawScan();
            // From the desk, Settings' BACK comes back to the desk, the way
            // the alert card's does. The gear drew on the desk from the day
            // desk mode shipped and this is the first time it did anything.
            if (state == AppState::DESK) s_backToDesk = true;
            // The main list, as from every other screen. The desk's own page
            // is the gear at the bottom left of the desk.
            enterSettings();
        }
    }

    // Long-press the middle of the title bar (between the settings and
    // BOTTOM centre now, not the title bar -- the bar is gone. A 1.5 second
    // hold on the middle of the button bar on CLEAR/LOG recalibrates touch.
    //
    // It lands on the LOG button, which is deliberate rather than awkward:
    // the DESK button beside it already carries a four second hold for the
    // outfit unlock, so this follows a gesture the same bar already has
    // instead of inventing one. LOG had no hold of its own.
    //
    // Still no confirmation panel. This is the way back when touch is
    // already too far out to hit a button, which is exactly when a CONFIRM
    // button would be the thing standing between you and a working screen.
    //
    // Honest trade against what it replaces: the old target was a 220x20
    // strip and this is a 96x20 button, so the escape hatch is less than
    // half the size. It is also one somebody might actually find.
    static uint32_t calHoldStart = 0;
    const int calW = tft.width(), calH = tft.height();
    bool overBottomMid = tp.valid && tp.x >= calW / 3 && tp.x < (calW * 2) / 3
                         && tp.y >= calH - 34;
    if ((state == AppState::CLEAR || state == AppState::LOG) && overBottomMid) {
        if (calHoldStart == 0) calHoldStart = now;
        else if (now - calHoldStart > 1500) {
            calHoldStart = 0;
            lastTouch = now;
            // Swallow the rest of this gesture, or the finger coming off the
            // LOG button opens the log the moment calibration finishes. Same
            // whole-gesture swallow the dim-wake tap uses above.
            s_swallowTouch = true;
            runTouchCalibration();
            enterClear();
        }
    } else {
        calHoldStart = 0;
    }

    FrameProf::lap(FrameProf::PRE);
    switch (state) {
        case AppState::BOOT: {
#if defined(CYD35)
            if (frameBufferOk) {
                // Same two-pass half-height `frame` trick CLEAR uses --
                // reuses that same already-allocated sprite (see its
                // setup() comment) rather than needing a second
                // allocation, since BOOT/CLEAR/ALERT are never on
                // screen at the same time. uiBootTick() has no internal
                // per-call state to double-advance, so no advance flag
                // needed here unlike uiClearTick().
                int halfH = tft.height() / 2;
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiBootTick(frame, now);
                pushFrame(0, 0);
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiBootTick(frame, now);
                pushFrame(0, halfH);
                frame.resetViewport();
            } else {
                uiBootTick(tft, now);
            }
#else
            uiBootTick(*canvas, now);
            if (crashCardWanted()) drawCrashCard(*canvas);
#endif
            bool leave = uiBootDone(bootStart, crashCardWanted() ? 9000 : 3000);
            if (leave) {
                // First-ever boot only -- goes straight into the normal
                // onboarding overlay once this screen's own DONE button
                // reaches enterClear(), same as it always did before
                // this existed.
                if (!Settings::colorChecked())    enterColorCheck(false);
                // Something newer is out: the window says so before the
                // board gets on with its day. Not over the first-boot
                // walkthrough, which has a screen of its own to finish.
                else if (OtaCore::availableVersion()[0] && !Security::locked()) enterSysProps();
                else if (Settings::deskWanted())  enterDesk();   // switched off on the desk: back to it
                else                              enterClear();
            }
            break;
        }
        case AppState::BINGO: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiBingoTick(t, now, engine, advance); });
            if (touchJustDown) {
                lastTouch = now;
                // OK leaves for the main screen or the desk, not back into
                // the settings menu: somebody who opened the card to look at
                // it wants the board back, not another list.
                if (uiBingoHitTest(*canvas, tp.x, tp.y, tft.width(), tft.height()) == BingoTap::BACK)
                    goHome();
            }
            break;
        }
        case AppState::DEX: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiDexTick(t, now, engine, advance); });
            if (touchJustDown) {
                lastTouch = now;
                if (uiDexHitTest(*canvas, tp.x, tp.y, tft.width(), tft.height()) == DexTap::BACK)
                    goHome();
            }
            break;
        }
        case AppState::SYS_PROPS: {
            // A detection still takes the screen. The window can open on a
            // board nobody is watching -- a squad member's hello brings the
            // news -- and it has no timeout, so without this it held every
            // alert back until somebody happened to tap it.
            {
                const Detection* latest = engine.latest();
                if (latest && (now - latest->firstSeen) < 200 &&
                    latest->conf >= Settings::minConfidence() && !IgnoreList::silenced(latest->mac) &&
                    alertMayInterrupt(*latest)) {
                    uiAlertSetRedacted(false);
                    enterAlert(*latest);
                    s_backToDesk = Settings::deskWanted();   // dismissed, back where the window was over
                    break;
                }
                if (engine.watchHitPending()) { enterWatchAlert(); break; }
            }
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiSysPropsTick(t, now, engine, advance); });
            if (touchJustDown) {
                switch (uiSysPropsTouch(*canvas, tp.x, tp.y)) {
                    case SysPropsHit::UPDATE_NOW:
                        // The same start the UPDATE screen's own WiFi button
                        // makes: the radio changes hands and the update screen
                        // carries it from there.
                        enterUpdate();
                        engine.startUpdateRadio();
                        if (!OtaWifi::begin()) {
                            engine.stopUpdateRadio();
                            Theme::showToast("CAN'T START UPDATE", updateRefusedWhy(), Theme::AMBER);
                        }
                        break;
                    case SysPropsHit::CLOSE: goHome(); break;
                    case SysPropsHit::NONE:  break;
                }
                lastTouch = now;
            }
            break;
        }
        case AppState::CLEAR: {
            // A newer release heard of AFTER the intro: the window, now. The
            // intro only opens it for news that is already known when the
            // intro ends, and on a board whose boot check could not reach the
            // site the news comes later, in a squad member's hello -- which on
            // the bench landed a few seconds either side of that moment, so
            // the window came up on one boot and not the next. Once per
            // version: takeAvailableNotice() answers once, and a newer version
            // arms it again. Not over a visit's banter any more either: being
            // painted over by it is why this is a window at all.
            if (now - transitionStart > 1500 && OtaCore::takeAvailableNotice()) {
                enterSysProps();
                break;
            }
            if (now - transitionStart > 7000 && !Squachy::visiting()) {
                // Once a day, a hello with the date in it; on the days that
                // count, how long it has been.
                const char* dl = Squachy::takeDayLine();
                if (dl) Squachy::announce(dl);
            }
            // And the first-of-its-kind line, straight after the card.
            if (s_firstLine[0] && now - transitionStart > 1200) {
                Squachy::announce(s_firstLine);
                s_firstLine[0] = '\0';
            }
            // Checked before drawing so the celebration takes over on the
            // same frame it becomes due, rather than after one frame of
            // CLEAR flashing up behind it.
            if (maybeEnterOutfitUnlock()) break;
#if defined(CYD35)
            if (frameBufferOk) {
                // Two passes through the half-height `frame` sprite
                // instead of one direct-to-tft pass -- see the setup()
                // comment by its creation. advance=true only on the
                // first pass so Squachy/digital-rain state advances once
                // per logical frame even though this draws twice.
                int halfH = tft.height() / 2;
                uint32_t tBand = micros();
                // The rows each pass can actually paint. Drawing that lands
                // entirely outside them is declined a block at a time rather
                // than clipped a pixel at a time -- see draw_band.h.
                DrawBand::set(0, halfH);
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiClearTick(frame, now, engine, true, s_scanPickerOpen);
                s_bandUs[0] = micros() - tBand;
                pushFrame(0, 0);
                tBand = micros();
                DrawBand::set(halfH, tft.height());
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiClearTick(frame, now, engine, false, s_scanPickerOpen);
                s_bandUs[1] = micros() - tBand;
                pushFrame(0, halfH);
                DrawBand::all();
                frame.resetViewport();
            } else {
                // Fallback if a post-boot rotate ever failed to
                // reallocate `frame` (see loop()) -- same direct-to-tft
                // path this board already uses for every other screen.
                uiClearTick(tft, now, engine, true, s_scanPickerOpen);
            }
#else
            uiClearTick(*canvas, now, engine, true, s_scanPickerOpen);
#endif
            FrameProf::lap(FrameProf::CHROME);
            // Toasts on the main screen too. They were only drawn on LOG and
            // NEARBY, so SNOOZED and READ, both raised on the way here or while
            // here, went unseen.
            Theme::drawToast(*canvas, now);
            // The clock is set and no zone was ever picked: the card, over
            // everything, until THIS IS RIGHT. A tap on it is the card's; a
            // tap beside it is the main screen's, so he can still be poked.
            if (uiZoneCardWanted()) {
                uiZoneCardDraw(*canvas, now);
                if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                    const ZoneHit zh = uiZoneCardHit(tp.x, tp.y, tft.width(), tft.height());
                    if (zh != ZoneHit::NONE) {
                        lastTouch = now;
                        if      (zh == ZoneHit::PREV) Settings::stepTimeZone(-1);
                        else if (zh == ZoneHit::NEXT) Settings::stepTimeZone(1);
                        else if (zh == ZoneHit::OK)   Settings::markTimeZoneChosen();
                        break;
                    }
                }
            }
#if CROWD_BENCH
            // The crowd benchmark (a test build): its numbers or its table go
            // over everything, a tap moves it on, and nothing else on this
            // screen -- alerts included -- interrupts it while it runs.
            if (CrowdBench::active()) {
                CrowdBench::drawOver(*canvas, now);
                if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                    lastTouch = now;
                    CrowdBench::tap();
                }
                break;
            }
#endif
            // Check for new detection — gated by the settings-menu
            // confidence filter (LOW_CONF/default = no filtering, every
            // match still interrupts with the ALERT screen).
            // A watched target coming back takes priority over a
            // routine signature-match alert -- it's not filtered by
            // the confidence setting either, since it isn't a
            // signature guess at all, it's the exact thing the user
            // explicitly asked to be told about.
            //
            // Neither one interrupts the first-boot walkthrough: a real
            // detection popping the full-screen ALERT (or watch-alert)
            // mid-explanation would cut Squachy off before he's done
            // introducing everything. The detection itself still gets
            // logged/counted as normal either way (that already
            // happened before this check runs) -- this only decides
            // whether it pops up over the tutorial. watchHitPending()
            // is deliberately NOT called in that branch since it's
            // consumed on read; leaving it untouched means it stays
            // pending and still fires for real once onboarding ends.
            if (Squachy::onboardingActive()
#if SQUACH_MESH
                || MeshTutor::active()     // same courtesy for the messages tutorial
#endif
               ) {
                // deliberately no-op
            } else if (engine.watchHitPending()) {
                enterWatchAlert();
            } else {
                const Detection* latest = engine.latest();
                if (latest && (now - latest->firstSeen) < 200 &&
                    // This sighting's grade, not the type's -- which is what
                    // finally makes ALERT FILTER mean something. Set it to
                    // High and an ESP32 probe request matching a
                    // module-vendor OUI stays in the log without taking over
                    // the screen.
                    latest->conf >= Settings::minConfidence() &&
                    // Your own AirTag and your own doorbell are true
                    // positives every single time, and a detector that
                    // shouts about them constantly is one you stop reading.
                    // Only the ALERT is suppressed -- the detection is
                    // still counted and still written to the LOG above, so
                    // the device stays visible and un-ignorable.
                    !IgnoreList::silenced(latest->mac) &&
                    alertMayInterrupt(*latest)) {
                    uiAlertSetRedacted(false);
                    enterAlert(*latest);
                }
            }
            // First-boot walkthrough: tapping its bubble advances (or
            // ends) it. Checked before everything else below so it eats
            // the tap on a hit — but it only ever hits its own bubble,
            // never Squachy himself or the button bar, so pet-tap,
            // background-cycling and the buttons all keep working
            // normally throughout the walkthrough, same as any other
            // time on this screen. Both this and the pet-tap check are
            // skipped outright in "boring mode": uiClearTick() never
            // draws him there, so hitTest() would otherwise still be
            // checking against wherever he last stood before the mode
            // was turned on — a tap on empty background shouldn't pet
            // a mascot that isn't there.
            // Summoning the werewolf earns the WOLF PELT costume. Read
            // here rather than inside theme.cpp so the background
            // renderer stays unaware of the outfit system.
            if (Theme::consumeWerewolfSummon()) Squachy::unlockWolfPelt();
            if (Theme::consumeToasterCatch())   Squachy::unlockChromeWing();
            if (Theme::consumeEyeCatch())       Squachy::unlockVoidEye();
            if (Theme::consumeLodgeKnock())     Squachy::unlockParka();
            if (Theme::consumeSharkCatch())     Squachy::unlockShark();
    if (Theme::consumePetUnlock())      Squachy::unlockPet();

            bool boring = Settings::boringMode();
            ButtonId barBtn = tp.valid ? Theme::hitTestButtonBar(tp.x, tp.y, tft.width(), tft.height()) : ButtonId::NONE;

            // Left/right 10% slivers of the screen (excluding the button
            // bar row itself, so its own leftmost/rightmost buttons still
            // win there) cycle the background one step. Edge-triggered on
            // touchJustDown rather than the TOUCH_DEBOUNCE_MS cooldown
            // below, so a held finger fires exactly once no matter how
            // long it stays down. This frees up tapping Squachy himself
            // for the gesture classifier instead of also nudging the
            // background, so he can be poked without changing the scene.
            const int edgeZoneW = tft.width() / 10;
            bool inEdgeZone = tp.valid && barBtn == ButtonId::NONE &&
                               (tp.x < edgeZoneW || tp.x >= tft.width() - edgeZoneW);

            // Squachy gesture state, tracked across frames from the
            // moment a touch lands on him until it releases. A quick tap
            // (released before SQ_HOLD_MS with negligible movement) reads
            // as PETTED -- the only one of the three that counts toward
            // the persisted pet-count/milestones. A stationary press held
            // past SQ_HOLD_MS reads as HELD. Movement past SQ_MOVE_PX
            // reads as PETTING (a drag/stroke) and keeps firing, throttled
            // internally by squachy.cpp, for as long as the stroke
            // continues.
            static bool     sqActive = false;
            static bool     sqHeld = false;
            static bool     sqPetting = false;
            static uint32_t sqStartMs = 0;
            static int      sqStartX = 0, sqStartY = 0;
            static int32_t  sqLastDx = 0;   // the stroke's reach so far, for the flick
            constexpr uint32_t SQ_HOLD_MS = 600;
            constexpr int32_t  SQ_MOVE_PX = 12;
            constexpr int32_t  SQ_MOVE_PX_SQ = SQ_MOVE_PX * SQ_MOVE_PX;

            // Hidden outfit-unlock gesture: hold the third button -- DESK
            // now, CLR when this was written -- (not tap it) for
            // CLR_UNLOCK_HOLD_MS. Same tracked-across-frames shape as
            // Squachy's own HELD/PETTED just below -- a touch that
            // starts on CLR is armed for that touch's whole lifetime,
            // so it can't also fire the normal "clear log" tap action
            // once the hold succeeds; released early, it still clears
            // the log exactly like a normal tap always has (see
            // touchJustUp below).
            static bool     clrHoldActive = false;
            static bool     clrHoldFired  = false;
            static uint32_t clrHoldStart  = 0;
            constexpr uint32_t CLR_UNLOCK_HOLD_MS = 4000;

            // Long-press NEARBY: open the closest live device. The headline
            // sits on top of Squachy, so the same touch is also tracked as a
            // possible pet -- a quick tap still pets him, a stroke still
            // strokes him, and only a still press held past the threshold
            // becomes this instead.
            static bool     nbActive = false;
            static uint32_t nbStart  = 0;
            constexpr uint32_t NEARBY_HOLD_MS = 600;

            // Decide up front whether a brand-new touch lands on Squachy
            // -- this only updates gesture-tracking state, it doesn't by
            // itself claim the touch, so a miss still falls through to
            // the button-bar branch below instead of being swallowed.
            // (A touch that DOES land on him, or continues an already-
            // active gesture, still takes priority over the button bar
            // in the branch below -- Squachy is drawn well clear of the
            // button row, so the two never really compete in practice.)
            if (touchJustDown) {
                sqActive = !boring && Squachy::hitTest(tp.x, tp.y);
                sqHeld = false;
                sqPetting = false;
                sqStartMs = now;
                sqStartX = tp.x;
                sqStartY = tp.y;
                sqLastDx = 0;
                clrHoldActive = !s_scanPickerOpen && barBtn == ButtonId::CLR;
                clrHoldFired  = false;
                clrHoldStart  = now;
                nbActive = !s_scanPickerOpen && uiClearNearbyHit(tp.x, tp.y);
                nbStart  = now;
            }

            // Any tap at all ends the parade, and is consumed doing it --
            // checked ahead of everything else so a tap cannot both stop
            // the show and cycle a background or pet him on the way out.
            if (Squachy::showOffActive() && tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                Squachy::stopShowOff();
                lastTouch = now;
                sqActive  = false;
#if SQUACH_MESH
            } else if (MeshTutor::active() && tp.valid) {
                // The messages tutorial owns the screen while it runs: every
                // touch goes to it, so nobody pets Squachy, cycles the scene
                // or clears the log halfway through a sentence. The whole
                // gesture is claimed, and the CLR long-press disarmed with it.
                sqActive      = false;
                clrHoldActive = false;
                if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                    lastTouch = now;
                    if (MeshTutor::cardTap(tp.x, tp.y) == MeshTutor::Tap::SKIP) {
                        MeshTutor::stop();
                    } else if (MeshTutor::step() == MeshTutor::Step::TAP_ICON) {
                        // Only the bubble moves this step on: it is the one
                        // thing the step is teaching.
                        if (uiClearBubbleHit(tp.x, tp.y)) { MeshTutor::next(); enterMeshCompose(); }
                    } else if (MeshTutor::waitsForTap()) {
                        MeshTutor::next();
                    }
                }
#endif
            } else if (!boring && Squachy::onboardingActive() && tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                Squachy::onboardingTapAdvance(tp.x, tp.y)) {
                lastTouch = now;
#if SQUACH_MESH
            } else if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                       uiClearBubbleHit(tp.x, tp.y)) {
                // The message bubble. Ahead of the background and the edge
                // zones: it can sit over the right-hand one, and a tap on it
                // must never cycle the scene on the way to the message.
                lastTouch = now;
                sqActive  = false;
                enterMeshCompose();
            } else if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                       uiClearWatchPillHit(tp.x, tp.y)) {
                // The watch/hunt pill. Opens the alert screen, which names the
                // target and carries REMOVE FROM WATCH LIST -- the same screen
                // a real sighting would have opened, just asked for rather
                // than waited for.
                lastTouch = now;
                sqActive  = false;
                enterWatchAlert();
            } else if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                       uiClearSquadHit(tp.x, tp.y)) {
                // The squad badge, ahead of the scene gestures for the same
                // reason as the bubble above.
                lastTouch = now;
                sqActive  = false;
                enterSquad();
            } else if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                       uiClearCrowdTap(tp.x, tp.y, now)) {
                // Asking one of the crowd who he is. Below the bubble and the
                // badge, which sit over the crowd and mean something more
                // specific; above the edge zones, so asking a stranger his
                // name cannot also change the background out from under him.
                lastTouch = now;
                sqActive  = false;
#endif
            } else if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                       Theme::backgroundTap(tp.x, tp.y, now)) {
                // Something tappable in the background itself claimed
                // this touch -- currently only the moon on the FIRE
                // scene. Checked BEFORE the edge zone below on purpose:
                // the moon sits high on the right, and while it has been
                // moved clear of the background-cycling sliver, ordering
                // it first means the egg cannot be broken by a later
                // change to either one.
                lastTouch = now;
            } else if (touchJustDown && inEdgeZone && !Settings::backgroundLocked() &&
                       (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                // The debounce check above matters more here than it
                // looks -- the title-bar SETTINGS icon lives in this
                // same left-edge zone, and tapping it to leave Settings
                // calls enterClear() earlier in this same loop()
                // iteration, before this switch runs. Without this
                // guard, the exact touch that just closed Settings fell
                // straight through into CLEAR's edge-zone gesture on
                // the very same frame and cycled the background right
                // as the screen appeared.
                lastTouch = now;
                if (tp.x < edgeZoneW) Settings::cyclePrevBackground();
                else                  Settings::cycleBackground();
            } else if (tp.valid && nbActive) {
                const int32_t dx = tp.x - sqStartX, dy = tp.y - sqStartY;
                // A much wider allowance than Squachy's pet stroke: a thumb
                // held still on a resistive panel wanders several pixels a
                // frame, and at the pet threshold every hold on a real board
                // was cancelled as a stroke and handed to him instead.
                constexpr int32_t NB_MOVE_PX_SQ = 24 * 24;
                if ((dx * dx + dy * dy) > NB_MOVE_PX_SQ) {
                    // A real stroke, not a press: Squachy gets it from here on.
                    nbActive = false;
                } else if ((now - nbStart) >= NEARBY_HOLD_MS) {
                    nbActive = false;
                    sqActive = false;
                    lastTouch = now;
                    // Closest means strongest signal, among devices that are
                    // live right now. RSSI is a guess at distance, not a
                    // measurement, which is why the card still shows the dBm.
                    const Detection* best = nullptr;
                    for (uint8_t i = 0; i < engine.logCount(); i++) {
                        const Detection* d = engine.logAt(i);
                        if (d && d->active && (!best || d->rssi > best->rssi)) best = d;
                    }
                    if (best) {
                        uiAlertSetRedacted(false);
                        enterAlert(*best);
                    } else {
                        Theme::showToast("NOTHING NEARBY", "It just left", Theme::CYAN);
                    }
                }
            } else if (!boring && tp.valid && sqActive) {
                int32_t dx = tp.x - sqStartX;
                int32_t dy = tp.y - sqStartY;
                // Moving BEFORE the hold threshold is a stroke; moving
                // after it is a carry. Latching sqPetting only while
                // !sqHeld is what keeps the two gestures apart -- without
                // that guard the first drag after a successful hold fell
                // straight back into petting, and he could never be
                // picked up at all.
                if (!sqHeld && (dx * dx + dy * dy) > SQ_MOVE_PX_SQ) sqPetting = true;
                sqLastDx = dx;
                if (sqHeld) {
                    Squachy::grabTo(tp.x, tp.y);
                } else if (sqPetting) {
                    Squachy::trigger(Squachy::Event::PETTING);
                } else if ((now - sqStartMs) >= SQ_HOLD_MS) {
                    sqHeld = true;
                    Squachy::trigger(Squachy::Event::HELD);
                }
            } else if (tp.valid && clrHoldActive) {
                if (!clrHoldFired && (now - clrHoldStart) >= CLR_UNLOCK_HOLD_MS) {
                    clrHoldFired = true;
                    Squachy::unlockAllOutfits();
                }
            } else if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                if (s_scanPickerOpen) {
                    // The bar's slots are relabeled [BLE][WIFI][BACK]
                    // right now (see the scanMenu arg on uiClearTick()
                    // above) -- same ButtonId::SCAN/LOG/CLR positions,
                    // different meaning while this is open.
                    if (barBtn == ButtonId::SCAN)      { s_scanPickerOpen = false; enterRawScan(true); }
                    else if (barBtn == ButtonId::LOG)  { s_scanPickerOpen = false; enterRawScan(false); }
                    else if (barBtn == ButtonId::CLR)  { s_scanPickerOpen = false; }
                } else if (barBtn == ButtonId::LOG)  { Squachy::trigger(Squachy::Event::LOG_OPENED); enterLog(); }
                else if (barBtn == ButtonId::SCAN) { s_scanPickerOpen = true; }
                else if (boring && tp.y >= 20 && !Settings::backgroundLocked()) {
                    // No Squachy to tap for this in boring mode — any tap
                    // on the main content area (below the title bar, not
                    // a real button, and not already claimed by an edge
                    // zone above) cycles the background instead, so it's
                    // still reachable without him.
                    Settings::cycleBackground();
                }
            }

            // Finalize the Squachy gesture on the exact frame the touch
            // releases, wherever the finger happens to end up (it can
            // slide off him mid-stroke and still release cleanly).
            if (touchJustUp && sqActive) {
                if (sqHeld) {
                    Squachy::release();     // drop him wherever he ended up
                } else if (!sqPetting) {
                    Squachy::noteTapAt(sqStartX, sqStartY);
                    Squachy::trigger(Squachy::Event::PETTED);
                } else if ((now - sqStartMs) < 320 && (sqLastDx > 48 || sqLastDx < -48)) {
                    // A stroke that covered fifty pixels in under a third of a
                    // second is a flick, not a pet.
                    Squachy::flick(sqLastDx > 0 ? 1 : -1);
                }
                sqActive = false;
            }
            // Released before the hold threshold -- a normal tap on DESK.
            // Clearing the log moved to the LOG screen's own CLR, where the
            // thing being cleared is in front of you.
            if (touchJustUp && clrHoldActive) {
                clrHoldActive = false;
                // Only a press that started on THIS screen. One that began
                // before an alert or the update window took over, and lifts
                // after it handed back, is not a tap on DESK.
                if (!clrHoldFired && clrHoldStart >= transitionStart) enterDesk();
            }
            break;
        }
        case AppState::ALERT: {
            const char* alertInfoText = s_infoShowingPrimer ? DetectionInfo::rssiConfidencePrimer()
                                                              : DetectionInfo::explainFor(s_confirmType, s_confirmVendor, s_confirmName, engine);
            // No heading during the primer page -- it's about RSSI/
            // confidence in general, not any one detection type.
            const char* alertInfoTypeName = s_infoShowingPrimer ? nullptr
                                          : DetectionInfo::titleFor(s_confirmType, s_confirmVendor, s_confirmName);
#if defined(CYD35)
            if (frameBufferOk) {
                // Same two-pass half-height `frame` trick CLEAR/BOOT
                // use, reusing that same already-allocated sprite.
                // uiAlertTick() (and everything it calls) is a pure
                // function of `now`/the alert's own fixed detection
                // data, so calling it twice with the same `now` is safe.
                int halfH = tft.height() / 2;
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiAlertTick(frame, now, engine, s_infoPending, alertInfoTypeName, alertInfoText);
                pushFrame(0, 0);
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiAlertTick(frame, now, engine, s_infoPending, alertInfoTypeName, alertInfoText);
                pushFrame(0, halfH);
                frame.resetViewport();
            } else {
                uiAlertTick(tft, now, engine, s_infoPending, alertInfoTypeName, alertInfoText);
            }
#else
            uiAlertTick(*canvas, now, engine, s_infoPending, alertInfoTypeName, alertInfoText);
#endif
            // The finger that opened the card has to lift before the card
            // listens: see s_alertArmed. The auto-dismiss timer below still
            // runs, so a hand left resting on the screen can't pin it open.
            if (!s_alertArmed) {
                if (!tp.valid) s_alertArmed = true;
                else           tp.valid = false;
            }

            // Same "ignore the touch that opened this until it releases"
            // gate LOG's info panel uses, applied here too -- MORE INFO
            // appears on the exact tap that opened it, so without this
            // the still-held finger could instantly dismiss it.
            if (s_infoPending) {
                if (!s_infoArmed) {
                    if (!tp.valid) s_infoArmed = true;
                } else if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                           Theme::infoPanelHitDismiss(tp.x, tp.y, tft.width(), tft.height())) {
                    lastTouch = now;
                    if (s_infoShowingPrimer) {
                        // First page done -- move straight to this
                        // alert's own explanation rather than closing,
                        // and only mark the primer seen once its page
                        // has actually been read past.
                        Settings::markInfoPrimerShown();
                        s_infoShowingPrimer = false;
                        s_infoArmed = false;
                    } else {
                        // GOT IT on the actual explanation (not the
                        // primer) closes the whole alert, straight back
                        // to CLEAR -- same as tapping anywhere else on
                        // the alert screen. Previously this just closed
                        // the info panel back to the plain ALERT screen,
                        // which read as an extra, redundant tap-to-
                        // dismiss step once you'd already read the
                        // explanation.
                        s_infoPending = false;
                        squachyCatch(lastAlertType, s_alertMac, lastAlertHits, lastAlertRssi, lastAlertConf);
                        enterClear();
                    }
                }
                break;
            }

            // Locked: an alert is for looking at, not acting on. MORE INFO,
            // IGNORE and HUNT would all change the device without the PIN, so
            // any tap -- or the timeout -- goes back to the lock screen.
            if (Security::locked()) {
                if ((tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) ||
                    (now - alertStart) > ALERT_AUTO_DISMISS_MS) {
                    lastTouch = now;
                    enterClear();       // the lock screen, while locked
                }
                break;
            }
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                if (uiAlertHitMoreInfo(tp.x, tp.y, tft.width(), tft.height())) {
                    s_confirmType        = lastAlertType;
                    memcpy(s_confirmVendor, s_alertVendor, sizeof s_confirmVendor);
                    memcpy(s_confirmName,   s_alertName,   sizeof s_confirmName);
                    s_infoShowingPrimer  = !Settings::infoPrimerShown();
                    s_infoPending        = true;
                    s_infoArmed          = false;
                } else if (uiAlertHitIgnore(tp.x, tp.y, tft.width(), tft.height())) {
                    // Toggle, not just add: the button reads MUTED once the
                    // device is on the list, so tapping it again has to be
                    // the way back off. Fires the DETECTION trigger for the
                    // same reason HUNT does below -- leaving by this route
                    // is still the user acknowledging the alert.
                    lastTouch = now;
                    if (IgnoreList::contains(s_alertMac)) IgnoreList::remove(s_alertMac);
                    else                                  IgnoreList::add(s_alertMac, lastAlertType);
                    squachyCatch(lastAlertType, s_alertMac, lastAlertHits, lastAlertRssi, lastAlertConf);
                    enterClear();
                } else if (uiAlertHitSnooze(tp.x, tp.y, tft.width(), tft.height())) {
                    // This device, until the board restarts: no more alerts
                    // from it, while it goes on being scanned, counted and
                    // logged like anything IGNOREd. Acknowledges the alert
                    // like IGNORE does. See IgnoreList::snooze for why it
                    // is no longer the whole type for an hour.
                    lastTouch = now;
                    IgnoreList::snooze(s_alertMac);
                    Theme::showToast("SNOOZED", "This one, until restart", Theme::AMBER);
                    squachyCatch(lastAlertType, s_alertMac, lastAlertHits, lastAlertRssi, lastAlertConf);
                    enterClear();
                } else if (uiAlertHitHunt(tp.x, tp.y, tft.width(), tft.height())) {
                    // Same call pair LOG's confirm panel makes. The
                    // DETECTION trigger fires here too: leaving via HUNT
                    // is still the user acknowledging this alert, and
                    // without it a detection chased straight from the
                    // alert would never register with Squachy at all --
                    // the only other exits (tap-to-dismiss, and GOT IT
                    // on the info panel) both fire it.
                    if (s_alertIsBle) engine.huntBle(s_alertMac, s_alertLabel);
                    else              engine.huntWifi(s_alertMac, s_alertLabel);
                    squachyCatch(lastAlertType, s_alertMac, lastAlertHits, lastAlertRssi, lastAlertConf);
                    enterHunt();
                } else {
                    squachyCatch(lastAlertType, s_alertMac, lastAlertHits, lastAlertRssi, lastAlertConf);
                    enterClear();
                }
            } else if ((now - alertStart) > ALERT_AUTO_DISMISS_MS) {
                squachyCatch(lastAlertType, s_alertMac, lastAlertHits, lastAlertRssi, lastAlertConf);
                enterClear();
            }
            break;
        }
        case AppState::OUTFIT_UNLOCK: {
#if defined(CYD35)
            if (frameBufferOk) {
                // Same two-pass half-height `frame` trick the other
                // full-screen states use. This one draws Squachy, so
                // `advance` would matter -- except uiOutfitUnlockTick()
                // drives him through drawWaving(), which is a pure
                // function of `now` with no state to double-advance.
                int halfH = tft.height() / 2;
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiOutfitUnlockTick(frame, now, engine);
                pushFrame(0, 0);
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiOutfitUnlockTick(frame, now, engine);
                pushFrame(0, halfH);
                frame.resetViewport();
            } else {
                uiOutfitUnlockTick(tft, now, engine);
            }
#else
            uiOutfitUnlockTick(*canvas, now, engine);
#endif
            const bool timedOut = (now - outfitUnlockStart) > OUTFIT_UNLOCK_AUTO_MS;
            const bool tapped   = tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                                  uiOutfitUnlockDismissable(now);
            if (tapped || timedOut) {
                if (tapped) lastTouch = now;
                // Straight into the next one if two were earned at once,
                // rather than bouncing through CLEAR between them.
                if (!maybeEnterOutfitUnlock()) enterClear();
            }
            break;
        }
        case AppState::WATCH_ALERT: {
#if defined(CYD35)
            if (frameBufferOk) {
                // Same two-pass half-height `frame` trick CLEAR/BOOT/
                // ALERT use -- unlike plain uiAlertTick(), this one
                // does draw Squachy, so advance has to gate his state
                // mutation to exactly one of the two passes, same as
                // uiClearTick()/uiBootTick().
                int halfH = tft.height() / 2;
                frame.setViewport(0, 0, tft.width(), tft.height(), true);
                uiWatchAlertTick(frame, now, engine, true);
                pushFrame(0, 0);
                frame.setViewport(0, -halfH, tft.width(), tft.height(), true);
                uiWatchAlertTick(frame, now, engine, false);
                pushFrame(0, halfH);
                frame.resetViewport();
            } else {
                uiWatchAlertTick(tft, now, engine, true);
            }
#else
            uiWatchAlertTick(*canvas, now, engine, true);
#endif
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                // The button ends the watch; anywhere else just dismisses the
                // alert and leaves it running. Both land back on CLEAR.
                if (uiWatchAlertHitRemove(*canvas, tp.x, tp.y)) {
                    engine.clearWatch();
                    Theme::showToast("UNWATCHED", nullptr, Theme::CYAN);
                }
                enterClear();
            } else if ((now - watchAlertStart) > ALERT_AUTO_DISMISS_MS) {
                enterClear();
            }
            break;
        }
        case AppState::LOG: {
            const char* infoText = s_infoShowingPrimer
                                  ? DetectionInfo::rssiConfidencePrimer()
                                  : DetectionInfo::explainFor(s_confirmType, s_confirmVendor, s_confirmName, engine);

            // No heading during the primer page -- it's about RSSI/
            // confidence in general, not any one detection type.
            const char* infoTypeName = s_infoShowingPrimer ? nullptr
                                     : DetectionInfo::titleFor(s_confirmType, s_confirmVendor, s_confirmName);
            // Nothing on LOG moves by the call -- the note about
            // drawActiveBackground in ui_log.cpp is a comment, not a call.
            drawTwoBand([&](TFT_eSPI& t, bool) {
                uiLogTick(t, now, engine, 0, s_confirmPending, s_confirmLabel,
                          s_infoPending, infoTypeName, infoText,
                          engine.isWatched(s_confirmMac, s_confirmIsBle),
                          engine.isHunted(s_confirmMac, s_confirmIsBle));
                Theme::drawToast(t, now);
            });

            // Same "ignore the touch that opened this until it releases"
            // gate s_confirmArmed uses on the confirm panel, applied to
            // the info panel's own GOT IT button -- it appears mid-hold
            // on the exact same MORE INFO tap that opened it, so without
            // this the still-held finger could instantly dismiss it.
            if (s_infoPending) {
                if (!s_infoArmed) {
                    if (!tp.valid) s_infoArmed = true;
                } else if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                           Theme::infoPanelHitDismiss(tp.x, tp.y, tft.width(), tft.height())) {
                    lastTouch = now;
                    if (s_infoShowingPrimer) {
                        // First page done -- move straight to this
                        // target's own explanation rather than closing,
                        // and only mark the primer seen once its page
                        // has actually been read past.
                        Settings::markInfoPrimerShown();
                        s_infoShowingPrimer = false;
                        s_infoArmed = false;
                    } else {
                        s_infoPending = false;
                    }
                }
                break;
            }

            // The confirm panel is modal: while it's up, a tap only
            // ever means WATCH, HUNT, INFO, or CANCEL on it, nothing
            // else on this screen (the button bar, another long-press,
            // scrolling) is reachable underneath it -- same pattern
            // RAWSCAN's identical (minus INFO) panel uses.
            if (s_confirmPending) {
                if (!s_confirmArmed) {
                    // Still the same touch that opened the panel --
                    // ignore it until it's released (see s_confirmArmed's
                    // comment) so it can't register as an instant tap.
                    if (!tp.valid) s_confirmArmed = true;
                } else if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                    LogConfirmTap ctap = uiLogHitConfirm(tp.x, tp.y, tft.width(), tft.height());
                    if (ctap == LogConfirmTap::WATCH) {
                        lastTouch = now;
                        s_confirmPending = false;
                        // Toggling like IGNORE beside it -- the only way to
                        // end a watch that isn't a reboot or the wipe.
                        if (engine.isWatched(s_confirmMac, s_confirmIsBle)) {
                            engine.clearWatch();
                            Theme::showToast("UNWATCHED", nullptr, Theme::CYAN);
                        } else if (s_confirmIsBle) {
                            engine.watchBle(s_confirmMac, s_confirmLabel);
                        } else {
                            engine.watchWifi(s_confirmMac, s_confirmLabel);
                        }
                    } else if (ctap == LogConfirmTap::IGNORE) {
                        lastTouch = now;
                        s_confirmPending = false;
                        // Toggling, so the toast has to say which way it went --
                        // "IGNORED" after un-ignoring would be worse than no
                        // feedback at all.
                        const bool wasOn = IgnoreList::contains(s_confirmMac);
                        if (wasOn) IgnoreList::remove(s_confirmMac);
                        else       IgnoreList::add(s_confirmMac, s_confirmType);
                        Theme::showToast(wasOn ? "UN-IGNORED" : "IGNORED",
                                         detectionTypeName(s_confirmType),
                                         Theme::colorFor(s_confirmType));
                    } else if (ctap == LogConfirmTap::HUNT) {
                        lastTouch = now;
                        s_confirmPending = false;
                        // Toggles, same as WATCH above it. Stopping a hunt
                        // stays here: HUNT MODE is somewhere to GO, and there
                        // is nowhere to go once the target is gone.
                        if (engine.isHunted(s_confirmMac, s_confirmIsBle)) {
                            engine.clearHunt();
                            Theme::showToast("HUNT STOPPED", nullptr, Theme::CYAN);
                        } else {
                            if (s_confirmIsBle) engine.huntBle(s_confirmMac, s_confirmLabel);
                            else                engine.huntWifi(s_confirmMac, s_confirmLabel);
                            enterHunt();
                        }
                    } else if (ctap == LogConfirmTap::INFO) {
                        lastTouch = now;
                        s_confirmPending = false;
                        s_infoShowingPrimer = !Settings::infoPrimerShown();
                        s_infoPending = true;
                        s_infoArmed   = false;
                    } else if (ctap == LogConfirmTap::CANCEL) {
                        lastTouch = now;
                        s_confirmPending = false;
                    }
                }
                break;
            }

            // Tap commits on release, not on press, and only if the
            // touch never moved past the scroll threshold -- firing on
            // press meant a swipe that started on a button/row acted on
            // it instantly, before the drag had any chance to be
            // recognized as a scroll instead. Same tracked-across-
            // frames shape as Squachy's own PETTED/HELD gesture and the
            // CLR-hold costume unlock.
            static bool gestureActive = false;
            static bool gestureMoved  = false;
            static int  gestureStartX = 0, gestureStartY = 0;
            static int  lastY = -1;
            static uint32_t gestureDownMs = 0;
            if (touchJustDown) {
                gestureActive = true;
                gestureMoved  = false;
                gestureStartX = tp.x;
                gestureStartY = tp.y;
                lastY = tp.y;
                gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) {
                    gestureMoved = true;
                    uiLogScroll(dy > 0 ? -1 : 1);
                    lastY = tp.y;
                }
            }
            if (touchJustUp && gestureActive) {
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS) {
                    lastTouch = now;
                    ButtonId b = Theme::hitTestButtonBar(gestureStartX, gestureStartY, tft.width(), tft.height());
                    if (b == ButtonId::SCAN) { enterClear(); }
                    if (b == ButtonId::CLR)  {
                        engine.clearLog();
                        BlackBox::markCleared();   // or a restart brings it all back
                        Squachy::trigger(Squachy::Event::LOG_CLEARED);
                        enterClear();
                    }
                    if (b == ButtonId::LOG)  { enterClear(); }   // toggle off
                }
                gestureActive = false;
            }

            // Long-press a log entry to bring up the same WATCH/HUNT/
            // CANCEL panel RAWSCAN's results use -- disambiguated from
            // the drag-to-scroll gesture above the same way RAWSCAN's
            // is, by requiring the touch to stay roughly still past a
            // hold threshold (same pattern CLEAR uses for petting
            // Squachy). BLE vs WiFi is inferred from channel: postBle()
            // always leaves it 0 (see detection.h), every WiFi-sourced
            // entry (including DEAUTH) carries the real 1..13 channel
            // it was captured on.
            static bool     rowHoldFired = false;
            static uint32_t rowHoldStart = 0;
            static int      rowHoldX = 0, rowHoldY = 0;
            constexpr uint32_t ROW_HOLD_MS    = 500;
            constexpr int32_t  ROW_MOVE_PX_SQ = 12 * 12;
            if (touchJustDown) {
                rowHoldFired = false;
                rowHoldStart = now;
                rowHoldX = tp.x;
                rowHoldY = tp.y;
            }
            if (tp.valid && !rowHoldFired) {
                int32_t hdx = tp.x - rowHoldX, hdy = tp.y - rowHoldY;
                if ((hdx * hdx + hdy * hdy) <= ROW_MOVE_PX_SQ && (now - rowHoldStart) >= ROW_HOLD_MS) {
                    int row = uiLogRowAt(*canvas, tp.x, tp.y, tft.width(), tft.height());
                    // Through the LOG screen's own accessor: past the rows
                    // held in RAM it is reading the black box, and a long
                    // press on one of those has to reach the same device.
                    const Detection* d = (row >= 0) ? uiLogRow(engine, row) : nullptr;
                    if (d) {
                        rowHoldFired = true;
                        memcpy(s_confirmMac, d->mac, 6);
                        s_confirmIsBle = (d->channel == 0);
                        s_confirmType  = d->type;
                        snprintf(s_confirmVendor, sizeof s_confirmVendor, "%s", vendorText(*d));
                        memcpy(s_confirmName,   d->name,   sizeof s_confirmName);
                        const char* lbl = d->name[0] ? d->name : vendorText(*d);
                        strncpy(s_confirmLabel, lbl, sizeof(s_confirmLabel) - 1);
                        s_confirmLabel[sizeof(s_confirmLabel) - 1] = 0;
                        s_confirmPending = true;
                        s_confirmArmed   = false;
                    }
                }
            }
            break;
        }
        case AppState::RAWSCAN: {
            bool done = s_rawScanIsBle ? engine.rawBleScanDone() : engine.rawWifiScanDone();
            drawTwoBand([&](TFT_eSPI& t, bool advance) {
                uiRawScanTick(t, now, engine, s_rawScanIsBle, done, s_confirmPending, s_confirmLabel,
                              engine.isWatched(s_confirmMac, s_rawScanIsBle),
                              engine.isHunted(s_confirmMac, s_rawScanIsBle), advance);
                Theme::drawToast(t, now);
            });

            // The confirm panel is modal: while it's up, a tap only
            // ever means WATCH, HUNT, or CANCEL on it, nothing else on
            // this screen (BACK/SWITCH, another long-press, scrolling)
            // is reachable underneath it.
            if (s_confirmPending) {
                if (!s_confirmArmed) {
                    // Still the same touch that opened the panel --
                    // ignore it until it's released (see s_confirmArmed's
                    // comment) so it can't register as an instant tap.
                    if (!tp.valid) s_confirmArmed = true;
                } else if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                    RawScanConfirmTap ctap = uiRawScanHitConfirm(tp.x, tp.y, tft.width(), tft.height());
                    if (ctap == RawScanConfirmTap::WATCH) {
                        lastTouch = now;
                        s_confirmPending = false;
                        // See the LOG screen's copy: same toggle. Unwatching
                        // stays on this screen -- the reason to leave was to
                        // go watch the thing, and there is nothing to go to.
                        if (engine.isWatched(s_confirmMac, s_rawScanIsBle)) {
                            engine.clearWatch();
                            Theme::showToast("UNWATCHED", nullptr, Theme::CYAN);
                        } else {
                            if (s_rawScanIsBle) engine.watchBle(s_confirmMac, s_confirmLabel);
                            else                engine.watchWifi(s_confirmMac, s_confirmLabel);
                            engine.stopRawScan();
                            enterClear();
                        }
                    } else if (ctap == RawScanConfirmTap::IGNORE) {
                        lastTouch = now;
                        s_confirmPending = false;
                        // The raw scanner classifies nothing, so there is no
                        // type to record and none to name in the toast.
                        const bool wasOn = IgnoreList::contains(s_confirmMac);
                        if (wasOn) IgnoreList::remove(s_confirmMac);
                        else       IgnoreList::add(s_confirmMac, DetectionType::UNKNOWN);
                        Theme::showToast(wasOn ? "UN-IGNORED" : "IGNORED",
                                         nullptr, Theme::CYAN);
                    } else if (ctap == RawScanConfirmTap::HUNT) {
                        lastTouch = now;
                        s_confirmPending = false;
                        // See the LOG screen's copy: same toggle. Stopping
                        // leaves the scan running, because the list you were
                        // looking at is still the thing you came here for.
                        if (engine.isHunted(s_confirmMac, s_rawScanIsBle)) {
                            engine.clearHunt();
                            Theme::showToast("HUNT STOPPED", nullptr, Theme::CYAN);
                        } else {
                            if (s_rawScanIsBle) engine.huntBle(s_confirmMac, s_confirmLabel);
                            else                engine.huntWifi(s_confirmMac, s_confirmLabel);
                            engine.stopRawScan();
                            enterHunt();
                        }
                    } else if (ctap == RawScanConfirmTap::CANCEL) {
                        lastTouch = now;
                        s_confirmPending = false;
                    }
                }
                break;
            }

            // BACK/SWITCH commit on release, not press, and only if the
            // touch never moved past the scroll threshold -- same
            // reasoning as LOG's identical fix: firing on press meant a
            // swipe starting on a button acted on it instantly, before
            // the drag could be recognized as a scroll. Independent of
            // (but coexists fine with) the row-hold gesture below --
            // whichever one actually has a target at the touch's start
            // position is the only one that ever fires anything.
            static bool gestureActive = false;
            static bool gestureMoved  = false;
            static int  gestureStartX = 0, gestureStartY = 0;
            if (touchJustDown) {
                gestureActive = true;
                gestureMoved  = false;
                gestureStartX = tp.x;
                gestureStartY = tp.y;
            }
            if (touchJustUp && gestureActive) {
                if (!gestureMoved) {
                    RawScanTap tap = uiRawScanHitTest(gestureStartX, gestureStartY, tft.width(), tft.height());
                    if (tap == RawScanTap::BACK) {
                        lastTouch = now;
                        engine.stopRawScan();
                        enterClear();
                    } else if (tap == RawScanTap::SWITCH) {
                        lastTouch = now;
                        enterRawScan(!s_rawScanIsBle);
                    }
                }
                gestureActive = false;
            }
            // Long-press a result row (once the scan's actually done)
            // to bring up the watch-confirm panel above -- disambiguated
            // from the drag-to-scroll gesture below by requiring the
            // touch to stay roughly still past a hold threshold, same
            // pattern CLEAR uses for petting Squachy (HELD).
            static bool     rowHoldFired  = false;
            static uint32_t rowHoldStart  = 0;
            static int      rowHoldX = 0, rowHoldY = 0;
            constexpr uint32_t ROW_HOLD_MS      = 500;
            constexpr int32_t  ROW_MOVE_PX_SQ   = 12 * 12;
            if (touchJustDown) {
                rowHoldFired = false;
                rowHoldStart = now;
                rowHoldX = tp.x;
                rowHoldY = tp.y;
            }
            if (done && tp.valid && !rowHoldFired) {
                int32_t hdx = tp.x - rowHoldX, hdy = tp.y - rowHoldY;
                if ((hdx * hdx + hdy * hdy) <= ROW_MOVE_PX_SQ && (now - rowHoldStart) >= ROW_HOLD_MS) {
                    int row = uiRawScanRowAt(*canvas, tp.x, tp.y, tft.width(), tft.height());
                    uint8_t count = s_rawScanIsBle ? engine.rawBleCount() : engine.rawWifiCount();
                    if (row >= 0 && row < (int)count) {
                        rowHoldFired = true;
                        bool haveTarget = false;
                        if (s_rawScanIsBle) {
                            const RawBleResult* r = engine.rawBleAt((uint8_t)row);
                            if (r) {
                                memcpy(s_confirmMac, r->mac, 6);
                                strncpy(s_confirmLabel, r->name[0] ? r->name : "Unnamed device",
                                        sizeof(s_confirmLabel) - 1);
                                haveTarget = true;
                            }
                        } else {
                            const uint8_t* bssid = engine.rawWifiBssid((uint8_t)row);
                            if (bssid) {
                                memcpy(s_confirmMac, bssid, 6);
                                const char* ssid = engine.rawWifiSsid((uint8_t)row);
                                strncpy(s_confirmLabel, ssid[0] ? ssid : "(hidden)", sizeof(s_confirmLabel) - 1);
                                haveTarget = true;
                            }
                        }
                        if (haveTarget) {
                            s_confirmLabel[sizeof(s_confirmLabel) - 1] = 0;
                            s_confirmPending = true;
                            s_confirmArmed   = false;
                        }
                    }
                }
            }
            // Swipe to scroll, same as LOG -- also marks gestureMoved
            // so the deferred BACK/SWITCH tap above cancels correctly
            // when this touch turns out to be a scroll.
            static int lastY = -1;
            if (touchJustDown) lastY = tp.y;
            if (tp.valid && lastY >= 0) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) {
                    gestureMoved = true;
                    uiRawScanScroll(dy > 0 ? -1 : 1);
                    lastY = tp.y;
                }
            } else if (!tp.valid) {
                lastY = -1;
            }
            break;
        }
        case AppState::SETTINGS: {
            // Nothing on this screen moves by the call -- no background, no
            // mascot -- so both passes are the same picture and the row
            // hashes find nothing to send once it has settled.
            drawTwoBand([&](TFT_eSPI& t, bool) { uiSettingsTick(t, now, engine); });
            // Row taps commit on release, not on press, and only if
            // the touch never moved past the scroll threshold -- same
            // fix as LOG/raw-scan: firing on press meant a swipe that
            // started on a row acted on it instantly, before the drag
            // could be recognized as a scroll instead.
            static bool gestureActive = false;
            static bool gestureMoved  = false;
            static int  gestureStartX = 0, gestureStartY = 0;
            static int  lastY = -1;
            // A tap is short. A thumb that rests on a row for half a second
            // was reaching for the list to scroll it, not choosing the row --
            // people kept opening things they were only scrolling past -- so
            // a long touch that never moved is spent, not acted on. Buttons
            // that need a hold (nothing on this screen) are unaffected.
            static uint32_t gestureDownMs = 0;
            // While a confirm panel is up it owns the screen: it answers the
            // tap, and the list underneath neither scrolls nor acts. Handled
            // before the gesture machinery rather than inside it so a drag
            // that starts on the panel cannot scroll the list out from under
            // the question being asked.
            static int confirmDownX = 0, confirmDownY = 0;
            if (uiSettingsConfirmRow() != SettingsRow::NONE) {
                // Where the finger went DOWN, not where it came up: tp is not
                // reliable on the release edge, which is why the row hit test
                // below keeps its own start coordinates too.
                if (touchJustDown) { confirmDownX = tp.x; confirmDownY = tp.y; }
                if (touchJustUp) {
                    lastTouch = now;
                    const SettingsRow pending = uiSettingsConfirmRow();
                    const SettingsConfirmTap ct =
                        uiSettingsHitConfirm(confirmDownX, confirmDownY, tft.width(), tft.height());
                    if (ct == SettingsConfirmTap::CONFIRM) {
                        uiSettingsSetConfirm(SettingsRow::NONE);
                        if (pending == SettingsRow::CALIBRATE) {
                            runTouchCalibration();
                            enterSettings();
                        } else if (pending == SettingsRow::RESET_STATS) {
                            engine.resetLifetime();
                            Dex::reset();
                            Regulars::reset();
                        } else if (pending == SettingsRow::BORING_MODE) {
                            Settings::toggleBoringMode();
                        } else if (pending == SettingsRow::REPLAY_INTRO) {
                            Squachy::replayIntro();
                            enterClear();
                        }
                    } else if (ct == SettingsConfirmTap::CANCEL) {
                        uiSettingsSetConfirm(SettingsRow::NONE);
                    }
                }
                gestureActive = false;
                break;
            }
            if (touchJustDown) {
                gestureActive = true;
                gestureMoved  = false;
                gestureStartX = tp.x;
                gestureStartY = tp.y;
                lastY = tp.y;
                gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) {
                    gestureMoved = true;
                    uiSettingsScroll(dy > 0 ? -1 : 1);
                    lastY = tp.y;
                }
            }
            if (touchJustUp && gestureActive) {
                if (!gestureMoved && now - gestureDownMs > TAP_MAX_MS) {
                    gestureActive = false;
                    break;
                }
                if (!gestureMoved) {
                    lastTouch = now;
                    // The pinned strip along the bottom: up a level from a
                    // sub-page, out of Settings from the main list. Reachable
                    // from anywhere in the list, which is the point of it.
                    // OK on the DESK MODE page: out, to the desk if that is
                    // where Settings was opened from (enterClear() honours
                    // that), otherwise the main screen.
                    if (uiSettingsTapPinnedOk(gestureStartX, gestureStartY, tft.width(), tft.height())) {
                        enterClear();
                        gestureActive = false;
                        break;
                    }
                    if (uiSettingsTapPinnedBack(*canvas, gestureStartX, gestureStartY,
                                                 tft.width(), tft.height())) {
                        if (uiSettingsCurrentPage() != SettingsPage::MAIN)
                            uiSettingsOpenPage(SettingsPage::MAIN);
                        else
                            enterClear();
                        gestureActive = false;
                        break;
                    }
                    // A heading folds its group away. Spends the tap.
                    if (uiSettingsTapHeader(*canvas, gestureStartX, gestureStartY,
                                             tft.width(), tft.height())) {
                        gestureActive = false;
                        break;
                    }
                    SettingsRow row = uiSettingsHitTest(*canvas, gestureStartX, gestureStartY, tft.width(), tft.height());
                    // Switched off by a mode: say so, rather than doing nothing
                    // and reading as a broken row.
                    if (uiSettingsRowIsOff(row)) {
                        Theme::showToast("BORING MODE IS ON", nullptr, Theme::CYAN);
                        gestureActive = false;
                        break;
                    }
                    switch (row) {
                        case SettingsRow::SYSTEM:
                            uiSettingsOpenPage(SettingsPage::SYSTEM);
                            break;
                        // Tapping a tracking row stops it. This and the pill on
                        // CLEAR are the only two ways to end a watch short of a
                        // reboot; before either existed there were none.
                        case SettingsRow::WATCH_TARGET:
                            engine.clearWatch();
                            Theme::showToast("WATCH STOPPED", nullptr, Theme::CYAN);
                            break;
                        case SettingsRow::HUNT_TARGET:
                            engine.clearHunt();
                            Theme::showToast("HUNT STOPPED", nullptr, Theme::CYAN);
                            break;
                        case SettingsRow::THEME:      Settings::cyclePalette(); break;
                        case SettingsRow::BACKGROUND: Settings::cycleBackground(); break;
                        case SettingsRow::BACKGROUND_LOCK: Settings::toggleBackgroundLocked(); break;
                        case SettingsRow::UPDATE_CHECK:    Settings::toggleUpdateCheck();     break;
                        case SettingsRow::TIME_ZONE:       Settings::cycleTimeZone();         break;
                        case SettingsRow::INVERT:
                            Settings::toggleInvert();
                            // XOR against the panel's own baseline, not an
                            // absolute call -- see PANEL_NEEDS_INVERSION.
                            tft.invertDisplay(PANEL_NEEDS_INVERSION != Settings::inverted());
                            break;
                        case SettingsRow::RGB_SWAP:
                            Settings::toggleRgbSwap();
                            applyColorOrder();
                            break;
                        case SettingsRow::ROTATION_LOCK: Settings::toggleRotationLock(); break;
                        case SettingsRow::BRIGHTNESS:
                            Settings::adjustBrightness(gestureStartX < tft.width() / 2 ? -16 : 16);
                            applyBrightness();
                            break;
                        case SettingsRow::CONFIDENCE: Settings::cycleMinConfidence(); break;
                        case SettingsRow::AUTO_QUIET:  Settings::cycleAutoQuiet(); break;
                        case SettingsRow::DETECTION_FILTER: enterDetFilter(); break;
                        case SettingsRow::POWER_SAVER: enterPower(); break;
#if defined(TWATCH_S3)
                        case SettingsRow::WATCH_RADIO: Settings::cycleRadioDuty(); break;
                        case SettingsRow::WATCH_BATTERY: break;   // a reading, not a switch
                        case SettingsRow::WATCH_RADIO_RESET: twatchRadioResetTap(); break;
                        case SettingsRow::WATCH_STEADY:
                            Settings::toggleSteadyPower();
                            twatchApplySteady();
                            twatchBatterySample(BlackBox::BATT_WHY_TIMER);   // a line in the log at the switch
                            break;
                        case SettingsRow::WATCH_TEMP: break;   // a reading, not a switch
                        case SettingsRow::WATCH_XTAL: twatchXtalStart(); break;
                        case SettingsRow::WATCH_BUZZ:
                            Settings::toggleBuzz();
                            // The sample only when turning it ON: playing it on
                            // the way off too made the tap feel like a test that
                            // passed, and left alerts silent.
                            if (Settings::buzz()) twatchBuzz(Buzz::SAMPLE);
                            Serial.printf("[buzz] alerts %s\n", Settings::buzz() ? "ON" : "OFF");
                            break;
#endif
                        case SettingsRow::STATUS_LIGHT: enterLight(); break;
                        case SettingsRow::SECURITY:    enterSecurity(); break;
                        case SettingsRow::IGNORED_DEVICES:  enterIgnoreList(); break;
#if SQUACH_MESH
                        case SettingsRow::SQUACHMESH:
                            // Asked once. After that the row opens the menu
                            // directly -- re-consenting on every visit trains
                            // people to dismiss the thing without reading it,
                            // which is worse than not asking.
                            if (Settings::meshConsent()) enterMeshMenu();
                            else                        enterMeshWarn();
                            break;
#endif
                        // These ask first -- see the confirm panel over in
                        // ui_settings. A row earns one when tapping it a
                        // second time does not put things back: calibration
                        // overwrites the calibration you are using, reset
                        // zeroes a count that most of the outfits are gated
                        // on, and the intro takes the screen over.
                        case SettingsRow::CALIBRATE:
                        case SettingsRow::RESET_STATS:
                        case SettingsRow::REPLAY_INTRO:
                            uiSettingsSetConfirm(row);
                            break;
                        case SettingsRow::BORING_MODE:
                            // Asked on the way IN only. Boring mode hides
                            // every Squachy row, which is exactly what makes
                            // it hard to undo by accident -- but turning it
                            // back off restores all of them, so a panel there
                            // would just be friction on the fix for the thing
                            // the panel exists to warn about.
                            if (Settings::boringMode()) Settings::toggleBoringMode();
                            else                        uiSettingsSetConfirm(row);
                            break;
                        case SettingsRow::CHECK_COLORS: enterColorCheck(true); break;
                        case SettingsRow::DIAGNOSTICS:  enterDiagnostics(); break;
                        case SettingsRow::WIFI_NETWORKS: enterWifiNets(); break;
                        case SettingsRow::DESK_MODE:    uiSettingsOpenPage(SettingsPage::DESK); break;
                        case SettingsRow::DESK_OPEN:
                            // Settings' BACK from the desk comes back to it;
                            // having just been sent there, that is not a
                            // detour anybody wants on the way out.
                            s_backToDesk = false;
                            enterDesk();
                            break;
                        case SettingsRow::DESK_BACKGROUND:
                            if (gestureStartX < tft.width() / 2) Settings::cyclePrevDeskBackground();
                            else                                 Settings::cycleDeskBackground();
                            break;
                        case SettingsRow::CLOCK_FONT:     Settings::cycleClockFont();     break;
                        case SettingsRow::CLOCK_SIZE:     Settings::cycleClockSize();     break;
                        case SettingsRow::CLOCK_BACKDROP: Settings::cycleClockBackdrop(); break;
#if SQUACH_MESH
                        case SettingsRow::DESK_SQUAD:   Settings::toggleDeskSquad();     break;
                        case SettingsRow::DESK_CROWD:   Settings::cycleDeskCrowd();      break;
                        case SettingsRow::DESK_VISIT:   Settings::toggleDeskFullVisit(); break;
#endif
                        case SettingsRow::UPDATE_FIRMWARE: enterUpdate(); break;
                        case SettingsRow::SHOW_OFF:
                            Squachy::startShowOff();
                            enterClear();
                            break;
                        case SettingsRow::SHADES_COLOR: Squachy::cycleShadesColor(); break;
                        case SettingsRow::SQUACHY_SIZE: Settings::cycleSquachySize(); break;
                        case SettingsRow::OUTFIT:       enterOutfit(); break;
                        case SettingsRow::PET:          Squachy::cyclePet(); break;
                        case SettingsRow::BANTER:       Settings::cycleBanter(); break;
                        case SettingsRow::VIEW_DIARY:   enterDiary(); break;
                        case SettingsRow::BINGO:        enterBingo(); break;
                        case SettingsRow::DEX:          enterDex(); break;
                        case SettingsRow::APPEARANCE:  uiSettingsOpenAppearance(true); break;
                        case SettingsRow::TOP_HAT:     Settings::toggleTopHat(); break;
                        // From a sub-page, back to the main list; from the
                        // main list, out.
                        case SettingsRow::BACK:
                            if (uiSettingsCurrentPage() != SettingsPage::MAIN) uiSettingsOpenPage(SettingsPage::MAIN);
                            else                                               enterClear();
                            break;
                        default: break;
                    }
                }
                gestureActive = false;
            }
            break;
        }
#if SQUACH_MESH
        case AppState::MESH_PHRASE: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiMeshPhraseTick(t, now, engine, advance); });
            if (touchJustDown) uiMeshPhraseTouch(tp.x, tp.y);
            if (uiMeshPhraseDone()) enterMeshMenu();
            break;
        }
        case AppState::MESH_COMPOSE: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiMeshComposeTick(t, now, engine, advance); });
            // Sent or not, back to the main screen -- that is where the
            // bubble's dots show it going out.
            if (touchJustDown) {
                const ComposeHit hit = uiMeshComposeTouch(tp.x, tp.y, now);
                // TYPE -- or EDIT on a typed message -- opens the keyboard,
                // carrying whatever is typed so far.
                if (hit == ComposeHit::TYPE) { enterPhoneMessage(uiMeshComposeTyped()); break; }
                // "?" replays the tutorial, which runs on the main screen.
                if (hit == ComposeHit::HELP) MeshTutor::start();
                if (hit != ComposeHit::NONE) enterClear();
            }
            break;
        }
        case AppState::MESH_WARN: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiMeshWarnTick(t, now, engine, advance); });
            if (touchJustDown) {
                switch (uiMeshWarnHitTest(*canvas, tp.x, tp.y)) {
                    case MeshWarnHit::YES:
                        Settings::setMeshConsent(true);
                        enterMeshMenu();
                        break;
                    // Nothing is stored on NO. Declining is not a decision
                    // worth remembering -- it just means not now.
                    case MeshWarnHit::NO: enterSettings(); break;
                    default: break;
                }
            }
            break;
        }
        case AppState::MESH_MENU: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiMeshMenuTick(t, now, engine, advance); });
            if (touchJustDown && Theme::pinnedBackHit(tp.x, tp.y, canvas->width(), canvas->height())) {
                lastTouch = now;
                enterSettings();
                break;
            }
            if (touchJustDown) {
                switch (uiMeshMenuHitTest(*canvas, tp.x, tp.y,
                                          canvas->width(), canvas->height())) {
                    case MeshMenuRow::DETECT:   Settings::cycleMeshDetect();   break;
                    case MeshMenuRow::TRANSMIT: Settings::cycleMeshTransmit(); break;
                    case MeshMenuRow::MESSAGES:
                        Settings::toggleMessages();
                        // The first time they go on, the tutorial runs. It is
                        // marked seen as it STARTS, so skipping it counts; the
                        // "?" on the message screen replays it. Not in boring
                        // mode, which has no Squachy to visit.
                        if (Settings::messagesOn() && !Settings::meshTutorSeen() &&
                            !Settings::boringMode()) {
                            Settings::setMeshTutorSeen();
                            MeshTutor::start();
                            enterClear();
                        }
                        break;
                    case MeshMenuRow::CROWD:    Settings::cycleMeshCrowd();    break;
                    case MeshMenuRow::SQUAD:    enterSquad(true);              break;
                    case MeshMenuRow::PHRASE:   enterMeshPhrase();             break;
                    case MeshMenuRow::NAME:     enterPhone();                  break;
                    case MeshMenuRow::BACK:     enterSettings();               break;
                    default: break;
                }
            }
            break;
        }
        case AppState::UPDATE: {
            // An update in progress owns the screen. Dimming, auto-lock and
            // the idle frame cap all run off lastTouch, so holding it at now
            // keeps all three away until it is over.
            if (OtaBle::state() != OtaBle::State::OFF || OtaWifi::state() != OtaWifi::State::OFF ||
                OtaCore::restartPending()) lastTouch = now;
            {
                // Without the frame buffer every draw lands on the panel as it
                // happens, so a full redraw each frame would flicker. Redraw
                // only when something on the screen would actually change.
                // A full repaint only when the screen changes to something
                // else; while a download runs, just the bar and its numbers,
                // painted over what is already there, four times a second.
                //
                // frameBufferOk was standing in for "the drawing is buffered",
                // and on the 3.5" those are not the same thing: the buffer
                // exists but every screen except the main one drew straight at
                // the panel, so this screen took the full-repaint branch and
                // repainted the glass itself, whole, every frame. That is the
                // flicker and the diagonal tear -- the panel was scanning out
                // rows while they were still being drawn. Through the bands it
                // is buffered like everything else, and a screen that is not
                // changing sends no rows at all.
                if (frameBufferOk) {
                    drawTwoBand([&](TFT_eSPI& t, bool) { uiUpdateTick(t, now); });
                } else {
                    static uint32_t lastKey = 0xFFFFFFFFUL, lastLive = 0;
                    const uint32_t key = ((uint32_t)OtaWifi::state() << 24) | ((uint32_t)OtaBle::state() << 20) |
                                         ((uint32_t)OtaWifi::netCount() << 1) |
                                         (OtaWifi::canTryAgain() ? 2u : 0u) | (OtaCore::restartPending() ? 1u : 0u);
                    if (key != lastKey) {
                        uiUpdateTick(*canvas, now, true);
                        lastKey  = key;
                        lastLive = now;
                    } else if (now - lastLive >= 250) {
                        uiUpdateTick(*canvas, now, false);
                        lastLive = now;
                    }
                }
            }
#if SQUACH_MESH
            autoUpdateTick(now);
#endif
            // touchJustDown, not the debounce timer: lastTouch is pinned above.
            if (touchJustDown) {
                int netIndex = -1;
                switch (uiUpdateHitTest(*canvas, tp.x, tp.y, &netIndex)) {
                    case UpdateHit::WIFI_START:
                        engine.startUpdateRadio();
                        if (!OtaWifi::begin()) {
                            engine.stopUpdateRadio();
                            Theme::showToast("CAN'T START UPDATE", updateRefusedWhy(), Theme::AMBER);
                        }
                        break;
                    case UpdateHit::NETWORK: {
                        const OtaWifi::Net* n = OtaWifi::net((uint8_t)netIndex);
                        if (!n) break;
                        const int8_t k = OtaWifi::savedIndexOf(n->ssid);
                        if (k >= 0) {
                            OtaWifi::connectSavedAt((uint8_t)k);
                        } else if (n->open) {
                            OtaWifi::connect(n->ssid, "", true);
                        } else {
                            enterWifiPass(n->ssid);
                        }
                        break;
                    }
                    case UpdateHit::RESCAN:    OtaWifi::rescan();   break;
                    case UpdateHit::INSTALL:   lendFrameToDownload(); OtaWifi::install(); break;
                    case UpdateHit::TRY_AGAIN: OtaWifi::tryAgain(); break;
                    case UpdateHit::BT_START:
                        // The radio first: NimBLE will not register the update
                        // service while a scan is still running.
                        engine.startUpdateRadio();
                        if (!OtaBle::begin()) {
                            engine.stopUpdateRadio();
                            Theme::showToast("CAN'T START UPDATE", "Leave and try again", Theme::AMBER);
                        }
                        break;
                    case UpdateHit::SWITCH:        uiUpdateAskSwitch(true);  break;
                    case UpdateHit::SWITCH_CANCEL: uiUpdateAskSwitch(false); break;
                    case UpdateHit::SWITCH_CONFIRM:
                        uiUpdateAskSwitch(false);
                        if (OtaCore::switchToOther() != OtaCore::Fail::NONE)
                            Theme::showToast("CAN'T SWITCH", "That version won't start", Theme::AMBER);
                        break;
                    case UpdateHit::CANCEL:
                    case UpdateHit::OK:
                        if (OtaWifi::state() != OtaWifi::State::OFF) {
                            // end() answers false now: nothing was handed
                            // back, so detection just starts again. It kept
                            // the true-means-restart answer from when an
                            // update cost the board its Bluetooth.
                            if (!OtaWifi::end()) engine.stopUpdateRadio();
                        } else {
                            OtaBle::end();
                            engine.stopUpdateRadio();
                        }
                        uiUpdateInit(*canvas);
                        break;
                    case UpdateHit::BACK:
                        enterSettings();
                        uiSettingsOpenPage(SettingsPage::SYSTEM);
                        break;
#if SQUACH_MESH
                    case UpdateHit::SQUAD_START: enterSquadUpdate(); break;
#endif
                    default: break;
                }
            }
            break;
        }
        case AppState::WIFI_PASS: {
            // Still inside update mode, with detection paused: the same pin on
            // lastTouch keeps auto-lock from taking the screen mid-password.
            lastTouch = now;
            // The board redraws only what changed, so it is the same with the
            // frame buffer and without it (given up for a download).
            drawTwoBand([&](TFT_eSPI& t, bool) { uiWifiPassTick(t, now); });
            if (touchJustDown)    uiWifiPassTouch(tp.x, tp.y, now, WifiPassTouch::DOWN);
            else if (tp.valid)    uiWifiPassTouch(tp.x, tp.y, now, WifiPassTouch::MOVE);
            else if (touchJustUp) uiWifiPassTouch(tp.x, tp.y, now, WifiPassTouch::UP);
            const WifiPassResult r = uiWifiPassResult();
            if (r == WifiPassResult::OK && s_passForNets) {
                s_passForNets = false;
                const bool ok = OtaWifi::saveNetwork(uiWifiPassSsid(), uiWifiPassText());
                uiWifiPassClear();
                Theme::showToast(ok ? "SAVED" : "LIST FULL", ok ? "Checked at the next boot" : "Remove one first",
                                 ok ? Theme::CYAN : Theme::AMBER);
                enterWifiNets();
            } else if (r == WifiPassResult::BACK && s_passForNets) {
                s_passForNets = false;
                uiWifiPassClear();
                enterWifiNets();
            } else if (r == WifiPassResult::OK) {
                OtaWifi::connect(uiWifiPassSsid(), uiWifiPassText(), true);
                uiWifiPassClear();
                enterUpdate();
            } else if (r == WifiPassResult::BACK) {
                uiWifiPassClear();
                enterUpdate();
            }
            break;
        }
        case AppState::WIFI_NETS: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiWifiNetsTick(t, now); });
            if (touchJustDown) {
                int row = -1;
                switch (uiWifiNetsHit(*canvas, tp.x, tp.y, &row)) {
                    case WifiNetsHit::ROW: uiWifiNetsSelect(row); break;
                    case WifiNetsHit::USE: {
                        const int s = uiWifiNetsSelected();
                        if (s >= 0 && s < OtaWifi::savedCount()) {
                            OtaWifi::useSaved((uint8_t)s);
                            Theme::showToast("TRIED FIRST", OtaWifi::savedSsidAt((uint8_t)s), Theme::CYAN);
                        }
                        break;
                    }
                    case WifiNetsHit::REMOVE: {
                        const int s = uiWifiNetsSelected();
                        if (s >= 0 && s < OtaWifi::savedCount()) {
                            OtaWifi::removeSaved((uint8_t)s);
                            uiWifiNetsSelect(OtaWifi::savedCount() ? (int)OtaWifi::savedUse() : -1);
                            Theme::showToast("REMOVED", nullptr, Theme::CYAN);
                        }
                        break;
                    }
                    case WifiNetsHit::ADD:
                        if (OtaWifi::savedCount() >= OtaWifi::SAVED_MAX)
                            Theme::showToast("LIST FULL", "Remove one first", Theme::AMBER);
                        else
                            enterWifiAdd();
                        break;
                    case WifiNetsHit::BACK: enterSettings(); break;
                    default: break;
                }
            }
            break;
        }
        case AppState::WIFI_ADD: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiWifiAddTick(t, now, engine); });
            if (touchJustDown) {
                int row = -1;
                switch (uiWifiAddHit(*canvas, tp.x, tp.y, engine, &row)) {
                    case WifiAddHit::ROW: {
                        char ssid[33];
                        snprintf(ssid, sizeof ssid, "%s", engine.rawWifiSsid((uint8_t)row));
                        const bool open = engine.rawWifiOpen((uint8_t)row);
                        engine.stopRawScan();
                        if (!ssid[0]) {
                            Theme::showToast("HIDDEN NETWORK", "No name to save", Theme::AMBER);
                            enterWifiNets();
                        } else if (open) {
                            const bool ok = OtaWifi::saveNetwork(ssid, "");
                            Theme::showToast(ok ? "SAVED" : "LIST FULL", ok ? "Open network" : "Remove one first",
                                             ok ? Theme::CYAN : Theme::AMBER);
                            enterWifiNets();
                        } else {
                            s_passForNets = true;
                            enterWifiPass(ssid);
                        }
                        break;
                    }
                    case WifiAddHit::RESCAN: engine.stopRawScan(); engine.startRawWifiScan(); break;
                    case WifiAddHit::BACK:   engine.stopRawScan(); enterWifiNets(); break;
                    default: break;
                }
            }
            break;
        }
        case AppState::NUDGE: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiNudgeTick(t, now, engine); });
            lastTouch = now;     // no dimming, no auto-lock, mid-count
            NudgeHit hit = NudgeHit::NONE;
            if (touchJustDown) hit = uiNudgeHit(*canvas, tp.x, tp.y);
            if (hit == NudgeHit::SKIP) { Serial.println("[nudge] skipped"); enterClear(); break; }
            if (hit == NudgeHit::NOW || uiNudgeSecondsLeft(now) <= 0) startNudgedUpdate();
            break;
        }
        case AppState::SQUAD_UPDATE: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiSquadUpdateTick(t, now, engine); });
            if (touchJustDown) {
                switch (uiSquadUpdateHit(*canvas, tp.x, tp.y)) {
                    case SquadUpdateHit::SHARE: uiSquadUpdateToggleShare(); break;
                    case SquadUpdateHit::SEND: {
                        uint8_t ver[3] = { 0, 0, 0 };
                        MeshMsg::parseVersion(OtaCore::runningVersion(), ver);
                        char ssid[33] = "", pass[65] = "";
                        const int8_t share = uiSquadUpdateShareIndex();
                        if (uiSquadUpdateShareWifi() && share >= 0) {
                            snprintf(ssid, sizeof ssid, "%s", OtaWifi::savedSsidAt((uint8_t)share));
                            OtaWifi::savedPassAt((uint8_t)share, pass, sizeof pass);
                        }
                        const MeshTalk::Send r = MeshTalk::sendNudge(ver, ssid[0] ? ssid : nullptr, pass, now);
                        memset(pass, 0, sizeof pass);
                        uiSquadUpdateSent(r == MeshTalk::Send::OK, now);
                        if (r == MeshTalk::Send::NOT_READY)    Theme::showToast("CAN'T SEND", "Messages need a phrase first", Theme::AMBER);
                        else if (r == MeshTalk::Send::TRANSMIT_OFF) Theme::showToast("CAN'T SEND", "Turn TRANSMIT on in SquachMesh", Theme::AMBER);
                        else if (r != MeshTalk::Send::OK)      Theme::showToast("CAN'T SEND", "WiFi password too long to share", Theme::AMBER);
                        break;
                    }
                    case SquadUpdateHit::BACK: engine.stopRawScan(); enterUpdate(); break;
                    default: break;
                }
            }
            break;
        }
        case AppState::INVITE: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiInviteTick(t, now, engine); });
            lastTouch = now;
            // A confirmed finish -- YOU'RE IN on one board, ADDED on the
            // other -- shows for a few seconds and then gets out of the
            // way, the same on both sides. Anything unconfirmed or failed
            // waits to be read.
            {
                const MeshTalk::InviteState st = MeshTalk::inviteState();
                const bool happy = st == MeshTalk::InviteState::JOINED ||
                                   (st == MeshTalk::InviteState::DONE && MeshTalk::inviteConfirmed());
                if (happy && now - MeshTalk::inviteSince() > 4000) {
                    MeshTalk::inviteCancel();
                    enterClear();
                    break;
                }
            }
            if (touchJustDown) {
                switch (uiInviteHit(*canvas, tp.x, tp.y)) {
                    case InviteHit::ACCEPT: {
                        const MeshTalk::Send r = MeshTalk::inviteAccept(now);
                        if (r == MeshTalk::Send::TRANSMIT_OFF) {
                            Theme::showToast("CAN'T ANSWER", "Turn TRANSMIT on in SquachMesh", Theme::AMBER);
                            MeshTalk::inviteCancel();
                            enterClear();
                        }
                        break;
                    }
                    case InviteHit::DECLINE: MeshTalk::inviteDecline(); enterClear(); break;
                    case InviteHit::MATCH:   MeshTalk::inviteConfirm(now); break;
                    case InviteHit::NOMATCH: MeshTalk::inviteCancel(); Theme::showToast("INVITE STOPPED", "The digits did not match", Theme::AMBER); enterClear(); break;
                    case InviteHit::CANCEL:  MeshTalk::inviteCancel(); enterClear(); break;
                    case InviteHit::SHOW:    break;     // the screen shows the phrase itself
                    case InviteHit::BACK:
                        // Leaving a finished or failed invite clears it; leaving
                        // SENDING lets the phrase finish its time on the air.
                        if (MeshTalk::inviteState() != MeshTalk::InviteState::SENDING) MeshTalk::inviteCancel();
                        enterClear();
                        break;
                    default: break;
                }
            }
            break;
        }
        case AppState::SQUAD: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiSquadTick(t, now, engine, advance); });
            if (touchJustDown && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                switch (uiSquadTouch(tp.x, tp.y, now)) {
                    case SquadHit::BACK:    if (uiSquadRosterMode()) enterMeshMenu(); else enterClear(); break;
                    // Back to the main screen to watch the swap happen.
                    case SquadHit::INVITED: enterClear(); break;
                    case SquadHit::REPLY:   enterMeshCompose(); break;
                    // A fox hunt: their board is the target, the HUNT gauge the
                    // receiver. Already hunting them: just go to the gauge.
                    case SquadHit::HUNT: {
                        const uint8_t* mac = uiSquadSelectedMac();
                        if (!mac) break;
                        if (!engine.isHunted(mac, true)) engine.huntBle(mac, uiSquadSelectedName());
                        enterHunt();
                        break;
                    }
                    case SquadHit::ADD: {
                        const uint8_t* mac = uiSquadSelectedMac();
                        if (!mac) break;
                        const MeshTalk::Send r = MeshTalk::inviteStart(mac, uiSquadSelectedName(), now);
                        if (r == MeshTalk::Send::OK)            enterInvite();
                        else if (r == MeshTalk::Send::NOT_READY) Theme::showToast("NO PHRASE TO SHARE", "Set one under SQUACHMESH first", Theme::AMBER);
                        else if (r == MeshTalk::Send::TRANSMIT_OFF) Theme::showToast("CAN'T SEND", "Turn TRANSMIT on in SquachMesh", Theme::AMBER);
                        else                                     Theme::showToast("CAN'T START", "Try again in a moment", Theme::AMBER);
                        break;
                    }
                    default: break;
                }
            }
            break;
        }
        case AppState::PHONE: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiPhoneTick(t, now, engine, advance); });
            // All three edges of a touch, not just the press. The payphone
            // keypad still acts on the press and ignores the rest; the
            // QWERTY board previews on the press, follows the finger, and
            // types on the release -- see ui_phone.h. Both come through here
            // because the choice between them is a setting the screen reads,
            // not a different screen.
            //
            // The wake-tap swallow near the top of loop() clears tp.valid and
            // both edges for the whole gesture, so a touch that only woke the
            // display reaches none of these.
            if (touchJustDown)    uiPhoneTouch(tp.x, tp.y, now, PhoneTouch::DOWN);
            else if (tp.valid)    uiPhoneTouch(tp.x, tp.y, now, PhoneTouch::MOVE);
            else if (touchJustUp) uiPhoneTouch(tp.x, tp.y, now, PhoneTouch::UP);
            // Back to where it was opened from, not to Settings.
            if (uiPhoneDone()) {
                // A message goes back to the message screen to be read over
                // before it is sent; a name goes back to the menu it came from.
                if (uiPhoneMessageMode()) {
                    const char* m = uiPhoneMessage();
                    enterMeshCompose();
                    if (m && m[0]) uiMeshComposeSetTyped(m);
                } else {
                    enterMeshMenu();
                }
            }
            break;
        }
#endif
        case AppState::IGNORE_LIST: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiIgnoreListTick(t, now); });
            // Same drag-to-scroll / act-on-release gesture the detection
            // filter uses: committing on press would make a swipe that
            // starts on a REMOVE button fire it before the drag is
            // recognised as a scroll.
            static bool ilActive = false, ilMoved = false;
            static int  ilStartX = 0, ilStartY = 0, ilLastY = -1;
            if (touchJustDown) {
                ilActive = true; ilMoved = false;
                ilStartX = tp.x; ilStartY = tp.y; ilLastY = tp.y;
            }
            if (tp.valid && ilActive) {
                int dy = tp.y - ilLastY;
                if (abs(dy) > 10) {
                    ilMoved = true;
                    uiIgnoreListScroll(dy > 0 ? -1 : 1);
                    ilLastY = tp.y;
                }
            }
            if (touchJustUp && ilActive) {
                if (!ilMoved && Theme::pinnedBackHit(ilStartX, ilStartY, tft.width(), tft.height())) {
                    ilActive = false;
                    lastTouch = now;
                    enterSettings();
                    break;
                }
                if (!ilMoved) {
                    uint8_t hit = uiIgnoreListHitRemove(*canvas, ilStartX, ilStartY,
                                                        tft.width(), tft.height());
                    if (hit != 0xFF) {
                        lastTouch = now;
                        const uint8_t* mac = IgnoreList::macAt(hit);
                        if (mac) {
                            // Copy first: remove() backfills the hole with
                            // the last entry, so the pointer it was read
                            // from stops meaning what it meant.
                            uint8_t tmp[6];
                            memcpy(tmp, mac, 6);
                            IgnoreList::remove(tmp);
                            uiIgnoreListScroll(0);   // re-clamp after shrink
                        }
                    }
                }
                ilActive = false;
            }
            break;
        }
        case AppState::DETECTION_FILTER: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiDetFilterTick(t, now, engine); });
            // Same drag-to-scroll / tap-on-release-to-toggle gesture
            // the Settings screen above uses, and for the same reason:
            // committing on press would make a swipe that starts on a
            // row toggle it before the drag is recognized as a scroll.
            static bool gestureActive = false;
            static bool gestureMoved  = false;
            static int  gestureStartX = 0, gestureStartY = 0;
            static int  lastY = -1;
            static uint32_t gestureDownMs = 0;
            if (touchJustDown) {
                gestureActive = true;
                gestureMoved  = false;
                gestureStartX = tp.x;
                gestureStartY = tp.y;
                lastY = tp.y;
                gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) {
                    gestureMoved = true;
                    uiDetFilterScroll(dy > 0 ? -1 : 1);
                    lastY = tp.y;
                }
            }
            if (touchJustUp && gestureActive) {
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS && Theme::pinnedBackHit(gestureStartX, gestureStartY, tft.width(), tft.height())) {
                    gestureActive = false;
                    lastTouch = now;
                    enterSettings();
                    break;
                }
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS) {
                    lastTouch = now;
                    DetectionType hit = uiDetFilterHitTest(*canvas, gestureStartX, gestureStartY, tft.width(), tft.height());
                    // iBeacons ask first, on the way ON only: see
                    // ui_beaconwarn.h for why this one type does.
                    if (hit == DetectionType::IBEACON && !Settings::typeEnabled(hit)) enterBeaconWarn();
                    else if (hit != DetectionType::COUNT) Settings::toggleType(hit);
                }
                gestureActive = false;
            }
            break;
        }
        case AppState::BEACON_WARN: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiBeaconWarnTick(t, now, engine, advance); });
            if (touchJustDown) {
                switch (uiBeaconWarnHitTest(*canvas, tp.x, tp.y)) {
                    case BeaconWarnHit::ENABLE:
                        if (!Settings::typeEnabled(DetectionType::IBEACON))
                            Settings::toggleType(DetectionType::IBEACON);
                        enterDetFilter(true);
                        break;
                    case BeaconWarnHit::KEEP_OFF: enterDetFilter(true); break;
                    default: break;
                }
            }
            break;
        }
        case AppState::SECURITY: {
            drawTwoBand([&](TFT_eSPI& t, bool) {
                uiSecurityTick(t, now, engine);
                Theme::drawToast(t, now);
            });
            // The same drag-to-scroll, act-on-release gesture as POWER SAVER.
            static bool gestureActive = false, gestureMoved = false;
            static int  gestureStartX = 0, gestureStartY = 0, lastY = -1;
            static uint32_t gestureDownMs = 0;
            if (touchJustDown) {
                gestureActive = true; gestureMoved = false;
                gestureStartX = tp.x; gestureStartY = tp.y; lastY = tp.y; gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) { gestureMoved = true; uiSecurityScroll(dy > 0 ? -1 : 1); lastY = tp.y; }
            }
            if (touchJustUp && gestureActive) {
                gestureActive = false;
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS && Theme::pinnedBackHit(gestureStartX, gestureStartY, tft.width(), tft.height())) {
                    lastTouch = now;
                    enterSettings();
                    break;
                }
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS) {
                    lastTouch = now;
                    const bool on = Security::enabled();
                    // Everything below PIN LOCK is inert until a PIN exists,
                    // and PIN LENGTH is inert once one does -- the row draws
                    // dim to match.
                    switch (uiSecurityHitTest(*canvas, gestureStartX, gestureStartY, tft.width(), tft.height())) {
                        case SecurityRow::PIN_LOCK:
                            startPinFlow(on ? PinFlow::OFF_VERIFY : PinFlow::SET_NEW);
                            break;
                        case SecurityRow::PIN_LENGTH:
                            if (!on) {
                                const Security::PinLen n = Security::pinLen();
                                Security::setPinLength(n == Security::PinLen::FOUR ? Security::PinLen::SIX
                                                     : n == Security::PinLen::SIX  ? Security::PinLen::EIGHT
                                                                                   : Security::PinLen::FOUR);
                            }
                            break;
                        case SecurityRow::CHANGE_PIN:   if (on) startPinFlow(PinFlow::CHANGE_CUR); break;
                        case SecurityRow::DURESS_PIN:
                            if (on) startPinFlow(Security::hasDuress() ? PinFlow::DURESS_OFF : PinFlow::DURESS_CUR);
                            break;
                        case SecurityRow::AUTO_LOCK:    if (on) Security::cycleAutoLock(); break;
                        case SecurityRow::LOCK_AT_BOOT: if (on) Security::setLockAtBoot(!Security::lockAtBoot()); break;
                        case SecurityRow::WIPE_ON_FAIL: if (on) Security::setWipeOnFail(!Security::wipeOnFail()); break;
                        case SecurityRow::LOCK_ALERTS:  if (on) Security::cycleLockAlerts(); break;
                        case SecurityRow::REMOTE_UPDATE: Settings::toggleRemoteUpdate(); break;
                        default: break;
                    }
                }
            }
            break;
        }
        case AppState::PIN_ENTRY: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiPhoneTick(t, now, engine, advance); });
            if (touchJustDown) uiPhoneTouch(tp.x, tp.y, now, PhoneTouch::DOWN);
            if (!uiPhoneDone()) break;
            if (!uiPhonePinReady()) { memset(s_pinFirst, 0, sizeof s_pinFirst); enterSecurity(); break; }   // BACK
            char d[9];
            strncpy(d, uiPhonePinDigits(), sizeof d - 1);
            d[sizeof d - 1] = '\0';
            const bool same = strcmp(d, s_pinFirst) == 0;
            bool done = false;
            switch (s_pinFlow) {
                case PinFlow::SET_NEW:
                case PinFlow::CHANGE_NEW:
                    if (Security::isDuress(d)) { startPinFlow(s_pinFlow, "THAT IS THE DURESS PIN"); break; }
                    memcpy(s_pinFirst, d, sizeof s_pinFirst);
                    startPinFlow(s_pinFlow == PinFlow::SET_NEW ? PinFlow::SET_AGAIN : PinFlow::CHANGE_AGAIN);
                    break;
                case PinFlow::SET_AGAIN:
                case PinFlow::CHANGE_AGAIN: {
                    const bool first = (s_pinFlow == PinFlow::SET_AGAIN);
                    if (!same) { startPinFlow(first ? PinFlow::SET_NEW : PinFlow::CHANGE_NEW, "DIDN'T MATCH - AGAIN"); break; }
                    Security::setPin(s_pinFirst);
                    // Said once, where it is set: what the lock is for, and
                    // what it is not.
                    if (first) Theme::showToast("PIN LOCK ON", "Snoops, not USB cables", Theme::AMBER);
                    else       Theme::showToast("PIN CHANGED", nullptr, Theme::GREEN);
                    done = true;
                    break;
                }
                case PinFlow::OFF_VERIFY:
                    if (!Security::verify(d)) { startPinFlow(PinFlow::OFF_VERIFY, "WRONG - CURRENT PIN"); break; }
                    Security::disable();
                    Theme::showToast("PIN LOCK OFF", nullptr, Theme::AMBER);
                    done = true;
                    break;
                case PinFlow::CHANGE_CUR:
                case PinFlow::DURESS_CUR:
                    if (!Security::verify(d)) { startPinFlow(s_pinFlow, "WRONG - CURRENT PIN"); break; }
                    startPinFlow(s_pinFlow == PinFlow::CHANGE_CUR ? PinFlow::CHANGE_NEW : PinFlow::DURESS_NEW);
                    break;
                case PinFlow::DURESS_NEW:
                    if (Security::verify(d)) { startPinFlow(PinFlow::DURESS_NEW, "MUST DIFFER FROM PIN"); break; }
                    memcpy(s_pinFirst, d, sizeof s_pinFirst);
                    startPinFlow(PinFlow::DURESS_AGAIN);
                    break;
                case PinFlow::DURESS_AGAIN:
                    if (!same) { startPinFlow(PinFlow::DURESS_NEW, "DIDN'T MATCH - AGAIN"); break; }
                    Security::setDuress(s_pinFirst);
                    Theme::showToast("DURESS PIN SET", "Wipes, then unlocks", Theme::RED);
                    done = true;
                    break;
                case PinFlow::DURESS_OFF:
                    if (!Security::verify(d)) { startPinFlow(PinFlow::DURESS_OFF, "WRONG - CURRENT PIN"); break; }
                    Security::clearDuress();
                    Theme::showToast("DURESS PIN OFF", nullptr, Theme::AMBER);
                    done = true;
                    break;
            }
            memset(d, 0, sizeof d);
            if (done) { memset(s_pinFirst, 0, sizeof s_pinFirst); enterSecurity(); }
            break;
        }
        case AppState::LOCKED: {
            // Detection runs on underneath, and ALERTS WHEN LOCKED decides how
            // much of a new one reaches the glass. The same test CLEAR makes.
            {
                const Security::LockAlerts la = Security::lockAlerts();
                const Detection* latest = engine.latest();
                if (la != Security::LockAlerts::NONE && latest && (now - latest->firstSeen) < 200 &&
                    latest->conf >= Settings::minConfidence() && !IgnoreList::silenced(latest->mac) &&
                    alertMayInterrupt(*latest)) {
                    uiAlertSetRedacted(la == Security::LockAlerts::TYPE_ONLY);
                    enterAlert(*latest);
                    break;
                }
                if (la == Security::LockAlerts::FULL && engine.watchHitPending()) { enterWatchAlert(); break; }
            }
            const uint32_t wait = Security::lockoutRemainingMs(now);
            static char waitMsg[24];
            if (wait) {
                snprintf(waitMsg, sizeof waitMsg, "WAIT %lu s", (unsigned long)((wait + 999) / 1000));
                uiPhonePinWait(waitMsg);
            } else {
                uiPhonePinWait(nullptr);
            }
#if SQUACH_MESH
            uiPhonePinPrompt(MeshTalk::inbox().unread ? "LOCKED - NEW MESSAGE" : "LOCKED");
#endif
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiPhoneTick(t, now, engine, advance); });
            if (touchJustDown) uiPhoneTouch(tp.x, tp.y, now, PhoneTouch::DOWN);
            if (uiPhonePinForgot()) {
                // A forgotten PIN: every secret goes, and the PIN with it, and
                // the board comes back unlocked. Settings and outfits stay.
                Security::disable();
                performWipe(WipeBoot::UNLOCKED);
                break;
            }
            if (uiPhoneDone() && uiPhonePinReady()) {
                switch (Security::check(uiPhonePinDigits(), now)) {
                    case Security::Check::OK:
                        uiAlertSetRedacted(false);
                        enterClear();
                        break;
                    case Security::Check::DURESS: performWipe(WipeBoot::UNLOCKED); break;
                    case Security::Check::WIPED:  performWipe(WipeBoot::LOCKED);   break;
                    default:                      uiPhonePinReject();              break;
                }
            }
            break;
        }
        case AppState::POWER_SAVER: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiPowerTick(t, now, engine); });
            // Same drag-to-scroll, commit-on-release gesture the Settings and
            // type-filter lists use, and for the same reason: committing on
            // press turns a swipe that happens to start on a row into a
            // toggle before the drag is ever recognised as a scroll.
            static bool gestureActive = false;
            static bool gestureMoved  = false;
            static int  gestureStartX = 0, gestureStartY = 0;
            static int  lastY = -1;
            static uint32_t gestureDownMs = 0;
            if (touchJustDown) {
                gestureActive = true; gestureMoved = false;
                gestureStartX = tp.x; gestureStartY = tp.y; lastY = tp.y; gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) {
                    gestureMoved = true;
                    uiPowerScroll(dy > 0 ? -1 : 1);
                    lastY = tp.y;
                }
            }
            if (touchJustUp && gestureActive) {
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS && Theme::pinnedBackHit(gestureStartX, gestureStartY, tft.width(), tft.height())) {
                    gestureActive = false;
                    lastTouch = now;
                    enterSettings();
                    break;
                }
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS) {
                    lastTouch = now;
                    PowerRow hit = uiPowerHitTest(*canvas, gestureStartX, gestureStartY,
                                                  tft.width(), tft.height());
                    switch (hit) {
                        case PowerRow::ENABLED:
                            Settings::togglePowerSaver();
                            // Turning it off has to undo whatever it was
                            // doing, immediately and on this frame -- leaving
                            // a dimmed screen behind after switching the
                            // feature off would read as a bug.
                            applyCpuClock();
                            if (s_screenDimmed) { s_screenDimmed = false; applyBrightness(); }
                            break;
                        case PowerRow::SCREEN_TIMEOUT: Settings::cycleScreenTimeout(); break;
                        case PowerRow::DIM_LEVEL:
                            // Left half darker, right half brighter, the same
                            // split the BRIGHTNESS row uses.
                            Settings::adjustDimLevel(gestureStartX < tft.width() / 2 ? -8 : 8);
                            if (s_screenDimmed) applyBrightness();   // show it live
                            break;
                        case PowerRow::IDLE_FPS:      Settings::cycleIdleFps(); break;
                        case PowerRow::IDLE_AFTER:    Settings::cycleIdleAfter(); break;
                        case PowerRow::CPU_CLOCK:
                            Settings::cycleCpuMhz();
                            applyCpuClock();
                            break;
                        case PowerRow::WAKE_ON_ALERT: Settings::toggleWakeOnAlert(); break;
#if defined(TWATCH_S3)
                        case PowerRow::RADIO_DUTY:    Settings::cycleRadioDuty(); break;
#endif
#if defined(TWATCH_S3)
#endif
                        default: break;
                    }
                }
                gestureActive = false;
            }
            break;
        }
        case AppState::STATUS_LIGHT: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiLightTick(t, now, engine); });
            static bool gestureActive = false;
            static bool gestureMoved  = false;
            static int  gestureStartX = 0, gestureStartY = 0;
            static int  lastY = -1;
            static uint32_t gestureDownMs = 0;
            if (touchJustDown) {
                gestureActive = true; gestureMoved = false;
                gestureStartX = tp.x; gestureStartY = tp.y; lastY = tp.y; gestureDownMs = now;
            }
            if (tp.valid && gestureActive) {
                int dy = tp.y - lastY;
                if (abs(dy) > 10) {
                    gestureMoved = true;
                    uiLightScroll(dy > 0 ? -1 : 1);
                    lastY = tp.y;
                }
            }
            if (touchJustUp && gestureActive) {
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS && Theme::pinnedBackHit(gestureStartX, gestureStartY, tft.width(), tft.height())) {
                    gestureActive = false;
                    lastTouch = now;
                    enterSettings();
                    uiSettingsOpenAppearance(true);
                    break;
                }
                if (!gestureMoved && now - gestureDownMs <= TAP_MAX_MS) {
                    lastTouch = now;
                    LightRow hit = uiLightHitTest(*canvas, gestureStartX, gestureStartY,
                                                  tft.width(), tft.height());
                    switch (hit) {
                        case LightRow::ENABLED:    Settings::toggleLight(); break;
                        case LightRow::ALERTS:     Settings::toggleLightAlerts(); break;
                        case LightRow::MESSAGES:   Settings::toggleLightMessages(); break;
                        case LightRow::IDLE:       Settings::cycleLightIdle(); break;
                        case LightRow::IDLE_COLOR: Settings::cycleLightColor(); break;
                        case LightRow::BRIGHTNESS: Settings::cycleLightBrightness(); break;
                        case LightRow::TEST:       StatusLight::test(now); break;
                        default: break;
                    }
                }
                gestureActive = false;
            }
            break;
        }
        case AppState::DESK: {
            Settings::deskActive(true);
            // The same late news as on the main screen (see CLEAR). An alert
            // already takes the desk over a focus block, so this may too, and
            // LATER comes back here: deskActive stays set through the window.
            if (now - transitionStart > 1500 && OtaCore::takeAvailableNotice()) {
                enterSysProps();
                break;
            }
            // The same test CLEAR makes, but the answer is a small card
            // beside the clock, and Squachy's reaction, not a new screen.
            {
                const Detection* latest = engine.latest();
                if (latest && (now - latest->firstSeen) < 200 &&
                    latest->conf >= Settings::minConfidence() && !IgnoreList::silenced(latest->mac) &&
                    alertMayInterrupt(*latest)) {
                    uiDeskAlert(*latest, now);
                    lastAlertType = latest->type;
                    squachyCatch(latest->type, latest->mac, latest->hits, latest->rssi, latest->conf);
                }
            }
            // The toast goes inside: anything drawn after the bands are
            // pushed would land straight on the panel again, over the top of
            // what was just sent, and flicker on its own.
            drawTwoBand([&](TFT_eSPI& t, bool advance) {
                uiDeskTick(t, now, engine, advance);
                Theme::drawToast(t, now);
            });
            // A fresh press only. A finger still down from the screen before
            // -- LATER on the update window opens the desk under it -- used to
            // land on BACK or the timer the moment the debounce ran out.
            if (touchJustDown && tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                // The same edge slivers CLEAR uses, below the title bar and
                // above the buttons: left edge back, right edge forward.
                const int ez = tft.width() / 10;
                const bool edge = (tp.x < ez || tp.x >= tft.width() - ez) && tp.y >= 16 &&
                                  tp.y < Theme::computeButtonBar(tft.width(), tft.height()).y;
                if (uiDeskHitMessage(tp.x, tp.y)) {
                    lastTouch = now;
                } else if (uiDeskHitAlert(tp.x, tp.y, now)) {
                    lastTouch = now;
                    uiAlertSetRedacted(false);
                    enterAlert(*uiDeskAlertDetection());
                } else if (uiDeskHitBack(tp.x, tp.y, tft.width(), tft.height())) {
                    lastTouch = now;
                    enterClear();   // BACK means the main screen, not the settings it came through
                } else if (uiDeskHitSettings(tp.x, tp.y, tft.width(), tft.height())) {
                    // The desk's own page in Settings, and back to the desk on
                    // the way out. (The title bar's icon opens the main list.)
                    lastTouch = now;
                    s_backToDesk = true;
                    enterSettings();
                    uiSettingsOpenPage(SettingsPage::DESK);
                } else if (uiDeskHitTimer(tp.x, tp.y, tft.width(), tft.height())) {
                    lastTouch = now;
                    uiDeskTapTimer(now);
                } else if (touchJustDown && uiDeskHitClockEdge(tp.x, tp.y) != 0) {
                    // The clock's own background turns over the same way the
                    // scene's does: right fifth forward, left fifth back.
                    lastTouch = now;
                    if (uiDeskHitClockEdge(tp.x, tp.y) > 0) Settings::cycleClockBackdrop();
                    else                                    Settings::cyclePrevClockBackdrop();
                    Theme::showToast(Settings::clockBackdropName(), "CLOCK BG", Theme::CYAN);
                } else if (edge && touchJustDown && !Settings::backgroundLocked()) {
                    lastTouch = now;
                    if (tp.x < ez) Settings::cyclePrevDeskBackground();
                    else           Settings::cycleDeskBackground();
                    Theme::showToast(Settings::backgroundName(Settings::background()), "DESK BACKGROUND", Theme::CYAN);
                }
            }
            break;
        }
        case AppState::DIAGNOSTICS: {
            DiagnosticsInfo info;
            {
                int16_t a = 0, b = 0;
                info.hasRaw = true;
                info.rawTouching = readTouchRaw(a, b);
                info.rawA = a;
                info.rawB = b;
                info.usingSavedCal = s_calSource == CalSource::SAVED;
                info.calSource = s_calSource == CalSource::SAVED     ? "saved"
                               : s_calSource == CalSource::OLD_SAVED ? "older firmware's"
                                                                     : "compiled-in default";
                // What the panel's top-left and bottom-right corners read
                // as, raw -- the old min/max pairs, for a Fit.
                float a0 = 0, b0 = 0, a1 = 0, b1 = 0;
                TouchFit::toRaw(s_touchFit, 0, 0, a0, b0);
                TouchFit::toRaw(s_touchFit, s_touchFit.w0, s_touchFit.h0, a1, b1);
                info.calA0 = (int16_t)lroundf(a0); info.calA1 = (int16_t)lroundf(a1);
                info.calB0 = (int16_t)lroundf(b0); info.calB1 = (int16_t)lroundf(b1);
            }
            info.usingCapTouch = usingCapTouch;
#if defined(CROWPANEL7)
            info.boardName = "CrowPanel 7";
#elif defined(TOUCH_ON_DISPLAY_BUS)
            info.boardName = "AWOK";
#elif defined(CYD35)
            info.boardName = "cyd35 BETA";
#else
            info.boardName = "cyd";
#endif
            info.touchValid = tp.valid;
            info.mappedX = tp.x;
            info.mappedY = tp.y;
            info.crash   = g_lastCrash;
            info.pushUs  = s_pushUsAvg;
            info.frameUs = s_frameUsAvg;
            info.bgUs    = Theme::backgroundUs();
            info.lastScreenName = s_lastScreenName;
            info.lastScreenUs   = s_lastScreenUs;
            info.freeHeap = ESP.getFreeHeap();
            info.largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            info.resetReason = resetReasonName();
            info.loopFree    = s_loopHeapFree;
            info.loopLargest = s_loopHeapLargest;
            info.otaSlot  = OtaCore::runningSlot();
            info.otaOther = OtaCore::otherVersion();
            info.bbReady   = BlackBox::ready();
            info.bbKept    = BlackBox::detectionsKept();
            info.bbCrashes = BlackBox::crashesKept();
            info.bbHaveLast = BlackBox::lastCrash(info.bbLast);

            drawTwoBand([&](TFT_eSPI& t, bool) { uiDiagnosticsTick(t, now, engine, info); });
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS &&
                uiDiagnosticsHitBack(tp.x, tp.y, tft.width(), tft.height())) {
                lastTouch = now;
                enterSettings();
            }
            break;
        }
        case AppState::HUNT: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiHuntTick(t, now, engine, advance); });
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                if (uiHuntHitStop(tp.x, tp.y, tft.width(), tft.height())) {
                    lastTouch = now;
                    engine.clearHunt();
                    Theme::showToast("HUNT STOPPED", nullptr, Theme::CYAN);
                    enterClear();
                } else if (uiHuntHitBack(tp.x, tp.y, tft.width(), tft.height())) {
                    lastTouch = now;
                    enterClear();
                }
            }
            break;
        }
        case AppState::COLOR_CHECK: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiColorCheckTick(t, now); });
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                ColorCheckTap ctap = uiColorCheckHitTest(tp.x, tp.y, tft.width(), tft.height());
                if (ctap == ColorCheckTap::INVERT) {
                    lastTouch = now;
                    Settings::toggleInvert();
                    // XOR against the panel's own baseline, not an
                    // absolute call -- see PANEL_NEEDS_INVERSION.
                    tft.invertDisplay(PANEL_NEEDS_INVERSION != Settings::inverted());
                } else if (ctap == ColorCheckTap::ORDER) {
                    lastTouch = now;
                    Settings::toggleRgbSwap();
                    applyColorOrder();
                } else if (ctap == ColorCheckTap::DONE) {
                    lastTouch = now;
                    Settings::markColorChecked();
                    if (s_colorCheckFromSettings) enterSettings();
                    else                           enterClear();
                }
            }
            break;
        }
        case AppState::DIARY: {
            drawTwoBand([&](TFT_eSPI& t, bool) { uiDiaryTick(t, now, engine); });
            // Simple read-only info panel — any tap takes you back,
            // no button bar or scroll needed.
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                enterClear();
            }
            break;
        }
        case AppState::OUTFIT: {
            drawTwoBand([&](TFT_eSPI& t, bool advance) { uiOutfitTick(t, now, engine, advance); });
            // Arrow taps cycle the equipped outfit (already persisted
            // live, no separate "confirm" step needed); a tap anywhere
            // else jumps straight back to the main screen, same "tap to
            // dismiss" feel as the Diary screen — the settings icon
            // (handled by the global back-navigation check above, which
            // runs before this switch and already changes `state`
            // itself when it fires) remains the way back to SETTINGS
            // specifically.
            if (tp.valid && (now - lastTouch) > TOUCH_DEBOUNCE_MS) {
                lastTouch = now;
                if (!uiOutfitTapArrow(tp.x, tp.y, tft.width(), tft.height())) {
                    enterClear();
                }
            }
            break;
        }
    }

#if !defined(CYD35)
    // Skipped on cyd35: this effect reads back already-drawn pixels to
    // shift them sideways, which is instant/reliable against the sprite
    // in RAM but would mean a live SPI readback from the panel itself
    // with no sprite buffer -- pixel readback (MISO) is a known-flaky
    // path on cheap SPI panels, not worth the risk for a 220ms cosmetic
    // transition effect.
    //
    // frameBufferOk guards both: if a rotate ever failed to reallocate
    // `frame` (see the rotate handler above), canvas already points
    // straight at tft and every draw this frame already landed on the
    // real screen -- pushing `frame` here would just paint stale data
    // from the sprite we stopped using back over the top of it.
    if (frameBufferOk) {
        if (now - transitionStart < TRANSITION_MS) {
            Theme::drawTransitionGlitch(frame, now - transitionStart, TRANSITION_MS);
        }
        FrameProf::lap(FrameProf::POST);
        pushFrame(0, 0);
        FrameProf::lap(FrameProf::PUSH);
    }
#endif

    s_pushUsAvg  = emaUpdate(s_pushUsAvg, s_pushAccumUs);
    const uint32_t frameUs = micros() - frameStartUs;
    s_frameUsAvg = emaUpdate(s_frameUsAvg, frameUs);
    FrameProf::endFrame();
    if (const char* nm = timedScreenName(state)) {
        if (now - transitionStart >= TRANSITION_MS) {
            if (s_lastScreenAt != transitionStart) {
                s_lastScreenAt   = transitionStart;
                s_lastScreenName = nm;
                s_lastScreenUs   = 0;
            }
            s_lastScreenUs = emaUpdate(s_lastScreenUs, frameUs);
        }
    }
    // The same two numbers DIAGNOSTICS shows, once every ten seconds on
    // serial, so a frame-rate change can be read off a capture rather than
    // off a screen somebody has to navigate to and photograph. Ten seconds
    // is the [scan] restart cadence; the log stays readable.
    {
        static uint32_t lastFrameSay = 0;
        if (s_frameUsAvg && now - lastFrameSay >= 10000) {
            lastFrameSay = now;
            const uint32_t loopsPerS = s_loopsSinceSay / 10;
            s_loopsSinceSay = 0;
            FrameProf::print();
#if defined(CYD35)
            // Measurement, not a feature: where the 3.5"'s frame really goes.
            Serial.printf("[bands] draw %lu / %lu us   hash %lu us   wire %lu us   rows %ld\n",
                          (unsigned long)s_bandUs[0], (unsigned long)s_bandUs[1],
                          (unsigned long)FramePush::hashUs(), (unsigned long)FramePush::wireUs(),
                          (long)SQW_PUSH_ROWS());
#endif
            const volatile uint32_t* ak = advertKinds();
            Serial.printf("[frame] avg %lu.%lu ms (%lu fps, loop %lu/s)  push %lu.%lu ms (%ld rows)  screen %u  bg %u  heap %lu/%lu  wifi %lu  ble %lu/s  adv %lu  kinds %lu/%lu/%lu/%lu/%lu  det %lu\n",
                          (unsigned long)(s_frameUsAvg / 1000), (unsigned long)((s_frameUsAvg / 100) % 10),
                          (unsigned long)(1000000UL / s_frameUsAvg), (unsigned long)loopsPerS,
                          (unsigned long)(s_pushUsAvg / 1000), (unsigned long)((s_pushUsAvg / 100) % 10), (long)SQW_PUSH_ROWS(),
                          (unsigned)state, (unsigned)Settings::background(), (unsigned long)ESP.getFreeHeap(),
                          (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                          (unsigned long)wifiFramesSeen(), (unsigned long)advertRate(), (unsigned long)advertsSeen(),
                          (unsigned long)ak[0], (unsigned long)ak[1], (unsigned long)ak[2], (unsigned long)ak[3], (unsigned long)ak[4],
                          (unsigned long)engine.lifetimeTotal());
        }
    }
#if CROWD_BENCH
    CrowdBench::noteFrame(micros() - frameStartUs, s_pushAccumUs);
#endif

    // ---- power saver ------------------------------------------------------
    // Both timers hang off lastTouch, which every screen already maintains.
    // They are separate settings because they are very different impositions:
    // slowing the animation down is barely noticeable, and blanking the screen
    // is not, so most people will want them on different clocks.
    {
        const uint32_t idleMs = now - lastTouch;

        // An alert has to be visible. A detector that dims itself and then
        // hides the thing it just found is worse than one with no saver at all.
        const bool alerting = (state == AppState::ALERT || state == AppState::WATCH_ALERT);
        const uint16_t timeoutSec = Settings::screenTimeoutSec();
        // Desk mode is a clock; a clock that goes dark is not there.
        bool wantDim = timeoutSec && idleMs > (uint32_t)timeoutSec * 1000UL &&
                       !(Settings::wakeOnAlert() && alerting) && state != AppState::DESK;
#if defined(TWATCH_S3)
        // On the cable the watch stays lit; on battery the timeout always runs
        // (see Settings::screenTimeoutSec), unless it is set to NEVER.
        if (s_onUsb) wantDim = false;
        // The crown, which beats the cable, the desk and a timeout of NEVER.
        // A touch since the press, or an alert that wants the screen, ends it.
        //
        // An alert lights it without ending it: once the alert is over the
        // screen goes dark again after the screen timeout (30 s when that is
        // NEVER), the way it would on battery. Taps on the alert card -- the
        // one that dismisses it above all -- do not count as the touch that
        // ends crown mode; a touch after that does.
        {
            static bool wasAlerting = false;
            if (s_crownDark) {
                if (alerting || wasAlerting) s_crownDarkAt = now;
                if (wasAlerting && !alerting) {
                    uint32_t t = Settings::screenTimeoutSecRaw();
                    if (!t) t = 30;
                    s_crownLitUntil = now + t * 1000UL;
                    if (!s_crownLitUntil) s_crownLitUntil = 1;
                }
                if ((int32_t)(lastTouch - s_crownDarkAt) > 0) s_crownDark = false;
            }
            wasAlerting = alerting;
        }
        if (s_crownDark) {
            const bool alertWants = Settings::wakeOnAlert() && alerting;
            const bool lit = s_crownLitUntil && (int32_t)(s_crownLitUntil - now) > 0;
            wantDim = !alertWants && !lit;
        }
#endif
        if (wantDim != s_screenDimmed) {
            s_screenDimmed = wantDim;
            applyBrightness();
        }

        // Auto-lock, on the saver's idle clock: after N idle minutes, or the
        // moment the saver dims the screen. Never out of the boot, a PIN being
        // typed, or an alert that is still up.
        if (Security::enabled() && !Security::locked() &&
            state != AppState::BOOT && state != AppState::COLOR_CHECK && state != AppState::PIN_ENTRY &&
            state != AppState::ALERT && state != AppState::WATCH_ALERT) {
            const uint32_t lockMs = Security::autoLockIdleMs();
            if ((lockMs && idleMs >= lockMs) || (Security::autoLockOnSleep() && s_screenDimmed)) {
                if (onRawScanScreen()) engine.stopRawScan();
                Security::lock();
                enterLocked();
            }
        }

        // Hold the loop to the chosen rate once idle. delay() hands the core
        // to the RTOS idle task, which parks it in WAITI -- a real saving
        // because the clock gates, though not the same order as a true light
        // sleep, which this firmware cannot take while the radio is scanning.
        //
        // Note which way this points: the slack being given back only exists
        // because the panel push is fast. On the 40 MHz build there is no idle
        // time left to hand over, so the faster SPI clock is what makes this
        // saving possible rather than something to trade against it.
#if defined(TWATCH_S3)
        // Asleep, the loop only needs to hear the radios and feel a tap:
        // ten passes a second is plenty, and the rest of the time is idle.
        if (s_panelAsleep) {
            const uint32_t spentUs = micros() - frameStartUs;
            if (spentUs < 100000UL) delay((100000UL - spentUs) / 1000UL);
        }
#endif
        const uint8_t fps = Settings::idleFps();
        if (fps && idleMs > (uint32_t)Settings::idleAfterSec() * 1000UL) {
            const uint32_t budgetUs = 1000000UL / fps;
            const uint32_t spentUs  = micros() - frameStartUs;
            if (spentUs < budgetUs) delay((budgetUs - spentUs) / 1000UL);
        }
    }

    // The light on the back reads the state machine rather than being told
    // about transitions, so there is no exit path it can miss.
    {
        StatusLight::Context lc;
        lc.alert      = (state == AppState::ALERT);
        lc.alertColor = Theme::colorFor(lastAlertType);
        // A fox caught on the HUNT gauge flashes the light green, the same
        // three flashes a detection gets in its own colour.
        if (state == AppState::HUNT && uiHuntCaught()) { lc.alert = true; lc.alertColor = Theme::GREEN; }
        if (state == AppState::DESK && uiDeskChime(now)) { lc.alert = true; lc.alertColor = Theme::GREEN; }
        if (state == AppState::DESK && uiDeskAlertUp(now)) { lc.alert = true; lc.alertColor = Theme::colorFor(uiDeskAlertDetection()->type); }
#if SQUACH_MESH
        lc.unread     = MeshTalk::inbox().unread;
        lc.visiting   = Squachy::visiting();
#else
        lc.unread     = false;
        lc.visiting   = false;
#endif
        lc.update = 0;
        {
            const OtaBle::State  b = OtaBle::state();
            const OtaWifi::State w = OtaWifi::state();
            if (b == OtaBle::State::DONE || w == OtaWifi::State::DONE) lc.update = 2;
            else if (b == OtaBle::State::FAILED || w == OtaWifi::State::FAILED) lc.update = 3;
            else if (b == OtaBle::State::RECEIVING || b == OtaBle::State::VERIFYING ||
                     w == OtaWifi::State::CONNECTING || w == OtaWifi::State::CHECKING ||
                     w == OtaWifi::State::DOWNLOADING || w == OtaWifi::State::VERIFYING) lc.update = 1;
        }
        lc.quiet        = (state == AppState::LOCKED || state == AppState::PIN_ENTRY);
        lc.screenDimmed = s_screenDimmed;
        lc.screenDark   = s_screenDimmed && Settings::dimLevel() == 0;
        StatusLight::tick(now, lc);
    }
    prevTouchValid = tp.valid;
}
