#define _POSIX_C_SOURCE 200809L
#pragma GCC diagnostic ignored "-Wunused-function"
#include "af_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include "af_push.h"
#include "push_t_common.inc"

static store_t ST;
static char BODY[16384];
static int G_PORT;
static const char *G_TOKEN;

static int http_req(int port, const char *method, const char *path, const char *hdrs, const char *body, size_t blen, int *status, char *resp, size_t rcap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct timeval tv = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    char req[4096];
    int len = snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Length: %zu\r\n", method, path, blen);
    if (hdrs) {
        len += snprintf(req + len, sizeof(req) - len, "%s", hdrs);
    }
    len += snprintf(req + len, sizeof(req) - len, "\r\n");

    if (write(fd, req, len) < 0) {
        close(fd);
        return -1;
    }
    if (blen > 0 && write(fd, body, blen) < 0) {
        close(fd);
        return -1;
    }

    char buf[8192];
    size_t rb_cap = 262144;
    char *resp_buf = malloc(rb_cap);
    if (!resp_buf) {
        close(fd);
        return -1;
    }
    size_t total = 0;
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (total + (size_t)n < rb_cap - 1) {
            memcpy(resp_buf + total, buf, n);
            total += n;
        }
    }
    close(fd);
    resp_buf[total] = 0;

    if (total == 0) {
        free(resp_buf);
        if (status) *status = 0;
        return -1;
    }

    char *hdr_end = strstr(resp_buf, "\r\n\r\n");
    if (!hdr_end) {
        free(resp_buf);
        if (status) *status = 0;
        return -1;
    }

    char *status_line = resp_buf;
    char *sp = strchr(status_line, ' ');
    if (!sp) {
        free(resp_buf);
        if (status) *status = 0;
        return -1;
    }
    *sp = '\0';
    int code = atoi(sp + 1);
    if (status) *status = code;

    char *body_start = hdr_end + 4;
    size_t body_len = total - (body_start - resp_buf);
    if (body_len >= rcap) body_len = rcap - 1;
    memcpy(resp, body_start, body_len);
    resp[body_len] = '\0';

    free(resp_buf);
    return 0;
}

static void ctl(const char *path) {
    int st = 0;
    char tmp[256];
    int ret = http_req(G_PORT, "POST", path, NULL, "", 0, &st, tmp, sizeof(tmp));
    CHECK(ret == 0);
    CHECK(st == 200);
}

static cJSON *stats(void) {
    static char resp[262144];
    int st = 0;
    int ret = http_req(G_PORT, "GET", "/ctl/stats", NULL, "", 0, &st, resp, sizeof(resp));
    if (ret != 0 || st != 200) {
        CHECK(0);
        return NULL;
    }
    return cJSON_Parse(resp);
}

static int srows(cJSON *s, const char *ring) {
    cJSON *rows = cJSON_GetObjectItem(s, "rows");
    if (!rows) return -1;
    cJSON *v = cJSON_GetObjectItem(rows, ring);
    return v ? v->valueint : -1;
}

static int sacc(cJSON *s, const char *ring) {
    cJSON *acc = cJSON_GetObjectItem(s, "accepted_sum");
    if (!acc) return -1;
    cJSON *v = cJSON_GetObjectItem(acc, ring);
    return v ? v->valueint : -1;
}

static int smax(cJSON *s, const char *ring) {
    cJSON *max = cJSON_GetObjectItem(s, "max_seq");
    if (!max) return -1;
    cJSON *v = cJSON_GetObjectItem(max, ring);
    return v ? v->valueint : -1;
}

static int smin(cJSON *s, const char *ring) {
    cJSON *min = cJSON_GetObjectItem(s, "min_seq");
    if (!min) return -1;
    cJSON *v = cJSON_GetObjectItem(min, ring);
    return v ? v->valueint : -1;
}

static int sposts(cJSON *s) {
    cJSON *posts = cJSON_GetObjectItem(s, "posts");
    if (!posts) return 0;
    return cJSON_GetArraySize(posts);
}

static int spost_int(cJSON *s, int i, const char *key) {
    cJSON *posts = cJSON_GetObjectItem(s, "posts");
    if (!posts) return -999;
    cJSON *p = cJSON_GetArrayItem(posts, i);
    if (!p) return -999;
    cJSON *v = cJSON_GetObjectItem(p, key);
    return v ? v->valueint : -999;
}

