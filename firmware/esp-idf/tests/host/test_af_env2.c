#include "af_test.h"
#include "af_env.h"
#include "af_sns_reg.h"
#include "af_sns_json.h"
#include "af_persist.h"
#include "af_store_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void legacy_encode(uint32_t ts, int16_t t_c100, uint32_t p_pa, uint8_t flags, uint8_t out[14]) {
    out[0]=ts&0xFF; out[1]=(ts>>8)&0xFF; out[2]=(ts>>16)&0xFF; out[3]=(ts>>24)&0xFF;
    out[4]=(uint8_t)(t_c100&0xFF); out[5]=(uint8_t)((t_c100>>8)&0xFF);
    out[6]=p_pa&0xFF; out[7]=(p_pa>>8)&0xFF; out[8]=(p_pa>>16)&0xFF; out[9]=(p_pa>>24)&0xFF;
    out[10]=flags; out[11]=0;
    uint16_t crc=af_crc16(out,12);
    out[12]=crc&0xFF; out[13]=(crc>>8)&0xFF;
}

static void fixcrc(uint8_t *buf) {
    uint16_t crc = af_crc16(buf, 12);
    buf[12] = crc & 0xFF;
    buf[13] = (crc >> 8) & 0xFF;
}

static void t_rec_roundtrip(void) {
    struct {
        uint8_t sensor;
        uint8_t model;
        uint8_t flags;
        int16_t t;
        uint32_t p;
        uint16_t h;
    } cases[] = {
        {1, AF_SNS_BMP280, AF_ENV_F_T | AF_ENV_F_P, 2650, 100218, 0},
        {2, AF_SNS_BMP280, AF_ENV_F_T | AF_ENV_F_P | AF_ENV_F_CORR, -1234, 100218, 0},
        {3, AF_SNS_SHT3X, AF_ENV_F_T | AF_ENV_F_H, 2310, 0, 4550},
        {4, AF_SNS_BME280, AF_ENV_F_T | AF_ENV_F_P | AF_ENV_F_H, -550, 95000, 10000},
        {8, AF_SNS_AHT2X, AF_ENV_F_T | AF_ENV_F_H, 0, 0, 0}
    };
    size_t n = sizeof(cases) / sizeof(cases[0]);
    for (size_t i = 0; i < n; i++) {
        af_env_rec_t rec = {0};
        rec.ts = 1761300000;
        rec.sensor = cases[i].sensor;
        rec.model = cases[i].model;
        rec.flags = cases[i].flags;
        rec.t_c100 = cases[i].t;
        rec.p_pa = cases[i].p;
        rec.h_c100 = cases[i].h;

        uint8_t buf[14] = {0};
        af_env_encode(&rec, buf);

        uint8_t expected_byte11 = 0;
        if (cases[i].sensor != 1 || cases[i].model != AF_SNS_BMP280) {
            expected_byte11 = (uint8_t)((af_env_model_code((af_sns_model_t)cases[i].model) << 4) | cases[i].sensor);
        }
        CHECK(buf[11] == expected_byte11);

        if (cases[i].h != 0) {
            uint32_t w = 0;
            w |= (uint32_t)buf[6];
            w |= (uint32_t)buf[7] << 8;
            w |= (uint32_t)buf[8] << 16;
            w |= (uint32_t)buf[9] << 24;
            uint16_t h_part = (uint16_t)((w >> 17) & 0x3FFF);
            CHECK(h_part == cases[i].h);
        }

        uint16_t crc = af_crc16(buf, 12);
        CHECK(buf[12] == (crc & 0xFF));
        CHECK(buf[13] == ((crc >> 8) & 0xFF));

        af_env_rec_t dec = {0};
        int rc = af_env_decode(buf, &dec);
        CHECK(rc == 0);
        CHECK(dec.ts == rec.ts);
        CHECK(dec.t_c100 == rec.t_c100);
        CHECK(dec.p_pa == rec.p_pa);
        CHECK(dec.flags == rec.flags);
        CHECK(dec.h_c100 == rec.h_c100);
        CHECK(dec.sensor == rec.sensor);
        CHECK(dec.model == rec.model);
    }

    // Boundaries
    af_env_rec_t rec = {0};
    rec.ts = 1;
    rec.sensor = 1;
    rec.model = AF_SNS_BMP280;
    rec.flags = AF_ENV_F_P;
    rec.p_pa = 131071;
    uint8_t buf[14] = {0};
    af_env_encode(&rec, buf);
    af_env_rec_t dec = {0};
    CHECK(af_env_decode(buf, &dec) == 0);
    CHECK(dec.p_pa == 131071);

    rec.p_pa = 131072;
    af_env_encode(&rec, buf);
    CHECK(af_env_decode(buf, &dec) == 0);
    CHECK(dec.p_pa == 131071);

    rec.flags = AF_ENV_F_H;
    rec.p_pa = 0;
    rec.h_c100 = 10000;
    af_env_encode(&rec, buf);
    CHECK(af_env_decode(buf, &dec) == 0);
    CHECK(dec.h_c100 == 10000);

    rec.h_c100 = 10001;
    af_env_encode(&rec, buf);
    CHECK(af_env_decode(buf, &dec) == 0);
    CHECK(dec.h_c100 == 10000);

    rec.sensor = 0;
    rec.model = 0; // NONE
    rec.flags = AF_ENV_F_T;
    rec.t_c100 = 100;
    rec.h_c100 = 0;
    af_env_encode(&rec, buf);
    CHECK(buf[11] == 0);
    CHECK(af_env_decode(buf, &dec) == 0);
    CHECK(dec.sensor == 1);
    CHECK(dec.model == AF_SNS_BMP280);

    rec.sensor = 9;
    rec.model = AF_SNS_BMP280;
    af_env_encode(&rec, buf);
    CHECK(af_env_decode(buf, &dec) == 0);
    CHECK(dec.sensor == 1);
}

