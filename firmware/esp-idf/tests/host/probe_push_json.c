/* T-71: печатает РЕАЛЬНЫЙ JSON af_push_json() для остановленного кольца (400): образцы для web-тестов. usage: probe_push_json <sending 0|1> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "af_push.h"

int main(int argc, char **argv) {
    af_push_cfg_t c; af_push_view_t v; static char buf[4096];
    af_push_cfg_default(&c); c.enabled = 1; strcpy(c.host, "192.0.2.10"); strcpy(c.token, "tok");
    memset(&v, 0, sizeof v);
    v.sending = (argc > 1 && argv[1][0] == '1'); v.now_ts = 1793351200;
    for (int i = 0; i < 3; i++) {
        v.info[i].present = 1; v.info[i].oldest = 1; v.info[i].head = 600 + i; v.info[i].epoch = 74;
        v.ring[i].cur.valid = 1; v.ring[i].cur.acked = 10; v.ring[i].cur.epoch = 74; v.ring[i].last_ok_ts = 1793351100; v.ring[i].sent_total = 10;
    }
    v.ring[0].err = AF_PUSH_ERR_BAD; v.ring[0].stopped = 1; v.ring[0].last_http = 400; v.ring[0].fail_total = 1; v.ring[0].last_rows = 500;
    if (argc > 2) { v.ring[0].err = 0; v.ring[0].stopped = 0; v.ring[0].last_http = 200; v.ring[0].fail_total = 0; }  /* 2-й аргумент: здоровая отправка */
    if (af_push_json(buf, sizeof buf, &c, &v) < 0) return 1;
    puts(buf);
    return 0;
}
