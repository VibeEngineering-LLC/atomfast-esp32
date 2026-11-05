#ifndef AF_SNS_DRV_H
#define AF_SNS_DRV_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include "af_sensor.h"

/* CRC-8, полином 0x31, init 0xFF, без отражения, финальный XOR 0.
   Источник: Sensirion SHT3x datasheet, табл. 20. */
static inline uint8_t af_crc8(const uint8_t *d, size_t n) {
    if (d == NULL) return 0xFF;
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x31);
            else crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* Разбор данных SHT3x.
   Источник: Sensirion SHT3x datasheet, раздел "Data Conversion". */
static inline int af_sht3x_parse(const uint8_t raw[6], float *t_c, float *rh) {
    if (raw == NULL || t_c == NULL || rh == NULL) return -1;
    if (af_crc8(raw, 2) != raw[2]) return -2;
    if (af_crc8(&raw[3], 2) != raw[5]) return -3;

    uint16_t s_t = (uint16_t)((raw[0] << 8) | raw[1]);
    uint16_t s_rh = (uint16_t)((raw[3] << 8) | raw[4]);

    *t_c = -45.0f + 175.0f * (float)s_t / 65535.0f;
    *rh = 100.0f * (float)s_rh / 65535.0f;
    return 0;
}

/* Проверка статуса SHT3x (CRC).
   Источник: Sensirion SHT3x datasheet. */
static inline int af_sht3x_status_ok(const uint8_t raw[3]) {
    if (raw == NULL) return 0;
    return (af_crc8(raw, 2) == raw[2]) ? 1 : 0;
}

/* Разбор данных AHT2x.
   Источник: AHT20/AHT21 datasheet, раздел "Data Format". */
static inline int af_aht2x_parse(const uint8_t raw[7], float *t_c, float *rh) {
    if (raw == NULL || t_c == NULL || rh == NULL) return -1;
    if (raw[0] & 0x80) return -2;
    if ((raw[0] & 0x08) == 0) return -3;
    if (af_crc8(raw, 6) != raw[6]) return -4;

    uint32_t s_rh = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) | (raw[3] >> 4);
    uint32_t s_t = (((uint32_t)raw[3] & 0x0F) << 16) | ((uint32_t)raw[4] << 8) | raw[5];

    *rh = (float)s_rh * 100.0f / 1048576.0f;
    *t_c = (float)s_t * 200.0f / 1048576.0f - 50.0f;
    return 0;
}

/* Калибровочные данные BME280. */
typedef struct {
    af_bmp280_cal_t tp;
    uint8_t h1;
    int16_t h2;
    uint8_t h3;
    int16_t h4;
    int16_t h5;
    int8_t h6;
} af_bme280_cal_t;

/* Разбор калибровочных данных BME280.
   Источник: Bosch BME280 datasheet BST-BME280-DS002, раздел "Calibration data". */
static inline int af_bme280_parse_cal(const uint8_t a[26], const uint8_t b[7], af_bme280_cal_t *c) {
    if (a == NULL || b == NULL || c == NULL) return -1;

    int ret = af_bmp280_parse_cal(a, 24, &c->tp);
    if (ret != 0) return ret;

    c->h1 = a[25];
    c->h2 = (int16_t)(b[0] | (b[1] << 8));
    c->h3 = b[2];
    c->h4 = (int16_t)(((int16_t)(int8_t)b[3]) * 16 + (b[4] & 0x0F));
    c->h5 = (int16_t)(((int16_t)(int8_t)b[5]) * 16 + (b[4] >> 4));
    c->h6 = (int8_t)b[6];
    return 0;
}

/* Расчет влажности BME280.
   Источник: Bosch BME280 datasheet BST-BME280-DS002, раздел 4.2.3. */
static inline float af_bme280_hum(const af_bme280_cal_t *c, int32_t adc_h, int32_t t_fine) {
    if (adc_h == 0x8000) return NAN;

    int32_t v = t_fine - 76800;
    v = (((((adc_h << 14) - ((int32_t)c->h4 * 1048576) - ((int32_t)c->h5 * v)) + 16384) >> 15) *
         (((((((v * (int32_t)c->h6) >> 10) * (((v * (int32_t)c->h3) >> 11) + 32768)) >> 10) + 2097152) * (int32_t)c->h2 + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)c->h1) >> 4);
    if (v < 0) v = 0;
    if (v > 419430400) v = 419430400;

    return (float)((uint32_t)(v >> 12) / 1024.0f);
}

/* Конвертация сырых данных BME280 в физические величины.
   Источник: Bosch BME280 datasheet BST-BME280-DS002. */
static inline int af_bme280_convert(const af_bme280_cal_t *c, const uint8_t raw[8], float *t_c, float *p_pa, float *rh) {
    if (c == NULL || raw == NULL || t_c == NULL || p_pa == NULL || rh == NULL) return -1;

    int32_t adc_p = af_bmp280_adc20(raw);
    int32_t adc_t = af_bmp280_adc20(&raw[3]);
    int32_t adc_h = (int32_t)((raw[6] << 8) | raw[7]);

    int32_t t_fine = 0;
    float temp = af_bmp280_temp_c(&c->tp, adc_t, &t_fine);
    if (isnan(temp)) return -2;

    float press = af_bmp280_press_pa(&c->tp, adc_p, t_fine);
    float hum = af_bme280_hum(c, adc_h, t_fine);

    *t_c = temp;
    *p_pa = press;
    *rh = hum;
    return 0;
}

#endif
