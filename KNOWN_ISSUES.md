# Known Issues & Hardware Compatibility — atomfast-esp32

Краткий справочник: какие платы мы реально проверили на AtomFast-шлюзе, какие проблемы клиенты ловят чаще всего, и что с этим делать. Длинные технические разборы инцидентов с прошлой разработки — в [README.md](README.md), раздел «Стабильность платформ».

---

## 1. Матрица совместимости плат (hardware compatibility)

> **Дисклеймер.** Работа подтверждена только на платах со статусом ✅. Для всех остальных — **гарантий нет**. Если попробовал и получилось/не получилось — открой [issue](https://github.com/VibeEngineering-LLC/atomfast-esp32/issues), добавим в матрицу.

### ✅ Протестированы и рекомендованы

| Плата | Чип | Flash | PSRAM | Антенна | YAML | Заметки |
|---|---|---|---|---|---|---|
| **ESP32-S3-DevKitC-1 N16R8** ⭐ | ESP32-S3 dual-core 240 MHz | 16 MB | 8 MB embedded octal | PCB + **U.FL/IPEX** | `esp32-s3-devkitc/atomfast_gateway_s3.yaml` (esp-idf baseline) | **Рекомендованная плата.** Стабильна на esp-idf, PSRAM решает `json:111`, U.FL позволяет внешнюю антенну (важно если AtomFast >5 м или за стеной). USB-C. |
| **ESP32-C3 SuperMini** | ESP32-C3 single-core RISC-V 160 MHz | 4 MB | — | `esp32-c3-supermini/atomfast_gateway_c3.yaml` (arduino + NimBLE) | ✅ **Smoke у клиента PASS** (v0.9.1, 2026-06-20): WiFi + Web UI + BLE-коннект к AtomFast подтверждены. Compile с v0.9.0-c3 (2026-06-19). Flash занят на **92.3 %** (1.69 МБ из 1.83 МБ) — узкий запас 142 KB на рост Web UI. Mini-форм-фактор ≈ 22×18 мм. Single-band 2.4 ГГц coex настроен через `scan_parameters: 1024/16` (duty 1.5 %). **Не пытаться** прошить S3 YAML на C3 — INC-C3-001. |
| **ESP32-WROVER-DEV** | ESP32 classic dual-core 240 MHz | 4 MB | 4/8 MB SPI | PCB | `esp32-classic/atomfast_gateway.yaml` (**только arduino**) | ✅ **Клиент подтвердил (2026-06-20)**: собирается и работает. Debug Log не виден — это by design (`log: false` в classic YAML для плат без octal PSRAM). WROVER-DEV имеет SPI PSRAM — можно безопасно включить `web_server: log: true` вручную (OOM маловероятен). Камера OV5660 на плате **не мешает** BLE/WiFi (радиотракт не зависит от GPIO камеры). |
| **ESP32-DevKitC v4 (WROOM-32)** | ESP32 classic dual-core 240 MHz | 4 MB | — | PCB | `esp32-classic/atomfast_gateway.yaml` (**только arduino**) | ⚠ Архивная сборка. arduino-стек стабилен, **esp-idf на этой плате — НЕТ** (см. INC-09/12 в README). Брать только если плата уже в руках. |

### ❌ НЕ работает / не подходит

| Плата | Причина |
|---|---|
| **ESP32-S2** | Нет BLE — физически не может быть BLE-шлюзом к AtomFast. |
| **ESP8266 (любой)** | Нет BLE. |

### ⚠ Не протестированы — на свой риск

| Плата | Ожидание |
|---|---|
| **XIAO ESP32-C3** (Seeed) | Для AtomFast не пробовали напрямую, но `esp32-c3-supermini/atomfast_gateway_c3.yaml` сделан под `board: esp32-c3-devkitm-1` с такой же 4 MB Flash без PSRAM — можно попробовать как стартовую точку (нужна верификация GPIO-маппинга и встроенного LED). |
| **ESP32-C6 / H2** | BLE 5.0 есть, NimBLE-стек поддерживается, но YAML под них в скилле не написан. |
| **ESP32-S3-DevKitC-1 N8R2 / N4R2** | Меньше Flash/PSRAM. `atomfast_gateway_s3.yaml` ориентирован на N16R8; на меньших ревизиях нужно править `flash_size`, `partition` и `psram: mode`. |
| **«Голый» ESP32-S3 без PSRAM или с QSPI PSRAM** | Меняй `psram.mode: octal` → `quad` или убирай блок целиком; `web_server.log: false`. Лучше взять оригинальную N16R8 от Espressif. |

---

## 2. Рекомендации по выбору железа

1. **Покупаешь новую плату под продакшен**: **ESP32-S3-DevKitC-1 N16R8** (Espressif оригинал). Стабильна, есть запас по памяти, есть U.FL для внешней BLE-антенны, USB-C. Прошивка — `esp32-s3-devkitc/atomfast_gateway_s3.yaml`.
2. **Уже есть классический ESP32-DevKitC v4**: можно использовать `esp32-classic/atomfast_gateway.yaml` (arduino). На esp-idf эту плату с AtomFast **не поднять** — известная регрессия INC-09/12.
3. **Нужен mini-шлюз (носимый / встройка)**: **ESP32-C3 SuperMini** с прошивкой `esp32-c3-supermini/atomfast_gateway_c3.yaml`. Compile PASS на v0.9.0-c3, **smoke у клиента ✅ на v0.9.1 (2026-06-20)**. Учти узкий запас Flash (92.3 %) и слабую PCB-антенну у части партий — для уверенного приёма прибор должен быть ≤ 5 м.
4. **Что не брать**:
   - ESP32-S2 / ESP8266 (нет BLE).
   - Безымянные клоны S3 без PSRAM или с QSPI PSRAM (поломанный baseline).

---

## 3. Инциденты

### INC-C3-001 — `OSError: [Errno 28] No space left on device` при прошивке ESP32-C3 SuperMini

- **Дата:** 2026-06-19
- **Скилл:** atomfast-esp32 (и radex-esp32 — симптом тот же)
- **Severity:** блокирует прошивку
- **Статус:** ✅ **Решено в v0.9.0-c3** — отдельный YAML `esp32-c3-supermini/atomfast_gateway_c3.yaml` под 4 MB Flash + NimBLE. Compile PASS, smoke у клиента pending.

**Симптом.** При попытке `esphome run --device COMx esp32-s3-devkitc/atomfast_gateway_s3.yaml` (или другой YAML с разметкой на 16 MB Flash) на плату **ESP32-C3 SuperMini** в логе появляется:

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

- ✅ **С v0.9.0-c3 в скилле есть отдельный YAML** `firmware/esp32-c3-supermini/atomfast_gateway_c3.yaml` под C3: `board: esp32-c3-devkitm-1`, `framework: arduino`, `bt_stack: NimBLE`, partition `min_spiffs.csv` (OTA-зона ≈ 1.9 МБ), **без** `bluetooth_proxy:`, **без** `sg_re`/`raw_atomfast`, `web_server.log: false`, `scan_parameters: 1024/16` (single-band coex). Flash занят на 92.3 % — рост Web UI требует кастомного partition CSV.
- **Не пытаться** прошить `_s3.yaml` или классический `atomfast_gateway.yaml` на ESP32-C3 — они не совместимы с C3.
- **Рабочая альтернатива** (если C3 не подходит по причине узкого Flash или слабой PCB-антенны): перейти на **ESP32-S3-DevKitC-1 N16R8** и использовать готовый `esp32-s3-devkitc/atomfast_gateway_s3.yaml`.

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
