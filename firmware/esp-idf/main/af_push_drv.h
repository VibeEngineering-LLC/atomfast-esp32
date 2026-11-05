#pragma once
/* Э8: отправка истории на приёмник по HTTP. Настройки и курсоры в NVS (namespace "afpush", ключи push_*). По умолчанию ВЫКЛ. */
#include <stddef.h>
#include "af_push.h"

/* Старт: прочитать настройки и курсоры из NVS (порча - значения по умолчанию), запустить задачу, если включено. Безопасен при ошибках. */
void af_push_start(void);
/* Текущие настройки (копия, с токеном - только для разбора POST, наружу не отдавать). */
void af_push_get_cfg(af_push_cfg_t *out);
/* Применить и сохранить настройки (cmd от af_push_parse). 0 принято; -1 не готов; -2 NVS не записан (действует до перезагрузки). */
int af_push_apply(const af_push_cmd_t *cmd);
/* JSON для GET /api/push (без токена). Длина или -1. */
int af_push_get_json(char *buf, size_t cap);
/* Read-only sender lifecycle/timing diagnostics; no configuration or credentials. */
int af_push_get_diag(char *buf, size_t cap);
/* Пробный запрос (асинхронно, результат в GET /api/push, поле test). 0 запущен; -1 уже идёт; -2 нет адреса или токена; -3 не готов. */
int af_push_test_start(void);
/* Сбросить курсоры и ошибки колец («отправлять с начала»). 0 ок; -1 не готов. */
int af_push_reset(void);
