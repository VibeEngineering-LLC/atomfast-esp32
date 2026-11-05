/* Сценарии инжекции ошибок возврата (e8-fixes): pfs_sim inject <имя>. Один сценарий - один процесс. Включается из pfs_sim.c. */
static int g_inj_fail;
extern esp_err_t g_restore_rc;
#define ICHECK(c) do { if (!(c)) { printf("INJ-FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_inj_fail++; } } while (0)

typedef struct { char name[16]; uint32_t size; } finfo_t;

static void inj_clear(void) {
    memset(&g_inj_malloc, 0, sizeof(inj_t));
    memset(&g_inj_fopen, 0, sizeof(inj_t));
    memset(&g_inj_fread, 0, sizeof(inj_t));
    memset(&g_inj_ftell, 0, sizeof(inj_t));
    memset(&g_inj_fclose, 0, sizeof(inj_t));
    memset(&g_inj_opendir, 0, sizeof(inj_t));
    memset(&g_inj_fread_corrupt, 0, sizeof(inj_t));
    g_inj_fopen_mode = 0;
    g_inj_fopen_pfx = 0;
    g_inj_fclose_drop = 0;
    g_inj_sem_fail = 0;
}

static int list_files(char pfx, finfo_t *out, int max) {
    int n = 0;
    sim_DIR *d = sim_opendir("/storage/h");
    if (!d) return -1;
    struct sim_dirent *e;
    while ((e = sim_readdir(d)) != NULL && n < max) {
        if (e->d_name[0] != pfx) continue;
        char p[80];
        snprintf(p, sizeof(p), "/h/%s", e->d_name);
        struct lfs_info in;
        if (lfs_stat(&g_lfs, p, &in) != 0) continue;
        snprintf(out[n].name, sizeof(out[n].name), "%s", e->d_name);
        out[n].size = in.size;
        n++;
    }
    sim_closedir(d);
    return n;
}

/* каждый файл из "до" существует "после" с тем же размером */
static int same_files(const finfo_t *b, int nb, const finfo_t *a, int na) {
    for (int i = 0; i < nb; i++) {
        int ok = 0;
        for (int j = 0; j < na; j++) {
            if (strcmp(b[i].name, a[j].name) == 0 && b[i].size == a[j].size) ok = 1;
        }
        if (!ok) return 0;
    }
    return 1;
}

static void warm(uint32_t n) {
    life_init();
    life_run(n);
    af_pfs_flush_now();
}

static void sc_p1(int arg) {
    (void)arg;
    warm(6000);
    finfo_t b[200], a[200];
    int nb = list_files('r', b, 200);
    uint32_t ni = s_k[0].next_idx;
    s_k[0].cur_open = 0; /* следующий сброс открывает новый сегмент */
    g_inj_fopen_mode = 'w';
    g_inj_fopen_pfx = 'r';
    g_inj_fopen.count = 130;
    for (int i = 0; i < 130; i++) { life_run(1); af_pfs_flush_now(); }
    ICHECK(s_k[0].next_idx == ni);
    inj_clear();
    life_run(1);
    af_pfs_flush_now();
    int na = list_files('r', a, 200);
    ICHECK(nb >= 3);
    ICHECK(na == nb + 1);
    ICHECK(same_files(b, nb, a, na));
}

/* arg: 0 fopen NULL, 1 fread короче, 2 ftell -1 (устойчиво, 3 попытки); 3 - разовый сбой fopen (должен пройти повтором) */
static void sc_p2(int arg) {
    warm(40000);
    finfo_t b[200], a[200];
    int nb = list_files('r', b, 200);
    uint32_t conf = s_k[0].persisted_seq;
    ICHECK(nb >= 20);
    if (arg == 0 || arg == 3) { g_inj_fopen_mode = 'r'; g_inj_fopen_pfx = 'r'; g_inj_fopen.skip = 12; g_inj_fopen.count = (arg == 3) ? 1 : 3; }
    if (arg == 1) { g_inj_fread.skip = 12; g_inj_fread.count = 3; }
    if (arg == 2) { g_inj_ftell.skip = 12; g_inj_ftell.count = 3; }
    life_init();
    inj_clear();
    int na = list_files('r', a, 200);
    ICHECK(same_files(b, nb, a, na));
    uint32_t m;
    verify_content(&m);
    ICHECK(m == 0);
    ICHECK(g_core->raw.seq == conf);
    if (arg == 3) ICHECK(g_core->raw.count == conf);
}

/* arg: 0 - есть сегменты, 1 - сегментов нет (обрыв до первого сброса) */
static void sc_p3(int arg) {
    warm(arg ? 0 : 3000);
    life_init();
    uint32_t e1 = g_core->raw.epoch, a1 = g_core->agg.epoch;
    life_run(100);
    uint32_t cli_seq = g_core->raw.seq, cli_aseq = g_core->agg.seq;
    life_init(); /* питание пропало без сброса */
    ICHECK(g_core->raw.seq < cli_seq);
    ICHECK(g_core->raw.epoch != e1);
    if (g_core->agg.seq < cli_aseq) ICHECK(g_core->agg.epoch != a1);
}

/* arg: 0 - отказ heap_caps_malloc, 1 - отказ opendir при восстановлении вида r */
static void sc_p4(int arg) {
    warm(6000);
    finfo_t b[200], a[200];
    int nb = list_files('r', b, 200);
    ICHECK(nb >= 3);
    if (arg == 0) g_inj_malloc.count = 1;
    else g_inj_opendir.count = 1;
    life_init();
    inj_clear();
    ICHECK(g_restore_rc != ESP_OK);
    life_run(3000);
    af_pfs_flush_now();
    int na = list_files('r', a, 200);
    ICHECK(same_files(b, nb, a, na));
}

