#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 4
void *heap_caps_malloc(size_t cap, uint32_t flags);