static void t_decode_errors_v2(void) {
    af_env_rec_t rec = {0};
    rec.ts = 1761300000;
    rec.sensor = 1;
    rec.model = AF_SNS_BMP280;
    rec.flags = AF_ENV_F_T | AF_ENV_F_P;
    rec.t_c100 = 2650;
    rec.p_pa = 100218;
    rec.h_c100 = 0;

    uint8_t buf[14] = {0};
    af_env_encode(&rec, buf);

    af_env_rec_t dec = {0};
    dec.ts = 77;

    // Byte 11 errors
    buf[11] = 0x01; fixcrc(buf);
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -2);
    CHECK(dec.ts == 77);

    buf[11] = 0x09; fixcrc(buf);
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -2);
    CHECK(dec.ts == 77);

    buf[11] = 0x40; fixcrc(buf);
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -2);
    CHECK(dec.ts == 77);

    buf[11] = 0x00; fixcrc(buf);
    CHECK(af_env_decode(buf, &dec) == 0);
    CHECK(dec.sensor == 1);

    buf[11] = 0x10; fixcrc(buf);
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -2);
    CHECK(dec.ts == 77);

    buf[11] = 0x42; fixcrc(buf);
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -2);
    CHECK(dec.ts == 77);

    // Flags errors
    buf[11] = 0x00;
    buf[10] = 0x10; fixcrc(buf);
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -3);
    CHECK(dec.ts == 77);

    buf[10] = 0x00; fixcrc(buf);
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -4);
    CHECK(dec.ts == 77);

    // Bit 31 of W
    buf[10] = AF_ENV_F_T | AF_ENV_F_P;
    buf[9] |= 0x80; fixcrc(buf);
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -5);
    CHECK(dec.ts == 77);

    // Flag H removed but h != 0
    // Set h in W
    uint32_t w = 0;
    w |= (uint32_t)buf[6];
    w |= (uint32_t)buf[7] << 8;
    w |= (uint32_t)buf[8] << 16;
    w |= (uint32_t)buf[9] << 24;
    w &= 0x7FFFFF; // clear bit 31
    w |= (uint32_t)100 << 17; // set h=100
    buf[6] = w & 0xFF;
    buf[7] = (w >> 8) & 0xFF;
    buf[8] = (w >> 16) & 0xFF;
    buf[9] = (w >> 24) & 0xFF;
    buf[10] = AF_ENV_F_T | AF_ENV_F_P; // no H flag
    fixcrc(buf);
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -5);
    CHECK(dec.ts == 77);

    // Flag P removed but p != 0
    // p is in lower 17 bits of W. Currently p is from previous encode (100218).
    // We need to ensure p != 0. It is.
    buf[10] = AF_ENV_F_T; // no P flag
    fixcrc(buf);
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -5);
    CHECK(dec.ts == 77);

    // Flag T removed but t != 0
    buf[10] = AF_ENV_F_P; // no T flag
    fixcrc(buf);
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -5);
    CHECK(dec.ts == 77);

    // h_c100 > 10000
    w &= 0x7FFFFF;
    w |= (uint32_t)10001 << 17;
    buf[6] = w & 0xFF;
    buf[7] = (w >> 8) & 0xFF;
    buf[8] = (w >> 16) & 0xFF;
    buf[9] = (w >> 24) & 0xFF;
    buf[10] = AF_ENV_F_T | AF_ENV_F_P | AF_ENV_F_H;
    fixcrc(buf);
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -5);
    CHECK(dec.ts == 77);

    // Bad CRC
    buf[12] ^= 0xFF;
    dec.ts = 77;
    CHECK(af_env_decode(buf, &dec) == -1);
    CHECK(dec.ts == 77);

    // NULLs
    CHECK(af_env_decode(NULL, &dec) == -1);
    CHECK(af_env_decode(buf, NULL) == -1);
}

