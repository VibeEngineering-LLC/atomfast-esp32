#!/bin/bash
# T-57 #SA-3: мутации af_hostchk.h; запуск в контейнере IDF из tests/host
cd "$(dirname "$0")"; M=../../main; mkdir -p /tmp/mh && cp $M/*.h /tmp/mh/
build() { gcc -std=c11 -w -O2 -I/tmp/mh -I. -o /tmp/mh/t test_af_hostchk.c -lm 2>&1 | head -3; /tmp/mh/t | grep -E '^FAIL' | sed 's/ test_af.*//' | sort -u | tr '\n' ';'; }
run() { cp $M/af_hostchk.h /tmp/mh/af_hostchk.h; sed -i "$2" /tmp/mh/af_hostchk.h; cmp -s $M/af_hostchk.h /tmp/mh/af_hostchk.h && { echo "$1: MUTATION NOT APPLIED"; return; }; r=$(build); echo "$1: ${r:-SURVIVED}"; }
echo "base: $(build)"
run H1_no_dot 's|if (!has_dot)|if (0)|'
run H2_digits 's|if (digits_dots)|if (0)|'
run H3_local 's|host\[n - 6\] == .\.. \&\&|1 \&\&|'
run H4_origin_eq 's|return strcmp(origin + 7, host) == 0;|return 1;|'
run H5_scheme 's|strncmp(origin, "http://", 7) != 0|0|'
run H6_name_gate 's|if (!af_host_name_ok(host))|if (0)|'
run H7_len6 's|n > 6 \&\& n >= 6|n >= 6|'
