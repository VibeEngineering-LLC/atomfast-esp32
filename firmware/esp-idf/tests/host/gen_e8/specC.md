Ты пишешь ЧАСТЬ 3 (последнюю) заголовочного файла `main/af_push.h` прошивки ESP32 (язык C11, gcc `-Wall -Wextra -Werror -O2`). Верни ТОЛЬКО код C, без пояснений и без markdown-ограждений. Комментарии короткие, по-русски. Всё `static inline`, без malloc, без ESP-IDF. Эта часть вклеивается ПОСЛЕ частей 1 и 2, где уже есть: все макросы AF_PUSH_* (в том числе AF_PUSH_BODY_MAX=16384, AF_PUSH_ROW_MAX=160), типы `af_push_cfg_t` (поля enabled, rings, port, interval_s, batch_rows, timeout_s, host, token), `af_push_cur_t` {acked, epoch, valid}, `af_push_plan_t` {start,count,lost,pending,flags}, `af_push_ring_t` (поля cur, err, stopped, fails, last_http, eff_batch, last_rows, sent_total, fail_total, retries, last_ok_ts), функции `af_push_plan(cur, oldest, head, epoch, batch, &plan)`, `af_push_err_name(uint8_t)`, `af_push_bad(const af_push_ring_t r[3], uint8_t rings, uint8_t auth_stop)`, `af_push_state_name(int enabled,int sending,int bad)`, `af_push_addr_class(const char *host)` (0 публичный, 1 частный, 2 имя), а также из включённых заголовков проекта: `af_rec.h` (AF_REC_RAW_SIZE=20, AF_REC_AGG_SIZE=28, af_raw_rec_t, af_agg_rec_t, `int af_raw_decode(const uint8_t*, af_raw_rec_t*)` 0=ок, `int af_agg_decode(const uint8_t*, af_agg_rec_t*)`, `int af_csv_header(int with_mac, char *buf, size_t cap)` возвращает длину вместе с '\n' или <0, `int af_raw_csv(const af_raw_rec_t*, const char *mac_or_NULL, char *buf, size_t cap)` и `int af_agg_csv(const af_agg_rec_t*, const char *mac_or_NULL, char *buf, size_t cap)` — длина строки с '\n' или <0), `af_env.h` (AF_REC_ENV_SIZE=14, af_env_rec_t, `int af_env_decode(const uint8_t*, af_env_rec_t*)`, `int af_env_csv_header(char *buf, size_t cap)`, `int af_env_csv(const af_env_rec_t*, char *buf, size_t cap)`), `af_hist_query.h` (AF_HQ_KIND_RAW=0, AF_HQ_KIND_AGG=1, AF_HQ_KIND_ENV=2, `int af_hq_ext_header(const char *base_hdr, char *buf, size_t cap)` возвращает длину или -1, `int af_hq_ext_prefix(const char *mac, uint32_t seq, uint32_t epoch, char *buf, size_t cap)` возвращает длину префикса "mac,seq,epoch," или -1 (мало места) или -2 (недопустимые символы в mac)). Не повторяй их объявления. Все эти функции использовать как есть, не копировать их тела.

Выдай ровно в таком порядке.

## 1. Включение
```
#include <stdarg.h>
```

