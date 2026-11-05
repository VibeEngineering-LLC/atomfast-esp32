#include "af_sensor.h"
#include <stdio.h>
#include <math.h>

static int g_fail = 0;
static int g_tests = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #cond); \
        g_fail++; \
    } \
} while(0)

static void pack_le16(uint8_t *dst, int16_t val) {
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
}

static void t_preset_table(void) {
    g_tests++; int f0 = g_fail;
    CHECK(AF_SNS_PRESET_COUNT == 7);
    
    const af_sns_preset_t *bmp280 = af_sns_preset(AF_SNS_BMP280);
    CHECK(bmp280 != NULL);
    if (bmp280) {
        CHECK(bmp280->caps == (AF_CAP_TEMP | AF_CAP_PRESS));
        CHECK(bmp280->supported == 1);
        CHECK(bmp280->addr[0] == 0x76);
        CHECK(bmp280->addr[1] == 0x77);
    }

    const af_sns_preset_t *bme280 = af_sns_preset(AF_SNS_BME280);
    CHECK(bme280 != NULL);
    if (bme280) {
        CHECK((bme280->caps & AF_CAP_HUM) != 0);
    }

    CHECK(af_sns_preset(AF_SNS_NONE) == NULL);
    CHECK(af_sns_preset((af_sns_model_t)99) == NULL);
    
    if (g_fail == f0) printf("PASS t_preset_table\n");
    else printf("FAIL t_preset_table\n");
}

static void t_chip_id(void) {
    g_tests++; int f0 = g_fail;
    CHECK(af_sns_from_chip_id(0x58) == AF_SNS_BMP280);
    CHECK(af_sns_from_chip_id(0x57) == AF_SNS_BMP280);
    CHECK(af_sns_from_chip_id(0x56) == AF_SNS_BMP280);
    CHECK(af_sns_from_chip_id(0x60) == AF_SNS_BME280);
    CHECK(af_sns_from_chip_id(0x55) == AF_SNS_BMP180);
    CHECK(af_sns_from_chip_id(0x00) == AF_SNS_NONE);
    CHECK(af_sns_from_chip_id(0xFF) == AF_SNS_NONE);
    CHECK(af_sns_from_chip_id(0x61) == AF_SNS_NONE);
    
    if (g_fail == f0) printf("PASS t_chip_id\n");
    else printf("FAIL t_chip_id\n");
}

static void t_effective_caps(void) {
    g_tests++; int f0 = g_fail;
    CHECK(af_sns_effective_caps(AF_SNS_BMP280, 0xFF) == (AF_CAP_TEMP | AF_CAP_PRESS));
    CHECK(af_sns_effective_caps(AF_SNS_BMP280, AF_CAP_TEMP) == AF_CAP_TEMP);
    CHECK(af_sns_effective_caps(AF_SNS_BMP280, AF_CAP_HUM) == 0);
    CHECK(af_sns_effective_caps(AF_SNS_NONE, 0xFF) == 0);
    
    if (g_fail == f0) printf("PASS t_effective_caps\n");
    else printf("FAIL t_effective_caps\n");
}

static void t_parse_cal_ok(void) {
    g_tests++; int f0 = g_fail;
    uint8_t buf[24];
    pack_le16(buf + 0, 27504);   // t1
    pack_le16(buf + 2, 26435);   // t2
    pack_le16(buf + 4, -1000);   // t3
    pack_le16(buf + 6, 36477);   // p1
    pack_le16(buf + 8, -10685);  // p2
    pack_le16(buf + 10, 3024);   // p3
    pack_le16(buf + 12, 2855);   // p4
    pack_le16(buf + 14, 140);    // p5
    pack_le16(buf + 16, -7);     // p6
    pack_le16(buf + 18, 15500);  // p7
    pack_le16(buf + 20, -14600); // p8
    pack_le16(buf + 22, 6000);   // p9

    af_bmp280_cal_t cal;
    int ret = af_bmp280_parse_cal(buf, 24, &cal);
    CHECK(ret == 0);
    if (ret == 0) {
        CHECK(cal.t1 == 27504);
        CHECK(cal.t2 == 26435);
        CHECK(cal.t3 == -1000);
        CHECK(cal.p1 == 36477);
        CHECK(cal.p2 == -10685);
        CHECK(cal.p3 == 3024);
        CHECK(cal.p4 == 2855);
        CHECK(cal.p5 == 140);
        CHECK(cal.p6 == -7);
        CHECK(cal.p7 == 15500);
        CHECK(cal.p8 == -14600);
        CHECK(cal.p9 == 6000);
    }
    
    if (g_fail == f0) printf("PASS t_parse_cal_ok\n");
    else printf("FAIL t_parse_cal_ok\n");
}

