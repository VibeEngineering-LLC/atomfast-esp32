#pragma once
/* e12: публикация в MQTT с автообнаружением Home Assistant. Настройки в NVS (namespace "mqtt"), по умолчанию ВЫКЛ. Брокер недоступен - задача живёт дальше, BLE и HTTP не блокируются. */
#include <stdint.h>
#include "af_mqtt_cfg.h"

/* Старт: прочитать настройки из NVS (порча - значения по умолчанию) и запустить задачу. Безопасен при ошибках. */
void af_mqtt_drv_start(void);
/* Применить и сохранить настройки (af_mqtt_cfg_valid должна быть 1). 0 принято; -1 неверные значения; -2 NVS не записан (действует до перезагрузки). */
int af_mqtt_drv_set_cfg(const af_mqtt_cfg_t *c);
/* Текущие настройки (копия). */
void af_mqtt_drv_get_cfg(af_mqtt_cfg_t *c);
/* Счётчики и состояние для /api/status. */
void af_mqtt_drv_get_stat(af_mqtt_stat_t *s);
