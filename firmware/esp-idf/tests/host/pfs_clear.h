/* pfs_clear.h — сценарии проверки af_pfs_clear */

typedef struct { uint32_t seq[3], epoch[3], count[3]; } snap3_t;

static void take(snap3_t *s) {
    s->seq[0] = g_core->raw.seq;   s->epoch[0] = g_core->raw.epoch;   s->count[0] = g_core->raw.count;
    s->seq[1] = g_core->agg.seq;   s->epoch[1] = g_core->agg.epoch;   s->count[1] = g_core->agg.count;
    s->seq[2] = g_core->env.seq;   s->epoch[2] = g_core->env.epoch;   s->count[2] = g_core->env.count;
}

static void clr_reinit(void) {
    if (g_core->raw.buf) { free(g_core->raw.buf); g_core->raw.buf = NULL; }
    if (g_core->agg.buf) { free(g_core->agg.buf); g_core->agg.buf = NULL; }
    if (g_core->env.buf) { free(g_core->env.buf); g_core->env.buf = NULL; }
    life_init();
}

/* только сегменты (имя 13 символов): epoch.bin не считается файлом вида e */
static int seg_list(char pfx, finfo_t *out, int max) {
    finfo_t tmp[200];
    int n = list_files(pfx, tmp, 200), m = 0;
    for (int i = 0; i < n && m < max; i++) if (strlen(tmp[i].name) == 13) out[m++] = tmp[i];
    return m;
}

static int nfiles(char pfx) {
    finfo_t tmp[200];
    return (pfx == 'c') ? list_files(pfx, tmp, 200) : seg_list(pfx, tmp, 200);
}

static void write_file(const char *path, const void *data, size_t n) {
    sim_FILE *f = sim_fopen(path, "wb");
    if (f) { sim_fwrite(data, 1, n, f); sim_fclose(f); }
}

static void sc_c1(int mask) {
    warm(6000);
    snap3_t pre; take(&pre);

    finfo_t rb[200], ra[200], re[200];
    int nb = seg_list('r', rb, 200);
    int na = seg_list('a', ra, 200);
    int ne = seg_list('e', re, 200);

    af_pfs_clear_res_t res;
    int rc = af_pfs_clear((uint32_t)mask, &res);
    ICHECK(rc == 0);
    ICHECK(res.persisted == 1);

    snap3_t post; take(&post);
    for (int k = 0; k < 3; k++) {
        int bit = (1 << k);
        if (mask & bit) {
            ICHECK(res.removed[k] == pre.count[k]);
            ICHECK(pre.count[k] > 0);
            ICHECK(post.seq[k] == pre.seq[k]);
            ICHECK(post.count[k] == 0);
            ICHECK(post.epoch[k] > pre.epoch[k]);
            char pfx = (k == 0) ? 'r' : (k == 1) ? 'a' : 'e';
            finfo_t fl[200];
            int nf = seg_list(pfx, fl, 200);
            ICHECK(nf == 1);
            ICHECK(fl[0].size == 32);
        } else {
            ICHECK(post.seq[k] == pre.seq[k]);
            ICHECK(post.count[k] == pre.count[k]);
            ICHECK(post.epoch[k] == pre.epoch[k]);
            if (k == 0) { finfo_t t[200]; int nt = seg_list('r', t, 200); ICHECK(nt == nb && same_files(rb, nb, t, nt)); }
            else if (k == 1) { finfo_t t[200]; int nt = seg_list('a', t, 200); ICHECK(nt == na && same_files(ra, na, t, nt)); }
            else { finfo_t t[200]; int nt = seg_list('e', t, 200); ICHECK(nt == ne && same_files(re, ne, t, nt)); }
        }
    }
    ICHECK(nfiles('c') == 0);

    snap3_t cl; take(&cl);
    clr_reinit();
    snap3_t post2; take(&post2);
    for (int k = 0; k < 3; k++) {
        int bit = (1 << k);
        if (mask & bit) {
            ICHECK(post2.seq[k] == pre.seq[k]);
            ICHECK(post2.count[k] == 0);
            ICHECK(post2.epoch[k] > cl.epoch[k]);
        } else {
            ICHECK(post2.seq[k] == pre.seq[k]);
            ICHECK(post2.count[k] == pre.count[k]);
        }
    }
    uint32_t m;
    verify_content(&m);
    ICHECK(m == 0);
    ICHECK(verify_env() == 0);

    life_run(1000);
    af_pfs_flush_now();
    snap3_t before_boot; take(&before_boot);
    clr_reinit();
    snap3_t post3; take(&post3);
    for (int k = 0; k < 3; k++) {
        int bit = (1 << k);
        if (mask & bit) {
            ICHECK(post3.seq[k] == before_boot.seq[k]);
            ICHECK(post3.seq[k] > pre.seq[k]);
            if (k == 0) ICHECK(post3.count[0] == 1000);
        }
    }
    verify_content(&m);
    ICHECK(m == 0);
    ICHECK(verify_env() == 0);
    ICHECK(s_restore_gap == 0 && s_restore_fail == 0);
}

