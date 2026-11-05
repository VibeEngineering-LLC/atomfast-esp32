#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>

#include "af_log_api.h"
#include "af_fsinfo.h"

#ifndef AF_LOG_HOST
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif


static af_log_core_t lg_core;
static af_log_ent_t *lg_ring;
static SemaphoreHandle_t lg_mtx;
static volatile int lg_inited = 0;
static volatile int lg_fs_ready = 0;
static volatile int lg_in_flush = 0;

static uint32_t lg_f[AF_LOG_MAX_FILES + 2];
static uint32_t lg_f_bytes[AF_LOG_MAX_FILES + 2];
static uint32_t lg_nf = 0;

static uint32_t lg_retry_s = 0;
static uint32_t lg_last_flush_s = 0;
static uint32_t lg_fail_n = 0;
static uint32_t lg_wr_err = 0;
static uint32_t lg_dropped_space = 0;
static uint32_t lg_flushes = 0;
static uint32_t lg_dropped_mtx = 0;

static volatile uint32_t lg_af_n = 0;
static volatile uint32_t lg_af_size = 0;
static volatile uint32_t lg_af_caps = 0;
static uint32_t lg_af_logged_n = 0;
static uint32_t lg_af_logged_s = 0;

static char lg_chunk[384];
static int lg_boot_flushed = 0;

static void lg_path(char *out, size_t cap, uint32_t first_seq)
{
    snprintf(out, cap, "/storage/l/g%08" PRIx32 ".log", first_seq);
}

static int lg_load_tail(uint32_t first_seq, uint32_t *last_seq)
{
    char path[64];
    lg_path(path, sizeof(path), first_seq);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return -1; }
    size_t rd = (sz > 512) ? 512 : (size_t)sz;
    fseek(f, (long)(sz - rd), SEEK_SET);
    char tail[512];
    size_t got = fread(tail, 1, rd, f);
    fclose(f);
    if (got == 0) return -1;
    return af_log_last_seq(tail, (int)got, (sz <= 512), last_seq);
}

static void lg_drop_oldest(uint32_t n)
{
    if (n == 0 || n > lg_nf) return;
    for (uint32_t i = 0; i < n; i++) {
        char path[64];
        lg_path(path, sizeof(path), lg_f[i]);
        unlink(path);
    }
    memmove(lg_f, lg_f + n, (lg_nf - n) * sizeof(uint32_t));
    memmove(lg_f_bytes, lg_f_bytes + n, (lg_nf - n) * sizeof(uint32_t));
    lg_nf -= n;
}

static int lg_fs_init(void)
{
    if (mkdir("/storage/l", 0777) != 0 && errno != EEXIST) return -1;
    DIR *d = opendir("/storage/l");
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        uint32_t fs;
        if (af_log_seg_parse(de->d_name, &fs) != 0) continue;
        int found = 0;
        for (uint32_t i = 0; i < lg_nf; i++) {
            if (lg_f[i] == fs) { found = 1; break; }
        }
        if (found) continue;
        if (lg_nf >= AF_LOG_MAX_FILES + 2) {
            lg_drop_oldest(1);
        }
        int pos = (int)lg_nf;
        for (int i = 0; i < (int)lg_nf; i++) {
            if (fs < lg_f[i]) { pos = i; break; }
        }
        memmove(&lg_f[pos + 1], &lg_f[pos], (lg_nf - pos) * sizeof(uint32_t));
        memmove(&lg_f_bytes[pos + 1], &lg_f_bytes[pos], (lg_nf - pos) * sizeof(uint32_t));
        lg_f[pos] = fs;
        lg_f_bytes[pos] = 0;
        lg_nf++;
    }
    closedir(d);

    for (uint32_t i = 0; i < lg_nf; i++) {
        char path[64];
        lg_path(path, sizeof(path), lg_f[i]);
        FILE *f = fopen(path, "rb");
        if (!f) { lg_f_bytes[i] = 0; continue; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fclose(f);
        lg_f_bytes[i] = (sz > 0) ? (uint32_t)sz : 0;
    }

    uint32_t last = 0;
    if (lg_nf > 0) {
        uint32_t newest = lg_f[lg_nf - 1];
        if (lg_load_tail(newest, &last) != 0) {
            if (lg_nf > 1) {
                uint32_t prev = lg_f[lg_nf - 2];
                if (lg_load_tail(prev, &last) != 0) {
                    if (newest > 0) last = newest - 1;
                }
            } else {
                if (newest > 0) last = newest - 1;
            }
        }
        char path[64];
        lg_path(path, sizeof(path), newest);
        FILE *f = fopen(path, "rb");
        if (f) {
            fseek(f, -1, SEEK_END);
            int c = fgetc(f);
            fclose(f);
            if (c != '\n' && c != EOF) {
                FILE *fa = fopen(path, "ab");
                if (fa) {
                    fputc('\n', fa);
                    fclose(fa);
                    lg_f_bytes[lg_nf - 1]++;
                }
            }
        }
    }

    if (last > 0) {
        if (xSemaphoreTake(lg_mtx, pdMS_TO_TICKS(200)) != pdTRUE) return -1;
        af_log_rebase(&lg_core, last);
        xSemaphoreGive(lg_mtx);
    }
    lg_fs_ready = 1;
    return 0;
}

