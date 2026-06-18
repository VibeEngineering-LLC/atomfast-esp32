# v0.7.12-webv2-alt (альтернативная прошивка)

Альтернатива основной `v0.7.12`. Те же BLE-парсер, scan-параметры, WiFi-watchdog
и Народмон-выгрузка, но Web UI переведён на **`web_server: version: 2`** с кастомной
надстройкой CSS+JS.

## Зачем альтернатива

`web_server: version: 3` (default в основной v0.7.12) — современный
Lit-based UI с `sorting_groups`, чистым SSE-стримом и компактной разметкой.
Стабилен после `log: false` (см. CHANGELOG v0.7.5 — Debug Log SSE-флуд → `json:111`).

`web_server: version: 2` — старый single-`<table>` UI без `sorting_groups`. Раздаёт
custom-инклюды `css_include` / `js_include` через `/0.css` и `/0.js` (gzip-bundle,
по индексу, НЕ по basename). На него можно навесить:

- **Кастомный порядок строк** через JS-`MutationObserver` (deep shadow-DOM walk),
  чтобы показания прибора шли вверху, кнопки рядом со своими блоками.
- **Ограничение высоты Debug Log** через JS (CSS не пробивает shadow-границу).
- **`select.template`** в качестве UX-элемента выбора (в v2 рендерится как
  native `<select>` dropdown в строке таблицы).

Вариант полезен, если оператор предпочитает плотную таблицу без коллапсов
sorting_groups и хочет ручной контроль порядка.

## Состав

| Файл | Назначение |
|---|---|
| `atomfast_gateway_webv2.yaml` | Прошивка (32 KB). MAC замаскирован `AA:BB:CC:DD:EE:FF`. |
| `www/custom.css` | Базовый styling таблиц + резервный селектор для лога. |
| `www/custom.js` | TARGET_ORDER 6 групп × 31 паттерн + deep-DOM walk + лог-ограничение 240 px. |

Сборка читает `www/` через `web_server.css_include` / `js_include` и
встраивает в gzip-bundle на этапе компиляции.

## Группы порядка строк (TARGET_ORDER)

1. **Показания прибора** — Радиация / Доза / CPS / Температура / Батарея / Окно агрегации.
2. **BLE-диагностика + кнопки** — связь, реконнекты, GAP rsn 0x8, RSSI, кнопки сброса/реконнекта.
3. **GATT** — таблица handle'ов + кнопка «Перечитать».
4. **WiFi** — сигнал, IP-адрес.
5. **Народмон** — switch, **select протокола (GET / POST / HTTPS POST / JSON POST)**,
   имена метрик R1/D1/T1/BAT1, кнопка «Послать сейчас».
6. **Система** — MAC, Uptime, «Перезагрузить».

## Народмон select (4 опции)

| Опция | Endpoint | Content-Type | Тело |
|---|---|---|---|
| HTTP GET | `http://narodmon.ru/get` | — | URL-параметры `?ID=<MAC>&<R1>=<r>&<T1>=<t>&...` |
| HTTP POST | `http://narodmon.ru/post` | `application/x-www-form-urlencoded` | form-body |
| HTTPS POST | `https://narodmon.ru/post` | `application/x-www-form-urlencoded` | то же через TLS |
| JSON POST | `http://narodmon.ru/json` | `application/json` | `{"devices":[{"mac":"...","sensors":[{"id":"R1","value":...},...]}]}` |

`select.template` с `optimistic: true`, `restore_value: true`,
`initial_option: "JSON POST"`. Диспетчер в `interval: 300s` (минимум Народмона —
короче ban IP на 1 час) ветвится по `id(narodmon_method).state` (или
`current_option()` в 2026.7.0+).

## Сборка

```powershell
$env:PYTHONIOENCODING='utf-8'; $env:PYTHONUTF8='1'
esphome compile firmware/archive/v0.7.12-webv2-alt/atomfast_gateway_webv2.yaml
```

(Если `esphome` нет в PATH — укажи полный путь к `esphome.exe`, обычно
`%LOCALAPPDATA%\Programs\Python\Python312\Scripts\esphome.exe` на Windows
или из своего venv.)

> ⚠️ Кириллица в путях проекта требует `PYTHONIOENCODING=utf-8` + `PYTHONUTF8=1`,
> иначе `esphome.exe` падает с `UnicodeDecodeError` в colorama (см. урок в основном CHANGELOG).

## Подводные камни v2

- **Custom-инклюды раздаются по индексу `/N.css|js`**, НЕ по basename.
  `curl http://<host>/custom.css` → 404; правильно `curl http://<host>/0.css`.
- **CSS не пробивает shadow-границу** `<esp-app>`. Ограничение высоты `esp-log`
  делает JS через inline `style.maxHeight = '240px'` на host'е.
- **REST-эндпоинт `/select/<object_id>`** слагифицируется из русского `name:`
  (например, «Протокол Народмона» → `select-______________`). Для curl-управления
  задавай `object_id:` явно.
- После перепрошивки оператору **Ctrl+Shift+R** в браузере, иначе кэшированный
  `/0.js` использует устаревший TARGET_ORDER.

## Известные ограничения

- 5-минутный upload-тест на `narodmon.ru/json` валидирован YAML-синтаксически
  (compile clean), но semantic `errno=200` от сервера фиксировался на боевой
  плате только частично — следующая публикация поднимет статус.
- Один deprecation warning от ESPHome 2026.5.3: `select::Select::state` →
  использовать `current_option()` в 2026.7.0+. На текущей версии не блокер.

## Откат на основную v0.7.12

Скачать [`../../atomfast_gateway.yaml`](../../atomfast_gateway.yaml) и прошить — она остаётся
эталонной стабильной (web_server v3 + `log:false` + `sorting_groups`).
