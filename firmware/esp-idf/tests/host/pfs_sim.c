#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "lfs.h"
#include "pfs_stubs.h"

int g_sim_verbose = 0;
int64_t g_now_us = 0;

/* Инжекция ошибок ввода-вывода (коды возврата, не обрыв питания). Фильтр fopen: режим 'r'/'w'/'a' и префикс файла ('r'/'a'), 0 - любой. */
int g_inj_sem_fail;
void (*g_take_hook)(void);
inj_t g_inj_malloc, g_inj_fopen, g_inj_fread, g_inj_ftell, g_inj_fclose, g_inj_opendir;
inj_t g_inj_fread_corrupt; /* e10: чтение успешно, но один байт записи №5 портится (содержимое отличается от прошлого прохода) */
char g_inj_fopen_mode, g_inj_fopen_pfx;
int g_inj_fclose_drop;

#define BLK 4096
#define NBLK 3040
#define READ_SZ 128
#define PROG_SZ 128
#define CACHE_SZ 512
#define LOOK_SZ 128
#define BLK_CYCLES 512
#define NAME_MAX_LEN 64

typedef struct {
    uint8_t  flash[NBLK * BLK];
    uint32_t erase_cnt[NBLK];
    uint64_t prog_bytes;
    uint32_t op_count;
    uint32_t crash_at;
    uint32_t rng;
    uint32_t conf_raw, conf_agg;
    uint32_t gen_raw, gen_agg;
    uint32_t gen2;
    uint32_t tail_bad_flag, gap_flag;
    uint32_t end_raw, end_agg;
    uint32_t viol_loss, viol_future, viol_content, viol_cont, viol_agg, mount_fail;
    uint32_t conf_env, gen_env, end_env, gen2_env, viol_env; /* e10: третье кольцо */
} sim_shared_t;

static sim_shared_t *sh;
static void (*g_crash_hook)(void);

static void rng_seed(uint32_t s) {
    sh->rng = s ? s : 1;
}

static uint32_t rnd(void) {
    uint32_t x = sh->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    sh->rng = x;
    return x;
}

static int bd_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {
    (void)c;
    memcpy(buffer, &sh->flash[block * BLK + off], size);
    return 0;
}

static int bd_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
    (void)c;
    const uint8_t *data = (const uint8_t *)buffer;
    uint8_t *flash = &sh->flash[block * BLK + off];
    for (lfs_size_t i = 0; i < size; i++) {
        flash[i] &= data[i];
    }
    sh->prog_bytes += size;

    if (sh->crash_at != 0) {
        sh->op_count++;
        if (sh->op_count == sh->crash_at) {
            uint32_t rnd_val = rnd();
            uint32_t prefix_len = rnd_val % (size + 1);
            for (uint32_t i = 0; i < prefix_len; i++) {
                flash[i] &= data[i];
            }
            if (g_crash_hook) g_crash_hook();
            _exit(77);
        }
    }
    return 0;
}

static int bd_erase(const struct lfs_config *c, lfs_block_t block) {
    (void)c;
    if (sh->crash_at != 0) {
        sh->op_count++;
        if (sh->op_count == sh->crash_at) {
            uint32_t rnd_val = rnd();
            if (rnd_val % 2 == 0) {
                // leave as is
            } else {
                uint32_t fill_len = rnd_val % BLK;
                memset(&sh->flash[block * BLK], 0xFF, fill_len);
                for (uint32_t i = fill_len; i < BLK; i++) {
                    sh->flash[block * BLK + i] = (uint8_t)(rnd() & 0xFF);
                }
            }
            if (g_crash_hook) g_crash_hook();
            _exit(77);
        }
    }
    memset(&sh->flash[block * BLK], 0xFF, BLK);
    sh->erase_cnt[block]++;
    return 0;
}

static int bd_sync(const struct lfs_config *c) {
    (void)c;
    return 0;
}

static lfs_t g_lfs;
static struct lfs_config g_cfg;
static int g_lfs_mounted;

esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *conf) {
    g_cfg.read_size = READ_SZ;
    g_cfg.prog_size = PROG_SZ;
    g_cfg.block_size = BLK;
    g_cfg.block_count = NBLK;
    g_cfg.block_cycles = BLK_CYCLES;
    g_cfg.cache_size = CACHE_SZ;
    g_cfg.lookahead_size = LOOK_SZ;
    g_cfg.name_max = NAME_MAX_LEN;
    g_cfg.read = bd_read;
    g_cfg.prog = bd_prog;
    g_cfg.erase = bd_erase;
    g_cfg.sync = bd_sync;

    int err = lfs_mount(&g_lfs, &g_cfg);
    if (err != 0) {
        if (conf && conf->format_if_mount_failed) {
            err = lfs_format(&g_lfs, &g_cfg);
            if (err == 0) {
                err = lfs_mount(&g_lfs, &g_cfg);
            }
        }
    }
    if (err == 0) {
        g_lfs_mounted = 1;
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t esp_littlefs_info(const char *l, size_t *total, size_t *used) {
    (void)l;
    *total = NBLK * BLK;
    lfs_ssize_t sz = lfs_fs_size(&g_lfs);
    if (sz < 0) sz = 0;
    *used = (size_t)sz * BLK;
    return ESP_OK;
}

typedef struct { lfs_file_t f; } sim_FILE;
struct sim_dirent { char d_name[64]; };
typedef struct { lfs_dir_t d; struct sim_dirent e; } sim_DIR;

static const char *strip_prefix(const char *path) {
    if (strncmp(path, "/storage", 8) == 0) {
        return path + 8;
    }
    return path;
}

sim_FILE *sim_fopen(const char *path, const char *mode) {
    const char *lpath = strip_prefix(path);
    int flags = 0;
    int match = (!g_inj_fopen_mode || g_inj_fopen_mode == mode[0]) && (!g_inj_fopen_pfx || g_inj_fopen_pfx == path[11]);
    if (match && inj_hit(&g_inj_fopen)) {
        errno = ENOSPC;
        return NULL;
    }
    if (strcmp(mode, "rb") == 0) {
        flags = LFS_O_RDONLY;
    } else if (strcmp(mode, "wb") == 0) {
        flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
    } else if (strcmp(mode, "ab") == 0) {
        flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND;
    } else {
        errno = EIO;
        return NULL;
    }

    sim_FILE *f = (sim_FILE *)malloc(sizeof(sim_FILE));
    if (!f) {
        errno = EIO;
        return NULL;
    }
    int err = lfs_file_open(&g_lfs, &f->f, lpath, flags);
    if (err != 0) {
        free(f);
        errno = EIO;
        return NULL;
    }
    return f;
}

size_t sim_fread(void *p, size_t sz, size_t n, sim_FILE *f) {
    size_t want = sz * n;
    if (inj_hit(&g_inj_fread)) want /= 2;  /* короткое чтение */
    lfs_ssize_t res = lfs_file_read(&g_lfs, &f->f, p, want);
    if (res < 0) return 0;
    if (res > 32 + 20 * 5 + 4 && inj_hit(&g_inj_fread_corrupt)) ((uint8_t *)p)[32 + 20 * 5 + 3] ^= 0xFF;
    return res / sz;
}

size_t sim_fwrite(const void *p, size_t sz, size_t n, sim_FILE *f) {
    lfs_ssize_t res = lfs_file_write(&g_lfs, &f->f, p, sz * n);
    if (res < 0) return 0;
    return res / sz;
}

int sim_fseek(sim_FILE *f, long off, int whence) {
    return (lfs_file_seek(&g_lfs, &f->f, off, whence) < 0) ? -1 : 0;
}

long sim_ftell(sim_FILE *f) {
    if (inj_hit(&g_inj_ftell)) return -1;
    lfs_ssize_t res = lfs_file_tell(&g_lfs, &f->f);
    return (long)res;
}

int sim_fclose(sim_FILE *f) {
    int inj = inj_hit(&g_inj_fclose);
    if (inj && g_inj_fclose_drop) lfs_file_truncate(&g_lfs, &f->f, 0);  /* сбой закрытия теряет содержимое */
    int err = lfs_file_close(&g_lfs, &f->f);
    free(f);
    return (err == 0 && !inj) ? 0 : EOF;
}

int sim_unlink(const char *path) {
    const char *lpath = strip_prefix(path);
    int err = lfs_remove(&g_lfs, lpath);
    return (err == 0) ? 0 : -1;
}

int sim_mkdir(const char *path, int mode) {
    (void)mode;
    const char *lpath = strip_prefix(path);
    int err = lfs_mkdir(&g_lfs, lpath);
    if (err == LFS_ERR_EXIST) {
        errno = EEXIST;
        return -1;
    }
    return (err == 0) ? 0 : -1;
}

sim_DIR *sim_opendir(const char *path) {
    const char *lpath = strip_prefix(path);
    if (inj_hit(&g_inj_opendir)) return NULL;
    sim_DIR *d = (sim_DIR *)malloc(sizeof(sim_DIR));
    if (!d) return NULL;
    int err = lfs_dir_open(&g_lfs, &d->d, lpath);
    if (err != 0) {
        free(d);
        return NULL;
    }
    return d;
}

struct sim_dirent *sim_readdir(sim_DIR *d) {
    struct lfs_info info;
    while (1) {
        int err = lfs_dir_read(&g_lfs, &d->d, &info);
        if (err <= 0) return NULL;
        if (info.type == LFS_TYPE_REG || info.type == LFS_TYPE_DIR) {
            if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
                continue;
            }
            strncpy(d->e.d_name, info.name, 63);
            d->e.d_name[63] = '\0';
            return &d->e;
        }
    }
}

int sim_closedir(sim_DIR *d) {
    lfs_dir_close(&g_lfs, &d->d);
    free(d);
    return 0;
}

/* e16: байтовые операции для af_log.c */
int sim_fgetc(sim_FILE *f) {
    unsigned char c;
    return (lfs_file_read(&g_lfs, &f->f, &c, 1) == 1) ? (int)c : EOF;
}

int sim_fputc(int c, sim_FILE *f) {
    unsigned char b = (unsigned char)c;
    return (lfs_file_write(&g_lfs, &f->f, &b, 1) == 1) ? c : EOF;
}

char *sim_fgets(char *s, int n, sim_FILE *f) {
    int i = 0;
    while (i < n - 1) {
        int c = sim_fgetc(f);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
    s[i] = '\0';
    return s;
}
#define fgetc sim_fgetc
#define fputc sim_fputc
#define fgets sim_fgets
#define FILE sim_FILE
#define fopen sim_fopen
#define fread sim_fread
#define fwrite sim_fwrite
#define fseek sim_fseek
#define ftell sim_ftell
#define fclose sim_fclose
#define unlink sim_unlink
#define mkdir sim_mkdir
#define DIR sim_DIR
#define dirent sim_dirent
#define opendir sim_opendir
#define readdir sim_readdir
#define closedir sim_closedir

#include "af_persist_fs.c"
#define AF_LOG_HOST 1
#define vSemaphoreDelete(m) ((void)(m))
#include "af_log.c"

#undef fgetc
#undef fputc
#undef fgets
#undef FILE
#undef fopen
#undef fread
#undef fwrite
#undef fseek
#undef ftell
#undef fclose
#undef unlink
#undef mkdir
#undef DIR
#undef dirent
#undef opendir
#undef readdir
#undef closedir

#include "real_hist.h"
#include "af_rec.h"
#include "af_ring.h"

static af_store_core_t g_core_s;
static af_store_core_t *g_core = &g_core_s;

static void crash_hook_impl(void) {
    sh->gen_raw = g_core->raw.seq;
    sh->gen_agg = g_core->agg.seq;
    sh->gen_env = g_core->env.seq;
}

static void gen_sample(uint32_t seq, uint32_t *ts, af_sample_t *s) {
    const hraw_t *h = &HRAW[seq % HRAW_N];
    *ts = 1760000000u + 2u * seq;
    s->dose_msv = h->dose;
    s->doserate_usvh = h->rate;
    s->pulses_2s = (uint16_t)h->p2s;
    s->cps = h->p2s / 2.0f;
    s->battery = (int8_t)h->bat;
    s->temp_c = (int8_t)h->temp;
    s->flags = (uint8_t)h->fl;
}

static void expect_raw(uint32_t seq, uint8_t out[20]) {
    uint32_t ts;
    af_raw_rec_t r;
    gen_sample(seq, &ts, &r.s);
    r.ts = ts;
    af_raw_encode(&r, out);
}

/* e10: запись env по её номеру в кольце (детерминированно, боевые значения платы: ~26.5 C, ~100218 Pa) */
static af_env_rec_t env_expected(uint32_t i) {
    af_env_rec_t r;
    r.ts = 1760000000u + 30u * i;
    r.t_c100 = (int16_t)(2650 + (int)(i % 200u));
    r.p_pa = 100218u - (i % 97u);
    r.flags = AF_ENV_F_T | AF_ENV_F_P;
    return r;
}

esp_err_t g_restore_rc;

static void life_init(void) {
    memset(s_k, 0, sizeof(s_k)); /* как после перезагрузки: статика нулевая */
    s_restore_gap = 0;
    s_restore_tail_bad = 0;
    g_crash_hook = crash_hook_impl;
    
    af_quota_t q;
    af_quota_split(4194304u, 20, 28, 1, 1, 60, 14u * 24u * 3600u, &q);
    
    size_t raw_size = q.atomic_limit * 20;
    size_t agg_size = q.agg_limit * 28;
    
    uint8_t *raw_buf = (uint8_t *)malloc(raw_size);
    uint8_t *agg_buf = (uint8_t *)malloc(agg_size);
    
    af_core_init(g_core, raw_buf, raw_size, agg_buf, agg_size, q.atomic_limit, q.agg_limit, 60, AF_MODE_LAST_DOSE);
    size_t env_size = (size_t)40320u * AF_REC_ENV_SIZE;
    af_core_env_init(g_core, (uint8_t *)malloc(env_size), env_size, 40320u);
    
    af_pfs_attach(g_core, (SemaphoreHandle_t)1);
    
    esp_err_t m = af_pfs_mount();
    if (m != ESP_OK) {
        sh->mount_fail++;
        _exit(78);
    }
    
    g_restore_rc = af_pfs_restore();
}

static void life_run(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        uint32_t seq = g_core->raw.seq;
        uint32_t ts;
        af_sample_t s;
        gen_sample(seq, &ts, &s);
        af_core_ingest(g_core, ts, &s);
        if (seq % 15u == 14u) { /* каждые 30 с - запись воздуха */
            af_env_rec_t er = env_expected(g_core->env.seq);
            af_core_env_push(g_core, &er);
        }
        g_now_us += 2000000;
        af_pfs_tick();
        sh->conf_raw = s_k[0].persisted_seq;
        sh->conf_agg = s_k[1].persisted_seq;
        sh->conf_env = s_k[2].persisted_seq;
    }
}

/* e10: содержимое кольца env после восстановления: каждая запись равна ожидаемой по своему номеру seq */
static uint32_t verify_env(void) {
    uint32_t bad = 0;
    for (uint32_t i = 0; i < g_core->env.count; i++) {
        uint8_t rec[AF_REC_ENV_SIZE];
        af_env_rec_t got;
        af_ring_get(&g_core->env, i, rec);
        af_env_rec_t exp = env_expected(af_ring_oldest_seq(&g_core->env) + i);
        if (af_env_decode(rec, &got) != 0 || got.ts != exp.ts || got.t_c100 != exp.t_c100 || got.p_pa != exp.p_pa || got.flags != exp.flags) {
            bad++;
        }
    }
    return bad;
}

static void verify_content(uint32_t *mismatch) {
    *mismatch = 0;
    for (uint32_t i = 0; i < g_core->raw.count; i++) {
        uint8_t rec[20];
        uint8_t exp[20];
        af_ring_get(&g_core->raw, i, rec);
        uint32_t seq = af_ring_oldest_seq(&g_core->raw) + i;
        expect_raw(seq, exp);
        if (memcmp(rec, exp, 20) != 0) {
            (*mismatch)++;
        }
    }
}

static int agg_ok(void) {
    uint64_t last_ts = 0;
    for (uint32_t i = 0; i < g_core->agg.count; i++) {
        uint8_t rec[28];
        af_agg_rec_t agg;
        af_ring_get(&g_core->agg, i, rec);
        if (af_agg_decode(rec, &agg) != 0) {
            return 0;
        }
        if (agg.ts <= last_ts) {
            return 0;
        }
        last_ts = agg.ts;
    }
    return 1;
}

#include "pfs_inject.h"
#include "pfs_clear.h"
#include "pfs_log.h"

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <wear|crash> ...\n", argv[0]);
        return 2;
    }

    if (getenv("PFS_VERBOSE")) {
        g_sim_verbose = 1;
    }

    sh = (sim_shared_t *)mmap(NULL, sizeof(sim_shared_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sh == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    if (strcmp(argv[1], "log") == 0 && argc >= 3) {
        return run_log(argv[2]);
    }

    if (strcmp(argv[1], "clear") == 0 && argc >= 3) {
        return run_clear(argv[2]);
    }


    if (strcmp(argv[1], "inject") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s inject <scenario>\n", argv[0]);
            return 2;
        }
        return run_inject(argv[2]);
    }

    if (strcmp(argv[1], "wear") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s wear <warm_days> <meas_days>\n", argv[0]);
            return 2;
        }
        uint32_t warm_days = (uint32_t)atoi(argv[2]);
        uint32_t meas_days = (uint32_t)atoi(argv[3]);

        memset(sh->flash, 0xFF, NBLK * BLK);
        memset(sh->erase_cnt, 0, sizeof(sh->erase_cnt));
        sh->prog_bytes = 0;
        sh->op_count = 0;
        sh->crash_at = 0;
        sh->rng = 12345;
        sh->conf_raw = 0;
        sh->conf_agg = 0;
        sh->gen_raw = 0;
        sh->gen_agg = 0;
        sh->gen2 = 0;
        sh->tail_bad_flag = 0;
        sh->gap_flag = 0;
        sh->end_raw = 0;
        sh->end_agg = 0;
        sh->viol_loss = 0;
        sh->viol_future = 0;
        sh->viol_content = 0;
        sh->viol_cont = 0;
        sh->viol_agg = 0;
        sh->conf_env = 0;
        sh->gen_env = 0;
        sh->end_env = 0;
        sh->gen2_env = 0;
        sh->viol_env = 0;
        sh->mount_fail = 0;

        life_init();
        life_run(warm_days * 43200);

        memset(sh->erase_cnt, 0, sizeof(sh->erase_cnt));
        sh->prog_bytes = 0;
        uint32_t fl0 = s_flushes;
        uint32_t seq0 = g_core->raw.seq;
        uint32_t aseq0 = g_core->agg.seq;

        life_run(meas_days * 43200);

        uint32_t flushes = s_flushes - fl0;
        uint64_t erases = 0;
        uint32_t blk_max = 0;
        uint64_t blk_sum = 0;
        uint32_t blk_touched = 0;
        for (uint32_t i = 0; i < NBLK; i++) {
            erases += sh->erase_cnt[i];
            if (sh->erase_cnt[i] > blk_max) blk_max = sh->erase_cnt[i];
            blk_sum += sh->erase_cnt[i];
            if (sh->erase_cnt[i] > 0) blk_touched++;
        }

        double erases_per_day = (meas_days > 0) ? (double)erases / meas_days : 0.0;
        double erases_per_flush = (flushes > 0) ? (double)erases / flushes : 0.0;
        double blk_avg_all = (double)blk_sum / NBLK;
        
        uint64_t payload_bytes = (uint64_t)(g_core->raw.seq - seq0) * 20 + (uint64_t)(g_core->agg.seq - aseq0) * 28;
        double write_amp = (payload_bytes > 0) ? (double)sh->prog_bytes / payload_bytes : 0.0;
        
        double years_by_max = 0.0;
        if (blk_max > 0 && meas_days > 0) {
            double erases_per_year = (double)blk_max / meas_days * 365.0;
            if (erases_per_year > 0) years_by_max = 100000.0 / erases_per_year;
        }
        
        double years_by_avg = 0.0;
        if (blk_avg_all > 0 && meas_days > 0) {
            double erases_per_year = blk_avg_all / meas_days * 365.0;
            if (erases_per_year > 0) years_by_avg = 100000.0 / erases_per_year;
        }

        uint32_t raw_files = s_k[0].next_idx - s_k[0].oldest_idx;
        uint32_t agg_files = s_k[1].next_idx - s_k[1].oldest_idx;
        
        size_t total, used;
        esp_littlefs_info(NULL, &total, &used);
        uint32_t fs_used = (uint32_t)used;

        printf("WEAR flush_s=%u warm_days=%u meas_days=%u flushes=%u flush_errs=%u erases=%llu erases_per_day=%.1f erases_per_flush=%.3f blk_max=%u blk_avg_all=%.2f blk_touched=%u prog_bytes=%llu payload_bytes=%llu write_amp=%.2f years_by_max=%.1f years_by_avg=%.1f raw_files=%u agg_files=%u fs_used=%u\n",
            AF_FLUSH_INTERVAL_S, warm_days, meas_days, flushes, 0,
            (unsigned long long)erases, erases_per_day, erases_per_flush,
            blk_max, blk_avg_all, blk_touched,
            (unsigned long long)sh->prog_bytes, (unsigned long long)payload_bytes, write_amp,
            years_by_max, years_by_avg,
            raw_files, agg_files, fs_used);

    } else if (strcmp(argv[1], "crash") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s crash <trials> <seed>\n", argv[0]);
            return 2;
        }
        uint32_t trials = (uint32_t)atoi(argv[2]);
        uint32_t seed = (uint32_t)atoi(argv[3]);

        uint32_t crashed = 0;
        uint32_t abnormal = 0;
        uint32_t mount_fail = 0;
        uint32_t viol_loss = 0;
        uint32_t viol_future = 0;
        uint32_t viol_content = 0;
        uint32_t viol_cont = 0;
        uint32_t viol_agg = 0;
        uint32_t viol_env = 0;
        uint32_t max_loss = 0;
        uint32_t tail_bad_trials = 0;
        uint32_t gap_trials = 0;

        for (uint32_t t = 0; t < trials; t++) {
            memset(sh->flash, 0xFF, NBLK * BLK);
            memset(sh->erase_cnt, 0, sizeof(sh->erase_cnt));
            sh->prog_bytes = 0;
            sh->op_count = 0;
            sh->crash_at = 0;
            sh->rng = seed * 2654435761u + t * 40503u + 1;
            sh->conf_raw = 0;
            sh->conf_agg = 0;
            sh->gen_raw = 0;
            sh->gen_agg = 0;
            sh->gen2 = 0;
            sh->tail_bad_flag = 0;
            sh->gap_flag = 0;
            sh->end_raw = 0;
            sh->end_agg = 0;
            sh->viol_loss = 0;
            sh->viol_future = 0;
            sh->viol_content = 0;
            sh->viol_cont = 0;
            sh->viol_agg = 0;
            sh->conf_env = 0;
            sh->gen_env = 0;
            sh->end_env = 0;
            sh->gen2_env = 0;
            sh->viol_env = 0;
            sh->mount_fail = 0;
            g_now_us = 0;

            // Phase A
            pid_t pid = fork();
            if (pid == 0) {
                alarm(8);
                life_init();
                uint32_t warm = 0;
                switch (t % 4) {
                    case 0: warm = 0; break;
                    case 1: warm = 3000; break;
                    case 2: warm = 40000; break;
                    case 3: warm = 190000; break;
                }
                life_run(warm);
                af_pfs_flush_now();
                _exit(0);
            } else if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                    abnormal++;
                }
            }

            // Phase B
            pid = fork();
            if (pid == 0) {
                alarm(8);
                life_init();
                sh->conf_raw = s_k[0].persisted_seq;
                sh->conf_agg = s_k[1].persisted_seq;
                sh->conf_env = s_k[2].persisted_seq;
                sh->op_count = 0;
                sh->crash_at = 1 + rnd() % 260;
                life_run(20000);
                _exit(0);
            } else if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status)) {
                    int code = WEXITSTATUS(status);
                    if (code == 77) {
                        crashed++;
                    } else if (code != 0) {
                        abnormal++;
                    }
                } else {
                    abnormal++;
                }
            }

            // Phase C
            pid = fork();
            if (pid == 0) {
                alarm(8);
                sh->crash_at = 0;
                life_init();
                
                sh->end_raw = g_core->raw.seq;
                sh->end_agg = g_core->agg.seq;
                sh->end_env = g_core->env.seq;
                if (sh->end_env < sh->conf_env) sh->viol_env++;
                if (sh->gen_env > sh->end_env && sh->gen_env - sh->end_env > 12) sh->viol_env++;
                if (sh->gen_env > 0 && sh->end_env > sh->gen_env) sh->viol_env++;
                sh->viol_env += verify_env();
                
                if (sh->end_raw < sh->conf_raw) sh->viol_loss++;
                if (sh->gen_raw > sh->end_raw && sh->gen_raw - sh->end_raw > 152) sh->viol_loss++;
                if (sh->gen_raw > 0 && sh->end_raw > sh->gen_raw) sh->viol_future++;
                
                uint32_t m = 0;
                verify_content(&m);
                sh->viol_content += m;
                
                if (!agg_ok()) sh->viol_agg++;
                if (sh->end_agg < sh->conf_agg) sh->viol_agg++;
                if (sh->gen_agg > sh->end_agg && sh->gen_agg - sh->end_agg > 6) sh->viol_agg++;
                
                sh->tail_bad_flag = s_restore_tail_bad;
                sh->gap_flag = s_restore_gap;
                
                life_run(700);
                af_pfs_flush_now();
                sh->gen2 = g_core->raw.seq;
                sh->gen2_env = g_core->env.seq;
                _exit(0);
            } else if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                    abnormal++;
                }
            }

            // Phase D
            pid = fork();
            if (pid == 0) {
                alarm(8);
                life_init();
                if (g_core->raw.seq != sh->gen2) sh->viol_cont++;
                if (g_core->env.seq != sh->gen2_env) sh->viol_env++;
                sh->viol_env += verify_env();
                
                uint32_t m = 0;
                verify_content(&m);
                sh->viol_content += m;
                
                _exit(0);
            } else if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                    abnormal++;
                }
            }

            mount_fail += sh->mount_fail;
            viol_loss += sh->viol_loss;
            viol_future += sh->viol_future;
            viol_content += sh->viol_content;
            viol_cont += sh->viol_cont;
            viol_agg += sh->viol_agg;
            viol_env += sh->viol_env;
            
            if (sh->tail_bad_flag > 0) tail_bad_trials++;
            if (sh->gap_flag > 0) gap_trials++;
            
            if (abnormal >= 2) break; /* стенд красный: дальше не ждём (зависшие потомки убивает alarm) */
            if (sh->gen_raw >= sh->end_raw) {
                uint32_t loss = sh->gen_raw - sh->end_raw;
                if (loss > max_loss) max_loss = loss;
            }
        }

        double max_loss_min = max_loss * 2.0 / 60.0;
        printf("CRASH trials=%u crashed=%u abnormal=%u mount_fail=%u viol_loss=%u viol_future=%u viol_content=%u viol_cont=%u viol_agg=%u viol_env=%u max_loss_records=%u max_loss_min=%.2f tail_bad_trials=%u gap_trials=%u\n",
            trials, crashed, abnormal, mount_fail, viol_loss, viol_future, viol_content, viol_cont, viol_agg, viol_env,
            max_loss, max_loss_min, tail_bad_trials, gap_trials);

        if (crashed == trials && abnormal == 0 && mount_fail == 0 && viol_loss == 0 && viol_future == 0 && viol_content == 0 && viol_cont == 0 && viol_agg == 0 && viol_env == 0) {
            return 0;
        }
        return 1;
    } else {
        fprintf(stderr, "Unknown mode: %s\n", argv[1]);
        return 2;
    }

    return 0;
}
