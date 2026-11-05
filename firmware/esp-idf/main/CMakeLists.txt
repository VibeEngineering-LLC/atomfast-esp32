# REQUIRES намеренно НЕ задаётся - компонент main по умолчанию зависит от всех компонентов сборки; явный список обрезал бы зависимости Bluedroid и ломал линковку (undefined reference к esp_ble_gattc_open).

idf_component_register(
    SRCS "main.c" "af_auth.c" "ble_gw.c" "af_ble_http.c" "wifi_manager.c" "web_server.c" "boot_config.c" "net_time.c" "http_io_gate.c" "debug_log_ring.c" "ota_github_client.c" "ota_busy.c" "task_load_sampler.c" "af_store.c" "af_history_http.c" "af_persist_fs.c" "af_env_drv.c" "af_mqtt_drv.c" "af_log.c" "af_log_http.c"
    "af_push.c" "af_meas.c" "af_meas_http.c" "af_fsinfo.c" "af_history_ds.c" "af_settings_http.c" "af_ble_sel_http.c" "af_meas_jrn_http.c"
    INCLUDE_DIRS "."
    EMBED_FILES "${PROJECT_DIR}/web/setup.html" "${PROJECT_DIR}/web/captive.html" "${PROJECT_DIR}/web/common-time.js.gz" "${PROJECT_DIR}/web/history.html.gz"
)

# порог 3584 остаётся историческим гейтом против больших структур на стеке
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=frame-larger-than=3584)
# e15: драйвер списка датчиков - по размеру (проект собирается с -Og; бюджет добавки флеша 12 КБ)
set_source_files_properties("af_env_drv.c" PROPERTIES COMPILE_OPTIONS "-Os")

# Э8: отправка истории - по размеру (флеш; -Os: text 16.3 КБ против 18.4 КБ при -Og)
set_source_files_properties("af_push.c" PROPERTIES COMPILE_OPTIONS "-Os")
# замеры (серии/разовое/непрерывное): по размеру (флеш)
set_source_files_properties("af_meas.c" "af_meas_http.c" "af_meas_jrn_http.c" PROPERTIES COMPILE_OPTIONS "-Os")
# e16g (T-48): веб без пароля, AF_AUTH_PASS не требуется и в образ не попадает.
