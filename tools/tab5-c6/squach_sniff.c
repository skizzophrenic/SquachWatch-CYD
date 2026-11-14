/*
 * SquachWatch sniff extension for the ESP-Hosted co-processor (ESP32-C6).
 *
 * The M5Stack Tab5's radio is an ESP32-C6 behind an SDIO link; stock
 * ESP-Hosted has no promiscuous mode. This file rides on the stock slave
 * unchanged: it registers a few custom-message handlers (ESP-Hosted's
 * CustomRpc, "peer data transfer") and does nothing at all until the host
 * asks. Booting is identical to the stock image -- that is the point: a
 * crash at boot would brick the radio (no rollback on the C6).
 *
 * What it forwards, once SNIFF_ON:
 *   - management frames of the kinds the detector reads (beacon, probe
 *     request, deauthentication, disassociation), whole, up to SNIFF_MAX
 *     bytes, so the P4 can parse SSIDs, Remote ID elements, a pwnagotchi's
 *     payload and RSN elements exactly as every other board does;
 *   - data frames as their 24-byte header only, and at most one per source
 *     address per DATA_HOLD ms, which is all the detector wants from them
 *     (two addresses to look up) -- a busy channel would otherwise swamp
 *     the link with other people's traffic.
 *   - channel hopping is done here, on the host's timing (CMD_HOP), so a
 *     slow host never parks the radio on one channel.
 *
 * Message IDs are our own; r4d10n's wifi_raw used 0x0100-0x0200, we start
 * at 0x5100 so the two can never be confused.
 */
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_hosted_peer_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "squach";

/* Host -> slave */
#define CMD_SNIFF       0x5100   /* u8 on/off */
#define CMD_CHANNEL     0x5101   /* u8 primary */
#define CMD_HOP         0x5102   /* u8 channel list length, then channels; u16 dwell ms. 0 length = stop hopping */
#define CMD_HELLO       0x5103   /* no payload: is the extension in this image? */
/* Slave -> host */
#define EVT_ACK         0x5180   /* u16 cmd, i32 status */
#define EVT_HELLO       0x5181   /* the tag below, plus u8 sniffing */
#define SQUACH_TAG      "squach-sniff 1"
#define EVT_FRAME       0x5200   /* sniff_evt_t + frame bytes */
#define EVT_STATS       0x5201   /* every second while sniffing */

#define SNIFF_MAX       600      /* bytes of a management frame forwarded */
#define DATA_HOLD_MS    2000     /* one data-frame header per source per this long */
#define DATA_SLOTS      64       /* recent data sources remembered */
#define QUEUE_DEPTH     24

typedef struct __attribute__((packed)) {
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  type;       /* 0 mgmt, 2 data */
    uint8_t  subtype;
    uint16_t sig_len;    /* the frame's full length on air */
    uint16_t data_len;   /* bytes that follow */
} sniff_evt_t;

typedef struct __attribute__((packed)) {
    uint32_t seen, forwarded, dropped;   /* frames since sniffing started */
    uint8_t  channel;
} sniff_stats_t;

typedef struct { uint8_t mac[6]; uint32_t at; } data_slot_t;

static QueueHandle_t  s_q, s_free;     /* items to send; items to fill */
static TaskHandle_t   s_task;
static bool           s_on;
static sniff_stats_t  s_stats;
static data_slot_t    s_slots[DATA_SLOTS];
static uint8_t        s_hop[14], s_hopN, s_hopI;
static uint16_t       s_dwell = 250;
static uint32_t       s_hopAt;

typedef struct { sniff_evt_t h; uint8_t b[SNIFF_MAX]; } item_t;

