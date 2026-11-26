// SquachWatch-CYD — RING. See include/tag_ring.h.
#include "tag_ring.h"

#if SQW_RING && defined(ESP_PLATFORM)
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include "detection.h"

namespace TagRing {

// ---- the latest trackers heard, with their address types ----
// Written on the Bluetooth host task, read on the main loop.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
struct Seen { uint8_t mac[6]; uint8_t type; bool conn; uint32_t at; };
static Seen    s_seen[8];
static uint8_t s_seenN = 0;

void noteSeen(const uint8_t mac[6], uint8_t addrType, bool connectable) {
    const uint32_t now = millis();
    portENTER_CRITICAL(&s_mux);
    int slot = -1;
    for (int i = 0; i < s_seenN; i++)
        if (memcmp(s_seen[i].mac, mac, 6) == 0) { slot = i; break; }
    if (slot < 0) {
        if (s_seenN < 8) slot = s_seenN++;
        else {
            slot = 0;
            for (int i = 1; i < 8; i++) if (s_seen[i].at < s_seen[slot].at) slot = i;
        }
        memcpy(s_seen[slot].mac, mac, 6);
    }
    s_seen[slot].type = addrType;
    s_seen[slot].conn = connectable;
    s_seen[slot].at   = now;
    portEXIT_CRITICAL(&s_mux);
}

bool latest(uint8_t mac[6]) {
    bool got = false;
    uint32_t best = 0;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < s_seenN; i++)
        if (!got || (int32_t)(s_seen[i].at - best) > 0) { memcpy(mac, s_seen[i].mac, 6); best = s_seen[i].at; got = true; }
    portEXIT_CRITICAL(&s_mux);
    return got;
}

// -1 when this address was never heard. conn and ageMs describe its last
// advert: whether it took connections, and how long ago it was.
static int typeOf(const uint8_t mac[6], bool& conn, uint32_t& ageMs) {
    int t = -1;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < s_seenN; i++)
        if (memcmp(s_seen[i].mac, mac, 6) == 0) {
            t = s_seen[i].type; conn = s_seen[i].conn; ageMs = millis() - s_seen[i].at; break;
        }
    portEXIT_CRITICAL(&s_mux);
    return t;
}

// ---- the ring itself ----
static const char* AIRTAG_SVC = "7DFC9000-7D1C-4951-86AA-8D9728F8D66C";
static const char* AIRTAG_CHR = "7DFC9001-7D1C-4951-86AA-8D9728F8D66C";
static const char* DULT_SVC   = "15190001-12F4-C226-88ED-2AC5579F2A85";
static const char* DULT_CHR   = "8E0C0001-1D68-FB92-BF61-48377421680E";
static const char* FINDMY_SVC = "FD44";
static const char* FINDMY_CHR = "4F860003-943B-49EF-BED4-2F730304427A";

static volatile bool   s_busy     = false;   // a ring is under way (main loop's view)
static volatile bool   s_done     = false;   // the task has finished; poll() collects
static volatile Result s_result   = Result::NONE;
static uint8_t         s_mac[6];
static int             s_firstType = BLE_ADDR_RANDOM;
static TaskHandle_t    s_task     = nullptr;
static volatile bool   s_heard    = false;   // an indication came back (DULT / Find My)

static void onIndicate(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool) { s_heard = true; }

// Writes the command the way the characteristic says it takes writes: with a
// reply when it offers that, else without one -- a phone app picks the same
// way on its own, and a write-with-reply to a no-reply characteristic is
// refused. Logs what the tracker said.
static bool sendCommand(NimBLEClient* c, NimBLERemoteCharacteristic* ch, const char* what,
                        const uint8_t* cmd, size_t n) {
    const bool reply = ch->canWrite() || !ch->canWriteNoResponse();
    Serial.printf("[ring] %s: write%s%s%s%s, sending %s a reply\n", what,
                  ch->canWrite() ? " +reply" : "", ch->canWriteNoResponse() ? " +noreply" : "",
                  ch->canIndicate() ? " +indicate" : "", ch->canNotify() ? " +notify" : "",
                  reply ? "with" : "without");
    const bool ok = ch->writeValue(cmd, n, reply);
    if (!ok) Serial.printf("[ring] %s: refused, error %d (0x%x)\n", what, c->getLastError(), c->getLastError());
    else     Serial.printf("[ring] %s: taken\n", what);
    return ok;
}

