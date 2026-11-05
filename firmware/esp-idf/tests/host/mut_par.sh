#!/usr/bin/env bash
# Параллельный прогон таблиц мутантов замеров: каждая порция - в своей копии дерева (/tmp/pN), исходное дерево не мутируется.
# usage (внутри контейнера): mut_par.sh <каталог-вывода>
set -u
OUT=$1; mkdir -p "$OUT"
cd /project/tests/host
split -l 9 -d meas.mut /tmp/chunk_m_
i=0
for c in /tmp/chunk_m_*; do
  i=$((i+1)); P=/tmp/p$i; mkdir -p $P; cp -r /project/main $P/main; mkdir -p $P/tests; cp -r /project/tests/host $P/tests/host
  cp "$c" $P/tests/host/chunk.mut
  (cd $P/tests/host && MT_TARGET=test-measunit bash mutate.sh chunk.mut ../../main/af_meas.h 2>&1 | grep -E "^==|VERDICT" > "$OUT/m$i.log") &
done
P=/tmp/p10; mkdir -p $P; cp -r /project/main $P/main; mkdir -p $P/tests; cp -r /project/tests/host $P/tests/host
(cd $P/tests/host && MT_TARGET=test-measunit bash mutate.sh meas_req.mut ../../main/af_meas_req.h 2>&1 | grep -E "^==|VERDICT" > "$OUT/req.log") &
P=/tmp/p11; mkdir -p $P; cp -r /project/main $P/main; mkdir -p $P/tests; cp -r /project/tests/host $P/tests/host
(cd $P/tests/host && MT_TARGET=test-measinteg bash mutate.sh meas_http.mut ../../main/af_meas_http.c 2>&1 | grep -E "^==|VERDICT" > "$OUT/http.log") &
wait
echo ALLDONE
