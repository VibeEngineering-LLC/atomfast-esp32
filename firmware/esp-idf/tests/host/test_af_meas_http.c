#include "af_test.h"
#include "esp_http_server.h"
#include "af_meas_http.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE 1000000ull

#define RUN2(fn) do { drv_host_init(); stub_reset(); RUN(fn); stub_reset(); drv_host_free(); } while (0)

static esp_err_t call(esp_err_t (*h)(httpd_req_t*), const char *uri, const char *body) {
    stub_reset();
    httpd_req_t r;
    stub_req(&r, uri, body);
    return h(&r);
}

static cJSON *rj(void) {
    return g_resp.body ? cJSON_Parse(g_resp.body) : NULL;
}

static int has(const char *sub) {
    return g_resp.body && strstr(g_resp.body, sub) != NULL;
}

static void sample(const char *name) {
    if (getenv("AF_SAMPLES")) {
        printf("SAMPLE %s %s\n", name, g_resp.body ? g_resp.body : "");
    }
}

static double num(cJSON *o, const char *k) {
    cJSON *item = cJSON_GetObjectItem(o, k);
    if (item && cJSON_IsNumber(item)) {
        return item->valuedouble;
    }
    return -1;
}

static void feed_run(uint64_t t0, int npk, uint16_t pulses) {
    for (int j = 0; j < npk; j++) {
        af_meas_feed(&g_core, t0 + 1000 + 2000ull * j, pulses, 0.1f);
    }
}

static void t_http_start_get(void) {
    CHECK(call(af_meas_http_post, "/api/meas/start", "{\"name\":\"Фон\",\"n\":5,\"t_s\":60,\"pause_s\":10}") == ESP_OK);
    CHECK(strcmp(g_resp.body, "{\"ok\":true,\"id\":1}") == 0);
    sample("start");

    g_now_ms = BASE + 5000;
    feed_run(BASE, 2, 35);

    CHECK(call(af_meas_http_get, "/api/meas", NULL) == ESP_OK);
    CHECK(strcmp(g_resp.type, "application/json") == 0);
    cJSON *j = rj();
    CHECK(j != NULL);
    if (j) {
        CHECK(cJSON_IsTrue(cJSON_GetObjectItem(j, "ok")));
        CHECK(cJSON_IsTrue(cJSON_GetObjectItem(j, "time_ok")));
        CHECK(num(j, "now_utc") == 1792859805);
        CHECK(num(j, "active_id") == 1);

        cJSON *limits = cJSON_GetObjectItem(j, "limits");
        CHECK(limits && cJSON_IsObject(limits));
        if (limits) {
            CHECK(num(limits, "n_max") == 1000);
            CHECK(num(limits, "t_min_s") == 10);
            CHECK(num(limits, "runs_max") == 8);
        }

        cJSON *runs = cJSON_GetObjectItem(j, "runs");
        CHECK(runs && cJSON_IsArray(runs) && cJSON_GetArraySize(runs) == 1);
        if (runs && cJSON_GetArraySize(runs) == 1) {
            cJSON *r0 = cJSON_GetArrayItem(runs, 0);
            cJSON *run = cJSON_GetObjectItem(r0, "run");
            CHECK(run && cJSON_IsObject(run));
            if (run) {
                CHECK(num(run, "id") == 1);
                cJSON *mode = cJSON_GetObjectItem(run, "mode");
                CHECK(mode && cJSON_IsString(mode) && strcmp(mode->valuestring, "series") == 0);
                cJSON *state = cJSON_GetObjectItem(run, "state");
                CHECK(state && cJSON_IsString(state) && strcmp(state->valuestring, "running") == 0);
                cJSON *phase = cJSON_GetObjectItem(run, "phase");
                CHECK(phase && cJSON_IsString(phase) && strcmp(phase->valuestring, "measuring") == 0);
                CHECK(num(run, "n") == 5);
                cJSON *name = cJSON_GetObjectItem(run, "name");
                CHECK(name && cJSON_IsString(name) && strcmp(name->valuestring, "Фон") == 0);
            }

            cJSON *sum = cJSON_GetObjectItem(r0, "sum");
            CHECK(sum && cJSON_IsObject(sum));
            if (sum) {
                CHECK(num(sum, "rows") == 0);
            }

            cJSON *live = cJSON_GetObjectItem(r0, "live");
            CHECK(live && cJSON_IsObject(live));
            if (live) {
                cJSON *lphase = cJSON_GetObjectItem(live, "phase");
                CHECK(lphase && cJSON_IsString(lphase) && strcmp(lphase->valuestring, "measuring") == 0);
                CHECK(num(live, "counts") == 70);
                CHECK(num(live, "npk") == 2);
            }
        }
        cJSON_Delete(j);
    }
    sample("get_running");
}