static const char *spost_ring(cJSON *s, int i) {
    cJSON *posts = cJSON_GetObjectItem(s, "posts");
    if (!posts) return NULL;
    cJSON *p = cJSON_GetArrayItem(posts, i);
    if (!p) return NULL;
    cJSON *v = cJSON_GetObjectItem(p, "ring");
    return v ? v->valuestring : NULL;
}

typedef struct { int port; const char *token; store_t *st; } ig_t;

static int ig_info(void *ctx, int kind, uint32_t *oldest, uint32_t *head, uint32_t *epoch) {
    ig_t *g = (ig_t*)ctx;
    return fk_info(g->st, kind, oldest, head, epoch);
}

static uint32_t ig_read(void *ctx, int kind, uint32_t since, void *out, uint32_t max, uint32_t *next, uint32_t *lost) {
    ig_t *g = (ig_t*)ctx;
    return fk_read(g->st, kind, since, out, max, next, lost);
}

static uint32_t ig_now(void *ctx) {
    (void)ctx;
    return 1792860000;
}

static int ig_post(void *ctx, int kind, const char *mac, uint32_t head, uint32_t epoch, uint32_t first_seq, uint32_t lost, const char *body, size_t len, int *http, char *resp, size_t resp_cap) {
    ig_t *g = (ig_t*)ctx;
    af_hq_hdrs_t hh;
    af_hq_hdrs_fill(&hh, kind, head, epoch, first_seq, lost, mac);
    
    char hdrs[512];
    snprintf(hdrs, sizeof(hdrs), 
        "Authorization: Bearer %s\r\n"
        "Content-Type: text/csv\r\n"
        "X-AF-Device: %s\r\n"
        "X-AF-Ring: %s\r\n"
        "X-AF-Epoch: %s\r\n"
        "X-AF-First-Seq: %s\r\n"
        "X-AF-Lost: %s\r\n",
        g->token, hh.device, hh.kind, hh.epoch, hh.first_seq, hh.lost);

    int ret = http_req(g->port, "POST", af_push_path(kind), hdrs, body, len, http, resp, resp_cap);
    if (ret != 0) {
        if (http) *http = 0;
        return 1;
    }
    return 0;
}

static const af_push_ops_t IG_OPS = { ig_info, ig_read, ig_post, ig_now };

static af_push_cfg_t icfg(uint16_t batch, uint8_t rings) {
    af_push_cfg_t c;
    af_push_cfg_default(&c);
    c.enabled = 1;
    c.host[0] = '1'; c.host[1] = '0'; c.host[2] = '.'; c.host[3] = '1'; c.host[4] = '.'; c.host[5] = '2'; c.host[6] = '.'; c.host[7] = '3'; c.host[8] = '\0';
    c.token[0] = 'x'; c.token[1] = '\0';
    c.batch_rows = batch;
    c.rings = rings;
    return c;
}

static int drive(ig_t *g, const af_push_cfg_t *c, af_push_ring_t r[3], uint8_t *auth, int max_steps) {
    int steps = 0;
    for (int i = 0; i < max_steps; i++) {
        int progress = 0;
        for (int k = 0; k < 3; k++) {
            if (!(c->rings & (1 << k))) continue;
            uint32_t left = 0;
            int moved = 0;
            int res = af_push_send_ring(&IG_OPS, g, k, c, D1_MAC, &r[k], auth, BODY, sizeof(BODY), &left, &moved);
            if (res == AF_PUSH_S_OK || res == AF_PUSH_S_WAIT) {
                progress = 1;
            }
            if (*auth != 0) {
                return steps;
            }
        }
        steps++;
        if (!progress) break;
    }
    return steps;
}

static void setup(int nraw, int nagg, int nenv) {
    ld_all();
    st_init(&ST, 1300, 400, 400);
    st_fill(&ST, nraw, nagg, nenv);
    ctl("/ctl/reset_db");
    ctl("/ctl/maxbody?v=65536");
}

