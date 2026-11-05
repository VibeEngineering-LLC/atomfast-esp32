(function (root, factory) {
    if (typeof module === 'object' && module.exports) { module.exports = factory(); }
    else { root.AFU = factory(); }
})(this, function () {
    'use strict';

    var DASH = '—';

    function isNum(v) {
        return typeof v === 'number' && isFinite(v);
    }

    function fmtNum(v, dec) {
        if (!isNum(v)) return DASH;
        var s = v.toFixed(dec);
        if (/^-0(\.0+)?$/.test(s)) {
            s = s.substring(1);
        }
        return s;
    }

    function pageOf(path) {
        if (typeof path !== 'string') return null;
        var p = path;
        if (p.length > 1 && p.charAt(p.length - 1) === '/') {
            p = p.substring(0, p.length - 1);
        }
        if (p === '' || p === '/' || p === '/index.html') return 'history';
        if (p === '/history') return 'history';
        if (p === '/settings' || p === '/readings') return 'settings';
        if (p === '/system') return 'system';
        if (p === '/measure' || p === '/series') return 'measure';
        return null;
    }

    function fmtUptime(sec) {
        if (!isNum(sec) || sec < 0) return DASH;
        var n = Math.floor(sec);
        var d = Math.floor(n / 86400);
        var rem = n % 86400;
        var h = Math.floor(rem / 3600);
        var m = Math.floor((rem % 3600) / 60);
        var s = rem % 60;
        var hh = h < 10 ? '0' + h : '' + h;
        var mm = m < 10 ? '0' + m : '' + m;
        var ss = s < 10 ? '0' + s : '' + s;
        var timeStr = hh + ':' + mm + ':' + ss;
        if (d === 0) return timeStr;
        return d + 'д ' + timeStr;
    }

    function fmtBytes(n) {
        if (!isNum(n) || n < 0) return DASH;
        if (n < 1024) return Math.round(n) + ' Б';
        if (n < 1048576) return (n / 1024).toFixed(1) + ' КиБ';
        return (n / 1048576).toFixed(2) + ' МиБ';
    }

    function usedPct(used, total) {
        if (!isNum(used) || !isNum(total) || total <= 0 || used < 0) return null;
        return Math.min(100, Math.round(used / total * 100));
    }

    function pctLevel(pct) {
        if (!isNum(pct)) return 'mut';
        if (pct >= 90) return 'bad';
        if (pct >= 75) return 'warn';
        return 'ok';
    }

    function freeLevel(freeBytes, warnBelow, badBelow) {
        if (!isNum(freeBytes)) return 'mut';
        if (freeBytes < badBelow) return 'bad';
        if (freeBytes < warnBelow) return 'warn';
        return 'ok';
    }

    function rssiQuality(dbm) {
        if (!isNum(dbm)) {
            return { pct: null, label: DASH, level: 'mut' };
        }
        var pct = Math.round(2 * (dbm + 100));
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;

        var label, level;
        if (dbm >= -55) {
            label = 'отличный';
            level = 'ok';
        } else if (dbm >= -67) {
            label = 'хороший';
            level = 'ok';
        } else if (dbm >= -75) {
            label = 'средний';
            level = 'warn';
        } else {
            label = 'слабый';
            level = 'bad';
        }

        return { pct: pct, label: label, level: level };
    }

    function yesNo(v) {
        if (v === true || v === 1 || v === '1' || v === 'true' || v === 'yes') return 'да';
        if (v === false || v === 0 || v === '0' || v === 'false' || v === 'no') return 'нет';
        if (v === undefined || v === null) return DASH;
        return String(v);
    }

    function heroModel(st, sn) {
        var wifi = (st && st.wifi_connected === true) ? 'ok' : 'bad';
        var ble;
        if (st && st.ble_connected === true) {
            ble = 'ok';
        } else if (st && st.ble_ready === true) {
            ble = 'warn';
        } else {
            ble = 'bad';
        }

        var bleText;
        if (ble === 'ok') bleText = 'подключён';
        else if (ble === 'warn') bleText = 'ищет прибор';
        else bleText = 'нет';

        var live = null;
        if (sn && typeof sn.live === 'object' && sn.live !== null) {
            live = sn.live;
        }

        var rate = (live && live.has_rec === true) ? fmtNum(live.rate_usvh, 3) : DASH;
        var cps = (live && live.has_rec === true) ? fmtNum(live.cps, 1) : DASH;

        var airT = DASH;
        var airP = DASH;
        var airOk = false;
        var airNote;

        var sensor = (sn && typeof sn.sensor === 'object' && sn.sensor !== null) ? sn.sensor : null;

        if (sensor) {
            if (sensor.status === 'ok') {
                airOk = true;
                airNote = 'BMP280';
                if (sensor.temp === true) airT = fmtNum(sensor.t_c, 1);
                if (sensor.press === true) airP = fmtNum(sensor.p_mmhg, 1);
            } else if (sensor.status === 'disabled') {
                airNote = 'датчик выключен';
            } else if (sensor.status === 'no_sensor') {
                airNote = 'датчик не найден';
            } else if (sensor.status === 'bus_error') {
                airNote = 'ошибка шины I2C';
            } else {
                airNote = 'датчик не отвечает';
            }
        } else {
            airNote = 'нет данных';
        }

        var fw = (st && typeof st.fw_version === 'string') ? st.fw_version : DASH;
        var uptime = fmtUptime(st ? st.uptime_s : NaN);

        return {
            wifi: wifi,
            ble: ble,
            bleText: bleText,
            live: live,
            rate: rate,
            cps: cps,
            airT: airT,
            airP: airP,
            airOk: airOk,
            airNote: airNote,
            fw: fw,
            uptime: uptime
        };
    }

    function sysModel(st, sy) {
        var fw = (st && typeof st.fw_version === 'string') ? st.fw_version : DASH;
        var part = (st && typeof st.running_partition === 'string') ? st.running_partition : DASH;
        var uptime = fmtUptime(st ? st.uptime_s : NaN);
        var boots = (st && typeof st.boot_count === 'number' && isFinite(st.boot_count)) ? String(st.boot_count) : DASH;
        var reset = (st && typeof st.reset_reason === 'string') ? st.reset_reason : DASH;

        var heapFree = fmtBytes(sy ? sy.free_heap : NaN);
        var heapMin = fmtBytes(sy ? sy.min_free_heap : NaN);
        var heapLevel = freeLevel(sy ? sy.free_heap : NaN, 80000, 40000);

        var intFree = fmtBytes(sy ? sy.int_free : NaN);
        var intMin = fmtBytes(sy ? sy.int_min : NaN);
        var intLevel = freeLevel(sy ? sy.int_free : NaN, 60000, 30000);

        var psramFree = fmtBytes(sy ? sy.psram_free : NaN);
        var psramTotal = fmtBytes(sy ? sy.psram_total : NaN);

        var flashPct = usedPct(sy ? sy.flash_used : NaN, sy ? sy.flash_total : NaN);
        var flashLevel = pctLevel(flashPct);
        var flashText = fmtBytes(sy ? sy.flash_used : NaN) + ' из ' + fmtBytes(sy ? sy.flash_total : NaN);

        var rssi = rssiQuality(sy ? sy.rssi : NaN);
        var rssiText = (sy && isNum(sy.rssi)) ? sy.rssi + ' дБм' : DASH;

        var ssid = (sy && typeof sy.ssid === 'string') ? sy.ssid : DASH;
        var netMode = (sy && typeof sy.net_mode === 'string') ? sy.net_mode : DASH;

        var timeSource = (st && typeof st.time_source === 'string') ? st.time_source : DASH;
        var sntp = yesNo(st ? st.sntp_synced : undefined);

        return {
            fw: fw,
            part: part,
            uptime: uptime,
            boots: boots,
            reset: reset,
            heapFree: heapFree,
            heapMin: heapMin,
            heapLevel: heapLevel,
            intFree: intFree,
            intMin: intMin,
            intLevel: intLevel,
            psramFree: psramFree,
            psramTotal: psramTotal,
            flashPct: flashPct,
            flashLevel: flashLevel,
            flashText: flashText,
            rssi: rssi,
            rssiText: rssiText,
            ssid: ssid,
            netMode: netMode,
            timeSource: timeSource,
            sntp: sntp
        };
    }

    // UI font scale: stored value is a percent from the fixed list, anything else means the default
    var FONT_SCALES = [
        { pct: 90, label: 'Мелкий' }, { pct: 100, label: 'Обычный' },
        { pct: 115, label: 'Крупный' }, { pct: 130, label: 'Очень крупный' }
    ];
    function fontPct(v) {
        var n = (typeof v === 'string' && /^\d{2,3}$/.test(v.trim())) ? parseInt(v, 10) : v;
        for (var i = 0; i < FONT_SCALES.length; i++) if (FONT_SCALES[i].pct === n) return n;
        return 100;
    }
    function fontScale(v) { return fontPct(v) / 100; }

    return { pageOf: pageOf, fmtUptime: fmtUptime, fmtBytes: fmtBytes, usedPct: usedPct, pctLevel: pctLevel,
             freeLevel: freeLevel, rssiQuality: rssiQuality, yesNo: yesNo, heroModel: heroModel, sysModel: sysModel,
             fmtNum: fmtNum, FONT_SCALES: FONT_SCALES, fontPct: fontPct, fontScale: fontScale };
});
