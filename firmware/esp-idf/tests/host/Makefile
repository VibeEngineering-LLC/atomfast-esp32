# Host-тесты AtomFast Э3 (gcc, без ESP-IDF). make test -> "ALL PASSED" либо "CHECK(S) FAILED".
CC ?= gcc
CJSON_DIR ?= /opt/esp/idf/components/json/cJSON
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2 -I../../main -I.
BINS = test_af_hostchk test_af_fmtval test_af_rec test_af_ring test_af_window test_af_store_core test_af_persist test_af_env test_af_env_store test_af_sensor test_af_hist_query test_af_hist_core test_af_mqtt_ha test_af_mqtt_cfg test_af_sns_drv test_af_sns_reg test_af_env2 test_af_sens_req test_af_env3 test_af_ble_stats test_af_push test_af_log test_af_log_fmt test_af_clear test_af_log_x test_af_meas_util test_af_meas test_af_meas2 test_af_meas3 test_af_meas_req test_af_meas4 test_af_meas5 test_af_meas_http test_af_fsinfo test_af_hbuf test_af_etag test_af_crc16 test_af_ds test_af_mjrn test_ble_sel_cand test_ble_sel test_ble_sel2a1 test_ble_sel2a2 test_ble_sel2b test_ble_devsw test_af_csvname test_af_settings test_af_hist_hdr test_af_hostreq
BINS += test_af_history_codec test_af_http_body test_ota_session test_af_ble_http

test_af_ble_http: test_af_ble_http.c ../../main/af_ble_http.c ../../main/af_ble_snap.h ../../main/af_ble_stats.h
	$(CC) $(CFLAGS) -Ible_http_stub -I$(CJSON_DIR) -o $@ test_af_ble_http.c $(CJSON_DIR)/cJSON.c -lm

test_ota_session: test_ota_session.c ../../main/ota_session.h ../../main/ota_mark_valid_plan.h ota_stub/esp_ota_ops.h
	$(CC) $(CFLAGS) -Iota_stub -o $@ test_ota_session.c

test_af_history_codec: test_af_history_codec.c ../../main/af_history_codec.h ../../main/af_rec.h ../../main/af_env.h ../../main/af_hist_query.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_history_codec.c -lm

test_af_http_body: test_af_http_body.c ../../main/af_http_body.h ../../main/http_io_gate.c ../../main/http_io_gate.h http_stub/esp_http_server.h
	$(CC) $(CFLAGS) -Ihttp_stub -o $@ test_af_http_body.c ../../main/http_io_gate.c

test_af_rec: test_af_rec.c ../../main/af_rec.h ../../main/af_parse.h af_test.h real_samples.h
	$(CC) $(CFLAGS) -o $@ test_af_rec.c -lm
test_af_ring: test_af_ring.c ../../main/af_ring.h af_test.h real_samples.h
	$(CC) $(CFLAGS) -o $@ test_af_ring.c -lm
test_af_window: test_af_window.c ../../main/af_window.h ../../main/af_rec.h ../../main/af_parse.h af_test.h real_samples.h
	$(CC) $(CFLAGS) -o $@ test_af_window.c -lm
test_af_store_core: test_af_store_core.c ../../main/af_store_core.h ../../main/af_window.h ../../main/af_ring.h ../../main/af_rec.h af_test.h real_samples.h
	$(CC) $(CFLAGS) -o $@ test_af_store_core.c -lm
test_ble_sel_cand: test_ble_sel_cand.c ../../main/ble_sel_cand.h ../../main/ble_sel.h ../../main/ble_sel_json.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_ble_sel_cand.c
test_ble_sel: test_ble_sel.c ../../main/ble_sel_cand.h ../../main/ble_sel.h ../../main/ble_sel_cmd.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_ble_sel.c
test_ble_sel2a1: test_ble_sel2a1.c ../../main/ble_sel_cand.h ../../main/ble_sel.h ../../main/ble_sel_cmd.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_ble_sel2a1.c
test_ble_sel2a2: test_ble_sel2a2.c ../../main/ble_sel_cand.h ../../main/ble_sel.h ../../main/ble_sel_cmd.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_ble_sel2a2.c
test_ble_sel2b: test_ble_sel2b.c ../../main/ble_sel_cand.h ../../main/ble_sel.h ../../main/ble_sel_cmd.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_ble_sel2b.c
test_ble_devsw: test_ble_devsw.c ../../main/ble_devsw.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_ble_devsw.c
test-blesel: test_ble_sel_cand test_ble_sel test_ble_sel2a1 test_ble_sel2a2 test_ble_sel2b
	@rc=0; for t in $^; do ./$$t || rc=1; done; if [ $$rc -eq 0 ]; then echo "ALL PASSED"; else echo "CHECK(S) FAILED"; fi; exit $$rc
