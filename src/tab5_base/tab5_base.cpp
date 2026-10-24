// SquachWatch BASE STATION for the M5Stack Tab5 -- step 2, the squad.
//
// Listens to SquachMesh and, once the squad phrase is set, opens the squad's
// sealed frames the same way a board does (meshmsg.cpp + meshcrypto.cpp, the
// boards' own code): HEADSUP alerts, canned and typed messages, HELLOs (each
// board's version) and FIXes (a phone's position). Every SquachWatch in range
// is listed on the left; the right is a live feed, newest first, with a sound
// for each catch.
//
// The phrase is typed once on a touchscreen keyboard that only accepts words
// from the shared word list, stretched to the key exactly as a board does it
// (PBKDF2, MeshMsg::ITERS rounds), and kept in NVS with the key so a reboot
// does not stretch it again. Receive only: the base station sends nothing,
// so the boards never see it in their squad roster.
//
// The radio is the Tab5's ESP32-C6, reached over SDIO by ESP-Hosted; the
// core's BLE library brings that link up inside BLEDevice::init().
//
// Threading: the scan callback runs in the BLE host task. It only copies the
// manufacturer data into a small ring; loop() drains it, owns every table,
// does the decryption and draws. Nothing else is shared.
#include <Arduino.h>
#include <atomic>
#include <SD_MMC.h>          // before M5Unified: M5GFX draws PNGs from an FS only if FS.h came first
#include <M5Unified.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "squachmesh.h"
#include "meshmsg.h"
#include "meshcrypto.h"
#include "state.h"
#include "gnss.h"
#include <esp_system.h>
// Shown on screen and in the file name, so an install can be told apart in
// Launcher (which shows a file's first five letters).
#define TAB5_VERSION "v32"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

namespace {

// ---- names --------------------------------------------------------------
// The shared nickname table: the same ten entries, in the same order, as
// NICKNAMES in squachy.cpp. An advert carries only the index.
const char* const NICKNAMES[] = {
    "SQUACHY", "BIGSY", "FOOTS", "STOMPER", "SHADOW",
    "TRACKER", "CHONK", "WOODS", "YETI", "SASSY",
};
const uint8_t NICKNAMES_N = sizeof(NICKNAMES) / sizeof(NICKNAMES[0]);

// ---- colours ---------------------------------------------------------------
const uint16_t BG     = 0x0841;   // near black
const uint16_t PANEL  = 0x10A2;   // card background
const uint16_t INK    = TFT_WHITE;
const uint16_t DIM    = 0x8410;   // grey
const uint16_t ACCENT = 0x07E0;   // phosphor green
const uint16_t WARN   = 0xFD20;   // orange
const uint16_t ALERT  = 0xF800;   // red
const uint16_t HACK   = 0xB81F;   // purple
const uint16_t INFO   = 0x5D1F;   // blue

// Every type's colour, by how much it matters. Red is kept for kit that
// attacks you -- hacking tools, skimmers, deauth floods, fake access points;
// orange for the watchers -- plate readers, Flock, Raven, police gear; the
// rest each get a hue of their own, so a map full of trackers is not one
// sea of yellow (2026-10-17). Tier 0 and 1 pins draw larger, and on top.
struct TypeLook { const char* name; uint16_t colour; uint8_t tier; };
const TypeLook TYPE_LOOKS[] = {
    { "HACKER",      0xF800, 0 },   // red
    { "SKIMMER",     0xF800, 0 },
    { "DEAUTH",      0xF800, 0 },
    { "EVILTWIN",    0xF800, 0 },
    { "FLOCK",       0xFC00, 1 },   // orange
    { "ALPR",        0xFC00, 1 },
    { "RAVEN",       0xFC00, 1 },
    { "AXON",        0xFC00, 1 },
    { "DRONE",       0xF81F, 2 },   // magenta
    { "META",        0xB81F, 2 },   // purple: camera glasses
    { "CAMERA",      0x07FF, 2 },   // cyan
    { "RING",        0x07FF, 2 },
    { "AIRTAG",      0xFFE0, 3 },   // yellow
    { "GOOGLE_TAG",  0x07E0, 3 },   // green
    { "TILE",        0x05FF, 3 },   // azure
    { "SAMSUNG_TAG", 0x9CDF, 3 },   // periwinkle
    { "IBEACON",     0xBDF7, 4 },   // light grey
};
const TypeLook* typeLook(const char* t) {
    for (const auto& l : TYPE_LOOKS) if (!strcmp(l.name, t)) return &l;
    return nullptr;
}
uint16_t colourForName(const char* t) { const TypeLook* l = typeLook(t); return l ? l->colour : DIM; }
uint8_t  tierForName(const char* t)   { const TypeLook* l = typeLook(t); return l ? l->tier : 4; }
uint16_t typeColour(uint8_t t) { return colourForName(detectionTypeName((DetectionType)t)); }

// ---- the ring the BLE task fills ------------------------------------------
struct Heard {
    uint8_t  mac[6];
    int8_t   rssi;
    uint8_t  len;
    uint8_t  data[32];     // company ID + payload; a legacy AD holds 29
};
constexpr uint16_t RING_N = 256;     // a map redraw can hold loop() for most of a second
Heard         s_ring[RING_N];
volatile uint16_t s_head = 0, s_tail = 0;
portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> s_dropped{0}, s_advSeen{0};

// The core's scanner keeps every device it hears in a map, which only a
// finished scan empties -- and this scan never finishes. It has to be emptied
// by hand, and ONLY from this callback: the host task inserts into and walks
// that map on every advert, so clearing it from loop() (as steps 1 and 2 did,
// every 30 s) raced it and freed devices the host task was still using --
// the Tab5's reboot on its own, 2026-10-16. Here it runs between two adverts,
// in the task that owns the map. The callback gets its own copy of the
// device, and with results kept (the library's default) nothing after the
// callback touches the stored one again.
constexpr uint32_t CLEAR_EVERY = 256;

class ScanCb : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        if (++s_advSeen % CLEAR_EVERY == 0) BLEDevice::getScan()->clearResults();
        if (!dev.haveManufacturerData()) return;
        String md = dev.getManufacturerData();
        const size_t n = md.length();
        if (n < 2 || n > sizeof(Heard::data)) return;
        const uint8_t* d = (const uint8_t*)md.c_str();
        if ((uint16_t)(d[0] | (d[1] << 8)) != SquachMesh::COMPANY_ID) return;
        portENTER_CRITICAL(&s_mux);
        const uint16_t next = (uint16_t)((s_head + 1) % RING_N);
        if (next == s_tail) { s_dropped++; portEXIT_CRITICAL(&s_mux); return; }
        Heard& h = s_ring[s_head];
        memcpy(h.mac, dev.getAddress().getNative(), 6);
        h.rssi = (int8_t)dev.getRSSI();
        h.len  = (uint8_t)n;
        memcpy(h.data, d, n);
        s_head = next;
        portEXIT_CRITICAL(&s_mux);
    }
};

bool takeHeard(Heard& out) {
    bool got = false;
    portENTER_CRITICAL(&s_mux);
    if (s_tail != s_head) { out = s_ring[s_tail]; s_tail = (uint16_t)((s_tail + 1) % RING_N); got = true; }
    portEXIT_CRITICAL(&s_mux);
    return got;
}

// ---- the squad key -----------------------------------------------------------
Preferences s_prefs;
bool  s_cryptoOk = false;     // MeshCrypto::selfTest() passed
bool  s_haveKey  = false;
char  s_phrase[MeshMsg::PHRASE_TEXT_MAX + 1] = "";
// The byte order getNative() hands back, against the one the boards build
// their nonce from. Settled by the first frame that authenticates.
int8_t s_macOrder = -1;       // -1 unknown, 0 as given, 1 reversed

bool setPhrase(const char* text) {
    uint8_t key[MeshMsg::KEY_LEN];
    const uint32_t t0 = millis();
    if (!MeshCrypto::impl().derive(text, strlen(text), (const uint8_t*)MeshMsg::SALT,
                                   strlen(MeshMsg::SALT), MeshMsg::ITERS, key)) return false;
    Serial.printf("[base] key stretched in %lu ms\n", (unsigned long)(millis() - t0));
    if (!MeshCrypto::impl().setKey(key)) return false;
    s_prefs.putString("phrase", text);
    s_prefs.putBytes("key", key, sizeof key);
    strncpy(s_phrase, text, sizeof s_phrase - 1);
    s_haveKey = true;
    return true;
}

void loadKey() {
    uint8_t key[MeshMsg::KEY_LEN];
    if (s_prefs.getBytes("key", key, sizeof key) != sizeof key) return;
    if (!MeshCrypto::impl().setKey(key)) return;
    s_prefs.getString("phrase", s_phrase, sizeof s_phrase);
    s_haveKey = true;
}

// ---- the Tab5's own GPS --------------------------------------------------------
// An M5Stack Unit GPS v1.1 on the Grove port (PORT.A: GPIO53 and GPIO54).
// Its sentences go through gnss.cpp, the boards' own NMEA parser. Which of the
// two pins carries its TX, and at what speed, is a search -- the same one the
// Waveshare GPS build does -- until a GOOD sentence parses. The port's 5 V is
// switched; M5.Power.setExtOutput() turns it on.
const int8_t   kGpsPins[]  = { 53, 54 };
const uint32_t kGpsBauds[] = { 115200, 9600, 38400 };
uint8_t  s_gpsTry = 0;
uint32_t s_gpsTryAt = 0;
int8_t   s_gpsPin = kGpsPins[0];
uint32_t s_gpsBaud = kGpsBauds[0];

// The UART's own buffer is 256 bytes: at 115200 that is about 22 ms of NMEA,
// and a map redraw holds loop() for most of a second. Overflowed sentences
// were dropped and the fix went stale -- the Tab5 "lost GPS" while the map
// was up, 2026-10-16. 8 KB is over half a second of the module's output.
constexpr size_t GPS_RX_BUF = 8192;

void gpsStart() {
    Gnss::reset();
    Serial1.setRxBufferSize(GPS_RX_BUF);
    Serial1.begin(s_gpsBaud, SERIAL_8N1, s_gpsPin, -1);
    s_gpsTryAt = millis();
}

void gpsTick(uint32_t now) {
    while (Serial1.available()) Gnss::feed((char)Serial1.read(), now);
    if (Gnss::good() || now - s_gpsTryAt < 6000) return;
    const uint8_t pins = sizeof kGpsPins / sizeof kGpsPins[0];
    const uint8_t bauds = sizeof kGpsBauds / sizeof kGpsBauds[0];
    s_gpsTry = (uint8_t)((s_gpsTry + 1) % (pins * bauds));
    s_gpsPin  = kGpsPins[s_gpsTry % pins];
    s_gpsBaud = kGpsBauds[s_gpsTry / pins];
    s_gpsTryAt = now;
    Serial1.end();
    Serial1.setRxBufferSize(GPS_RX_BUF);
    Serial1.begin(s_gpsBaud, SERIAL_8N1, s_gpsPin, -1);
    Serial.printf("[gps] nothing heard: trying GPIO%d at %lu baud\n", (int)s_gpsPin, (unsigned long)s_gpsBaud);
}

// Where the Tab5 is: its own GPS fix when fresh, else HOME from the card.
bool herePos(uint32_t now, int32_t& lat7, int32_t& lon7, uint8_t& accM);

// A line to the USB log every 30 s: what the GPS is doing, readable on a PC
// even while the Tab5 takes no input there.
void gpsReport(uint32_t now) {
    static uint32_t last = 0;
    if (now - last < 30000) return;
    last = now;
    const Gnss::Fix& f = Gnss::fix();
    const Gnss::Sky k = Gnss::sky();
    Serial.printf("[gps] GPIO%d %lu baud: %lu good, %lu bad; in view %u, heard %u, used %u; %s\n",
                  (int)s_gpsPin, (unsigned long)s_gpsBaud, (unsigned long)Gnss::good(), (unsigned long)Gnss::bad(),
                  k.view, k.heard, f.used, Gnss::fresh(now) ? "FIX" : f.valid ? "fix lost" : "no fix");
}

// "51.47793" from degrees x 10^7.
void degText(int32_t v7, char* out, size_t cap) {
    const bool neg = v7 < 0;
    const uint32_t a = (uint32_t)(neg ? -(int64_t)v7 : v7);
    snprintf(out, cap, "%s%lu.%05lu", neg ? "-" : "", (unsigned long)(a / 10000000UL),
             (unsigned long)((a % 10000000UL) / 100UL));
}

// ---- the SD card ---------------------------------------------------------------
// Every catch is a row in /squachwatch-base/catches.csv, with the Tab5's
// position when its GPS has a fresh fix -- the file the map is drawn from.
// The card is Launcher's too; this folder is ours.
bool s_sdOk = false;
const char* CATCHES = "/squachwatch-base/catches.csv";

// Where the map tiles are. The intended place is /squachwatch-base/tiles, but
// a copy by hand lands them one folder off as often as not (an unzip adds the
// zip's own folder, or the folder goes inside itself), so the usual mistakes
// are looked for too -- the first that has a zoom-12 folder wins.
char s_tileRoot[96] = "/squachwatch-base/tiles";
bool s_tilesFound = false;
void findTiles() {
    static const char* const where[] = {
        "/squachwatch-base/tiles",
        "/tiles",
        "/squachwatch-base/squachwatch-base/tiles",
        "/squachwatch_tab5_map_tiles/squachwatch-base/tiles",
        "/squachwatch_tab5_map_tiles/tiles",
        "/copy_to_tab5_sd_card/squachwatch-base/tiles",
    };
    char probe[128];
    for (const char* w : where) {
        snprintf(probe, sizeof probe, "%s/12", w);
        if (SD_MMC.exists(probe)) {
            strncpy(s_tileRoot, w, sizeof s_tileRoot - 1);
            s_tilesFound = true;
            Serial.printf("[map] tiles at %s\n", s_tileRoot);
            return;
        }
    }
    s_tilesFound = false;
    Serial.println("[map] no tiles on the card");
}

void sdStart() {
    s_sdOk = SD_MMC.begin("/sdcard", false);
    Serial.printf("[sd] %s\n", s_sdOk ? "mounted" : "no card");
    if (!s_sdOk) return;
    if (!SD_MMC.exists("/squachwatch-base")) SD_MMC.mkdir("/squachwatch-base");
    if (!SD_MMC.exists(CATCHES)) {
        File f = SD_MMC.open(CATCHES, FILE_WRITE);
        if (f) { f.println("utc,type,board,rssi,device_tail,lat,lon,acc_m,source,event"); f.close(); }
    }
    findTiles();
}

