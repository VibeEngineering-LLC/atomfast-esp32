# Firmware changelog — atomfast-esp32

Версионирование: semver (`vMAJOR.MINOR.PATCH`).
- MAJOR — несовместимое изменение протокола / архитектуры
- MINOR — добавлена функциональность, обратная совместимость не нарушена
- PATCH — bugfix / стабильность / refactor без изменений API

Дата — UTC+3 (Europe/Moscow).
Все версии валидированы на ESP32-DevKitC (WROOM-32, CH340C) с AtomFast Plus2.
Начиная с **v0.9.0-s3** — параллельная ветка baseline на **ESP32-S3-DevKitC-1
N16R8** (esp-idf, отдельный YAML `atomfast_gateway_s3.yaml`).

---

## v0.9.0-s3 (2026-06-17) — S3 N16R8 baseline (esp-idf, отдельный YAML)

**Что:** введён **второй параллельный baseline** для ESP32-S3-DevKitC-1 N16R8
(16 МБ Flash + 8 МБ embedded octal PSRAM, разъём U.FL/IPEX, USB-C) —
[`atomfast_gateway_s3.yaml`](atomfast_gateway_s3.yaml), `fw_version: "v0.9.0-s3"`.
Не заменяет `atomfast_gateway.yaml` (классический DevKitC, arduino) — обе сборки
сосуществуют под разные платформы. Запас heap от 8 МБ PSRAM позволяет включить
`web_server.log: true` без `json:111`-риска.

**Почему S3-ветка:** на классическом DevKitC `esp-idf` ломает coex (INC-09 — `rsn 0x3e`,
INC-12 — `Auth Expired`); arduino-сборка работает, но Espressif постепенно сворачивает
поддержку arduino-framework для ESP32. S3 с octal PSRAM на esp-idf — новый стабильный
target (вывод о развязке coex через S3-чип — **по аналогии с Radex S3**,
длительный приборный прогон AtomFast S3 ещё впереди).

**Изменения от `atomfast_gateway.yaml`:**

1. **Плата**: `esp32: board: esp32-s3-devkitc-1`, `variant: esp32s3`, `flash_mode: dio`.
2. **PSRAM**: `psram: mode: octal, speed: 80MHz`.
3. **Framework**: `esp-idf` (как у классической v0.8.0-step4), но с расширенными
   `sdkconfig_options` под octal PSRAM и WROOM-S3 (CONFIG_SPIRAM_MODE_OCT,
   CONFIG_SPIRAM_SPEED_80M, CONFIG_BT_BLE_50_FEATURES_SUPPORTED).
4. **Имя хоста / mDNS**: `atomfast-gw-s3.local`.
5. **`web_server.log: true`** — разрешено (CLAUDE.md, 2026-06-17). Откат к `false`
   только если появятся ребуты / `json:111` / OOM.
6. **+1 sensor — `doserate_ur`** (µR/h, доза × 100): `val_intensity * 100.0f`.
   Итог: **7 sensor'ов** в `sg_data` (vs 6 в классической).
7. **Слайдер `agg_interval_s`** (min 1, max 60, init 5 с, mode slider).
   В классической прошивке слайдера нет — окно агрегации фиксированное.
8. **Декодер с guard** (`if (std::isfinite(val_intensity) && val_intensity >= 0
   && val_intensity < 1000)`) — то же поведение, что бэкпорчено в классическую
   `atomfast_gateway.yaml` в этой же ревизии.
9. **Народмон D1 = µSv** (а не mSv как в классической v0.8.0-step4). После
   бэкпорта в классическую — обе сборки теперь выгружают `D1` в µSv (×1000 от
   `dose_mSv`). Народмон switch: `restore_mode: ALWAYS_OFF` (HARD, 2026-06-17).
10. **GATT-debug helper (`gatt_dump.h`) исключён** из S3 baseline — оставлен
    только в классической прошивке как RE-инструмент. В S3 baseline нет ни
    кнопки «Перечитать GATT», ни sensor'а с дампом таблицы.

**Stability target:** длительный прогон ≥5 минут уже выполнен на новой плате
S3 N16R8 (базовая валидация), многочасовой прогон ещё впереди.

**Файл:** `firmware/atomfast_gateway_s3.yaml`.

---

## v0.8.0-step4 (2026-06-14) — bt-proxy base + esp-idf + 13-байтовый парсер + Народмон select

**Что:** **major-минор bump** — `atomfast_gateway.yaml` пересобран **с нуля** от
официального ESPHome `bluetooth-proxies/esp32-generic` baseline (esp-idf,
SHA-1 `6d0be3a17183d5d43fbcffcd793b6f4facf9e14e`, branch `main`) по образу
успешной пересборки Radex v0.3.0. Поверх baseline'а — наш AtomFast-парсер
13-байтового notify-кадра, диагностика, Народмон-выгрузка (4 опции через
`select`), Web UI v3 (`log:false` + `auth` + `sorting_groups`). Boot-баннер:
`AtomFast BT-Proxy Base v0.8.0-step4 (esp-idf, bt-proxy + 13B parser + Народмон
4 опции GET/POST/HTTPS/JSON) booted`. **Новая публичная стабильная.**

**Почему:** v0.7.x — arduino-наследие, накопивший слои патчей (web_server v3
заплатки, INC-09 esp-idf rollback, webv2 параллель). Чистая пересборка от
официального bt-proxy эталона:
- избавляет от arduino-долгов (long-reconnect регрессии esp-idf на v0.7.0 не
  воспроизводятся при свежем старте от bt-proxy базы, корень был в
  arduino→esp-idf миграции, не в самом esp-idf);
- даёт **параллельно** bluetooth_proxy для Home Assistant — плата теперь
  одновременно (а) HA-нативный BLE-прокси для окружающих устройств,
  (б) наш кастомный gateway для AtomFast (по-прежнему 13-байтовый парсер);
- путь к единому шлюзу (план: + Radex/RadonEye через тот же bt-proxy + ble_client
  чейн до 3 устройств — см. CLAUDE.md секция «Дорожная карта»).

**Изменения:**

1. **Framework arduino → esp-idf** (`framework: type: esp-idf`,
   `sdkconfig_options: CONFIG_LWIP_MAX_SOCKETS=16, CONFIG_ESP_TASK_WDT_TIMEOUT_S=60`).
2. **bt-proxy слой**: `esp32_ble_tracker: scan_parameters.active: true` +
   `bluetooth_proxy: active: true` — пассивный forward всех окружающих BLE-adv
   в Home Assistant параллельно с нашим ble_client. На AtomFast не влияет
   (наш `ble_client` к `!secret atomfast_mac` — отдельный канал).
