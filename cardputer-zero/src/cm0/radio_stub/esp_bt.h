// SquachWatch-CM0 -- the esp_bt.h stub.
//
// One Classic-era call survives in the firmware (detection.cpp:1090):
// a liveness probe of the controller before the scan starts. The stub
// answers "enabled", because the BLE half of this app is the app's own
// scanner (Phase 3) and a "disabled" answer would make the firmware
// believe the radio is absent.
#pragma once

#ifndef ESP_OK
#define ESP_OK 0
#endif

#ifndef SQW_STUB_ESP_ERR_T
#define SQW_STUB_ESP_ERR_T
typedef int esp_err_t;
#endif

typedef enum {
    ESP_BT_CONTROLLER_STATUS_IDLE = 0,
    ESP_BT_CONTROLLER_STATUS_STARTING,
    ESP_BT_CONTROLLER_STATUS_ENABLED,
    ESP_BT_CONTROLLER_STATUS_STOPPING,
    ESP_BT_CONTROLLER_STATUS_STOPPED
} esp_bt_controller_state_t;

int esp_bt_controller_get_status(void);