void sdCatch(const char* type, const char* board, int rssi, const uint8_t tail[3], uint32_t now, const char* event) {
    if (!s_sdOk) return;
    File f = SD_MMC.open(CATCHES, FILE_APPEND);
    if (!f) return;
    char lat[16] = "", lon[16] = "", acc[8] = "", utc[24] = "";
    const uint32_t ep = Gnss::utcEpoch();
    if (ep) {
        time_t t = (time_t)ep; struct tm tmv; gmtime_r(&t, &tmv);
        strftime(utc, sizeof utc, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    }
    int32_t la, lo; uint8_t am;
    if (herePos(now, la, lo, am)) {
        degText(la, lat, sizeof lat);
        degText(lo, lon, sizeof lon);
        snprintf(acc, sizeof acc, "%u", am);
    }
    f.printf("%s,%s,%s,%d,%02X:%02X:%02X,%s,%s,%s,live,%s\n", utc, type, board, rssi,
             tail[2], tail[1], tail[0], lat, lon, acc, event);
    f.close();
}

// ---- pins for the map ---------------------------------------------------------
// Every catch with a position: those already in catches.csv when the card
// mounted, then each live one as it lands. The map is drawn from these.
struct Pin {
    int32_t  lat7, lon7;
    uint16_t colour;
    uint8_t  tier;         // tierForName(label): 0 attacks you ... 4 a beacon
    uint16_t dev;          // index into s_devs, or NO_DEV
    char     label[12];
};
constexpr uint16_t NO_DEV = 0xFFFF;
constexpr uint16_t PINS_N = 2000;
Pin*     s_pins = nullptr;                // in PSRAM
uint16_t s_pinN = 0;

// Types the map leaves off. The everyday trackers start hidden: AirTags and
// their kind change address every quarter hour, so one drive past a car park
// is hundreds of "devices" over the things worth seeing. Tapping a type in
// the CAPTURED panel flips it; the choice is kept.
constexpr uint8_t HIDE_N = 20;
char    s_hidden[HIDE_N][12];
uint8_t s_hiddenN = 0;
bool typeHidden(const char* t) {
    for (uint8_t i = 0; i < s_hiddenN; i++) if (!strcmp(s_hidden[i], t)) return true;
    return false;
}
void typeToggle(const char* t) {
    for (uint8_t i = 0; i < s_hiddenN; i++)
        if (!strcmp(s_hidden[i], t)) { memmove(s_hidden[i], s_hidden[i + 1], 12 * (s_hiddenN - i - 1)); s_hiddenN--; return; }
    if (s_hiddenN < HIDE_N) { strncpy(s_hidden[s_hiddenN], t, 11); s_hidden[s_hiddenN][11] = '\0'; s_hiddenN++; }
}

void pinAdd(int32_t lat7, int32_t lon7, const char* label, uint16_t dev = NO_DEV) {
    if (!s_pins) return;
    if (s_pinN == PINS_N) { memmove(&s_pins[0], &s_pins[1], sizeof(Pin) * (PINS_N - 1)); s_pinN--; }
    Pin& p = s_pins[s_pinN++];
    p.lat7 = lat7; p.lon7 = lon7; p.colour = colourForName(label); p.tier = tierForName(label); p.dev = dev;
    strncpy(p.label, label, sizeof p.label - 1); p.label[sizeof p.label - 1] = '\0';
}

// "51.4779300" -> degrees x 10^7, no floats.
bool parseDeg7(const char* s, int32_t& out) {
    if (!s || !*s) return false;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    int64_t whole = 0, frac = 0; int places = 0;
    while (*s >= '0' && *s <= '9') whole = whole * 10 + (*s++ - '0');
    if (*s == '.') { s++; while (*s >= '0' && *s <= '9' && places < 7) { frac = frac * 10 + (*s++ - '0'); places++; } }
    while (places < 7) { frac *= 10; places++; }
    const int64_t v = whole * 10000000LL + frac;
    out = (int32_t)(neg ? -v : v);
    return true;
}

// ---- home ------------------------------------------------------------------
// The Tab5 stays at home, so where it is needs no GPS: the first line of
// /squachwatch-base/home.txt on the card, "lat,lon" in degrees. A fresh fix
// of its own still wins, should it ever travel. The second line, if there
// is one, is the time zone as a POSIX TZ rule (PST8PDT,M3.2.0,M11.1.0 for
// North America's Pacific, CET-1CEST,M3.5.0,M10.5.0/3 for central Europe);
// the Nixie clock shows UTC without it. Kept on the card rather than in the
// source, so the place stays the owner's.
bool    s_homeOk = false;
int32_t s_homeLat7 = 0, s_homeLon7 = 0;

void loadHome() {
    if (!s_sdOk) return;
    File f = SD_MMC.open("/squachwatch-base/home.txt", FILE_READ);
    if (!f) { Serial.println("[home] no home.txt on the card"); return; }
    char line[64] = "", tz[64] = "";
    const size_t n = f.readBytesUntil('\n', line, sizeof line - 1);
    line[n] = '\0';
    const size_t m = f.readBytesUntil('\n', tz, sizeof tz - 1);
    tz[m] = '\0';
    f.close();
    for (char* t = tz + strlen(tz); t > tz && (t[-1] == '\r' || t[-1] == ' '); ) *--t = '\0';
    if (tz[0]) {
        setenv("TZ", tz, 1);
        tzset();
        Serial.printf("[home] time zone %s\n", tz);
    }
    char* comma = strchr(line, ',');
    int32_t la, lo;
    if (comma && parseDeg7(line, la) && parseDeg7(comma + 1 + strspn(comma + 1, " "), lo) && (la || lo)) {
        s_homeLat7 = la; s_homeLon7 = lo; s_homeOk = true;
        Serial.println("[home] position read from home.txt");
    } else {
        Serial.println("[home] home.txt is not \"lat,lon\"");
    }
}

bool herePos(uint32_t now, int32_t& lat7, int32_t& lon7, uint8_t& accM) {
    if (Gnss::fresh(now)) { lat7 = Gnss::fix().lat7; lon7 = Gnss::fix().lon7; accM = Gnss::fix().accM; return true; }
    if (s_homeOk)         { lat7 = s_homeLat7; lon7 = s_homeLon7; accM = 25; return true; }
    return false;
}

// ---- devices: one capture per device, not per sighting ---------------------------
// A device is its type and the last three bytes of its address -- what a
// HEADSUP carries. Seen again near where it was seen before, it is not a new
// capture: the sighting is counted, the feed gets a quiet note, and no pin is
// added (the Flock camera on the corner, passed every day). Seen somewhere
// new, it gets another pin. Seen at FOLLOW_PLACES different places within
// FOLLOW_WINDOW_S, it is FOLLOWING: it moved with the Tab5. Places only count
// with a fresh GPS fix; without one a repeat is just "seen again".
//
// Rebuilt from catches.csv at boot, so it survives a restart.
constexpr uint16_t DEVS_N          = 4000;   // AirTags change address: each change is a new device
constexpr uint8_t  PLACES_N        = 8;
constexpr int32_t  SAME_PLACE_M    = 150;
constexpr uint8_t  FOLLOW_PLACES   = 3;
constexpr uint32_t FOLLOW_WINDOW_S = 2 * 3600;

struct Dev {
    char     type[12];
    uint8_t  key[3];               // the address tail, as printed: ..AA:BB:CC
    uint16_t sightings;
    uint8_t  nPlaces;              // ring of the last PLACES_N places
    int32_t  lat7[PLACES_N], lon7[PLACES_N];
    uint32_t at[PLACES_N];         // UTC of the first sighting there; 0 unknown
    bool     following;
};
Dev*     s_devs = nullptr;         // in PSRAM
uint16_t s_devN = 0;
uint32_t s_sightings = 0;

enum class Seen : uint8_t { NEW, AGAIN, ELSEWHERE, FOLLOWING };
const char* seenName(Seen s) {
    switch (s) { case Seen::NEW: return "new"; case Seen::AGAIN: return "again";
                 case Seen::ELSEWHERE: return "elsewhere"; default: return "following"; }
}

int32_t metresApart(int32_t la1, int32_t lo1, int32_t la2, int32_t lo2) {
    const double dy = (la2 - la1) * 1e-7 * 111320.0;
    const double dx = (lo2 - lo1) * 1e-7 * 111320.0 * cos(la1 * 1e-7 * M_PI / 180.0);
    return (int32_t)sqrt(dx * dx + dy * dy);
}

// Where this sighting puts the device. `idx` is its slot, NO_DEV when the
// list is full (it is still logged, just not tracked).
Seen classify(const char* type, const uint8_t key[3], bool havePos, int32_t la, int32_t lo,
              uint32_t epoch, uint16_t& idx) {
    s_sightings++;
    Dev* d = nullptr;
    for (uint16_t i = 0; i < s_devN; i++)
        if (!memcmp(s_devs[i].key, key, 3) && !strcmp(s_devs[i].type, type)) { d = &s_devs[i]; idx = i; break; }
    if (!d) {
        idx = NO_DEV;
        if (!s_devs || s_devN == DEVS_N) return Seen::NEW;
        idx = s_devN;
        d = &s_devs[s_devN++];
        memset(d, 0, sizeof *d);
        strncpy(d->type, type, sizeof d->type - 1);
        memcpy(d->key, key, 3);
        d->sightings = 1;
        if (havePos) { d->lat7[0] = la; d->lon7[0] = lo; d->at[0] = epoch; d->nPlaces = 1; }
        return Seen::NEW;
    }
    if (d->sightings < 0xFFFF) d->sightings++;
    if (!havePos) return Seen::AGAIN;
    const uint8_t n = d->nPlaces < PLACES_N ? d->nPlaces : PLACES_N;
    for (uint8_t i = 0; i < n; i++)
        if (metresApart(d->lat7[i], d->lon7[i], la, lo) < SAME_PLACE_M) return Seen::AGAIN;
    const uint8_t slot = d->nPlaces % PLACES_N;
    d->lat7[slot] = la; d->lon7[slot] = lo; d->at[slot] = epoch;
    if (d->nPlaces < 0xFF) d->nPlaces++;
    // Distinct places inside the window. A place with no time (no GPS date
    // yet) counts: better a follower called early than missed.
    uint8_t recent = 0;
    for (uint8_t i = 0; i < (d->nPlaces < PLACES_N ? d->nPlaces : PLACES_N); i++)
        if (!epoch || !d->at[i] || epoch - d->at[i] <= FOLLOW_WINDOW_S) recent++;
    if (recent >= FOLLOW_PLACES) { d->following = true; return Seen::FOLLOWING; }
    return Seen::ELSEWHERE;
}

uint8_t hexByte(const char* h) {
    auto v = [](char c) -> uint8_t { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                          : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0; };
    return (uint8_t)(v(h[0]) << 4 | v(h[1]));
}

// "2026-10-16T14:05:09Z" -> UTC seconds, 0 if it is not one.
uint32_t utcFromText(const char* t) {
    int y, mo, d, h, mi, se;
    if (sscanf(t, "%d-%d-%dT%d:%d:%dZ", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
    return Gnss::toEpoch(y, mo, d, h, mi, se);
}

// The card's history replayed through the same rules, silently: devices,
// sightings and pins as they were before the restart.
// One CSV in the catches format, replayed through the same rules.
void loadCatchesCsv(const char* path) {
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return;
    char line[180];
    f.readBytesUntil('\n', line, sizeof line);            // the header
    uint32_t rows = 0;
    while (f.available()) {
        const size_t n = f.readBytesUntil('\n', line, sizeof line - 1);
        line[n] = '\0';
        // utc,type,board,rssi,device_tail,lat,lon,acc_m,source[,event]
        char* col[10] = {}; uint8_t c = 0; col[c++] = line;
        for (char* q = line; *q && c < 10; q++) if (*q == ',') { *q = '\0'; col[c++] = q + 1; }
        if (c < 7 || strlen(col[4]) < 8) continue;
        const uint8_t key[3] = { hexByte(col[4]), hexByte(col[4] + 3), hexByte(col[4] + 6) };
        int32_t la = 0, lo = 0;
        const bool havePos = parseDeg7(col[5], la) && parseDeg7(col[6], lo);
        uint16_t idx;
        const Seen sn = classify(col[1], key, havePos, la, lo, utcFromText(col[0]), idx);
        if (havePos && sn != Seen::AGAIN) pinAdd(la, lo, col[1], idx);
        rows++;
        if ((rows & 255) == 0) delay(1);                   // a big import: let the radio task breathe
    }
    f.close();
    Serial.printf("[map] %s: %lu rows\n", path, (unsigned long)rows);
}

// A board's own SD log, copied across as it is: squachwatch-YYYYMMDD.log,
// one row a catch -- time, type, rssi, mac, channel, vendor, name, and on a
// GPS board lat, lon, accuracy (sd_row.h). The whole history, where the
// black box keeps only the newest 180 (2026-10-19: a long day's catches had
// rolled out of it). Commas in names are already dots, so
// the columns are fixed. A bench fix (accuracy FAKE) gets no pin.
// One row of a board's log, edited in place. 0 not a row, 1 a row, 2 a row
// that put a pin on the map.
uint8_t boardLogRow(char* line) {
    size_t n = strlen(line);
    if (n && line[n - 1] == '\r') line[--n] = '\0';
    char* col[12] = {}; uint8_t c = 0; col[c++] = line;
    for (char* q = line; *q && c < 12; q++) if (*q == ',') { *q = '\0'; col[c++] = q + 1; }
    if (c < 7 || strlen(col[3]) < 17) return 0;
    const char* mac = col[3];             // AA:BB:CC:DD:EE:FF
    const uint8_t key[3] = { hexByte(mac + 15), hexByte(mac + 12), hexByte(mac + 9) };
    int32_t la = 0, lo = 0;
    const bool havePos = c >= 10 && strcmp(col[9], "FAKE") != 0
                      && parseDeg7(col[7], la) && parseDeg7(col[8], lo) && (la || lo);
    uint16_t idx;
    const Seen sn = classify(col[1], key, havePos, la, lo, utcFromText(col[0]), idx);
    if (havePos && sn != Seen::AGAIN) { pinAdd(la, lo, col[1], idx); return 2; }
    return 1;
}

void loadBoardLog(const char* path) {
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return;
    char line[200];
    uint32_t rows = 0, placed = 0;
    while (f.available()) {
        const size_t n = f.readBytesUntil('\n', line, sizeof line - 1);
        line[n] = '\0';
        const uint8_t r = boardLogRow(line);
        if (!r) continue;
        if (r == 2) placed++;
        rows++;
        if ((rows & 255) == 0) delay(1);
    }
    f.close();
    Serial.printf("[map] %s: %lu rows, %lu pins\n", path, (unsigned long)rows, (unsigned long)placed);
}

// The Tab5's own history, then every board's log copied into
// /squachwatch-base/import/ -- a board's catches from where the Tab5 was not
// (tools: the board's BLACKBOX dump, converted to this format). Read at
// every start rather than merged into catches.csv, so copying a file in
// twice cannot count it twice.
void sdLoadDevices() {
    if (!s_sdOk || !s_pins || !s_devs) return;
    loadCatchesCsv(CATCHES);
    // The import folder, and -- because not every card reader makes making
    // a folder easy -- any other .csv dropped in squachwatch-base itself,
    // beside catches.csv.
    static const char* const dirs[] = { "/squachwatch-base/import", "/squachwatch-base" };
    for (const char* d : dirs) {
        File dir = SD_MMC.open(d);
        if (!dir || !dir.isDirectory()) continue;
        char path[160];
        for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
            const char* nm = e.name();
            const char* base = strrchr(nm, '/') ? strrchr(nm, '/') + 1 : nm;
            const size_t n = strlen(base);
            const bool csv = !e.isDirectory() && n > 4 && !strcasecmp(base + n - 4, ".csv");
            const bool blog = !e.isDirectory() && n > 4 && !strcasecmp(base + n - 4, ".log")
                           && strstr(base, "squachwatch-");     // also NAME_squachwatch-... from a sync
            snprintf(path, sizeof path, "%s/%s", d, base);
            e.close();
            if (csv && strcmp(path, CATCHES) != 0) loadCatchesCsv(path);
            else if (blog)                        loadBoardLog(path);
        }
    }
    Serial.printf("[map] %u devices, %lu sightings, %u pins from the card\n",
                  s_devN, (unsigned long)s_sightings, s_pinN);
}

// ---- the boards ------------------------------------------------------------
struct Board {
    bool     live;
    uint8_t  mac[6];
    char     name[SquachMesh::NAME_LEN + 1];
    bool     phone;
    uint8_t  phoneId[SquachMesh::PHONE_ID_LEN];
    int8_t   rssi;
    uint32_t lastMs;
    uint32_t frames;       // squad frames heard, sealed or not
    uint32_t badTag;       // ...that did not open under our key
    bool     squad;        // one of its frames opened: it is in our squad
    bool     haveCtr;
    uint32_t lastCtr;      // replay: the last counter acted on
    uint8_t  ver[3];
    bool     haveFix;
    int32_t  lat7, lon7;
    uint32_t catches;      // HEADSUPs it has sent us
    bool     hasLook;      // a presence advert told us what it is wearing:
    uint8_t  outfit, shade;
    bool     aura;
    bool     present;      // on the WHO'S HERE stage
    uint32_t arriveMs;
};
constexpr uint8_t BOARDS_N = 12;
Board s_boards[BOARDS_N];

Board* boardFor(const uint8_t mac[6], uint32_t now) {
    Board* freeSlot = nullptr;
    Board* oldest = &s_boards[0];
    for (auto& b : s_boards) {
        if (b.live && memcmp(b.mac, mac, 6) == 0) return &b;
        if (!b.live && !freeSlot) freeSlot = &b;
        if (b.live && b.lastMs < oldest->lastMs) oldest = &b;
    }
    Board* b = freeSlot ? freeSlot : oldest;
    memset(b, 0, sizeof *b);
    b->live = true;
    memcpy(b->mac, mac, 6);
    strcpy(b->name, "?");
    b->lastMs = now;
    return b;
}

// The six bytes a frame from this board is sealed under.
void nonceMac(const Board& b, bool reversed, uint8_t out[6]) {
    if (b.phone) { SquachMesh::phoneMac(b.phoneId, out); return; }
    for (uint8_t i = 0; i < 6; i++) out[i] = reversed ? b.mac[5 - i] : b.mac[i];
}

// ---- the feed --------------------------------------------------------------
enum class FeedKind : uint8_t { CATCH, MESSAGE, NOTE };
struct FeedItem {
    FeedKind kind;
    uint32_t at;
    uint16_t colour;
    char     title[40];
    char     body[80];
    bool     havePos;      // the Tab5's GPS had a fresh fix when it landed
    int32_t  lat7, lon7;
};
constexpr uint8_t FEED_N = 24;
FeedItem s_feed[FEED_N];
uint8_t  s_feedN = 0;          // newest at index 0
uint32_t s_catches = 0;
bool     s_dirty = true;
bool     s_mapDirty = true;            // the map needs drawing again

void feedAdd(FeedKind k, uint16_t colour, const char* title, const char* body, uint32_t now) {
    if (s_feedN < FEED_N) s_feedN++;
    memmove(&s_feed[1], &s_feed[0], sizeof(FeedItem) * (s_feedN - 1));
    FeedItem& f = s_feed[0];
    f.kind = k; f.at = now; f.colour = colour;
    strncpy(f.title, title, sizeof f.title - 1); f.title[sizeof f.title - 1] = '\0';
    strncpy(f.body, body, sizeof f.body - 1);    f.body[sizeof f.body - 1] = '\0';
    uint8_t am;
    f.havePos = herePos(now, f.lat7, f.lon7, am);
    s_dirty = true;
}

// ---- settings, kept in Preferences (the SETTINGS screen) ----
const uint8_t  BRIGHT_LEVELS[] = { 60, 120, 180, 255 };
const char*    BRIGHT_NAMES[]  = { "25%", "50%", "75%", "100%" };
const uint8_t  VOL_LEVELS[]    = { 60, 140, 220 };
const char*    VOL_NAMES[]     = { "LOW", "MEDIUM", "HIGH" };
const char*    SOUND_NAMES[]   = { "ON", "QUIET AT NIGHT (11 PM - 7 AM)", "OFF" };
uint8_t s_bright = 2, s_vol = 1, s_sound = 0;
bool    s_clock24 = false;                 // the Nixie clock: 24-hour, or 12
bool    s_clockSet = false;                // the system clock holds real time (s_timeOk, later)

// Night by the local clock, once it is set.
bool nightNow() {
    if (!s_clockSet) return false;
    time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
    return lt.tm_hour >= 23 || lt.tm_hour < 7;
}

// FOLLOWING: three high beeps, hard to miss and nothing like a catch's chirp.
// Only SOUND OFF silences it -- not the night.
void alarm() {
    if (s_sound == 2) return;
    for (int i = 0; i < 3; i++) { M5.Speaker.tone(2637, 140); delay(220); }
}

void chirp(bool loud) {
    if (s_sound == 2 || (s_sound == 1 && nightNow())) return;
    if (loud) { M5.Speaker.tone(1760, 90); delay(110); M5.Speaker.tone(2349, 140); }
    else        M5.Speaker.tone(1318, 60);
}

// ---- opening a frame ------------------------------------------------------------
MeshMsg::Assembly s_asm;

bool fresh(const Board& b, uint32_t ctr) { return !b.haveCtr || ctr > b.lastCtr; }
void record(Board& b, uint32_t ctr) { b.haveCtr = true; b.lastCtr = ctr; }

// Tries the frame under each byte order still possible and returns what the
// last attempt said. `nm` is the address it opened under, for the assembly.
template <typename F>
MeshMsg::Open tryOpen(Board& b, F open, uint8_t nm[6]) {
    MeshMsg::Open r = MeshMsg::Open::BAD_TAG;
    for (uint8_t rev = 0; rev < 2; rev++) {
        if (b.phone && rev) break;                       // a phone's id has one order
        if (!b.phone && s_macOrder >= 0 && rev != s_macOrder) continue;
        nonceMac(b, rev, nm);
        r = open(nm);
        if (r == MeshMsg::Open::OK) {
            if (!b.phone && s_macOrder < 0) {
                s_macOrder = rev;
                Serial.printf("[base] nonce address order settled: %s\n", rev ? "reversed" : "as given");
            }
            return r;
        }
    }
    return r;
}

void openFrame(Board& b, const uint8_t* p, size_t n, uint32_t now) {
    uint32_t ctr = 0; uint8_t kind = 0;
    if (!MeshMsg::parseHeader(p, n, ctr, kind)) return;
    b.frames++;
    if (!s_haveKey || !s_cryptoOk) return;
    if (kind == MeshMsg::KIND_INVITE_PUB || kind == MeshMsg::KIND_INVITE_KEY) return;
    if (!fresh(b, ctr)) return;                         // a repeat: every frame comes dozens of times
    const auto& C = MeshCrypto::impl();
    uint8_t nm[6];
    char title[40], body[80];
    MeshMsg::Open r = MeshMsg::Open::BAD_FORMAT;

    switch (kind) {
    case MeshMsg::KIND_HEADSUP: {
        uint8_t type = 0, tail[3] = {}; int8_t rssi = 0;
        r = tryOpen(b, [&](const uint8_t* m) { return MeshMsg::openHeadsUp(C, m, p, n, ctr, type, rssi, tail); }, nm);
        if (r != MeshMsg::Open::OK) break;
        record(b, ctr);
        b.catches++; s_catches++;
        const char* tname = detectionTypeName((DetectionType)type);
        const uint8_t key[3] = { tail[2], tail[1], tail[0] };
        int32_t la = 0, lo = 0; uint8_t am;
        const bool havePos = herePos(now, la, lo, am);
        uint16_t idx;
        const Seen sn = classify(tname, key, havePos, la, lo, Gnss::utcEpoch(), idx);
        const Dev* d = idx != NO_DEV ? &s_devs[idx] : nullptr;
        sdCatch(tname, b.name, rssi, tail, now, seenName(sn));
        if (havePos && sn != Seen::AGAIN) { pinAdd(la, lo, tname, idx); s_mapDirty = true; }
        switch (sn) {
        case Seen::NEW:
            snprintf(title, sizeof title, "%s", tname);
            snprintf(body, sizeof body, "caught by %s   %d dBm   device ..%02X:%02X:%02X",
                     b.name, (int)rssi, tail[2], tail[1], tail[0]);
            feedAdd(FeedKind::CATCH, typeColour(type), title, body, now);
            chirp(true);
            break;
        case Seen::AGAIN:
            snprintf(title, sizeof title, "%s seen again", tname);
            snprintf(body, sizeof body, "..%02X:%02X:%02X is on the list: %u sightings",
                     tail[2], tail[1], tail[0], d ? d->sightings : 0);
            feedAdd(FeedKind::NOTE, DIM, title, body, now);
            M5.Speaker.tone(988, 40);
            break;
        case Seen::ELSEWHERE:
            snprintf(title, sizeof title, "%s AGAIN, NEW PLACE", tname);
            snprintf(body, sizeof body, "..%02X:%02X:%02X now seen at %u places   caught by %s",
                     tail[2], tail[1], tail[0], d ? d->nPlaces : 0, b.name);
            feedAdd(FeedKind::CATCH, typeColour(type), title, body, now);
            chirp(true);
            break;
        case Seen::FOLLOWING:
            snprintf(title, sizeof title, "FOLLOWING YOU: %s", tname);
            snprintf(body, sizeof body, "..%02X:%02X:%02X seen at %u places in 2 hours   caught by %s",
                     tail[2], tail[1], tail[0], d ? d->nPlaces : 0, b.name);
            feedAdd(FeedKind::CATCH, ALERT, title, body, now);
            alarm();
            break;
        }
        Serial.printf("[base] HEADSUP %s from %s at %d dBm: %s\n", tname, b.name, (int)rssi, seenName(sn));
        break;
    }
    case MeshMsg::KIND_CANNED: {
        uint8_t line = 0;
        r = tryOpen(b, [&](const uint8_t* m) { return MeshMsg::openCanned(C, m, p, n, ctr, line); }, nm);
        if (r != MeshMsg::Open::OK) break;
        record(b, ctr);
        snprintf(title, sizeof title, "%s", b.name);
        snprintf(body, sizeof body, "%s", line < MeshMsg::CANNED_N ? MeshMsg::CANNED[line] : "(a newer line)");
        feedAdd(FeedKind::MESSAGE, INFO, title, body, now);
        chirp(false);
        break;
    }
    case MeshMsg::KIND_TEXT: {
        uint8_t part = 0, total = 0;
        char chars[MeshMsg::TEXT_PART_CHARS + 1];
        r = tryOpen(b, [&](const uint8_t* m) { return MeshMsg::openTextPart(C, m, p, n, ctr, part, total, chars); }, nm);
        if (r != MeshMsg::Open::OK) break;
        char text[MeshMsg::TEXT_MAX + 1]; uint32_t base = 0;
        if (!s_asm.add(nm, ctr, part, total, chars, text, base)) break;   // more parts to come
        record(b, base + total - 1);
        snprintf(title, sizeof title, "%s", b.name);
        snprintf(body, sizeof body, "%s", text);
        feedAdd(FeedKind::MESSAGE, INFO, title, body, now);
        chirp(false);
        break;
    }
    case MeshMsg::KIND_EMOTE: {
        uint8_t em = 0, setup = 0;
        r = tryOpen(b, [&](const uint8_t* m) { return MeshMsg::openEmote(C, m, p, n, ctr, em, setup); }, nm);
        if (r != MeshMsg::Open::OK && r != MeshMsg::Open::UNKNOWN_LINE) break;
        record(b, ctr);
        snprintf(title, sizeof title, "%s", b.name);
        feedAdd(FeedKind::NOTE, DIM, title, "sent an emote", now);
        break;
    }
    case MeshMsg::KIND_HELLO: {
        // Not recorded, like on a board: a fact about the sender, repeated.
        uint8_t ver[3] = {}; uint32_t epoch = 0; uint8_t zone = 0;
        r = tryOpen(b, [&](const uint8_t* m) { return MeshMsg::openHello(C, m, p, n, ctr, ver, epoch, zone); }, nm);
        if (r == MeshMsg::Open::OK) memcpy(b.ver, ver, 3);
        break;
    }
    case MeshMsg::KIND_FIX: {
        int32_t la = 0, lo = 0; uint8_t acc = 0;
        r = tryOpen(b, [&](const uint8_t* m) { return MeshMsg::openFix(C, m, p, n, ctr, la, lo, acc); }, nm);
        if (r == MeshMsg::Open::OK) { b.haveFix = true; b.lat7 = la; b.lon7 = lo; }
        break;
    }
    default:
        // READ, NUDGE, WIFI, UPDATED: squad housekeeping with nothing to show.
        // Opened only to learn whether the sender is in our squad.
        {
            uint32_t mc = 0;
            if (kind == MeshMsg::KIND_READ)
                r = tryOpen(b, [&](const uint8_t* m) { return MeshMsg::openRead(C, m, p, n, ctr, mc); }, nm);
        }
        break;
    }

    if (r == MeshMsg::Open::OK || r == MeshMsg::Open::UNKNOWN_LINE) {
        if (!b.squad) { b.squad = true; s_dirty = true; }
    } else if (r == MeshMsg::Open::BAD_TAG) {
        b.badTag++;
    }
}

void handle(const Heard& h, uint32_t now) {
    const uint8_t* p = h.data + 2;
    const size_t   n = h.len - 2;
    Board* b = boardFor(h.mac, now);
    b->rssi = h.rssi;
    b->lastMs = now;
    if (MeshMsg::isFrame(p, n)) { openFrame(*b, p, n, now); return; }
    SquachMesh::Peer peer;
    if (!SquachMesh::decode(p, n, peer)) return;
    const char* nm = (peer.custom && peer.name[0]) ? peer.name : NICKNAMES[peer.nick % NICKNAMES_N];
    if (strcmp(b->name, nm) != 0) s_dirty = true;
    strncpy(b->name, nm, SquachMesh::NAME_LEN);
    b->name[SquachMesh::NAME_LEN] = '\0';
    b->phone = peer.phone;
    if (peer.phone) memcpy(b->phoneId, peer.id, sizeof b->phoneId);
    if (!b->hasLook || b->outfit != peer.outfit || b->shade != peer.shade || b->aura != peer.aura) s_dirty = true;
    b->hasLook = true;
    b->outfit = peer.outfit % SquachMesh::OUTFIT_N;
    b->shade  = peer.shade % SquachMesh::SHADE_N;
    b->aura   = peer.aura;
    if (!b->present) {
        b->present = true;
        b->arriveMs = now;
        char t[48], body[64];
        snprintf(t, sizeof t, "%s IS HERE", b->name);
        snprintf(body, sizeof body, "Came into range, %d dBm", b->rssi);
        feedAdd(FeedKind::NOTE, INFO, t, body, now);
        chirp(false);
    }
}

// A board not heard for a minute has gone: off the stage, with a note.
// Presence repeats within 13 s, so a minute is several missed in a row.
constexpr uint32_t GONE_MS = 60000;
void presenceTick(uint32_t now) {
    for (auto& b : s_boards) {
        if (!b.live || !b.present || now - b.lastMs < GONE_MS) continue;
        b.present = false;
        char t[48];
        snprintf(t, sizeof t, "%s LEFT", b.name);
        feedAdd(FeedKind::NOTE, DIM, t, "Not heard for a minute", now);
    }
}

// ---- touch buttons -----------------------------------------------------------
struct Button { int16_t x, y, w, h; uint16_t id; };
constexpr uint8_t BUTTONS_N = 64;
Button  s_buttons[BUTTONS_N];
uint8_t s_buttonN = 0;
void addButton(int x, int y, int w, int h, uint16_t id) {
    if (s_buttonN < BUTTONS_N) s_buttons[s_buttonN++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, id };
}
int hit(int x, int y) {
    for (uint8_t i = 0; i < s_buttonN; i++) {
        const Button& b = s_buttons[i];
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return b.id;
    }
    return -1;
}

// ---- touch, read in its own task ------------------------------------------------
// The panel used to be read from the drawing loop, between the steps of a
// frame; a quick tap could start and end inside one step and was never seen
// (2026-10-20: twenty taps on the clock to land five). Now a task reads it a
// hundred times a second whatever the screen is doing: where a finger is, and
// every touch-down queued for the loop. The I2C bus it shares with the power
// chip and the clock is taken under s_busLock.
//
// v31 (2026-10-21: three taps in four missed, and BACK on the map moved the
// map): a tap is now counted on the LIFT, where the finger settled, with
// where it landed kept too -- the panel's first report of a touch is often
// off, and a button is pressed if either point is on it. A finger lost for a
// read or two is the same touch (40 ms bridge), not a new one; before, every
// dropout queued a fresh touch-down at some point along a drag, and the
// map's next touch started from there.
SemaphoreHandle_t s_busLock = nullptr;
struct TouchNow { volatile bool down; volatile int16_t x, y; };
TouchNow s_touch = { false, 0, 0 };
volatile int16_t  s_downX = 0, s_downY = 0;        // where the current touch landed
volatile uint32_t s_downAt = 0;
struct TapEv { int16_t x, y, dx, dy; uint32_t at; };   // lifted at x,y; landed at dx,dy
TapEv    s_tapQ[8];
volatile uint8_t s_tapQHead = 0, s_tapQTail = 0;

// Touch calibration: screen = a * panel + b, per axis, from five taps on
// targets (Settings > TOUCH). Identity until calibrated.
float s_calAx = 1, s_calBx = 0, s_calAy = 1, s_calBy = 0;
volatile bool s_calRaw = false;            // while calibrating: the panel's own numbers
void calApply(int& x, int& y) {
    if (s_calRaw) return;
    x = (int)lroundf(s_calAx * x + s_calBx);
    y = (int)lroundf(s_calAy * y + s_calBy);
    if (x < 0) x = 0; if (x > 1279) x = 1279;
    if (y < 0) y = 0; if (y > 719)  y = 719;
}

void busTake() { if (s_busLock) xSemaphoreTake(s_busLock, portMAX_DELAY); }
void busGive() { if (s_busLock) xSemaphoreGive(s_busLock); }

void touchTask(void*) {
    bool touching = false;
    int16_t lx = 0, ly = 0;
    uint32_t lastSeen = 0;
    for (;;) {
        busTake();
        M5.update();
        const auto d = M5.Touch.getDetail();
        const bool p = M5.Touch.getCount() > 0 && d.isPressed();
        busGive();
        const uint32_t now = millis();
        if (p) {
            int x = d.x, y = d.y;
            calApply(x, y);
            if (!touching) { touching = true; s_downX = x; s_downY = y; s_downAt = now ? now : 1; }
            lx = x; ly = y; lastSeen = now;
            s_touch.x = x; s_touch.y = y;
        }
        s_touch.down = p;
        if (!p && touching && now - lastSeen >= 40) {       // lifted, not a dropout
            touching = false;
            const uint8_t n = (uint8_t)((s_tapQHead + 1) % 8);
            if (n != s_tapQTail) { s_tapQ[s_tapQHead] = { lx, ly, s_downX, s_downY, now }; s_tapQHead = n; }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void touchStart() {
    s_busLock = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(touchTask, "touch", 4096, nullptr, 3, nullptr, 0);
}

// The next tap the task saw, for the loop: kept in s_tapX/Y (where it lifted)
// and s_tapDX/DY (where it landed) until used. One more than 2 s old is dropped.
int16_t s_tapX = -1, s_tapY = -1, s_tapDX = -1, s_tapDY = -1;
void touchPoll() {
    while (s_tapX < 0 && s_tapQTail != s_tapQHead) {
        const TapEv e = s_tapQ[s_tapQTail];
        s_tapQTail = (uint8_t)((s_tapQTail + 1) % 8);
        if (millis() - e.at > 2000) continue;
        s_tapX = e.x; s_tapY = e.y; s_tapDX = e.dx; s_tapDY = e.dy;
    }
}
void tapsClear() { s_tapQTail = s_tapQHead; s_tapX = s_tapY = -1; }

enum : uint16_t { BTN_PHRASE = 1, BTN_MAP = 2, BTN_MAP_BACK = 3, BTN_ZOOM_IN = 4, BTN_ZOOM_OUT = 5,
                  BTN_ME = 6, BTN_TYPE = 40, BTN_KEY = 100, BTN_SPACE = 300, BTN_DEL, BTN_CLEAR,
                  BTN_CANCEL, BTN_SAVE, BTN_LIGHT, BTN_SETTINGS, BTN_SET_BACK, BTN_NIXIE,
                  BTN_SET_ROW = 500, BTN_NET = 600, BTN_W_BACK = 620, BTN_W_SCAN, BTN_W_SHIFT, BTN_W_SYM,
                  BTN_W_SPACE, BTN_W_DEL, BTN_W_CANCEL, BTN_W_JOIN, BTN_SUGGEST = 1000 };

// ---- screens ---------------------------------------------------------------
M5Canvas s_canvas(&M5.Display);

// ---- a fast way onto the panel -------------------------------------------------
// The panel is portrait (720 x 1280, RGB565 in plain byte order) and the
// picture landscape (1280 x 720, a sprite's swapped RGB565), so every pixel
// is turned and byte-swapped on the way. Through M5GFX that went a pixel at a
// time and took most of a 779 ms frame (2026-10-20). Here it goes in 32 x 32
// blocks through a small buffer in fast RAM: rows read straight across, rows
// written straight across, into the panel's own framebuffer. M5GFX at
// rotation 1 puts landscape (X, Y) at portrait (719 - Y, X) -- the mapping
// SquachWatch's own Tab5 build found -- and a pixel drawn by M5GFX at start-up
// checks it on this panel; if it does not match, the old way is used.
uint16_t* s_fb = nullptr;
bool      s_fbOk = false;

void fastBlitBegin() {
    auto* pnl = static_cast<lgfx::Panel_DSI*>(M5.Display.getPanel());
    if (!pnl) return;
    s_fb = static_cast<uint16_t*>(pnl->config_detail().buffer);
    if (!s_fb) return;
    // the check: one pixel by M5GFX, read back where this code would put it
    const uint16_t c = 0x1234;
    M5.Display.drawPixel(5, 7, c);
    M5.Display.display();
    const uint16_t got = s_fb[(size_t)5 * 720 + (719 - 7)];
    s_fbOk = got == c || got == (uint16_t)((c >> 8) | (c << 8));
    if (s_fbOk && got != c) s_fbOk = false;                  // byte order not as expected: stay safe
    Serial.printf("[blit] framebuffer %p, test pixel %04x -> %s\n", s_fb, got, s_fbOk ? "fast copy on" : "M5GFX copy");
}

static uint16_t s_tile[32 * 32];
void blitFrom(const uint16_t* src, int srcW, int sx, int sy, int dx, int dy, int w, int h);

// Copies the landscape rectangle of s_canvas to the panel.
void blitRect(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 1280) w = 1280 - x;
    if (y + h > 720)  h = 720 - y;
    if (w <= 0 || h <= 0) return;
    if (!s_fbOk) {
        M5.Display.setClipRect(x, y, w, h);
        s_canvas.pushSprite(0, 0);
        M5.Display.clearClipRect();
        return;
    }
    blitFrom(static_cast<const uint16_t*>(s_canvas.getBuffer()), 1280, x, y, x, y, w, h);
}

// The general case: the w x h rectangle at (sx, sy) of a landscape picture
// srcW wide goes to the screen at (dx, dy) (already on screen; the caller
// clips).
void blitFrom(const uint16_t* src, int srcW, int sx, int sy, int dx, int dy, int w, int h) {
    for (int oy = 0; oy < h; oy += 32) {
        const int th = (h - oy) < 32 ? (h - oy) : 32;
        for (int ox = 0; ox < w; ox += 32) {
            const int tw = (w - ox) < 32 ? (w - ox) : 32;
            for (int r = 0; r < th; r++)
                memcpy(&s_tile[r * 32], src + (size_t)(sy + oy + r) * srcW + sx + ox, (size_t)tw * 2);
            for (int c = 0; c < tw; c++) {
                uint16_t* d = s_fb + (size_t)(dx + ox + c) * 720 + (719 - (dy + oy));
                const uint16_t* t = &s_tile[c];
                for (int r = 0; r < th; r++, t += 32) d[-r] = __builtin_bswap16(*t);
            }
        }
    }
    M5.Display.display(dx, dy, w, h);
}

void pushCanvas() { blitRect(0, 0, 1280, 720); }

bool     s_bleOk = false;
uint32_t s_bootMs = 0;
enum class Screen : uint8_t { MAIN, KEYBOARD, UNLOCKING, MAP, SETTINGS, TOUCHTEST, WIFILIST, WIFIPASS, CALIBRATE };
Screen   s_screen = Screen::MAIN;

void button(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t ink, uint16_t id,
            const lgfx::IFont* font = &fonts::FreeSansBold12pt7b) {
    s_canvas.fillRoundRect(x, y, w, h, 10, fill);
    s_canvas.setTextColor(ink, fill);
    s_canvas.setFont(font);
    s_canvas.setTextDatum(middle_center);
    s_canvas.drawString(label, x + w / 2, y + h / 2);
    s_canvas.setTextDatum(top_left);
    addButton(x, y, w, h, id);
}

void agoText(uint32_t ms, char* out, size_t cap) {
    const uint32_t s = ms / 1000;
    if (s < 60)        snprintf(out, cap, "%lus ago", (unsigned long)s);
    else if (s < 3600) snprintf(out, cap, "%lum ago", (unsigned long)(s / 60));
    else               snprintf(out, cap, "%luh ago", (unsigned long)(s / 3600));
}

// ---- WHO'S HERE: each board in range as its Squachy ---------------------------
// The pictures are the boards' own drawing code, rendered once per look on a
// PC (outfit x shade x aura, 160 of them) into /squachwatch-base/squachy/ on
// the card: squachy_oNN_sS_aA.png, 220 x 300, transparent, every one standing
// on the same ground line. Each board's picture is decoded once into a small
// sprite and only again when its look or the stage's size changes, so the
// once-a-second redraw costs a copy, not a PNG decode.
constexpr uint16_t LOOK_KEY = 0x0120;
// What changed this frame, so only that goes to the panel (2026-10-20: a
// whole-screen push every frame showed as a wave across it, and took most of
// the frame). The whole screen still goes once a second.
struct DirtyRect { int16_t x, y, w, h; };
DirtyRect s_dirtyNow[16], s_dirtyPrev[16];
uint8_t   s_dirtyNowN = 0, s_dirtyPrevN = 0;
void noteDirty(int x, int y, int w, int h) {
    if (s_dirtyNowN < 16) s_dirtyNow[s_dirtyNowN++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
}
// The dance party: three or more Squachies in at once (or five quick taps on
// the Nixie clock, for half a minute). See partyBack() / partyFront().
bool     s_party = false;
uint32_t s_frameMsShown = 0;               // the basement's last frame, for ABOUT
uint32_t s_pushMsShown = 0;                // ...of which, copying it to the panel
bool     s_sceneFull = true;                // the next frame goes to the panel whole
uint32_t s_partyAt = 0, s_partyForceUntil = 0;      // the sprites' see-through colour
M5Canvas* s_lookSpr[BOARDS_N];
M5Canvas* s_lookSprL[BOARDS_N];            // the same picture facing left, made once
uint16_t  s_lookId[BOARDS_N];              // look + size the sprite holds; 0 = none
bool      s_lookOk[BOARDS_N];              // the picture was on the card

const M5Canvas* lookSprite(uint8_t slot, const Board& b, float scale) {
    const uint16_t id = 1 + (b.outfit | b.shade << 5 | (b.aura ? 1 : 0) << 7 | (uint16_t)(scale * 4 + 0.5f) << 8);
    if (!s_lookSpr[slot]) {
        s_lookSpr[slot] = new M5Canvas(&s_canvas);
        s_lookSpr[slot]->setPsram(true);
        s_lookSpr[slot]->setColorDepth(16);
    }
    M5Canvas* c = s_lookSpr[slot];
    if (s_lookId[slot] != id) {
        s_lookId[slot] = id;
        const int w = (int)(220 * scale + 0.99f), h = (int)(300 * scale + 0.99f);
        c->deleteSprite();
        c->createSprite(w, h);
        c->fillSprite(LOOK_KEY);
        char path[64];
        snprintf(path, sizeof path, "/squachwatch-base/squachy/squachy_o%02u_s%u_a%u.png",
                 b.outfit, b.shade, b.aura ? 1u : 0u);
        s_lookOk[slot] = s_sdOk && c->drawPngFile(SD_MMC, path, 0, 0, 0, 0, 0, 0, scale, scale);
        // and its mirror, once: flipping it every frame cost a pixel-by-pixel
        // transform per visitor per frame (2026-10-20)
        if (!s_lookSprL[slot]) {
            s_lookSprL[slot] = new M5Canvas(&s_canvas);
            s_lookSprL[slot]->setPsram(true);
            s_lookSprL[slot]->setColorDepth(16);
        }
        M5Canvas* L = s_lookSprL[slot];
        L->deleteSprite();
        if (s_lookOk[slot] && L->createSprite(w, h)) {
            L->fillSprite(LOOK_KEY);
            c->pushRotateZoom(L, w / 2.0f, h / 2.0f, 0, -1.0f, 1.0f);
        }
    }
    return s_lookOk[slot] ? c : nullptr;
}

// Without the picture: a plain silhouette, so the stage still says who is here.
void silhouette(int cx, int ground, float scale, uint16_t col) {
    const int u = (int)(10 * scale);
    s_canvas.fillRoundRect(cx - 5 * u, ground - 22 * u, 10 * u, 12 * u, 3 * u, col);   // body
    s_canvas.fillCircle(cx, ground - 25 * u, 5 * u, col);                               // head
    s_canvas.fillRoundRect(cx - 5 * u, ground - 10 * u, 4 * u, 10 * u, u, col);         // legs
    s_canvas.fillRoundRect(cx + u, ground - 10 * u, 4 * u, 10 * u, u, col);
}

// Returns the y the board list starts at.
int drawStage(int top, uint32_t now) {
    char line[64];
    const Board* here[BOARDS_N];
    uint8_t slot[BOARDS_N];
    uint8_t n = 0;
    for (uint8_t i = 0; i < BOARDS_N; i++) {
        const Board& b = s_boards[i];
        if (!b.live || !b.present) continue;
        uint8_t j = n++;                        // in order of arrival
        while (j > 0 && here[j - 1]->arriveMs > b.arriveMs) { here[j] = here[j - 1]; slot[j] = slot[j - 1]; j--; }
        here[j] = &b; slot[j] = i;
    }
    const int x0 = 20, w = 470, y0 = top + 34, h = 312, ground = y0 + 250;
    s_canvas.setTextColor(DIM, BG);
    s_canvas.setFont(&fonts::FreeSansBold12pt7b);
    s_canvas.drawString("WHO'S HERE", 28, top + 4);
    s_canvas.fillRoundRect(x0, y0, w, h, 12, PANEL);
    s_canvas.fillRect(x0 + 16, ground + 2, w - 32, 3, DIM);           // the floor
    if (!n) {
        s_canvas.setTextColor(DIM, PANEL);
        s_canvas.setFont(&fonts::FreeSans12pt7b);
        s_canvas.setTextDatum(middle_center);
        s_canvas.drawString("Nobody here yet.", x0 + w / 2, y0 + h / 2 - 16);
        s_canvas.drawString("A Squachy steps on when its board is in range.", x0 + w / 2, y0 + h / 2 + 16);
        s_canvas.setTextDatum(top_left);
        return y0 + h + 14;
    }
    const uint8_t shown = n > 3 ? 3 : n;
    const float scale = shown == 3 ? 0.68f : 0.8f;
    const int cell = w / shown;
    for (uint8_t k = 0; k < shown; k++) {
        const Board& b = *here[k];
        const int cx = x0 + cell * k + cell / 2;
        const int bob = ((now / 1000 + k) & 1) ? 0 : 3;               // a little life, once a second
        const bool quiet = now - b.lastMs > 20000;
        if (const M5Canvas* spr = lookSprite(slot[k], b, scale))
            const_cast<M5Canvas*>(spr)->pushSprite(&s_canvas, cx - spr->width() / 2, ground - spr->height() + bob, LOOK_KEY);
        else
            silhouette(cx, ground + bob, scale, quiet ? DIM : ACCENT);
        s_canvas.setTextDatum(top_center);
        s_canvas.setFont(&fonts::FreeSansBold12pt7b);
        s_canvas.setTextColor(quiet ? DIM : INK, PANEL);
        s_canvas.drawString(b.name, cx, ground + 10);
        s_canvas.setFont(&fonts::FreeSans9pt7b);
        s_canvas.setTextColor(DIM, PANEL);
        snprintf(line, sizeof line, "%d dBm%s", b.rssi, b.phone ? "  phone" : "");
        s_canvas.drawString(line, cx, ground + 36);
        s_canvas.setTextDatum(top_left);
    }
    if (n > shown) {
        snprintf(line, sizeof line, "+%u more", n - shown);
        s_canvas.setFont(&fonts::FreeSansBold12pt7b);
        s_canvas.setTextColor(ACCENT, PANEL);
        s_canvas.setTextDatum(top_right);
        s_canvas.drawString(line, x0 + w - 14, y0 + 10);
        s_canvas.setTextDatum(top_left);
    }
    return y0 + h + 14;
}

void drawMain(uint32_t now) {
    s_canvas.fillSprite(BG);
    s_buttonN = 0;
    char line[160];

    // header
    s_canvas.setTextColor(ACCENT, BG);
    s_canvas.setFont(&fonts::FreeSansBold24pt7b);
    s_canvas.drawString("SQUACHWATCH BASE STATION", 28, 18);
    s_canvas.setFont(&fonts::FreeSansBold12pt7b);
    if (!s_bleOk) {
        s_canvas.setTextColor(WARN, BG);
        snprintf(line, sizeof line, "BLUETOOTH FAILED TO START -- the C6's hosted firmware may lack BT.");
    } else if (!s_cryptoOk) {
        s_canvas.setTextColor(WARN, BG);
        snprintf(line, sizeof line, "CRYPTO SELF-TEST FAILED -- squad frames cannot be trusted on this build.");
    } else if (!s_haveKey) {
        s_canvas.setTextColor(WARN, BG);
        snprintf(line, sizeof line, "SQUAD LOCKED -- tap SQUAD PHRASE and type your squad's five words.");
    } else {
        s_canvas.setTextColor(ACCENT, BG);
        snprintf(line, sizeof line, "SQUAD UNLOCKED   devices %u   sightings %lu   up %lum   SD %s",
                 s_devN, (unsigned long)s_sightings, (unsigned long)((now - s_bootMs) / 60000), s_sdOk ? "OK" : "NONE");
    }
    s_canvas.drawString(line, 30, 76);

    // GPS, top right under the button
    {
        const Gnss::Fix& gf = Gnss::fix();
        const Gnss::Sky sk = Gnss::sky();
        char g[64];
        uint16_t col = DIM;
        if (!Gnss::good())          snprintf(g, sizeof g, "GPS: searching for the unit");
        else if (Gnss::fresh(now))  { snprintf(g, sizeof g, "GPS FIX  %u sats  +/-%um", gf.used, gf.accM); col = ACCENT; }
        else                        { snprintf(g, sizeof g, "GPS: no fix  %u/%u sats", sk.heard, sk.view); col = WARN; }
        s_canvas.setTextColor(col, BG);
        s_canvas.setFont(&fonts::FreeSansBold12pt7b);
        s_canvas.setTextDatum(top_right);
        s_canvas.drawString(g, 1260, 84);
        s_canvas.setTextDatum(top_left);
    }
    button(1010, 16, 250, 64, "SQUAD PHRASE", s_haveKey ? PANEL : WARN, INK, BTN_PHRASE);
    button(820, 16, 170, 64, "MAP", INFO, INK, BTN_MAP);

    // left: the boards
    const int top = 120;
    s_canvas.drawFastHLine(20, top - 6, 1240, DIM);
    s_canvas.setTextColor(DIM, BG);
    s_canvas.setFont(&fonts::FreeSansBold12pt7b);
    int y = drawStage(top, now), shown = 0;
    for (const auto& b : s_boards) {
        if (!b.live) continue;
        if (y > 712 - 56) break;
        const uint32_t ago = now - b.lastMs;
        const bool fresh = ago < 30000;
        s_canvas.fillRoundRect(20, y, 470, 52, 10, PANEL);
        uint16_t tag = DIM; const char* tagText = "NOT IN SQUAD";
        if (b.squad)                         { tag = ACCENT; tagText = "SQUAD"; }
        else if (!b.frames)                  { tag = DIM;    tagText = "NO SQUAD"; }
        else if (s_haveKey && b.badTag)      { tag = WARN;   tagText = "OTHER SQUAD"; }
        else if (!s_haveKey)                 { tag = DIM;    tagText = "SEALED"; }
        s_canvas.fillRoundRect(20, y, 10, 52, 4, fresh ? tag : DIM);
        s_canvas.setTextColor(fresh ? INK : DIM, PANEL);
        s_canvas.setFont(&fonts::FreeSansBold12pt7b);
        snprintf(line, sizeof line, "%s%s", b.name, b.phone ? " (PHONE)" : "");
        s_canvas.drawString(line, 44, y + 5);
        s_canvas.setFont(&fonts::FreeSans9pt7b);
        s_canvas.setTextColor(tag, PANEL);
        s_canvas.setTextDatum(top_right);
        s_canvas.drawString(tagText, 476, y + 8);
        s_canvas.setTextDatum(top_left);
        s_canvas.setTextColor(DIM, PANEL);
        char ago_s[16]; agoText(ago, ago_s, sizeof ago_s);
        if (b.ver[0] || b.ver[1] || b.ver[2])
            snprintf(line, sizeof line, "%d dBm  %s  v%u.%u.%u  %lu catches", b.rssi, ago_s,
                     b.ver[0], b.ver[1], b.ver[2], (unsigned long)b.catches);
        else
            snprintf(line, sizeof line, "%d dBm  %s  %lu catches", b.rssi, ago_s, (unsigned long)b.catches);
        s_canvas.drawString(line, 44, y + 30);
        y += 58; shown++;
    }
    if (!shown) {
        s_canvas.setTextColor(DIM, BG);
        s_canvas.setFont(&fonts::FreeSans12pt7b);
        s_canvas.drawString("No SquachWatch heard yet.", 28, y + 8);
    }

    // right: the feed
    const int fx = 510, fw = 750;
    s_canvas.setTextColor(DIM, BG);
    s_canvas.setFont(&fonts::FreeSansBold12pt7b);
    s_canvas.drawString("LIVE FEED", fx + 8, top + 4);
    y = top + 40;
    if (!s_feedN) {
        s_canvas.setFont(&fonts::FreeSans12pt7b);
        s_canvas.drawString(s_haveKey ? "Waiting for the squad. Catches and messages land here."
                                      : "Set the squad phrase to open the squad's alerts.", fx + 8, y + 8);
    }
    for (uint8_t i = 0; i < s_feedN && y <= 700 - 84; i++) {
        const FeedItem& f = s_feed[i];
        const bool big = f.kind == FeedKind::CATCH;
        const int h = big ? 96 : 78;
        if (y + h > 712) break;
        s_canvas.fillRoundRect(fx, y, fw, h, 10, PANEL);
        s_canvas.fillRoundRect(fx, y, 12, h, 4, f.colour);
        s_canvas.setTextColor(f.colour, PANEL);
        s_canvas.setFont(big ? &fonts::FreeSansBold24pt7b : &fonts::FreeSansBold12pt7b);
        s_canvas.drawString(f.title, fx + 26, y + 8);
        char ago_s[16]; agoText(now - f.at, ago_s, sizeof ago_s);
        s_canvas.setTextColor(DIM, PANEL);
        s_canvas.setFont(&fonts::FreeSans12pt7b);
        s_canvas.setTextDatum(top_right);
        s_canvas.drawString(ago_s, fx + fw - 16, y + 12);
        if (big) {
            char la[16], lo[16], pos[40];
            if (f.havePos) { degText(f.lat7, la, sizeof la); degText(f.lon7, lo, sizeof lo);
                             snprintf(pos, sizeof pos, "%s, %s", la, lo); }
            else             snprintf(pos, sizeof pos, "no GPS fix");
            s_canvas.setTextColor(f.havePos ? ACCENT : DIM, PANEL);
            s_canvas.drawString(pos, fx + fw - 16, y + 40);
        }
        s_canvas.setTextDatum(top_left);
        s_canvas.setTextColor(INK, PANEL);
        s_canvas.setFont(big ? &fonts::FreeSans12pt7b : &fonts::FreeSans18pt7b);
        s_canvas.drawString(f.body, fx + 26, y + (big ? 60 : 38));
        y += h + 8;
    }
    pushCanvas();
}

// ---- files over USB --------------------------------------------------------------
// The Tab5's USB is a serial line, not a drive, so a PC cannot copy onto its
// card. This is the way in instead: tools/tab5_upload.py sends
//
//   PUT <path> <bytes>\n   then exactly <bytes> raw bytes   ->  "OK <bytes>" or "ERR <why>"
//   PING\n                                                   ->  "PONG"
//   RESCAN\n                                                 ->  "OK" after finding the tiles again
//
// Paths are on the card and must start with /squachwatch-base/ -- nothing
// else on it (Launcher's apps, other firmware's files) can be written. Parent
// folders are made as needed. Each reply is one line starting "@@ ", so a PC
// can tell it from the log lines around it.
char     s_cmd[200];
uint16_t s_cmdN = 0;

bool mkdirs(const char* path) {
    char part[200];
    for (const char* q = path + 1; *q; q++) {
        if (*q != '/') continue;
        const size_t n = (size_t)(q - path);
        if (n >= sizeof part) return false;
        memcpy(part, path, n); part[n] = '\0';
        if (!SD_MMC.exists(part) && !SD_MMC.mkdir(part)) return false;
    }
    return true;
}

void usbPut(const char* args) {
    char path[160]; unsigned long size = 0;
    if (sscanf(args, "%159s %lu", path, &size) != 2) { Serial.println("@@ ERR usage: PUT <path> <bytes>"); return; }
    // Read the bytes first, whatever happens: a refused file must not leave
    // its contents in the line to be read as commands.
    static uint8_t* buf = nullptr;
    static size_t   cap = 0;
    if (size > 2 * 1024 * 1024) { Serial.println("@@ ERR too big (2 MB max)"); return; }
    if (size > cap) { free(buf); buf = (uint8_t*)ps_malloc(size + 1); cap = buf ? size : 0; }
    if (!buf) { Serial.println("@@ ERR no memory"); return; }
    size_t got = 0;
    const uint32_t t0 = millis();
    while (got < size && millis() - t0 < 10000) {
        const int a = Serial.available();
        if (a > 0) got += Serial.readBytes(buf + got, std::min((size_t)a, (size_t)(size - got)));
        else delay(1);
    }
    if (got != size) { Serial.printf("@@ ERR short read %u of %lu\n", (unsigned)got, size); return; }
    if (strncmp(path, "/squachwatch-base/", 18) != 0 || strstr(path, "..")) {
        Serial.println("@@ ERR only /squachwatch-base/... may be written"); return;
    }
    if (!s_sdOk)        { Serial.println("@@ ERR no SD card"); return; }
    if (!mkdirs(path))  { Serial.println("@@ ERR could not make its folder"); return; }
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f)             { Serial.println("@@ ERR could not open it"); return; }
    const size_t w = f.write(buf, size);
    f.close();
    if (w != size) { Serial.printf("@@ ERR wrote %u of %lu\n", (unsigned)w, size); return; }
    Serial.printf("@@ OK %lu\n", size);
}

void usbTick() {
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n') { if (s_cmdN < sizeof s_cmd - 1) s_cmd[s_cmdN++] = c; continue; }
        s_cmd[s_cmdN] = '\0';
        s_cmdN = 0;
        if (!strncmp(s_cmd, "PUT ", 4))     usbPut(s_cmd + 4);
        else if (!strcmp(s_cmd, "PING"))    Serial.println("@@ PONG");
        else if (!strcmp(s_cmd, "RESCAN"))  { if (s_sdOk) findTiles(); s_mapDirty = true;
                                              Serial.printf("@@ OK tiles %s\n", s_tilesFound ? s_tileRoot : "none"); }
        else if (s_cmd[0])                  Serial.println("@@ ERR unknown command");
    }
}

// ---- the map ------------------------------------------------------------------
constexpr uint8_t TALLY_N = 20;
char s_tallyType[TALLY_N][12];             // the CAPTURED rows on screen, for their taps
// Pre-rendered 256 px tiles on the card, /squachwatch-base/tiles/z/x/y.png --
// OpenStreetMap data drawn on a PC (tools/render_tiles.py) for the area the
// squad covers. Positions are Web Mercator "world pixels" at the map's zoom.
// Tiles go to zoom 16 (about 2 km across the screen); 17 and 18 (1 km, 500 m)
// enlarge the zoom-16 tiles, so street level costs nothing on the card.
constexpr int MAP_ZMIN = 12, MAP_ZMAX = 18, TILE_ZMAX = 16, TILE = 256;
int     s_mapZ = 14;
double  s_mapLat = 0, s_mapLon = 0;              // until there is a fix, a home or a pin
bool    s_mapCentred = false;

void worldPx(double lat, double lon, int z, double& x, double& y) {
    const double n = (double)(1u << z) * TILE;
    x = (lon + 180.0) / 360.0 * n;
    const double s = sin(lat * M_PI / 180.0);
    y = (0.5 - log((1 + s) / (1 - s)) / (4 * M_PI)) * n;
}
void worldToLatLon(double x, double y, int z, double& lat, double& lon) {
    const double n = (double)(1u << z) * TILE;
    lon = x / n * 360.0 - 180.0;
    lat = atan(sinh(M_PI * (1 - 2 * y / n))) * 180.0 / M_PI;
}

// First time the map opens: where the Tab5 is, else the newest pin.
void mapCentreDefault(uint32_t now) {
    if (s_mapCentred) return;
    int32_t la, lo; uint8_t am;
    if (herePos(now, la, lo, am)) { s_mapLat = la / 1e7; s_mapLon = lo / 1e7; s_mapCentred = true; }
    else if (s_pinN)        { s_mapLat = s_pins[s_pinN - 1].lat7 / 1e7; s_mapLon = s_pins[s_pinN - 1].lon7 / 1e7; s_mapCentred = true; }
}

// Decoded tiles, kept in PSRAM. Every pan used to decode each of the ~35
// tiles on screen from the card again -- about a second, in which the map
// took no swipes (2026-10-19). Now a pan decodes only the tiles newly in
// view. A tile not on the card is remembered as missing, so the card is
// asked once. Least recently used goes first.
constexpr uint8_t TILE_CACHE_N = 96;                  // 96 x 128 KB: the screen and a ring round it
struct CachedTile { int8_t z; int32_t x, y; uint32_t used; bool have; M5Canvas* spr; };
CachedTile s_tiles[TILE_CACHE_N];
uint32_t   s_tileClock = 0;

// Draws tile z/x/y at (dx, dy) on the canvas; false if the card has none.
CachedTile* cachedTile(int z, int x, int y) {
    CachedTile* hitT = nullptr;
    CachedTile* oldest = &s_tiles[0];
    for (auto& t : s_tiles) {
        if (t.used && t.z == z && t.x == x && t.y == y) { hitT = &t; break; }
        if (t.used < oldest->used) oldest = &t;
    }
    if (!hitT) {
        hitT = oldest;
        hitT->z = z; hitT->x = x; hitT->y = y; hitT->have = false;
        if (!hitT->spr) {
            hitT->spr = new M5Canvas(&s_canvas);
            hitT->spr->setPsram(true);
            hitT->spr->setColorDepth(16);
            hitT->spr->createSprite(TILE, TILE);
        }
        char path[160];
        snprintf(path, sizeof path, "%s/%d/%d/%d.png", s_tileRoot, z, x, y);
        if (hitT->spr->getBuffer() && SD_MMC.exists(path)) {
            hitT->spr->fillSprite(BG);
            hitT->have = hitT->spr->drawPngFile(SD_MMC, path, 0, 0);
        }
    }
    hitT->used = ++s_tileClock;
    return hitT;
}

// The ring of tiles just off the screen, loaded one at a time while the map
// sits still, so a swipe usually finds them decoded already (2026-10-19:
// with only the screen kept, each swipe still waited on the card).
struct TileKey { int8_t z; int32_t x, y; };
TileKey  s_pre[48];
uint8_t  s_preN = 0, s_preAt = 0;

bool tileCached(int z, int x, int y) {
    for (auto& t : s_tiles) if (t.used && t.z == z && t.x == x && t.y == y) return true;
    return false;
}

void prefetchPlan(int z, int tx0, int ty0, int tx1, int ty1) {
    s_preN = s_preAt = 0;
    if (z > TILE_ZMAX) {                      // the zoom-16 tiles under the view
        const int dz = z - TILE_ZMAX;
        z = TILE_ZMAX; tx0 >>= dz; ty0 >>= dz; tx1 >>= dz; ty1 >>= dz;
    }
    for (int ty = ty0 - 1; ty <= ty1 + 1; ty++)
        for (int tx = tx0 - 1; tx <= tx1 + 1; tx++) {
            const bool edge = tx < tx0 || tx > tx1 || ty < ty0 || ty > ty1;
            if (edge && s_preN < sizeof s_pre / sizeof s_pre[0]) s_pre[s_preN++] = { (int8_t)z, tx, ty };
        }
}

// One tile a call, from loop() while the map is up and nobody is touching it.
void prefetchTick() {
    while (s_preAt < s_preN) {
        const TileKey k = s_pre[s_preAt++];
        if (tileCached(k.z, k.x, k.y)) continue;
        cachedTile(k.z, k.x, k.y);
        return;
    }
}

bool drawTile(M5Canvas& dst, int z, int x, int y, int dx, int dy) {
    // A tile the card does not have is empty sea or land: the tiles' own
    // background colour, (24, 28, 36), so it matches its neighbours.
    if (z <= TILE_ZMAX) {
        CachedTile* t = cachedTile(z, x, y);
        if (t->have) t->spr->pushSprite(&dst, dx, dy);
        else         dst.fillRect(dx, dy, TILE, TILE, 0x18E4);
        return t->have;
    }
    // Closer than the card goes: the part of the zoom-16 tile under this one,
    // enlarged 2x or 4x.
    const int dz = z - TILE_ZMAX, k = 1 << dz, sub = TILE >> dz;
    CachedTile* t = cachedTile(TILE_ZMAX, x >> dz, y >> dz);
    if (!t->have) { dst.fillRect(dx, dy, TILE, TILE, 0x18E4); return false; }
    const int sx = (x & (k - 1)) * sub, sy = (y & (k - 1)) * sub;
    dst.setClipRect(dx, dy, TILE, TILE);
    t->spr->pushRotateZoom(&dst, dx - sx * k + TILE * k / 2.0f, dy - sy * k + TILE * k / 2.0f, 0, (float)k, (float)k);
    dst.clearClipRect();
    return true;
}

// The map is drawn a tile bigger than the screen on every side, so a finger
// dragging it shows real map coming in at the edges -- the screen is a
// window onto this, moved with the finger, and the map is drawn again
// round the new centre on the lift (2026-10-19: the old drag slid a copy of
// the screen with blank edges, and the map only appeared after the lift).
constexpr int MAP_MARGIN = 256;
uint32_t s_mapTouchAt = 0;                 // the last time a finger was on the map
M5Canvas s_mapBig(&M5.Display);
bool     s_mapBigOk = false;

void drawMap(uint32_t now) {
    s_canvas.fillSprite(BG);
    s_buttonN = 0;
    const int W = s_canvas.width(), H = s_canvas.height();
    if (!s_mapBig.getBuffer()) {
        s_mapBig.setPsram(true);
        s_mapBig.setColorDepth(16);
        s_mapBigOk = s_mapBig.createSprite(W + 2 * MAP_MARGIN, H + 2 * MAP_MARGIN) != nullptr;
    }
    M5Canvas& mc = s_mapBigOk ? s_mapBig : s_canvas;
    const int M = s_mapBigOk ? MAP_MARGIN : 0, MW = W + 2 * M, MH = H + 2 * M;
    mc.fillSprite(BG);
    double cx, cy;
    worldPx(s_mapLat, s_mapLon, s_mapZ, cx, cy);
    const double left = cx - MW / 2.0, top = cy - MH / 2.0;
    uint16_t drawn = 0;
    if (s_sdOk) {
        const int tx0 = (int)floor(left / TILE), ty0 = (int)floor(top / TILE);
        const int tx1 = (int)floor((left + MW) / TILE), ty1 = (int)floor((top + MH) / TILE);
        for (int ty = ty0; ty <= ty1; ty++)
            {
                for (int tx = tx0; tx <= tx1; tx++)
                    if (drawTile(mc, s_mapZ, tx, ty, (int)(tx * TILE - left), (int)(ty * TILE - top))) drawn++;
                touchPoll();                     // a swipe starting now is not lost
            }
        prefetchPlan(s_mapZ, tx0, ty0, tx1, ty1);
    }
    // pins, oldest first so the newest sit on top
    const bool labels = s_mapZ >= 15;
    mc.setFont(&fonts::FreeSansBold9pt7b);
    for (uint8_t pass = 0; pass < 2; pass++)
    for (uint16_t i = 0; i < s_pinN; i++) {
        const bool big = s_pins[i].tier <= 1;
        if (big != (pass == 1)) continue;
        if (typeHidden(s_pins[i].label)) continue;
        double px, py;
        worldPx(s_pins[i].lat7 / 1e7, s_pins[i].lon7 / 1e7, s_mapZ, px, py);
        const int sx = (int)(px - left), sy = (int)(py - top);
        if (sx < -20 || sy < -20 || sx > MW + 20 || sy > MH + 20) continue;
        if (s_pins[i].dev != NO_DEV && s_devs[s_pins[i].dev].following) {
            mc.drawCircle(sx, sy, 16, ALERT); mc.drawCircle(sx, sy, 17, ALERT);
            mc.drawCircle(sx, sy, 18, ALERT);
        }
        mc.fillCircle(sx, sy, big ? 13 : 10, big ? TFT_BLACK : TFT_WHITE);
        mc.fillCircle(sx, sy, big ? 11 : 8, s_pins[i].colour);
        if (labels) {
            mc.setTextColor(TFT_WHITE);
            mc.drawString(s_pins[i].label, sx + 14, sy - 8);
        }
    }
    // the Tab5 itself: its fix, or home
    int32_t hereLa, hereLo; uint8_t hereAcc;
    const bool here = herePos(now, hereLa, hereLo, hereAcc);
    if (here) {
        double px, py;
        worldPx(hereLa / 1e7, hereLo / 1e7, s_mapZ, px, py);
        const int sx = (int)(px - left), sy = (int)(py - top);
        mc.drawCircle(sx, sy, 18, INFO);
        mc.drawCircle(sx, sy, 17, INFO);
        mc.fillCircle(sx, sy, 9, TFT_WHITE);
        mc.fillCircle(sx, sy, 7, INFO);
    }
    if (s_mapBigOk) s_mapBig.pushSprite(&s_canvas, -M, -M);   // the screen's window
    // controls and status
    button(16, 16, 150, 64, "BACK", PANEL, INK, BTN_MAP_BACK);
    button(W - 16 - 80, 16, 80, 64, "+", PANEL, INK, BTN_ZOOM_IN, &fonts::FreeSansBold24pt7b);
    button(W - 16 - 170, 16, 80, 64, "-", PANEL, INK, BTN_ZOOM_OUT, &fonts::FreeSansBold24pt7b);
    button(W - 16 - 320, 16, 140, 64, Gnss::fresh(now) ? "ME" : "HOME", here ? INFO : PANEL, here ? INK : DIM, BTN_ME);
    // Tally: per type, how many devices and how many sightings; anything
    // following at the top, in red.
    {
        struct Row { const char* type; uint16_t devs; uint32_t sight; };
        Row rows[20]; uint8_t nr = 0;
        uint16_t follow = 0;
        for (uint16_t i = 0; i < s_devN; i++) {
            const Dev& d = s_devs[i];
            if (d.following) follow++;
            uint8_t r = 0;
            while (r < nr && strcmp(rows[r].type, d.type)) r++;
            if (r == nr) { if (nr == 20) continue; rows[nr++] = { d.type, 0, 0 }; }
            rows[r].devs++; rows[r].sight += d.sightings;
        }
        // worst first, then most devices
        for (uint8_t a = 1; a < nr; a++)
            for (uint8_t b = a; b > 0; b--) {
                const uint8_t ta = tierForName(rows[b].type), tb = tierForName(rows[b - 1].type);
                if (ta < tb || (ta == tb && rows[b].devs > rows[b - 1].devs)) { Row x = rows[b]; rows[b] = rows[b - 1]; rows[b - 1] = x; }
                else break;
            }
        const int px = W - 16 - 400, pw = 400;
        const int ph = 82 + (follow ? 40 : 0) + (nr ? nr : 1) * 34 + (follow ? (follow > 4 ? 4 : follow) * 30 : 0);
        s_canvas.fillRoundRect(px, 96, pw, ph, 10, PANEL);
        s_canvas.setFont(&fonts::FreeSansBold12pt7b);
        s_canvas.setTextColor(INK, PANEL);
        char t[64];
        snprintf(t, sizeof t, "CAPTURED  %u devices", s_devN);
        s_canvas.drawString(t, px + 14, 108);
        s_canvas.setTextColor(DIM, PANEL);
        s_canvas.setFont(&fonts::FreeSans9pt7b);
        s_canvas.drawString("devices / sightings    tap a row to show or hide it", px + 14, 138);
        s_canvas.setFont(&fonts::FreeSansBold12pt7b);
        int yy = 170;
        if (follow) {
            s_canvas.setTextColor(ALERT, PANEL);
            snprintf(t, sizeof t, "FOLLOWING YOU: %u", follow);
            s_canvas.drawString(t, px + 14, yy); yy += 34;
            s_canvas.setFont(&fonts::FreeSans12pt7b);
            uint8_t shown = 0;
            for (uint16_t i = 0; i < s_devN && shown < 4; i++) {
                const Dev& d = s_devs[i];
                if (!d.following) continue;
                snprintf(t, sizeof t, "%s ..%02X:%02X:%02X  %u places", d.type, d.key[0], d.key[1], d.key[2], d.nPlaces);
                s_canvas.drawString(t, px + 20, yy); yy += 30; shown++;
            }
            yy += 6;
        }
        s_canvas.setFont(&fonts::FreeSans12pt7b);
        if (!nr) { s_canvas.setTextColor(DIM, PANEL); s_canvas.drawString("nothing yet", px + 14, yy); }
        for (uint8_t r = 0; r < nr; r++) {
            const bool hid = typeHidden(rows[r].type);
            if (r < TALLY_N) { strncpy(s_tallyType[r], rows[r].type, 11); s_tallyType[r][11] = '\0';
                               addButton(px, yy - 4, pw, 32, BTN_TYPE + r); }
            if (hid) s_canvas.drawCircle(px + 22, yy + 11, 8, DIM);
            else     s_canvas.fillCircle(px + 22, yy + 11, 8, colourForName(rows[r].type));
            s_canvas.setTextColor(hid ? DIM : INK, PANEL);
            s_canvas.drawString(rows[r].type, px + 38, yy);
            snprintf(t, sizeof t, "%u  /  %lu", rows[r].devs, (unsigned long)rows[r].sight);
            s_canvas.setTextDatum(top_right);
            s_canvas.drawString(t, px + pw - 14, yy);
            s_canvas.setTextDatum(top_left);
            yy += 34;
        }
    }

    char line[120];
    snprintf(line, sizeof line, "%u pins   zoom %d", s_pinN, s_mapZ);
    s_canvas.setFont(&fonts::FreeSansBold12pt7b);
    s_canvas.setTextColor(INK, PANEL);
    s_canvas.fillRoundRect(180, 22, 470, 52, 10, PANEL);
    s_canvas.drawString(line, 196, 36);
    if (!s_sdOk || !drawn) {
        s_canvas.setTextColor(WARN, BG);
        s_canvas.setTextDatum(middle_center);
        s_canvas.drawString(s_sdOk ? (s_tilesFound ? "No map here: pan back to the squad's area."
                                                   : "No map tiles on the card: put squachwatch-base/tiles on it.")
                                   : "No SD card: the map tiles live on it.", W / 2, H / 2);
        s_canvas.setTextDatum(top_left);
    }
    s_canvas.setFont(&fonts::FreeSans9pt7b);
    s_canvas.setTextColor(DIM);
    s_canvas.setTextDatum(bottom_right);
    s_canvas.drawString("Map data (c) OpenStreetMap contributors", W - 10, H - 6);
    s_canvas.setTextDatum(top_left);
    pushCanvas();
}

void mapPan(int dx, int dy) {
    double cx, cy;
    worldPx(s_mapLat, s_mapLon, s_mapZ, cx, cy);
    worldToLatLon(cx - dx, cy - dy, s_mapZ, s_mapLat, s_mapLon);
    s_mapDirty = true;
}

void onMapTap(int id, uint32_t now) {
    switch (id) {
        case BTN_MAP_BACK: s_screen = Screen::MAIN; s_dirty = true; break;
        case BTN_ZOOM_IN:  if (s_mapZ < MAP_ZMAX) { s_mapZ++; s_mapDirty = true; } break;
        case BTN_ZOOM_OUT: if (s_mapZ > MAP_ZMIN) { s_mapZ--; s_mapDirty = true; } break;
        default:
            if (id >= BTN_TYPE && id < BTN_TYPE + TALLY_N) {
                typeToggle(s_tallyType[id - BTN_TYPE]);
                char b[HIDE_N * 12 + 1] = "";
                for (uint8_t i = 0; i < s_hiddenN; i++) { strcat(b, s_hidden[i]); strcat(b, ","); }
                s_prefs.putString("hidden", b);
                s_mapDirty = true;
            }
            break;
        case BTN_ME:
            { int32_t la, lo; uint8_t am;
              if (herePos(now, la, lo, am)) { s_mapLat = la / 1e7; s_mapLon = lo / 1e7; s_mapDirty = true; } }
            break;
    }
}

// ---- the phrase keyboard -------------------------------------------------------
char    s_typed[MeshMsg::PHRASE_TEXT_MAX + 1] = "";
char    s_unlockMsg[96] = "";

// The word being typed: from the last space to the end.
const char* currentWord() {
    const char* sp = strrchr(s_typed, ' ');
    return sp ? sp + 1 : s_typed;
}
bool inList(const char* w, size_t len) {
    for (uint16_t i = 0; i < MeshMsg::WORD_N; i++)
        if (strlen(MeshMsg::WORDS[i]) == len && strncmp(MeshMsg::WORDS[i], w, len) == 0) return true;
    return false;
}
// Five words, every one on the list, single spaces: the only thing a board's
// phrase can be.
bool phraseValid(uint8_t& words, int& badWord) {
    words = 0; badWord = -1;
    const char* s = s_typed;
    while (*s) {
        const char* e = strchr(s, ' ');
        const size_t len = e ? (size_t)(e - s) : strlen(s);
        if (!len) return false;
        if (!inList(s, len) && badWord < 0) badWord = words;
        words++;
        if (!e) break;
        s = e + 1;
    }
    return words == MeshMsg::PHRASE_WORDS && badWord < 0;
}

const char* const KEYROWS[3] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };

void drawKeyboard() {
    s_canvas.fillSprite(BG);
    s_buttonN = 0;
    s_canvas.setTextColor(ACCENT, BG);
    s_canvas.setFont(&fonts::FreeSansBold18pt7b);
    s_canvas.drawString("SQUAD PHRASE", 28, 16);
    s_canvas.setTextColor(DIM, BG);
    s_canvas.setFont(&fonts::FreeSans12pt7b);
    s_canvas.drawString("The squad's five words: on a board, SETTINGS > SQUACHMESH > PHRASE shows them if SHOW PHRASE is on.", 30, 58);

    // the field
    s_canvas.fillRoundRect(20, 92, 1240, 72, 10, PANEL);
    s_canvas.setTextColor(INK, PANEL);
    s_canvas.setFont(&fonts::FreeSansBold24pt7b);
    char shown[MeshMsg::PHRASE_TEXT_MAX + 2];
    snprintf(shown, sizeof shown, "%s_", s_typed);
    s_canvas.drawString(shown, 40, 106);

    uint8_t words = 0; int bad = -1;
    const bool ok = phraseValid(words, bad);
    s_canvas.setFont(&fonts::FreeSans12pt7b);
    char line[96];
    if (bad >= 0)      { s_canvas.setTextColor(WARN, BG); snprintf(line, sizeof line, "Word %d is not on the word list.", bad + 1); }
    else if (ok)       { s_canvas.setTextColor(ACCENT, BG); snprintf(line, sizeof line, "Five words, all on the list. Tap SAVE."); }
    else               { s_canvas.setTextColor(DIM, BG); snprintf(line, sizeof line, "%u of 5 words.", words); }
    s_canvas.drawString(line, 30, 172);

    // suggestions for the word being typed
    const char* cw = currentWord();
    const size_t cl = strlen(cw);
    int sx = 20, shownN = 0;
    if (cl) {
        for (uint16_t i = 0; i < MeshMsg::WORD_N && shownN < 6; i++) {
            if (strncmp(MeshMsg::WORDS[i], cw, cl) != 0) continue;
            button(sx, 206, 200, 62, MeshMsg::WORDS[i], INFO, INK, BTN_SUGGEST + i);
            sx += 208; shownN++;
        }
    }

    // the keys
    const int kw = 112, kh = 84, gap = 10;
    for (uint8_t r = 0; r < 3; r++) {
        const char* row = KEYROWS[r];
        const int n = strlen(row);
        const int x0 = (1280 - (n * kw + (n - 1) * gap)) / 2 - (r == 2 ? 70 : 0);
        for (int i = 0; i < n; i++) {
            char k[2] = { row[i], 0 };
            button(x0 + i * (kw + gap), 290 + r * (kh + gap), kw, kh, k, PANEL, INK, BTN_KEY + row[i],
                   &fonts::FreeSansBold18pt7b);
        }
        if (r == 2) button(x0 + n * (kw + gap), 290 + r * (kh + gap), 2 * kw + gap, kh, "DEL", PANEL, WARN, BTN_DEL);
    }
    const int by = 290 + 3 * (kh + gap);
    button(20,   by, 560, kh, "SPACE",  PANEL, INK,  BTN_SPACE);
    button(600,  by, 200, kh, "CLEAR",  PANEL, WARN, BTN_CLEAR);
    button(820,  by, 200, kh, "CANCEL", PANEL, INK,  BTN_CANCEL);
    button(1040, by, 220, kh, "SAVE",   ok ? ACCENT : PANEL, ok ? BG : DIM, ok ? BTN_SAVE : 0);
    pushCanvas();
}

void drawUnlocking() {
    s_canvas.fillSprite(BG);
    s_buttonN = 0;
    s_canvas.setTextColor(ACCENT, BG);
    s_canvas.setFont(&fonts::FreeSansBold24pt7b);
    s_canvas.setTextDatum(middle_center);
    s_canvas.drawString(s_unlockMsg, 640, 340);
    s_canvas.setTextDatum(top_left);
    pushCanvas();
}

void typeChar(char c) {
    const size_t n = strlen(s_typed);
    if (n + 1 >= sizeof s_typed) return;
    if (c == ' ' && (n == 0 || s_typed[n - 1] == ' ')) return;   // single spaces, none leading
    s_typed[n] = c; s_typed[n + 1] = '\0';
}

void onKeyboardTap(int id) {
    if (id >= BTN_KEY && id < BTN_KEY + 128) { typeChar((char)(id - BTN_KEY)); return; }
    if (id >= BTN_SUGGEST && id < BTN_SUGGEST + MeshMsg::WORD_N) {
        // Replace the word being typed with the whole word, then a space.
        char* cw = (char*)currentWord();
        *cw = '\0';
        const char* w = MeshMsg::WORDS[id - BTN_SUGGEST];
        if (strlen(s_typed) + strlen(w) + 1 < sizeof s_typed) {
            strcat(s_typed, w);
            uint8_t words = 0; int bad = -1;
            phraseValid(words, bad);
            if (words < MeshMsg::PHRASE_WORDS) typeChar(' ');
        }
        return;
    }
    switch (id) {
        case BTN_SPACE: typeChar(' '); break;
        case BTN_DEL: { const size_t n = strlen(s_typed); if (n) s_typed[n - 1] = '\0'; break; }
        case BTN_CLEAR: s_typed[0] = '\0'; break;
        case BTN_CANCEL: s_screen = Screen::MAIN; break;
        case BTN_SAVE: {
            // Trailing space from a suggestion is not part of the phrase.
            size_t n = strlen(s_typed);
            while (n && s_typed[n - 1] == ' ') s_typed[--n] = '\0';
            snprintf(s_unlockMsg, sizeof s_unlockMsg, "UNLOCKING THE SQUAD...");
            s_screen = Screen::UNLOCKING;
            drawUnlocking();
            const bool ok = s_cryptoOk && setPhrase(s_typed);
            // A new key: what we remembered under the old one means nothing.
            for (auto& b : s_boards) { b.squad = false; b.badTag = 0; b.haveCtr = false; }
            s_asm = MeshMsg::Assembly();
            snprintf(s_unlockMsg, sizeof s_unlockMsg, ok ? "SQUAD UNLOCKED" : "COULD NOT SET THE PHRASE");
            drawUnlocking();
            delay(900);
            if (ok) feedAdd(FeedKind::NOTE, ACCENT, "SQUAD UNLOCKED", "Listening for catches and messages.", millis());
            s_screen = Screen::MAIN;
            break;
        }
    }
}

} // namespace

// Startup, one step at a time on the screen itself: a step that hangs leaves
// its name up, which says more than a black panel does.
void bootStage(const char* what) {
    static int y = 120;
    M5.Display.setTextColor(ACCENT, BG);
    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.drawString(what, 40, y);
    y += 50;
    Serial.printf("[boot] %s\n", what);
}

// ---- HOME: the basement workshop ---------------------------------------------
// The main screen as a half-below-ground workshop at night (2026-10-18): a
// backdrop painted on a PC (tools/paint_tab5_basement.py, light on and light
// off) in /squachwatch-base/scene/, and over it, drawn here, everything that
// moves -- the laptop's screen, the radar's sweep and blips,
// the Nixie clock, the soldering iron's smoke, the news on the chalkboard,
// the visitors on the rug and the tally along the bottom. Pull the string by
// the bulb for the light. Without the backdrop on the card, the plain
// screen above is drawn instead.
//
// Places, in screen pixels, as the painter lays them out:
constexpr int LAP_X0 = 830, LAP_Y0 = 338, LAP_X1 = 935, LAP_Y1 = 405;   // the laptop's screen
constexpr int RADAR_X = 1190, RADAR_Y = 356, RADAR_R = 56;
constexpr int NIXIE_X[4] = { 552, 596, 660, 704 }, NIXIE_W = 36, NIXIE_Y0 = 168, NIXIE_Y1 = 210, NIXIE_COLON = 646;
constexpr int CHALK_X0 = 846, CHALK_Y0 = 92, CHALK_X1 = 1252, CHALK_Y1 = 268;
constexpr int SOLDER_X = 744, SOLDER_Y = 402;
constexpr int GROUND_Y = 612, TALLY_Y = 648;

M5Canvas s_bg(&M5.Display);
int8_t   s_bgState = 0;            // 0 not tried yet, 1 on screen, -1 not on the card
bool     s_lightOn = false;
bool     s_timeOk = false;         // the system clock holds real UTC

bool loadBackdrop() {
    if (!s_sdOk) { s_bgState = -1; return false; }
    if (!s_bg.getBuffer()) {
        s_bg.setPsram(true);
        s_bg.setColorDepth(16);
        if (!s_bg.createSprite(M5.Display.width(), M5.Display.height())) { s_bgState = -1; return false; }
    }
    const char* path = s_lightOn ? "/squachwatch-base/scene/basement_light_on.png"
                                 : "/squachwatch-base/scene/basement_light_off.png";
    const bool ok = SD_MMC.exists(path) && s_bg.drawPngFile(SD_MMC, path, 0, 0);
    s_bgState = ok ? 1 : -1;
    Serial.printf("[scene] %s %s\n", path, ok ? "loaded" : "not on the card");
    return ok;
}

uint16_t mix565(uint16_t a, uint16_t b, float t) {           // t 0 = a, 1 = b
    if (t <= 0) return a;
    if (t >= 1) return b;
    const int ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31;
    const int rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
    return (uint16_t)(((int)(ra + (rb - ra) * t) << 11) | ((int)(ga + (gb - ga) * t) << 5) | (int)(ba + (bb - ba) * t));
}

// ---- the clock: the GPS sets it, the Tab5's RTC keeps it (in UTC) ----
time_t utcFromTm(const struct tm& t) {                        // timegm, which newlib lacks
    int y = t.tm_year + 1900, m = t.tm_mon + 1;
    y -= m <= 2;
    const int era = y / 400, yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t.tm_mday - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = (long)era * 146097 + doe - 719468;
    return (time_t)(days * 86400L + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
}

void clockStart() {
    setenv("TZ", "UTC0", 1);                 // until home.txt names the zone (loadHome)
    tzset();
    if (!M5.Rtc.isEnabled()) return;
    m5::rtc_datetime_t dt;
    if (!M5.Rtc.getDateTime(&dt) || dt.date.year < 2025) return;
    struct tm t = {};
    t.tm_year = dt.date.year - 1900; t.tm_mon = dt.date.month - 1; t.tm_mday = dt.date.date;
    t.tm_hour = dt.time.hours; t.tm_min = dt.time.minutes; t.tm_sec = dt.time.seconds;
    timeval tv = { utcFromTm(t), 0 };
    settimeofday(&tv, nullptr);
    s_timeOk = true;
    s_clockSet = true;
    Serial.printf("[clock] from the RTC: %04d-%02d-%02d %02d:%02d UTC\n",
                  dt.date.year, dt.date.month, dt.date.date, dt.time.hours, dt.time.minutes);
}

void clockTick(uint32_t now) {
    static uint32_t last = 0;
    if (last && now - last < 60000) return;
    const uint32_t ep = Gnss::utcEpoch();
    if (!Gnss::fresh(now) || ep < 1700000000UL) return;
    last = now;
    const time_t mine = time(nullptr);
    if (s_timeOk && labs((long)(mine - (time_t)ep)) < 3) return;
    timeval tv = { (time_t)ep, 0 };
    settimeofday(&tv, nullptr);
    s_timeOk = true;
    s_clockSet = true;
    if (M5.Rtc.isEnabled()) { time_t t = ep; struct tm g; gmtime_r(&t, &g); busTake(); M5.Rtc.setDateTime(&g); busGive(); }
    Serial.println("[clock] set from the GPS");
}

// ---- the radar ----
struct Blip { float ang, dist; uint32_t born; uint16_t col; };
constexpr uint8_t BLIPS_N = 14;
Blip    s_blips[BLIPS_N];
uint8_t s_blipNext = 0;
void radarBlip(uint16_t col, uint32_t now) {
    Blip& b = s_blips[s_blipNext++ % BLIPS_N];
    b.ang = (esp_random() % 6283) / 1000.0f;
    b.dist = 0.2f + (esp_random() % 75) / 100.0f;
    b.born = now; b.col = col;
}

void drawRadar(uint32_t now) {
    const float TAU = 6.2831853f;
    const float a = fmodf(now * TAU / 2600.0f, TAU);
    // a blip for every catch the feed takes, in its colour; and, while the
    // air is busy, a green one now and then for the chatter
    static uint32_t lastCatchAt = 0, lastAdv = 0, lastAmbient = 0;
    if (s_feedN && s_feed[0].kind == FeedKind::CATCH && s_feed[0].at != lastCatchAt) {
        lastCatchAt = s_feed[0].at; radarBlip(s_feed[0].colour, now);
    }
    const uint32_t adv = s_advSeen.load();
    if (now - lastAmbient > 900 && adv - lastAdv > 20) { lastAmbient = now; lastAdv = adv; radarBlip(0x07E0, now); }
    for (int k = 14; k >= 0; k--) {                           // the trail, then the beam
        const float ang = a - k * 0.045f;
        const uint16_t col = mix565(k == 0 ? 0xAFF5 : 0x07E0, 0x0120, k / 15.0f);
        s_canvas.drawLine(RADAR_X, RADAR_Y, RADAR_X + (int)(cosf(ang) * RADAR_R), RADAR_Y - (int)(sinf(ang) * RADAR_R), col);
    }
    for (const auto& b : s_blips) {
        if (!b.born || now - b.born > 9000) continue;
        const float since = fmodf(a - b.ang + TAU, TAU) / TAU;   // 0 just swept, 1 about to be
        const float fade = since + (now - b.born) / 9000.0f * 0.6f;
        if (fade >= 1) continue;
        const int x = RADAR_X + (int)(cosf(b.ang) * b.dist * RADAR_R);
        const int y = RADAR_Y - (int)(sinf(b.ang) * b.dist * RADAR_R);
        s_canvas.fillCircle(x, y, fade < 0.2f ? 3 : 2, mix565(b.col, 0x0120, fade));
    }
}

// ---- the laptop: a terminal scrolling away, and the latest catch, loud ----
void drawLaptop(uint32_t now) {
    const int w = LAP_X1 - LAP_X0;
    if (s_feedN && s_feed[0].kind == FeedKind::CATCH && now - s_feed[0].at < 6000) {
        const bool on = (now / 300) % 2;
        s_canvas.fillRect(LAP_X0, LAP_Y0, w, LAP_Y1 - LAP_Y0, on ? 0x2000 : 0x0000);
        char t[16];
        snprintf(t, sizeof t, "%s", s_feed[0].title);
        if (char* sp = strchr(t, ' ')) *sp = '\0';                        // the type alone
        s_canvas.setFont(&fonts::FreeSansBold9pt7b);
        s_canvas.setTextColor(s_feed[0].colour);
        s_canvas.setTextDatum(middle_center);
        s_canvas.drawString(t, (LAP_X0 + LAP_X1) / 2, (LAP_Y0 + LAP_Y1) / 2 - 8);
        s_canvas.setFont(&fonts::Font0);
        s_canvas.setTextColor(0xFFFF);
        s_canvas.drawString("!! CAUGHT !!", (LAP_X0 + LAP_X1) / 2, (LAP_Y0 + LAP_Y1) / 2 + 12);
        s_canvas.setTextDatum(top_left);
        return;
    }
    const uint32_t base = now / 160;                                       // one new line every 160 ms
    for (int r = 0; r < 9; r++) {
        const uint32_t idx = base + r;
        uint32_t h = idx * 2654435761u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        const int indent = (h % 4) * 7;
        int len = 12 + (int)((h >> 8) % (w - 20 - indent));
        if ((h >> 4) % 7 == 0) continue;                                   // a blank line
        const uint16_t col = (h >> 3) % 5 == 0 ? 0xFFE0 : (h >> 3) % 5 == 1 ? 0xBDF7 : 0x07E0;
        const int y = LAP_Y0 + 4 + r * 7;
        if (r == 8) { len = len / 2; if ((now / 400) % 2) s_canvas.fillRect(LAP_X0 + 4 + indent + len + 3, y, 4, 4, 0xFFFF); }
        s_canvas.fillRect(LAP_X0 + 4 + indent, y, len, 3, col);
    }
}

// ---- the Nixie clock ----
void drawNixie(uint32_t now) {
    char d[5] = "----";
    bool on = false;
    if (s_timeOk) {
        time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
        if (s_clock24) snprintf(d, sizeof d, "%02d%02d", lt.tm_hour, lt.tm_min);
        else { int h = lt.tm_hour % 12; if (!h) h = 12; snprintf(d, sizeof d, "%2d%02d", h, lt.tm_min); }
        on = true;
    }
    s_canvas.setFont(&fonts::FreeSans18pt7b);
    s_canvas.setTextDatum(middle_center);
    const int cy = (NIXIE_Y0 + NIXIE_Y1) / 2;
    for (int i = 0; i < 4; i++) {
        const int cx = NIXIE_X[i] + NIXIE_W / 2;
        s_canvas.setTextColor(0x2104);                                    // the unlit numerals behind
        s_canvas.drawString("8", cx, cy);
        if (d[i] == ' ') continue;
        const char ch[2] = { d[i], 0 };
        if (on) {
            s_canvas.setTextColor(0x8200);                                // glow
            s_canvas.drawString(ch, cx - 1, cy); s_canvas.drawString(ch, cx + 1, cy);
            s_canvas.drawString(ch, cx, cy - 1); s_canvas.drawString(ch, cx, cy + 1);
            s_canvas.setTextColor(0xFD60);
        } else {
            s_canvas.setTextColor(0x6200);
        }
        s_canvas.drawString(ch, cx, cy);
    }
    s_canvas.setTextDatum(top_left);
    const uint16_t dot = on && (time(nullptr) % 2 == 0) ? 0xFD60 : 0x4100;
    s_canvas.fillCircle(NIXIE_COLON, cy - 9, 3, dot);
    s_canvas.fillCircle(NIXIE_COLON, cy + 9, 3, dot);
}

// ---- the soldering iron's smoke ----
void drawSmoke(uint32_t now) {
    const uint16_t far = s_lightOn ? 0x7BCF : 0x2945;
    for (int i = 0; i < 7; i++) {
        const int ph = (int)((now / 28 + i * 15) % 105);
        const float k = ph / 105.0f;
        const int x = SOLDER_X + (int)(sinf((ph + i * 13) / 9.0f) * 7 * k);
        const int y = SOLDER_Y - (int)(ph * 1.1f);
        s_canvas.fillCircle(x, y, 1 + ph / 30, mix565(0xDEFB, far, k));
    }
}

// Cuts `s` to what fits in `w` pixels in the current font, with "..".
void fitText(char* s, int w) {
    if (s_canvas.textWidth(s) <= w) return;
    size_t n = strlen(s);
    while (n > 0) {
        s[--n] = '\0';
        char t[96]; snprintf(t, sizeof t, "%s..", s);
        if (s_canvas.textWidth(t) <= w) { strcpy(s, t); return; }
    }
}

// ---- the chalkboard: the latest news, in chalk ----
void drawChalk(uint32_t now) {
    char t[96];
    const int x = CHALK_X0 + 12, w = CHALK_X1 - CHALK_X0 - 24;
    if (!s_feedN) {
        s_canvas.setFont(&fonts::FreeSans12pt7b);
        s_canvas.setTextColor(0xCE79);
        s_canvas.drawString(s_haveKey ? "No news yet." : "Squad locked:", x, CHALK_Y0 + 12);
        s_canvas.setFont(&fonts::FreeSans9pt7b);
        s_canvas.drawString(s_haveKey ? "Squad catches get written up here." : "tap SQUAD PHRASE to read their alerts.", x, CHALK_Y0 + 44);
        return;
    }
    for (uint8_t i = 0; i < s_feedN && i < 4; i++) {
        const FeedItem& f = s_feed[i];
        const int y = CHALK_Y0 + 6 + i * 43;
        char ago[16]; agoText(now - f.at, ago, sizeof ago);
        s_canvas.setFont(&fonts::FreeSans9pt7b);
        s_canvas.setTextColor(0x9CD3);
        s_canvas.setTextDatum(top_right);
        s_canvas.drawString(ago, CHALK_X1 - 12, y + 3);
        s_canvas.setTextDatum(top_left);
        const int agoW = s_canvas.textWidth(ago) + 10;
        s_canvas.setFont(&fonts::FreeSansBold12pt7b);
        s_canvas.setTextColor(mix565(f.colour, 0xFFFF, 0.25f));
        snprintf(t, sizeof t, "%s", f.title); fitText(t, w - agoW);
        s_canvas.drawString(t, x, y);
        s_canvas.setFont(&fonts::FreeSans9pt7b);
        s_canvas.setTextColor(0xDEFB);
        snprintf(t, sizeof t, "%s", f.body); fitText(t, w);
        s_canvas.drawString(t, x, y + 22);
    }
}

// ---- the visitors, on the rug ----
void drawVisitors(uint32_t now) {
    const Board* here[BOARDS_N];
    uint8_t slot[BOARDS_N], n = 0;
    for (uint8_t i = 0; i < BOARDS_N; i++) {
        const Board& b = s_boards[i];
        if (!b.live || !b.present) continue;
        uint8_t j = n++;
        while (j > 0 && here[j - 1]->arriveMs > b.arriveMs) { here[j] = here[j - 1]; slot[j] = slot[j - 1]; j--; }
        here[j] = &b; slot[j] = i;
    }
    if (!n) {
        s_canvas.setFont(&fonts::FreeSans9pt7b);
        s_canvas.setTextColor(0x8410);
        s_canvas.setTextDatum(top_center);
        s_canvas.drawString("nobody home", 300, GROUND_Y - 6);
        s_canvas.setTextDatum(top_left);
        return;
    }
    // Big, at the front of the room: 2.5 times the old stage's size was
    // too big, so three quarters of that (2026-10-18) -- 1.5 times the
    // picture's own size, one or two at a time; a crowd of three a little
    // smaller.
    const uint8_t shown = n > 3 ? 3 : n;
    const float scale = shown == 3 ? 1.25f : 1.5f;
    const int cell = shown == 3 ? 320 : 360, start = shown == 1 ? 110 : 50;
    // They wander about their spot, facing the way they walk; with two or
    // more in, now and then the first two meet in the middle for a high
    // five (2026-10-19).
    static uint32_t hiAt = 0, hiNext = 0;
    if (shown >= 2) {
        if (!hiNext) hiNext = now + 20000;
        if (!hiAt && (int32_t)(now - hiNext) >= 0) hiAt = now;
    }
    const float ht = hiAt ? (now - hiAt) / 4000.0f : -1;
    if (ht >= 1) { hiAt = 0; hiNext = now + 35000 + esp_random() % 40000; }
    int meetX = 0;
    if (ht >= 0 && ht < 1 && shown >= 2) meetX = (start + cell / 2 + start + cell + cell / 2) / 2;
    for (uint8_t k = 0; k < shown; k++) {
        const Board& b = *here[k];
        const float ph = s_party ? now / (380.0f + k * 70.0f) + k * 2.1f
                                 : now / (5200.0f + k * 1300.0f) + k * 2.1f;
        int cx = start + cell * k + cell / 2 + (int)((s_party ? 130 : 70) * sinf(ph));
        bool left = cosf(ph) < 0;                                            // which way he walks
        int jump = s_party ? (int)(55 * fabsf(sinf(now / 140.0f + k * 1.3f))) : 0;
        if (meetX && k < 2 && ht >= 0 && ht < 1) {
            const int target = meetX + (k == 0 ? -95 : 95);
            const float m = ht < 0.35f ? ht / 0.35f : ht > 0.7f ? (1 - ht) / 0.3f : 1;
            cx = cx + (int)((target - cx) * m);
            left = (k == 1);                                                 // facing each other
            if (ht > 0.35f && ht < 0.7f) jump = (int)(30 * sinf((ht - 0.35f) / 0.35f * 3.14159f));
        }
        const float t = (now - b.arriveMs) / 1400.0f;                       // walks in from the left
        if (t < 1) { cx -= (int)((1 - t) * (1 - t) * (cx + 240)); left = false; }
        const int bob = (int)(3 * sinf(now / 260.0f + k * 1.7f)) - jump;
        const bool quiet = now - b.lastMs > 20000;
        if (const M5Canvas* spr = lookSprite(slot[k], b, scale)) {
            M5Canvas* sp = const_cast<M5Canvas*>(spr);
            M5Canvas* L = s_lookSprL[slot[k]];
            if (left && L && L->getBuffer()) sp = L;
            sp->pushSprite(&s_canvas, cx - sp->width() / 2, GROUND_Y - sp->height() + bob, LOOK_KEY);
            noteDirty(cx - sp->width() / 2 - 8, GROUND_Y - sp->height() + bob - 8, sp->width() + 16, sp->height() + 60);
        } else
            silhouette(cx, GROUND_Y + bob, scale, quiet ? DIM : ACCENT);
        char t2[48];
        snprintf(t2, sizeof t2, "%s  %d dBm", b.name, b.rssi);
        s_canvas.setFont(&fonts::FreeSansBold9pt7b);
        const int tw = s_canvas.textWidth(t2) + 16;
        s_canvas.fillRoundRect(cx - tw / 2, GROUND_Y + 6, tw, 24, 8, 0x0841);
        s_canvas.setTextColor(quiet ? DIM : INK);
        s_canvas.setTextDatum(middle_center);
        s_canvas.drawString(t2, cx, GROUND_Y + 18);
        s_canvas.setTextDatum(top_left);
    }
    if (meetX && ht > 0.35f && ht < 0.75f) {
        const int by = GROUND_Y - 330;
        for (int i = 0; i < 10; i++) {
            const float a = i * 0.628f + now / 300.0f;
            const int r0 = 14, r1 = 40 + (int)(10 * sinf(now / 90.0f + i));
            s_canvas.drawLine(meetX + (int)(cosf(a) * r0), by + (int)(sinf(a) * r0),
                              meetX + (int)(cosf(a) * r1), by + (int)(sinf(a) * r1), i % 2 ? 0xFFE0 : 0xFD20);
        }
        s_canvas.setFont(&fonts::FreeSansBold18pt7b);
        s_canvas.setTextDatum(middle_center);
        s_canvas.setTextColor(0x0000); s_canvas.drawString("HIGH FIVE!", meetX + 3, by - 60 + 3);
        s_canvas.setTextColor(0xFFE0); s_canvas.drawString("HIGH FIVE!", meetX, by - 60);
        s_canvas.setTextDatum(top_left);
    }
    if (n > shown) {
        char t2[16]; snprintf(t2, sizeof t2, "+%u", n - shown);
        s_canvas.setFont(&fonts::FreeSansBold18pt7b);
        s_canvas.setTextColor(ACCENT);
        s_canvas.drawString(t2, start + shown * cell, GROUND_Y - 60);
    }
}

// ---- the tally along the bottom, like a SquachWatch's own ----
void drawTallyBar(uint32_t now) {
    struct Row { const char* type; uint16_t devs; };
    static Row rows[20]; static uint8_t nr = 0; static uint32_t at = 0;
    if (!at || now - at > 2000) {                                         // recount every 2 s
        at = now | 1; nr = 0;
        for (uint16_t i = 0; i < s_devN; i++) {
            uint8_t r = 0;
            while (r < nr && strcmp(rows[r].type, s_devs[i].type)) r++;
            if (r == nr) { if (nr == 20) continue; rows[nr++] = { s_devs[i].type, 0 }; }
            rows[r].devs++;
        }
        for (uint8_t a = 1; a < nr; a++)
            for (uint8_t b = a; b > 0; b--) {
                const uint8_t ta = tierForName(rows[b].type), tb = tierForName(rows[b - 1].type);
                if (ta < tb || (ta == tb && rows[b].devs > rows[b - 1].devs)) { Row x = rows[b]; rows[b] = rows[b - 1]; rows[b - 1] = x; }
                else break;
            }
    }
    const int W = M5.Display.width(), tw = W / 8;
    s_canvas.fillRect(0, TALLY_Y, W, M5.Display.height() - TALLY_Y, PANEL);
    s_canvas.drawFastHLine(0, TALLY_Y, W, 0x4208);
    char t[16];
    for (uint8_t i = 0; i < 8; i++) {
        const int x = i * tw;
        if (i) s_canvas.drawFastVLine(x, TALLY_Y + 10, 52, 0x2945);
        const char* name; uint16_t col; unsigned count;
        if (i == 0) { name = "DEVICES"; col = ACCENT; count = s_devN; }
        else if (i - 1 < nr) { name = rows[i - 1].type; col = colourForName(name); count = rows[i - 1].devs; }
        else break;
        s_canvas.fillCircle(x + 18, TALLY_Y + 20, 6, col);
        s_canvas.setFont(&fonts::FreeSans9pt7b);
        s_canvas.setTextColor(DIM);
        s_canvas.drawString(name, x + 30, TALLY_Y + 12);
        s_canvas.setFont(&fonts::FreeSansBold18pt7b);
        s_canvas.setTextColor(i == 0 ? ACCENT : INK);
        snprintf(t, sizeof t, "%u", count);
        s_canvas.drawString(t, x + 14, TALLY_Y + 32);
    }
}

// ---- the seasons ---------------------------------------------------------------
// October brings a jack-o'-lantern to the corner of the floor, its candle
// flickering, and now and then a ghost drifts across the room and fades;
// December, a tree with twinkling lights. Those months only (asked for
// 2026-10-19). Pixel art at four screen pixels a dot, to sit with the
// backdrop. /squachwatch-base/season.txt on the card -- "halloween",
// "christmas" or "none" -- overrides the calendar, to see one early.
static const char* const PUMPKIN[] = {
    "............gg..........",
    "...........Gg...........",
    "...........Gg...........",
    "...........Gg...........",
    ".......kkkkkkkkkk.......",
    "....kkkoOoooOooOokkk....",
    "...koooOooooOoooOoook...",
    "..kooooOooooOoooOooook..",
    ".kooooOfooooOooofOooook.",
    ".koooofffoooOoofffooook.",
    "koooofffffooOofffffooook",
    "koooooOooooffooooOoooook",
    "koooooOoooffffoooOoooook",
    ".kooooOooooooooooOooook.",
    ".koooofffffoofffffooook.",
    "..kooffffffffffffffook..",
    "...kofofffooOofffofok...",
    "....kkkoOoooOooOokkk....",
    ".......kkkkkkkkkk.......",
};
static const char* const GHOST[] = {
    "..............",
    "....ssssss....",
    "..sswwwwwwss..",
    ".swwwwwwwwwws.",
    ".swwwwwwwwwws.",
    ".swweewwweews.",
    "swwweewwweewws",
    "swwwwwwwwwwwws",
    "swwwwwwwwwwwws",
    "swwwwwwewwwwws",
    "swwwwwewewwwws",
    "swwwwwwewwwwws",
    "swwwwwwwwwwwws",
    "swwssswwwsssws",
    "sss...sss...s.",
    "...sss...sss..",
};
// A flying saucer and its pilot, for the window.
static const char* const UFO[] = {
    "......DDDD......",
    ".....DDDDDD.....",
    "....DDDDDDDD....",
    "..SSSSSSSSSSSS..",
    "SSLSSLSSLSSLSSLS",
    "..SSSSSSSSSSSS..",
    "....SS....SS....",
};
static const char* const ALIEN[] = {
    ".A...A.",
    "..A.A..",
    ".AAAAA.",
    "AAeAeAA",
    ".AAAAA.",
    "..AAA..",
    ".AAAAA.",
    ".A...A.",
};
static const char* const SCARY_GHOST[] = {
    "..................",
    ".....ssssssss.....",
    "....swwwwwwwws....",
    "...swwwwwwwwwws...",
    "..swwwwwwwwwwwws..",
    ".swwwwwwwwwwwwwws.",
    ".swweewwwwwweewws.",
    ".swwerewwwwerewws.",
    ".swweewwwwwweewws.",
    ".swwwwwwwwwwwwwws.",
    ".swwwwwwwwwwwwwws.",
    ".swwwwwewewwwwwws.",
    ".swwwweeeeeewwwws.",
    "..swwweeeeeewwws..",
    "..swwweeeeeewwws..",
    "..swwwwwewewwwws..",
    "..swwwwwwwwwwwws..",
    "...swwwwwswssws...",
    ".sswwwsws.s..sssss",
    "...sws.s..s....sws",
    "....s..s........s.",
    "....s...........s.",
};
static const char* const LEGS0[] = {
    ".....HHhhHhhH.......",
    "......hhHhhHh.......",
    ".....HhHhhHhh.......",
    ".....hHhhHhhHh......",
    "....HHhhHhhHhh......",
    ".....hhHhhHhhH......",
    "....HhHhhHhhHh......",
    ".....HhhHhhHhh......",
    "....HhhHhhHhhH......",
    "....hhHhh.hhHhh.....",
    "...HhHhhHHhHhhH.....",
    "....HhhHh.HhhHh.....",
    "...HhhHhhHhhHhh.....",
    "....hHhhH.hHhhH.....",
    "..HhHhhH..HhhHhh....",
    "...HhhHh...hHhhH....",
    "..FFFFFFFFFFFFFFFF..",
    "..FFFFFFFFFFFFFFFF..",
    "..FFFFFFFFFFFFFFFFF.",
    "..FFFFFFFFFFFFFFFFF.",
    ".......FFFF....FFFF.",
};
static const char* const LEGS1[] = {
    "....HhHhhHhhHhh.....",
    ".....HhhHhhHhhH.....",
    "....HhhHhhHhhHh.....",
    ".....hHhhHhhHhh.....",
    "....HHhhHhhHhhH.....",
    ".....hhHhhHhhHh.....",
    "....HhHhhHhhHhh.....",
    ".....HhhHhhHhhH.....",
    ".....HhHhhHhhH......",
    "......HhhHhhHh......",
    ".....HhhHhhHhh......",
    "......hHhhHhhH......",
    ".....HHhhHhhHh......",
    "......hhHhhHhh......",
    ".....HhHhhHhhH......",
    "......HhhHhhHh......",
    ".....FFFFFFFFFFF....",
    ".....FFFFFFFFFFF....",
    ".....FFFFFFFFFFFF...",
    ".....FFFFFFFFFFFF...",
    "..........FFFFFFF...",
};
static const char* const PEEK[] = {
    "..........hhHhhhH.....",
    "........hHhhhHhhhHh...",
    "......HhhhHhhhHhhhHhh.",
    ".....hhHhhhHhhhHhhhHhh",
    ".....HHHHHHHHHHHHHHHHh",
    "....hHhhhHhhhHhhhHhhhH",
    "....hhHhyyHhhhHyyhHhhh",
    "...HhhhHYYhHhhhYYhhHhh",
    "...hHhhhHhhhnnhhHhhhHh",
    "...hhHhhhHhhnnhhhHhhhH",
    "...hhhHhhhHhhhHhhhHhhh",
    "...HhhhHhhhHhhhHhhhHhh",
    "....HhhhHmmmmmmmmhhhHh",
    "....hHhhhHhhhHhhhHhhhH",
};

static const char* const TREE[] = {
    "..........y..........",
    "........yyyyy........",
    "..........y..........",
    ".........TTt.........",
    ".........TTt.........",
    "........TT1tt........",
    ".......TTTTttt.......",
    ".......TTTTttt.......",
    "......TT2TTtttt......",
    ".....TTTTTTt3ttt.....",
    "....TTTTTTTtttttt....",
    "......TTTrTtttt......",
    ".....TT1TTTtt2tt.....",
    ".....TTTTTTttttt.....",
    "....TTTTTT3tttttt....",
    "....TTTTTTTtttttt....",
    "...TTTTTTTTrtttttt...",
    "..TTTT1TTTTttt2tttt..",
    "....TTTTTTTtttttt....",
    "....TTTTT3Ttttttt....",
    "...TTTTrTTTt1ttttt...",
    "...TTTTTTTTttttttt...",
    "..TTT2TTTT3tttt3ttt..",
    "..TTTTTTTTTttrttttt..",
    ".TTTTTTT1TTt2ttttttt.",
    "TTTT2TTTTTTttttt1tttt",
    ".........bbb.........",
    ".........bbb.........",
    "......ppppppppp......",
    "......ppppppppp......",
    "......ppppppppp......",
};

enum class Season : uint8_t { NONE, HALLOWEEN, CHRISTMAS };
int8_t s_seasonForce = -1;            // from season.txt; -1 = the calendar

void loadSeason() {
    if (!s_sdOk) return;
    File f = SD_MMC.open("/squachwatch-base/season.txt", FILE_READ);
    if (!f) return;
    char w[16] = "";
    const size_t n = f.readBytes(w, sizeof w - 1);
    w[n] = 0;
    f.close();
    if (strncasecmp(w, "hallow", 6) == 0)      s_seasonForce = (int8_t)Season::HALLOWEEN;
    else if (strncasecmp(w, "christ", 6) == 0
          || strncasecmp(w, "xmas", 4) == 0)   s_seasonForce = (int8_t)Season::CHRISTMAS;
    else if (strncasecmp(w, "none", 4) == 0)   s_seasonForce = (int8_t)Season::NONE;
    Serial.printf("[season] season.txt: %s\n", w);
}

Season season() {
    if (s_seasonForce >= 0) return (Season)s_seasonForce;
    if (!s_timeOk) return Season::NONE;
    time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
    if (lt.tm_mon == 9)  return Season::HALLOWEEN;      // October
    if (lt.tm_mon == 11) return Season::CHRISTMAS;      // December
    return Season::NONE;
}

// The candle: two slow waves and a fast one, 0..1.
float flicker(uint32_t now) {
    const float v = 0.55f + 0.25f * sinf(now / 97.0f) + 0.15f * sinf(now / 41.0f + 1.3f) + 0.05f * sinf(now / 13.0f);
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

uint16_t artColour(char c, uint32_t now) {
    switch (c) {
        case 'o': return 0xF3C2;  case 'O': return 0xBA81;  case 'k': return 0x5941;
        case 'g': return 0x3C65;  case 'G': return 0x1AC3;
        case 'f': return mix565(0xFB80, 0xFF4C, flicker(now));
        case 'w': return 0xEF9F;  case 's': return 0xADBA;  case 'e': return 0x10A3;
        case 'T': return 0x2467;  case 't': return 0x1B45;  case 'b': return 0x6A23;
        case 'p': return 0xB145;  case 'r': return 0xD8E3;
        case 'h': return 0x59E4;  case 'H': return 0x3943;  case 'F': return 0x3122;
        case 'Y': return 0xCCA5;  case 'n': return 0x28C2;  case 'm': return 0x1881;
        case 'D': return 0x8F1F;  case 'S': return 0xAD75;  case 'A': return 0x5FE8;
        case 'L': { static const uint16_t U[3] = { 0xF800, 0xFFE0, 0x07FF };
                    return U[(now / 150) % 3]; }
        case 'y': return (now / 400) % 2 ? 0xFF2C : 0xFFF8;
        case '1': case '2': case '3': {
            static const uint16_t L[3] = { 0xF9E7, 0x551F, 0xFEC7 };
            const uint8_t i = (uint8_t)((c - '1' + now / 700) % 3);
            return ((now / 350 + c) % 5) == 0 ? 0x2945 : L[i];      // now and then one blinks off
        }
    }
    return 0;
}

void drawArt(const char* const* rows, int h, int x0, int y0, int s, uint32_t now, uint8_t alpha = 255, bool flip = false) {
    const int w = (int)strlen(rows[0]);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const char c = rows[y][flip ? w - 1 - x : x];
            if (c == '.') continue;
            const uint16_t col = artColour(c, now);
            if (alpha >= 255) s_canvas.fillRect(x0 + x * s, y0 + y * s, s, s, col);
            else              s_canvas.fillRectAlpha(x0 + x * s, y0 + y * s, s, s, alpha, col);
        }
}

// A soft round glow: rows of see-through colour.
void glow(int cx, int cy, int r, uint8_t alpha, uint16_t col) {
    for (int dy = -r; dy <= r; dy += 2) {
        const int hw = (int)sqrtf((float)(r * r - dy * dy));
        s_canvas.fillRectAlpha(cx - hw, cy + dy, 2 * hw, 2, alpha, col);
    }
}

constexpr int SEASON_X = 1150;        // the floor's right-hand corner
void drawSeason(uint32_t now) {
    const Season se = season();
    if (se == Season::HALLOWEEN) {
        const int h = sizeof PUMPKIN / sizeof PUMPKIN[0], w = (int)strlen(PUMPKIN[0]) * 4;
        const int y0 = GROUND_Y + 28 - h * 4;
        const float f = flicker(now);
        glow(SEASON_X + w / 2, y0 + h * 2 + 8, 70 + (int)(14 * f), (uint8_t)(18 + 14 * f), 0xFC60);
        glow(SEASON_X + w / 2, y0 + h * 2 + 8, 40 + (int)(8 * f), (uint8_t)(16 + 12 * f), 0xFE60);
        drawArt(PUMPKIN, h, SEASON_X, y0, 4, now);
    } else if (se == Season::CHRISTMAS) {
        const int h = sizeof TREE / sizeof TREE[0], w = (int)strlen(TREE[0]) * 4;
        const int y0 = GROUND_Y + 30 - h * 4;
        glow(SEASON_X + w / 2, y0 + 8, 26, 26, 0xFFE0);        // the star
        drawArt(TREE, h, SEASON_X, y0, 4, now);
    }
}

// The ghost: every minute or two in October, five seconds across the room.
void drawGhost(uint32_t now) {
    static uint32_t next = 0, start = 0;
    static int fromX = 0, toX = 0, baseY = 0;
    if (season() != Season::HALLOWEEN) { next = 0; return; }
    if (!next) next = now + 15000;
    if (!start) {
        if ((int32_t)(now - next) < 0) return;
        start = now;
        const bool ltr = esp_random() & 1;
        fromX = ltr ? -60 : 1280; toX = ltr ? 1280 : -60;
        baseY = 160 + (int)(esp_random() % 260);
    }
    // Faster and bigger than the first one, with a swoop down and up, and
    // a low "wooo" as it comes in (sound allowing).
    const float t = (now - start) / 3200.0f;
    if (t >= 1) { start = 0; next = now + 40000 + esp_random() % 80000; return; }
    static uint32_t wooAt = 0;
    if (t < 0.05f && wooAt != start) { wooAt = start; if (s_sound == 0 || (s_sound == 1 && !nightNow())) M5.Speaker.tone(196, 700); }
    const float a = t < 0.12f ? t / 0.12f : t > 0.75f ? (1 - t) / 0.25f : 1;
    const int x = fromX + (int)((toX - fromX) * t);
    const int y = baseY + (int)(60 * sinf(t * 3.14f)) + (int)(14 * sinf(t * 18.0f));
    const int h = sizeof SCARY_GHOST / sizeof SCARY_GHOST[0];
    drawArt(SCARY_GHOST, h, x, y, 5, now, (uint8_t)(230 * a), toX < fromX);
    noteDirty(x - 30, y - 30, 18 * 5 + 60, h * 5 + 60);
}

// ---- the window: the real sky, the real weather, and a visitor outside ----------
// The glass in the backdrop is night; over it, the sky by the sun's place for
// home (a sunrise and sunset worked out from the date and latitude), stars and
// the moon at night, and the weather from Open-Meteo once the Tab5 is on WiFi
// (the position sent rounded to about a kilometre). Now and then a pair of big
// hairy legs walks past outside, or he crouches and looks in (2026-10-19).
constexpr int WIN_X0 = 496, WIN_Y0 = 64, WIN_X1 = 785, WIN_Y1 = 149, WIN_GROUND = 136;

volatile int16_t s_wxCode = -1;            // WMO weather code, -1 unknown
volatile float   s_wxTemp = 0;
volatile uint32_t s_wxAt = 0;
TaskHandle_t     s_wxTask = nullptr;

void weatherTask(void*) {
    int32_t la = s_homeLat7, lo = s_homeLon7;
    if (Gnss::fresh(millis())) { la = Gnss::fix().lat7; lo = Gnss::fix().lon7; }
    char url[200];
    snprintf(url, sizeof url, "http://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
             "&current=temperature_2m,weather_code", la / 1e7, lo / 1e7);
    HTTPClient http;
    if (http.begin(url)) {
        http.setTimeout(8000);
        if (http.GET() == 200) {
            const String body = http.getString();
            const int cur = body.indexOf("\"current\":{");
            if (cur >= 0) {
                const int c = body.indexOf("\"weather_code\":", cur), tt = body.indexOf("\"temperature_2m\":", cur);
                if (c > 0)  s_wxCode = (int16_t)body.substring(c + 15).toInt();
                if (tt > 0) s_wxTemp = body.substring(tt + 17).toFloat();
                s_wxAt = millis() | 1;
                Serial.printf("[weather] code %d, %.1f C\n", (int)s_wxCode, (double)s_wxTemp);
            }
        }
        http.end();
    }
    s_wxTask = nullptr;
    vTaskDelete(nullptr);
}

void weatherTick(uint32_t now) {
    static uint32_t last = 0;
    if (s_wxTask || WiFi.status() != WL_CONNECTED || !(s_homeOk || Gnss::fresh(now))) return;
    if (last && now - last < 15 * 60000UL) return;
    last = now | 1;
    xTaskCreate(weatherTask, "weather", 8192, nullptr, 1, &s_wxTask);
}

// Sunrise and sunset in local hours, from the date and latitude (good to a
// quarter of an hour; no equation of time).
bool sunTimes(float& rise, float& set, float& hourNow) {
    if (!s_clockSet || !s_homeOk) return false;            // no clock or no home: the backdrop's night
    time_t t = time(nullptr);
    struct tm lt, gt;
    localtime_r(&t, &lt); gmtime_r(&t, &gt);
    int off = (lt.tm_hour - gt.tm_hour) * 60 + (lt.tm_min - gt.tm_min);
    if (lt.tm_yday != gt.tm_yday) off += (lt.tm_yday > gt.tm_yday || (lt.tm_yday == 0 && gt.tm_yday > 300)) ? 1440 : -1440;
    const float lat = s_homeLat7 / 1e7f, lon = s_homeLon7 / 1e7f;
    const float decl = 23.44f * sinf(6.2831853f * (284 + lt.tm_yday + 1) / 365.0f) * 0.0174533f;
    float cosH = -tanf(lat * 0.0174533f) * tanf(decl);
    if (cosH > 1) cosH = 1; if (cosH < -1) cosH = -1;
    const float half = acosf(cosH) * 57.29578f / 15.0f;
    const float noon = 12.0f - lon / 15.0f + off / 60.0f;
    rise = noon - half; set = noon + half;
    hourNow = lt.tm_hour + lt.tm_min / 60.0f + lt.tm_sec / 3600.0f;
    return true;
}

uint16_t skyAt(float y01, float day, float glow) {
    // night navy -> day blue, with sunrise/sunset orange near the horizon
    const uint16_t top = mix565(0x0844, 0x3D9F, day), bot = mix565(0x18E7, 0x9EFF, day);
    uint16_t c = mix565(top, bot, y01);
    if (glow > 0) c = mix565(c, 0xFB26, glow * y01 * 0.85f);
    return c;
}

void drawWindowView(uint32_t now) {
    float rise, set, hr;
    if (!sunTimes(rise, set, hr)) return;                 // no clock yet: the backdrop's night
    // day 0..1, glow at sunrise and sunset
    float day;
    if (hr < rise - 0.6f || hr > set + 0.6f) day = 0;
    else if (hr < rise + 0.6f) day = (hr - (rise - 0.6f)) / 1.2f;
    else if (hr > set - 0.6f)  day = ((set + 0.6f) - hr) / 1.2f;
    else day = 1;
    const float glow = (day > 0 && day < 1) ? 1 - fabsf(day * 2 - 1) : 0;
    const int16_t wx = s_wxCode;
    const bool cloudy = wx == 2 || wx == 3 || wx >= 45, overcast = wx == 3 || wx >= 51;
    float dayShown = day * (overcast ? 0.7f : 1.0f);
    s_canvas.setClipRect(WIN_X0, WIN_Y0, WIN_X1 - WIN_X0, WIN_Y1 - WIN_Y0);
    for (int y = WIN_Y0; y < WIN_Y1; y += 2)
        s_canvas.fillRect(WIN_X0, y, WIN_X1 - WIN_X0, 2, skyAt((y - WIN_Y0) / (float)(WIN_GROUND - WIN_Y0), dayShown, glow));
    if (day < 0.5f && !overcast)                          // stars
        for (int i = 0; i < 28; i++) {
            const uint32_t h = (uint32_t)i * 2654435761u;
            const int x = WIN_X0 + 4 + (int)(h % 280), y = WIN_Y0 + 3 + (int)((h >> 9) % 50);
            if (((now / 700) + i) % 9) s_canvas.fillRect(x, y, 2, 2, mix565(0xFFFF, 0x2104, day * 2));
        }
    if (day > 0) {                                        // the sun, across the day
        const float f = (hr - rise) / (set - rise);
        if (f > -0.05f && f < 1.05f) {
            const int x = WIN_X0 + 10 + (int)(f * 270), y = WIN_GROUND - 6 - (int)(sinf(f * 3.14159f) * 56);
            if (!overcast) {
                s_canvas.fillCircle(x, y, 13, mix565(0xFFE6, skyAt(0.3f, dayShown, glow), 0.6f));
                s_canvas.fillCircle(x, y, 9, glow > 0.3f ? 0xFD20 : 0xFFE6);
            }
        }
    } else {                                              // the moon, across the night
        float f = hr > set ? (hr - set) / (24 - set + rise) : (hr + 24 - set) / (24 - set + rise);
        f = f - floorf(f);
        if (!overcast) {
            const int x = WIN_X0 + 10 + (int)(f * 270), y = WIN_GROUND - 10 - (int)(sinf(f * 3.14159f) * 50);
            s_canvas.fillCircle(x, y, 8, 0xEF59);
            s_canvas.fillCircle(x + 2, y - 2, 2, 0xC638);
        }
    }
    if (cloudy) {                                         // clouds drifting
        const uint16_t cc = mix565(0x2945, overcast ? 0x9CF3 : 0xDEFB, dayShown);
        for (int i = 0; i < (overcast ? 5 : 3); i++) {
            const int x = WIN_X0 - 60 + (int)fmodf(now / (260.0f + i * 40) + i * 97, 380);
            const int y = WIN_Y0 + 8 + i * 9 % 30;
            s_canvas.fillRoundRect(x, y, 70, 16, 8, cc);
            s_canvas.fillRoundRect(x + 14, y - 7, 36, 16, 8, cc);
        }
    }
    const bool rain = (wx >= 51 && wx <= 67) || (wx >= 80 && wx <= 82) || wx >= 95;
    const bool snow = (wx >= 71 && wx <= 77) || wx == 85 || wx == 86;
    if (rain) {
        const int nDrops = wx <= 55 ? 18 : 40;
        for (int i = 0; i < nDrops; i++) {
            const uint32_t h = (uint32_t)i * 2246822519u;
            const int x = WIN_X0 + (int)(h % 290);
            const int y = WIN_Y0 + (int)fmodf(now / 3.0f + (h >> 8) % 90, 80);
            s_canvas.drawLine(x, y, x - 2, y + 7, mix565(0x5AEB, 0xBDF7, dayShown));
        }
    }
    if (snow)
        for (int i = 0; i < 34; i++) {
            const uint32_t h = (uint32_t)i * 2246822519u;
            const int x = WIN_X0 + (int)(h % 290) + (int)(4 * sinf(now / 600.0f + i));
            const int y = WIN_Y0 + (int)fmodf(now / 25.0f + (h >> 8) % 90, 80);
            s_canvas.fillRect(x, y, 3, 3, 0xFFFF);
        }
    if (wx == 45 || wx == 48) s_canvas.fillRectAlpha(WIN_X0, WIN_Y0, WIN_X1 - WIN_X0, WIN_Y1 - WIN_Y0, 120, 0xBDF7);

    // the ground outside, grass against the sky
    const uint16_t grass = mix565(0x0A02, 0x2C64, dayShown), soil = mix565(0x18A2, 0x4A45, dayShown);
    s_canvas.fillRect(WIN_X0, WIN_GROUND, WIN_X1 - WIN_X0, WIN_Y1 - WIN_GROUND, soil);
    for (int x = WIN_X0; x < WIN_X1; x += 4) {
        const int bh = 4 + (int)((x * 7) % 5) * 2;
        s_canvas.fillRect(x, WIN_GROUND - bh, 4, bh, grass);
    }

    // Sasquatch, outside: every few minutes
    static uint32_t next = 0, start = 0; static uint8_t kind = 0;
    if (!next) next = now + 90000;
    if (!start && (int32_t)(now - next) >= 0) { start = now; kind = (esp_random() % 4 == 0) ? 1 : 0; }
    if (start) {
        const float t = (now - start) / (kind ? 5000.0f : 7000.0f);
        if (t >= 1) { start = 0; next = now + 180000 + esp_random() % 180000; }
        else if (kind == 0) {                             // legs walk past
            const int x = WIN_X0 - 90 + (int)(t * 400);
            const bool f = (now / 300) % 2;
            const char* const* L = f ? LEGS1 : LEGS0;
            drawArt(L, sizeof LEGS0 / sizeof LEGS0[0], x, WIN_GROUND - 84, 4, now, day > 0.3f ? 255 : 200);
        } else {                                          // he looks in
            const float in = t < 0.15f ? t / 0.15f : t > 0.8f ? (1 - t) / 0.2f : 1;
            const int x = WIN_X1 - (int)(in * 110);
            drawArt(PEEK, sizeof PEEK / sizeof PEEK[0], x, WIN_GROUND - 56, 4, now);
        }
    }
    // A UFO now and then: a fly-by, or a landing on the grass where a little
    // green alien hops out, looks about and gets back in (2026-10-19).
    // Never while Sasquatch is about.
    static uint32_t uNext = 0, uStart = 0; static bool uLand = false;
    if (!uNext) uNext = now + 150000;
    if (!uStart && !start && (int32_t)(now - uNext) >= 0) { uStart = now; uLand = esp_random() & 1; }
    if (uStart) {
        const float dur = uLand ? 16000.0f : 3500.0f;
        const float t = (now - uStart) / dur;
        const int uw = 16 * 3, uh = 7 * 3, uLines = sizeof UFO / sizeof UFO[0];
        if (t >= 1) { uStart = 0; uNext = now + 360000 + esp_random() % 240000; }
        else if (!uLand) {
            const int x = WIN_X0 - uw + (int)(t * (WIN_X1 - WIN_X0 + uw));
            const int y = WIN_Y0 + 10 + (int)(8 * sinf(t * 20));
            drawArt(UFO, uLines, x, y, 3, now);
        } else {
            const int landX = 548, landY = WIN_GROUND - uh + 3;
            const float sec = t * 16;
            int x = landX, y = landY;
            if (sec < 3) {                                   // in from the left, down to the grass
                const float f = sec / 3;
                x = WIN_X0 - uw + (int)(f * (landX - WIN_X0 + uw));
                y = WIN_Y0 + 4 + (int)(f * f * (landY - WIN_Y0 - 4));
            } else if (sec > 12) {                           // off up to the right, fast
                const float f = (sec - 12) / 4;
                x = landX + (int)(f * f * 320);
                y = landY - (int)(f * 120);
            }
            if (sec >= 3 && sec < 12) {                      // landed: the beam, the pilot
                const bool beam = sec < 4 || sec > 11;
                if (beam) s_canvas.fillRectAlpha(x + 12, y + uh, uw - 24, WIN_GROUND - y - uh + 2, 110, 0xFFF0);
                if (sec >= 4 && sec < 11) {
                    float ax = x + uw + 4;
                    const float w = sec - 4;                 // 7 s on the grass
                    ax += (w < 2.5f ? w : w < 4.5f ? 2.5f : 7 - w) * 18;
                    const int hop = ((int)(sec * 4) % 2) ? 3 : 0;
                    const int ah = sizeof ALIEN / sizeof ALIEN[0];
                    drawArt(ALIEN, ah, (int)ax, WIN_GROUND - ah * 3 - hop, 3, now);
                }
            }
            drawArt(UFO, uLines, x, y, 3, now);
        }
    }
    s_canvas.clearClipRect();
    s_canvas.fillRect(636, WIN_Y0, 8, WIN_Y1 - WIN_Y0, 0x7A86);       // the mullion, in front
    if (s_wxAt) {                                         // the temperature, in the corner
        char tt[12]; snprintf(tt, sizeof tt, "%d C", (int)lroundf(s_wxTemp));
        s_canvas.setFont(&fonts::FreeSansBold9pt7b);
        s_canvas.setTextDatum(top_right);
        s_canvas.setTextColor(0x0000); s_canvas.drawString(tt, WIN_X1 - 5, WIN_Y0 + 5);
        s_canvas.setTextColor(0xFFFF); s_canvas.drawString(tt, WIN_X1 - 6, WIN_Y0 + 4);
        const int cw = s_canvas.textWidth("C");
        s_canvas.drawCircle(WIN_X1 - 6 - cw - 4, WIN_Y0 + 7, 2, 0xFFFF);
        s_canvas.setTextDatum(top_left);
    }
}

// ---- the dance party (a hidden one) --------------------------------------------
// The room with the lights down, made once a party (it was a blend of the
// whole room every frame).
M5Canvas s_bgDark(&M5.Display);
bool     s_bgDarkOk = false;
void makePartyBackdrop() {
    if (!s_bgDark.getBuffer()) {
        s_bgDark.setPsram(true);
        s_bgDark.setColorDepth(16);
        if (!s_bgDark.createSprite(M5.Display.width(), M5.Display.height())) return;
    }
    s_bg.pushSprite(&s_bgDark, 0, 0);
    s_bgDark.fillRectAlpha(0, 70, 1280, TALLY_Y - 70, 150, 0x0000);
    s_bgDarkOk = true;
}

void partyBack(uint32_t now) {
    if (!s_party) return;
    // two speaker stacks, woofers on the beat
    const float beat = fabsf(sinf(now / 140.0f));
    for (int side = 0; side < 2; side++) {
        const int x = side ? 1110 : 20, w = 150;
        for (int box = 0; box < 2; box++) {
            const int y = 300 + box * 170, h = 165;
            s_canvas.fillRoundRect(x, y, w, h, 8, 0x18C3);
            s_canvas.drawRoundRect(x, y, w, h, 8, 0x4208);
            const int r = 44 + (int)(beat * 7);
            s_canvas.fillCircle(x + w / 2, y + 95, r, 0x0841);
            s_canvas.drawCircle(x + w / 2, y + 95, r, 0x6B4D);
            s_canvas.fillCircle(x + w / 2, y + 95, 16 + (int)(beat * 4), 0x31A6);
            s_canvas.fillCircle(x + w / 2, y + 30, 14, 0x0841);
            s_canvas.drawCircle(x + w / 2, y + 30, 14, 0x6B4D);
        }
    }
}

void partyFront(uint32_t now) {
    if (!s_party) return;
    // lasers from three heads in the ceiling, sweeping
    static const uint16_t C[3] = { 0xF800, 0x07E0, 0xB81F };
    static const int HX[3] = { 330, 640, 950 };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            const float a = now / (900.0f + i * 230 + j * 170) + i * 2 + j * 1.7f;
            const int fx = 640 + (int)(620 * sinf(a)), fy = 420 + (int)(200 * sinf(a * 1.3f + j));
            for (int d = -1; d <= 1; d++) s_canvas.drawLine(HX[i] + d, 40, fx + d, fy, C[(i + j) % 3]);
            s_canvas.fillCircle(fx, fy, 6, C[(i + j) % 3]);
        }
    // the disco ball, lowered on its string and turning
    const float drop = (now - s_partyAt) / 2000.0f;
    const int by = 40 + (int)((drop < 1 ? drop : 1) * 190), bx = 640, br = 44;
    s_canvas.drawLine(bx, 0, bx, by - br, 0x8410);
    s_canvas.fillCircle(bx, by, br, 0x8C71);
    for (int yy = -br + 6; yy < br; yy += 9)
        for (int xx = -br + 6; xx < br; xx += 9) {
            if (xx * xx + yy * yy > (br - 4) * (br - 4)) continue;
            const uint32_t h = (uint32_t)((xx + 64) * 131 + (yy + 64) * 17) + now / 120;
            const uint16_t c = (h % 7 == 0) ? 0xFFFF : (h % 5 == 0) ? C[h % 3] : ((h % 2) ? 0xBDF7 : 0x6B4D);
            s_canvas.fillRect(bx + xx - 3, by + yy - 3, 7, 7, c);
        }
    for (int i = 0; i < 8; i++) {                                      // sparkles on the walls
        const uint32_t h = (uint32_t)i * 2654435761u + now / 200;
        s_canvas.fillCircle(80 + (int)(h % 1120), 90 + (int)((h >> 11) % 480), 3, 0xFFFF);
    }
}

// ---- the Tab5's battery, by the SETTINGS button --------------------------------
void drawBattery(uint32_t now) {
    static int32_t pct = -1; static bool chg = false; static uint32_t at = 0;
    if (!at || now - at > 10000) {
        at = now | 1;
        busTake();
        pct = M5.Power.getBatteryLevel();
        chg = M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging;
        busGive();
    }
    if (pct < 0) return;
    const int x = 690, y = 20, w = 54, h = 26;
    const uint16_t col = pct >= 50 ? 0x07E0 : pct > 20 ? 0xFFE0 : 0xF800;
    s_canvas.drawRoundRect(x, y, w, h, 4, INK);
    s_canvas.fillRect(x + w, y + 8, 4, h - 16, INK);
    const int fill = (w - 6) * (pct > 100 ? 100 : pct) / 100;
    if (fill > 0) s_canvas.fillRect(x + 3, y + 3, fill, h - 6, col);
    if (chg) {                                                         // a bolt
        const int cx = x + w / 2, cy = y + h / 2;
        s_canvas.fillTriangle(cx + 2, cy - 11, cx - 6, cy + 2, cx + 1, cy + 1, INK);
        s_canvas.fillTriangle(cx - 2, cy + 11, cx + 6, cy - 2, cx - 1, cy - 1, INK);
    }
    char t[8]; snprintf(t, sizeof t, "%ld%%", (long)pct);
    s_canvas.setFont(&fonts::FreeSansBold9pt7b);
    s_canvas.setTextDatum(middle_right);
    s_canvas.setTextColor(0x0000); s_canvas.drawString(t, x - 5, y + h / 2 + 1);
    s_canvas.setTextColor(col);    s_canvas.drawString(t, x - 6, y + h / 2);
    s_canvas.setTextDatum(top_left);
}

void drawScene(uint32_t now) {
    s_buttonN = 0;
    {   // three or more in, or the clock's five taps: party
        uint8_t in = 0;
        for (const auto& b : s_boards) if (b.live && b.present) in++;
        const bool on = in >= 3 || (int32_t)(s_partyForceUntil - now) > 0;
        if (on && !s_party) { s_partyAt = now; makePartyBackdrop(); }
        s_party = on;
    }
    s_dirtyNowN = 0;
    if (s_party && s_bgDarkOk) s_bgDark.pushSprite(&s_canvas, 0, 0);
    else                       s_bg.pushSprite(&s_canvas, 0, 0);
    touchPoll();
    drawWindowView(now);
    drawLaptop(now);
    drawNixie(now);
    drawRadar(now);
    drawSmoke(now);
    touchPoll();
    drawChalk(now);
    drawSeason(now);
    partyBack(now);
    drawVisitors(now);
    partyFront(now);
    drawGhost(now);
    touchPoll();
    drawTallyBar(now);

    // across the ceiling: the name, the state of things, the buttons
    char line[160];
    s_canvas.setFont(&fonts::FreeSansBold18pt7b);
    s_canvas.setTextColor(0x0000); s_canvas.drawString("SQUACHWATCH BASE", 18, 8);
    s_canvas.setTextColor(ACCENT); s_canvas.drawString("SQUACHWATCH BASE", 16, 6);
    s_canvas.setFont(&fonts::FreeSans9pt7b);
    s_canvas.setTextColor(DIM);
    s_canvas.drawString(TAB5_VERSION, 340, 14);
    s_canvas.setFont(&fonts::FreeSansBold18pt7b);
    uint16_t col = INK;
    if (!s_bleOk)          { col = WARN; snprintf(line, sizeof line, "BLUETOOTH FAILED TO START"); }
    else if (!s_cryptoOk)  { col = WARN; snprintf(line, sizeof line, "CRYPTO SELF-TEST FAILED"); }
    else if (!s_haveKey)   { col = WARN; snprintf(line, sizeof line, "SQUAD LOCKED -- tap SQUAD PHRASE"); }
    else {
        char g[40];
        if (!Gnss::good())         snprintf(g, sizeof g, "GPS searching");
        else if (Gnss::fresh(now)) snprintf(g, sizeof g, "GPS fix %u sats", Gnss::fix().used);
        else                       snprintf(g, sizeof g, "GPS no fix %u/%u", Gnss::sky().heard, Gnss::sky().view);
        snprintf(line, sizeof line, "squad unlocked   %lu sightings   %s   SD %s   up %lum",
                 (unsigned long)s_sightings, g, s_sdOk ? "OK" : "NONE", (unsigned long)((now - s_bootMs) / 60000));
    }
    s_canvas.setFont(&fonts::FreeSans9pt7b);
    s_canvas.setTextColor(0x0000); s_canvas.drawString(line, 19, 47);
    s_canvas.setTextColor(col);    s_canvas.drawString(line, 18, 46);
    button(770, 8, 140, 52, "SETTINGS", ACCENT, BG, BTN_SETTINGS);
    drawBattery(now);
    addButton(540, 160, 220, 70, BTN_NIXIE);                          // the clock: five quick taps
    button(920, 8, 140, 52, "MAP", INFO, INK, BTN_MAP);
    button(1070, 8, 196, 52, "SQUAD PHRASE", s_haveKey ? PANEL : WARN, INK, BTN_PHRASE);
    addButton(370, 60, 110, 260, BTN_LIGHT);                              // the pull string

    // To the panel: everything once a second (and for a party, and right
    // after a change), else only what moves -- the window and clock, the
    // bench's radar, laptop and smoke, the visitors and the ghost, where they
    // are now and where they were.
    const uint32_t tPush = millis();
    static uint32_t lastFull = 0;
    static bool wasParty = false;
    const bool full = s_party || wasParty || s_sceneFull || now - lastFull >= 1000;
    wasParty = s_party;
    if (full) {
        lastFull = now; s_sceneFull = false;
        pushCanvas();
    } else {
        noteDirty(490, 58, 300, 180);                                     // window, shelf, Nixie
        noteDirty(700, 270, 570, 150);                                    // smoke, laptop, radar
        noteDirty(1060, 470, 220, 178);                                   // the corner (pumpkin, tree)
        for (uint8_t pass = 0; pass < 2; pass++) {
            const DirtyRect* r = pass ? s_dirtyPrev : s_dirtyNow;
            const uint8_t n = pass ? s_dirtyPrevN : s_dirtyNowN;
            for (uint8_t i = 0; i < n; i++) blitRect(r[i].x, r[i].y, r[i].w, r[i].h);
        }
    }
    memcpy(s_dirtyPrev, s_dirtyNow, sizeof s_dirtyNow);
    s_dirtyPrevN = s_dirtyNowN;
    s_pushMsShown = millis() - tPush;
}

void drawHome(uint32_t now) {
    if (s_bgState == 0) loadBackdrop();
    if (s_bgState == 1) drawScene(now);
    else                drawMain(now);
}

// ---- HOME SYNC: boards hand over their logs over the home WiFi -------------------
// Asked for 2026-10-19, in place of carrying SD cards between boards. The
// Tab5 joins a network named in /squachwatch-base/wifi.txt -- name and
// password on alternate lines, as many pairs as wanted (home, a phone's
// hotspot...), tried in turn until one joins; on the card, never in the
// source -- and answers as squachbase.local. A board at home asks
//   GET  /have?board=NAME&file=squachwatch-YYYYMMDD.log  -> bytes held
// and sends what is missing, whole rows only:
//   POST /upload?board=NAME&file=...&offset=N            -> bytes held now
// An offset that is not the end of the copy here is refused with the size,
// so a lost reply costs a resend, never a doubled row. Copies live in
// /squachwatch-base/import/NAME_squachwatch-YYYYMMDD.log, which the start-up
// loader reads like any board log; the rows that arrive go on the map now.
WebServer s_web(80);
bool      s_webUp = false;
char      s_syncBoard[25] = "";            // the last board that sent rows
uint32_t  s_syncAt = 0, s_syncRows = 0;
constexpr uint8_t NETS_N = 4;
char      s_netSsid[NETS_N][33];
char      s_netPass[NETS_N][65];
uint8_t   s_netN = 0, s_netAt = 0;
uint32_t  s_netTryAt = 0;
char      s_wifiSsid[33] = "";               // the one joined, or being tried

volatile uint8_t s_lastReason = 0;         // the radio's last disconnect reason
uint8_t  s_joinState = 0;                  // WIFI screen: 0 none, 1 joining, 2 joined, 3 failed

const char* reasonText(uint8_t r) {
    switch (r) {
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_AUTH_FAIL: case WIFI_REASON_AUTH_EXPIRE: case WIFI_REASON_MIC_FAILURE:
            return "wrong password";
        case WIFI_REASON_NO_AP_FOUND:
            return "network not heard: out of range, or 5 GHz only (the Tab5 needs 2.4 GHz)";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY: case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return "its security type is not one the Tab5 can use";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "the signal dropped";
        case 0: return "no answer";
        default: return "could not join";
    }
}

void tryNet(uint8_t i, uint32_t now) {
    s_lastReason = 0;
    s_netAt = i;
    strcpy(s_wifiSsid, s_netSsid[i]);
    WiFi.disconnect(false, false);
    WiFi.begin(s_netSsid[i], s_netPass[i]);
    s_netTryAt = now ? now : 1;
    Serial.printf("[sync] trying %s\n", s_wifiSsid);
}

void wifiStart() {
    if (!s_sdOk) return;
    File f = SD_MMC.open("/squachwatch-base/wifi.txt", FILE_READ);
    if (!f) { Serial.println("[sync] no wifi.txt on the card: home sync off"); return; }
    while (f.available() && s_netN < NETS_N) {
        char* ss = s_netSsid[s_netN]; char* pw = s_netPass[s_netN];
        const size_t a = f.readBytesUntil('\n', ss, 32); ss[a] = 0;
        const size_t b = f.readBytesUntil('\n', pw, 64); pw[b] = 0;
        for (char* t : { ss, pw }) {
            size_t n = strlen(t);
            while (n && (t[n - 1] == '\r' || t[n - 1] == ' ')) t[--n] = 0;
        }
        if (ss[0]) s_netN++;
    }
    f.close();
    if (!s_netN) return;
    SD_MMC.mkdir("/squachwatch-base/import");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    tryNet(0, millis());
}

// A name a board may send: letters, digits, '-', '_' and '.', no "..".
bool cleanName(const String& v, size_t maxLen) {
    if (!v.length() || v.length() > maxLen) return false;
    for (size_t i = 0; i < v.length(); i++) {
        const char c = v[i];
        if (!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') return false;
    }
    return v.indexOf("..") < 0;
}

bool syncPath(char* out, size_t cap) {
    const String board = s_web.arg("board"), file = s_web.arg("file");
    if (!cleanName(board, 24) || !cleanName(file, 40) || !file.startsWith("squachwatch-") || !file.endsWith(".log"))
        return false;
    snprintf(out, cap, "/squachwatch-base/import/%s_%s", board.c_str(), file.c_str());
    return true;
}

uint32_t heldBytes(const char* path) {
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return 0;
    const uint32_t n = f.size();
    f.close();
    return n;
}

void webHave() {
    char path[120];
    if (!syncPath(path, sizeof path)) { s_web.send(400, "text/plain", "bad name"); return; }
    s_web.send(200, "text/plain", String(heldBytes(path)));
}

void webUpload() {
    char path[120];
    if (!syncPath(path, sizeof path)) { s_web.send(400, "text/plain", "bad name"); return; }
    const uint32_t held = heldBytes(path), offset = (uint32_t)s_web.arg("offset").toInt();
    if (offset != held) { s_web.send(409, "text/plain", String(held)); return; }
    String body = s_web.arg("plain");
    const int cut = body.lastIndexOf('\n');        // whole rows only
    if (cut < 0) { s_web.send(200, "text/plain", String(held)); return; }
    body.remove(cut + 1);
    File f = SD_MMC.open(path, FILE_APPEND);
    if (!f) { s_web.send(500, "text/plain", "card"); return; }
    f.write((const uint8_t*)body.c_str(), body.length());
    f.close();
    uint32_t rows = 0, placed = 0;                 // and onto the map, now
    char line[200];
    int from = 0;
    while (from < (int)body.length()) {
        const int nl = body.indexOf('\n', from);
        if (nl < 0) break;
        const int len = nl - from < (int)sizeof line - 1 ? nl - from : (int)sizeof line - 1;
        memcpy(line, body.c_str() + from, len); line[len] = 0;
        const uint8_t r = boardLogRow(line);
        if (r) rows++;
        if (r == 2) placed++;
        from = nl + 1;
    }
    s_web.send(200, "text/plain", String(held + body.length()));
    s_mapDirty = true;
    Serial.printf("[sync] %s: +%lu rows, %lu pins\n", path, (unsigned long)rows, (unsigned long)placed);
    if (rows) {
        char t[48], m[64];
        snprintf(t, sizeof t, "%s SYNCED", s_web.arg("board").c_str());
        snprintf(m, sizeof m, "+%lu catches, %lu new on the map", (unsigned long)rows, (unsigned long)placed);
        feedAdd(FeedKind::NOTE, ACCENT, t, m, millis());
        strncpy(s_syncBoard, s_web.arg("board").c_str(), sizeof s_syncBoard - 1);
        s_syncAt = millis();
        s_syncRows = rows;
    }
}

void webTick() {
    if (!s_netN) return;
    const uint32_t now = millis();
    // Not joined after 20 s: the next network on the list.
    const bool choosing = s_joinState == 1 || s_screen == Screen::WIFILIST || s_screen == Screen::WIFIPASS;
    if (!choosing && WiFi.status() != WL_CONNECTED && s_netN > 1 && now - s_netTryAt > 20000) {
        Serial.printf("[sync] %s did not join: reason %u (%s)\n", s_wifiSsid, s_lastReason, reasonText(s_lastReason));
        tryNet((uint8_t)((s_netAt + 1) % s_netN), now);
    }
    if (!s_webUp && WiFi.status() == WL_CONNECTED) {
        MDNS.begin("squachbase");
        MDNS.addService("squachbase", "tcp", 80);
        s_web.on("/have", HTTP_GET, webHave);
        s_web.on("/upload", HTTP_POST, webUpload);
        s_web.on("/", HTTP_GET, []() { s_web.send(200, "text/plain", "SquachWatch base station"); });
        s_web.begin();
        s_webUp = true;
        Serial.printf("[sync] on %s as %s (squachbase.local)\n", s_wifiSsid, WiFi.localIP().toString().c_str());
    }
    if (s_webUp) s_web.handleClient();
}

// ---- WIFI SETUP: pick a network and type its password on the Tab5 ------------------
// Until v30 (2026-10-21) the only way to give the Tab5 a network was to edit
// wifi.txt on the card, and when the home network would not join there was no
// telling why. Now WIFI in Settings scans, lists what the Tab5 can hear, and
// takes the password on a full keyboard; a network that joins is saved first
// in wifi.txt, and one that doesn't says why (the radio's own reason).
struct ScanNet { char ssid[33]; int8_t rssi; bool open; };
constexpr uint8_t SCAN_N = 8;
ScanNet  s_scan[SCAN_N];
uint8_t  s_scanN = 0;
bool     s_scanning = false;
char     s_wSsid[33] = "", s_wPass[65] = "";
uint8_t  s_wPage = 0;                      // keyboard: 0 abc, 1 ABC, 2 symbols
uint32_t s_joinAt = 0;
char     s_joinMsg[120] = "";

void wifiEvents() {
    WiFi.onEvent([](arduino_event_id_t e, arduino_event_info_t info) {
        if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) s_lastReason = info.wifi_sta_disconnected.reason;
    });
}

void scanStart() {
    WiFi.mode(WIFI_STA);
    if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);   // a join in progress blocks a scan
    WiFi.scanDelete();
    s_scanN = 0;
    s_scanning = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;
}

