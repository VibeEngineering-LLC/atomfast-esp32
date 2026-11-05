#!/usr/bin/env bash
# Мутации однопроходного восстановления (e16e, #SA-3): мутант красный, если стенд crash показал нарушения ИЛИ любой сценарий inject RED.
cd "$(dirname "$0")"
SC="p2_fopen p2_fread p2_transient p3_epoch p10_fopen p10_fread p14_valid_mismatch"
run() {
  if SEDMUT="$2" bash build_pfs_sim.sh 300 /tmp/pfs_m >/tmp/pfs_m.build 2>&1; then
    c=$(timeout -s KILL 100 /tmp/pfs_m crash 120 7 2>&1 | grep CRASH | grep -c -E "viol_[a-z]*=[1-9]|mount_fail=[1-9]|abnormal=[1-9]")
    r=0; for s in $SC; do timeout -s KILL 100 /tmp/pfs_m inject $s 2>&1 | grep -q "RED" && r=$((r+1)); done
    [ "$c" -gt 0 -o "$r" -gt 0 ] && echo "$1 | RED crash_viol=$c inject_red=$r" || echo "$1 | GREEN (МУТАНТ ЖИВ)"
  else echo "$1 | BUILD/APPLY FAILED: $(tail -1 /tmp/pfs_m.build)"; fi
}
run O0_BASE ''
run O1_no_gap_clear 's/if (run_open && (hole || sc.hdr.first_seq != run_end)) {/if (0) {/'
run O2_hole_ignored 's/if (run_open && (hole || /if (run_open \&\& (0 || /'
run O3_run_end_off_by_one 's/run_end = sc.hdr.first_seq + sc.valid;/run_end = sc.hdr.first_seq + sc.valid + 1;/'
run O4_push_one_short 's/for (uint32_t j = 0; j < sc.valid; j++) {/for (uint32_t j = 0; j + 1 < sc.valid; j++) {/'
run O5_no_sort 's/while (j > 0 \&\& s_info\[j - 1\].seg_idx > key.seg_idx) {/while (0) {/'
run O6_no_finish 's/af_persist_ring_finish(ring, p.end_seq, p.epoch > boot_epoch ? p.epoch : boot_epoch);/(void)boot_epoch;/'
run O7_wrong_hdr_idx 's/sc.hdr.seg_idx == idx);/1);/'
