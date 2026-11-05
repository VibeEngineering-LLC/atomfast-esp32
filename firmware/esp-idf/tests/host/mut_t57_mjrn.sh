#!/bin/bash
# T-57 #SA-3: мутации af_mjrn.h (valid по допуску потерь); запуск в контейнере IDF из tests/host
cd "$(dirname "$0")"; M=../../main; mkdir -p /tmp/mj && cp $M/*.h /tmp/mj/
build() { gcc -std=c11 -w -O2 -I/tmp/mj -I. -o /tmp/mj/t test_af_mjrn.c -lm 2>&1 | head -3; /tmp/mj/t | grep -E '^FAIL' | sed 's/ test_af.*//' | sort -u | tr '\n' ';'; }
run() { cp $M/af_mjrn.h /tmp/mj/af_mjrn.h; sed -i "$2" /tmp/mj/af_mjrn.h; cmp -s $M/af_mjrn.h /tmp/mj/af_mjrn.h && { echo "$1: MUTATION NOT APPLIED"; return; }; r=$(build); echo "$1: ${r:-SURVIVED}"; }
echo "base: $(build)"
run J1_always_valid 's|out->valid = (a->npk >= 1 \&\& lost <= a->lost_max) ? 1 : 0;|out->valid = 1;|'
run J2_lt 's|lost <= a->lost_max|lost < a->lost_max|'
run J3_default 's|a->lost_max = 2;|a->lost_max = 9;|'
run J4_reason 's|out->valid ? AF_MEAS_SR_TIME : AF_MEAS_SR_ERROR|AF_MEAS_SR_TIME|'
