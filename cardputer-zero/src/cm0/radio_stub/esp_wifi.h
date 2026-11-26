// SquachWatch-CM0 -- the esp_wifi.h stub.
//
// The surface the firmware's real detection.cpp calls on every non-TAB5
// board: init/deinit/start/stop, mode, channel, the promiscuous sniffer
// with the MGMT+DATA filter and its rx callback, and the driver scan.
//
// Phase 1: the definitions in radio_stub.cpp are stateful no-ops -- a
// radio that is "up", sniffs on channel 1, and finds nothing -- so the
// firmware boots and every screen that reads the radio's state reads
// consistent zeros. Phase 2 replaces the definitions with the USB
// adapter's monitor vif (AF_PACKET); the header -- and the firmware --
// do not move.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL 1
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED 0x201
#endif
#ifndef ESP_ERR_INVALID_PARAM
#define ESP_ERR_INVALID_PARAM 0x103
#endif

#ifndef SQW_STUB_ESP_ERR_T
#define SQW_STUB_ESP_ERR_T
typedef int esp_err_t;
#endif

typedef enum { WIFI_MODE_NULL = 0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;

typedef enum { WIFI_SECOND_CHAN_NONE = 0, WIFI_SECOND_CHAN_ONE, WIFI_SECOND_CHAN_TWO, WIFI_SECOND_CHAN_THREE } wifi_second_chan_t;

typedef enum { WIFI_BAND_MODE_AUTO = 0, WIFI_BAND_MODE_ONLY_2GHZ, WIFI_BAND_MODE_ONLY_5GHZ } wifi_band_mode_t;

// The 5 GHz channel table the firmware's boot probe and frame accounting
// use under SQW_WIFI_5G, verbatim from ESP-IDF 5.5's
// esp_wifi_types_generic.h (the C5 build's source), so the CM0 build
// probes and accounts for exactly the channels the C5 build does: bits
// 1-8 -> 36,40,...,64; 9-20 -> 100,104,...,144; 21-28 -> 149,153,...,177
// (every fourth channel, in the three 5 GHz blocks).
typedef enum { WIFI_BAND_2G = 1, WIFI_BAND_5G = 2 } wifi_band_t;

#define CHANNEL_TO_BIT_NUMBER(channel) ((channel >= 1 && channel <= 14) ? (channel) : \
    ((channel >= 36 && channel <= 64 && (channel - 36) % 4 == 0) ? ((channel - 36) / 4 + 1) : \
    ((channel >= 100 && channel <= 144 && (channel - 100) % 4 == 0) ? ((channel - 100) / 4 + 9) : \
    ((channel >= 149 && channel <= 177 && (channel - 149) % 4 == 0) ? ((channel - 149) / 4 + 21) : 0))))

#define BIT_NUMBER_TO_CHANNEL(bit_number, band) ((band == WIFI_BAND_2G) ? (bit_number) : \
    ((bit_number >= 1 && bit_number <= 8) ? ((bit_number - 1) * 4 + 36) : \
    ((bit_number >= 9 && bit_number <= 20) ? ((bit_number - 9) * 4 + 100) : \
    ((bit_number >= 21 && bit_number <= 28) ? ((bit_number - 21) * 4 + 149) : 0))))

typedef enum {
    WIFI_PROMISC_RX_CTL = 0,
    WIFI_PROMISC_RX_MGMT,
    WIFI_PROMISC_RX_DATA,
    WIFI_PROMISC_RX_CTL_PROT,
    WIFI_PROMISC_RX_DATA_PROT,
    WIFI_PROMISC_RX_OTHER_PROT
} wifi_promiscuous_pkt_type_t;

#define WIFI_PROMIS_FILTER_MASK_MGMT  (1u << 0)
#define WIFI_PROMIS_FILTER_MASK_DATA  (1u << 1)
#define WIFI_PROMIS_FILTER_MASK_CTL   (1u << 2)
#define WIFI_PROMIS_FILTER_MASK_ALL   (WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA | WIFI_PROMIS_FILTER_MASK_CTL)

typedef struct { uint32_t filter_mask; } wifi_promiscuous_filter_t;

