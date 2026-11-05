#ifndef AF_LOG_H
#define AF_LOG_H
/* e16-LOG: чистая логика журнала событий платы (кольцо, схлопывание, лимитер, формат, ротация). Без ESP-IDF, хост-тестируема. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define AF_LOG_INFO 1
#define AF_LOG_WARN 2
#define AF_LOG_ERROR 3
#define AF_LOG_RING_N 200u
#define AF_LOG_RING_FALLBACK_N 16u
#define AF_LOG_TAG_MAX 8
#define AF_LOG_MSG_MAX 96
#define AF_LOG_RECENT_N 8
#define AF_LOG_COLLAPSE_WIN_S 600u
#define AF_LOG_BUCKET_CAP 30u
#define AF_LOG_BUCKET_REFILL_S 1u
#define AF_LOG_ERR_BUCKET_CAP 10u
#define AF_LOG_ERR_BUCKET_REFILL_S 6u
#define AF_LOG_R_NEW 0
#define AF_LOG_R_COLLAPSED 1
#define AF_LOG_R_DROPPED (-1)

typedef struct {
    uint32_t seq;
    uint32_t mono_s;
    uint32_t last_s;
    uint32_t utc;
    uint16_t count;
    uint8_t level;
    uint8_t rsv;
    char tag[AF_LOG_TAG_MAX];
    char msg[AF_LOG_MSG_MAX];
} af_log_ent_t;

typedef struct {
    uint32_t h;
    uint32_t seq;
} af_log_recent_t;

typedef struct {
    af_log_ent_t *ring;
    uint32_t cap;
    uint32_t next_seq;
    uint32_t flushed;
    uint32_t barrier;
    uint32_t lost;
    uint32_t suppressed;
    uint32_t supp_pending;
    uint32_t collapsed;
    uint8_t urgent;
    uint8_t rpos;
    af_log_recent_t recent[AF_LOG_RECENT_N];
    uint32_t tok;
    uint32_t tok_t;
    uint32_t etok;
    uint32_t etok_t;
} af_log_core_t;

static inline int af_log_core_init(af_log_core_t *c, af_log_ent_t *ring, uint32_t cap, uint32_t first_seq) {
    if (c == NULL || ring == NULL || cap < 2) return -1;
    memset(c, 0, sizeof(*c));
    memset(ring, 0, sizeof(af_log_ent_t) * cap);
    if (first_seq < 1) first_seq = 1;
    c->ring = ring;
    c->cap = cap;
    c->next_seq = first_seq;
    c->flushed = first_seq;
    c->barrier = first_seq;
    c->tok = AF_LOG_BUCKET_CAP;
    c->etok = AF_LOG_ERR_BUCKET_CAP;
    return 0;
}

static inline uint32_t af_log_fnv(uint8_t level, const char *tag, const char *msg) {
    uint32_t h = 2166136261u;
    h ^= level; h *= 16777619u;
    h ^= 0x1F; h *= 16777619u;
    while (*tag) { h ^= (uint8_t)*tag++; h *= 16777619u; }
    h ^= 0x1F; h *= 16777619u;
    while (*msg) { h ^= (uint8_t)*msg++; h *= 16777619u; }
    return h;
}

static inline void af_log_sanitize(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    if (src == NULL) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (src[i] != '\0' && i < cap - 1) {
        unsigned char b = (unsigned char)src[i];
        if (b < 0x20 || b == 0x7F) dst[i] = ' ';
        else dst[i] = (char)b;
        i++;
    }
    if (src[i] != '\0' && ((unsigned char)src[i] & 0xC0) == 0x80) {
        /* первый отброшенный байт - продолжение: срез внутри символа, откат до ведущего байта */
        {
            size_t j = i;
            while (j > 0 && ((unsigned char)dst[j - 1] & 0xC0) == 0x80) j--;
            // dst[j-1] должно быть lead byte (110xxxxx или 1110xxxx или 11110xxx)
            if (j > 0) {
                unsigned char lead = (unsigned char)dst[j - 1];
                if ((lead & 0xE0) == 0xC0 || (lead & 0xF0) == 0xE0 || (lead & 0xF8) == 0xF0) {
                    i = j - 1;
                }
            }
        }
    }
    dst[i] = '\0';
}

