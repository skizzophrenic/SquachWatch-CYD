// SquachWatch-CYD -- HOME SYNC. See home_sync.h.
#include "home_sync.h"
#if SQW_HOME_SYNC && defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "detection.h"
#include "ota_wifi.h"
#include "squachy.h"
#include "frame_push.h"
#if defined(FREENOVE_S3) || defined(FREENOVE_S3_35)
#include <SD_MMC.h>
#define CARD SD_MMC                // as sd_log.cpp mounts it
#else
#include <SD.h>
#define CARD SD
#endif

namespace HomeSync {

namespace {

const uint32_t FIRST_MS    = 120000;       // the first look, two minutes after boot
const uint32_t EVERY_MS    = 20 * 60000;   // then every twenty minutes
const uint32_t SCAN_MS     = 15000;        // the look for the home network, at most
const uint32_t JOIN_MS     = 12000;
const size_t   CHUNK       = 8192;         // one POST
const uint8_t  DAYS        = 7;            // the newest day files only

enum class Phase : uint8_t { IDLE, SCANNING, SYNCING };
Phase    s_phase = Phase::IDLE;
bool     s_loaded = false, s_on = true, s_soon = false;
uint32_t s_lastTry = 0, s_phaseAt = 0;
int8_t   s_net = -1;                       // the saved network being used
DetectionEngine* s_engine = nullptr;
TaskHandle_t s_task = nullptr;
volatile bool s_done = false;
IPAddress s_base;                          // the base station, once found
char     s_last[96] = "not tried yet";

void load() {
    if (s_loaded) return;
    Preferences p;
    if (p.begin("homesync", true)) { s_on = p.getBool("on", true); p.end(); }
    s_loaded = true;
}

// The board's name for the base station: its nickname, letters and digits.
void boardName(char* out, size_t cap) {
    const char* n = Squachy::nickname();
    size_t o = 0;
    for (; *n && o + 1 < cap; n++) {
        const char c = *n;
        out[o++] = (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    }
    out[o] = '\0';
    if (!o) snprintf(out, cap, "BOARD");
}

// The day files on the card, newest last, at most DAYS of them.
uint8_t dayFiles(char names[][32]) {
    uint8_t n = 0;
    FramePush::busLock();
    File dir = CARD.open("/");
    if (dir) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            const char* nm = f.name();
            const char* base = strrchr(nm, '/') ? strrchr(nm, '/') + 1 : nm;
            const size_t len = strlen(base);
            if (!f.isDirectory() && !strncmp(base, "squachwatch-2", 13) && len > 4 && len < 32 &&
                !strcmp(base + len - 4, ".log")) {
                // keep the DAYS newest by name (the date is in it)
                if (n < DAYS) { strcpy(names[n++], base); }
                else {
                    uint8_t oldest = 0;
                    for (uint8_t i = 1; i < n; i++) if (strcmp(names[i], names[oldest]) < 0) oldest = i;
                    if (strcmp(base, names[oldest]) > 0) strcpy(names[oldest], base);
                }
            }
            f.close();
        }
        dir.close();
    }
    FramePush::busUnlock();
    // oldest first
    for (uint8_t i = 1; i < n; i++)
        for (uint8_t j = i; j > 0 && strcmp(names[j], names[j - 1]) < 0; j--) {
            char t[32]; strcpy(t, names[j]); strcpy(names[j], names[j - 1]); strcpy(names[j - 1], t);
        }
    return n;
}

long askHave(const char* board, const char* file) {
    HTTPClient http;
    char url[160];
    snprintf(url, sizeof url, "http://%s/have?board=%s&file=%s", s_base.toString().c_str(), board, file);
    if (!http.begin(url)) return -1;
    http.setTimeout(5000);
    const int code = http.GET();
    long v = code == 200 ? http.getString().toInt() : -1;
    http.end();
    return v;
}

// Sends file from `held` on; returns the bytes the base station holds after,
// or -1 if it stopped answering.
long sendFrom(const char* board, const char* file, long held, uint8_t* buf, uint32_t& rowsSent) {
    char path[40];
    snprintf(path, sizeof path, "/%s", file);
    for (uint8_t guard = 0; guard < 200; guard++) {
        FramePush::busLock();
        File f = CARD.open(path, FILE_READ);
        size_t got = 0;
        uint32_t size = 0;
        const bool opened = (bool)f;
        if (opened) {
            size = f.size();
            if ((uint32_t)held < size && f.seek(held)) got = f.read(buf, CHUNK);
            f.close();
        }
        FramePush::busUnlock();
        if (!opened || (uint32_t)held >= size) return held;
        size_t cut = got;                                  // whole rows only
        while (cut && buf[cut - 1] != '\n') cut--;
        if (!cut) return held;                             // a row still being written
        for (size_t i = 0; i < cut; i++) if (buf[i] == '\n') rowsSent++;
        HTTPClient http;
        char url[200];
        snprintf(url, sizeof url, "http://%s/upload?board=%s&file=%s&offset=%ld",
                 s_base.toString().c_str(), board, file, held);
        if (!http.begin(url)) return -1;
        http.setTimeout(8000);
        http.addHeader("Content-Type", "text/plain");
        const int code = http.POST(buf, cut);
        const long reply = (code == 200 || code == 409) ? http.getString().toInt() : -1;
        http.end();
        if (reply < 0) return -1;
        held = reply;                                      // 409: the base station says where it is
    }
    return held;
}

void syncTask(void*) {
    char ssid[33] = "", pass[65] = "";
    strncpy(ssid, OtaWifi::savedSsidAt((uint8_t)s_net), sizeof ssid - 1);
    OtaWifi::savedPassAt((uint8_t)s_net, pass, sizeof pass);
    WiFi.begin(ssid, pass);
    memset(pass, 0, sizeof pass);
    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < JOIN_MS) delay(100);
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(s_last, sizeof s_last, "home network %s seen, but no join", ssid);
    } else {
        char board[25];
        boardName(board, sizeof board);
        if (s_base == IPAddress()) {
            char host[32];
            snprintf(host, sizeof host, "sqw-%s", board);
            MDNS.begin(host);
            s_base = MDNS.queryHost("squachbase", 3000);
            MDNS.end();
        }
        if (s_base == IPAddress()) {
            snprintf(s_last, sizeof s_last, "home, but no base station answered (squachbase.local)");
        } else {
            uint8_t* buf = (uint8_t*)malloc(CHUNK);
            char names[DAYS][32];
            const uint8_t n = buf ? dayFiles(names) : 0;
            uint32_t rows = 0, files = 0;
            bool lost = !buf;
            for (uint8_t i = 0; i < n && !lost; i++) {
                const long held = askHave(board, names[i]);
                if (held < 0) { lost = true; break; }
                const uint32_t before = rows;
                if (sendFrom(board, names[i], held, buf, rows) < 0) lost = true;
                if (rows != before) files++;
            }
            free(buf);
            if (lost) s_base = IPAddress();                // ask mDNS again next time
            snprintf(s_last, sizeof s_last, "%s: %lu new rows from %lu files to %s",
                     lost ? "cut short" : "synced", (unsigned long)rows, (unsigned long)files,
                     s_base == IPAddress() ? "the base station" : s_base.toString().c_str());
        }
    }
    WiFi.disconnect(false, false);
    s_done = true;
    s_task = nullptr;
    vTaskDelete(nullptr);
}

