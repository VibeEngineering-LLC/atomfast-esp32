#include "af_test.h"
#include "ble_gw_stats.h"
#include "af_net.h"
#include "net_time.h"
#include "cJSON.h"
#include <limits.h>
#include <stdlib.h>

static int psram_calls, fallback_calls, frees, fail_psram, fail_fallback;
static size_t allocated_cap;
static unsigned char *allocated;
static char response[16384], response_type[64], response_headers[256];
static int response_code, ready = 1, session_count = 32;
static af_bs_snap_t snapshot;
static af_bs_sess_t session;

static void *alloc_guarded(size_t cap) {
    allocated = malloc(cap + 16);
    if (allocated) {
        allocated_cap = cap;
        memset(allocated + cap, 0xA5, 16);
    }
    return allocated;
}

static void *fallback_malloc(size_t cap) {
    fallback_calls++;
    return fail_fallback ? NULL : alloc_guarded(cap);
}

static void guarded_free(void *ptr) {
    CHECK(ptr == allocated);
    if (ptr) {
        for (size_t i = 0; i < 16; i++) CHECK(allocated[allocated_cap + i] == 0xA5);
        free(ptr);
    }
    allocated = NULL;
    frees++;
}

#define malloc fallback_malloc
#define free guarded_free
#ifndef AF_BLE_HTTP_IMPL
#define AF_BLE_HTTP_IMPL "../../main/af_ble_http.c"
#endif
#include AF_BLE_HTTP_IMPL
#undef malloc
#undef free

void *heap_caps_malloc(size_t cap, uint32_t flags) {
    CHECK(flags == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    psram_calls++;
    return fail_psram ? NULL : alloc_guarded(cap);
}
bool ble_gw_stats_snapshot(af_bs_snap_t *out, uint64_t *now_ms) {
    *out = snapshot;
    *now_ms = UINT64_C(4294967295000);
    return ready;
}
bool ble_gw_is_ready(void) { return ready; }
bool ble_gw_stats_session(uint8_t idx, af_bs_sess_t *out) {
    if (idx >= session_count) return false;
    *out = session;
    return true;
}
void ble_gw_stats_device(char *addr, size_t acap, int *type, char *name, size_t ncap) {
    snprintf(addr, acap, "AA:BB:**:**:**:FF");
    memset(name, 'X', ncap - 1);
    name[ncap - 1] = 0;
    *type = 1;
}
net_time_source_t net_time_source(void) { return TIME_SRC_NONE; }
int httpd_req_get_url_query_len(httpd_req_t *req) { return req->query ? (int)strlen(req->query) : 0; }
esp_err_t httpd_req_get_url_query_str(httpd_req_t *req, char *buf, size_t cap) {
    if (req->query_error || strlen(req->query) >= cap) return -1;
    strcpy(buf, req->query);
    return ESP_OK;
}
esp_err_t httpd_resp_send_err(httpd_req_t *req, int code, const char *message) {
    (void)req;
    response_code = code;
    snprintf(response, sizeof(response), "%s", message);
    return ESP_OK;
}
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type) {
    (void)req;
    snprintf(response_type, sizeof(response_type), "%s", type);
    return ESP_OK;
}
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *name, const char *value) {
    (void)req;
    snprintf(response_headers, sizeof(response_headers), "%s=%s", name, value);
    return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, long len) {
    (void)req;
    CHECK(len >= 0 && (size_t)len < sizeof(response));
    if (len < 0 || (size_t)len >= sizeof(response)) return -1;
    memcpy(response, buf, (size_t)len);
    response[len] = 0;
    return ESP_OK;
}

static void reset_response(void) {
    CHECK(allocated == NULL);
    psram_calls = fallback_calls = frees = 0;
    response_code = 200;
    response[0] = response_type[0] = response_headers[0] = 0;
}

static void fill_widest(void) {
    session = (af_bs_sess_t){ .end_utc = UINT32_MAX, .dur_ms = UINT32_MAX,
        .pkts = UINT32_MAX, .bad = UINT32_MAX, .setup_ms = UINT32_MAX,
        .reason = 0x3E, .rssi_min = INT8_MIN, .rssi_avg = INT8_MIN, .rssi_max = INT8_MIN };
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.ext = snapshot.connected = snapshot.have_ext = snapshot.have_last = true;
    snapshot.last_reason_valid = snapshot.have_bad = true;
    snapshot.last_reason = 0x3E;
    snapshot.ring_n = 32;
    snapshot.sess_dur_ms = snapshot.sess_pkts = snapshot.sess_bad = UINT32_MAX;
    snapshot.boot.connects = snapshot.life.connects = UINT32_MAX;
    snapshot.up_24h_pct = NAN;
}

