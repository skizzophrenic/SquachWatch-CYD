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
#include "qwerty.h"
#include "ui_meshmenu.h"
#include "ui_bingo.h"
#include "ui_dex.h"
#include "dex.h"
#include "regulars.h"
#include "bingo.h"
#include "blackbox.h"
#include "ui_meshwarn.h"
#include "ui_meshphrase.h"
#include "ui_meshcompose.h"
#include "meshtalk.h"
#include "meshmsg.h"
#include "meshcrypto.h"
#include "ui_watchalert.h"
#include "ui_colorcheck.h"
#include "ui_diagnostics.h"
#include "ui_desk.h"
#include "ui_zone.h"
#include "clock.h"
#include "ui_boot.h"
#include "ui_detfilter.h"
#include "ui_power.h"
#include "ui_security.h"
#include "ui_ignorelist.h"
#include "ui_light.h"
#include "ui_nudge.h"
#include "ui_squadupdate.h"
#include "ui_squad.h"
#include "ui_invite.h"
#include "ui_update.h"
#include "ui_wifipass.h"
#include "ui_outfit_unlock.h"
#include "ui_sysprops.h"
#include "ota_core.h"
#include "ui_wifinets.h"
#include "png_writer.h"
#include "touch_cal.h"

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
        d.vendor = seeds[i].vendor;
        snprintf(d.name,   sizeof(d.name),   "%s", seeds[i].name);
        d.firstSeen = now - 30000 - (uint32_t)i * 5000;
        d.lastSeen  = now - (uint32_t)i * 1200;
        d.hits      = seeds[i].hits;
        d.active    = true;
        // Two of them stand in for rows the black box brought back from
        // an earlier boot: faded, with no closer/further arrow.
        d.restored  = (i >= 4) ? 1 : 0;
        d.active    = (i >= 4) ? false : true;
        d.prevRssi  = (int8_t)(d.rssi - (i % 3 == 0 ? 7 : (i % 3 == 1 ? -8 : 1)));
        eng.postBle(d);
        // The Ring is a regular: seen on three days, so the LOG names it.
        if (seeds[i].type == DetectionType::RING)
            for (uint32_t day = 20000; day < 20003; day++) Regulars::noteOnDay(d.mac, d.type, day);
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

// ---- touchcal: the real calibration flow, with a scripted finger ----
// TouchCal::runInteractive() is blocking and reads touch through a
// RawReader, so this hands it one that plays a finger: tap to start, hold
// each of the five targets for a second and a bit, tap the dot a few pixels
// off. The finger's raw numbers come from the compiled-in 2.8" mapping, run
// backwards, so the flow is solving for a map it can actually find.
//
// Frames are filmed on virtual time, one every 66 ms, from inside the reader
// and after every delay() -- the two things the flow ever waits on.
namespace SimCal {
    TFT_eSPI*     t = nullptr;
    int           W = 0, H = 0;
    uint8_t       rot = 1;
    TouchFit::Fit fit;
    FILE*         raw = nullptr;
    int           skip = 0, want = 1, got = 0;
    uint32_t      nextSnap = 0;
    std::vector<uint8_t> lastRgb;
    int           tap = 0;
    bool          down = false;
    uint32_t      phaseStart = 0;

    void snap() {
        while (SimClock::nowMs >= nextSnap && got < skip + want) {
            if (got >= skip) {
                lastRgb = toRgb888(t->pixelsRGB565());
                if (raw) fwrite(lastRgb.data(), 1, lastRgb.size(), raw);
            }
            got++;
            nextSnap += 66;
        }
    }

    // Where tap N lands, on screen: 0 starts, 1-5 are the targets (a pixel
    // or so off, as a finger is), 6 is the check dot, a little off too.
    void tapPos(int n, float& sx, float& sy) {
        if (n == 0) { sx = W * 0.5f; sy = H * 0.4f; return; }
        if (n <= TouchFit::TARGETS) {
            float fx, fy;
            TouchFit::targetFrac(n - 1, fx, fy);
            sx = fx * W + ((n & 1) ? 1.5f : -1.0f);
            sy = fy * H + ((n & 2) ? -1.0f : 1.5f);
            return;
        }
        sx = 0.62f * W + 3; sy = 0.32f * H - 2;
    }

    bool reader(int16_t& a, int16_t& b) {
        snap();
        const uint32_t now = SimClock::nowMs;
        const uint32_t pause = tap == 0 ? 900 : 550;
        const uint32_t hold  = tap == 0 ? 150 : tap > TouchFit::TARGETS ? 350 : 1150;
        if (!down && tap <= TouchFit::TARGETS + 1 && now - phaseStart >= pause) { down = true; phaseStart = now; }
        else if (down && now - phaseStart >= hold) { down = false; phaseStart = now; tap++; }
        if (!down) return false;
        const int w0 = (rot & 1) ? H : W, h0 = (rot & 1) ? W : H;
        float sx, sy, nx, ny, ra = 0, rb = 0;
        tapPos(tap, sx, sy);
        TouchFit::screenToNative(sx, sy, rot, w0, h0, nx, ny);
        TouchFit::toRaw(fit, nx, ny, ra, rb);
        a = (int16_t)lroundf(ra);
        b = (int16_t)lroundf(rb);
        return true;
    }
}

