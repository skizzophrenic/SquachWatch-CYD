// HUMAN SENSING on the M5Stack Tab5: the engine behind sense_cyd.h, run on
// the Tab5's own radio (its ESP32-C6, over ESP-Hosted).
//
// On every other board the sensing engine (sense_cyd.cpp) reads the radio's
// channel state itself. The Tab5's P4 has no radio: its WiFi is the C6's,
// reached over SDIO, and channel state never crosses that link. So the work
// is split the way the Tab5 sensing app proved it (I:\GitHub\Tab5-Monster-C5-
// CSI-Human-Sensing, 2026-11-17):
//   - the C6 image (tools/tab5-c6/c6_radio.bin, hello "squach-sniff 2")
//     carries sense_c6.cpp: the motion models, one per transmitter, and the
//     VITALS chest signal, and sends ten status lines a second ($S, $T, $V);
//   - this file joins the saved network over ESP-Hosted, pings the router
//     fifty times a second (the readings), tells the C6 `station <bssid>
//     <chan>`, and turns the lines into what the screens read (SenseStore,
//     Vitals, the spectrum) -- the same calls sense_cyd.cpp makes;
//   - with no network saved, or no join in 30 s, the C6 listens instead
//     (`park auto`): fewer readings, whatever the house's devices send.
// The detector lets go of the radio first (DetectionEngine::startUpdateRadio
// switches the sniffer off) and the C6's frame event slot is lent to the
// sensing lines (Tab5Radio::senseLink) until the screen closes.
//
// The motion model is ESP32Marauder's CsiMotionModel (MIT License, Copyright
// (c) 2020 Just Call Me Koko, https://github.com/justcallmekoko/ESP32Marauder),
// running on the C6; see tools/tab5-c6/sense/.
#include "sense_cyd.h"
#if defined(SENSE_CYD_APP) && defined(TAB5)
#include <Arduino.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include "sense_store.h"
#include "sense_vitals.h"
#include "csi_vitals.h"
#include "tab5_radio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
extern "C" {
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "ping/ping_sock.h"
}

volatile uint8_t g_senseConsole = 0;

