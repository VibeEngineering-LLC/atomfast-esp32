#pragma once
#include <stdint.h>
#include "af_meas.h"

/* Инициализация драйвера измерений, вызывается один раз из web_server_init до регистрации маршрутов */
void af_meas_drv_init(void);

/* Запуск нового измерения, возвращает 0 или код ошибки AF_MEAS_E_*, id получает новый идентификатор */
int  af_meas_drv_start(const af_meas_params_t *p, uint32_t *id);

/* Остановка измерения по id, id 0 означает активное измерение */
int  af_meas_drv_stop(uint32_t id);

/* Пауза измерения по id */
int  af_meas_drv_pause(uint32_t id);

/* Возобновление измерения по id */
int  af_meas_drv_resume(uint32_t id);

/* Удаление измерения по id */
int  af_meas_drv_delete(uint32_t id);

/* Получение снимка состояния: id 0 = активное, иначе самое свежее; 0 успех, -1 ошибка; run->res не разыменовывать */
int  af_meas_drv_snapshot(uint32_t id, af_meas_run_t *run, af_meas_live_t *live, int *live_ok, af_meas_sum_t *sum);

/* Получение копии записи с индексом idx (0 = самая старая), 0 успех, -1 ошибка */
int  af_meas_drv_row(uint32_t id, uint32_t idx, af_meas_res_t *out);

/* Получение списка id прогонов, новые первыми, возвращает количество записанных id */
uint32_t af_meas_drv_ids(uint32_t *ids, uint32_t cap);

/* Проверка синхронизации времени, 1 если время синхронизировано (utc заполнен), иначе 0 */
int  af_meas_drv_time(uint32_t *utc);

/* e16f (T-38): положить в результаты готовый замер из журнала (одна строка), 0 или код AF_MEAS_E_* */
int  af_meas_drv_add_journal(const char *name, const af_meas_res_t *res, uint32_t from, uint32_t *id);

/* Возвращает id активного измерения, 0 если активного нет */
uint32_t af_meas_drv_active_id(void);
