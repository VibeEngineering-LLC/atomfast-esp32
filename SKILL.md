---
name: atomfast-esp32
description: >-
  ESPHome BLE-шлюз ESP32 → дозиметр AtomFast Plus2: парсер 13-байт on-air payload,
  Web UI живые графики, вывод в HA/CSV/narodmon без приложения изготовителя. НЕ для Radex/RadonEye.
---

# atomfast-esp32 — BLE-шлюз AtomFast на ESP32

## Когда применять

- «Выводить данные AtomFast в браузер / HA / на ESP», график CPS/dose-rate без приложения.
- Дозиметр AtomFast Plus2 + ESP32-DevKitC под рукой.
- Развёртывание копии прошивки на чужом железе — копировать через скилл, не воспроизводить с нуля.

## ⚠️ SAFETY-CORE (HARD)

1. **Никаких секретов в репо.** Реальный MAC, WiFi-пароль, OTA-пароль, API encryption key —
   ТОЛЬКО через `!secret` + `secrets.example.yaml`. **`.bin`/`.factory.bin` НЕ публиковать**
   (`strings firmware.bin` вытащит все ASCII-секреты). Никаких `firmware/bin/` в публичном репо.
2. **Народмон — HARD HOLD (бан аккаунта оператора).** Switch «Выгружать на Народмон» = **OFF по
   умолчанию** (`restore_mode: ALWAYS_OFF`). НЕ включать switch / кнопку «Послать сейчас» без
   явного снятия бана оператором в чате. Инфраструктура в YAML пассивна — не активировать.
3. **ASCII build-path на Windows.** GCC ломается на кириллице/пробелах в пути. Папка вида
   `D:\Документы\ESP32\` → раскомментировать `esphome.build_path:` с ASCII-путём.
4. **MTU 23 — hardware limit TI CC2541** (чип AtomFast). Не пытаться поднять MTU>23, не запрашивать
   BLE5 (LE 2M/Coded/S2/S8) — на этом чипе невозможно. 13B payload влезает в 20B ATT. Детали и
   бесполезные/полезные стратегии тюнинга — [`references/known-issues.md`](references/known-issues.md#2).

Полный журнал Known issues (0–4: supervision-timeout, gatt_dump count=0, MTU, battery int8, MAC-SSOT,
build-path) — [`references/known-issues.md`](references/known-issues.md).

## Архитектура

```
AtomFast ─BLE notify(13B/2s)─► ESP32 ─Web UI v3─► браузер / HA
                                  │
                                  └─► JSON /sensor/* → Python / CSV
```

- ESP32 — BLE central client, постоянное соединение; `bluetooth_proxy: active` работает параллельно.
- Парсер 13 байт → **6 sensor'ов в классической прошивке** (`atomfast_gateway.yaml`: dose, intensity,
  pulses_2s, cps, battery, temperature) **/ 7 в S3 baseline** (`atomfast_gateway_s3.yaml`: + `doserate_ur` µR/h).
- Web UI v3 — спарклайн + клик → полноразмерный график (история в памяти ESP до reboot).
- ESPHome API (encryption key из secrets) — для Home Assistant.

## Ключевые файлы

| Файл | Назначение |
|---|---|
| `firmware/atomfast_gateway.yaml` | главная прошивка (~746 стр, v0.8.0-step4, esp-idf + bt-proxy base + 13B parser + Народмон 4 опции) |
| `firmware/CHANGELOG.md` | **история версий (semver, обязательно обновлять)** |
| `firmware/include/gatt_dump.h` | helper: ESP-IDF GATT enumerator |
| `firmware/secrets.example.yaml` | шаблон секретов (WiFi, MAC, ключи) |
| `references/atomfast-onair-protocol.md` | реверс 13-байт on-air payload + GATT-карта (11 char) |
| `references/am6er-kbradar-py-connection.md` | параметры подключения (MAC/char/timeout/реконнект) + 2-й источник протокола (Am6er bleak) |
| `README.md` | инструкция для нового пользователя |

**Текущая версия — v0.8.0-step4** (2026-06-14, esp-idf, пересборка от ESPHome bt-proxy baseline;
1-час soak: 0 реассоц/json:111/ребутов, 5 реконнектов/час). Предыдущая стабильная — v0.7.12 (arduino,
`firmware/archive/v0.7.12/`). Полное описание версий + Народмон-выгрузка (R1/D1/T1/BAT1, 5 мин) —
[`references/version-history.md`](references/version-history.md). Альтернативный Web UI v2 (single-table,
custom.css/js) — [`references/alt-firmware-webv2.md`](references/alt-firmware-webv2.md).