static int renderTouchCal(TFT_eSPI& tft, int W, int H, int skip, int want,
                          const std::string& rawPath, const std::string& outPath) {
    using namespace SimCal;
    SimClock::virtualTime = true;
    SimClock::nowMs = 0;
    t = &tft; SimCal::W = W; SimCal::H = H;
    rot = W > H ? 1 : 0;
    const int w0 = (rot & 1) ? H : W, h0 = (rot & 1) ? W : H;
    fit = TouchFit::fromRanges(200, 3800, 200, 3800, w0, h0);
    SimCal::skip = skip; SimCal::want = want;
    if (!rawPath.empty()) {
        raw = fopen(rawPath.c_str(), "wb");
        if (!raw) { fprintf(stderr, "failed to open %s\n", rawPath.c_str()); return 1; }
    }
    SimClock::onDelay = snap;
    tft.fillScreen(Theme::BG);
    TouchFit::Fit out;
    // SKIP is offered, as it is to every board updating from older firmware.
    TouchCal::runInteractive(tft, reader, rot, &fit, 800, Theme::BG, Theme::WHITE, Theme::CYAN, out);
    while (got < skip + want) { SimClock::nowMs += 66; snap(); }
    SimClock::onDelay = nullptr;
    if (raw) {
        fclose(raw);
        printf("raw %dx%d rgb888 frames=%d -> %s\n", W, H, want, rawPath.c_str());
        return 0;
    }
    if (!PngWriter::write(outPath.c_str(), W, H, lastRgb.data())) return 1;
    printf("rendered 'touchcal' -> %s (%dx%d)\n", outPath.c_str(), W, H);
    return 0;
}