3. **AtomFast 13-байтовый парсер** в lambda `raw_value_atomfast`:
   - offset 0: flags (uint8)
   - offset 1-4: float32 LE — `dose_mSv` (накопленная доза, mSv)
   - offset 5-8: float32 LE — `intensity_uSv_h` (мощность дозы, µSv/h)
   - offset 9-10: uint16 LE — `pulses_2s` (импульсы за 2 с; CPS = /2)
   - offset 11: int8 — `battery_pct`
   - offset 12: int8 — `temperature_C`
   - `memcpy(&dose, &x[1], 4)` для float32 LE; bit-shift для uint16 LE.
   - guard `x.size() != 13` → `ESP_LOGW` + `return NAN`.
4. **6 template-сенсоров в группе «Прибор»** (`sg_data`):
   `dose_mSv` (4 знака), `intensity_uSv_h` (4 знака), `cps_sensor` (1 знак),
   `pulses_2s_sensor` (0 знаков), `battery_pct` (%), `temperature_C` (°C).
   Публикуются прямо из парсера через `id(...).publish_state(...)`,
   `update_interval: never`.
5. **Народмон select (4 опции)**: `narodmon_method` template-select с опциями
   `["HTTP GET","HTTP POST","HTTPS POST","JSON POST"]`. Условная диспетчеризация
   в `interval: 300s` (минимум Народмона) по `id(narodmon_method).state`:
   - **HTTP GET** → `narodmon.ru/get`, URL-params `?ID=<MAC>&R1=<r>&T1=<t>...`
   - **HTTP POST** → `narodmon.ru/post`, form-body `ID=<MAC>&R1=<r>&...`
   - **HTTPS POST** → `https://narodmon.ru/post`, form-body через TLS
     (`verify_ssl: false`, mbedTLS heap-heavy)
   - **JSON POST** → `narodmon.ru/json`, body
     `{"devices":[{"mac":"<MAC>","sensors":[{"id":"R1","value":<r>},...]}]}`,
     `Content-Type: application/json`
   - NaN-guard во всех 4-х ветках (`std::isnan(x) ? 0.0f : state`).
   - `capture_response: true` + `on_response: lambda` логирует `HTTP %d, body: %s`.
   - **default — `JSON POST`** (см. `atomfast_gateway.yaml:240`
     `initial_option: "JSON POST"`). Исходный план был `HTTP GET` (наиболее лёгкий
     и стабильный для Народмона); переключение на `JSON POST` зафиксировано
     в YAML на финальной заливке.
6. **Web UI v3 + `log: false` + `auth` + `sorting_groups`**:
   - `log: false` — убирает Debug Log панель → нет SSE-флуда от логов
     (фундаментальное лечение json:111, см. v0.7.5).
   - `web_server.auth` — Basic Auth (`web_username`/`web_password`). Не только
     безопасность — **INC-10 mitigation**: 401-челлендж режет фантомные XRay-keepalive
     ДО открытия /events, устраняет накопление ENFILE/503 от Windows-side
     системного proxy.
   - 2 sorting_groups: `sg_data` («Прибор», sorting_weight 5) и `sg_service`
     («Сервис», 10).
7. **Диагностика в `sg_service`**:
   - sensor: uptime, wifi_signal, free_heap (`esp_get_free_heap_size`),
     min_free_heap (`esp_get_minimum_free_heap_size`), BLE-connects/disconnects/
     notify-count (через `globals`), WiFi-реассоц (через `wifi.on_disconnect`).
   - text_sensor: IP, SSID, BSSID, MAC, версия прошивки.
   - button: safe_mode, factory_reset, restart, «Сбросить счётчики BLE/WiFi»
     (обнуляет `g_connect_count`/`g_disconnect_count`/`g_notify_count`/
     `g_wifi_reassoc_count` + publish_state(0)), «Послать сейчас (Народмон)».
8. **WiFi-watchdog 180 с** (`interval: 10s` + `App.safe_reboot()` — кросс-фреймворк
   замена `ESP.restart()`/`arch_restart()`, перенос из v0.6.2).
9. **`api: reboot_timeout: 0s`** — плата не ребутится при потере HA-связи
   (BLE-сбор и Народмон-выгрузка работают автономно).

**Замеры (1-час тест стабильности 2026-06-14 17:51 → 18:54, T+0...T+60, 13 снапшотов):**

| Метрика | T+0 | T+60 | Delta за 1 ч |
|---|---|---|---|
| Uptime | 841 c | 4561 c | +62 мин ✓ |
| BLE notify | 407 | 2202 | **+1795** (~30/мин = штатные 0.5 Hz) |
| BLE подключений | 2 | 8 | **+6** |
| BLE реконнектов | 2 | 7 | **+5** |
| WiFi реассоциаций | 0 | 0 | **0** ✓ |
| Heap free | 68 412 B | 65 496 B | -2 916 B (стабильно ~65 КБ) |
| Min free heap | 23 480 B | 23 480 B | **0** (исторический минимум держится — пика после T+0 не было) |
| RSSI | -48 dBm | -49 dBm | стабильно |
| json:111 | 0 | 0 | **0** ✓ |
| ERROR | 0 | 0 | **0** ✓ |

5 реконнектов BLE/час ≈ 12/2.2 ч — **сопоставимо с baseline v0.6.2 «отлично
работает» (~2/2.2 ч)** и **существенно лучше тестового BASELINE при плохой связи**
(29 rec/min). 0 WiFi-реассоц, 0 json:111, 0 ERROR, 0 ребутов после initial.
Heap headroom огромный (~65 КБ free), min_free_heap не проседал ниже бутового
23 480 B за весь час. Народмон switch OFF весь тест (HARD HOLD ban соблюдён).

**Известные ограничения:**

1. **Народмон-инфраструктура пассивна.** Switch «Выгружать на Народмон» = OFF по
   умолчанию. Все 4 варианта upload скомпилированы и проверены синтаксически, но
   **end-to-end RX 200 от `narodmon.ru/*`** в текущей сессии не верифицирован —
   стояла HARD HOLD на Народмон по запросу оператора (бан аккаунта). После снятия
   запрета — включить switch ON и снять 5-минутный snapshot UART с `HTTP %d, body`.
2. **BLE rsn 0x8 (supervision timeout)** периодически проявляется (5 за час).
   Самовосстанавливающийся: реконнект 10-15 с, потеря данных 14 с/событие.
   Корень — air-time конкуренция scan/connection-event при активном connect,
   ESPHome BLE-стек не экспонирует connection-params для тонкой настройки.
   На v0.8.0 — заметно реже, чем на v0.6.x/v0.7.2 (~раз в 7-12 мин против раз
   в 4 мин). Принять как baseline для домашнего шлюза.
