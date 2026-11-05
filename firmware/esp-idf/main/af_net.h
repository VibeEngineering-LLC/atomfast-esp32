#pragma once
#include <stdint.h>
#include <stdbool.h>

#define WIFI_SSID_MAX 32   // длина SSID в NVS
#define WIFI_PASS_MAX 64   // длина пароля Wi-Fi в NVS
#include "esp_http_server.h"

// Режимы работы сети
typedef enum {
    NET_MODE_STA = 0,     // Клиент домашней сети
    NET_MODE_FIELD_AP,    // Полевая точка доступа
    NET_MODE_SETUP        // Captive-портал первичной настройки
} net_run_mode_t;

// Инициализация менеджера Wi-Fi
void wifi_manager_init(void);

// Проверка подключения
bool wifi_is_connected(void);

// Получение текущего режима
net_run_mode_t wifi_manager_mode(void);

// Проверка режима AP
bool wifi_manager_is_ap_mode(void);

// Количество клиентов AP
int wifi_manager_ap_clients(void);

// SSID точки доступа
const char *wifi_manager_ap_ssid(void);

// Проверка пароля по умолчанию
bool wifi_manager_ap_pass_is_default(void);

// Принудительный режим AP
bool wifi_manager_ap_forced(void);

// Попытка возврата в STA
void wifi_manager_try_return_to_sta(void);

// Причина блокировки возврата
const char *wifi_manager_return_block_reason(void);

// Инициализация веб-сервера
bool web_server_init(void);

// Проверка тишины HTTP-активности
bool web_server_http_activity_quiet(uint32_t threshold_ms);

// Время простоя HTTP
uint32_t web_server_http_idle_s(void);

// Фиксация активности запроса
void web_server_note_request_activity(const char *uri);

// Диагностика аллокаций памяти
void mem_diag_get(uint32_t *n, uint32_t *last_size, uint32_t *last_caps, uint32_t *cjson_spill);

// Инициализация BLE-клиента
void ble_gw_init(void);

// Готовность BLE-клиента
bool ble_gw_is_ready(void);

// e11: MAC дозиметра (сохранённая цель) строкой "aa:bb:cc:dd:ee:ff"; 0 ок, -1 нет цели (out[0] = 0)
int ble_gw_target_mac(char *out, size_t cap);

// Диагностика BLE-событий
void ble_gw_get_diag(uint32_t *gap_events, uint32_t *gattc_events);

// Проверка Basic Auth
bool af_auth_check(httpd_req_t *req);

// Инициализация аутентификации
void af_auth_init(void);

// Состояние BLE-клиента AtomFast (Э2): последняя запись и счётчики
typedef struct {
    bool connected;            // соединение установлено
    bool has_rec;              // принята хотя бы одна годная запись
    uint32_t reconnects;       // число попыток переподключения
    uint32_t rx_total;         // принято уведомлений
    uint32_t bad_total;        // отбракованных парсером
    uint32_t errors;           // ошибки GATT/открытия/тишины
    uint32_t last_rec_age_ms;  // возраст последнего уведомления
    uint8_t flags;
    float dose_msv;
    float doserate_usvh;
    uint16_t pulses_2s;
    float cps;
    int8_t battery;
    int8_t temp_c;
} ble_gw_status_t;

// Снимок состояния BLE-клиента
void ble_gw_get_status(ble_gw_status_t *out);

// e16g (T-56): очередь принятых годных пакетов для замеров (по порядку, ни один пакет пачки не теряется)
typedef struct { uint64_t rx_ms; uint16_t pulses; float rate_usvh; } ble_gw_pkt_t;
bool ble_gw_pop_packet(ble_gw_pkt_t *p);   // false - очередь пуста
void ble_gw_drop_packets(void);            // сбросить непрочитанное (перед стартом замера)

// e10: ручное переподключение (закрыть соединение либо запустить попытку сразу); false - BLE не готов
bool ble_gw_reconnect(void);
