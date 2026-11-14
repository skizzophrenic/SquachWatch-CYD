// SquachWatch-CYD — the Tab5's ESP32-C6 radio over ESP-Hosted. See tab5_radio.h.
//
// What was established on the bench (2026-10-19) and is built on here, not
// re-derived:
//   - The C6 runs ESP-Hosted slave 2.12.13 with tools/tab5-c6/squach_sniff.c
//     on top. It is idle until CMD_SNIFF 1; the stock image boots the same.
//   - The Arduino core's ESP-Hosted host (2.12.13) is built for SDIO slot 1,
//     4-bit, 40 MHz, reset active-high -- right for this unit -- but with
//     the ESP32-P4 function board's pins. The Tab5's are set at runtime,
//     before esp_hosted_init(), through esp_hosted_sdio_set_config().
//   - The link sometimes came up only on a second connect: one retry.
//   - CMD_HELLO must carry a byte: the host library refuses an empty
//     payload (ESP_ERR_INVALID_ARG).
//   - The host keeps room for three custom-message handlers. ACK and FRAME
//     stay registered; HELLO is registered for the handshake and then swapped
//     for STATS.
//   - The C6's power is bit 0 of the output register on the second PI4IOE
//     expander (0x44, SDA 31 / SCL 32), which M5GFX drives: the bit is set
//     through lgfx's own I2C helper on the port M5GFX opened, never by
//     re-initialising the bus, and only after the panel is up.
#if defined(TAB5)
#include "tab5_radio.h"
#include "tab5_panel.h"
#include <Arduino.h>
#include <M5GFX.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_wifi.h>   // the P4's copy is esp_wifi_remote: every call is an RPC to the C6's driver
extern "C" {
#include "esp_hosted.h"
#include "esp_hosted_transport_config.h"
}