3. **min_free_heap 23 480 B исторический** — пик при boot-инициализации
   (BT+WiFi+web_server одновременно). НЕ показатель текущего состояния (см. INC-11
   в проектном CLAUDE.md — `min_free_heap` накопительный).

**Откат на v0.7.12 (предыдущая стабильная):** YAML — в
[`firmware/archive/v0.7.12/atomfast_gateway.yaml`](archive/v0.7.12/atomfast_gateway.yaml).

---

## v0.7.12-webv2-alt (2026-06-14) — альтернативная прошивка

**Что:** альтернативная прошивка к `v0.7.12`. Web UI переведён на
`web_server: version: 2` + кастомные `www/custom.css` и `www/custom.js` поверх
gzip-bundle. BLE-парсер, scan-параметры (`640/32ms`, duty 5 %), WiFi-watchdog,
Народмон-выгрузка — идентичны основной `v0.7.12`. Boot-баннер:
`AtomFast Gateway v0.7.12-webv2 (arduino, scan 640/32 ~5% duty, web_server v2 +
UI split + Narodmon select)`.

**Почему:** запрос оператора — «попробуй web_server v2; ограничь высоту лога;
разнеси датчики и сервис в две таблицы; сделай выбор протокола Народмона, JSON
обязательно». Основная `v0.7.12` (web_server v3 + `sorting_groups`) остаётся
эталонной стабильной; webv2 — параллельная ветка для пользователей, предпочитающих
плотную single-table раскладку с ручным контролем порядка строк.

**Изменения:**

1. **web_server v3 → v2** + `include_internal: false` + `css_include: www/custom.css` +
   `js_include: www/custom.js`. ESPHome 2026.5.3 раздаёт custom-инклюды по индексу
   (`/0.css`, `/0.js`), не по basename. Bundle gzip с boilerplate'ом.

2. **`www/custom.js` (4389 B, 149 строк)** — `MutationObserver(debounce 200ms)` +
   `setInterval(tick, 2000)` safety net:
   - `deepQueryAll(selector, root)` рекурсивно обходит `node.shadowRoot` — обязательно
     для `<esp-app>` shadow-host (внешний CSS не проникает в shadow boundary).
   - `TARGET_ORDER` — 31 паттерн в 6 группах: **Показания** (CPS / Доза / Мощность дозы /
     Батарея / Температура / Окно агрегации) → **BLE+кнопки** (связь, реконнекты,
     RSSI, сброс счётчиков, реконнект) → **GATT** (handle-таблица, кнопка перечитать) →
     **WiFi** (сигнал, IP) → **Народмон** (switch, **select протокола**, R1/D1/T1/BAT1,
     «Послать сейчас») → **Система** (MAC, Uptime, «Перезагрузить»).
   - Сортировка `<tbody>` по первому совпавшему паттерну Name-колонки, стабильная на
     равенстве. Кнопки попадают в свои группы по паттернам названия.
   - Лог-ограничение: `esp-log` host получает inline `style.maxHeight = '240px';
     overflowY='auto'; display='block'` — CSS shadow-границу не пробивает, потому JS.

3. **`www/custom.css` (461 B)** — базовый `table { width:100%; border-collapse:collapse }`
   + резервный селектор `#log, .log, esp-log { max-height: 240px; overflow-y: auto }`
   (если esp-log будет HTML-элементом без shadow-DOM).

4. **`select.template narodmon_method`** (icon `mdi:upload-network`, `optimistic: true`,
   `restore_value: true`, `initial_option: "JSON POST"`). Четыре опции:

   | Опция | Endpoint | Content-Type | Тело |
   |---|---|---|---|
   | HTTP GET | `http://narodmon.ru/get` | — | URL-параметры |
   | HTTP POST | `http://narodmon.ru/post` | `application/x-www-form-urlencoded` | form-body |
   | HTTPS POST | `https://narodmon.ru/post` | `application/x-www-form-urlencoded` | form-body через TLS |
   | **JSON POST** | `http://narodmon.ru/json` | `application/json` | `{"devices":[{"mac":"...","sensors":[{"id":"R1","value":...},...]}]}` |

5. Три скрипта `send_narodmon_get|post|json` через `http_request.get|post` action
   (`request_headers:` — переименование ESPHome 2026.5; **`headers:` deprecated**).
   Все ветки с `capture_response: true` + `on_response: lambda` логируют
   `HTTP %d, body: %s`. NaN-guard во всех ветках (`std::isnan(x) ? 0.0f : x`).
   Диспетчер в `interval: 300s` (минимум Народмона — короче ban IP на 1 час) ветвится
   по `id(narodmon_method).state` (или `current_option()` в 2026.7.0+).

**Сборка** (3 итерации до 6-группного TARGET_ORDER):
- compile №1 (full): ~55 с, RAM **22.6 %** / Flash **78.7 %**.
- compile №2-3 (assets rebuild): ~25 с каждый.
- Все три SUCCESS. Один deprecation warning: `select::Select::state` → `current_option()`
  в 2026.7.0+. Не блокер.

**Валидация:**
- UART boot (60 с, `v0712_webv2_uisplit_boot.log`): BLE CONNECTED #4, notify штатно
  (cps ~16-22, batt 57 %, T=24 °C), `json:111 = 0`, ERROR = 0.
- SSE `/events` snapshot 10 576 B: `select` с options
  `["JSON POST","HTTP GET","HTTP POST","HTTPS POST"]`, state `"JSON POST"`; switch
  «Выгружать на Народмон» = OFF (default); 4 text-метрики `R1/D1/T1/BAT1` редактируемы.
- Реальный браузер: порядок строк соответствует TARGET_ORDER, лог справа ограничен
  240 px высотой.
- **1-час тест стабильности** (`v0712_webv2_stability_1h.log`, фоновый UART-захват
  через `scripts/serial_capture.ps1`) — контроль: `json:111 = 0`, `errno 23 = 0`,
  reboot после initial = 0, WiFi handshake fail = 0. Результаты — следующая запись.

**Известные ограничения:**
- Custom-инклюды раздаются по индексу (`/0.css`, `/0.js`), curl на `/custom.css` → 404.
- REST-эндпоинт `/select/<object_id>` слагифицируется из русского `name:` —
  для curl-управления задавать `object_id:` явно.
- 5-мин upload-тест на `narodmon.ru/json` синтаксически валиден (compile clean), но
  end-to-end RX `errno=200` от сервера в этой версии не зафиксирован UART-логом
  (фаза перешла к 1-час стабильности). Будет докручен в следующей публикации.
- После перепрошивки оператору **Ctrl+Shift+R** в браузере, иначе кэшированный
  `/0.js` использует устаревший TARGET_ORDER.

