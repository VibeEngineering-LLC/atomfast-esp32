#pragma once
#include <stdlib.h>
#include <stdint.h>
/* хост-заглушка: PSRAM = обычная куча */
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 4
static inline void *heap_caps_malloc(size_t n, uint32_t caps) { (void)caps; return malloc(n); }
