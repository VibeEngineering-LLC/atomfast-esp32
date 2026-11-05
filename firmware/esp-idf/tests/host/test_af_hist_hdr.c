#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "af_hist_query.h"
#include "af_history_hdr.h"

static int g_n, g_fail;

#define CHECK(cond) do { \
    g_n++; \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        g_fail++; \
    } \
} while (0)

/* ---- mock httpd state ---- */

static int m_cnt;
static char m_name[16][32];
static char m_val[16][128];
static int m_cap = 8;

static void mreset(int cap)
{
    m_cnt = 0;
    m_cap = cap;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *name, const char *val)
{
    (void)req;
    if (m_cnt >= m_cap)
        return ESP_FAIL;
    snprintf(m_name[m_cnt], sizeof(m_name[m_cnt]), "%s", name);
    snprintf(m_val[m_cnt], sizeof(m_val[m_cnt]), "%s", val);
    m_cnt++;
    return ESP_OK;
}

static const char *mget(const char *name)
{
    for (int i = 0; i < m_cnt; i++) {
        if (strcmp(m_name[i], name) == 0)
            return m_val[i];
    }
    return NULL;
}

/* ---- test ---- */

int main(void)
{
    static httpd_req_t req;
    af_hq_hdrs_t hh;

    af_hq_hdrs_fill(&hh, AF_HQ_KIND_RAW, 6374, 12, 2482, 3, "12:34:ab:cd:ef:01");

    /* Case 1: ds + device (worst case, 7 headers) */
    mreset(8);
    esp_err_t rc = af_hist_resp_hdrs(&req, "attachment; filename=x.csv", &hh, "next=100;bucket=4");
    CHECK(rc == ESP_OK);
    CHECK(m_cnt <= AF_HIST_HDR_BUDGET);
    CHECK(m_cnt < 8);
    CHECK(mget("X-AF-Ds") != NULL && strcmp(mget("X-AF-Ds"), "next=100;bucket=4") == 0);
    CHECK(mget("X-AF-Device") != NULL && strcmp(mget("X-AF-Device"), "12:34:ab:cd:ef:01") == 0);
    CHECK(mget("X-AF-Seq") != NULL && strcmp(mget("X-AF-Seq"), "6374") == 0);
    CHECK(mget("X-AF-Kind") != NULL && strcmp(mget("X-AF-Kind"), "raw") == 0);
    CHECK(mget("X-AF-Range") != NULL && strcmp(mget("X-AF-Range"), "epoch=12;first=2482;lost=3") == 0);
    CHECK(mget("Cache-Control") != NULL && strcmp(mget("Cache-Control"), "no-store") == 0);
    CHECK(mget("Content-Disposition") != NULL);
    CHECK(mget("X-AF-Ring") == NULL);

    /* Case 2: no ds, no device (5 headers) */
    {
        af_hq_hdrs_t hh2;
        af_hq_hdrs_fill(&hh2, AF_HQ_KIND_RAW, 6374, 12, 2482, 3, NULL);
        mreset(8);
        rc = af_hist_resp_hdrs(&req, "attachment; filename=x.csv", &hh2, NULL);
        CHECK(rc == ESP_OK);
        CHECK(m_cnt == 5);
        CHECK(mget("X-AF-Ds") == NULL);
        CHECK(mget("X-AF-Device") == NULL);
    }

    /* Case 3: failure is not silent */
    mreset(3);
    rc = af_hist_resp_hdrs(&req, "attachment; filename=x.csv", &hh, "next=100;bucket=4");
    CHECK(rc != ESP_OK);

    mreset(6);
    rc = af_hist_resp_hdrs(&req, "attachment; filename=x.csv", &hh, "next=100;bucket=4");
    CHECK(rc != ESP_OK);

    /* Case 4: real limit 8 with worst case passes — covered by Case 1 */

    printf("test_af_hist_hdr: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
