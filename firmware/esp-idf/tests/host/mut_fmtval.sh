#!/bin/bash
# T-55 #SA-3: мутации af_fmtval.h; запуск в контейнере IDF из tests/host
cd "$(dirname "$0")"; M=../../main; mkdir -p /tmp/mf && cp $M/*.h /tmp/mf/
build() { gcc -std=c11 -w -O2 -I/tmp/mf -I. -o /tmp/mf/t test_af_fmtval.c -lm 2>&1 | head -3; /tmp/mf/t | grep -E '^FAIL' | sed 's/ test_af.*//' | sort -u | tr '\n' ';'; }
run() { cp $M/af_fmtval.h /tmp/mf/af_fmtval.h; sed -i "$2" /tmp/mf/af_fmtval.h; cmp -s $M/af_fmtval.h /tmp/mf/af_fmtval.h && { echo "$1: MUTATION NOT APPLIED"; return; }; r=$(build); echo "$1: ${r:-SURVIVED}"; }
echo "base: $(build)"
run F1_no_redec 's|if (rounded > 0.0) dec = af_dec_for(rounded);||'
run F2_eps_2dig 's|"%.1f", eps_pct|"%.2f", eps_pct|'
run F3_round_trunc 's|floor(x \* p + 0.5) / p|floor(x * p) / p|'
run F4_sig_always1 's|int sig = (first == 1 \|\| first == 2) ? 2 : 1;|int sig = 1;|'
run F5_abs_no_fabs 's|double abs = fabs(value) \* eps_pct|double abs = value * eps_pct|'
run F6_exp_thr 's|fabs(value) < 1e-4|fabs(value) < 1e-5|'
run F7_zero_val 's|if (value == 0.0) {|if (0) {|'
run F8_eps_neg 's|eps_pct < 0.0) {|eps_pct < -1e9) {|'
run F9_dec_clamp 's|if (dec > 12) dec = 12;|if (dec > 3) dec = 3;|'