static void t_legacy_real_data(void) {
    FILE *f = fopen("data/e10_env_real.csv", "r");
    if (!f) {
        CHECK(0);
        return;
    }
    char line[256] = {0};
    int total = 0;
    int csv_ok = 0;
    int bytes_ok = 0;

    // Skip header
    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        CHECK(0);
        return;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        // Parse: ts,env,temp_c,press_mmhg,press_pa,flags
        unsigned long ts = 0;
        char kind[16] = {0};
        char temp_str[32] = {0};
        char mmhg_str[32] = {0};
        unsigned long pa = 0;
        int flags = 0;

        if (sscanf(line, "%lu,%15[^,],%31[^,],%31[^,],%lu,%d", &ts, kind, temp_str, mmhg_str, &pa, &flags) != 6) {
            continue;
        }

        double temp_d = strtod(temp_str, NULL);
        int16_t t_c100 = (int16_t)lround(temp_d * 100.0);

        uint8_t old[14] = {0};
        legacy_encode(ts, t_c100, pa, (uint8_t)flags, old);

        af_env_rec_t d = {0};
        int rc = af_env_decode(old, &d);
        if (rc != 0) continue;
        if (d.sensor != 1 || d.model != AF_SNS_BMP280 || d.h_c100 != 0) continue;
        if (d.ts != ts || d.t_c100 != t_c100 || d.p_pa != pa || d.flags != (uint8_t)flags) continue;

        uint8_t nw[14] = {0};
        af_env_encode(&d, nw);
        if (memcmp(old, nw, 14) == 0) bytes_ok++;

        char buf[256] = {0};
        af_env_csv(&d, buf, sizeof(buf));
        char expected[256] = {0};
        snprintf(expected, sizeof(expected), "%lu,env,1,bmp280,%s,,%s,%lu,%d\n", ts, temp_str, mmhg_str, pa, flags);
        if (strcmp(buf, expected) == 0) csv_ok++;

        total++;
    }
    fclose(f);

    CHECK(total >= 291);
    CHECK(csv_ok == total);
    CHECK(bytes_ok == total);
    printf("INFO real_rows=%d csv_equal=%d bytes_equal=%d\n", total, csv_ok, bytes_ok);
}

