---
name: atomfast-esp32
description: ESPHome-прошивка для ESP32 — BLE-шлюз к дозиметру AtomFast. Разбирает on-air payload, поднимает Web UI с 7 живыми графиками. Использовать когда нужно вывести показания AtomFast в браузер, Home Assistant или CSV без приложения изготовителя.
---

# atomfast-esp32 — BLE-шлюз AtomFast на ESP32

## Когда применять этот скилл

- Оператор просит «выводить данные AtomFast в браузер / HA / на ESP»
- Нужен график CPS / dose-rate в реальном времени без приложения
- Дозиметр AtomFast Plus2 + ESP32-DevKitC под рукой
- Развёртывание новой копии прошивки на чужом железе — копировать
  через этот скилл, не воспроизводить с нуля

## Архитектура

```
AtomFast ─BLE notify(13B/2s)─► ESP32 ─Web UI v3─► браузер / HA
                                  │
                                  └─► JSON /sensor/* → Python / CSV
```

- ESP32 — BLE central client, держит постоянное соединение
- Парсер 13 байт → **6 sensor'ов в классической прошивке** (`atomfast_gateway.yaml`:
  dose, intensity, pulses_2s, cps, battery, temperature) **/ 7 в S3 baseline**
  (`atomfast_gateway_s3.yaml`: + doserate µR/h как отдельный сенсор `doserate_ur`)
- Web UI v3 — спарклайн + клик → полноразмерный график (в памяти ESP)
- ESPHome API (encryption key из secrets) — для Home Assistant

## Ключевые файлы

| Файл | Назначение |
|---|---|
| `firmware/atomfast_gateway.yaml` | главная прошивка ESPHome (~746 строк, v0.8.0-step4, esp-idf + bt-proxy base + 13B parser + Народмон 4 опции) |
| `firmware/CHANGELOG.md` | **история версий прошивки (semver, обязательно обновлять)** |
| `firmware/include/gatt_dump.h` | helper-заголовок: ESP-IDF GATT enumerator |
| `firmware/secrets.example.yaml` | шаблон секретов (WiFi, MAC, ключи) |
| `references/atomfast-onair-protocol.md` | реверс 13-байт on-air payload |
| `references/am6er-kbradar-py-connection.md` | параметры подключения (MAC/char/timeout/реконнект) + 2-й независимый источник протокола (Am6er/AtomFast-KBRadar-py, bleak) |
| `README.md` | человекопонятная инструкция для нового пользователя |

### Текущая версия прошивки

**v0.8.0-step4** (2026-06-14) — **major-минор bump**: `atomfast_gateway.yaml`
пересобран с нуля от официального ESPHome `bluetooth-proxies/esp32-generic`
baseline (esp-idf, SHA-1 `6d0be3a17183d5d43fbcffcd793b6f4facf9e14e`) по образу
успешной пересборки Radex v0.3.0. Поверх baseline'а — наш AtomFast-парсер
13-байтового notify-кадра, диагностика, Народмон-выгрузка (4 опции через
`select`: HTTP GET / HTTP POST / HTTPS POST / JSON POST), Web UI v3
(`log:false` + `auth` + `sorting_groups`).

Параллельно с нашим `ble_client` к AtomFast работает `bluetooth_proxy: active`
— плата одновременно: (а) наш кастомный gateway для AtomFast (13-байтовый
парсер → 6 сенсоров CPS/dose/intensity/batt/T/pulses); (б) HA-нативный
BLE-прокси для всех окружающих BLE-устройств. Путь к единому шлюзу: + Radex
+ RadonEye через тот же bt-proxy + `ble_client` чейн до 3 устройств
(ESPHome лимит).

**Замеры 1-час теста стабильности** (2026-06-14, 13 снапшотов × 5 мин):
0 WiFi-реассоц, 0 json:111, 0 ERROR, 0 ребутов. 5 BLE-реконнектов/час
(~12/2.2 ч, сопоставимо с baseline v0.6.2 «отлично работает»). Heap free
~65 КБ стабильно, min_free_heap 23 480 B (бутовый пик BT+WiFi+web_server init,
по INC-11 — НЕ показатель текущего состояния).

⚠ Народмон-инфраструктура **пассивна** (HARD HOLD по запросу оператора —
бан аккаунта). Switch «Выгружать на Народмон» = OFF по умолчанию. End-to-end
RX 200 от `narodmon.ru/*` НЕ верифицирован в этой сессии — после снятия
запрета: включить switch ON, дёрнуть кнопку «Послать сейчас (Народмон)»,
снять 5-мин snapshot UART с `HTTP %d, body`.

