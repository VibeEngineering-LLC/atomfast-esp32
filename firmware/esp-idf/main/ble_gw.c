#include "af_net.h"
#include "af_parse.h"
#include "af_store.h"
#include "af_log_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "af_hist_query.h"
#include "ble_tgt.h"
#include "af_ble_stats.h"
#include "af_ble_snap.h"
#include "af_ble_nvs.h"
#include "ble_gw_stats.h"
#include "af_meas_drv.h"
#include "af_persist_fs.h"
#include "freertos/queue.h"
#include "ble_devsw.h"
#include "ble_sel.h"
#include "ble_sel_cmd.h"
#include "ble_gw_sel.h"
#include "ble_sel_json.h"
static ble_sel_t s_sel;
static void sel_on_scan_result(esp_ble_gap_cb_param_t *p);
static bool sel_on_open_ok(uint16_t conn_id, const uint8_t addr[6]);
static void sel_on_disc(uint16_t conn_id);
static void sel_on_scan_end(void);
static void sel_on_open_fail(void);
static void sel_on_write_descr_ok(void);
static void sel_on_first_pkt(const af_sample_t *smp);
static void sel_scan_started(void);
static void sel_boot(void);
#include "net_time.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "ble_gw";

static esp_gatt_if_t s_gattc_if = ESP_GATT_IF_NONE;
static uint16_t s_conn_id = 0;
static esp_bd_addr_t s_target_addr;
static esp_ble_addr_type_t s_target_atype = BLE_ADDR_TYPE_PUBLIC;
static bool s_have_target = false;
static uint16_t s_svc_start = 0;
static uint16_t s_svc_end = 0;
static uint16_t s_meas_handle = 0;
static bool s_connected = false;
static bool s_want_scan = false;

static uint32_t s_reconnects = 0;
static uint32_t s_rx_total = 0;
/* e16g (T-56): очередь пакетов для замеров; два уведомления в одном событии соединения раньше сливались в одно (читался только последний пакет) */
#define BLE_PQ_N 16
static ble_gw_pkt_t s_pq[BLE_PQ_N];
static uint32_t s_pq_w = 0, s_pq_r = 0;
static uint32_t s_bad_total = 0;
static uint32_t s_errors = 0;
static uint32_t s_fail_streak = 0;
// e6: Tester sweep (rd 1200-1700 stable, 700-1100 unstable): start delay 1400 ms instead of 3300
// e16e (perf-audit tasks-ble P1-1, оператор 2026-10-27): 600 мс (замеры Тестера: 500/600 стабильны), вне окна 700-1100
#define AF_DELAY0_MS 600
static uint32_t s_delay_ms = AF_DELAY0_MS;
static TickType_t s_last_rx_tick = 0;
static bool s_has_rec = false;
static af_sample_t s_last;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static atomic_uint_fast32_t s_gap_events = 0;
static atomic_uint_fast32_t s_gattc_events = 0;
static atomic_bool s_ready = false;
static SemaphoreHandle_t s_reconnect_sem = NULL;

static esp_bt_uuid_t s_svc_uuid = {
    .len = ESP_UUID_LEN_128,
    .uuid = { .uuid128 = {0x81,0x25,0xc7,0xa1,0x23,0x2d,0xa4,0x87,0xfd,0x4f,0x8c,0xc2,0x4a,0x2a,0x46,0x63} }
};

static const esp_bt_uuid_t s_meas_uuid = {
    .len = ESP_UUID_LEN_128,
    .uuid = { .uuid128 = {0xBD,0xF7,0x54,0xAF,0xB9,0x14,0xED,0x81,0x04,0x43,0x1A,0x7A,0x7E,0x76,0xBC,0x70} }
};

static const esp_bt_uuid_t s_cccd_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG }
};

static esp_ble_scan_params_t s_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,   // e4: имя может быть только в scan response
    .scan_interval = 0x03C0,
    .scan_window = 0x03C0,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
};

static void apply_tx_power(bool conn_phase);

static af_bs_t s_bs;
static af_bs_sess_t *s_bs_ring;
static bool s_bs_ready;
static char s_dev_name[16];
/* e16i b21 (спека §3.3 п.7): первый пакет нового прибора пишется в хранилище после успешной смены, а не теряется */
static af_sample_t s_first_tmp;
static ble_first_tag_t s_first_tag;   /* B03: владелец слота s_first_tmp — seq задания смены */
static uint32_t s_label_drop;   /* V-01: пакетов не записано, метка цели != прибор соединения */
static uint16_t s_pend_ci[3];
static uint16_t s_pend_handle;
static uint64_t s_bs_rssi_ms;

#define BS_NVS_NS "afble"
#define BS_NVS_KEY "bst"

static uint64_t s_conn_t0_ms; /* e16-LOG: начало текущей BLE-сессии (длина сессии в журнале) */

static inline uint64_t bs_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static uint32_t bs_utc(void)
{
    if (net_time_source() == TIME_SRC_NONE) {
        return 0;
    }
    return (uint32_t)time(NULL);
}

