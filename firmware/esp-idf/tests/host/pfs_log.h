static void log_reboot(void) {
    if (lg_ring) free(lg_ring);
    memset(&lg_core, 0, sizeof(lg_core));
    lg_ring = NULL;
    lg_inited = 0;
    lg_fs_ready = 0;
    lg_in_flush = 0;
    lg_nf = 0;
    memset(lg_f, 0, sizeof(lg_f));
    memset(lg_f_bytes, 0, sizeof(lg_f_bytes));
    lg_retry_s = 0;
    lg_last_flush_s = 0;
    lg_fail_n = 0;
    lg_wr_err = 0;
    lg_dropped_space = 0;
    lg_flushes = 0;
    lg_dropped_mtx = 0;
    lg_af_n = 0;
    lg_af_size = 0;
    lg_af_caps = 0;
    lg_af_logged_n = 0;
    lg_af_logged_s = 0;
    lg_boot_flushed = 0;
    life_init();
    af_log_init();
}

static void log_first_boot(void) {
    life_init();
    af_log_init();
}

static int log_files(finfo_t *out, int max) {
    int n = 0;
    sim_DIR *d = sim_opendir("/storage/l");
    if (!d) return 0;
    struct sim_dirent *e;
    while ((e = sim_readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (n >= max) break;
        char path[64];
        snprintf(path, sizeof(path), "/l/%s", e->d_name);
        struct lfs_info info;
        if (lfs_stat(&g_lfs, path, &info) == 0) {
            snprintf(out[n].name, sizeof(out[n].name), "%s", e->d_name);
            out[n].size = (uint32_t)info.size;
            n++;
        }
    }
    sim_closedir(d);
    return n;
}

static uint32_t log_bytes(void) {
    finfo_t f[AF_LOG_MAX_FILES];
    int n = log_files(f, AF_LOG_MAX_FILES);
    uint32_t sum = 0;
    for (int i = 0; i < n; i++) sum += f[i].size;
    return sum;
}

static void spin(uint32_t secs) {
    for (uint32_t i = 0; i < secs; i++) {
        g_now_us += 1000000;
        af_log_tick();
    }
}

typedef struct {
    uint32_t n;
    uint32_t first, last;
    uint32_t prev;
    int ordered;
    int dup;
    uint32_t lvl_min;
} coll_t;

static int coll_cb(void *ctx, const af_log_ent_t *e) {
    coll_t *c = (coll_t *)ctx;
    if (getenv("LOGDBG")) printf("  E seq=%u L%d %s | %s x%u\n", e->seq, e->level, e->tag, e->msg, e->count);
    if (c->n == 0) {
        c->first = e->seq;
        c->prev = e->seq;
        c->ordered = 1;
        c->lvl_min = e->level;
    } else {
        if (e->seq <= c->prev) c->ordered = 0;
        if (e->seq == c->prev) c->dup = 1;
        c->prev = e->seq;
        if (e->level < c->lvl_min) c->lvl_min = e->level;
    }
    c->last = e->seq;
    c->n++;
    return 0;
}

static void collect(uint32_t since, int minlvl, uint32_t limit, coll_t *c) {
    memset(c, 0, sizeof(*c));
    af_log_iter(since, minlvl, limit, coll_cb, c);
}

static void put_events(int n, int level) {
    static int counter = 0;
    for (int i = 0; i < n; i++) {
        g_now_us += 1000000;
        af_log_event(level, "t", "event number %d with some padding text to make a realistic line", counter++);
        af_log_tick();
    }
}

static void sc_l1(int arg) {
    (void)arg;
    log_first_boot();
    int levels[5] = {1, 2, 3, 1, 2};
    for (int i = 0; i < 5; i++) {
        af_log_event(levels[i], "t", "event %d", i);
        spin(1);
    }
    coll_t c;
    collect(0, 1, 100, &c);
    ICHECK(c.n == 5);
    ICHECK(c.ordered);
    ICHECK(!c.dup);
    spin(61);
    ICHECK(lg_flushes >= 1);
    finfo_t f[AF_LOG_MAX_FILES];
    int nf = log_files(f, AF_LOG_MAX_FILES);
    ICHECK(nf == 1);
    ICHECK(strncmp(f[0].name, "g00000001", 9) == 0);
    ICHECK(f[0].size > 0);
    collect(0, 1, 100, &c);
    ICHECK(c.n == 5);
    ICHECK(c.ordered);
    uint32_t last_before = c.last;
    log_reboot();
    af_log_event(1, "t", "new 1");
    spin(1);
    af_log_event(2, "t", "new 2");
    spin(70);
    collect(0, 1, 100, &c);
    ICHECK(c.n >= 7);
    ICHECK(c.ordered);
    ICHECK(!c.dup);
    ICHECK(c.last > last_before);
    log_reboot();
    spin(2);
    collect(0, 1, 100, &c);
    ICHECK(c.n >= 7);
}

static void sc_l2(int arg) {
    (void)arg;
    log_first_boot();
    put_events(6000, 1);
    ICHECK(log_bytes() <= AF_LOG_MAX_TOTAL + AF_LOG_SEG_BYTES);
    ICHECK(lg_nf <= AF_LOG_MAX_FILES);
    finfo_t f[AF_LOG_MAX_FILES];
    int nf = log_files(f, AF_LOG_MAX_FILES);
    if (nf != (int)lg_nf) printf("  DBG files=%d lg_nf=%u bytes=%u\n", nf, lg_nf, log_bytes());
    ICHECK(nf == (int)lg_nf);
    coll_t c;
    collect(0, 1, 100000, &c);
    ICHECK(c.ordered);
    ICHECK(!c.dup);
    ICHECK(c.last >= 5990);
    ICHECK(c.first > 1);
    ICHECK(c.n >= 1000);
    log_reboot(); spin(2);
    ICHECK(log_bytes() <= AF_LOG_MAX_TOTAL + AF_LOG_SEG_BYTES);
    ICHECK(lg_nf <= AF_LOG_MAX_FILES);
    nf = log_files(f, AF_LOG_MAX_FILES);
    if (nf != (int)lg_nf) printf("  DBG files=%d lg_nf=%u bytes=%u\n", nf, lg_nf, log_bytes());
    ICHECK(nf == (int)lg_nf);
    for (int i = 0; i < nf - 1; i++) {
        ICHECK(f[i].size <= AF_LOG_SEG_BYTES);
    }
    ICHECK(sim_opendir("/storage/h") != NULL);
}

static void sc_l3(int arg) {
    log_first_boot();
    size_t total, used;
    esp_littlefs_info("storage", &total, &used);
    sim_FILE *junk = sim_fopen("/storage/h/junk.bin", "wb");
    char buf[4096];
    memset(buf, 'x', sizeof(buf));
    double target = arg ? 0.745 : 0.80;
    while (1) {
        esp_littlefs_info("storage", &total, &used);
        if ((double)used / (double)total >= target) break;
        sim_fwrite(buf, 1, sizeof(buf), junk);
    }
    sim_fclose(junk);
    esp_littlefs_info("storage", &total, &used);
    uint64_t used_before = used;
    uint32_t dropped_before = lg_dropped_space;
    if (arg == 0) {
        put_events(1500, 1);
        ICHECK(lg_dropped_space > 0);
        esp_littlefs_info("storage", &total, &used);
        ICHECK(used <= used_before + 12288);
    } else {
        double min_free_frac = 1.0;
        for (int i = 0; i < 1500; i++) {
            g_now_us += 1000000;
            af_log_event(1, "t", "ev %d", i);
            af_log_tick();
            if (i % 100 == 0) {
                esp_littlefs_info("storage", &total, &used);
                double free_frac = 1.0 - (double)used / (double)total;
                if (free_frac < min_free_frac) min_free_frac = free_frac;
            }
        }
        ICHECK(min_free_frac >= 0.25 - 8192.0 / (double)total);
        ICHECK(log_bytes() > 0);
    }
    uint32_t dropped_mid = lg_dropped_space;
    sim_unlink("/storage/h/junk.bin");
    put_events(100, 1);
    spin(70);
    ICHECK(lg_dropped_space == dropped_mid);
    (void)dropped_before;
    coll_t c;
    collect(0, 1, 1000, &c);
    ICHECK(c.n > 0);
}

static void sc_l4(int arg) {
    (void)arg;
    log_first_boot();
    put_events(10, 1);
    g_inj_fopen_mode = 'a';
    g_inj_fopen_pfx = 'g';
    g_inj_fopen.skip = 0;
    g_inj_fopen.count = 3;
    uint64_t last_fail_t = 0;
    int fails = 0;
    uint64_t gaps[3] = {0, 0, 0};
    for (int i = 0; i < 600; i++) {
        g_now_us += 1000000;
        af_log_tick();
        if (lg_wr_err > fails) {
            uint64_t now = g_now_us / 1000000;
            if (fails > 0) gaps[fails - 1] = now - last_fail_t;
            last_fail_t = now;
            fails = lg_wr_err;
        }
        if (fails >= 3) break;
    }
    ICHECK(fails >= 3);
    if (getenv("LOGDBG")) printf("  GAPS %llu %llu fails=%d\n", (unsigned long long)gaps[0], (unsigned long long)gaps[1], fails);
    ICHECK(gaps[0] >= 59);
    ICHECK(gaps[1] >= 119);
    spin(500);
    ICHECK(lg_fail_n == 0);
    ICHECK(lg_wr_err >= 3);
    coll_t c;
    collect(0, 1, 1000, &c);
    ICHECK(c.n >= 10);
    ICHECK(c.ordered);
    ICHECK(!c.dup);
}

static void sc_l5(int arg) {
    (void)arg;
    log_first_boot();
    const char *msgs[3] = {"alloc_fail", "mqtt reconnect", "i2c nack"};
    int lvls[3] = {3, 2, 2};
    for (int i = 0; i < 3600; i++) {
        for (int j = 0; j < 10; j++) {
            int idx = j % 3;
            af_log_event(lvls[idx], "t", "%s", msgs[idx]);
        }
        g_now_us += 1000000;
        af_log_tick();
    }
    spin(70);
    ICHECK(log_bytes() < 60000);
    ICHECK(lg_core.collapsed > 30000);
    coll_t c;
    collect(0, 1, 100000, &c);
    ICHECK(c.n <= 3 * 121 + 10);
    ICHECK(c.n >= 100);
    ICHECK(lg_flushes >= 55 && lg_flushes <= 130);
}

static void sc_l6(int arg) {
    (void)arg;
    log_first_boot();
    af_log_event(1, "t", "boot line");
    spin(3);
    ICHECK(lg_flushes == 1); /* первая запись сразу после подключения флеша */
    uint64_t t_boot_flush = g_now_us / 1000000;
    spin(5);
    uint32_t flushes_before = lg_flushes;
    af_log_event(3, "t", "fatal thing");
    while (lg_flushes == flushes_before && g_now_us / 1000000 < t_boot_flush + 100) spin(1);
    uint64_t dt = g_now_us / 1000000 - t_boot_flush;
    if (getenv("LOGDBG")) printf("  DT %llu\n", (unsigned long long)dt);
    ICHECK(g_now_us / 1000000 >= 31 && g_now_us / 1000000 <= 32); /* срочный сброс не раньше чем через 30 с после первого (на t=1) */
    uint32_t flushes_after_first = lg_flushes;
    af_log_event(3, "t", "fatal thing 2");
    for (int i = 0; i < 50; i++) {
        af_log_event(3, "t", "storm %d", i);
    }
    for (int i = 0; i < 29; i++) {
        g_now_us += 1000000;
        af_log_tick();
    }
    ICHECK(lg_flushes <= flushes_after_first + 1);
}

static void sc_l7(int arg) {
    (void)arg;
    log_first_boot();
    put_events(200, 1);
    spin(70);
    coll_t a;
    collect(0, 1, 100000, &a);
    ICHECK(a.n >= 150);
    uint32_t last_seq = a.last;
    uint32_t files = 0, bytes = 0;
    ICHECK(af_log_clear(&files, &bytes) == 0);
    ICHECK(files >= 1);
    ICHECK(bytes > 0);
    ICHECK(log_bytes() == 0);
    ICHECK(log_files(NULL, 0) == 0);
    coll_t c;
    collect(0, 1, 100, &c);
    ICHECK(c.n == 0);
    af_log_event(1, "log", "journal cleared by user");
    af_log_flush_now();
    collect(0, 1, 100, &c);
    ICHECK(c.n == 1);
    ICHECK(c.first > last_seq);
    log_reboot(); spin(2);
    collect(0, 1, 100, &c);
    ICHECK(c.n >= 1);
    ICHECK(c.first > last_seq);
    af_log_event(1, "t", "post reboot");
    spin(1);
    collect(0, 1, 100, &c);
    ICHECK(c.n >= 2);
    ICHECK(c.last > last_seq);
}

static void sc_l8(int arg) {
    (void)arg;
    log_first_boot();
    put_events(30, 1);
    spin(70);
    coll_t c;
    collect(0, 1, 1000, &c);
    uint32_t keep = c.n;
    uint32_t last_good_seq = c.last;
    char path[64];
    snprintf(path, sizeof(path), "/storage/l/g%08x.log", lg_f[lg_nf - 1]);
    sim_FILE *f = sim_fopen(path, "ab");
    sim_fwrite("99999\t1\t1\t0\tI\tt\t1\ttorn li", 1, 25, f);
    sim_fclose(f);
    log_reboot();
    spin(2);
    collect(0, 1, 1000, &c);
    ICHECK(c.n == keep);
    ICHECK(c.ordered);
    af_log_event(1, "t", "new after torn");
    spin(1);
    collect(0, 1, 1000, &c);
    ICHECK(c.n == keep + 1);
    ICHECK(c.last > last_good_seq);
    ICHECK(c.last < 99999);
    spin(70);
    sim_FILE *rf = sim_fopen(path, "rb");
    char buf[4096];
    size_t rd = sim_fread(buf, 1, sizeof(buf) - 1, rf);
    sim_fclose(rf);
    buf[rd] = 0;
    char *p = strstr(buf, "torn li");
    ICHECK(p != NULL);
    if (p) {
        if (getenv("LOGDBG")) printf("  AFTER %02x %02x %02x len=%zu\n", (unsigned char)p[7], (unsigned char)p[8], (unsigned char)p[9], rd);
        ICHECK(p[7] == '\n');
    }
}

static void sc_l9(int arg) {
    (void)arg;
    log_first_boot();
    put_events(40, 1);
    put_events(5, 2);
    put_events(5, 3);
    spin(70);
    put_events(10, 1);
    coll_t c;
    collect(0, 1, 1000, &c);
    ICHECK(c.n == 60);
    ICHECK(c.ordered);
    ICHECK(!c.dup);
    collect(0, 2, 1000, &c);
    ICHECK(c.n == 10);
    ICHECK(c.lvl_min >= 2);
    collect(0, 3, 1000, &c);
    ICHECK(c.n == 5);
    collect(0, 1, 7, &c);
    ICHECK(c.n == 7);
    ICHECK(c.last == c.first + 6);
    collect(0, 1, 50, &c);
    uint32_t s = c.last;
    collect(s, 1, 1000, &c);
    ICHECK(c.n == 10);
    ICHECK(c.first == s + 1);
    collect(4000000000u, 1, 100, &c);
    ICHECK(c.n == 0);
    collect(0xFFFFFFFF, 1, 100, &c);
    ICHECK(c.n == 0);
    char path[64];
    snprintf(path, sizeof(path), "/storage/l/g%08x.log", lg_f[lg_nf - 1]);
    sim_FILE *rf = sim_fopen(path, "rb");
    char line[256];
    size_t rd = sim_fread(line, 1, sizeof(line) - 1, rf);
    sim_fclose(rf);
    line[rd] = 0;
    char *nl = strchr(line, '\n');
    if (nl) *nl = 0;
    sim_FILE *wf = sim_fopen(path, "ab");
    sim_fwrite(line, 1, strlen(line), wf);
    sim_fwrite("\n", 1, 1, wf);
    sim_fclose(wf);
    log_reboot(); spin(2);
    collect(0, 1, 1000, &c);
    ICHECK(!c.dup);
    ICHECK(c.ordered);
}

static void sc_l10(int arg) {
    log_first_boot();
    memset(sh->erase_cnt, 0, sizeof(sh->erase_cnt));
    sh->prog_bytes = 0;
    for (int m = 0; m < 1440; m++) {
        if (arg == 0) {
            af_log_event(1, "t", "sparse %d", m);
        } else {
            for (int i = 0; i < 10; i++) {
                af_log_event(1, "t", "chatty %d %d", m, i);
            }
        }
        spin(60);
    }
    uint64_t er = 0;
    uint32_t blk_max = 0;
    for (int i = 0; i < NBLK; i++) {
        er += sh->erase_cnt[i];
        if (sh->erase_cnt[i] > blk_max) blk_max = sh->erase_cnt[i];
    }
    uint32_t flushes = lg_flushes;
    uint32_t bytes = log_bytes();
    uint32_t files = lg_nf;
    printf("LOGWEAR arg=%d day_events=%u flushes=%u erases=%llu blk_max=%u prog_bytes=%llu log_bytes=%u files=%u\n",
           arg, arg ? 14400 : 1440, flushes, (unsigned long long)er, blk_max, (unsigned long long)sh->prog_bytes, bytes, files);
    ICHECK(lg_flushes >= 1400 && lg_flushes <= 1500);
    ICHECK(er < 20000);
    ICHECK(lg_wr_err == 0);
}

static int mem_n;
static char mem_msgs[8][96];

static int mem_cb(void *ctx, const af_log_ent_t *e) {
    (void)ctx;
    if (e->level == 3 && strcmp(e->tag, "mem") == 0) {
        if (mem_n < 8) {
            strcpy(mem_msgs[mem_n], e->msg);
            mem_n++;
        }
    }
    return 0;
}

static void sc_l11(int arg) {
    (void)arg;
    log_first_boot();
    mem_n = 0;
    spin(2);
    af_log_note_alloc_fail(4096, 0x808);
    af_log_note_alloc_fail(8192, 0x1800);
    spin(2);
    af_log_iter(0, 1, 1000, mem_cb, NULL);
    ICHECK(mem_n == 1);
    ICHECK(strstr(mem_msgs[0], "alloc_fail x2") != NULL);
    ICHECK(strstr(mem_msgs[0], "last_size=8192") != NULL);
    ICHECK(strstr(mem_msgs[0], "caps=0x1800") != NULL);
    
    af_log_note_alloc_fail(100, 1);
    spin(1);
    af_log_note_alloc_fail(100, 1);
    spin(1);
    af_log_note_alloc_fail(100, 1);
    spin(5);
    
    mem_n = 0;
    af_log_iter(0, 1, 1000, mem_cb, NULL);
    ICHECK(mem_n == 1);
    
    spin(60);
    mem_n = 0;
    af_log_iter(0, 1, 1000, mem_cb, NULL);
    ICHECK(mem_n == 2);
    ICHECK(strstr(mem_msgs[1], "alloc_fail x3") != NULL);
    
    spin(120);
    mem_n = 0;
    af_log_iter(0, 1, 1000, mem_cb, NULL);
    ICHECK(mem_n == 2);
}
static const sc_t SCEN_LOG[] = {
    {"l1_roundtrip", sc_l1, 0},
    {"l2_rotation", sc_l2, 0},
    {"l3_reserve_low", sc_l3, 0},
    {"l3_reserve_edge", sc_l3, 1},
    {"l4_write_fail", sc_l4, 0},
    {"l5_flood", sc_l5, 0},
    {"l6_urgent", sc_l6, 0},
    {"l7_clear", sc_l7, 0},
    {"l8_torn_line", sc_l8, 0},
    {"l9_filters", sc_l9, 0},
    {"l10_wear_sparse", sc_l10, 0},
    {"l10_wear_chatty", sc_l10, 1}, {"l11_alloc_fail", sc_l11, 0}
};

static int run_log(const char *name) {
    for (size_t i = 0; i < sizeof(SCEN_LOG) / sizeof(SCEN_LOG[0]); i++) {
        if (strcmp(SCEN_LOG[i].name, name) != 0) continue;
        memset(sh->flash, 0xFF, NBLK * BLK);
        sh->crash_at = 0;
        alarm(250);
        SCEN_LOG[i].fn(SCEN_LOG[i].arg);
        printf("LOG %s %s failures=%d\n", name, g_inj_fail ? "RED" : "GREEN", g_inj_fail);
        return g_inj_fail ? 1 : 0;
    }
    fprintf(stderr, "unknown scenario %s\n", name);
    return 2;
}
