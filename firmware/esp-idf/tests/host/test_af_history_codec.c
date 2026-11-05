#include "af_test.h"
#include "af_history_codec.h"

static const char *headers[] = {
    "ts,kind,dose_msv,doserate_usvh,pulses_2s,cps,battery,temp_c,flags,n\n",
    "ts,kind,dose_msv,doserate_usvh,pulses_2s,cps,battery,temp_c,flags,n\n",
    "ts,kind,sensor,model,temp_c,hum_pct,press_mmhg,press_pa,flags\n"
};
static const char *rows[] = {
    "123,0,0.25,0.125,40,20,80,25,1,1\n",
    "123,60,0.5,0.25,12,6,70,-3,2,30\n",
    "123,env,2,bme280,25.00,50.00,760.00,101325,11\n"
};

static void encode(int kind, uint8_t *data) {
    if (kind == AF_HQ_KIND_RAW) {
        af_raw_rec_t r = { .ts = 123, .s = {
            .dose_msv = .25f, .doserate_usvh = .125f, .pulses_2s = 40,
            .battery = 80, .temp_c = 25, .flags = 1
        }};
        af_raw_encode(&r, data);
    } else if (kind == AF_HQ_KIND_AGG) {
        af_agg_rec_t r = { .ts = 123, .period_s = 60, .n = 30,
            .dose_msv = .5f, .doserate_usvh = .25f, .pulses_2s = 12,
            .battery = 70, .temp_c = -3, .flags = 2 };
        af_agg_encode(&r, data);
    } else {
        af_env_rec_t r = { .ts = 123, .sensor = 2, .model = AF_SNS_BME280,
            .t_c100 = 2500, .h_c100 = 5000, .p_pa = 101325,
            .flags = AF_ENV_F_T | AF_ENV_F_H | AF_ENV_F_P };
        af_env_encode(&r, data);
    }
}

static void t_wire_rows(void) {
    const size_t sizes[] = { AF_REC_RAW_SIZE, AF_REC_AGG_SIZE, AF_REC_ENV_SIZE };
    for (int kind = 0; kind < 3; kind++) {
        uint8_t data[AF_REC_AGG_SIZE];
        encode(kind, data);
        CHECK(af_history_record_size(kind) == sizes[kind]);
        af_history_row_t row;
        CHECK(af_history_row_decode(kind, data, &row) == 0);
        CHECK(row.ts == 123);
        char buf[256];
        int n = af_history_row_csv(kind, &row, buf, sizeof(buf));
        CHECK(n == (int)strlen(rows[kind]));
        CHECK(strcmp(buf, rows[kind]) == 0);
        for (int cap = 0; cap <= n; cap++) {
            memset(buf, 0xA5, sizeof(buf));
            CHECK(af_history_row_csv(kind, &row, buf, (size_t)cap) < 0);
            CHECK((unsigned char)buf[cap] == 0xA5);
        }
        CHECK(af_history_row_csv(kind, &row, buf, (size_t)n + 1) == n);
        CHECK(strcmp(buf, rows[kind]) == 0);
        data[0] ^= 1;
        CHECK(af_history_row_decode(kind, data, &row) != 0);
    }
}

static void t_headers_and_extensions(void) {
    for (int kind = 0; kind < 3; kind++) {
        for (int ext = 0; ext <= 1; ext++) {
            char want[256], buf[256];
            snprintf(want, sizeof(want), "%s%s", ext ? "mac,seq,epoch," : "", headers[kind]);
            int n = af_history_csv_header(kind, ext, buf, sizeof(buf));
            CHECK(n == (int)strlen(want));
            CHECK(strcmp(buf, want) == 0);
            for (int cap = 0; cap <= n; cap++) {
                memset(buf, 0xA5, sizeof(buf));
                CHECK(af_history_csv_header(kind, ext, buf, (size_t)cap) < 0);
                CHECK((unsigned char)buf[cap] == 0xA5);
            }
            CHECK(af_history_csv_header(kind, ext, buf, (size_t)n + 1) == n);
            CHECK(strcmp(buf, want) == 0);
        }
        uint8_t data[AF_REC_AGG_SIZE];
        encode(kind, data);
        af_history_row_t row;
        CHECK(af_history_row_decode(kind, data, &row) == 0);
        char buf[256], want[256];
        int prefix = af_hq_ext_prefix("AA:BB:CC:DD:EE:FF", UINT32_MAX, UINT32_MAX, buf, sizeof(buf));
        CHECK(prefix > 0);
        CHECK(af_history_row_csv(kind, &row, buf + prefix, sizeof(buf) - (size_t)prefix) > 0);
        snprintf(want, sizeof(want), "AA:BB:CC:DD:EE:FF,4294967295,4294967295,%s", rows[kind]);
        CHECK(strcmp(buf, want) == 0);
    }
}

static void t_invalid_inputs(void) {
    uint8_t data[AF_REC_AGG_SIZE] = {0};
    af_history_row_t row;
    char buf[256];
    CHECK(af_history_record_size(-1) == 0);
    CHECK(af_history_record_size(3) == 0);
    CHECK(af_history_row_decode(3, data, &row) < 0);
    CHECK(af_history_row_decode(0, NULL, &row) < 0);
    CHECK(af_history_row_decode(0, data, NULL) < 0);
    CHECK(af_history_row_csv(3, &row, buf, sizeof(buf)) < 0);
    CHECK(af_history_row_csv(0, NULL, buf, sizeof(buf)) < 0);
    CHECK(af_history_csv_header(3, 0, buf, sizeof(buf)) < 0);
    CHECK(af_history_csv_header(0, 1, NULL, sizeof(buf)) < 0);
}

int main(void) {
    RUN(t_wire_rows);
    RUN(t_headers_and_extensions);
    RUN(t_invalid_inputs);
    TESTS_DONE();
}
