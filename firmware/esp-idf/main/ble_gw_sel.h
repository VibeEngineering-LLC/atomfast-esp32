/* main/ble_gw_sel.h — команды автомата выбора BLE-прибора T-86 (реализация в ble_gw.c) */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int ble_gw_sel_json(char *buf, size_t cap);   /* JSON состояния для GET /api/ble/cands; длина либо <0 */
int ble_gw_sel_select(uint32_t gen, uint8_t tok, bool confirm_clear, uint32_t *ring_count);
int ble_gw_sel_rescan(bool replace, uint32_t *gen_out);
int ble_gw_sel_forget(bool confirm);
int ble_gw_sel_cancel(void);
int ble_gw_sel_manual(const uint8_t addr[6], uint8_t atype, bool confirm_clear, uint32_t *ring_count);
