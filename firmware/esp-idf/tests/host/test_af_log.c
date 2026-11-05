#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "af_test.h"
#include "af_log.h"

static af_log_ent_t RING[200];
static af_log_core_t C;
static int g_real_samples = 0;

static void fresh(uint32_t cap) {
    memset(RING, 0, sizeof RING);
    CHECK(af_log_core_init(&C, RING, cap, 1) == 0);
}

static int pushn(int level, const char *tag, const char *msg, uint32_t t) {
    return af_log_push(&C, level, tag, msg, t, 1700000000u + t);
}

static const char *real_dump_value(const char *file, const char *key, char *out, size_t cap) {
    const char *dir = getenv("AF_DUMP_DIR");
    if (!dir) dir = "/mnt/c/Temp/af_integration_dump/d1";
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, strlen(key)) == 0) {
            char *val = line + strlen(key);
            while (*val == ' ' || *val == '\t') val++;
            size_t len = strlen(val);
            if (len > 0 && (val[len-1] == '\n' || val[len-1] == '\r')) val[len-1] = 0;
            if (len > 0 && (val[len-1] == '\n' || val[len-1] == '\r')) val[len-1] = 0;
            if (cap > 0) {
                snprintf(out, cap, "%.*s", (int)(cap - 1), val);
                out[cap - 1] = 0;
            }
            fclose(f);
            return out;
        }
    }
    fclose(f);
    return NULL;
}

static void t_init(void) {
    CHECK(af_log_core_init(NULL, RING, 200, 1) == -1);
    CHECK(af_log_core_init(&C, NULL, 200, 1) == -1);
    CHECK(af_log_core_init(&C, RING, 1, 1) == -1);

    memset(RING, 0, sizeof RING);
    CHECK(af_log_core_init(&C, RING, 200, 0) == 0);
    CHECK(C.next_seq == 1);
    CHECK(C.flushed == 1);
    CHECK(C.barrier == 1);
    CHECK(C.tok == AF_LOG_BUCKET_CAP);
    CHECK(C.etok == AF_LOG_ERR_BUCKET_CAP);

    RING[3].seq = 9;
    CHECK(af_log_core_init(&C, RING, 200, 500) == 0);
    CHECK(C.next_seq == 500);
    CHECK(C.flushed == 500);
    CHECK(C.barrier == 500);
    CHECK(RING[3].seq == 0);
}

static void t_push_basic(void) {
    fresh(200);
    CHECK(pushn(1, "ble", "connected", 10) == AF_LOG_R_NEW);
    const af_log_ent_t *e = af_log_get(&C, 1);
    CHECK(e != NULL);
    if (e) {
        CHECK(e->seq == 1);
        CHECK(e->mono_s == 10);
        CHECK(e->last_s == 10);
        CHECK(e->utc == 1700000010);
        CHECK(e->count == 1);
        CHECK(e->level == 1);
        CHECK(strcmp(e->tag, "ble") == 0);
        CHECK(strcmp(e->msg, "connected") == 0);
    }
    CHECK(af_log_get(&C, 0) == NULL);
    CHECK(af_log_get(&C, 2) == NULL);
    CHECK(af_log_pending(&C) == 1);

    CHECK(pushn(1, "ble", "disconnected", 11) == AF_LOG_R_NEW);
    e = af_log_get(&C, 2);
    CHECK(e != NULL);
    if (e) CHECK(e->seq == 2);
}

static void t_level_tag(void) {
    fresh(200);
    CHECK(pushn(0, "t", "m", 1) == AF_LOG_R_NEW);
    const af_log_ent_t *e = af_log_get(&C, 1);
    CHECK(e && e->level == 1);

    CHECK(pushn(9, "t", "m", 2) == AF_LOG_R_NEW);
    e = af_log_get(&C, 2);
    CHECK(e && e->level == 3);

    CHECK(pushn(-5, "t", "m3", 3) == AF_LOG_R_NEW);
    e = af_log_get(&C, 3);
    CHECK(e && e->level == 1);

    CHECK(pushn(1, NULL, "m4", 4) == AF_LOG_R_NEW);
    e = af_log_get(&C, 4);
    CHECK(e && strcmp(e->tag, "-") == 0);

    CHECK(pushn(1, "", "m5", 5) == AF_LOG_R_NEW);
    e = af_log_get(&C, 5);
    CHECK(e && strcmp(e->tag, "-") == 0);

    CHECK(pushn(1, "abcdefghijkl", "m6", 6) == AF_LOG_R_NEW);
    e = af_log_get(&C, 6);
    CHECK(e && strcmp(e->tag, "abcdefg") == 0);

    CHECK(pushn(1, "t", NULL, 7) == AF_LOG_R_NEW);
    e = af_log_get(&C, 7);
    CHECK(e && strcmp(e->msg, "") == 0);

    CHECK(af_log_push(NULL, 1, "t", "m", 1, 1) == AF_LOG_R_DROPPED);
    
    af_log_core_t bad;
    memset(&bad, 0, sizeof bad);
    bad.ring = NULL;
    CHECK(af_log_push(&bad, 1, "t", "m", 1, 1) == AF_LOG_R_DROPPED);
}