namespace {

// ---- Lines from the C6 (RPC task -> loop) ----------------------------------
struct Line { uint8_t len; char text[161]; };   // SENSE_LINE_MAX is 160
QueueHandle_t s_q = nullptr;
Line          s_in;                 // filled on the RPC task only
volatile uint32_t s_linesLost = 0;
void onLine(const char* d, size_t n) {
    if (!s_q || !n) return;
    if (n > sizeof s_in.text - 1) n = sizeof s_in.text - 1;
    s_in.len = (uint8_t)n;
    memcpy(s_in.text, d, n);
    s_in.text[n] = 0;
    if (xQueueSend(s_q, &s_in, 0) != pdTRUE) s_linesLost = s_linesLost + 1;
}

// ---- State --------------------------------------------------------------------
enum class Join : uint8_t { NONE, JOINING, UP, LISTEN };
bool     s_running = false, s_linked = false;
Join     s_join = Join::NONE;
char     s_ssid[33] = "";
uint8_t  s_mode = SENSE_MODE_OFF;
uint32_t s_startAt = 0, s_retryAt = 0, s_statAt = 0;
bool     s_everUp = false;
volatile bool s_gotIp = false, s_lost = false;
volatile uint16_t s_reason = 0;
volatile uint32_t s_pingOk = 0, s_pingLost = 0;
uint32_t s_okAt = 0, s_lostAt = 0;
esp_ping_handle_t s_ping = nullptr;
bool     s_vitOn = false;
// VITALS' chest signal is fitted here, on the P4: the C6 sends a frame every
// 50 ms (`vitals frames`) and this does the 20 s fit it had no room for
// beside its Bluetooth controller (10 s there, a weak breathing signal on
// the bench, 2026-11-17). The window lives in PSRAM.
CsiVitals* s_cv = nullptr;
uint32_t   s_cvFrames = 0;
const uint32_t GIVE_UP_MS = 30000, RETRY_MS = 3000;

void note(const char* fmt, ...) {
    char b[160];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    Serial.printf("[sense] %s\n", b);
}

// ---- The network ------------------------------------------------------------
void onWifi(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_reason = data ? ((wifi_event_sta_disconnected_t*)data)->reason : 0;
        s_lost = true;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_gotIp = true;
    }
}
void pingOk(esp_ping_handle_t, void*)   { s_pingOk = s_pingOk + 1; }
void pingLost(esp_ping_handle_t, void*) { s_pingLost = s_pingLost + 1; }
void stopPing() {
    if (!s_ping) return;
    esp_ping_stop(s_ping);
    esp_ping_delete_session(s_ping);
    s_ping = nullptr;
}
// Fifty a second at most: the next ping goes 20 ms after the last reply.
bool startPing(uint32_t gw) {
    stopPing();
    esp_ping_config_t pc = ESP_PING_DEFAULT_CONFIG();
    pc.target_addr.type = IPADDR_TYPE_V4;
    pc.target_addr.u_addr.ip4.addr = gw;
    pc.count = ESP_PING_COUNT_INFINITE;
    pc.interval_ms = 20;
    pc.timeout_ms = 500;
    pc.data_size = 8;
    pc.task_stack_size = 3072;
    esp_ping_callbacks_t cb = {};
    cb.on_ping_success = pingOk;
    cb.on_ping_timeout = pingLost;
    if (esp_ping_new_session(&pc, &cb, &s_ping) != ESP_OK) { s_ping = nullptr; return false; }
    return esp_ping_start(s_ping) == ESP_OK;
}
void listen(const char* why) {
    stopPing();
    if (s_join == Join::JOINING || s_join == Join::UP) esp_wifi_disconnect();
    s_join = Join::LISTEN;
    note("%s: listening instead (the C6 parks on the loudest channel)", why);
    Tab5Radio::senseSend("park auto");
}
void startJoin(const char* pass) {
    static bool handlers = false;
    if (!handlers) {
        esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, onWifi, nullptr, nullptr);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onWifi, nullptr, nullptr);
        handlers = true;
    }
    wifi_config_t cfg = {};
    memcpy(cfg.sta.ssid, s_ssid, strlen(s_ssid));
    if (pass) memcpy(cfg.sta.password, pass, strnlen(pass, 64));
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    const esp_err_t ec = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    memset(&cfg, 0, sizeof cfg);
    if (ec != ESP_OK) { note("join config refused: %s", esp_err_to_name(ec)); listen("no join"); return; }
    s_gotIp = s_lost = false; s_reason = 0; s_everUp = false;
    const esp_err_t e = esp_wifi_connect();
    s_startAt = millis(); s_retryAt = 0;
    s_join = Join::JOINING;
    note("joining the saved network: %s", esp_err_to_name(e));
}
void wentUp(uint32_t now) {
    wifi_ap_record_t ap = {};
    esp_netif_ip_info_t ip = {};
    esp_netif_t* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    const bool haveAp = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    const bool haveIp = nif && esp_netif_get_ip_info(nif, &ip) == ESP_OK && ip.gw.addr;
    if (!haveAp || !haveIp) { listen(haveAp ? "joined, but no router address" : "joined, but no access point info"); return; }
    const bool pinging = startPing(ip.gw.addr);
    char cmd[48];
    snprintf(cmd, sizeof cmd, "station %02x:%02x:%02x:%02x:%02x:%02x %u", ap.bssid[0], ap.bssid[1], ap.bssid[2],
             ap.bssid[3], ap.bssid[4], ap.bssid[5], (unsigned)ap.primary);
    Tab5Radio::senseSend(cmd);
    if (s_vitOn) Tab5Radio::senseSend("vitals frames");   // again, so it follows the router
    s_join = Join::UP; s_everUp = true; s_reason = 0;
    s_statAt = now; s_okAt = s_pingOk; s_lostAt = s_pingLost;
    note("joined: channel %u rssi %d, %s", (unsigned)ap.primary, (int)ap.rssi, pinging ? "pinging the router every 20 ms" : "THE PING DID NOT START");
}
void joinTick(uint32_t now) {
    if (s_lost) {
        s_lost = false;
        if (s_join == Join::UP) { stopPing(); note("the network dropped (reason %u): joining again", (unsigned)s_reason); s_join = Join::JOINING; }
        if (s_join == Join::JOINING) s_retryAt = now + RETRY_MS;
    }
    if (s_join == Join::JOINING) {
        if (s_gotIp) { s_gotIp = false; wentUp(now); return; }
        if (!s_everUp && now - s_startAt > GIVE_UP_MS) { listen("no join in 30 s"); return; }
        if (s_retryAt && (int32_t)(now - s_retryAt) >= 0) { s_retryAt = 0; esp_wifi_connect(); }
    } else if (s_join == Join::UP && now - s_statAt >= 10000) {
        const uint32_t ok = s_pingOk, lost = s_pingLost;
        note("pings %lu/s ok, %lu lost in 10 s; %lu lines lost", (unsigned long)((ok - s_okAt) / 10), (unsigned long)(lost - s_lostAt),
             (unsigned long)s_linesLost);
        s_okAt = ok; s_lostAt = lost; s_statAt = now;
    }
}