static void bs_init(void)
{
    s_bs_ring = heap_caps_malloc(AF_BS_RING_N * sizeof(af_bs_sess_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_bs_ring == NULL) {
        s_bs_ring = heap_caps_malloc(AF_BS_RING_N * sizeof(af_bs_sess_t), MALLOC_CAP_8BIT);
    }

    af_bs_init(&s_bs, s_bs_ring);
    af_bs_scan_params(&s_bs, s_scan_params.scan_interval, s_scan_params.scan_window);

    uint8_t buf[AF_BSN_LEN];
    size_t n = sizeof(buf);
    nvs_handle_t h;
    esp_err_t err = nvs_open(BS_NVS_NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_blob(h, BS_NVS_KEY, buf, &n);
        nvs_close(h);
        if (err != ESP_OK) {
            n = 0;
        }
    } else {
        n = 0;
    }

    af_bsn_decode(&s_bs, buf, n);

    ESP_LOGI(TAG, "bstat: ring=%s life connects=%lu", s_bs_ring ? "ok" : "none", (unsigned long)s_bs.life.connects);

    /* П.10: флаг расширенной статистики из NVS afset/ble_ext (нет ключа - выключено) */
    {
        nvs_handle_t eh;
        uint8_t ev = 0;
        if (nvs_open("afset", NVS_READONLY, &eh) == ESP_OK) {
            if (nvs_get_u8(eh, "ble_ext", &ev) != ESP_OK) {
                ev = 0;
            }
            nvs_close(eh);
        }
        af_bs_set_ext(&s_bs, ev == 1);
    }
    s_bs_ready = true;
}

static void bs_on_attempt(void)
{
    if (!s_bs_ready) return;
    portENTER_CRITICAL(&s_mux);
    af_bs_on_attempt(&s_bs, bs_now_ms());
    portEXIT_CRITICAL(&s_mux);
}

static void bs_on_open_fail(uint16_t reason)
{
    if (!s_bs_ready) return;
    portENTER_CRITICAL(&s_mux);
    af_bs_on_open_fail(&s_bs, bs_now_ms(), reason);
    portEXIT_CRITICAL(&s_mux);
    af_log_event(2, "ble", "connect failed reason=%u", (unsigned)reason);
}

static void bs_on_conn_params(uint16_t interval, uint16_t latency, uint16_t timeout, uint16_t handle)
{
    if (!s_bs_ready) return;
    portENTER_CRITICAL(&s_mux);
    s_pend_ci[0] = interval;
    s_pend_ci[1] = latency;
    s_pend_ci[2] = timeout;
    s_pend_handle = handle;
    portEXIT_CRITICAL(&s_mux);
}

static void bs_on_open_ok(uint16_t mtu)
{
    if (!s_bs_ready) return;
    uint16_t hdl;
    portENTER_CRITICAL(&s_mux);
    af_bs_on_connect(&s_bs, bs_now_ms(), mtu);
    if (s_pend_ci[0] != 0) {
        af_bs_set_conn_params(&s_bs, s_pend_ci[0], s_pend_ci[1], s_pend_ci[2]);
    }
    hdl = s_pend_handle;
    s_pend_ci[0] = 0;
    s_pend_ci[1] = 0;
    s_pend_ci[2] = 0;
    s_pend_handle = 0;
    portEXIT_CRITICAL(&s_mux);

    esp_ble_power_type_t t = (hdl < CONFIG_BT_ACL_CONNECTIONS) ? (esp_ble_power_type_t)(ESP_BLE_PWR_TYPE_CONN_HDL0 + hdl) : ESP_BLE_PWR_TYPE_DEFAULT;
    int lvl = (int)esp_ble_tx_power_get(t);
    if (lvl >= 0 && lvl < 127) {
        portENTER_CRITICAL(&s_mux);
        af_bs_set_tx(&s_bs, (int8_t)lvl);
        portEXIT_CRITICAL(&s_mux);
    }
    s_conn_t0_ms = bs_now_ms();
    af_log_event(1, "ble", "connected mtu=%u", (unsigned)mtu);
}

static void bs_on_mtu(uint16_t mtu)
{
    if (!s_bs_ready) return;
    portENTER_CRITICAL(&s_mux);
    af_bs_set_mtu(&s_bs, mtu);
    portEXIT_CRITICAL(&s_mux);
}

static void bs_on_conn_update(uint16_t interval, uint16_t latency, uint16_t timeout)
{
    if (!s_bs_ready) return;
    portENTER_CRITICAL(&s_mux);
    af_bs_set_conn_params(&s_bs, interval, latency, timeout);
    portEXIT_CRITICAL(&s_mux);
}

static void bs_on_packet(bool ok)
{
    if (!s_bs_ready) return;
    portENTER_CRITICAL(&s_mux);
    af_bs_on_packet(&s_bs, bs_now_ms(), ok);
    portEXIT_CRITICAL(&s_mux);
}

static void bs_on_rssi(int8_t rssi)
{
    if (!s_bs_ready) return;
    portENTER_CRITICAL(&s_mux);
    af_bs_on_rssi(&s_bs, rssi);
    portEXIT_CRITICAL(&s_mux);
}

static void bs_on_disc(uint16_t reason)
{
    if (!s_bs_ready) return;
    portENTER_CRITICAL(&s_mux);
    bool was = s_bs.connected;
    af_bs_on_disconnect(&s_bs, bs_now_ms(), bs_utc(), reason);
    portEXIT_CRITICAL(&s_mux);
    if (was) af_log_event(1, "ble", "disconnected reason=%u dur=%us", (unsigned)reason, (unsigned)((bs_now_ms() - s_conn_t0_ms) / 1000));
}

// закрытие соединения платой: помечаем разрыв локальным (стек отдаёт код 0 вместо 0x16)
static esp_err_t bs_local_close(esp_gatt_if_t gattc_if, uint16_t conn_id)
{
    if (s_bs_ready) {
        portENTER_CRITICAL(&s_mux);
        af_bs_mark_local_close(&s_bs);
        portEXIT_CRITICAL(&s_mux);
    }
    return esp_ble_gattc_close(gattc_if, conn_id);
}

static void bs_on_scan_start(void)
{
    if (!s_bs_ready) return;
    portENTER_CRITICAL(&s_mux);
    af_bs_on_scan_start(&s_bs, bs_now_ms());
    portEXIT_CRITICAL(&s_mux);
}

static void bs_on_scan_adv(void)
{
    if (!s_bs_ready) return;
    portENTER_CRITICAL(&s_mux);
    af_bs_on_scan_adv(&s_bs);
    portEXIT_CRITICAL(&s_mux);
}

static void bs_on_scan_stop(void)
{
    if (!s_bs_ready) return;
    portENTER_CRITICAL(&s_mux);
    af_bs_on_scan_stop(&s_bs);
    portEXIT_CRITICAL(&s_mux);
}

static void bs_on_scan_hit(uint8_t *adv)
{
    if (!s_bs_ready) return;
    portENTER_CRITICAL(&s_mux);
    af_bs_on_scan_hit(&s_bs, bs_now_ms());
    portEXIT_CRITICAL(&s_mux);

    if (adv != NULL) {
        uint8_t *name = NULL;
        uint8_t len = 0;
        name = esp_ble_resolve_adv_data(adv, ESP_BLE_AD_TYPE_NAME_CMPL, &len);
        if (name == NULL) {
            name = esp_ble_resolve_adv_data(adv, ESP_BLE_AD_TYPE_NAME_SHORT, &len);
        }
        if (name != NULL) {
            int i = 0;
            portENTER_CRITICAL(&s_mux);
            for (uint8_t j = 0; j < len && i < 15; j++) {
                char c = (char)name[j];
                if (c >= 0x20 && c <= 0x7E && c != '"' && c != '\\') {
                    s_dev_name[i++] = c;
                }
            }
            s_dev_name[i] = '\0';
            portEXIT_CRITICAL(&s_mux);
        }
    }
}

static void bs_periodic(void)
{
    if (!s_bs_ready) return;
    uint64_t now = bs_now_ms();
    bool conn = false;
    bool due = false;
    uint8_t blob[AF_BSN_LEN];

    portENTER_CRITICAL(&s_mux);
    af_bs_tick(&s_bs, now);
    conn = s_bs.connected;
    due = af_bsn_due(&s_bs, now);
    if (due) {
        af_bsn_encode(&s_bs, blob, sizeof(blob));
    }
    portEXIT_CRITICAL(&s_mux);

    if (conn && s_have_target && (now - s_bs_rssi_ms >= 2000)) {
        s_bs_rssi_ms = now;
        esp_ble_gap_read_rssi(s_target_addr);
    }

    if (due) {
        nvs_handle_t h;
        esp_err_t err = nvs_open(BS_NVS_NS, NVS_READWRITE, &h);
        if (err == ESP_OK) {
            err = nvs_set_blob(h, BS_NVS_KEY, blob, AF_BSN_LEN);
            if (err == ESP_OK) {
                err = nvs_commit(h);
            }
            nvs_close(h);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "bstat nvs: %s", esp_err_to_name(err));
        }
    }

    if (due) {
        portENTER_CRITICAL(&s_mux);
        af_bsn_saved(&s_bs, now);
        portEXIT_CRITICAL(&s_mux);
    }
}

/* П.10: включить/выключить расширенную статистику сразу, без перезагрузки */
void ble_gw_set_ext(bool on)
{
    if (!s_bs_ready) {
        return;
    }
    portENTER_CRITICAL(&s_mux);
    af_bs_set_ext(&s_bs, on);
    portEXIT_CRITICAL(&s_mux);
}

