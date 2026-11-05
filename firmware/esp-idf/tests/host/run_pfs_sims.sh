#!/usr/bin/env bash
# Прогон стенда pfs_sim: сбой питания (3 зерна x 400) и износ (5 периодов сброса). Логи -> каталог $1.
set -euo pipefail
cd "$(dirname "$0")"
L=$1; mkdir -p "$L"
for n in 60 120 300 600 900; do bash build_pfs_sim.sh $n /tmp/pfs_sim_$n 2>&1 | tail -1; done
: > "$L/crash_sim.log"
for s in 1 2 3; do /tmp/pfs_sim_300 crash 400 $s 2>&1 | grep CRASH >> "$L/crash_sim.log"; done
: > "$L/wear_sim.log"
for n in 60 120 300 600 900; do /tmp/pfs_sim_$n wear 5 3 2>&1 | grep WEAR >> "$L/wear_sim.log"; done
echo DONE >> "$L/wear_sim.log"