static void sc_c2(int mask) {
    (void)mask;
    warm(6000);
    snap3_t pre; take(&pre);

    af_pfs_clear_res_t res1;
    int rc1 = af_pfs_clear(1, &res1);
    ICHECK(rc1 == 0);
    snap3_t s1; take(&s1);

    af_pfs_clear_res_t res2;
    int rc2 = af_pfs_clear(1, &res2);
    ICHECK(rc2 == 0);
    ICHECK(res2.removed[0] == 0);
    ICHECK(res2.seq[0] == s1.seq[0]);
    ICHECK(res2.epoch[0] > s1.epoch[0]);

    finfo_t fl[200];
    int nf = list_files('r', fl, 200);
    ICHECK(nf == 1);
    ICHECK(fl[0].size == 32);

    clr_reinit();
    snap3_t post; take(&post);
    ICHECK(post.seq[0] == pre.seq[0]);
}

static void sc_c3(int mask) {
    (void)mask;
    life_init();
    snap3_t pre; take(&pre);

    af_pfs_clear_res_t res;
    int rc = af_pfs_clear(7, &res);
    ICHECK(rc == 0);
    for (int k = 0; k < 3; k++) {
        ICHECK(res.removed[k] == 0);
        ICHECK(res.seq[k] == 0);
        ICHECK(res.epoch[k] > pre.epoch[k]);
    }

    clr_reinit();
    snap3_t post; take(&post);
    for (int k = 0; k < 3; k++) ICHECK(post.seq[k] == 0);

    life_run(200);
    af_pfs_flush_now();
    clr_reinit();
    snap3_t post2; take(&post2);
    ICHECK(post2.seq[0] == 200);
    ICHECK(post2.count[0] == 200);
    uint32_t m;
    verify_content(&m);
    ICHECK(m == 0);
}

static void sc_c4(int mask) {
    warm(6000);
    snap3_t pre; take(&pre);
    finfo_t rb[200];
    int nb = list_files('r', rb, 200);

    g_inj_fopen_mode = 'w';
    g_inj_fopen_pfx = 'c';
    g_inj_fopen.count = 1;

    af_pfs_clear_res_t res;
    int rc = af_pfs_clear((uint32_t)mask, &res);
    ICHECK(rc == -3);
    inj_clear();

    snap3_t post; take(&post);
    for (int k = 0; k < 3; k++) {
        ICHECK(post.seq[k] == pre.seq[k]);
        ICHECK(post.count[k] == pre.count[k]);
        ICHECK(post.epoch[k] == pre.epoch[k]);
    }
    finfo_t rb2[200];
    int nb2 = list_files('r', rb2, 200);
    ICHECK(nb == nb2);
    ICHECK(same_files(rb, nb, rb2, nb2));
    ICHECK(nfiles('c') == 0);

    af_pfs_clear_res_t res2;
    int rc2 = af_pfs_clear((uint32_t)mask, &res2);
    ICHECK(rc2 == 0);
    ICHECK(res2.removed[0] == pre.count[0]);
}

static void sc_c5(int mask) {
    (void)mask;
    warm(6000);
    snap3_t pre; take(&pre);

    g_inj_fopen_mode = 'w';
    g_inj_fopen_pfx = 'r';
    g_inj_fopen.count = 1;

    af_pfs_clear_res_t res;
    int rc = af_pfs_clear(1, &res);
    ICHECK(rc == -2);
    ICHECK(res.persisted == 0);
    inj_clear();

    snap3_t post; take(&post);
    ICHECK(post.count[0] == 0);
    ICHECK(post.seq[0] == pre.seq[0]);
    ICHECK(nfiles('c') == 1);

    clr_reinit();
    snap3_t post2; take(&post2);
    ICHECK(post2.seq[0] == pre.seq[0]);
    ICHECK(post2.count[0] == 0);
    finfo_t fl[200];
    int nf = list_files('r', fl, 200);
    ICHECK(nf == 1);
    ICHECK(fl[0].size == 32);
    ICHECK(nfiles('c') == 0);
    ICHECK(post2.epoch[0] > pre.epoch[0]);
}

