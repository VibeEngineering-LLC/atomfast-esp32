process.env.TZ = 'Europe/Moscow';
const assert = require('assert');
const path = require('path');
const X = require(process.env.HT_MODULE || path.join(__dirname, '..', '..', 'web', 'timeaxis.js'));
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }
const U = (y, mo, d, h, mi, s) => Date.UTC(y, mo - 1, d, h, mi || 0, s || 0) / 1000;
const MSK = (y, mo, d, h, mi, s) => U(y, mo, d, h - 3, mi, s);
function withTZ(tz, fn) { const old = process.env.TZ; process.env.TZ = tz; try { fn(); } finally { process.env.TZ = old; } }

check('fmt_label_modes', () => {
    const ts = MSK(2026, 10, 25, 9, 5, 9);
    assert.strictEqual(X.fmtLabel(ts, 's'), '09:05:09');
    assert.strictEqual(X.fmtLabel(ts, 'm'), '09:05');
    assert.strictEqual(X.fmtLabel(ts, 'd'), '25.10');
});

check('weekday_short_ru', () => {
    assert.strictEqual(X.weekdayShort(MSK(2026, 10, 25, 12, 0, 0)), 'Вс');
    assert.strictEqual(X.weekdayShort(MSK(2026, 10, 26, 12, 0, 0)), 'Пн');
    assert.strictEqual(X.weekdayShort(MSK(2026, 10, 27, 12, 0, 0)), 'Вт');
    assert.strictEqual(X.weekdayShort(MSK(2026, 10, 31, 12, 0, 0)), 'Сб');
});

check('date_label_and_tooltip', () => {
    const ts = MSK(2026, 10, 25, 9, 5, 9);
    assert.strictEqual(X.dateLabel(ts, false), '25.10');
    assert.strictEqual(X.dateLabel(ts, true), '25.10.2026');
    assert.strictEqual(X.tooltipTime(ts), '25.10.2026 09:05:09');
});

check('window_label', () => {
    assert.strictEqual(X.windowLabel(MSK(2026, 10, 24, 18, 0, 0), MSK(2026, 10, 25, 18, 0, 0)), 'с 24.10 18:00 по 25.10 18:00');
    assert.strictEqual(X.windowLabel(MSK(2026, 12, 30, 12, 0, 0), MSK(2027, 1, 2, 12, 0, 0)), 'с 30.12.2026 12:00 по 02.01.2027 12:00');
});

check('midnights_are_local_not_utc', () => {
    const m = X.midnights(MSK(2026, 10, 24, 12, 0, 0), MSK(2026, 10, 26, 12, 0, 0));
    assert.deepStrictEqual(m, [MSK(2026, 10, 25, 0, 0, 0), MSK(2026, 10, 26, 0, 0, 0)]);
    assert.strictEqual(m[0] % 86400, 75600);
});

check('midnights_inclusive_edges_and_invalid', () => {
    const a = MSK(2026, 10, 25, 0, 0, 0);
    const b = MSK(2026, 10, 26, 0, 0, 0);
    assert.deepStrictEqual(X.midnights(a, b), [a, b]);
    assert.deepStrictEqual(X.midnights(b, a), []);
    assert.deepStrictEqual(X.midnights(NaN, b), []);
});

check('plan_one_hour', () => {
    const p = X.axisPlan(MSK(2026, 10, 25, 9, 5, 0), MSK(2026, 10, 25, 10, 5, 0), 8);
    assert.strictEqual(p.mode, 'm');
    assert.deepStrictEqual(p.ticks.map(t => t.label), ['09:10', '09:20', '09:30', '09:40', '09:50', '10:00']);
    assert.strictEqual(p.ticks[0].date, '25.10');
    assert.deepStrictEqual(p.ticks.slice(1).map(t => t.date), [null, null, null, null, null]);
    assert.deepStrictEqual(p.marks, []);
});

check('plan_six_hours', () => {
    const p = X.axisPlan(MSK(2026, 10, 25, 10, 0, 0), MSK(2026, 10, 25, 16, 0, 0), 8);
    assert.strictEqual(p.mode, 'm');
    assert.deepStrictEqual(p.ticks.map(t => t.label), ['10:00', '11:00', '12:00', '13:00', '14:00', '15:00', '16:00']);
    assert.strictEqual(p.ticks[0].date, '25.10');
    assert.strictEqual(p.ticks[3].date, null);
});

check('plan_seconds_mode', () => {
    const p = X.axisPlan(MSK(2026, 10, 25, 9, 5, 0), MSK(2026, 10, 25, 9, 10, 0), 8);
    assert.strictEqual(p.mode, 's');
    assert.deepStrictEqual(p.ticks.map(t => t.label), ['09:05:00', '09:06:00', '09:07:00', '09:08:00', '09:09:00', '09:10:00']);
});

check('plan_24h_midnight_date', () => {
    const p = X.axisPlan(MSK(2026, 10, 24, 18, 0, 0), MSK(2026, 10, 25, 18, 0, 0), 8);
    assert.strictEqual(p.mode, 'm');
    assert.deepStrictEqual(p.ticks.map(t => t.label), ['18:00', '21:00', '00:00', '03:00', '06:00', '09:00', '12:00', '15:00', '18:00']);
    assert.strictEqual(p.ticks[0].date, '24.10');
    assert.strictEqual(p.ticks[2].date, '25.10');
    assert.strictEqual(p.ticks.filter(t => t.date !== null).length, 2);
    assert.deepStrictEqual(p.marks.map(m => m.date), ['25.10']);
    assert.strictEqual(p.marks[0].wd, 'Вс');
    assert.strictEqual(p.marks[0].ts, MSK(2026, 10, 25, 0, 0, 0));
});