static inline int af_log__is_word_char(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static inline int af_log__is_hex(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline int af_log__is_digit(unsigned char c) {
    return (c >= '0' && c <= '9');
}

static inline void af_log_redact(char *s) {
    if (s == NULL) return;
    size_t len = strlen(s);
    if (len == 0) return;

    // (a) Секреты
    const char *keywords[] = {"pass", "pwd", "token", "secret", "key", "auth"};
    size_t nkw = sizeof(keywords) / sizeof(keywords[0]);
    
    for (size_t i = 0; i < len; i++) {
        // Проверка начала слова
        if (i > 0 && af_log__is_word_char((unsigned char)s[i-1])) continue;
        
        for (size_t k = 0; k < nkw; k++) {
            size_t klen = strlen(keywords[k]);
            if (i + klen > len) continue;
            
            // Сравнение без учета регистра
            int match = 1;
            for (size_t j = 0; j < klen; j++) {
                unsigned char a = (unsigned char)s[i+j];
                unsigned char b = (unsigned char)keywords[k][j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = 0; break; }
            }
            if (!match) continue;
            
            // Остаток слова пропускаем (password, token_id), затем пробелы и ':' или '='
            size_t p = i + klen;
            while (p < len && af_log__is_word_char((unsigned char)s[p])) p++;
            while (p < len && s[p] == ' ') p++;
            if (p >= len || (s[p] != ':' && s[p] != '=')) continue;
            p++; // пропускаем ':' или '='
            
            // Пропускаем пробелы
            while (p < len && s[p] == ' ') p++;
            
            // Ищем значение
            size_t start_val = p;
            while (p < len) {
                unsigned char c = (unsigned char)s[p];
                if (c == ' ' || c == ',' || c == ';' || c == '&' || c == '"' || c == '\'') break;
                p++;
            }
            
            if (p > start_val) {
                for (size_t j = start_val; j < p; j++) s[j] = '*';
                i = p - 1; // продолжаем после значения
            }
            break; // одно совпадение на позицию
        }
    }

    // (b) MAC адреса
    for (size_t i = 0; i + 17 <= len; i++) {
        // Проверка границ
        if (i > 0 && af_log__is_word_char((unsigned char)s[i-1])) continue;
        if (i + 17 < len && af_log__is_word_char((unsigned char)s[i+17])) continue;
        
        int is_mac = 1;
        for (int j = 0; j < 17; j++) {
            if (j % 3 != 2) {
                if (!af_log__is_hex((unsigned char)s[i+j])) { is_mac = 0; break; }
            } else {
                if (s[i+j] != ':' && s[i+j] != '-') { is_mac = 0; break; }
            }
        }
        
        if (is_mac) {
            for (int j = 0; j < 17; j++) s[i+j] = '*';
            i += 16; // пропускаем
        }
    }

    // (c) IPv4 адреса
    for (size_t i = 0; i < len; i++) {
        if (!af_log__is_digit((unsigned char)s[i])) continue;
        
        // Проверка предыдущего символа
        if (i > 0 && (af_log__is_digit((unsigned char)s[i-1]) || s[i-1] == '.')) continue;
        
        // Пробуем распознать 4 группы
        size_t p = i;
        int groups = 0;
        int valid = 1;
        
        for (int g = 0; g < 4; g++) {
            // Группа цифр
            size_t start = p;
            while (p < len && af_log__is_digit((unsigned char)s[p])) p++;
            if (p == start) { valid = 0; break; }
            if (p - start > 3) { valid = 0; break; }
            groups++;
            
            if (g < 3) {
                if (p >= len || s[p] != '.') { valid = 0; break; }
                p++;
            }
        }
        
        if (valid && groups == 4) {
            // Проверка следующего символа
            if (p < len && (af_log__is_digit((unsigned char)s[p]) || s[p] == '.')) {
                // Если после '.' идет цифра, это не IP
                if (s[p] == '.' && p + 1 < len && af_log__is_digit((unsigned char)s[p+1])) {
                    valid = 0;
                } else if (af_log__is_digit((unsigned char)s[p])) {
                    valid = 0;
                }
            }
            
            if (valid) {
                for (size_t j = i; j < p; j++) s[j] = '*';
                i = p - 1;
            }
        }
    }
}

static inline int af_log__same(const af_log_ent_t *e, uint8_t level, const char *tag, const char *msg) {
    return e->level == level && strcmp(e->tag, tag) == 0 && strcmp(e->msg, msg) == 0;
}

static inline void af_log__refill(uint32_t *tok, uint32_t *t, uint32_t now, uint32_t cap, uint32_t refill_s) {
    if (now < *t) {
        *t = now;
        return;
    }
    uint64_t elapsed = (uint64_t)now - (uint64_t)*t;
    uint64_t add = elapsed / refill_s;
    if (add > (uint64_t)cap) add = cap;
    
    uint64_t new_tok = (uint64_t)*tok + add;
    if (new_tok > (uint64_t)cap) new_tok = cap;
    *tok = (uint32_t)new_tok;
    
    if (*tok == cap) {
        *t = now;
    } else {
        *t = (uint32_t)(*t + add * refill_s);
    }
}

static inline af_log_ent_t *af_log__add(af_log_core_t *c, uint8_t level, const char *tag, const char *msg, uint32_t h, uint32_t mono_s, uint32_t utc) {
    uint32_t seq = c->next_seq++;
    af_log_ent_t *slot = &c->ring[seq % c->cap];
    
    if (slot->seq != 0 && slot->seq >= c->flushed) {
        c->lost++;
        c->flushed = slot->seq + 1;
        if (c->barrier < c->flushed) c->barrier = c->flushed;
    }
    
    memset(slot, 0, sizeof(*slot));
    slot->seq = seq;
    slot->mono_s = mono_s;
    slot->last_s = mono_s;
    slot->utc = utc;
    slot->count = 1;
    slot->level = level;
    
    // Копирование tag
    size_t tl = strlen(tag);
    if (tl >= AF_LOG_TAG_MAX) tl = AF_LOG_TAG_MAX - 1;
    memcpy(slot->tag, tag, tl);
    slot->tag[tl] = '\0';
    
    // Копирование msg
    size_t ml = strlen(msg);
    if (ml >= AF_LOG_MSG_MAX) ml = AF_LOG_MSG_MAX - 1;
    memcpy(slot->msg, msg, ml);
    slot->msg[ml] = '\0';
    
    c->recent[c->rpos].h = h;
    c->recent[c->rpos].seq = seq;
    c->rpos = (c->rpos + 1) % AF_LOG_RECENT_N;
    
    if (level == AF_LOG_ERROR) c->urgent = 1;
    
    return slot;
}

static inline int af_log_push(af_log_core_t *c, int level, const char *tag, const char *msg, uint32_t mono_s, uint32_t utc) {
    if (c == NULL || c->ring == NULL) return AF_LOG_R_DROPPED;
    
    if (level < 1) level = 1;
    if (level > 3) level = 3;
    
    char tg[AF_LOG_TAG_MAX];
    char ms[AF_LOG_MSG_MAX];
    
    if (tag == NULL || tag[0] == '\0') {
        strcpy(tg, "-");
    } else {
        af_log_sanitize(tg, AF_LOG_TAG_MAX, tag);
    }
    
    if (msg == NULL) {
        ms[0] = '\0';
    } else {
        af_log_sanitize(ms, AF_LOG_MSG_MAX, msg);
        af_log_redact(ms);
    }
    
    uint32_t h = af_log_fnv((uint8_t)level, tg, ms);
    
    // Шаг 1: COLLAPSE
    for (int i = 0; i < AF_LOG_RECENT_N; i++) {
        af_log_recent_t *r = &c->recent[i];
        if (r->seq == 0 || r->h != h) continue;
        if (r->seq >= c->next_seq) continue;
        
        af_log_ent_t *e = &c->ring[r->seq % c->cap];
        if (e->seq != r->seq) continue;
        if (e->seq < c->flushed) continue;
        if (e->seq < c->barrier) continue;
        if (!af_log__same(e, (uint8_t)level, tg, ms)) continue;
        if (mono_s < e->last_s) continue;
        if ((mono_s - e->last_s) >= AF_LOG_COLLAPSE_WIN_S) continue;
        
        if (e->count < 65535) e->count++;
        e->last_s = mono_s;
        c->collapsed++;
        return AF_LOG_R_COLLAPSED;
    }
    
    // Шаг 2: Rate limit
    af_log__refill(&c->tok, &c->tok_t, mono_s, AF_LOG_BUCKET_CAP, AF_LOG_BUCKET_REFILL_S);
    af_log__refill(&c->etok, &c->etok_t, mono_s, AF_LOG_ERR_BUCKET_CAP, AF_LOG_ERR_BUCKET_REFILL_S);
    
    if (level == AF_LOG_ERROR) {
        if (c->etok == 0) {
            c->suppressed++;
            c->supp_pending++;
            return AF_LOG_R_DROPPED;
        }
        c->etok--;
    } else {
        if (c->tok == 0) {
            c->suppressed++;
            c->supp_pending++;
            return AF_LOG_R_DROPPED;
        }
        c->tok--;
    }
    
    // Шаг 3: Notice
    if (c->supp_pending > 0 && c->tok >= 1) {
        char ms2[AF_LOG_MSG_MAX];
        snprintf(ms2, AF_LOG_MSG_MAX, "suppressed %u events", (unsigned)c->supp_pending);
        uint32_t h2 = af_log_fnv(AF_LOG_WARN, "log", ms2);
        af_log__add(c, AF_LOG_WARN, "log", ms2, h2, mono_s, utc);
        c->tok--;
        c->supp_pending = 0;
    }
    
    // Шаг 4: Add
    af_log__add(c, (uint8_t)level, tg, ms, h, mono_s, utc);
    return AF_LOG_R_NEW;
}

static inline uint32_t af_log_pending(const af_log_core_t *c) {
    if (c == NULL) return 0;
    if (c->flushed >= c->next_seq) return 0;
    return c->next_seq - c->flushed;
}

static inline const af_log_ent_t *af_log_get(const af_log_core_t *c, uint32_t seq) {
    if (c == NULL || seq == 0 || seq >= c->next_seq) return NULL;
    const af_log_ent_t *e = &c->ring[seq % c->cap];
    if (e->seq != seq) return NULL;
    return e;
}

static inline uint32_t af_log_snapshot(af_log_core_t *c) {
    if (c == NULL) return 0;
    c->barrier = c->next_seq;
    return c->next_seq;
}

static inline void af_log_mark_flushed(af_log_core_t *c, uint32_t upto) {
    if (upto > c->next_seq) upto = c->next_seq;
    if (upto > c->flushed) c->flushed = upto;
    if (c->barrier < c->flushed) c->barrier = c->flushed;
    if (c->flushed >= c->next_seq) c->urgent = 0;
}

static inline void af_log_abort_flush(af_log_core_t *c) {
    c->barrier = c->flushed;
}

#define AF_LOG_LINE_MAX 192
#define AF_LOG_FLUSH_INTERVAL_S 60u
#define AF_LOG_URGENT_MIN_S 30u
#define AF_LOG_SEG_BYTES 16384u
#define AF_LOG_MAX_TOTAL 262144u
#define AF_LOG_MAX_FILES 24u
#define AF_LOG_RESERVE_PCT 25u
#define AF_LOG_FS_BLOCK 4096u
#define AF_LOG_Q_DEF_LIMIT 100u
#define AF_LOG_Q_MAX_LIMIT 500u

static inline const char *af_log_level_name(int level) {
    switch (level) {
        case 1: return "info";
        case 2: return "warn";
        case 3: return "error";
        default: return "info";
    }
}

static inline int af_log_level_parse(const char *s) {
    if (!s || !s[0]) return 0;
    if (s[0] == '1' && s[1] == '\0') return 1;
    if (s[0] == '2' && s[1] == '\0') return 2;
    if (s[0] == '3' && s[1] == '\0') return 3;
    if (s[0] == 'i' && s[1] == 'n' && s[2] == 'f' && s[3] == 'o' && s[4] == '\0') return 1;
    if (s[0] == 'w' && s[1] == 'a' && s[2] == 'r' && s[3] == 'n' && s[4] == '\0') return 2;
    if (s[0] == 'e' && s[1] == 'r' && s[2] == 'r' && s[3] == 'o' && s[4] == 'r' && s[5] == '\0') return 3;
    return 0;
}

static inline int af_log_fmt_store(const af_log_ent_t *e, char *buf, size_t cap) {
    if (!e || !buf || cap == 0) return -1;
    char lvl = 'I';
    if (e->level == 2) lvl = 'W';
    else if (e->level == 3) lvl = 'E';
    int n = snprintf(buf, cap, "%u\t%u\t%u\t%u\t%c\t%s\t%u\t%s\n",
                     (unsigned)e->seq, (unsigned)e->mono_s, (unsigned)e->last_s,
                     (unsigned)e->utc, lvl, e->tag, (unsigned)e->count, e->msg);
    if (n < 0 || (size_t)n >= cap) return -1;
    return n;
}

static inline int af_log_parse_store(const char *line, size_t len, af_log_ent_t *e) {
    if (!line || !e) return -1;
    if (len == 0) return -1;
    size_t end = len;
    if (line[end - 1] == '\n') end--;
    if (end == 0) return -1;

    size_t starts[8];
    int n_fields = 0;
    size_t pos = 0;
    while (pos < end) {
        if (n_fields >= 8) return -1;
        starts[n_fields++] = pos;
        while (pos < end && line[pos] != '\t') pos++;
        if (pos < end) pos++;
    }
    if (n_fields == 7 && line[end - 1] == '\t') { starts[7] = end; n_fields = 8; } /* пустое сообщение */
    if (n_fields != 8) return -1;

    size_t lens[8];
    for (int i = 0; i < 8; i++) {
        size_t next = (i < 7) ? starts[i + 1] : end;
        lens[i] = next - starts[i] - ((i < 7) ? 1 : 0); /* без разделителя */
    }
    if (lens[4] != 1) return -1;

    uint32_t vals[5];
    for (int i = 0; i < 5; i++) {
        int idx = (i < 4) ? i : 6;
        size_t l = lens[idx];
        if (l == 0 || l > 10) return -1;
        uint64_t v = 0;
        for (size_t j = 0; j < l; j++) {
            char c = line[starts[idx] + j];
            if (c < '0' || c > '9') return -1;
            v = v * 10 + (uint64_t)(c - '0');
            if (v > 4294967295ULL) return -1;
        }
        vals[i] = (uint32_t)v;
    }

    if (vals[0] < 1) return -1;
    if (vals[4] < 1 || vals[4] > 65535) return -1;

    char lvl_c = line[starts[4]];
    int lvl = 0;
    if (lvl_c == 'I') lvl = 1;
    else if (lvl_c == 'W') lvl = 2;
    else if (lvl_c == 'E') lvl = 3;
    else return -1;

    size_t tag_l = lens[5];
    if (tag_l < 1 || tag_l > 7) return -1;
    for (size_t j = 0; j < tag_l; j++) {
        unsigned char c = (unsigned char)line[starts[5] + j];
        if (c < 0x20) return -1;
    }

    size_t msg_l = lens[7];
    if (msg_l > 95) return -1;
    for (size_t j = 0; j < msg_l; j++) {
        unsigned char c = (unsigned char)line[starts[7] + j];
        if (c < 0x20) return -1;
    }

    memset(e, 0, sizeof(*e));
    e->seq = vals[0];
    e->mono_s = vals[1];
    e->last_s = vals[2];
    e->utc = vals[3];
    e->count = (uint16_t)vals[4];
    e->level = (uint8_t)lvl;
    memcpy(e->tag, line + starts[5], tag_l);
    e->tag[tag_l] = '\0';
    memcpy(e->msg, line + starts[7], msg_l);
    e->msg[msg_l] = '\0';
    return 0;
}

static inline int af_log_json_esc(char *dst, size_t cap, const char *src) {
    if (!dst || !src || cap == 0) return -1;
    size_t pos = 0;
    for (const char *p = src; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            if (pos + 2 > cap - 1) return -1;
            dst[pos++] = '\\';
            dst[pos++] = c;
        } else if (c < 0x20) {
            if (pos + 6 > cap - 1) return -1;
            dst[pos++] = '\\';
            dst[pos++] = 'u';
            dst[pos++] = '0';
            dst[pos++] = '0';
            char hex1 = (c >> 4) & 0xF;
            char hex2 = c & 0xF;
            dst[pos++] = (char)(hex1 < 10 ? '0' + hex1 : 'a' + hex1 - 10);
            dst[pos++] = (char)(hex2 < 10 ? '0' + hex2 : 'a' + hex2 - 10);
        } else {
            if (pos + 1 > cap - 1) return -1;
            dst[pos++] = *p;
        }
    }
    dst[pos] = '\0';
    return (int)pos;
}

static inline int af_log_fmt_json(const af_log_ent_t *e, char *buf, size_t cap) {
    if (!e || !buf || cap == 0) return -1;
    char tag_esc[2 * AF_LOG_TAG_MAX + 8];
    char msg_esc[2 * AF_LOG_MSG_MAX + 8];
    if (af_log_json_esc(tag_esc, sizeof(tag_esc), e->tag) < 0) return -1;
    if (af_log_json_esc(msg_esc, sizeof(msg_esc), e->msg) < 0) return -1;
    int n = snprintf(buf, cap, "{\"seq\":%u,\"t\":%u,\"t2\":%u,\"utc\":%u,\"lvl\":\"%s\",\"tag\":\"%s\",\"n\":%u,\"msg\":\"%s\"}\n",
                     (unsigned)e->seq, (unsigned)e->mono_s, (unsigned)e->last_s, (unsigned)e->utc,
                     af_log_level_name(e->level), tag_esc, (unsigned)e->count, msg_esc);
    if (n < 0 || (size_t)n >= cap) return -1;
    return n;
}

static inline int af_log_iso(uint32_t utc, char out[21]) {
    if (utc == 0) {
        out[0] = '-';
        out[1] = '\0';
        return 1;
    }
    uint32_t days = utc / 86400u;
    uint32_t secs = utc % 86400u;
    uint32_t hh = secs / 3600u;
    uint32_t mm = (secs % 3600u) / 60u;
    uint32_t ss = secs % 60u;

    uint32_t z = days + 719468u;
    uint32_t era = z / 146097u;
    uint32_t doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    uint32_t y = yoe + era * 400u;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u;
    uint32_t d = doy - (153u * mp + 2u) / 5u + 1u;
    uint32_t m = (mp < 10u) ? mp + 3u : mp - 9u;
    if (m <= 2u) y++;

    int n = snprintf(out, 21, "%04u-%02u-%02uT%02u:%02u:%02uZ",
                     (unsigned)y, (unsigned)m, (unsigned)d, (unsigned)hh, (unsigned)mm, (unsigned)ss);
    return (n > 0 && n < 21) ? n : -1;
}

static inline int af_log_fmt_text(const af_log_ent_t *e, char *buf, size_t cap) {
    if (!e || !buf || cap == 0) return -1;
    char iso[21], rep[12];
    if (af_log_iso(e->utc, iso) < 0) return -1;
    const char *lvl_str = "INFO ";
    if (e->level == 2) lvl_str = "WARN ";
    else if (e->level == 3) lvl_str = "ERROR";
    rep[0] = '\0';
    if (e->count > 1) snprintf(rep, sizeof rep, "x%u ", (unsigned)e->count);
    int n = snprintf(buf, cap, "%s +%us %s %s %s%s\n", iso, (unsigned)e->mono_s, lvl_str, e->tag, rep, e->msg);
    if (n < 0 || (size_t)n >= cap) return -1;
    return n;
}

typedef struct { uint32_t since; uint32_t limit; int min_level; } af_log_q_t;

static inline int af_log_query_parse(const char *qs, af_log_q_t *q) {
    if (!q) return -1;
    q->since = 0;
    q->limit = AF_LOG_Q_DEF_LIMIT;
    q->min_level = 1;
    if (!qs || !qs[0]) return 0;

    int seen_since = 0, seen_limit = 0, seen_level = 0;
    const char *p = qs;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t len = amp ? (size_t)(amp - p) : strlen(p);
        if (len == 0) {
            if (amp) { p = amp + 1; continue; }
            break;
        }
        const char *eq = memchr(p, '=', len);
        if (!eq) return -1;
        size_t klen = (size_t)(eq - p);
        size_t vlen = len - klen - 1;
        if (vlen > 16) return -1;

        if (klen == 5 && memcmp(p, "since", 5) == 0) {
            if (seen_since) return -1;
            seen_since = 1;
            if (vlen < 1 || vlen > 10) return -1;
            uint64_t v = 0;
            for (size_t i = 0; i < vlen; i++) {
                char c = p[klen + 1 + i];
                if (c < '0' || c > '9') return -1;
                v = v * 10 + (uint64_t)(c - '0');
                if (v > 4294967295ULL) return -1;
            }
            q->since = (uint32_t)v;
        } else if (klen == 5 && memcmp(p, "limit", 5) == 0) {
            if (seen_limit) return -1;
            seen_limit = 1;
            if (vlen < 1 || vlen > 10) return -1;
            uint64_t v = 0;
            for (size_t i = 0; i < vlen; i++) {
                char c = p[klen + 1 + i];
                if (c < '0' || c > '9') return -1;
                v = v * 10 + (uint64_t)(c - '0');
                if (v > 4294967295ULL) return -1;
            }
            if (v == 0) return -1;
            q->limit = (v > AF_LOG_Q_MAX_LIMIT) ? AF_LOG_Q_MAX_LIMIT : (uint32_t)v;
        } else if (klen == 5 && memcmp(p, "level", 5) == 0) {
            if (seen_level) return -1;
            seen_level = 1;
            if (vlen == 0) return -1;
            char tmp[17];
            if (vlen > 16) return -1;
            memcpy(tmp, p + klen + 1, vlen);
            tmp[vlen] = '\0';
            int lvl = af_log_level_parse(tmp);
            if (lvl == 0) return -1;
            q->min_level = lvl;
        }
        if (amp) p = amp + 1;
        else break;
    }
    return 0;
}

