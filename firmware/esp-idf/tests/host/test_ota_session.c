#include <string.h>
#include "af_test.h"
#include "../../main/ota_session.h"
#include "../../main/ota_mark_valid_plan.h"

static int begin_result, write_result, end_result, abort_result;
static unsigned begin_calls, write_calls, end_calls, abort_calls;
static esp_ota_handle_t live_handle;
static esp_partition_t partition = { .size = 4096 };

static void reset_sdk(void)
{
    begin_result = write_result = end_result = abort_result = ESP_OK;
    begin_calls = write_calls = end_calls = abort_calls = 0;
    live_handle = 0;
}

esp_err_t esp_ota_begin(const esp_partition_t *p, size_t size, esp_ota_handle_t *handle)
{
    CHECK(p == &partition);
    CHECK(size == OTA_SIZE_UNKNOWN);
    CHECK(live_handle == 0);
    begin_calls++;
    if (begin_result == ESP_OK) {
        live_handle = 100 + begin_calls;
        *handle = live_handle;
    }
    return begin_result;
}

esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size)
{
    CHECK(handle == live_handle && handle != 0);
    CHECK(size == 3 && memcmp(data, "abc", 3) == 0);
    write_calls++;
    return write_result;
}

esp_err_t esp_ota_abort(esp_ota_handle_t handle)
{
    CHECK(handle == live_handle && handle != 0);
    abort_calls++;
    live_handle = 0;
    return abort_result;
}

esp_err_t esp_ota_end(esp_ota_handle_t handle)
{
    CHECK(handle == live_handle && handle != 0);
    end_calls++;
    live_handle = 0;
    return end_result;
}

static void failed_begin_has_no_cleanup_handle(void)
{
    reset_sdk();
    ota_session_t s = {0};
    begin_result = ESP_FAIL;
    CHECK(ota_session_begin(&s, &partition, OTA_SIZE_UNKNOWN) == ESP_FAIL);
    CHECK(!s.active && s.handle == 0);
    CHECK(ota_session_abort(&s) == ESP_OK);
    CHECK(abort_calls == 0);
    CHECK(ota_session_write(&s, "abc", 3) == ESP_ERR_INVALID_STATE);
    CHECK(ota_session_end(&s) == ESP_ERR_INVALID_STATE);
    CHECK(write_calls == 0 && end_calls == 0);
}

static void failed_write_aborts_once(void)
{
    reset_sdk();
    ota_session_t s = {0};
    CHECK(ota_session_begin(&s, &partition, OTA_SIZE_UNKNOWN) == ESP_OK);
    CHECK(ota_session_begin(&s, &partition, OTA_SIZE_UNKNOWN) == ESP_ERR_INVALID_STATE);
    CHECK(begin_calls == 1);
    write_result = ESP_FAIL;
    CHECK(ota_session_write(&s, "abc", 3) == ESP_FAIL);
    CHECK(s.active);
    CHECK(ota_session_abort(&s) == ESP_OK);
    CHECK(ota_session_abort(&s) == ESP_OK);
    CHECK(abort_calls == 1 && !s.active && s.handle == 0);
}

static void end_consumes_handle_on_success_and_failure(void)
{
    for (int fail = 0; fail < 2; fail++) {
        reset_sdk();
        ota_session_t s = {0};
        CHECK(ota_session_begin(&s, &partition, OTA_SIZE_UNKNOWN) == ESP_OK);
        CHECK(ota_session_write(&s, "abc", 3) == ESP_OK);
        end_result = fail ? ESP_FAIL : ESP_OK;
        CHECK(ota_session_end(&s) == end_result);
        CHECK(!s.active && s.handle == 0);
        CHECK(ota_session_abort(&s) == ESP_OK);
        CHECK(ota_session_end(&s) == ESP_ERR_INVALID_STATE);
        CHECK(end_calls == 1 && abort_calls == 0);
    }
}

static void download_restart_and_failed_rebegin(void)
{
    for (int fail = 0; fail < 2; fail++) {
        reset_sdk();
        ota_session_t s = {0};
        CHECK(ota_session_begin(&s, &partition, OTA_SIZE_UNKNOWN) == ESP_OK);
        CHECK(ota_session_write(&s, "abc", 3) == ESP_OK);
        CHECK(ota_session_abort(&s) == ESP_OK);
        begin_result = fail ? ESP_FAIL : ESP_OK;
        CHECK(ota_session_begin(&s, &partition, OTA_SIZE_UNKNOWN) == begin_result);
        CHECK(s.active == !fail);
        CHECK(ota_session_abort(&s) == ESP_OK);
        CHECK(abort_calls == (fail ? 1u : 2u));
    }
}

static void abort_failure_does_not_reuse_handle(void)
{
    reset_sdk();
    ota_session_t s = {0};
    CHECK(ota_session_begin(&s, &partition, OTA_SIZE_UNKNOWN) == ESP_OK);
    abort_result = ESP_FAIL;
    CHECK(ota_session_abort(&s) == ESP_FAIL);
    CHECK(!s.active && s.handle == 0);
    CHECK(ota_session_abort(&s) == ESP_OK);
    CHECK(abort_calls == 1);
}

static int mark_result, mark_calls;
static int mark_valid(void *ctx)
{
    CHECK(ctx == &mark_result);
    mark_calls++;
    return mark_result;
}

static void mark_valid_requires_readiness(void)
{
    bool marked = false;
    int error = ESP_OK;
    mark_calls = 0;
    mark_result = ESP_OK;
    CHECK(!ota_mark_valid_should_fire(false, false, 100, 30));
    CHECK(!ota_mark_valid_should_fire(false, true, 29, 30));
    CHECK(ota_mark_valid_should_fire(false, true, 30, 30));
    CHECK(ota_mark_valid_should_fire(true, false, 0, 30));
    CHECK(!ota_mark_valid_attempt(&marked, false, mark_valid, &mark_result, &error));
    CHECK(!marked && mark_calls == 0);
}

static void failed_mark_is_retried_until_success(void)
{
    bool marked = false;
    int error = ESP_OK;
    mark_calls = 0;
    mark_result = ESP_FAIL;
    CHECK(ota_mark_valid_attempt(&marked, true, mark_valid, &mark_result, &error));
    CHECK(!marked && error == ESP_FAIL && mark_calls == 1);
    mark_result = ESP_OK;
    CHECK(ota_mark_valid_attempt(&marked, true, mark_valid, &mark_result, &error));
    CHECK(marked && error == ESP_OK && mark_calls == 2);
    CHECK(!ota_mark_valid_attempt(&marked, true, mark_valid, &mark_result, &error));
    CHECK(mark_calls == 2);
}

int main(void)
{
    RUN(failed_begin_has_no_cleanup_handle);
    RUN(failed_write_aborts_once);
    RUN(end_consumes_handle_on_success_and_failure);
    RUN(download_restart_and_failed_rebegin);
    RUN(abort_failure_does_not_reuse_handle);
    RUN(mark_valid_requires_readiness);
    RUN(failed_mark_is_retried_until_success);
    TESTS_DONE();
}