static void t_http_errors(void) {
    // (1)
    CHECK(call(af_meas_http_post, "/api/meas/start", NULL) == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "400 Bad Request") == 0);
    CHECK(has("bad_body"));

    // (2)
    CHECK(call(af_meas_http_post, "/api/meas/start", "{\"n\":\"x\"}") == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "400 Bad Request") == 0);
    CHECK(has("bad_value"));

    // (3)
    CHECK(call(af_meas_http_post, "/api/meas/start", "{\"zzz\":1}") == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "400 Bad Request") == 0);
    CHECK(has("unknown_key"));
    sample("error");

    // (4)
    CHECK(call(af_meas_http_post, "/api/meas/start", "[1]") == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "400 Bad Request") == 0);
    CHECK(has("json"));

    // (5)
    CHECK(call(af_meas_http_post, "/api/meas/start", "{\"stop_mode\":\"sigma\",\"n\":3}") == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "400 Bad Request") == 0);
    CHECK(has("sigma_only_single"));

    // (6)
    CHECK(call(af_meas_http_post, "/api/meas/start", "{\"name\":\"a\\\"b\"}") == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "400 Bad Request") == 0);
    CHECK(has("bad_body"));

    // (7)
    g_utc_ok = 0;
    CHECK(call(af_meas_http_post, "/api/meas/start", "{\"start_mode\":\"at\",\"start_at\":1792860000}") == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "409 Conflict") == 0);
    CHECK(has("time_not_synced"));
    g_utc_ok = 1;

    // (8)
    CHECK(call(af_meas_http_post, "/api/meas/start", "{\"n\":1,\"t_s\":60}") == ESP_OK);
    CHECK(call(af_meas_http_post, "/api/meas/start", "{\"n\":1,\"t_s\":60}") == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "409 Conflict") == 0);
    CHECK(has("busy"));

    // (9)
    CHECK(call(af_meas_http_post, "/api/meas/stop", "{\"id\":99}") == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "404 Not Found") == 0);
    CHECK(has("not_found"));

    // (10)
    CHECK(call(af_meas_http_post, "/api/meas/zzz", "{}") == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "404 Not Found") == 0);
    CHECK(has("not_found"));

    // (11)
    CHECK(call(af_meas_http_post, "/api/meas/delete", NULL) == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "400 Bad Request") == 0);
    CHECK(has("bad_body"));

    // (12)
    CHECK(call(af_meas_http_post, "/api/meas/delete", "{\"id\":1}") == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "409 Conflict") == 0);
    CHECK(has("bad_state"));

    // (13)
    CHECK(call(af_meas_http_post, "/api/meas/stop", NULL) == ESP_OK);
    CHECK(call(af_meas_http_post, "/api/meas/stop", NULL) == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "409 Conflict") == 0);
    CHECK(has("bad_state"));

    // (14)
    CHECK(call(af_meas_http_results, "/api/meas/results?id=99", NULL) == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "404 Not Found") == 0);

    CHECK(call(af_meas_http_results, "/api/meas/results?limit=0", NULL) == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "400 Bad Request") == 0);

    CHECK(call(af_meas_http_results, "/api/meas/results?limit=1001", NULL) == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "400 Bad Request") == 0);

    CHECK(call(af_meas_http_results, "/api/meas/results?id=abc", NULL) == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "400 Bad Request") == 0);
}