static void t_parse_cal_errors(void) {
    g_tests++; int f0 = g_fail;
    af_bmp280_cal_t cal;
    uint8_t buf[24] = {0};
    
    // NULL buffer
    CHECK(af_bmp280_parse_cal(NULL, 24, &cal) == -1);
    
    // Invalid size
    CHECK(af_bmp280_parse_cal(buf, 23, &cal) == -1);
    
    // NULL output
    CHECK(af_bmp280_parse_cal(buf, 24, NULL) == -1);
    
    // t1 = 0
    pack_le16(buf + 0, 0);
    CHECK(af_bmp280_parse_cal(buf, 24, &cal) == -2);
    
    // p1 = 0
    pack_le16(buf + 0, 27504); // restore t1
    pack_le16(buf + 6, 0);     // set p1 to 0
    CHECK(af_bmp280_parse_cal(buf, 24, &cal) == -2);
    
    if (g_fail == f0) printf("PASS t_parse_cal_errors\n");
    else printf("FAIL t_parse_cal_errors\n");
}

static void t_adc20(void) {
    g_tests++; int f0 = g_fail;
    uint8_t raw1[3] = {0x7E, 0xED, 0x00};
    CHECK(af_bmp280_adc20(raw1) == 519888);
    
    uint8_t raw2[3] = {0x80, 0x00, 0x00};
    CHECK(af_bmp280_adc20(raw2) == 0x80000);
    
    if (g_fail == f0) printf("PASS t_adc20\n");
    else printf("FAIL t_adc20\n");
}

static void t_temp_datasheet(void) {
    g_tests++; int f0 = g_fail;
    uint8_t buf[24];
    pack_le16(buf + 0, 27504);
    pack_le16(buf + 2, 26435);
    pack_le16(buf + 4, -1000);
    pack_le16(buf + 6, 36477);
    pack_le16(buf + 8, -10685);
    pack_le16(buf + 10, 3024);
    pack_le16(buf + 12, 2855);
    pack_le16(buf + 14, 140);
    pack_le16(buf + 16, -7);
    pack_le16(buf + 18, 15500);
    pack_le16(buf + 20, -14600);
    pack_le16(buf + 22, 6000);

    af_bmp280_cal_t cal;
    if (af_bmp280_parse_cal(buf, 24, &cal) != 0) {
        printf("  FAIL t_temp_datasheet: parse failed\n");
        g_fail++;
        printf("FAIL t_temp_datasheet\n");
        return;
    }

    int32_t t_fine = 0;
    float temp = af_bmp280_temp_c(&cal, 519888, &t_fine);
    CHECK(t_fine == 128422);
    CHECK(fabsf(temp - 25.08f) <= 0.01f);
    
    if (g_fail == f0) printf("PASS t_temp_datasheet\n");
    else printf("FAIL t_temp_datasheet\n");
}

static void t_press_datasheet(void) {
    g_tests++; int f0 = g_fail;
    uint8_t buf[24];
    pack_le16(buf + 0, 27504);
    pack_le16(buf + 2, 26435);
    pack_le16(buf + 4, -1000);
    pack_le16(buf + 6, 36477);
    pack_le16(buf + 8, -10685);
    pack_le16(buf + 10, 3024);
    pack_le16(buf + 12, 2855);
    pack_le16(buf + 14, 140);
    pack_le16(buf + 16, -7);
    pack_le16(buf + 18, 15500);
    pack_le16(buf + 20, -14600);
    pack_le16(buf + 22, 6000);

    af_bmp280_cal_t cal;
    if (af_bmp280_parse_cal(buf, 24, &cal) != 0) {
        printf("  FAIL t_press_datasheet: parse failed\n");
        g_fail++;
        printf("FAIL t_press_datasheet\n");
        return;
    }

    float press = af_bmp280_press_pa(&cal, 415148, 128422);
    CHECK(fabsf(press - 100653.27f) <= 1.0f);
    
    if (g_fail == f0) printf("PASS t_press_datasheet\n");
    else printf("FAIL t_press_datasheet\n");
}

