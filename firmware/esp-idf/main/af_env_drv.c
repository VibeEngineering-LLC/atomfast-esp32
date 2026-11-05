#include "af_env_drv.h"
#include "af_log_api.h"
#include "af_board.h"
#include "af_store.h"
#include "af_net.h"
#include "af_sensor.h"
#include "af_sns_drv.h"
#include "af_sns_reg.h"
#include "af_sns_json.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <inttypes.h>

static const char *TAG = "af_env";

typedef struct {
    uint8_t state;
    uint8_t cons_err;
    uint8_t ready;
    uint8_t chip;
    int8_t rec_rc;
    i2c_master_dev_handle_t dev;
    union {
        af_bmp280_cal_t bmp;
        af_bme280_cal_t bme;
    } cal;
    float t_show, p_show, h_show;
    float t_raw, p_raw_mmhg;
    uint32_t n_ok, n_err, store_ok, store_fail, store_off, last_ok_ts;
    int64_t last_ok_us;
    uint32_t next_meas_s, next_try_s;
    const char *err;
} rt_t;

static SemaphoreHandle_t s_mtx;
static af_sns_reg_t s_reg;
static rt_t s_rt[AF_SNS_MAX];
static i2c_master_bus_handle_t s_bus;
static TaskHandle_t s_task;
static int s_nvs_ok;
static uint8_t s_scan[16];
static int s_scan_n;
static int s_scan_done;
static af_liveavg_t s_avg;
static int64_t s_avg_us;
static int s_bus_ok;
static volatile int s_scan_req;
static int s_scan_found;
static int s_scan_changed;
static uint8_t s_reset_mask;
static int s_legacy_pending;

static void fnum(char *dst, size_t n, float v, int dec) {
    if (isnan(v)) {
        if (n > 4) {
            memcpy(dst, "null", 4);
            dst[4] = '\0';
        } else if (n > 0) {
            dst[0] = '\0';
        }
    } else {
        snprintf(dst, n, "%.*f", dec, v);
    }
}

static void reg_load(void) {
    nvs_handle_t h;
    if (nvs_open("envs", NVS_READONLY, &h) != ESP_OK) {
        s_nvs_ok = 0;
        af_sns_reg_empty(&s_reg);
        s_legacy_pending = 1;
        return;
    }

    uint8_t blob[AF_SNS_BLOB_SIZE];
    size_t len = sizeof(blob);
    if (nvs_get_blob(h, "sreg", blob, &len) == ESP_OK && len == AF_SNS_BLOB_SIZE) {
        if (af_sns_reg_unpack(blob, len, &s_reg) == 0) {
            s_nvs_ok = 1;
            s_legacy_pending = 0;
            nvs_close(h);
            return;
        }
    }

    af_env_cfg_t cfg;
    af_env_cfg_default(&cfg);
    uint8_t en = cfg.enabled;
    int16_t t10 = cfg.t_off10;
    int16_t p10 = cfg.p_off10;
    uint8_t agg = cfg.agg_s;
    uint16_t per = cfg.period_s;
    nvs_get_u8(h, "en", &en);
    nvs_get_i16(h, "t10", &t10);
    nvs_get_i16(h, "p10", &p10);
    nvs_get_u8(h, "agg", &agg);
    nvs_get_u16(h, "per", &per);
    nvs_close(h);

    cfg.enabled = en;
    cfg.t_off10 = t10;
    cfg.p_off10 = p10;
    cfg.agg_s = agg;
    cfg.period_s = per;
    af_env_cfg_clamp(&cfg);
    af_sns_reg_from_legacy(&s_reg, &cfg);
    s_nvs_ok = 1;
    s_legacy_pending = 1;
}

static int reg_save(const af_sns_reg_t *reg) {
    nvs_handle_t h;
    if (nvs_open("envs", NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }

    uint8_t blob[AF_SNS_BLOB_SIZE];
    size_t len = af_sns_reg_pack(reg, blob);
    esp_err_t rc = nvs_set_blob(h, "sreg", blob, len);

    uint8_t primary = 0;
    if (reg->s[0].sensor != 0) {
        primary = 1;
    } else {
        for (int i = 1; i < AF_SNS_MAX; i++) {
            if (reg->s[i].sensor != 0) {
                primary = i + 1;
                break;
            }
        }
    }

    if (primary != 0) {
        const af_sns_cfg_t *c = &reg->s[primary - 1];
        rc = rc == ESP_OK ? nvs_set_u8(h, "en", c->enabled) : rc;
        rc = rc == ESP_OK ? nvs_set_i16(h, "t10", c->t_off10) : rc;
        rc = rc == ESP_OK ? nvs_set_i16(h, "p10", c->p_off10) : rc;
        rc = rc == ESP_OK ? nvs_set_u16(h, "per", c->period_s) : rc;
        rc = rc == ESP_OK ? nvs_set_u8(h, "agg", reg->agg_s) : rc;
    }

    rc = rc == ESP_OK ? nvs_commit(h) : rc;
    nvs_close(h);
    return (rc == ESP_OK) ? 0 : -1;
}

static i2c_master_dev_handle_t dev_for_addr(uint8_t addr) {
    for (int i = 0; i < AF_SNS_MAX; i++) {
        if (s_reg.s[i].sensor != 0 && s_reg.s[i].addr == addr && s_rt[i].ready && s_rt[i].dev != NULL) {
            return s_rt[i].dev;
        }
    }
    return NULL;
}

static int i2c_xfer_dev(i2c_master_dev_handle_t dev, const uint8_t *w, size_t wn, uint8_t *r, size_t rn) {
    if (wn > 0) {
        if (i2c_master_transmit(dev, w, wn, 100) != ESP_OK) {
            return -1;
        }
    }
    if (rn > 0) {
        if (i2c_master_receive(dev, r, rn, 100) != ESP_OK) {
            return -1;
        }
    }
    return 0;
}

static int ops_probe(void *ctx, uint8_t addr) {
    (void)ctx;
    return (i2c_master_probe(s_bus, addr, 30) == ESP_OK) ? 0 : 1;
}

static int ops_xfer(void *ctx, uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn) {
    (void)ctx;
    i2c_master_dev_handle_t dev = dev_for_addr(addr);
    int rc;
    if (dev == NULL) {
        i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = AF_BOARD_I2C_HZ
        };
        if (i2c_master_bus_add_device(s_bus, &dc, &dev) != ESP_OK) {
            return -1;
        }
        rc = i2c_xfer_dev(dev, w, wn, r, rn);
        i2c_master_bus_rm_device(dev);
    } else {
        rc = i2c_xfer_dev(dev, w, wn, r, rn);
    }
    return rc;
}

