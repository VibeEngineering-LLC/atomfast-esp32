#include "af_persist_fs.h"
#include "af_persist.h"
#include "af_clear.h"
#include "af_log_api.h"
#include "af_fsinfo.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include "esp_memory_utils.h"
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

static const char *TAG = "af_pfs";

typedef struct {
    const char *prefix;
    uint8_t kind;
    uint32_t rec_size;
    uint32_t seg_cap;
    uint32_t keep;
    uint32_t next_idx;
    uint32_t oldest_idx;
    uint32_t cur_idx;
    uint32_t cur_first;
    uint32_t cur_valid;
    int cur_open;
    uint32_t persisted_seq;
    uint32_t restored;
    int disabled; /* e8 п.4: восстановление не удалось - запись вида отключена, чтобы не затереть файлы */
} kstate_t;

static kstate_t s_k[3];
static af_store_core_t *s_core;
static SemaphoreHandle_t s_mtx;
static SemaphoreHandle_t s_flush_mtx;
static int s_mounted;
static int s_formatted;

static uint32_t s_flushes;
static uint32_t s_flush_errs;
static uint32_t s_bytes_written;
static uint32_t s_last_flush_ms;
static uint32_t s_max_flush_ms;
static uint32_t s_restore_ms;
static uint32_t s_restore_tail_bad;
static uint32_t s_restore_gap;
static uint32_t s_restore_fail;
static int64_t s_last_flush_s;
static int s_ext_stack_tasks;
static int s_caller_stack_internal;
static int s_flush_stack_internal;

static uint8_t s_chunk[64 * 28];
/* e16e (perf-audit memory F-1): 160 x 20 Б нужны только при восстановлении и очистке - в PSRAM, один раз, не освобождается */
static af_seg_info_t *s_info;
static int info_ensure(void) {
    if (!s_info) s_info = (af_seg_info_t *)heap_caps_malloc(160 * sizeof(af_seg_info_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_info ? 0 : -1;
}
static void clr_recover(void); /* e16 */

static void seg_path(const kstate_t *k, uint32_t idx, char *out, size_t cap) {
    snprintf(out, cap, "/storage/h/%s%08" PRIx32 ".seg", k->prefix, idx);
}

esp_err_t af_pfs_mount(void) {
    esp_vfs_littlefs_conf_t c = {
        .base_path = "/storage",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false
    };

    esp_err_t err = esp_vfs_littlefs_register(&c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        return err;
    }

    int mk = mkdir("/storage/h", 0777);
    int mk_errno = errno;
    int dir_existed = (mk == 0) ? 0 : 1;
    if (mk != 0 && mk_errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir /storage/h failed: %s", strerror(mk_errno));
    }

    s_mounted = 1;

    size_t total = 0, used = 0;
    if (esp_littlefs_info("storage", &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "fs: total=%llu used=%llu", (unsigned long long)total, (unsigned long long)used);
        if (used < 16384 && !dir_existed) {
            s_formatted = 1;
        }
    } else {
        ESP_LOGW(TAG, "esp_littlefs_info failed");
    }

    return ESP_OK;
}

void af_pfs_attach(af_store_core_t *core, SemaphoreHandle_t mtx) {
    s_core = core;
    s_mtx = mtx;

    s_flush_mtx = xSemaphoreCreateMutex();

    s_k[0].prefix = "r";
    s_k[0].kind = AF_SEG_KIND_RAW;
    s_k[0].rec_size = 20;
    s_k[0].seg_cap = AF_SEG_RAW_RECS;
    s_k[0].keep = AF_SEG_RAW_KEEP;

    s_k[1].prefix = "a";
    s_k[1].kind = AF_SEG_KIND_AGG;
    s_k[1].rec_size = 28;
    s_k[1].seg_cap = AF_SEG_AGG_RECS;
    s_k[1].keep = AF_SEG_AGG_KEEP;

    s_k[2].prefix = "e";
    s_k[2].kind = AF_SEG_KIND_ENV;
    s_k[2].rec_size = AF_REC_ENV_SIZE;
    s_k[2].seg_cap = AF_SEG_ENV_RECS;
    s_k[2].keep = AF_SEG_ENV_KEEP;

    s_last_flush_s = esp_timer_get_time() / 1000000;
}

/* Кольцо вида: 0 raw, 1 agg, 2 env (e10). */
static af_ring_t *ring_of(int ki) {
    return (ki == 0) ? &s_core->raw : (ki == 1) ? &s_core->agg : &s_core->env;
}

static void count_ext_stacks(void) {
    static TaskStatus_t ts[48];
    UBaseType_t n = uxTaskGetSystemState(ts, 48, NULL);
    s_ext_stack_tasks = 0;
    for (UBaseType_t i = 0; i < n; i++) {
        if (esp_ptr_external_ram(ts[i].pxStackBase)) {
            s_ext_stack_tasks++;
        }
    }
    int local_var = 0;
    (void)local_var;
    s_caller_stack_internal = esp_ptr_internal(&local_var);
}

#define EPOCH_PATH "/storage/h/epoch.bin"

/* e8 п.3: эпоха загрузки хранится отдельным файлом (8 байт: epoch и ~epoch), чтобы бамп не терялся при перезагрузке без сброса. 0 - нет файла или он битый. */
static uint32_t epoch_load(void) {
    uint32_t w[2] = {0, 0};
    FILE *f = fopen(EPOCH_PATH, "rb");
    if (!f) return 0;
    size_t rd = fread(w, sizeof(uint32_t), 2, f);
    fclose(f);
    return (rd == 2 && w[1] == ~w[0]) ? w[0] : 0;
}

static void epoch_store(uint32_t e) {
    uint32_t w[2] = {e, ~e};
    FILE *f = fopen(EPOCH_PATH, "wb");
    if (!f) {
        s_flush_errs++;
        return;
    }
    size_t wr = fwrite(w, sizeof(uint32_t), 2, f);
    if (fclose(f) != 0 || wr != 2) s_flush_errs++;
}

#define RD_TRIES 3

/* Чтение сегмента целиком с повтором. 0 - прочитан; 1 - больше img_size (мусор); -1 - ошибка ввода-вывода (e8 п.2, п.10). */
static int read_seg(const char *path, uint8_t *img, size_t img_size, size_t *len) {
    for (int t = 0; t < RD_TRIES; t++) {
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        long sz = (fseek(f, 0, SEEK_END) == 0) ? ftell(f) : -1;
        if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); continue; }
        if ((size_t)sz > img_size) { fclose(f); *len = (size_t)sz; return 1; }
        size_t rd = fread(img, 1, (size_t)sz, f);
        fclose(f);
        if (rd == (size_t)sz) { *len = rd; return 0; }
    }
    return -1;
}