bool ble_gw_stats_snapshot(af_bs_snap_t *o, uint64_t *now_ms)
{
    if (o == NULL || !s_bs_ready) {
        return false;
    }
    uint64_t now = bs_now_ms();
    portENTER_CRITICAL(&s_mux);
    af_bs_tick(&s_bs, now);
    af_bs_snapshot(&s_bs, now, o);
    portEXIT_CRITICAL(&s_mux);
    if (now_ms != NULL) {
        *now_ms = now;
    }
    return true;
}

bool ble_gw_stats_session(uint8_t idx, af_bs_sess_t *out)
{
    if (out == NULL || !s_bs_ready) {
        return false;
    }
    portENTER_CRITICAL(&s_mux);
    const af_bs_sess_t *r = af_bs_ring_get(&s_bs, idx);
    if (r == NULL) {
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    *out = *r;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

void ble_gw_stats_device(char *addr, size_t acap, int *addr_type, char *name, size_t ncap)
{
    if (addr != NULL && acap > 0) {
        if (s_have_target) {
            snprintf(addr, acap, "%02X:%02X:**:**:**:%02X", s_target_addr[0], s_target_addr[1], s_target_addr[5]);
        } else {
            addr[0] = '\0';
        }
    }

    if (addr_type != NULL) {
        if (s_have_target) {
            if ((int)s_target_atype == BLE_ADDR_TYPE_PUBLIC) {
                *addr_type = 0;
            } else if ((int)s_target_atype == BLE_ADDR_TYPE_RANDOM) {
                *addr_type = 1;
            } else {
                *addr_type = -1;
            }
        } else {
            *addr_type = -1;
        }
    }

    if (name != NULL && ncap > 0) {
        portENTER_CRITICAL(&s_mux);
        size_t len = strlen(s_dev_name);
        if (len >= ncap) {
            len = ncap - 1;
        }
        memcpy(name, s_dev_name, len);
        name[len] = '\0';
        portEXIT_CRITICAL(&s_mux);
    }
}
static void start_scan(void)
{
    s_want_scan = true;
    s_have_target = false;
    esp_ble_gap_set_scan_params(&s_scan_params);
}

// Имя в рекламе начинается с "AtomFast" (без учёта регистра)
static bool adv_name_is_af(uint8_t *adv, esp_ble_adv_data_type type)
{
    uint8_t len = 0;
    const uint8_t *nm = esp_ble_resolve_adv_data(adv, type, &len);
    static const char pfx[] = "atomfast";
    if (nm == NULL || len < 8) {
        return false;
    }
    for (int i = 0; i < 8; i++) {
        uint8_t c = nm[i];
        if (c >= 'A' && c <= 'Z') {
            c += 32;
        }
        if (c != (uint8_t)pfx[i]) {
            return false;
        }
    }
    return true;
}

// e4: сервис AtomFast в рекламе (полный или неполный список 128-бит UUID)
static bool adv_uuid_is_af(uint8_t *adv, esp_ble_adv_data_type type)
{
    uint8_t len = 0;
    const uint8_t *u = esp_ble_resolve_adv_data(adv, type, &len);
    for (uint8_t off = 0; u != NULL && off + 16 <= len; off += 16) {
        if (memcmp(u + off, s_svc_uuid.uuid.uuid128, 16) == 0) {
            return true;
        }
    }
    return false;
}

// e5: компактный счётчик скана (без адресов и hex рекламы в логе)
static atomic_uint_fast32_t s_sc_adv = 0;    // принято рекламных пакетов
static atomic_uint_fast32_t s_sc_cand = 0;   // из них кандидатов (имя/UUID AtomFast)

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    atomic_fetch_add(&s_gap_events, 1);

    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (s_want_scan) {
            s_want_scan = false;
            portENTER_CRITICAL(&s_mux);
            bool may = ble_sel_scan_may_start(&s_sel);   /* V-02: скан только в состоянии SCAN автомата */
            portEXIT_CRITICAL(&s_mux);
            if (may) {
                bs_on_scan_start();
                esp_ble_gap_start_scanning(0);
                sel_scan_started();
            }
        }
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            atomic_fetch_add(&s_sc_adv, 1);
            bs_on_scan_adv();
            sel_on_scan_result(param);
        }
        break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        bs_on_scan_stop();
        sel_on_scan_end();
        break;

    case ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT:
        if (param->read_rssi_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            bs_on_rssi(param->read_rssi_cmpl.rssi);
        }
        break;

    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        if (param->update_conn_params.status == ESP_BT_STATUS_SUCCESS) {
            bs_on_conn_update(param->update_conn_params.conn_int, param->update_conn_params.latency, param->update_conn_params.timeout);
        }
        break;

    default:
        break;
    }
}

