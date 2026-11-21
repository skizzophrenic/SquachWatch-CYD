// SquachWatch-CYD — firmware updates over Bluetooth. See include/ota_ble.h.
#include "ota_ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include "security.h"
#include "detection.h"   // DetectionEngine::bleUp

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#ifndef SQW_ENV
#define SQW_ENV "unknown"
#endif

using OtaCore::Fail;

#if CONFIG_BT_NIMBLE_ROLE_PERIPHERAL   // nimconfig.h defines it 1 or 0, never leaves it undefined

namespace OtaBle {
namespace {

// Random-looking and ours. The browser filters its device picker on the
// service, so nothing else in range is ever offered.
const char* SVC_UUID  = "9d1f0001-2a4b-4c8e-9b1e-5357574f5441";
const char* INFO_UUID = "9d1f0002-2a4b-4c8e-9b1e-5357574f5441";
const char* CTRL_UUID = "9d1f0003-2a4b-4c8e-9b1e-5357574f5441";
const char* DATA_UUID = "9d1f0004-2a4b-4c8e-9b1e-5357574f5441";

const uint32_t RECEIVE_TIMEOUT_MS = 20000;
const uint8_t  CODE_TRIES = 3;

enum class Pending : uint8_t { NONE, BEGIN, END, FAIL };

// Written from the NimBLE host task (core 0) and read from the loop (core 1),
// one field at a time. The flash itself is behind OtaCore's own lock.
volatile State    s_state       = State::OFF;
volatile Pending  s_pending     = Pending::NONE;
volatile Fail     s_fail        = Fail::NONE;
volatile bool     s_connected   = false;
volatile bool     s_codeOk      = false;
volatile bool     s_readvertise = false;
volatile uint32_t s_size        = 0;
volatile uint32_t s_rx          = 0;
volatile uint32_t s_lastDataMs  = 0;
volatile uint16_t s_connHandle  = 0xFFFF;
// Transfer telemetry, printed from the loop every two seconds.
volatile uint32_t s_dataWrites = 0, s_dataIgnored = 0, s_syncs = 0;
volatile uint32_t s_hostStackLow = 0xFFFFFFFF;
uint32_t          s_lastReport = 0;
uint8_t           s_triesLeft  = CODE_TRIES;
uint32_t          s_code       = 0;
uint8_t           s_sig[80];
uint8_t           s_sigLen     = 0;
uint32_t          s_doneAt     = 0;

NimBLEServer*         s_server = nullptr;
NimBLECharacteristic* s_info   = nullptr;
NimBLECharacteristic* s_ctrl   = nullptr;
NimBLECharacteristic* s_data   = nullptr;

char s_name[20]     = "SquachWatch";
char s_infoBuf[128] = "";

uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void put32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

void notifyCtrl(const uint8_t* b, size_t n) {
    if (s_ctrl && s_connected) s_ctrl->notify(b, n, s_connHandle);   // 2.x: the third argument is the connection, not "is a notification"
}

void sendAck() {
    uint8_t b[5] = { REPLY_ACK };
    put32(b + 1, s_rx);
    notifyCtrl(b, sizeof b);
}

// Safe from either task: it only records the failure. The loop does the
// cleanup, because aborting the flash handle has to wait for a write in
// progress on the other core.
void requestFail(Fail f) {
    const State s = s_state;
    if (s == State::OFF || s == State::FAILED || s == State::DONE) return;
    s_fail    = f;
    s_state   = State::FAILED;
    s_pending = Pending::FAIL;
}

// Every GAP event, after NimBLE has handled it. Only here for the log: the
// server callbacks are not told WHY a link dropped, and the reason is the
// whole question when a transfer dies before its first byte.
int gapListener(ble_gap_event* ev, void*) {
    switch (ev->type) {
        case BLE_GAP_EVENT_DISCONNECT:
            Serial.printf("[ota] link dropped: reason 0x%02x (%d)\n", (unsigned)ev->disconnect.reason, ev->disconnect.reason);
            break;
        case BLE_GAP_EVENT_MTU:
            Serial.printf("[ota] mtu now %u\n", (unsigned)ev->mtu.value);
            break;
        case BLE_GAP_EVENT_CONN_UPDATE: {
            ble_gap_conn_desc d;
            if (ble_gap_conn_find(ev->conn_update.conn_handle, &d) == 0)
                Serial.printf("[ota] connection updated: status %d, interval %.1f ms\n", ev->conn_update.status, d.conn_itvl * 1.25f);
            break;
        }
        default: break;
    }
    return 0;
}

void startAdvertising() {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->stop();
    NimBLEAdvertisementData d;
    d.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    d.setCompleteServices(NimBLEUUID(SVC_UUID));
    adv->setAdvertisementData(d);
    NimBLEAdvertisementData r;
    r.setName(s_name);
    adv->setScanResponseData(r);
    adv->enableScanResponse(true);
    adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);
    adv->setMinInterval(48);   // 30 ms: quick to find while somebody is looking
    adv->setMaxInterval(96);
    adv->start();
}

class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo& d) override {
        s_connHandle = d.getConnHandle();
        s_connected  = true;
        s_codeOk     = false;
        s_triesLeft  = CODE_TRIES;
        if (s_state == State::WAITING) s_state = State::CONNECTED;
        Serial.printf("[ota] connected: interval %.1f ms, latency %u, timeout %u ms\n",
                      d.getConnInterval() * 1.25f, (unsigned)d.getConnLatency(),
                      (unsigned)d.getConnTimeout() * 10u);
        // No connection-parameter request here. Asking for a faster interval
        // while the browser is still discovering services hung Windows'
        // Bluetooth stack on the bench; it is asked for once sending starts.
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
        s_connected  = false;
        s_connHandle = 0xFFFF;
        s_codeOk     = false;
        const State st = s_state;
        if (st == State::RECEIVING) requestFail(Fail::LOST_CONNECTION);
        else if (st == State::CONNECTED) { s_state = State::WAITING; s_readvertise = true; }
    }
} s_serverCb;

class CtrlCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        const NimBLEAttValue v = c->getValue();
        const uint8_t* p = v.data();
        const size_t   n = v.length();
        if (!n) return;
        Serial.printf("[ota] CTRL write: cmd %u, %u bytes\n", (unsigned)p[0], (unsigned)n);
        switch (p[0]) {
            case CMD_HELLO: {
                if (n < 5 || s_state != State::CONNECTED) return;
                if (get32(p + 1) == s_code) {
                    s_codeOk = true;
                    const uint8_t r = REPLY_HELLO_OK;
                    notifyCtrl(&r, 1);
                } else {
                    if (s_triesLeft) s_triesLeft--;
                    const uint8_t r[2] = { REPLY_BAD_CODE, s_triesLeft };
                    notifyCtrl(r, 2);
                    if (!s_triesLeft) requestFail(Fail::BAD_CODE);
                }
                break;
            }
            case CMD_BEGIN: {
                if (!s_codeOk || s_state != State::CONNECTED || n < 6) return;
                const uint8_t sl = p[5];
                if (sl == 0 || sl > sizeof s_sig || n < 6u + sl) { requestFail(Fail::BAD_SIGNATURE); return; }
                memcpy(s_sig, p + 6, sl);
                s_sigLen  = sl;
                s_size    = get32(p + 1);
                s_pending = Pending::BEGIN;
                break;
            }
            case CMD_SYNC:
                s_syncs++;
                if (s_state == State::RECEIVING) sendAck();
                break;
            case CMD_END:
                if (s_state == State::RECEIVING) s_pending = Pending::END;
                break;
            case CMD_ABORT:
                requestFail(Fail::CANCELLED);
                break;
            default: break;
        }
    }
} s_ctrlCb;

class DataCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        if (s_state != State::RECEIVING) return;
        const NimBLEAttValue v = c->getValue();
        const size_t n = v.length();
        if (n < 5) return;
        const uint8_t* p   = v.data();
        const uint32_t off = get32(p);
        if (s_dataWrites == 0) Serial.printf("[ota] first data write: %u bytes at %lu\n", (unsigned)n, (unsigned long)off);
        s_dataWrites++;
        const uint32_t hw = uxTaskGetStackHighWaterMark(nullptr);
        if (hw < s_hostStackLow) s_hostStackLow = hw;
        // Out of order means a chunk went missing in between. Ignore
        // everything until the browser's next SYNC tells it where to resume.
        if (off != s_rx) { s_dataIgnored++; return; }
        if (!OtaCore::write(p + 4, n - 4)) {
            requestFail(off == 0 ? Fail::NOT_FIRMWARE : Fail::WRITE_ERROR);
            return;
        }
        s_rx += n - 4;
        s_lastDataMs = millis();
    }
} s_dataCb;

