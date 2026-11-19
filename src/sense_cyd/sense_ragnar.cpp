// SquachWatch-CYD -- HUMAN SENSING <-> Ragnar RuSense link. See include/sense_ragnar.h.
//
// One task, made when HUMAN SENSING opens with a host and token saved, and told
// to go when it closes. Every 2 s (sooner when the board's state changes) it
// posts the board's verdict and reads the mesh's. Three failures in a row and
// it waits 30 s before trying again, so a Ragnar that is off costs little.
//
// The task never touches the engine: the loop hands it the latest SenseReport
// (report()), and it hands the screen a View, both under one spinlock.
// The Tab5 senses through its C6 and has no link yet.
#if defined(SENSE_CYD_APP) && !defined(TAB5)
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdlib.h>
#include "sense_ragnar.h"
#include "settings.h"

namespace SenseRagnar {
namespace {

const uint32_t kPeriodMs   = 2000;
const uint32_t kBackoffMs  = 30000;
const uint32_t kStaleMs    = 6000;
const uint16_t kTimeoutMs  = 1500;
const uint32_t kStack      = 5120;    // lora_feed.cpp's measure for this HTTPClient shape
const uint32_t kMinHeap    = 30000;   // to start: below this the board keeps its heap for sensing
// Before every request too: the join and DHCP take their share after start(),
// and HTTPClient plus a response body want a few KB in one piece.
const uint32_t kReqHeap    = 16000;
const uint32_t kReqBlock   = 8000;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
// Bumped by start() and stop(): a task runs while the generation it was made
// in is current, so a stop() during a request needs no wait, and a start()
// right after it simply makes a new task (the old one leaves on its own).
volatile uint32_t s_gen = 0;
volatile uint8_t  s_alive = 0;
TaskHandle_t      s_task = nullptr;

SenseReport s_rep = {};          // the loop's latest (s_mux)
bool        s_haveRep = false;
uint8_t     s_lastState = 0xFF;  // for the push-now on a change
View        s_view = {};         // the mesh's (s_mux)
uint32_t    s_viewAt = 0;
volatile uint8_t  s_fails = 0;
volatile int16_t  s_lastCode = 0;
volatile uint32_t s_pulls = 0, s_pushes = 0;
volatile bool     s_heapShort = false;
// The task's own copy of the settings, taken in start(): the console may
// rewrite Settings' buffers on the loop task at any moment, and a change is
// promised to apply when HUMAN SENSING next opens, not mid-request.
char    s_host[64] = "", s_tok[48] = "";
uint8_t s_mode = 0;

void note(const char* fmt, ...) {
    char b[200];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    Serial.printf("[sense] ragnar %s\n", b);
}
// The running link's host (the task's copy), or the saved one for the console.
const char* shownHost(const char* h = nullptr) { return Settings::privacyMode() ? "(hidden)" : (h ? h : s_host); }
const char* linkName(uint8_t m) { static const char* const N[4] = { "off", "pull", "push", "both" }; return N[m & 3]; }

// "http://host:port" + path. A host with no port gets Ragnar's 8000.
void url(char* out, size_t n, const char* path) {
    const char* h = s_host;
    if (!strncasecmp(h, "http://", 7)) h += 7;
    const char* colon = strchr(h, ':');
    snprintf(out, n, colon ? "http://%s%s" : "http://%s:8000%s", h, path);
}

// A flat JSON body (Flask's jsonify): the value after "key", or null.
const char* jfind(const char* body, const char* key) {
    char pat[24]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char* p = strstr(body, pat);
    if (!p) return nullptr;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    return *p ? p : nullptr;
}
bool jbool(const char* body, const char* key) { const char* p = jfind(body, key); return p && !strncmp(p, "true", 4); }
float jnum(const char* body, const char* key, float none) {
    const char* p = jfind(body, key);
    if (!p || !strncmp(p, "null", 4)) return none;
    return (float)strtod(p, nullptr);
}
long jint(const char* body, const char* key, long none) { return (long)jnum(body, key, (float)none); }

// One HTTP exchange's setup: the timeouts, the URL and the bearer token.
bool open(HTTPClient& http, const char* path) {
    char u[96]; url(u, sizeof u, path);
    http.setConnectTimeout(kTimeoutMs); http.setTimeout(kTimeoutMs); http.setReuse(false);
    if (!http.begin(u)) return false;
    char auth[64]; snprintf(auth, sizeof auth, "Bearer %s", s_tok);
    http.addHeader("Authorization", auth);
    return true;
}

bool pullOnce() {
    HTTPClient http;
    if (!open(http, "/api/cyd/rusense")) return false;
    const int code = http.GET();
    s_lastCode = (int16_t)code;
    if (code != 200) { http.end(); return false; }
    String body = http.getString();
    http.end();
    const char* b = body.c_str();
    View v = {};
    v.ok      = jbool(b, "rusense");
    v.present = jbool(b, "present");
    v.people  = (uint8_t)constrain(jint(b, "people", 0), 0, 99);
    v.conf    = (int8_t)constrain(jint(b, "conf", -1), -1, 100);
    v.model   = jbool(b, "model");
    v.vote    = jbool(b, "cyd_vote");
    v.hr      = (int16_t)jint(b, "hr", -1);
    const float br = jnum(b, "br", -1.0f);
    v.br10    = br < 0 ? -1 : (int16_t)(br * 10.0f + 0.5f);
    v.up      = (int8_t)constrain(jint(b, "up", -1), -1, 99);
    v.nodes   = (int8_t)constrain(jint(b, "nodes", -1), -1, 99);
    v.sightAgo = (int32_t)jint(b, "sight_ago", -1);
    v.sightS   = (int32_t)jint(b, "sight_s", 0);
    const char* m = jfind(b, "motion");
    if (m && *m == '"') { m++; size_t k = 0; while (m[k] && m[k] != '"' && k < sizeof v.motion - 1) { v.motion[k] = m[k]; k++; } }
    portENTER_CRITICAL(&s_mux); s_view = v; s_viewAt = millis(); portEXIT_CRITICAL(&s_mux);
    s_pulls++;
    return true;
}

bool pushOnce() {
    SenseReport r; bool have;
    portENTER_CRITICAL(&s_mux); r = s_rep; have = s_haveRep; portEXIT_CRITICAL(&s_mux);
    if (!have || !senseModeLive(r.mode)) return true;   // nothing to say yet: not a failure
    char body[160];
    snprintf(body, sizeof body,
             "{\"node\":\"squachwatch\",\"state\":%u,\"score\":%u,\"rel\":%u,\"presence\":%s,\"rssi\":%d,\"chan\":%u}",
             (unsigned)r.state, (unsigned)r.score, (unsigned)r.rel, r.presence ? "true" : "false", (int)r.rssi, (unsigned)r.chan);
    HTTPClient http;
    if (!open(http, "/api/cyd/presence")) return false;
    http.addHeader("Content-Type", "application/json");
    const int code = http.POST((uint8_t*)body, strlen(body));
    http.end();
    s_lastCode = (int16_t)code;
    if (code != 200) return false;
    s_pushes++;
    return true;
}

void worker(void* arg) {
    const uint32_t gen = (uint32_t)(uintptr_t)arg;
    s_alive++;
    uint32_t waitMs = 500;   // the first exchange soon after the screen opens
    while (gen == s_gen) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
        if (gen != s_gen) break;
        if (WiFi.status() != WL_CONNECTED) { waitMs = kPeriodMs; continue; }
        // Short of heap this cycle: skip it rather than starve the sensing.
        const bool shortNow = ESP.getFreeHeap() < kReqHeap || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kReqBlock;
        if (shortNow != s_heapShort) {
            s_heapShort = shortNow;
            note(shortNow ? "pausing: heap %lu, largest %lu" : "heap back (%lu, largest %lu)",
                 (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        }
        if (shortNow) { waitMs = kPeriodMs; continue; }
        const uint8_t mode = s_mode;
        bool ok = true;
        if (mode & LINK_PUSH) ok = pushOnce() && ok;
        if (gen != s_gen) break;
        if (mode & LINK_PULL) ok = pullOnce() && ok;
        if (ok) {
            if (s_fails >= 3) note("back (%s)", shownHost());
            s_fails = 0; waitMs = kPeriodMs;
        } else {
            s_fails = s_fails < 250 ? s_fails + 1 : s_fails;
            if (s_fails == 3) note("no answer from %s (last %d), trying every 30 s", shownHost(), (int)s_lastCode);
            waitMs = s_fails >= 3 ? kBackoffMs : kPeriodMs;
        }
    }
    s_alive--;
    vTaskDelete(nullptr);
}

}  // namespace

uint8_t linkMode()   { return Settings::ragnarLink(); }
bool    configured() { return Settings::ragnarHost()[0] && Settings::ragnarToken()[0] && linkMode() != LINK_OFF; }
bool    pulling()    { return configured() && (linkMode() & LINK_PULL); }

void start() {
    if (!configured()) return;
    const uint32_t heap = ESP.getFreeHeap();
    if (heap < kMinHeap) { note("off: heap %lu is short for it (needs %lu)", (unsigned long)heap, (unsigned long)kMinHeap); return; }
    portENTER_CRITICAL(&s_mux); s_haveRep = false; s_viewAt = 0; s_view = {}; portEXIT_CRITICAL(&s_mux);
    s_lastState = 0xFF; s_fails = 0; s_lastCode = 0; s_heapShort = false;
    snprintf(s_host, sizeof s_host, "%s", Settings::ragnarHost());
    snprintf(s_tok, sizeof s_tok, "%s", Settings::ragnarToken());
    s_mode = linkMode();
    const uint32_t gen = ++s_gen;
    if (xTaskCreatePinnedToCore(worker, "ragnar", kStack, (void*)(uintptr_t)gen, 1, &s_task,
                                0) != pdPASS) {   // the WiFi core: the loop's 50 Hz pings keep core 1
        s_task = nullptr; note("off: no task");
        return;
    }
    note("on: %s, %s", shownHost(), linkName(s_mode));
}

void stop() {
    if (!s_task) return;
    // Wake it first, while its generation is still current: until s_gen
    // moves, the task cannot leave its loop, so the handle is still alive.
    // Bumping first let it exit and delete itself before the notify, which
    // then wrote into a freed task (the code review, 2026-11-19).
    TaskHandle_t t = s_task;
    s_task = nullptr;
    xTaskNotifyGive(t);
    s_gen++;                                  // it leaves at its next check
}

void report(const SenseReport& r) {
    if (!s_task) return;
    portENTER_CRITICAL(&s_mux); s_rep = r; s_haveRep = true; portEXIT_CRITICAL(&s_mux);
    if (r.state != s_lastState) {             // a change goes now, not in 2 s
        const bool first = s_lastState == 0xFF;
        s_lastState = r.state;
        if (!first && (s_mode & LINK_PUSH)) xTaskNotifyGive(s_task);
    }
}

bool view(View& out, uint32_t now) {
    portENTER_CRITICAL(&s_mux);
    const uint32_t at = s_viewAt; out = s_view;
    portEXIT_CRITICAL(&s_mux);
    // Signed: `now` is the frame's, taken before a pull on the other core may
    // have stamped s_viewAt, and an unsigned difference called that stale.
    return at && (int32_t)(now - at) < (int32_t)kStaleMs;
}

const char* trouble() {
    if (!configured()) return "";
    if (!s_task) return "OFF";
    if (s_lastCode == 401 || s_lastCode == 403) return "BAD TOKEN";
    if (s_fails >= 3) return "OFFLINE";
    if (s_heapShort) return "LOW HEAP";
    return "";
}

void command(char* arg) {
    while (*arg == ' ') arg++;
    char* sp = strchr(arg, ' ');
    char* rest = nullptr;
    if (sp) { *sp = 0; rest = sp + 1; while (*rest == ' ') rest++; }
    if (!*arg || !strcasecmp(arg, "STAT")) {
        note("%s, host %s, token %s, link %s, task %s, alive %u, pulls %lu pushes %lu, fails %u, last %d",
             configured() ? "set" : "not set", Settings::ragnarHost()[0] ? shownHost(Settings::ragnarHost()) : "-",
             Settings::ragnarToken()[0] ? "saved" : "-", linkName(linkMode()), s_task ? "up" : "down",
             (unsigned)s_alive, (unsigned long)s_pulls, (unsigned long)s_pushes, (unsigned)s_fails, (int)s_lastCode);
        return;
    }
    if (!strcasecmp(arg, "OFF")) {
        stop(); Settings::setRagnar("", ""); note("forgot the host and token");
        return;
    }
    if (!strcasecmp(arg, "LINK")) {
        uint8_t m = 0xFF;
        for (uint8_t k = 0; k < 4; k++) if (rest && !strcasecmp(rest, linkName(k))) m = k;
        if (m == 0xFF) { note("LINK off|pull|push|both (now %s)", linkName(linkMode())); return; }
        Settings::setRagnarLink(m);
        note("link %s (applies when HUMAN SENSING next opens)", linkName(m));
        return;
    }
    // SENSE RAGNAR <host[:port]> <token>
    if (!rest || !*rest || strchr(rest, ' ') || strlen(arg) > 63 || strlen(rest) > 47) {
        note("SENSE RAGNAR <host[:port]> <token> | LINK off|pull|push|both | OFF | STAT");
        return;
    }
    Settings::setRagnar(arg, rest);
    note("saved %s; the link starts when HUMAN SENSING next opens", shownHost(Settings::ragnarHost()));
}

}  // namespace SenseRagnar
#endif
