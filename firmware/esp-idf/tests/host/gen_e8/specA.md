Ты пишешь ЧАСТЬ 1 заголовочного файла `main/af_push.h` прошивки ESP32 (язык C11, gcc с `-Wall -Wextra -Werror -O2`). Верни ТОЛЬКО код C, без пояснений и без markdown-ограждений. Комментарии короткие, по-русски. Всё `static inline`, без malloc, без зависимостей от ESP-IDF, без глобальных переменных.

Файл начнётся с текста, который ты выдашь первым (ничего до него нет). Выдай ровно в таком порядке.

## 1. Начало файла (дословно)
```
#ifndef AF_PUSH_H
#define AF_PUSH_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "cJSON.h"
#include "af_rec.h"
#include "af_env.h"
#include "af_hist_query.h"
#include "af_ble_nvs.h"
#include "af_sens_req.h"
```
Далее дословно (макросы):
```
#define AF_PUSH_HOST_MAX 64
#define AF_PUSH_TOKEN_MAX 65
#define AF_PUSH_PORT_DEF 8480
#define AF_PUSH_INTERVAL_MIN 10
#define AF_PUSH_INTERVAL_MAX 3600
#define AF_PUSH_INTERVAL_DEF 60
#define AF_PUSH_BATCH_MIN 50
#define AF_PUSH_BATCH_MAX 2000
#define AF_PUSH_BATCH_DEF 500
#define AF_PUSH_TIMEOUT_MIN 2
#define AF_PUSH_TIMEOUT_MAX 30
#define AF_PUSH_TIMEOUT_DEF 8
#define AF_PUSH_RINGS 3
#define AF_PUSH_RING_ALL 7
#define AF_PUSH_BATCH_FLOOR 10
#define AF_PUSH_BODY_MAX 16384
#define AF_PUSH_ROW_MAX 160
#define AF_PUSH_CFG_BLOB 152
#define AF_PUSH_CUR_BLOB 48
#define AF_PUSH_CUR_SAVE_MS 30000u
#define AF_PUSH_BACKOFF_BASE_MS 2000u
#define AF_PUSH_BACKOFF_CAP_MS 300000u

#define AF_PUSH_E_BODY -1
#define AF_PUSH_E_JSON -2
#define AF_PUSH_E_KEY -3
#define AF_PUSH_E_VALUE -4
#define AF_PUSH_E_INCOMPLETE -5

typedef struct {
    uint8_t enabled;
    uint8_t rings;       /* маска: бит0 raw, бит1 agg, бит2 env */
    uint16_t port;
    uint16_t interval_s;
    uint16_t batch_rows;
    uint8_t timeout_s;
    char host[AF_PUSH_HOST_MAX];
    char token[AF_PUSH_TOKEN_MAX];
} af_push_cfg_t;

typedef struct {
    af_push_cfg_t cfg;       /* итоговые настройки после слияния */
    uint8_t token_changed;   /* токен сменён или очищен */
    uint8_t dest_changed;    /* host или port изменились относительно текущих */
} af_push_cmd_t;
```

## 2. Функции (реализуй ровно с этими именами и сигнатурами)

`static inline void af_push_cfg_default(af_push_cfg_t *c)` — memset 0; enabled=0; rings=AF_PUSH_RING_ALL; port=AF_PUSH_PORT_DEF; interval_s=AF_PUSH_INTERVAL_DEF; batch_rows=AF_PUSH_BATCH_DEF; timeout_s=AF_PUSH_TIMEOUT_DEF; host и token пустые. При c==NULL ничего не делать.

`static inline int af_push_host_ok(const char *s)` — 1 если годится, 0 если нет. Правила: s не NULL; длина 1..63; символы только [A-Za-z0-9.-]; первый и последний символ — буква или цифра; нет подряд идущих точек ("..") ; если строка состоит ТОЛЬКО из цифр и точек, она обязана быть корректным IPv4: ровно 4 десятичных октета, каждый 1..3 цифры, значение 0..255, без ведущих нулей (октет "0" допустим, "01" и "00" нет). Иначе (содержит букву или дефис) — имя хоста, допустимо.

`static inline int af_push_token_ok(const char *s)` — 1 если s не NULL, длина 1..64 и каждый символ в диапазоне 0x21..0x7E, кроме `\` (0x5C) и `"` (0x22). Иначе 0.

`static inline int af_push_addr_class(const char *host)` — классификация адреса для предупреждения: если host не корректный IPv4-литерал (по тем же правилам разбора из af_push_host_ok), вернуть 2 (имя, класс неизвестен). Для IPv4: вернуть 1 для частных/локальных диапазонов: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 127.0.0.0/8, 169.254.0.0/16; для остальных вернуть 0 (публичный). NULL или пустая строка — вернуть 2. Разбор IPv4 вынеси во вспомогательную функцию `static inline int af_push_ipv4(const char *s, uint8_t o[4])` (0 — ок, -1 — не IPv4 или неверно) и используй её и в af_push_host_ok.