static void measure_fail(int i, const char *msg) {
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_rt[i].n_err++;
    s_rt[i].cons_err++;
    s_rt[i].state = 2;
    s_rt[i].err = msg;
    uint8_t cons = s_rt[i].cons_err;
    xSemaphoreGive(s_mtx);
    ESP_LOGW(TAG, "sensor %d measure failed: %s (consecutive %d)", i + 1, msg, cons);
    af_log_event(2, "sensor", "s%d measure failed: %s", i + 1, msg);
    if (cons >= 3) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_rt[i].t_show = NAN;
        s_rt[i].p_show = NAN;
        s_rt[i].h_show = NAN;
        s_rt[i].t_raw = NAN;
        s_rt[i].p_raw_mmhg = NAN;
        xSemaphoreGive(s_mtx);
        if (s_rt[i].dev != NULL) {
            i2c_master_bus_rm_device(s_rt[i].dev);
            s_rt[i].dev = NULL;
        }
        s_rt[i].ready = 0;
        uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000);
        s_rt[i].next_try_s = now_s + 10;
    }
}

static void sns_init(int i, const af_sns_cfg_t *cfg) {
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = cfg->addr,
        .scl_speed_hz = AF_BOARD_I2C_HZ
    };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(s_bus, &dc, &dev) != ESP_OK) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_rt[i].state = 2;
        s_rt[i].err = "add";
        s_rt[i].n_err++;
        s_rt[i].t_show = NAN;
        s_rt[i].p_show = NAN;
        s_rt[i].h_show = NAN;
        xSemaphoreGive(s_mtx);
        ESP_LOGW(TAG, "sensor %d: failed to add device", i + 1);
        af_log_event(3, "sensor", "s%d add device failed", i + 1);
        return;
    }

    int ok = 1;
    const char *err_msg = "";

    if (cfg->model == AF_SNS_BMP280) {
        uint8_t id = 0;
        if (i2c_xfer_dev(dev, (const uint8_t[]){AF_BMP280_REG_ID}, 1, &id, 1) != 0) {
            ok = 0; err_msg = "id";
        } else if (id != 0x56 && id != 0x57 && id != 0x58) {
            ok = 0; err_msg = "id";
        } else {
            s_rt[i].chip = id;
            uint8_t rst[] = {0xE0, 0xB6};
            if (i2c_xfer_dev(dev, rst, 2, NULL, 0) != 0) {
                ok = 0; err_msg = "rst";
            } else {
                vTaskDelay(pdMS_TO_TICKS(30));
                uint8_t cal_buf[24];
                if (i2c_xfer_dev(dev, (const uint8_t[]){AF_BMP280_REG_CAL}, 1, cal_buf, 24) != 0) {
                    ok = 0; err_msg = "cal";
                } else if (af_bmp280_parse_cal(cal_buf, 24, &s_rt[i].cal.bmp) != 0) {
                    ok = 0; err_msg = "cal";
                } else {
                    uint8_t cfg_reg[] = {AF_BMP280_REG_CONFIG, 0x00};
                    if (i2c_xfer_dev(dev, cfg_reg, 2, NULL, 0) != 0) {
                        ok = 0; err_msg = "cfg";
                    }
                }
            }
        }
    } else if (cfg->model == AF_SNS_BME280) {
        uint8_t id = 0;
        if (i2c_xfer_dev(dev, (const uint8_t[]){AF_BMP280_REG_ID}, 1, &id, 1) != 0) {
            ok = 0; err_msg = "id";
        } else if (id != 0x60) {
            ok = 0; err_msg = "id";
        } else {
            s_rt[i].chip = id;
            uint8_t rst[] = {0xE0, 0xB6};
            if (i2c_xfer_dev(dev, rst, 2, NULL, 0) != 0) {
                ok = 0; err_msg = "rst";
            } else {
                vTaskDelay(pdMS_TO_TICKS(30));
                uint8_t a[26], b[7];
                if (i2c_xfer_dev(dev, (const uint8_t[]){AF_BMP280_REG_CAL}, 1, a, 26) != 0) {
                    ok = 0; err_msg = "cal";
                } else if (i2c_xfer_dev(dev, (const uint8_t[]){0xE1}, 1, b, 7) != 0) {
                    ok = 0; err_msg = "cal";
                } else if (af_bme280_parse_cal(a, b, &s_rt[i].cal.bme) != 0) {
                    ok = 0; err_msg = "cal";
                } else {
                    uint8_t cfg_reg[] = {AF_BMP280_REG_CONFIG, 0x00};
                    if (i2c_xfer_dev(dev, cfg_reg, 2, NULL, 0) != 0) {
                        ok = 0; err_msg = "cfg";
                    }
                }
            }
        }
    } else if (cfg->model == AF_SNS_SHT3X) {
        if (i2c_xfer_dev(dev, (const uint8_t[]){0xF3, 0x2D}, 2, NULL, 0) != 0) {
            ok = 0; err_msg = "status";
        } else {
            uint8_t st[3];
            if (i2c_xfer_dev(dev, NULL, 0, st, 3) != 0) {
                ok = 0; err_msg = "status";
            } else if (!af_sht3x_status_ok(st)) {
                ok = 0; err_msg = "status";
            } else {
                s_rt[i].chip = 0;
            }
        }
    } else if (cfg->model == AF_SNS_AHT2X) {
        if (i2c_xfer_dev(dev, (const uint8_t[]){0x71}, 1, NULL, 0) != 0) {
            ok = 0; err_msg = "status";
        } else {
            uint8_t st = 0;
            if (i2c_xfer_dev(dev, NULL, 0, &st, 1) != 0) {
                ok = 0; err_msg = "status";
            } else {
                if ((st & 0x08) == 0) {
                    uint8_t init[] = {0xBE, 0x08, 0x00};
                    if (i2c_xfer_dev(dev, init, 3, NULL, 0) != 0) {
                        ok = 0; err_msg = "status";
                    } else {
                        vTaskDelay(pdMS_TO_TICKS(15));
                    }
                }
                if (ok) s_rt[i].chip = st;
            }
        }
    } else {
        ok = 0; err_msg = "init";
    }

    if (!ok) {
        i2c_master_bus_rm_device(dev);
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_rt[i].dev = NULL;
        s_rt[i].ready = 0;
        s_rt[i].state = 2;
        s_rt[i].err = err_msg;
        s_rt[i].n_err++;
        s_rt[i].t_show = NAN;
        s_rt[i].p_show = NAN;
        s_rt[i].h_show = NAN;
        xSemaphoreGive(s_mtx);
        ESP_LOGW(TAG, "sensor %d init failed: %s", i + 1, err_msg);
        af_log_event(2, "sensor", "s%d init failed: %s", i + 1, err_msg);
    } else {
        s_rt[i].dev = dev;
        s_rt[i].ready = 1;
        s_rt[i].cons_err = 0;
        s_rt[i].next_meas_s = 0;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        if (s_rt[i].state == 0) {
            s_rt[i].err = "init";
        }
        xSemaphoreGive(s_mtx);
        ESP_LOGI(TAG, "sensor %d init ok (model %d, addr 0x%02X)", i + 1, cfg->model, cfg->addr);
    }
}

