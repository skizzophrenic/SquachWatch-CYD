// SquachWatch-CYD — SHARE LOGS over WiFi. See include/log_share.h.
#include "log_share.h"
#if SQW_LOG_SHARE
#include "ota_wifi.h"
#include "security.h"
#include "blackbox.h"
#include "clock.h"
#include "frame_push.h"   // busLock(): the card can share the panel's bus
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <esp_random.h>
#include <string.h>
#include <stdio.h>

namespace LogShare {

namespace {

const uint32_t JOIN_MS     = 15000;   // per saved network
const uint32_t JOIN_GRACE_MS = 3000;  // a failure status before this is stale
const uint8_t  BAD_AUTH_MAX = 5;
const uint32_t FAILED_MS   = 60000;   // a failure stays on screen this long, then leaves by itself
const char*    USER        = "squach";

State     s_state = State::OFF;
WebServer* s_srv  = nullptr;
bool      s_sd    = false;
const char* s_why  = "";
const char* s_fail = "";
char      s_ssid[33] = "";
char      s_addr[16] = "";
char      s_code[7]  = "";
uint8_t   s_try[OtaWifi::SAVED_MAX];   // the saved networks, in the order they are tried
uint8_t   s_tryN = 0, s_tryAt = 0;
uint32_t  s_joinAt = 0, s_startAt = 0, s_lastReq = 0, s_failAt = 0;
uint16_t  s_requests = 0;
uint8_t   s_badAuth = 0;
bool      s_stopReq = false;
bool      s_timedOut = false;

void joinNext(uint32_t now) {
    while (s_tryAt < s_tryN) {
        const uint8_t i = s_try[s_tryAt++];
        char pass[65] = "";
        if (!OtaWifi::savedPassAt(i, pass, sizeof pass)) continue;
        snprintf(s_ssid, sizeof s_ssid, "%s", OtaWifi::savedSsidAt(i));
        Serial.printf("[share] joining %s\n", s_ssid);
        WiFi.disconnect(false, false);
        WiFi.begin(s_ssid, pass[0] ? pass : nullptr);
        memset(pass, 0, sizeof pass);
        s_joinAt = now;
        s_state  = State::JOINING;
        return;
    }
    WiFi.disconnect(false, false);
    s_fail   = "Couldn't join a saved network";
    s_state  = State::FAILED;
    s_failAt = now;
    Serial.println("[share] no saved network joined");
}

// Every handler starts here. A request with no credentials is the browser
// asking what it needs, not a guess, so only a wrong code counts.
bool allowed() {
    if (s_srv->authenticate(USER, s_code)) {
        s_lastReq = millis();
        s_requests++;
        return true;
    }
    if (s_srv->hasHeader("Authorization") && ++s_badAuth >= BAD_AUTH_MAX) {
        Serial.println("[share] too many wrong codes: stopping");
        s_stopReq = true;
    }
    s_srv->requestAuthentication(BASIC_AUTH, "SquachWatch");
    return false;
}

// The log files this firmware writes, and nothing else on the card.
bool ourFile(const char* base) {
    const bool log  = strncmp(base, "squachwatch-", 12) == 0;
    const bool wig  = strncmp(base, "wigle-", 6) == 0;
    if (!log && !wig) return false;
    for (const char* p = base; *p; p++) {
        const char c = *p;
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return false;
    }
    return strstr(base, "..") == nullptr;
}

const char* baseName(const char* nm) {
    const char* base = nm;
    for (const char* p = nm; *p; p++) if (*p == '/') base = p + 1;
    return base;
}

void handleIndex() {
    if (!allowed()) return;
    String h;
    h.reserve(2048);
    h += F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
           "<title>SquachWatch logs</title><style>body{font-family:sans-serif;margin:16px;max-width:640px}"
           "td{padding:4px 12px 4px 0}a{color:#0645ad}</style></head><body><h2>SquachWatch logs</h2><table>");
    h += F("<tr><td><a href='/blackbox.csv'>blackbox.csv</a></td><td>black box (flash): boots, crashes, ");
    h += String((unsigned)BlackBox::detectionsKept());
    h += F(" detections</td></tr>");
    uint16_t files = 0;
    if (s_sd) {
        FramePush::busLock();
        File dir = SD.open("/");
        if (dir) {
            for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
                const char* base = baseName(f.name());
                if (!f.isDirectory() && ourFile(base)) {
                    h += F("<tr><td><a href='/f?n=");
                    h += base;
                    h += F("'>");
                    h += base;
                    h += F("</a></td><td>");
                    h += String((unsigned long)f.size());
                    h += F(" bytes</td></tr>");
                    files++;
                }
                f.close();
            }
            dir.close();
        }
        FramePush::busUnlock();
    }
    h += F("</table>");
    if (!s_sd)       h += F("<p>No SD card mounted: only the black box is here.</p>");
    else if (!files) h += F("<p>No log files on the SD card yet.</p>");
    h += F("<p>Sharing stops after 5 minutes with no request, 20 minutes at most, or when STOP is tapped.</p>"
           "</body></html>");
    s_srv->send(200, "text/html", h);
}