static void t_http_results(void) {
    CHECK(call(af_meas_http_post, "/api/meas/start", "{\"n\":3,\"t_s\":20,\"retry_invalid\":false,\"name\":\"Серия\"}") == ESP_OK);

    for (int k = 0; k < 3; k++) {
        feed_run(BASE + 20000ull * k, 10, 10);
    }

    g_now_ms = BASE + 60001;
    af_meas_tick(&g_core, g_now_ms, 1, g_utc + 60);

    CHECK(call(af_meas_http_results, "/api/meas/results?id=1&limit=2", NULL) == ESP_OK);
    cJSON *j = rj();
    CHECK(j != NULL);
    if (j) {
        CHECK(cJSON_IsTrue(cJSON_GetObjectItem(j, "ok")));
        cJSON *run = cJSON_GetObjectItem(j, "run");
        CHECK(run && cJSON_IsObject(run));
        if (run) {
            cJSON *state = cJSON_GetObjectItem(run, "state");
            CHECK(state && cJSON_IsString(state) && strcmp(state->valuestring, "done") == 0);
            CHECK(num(run, "done") == 3);
        }

        cJSON *sum = cJSON_GetObjectItem(j, "sum");
        CHECK(sum && cJSON_IsObject(sum));
        if (sum) {
            CHECK(num(sum, "n_valid") == 3);
            CHECK(num(sum, "counts") == 300);
        }

        CHECK(num(j, "from_idx") == 1);

        cJSON *rows = cJSON_GetObjectItem(j, "rows");
        CHECK(rows && cJSON_IsArray(rows) && cJSON_GetArraySize(rows) == 2);
        if (rows && cJSON_GetArraySize(rows) == 2) {
            cJSON *r0 = cJSON_GetArrayItem(rows, 0);
            cJSON *r1 = cJSON_GetArrayItem(rows, 1);
            CHECK(r0 && cJSON_IsObject(r0));
            CHECK(r1 && cJSON_IsObject(r1));
            if (r0) {
                CHECK(num(r0, "i") == 2);
                CHECK(num(r0, "counts") == 100);
                CHECK(num(r0, "npk") == 10);
                cJSON *reason = cJSON_GetObjectItem(r0, "stop_reason");
                CHECK(reason && cJSON_IsString(reason) && strcmp(reason->valuestring, "time") == 0);
                CHECK(cJSON_IsTrue(cJSON_GetObjectItem(r0, "valid")));
            }
            if (r1) {
                CHECK(num(r1, "i") == 3);
            }
        }

        cJSON *live = cJSON_GetObjectItem(j, "live");
        CHECK(live && cJSON_IsNull(live));
        cJSON_Delete(j);
    }
    sample("results");

    CHECK(call(af_meas_http_results, "/api/meas/results?id=1", NULL) == ESP_OK);
    j = rj();
    CHECK(j != NULL);
    if (j) {
        cJSON *rows = cJSON_GetObjectItem(j, "rows");
        CHECK(rows && cJSON_IsArray(rows) && cJSON_GetArraySize(rows) == 3);
        CHECK(num(j, "from_idx") == 0);
        cJSON_Delete(j);
    }

    CHECK(call(af_meas_http_results, "/api/meas/results", NULL) == ESP_OK);
    j = rj();
    CHECK(j != NULL);
    if (j) {
        cJSON *run = cJSON_GetObjectItem(j, "run");
        CHECK(run && cJSON_IsObject(run));
        if (run) {
            CHECK(num(run, "id") == 1);
        }
        cJSON_Delete(j);
    }

    CHECK(call(af_meas_http_csv, "/api/meas/results.csv?id=1", NULL) == ESP_OK);
    CHECK(strcmp(g_resp.type, "text/csv; charset=utf-8") == 0);
    int found_cd = 0;
    for (int i = 0; i < g_resp.nhdr; i++) {
        if (strcmp(g_resp.hdr_name[i], "Content-Disposition") == 0) {
            if (strstr(g_resp.hdr_val[i], "attachment; filename=\"measure-1.csv\"") != NULL) {
                found_cd = 1;
            }
        }
    }
    CHECK(found_cd == 1);
    CHECK(g_resp.body && strncmp(g_resp.body, "i,t0_utc,t_s,counts,", 19) == 0);
    int nl_count = 0;
    if (g_resp.body) {
        for (size_t i = 0; g_resp.body[i] != '\0'; i++) {
            if (g_resp.body[i] == '\n') nl_count++;
        }
    }
    CHECK(nl_count == 4);
    if (g_resp.body) {
        char *first_nl = strchr(g_resp.body, '\n');
        if (first_nl) {
            CHECK(strncmp(first_nl + 1, "1,", 2) == 0);
        }
    }
    sample("csv");
}

