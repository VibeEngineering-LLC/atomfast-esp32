# atomfast-esp32 — Known issues (полные тела)

Извлечено из `SKILL.md` при lean-рефакторе #CTX-4 (2026-07-11), провенанс verbatim.
Краткие анти-футган-строки (MTU 23, ASCII build-path) продублированы в SKILL.md
(SAFETY-CORE / gotchas) — здесь полные тела.

## Known issues

0. **Редкий supervision-timeout disconnect снижен в ~7× на v0.7.12, не устранён полностью** (open, 2026-06-14).
   На v0.7.12 (scan 640/32ms, duty 5 %) — один `rsn 0x8` за ~15 минут на 30-мин валидации
   (vs `rsn 0x8` каждые ~7 мин на v0.6.x/v0.7.2). Самовосстанавливающийся, реконнект 10-15 с,
   потеря 5-7 notify-сэмплов. Дальнейшее снижение требует либо переписать BLE-стек (вне ESPHome),
   либо принять как baseline для домашнего шлюза. См. v0.7.12 «Известные проблемы» в
   [`firmware/CHANGELOG.md`](../firmware/CHANGELOG.md).

1. **`gatt_dump.h` возвращает count=0 status=1.** ESP-IDF вызов
   `esp_ble_gattc_get_attr_count(gattc_if, conn_id, ESP_GATT_DB_PRIMARY_SERVICE,
   0, 0, ESP_GATT_ILLEGAL_HANDLE, &srv_count)` (`gatt_dump.h:81-83`) для
   подсчёта PRIMARY-сервисов **игнорирует** start/end handles по
   спецификации ESP-IDF — то есть граничный фикс «нужно start=0x0001,
   end=0xFFFF» **мимо**. Корневая причина пока не установлена: возможно
   AtomFast завершает service discovery позже, чем lambda опрашивает таблицу,
   либо сервисы регистрируются через `register_for_notify`, а не как
   primary records у ESP-IDF GATT client. Не блокер — полная GATT-карта
   известна из source-репо изготовителя (Am6er/atomswift, см.
   `references/atomfast-onair-protocol.md`).

2. **MTU 23, не 247 — hardware limit чипа AtomFast (TI CC2541).** AtomFast
   построен на TI CC2541 — single-mode BLE 4.0 SoC (datasheet:
   `1 Mbps, GFSK, 250-kHz deviation`, p.5-6). Это не баг прошивки прибора и
   не ограничение ESPHome `ble_client` — поднять MTU выше 23 на этом чипе
   невозможно. Также не доступны BLE 5 фичи: `LE 2M`, `LE Coded`, `S2`, `S8` —
   не запрашивать. 13-байт payload влезает в 20 B ATT payload (MTU 23), так что
   практически ограничение не мешает.

   Стратегии тюнинга, которые **имеют смысл** на CC2541-периферии:
   - стабильный connection interval (LE 1M PHY)
   - снижение reconnect-storms (см. Issue #0 — supervision-timeout)
   - аккуратный scan duty cycle (`interval_window` баланс, см. v0.7.12)

   Стратегии, которые **бесполезны** (не работают на CC2541):
   - запрос PHY upgrade в `ble_client` (LE 2M / Coded)
   - попытка MTU exchange до 247
   - попытка активировать high-gain RX mode (`-94 dBm` вместо `-88 dBm`) —
     управляется TI vendor-specific HCI командой `HCI_EXT_SetRxGainCmd`,
     которая не доступна через стандартный GATT и не выставлена в shipping
     firmware AtomFast (нет UART/HCI bridge, PTM или custom-команды).

2a. **`battery` int8 → отрицательный % при байте > 127.** Декодер обеих прошивок
   читает byte 11 как `int8_t` (`atomfast_gateway.yaml:502`,
   `atomfast_gateway_s3.yaml:363`). В живом диапазоне заряда 0-100 % это не
   проявляется (старший бит выставлен только при значениях ≥ 128), но если
   прошивка прибора начнёт писать в этот байт другие сигналы — выход уйдёт
   в отрицательную область. Это известный, но не рабочий риск.

3. **MAC AtomFast — один источник истины (`!secret atomfast_mac`)** — c `v0.9.1`
   substitution `atomfast_mac_str` удалена из обоих современных YAML (c3 + s3).
   Фильтр RSSI sensor'а теперь работает через
   `esp32_ble_tracker.on_ble_advertise.mac_address: !secret atomfast_mac`
   (параметр трекера поддерживает `!secret` напрямую). До `v0.9.0` MAC
   приходилось задавать в **двух** местах — substitution и `!secret` — это
   было известное дублирование, теперь убрано. `esp32-classic/atomfast_gateway.yaml`
   (`v0.8.0-step4`) никогда не использовал substitution `atomfast_mac_str`.

4. **ASCII build-path на Windows.** GCC ломается на пути с кириллицей
   или пробелами. Если папка проекта — например
   `D:\Документы\ESP32\` — раскомментировать `esphome.build_path:` и
   указать ASCII-путь.