static void info_bad(af_seg_info_t *e, uint32_t idx, int io_err) {
    memset(e, 0, sizeof(*e));
    e->seg_idx = idx;
    e->io_err = (uint8_t)io_err;
}

static int restore_kind(int ki, uint32_t boot_epoch) {
    kstate_t *k = &s_k[ki];
    af_ring_t *ring = ring_of(ki);
    if (!ring->buf) { /* e10: кольцо вида не создано (нет PSRAM) - вид отключён, не ошибка восстановления */
        k->disabled = 1;
        return 0;
    }
    ring->epoch = boot_epoch; /* e8 п.3: epoch не ниже, чем на прошлой загрузке, +1 */

    size_t img_size = 32 + k->seg_cap * k->rec_size + 64;
    uint8_t *img = heap_caps_malloc(img_size, MALLOC_CAP_SPIRAM);
    if (!img) {
        ESP_LOGE(TAG, "restore: PSRAM alloc failed for kind %d", ki);
        k->disabled = 1;
        s_restore_fail++;
        return -1;
    }

    uint32_t n = 0;
    DIR *d = opendir("/storage/h");
    if (!d) {
        ESP_LOGE(TAG, "restore: opendir failed for kind %d", ki);
        heap_caps_free(img);
        k->disabled = 1;
        s_restore_fail++;
        return -1;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < 160) {
        const char *name = e->d_name;
        size_t len = strlen(name);
        if (len != 1 + 8 + 4) continue;
        if (name[0] != k->prefix[0]) continue;
        if (strcmp(name + len - 4, ".seg") != 0) continue;

        int is_hex = 1;
        for (int i = 1; i <= 8; i++) {
            char c = name[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                is_hex = 0;
                break;
            }
        }
        if (!is_hex) continue;

        uint32_t idx = (uint32_t)strtoul(name + 1, NULL, 16);
        memset(&s_info[n], 0, sizeof(s_info[n]));
        s_info[n].seg_idx = idx;
        n++;
    }
    closedir(d);

    // Сортировка по seg_idx (вставками)
    for (uint32_t i = 1; i < n; i++) {
        af_seg_info_t key = s_info[i];
        uint32_t j = i;
        while (j > 0 && s_info[j - 1].seg_idx > key.seg_idx) {
            s_info[j] = s_info[j - 1];
            j--;
        }
        s_info[j] = key;
    }

    // Один проход чтения и загрузки
    uint32_t run_end = 0;
    int run_open = 0;
    int hole = 0;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = s_info[i].seg_idx;
        char path[64];
        seg_path(k, idx, path, sizeof(path));

        size_t flen = 0;
        int rr = read_seg(path, img, img_size, &flen);
        if (rr != 0) {
            info_bad(&s_info[i], idx, rr < 0);
            hole = 1;
            continue;
        }

        af_seg_scan_t sc;
        memset(&sc, 0, sizeof(sc));
        int rc = af_seg_scan(img, flen, &sc);
        
        s_info[i].seg_idx = idx;
        s_info[i].io_err = 0;
        s_info[i].hdr_ok = (rc == 0 && sc.hdr.kind == k->kind && sc.hdr.seg_idx == idx);
        s_info[i].first_seq = sc.hdr.first_seq;
        s_info[i].epoch = sc.hdr.epoch;
        s_info[i].valid = sc.valid;
        s_info[i].tail_bad = sc.tail_bad;

        if (!s_info[i].hdr_ok) {
            hole = 1;
            continue;
        }

        if (run_open && (hole || sc.hdr.first_seq != run_end)) {
            // e16e: прежний план грузил только последний непрерывный участок; здесь сбрасываем при разрыве
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            af_ring_clear(ring);
            xSemaphoreGive(s_mtx);
        }

        run_open = 1;
        hole = 0;
        run_end = sc.hdr.first_seq + sc.valid;

        xSemaphoreTake(s_mtx, portMAX_DELAY);
        for (uint32_t j = 0; j < sc.valid; j++) {
            af_ring_push(ring, img + AF_SEG_HDR_SIZE + (size_t)j * k->rec_size);
        }
        xSemaphoreGive(s_mtx);
    }

    af_restore_plan_t p;
    af_restore_plan(s_info, n, ring->cap_max, k->seg_cap, &p);

    /* e8 п.2: ошибка ввода-вывода - не повод удалять историю; битый заголовок удаляется как раньше */
    int io_err_any = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s_info[i].io_err) io_err_any = 1;
    }
    for (uint32_t i = 0; i < n; i++) {
        const af_seg_info_t *e = &s_info[i];
        int corrupt = !e->hdr_ok && !e->io_err;
        int stale = e->hdr_ok && p.have_data && e->seg_idx < p.keep_from_idx && !io_err_any;
        if (corrupt || stale) {
            char path[64];
            seg_path(k, e->seg_idx, path, sizeof(path));
            unlink(path);
        }
    }

    if (p.have_data) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        af_persist_ring_finish(ring, p.end_seq, p.epoch > boot_epoch ? p.epoch : boot_epoch);
        xSemaphoreGive(s_mtx);
    }
    if (p.have_data) {
        k->persisted_seq = p.end_seq;
        k->restored = ring->count;
        k->next_idx = p.next_seg_idx;
        k->oldest_idx = io_err_any ? s_info[0].seg_idx : p.keep_from_idx;
        k->cur_idx = s_info[p.last_pos].seg_idx;
        k->cur_first = s_info[p.last_pos].first_seq;
        k->cur_valid = s_info[p.last_pos].valid;
        k->cur_open = p.append_last;
    } else {
        k->persisted_seq = 0;
        k->restored = 0;
        k->next_idx = p.next_seg_idx; /* e8 п.2/п.4: не занимать номера уцелевших файлов */
        k->oldest_idx = io_err_any ? s_info[0].seg_idx : p.next_seg_idx;
        k->cur_idx = 0;
        k->cur_first = 0;
        k->cur_valid = 0;
        k->cur_open = 0;
    }

    s_restore_gap += p.dropped_gap;
    if (p.tail_bad_any) {
        s_restore_tail_bad++;
    }

    heap_caps_free(img);

    ESP_LOGI(TAG, "restore kind %d: recs=%" PRIu32 " end_seq=%" PRIu32 " epoch=%" PRIu32 " gap=%" PRIu32 " tail_bad=%d",
             ki, p.total_recs, p.end_seq, p.epoch, p.dropped_gap, p.tail_bad_any);
    return 0;
}