// ---- SPECTRUM.EXE ---------------------------------------------------------------
// The other boards take the channel's shape straight from the radio; here only
// the C6's status lines arrive, so the rhythm is read from each line's "live"
// value (how far the latest readings sit from the calibration), ten a second:
// a 128-point FFT of the last 12.8 s, bins 0-31 = 0-2.42 Hz, in dB over the
// room's recent level, one row per line -- the same picture, the same calls.
const int kSpN = 128, kSpBins = 32, kSpRows = 108;
struct SpecState {
    float ring[kSpN]; uint32_t ringTotal;
    float re[kSpN], im[kSpN];
    float ref; int refN;
    uint8_t rows[kSpRows][kSpBins]; int rowHead, rowCount;
    int peak;
};
SpecState* s_sp = nullptr;
void fft128(float* re, float* im) {
    const int n = kSpN;
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -6.2831853f / len;
        for (int k = 0; k < len / 2; k++) {
            const float wr = cosf(ang * k), wi = sinf(ang * k);
            for (int i = 0; i < n; i += len) {
                const int a = i + k, b = a + len / 2;
                const float vr = re[b] * wr - im[b] * wi, vi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - vr; im[b] = im[a] - vi; re[a] += vr; im[a] += vi;
            }
        }
    }
}
void specPush(float c) {
    if (!s_sp) return;
    SpecState& p = *s_sp;
    p.ring[p.ringTotal % kSpN] = c; p.ringTotal++;
    if (p.ringTotal < (uint32_t)kSpN) return;
    float mean = 0;
    for (int i = 0; i < kSpN; i++) mean += p.ring[(p.ringTotal + i) % kSpN];
    mean /= kSpN;
    for (int i = 0; i < kSpN; i++) {
        p.re[i] = (p.ring[(p.ringTotal + i) % kSpN] - mean) * (0.5f - 0.5f * cosf(6.2831853f * i / (kSpN - 1)));
        p.im[i] = 0;
    }
    fft128(p.re, p.im);
    uint8_t* row = p.rows[p.rowHead];
    float db[kSpBins], srt[kSpBins];
    for (int b = 0; b < kSpBins; b++) db[b] = 10.0f * log10f(p.re[b] * p.re[b] + p.im[b] * p.im[b] + 1e-12f);
    int ns = 0;
    for (int b = 2; b < kSpBins; b++) { int j = ns++; const float v = db[b]; while (j > 0 && srt[j - 1] > v) { srt[j] = srt[j - 1]; j--; } srt[j] = v; }
    const float med = srt[ns / 2];
    // A flat stretch (the C6 sends live 0 while it calibrates) is silence, not
    // the room's level: as a reference it made every later row read 100 dB
    // over and the box drew solid white (bench 2026-11-17). Dark, and skipped.
    if (med < -90.0f) {
        memset(row, 0, kSpBins); p.peak = -1;
        p.rowHead = (p.rowHead + 1) % kSpRows;
        if (p.rowCount < kSpRows) p.rowCount++;
        return;
    }
    p.ref = p.refN ? p.ref + (med - p.ref) * (p.refN < 90 ? 1.0f / (p.refN + 1) : 0.0033f) : med;
    p.refN++;
    int best = -1; float bestOver = 8.0f;
    for (int b = 0; b < kSpBins; b++) {
        const float over = db[b] - p.ref;
        const float v = (over - 4.0f) * (255.0f / 24.0f);
        row[b] = b < 2 ? 0 : (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
        if (b >= 2 && over > bestOver) { bestOver = over; best = b; }
    }
    p.peak = best;
    p.rowHead = (p.rowHead + 1) % kSpRows;
    if (p.rowCount < kSpRows) p.rowCount++;
}

// ---- The C6's lines -----------------------------------------------------------
// $S,up,score,state,rate,chan,band,rssi,rel,cal,targets,mode,amp,phase,corr,live,dropped,presence
// $T,idx,MMMM,rssi,rate,score,cal,rel      $V,up,c1x1e4,c2x1e4,n,q      # a note
int fields(char* s, char** f, int cap) {
    int n = 0;
    while (s && n < cap) { f[n++] = s; s = strchr(s, ','); if (s) *s++ = 0; }
    return n;
}
void parse(char* l, uint32_t now) {
    char* f[20];
    if (!strncmp(l, "$S,", 3)) {
        const int n = fields(l + 3, f, 20);
        if (n < 17) return;
        SenseReport r = {};
        r.upMs = now;
        r.score = (uint8_t)atoi(f[1]); r.state = (uint8_t)atoi(f[2]); r.rate = (uint16_t)atoi(f[3]);
        r.chan = (uint16_t)atoi(f[4]); r.rssi = (int8_t)atoi(f[6]); r.rel = (uint8_t)atoi(f[7]); r.cal = (uint8_t)atoi(f[8]);
        const uint8_t m = (uint8_t)atoi(f[10]);
        r.mode = m == 2 ? SENSE_MODE_STA : (m == 1 ? SENSE_MODE_PARK : SENSE_MODE_JOINING);
        r.live = (uint16_t)atoi(f[14]); r.drop = (uint32_t)strtoul(f[15], nullptr, 10); r.presence = atoi(f[16]) != 0;
        s_mode = r.mode;
        SenseStore::status(r, now);
        specPush(r.live / 10.0f);
    } else if (!strncmp(l, "$T,", 3)) {
        if (fields(l + 3, f, 8) < 7) return;
        const int i = atoi(f[0]);
        if (i < 0 || i >= SenseStore::PATHS) return;
        SenseStore::path((uint8_t)i, (uint16_t)strtoul(f[1], nullptr, 16), (int8_t)atoi(f[2]), (uint16_t)atoi(f[3]),
                         (uint8_t)atoi(f[4]), (uint8_t)atoi(f[5]), (uint8_t)atoi(f[6]), now);
    } else if (!strncmp(l, "$V,", 3)) {
        if (fields(l + 3, f, 6) < 5 || !s_vitOn) return;
        Vitals::feed(atol(f[1]) / 10000.0f, (uint8_t)atoi(f[4]));
    } else if (l[0] == '#' || l[0] == '$') {
        if (g_senseConsole || l[0] == '#') Serial.printf("[sense] %s\n", l);
    }
}

// 0x01, n, T, then T x u16 LE (amplitude x 8192): one 50 ms frame from the C6.
void frameIn(const uint8_t* b, size_t len) {
    if (!s_cv || !s_vitOn || len < 3) return;
    const int T = b[2];
    if (T <= 0 || T > CsiVitals::kMaxTones || len < (size_t)(3 + 2 * T)) return;
    float x[CsiVitals::kMaxTones];
    for (int i = 0; i < T; i++) x[i] = (float)(b[3 + 2 * i] | (b[4 + 2 * i] << 8)) / 8192.0f;
    float c1, c2; uint8_t q;
    if (s_cv->push(x, T, c1, c2, q)) { s_cvFrames++; Vitals::feed(c1, q); }
}

}  // namespace

