// SquachWatch-CM0 -- the NimBLE host-task event queue stub.
//
// The part of the NimBLE port the firmware touches: one-shot events
// posted to the default queue, which the real port runs on NimBLE's host
// task (detection.cpp's scan flush, and the OTA scan stop/start pair).
// The events must be complete types -- the firmware keeps them in statics.
//
// Phase 1: nothing is ever posted (the stub scanner never starts, and the
// OTA path that would post the stop/start pair is the sim's ota_sim), so
// the no-op delivery loses nothing. Phase 3's scanner runs its own
// threads and starts delivering these for real, on its own queue.
#pragma once
#include <stdint.h>

typedef void (*ble_npl_event_fn_t)(struct ble_npl_event* ev);

struct ble_npl_event {
    ble_npl_event_fn_t fn;
    void*              arg;
    struct { struct ble_npl_event* next; } link;
};

void ble_npl_event_init(struct ble_npl_event* ev, ble_npl_event_fn_t fn, void* arg);
void ble_npl_eventq_put(void* eventq, struct ble_npl_event* ev);
void* nimble_port_get_dflt_eventq(void);