static void t_sanitize(void) {
    char d[8];
    af_log_sanitize(d, 8, "a\tb\nc\rd\x01e");
    CHECK(strcmp(d, "a b c d") == 0);

    af_log_sanitize(d, 1, "abc");
    CHECK(d[0] == 0);

    af_log_sanitize(d, 8, NULL);
    CHECK(d[0] == 0);

    const char *src = "ab\xD0\xB0\xD0\xB1\xD0\xB2";
    af_log_sanitize(d, 5, src);
    CHECK(d[0] == 'a' && d[1] == 'b' && (unsigned char)d[2] == 0xD0 && (unsigned char)d[3] == 0xB0 && d[4] == 0);

    af_log_sanitize(d, 4, src);
    CHECK(d[0] == 'a' && d[1] == 'b' && d[2] == 0);

    char msg[128];
    for (int i = 0; i < 60; i++) {
        msg[i*2] = (char)0xD0;
        msg[i*2+1] = (char)0xB0;
    }
    msg[120] = 0;
    
    fresh(200);
    CHECK(pushn(1, "t", msg, 1) == AF_LOG_R_NEW);
    const af_log_ent_t *e = af_log_get(&C, 1);
    CHECK(e != NULL);
    if (e) {
        size_t len = strlen(e->msg);
        CHECK(len <= 95);
        CHECK(len % 2 == 0);
        for (size_t i = 0; i < len; i += 2) {
            CHECK((unsigned char)e->msg[i] == 0xD0);
            CHECK((unsigned char)e->msg[i+1] == 0xB0);
        }
    }
}

static void t_redact(void) {
    char buf[256];
    
    strcpy(buf, "mqtt pass=hunter2 user=bob");
    af_log_redact(buf);
    CHECK(strcmp(buf, "mqtt pass=******* user=bob") == 0);

    strcpy(buf, "token: abcdef");
    af_log_redact(buf);
    CHECK(strcmp(buf, "token: ******") == 0);

    strcpy(buf, "password=Zq9 end");
    af_log_redact(buf);
    CHECK(strcmp(buf, "password=*** end") == 0);

    strcpy(buf, "KEY=zz");
    af_log_redact(buf);
    CHECK(strcmp(buf, "KEY=**") == 0);

    strcpy(buf, "monkey=1");
    af_log_redact(buf);
    CHECK(strcmp(buf, "monkey=1") == 0);

    strcpy(buf, "ver 1.2.29 ok");
    af_log_redact(buf);
    CHECK(strcmp(buf, "ver 1.2.29 ok") == 0);

    strcpy(buf, "ip 192.168.1.5 x");
    af_log_redact(buf);
    CHECK(strcmp(buf, "ip *********** x") == 0);

    strcpy(buf, "mac aa:bb:cc:dd:ee:ff up");
    af_log_redact(buf);
    CHECK(strcmp(buf, "mac ***************** up") == 0);

    strcpy(buf, "AA-BB-CC-DD-EE-FF");
    af_log_redact(buf);
    CHECK(strcmp(buf, "*****************") == 0);

    strcpy(buf, "keyboard");
    af_log_redact(buf);
    CHECK(strcmp(buf, "keyboard") == 0);

    strcpy(buf, "pass");
    af_log_redact(buf);
    CHECK(strcmp(buf, "pass") == 0);

    strcpy(buf, "pass=");
    af_log_redact(buf);
    CHECK(strcmp(buf, "pass=") == 0);

    strcpy(buf, "a=1.2.3.4.5");
    size_t len_before = strlen(buf);
    af_log_redact(buf);
    CHECK(strlen(buf) == len_before);

    char val[64];
    if (real_dump_value("raw.hdr", "X-AF-Device: ", val, sizeof val)) {
        char test[128];
        snprintf(test, sizeof test, "dev %s connected", val);
        size_t orig_len = strlen(test);
        af_log_redact(test);
        CHECK(strlen(test) == orig_len);
        CHECK(strstr(test, val) == NULL);
        CHECK(strncmp(test, "dev ", 4) == 0);
        CHECK(strstr(test, " connected") != NULL);
        g_real_samples++;

        char lower_val[64];
        for (size_t i = 0; val[i]; i++) lower_val[i] = tolower(val[i]);
        lower_val[strlen(val)] = 0;
        
        snprintf(test, sizeof test, "dev %s connected", lower_val);
        orig_len = strlen(test);
        af_log_redact(test);
        CHECK(strlen(test) == orig_len);
        CHECK(strstr(test, lower_val) == NULL);
        g_real_samples++;
    }
}

