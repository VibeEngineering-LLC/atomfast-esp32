#include "af_test.h"
#include "af_env.h"
#include <math.h>

static void t_h_off_limit(void) {
    af_env_in_t in = {
        .sensor = 2,
        .model = AF_SNS_BME280,
        .enabled = 1,
        .t_off10 = 0,
        .p_off10 = 0,
        .h_off10 = 150,
        .caps = AF_CAP_TEMP | AF_CAP_PRESS | AF_CAP_HUM,
        .t_c = 23.0f,
        .p_pa = 100000.0f,
        .h_pct = 40.0f,
        .ts = 1761300000
    };
    af_env_rec_t out;
    float t_shown, p_shown_mmhg, h_shown;

    int rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown_mmhg, &h_shown);
    CHECK(rc == 0);
    CHECK(fabsf(h_shown - 50.0f) < 0.001f);
    CHECK(out.h_c100 == 5000);

    in.h_off10 = -150;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown_mmhg, &h_shown);
    CHECK(rc == 0);
    CHECK(fabsf(h_shown - 30.0f) < 0.001f);
    CHECK(out.h_c100 == 3000);

    in.h_pct = 95.0f;
    in.h_off10 = 100;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown_mmhg, &h_shown);
    CHECK(rc == 0);
    CHECK(h_shown == 100.0f);
    CHECK(out.h_c100 == 10000);

    in.h_pct = 3.0f;
    in.h_off10 = -100;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown_mmhg, &h_shown);
    CHECK(rc == 0);
    CHECK(h_shown == 0.0f);
    CHECK(out.h_c100 == 0);
    CHECK((out.flags & AF_ENV_F_H) != 0);
}

static void t_t_p_off_limits(void) {
    af_env_in_t in = {
        .sensor = 2,
        .model = AF_SNS_BME280,
        .enabled = 1,
        .t_off10 = 100,
        .p_off10 = 0,
        .h_off10 = 0,
        .caps = AF_CAP_TEMP | AF_CAP_PRESS | AF_CAP_HUM,
        .t_c = 23.0f,
        .p_pa = 100000.0f,
        .h_pct = 40.0f,
        .ts = 1761300000
    };
    af_env_rec_t out;
    float t_shown, p_shown_mmhg, h_shown;

    int rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown_mmhg, &h_shown);
    CHECK(rc == 0);
    CHECK(fabsf(t_shown - 28.0f) < 0.001f);
    CHECK((out.flags & AF_ENV_F_CORR) != 0);

    in.t_off10 = -100;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown_mmhg, &h_shown);
    CHECK(rc == 0);
    CHECK(fabsf(t_shown - 18.0f) < 0.001f);
    CHECK((out.flags & AF_ENV_F_CORR) != 0);

    in.t_off10 = 0;
    in.p_off10 = 500;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown_mmhg, &h_shown);
    CHECK(rc == 0);
    CHECK(fabsf(p_shown_mmhg - 770.06f) < 0.01f);
    CHECK((out.flags & AF_ENV_F_CORR) != 0);
}

static void t_sensor_model_passthrough(void) {
    af_env_in_t in = {
        .sensor = 1,
        .model = AF_SNS_BME280,
        .enabled = 1,
        .t_off10 = 0,
        .p_off10 = 0,
        .h_off10 = 0,
        .caps = AF_CAP_TEMP | AF_CAP_PRESS | AF_CAP_HUM,
        .t_c = 23.0f,
        .p_pa = 100000.0f,
        .h_pct = 40.0f,
        .ts = 1761300000
    };
    af_env_rec_t out;
    float t_shown, p_shown_mmhg, h_shown;

    int rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown_mmhg, &h_shown);
    CHECK(rc == 0);
    CHECK(out.sensor == 1);
    CHECK(out.model == AF_SNS_BME280);

    in.sensor = 5;
    in.model = AF_SNS_SHT3X;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown_mmhg, &h_shown);
    CHECK(rc == 0);
    CHECK(out.sensor == 5);
    CHECK(out.model == AF_SNS_SHT3X);

    in.sensor = 8;
    in.model = AF_SNS_BME280;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown_mmhg, &h_shown);
    CHECK(rc == 0);
    CHECK(out.sensor == 8);
    CHECK(out.model == AF_SNS_BME280);
}

int main(void) {
    RUN(t_h_off_limit);
    RUN(t_t_p_off_limits);
    RUN(t_sensor_model_passthrough);
    TESTS_DONE();
    return 0;
}