static void i_full_delivery(void) {
    setup(1200, 365, 291);
    ig_t g = { G_PORT, G_TOKEN, &ST };
    af_push_cfg_t c = icfg(100, 7);
    af_push_ring_t r[3];
    for (int i=0; i<3; i++) af_push_ring_init(&r[i], 100);
    uint8_t auth = 0;
    drive(&g, &c, r, &auth, 200);

    cJSON *s = stats();
    CHECK(srows(s, "raw") == 1200);
    CHECK(srows(s, "agg") == 365);
    CHECK(srows(s, "env") == 291);
    CHECK(sacc(s, "raw") == 1200);
    CHECK(sacc(s, "agg") == 365);
    CHECK(sacc(s, "env") == 291);
    CHECK(smax(s, "raw") == 1199);
    CHECK(smax(s, "agg") == 364);
    CHECK(smax(s, "env") == 290);
    CHECK(smin(s, "raw") == 0);
    CHECK(smin(s, "agg") == 0);
    CHECK(smin(s, "env") == 0);
    
    CHECK(r[0].cur.acked == 1199 && r[0].cur.valid == 1 && r[0].cur.epoch == 21);
    CHECK(r[1].cur.acked == 364 && r[1].cur.valid == 1 && r[1].cur.epoch == 21);
    CHECK(r[2].cur.acked == 290 && r[2].cur.valid == 1 && r[2].cur.epoch == 21);
    CHECK(r[0].stopped == 0 && r[0].err == 0);
    CHECK(r[1].stopped == 0 && r[1].err == 0);
    CHECK(r[2].stopped == 0 && r[2].err == 0);

    int n_posts = sposts(s);
    int raw_posts = 0, agg_posts = 0, env_posts = 0;
    for (int i = 0; i < n_posts; i++) {
        const char *ring = spost_ring(s, i);
        int status = spost_int(s, i, "status");
        int inj = spost_int(s, i, "inj");
        CHECK(status == 200);
        CHECK(inj == 0);
        if (strcmp(ring, "raw") == 0) raw_posts++;
        else if (strcmp(ring, "agg") == 0) agg_posts++;
        else if (strcmp(ring, "env") == 0) env_posts++;
    }
    CHECK(raw_posts == 12);
    CHECK(agg_posts == 4);
    CHECK(env_posts == 3);
    
    cJSON *hw = cJSON_GetObjectItem(s, "hardware");
    CHECK(hw && hw->valueint == 1);
    cJSON_Delete(s);
}

static void i_big_batch(void) {
    setup(1200, 0, 0);
    ig_t g = { G_PORT, G_TOKEN, &ST };
    af_push_cfg_t c = icfg(2000, 1);
    af_push_ring_t r[3];
    for (int i=0; i<3; i++) af_push_ring_init(&r[i], 2000);
    r[0].eff_batch = 2000;
    uint8_t auth = 0;
    drive(&g, &c, r, &auth, 100);

    cJSON *s = stats();
    CHECK(srows(s, "raw") == 1200);
    int n_posts = sposts(s);
    int raw_posts = 0;
    for (int i = 0; i < n_posts; i++) {
        const char *ring = spost_ring(s, i);
        if (strcmp(ring, "raw") == 0) {
            raw_posts++;
            int len = spost_int(s, i, "len");
            int status = spost_int(s, i, "status");
            CHECK(len <= 16384);
            CHECK(status == 200);
        }
    }
    CHECK(raw_posts >= 6 && raw_posts <= 9);
    cJSON_Delete(s);
}

static void i_retry_5xx(void) {
    setup(300, 0, 0);
    ig_t g = { G_PORT, G_TOKEN, &ST };
    af_push_cfg_t c = icfg(50, 1);
    af_push_ring_t r[3];
    for (int i=0; i<3; i++) af_push_ring_init(&r[i], 50);
    uint8_t auth = 0;
    ctl("/ctl/fault?kind=code&code=503&n=2");
    drive(&g, &c, r, &auth, 100);

    cJSON *s = stats();
    CHECK(srows(s, "raw") == 300);
    int n_posts = sposts(s);
    CHECK(n_posts >= 3);
    CHECK(spost_int(s, 0, "status") == 503);
    CHECK(spost_int(s, 1, "status") == 503);
    CHECK(spost_int(s, 2, "status") == 200);
    CHECK(spost_int(s, 0, "first_seq") == 0);
    CHECK(spost_int(s, 1, "first_seq") == 0);
    CHECK(spost_int(s, 2, "first_seq") == 0);
    CHECK(r[0].fail_total == 2);
    CHECK(r[0].retries == 2);
    CHECK(r[0].fails == 0);
    CHECK(sacc(s, "raw") == 300);
    cJSON_Delete(s);
}