// Indications on (both standards send one when the sound ends), then the
// start command. True when the command was taken.
static bool ringControlPoint(NimBLEClient* c, const char* svc, const char* chr, const char* what,
                             const uint8_t* cmd, size_t n, bool& found) {
    NimBLERemoteService* s = c->getService(svc);
    if (!s) { Serial.printf("[ring] %s: no such service\n", what); return false; }
    NimBLERemoteCharacteristic* ch = s->getCharacteristic(chr);
    if (!ch) { Serial.printf("[ring] %s: service, but no control point\n", what); return false; }
    found = true;
    bool sub = true;
    if (ch->canIndicate())      sub = ch->subscribe(false, onIndicate, true);
    else if (ch->canNotify())   sub = ch->subscribe(true,  onIndicate, true);
    if (!sub) Serial.printf("[ring] %s: indications refused, error %d\n", what, c->getLastError());
    s_heard = false;
    return sendCommand(c, ch, what, cmd, n);
}

static Result ringOnce(NimBLEClient* c) {
    bool found = false;
    // AirTag's own sound: one byte, and the tag drops the link when done.
    if (NimBLERemoteService* s = c->getService(AIRTAG_SVC)) {
        if (NimBLERemoteCharacteristic* ch = s->getCharacteristic(AIRTAG_CHR)) {
            found = true;
            const uint8_t play = 0xAF;
            if (sendCommand(c, ch, "AirTag", &play, 1)) return Result::RANG;
        } else Serial.println("[ring] AirTag: service, but no sound characteristic");
    } else Serial.println("[ring] AirTag: no such service");
    if (!c->isConnected()) return found ? Result::REFUSED : Result::NO_ANSWER;
    static const uint8_t DULT_PLAY[]   = { 0x00, 0x03 };
    if (ringControlPoint(c, DULT_SVC, DULT_CHR, "DULT", DULT_PLAY, sizeof DULT_PLAY, found)) return Result::RANG;
    if (!c->isConnected()) return found ? Result::REFUSED : Result::NO_ANSWER;
    static const uint8_t FINDMY_PLAY[] = { 0x01, 0x00, 0x03 };
    if (ringControlPoint(c, FINDMY_SVC, FINDMY_CHR, "Find My", FINDMY_PLAY, sizeof FINDMY_PLAY, found)) return Result::RANG;
    return found ? Result::REFUSED : Result::NO_SOUND;
}

// What the radio said at each step, for the console. The connect itself
// only returns "timed out"; these carry the controller's own reasons.
class RingCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* c) override { Serial.printf("[ring] link up, handle %u\n", c->getConnHandle()); }
    void onConnectFail(NimBLEClient*, int reason) override {
        Serial.printf("[ring] link failed, reason %d (0x%x)\n", reason, reason);
    }
    void onDisconnect(NimBLEClient*, int reason) override {
        Serial.printf("[ring] hung up, reason %d (0x%x)\n", reason, reason);
    }
};
static RingCallbacks s_cb;

// The reason, on the console, if the Bluetooth host ever resets itself.
static void (*s_oldReset)(int) = nullptr;
static void onHostReset(int reason) {
    Serial.printf("[ring] Bluetooth host reset, reason %d (0x%x)\n", reason, reason);
    if (s_oldReset) s_oldReset(reason);
}
static void hookOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    s_oldReset = ble_hs_cfg.reset_cb;
    ble_hs_cfg.reset_cb = onHostReset;
}

static void radioState(const char* when) {
    Serial.printf("[ring] %s: scanning %d, advertising %d, connecting %d\n", when,
                  ble_gap_disc_active(), ble_gap_adv_active(), ble_gap_conn_active());
}

