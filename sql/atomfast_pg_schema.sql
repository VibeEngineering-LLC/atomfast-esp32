-- atomfast_pg_schema.sql: схема PostgreSQL для записи данных с платы AtomFast Gateway (ESP-IDF).
-- Прошивка: 0.1.0-e16k (схема совпадает с 0.1.0-e16j; запросы платы к БД между ними не менялись).
-- Назначение: в пустой базе создаёт таблицы, ключи и функцию af_hwid(macaddr), которые нужны плате для вставки
--   (hardware, af_raw, af_agg, af_env), а также служебные таблицы и функции приёмника (af_cursor, af_gap, af_import_log, af_ingest и др.).
-- Применение (из psql, под ролью владельца объектов):
--   psql -v ON_ERROR_STOP=1 -d <имя_базы> -f atomfast_pg_schema.sql
--   Имя базы должно совпадать с полем db в настройках записи в PostgreSQL на плате (значение по умолчанию в прошивке: af_test).
-- Идемпотентность: скрипт можно запускать повторно (IF NOT EXISTS / OR REPLACE); данные не удаляются.
-- Схема: объекты создаются в схеме по умолчанию (search_path, обычно public): плата пишет имена таблиц без схемы.
-- Целевые версии: PostgreSQL 14 и новее (в разделе представлений используется date_trunc с тремя аргументами).
-- Роли: скрипт создаёт группы af_writer и af_reader без права входа и без пароля; для этого нужно право CREATEROLE.
--   Роль, под которой плата подключается к БД, создаёт администратор и включает в af_writer: GRANT af_writer TO <роль_платы>;
-- Секретов, хостов, адресов и реальных MAC в файле нет.
BEGIN;

-- 1. Таблица приборов: MAC прибора (дозиметра, не шлюза) -> целочисленный id.
CREATE TABLE IF NOT EXISTS hardware (
    id         integer     GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    mac        macaddr     NOT NULL UNIQUE,   -- MAC прибора (дозиметра), не шлюза
    created_at timestamptz NOT NULL DEFAULT now()
);

-- 2. Данные: сырые измерения (af_raw) и агрегаты по окнам (af_agg). Колонки строки приходят из CSV платы (main/pg_csv.c).
-- Единицы: doserate мкЗв/ч, dose мЗв (имена полей экспортного CSV: doserate_usvh, dose_msv).
-- Соответствие колонок полям CSV: c2s - импульсы за 2 с (pulses_2s в экспортном CSV), t - temp_c, charge - battery.
-- Ключ идемпотентности (hwid, ring, gen, seq): epoch и ts в ключ НЕ входят (epoch растёт на каждой
-- загрузке при неизменных seq; соседние сырые строки могут повторять ts).
-- Порядок колонок: сначала 8-байтовые, затем 4-, затем 2-байтовые (минимум выравнивания).
CREATE TABLE IF NOT EXISTS af_raw (
    seq         bigint           NOT NULL,
    ts          timestamptz      NOT NULL,                  -- время платы, UTC; CSV: unix-секунды
    inserted_at timestamptz      NOT NULL DEFAULT now(),
    epoch       bigint           NOT NULL,                  -- эпоха кольца в момент чтения (справочно, НЕ ключ)
    hwid        integer          NOT NULL REFERENCES hardware(id),
    gen         integer          NOT NULL DEFAULT 0,        -- поколение истории (задаёт загрузчик при откате seq)
    doserate    real             NOT NULL,                  -- мкЗв/ч
    dose        real             NOT NULL,                  -- мЗв, накопленная
    c2s         real             NOT NULL,                  -- импульсы за 2 с (raw: u16; agg: среднее/последнее)
    cps         real             NOT NULL,                  -- имп/с = c2s / 2
    n           integer          NOT NULL DEFAULT 1,        -- число отсчётов (raw = 1)
    ring        smallint         NOT NULL DEFAULT 0,        -- 0 сырые, 1 агрегированные (отдельное пространство seq)
    kind        smallint         NOT NULL DEFAULT 0,        -- 0 сырые, иначе секунд усреднения
    charge      smallint         NOT NULL,                  -- battery, int8
    t           smallint         NOT NULL,                  -- temp_c, int8, °C
    flags       smallint         NOT NULL DEFAULT 0,        -- флаги прибора (поле flags CSV)
    CONSTRAINT af_raw_pk      PRIMARY KEY (hwid, ring, gen, seq),
    CONSTRAINT af_raw_ring    CHECK (ring = 0),
    CONSTRAINT af_raw_seq     CHECK (seq BETWEEN 0 AND 4294967295),
    CONSTRAINT af_raw_epoch   CHECK (epoch BETWEEN 0 AND 4294967295),
    CONSTRAINT af_raw_ts      CHECK (ts >= timestamptz '2020-01-01 00:00:00+00' AND ts < timestamptz '2100-01-01 00:00:00+00'),
    CONSTRAINT af_raw_kind    CHECK (kind = 0),
    CONSTRAINT af_raw_n       CHECK (n = 1),
    CONSTRAINT af_raw_doserate CHECK (doserate >= 0 AND doserate <= 1000),
    CONSTRAINT af_raw_dose    CHECK (dose >= 0),
    CONSTRAINT af_raw_c2s     CHECK (c2s >= 0 AND c2s <= 65535),
    CONSTRAINT af_raw_cps     CHECK (cps >= 0 AND cps <= 32767.5),
    CONSTRAINT af_raw_charge  CHECK (charge BETWEEN -128 AND 127),
    CONSTRAINT af_raw_t       CHECK (t BETWEEN -128 AND 127),
    CONSTRAINT af_raw_flags   CHECK (flags BETWEEN 0 AND 255)
);
CREATE INDEX IF NOT EXISTS af_raw_ts_idx ON af_raw (hwid, ts);