static void sns_measure(int i, const af_sns_cfg_t *cfg) {
    float t = NAN, p = NAN, h = NAN;
    int ok = 1;
    const char *err_msg = "";

    if (cfg->model == AF_SNS_BMP280) {
        uint8_t ctrl = af_bmp280_ctrl(AF_ENV_OSRS_T, AF_ENV_OSRS_P, AF_BMP280_MODE_FORCED);
        uint8_t ctrl_reg[] = {AF_BMP280_REG_CTRL, ctrl};
        if (i2c_xfer_dev(s_rt[i].dev, ctrl_reg, 2, NULL, 0) != 0) {
            ok = 0; err_msg = "read";
        } else {
            vTaskDelay(pdMS_TO_TICKS(af_bmp280_meas_ms(AF_ENV_OSRS_T, AF_ENV_OSRS_P) + 5));
            for (int k = 0; k < 10; k++) {
                uint8_t status = 0;
                if (i2c_xfer_dev(s_rt[i].dev, (const uint8_t[]){AF_BMP280_REG_STATUS}, 1, &status, 1) != 0) {
                    ok = 0; err_msg = "read";
                    break;
                }
                if ((status & 0x08) == 0) break;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (ok) {
                uint8_t raw[6];
                if (i2c_xfer_dev(s_rt[i].dev, (const uint8_t[]){AF_BMP280_REG_DATA}, 1, raw, 6) != 0) {
                    ok = 0; err_msg = "read";
                } else if (af_env_bmp280_convert(&s_rt[i].cal.bmp, raw, &t, &p) != 0) {
                    ok = 0; err_msg = "convert";
                }
            }
        }
    } else if (cfg->model == AF_SNS_BME280) {
        uint8_t ctrl = af_bmp280_ctrl(AF_ENV_OSRS_T, AF_ENV_OSRS_P, AF_BMP280_MODE_FORCED);
        uint8_t hum_reg[] = {0xF2, 0x01};
        uint8_t ctrl_reg[] = {AF_BMP280_REG_CTRL, ctrl};
        if (i2c_xfer_dev(s_rt[i].dev, hum_reg, 2, NULL, 0) != 0) {
            ok = 0; err_msg = "read";
        } else if (i2c_xfer_dev(s_rt[i].dev, ctrl_reg, 2, NULL, 0) != 0) {
            ok = 0; err_msg = "read";
        } else {
            vTaskDelay(pdMS_TO_TICKS(af_bmp280_meas_ms(AF_ENV_OSRS_T, AF_ENV_OSRS_P) + 5 + 3));
            for (int k = 0; k < 10; k++) {
                uint8_t status = 0;
                if (i2c_xfer_dev(s_rt[i].dev, (const uint8_t[]){AF_BMP280_REG_STATUS}, 1, &status, 1) != 0) {
                    ok = 0; err_msg = "read";
                    break;
                }
                if ((status & 0x08) == 0) break;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (ok) {
                uint8_t raw[8];
                if (i2c_xfer_dev(s_rt[i].dev, (const uint8_t[]){AF_BMP280_REG_DATA}, 1, raw, 8) != 0) {
                    ok = 0; err_msg = "read";
                } else if (af_bme280_convert(&s_rt[i].cal.bme, raw, &t, &p, &h) != 0) {
                    ok = 0; err_msg = "convert";
                }
            }
        }
    } else if (cfg->model == AF_SNS_SHT3X) {
        uint8_t cmd[] = {0x24, 0x00};
        if (i2c_xfer_dev(s_rt[i].dev, cmd, 2, NULL, 0) != 0) {
            ok = 0; err_msg = "read";
        } else {
            vTaskDelay(pdMS_TO_TICKS(16));
            uint8_t raw[6];
            if (i2c_xfer_dev(s_rt[i].dev, NULL, 0, raw, 6) != 0) {
                ok = 0; err_msg = "read";
            } else if (af_sht3x_parse(raw, &t, &h) != 0) {
                ok = 0; err_msg = "convert";
            }
        }
    } else if (cfg->model == AF_SNS_AHT2X) {
        uint8_t cmd[] = {0xAC, 0x33, 0x00};
        if (i2c_xfer_dev(s_rt[i].dev, cmd, 3, NULL, 0) != 0) {
            ok = 0; err_msg = "read";
        } else {
            vTaskDelay(pdMS_TO_TICKS(80));
            uint8_t raw[7];
            if (i2c_xfer_dev(s_rt[i].dev, NULL, 0, raw, 7) != 0) {
                ok = 0; err_msg = "read";
            } else {
                int prc = af_aht2x_parse(raw, &t, &h);
                if (prc == -2) {
                    for (int k = 0; k < 2; k++) {
                        vTaskDelay(pdMS_TO_TICKS(20));
                        if (i2c_xfer_dev(s_rt[i].dev, NULL, 0, raw, 7) != 0) {
                            ok = 0; err_msg = "read";
                            break;
                        }
                        prc = af_aht2x_parse(raw, &t, &h);
                        if (prc != -2) break;
                    }
                    if (ok && prc == -2) {
                        ok = 0; err_msg = "busy";
                    }
                } else if (prc == -3) {
                    ok = 0; err_msg = "status";
                } else if (prc == -4) {
                    ok = 0; err_msg = "crc";
                } else if (prc != 0) {
                    ok = 0; err_msg = "convert";
                }
            }
        }
    } else {
        ok = 0; err_msg = "init";
    }

    if (!ok) {
        measure_fail(i, err_msg);
        return;
    }

    time_t ts = time(NULL);
    if (ts < 1700000000) ts = 0;

    af_env_in_t in = {
        .sensor = cfg->sensor,
        .model = cfg->model,
        .enabled = 1,
        .t_off10 = cfg->t_off10,
        .p_off10 = cfg->p_off10,
        .h_off10 = cfg->h_off10,
        .caps = af_sns_preset(cfg->model)->caps,
        .t_c = t,
        .p_pa = p,
        .h_pct = h,
        .ts = (uint32_t)ts
    };

    af_env_rec_t rec;
    float t_show = NAN, p_show = NAN, h_show = NAN;
    int rc = af_env_make_rec_ex(&in, &rec, &t_show, &p_show, &h_show);

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_rt[i].rec_rc = rc;
    s_rt[i].t_raw = t;
    s_rt[i].p_raw_mmhg = af_pa_to_mmhg(p);
    s_rt[i].t_show = t_show;
    s_rt[i].p_show = p_show;
    s_rt[i].h_show = h_show;
    if (rc == 0 || rc == -3) {
        s_rt[i].n_ok++;
        s_rt[i].last_ok_us = esp_timer_get_time();
        s_rt[i].last_ok_ts = (uint32_t)ts;
        s_rt[i].state = 1;
        s_rt[i].err = "";
        s_rt[i].cons_err = 0;
    } else if (rc == -2) {
        s_rt[i].err = "implausible";
        s_rt[i].n_err++;
        s_rt[i].state = 2;
    }
    xSemaphoreGive(s_mtx);

    if (rc == 0) {
        int sr = af_store_submit_env(&rec);   /* e16i П.2: 0 принято, 1 вид выключен пользователем, иначе сбой */
        if (sr == 0) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_rt[i].store_ok++;
            xSemaphoreGive(s_mtx);
        } else {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            if (sr == 1) s_rt[i].store_off++; else s_rt[i].store_fail++;
            xSemaphoreGive(s_mtx);
        }
    }
}

static bool env_try_start_bus(void) {
    i2c_master_bus_config_t bc = {
        .i2c_port = AF_BOARD_I2C_PORT,
        .sda_io_num = AF_BOARD_I2C_SDA,
        .scl_io_num = AF_BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };

    if (i2c_new_master_bus(&bc, &s_bus) != ESP_OK) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_bus_ok = 0;
        xSemaphoreGive(s_mtx);
        ESP_LOGE(TAG, "failed to create I2C bus");
        af_log_event(3, "i2c", "bus create failed");
        return false;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_bus_ok = 1;
    xSemaphoreGive(s_mtx);

    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (i2c_master_probe(s_bus, a, 20) == ESP_OK) {
            ESP_LOGI(TAG, "I2C scan: device at 0x%02X", a);
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            if (s_scan_n < 16) {
                s_scan[s_scan_n++] = a;
            }
            xSemaphoreGive(s_mtx);
        }
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_scan_done = 1;
    xSemaphoreGive(s_mtx);
    ESP_LOGI(TAG, "I2C scan done: %d device(s)", s_scan_n);
    af_log_event(1, "i2c", "scan done: %d device(s)", s_scan_n);

    if (s_legacy_pending) {
        af_i2c_ops_t ops = {
            .ctx = NULL,
            .probe = ops_probe,
            .xfer = ops_xfer
        };
        af_sns_found_t found[8];
        int n = af_sns_scan(&ops, found, 8);
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        int adopted = af_sns_reg_adopt_addr(&s_reg, found, n);
        af_sns_reg_t reg_copy = s_reg;
        xSemaphoreGive(s_mtx);
        if (adopted || s_legacy_pending) {
            int rc = reg_save(&reg_copy);
            s_nvs_ok = (rc == 0);
            s_legacy_pending = 0;
        }
    }

    return true;
}

static void env_task(void *arg) {
    (void)arg;
    uint32_t last_rx = UINT32_MAX;
    bool bus_started = false;
    int64_t bus_retry_at_us = 0;
    TickType_t wait_ticks = 0;

    while (1) {
        ulTaskNotifyTake(pdTRUE, wait_ticks);
        wait_ticks = pdMS_TO_TICKS(1000);
        uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000);

        ble_gw_status_t b;
        ble_gw_get_status(&b);
        if (b.has_rec && b.rx_total != last_rx) {
            last_rx = b.rx_total;
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            af_liveavg_add(&s_avg, b.cps, b.doserate_usvh);
            xSemaphoreGive(s_mtx);
        }

        xSemaphoreTake(s_mtx, portMAX_DELAY);
        uint8_t agg_s = s_reg.agg_s;
        int tick_rc = af_liveavg_tick(&s_avg, now_s, agg_s);
        if (tick_rc == 1) {
            s_avg_us = esp_timer_get_time();
        }
        xSemaphoreGive(s_mtx);

        if (!bus_started) {
            if (esp_timer_get_time() < bus_retry_at_us) continue;
            bus_started = env_try_start_bus();
            if (!bus_started) {
                bus_retry_at_us = esp_timer_get_time() + 1000000;
                continue;
            }
        }

        if (s_scan_req == 1) {
            s_scan_req = 2;
            af_i2c_ops_t ops = {
                .ctx = NULL,
                .probe = ops_probe,
                .xfer = ops_xfer
            };
            af_sns_found_t found[8];
            int n = af_sns_scan(&ops, found, 8);
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            int changed = af_sns_reg_merge_scan(&s_reg, found, n);
            af_sns_reg_t reg_copy = s_reg;
            xSemaphoreGive(s_mtx);
            if (changed > 0) {
                int rc = reg_save(&reg_copy);
                s_nvs_ok = (rc == 0);
            }
            s_scan_found = n;
            s_scan_changed = changed;
            af_log_event(1, "i2c", "rescan: found=%d changed=%d", n, changed);
            s_scan_req = 3;
        }

        xSemaphoreTake(s_mtx, portMAX_DELAY);
        uint8_t reset_mask = s_reset_mask;
        s_reset_mask = 0;
        xSemaphoreGive(s_mtx);

        if (reset_mask != 0) {
            for (int i = 0; i < AF_SNS_MAX; i++) {
                if (reset_mask & (1 << i)) {
                    if (s_rt[i].dev != NULL) {
                        i2c_master_bus_rm_device(s_rt[i].dev);
                    }
                    /* C-05: json-читатель берёт s_rt под s_mtx — обнуляем там же */
                    xSemaphoreTake(s_mtx, portMAX_DELAY);
                    memset(&s_rt[i], 0, sizeof(rt_t));
                    s_rt[i].err = "init";
                    xSemaphoreGive(s_mtx);
                }
            }
        }

        for (int i = 0; i < AF_SNS_MAX; i++) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            af_sns_cfg_t cfg = s_reg.s[i];
            xSemaphoreGive(s_mtx);

            if (cfg.sensor == 0) continue;

            if (cfg.enabled == 0) {
                if (s_rt[i].ready) {
                    if (s_rt[i].dev != NULL) {
                        i2c_master_bus_rm_device(s_rt[i].dev);
                        s_rt[i].dev = NULL;
                    }
                    s_rt[i].ready = 0;
                    xSemaphoreTake(s_mtx, portMAX_DELAY);
                    s_rt[i].t_show = NAN;
                    s_rt[i].p_show = NAN;
                    s_rt[i].h_show = NAN;
                    xSemaphoreGive(s_mtx);
                }
                continue;
            }

            if (!s_rt[i].ready) {
                if (now_s >= s_rt[i].next_try_s) {
                    sns_init(i, &cfg);
                    if (!s_rt[i].ready) {
                        s_rt[i].next_try_s = now_s + 60;
                    }
                }
                continue;
            }

            if (now_s >= s_rt[i].next_meas_s) {
                sns_measure(i, &cfg);
                s_rt[i].next_meas_s = now_s + cfg.period_s;
            }
        }
    }
}

