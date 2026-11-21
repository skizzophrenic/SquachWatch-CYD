// SquachWatch-CYD -- the fake flood. See include/flood_bench.h.
#include "flood_bench.h"
#if FLOOD_BENCH

#include <NimBLEDevice.h>
#include <Arduino.h>
#if defined(CONFIG_NIMBLE_CPP_IDF)
#include "nimble/nimble_port.h"
#else
#include "nimble/porting/nimble/include/nimble/nimble_port.h"
#endif
#include <string.h>
#include "detection.h"   // DetectionEngine::bleUp

// The host's own entry for an advertising report, the one the HCI parser
// calls: it sanity-checks the payload and hands the report to whatever
// scan is running, exactly as a real advert is. Declared here rather than
// through the host's private header.
extern "C" void ble_gap_rx_adv_report(struct ble_gap_disc_desc* desc);

static volatile uint16_t s_perSec    = 0;
// 50 ms slots: the loop task counts them due, the host task counts them
// paid. One writer each, so neither has to read-modify-write the other's.
static volatile uint32_t s_slotsDue  = 0;
static volatile uint32_t s_slotsPaid = 0;
static struct ble_npl_event s_ev;
static bool     s_evReady = false;
static uint32_t s_made    = 0;

// Runs on the host task: the adverts of every slot due and not yet paid.
static void burstOnHost(struct ble_npl_event*) {
    // At most four slots (200 ms) at once. A real radio delivers one advert
    // at a time with the reply timer running between them; a burst that
    // pays a second of backlog in one go makes hundreds of records before
    // the timer can free one, and the heap goes before the radio would
    // ever have been asked. What is not paid is dropped, and the rate the
    // board reports (adv, ble/s on the frame line) is the truth.
    const uint32_t due = s_slotsDue;
    uint32_t slots = due - s_slotsPaid;
    if (slots > 4) slots = 4;
    s_slotsPaid = due;
    const uint16_t n = (uint16_t)(((s_perSec + 19) / 20) * slots);
    for (uint16_t i = 0; i < n; i++) {
        // Flags, Tile's 16-bit service UUID (0xFEED) so the detector counts
        // every one, then a short name, from a fresh random address. The
        // detection count on the frame line is how the bench proves an
        // advert was handled at first sight rather than waiting on a reply.
        uint8_t data[18] = { 0x02, 0x01, 0x06, 0x03, 0x03, 0xED, 0xFE,
                             0x0A, 0x09, 'F','L','O','O','D','0','0','0','0' };
        const uint32_t k = s_made++;
        data[14] = (uint8_t)('A' + (k >> 12 & 15)); data[15] = (uint8_t)('A' + (k >> 8 & 15));
        data[16] = (uint8_t)('A' + (k >> 4 & 15));  data[17] = (uint8_t)('A' + (k & 15));
        ble_gap_disc_desc desc;
        memset(&desc, 0, sizeof desc);
        desc.event_type  = BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;   // connectable and scannable: the kind a scanner waits on
        desc.length_data = sizeof data;
        desc.data        = data;
        desc.rssi        = (int8_t)(-55 - (int8_t)(k % 30));
        desc.addr.type   = BLE_ADDR_RANDOM;
        for (int b = 0; b < 6; b++) desc.addr.val[b] = (uint8_t)esp_random();
        desc.addr.val[5] |= 0xC0;                             // a static random address
        ble_gap_rx_adv_report(&desc);
    }
}

void floodSet(uint16_t perSecond) {
    s_perSec = perSecond;
    Serial.printf("[flood] %u fake adverts a second, connectable, never answering\n", (unsigned)perSecond);
}

void floodTick() {
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (!s_perSec) { last = now; return; }
    const uint32_t owed = (now - last) / 50;
    if (!owed) return;
    last += owed * 50;
    s_slotsDue += owed;
    if (!DetectionEngine::bleUp()) return;   // a boot without the stack
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (!scan || !scan->isScanning()) return;   // nothing to hand them to; the host drops the backlog
    // A post while the last one is still queued is ignored by NimBLE's port;
    // the burst pays every slot due when it does run, so nothing is lost.
    if (!s_evReady) { ble_npl_event_init(&s_ev, burstOnHost, nullptr); s_evReady = true; }
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_ev);
}

#endif
