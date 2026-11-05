Ты пишешь ЧАСТЬ 2 заголовочного файла `main/af_push.h` прошивки ESP32 (язык C11, gcc `-Wall -Wextra -Werror -O2`). Верни ТОЛЬКО код C, без пояснений и без markdown-ограждений. Комментарии короткие, по-русски. Всё `static inline`, без malloc, без ESP-IDF. Эта часть вклеивается ПОСЛЕ части 1, где уже есть: макросы AF_PUSH_* (BATCH_FLOOR=10, CUR_BLOB=48, CUR_SAVE_MS=30000u, BACKOFF_BASE_MS=2000u, BACKOFF_CAP_MS=300000u, RINGS=3), `af_push_cfg_t`, включения `<stdint.h> <stddef.h> <string.h> <stdbool.h>`, `cJSON.h`, а также функции `af_bsn_crc32(const uint8_t*, size_t)` (CRC32, из af_ble_nvs.h) и `af_sens_keys_ok(const cJSON *root, const char *const *allowed)` и `af_sens_num(const cJSON*, double lo, double hi, int integer, double *out)` (возвращает 0 при успехе). Не повторяй их объявления.

Выдай ровно в таком порядке.

## 1. Типы и макросы (дословно)
```
typedef struct {
    uint32_t acked;   /* последний подтверждённый seq */
    uint32_t epoch;   /* epoch кольца на момент подтверждения */
    uint8_t valid;    /* 0 - подтверждений ещё не было */
} af_push_cur_t;

#define AF_PUSH_PF_FRESH 1
#define AF_PUSH_PF_EPOCH 2
#define AF_PUSH_PF_ROLLBACK 4
#define AF_PUSH_PF_EVICTED 8

typedef struct {
    uint32_t start;    /* с какого seq слать */
    uint32_t count;    /* максимум строк в пачке (<= batch) */
    uint32_t lost;     /* сколько строк вытеснено до start (для X-AF-Lost) */
    uint32_t pending;  /* сколько строк ждёт отправки с start */
    uint8_t flags;
} af_push_plan_t;

#define AF_PUSH_ERR_NONE 0
#define AF_PUSH_ERR_NET 1
#define AF_PUSH_ERR_SRV 2
#define AF_PUSH_ERR_AUTH 3
#define AF_PUSH_ERR_BAD 4
#define AF_PUSH_ERR_BIG 5
#define AF_PUSH_ERR_PROTO 6
#define AF_PUSH_ERR_HTTP4 7
#define AF_PUSH_ERR_NODEV 8
#define AF_PUSH_ERR_NOSTORE 9
#define AF_PUSH_ERR_OOM 10
#define AF_PUSH_ERR_COUNT 11

#define AF_PUSH_A_OK 0
#define AF_PUSH_A_RETRY 1
#define AF_PUSH_A_STOP_AUTH 2
#define AF_PUSH_A_STOP_RING 3
#define AF_PUSH_A_SHRINK 4

typedef struct {
    af_push_cur_t cur;
    uint8_t err;          /* AF_PUSH_ERR_* последней ошибки (0 - нет) */
    uint8_t stopped;      /* 1 - кольцо остановлено, нужно вмешательство */
    uint8_t fails;        /* подряд идущих неудач (для паузы) */
    int16_t last_http;    /* 0 - ответа не было */
    uint16_t eff_batch;   /* текущий размер пачки (после 413 уменьшается) */
    uint16_t last_rows;   /* строк в последней успешной пачке */
    uint32_t sent_total;  /* принято приёмником (сумма accepted) */
    uint32_t fail_total;
    uint32_t retries;
    uint32_t last_ok_ts;
} af_push_ring_t;
```

## 2. Функции (ровно эти имена и сигнатуры)

`static inline const char *af_push_err_name(uint8_t e)` — таблица строк по индексу: "", "net", "server", "auth", "bad_batch", "too_big", "proto", "http4xx", "no_device", "no_store", "oom"; для e >= AF_PUSH_ERR_COUNT вернуть "unknown".