esp_err_t af_pfs_restore(void) {
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    if (info_ensure() != 0) {
        ESP_LOGE(TAG, "restore: PSRAM alloc failed for segment table");
        s_restore_fail++;
        return ESP_ERR_NO_MEM;
    }
    int64_t t0 = esp_timer_get_time();
    clr_recover(); /* e16: дозавершить прерванную очистку до чтения сегментов */
    uint32_t boot_epoch = epoch_load() + 1;
    int rc0 = restore_kind(0, boot_epoch);
    int rc1 = restore_kind(1, boot_epoch);
    int rc2 = restore_kind(2, boot_epoch);
    uint32_t ep = s_core->raw.epoch > s_core->agg.epoch ? s_core->raw.epoch : s_core->agg.epoch;
    if (s_core->env.buf && s_core->env.epoch > ep) ep = s_core->env.epoch;
    epoch_store(ep);
    int64_t t1 = esp_timer_get_time();
    s_restore_ms = (uint32_t)((t1 - t0) / 1000);
    af_log_event(1, "hist", "restored raw=%u agg=%u env=%u epoch=%u ms=%u", (unsigned)s_core->raw.count, (unsigned)s_core->agg.count, (unsigned)s_core->env.count, (unsigned)s_core->raw.epoch, (unsigned)s_restore_ms);
    if (s_restore_fail || s_restore_gap || s_restore_tail_bad) af_log_event(2, "hist", "restore gap=%u tail_bad=%u fail=%u", (unsigned)s_restore_gap, (unsigned)s_restore_tail_bad, (unsigned)s_restore_fail);

    count_ext_stacks();

    return (rc0 == 0 && rc1 == 0 && rc2 == 0) ? ESP_OK : ESP_FAIL;
}

