#pragma once

#include <stdbool.h>
#include "esp_ota_ops.h"

typedef struct {
    esp_ota_handle_t handle;
    bool active;
} ota_session_t;

static inline esp_err_t ota_session_begin(ota_session_t *session,
                                         const esp_partition_t *partition,
                                         size_t image_size)
{
    if (session->active) return ESP_ERR_INVALID_STATE;
    session->handle = 0;
    esp_err_t err = esp_ota_begin(partition, image_size, &session->handle);
    session->active = err == ESP_OK;
    if (!session->active) session->handle = 0;
    return err;
}

static inline esp_err_t ota_session_write(ota_session_t *session, const void *data, size_t size)
{
    if (!session->active) return ESP_ERR_INVALID_STATE;
    return esp_ota_write(session->handle, data, size);
}

static inline esp_err_t ota_session_abort(ota_session_t *session)
{
    if (!session->active) return ESP_OK;
    esp_ota_handle_t handle = session->handle;
    session->handle = 0;
    session->active = false;
    return esp_ota_abort(handle);
}

static inline esp_err_t ota_session_end(ota_session_t *session)
{
    if (!session->active) return ESP_ERR_INVALID_STATE;
    esp_ota_handle_t handle = session->handle;
    /* ESP-IDF consumes the handle even when image validation fails. */
    session->handle = 0;
    session->active = false;
    return esp_ota_end(handle);
}