**Соседство с основной `v0.7.12`:** обе версии параллельны. Эталонная стабильная —
[`atomfast_gateway.yaml`](atomfast_gateway.yaml) (web_server v3 + `sorting_groups` +
`log:false`). Альтернатива (webv2) — в [`archive/v0.7.12-webv2-alt/`](archive/v0.7.12-webv2-alt/).

---

## v0.7.12 (2026-06-14)

**Что:** BLE-скан `interval/window` `600/180ms → 640/32ms` (duty 30 % → 5 %) — копия проверенной стабильной конфигурации Radex `v0.3.0-step2`. Это первая публикуемая версия из ветки `v0.7.3..v0.7.12` (промежуточные — pre-release / диагностические, см. ниже).

**Почему:** baseline v0.7.2 (arduino 600/180, duty 30 %) держал стабильный коннект, но супервизионный таймаут `rsn 0x8` повторялся раз в ~7 мин (#82/#98 «known issue»). Параллельно я давил флуд `json:111` в Web UI на Radex (та же кодовая база web_server v3) — и нашёл, что Radex `v0.3.0-step2` живёт месяцами без `rsn 0x8` именно благодаря узкому окну скана. Сравнение YAML AtomFast vs Radex показало, что единственное физическое отличие — scan duty (30 % vs 5 %). Гипотеза: узкое окно 32 ms почти не «зажимает» air time, контроллеру AtomFast легче попадать в supervision-окно ESP. Перенёс scan-параметры Radex'а как есть.

**Изменения:**
- `esp32_ble_tracker.scan_parameters`: `interval 600ms → 640ms`, `window 180ms → 32ms` (duty 30 % → 5 %).
- Boot-баннер: `v0.7.2` → `v0.7.12 (arduino, scan 640/32 ~5%% duty)`.
- Всё остальное (framework arduino, WiFi-watchdog, web_server v3 с `log:false`, BLE-парсер, Народмон-выгрузка, sorting_groups) — без изменений.

**Замеры (UART COM8, валидация 30 мин):**
- **`rsn 0x8` supervision-timeout: 2 / 30 мин** (= ~1 за 15 мин). Было 5/10 мин на v0.7.2 ≈ 15/30 мин экстраполированно — **снижено в ~7 раз**.
- **`rsn 0x3e` failed-establishment: 1 / 30 мин** (одиночное, без кластера). Промежуточная v0.7.0 esp-idf давала кластеры 7 за 60 с — на arduino+узкое окно кластер не возникает.
- **Самая длинная подтверждённая непрерывная сессия: 2282 с ≈ 38 мин** (была закрыта в окне захвата, началась до его старта). Notify шёл штатно всё это время.
- **Notify density: 873 строки / 1800 с ≈ 0.485 Hz** (97 % от теоретического 0.5 Hz, против ~80 % на baseline).
- `0` ребутов после initial, `json:111 = 0`, `ERROR = 0`. WiFi-watchdog — только на буте (норма).

**Trade-off:** реконнект после `rsn 0x8` стал чуть медленнее (~10-15 s вместо 6 s на v0.7.2), потому что 5 %-duty скан реже «видит» advertising-пакеты при cold-reconnect. На длинной дистанции это всё равно лучше: 1 disconnect / 15 мин с 15-s recovery vs 1 disconnect / 7 мин с 6-s recovery.

**Известные проблемы:**
- `rsn 0x8` не устранён полностью — остался единичный таймаут раз в ~15 мин. Самовосстанавливающийся. Дальнейшее снижение требует либо переписать BLE-стек (вне ESPHome), либо принять как baseline для домашнего шлюза.

---

### Pre-release: `v0.7.3..v0.7.11` (диагностические, в публичный репо не выгружались)

Промежуточные версии, через которые шла трасса от `v0.7.2` к `v0.7.12`. Каждая решала одну побочную проблему, накопленную поверх рабочего baseline. Хранятся только в проектной папке оператора (`firmware/atomfast/_versions/`).

| Версия | Что изменилось | Зачем |
|---|---|---|
| `v0.7.3` (внутренняя) | — | пропуск номера |
| `v0.7.4` (внутренняя) | — | пропуск номера |
| `v0.7.5` | `web_server.log: false` | Debug Log панель в Web UI v3 подписана на `/events` SSE и сериализует КАЖДОЕ лог-сообщение как отдельный JSON-event. С `interval:5s` heap-логом + WiFi-флапами поток `≥10` event/с → SSE-буфер не дренируется → ArduinoJSON document overflow → флуд `json:111`. Однострочный фикс. Гипотеза оператора, доказана: после `log:false` под F5×7 + параллельный API log-forwarding к HA `json:111 = 0`. |
| `v0.7.6` | `wifi.fast_connect: true` + bssid-pin | Попытка ускорить реконнект к домашнему меш-Wi-Fi. Обернулось каскадом `rsn 0x8` каждые 8 с (#73) — fast_connect конфликтует с band-steering меша. |
| `v0.7.7` | Снять bssid-pin, оставить простой STA | Откат `v0.7.6`: на длинном прогоне пин на конкретный AP получал `Not Authenticated ×6 / Handshake Failed ×1` → пин не лечит проблему меша, только ограничивает роуминг. Возврат к свободному роумингу. |
| `v0.7.8` | Переключение на альтернативную SSID того же роутера | Диагностика: изолировать проблему меш-сети — переключиться на отдельную одноточечную SSID. Подтверждено, что проблема в band-steering меш-точек, не в WiFi-стеке ESP. |
| `v0.7.9` | (внутренняя) | Попытка `-DCONFIG_LWIP_MAX_SOCKETS=16` через build_flag → конфликт `redefined` с auto-calc ESPHome 2026.5.3 + `-Werror` wpa_supplicant. Откатил. |
| `v0.7.10` | Переключение на сервисную SSID того же роутера | Та же диагностика что `v0.7.8` — отдельная SSID, чтобы понять, что мешу мешает. |
| `v0.7.11` | Возврат к рабочей SSID, `power_save_mode: none` | Стабилизирующий промежуточный baseline для замера supervision-timeout перед `v0.7.12`. На нём набралась статистика 5× `rsn 0x8` за 10 мин — оттолкнулся к scan-tuning'у. |

Полная история диагностики этой ветки — в [CLAUDE.md проекта ESP32](../../README.md) (раздел «ЖУРНАЛ ИНЦИДЕНТОВ»), здесь не дублируется.

---

## v0.7.2 (2026-06-13)

**Что:** ОТКАТ фреймворка `esp-idf → arduino`. BLE-скан остаётся `600/180ms` (duty 30 %).

**Почему:** миграция на esp-idf (v0.7.0) дала **регрессию реконнекта** — жалоба оператора «долго/не с первой попытки коннектится» (вторая жалоба подряд на деградацию AtomFast). UART-подтверждение v0.7.1 (esp-idf 600/180): шумный bootstrap — `2× rsn 0x3e` (connection establishment failure) + коннект с многих попыток (CONNECTED #7, не #1) + реконнект ~13 s. Общий знаменатель обеих жалоб (v0.7.0 320/300 и v0.7.1 600/180) — именно фреймворк esp-idf. Arduino v0.6.2 (600/180) — единственная конфигурация, которую оператор оценил «отлично работает». Миграция на esp-idf была без жёсткой необходимости (esp-idf нужен был для Radex-истории, которую вычеркнули; AtomFast = passive notify, его не требует) → откат к проверенному baseline. v0.7.1 (промежуточный esp-idf scan-rollback) в публичный репо не публиковалась.

**Изменения:**
- `esp32.framework.type`: `esp-idf → arduino`.
- `esp32_ble_tracker.scan_parameters`: остаётся `interval 600ms / window 180ms` (duty 30 %, baseline «отлично работает»).
- `arch_restart()` и `wifi::global_wifi_component->is_connected()` — кросс-фреймворковые примитивы, компилируются на arduino без изменений лямбд (изменений кода для отката не потребовалось).
- Добавлен `else`-бранч в lambda сессии: если `on_connect` не отработал (handshake не завершился до развала коннекта) — `g_last_session_s = 0`, не залипаем на прошлом значении.
- Boot-баннер: `v0.7.0 (esp-idf)` → `v0.7.2 (arduino rollback, scan 600/180)`.

**Замеры (UART COM8, валидация ~40 мин, аптайм с буста):**
- **Bootstrap: CONNECTED #1 с первой попытки за ~21 s, `0× rsn 0x3e`** — жалоба устранена (было: #7 + 2× rsn 0x3e на esp-idf).
- **1 disconnect за 40 мин** (`rsn 0x8` supervision timeout — известный #82/#98, зависит от качества BLE-сигнала, не регрессия отката), реконнект **6 s** (было 13 s на v0.7.1).
- `0` ребутов после initial, `json:111 = 0`, `ERROR = 0`. WiFi-watchdog — только на буте (норма).
- 1 disconnect/40 мин ≈ 3/2.2 ч — сопоставимо с baseline v0.6.2 «отлично работает» (~2/2.2 ч).

**Известные проблемы:**
- `rsn 0x8` (supervision timeout) раз в ~7 мин — самовосстанавливающийся (реконнект 6 s), отдельный таск #98. Не блокирует стабильность.

---

## v0.7.0 (2026-06-13)

**Что:** миграция фреймворка `arduino → esp-idf` + уплотнение BLE-скана `600/180ms → 320/300ms`.

**Почему:** дорожная карта единого шлюза требует esp-idf (надёжнее BLE-coex, нет лимита `task_wdt` 5 с, устойчивее lambda) — это подтверждено baseline'ом `esphome/bluetooth-proxies` (esp-idf + high-duty scan стабилен). Заодно проверяется гипотеза, что плотный scan на esp-idf-стеке попутно лечит supervision-timeout `rsn 0x08` (#82/#98): чаще «накрываем» прибор → меньше пропусков supervision-окна. Выяснилось также, что arduino был не осознанным выбором, а историческим дефолтом ESPHome для ESP32, унаследованным через boilerplate — жёсткой зависимости не было (см. ниже «одна строка»).

**Изменения:**
- `esp32.framework.type`: `arduino → esp-idf`.
- `esp32_ble_tracker.scan_parameters`: `interval 600ms → 320ms`, `window 180ms → 300ms` (duty ~94%). Arduino-эпохи ограничение duty ≤35% снято esp-idf-стеком. `320` не делитель `1000` → фаза скана совпадает с 1-секундным тиком процессора AtomFast лишь раз в 8 с (нет фазового замка).
- `ESP.restart()` → `arch_restart()` в WiFi-watchdog — **единственная** правка кода, потребовавшаяся для смены фреймворка (`ESP.restart()` — arduino-only; `arch_restart()` — кросс-фреймворковый примитив ESPHome, на esp-idf вызывает `esp_restart()`). Доказательство, что arduino был унаследованным дефолтом, а не требованием. `millis()` и `wifi::global_wifi_component->is_connected()` работают на обоих фреймворках — не трогались.
- Boot-баннер: `v0.6.2` → `v0.7.0 (esp-idf)`.

**Замеры (UART COM8, валидация ~8.5 мин, аптайм с буста):**

| Метрика | v0.6.2 (arduino, 600/180) | v0.7.0 (esp-idf, 320/300) |
|---|---|---|
| BLE реконнектов | ~2 за прогон | **0 за 8.5 мин** (1 CONNECT, 0 DISCONNECT) |
| `rsn 0x08` (supervision-timeout) | раз в ~4 мин (#98) | **0** (ожидали ~2) |
| WiFi-watchdog (после буста) | 0 | **0** (только бутовая 10s-метка) |
| `json:111` overflow | 0 | **0** |
| Reboot после initial | 0 | **0** |
| RAM (compile) | 22.6% | **21.9%** |
| Flash (compile) | 78.6% | **77.6%** |
| Compile time | ~90 с | 191 с (первая esp-idf сборка) |

**Замечание про esp-idf vs arduino:** esp-idf-бинарь чуть легче по RAM/Flash; время первой сборки выше (toolchain), повторные — быстрее за счёт кэша.

**Известные проблемы:**
- `rsn 0x08` (#98) в окне валидации не воспроизвёлся, но окно короткое — для уверенного закрытия нужен длинный (несколько часов) прогон. Гипотеза «esp-idf попутно лечит #98» пока подтверждается, не закрыта.

**Архив**: предыдущая стабильная прошивка — [`archive/v0.6.2/atomfast_gateway.yaml`](archive/v0.6.2/atomfast_gateway.yaml).

---

## v0.6.2 (2026-06-13)

**Что:** устранён «тихий» отказ WiFi (BLE жив, notify идёт, но WiFi мёртв — ни ping, ни HTTP, без reboot-loop). Два изменения: (1) scan-параметры `1100/350ms` → `600/180ms` для устранения резонанса с adv-периодом AtomFast; (2) добавлен WiFi-watchdog с автономным `ESP.restart()`.

**Почему (INC-01):** при `api: reboot_timeout: 0s` (отключённое авто-восстановление по API) ESP мог зависнуть в состоянии «WiFi-стек повис, BLE работает» без единого reboot — Web UI и Народмон-выгрузка молча отваливались, прибор внешне выглядел живым. Корневой триггер — резонанс окна скана с рекламным интервалом прибора: `scan interval 1100ms` почти совпадал с `BLE_DEFAULT_ADV_INTERVAL_FAST ≈ 1000ms` AtomFast → фазовое биение, периодические провалы air-time, голодание WiFi-стека на одном радио.

**Изменения:**
- `esp32_ble_tracker.scan_parameters`: `interval: 1100ms → 600ms`, `window: 350ms → 180ms`. Новый `interval` заметно короче adv-периода (~1000ms) → нет фазовой синхронизации; duty 30% (было 32%), ~420ms свободно на connection-events.
- Новый `interval: 10s` lambda-watchdog: накапливает секунды без WiFi через `wifi::global_wifi_component->is_connected()`; при ≥180s простоя — `ESP.restart()`. Полностью автономно, не зависит от native_api.
- Boot-баннер обновлён до `v0.6.2`.

**Важно (урок для esp32-dev):** в lambda этой сборки Arduino-глобали `WiFi` / `WL_CONNECTED` **не объявлены** (нет авто-include `WiFi.h`) — компиляция падает с `'WiFi' was not declared in this scope`. Состояние WiFi надо читать нативным API ESPHome: `wifi::global_wifi_component->is_connected()`.

**Замеры (live HTTP, аптайм ≈16 мин на ESP32-DevKitC):**

| Метрика | v0.6.1 | v0.6.2 |
|---|---|---|
| «Тихий» отказ WiFi | случался (INC-01) | **0** (watchdog + scan-fix) |
| WiFi-watchdog срабатываний (после буста) | — | **0** (только на старте, 10s-метка) |
| HTTP-отклик `/` | — | **200 за 0.26 s** |
| BLE реконнектов за 16 мин | — | **1** (последняя сессия 704 s чистых) |
| `json:111` overflow | 0 | **0** |
| Reboot-loop | 0 | **0** |
| scan duty | 32% (1100/350) | 30% (600/180) |

**Архив**: предыдущая стабильная прошивка — [`archive/v0.6.1/atomfast_gateway.yaml`](archive/v0.6.1/atomfast_gateway.yaml).

**Известные проблемы (перенесены, не регрессия v0.6.2):**
- **Supervision timeout `rsn 0x8`** (`BT_HCI: hcif disc complete: rsn 0x8`, `BLE_HCI_CONNECTION_TIMEOUT`) сохраняется — редкий BLE-дисконнект с чистым авто-реконнектом ~14 с. Это конкуренция air-time между окнами скана и connection-events, цель отдельной работы (v0.6.3). v0.6.2 на него не нацеливался.

---

## v0.6.1 (2026-06-13)

**Что:** убран паразитный READ на handle 0x25 — warning `[W][ble_sensor:095]: Error reading char at handle 37, status=2`, который шёл ровно раз в минуту, больше не появляется.

**Почему:** ESPHome `sensor: platform: ble_client` с `type: characteristic` имеет дефолтный `update_interval: 60s`. Даже при `notify: true` он параллельно дёргает ATT READ на ту же характеристику. AtomFast разрешает только NOTIFY, READ возвращает `status=2` (`READ_NOT_PERMITTED`). Сам по себе warning безвреден, но засорял лог и потенциально провоцировал лишние BLE-операции под нагрузкой scan.

**Изменения:**
- В блоке `sensor: - platform: ble_client raw_value_atomfast`: добавлен `update_interval: never`. NOTIFY-подписка не зависит от sensor update_interval и работает по-прежнему.
- Комментарий-якорь рядом со строкой объясняет причину.

**Замеры (330 с прогон на ESP32-DevKitC):**

| Метрика | До (v0.6.0) | После (v0.6.1) |
|---|---|---|
| `[W][ble_sensor:095]` warning | 1/мин (≈5 за 5 мин) | **0** |
| Disconnect'ов за 5 мин | 1-2 (старт) | 1 (старт) |
| Notify-пакетов | стабильно 0.5 Hz | стабильно 0.5 Hz |

**Архив**: предыдущая стабильная прошивка лежит в [`archive/v0.6.0/atomfast_gateway.yaml`](archive/v0.6.0/atomfast_gateway.yaml).

**Известные проблемы:**
- **Редкий supervision timeout не устранён полностью** (2026-06-13 13:48 MSK):
  AtomFast DISCONNECT #136 после сессии длиной **2057 с (≈34 мин)** на v0.6.1.
  ESP-IDF: `BT_HCI: hcif disc complete: hdl 0x0, rsn 0x8 dev_find 1` →
  reason `0x08 = BLE_HCI_CONNECTION_TIMEOUT` (тот же класс, что давили в v0.6.0).
  v0.6.0 снизил частоту с ~1/7 с до ~1/34 мин — на порядки лучше, но потолок
  не достигнут. Reconnect отработал штатно: повторный connect через 8 с
  (`Connection open` 13:48:43, `Service discovery complete` 13:48:45,
  `AtomFast CONNECTED #133` 13:48:46), notify-поток восстановился сразу.
  Гипотезы для расследования: (1) WiFi RF interference с BLE — особенно
  если совпало с 5-мин Народмон-uploadом или большим HA-pollом;
  (2) AtomFast ушёл в power-save и не ответил на keep-alive (off-our-side);
  (3) недолёт по RSSI + случайное препятствие. Для диагностики следующего
  disconnect — раскомментировать `logs: ble_client_base: DEBUG` и поймать
  UART-лог. См. задача #82.
- **Расчёт timing vs Народмон-uploadа (post-mortem, 2026-06-13 13:57 MSK).**
  Boot ESP'а: 13:14:08 (по uptime=2581 с на момент проверки).
  Disconnect: 13:48:35 → offset boot+2067 с. Народмон upload идёт по
  `interval: 300s` от boot → ближайший upload-момент — boot+2100 с
  (13:49:08), на **33 с ПОЗЖЕ** disc'а. Disc не во время самого
  HTTP-uploadа. Предыдущие 6 upload'ов за сессию (boot+290, 590, ...,
  1790) прошли без disc. Гипотеза «WiFi RF starvation BLE во время
  http_request» прямой синхронности не показывает — ослабляется.
  Сильнее становятся гипотезы (2) AtomFast power-save и (3) RSSI/радио.
- **Повторное наблюдение: DC #137 в 14:42:36 MSK** (uptime ≈ 5345 c, boot ≈ 13:13:31).
  Длина закрытой сессии — **3230 с ≈ 53.8 мин** (читается из UART:
  `atomfast:216  AtomFast DISCONNECT #137, последняя сессия 3230 s — пробую переподключиться`).
  reason снова `0x08` (UART строка ESP-IDF:
  `BT_HCI: hcif disc complete: hdl 0x0, rsn 0x8 dev_find 1`).
  Reconnect автоматический: `Connection open` 14:42:56 → `Service discovery complete`
  14:42:58 → `AtomFast CONNECTED #134` 14:42:58. Полный простой ≈ 22 с.
  **Итог за сессию работы 2026-06-13 (boot 13:13:31 ... 14:42:36 = 89 мин):**
  2 disconnect'а (#136 после 34 мин, #137 после 54 мин), оба reason 0x08,
  оба восстановились за ≤ 22 с. Среднее MTBF за день — **≈ 44 мин**.
  Для сравнения v0.5.x baseline (90 с прогона) был MTBF ≈ 7 с —
  v0.6.0/v0.6.1 на **2-3 порядка** лучше; supervision timeout стал
  редким событием, ловить его сложнее.
- **Пропуски точек в Web UI sparkline** (2026-06-13, скриншот оператора 14:02-14:28
  µSv/h). На графике µSv/h видны тонкие «затяжки» (короткие горизонтальные
  отрезки) и редкие нулевые пропуски в 14:08, 14:11, 14:15, 14:23, 14:27.
  **DC #136/#137 НЕ в этом окне** (#136 был 13:48, #137 — 14:42), значит
  это не результат disconnect'а. Гипотезы:
  (а) web_server v3 sparkline отрисовывает «N последних публикаций» с
  глобальной нормализацией оси — равные подряд значения дают плоский
  отрезок (не пропуск);
  (б) JSON-serialization SSE с кириллическими entity-name'ами тяжёлая,
  под нагрузкой HA-polling'а или одновременных REST-вызовов исходящий
  notify pacing страдает (пропуск ≈ 1 точка);
  (в) сам прибор AtomFast может пропускать notify под BLE air-time
  prep'ом 1× в 30-60 с.
  План: померять offline (без HA + без REST polling) в течение 30 мин,
  посчитать число notify'ов и сравнить с ожидаемыми 30*60/2 = 900.
- **Deprecated URL warning** (открыто 2026-06-13).
  `[W][web_server:191]: Deprecated URL format: /sensor/uptime - use entity name '/sensor/Uptime' instead. Object ID URLs will be removed in 2026.7.0.`
  Триггер — мой Python REST polling из `.esp32_capture` скриптов использует
  lowercase object_id `/sensor/uptime`. ESPHome 2026.7.0 удалит эту форму,
  оставит только entity-name `/sensor/Uptime`. Перед апгрейдом — пройтись
  по всем helper-скриптам и REST-вызовам, заменить object_id на entity-name
  (см. ID-таблицу в README): `/sensor/Uptime`, `/sensor/BLE: реконнектов всего`
  (с URL-encoding пробелов и кириллицы), и т.д. Альтернатива — явно задать
  `id:` всем sensor'ам в YAML (тогда object_id остаётся стабильным).
- **РЕЗКАЯ ДЕГРАДАЦИЯ MTBF после 14:50 MSK** (2026-06-13, замер 14:47-14:54 на COM10).
  За **11 минут** наблюдалось **4 disconnect'a** подряд (UART-лог
  `atomfast_uart_20260613_144709.log`):
  - DC #137 — 14:42:36, sess **3230 s** (≈54 мин), reason `0x08`
  - DC #138 — ≈14:48 (между захватами, не зафиксирован построчно)
  - DC #139 — 14:50:54, sess **299 s** (≈5 мин), reason `0x08`
  - DC #140 — 14:53:25, sess **98 s** (1.6 мин), reason `0x08`
  Перед каждым успешным re-connect — серия `BT_HCI: hcif disc complete
  rsn 0x3e` (`BLE_HCI_CONN_FAILED_TO_BE_ESTABLISHED`) на 2-4 повторе.
  Reconnect-окно растёт: 22 с (DC#137) → 52 с (DC#139) → 21 с (DC#140).
  **MTBF упал**: 53.8 мин → 5 мин → 1.6 мин. Это **не** случайный шум —
  паттерн ускорения, минимум 2 порядка к утренней статистике.
  Гипотезы (приоритет ↓):
  (1) **Окружающая RF-обстановка**: к Wi-Fi/BT эфиру добавилось внешнее
  устройство (соседская сеть, чей-то BLE-prox, microwave), забирающее
  air-time и зажимающее scan-окно ESP32 → connection events не
  доезжают, supervision timeout 4 с истекает.
  (2) **AtomFast battery / power-save**: при низком заряде прибор
  переходит в агрессивный sleep, не отвечает на keep-alive. Если
  скорость деградации сохранится — кандидат №1, проверять напряжением.
  (3) **Перегрев ESP32-DevKitC**: длительная сессия + летняя комната,
  RF-фронт начинает шуметь. Проверять `temperature_internal` sensor
  (если есть в этой версии прошивки).
  **План**: оставить UART-захват активным; если за следующий час
  MTBF не вернётся к ≥10 мин — поднимать `logger.level: VERBOSE` для
  `ble_client_base` (через OTA) и ловить timeline `connection_event` /
  `scan_resume`. Параллельно проверить `wifi_signal_db` на тренд
  падения (Wi-Fi-проблема выдаст себя там раньше BLE).
- остальное — см. v0.6.0.

---

## v0.6.0 (2026-06-12)

**Что:** стабильность BLE-соединения — устранены частые supervision-timeout disconnect'ы.

**Почему:** baseline-замер (90 секунд) показал 2 disconnect'а с `reason 0x08`
(`BLE_HCI_CONNECTION_TIMEOUT`), сессии по 7 секунд. Корень — scan_parameters
`interval=window=600 ms` забирали 100 % радио-времени, connection-event'ам
уже подключённого клиента не хватало слота → центральный stack рвал коннект
по supervision timeout (4 с дефолт).

**Изменения:**
- `esp32_ble_tracker.scan_parameters`: `interval: 600 ms / window: 600 ms / active: true`
  → `interval: 1100 ms / window: 350 ms / active: false`. Duty cycle 100 % → 32 %.
  Passive scan: имя + RSSI приходят сразу в advertise payload, SCAN_REQ не нужен.
- `logger.level: DEBUG → INFO`. UART blocking writes на 115200 baud-rate
  занимали 50-200 мс на DEBUG-flood, что само провоцировало supervision timeout.
- Новые globals: `g_disconnects`, `g_connects` (uint32_t, `restore_value: true`),
  `g_last_connect_ms`, `g_last_session_s` (transient).
- `ble_client.on_connect`/`on_disconnect` инкрементируют счётчики и логируют
  длительность только что закрытой сессии.
- 3 новых sensor'а в группе «Диагностика» Web UI: «BLE: реконнектов всего»,
  «BLE: успешных коннектов», «BLE: длина последней сессии».
- Кнопка «Сбросить счётчики BLE» в той же группе.
- `interval: 10s` публикует счётчики на UI.

**Замеры (90-120 секунд прогона на одном железе):**

| Метрика | До (v0.5.0) | После (v0.6.0) |
|---|---|---|
| Disconnect | 2 / 90 с | 0 / 87 с (в новой сессии) |
| Длина первой сессии | 7 с | 87 с (и продолжается) |
| Notify rate | 0.41 Hz (с пробоями) | 0.50 Hz (стабильно) |
| Reason 0x08 (CONN_TIMEOUT) | да | не появляется |
| Средний интервал notify | — | 2.03 с (ожидаемые 2.0 с) |

**Известные проблемы:**
- `Error reading char at handle 37, status=2` — единичная попытка ESPHome
  ble_sensor читать характеристику autonomously. На notify-поток не влияет,
  но шумит в логе. Будет вынесено в отдельный fix.
- При INFO logger строка `ESP_GATTC_DISCONNECT_EVT, reason 0xXX` (из ble_client_base)
  не печатается. Для диагностики reason — раскомментировать `logs:
  ble_client_base: DEBUG` в `logger:` секции.

---

## v0.5.0 (2026-06-12)

**Что:** выгрузка показаний на narodmon.ru (4 метрики, раз в 5 минут).

**Почему:** опубликовать дозу/мощность дозы/температуру/батарею на публичную
карту Народмона. Должно работать без приложения, чисто через ESPHome.

**Изменения:**
- `http_request` компонент (`useragent: "esphome-atomfast-gw/1.0"`, `verify_ssl: false`).
- Группа `sg_narodmon` в Web UI (weight 30) с UX:
  - Switch «Выгружать на Народмон» (`RESTORE_DEFAULT_ON`, `optimistic: true`).
  - 4× `text:` поля для редактирования имён метрик (R1/D1/T1/BAT1) — префикс
    задаёт тип величины на narodmon.ru. Сохраняются в NVS.
- `interval: 300s` шлёт `GET http://narodmon.ru/get?ID=<MAC>&R1=...&D1=...&T1=...&BAT1=...`,
  только при `switch ON ∧ wifi.connected ∧ !isnan(doserate_ur)`.
- `on_response` парсит ответ (`OK` / `INTERVAL` / `BANNED` / `#cmd;cmd`).

**Замеры:** первая успешная выгрузка — HTTP 200, body содержит `OK <ts>`.

**Известные проблемы:**
- Парсер ответа пока только логирует — `#cmd;cmd` (devctrl) не исполняется.

---

## v0.4.0 (2026-06-12)

**Что:** реверс 13-байт on-air payload AtomFast + 7 instant sensor'ов.

**Почему:** до этого парсер использовал архивный формат (28 байт), на on-air
notify прибор шлёт другой формат. Все sensor'ы публиковали NaN/мусор.

**Изменения:**
- Парсер 13 байт по схеме из `Am6er/atomswift` (open-source клиента AtomFast):
  ```
  byte 0       — flags (bit 3 = re-read config request)
  bytes 1..4   — float32 LE val_dose (накопленная доза, mSv)
  bytes 5..8   — float32 LE val_intensity (мощность дозы, µSv/h, >1000 = bad)
  bytes 9..10  — uint16 LE pulses_2s
  byte 11      — int8 battery (%)
  byte 12      — int8 temperature (°C)
  ```
- 7 instant sensor'ов: CPS, dose-rate (µSv/h и µR/h), импульсы, накопленная доза,
  батарея, температура.
- Web UI v3 группы: `sg_main` (4 sensor'а на главной), `sg_diag`, `sg_re`, `sg_settings`.
- Слайдер «Интервал агрегации» (1..60 с) — пересчёт CPS_avg / dose_avg.

**Замеры:** пример пакета `00 EE EE E1 3C 07 7A BA 3D 26 00 40 19` →
`dose=27.58 µSv, rate=0.091 µSv/h, p2s=38 (cps=19), batt=64%, T=25°C`.

---

## v0.3.0 (2026-06-11)

**Что:** GATT-карта подтверждена, service UUID найден.

**Изменения:**
- `substitutions.atomfast_service_uuid: 63462a4a-c28c-4ffd-87a4-2d23a1c72581` (`ATOMTAG_SERVICE`).
- `substitutions.atomfast_char_uuid: 70BC767E-7A1A-4304-81ED-14B9AF54F7BD` (`ATOMTAG_MEASUREMENT`, notify).
- `include/gatt_dump.h` helper через ESP-IDF GATT API
  (`esp_ble_gattc_get_attr_count` + `esp_ble_gattc_get_all_char`).
- text_sensor «GATT таблица» + кнопка «Перечитать GATT» в `sg_re`.

**Известные проблемы:**
- `gatt_dump.h` возвращает `count=0 status=1` — параметры запроса нужно
  уточнить (`start=0x0001, end=0xFFFF` и/или `ESP_GATT_DB_ALL`). Не блокер —
  GATT-карта взята из open-source приложения.

---

## v0.2.0 (2026-06-11)

**Что:** scan 600/600 ms для быстрого reconnect (по приложению AtomFast).

**Изменения:**
- `esp32_ble_tracker.scan_parameters: interval: 600ms, window: 600ms, active: true`.
- Параметр взят из decompiled Java-приложения (`retry_delay = 600`).
- Логи `[D][ble_client_base]` использовались для discovery service UUID.

**Известные проблемы:**
- 100 % duty cycle → supervision timeout (исправлено в v0.6.0).

---

## v0.1.0 (2026-06-11)

**Что:** initial release, AtomFast-only прошивка отдельно от radon_gateway.

**Изменения:**
- ESPHome-проект `firmware/atomfast/` отделён от RadonEye-проекта.
- `ble_client` с `auto_connect: true` к MAC из `!secret atomfast_mac`.
- Web UI v3 с базовыми sensor'ами (мощность дозы, RSSI).
- Python-демон `scripts/atomfast/logger.py` для CSV-логирования с ПК.
- `secrets.example.yaml` шаблон.

**Известные проблемы:**
- Парсер ещё на архивном формате (28 байт) — исправлено в v0.4.0.

---

## Архитектурные ограничения (не баги)

- **MTU=23, не 247.** AtomFast не отвечает на `esp_ble_gattc_send_mtu_req()` —
  это ограничение прошивки прибора, обойти нельзя. Для нас не критично:
  on-air payload 13 байт, ATT payload вмещает 20 байт. Не пытаться поднимать
  MTU в будущих версиях — упрётесь в `[W][ble_client_base]: MTU exchange
  timeout` и потеряете время. (Закрыто 2026-06-12.)