namespace {

// ---- The protocol, as squach_sniff.c defines it ---------------------------
const uint32_t CMD_SNIFF   = 0x5100;   // u8 on/off
const uint32_t CMD_CHANNEL = 0x5101;   // u8 1-14
const uint32_t CMD_HOP     = 0x5102;   // u8 n, n channels, u16 dwell ms LE; n = 0 stops
const uint32_t CMD_HELLO   = 0x5103;   // one byte, any
const uint32_t EVT_ACK     = 0x5180;   // u16 cmd, i32 status
const uint32_t EVT_HELLO   = 0x5181;   // char tag[16], u8 sniffing
const uint32_t EVT_FRAME   = 0x5200;   // the packed header below, then the frame bytes
const uint32_t EVT_STATS   = 0x5201;   // u32 seen, forwarded, dropped; u8 channel

struct __attribute__((packed)) WireFrame { int8_t rssi; uint8_t channel, type, subtype; uint16_t sigLen, dataLen; };
struct __attribute__((packed)) WireAck   { uint16_t cmd; int32_t status; };
struct __attribute__((packed)) WireHello { char tag[16]; uint8_t sniffing; };
struct __attribute__((packed)) WireStats { uint32_t seen, forwarded, dropped; uint8_t channel; };

// ---- The Tab5's wiring -------------------------------------------------------
const int SDIO_CLK = 12, SDIO_CMD = 13, SDIO_D0 = 11, SDIO_D1 = 10, SDIO_D2 = 9, SDIO_D3 = 8, C6_RESET = 15;
const int      PI4IO2_PORT = 1;          // M5GFX's in_i2c_port for this board (I2C_NUM_1)
const uint8_t  PI4IO2_ADDR = 0x44;
const uint8_t  PI4IO2_OUT  = 0x05;
const uint8_t  WLAN_PWR_EN = 1u << 0;
const uint32_t PI4IO2_FREQ = 100000;

// ---- State -------------------------------------------------------------------
bool              s_up = false;
QueueHandle_t     s_q  = nullptr;
const UBaseType_t Q_DEPTH = 32;         // 32 x 608 B: a burst of beacons on a busy channel
char              s_tag[17] = "";
volatile uint32_t s_helloAt = 0;
volatile uint8_t  s_helloSniffing = 0;
Tab5Radio::Stats  s_stats = {};
// Filled on the RPC receive task, which is the only caller of onFrame: a
// static rather than a 608-byte local on that task's stack.
Tab5Radio::Frame  s_in;

// ---- Receive side: the RPC task. Copy and queue, never parse. ---------------
void onFrame(uint32_t, const uint8_t* d, size_t n, void*) {
    if (!s_q || n < sizeof(WireFrame)) return;
    WireFrame h;
    memcpy(&h, d, sizeof h);
    const size_t have = n - sizeof(WireFrame);
    uint16_t take = h.dataLen;
    if (take > have) take = (uint16_t)have;
    if (take > Tab5Radio::FRAME_MAX) take = Tab5Radio::FRAME_MAX;
    s_in.rssi = h.rssi; s_in.channel = h.channel; s_in.type = h.type; s_in.subtype = h.subtype;
    s_in.sigLen = h.sigLen; s_in.dataLen = take;
    memcpy(s_in.data, d + sizeof(WireFrame), take);
    if (xQueueSend(s_q, &s_in, 0) == pdTRUE) s_stats.received++;
    else s_stats.queueFull++;
}

void onAck(uint32_t, const uint8_t* d, size_t n, void*) {
    if (n < sizeof(WireAck)) return;
    WireAck a;
    memcpy(&a, d, sizeof a);
    s_stats.acks++;
    s_stats.lastAckCmd = a.cmd;
    s_stats.lastAckStatus = a.status;
}

void onHello(uint32_t, const uint8_t* d, size_t n, void*) {
    if (n < sizeof(WireHello)) return;
    WireHello h;
    memcpy(&h, d, sizeof h);
    memcpy(s_tag, h.tag, 16);
    s_tag[16] = 0;
    s_helloSniffing = h.sniffing;
    s_helloAt = millis() ? millis() : 1;
}

void onStats(uint32_t, const uint8_t* d, size_t n, void*) {
    if (n < sizeof(WireStats)) return;
    WireStats s;
    memcpy(&s, d, sizeof s);
    s_stats.seen = s.seen; s_stats.forwarded = s.forwarded; s_stats.dropped = s.dropped;
    s_stats.channel = s.channel;
    s_stats.statsAt = millis() ? millis() : 1;
}

// ---- The stall detector ------------------------------------------------------
// Every send is an RPC that waits for the C6's reply, from the loop task, so
// the loop's heartbeat (next() is called every pass) and where it last was
// are kept where a task of our own can read them. That task says so on the
// console when the heartbeat stops, and once a minute what the sends cost:
// the first product run on the bench (2026-10-19) went silent at 71 s with
// nothing printed, and this is how the next one says where.
volatile uint32_t    s_loopBeat = 0;
volatile const char* s_where    = "idle";
volatile uint32_t    s_sends = 0, s_sendUsLast = 0, s_sendUsMax = 0, s_sendFails = 0;

void diagTask(void*) {
    uint32_t lastBeat = 0, stalledFor = 0, minuteAt = millis();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        const uint32_t beat = s_loopBeat;
        if (beat == lastBeat && s_up) {
            stalledFor += 5;
            Serial.printf("[c6] LOOP STALLED %lu s: last at %s; sends %lu (fails %lu, last %lu us, max %lu us); rx %lu, qfull %lu, pending %u; heap %lu\n",
                          (unsigned long)stalledFor, (const char*)s_where, (unsigned long)s_sends, (unsigned long)s_sendFails,
                          (unsigned long)s_sendUsLast, (unsigned long)s_sendUsMax, (unsigned long)s_stats.received,
                          (unsigned long)s_stats.queueFull, (unsigned)(s_q ? uxQueueMessagesWaiting(s_q) : 0), (unsigned long)ESP.getFreeHeap());
        } else stalledFor = 0;
        lastBeat = beat;
        if (millis() - minuteAt >= 60000) {
            minuteAt = millis();
            Serial.printf("[c6] link: sends %lu (fails %lu, last %lu us, max %lu us); c6 seen %lu fwd %lu drop %lu ch %u; rx %lu qfull %lu\n",
                          (unsigned long)s_sends, (unsigned long)s_sendFails, (unsigned long)s_sendUsLast, (unsigned long)s_sendUsMax,
                          (unsigned long)s_stats.seen, (unsigned long)s_stats.forwarded, (unsigned long)s_stats.dropped,
                          (unsigned)s_stats.channel, (unsigned long)s_stats.received, (unsigned long)s_stats.queueFull);
        }
    }
}

bool send(uint32_t id, const void* p, size_t n) {
    if (!s_up) return false;
    static char where[24];
    snprintf(where, sizeof where, "send %04lx", (unsigned long)id);
    s_where = where;
    const uint32_t t0 = micros();
    const esp_err_t e = esp_hosted_send_custom_data(id, (const uint8_t*)p, n);
    const uint32_t us = micros() - t0;
    s_sends++;
    s_sendUsLast = us;
    if (us > s_sendUsMax) s_sendUsMax = us;
    s_where = "idle";
    if (e != ESP_OK) {
        s_sendFails++;
        Serial.printf("[c6] send %04lx (%u B) refused: %s\n", (unsigned long)id, (unsigned)n, esp_err_to_name(e));
    }
    return e == ESP_OK;
}

