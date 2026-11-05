#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "af_test.h"
#include "af_clear.h"

static int g_real = 0;

static int real_num(const char *section, const char *key, uint32_t *out) {
    const char *dir = getenv("AF_DUMP_DIR");
    if (!dir) dir = "/mnt/c/Temp/af_integration_dump/d1";
    char path[512];
    snprintf(path, sizeof(path), "%s/status.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return 0; }
    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);

    char sec_pat[64], key_pat[64];
    snprintf(sec_pat, sizeof(sec_pat), "\"%s\":{", section);
    snprintf(key_pat, sizeof(key_pat), "\"%s\":", key);

    char *sec_pos = strstr(buf, sec_pat);
    if (!sec_pos) { free(buf); return 0; }
    char *search_start = sec_pos + strlen(sec_pat);
    char *key_pos = strstr(search_start, key_pat);
    if (!key_pos) { free(buf); return 0; }
    char *val_start = key_pos + strlen(key_pat);
    // Пропускаем пробелы
    while (*val_start == ' ' || *val_start == '\t' || *val_start == '\n' || *val_start == '\r') val_start++;
    char *end;
    unsigned long v = strtoul(val_start, &end, 10);
    if (end == val_start) { free(buf); return 0; }
    *out = (uint32_t)v;
    free(buf);
    return 1;
}

static void t_mask(void) {
    CHECK(af_clr_ring_mask("raw") == 1);
    CHECK(af_clr_ring_mask("agg") == 2);
    CHECK(af_clr_ring_mask("env") == 4);
    CHECK(af_clr_ring_mask("all") == 7);
    CHECK(af_clr_ring_mask("RAW") == 0);
    CHECK(af_clr_ring_mask("") == 0);
    CHECK(af_clr_ring_mask("all ") == 0);
    CHECK(af_clr_ring_mask("everything") == 0);
    CHECK(af_clr_ring_mask("hist") == 0);
    CHECK(af_clr_ring_mask(NULL) == 0);
}

static void t_req_ok(void) {
    uint32_t mask = 0;
    const char *b1 = "{\"ring\":\"raw\",\"confirm\":\"CLEAR\"}";
    CHECK(af_clr_req_parse(b1, strlen(b1), 1, &mask) == 0);
    CHECK(mask == 1);

    const char *b2 = "{\"confirm\":\"CLEAR\",\"ring\":\"all\"}";
    CHECK(af_clr_req_parse(b2, strlen(b2), 1, &mask) == 0);
    CHECK(mask == 7);

    const char *b3 = " { \"ring\" : \"agg\" ,\n\t\"confirm\" : \"CLEAR\" }  \r\n";
    CHECK(af_clr_req_parse(b3, strlen(b3), 1, &mask) == 0);
    CHECK(mask == 2);

    const char *b4 = "{\"ring\":\"env\",\"confirm\":\"CLEAR\"}";
    CHECK(af_clr_req_parse(b4, strlen(b4), 1, &mask) == 0);
    CHECK(mask == 4);

    mask = 99;
    const char *b5 = "{\"confirm\":\"CLEAR\"}";
    CHECK(af_clr_req_parse(b5, strlen(b5), 0, &mask) == 0);
    CHECK(mask == 0);
}

