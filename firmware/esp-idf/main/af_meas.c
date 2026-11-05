#include "af_meas_drv.h"
#include "af_meas.h"
#include "af_mjadd.h"
#include "af_store.h"
#include "af_net.h"
#include "af_log_api.h"
#include "af_fsinfo.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"

#define MEAS_TASK_STACK 5120
#define MEAS_DIR "/storage/series"
#define MEAS_LOCK_MS 1000

static af_meas_core_t s_core;
static SemaphoreHandle_t s_mtx;
static TaskHandle_t s_task;

typedef struct {
    char name[AF_MEAS_FILES_MAX][AF_MEAS_FNAME_MAX];
    uint32_t size[AF_MEAS_FILES_MAX];
    uint32_t n;
} meas_ft_t;

static meas_ft_t *s_ft;
static uint32_t s_file_id;
static uint32_t s_file_seen;

static uint64_t now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static int get_utc(uint32_t *u) {
    time_t t = time(NULL);
    if (t < 1700000000) {
        return 0;
    }
    if (u) *u = (uint32_t)t;
    return 1;
}

static void *m_alloc(size_t n) {
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p && n <= 4096) {
        p = heap_caps_malloc(n, MALLOC_CAP_8BIT);
    }
    return p;
}

static void m_free(void *p) {
    heap_caps_free(p);
}

static void m_log(int level, const char *msg) {
    af_log_event(level, "meas", "%s", msg);
}

static uint32_t m_seq(uint32_t *epoch) {
    af_store_status_t st;
    af_store_get_status(&st);
    if (epoch) *epoch = st.raw.epoch;
    return st.raw.seq;
}

static int lock(void) {
    return s_mtx != NULL && xSemaphoreTake(s_mtx, pdMS_TO_TICKS(MEAS_LOCK_MS)) == pdTRUE;
}

static void unlock(void) {
    if (s_mtx) xSemaphoreGive(s_mtx);
}

static void ft_alloc(void) {
    if (s_ft) return;
    s_ft = heap_caps_calloc(1, sizeof(meas_ft_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ft) return;

    mkdir(MEAS_DIR, 0777);

    DIR *d = opendir(MEAS_DIR);
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        size_t len = strlen(e->d_name);
        if (len >= AF_MEAS_FNAME_MAX || len < 4) continue;
        if (strcmp(e->d_name + len - 4, ".csv") != 0) continue;

        char path[96];
        snprintf(path, sizeof(path), "%s/%.*s", MEAS_DIR, AF_MEAS_FNAME_MAX - 1, e->d_name);

        struct stat st;
        if (stat(path, &st) != 0) continue;

        if (s_ft->n >= AF_MEAS_FILES_MAX) {
            unlink(path);
            continue;
        }

        strncpy(s_ft->name[s_ft->n], e->d_name, AF_MEAS_FNAME_MAX - 1);
        s_ft->name[s_ft->n][AF_MEAS_FNAME_MAX - 1] = '\0';
        s_ft->size[s_ft->n] = (uint32_t)st.st_size;
        s_ft->n++;
    }
    closedir(d);
}

static uint32_t ft_total(void) {
    uint32_t sum = 0;
    if (!s_ft) return 0;
    for (uint32_t i = 0; i < s_ft->n; i++) {
        sum += s_ft->size[i];
    }
    return sum;
}

static uint32_t fs_free_pct(void) {
    size_t total = 0, used = 0;
    if (af_fsinfo_get(&total, &used) != ESP_OK || total == 0) {
        return 0;
    }
    return (uint32_t)(100ull * (uint64_t)(total - used) / total);
}

static void ft_drop_oldest(void) {
    if (!s_ft || s_ft->n == 0) return;
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", MEAS_DIR, s_ft->name[0]);
    unlink(path);
    for (uint32_t i = 1; i < s_ft->n; i++) {
        memcpy(s_ft->name[i - 1], s_ft->name[i], AF_MEAS_FNAME_MAX);
        s_ft->size[i - 1] = s_ft->size[i];
    }
    s_ft->n--;
}

static int ft_find(const char *name) {
    if (!s_ft) return -1;
    for (uint32_t i = 0; i < s_ft->n; i++) {
        if (strcmp(s_ft->name[i], name) == 0) return (int)i;
    }
    return -1;
}