CREATE TABLE IF NOT EXISTS af_agg (
    seq         bigint           NOT NULL,
    ts          timestamptz      NOT NULL,                  -- начало окна
    inserted_at timestamptz      NOT NULL DEFAULT now(),
    epoch       bigint           NOT NULL,
    hwid        integer          NOT NULL REFERENCES hardware(id),
    gen         integer          NOT NULL DEFAULT 0,
    doserate    real             NOT NULL,
    dose        real             NOT NULL,
    c2s         real             NOT NULL,
    cps         real             NOT NULL,
    n           integer          NOT NULL,                  -- отсчётов в окне
    ring        smallint         NOT NULL DEFAULT 1,
    kind        smallint         NOT NULL,                  -- секунд усреднения (окно 60..1200 с, кратно 60)
    charge      smallint         NOT NULL,
    t           smallint         NOT NULL,
    flags       smallint         NOT NULL DEFAULT 0,
    CONSTRAINT af_agg_pk      PRIMARY KEY (hwid, ring, gen, seq),
    CONSTRAINT af_agg_ring    CHECK (ring = 1),
    CONSTRAINT af_agg_seq     CHECK (seq BETWEEN 0 AND 4294967295),
    CONSTRAINT af_agg_epoch   CHECK (epoch BETWEEN 0 AND 4294967295),
    CONSTRAINT af_agg_ts      CHECK (ts >= timestamptz '2020-01-01 00:00:00+00' AND ts < timestamptz '2100-01-01 00:00:00+00'),
    CONSTRAINT af_agg_kind    CHECK (kind BETWEEN 60 AND 1200 AND kind % 60 = 0),
    CONSTRAINT af_agg_n       CHECK (n BETWEEN 1 AND 65535),
    CONSTRAINT af_agg_doserate CHECK (doserate >= 0 AND doserate <= 1000),
    CONSTRAINT af_agg_dose    CHECK (dose >= 0),
    CONSTRAINT af_agg_c2s     CHECK (c2s >= 0 AND c2s <= 65535),
    CONSTRAINT af_agg_cps     CHECK (cps >= 0 AND cps <= 32767.5),
    CONSTRAINT af_agg_charge  CHECK (charge BETWEEN -128 AND 127),
    CONSTRAINT af_agg_t       CHECK (t BETWEEN -128 AND 127),
    CONSTRAINT af_agg_flags   CHECK (flags BETWEEN 0 AND 255)
);
CREATE INDEX IF NOT EXISTS af_agg_ts_idx ON af_agg (hwid, ts);