`static inline void af_push_plan(const af_push_cur_t *cur, uint32_t oldest, uint32_t head, uint32_t epoch, uint32_t batch, af_push_plan_t *p)` — план отправки. Все разности беззнаковые 32-бит с приведением к int32_t для сравнения (кольцо seq может переполняться). Правила:
- memset(p,0). Если cur NULL или p NULL — выйти.
- Если !cur->valid: start=oldest, flags=AF_PUSH_PF_FRESH.
- иначе если cur->epoch != epoch: start=oldest, flags=AF_PUSH_PF_EPOCH.
- иначе next = cur->acked + 1 (uint32). Если (int32_t)(next - head) > 0 — откат seq: start=oldest, flags=AF_PUSH_PF_ROLLBACK. Иначе если (int32_t)(oldest - next) > 0 — запрошенное вытеснено: start=oldest, lost=oldest-next, flags=AF_PUSH_PF_EVICTED. Иначе start=next.
- pending: если (int32_t)(head - start) > 0 то head - start, иначе 0. count = pending, но не больше batch.

`static inline size_t af_push_curs_encode(const af_push_cur_t cur[3], uint8_t *buf, size_t cap)` — blob ровно AF_PUSH_CUR_BLOB (48) байт, вернуть 48; при NULL или cap<48 вернуть 0. Раскладка: memset(buf,0,48); 0..3 магия 'A','P','C','1'; 4..5 версия 1,0; 6..7 длина полезной части 0x24,0x00 (36); для i=0..2 по смещению 8+12*i: acked (4 байта, младший первым), epoch (4 байта, младший первым), valid (1 байт), 3 байта нулей; 44..47 CRC32 (af_bsn_crc32) по байтам [0..43], младший байт первым.

`static inline bool af_push_curs_decode(af_push_cur_t cur[3], const uint8_t *buf, size_t len)` — обратно. При любой ошибке (NULL; len != 48; магия; версия; длина; CRC; байт valid > 1; пад-байты не нули) — memset(cur,0,3*sizeof(af_push_cur_t)) и вернуть false. Порядок: NULL/len, магия, версия, длина, CRC, затем поля.

`static inline bool af_push_curs_due(bool dirty, uint32_t now_ms, uint32_t last_save_ms)` — вернуть true только если dirty И (uint32_t)(now_ms - last_save_ms) >= AF_PUSH_CUR_SAVE_MS.

`static inline void af_push_ring_init(af_push_ring_t *r, uint16_t cfg_batch)` — memset 0, eff_batch=cfg_batch.

`static inline void af_push_ring_clear_err(af_push_ring_t *r, uint16_t cfg_batch)` — err=0, stopped=0, fails=0, last_http=0, eff_batch=cfg_batch (курсор и счётчики sent_total/fail_total/retries/last_ok_ts НЕ трогать).

`static inline void af_push_ring_reset_cursor(af_push_ring_t *r, uint16_t cfg_batch)` — курсор обнулить (memset cur), затем как clear_err.

`static inline void af_push_advance_local(af_push_ring_t *r, uint32_t scan_end, uint32_t epoch)` — курсор = {acked=scan_end-1, epoch, valid=1} (когда в просмотренном диапазоне нечего слать).

`static inline int af_push_parse_resp(const char *body, size_t n, uint32_t *accepted, int64_t *last_seq)` — строгий разбор ответа приёмника `{"accepted":N,"last_seq":S}`. Возврат 0 или -1. Требования: аргументы не NULL, n>0; cJSON_ParseWithLength; корень — объект; ровно два ключа "accepted" и "last_seq" (af_sens_keys_ok с allowed {"accepted","last_seq",NULL} И cJSON_GetArraySize(root)==2); accepted — целое число 0..4294967295 (af_sens_num); last_seq — целое число -1..4294967295 (af_sens_num; значение -1 законно: приёмник ничего не хранит). Записать в *accepted и *last_seq только при успехе. cJSON_Delete на всех путях ровно один раз.