void keepScan(uint8_t slot, const char* nm, int8_t r, bool open) {
    strcpy(s_scan[slot].ssid, nm); s_scan[slot].rssi = r; s_scan[slot].open = open;
}

void scanTick() {
    if (!s_scanning) return;
    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    s_scanning = false;
    s_scanN = 0;
    for (int16_t i = 0; i < n; i++) {                      // the strongest of each name
        const String nm = WiFi.SSID(i);
        if (!nm.length() || nm.length() > 32) continue;
        const int8_t r = WiFi.RSSI(i);
        const bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        int k = -1;
        for (uint8_t j = 0; j < s_scanN; j++) if (nm == s_scan[j].ssid) k = j;
        if (k >= 0) { if (r > s_scan[k].rssi) s_scan[k].rssi = r; continue; }
        if (s_scanN < SCAN_N) { keepScan(s_scanN++, nm.c_str(), r, open); continue; }
        uint8_t w = 0;                                     // full: keep the stronger
        for (uint8_t j = 1; j < SCAN_N; j++) if (s_scan[j].rssi < s_scan[w].rssi) w = j;
        if (r > s_scan[w].rssi) keepScan(w, nm.c_str(), r, open);
    }
    for (uint8_t a = 0; a < s_scanN; a++)                   // strongest first
        for (uint8_t c = a + 1; c < s_scanN; c++)
            if (s_scan[c].rssi > s_scan[a].rssi) { ScanNet t = s_scan[a]; s_scan[a] = s_scan[c]; s_scan[c] = t; }
    WiFi.scanDelete();
    Serial.printf("[wifi] scan: %u networks\n", s_scanN);
    s_dirty = true;
}