-- 3. Датчики среды (температура, давление, влажность). Кольцо среды различается колонкой ring = 2; колонки kind у среды нет.
-- Давление хранится в паскалях (press_pa); мм рт. ст. в БД не хранится (производная: press_pa / 133.322387).
-- Диапазоны: температура -40..85 C, давление 29 900..110 100 Па, влажность 0..100 %.
-- Несколько датчиков в приборе: sensor 1..8, model из bmp280|bme280|sht3x|aht2x.
-- Биты flags: 0 температура, 1 давление, 2 коррекция, 3 влажность (значения 1, 2, 4, 8).
-- Допустимы флаги 1..15 с хотя бы одним из битов 0/1/3 (маска 11).
-- Ключ идемпотентности (hwid, ring, gen, seq); seq общий на кольцо, датчики чередуются.

CREATE TABLE IF NOT EXISTS af_env (
    seq bigint NOT NULL,
    ts timestamptz NOT NULL,
    inserted_at timestamptz NOT NULL DEFAULT now(),
    epoch bigint NOT NULL,
    hwid integer NOT NULL REFERENCES hardware(id),
    gen integer NOT NULL DEFAULT 0,
    press_pa integer NULL,
    temp_c real NULL,
    hum_pct real NULL,
    model text NOT NULL DEFAULT 'bmp280',
    ring smallint NOT NULL DEFAULT 2,
    sensor smallint NOT NULL DEFAULT 1,
    flags smallint NOT NULL,
    CONSTRAINT af_env_pk PRIMARY KEY (hwid, ring, gen, seq),
    CONSTRAINT af_env_ring CHECK (ring = 2),
    CONSTRAINT af_env_seq CHECK (seq BETWEEN 0 AND 4294967295),
    CONSTRAINT af_env_ts CHECK (ts >= timestamptz '2020-01-01 00:00:00+00' AND ts < timestamptz '2100-01-01 00:00:00+00'),
    CONSTRAINT af_env_t CHECK (temp_c IS NULL OR (temp_c >= -40 AND temp_c <= 85)),
    CONSTRAINT af_env_p CHECK (press_pa IS NULL OR (press_pa >= 29900 AND press_pa <= 110100))
);

DO $$
BEGIN
    ALTER TABLE af_env ADD COLUMN IF NOT EXISTS sensor smallint NOT NULL DEFAULT 1;
    ALTER TABLE af_env ADD COLUMN IF NOT EXISTS model text NOT NULL DEFAULT 'bmp280';
    ALTER TABLE af_env ADD COLUMN IF NOT EXISTS hum_pct real NULL;

    ALTER TABLE af_env DROP CONSTRAINT IF EXISTS af_env_sensor;
    ALTER TABLE af_env ADD CONSTRAINT af_env_sensor CHECK (sensor BETWEEN 1 AND 8);

    ALTER TABLE af_env DROP CONSTRAINT IF EXISTS af_env_model;
    ALTER TABLE af_env ADD CONSTRAINT af_env_model CHECK (model IN ('bmp280','bme280','sht3x','aht2x'));

    ALTER TABLE af_env DROP CONSTRAINT IF EXISTS af_env_hum;
    ALTER TABLE af_env ADD CONSTRAINT af_env_hum CHECK (hum_pct IS NULL OR (hum_pct >= 0 AND hum_pct <= 100));

    ALTER TABLE af_env DROP CONSTRAINT IF EXISTS af_env_flags;
    ALTER TABLE af_env ADD CONSTRAINT af_env_flags CHECK (flags BETWEEN 1 AND 15 AND (flags & 11) <> 0);

    ALTER TABLE af_env DROP CONSTRAINT IF EXISTS af_env_null;
    ALTER TABLE af_env ADD CONSTRAINT af_env_null CHECK (
        (temp_c IS NOT NULL) = ((flags & 1) = 1) AND
        (press_pa IS NOT NULL) = ((flags & 2) = 2) AND
        (hum_pct IS NOT NULL) = ((flags & 8) = 8)
    );
