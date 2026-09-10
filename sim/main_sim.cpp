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
#include "squachmesh.h"
#include "ui_phone.h"
#include "ui_meshmenu.h"
#include "ui_watchalert.h"
#include "ui_colorcheck.h"
#include "ui_diagnostics.h"
#include "ui_boot.h"
#include "ui_detfilter.h"
#include "ui_power.h"
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
        // The name is what the detector writes for a real one: six hex of
        // the proximity UUID, then major.minor.
        { DetectionType::IBEACON, "iBeacon", "B9407F 10.42",  -59, 9 },
        { DetectionType::HACKER,  "Flipper", "Flipper Ozzyx", -49, 6 },
        // Named after the aircraft rather than after a service UUID, which
        // is what the decoder buys. See the Remote ID seed below.
        { DetectionType::DRONE,   "DroneID", "SIMDRONE-0001", -71, 2 },
    };
    uint32_t now = millis();
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        Detection d{};
        for (int b = 0; b < 6; b++) d.mac[b] = (uint8_t)(0x10 * (i + 1) + b);
        d.rssi      = seeds[i].rssi;
        d.channel   = (uint8_t)(1 + i);
        d.type      = seeds[i].type;
        d.conf      = confidenceFor(seeds[i].type);
        snprintf(d.vendor, sizeof(d.vendor), "%s", seeds[i].vendor);
        snprintf(d.name,   sizeof(d.name),   "%s", seeds[i].name);
        d.firstSeen = now - 30000 - (uint32_t)i * 5000;
        d.lastSeen  = now - (uint32_t)i * 1200;
        d.hits      = seeds[i].hits;
        d.active    = true;
        eng.postBle(d);
    }

    // Give the drone an actual Remote ID broadcast to have decoded.
    //
    // Built as three real ASTM F3411 adverts and pushed through the engine's
    // own mergeRemoteId(), which in this build runs the REAL decoder -- so
    // the info panel the emulator renders is showing genuinely decoded
    // values, and a mistake in the decoder shows up here rather than only in
    // a field with a drone overhead.
    {
        auto put32 = [](uint8_t* p, int32_t v) {
            p[0] = (uint8_t)(v & 0xFF);         p[1] = (uint8_t)((v >> 8) & 0xFF);
            p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
        };
        uint8_t msg[3][25];
        memset(msg, 0, sizeof(msg));
        msg[0][0] = (0x0 << 4) | 0x2;          // Basic ID
        msg[0][1] = (0x1 << 4) | 0x2;          // serial, multirotor
        memcpy(msg[0] + 2, "SIMDRONE-0001       ", 20);
        msg[1][0] = (0x1 << 4) | 0x2;          // Location
        put32(msg[1] + 5,  407128000);
        put32(msg[1] + 9, -740060000);
        { const uint16_t alt = (uint16_t)((120 + 1000) * 2);
          msg[1][15] = (uint8_t)(alt & 0xFF); msg[1][16] = (uint8_t)(alt >> 8); }
        msg[2][0] = (0x4 << 4) | 0x2;          // System: the operator
        put32(msg[2] + 2,  407580000);
        put32(msg[2] + 6, -739855000);

        const uint8_t mac[6] = { 0x02, 0xFF, 0xFA, 0xA0, 0x01, 0x5D };
        for (int i = 0; i < 3; i++) {
            uint8_t adv[32];
            adv[0] = 1 + 2 + 27;               // AD type + UUID + body
            adv[1] = 0x16;                     // service data, 16-bit UUID
            adv[2] = 0xFA; adv[3] = 0xFF;      // 0xFFFA, little endian
            adv[4] = 0x0D;                     // ODID application code
            adv[5] = (uint8_t)(i + 1);         // message counter
            memcpy(adv + 6, msg[i], 25);
            eng.mergeRemoteId(mac, adv, (uint8_t)(1 + adv[0]));
        }
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
        "  screens: clear log alert settings detfilter power diary hunt rawscan watchalert colorcheck boot phone\n"
        "  --portrait        render 240x320 instead of 320x240\n"
        "  --bg N            background style 0..9 (see Settings::Background)\n"
        "  --theme N         palette index\n"
        "  --alert N         DetectionType the ALERT screen fires on\n"
        "  --noseed          no detections at all -- CLEAR's idle state\n"
        "  --frames N        animation warm-up frames before capture (default 90)\n"
        "  --onboard         let Squachy's first-boot walkthrough run\n"
        "  --sequence N      capture N consecutive frames instead of one\n"
        "  --raw PATH        write raw RGB888 frames to PATH instead of PNGs --\n"
        "                    what the GUI consumes, no encode/decode on either side\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    std::string screen  = argv[1];
    std::string outPath = (argc > 2 && argv[2][0] != '-') ? argv[2] : "squachsim.png";

    bool portrait = false, onboard = false, showoff = false;
    int confirmRow = -1;   // settings screen: put a confirm panel up
    int scrollBy = 0;      // settings screen: scroll down N rows first
    int bg = -1, themeIdx = -1, frames = 90, sequence = 1, outfitIdx = -1;
    // --info N renders LOG's MORE INFO panel for DetectionType N. The panel
    // is a real layout with real wrapped text and it was previously only
    // reachable on hardware, which is how two of its paragraphs went stale.
    int infoType = -1;
    // --alert N picks WHICH seeded detection the ALERT screen fires on,
    // by DetectionType. Without it the screen always alerted on logAt(0)
    // -- whatever was seeded last -- so every alert frame ever rendered
    // was a DRONE, and the other types' headlines, colours and
    // confidence rows were never looked at.
    int alertType = -1;
    // --noseed leaves the engine empty. The CLEAR screen has two states
    // now -- the headline only draws when something is actually live --
    // and with detections always seeded the emulator could not render
    // the idle one at all. Same gap --alert filled from the other side.
    bool noSeed = false;
    // SPIKE: --peer N draws a visiting Squachy in outfit N beside our own,
    // both at SMALL. No radio involved -- the point is to find out whether
    // two of him fit and whether the renderer survives being called twice.
    int peerOutfit = -1;
    // --type feeds the payphone a tap sequence: digits are key presses,
    // "." waits past the multi-tap window. Typing is the feature; a screen
    // that only renders proves nothing about it.
    std::string typeSeq;
    std::string rawPath;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--portrait") portrait = true;
        else if (a == "--onboard") onboard = true;
        else if (a == "--bg" && i + 1 < argc) bg = atoi(argv[++i]);
        else if (a == "--theme" && i + 1 < argc) themeIdx = atoi(argv[++i]);
        else if (a == "--outfit" && i + 1 < argc) outfitIdx = atoi(argv[++i]);
        else if (a == "--frames" && i + 1 < argc) frames = atoi(argv[++i]);
        else if (a == "--sequence" && i + 1 < argc) sequence = atoi(argv[++i]);
        else if (a == "--raw" && i + 1 < argc) rawPath = argv[++i];
        else if (a == "--showoff") showoff = true;
        else if (a == "--confirm" && i + 1 < argc) confirmRow = atoi(argv[++i]);
        else if (a == "--info" && i + 1 < argc) infoType = atoi(argv[++i]);
        else if (a == "--alert" && i + 1 < argc) alertType = atoi(argv[++i]);
        else if (a == "--noseed") noSeed = true;
        else if (a == "--peer" && i + 1 < argc) peerOutfit = atoi(argv[++i]);
        else if (a == "--type" && i + 1 < argc) typeSeq = argv[++i];
        else if (a == "--scroll" && i + 1 < argc) scrollBy = atoi(argv[++i]);
    }
    if (sequence < 1) sequence = 1;

    const int W = portrait ? 240 : 320;
    const int H = portrait ? 320 : 240;

    TFT_eSPI tft(W, H);
    tft.init();
    tft.setRotation(portrait ? 0 : 1);

    TFT_eSprite frame(&tft);
    // The firmware calls setColorDepth(8) before createSprite; match it
    // or the preview shows gradients the panel cannot produce.
    frame.setColorDepth(8);
    frame.createSprite(W, H);

    Settings::load();
    // Settings' own cycle* mutators are the only public way in, so walk
    // them to the requested index rather than reaching past the API.
    if (themeIdx >= 0) while ((int)Settings::paletteIndex() != themeIdx % (int)Theme::PALETTE_COUNT) Settings::cyclePalette();
    if (bg >= 0) {
        const int want = bg % (int)Settings::BACKGROUND_COUNT;
        // BLACK only exists while BORING MODE is on, so asking for it here
        // means asking for that mode too -- which is also the honest render,
        // since it is the only way a real device can show this screen.
        if (want == (int)Settings::Background::BLACK && !Settings::boringMode())
            Settings::toggleBoringMode();
        // Bounded. This used to spin until it matched, which was fine while
        // every index was reachable and became an infinite loop the moment
        // one was not.
        for (int i = 0; i < (int)Settings::BACKGROUND_COUNT; i++) {
            if ((int)Settings::background() == want) break;
            Settings::cycleBackground();
        }
    }

    // Outfits are gated behind lifetime-detection thresholds, so unlock
    // the lot before walking to the one asked for -- same "use the
    // public mutators rather than reach past the API" approach the
    // theme/background options above take. A fresh Preferences starts
    // at index 0 (NONE), so N cycles lands on N.
    if (outfitIdx >= 0) {
        Squachy::unlockAllOutfits();
        for (int k = 0; k < outfitIdx; k++) Squachy::cycleOutfit();
    }

    SquachMesh::Peer guest{};
    if (peerOutfit >= 0) {
        guest.nick = 4; guest.outfit = (uint8_t)peerOutfit; guest.shade = 1;
        guest.custom = false; guest.name[0] = 0;
        uiClearSetGuest(&guest);
    }

    DetectionEngine engine;
    engine.init();
    if (!noSeed) seedDetections(engine);

    if (onboard) Squachy::trigger(Squachy::Event::BOOTED);
    // Runs every pose he has back to back, which is the only way to see
    // the whole set -- most are gated behind random idle rolls.
    if (showoff) Squachy::startShowOff();

    // Backgrounds (matrix rain, starfield, aquarium, fire...) and
    // Squachy's idle animation all build state across frames -- a single
    // tick renders a half-empty scene that looks nothing like the real
    // device. Warm up by ticking with advancing time, then capture the
    // last frame.
    const uint32_t STEP_MS = 33;   // ~30fps, close to the device's real loop rate
    uint32_t now = millis();

    auto tick = [&](uint32_t t) {
        if      (screen == "clear")    uiClearTick(frame, t, engine, true, false);
        else if (screen == "log") {
            const bool info = (infoType >= 0);
            const DetectionType it = info ? (DetectionType)infoType : DetectionType::UNKNOWN;
            uiLogTick(frame, t, engine, 0, false, "", info,
                      info ? detectionTypeName(it) : nullptr,
                      info ? DetectionInfo::explainLive(it, engine) : "");
        }
        else if (screen == "alert")    uiAlertTick(frame, t, engine, false, nullptr, "");
        else if (screen == "settings") uiSettingsTick(frame, t, engine);
        else if (screen == "detfilter") uiDetFilterTick(frame, t, engine);
        else if (screen == "power")    uiPowerTick(frame, t, engine);
        else if (screen == "diary")    uiDiaryTick(frame, t, engine);
        else if (screen == "hunt")     uiHuntTick(frame, t, engine);
        else if (screen == "rawscan")  uiRawScanTick(frame, t, engine, true, true, false, "");
        else if (screen == "phone")    uiPhoneTick(frame, t, engine);
        else if (screen == "meshmenu") uiMeshMenuTick(frame, t, engine);
        else if (screen == "watchalert") uiWatchAlertTick(frame, t, engine, true);
        else if (screen == "colorcheck") uiColorCheckTick(frame, t);
        else if (screen == "diagnostics") {
            // main.cpp fills this from board-specific globals the sim
            // has no equivalent for, so these are plausible stand-ins.
            // The frame timings are the real 40MHz arithmetic: 153,600
            // bytes at 16bpp is ~30.7ms of SPI, which is what makes
            // this screen worth rendering here at all -- it's how the
            // layout gets checked before the numbers mean anything.
            DiagnosticsInfo info{};
            info.hasRaw = true;
            info.rawTouching = false;
            info.rawA = 1820; info.rawB = 2140;
            info.touchValid = false;
            info.mappedX = 0; info.mappedY = 0;
            info.usingSavedCal = false;
            info.calA0 = 200; info.calA1 = 3800;
            info.calB0 = 200; info.calB1 = 3800;
            info.pushUs = 30700;
            info.frameUs = 41200;
            info.freeHeap = 180000;
            info.largestBlock = 110000;
            info.resetReason = "POWERON_RESET";
            info.boardName = "cyd";
            info.usingCapTouch = false;
            uiDiagnosticsTick(frame, t, engine, info);
        }
        else if (screen == "boot")     uiBootTick(frame, t);
        else return false;
        return true;
    };

    // Per-screen init, where the screen has one.
    if      (screen == "clear")      uiClearInit(frame);
    else if (screen == "log")        uiLogInit(frame);
    else if (screen == "settings")   uiSettingsInit(frame);
    else if (screen == "detfilter")  uiDetFilterInit(frame);
    else if (screen == "diary")      uiDiaryInit(frame);
    else if (screen == "rawscan")    uiRawScanInit(frame, true);
    else if (screen == "watchalert") uiWatchAlertInit(frame);
    else if (screen == "diagnostics") uiDiagnosticsInit(frame);
    else if (screen == "colorcheck") uiColorCheckInit(frame);
    else if (screen == "boot")       uiBootInit(frame);
    else if (screen == "meshmenu")   uiMeshMenuInit(frame);
    else if (screen == "phone")      {
        uiPhoneInit(frame);
        uint32_t tnow = now;
        // Key centres, computed the same way ui_phone.cpp lays them out.
        for (size_t i = 0; i < typeSeq.size(); i++) {
            if (typeSeq[i] == '.') { tnow += 900; continue; }
            const int k = typeSeq[i] - '0';
            if (k < 0 || k > 11) continue;
            const int kx = 78 + (164 - (48 * 3 + 3 * 2)) / 2 + (k % 3) * 51 + 24;
            const int ky = 8 + 60 + (k / 3) * 33 + 15;
            uiPhoneTouch(kx, ky, tnow);
            tnow += 120;
        }
    }
    else if (screen == "hunt")       { engine.huntBle((const uint8_t*)"\x11\x22\x33\x44\x55\x66", "AirTag"); }
    else if (screen == "alert")      {
        const Detection* d = nullptr;
        if (alertType >= 0) {
            for (uint16_t i = 0; i < engine.logCount(); i++) {
                const Detection* c = engine.logAt(i);
                if (c && (int)c->type == alertType) { d = c; break; }
            }
            if (!d) {
                fprintf(stderr, "no seeded detection of type %d -- see seedDetections()\n",
                        alertType);
                return 1;
            }
        } else {
            d = engine.logAt(0);
        }
        if (!d) { fprintf(stderr, "no seeded detection to alert on\n"); return 1; }
        uiAlertInit(frame, *d);
    }

    // After uiSettingsInit(), which clears any pending question.
    if (confirmRow >= 0) uiSettingsSetConfirm((SettingsRow)confirmRow);
    for (int k = 0; k < scrollBy; k++) uiSettingsScroll(1);

    for (int i = 0; i < frames; i++) {
        if (!tick(now + (uint32_t)i * STEP_MS)) { usage(); return 2; }
    }

    // Capture runs on from where the warm-up left off, so a sequence is
    // continuous motion rather than N restarts of the same instant. The
    // warm-up is the expensive part (~2ms/frame) and it's paid once, so
    // asking for 45 frames costs barely more than asking for one.
    FILE* rawOut = nullptr;
    if (!rawPath.empty()) {
        rawOut = fopen(rawPath.c_str(), "wb");
        if (!rawOut) { fprintf(stderr, "failed to open %s\n", rawPath.c_str()); return 1; }
    }

    for (int s = 0; s < sequence; s++) {
        tick(now + (uint32_t)(frames + s) * STEP_MS);
        frame.pushSprite(0, 0);
        std::vector<uint8_t> rgb = toRgb888(tft.pixelsRGB565());

        if (rawOut) {
            fwrite(rgb.data(), 1, rgb.size(), rawOut);
            continue;
        }
        // Multi-frame PNG output gets an index suffix; a single frame
        // keeps the exact path asked for.
        std::string path = outPath;
        if (sequence > 1) {
            std::string stem = outPath, ext = ".png";
            size_t dot = outPath.rfind('.');
            if (dot != std::string::npos) { stem = outPath.substr(0, dot); ext = outPath.substr(dot); }
            char buf[16];
            snprintf(buf, sizeof(buf), "_%04d", s);
            path = stem + buf + ext;
        }
        if (!PngWriter::write(path.c_str(), W, H, rgb.data())) {
            fprintf(stderr, "failed to write %s\n", path.c_str());
            if (rawOut) fclose(rawOut);
            return 1;
        }
    }

    if (rawOut) {
        fclose(rawOut);
        // The GUI reads geometry off this line rather than assuming.
        printf("raw %dx%d rgb888 frames=%d -> %s\n", W, H, sequence, rawPath.c_str());
    } else if (sequence > 1) {
        printf("rendered '%s' -> %d frames (%dx%d, %d warm-up frames)\n",
               screen.c_str(), sequence, W, H, frames);
    } else {
        printf("rendered '%s' -> %s (%dx%d, %d warm-up frames)\n",
               screen.c_str(), outPath.c_str(), W, H, frames);
    }
    return 0;
}
