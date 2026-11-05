/* b29 G (b26 F10): POST /api/ble/manual - atype только целое 0 или 1; дробное, отрицательное, огромное -> 400 без вызова ble_gw_sel_manual. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "pfs_stubs.h"
#include "af_test.h"

/* globals required by pfs_stubs.h */
int g_inj_sem_fail;
void (*g_take_hook)(void);
int g_sim_verbose;
int64_t g_now_us;
inj_t g_inj_malloc;

#include "af_ble_sel_http.c"

/* --- stubs (external linkage) --- */

bool web_server_csrf_ok(httpd_req_t *req)
{
    (void)req;
    return true;
}

static int g_manual_calls;
static uint8_t g_manual_atype;

int ble_gw_sel_manual(const uint8_t addr[6], uint8_t atype, bool confirm_clear, uint32_t *ring_count)
{
    (void)addr;
    (void)confirm_clear;
    g_manual_calls++;
    g_manual_atype = atype;
    if (ring_count) *ring_count = 0;
    return 0;
}

int ble_gw_sel_json(char *buf, size_t cap)
{
    (void)buf; (void)cap;
    return -1;
}

int ble_gw_sel_select(uint32_t gen, uint8_t tok, bool confirm_clear, uint32_t *ring_count)
{
    (void)gen; (void)tok; (void)confirm_clear; (void)ring_count;
    return 0;
}

int ble_gw_sel_rescan(bool replace, uint32_t *gen_out)
{
    (void)replace; (void)gen_out;
    return 0;
}

int ble_gw_sel_forget(bool confirm)
{
    (void)confirm;
    return 0;
}

int ble_gw_sel_cancel(void)
{
    return 0;
}

static char g_status[64];
static char g_body[512];

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s)
{
    (void)r;
    strncpy(g_status, s, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t)
{
    (void)r; (void)t;
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *k, const char *v)
{
    (void)r; (void)k; (void)v;
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len)
{
    (void)r;
    size_t n = (len < 0) ? strlen(buf) : (size_t)len; /* HTTPD_RESP_USE_STRLEN */
    if (n > 511) n = 511;
    if (n > 0) memcpy(g_body, buf, n);
    g_body[n] = '\0';
    return ESP_OK;
}

int httpd_req_recv(httpd_req_t *r, char *buf, size_t len)
{
    size_t remaining = r->content_len - r->pos;
    size_t n = (len < remaining) ? len : remaining;
    if (n > 0) memcpy(buf, r->body + r->pos, n);
    r->pos += n;
    return (int)n;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *u)
{
    (void)h; (void)u;
    return ESP_OK;
}

/* --- helpers --- */

static void post(const char *body)
{
    httpd_req_t r;
    memset(&r, 0, sizeof(r));
    r.body = body;
    r.content_len = strlen(body);
    r.pos = 0;
    g_status[0] = 0;
    g_body[0] = 0;
    g_manual_calls = 0;
    g_manual_atype = 0xFF;
    (void)af_ble_sel_manual_handler(&r);
}

static int is400(void)
{
    return strncmp(g_status, "400", 3) == 0;
}

/* --- tests --- */

static void t_atype_int_accepted(void)
{
    post("{\"addr\":\"02:af:00:00:00:02\",\"atype\":0}");
    CHECK(g_manual_calls == 1 && g_manual_atype == 0);

    post("{\"addr\":\"02:af:00:00:00:02\",\"atype\":1}");
    CHECK(g_manual_calls == 1 && g_manual_atype == 1);

    post("{\"addr\":\"02:af:00:00:00:02\",\"atype\":1.0}");
    CHECK(g_manual_calls == 1 && g_manual_atype == 1);
}

static void t_atype_fraction_rejected(void)
{
    const char *vals[] = {"0.5", "0.999", "1.5", "-0.5"};
    char buf[256];
    for (size_t i = 0; i < 4; i++) {
        snprintf(buf, sizeof(buf), "{\"addr\":\"02:af:00:00:00:02\",\"atype\":%s}", vals[i]);
        post(buf);
        CHECK(is400());
        CHECK(g_manual_calls == 0);
    }
}

static void t_atype_out_of_range_rejected(void)
{
    const char *vals[] = {"2", "-1", "1e300", "-1e300", "4294967297"};
    char buf[256];
    for (size_t i = 0; i < 5; i++) {
        snprintf(buf, sizeof(buf), "{\"addr\":\"02:af:00:00:00:02\",\"atype\":%s}", vals[i]);
        post(buf);
        CHECK(is400());
        CHECK(g_manual_calls == 0);
    }
}

int main(void)
{
    RUN(t_atype_int_accepted);
    RUN(t_atype_fraction_rejected);
    RUN(t_atype_out_of_range_rejected);
    TESTS_DONE();
}
