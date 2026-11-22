// SquachWatch-CM0 -- the FreeRTOS primitives sound.cpp uses, on pthreads.
//
// The firmware's sound player is a small task fed by a queue (sound.cpp,
// "Playback is a small FreeRTOS task fed by a queue"). On the CM0 that
// is one pthread and one mutex/condvar queue with the same contract:
// xQueueSend with zero ticks drops when full ("a full queue drops the
// sound, not the frame"), xQueueReceive with portMAX_DELAY blocks,
// xQueueReset empties. Threading facts the shim relies on: the sound
// task is the sole receiver; the UI thread is the sole sender (play())
// and the only one who resets (quiet()); the task lives for the whole
// process and is never joined -- it dies with the process.
//
// Picked up by every translation unit through arduino_cm0.h (the
// force-include), the same way the ESP32's Arduino.h exposes FreeRTOS.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>

#define portMAX_DELAY 0xFFFFFFFFu
#define pdTRUE        1
#define pdFALSE       0
typedef int      BaseType_t;
typedef uint32_t UBaseType_t;

// sound.cpp pins its task to core 1 when there is one: the core number is
// ignored here (pthreads), so any answer works; the Pi Zero 2 has four.
#define portNUM_PROCESSORS 4

// The detection log's lock (detection.cpp, since v1.45): every writer --
// here the BLE scan thread (postBle) and the main loop -- takes it, and
// the merges call pushLog() with it held, so it must be a REAL recursive
// mutex: a no-op stand-in would bring back the two-rows-in-one-slot race
// that upstream fixed.
typedef pthread_mutex_t* SemaphoreHandle_t;
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
    auto* m = new pthread_mutex_t;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    return m;
}
inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t m, uint32_t) {
    return pthread_mutex_lock(m) == 0 ? pdTRUE : pdFALSE;   // portMAX_DELAY everywhere
}
inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t m) {
    return pthread_mutex_unlock(m) == 0 ? pdTRUE : pdFALSE;
}

struct ShimQueue {
    pthread_mutex_t m;
    pthread_cond_t  notEmpty;
    pthread_cond_t  notFull;
    uint8_t* items;      // len * size
    size_t   size;       // bytes per item
    size_t   len;        // item capacity
    size_t   head, tail, count;
};
typedef ShimQueue* QueueHandle_t;

struct ShimTask { pthread_t t; };
typedef ShimTask* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);

inline QueueHandle_t xQueueCreate(UBaseType_t len, size_t size) {
    auto* q = new ShimQueue();
    q->items  = new uint8_t[len * size];
    q->size   = size;
    q->len    = len;
    q->head   = q->tail = q->count = 0;
    pthread_mutex_init(&q->m, nullptr);
    pthread_cond_init(&q->notEmpty, nullptr);
    pthread_cond_init(&q->notFull, nullptr);
    return q;
}

inline BaseType_t xQueueSend(QueueHandle_t q, const void* item, uint32_t ticks) {
    pthread_mutex_lock(&q->m);
    if (q->count == q->len) {           // zero ticks: drop, as the firmware expects
        if (ticks == 0) {
            if (getenv("SQUACH_I2S_TRACE"))
                fprintf(stderr, "[shim] queue send DROPPED (full, %zu)\n", q->count);
            pthread_mutex_unlock(&q->m);
            return pdFALSE;
        }
        pthread_cond_wait(&q->notFull, &q->m);
    }
    memcpy(&q->items[q->tail * q->size], item, q->size);
    q->tail = (q->tail + 1) % q->len;
    q->count++;
    if (getenv("SQUACH_I2S_TRACE"))
        fprintf(stderr, "[shim] queue send %u\n", ((const uint8_t*)item)[0]);
    pthread_cond_signal(&q->notEmpty);
    pthread_mutex_unlock(&q->m);
    return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t q, void* item, uint32_t ticks) {
    // The lock lives for the whole loop: pthread_cond_wait re-acquires it
    // on wake, so re-locking each iteration would deadlock on the second.
    pthread_mutex_lock(&q->m);
    for (;;) {
        if (q->count > 0) {
            memcpy(item, &q->items[q->head * q->size], q->size);
            q->head = (q->head + 1) % q->len;
            q->count--;
            if (getenv("SQUACH_I2S_TRACE"))
                fprintf(stderr, "[shim] queue recv %u\n", ((const uint8_t*)item)[0]);
            pthread_cond_signal(&q->notFull);
            pthread_mutex_unlock(&q->m);
            return pdTRUE;
        }
        if (ticks != portMAX_DELAY) {   // the only non-blocking receive used
            pthread_mutex_unlock(&q->m);
            return pdFALSE;
        }
        pthread_cond_wait(&q->notEmpty, &q->m);
    }
}

inline void xQueueReset(QueueHandle_t q) {
    pthread_mutex_lock(&q->m);
    q->head = q->tail = q->count = 0;
    pthread_cond_broadcast(&q->notFull);
    pthread_mutex_unlock(&q->m);
}

inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, uint32_t,
                                          void* param, UBaseType_t, TaskHandle_t* handle,
                                          BaseType_t) {
    struct Ctx { TaskFunction_t fn; void* param; };
    auto* cc = new Ctx{fn, param};
    auto trampoline = [](void* a) -> void* {
        auto* c = static_cast<Ctx*>(a);
        c->fn(c->param);
        delete c;
        return nullptr;
    };
    auto* t = new ShimTask();
    if (pthread_create(&t->t, nullptr, trampoline, cc) != 0) {
        delete cc;
        delete t;
        return pdFALSE;
    }
    if (handle) *handle = t;
    return pdTRUE;
}
