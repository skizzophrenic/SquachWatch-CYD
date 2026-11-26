// SquachWatch-CM0 -- the esp_netif.h stub.
//
// One thing the firmware asks of it (detection.cpp:963): the station
// netif handle, to attach after the slim wifi_init(). The stub keeps one
// instance, so the attach "succeeds" and the boot log is consistent.
#pragma once

#ifndef ESP_OK
#define ESP_OK 0
#endif

#ifndef SQW_STUB_ESP_ERR_T
#define SQW_STUB_ESP_ERR_T
typedef int esp_err_t;
#endif

typedef struct esp_netif_t { int _stub; } esp_netif_t;

esp_netif_t* esp_netif_get_handle_from_ifkey(const char* ifkey);
esp_err_t esp_netif_attach_wifi_station(esp_netif_t* netif);
