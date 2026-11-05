#include "boot_config.h"
#include "boot_flag_plan.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include <inttypes.h>

static const char *TAG = "bootcfg";
#define BOOT_NS "boot"

static bool get_flag(nvs_handle_t h, const char *key)
{
    uint8_t v = 0;
    esp_err_t rc = nvs_get_u8(h, key, &v);
    return boot_flag_from_nvs(rc == ESP_OK, v);
}

void boot_config_load(boot_config_t *out)
{
    if (!out) return;
    out->field_ap_fallback_enabled = false;

    nvs_handle_t h;
    if (nvs_open(BOOT_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }

    out->field_ap_fallback_enabled = get_flag(h, "fap_en");

    nvs_close(h);
}

int boot_config_save(const boot_config_t *in)
{
    if (!in) return -1;

    nvs_handle_t h;
    if (nvs_open(BOOT_NS, NVS_READWRITE, &h) != ESP_OK) return -1;

    esp_err_t e = ESP_OK;
    e |= nvs_set_u8(h, "fap_en", in->field_ap_fallback_enabled ? 1 : 0);

    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);

    if (e != ESP_OK) {
        ESP_LOGW(TAG, "save failed (0x%x)", (int)e);
        return -1;
    }
    return 0;
}

static uint32_t s_boot_sess;

uint32_t boot_config_bump_session(uint32_t floor_value)
{
    nvs_handle_t h;
    if (nvs_open(BOOT_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "session bump: NVS unavailable");
        return 0;
    }

    uint32_t s = 0;
    nvs_get_u32(h, "sess", &s);

    if (s < floor_value) {
        ESP_LOGW(TAG, "session counter %" PRIu32 " < floor (%" PRIu32 ") — continuing from floor",
                 s, floor_value);
        s = floor_value;
    }

    s++;
    esp_err_t e = nvs_set_u32(h, "sess", s);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);

    if (e != ESP_OK) {
        ESP_LOGE(TAG, "session bump: NVS write failed (0x%x)", (int)e);
        return 0;
    }

    ESP_LOGI(TAG, "board session #%" PRIu32, s);
    if (s_boot_sess == 0) s_boot_sess = s;
    return s;
}

uint32_t boot_config_get_boot_session(void) { return s_boot_sess; }

uint32_t boot_config_get_session(void)
{
    nvs_handle_t h;
    if (nvs_open(BOOT_NS, NVS_READONLY, &h) != ESP_OK) return 0;
    uint32_t s = 0;
    nvs_get_u32(h, "sess", &s);
    nvs_close(h);
    return s;
}