`static inline int af_push_cfg_valid(const af_push_cfg_t *c)` — 0 если все поля в допустимых пределах, иначе AF_PUSH_E_VALUE. Проверки: c не NULL; enabled 0 или 1; rings & ~AF_PUSH_RING_ALL == 0; port 1..65535 (то есть != 0); interval_s в [AF_PUSH_INTERVAL_MIN, AF_PUSH_INTERVAL_MAX]; batch_rows в [AF_PUSH_BATCH_MIN, AF_PUSH_BATCH_MAX]; timeout_s в [AF_PUSH_TIMEOUT_MIN, AF_PUSH_TIMEOUT_MAX]; host: либо пустая строка, либо af_push_host_ok; token: либо пустая строка, либо af_push_token_ok; оба массива обязаны иметь завершающий NUL внутри своего размера (memchr по NUL); после первого NUL в host и token — до конца массива одни нули (иначе мусор — вернуть ошибку).

`static inline int af_push_cfg_complete(const af_push_cfg_t *c)` — 1 если !enabled; если enabled: host непустой И token непустой И rings != 0 → 1, иначе 0.

`static inline int af_push_parse(const char *body, size_t n, const af_push_cfg_t *cur, af_push_cmd_t *cmd)` — разбор тела POST /api/push. Возвращает 0 или код ошибки. Порядок:
1. cmd NULL или cur NULL → AF_PUSH_E_BODY. memset(cmd,0). Если !af_sens_body_ok(body,n) → AF_PUSH_E_BODY (эта функция уже запрещает обратную косую, NUL, управляющие символы и не-ASCII).
2. cJSON_ParseWithLength(body,n); NULL → AF_PUSH_E_JSON; не объект → удалить, AF_PUSH_E_JSON.
3. Допустимые ключи ровно: "enabled","host","port","token","clear_token","interval_s","batch_rows","timeout_s","rings", NULL (массив const char *const[]). Проверка через уже существующую функцию `af_sens_keys_ok(root, allowed)` (она отвергает неизвестные и повторные ключи); если 0 → AF_PUSH_E_KEY.
4. cmd->cfg = *cur (патч-семантика: изменяются только присланные поля). Для каждого присланного ключа:
   - enabled, clear_token: обязаны быть булевыми (cJSON_IsBool), иначе AF_PUSH_E_VALUE;
   - host: строка; пустая строка допустима (очищает хост); непустая — через af_push_host_ok, иначе VALUE; копировать в cfg.host (длина уже проверена ≤63);
   - port, interval_s, batch_rows, timeout_s: числа, целые, в своих пределах, проверка через `af_sens_num(item, lo, hi, 1, &v)` (она есть в af_sens_req.h; возвращает 0 при успехе); порт 1..65535;
   - token: строка; пустая строка = «не менять»; непустая — af_push_token_ok иначе VALUE; копировать в cfg.token; token_changed=1;
   - rings: массив из 1..3 строк, каждая "raw"/"agg"/"env", без повторов, иначе VALUE; результат — маска; пустой массив запрещён (VALUE).
   - Если clear_token==true и одновременно пришёл непустой token → VALUE. clear_token==true без токена: cfg.token обнулить (memset), token_changed=1.
5. dest_changed = (strcmp(host_new, cur->host) != 0) || (port_new != cur->port).
6. Итоговый cfg: если af_push_cfg_valid(&cmd->cfg) != 0 → AF_PUSH_E_VALUE; если !af_push_cfg_complete(&cmd->cfg) → AF_PUSH_E_INCOMPLETE.
7. На всех путях выхода cJSON_Delete(root) ровно один раз.

`static inline size_t af_push_cfg_encode(const af_push_cfg_t *c, uint8_t *buf, size_t cap)` — упаковка в blob ровно AF_PUSH_CFG_BLOB (152) байт; вернуть 152, при c/buf NULL или cap<152 вернуть 0. Раскладка (смещения): 0..3 магия 'A','P','S','1'; 4..5 версия 1,0; 6..7 длина полезной части 0x8C,0x00 (140); 8 enabled; 9 rings; 10..11 port (младший байт первым); 12..13 interval_s; 14..15 batch_rows; 16 timeout_s; 17 резерв 0; 18..81 host (64 байта, копия массива целиком); 82..146 token (65 байт); 147 резерв 0; 148..151 CRC32 по байтам [0..147], младший байт первым. CRC считать готовой функцией `af_bsn_crc32(const uint8_t *p, size_t n)` из af_ble_nvs.h (не писать свою). Перед заполнением memset(buf,0,152).

`static inline bool af_push_cfg_decode(af_push_cfg_t *c, const uint8_t *buf, size_t len)` — обратная операция. При ЛЮБОЙ ошибке (c или buf NULL; len != 152; магия; версия; длина полезной части; CRC; af_push_cfg_valid() вернул не 0 на разобранных полях; байты 17 и 147 не нули) — af_push_cfg_default(c) и вернуть false (при c==NULL просто false). При успехе заполнить c и вернуть true. Порядок проверок: NULL/len, магия, версия, длина, CRC, затем разбор полей и af_push_cfg_valid.

Больше ничего не добавляй: ни `#endif`, ни других функций (файл продолжат другие части).
