Ты пишешь ОДНУ функцию C `static void sec3_res(void)` для файла `tests/host/probe_push.c` (C11, gcc `-Wall -Wextra -O2`). Верни ТОЛЬКО код этой функции, без пояснений и без markdown-ограждений. Комментарии короткие, по-русски. Функция печатает в stdout ПО ОДНОЙ СТРОКЕ НА ОБРАЗЕЦ (детерминированно) — результаты обработки ответов приёмника на реальные пачки.

## Уже есть в файле (не объявляй заново)
`#include <stdio.h> <stdlib.h> <string.h> <stdint.h>`, `af_push.h`. Типы: `af_push_cur_t {uint32_t acked; uint32_t epoch; uint8_t valid;}`; `af_push_ring_t {af_push_cur_t cur; uint8_t err; uint8_t stopped; uint8_t fails; int16_t last_http; uint16_t eff_batch; uint16_t last_rows; uint32_t sent_total; uint32_t fail_total; uint32_t retries; uint32_t last_ok_ts;}`; функция `int af_push_on_result(af_push_ring_t *r, uint8_t *auth_stop, int http, int transport_err, const char *body, size_t n, uint32_t start, uint32_t rows, uint32_t head, uint32_t epoch, uint16_t cfg_batch, uint32_t now_ts)` возвращает действие (0 OK, 1 RETRY, 2 STOP_AUTH, 3 STOP_RING, 4 SHRINK); `void af_push_ring_init(af_push_ring_t *r, uint16_t cfg_batch)`.

## Что напечатать
Вспомогательный макрос внутри функции не нужен; используй локальную вложенную логику через цикл по таблице образцов. Для каждого образца: свежее кольцо `af_push_ring_t r; af_push_ring_init(&r, 500); r.cur.acked = 99; r.cur.epoch = 21; r.cur.valid = 1; uint8_t auth = 0;`, вызов `act = af_push_on_result(&r, &auth, http, terr, body, strlen(body), start, rows, 1200, 21, 500, 1792860000)`, и печать одной строки:
`printf("res id=%s act=%d acked=%lu v=%u ep=%lu err=%u stop=%u fails=%u http=%d eff=%u sent=%lu failt=%lu retr=%lu auth=%u\n", id, act, (unsigned long)r.cur.acked, (unsigned)r.cur.valid, (unsigned long)r.cur.epoch, (unsigned)r.err, (unsigned)r.stopped, (unsigned)r.fails, (int)r.last_http, (unsigned)r.eff_batch, (unsigned long)r.sent_total, (unsigned long)r.fail_total, (unsigned long)r.retries, (unsigned)auth);`
Образцы (id строкой):
1. «b0».. «b11»: 12 пачек raw, i=0..11: http=200, terr=0, start=100*i, rows=100, тело `{"accepted":100,"last_seq":<100*i+99>}` (собрать snprintf).
2. «idem»: http=200, start=0, rows=100, тело `{"accepted":0,"last_seq":1199}`.
3. «empty»: http=200, start=0, rows=0, тело `{"accepted":0,"last_seq":-1}`.
4. «bad_acc»: http=200, start=100, rows=100, тело `{"accepted":101,"last_seq":199}`.
5. «bad_low»: http=200, start=100, rows=100, тело `{"accepted":0,"last_seq":50}`.
6. «bad_high»: http=200, start=1100, rows=100, тело `{"accepted":100,"last_seq":1300}`.
7. «junk»: http=200, тело `OK`.
8. Для кодов http из списка 400, 401, 404, 405, 408, 413, 429, 500, 502, 503, 599, 301, 204: id «h<код>», terr=0, start=100, rows=100, тело `{"error":"x"}`.
9. «net»: terr=1, http=0, start=100, rows=100, тело пустая строка.
10. «huge413»: ring с eff_batch=10 (после af_push_ring_init установить r.eff_batch = 10), http=413.
11. «eff0»: terr=1 и r.eff_batch = 0 перед вызовом.
Реализуй таблицей структур (id, http, terr, start, rows, body, eff_override) в одном цикле, где eff_override=-1 означает «не менять».