END $$;

CREATE INDEX IF NOT EXISTS af_env_ts_idx ON af_env (hwid, ts);
CREATE INDEX IF NOT EXISTS af_env_sensor_ts_idx ON af_env (hwid, sensor, ts);
-- 4. Курсор приёмника: позиция выгрузки по (hwid, ring); обновляется в той же транзакции, что и данные.
CREATE TABLE IF NOT EXISTS af_cursor (
    hwid       integer     NOT NULL REFERENCES hardware(id),
    ring       smallint    NOT NULL CHECK (ring IN (0, 1, 2)),
    gen        integer     NOT NULL DEFAULT 0,
    next_seq   bigint      NOT NULL CHECK (next_seq BETWEEN 0 AND 4294967296),
    epoch      bigint      NOT NULL,
    updated_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (hwid, ring)
);

-- Дыры: вытеснено до выгрузки (lost), откат seq, пропуск строки по CRC.
CREATE TABLE IF NOT EXISTS af_gap (
    id          bigint      GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    hwid        integer     NOT NULL REFERENCES hardware(id),
    ring        smallint    NOT NULL CHECK (ring IN (0, 1, 2)),
    gen         integer     NOT NULL,
    seq_from    bigint      NOT NULL,
    seq_to      bigint      NULL,                              -- включительно; NULL = неизвестно
    reason      text        NOT NULL CHECK (reason IN ('evicted', 'regression', 'crc_skipped', 'ts_unknown', 'timeout', 'unknown')),
    detected_at timestamptz NOT NULL DEFAULT now(),
    note        text        NULL
);

-- Журнал загрузок (одна строка на пачку).
CREATE TABLE IF NOT EXISTS af_import_log (
    id          bigint      GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    hwid        integer     NOT NULL REFERENCES hardware(id),
    ring        smallint    NOT NULL CHECK (ring IN (0, 1, 2)),
    gen         integer     NOT NULL,
    epoch       bigint      NOT NULL,
    seq_from    bigint      NOT NULL,
    seq_to      bigint      NULL,
    rows_in     bigint      NOT NULL,
    rows_new    bigint      NOT NULL,
    finished_at timestamptz NOT NULL DEFAULT now(),
    note        text        NULL
);

-- 5. Функции.
CREATE OR REPLACE FUNCTION af_hwid(p_mac macaddr) RETURNS integer
LANGUAGE plpgsql AS $$
DECLARE
    v integer;
BEGIN
    SELECT id INTO v FROM hardware WHERE mac = p_mac;
    IF v IS NULL THEN
        INSERT INTO hardware (mac) VALUES (p_mac) ON CONFLICT (mac) DO NOTHING;
        SELECT id INTO v FROM hardware WHERE mac = p_mac;
    END IF;
    RETURN v;
END $$;

-- Временная таблица приёма: CSV платы грузится в неё как есть (COPY ... FROM STDIN CSV HEADER), без сборки SQL из текста.
-- ord нумерует строки в порядке поступления; seq строки = p_first_seq + (ord - min(ord)).
CREATE OR REPLACE FUNCTION af_stage_prepare() RETURNS void
LANGUAGE plpgsql AS $$
BEGIN
    CREATE TEMP TABLE IF NOT EXISTS af_stage (
        ord           bigint GENERATED ALWAYS AS IDENTITY,
        ts            bigint NOT NULL,
        kind          integer NOT NULL,
        dose_msv      real NOT NULL,
        doserate_usvh real NOT NULL,
        pulses_2s     real NOT NULL,
        cps           real NOT NULL,
        battery       integer NOT NULL,
        temp_c        integer NOT NULL,
        flags         integer NOT NULL,
        n             integer NOT NULL
    ) ON COMMIT DELETE ROWS;
    DELETE FROM af_stage;