int8_t savedNet(const char* ssid) {
    for (uint8_t i = 0; i < s_netN; i++) if (!strcmp(s_netSsid[i], ssid)) return i;
    return -1;
}

// The joined network first, then the others as they were; written to the card.
void saveNet(const char* ssid, const char* pass) {
    char ss[NETS_N][33], pw[NETS_N][65]; uint8_t n = 0;
    strcpy(ss[n], ssid); strcpy(pw[n], pass); n++;
    for (uint8_t i = 0; i < s_netN && n < NETS_N; i++)
        if (strcmp(s_netSsid[i], ssid)) { strcpy(ss[n], s_netSsid[i]); strcpy(pw[n], s_netPass[i]); n++; }
    memcpy(s_netSsid, ss, sizeof ss); memcpy(s_netPass, pw, sizeof pw);
    s_netN = n; s_netAt = 0;
    if (!s_sdOk) return;
    SD_MMC.mkdir("/squachwatch-base");
    SD_MMC.mkdir("/squachwatch-base/import");
    File f = SD_MMC.open("/squachwatch-base/wifi.txt", FILE_WRITE);
    if (!f) return;
    for (uint8_t i = 0; i < s_netN; i++) f.printf("%s\n%s\n", s_netSsid[i], s_netPass[i]);
    f.close();
}