static void t_disabled_channel(void) {
    g_tests++; int f0 = g_fail;
    uint8_t buf[24];
    pack_le16(buf + 0, 27504);
    pack_le16(buf + 2, 26435);
    pack_le16(buf + 4, -1000);
    pack_le16(buf + 6, 36477);
    pack_le16(buf + 8, -10685);
    pack_le16(buf + 10, 3024);
    pack_le16(buf + 12, 2855);
    pack_le16(buf + 14, 140);
    pack_le16(buf + 16, -7);
    pack_le16(buf + 18, 15500);
    pack_le16(buf + 20, -14600);
    pack_le16(buf + 22, 6000);

    af_bmp280_cal_t cal;
    if (af_bmp280_parse_cal(buf, 24, &cal) != 0) {
        printf("  FAIL t_disabled_channel: parse failed\n");
        g_fail++;
        printf("FAIL t_disabled_channel\n");
        return;
    }

    int32_t t_fine = 12345;
    float temp = af_bmp280_temp_c(&cal, 0x80000, &t_fine);
    CHECK(isnan(temp));
    CHECK(t_fine == 12345);

    float press = af_bmp280_press_pa(&cal, 0x80000, 128422);
    CHECK(isnan(press));
    
    if (g_fail == f0) printf("PASS t_disabled_channel\n");
    else printf("FAIL t_disabled_channel\n");
}

static void t_press_div0(void) {
    g_tests++; int f0 = g_fail;
    af_bmp280_cal_t cal;
    // Manually set p1 to 0 to trigger division by zero scenario
    cal.t1 = 27504;
    cal.t2 = 26435;
    cal.t3 = -1000;
    cal.p1 = 0;
    cal.p2 = 0;
    cal.p3 = 0;
    cal.p4 = 0;
    cal.p5 = 0;
    cal.p6 = 0;
    cal.p7 = 0;
    cal.p8 = 0;
    cal.p9 = 0;

    float press = af_bmp280_press_pa(&cal, 415148, 128422);
    CHECK(isnan(press));
    
    if (g_fail == f0) printf("PASS t_press_div0\n");
    else printf("FAIL t_press_div0\n");
}

static void t_mmhg(void) {
    g_tests++; int f0 = g_fail;
    CHECK(fabsf(af_pa_to_mmhg(101325.0f) - 760.0f) <= 0.05f);
    CHECK(fabsf(af_pa_to_mmhg(100653.27f) - 754.96f) <= 0.05f);
    CHECK(isnan(af_pa_to_mmhg(NAN)));
    
    if (g_fail == f0) printf("PASS t_mmhg\n");
    else printf("FAIL t_mmhg\n");
}

static void t_offset_clamp(void) {
    g_tests++; int f0 = g_fail;
    CHECK(af_apply_offset(26.5f, 1.0f, 5.0f) == 27.5f);
    CHECK(af_apply_offset(26.5f, 10.0f, 5.0f) == 31.5f); // 26.5 + 5.0
    CHECK(af_apply_offset(26.5f, -30.0f, 20.0f) == 6.5f); // 26.5 - 20.0
    CHECK(isnan(af_apply_offset(NAN, 1.0f, 5.0f)));
    
    if (g_fail == f0) printf("PASS t_offset_clamp\n");
    else printf("FAIL t_offset_clamp\n");
}

static void t_plausible(void) {
    g_tests++; int f0 = g_fail;
    CHECK(af_sns_plausible(25.0f, 751.7f) == 1);
    CHECK(af_sns_plausible(NAN, 751.7f) == 1);
    CHECK(af_sns_plausible(25.0f, NAN) == 1);
    CHECK(af_sns_plausible(NAN, NAN) == 0);
    CHECK(af_sns_plausible(-41.0f, 750.0f) == 0);
    CHECK(af_sns_plausible(25.0f, 830.0f) == 0);
    CHECK(af_sns_plausible(85.0f, 225.0f) == 1);
    
    if (g_fail == f0) printf("PASS t_plausible\n");
    else printf("FAIL t_plausible\n");
}

static void t_real_board(void) {
    g_tests++; int f0 = g_fail;
    CHECK(af_sns_plausible(26.5f, 751.7f) == 1);
    CHECK(af_apply_offset(751.7f, 0.0f, AF_PRESS_OFF_LIM_MMHG) == 751.7f);
    
    float pa = 751.7f * 133.322387f;
    float mmhg_back = af_pa_to_mmhg(pa);
    CHECK(fabsf(mmhg_back - 751.7f) <= 0.01f);
    
    if (g_fail == f0) printf("PASS t_real_board\n");
    else printf("FAIL t_real_board\n");
}

int main(void) {
    t_preset_table();
    t_chip_id();
    t_effective_caps();
    t_parse_cal_ok();
    t_parse_cal_errors();
    t_adc20();
    t_temp_datasheet();
    t_press_datasheet();
    t_disabled_channel();
    t_press_div0();
    t_mmhg();
    t_offset_clamp();
    t_plausible();
    t_real_board();

    printf("TESTS %d FAILURES %d\n", g_tests, g_fail);
    return g_fail != 0;
}
