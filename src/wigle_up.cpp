// SquachWatch-CYD -- the wardrive log straight to WiGLE. See wigle_up.h.
#include "wigle_up.h"
#if SQW_WIGLE_UPLOAD
#include "wardrive.h"
#include "ota_wifi.h"
#include "clock.h"
#include "security.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <mbedtls/base64.h>
#include <string.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif
#ifndef SQW_ENV
#define SQW_ENV "board"
#endif

namespace WigleUp {
namespace {

const char* NS   = "wigle";
const char* HOST = "api.wigle.net";
const char* BOUNDARY = "SquachWatchWiGLE7f3a91";

// api.wigle.net is a Let's Encrypt certificate: wigle.net <- YR2 <- Root YR,
// and the server sends Root YR cross-signed by ISRG Root X1, so X1 is the one
// root to trust (checked 2026-11-21). It is good until 2035.
const char* ISRG_X1 =
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
"-----END CERTIFICATE-----\n";

const uint32_t TASK_STACK  = 12288;    // the updater's was 12 KB when it spoke TLS
const size_t   SB_SIZE     = 8192;
const size_t   CHUNK       = 1024;
const uint32_t AUTO_GAP_MS = 30UL * 60UL * 1000UL;   // between automatic attempts
#if !defined(CYD_GPS)
const uint32_t MAX_ROWS    = 20000;                  // the watch: rows to a request
#endif

// ---- what is kept ----------------------------------------------------------------
Preferences s_prefs;
bool     s_began = false;
char     s_key[112] = "";      // base64 of "name:token"
char     s_name[24] = "";      // the name part, for the row
bool     s_auto = false;
uint32_t s_lastEpoch = 0, s_lastRows = 0;
#if !defined(CYD_GPS)
uint32_t s_markSeq = 0;        // the last record WiGLE has: sector sequence and slot
uint16_t s_markSlot = 0;
#endif

// ---- the run ---------------------------------------------------------------------
volatile State s_state = State::IDLE;
char      s_fail[40] = "";
void    (*s_pause)() = nullptr;
void    (*s_resume)() = nullptr;
StreamBufferHandle_t s_sb = nullptr;
TaskHandle_t s_task = nullptr;
bool      s_paused = false;
bool      s_automatic = false;
uint32_t  s_autoTriedAt = 0;
uint32_t  s_rowsRun = 0;       // rows WiGLE has taken this run
uint16_t  s_jobNo = 0, s_jobsKnown = 0;

// The handshake: the task asks for a job, the loop prepares it and feeds its
// bytes, the task sends and says how it went, the loop acts on that.
volatile bool     s_wantJob = false, s_jobReady = false, s_noMore = false;
volatile bool     s_jobDone = false, s_jobOk = false, s_cancel = false, s_taskDone = false;
volatile uint32_t s_jobLen = 0, s_jobSent = 0;
char              s_jobName[40] = "";

// The loop's side of the current job.
bool     s_preparing = false, s_feeding = false;
uint32_t s_fed = 0;
uint32_t s_jobRows = 0;
uint8_t  s_buf[CHUNK];
uint16_t s_bufLen = 0, s_bufPos = 0;   // bytes read but not yet in the stream buffer

#if defined(CYD_GPS)
char     s_files[16][32];
uint32_t s_sizes[16];
uint8_t  s_fileN = 0, s_fileAt = 0;
uint32_t s_newlines = 0;
#else
Wardrive::Cursor s_cur;
uint32_t s_endSeq = 0;
uint16_t s_endSlot = 0;
bool     s_headerOut = false;
bool     s_pastEnd = false;
uint32_t s_countRows = 0, s_countBytes = 0;
bool after(uint32_t seq, uint16_t slot, uint32_t mSeq, uint16_t mSlot) {
    return mSeq == 0 || seq > mSeq || (seq == mSeq && slot > mSlot);
}
bool atOrBefore(uint32_t seq, uint16_t slot, uint32_t eSeq, uint16_t eSlot) {
    return seq < eSeq || (seq == eSeq && slot <= eSlot);
}
#endif

void load() {
    if (s_began) return;
    s_began = true;
    if (!s_prefs.begin(NS, false)) return;
    String k = s_prefs.getString("key", "");
    snprintf(s_key, sizeof s_key, "%s", k.c_str());
    s_auto      = s_prefs.getBool("auto", false);
    s_lastEpoch = s_prefs.getUInt("lastAt", 0);
    s_lastRows  = s_prefs.getUInt("lastRows", 0);
#if !defined(CYD_GPS)
    s_markSeq   = s_prefs.getUInt("mSeq", 0);
    s_markSlot  = (uint16_t)s_prefs.getUInt("mSlot", 0);
#endif
    s_name[0] = 0;
    if (s_key[0]) {
        unsigned char out[96]; size_t n = 0;
        if (mbedtls_base64_decode(out, sizeof out - 1, &n, (const unsigned char*)s_key, strlen(s_key)) == 0) {
            out[n] = 0;
            const char* c = strchr((const char*)out, ':');
            if (c) { size_t m = (size_t)(c - (const char*)out); if (m >= sizeof s_name) m = sizeof s_name - 1; memcpy(s_name, out, m); s_name[m] = 0; }
        }
    }
}

void fail(const char* why) {
    snprintf(s_fail, sizeof s_fail, "%s", why);
    s_state = State::FAILED;
}

// ---- the task: WiFi and HTTPS ----------------------------------------------------
bool joinWifi() {
    WiFi.disconnect(false, false);
    delay(100);
    // PASSIVE, like every scan this firmware makes (tools/check_emissions.sh):
    // listening for the saved networks, not asking for them.
    const int n = WiFi.scanNetworks(false, false, true, 150);
    int8_t best = -1; int bestRssi = -200;
    for (int j = 0; j < n; j++) {
        const int8_t k = OtaWifi::savedIndexOf(WiFi.SSID(j).c_str());
        if (k >= 0 && WiFi.RSSI(j) > bestRssi) { best = k; bestRssi = WiFi.RSSI(j); }
    }
    WiFi.scanDelete();
    if (best < 0) { fail(OtaWifi::savedCount() ? "NO SAVED WIFI HERE" : "NO WIFI SAVED"); return false; }
    char ssid[33]; snprintf(ssid, sizeof ssid, "%s", OtaWifi::savedSsidAt((uint8_t)best));
    char pass[65] = "";
    OtaWifi::savedPassAt((uint8_t)best, pass, sizeof pass);
    Serial.printf("[wigle] joining %s\n", ssid);
    WiFi.begin(ssid, pass[0] ? pass : nullptr);
    memset(pass, 0, sizeof pass);
    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000 && !s_cancel) delay(100);
    if (WiFi.status() != WL_CONNECTED) { if (!s_cancel) fail("COULD NOT JOIN WIFI"); return false; }
    // The certificate check needs the date: one NTP round trip while joined.
    Clock::syncWait(4000);
    return true;
}

bool writeAll(WiFiClientSecure& c, const uint8_t* p, size_t n) {
    while (n) {
        const size_t w = c.write(p, n);
        if (!w) return false;
        p += w; n -= w;
    }
    return true;
}

// One file to WiGLE. True when it answered "success":true.
bool sendJob() {
    WiFiClientSecure c;
    c.setCACert(ISRG_X1);
    c.setTimeout(20);
    if (!c.connect(HOST, 443)) { fail("NO CONNECTION TO WIGLE"); return false; }
    char pre[256];
    const int preN = snprintf(pre, sizeof pre,
        "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\nContent-Type: text/csv\r\n\r\n",
        BOUNDARY, s_jobName);
    char post[64];
    const int postN = snprintf(post, sizeof post, "\r\n--%s--\r\n", BOUNDARY);
    char head[400];
    const int headN = snprintf(head, sizeof head,
        "POST /api/v2/file/upload HTTP/1.1\r\nHost: %s\r\nAuthorization: Basic %s\r\nAccept: application/json\r\n"
        "User-Agent: SquachWatch/%s\r\nContent-Type: multipart/form-data; boundary=%s\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",
        HOST, s_key, FIRMWARE_VERSION, BOUNDARY, (unsigned long)(preN + s_jobLen + postN));
    if (!writeAll(c, (const uint8_t*)head, headN) || !writeAll(c, (const uint8_t*)pre, preN)) { fail("WIGLE HUNG UP"); c.stop(); return false; }
    uint8_t buf[CHUNK];
    uint32_t left = s_jobLen;
    while (left && !s_cancel) {
        const size_t want = left < CHUNK ? left : CHUNK;
        const size_t got = xStreamBufferReceive(s_sb, buf, want, pdMS_TO_TICKS(10000));
        if (!got) { fail("THE FILE STOPPED"); c.stop(); return false; }
        if (!writeAll(c, buf, got)) { fail("WIGLE HUNG UP"); c.stop(); return false; }
        left -= got; s_jobSent += got;
    }
    if (s_cancel) { c.stop(); return false; }
    if (!writeAll(c, (const uint8_t*)post, postN)) { fail("WIGLE HUNG UP"); c.stop(); return false; }
    // The reply: a status line, headers, a short JSON body.
    int code = 0;
    const uint32_t t0 = millis();
    String status = c.readStringUntil('\n');
    sscanf(status.c_str(), "HTTP/%*s %d", &code);
    while (c.connected() || c.available()) {
        String h = c.readStringUntil('\n');
        if (h.length() <= 1) break;
        if (millis() - t0 > 30000) break;
    }
    char body[512]; size_t bn = 0;
    while ((c.connected() || c.available()) && bn < sizeof body - 1 && millis() - t0 < 30000) {
        const int ch = c.read();
        if (ch < 0) { delay(10); continue; }
        body[bn++] = (char)ch;
    }
    body[bn] = 0;
    c.stop();
    const bool ok = code == 200 && strstr(body, "\"success\":true") != nullptr;
    Serial.printf("[wigle] %s: HTTP %d, %s\n", s_jobName, code, ok ? "taken" : body);
    if (!ok) {
        if (code == 401 || code == 403) fail("WIGLE REFUSED THE KEY");
        else { char w[40]; snprintf(w, sizeof w, "WIGLE SAID %d", code); fail(w); }
    }
    return ok;
}

void run(void*) {
    bool ok = joinWifi();
    while (ok && !s_cancel) {
        s_wantJob = true;
        while (!s_jobReady && !s_noMore && !s_cancel) delay(20);
        s_wantJob = false;
        if (!s_jobReady) break;
        s_jobReady = false;
        s_state = State::SENDING;
        const bool good = sendJob();
        s_jobOk = good;
        s_jobDone = true;
        while (s_jobDone && !s_cancel) delay(20);
        if (!good) ok = false;
    }
    WiFi.disconnect(false, false);
    Serial.printf("[wigle] task stack: %u bytes never used\n", (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
    s_taskDone = true;
    s_task = nullptr;
    vTaskDelete(nullptr);
}

// ---- the loop: preparing jobs and feeding them ----------------------------------------
#if defined(CYD_GPS)
bool nextJob() {
    while (s_fileAt < s_fileN) {
        const char* path = s_files[s_fileAt];
        uint32_t size = 0;
        if (Wardrive::sdOpen(path, size) && size > 0) {
            snprintf(s_jobName, sizeof s_jobName, "%s", path + 1);
            s_jobLen = size; s_jobSent = 0; s_fed = 0; s_newlines = 0;
            s_bufLen = s_bufPos = 0;
            s_jobNo++;
            return true;
        }
        Wardrive::sdClose();
        s_fileAt++;
    }
    return false;
}
// Up to `room` more bytes of the file into the stream buffer.
void feed() {
    for (int guard = 0; guard < 8 && s_fed < s_jobLen; guard++) {
        if (s_bufPos >= s_bufLen) {
            const uint32_t want = s_jobLen - s_fed < CHUNK ? s_jobLen - s_fed : CHUNK;
            const int got = Wardrive::sdRead(s_buf, want);
            if (got <= 0) { s_cancel = true; fail("COULD NOT READ THE CARD"); return; }
            s_bufLen = (uint16_t)got; s_bufPos = 0;
            for (int i = 0; i < got; i++) if (s_buf[i] == '\n') s_newlines++;
        }
        const size_t sent = xStreamBufferSend(s_sb, s_buf + s_bufPos, s_bufLen - s_bufPos, 0);
        if (!sent) return;
        s_bufPos = (uint16_t)(s_bufPos + sent);
        s_fed += sent;
    }
}
void jobFinished(bool ok) {
    Wardrive::sdClose();
    if (ok) {
        Wardrive::sdMarkSent(s_files[s_fileAt]);
        s_rowsRun += s_newlines > 2 ? s_newlines - 2 : 0;   // the two header lines are not rows
    }
    s_fileAt++;
}
bool anythingWaiting() { return Wardrive::sdWaitingCount() > 0 || (s_fileN = Wardrive::sdWaitingFiles(s_files, s_sizes, 16)) > 0; }
void beginRun() {
    s_fileN = Wardrive::sdWaitingFiles(s_files, s_sizes, 16);
    s_fileAt = 0;
    s_jobsKnown = s_fileN;
}
#else
// The watch: count first (so Content-Length is right), then send the same
// rows again from the same place. Nothing is appended while the radios are
// paused, so the second pass finds exactly what the first one counted.
size_t headerBytes(char* out, size_t n) {
    return Wardrive::headerLines(out, n, FIRMWARE_VERSION, "twatch-s3", "LilyGo T-Watch S3 Plus", "LilyGo");
}
bool countStep() {
    Wardrive::Record r; uint32_t seq; uint16_t slot;
    char row[320];
    for (int i = 0; i < 400; i++) {
        if (s_countRows >= MAX_ROWS || !Wardrive::next(s_cur, r, seq, slot)) return true;   // done counting
        if (!after(seq, slot, s_markSeq, s_markSlot)) continue;
        s_endSeq = seq; s_endSlot = slot;
        if (r.flags & Wardrive::F_FAKE) continue;
        const size_t w = Wardrive::csvRow(r, row, sizeof row);
        if (!w) continue;
        s_countRows++; s_countBytes += w;
    }
    return false;
}
void feed() {
    char row[320];
    for (int guard = 0; guard < 16 && s_fed < s_jobLen; guard++) {
        if (s_bufPos >= s_bufLen) {
            s_bufLen = s_bufPos = 0;
            if (!s_headerOut) {
                s_headerOut = true;
                const size_t n = headerBytes((char*)s_buf, sizeof s_buf);
                s_bufLen = (uint16_t)n;
            } else {
                Wardrive::Record r; uint32_t seq; uint16_t slot;
                while (!s_pastEnd && s_bufLen == 0) {
                    if (!Wardrive::next(s_cur, r, seq, slot)) { s_pastEnd = true; break; }
                    if (!after(seq, slot, s_markSeq, s_markSlot)) continue;
                    if (!atOrBefore(seq, slot, s_endSeq, s_endSlot)) { s_pastEnd = true; break; }
                    if (r.flags & Wardrive::F_FAKE) continue;
                    const size_t w = Wardrive::csvRow(r, row, sizeof row);
                    if (w) { memcpy(s_buf, row, w); s_bufLen = (uint16_t)w; }
                }
                if (s_bufLen == 0) { s_cancel = true; fail("THE LOG CHANGED"); return; }
            }
        }
        const size_t sent = xStreamBufferSend(s_sb, s_buf + s_bufPos, s_bufLen - s_bufPos, 0);
        if (!sent) return;
        s_bufPos = (uint16_t)(s_bufPos + sent);
        s_fed += sent;
    }
}
void jobFinished(bool ok) {
    if (!ok) return;
    s_markSeq = s_endSeq; s_markSlot = s_endSlot;
    s_prefs.putUInt("mSeq", s_markSeq);
    s_prefs.putUInt("mSlot", s_markSlot);
    s_rowsRun += s_jobRows;
}
bool anythingWaiting() {
    if (s_markSeq > Wardrive::newestSeq()) forgetProgress();
    return Wardrive::countAfter(s_markSeq, s_markSlot) > 0;
}
void beginRun() { s_jobsKnown = 0; }
#endif

void finishRun() {
    if (s_sb) { vStreamBufferDelete(s_sb); s_sb = nullptr; }
#if defined(CYD_GPS)
    Wardrive::sdClose();
#endif
    if (s_paused && s_resume) s_resume();
    s_paused = false;
    if (s_rowsRun) {
        s_lastEpoch = Clock::nowEpoch();
        s_lastRows  = s_rowsRun;
        s_prefs.putUInt("lastAt", s_lastEpoch);
        s_prefs.putUInt("lastRows", s_lastRows);
    }
    if (s_state != State::FAILED) s_state = State::DONE;
    // A run nobody asked for goes quietly: AUTO UPLOAD tries again later, and
    // a failure it hit is not left on the row as if somebody had pressed it.
    if (s_automatic && s_state == State::FAILED) s_state = State::IDLE;
    Serial.printf("[wigle] run over: %lu rows to WiGLE%s%s\n", (unsigned long)s_rowsRun,
                  s_state == State::FAILED ? ", stopped: " : "", s_state == State::FAILED ? s_fail : "");
}

}  // namespace

// ---- the API --------------------------------------------------------------------------
void begin() { load(); }
void setRadioHooks(void (*pause)(), void (*resume)()) { s_pause = pause; s_resume = resume; }

bool setKey(const char* encoded) {
    load();
    if (!encoded) return false;
    char k[sizeof s_key]; size_t n = 0;
    for (const char* p = encoded; *p && n < sizeof k - 1; p++)
        if (*p != ' ' && *p != '\r' && *p != '\n' && *p != '\t') k[n++] = *p;
    k[n] = 0;
    unsigned char out[96]; size_t m = 0;
    if (n < 8 || mbedtls_base64_decode(out, sizeof out - 1, &m, (const unsigned char*)k, n) != 0) return false;
    out[m] = 0;
    if (!strchr((const char*)out, ':')) return false;
    snprintf(s_key, sizeof s_key, "%s", k);
    s_prefs.putString("key", s_key);
    s_began = false; load();   // the name, read back the way it will be at boot
    return true;
}
void clearKey() { load(); s_key[0] = 0; s_name[0] = 0; s_prefs.remove("key"); }
bool hasKey() { load(); return s_key[0] != 0; }
const char* keyName() { load(); return s_name; }

bool start() {
    load();
    if (busy()) return true;
    s_fail[0] = 0;
    if (Security::locked())     { fail("LOCKED"); return false; }
    if (!s_key[0])              { fail("NO WIGLE KEY"); return false; }
    if (!OtaWifi::savedCount()) { fail("NO WIFI SAVED"); return false; }
    if (OtaWifi::state() != OtaWifi::State::OFF) { fail("AN UPDATE HAS THE WIFI"); return false; }
    if (!anythingWaiting())     { fail("NOTHING NEW TO SEND"); return false; }
    s_sb = xStreamBufferCreate(SB_SIZE, 1);
    if (!s_sb) { fail("NOT ENOUGH MEMORY"); return false; }
    beginRun();
    s_wantJob = s_jobReady = s_noMore = s_jobDone = s_jobOk = s_cancel = s_taskDone = false;
    s_preparing = s_feeding = false;
    s_rowsRun = 0; s_jobNo = 0;
    if (s_pause) s_pause();
    s_paused = true;
    s_state = State::JOINING;
    if (xTaskCreatePinnedToCore(run, "wigle", TASK_STACK, nullptr, 1, &s_task, 0) != pdPASS) {
        s_task = nullptr;
        fail("NOT ENOUGH MEMORY");
        finishRun();
        return false;
    }
    Serial.printf("[wigle] upload started%s\n", s_automatic ? " (automatic)" : "");
    return true;
}

void cancel() { if (busy()) s_cancel = true; }

void tick(uint32_t nowMs, bool autoOk) {
    load();
    if (!busy() && !s_task) {
        if (s_auto && autoOk && s_key[0] && (!s_autoTriedAt || nowMs - s_autoTriedAt >= AUTO_GAP_MS)) {
            s_autoTriedAt = nowMs ? nowMs : 1;
            s_automatic = true;
            if (!start() && s_state == State::FAILED) s_state = State::IDLE;
        }
        return;
    }
    // A job asked for and not yet prepared.
    if (s_wantJob && !s_preparing && !s_jobReady && !s_feeding) {
#if defined(CYD_GPS)
        if (nextJob()) { s_feeding = true; s_jobReady = true; }
        else s_noMore = true;
#else
        s_cur = Wardrive::Cursor();
        s_countRows = s_countBytes = 0;
        s_endSeq = s_markSeq; s_endSlot = s_markSlot;
        s_preparing = true;
#endif
    }
#if !defined(CYD_GPS)
    if (s_preparing && countStep()) {
        s_preparing = false;
        if (!s_countRows) s_noMore = true;
        else {
            char h[512];
            const size_t hb = headerBytes(h, sizeof h);
            time_t t = (time_t)Clock::nowEpoch(); struct tm tm; gmtime_r(&t, &tm);
            snprintf(s_jobName, sizeof s_jobName, "squachwatch-%04d%02d%02d-%02d%02d%02d-%u.csv",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (unsigned)(s_jobNo + 1));
            s_jobLen = (uint32_t)hb + s_countBytes; s_jobSent = 0; s_fed = 0;
            s_jobRows = s_countRows;
            s_cur = Wardrive::Cursor(); s_headerOut = false; s_pastEnd = false;
            s_bufLen = s_bufPos = 0;
            s_jobNo++;
            s_feeding = true;
            s_jobReady = true;
        }
    }
#endif
    if (s_feeding && s_sb) feed();
    if (s_jobDone) {
        s_feeding = false;
        jobFinished(s_jobOk);
        s_jobDone = false;
    }
    if (s_taskDone && !s_task) {
        s_taskDone = false;
        finishRun();
        s_automatic = false;
    }
}

State state() { return s_state; }
bool busy() { return s_state == State::JOINING || s_state == State::SENDING; }
const char* failText() { return s_fail; }

const char* status(char* buf, size_t n) {
    load();
    switch (s_state) {
        case State::JOINING: snprintf(buf, n, "JOINING WIFI"); break;
        case State::SENDING: {
            const unsigned pct = s_jobLen ? (unsigned)((uint64_t)s_jobSent * 100 / s_jobLen) : 0;
            if (s_jobsKnown > 1) snprintf(buf, n, "SENDING %u/%u %u%%", (unsigned)s_jobNo, (unsigned)s_jobsKnown, pct);
            else                 snprintf(buf, n, "SENDING %u%%", pct);
            break;
        }
        case State::DONE:   snprintf(buf, n, "SENT %lu ROWS", (unsigned long)s_rowsRun); break;
        case State::FAILED: snprintf(buf, n, "%s", s_fail); break;
        default:
            if (!s_key[0]) snprintf(buf, n, "NEEDS A KEY");
            else {
                const uint32_t w = waiting();
#if defined(CYD_GPS)
                if (w) snprintf(buf, n, "%lu FILE%s READY", (unsigned long)w, w == 1 ? "" : "S");
#else
                if (w) snprintf(buf, n, "%lu ROWS READY", (unsigned long)w);
#endif
                else snprintf(buf, n, "UP TO DATE");
            }
            break;
    }
    return buf;
}

bool autoOn() { load(); return s_auto; }
void toggleAuto() { load(); s_auto = !s_auto; s_prefs.putBool("auto", s_auto); s_autoTriedAt = 0; }
uint32_t lastEpoch() { load(); return s_lastEpoch; }
uint32_t lastRows()  { load(); return s_lastRows; }

uint32_t waiting() {
    load();
#if defined(CYD_GPS)
    return Wardrive::sdWaitingCount();
#else
    if (s_markSeq > Wardrive::newestSeq()) return Wardrive::count();
    return Wardrive::countAfter(s_markSeq, s_markSlot);
#endif
}

void forgetProgress() {
    load();
#if !defined(CYD_GPS)
    s_markSeq = 0; s_markSlot = 0;
    s_prefs.remove("mSeq"); s_prefs.remove("mSlot");
#endif
}

void wipe() {
    load();
    cancel();
    clearKey();
    forgetProgress();
    s_lastEpoch = s_lastRows = 0;
    s_auto = false;
    s_prefs.clear();
}

}  // namespace WigleUp
#endif