void joinStart(uint32_t now) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(false, false);
    s_lastReason = 0;
    strcpy(s_wifiSsid, s_wSsid);
    WiFi.begin(s_wSsid, s_wPass);
    s_joinState = 1; s_joinAt = now ? now : 1;
    snprintf(s_joinMsg, sizeof s_joinMsg, "Joining %s...", s_wSsid);
    Serial.printf("[wifi] joining %s from the WIFI screen\n", s_wSsid);
}

void joinTick(uint32_t now) {
    if (s_joinState != 1) return;
    if (WiFi.status() == WL_CONNECTED) {
        saveNet(s_wSsid, s_wPass);
        s_joinState = 2;
        snprintf(s_joinMsg, sizeof s_joinMsg, "Joined %s (%s) and saved.", s_wSsid, WiFi.localIP().toString().c_str());
        feedAdd(FeedKind::NOTE, ACCENT, "WIFI JOINED", s_wSsid, now);
        Serial.printf("[wifi] %s joined, saved first in wifi.txt\n", s_wSsid);
        s_dirty = true;
        return;
    }
    const uint8_t r = s_lastReason;
    // A clear answer (a wrong password, no such network) after a few seconds;
    // otherwise give it 20 s.
    if ((r && now - s_joinAt > 6000) || now - s_joinAt > 20000) {
        s_joinState = 3;
        snprintf(s_joinMsg, sizeof s_joinMsg, "%s: %s.", s_wSsid, reasonText(r));
        Serial.printf("[wifi] %s did not join: reason %u (%s)\n", s_wSsid, r, reasonText(r));
        WiFi.disconnect(false, false);
        s_dirty = true;
    }
}

