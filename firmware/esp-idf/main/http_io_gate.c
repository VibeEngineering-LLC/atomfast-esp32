#include "http_io_gate.h"

/* Legacy diagnostics remain in /api/system and the debug log. There are no
 * HEAVY lane users in this firmware, so no semaphore or counters are needed. */
void http_io_gate_init(void) {}
bool http_io_gate_busy(void) { return false; }
int http_io_gate_waiters(void) { return 0; }
uint32_t http_io_gate_reject_count(void) { return 0; }
