#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "af_test.h"
#include "af_parse.h"
#include "af_rec.h"
#include "real_samples.h"

static af_raw_rec_t mk_raw(uint32_t ts, float dose, float rate, uint16_t p, int8_t bat, int8_t t, uint8_t fl) {
    af_raw_rec_t r;
    r.ts = ts;
    r.s.flags = fl;
    r.s.dose_msv = dose;
    r.s.doserate_usvh = rate;
    r.s.pulses_2s = p;
    r.s.cps = p / 2.0f;
    r.s.battery = bat;
    r.s.temp_c = t;
    return r;
}

static af_agg_rec_t mk_agg(uint32_t ts, uint16_t per, uint16_t n, float dose, float rate, float pulses, int8_t bat, int8_t t, uint8_t fl, uint8_t afl, uint8_t modes) {
    af_agg_rec_t a;
    a.ts = ts;
    a.period_s = per;
    a.n = n;
    a.dose_msv = dose;
    a.doserate_usvh = rate;
    a.pulses_2s = pulses;
    a.battery = bat;
    a.temp_c = t;
    a.flags = fl;
    a.agg_flags = afl;
    a.modes = modes;
    return a;
}

static void t_crc(void) {
    CHECK(af_crc16((const uint8_t*)"123456789", 9) == 0x29B1);
    CHECK(af_crc16((const uint8_t*)"123456789", 0) == 0xFFFF);
}

static void t_raw_layout(void) {
    af_raw_rec_t r = mk_raw(0x01020304, 1.0f, 2.0f, 0x0102, -1, -128, 0x08);
    uint8_t out[20];
    af_raw_encode(&r, out);
    CHECK(out[0] == 0x04 && out[1] == 0x03 && out[2] == 0x02 && out[3] == 0x01);
    CHECK(out[4] == 0x00 && out[5] == 0x00 && out[6] == 0x80 && out[7] == 0x3F);
    CHECK(out[8] == 0x00 && out[9] == 0x00 && out[10] == 0x00 && out[11] == 0x40);
    CHECK(out[12] == 0x02 && out[13] == 0x01);
    CHECK(out[14] == 0xFF);
    CHECK(out[15] == 0x80);
    CHECK(out[16] == 0x08);
    CHECK(out[17] == 0x00);
}

static void t_raw_crc_bytes(void) {
    af_raw_rec_t r = mk_raw(0x01020304, 1.0f, 2.0f, 0x0102, -1, -128, 0x08);
    uint8_t out[20];
    af_raw_encode(&r, out);
    uint16_t crc = af_crc16(out, 18);
    CHECK((out[18] | (out[19] << 8)) == crc);
}

static void t_raw_vector(void) {
    uint8_t v[13] = {0x00,0xEE,0xEE,0xE1,0x3C,0x07,0x7A,0xBA,0x3D,0x26,0x00,0x40,0x19};
    af_sample_t smp;
    CHECK(af_parse(v, 13, &smp) == AF_OK);
    af_raw_rec_t r;
    r.ts = 1760000000;
    r.s = smp;
    uint8_t out[20];
    af_raw_encode(&r, out);
    CHECK(memcmp(&out[4], &v[1], 8) == 0);
    CHECK(out[12] == v[9]);
    CHECK(out[13] == v[10]);
    CHECK(out[14] == v[11]);
    CHECK(out[15] == v[12]);
    CHECK(out[16] == v[0]);
}

static void t_raw_roundtrip_real(void) {
    int bad = 0;
    for (unsigned i = 0; i < REAL_N; i++) {
        uint32_t ts = 1760000000u + 2u * i;
        af_raw_rec_t a = mk_raw(ts, REAL[i].dose, REAL[i].rate, REAL[i].p2s, REAL[i].bat, REAL[i].temp, 0);
        uint8_t buf[20];
        af_raw_encode(&a, buf);
        af_raw_rec_t b;
        if (af_raw_decode(buf, &b) != 0) { bad++; continue; }
        if (memcmp(&a.s.doserate_usvh, &b.s.doserate_usvh, 4) != 0) bad++;
        if (memcmp(&a.s.dose_msv, &b.s.dose_msv, 4) != 0) bad++;
        if (a.ts != b.ts) bad++;
        if (a.s.pulses_2s != b.s.pulses_2s) bad++;
        if (a.s.battery != b.s.battery) bad++;
        if (a.s.temp_c != b.s.temp_c) bad++;
        if (b.s.cps != (float)REAL[i].p2s / 2.0f) bad++;
    }
    CHECK(bad == 0);
}

