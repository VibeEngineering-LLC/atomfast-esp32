#include <stdio.h>
#include <string.h>
#include "af_parse.h"
#include "af_rec.h"
#include "af_window.h"
#include "real_samples.h"

int main(void) {
    const uint8_t modes[] = {0x00, 0x1F};
    char line[512];

    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        af_window_t w;
        if (af_window_init(&w, 60, modes[m]) != 0) {
            continue;
        }

        for (size_t i = 0; i <= REAL_N; i++) {
            size_t k = i % REAL_N;
            const real_t *r = &REAL[k];

            af_sample_t s;
            memset(&s, 0, sizeof(s));
            s.dose_msv = r->dose;
            s.doserate_usvh = r->rate;
            s.pulses_2s = r->p2s;
            s.cps = r->p2s / 2.0f;
            s.battery = r->bat;
            s.temp_c = r->temp;

            af_raw_rec_t rec;
            rec.ts = 1760000040u + 2u * (uint32_t)i;
            rec.s = s;

            uint8_t buf[AF_REC_RAW_SIZE];
            af_raw_encode(&rec, buf);

            af_raw_rec_t dec;
            if (af_raw_decode(buf, &dec) == 0) {
                if (af_raw_csv(&dec, "TESTMAC", line, sizeof(line)) > 0) {
                    fputs(line, stdout);
                }
            }

            af_agg_rec_t agg;
            if (af_window_add(&w, rec.ts, &s, &agg) == 1) {
                if (af_agg_csv(&agg, "TESTMAC", line, sizeof(line)) > 0) {
                    fputs(line, stdout);
                }
            }
        }

        af_agg_rec_t flush_agg;
        if (af_window_flush(&w, &flush_agg) == 1) {
            if (af_agg_csv(&flush_agg, "TESTMAC", line, sizeof(line)) > 0) {
                fputs(line, stdout);
            }
        }
    }

    return 0;
}