static void sc_p7(int arg) {
    (void)arg;
    warm(1000);
    s_k[0].cur_open = 0;
    uint32_t e0 = s_flush_errs;
    g_inj_fclose.count = 1;
    g_inj_fclose_drop = 1;
    life_run(1);
    af_pfs_flush_now(); /* fclose заголовка нового сегмента даёт EOF и теряет файл */
    inj_clear();
    ICHECK(s_flush_errs > e0);
    life_run(1000);
    af_pfs_flush_now();
    uint32_t conf = s_k[0].persisted_seq;
    life_init();
    ICHECK(g_core->raw.seq >= conf);
    uint32_t m;
    verify_content(&m);
    ICHECK(m == 0);
}

/* arg: 0 - fopen NULL, 1 - fread короче; отказ на втором проходе восстановления, на втором грузимом сегменте */
static void sc_p10(int arg) {
    warm(40000);
    uint32_t conf = s_k[0].persisted_seq;
    finfo_t b[200];
    int nb = list_files('r', b, 200);
    if (arg == 0) { g_inj_fopen_mode = 'r'; g_inj_fopen_pfx = 'r'; g_inj_fopen.skip = nb + 1; g_inj_fopen.count = 3; }
    else { g_inj_fread.skip = nb + 2; g_inj_fread.count = 3; } /* +1: чтение файла epoch перед проходом 1 (после правки п.3) */
    life_init();
    inj_clear();
    uint32_t m;
    verify_content(&m);
    ICHECK(m == 0);
    ICHECK(g_core->raw.seq == conf);
    ICHECK(g_core->raw.count > 0);
}

/* e10 (переписано в e16e под один проход): сегмент, прочитанный с меньшим valid (испорчена запись №5), обязан разорвать участок:
   загруженное до него сбрасывается, иначе метки seq сдвигаются (долг e8 - защита сверки valid была без теста) */
static void sc_p14(int arg) {
    (void)arg;
    warm(40000);
    uint32_t conf = s_k[0].persisted_seq;
    finfo_t b[200];
    int nb = list_files('r', b, 200);
    ICHECK(nb >= 20);
    /* e16e: восстановление в один проход - второго чтения нет; портим чтение сегмента в середине списка (1 чтение epoch + nb/2):
       сегмент с меньшим valid разрывает нумерацию, ранее загруженное сбрасывается, остаётся только участок после него */
    g_inj_fread_corrupt.skip = 1 + nb / 2;
    g_inj_fread_corrupt.count = 1;
    life_init();
    inj_clear();
    uint32_t m;
    verify_content(&m);
    ICHECK(m == 0);
    ICHECK(g_core->raw.seq == conf);
    ICHECK(g_core->raw.count > 0);
    ICHECK(g_core->raw.count < conf);
}

static void sc_p11(int arg) {
    (void)arg;
    warm(1000);
    uint32_t e0 = s_flush_errs;
    g_inj_sem_fail = 1;
    af_pfs_flush_now();
    g_inj_sem_fail = 0;
    ICHECK(s_flush_errs > e0);
}

/* охрана буфера статуса: рост JSON (поле restore_fail, блок env) не должен вытолкнуть его за 800 байт обработчика (af_history_http.c: pj[800]) */
static void sc_p13(int arg) {
    (void)arg;
    warm(100);
    s_flushes = s_flush_errs = s_bytes_written = s_last_flush_ms = s_max_flush_ms = s_restore_ms = 999999u;
    s_restore_gap = s_restore_tail_bad = s_restore_fail = 999999u;
    for (int i = 0; i < 3; i++) {
        s_k[i].persisted_seq = s_k[i].restored = s_k[i].cur_valid = s_k[i].next_idx = 999999u;
    }
    char big[2048], buf[800];
    printf("INFO status_json_len_6digit_counters=%d cap=800\n", af_pfs_status_json(big, sizeof(big)));
    int n = af_pfs_status_json(buf, sizeof(buf));
    ICHECK(n > 0);
}

typedef struct { const char *name; void (*fn)(int); int arg; } sc_t;
static const sc_t SCENARIOS[] = {
    {"p1_fopen_wb", sc_p1, 0}, {"p2_fopen", sc_p2, 0}, {"p2_fread", sc_p2, 1}, {"p2_ftell", sc_p2, 2},
    {"p2_transient", sc_p2, 3}, {"p3_epoch", sc_p3, 0}, {"p3_nodata", sc_p3, 1}, {"p13_status_len", sc_p13, 0}, {"p4_malloc", sc_p4, 0}, {"p4_opendir", sc_p4, 1},
    {"p7_fclose", sc_p7, 0}, {"p10_fopen", sc_p10, 0}, {"p10_fread", sc_p10, 1}, {"p11_sem", sc_p11, 0}, {"p14_valid_mismatch", sc_p14, 0},
};

static int run_inject(const char *name) {
    for (size_t i = 0; i < sizeof(SCENARIOS) / sizeof(SCENARIOS[0]); i++) {
        if (strcmp(SCENARIOS[i].name, name) != 0) continue;
        memset(sh->flash, 0xFF, NBLK * BLK);
        sh->crash_at = 0;
        alarm(60);
        SCENARIOS[i].fn(SCENARIOS[i].arg);
        printf("INJ %s %s failures=%d\n", name, g_inj_fail ? "RED" : "GREEN", g_inj_fail);
        return g_inj_fail ? 1 : 0;
    }
    fprintf(stderr, "unknown scenario %s\n", name);
    return 2;
}