static void sc_c6(int mask) {
    warm(40000);
    uint8_t *snap = malloc(NBLK * BLK);
    memcpy(snap, sh->flash, NBLK * BLK);
    snap3_t pre0; take(&pre0);
    (void)pre0;

    int completed = 0, npts = 0;
    int n_cleared = 0, n_intact = 0;

    for (int N = 1; N <= 600; N++) {
        memcpy(sh->flash, snap, NBLK * BLK);
        sh->crash_at = 0;
        clr_reinit();
        snap3_t pre; take(&pre);

        npts = N;
        pid_t pid = fork();
        if (pid == 0) {
            sh->op_count = 0;
            sh->crash_at = (uint32_t)N;
            af_pfs_clear_res_t r;
            af_pfs_clear((uint32_t)mask, &r);
            _exit(0);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        ICHECK(WIFEXITED(st));
        int code = WEXITSTATUS(st);
        ICHECK(code == 0 || code == 77);
        if (code == 0) {
            completed = 1;
            break;
        }

        sh->crash_at = 0;
        clr_reinit();
        snap3_t post; take(&post);

        int kind0_cleared = 0;
        for (int k = 0; k < 3; k++) {
            int bit = (1 << k);
            if (mask & bit) {
                ICHECK(post.seq[k] == pre.seq[k]);
                ICHECK(post.count[k] == 0 || post.count[k] == pre.count[k]);
                ICHECK(post.epoch[k] >= pre.epoch[k]);
                if (k == 0) {
                    if (post.count[0] == 0) n_cleared++;
                    else n_intact++;
                    kind0_cleared = (post.count[0] == 0);
                }
            } else {
                ICHECK(post.seq[k] == pre.seq[k]);
                ICHECK(post.count[k] == pre.count[k]);
            }
        }
        (void)kind0_cleared;

        uint32_t m;
        verify_content(&m);
        ICHECK(m == 0);
        ICHECK(verify_env() == 0);
        ICHECK(s_restore_fail == 0);
        ICHECK(nfiles('c') == 0);
    }

    free(snap);
    ICHECK(completed);
    ICHECK(n_cleared > 0 && n_intact > 0);
    printf("INFO CLR_CRASH mask=%d points=%d intact=%d cleared=%d\n", mask, npts, n_intact, n_cleared);
}

static void sc_c7(int mask) {
    warm(6000);
    af_pfs_clear_res_t res;
    int rc = af_pfs_clear((uint32_t)mask, &res);
    ICHECK(rc == 0);

    life_run(500);
    af_pfs_flush_now();
    snap3_t pre; take(&pre);
    ICHECK(pre.seq[0] == 6500);
    ICHECK(pre.count[0] == 500);

    af_clr_marker_t m = {0};
    m.mask = 1;
    m.epoch = res.epoch[0];
    m.seq[0] = res.seq[0];
    uint8_t b[AF_CLR_SIZE];
    af_clr_marker_encode(&m, b);
    write_file("/storage/h/clr.bin", b, AF_CLR_SIZE);

    clr_reinit();
    snap3_t post; take(&post);
    ICHECK(post.seq[0] == pre.seq[0]);
    ICHECK(post.count[0] == 500);
    ICHECK(nfiles('c') == 0);
    uint32_t mm;
    verify_content(&mm);
    ICHECK(mm == 0);
}

static void sc_c8(int arg) {
    warm(6000);
    snap3_t pre; take(&pre);
    int nb = list_files('r', (finfo_t[200]){0}, 200);

    if (arg == 0) {
        uint8_t buf[26];
        for (int i = 0; i < 26; i++) buf[i] = (uint8_t)(i * 7 + 3);
        write_file("/storage/h/clr.bin", buf, 26);
    } else {
        af_clr_marker_t m = {0};
        m.mask = 1;
        m.epoch = 99;
        m.seq[0] = 1234;
        uint8_t b[AF_CLR_SIZE];
        af_clr_marker_encode(&m, b);
        write_file("/storage/h/clr.bin", b, 10);
    }

    clr_reinit();
    snap3_t post; take(&post);
    for (int k = 0; k < 3; k++) {
        ICHECK(post.seq[k] == pre.seq[k]);
        ICHECK(post.count[k] == pre.count[k]);
        ICHECK(post.epoch[k] >= pre.epoch[k]);
    }
    ICHECK(nfiles('c') == 0);
    int nb2 = list_files('r', (finfo_t[200]){0}, 200);
    ICHECK(nb == nb2);
}

static void sc_c10(int mask) {
    (void)mask;
    life_init();
    uint32_t last_epoch = 0;
    for (int i = 0; i < 5; i++) {
        life_run(1500);
        af_pfs_flush_now();
        snap3_t a; take(&a);

        af_pfs_clear_res_t res;
        int rc = af_pfs_clear(1, &res);
        ICHECK(rc == 0);
        snap3_t b; take(&b);
        ICHECK(b.seq[0] == a.seq[0]);
        ICHECK(b.epoch[0] > last_epoch);
        last_epoch = b.epoch[0];

        clr_reinit();
        snap3_t c; take(&c);
        ICHECK(c.seq[0] == a.seq[0]);
        ICHECK(c.epoch[0] > last_epoch);
        last_epoch = c.epoch[0];
    }

    ICHECK(g_core->raw.seq == 5 * 1500u);

    life_run(100);
    af_pfs_flush_now();
    clr_reinit();
    snap3_t post; take(&post);
    ICHECK(post.count[0] == 100);
    uint32_t m;
    verify_content(&m);
    ICHECK(m == 0);
}

static int c11_takes;

static void c11_hook(void)
{
    c11_takes++;
    if (c11_takes < 3) {
        g_take_hook = c11_hook;
        return;
    }
    /* 3-й захват: имитация 7 новых сэмплов */
    for (int i = 0; i < 7; i++) {
        uint32_t seq = g_core->raw.seq;
        uint32_t ts;
        af_sample_t s;
        gen_sample(seq, &ts, &s);
        af_core_ingest(g_core, ts, &s);
    }
}

static void sc_c11(int mask)
{
    warm(6000);
    snap3_t pre;
    take(&pre);
    c11_takes = 0;
    g_take_hook = c11_hook;
    af_pfs_clear_res_t res;
    int rc = af_pfs_clear((uint32_t)mask, &res);
    g_take_hook = NULL;
    ICHECK(rc == 0);
    ICHECK(c11_takes >= 3);
    snap3_t post;
    take(&post);
    ICHECK(res.removed[0] == pre.count[0]);
    ICHECK(post.count[0] == 7);
    ICHECK(post.seq[0] == pre.seq[0] + 7);
    ICHECK(post.epoch[0] > pre.epoch[0]);
    uint32_t m;
    verify_content(&m);
    ICHECK(m == 0);
    af_pfs_flush_now();
    clr_reinit();
    snap3_t post2;
    take(&post2);
    ICHECK(post2.seq[0] == pre.seq[0] + 7);
    ICHECK(post2.count[0] == 7);
    verify_content(&m);
    ICHECK(m == 0);
    ICHECK(s_restore_fail == 0 && s_restore_gap == 0);
    ICHECK(post2.epoch[0] > post.epoch[0]);
}
static const sc_t SCEN_CLR[] = {
    {"c1_raw", sc_c1, 1},
    {"c1_agg", sc_c1, 2},
    {"c1_env", sc_c1, 4},
    {"c1_all", sc_c1, 7},
    {"c2_twice", sc_c2, 1},
    {"c3_empty", sc_c3, 7},
    {"c4_marker_fail", sc_c4, 1},
    {"c5_hdr_fail", sc_c5, 1},
    {"c6_crash_raw", sc_c6, 1},
    {"c6_crash_all", sc_c6, 7},
    {"c7_stale_marker", sc_c7, 1},
    {"c8_garbage_marker", sc_c8, 0},
    {"c9_short_marker", sc_c8, 1},
    {"c10_cycles", sc_c10, 1}, {"c11_interleave", sc_c11, 1},
};

static int run_clear(const char *name) {
    for (size_t i = 0; i < sizeof(SCEN_CLR) / sizeof(SCEN_CLR[0]); i++) {
        if (strcmp(SCEN_CLR[i].name, name) != 0) continue;
        memset(sh->flash, 0xFF, NBLK * BLK);
        sh->crash_at = 0;
        alarm(280);
        SCEN_CLR[i].fn(SCEN_CLR[i].arg);
        printf("CLR %s %s failures=%d\n", name, g_inj_fail ? "RED" : "GREEN", g_inj_fail);
        return g_inj_fail ? 1 : 0;
    }
    fprintf(stderr, "unknown scenario %s\n", name);
    return 2;
}