static void t_sessions_limits(void) {
    fill_widest();
    for (int limit = 1; limit <= 32; limit++) {
        char query[32];
        snprintf(query, sizeof(query), "limit=%d", limit);
        httpd_req_t req = { .query = query };
        reset_response();
        CHECK(af_ble_http_sessions(&req) == ESP_OK);
        CHECK(response_code == 200 && frees == 1);
        CHECK(allocated_cap == 3 + (size_t)limit * 248u);
        CHECK(strlen(response) < allocated_cap);
        cJSON *j = cJSON_Parse(response);
        CHECK(cJSON_IsArray(j) && cJSON_GetArraySize(j) == limit);
        cJSON_Delete(j);
    }
    /* Every possible reason catches future growth in the shared text table. */
    for (unsigned reason = 0; reason <= UINT16_MAX; reason++) {
        session.reason = (uint16_t)reason;
        httpd_req_t req = { .query = "limit=1" };
        reset_response();
        CHECK(af_ble_http_sessions(&req) == ESP_OK && response_code == 200);
        CHECK(strlen(response) < 3 + 248u);
        req.query = NULL;
        session_count = 1;
        reset_response();
        CHECK(af_ble_http_sessions_csv(&req) == ESP_OK && response_code == 200);
        const char *row = strchr(response, '\n');
        CHECK(row != NULL && strlen(row + 1) <= 116u);
        session_count = 32;
    }
    session.reason = 0x3E;
    httpd_req_t req = {0};
    reset_response();
    CHECK(af_ble_http_sessions_csv(&req) == ESP_OK && response_code == 200);
    CHECK(strlen(response) < allocated_cap);
    CHECK(allocated_cap == sizeof("idx,start_utc,end_utc,dur_s,pkts,bad,reason_code,reason,rssi_min_dbm,rssi_avg_dbm,rssi_max_dbm,setup_ms\n") + 32 * 116u);
    int lines = 0;
    for (const char *p = response; *p; p++) if (*p == '\n') lines++;
    CHECK(lines == 33);
}

static void t_validation_and_allocation(void) {
    const char *invalid[] = { "x=1", "limit=", "limit=0", "limit=33", "limit=100", "limit=-1", "limit=1&x=1" };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        httpd_req_t req = { .query = invalid[i] };
        reset_response();
        CHECK(af_ble_http_sessions(&req) == ESP_OK && response_code == 400);
        CHECK(psram_calls == 0 && fallback_calls == 0 && frees == 0);
    }
    httpd_req_t req = { .query = "x=1" };
    reset_response();
    CHECK(af_ble_http_sessions_csv(&req) == ESP_OK && response_code == 400 && psram_calls == 0);
    req.query = "limit=1";
    req.query_error = 1;
    reset_response();
    CHECK(af_ble_http_sessions(&req) == ESP_OK && response_code == 400 && psram_calls == 0);
    req.query = "limit=1234567890123456789012345678901234567890123456789012345678901234567890";
    req.query_error = 0;
    reset_response();
    CHECK(af_ble_http_sessions(&req) == ESP_OK && response_code == 400 && psram_calls == 0);
    req = (httpd_req_t){0};
    fail_psram = 1;
    reset_response();
    CHECK(af_ble_http_sessions(&req) == ESP_OK && response_code == 200);
    CHECK(psram_calls == 1 && fallback_calls == 1 && frees == 1);
    fail_fallback = 1;
    reset_response();
    CHECK(af_ble_http_sessions(&req) == ESP_OK && response_code == 500);
    CHECK(strcmp(response, "oom") == 0 && psram_calls == 1 && fallback_calls == 1);
    fail_psram = fail_fallback = 0;
}

static void t_stats_and_empty(void) {
    httpd_req_t req = {0};
    fill_widest();
    for (int ext = 0; ext <= 1; ext++) {
        snapshot.ext = ext;
        reset_response();
        CHECK(af_ble_http_stats(&req) == ESP_OK && response_code == 200);
        CHECK(allocated_cap == 10240);
        cJSON *j = cJSON_Parse(response);
        CHECK(cJSON_IsObject(j));
        cJSON_Delete(j);
    }
    ready = 0;
    reset_response();
    CHECK(af_ble_http_sessions(&req) == ESP_OK && strcmp(response, "[]") == 0);
    reset_response();
    CHECK(af_ble_http_stats(&req) == ESP_OK && strcmp(response, "{\"ready\":false}") == 0);
    ready = 1;
}

static void dump_valid_responses(void) {
    httpd_req_t req = {0};
    fill_widest();
    for (int scenario = 0; scenario < 4; scenario++) {
        ready = scenario != 3;
        snapshot.ext = scenario == 0;
        if (scenario == 2) memset(&session, 0, sizeof(session));
        reset_response(); af_ble_http_stats(&req);
        printf("%d stats %s %s %s\n", scenario, response_type, response_headers, response);
        reset_response(); af_ble_http_sessions(&req);
        printf("%d sessions %s %s %s\n", scenario, response_type, response_headers, response);
        reset_response(); af_ble_http_sessions_csv(&req);
        printf("%d csv %s %s %s\n", scenario, response_type, response_headers, response);
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--dump") == 0) { dump_valid_responses(); return g_failures ? 1 : 0; }
    RUN(t_sessions_limits);
    RUN(t_validation_and_allocation);
    RUN(t_stats_and_empty);
    TESTS_DONE();
}
