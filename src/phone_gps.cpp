// SquachWatch-CYD — the phone's GPS over Bluetooth. See include/phone_gps.h.
#include "phone_gps.h"
#if defined(WAVESHARE_206)
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include "gnss.h"
#include "clock.h"
#include "wardrive.h"

namespace PhoneGps {
namespace {

const char* SVC_UUID  = "9d1f0101-2a4b-4c8e-9b1e-5357574f5441";
const char* LOC_UUID  = "9d1f0102-2a4b-4c8e-9b1e-5357574f5441";
const char* STAT_UUID = "9d1f0103-2a4b-4c8e-9b1e-5357574f5441";

NimBLEServer*         s_server = nullptr;
NimBLECharacteristic* s_loc    = nullptr;
NimBLECharacteristic* s_stat   = nullptr;
bool     s_on = false;
uint32_t s_statAt = 0, s_lastFixMs = 0, s_count = 0;
bool     s_wasConnected = false;

// Written in the NimBLE host task, applied in the loop.
portMUX_TYPE   s_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool  s_have = false;
int32_t  s_lat7 = 0, s_lon7 = 0;
uint16_t s_acc = 0;
int16_t  s_alt = 0;
uint32_t s_epoch = 0;

int32_t  rd32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

class LocCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        const NimBLEAttValue v = c->getValue();
        if (v.length() < 16) return;
        const uint8_t* p = v.data();
        const int32_t la = rd32(p), lo = rd32(p + 4);
        if (la < -900000000 || la > 900000000 || lo < -1800000000 || lo > 1800000000) return;
        portENTER_CRITICAL(&s_mux);
        s_lat7 = la; s_lon7 = lo;
        s_acc = rd16(p + 8); s_alt = (int16_t)rd16(p + 10);
        s_epoch = (uint32_t)rd32(p + 12);
        s_have = true;
        portEXIT_CRITICAL(&s_mux);
    }
} s_locCb;

void advertise() {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->stop();
    NimBLEAdvertisementData d;
    d.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    d.setCompleteServices(NimBLEUUID(SVC_UUID));
    adv->setAdvertisementData(d);
    NimBLEAdvertisementData r;
    r.setName("SquachWatch GPS");
    adv->setScanResponseData(r);
    adv->enableScanResponse(true);
    adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);
    adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
    adv->setMinInterval(160);   // 100 ms
    adv->setMaxInterval(320);
    adv->start();
}

void sendStat(bool notify) {
    if (!s_stat) return;
    char buf[120];
    const char* fn = Wardrive::wsFileName();
    snprintf(buf, sizeof buf, "state=%u;rows=%lu;wifi=%lu;fix=%u;file=%s",
             (unsigned)Wardrive::wsState(), (unsigned long)Wardrive::written(),
             (unsigned long)Wardrive::wsWifiRows(),
             (unsigned)(s_lastFixMs && millis() - s_lastFixMs < 5000), fn);
    s_stat->setValue((const uint8_t*)buf, strlen(buf));
    if (notify) s_stat->notify();
}

}  // namespace

bool registerService() {
    if (s_server) return true;
    s_server = NimBLEDevice::createServer();
    if (!s_server) return false;
    s_server->advertiseOnDisconnect(false);   // tick() restarts it, the mesh's settings undone
    NimBLEService* svc = s_server->createService(SVC_UUID);
    s_loc  = svc->createCharacteristic(LOC_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, 20);
    s_stat = svc->createCharacteristic(STAT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 120);
    s_loc->setCallbacks(&s_locCb);
    svc->start();
    s_server->start();
    sendStat(false);
    Serial.println("[phonegps] service registered");
    return true;
}
bool registered() { return s_server != nullptr; }

void setOn(bool on) {
    if (on == s_on) return;
    s_on = on;
    if (!s_server) { s_on = false; return; }
    if (on) {
        advertise();
        Serial.println("[phonegps] advertising as \"SquachWatch GPS\"");
    } else {
        NimBLEDevice::getAdvertising()->stop();
        for (uint16_t h : s_server->getPeerDevices()) s_server->disconnect(h);
        Serial.println("[phonegps] off");
    }
}
bool on() { return s_on; }
bool owner() { return s_on; }
bool connected() { return s_server && s_server->getConnectedCount() > 0; }
uint32_t lastFixMs() { return s_lastFixMs; }

void tick(uint32_t nowMs) {
    if (!s_on || !s_server) return;
    if (s_have) {
        int32_t la, lo; uint16_t acc; int16_t alt; uint32_t ep;
        portENTER_CRITICAL(&s_mux);
        la = s_lat7; lo = s_lon7; acc = s_acc; alt = s_alt; ep = s_epoch;
        s_have = false;
        portEXIT_CRITICAL(&s_mux);
        // A phone's clock is set from the network: better than this one's.
        const uint32_t mine = Clock::nowEpoch();
        if (ep > 1700000000u && (!mine || (mine > ep ? mine - ep : ep - mine) >= 2)) {
            if (Clock::setEpoch(ep)) Serial.printf("[phonegps] clock set from the phone (was %ld s off)\n",
                                                   mine ? (long)mine - (long)ep : 0L);
        }
        Gnss::phone(la, lo, (uint8_t)(acc < 1 ? 1 : acc > 255 ? 255 : acc), Clock::nowEpoch(), nowMs);
        Gnss::phoneAlt(alt);
        if (!s_lastFixMs || (++s_count % 60) == 0)
            Serial.printf("[phonegps] position %ld,%ld +/-%u m (%lu so far)\n", (long)la, (long)lo, (unsigned)acc,
                          (unsigned long)(s_count ? s_count : 1));
        if (!s_count) s_count = 1;
        s_lastFixMs = nowMs ? nowMs : 1;
    }
    const bool c = connected();
    if (c != s_wasConnected) {
        s_wasConnected = c;
        Serial.printf("[phonegps] phone %s\n", c ? "connected" : "disconnected");
        if (!c) advertise();
    } else if (!c && !NimBLEDevice::getAdvertising()->isAdvertising()) {
        advertise();
    }
    if (c && nowMs - s_statAt >= 2000) { s_statAt = nowMs; sendStat(true); }
}

}  // namespace PhoneGps

#endif  // WAVESHARE_206