static int lg_flush(void)
{
    if (__atomic_exchange_n(&lg_in_flush, 1, __ATOMIC_SEQ_CST)) return 0;
    int result = 0;
    uint32_t dropped = 0;

    if (xSemaphoreTake(lg_mtx, pdMS_TO_TICKS(200)) != pdTRUE) {
        __atomic_store_n(&lg_in_flush, 0, __ATOMIC_SEQ_CST);
        return -1;
    }
    uint32_t from = lg_core.flushed;
    uint32_t upto = af_log_snapshot(&lg_core);
    xSemaphoreGive(lg_mtx);

    if (from >= upto) {
        __atomic_store_n(&lg_in_flush, 0, __ATOMIC_SEQ_CST);
        return 0;
    }

    size_t fs_total = 0, fs_used = 0;
    if (af_fsinfo_get(&fs_total, &fs_used) != ESP_OK) {
        if (xSemaphoreTake(lg_mtx, pdMS_TO_TICKS(200)) == pdTRUE) {
            af_log_abort_flush(&lg_core);
            xSemaphoreGive(lg_mtx);
        }
        lg_fail_n++;
        lg_wr_err++;
        lg_last_flush_s = (uint32_t)(esp_timer_get_time() / 1000000);
        __atomic_store_n(&lg_in_flush, 0, __ATOMIC_SEQ_CST);
        af_log_event(AF_LOG_ERROR, "log", "flash write failed rc=-1");
        return -1;
    }

    uint32_t seq = from;
    uint32_t done_upto = from;
    uint32_t chunk_first_seq = 0;
    uint32_t chunk_lines = 0;

    while (seq < upto) {
        size_t n = 0;
        if (xSemaphoreTake(lg_mtx, pdMS_TO_TICKS(200)) != pdTRUE) {
            result = -1;
            break;
        }
        while (seq < upto && n + AF_LOG_LINE_MAX <= sizeof(lg_chunk)) {
            const af_log_ent_t *e = af_log_get(&lg_core, seq);
            if (e) {
                int l = af_log_fmt_store(e, lg_chunk + n, sizeof(lg_chunk) - n);
                if (l > 0) {
                    if (n == 0) chunk_first_seq = e->seq;
                    n += (size_t)l;
                    chunk_lines++;
                }
            }
            seq++;
        }
        xSemaphoreGive(lg_mtx);

        if (n == 0) {
            done_upto = seq;
            continue;
        }

        uint32_t fb[AF_LOG_MAX_FILES + 2];
        for (uint32_t i = 0; i < lg_nf; i++) fb[i] = lg_f_bytes[i];
        af_log_rot_in_t in;
        in.fs_total = (uint32_t)fs_total;
        in.fs_used = (uint32_t)fs_used;
        in.add_bytes = (uint32_t)n;
        in.nfiles = lg_nf;
        in.cur_bytes = (lg_nf > 0) ? lg_f_bytes[lg_nf - 1] : 0;

        uint32_t del_n = 0;
        int new_seg = 0;
        int pr = af_log_rot_plan(&in, fb, &del_n, &new_seg);

        if (pr < 0) {
            dropped += (upto - from);
            done_upto = upto;
            result = -2;
            break;
        }

        if (del_n > 0) {
            uint32_t freed = 0;
            for (uint32_t i = 0; i < del_n; i++) {
                freed += (lg_f_bytes[i] + 4095) & ~4095u;
            }
            lg_drop_oldest(del_n);
            fs_used = (fs_used > freed) ? fs_used - freed : 0;
        }

        if (new_seg) {
            if (lg_nf >= AF_LOG_MAX_FILES + 2) {
                lg_drop_oldest(1);
            }
            lg_f[lg_nf] = chunk_first_seq;
            lg_f_bytes[lg_nf] = 0;
            lg_nf++;
        }

        char path[64];
        lg_path(path, sizeof(path), lg_f[lg_nf - 1]);
        FILE *f = fopen(path, "ab");
        if (!f) {
            result = -1;
            break;
        }
        size_t nw = fwrite(lg_chunk, 1, n, f);
        int cr = fclose(f);
        if (nw != n || cr != 0) {
            result = -1;
            lg_fs_ready = 0;
            break;
        }
        lg_f_bytes[lg_nf - 1] += (uint32_t)n;
        fs_used += (uint32_t)n;
        if (new_seg) fs_used += 4096;
        done_upto = seq;
    }

    if (xSemaphoreTake(lg_mtx, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (done_upto > from) af_log_mark_flushed(&lg_core, done_upto);
        if (done_upto < upto) af_log_abort_flush(&lg_core);
        xSemaphoreGive(lg_mtx);
    } else {
        lg_fail_n++;
    }

    uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000);
    if (result == 0) {
        lg_fail_n = 0;
        lg_flushes++;
    } else {
        lg_fail_n++;
        lg_wr_err++;
        if (result == -2) lg_dropped_space += dropped;
    }
    lg_last_flush_s = now_s;

    __atomic_store_n(&lg_in_flush, 0, __ATOMIC_SEQ_CST);

    if (result == -1) {
        af_log_event(AF_LOG_ERROR, "log", "flash write failed rc=-1");
    } else if (result == -2) {
        af_log_event(AF_LOG_WARN, "log", "no flash space, %u lines dropped", (unsigned)dropped);
    }
    return result;
}