void doBegin() {
    // Erases exactly the sectors the image needs, so the writes that follow in
    // the BLE callback are writes only. Takes a few seconds.
    const Fail f = OtaCore::begin(s_size, s_sig, s_sigLen);
    if (f != Fail::NONE) { requestFail(f); return; }
    s_rx         = 0;
    s_lastDataMs = millis();
    s_state      = State::RECEIVING;

    // Two requests for a faster link, both made only now that discovery is
    // long over (asking during discovery hung Windows' stack). Either may be
    // refused, and the transfer works at whatever the phone or PC allows.
    //
    // Bigger radio packets: 251 bytes per packet instead of the 27-byte
    // default, so a 512-byte chunk is three packets instead of twenty.
    // Shorter interval: 7.5-15 ms between packets instead of the 30-60 ms a
    // phone picks on its own. (An earlier version blamed this request for
    // dropped links. It was innocent -- the drop was the timeout below.)
    ble_gap_set_data_len(s_connHandle, 251, 2120);
    s_server->updateConnParams(s_connHandle, 6, 12, 0, 400);

    // The largest chunk one write-without-response can carry on this link:
    // ATT MTU less the 3-byte ATT header and our 4-byte offset.
    uint16_t mtu   = s_server->getPeerMTU(s_connHandle);
    uint16_t chunk = mtu > 27 ? (uint16_t)(mtu - 7) : 20;
    if (chunk > 508) chunk = 508;
    Serial.printf("[ota] ready: peer mtu %u, chunk %u\n", (unsigned)mtu, (unsigned)chunk);
    const uint8_t r[3] = { REPLY_READY, (uint8_t)(chunk & 0xFF), (uint8_t)(chunk >> 8) };
    notifyCtrl(r, sizeof r);
}

void doEnd() {
    if (s_rx != s_size) { requestFail(Fail::DAMAGED); return; }
    s_state = State::VERIFYING;
    const Fail f = OtaCore::finish();
    if (f != Fail::NONE) { requestFail(f); return; }
    s_state  = State::DONE;
    s_doneAt = millis();
    const uint8_t r = REPLY_DONE;
    notifyCtrl(&r, 1);
}

}  // namespace

bool available() { return OtaCore::available(); }

bool begin() {
    if (s_state != State::OFF) return true;
    s_fail = Fail::NONE;
    if (!available())       { s_fail = Fail::WRITE_ERROR; return false; }
    if (Security::locked()) { s_fail = Fail::LOCKED;      return false; }
    if (!DetectionEngine::bleUp()) { s_fail = Fail::RADIO_BUSY; return false; }   // a boot without the stack

    // Detection asked the host task to stop scanning. NimBLE will not register
    // a GATT server while a scan runs, so wait for that to land.
    NimBLEScan* scan = NimBLEDevice::getScan();
    const uint32_t t0 = millis();
    while (scan && scan->isScanning() && millis() - t0 < 2000) delay(10);
    if (scan && scan->isScanning()) { s_fail = Fail::RADIO_BUSY; return false; }

    if (!s_server) {
        NimBLEDevice::setMTU(517);
        s_server = NimBLEDevice::createServer();
        NimBLEDevice::setCustomGapHandler(gapListener);
        s_server->setCallbacks(&s_serverCb, false);
        s_server->advertiseOnDisconnect(false);
        NimBLEService* svc = s_server->createService(SVC_UUID);
        s_info = svc->createCharacteristic(INFO_UUID, NIMBLE_PROPERTY::READ, 128);
        s_ctrl = svc->createCharacteristic(CTRL_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY, 96);
        s_data = svc->createCharacteristic(DATA_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, 512);
        s_ctrl->setCallbacks(&s_ctrlCb);
        s_data->setCallbacks(&s_dataCb);
        svc->start();
        s_server->start();

        const NimBLEAddress a = NimBLEDevice::getAddress();
        const uint8_t* m = a.getBase()->val;
        snprintf(s_name, sizeof s_name, "SquachWatch-%02X%02X", m[1], m[0]);
    }

    snprintf(s_infoBuf, sizeof s_infoBuf, "env=%s;ver=%s;slot=%s;max=%lu;proto=1",
             SQW_ENV, FIRMWARE_VERSION, OtaCore::runningSlot(), (unsigned long)OtaCore::maxImageSize());
    s_info->setValue((const uint8_t*)s_infoBuf, strlen(s_infoBuf));

    s_code      = 100000 + (esp_random() % 900000);
    // On the serial console too. USB access already means full control of the
    // board, and a bench script cannot read the screen.
    Serial.printf("[ota] pairing code %lu\n", (unsigned long)s_code);
    s_pending   = Pending::NONE;
    s_codeOk    = false;
    s_size      = 0;
    s_rx        = 0;
    s_triesLeft = CODE_TRIES;
    s_state     = State::WAITING;
    startAdvertising();
    Serial.printf("[ota] bluetooth update mode: advertising as %s, heap %lu\n", s_name,
                  (unsigned long)ESP.getFreeHeap());
    return true;
}

