// SquachWatch-Sim — heap-caps shim (the diagnostics screen, and the LORA
// page's PSRAM copies, which here are plain heap).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_DMA      (1 << 3)
#define MALLOC_CAP_SPIRAM   (1 << 10)
// The emulator has one heap, so the mask makes no difference to the answers
// below -- but the firmware's heap-pressure checks ask for
// MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT (src/detection.cpp explains why), and
// src/wardrive_sd.cpp is compiled here, so the bit has to exist. The real
// value is (1 << 11); it is kept the same here so a mask printed out of the
// emulator reads like one printed off a board.
#define MALLOC_CAP_INTERNAL (1 << 11)
inline void* heap_caps_calloc(size_t n, size_t size, uint32_t) { return calloc(n, size); }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 110 * 1024; }
inline size_t heap_caps_get_free_size(uint32_t) { return 180 * 1024; }