// ---- sense_cyd.h ---------------------------------------------------------------
void senseCydStart(const char* ssid, const char* pass) {
    if (s_running) return;
    // Running even when it cannot start, as sense_cyd.cpp does, so the loop
    // does not ask again every pass; the screen says why.
    s_running = true;
    if (!Tab5Radio::senseCapable()) { note("the radio's image has no sensing (\"%s\"): radio update needed", Tab5Radio::tag()); return; }
    if (!s_q) s_q = xQueueCreate(64, sizeof(Line));
    if (!s_sp) s_sp = (SpecState*)calloc(1, sizeof(SpecState));
    if (s_sp) { memset(s_sp, 0, sizeof *s_sp); s_sp->peak = -1; }
    if (!s_q) { note("no memory for the line queue"); return; }
    s_linked = Tab5Radio::senseLink(true, onLine);
    if (!s_linked) { note("the radio would not lend its line slot"); return; }
    s_mode = SENSE_MODE_JOINING;
    s_linesLost = 0;
    strncpy(s_ssid, ssid ? ssid : "", sizeof s_ssid - 1); s_ssid[sizeof s_ssid - 1] = 0;
    Tab5Radio::senseSend("info");
    note("HUMAN SENSING on the Tab5's own radio: heap %lu", (unsigned long)ESP.getFreeHeap());
    if (s_ssid[0]) startJoin(pass);
    else listen("no saved network");
}