static void meas_write_file(const char *prefix, const char *tpl, int no_overwrite, const af_meas_res_t *row) {
    ft_alloc();
    if (!s_ft) {
        af_log_event(2, "meas", "no mem for files");
        return;
    }

    char fname[AF_MEAS_FNAME_MAX];
    int len = af_meas_tpl_expand(tpl, prefix, row->i, row->t0_utc, fname, sizeof(fname));
    if (len < 0) return;

    if (no_overwrite) {
        if (ft_find(fname) >= 0) {
            char cand[AF_MEAS_FNAME_MAX];
            char path[96];
            int found = 0;
            for (uint32_t k = 1; k <= 999; k++) {
                if (af_meas_name_suffix(fname, k, cand, sizeof(cand)) < 0) break;
                if (ft_find(cand) < 0) {
                    snprintf(path, sizeof(path), "%s/%s", MEAS_DIR, cand);
                    struct stat sb;
                    if (stat(path, &sb) != 0) {
                        strncpy(fname, cand, AF_MEAS_FNAME_MAX - 1);
                        fname[AF_MEAS_FNAME_MAX - 1] = '\0';
                        found = 1;
                        break;
                    }
                }
            }
            if (!found) return;
        }
    } else {
        int idx = ft_find(fname);
        if (idx >= 0) {
            for (uint32_t i = (uint32_t)idx; i < s_ft->n - 1; i++) {
                memcpy(s_ft->name[i], s_ft->name[i + 1], AF_MEAS_FNAME_MAX);
                s_ft->size[i] = s_ft->size[i + 1];
            }
            s_ft->n--;
        }
    }

    char buf[512];
    int hlen = af_meas_fmt_csv_header(buf, sizeof(buf));
    if (hlen < 0) return;

    int rlen = af_meas_fmt_csv_row(row, buf + hlen, sizeof(buf) - hlen);
    if (rlen < 0) return;
    int total_len = hlen + rlen;

    while (s_ft->n > 0 && af_meas_evict_need(s_ft->n, ft_total(), (uint32_t)total_len, fs_free_pct())) {
        ft_drop_oldest();
    }
    if (af_meas_evict_need(s_ft->n, ft_total(), (uint32_t)total_len, fs_free_pct())) {
        af_log_event(2, "meas", "no space for result file");
        return;
    }

    char path[96];
    snprintf(path, sizeof(path), "%s/%s", MEAS_DIR, fname);

    FILE *f = fopen(path, "wb");
    if (!f) {
        af_log_event(2, "meas", "result file write failed");
        return;
    }

    if (fwrite(buf, 1, (size_t)total_len, f) != (size_t)total_len) {
        fclose(f);
        unlink(path);
        af_log_event(2, "meas", "result file write failed");
        return;
    }
    fclose(f);

    if (s_ft->n < AF_MEAS_FILES_MAX) {
        strncpy(s_ft->name[s_ft->n], fname, AF_MEAS_FNAME_MAX - 1);
        s_ft->name[s_ft->n][AF_MEAS_FNAME_MAX - 1] = '\0';
        s_ft->size[s_ft->n] = (uint32_t)total_len;
        s_ft->n++;
    }
}

static void meas_task(void *arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (!lock()) continue;

        uint64_t tnow = now_ms();
        ble_gw_pkt_t pk;   /* e16g (T-56): каждый принятый пакет по очереди, а не последнее состояние раз в 250 мс */
        while (ble_gw_pop_packet(&pk)) {
            af_meas_feed(&s_core, pk.rx_ms, pk.pulses, pk.rate_usvh);
        }

        uint32_t u = 0;
        int uok = get_utc(&u);
        af_meas_tick(&s_core, tnow, uok, u);

        af_meas_res_t rows[4];
        int nrows = 0;
        char prefix[AF_MEAS_PREFIX_MAX];
        char tpl[AF_MEAS_TPL_MAX];
        int no_ow = 0;

        af_meas_run_t *r = s_file_id ? af_meas_find(&s_core, s_file_id) : NULL;
        if (r && r->file_store) {
            uint32_t pending = r->rows_total - s_file_seen;
            uint32_t avail = pending < r->res_n ? pending : r->res_n;
            s_file_seen += pending - avail;
            uint32_t take = avail < 4 ? avail : 4;
            for (uint32_t j = 0; j < take; j++) {
                const af_meas_res_t *row = af_meas_res_at(r, r->res_n - avail + j);
                if (row && (row->flags & AF_MEAS_F_FINAL)) {
                    if (nrows < 4) {
                        rows[nrows++] = *row;
                    }
                }
            }
            s_file_seen += take;
            strncpy(prefix, r->prefix, AF_MEAS_PREFIX_MAX - 1);
            prefix[AF_MEAS_PREFIX_MAX - 1] = '\0';
            strncpy(tpl, r->tpl, AF_MEAS_TPL_MAX - 1);
            tpl[AF_MEAS_TPL_MAX - 1] = '\0';
            no_ow = r->no_overwrite;
        } else if (r == NULL) {
            s_file_id = 0;
        }

        int idle = (s_core.active < 0) &&
                   (s_file_id == 0 || r == NULL || r->rows_total == s_file_seen) &&
                   (nrows == 0);

        if (idle) {
            s_task = NULL;
            unlock();
            vTaskDelete(NULL);
        }

        unlock();

        for (int i = 0; i < nrows; i++) {
            meas_write_file(prefix, tpl, no_ow, &rows[i]);
        }
    }
}

void af_meas_drv_init(void) {
    s_mtx = xSemaphoreCreateMutex();
    af_meas_init(&s_core, m_alloc, m_free, m_seq, m_log);
}