static void i_retry_drop(void) {
    setup(300, 0, 0);
    ig_t g = { G_PORT, G_TOKEN, &ST };
    af_push_cfg_t c = icfg(50, 1);
    af_push_ring_t r[3];
    for (int i=0; i<3; i++) af_push_ring_init(&r[i], 50);
    uint8_t auth = 0;
    ctl("/ctl/fault?kind=drop&n=2");
    drive(&g, &c, r, &auth, 100);

    cJSON *s = stats();
    CHECK(srows(s, "raw") == 300);
    int n_posts = sposts(s);
    CHECK(n_posts >= 3);
    CHECK(spost_int(s, 0, "status") == 0);
    CHECK(spost_int(s, 1, "status") == 0);
    CHECK(spost_int(s, 2, "status") == 200);
    CHECK(spost_int(s, 0, "first_seq") == 0);
    CHECK(spost_int(s, 1, "first_seq") == 0);
    CHECK(spost_int(s, 2, "first_seq") == 0);
    CHECK(sacc(s, "raw") == 300);
    CHECK(r[0].fail_total == 2);
    cJSON_Delete(s);
}

static void i_commit_drop(void) {
    setup(300, 0, 0);
    ig_t g = { G_PORT, G_TOKEN, &ST };
    af_push_cfg_t c = icfg(50, 1);
    af_push_ring_t r[3];
    for (int i=0; i<3; i++) af_push_ring_init(&r[i], 50);
    uint8_t auth = 0;
    ctl("/ctl/fault?kind=commit_drop&n=1");
    drive(&g, &c, r, &auth, 100);

    cJSON *s = stats();
    CHECK(srows(s, "raw") == 300);
    int n_posts = sposts(s);
    CHECK(n_posts >= 2);
    CHECK(spost_int(s, 0, "status") == 0);
    CHECK(spost_int(s, 1, "status") == 200);
    CHECK(spost_int(s, 0, "first_seq") == 0);
    CHECK(spost_int(s, 1, "first_seq") == 0);
    CHECK(spost_int(s, 1, "accepted") == 0);
    CHECK(spost_int(s, 1, "last_seq") == 49);
    CHECK(smax(s, "raw") == 299);
    CHECK(sacc(s, "raw") == 250);
    CHECK(r[0].cur.acked == 299);
    cJSON_Delete(s);
}

static void i_auth_401(void) {
    setup(300, 0, 0);
    ig_t g = { G_PORT, G_TOKEN, &ST };
    af_push_cfg_t c = icfg(50, 1);
    af_push_ring_t r[3];
    for (int i=0; i<3; i++) af_push_ring_init(&r[i], 50);
    uint8_t auth = 0;
    ctl("/ctl/token?v=other-token-0123456789");
    drive(&g, &c, r, &auth, 50);
    CHECK(auth == 1);
    
    cJSON *s = stats();
    int n_posts = sposts(s);
    int raw_posts = 0;
    for (int i = 0; i < n_posts; i++) {
        if (strcmp(spost_ring(s, i), "raw") == 0) {
            raw_posts++;
            CHECK(spost_int(s, i, "status") == 401);
        }
    }
    CHECK(raw_posts == 1);
    CHECK(srows(s, "raw") == 0);
    CHECK(r[0].err == AF_PUSH_ERR_AUTH);
    cJSON_Delete(s);

    char path[256];
    snprintf(path, sizeof(path), "/ctl/token?v=%s", G_TOKEN);
    ctl(path);
    auth = 0;
    af_push_ring_clear_err(&r[0], 50);
    drive(&g, &c, r, &auth, 100);
    
    s = stats();
    CHECK(srows(s, "raw") == 300);
    cJSON_Delete(s);
}

