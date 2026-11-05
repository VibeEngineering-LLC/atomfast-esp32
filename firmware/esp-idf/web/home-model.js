(function (root, factory) {
    if (typeof module === 'object' && module.exports) { module.exports = factory(require('./ui-common.js')); }
    else { root.AFHM = factory(root.AFU); }
})(this, function (AFU) {
    'use strict';

    var DASH = '—';

    function isObj(v) {
        return v !== null && typeof v === 'object' && !Array.isArray(v);
    }

    function sensorCard(s, idx) {
        if (!isObj(s)) {
            s = {};
        }

        var MODEL_NAMES = { bmp280: 'BMP280', bme280: 'BME280', sht3x: 'SHT3x', aht2x: 'AHT2x' };
        var model = (typeof s.model === 'string' && s.model !== '') ? s.model : 'Датчик';
        if (Object.prototype.hasOwnProperty.call(MODEL_NAMES, model)) {
            model = MODEL_NAMES[model];
        }
        var addr = (typeof s.addr === 'string') ? s.addr : '';
        var title = model + (addr ? ' (' + addr + ')' : '');
        var name = (typeof s.name === 'string') ? s.name : '';
        var number = (typeof s.index === 'number' && isFinite(s.index)) ? s.index : idx + 1;
        if (typeof s.sensor === 'number' && s.sensor >= 1 && s.sensor <= 8 && Math.floor(s.sensor) === s.sensor) {
            number = s.sensor;
        }
        var enabled = !(s.enabled === false || s.status === 'off');

        var level = 'mut';
        var statusText = 'состояние неизвестно';
        var status = s.status;

        if (status === 'ok') {
            level = 'ok';
            statusText = 'работает';
        } else if (status === 'off') {
            level = 'mut';
            statusText = 'выключен';
        } else if (status === 'err') {
            level = 'warn';
            statusText = 'ошибка';
        } else if (status === 'init') {
            level = 'mut';
            statusText = 'опрашивается';
        } else if (status === 'disabled') {
            level = 'mut';
            statusText = 'выключен';
        } else if (status === 'no_sensor') {
            level = 'warn';
            statusText = 'не найден';
        } else if (status === 'bus_error') {
            level = 'bad';
            statusText = 'ошибка шины I2C';
        } else if (status === 'read_error') {
            level = 'bad';
            statusText = 'не отвечает';
        } else if (status === 'unsupported') {
            level = 'warn';
            statusText = 'не поддерживается';
        }

        var values = [];
        if (status === 'ok') {
            if (s.temp === true) {
                values.push({
                    id: 't',
                    label: 'Температура',
                    value: AFU.fmtNum(s.t_c, 1),
                    unit: '°C'
                });
            }
            if (s.press === true) {
                values.push({
                    id: 'p',
                    label: 'Давление',
                    value: AFU.fmtNum(s.p_mmhg, 1),
                    unit: 'мм рт. ст.'
                });
            }
            if (s.hum === true) {
                values.push({
                    id: 'h',
                    label: 'Влажность',
                    value: AFU.fmtNum(s.h_pct, 1),
                    unit: '%'
                });
            }
        }

        var ageText = '';
        if (status === 'ok' && typeof s.last_ok_s_ago === 'number' && isFinite(s.last_ok_s_ago) && s.last_ok_s_ago >= 0) {
            ageText = 'измерено ' + Math.round(s.last_ok_s_ago) + ' с назад';
        }

        return {
            id: 'sensor' + idx,
            model: model,
            addr: addr,
            title: title,
            name: name,
            number: number,
            enabled: enabled,
            level: level,
            statusText: statusText,
            values: values,
            ageText: ageText
        };
    }

    function sensorCards(sn) {
        var result = [];

        if (isObj(sn) && Array.isArray(sn.sensors)) {
            for (var i = 0; i < sn.sensors.length; i++) {
                var el = sn.sensors[i];
                var sensorObj = null;

                if (isObj(el) && isObj(el.sensor)) {
                    sensorObj = el.sensor;
                } else if (isObj(el)) {
                    sensorObj = el;
                }

                if (sensorObj !== null) {
                    result.push(sensorCard(sensorObj, result.length));
                }
            }
        } else if (isObj(sn) && isObj(sn.sensor)) {
            result.push(sensorCard(sn.sensor, 0));
        }

        return result;
    }

    function groupByModel(cards) {
        if (!Array.isArray(cards)) {
            return [];
        }

        var groups = [];
        var map = Object.create(null); // WEB-01: 'constructor'/'__proto__' не должны совпасть с наследованными

        for (var i = 0; i < cards.length; i++) {
            var card = cards[i];
            if (!isObj(card)) {
                continue;
            }

            var model = card.model;
            if (typeof model !== 'string') {
                continue;
            }

            if (!map[model]) {
                map[model] = {
                    model: model,
                    cards: []
                };
                groups.push(map[model]);
            }

            map[model].cards.push(card);
        }

        return groups;
    }

    function homeModel(st, sn) {
        var chips = {};

        // BLE
        if (st && st.ble_connected === true) {
            chips.ble = { level: 'ok', text: 'Дозиметр: подключён' };
        } else if (st && st.ble_ready === true) {
            chips.ble = { level: 'warn', text: 'Дозиметр: ищет прибор' };
        } else {
            chips.ble = { level: 'bad', text: 'Дозиметр: нет связи' };
        }

        // WiFi
        if (st && st.wifi_connected === true) {
            chips.wifi = { level: 'ok', text: 'Wi-Fi: подключён' };
        } else {
            chips.wifi = { level: 'bad', text: 'Wi-Fi: нет' };
        }

        // MQTT
        var m = (st && isObj(st.mqtt)) ? st.mqtt : null;
        if (!m) {
            chips.mqtt = { level: 'mut', text: 'MQTT: нет данных' };
        } else if (!m.enabled) {
            chips.mqtt = { level: 'mut', text: 'MQTT: выключен' };
        } else if (m.connected) {
            chips.mqtt = { level: 'ok', text: 'MQTT: подключён' };
        } else {
            chips.mqtt = { level: 'warn', text: 'MQTT: не подключён' };
        }

        // Time
        if (st && st.sntp_synced === true) {
            chips.time = { level: 'ok', text: 'Время: SNTP' };
        } else if (st && typeof st.time_source === 'string' && st.time_source !== 'none' && st.time_source !== '') {
            chips.time = { level: 'warn', text: 'Время: ' + st.time_source };
        } else {
            chips.time = { level: 'bad', text: 'Время: не задано' };
        }

        // FW
        var fw = (st && typeof st.fw_version === 'string') ? st.fw_version : DASH;

        // Live
        var live = (isObj(sn) && isObj(sn.live)) ? sn.live : null;
        var hasRec = (live !== null && live.has_rec === true);

        var rate = hasRec ? AFU.fmtNum(live.rate_usvh, 3) : DASH;
        var cps = hasRec ? AFU.fmtNum(live.cps, 1) : DASH;
        var avgCps = hasRec ? AFU.fmtNum(live.avg_cps, 2) : DASH;
        var dose = hasRec ? AFU.fmtNum(live.dose_usv, 3) : DASH;

        // Battery
        var battery = DASH;
        if (st && isObj(st.ble_last_rec) && typeof st.ble_last_rec.battery === 'number' && isFinite(st.ble_last_rec.battery)) {
            battery = String(Math.round(st.ble_last_rec.battery));
        }

        // Cards
        var cards = sensorCards(sn);

        return {
            chips: chips,
            fw: fw,
            live: live,
            hasRec: hasRec,
            rate: rate,
            cps: cps,
            avgCps: avgCps,
            dose: dose,
            battery: battery,
            cards: cards
        };
    }

    // Заполнение диска: used, total в байтах -> целые проценты; total нет/0 -> 'н/д'; used > total -> 100 %.
    // Пороги цвета по округлённому значению: >= 80 % warn, >= 95 % crit; для 'н/д' level = 'ok' (нейтрально).
    function diskUsage(used, total) {
        var ok = typeof used === 'number' && isFinite(used) && typeof total === 'number' && isFinite(total) && total > 0;
        if (!ok) { return { pct: null, text: 'н/д', level: 'ok' }; }
        var pct = Math.round(Math.max(0, used) / total * 100);
        if (pct > 100) { pct = 100; }
        return { pct: pct, text: pct + ' %', level: pct >= 95 ? 'crit' : (pct >= 80 ? 'warn' : 'ok') };
    }

    return {
        diskUsage: diskUsage,
        sensorCard: sensorCard,
        sensorCards: sensorCards,
        groupByModel: groupByModel,
        homeModel: homeModel
    };
});
