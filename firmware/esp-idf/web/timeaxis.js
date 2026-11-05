(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(); } else { root.AFTX = factory(); } })(this, function () { 'use strict';

function pad(n) {
    var s = String(n);
    return s.length < 2 ? '0' + s : s;
}

function off(ts) {
    return new Date(ts * 1000).getTimezoneOffset();
}

function wall(ts) {
    return ts - off(ts) * 60;
}

function unwall(W) {
    var t0 = W + off(W) * 60;
    return W + off(t0) * 60;
}

function fmtLabel(ts, mode) {
    var d = new Date(ts * 1000);
    var h = pad(d.getHours());
    var m = pad(d.getMinutes());
    if (mode === 'd') {
        return pad(d.getDate()) + '.' + pad(d.getMonth() + 1);
    }
    if (mode === 's') {
        return h + ':' + m + ':' + pad(d.getSeconds());
    }
    return h + ':' + m;
}

function weekdayShort(ts) {
    var names = ['Вс', 'Пн', 'Вт', 'Ср', 'Чт', 'Пт', 'Сб'];
    return names[new Date(ts * 1000).getDay()];
}

function dateLabel(ts, withYear) {
    var d = new Date(ts * 1000);
    var base = pad(d.getDate()) + '.' + pad(d.getMonth() + 1);
    if (withYear) {
        return base + '.' + d.getFullYear();
    }
    return base;
}

function tooltipTime(ts) {
    var d = new Date(ts * 1000);
    return pad(d.getDate()) + '.' + pad(d.getMonth() + 1) + '.' + d.getFullYear() + ' ' +
        pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
}

function windowLabel(t0, t1) {
    var d0 = new Date(t0 * 1000);
    var d1 = new Date(t1 * 1000);
    var sameYear = d0.getFullYear() === d1.getFullYear();
    var a = pad(d0.getDate()) + '.' + pad(d0.getMonth() + 1);
    var b = pad(d1.getDate()) + '.' + pad(d1.getMonth() + 1);
    if (!sameYear) {
        a += '.' + d0.getFullYear();
        b += '.' + d1.getFullYear();
    }
    a += ' ' + pad(d0.getHours()) + ':' + pad(d0.getMinutes());
    b += ' ' + pad(d1.getHours()) + ':' + pad(d1.getMinutes());
    return 'с ' + a + ' по ' + b;
}

function midnights(t0, t1) {
    if (typeof t0 !== 'number' || typeof t1 !== 'number' ||
        !isFinite(t0) || !isFinite(t1) || t1 < t0) {
        return [];
    }
    var d = new Date(t0 * 1000);
    var y = d.getFullYear();
    var m = d.getMonth();
    var day = d.getDate();
    var res = [];
    var i;
    for (i = 0; i < 4000; i++) {
        var dt = new Date(y, m, day + i, 0, 0, 0);
        var ts = dt.getTime() / 1000;
        if (ts > t1) {
            break;
        }
        if (ts >= t0) {
            res.push(ts);
        }
    }
    return res;
}

function axisPlan(t0, t1, count) {
    if (typeof t0 !== 'number' || typeof t1 !== 'number' ||
        !isFinite(t0) || !isFinite(t1) || t1 <= t0) {
        return { mode: 'm', ticks: [], marks: [] };
    }
    var span = t1 - t0;
    var mode;
    if (span < 600) {
        mode = 's';
    } else if (span <= 172800) {
        mode = 'm';
    } else {
        mode = 'd';
    }
    var d0 = new Date(t0 * 1000);
    var d1 = new Date(t1 * 1000);
    var withYear = d0.getFullYear() !== d1.getFullYear();
    var ticks = [];
    var marks = [];
    var mm = midnights(t0, t1);
    var i;

    if (mode === 's' || mode === 'm') {
        var steps = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 10800, 21600, 43200];
        var step = steps[steps.length - 1];
        for (i = 0; i < steps.length; i++) {
            if (span / steps[i] <= count) {
                step = steps[i];
                break;
            }
        }
        var W = Math.ceil(wall(t0) / step) * step;
        var prevTs = null;
        var prevDateKey = null;
        var first = true;
        while (true) {
            var ts = unwall(W);
            if (ts > t1) {
                break;
            }
            if (ts >= t0 && (prevTs === null || ts > prevTs)) {
                var dd = new Date(ts * 1000);
                var dateKey = dd.getFullYear() + '-' + dd.getMonth() + '-' + dd.getDate();
                var dateStr = null;
                if (first || dateKey !== prevDateKey) {
                    dateStr = dateLabel(ts, withYear);
                }
                ticks.push({
                    ts: ts,
                    label: fmtLabel(ts, mode),
                    date: dateStr
                });
                prevTs = ts;
                prevDateKey = dateKey;
                first = false;
            }
            W += step;
        }
    } else {
        var kSteps = [1, 2, 3, 5, 7, 14, 30, 60, 90, 180, 365];
        var k = kSteps[kSteps.length - 1];
        for (i = 0; i < kSteps.length; i++) {
            if (Math.ceil(mm.length / kSteps[i]) <= count) {
                k = kSteps[i];
                break;
            }
        }
        for (i = 0; i < mm.length; i += k) {
            ticks.push({
                ts: mm[i],
                label: dateLabel(mm[i], withYear),
                date: null
            });
        }
    }

    for (i = 0; i < mm.length; i++) {
        marks.push({
            ts: mm[i],
            date: dateLabel(mm[i], withYear),
            wd: weekdayShort(mm[i])
        });
    }

    return { mode: mode, ticks: ticks, marks: marks };
}

return {
    fmtLabel: fmtLabel,
    weekdayShort: weekdayShort,
    dateLabel: dateLabel,
    tooltipTime: tooltipTime,
    windowLabel: windowLabel,
    midnights: midnights,
    axisPlan: axisPlan
};
});
