#pragma once
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
typedef uint32_t esp_ota_handle_t;
typedef struct { size_t size; } esp_partition_t;

#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_INVALID_STATE 0x103
#define OTA_SIZE_UNKNOWN ((size_t)-1)

esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t size, esp_ota_handle_t *handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