## 2. Типы (дословно)
```
typedef int (*af_push_info_fn)(void *ctx, int kind, uint32_t *oldest, uint32_t *head, uint32_t *epoch);
typedef uint32_t (*af_push_read_fn)(void *ctx, int kind, uint32_t since, void *out, uint32_t max, uint32_t *next, uint32_t *lost);

typedef struct {
    uint32_t first_seq;  /* seq первой записанной в тело строки */
    uint32_t last_seq;   /* seq последней записанной строки */
    uint32_t rows;       /* строк в теле */
    uint32_t scan_end;   /* seq сразу после последней просмотренной записи */
    uint32_t lost;       /* потери для X-AF-Lost */
    size_t len;          /* длина тела (шапка + строки) */
} af_push_pack_t;

typedef struct {
    uint8_t state;       /* 0 idle, 1 running, 2 done */
    int16_t http;
    uint32_t ms;
    int32_t accepted;    /* -1 если нет разобранного ответа */
    uint8_t err;         /* AF_PUSH_ERR_* */
    uint32_t ts;
} af_push_test_t;

typedef struct {
    uint8_t present;     /* кольцо существует */
    uint32_t oldest;
    uint32_t head;
    uint32_t epoch;
} af_push_ringinfo_t;

typedef struct {
    int sending;
    int auth_stop;
    uint32_t stack_hwm;
    uint32_t now_ts;
    af_push_ring_t ring[3];
    af_push_ringinfo_t info[3];
    af_push_test_t test;
} af_push_view_t;
```

## 3. Функции (ровно эти имена и сигнатуры)

`static inline const char *af_push_kind_name(int kind)` — "raw","agg","env" для 0,1,2; иначе "raw".

`static inline const char *af_push_path(int kind)` — "/ingest/raw", "/ingest/agg", "/ingest/env" для 0,1,2; иначе "/ingest/raw".

`static inline int af_push_test_body(char *buf, size_t cap)` — тело пробного запроса: только шапка CSV кольца env в ext-формате. Взять базовую шапку `af_env_csv_header(base, sizeof base)` (base[96]), затем `af_hq_ext_header(base, buf, cap)`. Вернуть длину или -1 при любой ошибке.

`static inline int af_push_pack(af_push_read_fn rd, void *ctx, int kind, uint32_t start, uint32_t max_rows, const char *mac, uint32_t epoch, uint32_t lost0, char *buf, size_t cap, af_push_pack_t *out)` — собирает тело CSV (ext-формат: `mac,seq,epoch,` + обычная строка записи) для кольца kind, начиная с seq=start. Возврат 0 успех; -1 неверные аргументы (rd, buf, out NULL; mac NULL или пустая; kind вне 0..2; cap < 256 + AF_PUSH_ROW_MAX; max_rows == 0); -2 ошибка шапки; -3 недопустимые символы в mac (af_hq_ext_prefix вернул -2). Алгоритм:
1. memset(out,0); out->lost = lost0; out->scan_end = start.
2. Шапка: base[96]; для kind==AF_HQ_KIND_ENV n=af_env_csv_header(base,sizeof base), иначе n=af_csv_header(0,base,sizeof base); n<0 → -2. Затем `len = af_hq_ext_header(base, buf, cap)`; <0 → -2.
3. Цикл чтения чанками. Локальный буфер `uint8_t rec[32*28]`. Переменные: cur=start; first_read=1; full=0. Пока rows < max_rows и !full: если cap - len < AF_PUSH_ROW_MAX → выйти. chunk = min(max_rows - rows, 32). `n = rd(ctx, kind, cur, rec, chunk, &next, &lost)`. Если first_read: out->lost += lost; first_read=0; иначе если lost != 0 → выйти из цикла (чанк не использовать). Если n == 0 → выйти. seq0 = next - n (uint32). Для i=0..n-1: seq = seq0 + i. Пока rows == max_rows — прервать (full=1). Если cap - len < AF_PUSH_ROW_MAX → full=1 и прервать (запись seq не обработана, scan_end остаётся равным seq). Декодировать запись из rec + i*размер_записи (размер: RAW=AF_REC_RAW_SIZE, AGG=AF_REC_AGG_SIZE, ENV=AF_REC_ENV_SIZE) соответствующим decode; если decode вернул не 0 — пропустить запись: scan_end = seq + 1; continue. Префикс: pl = af_hq_ext_prefix(mac, seq, epoch, buf+len, cap-len); pl == -2 → return -3; pl < 0 → full=1, прервать (scan_end не менять). Тело строки: для RAW `af_raw_csv(&r, NULL, buf+len+pl, cap-len-pl)`, для AGG `af_agg_csv(&r, NULL, ...)`, для ENV `af_env_csv(&r, ...)`. Если результат < 0: buf[len]=0 (откатить префикс), scan_end = seq + 1, continue (запись нефинитна — пропустить). Иначе len += pl + rl; если rows == 0: first_seq = seq; rows++; last_seq = seq; scan_end = seq + 1. После обработки чанка (если не full) cur = next.
4. buf[len]=0; out->len=len; вернуть 0.
Заметь: при пропуске записей курсор подтверждает только реально отправленные строки; scan_end нужен вызывающему, когда rows==0.

