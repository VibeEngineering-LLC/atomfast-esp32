#ifndef AF_SENSOR_H
#define AF_SENSOR_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

typedef enum { AF_SNS_NONE=0, AF_SNS_BMP280, AF_SNS_BME280, AF_SNS_BMP180, AF_SNS_AHT2X, AF_SNS_SHT3X, AF_SNS_SHT4X, AF_SNS_HTU21D } af_sns_model_t;

#define AF_CAP_TEMP  0x01
#define AF_CAP_PRESS 0x02
#define AF_CAP_HUM   0x04

typedef struct { af_sns_model_t model; const char *name; uint8_t caps; uint8_t addr[2]; uint8_t supported; } af_sns_preset_t;

static const af_sns_preset_t AF_SNS_PRESETS[] = {
    { AF_SNS_BMP280, "BMP280", AF_CAP_TEMP | AF_CAP_PRESS, { 0x76, 0x77 }, 1 },
    { AF_SNS_BME280, "BME280", AF_CAP_TEMP | AF_CAP_PRESS | AF_CAP_HUM, { 0x76, 0x77 }, 1 },
    { AF_SNS_BMP180, "BMP180", AF_CAP_TEMP | AF_CAP_PRESS, { 0x77, 0x00 }, 0 },
    { AF_SNS_AHT2X, "AHT20/AHT21", AF_CAP_TEMP | AF_CAP_HUM, { 0x38, 0x00 }, 1 },
    { AF_SNS_SHT3X, "SHT3x", AF_CAP_TEMP | AF_CAP_HUM, { 0x44, 0x45 }, 1 },
    { AF_SNS_SHT4X, "SHT4x", AF_CAP_TEMP | AF_CAP_HUM, { 0x44, 0x00 }, 0 },
    { AF_SNS_HTU21D, "HTU21D/SHT21", AF_CAP_TEMP | AF_CAP_HUM, { 0x40, 0x00 }, 0 }
};

#define AF_SNS_PRESET_COUNT 7

static inline const af_sns_preset_t *af_sns_preset(af_sns_model_t m) {
    if (m == AF_SNS_NONE || (int)m > AF_SNS_PRESET_COUNT) return NULL;
    return &AF_SNS_PRESETS[(int)m - 1];  /* table starts at BMP280 = 1 */
}

static inline af_sns_model_t af_sns_from_chip_id(uint8_t id) {
    switch (id) {
        case 0x56:
        case 0x57:
        case 0x58: return AF_SNS_BMP280;
        case 0x60: return AF_SNS_BME280;
        case 0x55: return AF_SNS_BMP180;
        default:   return AF_SNS_NONE;
    }
}

static inline uint8_t af_sns_effective_caps(af_sns_model_t m, uint8_t user_mask) {
    const af_sns_preset_t *p = af_sns_preset(m);
    if (!p) return 0;
    return p->caps & user_mask;
}

typedef struct { uint16_t t1; int16_t t2, t3; uint16_t p1; int16_t p2, p3, p4, p5, p6, p7, p8, p9; } af_bmp280_cal_t;

static inline int af_bmp280_parse_cal(const uint8_t *b, size_t n, af_bmp280_cal_t *c) {
    if (!b || !c || n < 24) return -1;
    c->t1 = (uint16_t)(b[0] | (b[1] << 8));
    c->t2 = (int16_t)(int16_t)(b[2] | (b[3] << 8));
    c->t3 = (int16_t)(int16_t)(b[4] | (b[5] << 8));
    c->p1 = (uint16_t)(b[6] | (b[7] << 8));
    c->p2 = (int16_t)(int16_t)(b[8] | (b[9] << 8));
    c->p3 = (int16_t)(int16_t)(b[10] | (b[11] << 8));
    c->p4 = (int16_t)(int16_t)(b[12] | (b[13] << 8));
    c->p5 = (int16_t)(int16_t)(b[14] | (b[15] << 8));
    c->p6 = (int16_t)(int16_t)(b[16] | (b[17] << 8));
    c->p7 = (int16_t)(int16_t)(b[18] | (b[19] << 8));
    c->p8 = (int16_t)(int16_t)(b[20] | (b[21] << 8));
    c->p9 = (int16_t)(int16_t)(b[22] | (b[23] << 8));
    if (c->t1 == 0 || c->p1 == 0) return -2;
    return 0;
}

static inline int32_t af_bmp280_adc20(const uint8_t raw[3]) {
    return ((int32_t)raw[0] << 16 | (int32_t)raw[1] << 8 | (int32_t)raw[2]) >> 4;
}

static inline float af_bmp280_temp_c(const af_bmp280_cal_t *c, int32_t adc_t, int32_t *t_fine) {
    if (adc_t == 0x80000) return NAN;
    int32_t var1 = (((adc_t >> 3) - ((int32_t)c->t1 << 1)) * c->t2) >> 11;
    int32_t var2 = (((((adc_t >> 4) - c->t1) * ((adc_t >> 4) - c->t1)) >> 12) * c->t3) >> 14;
    int32_t tf = var1 + var2;
    if (t_fine) *t_fine = tf;
    return (tf * 5 + 128) / 25600.0f;
}

static inline float af_bmp280_press_pa(const af_bmp280_cal_t *c, int32_t adc_p, int32_t t_fine) {
    if (adc_p == 0x80000) return NAN;
    int64_t var1 = (int64_t)t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)c->p6;
    var2 = var2 + ((var1 * (int64_t)c->p5) * 131072LL); /* e10: умножение вместо сдвига отрицательного (UB, UBSan) */
    var2 = var2 + ((int64_t)c->p4 * 34359738368LL);
    var1 = ((var1 * var1 * (int64_t)c->p3) >> 8) + ((var1 * (int64_t)c->p2) * 4096LL);
    var1 = (((int64_t)1 << 47) + var1) * (int64_t)c->p1 >> 33;
    if (var1 == 0) return NAN;
    int64_t p = 1048576 - adc_p;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)c->p9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)c->p8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)c->p7 * 16LL);
    return (float)(p / 256.0f);
}

#define AF_PA_PER_MMHG 133.322387f
#define AF_TEMP_OFF_LIM 5.0f
#define AF_PRESS_OFF_LIM_MMHG 20.0f

static inline float af_pa_to_mmhg(float pa) {
    return pa / AF_PA_PER_MMHG;
}

static inline float af_apply_offset(float v, float off, float lim) {
    if (isnan(v)) return NAN;
    if (off > lim) off = lim;
    if (off < -lim) off = -lim;
    return v + off;
}

static inline int af_sns_plausible(float t_c, float p_mmhg) {
    int t_ok = isnan(t_c) || (t_c >= -40.0f && t_c <= 85.0f);
    int p_ok = isnan(p_mmhg) || (p_mmhg >= 225.0f && p_mmhg <= 825.0f);
    int both_nan = isnan(t_c) && isnan(p_mmhg);
    return (t_ok && p_ok && !both_nan) ? 1 : 0;
}

#endif