void end() {
    if (s_state == State::OFF || s_state == State::DONE) return;
    s_state   = State::OFF;
    OtaCore::abort();
    s_pending = Pending::NONE;
    NimBLEDevice::getAdvertising()->stop();
    if (s_connected && s_server) s_server->disconnect(s_connHandle);
    s_codeOk = false;
    Serial.println("[ota] bluetooth update mode off");
}

void tick(uint32_t now) {
    if (s_state == State::OFF) return;

    if (s_readvertise) {
        s_readvertise = false;
        if (s_state == State::WAITING) startAdvertising();
    }

    const Pending p = s_pending;
    s_pending = Pending::NONE;
    if (p == Pending::FAIL) {
        OtaCore::abort();
        const char* w = OtaCore::failWords(s_fail);
        uint8_t buf[2 + 96];
        buf[0] = REPLY_FAIL;
        buf[1] = (uint8_t)s_fail;
        size_t wl = strlen(w);
        if (wl > 96) wl = 96;
        memcpy(buf + 2, w, wl);
        notifyCtrl(buf, 2 + wl);
        Serial.printf("[ota] stopped: %s  (rx %lu writes %lu ignored %lu syncs %lu)\n", w,
                      (unsigned long)s_rx, (unsigned long)s_dataWrites,
                      (unsigned long)s_dataIgnored, (unsigned long)s_syncs);
    } else if (p == Pending::BEGIN) {
        doBegin();
    } else if (p == Pending::END) {
        doEnd();
    }

    // Measured against the clock NOW, not the loop's `now`. doBegin() above
    // stamps s_lastDataMs with millis() a few milliseconds after the loop
    // captured `now`, so `now - s_lastDataMs` wrapped to four billion and the
    // transfer was declared lost in the same tick it was declared ready --
    // before the browser had sent a byte. Every Bluetooth update ever tried
    // on a real board died right here.
    if (s_state == State::RECEIVING && (int32_t)(millis() - s_lastDataMs) > (int32_t)RECEIVE_TIMEOUT_MS)
        requestFail(Fail::LOST_CONNECTION);

    if ((s_state == State::RECEIVING || s_state == State::CONNECTED) && now - s_lastReport >= 2000) {
        s_lastReport = now;
        Serial.printf("[ota] %s rx %lu/%lu  writes %lu ignored %lu syncs %lu  host stack free %lu  heap %lu\n",
                      s_state == State::RECEIVING ? "receiving" : "connected",
                      (unsigned long)s_rx, (unsigned long)s_size, (unsigned long)s_dataWrites,
                      (unsigned long)s_dataIgnored, (unsigned long)s_syncs,
                      (unsigned long)(s_hostStackLow == 0xFFFFFFFF ? 0 : s_hostStackLow),
                      (unsigned long)ESP.getFreeHeap());
    }

    if (s_state == State::DONE && !OtaCore::restartPending() && now - s_doneAt > 2500) {
        if (s_connected && s_server) s_server->disconnect(s_connHandle);
        OtaCore::restartSoon(500);
    }
}

State       state()         { return s_state; }
bool        codeAccepted()  { return s_codeOk; }
uint32_t    bytesReceived() { return s_rx; }
uint32_t    bytesExpected() { return s_size; }
uint8_t     percent()       { return s_size ? (uint8_t)((uint64_t)s_rx * 100 / s_size) : 0; }
const char* failureText()   { return OtaCore::failWords(s_fail); }
const char* deviceName()    { return s_name; }
uint32_t    pairingCode()   { return s_code; }

}  // namespace OtaBle

#else  // no Bluetooth server compiled in: the CYDs, see nimble_flags_cyd in platformio.ini

namespace OtaBle {
bool        available()     { return false; }
bool        begin()         { return false; }
void        end()           {}
void        tick(uint32_t)  {}
State       state()         { return State::OFF; }
bool        codeAccepted()  { return false; }
uint8_t     percent()       { return 0; }
uint32_t    bytesReceived() { return 0; }
uint32_t    bytesExpected() { return 0; }
const char* failureText()   { return ""; }
const char* deviceName()    { return ""; }
uint32_t    pairingCode()   { return 0; }
}  // namespace OtaBle

#endif