void finish(uint32_t now) {
    WiFi.scanDelete();
    if (s_engine) s_engine->stopUpdateRadio();
    s_phase = Phase::IDLE;
    s_lastTry = now ? now : 1;
    Serial.printf("[sync] %s\n", s_last);
}

}  // namespace

bool enabled() { load(); return s_on; }
void setEnabled(bool on) {
    load();
    s_on = on;
    Preferences p;
    if (p.begin("homesync", false)) { p.putBool("on", on); p.end(); }
}
void syncSoon() { s_soon = true; }
void printStatus() {
    load();
    Serial.printf("[sync] home sync %s; last: %s\n", s_on ? "ON" : "OFF", s_last);
}

void tick(uint32_t now, bool idle, DetectionEngine& engine) {
    load();
    s_engine = &engine;
    switch (s_phase) {
    case Phase::IDLE: {
        if (!s_on || !idle || !engine.sd().ready() || !OtaWifi::hasSaved()) return;
        const uint32_t wait = s_lastTry ? EVERY_MS : FIRST_MS;
        if (!s_soon && now - s_lastTry < wait) return;
        s_soon = false;
        engine.startUpdateRadio();
        WiFi.mode(WIFI_STA);
        s_net = -1;
        // The same scan the update screen makes, every channel at the
        // driver's own pace: the first try, a quick look by name at 120 ms a
        // channel and six seconds at most, missed a network on the same desk
        // (2026-10-19) -- the C5 has the 5 GHz channels to visit too.
        WiFi.scanDelete();
        const int r = WiFi.scanNetworks(true, false);
        if (r == WIFI_SCAN_FAILED) { snprintf(s_last, sizeof s_last, "scan would not start"); finish(now); return; }
        s_phase = Phase::SCANNING;
        s_phaseAt = now;
        return;
    }
    case Phase::SCANNING: {
        const int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING && now - s_phaseAt < SCAN_MS) return;
        if (n == WIFI_SCAN_RUNNING) { snprintf(s_last, sizeof s_last, "scan did not finish in %lu s", (unsigned long)(SCAN_MS / 1000)); finish(now); return; }
        if (n < 0)                  { snprintf(s_last, sizeof s_last, "scan failed (%d)", n); finish(now); return; }
        // the strongest saved network heard
        int bestRssi = -127;
        for (int i = 0; i < n; i++) {
            const int8_t k = OtaWifi::savedIndexOf(WiFi.SSID(i).c_str());
            if (k >= 0 && WiFi.RSSI(i) > bestRssi) { bestRssi = WiFi.RSSI(i); s_net = k; }
        }
        if (s_net < 0) { snprintf(s_last, sizeof s_last, "not home (%d networks heard, none saved)", n); finish(now); return; }
        Serial.printf("[sync] home: %s at %d dBm, joining\n", OtaWifi::savedSsidAt((uint8_t)s_net), bestRssi);
        WiFi.scanDelete();
        s_done = false;
        if (xTaskCreatePinnedToCore(syncTask, "homesync", 8192, nullptr, 1, &s_task,
                                    portNUM_PROCESSORS > 1 ? 1 : 0) != pdPASS) {
            s_task = nullptr;
            snprintf(s_last, sizeof s_last, "no room for the sync task");
            finish(now);
            return;
        }
        s_phase = Phase::SYNCING;
        s_phaseAt = now;
        return;
    }
    case Phase::SYNCING:
        if (!s_done) return;
        finish(now);
        return;
    }
}

}  // namespace HomeSync

#else   // not built in

namespace HomeSync {
void tick(uint32_t, bool, DetectionEngine&) {}
bool enabled() { return false; }
void setEnabled(bool) {}
void syncSoon() {}
void printStatus() {}
}

#endif