void af_env_drv_start(void) {
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        ESP_LOGE(TAG, "failed to create mutex");
        return;
    }
    af_liveavg_init(&s_avg);
    reg_load();
    if (xTaskCreate(env_task, "af_env", 4096, NULL, 3, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "failed to create env task");
    }
}

void af_env_drv_get_reg(af_sns_reg_t *r) {
    if (r == NULL) return;
    if (s_mtx == NULL) {
        af_sns_reg_empty(r);
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *r = s_reg;
    xSemaphoreGive(s_mtx);
}

uint8_t af_env_drv_primary(void) {
    if (s_mtx == NULL) return 0;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint8_t p = 0;
    if (s_reg.s[0].sensor != 0) {
        p = 1;
    } else {
        uint8_t order[8];
        int n = af_sns_reg_order(&s_reg, order);
        if (n > 0) p = order[0];
    }
    xSemaphoreGive(s_mtx);
    return p;
}

int af_env_drv_patch(uint8_t sensor, const af_sns_patch_t *p, int *persisted) {
    if (s_mtx == NULL) return -3;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int rc = af_sns_reg_patch(&s_reg, sensor, p);
    if (rc == 0) {
        af_sns_reg_t copy = s_reg;
        xSemaphoreGive(s_mtx);
        int save_rc = reg_save(&copy);
        s_nvs_ok = (save_rc == 0);
        if (persisted) *persisted = (save_rc == 0);
    } else {
        xSemaphoreGive(s_mtx);
        if (persisted) *persisted = 0;
    }
    return rc;
}

int af_env_drv_set_agg(int32_t agg_s, int *persisted) {
    if (s_mtx == NULL) return -3;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int rc = af_sns_reg_set_agg(&s_reg, agg_s);
    if (rc == 0) {
        af_sns_reg_t copy = s_reg;
        xSemaphoreGive(s_mtx);
        int save_rc = reg_save(&copy);
        s_nvs_ok = (save_rc == 0);
        if (persisted) *persisted = (save_rc == 0);
    } else {
        xSemaphoreGive(s_mtx);
        if (persisted) *persisted = 0;
    }
    return rc;
}

int af_env_drv_enable_model(uint8_t model, uint8_t enabled, int *persisted) {
    if (s_mtx == NULL) return -3;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int rc = af_sns_reg_enable_model(&s_reg, model, enabled);
    if (rc >= 0) {
        af_sns_reg_t copy = s_reg;
        xSemaphoreGive(s_mtx);
        int save_rc = reg_save(&copy);
        s_nvs_ok = (save_rc == 0);
        if (persisted) *persisted = (save_rc == 0);
    } else {
        xSemaphoreGive(s_mtx);
        if (persisted) *persisted = 0;
    }
    return rc;
}

int af_env_drv_add(uint8_t model, uint8_t addr, uint8_t enabled, int *persisted) {
    if (s_mtx == NULL) return -4;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int rc = af_sns_reg_add(&s_reg, model, addr, enabled);
    if (rc >= 1) {
        s_reset_mask |= (1 << (rc - 1));
        af_sns_reg_t copy = s_reg;
        xSemaphoreGive(s_mtx);
        int save_rc = reg_save(&copy);
        s_nvs_ok = (save_rc == 0);
        if (persisted) *persisted = (save_rc == 0);
    } else {
        xSemaphoreGive(s_mtx);
        if (persisted) *persisted = 0;
    }
    return rc;
}

int af_env_drv_remove(uint8_t sensor, int *persisted) {
    if (s_mtx == NULL) return -3;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int rc = af_sns_reg_remove(&s_reg, sensor);
    if (rc == 0) {
        s_reset_mask |= (1 << (sensor - 1));
        af_sns_reg_t copy = s_reg;
        xSemaphoreGive(s_mtx);
        int save_rc = reg_save(&copy);
        s_nvs_ok = (save_rc == 0);
        if (persisted) *persisted = (save_rc == 0);
    } else {
        xSemaphoreGive(s_mtx);
        if (persisted) *persisted = 0;
    }
    return rc;
}

int af_env_drv_scan(int *found, int *changed) {
    if (s_mtx == NULL) return -3;
    if (s_scan_req != 0 && s_scan_req != 3) return -1;
    s_scan_req = 1;
    xTaskNotifyGive(s_task);
    for (int i = 0; i < 150; i++) {
        if (s_scan_req == 3) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_scan_req != 3) return -1;
    if (found) *found = s_scan_found;
    if (changed) *changed = s_scan_changed;
    s_scan_req = 0;
    return 0;
}

int af_env_drv_json(char *buf, size_t cap) {
    if (s_mtx == NULL) return -1;

    uint8_t primary = 0;
    af_sns_cfg_t pcfg;
    rt_t prt;
    af_sns_reg_t reg_copy;
    rt_t rt_copy[AF_SNS_MAX];
    uint8_t scan[16];
    int scan_n, scan_done;
    af_liveavg_t avg;
    int64_t avg_us;
    int nvs_ok;
    int bus_ok;
    int scan_req;
    int scan_found, scan_changed;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_reg.s[0].sensor != 0) {
        primary = 1;
    } else {
        uint8_t order[8];
        int n = af_sns_reg_order(&s_reg, order);
        if (n > 0) primary = order[0];
    }
    if (primary != 0) {
        pcfg = s_reg.s[primary - 1];
        prt = s_rt[primary - 1];
    } else {
        memset(&pcfg, 0, sizeof(pcfg));
        memset(&prt, 0, sizeof(prt));
        prt.err = "no_sensor";
        prt.state = 2;
    }
    reg_copy = s_reg;
    memcpy(rt_copy, s_rt, sizeof(rt_copy));
    memcpy(scan, s_scan, sizeof(scan));
    scan_n = s_scan_n;
    scan_done = s_scan_done;
    avg = s_avg;
    avg_us = s_avg_us;
    nvs_ok = s_nvs_ok;
    bus_ok = s_bus_ok;
    scan_req = s_scan_req;
    scan_found = s_scan_found;
    scan_changed = s_scan_changed;
    xSemaphoreGive(s_mtx);

    ble_gw_status_t b;
    ble_gw_get_status(&b);

    int64_t now_us = esp_timer_get_time();
    int last_ok_age_s = prt.last_ok_us ? (int)((now_us - prt.last_ok_us) / 1000000) : -1;
    int avg_age_s = avg.valid ? (int)((now_us - avg_us) / 1000000) : -1;

    char scan_buf[160];
    int scan_len = 0;
    if (scan_n == 0) {
        scan_buf[0] = '\0';
    } else {
        for (int i = 0; i < scan_n; i++) {
            if (i > 0) scan_buf[scan_len++] = ',';
            scan_len += snprintf(scan_buf + scan_len, sizeof(scan_buf) - scan_len, "\"0x%02X\"", scan[i]);
        }
    }

    const char *status_name = "no_sensor";
    const char *model_name = "none";
    uint8_t caps = 0;
    if (primary != 0) {
        if (!pcfg.enabled) status_name = "disabled";
        else if (!bus_ok) status_name = "bus_error";
        else {
            if (prt.state == 1) status_name = "ok";
            else if (prt.state == 0) status_name = "init";
            else status_name = "read_error";
        }
        const af_sns_preset_t *preset = af_sns_preset(pcfg.model);
        if (preset) {
            model_name = preset->name;
            caps = preset->caps;
        }
    }

    float cps = b.has_rec ? b.cps : NAN;
    float rate_usvh = b.has_rec ? b.doserate_usvh : NAN;
    float rate_urh = b.has_rec ? b.doserate_usvh * 100.0f : NAN;
    uint16_t pulses_2s = b.has_rec ? b.pulses_2s : 0;
    float dose_usv = b.has_rec ? b.dose_msv * 1000.0f : NAN;

    float avg_cps = avg.valid ? avg.cps_avg : NAN;
    float avg_rate_usvh = avg.valid ? avg.rate_avg : NAN;
    uint32_t avg_n = avg.valid ? avg.avg_n : 0;

    char t_show_buf[32], p_show_buf[32], h_show_buf[32], t_raw_buf[32], p_raw_buf[32];
    char t_off_buf[32], p_off_buf[32], h_off_buf[32];
    char cps_buf[32], rate_usvh_buf[32], rate_urh_buf[32], dose_usv_buf[32];
    char avg_cps_buf[32], avg_rate_usvh_buf[32];

    fnum(t_show_buf, sizeof(t_show_buf), prt.t_show, 2);
    fnum(p_show_buf, sizeof(p_show_buf), prt.p_show, 2);
    fnum(h_show_buf, sizeof(h_show_buf), prt.h_show, 2);
    fnum(t_raw_buf, sizeof(t_raw_buf), prt.t_raw, 2);
    fnum(p_raw_buf, sizeof(p_raw_buf), prt.p_raw_mmhg, 2);
    fnum(t_off_buf, sizeof(t_off_buf), pcfg.t_off10 / 10.0f, 1);
    fnum(p_off_buf, sizeof(p_off_buf), pcfg.p_off10 / 10.0f, 1);
    fnum(h_off_buf, sizeof(h_off_buf), pcfg.h_off10 / 10.0f, 1);
    fnum(cps_buf, sizeof(cps_buf), cps, 2);
    fnum(rate_usvh_buf, sizeof(rate_usvh_buf), rate_usvh, 4);
    fnum(rate_urh_buf, sizeof(rate_urh_buf), rate_urh, 2);
    fnum(dose_usv_buf, sizeof(dose_usv_buf), dose_usv, 3);
    fnum(avg_cps_buf, sizeof(avg_cps_buf), avg_cps, 3);
    fnum(avg_rate_usvh_buf, sizeof(avg_rate_usvh_buf), avg_rate_usvh, 4);

    char *sensors_buf = heap_caps_malloc(3000, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sensors_buf) return -1;

    af_sns_view_t views[8];
    int n_views = 0;
    uint8_t order[8];
    int n_order = af_sns_reg_order(&reg_copy, order);
    for (int i = 0; i < n_order; i++) {
        uint8_t s = order[i];
        if (s == 0) continue;
        const af_sns_cfg_t *c = &reg_copy.s[s - 1];
        const rt_t *r = &rt_copy[s - 1];
        af_sns_view_t v = {
            .sensor = s,
            .model = c->model,
            .addr = c->addr,
            .enabled = c->enabled,
            .t_off10 = c->t_off10,
            .p_off10 = c->p_off10,
            .h_off10 = c->h_off10,
            .period_s = c->period_s,
            .state = r->state,
            .caps = af_sns_preset(c->model)->caps,
            .t_c = r->t_show,
            .p_mmhg = r->p_show,
            .h_pct = r->h_show,
            .n_ok = r->n_ok,
            .n_err = r->n_err,
            .store_ok = r->store_ok,
            .store_fail = r->store_fail,
            .last_ok_ts = r->last_ok_ts,
            .last_ok_age_s = r->last_ok_us ? (int)((now_us - r->last_ok_us) / 1000000) : -1,
            .err = r->err
        };
        views[n_views++] = v;
    }

    int sensors_len = af_sns_json_list(views, n_views, sensors_buf, 3000);
    if (sensors_len < 0) {
        heap_caps_free(sensors_buf);
        return -1;
    }

    int len = snprintf(buf, cap,
        "{\"board\":\"%s\",\"i2c\":{\"sda\":%d,\"scl\":%d,\"scan_done\":%d,\"scan\":[%s]},"
        "\"sensor\":{\"status\":\"%s\",\"model\":\"%s\",\"addr\":\"0x%02X\",\"chip_id\":\"0x%02X\",\"caps\":%d,\"temp\":%s,\"press\":%s,\"hum\":%s,"
        "\"t_c\":%s,\"p_mmhg\":%s,\"h_pct\":%s,\"t_raw_c\":%s,\"p_raw_mmhg\":%s,\"n_ok\":%" PRIu32 ",\"n_err\":%" PRIu32 ",\"store_ok\":%" PRIu32 ",\"store_fail\":%" PRIu32 ","
        "\"last_ok_s_ago\":%d,\"last_ok_ts\":%" PRIu32 ",\"err\":\"%s\",\"det76\":\"\",\"det77\":\"\",\"rec_rc\":%d},"
        "\"cfg\":{\"enabled\":%d,\"t_off\":%s,\"p_off\":%s,\"h_off\":%s,\"agg_s\":%d,\"period_s\":%d,\"nvs_ok\":%d},"
        "\"live\":{\"ble\":%s,\"has_rec\":%s,\"cps\":%s,\"rate_usvh\":%s,\"rate_urh\":%s,\"pulses_2s\":%d,\"dose_usv\":%s,"
        "\"avg_cps\":%s,\"avg_rate_usvh\":%s,\"avg_n\":%" PRIu32 ",\"avg_age_s\":%d},"
        "\"agg_s\":%d,\"max_sensors\":8,\"sensors\":%s,\"last_scan\":{\"n\":%d,\"changed\":%d,\"state\":%d}}",
        AF_BOARD_NAME,
        AF_BOARD_I2C_SDA, AF_BOARD_I2C_SCL, scan_done, scan_buf,
        status_name, model_name, primary ? pcfg.addr : 0, primary ? prt.chip : 0, caps,
        (caps & AF_CAP_TEMP) ? "true" : "false",
        (caps & AF_CAP_PRESS) ? "true" : "false",
        (caps & AF_CAP_HUM) ? "true" : "false",
        t_show_buf, p_show_buf, h_show_buf, t_raw_buf, p_raw_buf,
        prt.n_ok, prt.n_err, prt.store_ok, prt.store_fail,
        last_ok_age_s, prt.last_ok_ts, prt.err ? prt.err : "", prt.rec_rc,
        pcfg.enabled, t_off_buf, p_off_buf, h_off_buf, reg_copy.agg_s, pcfg.period_s, nvs_ok,
        b.connected ? "true" : "false",
        b.has_rec ? "true" : "false",
        cps_buf, rate_usvh_buf, rate_urh_buf, pulses_2s, dose_usv_buf,
        avg_cps_buf, avg_rate_usvh_buf, avg_n, avg_age_s,
        reg_copy.agg_s, sensors_buf, scan_found, scan_changed, scan_req
    );

    heap_caps_free(sensors_buf);

    if (len < 0 || (size_t)len >= cap) {
        return -1;
    }
    return len;
}

int af_env_drv_get_air(float *t_c, float *p_mmhg, uint8_t *caps) {
    if (s_mtx == NULL) return -1;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint8_t order[8];
    int n = af_sns_reg_order(&s_reg, order);
    float t = NAN, p = NAN;
    uint8_t c = 0;
    for (int i = 0; i < n; i++) {
        uint8_t s = order[i];
        if (s == 0) continue;
        if (!s_reg.s[s - 1].enabled) continue;
        if (s_rt[s - 1].state != 1) continue;
        if (isnan(t) && !isnan(s_rt[s - 1].t_show)) {
            t = s_rt[s - 1].t_show;
            c |= AF_CAP_TEMP;
        }
        if (isnan(p) && !isnan(s_rt[s - 1].p_show)) {
            p = s_rt[s - 1].p_show;
            c |= AF_CAP_PRESS;
        }
    }
    xSemaphoreGive(s_mtx);
    if (t_c) *t_c = t;
    if (p_mmhg) *p_mmhg = p;
    if (caps) *caps = c;
    return 0;
}