check('plan_three_days_day_labels', () => {
    const p = X.axisPlan(MSK(2026, 10, 23, 15, 0, 0), MSK(2026, 10, 26, 15, 0, 0), 8);
    assert.strictEqual(p.mode, 'd');
    assert.deepStrictEqual(p.ticks.map(t => t.label), ['24.10', '25.10', '26.10']);
    assert.deepStrictEqual(p.ticks.map(t => t.date), [null, null, null]);
    assert.deepStrictEqual(p.marks.map(m => m.wd), ['Сб', 'Вс', 'Пн']);
});

check('plan_thirty_days_stride', () => {
    const p = X.axisPlan(MSK(2026, 10, 1, 0, 0, 0), MSK(2026, 10, 31, 0, 0, 0), 8);
    assert.strictEqual(p.mode, 'd');
    assert.deepStrictEqual(p.ticks.map(t => t.label), ['01.10', '06.10', '11.10', '16.10', '21.10', '26.10', '31.10']);
    assert.strictEqual(p.marks.length, 31);
});

check('plan_year_change_labels', () => {
    const p = X.axisPlan(MSK(2026, 12, 30, 12, 0, 0), MSK(2027, 1, 2, 12, 0, 0), 8);
    assert.strictEqual(p.mode, 'd');
    assert.deepStrictEqual(p.ticks.map(t => t.label), ['31.12.2026', '01.01.2027', '02.01.2027']);
    assert.deepStrictEqual(p.marks.map(m => m.date), ['31.12.2026', '01.01.2027', '02.01.2027']);
});

check('plan_same_year_no_year', () => {
    const p = X.axisPlan(MSK(2026, 10, 24, 18, 0, 0), MSK(2026, 10, 25, 18, 0, 0), 8);
    assert.ok(p.marks.every(m => !/\d{4}$/.test(m.date)));
});

check('plan_invalid_window', () => {
    assert.deepStrictEqual(X.axisPlan(10, 10, 8), { mode: 'm', ticks: [], marks: [] });
    assert.deepStrictEqual(X.axisPlan(20, 10, 8), { mode: 'm', ticks: [], marks: [] });
    assert.deepStrictEqual(X.axisPlan(NaN, 10, 8), { mode: 'm', ticks: [], marks: [] });
});

check('plan_window_start_on_midnight', () => {
    const a = MSK(2026, 10, 25, 0, 0, 0);
    const p = X.axisPlan(a, a + 6 * 3600, 8);
    assert.strictEqual(p.ticks[0].label, '00:00');
    assert.strictEqual(p.ticks[0].date, '25.10');
    assert.deepStrictEqual(p.marks.map(m => m.ts), [a]);
});

check('dst_day_is_25_hours', () => {
    withTZ('Europe/Berlin', () => {
        const t0 = U(2026, 10, 24, 10, 0, 0);
        const t1 = U(2026, 10, 26, 11, 0, 0);
        const m = X.midnights(t0, t1);
        assert.strictEqual(m.length, 2);
        assert.strictEqual(m[0], U(2026, 10, 24, 22, 0, 0));
        assert.strictEqual(m[1], U(2026, 10, 25, 23, 0, 0));
        assert.strictEqual(m[1] - m[0], 90000);
        const p = X.axisPlan(t0, t1, 8);
        assert.strictEqual(p.mode, 'd');
        assert.deepStrictEqual(p.ticks.map(t => t.label), ['25.10', '26.10']);
    });
});

check('dst_hour_ticks_monotonic', () => {
    withTZ('Europe/Berlin', () => {
        const t0 = U(2026, 10, 24, 22, 0, 0);
        const t1 = U(2026, 10, 25, 5, 0, 0);
        const p = X.axisPlan(t0, t1, 8);
        assert.strictEqual(p.mode, 'm');
        assert.deepStrictEqual(p.ticks.map(t => t.label), ['00:00', '01:00', '02:00', '03:00', '04:00', '05:00', '06:00']);
        for (let i = 1; i < p.ticks.length; i++) assert.ok(p.ticks[i].ts > p.ticks[i - 1].ts);
        assert.ok(p.ticks.every(t => new Date(t.ts * 1000).getMinutes() === 0));
        assert.strictEqual(p.ticks[0].date, '25.10');
    });
});

check('timezone_is_browser_local', () => {
    withTZ('Asia/Tokyo', () => {
        const ts = U(2026, 10, 25, 0, 30, 0);
        assert.strictEqual(X.fmtLabel(ts, 'm'), '09:30');
        assert.strictEqual(X.midnights(U(2026, 10, 24, 20, 0, 0), U(2026, 10, 26, 0, 0, 0))[0], U(2026, 10, 25, 15, 0, 0));
    });
});

console.log('TESTS: ' + passed + '/' + total); process.exit(passed === total ? 0 : 1);
