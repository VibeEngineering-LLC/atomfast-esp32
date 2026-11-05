#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "af_ble_stats.h"
#include "af_ble_snap.h"
#include "af_ble_nvs.h"

static af_bs_t S;
static af_bs_sess_t ring[32];

int main(void) {
    FILE *f = fopen("data/e16_raw_ts.csv", "r");
    if (!f) {
        fprintf(stderr, "NO DATA\n");
        return 2;
    }

    uint32_t TS[12000];
    int n = 0;
    char line[256];
    if (fgets(line, sizeof(line), f)) {
        while (n < 12000 && fgets(line, sizeof(line), f)) {
            uint32_t epoch, ts;
            if (sscanf(line, "%u,%u", &epoch, &ts) == 2) {
                TS[n++] = ts;
            }
        }
    }
    fclose(f);

    if (n == 0) {
        fprintf(stderr, "NO DATA\n");
        return 2;
    }

    af_bs_init(&S, ring);

    uint64_t tm[12000];
    for (int i = 0; i < n; i++) {
        tm[i] = 1000000 + (uint64_t)(TS[i] - TS[0]) * 1000;
    }

    int a = 0;
    int k = 0;
    while (a < n) {
        int b = a;
        while (b + 1 < n && (TS[b + 1] - TS[b]) < 10) {
            b++;
        }

        if (k % 2 == 0) {
            af_bs_on_attempt(&S, tm[a] - 4000);
            af_bs_on_open_fail(&S, tm[a] - 3500, 0x3E);
        }

        af_bs_on_attempt(&S, tm[a] - 3000);
        af_bs_on_connect(&S, tm[a] - 1000, 247);

        for (int i = a; i <= b; i++) {
            int ok = (i % 97 != 0);
            af_bs_on_packet(&S, tm[i], ok);
            af_bs_tick(&S, tm[i]);

            af_bs_snap_t snap;
            af_bs_snapshot(&S, tm[i], &snap);

            printf("P %d %.4f %.4f %.4f %.4f %d %d %d %.3f %.3f %u %u %d %d %.3f %.3f %.3f %u %u %d\n",
                   i,
                   snap.pps_inst,
                   snap.pps_1m,
                   snap.pps_10m,
                   snap.pps_sess,
                   snap.i_last_ms,
                   snap.i_min_ms,
                   snap.i_max_ms,
                   snap.i_avg_ms,
                   snap.jitter_ms,
                   snap.pauses,
                   snap.pause_max_ms,
                   snap.i10_min_ms,
                   snap.i10_max_ms,
                   snap.i10_avg_ms,
                   snap.up_boot_pct,
                   snap.bad_pct,
                   snap.sess_pkts,
                   snap.pkts_boot,
                   snap.last_pkt_age_ms
            );
        }

        af_bs_on_disconnect(&S, tm[b] + 2000, 1700000000u + k, 0x08);
        af_bs_tick(&S, tm[b] + 2000);

        af_bs_snap_t snap;
        af_bs_snapshot(&S, tm[b] + 2000, &snap);

        uint8_t blob[64];
        af_bsn_encode(&S, blob, 64);

        const af_bs_sess_t *rec = af_bs_ring_get(&S, 0);

        printf("S %u dur=%u pkts=%u bad=%u setup=%u reason=%u utc=%u ring_n=%d avg=%u med=%u longest=%u shortest=%u last=%u setup_snap=%d boot_reasons=%u,%u,%u,%u,%u,%u life=%u,%u,%u,%u conn_ms=%llu up24=%.3f due29=%d due30=%d blob=",
               k,
               rec->dur_ms,
               rec->pkts,
               rec->bad,
               rec->setup_ms,
               rec->reason,
               rec->end_utc,
               snap.ring_n,
               snap.avg_ms,
               snap.median_ms,
               snap.longest_ms,
               snap.shortest_ms,
               snap.last_dur_ms,
               snap.setup_ms,
               S.boot.reason[0],
               S.boot.reason[1],
               S.boot.reason[2],
               S.boot.reason[3],
               S.boot.reason[4],
               S.boot.reason[5],
               S.life.connects,
               S.life.disconnects,
               S.life.attempts,
               S.life.fails,
               (unsigned long long)S.life.conn_ms,
               snap.up_24h_pct,
               af_bsn_due(&S, tm[b] + 2000 + 29999),
               af_bsn_due(&S, tm[b] + 2000 + 30000)
        );

        for (int i = 0; i < 64; i++) {
            printf("%02x", blob[i]);
        }
        printf("\n");

        a = b + 1;
        k++;
    }

    return 0;
}