void drawWifiList() {
    s_canvas.fillSprite(BG);
    s_buttonN = 0;
    const int W = s_canvas.width();
    s_canvas.setTextColor(ACCENT, BG);
    s_canvas.setFont(&fonts::FreeSansBold24pt7b);
    s_canvas.drawString("WIFI", 28, 18);
    button(W - 16 - 170, 16, 170, 64, "BACK", PANEL, INK, BTN_W_BACK);
    button(W - 16 - 170 - 16 - 250, 16, 250, 64, s_scanning ? "SCANNING..." : "SCAN AGAIN", PANEL, s_scanning ? DIM : INK,
           s_scanning ? 0 : BTN_W_SCAN);

    s_canvas.setFont(&fonts::FreeSans12pt7b);
    char st[140];
    if (s_joinState) snprintf(st, sizeof st, "%s", s_joinMsg);
    else if (WiFi.status() == WL_CONNECTED) snprintf(st, sizeof st, "On %s (%s).", s_wifiSsid, WiFi.localIP().toString().c_str());
    else snprintf(st, sizeof st, "Not on a network.");
    s_canvas.setTextColor(s_joinState == 3 ? WARN : s_joinState == 2 ? ACCENT : INK, BG);
    s_canvas.drawString(st, 30, 96);
    s_canvas.setTextColor(DIM, BG);
    s_canvas.drawString("Tap a network to join it. Only 2.4 GHz networks show: the Tab5's radio can't use 5 GHz.", 30, 126);

    int y = 164;
    if (!s_scanN) {
        s_canvas.setTextColor(DIM, BG);
        s_canvas.drawString(s_scanning ? "Listening for networks..." : "No networks heard. Tap SCAN AGAIN.", 40, y + 14);
    }
    for (uint8_t i = 0; i < s_scanN; i++) {
        const ScanNet& n = s_scan[i];
        const bool on = WiFi.status() == WL_CONNECTED && !strcmp(n.ssid, s_wifiSsid);
        const uint16_t fill = on ? 0x1B44 : PANEL;
        s_canvas.fillRoundRect(20, y, W - 40, 58, 10, fill);
        s_canvas.setFont(&fonts::FreeSansBold12pt7b);
        s_canvas.setTextColor(INK, fill);
        s_canvas.drawString(n.ssid, 40, y + 18);
        s_canvas.setFont(&fonts::FreeSans12pt7b);
        s_canvas.setTextColor(DIM, fill);
        char tag[48];
        snprintf(tag, sizeof tag, "%s%s%s", on ? "CONNECTED   " : "", savedNet(n.ssid) >= 0 ? "saved   " : "", n.open ? "open" : "");
        s_canvas.drawString(tag, 760, y + 18);
        const uint8_t bars = n.rssi > -55 ? 4 : n.rssi > -65 ? 3 : n.rssi > -75 ? 2 : 1;   // signal
        for (uint8_t k = 0; k < 4; k++) {
            const int bh = 8 + k * 8;
            s_canvas.fillRect(W - 120 + k * 16, y + 46 - bh, 10, bh, k < bars ? ACCENT : 0x39E7);
        }
        addButton(20, y, W - 40, 58, BTN_NET + i);
        y += 64;
    }
    pushCanvas();
}