static void spawn_locked(void) {
    if (s_task != NULL) return;
    if (xTaskCreate(meas_task, "af_meas", MEAS_TASK_STACK, NULL, 2, &s_task) != pdPASS) {
        s_task = NULL;
        af_log_event(3, "meas", "task create failed");
    }
}

int af_meas_drv_start(const af_meas_params_t *p, uint32_t *id) {
    if (!lock()) return AF_MEAS_E_NOMEM;

    if (s_task != NULL && s_file_id != 0) {
        af_meas_run_t *r = af_meas_find(&s_core, s_file_id);
        if (r && r->rows_total != s_file_seen) {
            unlock();
            return AF_MEAS_E_BUSY;
        }
    }

    uint32_t u = 0;
    int uok = get_utc(&u);
    int rc = af_meas_start(&s_core, p, now_ms(), uok, u, id);

    if (rc == 0) {
        s_file_id = p->file_store ? *id : 0;
        s_file_seen = 0;
        ble_gw_drop_packets();   /* пакеты до старта не учитываются (feed к тому же отбрасывает rx_ms < t0) */
        spawn_locked();
        if (s_task == NULL) {
            af_meas_stop(&s_core, *id, now_ms());
            rc = AF_MEAS_E_NOMEM;
        }
    }

    unlock();
    return rc;
}

int af_meas_drv_stop(uint32_t id) {
    if (!lock()) return AF_MEAS_E_NOMEM;
    int rc = af_meas_stop(&s_core, id, now_ms());
    unlock();
    return rc;
}

int af_meas_drv_pause(uint32_t id) {
    if (!lock()) return AF_MEAS_E_NOMEM;
    int rc = af_meas_pause(&s_core, id, now_ms());
    unlock();
    return rc;
}

int af_meas_drv_resume(uint32_t id) {
    if (!lock()) return AF_MEAS_E_NOMEM;
    int rc = af_meas_resume(&s_core, id, now_ms());
    unlock();
    return rc;
}

int af_meas_drv_delete(uint32_t id) {
    if (!lock()) return AF_MEAS_E_NOMEM;
    int rc = af_meas_delete(&s_core, id);
    if (s_file_id == id) {
        s_file_id = 0;
    }
    unlock();
    return rc;
}

int af_meas_drv_add_journal(const char *name, const af_meas_res_t *res, uint32_t from, uint32_t *id) {
    if (!lock()) return AF_MEAS_E_NOMEM;
    int rc = af_meas_add_journal(&s_core, name, res, from, id);
    unlock();
    return rc;
}
uint32_t af_meas_drv_active_id(void) {
    if (!lock()) return 0;
    uint32_t id = s_core.active >= 0 ? s_core.runs[s_core.active].id : 0;
    unlock();
    return id;
}

int af_meas_drv_time(uint32_t *utc) {
    uint32_t u = 0;
    int ok = get_utc(&u);
    if (utc) *utc = ok ? u : 0;
    return ok;
}

static af_meas_run_t *pick(uint32_t id) {
    if (id != 0) {
        return af_meas_find(&s_core, id);
    }
    if (s_core.active >= 0) {
        return &s_core.runs[s_core.active];
    }
    af_meas_run_t *best = NULL;
    uint32_t max_id = 0;
    for (int i = 0; i < AF_MEAS_MAX_RUNS; i++) {
        if (s_core.runs[i].id != 0) {
            if (!best || s_core.runs[i].id > max_id) {
                best = &s_core.runs[i];
                max_id = s_core.runs[i].id;
            }
        }
    }
    return best;
}

int af_meas_drv_snapshot(uint32_t id, af_meas_run_t *run, af_meas_live_t *live, int *live_ok, af_meas_sum_t *sum) {
    if (!lock()) return -1;
    af_meas_run_t *r = pick(id);
    if (!r) {
        unlock();
        return -1;
    }
    *run = *r;
    *live_ok = (af_meas_live(r, now_ms(), live) == 0);
    af_meas_summary(r, sum);
    unlock();
    return 0;
}

int af_meas_drv_row(uint32_t id, uint32_t idx, af_meas_res_t *out) {
    if (!lock()) return -1;
    af_meas_run_t *r = af_meas_find(&s_core, id);
    if (!r) {
        unlock();
        return -1;
    }
    const af_meas_res_t *row = af_meas_res_at(r, idx);
    if (!row) {
        unlock();
        return -1;
    }
    *out = *row;
    unlock();
    return 0;
}

uint32_t af_meas_drv_ids(uint32_t *ids, uint32_t cap) {
    if (!lock()) return 0;
    uint32_t n = 0;
    for (int i = 0; i < AF_MEAS_MAX_RUNS && n < cap; i++) {
        if (s_core.runs[i].id != 0) {
            ids[n++] = s_core.runs[i].id;
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = i + 1; j < n; j++) {
            if (ids[j] > ids[i]) {
                uint32_t tmp = ids[i];
                ids[i] = ids[j];
                ids[j] = tmp;
            }
        }
    }
    unlock();
    return n;
}
