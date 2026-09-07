// SquachWatch-Sim — heap-caps shim (diagnostics screen only).
#pragma once
#include <cstddef>
#define MALLOC_CAP_8BIT  (1 << 2)
#define MALLOC_CAP_DMA   (1 << 3)
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 110 * 1024; }
inline size_t heap_caps_get_free_size(uint32_t) { return 180 * 1024; }
