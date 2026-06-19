// gatt_dump.h — универсальный дамп GATT-таблицы подключенного BLE-клиента
// через ESP-IDF API. Используется как diagnostic-инструмент для исследования
// неизвестных BLE-устройств. Безопасно вызывать ТОЛЬКО когда клиент в
// состоянии ESTABLISHED (после ESP_GATTC_SEARCH_CMPL_EVT) — это значит
// ESPHome::esp32_ble_client::BLEClientBase::connected() == true.
//
// Возвращает многострочный std::string, формат:
//   SVC 1/3  UUID=180A start=0x0040 end=0x0048 prim=1
//     CHR 2A29 handle=0x0042 props=R           (read)
//     CHR 2A24 handle=0x0044 props=R
//   SVC 2/3  UUID=70BC767E-7A1A-4304-81ED-14B9AF54F7BD ...
//     CHR 70BC767E-... handle=0x... props=NI   (notify + indicate)
//
// + дублирует строки в ESP_LOGI("gatt") для последовательного лога.

#pragma once

#ifdef USE_ESP32

#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include "esphome/core/log.h"
#include "esphome/components/esp32_ble_client/ble_client_base.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"

namespace gatt_dump_util {

static const char *const TAG = "gatt";

inline std::string uuid_to_str(const esp_bt_uuid_t &u) {
  char buf[40];
  if (u.len == ESP_UUID_LEN_16) {
    snprintf(buf, sizeof(buf), "%04X", u.uuid.uuid16);
  } else if (u.len == ESP_UUID_LEN_32) {
    snprintf(buf, sizeof(buf), "%08lX", (unsigned long) u.uuid.uuid32);
  } else if (u.len == ESP_UUID_LEN_128) {
    const uint8_t *b = u.uuid.uuid128;  // LE byte order → отображаем BE
    snprintf(buf, sizeof(buf),
             "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             b[15], b[14], b[13], b[12], b[11], b[10], b[9], b[8],
             b[7], b[6], b[5], b[4], b[3], b[2], b[1], b[0]);
  } else {
    snprintf(buf, sizeof(buf), "len=%u?", u.len);
  }
  return std::string(buf);
}

inline std::string props_to_str(esp_gatt_char_prop_t p) {
  std::string s;
  if (p & ESP_GATT_CHAR_PROP_BIT_BROADCAST) s += 'B';
  if (p & ESP_GATT_CHAR_PROP_BIT_READ)      s += 'R';
  if (p & ESP_GATT_CHAR_PROP_BIT_WRITE_NR)  s += 'w';
  if (p & ESP_GATT_CHAR_PROP_BIT_WRITE)     s += 'W';
  if (p & ESP_GATT_CHAR_PROP_BIT_NOTIFY)    s += 'N';
  if (p & ESP_GATT_CHAR_PROP_BIT_INDICATE)  s += 'I';
  if (p & ESP_GATT_CHAR_PROP_BIT_AUTH)      s += 'A';
  if (p & ESP_GATT_CHAR_PROP_BIT_EXT_PROP)  s += 'X';
  return s;
}

// Безопасный дамп. Возвращает "" если клиент не подключён или GATT нет.
inline std::string dump_gatt(esphome::esp32_ble_client::BLEClientBase *cli) {
  if (cli == nullptr || !cli->connected()) {
    return std::string("[gatt_dump] клиент не в ESTABLISHED — пропуск");
  }
  esp_gatt_if_t gattc_if = (esp_gatt_if_t) cli->get_gattc_if();
  uint16_t conn_id = cli->get_conn_id();

  if (gattc_if == ESP_GATT_IF_NONE) {
    return std::string("[gatt_dump] gattc_if == NONE");
  }

  std::string out;
  out.reserve(2048);

  // Шаг 1: сколько сервисов?
  uint16_t srv_count = 0;
  esp_gatt_status_t st = esp_ble_gattc_get_attr_count(
      gattc_if, conn_id, ESP_GATT_DB_PRIMARY_SERVICE,
      0, 0, ESP_GATT_ILLEGAL_HANDLE, &srv_count);
  if (st != ESP_GATT_OK || srv_count == 0) {
    char tmp[80];
    snprintf(tmp, sizeof(tmp), "[gatt_dump] нет primary services (status=%d, count=%u)",
             st, srv_count);
    return std::string(tmp);
  }

  // Шаг 2: вытащить сервисы (malloc — больших стеков на ESP32 нет)
  esp_gattc_service_elem_t *srv = (esp_gattc_service_elem_t *)
      calloc(srv_count, sizeof(esp_gattc_service_elem_t));
  if (!srv) {
    return std::string("[gatt_dump] OOM service array");
  }
  uint16_t fetched = srv_count;
  st = esp_ble_gattc_get_service(gattc_if, conn_id, nullptr, srv, &fetched, 0);
  if (st != ESP_GATT_OK) {
    free(srv);
    char tmp[80];
    snprintf(tmp, sizeof(tmp), "[gatt_dump] get_service failed status=%d", st);
    return std::string(tmp);
  }

  char hdr[128];
  snprintf(hdr, sizeof(hdr), "[gatt_dump] %u primary services:\n", fetched);
  out += hdr;
  ESP_LOGI(TAG, "===== GATT DUMP: %u primary services =====", fetched);

  // Шаг 3: по каждому сервису — характеристики
  for (uint16_t i = 0; i < fetched; i++) {
    std::string suuid = uuid_to_str(srv[i].uuid);
    char line[200];
    snprintf(line, sizeof(line),
             "SVC %u/%u  UUID=%s  start=0x%04X end=0x%04X prim=%d\n",
             i + 1, fetched, suuid.c_str(),
             srv[i].start_handle, srv[i].end_handle, srv[i].is_primary);
    out += line;
    ESP_LOGI(TAG, "SVC %u/%u UUID=%s handles=0x%04X..0x%04X",
             i + 1, fetched, suuid.c_str(),
             srv[i].start_handle, srv[i].end_handle);

    // Характеристики этого сервиса
    uint16_t ch_count = 0;
    st = esp_ble_gattc_get_attr_count(
        gattc_if, conn_id, ESP_GATT_DB_CHARACTERISTIC,
        srv[i].start_handle, srv[i].end_handle, ESP_GATT_ILLEGAL_HANDLE,
        &ch_count);
    if (st != ESP_GATT_OK || ch_count == 0) {
      out += "  (нет характеристик)\n";
      continue;
    }
    esp_gattc_char_elem_t *chs = (esp_gattc_char_elem_t *)
        calloc(ch_count, sizeof(esp_gattc_char_elem_t));
    if (!chs) {
      out += "  (OOM char array)\n";
      continue;
    }
    uint16_t ch_fetched = ch_count;
    st = esp_ble_gattc_get_all_char(gattc_if, conn_id,
                                    srv[i].start_handle, srv[i].end_handle,
                                    chs, &ch_fetched, 0);
    if (st != ESP_GATT_OK) {
      free(chs);
      snprintf(line, sizeof(line),
               "  (get_all_char failed status=%d)\n", st);
      out += line;
      continue;
    }
    for (uint16_t j = 0; j < ch_fetched; j++) {
      std::string cuuid = uuid_to_str(chs[j].uuid);
      std::string p = props_to_str(chs[j].properties);
      snprintf(line, sizeof(line),
               "  CHR %s  handle=0x%04X  props=%s\n",
               cuuid.c_str(), chs[j].char_handle, p.c_str());
      out += line;
      ESP_LOGI(TAG, "  CHR %s  handle=0x%04X  props=%s",
               cuuid.c_str(), chs[j].char_handle, p.c_str());
    }
    free(chs);
  }
  free(srv);

  ESP_LOGI(TAG, "===== END GATT DUMP =====");
  return out;
}

}  // namespace gatt_dump_util

#endif  // USE_ESP32
