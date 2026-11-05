#pragma once
/* e16-BLE: интерфейс подробной статистики BLE (реализация в ble_gw.c, чистая логика в af_ble_*.h) */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "af_ble_snap.h"

/* Снимок статистики; *now_ms - монотонное время снимка (мс с загрузки). false, если BLE ещё не готов. */
bool ble_gw_stats_snapshot(af_bs_snap_t *o, uint64_t *now_ms);
void ble_gw_set_ext(bool on);
/* Запись журнала сессий, idx 0 = самая свежая. false, если записи нет. */
bool ble_gw_stats_session(uint8_t idx, af_bs_sess_t *out);
/* Прибор: адрес с маской "A0:6C:**:**:**:F5" (пусто, если цели нет), тип адреса (0 public, 1 random, -1 н/д), имя из рекламы (пусто, если не видели). */
void ble_gw_stats_device(char *addr, size_t acap, int *addr_type, char *name, size_t ncap);