static void t_req_reject(void) {
    uint32_t mask = 99;
    const char *b;
    size_t len;

    // -1 cases
    b = ""; len = 0;
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\",\"confirm\":\"CLEAR\",\"x\":\"y\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\",\"ring\":\"agg\",\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\",\"confirm\":\"CLEAR\",}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\",\"confirm\":\"CLEAR\"}x"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\",\"confirm\":\"CLEAR\"}}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\",\"confirm\":CLEAR}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":1,\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":true,\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":null,\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":[\"raw\"],\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{'ring':'raw','confirm':'CLEAR'}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\",\"confirm\":\"CLEAR\""; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\",\"confirm\":\"CLEAR"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":\"ra\\w\",\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\" \"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    // 129 bytes
    char buf129[130];
    strcpy(buf129, "{\"ring\":\"raw\",\"confirm\":\"CLEAR\"}");
    size_t base_len = strlen(buf129);
    for (size_t i = base_len; i < 129; i++) buf129[i] = ' ';
    buf129[129] = '\0';
    CHECK(af_clr_req_parse(buf129, 129, 1, &mask) == -1);
    CHECK(mask == 99);

    // Long key
    b = "{\"abcdefghijklmnopq\":\"x\",\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    // Long value
    b = "{\"ring\":\"abcdefghijklmnopq\",\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -1);
    CHECK(mask == 99);

    // need_ring=0 rejects
    b = "{\"ring\":\"raw\",\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 0, &mask) == -1);
    CHECK(mask == 99);

    b = "{}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 0, &mask) == -1);
    CHECK(mask == 99);

    // NULLs
    CHECK(af_clr_req_parse(NULL, 10, 1, &mask) == -1);
    CHECK(af_clr_req_parse(b, 10, 1, NULL) == -1);

    // -2 cases
    b = "{\"ring\":\"raw\",\"confirm\":\"clear\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -2);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\",\"confirm\":\"CLEAR \"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -2);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\",\"confirm\":\"\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -2);
    CHECK(mask == 99);

    b = "{\"ring\":\"raw\",\"confirm\":\"YES\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -2);
    CHECK(mask == 99);

    b = "{\"confirm\":\"nope\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 0, &mask) == -2);
    CHECK(mask == 99);

    // -3 cases
    b = "{\"ring\":\"RAW\",\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -3);
    CHECK(mask == 99);

    b = "{\"ring\":\"\",\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -3);
    CHECK(mask == 99);

    b = "{\"ring\":\"everything\",\"confirm\":\"CLEAR\"}"; len = strlen(b);
    CHECK(af_clr_req_parse(b, len, 1, &mask) == -3);
    CHECK(mask == 99);

    // Binary body
    uint8_t bin[100];
    for (int i = 0; i < 100; i++) bin[i] = (uint8_t)(i * 37 + 11);
    CHECK(af_clr_req_parse((const char*)bin, 100, 1, &mask) == -1);
    CHECK(mask == 99);
}

static void t_marker_roundtrip(void) {
    af_clr_marker_t m;
    m.mask = 5;
    m.epoch = 22;
    m.seq[0] = 10900;
    m.seq[1] = 365;
    m.seq[2] = 291;

    uint32_t e, s, a, v;
    if (real_num("raw", "epoch", &e) && real_num("raw", "seq", &s) && real_num("agg", "seq", &a) && real_num("env", "seq", &v)) {
        m.epoch = e + 1;
        m.seq[0] = s;
        m.seq[1] = a;
        m.seq[2] = v;
        g_real++;
    }

    uint8_t b[AF_CLR_SIZE];
    af_clr_marker_encode(&m, b);

    af_clr_marker_t m2;
    memset(&m2, 0, sizeof(m2));
    CHECK(af_clr_marker_decode(b, AF_CLR_SIZE, &m2) == 0);
    CHECK(m2.mask == m.mask);
    CHECK(m2.epoch == m.epoch);
    CHECK(m2.seq[0] == m.seq[0]);
    CHECK(m2.seq[1] == m.seq[1]);
    CHECK(m2.seq[2] == m.seq[2]);

    // Проверка байтов
    CHECK(b[0] == 0x43);
    CHECK(b[1] == 0x4C);
    CHECK(b[2] == 0x52);
    CHECK(b[3] == 0x31);
    CHECK(b[4] == 1);
    CHECK(b[5] == (uint8_t)m.mask);
    CHECK(b[6] == 0);
    CHECK(b[7] == 0);
    CHECK(b[8] == (uint8_t)(m.epoch & 0xFF));
    CHECK(b[9] == (uint8_t)((m.epoch >> 8) & 0xFF));
    CHECK(b[10] == (uint8_t)((m.epoch >> 16) & 0xFF));
    CHECK(b[11] == (uint8_t)((m.epoch >> 24) & 0xFF));
    CHECK(b[12] == (uint8_t)(m.seq[0] & 0xFF));
    CHECK(b[13] == (uint8_t)((m.seq[0] >> 8) & 0xFF));
    CHECK(b[14] == (uint8_t)((m.seq[0] >> 16) & 0xFF));
    CHECK(b[15] == (uint8_t)((m.seq[0] >> 24) & 0xFF));
    CHECK(b[16] == (uint8_t)(m.seq[1] & 0xFF));
    CHECK(b[17] == (uint8_t)((m.seq[1] >> 8) & 0xFF));
    CHECK(b[18] == (uint8_t)((m.seq[1] >> 16) & 0xFF));
    CHECK(b[19] == (uint8_t)((m.seq[1] >> 24) & 0xFF));
    CHECK(b[20] == (uint8_t)(m.seq[2] & 0xFF));
    CHECK(b[21] == (uint8_t)((m.seq[2] >> 8) & 0xFF));
    CHECK(b[22] == (uint8_t)((m.seq[2] >> 16) & 0xFF));
    CHECK(b[23] == (uint8_t)((m.seq[2] >> 24) & 0xFF));

    // Повреждение битов
    for (int i = 0; i < AF_CLR_SIZE; i++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t b_copy[AF_CLR_SIZE];
            memcpy(b_copy, b, AF_CLR_SIZE);
            b_copy[i] ^= (1 << bit);
            af_clr_marker_t m3;
            memset(&m3, 0, sizeof(m3));
            CHECK(af_clr_marker_decode(b_copy, AF_CLR_SIZE, &m3) == -1);
        }
    }

    // Длина
    CHECK(af_clr_marker_decode(b, 25, &m2) == -1);
    CHECK(af_clr_marker_decode(b, 27, &m2) == -1);
    CHECK(af_clr_marker_decode(b, 0, &m2) == -1);
    CHECK(af_clr_marker_decode(NULL, AF_CLR_SIZE, &m2) == -1);
    CHECK(af_clr_marker_decode(b, AF_CLR_SIZE, NULL) == -1);

    /* Некорректные поля при ВЕРНОМ crc: проверяется именно разбор поля, а не crc */
    uint8_t bb[AF_CLR_SIZE];
    for (int k = 0; k < 5; k++) {
        memcpy(bb, b, AF_CLR_SIZE);
        if (k == 0) bb[5] = 0;   /* mask 0 */
        if (k == 1) bb[5] = 8;   /* mask 8 */
        if (k == 2) bb[4] = 2;   /* версия 2 */
        if (k == 3) bb[6] = 1;   /* зарезервированный байт 6 */
        if (k == 4) bb[7] = 1;   /* зарезервированный байт 7 */
        uint16_t c = af_crc16(bb, 24);
        bb[24] = (uint8_t)(c & 0xFF);
        bb[25] = (uint8_t)(c >> 8);
        CHECK(af_clr_marker_decode(bb, AF_CLR_SIZE, &m2) == -1);
    }
}