test-blesw: test_ble_devsw
	@if ./test_ble_devsw; then echo "ALL PASSED"; else echo "CHECK(S) FAILED"; exit 1; fi
test: $(BINS)
	@rc=0; for t in $(BINS); do ./$$t || rc=1; done; if [ $$rc -eq 0 ]; then echo "ALL PASSED"; else echo "CHECK(S) FAILED"; fi; exit $$rc
test-%: test_af_%
	@if ./test_af_$*; then echo "ALL PASSED"; else echo "CHECK(S) FAILED"; exit 1; fi
asan:
	$(CC) $(CFLAGS) -Ible_http_stub -I$(CJSON_DIR) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_ble_http test_af_ble_http.c $(CJSON_DIR)/cJSON.c -lm && /tmp/af_asan_ble_http
	$(CC) $(CFLAGS) -Iota_stub -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_ota_session test_ota_session.c && /tmp/af_asan_ota_session
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_history_codec test_af_history_codec.c -lm && /tmp/af_asan_history_codec
	$(CC) $(CFLAGS) -Ihttp_stub -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_http_body test_af_http_body.c ../../main/http_io_gate.c && /tmp/af_asan_http_body
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_rec test_af_rec.c -lm && /tmp/af_asan_rec
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_ring test_af_ring.c -lm && /tmp/af_asan_ring
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_win test_af_window.c -lm && /tmp/af_asan_win
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_core test_af_store_core.c -lm && /tmp/af_asan_core
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_persist test_af_persist.c -lm && /tmp/af_asan_persist
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_env test_af_env.c -lm && /tmp/af_asan_env
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_env_store test_af_env_store.c -lm && /tmp/af_asan_env_store
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_sensor test_af_sensor.c -lm && /tmp/af_asan_sensor
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_hq test_af_hist_query.c -lm && /tmp/af_asan_hq
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_hcore test_af_hist_core.c -lm && /tmp/af_asan_hcore
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_mqha test_af_mqtt_ha.c -lm && /tmp/af_asan_mqha
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_mqcfg test_af_mqtt_cfg.c -lm && /tmp/af_asan_mqcfg
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_sns_drv test_af_sns_drv.c -lm && /tmp/af_asan_sns_drv
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_sns_reg test_af_sns_reg.c -lm && /tmp/af_asan_sns_reg
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_env2 test_af_env2.c -lm && /tmp/af_asan_env2
	$(CC) $(CFLAGS) -I$(CJSON_DIR) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_sens_req test_af_sens_req.c $(CJSON_DIR)/cJSON.c -lm && /tmp/af_asan_sens_req
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_env3 test_af_env3.c -lm && /tmp/af_asan_env3
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_ble test_af_ble_stats.c -lm && /tmp/af_asan_ble
	$(CC) $(CFLAGS) -I$(CJSON_DIR) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_push test_af_push.c $(CJSON_DIR)/cJSON.c -lm && /tmp/af_asan_push
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_log test_af_log.c -lm && /tmp/af_asan_log
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_logfmt test_af_log_fmt.c -lm && /tmp/af_asan_logfmt
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_clear test_af_clear.c -lm && /tmp/af_asan_clear
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_logx test_af_log_x.c -lm && /tmp/af_asan_logx
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_mu test_af_meas_util.c -lm && /tmp/af_asan_mu
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_m1 test_af_meas.c -lm && /tmp/af_asan_m1
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_m2 test_af_meas2.c -lm && /tmp/af_asan_m2
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_m3 test_af_meas3.c -lm && /tmp/af_asan_m3
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_m4 test_af_meas4.c -lm && /tmp/af_asan_m4
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_m5 test_af_meas5.c -lm && /tmp/af_asan_m5
	$(CC) $(CFLAGS) -I$(CJSON_DIR) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_mreq test_af_meas_req.c $(CJSON_DIR)/cJSON.c -lm && /tmp/af_asan_mreq
	$(CC) $(CFLAGS) -Ihttp_stub -I$(CJSON_DIR) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_mhttp test_af_meas_http.c http_stub/http_stub.c ../../main/af_meas_http.c $(CJSON_DIR)/cJSON.c -lm && /tmp/af_asan_mhttp
	$(CC) $(CFLAGS) -Ipfs_stubs -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_fsinfo test_af_fsinfo.c ../../main/af_fsinfo.c && /tmp/af_asan_fsinfo
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_hbuf test_af_hbuf.c && /tmp/af_asan_hbuf
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_etag test_af_etag.c && /tmp/af_asan_etag
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_crc16 test_af_crc16.c && /tmp/af_asan_crc16
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_ds test_af_ds.c -lm && /tmp/af_asan_ds
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o /tmp/af_asan_mjrn test_af_mjrn.c -lm && /tmp/af_asan_mjrn
clean:
	rm -f $(BINS)
