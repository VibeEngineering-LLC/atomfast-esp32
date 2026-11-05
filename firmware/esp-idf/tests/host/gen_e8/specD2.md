Ты пишешь ЧАСТЬ 4 заголовочного файла `main/af_push.h` прошивки ESP32 (язык C11, gcc `-Wall -Wextra -Werror -O2`). Верни ТОЛЬКО код C, без пояснений и без markdown-ограждений. Комментарии короткие, по-русски. Всё `static inline`, без malloc, без ESP-IDF. Эта часть вклеивается ПЕРЕД закрывающей строкой `#endif` файла, после частей 1-3, где уже есть: `af_push_cfg_t` (поля enabled, rings, port, interval_s, batch_rows, timeout_s, host, token), `af_push_ring_t` (поля cur{acked,epoch,valid}, err, stopped, fails, last_http, eff_batch, last_rows, sent_total, fail_total, retries, last_ok_ts), `af_push_plan_t` {start,count,lost,pending,flags}, `af_push_pack_t` {first_seq,last_seq,rows,scan_end,lost,len}, `af_push_read_fn`, макросы AF_PUSH_ERR_NODEV, AF_PUSH_ERR_NOSTORE, AF_PUSH_A_OK/RETRY/STOP_AUTH/STOP_RING/SHRINK, и функции:
`void af_push_plan(const af_push_cur_t *cur, uint32_t oldest, uint32_t head, uint32_t epoch, uint32_t batch, af_push_plan_t *p)`,
`int af_push_pack(af_push_read_fn rd, void *ctx, int kind, uint32_t start, uint32_t max_rows, const char *mac, uint32_t epoch, uint32_t lost0, char *buf, size_t cap, af_push_pack_t *out)` (0 ок; -3 недопустимые символы в mac; другое ненулевое — ошибка),
`void af_push_advance_local(af_push_ring_t *r, uint32_t scan_end, uint32_t epoch)`,
`int af_push_on_result(af_push_ring_t *r, uint8_t *auth_stop, int http, int transport_err, const char *body, size_t n, uint32_t start, uint32_t rows, uint32_t head, uint32_t epoch, uint16_t cfg_batch, uint32_t now_ts)` (возвращает AF_PUSH_A_*). Не повторяй их объявления.

Нужна одна обобщённая функция «отправить одну пачку одного кольца» с подключаемым вводом-выводом, чтобы одну и ту же логику гонять и на плате, и в хост-тесте.

## Выдай дословно (макросы и тип)
```
#define AF_PUSH_S_OK 0     /* пачка принята (или локально пропущены непригодные записи): можно сразу повторить */
#define AF_PUSH_S_IDLE 1   /* слать нечего */
#define AF_PUSH_S_WAIT 2   /* нужна пауза: ошибка, занято, нет данных для упаковки */
#define AF_PUSH_S_STOP 3   /* кольцо остановлено или остановлен доступ (401) */

typedef struct {
    int (*info)(void *ctx, int kind, uint32_t *oldest, uint32_t *head, uint32_t *epoch);   /* 0 ок, -1 занято, -2 кольца нет */
    af_push_read_fn read;
    int (*post)(void *ctx, int kind, const char *mac, uint32_t head, uint32_t epoch, uint32_t first_seq, uint32_t lost, const char *body, size_t len, int *http, char *resp, size_t resp_cap);   /* 0 - ответ получен (код в *http), иначе ошибка транспорта; resp - тело ответа, завершить нулём */
    uint32_t (*now_ts)(void *ctx);   /* unix-время или 0 */
} af_push_ops_t;
```

## Функция
`static inline int af_push_send_ring(const af_push_ops_t *ops, void *ctx, int kind, const af_push_cfg_t *c, const char *mac, af_push_ring_t *r, uint8_t *auth_stop, char *body, size_t cap, uint32_t *pending_left, int *cursor_moved)` — возвращает AF_PUSH_S_*. Алгоритм по шагам:
1. Если любой из ops, ops->info, ops->read, ops->post, c, r, body, pending_left, cursor_moved равен NULL, либо kind вне 0..2 — вернуть AF_PUSH_S_WAIT. `*pending_left = 0; *cursor_moved = 0;`
2. Если `r->stopped` — вернуть AF_PUSH_S_STOP.
3. Если mac NULL или mac[0] == 0: `r->err = AF_PUSH_ERR_NODEV`; вернуть AF_PUSH_S_WAIT.
4. `int ir = ops->info(ctx, kind, &oldest, &head, &epoch)`. ir == -1 → вернуть AF_PUSH_S_WAIT (ошибку не записывать); ir != 0 (то есть -2 и прочее) → вернуть AF_PUSH_S_IDLE.
5. `batch = r->eff_batch ? r->eff_batch : c->batch_rows;` если batch > c->batch_rows — batch = c->batch_rows. `af_push_plan(&r->cur, oldest, head, epoch, batch, &pl)`. Если `pl.count == 0` — AF_PUSH_S_IDLE.
6. `af_push_pack(ops->read, ctx, kind, pl.start, pl.count, mac, epoch, pl.lost, body, cap, &pk)`: возврат -3 → `r->err = AF_PUSH_ERR_NODEV`, WAIT; любой другой ненулевой → `r->err = AF_PUSH_ERR_NOSTORE`, WAIT.
7. Если `pk.rows == 0`: если `pk.scan_end != pl.start` (просмотрены только непригодные записи) — `af_push_advance_local(r, pk.scan_end, epoch); *cursor_moved = 1;` вернуть AF_PUSH_S_OK; иначе вернуть AF_PUSH_S_WAIT.
8. `char resp[160]; resp[0] = 0; int http = 0; int pr = ops->post(ctx, kind, mac, head, epoch, pk.first_seq, pk.lost, body, pk.len, &http, resp, sizeof resp); resp[sizeof resp - 1] = 0;`
9. `uint32_t ts = ops->now_ts ? ops->now_ts(ctx) : 0; int act = af_push_on_result(r, auth_stop, http, pr != 0, resp, strlen(resp), pk.first_seq, pk.rows, head, epoch, c->batch_rows, ts);`
10. Если `act == AF_PUSH_A_OK`: `*cursor_moved = 1;` пересчитать остаток: `af_push_plan_t p2; af_push_plan(&r->cur, oldest, head, epoch, 0xFFFFFFFFu, &p2); *pending_left = p2.pending;`
11. Результат: AF_PUSH_A_OK → AF_PUSH_S_OK; AF_PUSH_A_SHRINK и AF_PUSH_A_RETRY → AF_PUSH_S_WAIT; AF_PUSH_A_STOP_AUTH и AF_PUSH_A_STOP_RING → AF_PUSH_S_STOP; прочее → AF_PUSH_S_WAIT.

Больше ничего не добавляй (без `#endif`).
