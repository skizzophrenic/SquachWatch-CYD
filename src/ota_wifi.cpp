// SquachWatch-CYD — firmware updates over WiFi. See include/ota_wifi.h.
#include "ota_wifi.h"
#if defined(CROWPANEL7)
#include "crowpanel7_backlight.h"
#include "settings.h"
#endif
#include "security.h"
#include "clock.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <string.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#ifndef OTA_WIFI_BASE
#define OTA_WIFI_BASE "http://squachwatch.com/"
#endif

using OtaCore::Fail;

namespace OtaWifi {
namespace {

const char* NVS_NS = "otawifi";
const uint32_t JOIN_TIMEOUT_MS  = 20000;
const uint32_t STALL_TIMEOUT_MS = 15000;
const uint32_t TASK_STACK       = 8192;    // measured 3.2 KB used over a whole plain-HTTP update (was 12 KB, sized for TLS)

volatile State s_state   = State::OFF;
volatile Fail  s_fail    = Fail::NONE;
volatile bool  s_install = false;
volatile bool  s_cancel  = false;
volatile bool  s_downloadStarted = false;
volatile uint32_t s_rx   = 0;
volatile uint32_t s_size = 0;

Net     s_nets[NET_MAX];
uint8_t s_netN = 0;

char    s_ssid[33]  = "";
char    s_pass[65]  = "";
bool    s_save      = false;
bool    s_savedRead = false;
bool    s_autoJoin  = false;    // begin() with networks saved: join the best one after the scan
struct Saved { char ssid[33]; SavedResult result; };
Saved   s_list[SAVED_MAX];
uint8_t s_n = 0, s_use = 0;
char    s_latest[24] = "";
uint8_t s_sig[80];
uint8_t s_sigLen = 0;

TaskHandle_t s_task       = nullptr;

void key(char* out, const char* k, uint8_t i) { snprintf(out, 4, "%s%u", k, (unsigned)i); }

// Keys: n, use, s0..s5 (names), p0..p5 (passwords), r0..r5 (how the last try
// went). A board from before the list had one network under ssid/pass; it
// becomes slot 0 the first time this runs.
void readSaved() {
    if (s_savedRead) return;
    s_savedRead = true;
    Preferences p;
    if (!p.begin(NVS_NS, false)) return;
    if (p.isKey("n")) {
        s_n   = p.getUChar("n", 0);   if (s_n > SAVED_MAX) s_n = SAVED_MAX;
        s_use = p.getUChar("use", 0); if (s_use >= s_n)    s_use = 0;
        for (uint8_t i = 0; i < s_n; i++) {
            char k[4];
            key(k, "s", i);
            strncpy(s_list[i].ssid, p.getString(k, "").c_str(), 32);
            s_list[i].ssid[32] = '\0';
            key(k, "r", i);
            s_list[i].result = (SavedResult)p.getUChar(k, 0);
        }
    } else if (p.isKey("ssid")) {
        strncpy(s_list[0].ssid, p.getString("ssid", "").c_str(), 32);
        s_list[0].ssid[32] = '\0';
        s_list[0].result = SavedResult::UNTRIED;
        if (s_list[0].ssid[0]) {
            s_n = 1; s_use = 0;
            p.putString("s0", s_list[0].ssid);
            p.putString("p0", p.getString("pass", ""));
            p.putUChar("n", 1);
            p.putUChar("use", 0);
            Serial.printf("[ota] wifi: %s moved to the network list\n", s_list[0].ssid);
        }
        p.remove("ssid");
        p.remove("pass");
    }
    p.end();
}

void writeList() {
    Preferences p;
    if (p.begin(NVS_NS, false)) {
        p.putUChar("n", s_n);
        p.putUChar("use", s_use);
        for (uint8_t i = 0; i < s_n; i++) {
            char k[4];
            key(k, "s", i); p.putString(k, s_list[i].ssid);
            key(k, "r", i); p.putUChar(k, (uint8_t)s_list[i].result);
        }
        p.end();
    }
}

bool passAt(uint8_t i, char* out, size_t cap) {
    if (!out || !cap) return false;
    out[0] = '\0';
    if (i >= s_n) return false;
    Preferences p;
    if (!p.begin(NVS_NS, true)) return false;
    char k[4];
    key(k, "p", i);
    strncpy(out, p.getString(k, "").c_str(), cap - 1);
    out[cap - 1] = '\0';
    p.end();
    return true;
}

void setResult(int8_t i, SavedResult r) {
    if (i < 0 || i >= (int8_t)s_n || s_list[i].result == r) return;
    s_list[i].result = r;
    Preferences p;
    if (p.begin(NVS_NS, false)) {
        char k[4];
        key(k, "r", (uint8_t)i);
        p.putUChar(k, (uint8_t)r);
        p.end();
    }
}

void fail(Fail f) {
    if (s_state == State::FAILED) return;
    // A cancel wins. The task can be several seconds inside an HTTP request
    // when end() is called, and whatever it decides on its way out must not
    // put update mode back on a screen the board has already left.
    if (s_cancel) return;
    s_fail  = f;
    s_state = State::FAILED;
    Serial.printf("[ota] wifi update stopped: %s\n", OtaCore::failWords(f));
}

void startScan() {
    s_netN = 0;
    WiFi.scanDelete();
    WiFi.scanNetworks(true /* async */, false /* no hidden */);
    s_state = State::SCANNING;
}

void collectScan(int n) {
    s_netN = 0;
    for (int i = 0; i < n; i++) {
        String name = WiFi.SSID(i);
        if (!name.length()) continue;
        const int8_t rssi = (int8_t)WiFi.RSSI(i);
        const bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        // One row per name: the same network from two access points is one
        // choice, shown at its strongest.
        int found = -1;
        for (uint8_t k = 0; k < s_netN; k++) if (name == s_nets[k].ssid) { found = k; break; }
        if (found >= 0) {
            if (rssi > s_nets[found].rssi) s_nets[found].rssi = rssi;
            continue;
        }
        Net cand;
        strncpy(cand.ssid, name.c_str(), sizeof cand.ssid - 1);
        cand.ssid[sizeof cand.ssid - 1] = '\0';
        cand.rssi = rssi;
        cand.open = open;
        if (s_netN < NET_MAX) {
            s_nets[s_netN++] = cand;
        } else {
            // Full: replace the weakest if this one is stronger.
            uint8_t weakest = 0;
            for (uint8_t k = 1; k < s_netN; k++) if (s_nets[k].rssi < s_nets[weakest].rssi) weakest = k;
            if (rssi > s_nets[weakest].rssi) s_nets[weakest] = cand;
        }
    }
    // Strongest first.
    for (uint8_t i = 1; i < s_netN; i++)
        for (uint8_t j = i; j > 0 && s_nets[j].rssi > s_nets[j - 1].rssi; j--) {
            Net t = s_nets[j]; s_nets[j] = s_nets[j - 1]; s_nets[j - 1] = t;
        }
    WiFi.scanDelete();
}

// Plain HTTP, on purpose, for the firmware as well as the manifest.
//
// What made this safe was already here: every image is signed, and the board
// refuses one that is not ours or that is older than the one running (see
// OtaCore::finish). TLS was buying secrecy about WHICH public file was being
// downloaded, and charging about 45 KB of flash and a 40 KB contiguous heap
// block for it -- the block that forced the screen buffer to be freed before
// an update could start, on a board whose largest block is 34 KB.
//
// The cost is that somebody on the same network can see which release is
// being fetched, and a network that blocks plain HTTP now blocks updates too.
static WiFiClient* s_plain = nullptr;
WiFiClient* client() {
    if (!s_plain) s_plain = new WiFiClient();
    return s_plain;
}

// A small file into `out`. Returns the HTTP status, or a negative number when
// nothing came back at all.
int getSmall(const String& file, uint8_t* out, size_t cap, size_t& len) {
    len = 0;
    HTTPClient http;
    if (!http.begin(*client(), String(OTA_WIFI_BASE) + file)) return -1;
    http.setTimeout(STALL_TIMEOUT_MS);
    const int code = http.GET();
    if (code == 200) {
        WiFiClient* s = http.getStreamPtr();
        const int total = http.getSize();
        uint32_t t0 = millis();
        while (len < cap && (total < 0 || (int)len < total) && millis() - t0 < STALL_TIMEOUT_MS) {
            const int a = s->available();
            if (a > 0) {
                const int r = s->read(out + len, (size_t)a < cap - len ? (size_t)a : cap - len);
                if (r > 0) { len += (size_t)r; t0 = millis(); }
            } else if (!http.connected()) {
                break;
            } else {
                delay(5);
            }
        }
    }
    http.end();
    return code;
}

// Pulls "version": "1.7.2" out of a flasher manifest without a JSON library.
bool parseVersion(const char* body, char* out, size_t cap) {
    const char* k = strstr(body, "\"version\"");
    if (!k) return false;
    const char* q = strchr(k + 9, '"');
    if (!q) return false;
    const char* e = strchr(q + 1, '"');
    if (!e || e == q + 1) return false;
    size_t n = (size_t)(e - (q + 1));
    const bool addV = q[1] != 'v';
    if (n + (addV ? 1 : 0) >= cap) return false;
    size_t o = 0;
    if (addV) out[o++] = 'v';
    memcpy(out + o, q + 1, n);
    out[o + n] = '\0';
    return true;
}

// The two fields the flasher's manifest carries for the board itself, both
// optional: "release_name", and "whats_new" as an array of short strings.
// Everything else in that file is for the browser flasher, which ignores
// what it does not know, exactly as this does.
// One JSON string, from its opening quote. Escapes are read, not copied: the
// workflow writes these with json.dump, which turns a quote into \" and
// anything past ASCII into \uXXXX -- taken raw, a quote ended the line early
// and scrambled every line after it. The board's fonts are ASCII, so an
// escaped character past it comes out as '?'. Copies what fits into `out`
// and returns the character after the closing quote, or nullptr.
static const char* jsonString(const char* q, char* out, size_t cap) {
    if (!q || *q != '"' || !cap) return nullptr;
    size_t o = 0;
    for (const char* p = q + 1; *p; p++) {
        char c = *p;
        if (c == '"') { out[o] = '\0'; return p + 1; }
        if (c == '\\') {
            const char e = *++p;
            if (!e) break;
            if (e == 'u') {
                int v = 0, d = 0;
                for (; d < 4 && isxdigit((unsigned char)p[1]); d++, p++)
                    v = v * 16 + (isdigit((unsigned char)p[1]) ? p[1] - '0' : (tolower((unsigned char)p[1]) - 'a' + 10));
                c = (d == 4 && v >= 0x20 && v < 0x7F) ? (char)v : '?';
            } else {
                c = (e == 'n' || e == 't' || e == 'r') ? ' ' : e;   // \" \\ \/ are the character itself
            }
        }
        if (o + 1 < cap) out[o++] = c;
    }
    out[o] = '\0';
    return nullptr;
}

void parseRelease(const char* body) {
    char name[20] = "";
    if (const char* k = strstr(body, "\"release_name\"")) {
        const char* p = k + 14;
        while (*p == ' ' || *p == ':') p++;
        jsonString(p, name, sizeof name);
    }
    char lines[OtaCore::NEWS_MAX][40];
    const char* ptr[OtaCore::NEWS_MAX];
    uint8_t n = 0;
    if (const char* a = strstr(body, "\"whats_new\"")) {
        const char* p = strchr(a + 11, '[');
        // String by string, not "everything before the first ]": a bracket
        // inside a line of the notes is part of the line.
        while (p && n < OtaCore::NEWS_MAX) {
            p++;
            while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
            if (*p != '"') break;
            p = jsonString(p, lines[n], sizeof lines[0]);
            ptr[n] = lines[n];
            n++;
            if (p) p--;                   // the loop steps over it
        }
    }
    if (name[0] || n) OtaCore::noteRelease(name, ptr, n);
}

bool join() {
    s_state = State::CONNECTING;
    Serial.printf("[ota] joining %s\n", s_ssid);
    WiFi.begin(s_ssid, s_pass[0] ? s_pass : nullptr);
    const uint32_t t0 = millis();
    wl_status_t st = WiFi.status();
    while (st != WL_CONNECTED && millis() - t0 < JOIN_TIMEOUT_MS && !s_cancel) {
        if (st == WL_CONNECT_FAILED) break;
        delay(100);
        st = WiFi.status();
    }
    if (s_cancel) return false;
    if (st != WL_CONNECTED) {
        setResult(savedIndexOf(s_ssid), st == WL_NO_SSID_AVAIL ? SavedResult::NOT_FOUND : SavedResult::BAD_PASSWORD);
        fail(st == WL_NO_SSID_AVAIL ? Fail::WIFI_NOT_FOUND : Fail::WIFI_PASSWORD);
        return false;
    }
    Serial.printf("[ota] joined %s as %s\n", s_ssid, WiFi.localIP().toString().c_str());
    // The clock rides along: one NTP round trip while the radio is up anyway.
    Clock::syncWait(1500);
    if (s_save && !saveNetwork(s_ssid, s_pass))
        Serial.println("[ota] wifi: the network list is full, not saved");
    setResult(savedIndexOf(s_ssid), SavedResult::JOINED);
    return true;
}

bool check() {
    s_state = State::CHECKING;
    Serial.printf("[ota] checking %s, largest block %lu\n", OTA_WIFI_BASE,
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    uint8_t body[1024];
    size_t  len = 0;
    int code = getSmall(String("manifest-") + OtaCore::buildName() + ".json", body, sizeof body - 1, len);
    if (code != 200) {
        Serial.printf("[ota] manifest: HTTP %d\n", code);
        fail(Fail::NO_SITE);
        return false;
    }
    body[len] = '\0';
    if (!parseVersion((const char*)body, s_latest, sizeof s_latest)) { fail(Fail::NO_SITE); return false; }

    code = getSmall(String(OtaCore::buildName()) + "-firmware.sig", s_sig, sizeof s_sig, len);
    if (code == 404) { fail(Fail::NOT_SIGNED); return false; }
    if (code != 200 || len < 8 || len >= sizeof s_sig) {
        Serial.printf("[ota] signature: HTTP %d, %u bytes\n", code, (unsigned)len);
        fail(code == 200 ? Fail::NOT_SIGNED : Fail::NO_SITE);
        return false;
    }
    s_sigLen = (uint8_t)len;
    Serial.printf("[ota] latest is %s, running %s\n", s_latest, FIRMWARE_VERSION);
    return true;
}

void download() {
    s_state = State::DOWNLOADING;
    s_downloadStarted = true;
    s_rx = 0;
    HTTPClient http;
    if (!http.begin(*client(), String(OTA_WIFI_BASE) + OtaCore::buildName() + "-firmware.bin")) {
        fail(Fail::NO_SITE);
        return;
    }
    http.setTimeout(STALL_TIMEOUT_MS);
        Serial.printf("[ota] heap before GET: %lu free, largest %lu\n", (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    const int code = http.GET();
    const int size = http.getSize();
        Serial.printf("[ota] heap after GET: %lu free, largest %lu\n", (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (code != 200 || size <= 0) {
        Serial.printf("[ota] firmware: HTTP %d, size %d\n", code, size);
        http.end();
        fail(Fail::NO_SITE);
        return;
    }
    Fail f = OtaCore::begin((uint32_t)size, s_sig, s_sigLen);
    if (f != Fail::NONE) { http.end(); fail(f); return; }
    s_size = (uint32_t)size;
        Serial.printf("[ota] heap after ota begin: %lu free, largest %lu\n", (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    uint8_t* buf = (uint8_t*)malloc(4096);
    if (!buf) {
        Serial.printf("[ota] no room for the download buffer: heap %lu largest %lu\n",
                      (unsigned long)ESP.getFreeHeap(),
                      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        http.end(); OtaCore::abort(); fail(Fail::LOW_MEMORY); return;
    }
    WiFiClient* s = http.getStreamPtr();
    uint32_t last = millis();
    // Every 256 KB, and at the end: how far, how fast, and how much room is
    // left. "The download stopped" on its own said nothing about which of
    // the two exits below took it, or how far it had got.
    const uint32_t t0 = millis();
    uint32_t nextMark = 262144;
    const char* why = "complete";
    while (s_rx < s_size && !s_cancel && s_state == State::DOWNLOADING) {
        const int a = s->available();
        if (a <= 0) {
            if (!http.connected())                 { why = "connection closed"; break; }
            if (millis() - last > STALL_TIMEOUT_MS) { why = "no data for 15 s";  break; }
            delay(5);
            continue;
        }
        size_t want = (size_t)a;
        if (want > 4096) want = 4096;
        if (want > s_size - s_rx) want = s_size - s_rx;
        const int r = s->read(buf, want);
        if (r <= 0) continue;
        if (!OtaCore::write(buf, (size_t)r)) {
            fail(s_rx == 0 ? Fail::NOT_FIRMWARE : Fail::WRITE_ERROR);
            break;
        }
        s_rx += (uint32_t)r;
        last = millis();
        if (s_rx >= nextMark) {
            nextMark += 262144;
            const uint32_t el = millis() - t0;
            Serial.printf("[ota] rx %lu/%lu  %lu KB/s  heap %lu largest %lu\n", (unsigned long)s_rx,
                          (unsigned long)s_size, (unsigned long)(el ? s_rx / el : 0),
                          (unsigned long)ESP.getFreeHeap(),
                          (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        }
    }
    if (s_rx < s_size) {
        const uint32_t el = millis() - t0;
        Serial.printf("[ota] download ended: %s at %lu/%lu after %lu ms (last data %lu ms ago), "
                      "connected %d, heap %lu largest %lu\n", why, (unsigned long)s_rx, (unsigned long)s_size,
                      (unsigned long)el, (unsigned long)(millis() - last), http.connected() ? 1 : 0,
                      (unsigned long)ESP.getFreeHeap(),
                      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
    free(buf);
    http.end();

    if (s_cancel || s_state == State::FAILED) { OtaCore::abort(); return; }
    if (s_rx != s_size) { OtaCore::abort(); fail(Fail::TIMEOUT); return; }

    s_state = State::VERIFYING;
    f = OtaCore::finish();
    if (f != Fail::NONE) { fail(f); return; }
    s_state = State::DONE;
    OtaCore::restartSoon(4000);
}

void run(void*) {
    WiFi.scanDelete();
    Clock::syncStop();
    WiFi.disconnect(false, false);
    delay(100);
    // Bench diagnosis: a drop mid-update names its reason (200 beacon
    // timeout, 2 auth expired, 201 no AP found, 15 handshake).
    const wifi_event_id_t dropEv = WiFi.onEvent(
        [](arduino_event_id_t, arduino_event_info_t info) {
            Serial.printf("[ota] WiFi dropped mid-update, reason %u\n", (unsigned)info.wifi_sta_disconnected.reason);
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    if (join()) {
        // The password has done its job; it only lives on in NVS if saved.
        memset(s_pass, 0, sizeof s_pass);
        if (check()) {
            s_state = State::READY;
            while (!s_install && !s_cancel) delay(50);
            if (!s_cancel) download();
        }
    }
    memset(s_pass, 0, sizeof s_pass);
    // Same reason as fail()'s: a cancel that arrived mid-request leaves this
    // task holding a state nobody is watching any more. DONE is left alone --
    // that install happened, and the board is already on its way to a restart.
    if (s_cancel && s_state != State::DONE) s_state = State::OFF;
    // Hand the radio back the way it was found. Detection sniffs WiFi by
    // hopping channels, which an association would fight over, and the board
    // no longer restarts when update mode ends -- so the association goes
    // rather than the board.
    WiFi.removeEvent(dropEv);
    WiFi.disconnect(false, false);
    Serial.printf("[ota] update task stack: %u of %u bytes never used\n",
                  (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)), (unsigned)TASK_STACK);
    s_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

int8_t bestSavedInScan();   // below, with the rest of the list

bool begin() {
    if (s_state != State::OFF) return true;
    if (Security::locked() || !OtaCore::available()) return false;
    // A cancelled attempt's task can still be unwinding an HTTP request it is
    // waiting on. One at a time: it clears s_task on its way out.
    if (s_task) return false;
    readSaved();
    s_fail = Fail::NONE;
    s_cancel = s_install = s_downloadStarted = false;
    s_rx = s_size = 0;
    WiFi.mode(WIFI_STA);
#if defined(CROWPANEL7)
    // The join and the download at full backlight browned this board out
    // into a POWER ON reset (black box, 2026-09-23): a 7" backlight plus
    // WiFi at 20 dBm plus BLE is more than a USB port reliably gives. The
    // boot check already dims before its join (main.cpp); this is the flow
    // the screen starts. 8.5 dBm is plenty for a network in the same flat.
    CrowBL::set(24);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
    // A saved network is used straight away. The list only appears when there
    // is nothing saved, or through TRY AGAIN when the saved one cannot be
    // joined -- which is also how somebody who has moved picks a new one.
    // With networks saved, the scan comes first and the best of them is
    // joined from tick(); the list only appears when none is in range.
    s_autoJoin = s_n > 0;
    startScan();
    Serial.println("[ota] wifi update mode: scanning");
    return true;
}

bool end() {
    if (s_state == State::OFF) return false;
    s_cancel = true;
    WiFi.scanDelete();
    s_state = State::OFF;
#if defined(CROWPANEL7)
    CrowBL::set(Settings::brightness());   // the dim above, undone
#endif
    Serial.println("[ota] wifi update mode off");
    return false;
}

void tick(uint32_t) {
    if (s_state != State::SCANNING) return;
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    if (n > 0) collectScan(n);
    else       s_netN = 0;
    s_state = State::PICK;
    if (s_autoJoin) {
        s_autoJoin = false;
        const int8_t k = bestSavedInScan();
        if (k >= 0) {
            Serial.printf("[ota] wifi update mode: using saved network %s\n", s_list[k].ssid);
            connectSavedAt((uint8_t)k);
        } else {
            Serial.println("[ota] wifi update mode: no saved network in range, showing the list");
        }
    }
}

void rescan() {
    if (s_state == State::PICK || s_state == State::FAILED) startScan();
}

State      state()    { return s_state; }
uint8_t    netCount() { return s_netN; }
const Net* net(uint8_t i) { return i < s_netN ? &s_nets[i] : nullptr; }

bool hasSaved() { readSaved(); return s_n > 0; }
bool savedPassAt(uint8_t i, char* out, size_t cap) { readSaved(); return i < s_n && passAt(i, out, cap); }

void forget() {
    Preferences p;
    if (p.begin(NVS_NS, false)) { p.clear(); p.end(); }
    s_n = 0; s_use = 0;
    s_savedRead = true;
}

uint8_t     savedCount()          { readSaved(); return s_n; }
const char* savedSsidAt(uint8_t i){ readSaved(); return i < s_n ? s_list[i].ssid : ""; }
uint8_t     savedUse()            { readSaved(); return s_use; }
SavedResult savedResult(uint8_t i){ readSaved(); return i < s_n ? s_list[i].result : SavedResult::UNTRIED; }
int8_t savedIndexOf(const char* ssid) {
    readSaved();
    if (!ssid || !ssid[0]) return -1;
    for (uint8_t i = 0; i < s_n; i++) if (!strcmp(s_list[i].ssid, ssid)) return (int8_t)i;
    return -1;
}

bool saveNetwork(const char* ssid, const char* pass) {
    readSaved();
    if (!ssid || !ssid[0]) return false;
    int8_t k = savedIndexOf(ssid);
    if (k < 0) {
        if (s_n >= SAVED_MAX) return false;
        k = (int8_t)s_n++;
        strncpy(s_list[k].ssid, ssid, 32);
        s_list[k].ssid[32] = '\0';
        if (s_n == 1) s_use = 0;
    }
    s_list[k].result = SavedResult::UNTRIED;
    Preferences p;
    if (p.begin(NVS_NS, false)) {
        char key_[4];
        key(key_, "p", (uint8_t)k);
        p.putString(key_, pass ? pass : "");
        p.end();
    }
    writeList();
    Serial.printf("[ota] wifi: saved %s (%u of %u)\n", ssid, (unsigned)s_n, (unsigned)SAVED_MAX);
    return true;
}

void removeSaved(uint8_t i) {
    readSaved();
    if (i >= s_n) return;
    // Passwords move down with their names; the last slot's is dropped.
    Preferences p;
    const bool open = p.begin(NVS_NS, false);
    for (uint8_t j = i; j + 1 < s_n; j++) {
        s_list[j] = s_list[j + 1];
        if (open) {
            char from[4], to[4];
            key(from, "p", (uint8_t)(j + 1));
            key(to,   "p", j);
            p.putString(to, p.getString(from, ""));
        }
    }
    if (open) {
        char k[4];
        key(k, "p", (uint8_t)(s_n - 1)); p.remove(k);
        key(k, "s", (uint8_t)(s_n - 1)); p.remove(k);
        key(k, "r", (uint8_t)(s_n - 1)); p.remove(k);
        p.end();
    }
    s_n--;
    if (s_use == i) s_use = 0;
    else if (s_use > i) s_use--;
    writeList();
}

void printSaved() {
    readSaved();
    static const char* const R[] = { "not tried", "joined", "wrong password", "not found" };
    Serial.printf("[wifi] %u saved, USE is %u\n", (unsigned)s_n, (unsigned)s_use);
    for (uint8_t i = 0; i < s_n; i++)
        Serial.printf("[wifi]   %u: %s -- %s\n", (unsigned)i, s_list[i].ssid, R[(uint8_t)s_list[i].result & 3]);
}

void useSaved(uint8_t i) {
    readSaved();
    if (i >= s_n) return;
    s_use = i;
    writeList();
}

// The best saved network in the last scan: USE when it is there, else the
// strongest. -1 when none of them is.
int8_t bestSavedInScan() {
    readSaved();
    int8_t best = -1, bestRssi = -127;
    for (uint8_t j = 0; j < s_netN; j++) {
        const int8_t k = savedIndexOf(s_nets[j].ssid);
        if (k < 0) continue;
        if (k == (int8_t)s_use) return k;
        if (s_nets[j].rssi > bestRssi) { bestRssi = s_nets[j].rssi; best = k; }
    }
    return best;
}

void connect(const char* ssid, const char* pass, bool save) {
    if (s_task || (s_state != State::PICK && s_state != State::FAILED)) return;
    strncpy(s_ssid, ssid ? ssid : "", sizeof s_ssid - 1);
    s_ssid[sizeof s_ssid - 1] = '\0';
    strncpy(s_pass, pass ? pass : "", sizeof s_pass - 1);
    s_pass[sizeof s_pass - 1] = '\0';
    s_save    = save;
    s_fail    = Fail::NONE;
    s_cancel  = s_install = s_downloadStarted = false;
    s_state   = State::CONNECTING;
    if (xTaskCreatePinnedToCore(run, "otawifi", TASK_STACK, nullptr, 1, &s_task, 1) != pdPASS) {
        s_task = nullptr;
        memset(s_pass, 0, sizeof s_pass);
        Serial.printf("[ota] no room for the update task: %u wanted, heap %lu largest %lu\n", (unsigned)TASK_STACK,
                      (unsigned long)ESP.getFreeHeap(),
                      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        fail(Fail::LOW_MEMORY);
    }
}

bool bootCheck(uint32_t budgetMs) {
    readSaved();
    if (!s_n) return false;
    const uint32_t t0 = millis();
    WiFi.mode(WIFI_STA);
#if defined(CROWPANEL7)
    WiFi.setTxPower(WIFI_POWER_8_5dBm);   // see begin(); the backlight is already down here
#endif
    // More than one network saved: a quick scan says which are here, and the
    // one marked USE wins when it is, else the strongest of the rest. One
    // network: join it blind, as before, and keep the scan's two seconds.
    uint8_t pick = s_use;
    if (s_n > 1) {
        // PASSIVE, 130 ms a channel: listen for beacons rather than ask.
        // The active scan asked for 110 ms a channel and took 5.6 seconds on
        // the soak board, twice in a row, so it was not a warm-up cost; it
        // was most of a boot check that ran to thirteen. A passive scan
        // keeps to the time it is given -- 1.5 s at 110 ms, measured -- and
        // an access point beacons about every 102 ms, so 130 ms on each
        // channel hears every network in range. Hidden networks were never
        // shown by this scan anyway.
        const uint32_t ts = millis();
        const int found = WiFi.scanNetworks(false, false, true, 130);
        int8_t best = -1, bestRssi = -127;
        bool   seen[SAVED_MAX] = { false, false, false, false, false, false };
        uint8_t inRange = 0;
        for (int j = 0; j < found; j++) {
            const int8_t k = savedIndexOf(WiFi.SSID(j).c_str());
            if (k < 0) continue;
            if (!seen[k]) inRange++;
            seen[k] = true;
            if (k == (int8_t)s_use) { best = k; bestRssi = 127; }
            else if (WiFi.RSSI(j) > bestRssi) { bestRssi = (int8_t)WiFi.RSSI(j); best = k; }
        }
        WiFi.scanDelete();
        Serial.printf("[ota] boot check: scan %lu ms, %d network(s), %u of %u saved in range\n",
                      (unsigned long)(millis() - ts), found, (unsigned)inRange, (unsigned)s_n);
        for (uint8_t i = 0; i < s_n; i++) if (!seen[i]) setResult((int8_t)i, SavedResult::NOT_FOUND);
        if (best < 0) {
            // The radio off on the way out, as the end of this does: the
            // station mode switched on above held the WiFi driver's heap
            // through the whole of the boot that followed.
            WiFi.mode(WIFI_OFF);
            return false;
        }
        pick = (uint8_t)best;
    }
    char pass[65] = "";
    passAt(pick, pass, sizeof pass);
    const char* ssid = s_list[pick].ssid;
    Serial.printf("[ota] boot check: joining %s (heap %lu, largest %lu)\n", ssid,
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    // Why the access point let go, if it did. The status alone says only
    // "disconnected"; the driver's reason code says whether that was the
    // password (15, a handshake that never finished), the signal (2, auth
    // expired), or the network vanishing (201).
    static volatile uint8_t s_dropReason;
    s_dropReason = 0;
    const wifi_event_id_t dropEv = WiFi.onEvent(
        [](arduino_event_id_t, arduino_event_info_t info) {
            s_dropReason = info.wifi_sta_disconnected.reason;
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    // The budget starts HERE, after the scan, not at t0. It used to be one
    // clock for the lot, and on the soak board the scan alone took 5.7 of
    // the 6 seconds the join was allowed: the join got 0.3 s, gave up
    // before the router had answered, and a network three access points
    // strong went unjoined boot after boot. A board with one saved network
    // skips the scan, which is why it never showed there.
    const uint32_t tj = millis();
    WiFi.begin(ssid, pass[0] ? pass : nullptr);
    wl_status_t st = WiFi.status();
    // Two thirds of the budget for the join, the rest for the fetch.
    while (st != WL_CONNECTED && millis() - tj < budgetMs * 2 / 3) {
        if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) break;
        delay(50);
        st = WiFi.status();
    }
    WiFi.removeEvent(dropEv);
    if (st != WL_CONNECTED)
        Serial.printf("[ota] boot check: join gave up after %lu ms, status %d, last drop reason %u\n",
                      (unsigned long)(millis() - tj), (int)st, (unsigned)s_dropReason);
    memset(pass, 0, sizeof pass);
    // How it went, for the WIFI NETWORKS screen. A join that merely ran out
    // of time says nothing about the password, so it changes nothing.
    if (st == WL_CONNECTED)           setResult((int8_t)pick, SavedResult::JOINED);
    else if (st == WL_NO_SSID_AVAIL)  setResult((int8_t)pick, SavedResult::NOT_FOUND);
    else if (st == WL_CONNECT_FAILED) setResult((int8_t)pick, SavedResult::BAD_PASSWORD);
    bool found = false;
    if (st == WL_CONNECTED) {
        Serial.printf("[ota] boot check: joined in %lu ms\n", (unsigned long)(millis() - t0));
        uint8_t body[1024];
        size_t  len = 0;
        // From the join, like the join's own wait, and never below a second:
        // this was an unsigned subtraction, and a join that ran past the
        // whole budget would have wrapped it round to about fifty days.
        const uint32_t usedJ = millis() - tj;
        const uint32_t left  = usedJ + 1000 < budgetMs ? budgetMs - usedJ : 1000;
        // Plain HTTP, on purpose. A TLS handshake wants 40 KB in one piece
        // and five to ten seconds, and one that timed out left a dead
        // connection in the middle of the heap that cost the frame buffer
        // its block -- measured, twice. The site answers the manifest over
        // plain HTTP, and nothing rides on this answer but a notice: the
        // install itself goes over HTTPS and checks the signature.
        const String base = OTA_WIFI_BASE;
        WiFiClient plain;
        HTTPClient http;
        if (http.begin(plain, base + "manifest-" + OtaCore::buildName() + ".json")) {
            http.setConnectTimeout((int32_t)left);
            http.setTimeout((uint16_t)(left > 60000 ? 60000 : left));
            const int code = http.GET();
            if (code == 200) {
                WiFiClient* s = http.getStreamPtr();
                const int total = http.getSize();     // the server keeps the connection open, so the
                const uint32_t t1 = millis();         // content length is what says "that is all of it"
                while (len < sizeof body - 1 && (total < 0 || (int)len < total) && millis() - t1 < left) {
                    const int a = s->available();
                    if (a > 0) { const int r = s->read(body + len, (size_t)a < sizeof body - 1 - len ? (size_t)a : sizeof body - 1 - len); if (r > 0) len += (size_t)r; }
                    else if (!http.connected()) break;
                    else delay(5);
                }
                Serial.printf("[ota] boot check: manifest %u bytes in %lu ms\n", (unsigned)len, (unsigned long)(millis() - t1));
                body[len] = '\0';
                char latest[16];
                if (parseVersion((const char*)body, latest, sizeof latest)) {
                    Serial.printf("[ota] boot check: site has %s, running %s\n", latest, FIRMWARE_VERSION);
                    OtaCore::noteAvailable(latest, "");
                    // Only once it is known to be newer: noteAvailable drops
                    // anything that is not, and notes without an update are
                    // notes about the version already running.
                    if (OtaCore::availableVersion()[0]) parseRelease((const char*)body);
                    found = true;
                }
            } else {
                Serial.printf("[ota] boot check: manifest HTTP %d\n", code);
            }
            http.end();
        }
        // The clock, asked AFTER the manifest rather than alongside it. The
        // two used to overlap to save a moment, and on two boots in three
        // the manifest's GET then took 2.8-3.0 s instead of 70 ms: two name
        // lookups at once, one of them waiting out a retry. Asked one after
        // the other, the clock costs its own couple of hundred ms and the
        // GET costs what it should.
        Clock::syncStart();
    } else {
        Serial.printf("[ota] boot check: no join (%d) in %lu ms\n", (int)st, (unsigned long)(millis() - t0));
    }
    if (st == WL_CONNECTED) {
        // A time server answers in well under a second; this is the cap on
        // a bad day, not the usual cost. Skipped once the clock is fresh.
        const uint32_t t2 = millis();
        const uint32_t used = t2 - tj;
        uint32_t left = used < budgetMs ? budgetMs - used : 0;
        if (left > 2500) left = 2500;
        const bool ok = Clock::syncWait(left);
        char clk[24];
        Clock::formatClock(clk, sizeof clk);
        Serial.printf("[clock] %s in %lu ms (%s)\n", ok ? "set" : "no answer",
                      (unsigned long)(millis() - t2), clk);
    }
    Clock::syncStop();
    // Everything back the way it was: the driver torn down, so Bluetooth
    // starts into the heap it always had.
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    Serial.printf("[ota] boot check done in %lu ms (heap %lu, largest %lu)\n", (unsigned long)(millis() - t0),
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return found;
}

void connectSavedAt(uint8_t i) {
    readSaved();
    if (i >= s_n) return;
    char pass[65] = "";
    passAt(i, pass, sizeof pass);
    connect(s_list[i].ssid, pass, false);
    memset(pass, 0, sizeof pass);
}

void connectSaved() {
    readSaved();
    if (!s_n) return;
    const int8_t k = bestSavedInScan();
    connectSavedAt(k >= 0 ? (uint8_t)k : s_use);
}

const char* network()       { return s_ssid; }
const char* latestVersion() { return s_latest; }
bool        upToDate()      { return s_latest[0] && strcmp(s_latest, FIRMWARE_VERSION) == 0; }
void        install()       { if (s_state == State::READY) s_install = true; }

bool canTryAgain() { return s_state == State::FAILED && !s_downloadStarted && !s_task; }
void tryAgain() {
    if (!canTryAgain()) return;
    Clock::syncStop();
    WiFi.disconnect(false, false);
    startScan();
}

uint8_t     percent()       { return s_size ? (uint8_t)((uint64_t)s_rx * 100 / s_size) : 0; }
uint32_t    bytesReceived() { return s_rx; }
uint32_t    bytesExpected() { return s_size; }
const char* failureText()   { return OtaCore::failWords(s_fail); }

}  // namespace OtaWifi