void senseCydStop() {
    if (!s_running) return;
    if (s_vitOn) { Tab5Radio::senseSend("vitals off"); Vitals::end(); delete s_cv; s_cv = nullptr; s_vitOn = false; }
    Tab5Radio::senseSend("off");
    stopPing();
    if (s_join == Join::JOINING || s_join == Join::UP) esp_wifi_disconnect();
    s_join = Join::NONE;
    delay(50);   // let "off" leave before the slot goes back
    if (s_linked) Tab5Radio::senseLink(false, nullptr);
    s_linked = false;
    if (s_q) xQueueReset(s_q);
    free(s_sp); s_sp = nullptr;
    s_running = false; s_mode = SENSE_MODE_OFF;
    note("HUMAN SENSING off: %lu lines lost", (unsigned long)s_linesLost);
}

void senseCydTick() {
    if (!s_running || !s_linked) return;
    const uint32_t now = millis();
    joinTick(now);
    Line l;
    for (int k = 0; k < 64 && s_q && xQueueReceive(s_q, &l, 0) == pdTRUE; k++) {
        if ((uint8_t)l.text[0] == 0x01) frameIn((const uint8_t*)l.text, l.len);   // a VITALS frame
        else parse(l.text, now);
    }
    SenseStore::tick(now);
}

bool        senseCydRunning() { return s_running; }
void        senseCydCommand(const char* cmd) { if (s_running && cmd) Tab5Radio::senseSend(cmd); }
// Listening with no network saved still reads as a network: the screen's
// ADD WIFI is for the boards that have a list to add one to.
const char* senseCydNetwork() { return s_ssid[0] ? s_ssid : (s_running ? "LISTENING" : ""); }
uint16_t    senseCydReason()  { return s_join == Join::JOINING ? s_reason : 0; }
uint16_t    senseCydRetryIn() { return (s_join == Join::JOINING && s_retryAt) ? (uint16_t)(((int32_t)(s_retryAt - millis()) + 999) / 1000) : 0; }
uint8_t     senseCydMode()    { return s_mode; }

void senseCydVitals(bool on) {
    if (on && !s_vitOn && s_running && s_linked) {
        if (!s_cv) s_cv = new (std::nothrow) CsiVitals();
        if (!s_cv || !s_cv->begin() || !Vitals::begin(millis())) {
            note("VITALS: no memory"); delete s_cv; s_cv = nullptr; Vitals::end(); return;
        }
        s_vitOn = true; s_cvFrames = 0;
        Tab5Radio::senseSend("vitals frames");
        note("VITALS on: frames from the radio, the 20 s fit on the Tab5");
    } else if (!on && s_vitOn) {
        Tab5Radio::senseSend("vitals off");
        Vitals::end();
        delete s_cv; s_cv = nullptr;   // the window (PSRAM) goes back
        s_vitOn = false;
        note("VITALS off: %lu frames fitted", (unsigned long)s_cvFrames);
    }
}

int            senseCydSpecRows()    { return s_sp ? s_sp->rowCount : 0; }
int            senseCydSpecRowsMax() { return kSpRows; }
const uint8_t* senseCydSpecRow(int r) {
    if (!s_sp || r < 0 || r >= s_sp->rowCount) return nullptr;
    return s_sp->rows[(s_sp->rowHead - 1 - r + kSpRows * 2) % kSpRows];
}
int   senseCydSpecPeak()  { return s_sp ? s_sp->peak : -1; }
float senseCydSpecBinHz() { return 10.0f / kSpN; }
#endif  // SENSE_CYD_APP && TAB5
