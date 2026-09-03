// SquachWatch-CYD PC emulator — render harness.
//
// Builds the project's real UI code (theme.cpp, squachy.cpp, ui_*.cpp)
// natively and renders a chosen screen to a PNG, so layout and sizing
// work doesn't need a 45-second build + flash + squint-at-the-device
// cycle.
//
//   ./squachsim clear out.png
//   ./squachsim alert out.png --portrait
//   ./squachsim clear out.png --bg 8 --frames 120
//   ./squachsim clear out.png --onboard
//
// What this is NOT: the detection engine behind it is a stand-in with
// no radios (see detection_sim.cpp), so this shows how screens render,
// not whether the real matching logic works.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <TFT_eSPI.h>
#include "theme.h"
#include "settings.h"
#include "squachy.h"
#include "detection.h"
#include "detection_info.h"
#include "ui_clear.h"
#include "ui_log.h"
#include "ui_alert.h"
#include "ui_settings.h"
#include "ui_diary.h"
#include "ui_hunt.h"
#include "ui_rawscan.h"
#include "ui_watchalert.h"
#include "ui_colorcheck.h"
#include "ui_boot.h"
#include "png_writer.h"

// A few plausible log entries so screens have something real to draw --
// counters, LOG rows, an ALERT target. Seeded through the engine's own
// public postBle() so it goes through the same pushLog() path the real
// firmware uses.
static void seedDetections(DetectionEngine& eng) {
    struct Seed { DetectionType type; const char* vendor; const char* name; int8_t rssi; uint16_t hits; };
    static const Seed seeds[] = {
        { DetectionType::AIRTAG,  "Apple",   "AirTag",        -42, 7 },
        { DetectionType::FLOCK,   "Flock",   "Flock Safety",  -68, 3 },
        { DetectionType::RING,    "Amazon",  "Ring Doorbell", -55, 2 },
        { DetectionType::META,    "Meta",    "Ray-Ban Meta",  -73, 1 },
        { DetectionType::TILE,    "Tile",    "Tile Mate",     -81, 4 },
    };
    uint32_t now = millis();
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        Detection d{};
        for (int b = 0; b < 6; b++) d.mac[b] = (uint8_t)(0x10 * (i + 1) + b);
        d.rssi      = seeds[i].rssi;
        d.channel   = (uint8_t)(1 + i);
        d.type      = seeds[i].type;
        snprintf(d.vendor, sizeof(d.vendor), "%s", seeds[i].vendor);
        snprintf(d.name,   sizeof(d.name),   "%s", seeds[i].name);
        d.firstSeen = now - 30000 - (uint32_t)i * 5000;
        d.lastSeen  = now - (uint32_t)i * 1200;
        d.hits      = seeds[i].hits;
        d.active    = true;
        eng.postBle(d);
    }
}

// RGB565 -> RGB888, replicating each channel's high bits down into the
// low ones so full-scale stays full-scale (0x1F -> 0xFF, not 0xF8)
// instead of every render coming out slightly dim.
static std::vector<uint8_t> toRgb888(const std::vector<uint16_t>& src) {
    std::vector<uint8_t> out;
    out.reserve(src.size() * 3);
    for (uint16_t v : src) {
        uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        out.push_back((uint8_t)((r << 3) | (r >> 2)));
        out.push_back((uint8_t)((g << 2) | (g >> 4)));
        out.push_back((uint8_t)((b << 3) | (b >> 2)));
    }
    return out;
}

