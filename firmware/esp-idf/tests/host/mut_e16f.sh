#!/bin/bash
# e16f #SA-3: мутации af_mjrn.h; запуск в контейнере IDF из tests/host: bash mut_e16f.sh
cd "$(dirname "$0")"; M=../../main; mkdir -p /tmp/mj && cp $M/*.h $M/af_meas.c /tmp/mj/ 2>/dev/null
build() { gcc -std=c11 -Wall -Wextra -Werror -O2 -I/tmp/mj -I. -o /tmp/mj/t test_af_mjrn.c -lm 2>&1 | head -3; /tmp/mj/t | grep -E '^FAIL' | sed 's/ test_af.*//' | tr '\n' ';'; }
run() { cp $M/af_mjrn.h /tmp/mj/af_mjrn.h; sed -i "$2" /tmp/mj/af_mjrn.h; cmp -s $M/af_mjrn.h /tmp/mj/af_mjrn.h && { echo "$1: MUTATION NOT APPLIED"; return; }; r=$(build); echo "$1: ${r:-SURVIVED}"; }
echo "base: $(build)"
run M1_gap_no_minus1 's|(gap_ms + 1000u) / 2000u) - 1u|(gap_ms + 1000u) / 2000u)|'
run M2_upper_incl 's|ts >= a->to|ts > a->to|'
run M3_lower_excl 's|ts < a->from |ts <= a->from |'
run M4_tail_off 's|tail_ms > AF_MEAS_GAP_MS|tail_ms > 100000000000ull|'
run M5_sat_off 's|a->counts > 0xFFFFFFFFu - pulses|0|'
run M6_head_gap_off 's|a->last_ts = from;|a->last_ts = 0;|'
run M7_rate_zero 's|a->rate_sum += (double)rate_usvh;|a->rate_sum += 0.0;|'
run M8_backwards_ok 's/ || ts < a->last_ts//'
run M9_gap_count_off 's|a->gaps++;||'
run M10_lost_tail_plus 's|lost += (uint32_t)((tail_ms + 1000u)|lost += 1 + (uint32_t)((tail_ms + 1000u)|'
run M11_maxlen_off 's|(a->to - a->from) > AF_MJRN_MAX_S|(a->to - a->from) > 0xFFFFFFF0u|'
run M12_npk_clamp_off 's|a->npk > 65535u|a->npk > 6553500u|'