## Парсинг 13-байт payload

```
byte 0       — flags (bit 3 = re-read config request)
bytes 1..4   — float32 LE val_dose      (накопленная доза, mSv)
bytes 5..8   — float32 LE val_intensity (мощность дозы, µSv/h, >1000 = bad)
bytes 9..10  — uint16 LE pulses_2s      (импульсы за 2 с)
byte 11      — int8 battery (%)
byte 12      — int8 temperature (°C, signed)
```

Источник — `Am6er/atomswift/AtomDeviceNRF.java::broadcastUpdate(UUID_MEASUREMENT, …)`.
Проверено live на ESP32 (2026-06-12): `00 EE EE E1 3C 07 7A BA 3D 26 00 40 19`
→ доза 27.58 µSv, rate 0.091 µSv/h, 38 имп/2с (19 cps), 64 % батарея, 25 °C.

## Известные значения протокола

| UUID/константа | Значение | Источник |
|---|---|---|
| ATOMTAG_SERVICE | `63462a4a-c28c-4ffd-87a4-2d23a1c72581` | Am6er/atomswift GattAttributes.java |
| ATOMTAG_MEASUREMENT (notify) | `70BC767E-7A1A-4304-81ED-14B9AF54F7BD` | Am6er/atomswift |
| notify cadence | 0.5 Hz (2 с) | AtomFast firmware |
| MTU | 23 (не увеличивается, см. SAFETY-CORE #4) | live ESP32 |
| scan interval/window | 640 / 32 ms (v0.7.12, duty 5 %) | эмпирика (копия Radex v0.3.0-step2) |
| on-air payload size | 13 байт | live ESP32 + Am6er source |

## Что прошивка делает (механика)

1. `esp32_ble_tracker` interval=window=600 ms — почти непрерывный скан (для быстрого reconnect,
   `retry_delay = 600` ms из приложения AtomFast).
2. `ble_client auto_connect: true` → MAC из `!secret atomfast_mac` сразу после boot.
3. Подписка на char `70BC767E-...` сервиса `63462a4a-...`.
4. На каждый notify (~2 с) lambda парсит 13 байт (схема выше) → 6/7 sensor'ов.
5. Web UI v3 рисует sparkline + история в памяти ESP до reboot.
6. Окно агрегации (CPS_avg/dose_avg) — слайдер `number.agg_interval_s` (1–60, init 5 с),
   **только S3 baseline** (`atomfast_gateway_s3.yaml:479-483`).
7. Раз в 5 мин — выгрузка на narodmon.ru через `http_request.get:` (при switch=ON; см. SAFETY-CORE #2).

## Тест-планы (тела — reference)

- **План А** — первый прогон на новом железе (ESPHome install → secrets → COM → run → 7 графиков).
- **План B** — OTA существующей прошивки (`--device atomfast-gw.local`).
- **План C** — адаптация под другой BLE-прибор (GATT-карта → UUID substitutions → парсер lambda).

Полные шаги — [`references/test-plans.md`](references/test-plans.md).

## Contributing

Баги/PR/новые модели (Plus2/Plus3/Mini2/HoneyCounter):
[github.com/VibeEngineering-LLC/atomfast-esp32/issues](https://github.com/VibeEngineering-LLC/atomfast-esp32/issues).
Hard rules PR — см. SAFETY-CORE (никаких реальных секретов/`.bin`; новая версия → запись в CHANGELOG +
копия старой в `firmware/archive/<old-version>/`).

## Reference-файлы

| Файл | Содержимое |
|---|---|
| [`references/atomfast-onair-protocol.md`](references/atomfast-onair-protocol.md) | Реверс 13-байт payload + полная GATT-карта |
| [`references/am6er-kbradar-py-connection.md`](references/am6er-kbradar-py-connection.md) | Параметры подключения + 2-й источник протокола |
| [`references/known-issues.md`](references/known-issues.md) | Known issues 0–4 (полные тела) |
| [`references/test-plans.md`](references/test-plans.md) | Тест-планы А/B/C (полные шаги) |
| [`references/version-history.md`](references/version-history.md) | История версий + Народмон-выгрузка |
| [`references/alt-firmware-webv2.md`](references/alt-firmware-webv2.md) | Альтернативный Web UI v2 (webv2-alt) |
