#include "af_fmtval.h"
#include "af_test.h"

/* Боевые образцы */
static void test_samples(void) {
    char v[64], s[64], e[64];

    af_fmt_rate(v, 64, s, 64, e, 64, 0.0759, 4.23);
    CHECK(strcmp(v, "0.076") == 0);
    CHECK(strcmp(s, "0.003") == 0);
    CHECK(strcmp(e, "4.2") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 0.0794586, 3.02199);
    CHECK(strcmp(v, "0.0795") == 0);
    CHECK(strcmp(s, "0.0024") == 0);
    CHECK(strcmp(e, "3.0") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 0.0779581, 0.9648);
    CHECK(strcmp(v, "0.0780") == 0);
    CHECK(strcmp(s, "0.0008") == 0);
    CHECK(strcmp(e, "1.0") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 0.0777227, 0.395758);
    CHECK(strcmp(v, "0.0777") == 0);
    CHECK(strcmp(s, "0.0003") == 0);
    CHECK(strcmp(e, "0.4") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 0.0788617, 0.196018);
    CHECK(strcmp(v, "0.07886") == 0);
    CHECK(strcmp(s, "0.00015") == 0);
    CHECK(strcmp(e, "0.2") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 0.0790608, 0.11308);
    CHECK(strcmp(v, "0.07906") == 0);
    CHECK(strcmp(s, "0.00009") == 0);
    CHECK(strcmp(e, "0.1") == 0);
}

/* Крайние случаи */
static void test_edge(void) {
    char v[64], s[64], e[64];

    af_fmt_rate(v, 64, s, 64, e, 64, 7.99, 3.7);
    CHECK(strcmp(v, "8.0") == 0);
    CHECK(strcmp(s, "0.3") == 0);
    CHECK(strcmp(e, "3.7") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 0.1, 9.96);
    CHECK(strcmp(v, "0.100") == 0);
    CHECK(strcmp(s, "0.010") == 0);
    CHECK(strcmp(e, "10.0") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 50.0, 150.0);
    CHECK(strcmp(v, "50") == 0);
    CHECK(strcmp(s, "80") == 0);
    CHECK(strcmp(e, "150.0") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 0.0, 5.0);
    CHECK(strcmp(v, "0") == 0);
    CHECK(strcmp(s, "0") == 0);
    CHECK(strcmp(e, "5.0") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 5.0, 0.0);
    CHECK(strcmp(v, "5") == 0);
    CHECK(strcmp(s, "0") == 0);
    CHECK(strcmp(e, "0.0") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, NAN, 3.0);
    CHECK(strcmp(v, "") == 0);
    CHECK(strcmp(s, "") == 0);
    CHECK(strcmp(e, "") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 0.08, NAN);
    CHECK(strcmp(v, "0.08") == 0);
    CHECK(strcmp(s, "") == 0);
    CHECK(strcmp(e, "") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 0.08, -1.0);
    CHECK(strcmp(v, "0.08") == 0);
    CHECK(strcmp(s, "") == 0);
    CHECK(strcmp(e, "") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 123456.0, 0.5);
    CHECK(strcmp(v, "123500") == 0);
    CHECK(strcmp(s, "600") == 0);
    CHECK(strcmp(e, "0.5") == 0);

    af_fmt_rate(v, 64, s, 64, e, 64, 1.2e-9, 10.0);
    CHECK(strcmp(v, "1.20e-9") == 0);
    CHECK(strcmp(s, "1.2e-10") == 0);
    CHECK(strcmp(e, "10.0") == 0);
}

/* Малый буфер: не переполняется, нуль-терминирован */
static void test_small_buf(void) {
    char v[3], s[3], e[3];
    af_fmt_rate(v, 3, s, 3, e, 3, 0.0759, 4.23);
    CHECK(v[2] == '\0');  /* cap=3: максимум 2 символа + NUL */
    CHECK(s[2] == '\0');
    CHECK(e[2] == '\0');

    char v2[1], s2[1], e2[1];
    af_fmt_rate(v2, 1, s2, 1, e2, 1, 123456.0, 0.5);
    CHECK(v2[0] == '\0');
    CHECK(s2[0] == '\0');
    CHECK(e2[0] == '\0');

    char v3[3], s3[3], e3[3];
    af_fmt_rate(v3, 3, s3, 3, e3, 3, 1.2e-9, 10.0);
    CHECK(v3[2] == '\0');
    CHECK(s3[2] == '\0');
    CHECK(e3[2] == '\0');
}

/* T-55: общий вектор с JS (uncertainty.js rateEps): знак, порог экспоненты */
static void test_js_parity(void) {
    char v[64], s[64], e[64];
    af_fmt_rate(v, 64, s, 64, e, 64, -0.0759, 4.23);
    CHECK(strcmp(v, "-0.076") == 0);
    CHECK(strcmp(e, "4.2") == 0);
    af_fmt_rate(v, 64, s, 64, e, 64, 5e-5, 10.0);
    CHECK(strcmp(v, "5.0e-5") == 0);
    af_fmt_rate(v, 64, s, 64, e, 64, 2.5e12, 4.0);
    CHECK(strcmp(v, "2.50e12") == 0);
}

int main(void) {
    RUN(test_samples);
    RUN(test_edge);
    RUN(test_small_buf);
    RUN(test_js_parity);
    TESTS_DONE();
    return 0;
}