static void ack(uint16_t cmd, int32_t status) {
    struct __attribute__((packed)) { uint16_t cmd; int32_t status; } a = { cmd, status };
    esp_hosted_send_custom_data(EVT_ACK, (const uint8_t *)&a, sizeof a);
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* The data-frame squeeze: true if this source was forwarded recently. */
static bool data_seen(const uint8_t *mac) {
    const uint32_t now = now_ms();
    int oldest = 0;
    for (int i = 0; i < DATA_SLOTS; i++) {
        if (memcmp(s_slots[i].mac, mac, 6) == 0) {
            if (now - s_slots[i].at < DATA_HOLD_MS) return true;
            s_slots[i].at = now; return false;
        }
        if (s_slots[i].at < s_slots[oldest].at) oldest = i;
    }
    memcpy(s_slots[oldest].mac, mac, 6); s_slots[oldest].at = now;
    return false;
}

/* In the WiFi task: decide fast, copy little, hand off. */
static void rx_cb(void *buf, wifi_promiscuous_pkt_type_t kind) {
    if (!s_on) return;
    const wifi_promiscuous_pkt_t *p = (const wifi_promiscuous_pkt_t *)buf;
    const uint16_t len = p->rx_ctrl.sig_len;
    s_stats.seen++;
    if (len < 24) return;
    const uint8_t fc0 = p->payload[0];
    const uint8_t type = (fc0 & 0x0C) >> 2, sub = (fc0 & 0xF0) >> 4;
    uint16_t take;
    if (type == 0) {
        if (!(sub == 8 || sub == 4 || sub == 12 || sub == 10)) return;   /* beacon, probe req, deauth, disassoc */
        take = len > SNIFF_MAX ? SNIFF_MAX : len;
    } else if (type == 2) {
        /* Both addresses matter to the detector, but one hold per source is
         * plenty: a device that keeps talking is seen again in two seconds. */
        if (data_seen(p->payload + 10)) return;
        take = 24;
    } else return;
    /* A slot from the free list, or the frame is dropped: a slot is never
     * written while the link task still holds it. */
    item_t *it;
    if (xQueueReceive(s_free, &it, 0) != pdTRUE) { s_stats.dropped++; return; }
    it->h.rssi = p->rx_ctrl.rssi; it->h.channel = p->rx_ctrl.channel;
    it->h.type = type; it->h.subtype = sub; it->h.sig_len = len; it->h.data_len = take;
    memcpy(it->b, p->payload, take);
    xQueueSend(s_q, &it, 0);                     /* cannot fail: one slot per free entry */
}

/* Our own task: the link, and the hop clock. Never in the WiFi task. One
 * pass is its own function so the desk test can turn the loop by hand. */
static void sniff_once(void) {
    static uint32_t statsAt;
    {
        /* Wait for one, then drain whatever else arrived: a burst goes out
         * in one go rather than one frame per turn of the hop clock. */
        item_t *it;
        TickType_t wait = pdMS_TO_TICKS(20);
        while (xQueueReceive(s_q, &it, wait) == pdTRUE) {
            wait = 0;
            if (esp_hosted_send_custom_data(EVT_FRAME, (const uint8_t *)it, sizeof(sniff_evt_t) + it->h.data_len) == ESP_OK)
                s_stats.forwarded++;
            else s_stats.dropped++;
            xQueueSend(s_free, &it, 0);
        }
        const uint32_t now = now_ms();
        if (s_on && s_hopN && now - s_hopAt >= s_dwell) {
            s_hopAt = now;
            s_hopI = (uint8_t)((s_hopI + 1) % s_hopN);
            esp_wifi_set_channel(s_hop[s_hopI], WIFI_SECOND_CHAN_NONE);
            s_stats.channel = s_hop[s_hopI];
        }
        if (s_on && now - statsAt >= 1000) {
            statsAt = now;
            esp_hosted_send_custom_data(EVT_STATS, (const uint8_t *)&s_stats, sizeof s_stats);
        }
    }
}
static void sniff_task(void *arg) { (void)arg; for (;;) sniff_once(); }

static void on_sniff(uint32_t id, const uint8_t *d, size_t n, void *ctx) {
    if (n < 1) { ack(CMD_SNIFF, ESP_ERR_INVALID_ARG); return; }
    esp_err_t e = ESP_OK;
    if (d[0]) {
        memset(&s_stats, 0, sizeof s_stats); memset(s_slots, 0, sizeof s_slots);
        wifi_promiscuous_filter_t f = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
        esp_wifi_set_promiscuous_filter(&f);
        esp_wifi_set_promiscuous_rx_cb(rx_cb);
        e = esp_wifi_set_promiscuous(true);
        s_on = (e == ESP_OK);
    } else {
        s_on = false;
        e = esp_wifi_set_promiscuous(false);
    }
    ESP_LOGI(TAG, "sniff %s: %s", d[0] ? "on" : "off", esp_err_to_name(e));
    ack(CMD_SNIFF, e);
}

static void on_channel(uint32_t id, const uint8_t *d, size_t n, void *ctx) {
    if (n < 1 || d[0] < 1 || d[0] > 14) { ack(CMD_CHANNEL, ESP_ERR_INVALID_ARG); return; }
    s_hopN = 0;
    esp_err_t e = esp_wifi_set_channel(d[0], WIFI_SECOND_CHAN_NONE);
    s_stats.channel = d[0];
    ack(CMD_CHANNEL, e);
}

static void on_hop(uint32_t id, const uint8_t *d, size_t n, void *ctx) {
    if (n < 1 || d[0] > 14 || n < (size_t)(1 + d[0] + 2)) { ack(CMD_HOP, ESP_ERR_INVALID_ARG); return; }
    s_hopN = d[0];
    memcpy(s_hop, d + 1, s_hopN);
    s_dwell = (uint16_t)(d[1 + s_hopN] | (d[2 + s_hopN] << 8));
    if (s_dwell < 50) s_dwell = 50;
    s_hopI = 0; s_hopAt = now_ms();
    if (s_hopN) { esp_wifi_set_channel(s_hop[0], WIFI_SECOND_CHAN_NONE); s_stats.channel = s_hop[0]; }
    ESP_LOGI(TAG, "hop %u channels, %u ms", s_hopN, s_dwell);
    ack(CMD_HOP, ESP_OK);
}

static void on_hello(uint32_t id, const uint8_t *d, size_t n, void *ctx) {
    struct __attribute__((packed)) { char tag[16]; uint8_t on; } h;
    memset(&h, 0, sizeof h); strncpy(h.tag, SQUACH_TAG, sizeof h.tag - 1); h.on = s_on;
    esp_hosted_send_custom_data(EVT_HELLO, (const uint8_t *)&h, sizeof h);
}

void squach_sniff_init(void) {
    static item_t pool[QUEUE_DEPTH];
    s_q    = xQueueCreate(QUEUE_DEPTH, sizeof(item_t *));
    s_free = xQueueCreate(QUEUE_DEPTH, sizeof(item_t *));
    for (int i = 0; i < QUEUE_DEPTH; i++) { item_t *it = &pool[i]; xQueueSend(s_free, &it, 0); }
    xTaskCreate(sniff_task, "squach", 4096, NULL, 5, &s_task);
    esp_hosted_register_custom_callback(CMD_SNIFF,   on_sniff,   NULL);
    esp_hosted_register_custom_callback(CMD_CHANNEL, on_channel, NULL);
    esp_hosted_register_custom_callback(CMD_HOP,     on_hop,     NULL);
    esp_hosted_register_custom_callback(CMD_HELLO,   on_hello,   NULL);
    ESP_LOGI(TAG, "sniff extension ready (idle until asked)");
}