static int flush_kind(int ki) {
    kstate_t *k = &s_k[ki];
    af_ring_t *ring = ring_of(ki);

    if (k->disabled) return -1; /* e8 п.4: восстановление не удалось, файлы не трогаем */
    if ((ki == 0 && !s_core->en_raw) || (ki == 1 && !s_core->en_agg) || (ki == 2 && !s_core->en_env)) return 0; /* b25 F11: вид выключен пользователем */

    while (1) {
        uint32_t pending;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        pending = ring->seq - k->persisted_seq;
        if (pending > ring->count) {
            k->persisted_seq = af_ring_oldest_seq(ring);
            k->cur_open = 0;
            pending = ring->seq - k->persisted_seq;
        }
        xSemaphoreGive(s_mtx);

        if (pending == 0) break;

        uint32_t chunk = af_flush_chunk(pending, k->cur_valid, k->seg_cap);

        if (!k->cur_open || chunk == 0) {
            uint32_t idx = k->next_idx; /* e8 п.1: занимается только после успешного создания */
            char path[64];
            seg_path(k, idx, path, sizeof(path));

            FILE *f = fopen(path, "wb");
            if (!f) {
                s_flush_errs++;
                return -1;
            }

            uint32_t epoch;
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            epoch = ring->epoch;
            xSemaphoreGive(s_mtx);

            af_seg_hdr_t h = {
                .kind = k->kind,
                .rec_size = k->rec_size,
                .seg_idx = idx,
                .first_seq = k->persisted_seq,
                .epoch = epoch,
                .cap_recs = k->seg_cap
            };

            uint8_t hdr_buf[32];
            af_seg_hdr_encode(&h, hdr_buf);

            size_t hw = fwrite(hdr_buf, 1, 32, f);
            int hc = fclose(f);
            if (hw != 32 || hc != 0) { /* e8 п.7: заголовок не сохранён - не продолжать в этот файл */
                unlink(path);
                s_flush_errs++;
                return -1;
            }
            k->next_idx = idx + 1;

            k->cur_idx = idx;
            k->cur_first = k->persisted_seq;
            k->cur_valid = 0;
            k->cur_open = 1;

            while (k->next_idx - k->oldest_idx > k->keep) {
                char old_path[64];
                seg_path(k, k->oldest_idx, old_path, sizeof(old_path));
                unlink(old_path);
                k->oldest_idx++;
            }
        }

        chunk = af_flush_chunk(pending, k->cur_valid, k->seg_cap);
        if (chunk == 0) break;

        char path[64];
        seg_path(k, k->cur_idx, path, sizeof(path));

        FILE *f = fopen(path, "ab");
        if (!f) {
            s_flush_errs++;
            return -1;
        }

        uint32_t done = 0;
        int err = 0;

        while (done < chunk) {
            uint32_t m = chunk - done;
            if (m > 64) m = 64;

            uint32_t next_seq;
            uint32_t lost;
            uint32_t got;

            xSemaphoreTake(s_mtx, portMAX_DELAY);
            got = af_ring_read_since(ring, k->persisted_seq + done, s_chunk, m, &next_seq, &lost);
            xSemaphoreGive(s_mtx);

            if (lost != 0 || got == 0) {
                k->cur_open = 0;
                break;
            }

            if (fwrite(s_chunk, k->rec_size, got, f) != got) {
                err = 1;
                break;
            }

            done += got;
        }

        if (fclose(f) != 0) {
            err = 1;
        }

        if (!err && done == 0) {
            return -1;
        }

        if (err) {
            s_flush_errs++;

            size_t img_size = 32 + k->seg_cap * k->rec_size + 64;
            uint8_t *img = heap_caps_malloc(img_size, MALLOC_CAP_SPIRAM);
            if (img) {
                FILE *rf = fopen(path, "rb");
                if (rf) {
                    fseek(rf, 0, SEEK_END);
                    size_t flen = ftell(rf);
                    fseek(rf, 0, SEEK_SET);
                    if (flen > img_size) flen = img_size;
                    size_t rd = fread(img, 1, flen, rf);
                    fclose(rf);

                    if (rd >= 32) {
                        af_seg_scan_t sc;
                        if (af_seg_scan(img, rd, &sc) == 0) {
                            k->cur_valid = sc.valid;
                        } else {
                            k->cur_valid = 0;
                        }
                    } else {
                        k->cur_valid = 0;
                    }
                }
                heap_caps_free(img);
            }

            k->persisted_seq = k->cur_first + k->cur_valid;
            k->cur_open = 0;
            return -1;
        }

        k->persisted_seq += done;
        k->cur_valid += done;
        s_bytes_written += done * k->rec_size;
    }

    return 0;
}

