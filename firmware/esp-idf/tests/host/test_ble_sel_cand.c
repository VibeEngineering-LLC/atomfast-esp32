#include "af_test.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "ble_sel_json.h"

static void T6_cand_table(void)
{
    static const uint8_t A[6] = {0xA0, 0x6C, 0x3B, 0x51, 0x9D, 0xF5};
    static const uint8_t X[6] = {0xA0, 0x6C, 0x3B, 0x51, 0x9D, 0xE1};
    static const uint8_t Y[6] = {0xA0, 0x6C, 0x47, 0x02, 0xC8, 0xE1};

    ble_cands_t t;
    ble_cands_clear(&t);

    int idx = ble_cands_update(&t, A, 0, -50, "AtomFast ", false, 100);
    CHECK(idx == 0);
    CHECK(t.c[0].tok == 1);
    CHECK(t.c[0].kind == BLE_KIND_ATOMFAST);

    idx = ble_cands_update(&t, A, 1, -60, "AtomFast ", false, 110);
    CHECK(idx == 1);
    CHECK(ble_cands_count(&t) == 2);

    idx = ble_cands_update(&t, A, 0, -40, "", false, 120);
    CHECK(idx == 0);
    CHECK(strcmp(t.c[0].name, "AtomFast ") == 0);
    CHECK(t.c[0].kind == BLE_KIND_ATOMFAST);
    CHECK(t.c[0].rssi == -40);

    idx = ble_cands_update(&t, Y, 0, -70, NULL, true, 130);
    CHECK(idx == 2);
    CHECK(t.c[2].kind == BLE_KIND_FAMILY);
    idx = ble_cands_update(&t, Y, 0, -71, "AtomSwift", false, 140);
    CHECK(idx == 2);
    CHECK(strcmp(t.c[2].name, "AtomSwift") == 0);
    CHECK(t.c[2].kind == BLE_KIND_ATOMSWIFT);

    idx = ble_cands_update(&t, X, 0, -50, "Xiaomi", false, 150);
    CHECK(idx == -1);
    CHECK(ble_cands_count(&t) == 3);

    for (int k = 1; k <= 5; k++) {
        uint8_t Dk[6] = {0xA0, 0x6C, 0x10, 0x20, 0x30, (uint8_t)k};
        (void)ble_cands_update(&t, Dk, 0, -60, NULL, true, (uint32_t)(200 + k));
    }
    CHECK(ble_cands_count(&t) == 8);

    uint8_t D6[6] = {0xA0, 0x6C, 0x10, 0x20, 0x30, 6};
    idx = ble_cands_update(&t, D6, 0, -60, NULL, true, 300);
    CHECK(idx == 1);
    CHECK(ble_cands_count(&t) == 8);
    CHECK(ble_cands_find_tok(&t, 2) == -1);
    CHECK(t.c[1].tok == 9);
    CHECK(memcmp(t.c[1].addr, D6, 6) == 0);

    CHECK(ble_kind_of("ATOMFAST-1") == BLE_KIND_ATOMFAST);
    CHECK(ble_kind_of("atomswift") == BLE_KIND_ATOMSWIFT);
    CHECK(ble_kind_of("Atom") == BLE_KIND_FAMILY);
    CHECK(ble_kind_of(NULL) == BLE_KIND_FAMILY);
}

static void T13_mask_json(void)
{
    static const uint8_t A[6] = {0xA0, 0x6C, 0x3B, 0x51, 0x9D, 0xF5};
    static const uint8_t X[6] = {0xA0, 0x6C, 0x3B, 0x51, 0x9D, 0xE1};
    static const uint8_t Y[6] = {0xA0, 0x6C, 0x47, 0x02, 0xC8, 0xE1};

    char m[18];
    ble_sel_mask(A, false, m);
    CHECK(strcmp(m, "A0:6C:**:**:**:F5") == 0);
    ble_sel_mask(A, true, m);
    CHECK(strcmp(m, "A0:6C:**:**:9D:F5") == 0);

    ble_sel_t s;
    ble_acts_t o;
    ble_sel_init(&s, NULL, 0, NULL, 1000, &o);
    (void)ble_sel_on_scan_result(&s, A, 0, -50, "AtomFast ", true, 2000, &o);
    (void)ble_sel_on_scan_result(&s, X, 0, -60, "AtomSwift", true, 2001, &o);
    (void)ble_sel_on_scan_result(&s, Y, 0, -70, "AtomSwift", true, 2002, &o);

    char buf[2048];
    int n = ble_sel_json(&s, 9200, buf, 2048);
    CHECK(n > 0);
    CHECK(strlen(buf) == (size_t)n);
    CHECK(strstr(buf, "\"state\":\"scan\"") != NULL);
    CHECK(strstr(buf, "\"elapsed_ms\":8200") != NULL);
    CHECK(strstr(buf, "\"limit_ms\":20000},\"tgt\":") != NULL);
    CHECK(strstr(buf, ",\"conn\":") != NULL);
    CHECK(strstr(buf, ",\"cands\":[") != NULL);
    CHECK(strstr(buf, "A0:6C:**:**:**:F5") != NULL);
    CHECK(strstr(buf, "A0:6C:**:**:9D:E1") != NULL);
    CHECK(strstr(buf, "A0:6C:**:**:C8:E1") != NULL);

    char full[32];
    snprintf(full, sizeof full, "%02X:%02X:%02X:%02X:%02X:%02X", A[0], A[1], A[2], A[3], A[4], A[5]);
    CHECK(strstr(buf, full) == NULL);
    snprintf(full, sizeof full, "%02X:%02X:%02X:%02X:%02X:%02X", X[0], X[1], X[2], X[3], X[4], X[5]);
    CHECK(strstr(buf, full) == NULL);
    snprintf(full, sizeof full, "%02X:%02X:%02X:%02X:%02X:%02X", Y[0], Y[1], Y[2], Y[3], Y[4], Y[5]);
    CHECK(strstr(buf, full) == NULL);
    CHECK(strstr(buf, "3B:51") == NULL);
    CHECK(strstr(buf, "47:02") == NULL);

    ble_sel_init(&s, NULL, 0, NULL, 1000, &o);
    for (int k = 1; k <= 8; k++) {
        uint8_t Dk[6] = {0xA0, 0x6C, 0x10, 0x20, 0x30, (uint8_t)k};
        (void)ble_sel_on_scan_result(&s, Dk, 0, -60, "AtomSwift-12345", true, (uint32_t)(1000 + k), &o);
    }
    n = ble_sel_json(&s, 5000, buf, 2048);
    CHECK(n > 0);
    CHECK(n < 2048);

    char small[64];
    CHECK(ble_sel_json(&s, 5000, small, sizeof small) == BLE_E_NOSPACE);
    CHECK(strlen(small) < 64);

    ble_sel_init(&s, NULL, 0, NULL, 1000, &o);
    uint8_t Esc[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    (void)ble_sel_on_scan_result(&s, Esc, 0, -50, "Ato\"m\\x", true, 2000, &o);
    n = ble_sel_json(&s, 9200, buf, 2048);
    CHECK(n > 0);
    CHECK(strstr(buf, "Ato\\\"m\\\\x") != NULL);
}

int main(void)
{
    RUN(T6_cand_table);
    RUN(T13_mask_json);
    TESTS_DONE();
}
