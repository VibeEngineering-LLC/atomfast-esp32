/* test_af_csvname.c — host-тесты af_csv_name (C11) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "af_test.h"
#include "af_csvname.h"

/* Таблица видов (порядок соответствует AF_CSVNAME_RAW/AGG/ENV) */
static const char *KN[3] = {"raw", "agg", "env"};

/* Моменты с временем: 1; 86399 и 86400 — граница суток; 1761868800; 951782400 — 29.02.2000; 4294967295 — максимум uint32 */
static const uint32_t TS_NZ[6]   = {1u, 86399u, 86400u, 1761868800u, 951782400u, 4294967295u};
static const char    *DATE_NZ[6] = {"19700101", "19700101", "19700102", "20251031", "20000229", "21060207"};
static const char    *HMS_NZ[6]  = {"000001", "235959", "000000", "000000", "000000", "062815"};

/* Заполняет 40 байт значением 0x7F */
static void canary(char *b) { memset(b, 0x7F, 40); }

/* 1. Префикс, вид, разделитель */
static void t_kind(void)
{
    size_t k, i;
    uint32_t ts[2] = {0u, 1761868800u};
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 2; i++) {
            char b[40];
            canary(b);
            int r = af_csv_name((uint8_t)k, ts[i], b, 40);
            CHECK(r > 0);
            CHECK(memcmp(b, "atomfast_", 9) == 0);
            CHECK(memcmp(b + 9, KN[k], 3) == 0);
            CHECK(b[12] == '_');
        }
    }
}

/* 2. Отметка notime при ts==0 */
static void t_notime(void)
{
    size_t k;
    for (k = 0; k < 3; k++) {
        char b[40];
        canary(b);
        af_csv_name((uint8_t)k, 0u, b, 40);
        CHECK(memcmp(b + 13, "notime", 6) == 0);
    }
}

/* 3. Дата YYYYMMDD */
static void t_date(void)
{
    size_t k, i;
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 6; i++) {
            char b[40];
            canary(b);
            af_csv_name((uint8_t)k, TS_NZ[i], b, 40);
            CHECK(memcmp(b + 13, DATE_NZ[i], 8) == 0);
        }
    }
}

/* 4. Время HHMMSS */
static void t_hms(void)
{
    size_t k, i;
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 6; i++) {
            char b[40];
            canary(b);
            af_csv_name((uint8_t)k, TS_NZ[i], b, 40);
            CHECK(memcmp(b + 22, HMS_NZ[i], 6) == 0);
        }
    }
}

/* 5. Расширение .csv */
static void t_ext(void)
{
    size_t k, i;
    uint32_t ts[7] = {0u, 1u, 86399u, 86400u, 1761868800u, 951782400u, 4294967295u};
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 7; i++) {
            char b[40];
            canary(b);
            af_csv_name((uint8_t)k, ts[i], b, 40);
            CHECK(memchr(b, 0, sizeof b) != NULL); /* NUL записан */
            size_t l = strlen(b);
            CHECK(l >= 4u);
            if (l >= 4u) {
                CHECK(memcmp(b + l - 4u, ".csv", 4) == 0);
            }
        }
    }
}

/* 6. Возвращаемая длина */
static void t_len(void)
{
    size_t k, i;
    for (k = 0; k < 3; k++) {
        char b[40];
        canary(b);
        int r = af_csv_name((uint8_t)k, 0u, b, 40);
        CHECK(r == 23);
        for (i = 0; i < 6; i++) {
            canary(b);
            r = af_csv_name((uint8_t)k, TS_NZ[i], b, 40);
            CHECK(r == 32);
        }
    }
}

/* 7. Мелкие cap — ошибка, буфер не тронут */
static void t_cap_small(void)
{
    size_t k, ci;
    for (k = 0; k < 3; k++) {
        size_t caps1[] = {0, 1, 10, 23};
        for (ci = 0; ci < 4; ci++) {
            char b[40];
            canary(b);
            char ref[40];
            memcpy(ref, b, 40);
            int r = af_csv_name((uint8_t)k, 0u, b, caps1[ci]);
            CHECK(r == -1);
            CHECK(memcmp(b, ref, 40) == 0);
        }
        size_t caps2[] = {0, 1, 10, 32};
        for (ci = 0; ci < 4; ci++) {
            char b[40];
            canary(b);
            char ref[40];
            memcpy(ref, b, 40);
            int r = af_csv_name((uint8_t)k, 1761868800u, b, caps2[ci]);
            CHECK(r == -1);
            CHECK(memcmp(b, ref, 40) == 0);
        }
    }
}

/* 8. Точное cap — успех и NUL */
static void t_cap_exact(void)
{
    size_t k;
    for (k = 0; k < 3; k++) {
        char b[40];
        canary(b);
        int r = af_csv_name((uint8_t)k, 0u, b, 24);
        CHECK(r != -1);
        canary(b);
        r = af_csv_name((uint8_t)k, 1761868800u, b, 33);
        CHECK(r != -1);
    }
}

/* 9. Недопустимый kind */
static void t_kind3(void)
{
    uint8_t bad[3] = {3u, 4u, 255u};
    uint32_t ts[2] = {0u, 1761868800u};
    size_t i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 2; j++) {
            char b[40];
            canary(b);
            char ref[40];
            memcpy(ref, b, 40);
            int r = af_csv_name(bad[i], ts[j], b, 40);
            CHECK(r == -1);
            CHECK(memcmp(b, ref, 40) == 0);
        }
    }
}

/* 10. NULL-буфер */
static void t_null(void)
{
    int r;
    r = af_csv_name(0u, 0u, NULL, 40);
    CHECK(r == -1);
    r = af_csv_name(0u, 0u, NULL, 0);
    CHECK(r == -1);
    r = af_csv_name(0u, 1761868800u, NULL, 40);
    CHECK(r == -1);
    r = af_csv_name(0u, 1761868800u, NULL, 0);
    CHECK(r == -1);
}

/* 11. Допустимые символы и дефис-разделитель */
static void t_chars(void)
{
    size_t k, i, j;
    uint32_t ts[7] = {0u, 1u, 86399u, 86400u, 1761868800u, 951782400u, 4294967295u};
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 7; i++) {
            char b[40];
            canary(b);
            af_csv_name((uint8_t)k, ts[i], b, 40);
            size_t l = strlen(b);
            for (j = 0; j < l; j++) {
                char c = b[j];
                int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
                CHECK(ok);
            }
            if (ts[i] != 0u) {
                CHECK(b[21] == '-');
            }
        }
    }
}

int main(void)
{
    RUN(t_kind);
    RUN(t_notime);
    RUN(t_date);
    RUN(t_hms);
    RUN(t_ext);
    RUN(t_len);
    RUN(t_cap_small);
    RUN(t_cap_exact);
    RUN(t_kind3);
    RUN(t_null);
    RUN(t_chars);
    TESTS_DONE();
}