bool powerOn() {
    // The expander's output register, bit 0. M5GFX's own init already wrote
    // the register with this bit set; this makes the fact explicit and
    // catches an expander that is not answering.
    const auto r = lgfx::i2c::bitOn(PI4IO2_PORT, PI4IO2_ADDR, PI4IO2_OUT, WLAN_PWR_EN, PI4IO2_FREQ);
    if (!r.has_value()) {
        Serial.println("[c6] the radio's power bit did not take (expander 0x44 not answering)");
        return false;
    }
    return true;
}

}  // namespace

namespace Tab5Radio {

bool begin() {
    if (s_up) return true;
    if (!Tab5Panel::up()) {
        Serial.println("[c6] the panel is not up, so the expander bus is not: no radio");
        return false;
    }
    if (!s_q) s_q = xQueueCreate(Q_DEPTH, sizeof(Frame));
    if (!s_q) { Serial.println("[c6] no room for the frame queue"); return false; }

    Serial.println("[c6] powering the radio");
    if (!powerOn()) return false;
    delay(1500);   // the C6's own boot into the ESP-Hosted slave before the link asks for it

    struct esp_hosted_sdio_config cfg = INIT_DEFAULT_HOST_SDIO_CONFIG();
    cfg.pin_clk.pin = SDIO_CLK; cfg.pin_cmd.pin = SDIO_CMD;
    cfg.pin_d0.pin = SDIO_D0; cfg.pin_d1.pin = SDIO_D1; cfg.pin_d2.pin = SDIO_D2; cfg.pin_d3.pin = SDIO_D3;
    cfg.pin_reset.pin = C6_RESET;
    Serial.printf("[c6] sdio slot %u, %u-bit, %lu kHz, clk %d cmd %d d0-3 %d %d %d %d, reset %d\n",
                  (unsigned)cfg.slot, (unsigned)cfg.bus_width, (unsigned long)cfg.clock_freq_khz,
                  cfg.pin_clk.pin, cfg.pin_cmd.pin, cfg.pin_d0.pin, cfg.pin_d1.pin, cfg.pin_d2.pin, cfg.pin_d3.pin, cfg.pin_reset.pin);
    esp_err_t e = (esp_err_t)esp_hosted_sdio_set_config(&cfg);
    if (e != ESP_OK && e != ESP_ERR_NOT_ALLOWED) {   // NOT_ALLOWED: already set by an earlier attempt
        Serial.printf("[c6] sdio config refused: %s\n", esp_err_to_name(e));
        return false;
    }
    e = esp_hosted_init();
    if (e != ESP_OK) {
        Serial.printf("[c6] esp_hosted_init failed: %s\n", esp_err_to_name(e));
        return false;
    }
    // The link. Once, and once more after a breath: on the bench it came up
    // on the second try now and then.
    for (uint8_t attempt = 1; attempt <= 2; attempt++) {
        const uint32_t t0 = millis();
        e = esp_hosted_connect_to_slave();
        Serial.printf("[c6] connect attempt %u: %s (%lu ms)\n", (unsigned)attempt, esp_err_to_name(e), (unsigned long)(millis() - t0));
        if (e == ESP_OK) break;
        if (attempt == 1) delay(3000);
    }
    if (e != ESP_OK) {
        Serial.println("[c6] the link did not come up; the radio is off this boot");
        return false;
    }
    {
        esp_hosted_coprocessor_fwver_t v = {};
        if (esp_hosted_get_coprocessor_fwversion(&v) == ESP_OK)
            Serial.printf("[c6] esp-hosted slave %lu.%lu.%lu\n", (unsigned long)v.major1, (unsigned long)v.minor1, (unsigned long)v.patch1);
    }
    s_up = true;

    esp_hosted_register_custom_callback(EVT_ACK,   onAck,   nullptr);
    esp_hosted_register_custom_callback(EVT_FRAME, onFrame, nullptr);
    e = esp_hosted_register_custom_callback(EVT_HELLO, onHello, nullptr);
    if (e != ESP_OK) Serial.printf("[c6] hello handler refused: %s\n", esp_err_to_name(e));

    // Is our extension in the image? One byte, because an empty payload is
    // refused by the host library before it ever leaves.
    s_helloAt = 0;
    const uint8_t q = '?';
    if (!send(CMD_HELLO, &q, 1)) { s_up = false; return false; }
    const uint32_t t0 = millis();
    while (!s_helloAt && millis() - t0 < 2000) delay(10);
    if (!s_helloAt) {
        Serial.println("[c6] no answer to hello: the C6 runs a stock ESP-Hosted image, not the sniff one");
        s_up = false;
        return false;
    }
    Serial.printf("[c6] hello: \"%s\", sniffing %u, %lu ms\n", s_tag, (unsigned)s_helloSniffing, (unsigned long)(s_helloAt - t0));
    // The C6's WiFi driver, which the sniff extension rides on, starts only
    // when the host asks (CMD_SNIFF answered ESP_ERR_WIFI_NOT_INIT before
    // this, 2026-10-19). On the P4 esp_wifi_* is esp_wifi_remote: each call
    // is an RPC to the C6, so this is the slave's driver being initialised
    // in station mode and started, the way the Arduino core's own
    // WiFi.mode(WIFI_STA) does it, without the core's network layer.
    {
        esp_err_t r = esp_netif_init();
        if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) Serial.printf("[c6] esp_netif_init: %s\n", esp_err_to_name(r));
        r = esp_event_loop_create_default();
        if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) Serial.printf("[c6] event loop: %s\n", esp_err_to_name(r));
        wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
        wcfg.nvs_enable = 0;   // as the core does over ESP-Hosted
        const esp_err_t ei = esp_wifi_init(&wcfg);
        const esp_err_t es = (ei == ESP_OK) ? esp_wifi_set_storage(WIFI_STORAGE_RAM) : ei;
        const esp_err_t em = (ei == ESP_OK) ? esp_wifi_set_mode(WIFI_MODE_STA) : ei;
        const esp_err_t et = (em == ESP_OK) ? esp_wifi_start() : em;
        Serial.printf("[c6] wifi driver on the C6: init %s, storage %s, mode %s, start %s\n",
                      esp_err_to_name(ei), esp_err_to_name(es), esp_err_to_name(em), esp_err_to_name(et));
        if (et != ESP_OK) Serial.println("[c6] the C6's WiFi driver did not start; sniffing will be refused");
    }
    // The handshake is done: its handler's slot goes to the stats.
    if (esp_hosted_register_custom_callback(EVT_HELLO, nullptr, nullptr) == ESP_OK) {
        e = esp_hosted_register_custom_callback(EVT_STATS, onStats, nullptr);
        if (e != ESP_OK) Serial.printf("[c6] stats handler refused: %s (no counters, frames still flow)\n", esp_err_to_name(e));
    } else {
        Serial.println("[c6] hello handler would not go; no stats handler (frames still flow)");
    }
    xTaskCreate(diagTask, "c6diag", 3072, nullptr, 1, nullptr);
    return true;
}

