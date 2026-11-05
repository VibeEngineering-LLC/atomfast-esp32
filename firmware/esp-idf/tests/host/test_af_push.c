/* Э8: host-тесты af_push.h. Сгенерировано gen_e8/assemble_test.sh из push_t_*.inc (тела тестов - gen_code.py). */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "af_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "af_push.h"
#include "push_t_common.inc"
#include "push_t_cfg.inc"
#include "push_t_logic.inc"
#include "push_t_io.inc"
#include "push_t_extra.inc"
int main(void) {
    RUN(t_cfg_default);
    RUN(t_host_ok_good);
    RUN(t_host_ok_bad);
    RUN(t_token_ok);
    RUN(t_addr_class);
    RUN(t_cfg_valid_bounds);
    RUN(t_cfg_valid_garbage);
    RUN(t_cfg_complete);
    RUN(t_parse_full);
    RUN(t_parse_patch);
    RUN(t_parse_clear_token);
    RUN(t_parse_reject_body);
    RUN(t_parse_reject_json);
    RUN(t_parse_reject_keys);
    RUN(t_parse_reject_values);
    RUN(t_parse_incomplete);
    RUN(t_parse_dest_changed);
    RUN(t_parse_keeps_cur);
    RUN(t_cfg_blob_roundtrip);
    RUN(t_cfg_blob_layout);
    RUN(t_cfg_blob_corrupt);
    RUN(t_cfg_encode_args);
    RUN(t_plan_fresh);
    RUN(t_plan_continue);
    RUN(t_plan_batch_cap);
    RUN(t_plan_caught_up);
    RUN(t_plan_evicted);
    RUN(t_plan_epoch);
    RUN(t_plan_rollback);
    RUN(t_plan_wrap);
    RUN(t_plan_null);
    RUN(t_curs_roundtrip);
    RUN(t_curs_layout);
    RUN(t_curs_corrupt);
    RUN(t_curs_due);
    RUN(t_resp_parse);
    RUN(t_result_200);
    RUN(t_result_clears_err);
    RUN(t_result_200_empty_receiver);
    RUN(t_result_200_bad);
    RUN(t_result_idempotent);
    RUN(t_result_401);
    RUN(t_result_400);
    RUN(t_result_413);
    RUN(t_result_5xx);
    RUN(t_result_transport);
    RUN(t_result_other);
    RUN(t_result_growth);
    RUN(t_backoff);
    RUN(t_pause);
    RUN(t_bad_state);
    RUN(t_err_names);
    RUN(t_ring_helpers);
    RUN(t_pack_raw_d1);
    RUN(t_pack_agg_d1);
    RUN(t_pack_env);
    RUN(t_pack_limits);
    RUN(t_pack_resume);
    RUN(t_pack_lost);
    RUN(t_pack_skip_bad);
    RUN(t_pack_args);
    RUN(t_pack_empty);
    RUN(t_pack_busy);
    RUN(t_test_body);
    RUN(t_kind_names_paths);
    RUN(t_json_basic);
    RUN(t_json_state);
    RUN(t_json_no_token);
    RUN(t_json_trunc);
    RUN(t_json_lag);
    RUN(t_json_no_cursor);
    RUN(t_send_ok_raw);
    RUN(t_send_retry_5xx);
    RUN(t_send_transport);
    RUN(t_send_401);
    RUN(t_send_400_stops_ring);
    RUN(t_send_413_shrinks);
    RUN(t_send_no_mac);
    RUN(t_send_busy);
    RUN(t_send_empty);
    RUN(t_send_skip_bad);
    RUN(t_send_resend_after_reset);
    RUN(t_send_epoch_change);
    RUN(t_send_evicted_lost);
    RUN(t_send_args);
    RUN(t_send_agg_env);
    RUN(t_send_proto_stop);
    RUN(t_send_read_timeout);
    RUN(t_send_batch_clamp);
    RUN(t_send_first_seq_skips);
    RUN(t_json_size_max);
    RUN(t_json_public_name);
    RUN(t_send_stale_epoch_ring);
    RUN(t_test_request_has_header);
    RUN(t_test_response_marker);
    RUN(t_json_legacy);
    TESTS_DONE();
}