END $$;

-- Временная таблица приёма среды: CSV среды грузится в неё через COPY, затем af_ingest(2, ...).
CREATE OR REPLACE FUNCTION af_stage_env_prepare() RETURNS void
LANGUAGE plpgsql AS $$
BEGIN
    CREATE TEMP TABLE IF NOT EXISTS af_stage_env (
        ord bigint GENERATED ALWAYS AS IDENTITY,
        ts bigint NOT NULL,
        sensor integer NOT NULL,
        model text NOT NULL,
        temp_c real NULL,
        hum_pct real NULL,
        press_pa integer NULL,
        flags integer NOT NULL
    ) ON COMMIT DELETE ROWS;

    DELETE FROM af_stage_env;
END $$;

-- Перенос из af_stage в af_raw (p_ring = 0) или af_agg (p_ring = 1); из af_stage_env в af_env (p_ring = 2).
-- Повтор безопасен: ON CONFLICT DO NOTHING по ключу. Нарушение CHECK/типа откатывает всю транзакцию (пачка целиком, курсор не двигается).
-- Строки с ts = 0 (время платы неизвестно) в БД не грузятся, но занимают номер seq; их число - rows_ts0.
CREATE OR REPLACE FUNCTION af_ingest(
    p_ring smallint,
    p_hwid integer,
    p_gen integer,
    p_epoch bigint,
    p_first_seq bigint
) RETURNS TABLE (
    rows_in bigint,
    rows_new bigint,
    rows_ts0 bigint,
    seq_min bigint,
    seq_max bigint
)
LANGUAGE plpgsql AS $$
DECLARE
    v_in bigint;
    v_ts0 bigint;
    v_new bigint;
    v_ord0 bigint;
BEGIN
    IF p_ring NOT IN (0, 1, 2) THEN
        RAISE EXCEPTION 'af_ingest: bad ring %', p_ring USING ERRCODE = '22023';
    END IF;

    IF p_ring = 2 THEN
        SELECT count(*), count(*) FILTER (WHERE ts = 0), min(ord)
        INTO v_in, v_ts0, v_ord0
        FROM af_stage_env;

        INSERT INTO af_env (seq, ts, epoch, hwid, gen, sensor, model, temp_c, hum_pct, press_pa, ring, flags)
        SELECT
            p_first_seq + (s.ord - v_ord0),
            to_timestamp(s.ts),
            p_epoch,
            p_hwid,
            p_gen,
            s.sensor,
            s.model,
            s.temp_c,
            s.hum_pct,
            s.press_pa,
            2,
            s.flags
        FROM af_stage_env s
        WHERE s.ts <> 0
        ON CONFLICT (hwid, ring, gen, seq) DO NOTHING;

        GET DIAGNOSTICS v_new = ROW_COUNT;
    ELSE
        SELECT count(*), count(*) FILTER (WHERE ts = 0), min(ord)
        INTO v_in, v_ts0, v_ord0
        FROM af_stage;

        IF p_ring = 0 THEN
            INSERT INTO af_raw (seq, ts, epoch, hwid, gen, doserate, dose, c2s, cps, n, ring, kind, charge, t, flags)
            SELECT
                p_first_seq + (s.ord - v_ord0),
                to_timestamp(s.ts),
                p_epoch,
                p_hwid,
                p_gen,
                s.doserate_usvh,
                s.dose_msv,
                s.pulses_2s,
                s.cps,
                s.n,
                0,
                s.kind,
                s.battery,
                s.temp_c,
                s.flags
            FROM af_stage s
            WHERE s.ts <> 0
            ON CONFLICT (hwid, ring, gen, seq) DO NOTHING;
        ELSE
            INSERT INTO af_agg (seq, ts, epoch, hwid, gen, doserate, dose, c2s, cps, n, ring, kind, charge, t, flags)
            SELECT
                p_first_seq + (s.ord - v_ord0),
                to_timestamp(s.ts),
                p_epoch,
                p_hwid,
                p_gen,
                s.doserate_usvh,
                s.dose_msv,
                s.pulses_2s,
                s.cps,
                s.n,
                1,
                s.kind,
                s.battery,
                s.temp_c,
                s.flags
            FROM af_stage s
            WHERE s.ts <> 0
            ON CONFLICT (hwid, ring, gen, seq) DO NOTHING;
        END IF;

        GET DIAGNOSTICS v_new = ROW_COUNT;
    END IF;

    RETURN QUERY SELECT v_in, v_new, v_ts0, p_first_seq, p_first_seq + v_in - 1;
