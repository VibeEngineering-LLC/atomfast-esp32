(function (root, factory) {
    if (typeof module === 'object' && module.exports) { module.exports = factory(); }
    else { root.AFAX = factory(); }
})(this, function () {
    'use strict';

    function isFin(v) {
        return typeof v === 'number' && isFinite(v);
    }

    var GROUPS = [
        { id: 'dose', title: 'доза', zeroBase: false },
        { id: 'count', title: 'счёт', zeroBase: false },
        { id: 'temp', title: '°C', zeroBase: false },
        { id: 'press', title: 'давление', zeroBase: false },
        { id: 'hum', title: 'влажность', zeroBase: false }
    ];

    var GROUP_IDS = GROUPS.map(function (g) { return g.id; });

    function isGroup(id) {
        return typeof id === 'string' && GROUP_IDS.indexOf(id) !== -1;
    }

    function groupOf(def) {
        if (isGroup(def.group)) {
            return def.group;
        }
        return 'count';
    }

    function buildAxes(defs, vis) {
        var visible = [];
        for (var i = 0; i < defs.length; i++) {
            var d = defs[i];
            if (vis && vis[d.id] === true) {
                visible.push(d);
            }
        }

        var groupMap = {};
        for (var j = 0; j < visible.length; j++) {
            var v = visible[j];
            var g = groupOf(v);
            if (!groupMap[g]) {
                groupMap[g] = {
                    id: g,
                    series: [],
                    fields: [],
                    units: [],
                    colors: []
                };
            }
            var gm = groupMap[g];
            gm.series.push(v.id);
            gm.fields.push(v.field);
            if (v.unit !== undefined && v.unit !== null && gm.units.indexOf(v.unit) === -1) {
                gm.units.push(v.unit);
            }
            gm.colors.push(v.color);
        }

        var presentGroups = [];
        for (var k = 0; k < GROUP_IDS.length; k++) {
            var gid = GROUP_IDS[k];
            if (groupMap[gid]) {
                presentGroups.push(gid);
            }
        }

        if (presentGroups.length === 0) {
            return { axes: [], byGroup: {} };
        }

        var axes = [];
        var byGroup = {};
        var axisSlots = [
            { side: 'L', slot: 0 },
            { side: 'R', slot: 0 },
            { side: 'L', slot: 1 },
            { side: 'R', slot: 1 }
        ];

        for (var p = 0; p < presentGroups.length; p++) {
            var pg = presentGroups[p];
            var axisIdx;
            if (p < 4) {
                axisIdx = p;
                if (!axes[axisIdx]) {
                    axes[axisIdx] = {
                        groups: [],
                        series: [],
                        fields: [],
                        units: [],
                        colors: [],
                        side: axisSlots[axisIdx].side,
                        slot: axisSlots[axisIdx].slot,
                        zeroBase: true
                    };
                }
            } else {
                axisIdx = 3;
                if (!axes[axisIdx]) {
                    axes[axisIdx] = {
                        groups: [],
                        series: [],
                        fields: [],
                        units: [],
                        colors: [],
                        side: axisSlots[3].side,
                        slot: axisSlots[3].slot,
                        zeroBase: true
                    };
                }
            }

            var ax = axes[axisIdx];
            var gm = groupMap[pg];
            ax.groups.push(pg);
            for (var s = 0; s < gm.series.length; s++) {
                ax.series.push(gm.series[s]);
                ax.fields.push(gm.fields[s]);
                ax.colors.push(gm.colors[s]);
            }
            for (var u = 0; u < gm.units.length; u++) {
                if (ax.units.indexOf(gm.units[u]) === -1) {
                    ax.units.push(gm.units[u]);
                }
            }

            var groupDef = null;
            for (var gd = 0; gd < GROUPS.length; gd++) {
                if (GROUPS[gd].id === pg) {
                    groupDef = GROUPS[gd];
                    break;
                }
            }
            if (groupDef && !groupDef.zeroBase) {
                ax.zeroBase = false;
            }

            byGroup[pg] = axisIdx;
        }

        for (var a = 0; a < axes.length; a++) {
            if (axes[a]) {
                axes[a].id = axes[a].groups[0];
            }
        }

        return { axes: axes, byGroup: byGroup };
    }

    function autoRange(rows, fields, includeZero) {
        var lo = Infinity;
        var hi = -Infinity;
        var found = false;

        for (var i = 0; i < rows.length; i++) {
            var row = rows[i];
            for (var f = 0; f < fields.length; f++) {
                var val = row[fields[f]];
                if (isFin(val)) {
                    found = true;
                    if (val < lo) lo = val;
                    if (val > hi) hi = val;
                }
            }
        }

        if (!found) {
            return null;
        }

        var span = hi - lo;
        var scale = Math.max(Math.abs(lo), Math.abs(hi));
        var min, max;
        if (span <= scale * 1e-4) {
            var half = scale > 0 ? scale * 0.02 : 1;
            var mid = (lo + hi) / 2;
            min = mid - half;
            max = mid + half;
        } else {
            min = lo - span * 0.1;
            max = hi + span * 0.1;
        }

        if (includeZero) {
            if (min > 0) min = 0;
            if (max < 0) max = 0;
        }

        return { min: min, max: max };
    }
    function niceTicks(min, max, count) {
        if (!isFin(min) || !isFin(max) || max <= min) {
            return [];
        }

        var range = max - min;
        var raw = range / (count > 0 ? count : 1);
        var mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10));
        var norm = raw / mag;
        var step;
        if (norm <= 1) {
            step = mag;
        } else if (norm <= 2) {
            step = 2 * mag;
        } else if (norm <= 5) {
            step = 5 * mag;
        } else {
            step = 10 * mag;
        }

        var ticks = [];
        var first = Math.ceil(min / step - 1e-9) * step;
        var v = first;
        while (v <= max + step * 1e-9) {
            ticks.push(Number(v.toPrecision(12)));
            v += step;
        }

        return ticks;
    }

    function parseNum(val) {
        if (typeof val === 'number') {
            return val;
        }
        if (typeof val === 'string') {
            var s = val.trim().replace(',', '.');
            if (/^[+-]?(\d+\.?\d*|\.\d+)(e[+-]?\d+)?$/i.test(s)) {
                return parseFloat(s);
            }
        }
        return NaN;
    }

    function validateRange(minText, maxText) {
        var min = parseNum(minText);
        var max = parseNum(maxText);

        if (!isFin(min) || !isFin(max)) {
            return { ok: false, min: null, max: null, error: 'введите числа' };
        }

        if (min >= max) {
            return { ok: false, min: null, max: null, error: 'min должен быть меньше max' };
        }

        return { ok: true, min: min, max: max, error: '' };
    }

    function resolve(entry, auto) {
        if (entry && typeof entry === 'object' && entry.mode === 'manual' && isFin(entry.min) && isFin(entry.max) && entry.min < entry.max) {
            return { min: entry.min, max: entry.max, mode: 'manual' };
        }
        if (auto && typeof auto === 'object') {
            return { min: auto.min, max: auto.max, mode: 'auto' };
        }
        return null;
    }

    function zoomRange(min, max, factor, anchorFrac) {
        if (!isFin(min) || !isFin(max) || max <= min || !isFin(factor) || factor <= 0) {
            return { min: min, max: max };
        }

        if (!isFin(anchorFrac)) {
            anchorFrac = 0.5;
        }
        if (anchorFrac < 0) anchorFrac = 0;
        if (anchorFrac > 1) anchorFrac = 1;

        var span = max - min;
        var anchor = min + span * anchorFrac;
        var newSpan = span * factor;
        var newMin = anchor - newSpan * anchorFrac;

        return { min: newMin, max: newMin + newSpan };
    }

    function panRange(min, max, fracDelta) {
        if (!isFin(min) || !isFin(max) || !isFin(fracDelta)) {
            return { min: min, max: max };
        }

        var shift = (max - min) * fracDelta;
        return { min: min + shift, max: max + shift };
    }

    function setManual(cfg, gid, min, max) {
        var copy = {};
        if (cfg && typeof cfg === 'object') {
            for (var k in cfg) {
                if (Object.prototype.hasOwnProperty.call(cfg, k)) {
                    copy[k] = cfg[k];
                }
            }
        }

        if (isGroup(gid) && isFin(min) && isFin(max) && min < max) {
            var keepM = copy[gid] && copy[gid].zero === true;
            copy[gid] = keepM ? { mode: 'manual', min: min, max: max, zero: true } : { mode: 'manual', min: min, max: max };
        }

        return copy;
    }

    function setAuto(cfg, gid) {
        var copy = {};
        if (cfg && typeof cfg === 'object') {
            for (var k in cfg) {
                if (Object.prototype.hasOwnProperty.call(cfg, k)) {
                    copy[k] = cfg[k];
                }
            }
        }

        if (isGroup(gid)) {
            var keepZ = copy[gid] && copy[gid].zero === true;
            copy[gid] = keepZ ? { mode: 'auto', zero: true } : { mode: 'auto' };
        }

        return copy;
    }

    function zeroOf(cfg, gid) {
        return !!(cfg && typeof cfg === 'object' && cfg[gid] && cfg[gid].zero === true);
    }

    function setZero(cfg, gid, on) {
        var copy = {};
        if (cfg && typeof cfg === 'object') {
            for (var k in cfg) {
                if (Object.prototype.hasOwnProperty.call(cfg, k)) copy[k] = cfg[k];
            }
        }
        if (!isGroup(gid)) return copy;
        var e = {};
        var prev = copy[gid];
        if (prev && typeof prev === 'object') {
            for (var p in prev) if (Object.prototype.hasOwnProperty.call(prev, p)) e[p] = prev[p];
        }
        if (!e.mode) e.mode = 'auto';
        if (on) e.zero = true; else delete e.zero;
        copy[gid] = e;
        return copy;
    }
