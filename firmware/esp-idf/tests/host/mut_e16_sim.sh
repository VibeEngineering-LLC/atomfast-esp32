#!/usr/bin/env bash
# e16: мутации #SA-3 боевых af_persist_fs.c / af_log.c под стендом pfs_sim. Таблица e16_sim.mut: метка|файл|sed|группа(clear|log).
# База обязана быть зелёной; каждый мутант обязан покраснеть (печатается список красных сценариев).
set -u
cd "$(dirname "$0")"
CLR="c1_raw c1_agg c1_env c1_all c2_twice c3_empty c4_marker_fail c5_hdr_fail c6_crash_raw c6_crash_all c7_stale_marker c8_garbage_marker c9_short_marker c10_cycles c11_interleave"
LOG="l1_roundtrip l2_rotation l3_reserve_low l3_reserve_edge l4_write_fail l5_flood l6_urgent l7_clear l8_torn_line l9_filters l11_alloc_fail"
run() { # метка файл sed группа
  local list=$CLR; [ "$4" = log ] && list=$LOG
  if ! MUTFILE="$2" SEDMUT="$3" bash build_pfs_sim.sh 300 /tmp/pfs_mut >/tmp/pfs_mut.build 2>&1; then echo "$1 | BUILD/APPLY FAILED: $(tail -1 /tmp/pfs_mut.build)"; return; fi
  local red="" s r
  for s in $list; do
    r=$(timeout -s KILL 100 /tmp/pfs_mut $4 $s 2>&1 | grep -E '^(CLR|LOG) ' | grep -c RED)
    [ "$r" != "0" ] && red="$red $s"
  done
  echo "$1 | red:${red:- NONE (мутант выжил)}"
}
run BASE_clear af_persist_fs.c '' clear
run BASE_log af_log.c '' log
while IFS='|' read -r label file expr grp; do
  [ -z "$label" ] && continue
  run "$label" "$file" "$expr" "$grp"
done < "${1:-e16_sim.mut}"
