#ifndef AF_CLEAR_H
#define AF_CLEAR_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "af_rec.h"

#define AF_CLR_MASK_RAW 1u
#define AF_CLR_MASK_AGG 2u
#define AF_CLR_MASK_ENV 4u
#define AF_CLR_MASK_ALL 7u
#define AF_CLR_MAGIC 0x31524C43u
#define AF_CLR_VER 1
#define AF_CLR_SIZE 26
#define AF_CLR_BODY_MAX 128

typedef struct {
    uint32_t mask;
    uint32_t epoch;
    uint32_t seq[3];
} af_clr_marker_t;

static inline uint32_t af_clr_ring_mask(const char *name) {
    if (name == NULL) return 0;
    if (strcmp(name, "raw") == 0) return AF_CLR_MASK_RAW;
    if (strcmp(name, "agg") == 0) return AF_CLR_MASK_AGG;
    if (strcmp(name, "env") == 0) return AF_CLR_MASK_ENV;
    if (strcmp(name, "all") == 0) return AF_CLR_MASK_ALL;
    return 0;
}

static inline int af_clr_req_parse(const char *body, size_t len, int need_ring, uint32_t *mask) {
    if (body == NULL || mask == NULL) return -1;
    if (len < 1 || len > AF_CLR_BODY_MAX) return -1;

    size_t pos = 0;
    int seen_ring = 0;
    int seen_confirm = 0;
    char ring_val[17] = {0};
    char confirm_val[17] = {0};

    // Пропустить пробелы
    while (pos < len && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' || body[pos] == '\n')) pos++;
    if (pos >= len || body[pos] != '{') return -1;
    pos++;

    // Первый элемент или пустой объект
    if (pos < len && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' || body[pos] == '\n')) {
        // Проверка на пустой объект
        size_t save = pos;
        while (pos < len && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' || body[pos] == '\n')) pos++;
        if (pos < len && body[pos] == '}') {
            // Пустой объект недопустим, так как confirm обязателен
            return -1;
        }
        pos = save;
    }

    int first = 1;
    while (1) {
        if (!first) {
            // Пропустить пробелы перед запятой
            while (pos < len && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' || body[pos] == '\n')) pos++;
            if (pos >= len || body[pos] != ',') return -1;
            pos++;
            // Пропустить пробелы после запятой
            while (pos < len && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' || body[pos] == '\n')) pos++;
            if (pos >= len || body[pos] == '}') return -1; // Запрещенный хвостовое запятая
        }

        // Пропустить пробелы перед ключом
        while (pos < len && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' || body[pos] == '\n')) pos++;
        if (pos >= len || body[pos] != '"') return -1;
        pos++;

        // Чтение ключа
        size_t key_start = pos;
        size_t key_len = 0;
        while (pos < len && body[pos] != '"') {
            if (body[pos] == '\\' || (unsigned char)body[pos] < 0x20) return -1;
            key_len++;
            if (key_len > 16) return -1;
            pos++;
        }
        if (pos >= len) return -1;
        pos++; // Пропустить закрывающую кавычку

        // Проверка ключа
        if (key_len == 4 && memcmp(body + key_start, "ring", 4) == 0) {
            if (seen_ring) return -1; // Дубликат
            if (!need_ring) return -1; // Ключ ring запрещен, если need_ring == 0
            seen_ring = 1;
        } else if (key_len == 7 && memcmp(body + key_start, "confirm", 7) == 0) {
            if (seen_confirm) return -1; // Дубликат
            seen_confirm = 1;
        } else {
            return -1; // Неизвестный ключ
        }

        // Пропустить пробелы после ключа
        while (pos < len && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' || body[pos] == '\n')) pos++;
        if (pos >= len || body[pos] != ':') return -1;
        pos++;

        // Пропустить пробелы перед значением
        while (pos < len && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' || body[pos] == '\n')) pos++;
        if (pos >= len || body[pos] != '"') return -1;
        pos++;

        // Чтение значения
        size_t val_start = pos;
        size_t val_len = 0;
        while (pos < len && body[pos] != '"') {
            if (body[pos] == '\\' || (unsigned char)body[pos] < 0x20) return -1;
            val_len++;
            if (val_len > 16) return -1;
            pos++;
        }
        if (pos >= len) return -1;
        pos++; // Пропустить закрывающую кавычку

        // Сохранить значение
        if (key_len == 4 && memcmp(body + key_start, "ring", 4) == 0) {
            if (val_len > 16) return -1;
            memcpy(ring_val, body + val_start, val_len);
            ring_val[val_len] = '\0';
        } else if (key_len == 7 && memcmp(body + key_start, "confirm", 7) == 0) {
            if (val_len > 16) return -1;
            memcpy(confirm_val, body + val_start, val_len);
            confirm_val[val_len] = '\0';
        }

        // Пропустить пробелы после значения
        while (pos < len && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' || body[pos] == '\n')) pos++;

        if (pos >= len) return -1;
        if (body[pos] == ',') {
            first = 0;
            continue;
        } else if (body[pos] == '}') {
            pos++;
            break;
        } else {
            return -1;
        }
    }

    // Пропустить пробелы в конце
    while (pos < len && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' || body[pos] == '\n')) pos++;
    if (pos != len) return -1; // Лишние символы

    // Проверка обязательных полей
    if (!seen_confirm) return -1;
    if (need_ring && !seen_ring) return -1;

    // Проверка значения confirm
    if (strcmp(confirm_val, "CLEAR") != 0) return -2;

    // Обработка ring
    if (need_ring) {
        uint32_t m = af_clr_ring_mask(ring_val);
        if (m == 0) return -3;
        *mask = m;
    } else {
        *mask = 0;
    }

    return 0;
}

static inline void af_clr_marker_encode(const af_clr_marker_t *m, uint8_t out[AF_CLR_SIZE]) {
    if (m == NULL || out == NULL) return;
    
    // Magic
    out[0] = (uint8_t)(AF_CLR_MAGIC & 0xFF);
    out[1] = (uint8_t)((AF_CLR_MAGIC >> 8) & 0xFF);
    out[2] = (uint8_t)((AF_CLR_MAGIC >> 16) & 0xFF);
    out[3] = (uint8_t)((AF_CLR_MAGIC >> 24) & 0xFF);
    
    // Version
    out[4] = AF_CLR_VER;
    
    // Mask
    out[5] = (uint8_t)(m->mask & 0xFF);
    
    // Zero bytes
    out[6] = 0;
    out[7] = 0;
    
    // Epoch
    out[8] = (uint8_t)(m->epoch & 0xFF);
    out[9] = (uint8_t)((m->epoch >> 8) & 0xFF);
    out[10] = (uint8_t)((m->epoch >> 16) & 0xFF);
    out[11] = (uint8_t)((m->epoch >> 24) & 0xFF);
    
    // Seq[0]
    out[12] = (uint8_t)(m->seq[0] & 0xFF);
    out[13] = (uint8_t)((m->seq[0] >> 8) & 0xFF);
    out[14] = (uint8_t)((m->seq[0] >> 16) & 0xFF);
    out[15] = (uint8_t)((m->seq[0] >> 24) & 0xFF);
    
    // Seq[1]
    out[16] = (uint8_t)(m->seq[1] & 0xFF);
    out[17] = (uint8_t)((m->seq[1] >> 8) & 0xFF);
    out[18] = (uint8_t)((m->seq[1] >> 16) & 0xFF);
    out[19] = (uint8_t)((m->seq[1] >> 24) & 0xFF);
    
    // Seq[2]
    out[20] = (uint8_t)(m->seq[2] & 0xFF);
    out[21] = (uint8_t)((m->seq[2] >> 8) & 0xFF);
    out[22] = (uint8_t)((m->seq[2] >> 16) & 0xFF);
    out[23] = (uint8_t)((m->seq[2] >> 24) & 0xFF);
    
    // CRC
    uint16_t crc = af_crc16(out, 24);
    out[24] = (uint8_t)(crc & 0xFF);
    out[25] = (uint8_t)((crc >> 8) & 0xFF);
}

static inline int af_clr_marker_decode(const uint8_t *in, size_t len, af_clr_marker_t *m) {
    if (in == NULL || m == NULL) return -1;
    if (len != AF_CLR_SIZE) return -1;
    
    // Magic
    uint32_t magic = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
    if (magic != AF_CLR_MAGIC) return -1;
    
    // Version
    if (in[4] != AF_CLR_VER) return -1;
    
    // Zero bytes
    if (in[6] != 0 || in[7] != 0) return -1;
    
    // CRC
    uint16_t crc_calc = af_crc16(in, 24);
    uint16_t crc_stored = (uint16_t)in[24] | ((uint16_t)in[25] << 8);
    if (crc_calc != crc_stored) return -1;
    
    // Mask
    uint32_t mask = in[5];
    if (mask < 1 || mask > 7) return -1;
    
    // Fill struct
    m->mask = mask;
    m->epoch = (uint32_t)in[8] | ((uint32_t)in[9] << 8) | ((uint32_t)in[10] << 16) | ((uint32_t)in[11] << 24);
    m->seq[0] = (uint32_t)in[12] | ((uint32_t)in[13] << 8) | ((uint32_t)in[14] << 16) | ((uint32_t)in[15] << 24);
    m->seq[1] = (uint32_t)in[16] | ((uint32_t)in[17] << 8) | ((uint32_t)in[18] << 16) | ((uint32_t)in[19] << 24);
    m->seq[2] = (uint32_t)in[20] | ((uint32_t)in[21] << 8) | ((uint32_t)in[22] << 16) | ((uint32_t)in[23] << 24);
    
    return 0;
}

static inline int af_clr_kind_done(const af_clr_marker_t *m, int kind, const uint32_t *hdr_epoch, const uint32_t *hdr_first_seq, uint32_t n) {
    if (m == NULL) return 0;
    if (kind < 0 || kind > 2) return 0;
    if (n > 0 && (hdr_epoch == NULL || hdr_first_seq == NULL)) return 0;
    
    for (uint32_t i = 0; i < n; i++) {
        if (hdr_epoch[i] >= m->epoch && hdr_first_seq[i] >= m->seq[kind]) {
            return 1;
        }
    }
    return 0;
}

static inline int af_clr_res_json(char *buf, size_t cap, uint32_t mask, const uint32_t removed[3], const uint32_t seq[3], const uint32_t epoch[3], int persisted) {
    if (buf == NULL || removed == NULL || seq == NULL || epoch == NULL) return -1;
    
    // Формируем список очищенных колец
    char cleared[64] = {0};
    size_t cleared_len = 0;
    int first = 1;
    
    if (mask & AF_CLR_MASK_RAW) {
        if (!first) cleared[cleared_len++] = ',';
        const char *s = "\"raw\"";
        size_t slen = strlen(s);
        if (cleared_len + slen < sizeof(cleared)) {
            memcpy(cleared + cleared_len, s, slen);
            cleared_len += slen;
        }
        first = 0;
    }
    if (mask & AF_CLR_MASK_AGG) {
        if (!first) cleared[cleared_len++] = ',';
        const char *s = "\"agg\"";
        size_t slen = strlen(s);
        if (cleared_len + slen < sizeof(cleared)) {
            memcpy(cleared + cleared_len, s, slen);
            cleared_len += slen;
        }
        first = 0;
    }
    if (mask & AF_CLR_MASK_ENV) {
        if (!first) cleared[cleared_len++] = ',';
        const char *s = "\"env\"";
        size_t slen = strlen(s);
        if (cleared_len + slen < sizeof(cleared)) {
            memcpy(cleared + cleared_len, s, slen);
            cleared_len += slen;
        }
        first = 0;
    }
    
    const char *persisted_str = persisted ? "true" : "false";
    
    int ret = snprintf(buf, cap, 
        "{\"ok\":true,\"cleared\":[%.*s],\"removed\":{\"raw\":%u,\"agg\":%u,\"env\":%u},"
        "\"seq\":{\"raw\":%u,\"agg\":%u,\"env\":%u},"
        "\"epoch\":{\"raw\":%u,\"agg\":%u,\"env\":%u},"
        "\"persisted\":%s}",
        (int)cleared_len, cleared,
        (unsigned)removed[0], (unsigned)removed[1], (unsigned)removed[2],
        (unsigned)seq[0], (unsigned)seq[1], (unsigned)seq[2],
        (unsigned)epoch[0], (unsigned)epoch[1], (unsigned)epoch[2],
        persisted_str
    );
    
    if (ret < 0 || (size_t)ret >= cap) return -1;
    return ret;
}

static inline int af_clr_log_msg(char *buf, size_t cap, uint32_t mask, const uint32_t removed[3]) {
    if (buf == NULL || removed == NULL) return -1;
    
    char ring_str[32] = {0};
    size_t ring_len = 0;
    int first = 1;
    
    if (mask & AF_CLR_MASK_RAW) {
        if (!first) ring_str[ring_len++] = '+';
        const char *s = "raw";
        size_t slen = strlen(s);
        if (ring_len + slen < sizeof(ring_str)) {
            memcpy(ring_str + ring_len, s, slen);
            ring_len += slen;
        }
        first = 0;
    }
    if (mask & AF_CLR_MASK_AGG) {
        if (!first) ring_str[ring_len++] = '+';
        const char *s = "agg";
        size_t slen = strlen(s);
        if (ring_len + slen < sizeof(ring_str)) {
            memcpy(ring_str + ring_len, s, slen);
            ring_len += slen;
        }
        first = 0;
    }
    if (mask & AF_CLR_MASK_ENV) {
        if (!first) ring_str[ring_len++] = '+';
        const char *s = "env";
        size_t slen = strlen(s);
        if (ring_len + slen < sizeof(ring_str)) {
            memcpy(ring_str + ring_len, s, slen);
            ring_len += slen;
        }
        first = 0;
    }
    
    int ret = snprintf(buf, cap, "history cleared ring=%.*s removed=%u/%u/%u", 
        (int)ring_len, ring_str, 
        (unsigned)removed[0], (unsigned)removed[1], (unsigned)removed[2]);
    
    if (ret < 0 || (size_t)ret >= cap) return -1;
    return ret;
}

#endif
