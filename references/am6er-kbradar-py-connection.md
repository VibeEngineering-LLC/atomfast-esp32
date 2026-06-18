# Параметры подключения AtomFast — источник Am6er/AtomFast-KBRadar-py

**Провенанс:** https://github.com/Am6er/AtomFast-KBRadar-py
(Python/bleak клиент, автор Am6er). Зафиксировано 2026-06-14, upstream commit `9bdb3df`.
Назначение: независимое подтверждение нашего BLE-протокола + параметров подключения,
которые мы используем в ESPHome-прошивке `atomfast_gateway`.

Это **второй независимый источник** на тот же прибор (первый — `Am6er/atomswift`
Android-исходник, см. `atomfast-onair-protocol.md`). Оба от того же автора, но разные
реализации (Java/Android vs Python/bleak) → кросс-проверка.

## Параметры подключения (connection parameters)

| Параметр | Значение | Где в репо |
|---|---|---|
| Рекламируемое имя устройства | `AtomFast` | README (SearchDevices вывод), `NAME` в POST |
| Адресация | по **MAC** (вводится вручную) | `MAC_ADDR = '...'` (формат `AA:BB:CC:DD:EE:FF`) |
| Notify characteristic | `70bc767e-7a1a-4304-81ed-14b9af54f7bd` | `CHARACTERISTIC` константа |
| Service UUID | **НЕ указан** — bleak резолвит сервис по char-UUID сам | — |
| **Connect timeout** | **60.0 секунд** | `BleakClient(MAC, timeout=60.0, ...)` |
| Scan/discover timeout | 60.0 секунд | `BleakScanner.discover(timeout=60.0)` |
| Подписка | `start_notify(CHARACTERISTIC, callback)` после connect | — |
| Conn interval / latency / supervision | **НЕ задаются** (дефолты bleak/ОС) | — |
| Scan interval / window | **НЕ задаются** (дефолты bleak/ОС) | — |

### Важно для нашей задачи #98 (rsn 0x08 supervision timeout)
Репо **НЕ даёт прямого решения** по conn-параметрам: bleak использует дефолты ОС,
conn interval / supervision timeout не экспонируются. Единственный явный параметр
установления связи — `timeout=60.0` (сколько ждать connect). Стратегия стабильности
в репо — **не тюнинг параметров, а бесконечный реконнект-цикл** (см. ниже).

## Логика реконнекта (стратегия стабильности upstream)

```
while True:                                  # внешний вечный цикл
    client = BleakClient(MAC, timeout=60.0, disconnected_callback=...)
    try:
        await client.connect()
        await client.start_notify(CHAR, callback)
        while True:                          # рабочий цикл, ~5 мин
            sleep(5) × 60                     # проверка RECONNECT_FLAG каждые 5 с
            if RECONNECT_FLAG: break          # дисконнект → пересоздать клиента
            if measurements_empty: break      # нет данных за интервал → реконнект
            ... агрегация + выгрузка ...
    except: pass                              # любая ошибка → внешний цикл реконнектит
```

- **Триггеры реконнекта**: (1) `disconnected_callback` ставит `RECONNECT_FLAG`;
  (2) пустой список измерений за интервал («нет данных» = молчащий линк);
  (3) любое исключение в connect/notify.
- `disconnected_callback` печатает `«disconnect event recieved (no device nearby)»`
  и очищает буферы — то есть upstream трактует разрыв как «прибор ушёл из радиуса».
- **Нет** backoff/retry_delay между попытками — пересоздание клиента сразу.

## Парсинг payload (callback) — ПОДТВЕРЖДАЕТ наш парсер

```python
DATA.dose      = 100 * struct.unpack('<f', data[1:5])[0]   # float32 LE, ×100
DATA.intensity = 100 * struct.unpack('<f', data[5:9])[0]   # float32 LE, ×100
DATA.bat       = data[11]                                   # uint8
DATA.temp      = data[12]                                   # int8
```

Сопоставление с нашим `atomfast-onair-protocol.md`:

| Байты | Наш парсер (ESPHome) | Am6er-py | Совпадение |
|---|---|---|---|
| 0 | flags (bit3 = re-read cfg) | не читает | — |
| 1..4 | float32 LE dose (mSv) | float32 LE dose **×100** | ✓ те же байты |
| 5..8 | float32 LE intensity (µSv/h) | float32 LE intensity **×100** | ✓ те же байты |
| 9..10 | uint16 LE pulses_2s | **не читает** | (наш богаче) |
| 11 | uint8 battery (%) | uint8 battery | ✓ |
| 12 | int8 temp (°C) | int8 temp | ✓ |

**Множитель ×100** в upstream = конверсия µSv/h → **µR/h** (микрорентген/час):
1 µSv/h ≈ 100 µR/h для гамма. То есть upstream хранит ту же величину в R-единицах.
Наш парсер хранит в Sv-единицах (µSv/h) без множителя — **физически идентично**,
отличие только в единицах вывода. Байтовые смещения и тип (float32 LE) — **совпадают**.

## Народмон-выгрузка upstream (для сверки с нашим HTTP-блоком)

- URL: `https://narodmon.ru/post.php`, метод **POST**, `application/x-www-form-urlencoded`.
- Поля: `ID`=MAC, `NAME`='AtomFast', `R_DoseRate`=avg_intensity (µR/h), `TEMP_Detector`=avg_temp.
- Период: раз в 5 минут (агрегация 60×5 с).
- (AtomSwift-grafana.py доп. шлёт в InfluxDB line-protocol `DOSERATE,MODEL="AtomSwift" value=...`
  на `:8086/write?db=mon` — для нас не актуально.)
- Авторитет протокола Народмона — скилл `narodmon-iot`, не дублируем.

## Что взять / не взять для нашей прошивки

- ✅ Подтверждение байтовой схемы payload (anti-hallucination — независимый 2-й источник).
- ✅ Имя устройства `AtomFast` в рекламе (для discovery-фильтра, если понадобится).
- ✅ Notify char `70bc767e-...` — совпадает с нашим (ATOMTAG_MEASUREMENT).
- ⚠️ Connect timeout 60 с — концептуально (ESPHome ble_client управляет этим иначе).
- ❌ Conn-параметры (interval/supervision) для фикса rsn 0x08 — в репо ИХ НЕТ.
  Решение #98 этот источник не даёт; остаётся scan-duty / external_component путь.

## Файлы upstream (клон в audit/_drafts/AtomFast-KBRadar-py)

| Файл | Что |
|---|---|
| `AtomFast-KBRadar-Win.py` | Narodmon-only клиент (MAC + notify + 5-мин агрегация) |
| `AtomSwift.py` | то же, др. MAC |
| `AtomSwift-grafana.py` | + выгрузка в InfluxDB |
| `SearchDevices.py` | `BleakScanner.discover(timeout=60.0)` — печать MAC+Name |
| `requirements.txt` | bleak~=0.22.2, requests~=2.32.3 |