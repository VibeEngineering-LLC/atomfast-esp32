#!/usr/bin/env bash
# Мутации исправлений e8 (#SA-3): мутант возвращает дефект, красным обязан стать ровно перечисленный сценарий.
# usage: mut_e8.sh ; строки результата: метка | red=[..] expected=[..] | CAUGHT/MISMATCH
set -u
cd "$(dirname "$0")"
PS="p1_fopen_wb p2_fopen p2_fread p2_ftell p2_transient p3_epoch p3_nodata p4_malloc p4_opendir p7_fclose p10_fopen p10_fread p11_sem p13_status_len p14_valid_mismatch"
SS="p9_taskfail p9b_hook_guard p13_env_submit p11_drain p12_timeout"
verdict() { [ "$(echo $2)" = "$3" ] && v=CAUGHT || v=MISMATCH; echo "$1 | red=[$(echo $2)] expected=[$3] | $v"; }
runp() { SEDMUT="$3" bash build_pfs_sim.sh 300 /tmp/pfs_mut >/tmp/pfs_mut.b 2>&1 || { echo "$1 | BUILD/APPLY FAILED: $(tail -1 /tmp/pfs_mut.b)"; return; }
  red=""; for s in $PS; do timeout -s KILL 120 /tmp/pfs_mut inject $s >/dev/null 2>&1 || red="$red $s"; done; verdict "$1" "$red" "$2"; }
runs() { SEDMUT="$3" bash build_store_sim.sh /tmp/store_mut >/tmp/store_mut.b 2>&1 || { echo "$1 | BUILD/APPLY FAILED: $(tail -1 /tmp/store_mut.b)"; return; }
  red=""; for s in $SS; do /tmp/store_mut $s >/dev/null 2>&1 || red="$red $s"; done; verdict "$1" "$red" "$2"; }
runp BASE '' ''
runp M1_next_idx_early p1_fopen_wb 's/uint32_t idx = k->next_idx;/uint32_t idx = k->next_idx++;/'
runp M2_no_read_retry p2_transient 's/t < RD_TRIES/t < 1/'
runp M3_io_err_unlinked 'p2_fopen p2_fread p2_ftell' 's/int corrupt = !e->hdr_ok && !e->io_err;/int corrupt = !e->hdr_ok;/'
runp M4_stale_unlinked_on_io 'p2_fopen p2_fread p2_ftell' 's/ && !io_err_any;/;/'
runp M5_ftell_unchecked p2_ftell 's/if (sz < 0 || fseek(f, 0, SEEK_SET) != 0)/if (fseek(f, 0, SEEK_SET) != 0)/'
runp M6_epoch_not_stored 'p3_epoch p3_nodata' 's/^    epoch_store(.*$/    (void)boot_epoch;/'
runp M7_epoch_no_plus1 'p3_epoch p3_nodata' 's/uint32_t boot_epoch = epoch_load() + 1;/uint32_t boot_epoch = epoch_load();/'
runp M8_restore_not_disabled 'p4_malloc p4_opendir' 's/k->disabled = 1;//g'
runp M9_restore_rc_ok 'p4_malloc p4_opendir' 's/return (rc0 == 0 && rc1 == 0 && rc2 == 0) ? ESP_OK : ESP_FAIL;/return ESP_OK;/'
runp M10_hdr_close_ignored p7_fclose 's/if (hw != 32 || hc != 0) {/if (hw != 32) {/'
runp M11_ring_not_reset 'p10_fopen p10_fread p14_valid_mismatch' 's/af_ring_clear(ring);/(void)ring;/'
runp M12_flush_mtx_silent p11_sem 's/s_flush_errs++; \/\* e8 .*11.*/(void)0;/'
runs SBASE '' ''
runs S1_hook_not_unregistered p9_taskfail '/esp_unregister_shutdown_handler(store_shutdown);/d'
runs S2_no_drain p11_drain 's/while (xQueueReceive(s_q, &it, 0) == pdTRUE) {/while (0) {/'
runs S3_cursor_reset p12_timeout 's/if (next) \*next = since;/if (next) *next = 0;/'
runs S4_timeouts_uncounted p12_timeout '/__atomic_fetch_add(&s_read_timeouts/d'
runp M13_valid_recheck_removed p14_valid_mismatch 's/ || (sc.valid != s_info\[pos\].valid)//'
runs S5_ready_guard_removed p9b_hook_guard 's/return; \/\* e8 .*9: .*/(void)0;/'
runs S6_env_not_pushed p13_env_submit 's/int rc = af_core_env_push(&s_core, r);/int rc = 0;/'
runs S7_env_ready_unchecked p13_env_submit 's/if (!s_ready || r == NULL || !s_env_buf) {/if (r == NULL) {/'