#ifndef AF_LOG_HOST
static void lg_shutdown(void)
{
    af_log_flush_now();
}
#endif

void af_log_init(void)
{
    if (lg_inited) return;
    lg_mtx = xSemaphoreCreateMutex();
    if (!lg_mtx) return;

    lg_ring = (af_log_ent_t *)heap_caps_malloc(AF_LOG_RING_N * sizeof(af_log_ent_t), MALLOC_CAP_SPIRAM);
    uint32_t cap = AF_LOG_RING_N;
    if (!lg_ring) {
        lg_ring = (af_log_ent_t *)heap_caps_malloc(AF_LOG_RING_FALLBACK_N * sizeof(af_log_ent_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        cap = AF_LOG_RING_FALLBACK_N;
    }
    if (!lg_ring) {
        vSemaphoreDelete(lg_mtx);
        lg_mtx = NULL;
        return;
    }

    if (af_log_core_init(&lg_core, lg_ring, cap, 1) != 0) {
        heap_caps_free(lg_ring);
        lg_ring = NULL;
        vSemaphoreDelete(lg_mtx);
        lg_mtx = NULL;
        return;
    }

    lg_inited = 1;

#ifndef AF_LOG_HOST
    esp_register_shutdown_handler(lg_shutdown);
    const char *rname = "other";
    int rlevel = AF_LOG_WARN;
    esp_reset_reason_t rr = esp_reset_reason();
    switch (rr) {
        case ESP_RST_PANIC: rname = "panic"; rlevel = AF_LOG_ERROR; break;
        case ESP_RST_INT_WDT: rname = "int_wdt"; rlevel = AF_LOG_ERROR; break;
        case ESP_RST_TASK_WDT: rname = "task_wdt"; rlevel = AF_LOG_ERROR; break;
        case ESP_RST_WDT: rname = "wdt"; rlevel = AF_LOG_ERROR; break;
        case ESP_RST_BROWNOUT: rname = "brownout"; rlevel = AF_LOG_ERROR; break;
        case ESP_RST_CPU_LOCKUP: rname = "lockup"; rlevel = AF_LOG_ERROR; break;
        case ESP_RST_POWERON: rname = "poweron"; rlevel = AF_LOG_INFO; break;
        case ESP_RST_SW: rname = "sw"; rlevel = AF_LOG_INFO; break;
        case ESP_RST_EXT: rname = "ext"; rlevel = AF_LOG_INFO; break;
        case ESP_RST_DEEPSLEEP: rname = "deepsleep"; rlevel = AF_LOG_INFO; break;
        default: break;
    }
    const esp_app_desc_t *ad = esp_app_get_description();
    char ver[32] = "?";
    if (ad) snprintf(ver, sizeof(ver), "%s", ad->version);
    uint32_t heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    af_log_event(rlevel, "boot", "boot reset=%s fw=%s heap=%u", rname, ver, (unsigned)heap);
#endif
}

void af_log_event(int level, const char *tag, const char *fmt, ...)
{
    if (!lg_inited) return;
    char m[AF_LOG_MSG_MAX + 8];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof(m), fmt, ap);
    va_end(ap);

    uint32_t mono_s = (uint32_t)(esp_timer_get_time() / 1000000);
    time_t t = time(NULL);
    uint32_t utc = (t >= 1700000000) ? (uint32_t)t : 0;

    if (xSemaphoreTake(lg_mtx, pdMS_TO_TICKS(50)) != pdTRUE) {
        __atomic_fetch_add(&lg_dropped_mtx, 1, __ATOMIC_RELAXED);
        return;
    }
    af_log_push(&lg_core, level, tag, m, mono_s, utc);
    xSemaphoreGive(lg_mtx);
}

void af_log_note_alloc_fail(size_t size, uint32_t caps)
{
    __atomic_fetch_add(&lg_af_n, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&lg_af_size, (uint32_t)size, __ATOMIC_RELAXED);
    __atomic_store_n(&lg_af_caps, caps, __ATOMIC_RELAXED);
}

void af_log_tick(void)
{
    if (!lg_inited) return;
    uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000);

    uint32_t n = __atomic_load_n(&lg_af_n, __ATOMIC_RELAXED);
    if (n != lg_af_logged_n && (lg_af_logged_s == 0 || now_s - lg_af_logged_s >= 60)) {
        uint32_t sz = __atomic_load_n(&lg_af_size, __ATOMIC_RELAXED);
        uint32_t cp = __atomic_load_n(&lg_af_caps, __ATOMIC_RELAXED);
        af_log_event(AF_LOG_ERROR, "mem", "alloc_fail x%u last_size=%u caps=0x%x",
                     (unsigned)(n - lg_af_logged_n), (unsigned)sz, (unsigned)cp);
        lg_af_logged_n = n;
        lg_af_logged_s = now_s;
    }

    if (!lg_fs_ready) {
        if (now_s >= lg_retry_s) {
            if (lg_fs_init() != 0) {
                lg_retry_s = now_s + 30;
                return;
            }
            lg_retry_s = 0;
            if (!lg_boot_flushed) { lg_boot_flushed = 1; lg_last_flush_s = now_s - AF_LOG_FLUSH_INTERVAL_S; } /* первая запись сразу: строки загрузки и причина сброса */
        }
        if (!lg_fs_ready) return;
    }

    uint32_t pending = 0;
    int urgent = 0;
    uint32_t cap = 0;
    if (xSemaphoreTake(lg_mtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        pending = af_log_pending(&lg_core);
        urgent = lg_core.urgent;
        cap = lg_core.cap;
        xSemaphoreGive(lg_mtx);
    }

    if (af_log_flush_due(now_s, lg_last_flush_s, pending, urgent, cap, lg_fail_n)) {
        lg_flush();
    }
}

void af_log_flush_now(void)
{
    if (!lg_inited) return;
    if (!lg_fs_ready) {
        lg_fs_init();
    }
    if (lg_fs_ready) {
        lg_flush();
    }
}

int af_log_clear(uint32_t *files, uint32_t *bytes)
{
    if (!lg_inited) return -1;
    uint32_t total_files = 0;
    uint32_t total_bytes = 0;

    if (lg_fs_ready) {
        for (uint32_t i = 0; i < lg_nf; i++) {
            char path[64];
            lg_path(path, sizeof(path), lg_f[i]);
            unlink(path);
            total_files++;
            total_bytes += lg_f_bytes[i];
        }
        lg_nf = 0;

        DIR *d = opendir("/storage/l");
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                uint32_t fs;
                if (af_log_seg_parse(de->d_name, &fs) != 0) continue;
                char path[64];
                lg_path(path, sizeof(path), fs);
                unlink(path);
                total_files++;
            }
            closedir(d);
        }
    }

    if (xSemaphoreTake(lg_mtx, pdMS_TO_TICKS(200)) != pdTRUE) return -1;
    af_log_clear_ram(&lg_core);
    xSemaphoreGive(lg_mtx);

    if (files) *files = total_files;
    if (bytes) *bytes = total_bytes;
    return 0;
}