static inline int af_log_seg_name(uint32_t first_seq, char *buf, size_t cap) {
    if (!buf || cap < 14) return -1;
    int n = snprintf(buf, cap, "g%08x.log", (unsigned)first_seq);
    return (n == 13) ? 13 : -1;
}

static inline int af_log_seg_parse(const char *name, uint32_t *first_seq) {
    if (!name || !first_seq) return -1;
    if (strlen(name) != 13) return -1;
    if (name[0] != 'g') return -1;
    if (memcmp(name + 9, ".log", 4) != 0) return -1;
    uint32_t v = 0;
    for (int i = 1; i <= 8; i++) {
        char c = name[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return -1;
        v = (v << 4) | (uint32_t)d;
    }
    *first_seq = v;
    return 0;
}

static inline int af_log_last_seq(const char *buf, size_t len, int at_file_start, uint32_t *seq) {
    if (!buf || !seq) return -1;
    uint32_t last = 0;
    int found = 0;
    size_t pos = 0;
    int first_piece = 1;
    while (pos < len) {
        size_t nl = memchr(buf + pos, '\n', len - pos) ? (size_t)((char*)memchr(buf + pos, '\n', len - pos) - (buf + pos)) : (size_t)-1;
        if (nl == (size_t)-1) break;
        size_t piece_len = nl;
        if (first_piece && !at_file_start) {
            first_piece = 0;
            pos += nl + 1;
            continue;
        }
        first_piece = 0;
        af_log_ent_t e;
        if (af_log_parse_store(buf + pos, piece_len, &e) == 0) {
            last = e.seq;
            found = 1;
        }
        pos += nl + 1;
    }
    if (!found) return -1;
    *seq = last;
    return 0;
}

static inline uint32_t af_log_scan_start(const uint32_t *first_seqs, uint32_t n, uint32_t since) {
    if (n == 0) return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (i == n - 1) return i;
        if ((uint64_t)first_seqs[i + 1] > (uint64_t)since + 1) return i;
    }
    return n - 1;
}