test_af_persist: test_af_persist.c ../../main/af_persist.h ../../main/af_rec.h ../../main/af_ring.h af_test.h real_hist.h
	$(CC) $(CFLAGS) -o $@ test_af_persist.c -lm
test_af_env: test_af_env.c ../../main/af_env.h ../../main/af_sensor.h ../../main/af_rec.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_env.c -lm
test_af_env_store: test_af_env_store.c ../../main/af_env.h ../../main/af_store_core.h ../../main/af_persist.h ../../main/af_ring.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_env_store.c -lm
test_af_sensor: test_af_sensor.c ../../main/af_sensor.h
	$(CC) $(CFLAGS) -o $@ test_af_sensor.c -lm
test_af_hist_query: test_af_hist_query.c ../../main/af_hist_query.h af_test.h data/e9_pre_raw.csv data/e9_pre_agg.csv
	$(CC) $(CFLAGS) -o $@ test_af_hist_query.c -lm
test_af_hist_core: test_af_hist_core.c ../../main/af_hist_core.h ../../main/af_hist_query.h ../../main/af_store_core.h af_test.h data/e9_pre_raw.csv data/e9_pre_agg.csv
	$(CC) $(CFLAGS) -o $@ test_af_hist_core.c -lm
test_af_mqtt_ha: test_af_mqtt_ha.c ../../main/af_mqtt_ha.h
	$(CC) $(CFLAGS) -o $@ test_af_mqtt_ha.c -lm
test_af_mqtt_cfg: test_af_mqtt_cfg.c ../../main/af_mqtt_cfg.h ../../main/af_mqtt_ha.h ../../main/af_sensor.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_mqtt_cfg.c -lm
test_af_sns_drv: test_af_sns_drv.c ../../main/af_sns_drv.h ../../main/af_sensor.h sns_vectors.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_sns_drv.c -lm
test_af_sns_reg: test_af_sns_reg.c ../../main/af_sns_reg.h ../../main/af_env.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_sns_reg.c -lm
test_af_env2: test_af_env2.c ../../main/af_env.h ../../main/af_sns_reg.h ../../main/af_sns_json.h ../../main/af_persist.h ../../main/af_store_core.h af_test.h data/e10_env_real.csv
	$(CC) $(CFLAGS) -o $@ test_af_env2.c -lm
test_af_sens_req: test_af_sens_req.c ../../main/af_sens_req.h ../../main/af_sns_reg.h af_test.h
	$(CC) $(CFLAGS) -I$(CJSON_DIR) -o $@ test_af_sens_req.c $(CJSON_DIR)/cJSON.c -lm
test_af_env3: test_af_env3.c ../../main/af_env.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_env3.c -lm
test_af_ble_stats: test_af_ble_stats.c ../../main/af_ble_stats.h ../../main/af_ble_snap.h ../../main/af_ble_nvs.h af_test.h data/e16_raw_ts.csv
	$(CC) $(CFLAGS) -o $@ test_af_ble_stats.c -lm
test_af_push: test_af_push.c ../../main/af_push.h push_t_common.inc push_t_cfg.inc push_t_logic.inc push_t_io.inc push_t_extra.inc af_test.h data/e8_d1_raw.csv data/e8_d1_agg.csv data/e8_d1_env.csv
	$(CC) $(CFLAGS) -I$(CJSON_DIR) -o $@ test_af_push.c $(CJSON_DIR)/cJSON.c -lm