static void flush_all(void) {
    if (xSemaphoreTake(s_flush_mtx, pdMS_TO_TICKS(5000)) != pdTRUE) {
        s_flush_errs++; /* e8 п.11: пропуск сброса виден в статусе, а не молчаливый */
        ESP_LOGE(TAG, "flush skipped: flush mutex busy");
        return;
    }

    int64_t t0 = esp_timer_get_time();

    uint32_t errs0 = s_flush_errs;
    flush_kind(0);
    flush_kind(1);
    flush_kind(2);
    if (s_flush_errs != errs0) af_log_event(3, "flash", "history flush errors +%u (total %u)", (unsigned)(s_flush_errs - errs0), (unsigned)s_flush_errs);

    int64_t t1 = esp_timer_get_time();
    uint32_t dt_ms = (uint32_t)((t1 - t0) / 1000);

    s_flushes++;
    s_last_flush_ms = dt_ms;
    if (dt_ms > s_max_flush_ms) {
        s_max_flush_ms = dt_ms;
    }
    s_last_flush_s = esp_timer_get_time() / 1000000;

    int local_var = 0;
    (void)local_var;
    s_flush_stack_internal = esp_ptr_internal(&local_var);

    uint32_t raw_persisted, agg_persisted, env_persisted;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    raw_persisted = s_k[0].persisted_seq;
    agg_persisted = s_k[1].persisted_seq;
    env_persisted = s_k[2].persisted_seq;
    xSemaphoreGive(s_mtx);

    ESP_LOGI(TAG, "flush: raw persisted=%" PRIu32 " agg persisted=%" PRIu32 " env persisted=%" PRIu32 " ms=%" PRIu32 "", raw_persisted, agg_persisted, env_persisted, dt_ms);

    xSemaphoreGive(s_flush_mtx);
}

/* --- Очистка истории (clear history) --- */

static int clr_hdr_read(const char *path, af_seg_hdr_t *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t buf[32];
    size_t rd = fread(buf, 1, AF_SEG_HDR_SIZE, f);
    fclose(f);
    if (rd != AF_SEG_HDR_SIZE) return -1;
    return (af_seg_hdr_decode(buf, h) == 0) ? 0 : -1;
}

static int clr_purge_kind(const kstate_t *k) {
    if (info_ensure() != 0) return 1;
    for (int pass = 0; pass < 8; pass++) {
        uint32_t n = 0;
        DIR *d = opendir("/storage/h");
        if (!d) return 1;
        struct dirent *e;
        while ((e = readdir(d)) != NULL && n < 160) {
            const char *name = e->d_name;
            size_t len = strlen(name);
            if (len != 13) continue;
            if (name[0] != k->prefix[0]) continue;
            if (strcmp(name + 9, ".seg") != 0) continue;
            int is_hex = 1;
            for (int i = 1; i <= 8; i++) {
                char c = name[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                    is_hex = 0;
                    break;
                }
            }
            if (!is_hex) continue;
            s_info[n++].seg_idx = (uint32_t)strtoul(name + 1, NULL, 16);
        }
        closedir(d);
        if (n == 0) return 0;
        for (uint32_t i = 0; i < n; i++) {
            char path[64];
            seg_path(k, s_info[i].seg_idx, path, sizeof(path));
            unlink(path);
        }
    }
    /* Проверка остатков */
    DIR *d = opendir("/storage/h");
    if (!d) return 1;
    int remaining = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        size_t len = strlen(name);
        if (len != 13) continue;
        if (name[0] != k->prefix[0]) continue;
        if (strcmp(name + 9, ".seg") != 0) continue;
        int is_hex = 1;
        for (int i = 1; i <= 8; i++) {
            char c = name[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                is_hex = 0;
                break;
            }
        }
        if (is_hex) remaining++;
    }
    closedir(d);
    return remaining;
}