static void t_raw_crc_flip(void) {
    af_raw_rec_t a = mk_raw(1760000000u, REAL[0].dose, REAL[0].rate, REAL[0].p2s, REAL[0].bat, REAL[0].temp, 0);
    uint8_t buf[20];
    af_raw_encode(&a, buf);
    int bad = 0;
    for (int k = 0; k < 20; k++) {
        uint8_t orig = buf[k];
        buf[k] ^= 0x01;
        af_raw_rec_t b;
        if (af_raw_decode(buf, &b) != -1) bad++;
        buf[k] = orig;
    }
    CHECK(bad == 0);
}

static void t_raw_reserved(void) {
    af_raw_rec_t a = mk_raw(1760000000u, REAL[0].dose, REAL[0].rate, REAL[0].p2s, REAL[0].bat, REAL[0].temp, 0);
    uint8_t buf[20];
    af_raw_encode(&a, buf);
    buf[17] = 1;
    uint16_t crc = af_crc16(buf, 18);
    buf[18] = crc & 0xFF;
    buf[19] = crc >> 8;
    af_raw_rec_t b;
    b.ts = 777;
    CHECK(af_raw_decode(buf, &b) == -2);
    CHECK(b.ts == 777);
}

static void t_raw_extremes(void) {
    af_raw_rec_t cases[4] = {
        mk_raw(0, 0.0f, 0.0f, 0, -128, -128, 0),
        mk_raw(0xFFFFFFFF, 0.192f, 1000.0f, 65535, 127, 127, 0xFF),
        mk_raw(1, 3.0e38f, 1.0f, 10, 50, 20, 0),
        mk_raw(1, 1.0f, 1.0e-30f, 10, 50, 20, 0)
    };
    int bad = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t buf[20];
        af_raw_encode(&cases[i], buf);
        af_raw_rec_t b;
        if (af_raw_decode(buf, &b) != 0) { bad++; continue; }
        if (memcmp(&cases[i].s.dose_msv, &b.s.dose_msv, 4) != 0) bad++;
        if (memcmp(&cases[i].s.doserate_usvh, &b.s.doserate_usvh, 4) != 0) bad++;
        if (cases[i].ts != b.ts) bad++;
        if (cases[i].s.pulses_2s != b.s.pulses_2s) bad++;
        if (cases[i].s.battery != b.s.battery) bad++;
        if (cases[i].s.temp_c != b.s.temp_c) bad++;
        if (cases[i].s.flags != b.s.flags) bad++;
    }
    CHECK(bad == 0);
}

static void t_agg_layout(void) {
    af_agg_rec_t a = mk_agg(0x01020304, 0x0102, 0x0304, 1.0f, 2.0f, 35.5f, -1, -128, 0x08, 0x03, 0x1F);
    uint8_t out[28];
    af_agg_encode(&a, out);
    CHECK(out[0] == 0x04 && out[1] == 0x03 && out[2] == 0x02 && out[3] == 0x01);
    CHECK(out[4] == 0x02 && out[5] == 0x01);
    CHECK(out[6] == 0x04 && out[7] == 0x03);
    CHECK(out[8] == 0x00 && out[9] == 0x00 && out[10] == 0x80 && out[11] == 0x3F);
    CHECK(out[12] == 0x00 && out[13] == 0x00 && out[14] == 0x00 && out[15] == 0x40);
    CHECK(out[16] == 0x00 && out[17] == 0x00 && out[18] == 0x0E && out[19] == 0x42);
    CHECK(out[20] == 0xFF);
    CHECK(out[21] == 0x80);
    CHECK(out[22] == 0x08);
    CHECK(out[23] == 0x03);
    CHECK(out[24] == 0x1F);
    CHECK(out[25] == 0x00);
    uint16_t crc = af_crc16(out, 26);
    CHECK((out[26] | (out[27] << 8)) == crc);
}

