(function (root, factory) {
    if (typeof module === 'object' && module.exports) {
        module.exports = factory();
    } else {
        root.AFSN = factory();
    }
})(this, function () {
    'use strict';

    // Проверка, является ли значение объектом (не null)
    function isObj(v) {
        return v !== null && typeof v === 'object';
    }

    // Проверка, является ли значение конечным числом
    function isFiniteNum(v) {
        return typeof v === 'number' && isFinite(v);
    }

    // Форматирование числа с заданной точностью
    function fmt(v, dec) {
        if (!isFiniteNum(v)) {
            return '\u2014';
        }
        var s = v.toFixed(dec);
        // Убираем отрицательный ноль
        if (s.charAt(0) === '-') {
            var allZero = true;
            for (var i = 1; i < s.length; i++) {
                if (s.charAt(i) !== '0' && s.charAt(i) !== '.') {
                    allZero = false;
                    break;
                }
            }
            if (allZero) {
                s = s.substring(1);
            }
        }
        return s;
    }

    // Получение списка показаний
    function readings(j, st) {
        var live = isObj(j) && isObj(j.live) ? j.live : null;
        var sensor = isObj(j) && isObj(j.sensor) ? j.sensor : null;

        var airT = '\u2014';
        var airP = '\u2014';
        var airH = null;

        if (sensor && sensor.status === 'ok') {
            if (sensor.temp === true) {
                airT = fmt(sensor.t_c, 1);
            }
            if (sensor.press === true) {
                airP = fmt(sensor.p_mmhg, 1);
            }
            if (sensor.hum === true) {
                airH = {
                    id: 'air_h',
                    label: 'Влажность среды',
                    value: fmt(sensor.h_pct, 1),
                    unit: '%'
                };
            }
        }

        var pulsesVal = '\u2014';
        if (live && live.has_rec === true) {
            pulsesVal = fmt(live.pulses_2s, 0);
        }

        var result = [
            { id: 'cps', label: 'CPS (мгновенный)', value: live ? fmt(live.cps, 2) : '\u2014', unit: 'имп/с' },
            { id: 'rate', label: 'Мощность дозы', value: live ? fmt(live.rate_usvh, 4) : '\u2014', unit: 'мкЗв/ч' },
            { id: 'pulses', label: 'Импульсы за 2 с', value: pulsesVal, unit: 'имп' },
            { id: 'dose', label: 'Накопленная доза', value: live ? fmt(live.dose_usv, 3) : '\u2014', unit: 'мкЗв' },
            { id: 'avg_cps', label: 'CPS (среднее за окно)', value: live ? fmt(live.avg_cps, 3) : '\u2014', unit: 'имп/с' },
            { id: 'avg_rate', label: 'Мощность дозы (среднее за окно)', value: live ? fmt(live.avg_rate_usvh, 4) : '\u2014', unit: 'мкЗв/ч' },
            { id: 'air_t', label: 'Температура среды', value: airT, unit: '\u00B0C' },
            { id: 'air_p', label: 'Давление среды', value: airP, unit: 'мм рт. ст.' }
        ];

        if (airH) {
            result.push(airH);
        }

        // Аккумулятор прибора: /api/status -> ble_last_rec.battery (проценты), как в home-model
        var batt = '—';
        if (isObj(st) && isObj(st.ble_last_rec) && typeof st.ble_last_rec.battery === 'number' && isFinite(st.ble_last_rec.battery)) {
            batt = String(Math.round(st.ble_last_rec.battery));
        }
        result.push({ id: 'battery', label: 'Заряд аккумулятора прибора', value: batt, unit: '%' });

        return result;
    }

    // Строка состояния датчика
    function sensorLine(j) {
        if (!isObj(j) || !isObj(j.sensor)) {
            return 'Нет данных от платы';
        }

        var s = j.sensor;
        var status = s.status;
        var model = s.model || '';
        var addr = s.addr || '';
        var err = s.err;

        if (status === 'ok') {
            if (isFiniteNum(s.last_ok_s_ago) && s.last_ok_s_ago >= 0) {
                var n = Math.round(s.last_ok_s_ago);
                return 'Датчик: ' + model + ' (' + addr + '), измерено ' + n + ' с назад';
            }
            return 'Датчик: ' + model + ' (' + addr + '), ожидание первого измерения';
        }
        if (status === 'no_sensor') {
            return 'Датчик среды не найден (проверьте подключение I2C)';
        }
        if (status === 'bus_error') {
            return 'Ошибка шины I2C';
        }
        if (status === 'disabled') {
            return 'Датчик выключен в настройках';
        }
        if (status === 'read_error') {
            var suffix = '';
            if (typeof err === 'string' && err.length > 0) {
                suffix = ' (' + err + ')';
            }
            return 'Датчик найден, но не отвечает' + suffix;
        }
        if (status === 'unsupported') {
            return 'Датчик ' + model + ' пока не поддерживается';
        }
        if (status === 'init') {
            return 'Датчик опрашивается\u2026';
        }
        return 'Состояние датчика неизвестно';
    }

    // Валидация конфигурации
    function validateCfg(input) {
        if (!isObj(input)) {
            return { ok: false, field: '', error: 'нет данных' };
        }

        function parseNum(v) {
            if (typeof v === 'string') {
                v = v.replace(',', '.').trim();
            }
            if (typeof v === 'number') {
                return v;
            }
            if (typeof v === 'string' && v.length > 0) {
                var n = Number(v);
                return n;
            }
            return NaN;
        }

        function checkField(val, min, max, fieldName, rangeMsg) {
            var n = parseNum(val);
            if (isNaN(n) || !isFinite(n)) {
                return { ok: false, field: fieldName, error: 'введите число' };
            }
            if (n < min || n > max) {
                return { ok: false, field: fieldName, error: rangeMsg };
            }
            return { ok: true, value: n };
        }

        // t_off
        var r1 = checkField(input.t_off, -5, 5, 't_off', 'поправка температуры: от -5 до 5 \u00B0C');
        if (!r1.ok) return r1;
        var t_off = Math.round(r1.value * 10) / 10;

        // p_off
        var r2 = checkField(input.p_off, -20, 20, 'p_off', 'поправка давления: от -20 до 20 мм рт. ст.');
        if (!r2.ok) return r2;
        var p_off = Math.round(r2.value * 10) / 10;

        // agg_s
        var r3 = checkField(input.agg_s, 1, 60, 'agg_s', 'интервал агрегации: целое от 1 до 60 с');
        if (!r3.ok) return r3;
        if (r3.value !== Math.floor(r3.value)) {
            return { ok: false, field: 'agg_s', error: 'интервал агрегации: целое от 1 до 60 с' };
        }
        var agg_s = Math.floor(r3.value);

        // period_s
        var r4 = checkField(input.period_s, 5, 600, 'period_s', 'период измерения: целое от 5 до 600 с');
        if (!r4.ok) return r4;
        if (r4.value !== Math.floor(r4.value)) {
            return { ok: false, field: 'period_s', error: 'период измерения: целое от 5 до 600 с' };
        }
        var period_s = Math.floor(r4.value);

        var enabled = (input.enabled === true || input.enabled === 'true' || input.enabled === 1);

        return {
            ok: true,
            body: {
                enabled: enabled,
                t_off: t_off,
                p_off: p_off,
                agg_s: agg_s,
                period_s: period_s
            }
        };
    }

    // Преобразование конфигурации в форму
    function cfgToForm(j) {
        if (!isObj(j) || !isObj(j.cfg)) {
            return null;
        }
        return {
            enabled: !!j.cfg.enabled,
            t_off: j.cfg.t_off,
            p_off: j.cfg.p_off,
            agg_s: j.cfg.agg_s,
            period_s: j.cfg.period_s
        };
    }

    return {
        fmt: fmt,
        readings: readings,
        sensorLine: sensorLine,
        validateCfg: validateCfg,
        cfgToForm: cfgToForm
    };
});
