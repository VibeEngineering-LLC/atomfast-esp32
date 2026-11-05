(function (root, factory) {
    if (typeof module === 'object' && module.exports) {
        module.exports = factory(require('./history-core.js'));
    } else {
        root.AFS = factory(root.AFH);
    }
})(this, function (AFH) {
    'use strict';

    function isFiniteNum(v) {
        return typeof v === 'number' && isFinite(v);
    }

    function visibleDefs(defs, vis) {
        var res = [];
        for (var i = 0; i < defs.length; i++) {
            if (vis[defs[i].id] === true) res.push(defs[i]);
        }
        return res;
    }

    function axisDefs(defs, vis, axis) {
        var vd = visibleDefs(defs, vis);
        var res = [];
        for (var i = 0; i < vd.length; i++) {
            if (vd[i].axis === axis) res.push(vd[i]);
        }
        return res;
    }

    function axisLabel(defs, vis, axis) {
        var ad = axisDefs(defs, vis, axis);
        var units = [];
        for (var i = 0; i < ad.length; i++) {
            var u = ad[i].unit;
            if (units.indexOf(u) === -1) units.push(u);
        }
        return units.join(', ');
    }

    function nearestIndex(rows, ts) {
        if (!rows.length) return -1;
        var lo = 0, hi = rows.length - 1;
        while (lo < hi) {
            var mid = (lo + hi) >> 1;
            if (rows[mid].ts < ts) lo = mid + 1; else hi = mid;
        }
        if (lo > 0 && Math.abs(rows[lo - 1].ts - ts) <= Math.abs(rows[lo].ts - ts)) return lo - 1;
        return lo;
    }

    function sliceRange(rows, t0, t1) {
        var lo = 0, hi = rows.length;
        while (lo < hi) {
            var mid = (lo + hi) >> 1;
            if (rows[mid].ts < t0) lo = mid + 1; else hi = mid;
        }
        var start = lo;
        hi = rows.length;
        while (lo < hi) {
            var mid2 = (lo + hi) >> 1;
            if (rows[mid2].ts <= t1) lo = mid2 + 1; else hi = mid2;
        }
        return rows.slice(start, lo);
    }

    function axisScale(rows, fieldList, count) {
        if (!rows.length || !fieldList.length) return null;
        var lo = Infinity, hi = -Infinity, found = false;
        for (var i = 0; i < rows.length; i++) {
            for (var j = 0; j < fieldList.length; j++) {
                var v = rows[i][fieldList[j]];
                if (isFiniteNum(v)) {
                    found = true;
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                }
            }
        }
        if (!found) return null;
        var pad = (hi - lo) * 0.05;
        var ticks = AFH.niceTicks(lo - pad, hi + pad, count);
        return { min: ticks[0], max: ticks[ticks.length - 1], ticks: ticks };
    }

    function scaleFor(rows, defs, vis, axis, count) {
        var ad = axisDefs(defs, vis, axis);
        var fields = [];
        for (var i = 0; i < ad.length; i++) fields.push(ad[i].field);
        return axisScale(rows, fields, count);
    }

    function cursorValues(rows, defs, vis, ts) {
        if (!rows.length) return null;
        var idx = nearestIndex(rows, ts);
        var row = rows[idx];
        var vd = visibleDefs(defs, vis);
        var items = [];
        for (var i = 0; i < vd.length; i++) {
            var d = vd[i];
            var v = row[d.field];
            items.push({
                id: d.id, name: d.name, unit: d.unit, color: d.color, axis: d.axis,
                value: isFiniteNum(v) ? v : null
            });
        }
        return { ts: row.ts, index: idx, items: items };
    }

    function plotPoints(rows, d, maxPoints) {
        var filtered = [];
        for (var i = 0; i < rows.length; i++) {
            if (isFiniteNum(rows[i][d.field])) filtered.push(rows[i]);
        }
        return AFH.downsample(filtered, d.field, maxPoints);
    }

    function yToPx(v, min, max, top, height) {
        if (max <= min) return top + height / 2;
        return top + height * (1 - (v - min) / (max - min));
    }

    function xToPx(t, t0, t1, left, width) {
        if (t1 <= t0) return left;
        return left + width * (t - t0) / (t1 - t0);
    }

    function pxToTime(px, t0, t1, left, width) {
        if (width <= 0) return t0;
        var t = t0 + (px - left) * (t1 - t0) / width;
        if (t < t0) return t0;
        if (t > t1) return t1;
        return t;
    }

    function zoomRange(t0, t1, cx, factor, minSpan) {
        var a = cx - (cx - t0) * factor;
        var b = cx + (t1 - cx) * factor;
        if (b - a < minSpan) {
            var center = (a + b) / 2;
            a = center - minSpan / 2;
            b = center + minSpan / 2;
        }
        return { t0: a, t1: b };
    }

    function fullRange(rows) {
        if (!rows.length) return null;
        var t0 = rows[0].ts, t1 = rows[rows.length - 1].ts;
        if (t0 === t1) t1 = t0 + 1;
        return { t0: t0, t1: t1 };
    }

// Разбор CSV показаний воздуха
    function parseEnvCsv(text) {
        var lines = String(text).split(/\r\n|\n|\r/);
        var headerIdx = -1;
        var i;
        for (i = 0; i < lines.length; i++) {
            if (lines[i].trim() !== '') {
                headerIdx = i;
                break;
            }
        }
        if (headerIdx === -1) {
            return { rows: [], bad: 0 };
        }
        var cols = lines[headerIdx].split(',');
        var hasTs = false;
        for (i = 0; i < cols.length; i++) {
            if (cols[i] === 'ts') {
                hasTs = true;
                break;
            }
        }
        if (!hasTs) {
            var badCount = 0;
            for (i = headerIdx + 1; i < lines.length; i++) {
                if (lines[i].trim() !== '') {
                    badCount++;
                }
            }
            return { rows: [], bad: badCount };
        }
        var iTs = -1, iT = -1, iP = -1, iF = -1, iK = -1, iS = -1, iM = -1, iH = -1;
        for (i = 0; i < cols.length; i++) {
            if (cols[i] === 'ts') iTs = i;
            else if (cols[i] === 'kind') iK = i;
            else if (cols[i] === 'temp_c') iT = i;
            else if (cols[i] === 'press_mmhg') iP = i;
            else if (cols[i] === 'flags') iF = i;
            else if (cols[i] === 'sensor') iS = i;
            else if (cols[i] === 'model') iM = i;
            else if (cols[i] === 'hum_pct') iH = i;
        }
        var rows = [];
        var bad = 0;
        for (i = headerIdx + 1; i < lines.length; i++) {
            var line = lines[i];
            if (line.trim() === '') continue;
            var parts = line.split(',');
            if (parts.length !== cols.length) {
                bad++;
                continue;
            }
            var ts = Number(parts[iTs]);
            if (!isFiniteNum(ts)) {
                bad++;
                continue;
            }
            if (iK !== -1 && parts[iK] !== 'env') {
                bad++;
                continue;
            }
            var air_t = NaN;
            if (iT !== -1) {
                air_t = (parts[iT] === '' ? NaN : Number(parts[iT]));
            }
            var air_p = NaN;
            if (iP !== -1) {
                air_p = (parts[iP] === '' ? NaN : Number(parts[iP]));
            }
            var air_h = NaN;
            if (iH !== -1) {
                air_h = (parts[iH] === '' ? NaN : Number(parts[iH]));
            }
            var sn = 1;
            if (iS !== -1) {
                var sv = Number(parts[iS]);
                if (isFiniteNum(sv) && sv >= 1 && sv <= 8 && Math.floor(sv) === sv) sn = sv;
            }
            var flags = 0;
            if (iF !== -1) {
                var fVal = Number(parts[iF]);
                if (isFiniteNum(fVal)) flags = fVal;
            }
            if (!isFiniteNum(air_t) && !isFiniteNum(air_p) && !isFiniteNum(air_h)) {
                bad++;
                continue;
            }
            rows.push({ ts: ts, sensor: sn, model: (iM !== -1 ? parts[iM] : ''), air_t: air_t, air_p: air_p, air_h: air_h, flags: flags });
        }
        return { rows: rows, bad: bad };
    }

    // Привязка показаний воздуха к строкам истории (несколько датчиков: поля air_t_N, air_p_N, air_h_N)
    function mergeEnv(rows, env, maxGap) {
        if (!Array.isArray(rows)) return 0;
        var gap = (isFiniteNum(maxGap) && maxGap >= 0) ? maxGap : 120;
        var by = {};
        var ids = [];
        (Array.isArray(env) ? env : []).forEach(function (e) {
            var n = isFiniteNum(e.sensor) ? e.sensor : 1;
            if (!by[n]) { by[n] = []; ids.push(n); }
            by[n].push(e);
        });
        ids.sort(function (x, y) { return x - y; });
        var multi = ids.length > 1;
        var ptr = {};
        var best = {};
        var count = 0;
        for (var i = 0; i < rows.length; i++) {
            var r = rows[i];
            var first = null;
            ids.forEach(function (n) {
                var a = by[n];
                if (ptr[n] === undefined) ptr[n] = 0;
                while (ptr[n] < a.length && a[ptr[n]].ts <= r.ts) { best[n] = a[ptr[n]]; ptr[n]++; }
                var b = best[n];
                var ok = !!b && (r.ts - b.ts) <= gap;
                var t = ok ? b.air_t : NaN, p = ok ? b.air_p : NaN, h = ok ? b.air_h : NaN;
                if (multi) { r['air_t_' + n] = t; r['air_p_' + n] = p; r['air_h_' + n] = h; }
                if (first === null) first = { t: t, p: p, h: h };
            });
            if (first === null) first = { t: NaN, p: NaN, h: NaN };
            r.air_t = first.t; r.air_p = first.p; r.air_h = first.h;
            if (isFiniteNum(r.air_t) || isFiniteNum(r.air_p) || isFiniteNum(r.air_h)) count++;
        }
        return count;
    }
    return {
        visibleDefs: visibleDefs,
        axisDefs: axisDefs,
        axisLabel: axisLabel,
        nearestIndex: nearestIndex,
        sliceRange: sliceRange,
        axisScale: axisScale,
        scaleFor: scaleFor,
        cursorValues: cursorValues,
        plotPoints: plotPoints,
        yToPx: yToPx,
        xToPx: xToPx,
        pxToTime: pxToTime,
        zoomRange: zoomRange,
        fullRange: fullRange,
        parseEnvCsv: parseEnvCsv,
        mergeEnv: mergeEnv
    };
});