static void ringTask(void*) {
    Result r = Result::NO_ANSWER;
    // startUpdateRadio() asks the host to stop the scan; a connect while the
    // scan still runs is refused, so wait for it (up to 2 s).
    NimBLEScan* scan = NimBLEDevice::getScan();
    for (int i = 0; i < 40 && scan && scan->isScanning(); i++) vTaskDelay(pdMS_TO_TICKS(50));
    radioState("before");

    NimBLEClient* c = NimBLEDevice::createClient();
    if (c) {
        c->setClientCallbacks(&s_cb, false);
        c->setConnectTimeout(6000);
        // 0x3e, "connection failed to be established": the tracker heard the
        // request but the link never settled. Common at a distance; NimBLE
        // retries it itself, two times by default.
        c->setConnectRetries(5);
        const uint8_t order[2] = { (uint8_t)s_firstType,
                                   (uint8_t)(s_firstType == BLE_ADDR_RANDOM ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM) };
        for (int k = 0; k < 2; k++) {
            ble_addr_t a;
            a.type = order[k];
            // s_mac is in PRINTED order, most significant byte first: the scan
            // flips every address that way for display (detection.cpp, issue
            // #22). The radio wants NimBLE's own order, least significant
            // first. Handing it the printed bytes dialed an address nobody
            // had, and every ring timed out (2026-11-19, found on the bench
            // against a C5 on its Bluetooth update screen).
            for (int i = 0; i < 6; i++) a.val[i] = s_mac[5 - i];
            Serial.printf("[ring] try %d: connecting to %02x:%02x:%02x:%02x:%02x:%02x (%s address)\n", k + 1,
                          s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5],
                          a.type == BLE_ADDR_RANDOM ? "random" : "public");
            const uint32_t t0 = millis();
            if (!c->connect(NimBLEAddress(a), true)) {
                Serial.printf("[ring] no connection (error %d) after %lu ms\n", c->getLastError(),
                              (unsigned long)(millis() - t0));
                radioState("after");
                continue;
            }
            Serial.printf("[ring] connected in %lu ms\n", (unsigned long)(millis() - t0));
            r = ringOnce(c);
            Serial.printf("[ring] %s\n", title(r));
            if (r == Result::RANG && c->isConnected()) {
                // DULT and Find My say when the sound is over; give them up to
                // 8 s, then hang up. AirTag hangs up by itself.
                for (int i = 0; i < 80 && c->isConnected() && !s_heard; i++) vTaskDelay(pdMS_TO_TICKS(100));
            }
            if (c->isConnected()) c->disconnect();
            for (int i = 0; i < 20 && c->isConnected(); i++) vTaskDelay(pdMS_TO_TICKS(50));
            break;
        }
        NimBLEDevice::deleteClient(c);
    }
    s_result = r;
    s_done   = true;
    s_task   = nullptr;
    vTaskDelete(nullptr);
}

// ---- RING NAME, for the bench ----
static char          s_findText[24];
static volatile bool s_findArmed = false;
static volatile bool s_found     = false;
static uint8_t       s_foundMac[6];
static volatile uint32_t s_findAll = 0, s_findConn = 0;   // adverts heard while armed
static volatile bool     s_findNoConn = false;

void armFind(const char* text) {
    strncpy(s_findText, text, sizeof s_findText - 1);
    s_findText[sizeof s_findText - 1] = 0;
    s_found = false;
    s_findAll = s_findConn = 0;
    s_findNoConn = false;
    s_findArmed = s_findText[0] != 0;
}

bool findWanted() { return s_findArmed; }


// Meshtastic's service UUID 6ba1b218-15a8-461f-9fa8-5dcae273eafd, as it sits
// in an advert (least significant byte first). Its name is often only in the
// scan response, which a passive scan never asks for.
static const uint8_t MESHTASTIC_UUID[16] = { 0xfd, 0xea, 0x73, 0xe2, 0xca, 0x5d, 0xa8, 0x9f,
                                             0x1f, 0x46, 0xa8, 0x15, 0x18, 0xb2, 0xa1, 0x6b };

void noteNamed(const uint8_t mac[6], uint8_t addrType, const char* name, bool connectable,
               const uint8_t* pl, size_t plLen) {
    if (!s_findArmed) return;
    s_findAll++;
    if (connectable) s_findConn++;
    bool hit = false;
    if (name && name[0]) {
        // Case-blind "contains".
        const size_t n = strlen(s_findText), m = strlen(name);
        for (size_t i = 0; !hit && i + n <= m; i++) hit = strncasecmp(name + i, s_findText, n) == 0;
    }
    if (!hit && pl && strcasecmp(s_findText, "meshtastic") == 0)
        for (size_t i = 0; !hit && i + 16 <= plLen; i++) hit = memcmp(pl + i, MESHTASTIC_UUID, 16) == 0;
    // Another SquachWatch on its Bluetooth update screen (ota_ble.cpp's
    // service, 9d1f0001-2a4b-4c8e-9b1e-5357574f5441).
    static const uint8_t SQW_OTA_UUID[16] = { 0x41, 0x54, 0x4f, 0x57, 0x57, 0x53, 0x1e, 0x9b,
                                              0x8e, 0x4c, 0x4b, 0x2a, 0x01, 0x00, 0x1f, 0x9d };
    if (!hit && pl && strcasecmp(s_findText, "squachwatch") == 0)
        for (size_t i = 0; !hit && i + 16 <= plLen; i++) hit = memcmp(pl + i, SQW_OTA_UUID, 16) == 0;
    if (!hit) return;
    if (!connectable) {
        if (!s_findNoConn) Serial.println("[ring] heard it, but its adverts say it takes no connections");
        s_findNoConn = true;
        return;
    }
    if (!name || !name[0]) name = "(found by its service)";
    noteSeen(mac, addrType, true);
    memcpy(s_foundMac, mac, 6);
    s_findArmed = false;
    s_found = true;
    Serial.printf("[ring] found \"%s\" (%s address)\n", name, addrType == BLE_ADDR_RANDOM ? "random" : "public");
}