bool up() { return s_up; }
const char* tag() { return s_tag; }

bool bleBegin() {
    // The C6's Bluetooth controller, over the link begin() brought up. Its
    // HCI rides the same SDIO transport as the WiFi RPCs; the NimBLE host on
    // the P4 (the Arduino core's, built with ESP-Hosted's VHCI transport)
    // talks to it once this returns. The same two calls the core's own BLE
    // library makes in hostedInitBLE().
    if (!s_up) {
        Serial.println("[c6] no link, so no Bluetooth controller");
        return false;
    }
    esp_err_t e = esp_hosted_bt_controller_init();
    if (e != ESP_OK) {
        Serial.printf("[c6] bluetooth controller init failed: %s\n", esp_err_to_name(e));
        return false;
    }
    e = esp_hosted_bt_controller_enable();
    if (e != ESP_OK) {
        Serial.printf("[c6] bluetooth controller enable failed: %s\n", esp_err_to_name(e));
        return false;
    }
    Serial.println("[c6] bluetooth controller up");
    return true;
}

bool sniff(bool on) {
    const uint8_t v = on ? 1 : 0;
    return send(CMD_SNIFF, &v, 1);
}

bool setChannel(uint8_t ch) {
    if (ch < 1 || ch > 14) return false;
    return send(CMD_CHANNEL, &ch, 1);
}

bool hop(const uint8_t* chans, uint8_t n, uint16_t dwellMs) {
    if (n > 14) return false;
    uint8_t msg[1 + 14 + 2];
    msg[0] = n;
    for (uint8_t i = 0; i < n; i++) msg[1 + i] = chans[i];
    msg[1 + n] = (uint8_t)(dwellMs & 0xFF);
    msg[2 + n] = (uint8_t)(dwellMs >> 8);
    return send(CMD_HOP, msg, (size_t)(3 + n));
}

bool hopStop() { return hop(nullptr, 0, 250); }

bool next(Frame& out) {
    s_loopBeat++;   // the loop's heartbeat, for the stall detector
    if (!s_q) return false;
    return xQueueReceive(s_q, &out, 0) == pdTRUE;
}

uint16_t pending() { return s_q ? (uint16_t)uxQueueMessagesWaiting(s_q) : 0; }

Stats stats() { return s_stats; }

}  // namespace Tab5Radio
#endif  // TAB5
