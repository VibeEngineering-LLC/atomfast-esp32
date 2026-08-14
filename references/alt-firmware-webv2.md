# atomfast-esp32 — Альтернативная прошивка `v0.7.12-webv2-alt` (полное тело)

Извлечено из `SKILL.md` при lean-рефакторе #CTX-4 (2026-07-11), провенанс verbatim.

Параллельная к `v0.7.12` ветка с **`web_server: version: 2`** + кастомные
`www/custom.css` (461 B) и `www/custom.js` (149 строк) поверх gzip-bundle.
Те же BLE-парсер, scan 640/32ms, WiFi-watchdog, Народмон-выгрузка — только Web UI
другой: плотная single-`<table>` раскладка без sorting_groups, ручной порядок
строк через `MutationObserver` (deep shadow-DOM walk), ограничение высоты лога
240 px через JS (CSS shadow-границу не пробивает), `select.template` Народмона
с четырьмя протоколами (**HTTP GET / HTTP POST / HTTPS POST / JSON POST** на
`narodmon.ru/json` с телом `{"devices":[{"mac":"...","sensors":[...]}]}`).

Файлы: [`firmware/archive/v0.7.12-webv2-alt/`](../firmware/archive/v0.7.12-webv2-alt/)
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