static void i_bad_400(void) {
    setup(300, 200, 0);
    ig_t g = { G_PORT, G_TOKEN, &ST };
    af_push_cfg_t c = icfg(50, 3);
    af_push_ring_t r[3];
    for (int i=0; i<3; i++) af_push_ring_init(&r[i], 50);
    uint8_t auth = 0;
    ctl("/ctl/fault?kind=code&code=400&n=1");
    drive(&g, &c, r, &auth, 100);

    CHECK(r[0].stopped == 1);
    CHECK(r[0].err == AF_PUSH_ERR_BAD);
    
    cJSON *s = stats();
    CHECK(srows(s, "agg") == 200);
    int n_posts = sposts(s);
    int raw_posts = 0;
    for (int i = 0; i < n_posts; i++) {
        if (strcmp(spost_ring(s, i), "raw") == 0) {
            raw_posts++;
            CHECK(spost_int(s, i, "status") == 400);
        }
    }
    CHECK(raw_posts == 1);
    cJSON_Delete(s);

    drive(&g, &c, r, &auth, 20);
    s = stats();
    int n_posts2 = sposts(s);
    int raw_posts2 = 0;
    for (int i = 0; i < n_posts2; i++) {
        if (strcmp(spost_ring(s, i), "raw") == 0) raw_posts2++;
    }
    CHECK(raw_posts2 == 1);
    cJSON_Delete(s);

    af_push_ring_clear_err(&r[0], 50);
    drive(&g, &c, r, &auth, 100);
    s = stats();
    CHECK(srows(s, "raw") == 300);
    cJSON_Delete(s);

    af_push_pack_t pk;
    af_push_pack(fk_read, &ST, 0, 0, 10, D1_MAC, 21, 0, BODY, sizeof(BODY), &pk);
    char hdrs[512];
    snprintf(hdrs, sizeof(hdrs), 
        "Authorization: Bearer %s\r\n"
        "X-AF-Device: %s\r\n"
        "X-AF-Epoch: 21\r\n"
        "X-AF-First-Seq: 5\r\n"
        "X-AF-Lost: 0\r\n",
        G_TOKEN, D1_MAC);
    int st = 0;
    char resp[256];
    http_req(G_PORT, "POST", "/ingest/raw", hdrs, BODY, pk.len, &st, resp, sizeof(resp));
    CHECK(st == 400);
}

static void i_413_shrink(void) {
    setup(300, 0, 0);
    ig_t g = { G_PORT, G_TOKEN, &ST };
    af_push_cfg_t c = icfg(200, 1);
    af_push_ring_t r[3];
    for (int i=0; i<3; i++) af_push_ring_init(&r[i], 200);
    r[0].eff_batch = 200;
    uint8_t auth = 0;
    ctl("/ctl/maxbody?v=3000");
    drive(&g, &c, r, &auth, 200);

    cJSON *s = stats();
    CHECK(srows(s, "raw") == 300);
    int n_posts = sposts(s);
    CHECK(n_posts > 0);
    CHECK(spost_int(s, 0, "status") == 413);
    CHECK(r[0].fail_total >= 1);
    CHECK(r[0].eff_batch >= 25);
    for (int i = 0; i < n_posts; i++) {
        if (strcmp(spost_ring(s, i), "raw") == 0) {
            int status = spost_int(s, i, "status");
            if (status == 200) {
                int len = spost_int(s, i, "len");
                CHECK(len <= 3000);
            }
        }
    }
    cJSON_Delete(s);
    ctl("/ctl/maxbody?v=65536");
}

static void i_reset_resend(void) {
    setup(300, 0, 0);
    ig_t g = { G_PORT, G_TOKEN, &ST };
    af_push_cfg_t c = icfg(100, 1);
    af_push_ring_t r[3];
    for (int i=0; i<3; i++) af_push_ring_init(&r[i], 100);
    uint8_t auth = 0;
    drive(&g, &c, r, &auth, 100);
    
    cJSON *s = stats();
    CHECK(srows(s, "raw") == 300);
    int n0 = sposts(s);
    cJSON_Delete(s);

    af_push_ring_reset_cursor(&r[0], 100);
    drive(&g, &c, r, &auth, 100);

    s = stats();
    int n1 = sposts(s);
    CHECK(n1 == n0 + 1);
    CHECK(spost_int(s, n0, "first_seq") == 0);
    CHECK(spost_int(s, n0, "status") == 200);
    CHECK(spost_int(s, n0, "accepted") == 0);
    CHECK(spost_int(s, n0, "last_seq") == 299);
    CHECK(srows(s, "raw") == 300);
    CHECK(sacc(s, "raw") == 300);
    CHECK(r[0].cur.acked == 299);
    CHECK(r[0].sent_total == 300);
    cJSON_Delete(s);
}