static void t_http_lifecycle(void) {
    CHECK(call(af_meas_http_post, "/api/meas/start", "{\"n\":0,\"t_s\":100}") == ESP_OK);
    CHECK(has("\"id\":1"));

    feed_run(BASE, 5, 10);
    g_now_ms = BASE + 10000;

    CHECK(call(af_meas_http_post, "/api/meas/pause", NULL) == ESP_OK);
    CHECK(strcmp(g_resp.body, "{\"ok\":true,\"id\":1}") == 0);

    CHECK(call(af_meas_http_get, "/api/meas", NULL) == ESP_OK);
    cJSON *j = rj();
    CHECK(j != NULL);
    if (j) {
        cJSON *runs = cJSON_GetObjectItem(j, "runs");
        CHECK(runs && cJSON_IsArray(runs) && cJSON_GetArraySize(runs) == 1);
        if (runs && cJSON_GetArraySize(runs) == 1) {
            cJSON *r0 = cJSON_GetArrayItem(runs, 0);
            cJSON *run = cJSON_GetObjectItem(r0, "run");
            CHECK(run && cJSON_IsObject(run));
            if (run) {
                cJSON *state = cJSON_GetObjectItem(run, "state");
                CHECK(state && cJSON_IsString(state) && strcmp(state->valuestring, "paused") == 0);
            }
        }
        cJSON_Delete(j);
    }

    CHECK(call(af_meas_http_post, "/api/meas/resume", "{\"id\":1}") == ESP_OK);

    CHECK(call(af_meas_http_get, "/api/meas", NULL) == ESP_OK);
    j = rj();
    CHECK(j != NULL);
    if (j) {
        cJSON *runs = cJSON_GetObjectItem(j, "runs");
        CHECK(runs && cJSON_IsArray(runs) && cJSON_GetArraySize(runs) == 1);
        if (runs && cJSON_GetArraySize(runs) == 1) {
            cJSON *r0 = cJSON_GetArrayItem(runs, 0);
            cJSON *run = cJSON_GetObjectItem(r0, "run");
            CHECK(run && cJSON_IsObject(run));
            if (run) {
                cJSON *state = cJSON_GetObjectItem(run, "state");
                CHECK(state && cJSON_IsString(state) && strcmp(state->valuestring, "running") == 0);
            }
        }
        cJSON_Delete(j);
    }

    g_now_ms = BASE + 16000;   /* после возобновления окно открыто с BASE+10000, подаём 3 пакета */
    feed_run(BASE + 10000, 3, 10);
    CHECK(call(af_meas_http_post, "/api/meas/stop", NULL) == ESP_OK);
    CHECK(strcmp(g_resp.body, "{\"ok\":true,\"id\":1}") == 0);

    CHECK(call(af_meas_http_get, "/api/meas", NULL) == ESP_OK);
    j = rj();
    CHECK(j != NULL);
    if (j) {
        cJSON *runs = cJSON_GetObjectItem(j, "runs");
        CHECK(runs && cJSON_IsArray(runs) && cJSON_GetArraySize(runs) == 1);
        if (runs && cJSON_GetArraySize(runs) == 1) {
            cJSON *r0 = cJSON_GetArrayItem(runs, 0);
            cJSON *run = cJSON_GetObjectItem(r0, "run");
            CHECK(run && cJSON_IsObject(run));
            if (run) {
                cJSON *state = cJSON_GetObjectItem(run, "state");
                CHECK(state && cJSON_IsString(state) && strcmp(state->valuestring, "done") == 0);
                cJSON *reason = cJSON_GetObjectItem(run, "end_reason");
                CHECK(reason && cJSON_IsString(reason) && strcmp(reason->valuestring, "manual") == 0);
            }
            cJSON *sum = cJSON_GetObjectItem(r0, "sum");
            CHECK(sum && cJSON_IsObject(sum));
            if (sum) {
                CHECK(num(sum, "rows") == 1);
            }
        }
        cJSON_Delete(j);
    }

    CHECK(call(af_meas_http_post, "/api/meas/delete", "{\"id\":1}") == ESP_OK);

    CHECK(call(af_meas_http_results, "/api/meas/results?id=1", NULL) == ESP_FAIL);
    CHECK(strcmp(g_resp.status, "404 Not Found") == 0);

    CHECK(call(af_meas_http_get, "/api/meas", NULL) == ESP_OK);
    j = rj();
    CHECK(j != NULL);
    if (j) {
        cJSON *runs = cJSON_GetObjectItem(j, "runs");
        CHECK(runs && cJSON_IsArray(runs) && cJSON_GetArraySize(runs) == 0);
        CHECK(num(j, "active_id") == 0);
        cJSON_Delete(j);
    }
}

