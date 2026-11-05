#!/bin/bash
# Мутации e16e (#SA-3): каждый мутант на копии файла обязан покраснеть; БАЗА зелёная. Запуск в контейнере IDF из tests/host.
cd "$(dirname "$0")"; M=../../main; T=/tmp/mut_e16e; rm -rf $T; mkdir -p $T
C="gcc -std=c11 -w -O2 -I$T -I$M -Ipfs_stubs -I."
run() { # метка, модуль, sed, тест, доп.источники
  cp $M/af_fsinfo.c $M/af_hbuf.h $M/af_etag.h $M/af_rec.h $M/af_crc16_tab.h $M/af_parse.h $M/af_ds.h $M/af_hist_query.h $T/; [ -n "$3" ] && sed -i "$3" $T/$2
  if [ -n "$3" ] && cmp -s $M/$2 $T/$2; then echo "$1 | SED NOT APPLIED"; return; fi
  if $C -o $T/t $4 $5 2>/dev/null; then r=$($T/t 2>&1 | grep -c '^test_af_.*:[0-9]*$'); $T/t >/dev/null 2>&1; rc=$?; if [ -z "$3" ]; then [ $rc -eq 0 ] && echo "$1 | BASE GREEN (ok)" || echo "$1 | BASE RED (плохо)"; else [ $rc -ne 0 ] && echo "$1 | RED, упавших проверок $r" || echo "$1 | GREEN (МУТАНТ ЖИВ)"; fi; else echo "$1 | BUILD FAIL"; fi
}
run BASE_fsinfo af_fsinfo.c '' test_af_fsinfo.c $T/af_fsinfo.c
run F1_ttl_zero af_fsinfo.c 's/AF_FSINFO_TTL_US)/0)/' test_af_fsinfo.c $T/af_fsinfo.c
run F2_err_cached af_fsinfo.c 's/return err;/s_at_us = now; return err;/' test_af_fsinfo.c $T/af_fsinfo.c
run F3_invalidate_noop af_fsinfo.c 's/s_at_us = 0;/;/' test_af_fsinfo.c $T/af_fsinfo.c
run F4_used_from_total af_fsinfo.c 's/s_used = u;/s_used = t;/' test_af_fsinfo.c $T/af_fsinfo.c
run BASE_hbuf af_hbuf.h '' test_af_hbuf.c
run H1_overflow_ge af_hbuf.h 's/h->n + len > h->cap/h->n + len >= h->cap/' test_af_hbuf.c
run H2_flush_keeps_n af_hbuf.h 's/h->n = 0;/;/' test_af_hbuf.c
run H3_not_sticky af_hbuf.h '0,/if (h->err)/s//if (0)/' test_af_hbuf.c
run H4_big_not_direct af_hbuf.h 's/len >= h->cap/len > h->cap/' test_af_hbuf.c
run BASE_etag af_etag.h '' test_af_etag.c
run E1_upper_hex af_etag.h 's/0123456789abcdef/0123456789ABCDEF/' test_af_etag.c
run E2_no_star af_etag.h 's/start\[0\] == .\*./start[0] == 0x01/' test_af_etag.c
run E4_no_trailing_trim af_etag.h 's/while (len > 0 \&\& (start\[len - 1\]/while (0 \&\& (start[len - 1]/' test_af_etag.c
run E3_no_weak af_etag.h 's/s\[0\] == .W./s[0] == 0x01/' test_af_etag.c
run BASE_crc af_rec.h '' test_af_crc16.c
run C1_tab_entry af_crc16_tab.h 's/0x2042/0x2043/' test_af_crc16.c
run C2_tab_last af_crc16_tab.h 's/0x1EF0/0x1EF1/' test_af_crc16.c
run C3_wrong_shift af_rec.h 's/(crc << 8) ^ af_crc16_tab/(crc << 7) ^ af_crc16_tab/' test_af_crc16.c
run C4_index_no_xor af_rec.h 's/af_crc16_tab\[(uint8_t)((crc >> 8) ^ p\[i\])\]/af_crc16_tab[(uint8_t)(crc >> 8)]/' test_af_crc16.c
run BASE_ds af_ds.h '' test_af_ds.c -lm
run D1_bucket_floor af_ds.h 's/(t + p - 1) \/ p/t \/ p/' test_af_ds.c -lm
run D2_w_ignored af_ds.h 's/a->w += weight;/a->w += 1;/' test_af_ds.c -lm
run D3_min_not_updated af_ds.h 's/if (doserate < a->rate_min) a->rate_min = doserate;/;/' test_af_ds.c -lm
run D4_span_no_step af_ds.h 's/(a->ts_last - a->ts_first) + a->step_s/(a->ts_last - a->ts_first)/' test_af_ds.c -lm
run D5_cps_not_half af_ds.h 's/double cps_avg = pulses_avg \/ 2.0;/double cps_avg = pulses_avg;/' test_af_ds.c -lm
run D6_flags_last af_ds.h 's/a->flags_or |= flags;/a->flags_or = flags;/' test_af_ds.c -lm
run D7_nonfinite_accepted af_ds.h 's/if (!isfinite(dose_msv) || !isfinite(doserate) || !isfinite(pulses_2s)) return -1;//' test_af_ds.c -lm
run BASE_hq af_hist_query.h '' test_af_hist_query.c
run Q1_pts_min af_hist_query.h 's/(v < 2 || v > 2000)/(v < 1 || v > 2000)/' test_af_hist_query.c
run Q2_pts_max af_hist_query.h 's/(v < 2 || v > 2000)/(v < 2 || v > 2001)/' test_af_hist_query.c
run Q3_pts_not_stored af_hist_query.h 's/p->pts = v;/;/' test_af_hist_query.c