Предыдущая стабильная **v0.7.12** (arduino, scan 640/32ms) — в
[`firmware/archive/v0.7.12/`](firmware/archive/v0.7.12/). Откат: скопировать
`atomfast_gateway.yaml` оттуда поверх главной.

Прошлые версии — [`firmware/archive/`](firmware/archive/). Подробности
изменений и замеров — [`firmware/CHANGELOG.md`](firmware/CHANGELOG.md). Все
будущие правки → отдельная запись в CHANGELOG (semver), копия предыдущей
версии в `firmware/archive/<old-version>/`.

### Альтернативная прошивка — `v0.7.12-webv2-alt`

Параллельная к `v0.7.12` ветка с **`web_server: version: 2`** + кастомные
`www/custom.css` (461 B) и `www/custom.js` (149 строк) поверх gzip-bundle.
Те же BLE-парсер, scan 640/32ms, WiFi-watchdog, Народмон-выгрузка — только Web UI
другой: плотная single-`<table>` раскладка без sorting_groups, ручной порядок
строк через `MutationObserver` (deep shadow-DOM walk), ограничение высоты лога
240 px через JS (CSS shadow-границу не пробивает), `select.template` Народмона
с четырьмя протоколами (**HTTP GET / HTTP POST / HTTPS POST / JSON POST** на
`narodmon.ru/json` с телом `{"devices":[{"mac":"...","sensors":[...]}]}`).

Файлы: [`firmware/archive/v0.7.12-webv2-alt/`](firmware/archive/v0.7.12-webv2-alt/)
(YAML 32 KB + `www/custom.css` + `www/custom.js` + README).

Когда применять:
- Оператор хочет плотную single-table раскладку с ручным контролем порядка строк
  (кнопки рядом со своими блоками — BLE / GATT / Народмон / Система).
- Нужны кастомные UI-надстройки (порядок, лог-высота) — v3-`sorting_groups`
  на них не сводятся.

Подводные камни v2: custom-инклюды раздаются по индексу (`/0.css`, `/0.js`), не
по basename; REST-эндпоинт `/select/<object_id>` слагифицируется из русского
`name:` (для curl задавать `object_id:` явно); после flash оператору
**Ctrl+Shift+R** в браузере. Подробнее — `README.md` в папке версии.

### Народмон-выгрузка (с 2026-06-12)

Прошивка выгружает четыре метрики на narodmon.ru каждые 5 минут:
**R1** (мощность дозы µR/h), **D1** (накопленная доза µSv), **T1** (температура),
**BAT1** (заряд батареи). Имена метрик правятся в Web UI без перепрошивки,
выгрузку можно отключать switch'ем «Выгружать на Народмон».