bool takeFound(uint8_t mac[6]) {
    static uint32_t lastNote = 0;
    if (s_findArmed && millis() - lastNote > 5000) {
        lastNote = millis();
        Serial.printf("[ring] still looking: %lu adverts heard, %lu of them take connections\n",
                      (unsigned long)s_findAll, (unsigned long)s_findConn);
    }
    if (!s_found) return false;
    s_found = false;
    memcpy(mac, s_foundMac, 6);
    return true;
}

bool busy() { return s_busy; }

bool start(DetectionEngine& eng, const uint8_t mac[6]) {
    if (s_busy || !NimBLEDevice::isInitialized()) return false;
    memcpy(s_mac, mac, 6);
    bool conn = true;
    uint32_t age = 0;
    const int t = typeOf(mac, conn, age);
    s_firstType = t < 0 ? BLE_ADDR_RANDOM : t;   // trackers use random addresses
    if (t < 0) Serial.println("[ring] not heard since boot: it may have a new address by now");
    else Serial.printf("[ring] last heard %lu s ago, %s address, %s\n", (unsigned long)(age / 1000),
                       t == BLE_ADDR_RANDOM ? "random" : "public",
                       conn ? "taking connections" : "NOT taking connections (adverts only)");
    hookOnce();
    s_done   = false;
    s_result = Result::NONE;
    s_busy   = true;
    eng.startUpdateRadio();   // scan and the squad's advert off while connected
    // 6 KB: the client's calls block in this task, not on the host task.
    if (xTaskCreate(ringTask, "ring", 6144, nullptr, 2, &s_task) != pdPASS) {
        s_task = nullptr;
        eng.stopUpdateRadio();
        s_busy = false;
        return false;
    }
    return true;
}

Result poll(DetectionEngine& eng) {
    if (!s_busy || !s_done) return Result::NONE;
    eng.stopUpdateRadio();
    s_busy = false;
    s_done = false;
    return s_result;
}

}  // namespace TagRing
#endif  // SQW_RING on a board

// ---- the emulator's RING: no radio to connect with, so the tracker always
// answers. A tap shows the same toast the board does, a moment later.
#if SQW_RING && !defined(ESP_PLATFORM)
#include <Arduino.h>
#include <string.h>

namespace TagRing {

static bool     s_busy = false;
static uint32_t s_at   = 0;

void noteSeen(const uint8_t*, uint8_t, bool) {}
bool latest(uint8_t*) { return false; }
bool busy() { return s_busy; }
bool start(DetectionEngine&, const uint8_t*) {
    if (s_busy) return false;
    s_busy = true;
    s_at   = millis();
    return true;
}
Result poll(DetectionEngine&) {
    if (!s_busy || millis() - s_at < 900) return Result::NONE;
    s_busy = false;
    return Result::RANG;
}
void armFind(const char*) {}
bool findWanted() { return false; }
void noteNamed(const uint8_t*, uint8_t, const char*, bool, const uint8_t*, size_t) {}
bool takeFound(uint8_t*) { return false; }

}  // namespace TagRing
#endif  // SQW_RING in the emulator

#if SQW_RING
namespace TagRing {

const char* title(Result r) {
    switch (r) {
        case Result::RANG:      return "RINGING";
        case Result::NO_ANSWER: return "NO ANSWER";
        case Result::NO_SOUND:  return "NO SOUND";
        case Result::REFUSED:   return "IT SAID NO";
        default:                return "";
    }
}

const char* detail(Result r) {
    switch (r) {
        case Result::RANG:      return "Listen for a beep";
        case Result::NO_ANSWER: return "Near its owner, or gone";
        case Result::NO_SOUND:  return "Can't ring this one";
        case Result::REFUSED:   return "Near its owner?";
        default:                return "";
    }
}

}  // namespace TagRing
#endif  // SQW_RING