void handleFile() {
    if (!allowed()) return;
    const String n = s_srv->arg("n");
    if (!s_sd || !ourFile(n.c_str())) { s_srv->send(404, "text/plain", "Not found"); return; }
    FramePush::busLock();
    File f = SD.open(String("/") + n, FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        FramePush::busUnlock();
        s_srv->send(404, "text/plain", "Not found");
        return;
    }
    s_srv->sendHeader("Content-Disposition", String("attachment; filename=\"") + n + "\"");
    const size_t sent = s_srv->streamFile(f, "text/csv");
    f.close();
    FramePush::busUnlock();
    Serial.printf("[share] sent %s, %u bytes\n", n.c_str(), (unsigned)sent);
}

// The black box goes out as it is read, in chunks, never whole: about 1,800
// rows of up to ~130 bytes is more than the heap would give in one piece.
struct Chunk {
    char   buf[1024];
    size_t n = 0;
    void flush() { if (n) { s_srv->sendContent(buf, n); n = 0; } }
};

void handleBlackBox() {
    if (!allowed()) return;
    s_srv->sendHeader("Content-Disposition", "attachment; filename=\"blackbox.csv\"");
    s_srv->setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_srv->send(200, "text/csv", "");
    static Chunk c;   // off the loop task's stack
    c.n = 0;
    BlackBox::dumpTo([](const char* s, void* ctx) {
        Chunk& k = *(Chunk*)ctx;
        const size_t len = strlen(s);
        if (k.n + len > sizeof k.buf) k.flush();
        if (len > sizeof k.buf) { s_srv->sendContent(s, len); return; }
        memcpy(k.buf + k.n, s, len);
        k.n += len;
    }, &c);
    c.flush();
    s_srv->sendContent("");   // the chunked reply's last, empty chunk
    Serial.println("[share] sent blackbox.csv");
}

void startServer(uint32_t now) {
    // On the network anyway, so the clock rides along, as it does on the boot
    // check: a board kept on the release it was flashed with, UPDATE CHECK
    // off, otherwise only learns the time over serial. One NTP request,
    // usually a couple of hundred ms; the cap is for a bad day.
    {
        const uint32_t t0 = millis();
        Clock::syncWait(2000);   // true whenever the clock is set at all, so not proof of an answer
        char clk[24];
        Clock::formatClock(clk, sizeof clk);
        Serial.printf("[share] asked a time server: %lu ms, clock now %s (%s)\n",
                      (unsigned long)(millis() - t0), clk, Clock::trusted() ? "trusted" : "not trusted");
        now = millis();
    }
    snprintf(s_addr, sizeof s_addr, "%s", WiFi.localIP().toString().c_str());
    snprintf(s_code, sizeof s_code, "%06lu", (unsigned long)(esp_random() % 1000000UL));
    s_srv = new WebServer(80);
    static const char* HEADERS[] = { "Authorization" };
    s_srv->collectHeaders(HEADERS, 1);
    s_srv->on("/", HTTP_GET, handleIndex);
    s_srv->on("/f", HTTP_GET, handleFile);
    s_srv->on("/blackbox.csv", HTTP_GET, handleBlackBox);
    s_srv->onNotFound([]() { s_srv->send(404, "text/plain", "Not found"); });
    s_srv->begin();
    s_lastReq = now;
    s_state   = State::SERVING;
    Serial.printf("[share] serving on http://%s/ (network %s)\n", s_addr, s_ssid);
}

}  // namespace

