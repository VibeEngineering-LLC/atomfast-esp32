# Установка прошивки AtomFast → ESP32 с нуля

Подробная пошаговая инструкция: что купить, что поставить, как прошить плату двумя разными способами и что делать, если первый раз — то всё. Не предполагает никакого предварительного опыта работы с ESPHome или Arduino.

> Краткая шпаргалка для опытных — в [`README.md`](README.md), раздел «Развёртывание (5 минут)». Этот файл — для тех, у кого ничего ещё не установлено.

---

## Содержание

1. [Что в итоге получится](#что-в-итоге-получится)
2. [Требования к оборудованию](#требования-к-оборудованию)
3. [Требования к системе и софту](#требования-к-системе-и-софту)
4. [Шаг 1: Установить драйвер USB-Serial](#шаг-1-установить-драйвер-usb-serial)
5. [Шаг 2: Установить Python и ESPHome](#шаг-2-установить-python-и-esphome)
6. [Шаг 3: Получить файлы прошивки](#шаг-3-получить-файлы-прошивки)
7. [Шаг 4: Заполнить `secrets.yaml`](#шаг-4-заполнить-secretsyaml)
8. [Шаг 5: Подставить MAC AtomFast](#шаг-5-подставить-mac-atomfast)
9. [Шаг 6: Прошить плату — выбери путь А или Б](#шаг-6-прошить-плату--выбери-путь-а-или-б)
10. [Путь А: через Claude Code](#путь-а-через-claude-code)
11. [Путь Б: через стандартный ESPHome (CLI, Web Installer, Dashboard)](#путь-б-через-стандартный-esphome)
12. [Шаг 7: Подключить плату к WiFi (captive portal)](#шаг-7-подключить-плату-к-wifi)
13. [Шаг 8: Открыть Web UI и убедиться что всё работает](#шаг-8-открыть-web-ui)
14. [Шаг 9 (опционально): Подключить к Home Assistant](#шаг-9-подключить-к-home-assistant)
15. [Troubleshooting — топ-10 проблем](#troubleshooting)

---

## Что в итоге получится

После прошивки у тебя в локальной сети появляется маленький веб-сервер `http://atomfast-gw.local/`, который:

- держит постоянную BLE-связь с дозиметром AtomFast (приложение AtomSwift на телефоне больше не нужно — но и не мешает);
- показывает живые показания радиации, накопленную дозу, температуру прибора и заряд батареи прямо в браузере, со встроенными графиками;
- (опционально) транслирует данные в Home Assistant по нативному протоколу ESPHome с шифрованием;
- (опционально и **по умолчанию выключено**) отправляет показания на народный мониторинг `narodmon.ru`.

Никакого облака. Никаких аккаунтов. Всё крутится в твоей локальной сети, исходники открыты, прошивка — твоя.

---

## Требования к оборудованию

### Дозиметр

- **AtomFast** модели Plus или Plus2 (с поддержкой Bluetooth Low Energy). Проверено на Plus2 — должно работать и на Plus. Старые «обычные» AtomFast (без BLE) **не подойдут**.
- Прибор должен быть заряжен.
- MAC-адрес прибора понадобится в Шаге 5. Узнать его можно так:
  - Открыть приложение **AtomSwift** на телефоне → подключиться к прибору → меню → информация о приборе → строка «BT address» / «MAC».
  - Или через BLE-сканер на компьютере: на Windows — [Bluetooth LE Explorer](https://apps.microsoft.com/detail/9N0ZTKF1QD98), на Linux — `bluetoothctl scan on`, на macOS — приложение «LightBlue» из App Store, на смартфоне — [nRF Connect](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp).

### Плата ESP32 — два варианта на выбор

> **Если сомневаешься — бери первый вариант (ESP32-S3-DevKitC-1 N16R8).** Это актуальная рекомендуемая платформа: стабильна на esp-idf, есть PSRAM и разъём для внешней антенны.

#### Вариант 1 — ESP32-S3-DevKitC-1 N16R8 ⭐ (рекомендуется)

Официальная плата от Espressif на чипе ESP32-S3 с маркировкой **N16R8** = 16 МБ Flash + 8 МБ embedded octal PSRAM. **Стабильная и рекомендованная** конфигурация для этого скилла.

- Чип: **ESP32-S3** (двухъядерный Xtensa LX7 @ 240 МГц + LP-ядро RISC-V).
- Flash: **16 МБ** (Quad SPI).
- **PSRAM: 8 МБ embedded octal-SPI** (нужно для веб-логов в браузере; решает INC-05 на esp-idf).
- **Wi-Fi 2.4 ГГц + Bluetooth 5 LE** (один радиочастотный фронт-энд; S3-чип имеет ровный coex, INC-09/12 на классическом DevKitC не воспроизводятся).
- **Разъём антенны: U.FL / IPEX (IPX)** — можно подключить внешнюю штыревую антенну для большей дальности до AtomFast (на платке штатно стоит керамическая чип-антенна, но её можно перепаять/переключить на IPEX-разъём 0-Ω резистором).
- USB-Serial: **USB-C** разъём «UART» (через CH343 или аналог) для прошивки + второй **USB-C** «OTG» для нативного USB прямо в чип (CDC ACM, драйвер обычно встроен в ОС). Для прошивки используй UART-разъём.
- Прошивка для этой платы: **`firmware/atomfast_gateway_s3.yaml`** (esp-idf, baseline ESPHome bluetooth-proxies upstream).
- Цена: ~700–1500 ₽ на Ali (ищи именно «**ESP32-S3-DevKitC-1 N16R8**» — буква-цифра конфигурация в названии), ~2500–3500 ₽ в радиомагазинах.

> **Не бери дешёвые S3-клоны без PSRAM или с QSPI вместо octal.** В YAML `atomfast_gateway_s3.yaml` есть блок `psram: mode: octal, speed: 80MHz` — на плате без PSRAM компиляция пройдёт, но Web UI с включённым логом упадёт в OOM при первом же F5. На N16R8 PSRAM ровно та, которую ожидает YAML — поэтому ищи маркировку **N16R8** в названии товара.

#### Вариант 2 — классический ESP32-DevKitC v4 (архивная сборка для уже имеющихся плат)

«Синяя плата с серебристым экраном» на чипе ESP32-WROOM-32 (без S3, без PSRAM). Самая распространённая ESP32-плата в мире. Под этот скилл подходит **только если плата уже есть у тебя на руках** — для новой установки рекомендуется Вариант 1.

- Чип: ESP32 (двухъядерный Tensilica Xtensa LX6 @ 240 МГц).
- Flash: 4 МБ.
- RAM: 320 КБ (**без PSRAM**).
- USB-Serial мост: **CH340** или **CP2102** (зависит от партии).
- Цена: ~300–500 ₽ на Ali, ~1500 ₽ в радиомагазинах.
- Прошивка для этой платы: **`firmware/atomfast_gateway.yaml`** (arduino-baseline; работает стабильно). **С esp-idf на этой плате — нестабильно**: INC-09 (rsn 0x3e, медленные коннекты), INC-12 (Auth Expired), INC-05 (json:111 без PSRAM). См. раздел «Стабильность фреймворков» в [README.md](README.md).

> **`atomfast_gateway_s3.yaml` на классической DevKitC НЕ работает** — там нет PSRAM, нет S3-чипа, активная esp-idf coex-подсистема ломает реконнекты. Не пробуй кросс-комбинацию: каждый YAML строго под свою плату.

### Кабель USB

- **USB → USB-C** для S3-DevKitC-1 N16R8 (современный разъём; на плате **два USB-C** — UART-разъём через CH343 для прошивки и нативный OTG-разъём в чип).
- **USB → Micro-USB** для классического DevKitC v4 (старый разъём).
- **Важно**: кабель должен быть **с данными**, не только-питание (cheap charging-only кабели не передают serial). Если у тебя в столе несколько кабелей и непонятно какой какой — пробуй кабелем от смартфона или внешнего диска, они почти всегда с данными.

### Питание после прошивки

После того как ты прошил плату, её надо где-то держать включённой 24/7. Варианты:

- USB-блок от телефона (5 V, 1 A — с запасом).
- Powerbank (ESP32 кушает ~80–120 mA в активном BLE-режиме, средний powerbank на 5000 mAh держит ~30 ч).
- Тот же ноутбук, если шлюз тебе нужен только когда ноут включён.

---

## Требования к системе и софту

### Операционная система

Работает на любой из трёх:

| ОС | Замечания |
|---|---|
| **Windows 10/11** | Самый протестированный путь (этот скилл разрабатывался на Windows 11). Нужен драйвер CH340/CP2102 (Шаг 1). |
| **Linux** (Ubuntu / Debian / Fedora / Arch) | Драйверы обычно встроены в ядро. Может потребоваться добавить юзера в группу `dialout` (`sudo usermod -aG dialout $USER` + перелогин). |
| **macOS** (Intel/Apple Silicon) | Драйверы CH340/CP2102 ставятся одной командой через Homebrew или вручную с сайта производителя. |

### Свободное место и память

- На диске: ~500 МБ под Python + ESPHome + первый билд (далее каждый билд ~150 МБ кэша, можно периодически чистить `esphome clean`).
- RAM: 4 ГБ хватит впритык; 8+ ГБ комфортно (компилятор ESP-IDF при сборке S3-прошивки временно ест ~2 ГБ).

### Софт

| Компонент | Версия | Зачем |
|---|---|---|
| **Python** | 3.10 — 3.12 | ESPHome — это Python-пакет. На Python 3.13 пока есть проблемы совместимости, лучше 3.12. |
| **ESPHome** | ≥ 2026.5 | Сама прошивка. Поставится через `pip install esphome`. |
| **Драйвер USB-Serial** | актуальный с сайта производителя моста | См. Шаг 1. |
| **Git** | любая свежая | Чтобы скачать репозиторий с прошивкой. Опционально — можно скачать ZIP с GitHub. |
| **Текстовый редактор** | любой | Notepad++, VS Code, nano, vim — для правки `secrets.yaml`. |
| **Браузер** | Chrome или Edge для Web Installer; любой для Web UI | Шаг 8 — открыть Web UI прошивки. Если планируешь Путь Б через Web Installer — нужен именно Chrome или Edge (Firefox WebSerial не поддерживает). |

---

## Шаг 1: Установить драйвер USB-Serial

Это нужно, чтобы Windows / macOS / Linux увидели плату как COM-порт. **Сначала драйвер, потом втыкай плату.**

### Какой мост на твоей плате

- **CH340 / CH341** — самый частый на дешёвых DevKitC v4 с Ali (≈80% случаев).
- **CP2102** — на «фирменных» Espressif-платах и некоторых HW-388 клонах.
- **FTDI FT232R** — редкий, на старых партиях.
- **CH343** — на новых ESP32-S3-DevKitC-1 (USB-Serial port, не USB-OTG).
- **Native USB** (ESP32-S3 OTG-разъём) — драйверы встроены в ОС, ничего ставить не надо.

Если не знаешь какой у тебя — посмотри на маленький квадратный чип рядом с USB-разъёмом, на нём будет надпись.

### Windows

| Мост | Где взять драйвер |
|---|---|
| CH340/CH341 | https://www.wch-ic.com/downloads/CH341SER_ZIP.html (страница на китайском — кнопка «Download» там единственная) |
| CP2102 | https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers |
| FTDI | https://ftdichip.com/drivers/vcp-drivers/ |
| CH343 | https://www.wch-ic.com/downloads/CH343SER_ZIP.html |

Запустить установщик от администратора → Reboot → воткнуть плату. В Диспетчере устройств появится **«Порты (COM и LPT)» → USB-SERIAL CHxxx (COMx)**.

### Linux

В современных дистрибутивах драйверы CH340/CP2102/FTDI **уже встроены в ядро**. Просто воткни плату и посмотри:

```bash
dmesg | tail -20    # должна быть строка вида:
# usb 1-1: ch341-uart converter now attached to ttyUSB0
ls -l /dev/ttyUSB* /dev/ttyACM*
```

Если порт `/dev/ttyUSB0` появился, но ESPHome не может на него писать (`Permission denied`):

```bash
sudo usermod -aG dialout $USER
# Логнуться-разлогнуться. Или временный фикс: sudo chmod 666 /dev/ttyUSB0
```

### macOS

```bash
# CH340 (homebrew)
brew install --cask wch-ch34x-usb-serial-driver
# CP2102
brew install --cask silicon-labs-vcp-driver
```

После установки перезагрузка, потом плата → `ls /dev/cu.usbserial-*`.

### Как проверить, что плата увидена

**Windows (PowerShell):**

```powershell
Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match 'CH340|CP210|FTDI|CH343' } | Select-Object Name, DeviceID
```

Должна вернуть строку с реальным COM-портом, например `USB-SERIAL CH340 (COM7)`.

**Linux:** `ls /dev/ttyUSB* /dev/ttyACM*`

**macOS:** `ls /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART`

Если ничего не показывает — проблема в драйвере или в кабеле (см. Troubleshooting).

---

## Шаг 2: Установить Python и ESPHome

### Python

| ОС | Команда / ссылка |
|---|---|
| **Windows** | https://www.python.org/downloads/ — скачать **Python 3.12**, при установке поставить галку «Add Python to PATH». |
| **Linux (Ubuntu/Debian)** | `sudo apt install python3.12 python3.12-venv python3-pip` |
| **Linux (Fedora)** | `sudo dnf install python3.12 python3-pip` |
| **Linux (Arch)** | `sudo pacman -S python python-pip` |
| **macOS** | `brew install python@3.12` |

Проверить:

```bash
python --version    # должно показать Python 3.10..3.12
pip --version
```

### ESPHome

```bash
pip install esphome
# Или, если хочешь изолированно (рекомендую):
python -m venv esphome-env
# Windows: esphome-env\Scripts\activate
# Linux/macOS: source esphome-env/bin/activate
pip install esphome
```

Проверить:

```bash
esphome version
# Должно показать что-то вроде: Version: 2026.5.3
```

> ⚠ **Windows-нюанс**: после `pip install esphome` команда `esphome` иногда не попадает в PATH. Если `esphome version` пишет «не является внутренней или внешней командой», используй полный путь — у Python 3.12 это обычно `%LOCALAPPDATA%\Programs\Python\Python312\Scripts\esphome.exe`. Можно создать алиас в PowerShell:
>
> ```powershell
> Set-Alias esphome "$env:LOCALAPPDATA\Programs\Python\Python312\Scripts\esphome.exe"
> ```

> ⚠ **Кириллица в путях (Windows)**: если у тебя имя пользователя или путь содержат русские буквы, ESPHome может упасть с `UnicodeDecodeError`. Перед запуском в той же сессии PowerShell/CMD выставь:
>
> ```powershell
> $env:PYTHONIOENCODING = 'utf-8'
> $env:PYTHONUTF8 = '1'
> ```

---

## Шаг 3: Получить файлы прошивки

### Через git (рекомендую)

```bash
git clone https://github.com/VibeEngineering-LLC/atomfast-esp32.git
cd atomfast-esp32/firmware
ls
# atomfast_gateway.yaml          ← для классического DevKitC (arduino)
# atomfast_gateway_s3.yaml       ← для ESP32-S3-DevKitC-1 (esp-idf)
# secrets.example.yaml           ← шаблон секретов
# CHANGELOG.md                   ← история версий
# archive/                       ← старые версии для отката
# include/gatt_dump.h            ← helper для GATT-дампа
```

### Без git (через ZIP)

1. Открой https://github.com/VibeEngineering-LLC/atomfast-esp32 в браузере.
2. Зелёная кнопка «Code» → «Download ZIP».
3. Распакуй, перейди в `atomfast-esp32-master/firmware/`.

---

## Шаг 4: Заполнить `secrets.yaml`

В папке `firmware/` есть файл-шаблон `secrets.example.yaml`. Скопируй его и заполни:

```bash
cp secrets.example.yaml secrets.yaml
# Windows-эквивалент: copy secrets.example.yaml secrets.yaml
```

Открой `secrets.yaml` в редакторе. Что заполнять:

| Ключ | Что туда написать |
|---|---|
| `wifi_ssid` | Имя твоей WiFi-сети **2.4 ГГц** (5 ГГц ESP32 не поддерживает!). |
| `wifi_password` | Пароль WiFi. |
| `ap_password` | Любой пароль ≥ 8 символов. Понадобится, если плата не сможет подключиться к домашней WiFi — поднимет свою AP, на неё ты зайдёшь с телефона с этим паролем. |
| `api_encryption_key` | Сгенерировать: `python -c "import secrets,base64; print(base64.b64encode(secrets.token_bytes(32)).decode())"` |
| `ota_password` | Любой пароль ≥ 8 символов. Защищает от случайной перепрошивки. |
| `web_username` / `web_password` | Логин и пароль для Web UI (Basic Auth). Используется только в arduino-прошивке. |
| `atomfast_mac` | MAC твоего AtomFast. UPPERCASE с двоеточиями: `A1:B2:C3:D4:E5:F6`. |
| `atomfast_s3_api_key` | Только если шьёшь S3-baseline. Та же команда генерации, что и `api_encryption_key`. |
| `atomfast_s3_ota_pass` | Только S3. Любой пароль ≥ 8 символов. |
| `atomfast_s3_web_pass` | Только S3. Любой пароль ≥ 8 символов. |

> ⚠ **secrets.yaml в git НЕ коммитить!** В папке уже есть `.gitignore`, который его игнорирует. Не трогай.

Альтернативный быстрый способ сгенерировать все три рандомных пароля сразу:

```bash
python -c "import secrets,base64; print('api_key:', base64.b64encode(secrets.token_bytes(32)).decode()); print('ota:', secrets.token_urlsafe(18)); print('web:', secrets.token_urlsafe(18))"
```

---

## Шаг 5: Подставить MAC AtomFast

В YAML-файле (`atomfast_gateway.yaml` ИЛИ `atomfast_gateway_s3.yaml`, в зависимости от твоей платы) есть строка:

```yaml
substitutions:
  ...
  atomfast_mac_str: "AA:BB:CC:DD:EE:FF"
```

Заменить `AA:BB:CC:DD:EE:FF` на свой реальный MAC (тот же, что в `secrets.yaml::atomfast_mac`). Эта substitution используется в лямбде для фильтра RSSI sensor'а — она сравнивает строку, поэтому нужна именно текстом, не через `!secret`.

---

## Шаг 6: Прошить плату — выбери путь А или Б

Теперь главная развилка:

- **Путь А** — попросить AI-агента (Claude Code или аналог) пройти все шаги за тебя. Удобно если ты впервые слышишь слова «ESPHome» и «COM-порт». Требует подписку Anthropic (или $5 на API-баланс) — см. ниже про бесплатную опцию.
- **Путь Б** — сделать руками через стандартный ESPHome. Бесплатно. Три варианта: CLI, ESPHome Dashboard, ESPHome Web Installer (через Chrome).

> Можно начать с А, если что-то пойдёт не так — переключиться на Б. И наоборот.

---

## Путь А: через Claude Code

### Что такое Claude Code

Claude Code — это CLI-агент от Anthropic. Запускаешь его в терминале, он умеет читать файлы, запускать команды (с твоим подтверждением), задавать вопросы. Для прошивки удобно: ты говоришь ему «прошей плату», он сам определяет COM-порт, компилирует, заливает, открывает Web UI.

### Установка Claude Code

```bash
# Требует Node.js ≥ 18. Поставить можно отсюда: https://nodejs.org/
npm install -g @anthropic-ai/claude-code

# Проверить
claude --version
```

Запустить в папке с прошивкой:

```bash
cd atomfast-esp32/firmware
claude
```

При первом запуске спросит логин — войти через **claude.ai** или ввести API-ключ с https://console.anthropic.com/.

### Доступ — что доступно бесплатно, а что нет

| Способ | Бесплатно? | Что можно |
|---|---|---|
| **Claude Code CLI с подпиской Claude Pro/Max** ($20/$100/$200 в месяц) | Нет, но включено в подписку | Полноценный агент с шагами прошивки. |
| **Claude Code CLI с API-ключом** (pay-as-you-go) | Нет; примерная стоимость одной сессии прошивки — $0.50–2 | То же. |
| **Claude.ai (веб) — бесплатный тариф** | **Да**, с дневным лимитом | Можно зайти на claude.ai, спросить совет, скопировать команды в свой терминал и выполнить вручную. **Веб-Claude не имеет доступа к твоему COM-порту**, прошивать должен ты сам. |
| **Cursor IDE с бесплатным лимитом Claude** | Да, ~2000 запросов в месяц на старте | Cursor — IDE с встроенным AI-чатом. Бесплатного лимита хватит на десятки прошивок. **Тоже не имеет прямого доступа к COM**, но может терминальные команды предложить. |
| **Aider / Continue.dev + локальная Ollama** | Полностью бесплатно | Open-source агенты, цепляются к локальной модели (например `qwen3-coder:30b`). Для шага прошивки — ОК. |

**Рекомендация**: если хочется бесплатно — открыть https://claude.ai (без подписки), вставить промпт ниже, скопировать выданные команды в свой терминал и выполнить руками. AI не запустит `esphome run` за тебя, но даст пошаговый план и поможет с ошибками. Если готов платить $20/мес или $5 на API — установить Claude Code CLI, он сделает всё сам.

### Готовый промпт для Claude Code (или для веб-Claude)

Скопируй текст ниже и вставь в чат с Claude (CLI или веб). На веб-Claude он даст пошаговый план; в CLI — реально пройдёт по шагам и предложит запустить команды.

```
У меня есть дозиметр AtomFast Plus2 и плата ESP32 — рекомендуемая
ESP32-S3-DevKitC-1 N16R8 (16 MB Flash + 8 MB embedded octal PSRAM, U.FL/IPEX,
USB-C) или, как альтернатива, классический ESP32-DevKitC v4 (если уже есть на руках).
Я хочу прошить плату прошивкой из репозитория
https://github.com/VibeEngineering-LLC/atomfast-esp32 (папка atomfast-esp32/firmware/).

Сделай по шагам, на русском, нумеруя:

1. Спроси у меня, какая у меня плата:
   - ⭐ ESP32-S3-DevKitC-1 N16R8 (чёрная плата с двумя USB-C, разъём U.FL/IPEX
     для антенны) — РЕКОМЕНДУЕТСЯ →
     прошивка atomfast_gateway_s3.yaml (esp-idf baseline bluetooth-proxies, стабильна);
   - классический ESP32-DevKitC v4 (синяя плата с micro-USB, без PSRAM) →
     прошивка atomfast_gateway.yaml (arduino-baseline; archive, только если плата
     уже есть на руках — с esp-idf нестабильна).
   Запомни выбор и используй везде дальше. Если оператор не уверен — рекомендуй S3 N16R8.

2. Проверь, что репо уже клонирован (cwd — папка firmware/). Если нет — клонируй:
   git clone https://github.com/VibeEngineering-LLC/atomfast-esp32.git
   cd atomfast-esp32/firmware

3. Проверь установлен ли ESPHome:
   esphome version
   Если нет — pip install esphome (требует Python 3.10..3.12).

4. Открой secrets.example.yaml и помоги мне заполнить secrets.yaml:
   - спроси SSID и пароль моей домашней WiFi 2.4 ГГц
     (если у меня только 5 ГГц — предупреди что ESP32 не поддерживает);
   - сгенерируй api_encryption_key через
     python -c "import secrets,base64; print(base64.b64encode(secrets.token_bytes(32)).decode())"
   - сгенерируй ota_password и web_password через
     python -c "import secrets; print(secrets.token_urlsafe(18))"
   - если выбрана S3-прошивка — сгенерируй ещё три ключа
     (atomfast_s3_api_key, atomfast_s3_ota_pass, atomfast_s3_web_pass)
   - спроси MAC моего AtomFast (формат UPPERCASE с двоеточиями); если не знаю
     — предложи открыть приложение AtomSwift → информация о приборе → BT address.
   Запиши всё в secrets.yaml. Покажи мне финальный файл (пароли можешь замаскировать).

5. Подставь MAC AtomFast в substitutions.atomfast_mac_str в выбранном YAML.

6. Определи COM-порт платы:
   - Windows (PowerShell):
     Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match 'CH340|CP210|FTDI|CH343' }
   - Linux: ls /dev/ttyUSB* /dev/ttyACM*
   - macOS: ls /dev/cu.usbserial-*
   Покажи мне список, попроси подтвердить нужный порт (на классическом DevKitC
   это обычно единственный CH340/CP2102; на S3 могут быть два — нужен тот,
   что подписан CH343 или CDC ACM).

7. Скомпилируй прошивку:
   esphome compile <выбранный-yaml>
   Если компиляция падает — расскажи мне почему и как починить. Не запускай
   upload пока компиляция не прошла.

8. Прошей:
   esphome upload <выбранный-yaml> --device <COM-порт>
   Если автоматическое срабатывание BOOT не работает (плата висит на «Connecting...
   ___...») — попроси меня зажать кнопку BOOT, нажать RESET, отпустить BOOT.

9. После прошивки — расскажи что плата сейчас сделала:
   - перезагрузилась через RTS;
   - попыталась подключиться к WiFi из secrets.yaml;
   - если не вышло — подняла AP «AtomFast-S3 Fallback» (для S3) или
     «atomfast-gw Fallback» (для arduino).
   Объясни мне, как зайти на captive portal с телефона если нужно.

10. После того как плата в сети — открой
    http://atomfast-gw.local/ (arduino) или http://atomfast-gw-s3.local/ (S3)
    в браузере. Запроси Basic Auth: username = admin (для S3) или то что я задал
    (для arduino), пароль из secrets.yaml. Помоги мне убедиться, что Web UI
    отвечает и в нём есть карточка «BLE: AtomFast подключён» = ON.

11. Если что-то идёт не так на любом шаге — сначала задай уточняющий вопрос,
    не делай destructive-операций без подтверждения (не удаляй файлы, не
    перезапускай безусловно).

Действуй. Начни с шага 1 — спроси какая у меня плата.
```

### Если ты на бесплатном веб-Claude (claude.ai без подписки)

Вставь тот же промпт, но добавь сверху:

> **Я открыл тебя в браузере (claude.ai, бесплатный тариф). У тебя нет доступа к моему компьютеру. Каждую команду показывай мне в код-блоке, я скопирую и выполню сам, потом покажу тебе результат.**

После этого работа идёт диалогом: Claude выдаёт команду — ты её копируешь — выполняешь — копируешь вывод обратно в чат. Медленнее, чем CLI-агент, но бесплатно. Для одной прошивки сессия занимает ~30 минут, лимит свободного тарифа хватит.

---

## Путь Б: через стандартный ESPHome

### Б.1 — ESPHome CLI (самый простой, рекомендую для второй прошивки)

В папке `firmware/`:

```bash
# Только проверка YAML (без компиляции):
esphome config atomfast_gateway.yaml

# Скомпилировать (займёт 3-5 минут первый раз, потом ~1 мин с кэшем):
esphome compile atomfast_gateway.yaml

# Прошить через USB:
#   Windows:
esphome upload atomfast_gateway.yaml --device COM7
#   Linux:
esphome upload atomfast_gateway.yaml --device /dev/ttyUSB0
#   macOS:
esphome upload atomfast_gateway.yaml --device /dev/cu.usbserial-0001

# Или одной командой (compile + upload):
esphome run atomfast_gateway.yaml --device COM7
```

После первой прошивки плата получит OTA-обновления **по воздуху**:

```bash
esphome run atomfast_gateway.yaml --device atomfast-gw.local
# или по IP:
esphome run atomfast_gateway.yaml --device 192.168.1.42
```

Логи (UART, **не** для arduino-прошивки на esp-idf — там DTR/RTS повесит плату):

```bash
# По воздуху (безопасно):
esphome logs atomfast_gateway.yaml --device atomfast-gw.local

# Через USB — НЕ ИСПОЛЬЗУЙ на этой прошивке!
# esphome logs --device COM7  ← DTR/RTS периодически дёргает RESET, плата перезагружается
# Если нужен UART — используй PowerShell + System.IO.Ports.SerialPort или putty с DTR=OFF.
```

### Б.2 — ESPHome Dashboard (через Home Assistant Add-on)

Если у тебя Home Assistant — поставь Add-on «ESPHome Builder», открой dashboard, кнопка «New Device» → импортируй YAML вручную. Это удобно если планируешь подключать к HA сразу.

Минусы: только для тех, у кого уже есть HA. Для standalone Web UI избыточно.

### Б.3 — ESPHome Web Installer (через Chrome, без локального Python)

> Подходит только если ты сам собираешь `.bin` локально, либо если тебе кто-то прислал готовый `.bin`. **В этом репозитории `.bin`-файлов нет** (политика безопасности — не публикуем бинарники с зашитыми чужими секретами).

Чтобы воспользоваться Web Installer, надо сначала собрать `.bin` локально через `esphome compile`. После компиляции ESPHome кладёт собранный образ сюда:

- **Windows**: `~/.esphome/build/atomfast-gw/.pioenvs/atomfast-gw/firmware.factory.bin`
- **Linux/macOS**: `~/.esphome/build/atomfast-gw/.pioenvs/atomfast-gw/firmware.factory.bin`

(для S3 — `atomfast-gw-s3` вместо `atomfast-gw`)

Дальше:

1. Открой https://web.esphome.io в **Chrome** или **Edge** (Firefox WebSerial не поддерживает).
2. Воткни плату по USB.
3. Кнопка «Connect» → выбери COM-порт.
4. «Choose File» → укажи `firmware.factory.bin`.
5. «Install».

Преимущество перед CLI: не надо больше ничего запускать на машине после первой компиляции — плату можно прошить с любого Windows-компа с Chrome. Удобно если делишься прошивкой с другом.

---

## Шаг 7: Подключить плату к WiFi

После прошивки плата перезагрузится. Дальше два сценария:

### Сценарий 1 — секреты в `secrets.yaml` правильные

Плата увидит твою WiFi, подключится, получит IP по DHCP. Через ~15 секунд после `Hard reset RTS` она уже в сети. Пингани:

```bash
ping atomfast-gw.local
# или
ping atomfast-gw-s3.local
```

Если ответ есть — переходи к Шагу 8.

### Сценарий 2 — `secrets.yaml` пустой или с опечаткой

Плата не подключится к домашней WiFi за 60 с и **поднимет свою AP**:

- SSID: `atomfast-gw Fallback` (arduino) или `AtomFast-S3 Fallback` (S3)
- пароль: `ap_password` из твоего `secrets.yaml`

С телефона/ноута подключись к этой AP — браузер автоматически откроет **captive portal** на `192.168.4.1`. Введи правильные SSID + пароль домашней WiFi → плата сохранит их в NVS, перезагрузится, в этот раз подключится.

> Если captive portal не открылся автоматически — открой в браузере явно `http://192.168.4.1/`.

> Если ты на Windows и из домашней сети `atomfast-gw.local` не пингуется — почти всегда дело в том, что mDNS заблокирован файрволом. Проверь IP через роутер (admin-панель → DHCP clients) и пингуй по IP.

---

## Шаг 8: Открыть Web UI

```
http://atomfast-gw.local/        (arduino)
http://atomfast-gw-s3.local/     (esp-idf S3)
```

Браузер спросит Basic Auth — введи `web_username`/`web_password` (для arduino) или `admin`/`atomfast_s3_web_pass` (для S3) из своего `secrets.yaml`.

Что ты должен увидеть:

- **Главная карточка**: «AtomFast — показания», в ней живые цифры — мощность дозы (µSv/h), CPS, накопленная доза, импульсы за 2 с. Цифры обновляются каждые 2 с.
- **Карточка «BLE: AtomFast подключён»** в группе «Диагностика» — должна быть **ON**. Если **OFF** — плата ещё не нашла твой AtomFast по BLE. Проверь: прибор включён, в радиусе ~10 м от платы, и **в приложении AtomSwift ты вышел из подключения** (приборы single-central — приложение и шлюз не могут держать его одновременно).
- **RSSI AtomFast**: −30 .. −80 dBm (чем ближе к 0, тем сильнее сигнал).
- **WiFi сигнал**: −40 .. −80 dBm.
- **Uptime**: считает секунды/минуты с момента boot.

Если карточка показаний пустая, а BLE OFF — значит шлюз ещё не нашёл прибор. Подожди 30 секунд. Если так и не поднимается — проверь MAC в `atomfast_mac` (typo в одной букве = тишина) и убедись что прибор не «занят» приложением.

---

## Шаг 9: Подключить к Home Assistant

(Опционально. Если HA у тебя нет — пропускай.)

В HA → Settings → Devices & Services → Add Integration → **ESPHome**:

| Поле | Значение |
|---|---|
| Host | `atomfast-gw.local` (или `atomfast-gw-s3.local`, или IP) |
| Port | `6053` (не менять) |
| Encryption key | значение `api_encryption_key` (для arduino) или `atomfast_s3_api_key` (для S3) из твоего `secrets.yaml` |

HA подхватит все сенсоры автоматически. Если хочешь — настрой автоматизации (например, push в Telegram при превышении 0.5 µSv/h).

---

## Troubleshooting

### 1. Плата не определяется как COM-порт

- Кабель only-charging? Замени на кабель с данными.
- Драйвер не поставился (Windows)? Открой Диспетчер устройств — если есть «Неизвестное устройство» с жёлтым треугольником — кликни ПКМ → Обновить драйвер → автоматический поиск. Если не помогло — ставь драйвер с сайта производителя (Шаг 1).
- На S3: USB-C разъём с одной стороны платы — это USB-OTG в чип (CDC ACM, нужен другой драйвер); с другой стороны — UART через CH343. Попробуй второй разъём.

### 2. `esphome compile` падает с `UnicodeDecodeError`

- Кириллица в имени пользователя / пути → выстави в текущей сессии:
  ```powershell
  $env:PYTHONIOENCODING = 'utf-8'
  $env:PYTHONUTF8 = '1'
  ```
- Имя пользователя на латинице, а ошибка всё равно? Проверь что в `secrets.yaml` нет кириллицы в значениях (кириллица в WiFi-пароле — это норма, в `wifi_ssid` — тоже норма, но в комментариях `secrets.example.yaml` бывают edge cases на старых Python).

### 3. `esphome upload`: «Failed to connect to ESP32: No serial data received»

- Это автоматическое срабатывание BOOT не сработало. Зажми кнопку **BOOT** на плате, нажми **RESET** (или EN), отпусти **BOOT** — и быстро запусти `esphome upload` снова. На некоторых клонах нет кнопки BOOT — нужно замкнуть GPIO0 на GND рукой во время старта.

### 4. После прошивки плата ребутится в цикле (`rst:0x1 (POWERON)` каждые 5 с)

- Скорее всего — `psram:` блок при отсутствии PSRAM на плате. Проверь: `esp32-s3-devkitc-1` ОФИЦИАЛЬНЫЙ от Espressif — с PSRAM. Дешёвые клоны с надписью «ESP32-S3-DevKitC-1» иногда без неё. Проверка: открой `Get-Help esptool.py` → `esptool.py flash_id` — покажет flash size и наличие PSRAM. Если PSRAM нет — убирай блок `psram:` из YAML.
- Или: `min_free_heap` упал в 0 — `web_server.log: true` на DevKitC без PSRAM (см. README раздел «Стабильность»). Меняй на `log: false`.

### 5. Web UI открывается, но карточек нет / пустые

- Возможно это `INC-10` — Windows-side HTTP-proxy (XRay, VPN) держит keep-alive на ESP32, исчерпывает LWIP-пул. Открой в Windows: Настройки → Сеть и интернет → Прокси → отметить «Не использовать для локальных адресов», в список добавить `192.168.*;10.*;172.16.*;localhost;127.*;*.local`. Откати браузер.
- Или: ESPHome-кэш битый. Останови сервисы, удали `~/.esphome/build/atomfast-gw/` и пересобери.

### 6. `BLE: AtomFast подключён = OFF`

- AtomSwift на телефоне всё ещё держит коннект к прибору. Закрой приложение (полностью, не просто свернуть).
- MAC в `secrets.yaml::atomfast_mac` — с опечаткой. Перепроверь, должен быть точно как в приборе, UPPERCASE с двоеточиями.
- Прибор разряжен или выключен (BLE-маяк работает только при включённом приборе).
- Расстояние > 10 м или стена через две комнаты — попробуй приблизить шлюз к прибору на эпоху проверки.

### 7. `WiFi: Auth Expired` каждые несколько секунд

- Это INC-12 (см. README раздел «Стабильность»). На esp-idf-сборках с ручными scan_parameters: ставь **640ms / 32ms** (~5% duty) или убирай блок `scan_parameters` целиком — esp-idf coex сам сбалансирует.
- На S3-baseline (`atomfast_gateway_s3.yaml`) это уже сделано (640/32) — если ты получаешь Auth Expired на S3, значит у тебя что-то с домашней WiFi (5 ГГц-only? слишком далеко?).

### 8. `json:111: JSON document overflow` в логах + пустой Web UI

- Это INC-05. Включён `web_server: log: true` на DevKitC без PSRAM при открытом браузере. На arduino `atomfast_gateway.yaml` это default = false, проблемы быть не должно.
- Если ты сам редактировал YAML и включил `log: true` на DevKitC — выключи.
- На S3-baseline `log: true` оставлен сознательно (8 МБ PSRAM, запас heap). Если на S3 всё равно json:111 — это новый кейс, открывай issue.

### 9. OTA не работает (после смены `web_password` плата не пускает)

- Это HARD-правило 2026-06-17: `web_server.auth.password` фиксируется на первой заливке. Браузер Chrome кэширует Basic Auth для realm «Login Required» и после OTA с новым паролем prompt не показывает. Лечение:
  - Открыть в Chrome `chrome://settings/passwords` → удалить запись для IP / hostname платы.
  - Или открыть URL с userinfo: `http://admin:новый_пароль@atomfast-gw-s3.local/` — Chrome обновит cached auth.
- Лучше: **не меняй web_password между прошивками одной платы.** Один раз задай, держись.

### 10. Команды Claude Code не запускаются с правильной кодировкой (Windows)

- Перед запуском в той же сессии PowerShell:
  ```powershell
  $env:PYTHONIOENCODING = 'utf-8'
  $env:PYTHONUTF8 = '1'
  ```
- В CMD:
  ```cmd
  set PYTHONIOENCODING=utf-8
  set PYTHONUTF8=1
  chcp 65001
  ```
- Эти настройки **не сохраняются** между сессиями терминала — каждый раз делай заново. Если надоело — поставь их в System Environment Variables.

---

## Что дальше

После того как Web UI работает:

- **Прочитай [`README.md`](README.md)** — там подробности про протокол AtomFast, графики в Web UI, известные ограничения.
- **Подключи к Home Assistant** (Шаг 9) — для долгосрочного хранения графиков. Web UI ESPHome хранит историю только с последнего ребута (~1 час).
- **Обновляй прошивку по воздуху** — каждое обновление этого скилла можно прошить через `esphome run --device atomfast-gw.local atomfast_gateway.yaml`. Никаких USB-кабелей после первой прошивки больше не нужно.
- **Не включай Народмон-выгрузку** без понимания политики Народмона. В YAML есть инфраструктура (switch «Выгружать на Народмон»), но он **по умолчанию выключен** через `restore_mode: ALWAYS_OFF` — даже после ребута / factory_reset / OTA вернётся в OFF. Это HARD-safety: «Народмон» — публичный сервис, неверная конфигурация скомпрометирует твой MAC и привязку к локации.

---

## Лицензия и обратная связь

MIT, всё открыто, форки приветствуются.

Баги / pull requests / новые модели AtomFast: https://github.com/VibeEngineering-LLC/atomfast-esp32/issues
