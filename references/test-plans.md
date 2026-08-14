# atomfast-esp32 — Тест-планы (полные тела)

Извлечено из `SKILL.md` при lean-рефакторе #CTX-4 (2026-07-11), провенанс verbatim.
§17: заголовок + указатель остаются в SKILL.md; тела — здесь.

## План А — Первый прогон на новом железе

1. Установить ESPHome ≥ 2026.5: `pip install esphome`
2. Скопировать `firmware/*` в рабочую папку
3. `cp secrets.example.yaml secrets.yaml`, вписать SSID/пароль/MAC
4. Найти COM-порт ESP32:
   - Windows: `Get-CimInstance Win32_PnPEntity | Where { $_.Name -match 'CH340|CP210|FTDI' }`
   - Linux: `ls /dev/ttyUSB*`
   - ⚠ Иногда `COM5` или другой «занятый» порт уже принадлежит звуковой карте / Bluetooth-стеку / внутреннему модему. Ищи порт с явной меткой `CH340` / `CP210x` / `FTDI` / `USB Serial Device` — это ESP32, остальное — не он.
5. `esphome run --device COM<N> atomfast_gateway.yaml`
6. Открыть `http://atomfast-gw.local/` — должны идти точки на 7 графиков

## План B — OTA-обновление существующей прошивки

1. Найти IP/hostname: `http://atomfast-gw.local/` (уже работает)
2. `esphome run --device atomfast-gw.local atomfast_gateway.yaml` (≈ 1 минута)
3. Если порт занят локальным COM-процессом — закрыть `esphome logs`
   и повторить

## План C — Адаптация под другой BLE-прибор (template)

1. Снять snapshot текущего прибора через приложение изготовителя
2. Если есть открытый исходник приложения — взять GATT-карту оттуда
   (как сделано с Am6er/atomswift для AtomFast)
3. Иначе — поднять `gatt_dump.h` дамп (см. Known issues #1)
4. Заменить service/char UUID в substitutions
5. Переписать парсер lambda в `raw_value_atomfast` под формат прибора
6. Обновить названия sensor'ов под измеряемые величины
