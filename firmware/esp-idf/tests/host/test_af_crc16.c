#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "af_rec.h"

static int g_fail = 0;
static int g_n = 0;

#define CHECK(cond) do { \
    g_n++; \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        g_fail++; \
    } \
} while(0)

/* Сцепление CRC: продолжение вычисления с текущего состояния */
static uint16_t crc_cont(uint16_t crc, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        crc = (uint16_t)((crc << 8) ^ af_crc16_tab[(uint8_t)((crc >> 8) ^ p[i])]);
    }
    return crc;
}

/* Детерминированный xorshift32 */
static uint32_t g_rng = 0x12345678;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}

int main(void) {
    /* 1. Известный вектор: "123456789" -> 0x29B1 */
    const uint8_t vec[] = "123456789";
    CHECK(af_crc16(vec, 9) == 0x29B1);
    CHECK(af_crc16_bitwise(vec, 9) == 0x29B1);

    /* 2. Пустой вход -> 0xFFFF */
    CHECK(af_crc16(vec, 0) == 0xFFFF);
    CHECK(af_crc16_bitwise(vec, 0) == 0xFFFF);

    /* 3. Одиночные байты 0..255: табличная == битовая */
    for (int b = 0; b < 256; b++) {
        uint8_t byte = (uint8_t)b;
        CHECK(af_crc16(&byte, 1) == af_crc16_bitwise(&byte, 1));
    }

    /* 4. Проверка таблицы: вычисление по битовому циклу */
    for (int i = 0; i < 256; i++) {
        uint16_t c = (uint16_t)(i << 8);
        for (int k = 0; k < 8; k++) {
            if (c & 0x8000) c = (uint16_t)((c << 1) ^ 0x1021);
            else c = (uint16_t)(c << 1);
        }
        CHECK(af_crc16_tab[i] == c);
    }
    CHECK(af_crc16_tab[0] == 0x0000);
    CHECK(af_crc16_tab[1] == 0x1021);
    CHECK(af_crc16_tab[255] == 0x1EF0);

    /* 5. Случайные буферы: 2000 прогонов, длина 0..300 */
    uint8_t buf[300];
    for (int run = 0; run < 2000; run++) {
        size_t len = xorshift32() % 301; /* 0..300 */
        for (size_t i = 0; i < len; i++) {
            buf[i] = (uint8_t)(xorshift32() & 0xFF);
        }
        CHECK(af_crc16(buf, len) == af_crc16_bitwise(buf, len));
    }

    /* 6. Формы боевого входа */
    uint8_t rec[18];
    /* Все нули */
    memset(rec, 0, 18);
    CHECK(af_crc16(rec, 18) == af_crc16_bitwise(rec, 18));
    /* Все 0xFF */
    memset(rec, 0xFF, 18);
    CHECK(af_crc16(rec, 18) == af_crc16_bitwise(rec, 18));
    /* Нарастающие 0..17 */
    for (int i = 0; i < 18; i++) rec[i] = (uint8_t)i;
    CHECK(af_crc16(rec, 18) == af_crc16_bitwise(rec, 18));

    /* Заголовок сегмента: 30 байт, 0..29 */
    uint8_t hdr[30];
    for (int i = 0; i < 30; i++) hdr[i] = (uint8_t)i;
    CHECK(af_crc16(hdr, 30) == af_crc16_bitwise(hdr, 30));

    /* 7. Свойство сцепления: буфер 100 байт, разбивка 40+60 */
    uint8_t big[100];
    for (int i = 0; i < 100; i++) big[i] = (uint8_t)(xorshift32() & 0xFF);
    uint16_t crc_part1 = crc_cont(0xFFFF, big, 40);
    uint16_t crc_full = crc_cont(crc_part1, big + 40, 60);
    CHECK(crc_full == af_crc16(big, 100));

    printf("TESTS %d FAIL %d\n", g_n, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
