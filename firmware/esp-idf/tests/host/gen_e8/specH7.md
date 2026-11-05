Ты пишешь ОДНУ функцию C `static void sec7_json(void)` для файла `tests/host/probe_push.c` (C11, gcc `-Wall -Wextra -O2`). Верни ТОЛЬКО код этой функции, без пояснений и без markdown-ограждений. Комментарии короткие, по-русски. Функция печатает в stdout ПО ОДНОЙ СТРОКЕ НА ОБРАЗЕЦ (детерминированно): хеш JSON ответа GET /api/push для шести боевых состояний.

## Уже есть в файле (не объявляй заново)
`#include <stdio.h> <stdlib.h> <string.h> <stdint.h>`, `af_push.h`; глобальный `static char OUT[8192];` и `static unsigned long h32(const void *p, size_t n)` (CRC32 как unsigned long). Типы из af_push.h:
- `af_push_cfg_t {uint8_t enabled; uint8_t rings; uint16_t port; uint16_t interval_s; uint16_t batch_rows; uint8_t timeout_s; char host[64]; char token[65];}` и `void af_push_cfg_default(af_push_cfg_t*)`.
- `af_push_cur_t {uint32_t acked; uint32_t epoch; uint8_t valid;}`.
- `af_push_ring_t {af_push_cur_t cur; uint8_t err; uint8_t stopped; uint8_t fails; int16_t last_http; uint16_t eff_batch; uint16_t last_rows; uint32_t sent_total; uint32_t fail_total; uint32_t retries; uint32_t last_ok_ts;}` и `void af_push_ring_init(af_push_ring_t*, uint16_t cfg_batch)`.
- `af_push_ringinfo_t {uint8_t present; uint32_t oldest; uint32_t head; uint32_t epoch;}`.
- `af_push_test_t {uint8_t state; int16_t http; uint32_t ms; int32_t accepted; uint8_t err; uint32_t ts;}`.
- `af_push_view_t {int sending; int auth_stop; uint32_t stack_hwm; uint32_t now_ts; af_push_ring_t ring[3]; af_push_ringinfo_t info[3]; af_push_test_t test;}`.
- `int af_push_json(char *buf, size_t cap, const af_push_cfg_t *c, const af_push_view_t *v)` — длина JSON или -1.

## Что сделать
Базовые боевые настройки: `af_push_cfg_default(&c); c.enabled = 1; strcpy(c.host, "10.20.30.40"); strcpy(c.token, "AbCdEf0123456789xyz"); c.port = 8480; c.interval_s = 60; c.batch_rows = 500;` Базовое представление `v`: `memset(&v, 0, sizeof v); v.now_ts = 1792860030; v.stack_hwm = 1800; for i in 0..2: af_push_ring_init(&v.ring[i], 500); v.info[0] = {1, 0, 1200, 21}; v.info[1] = {1, 0, 365, 21}; v.info[2] = {1, 0, 291, 21};` (поля info: present, oldest, head, epoch).
Шесть образцов (id 0..5), каждый — отдельный вызов `n = af_push_json(OUT, sizeof OUT, &c, &v)` и печать `printf("json id=%d n=%d crc=%08lx\n", id, n, n > 0 ? h32(OUT, strlen(OUT)) : 0UL);`:
0. полная доставка: ring[k].cur = {1199, 21, 1}, {364, 21, 1}, {290, 21, 1}; sent_total = 1200, 365, 291; last_ok_ts = 1792860000; last_rows = 100; last_http = 200.
1. середина доставки: cur = {599, 21, 1}, {199, 21, 1}, {99, 21, 1}; sent_total = 600, 200, 100; last_ok_ts = 1792860000; last_rows = 100; last_http = 200; v.sending = 1.
2. ошибка 401: auth_stop = 1; ring[0].err = 3 (AF_PUSH_ERR_AUTH); ring[0].last_http = 401; ring[0].fail_total = 1; курсоры не заданы (valid 0).
3. кольцо agg остановлено: ring[1].stopped = 1; ring[1].err = 4; ring[1].last_http = 400; ring[1].cur = {99, 21, 1}; остальные как в образце 0.
4. выключено: c.enabled = 0, остальное как в образце 0.
5. публичный адрес: strcpy(c.host, "8.8.8.8"), остальное как в образце 0; дополнительно v.test = {2, 200, 15, 0, 0, 1792860020} (state, http, ms, accepted, err, ts).
Каждый образец начинай со свежей копии базовых c и v (переприсвоением из базовых переменных).