static void gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    (void)gattc_if;
    atomic_fetch_add(&s_gattc_events, 1);

    switch (event) {
    case ESP_GATTC_REG_EVT:
        if (param->reg.status == ESP_GATT_OK) {
            s_gattc_if = gattc_if;
            sel_boot();
        } else {
            portENTER_CRITICAL(&s_mux);
            s_errors++;
            portEXIT_CRITICAL(&s_mux);
            ESP_LOGE(TAG, "GATTC reg fail: %s", esp_err_to_name(param->reg.status));
        }
        break;

    case ESP_GATTC_OPEN_EVT:
        if (param->open.status != ESP_GATT_OK) {
            bs_on_open_fail((uint16_t)param->open.status);
            portENTER_CRITICAL(&s_mux);
            s_errors++;
            s_fail_streak++;
            portEXIT_CRITICAL(&s_mux);
            sel_on_open_fail();
        } else if (!sel_on_open_ok(param->open.conn_id, param->open.remote_bda)) {
            /* b23 F04: устаревший/чужой OPEN_OK - автомат уже выдал CLOSE, внешнюю оболочку не трогаем */
            ESP_LOGW(TAG, "open ok отклонён автоматом, conn=%u", (unsigned)param->open.conn_id);
        } else {
            s_conn_id = param->open.conn_id;
            portENTER_CRITICAL(&s_mux);
            s_connected = true;
            s_fail_streak = 0;
            s_delay_ms = AF_DELAY0_MS;
            s_last_rx_tick = xTaskGetTickCount();
            s_meas_handle = 0;
            s_svc_start = 0;
            s_svc_end = 0;
            portEXIT_CRITICAL(&s_mux);
            apply_tx_power(true);
            bs_on_open_ok(param->open.mtu);
            ble_tl("open ok");
            esp_ble_gattc_search_service(gattc_if, s_conn_id, &s_svc_uuid);
        }
        break;

    case ESP_GATTC_CONNECT_EVT:
        bs_on_conn_params(param->connect.conn_params.interval, param->connect.conn_params.latency,
                          param->connect.conn_params.timeout, param->connect.conn_handle);
        break;

    case ESP_GATTC_CFG_MTU_EVT:
        if (param->cfg_mtu.status == ESP_GATT_OK) {
            bs_on_mtu(param->cfg_mtu.mtu);
        }
        break;

    case ESP_GATTC_SEARCH_RES_EVT:
        if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128) {
            if (memcmp(param->search_res.srvc_id.uuid.uuid.uuid128, s_svc_uuid.uuid.uuid128, 16) == 0) {
                portENTER_CRITICAL(&s_mux);
                s_svc_start = param->search_res.start_handle;
                s_svc_end = param->search_res.end_handle;
                portEXIT_CRITICAL(&s_mux);
            }
        }
        break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
        if (param->search_cmpl.status != ESP_GATT_OK) {
            portENTER_CRITICAL(&s_mux);
            s_errors++;
            portEXIT_CRITICAL(&s_mux);
            bs_local_close(gattc_if, s_conn_id);
            break;
        }

        // Проверяем, нашли ли мы сервис (s_svc_end должен быть установлен, если сервис найден)
        // Но в SEARCH_RES мы не знаем точно, что это наш сервис, пока не сравним.
        // Лучше проверять через get_char_by_uuid, если s_svc_start != 0.
        // Однако, если сервис не найден, s_svc_start останется 0.
        
        if (s_svc_start == 0) {
            portENTER_CRITICAL(&s_mux);
            s_errors++;
            portEXIT_CRITICAL(&s_mux);
            bs_local_close(gattc_if, s_conn_id);
            break;
        }

        esp_gattc_char_elem_t elem;
        uint16_t count = 1;
        esp_gatt_status_t gst = esp_ble_gattc_get_char_by_uuid(gattc_if, s_conn_id, s_svc_start, s_svc_end, s_meas_uuid, &elem, &count);
        if (gst != ESP_GATT_OK || count == 0) {
            portENTER_CRITICAL(&s_mux);
            s_errors++;
            portEXIT_CRITICAL(&s_mux);
            bs_local_close(gattc_if, s_conn_id);
        } else {
            portENTER_CRITICAL(&s_mux);
            s_meas_handle = elem.char_handle;
            portEXIT_CRITICAL(&s_mux);
            esp_ble_gattc_register_for_notify(gattc_if, s_sel.conn_addr, s_meas_handle);
        }
        break;

    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (param->reg_for_notify.status != ESP_GATT_OK) {
            portENTER_CRITICAL(&s_mux);
            s_errors++;
            portEXIT_CRITICAL(&s_mux);
            bs_local_close(gattc_if, s_conn_id);
            break;
        }

        esp_gattc_descr_elem_t descr;
        uint16_t count = 1;
        esp_gatt_status_t gst = esp_ble_gattc_get_descr_by_char_handle(gattc_if, s_conn_id, param->reg_for_notify.handle, s_cccd_uuid, &descr, &count);
        if (gst != ESP_GATT_OK || count == 0) {
            portENTER_CRITICAL(&s_mux);
            s_errors++;
            portEXIT_CRITICAL(&s_mux);
            bs_local_close(gattc_if, s_conn_id);
            break;
        }

        uint8_t notify_en[2] = {0x01, 0x00};
        esp_err_t err = esp_ble_gattc_write_char_descr(gattc_if, s_conn_id, descr.handle, sizeof(notify_en), notify_en, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
        if (err != ESP_OK) {
            portENTER_CRITICAL(&s_mux);
            s_errors++;
            portEXIT_CRITICAL(&s_mux);
            bs_local_close(gattc_if, s_conn_id);
        }
        break;

    }
    case ESP_GATTC_WRITE_DESCR_EVT:
        if (param->write.status != ESP_GATT_OK) {
            portENTER_CRITICAL(&s_mux);
            s_errors++;
            portEXIT_CRITICAL(&s_mux);
            bs_local_close(gattc_if, s_conn_id);
        } else {
            ESP_LOGI(TAG, "подписка включена");
            sel_on_write_descr_ok();
            ble_relax_conn_now(s_sel.conn_addr);   // e16e: после CCCD интервал 45-100 мс (пакет раз в 2 с)
        }
        break;

    case ESP_GATTC_NOTIFY_EVT: {
        if (param->notify.handle != s_meas_handle) {
            break;
        }

        af_sample_t tmp;
        af_status_t st = af_parse(param->notify.value, param->notify.value_len, &tmp);
        ble_pkt_dec_t dec;
        uint32_t store_gen = 0;
        
        portENTER_CRITICAL(&s_mux);
        dec = ble_sel_accept_pkt(&s_sel, st == AF_OK);
        if (dec.store && !ble_sel_label_ok(&s_sel, s_have_target, s_target_addr)) { dec.store = 0; s_label_drop++; } /* V-01: метка = прибор соединения */
        store_gen = af_store_gen();
        s_rx_total++;
        if (dec.store) {
            s_last = tmp;
            s_has_rec = true;
            s_pq[s_pq_w % BLE_PQ_N] = (ble_gw_pkt_t){ (uint64_t)(esp_timer_get_time() / 1000), tmp.pulses_2s, tmp.doserate_usvh };
            s_pq_w++;
            if (s_pq_w - s_pq_r > BLE_PQ_N) s_pq_r = s_pq_w - BLE_PQ_N;
        } else if (st != AF_OK) {
            s_bad_total++;
        }
        s_last_rx_tick = xTaskGetTickCount();
        portEXIT_CRITICAL(&s_mux);
        bs_on_packet(st == AF_OK);
        if (dec.store) af_store_submit_gen(&tmp, store_gen);  // Э3b: неблокирующая очередь
        if (dec.first) sel_on_first_pkt(&tmp);   /* B03: слот пишется под s_mux вместе с выдачей задания */
        break;
    }
    case ESP_GATTC_DISCONNECT_EVT: {
        portENTER_CRITICAL(&s_mux);
        bool was_connected = s_connected;
        s_connected = false;
        if (was_connected) {
            s_fail_streak++;
        }
        portEXIT_CRITICAL(&s_mux);
        bs_on_disc((uint16_t)param->disconnect.reason);
        sel_on_disc(param->disconnect.conn_id);
        break;

    }
    case ESP_GATTC_CLOSE_EVT:
        bs_on_disc((uint16_t)param->close.reason);
        sel_on_disc(param->close.conn_id);
        portENTER_CRITICAL(&s_mux);
        if (s_connected) {
            s_connected = false;
            s_fail_streak++;
            portEXIT_CRITICAL(&s_mux);
        } else {
            portEXIT_CRITICAL(&s_mux);
        }
        break;

    default:
        break;
    }
}