const char* const WKEYS[3][4] = {
    { "1234567890", "qwertyuiop", "asdfghjkl",  "zxcvbnm" },
    { "!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL",  "ZXCVBNM" },
    { "1234567890", "-_=+[]{}\\|", ";:'\",.<>/?", "`~!@#$%^&*" },
};

void drawWifiPass() {
    s_canvas.fillSprite(BG);
    s_buttonN = 0;
    s_canvas.setTextColor(ACCENT, BG);
    s_canvas.setFont(&fonts::FreeSansBold18pt7b);
    char t[64]; snprintf(t, sizeof t, "PASSWORD FOR %s", s_wSsid);
    s_canvas.drawString(t, 28, 16);
    s_canvas.fillRoundRect(20, 64, 1240, 66, 10, PANEL);
    s_canvas.setTextColor(INK, PANEL);
    s_canvas.setFont(&fonts::FreeMonoBold18pt7b);
    char shown[70]; snprintf(shown, sizeof shown, "%s_", s_wPass);
    s_canvas.drawString(shown, 40, 82);
    s_canvas.setFont(&fonts::FreeSans12pt7b);
    s_canvas.setTextColor(s_joinState == 3 ? WARN : DIM, BG);
    s_canvas.drawString(s_joinState == 3 ? s_joinMsg : "Capitals count. ABC for capitals, #+= for symbols.", 30, 140);

    const int kw = 112, kh = 82, gap = 10;
    for (uint8_t r = 0; r < 4; r++) {
        const char* row = WKEYS[s_wPage][r];
        const int n = strlen(row);
        const int x0 = (1280 - (n * kw + (n - 1) * gap)) / 2;
        for (int i = 0; i < n; i++) {
            char k[2] = { row[i], 0 };
            button(x0 + i * (kw + gap), 176 + r * (kh + gap), kw, kh, k, PANEL, INK, BTN_KEY + (uint8_t)row[i],
                   &fonts::FreeSansBold18pt7b);
        }
    }
    const int by = 176 + 4 * (kh + gap);
    int x = 25;
    button(x, by, 150, kh, s_wPage == 1 ? "abc" : "ABC", s_wPage == 1 ? INFO : PANEL, INK, BTN_W_SHIFT); x += 160;
    button(x, by, 150, kh, s_wPage == 2 ? "abc" : "#+=", s_wPage == 2 ? INFO : PANEL, INK, BTN_W_SYM);   x += 160;
    button(x, by, 330, kh, "SPACE",   PANEL, INK,  BTN_W_SPACE);  x += 340;
    button(x, by, 150, kh, "DEL",     PANEL, WARN, BTN_W_DEL);    x += 160;
    button(x, by, 170, kh, "CANCEL",  PANEL, INK,  BTN_W_CANCEL); x += 180;
    const bool ok = strlen(s_wPass) >= 8;                     // WPA's shortest
    button(x, by, 1255 - x, kh, "JOIN", ok ? ACCENT : PANEL, ok ? BG : DIM, ok ? BTN_W_JOIN : 0);
    pushCanvas();
}

void onWifiTap(int id, uint32_t now) {
    if (s_screen == Screen::WIFILIST) {
        if (id == BTN_W_BACK) { s_screen = Screen::SETTINGS; }
        else if (id == BTN_W_SCAN) { s_joinState = 0; scanStart(); }
        else if (id >= BTN_NET && id < BTN_NET + s_scanN) {
            const ScanNet& n = s_scan[id - BTN_NET];
            strcpy(s_wSsid, n.ssid);
            const int8_t k = savedNet(n.ssid);
            strcpy(s_wPass, k >= 0 ? s_netPass[k] : "");       // a saved one, to check or fix
            s_joinState = 0;
            if (n.open) joinStart(now);
            else { s_wPage = 0; s_screen = Screen::WIFIPASS; }
        }
        s_dirty = true;
        return;
    }
    // the password keyboard
    const size_t n = strlen(s_wPass);
    if (id >= BTN_KEY && id < BTN_KEY + 128) {
        if (n + 1 < sizeof s_wPass) { s_wPass[n] = (char)(id - BTN_KEY); s_wPass[n + 1] = 0; }
        if (s_wPage == 1) s_wPage = 0;                         // one capital, like a phone
    } else switch (id) {
        case BTN_W_SHIFT:  s_wPage = s_wPage == 1 ? 0 : 1; break;
        case BTN_W_SYM:    s_wPage = s_wPage == 2 ? 0 : 2; break;
        case BTN_W_SPACE:  if (n + 1 < sizeof s_wPass) { s_wPass[n] = ' '; s_wPass[n + 1] = 0; } break;
        case BTN_W_DEL:    if (n) s_wPass[n - 1] = 0; break;
        case BTN_W_CANCEL: s_screen = Screen::WIFILIST; break;
        case BTN_W_JOIN:   joinStart(now); s_screen = Screen::WIFILIST; break;
    }
    s_dirty = true;
}

// ---- SETTINGS ------------------------------------------------------------------
// One row each, the whole row a button: a tap moves the setting on (or does
// the row's job). Kept in Preferences; home in home.txt as before.
int8_t s_seasonPref = -1;                  // -1 AUTO, else a Season

bool setHomeHere(uint32_t now) {
    if (!Gnss::fresh(now) || !s_sdOk) return false;
    char tz[64] = "";
    File r = SD_MMC.open("/squachwatch-base/home.txt", FILE_READ);
    if (r) {
        char skip[64];
        r.readBytesUntil('\n', skip, sizeof skip - 1);
        const size_t m = r.readBytesUntil('\n', tz, sizeof tz - 1); tz[m] = 0;
        r.close();
    }
    char la[16], lo[16];
    degText(Gnss::fix().lat7, la, sizeof la);
    degText(Gnss::fix().lon7, lo, sizeof lo);
    File w = SD_MMC.open("/squachwatch-base/home.txt", FILE_WRITE);
    if (!w) return false;
    w.printf("%s,%s\n", la, lo);
    if (tz[0]) w.printf("%s\n", tz);
    w.close();
    s_homeLat7 = Gnss::fix().lat7; s_homeLon7 = Gnss::fix().lon7; s_homeOk = true;
    return true;
}

void drawSettings(uint32_t now) {
    s_canvas.fillSprite(BG);
    s_buttonN = 0;
    const int W = s_canvas.width();
    s_canvas.setTextColor(ACCENT, BG);
    s_canvas.setFont(&fonts::FreeSansBold24pt7b);
    s_canvas.drawString("SETTINGS", 28, 18);
    button(W - 16 - 170, 16, 170, 64, "BACK", PANEL, INK, BTN_SET_BACK);

    char v[96];
    const char* seasonName = s_seasonPref < 0 ? "AUTO (pumpkin in October, tree in December)"
                           : s_seasonPref == (int8_t)Season::HALLOWEEN ? "HALLOWEEN" :
                             s_seasonPref == (int8_t)Season::CHRISTMAS ? "CHRISTMAS" : "NONE";
    char wifi[96];
    if (WiFi.status() == WL_CONNECTED)        snprintf(wifi, sizeof wifi, "%s  %s  (tap to change)", s_wifiSsid, WiFi.localIP().toString().c_str());
    else if (!s_netN)                         snprintf(wifi, sizeof wifi, "not set -- tap to choose a network");
    else                                      snprintf(wifi, sizeof wifi, "trying %s...  (tap to choose a network)", s_wifiSsid);
    char home[64];
    if (s_homeOk) snprintf(home, sizeof home, "set%s", Gnss::fresh(now) ? "  -- tap to move it here" : "");
    else          snprintf(home, sizeof home, "%s", Gnss::fresh(now) ? "not set -- tap to set it here" : "not set (needs a GPS fix to set)");
    char sync[96];
    if (s_syncAt) { char ago[16]; agoText(now - s_syncAt, ago, sizeof ago);
                    snprintf(sync, sizeof sync, "%s, %s, +%lu catches", s_syncBoard, ago, (unsigned long)s_syncRows); }
    else          snprintf(sync, sizeof sync, "none yet this start");
    char about[96];
    snprintf(about, sizeof about, TAB5_VERSION "  -  frame %lu ms (copy %lu, %s)  -  %u devices, SD %s, up %lum",
             (unsigned long)s_frameMsShown, (unsigned long)s_pushMsShown, s_fbOk ? "fast" : "slow", s_devN,
             s_sdOk ? "OK" : "NONE", (unsigned long)((now - s_bootMs) / 60000));
    (void)s_sightings;

    struct Row { const char* label; const char* value; };
    const Row rows[] = {
        { "BRIGHTNESS",     BRIGHT_NAMES[s_bright] },
        { "SOUND",          SOUND_NAMES[s_sound] },
        { "VOLUME",         VOL_NAMES[s_vol] },
        { "BASEMENT LIGHT", s_lightOn ? "ON" : "OFF" },
        { "SEASON",         seasonName },
        { "CLOCK",          s_clock24 ? "24-HOUR" : "12-HOUR" },
        { "HOME",           home },
        { "WIFI",           wifi },
        { "TOUCH",          s_prefs.isKey("tcal") ? "calibrated  (tap to calibrate again)" : "as it came  (tap to calibrate)" },
        { "LAST SYNC",      sync },
        { "ABOUT",          about },
    };
    int y = 92;
    for (uint8_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        s_canvas.fillRoundRect(20, y, W - 40, 50, 10, PANEL);
        s_canvas.setFont(&fonts::FreeSansBold12pt7b);
        s_canvas.setTextColor(INK, PANEL);
        s_canvas.drawString(rows[i].label, 40, y + 14);
        s_canvas.setFont(&fonts::FreeSans12pt7b);
        s_canvas.setTextColor(i >= 9 ? DIM : ACCENT, PANEL);
        snprintf(v, sizeof v, "%s", rows[i].value);
        s_canvas.drawString(v, 330, y + 14);
        if (i < 9) addButton(20, y, W - 40, 50, BTN_SET_ROW + i);
        y += 56;
    }
    pushCanvas();
}