Авторитетный источник протокола (TCP/UDP/HTTP GET/POST/JSON/MQTT, префиксы,
коды ответа, правила бана) — официальная документация
[narodmon.ru](https://narodmon.ru/client). HTTP-блок в `atomfast_gateway.yaml`
содержит жёстко прошитые URL четырёх транспортов; при изменении API
проверяй разделы `4 - HTTP / 5 - JSON` в документации.

## Что прошивка делает (механика)

1. `esp32_ble_tracker` сканирует с interval=window=600 ms — почти
   непрерывно, для быстрого reconnect (параметр взят из приложения
   AtomFast: `retry_delay = 600` ms).
2. `ble_client` с `auto_connect: true` подключается к MAC из
   `!secret atomfast_mac` сразу после boot.
3. Подписка на characteristic `70BC767E-...` сервиса `63462a4a-...`.
4. На каждый notify (~раз в 2 с) lambda парсит 13 байт по схеме из
   `references/atomfast-onair-protocol.md` и публикует
   **6 sensor'ов в классической / 7 в S3 baseline** (см. секцию «Архитектура»).
5. Web UI v3 рисует sparkline + хранит историю в памяти ESP до reboot.
6. Окно агрегации (CPS_avg, dose_avg) управляется слайдером в UI —
   `number.agg_interval_s` (min 1, max 60, init 5 с). **Только в S3 baseline**
   (`atomfast_gateway_s3.yaml:479-483`); в классической прошивке слайдера нет.
7. Раз в 5 минут — выгрузка четырёх метрик на narodmon.ru через
   `http_request.get:` action (см. секцию «Народмон-выгрузка» выше).

## Известные значения протокола (для повторного использования)

| UUID/константа | Значение | Источник |
|---|---|---|
| ATOMTAG_SERVICE | `63462a4a-c28c-4ffd-87a4-2d23a1c72581` | Am6er/atomswift GattAttributes.java |
| ATOMTAG_MEASUREMENT (notify) | `70BC767E-7A1A-4304-81ED-14B9AF54F7BD` | Am6er/atomswift |
| notify cadence | 0.5 Hz (2 с) | AtomFast firmware |
| MTU | 23 (не увеличивается) | live ESP32 |
| scan interval/window | 640 / 32 ms (v0.7.12, duty 5 %) | Эмпирика: копия Radex v0.3.0-step2; снижение `rsn 0x8` в ~7× — **экстраполяция малой выборки** (2 события за 30 мин на v0.7.12 vs 5 за 10 мин на v0.7.2) |
| on-air payload size | 13 байт | live ESP32 + Am6er source |

Полная GATT-карта (11 characteristic) — см. `references/atomfast-onair-protocol.md`
плюс реверс-источник Am6er/atomswift.

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
Проверено live на ESP32 (2026-06-12): пример `00 EE EE E1 3C 07 7A BA 3D 26 00 40 19`
→ доза 27.58 µSv, rate 0.091 µSv/h, 38 имп/2с (19 cps), 64 % батарея, 25 °C.

## Тест-планы

### План А — Первый прогон на новом железе

1. Установить ESPHome ≥ 2026.5: `pip install esphome`
2. Скопировать `firmware/*` в рабочую папку
3. `cp secrets.example.yaml secrets.yaml`, вписать SSID/пароль/MAC
4. Найти COM-порт ESP32:
   - Windows: `Get-CimInstance Win32_PnPEntity | Where { $_.Name -match 'CH340|CP210|FTDI' }`
   - Linux: `ls /dev/ttyUSB*`
   - ⚠ Иногда `COM5` или другой «занятый» порт уже принадлежит звуковой карте / Bluetooth-стеку / внутреннему модему. Ищи порт с явной меткой `CH340` / `CP210x` / `FTDI` / `USB Serial Device` — это ESP32, остальное — не он.
5. `esphome run --device COM<N> atomfast_gateway.yaml`
6. Открыть `http://atomfast-gw.local/` — должны идти точки на 7 графиков

### План B — OTA-обновление существующей прошивки

1. Найти IP/hostname: `http://atomfast-gw.local/` (уже работает)
2. `esphome run --device atomfast-gw.local atomfast_gateway.yaml` (≈ 1 минута)
3. Если порт занят локальным COM-процессом — закрыть `esphome logs`
   и повторить

### План C — Адаптация под другой BLE-прибор (template)

1. Снять snapshot текущего прибора через приложение изготовителя
2. Если есть открытый исходник приложения — взять GATT-карту оттуда
   (как сделано с Am6er/atomswift для AtomFast)
3. Иначе — поднять `gatt_dump.h` дамп (см. Known issues #1)
4. Заменить service/char UUID в substitutions
5. Переписать парсер lambda в `raw_value_atomfast` под формат прибора
6. Обновить названия sensor'ов под измеряемые величины

## Known issues

0. **Редкий supervision-timeout disconnect снижен в ~7× на v0.7.12, не устранён полностью** (open, 2026-06-14).
   На v0.7.12 (scan 640/32ms, duty 5 %) — один `rsn 0x8` за ~15 минут на 30-мин валидации
   (vs `rsn 0x8` каждые ~7 мин на v0.6.x/v0.7.2). Самовосстанавливающийся, реконнект 10-15 с,
   потеря 5-7 notify-сэмплов. Дальнейшее снижение требует либо переписать BLE-стек (вне ESPHome),
   либо принять как baseline для домашнего шлюза. См. v0.7.12 «Известные проблемы» в
   [`firmware/CHANGELOG.md`](firmware/CHANGELOG.md).

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

## Contributing / issues

Баги, pull requests, новые модели AtomFast (Plus2/Plus3/Mini2/HoneyCounter):
[github.com/VibeEngineering-LLC/atomfast-esp32/issues](https://github.com/VibeEngineering-LLC/atomfast-esp32/issues).

Hard rules для PR: НИ В КОЕМ СЛУЧАЕ не пушить реальный MAC, WiFi-пароли,
OTA-пароль, API encryption key — всё через `!secret` и `secrets.example.yaml`.
Никаких бинарников `.bin` в репо (рисунок секретов из чужой среды). При
добавлении новой версии прошивки — отдельная запись в `firmware/CHANGELOG.md`
(semver), копия предыдущей версии в `firmware/archive/<old-version>/`.