static void usage() {
    fprintf(stderr,
        "usage: squachsim <screen> [out.png] [options]\n"
        "  screens: clear log alert settings diary hunt rawscan watchalert colorcheck diagnostics boot\n"
        "  --portrait        render 240x320 instead of 320x240\n"
        "  --bg N            background style 0..9 (see Settings::Background)\n"
        "  --theme N         palette index\n"
        "  --frames N        animation warm-up frames before capture (default 90)\n"
        "  --onboard         let Squachy's first-boot walkthrough run\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    std::string screen  = argv[1];
    std::string outPath = (argc > 2 && argv[2][0] != '-') ? argv[2] : "squachsim.png";

    bool portrait = false, onboard = false;
    int bg = -1, themeIdx = -1, frames = 90;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--portrait") portrait = true;
        else if (a == "--onboard") onboard = true;
        else if (a == "--bg" && i + 1 < argc) bg = atoi(argv[++i]);
        else if (a == "--theme" && i + 1 < argc) themeIdx = atoi(argv[++i]);
        else if (a == "--frames" && i + 1 < argc) frames = atoi(argv[++i]);
    }

    const int W = portrait ? 240 : 320;
    const int H = portrait ? 320 : 240;

    TFT_eSPI tft(W, H);
    tft.init();
    tft.setRotation(portrait ? 0 : 1);

    TFT_eSprite frame(&tft);
    frame.createSprite(W, H);

    Settings::load();
    // Settings' own cycle* mutators are the only public way in, so walk
    // them to the requested index rather than reaching past the API.
    if (themeIdx >= 0) while ((int)Settings::paletteIndex() != themeIdx % (int)Theme::PALETTE_COUNT) Settings::cyclePalette();
    if (bg >= 0) while ((int)Settings::background() != bg % (int)Settings::BACKGROUND_COUNT) Settings::cycleBackground();

    DetectionEngine engine;
    engine.init();
    seedDetections(engine);

    if (onboard) Squachy::trigger(Squachy::Event::BOOTED);

    // Backgrounds (matrix rain, starfield, aquarium, fire...) and
    // Squachy's idle animation all build state across frames -- a single
    // tick renders a half-empty scene that looks nothing like the real
    // device. Warm up by ticking with advancing time, then capture the
    // last frame.
    const uint32_t STEP_MS = 33;   // ~30fps, close to the device's real loop rate
    uint32_t now = millis();

    auto tick = [&](uint32_t t) {
        if      (screen == "clear")    uiClearTick(frame, t, engine, true, false);
        else if (screen == "log")      uiLogTick(frame, t, engine, 0, false, "", false, nullptr, "");
        else if (screen == "alert")    uiAlertTick(frame, t, false, nullptr, "");
        else if (screen == "settings") uiSettingsTick(frame, t, engine);
        else if (screen == "diary")    uiDiaryTick(frame, t, engine);
        else if (screen == "hunt")     uiHuntTick(frame, t, engine);
        else if (screen == "rawscan")  uiRawScanTick(frame, t, engine, true, true, false, "");
        else if (screen == "watchalert") uiWatchAlertTick(frame, t, engine, true);
        else if (screen == "colorcheck") uiColorCheckTick(frame, t);
        else if (screen == "boot")     uiBootTick(frame, t);
        else return false;
        return true;
    };

    // Per-screen init, where the screen has one.
    if      (screen == "clear")      uiClearInit(frame);
    else if (screen == "log")        uiLogInit(frame);
    else if (screen == "settings")   uiSettingsInit(frame);
    else if (screen == "rawscan")    uiRawScanInit(frame, true);
    else if (screen == "watchalert") uiWatchAlertInit(frame);
    else if (screen == "colorcheck") uiColorCheckInit(frame);
    else if (screen == "boot")       uiBootInit(frame);
    else if (screen == "hunt")       { engine.huntBle((const uint8_t*)"\x11\x22\x33\x44\x55\x66", "AirTag"); }
    else if (screen == "alert")      {
        const Detection* d = engine.logAt(0);
        if (!d) { fprintf(stderr, "no seeded detection to alert on\n"); return 1; }
        uiAlertInit(frame, *d);
    }

    for (int i = 0; i < frames; i++) {
        if (!tick(now + (uint32_t)i * STEP_MS)) { usage(); return 2; }
    }

    frame.pushSprite(0, 0);

    std::vector<uint8_t> rgb = toRgb888(tft.pixelsRGB565());
    if (!PngWriter::write(outPath.c_str(), W, H, rgb.data())) {
        fprintf(stderr, "failed to write %s\n", outPath.c_str());
        return 1;
    }
    printf("rendered '%s' -> %s (%dx%d, %d warm-up frames)\n",
           screen.c_str(), outPath.c_str(), W, H, frames);
    return 0;
}