static void t_push_redacts(void) {
    fresh(200);
    CHECK(pushn(1, "mqtt", "login pass=hunter2", 5) == AF_LOG_R_NEW);
    const af_log_ent_t *e = af_log_get(&C, 1);
    CHECK(e && strcmp(e->msg, "login pass=*******") == 0);
}

static void t_collapse(void) {
    fresh(200);
    int first = pushn(1, "t", "m", 100);
    CHECK(first == AF_LOG_R_NEW);
    for (int i = 101; i <= 1099; i++) {
        CHECK(pushn(1, "t", "m", i) == AF_LOG_R_COLLAPSED);
    }
    CHECK(af_log_pending(&C) == 1);
    const af_log_ent_t *e = af_log_get(&C, 1);
    CHECK(e && e->count == 1000);
    CHECK(e && e->last_s == 1099);
    CHECK(e && e->mono_s == 100);
    CHECK(C.collapsed == 999);
    CHECK(C.suppressed == 0);
}

static void t_collapse_alt(void) {
    fresh(200);
    uint32_t t = 0;
    for (int i = 0; i < 40; i++) {
        pushn(1, "A", "m", t++);
        pushn(1, "B", "m", t++);
    }
    CHECK(af_log_pending(&C) == 2);
    const af_log_ent_t *ea = af_log_get(&C, 1);
    const af_log_ent_t *eb = af_log_get(&C, 2);
    CHECK(ea && ea->count == 40);
    CHECK(eb && eb->count == 40);
}

static void t_collapse_levels(void) {
    fresh(200);
    CHECK(pushn(1, "t", "m", 1) == AF_LOG_R_NEW);
    CHECK(pushn(2, "t", "m", 2) == AF_LOG_R_NEW);
    CHECK(af_log_pending(&C) == 2);

    fresh(200);
    CHECK(pushn(1, "t1", "m", 1) == AF_LOG_R_NEW);
    CHECK(pushn(1, "t2", "m", 2) == AF_LOG_R_NEW);
    CHECK(af_log_pending(&C) == 2);
}

static void t_collapse_window(void) {
    fresh(200);
    CHECK(pushn(1, "t", "m", 0) == AF_LOG_R_NEW);
    CHECK(pushn(1, "t", "m", 599) == AF_LOG_R_COLLAPSED);
    CHECK(pushn(1, "t", "m", 1199) == AF_LOG_R_NEW);
    CHECK(af_log_pending(&C) == 2);
}

static void t_no_collapse_after_flush(void) {
    fresh(200);
    CHECK(pushn(1, "t", "m", 1) == AF_LOG_R_NEW);
    af_log_mark_flushed(&C, C.next_seq);
    CHECK(pushn(1, "t", "m", 2) == AF_LOG_R_NEW);
    const af_log_ent_t *e = af_log_get(&C, 2);
    CHECK(e && e->seq == 2);
}

static void t_barrier(void) {
    fresh(200);
    CHECK(pushn(1, "t", "m", 1) == AF_LOG_R_NEW);
    uint32_t s = af_log_snapshot(&C);
    CHECK(pushn(1, "t", "m", 2) == AF_LOG_R_NEW);
    af_log_mark_flushed(&C, s);
    CHECK(af_log_pending(&C) == 1);
    CHECK(pushn(1, "t", "m", 3) == AF_LOG_R_COLLAPSED);
    const af_log_ent_t *e = af_log_get(&C, 2);
    CHECK(e && e->count == 2);

    fresh(200);
    CHECK(pushn(1, "t", "m", 1) == AF_LOG_R_NEW);
    af_log_snapshot(&C);
    af_log_abort_flush(&C);
    CHECK(pushn(1, "t", "m", 2) == AF_LOG_R_COLLAPSED);
}

static void t_count_sat(void) {
    fresh(200);
    for (int i = 0; i < 70000; i++) {
        pushn(1, "t", "m", 0);
    }
    const af_log_ent_t *e = af_log_get(&C, 1);
    CHECK(e && e->count == 65535);
    CHECK(af_log_pending(&C) == 1);
}