END $$;
-- Продвижение курсора: только вперёд внутри поколения; новое поколение допускается всегда с большим номером gen.
CREATE OR REPLACE FUNCTION af_commit_cursor(p_hwid integer, p_ring smallint, p_gen integer, p_next_seq bigint, p_epoch bigint)
RETURNS void
LANGUAGE plpgsql AS $$
DECLARE
    v_rows bigint;
BEGIN
    INSERT INTO af_cursor AS c (hwid, ring, gen, next_seq, epoch)
    VALUES (p_hwid, p_ring, p_gen, p_next_seq, p_epoch)
    ON CONFLICT (hwid, ring) DO UPDATE
       SET gen = EXCLUDED.gen, next_seq = EXCLUDED.next_seq, epoch = EXCLUDED.epoch, updated_at = now()
     WHERE EXCLUDED.gen > c.gen OR (EXCLUDED.gen = c.gen AND EXCLUDED.next_seq >= c.next_seq);
    GET DIAGNOSTICS v_rows = ROW_COUNT;
    IF v_rows = 0 THEN
        RAISE EXCEPTION 'af_commit_cursor: cursor would move backwards (hwid %, ring %, gen %, next_seq %)',
            p_hwid, p_ring, p_gen, p_next_seq USING ERRCODE = '22023';
    END IF;
END $$;

-- Политика хранения: сырые старше p_raw_days суток удаляются; агрегированные - если задан p_agg_days (иначе хранятся всегда).
CREATE OR REPLACE FUNCTION af_purge(p_raw_days integer, p_agg_days integer DEFAULT NULL)
RETURNS TABLE (raw_deleted bigint, agg_deleted bigint)
LANGUAGE plpgsql AS $$
DECLARE
    v_raw bigint := 0;
    v_agg bigint := 0;
BEGIN
    IF p_raw_days IS NULL OR p_raw_days < 1 OR (p_agg_days IS NOT NULL AND p_agg_days < 1) THEN
        RAISE EXCEPTION 'af_purge: days must be >= 1' USING ERRCODE = '22023';
    END IF;
    DELETE FROM af_raw WHERE ts < now() - make_interval(days => p_raw_days);
    GET DIAGNOSTICS v_raw = ROW_COUNT;
    IF p_agg_days IS NOT NULL THEN
        DELETE FROM af_agg WHERE ts < now() - make_interval(days => p_agg_days);
        GET DIAGNOSTICS v_agg = ROW_COUNT;
    END IF;
    RETURN QUERY SELECT v_raw, v_agg;
END $$;

-- 6. Представления для графиков (обычные; при росте данных - материализовать).
CREATE OR REPLACE VIEW af_raw_hourly AS
SELECT hwid,
       date_trunc('hour', ts, 'UTC')            AS hour_utc,
       count(*)                                 AS samples,
       avg(doserate::double precision)          AS doserate_avg,
       min(doserate)                            AS doserate_min,
       max(doserate)                            AS doserate_max,
       avg(cps::double precision)               AS cps_avg,
       (array_agg(dose ORDER BY ts DESC, seq DESC))[1] AS dose_last
  FROM af_raw
 GROUP BY hwid, date_trunc('hour', ts, 'UTC');

