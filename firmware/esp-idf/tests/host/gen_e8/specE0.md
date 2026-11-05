Ты пишешь общий вспомогательный файл `tests/host/push_t_common.inc` для host-тестов (C11, gcc `-Wall -Wextra -Werror -O2`, запуск на Linux) заголовка `main/af_push.h` прошивки AtomFast. Верни ТОЛЬКО код C, без пояснений и без markdown-ограждений. Комментарии короткие, по-русски. Файл будет включён директивой `#include` в `test_af_push.c` ПОСЛЕ включений `af_test.h` (макросы CHECK(cond), RUN(fn); глобальный счётчик g_failures), `<stdio.h> <stdlib.h> <string.h> <math.h> <stdint.h>` и `af_push.h` (а он тянет af_rec.h, af_env.h, af_hist_query.h, cJSON.h). Файл `af_ring.h` включи сам (`#include "af_ring.h"`). Все функции `static`; все функции, которые не будут использованы, помечай `__attribute__((unused))`.

## Данные
Тестовые данные (боевые выгрузки платы AtomFast, MAC обезличен) лежат в файлах относительно текущего каталога: `data/e8_d1_raw.csv` (шапка + 1200 строк), `data/e8_d1_agg.csv` (шапка + 365 строк), `data/e8_d1_env.csv` (шапка + 291 строка). Формат строки raw: `mac,seq,epoch,ts,kind,dose_msv,doserate_usvh,pulses_2s,cps,battery,temp_c,flags,n` (пример: `02:af:00:00:00:01,0,21,1792859805,0,0.192411,0.0824327,35,17.5,27,26,0,1`), agg: те же колонки, где kind — период в секундах (60), n — число отсчётов (пример: `02:af:00:00:00:01,0,21,1792859760,60,0.192412,0.0795502,35.75,17.875,27,26,0,8`), env: `mac,seq,epoch,ts,kind,temp_c,press_mmhg,press_pa,flags` (пример: `02:af:00:00:00:01,289,21,1792881952,env,25.44,758.51,101126,3`).

## Макросы и глобальные массивы (дословно)
```
#define D1_MAC "02:af:00:00:00:01"
#define D1_EPOCH 21u
#define L_MAX 1300
#define L_LEN 200
static char L_RAW[L_MAX][L_LEN];
static char L_AGG[L_MAX][L_LEN];
static char L_ENV[L_MAX][L_LEN];
static int N_RAW, N_AGG, N_ENV;
```

## Функции (ровно эти имена и сигнатуры)

`static int ld_lines(const char *path, char (*out)[L_LEN], int max)` — читает файл построчно, пропускает первую (шапку), убирает '\n' и '\r' в конце, копирует строку в out[i] (длина строки меньше L_LEN, иначе усечь), возвращает число прочитанных строк (0 если файл не открылся; тогда напечатать `FAIL ld_lines <path>:0: open` и увеличить g_failures).

`static void ld_all(void)` — один раз (повторные вызовы ничего не делают, статический флаг) загрузить три файла в L_RAW/L_AGG/L_ENV и заполнить N_RAW, N_AGG, N_ENV.

`static void mk_raw(const char *line, af_raw_rec_t *r)` — разобрать строку raw (через strtok_r/ручной разбор по запятым; допустимо sscanf с форматом `%*[^,],%*u,%*u,%u,%*u,%f,%f,%u,%*f,%d,%d,%u`): ts, dose_msv, doserate_usvh, pulses_2s (целое), battery, temp_c, flags. Заполнить `af_raw_rec_t`: поля r->ts и r->s.{flags, dose_msv, doserate_usvh, pulses_2s, cps = pulses_2s/2.0f, battery, temp_c}; всё остальное обнулить.

`static void mk_agg(const char *line, af_agg_rec_t *r)` — разобрать строку agg: ts, period (kind), dose, rate, pulses_2s (float), cps (пропустить), battery, temp_c, flags, n. Заполнить `af_agg_rec_t`: ts, period_s, n, dose_msv, doserate_usvh, pulses_2s, battery, temp_c, flags; agg_flags=0, modes=0.

`static void mk_env(const char *line, af_env_rec_t *r)` — разобрать строку env: ts, temp_c (double), press_mmhg (пропустить), press_pa (целое), flags. Заполнить `af_env_rec_t`: memset 0; ts; t_c100 = (int16_t)lround(temp_c*100); p_pa = press_pa; flags; h_c100=0; sensor=1; model=0.