test_af_log: test_af_log.c ../../main/af_log.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_log.c -lm
test_af_log_fmt: test_af_log_fmt.c ../../main/af_log.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_log_fmt.c -lm
test_af_clear: test_af_clear.c ../../main/af_clear.h ../../main/af_rec.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_clear.c -lm
test_af_log_x: test_af_log_x.c ../../main/af_log.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_log_x.c -lm
test_af_meas_util: test_af_meas_util.c ../../main/af_meas.h meas_t_common.inc af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_meas_util.c -lm
test_af_meas: test_af_meas.c ../../main/af_meas.h meas_t_common.inc af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_meas.c -lm
test_af_meas2: test_af_meas2.c ../../main/af_meas.h meas_t_common.inc af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_meas2.c -lm
test_af_meas3: test_af_meas3.c ../../main/af_meas.h meas_t_common.inc af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_meas3.c -lm
test_af_meas_req: test_af_meas_req.c ../../main/af_meas_req.h ../../main/af_meas.h af_test.h
	$(CC) $(CFLAGS) -I$(CJSON_DIR) -o $@ test_af_meas_req.c $(CJSON_DIR)/cJSON.c -lm
test_af_meas4: test_af_meas4.c ../../main/af_meas.h meas_t_common.inc af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_meas4.c -lm
test_af_meas5: test_af_meas5.c ../../main/af_meas.h meas_t_common.inc af_test.h data/e16s_raw_d1.csv
	$(CC) $(CFLAGS) -o $@ test_af_meas5.c -lm
test_af_meas_http: test_af_meas_http.c http_stub/http_stub.c http_stub/esp_http_server.h ../../main/af_meas_http.c ../../main/af_meas_req.h ../../main/af_meas.h af_test.h
	$(CC) $(CFLAGS) -Ihttp_stub -I$(CJSON_DIR) -o $@ test_af_meas_http.c http_stub/http_stub.c ../../main/af_meas_http.c $(CJSON_DIR)/cJSON.c -lm
BINS_MEAS_UNIT = test_af_meas_util test_af_meas test_af_meas2 test_af_meas3 test_af_meas4 test_af_meas_req
BINS_MEAS_INTEG = test_af_meas_http test_af_meas5
test-measunit: $(BINS_MEAS_UNIT)
	@rc=0; for t in $(BINS_MEAS_UNIT); do ./$$t || rc=1; done; if [ $$rc -eq 0 ]; then echo "ALL PASSED"; else echo "CHECK(S) FAILED"; fi; exit $$rc
test-measinteg: $(BINS_MEAS_INTEG)
	@rc=0; for t in $(BINS_MEAS_INTEG); do ./$$t || rc=1; done; if [ $$rc -eq 0 ]; then echo "ALL PASSED"; else echo "CHECK(S) FAILED"; fi; exit $$rc
test_af_fsinfo: test_af_fsinfo.c ../../main/af_fsinfo.c ../../main/af_fsinfo.h ../../main/ttl_plan.h pfs_stubs.h
	$(CC) $(CFLAGS) -Ipfs_stubs -o $@ test_af_fsinfo.c ../../main/af_fsinfo.c
test_af_hbuf: test_af_hbuf.c ../../main/af_hbuf.h
	$(CC) $(CFLAGS) -o $@ test_af_hbuf.c
test_af_etag: test_af_etag.c ../../main/af_etag.h
	$(CC) $(CFLAGS) -o $@ test_af_etag.c
test_af_crc16: test_af_crc16.c ../../main/af_rec.h ../../main/af_crc16_tab.h
	$(CC) $(CFLAGS) -o $@ test_af_crc16.c
test_af_ds: test_af_ds.c ../../main/af_ds.h
	$(CC) $(CFLAGS) -o $@ test_af_ds.c -lm
test_af_mjrn: test_af_mjrn.c ../../main/af_mjrn.h ../../main/af_mjadd.h ../../main/af_meas.h meas_t_common.inc af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_mjrn.c -lm
test_af_fmtval: test_af_fmtval.c ../../main/af_fmtval.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_fmtval.c -lm

test_af_hostchk: test_af_hostchk.c ../../main/af_hostchk.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_hostchk.c -lm

test_af_csvname: test_af_csvname.c ../../main/af_csvname.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_csvname.c
test_af_settings: test_af_settings.c ../../main/af_settings.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_settings.c
test_af_hist_hdr: test_af_hist_hdr.c ../../main/af_history_hdr.h ../../main/af_hist_query.h http_stub/esp_http_server.h af_test.h
	@! grep -n 'httpd_resp_set_hdr(req, "X-AF' ../../main/af_history_ds.c ../../main/af_history_http.c || { echo "handler sets headers directly, bypassing af_hist_resp_hdrs"; exit 1; }
	$(CC) $(CFLAGS) -Ihttp_stub -o $@ test_af_hist_hdr.c
test_af_hostreq: test_af_hostreq.c ../../main/af_hostreq.h ../../main/af_hostchk.h af_test.h
	$(CC) $(CFLAGS) -o $@ test_af_hostreq.c