static void t_ring_overflow(void) {
    fresh(200);
    char msg[16];
    for (int i = 0; i < 450; i++) {
        snprintf(msg, sizeof msg, "m%d", i);
        CHECK(pushn(1, "t", msg, i) == AF_LOG_R_NEW);
    }
    CHECK(C.next_seq == 451);
    CHECK(C.lost == 250);
    CHECK(C.flushed == 251);
    CHECK(af_log_pending(&C) == 200);
    CHECK(af_log_get(&C, 250) == NULL);
    CHECK(af_log_get(&C, 251) != NULL);
    CHECK(af_log_get(&C, 450) != NULL);
    af_log_mark_flushed(&C, 451);
    CHECK(af_log_pending(&C) == 0);
}

static void t_overwrite_flushed(void) {
    fresh(4);
    for (int i = 0; i < 4; i++) {
        char msg[16];
        snprintf(msg, sizeof msg, "m%d", i);
        pushn(1, "t", msg, i);
    }
    af_log_mark_flushed(&C, 5);
    for (int i = 4; i < 7; i++) {
        char msg[16];
        snprintf(msg, sizeof msg, "m%d", i);
        pushn(1, "t", msg, i);
    }
    CHECK(C.lost == 0);
}

static void t_rate_info(void) {
    fresh(200);
    int new_count = 0, drop_count = 0;
    for (int i = 0; i < 100; i++) {
        char msg[16];
        snprintf(msg, sizeof msg, "m%d", i);
        int r = pushn(1, "t", msg, 1000);
        if (r == AF_LOG_R_NEW) new_count++;
        else if (r == AF_LOG_R_DROPPED) drop_count++;
    }
    CHECK(new_count == 30);
    CHECK(drop_count == 70);
    CHECK(C.suppressed == 70);
    CHECK(C.supp_pending == 70);

    for (int i = 0; i < 10; i++) {
        char msg[16];
        snprintf(msg, sizeof msg, "e%d", i);
        CHECK(pushn(3, "t", msg, 1000) == AF_LOG_R_NEW);
    }
    char msg[16];
    snprintf(msg, sizeof msg, "e10");
    CHECK(pushn(3, "t", msg, 1000) == AF_LOG_R_DROPPED);
    CHECK(C.suppressed == 71);

    CHECK(pushn(1, "t", "next", 1030) == AF_LOG_R_NEW);
    const af_log_ent_t *e = af_log_get(&C, C.next_seq - 2);
    CHECK(e != NULL);
    if (e) {
        CHECK(strcmp(e->tag, "log") == 0);
        CHECK(e->level == 2);
        CHECK(strcmp(e->msg, "suppressed 71 events") == 0);
    }
    CHECK(C.supp_pending == 0);
}

static void t_rate_clock_back(void) {
    fresh(200);
    CHECK(pushn(1, "t", "m1", 5000) == AF_LOG_R_NEW);
    CHECK(pushn(1, "t", "m2", 10) == AF_LOG_R_NEW);
}

static void t_urgent(void) {
    fresh(200);
    CHECK(pushn(1, "t", "m", 1) == AF_LOG_R_NEW);
    CHECK(C.urgent == 0);
    CHECK(pushn(3, "t", "m", 2) == AF_LOG_R_NEW);
    CHECK(C.urgent == 1);
    af_log_mark_flushed(&C, C.next_seq - 1);
    CHECK(C.urgent == 1);
    af_log_mark_flushed(&C, C.next_seq);
    CHECK(C.urgent == 0);
}

static void t_mark_flushed_clamp(void) {
    fresh(200);
    pushn(1, "t", "m", 1);
    af_log_mark_flushed(&C, 1000);
    CHECK(C.flushed == C.next_seq);
    af_log_mark_flushed(&C, 0);
    CHECK(C.flushed == C.next_seq);
}