`static inline int af_push_app(char *buf, size_t cap, int pos, const char *fmt, ...)` с атрибутом `__attribute__((format(printf, 4, 5)))` — дописывает форматированную строку в buf с позиции pos через vsnprintf. Если pos < 0 или pos >= cap — вернуть -1. Если результат < 0 или pos + результат >= cap — вернуть -1. Иначе вернуть pos + результат.

`static inline int af_push_json(char *buf, size_t cap, const af_push_cfg_t *c, const af_push_view_t *v)` — собрать JSON ответа GET /api/push. Возврат длины или -1 (аргументы NULL, переполнение). Все числа беззнаковые печатать как `%lu` с приведением к `(unsigned long)`, знаковые как `%ld` с `(long)`. Токен в вывод не попадает НИКОГДА, только булево token_set (token[0] != 0). host выводится как есть (в нём допустимы только [A-Za-z0-9.-], экранирование не нужно). Формат, ключи строго в этом порядке:
```
{"enabled":B,"host":"H","port":N,"token_set":B,"interval_s":N,"batch_rows":N,"timeout_s":N,
 "rings":["raw","agg","env"],  <- только включённые биты маски c->rings в порядке raw, agg, env
 "warn":{"no_tls":true,"public_addr":B},   <- public_addr = (af_push_addr_class(c->host) == 0)
 "state":"S","auth_stop":B,"stack_hwm":N,
 "test":{"state":"idle|running|done","http":N,"ms":N,"accepted":N,"err":"имя","ts":N},   <- state по v->test.state: 0 idle, 1 running, иначе done; err = af_push_err_name(v->test.err)
 "ring":{"raw":R0,"agg":R1,"env":R2}}
```
(без переводов строк и пробелов в реальном выводе; B — true/false). S = af_push_state_name(c->enabled, v->sending, af_push_bad(v->ring, c->rings, (uint8_t)v->auth_stop)). Объект кольца R для i=0..2:
`{"on":B,"present":B,"acked_seq":N,"epoch":N,"pending":N,"last_ok_ts":N,"lag_s":N,"last_err":{"code":N,"text":"имя"},"last_http":N,"sent_total":N,"fail_total":N,"retries":N,"last_batch_rows":N,"stopped":B}`
где on = бит i маски c->rings; present = v->info[i].present; acked_seq = cur.valid ? (long)cur.acked : -1; epoch = v->info[i].epoch; pending = при present — `af_push_plan(&v->ring[i].cur, info.oldest, info.head, info.epoch, 0xFFFFFFFFu, &p)` и p.pending, иначе 0; lag_s = если pending > 0 и last_ok_ts != 0 и now_ts >= last_ok_ts то now_ts - last_ok_ts иначе 0; last_err.code = ring.err, text = af_push_err_name(ring.err); last_http = (long)ring.last_http; остальное — поля ring (sent_total, fail_total, retries, last_rows → last_batch_rows, stopped).
Собирай вывод через af_push_app, накапливая pos; если pos < 0 в конце — вернуть -1.

Больше ничего не добавляй, КРОМЕ последней строки файла `#endif` (закрытие include guard AF_PUSH_H) в самом конце.
