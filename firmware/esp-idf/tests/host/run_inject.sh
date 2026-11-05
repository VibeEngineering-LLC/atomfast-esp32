#!/usr/bin/env bash
# Сценарии инжекции ошибок (e8-fixes). usage: run_inject.sh [MAIN=каталог main] ; печатает строку INJ по каждому сценарию.
set -u
cd "$(dirname "$0")"
SC="p1_fopen_wb p2_fopen p2_fread p2_ftell p2_transient p3_epoch p3_nodata p4_malloc p4_opendir p7_fclose p10_fopen p10_fread p11_sem p13_status_len p14_valid_mismatch"
bash build_pfs_sim.sh 300 /tmp/pfs_inj >/tmp/pfs_inj.build 2>&1 || { tail -20 /tmp/pfs_inj.build; exit 9; }
for s in $SC; do timeout -s KILL 120 /tmp/pfs_inj inject $s 2>&1 | grep -E "^(INJ|INFO)" || echo "INJ $s NORESULT"; done
