#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "af_test.h"
#include "af_log.h"

static af_log_ent_t RING[200];
static af_log_core_t C;

static void fresh(void) {
    CHECK(af_log_core_init(&C, RING, 200, 1) == 0);
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static const char ALPHABET[] = "abcdefghijklmnopqrstuvwxyz0123456789";

static void gen_tag(char *out, uint32_t *state, int len) {
    for (int i = 0; i < len; i++) {
        out[i] = ALPHABET[xorshift32(state) % 36];
    }
    out[len] = '\0';
}

static int cmp_hash_a(const void *a, const void *b) {
    const struct { uint32_t h; char tag[8]; } *pa = (const void *)a;
    const struct { uint32_t h; char tag[8]; } *pb = (const void *)b;
    if (pa->h < pb->h) return -1;
    if (pa->h > pb->h) return 1;
    return 0;
}

static int cmp_hash_b(const void *a, const void *b) {
    const struct { uint32_t h; char m[12]; } *pa = (const void *)a;
    const struct { uint32_t h; char m[12]; } *pb = (const void *)b;
    if (pa->h < pb->h) return -1;
    if (pa->h > pb->h) return 1;
    return 0;
}

static void t_rebase_pending(void) {
    fresh();
    for (int i = 0; i < 5; i++) {
        char msg[4];
        snprintf(msg, sizeof(msg), "e%d", i);
        af_log_push(&C, 0, "t", msg, (uint32_t)i, 0);
    }
    CHECK(C.flushed == 1);
    CHECK(C.next_seq == 6);
    
    af_log_rebase(&C, 3);
    
    CHECK(C.next_seq == 206);
    CHECK(C.flushed == 201);
    CHECK(af_log_get(&C, 201) != NULL);
    CHECK(af_log_get(&C, 205) != NULL);
    CHECK(af_log_get(&C, 1) == NULL);
    CHECK(af_log_pending(&C) == 5);

    fresh();
    af_log_push(&C, 0, "t", "a", 0, 0);
    af_log_push(&C, 0, "t", "b", 1, 0);
    C.flushed = C.next_seq;
    CHECK(C.flushed == 3);
    
    af_log_rebase(&C, 2);
    
    CHECK(C.next_seq == 3);
    CHECK(C.flushed == 3);
}

static void t_collision_level(void) {
    const int N = 400000;
    struct { uint32_t h; char tag[8]; } *arr1 = malloc(sizeof(*arr1) * N);
    struct { uint32_t h; char tag[8]; } *arr2 = malloc(sizeof(*arr2) * N);
    if (!arr1 || !arr2) {
        printf("SKIP no collision\n");
        CHECK(0);
        free(arr1);
        free(arr2);
        return;
    }

    uint32_t state = 2463534242u;
    for (int i = 0; i < N; i++) {
        gen_tag(arr1[i].tag, &state, 6);
        arr1[i].h = af_log_fnv(1, arr1[i].tag, "same");
    }
    for (int i = 0; i < N; i++) {
        gen_tag(arr2[i].tag, &state, 6);
        arr2[i].h = af_log_fnv(2, arr2[i].tag, "same");
    }

    qsort(arr1, N, sizeof(*arr1), cmp_hash_a);
    qsort(arr2, N, sizeof(*arr2), cmp_hash_a);

    int found = 0;
    char tagA[8] = {0}, tagB[8] = {0};
    int i = 0, j = 0;
    while (i < N && j < N) {
        if (arr1[i].h < arr2[j].h) {
            i++;
        } else if (arr1[i].h > arr2[j].h) {
            j++;
        } else {
            found = 1;
            memcpy(tagA, arr1[i].tag, 8);
            memcpy(tagB, arr2[j].tag, 8);
            break;
        }
    }

    if (!found) {
        printf("SKIP no collision\n");
        CHECK(0);
    } else {
        fresh();
        int r1 = af_log_push(&C, 1, tagA, "same", 10, 0);
        int r2 = af_log_push(&C, 2, tagB, "same", 11, 0);
        CHECK(r1 == AF_LOG_R_NEW);
        CHECK(r2 == AF_LOG_R_NEW);
        CHECK(af_log_pending(&C) == 2);
        const af_log_ent_t *e1 = af_log_get(&C, 1);
        const af_log_ent_t *e2 = af_log_get(&C, 2);
        CHECK(e1 != NULL && e1->count == 1);
        CHECK(e2 != NULL && e2->count == 1);
        printf("COLLISION_PAIRS_USED 1\n");
    }

    free(arr1);
    free(arr2);
}

static void t_collision_msg(void) {
    const int N = 600000;
    struct { uint32_t h; char m[12]; } *arr = malloc(sizeof(*arr) * N);
    if (!arr) {
        printf("SKIP no collision\n");
        CHECK(0);
        return;
    }

    uint32_t state = 88172645u;
    for (int i = 0; i < N; i++) {
        gen_tag(arr[i].m, &state, 8);
        arr[i].h = af_log_fnv(1, "t", arr[i].m);
    }

    qsort(arr, N, sizeof(*arr), cmp_hash_b);

    int found = 0;
    char msgA[12] = {0}, msgB[12] = {0};
    for (int i = 0; i < N - 1; i++) {
        if (arr[i].h == arr[i+1].h) {
            if (strcmp(arr[i].m, arr[i+1].m) != 0) {
                found = 1;
                memcpy(msgA, arr[i].m, 12);
                memcpy(msgB, arr[i+1].m, 12);
                break;
            }
        }
    }

    if (!found) {
        printf("SKIP no collision\n");
        CHECK(0);
    } else {
        fresh();
        int r1 = af_log_push(&C, 1, "t", msgA, 0, 0);
        int r2 = af_log_push(&C, 1, "t", msgB, 1, 0);
        CHECK(r1 == AF_LOG_R_NEW);
        CHECK(r2 == AF_LOG_R_NEW);
        CHECK(af_log_pending(&C) == 2);
        const af_log_ent_t *e1 = af_log_get(&C, 1);
        const af_log_ent_t *e2 = af_log_get(&C, 2);
        CHECK(e1 != NULL && e1->count == 1);
        CHECK(e2 != NULL && e2->count == 1);
    }

    free(arr);
}

static void t_rot_newest_protected(void) {
    af_log_rot_in_t in;
    int new_seg = -1;
    uint32_t del_n = 0;
    int rc;

    in.fs_total = 12476416;
    in.fs_used = 10000000;
    in.nfiles = 1;
    in.cur_bytes = 1000;
    in.add_bytes = 500;
    uint32_t fb1[] = {1000};
    rc = af_log_rot_plan(&in, fb1, &del_n, &new_seg);
    CHECK(rc == -1);
    CHECK(new_seg == 0);
    CHECK(del_n == 0);

    in.nfiles = 3;
    in.cur_bytes = 1000;
    in.add_bytes = 500;
    uint32_t fb2[] = {2000, 3000, 1000};
    rc = af_log_rot_plan(&in, fb2, &del_n, &new_seg);
    CHECK(rc == -1);
    CHECK(new_seg == 0);
    CHECK(del_n == 2);

    in.fs_used = 5000000;
    in.nfiles = 3;
    in.cur_bytes = 1000;
    in.add_bytes = 500;
    rc = af_log_rot_plan(&in, fb2, &del_n, &new_seg);
    CHECK(rc == 0);
    CHECK(del_n == 0);
    CHECK(new_seg == 0);
}

int main(void) {
    RUN(t_rebase_pending);
    RUN(t_collision_level);
    RUN(t_collision_msg);
    RUN(t_rot_newest_protected);
    TESTS_DONE();
}
