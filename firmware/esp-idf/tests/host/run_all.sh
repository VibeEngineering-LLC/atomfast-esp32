#!/usr/bin/env bash
# Полный прогон Э3: тесты, ASan/UBSan, мутации, боевая мера #SA-3a. Логи -> каталог $1.
set -u
cd "$(dirname "$0")"
L=$1; mkdir -p "$L"
(make clean >/dev/null; make test) > "$L/make-test.log" 2>&1
make asan > "$L/asan.log" 2>&1
MT_TARGET=test-rec bash mutate.sh rec.mut ../../main/af_rec.h > "$L/mut-rec.log" 2>&1
MT_TARGET=test-ring bash mutate.sh ring.mut ../../main/af_ring.h > "$L/mut-ring.log" 2>&1
MT_TARGET=test-ring bash mutate.sh ring_multi.mut ../../main/af_ring.h > "$L/mut-ring-multi.log" 2>&1
MT_TARGET=test-window bash mutate.sh win.mut ../../main/af_window.h > "$L/mut-win.log" 2>&1
MT_TARGET=test-window bash mutate.sh win_coarse.mut ../../main/af_window.h > "$L/mut-win-coarse.log" 2>&1
bash measure_real.sh rec.mut af_rec.h > "$L/real-rec.log" 2>&1
bash measure_real.sh win.mut af_window.h > "$L/real-win.log" 2>&1
echo finished