function scaleTargets(axes, sel, all) {
        var ids = [];
        if (!Array.isArray(axes)) return ids;
        if (all === true) {
            for (var i = 0; i < axes.length; i++) if (axes[i]) ids.push(axes[i].id);
            return ids;
        }
        for (var j = 0; j < axes.length; j++) {
            if (axes[j] && axes[j].id === sel) return [sel];
        }
        return ids;
    }

    function zoomAxes(cfg, axes, ids, factor) {
        var out = cfg;
        for (var i = 0; i < ids.length; i++) {
            for (var j = 0; j < axes.length; j++) {
                if (axes[j] && axes[j].id === ids[i]) {
                    var r = zoomRange(axes[j].min, axes[j].max, factor, 0.5);
                    out = setManual(out, ids[i], r.min, r.max);
                }
            }
        }
        return out;
    }

    function fitAxes(cfg, ids) {
        var out = cfg;
        for (var i = 0; i < ids.length; i++) out = setAuto(out, ids[i]);
        return out;
    }

    // E-1: смена периода (Час/6 ч/Сутки/Вся/Интервал) возвращает все оси в авто (флаг zero сохраняется)
    function fitAll(cfg) {
        var out = {};
        if (cfg && typeof cfg === 'object') {
            for (var k in cfg) {
                if (Object.prototype.hasOwnProperty.call(cfg, k)) setAutoKeep(out, k, cfg[k]);
            }
        }
        return out;
    }
    function setAutoKeep(out, k, old) {
        if (isGroup(k)) out[k] = (old && old.zero === true) ? { mode: 'auto', zero: true } : { mode: 'auto' };
        return out;
    }

    function resetAxes(cfg, ids) {
        var out = {};
        if (cfg && typeof cfg === 'object') {
            for (var k in cfg) {
                if (Object.prototype.hasOwnProperty.call(cfg, k) && ids.indexOf(k) === -1) out[k] = cfg[k];
            }
        }
        return out;
    }

    function setAxisText(cfg, id, minText, maxText) {
        var v = validateRange(minText, maxText);
        if (!v.ok) return { ok: false, cfg: cfg, error: v.error };
        return { ok: true, cfg: setManual(cfg, id, v.min, v.max), error: '' };
    }
    function serialize(cfg) {
        return JSON.stringify({
            v: 1,
            cfg: cfg || {}
        });
    }

    function restore(text) {
        if (typeof text !== 'string') {
            return { cfg: {} };
        }

        var data;
        try {
            data = JSON.parse(text);
        } catch (e) {
            return { cfg: {} };
        }

        if (!data || typeof data !== 'object' || data.v !== 1) {
            return { cfg: {} };
        }

        var cfg = {};
        if (data.cfg && typeof data.cfg === 'object') {
            for (var k in data.cfg) {
                if (Object.prototype.hasOwnProperty.call(data.cfg, k) && isGroup(k)) {
                    var val = data.cfg[k];
                    var zk = val && val.zero === true;
                    if (val && typeof val === 'object' && val.mode === 'manual' && isFin(val.min) && isFin(val.max) && val.min < val.max) {
                        cfg[k] = zk ? { mode: 'manual', min: val.min, max: val.max, zero: true } : { mode: 'manual', min: val.min, max: val.max };
                    } else {
                        cfg[k] = zk ? { mode: 'auto', zero: true } : { mode: 'auto' };
                    }
                }
            }
        }

        return { cfg: cfg };
    }

    function quickPeriod(id) {
        if (id === '1h') {
            return { windowSec: 3600, viewSec: 3600 };
        }
        if (id === '6h') {
            return { windowSec: 86400, viewSec: 21600 };
        }
        if (id === '24h') {
            return { windowSec: 86400, viewSec: 86400 };
        }
        if (id === 'all') {
            return { windowSec: 0, viewSec: 0 };
        }
        return null;
    }

    function axisTitle(axis) {
        if (axis && typeof axis === 'object' && Array.isArray(axis.units)) {
            return axis.units.join(', ');
        }
        return '';
    }

    // T-54: вертикальная линия КАЖДОЙ оси (в т.ч. нулевого слота) на краю её слота; список отрезков для рисования
    function axisLines(axes, left, plotW, top, plotH, slotW) {
        var out = [];
        for (var i = 0; i < axes.length; i++) {
            var a = axes[i];
            var x = a.side === 'L' ? left - a.slot * slotW : left + plotW + a.slot * slotW;
            out.push({ id: a.id, x: x, y0: top, y1: top + plotH });
        }
        return out;
    }
    return {
        GROUPS: GROUPS,
        GROUP_IDS: GROUP_IDS,
        isGroup: isGroup,
        groupOf: groupOf,
        buildAxes: buildAxes,
        autoRange: autoRange,
        niceTicks: niceTicks,
        validateRange: validateRange,
        resolve: resolve,
        zoomRange: zoomRange,
        panRange: panRange,
        setManual: setManual,
        setAuto: setAuto,
        serialize: serialize,
        restore: restore,
        quickPeriod: quickPeriod,
        zeroOf: zeroOf,
        scaleTargets: scaleTargets,
        zoomAxes: zoomAxes,
        fitAxes: fitAxes,
        fitAll: fitAll,
        resetAxes: resetAxes,
        setAxisText: setAxisText,
        setZero: setZero,
        axisTitle: axisTitle,
        axisLines: axisLines
    };
});