static void t_legacy_segment_scan(void) {
    // Read data
    FILE *f = fopen("data/e10_env_real.csv", "r");
    if (!f) {
        CHECK(0);
        return;
    }
    char line[256] = {0};
    if (fgets(line, sizeof(line), f) == NULL) { fclose(f); CHECK(0); return; }

    uint8_t *img = (uint8_t*)malloc(32 + 14 * 10000);
    if (!img) { fclose(f); CHECK(0); return; }
    memset(img, 0, 32 + 14 * 10000);

    int count = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        unsigned long ts = 0;
        char kind[16] = {0};
        char temp_str[32] = {0};
        char mmhg_str[32] = {0};
        unsigned long pa = 0;
        int flags = 0;
        if (sscanf(line, "%lu,%15[^,],%31[^,],%31[^,],%lu,%d", &ts, kind, temp_str, mmhg_str, &pa, &flags) != 6) continue;
        double temp_d = strtod(temp_str, NULL);
        int16_t t_c100 = (int16_t)lround(temp_d * 100.0);
        uint8_t rec[14] = {0};
        legacy_encode(ts, t_c100, pa, (uint8_t)flags, rec);
        memcpy(img + 32 + count * 14, rec, 14);
        count++;
    }
    fclose(f);

    af_seg_hdr_t hdr = {0};
    hdr.kind = AF_SEG_KIND_ENV;
    hdr.rec_size = 14;
    hdr.seg_idx = 0;
    hdr.first_seq = 0;
    hdr.epoch = 3;
    hdr.cap_recs = 1024;
    af_seg_hdr_encode(&hdr, img);

    af_seg_scan_t scan = {0};
    int rc = af_seg_scan(img, 32 + 14 * count, &scan);
    CHECK(rc == 0);
    CHECK(scan.valid == (uint32_t)count);
    CHECK(scan.tail_bad == 0);
    CHECK(scan.good_bytes == (uint32_t)(32 + 14 * count));

    // Add 10 new records
    for (int i = 0; i < 10; i++) {
        af_env_rec_t rec = {0};
        rec.ts = 1761300000 + i;
        rec.sensor = 2 + (i % 7);
        rec.model = AF_SNS_BME280;
        rec.flags = AF_ENV_F_T | AF_ENV_F_H;
        rec.t_c100 = 2000;
        rec.h_c100 = 5000;
        uint8_t buf[14] = {0};
        af_env_encode(&rec, buf);
        memcpy(img + 32 + (count + i) * 14, buf, 14);
    }
    rc = af_seg_scan(img, 32 + 14 * (count + 10), &scan);
    CHECK(rc == 0);
    CHECK(scan.valid == (uint32_t)(count + 10));

    // Add 5 bytes garbage
    memset(img + 32 + 14 * (count + 10), 0xAA, 5);
    rc = af_seg_scan(img, 32 + 14 * (count + 10) + 5, &scan);
    CHECK(rc == 0);
    CHECK(scan.valid == (uint32_t)(count + 10));
    CHECK(scan.tail_bad == 1);

    // Corrupt CRC at index 100
    if (count > 100) {
        uint8_t *rec_ptr = img + 32 + 100 * 14;
        rec_ptr[12] ^= 0xFF;
        rc = af_seg_scan(img, 32 + 14 * count, &scan);
        CHECK(rc == 0);
        CHECK(scan.valid == 100);
    }

    free(img);
}

static void t_core_ring_mixed(void) {
    af_store_core_t c = {0};
    uint8_t buf[14 * 64] = {0};
    CHECK(af_core_env_init(&c, buf, sizeof(buf), 0) == 0);
    /* c = {0} минует af_core_init: en_env==0 -> push возвращает 1 (выключен, af_store_core.h:60). Включаем явно (e16i П.2). */
    c.en_env = 1;

    // Push 20 legacy
    for (int i = 0; i < 20; i++) {
        uint8_t rec[14] = {0};
        legacy_encode(1761300000 + i, 2000, 100000, AF_ENV_F_T | AF_ENV_F_P, rec);
        af_ring_push(&c.env, rec);
    }

    // Push 20 new
    for (int i = 0; i < 20; i++) {
        af_env_rec_t rec = {0};
        rec.ts = 1761300000 + 20 + i;
        rec.sensor = 1 + (i % 4);
        rec.model = AF_SNS_BME280;
        rec.flags = AF_ENV_F_T | AF_ENV_F_H;
        rec.t_c100 = 2000;
        rec.h_c100 = 5000;
        CHECK(af_core_env_push(&c, &rec) == 0);
    }

    uint8_t out[14 * 64] = {0};
    uint32_t next = 0;
    uint32_t lost = 0;
    int n = af_core_read(&c, 2, 0, out, 64, &next, &lost);
    CHECK(n == 40);
    CHECK(lost == 0);

    for (int i = 0; i < 40; i++) {
        af_env_rec_t d = {0};
        CHECK(af_env_decode(out + i * 14, &d) == 0);
        if (i < 20) {
            CHECK(d.sensor == 1);
            CHECK(d.model == AF_SNS_BMP280);
        } else {
            CHECK(d.sensor == 1 + ((i - 20) % 4));
            CHECK(d.model == AF_SNS_BME280);
            CHECK(d.h_c100 == 5000);
        }
    }

    // Overwrite
    for (int i = 0; i < 40; i++) {
        af_env_rec_t rec = {0};
        rec.ts = 1761300000 + 100 + i;
        rec.sensor = 1;
        rec.model = AF_SNS_BMP280;
        rec.flags = AF_ENV_F_T;
        rec.t_c100 = 1000;
        CHECK(af_core_env_push(&c, &rec) == 0);
    }

    n = af_core_read(&c, 2, 0, out, 64, &next, &lost);
    CHECK(n == 64);
    for (int i = 0; i < 40; i++) {
        af_env_rec_t d = {0};
        CHECK(af_env_decode(out + i * 14, &d) == 0);
    }
}