uint32_t af_log_iter(uint32_t since, int min_level, uint32_t limit, af_log_emit_t cb, void *ctx)
{
    if (!lg_inited || !cb) return 0;
    uint32_t count = 0;
    uint32_t last_emit = since;
    int stop = 0;

    uint32_t fl = 0;
    uint32_t firsts[AF_LOG_MAX_FILES + 2];
    uint32_t nf = 0;
    if (xSemaphoreTake(lg_mtx, pdMS_TO_TICKS(200)) == pdTRUE) {
        fl = lg_core.flushed;
        nf = lg_nf;
        for (uint32_t i = 0; i < nf; i++) firsts[i] = lg_f[i];
        xSemaphoreGive(lg_mtx);
    }

    if (lg_fs_ready && nf > 0) {
        uint32_t idx = af_log_scan_start(firsts, nf, since);
        for (uint32_t i = idx; i < nf && !stop; i++) {
            char path[64];
            lg_path(path, sizeof(path), firsts[i]);
            FILE *f = fopen(path, "rb");
            if (!f) continue;
            char line[AF_LOG_LINE_MAX + 8];
            while (fgets(line, sizeof(line), f) && !stop) {
                af_log_ent_t e;
                if (af_log_parse_store(line, (int)strlen(line), &e) != 0) continue;
                if (e.seq <= last_emit) continue;
                if (e.seq >= fl) continue;
                if (e.level < min_level) continue;
                if (cb(ctx, &e) != 0) { stop = 1; break; }
                last_emit = e.seq;
                count++;
                if (count >= limit) { stop = 1; break; }
            }
            fclose(f);
        }
    }

    if (!stop) {
        uint32_t start_seq = (fl > since) ? fl : (since + 1);
        if (since == UINT32_MAX) start_seq = fl;
        for (;;) {
            af_log_ent_t e;
            int have = 0, done = 0;
            if (xSemaphoreTake(lg_mtx, pdMS_TO_TICKS(200)) != pdTRUE) break;
            uint32_t next_seq = lg_core.next_seq;
            if (next_seq > lg_core.cap && start_seq < next_seq - lg_core.cap) start_seq = next_seq - lg_core.cap;
            if (start_seq >= next_seq) {
                done = 1;
            } else {
                const af_log_ent_t *pe = af_log_get(&lg_core, start_seq);
                if (pe) { e = *pe; have = 1; }
                start_seq++;
            }
            xSemaphoreGive(lg_mtx);
            if (done) break;
            if (!have || e.seq <= last_emit || e.level < min_level) continue;
            if (cb(ctx, &e) != 0) break;
            last_emit = e.seq;
            if (++count >= limit) break;
        }
    }
    return count;
}

void af_log_stats(af_log_stats_t *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    if (!lg_inited) return;
    if (xSemaphoreTake(lg_mtx, pdMS_TO_TICKS(50)) != pdTRUE) return;
    s->next_seq = lg_core.next_seq;
    s->flushed = lg_core.flushed;
    s->lost = lg_core.lost;
    s->suppressed = lg_core.suppressed;
    s->collapsed = lg_core.collapsed;
    s->pending = af_log_pending(&lg_core);
    xSemaphoreGive(lg_mtx);
    s->files = lg_nf;
    uint32_t b = 0;
    for (uint32_t i = 0; i < lg_nf; i++) b += lg_f_bytes[i];
    s->bytes = b;
    s->wr_err = lg_wr_err;
    s->dropped_space = lg_dropped_space;
    s->flushes = lg_flushes;
    s->fail_n = lg_fail_n;
    s->fs_ready = lg_fs_ready;
}