typedef struct { uint32_t fs_total; uint32_t fs_used; uint32_t add_bytes; uint32_t nfiles; uint32_t cur_bytes; } af_log_rot_in_t;

static inline int af_log_rot_plan(const af_log_rot_in_t *in, const uint32_t *file_bytes, uint32_t *del_n, int *new_seg) {
    if (!in || !del_n || !new_seg) return -1;
    if (in->nfiles > 0 && !file_bytes) return -1;
    *del_n = 0;
    *new_seg = 0;

    int ns = 0;
    if (in->nfiles == 0) ns = 1;
    else if ((uint64_t)in->cur_bytes + in->add_bytes > AF_LOG_SEG_BYTES) ns = 1;
    *new_seg = ns;

    uint64_t total = 0;
    for (uint32_t i = 0; i < in->nfiles; i++) total += file_bytes[i];
    uint64_t proj_files = (uint64_t)in->nfiles + ns;
    uint64_t proj_bytes = total + in->add_bytes;

    uint64_t reserve = 0;
    if (in->fs_total > 0) reserve = (uint64_t)in->fs_total * AF_LOG_RESERVE_PCT / 100;

    uint32_t d = 0;
    while (1) {
        uint64_t freed = 0;
        for (uint32_t i = 0; i < d; i++) {
            uint64_t b = file_bytes[i];
            freed += ((b + AF_LOG_FS_BLOCK - 1) / AF_LOG_FS_BLOCK) * AF_LOG_FS_BLOCK;
        }
        uint64_t used_after = in->fs_used + in->add_bytes + (ns ? AF_LOG_FS_BLOCK : 0);
        if (used_after > freed) used_after -= freed;
        else used_after = 0;
        uint64_t free_after = in->fs_total;
        if (used_after < free_after) free_after -= used_after;
        else free_after = 0;

        int need = 0;
        if (proj_files - d > AF_LOG_MAX_FILES) need = 1;
        if (proj_bytes - freed > AF_LOG_MAX_TOTAL) need = 1;
        if (in->fs_total > 0 && free_after < reserve) need = 1;

        if (!need) break;

        int can_del = 0;
        if (d < in->nfiles) {
            if (d < in->nfiles - 1) can_del = 1;
            else if (ns == 1) can_del = 1;
        }
        if (!can_del) {
            *del_n = d;
            return -1;
        }
        d++;
    }
    *del_n = d;
    return 0;
}

