// SquachWatch-CYD — RING. See include/tag_ring.h.
#include "tag_ring.h"

#if SQW_RING
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

// Indications on (both standards send one when the sound ends), then the
// start command. True when the command was taken.
static bool ringControlPoint(NimBLEClient* c, const char* svc, const char* chr,
                             const uint8_t* cmd, size_t n, bool& found) {
    NimBLERemoteService* s = c->getService(svc);
    if (!s) return false;
    NimBLERemoteCharacteristic* ch = s->getCharacteristic(chr);
    if (!ch) return false;
    found = true;
    if (ch->canIndicate())      ch->subscribe(false, onIndicate, true);
    else if (ch->canNotify())   ch->subscribe(true,  onIndicate, true);
    s_heard = false;
    return ch->writeValue(cmd, n, true);
}

static Result ringOnce(NimBLEClient* c) {
    bool found = false;
    // AirTag's own sound: one byte, and the tag drops the link when done.
    if (NimBLERemoteService* s = c->getService(AIRTAG_SVC)) {
        if (NimBLERemoteCharacteristic* ch = s->getCharacteristic(AIRTAG_CHR)) {
            found = true;
            const uint8_t play = 0xAF;
            if (ch->writeValue(&play, 1, true)) return Result::RANG;
        }
    }
    if (!c->isConnected()) return found ? Result::REFUSED : Result::NO_ANSWER;
    static const uint8_t DULT_PLAY[]   = { 0x00, 0x03 };
    if (ringControlPoint(c, DULT_SVC, DULT_CHR, DULT_PLAY, sizeof DULT_PLAY, found)) return Result::RANG;
    if (!c->isConnected()) return found ? Result::REFUSED : Result::NO_ANSWER;
    static const uint8_t FINDMY_PLAY[] = { 0x01, 0x00, 0x03 };
    if (ringControlPoint(c, FINDMY_SVC, FINDMY_CHR, FINDMY_PLAY, sizeof FINDMY_PLAY, found)) return Result::RANG;
    return found ? Result::REFUSED : Result::NO_SOUND;
}

static void ringTask(void*) {
    Result r = Result::NO_ANSWER;
    // startUpdateRadio() asks the host to stop the scan; a connect while the
    // scan still runs is refused, so wait for it (up to 2 s).
    NimBLEScan* scan = NimBLEDevice::getScan();
    for (int i = 0; i < 40 && scan && scan->isScanning(); i++) vTaskDelay(pdMS_TO_TICKS(50));

    NimBLEClient* c = NimBLEDevice::createClient();
    if (c) {
        c->setConnectTimeout(5000);
        const uint8_t order[2] = { (uint8_t)s_firstType,
                                   (uint8_t)(s_firstType == BLE_ADDR_RANDOM ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM) };
        for (int k = 0; k < 2; k++) {
            ble_addr_t a;
            a.type = order[k];
            memcpy(a.val, s_mac, 6);
            Serial.printf("[ring] connecting (%s address)\n", a.type == BLE_ADDR_RANDOM ? "random" : "public");
            if (!c->connect(NimBLEAddress(a), true)) {
                Serial.printf("[ring] no connection (error %d)\n", c->getLastError());
                continue;
            }
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
