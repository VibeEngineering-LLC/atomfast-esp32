#include "af_test.h"
#include "af_sns_reg.h"
#include <math.h>
#include <string.h>

static uint8_t crc8_calc(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

static void t_default_valid(void) {
    af_sns_reg_t r;
    af_sns_reg_default(&r);
    CHECK(af_sns_reg_valid(&r) == 1);
    CHECK(af_sns_reg_count(&r) == 1);
    af_sns_cfg_t *c1 = af_sns_reg_find(&r, 1);
    CHECK(c1 != NULL);
    if (c1) CHECK(c1->model == AF_SNS_BMP280);
    CHECK(af_sns_reg_find(&r, 2) == NULL);
    CHECK(af_sns_reg_find(&r, 0) == NULL);
    CHECK(af_sns_reg_find(&r, 9) == NULL);
    CHECK(r.agg_s == 5);

    // agg_s=0
    r.agg_s = 0;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.agg_s = 5;
    // agg_s=61
    r.agg_s = 61;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.agg_s = 5;
    // period_s=4
    r.s[0].period_s = 4;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.s[0].period_s = 30;
    // period_s=601
    r.s[0].period_s = 601;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.s[0].period_s = 30;
    // enabled=2
    r.s[0].enabled = 2;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.s[0].enabled = 1;
    // addr=0x77 with SHT3X
    r.s[0].model = AF_SNS_SHT3X;
    r.s[0].addr = 0x77;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.s[0].model = AF_SNS_BMP280;
    r.s[0].addr = 0x76;
    // t_off10=51
    r.s[0].t_off10 = 51;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.s[0].t_off10 = 0;
    // p_off10=201
    r.s[0].p_off10 = 201;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.s[0].p_off10 = 0;
    // h_off10=1 for BMP280
    r.s[0].h_off10 = 1;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.s[0].h_off10 = 0;
    // sensor=2 in slot 0
    r.s[0].sensor = 2;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.s[0].sensor = 1;
    // occupied slot 1 with same addr as slot 0
    r.s[1].sensor = 2;
    r.s[1].model = AF_SNS_BMP280;
    r.s[1].addr = 0x76;
    r.s[1].enabled = 0;
    r.s[1].period_s = 30;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.s[1].sensor = 0;
    r.s[1].addr = 0;
    // free slot with non-zero addr
    r.s[1].sensor = 0;
    r.s[1].addr = 0x44;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.s[1].addr = 0;
    r.s[1].period_s = 0;
    r.s[1].model = 0;
    r.s[1].enabled = 0;
    CHECK(af_sns_reg_valid(&r) == 1);
}

static void t_add_rules(void) {
    af_sns_reg_t r;
    af_sns_reg_default(&r);
    CHECK(af_sns_reg_add(&r, AF_SNS_SHT3X, 0x44, 0) == 2);
    CHECK(af_sns_reg_add(&r, AF_SNS_AHT2X, 0x38, 1) == 3);
    CHECK(af_sns_reg_add(&r, AF_SNS_BMP280, 0x77, 0) == 4);
    CHECK(af_sns_reg_add(&r, AF_SNS_SHT3X, 0x45, 0) == 5);
    CHECK(af_sns_reg_add(&r, AF_SNS_SHT3X, 0x44, 0) == -2);
    CHECK(af_sns_reg_add(&r, AF_SNS_BMP280, 0x76, 0) == -2);
    CHECK(af_sns_reg_add(&r, AF_SNS_AHT2X, 0x44, 0) == -1);
    CHECK(af_sns_reg_add(&r, 7, 0x76, 0) == -1);
    CHECK(af_sns_reg_add(&r, AF_SNS_BME280, 0x75, 0) == -1);
    CHECK(af_sns_reg_add(&r, AF_SNS_BMP280, 0x77, 2) == -4);
    CHECK(af_sns_reg_add(NULL, AF_SNS_BMP280, 0x76, 0) == -4);
    CHECK(af_sns_reg_count(&r) == 5);
    CHECK(af_sns_reg_valid(&r) == 1);

    CHECK(af_sns_reg_remove(&r, 3) == 0);
    CHECK(af_sns_reg_remove(&r, 3) == -1);
    CHECK(af_sns_reg_remove(&r, 9) == -1);
    CHECK(af_sns_reg_count(&r) == 4);
    CHECK(af_sns_reg_add(&r, AF_SNS_AHT2X, 0x38, 0) == 3);
    
    af_sns_cfg_t *c1 = af_sns_reg_find(&r, 1);
    af_sns_cfg_t *c2 = af_sns_reg_find(&r, 2);
    af_sns_cfg_t *c4 = af_sns_reg_find(&r, 4);
    af_sns_cfg_t *c5 = af_sns_reg_find(&r, 5);
    if (c1) CHECK(c1->addr == 0x76);
    if (c2) CHECK(c2->addr == 0x44);
    if (c4) CHECK(c4->addr == 0x77);
    if (c5) CHECK(c5->addr == 0x45);

    // Test -3
    af_sns_reg_t r2;
    memset(&r2, 0, sizeof(r2));
    for (int i = 0; i < 8; i++) {
        r2.s[i].sensor = (uint8_t)(i + 1);
        r2.s[i].model = AF_SNS_BMP280;
        r2.s[i].addr = (uint8_t)(0x10 + i);
        r2.s[i].enabled = 0;
        r2.s[i].period_s = 30;
    }
    CHECK(af_sns_reg_add(&r2, AF_SNS_SHT3X, 0x44, 0) == -3);
}

static void t_order_grouping(void) {
    af_sns_reg_t r;
    memset(&r, 0, sizeof(r));
    r.s[0].sensor = 1; r.s[0].model = AF_SNS_BMP280; r.s[0].addr = 0x76; r.s[0].enabled = 1; r.s[0].period_s = 30;
    r.s[1].sensor = 2; r.s[1].model = AF_SNS_AHT2X; r.s[1].addr = 0x38; r.s[1].enabled = 1; r.s[1].period_s = 30;
    r.s[2].sensor = 3; r.s[2].model = AF_SNS_SHT3X; r.s[2].addr = 0x44; r.s[2].enabled = 1; r.s[2].period_s = 30;
    r.s[3].sensor = 4; r.s[3].model = AF_SNS_BMP280; r.s[3].addr = 0x77; r.s[3].enabled = 1; r.s[3].period_s = 30;
    r.s[4].sensor = 5; r.s[4].model = AF_SNS_SHT3X; r.s[4].addr = 0x45; r.s[4].enabled = 1; r.s[4].period_s = 30;
    r.agg_s = 5;

    uint8_t out[8] = {0};
    int n = af_sns_reg_order(&r, out);
    CHECK(n == 5);
    if (n == 5) {
        CHECK(out[0] == 1);
        CHECK(out[1] == 4);
        CHECK(out[2] == 3);
        CHECK(out[3] == 5);
        CHECK(out[4] == 2);
    }

    af_sns_reg_t r2;
    memset(&r2, 0, sizeof(r2));
    r2.s[0].sensor = 1; r2.s[0].model = AF_SNS_BME280; r2.s[0].addr = 0x76; r2.s[0].enabled = 1; r2.s[0].period_s = 30;
    r2.s[1].sensor = 2; r2.s[1].model = AF_SNS_SHT3X; r2.s[1].addr = 0x44; r2.s[1].enabled = 1; r2.s[1].period_s = 30;
    r2.s[2].sensor = 3; r2.s[2].model = AF_SNS_BMP280; r2.s[2].addr = 0x77; r2.s[2].enabled = 1; r2.s[2].period_s = 30;
    r2.agg_s = 5;

    uint8_t out2[8] = {0};
    int n2 = af_sns_reg_order(&r2, out2);
    CHECK(n2 == 3);
    if (n2 == 3) {
        CHECK(out2[0] == 3);
        CHECK(out2[1] == 1);
        CHECK(out2[2] == 2);
    }

    af_sns_reg_t r3;
    memset(&r3, 0, sizeof(r3));
    uint8_t out3[8] = {0};
    int n3 = af_sns_reg_order(&r3, out3);
    CHECK(n3 == 0);
}

static void t_enable_model(void) {
    af_sns_reg_t r;
    memset(&r, 0, sizeof(r));
    r.s[0].sensor = 1; r.s[0].model = AF_SNS_BMP280; r.s[0].addr = 0x76; r.s[0].enabled = 1; r.s[0].period_s = 30;
    r.s[1].sensor = 2; r.s[1].model = AF_SNS_AHT2X; r.s[1].addr = 0x38; r.s[1].enabled = 1; r.s[1].period_s = 30;
    r.s[2].sensor = 3; r.s[2].model = AF_SNS_SHT3X; r.s[2].addr = 0x44; r.s[2].enabled = 1; r.s[2].period_s = 30;
    r.s[3].sensor = 4; r.s[3].model = AF_SNS_BMP280; r.s[3].addr = 0x77; r.s[3].enabled = 1; r.s[3].period_s = 30;
    r.s[4].sensor = 5; r.s[4].model = AF_SNS_SHT3X; r.s[4].addr = 0x45; r.s[4].enabled = 1; r.s[4].period_s = 30;
    r.agg_s = 5;

    CHECK(af_sns_reg_enable_model(&r, AF_SNS_BMP280, 0) == 2);
    CHECK(r.s[0].enabled == 0);
    CHECK(r.s[3].enabled == 0);
    CHECK(r.s[1].enabled == 1);
    CHECK(r.s[2].enabled == 1);
    CHECK(r.s[4].enabled == 1);

    CHECK(af_sns_reg_enable_model(&r, AF_SNS_BMP280, 1) == 2);
    CHECK(r.s[0].enabled == 1);
    CHECK(r.s[3].enabled == 1);

    CHECK(af_sns_reg_enable_model(&r, AF_SNS_BME280, 0) == 0);
    CHECK(af_sns_reg_enable_model(&r, 99, 0) == -1);
    
    af_sns_reg_t r_copy;
    memcpy(&r_copy, &r, sizeof(r));
    CHECK(af_sns_reg_enable_model(&r, AF_SNS_SHT3X, 2) == -2);
    CHECK(memcmp(&r, &r_copy, sizeof(r)) == 0);
    
    CHECK(af_sns_reg_enable_model(NULL, AF_SNS_BMP280, 0) == -1);
}

static void t_patch_rules(void) {
    af_sns_reg_t r;
    af_sns_reg_default(&r);
    
    af_sns_patch_t p;
    memset(&p, 0, sizeof(p));
    p.has_t_off = 1;
    p.t_off = 0.3f;
    CHECK(af_sns_reg_patch(&r, 1, &p) == 0);
    af_sns_cfg_t *c1 = af_sns_reg_find(&r, 1);
    if (c1) CHECK(c1->t_off10 == 3);

    memset(&p, 0, sizeof(p));
    p.has_p_off = 1;
    p.p_off = -2.5f;
    CHECK(af_sns_reg_patch(&r, 1, &p) == 0);
    if (c1) CHECK(c1->p_off10 == -25);

    memset(&p, 0, sizeof(p));
    p.has_period = 1;
    p.period_s = 600;
    CHECK(af_sns_reg_patch(&r, 1, &p) == 0);
    memset(&p, 0, sizeof(p));
    p.has_period = 1;
    p.period_s = 5;
    CHECK(af_sns_reg_patch(&r, 1, &p) == 0);

    memset(&p, 0, sizeof(p));
    p.has_enabled = 1;
    p.enabled = 0;
    CHECK(af_sns_reg_patch(&r, 1, &p) == 0);

    af_sns_reg_t r_copy;
    memcpy(&r_copy, &r, sizeof(r));
    
    memset(&p, 0, sizeof(p));
    p.has_t_off = 1;
    p.t_off = 5.01f;
    CHECK(af_sns_reg_patch(&r, 1, &p) == -2);
    CHECK(memcmp(&r, &r_copy, sizeof(r)) == 0);

    p.t_off = -5.01f;
    CHECK(af_sns_reg_patch(&r, 1, &p) == -2);
    CHECK(memcmp(&r, &r_copy, sizeof(r)) == 0);

    p.t_off = NAN;
    CHECK(af_sns_reg_patch(&r, 1, &p) == -2);
    CHECK(memcmp(&r, &r_copy, sizeof(r)) == 0);

    p.t_off = INFINITY;
    CHECK(af_sns_reg_patch(&r, 1, &p) == -2);
    CHECK(memcmp(&r, &r_copy, sizeof(r)) == 0);

    memset(&p, 0, sizeof(p));
    p.has_p_off = 1;
    p.p_off = 20.5f;
    CHECK(af_sns_reg_patch(&r, 1, &p) == -2);
    CHECK(memcmp(&r, &r_copy, sizeof(r)) == 0);

    memset(&p, 0, sizeof(p));
    p.has_period = 1;
    p.period_s = 4;
    CHECK(af_sns_reg_patch(&r, 1, &p) == -2);
    CHECK(memcmp(&r, &r_copy, sizeof(r)) == 0);

    p.period_s = 601;
    CHECK(af_sns_reg_patch(&r, 1, &p) == -2);
    CHECK(memcmp(&r, &r_copy, sizeof(r)) == 0);

    memset(&p, 0, sizeof(p));
    p.has_enabled = 1;
    p.enabled = 2;
    CHECK(af_sns_reg_patch(&r, 1, &p) == -2);
    CHECK(memcmp(&r, &r_copy, sizeof(r)) == 0);

    memset(&p, 0, sizeof(p));
    p.has_enabled = 1;
    p.enabled = 0;
    p.has_t_off = 1;
    p.t_off = 6.0f;
    CHECK(af_sns_reg_patch(&r, 1, &p) == -2);
    CHECK(memcmp(&r, &r_copy, sizeof(r)) == 0);

    // Boundaries
    memset(&p, 0, sizeof(p));
    p.has_t_off = 1;
    p.t_off = 5.0f;
    CHECK(af_sns_reg_patch(&r, 1, &p) == 0);
    if (c1) CHECK(c1->t_off10 == 50);

    memset(&p, 0, sizeof(p));
    p.has_p_off = 1;
    p.p_off = 20.0f;
    CHECK(af_sns_reg_patch(&r, 1, &p) == 0);
    if (c1) CHECK(c1->p_off10 == 200);

    // Add SHT3X
    CHECK(af_sns_reg_add(&r, AF_SNS_SHT3X, 0x44, 0) == 2);
    af_sns_cfg_t *c2 = af_sns_reg_find(&r, 2);
    
    memset(&p, 0, sizeof(p));
    p.has_p_off = 1;
    p.p_off = 1.0f;
    CHECK(af_sns_reg_patch(&r, 2, &p) == -2);

    memset(&p, 0, sizeof(p));
    p.has_p_off = 1;
    p.p_off = 0.0f;
    CHECK(af_sns_reg_patch(&r, 2, &p) == 0);

    memset(&p, 0, sizeof(p));
    p.has_h_off = 1;
    p.h_off = 10.0f;
    CHECK(af_sns_reg_patch(&r, 2, &p) == 0);
    if (c2) CHECK(c2->h_off10 == 100);

    memset(&p, 0, sizeof(p));
    p.has_h_off = 1;
    p.h_off = 10.1f;
    CHECK(af_sns_reg_patch(&r, 2, &p) == -2);

    // BMP280 h_off
    memset(&p, 0, sizeof(p));
    p.has_h_off = 1;
    p.h_off = 1.0f;
    CHECK(af_sns_reg_patch(&r, 1, &p) == -2);

    CHECK(af_sns_reg_patch(&r, 9, &p) == -1);
    CHECK(af_sns_reg_patch(NULL, 1, &p) == -1);
    CHECK(af_sns_reg_patch(&r, 1, NULL) == -1);
}

static void t_agg(void) {
    af_sns_reg_t r;
    af_sns_reg_default(&r);
    CHECK(af_sns_reg_set_agg(&r, 1) == 0);
    CHECK(r.agg_s == 1);
    CHECK(af_sns_reg_set_agg(&r, 60) == 0);
    CHECK(r.agg_s == 60);
    CHECK(af_sns_reg_set_agg(&r, 0) == -2);
    CHECK(r.agg_s == 60);
    CHECK(af_sns_reg_set_agg(&r, 61) == -2);
    CHECK(r.agg_s == 60);
    CHECK(af_sns_reg_set_agg(NULL, 1) == -1);
}

static void t_blob(void) {
    af_sns_reg_t r;
    memset(&r, 0, sizeof(r));
    r.s[0].sensor = 1; r.s[0].model = AF_SNS_BMP280; r.s[0].addr = 0x76; r.s[0].enabled = 1; r.s[0].t_off10 = -7; r.s[0].p_off10 = 33; r.s[0].h_off10 = 0; r.s[0].period_s = 77;
    r.s[1].sensor = 2; r.s[1].model = AF_SNS_AHT2X; r.s[1].addr = 0x38; r.s[1].enabled = 1; r.s[1].t_off10 = -7; r.s[1].p_off10 = 0; r.s[1].h_off10 = -100; r.s[1].period_s = 77;
    r.s[2].sensor = 3; r.s[2].model = AF_SNS_SHT3X; r.s[2].addr = 0x44; r.s[2].enabled = 1; r.s[2].t_off10 = -7; r.s[2].p_off10 = 0; r.s[2].h_off10 = -100; r.s[2].period_s = 77;
    r.s[3].sensor = 4; r.s[3].model = AF_SNS_BMP280; r.s[3].addr = 0x77; r.s[3].enabled = 1; r.s[3].t_off10 = -7; r.s[3].p_off10 = 33; r.s[3].h_off10 = 0; r.s[3].period_s = 77;
    r.s[4].sensor = 5; r.s[4].model = AF_SNS_SHT3X; r.s[4].addr = 0x45; r.s[4].enabled = 1; r.s[4].t_off10 = -7; r.s[4].p_off10 = 0; r.s[4].h_off10 = -100; r.s[4].period_s = 77;
    r.agg_s = 9;

    uint8_t blob[102];
    size_t len = af_sns_reg_pack(&r, blob);
    CHECK(len == 102);

    af_sns_reg_t r2;
    memset(&r2, 0, sizeof(r2));
    CHECK(af_sns_reg_unpack(blob, 102, &r2) == 0);
    CHECK(memcmp(&r, &r2, sizeof(r)) == 0);

    uint8_t blob2[102];
    size_t len2 = af_sns_reg_pack(&r2, blob2);
    CHECK(len2 == 102);
    CHECK(memcmp(blob, blob2, 102) == 0);

    int errors = 0;
    for (int i = 0; i < 102; i++) {
        uint8_t test_blob[102];
        memcpy(test_blob, blob, 102);
        test_blob[i] ^= 0x01;
        af_sns_reg_t r3;
        memset(&r3, 0, sizeof(r3));
        if (af_sns_reg_unpack(test_blob, 102, &r3) == 0) {
            errors++;
        }
    }
    CHECK(errors == 0);

    CHECK(af_sns_reg_unpack(blob, 101, &r2) == -1);
    CHECK(af_sns_reg_unpack(blob, 103, &r2) == -1);
    CHECK(af_sns_reg_unpack(NULL, 102, &r2) == -1);

    // Version error
    uint8_t blob_v[102];
    memcpy(blob_v, blob, 102);
    blob_v[0] = 2;
    uint16_t crc = af_crc16(blob_v, 100);
    blob_v[100] = crc & 0xFF;
    blob_v[101] = crc >> 8;
    CHECK(af_sns_reg_unpack(blob_v, 102, &r2) == -3);

    // Count error
    uint8_t blob_c[102];
    memcpy(blob_c, blob, 102);
    blob_c[1] = 4;
    crc = af_crc16(blob_c, 100);
    blob_c[100] = crc & 0xFF;
    blob_c[101] = crc >> 8;
    CHECK(af_sns_reg_unpack(blob_c, 102, &r2) == -4);

    // Duplicate addr
    uint8_t blob_d[102];
    memcpy(blob_d, blob, 102);
    // Slot 1 is at offset 4 + 12 = 16. Addr is at offset 16 + 2 = 18.
    // Slot 0 addr is at offset 4 + 2 = 6.
    // Set slot 1 addr to slot 0 addr (0x76)
    blob_d[18] = 0x76;
    crc = af_crc16(blob_d, 100);
    blob_d[100] = crc & 0xFF;
    blob_d[101] = crc >> 8;
    CHECK(af_sns_reg_unpack(blob_d, 102, &r2) == -4);

    // agg_s = 0
    uint8_t blob_a[102];
    memcpy(blob_a, blob, 102);
    blob_a[2] = 0;
    crc = af_crc16(blob_a, 100);
    blob_a[100] = crc & 0xFF;
    blob_a[101] = crc >> 8;
    CHECK(af_sns_reg_unpack(blob_a, 102, &r2) == -4);

    af_sns_reg_t r_orig;
    memcpy(&r_orig, &r2, sizeof(r2));
    CHECK(af_sns_reg_unpack(blob_a, 102, &r2) == -4);
    CHECK(memcmp(&r_orig, &r2, sizeof(r2)) == 0);
}

static void t_legacy(void) {
    af_sns_reg_t r;
    af_env_cfg_t c;
    c.enabled = 0;
    c.t_off10 = -30;
    c.p_off10 = 70;
    c.agg_s = 10;
    c.period_s = 60;
    af_sns_reg_from_legacy(&r, &c);
    CHECK(r.s[0].sensor == 1);
    CHECK(r.s[0].model == AF_SNS_BMP280);
    CHECK(r.s[0].addr == 0x76);
    CHECK(r.s[0].enabled == 0);
    CHECK(r.s[0].t_off10 == -30);
    CHECK(r.s[0].p_off10 == 70);
    CHECK(r.s[0].h_off10 == 0);
    CHECK(r.s[0].period_s == 60);
    CHECK(r.agg_s == 10);
    CHECK(af_sns_reg_valid(&r) == 1);
    CHECK(af_sns_reg_count(&r) == 1);

    af_env_cfg_t c2;
    c2.enabled = 5;
    c2.t_off10 = 999;
    c2.p_off10 = -999;
    c2.agg_s = 0;
    c2.period_s = 1;
    af_sns_reg_from_legacy(&r, &c2);
    CHECK(r.s[0].enabled == 1);
    CHECK(r.s[0].t_off10 == 50);
    CHECK(r.s[0].p_off10 == -200);
    CHECK(r.agg_s == 1);
    CHECK(r.s[0].period_s == 5);
    CHECK(af_sns_reg_valid(&r) == 1);
}

typedef struct {
    uint8_t present[128];
    uint8_t id[128];
    uint8_t sht_status[128][3];
    uint8_t aht_status[128];
    int xfer_fail;
    int calls_probe;
    int calls_xfer;
} mock_t;

static int mock_probe(void *ctx, uint8_t addr) {
    mock_t *m = (mock_t *)ctx;
    m->calls_probe++;
    if (m->present[addr]) return 0;
    return 1;
}

static int mock_xfer(void *ctx, uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn) {
    mock_t *m = (mock_t *)ctx;
    m->calls_xfer++;
    if (m->xfer_fail) return 1;
    
    if (wn == 1 && w[0] == 0xD0) {
        if (rn >= 1) r[0] = m->id[addr];
        return 0;
    }
    if (wn == 2 && w[0] == 0xF3 && w[1] == 0x2D) {
        if (rn >= 3) {
            r[0] = m->sht_status[addr][0];
            r[1] = m->sht_status[addr][1];
            r[2] = m->sht_status[addr][2];
        }
        return 0;
    }
    if (wn == 1 && w[0] == 0x71) {
        if (rn >= 1) r[0] = m->aht_status[addr];
        return 0;
    }
    return 2;
}

static void t_identify_scan(void) {
    mock_t m;
    memset(&m, 0, sizeof(m));
    m.present[0x76] = 1;
    m.id[0x76] = 0x58;
    
    af_i2c_ops_t ops;
    ops.ctx = &m;
    ops.probe = mock_probe;
    ops.xfer = mock_xfer;

    af_sns_found_t found[8];
    int n = af_sns_scan(&ops, found, 8);
    CHECK(n == 1);
    if (n == 1) {
        CHECK(found[0].addr == 0x76);
        CHECK(found[0].model == AF_SNS_BMP280);
    }

    m.present[0x44] = 1;
    m.sht_status[0x44][0] = 0x80;
    m.sht_status[0x44][1] = 0x00;
    m.sht_status[0x44][2] = crc8_calc(m.sht_status[0x44], 2);
    
    n = af_sns_scan(&ops, found, 8);
    CHECK(n == 2);
    if (n == 2) {
        CHECK(found[0].addr == 0x44);
        CHECK(found[0].model == AF_SNS_SHT3X);
        CHECK(found[1].addr == 0x76);
        CHECK(found[1].model == AF_SNS_BMP280);
    }

    m.sht_status[0x44][2] ^= 1;
    n = af_sns_scan(&ops, found, 8);
    CHECK(n == 1);
    if (n == 1) {
        CHECK(found[0].addr == 0x76);
    }
    m.sht_status[0x44][2] ^= 1;

    m.present[0x77] = 1;
    m.id[0x77] = 0x60;
    n = af_sns_scan(&ops, found, 8);
    CHECK(n == 3);
    if (n == 3) {
        CHECK(found[2].addr == 0x77);
        CHECK(found[2].model == AF_SNS_BME280);
    }

    m.id[0x77] = 0x61;
    n = af_sns_scan(&ops, found, 8);
    CHECK(n == 2);
    
    m.id[0x77] = 0x56;
    n = af_sns_scan(&ops, found, 8);
    CHECK(n == 3);
    if (n == 3) {
        CHECK(found[2].model == AF_SNS_BMP280);
    }
    m.id[0x77] = 0x57;
    n = af_sns_scan(&ops, found, 8);
    CHECK(n == 3);
    if (n == 3) {
        CHECK(found[2].model == AF_SNS_BMP280);
    }

    m.present[0x38] = 1;
    m.aht_status[0x38] = 0x18;
    n = af_sns_scan(&ops, found, 8);
    CHECK(n == 4);
    if (n == 4) {
        CHECK(found[0].addr == 0x38);
        CHECK(found[0].model == AF_SNS_AHT2X);
    }

    m.aht_status[0x38] = 0xFF;
    n = af_sns_scan(&ops, found, 8);
    CHECK(n == 3);
    m.aht_status[0x38] = 0x18;

    m.present[0x45] = 1;
    m.sht_status[0x45][0] = 0x80;
    m.sht_status[0x45][1] = 0x00;
    m.sht_status[0x45][2] = crc8_calc(m.sht_status[0x45], 2);
    n = af_sns_scan(&ops, found, 8);
    CHECK(n == 5);
    if (n == 5) {
        CHECK(found[1].addr == 0x44);
        CHECK(found[2].addr == 0x45);
        CHECK(found[2].model == AF_SNS_SHT3X);
    }

    n = af_sns_scan(&ops, found, 1);
    CHECK(n == 1);
    if (n == 1) {
        CHECK(found[0].addr == 0x38);
    }

    CHECK(af_sns_scan(&ops, found, 0) == 0);
    CHECK(af_sns_scan(&ops, NULL, 8) == 0);

    m.xfer_fail = 1;
    int calls_before = m.calls_probe;
    n = af_sns_scan(&ops, found, 8);
    CHECK(n == 0);
    CHECK(m.calls_probe == calls_before + 5);
    m.xfer_fail = 0;

    m.present[0x50] = 1;
    n = af_sns_scan(&ops, found, 8);
    CHECK(n == 5);
    m.present[0x50] = 0;
}

static void t_merge_scan(void) {
    af_sns_reg_t r;
    af_sns_reg_default(&r);
    r.s[0].t_off10 = 7;

    af_sns_found_t f[3];
    f[0].addr = 0x76; f[0].model = AF_SNS_BMP280;
    f[1].addr = 0x44; f[1].model = AF_SNS_SHT3X;
    f[2].addr = 0x38; f[2].model = AF_SNS_AHT2X;

    CHECK(af_sns_reg_merge_scan(&r, f, 3) == 2);
    af_sns_cfg_t *c1 = af_sns_reg_find(&r, 1);
    if (c1) {
        CHECK(c1->enabled == 1);
        CHECK(c1->t_off10 == 7);
    }
    af_sns_cfg_t *c2 = af_sns_reg_find(&r, 2);
    if (c2) {
        CHECK(c2->addr == 0x44);
        CHECK(c2->model == AF_SNS_SHT3X);
        CHECK(c2->enabled == 0);
    }
    af_sns_cfg_t *c3 = af_sns_reg_find(&r, 3);
    if (c3) {
        CHECK(c3->addr == 0x38);
        CHECK(c3->model == AF_SNS_AHT2X);
        CHECK(c3->enabled == 0);
    }
    CHECK(af_sns_reg_count(&r) == 3);
    CHECK(af_sns_reg_merge_scan(&r, f, 3) == 0);

    af_sns_found_t f2[1];
    f2[0].addr = 0x76; f2[0].model = AF_SNS_BME280;
    CHECK(af_sns_reg_merge_scan(&r, f2, 1) == 1);
    c1 = af_sns_reg_find(&r, 1);
    if (c1) {
        CHECK(c1->model == AF_SNS_BME280);
        CHECK(c1->enabled == 0);
        CHECK(c1->t_off10 == 0);
        CHECK(c1->p_off10 == 0);
        CHECK(c1->h_off10 == 0);
        CHECK(c1->period_s == AF_ENV_PERIOD_DEF);
    }
    CHECK(af_sns_reg_valid(&r) == 1);

    af_sns_found_t f3[3];
    f3[0].addr = 0x44; f3[0].model = AF_SNS_BMP280;
    f3[1].addr = 0x10; f3[1].model = AF_SNS_SHT3X;
    f3[2].addr = 0x76; f3[2].model = 77;
    af_sns_reg_t r_copy;
    memcpy(&r_copy, &r, sizeof(r));
    CHECK(af_sns_reg_merge_scan(&r, f3, 3) == 0);
    CHECK(memcmp(&r, &r_copy, sizeof(r)) == 0);

    CHECK(af_sns_reg_merge_scan(&r, NULL, 0) == 0);

    // Full registry test
    af_sns_reg_t r_full;
    memset(&r_full, 0, sizeof(r_full));
    r_full.s[0].sensor = 1; r_full.s[0].model = AF_SNS_AHT2X; r_full.s[0].addr = 0x38; r_full.s[0].enabled = 0; r_full.s[0].period_s = 30;
    r_full.s[1].sensor = 2; r_full.s[1].model = AF_SNS_SHT3X; r_full.s[1].addr = 0x44; r_full.s[1].enabled = 0; r_full.s[1].period_s = 30;
    r_full.s[2].sensor = 3; r_full.s[2].model = AF_SNS_SHT3X; r_full.s[2].addr = 0x45; r_full.s[2].enabled = 0; r_full.s[2].period_s = 30;
    r_full.s[3].sensor = 4; r_full.s[3].model = AF_SNS_BMP280; r_full.s[3].addr = 0x76; r_full.s[3].enabled = 0; r_full.s[3].period_s = 30;
    r_full.s[4].sensor = 5; r_full.s[4].model = AF_SNS_BMP280; r_full.s[4].addr = 0x77; r_full.s[4].enabled = 0; r_full.s[4].period_s = 30;
    r_full.s[5].sensor = 6; r_full.s[5].model = AF_SNS_BMP280; r_full.s[5].addr = 0x10; r_full.s[5].enabled = 0; r_full.s[5].period_s = 30;
    r_full.s[6].sensor = 7; r_full.s[6].model = AF_SNS_BMP280; r_full.s[6].addr = 0x11; r_full.s[6].enabled = 0; r_full.s[6].period_s = 30;
    r_full.s[7].sensor = 8; r_full.s[7].model = AF_SNS_BMP280; r_full.s[7].addr = 0x12; r_full.s[7].enabled = 0; r_full.s[7].period_s = 30;
    r_full.agg_s = 5;

    af_sns_found_t f4[1];
    f4[0].addr = 0x76; f4[0].model = AF_SNS_BMP280;
    CHECK(af_sns_reg_merge_scan(&r_full, f4, 1) == 0);
}

static void t_adopt_addr(void) {
    af_sns_reg_t r;
    af_sns_reg_default(&r);
    af_sns_found_t f[1];
    f[0].addr = 0x77; f[0].model = AF_SNS_BMP280;
    CHECK(af_sns_reg_adopt_addr(&r, f, 1) == 1);
    af_sns_cfg_t *c1 = af_sns_reg_find(&r, 1);
    if (c1) CHECK(c1->addr == 0x77);
    CHECK(af_sns_reg_valid(&r) == 1);
    CHECK(af_sns_reg_adopt_addr(&r, f, 1) == 0);

    af_sns_reg_default(&r);
    f[0].addr = 0x76; f[0].model = AF_SNS_BMP280;
    CHECK(af_sns_reg_adopt_addr(&r, f, 1) == 0);

    f[0].addr = 0x77; f[0].model = AF_SNS_SHT3X;
    CHECK(af_sns_reg_adopt_addr(&r, f, 1) == 0);

    af_sns_reg_default(&r);
    CHECK(af_sns_reg_add(&r, AF_SNS_BME280, 0x77, 0) == 2);
    f[0].addr = 0x77; f[0].model = AF_SNS_BMP280;
    CHECK(af_sns_reg_adopt_addr(&r, f, 1) == 0);

    CHECK(af_sns_reg_adopt_addr(&r, NULL, 0) == 0);
}

static void t_extra_rules(void) {
    af_sns_reg_t r;
    af_sns_reg_default(&r);
    int n = af_sns_reg_add(&r, AF_SNS_SHT3X, 0x44, 0);
    CHECK(n == 2);
    CHECK(r.s[1].period_s == AF_ENV_PERIOD_DEF);
    CHECK(af_sns_reg_valid(&r) == 1);
    r.s[1].p_off10 = 1;
    CHECK(af_sns_reg_valid(&r) == 0);
    r.s[1].p_off10 = 0;
    af_sns_found_t f[2] = {{0x76, AF_SNS_BMP280}, {0x77, AF_SNS_BMP280}};
    CHECK(af_sns_reg_adopt_addr(&r, f, 2) == 0);
    CHECK(r.s[0].addr == 0x76);
}

static void t_reg_empty(void)
{
    af_sns_reg_t r;

    memset(&r, 0xAB, sizeof r);
    af_sns_reg_empty(&r);
    CHECK(af_sns_reg_count(&r) == 0);
    CHECK(r.agg_s == AF_ENV_AGG_DEF);
    CHECK(af_sns_reg_valid(&r) == 1);
    for (unsigned int i = 0; i < AF_SNS_MAX; i++)
        CHECK(r.s[i].sensor == 0);
    af_sns_reg_empty(NULL);
    af_sns_reg_default(&r);
    CHECK(af_sns_reg_count(&r) == 1);
}
int main(void) {
    RUN(t_default_valid);
    RUN(t_add_rules);
    RUN(t_order_grouping);
    RUN(t_enable_model);
    RUN(t_patch_rules);
    RUN(t_agg);
    RUN(t_blob);
    RUN(t_legacy);
    RUN(t_identify_scan);
    RUN(t_merge_scan);
    RUN(t_adopt_addr);
    RUN(t_extra_rules);
    RUN(t_reg_empty);
    TESTS_DONE();
    return 0;
}