static inline int af_log_flush_due(uint32_t now_s, uint32_t last_flush_s, uint32_t pending, int urgent, uint32_t ring_cap, uint32_t fail_n) {
    if (pending == 0) return 0;
    uint32_t f = (fail_n > 4) ? 4 : fail_n;
    uint32_t iv = AF_LOG_FLUSH_INTERVAL_S << f;
    uint32_t uv = AF_LOG_URGENT_MIN_S << f;
    if (now_s < last_flush_s) return 1;
    uint32_t elapsed = now_s - last_flush_s;
    if (elapsed >= iv) return 1;
    if (urgent != 0 && elapsed >= uv) return 1;
    if ((uint64_t)pending * 4 >= (uint64_t)ring_cap * 3 && elapsed >= uv) return 1;
    return 0;
}

/* Сдвиг нумерации после чтения флеша: offset кратен cap, поэтому позиции слотов seq % cap не меняются. */
static inline void af_log_rebase(af_log_core_t *c, uint32_t last_flash_seq)
{
    if (c == NULL || c->ring == NULL || last_flash_seq == 0 || c->flushed > last_flash_seq) return;
    uint64_t k = ((uint64_t)last_flash_seq + c->cap - 1) / c->cap;
    uint32_t off = (uint32_t)(k * c->cap);
    for (uint32_t i = 0; i < c->cap; i++) if (c->ring[i].seq) c->ring[i].seq += off;
    for (uint32_t i = 0; i < AF_LOG_RECENT_N; i++) if (c->recent[i].seq) c->recent[i].seq += off;
    c->next_seq += off; c->flushed += off; c->barrier += off;
}

/* Очистка журнала в ОЗУ: нумерация продолжается (курсор клиента не откатывается). */
static inline void af_log_clear_ram(af_log_core_t *c)
{
    if (c == NULL || c->ring == NULL) return;
    memset(c->ring, 0, (size_t)c->cap * sizeof(af_log_ent_t));
    memset(c->recent, 0, sizeof(c->recent));
    c->flushed = c->barrier = c->next_seq;
    c->urgent = 0; c->supp_pending = 0;
}

#endif