// TOUCH CALIBRATION: five crosses, one tap each. The panel's own numbers
// for the five give, per axis, the straight line that best maps them onto
// the crosses; it is kept in Preferences and the touch task applies it to
// every touch. A result that does not look like a touch panel (scaled more
// than 15 %, moved more than 120 px, or taps that don't line up) is refused.
const int16_t CAL_X[5] = { 100, 1180, 640, 100, 1180 };
const int16_t CAL_Y[5] = { 180, 180, 450, 650, 650 };
int16_t s_calRawX[5], s_calRawY[5];
uint8_t s_calStep = 0;
char    s_calMsg[96] = "";

void drawCalibrate() {
    M5.Display.fillScreen(BG);
    M5.Display.setTextColor(ACCENT, BG);
    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.drawString("TOUCH CALIBRATION", 28, 18);
    M5.Display.setFont(&fonts::FreeSans12pt7b);
    M5.Display.setTextColor(s_calMsg[0] ? WARN : DIM, BG);
    char line[120];
    if (s_calMsg[0]) snprintf(line, sizeof line, "%s", s_calMsg);
    else snprintf(line, sizeof line, "Tap the middle of the cross, then lift. %u of 5.", s_calStep + 1);
    M5.Display.drawString(line, 28, 70);
    for (int i = 0; i < 2; i++) {                                  // RESET and CANCEL
        const int x = i ? 1094 : 900, w = i ? 170 : 180;
        M5.Display.fillRoundRect(x, 16, w, 64, 10, PANEL);
        M5.Display.setTextColor(i ? INK : WARN, PANEL);
        M5.Display.setFont(&fonts::FreeSansBold12pt7b);
        M5.Display.setTextDatum(middle_center);
        M5.Display.drawString(i ? "CANCEL" : "RESET", x + w / 2, 48);
        M5.Display.setTextDatum(top_left);
    }
    if (s_calStep < 5) {
        const int x = CAL_X[s_calStep], y = CAL_Y[s_calStep];
        M5.Display.drawFastHLine(x - 30, y, 61, ACCENT);
        M5.Display.drawFastVLine(x, y - 30, 61, ACCENT);
        M5.Display.drawCircle(x, y, 14, ACCENT);
    }
}

void calStart() {
    s_calStep = 0; s_calMsg[0] = 0;
    s_calRaw = true;
    tapsClear();
    s_screen = Screen::CALIBRATE;
    drawCalibrate();
}

bool calSolve(const int16_t* raw, const int16_t* want, float& a, float& b, float& rms) {
    float mr = 0, mw = 0;
    for (int i = 0; i < 5; i++) { mr += raw[i]; mw += want[i]; }
    mr /= 5; mw /= 5;
    float sxy = 0, sxx = 0;
    for (int i = 0; i < 5; i++) { sxy += (raw[i] - mr) * (want[i] - mw); sxx += (raw[i] - mr) * (raw[i] - mr); }
    if (sxx < 1) return false;
    a = sxy / sxx; b = mw - a * mr;
    float e = 0;
    for (int i = 0; i < 5; i++) { const float d = a * raw[i] + b - want[i]; e += d * d; }
    rms = sqrtf(e / 5);
    return true;
}

void calTap(int x, int y, uint32_t now) {
    (void)now;
    if (y < 90 && x >= 1094) {                                   // CANCEL
        s_calRaw = false; s_screen = Screen::SETTINGS; s_dirty = true; return;
    }
    if (y < 90 && x >= 900) {                                    // RESET: the panel as it came
        s_calAx = 1; s_calBx = 0; s_calAy = 1; s_calBy = 0;
        s_prefs.remove("tcal");
        s_calRaw = false; s_screen = Screen::TOUCHTEST; s_dirty = true;
        Serial.println("[touch] calibration reset");
        return;
    }
    if (s_calStep >= 5) return;
    s_calRawX[s_calStep] = x; s_calRawY[s_calStep] = y;
    s_calMsg[0] = 0;
    M5.Speaker.tone(1318, 40);
    if (++s_calStep < 5) { drawCalibrate(); return; }

    float ax, bx, ay, by, ex, ey;
    const bool ok = calSolve(s_calRawX, CAL_X, ax, bx, ex) && calSolve(s_calRawY, CAL_Y, ay, by, ey)
                 && fabsf(ax - 1) < 0.15f && fabsf(ay - 1) < 0.15f && fabsf(bx) < 120 && fabsf(by) < 120
                 && ex < 30 && ey < 30;
    Serial.printf("[touch] calibration: x = %.3f*x%+.1f (rms %.1f), y = %.3f*y%+.1f (rms %.1f) -> %s\n",
                  ax, bx, ex, ay, by, ey, ok ? "saved" : "refused");
    if (!ok) {
        snprintf(s_calMsg, sizeof s_calMsg, "Those taps didn't line up. Once more, tapping the middle of each cross.");
        s_calStep = 0;
        drawCalibrate();
        return;
    }
    s_calAx = ax; s_calBx = bx; s_calAy = ay; s_calBy = by;
    const float c[4] = { ax, bx, ay, by };
    s_prefs.putBytes("tcal", c, sizeof c);
    s_calRaw = false;
    s_screen = Screen::TOUCHTEST;                                // straight to drawing, to see it
    s_dirty = true;
}

// TOUCH TEST: after calibrating, and from RESET -- every touch leaves a dot
// and its x,y, straight on the panel, as fast as it is read, with the five
// calibration circles to touch.
void drawTouchTest() {
    M5.Display.fillScreen(BG);
    M5.Display.setTextColor(ACCENT, BG);
    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.drawString("TOUCH TEST", 28, 18);
    M5.Display.setFont(&fonts::FreeSans12pt7b);
    M5.Display.setTextColor(DIM, BG);
    M5.Display.drawString("Draw with a finger: the dots should sit right under it, and on the circles when you touch them. BACK when done.", 28, 70);
    for (int x = 0; x < 1280; x += 160) M5.Display.drawFastVLine(x, 110, 610, 0x2104);
    for (int y = 110; y < 720; y += 120) M5.Display.drawFastHLine(0, y, 1280, 0x2104);
    for (int i = 0; i < 5; i++) M5.Display.drawCircle(CAL_X[i], CAL_Y[i], 14, INFO);
    M5.Display.fillRoundRect(1094, 16, 170, 64, 10, PANEL);
    M5.Display.setTextColor(INK, PANEL);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("BACK", 1179, 48);
    M5.Display.setTextDatum(top_left);
}

void touchTestTick() {
    struct { int x, y; } t = { s_touch.x, s_touch.y };
    if (!s_touch.down) return;
    if (t.x >= 1094 && t.y < 80) { s_screen = Screen::SETTINGS; s_dirty = true; return; }
    if (t.y < 110) return;
    M5.Display.fillCircle(t.x, t.y, 6, ACCENT);
    char c[24]; snprintf(c, sizeof c, "x %4d  y %4d   ", t.x, t.y);
    M5.Display.setFont(&fonts::FreeSans12pt7b);
    M5.Display.setTextColor(INK, BG);
    M5.Display.drawString(c, 700, 22);
}

void onSettingsTap(int id, uint32_t now) {
    if (id == BTN_SET_BACK) { s_screen = Screen::MAIN; s_dirty = true; return; }
    if (id < BTN_SET_ROW || id >= BTN_SET_ROW + 9) return;
    switch (id - BTN_SET_ROW) {
        case 0: s_bright = (s_bright + 1) % 4; s_prefs.putUChar("bright", s_bright);
                M5.Display.setBrightness(BRIGHT_LEVELS[s_bright]); break;
        case 1: s_sound = (s_sound + 1) % 3; s_prefs.putUChar("sound", s_sound); break;
        case 2: s_vol = (s_vol + 1) % 3; s_prefs.putUChar("vol", s_vol);
                M5.Speaker.setVolume(VOL_LEVELS[s_vol]); M5.Speaker.tone(1318, 80); break;
        case 3: s_lightOn = !s_lightOn; s_prefs.putBool("light", s_lightOn); loadBackdrop(); break;
        case 4: s_seasonPref = s_seasonPref < 0 ? (int8_t)Season::HALLOWEEN
                             : s_seasonPref == (int8_t)Season::HALLOWEEN ? (int8_t)Season::CHRISTMAS
                             : s_seasonPref == (int8_t)Season::CHRISTMAS ? (int8_t)Season::NONE : -1;
                s_prefs.putChar("season", s_seasonPref);
                s_seasonForce = s_seasonPref; break;
        case 5: s_clock24 = !s_clock24; s_prefs.putBool("clock24", s_clock24); break;
        case 6: if (setHomeHere(now)) feedAdd(FeedKind::NOTE, INFO, "HOME SET", "Home is where the GPS says the Tab5 is", now);
                break;
        case 8: calStart(); return;
        case 7: s_screen = Screen::WIFILIST; s_joinState = 0; scanStart(); s_dirty = true; return;
    }
    s_dirty = true;
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setRotation(1);           // landscape, 1280 x 720
    M5.Display.setBrightness(160);
    M5.Display.fillScreen(BG);
    M5.Display.setTextColor(ACCENT, BG);
    M5.Display.setFont(&fonts::FreeSansBold24pt7b);
    M5.Display.drawString("SQUACHWATCH BASE STATION -- starting", 40, 40);
    M5.Speaker.setVolume(140);
    s_bootMs = millis();
    Serial.println("[base] SquachWatch base station, step 3b (map)");

    bootStage("SCREEN BUFFER...");
    s_canvas.setPsram(true);
    s_canvas.setColorDepth(16);
    s_canvas.createSprite(M5.Display.width(), M5.Display.height());
    fastBlitBegin();

    bootStage("SQUAD KEY...");
    s_cryptoOk = MeshCrypto::selfTest();
    Serial.printf("[base] crypto self-test %s\n", s_cryptoOk ? "passed" : "FAILED");
    s_prefs.begin("sqbase", false);
    {
        char b[HIDE_N * 12 + 1];
        if (!s_prefs.isKey("hidden")) strcpy(b, "AIRTAG,GOOGLE_TAG,TILE,SAMSUNG_TAG,IBEACON,");
        else s_prefs.getString("hidden", b, sizeof b);
        for (char* t = strtok(b, ","); t && s_hiddenN < HIDE_N; t = strtok(nullptr, ",")) typeToggle(t);
    }
    if (s_cryptoOk) loadKey();
    Serial.printf("[base] squad key %s\n", s_haveKey ? "loaded" : "not set");
    s_lightOn = s_prefs.getBool("light", false);
    s_bright  = s_prefs.getUChar("bright", 2) % 4;
    s_vol     = s_prefs.getUChar("vol", 1) % 3;
    s_sound   = s_prefs.getUChar("sound", 0) % 3;
    s_clock24 = s_prefs.getBool("clock24", false);
    s_seasonPref = s_prefs.getChar("season", -1);
    M5.Display.setBrightness(BRIGHT_LEVELS[s_bright]);
    M5.Speaker.setVolume(VOL_LEVELS[s_vol]);
    clockStart();

    // Bluetooth before the SD card: the radio's link to the C6 is SDIO, on
    // the same SDMMC host the card uses, and the hosted link comes up first.
    // The other order left a black screen on the first start, 2026-10-16.
    bootStage("BLUETOOTH (ESP32-C6)...");
    BLEDevice::init("");
    s_bleOk = BLEDevice::getInitialized();
    Serial.printf("[base] BLE %s\n", s_bleOk ? "up" : "FAILED");
    if (s_bleOk) {
        BLEScan* scan = BLEDevice::getScan();
        scan->setAdvertisedDeviceCallbacks(new ScanCb(), true);
        scan->setActiveScan(false);      // the frames ride in primary adverts
        scan->setInterval(100);
        scan->setWindow(99);
        scan->start(0, nullptr, false);  // forever, in the background
    }

    bootStage("POWERING THE GROVE PORT...");
    M5.Power.setExtOutput(true);         // 5 V on the Grove port, for the GPS
    bootStage("STARTING GPS...");
    gpsStart();
    bootStage("MOUNTING SD CARD...");
    s_pins = (Pin*)ps_malloc(sizeof(Pin) * PINS_N);
    s_devs = (Dev*)ps_malloc(sizeof(Dev) * DEVS_N);
    sdStart();
    sdLoadDevices();
    loadHome();
    loadSeason();
    if (s_seasonPref >= 0) s_seasonForce = s_seasonPref;   // the screen's choice beats season.txt
    wifiEvents();
    wifiStart();
    {
        const esp_reset_reason_t why = esp_reset_reason();
        const char* w = why == ESP_RST_POWERON ? "power on" : why == ESP_RST_SW ? "restart" :
                        why == ESP_RST_PANIC ? "CRASH" : why == ESP_RST_INT_WDT ? "WATCHDOG (int)" :
                        why == ESP_RST_TASK_WDT ? "WATCHDOG (task)" : why == ESP_RST_WDT ? "WATCHDOG" :
                        why == ESP_RST_BROWNOUT ? "BROWNOUT (power dip)" : "other";
        char line[64];
        snprintf(line, sizeof line, "READY  (last start: %s)", w);
        bootStage(line);
        if (why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT ||
            why == ESP_RST_WDT || why == ESP_RST_BROWNOUT) {
            char b[48];
            snprintf(b, sizeof b, "The base station restarted: %s", w);
            feedAdd(FeedKind::NOTE, WARN, "RESTARTED", b, millis());
        }
    }
    if (s_prefs.isKey("tcal")) {
        float c[4];
        if (s_prefs.getBytes("tcal", c, sizeof c) == sizeof c) { s_calAx = c[0]; s_calBx = c[1]; s_calAy = c[2]; s_calBy = c[3]; }
        Serial.printf("[touch] calibrated: x = %.3f*x%+.1f, y = %.3f*y%+.1f\n", s_calAx, s_calBx, s_calAy, s_calBy);
    }
    touchStart();                        // last: setup is done with the I2C bus
}

void loop() {
    touchPoll();
    const uint32_t now = millis();
    Heard h;
    uint8_t budget = 40;                  // keep the screen responsive in a busy room
    while (budget-- && takeHeard(h)) handle(h, now);
    gpsTick(now);
    gpsReport(now);
    presenceTick(now);
    clockTick(now);
    webTick();
    scanTick();
    joinTick(now);
    weatherTick(now);
    usbTick();

    // Every pass, not only while a finger is down: a lift (the end of a tap,
    // a drag or a flick) is reported on the pass after the last touch.
    {
        struct { int x, y; } t = { s_touch.x, s_touch.y };
        static Screen lastScreen = Screen::MAIN;
        if (s_screen != lastScreen) { lastScreen = s_screen; tapsClear(); }   // no taps carried across
        if (s_screen == Screen::MAP) {
            tapsClear();                                  // the map reads the finger itself
            // The swipe, tracked here rather than through M5Unified's
            // flick/drag states: between its real reads it advances those on
            // a timer, and the end of a swipe kept slipping past (v3 slid the
            // map and never moved it, 2026-10-17). Where the finger went down
            // and where it last was; on the lift, more than a few pixels is a
            // pan and less is a tap on whatever button is under it. While it
            // moves, the map already drawn slides with it, the edge it leaves
            // filled with background; the new area is drawn on the lift.
            static bool down = false;
            static int  sx = 0, sy = 0, lx = 0, ly = 0;
            static uint32_t lastSeen = 0, downAt = 0;
            static int  vx0 = 0, vy0 = 0; static uint32_t vt0 = 0;   // for the flick's speed
            static uint16_t gaps = 0, samples = 0;                    // for the log
            static int  startBtn = 0;                                 // a touch that began on a top-bar button
            static bool wasPressed = false;
            const bool pressed = s_touch.down;
            if (down && pressed && !wasPressed) gaps++;               // the finger came back
            wasPressed = pressed;
            if (pressed) {
                lastSeen = now;
                s_mapTouchAt = now;
                samples++;
                if (!down) {
                    // Where the finger first landed, if a read during the
                    // last redraw caught it: the swipe counts from there
                    // (2026-10-20: a swipe begun while the map was still
                    // drawing lost its start, so it took touch, pause, drag).
                    const bool landed = s_downAt && now - s_downAt < 600;
                    const int fx = landed ? s_downX : t.x, fy = landed ? s_downY : t.y;
                    // BACK, + and -, ME: a touch that starts on one is a press.
                    // Only the top bar: the CAPTURED rows cover much of the map,
                    // and a drag that starts on them pans (v31 froze the map
                    // there, 2026-10-21); a tap on a row still toggles it.
                    startBtn = fy < 96 ? hit(fx, fy) : 0;
                    down = true; sx = fx; sy = fy; lx = t.x; ly = t.y; downAt = now; vx0 = fx; vy0 = fy; vt0 = now; gaps = 0; samples = 1;
                }
                else       { lx = t.x; ly = t.y; }
                if (now - vt0 > 80) { vx0 = lx; vy0 = ly; vt0 = now; }  // a sample ~80 ms back
                const int dx = lx - sx, dy = ly - sy;
                static uint32_t lastSlide = 0;
                if (!startBtn && (abs(dx) > 12 || abs(dy) > 12) && now - lastSlide >= 25) {
                    lastSlide = now;
                    const int W = M5.Display.width(), H = M5.Display.height();
                    M5.Display.startWrite();
                    if (s_mapBigOk && s_fbOk) {
                        // the real map, a tile round the screen, under the
                        // finger, by the fast copy; past that tile, background
                        const int M = MAP_MARGIN;
                        const int ox = -M + dx, oy = -M + dy;
                        const int x0 = ox > 0 ? ox : 0, y0 = oy > 0 ? oy : 0;
                        const int x1 = (ox + s_mapBig.width()) < W ? ox + s_mapBig.width() : W;
                        const int y1 = (oy + s_mapBig.height()) < H ? oy + s_mapBig.height() : H;
                        if (x1 > x0 && y1 > y0)
                            blitFrom(static_cast<const uint16_t*>(s_mapBig.getBuffer()), s_mapBig.width(),
                                     x0 - ox, y0 - oy, x0, y0, x1 - x0, y1 - y0);
                        if (dx > M)  M5.Display.fillRect(0, 0, dx - M, H, BG);
                        if (dx < -M) M5.Display.fillRect(W + dx + M, 0, -dx - M, H, BG);
                        if (dy > M)  M5.Display.fillRect(0, 0, W, dy - M, BG);
                        if (dy < -M) M5.Display.fillRect(0, H + dy + M, W, -dy - M, BG);
                    } else if (s_mapBigOk) {
                        const int M = MAP_MARGIN;
                        s_mapBig.pushSprite(-M + dx, -M + dy);
                        if (dx > M)  M5.Display.fillRect(0, 0, dx - M, H, BG);
                        if (dx < -M) M5.Display.fillRect(W + dx + M, 0, -dx - M, H, BG);
                        if (dy > M)  M5.Display.fillRect(0, 0, W, dy - M, BG);
                        if (dy < -M) M5.Display.fillRect(0, H + dy + M, W, -dy - M, BG);
                    } else {
                        s_canvas.pushSprite(dx, dy);
                        if (dx > 0) M5.Display.fillRect(0, 0, dx, H, BG);
                        if (dx < 0) M5.Display.fillRect(W + dx, 0, -dx, H, BG);
                        if (dy > 0) M5.Display.fillRect(0, 0, W, dy, BG);
                        if (dy < 0) M5.Display.fillRect(0, H + dy, W, -dy, BG);
                    }
                    M5.Display.endWrite();
                }
            } else if (down && now - lastSeen > 100) {
                // The panel loses a moving finger for a read or two now and
                // then; a swipe only ends after 100 ms of nothing. Before
                // (2026-10-19), every dropout ended the swipe, redrew the
                // map, and the finger's travel during the redraw was lost --
                // twenty swipes to cross a kilometre.
                down = false;
                int dx = lx - sx, dy = ly - sy;
                if (startBtn) {
                    // Began on BACK, a zoom button, a type row...: a press, however
                    // the finger slid.
                    const int id = startBtn; startBtn = 0;
                    onMapTap(id, now);
                } else if (abs(dx) > 12 || abs(dy) > 12) {
                    // A flick throws the map further: up to three times the
                    // finger's travel, by its speed over the last ~80 ms.
                    const float dt = (float)(lastSeen - vt0 > 0 ? lastSeen - vt0 : 1);
                    const float speed = sqrtf((float)((lx - vx0) * (lx - vx0) + (ly - vy0) * (ly - vy0))) / dt;  // px per ms
                    float k = 1.0f + (speed - 0.8f) * 1.2f;
                    if (k < 1) k = 1;
                    if (k > 3) k = 3;
                    Serial.printf("[map] swipe %d,%d px in %lu ms, %u reads, %u dropouts, speed %.2f px/ms, x%.1f\n",
                                  lx - sx, ly - sy, (unsigned long)(lastSeen - downAt), samples, gaps, speed, k);
                    dx = (int)(dx * k); dy = (int)(dy * k);
                    mapPan(dx, dy);
                }
                else { int id = hit(lx, ly); if (id <= 0) id = hit(sx, sy); if (id > 0) onMapTap(id, now); }
            }
        } else if (s_tapX >= 0 && s_screen == Screen::CALIBRATE) {
            const int x = s_tapX, y = s_tapY;
            s_tapX = s_tapY = -1;
            calTap(x, y, now);
        } else if (s_tapX >= 0) {
            int id = hit(s_tapX, s_tapY);
            if (id <= 0) id = hit(s_tapDX, s_tapDY);
            s_tapX = s_tapY = -1;
            if (s_screen == Screen::MAIN && id == BTN_PHRASE) {
                strncpy(s_typed, s_phrase, sizeof s_typed - 1);
                s_screen = Screen::KEYBOARD;
                s_dirty = true;
            } else if (s_screen == Screen::MAIN && id == BTN_LIGHT) {
                s_lightOn = !s_lightOn;                 // the pull string
                s_prefs.putBool("light", s_lightOn);
                M5.Speaker.tone(700, 30);
                loadBackdrop();
                s_dirty = true;
            } else if (s_screen == Screen::MAIN && id == BTN_NIXIE) {
                static uint32_t firstTap = 0; static uint8_t taps = 0;
                if (now - firstTap > 3000) { firstTap = now; taps = 0; }
                if (++taps >= 5) { taps = 0; s_partyForceUntil = now + 30000; s_dirty = true; }
            } else if (s_screen == Screen::MAIN && id == BTN_SETTINGS) {
                s_screen = Screen::SETTINGS;
                s_dirty = true;
            } else if (s_screen == Screen::SETTINGS && id > 0) {
                onSettingsTap(id, now);
            } else if ((s_screen == Screen::WIFILIST || s_screen == Screen::WIFIPASS) && id > 0) {
                onWifiTap(id, now);
            } else if (s_screen == Screen::MAIN && id == BTN_MAP) {
                mapCentreDefault(now);
                s_screen = Screen::MAP;
                s_mapDirty = true;
            } else if (s_screen == Screen::KEYBOARD && id > 0) {
                onKeyboardTap(id);
                s_dirty = true;
            }
        }
    }

    static uint32_t lastDraw = 0;
    if (s_screen == Screen::TOUCHTEST && !s_dirty) { touchTestTick(); delay(2); return; }
    if (s_screen == Screen::CALIBRATE) { delay(2); return; }
    if (s_screen == Screen::MAP) {
        // Only when something changed, plus every 10 s for the Tab5's own dot.
        // never mid-swipe: a redraw then would cost the finger's travel
        const bool touching = now - s_mapTouchAt < 300;
        if (!touching && (s_mapDirty || now - lastDraw >= 10000)) {
            lastDraw = now; s_mapDirty = false;
            const uint32_t t0 = millis();
            drawMap(now);
            Serial.printf("[map] drawn in %lu ms (zoom %d)\n", (unsigned long)(millis() - t0), s_mapZ);
        }
        else if (!s_touch.down && now - s_mapTouchAt > 1500) prefetchTick();   // not between swipes
    } else {
        // A frame of the basement can take longer than the gap between
        // frames; then the loop would only ever draw. The next one waits at
        // least as long as the last one took, so half the time, at worst,
        // goes to the radio, the GPS and the touch panel.
        static uint32_t frameMs = 0, lastLog = 0;
        const bool scene = s_screen == Screen::MAIN && s_bgState == 1;
        const uint32_t every = scene ? (frameMs * 3 / 2 > 40 ? frameMs * 3 / 2 : 40) : 1000;
        if ((s_dirty && now - lastDraw >= frameMs) || now - lastDraw >= every) {
            lastDraw = now;
            if (s_dirty) s_sceneFull = true;
            s_dirty = false;
            const uint32_t t0 = millis();
            if (s_screen == Screen::KEYBOARD)      drawKeyboard();
            else if (s_screen == Screen::TOUCHTEST) drawTouchTest();
            else if (s_screen == Screen::SETTINGS) drawSettings(now);
            else if (s_screen == Screen::WIFILIST) drawWifiList();
            else if (s_screen == Screen::WIFIPASS) drawWifiPass();
            else                                   drawHome(now);
            frameMs = millis() - t0;
            if (scene) s_frameMsShown = frameMs;
            if (scene && now - lastLog >= 30000) { lastLog = now; Serial.printf("[scene] a frame takes %lu ms\n", (unsigned long)frameMs); }
        }
    }
    delay(5);
}