static void t_agg_roundtrip_real(void) {
    int bad = 0;
    for (int w = 0; w < 3; w++) {
        unsigned first = w * 15;
        unsigned count = (w < 2) ? 15 : (REAL_N - first);
        double sum_rate = 0.0, sum_pulses = 0.0;
        for (unsigned i = first; i < first + count; i++) {
            sum_rate += REAL[i].rate;
            sum_pulses += REAL[i].p2s;
        }
        float avg_rate = (float)(sum_rate / count);
        float avg_pulses = (float)(sum_pulses / count);
        
        af_agg_rec_t a = mk_agg(1760000040 + 60 * w, 60, (uint16_t)count, REAL[first].dose, avg_rate, avg_pulses, REAL[first].bat, REAL[first].temp, 0, 0, 0);
        uint8_t buf[28];
        af_agg_encode(&a, buf);
        af_agg_rec_t b;
        if (af_agg_decode(buf, &b) != 0) { bad++; continue; }
        if (memcmp(&a.dose_msv, &b.dose_msv, 4) != 0) bad++;
        if (memcmp(&a.doserate_usvh, &b.doserate_usvh, 4) != 0) bad++;
        if (memcmp(&a.pulses_2s, &b.pulses_2s, 4) != 0) bad++;
        if (a.ts != b.ts) bad++;
        if (a.period_s != b.period_s) bad++;
        if (a.n != b.n) bad++;
        if (a.battery != b.battery) bad++;
        if (a.temp_c != b.temp_c) bad++;
        if (a.flags != b.flags) bad++;
        if (a.agg_flags != b.agg_flags) bad++;
        if (a.modes != b.modes) bad++;
    }
    CHECK(bad == 0);
}

static void t_agg_errors(void) {
    af_agg_rec_t a = mk_agg(1, 60, 10, 1.0f, 1.0f, 10.0f, 50, 20, 0, 0, 0);
    uint8_t buf[28];
    af_agg_encode(&a, buf);
    int bad = 0;
    for (int k = 0; k < 28; k++) {
        uint8_t orig = buf[k];
        buf[k] ^= 0x01;
        af_agg_rec_t b;
        if (af_agg_decode(buf, &b) != -1) bad++;
        buf[k] = orig;
    }
    CHECK(bad == 0);
    
    buf[25] = 1;
    uint16_t crc = af_crc16(buf, 26);
    buf[26] = crc & 0xFF;
    buf[27] = crc >> 8;
    af_agg_rec_t b;
    CHECK(af_agg_decode(buf, &b) == -2);
}

static void t_csv_header(void) {
    char buf[160];
    int len = af_csv_header(0, buf, 160);
    CHECK(len == (int)strlen(buf));
    CHECK(strcmp(buf, "ts,kind,dose_msv,doserate_usvh,pulses_2s,cps,battery,temp_c,flags,n\n") == 0);
    
    len = af_csv_header(1, buf, 160);
    CHECK(len == (int)strlen(buf));
    CHECK(strcmp(buf, "mac,ts,kind,dose_msv,doserate_usvh,pulses_2s,cps,battery,temp_c,flags,n\n") == 0);
}

static void t_csv_raw(void) {
    af_raw_rec_t r = mk_raw(1760000000, 0.192f, 0.078f, 33, 28, 25, 0);
    char buf[160];
    int len = af_raw_csv(&r, NULL, buf, 160);
    CHECK(len == (int)strlen(buf));
    CHECK(strcmp(buf, "1760000000,0,0.192,0.078,33,16.5,28,25,0,1\n") == 0);
    
    len = af_raw_csv(&r, "TESTMAC", buf, 160);
    CHECK(len == (int)strlen(buf));
    CHECK(strcmp(buf, "TESTMAC,1760000000,0,0.192,0.078,33,16.5,28,25,0,1\n") == 0);
}

static void t_csv_agg(void) {
    af_agg_rec_t a = mk_agg(1760000040, 60, 30, 0.192f, 0.0745f, 35.5f, 28, 25, 0x09, 0x03, 0);
    char buf[160];
    int len = af_agg_csv(&a, "TESTMAC", buf, 160);
    CHECK(len == (int)strlen(buf));
    CHECK(strcmp(buf, "TESTMAC,1760000040,60,0.192,0.0745,35.5,17.75,28,25,9,30\n") == 0);
    
    a.period_s = 1200;
    len = af_agg_csv(&a, "TESTMAC", buf, 160);
    CHECK(len > 0);
    CHECK(strncmp(buf, "TESTMAC,1760000040,1200,", 24) == 0);
}

static void t_csv_small_buf(void) {
    af_raw_rec_t r = mk_raw(1760000000, 0.192f, 0.078f, 33, 28, 25, 0);
    char buf[160];
    int L = af_raw_csv(&r, NULL, buf, 160);
    
    char b[64];
    memset(b, 0x55, sizeof(b));
    int ret = af_raw_csv(&r, NULL, b, L + 1);
    CHECK(ret == L);
    CHECK(b[L] == 0);
    CHECK(b[L+1] == 0x55);
    
    memset(b, 0x55, sizeof(b));
    ret = af_raw_csv(&r, NULL, b, L);
    CHECK(ret == -1);
    CHECK(b[0] == 0);
    
    ret = af_raw_csv(&r, NULL, b, 0);
    CHECK(ret == -1);
    
    ret = af_raw_csv(&r, NULL, NULL, 160);
    CHECK(ret == -1);
}