static void t_kind_done(void) {
    af_clr_marker_t m;
    m.mask = 7;
    m.epoch = 22;
    m.seq[0] = 1000;
    m.seq[1] = 50;
    m.seq[2] = 20;

    uint32_t ep1[] = {21, 21};
    uint32_t sq1[] = {500, 900};
    CHECK(af_clr_kind_done(&m, 0, ep1, sq1, 2) == 0);

    uint32_t ep2[] = {21, 21, 22};
    uint32_t sq2[] = {500, 900, 1000};
    CHECK(af_clr_kind_done(&m, 0, ep2, sq2, 3) == 1);

    uint32_t ep3[] = {23};
    uint32_t sq3[] = {1000};
    CHECK(af_clr_kind_done(&m, 0, ep3, sq3, 1) == 1);

    uint32_t ep4[] = {22};
    uint32_t sq4[] = {999};
    CHECK(af_clr_kind_done(&m, 0, ep4, sq4, 1) == 0);

    uint32_t ep5[] = {21};
    uint32_t sq5[] = {1000};
    CHECK(af_clr_kind_done(&m, 0, ep5, sq5, 1) == 0);

    // kind 1
    uint32_t ep6[] = {22};
    uint32_t sq6[] = {1000};
    CHECK(af_clr_kind_done(&m, 1, ep6, sq6, 1) == 1);

    uint32_t ep7[] = {22};
    uint32_t sq7[] = {49};
    CHECK(af_clr_kind_done(&m, 1, ep7, sq7, 1) == 0);

    CHECK(af_clr_kind_done(&m, 0, NULL, NULL, 0) == 0);
    CHECK(af_clr_kind_done(&m, -1, ep1, sq1, 2) == 0);
    CHECK(af_clr_kind_done(&m, 3, ep1, sq1, 2) == 0);
    CHECK(af_clr_kind_done(NULL, 0, ep1, sq1, 2) == 0);
    CHECK(af_clr_kind_done(&m, 0, NULL, sq1, 1) == 0);
    CHECK(af_clr_kind_done(&m, 0, ep1, NULL, 1) == 0);
    CHECK(af_clr_kind_done(&m, 0, NULL, NULL, 0) == 0);
}