static void t_csv_v2(void) {
    char buf[256] = {0};
    size_t len = af_env_csv_header(buf, sizeof(buf));
    CHECK(len == strlen("ts,kind,sensor,model,temp_c,hum_pct,press_mmhg,press_pa,flags\n"));
    CHECK(strcmp(buf, "ts,kind,sensor,model,temp_c,hum_pct,press_mmhg,press_pa,flags\n") == 0);

    af_env_rec_t rec = {0};
    rec.ts = 1761300000;
    rec.sensor = 1;
    rec.model = AF_SNS_BMP280;
    rec.flags = AF_ENV_F_T | AF_ENV_F_P;
    rec.t_c100 = 2650;
    rec.p_pa = 100218;
    rec.h_c100 = 0;

    len = af_env_csv(&rec, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strcmp(buf, "1761300000,env,1,bmp280,26.50,,751.70,100218,3\n") == 0);

    rec.sensor = 3;
    rec.model = AF_SNS_SHT3X;
    rec.flags = AF_ENV_F_T | AF_ENV_F_H;
    rec.t_c100 = 2345;
    rec.p_pa = 0;
    rec.h_c100 = 4568;
    len = af_env_csv(&rec, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strcmp(buf, "1761300000,env,3,sht3x,23.45,45.68,,,9\n") == 0);

    rec.sensor = 2;
    rec.model = AF_SNS_BME280;
    rec.flags = AF_ENV_F_T | AF_ENV_F_P | AF_ENV_F_H;
    rec.t_c100 = -550;
    rec.p_pa = 100000;
    rec.h_c100 = 5500;
    len = af_env_csv(&rec, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strcmp(buf, "1761300000,env,2,bme280,-5.50,55.00,750.06,100000,11\n") == 0);

    rec.sensor = 8;
    rec.model = AF_SNS_AHT2X;
    rec.flags = AF_ENV_F_H;
    rec.t_c100 = 0;
    rec.p_pa = 0;
    rec.h_c100 = 10000;
    len = af_env_csv(&rec, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strcmp(buf, "1761300000,env,8,aht2x,,100.00,,,8\n") == 0);

    // Small buffer
    char small[10] = {0};
    CHECK(af_env_csv(&rec, small, 10) == -1);
    CHECK(small[0] == 0);

    // Exact size
    char exact[256] = {0};
    size_t exact_len = af_env_csv(&rec, exact, 256);
    CHECK(exact_len > 0);
    CHECK(af_env_csv(&rec, exact, exact_len) == -1);
    CHECK(exact[0] == 0);

    // Header small
    char hbuf[256] = {0};
    size_t hlen = af_env_csv_header(hbuf, 256);
    CHECK(af_env_csv_header(hbuf, hlen) == -1);
    CHECK(hbuf[0] == 0);
}