static int clr_make_hdr_seg(kstate_t *k, uint32_t idx, uint32_t first_seq, uint32_t epoch) {
    char path[64];
    seg_path(k, idx, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) {
        s_flush_errs++;
        return -1;
    }
    af_seg_hdr_t h = {
        .kind = k->kind,
        .rec_size = k->rec_size,
        .seg_idx = idx,
        .first_seq = first_seq,
        .epoch = epoch,
        .cap_recs = k->seg_cap
    };
    uint8_t hdr_buf[32];
    af_seg_hdr_encode(&h, hdr_buf);
    size_t hw = fwrite(hdr_buf, 1, 32, f);
    int hc = fclose(f);
    if (hw != 32 || hc != 0) {
        unlink(path);
        s_flush_errs++;
        return -1;
    }
    return 0;
}

static int clr_marker_write(const af_clr_marker_t *m) {
    uint8_t buf[AF_CLR_SIZE];
    af_clr_marker_encode(m, buf);
    FILE *f = fopen("/storage/h/clr.bin", "wb");
    if (!f) return -1;
    size_t wr = fwrite(buf, 1, AF_CLR_SIZE, f);
    int hc = fclose(f);
    if (wr != AF_CLR_SIZE || hc != 0) {
        unlink("/storage/h/clr.bin");
        return -1;
    }
    return 0;
}

static void clr_marker_remove(void) {
    unlink("/storage/h/clr.bin");
}

static void clr_recover(void) {
    uint8_t buf[AF_CLR_SIZE + 1];
    FILE *f = fopen("/storage/h/clr.bin", "rb");
    if (!f) return;
    size_t rd = fread(buf, 1, AF_CLR_SIZE + 1, f);
    fclose(f);
    if (rd != AF_CLR_SIZE) {
        unlink("/storage/h/clr.bin");
        return;
    }
    af_clr_marker_t m;
    if (af_clr_marker_decode(buf, AF_CLR_SIZE, &m) != 0) {
        unlink("/storage/h/clr.bin");
        return;
    }

    int all_ok = 1;
    for (int ki = 0; ki < 3; ki++) {
        if (!(m.mask & (1u << ki))) continue;
        af_ring_t *ring = ring_of(ki);
        if (!ring->buf) continue;

        /* Проверка, завершена ли очистка для этого вида */
        int complete = 0;
        DIR *d = opendir("/storage/h");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                const char *name = e->d_name;
                size_t len = strlen(name);
                if (len != 13) continue;
                if (name[0] != s_k[ki].prefix[0]) continue;
                if (strcmp(name + 9, ".seg") != 0) continue;
                int is_hex = 1;
                for (int i = 1; i <= 8; i++) {
                    char c = name[i];
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                        is_hex = 0;
                        break;
                    }
                }
                if (!is_hex) continue;
                uint32_t idx = (uint32_t)strtoul(name + 1, NULL, 16);
                char path[64];
                seg_path(&s_k[ki], idx, path, sizeof(path));
                af_seg_hdr_t h;
                if (clr_hdr_read(path, &h) == 0) {
                    uint32_t ep = h.epoch;
                    uint32_t fs = h.first_seq;
                    if (af_clr_kind_done(&m, ki, &ep, &fs, 1)) {
                        complete = 1;
                        break;
                    }
                }
            }
            closedir(d);
        }

        if (complete) continue;

        /* Очистка и создание заголовка */
        if (clr_purge_kind(&s_k[ki]) != 0) {
            all_ok = 0;
            continue;
        }
        if (clr_make_hdr_seg(&s_k[ki], 0, m.seq[ki], m.epoch) != 0) {
            all_ok = 0;
            continue;
        }
    }

    if (!all_ok) return;

    if (epoch_load() < m.epoch) {
        epoch_store(m.epoch);
    }
    clr_marker_remove();
}

