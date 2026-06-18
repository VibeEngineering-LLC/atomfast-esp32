# AtomFast → ESP32 → Web UI

BLE-шлюз для дозиметра **AtomFast** на ESP32. Подключается к прибору по
Bluetooth Low Energy, парсит фирменный 13-байт on-air протокол и публикует
живые показания в Web UI ESPHome (**7 графиков в S3 baseline / 6 в классической
прошивке**) прямо в браузере — без приложения, без Home Assistant, без облака.

![схема](https://img.shields.io/badge/ESP32-DevKitC%20%2F%20S3-blue) ![ESPHome](https://img.shields.io/badge/ESPHome-2026.5+-green) ![Framework](https://img.shields.io/badge/Framework-arduino%20%7C%20esp--idf-yellowgreen) ![BLE](https://img.shields.io/badge/BLE-AtomFast-orange)

## Две прошивки в скилле — какую выбрать

В `firmware/` лежат **два готовых YAML** под разное железо. Они не взаимозаменяемы — разные платы, разные фреймворки, разные комптромиссы:

| YAML | Плата | Фреймворк | Кому |
|---|---|---|---|
| **`atomfast_gateway_s3.yaml`** ⭐ | **ESP32-S3-DevKitC-1 N16R8** (16 MB Flash + 8 MB embedded octal PSRAM, разъём **U.FL/IPEX** для внешней антенны, USB-C) | **esp-idf** | **Рекомендованный baseline.** Текущая основная конфигурация. Копия `esphome/bluetooth-proxies` upstream (esp-idf-каркас), адаптированная под AtomFast: без `bluetooth_proxy:`, с `web_server.log: true` (PSRAM 8 МБ — запас heap есть), switch Народмона `ALWAYS_OFF`. **Wi-Fi + Bluetooth стабильны** — INC-09/12 на S3 не воспроизводятся **по аналогии с Radex S3** (приборного многочасового S3-прогона AtomFast пока нет; вывод о коренной развязке coex-проблемы через S3-чип перенесён с Radex; см. «Стабильность» ниже). |
| **`atomfast_gateway.yaml`** | ESP32-DevKitC v4 (WROOM-32, 4 MB flash, без PSRAM) | **arduino** | **Архивная сборка** для старых плат, которые уже есть на руках. Прошла многочасовые прогоны на arduino без регрессий, но на esp-idf нестабильна — **классический DevKitC + esp-idf не использовать**. На новую установку лучше брать S3 N16R8. |

**Рекомендация: `atomfast_gateway_s3.yaml` на ESP32-S3-DevKitC-1 N16R8.** Стабильная, актуальная, с запасом по памяти и внешней антенной.

## Что это решает

Приложение AtomFast красивое, но:
- **закрытое** — нет API, нет CSV-экспорта, данные живут только в телефоне;
- **сессионное** — записывает только пока приложение открыто;
- **только Android/iOS** — нельзя интегрировать в умный дом.

Эта прошивка превращает любой ESP32 в **постоянно работающий шлюз**:
он держит связь с прибором 24/7, сохраняет график в памяти ESP, отдаёт
JSON через HTTP, и при желании подключается к Home Assistant.

## Что измеряется

Каждые **2 секунды** прибор отправляет полный пакет состояния (тот же,
что видит приложение). Прошивка парсит его побайтно и публикует в Web UI:

| Метрика | Единицы | Откуда |
|---|---|---|
| CPS (импульсы/сек) | cps | счёт за 2 с ÷ 2 |
| Мощность дозы | µSv/h | float32 LE, bytes 5..8 |
| Мощность дозы | µR/h | то же × 100 |
| Импульсы за 2 с | имп/2с | сырой счётчик uint16 LE |
| Накопленная доза | µSv | float32 LE, bytes 1..4, ×1000 |
| Батарея | % | int8, byte 11 |
| Температура | °C | int8 signed, byte 12 |
| RSSI BLE | dBm | из advertise-пакетов |

Декод сходится с протоколом по источнику парсинга — официальное приложение
**AtomSwift** от изготовителя (`Am6er/atomswift`, приватный репо). Длинное
side-by-side сравнение «экран приложения vs Web UI шлюза по каждой метрике»
ещё не делалось, но раскладка байтов (`val_dose`, `val_intensity`,
`pulses_2s`, `battery`, `temperature`) совпадает с upstream-кодом
из AtomSwift `AtomDeviceNRF.java`.

## Что нужно

**Железо:**
- **Рекомендуется**: ESP32-S3-DevKitC-1 **N16R8** (16 MB Flash + 8 MB embedded octal PSRAM, разъём U.FL/IPEX для внешней антенны, Wi-Fi + Bluetooth, USB-C). Стабильна на esp-idf-baseline.
- *Альтернатива (архивная)*: классический ESP32-DevKitC v4 на WROOM-32 с CH340 — только под arduino-baseline `atomfast_gateway.yaml`. **С esp-idf не работает стабильно.**
- кабель USB-C (для S3) или Micro-USB (для классического v4) → ПК для первой прошивки
- блок питания 5 V USB или powerbank для постоянной работы
- сам дозиметр **AtomFast** (модель 5xx с BLE)

**Софт:**
- Python 3.10+
- ESPHome ≥ 2026.5: `pip install esphome`
- драйвер CH340 (Windows: [WCH-CH340 driver](https://www.wch-ic.com/downloads/CH341SER_ZIP.html))

## Развёртывание (5 минут)

> Здесь — короткая шпаргалка для тех, у кого уже стоит ESPHome.
> **Полная инструкция с нуля (требования к Windows/Linux/macOS, два пути установки: через Claude Code или через стандартный ESPHome Web Installer)** — в [`INSTALL.md`](INSTALL.md).

```bash
# 1. Скопировать прошивку к себе
git clone https://github.com/VibeEngineering-LLC/atomfast-esp32.git
cd atomfast-esp32/firmware

# 2. Заполнить секреты
cp secrets.example.yaml secrets.yaml
# Открыть secrets.yaml в редакторе, вписать SSID, пароль WiFi, MAC прибора

# 3. Подставить MAC AtomFast в substitution (для RSSI sensor)
#    Открыть atomfast_gateway.yaml ИЛИ atomfast_gateway_s3.yaml,
#    найти substitutions.atomfast_mac_str и заменить
#    AA:BB:CC:DD:EE:FF на свой MAC.

# 4. Прошить через USB
#    a) ESP32-S3-DevKitC-1 N16R8, esp-idf, baseline bluetooth-proxies upstream (РЕКОМЕНДУЕТСЯ):
esphome run --device COM3 atomfast_gateway_s3.yaml          # Windows
esphome run --device /dev/ttyUSB0 atomfast_gateway_s3.yaml  # Linux

#    b) Классический ESP32-DevKitC v4 (архивная сборка для старых плат, только arduino):
esphome run --device COM3 atomfast_gateway.yaml

# 5. Открыть Web UI
# http://atomfast-gw-s3.local/   (mDNS, для esp-idf S3 baseline — РЕКОМЕНДУЕТСЯ)
# http://atomfast-gw.local/      (mDNS, для arduino-прошивки на классическом DevKitC)
# или   http://<IP-ESP-в-сети>/
```

После первой прошивки следующие обновления — **по воздуху (OTA)**:
```bash
esphome run --device atomfast-gw-s3.local atomfast_gateway_s3.yaml   # esp-idf S3 (рекомендуется)
esphome run --device atomfast-gw.local    atomfast_gateway.yaml      # arduino (архивная)
```

## Что увидишь в Web UI

ESPHome Web UI v3 рисует **спарклайн** на каждой карточке и **полноразмерный
график** при клике. Группы:

**AtomFast — показания** (главная)
- CPS (мгновенный)
- Мощность дозы µSv/h
- Мощность дозы µR/h
- Импульсы за 2 с
- Накопленная доза µSv
- CPS (среднее за окно)
- Мощность дозы (среднее за окно)

**Настройки**
- слайдер «Интервал агрегации» (1..60 с, init 5) — окно усреднения CPS/dose.
  **Только в S3 baseline** (`atomfast_gateway_s3.yaml`, `number.agg_interval_s`).
  В классической прошивке слайдера нет — фиксированное окно.
- кнопка «Перечитать GATT» (для отладки BLE) — **только в классической прошивке**
  (`atomfast_gateway.yaml`, через `include/gatt_dump.h`). В S3 baseline GATT-UI нет.

**RE / Отладка BLE** (**только в классической прошивке**)
- сырой hex последнего notify-пакета
- дамп GATT-таблицы прибора

**Диагностика**
- батарея AtomFast
- температура AtomFast
- RSSI BLE
- WiFi сигнал
- uptime

## Где живёт график

В **оперативной памяти ESP**. Web UI v3 ESPHome хранит историю sensor'ов
с момента последней перезагрузки (~час назад на типичную картку). Если
нужна постоянная история — подключи к Home Assistant через ESPHome API
(`encryption.key` уже сгенерирован в `secrets.yaml`).

Для CSV-логирования на ПК — отдельный Python-демон (см. `scripts/` в
исходном проекте, либо подключиться к JSON-эндпоинту `/sensor/cps_instant`).

## Архитектура

```
┌────────────┐  BLE notify   ┌──────────────┐  HTTP/WiFi  ┌───────────┐
│  AtomFast  │ ──────────►  │  ESP32-DevKit │ ──────────► │ Браузер,  │
│  (Plus2)   │   13 байт    │   (ESPHome)   │   JSON +    │ HA, Python│
│            │   раз в 2 с  │               │   Web UI v3 │           │
└────────────┘              └──────────────┘             └───────────┘
                                   │ I/O
                                   ▼
                            ┌──────────────┐
                            │  парсер      │
                            │  13 байт →   │
                            │  7 sensors   │
                            └──────────────┘
```

Никаких облаков. Никаких аккаунтов. Всё крутится в твоей локальной сети.

## Протокол AtomFast

Полная карта BLE GATT + разбор on-air payload —
[references/atomfast-onair-protocol.md](references/atomfast-onair-protocol.md).

Кратко: 13 байт каждые 2 с по characteristic
`70BC767E-7A1A-4304-81ED-14B9AF54F7BD` сервиса
`63462a4a-c28c-4ffd-87a4-2d23a1c72581` — флаги + 2 float32 LE
(накопленная доза mSv, мощность дозы µSv/h) + uint16 LE (импульсы) +
батарея + температура.

## BLE-топология и BT-сигнал

### Топология

```
[AtomSwift app на телефоне (опционально)]
            │
            BLE single-central — выйти из app перед запуском шлюза!
            │
            ▼
   ┌─────────────────────┐
   │  AtomFast Plus2     │  ◄── BLE notify push (0.5 Hz, 13 байт)
   │  Service 63462a4a...│
   │  bond не требуется  │
   └─────────────────────┘
            │
            │ BLE advertise + connect
            │
            ▼
   ┌─────────────────────┐
   │  ESP32-DevKitC      │
   │  (ESPHome           │
   │   atomfast_gateway) │
   └─────────────────────┘
            │
            │ WiFi 2.4 GHz (co-existence с BLE)
            │
            ├──► Web UI — http://atomfast-gw.local/ :80 (sparkline + 7 графиков)
            ├──► Home Assistant — ESPHome API :6053 (encrypted)
            └──► Народмон — http://narodmon.ru/get каждые 5 мин (R1/D1/T1/BAT1)
```

### Advertisement

| Параметр | Значение |
|---|---|
| Имя | `AtomFast XXXX` (XXXX — суффикс серийника) |
| Тип PDU | ADV_IND |
| Интервал | ≈ 1.1 с между advert (наблюдаемо ESP32) |
| Manufacturer data | TBD (есть, layout полностью не реверсили) |
| Service UUIDs в adv | `63462a4a-c28c-4ffd-87a4-2d23a1c72581` объявляется |
| RSSI на 5 м | −37..−68 dBm (наблюдаемо на ESP32, зависит от ориентации антенны) |
| Pairing/bonding | НЕ требуется |

### Connection

| Параметр | Значение | Источник |
|---|---|---|
| Bonding/pairing | НЕ требуется | live |
| MTU | 23 (default, не увеличивается) | live ESP32 — AtomFast не отвечает на MTU exchange |
| Scan interval / window | 600 / 600 ms (100 % duty) | `AtomDeviceNRF.java:59 retry_delay = 600` |
| Connection timeout | 24000 ms | `AtomDeviceNRF.java connect_timeout = 24000L` |
| Reconnect delay | 3300 ms | `reconnect_after_failed_connect_delay = 3300L` |
| Notify cadence | 0.500 Hz (раз в 2 с) | firmware прибора |
| Single-central | Один host — другой получит refuse | live |

### GATT-структура (из Am6er/atomswift GattAttributes.java)

| Сервис | UUID | Тип | Назначение |
|---|---|---|---|
| GAP | `0x1800` | std | Generic Access |
| GATT | `0x1801` | std | Generic Attribute |
| Device Information | `0x180A` | std | модель, серийный номер, FW/HW revision (`0x2A24..0x2A29`) |
| **ATOMTAG_SERVICE** | `63462a4a-c28c-4ffd-87a4-2d23a1c72581` | custom | основной сервис AtomFast |

| Characteristic (Java symbol) | UUID | Props | Назначение |
|---|---|---|---|
| ATOMTAG_MEASUREMENT | `70BC767E-7A1A-4304-81ED-14B9AF54F7BD` | Notify | **главный поток данных** (13 байт каждые 2 с) |
| ATOMTAG_MEASUREMENT2 | `8E26EDC8-A1E9-4C06-9BD0-97B97E7B3FB9` | Notify | вторичный measurement (вероятно CPM stream, TBD) |
| ATOMTAG_THRESHOLD1 | `3F71E820-1D98-46D4-8ED6-324C8428868C` | Write | порог тревоги 1 |
| ATOMTAG_THRESHOLD2 | `2E95D467-4DB7-4D7F-9D82-4CD5C102FA05` | Write | порог тревоги 2 |
| ATOMTAG_THRESHOLD3 | `F8DE242F-8D84-4C12-9A2F-9C64A31CA7CA` | Write | порог тревоги 3 |
| ATOMTAG_CONFIG | `EA50CFCD-AC4A-4A48-BF0E-879E548AE157` | Write | конфиг устройства |
| **ATOMTAG_CALIBRATION** | `57F7031F-03C1-4016-8749-BAABAA58612D` | Write | ⚠ калибровка — НЕ ТРОГАТЬ |
| ATOMTAG_CONFIG2 | `E2423A67-7541-4080-8B5A-59449454A873` | Write | вторичный конфиг |
| ATOMTAG_TEXT | `BB6C9C06-C37D-49B0-94CA-83623622573B` | TBD | текстовый дескриптор |

### Pattern взаимодействия — Notify push (без команды)

**Тип**: чистый push-only Notify. Host подписывается через CCCD на
`70BC767E-...`, прибор сам шлёт пакет раз в 2 с. Никакого опкода / команды слать
не нужно — данные идут сразу после CCCD write `01 00`.

### Payload — 13 байт on-air (источник: Am6er/atomswift)

| Offset | Размер | Тип | Поле | Описание |
|---|---|---|---|---|
| 0 | 1 | uint8 | flags | флаги (bit 3 = re-read config request) |
| 1..4 | 4 | float32 LE | val_dose | накопленная доза, **mSv** |
| 5..8 | 4 | float32 LE | val_intensity | мгновенная мощность дозы, **µSv/h**. `>1000` — битый пакет, игнор |
| 9..10 | 2 | uint16 LE | pulses_last_2sec | импульсы за последние 2 с |
| 11 | 1 | int8 | valBattery | % заряда батареи |
| 12 | 1 | int8 | valTemperature | температура °C (signed) |

Live verification: `00 EE EE E1 3C 07 7A BA 3D 26 00 40 19` → доза 27.58 µSv,
rate 0.091 µSv/h, 38 имп/2с (19 cps), 64 % батарея, 25 °C
(см. [`references/atomfast-onair-protocol.md`](references/atomfast-onair-protocol.md)).

### Параметры stability в ESPHome

```yaml
esp32_ble_tracker:
  scan_parameters:
    active: true       # defaults (320/30 ms ≈ 9 % duty)

ble_client:
  - mac_address: !secret atomfast_mac
    id: ble_atomfast
    auto_connect: true   # AtomFast advertise'ит регулярно
    services:
      - uuid: "63462a4a-c28c-4ffd-87a4-2d23a1c72581"
        characteristics:
          - uuid: "70BC767E-7A1A-4304-81ED-14B9AF54F7BD"
            on_notify:
              then:
                - lambda: |-
                    // parse 13-byte payload, publish 7 sensors
```

**Историческая заметка**: в atomfast v0.4.x использовался ручной
`scan_parameters: { interval: 600ms, window: 600ms, active: true }` (взято из
`retry_delay = 600` в AtomSwift firmware). Это работает **при одиночном
ble_client без HA-нагрузки**. Как только в прошивке появляется второй сервис
(ESPHome API + Web UI + Народмон HTTP), defaults становятся обязательными —
см. [`esp32-dev/SKILL.md`](../esp32-dev/SKILL.md) → "scan_parameters defaults для
WiFi co-existence" и инцидент radex v0.1.2 → v0.1.3.

## Стабильность платформ — почему ESP32-S3-DevKitC-1 N16R8, а не классический DevKitC на esp-idf

**Короткий итог**: ESP32-S3-DevKitC-1 **N16R8** (16 МБ Flash + 8 МБ embedded octal PSRAM, U.FL/IPEX, USB-C) **+ esp-idf** — стабильная и рекомендованная конфигурация. Классический («синий») ESP32-DevKitC с WROOM-32 / 4 МБ flash / без PSRAM **+ esp-idf — НЕ стабилен**: WiFi/BLE coexistence ломает реконнекты, web_server без PSRAM уходит в `json:111`. На том же классическом DevKitC arduino-фреймворк работает, но это исторический baseline для уже имеющихся плат, а не рекомендация для новой установки.

Проверено серией инцидентов в этом проекте за июнь 2026:

### INC-09: esp-idf даёт медленные коннекты и `rsn 0x3e`

При попытке мигрировать AtomFast-шлюз с arduino на esp-idf (`v0.7.0`–`v0.7.1`) на той же плате DevKitC с теми же scan-параметрами оператор получил:
- **`rsn 0x3e × 2`** — Connection Establishment Failure: HCI ESP'а пытается установить link, прибор не отвечает, повтор.
- **CONNECTED #7** — успешный коннект только с **седьмой попытки** после reboot (на arduino — с первой).
- **сессия 7 секунд, реконнект 13 секунд** — на arduino сессии многочасовые.

Откат на arduino (`v0.7.2`, тот же 600 ms / 180 ms scan) → **40-минутный прогон: CONNECTED #1 за ~21 с, 0× rsn 0x3e, 0 ребутов**. Регрессия чисто эспидфовая.

**Корень**: esp-idf включает встроенный BLE/WiFi coexistence-механизм (`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`), который динамически режет тайминги BLE-сканера для защиты WiFi-радио. Эти подрезы конфликтуют с активным `ble_client`, который пытается удерживать GATT-сессию — и проигрывает.

### INC-12: esp-idf coex ломает фиксированный `scan_parameters: 600/180`

При прошивке RadonEye HA Gateway на esp-idf со стандартным «арудино-стабильным» `interval: 600ms, window: 180ms` (duty 30%) — плата **уходит в `Auth Expired`** сразу после ассоциации к WiFi:
- пинг — 50% потерь;
- web server мёртв (curl exit 28 — connection refused);
- UART флудит `wifi: STA_DISCONNECTED reason=15 (AUTH_EXPIRE)`.

**Корень**: на arduino эти 600/180 — твои персональные scan-окна, BLE-стек ESP'а просто их слушает. На esp-idf coex-субсистема ожидает дефолтные «короткие» окна и применяет свои коррекции поверх; ручные `interval`/`window` ломают её модель и заставляют BLE занимать радио по жёсткому расписанию. WiFi-радио (один и тот же чип!) недополучает воздух и не успевает обновить ключ авторизации.

**HARD-правило по итогу INC-12**: на esp-idf с активным `ble_client` — **`640ms / 32ms` (~5% duty)** или вообще оставить только `active: false` (esp-idf сам сбалансирует). На arduino — **600 ms / 180 ms (30% duty)**.

В `atomfast_gateway_s3.yaml` стоит 640/32 — соблюдено.

### INC-05: `web_server.log: true` → `json:111` на DevKitC

Когда в Web UI ESPHome v3 включена панель Debug Log, она подписывается на тот же SSE-стрим `/events`, что и состояния сенсоров. На esp-idf-сборке ESP32-DevKitC (320 КБ RAM, без PSRAM) каждое сообщение лога — отдельный JSON-event. При интервале опроса сенсоров 5 с + WiFi-флапы + лог DEBUG'а это даёт десятки event/с, ArduinoJSON document переполняется:

```
[E][json:111]: JSON document overflow
[W][component:522] web_server took a long time (73 ms)
```

Один залп = ~20 строк/сек, длится пока браузер открыт, иногда уносит свободную heap до ~12 КБ. Пустой левый сайдбар Web UI — следствие.

**Лечение (одна строка)**: `web_server: log: false` — Debug Log панель в браузере исчезает, state-events идут штатно. Для `atomfast_gateway.yaml` это default.

**Почему `atomfast_gateway_s3.yaml` всё-таки с `log: true`**: ESP32-S3-DevKitC-1 N16R8 несёт **8 МБ embedded octal PSRAM**. Запас heap на порядок больше, чем на классическом DevKitC; json:111 при F5 не достигается. Условие отката (зашито в YAML-комментарии): любые ребуты / json:111 / OOM → вернуть `log: false` и не активировать заново.

### Итог: что выбрать

- ⭐ **ESP32-S3-DevKitC-1 N16R8** (16 МБ Flash + 8 МБ embedded octal PSRAM, U.FL/IPEX, USB-C) → `atomfast_gateway_s3.yaml` (esp-idf, 640/32, log:true). **Рекомендованная актуальная конфигурация.** Внешняя антенна через IPEX повышает дальность BLE до прибора, embedded octal PSRAM решает INC-05, S3-чип развязывает coex (вывод **по аналогии с Radex S3**: на Radex-стороне S3 чип убрал `rsn 0x3e` / `Auth Expired`; на AtomFast приборного S3-прогона аналогичной длительности пока нет, тезис «S3 решает INC-09/12» — перенос гипотезы).
- **Классический ESP32-DevKitC v4** (WROOM-32, 4 MB flash, без PSRAM) → `atomfast_gateway.yaml` (arduino, 600/180, log:false). Работает только на arduino-baseline. С esp-idf на этой плате — INC-09 (rsn 0x3e, медленные коннекты) и INC-12 (Auth Expired). Имеет смысл, только если плата уже есть на руках и не хочется покупать S3.
- **ESP32-S3 без PSRAM или дешёвая клон-плата с QSPI вместо octal** → меняй `psram: mode: octal` на `quad` или убирай блок целиком; ставь `web_server: log: false`. Лучший вариант — взять оригинальную N16R8 от Espressif.

**Не ставь esp-idf на классический DevKitC.** Реконнекты будут хуже, чем на arduino, а проблема в BLE/WiFi coex и нехватке RAM — её на этой плате не починить.

## Известные ограничения

- **MTU 23** — AtomFast не отвечает на запрос увеличения MTU; payload 13 байт
  всё равно влезает.
- **Один прибор на ESP** — в текущей версии. ESP32 поддерживает до 3
  BLE-клиентов одновременно, см. ветку `multi-device` (TODO).
- **Service UUID и формат payload** валидны для семейства AtomFast Plus2;
  на старых моделях (Plus, обычный) формат может отличаться — pull request
  с реверсом приветствуются.
- **`gatt_dump.h` помощник** показывает «count=0» через `esp_ble_gattc_get_attr_count`
  даже когда `ble_client` уже подписан — известный баг, не влияет на работу
  основной прошивки.

## Лицензия

MIT, см. корень репозитория.

## Кредиты

- **AtomFast** (`youratom.com`) — отличный карманный дозиметр.
- **Am6er/atomswift** (приватный репо) — авторитетный источник
  GATT-карты и формулы парсинга on-air payload.
- **ESPHome** — лучший фреймворк для ESP32 в нашей вселенной.
- Глубинный реверс протокола: skill
  [`atomfast-tester`](../atomfast-tester/) в этом же репо.
