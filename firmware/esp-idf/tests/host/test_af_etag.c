#include <stdio.h>
#include <string.h>
#include "af_etag.h"

static int g_fail = 0;
static int g_n = 0;

#define CHECK(cond) do { \
    g_n++; \
    if (!(cond)) { \
        g_fail++; \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
    } \
} while(0)

int main(void) {
    /* Сценарий 1: make с валидными данными */
    {
        uint8_t sha[10] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xff, 0xff};
        char out[20];
        memset(out, 0x55, sizeof(out));
        int ret = af_etag_make(sha, 10, out, 20);
        CHECK(ret == 18);
        CHECK(strcmp(out, "\"0123456789abcdef\"") == 0);
        CHECK(out[19] == 0x55);
    }

    /* Сценарий 2: make с нулями и 0xFF, проверка hex-формата */
    {
        uint8_t sha_zero[8] = {0};
        char out[20];
        int ret = af_etag_make(sha_zero, 8, out, 20);
        CHECK(ret == 18);
        CHECK(strcmp(out, "\"0000000000000000\"") == 0);

        uint8_t sha_ff[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        ret = af_etag_make(sha_ff, 8, out, 20);
        CHECK(ret == 18);
        CHECK(strcmp(out, "\"ffffffffffffffff\"") == 0);

        uint8_t sha_mix[8] = {0x0A, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        ret = af_etag_make(sha_mix, 8, out, 20);
        CHECK(ret == 18);
        CHECK(strcmp(out, "\"0ab0000000000000\"") == 0);
    }

    /* Сценарий 3: ошибки make */
    {
        uint8_t sha[8] = {0};
        char out[20];

        /* sha NULL */
        memset(out, 0xAA, sizeof(out));
        int ret = af_etag_make(NULL, 8, out, 20);
        CHECK(ret == -1);
        CHECK(out[0] == 0);

        /* out NULL */
        ret = af_etag_make(sha, 8, NULL, 20);
        CHECK(ret == -1);

        /* sha_len < 8 */
        memset(out, 0xAA, sizeof(out));
        ret = af_etag_make(sha, 7, out, 20);
        CHECK(ret == -1);
        CHECK(out[0] == 0);

        /* cap < 19 */
        memset(out, 0xAA, sizeof(out));
        ret = af_etag_make(sha, 8, out, 18);
        CHECK(ret == -1);
        CHECK(out[0] == 0);

        /* cap 0 */
        ret = af_etag_make(sha, 8, out, 0);
        CHECK(ret == -1);
    }

    /* Сценарий 4: match положительные */
    {
        const char *etag = "\"0123456789abcdef\"";

        CHECK(af_etag_match(etag, etag) == 1);

        const char *w_etag = "W/\"0123456789abcdef\"";
        CHECK(af_etag_match(w_etag, etag) == 1);

        CHECK(af_etag_match("*", etag) == 1);

        const char *list1 = "\"aaa\", \"0123456789abcdef\", \"bbb\"";
        CHECK(af_etag_match(list1, etag) == 1);

        const char *list2 = "\"aaa\", \"bbb\", \"0123456789abcdef\"";
        CHECK(af_etag_match(list2, etag) == 1);

        const char *list3 = "\"0123456789abcdef\", \"aaa\", \"bbb\"";
        CHECK(af_etag_match(list3, etag) == 1);

        const char *list_tab = "\"aaa\"\t, \"0123456789abcdef\"";
        CHECK(af_etag_match(list_tab, etag) == 1);
    }

    /* Сценарий 5: match отрицательные */
    {
        const char *etag = "\"0123456789abcdef\"";

        CHECK(af_etag_match(NULL, etag) == 0);
        CHECK(af_etag_match(etag, NULL) == 0);
        CHECK(af_etag_match(etag, "") == 0);
        CHECK(af_etag_match("", etag) == 0);

        const char *wrong_digit = "\"0123456789abcdee\"";
        CHECK(af_etag_match(wrong_digit, etag) == 0);

        const char *no_quotes = "0123456789abcdef";
        CHECK(af_etag_match(no_quotes, etag) == 0);

        const char *prefix = "\"0123456789abcde\"";
        CHECK(af_etag_match(prefix, etag) == 0);

        const char *suffix = "\"0123456789abcdefx\"";
        CHECK(af_etag_match(suffix, etag) == 0);

        const char *empty_list = ",, ,";
        CHECK(af_etag_match(empty_list, etag) == 0);

        const char *w_empty = "W/";
        CHECK(af_etag_match(w_empty, etag) == 0);
    }

    /* Сценарий 6: устойчивость к длинным строкам */
    {
        const char *etag = "\"0123456789abcdef\"";
        char buf[4096];
        size_t pos = 0;

        /* Заполняем множеством элементов "x" */
        for (int i = 0; i < 100; i++) {
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "\"x\", ");
        }

        /* Добавляем etag в конце */
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "%s", etag);

        CHECK(af_etag_match(buf, etag) == 1);

        /* Теперь без etag */
        pos = 0;
        for (int i = 0; i < 100; i++) {
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "\"x\", ");
        }
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "\"y\"");

        CHECK(af_etag_match(buf, etag) == 0);
    }

    /* e16e: пробелы и табуляции ПЕРЕД запятой и в конце списка тоже игнорируются */
    {
        char e2[24];
        const uint8_t sh2[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        CHECK(af_etag_make(sh2, 8, e2, sizeof e2) == 18);
        char l1[96], l2[96];
        snprintf(l1, sizeof l1, "\"zz\" 	, %s 	,\"q\"", e2);
        snprintf(l2, sizeof l2, "\"zz\" ,%s 	", e2);
        CHECK(af_etag_match(l1, e2) == 1);
        CHECK(af_etag_match(l2, e2) == 1);
        CHECK(af_etag_match("\"zz\" 	,\"q\" ", e2) == 0);
    }

    printf("TESTS %d FAIL %d\n", g_n, g_fail);
    return g_fail == 0 ? 0 : 1;
}