int af_pfs_clear(uint32_t mask, af_pfs_clear_res_t *res) {
    if (!res || !s_core || !s_mtx) return -1;
    memset(res, 0, sizeof(*res));
    mask &= AF_CLR_MASK_ALL;
    if (mask == 0) return -1;

    if (xSemaphoreTake(s_flush_mtx, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return -1;
    }

    af_clr_marker_t m;
    memset(&m, 0, sizeof(m));
    uint32_t maxep = 0;
    uint32_t flash_mask = 0;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (int ki = 0; ki < 3; ki++) {
        if (!(mask & (1u << ki))) continue;
        af_ring_t *ring = ring_of(ki);
        if (!ring->buf) {
            mask &= ~(1u << ki);
            continue;
        }
        m.seq[ki] = ring->seq;
        if (ring->epoch > maxep) maxep = ring->epoch;
        if (!s_k[ki].disabled) {
            flash_mask |= (1u << ki);
        }
    }
    if (mask == 0) {
        xSemaphoreGive(s_mtx);
        xSemaphoreGive(s_flush_mtx);
        return 0;
    }
    if (!s_mounted) flash_mask = 0; /* ОЗУ-режим: файлов нет */
    m.mask = flash_mask;
    m.epoch = maxep + 1;
    xSemaphoreGive(s_mtx);

    int all_ok = 1;
    uint32_t hdr_idx[3] = {0, 0, 0};
    int ok_k[3] = {0, 0, 0};

    if (s_mounted && flash_mask != 0) {
        if (clr_marker_write(&m) != 0) {
            xSemaphoreGive(s_flush_mtx);
            return -3;
        }
    }

    for (int ki = 0; ki < 3; ki++) {
        if (!(flash_mask & (1u << ki))) continue;
        int ok = (clr_purge_kind(&s_k[ki]) == 0);
        if (ok) {
            uint32_t idx = s_k[ki].next_idx;
            ok = (clr_make_hdr_seg(&s_k[ki], idx, m.seq[ki], m.epoch) == 0);
            if (ok) hdr_idx[ki] = idx;
        }
        ok_k[ki] = ok;
        if (!ok) all_ok = 0;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (int ki = 0; ki < 3; ki++) {
        if (!(mask & (1u << ki))) continue;
        af_ring_t *ring = ring_of(ki);
        if (!ring->buf) continue;

        uint32_t S = m.seq[ki];
        uint32_t keep = ring->seq - S;
        if (keep > ring->count) keep = ring->count;
        uint32_t drop = ring->count - keep;
        res->removed[ki] = drop;
        ring->tail = (ring->tail + drop) % ring->cap_max;
        ring->count = keep;
        ring->epoch = m.epoch;

        s_k[ki].persisted_seq = S;
        s_k[ki].restored = 0;

        if (flash_mask & (1u << ki)) {
            if (ok_k[ki]) {
                s_k[ki].cur_idx = hdr_idx[ki];
                s_k[ki].cur_first = S;
                s_k[ki].cur_valid = 0;
                s_k[ki].cur_open = 1;
                s_k[ki].oldest_idx = hdr_idx[ki];
                s_k[ki].next_idx = hdr_idx[ki] + 1;
            } else {
                s_k[ki].cur_open = 0;
                s_k[ki].cur_valid = 0;
                s_k[ki].oldest_idx = s_k[ki].next_idx;
            }
        } else {
            s_k[ki].cur_open = 0;
        }

        res->seq[ki] = ring->seq;
        res->epoch[ki] = ring->epoch;
    }

    uint32_t ep = 0;
    for (int i = 0; i < 3; i++) {
        af_ring_t *r = ring_of(i);
        if (r->buf && r->epoch > ep) ep = r->epoch;
    }
    xSemaphoreGive(s_mtx);

    if (s_mounted && flash_mask != 0) {
        epoch_store(ep);
        if (all_ok) {
            clr_marker_remove();
        }
    }

    res->persisted = (s_mounted && flash_mask == mask && all_ok) ? 1 : 0;
    xSemaphoreGive(s_flush_mtx);

    ESP_LOGI(TAG, "clear: mask=%" PRIu32 " removed raw=%" PRIu32 " agg=%" PRIu32 " env=%" PRIu32,
             mask, res->removed[0], res->removed[1], res->removed[2]);

    return (all_ok || !s_mounted || flash_mask == 0) ? 0 : -2;
}
void af_pfs_tick(void) {
    if (!s_mounted || !s_core) return;

    uint32_t pending_raw, pending_agg, pending_env;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    pending_raw = s_core->raw.seq - s_k[0].persisted_seq;
    if (pending_raw > s_core->raw.count) pending_raw = s_core->raw.count;
    if (s_k[0].disabled || !s_core->en_raw) pending_raw = 0;

    pending_agg = s_core->agg.seq - s_k[1].persisted_seq;
    if (pending_agg > s_core->agg.count) pending_agg = s_core->agg.count;
    if (s_k[1].disabled || !s_core->en_agg) pending_agg = 0;

    pending_env = s_core->env.seq - s_k[2].persisted_seq;
    if (pending_env > s_core->env.count) pending_env = s_core->env.count;
    if (s_k[2].disabled || !s_core->en_env) pending_env = 0;
    xSemaphoreGive(s_mtx);

    uint32_t pending = pending_raw + pending_agg + pending_env;
    int64_t now_s = esp_timer_get_time() / 1000000;

    if (af_flush_due(now_s, s_last_flush_s, AF_FLUSH_INTERVAL_S, pending)) {
        flush_all();
    }
}

void af_pfs_flush_now(void) {
    if (s_mounted) {
        flush_all();
    }
}

int af_pfs_status_json(char *buf, size_t cap) {
    if (!s_core || !s_mtx) {
        int l0 = snprintf(buf, cap, "{\"mounted\":0}");
        return (l0 < 0 || (size_t)l0 >= cap) ? -1 : l0;
    }
    count_ext_stacks();

    uint32_t raw_persisted, raw_pending, raw_restored, raw_files, raw_cur_valid;
    uint32_t agg_persisted, agg_pending, agg_restored, agg_files, agg_cur_valid;
    uint32_t env_persisted, env_pending, env_restored, env_files, env_cur_valid;

    int got_mtx = (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(200)) == pdTRUE);
    raw_persisted = s_k[0].persisted_seq;
    raw_pending = s_core->raw.seq - s_k[0].persisted_seq;
    if (raw_pending > s_core->raw.count) raw_pending = s_core->raw.count;
    raw_restored = s_k[0].restored;
    raw_files = s_k[0].next_idx - s_k[0].oldest_idx;
    raw_cur_valid = s_k[0].cur_valid;

    agg_persisted = s_k[1].persisted_seq;
    agg_pending = s_core->agg.seq - s_k[1].persisted_seq;
    if (agg_pending > s_core->agg.count) agg_pending = s_core->agg.count;
    agg_restored = s_k[1].restored;
    agg_files = s_k[1].next_idx - s_k[1].oldest_idx;
    agg_cur_valid = s_k[1].cur_valid;

    env_persisted = s_k[2].persisted_seq;
    env_pending = s_core->env.seq - s_k[2].persisted_seq;
    if (env_pending > s_core->env.count) env_pending = s_core->env.count;
    env_restored = s_k[2].restored;
    env_files = s_k[2].next_idx - s_k[2].oldest_idx;
    env_cur_valid = s_k[2].cur_valid;
    if (got_mtx) xSemaphoreGive(s_mtx);

    size_t fs_total = 0, fs_used = 0;
    if (s_mounted) {
        af_fsinfo_get(&fs_total, &fs_used);   /* e16e: общий кэш 30 с */
    }

    int64_t now_s = esp_timer_get_time() / 1000000;
    uint32_t since_flush_s = (uint32_t)(now_s - s_last_flush_s);

    int len = snprintf(buf, cap,
        "{\"mounted\":%d,\"formatted\":%d,\"flushes\":%" PRIu32 ",\"flush_errs\":%" PRIu32 ",\"bytes_written\":%" PRIu32 ","
        "\"last_flush_ms\":%" PRIu32 ",\"max_flush_ms\":%" PRIu32 ",\"since_flush_s\":%" PRIu32 ",\"restore_ms\":%" PRIu32 ","
        "\"restore_gap\":%" PRIu32 ",\"restore_tail_bad\":%" PRIu32 ",\"restore_fail\":%" PRIu32 ",\"ext_stack_tasks\":%d,\"flush_stack_internal\":%d,"
        "\"raw\":{\"persisted_seq\":%" PRIu32 ",\"pending\":%" PRIu32 ",\"restored\":%" PRIu32 ",\"files\":%" PRIu32 ",\"cur_valid\":%" PRIu32 "},"
        "\"agg\":{\"persisted_seq\":%" PRIu32 ",\"pending\":%" PRIu32 ",\"restored\":%" PRIu32 ",\"files\":%" PRIu32 ",\"cur_valid\":%" PRIu32 "},"
        "\"env\":{\"persisted_seq\":%" PRIu32 ",\"pending\":%" PRIu32 ",\"restored\":%" PRIu32 ",\"files\":%" PRIu32 ",\"cur_valid\":%" PRIu32 "},"
        "\"fs_total\":%" PRIu32 ",\"fs_used\":%" PRIu32 "}",
        s_mounted, s_formatted, s_flushes, s_flush_errs, s_bytes_written,
        s_last_flush_ms, s_max_flush_ms, since_flush_s, s_restore_ms,
        s_restore_gap, s_restore_tail_bad, s_restore_fail, s_ext_stack_tasks, s_flush_stack_internal,
        raw_persisted, raw_pending, raw_restored, raw_files, raw_cur_valid,
        agg_persisted, agg_pending, agg_restored, agg_files, agg_cur_valid,
        env_persisted, env_pending, env_restored, env_files, env_cur_valid,
        (uint32_t)fs_total, (uint32_t)fs_used);

    if (len < 0 || (size_t)len >= cap) {
        return -1;
    }
    return len;
}

int af_pfs_mounted(void)
{
    return s_mounted;
}