static void t_make_rec_ex(void) {
    af_env_in_t in = {0};
    in.sensor = 2;
    in.model = AF_SNS_BME280;
    in.enabled = 1;
    in.caps = AF_CAP_TEMP | AF_CAP_PRESS | AF_CAP_HUM;
    in.t_c = 23.456f;
    in.p_pa = 100000.0f;
    in.h_pct = 45.678f;
    in.ts = 1761300000;
    in.t_off10 = 5;
    in.p_off10 = -10;
    in.h_off10 = 20;

    af_env_rec_t out = {0};
    float t_shown = 0, p_shown = 0, h_shown = 0;
    int rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown, &h_shown);
    CHECK(rc == 0);
    CHECK(fabsf(t_shown - 23.956f) < 0.001f);
    CHECK(fabsf(p_shown - (100000.0f / 133.322387f - 1.0f)) < 0.001f);
    CHECK(fabsf(h_shown - 47.678f) < 0.001f);
    CHECK(out.flags == (AF_ENV_F_T | AF_ENV_F_P | AF_ENV_F_H | AF_ENV_F_CORR));
    CHECK(out.t_c100 == 2396);
    CHECK(out.h_c100 == 4768);
    CHECK(out.sensor == 2);
    CHECK(out.model == AF_SNS_BME280);
    CHECK(abs((int)out.p_pa - 99867) <= 1);

    // No corrections
    in.t_off10 = 0;
    in.p_off10 = 0;
    in.h_off10 = 0;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown, &h_shown);
    CHECK(rc == 0);
    CHECK((out.flags & AF_ENV_F_CORR) == 0);

    // No H cap
    in.caps = AF_CAP_TEMP | AF_CAP_PRESS;
    in.h_off10 = 20;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown, &h_shown);
    CHECK(rc == 0);
    CHECK((out.flags & AF_ENV_F_H) == 0);
    CHECK((out.flags & AF_ENV_F_CORR) == 0);

    // SHT3X, p NaN
    in.t_off10 = 0; in.p_off10 = 0; in.h_off10 = 0;
    in.caps = AF_CAP_TEMP | AF_CAP_HUM;
    in.p_pa = NAN;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown, &h_shown);
    CHECK(rc == 0);
    CHECK(out.flags == (AF_ENV_F_T | AF_ENV_F_H));
    CHECK(out.p_pa == 0);
    CHECK(isnan(p_shown));

    // Clamp h
    in.caps = AF_CAP_TEMP | AF_CAP_HUM;
    in.h_pct = 100.3f;
    in.h_off10 = 0;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown, &h_shown);
    CHECK(rc == 0);
    CHECK(out.h_c100 == 10000);

    // Out of range h
    in.h_pct = 101.0f;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown, &h_shown);
    CHECK(rc == 0);
    CHECK((out.flags & AF_ENV_F_H) == 0);
    CHECK(isnan(h_shown));

    // NaN h
    in.h_pct = NAN;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown, &h_shown);
    CHECK(rc == 0);
    CHECK((out.flags & AF_ENV_F_H) == 0);

    // Negative h
    in.h_pct = -0.2f;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown, &h_shown);
    CHECK(rc == 0);
    CHECK((out.flags & AF_ENV_F_H) == 0);

    // Disabled
    in.enabled = 0;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown, &h_shown);
    CHECK(rc == -1);
    CHECK(isnan(t_shown));

    // ts 0
    in.enabled = 1;
    in.ts = 0;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown, &h_shown);
    CHECK(rc == -3);
    CHECK(!isnan(t_shown));

    // NULL
    CHECK(af_env_make_rec_ex(NULL, &out, &t_shown, &p_shown, &h_shown) == -4);
    CHECK(af_env_make_rec_ex(&in, NULL, &t_shown, &p_shown, &h_shown) == -4);

    // Implausible T
    in.ts = 1761300000;
    in.t_c = 200.0f;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown, &h_shown);
    CHECK(rc == -2);

    // T NaN, P NaN
    in.t_c = NAN;
    in.p_pa = NAN;
    rc = af_env_make_rec_ex(&in, &out, &t_shown, &p_shown, &h_shown);
    CHECK(rc == -2);
}

