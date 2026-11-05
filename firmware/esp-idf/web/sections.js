(function (root, factory) {
    if (typeof module === 'object' && module.exports) { module.exports = factory(require('./ui-common.js'), require('./home-model.js')); }
    else { root.AFSEC = factory(root.AFU, root.AFHM); }
})(this, function (AFU, AFHM) {
    'use strict';

    var SECTIONS = [
        { id: 'status', title: 'Состояние' },
        { id: 'system', title: 'Система' },
        { id: 'wifi', title: 'Wi-Fi и сеть' },
        { id: 'ble', title: 'BLE' },
        { id: 'sensors', title: 'Датчики среды' },
        { id: 'mqtt', title: 'MQTT / Home Assistant' },
        { id: 'db', title: 'База данных' },
        { id: 'storage', title: 'Хранилище' },
        { id: 'firmware', title: 'Прошивки' },
        { id: 'maint', title: 'Обслуживание' }
    ];

    var IDS = SECTIONS.map(function (s) { return s.id; });

    function fromHash(hash) {
        if (typeof hash !== 'string') return null;
        var h = hash;
        if (h.charAt(0) === '#') h = h.slice(1);
        if (h === 'summary') return 'status'; // П.8: «Сводка» убрана, старые закладки ведут в «Состояние»
        if (IDS.indexOf(h) !== -1) return h;
        return null;
    }

    function pick(hash, stored) {
        var h = fromHash(hash);
        if (h !== null) return h;
        if (typeof stored === 'string' && IDS.indexOf(stored) !== -1) return stored;
        return 'status';
    }

    function toHash(id) {
        if (IDS.indexOf(id) !== -1) return '#' + id;
        return '#status';
    }

    function needs(id) {
        switch (id) {
            case 'status': return ['status'];
            case 'system': return ['status', 'system'];
            case 'wifi': return ['status', 'system'];
            case 'ble': return ['status', 'ble', 'sessions'];
            case 'sensors': return ['sensors'];
            case 'mqtt': return ['status'];
            case 'storage': return ['status', 'storage'];
            case 'maint': return ['status', 'system'];
            default: return ['status'];
        }
    }

    function isObj(v) {
        return v !== null && typeof v === 'object' && !Array.isArray(v);
    }

    function worst(a, b) {
        if (a === 'bad' || b === 'bad') return 'bad';
        if (a === 'warn' || b === 'warn') return 'warn';
        return '';
    }

    function lv(x) {
        if (x === 'bad') return 'bad';
        if (x === 'warn') return 'warn';
        return '';
    }

    function badges(d) {
        var out = {};
        IDS.forEach(function (id) {
            out[id] = { text: '', level: '' };
        });

        if (d === null || d === undefined) return out;

        var st = d.st;
        var sy = d.sy;
        var sn = d.sn;
        var hs = d.hs;

        // status
        if (isObj(st)) {
            if (st.wifi_connected !== true) {
                out.status.level = 'bad';
            } else if (st.ble_connected !== true) {
                out.status.level = 'warn';
            }
        }

        // system
        var m = AFU.sysModel(st, sy);
        out.system.level = worst(worst(lv(m.heapLevel), lv(m.intLevel)), lv(m.flashLevel));

        // wifi
        var rssiVal = (sy && isObj(sy)) ? sy.rssi : NaN;
        var rssiQ = AFU.rssiQuality(rssiVal);
        out.wifi.level = lv(rssiQ.level);
        if (isObj(st) && st.wifi_connected !== true) {
            out.wifi.level = 'bad';
        }

        // ble
        if (isObj(st)) {
            var n = st.ble_errors;
            if (typeof n === 'number' && n > 0) {
                out.ble.text = String(Math.round(n));
                out.ble.level = 'warn';
            }
            if (st.ble_connected !== true) {
                var bleLvl = st.ble_ready === true ? 'warn' : 'bad';
                out.ble.level = worst(out.ble.level, bleLvl);
            }
        }

        // sensors
        var cards = AFHM.sensorCards(sn);
        if (cards && cards.length > 0) {
            var nEn = 0;
            var sLvl = '';
            for (var i = 0; i < cards.length; i++) {
                if (cards[i].enabled === false) continue;
                nEn++;
                sLvl = worst(sLvl, lv(cards[i].level));
            }
            out.sensors.text = String(nEn);
            out.sensors.level = sLvl;
        }

        // mqtt
        var hm = AFHM.homeModel(st, sn);
        var chip = hm.chips.mqtt;
        out.mqtt.level = lv(chip.level);
        if (isObj(st) && isObj(st.mqtt) && typeof st.mqtt.failed === 'number' && st.mqtt.failed > 0) {
            out.mqtt.text = String(Math.round(st.mqtt.failed));
        }

        // storage
        if (isObj(hs) && isObj(hs.persist) && typeof hs.persist.flush_errs === 'number' && hs.persist.flush_errs > 0) {
            out.storage.text = String(Math.round(hs.persist.flush_errs));
            out.storage.level = 'bad';
        } else if (isObj(hs) && isObj(hs.raw) && typeof hs.raw.limit === 'number' && hs.raw.limit > 0 && typeof hs.raw.count === 'number') {
            var p = (hs.raw.count / hs.raw.limit) * 100;
            if (p >= 100) {
                out.storage.level = 'bad';
            } else if (p >= 80) {
                out.storage.level = 'warn';
            }
        }

        return out;
    }

    return { SECTIONS: SECTIONS, IDS: IDS, fromHash: fromHash, pick: pick, toHash: toHash, needs: needs, badges: badges };
});