static void t_rebase(void) {
    fresh(200);
    for (int i = 1; i <= 5; i++) {
        char msg[16];
        snprintf(msg, sizeof msg, "m%d", i);
        pushn(1, "t", msg, i);
    }
    af_log_rebase(&C, 700);
    CHECK(C.next_seq == 806);
    CHECK(C.flushed == 801);
    CHECK(C.barrier == 801);
    for (int i = 801; i <= 805; i++) {
        CHECK(af_log_get(&C, i) != NULL);
    }
    CHECK(af_log_get(&C, 1) == NULL);
    CHECK(af_log_pending(&C) == 5);
    CHECK(pushn(1, "t", "m6", 6) == AF_LOG_R_NEW);
    const af_log_ent_t *e = af_log_get(&C, 806);
    CHECK(e && e->seq == 806);

    af_log_rebase(&C, 0);
    CHECK(C.next_seq == 807);
    af_log_rebase(NULL, 100);

    /* flushed уже выше последнего номера на флеше: сдвиг не нужен */
    fresh(200);
    pushn(1, "t", "a", 1);
    pushn(1, "t", "b", 2);
    af_log_mark_flushed(&C, 3);
    uint32_t nx = C.next_seq;
    af_log_rebase(&C, 2);
    CHECK(C.next_seq == nx);

    /* записи 1..2 ещё не на флеше, а на флеше уже номер 3: сдвиг на cap */
    fresh(200);
    pushn(1, "t", "a", 1);
    pushn(1, "t", "b", 2);
    af_log_rebase(&C, 3);
    CHECK(C.next_seq == 203);
    CHECK(C.flushed == 201);
    CHECK(af_log_get(&C, 201) != NULL && af_log_get(&C, 202) != NULL);
}

static void t_clear_ram(void) {
    fresh(200);
    for (int i = 1; i <= 5; i++) {
        char msg[16];
        snprintf(msg, sizeof msg, "m%d", i);
        pushn(1, "t", msg, i);
    }
    af_log_clear_ram(&C);
    CHECK(af_log_pending(&C) == 0);
    CHECK(af_log_get(&C, 3) == NULL);
    CHECK(C.next_seq == 6);
    CHECK(pushn(1, "t", "m1", 6) == AF_LOG_R_NEW);
    const af_log_ent_t *e = af_log_get(&C, 6);
    CHECK(e && e->seq == 6);
}

static void t_flood_sim(void) {
    fresh(200);
    const char *msgs[3] = {"alloc_fail", "mqtt err", "i2c nack"};
    int levels[3] = {3, 2, 2};
    uint32_t total_flushed = 0;
    uint32_t total_counts = 0;
    int last_flush_t = -1;

    for (int i = 0; i < 10000; i++) {
        uint32_t t = (uint32_t)(i * 3600 / 10000);
        int idx = i % 3;
        pushn(levels[idx], "t", msgs[idx], t);

        if ((int)(t / 60) > last_flush_t / 60) {
            uint32_t n = af_log_pending(&C);
            for (uint32_t s = C.flushed; s < C.next_seq; s++) {
                const af_log_ent_t *e = af_log_get(&C, s);
                if (e) total_counts += e->count;
            }
            total_flushed += n;
            af_log_mark_flushed(&C, af_log_snapshot(&C));
            last_flush_t = (int)t;
        }
    }
    
    uint32_t n = af_log_pending(&C);
    for (uint32_t s = C.flushed; s < C.next_seq; s++) {
        const af_log_ent_t *e = af_log_get(&C, s);
        if (e) total_counts += e->count;
    }
    total_flushed += n;
    af_log_mark_flushed(&C, af_log_snapshot(&C));

    CHECK(total_flushed <= 183);
    CHECK(total_flushed >= 150);
    CHECK(total_counts == 10000 - C.suppressed);
    CHECK(C.suppressed == 0);
}

static void t_flood_distinct(void) {
    fresh(200);
    int accepted = 0;
    for (int i = 0; i < 5000; i++) {
        char msg[16];
        snprintf(msg, sizeof msg, "e%d", i);
        uint32_t t = i / 500;
        if (pushn(1, "t", msg, t) == AF_LOG_R_NEW) accepted++;
    }
    CHECK(accepted <= 40);
    CHECK(C.suppressed >= 4900);
    CHECK(af_log_pending(&C) <= 200);
}

int main(void) {
    RUN(t_init);
    RUN(t_push_basic);
    RUN(t_level_tag);
    RUN(t_sanitize);
    RUN(t_redact);
    RUN(t_push_redacts);
    RUN(t_collapse);
    RUN(t_collapse_alt);
    RUN(t_collapse_levels);
    RUN(t_collapse_window);
    RUN(t_no_collapse_after_flush);
    RUN(t_barrier);
    RUN(t_count_sat);
    RUN(t_ring_overflow);
    RUN(t_overwrite_flushed);
    RUN(t_rate_info);
    RUN(t_rate_clock_back);
    RUN(t_urgent);
    RUN(t_mark_flushed_clamp);
    RUN(t_rebase);
    RUN(t_clear_ram);
    RUN(t_flood_sim);
    RUN(t_flood_distinct);

    if (g_real_samples > 0) {
        printf("REAL_SAMPLES %d\n", g_real_samples);
    } else {
        printf("REAL_SAMPLES 0 (dump not found)\n");
    }

    TESTS_DONE();
    return 0;
}