`typedef struct { af_ring_t ring[3]; uint8_t buf[3][4096*28]; int busy; } store_t;` (в статической памяти; буфера с запасом) и функции:
- `static void st_init(store_t *st, uint32_t cap_raw, uint32_t cap_agg, uint32_t cap_env)` — memset 0; кольца: `af_ring_init(&st->ring[0], st->buf[0], (size_t)cap_raw*AF_REC_RAW_SIZE, AF_REC_RAW_SIZE, D1_EPOCH, AF_RING_OVERWRITE)`, `ring[1]` с AF_REC_AGG_SIZE и cap_agg, `ring[2]` с AF_REC_ENV_SIZE и cap_env. cap_* не превышают 4096.
- `static void st_fill(store_t *st, int nraw, int nagg, int nenv)` — вызвать ld_all(); положить в кольца первые nraw/nagg/nenv записей из L_RAW/L_AGG/L_ENV (mk_* → af_raw_encode/af_agg_encode/af_env_encode → af_ring_push). Если запрошено больше, чем N_*, взять сколько есть.
- `static int fk_info(void *ctx, int kind, uint32_t *oldest, uint32_t *head, uint32_t *epoch)` — ctx это store_t*; если st->busy — вернуть -1; иначе *oldest = af_ring_oldest_seq(&ring[kind]), *head = ring[kind].seq, *epoch = ring[kind].epoch; вернуть 0.
- `static uint32_t fk_read(void *ctx, int kind, uint32_t since, void *out, uint32_t max, uint32_t *next, uint32_t *lost)` — вернуть `af_ring_read_since(&st->ring[kind], since, out, max, next, lost)` (если st->busy — *next=since, *lost=0 и вернуть 0).

`static int cmp_line(const char *got, const char *want_csv_line)` — сравнивает строку тела `got` (до '\n', без него) с ожидаемой строкой выгрузки `want_csv_line`; возвращает 1 если равны побайтно (длина тоже), иначе 0.

`static int body_line(const char *body, int idx, char *out, size_t cap)` — копирует idx-ю строку тела (0 — шапка, 1 — первая запись) без '\n' в out; возвращает длину или -1, если строки нет.

`static int body_lines(const char *body)` — число строк тела (число '\n').

## Фейковый приёмник для проверок af_push_send_ring
```
typedef struct {
    store_t *st;
    int http;            /* код, который вернёт post */
    int transport;       /* !=0 - post вернёт ошибку транспорта */
    char resp[160];      /* тело ответа; если пустое - сформировать {"accepted":rows,"last_seq":last} автоматически (см. ниже) */
    int auto_ok;         /* 1 - при http==200 и пустом resp формировать ответ из последней отправленной пачки */
    int calls;
    uint32_t last_first_seq, last_lost, last_epoch, last_head;
    char last_mac[24];
    char last_body[16384];
    size_t last_len;
    uint32_t now;
} fk_t;
```
`static int fk_post(void *ctx, int kind, const char *mac, uint32_t head, uint32_t epoch, uint32_t first_seq, uint32_t lost, const char *body, size_t len, int *http, char *resp, size_t resp_cap)` — ctx это fk_t*; calls++; запомнить аргументы (first_seq, lost, epoch, head, mac, тело — копия не более 16383 байт, last_len); *http = f->http; если f->transport — вернуть 1 (resp = пустая строка); иначе если f->resp не пустая — скопировать её в resp; иначе если f->auto_ok и http==200 — сформировать `{"accepted":N,"last_seq":S}`, где N = число строк данных в теле (число '\n' минус 1), S = seq последней строки тела (взять второе поле последней строки; если строк данных нет — first_seq-1 как знаковое); вернуть 0.
`static uint32_t fk_now(void *ctx)` — вернуть ((fk_t*)ctx)->now.
`static const af_push_ops_t FK_OPS = { fk_info_wrap, fk_read_wrap, fk_post, fk_now };` — но fk_info/fk_read ожидают store_t*, а ctx здесь fk_t*: напиши обёртки `fk_info_wrap`/`fk_read_wrap`, которые берут `((fk_t*)ctx)->st` и вызывают fk_info/fk_read с ним.

Больше ничего не добавляй (ни main, ни тестов).
