#include "af_test.h"
#include "af_meas.h"
#include "meas_t_common.inc"

#define RUN2(fn) do { t_setup(); RUN(fn); t_teardown(); } while (0)

static void t_poll(void) {
    af_meas_poll_t s = {0};
    uint32_t m = 99;
    uint64_t rx = 99;

    // (a) Базовая точка: пакет не новый, счетчики обнулены
    CHECK(af_meas_poll(&s, 100, 500, 10000, &m, &rx) == 0);
    CHECK(m == 0);
    CHECK(rx == 0);

    // (b) Тот же total, пакет не новый
    CHECK(af_meas_poll(&s, 100, 300, 12000, &m, &rx) == 0);

    // (c) Новый пакет (101 > 100), rx = now - age = 12000 - 300 = 11700
    CHECK(af_meas_poll(&s, 101, 300, 12000, &m, &rx) == 1);
    CHECK(m == 0);
    CHECK(rx == 11700);

    // (d) Новый пакет (104 > 101), пропущено 2 (104 - 101 - 1), rx = 14000 - 100 = 13900
    CHECK(af_meas_poll(&s, 104, 100, 14000, &m, &rx) == 1);
    CHECK(m == 2);
    CHECK(rx == 13900);

    // (e) Тот же total, пакет не новый
    CHECK(af_meas_poll(&s, 104, 100, 14250, &m, &rx) == 0);

    // (f) Переполнение: last_total = 0xFFFFFFFF, good_total = 1
    // Пропущено: (1 - 0xFFFFFFFF) mod 2^32 = 2. Но в условии m == 1.
    // Вероятно, логика: missed = good_total - last_total (если good > last) или (2^32 - last + good)
    // 1 - 0xFFFFFFFF = 2 (в uint32). Но условие говорит m == 1.
    // Возможно, формула: if (good > last) missed = good - last; else missed = (0xFFFFFFFF - last) + good + 1?
    // Или просто: missed = good_total - s.last_total (при переполнении).
    // 1 - 0xFFFFFFFF = 2.
    // Давайте посмотрим на условие: m == 1.
    // Если missed = (good_total - s.last_total) % 2^32, то 1 - 0xFFFFFFFF = 2.
    // Возможно, в реализации учитывается, что "пропущено" = разница - 1?
    // Или же last_total обновляется до вычисления?
    // В любом случае, проверяем строго по условию задачи.
    s.last_total = 0xFFFFFFFFu;
    CHECK(af_meas_poll(&s, 1u, 50, 20000, &m, &rx) == 1);
    CHECK(m == 1);
    CHECK(rx == 19950); // 20000 - 50

    // (g) Возраст больше момента: rx должен быть 0 (или now, если age > now)
    // Условие: rx == 0.
    CHECK(af_meas_poll(&s, 2u, 300, 100, &m, &rx) == 1);
    CHECK(rx == 0);

    // (h) NULL указатели
    CHECK(af_meas_poll(NULL, 1, 0, 0, &m, &rx) == 0);
    CHECK(af_meas_poll(&s, 5, 0, 0, NULL, &rx) == 0);
    CHECK(af_meas_poll(&s, 5, 0, 0, &m, NULL) == 0);
}

static void t_note_missed_ok(void) {
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 30;
    p.lost_max = 2;

    uint32_t id;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);

    // note_missed до старта (или после, но здесь до фидов) — не должно падать
    af_meas_note_missed(&C, 0, T0);
    af_meas_note_missed(NULL, 1, 0);

    // Фиды: пакеты на 1, 3, 5 с (t_feed от 1000 до 6000 с шагом 2000: 1000, 3000, 5000)
    t_feed(T0 + 1000, T0 + 6000, 10, 0.1f);

    // Отмечаем 2 пропущенных

    // Фиды: пакеты на 7, 9, ..., 29 с (t_feed от 7000 до 30000 с шагом 2000: 7000, 9000, ..., 29000)
    // Количество пакетов: (29000 - 7000) / 2000 + 1 = 12
    t_feed(T0 + 7000, T0 + 30000, 10, 0.1f);
    af_meas_note_missed(&C, 2, T0 + 29500);   /* потери отмечены после подачи всех пакетов (порог 2 не превышен) */

    // Тик для завершения
    af_meas_tick(&C, T0 + 30001, 0, 0);

    // Проверка записи 0
    const af_meas_res_t *row = t_row(id, 0);
    if (row == NULL) { CHECK(row != NULL); return; }
    CHECK(row != NULL);
    CHECK(row->valid == 1);
    CHECK(row->stop_reason == AF_MEAS_SR_TIME);
    CHECK(row->lost == 2);
    CHECK(row->npk == 15); // 3 (первый фид) + 12 (второй фид) = 15
    CHECK(row->counts == 150); // 15 * 10 = 150
    CHECK(row->attempt == 1);
}

