// wichat_hook.cpp — WiChat background mesh service
#include "wichat_hook.h"
#include <esp_wifi.h>
#include <Arduino.h>

static wichat_rx_hook_t s_hook = nullptr;
static wichat_rx_hook_t s_captureHook = nullptr;
static uint8_t s_pinnedCh = 0;

void wichat_set_rx_hook(wichat_rx_hook_t hook) { s_hook = hook; }
wichat_rx_hook_t wichat_get_rx_hook(void) { return s_hook; }

void wichat_pin_channel(uint8_t ch) { s_pinnedCh = ch; }
uint8_t wichat_pinned_channel(void) { return s_pinnedCh; }

void wichat_set_capture_hook(wichat_rx_hook_t hook) {
    s_captureHook = hook;
    // Capture tools (PCAP / handshake) need every frame type, including CTRL
    // and MISC. Outside those tools keep the filter on MGMT|DATA so the
    // promiscuous RX callback isn't hammered by control frames — this reduces
    // WiFi-task load, which matters on the shared-bus 3.5" board. Harmless if
    // WiFi/promiscuous isn't up yet (returns NOT_INIT and is re-set at boot).
    wifi_promiscuous_filter_t filter;
    filter.filter_mask = hook ? WIFI_PROMIS_FILTER_MASK_ALL
                              : (WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA);
    esp_wifi_set_promiscuous_filter(&filter);
}
wichat_rx_hook_t wichat_get_capture_hook(void) { return s_captureHook; }

int wichat_send_frame(const uint8_t* data, size_t len) {
    // Send raw 802.11 frame — this works alongside promiscuous RX
    esp_err_t r = esp_wifi_80211_tx(WIFI_IF_STA, data, (int)len, false);
    return (r == ESP_OK) ? 0 : -1;
}