static void reconnect_task(void *arg)
{
    (void)arg;
    for (;;) {
        TickType_t wait_ticks = pdMS_TO_TICKS(1000);
        BaseType_t sem_ret = xSemaphoreTake(s_reconnect_sem, wait_ticks);
        bs_periodic();
        static TickType_t s_stat_tick = 0;
        TickType_t now_t = xTaskGetTickCount();
        if (s_stat_tick == 0 || (now_t - s_stat_tick) >= pdMS_TO_TICKS(30000)) {
            s_stat_tick = now_t ? now_t : 1;
            ESP_LOGI(TAG, "scan stat: adv=%lu cand=%lu target=%d conn=%d",
                     (unsigned long)atomic_load(&s_sc_adv), (unsigned long)atomic_load(&s_sc_cand),
                     (int)s_have_target, (int)s_connected);
            if (s_label_drop) ESP_LOGW(TAG, "label drop: n=%lu", (unsigned long)s_label_drop);
        }
        // Проверка тишины
        portENTER_CRITICAL(&s_mux);
        bool is_connected = s_connected;
        TickType_t last_rx = s_last_rx_tick;
        portEXIT_CRITICAL(&s_mux);

        if (is_connected) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_rx) > pdMS_TO_TICKS(10000)) {
                portENTER_CRITICAL(&s_mux);
                s_errors++;
                portEXIT_CRITICAL(&s_mux);
                bs_local_close(s_gattc_if, s_conn_id);
                continue;
            }
        }

        (void)sem_ret;
        if (1) { /* T-86: переподключением владеет автомат ble_sel */
            continue;
        }

        portENTER_CRITICAL(&s_mux);
        s_reconnects++;
        uint32_t delay = s_delay_ms;
        s_delay_ms = (s_delay_ms * 2 > 60000) ? 60000 : (s_delay_ms * 2);
        uint32_t fail_streak = s_fail_streak;
        bool have_target = s_have_target;
        portEXIT_CRITICAL(&s_mux);

        ESP_LOGI(TAG, "reconnect: delay=%lu ms, fails=%lu", (unsigned long)delay, (unsigned long)fail_streak);
        vTaskDelay(pdMS_TO_TICKS(delay));

        if (fail_streak >= 3 || !have_target) {
            portENTER_CRITICAL(&s_mux);
            s_fail_streak = 0;
            portEXIT_CRITICAL(&s_mux);
            start_scan();
        } else {
            bs_on_attempt();
            esp_err_t err = esp_ble_gattc_open(s_gattc_if, s_target_addr, s_target_atype, true);
            if (err != ESP_OK) {
                portENTER_CRITICAL(&s_mux);
                s_errors++;
                portEXIT_CRITICAL(&s_mux);
                if (s_reconnect_sem) xSemaphoreGive(s_reconnect_sem);
            }
        }
    }
}

// e5: типы мощности ESP-IDF 5.4 (enum: CONN_HDL0..8 = 0..8, ADV = 9, SCAN = 10, DEFAULT = 11).
// Контроллер принимает CONN_HDLn только для n < числа соединений (CONFIG_BT_ACL_CONNECTIONS);
// HDL4..8 при значении 4 возвращали ошибку (8 предупреждений на старте в e4). e5-вывод: на старте до
// соединения ошибку дают и HDL0..3 (соединения нет), поэтому CONN_HDLn ставится только после OPEN_EVT.
static const char *pwr_name(esp_power_level_t l)
{
    return l == ESP_PWR_LVL_P20 ? "P20(+20dBm)" : (l == ESP_PWR_LVL_INVALID ? "INVALID" : "other");
}

static void apply_tx_power(bool conn_phase)
{
    const esp_ble_power_type_t base[] = { ESP_BLE_PWR_TYPE_DEFAULT, ESP_BLE_PWR_TYPE_ADV, ESP_BLE_PWR_TYPE_SCAN };
    for (size_t i = 0; i < sizeof(base) / sizeof(base[0]); i++) {
        if (esp_ble_tx_power_set(base[i], ESP_PWR_LVL_P20) != ESP_OK) {
            ESP_LOGW(TAG, "tx_power_set fail for type %d", (int)base[i]);
        }
    }
    if (!conn_phase) {
        return;   // CONN_HDLn существует только у установленного соединения (e5, проверено на железе)
    }
    // Номер HCI-handle заранее неизвестен (в e5 первое соединение получило HDL1): выставляем 0..N-1,
    // принимает только существующий; ошибка для остальных ожидаема и не логируется как сбой.
    int ok_mask = 0;
    esp_power_level_t cl[CONFIG_BT_ACL_CONNECTIONS];
    for (int h = 0; h < CONFIG_BT_ACL_CONNECTIONS; h++) {
        if (esp_ble_tx_power_set((esp_ble_power_type_t)(ESP_BLE_PWR_TYPE_CONN_HDL0 + h), ESP_PWR_LVL_P20) == ESP_OK) {
            ok_mask |= 1 << h;
        }
        cl[h] = esp_ble_tx_power_get((esp_ble_power_type_t)(ESP_BLE_PWR_TYPE_CONN_HDL0 + h));
    }
    esp_power_level_t d = esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_DEFAULT);
    esp_power_level_t a = esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_ADV);
    esp_power_level_t s = esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_SCAN);
    ESP_LOGI(TAG, "tx_power eff: default=%d(%s) adv=%d(%s) scan=%d(%s) conn_set_mask=0x%x [P20=%d]",
             (int)d, pwr_name(d), (int)a, pwr_name(a), (int)s, pwr_name(s), ok_mask, (int)ESP_PWR_LVL_P20);
    for (int h = 0; h < CONFIG_BT_ACL_CONNECTIONS; h++) {
        if (ok_mask & (1 << h)) {
            ESP_LOGI(TAG, "tx_power eff: conn_hdl%d=%d(%s)", h, (int)cl[h], pwr_name(cl[h]));
        }
    }
}

/* --- devsw state (not present elsewhere in ble_gw.c) --- */
static volatile uint8_t s_conn_valid;
static uint16_t s_devsw_conn_cur;   /* B02: conn_id исполняемого задания; актуальность — по автомату, не по копии seq */

/* --- devsw ops callbacks --- */

static bool dsw_meas_active(void *ctx)
{
    return af_meas_drv_active_id() != 0;
}