static void t_http_order(void) {
    uint64_t t = BASE;
    for (int i = 0; i < 3; i++) {
        g_now_ms = t;
        CHECK(call(af_meas_http_post, "/api/meas/start", "{\"n\":1,\"t_s\":10,\"retry_invalid\":false}") == ESP_OK);
        g_now_ms = t + 10001;
        af_meas_tick(&g_core, g_now_ms, 1, g_utc);
        t += 20000;
    }

    CHECK(call(af_meas_http_get, "/api/meas", NULL) == ESP_OK);
    cJSON *j = rj();
    CHECK(j != NULL);
    if (j) {
        cJSON *runs = cJSON_GetObjectItem(j, "runs");
        CHECK(runs && cJSON_IsArray(runs) && cJSON_GetArraySize(runs) == 3);
        if (runs && cJSON_GetArraySize(runs) == 3) {
            cJSON *r0 = cJSON_GetArrayItem(runs, 0);
            cJSON *r1 = cJSON_GetArrayItem(runs, 1);
            cJSON *r2 = cJSON_GetArrayItem(runs, 2);
            cJSON *run0 = cJSON_GetObjectItem(r0, "run");
            cJSON *run1 = cJSON_GetObjectItem(r1, "run");
            cJSON *run2 = cJSON_GetObjectItem(r2, "run");
            CHECK(run0 && num(run0, "id") == 3);
            CHECK(run1 && num(run1, "id") == 2);
            CHECK(run2 && num(run2, "id") == 1);
        }
        cJSON_Delete(j);
    }
}

int main(void) {
    RUN2(t_http_start_get);
    RUN2(t_http_errors);
    RUN2(t_http_results);
    RUN2(t_http_lifecycle);
    RUN2(t_http_order);
    TESTS_DONE();
    return 0;
}