-- Среднее по окнам, взвешенное числом отсчётов n (среднее средних без весов смещено на неполных окнах).
CREATE OR REPLACE VIEW af_agg_hourly AS
SELECT hwid,
       kind,
       date_trunc('hour', ts, 'UTC')            AS hour_utc,
       count(*)                                 AS windows,
       sum(n)::bigint                           AS samples,
       sum(n::double precision * doserate) / sum(n) AS doserate_avg,
       sum(n::double precision * cps) / sum(n)  AS cps_avg,
       (array_agg(dose ORDER BY ts DESC, seq DESC))[1] AS dose_last
  FROM af_agg
 GROUP BY hwid, kind, date_trunc('hour', ts, 'UTC');

-- Почасовые средние среды по датчику. avg пропускает NULL (датчик без величины не тянет среднее к нулю);
-- temp_n/hum_n/press_n - число отсчётов, вошедших в среднее; model в группировке: замена датчика под тем же номером не смешивает модели.
CREATE OR REPLACE VIEW af_env_hourly AS
SELECT hwid, sensor, model,
       date_trunc('hour', ts, 'UTC')            AS hour_utc,
       count(*)                                 AS samples,
       avg(temp_c::double precision)            AS temp_avg,
       avg(hum_pct::double precision)           AS hum_avg,
       avg(press_pa::double precision)          AS press_pa_avg,
       count(temp_c)                            AS temp_n,
       count(hum_pct)                           AS hum_n,
       count(press_pa)                          AS press_n
  FROM af_env
 GROUP BY hwid, sensor, model, date_trunc('hour', ts, 'UTC');

-- Подозрение на повторный импорт после ложного поколения: одинаковое содержимое под разными gen.
CREATE OR REPLACE VIEW af_dup_suspects AS
SELECT 'raw'::text AS tbl, hwid, ts, count(*) AS copies, count(DISTINCT gen) AS gens
  FROM af_raw GROUP BY hwid, ts, doserate, dose, c2s, charge, t HAVING count(DISTINCT gen) > 1
UNION ALL
SELECT 'agg'::text, hwid, ts, count(*), count(DISTINCT gen)
  FROM af_agg GROUP BY hwid, ts, kind, doserate, dose, c2s, charge, t HAVING count(DISTINCT gen) > 1;

-- 7. Роли (группы без входа; логин-роли создаёт администратор отдельно и выдаёт членство - пароль в файл не пишется).
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'af_writer') THEN
        CREATE ROLE af_writer NOLOGIN;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'af_reader') THEN
        CREATE ROLE af_reader NOLOGIN;
    END IF;
END $$;
-- af_stage_prepare() создаёт временную таблицу: ролям записи нужно право TEMPORARY на базу (у PUBLIC оно может быть отозвано).
DO $$ BEGIN EXECUTE format('GRANT TEMPORARY ON DATABASE %I TO af_writer', current_database()); END $$;
-- Права задаются целиком, а не накоплением GRANT (иначе лишнее право, выданное когда-то, переживает перезаливку схемы).
REVOKE ALL ON hardware, af_raw, af_agg, af_env, af_cursor, af_gap, af_import_log,
              af_raw_hourly, af_agg_hourly, af_env_hourly, af_dup_suspects FROM af_writer, af_reader;
GRANT SELECT, INSERT         ON hardware, af_raw, af_agg, af_env, af_gap, af_import_log TO af_writer;
GRANT SELECT, INSERT, UPDATE ON af_cursor TO af_writer;
GRANT SELECT ON hardware, af_raw, af_agg, af_env, af_cursor, af_gap, af_import_log,
                af_raw_hourly, af_agg_hourly, af_env_hourly, af_dup_suspects TO af_reader;

COMMIT;