static bool dsw_still_current(void *ctx, uint32_t seq)
{
    portENTER_CRITICAL(&s_mux);
    bool ok = s_connected && ble_sel_devsw_fresh(&s_sel, seq, s_devsw_conn_cur);
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

/* B02: перед каждым необратимым шагом после первой очистки */
static bool dsw_owner(void *ctx, uint32_t seq)
{
    portENTER_CRITICAL(&s_mux);
    bool ok = ble_sel_devsw_owner(&s_sel, seq);
    portEXIT_CRITICAL(&s_mux);
    return ok;
}
static void dsw_drop_state(void *ctx)
{
    ble_gw_drop_packets();
    portENTER_CRITICAL(&s_mux);
    s_has_rec = false;
    s_dev_name[0] = '\0';
    portEXIT_CRITICAL(&s_mux);
}

static uint32_t dsw_rings_count(void *ctx)
{
    af_store_status_t st;
    af_store_get_status(&st);
    return st.raw.count + st.agg.count;
}

static int dsw_rings_clear(void *ctx, int *persisted)
{
    return af_store_clear_for_device_change(persisted);
}

static bool dsw_pfs_mounted(void *ctx)
{
    return af_pfs_mounted() != 0;
}

/* b29 B06: имя — из задания (req.name), не из общего снимка автомата; сбой записи имени не маскируется */
static int dsw_tgt_save(void *ctx, const uint8_t addr[6], uint8_t atype, const char *name)
{
    (void)ctx;
    int rc = ble_tgt_save_named(addr, atype, name);
    if (rc == BLE_TGT_SAVE_NONAME) {
        ESP_LOGW(TAG, "devsw: target saved, name not saved");
        return BLE_DEVSW_SAVE_NONAME;
    }
    return rc == 0 ? 0 : -1;
}

static int dsw_tgt_clear(void *ctx)
{
    return ble_tgt_clear() == ESP_OK ? 0 : -1;
}

/* b29: флаг оболочки не читается, s_sel.devsw_commit не ставится. Таймаут (f) ble_sel_tick всё равно не
   прерывает начатую запись: тик и задание исполняются в одной задаче sel_task (тик — только в ветке
   без задания), результат задания обрабатывается в той же итерации, до следующего тика. */
static void dsw_set_commit(void *ctx)
{
    portENTER_CRITICAL(&s_mux);
    portEXIT_CRITICAL(&s_mux);
}

static void dsw_commit_ram(void *ctx, const ble_devsw_req_t *req)
{
    portENTER_CRITICAL(&s_mux);
    if (req->op == BLE_OP_FORGET) {
        memset(s_target_addr, 0, sizeof s_target_addr);
        s_target_atype = (esp_ble_addr_type_t)0;
        s_have_target = false;
        s_dev_name[0] = '\0';
    } else {
        memcpy(s_target_addr, req->addr, 6);
        s_target_atype = (esp_ble_addr_type_t)req->atype;
        s_have_target = true;
        strncpy(s_dev_name, req->name, sizeof(s_dev_name) - 1);
        s_dev_name[sizeof(s_dev_name) - 1] = '\0';
    }
    portEXIT_CRITICAL(&s_mux);
}

static bool dsw_set_valid(void *ctx, uint32_t seq)
{
    portENTER_CRITICAL(&s_mux);
    bool ok = false;
    if (s_connected && ble_sel_devsw_fresh(&s_sel, seq, s_devsw_conn_cur)) {   /* B02: устаревшее задание не валидирует новое соединение */
        s_conn_valid = 1;
        s_last_rx_tick = xTaskGetTickCount();
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

static void dsw_sleep_ms(void *ctx, uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* --- ops table --- */

static const ble_devsw_ops_t s_devsw_ops = {
    .ctx          = NULL,
    .meas_active  = dsw_meas_active,
    .still_current = dsw_still_current,
    .drop_state   = dsw_drop_state,
    .rings_count  = dsw_rings_count,
    .rings_clear  = dsw_rings_clear,
    .pfs_mounted  = dsw_pfs_mounted,
    .tgt_save     = dsw_tgt_save,
    .tgt_clear    = dsw_tgt_clear,
    .set_commit   = dsw_set_commit,
    .commit_ram   = dsw_commit_ram,
    .set_valid    = dsw_set_valid,
    .sleep_ms     = dsw_sleep_ms,
    .owner        = dsw_owner,
};
/* --- entry point --- */

static ble_devsw_res_t dsw_run(uint8_t op, uint32_t seq, uint16_t conn_id,
                               const uint8_t addr[6], uint8_t atype, const char *name)
{
    ble_devsw_req_t req;
    memset(&req, 0, sizeof(req));
    req.op      = op;
    req.seq     = seq;
    req.conn_id = conn_id;
    memcpy(req.addr, addr, 6);
    req.atype   = atype;
    if (name) {
        strncpy(req.name, name, sizeof(req.name) - 1);
        req.name[sizeof(req.name) - 1] = '\0';
    }

    portENTER_CRITICAL(&s_mux);
    s_devsw_conn_cur = conn_id;
    portEXIT_CRITICAL(&s_mux);
    return ble_devsw_run(&s_devsw_ops, &req);
}

/* --- ble_sel executor (T-86) --- */

static uint32_t sel_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void sel_dsw_enqueue(const ble_act_t *a);
static void sel_exec_depth(const ble_acts_t *acts, int depth)
{
    for (uint8_t i = 0; i < acts->n; i++) {
        const ble_act_t *a = &acts->a[i];
        switch (a->kind) {
        case BLE_ACT_SCAN_START: {
            bs_on_scan_start();
            esp_err_t err = esp_ble_gap_start_scanning(0);
            if (err == ESP_OK) {
                sel_scan_started();
            }
            if (err != ESP_OK && depth < 1) {
                ble_acts_t next;
                uint32_t now = sel_now_ms();
                portENTER_CRITICAL(&s_mux);
                ble_acts_reset(&next);
                ble_sel_on_scan_start(&s_sel, false, now, &next);
                portEXIT_CRITICAL(&s_mux);
                sel_exec_depth(&next, depth + 1);
            }
            break;
        }
        case BLE_ACT_SCAN_STOP:
            esp_ble_gap_stop_scanning();
            break;
        case BLE_ACT_OPEN: {
            bs_on_attempt();
            esp_bd_addr_t oa;
            memcpy(oa, a->addr, sizeof oa);
            esp_err_t err = esp_ble_gattc_open(s_gattc_if, oa, (esp_ble_addr_type_t)a->atype, true);
            if (err != ESP_OK) {
                ble_acts_t next;
                uint32_t now = sel_now_ms();
                portENTER_CRITICAL(&s_mux);
                ble_acts_reset(&next);
                ble_sel_on_open_fail(&s_sel, now, &next);
                portEXIT_CRITICAL(&s_mux);
                sel_exec_depth(&next, depth + 1);
            }
            break;
        }
        case BLE_ACT_CLOSE_EVT_CONN:
            esp_ble_gattc_close(s_gattc_if, a->conn_id);
            break;
        case BLE_ACT_CLOSE_CUR:
            bs_local_close(s_gattc_if, s_conn_id);
            break;
        case BLE_ACT_DEVSW:
            sel_dsw_enqueue(a);
            break;
        case BLE_ACT_FAST_CONN:
            ble_fast_conn_now(a->addr);
            break;
        case BLE_ACT_SET_VALID_SAME:
            portENTER_CRITICAL(&s_mux); s_conn_valid = 1; s_last_rx_tick = xTaskGetTickCount(); portEXIT_CRITICAL(&s_mux);
            break;
        case BLE_ACT_NOTE_ERR:
            /* note/err фиксирует сам автомат (s_sel.err/note), исполнителю делать нечего */
            break;
        default:
            break;
        }
    }
}

static void sel_exec(const ble_acts_t *acts)
{
    sel_exec_depth(acts, 0);
}

static void sel_on_scan_result(esp_ble_gap_cb_param_t *p)
{
    if (p->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) {
        return;
    }
    uint8_t len = 0;
    const uint8_t *nm = esp_ble_resolve_adv_data(p->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &len);
    if (nm == NULL) {
        len = 0;
        nm = esp_ble_resolve_adv_data(p->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_SHORT, &len);
    }
    char name_buf[BLE_SEL_NAME_LEN];
    memset(name_buf, 0, sizeof name_buf);
    if (nm != NULL) {
        size_t n = len < sizeof name_buf - 1 ? len : sizeof name_buf - 1;
        memcpy(name_buf, nm, n);
    }
    const char *name = name_buf;
    bool has_uuid = adv_uuid_is_af(p->scan_rst.ble_adv, ESP_BLE_AD_TYPE_128SRV_CMPL) ||
                    adv_uuid_is_af(p->scan_rst.ble_adv, ESP_BLE_AD_TYPE_128SRV_PART);

    ble_acts_t acts;
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    ble_acts_reset(&acts);
    ble_sel_on_scan_result(&s_sel, p->scan_rst.bda,
                           (uint8_t)p->scan_rst.ble_addr_type,
                           p->scan_rst.rssi, name, has_uuid, now, &acts);
    portEXIT_CRITICAL(&s_mux);
    sel_exec(&acts);
}

static bool sel_on_open_ok(uint16_t conn_id, const uint8_t addr[6])
{
    ble_acts_t acts;
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    ble_acts_reset(&acts);
    ble_sel_on_open_ok(&s_sel, conn_id, addr, now, &acts);
    bool accepted = (s_sel.connected && s_sel.conn_id == conn_id);
    portEXIT_CRITICAL(&s_mux);
    sel_exec(&acts);
    return accepted;
}

static void sel_on_disc(uint16_t conn_id)
{
    ble_acts_t acts;
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    ble_acts_reset(&acts);
    ble_sel_on_disc(&s_sel, conn_id, now, &acts);
    portEXIT_CRITICAL(&s_mux);
    sel_exec(&acts);
}

/* --- sel_dsw job & queue --- */

typedef struct {
    uint8_t  op;
    uint32_t seq;
    uint16_t conn_id;
    uint8_t  addr[6];
    uint8_t  atype;
    char     name[16];
} sel_dsw_job_t;

static QueueHandle_t s_sel_q;

static void sel_dsw_enqueue(const ble_act_t *a)
{
    sel_dsw_job_t job;
    memset(&job, 0, sizeof(job));
    job.op  = a->op;
    job.seq = a->seq;
    portENTER_CRITICAL(&s_mux);
    job.conn_id = s_sel.conn_id;
    memcpy(job.addr, s_sel.devsw_addr, 6);   /* снимок автомата на момент выдачи задания (V-01) */
    job.atype    = s_sel.devsw_atype;
    strncpy(job.name, s_sel.devsw_name, sizeof(job.name) - 1);
    job.name[sizeof(job.name) - 1] = '\0';
    portEXIT_CRITICAL(&s_mux);

    if (xQueueSend(s_sel_q, &job, 0) != pdTRUE) {
        ble_acts_t next;
        uint32_t now = sel_now_ms();
        portENTER_CRITICAL(&s_mux);
        ble_acts_reset(&next);
        ble_sel_on_devsw_done(&s_sel, a->seq, -1, BLE_DSW_E_ABORTED, false, now, &next);
        portEXIT_CRITICAL(&s_mux);
        sel_exec(&next);
    }
}

static void sel_task(void *arg)
{
    (void)arg;
    for (;;) {
        sel_dsw_job_t job;
        if (xQueueReceive(s_sel_q, &job, pdMS_TO_TICKS(250)) == pdTRUE) {
            ble_devsw_res_t r = dsw_run(job.op, job.seq, job.conn_id,
                                        job.addr, job.atype, job.name);
            ble_acts_t acts;
            uint32_t now = sel_now_ms();
            portENTER_CRITICAL(&s_mux);
            ble_acts_reset(&acts);
            ble_sel_on_devsw_done(&s_sel, job.seq, r.rc, r.err, r.valid != 0, now, &acts);
            /* B03: первый пакет пишется, только если слот принадлежит этому заданию и автомат принял результат */
            af_sample_t first;
            memset(&first, 0, sizeof first);
            bool first_ok = ble_first_take(&s_first_tag, &s_sel, job.seq,
                                           job.op == BLE_OP_CONFIRM_NEW && r.rc == 0 && r.valid != 0);
            if (first_ok) first = s_first_tmp;
            portEXIT_CRITICAL(&s_mux);
            if (first_ok) af_store_submit(&first);
            sel_exec(&acts);
        } else {
            ble_acts_t acts;
            uint32_t now = sel_now_ms();
            portENTER_CRITICAL(&s_mux);
            ble_acts_reset(&acts);
            ble_sel_tick(&s_sel, now, &acts);
            portEXIT_CRITICAL(&s_mux);
            if (acts.n > 0) {
                sel_exec(&acts);
            }
        }
    }
}

static void sel_start(void)
{
    if (s_sel_q != NULL) {
        return;
    }
    s_sel_q = xQueueCreate(2, sizeof(sel_dsw_job_t));
    if (s_sel_q == NULL) {
        /* TODO(лог_ошибки) */
        return;
    }
    xTaskCreate(sel_task, "ble_sel", 4096, NULL, 4, NULL);
}
static void sel_on_scan_end(void)
{
    ble_acts_t acts;
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    ble_acts_reset(&acts);
    ble_sel_on_scan_end(&s_sel, now, &acts);
    portEXIT_CRITICAL(&s_mux);
    sel_exec(&acts);
}
static void sel_on_open_fail(void)
{
    ble_acts_t acts;
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    ble_acts_reset(&acts);
    ble_sel_on_open_fail(&s_sel, now, &acts);
    portEXIT_CRITICAL(&s_mux);
    sel_exec(&acts);
}
static void sel_on_write_descr_ok(void)
{
    ble_acts_t acts;
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    ble_acts_reset(&acts);
    ble_sel_on_write_descr_ok(&s_sel, now, &acts);
    portEXIT_CRITICAL(&s_mux);
    sel_exec(&acts);
}
static void sel_on_first_pkt(const af_sample_t *smp)
{
    ble_acts_t acts;
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    ble_acts_reset(&acts);
    ble_sel_on_first_pkt(&s_sel, now, &acts);
    if (ble_first_put(&s_first_tag, &acts)) s_first_tmp = *smp;   /* B03: слот и его владелец — под одним s_mux */
    portEXIT_CRITICAL(&s_mux);
    sel_exec(&acts);
}
static void sel_scan_started(void)
{
    ble_acts_t acts;
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    ble_acts_reset(&acts);
    ble_sel_on_scan_start(&s_sel, true, now, &acts);
    portEXIT_CRITICAL(&s_mux);
    sel_exec(&acts);
}
static void sel_boot(void)
{
    ble_acts_t acts;
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    ble_acts_reset(&acts);
    ble_sel_init(&s_sel, s_have_target ? s_target_addr : NULL, (uint8_t)s_target_atype, s_dev_name, now, &acts);
    portEXIT_CRITICAL(&s_mux);
    s_want_scan = !s_have_target;   /* V-02: при сохранённой цели скан после set_scan_params не стартует */
    esp_ble_gap_set_scan_params(&s_scan_params);
    if (s_have_target) {
        bs_on_attempt();
        sel_exec(&acts);
    }
    sel_start();
}
int ble_gw_sel_json(char *buf, size_t cap)
{
    portENTER_CRITICAL(&s_mux);
    ble_sel_t cp = s_sel;
    portEXIT_CRITICAL(&s_mux);
    return ble_sel_json(&cp, sel_now_ms(), buf, cap);
}

int ble_gw_sel_select(uint32_t gen, uint8_t tok, bool confirm_clear, uint32_t *ring_count)
{
    bool meas = af_meas_drv_active_id() != 0;
    uint32_t rings = dsw_rings_count(NULL);
    ble_acts_t acts;
    ble_acts_reset(&acts);
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    int rc = ble_sel_select_blocked(&s_sel) ? BLE_E_BUSY   /* B02: смена прибора ещё исполняется */
           : ble_sel_cmd_select(&s_sel, gen, tok, confirm_clear, meas, rings, now, &acts);
    portEXIT_CRITICAL(&s_mux);
    if (acts.n > 0) sel_exec(&acts);
    if (ring_count) *ring_count = rings;
    return rc;
}

int ble_gw_sel_rescan(bool replace, uint32_t *gen_out)
{
    bool meas = af_meas_drv_active_id() != 0;
    ble_acts_t acts;
    ble_acts_reset(&acts);
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    int rc = ble_sel_cmd_rescan(&s_sel, replace, meas, now, &acts);
    if (gen_out) *gen_out = s_sel.gen;
    portEXIT_CRITICAL(&s_mux);
    if (acts.n > 0) sel_exec(&acts);
    return rc;
}

int ble_gw_sel_forget(bool confirm)
{
    bool meas = af_meas_drv_active_id() != 0;
    ble_acts_t acts;
    ble_acts_reset(&acts);
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    int rc = ble_sel_cmd_forget(&s_sel, confirm, meas, now, &acts);
    portEXIT_CRITICAL(&s_mux);
    if (acts.n > 0) sel_exec(&acts);
    return rc;
}

int ble_gw_sel_cancel(void)
{
    ble_acts_t acts;
    ble_acts_reset(&acts);
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    int rc = ble_sel_cmd_cancel(&s_sel, now, &acts);
    portEXIT_CRITICAL(&s_mux);
    if (acts.n > 0) sel_exec(&acts);
    return rc;
}

int ble_gw_sel_manual(const uint8_t addr[6], uint8_t atype, bool confirm_clear, uint32_t *ring_count)
{
    bool meas = af_meas_drv_active_id() != 0;
    uint32_t rings = dsw_rings_count(NULL);
    ble_acts_t acts;
    ble_acts_reset(&acts);
    uint32_t now = sel_now_ms();
    portENTER_CRITICAL(&s_mux);
    int rc;
    int idx = -1;
    if (ble_sel_select_blocked(&s_sel)) {
        rc = BLE_E_BUSY;   /* B02: смена прибора ещё исполняется */
    } else if ((idx = ble_cands_update(&s_sel.cands, addr, atype, -60, "manual", true, now)) < 0) {
        rc = BLE_E_BAD;
    } else {
        uint8_t tok = s_sel.cands.c[idx].tok;
        uint32_t gen = s_sel.gen;
        rc = ble_sel_cmd_select(&s_sel, gen, tok, confirm_clear, meas, rings, now, &acts);
    }
    portEXIT_CRITICAL(&s_mux);
    if (acts.n > 0) sel_exec(&acts);
    if (ring_count) *ring_count = rings;
    return rc;
}
void ble_gw_init(void)
{
    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mem_release: %s", esp_err_to_name(err));   // как у донора - не фатально
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "controller_init fail: %s", esp_err_to_name(err));
        return;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "controller_enable fail: %s", esp_err_to_name(err));
        return;
    }

    err = esp_bluedroid_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid_init fail: %s", esp_err_to_name(err));
        return;
    }

    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid_enable fail: %s", esp_err_to_name(err));
        return;
    }

    bs_init();
    apply_tx_power(false);

    err = esp_ble_gap_register_callback(gap_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gap_cb reg fail: %s", esp_err_to_name(err));
        return;
    }

    err = esp_ble_gattc_register_callback(gattc_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gattc_cb reg fail: %s", esp_err_to_name(err));
        return;
    }

    s_reconnect_sem = xSemaphoreCreateBinary();
    if (s_reconnect_sem == NULL) {
        ESP_LOGE(TAG, "sem create fail");
        return;
    }

    if (xTaskCreate(reconnect_task, "af_reconn", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "task create fail");
        return;
    }

    s_delay_ms = AF_DELAY0_MS;
    s_want_scan = true;
    { uint8_t at = 0; if (ble_tgt_load(s_target_addr, &at)) { s_have_target = true; s_target_atype = (esp_ble_addr_type_t)at; (void)ble_tgt_name_read(s_dev_name, sizeof s_dev_name); } ble_tl(s_have_target ? "NVS target loaded" : "no NVS target"); }

    err = esp_ble_gattc_app_register(0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "app_register fail: %s", esp_err_to_name(err));
        return;
    }

    atomic_store(&s_ready, true);
    ESP_LOGI(TAG, "BLE central ready (Bluedroid, TX P20)");
}