bool begin(uint32_t now) {
    s_why = "";
    if (s_state != State::OFF) return true;
    if (Security::locked())                        { s_why = "Unlock the board first"; return false; }
    if (OtaWifi::state() != OtaWifi::State::OFF)   { s_why = "A WiFi update is running"; return false; }
    const uint8_t n = OtaWifi::savedCount();
    if (!n)                                        { s_why = "Save a network in WIFI NETWORKS"; return false; }
    // The one marked USE first, then the rest in list order.
    s_tryN = 0;
    s_try[s_tryN++] = OtaWifi::savedUse() < n ? OtaWifi::savedUse() : 0;
    for (uint8_t i = 0; i < n && s_tryN < OtaWifi::SAVED_MAX; i++)
        if (i != s_try[0]) s_try[s_tryN++] = i;
    s_tryAt = 0;
    s_sd = SD.cardType() != CARD_NONE;
    s_requests = 0;
    s_badAuth  = 0;
    s_stopReq  = s_timedOut = false;
    s_fail = "";
    s_addr[0] = s_code[0] = 0;
    s_startAt = now;
    WiFi.persistent(false);   // the password stays in our own list, not the driver's
    WiFi.mode(WIFI_STA);
    joinNext(now);
    return true;
}

void end() {
    if (s_state == State::OFF) return;
    if (s_srv) { s_srv->stop(); delete s_srv; s_srv = nullptr; }
    WiFi.disconnect(false, false);
    memset(s_code, 0, sizeof s_code);
    s_state = State::OFF;
    Serial.printf("[share] off after %u request(s)\n", (unsigned)s_requests);
}

void tick(uint32_t now) {
    if (s_state == State::OFF) return;
    if (Security::locked()) { end(); return; }
    if (s_state == State::JOINING) {
        const wl_status_t st = WiFi.status();
        if (st == WL_CONNECTED) startServer(now);
        // Just after begin() the status can still be a failure left over
        // from before; give the join a moment before believing one.
        const bool failed = (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) && now - s_joinAt > JOIN_GRACE_MS;
        if (failed || now - s_joinAt > JOIN_MS) joinNext(now);
        return;
    }
    if (s_state == State::FAILED) {
        // The screen is held awake while sharing; a failure nobody taps OK
        // on must not hold it forever.
        if (now - s_failAt > FAILED_MS) { s_timedOut = true; end(); }
        return;
    }
    s_srv->handleClient();
    if (WiFi.status() != WL_CONNECTED) {
        if (s_srv) { s_srv->stop(); delete s_srv; s_srv = nullptr; }
        s_fail   = "Lost the WiFi network";
        s_state  = State::FAILED;
        s_failAt = now;
        return;
    }
    // millis(), not `now`: a request answered just above stamped s_lastReq
    // with millis(), which is later than the `now` this pass began with, and
    // `now - s_lastReq` would wrap to four billion and read as idle.
    const uint32_t t = millis();
    const bool idle = t - s_lastReq > IDLE_MS, tooLong = t - s_startAt > MAX_MS;
    if (s_stopReq || idle || tooLong) {
        s_timedOut = true;
        end();
    }
}

bool        active()        { return s_state != State::OFF; }
State       state()         { return s_state; }
const char* whyNot()        { return s_why; }
const char* failText()      { return s_fail; }
const char* network()       { return s_ssid; }
const char* address()       { return s_addr; }
const char* code()          { return s_code; }
uint16_t    requests()      { return s_requests; }
bool        endedByTimeout(){ return s_timedOut; }

uint32_t secondsLeft(uint32_t) {
    if (s_state != State::SERVING) return 0;
    // millis() for the same reason as tick(): the frame's `now` can be older
    // than a request stamped since.
    const uint32_t t = millis();
    const uint32_t idleUsed = t - s_lastReq, used = t - s_startAt;
    const uint32_t a = idleUsed < IDLE_MS ? IDLE_MS - idleUsed : 0;
    const uint32_t b = used < MAX_MS ? MAX_MS - used : 0;
    return (a < b ? a : b) / 1000;
}

}  // namespace LogShare
#endif  // SQW_LOG_SHARE
