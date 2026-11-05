#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "af_ds.h"

static int g_n = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    g_n++; \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        g_fail++; \
    } \
} while (0)

int main(void) {
    /* Сценарий 1: Пример из описания */
    {
        af_ds_acc_t a;
        af_ds_reset(&a);
        CHECK(af_ds_add(&a, 1000, 5.0, 0.10, 4, 90, 20, 0, 1, 2) == 0);
        CHECK(af_ds_add(&a, 1002, 5.5, 0.20, 6, 80, 25, 1, 1, 2) == 0);
        
        char buf[256];
        int len = af_ds_row(&a, buf, sizeof(buf));
        const char *expected = "1000,4,5.5,0.15,5,2.5,80,25,1,2,0.1,0.2,2,3\n";
        CHECK(len == (int)strlen(expected));
        CHECK(strcmp(buf, expected) == 0);
    }

    /* Сценарий 2: Заголовок */
    {
        char buf[256];
        const char *expected = "ts,kind,dose_msv,doserate_usvh,pulses_2s,cps,battery,temp_c,flags,n,doserate_min,doserate_max,cps_min,cps_max\n";
        int len = af_ds_header(buf, sizeof(buf));
        CHECK(len == (int)strlen(expected));
        CHECK(strcmp(buf, expected) == 0);

        /* cap на 1 меньше нужного */
        memset(buf, 0, sizeof(buf));
        len = af_ds_header(buf, (int)strlen(expected));
        CHECK(len == -1);
        CHECK(buf[0] == 0);

        /* buf == NULL */
        CHECK(af_ds_header(NULL, 256) == -1);
    }

    /* Сценарий 3: Взвешенное среднее */
    {
        af_ds_acc_t a;
        af_ds_reset(&a);
        /* ts=100, rate=1.0, pulses=4, w=10 */
        CHECK(af_ds_add(&a, 100, 0.0, 1.0, 4, 100, 20, 0, 10, 60) == 0);
        /* ts=200, rate=2.0, pulses=8, w=30 */
        CHECK(af_ds_add(&a, 200, 0.0, 2.0, 8, 100, 20, 0, 30, 60) == 0);

        char buf[256];
        int len = af_ds_row(&a, buf, sizeof(buf));
        CHECK(len > 0);

        /* Разбор строки: ts_first,span,dose_last,rate_avg,pulses_avg,cps_avg,battery_last,temp_last,flags_or,w,rate_min,rate_max,cps_min,cps_max */
        unsigned long ts_first, span, w;
        double dose_last, rate_avg, pulses_avg, cps_avg, rate_min, rate_max, cps_min, cps_max;
        int battery_last, temp_last, flags_or;
        
        int parsed = sscanf(buf, "%lu,%lu,%lf,%lf,%lf,%lf,%d,%d,%u,%lu,%lf,%lf,%lf,%lf",
                            &ts_first, &span, &dose_last, &rate_avg, &pulses_avg, &cps_avg,
                            &battery_last, &temp_last, &flags_or, &w,
                            &rate_min, &rate_max, &cps_min, &cps_max);
        CHECK(parsed == 14);
        CHECK(w == 40);
        CHECK(fabs(rate_avg - 1.75) < 1e-9);
        CHECK(fabs(pulses_avg - 7.0) < 1e-9);
        /* span = ts_last - ts_first + step_s = 200 - 100 + 60 = 160 */
        CHECK(span == 160);
    }

    /* Сценарий 4: Отказы add */
    {
        af_ds_acc_t a;
        af_ds_reset(&a);
        /* ts == 0 */
        CHECK(af_ds_add(&a, 0, 1.0, 1.0, 1, 100, 20, 0, 1, 2) == -1);
        CHECK(a.src == 0);
        
        /* weight == 0 */
        CHECK(af_ds_add(&a, 100, 1.0, 1.0, 1, 100, 20, 0, 0, 2) == -1);
        CHECK(a.src == 0);

        /* NaN в rate */
        CHECK(af_ds_add(&a, 100, 1.0, NAN, 1, 100, 20, 0, 1, 2) == -1);
        CHECK(a.src == 0);

        /* INFINITY в dose */
        CHECK(af_ds_add(&a, 100, INFINITY, 1.0, 1, 100, 20, 0, 1, 2) == -1);
        CHECK(a.src == 0);

        /* NaN в pulses */
        CHECK(af_ds_add(&a, 100, 1.0, 1.0, NAN, 100, 20, 0, 1, 2) == -1);
        CHECK(a.src == 0);

        /* Корректная запись принимается */
        CHECK(af_ds_add(&a, 100, 1.0, 1.0, 1, 100, 20, 0, 1, 2) == 0);
        CHECK(a.src != 0);
    }

    /* Сценарий 5: Пустая корзина */
    {
        af_ds_acc_t a;
        af_ds_reset(&a);
        char buf[256];
        CHECK(af_ds_row(&a, buf, sizeof(buf)) == -1);
    }

    /* Сценарий 6: af_ds_bucket */
    {
        CHECK(af_ds_bucket(10, 5) == 2);
        CHECK(af_ds_bucket(11, 5) == 3);
        CHECK(af_ds_bucket(1, 2000) == 1);
        CHECK(af_ds_bucket(0, 10) == 1);
        CHECK(af_ds_bucket(5, 0) == 1);
        CHECK(af_ds_bucket(4294967295u, 2) == 2147483648u);
        CHECK(af_ds_bucket(2000, 2000) == 1);
        CHECK(af_ds_bucket(2001, 2000) == 2);
        CHECK(af_ds_bucket(181491, 900) == 202);
    }

    /* Сценарий 7: Прореживание всего ряда */
    {
        const int total_pts = 1000;
        const int bucket_size = af_ds_bucket(total_pts, 100); /* 10 */
        CHECK(bucket_size == 10);

        af_ds_acc_t a;
        af_ds_reset(&a);

        int rows_count = 0;
        unsigned long total_w = 0;
        double sum_rate_weighted = 0.0;
        double sum_rate_total = 0.0;
        int all_min_max_ok = 1;

        for (int i = 0; i < total_pts; i++) {
            unsigned long ts = 1700000000UL + 2UL * i;
            double rate = 0.1 + 0.001 * (i % 50);
            int pulses = (i % 13) + 1;
            
            CHECK(af_ds_add(&a, ts, 0.0, rate, pulses, 100, 20, 0, 1, 2) == 0);
            sum_rate_total += rate;

            if ((i + 1) % bucket_size == 0) {
                char buf[512];
                int len = af_ds_row(&a, buf, sizeof(buf));
                CHECK(len > 0);
                rows_count++;

                unsigned long ts_first, span, w;
                double dose_last, rate_avg, pulses_avg, cps_avg, rate_min, rate_max, cps_min, cps_max;
                int battery_last, temp_last, flags_or;
                
                int parsed = sscanf(buf, "%lu,%lu,%lf,%lf,%lf,%lf,%d,%d,%u,%lu,%lf,%lf,%lf,%lf",
                                    &ts_first, &span, &dose_last, &rate_avg, &pulses_avg, &cps_avg,
                                    &battery_last, &temp_last, &flags_or, &w,
                                    &rate_min, &rate_max, &cps_min, &cps_max);
                CHECK(parsed == 14);
                
                total_w += w;
                sum_rate_weighted += rate_avg * (double)w;

                /* min <= avg <= max */
                if (!(rate_min <= rate_avg && rate_avg <= rate_max)) all_min_max_ok = 0;
                if (!(cps_min <= cps_avg && cps_avg <= cps_max)) all_min_max_ok = 0;

                af_ds_reset(&a);
            }
        }

        CHECK(rows_count == 100);
        CHECK(total_w == 1000);
        CHECK(all_min_max_ok == 1);

        double avg_rate_rows = sum_rate_weighted / (double)total_w;
        double avg_rate_raw = sum_rate_total / (double)total_pts;
        CHECK(fabs(avg_rate_rows - avg_rate_raw) < 1e-4);
    }

    /* Сценарий 8: Устойчивость значений */
    {
        af_ds_acc_t a;
        af_ds_reset(&a);
        
        /* rate 1e30, temp -40, battery -1, flags 0x01 */
        CHECK(af_ds_add(&a, 100, 0.0, 1e30, 1, -1, -40, 0x01, 1, 2) == 0);
        /* flags 0x02 */
        CHECK(af_ds_add(&a, 102, 0.0, 1e30, 1, -1, -40, 0x02, 1, 2) == 0);
        /* flags 0x80 */
        CHECK(af_ds_add(&a, 104, 0.0, 1e30, 1, -1, -40, 0x80, 1, 2) == 0);

        char buf[512];
        int len = af_ds_row(&a, buf, sizeof(buf));
        CHECK(len > 0);

        unsigned long ts_first, span, w;
        double dose_last, rate_avg, pulses_avg, cps_avg, rate_min, rate_max, cps_min, cps_max;
        int battery_last, temp_last, flags_or;
        
        int parsed = sscanf(buf, "%lu,%lu,%lf,%lf,%lf,%lf,%d,%d,%u,%lu,%lf,%lf,%lf,%lf",
                            &ts_first, &span, &dose_last, &rate_avg, &pulses_avg, &cps_avg,
                            &battery_last, &temp_last, &flags_or, &w,
                            &rate_min, &rate_max, &cps_min, &cps_max);
        CHECK(parsed == 14);
        
        CHECK(battery_last == -1);
        CHECK(temp_last == -40);
        CHECK(flags_or == (0x01 | 0x02 | 0x80)); /* 131 */
        CHECK(rate_avg == 1e30);
    }

    /* Сценарий 9 (e16e): минимум и максимум не в краях корзины - убывающая, затем возрастающая запись */
    {
        af_ds_acc_t a;
        char row[256];
        af_ds_reset(&a);
        CHECK(af_ds_add(&a, 2000, 1.0f, 0.30f, 6.0f, 90, 20, 0, 1, 2) == 0);
        CHECK(af_ds_add(&a, 2002, 1.1f, 0.10f, 2.0f, 90, 20, 0, 1, 2) == 0);
        CHECK(af_ds_add(&a, 2004, 1.2f, 0.50f, 10.0f, 90, 20, 0, 1, 2) == 0);
        CHECK(af_ds_add(&a, 2006, 1.3f, 0.20f, 4.0f, 90, 20, 0, 1, 2) == 0);
        CHECK(af_ds_row(&a, row, sizeof(row)) > 0);
        /* rate: min 0.1, max 0.5, avg 0.275; cps: min 1, max 5 */
        CHECK(strcmp(row, "2000,8,1.3,0.275,5.5,2.75,90,20,0,4,0.1,0.5,1,5\n") == 0);
    }

    printf("TESTS %d FAIL %d\n", g_n, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