/* e11, MAC дозиметра (сохранённая цель) для заголовка X-AF-Device и столбца mac CSV; нет цели - -1. */
int ble_gw_target_mac(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return -1;
    }
    out[0] = 0;
    if (!s_have_target) {
        return -1;
    }
    if (af_hq_fmt_mac((const uint8_t *)s_target_addr, out, cap) == 17) {
        return 0;
    }
    return -1;
}
bool ble_gw_is_ready(void)
{
    return atomic_load(&s_ready);
}

void ble_gw_get_diag(uint32_t *gap_events, uint32_t *gattc_events)
{
    if (gap_events) *gap_events = atomic_load(&s_gap_events);
    if (gattc_events) *gattc_events = atomic_load(&s_gattc_events);
}

bool ble_gw_pop_packet(ble_gw_pkt_t *p)
{
    if (p == NULL) return false;
    portENTER_CRITICAL(&s_mux);
    bool ok = (s_pq_r != s_pq_w);
    if (ok) { *p = s_pq[s_pq_r % BLE_PQ_N]; s_pq_r++; }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

void ble_gw_drop_packets(void)
{
    portENTER_CRITICAL(&s_mux);
    s_pq_r = s_pq_w;
    portEXIT_CRITICAL(&s_mux);
}

void ble_gw_get_status(ble_gw_status_t *out)
{
    if (out == NULL) return;

    portENTER_CRITICAL(&s_mux);
    out->connected = s_connected;
    out->has_rec = s_has_rec;
    out->reconnects = s_reconnects;
    out->rx_total = s_rx_total;
    out->bad_total = s_bad_total;
    out->errors = s_errors;
    
    if (s_has_rec) {
        TickType_t now = xTaskGetTickCount();
        out->last_rec_age_ms = (now - s_last_rx_tick) * portTICK_PERIOD_MS;
    } else {
        out->last_rec_age_ms = 0;
    }

    out->flags = s_last.flags;
    out->dose_msv = s_last.dose_msv;
    out->doserate_usvh = s_last.doserate_usvh;
    out->pulses_2s = s_last.pulses_2s;
    out->cps = s_last.cps;
    out->battery = s_last.battery;
    out->temp_c = s_last.temp_c;
    portEXIT_CRITICAL(&s_mux);
}

// e10: ручное переподключение к AtomFast (аналог кнопки ESPHome «Переподключиться к AtomFast»)
bool ble_gw_reconnect(void)
{
    if (!atomic_load(&s_ready) || s_reconnect_sem == NULL) return false;

    portENTER_CRITICAL(&s_mux);
    bool conn = s_connected;
    uint16_t cid = s_conn_id;
    s_delay_ms = AF_DELAY0_MS;
    s_fail_streak = 0;
    portEXIT_CRITICAL(&s_mux);

    if (conn) {
        // закрытие соединения само запускает штатный цикл переподключения (CLOSE_EVT/DISCONNECT_EVT)
        return bs_local_close(s_gattc_if, cid) == ESP_OK;
    }
    {
        ble_acts_t acts;
        uint32_t now = sel_now_ms();
        portENTER_CRITICAL(&s_mux);
        ble_acts_reset(&acts);
        (void)ble_sel_cmd_reconnect(&s_sel, now, &acts);
        portEXIT_CRITICAL(&s_mux);
        sel_exec(&acts);
    }
    xSemaphoreGive(s_reconnect_sem);
    return true;
}
