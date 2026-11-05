#pragma once
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define HTTPD_RESP_USE_STRLEN (-1)

typedef struct httpd_req {
    const char *uri;
    int content_len;
    const char *body;
    int body_pos;
} httpd_req_t;

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *name, const char *val);
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *s);
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, long len);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t len);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t len);

typedef struct {
    char status[48];
    char type[80];
    char hdr_name[4][32];
    char hdr_val[4][128];
    int nhdr;
    char *body;
    size_t len;
    size_t cap;
    int chunks;
    int finished;
} stub_resp_t;

extern stub_resp_t g_resp;
void stub_reset(void);
void stub_req(httpd_req_t *r, const char *uri, const char *body);

#include "af_meas.h"
extern af_meas_core_t g_core;
extern uint64_t g_now_ms;
extern int g_utc_ok;
extern uint32_t g_utc;
void drv_host_init(void);
void drv_host_free(void);