`static inline int af_push_on_result(af_push_ring_t *r, uint8_t *auth_stop, int http, int transport_err, const char *body, size_t n, uint32_t start, uint32_t rows, uint32_t head, uint32_t epoch, uint16_t cfg_batch, uint32_t now_ts)` — обработка итога одной отправки, возвращает AF_PUSH_A_*. start - первый seq пачки, rows - число строк в теле, head - голова кольца, epoch - epoch кольца при отправке. Логика:
- r->last_http = transport_err ? 0 : (http в пределах 0..999 ? http : 0). Если r->eff_batch == 0 — присвоить cfg_batch.
- transport_err != 0: err=AF_PUSH_ERR_NET; fails++ (насыщение на 255); fail_total++; retries++; вернуть AF_PUSH_A_RETRY.
- http == 200: af_push_parse_resp(body,n,&acc,&last). Если разбор не удался ИЛИ acc > rows ИЛИ (last + 1) < (int64_t)start ИЛИ (last + 1) > (int64_t)head — это нарушение протокола: err=AF_PUSH_ERR_PROTO, stopped=1, fail_total++, вернуть AF_PUSH_A_STOP_RING (курсор НЕ менять). Иначе: если last == -1 → cur.valid=0, cur.acked=0, cur.epoch=epoch; иначе cur.acked=(uint32_t)last, cur.epoch=epoch, cur.valid=1. Затем sent_total += acc; last_rows=(uint16_t)rows; last_ok_ts=now_ts; err=0; stopped=0; fails=0; если eff_batch < cfg_batch — eff_batch = min(cfg_batch, 2*eff_batch). Вернуть AF_PUSH_A_OK.
- http == 401: err=AUTH; *auth_stop=1 (если auth_stop не NULL); fail_total++; вернуть AF_PUSH_A_STOP_AUTH.
- http == 400: err=BAD; stopped=1; fail_total++; вернуть AF_PUSH_A_STOP_RING.
- http == 413: err=BIG; fail_total++; если eff_batch > AF_PUSH_BATCH_FLOOR: eff_batch = max(AF_PUSH_BATCH_FLOOR, eff_batch/2), retries++, вернуть AF_PUSH_A_SHRINK; иначе stopped=1 и вернуть AF_PUSH_A_STOP_RING.
- http == 408 или http == 429 или 500 <= http <= 599: err=SRV; fails++ (насыщение 255); fail_total++; retries++; вернуть AF_PUSH_A_RETRY.
- прочие 400..499: err=HTTP4; stopped=1; fail_total++; вернуть AF_PUSH_A_STOP_RING.
- любой другой код (1xx, 2xx кроме 200, 3xx, мусор): err=PROTO; stopped=1; fail_total++; вернуть AF_PUSH_A_STOP_RING.

`static inline uint32_t af_push_backoff_ms(uint8_t fails, uint8_t rnd)` — пауза перед повтором. fails==0 → 0. Иначе d = AF_PUSH_BACKOFF_BASE_MS << (min(fails-1, 8)) (в uint64), затем d = min(d, AF_PUSH_BACKOFF_CAP_MS); jitter = d/8 * rnd / 255 (uint64); вернуть (uint32_t)(d + jitter).

`static inline uint32_t af_push_pause_ms(uint32_t pending_left, uint16_t interval_s, uint8_t rnd)` — пауза после успешной пачки. Если pending_left > 0: 1000 + 250*rnd/255. Иначе: interval_s*1000 + (interval_s*100)*rnd/255 (в uint64, итог uint32).

`static inline int af_push_bad(const af_push_ring_t r[3], uint8_t rings, uint8_t auth_stop)` — 1 если auth_stop, либо для какого-то кольца i с включённым битом (rings >> i) & 1 выполнено r[i].stopped или r[i].fails >= 3; иначе 0.

`static inline const char *af_push_state_name(int enabled, int sending, int bad)` — "off" если !enabled; иначе "sending" если sending; иначе "error" если bad; иначе "idle".

Больше ничего не добавляй: ни `#endif`, ни других функций.
