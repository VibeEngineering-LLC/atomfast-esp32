#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Compatibility observations for /api/system and the debug log. */
void http_io_gate_init(void);
bool http_io_gate_busy(void);
int http_io_gate_waiters(void);
uint32_t http_io_gate_reject_count(void);
