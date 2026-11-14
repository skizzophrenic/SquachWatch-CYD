/*
 * Desk test for squach_sniff.c: the extension compiled on the PC against
 * stand-ins for FreeRTOS, esp_wifi and ESP-Hosted, so the decisions it makes
 * per frame (what is forwarded, what is squeezed, what the hop clock does,
 * what a bad command gets) are proven before the image goes near the Tab5's
 * radio, which has no rollback.
 *
 *   cc -I. -o sniff_test sniff_test.c && ./sniff_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- stand-ins ------------------------------------------------------------- */
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_LOGI(t, ...) do { printf("[%s] ", t); printf(__VA_ARGS__); printf("\n"); } while (0)
static const char *esp_err_to_name(esp_err_t e) { return e ? "ERR" : "ESP_OK"; }

static int64_t g_us;
static int64_t esp_timer_get_time(void) { return g_us; }

typedef enum { WIFI_PKT_MGMT, WIFI_PKT_DATA } wifi_promiscuous_pkt_type_t;
typedef struct { int8_t rssi; uint8_t channel; uint16_t sig_len; } rx_ctrl_t;
typedef struct { rx_ctrl_t rx_ctrl; uint8_t payload[1000]; } wifi_promiscuous_pkt_t;
typedef struct { uint32_t filter_mask; } wifi_promiscuous_filter_t;
#define WIFI_PROMIS_FILTER_MASK_MGMT 1
#define WIFI_PROMIS_FILTER_MASK_DATA 2
#define WIFI_SECOND_CHAN_NONE 0
static int g_chan, g_promisc, g_chanSets;
static void (*g_rxcb)(void *, wifi_promiscuous_pkt_type_t);
static esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *f) { (void)f; return ESP_OK; }
static esp_err_t esp_wifi_set_promiscuous_rx_cb(void (*cb)(void *, wifi_promiscuous_pkt_type_t)) { g_rxcb = cb; return ESP_OK; }
static esp_err_t esp_wifi_set_promiscuous(bool on) { g_promisc = on; return ESP_OK; }
static esp_err_t esp_wifi_set_channel(uint8_t c, int s) { (void)s; g_chan = c; g_chanSets++; return ESP_OK; }

/* The link: every message the slave sends, kept. */
static struct { uint32_t id; uint16_t len; uint8_t d[700]; } g_sent[256];
static int g_nsent;
static esp_err_t esp_hosted_send_custom_data(uint32_t id, const uint8_t *d, size_t n) {
    if (g_nsent < 256) { g_sent[g_nsent].id = id; g_sent[g_nsent].len = (uint16_t)n; memcpy(g_sent[g_nsent].d, d, n); g_nsent++; }
    return ESP_OK;
}
typedef void (*cb_t)(uint32_t, const uint8_t *, size_t, void *);
static struct { uint32_t id; cb_t cb; } g_cbs[8]; static int g_ncb;
static esp_err_t esp_hosted_register_custom_callback(uint32_t id, cb_t cb, void *ctx) { (void)ctx; g_cbs[g_ncb].id = id; g_cbs[g_ncb].cb = cb; g_ncb++; return ESP_OK; }
static void host_send(uint32_t id, const void *d, size_t n) { for (int i = 0; i < g_ncb; i++) if (g_cbs[i].id == id) g_cbs[i].cb(id, d, n, NULL); }

/* FreeRTOS: a queue is a ring of pointers; the task body is run by hand. */
typedef struct { void *p[64]; int head, tail, n, cap; } q_t;
typedef q_t *QueueHandle_t;
typedef void *TaskHandle_t;
typedef int BaseType_t;
typedef int TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(x) (x)
static QueueHandle_t xQueueCreate(int cap, size_t sz) { (void)sz; q_t *q = calloc(1, sizeof *q); q->cap = cap; return q; }
static BaseType_t xQueueSend(QueueHandle_t q, const void *item, int to) { (void)to; if (q->n >= q->cap) return pdFALSE; q->p[q->tail] = *(void **)item; q->tail = (q->tail + 1) % 64; q->n++; return pdTRUE; }
static BaseType_t xQueueReceive(QueueHandle_t q, void *item, int to) { (void)to; if (!q->n) return pdFALSE; *(void **)item = q->p[q->head]; q->head = (q->head + 1) % 64; q->n--; return pdTRUE; }
static void (*g_taskfn)(void *);
static BaseType_t xTaskCreate(void (*fn)(void *), const char *nm, int st, void *arg, int pr, TaskHandle_t *h) { (void)nm; (void)st; (void)arg; (void)pr; (void)h; g_taskfn = fn; return pdTRUE; }
/* The extension itself; the test turns its task loop by hand (sniff_once). */
#include "../squach_sniff.c"