// The DMA landing zone header the rx callback's buffer points to. Only the
// fields the firmware reads (sig_len, rssi, channel) matter for this build;
// the rest are there so a phase-2 definition can fill the real shape.
typedef struct {
    uint16_t version;
    uint16_t sig_len;
    uint16_t ctrl_len;
    int8_t   rssi;
    int8_t   rssi_lci;
    int8_t   psdu_offset;
    uint8_t  pwrmon_dbm_offset;
    uint8_t  sig_type;
    uint8_t  channel;
    uint8_t  rate_idx;
    uint8_t  data_seq;
    uint8_t  data_type;
    uint8_t  data_status;
    uint8_t  data_phy_type;
    uint8_t  data_phy;
} wifi_rx_ctrl_t;

typedef struct {
    uint8_t*       payload;
    wifi_rx_ctrl_t rx_ctrl;
} wifi_promiscuous_pkt_t;

typedef void (*wifi_promiscuous_rx_cb_t)(void* buffer, wifi_promiscuous_pkt_type_t type);

#define WIFI_SCAN_TYPE_ACTIVE  0
#define WIFI_SCAN_TYPE_PASSIVE 1

typedef struct {
    bool     show_hidden;
    uint8_t  scan_type;
    struct { uint32_t min; uint32_t max; uint32_t passive; } scan_time;
    uint8_t  channel;
    uint8_t  lci;
    uint32_t scan_max_time;
} wifi_scan_config_t;

typedef struct {
    uint8_t  ssid_len;
    char     ssid[33];
    uint8_t  ssid_hidden;
    uint8_t  schan;
    uint8_t  scd;
    uint8_t  second_channel;
    uint8_t  bcn_hopping;
    uint8_t  security;
    int8_t   channel;
    int8_t   primary;
    uint8_t  secondary;
    uint8_t  secondary2;
    uint8_t  bssid[6];
    int8_t   rssi;
    uint32_t timestamp;
    uint32_t localtimestamp;
    uint16_t beacon_interval;
    uint8_t  capability_info[2];
} wifi_ap_record_t;

typedef struct { char cc[3]; uint8_t schan; uint8_t nchan; uint8_t max_txpwr; } wifi_country_t;

typedef struct {
    uint8_t wifi_work_mode;
    uint8_t nvs_enable;
    uint8_t static_rx_buf_num;
    uint8_t dynamic_rx_buf_num;
    uint8_t static_tx_buf_num;
    uint8_t dynamic_tx_buf_num;
    uint8_t tx_buf_type;
    uint8_t cache_tx_buf_num;
    uint8_t ampdu_rx_enable;
    uint8_t ampdu_tx_enable;
    uint8_t amsdu_tx_enable;
    uint8_t csi_enable;
    uint8_t mgmt_sbuf_num;
} wifi_init_config_t;

// The core's own defaults; detection.cpp overwrites the fields it cares
// about the same way it does on the device.
#define WIFI_INIT_CONFIG_DEFAULT() \
    { 0, 0, 10, 32, 0, 32, 1, 2, 1, 1, 0, 0, 20 }

esp_err_t esp_wifi_init(const wifi_init_config_t* config);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_get_mode(wifi_mode_t* mode);
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t secondary);
esp_err_t esp_wifi_get_channel(uint8_t* primary, wifi_second_chan_t* secondary);
esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t mode);
esp_err_t esp_wifi_set_promiscuous(bool enable);
esp_err_t esp_wifi_get_promiscuous(bool* enable);
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t* filter);
esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_cb_t cb);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t* config, bool async);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t* num);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t* num, wifi_ap_record_t* records);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_get_max_tx_power(int8_t* tx_power);
esp_err_t esp_wifi_get_country(wifi_country_t* country);

// ------------------------------------------------------------------
// The freeRTOS critical-section pair the firmware's black-box queue
// runs under (detection.cpp). A no-op, and that is correct only because
// every firmware function runs on the one UI thread: wifi_mon's feed
// thread never calls into the firmware -- it queues frames and the UI
// thread delivers them (cm0::wifiMonPump before each loop()). Anything
// that adds a second thread calling firmware code must revisit this.
typedef struct { int locked; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }
static inline void stubPortCritical(const portMUX_TYPE*) {}
#define portENTER_CRITICAL(m) stubPortCritical(m)
#define portEXIT_CRITICAL(m)  stubPortCritical(m)
