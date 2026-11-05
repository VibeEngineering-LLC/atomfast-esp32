#pragma once
#include <stdbool.h>
#include <stdint.h>

// Конфигурация загрузки
typedef struct {
    bool field_ap_fallback_enabled; // Переход в полевую точку доступа при потере Wi-Fi, ключ NVS "fap_en", нет ключа = ВЫКЛ
} boot_config_t;

// Загрузка конфигурации, отсутствующий ключ = значение по умолчанию
void boot_config_load(boot_config_t *out);

// Сохранение конфигурации, 0 успех, -1 ошибка
int boot_config_save(const boot_config_t *in);

// Счётчик загрузок платы (ключ "sess"), +1 за вызов, только из main-task; 0 = NVS недоступен
uint32_t boot_config_bump_session(uint32_t floor_value);

// Текущее значение счётчика, 0 если ключа нет
uint32_t boot_config_get_session(void);

// Номер этой загрузки, не меняется до перезагрузки
uint32_t boot_config_get_boot_session(void);
