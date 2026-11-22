// SquachWatch-CM0 -- the sim's heap-caps shim (same constants and the same
// fixed free-size numbers: the diagnostics screen has always shown those on
// this board, and nothing here needs the truth), plus the two calls the
// real speaker makes -- sound.cpp renders each sound into
// heap_caps_malloc, and frees the same pointers with free(), so both must
// be glibc here.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#define MALLOC_CAP_8BIT   (1 << 2)
#define MALLOC_CAP_DMA    (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)   // v1.45's heap checks ask for internal SRAM
inline void* heap_caps_calloc(size_t n, size_t size, uint32_t) { return calloc(n, size); }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 110 * 1024; }
inline size_t heap_caps_get_free_size(uint32_t) { return 180 * 1024; }
inline void* heap_caps_malloc(size_t size, uint32_t caps) { (void)caps; return malloc(size); }
inline void  heap_caps_free(void* p) { free(p); }