static void t_csv_bad_mac(void) {
    af_raw_rec_t r = mk_raw(1760000000, 0.192f, 0.078f, 33, 28, 25, 0);
    af_agg_rec_t a = mk_agg(1760000040, 60, 30, 0.192f, 0.0745f, 35.5f, 28, 25, 0x09, 0x03, 0);
    char buf[160];
    
    CHECK(af_raw_csv(&r, "A,B", buf, 160) == -2);
    CHECK(af_raw_csv(&r, "A\"B", buf, 160) == -2);
    CHECK(af_raw_csv(&r, "A\nB", buf, 160) == -2);
    CHECK(af_raw_csv(&r, "A\rB", buf, 160) == -2);
    
    CHECK(af_agg_csv(&a, "A,B", buf, 160) == -2);
    CHECK(af_agg_csv(&a, "A\"B", buf, 160) == -2);
    CHECK(af_agg_csv(&a, "A\nB", buf, 160) == -2);
    CHECK(af_agg_csv(&a, "A\rB", buf, 160) == -2);
    
    int len = af_raw_csv(&r, "", buf, 160);
    CHECK(len > 0);
    CHECK(strncmp(buf, ",1760000000,", 12) == 0);
    
    len = af_raw_csv(&r, "AB:CD", buf, 160);
    CHECK(len > 0);
}

static void t_csv_nonfinite(void) {
    af_raw_rec_t r = mk_raw(1760000000, NAN, 0.078f, 33, 28, 25, 0);
    char buf[160];
    CHECK(af_raw_csv(&r, NULL, buf, 160) == -3);
    
    r = mk_raw(1760000000, 0.192f, INFINITY, 33, 28, 25, 0);
    CHECK(af_raw_csv(&r, NULL, buf, 160) == -3);
    
    af_agg_rec_t a = mk_agg(1760000040, 60, 30, 0.192f, 0.0745f, NAN, 28, 25, 0x09, 0x03, 0);
    CHECK(af_agg_csv(&a, "TESTMAC", buf, 160) == -3);
    
    r = mk_raw(1760000000, NAN, 0.078f, 33, 28, 25, 0);
    CHECK(af_raw_csv(&r, "A,B", buf, 160) == -2);
    
    r = mk_raw(1760000000, NAN, 0.078f, 33, 28, 25, 0);
    CHECK(af_raw_csv(&r, NULL, buf, 4) == -3);
}

static void t_csv_real_lines(void) {
    int bad = 0;
    for (unsigned i = 0; i < REAL_N; i++) {
        af_raw_rec_t r = mk_raw(1760000000u + 2u*i, REAL[i].dose, REAL[i].rate, REAL[i].p2s, REAL[i].bat, REAL[i].temp, 0);
        char buf[160];
        int len = af_raw_csv(&r, NULL, buf, 160);
        if (len <= 0) { bad++; continue; }
        
        int commas = 0;
        for (int j = 0; j < len; j++) if (buf[j] == ',') commas++;
        if (commas != 9) { bad++; continue; }
        if (buf[len-1] != '\n') { bad++; continue; }
        
        unsigned long ts;
        unsigned int kind, n;
        double dose, rate, pulses, cps;
        int bat, temp;
        unsigned int flags;
        int parsed = sscanf(buf, "%lu,%u,%lf,%lf,%lf,%lf,%d,%d,%u,%u", &ts, &kind, &dose, &rate, &pulses, &cps, &bat, &temp, &flags, &n);
        if (parsed != 10) { bad++; continue; }
        
        if (kind != 0) bad++;
        if (n != 1) bad++;
        if (cps != pulses / 2.0) bad++;
    }
    CHECK(bad == 0);
}

int main(void) {
    RUN(t_crc);
    RUN(t_raw_layout);
    RUN(t_raw_crc_bytes);
    RUN(t_raw_vector);
    RUN(t_raw_roundtrip_real);
    RUN(t_raw_crc_flip);
    RUN(t_raw_reserved);
    RUN(t_raw_extremes);
    RUN(t_agg_layout);
    RUN(t_agg_roundtrip_real);
    RUN(t_agg_errors);
    RUN(t_csv_header);
    RUN(t_csv_raw);
    RUN(t_csv_agg);
    RUN(t_csv_small_buf);
    RUN(t_csv_bad_mac);
    RUN(t_csv_nonfinite);
    RUN(t_csv_real_lines);
    TESTS_DONE();
}