static void usage() {
    fprintf(stderr,
        "usage: squachsim <screen> [out.png] [options]\n"
        "  screens: clear log alert settings detfilter power diary hunt rawscan watchalert colorcheck boot phone meshmenu meshwarn bingo dex touchcal icons\n"
        "  dex               --pose N opens entry N\'s card (1-17); 0 is the index\n"
        "  touchcal          the touch calibration, played by a scripted finger:\n"
        "                    --frames skips that many 66 ms frames, --sequence films\n"
        "  --portrait        render 240x320 instead of 320x240\n"
        "  --size WxH        render at another panel size, e.g. 480x320 for the 3.5in\n"
        "  --qwerty          phone screen: the QWERTY board, not the keypad\n"
        "  --msgs            messages on, with a phrase set\n"
        "  --inbox N         ...and canned line N just arrived from the visitor\n"
        "  --inboxtext TEXT  ...or this typed message did (A-Z 0-9 .,?!'-, up to 48)\n"
        "  --phrase-mode N   phrase screen: 0 show, 1 rolled, 2 picking a word, 3 picking a letter\n"
        "  --bg N            background style 0..9 (see Settings::Background)\n"
        "  --theme N         palette index\n"
        "  --alert N         DetectionType the ALERT screen fires on\n"
        "  --first / --night / --lastfree   the ALERT card's banners\n"
        "  --noseed          no detections at all -- CLEAR's idle state\n"
        "  --pet N           companion: 0 off, 1 VAPOR SHAGGY, 2 the yeti\n"
        "  --peer N          draw a visiting SquachMesh peer in outfit N\n"
        "  --peername NAME   give that visitor a custom name\n"
        "  --crowd N         clear screen: N squad members in range, roaming with ours\n"
        "  --ondesk          desk screen: the squad under the clock (DESK MODE > SQUAD ON DESK)\n"
        "  --tab N           sysprops screen: 0 update, 1 notes, 2 board\n"
        "  --beacons         turn IBEACON on, which gives it a counter column\n"
        "  --from NAME       sysprops screen: heard from that squad member, not the site\n"
        "  --frames N        animation warm-up frames before capture (default 90)\n"
        "  --onboard         let Squachy's first-boot walkthrough run\n"
        "  --sequence N      capture N consecutive frames instead of one\n"
        "  --tap F:X:Y       tap the BACKGROUND at x,y on warm-up frame F (repeatable)\n"
        "  --raw PATH        write raw RGB888 frames to PATH instead of PNGs --\n"
        "                    what the GUI consumes, no encode/decode on either side\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    std::string screen  = argv[1];
    std::string outPath = (argc > 2 && argv[2][0] != '-') ? argv[2] : "squachsim.png";

    bool portrait = false, onboard = false, showoff = false, qwerty = false;
    // Messages: --msgs switches them on with a phrase set; --inbox N also
    // delivers canned line N from the visitor, through the real receive path.
    bool msgs = false;
    int inboxLine = -1, phraseMode = -1;
    std::string sizeArg;   // --size WxH: render at another panel size
    int confirmRow = -1;   // settings screen: put a confirm panel up
    int scrollBy = 0;      // settings screen: scroll down N rows first
    int bg = -1, themeIdx = -1, frames = 90, sequence = 1, outfitIdx = -1, poseIdx = -1, petIdx = -1;
    // Background taps to inject during the warm-up, as frame:x:y. Every egg
    // (the lodge, the moon, the gold toaster, the starfield eye, the aquarium
    // shark) is reached through Theme::backgroundTap(), and until this flag
    // none of them could be exercised without a finger on glass.
    struct TapAt { int f, x, y; };
    TapAt taps[6]; int tapN = 0;
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
    bool alertFirst = false, alertNight = false, alertLastFree = false;
    // --noseed leaves the engine empty. The CLEAR screen has two states
    // now -- the headline only draws when something is actually live --
    // and with detections always seeded the emulator could not render
    // the idle one at all. Same gap --alert filled from the other side.
    bool noSeed = false;
    // SPIKE: --peer N draws a visiting Squachy in outfit N beside our own,
    // both at SMALL. No radio involved -- the point is to find out whether
    // two of him fit and whether the renderer survives being called twice.
    int peerOutfit = -1;
    // A custom name for the visitor, so the nameplate under his feet shows
    // the thing custom names exist for: a name that travelled here from
    // somebody else's device.
    std::string peerName;
    int crowdN = 0;
    int tabIdx = 0;
    bool beacons = false;
    bool onDesk  = false;
    std::string heardFrom;
    std::string inboxText;
    // --type feeds the payphone a tap sequence: digits are key presses,
    // "." waits past the multi-tap window. Typing is the feature; a screen
    // that only renders proves nothing about it.
    std::string typeSeq;
    std::string rawPath;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--portrait") portrait = true;
        else if (a == "--qwerty") qwerty = true;
        else if (a == "--msgs") msgs = true;
        else if (a == "--inbox" && i + 1 < argc) inboxLine = atoi(argv[++i]);
        else if (a == "--phrase-mode" && i + 1 < argc) phraseMode = atoi(argv[++i]);
        else if (a == "--onboard") onboard = true;
        else if (a == "--bg" && i + 1 < argc) bg = atoi(argv[++i]);
        else if (a == "--theme" && i + 1 < argc) themeIdx = atoi(argv[++i]);
        else if (a == "--outfit" && i + 1 < argc) outfitIdx = atoi(argv[++i]);
        else if (a == "--pet" && i + 1 < argc) petIdx = atoi(argv[++i]);
        else if (a == "--pose" && i + 1 < argc) poseIdx = atoi(argv[++i]);
        else if (a == "--frames" && i + 1 < argc) frames = atoi(argv[++i]);
        else if (a == "--sequence" && i + 1 < argc) sequence = atoi(argv[++i]);
        else if (a == "--raw" && i + 1 < argc) rawPath = argv[++i];
        else if (a == "--showoff") showoff = true;
        else if (a == "--confirm" && i + 1 < argc) confirmRow = atoi(argv[++i]);
        else if (a == "--info" && i + 1 < argc) infoType = atoi(argv[++i]);
        else if (a == "--alert" && i + 1 < argc) alertType = atoi(argv[++i]);
        else if (a == "--first") alertFirst = true;
        else if (a == "--night") alertNight = true;
        else if (a == "--lastfree") alertLastFree = true;
        else if (a == "--noseed") noSeed = true;
        else if (a == "--peer" && i + 1 < argc) peerOutfit = atoi(argv[++i]);
        else if (a == "--peername" && i + 1 < argc) peerName = argv[++i];
        else if (a == "--crowd" && i + 1 < argc) crowdN = atoi(argv[++i]);
        else if (a == "--tab" && i + 1 < argc) tabIdx = atoi(argv[++i]);
        else if (a == "--beacons") beacons = true;
        else if (a == "--ondesk") onDesk = true;
        else if (a == "--from" && i + 1 < argc) heardFrom = argv[++i];
        else if (a == "--inboxtext" && i + 1 < argc) inboxText = argv[++i];
        else if (a == "--type" && i + 1 < argc) typeSeq = argv[++i];
        else if (a == "--scroll" && i + 1 < argc) scrollBy = atoi(argv[++i]);
        else if (a == "--tap" && i + 1 < argc && tapN < 6) {
            int tf = 0, tx = 0, ty = 0;
            if (sscanf(argv[++i], "%d:%d:%d", &tf, &tx, &ty) == 3) taps[tapN++] = { tf, tx, ty };
            else fprintf(stderr, "--tap wants frame:x:y" "\n");
        }
        else if (a == "--size" && i + 1 < argc) sizeArg = argv[++i];
    }
    if (sequence < 1) sequence = 1;

    // --size WxH renders at another panel's dimensions. Every screen already
    // composes itself against t.width()/t.height(), so this shows the 3.5"'s
    // 480x320 here -- and catches text running off the end of a row -- without
    // flashing a board and squinting at it.
    int W = portrait ? 240 : 320;
    int H = portrait ? 320 : 240;
    if (!sizeArg.empty()) {
        int sw = 0, sh = 0;
        if (sscanf(sizeArg.c_str(), "%dx%d", &sw, &sh) == 2 && sw > 63 && sh > 63) { W = sw; H = sh; }
        else { fprintf(stderr, "--size wants WxH, e.g. 480x320" "\n"); return 2; }
    }

    TFT_eSPI tft(W, H);
    tft.init();
    tft.setRotation(portrait ? 0 : 1);

    if (screen == "touchcal") return renderTouchCal(tft, W, H, frames, sequence, rawPath, outPath);

    TFT_eSprite frame(&tft);
    // The firmware calls setColorDepth(8) before createSprite; match it
    // or the preview shows gradients the panel cannot produce.
    frame.setColorDepth(8);
    frame.createSprite(W, H);

    Settings::load();
    Clock::begin();
    Regulars::begin();
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
        // A retired slot is still a valid number but no longer in the ring,
        // so the loop above runs out and leaves you looking at whatever it
        // stopped on. Silently rendering a different background than the one
        // asked for is how a screenshot ends up mislabelled.
        if ((int)Settings::background() != want)
            fprintf(stderr, "--bg %d is not selectable (retired?) -- rendered %s instead\n",
                    want, Settings::backgroundName(Settings::background()));
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
    // --pet N picks the companion: 0 off, 1 VAPOR SHAGGY, 2 the yeti. The
    // unlock comes with it, the same way --outfit unlocks what it selects.
    if (petIdx >= 0) {
        Squachy::unlockPet();
        for (int k = 0; k < 8 && (int)Squachy::petChoice() != petIdx; k++) Squachy::cyclePet();
    }

    SquachMesh::Peer guest{};
    if (peerOutfit >= 0) {
        guest.nick = 4; guest.outfit = (uint8_t)peerOutfit; guest.shade = 1;
        guest.custom = !peerName.empty();
        snprintf(guest.name, sizeof(guest.name), "%s", peerName.c_str());
        uiClearSetGuest(&guest);
    }

    // Messages go through the real runtime and the real receive path; only
    // the cipher is a stand-in (sim/meshcrypto_sim.cpp). Set, not toggled,
    // because the NVS shim may remember a previous run.
    MeshTalk::begin();
    {
        const bool want = msgs || inboxLine >= 0 || !inboxText.empty();
        if (Settings::messagesOn() != want) Settings::toggleMessages();
        if (want) MeshTalk::setPhrase("GIBSON MOTHMAN PHREAK NESSIE ZEROCOOL");
    }
    if (inboxLine >= 0) {
        const uint8_t from[6] = { 0x24, 0x0A, 0xC4, 0xBF, 0x00, 0x7E };
        uint8_t f[MeshMsg::CANNED_FRAME_LEN];
        const size_t n = MeshMsg::sealCanned(MeshCrypto::impl(), from, 1,
                                             (uint8_t)inboxLine, f, sizeof f);
        MeshTalk::onFrame(from, f, n, peerName.empty() ? "BIGFOOT" : peerName.c_str());
        MeshTalk::tick(millis());
    }
    if (!inboxText.empty()) {
        // Every part, through the same receive path a board's scan feeds.
        const uint8_t from[6] = { 0x24, 0x0A, 0xC4, 0xBF, 0x00, 0x7E };
        const uint8_t total = MeshMsg::textParts(inboxText.c_str());
        if (!total) fprintf(stderr, "--inboxtext: not a message\n");
        for (uint8_t p = 0; p < total; p++) {
            uint8_t f[MeshMsg::TEXT_FRAME_LEN];
            const size_t n = MeshMsg::sealTextPart(MeshCrypto::impl(), from, 10 + p,
                                                   inboxText.c_str(), p, total, f, sizeof f);
            MeshTalk::onFrame(from, f, n, peerName.empty() ? "BIGFOOT" : peerName.c_str());
        }
        MeshTalk::tick(millis());
    }

    if (crowdN > 0) {
        // The same seeding the roster uses: an advert so Mesh knows the look,
        // a HELLO so the board counts them as squad, all heard just now so
        // every one of them is in range. CROWD is raised until they all fit.
        if (!Settings::meshDetect()) Settings::cycleMeshDetect();
        if (!Settings::messagesOn()) Settings::toggleMessages();
        MeshTalk::setPhrase("GIBSON MOTHMAN PHREAK NESSIE ZEROCOOL");
        for (int g = 0; g < 10 && (Settings::meshCrowd() < crowdN + 1); g++) Settings::cycleMeshCrowd();
        static const char* const NAMES[7] = { "BIGFOOT", nullptr, "YETI", "MOTHMAN", nullptr, "NESSIE", "WENDIGO" };
        uint32_t ctr = 100;
        for (int k = 0; k < crowdN && k < 7; k++) {
            const uint8_t mac[6] = { 0x24, 0x0A, 0xC4, (uint8_t)(k + 1), 0x00, (uint8_t)(k + 1) };
            SquachMesh::Peer p{};
            p.nick = (uint8_t)(3 + k * 2); p.outfit = (uint8_t)((k * 5 + 1) % Squachy::outfitCount()); p.shade = (uint8_t)(k % 4);
            if (NAMES[k]) { p.custom = true; snprintf(p.name, sizeof p.name, "%s", NAMES[k]); }
            uint8_t ad[SquachMesh::LEN_MAX + 2] = { (uint8_t)(SquachMesh::COMPANY_ID & 0xFF), (uint8_t)(SquachMesh::COMPANY_ID >> 8) };
            const size_t an = SquachMesh::encode(p, ad + 2);
            Mesh::onManufacturerData(ad, an + 2, mac, millis());
            uint8_t f[MeshMsg::FRAME_MAX];
            const uint8_t hv[3] = { 1, 7, 9 };
            const size_t n = MeshMsg::sealHello(MeshCrypto::impl(), mac, ctr++, hv, f, sizeof f);
            MeshTalk::onFrame(mac, f, n, NAMES[k] ? NAMES[k] : "");
        }
        MeshTalk::tick(millis());
    }

    // Set rather than toggled: the NVS shim may remember a previous run.
    if (onDesk != Settings::deskSquad()) Settings::toggleDeskSquad();
    // The desk's HOW MANY follows --crowd, the way the main screen's does.
    for (int g = 0; g < 10 && Settings::deskCrowd() != Settings::meshCrowd(); g++) Settings::cycleDeskCrowd();
    if (getenv("SQUACHSIM_FULLVISIT") && !Settings::deskFullVisit()) Settings::toggleDeskFullVisit();
    // SQUACHSIM_CLOCK="font,size,backdrop", e.g. "1,2,3" for large Bangers over toasters.
    if (const char* ck = getenv("SQUACHSIM_CLOCK")) {
        int f = 0, z = 1, b = 0;
        sscanf(ck, "%d,%d,%d", &f, &z, &b);
        for (int g = 0; g < 4 && Settings::clockFont() != f; g++)     Settings::cycleClockFont();
        for (int g = 0; g < 4 && Settings::clockSize() != z; g++)     Settings::cycleClockSize();
        for (int g = 0; g < 8 && Settings::clockBackdrop() != b; g++) Settings::cycleClockBackdrop();
    }
    // Off by default on the board, so the counter column only exists when
    // somebody has asked for the type.
    if (beacons && !Settings::typeEnabled(DetectionType::IBEACON))
        Settings::toggleType(DetectionType::IBEACON);

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

    // Virtual time, in the ONE-SHOT renderer too, not just the interactive
    // emulator.
    //
    // This screen is ticked with a synthetic `now` that runs at 33 ms a
    // frame while millis() was still answering with wall-clock -- and the
    // firmware reads both. Anything that stamped a deadline with millis()
    // and was then tested against `now` compared two different clocks that
    // were seconds apart within a few frames: Squachy::say() sets
    // bubbleUntil = millis() + ms, and tick() shows the bubble while
    // now < bubbleUntil, so by frame 100 the host's speech bubble could not
    // render at all here. That is not a rendering difference, it is the
    // emulator disagreeing with the device about what time it is, and it is
    // the sixth fidelity bug found in this shim.
    //
    // Pinning millis() to the frame's own timestamp makes the two clocks the
    // same clock, which is what they are on the device.
    SimClock::virtualTime = true;
    SimClock::nowMs       = now;

    auto tick = [&](uint32_t t) {
        SimClock::nowMs = t;
        if      (screen == "clear")    { uiClearEmoteTick(t); uiClearTick(frame, t, engine, true, false); }
        else if (screen == "log") {
            const bool info = (infoType >= 0);
            const DetectionType it = info ? (DetectionType)infoType : DetectionType::UNKNOWN;
            uiLogTick(frame, t, engine, 0, false, "", info,
                      info ? detectionTypeName(it) : nullptr,
                      info ? DetectionInfo::explainLive(it, engine) : "",
                      false, false);   // no confirm panel in the sim: nothing watched, nothing hunted
        }
        else if (screen == "alert")    uiAlertTick(frame, t, engine, false, nullptr, "");
        else if (screen == "settings") uiSettingsTick(frame, t, engine);
        else if (screen == "detfilter") uiDetFilterTick(frame, t, engine);
        else if (screen == "power")    uiPowerTick(frame, t, engine);
        else if (screen == "security") uiSecurityTick(frame, t, engine);
        else if (screen == "ignorelist") uiIgnoreListTick(frame, t);
        else if (screen == "light")    uiLightTick(frame, t, engine);
        else if (screen == "diary")    uiDiaryTick(frame, t, engine);
        else if (screen == "desk")     uiDeskTick(frame, t, engine);
        else if (screen == "zonecard") { uiClearTick(frame, t, engine, true, false); uiZoneCardDraw(frame, t); }
        else if (screen == "hunt")     uiHuntTick(frame, t, engine);
        else if (screen == "rawscan")  uiRawScanTick(frame, t, engine, true, true, false, "", false, false);
        else if (screen == "phone")    uiPhoneTick(frame, t, engine);
        else if (screen == "bingo")    uiBingoTick(frame, t, engine);
        else if (screen == "dex")      uiDexTick(frame, t, engine);
        else if (screen == "meshmenu") uiMeshMenuTick(frame, t, engine);
        else if (screen == "roster")   uiSquadTick(frame, t, engine);
        else if (screen == "meshwarn") uiMeshWarnTick(frame, t, engine);
        else if (screen == "phrase")   uiMeshPhraseTick(frame, t, engine);
        else if (screen == "compose")  uiMeshComposeTick(frame, t, engine);
        else if (screen == "watchalert") uiWatchAlertTick(frame, t, engine, true);
        else if (screen == "colorcheck") uiColorCheckTick(frame, t);
        else if (screen == "icons") {
            // Every detection type's icon, large, on a black ground: an icon
            // sheet for mockups and docs. 6 across; --size sets the cell.
            frame.fillRect(0, 0, frame.width(), frame.height(), 0x0000);
            const int cols = 6, cw = frame.width() / cols, ch = frame.height() / 3;
            // A third, not a half: several icons draw wider than 2s.
            const int s = (cw < ch ? cw : ch) / 3;
            for (int i = 1; i < (int)DetectionType::COUNT; i++) {
                const int k = i - 1;
                Theme::drawTypeIcon(frame, (DetectionType)i, (k % cols) * cw + cw / 2, (k / cols) * ch + ch / 2, s);
            }
        }
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
            // A plausible last screen, so the LAST line is laid out at its longest.
            info.lastScreenName = "WATCH";
            info.lastScreenUs   = 123400;
            // The black box at its longest: a dozen crashes, the newest with
            // a date and a version.
            info.bbReady    = true;
            info.bbKept     = 1834;
            info.bbCrashes  = 12;
            info.bbHaveLast = true;
            info.bbLast.epoch = 1789560000u;
            strncpy(info.bbLast.version, "1.11.0", sizeof info.bbLast.version - 1);
            // SQUACHSIM_CRASH: the crash lines too, for the tallest layout.
            if (getenv("SQUACHSIM_CRASH")) {
                info.crash.valid = true;
                info.crash.uptimeMs = 2521000; info.crash.heapFree = 38112; info.crash.heapBlock = 24564;
                info.crash.haveDump = true;
                strncpy(info.crash.task, "nimble_host", sizeof info.crash.task - 1);
                info.crash.pc = 0x400D1234; info.crash.btN = 3;
                info.crash.bt[0] = 0x400D5678; info.crash.bt[1] = 0x400D9ABC; info.crash.bt[2] = 0x400E0123;
            }
            info.bgUs           = 12300;
            info.frameUs = 41200;
            info.freeHeap = 180000;
            info.largestBlock = 110000;
            info.resetReason = "POWERON_RESET";
            info.boardName = "cyd";
            info.usingCapTouch = false;
            uiDiagnosticsTick(frame, t, engine, info);
        }
        else if (screen == "boot")     uiBootTick(frame, t);
        else if (screen == "update")   uiUpdateTick(frame, t);
        else if (screen == "nudge")    uiNudgeTick(frame, t, engine);
        else if (screen == "squadupdate") uiSquadUpdateTick(frame, t, engine);
        else if (screen == "invite")   uiInviteTick(frame, t, engine);
        else if (screen == "wifipass") uiWifiPassTick(frame, t);
        else if (screen == "petunlock" || screen == "unlock") uiOutfitUnlockTick(frame, t, engine);
        else if (screen == "sysprops") uiSysPropsTick(frame, t, engine);
        else if (screen == "wifinets") uiWifiNetsTick(frame, t);
        else if (screen == "wifiadd")  uiWifiAddTick(frame, t, engine);
        else if (screen == "poses") {
            // Every arm movement he has, for the costume test in
            // sim/test_outfit_poses.py: IDLE, WAVE, then each VisitPose, each at
            // POSE_PHASES moments 125 ms apart, one per captured frame, on a flat
            // key colour so the script can separate him from the backdrop. Run
            // with --frames 0 and --sequence POSE_N * POSE_PHASES.
            using VP = Squachy::VisitPose;
            static const VP kPoses[] = {
                VP::NONE, VP::NONE, VP::HIGH_FIVE, VP::LOW_FIVE, VP::FIST, VP::STARTLED,
                VP::DANCE, VP::PUMP, VP::SLEEPY, VP::STRETCH, VP::LAUGH, VP::SALUTE,
                VP::BOW, VP::HUG, VP::SAD, VP::GRR, VP::CROUCH, VP::PULL, VP::WIGGLE,
                VP::CHEER, VP::SELFIE, VP::HOWL, VP::POINT, VP::STRAIN, VP::COVER,
                VP::LOOK_AROUND, VP::HANDS_UP,
            };
            const uint32_t POSE_N = sizeof kPoses / sizeof kPoses[0];
            const uint32_t POSE_PHASES = 8;
            const uint32_t k = ((t - now) / STEP_MS) % (POSE_N * POSE_PHASES);
            const uint32_t pi = (poseIdx >= 0) ? (uint32_t)poseIdx : k / POSE_PHASES;
            const uint32_t tt = 100000u + (k % POSE_PHASES) * 125u;
            SimClock::nowMs = tt;
            frame.fillRect(0, 0, W, H, 0x024A);   // dark teal: no costume uses it
            Squachy::drawWaving(frame, W / 2, H - 12, tt, 2.0f, nullptr, false, 0,
                                /*waving*/ pi == 1, 34, false, false, false, kPoses[pi]);
        }
        else return false;
        return true;
    };

    // Per-screen init, where the screen has one.
    if      (screen == "clear" || screen == "zonecard") uiClearInit(frame);
    else if (screen == "log") {
        // Three sightings in the black box, so the list shows the rows held
        // in RAM and then carries on into the ones kept in flash.
        BlackBox::begin();
        for (uint8_t i = 0; i < 3; i++) {
            Detection old{};
            for (int b2 = 0; b2 < 6; b2++) old.mac[b2] = (uint8_t)(0xA0 + i * 8 + b2);
            old.type = (DetectionType)(1 + i * 5);
            old.rssi = (int8_t)(-70 - i * 4);
            old.hits = (uint16_t)(2 + i);
            snprintf(old.name, sizeof old.name, "%s", i == 1 ? "Old Flipper" : "");
            BlackBox::noteDetection(old, false);
        }
        uiLogInit(frame);
    }
    else if (screen == "settings")   {
        uiSettingsInit(frame);
        // SQUACHSIM_PAGE=N opens a sub-page: 1 appearance, 2 system, 3 desk.
        if (const char* pg = getenv("SQUACHSIM_PAGE")) uiSettingsOpenPage((SettingsPage)atoi(pg));
    }
    else if (screen == "detfilter")  uiDetFilterInit(frame);
    else if (screen == "diary")      uiDiaryInit(frame);
    else if (screen == "desk")       {
        uiDeskInit(frame);
        if (getenv("SQUACH_TIMER")) uiDeskTapTimer(now);
        // SQUACHSIM_EPOCH=<unix seconds> sets the clock, for rendering a
        // particular time (a two-digit hour, a PM).
        if (const char* ep = getenv("SQUACHSIM_EPOCH")) Clock::setEpoch((uint32_t)strtoul(ep, nullptr, 10));
        if (getenv("SQUACH_ALERT") && engine.logAt(0)) uiDeskAlert(*engine.logAt(0), now);
    }
    else if (screen == "rawscan")    uiRawScanInit(frame, true);
    else if (screen == "watchalert") uiWatchAlertInit(frame);
    else if (screen == "diagnostics") uiDiagnosticsInit(frame);
    else if (screen == "colorcheck") uiColorCheckInit(frame);
    else if (screen == "icons")      {}
    else if (screen == "boot")       uiBootInit(frame);
    else if (screen == "update")     uiUpdateInit(frame);
    else if (screen == "nudge")      { const uint8_t v[3] = { 1, 7, 6 }; uiNudgeInit(frame, "BIGFOOT", v, 30, 0); }
    else if (screen == "squadupdate") uiSquadUpdateInit(frame);
    else if (screen == "invite") {
        // --pose N picks the page: 0 offering, 1 asked, 2 code, 3 sending, 4 joined,
        // 5 failed, 6 waiting, 7 done. Even poses after 5 are the inviter's side.
        uiInviteInit(frame);
        static const MeshTalk::InviteState PAGES[] = { MeshTalk::InviteState::OFFERING, MeshTalk::InviteState::ASKED,
            MeshTalk::InviteState::CODE, MeshTalk::InviteState::SENDING, MeshTalk::InviteState::JOINED, MeshTalk::InviteState::FAILED,
            MeshTalk::InviteState::WAITING, MeshTalk::InviteState::DONE };
        const int p = (poseIdx >= 0 && poseIdx < 8) ? poseIdx : 2;
        uiInviteDemo(PAGES[p], 4821, p == 1 || p == 4 || p == 6 ? "BIGFOOT" : "YETI", p == 0 || p == 3 || p == 5 || p == 7);
    }
    else if (screen == "wifipass")   uiWifiPassInit(frame, "SquachNet");
    else if (screen == "petunlock")  uiPetUnlockInit(frame);
    else if (screen == "unlock")     uiOutfitUnlockInit(frame, (uint8_t)(outfitIdx < 0 ? 2 : outfitIdx));
    else if (screen == "sysprops") {
        // The real path: a version arrives, then the release's own lines if
        // it came from the site. --from makes it a squad member's hello,
        // which carries a number and no notes.
        OtaCore::noteAvailable("1.21.0", heardFrom.c_str());
        if (heardFrom.empty()) {
            static const char* const NEWS[3] = {
                "He hears again, every boot",
                "A proper buzz on the wrist",
                "The Freenove 3.2 inch joins",
            };
            OtaCore::noteRelease("All Ears", NEWS, 3);
        }
        uiSysPropsInit(frame);
        // The tab a tap would have opened, for rendering one of them.
        if (tabIdx > 0 && tabIdx < 3) {
            // The same arithmetic as geom() in ui_sysprops.cpp.
            const int ww = frame.width()  - 8 < 300 ? frame.width()  - 8 : 300;
            const int wh = frame.height() - 8 < 232 ? frame.height() - 8 : 232;
            const int tw = (ww - 8) / 3;
            const int wx = (frame.width()  - ww) / 2;
            const int wy = (frame.height() - wh) / 2;
            uiSysPropsTouch(frame, wx + 4 + tabIdx * tw + tw / 2, wy + 3 + 18 + 3 + 10);
        }
    }
    else if (screen == "wifinets")   uiWifiNetsInit(frame);
    else if (screen == "wifiadd")    uiWifiAddInit(frame);
    else if (screen == "dex") {
        // Eleven of the seventeen caught, through the engine's own door, with
        // a record for each so the cards have something to say. --pose N
        // opens straight on entry N (1-based); --pose 0 shows the index.
        Dex::begin();
        static const DetectionType got[] = {
            DetectionType::FLOCK, DetectionType::AXON, DetectionType::META, DetectionType::AIRTAG,
            DetectionType::DRONE, DetectionType::ALPR, DetectionType::CAMERA, DetectionType::SAMSUNG_TAG,
            DetectionType::TILE, DetectionType::RING, DetectionType::IBEACON };
        uint16_t serial = 900;
        for (DetectionType ty : got) {
            const int n = ty == DetectionType::AIRTAG ? 38 : ty == DetectionType::AXON ? 2 : 5;
            for (int k = 0; k < n; k++) {
                Detection d{};
                d.mac[0] = 0x02; d.mac[4] = (uint8_t)k; d.mac[5] = (uint8_t)serial++;
                d.type = ty;
                d.conf = confidenceFor(ty);
                d.rssi = (int8_t)(-70 + (k % 30));
                d.firstSeen = d.lastSeen = now;
                d.hits = 1;
                d.active = true;
                engine.postBle(d);
                Dex::note(ty, d.rssi);
            }
        }
        uiDexInit(frame);
        if (poseIdx > 0) uiDexOpenCard((uint8_t)(poseIdx - 1));
    }
    else if (screen == "bingo") {
        // A card part-way through, so the marked, the lined and the
        // still-missing all show at once.
        Bingo::begin(engine);
        for (int k = 0; k < 6; k++) Bingo::note(Bingo::typeAt((uint8_t)k));
        Bingo::tick(1000);
        uiBingoInit(frame);
        // --pose 1: the panel NEW asks first. Tapped through the real hit
        // test, at the middle of the middle button.
        if (poseIdx == 1) {
            const Theme::ButtonBarGeom bar = Theme::computeButtonBar(frame.width(), frame.height());
            uiBingoHitTest(frame, bar.x[1] + bar.w[1] / 2, bar.y + bar.h / 2, frame.width(), frame.height());
        }
    }
    else if (screen == "meshmenu")   uiMeshMenuInit(frame);
    else if (screen == "roster") {
        // Three members, through the real paths: an advert each so Mesh knows
        // their look, then a sealed HELLO each so MeshTalk puts them on the
        // roster. --pose 1 marks the first of them as still in range.
        if (!Settings::meshDetect()) Settings::cycleMeshDetect();
        if (!Settings::messagesOn()) Settings::toggleMessages();
        MeshTalk::setPhrase("GIBSON MOTHMAN PHREAK NESSIE ZEROCOOL");
        struct Seed { uint8_t mac[6]; uint8_t nick, outfit, shade; const char* name; uint8_t met; };
        static const Seed SEEDS[] = {
            { { 0x24, 0x0A, 0xC4, 0x01, 0x00, 0x01 }, 4, 12, 2, "BIGFOOT", 7 },
            { { 0x24, 0x0A, 0xC4, 0x02, 0x00, 0x02 }, 6,  3, 1, nullptr,   2 },
            { { 0x24, 0x0A, 0xC4, 0x03, 0x00, 0x03 }, 9,  5, 0, "YETI",    1 },
        };
        // Seeded in the past, so that by render time only the one advertised
        // again below is still in range.
        const uint32_t renderNow = SimClock::nowMs;
        SimClock::nowMs = renderNow - 3u * 3600000u;
        uint32_t ctr = 100;
        for (const Seed& sd : SEEDS) {
            SquachMesh::Peer p{};
            p.nick = sd.nick; p.outfit = sd.outfit; p.shade = sd.shade;
            if (sd.name) { p.custom = true; snprintf(p.name, sizeof p.name, "%s", sd.name); }
            uint8_t ad[SquachMesh::LEN_MAX + 2] = { (uint8_t)(SquachMesh::COMPANY_ID & 0xFF), (uint8_t)(SquachMesh::COMPANY_ID >> 8) };
            const size_t an = SquachMesh::encode(p, ad + 2);
            Mesh::onManufacturerData(ad, an + 2, sd.mac, millis());
            // met N times: N hellos, each after the six-minute freshness ran out.
            for (uint8_t k = 0; k < sd.met; k++) {
                uint8_t f[MeshMsg::FRAME_MAX];
                const uint8_t hv[3] = { 1, 7, 9 };
                const size_t n = MeshMsg::sealHello(MeshCrypto::impl(), sd.mac, ctr++, hv, f, sizeof f);
                MeshTalk::onFrame(sd.mac, f, n, sd.name ? sd.name : "");
                SimClock::nowMs += 7 * 60000;
                MeshTalk::tick(millis());
            }
        }
        SimClock::nowMs = renderNow;
        MeshTalk::tick(millis());
        // Only the first is still around: the others were heard long ago.
        if (poseIdx == 1) {
            SquachMesh::Peer p{}; p.nick = 4; p.outfit = 12; p.shade = 2; p.custom = true;
            snprintf(p.name, sizeof p.name, "%s", "BIGFOOT");
            uint8_t ad[SquachMesh::LEN_MAX + 2] = { (uint8_t)(SquachMesh::COMPANY_ID & 0xFF), (uint8_t)(SquachMesh::COMPANY_ID >> 8) };
            const size_t an = SquachMesh::encode(p, ad + 2);
            Mesh::onManufacturerData(ad, an + 2, SEEDS[0].mac, millis());
        }
        uiSquadInit(frame, true);
    }
    else if (screen == "phrase")     {
        uiMeshPhraseInit(frame);
        if (phraseMode >= 0) uiMeshPhraseDemo((uint8_t)phraseMode);
    }
    else if (screen == "compose")    uiMeshComposeInit(frame);
    else if (screen == "phone")      {
        // Set rather than toggled: the NVS shim may remember a previous run.
        if (Settings::phoneQwerty() != qwerty) Settings::togglePhoneQwerty();
        uiPhoneInit(frame);
        uint32_t tnow = now;
        if (!qwerty) {
            // Key centres, computed the same way ui_phone.cpp lays them out.
            for (size_t i = 0; i < typeSeq.size(); i++) {
                if (typeSeq[i] == '.') { tnow += 900; continue; }
                const int k = typeSeq[i] - '0';
                if (k < 0 || k > 11) continue;
                const int kx = 78 + (164 - (48 * 3 + 3 * 2)) / 2 + (k % 3) * 51 + 24;
                const int ky = 8 + 60 + (k / 3) * 33 + 15;
                uiPhoneTouch(kx, ky, tnow, PhoneTouch::DOWN);
                tnow += 120;
            }
        } else {
            // Letters typed as real taps, press then release, at key centres
            // from the same Qwerty::layout the screen draws. A '^' holds the
            // NEXT key down without releasing it, so a frame can show the
            // preview box and the armed key.
            Qwerty::Key keys[Qwerty::KEY_N];
            const uint8_t kn = Qwerty::layout(frame.width(), Qwerty::BAND_TOP,
                                              frame.height() - Qwerty::BAND_BOTTOM_INSET, keys);
            tick(tnow);                  // the screen lays its keys out on draw
            bool hold = false;
            for (size_t i = 0; i < typeSeq.size(); i++) {
                char c = typeSeq[i];
                if (c == '^') { hold = true; continue; }
                if (c >= 'a' && c <= 'z') c = (char)(c - 32);
                for (uint8_t k = 0; k < kn; k++) {
                    if (keys[k].ch != c) continue;
                    const int cx = keys[k].x + keys[k].w / 2, cy = keys[k].y + keys[k].h / 2;
                    uiPhoneTouch(cx, cy, tnow, PhoneTouch::DOWN);
                    if (!hold) uiPhoneTouch(cx, cy, tnow, PhoneTouch::UP);
                    break;
                }
                hold = false;
                tnow += 120;
            }
        }
    }
    else if (screen == "hunt")       {
        // --pose 1: the target is in your hand, two strong samples in, CAUGHT!
        const uint8_t hm[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
        engine.huntBle(hm, poseIdx == 1 ? "BIGFOOT" : "AirTag");
        if (poseIdx == 1) {
            engine.checkHuntBle(hm, -36); SimClock::nowMs += 2100;
            engine.checkHuntBle(hm, -34);
        }
    }
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
        // The banners, the way main.cpp sets them: after init, which clears them.
        if (alertFirst)    uiAlertSetFirst(true);
        if (alertNight)    uiAlertSetNight(true);
        if (alertLastFree) uiAlertSetLastFree(true);
    }

    // After uiSettingsInit(), which clears any pending question.
    if (confirmRow >= 0) uiSettingsSetConfirm((SettingsRow)confirmRow);
    // --scroll drives whichever list is on screen, not just SETTINGS. Every
    // one of them clamps at the bottom now, and a flag that could only reach
    // one of the eight could only ever test one of them.
    for (int k = 0; k < scrollBy; k++) {
        if      (screen == "log")        uiLogScroll(1);
        else if (screen == "rawscan")    uiRawScanScroll(1);
        else if (screen == "power")      uiPowerScroll(1);
        else if (screen == "security")   uiSecurityScroll(1);
        else if (screen == "light")      uiLightScroll(1);
        else if (screen == "detfilter")  uiDetFilterScroll(1);
        else if (screen == "ignorelist") uiIgnoreListScroll(1);
        else                             uiSettingsScroll(1);
    }

    for (int i = 0; i < frames; i++) {
        const uint32_t tNow = now + (uint32_t)i * STEP_MS;
        if (!tick(tNow)) { usage(); return 2; }
        // After the frame, so the tap lands on something just drawn:
        // backgroundTap() hit-tests published positions and ignores anything
        // that has not been refreshed in the last few frames.
        for (int k = 0; k < tapN; k++)
            if (taps[k].f == i) Theme::backgroundTap(taps[k].x, taps[k].y, tNow);
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
        const uint32_t sNow = now + (uint32_t)(frames + s) * STEP_MS;
        tick(sNow);
        // --tap frame numbers run straight on through the capture, so a tap
        // can land on a frame you can actually look at afterwards.
        for (int k = 0; k < tapN; k++)
            if (taps[k].f == frames + s) Theme::backgroundTap(taps[k].x, taps[k].y, sNow);
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
