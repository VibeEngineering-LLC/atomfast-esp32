# Known Issues & Hardware Compatibility — atomfast-esp32

Краткий справочник: какие платы мы реально проверили на AtomFast-шлюзе, какие проблемы клиенты ловят чаще всего, и что с этим делать. Длинные технические разборы инцидентов с прошлой разработки — в [README.md](README.md), раздел «Стабильность платформ».

---

## 1. Матрица совместимости плат (hardware compatibility)

> **Дисклеймер.** Работа подтверждена только на платах со статусом ✅. Для всех остальных — **гарантий нет**. Если попробовал и получилось/не получилось — открой [issue](https://github.com/VibeEngineering-LLC/atomfast-esp32/issues), добавим в матрицу.

### ✅ Протестированы и рекомендованы

| Плата | Чип | Flash | PSRAM | Антенна | YAML | Заметки |
|---|---|---|---|---|---|---|
| **ESP32-S3-DevKitC-1 N16R8** ⭐ | ESP32-S3 dual-core 240 MHz | 16 MB | 8 MB embedded octal | PCB + **U.FL/IPEX** | `atomfast_gateway_s3.yaml` (esp-idf baseline) | **Рекомендованная плата.** Стабильна на esp-idf, PSRAM решает `json:111`, U.FL позволяет внешнюю антенну (важно если AtomFast >5 м или за стеной). USB-C. |
| **ESP32-DevKitC v4 (WROOM-32)** | ESP32 classic dual-core 240 MHz | 4 MB | — | PCB | `atomfast_gateway.yaml` (**только arduino**) | ⚠ Архивная сборка. arduino-стек стабилен, **esp-idf на этой плате — НЕТ** (см. INC-09/12 в README). Брать только если плата уже в руках. |

### ❌ НЕ работает / не подходит

| Плата | Причина |
|---|---|
| **ESP32-C3 SuperMini** | См. **INC-C3-001** ниже. Готового C3-YAML в скилле нет; попытка прошить наш S3-вариант → `OSError: [Errno 28] No space left on device`. |
| **ESP32-S2** | Нет BLE — физически не может быть BLE-шлюзом к AtomFast. |
| **ESP8266 (любой)** | Нет BLE. |

### ⚠ Не протестированы — на свой риск

| Плата | Ожидание |
|---|---|
| **XIAO ESP32-C3** (Seeed) | Для AtomFast не пробовали. На соседнем radon-шлюзе работает стабильно, но потребуется отдельный YAML — текущие наши под C3 не пойдут (см. INC-C3-001). |
| **ESP32-C6 / H2** | BLE 5.0 есть, NimBLE-стек поддерживается, но YAML под них в скилле не написан. |
| **ESP32-S3-DevKitC-1 N8R2 / N4R2** | Меньше Flash/PSRAM. `atomfast_gateway_s3.yaml` ориентирован на N16R8; на меньших ревизиях нужно править `flash_size`, `partition` и `psram: mode`. |
| **«Голый» ESP32-S3 без PSRAM или с QSPI PSRAM** | Меняй `psram.mode: octal` → `quad` или убирай блок целиком; `web_server.log: false`. Лучше взять оригинальную N16R8 от Espressif. |

---

## 2. Рекомендации по выбору железа

1. **Покупаешь новую плату под продакшен**: **ESP32-S3-DevKitC-1 N16R8** (Espressif оригинал). Стабильна, есть запас по памяти, есть U.FL для внешней BLE-антенны, USB-C. Прошивка — `atomfast_gateway_s3.yaml`.
2. **Уже есть классический ESP32-DevKitC v4**: можно использовать `atomfast_gateway.yaml` (arduino). На esp-idf эту плату с AtomFast **не поднять** — известная регрессия INC-09/12.
3. **Что не брать**:
   - ESP32-C3 SuperMini (плохая PCB-антенна у части партий + готового YAML нет).
   - ESP32-S2 / ESP8266 (нет BLE).
   - Безымянные клоны S3 без PSRAM или с QSPI PSRAM (поломанный baseline).

---

## 3. Инциденты

### INC-C3-001 — `OSError: [Errno 28] No space left on device` при прошивке ESP32-C3 SuperMini

- **Дата:** 2026-06-19
- **Скилл:** atomfast-esp32 (и radex-esp32 — симптом тот же)
- **Severity:** блокирует прошивку

**Симптом.** При попытке `esphome run --device COMx atomfast_gateway_s3.yaml` (или другой YAML с разметкой на 16 MB Flash) на плату **ESP32-C3 SuperMini** в логе появляется:

```
OSError: [Errno 28] No space left on device
```

Текст звучит как «кончилось место на диске PC», но на host-машине места много (десятки/сотни GB свободно). Ошибка приходит из esptool / arduino-builder / esp-idf flash tool.

**Корень.**

1. `atomfast_gateway_s3.yaml` рассчитан на **ESP32-S3-DevKitC-1 N16R8** — Flash 16 MB. Параметры `esp32.flash_size` и таблица партиций в YAML соответствуют 16 MB.
2. **ESP32-C3 SuperMini** имеет **4 MB Flash**.
3. При записи esptool проверяет `partition_table.end_offset > chip_flash_size` → `ENOSPC` (`Errno 28`).

Дополнительные факторы:
- `atomfast_gateway.yaml` (классический DevKitC) тоже не подойдёт для C3 — он под `board: esp32dev` и **Bluedroid** BLE-стек, а C3 поддерживает **только NimBLE**. Компиляция упадёт ещё до flash на этапе линковки.
- `bluetooth_proxy` + наш AtomFast `ble_client` + Web UI с 7 графиками одновременно на 400 KB SRAM C3 (без PSRAM) — даже если соберётся, рискует OOM.

**Решение.**

- **Не пытаться прошить существующие YAML на ESP32-C3** — ни `_s3.yaml`, ни классический. Они не для C3.
- Для AtomFast→HA шлюза на C3 нужен **отдельный YAML** под `board: esp32-c3-devkitm-1`, `framework: arduino`, partition `min_spiffs.csv` (даёт OTA по ~1.9 MB), NimBLE-стек, **без** `bluetooth_proxy`, упрощённый Web UI. На момент 2026-06-19 такого YAML в репо нет.
- **Рабочая альтернатива:** перейти на **ESP32-S3-DevKitC-1 N16R8** и использовать готовый `atomfast_gateway_s3.yaml`.

**Профилактика.** Перед прошивкой незнакомой платы проверь физический Flash:

```bash
esptool.py --chip esp32c3 --port COM3 flash_id
# покажет MAC, chip rev, и Detected flash size: 4MB
```

Параметр `esp32.flash_size` в YAML должен совпадать с этим значением. Несовпадение → ENOSPC при записи.

### Прочие инциденты разработки

Подробные технические разборы инцидентов **INC-05** (`json:111` на DevKitC без PSRAM), **INC-09** (esp-idf даёт `rsn 0x3e` на классическом DevKitC), **INC-12** (esp-idf coex ломает ручной `scan_parameters: 600/180`) — в [README.md](README.md), раздел «Стабильность платформ — почему ESP32-S3-DevKitC-1 N16R8, а не классический DevKitC на esp-idf».

---

## 4. Частые ошибки установки клиентов

### `Invalid key format, please check it's using base64`

Самая частая ошибка первой установки. Полный разбор — в [INSTALL.md](INSTALL.md), Troubleshooting раздел 0.

Короткий путь решения: сгенерируй валидный `api_encryption_key` командой

```bash
python -c "import secrets,base64; print(base64.b64encode(secrets.token_bytes(32)).decode())"
```

и положи в `secrets.yaml` вместо placeholder'а `0000…==`.

### Прочие 11 типовых ошибок установки

См. [INSTALL.md](INSTALL.md), раздел **Troubleshooting** — там 11 пронумерованных пунктов:
0. Invalid key format (САМАЯ ЧАСТАЯ)
1. Плата не определяется как COM-порт
2. `esphome compile` падает с `UnicodeDecodeError`
3. `esphome upload`: «Failed to connect to ESP32»
4. После прошивки плата ребутится в цикле
5. Web UI открывается, но карточек нет
6. `BLE: AtomFast подключён = OFF`
7. `WiFi: Auth Expired` каждые несколько секунд
8. `json:111: JSON document overflow` + пустой Web UI
9. OTA не работает после смены пароля
10. Команды Claude Code не запускаются с правильной кодировкой (Windows)

---

## 5. Связанные скиллы / cross-references

- [radex-esp32 KNOWN_ISSUES](https://github.com/VibeEngineering-LLC/radex-esp32/blob/main/KNOWN_ISSUES.md) — аналогичный справочник для Radex MR107ion (инцидент INC-C3-001 идентичен).
- [atomfast-esp32 README — Стабильность платформ](README.md) — полный технический разбор INC-05/09/12.
- [Issues tracker](https://github.com/VibeEngineering-LLC/atomfast-esp32/issues) — сюда репортить новые проблемы или дополнения в матрицу плат.