static void t_res_json(void) {
    char buf[256];
    uint32_t removed[3] = {1234, 56, 0};
    uint32_t seq[3] = {10900, 365, 291};
    uint32_t epoch[3] = {22, 22, 21};

    int len = af_clr_res_json(buf, sizeof(buf), 3, removed, seq, epoch, 1);
    const char *expected = "{\"ok\":true,\"cleared\":[\"raw\",\"agg\"],\"removed\":{\"raw\":1234,\"agg\":56,\"env\":0},\"seq\":{\"raw\":10900,\"agg\":365,\"env\":291},\"epoch\":{\"raw\":22,\"agg\":22,\"env\":21},\"persisted\":true}";
    CHECK(len == (int)strlen(expected));
    CHECK(strncmp(buf, expected, len) == 0);

    len = af_clr_res_json(buf, sizeof(buf), 7, removed, seq, epoch, 1);
    const char *expected2 = "{\"ok\":true,\"cleared\":[\"raw\",\"agg\",\"env\"],\"removed\":{\"raw\":1234,\"agg\":56,\"env\":0},\"seq\":{\"raw\":10900,\"agg\":365,\"env\":291},\"epoch\":{\"raw\":22,\"agg\":22,\"env\":21},\"persisted\":true}";
    CHECK(len == (int)strlen(expected2));
    CHECK(strncmp(buf, expected2, len) == 0);

    len = af_clr_res_json(buf, sizeof(buf), 4, removed, seq, epoch, 1);
    const char *expected3 = "{\"ok\":true,\"cleared\":[\"env\"],\"removed\":{\"raw\":1234,\"agg\":56,\"env\":0},\"seq\":{\"raw\":10900,\"agg\":365,\"env\":291},\"epoch\":{\"raw\":22,\"agg\":22,\"env\":21},\"persisted\":true}";
    CHECK(len == (int)strlen(expected3));
    CHECK(strncmp(buf, expected3, len) == 0);

    len = af_clr_res_json(buf, sizeof(buf), 3, removed, seq, epoch, 0);
    const char *expected4 = "{\"ok\":true,\"cleared\":[\"raw\",\"agg\"],\"removed\":{\"raw\":1234,\"agg\":56,\"env\":0},\"seq\":{\"raw\":10900,\"agg\":365,\"env\":291},\"epoch\":{\"raw\":22,\"agg\":22,\"env\":21},\"persisted\":false}";
    CHECK(len == (int)strlen(expected4));
    CHECK(strncmp(buf, expected4, len) == 0);

    // cap too small
    CHECK(af_clr_res_json(buf, 10, 3, removed, seq, epoch, 1) == -1);
    CHECK(af_clr_res_json(NULL, 256, 3, removed, seq, epoch, 1) == -1);
    CHECK(af_clr_res_json(buf, 256, 3, NULL, seq, epoch, 1) == -1);

    // Large values
    uint32_t removed_big[3] = {4294967295, 4294967295, 4294967295};
    uint32_t seq_big[3] = {4294967295, 4294967295, 4294967295};
    uint32_t epoch_big[3] = {4294967295, 4294967295, 4294967295};
    len = af_clr_res_json(buf, sizeof(buf), 7, removed_big, seq_big, epoch_big, 1);
    const char *expected5 = "{\"ok\":true,\"cleared\":[\"raw\",\"agg\",\"env\"],\"removed\":{\"raw\":4294967295,\"agg\":4294967295,\"env\":4294967295},\"seq\":{\"raw\":4294967295,\"agg\":4294967295,\"env\":4294967295},\"epoch\":{\"raw\":4294967295,\"agg\":4294967295,\"env\":4294967295},\"persisted\":true}";
    CHECK(len == (int)strlen(expected5));
    CHECK(strncmp(buf, expected5, len) == 0);
}

static void t_log_msg(void) {
    char buf[256];
    uint32_t removed[3] = {1234, 56, 0};

    int len = af_clr_log_msg(buf, sizeof(buf), 3, removed);
    const char *expected = "history cleared ring=raw+agg removed=1234/56/0";
    CHECK(len == (int)strlen(expected));
    CHECK(strncmp(buf, expected, len) == 0);

    len = af_clr_log_msg(buf, sizeof(buf), 7, removed);
    const char *expected2 = "history cleared ring=raw+agg+env removed=1234/56/0";
    CHECK(len == (int)strlen(expected2));
    CHECK(strncmp(buf, expected2, len) == 0);

    len = af_clr_log_msg(buf, sizeof(buf), 4, removed);
    const char *expected3 = "history cleared ring=env removed=1234/56/0";
    CHECK(len == (int)strlen(expected3));
    CHECK(strncmp(buf, expected3, len) == 0);

    CHECK(af_clr_log_msg(buf, 10, 3, removed) == -1);
    CHECK(af_clr_log_msg(NULL, 256, 3, removed) == -1);
    CHECK(af_clr_log_msg(buf, 256, 3, NULL) == -1);
}

int main(void) {
    RUN(t_mask);
    RUN(t_req_ok);
    RUN(t_req_reject);
    RUN(t_marker_roundtrip);
    RUN(t_kind_done);
    RUN(t_res_json);
    RUN(t_log_msg);

    if (g_real > 0) {
        printf("REAL_SAMPLES %d\n", g_real);
    } else {
        printf("REAL_SAMPLES 0 (dump not found)\n");
    }

    TESTS_DONE();
    return 0;
}