static void i_epoch_change(void) {
    setup(300, 0, 0);
    ig_t g = { G_PORT, G_TOKEN, &ST };
    af_push_cfg_t c = icfg(100, 1);
    af_push_ring_t r[3];
    for (int i=0; i<3; i++) af_push_ring_init(&r[i], 100);
    uint8_t auth = 0;
    drive(&g, &c, r, &auth, 100);
    
    cJSON *s = stats();
    CHECK(srows(s, "raw") == 300);
    cJSON_Delete(s);

    af_ring_clear(&ST.ring[0]);
    for (int i = 0; i < 30; i++) {
        af_raw_rec_t rec;
        mk_raw(L_RAW[i], &rec);
        uint8_t tmp[AF_REC_RAW_SIZE];
        af_raw_encode(&rec, tmp);
        af_ring_push(&ST.ring[0], tmp);
    }
    drive(&g, &c, r, &auth, 50);

    s = stats();
    CHECK(srows(s, "raw") == 330);
    CHECK(smax(s, "raw") == 329);
    CHECK(r[0].cur.epoch == 22);
    CHECK(r[0].cur.acked == 329);
    CHECK(r[0].err == 0);
    cJSON_Delete(s);
}

static void i_test_request(void) {
    setup(0, 0, 291);
    ig_t g = { G_PORT, G_TOKEN, &ST };
    
    char buf[4096];
    int n = af_push_test_body(buf, sizeof(buf));
    CHECK(n > 0);
    
    char hdrs[512];
    snprintf(hdrs, sizeof(hdrs), 
        "Authorization: Bearer %s\r\n"
        "X-AF-Device: %s\r\n"
        "X-AF-Epoch: 0\r\n"
        "X-AF-First-Seq: 0\r\n"
        "X-AF-Lost: 0\r\n"
        "X-AF-Ring: env\r\n"
        "X-AF-Test: 1\r\n",
        G_TOKEN, D1_MAC);
    int st = 0;
    char resp[256];
    http_req(G_PORT, "POST", "/ingest/env", hdrs, buf, n, &st, resp, sizeof(resp));
    CHECK(st == 200);
    
    uint32_t accepted = 0;
    int64_t last_seq = 0;
    CHECK(af_push_parse_resp(resp, strlen(resp), &accepted, &last_seq) == 0);
    CHECK(accepted == 0);
    CHECK(last_seq == -1);
    CHECK(af_push_resp_test(resp, strlen(resp)) == 1);
    
    cJSON *s = stats();
    CHECK(srows(s, "env") == 0);
    cJSON *hw0 = cJSON_GetObjectItem(s, "hardware");
    CHECK(hw0 && hw0->valueint == 0);
    cJSON_Delete(s);

    af_push_cfg_t c = icfg(100, 4);
    af_push_ring_t r[3];
    for (int i=0; i<3; i++) af_push_ring_init(&r[i], 100);
    uint8_t auth = 0;
    drive(&g, &c, r, &auth, 100);
    
    s = stats();
    CHECK(srows(s, "env") == 291);
    cJSON_Delete(s);

    n = af_push_test_body(buf, sizeof(buf));
    http_req(G_PORT, "POST", "/ingest/env", hdrs, buf, n, &st, resp, sizeof(resp));
    CHECK(st == 200);
    CHECK(af_push_parse_resp(resp, strlen(resp), &accepted, &last_seq) == 0);
    CHECK(accepted == 0);
    CHECK(last_seq == -1);
    CHECK(af_push_resp_test(resp, strlen(resp)) == 1);
    
    s = stats();
    CHECK(srows(s, "env") == 291);
    cJSON_Delete(s);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <token>\n", argv[0]);
        return 2;
    }
    G_PORT = atoi(argv[1]);
    G_TOKEN = argv[2];
    signal(SIGPIPE, SIG_IGN);

    RUN(i_full_delivery);
    RUN(i_big_batch);
    RUN(i_retry_5xx);
    RUN(i_retry_drop);
    RUN(i_commit_drop);
    RUN(i_auth_401);
    RUN(i_bad_400);
    RUN(i_413_shrink);
    RUN(i_reset_resend);
    RUN(i_epoch_change);
    RUN(i_test_request);

    TESTS_DONE();
    return 0;
}