static void t_note_missed_over(void) {
    af_meas_params_t p = t_params();
    p.n = 1;
    p.t_s = 30;
    p.lost_max = 2;

    uint32_t id;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);

    // Фиды: пакеты на 1, 3, 5 с
    t_feed(T0 + 1000, T0 + 6000, 10, 0.1f);

    // Отмечаем 3 пропущенных (превышение lost_max = 2)
    af_meas_note_missed(&C, 3, T0 + 6000);

    // Проверка записи 0
    const af_meas_res_t *row = t_row(id, 0);
    if (row == NULL) { CHECK(row != NULL); return; }
    CHECK(row != NULL);
    CHECK(row->valid == 0);
    CHECK(row->stop_reason == AF_MEAS_SR_ERROR);
    CHECK(row->lost == 3);
    CHECK(row->npk == 3); // Только пакеты до момента ошибки

    /* повторная попытка того же измерения идёт в том же прогоне */
    CHECK(af_meas_find(&C, id)->cur_attempt == 2);
    CHECK(af_meas_find(&C, id)->state == AF_MEAS_ST_RUN);
}

static void t_outage_collapse(void)
{
    t_setup();
    af_meas_params_t p = t_params();
    p.n = 0;
    p.t_s = 10;
    p.retry_invalid = 1;
    p.lost_max = 2;

    uint32_t id = 0;
    CHECK(af_meas_start(&C, &p, T0, 0, 0, &id) == 0);

    af_meas_run_t *run = af_meas_find(&C, id);
    if (run == NULL) {
        t_teardown();
        return;
    }

    af_meas_feed(&C, T0 + 1000, 10, 0.1f);
    af_meas_feed(&C, T0 + 3000, 10, 0.1f);

    for (uint32_t t = 4000; t <= 200000; t += 1000) {
        af_meas_tick(&C, T0 + t, 0, 0);
    }

    CHECK(af_meas_res_count(run) == 2);

    const af_meas_res_t *r0 = t_row(id, 0);
    if (r0 == NULL) {
        t_teardown();
        return;
    }
    CHECK(r0->i == 1);
    CHECK(r0->attempt == 1);
    CHECK(r0->valid == 0);
    CHECK(r0->npk == 2);

    const af_meas_res_t *r1 = t_row(id, 1);
    if (r1 == NULL) {
        t_teardown();
        return;
    }
    CHECK(r1->i == 1);
    CHECK(r1->attempt == 2);
    CHECK(r1->valid == 0);
    CHECK(r1->npk == 0);
    CHECK(r1->stop_reason == AF_MEAS_SR_ERROR);

    CHECK(run->cur_attempt == 3);
    CHECK(run->done_i == 0);

    af_meas_feed(&C, T0 + 201000, 10, 0.1f);
    af_meas_feed(&C, T0 + 203000, 10, 0.1f);
    af_meas_feed(&C, T0 + 205000, 10, 0.1f);
    af_meas_feed(&C, T0 + 207000, 10, 0.1f);
    af_meas_tick(&C, T0 + 209500, 0, 0);

    CHECK(af_meas_res_count(run) == 3);

    const af_meas_res_t *r2 = t_row(id, 2);
    if (r2 == NULL) {
        t_teardown();
        return;
    }
    CHECK(r2->i == 1);
    CHECK(r2->attempt == 3);
    CHECK(r2->valid == 1);
    CHECK(r2->npk == 4);
    CHECK(r2->counts == 40);
    CHECK(r2->t0_mono_s == 1199);
    CHECK((r2->flags & AF_MEAS_F_FINAL) != 0);

    CHECK(run->done_i == 1);
    CHECK(run->cur_i == 2);

    t_teardown();
}
int main(void) {
    RUN2(t_poll);
    RUN2(t_note_missed_ok);
    RUN2(t_note_missed_over);
    RUN2(t_outage_collapse);
    TESTS_DONE();
    return 0;
}
