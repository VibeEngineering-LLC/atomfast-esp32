#!/usr/bin/env bash
# Мутации боевой обвязки af_persist_fs.c под стендом pfs_sim (#SA-3): базовая сборка зелёная, каждый мутант обязан покраснеть.
# e10: шаблоны G2/G6 обновлены под код после правок e8 (были устаревшими - SED DID NOT APPLY), добавлены G9-G11 для вида env.
# usage: mut_pfs.sh <trials>
set -u
cd "$(dirname "$0")"
TR=${1:-120}
run() { # метка, sed
  if SEDMUT="$2" bash build_pfs_sim.sh 300 /tmp/pfs_mut >/tmp/pfs_mut.build 2>&1; then
    r=$(timeout -s KILL 100 /tmp/pfs_mut crash $TR 7 2>&1 | grep CRASH)
    [ -z "$r" ] && { echo "$1 | RED: no result within 100 s (hang/abort)"; return; }
    echo "$1 | $(echo "$r" | grep -o 'trials=[0-9]* crashed=[0-9]* abnormal=[0-9]* mount_fail=[0-9]* viol_loss=[0-9]* viol_future=[0-9]* viol_content=[0-9]* viol_cont=[0-9]* viol_agg=[0-9]* viol_env=[0-9]*')"
  else echo "$1 | BUILD/APPLY FAILED: $(tail -1 /tmp/pfs_mut.build)"; fi
}
run BASE ''
run G1_persisted_overclaim 's/k->persisted_seq += done;/k->persisted_seq += done + 1;/'
run G2_no_ring_finish 's/af_persist_ring_finish(ring, p.end_seq, p.epoch > boot_epoch ? p.epoch : boot_epoch);/(void)boot_epoch;/'
run G3_hdr_first_seq_off 's/.first_seq = k->persisted_seq,/.first_seq = k->persisted_seq + 1,/'
run G4_short_write 's/fwrite(s_chunk, k->rec_size, got, f) != got/fwrite(s_chunk, k->rec_size, got > 1 ? got - 1 : got, f) != (got > 1 ? got - 1 : got)/'
run G5_no_skip_on_restore 's/uint32_t skip = (pos == p.load_from) ? p.skip_recs : 0;/uint32_t skip = 0;/'
run G6_agg_not_restored 's/^    int rc1 = restore_kind(1, boot_epoch);/    int rc1 = 0;/'
run G7_agg_not_flushed 's/^    flush_kind(1);//'
run G8_tick_never_flushes 's/if (af_flush_due(now_s, s_last_flush_s, AF_FLUSH_INTERVAL_S, pending)) {/if (0) {/'
run G9_env_not_restored 's/^    int rc2 = restore_kind(2, boot_epoch);/    int rc2 = 0;/'
run G10_env_not_flushed 's/^    flush_kind(2);//'
run G11_env_hdr_kind 's/.kind = k->kind,/.kind = AF_SEG_KIND_RAW,/'