static void t_json_list(void) {
    char buf[1024] = {0};
    int len = af_sns_json_list(NULL, 0, buf, sizeof(buf));
    CHECK(len == 2);
    CHECK(strcmp(buf, "[]") == 0);

    af_sns_view_t v = {0};
    v.sensor = 1;
    v.model = AF_SNS_BMP280;
    v.addr = 0x76;
    v.enabled = 1;
    v.state = 1;
    v.caps = 3; // T|P
    v.t_c = 23.45f;
    v.p_mmhg = 745.12f;
    v.h_pct = NAN;
    v.t_off10 = 0;
    v.p_off10 = 0;
    v.h_off10 = 0;
    v.period_s = 30;
    v.n_ok = 12;
    v.n_err = 0;
    v.store_ok = 12;
    v.store_fail = 0;
    v.last_ok_ts = 1761300000;
    v.last_ok_age_s = 15;
    v.err = "";

    len = af_sns_json_list(&v, 1, buf, sizeof(buf));
    CHECK(len > 0);
    const char *expected = "[{\"sensor\":1,\"model\":\"bmp280\",\"addr\":\"0x76\",\"enabled\":true,\"status\":\"ok\",\"err\":\"\",\"caps\":3,\"temp\":true,\"press\":true,\"hum\":false,\"t_c\":23.45,\"h_pct\":null,\"p_mmhg\":745.12,\"t_off\":0.0,\"p_off\":0.0,\"h_off\":0.0,\"period_s\":30,\"n_ok\":12,\"n_err\":0,\"store_ok\":12,\"store_fail\":0,\"last_ok_ts\":1761300000,\"last_ok_s_ago\":15}]";
    CHECK(strcmp(buf, expected) == 0);

    // Disabled
    v.enabled = 0;
    v.err = "x";
    len = af_sns_json_list(&v, 1, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strstr(buf, "\"status\":\"off\"") != NULL);
    CHECK(strstr(buf, "\"err\":\"\"") != NULL);

    // Error
    v.enabled = 1;
    v.state = 2;
    v.err = "read";
    len = af_sns_json_list(&v, 1, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strstr(buf, "\"status\":\"err\"") != NULL);
    CHECK(strstr(buf, "\"err\":\"read\"") != NULL);

    v.state = 0;
    v.err = "";
    len = af_sns_json_list(&v, 1, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strstr(buf, "\"status\":\"err\"") != NULL);
    CHECK(strstr(buf, "\"err\":\"init\"") != NULL);

    v.state = 2;
    v.err = "";
    len = af_sns_json_list(&v, 1, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strstr(buf, "\"err\":\"error\"") != NULL);

    // SHT3X
    v.model = AF_SNS_SHT3X;
    v.caps = AF_CAP_TEMP | AF_CAP_HUM;
    v.p_mmhg = 745.12f; // Should be null
    len = af_sns_json_list(&v, 1, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strstr(buf, "\"p_mmhg\":null") != NULL);

    // SHT3X исправный: давление null несмотря на значение, T и H числами (2 и 2 знака)
    af_sns_view_t w = v;
    w.state = 1;
    w.enabled = 1;
    w.err = "";
    w.t_c = 23.45f;
    w.h_pct = 45.678f;
    w.p_mmhg = 745.12f;
    len = af_sns_json_list(&w, 1, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strstr(buf, "\"p_mmhg\":null") != NULL);
    CHECK(strstr(buf, "\"t_c\":23.45,\"h_pct\":45.68,") != NULL);
    w.caps = AF_CAP_HUM;
    len = af_sns_json_list(&w, 1, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strstr(buf, "\"t_c\":null") != NULL);

    // Two sensors
    af_sns_view_t v2 = v;
    v2.sensor = 2;
    af_sns_view_t two[2];
    two[0] = v;
    two[1] = v2;
    len = af_sns_json_list(two, 2, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strstr(buf, "},{") != NULL);

    // Negative offset
    v.t_off10 = -5;
    len = af_sns_json_list(&v, 1, buf, sizeof(buf));
    CHECK(len > 0);
    CHECK(strstr(buf, "\"t_off\":-0.5") != NULL);

    // Buffer size
    int full_len = af_sns_json_list(&v, 1, buf, sizeof(buf));
    char big[1024];
    for (int cap = 0; cap < full_len; cap++) {
        memset(big, 0xA5, sizeof(big));
        int rc = af_sns_json_list(&v, 1, big, (size_t)cap);
        CHECK(rc == -1);
        CHECK((unsigned char)big[cap] == 0xA5);
    }
    CHECK(af_sns_json_list(&v, 1, buf, (size_t)full_len + 1) == full_len);

    CHECK(af_sns_json_list(&v, -1, buf, sizeof(buf)) == -1);
    CHECK(af_sns_json_list(NULL, 1, buf, sizeof(buf)) == -1);
    CHECK(af_sns_json_list(&v, 1, NULL, sizeof(buf)) == -1);
}

int main(void) {
    RUN(t_rec_roundtrip);
    RUN(t_decode_errors_v2);
    RUN(t_legacy_real_data);
    RUN(t_legacy_segment_scan);
    RUN(t_core_ring_mixed);
    RUN(t_csv_v2);
    RUN(t_make_rec_ex);
    RUN(t_json_list);
    TESTS_DONE();
    return 0;
}
