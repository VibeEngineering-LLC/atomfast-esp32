#include <string.h>
#include <sys/types.h>
#include "http_stub/esp_http_server.h"
#include "af_test.h"

#define HTTPD_SOCK_ERR_TIMEOUT (-3)
esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t length);
#include "../../main/af_http_body.h"
#include "../../main/http_io_gate.h"

static int steps[16], step_count, step_pos, recv_calls;
static char response_status[48], response_type[48], response_body[128];
static int send_calls;

static httpd_req_t request(const char *body)
{
    step_count = step_pos = recv_calls = 0;
    memset(steps, 0, sizeof steps);
    httpd_req_t req = { .body = body, .content_len = (int)strlen(body) };
    return req;
}

int httpd_req_recv(httpd_req_t *req, char *buf, size_t length)
{
    recv_calls++;
    int step = step_pos < step_count ? steps[step_pos++] : (int)length;
    if (step <= 0) return step;
    size_t n = (size_t)step < length ? (size_t)step : length;
    memcpy(buf, req->body + req->body_pos, n);
    req->body_pos += (int)n;
    return (int)n;
}

esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status)
{
    (void)req;
    snprintf(response_status, sizeof response_status, "%s", status);
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{
    (void)req;
    snprintf(response_type, sizeof response_type, "%s", type);
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t length)
{
    (void)req;
    send_calls++;
    if (length < 0 || (size_t)length >= sizeof response_body) return ESP_FAIL;
    memcpy(response_body, buf, (size_t)length);
    response_body[length] = '\0';
    return ESP_OK;
}

static void empty_and_exact_capacity(void)
{
    char buf[5] = "xxxx";
    httpd_req_t req = request("");
    CHECK(af_http_read_body(&req, buf, 1) == 0);
    CHECK(buf[0] == '\0');
    CHECK(recv_calls == 0);
    req = request("abcd");
    CHECK(af_http_read_body(&req, buf, sizeof buf) == 4);
    CHECK(strcmp(buf, "abcd") == 0);
}

static void reject_oversize_without_read(void)
{
    char buf[4] = "xxx";
    httpd_req_t req = request("abcd");
    CHECK(af_http_read_body(&req, buf, sizeof buf) == -1);
    CHECK(recv_calls == 0);
    CHECK(strcmp(buf, "xxx") == 0);
    CHECK(af_http_read_body(&req, buf, 0) == -1);
    CHECK(af_http_read_body(NULL, buf, sizeof buf) == -1);
    CHECK(af_http_read_body(&req, NULL, sizeof buf) == -1);
}

static void fragmented_body_and_timeout_reset(void)
{
    char buf[5];
    httpd_req_t req = request("abcd");
    int sequence[] = {-3, -3, -3, 1, -3, -3, -3, 1, 2};
    memcpy(steps, sequence, sizeof sequence);
    step_count = sizeof sequence / sizeof sequence[0];
    CHECK(af_http_read_body(&req, buf, sizeof buf) == 4);
    CHECK(strcmp(buf, "abcd") == 0);
    CHECK(recv_calls == 9);
}

static void fourth_timeout_and_disconnect(void)
{
    char buf[5];
    httpd_req_t req = request("abcd");
    for (int i = 0; i < 4; i++) steps[i] = HTTPD_SOCK_ERR_TIMEOUT;
    step_count = 4;
    CHECK(af_http_read_body(&req, buf, sizeof buf) == -1);
    CHECK(recv_calls == 4);
    req = request("abcd");
    steps[0] = 2; steps[1] = 0; step_count = 2;
    CHECK(af_http_read_body(&req, buf, sizeof buf) == -1);
    CHECK(recv_calls == 2);
    req = request("abcd");
    steps[0] = -2; step_count = 1;
    CHECK(af_http_read_body(&req, buf, sizeof buf) == -1);
}

static void error_response_contract(void)
{
    httpd_req_t req = request("");
    CHECK(af_http_send_error(&req, "403 Forbidden", "csrf") == ESP_OK);
    CHECK(strcmp(response_status, "403 Forbidden") == 0);
    CHECK(strcmp(response_type, "application/json") == 0);
    CHECK(strcmp(response_body, "{\"error\":\"csrf\"}") == 0);
    char oversized[128];
    memset(oversized, 'a', sizeof oversized - 1);
    oversized[sizeof oversized - 1] = '\0';
    int before = send_calls;
    CHECK(af_http_send_error(&req, "500 Internal Server Error", oversized) == ESP_FAIL);
    CHECK(send_calls == before);
}

static void gate_observation_contract(void)
{
    for (int i = 0; i < 2; i++) {
        http_io_gate_init();
        CHECK(http_io_gate_busy() == false);
        CHECK(http_io_gate_waiters() == 0);
        CHECK(http_io_gate_reject_count() == 0);
    }
}

int main(void)
{
    RUN(empty_and_exact_capacity);
    RUN(reject_oversize_without_read);
    RUN(fragmented_body_and_timeout_reset);
    RUN(fourth_timeout_and_disconnect);
    RUN(error_response_contract);
    RUN(gate_observation_contract);
    TESTS_DONE();
}
