#include "af_test.h"
#include <string.h>
#include <stdint.h>
#include "ble_tgt.h"

/* b29 поток H (B06): смена цели A -> B при сбоях NVS на записи имени (tnm). Адреса и имена — как в test_ble_sel.c. */
static const uint8_t A[6] = {0xA0, 0x6C, 0x3B, 0x51, 0x9D, 0xF5};
static const uint8_t B[6] = {0xA0, 0x6C, 0x47, 0x02, 0xC8, 0xE1};
static const char NA[] = "AtomFast ";
static const char NB[] = "AtomSwift";

typedef struct { int ok, rc; char ram[16]; } sw_t;   /* ok — цель записана; ram — имя, которое оболочка кладёт в ОЗУ */

#ifdef BLE_TGT_SAVE_NONAME
/* ble_gw.c dsw_tgt_save + ble_devsw.h (@name_unsaved, @ram_name_nvs) */
static sw_t do_save(const uint8_t a[6], const char *name) {
    sw_t r;
    memset(&r, 0, sizeof r);
    r.rc = ble_tgt_save_named(a, 0, name);
    r.ok = (r.rc != -1);
    if (r.rc == 0) snprintf(r.ram, sizeof r.ram, "%s", name);
    return r;
}
#else
/* модель ble_gw.c:936-941 на 80cce32: имя — вторым вызовом, его ошибка не видна; ОЗУ берёт req.name */
static sw_t do_save(const uint8_t a[6], const char *name) {
    sw_t r;
    memset(&r, 0, sizeof r);
    esp_err_t rc = ble_tgt_save(a, 0);
    if (rc == ESP_OK) (void)ble_tgt_name_save(name);
    r.ok = (rc == ESP_OK);
    r.rc = r.ok ? 0 : -1;
    if (r.ok) snprintf(r.ram, sizeof r.ram, "%s", name);
    return r;
}
#endif

/* в NVS прежний прибор A с его именем */
static void boot_A(void) {
    uint8_t b[7];
    memset(&FN, 0, sizeof FN);
    FN.fail_erase_tnm_from = -1;
    memcpy(b, A, 6);
    b[6] = 0;
    fnvs_put(&FN.tgt, b, 7);
    fnvs_put(&FN.tnm, NA, sizeof NA);
}

/* перезагрузка: сбоев нет, читается то, что во флеше */
static void reboot_read(uint8_t a[6], int *has, char nm[16]) {
    uint8_t t = 0;
    FN.fail_set_tnm = 0;
    FN.fail_get_tnm = 0;
    FN.fail_commit_tnm = 0;
    FN.fail_erase_tnm_from = -1;
    FN.frozen = 0;
    *has =(ble_tgt_read(a, &t) == ESP_OK);
    (void)ble_tgt_name_read(nm, 16);
}

/* инвариант B06: имя в NVS принадлежит цели в NVS; при записанной цели ОЗУ показывает то же имя, что NVS после перезагрузки */
static void check_consistent(const sw_t *r) {
    uint8_t a[6];
    int has = 0;
    char nm[16];
    reboot_read(a, &has, nm);
    CHECK(has);
    if (r->ok) {
        CHECK(memcmp(a, B, 6) == 0);
        CHECK(strcmp(nm, r->ram) == 0);
    }
    if (has && memcmp(a, B, 6) == 0) CHECK(strcmp(nm, NA) != 0);
}

static void T1_no_fault(void) {
    boot_A();
    sw_t r = do_save(B, NB);
    CHECK(r.ok && r.rc == 0 && strcmp(r.ram, NB) == 0);
    check_consistent(&r);
}

static void T2_name_write_fails_once(void) {
    boot_A();
    FN.fail_set_tnm = 1;
    sw_t r = do_save(B, NB);
    check_consistent(&r);
    CHECK(r.rc == 0 && strcmp(r.ram, NB) == 0);   /* повтор записи имени */
}

static void T3_name_write_fails_always(void) {
    boot_A();
    FN.fail_set_tnm = -1;
    sw_t r = do_save(B, NB);
    CHECK(r.ok);
    check_consistent(&r);
#ifdef BLE_TGT_SAVE_NONAME
    CHECK(r.rc == BLE_TGT_SAVE_NONAME && r.ram[0] == '\0');
#endif
}

static void T4_name_commit_fails(void) {
    boot_A();
    FN.fail_commit_tnm = -1;
    sw_t r = do_save(B, NB);
    CHECK(r.ok);
    check_consistent(&r);
}

/* стирание старого имени не удалось — цель не меняется (контроль: на старом коде шага нет) */
static void T5_old_name_erase_fails(void) {
    boot_A();
    FN.fail_erase_tnm_from = 0;
    sw_t r = do_save(B, NB);
    check_consistent(&r);
}

static void T6_name_fails_and_later_erase_fails(void) {
    boot_A();
    FN.fail_set_tnm = -1;
    FN.fail_erase_tnm_from = 1;
    sw_t r = do_save(B, NB);
    CHECK(r.ok);
    check_consistent(&r);
}

/* запись имени прошла, контрольное чтение — нет (контроль: старый код чтения не делает) */
static void T7_name_readback_fails(void) {
    boot_A();
    FN.fail_get_tnm = -1;
    sw_t r = do_save(B, NB);
    CHECK(r.ok);
    check_consistent(&r);
}

/* отказ питания/флеша сразу после записи цели: имени прежнего прибора у новой цели быть не должно */
static void T8_power_loss_after_tgt(void) {
    boot_A();
    FN.freeze_after_tgt = 1;
    sw_t r = do_save(B, NB);
    check_consistent(&r);
}

int main(void) {
    RUN(T1_no_fault);
    RUN(T2_name_write_fails_once);
    RUN(T3_name_write_fails_always);
    RUN(T4_name_commit_fails);
    RUN(T5_old_name_erase_fails);
    RUN(T6_name_fails_and_later_erase_fails);
    RUN(T7_name_readback_fails);
    RUN(T8_power_loss_after_tgt);
    TESTS_DONE();
}
