# atomfast-esp32 — История версий прошивки (полное тело)

Извлечено из `SKILL.md` при lean-рефакторе #CTX-4 (2026-07-11), провенанс verbatim.
Канонический changelog — [`firmware/CHANGELOG.md`](../firmware/CHANGELOG.md); здесь снимок
описания текущей версии из SKILL.md на момент рефактора.

## Текущая версия прошивки

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
[`firmware/archive/v0.7.12/`](../firmware/archive/v0.7.12/). Откат: скопировать
`atomfast_gateway.yaml` оттуда поверх главной.

Прошлые версии — [`firmware/archive/`](../firmware/archive/). Подробности
изменений и замеров — [`firmware/CHANGELOG.md`](../firmware/CHANGELOG.md). Все
будущие правки → отдельная запись в CHANGELOG (semver), копия предыдущей
версии в `firmware/archive/<old-version>/`.

## Народмон-выгрузка (с 2026-06-12)

Прошивка выгружает четыре метрики на narodmon.ru каждые 5 минут:
**R1** (мощность дозы µR/h), **D1** (накопленная доза µSv), **T1** (температура),
**BAT1** (заряд батареи). Имена метрик правятся в Web UI без перепрошивки,
выгрузку можно отключать switch'ем «Выгружать на Народмон».

Авторитетный источник протокола (TCP/UDP/HTTP GET/POST/JSON/MQTT, префиксы,
коды ответа, правила бана) — официальная документация
[narodmon.ru](https://narodmon.ru/client). HTTP-блок в `atomfast_gateway.yaml`
содержит жёстко прошитые URL четырёх транспортов; при изменении API
проверяй разделы `4 - HTTP / 5 - JSON` в документации.
