# AtomFast on-air BLE protocol — полная карта

> **Provenance.** Service UUID и парсинг 13-байт payload получены из
> исходников Android-приложения изготовителя
> [`github.com/Am6er/atomswift`](https://github.com/Am6er/atomswift)
> (приватный репо; доступ через `gh api`).
> Файл `app/src/main/java/org/fe57/atomtag/GattAttributes.java` —
> авторитетная GATT-карта; файл `AtomDeviceNRF.java::broadcastUpdate` —
> авторитетный парсер.
>
> **Подтверждено live** на ESP32-DevKitC + AtomFast Plus2 (2026-06-12,
> прошивка `firmware/atomfast_gateway.yaml` этого же скилла).

## 1. GATT topology

| Java symbol (Am6er) | UUID | Назначение |
|---|---|---|
| `ATOMTAG_SERVICE` | `63462a4a-c28c-4ffd-87a4-2d23a1c72581` | **Primary service** |
| `ATOMTAG_MEASUREMENT` | `70BC767E-7A1A-4304-81ED-14B9AF54F7BD` | основной notify (CPS / dose-rate / накопленная доза / батарея) |
| `ATOMTAG_MEASUREMENT2` | `8E26EDC8-A1E9-4C06-9BD0-97B97E7B3FB9` | вторичный measurement (назначение TBD — вероятно CPM stream) |
| `ATOMTAG_THRESHOLD1` | `3F71E820-1D98-46D4-8ED6-324C8428868C` | порог тревоги 1 |
| `ATOMTAG_THRESHOLD2` | `2E95D467-4DB7-4D7F-9D82-4CD5C102FA05` | порог тревоги 2 |
| `ATOMTAG_THRESHOLD3` | `F8DE242F-8D84-4C12-9A2F-9C64A31CA7CA` | порог тревоги 3 |
| `ATOMTAG_CONFIG` | `EA50CFCD-AC4A-4A48-BF0E-879E548AE157` | конфиг устройства (write) |
| `ATOMTAG_CALIBRATION` | `57F7031F-03C1-4016-8749-BAABAA58612D` | калибровка (sensitive — НЕ трогать) |
| `ATOMTAG_CONFIG2` | `E2423A67-7541-4080-8B5A-59449454A873` | вторичный конфиг |
| `ATOMTAG_TEXT` | `BB6C9C06-C37D-49B0-94CA-83623622573B` | текстовый дескриптор |
| `CLIENT_CHARACTERISTIC_CONFIG` | `00002902-0000-1000-8000-00805f9b34fb` | стандартный CCCD для включения notify |

Плюс стандартный **Device Information Service** `0x180A`:
`0x2A24..0x2A29` — модель, серийный номер, firmware revision,
hardware revision, software revision, имя изготовителя.

## 2. Параметры соединения

| Параметр | Значение | Откуда |
|---|---|---|
| Notify rate | 0.500 Hz (раз в 2 с) | firmware прибора |
| MTU | 23 (не увеличивается) | live ESP32 — AtomFast не отвечает на MTU exchange |
| Scan interval | 600 ms | `AtomDeviceNRF.java:59 retry_delay = 600` |
| Scan window | 600 ms | то же (interval == window для максимального duty cycle) |
| Connection timeout | 24000 ms | `connect_timeout = 24000L` |
| Reconnect delay | 3300 ms | `reconnect_after_failed_connect_delay = 3300L` |
| Averaging window (app) | 6 значений | `AVERAGING_COUNT = 6` |

## 3. On-air notify payload (13 байт)

Источник парсинга — `AtomDeviceNRF.java::broadcastUpdate`,
ветка `if (GattAttributes.UUID_MEASUREMENT.equals(characteristic.getUuid()))`.

| Offset | Размер | Тип | Поле (Java) | Описание |
|---|---|---|---|---|
| 0 | 1 | `uint8` | `valFlags` | флаги; bit 3 = re-read config request |
| 1..4 | 4 | float32 LE | `val_dose` | накопленная доза, **mSv** |
| 5..8 | 4 | float32 LE | `val_intensity` | мгновенная мощность дозы, **µSv/h**. `>1000` — битый пакет, игнор |
| 9..10 | 2 | uint16 LE | `pulses_last_2sec` | импульсы за последние 2 с |
| 11 | 1 | int8 | `valBattery` | % заряда батареи |
| 12 | 1 | int8 | `valTemperature` | температура °C (signed) |

**Особенность Java-кода**: `ByteBuffer.wrap` по умолчанию BIG-endian, но
автор делает ручной reverse:
```java
byte[] tmp = {0, 0, 0, 0};
for (int k = 0; k < 4; k++) tmp[k] = data[8 - k];
bb = ByteBuffer.wrap(tmp);
val_intensity = bb.getFloat();
```
→ это эквивалентно прямому чтению float32 LE из `data[5..8]`. В С++ на
ESP можно делать просто `memcpy(&val_intensity, data + 5, 4)` —
архитектура ESP32 уже little-endian.

## 4. Worked example (live ESP32, 2026-06-12)

Сырые байты с шины: `00 EE EE E1 3C 07 7A BA 3D 26 00 40 19`

| Поле | Hex | Декод |
|---|---|---|
| flags | `0x00` | нет re-read config |
| val_dose | `EE EE E1 3C` = `0x3CE1EEEE` | **0.02758 mSv** = 27.58 µSv накопленная доза |
| val_intensity | `07 7A BA 3D` = `0x3DBA7A07` | **0.0911 µSv/h** ≈ 9.1 µR/h (типичный фон) |
| pulses_2s | `0x0026` | **38** → CPS = 19 |
| battery | `0x40` | **64 %** |
| temperature | `0x19` | **25 °C** |

Сравнение с экраном приложения AtomFast в тот же момент — совпадение
до последнего знака.

## 5. C++ парсер (ESP32, тестировано в продакшене)

```cpp
if (x.size() >= 13) {
  uint8_t flags = x[0];
  float val_dose, val_intensity;
  memcpy(&val_dose,      x.data() + 1, 4);
  memcpy(&val_intensity, x.data() + 5, 4);
  uint16_t p2s = (uint16_t) x[9] | ((uint16_t) x[10] << 8);
  int8_t battery     = (int8_t) x[11];
  int8_t temperature = (int8_t) x[12];
  float cps = (float) p2s / 2.0f;

  if (std::isfinite(val_intensity) && val_intensity >= 0 && val_intensity < 1000) {
    // публикация в sensor'ы…
  }
}
```

Sanity checks (Am6er делает то же самое):
- `val_intensity > 1000` → битый пакет, игнор
- `val_dose < 0` → битый пакет, игнор

## 6. Python парсер (для ПК-логгера)

```python
import struct

def parse_atomfast_payload(data: bytes) -> dict | None:
    if len(data) < 13:
        return None
    flags = data[0]
    val_dose_mSv      = struct.unpack('<f', data[1:5])[0]
    val_intensity_uSh = struct.unpack('<f', data[5:9])[0]
    pulses_2s         = struct.unpack('<H', data[9:11])[0]
    battery_pct       = struct.unpack('<b', data[11:12])[0]
    temperature_C     = struct.unpack('<b', data[12:13])[0]

    if not (0 <= val_intensity_uSh < 1000):
        return None
    if val_dose_mSv < 0:
        return None

    return {
        'flags': flags,
        'cumulative_dose_uSv': val_dose_mSv * 1000.0,
        'dose_rate_uSv_h': val_intensity_uSh,
        'dose_rate_uR_h': val_intensity_uSh * 100.0,
        'cps': pulses_2s / 2.0,
        'pulses_2s': pulses_2s,
        'battery_pct': battery_pct,
        'temperature_C': temperature_C,
    }
```

## 7. Что ещё в Am6er-репо (для будущего реверса)

- `AtomDeviceNRF.java` — основной парсер + measure-mode (стартовая доза,
  накопление импульсов, error confidence interval)
- `Constants.java` — типы устройств (`DEV_UNKNOWN`, `DEV_NRF`, …)
- `AtomSpectraDevice.java` — спектрометрический режим (AtomSpectra,
  отдельный продукт)
- `CompactNotifications.java` — формат компактных notify (≠ measurement)
- `NRF.java` — обёртка над Nordic BLE Library (no.nordicsemi.android.ble)

Связь с этим скиллом: для других AtomFast-моделей (Plus, Spectra) можно
адаптировать парсер, взяв соответствующий `AtomDevice*.java`. Service UUID
тот же (`63462a4a-...`), characteristic UUID может различаться.

## 8. Кросс-ссылки

- Глубинный реверс приложения (фокус на тестере, не на шлюзе):
  [`atomfast-tester`](../../atomfast-tester/) — там полная история
  переходов состояний, persistent storage, archive-format (28 байт)
  и анти-claim-блок про предыдущий misread (теперь снят).
- Прошивка-шлюз: [`firmware/atomfast_gateway.yaml`](../firmware/atomfast_gateway.yaml).
- BLE-помощник для дампа GATT-таблицы любого прибора:
  [`firmware/include/gatt_dump.h`](../firmware/include/gatt_dump.h)
  (helper, known issue: возвращает count=0 — нужен фикс).