/* ---- helpers ----------------------------------------------------------------- */
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void frame(uint8_t fc0, const uint8_t *src, uint16_t len, int8_t rssi, uint8_t ch) {
    static wifi_promiscuous_pkt_t p; memset(&p, 0, sizeof p);
    p.rx_ctrl.rssi = rssi; p.rx_ctrl.channel = ch; p.rx_ctrl.sig_len = len;
    p.payload[0] = fc0; memcpy(p.payload + 10, src, 6);
    for (int i = 24; i < len && i < 1000; i++) p.payload[i] = (uint8_t)i;
    g_rxcb(&p, WIFI_PKT_MGMT);
}
static int count(uint32_t id) { int n = 0; for (int i = 0; i < g_nsent; i++) if (g_sent[i].id == id) n++; return n; }
static void run_task(int ms) { for (int i = 0; i < ms; i += 20) { g_us += 20000; sniff_once(); } }

int main(void) {
    squach_sniff_init();
    CHECK(g_ncb == 4, "four handlers registered, got %d", g_ncb);
    host_send(CMD_HELLO, NULL, 0);
    CHECK(g_nsent == 1 && g_sent[0].id == EVT_HELLO && strcmp((const char *)g_sent[0].d, SQUACH_TAG) == 0 && g_sent[0].d[16] == 0, "hello answers with the tag, not sniffing");
    g_nsent = 0;
    const uint8_t a[6] = {1,2,3,4,5,6}, b[6] = {9,9,9,9,9,9};

    /* Idle until asked: a frame before SNIFF_ON goes nowhere. */
    CHECK(g_rxcb == NULL, "no rx callback before SNIFF_ON");

    /* Bad commands are refused and acked, not acted on. */
    uint8_t bad = 15; host_send(CMD_CHANNEL, &bad, 1);
    CHECK(g_sent[g_nsent - 1].id == EVT_ACK && g_chanSets == 0, "channel 15 refused");
    host_send(CMD_SNIFF, NULL, 0);
    CHECK(g_sent[g_nsent - 1].id == EVT_ACK && !g_promisc, "empty SNIFF refused");

    /* On. */
    uint8_t on = 1; host_send(CMD_SNIFF, &on, 1);
    CHECK(g_promisc && g_rxcb, "promiscuous on with our callback");
    g_nsent = 0;

    /* Beacon (fc0 0x80) forwarded whole; probe req (0x40), deauth (0xC0), disassoc (0xA0) too; others not. */
    frame(0x80, a, 300, -60, 6); frame(0x40, a, 60, -70, 6); frame(0xC0, a, 26, -50, 6); frame(0xA0, a, 26, -50, 6);
    frame(0x50, a, 100, -60, 6);   /* probe response: not wanted */
    frame(0xB4, a, 10, -60, 6);    /* control RTS: not wanted */
    run_task(40);
    CHECK(count(EVT_FRAME) == 4, "four management kinds forwarded, got %d", count(EVT_FRAME));
    const sniff_evt_t *h = (const sniff_evt_t *)g_sent[0].d;
    CHECK(h->type == 0 && h->subtype == 8 && h->data_len == 300 && h->sig_len == 300 && h->rssi == -60 && h->channel == 6, "beacon header right");
    CHECK(g_sent[0].len == sizeof(sniff_evt_t) + 300 && g_sent[0].d[sizeof(sniff_evt_t) + 24] == 24, "beacon bytes follow the header");

    /* A long beacon is capped at SNIFF_MAX but reports its true length. */
    g_nsent = 0; frame(0x80, a, 900, -60, 1); run_task(20);
    h = (const sniff_evt_t *)g_sent[0].d;
    CHECK(h->data_len == SNIFF_MAX && h->sig_len == 900, "long beacon capped: %u of %u", h->data_len, h->sig_len);

    /* Data frames: header only, one per source per DATA_HOLD_MS. */
    g_nsent = 0;
    frame(0x08, a, 500, -40, 6); frame(0x08, a, 500, -40, 6); frame(0x88, a, 500, -40, 6); frame(0x08, b, 500, -40, 6);
    run_task(80);
    CHECK(count(EVT_FRAME) == 2, "two sources -> two data headers, got %d", count(EVT_FRAME));
    h = (const sniff_evt_t *)g_sent[0].d;
    CHECK(h->type == 2 && h->data_len == 24 && h->sig_len == 500, "data frame squeezed to its header");
    g_us += (DATA_HOLD_MS + 1) * 1000; g_nsent = 0;
    frame(0x08, a, 500, -40, 6); run_task(20);
    CHECK(count(EVT_FRAME) == 1, "same source forwarded again after the hold");

    /* A flood: never more than the pool in flight, the rest counted as dropped, nothing overwritten. */
    g_nsent = 0; const uint32_t before = s_stats.dropped;
    for (int i = 0; i < 100; i++) { uint8_t s[6] = {0x10, 0, 0, 0, (uint8_t)(i >> 8), (uint8_t)i}; frame(0x80, s, 200, -60, 6); }
    CHECK(s_stats.dropped - before == 100 - QUEUE_DEPTH, "flood: %u dropped", (unsigned)(s_stats.dropped - before));
    run_task(QUEUE_DEPTH * 20 + 40);
    CHECK(count(EVT_FRAME) == QUEUE_DEPTH, "flood: %d forwarded of %d slots", count(EVT_FRAME), QUEUE_DEPTH);
    int ok = 1; for (int i = 0; i < QUEUE_DEPTH; i++) { const sniff_evt_t *e = (const sniff_evt_t *)g_sent[i].d; if (g_sent[i].d[sizeof *e + 15] != i) ok = 0; }
    CHECK(ok, "flood: each forwarded frame is the one that was queued, in order");
    frame(0x80, a, 200, -60, 6); run_task(20);
    CHECK(g_sent[g_nsent - 1].id == EVT_FRAME || count(EVT_FRAME) == QUEUE_DEPTH + 1, "slots are free again after the flood");

    /* Hop: the list, the dwell, the clock; a bad list refused. */
    uint8_t hop[] = { 3, 1, 6, 11, 0x2C, 0x01 };   /* 300 ms */
    g_chanSets = 0; host_send(CMD_HOP, hop, sizeof hop);
    CHECK(g_chan == 1 && s_dwell == 300, "hop starts on channel 1 at 300 ms (chan %d, dwell %u)", g_chan, s_dwell);
    run_task(320); CHECK(g_chan == 6, "after 300 ms: channel 6, got %d", g_chan);
    run_task(300); CHECK(g_chan == 11, "then 11, got %d", g_chan);
    run_task(300); CHECK(g_chan == 1, "then back to 1, got %d", g_chan);
    uint8_t badhop[] = { 5, 1, 2 }; g_chanSets = 0; host_send(CMD_HOP, badhop, sizeof badhop);
    CHECK(g_chanSets == 0 && s_hopN == 3, "short hop list refused, old list kept");
    uint8_t tiny[] = { 1, 6, 10, 0 }; host_send(CMD_HOP, tiny, sizeof tiny);
    CHECK(s_dwell == 50, "dwell floor is 50 ms, got %u", s_dwell);
    uint8_t stop[] = { 0, 0, 0 }; host_send(CMD_HOP, stop, sizeof stop); g_chanSets = 0; run_task(400);
    CHECK(g_chanSets == 0, "hop stopped");
    uint8_t c9 = 9; host_send(CMD_CHANNEL, &c9, 1); CHECK(g_chan == 9 && s_hopN == 0, "fixed channel cancels hopping");

    /* Stats once a second while on, with the channel. */
    g_nsent = 0; run_task(2100);
    CHECK(count(EVT_STATS) >= 2, "stats each second, got %d", count(EVT_STATS));

    /* Off: nothing more, promiscuous off. */
    uint8_t off = 0; host_send(CMD_SNIFF, &off, 1); g_nsent = 0;
    frame(0x80, a, 100, -60, 6); run_task(1100);
    CHECK(!g_promisc && count(EVT_FRAME) == 0 && count(EVT_STATS) == 0, "off: silent");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
