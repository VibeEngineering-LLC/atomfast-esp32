process.env.TZ = 'Europe/Berlin';
const assert = require('assert');
const path = require('path');
const R = require(process.env.HR_MODULE || path.join(__dirname, '..', '..', 'web', 'range.js'));
let passed = 0, total = 0;
const queue = [];
function check(name, fn) { queue.push([name, fn]); }
const L = (y, mo, d, h, mi) => Math.floor(new Date(y, mo - 1, d, h, mi, 0, 0).getTime() / 1000);
const NOW = L(2026, 10, 22, 15, 30);
const A = 1792853245;
const B = 1792859104;

check('is_ts_bounds', () => {
    assert.strictEqual(R.isTs(1000000000), true);
    assert.strictEqual(R.isTs(4294967295), true);
    for (const v of [999999999, 4294967296, 1.5, '1792853245', NaN, null, -5]) assert.strictEqual(R.isTs(v), false, String(v));
});

check('input_roundtrip_and_format', () => {
    assert.strictEqual(R.toInput(L(2026, 10, 22, 15, 30)), '2026-10-22T15:30');
    assert.strictEqual(R.toInput(L(2026, 1, 2, 3, 4)), '2026-01-02T03:04');
    assert.strictEqual(R.fromInput('2026-10-22T15:30'), L(2026, 10, 22, 15, 30));
    assert.strictEqual(R.fromInput(R.toInput(A)), A - (A % 60));
});

check('input_local_not_utc', () => {
    const ts = R.fromInput('2026-10-22T15:30');
    assert.notStrictEqual(ts, Date.UTC(2026, 9, 22, 15, 30) / 1000);
    assert.strictEqual(Date.UTC(2026, 9, 22, 15, 30) / 1000 - ts, 7200);
    assert.strictEqual(R.toInput(Date.UTC(2026, 9, 22, 13, 30) / 1000), '2026-10-22T15:30');
});

check('input_rejects_invalid', () => {
    for (const s of ['', null, undefined, 5, '2026-10-22', '2026-10-22 15:30', '2026-13-01T10:00', '2026-02-30T10:00', '2026-10-22T24:00', '2026-10-22T10:60', '2026-10-22T10:00:60', '2026-10-22T10:00Z', 'x2026-10-22T10:00']) assert.strictEqual(R.fromInput(s), null, String(s));
});

check('input_accepts_seconds', () => {
    assert.strictEqual(R.fromInput('2026-10-22T15:30:45'), L(2026, 10, 22, 15, 30) + 45);
});

check('input_dst_gap_rejected_and_overlap_roundtrips', () => {
    assert.strictEqual(R.fromInput('2026-03-29T02:30'), null);
    assert.strictEqual(R.fromInput('2026-03-29T03:30') - R.fromInput('2026-03-29T01:30'), 3600);
    const amb = R.fromInput('2026-10-25T02:30');
    assert.strictEqual(R.toInput(amb), '2026-10-25T02:30');
});

check('label_format', () => {
    assert.strictEqual(R.label(L(2026, 10, 22, 9, 5), L(2026, 10, 23, 18, 45)), '22.10 09:05 — 23.10 18:45');
});

check('validate_ok', () => {
    assert.deepStrictEqual(R.validate({ from: NOW - 3600, to: NOW }, { now: NOW }), { ok: true, error: '' });
    assert.strictEqual(R.validate({ from: A, to: B }, {}).ok, true);
});

check('validate_order_and_span', () => {
    assert.strictEqual(R.validate({ from: B, to: A }, {}).ok, false);
    assert.ok(R.validate({ from: B, to: A }, {}).error.indexOf('раньше') !== -1);
    assert.strictEqual(R.validate({ from: A, to: A }, {}).ok, false);
    assert.strictEqual(R.validate({ from: A, to: A + 59 }, {}).ok, false);
    assert.strictEqual(R.validate({ from: A, to: A + 60 }, {}).ok, true);
});

check('validate_not_future', () => {
    assert.strictEqual(R.validate({ from: NOW - 100, to: NOW + 60 }, { now: NOW }).ok, true);
    const r = R.validate({ from: NOW - 100, to: NOW + 61 }, { now: NOW });
    assert.strictEqual(r.ok, false);
    assert.ok(r.error.indexOf('будущем') !== -1);
    assert.strictEqual(R.validate({ from: NOW - 100, to: NOW + 5000 }, {}).ok, true);
});

check('validate_history_start', () => {
    assert.strictEqual(R.validate({ from: A, to: B }, { first: A }).ok, true);
    const r = R.validate({ from: A - 1, to: B }, { first: A });
    assert.strictEqual(r.ok, false);
    assert.ok(r.error.indexOf('истории') !== -1);
    assert.strictEqual(R.validate({ from: A - 1, to: B }, {}).ok, true);
});

check('validate_non_numbers', () => {
    for (const r of [null, {}, { from: 'a', to: 'b' }, { from: 5, to: 10 }, { from: A }, { to: B }]) assert.strictEqual(R.validate(r, {}).ok, false, JSON.stringify(r));
});

check('presets_ids_and_order', () => {
    assert.deepStrictEqual(R.presets(NOW).map(p => p.id), ['yesterday', 'today', 'last3d', 'last7d', 'prevweek']);
    assert.deepStrictEqual(R.presets(NOW).map(p => p.label), ['Вчера', 'Сегодня', 'Последние 3 дня', 'Последние 7 дней', 'Прошлая неделя']);
});

check('presets_fixed_date_values', () => {
    const p = {};
    for (const x of R.presets(NOW)) p[x.id] = x;
    assert.deepStrictEqual([p.yesterday.from, p.yesterday.to], [L(2026, 10, 21, 0, 0), L(2026, 10, 22, 0, 0)]);
    assert.deepStrictEqual([p.today.from, p.today.to], [L(2026, 10, 22, 0, 0), NOW]);
    assert.deepStrictEqual([p.last3d.from, p.last3d.to], [NOW - 259200, NOW]);
    assert.deepStrictEqual([p.last7d.from, p.last7d.to], [NOW - 604800, NOW]);
    assert.deepStrictEqual([p.prevweek.from, p.prevweek.to], [L(2026, 10, 12, 0, 0), L(2026, 10, 19, 0, 0)]);
});

check('presets_sunday_belongs_to_week_ending_that_day', () => {
    const sun = L(2026, 10, 18, 12, 0);
    const pw = R.presets(sun).find(x => x.id === 'prevweek');
    assert.deepStrictEqual([pw.from, pw.to], [L(2026, 10, 5, 0, 0), L(2026, 10, 12, 0, 0)]);
    const mon = L(2026, 10, 19, 8, 0);
    const pm = R.presets(mon).find(x => x.id === 'prevweek');
    assert.deepStrictEqual([pm.from, pm.to], [L(2026, 10, 12, 0, 0), L(2026, 10, 19, 0, 0)]);
});

check('presets_dst_day_lengths', () => {
    const now = L(2026, 10, 26, 10, 0);
    const p = {};
    for (const x of R.presets(now)) p[x.id] = x;
    assert.strictEqual(p.yesterday.to - p.yesterday.from, 90000);
    assert.strictEqual(p.prevweek.to - p.prevweek.from, 608400);
    assert.strictEqual(p.today.from, L(2026, 10, 26, 0, 0));
    const spring = L(2026, 3, 30, 10, 0);
    const y = R.presets(spring).find(x => x.id === 'yesterday');
    assert.strictEqual(y.to - y.from, 82800);
});

check('presets_all_valid_on_normal_day', () => {
    for (const p of R.presets(NOW)) assert.strictEqual(R.validate(p, { now: NOW }).ok, true, p.id);
});

check('last_n_units', () => {
    assert.deepStrictEqual(R.lastN(30, 'min', NOW), { from: NOW - 1800, to: NOW });
    assert.deepStrictEqual(R.lastN(5, 'h', NOW), { from: NOW - 18000, to: NOW });
    assert.deepStrictEqual(R.lastN(2, 'd', NOW), { from: NOW - 172800, to: NOW });
});

check('last_n_invalid', () => {
    for (const n of [0, -1, 1.5, '3', NaN, null, 100000]) assert.strictEqual(R.lastN(n, 'h', NOW), null, String(n));
    for (const u of ['x', '', 'constructor', 'hour', undefined]) assert.strictEqual(R.lastN(3, u, NOW), null, String(u));
});

check('estimate_and_max_span', () => {
    assert.strictEqual(R.estimate(0, 100, 2), 50);
    assert.strictEqual(R.estimate(0, 101, 2), 51);
    assert.strictEqual(R.maxSpan('raw'), 8000);
    assert.strictEqual(R.maxSpan('agg'), 1260000);
    assert.strictEqual(R.maxSpan('raw', { rate: 1 }), 4000);
    assert.strictEqual(R.maxSpan('agg', { windowS: 30 }), 630000);
});

check('span_label', () => {
    assert.strictEqual(R.spanLabel(1800), '30 мин');
    assert.strictEqual(R.spanLabel(8000), '2.2 ч');
    assert.strictEqual(R.spanLabel(172800), '2 сут');
    assert.strictEqual(R.spanLabel(1260000), '14.6 сут');
});

check('raw_warning_threshold', () => {
    assert.strictEqual(R.rowWarning('raw', A, A + 8000, {}).warn, false);
    const w = R.rowWarning('raw', A, A + 8002, {});
    assert.strictEqual(w.warn, true);
    assert.strictEqual(w.rows, 4001);
    assert.strictEqual(w.limit, 4000);
    assert.strictEqual(w.suggestAgg, true);
    assert.ok(w.text.indexOf('Агрегаты') !== -1 && w.text.indexOf('4001') !== -1 && w.text.indexOf('4000') !== -1);
});

check('raw_warning_uses_rate', () => {
    assert.strictEqual(R.rowWarning('raw', A, A + 8000, { rate: 1 }).warn, true);
    assert.strictEqual(R.rowWarning('raw', A, A + 4000, { rate: 1 }).warn, false);
});

check('agg_warning_threshold', () => {
    assert.strictEqual(R.rowWarning('agg', A, A + 1260000, {}).warn, false);
    const w = R.rowWarning('agg', A, A + 1260061, {});
    assert.strictEqual(w.warn, true);
    assert.strictEqual(w.suggestAgg, false);
    assert.ok(w.text.indexOf('сузьте') !== -1);
});

check('raw_warning_without_agg_escape', () => {
    const w = R.rowWarning('raw', A, A + 2000000, {});
    assert.strictEqual(w.warn, true);
    assert.strictEqual(w.suggestAgg, false);
    assert.ok(w.text.indexOf('сузьте') !== -1);
});

check('query_format_and_order', () => {
    assert.strictEqual(R.query('raw', A, B, 4000), 'kind=raw&from=' + A + '&to=' + B + '&limit=4000');
    assert.strictEqual(R.query('agg', A, B, 21000), 'kind=agg&from=' + A + '&to=' + B + '&limit=21000');
    assert.strictEqual(R.query('env', A, B, 5000.9), 'kind=env&from=' + A + '&to=' + B + '&limit=5000');
});

check('query_invalid', () => {
    assert.strictEqual(R.query('bogus', A, B, 10), '');
    assert.strictEqual(R.query('raw', B, A, 10), '');
    assert.strictEqual(R.query('raw', A, A, 10), '');
    assert.strictEqual(R.query('raw', 1.5, B, 10), '');
    assert.strictEqual(R.query('raw', A, B, 0), 'kind=raw&from=' + A + '&to=' + B + '&limit=1');
});

check('url_seconds', () => {
    assert.deepStrictEqual(R.parseUrl('?from=' + A + '&to=' + B), { from: A, to: B });
    assert.deepStrictEqual(R.parseUrl('theme=dark&to=' + B + '&from=' + A), { from: A, to: B });
});

check('url_iso_with_zone_and_local', () => {
    assert.deepStrictEqual(R.parseUrl('?from=2026-10-22T10:00:00Z&to=2026-10-22T12:00:00Z'), { from: Date.UTC(2026, 9, 22, 10) / 1000, to: Date.UTC(2026, 9, 22, 12) / 1000 });
    assert.deepStrictEqual(R.parseUrl('?from=2026-10-22T10:00:00%2B02:00&to=2026-10-22T12:00:00%2B02:00'), { from: Date.UTC(2026, 9, 22, 8) / 1000, to: Date.UTC(2026, 9, 22, 10) / 1000 });
    assert.deepStrictEqual(R.parseUrl('?from=2026-10-22T10:00&to=2026-10-22T12:00'), { from: L(2026, 10, 22, 10, 0), to: L(2026, 10, 22, 12, 0) });
    assert.deepStrictEqual(R.parseUrl('?from=2026-10-22T10:00:30&to=' + B), { from: L(2026, 10, 22, 10, 0) + 30, to: B });
});

check('url_garbage_ignored', () => {
    const bad = ['', '?', '?from=' + A, '?to=' + B, '?from=abc&to=' + B, '?from=' + A + '&to=xyz', '?from=' + B + '&to=' + A, '?from=' + A + '&to=' + A,
        '?from=-5&to=' + B, '?from=99999999999&to=' + B, '?from=5&to=' + B, '?from=1.5e9&to=' + B, '?from=0x10&to=' + B, '?from=' + A + '&to=',
        '?from=2026-02-30T10:00&to=' + B, '?from=2026-10-22&to=' + B, '?from=<script>&to=' + B, '?from=' + A + ' &to=' + B];
    for (const s of bad) assert.strictEqual(R.parseUrl(s), null, s);
    assert.strictEqual(R.parseUrl(null), null);
    assert.strictEqual(R.parseUrl(undefined), null);
});

check('link_for', () => {
    assert.strictEqual(R.linkFor('/', { from: A, to: B }), '/?from=' + A + '&to=' + B);
    assert.deepStrictEqual(R.parseUrl(R.linkFor('/', { from: A, to: B }).slice(1)), { from: A, to: B });
});

check('selection_to_range', () => {
    assert.deepStrictEqual(R.selectionToRange(A + 0.2, B - 0.2), { from: A, to: B });
    assert.deepStrictEqual(R.selectionToRange(B, A), { from: A, to: B });
    assert.strictEqual(R.selectionToRange(A, A + 9), null);
    assert.deepStrictEqual(R.selectionToRange(A, A + 10), { from: A, to: A + 10 });
    assert.strictEqual(R.selectionToRange(A, A + 30, 60), null);
    assert.strictEqual(R.selectionToRange(NaN, A), null);
    assert.strictEqual(R.selectionToRange(Infinity, A), null);
});

check('store_garbage_and_version', () => {
    for (const t of ['', 'x', '{', 'null', '[]', '{}', '{"v":2}', '{"v":"1"}']) assert.deepStrictEqual(R.parseStore(t), R.emptyStore(), t);
});

check('store_validation', () => {
    const s = R.parseStore(JSON.stringify({ v: 1, last: { from: A, to: B }, active: true, lastN: { n: 3, unit: 'h' } }));
    assert.deepStrictEqual(s, { v: 1, last: { from: A, to: B }, active: true, lastN: { n: 3, unit: 'h' } });
    assert.strictEqual(R.parseStore(JSON.stringify({ v: 1, last: { from: B, to: A }, active: true })).last, null);
    assert.strictEqual(R.parseStore(JSON.stringify({ v: 1, last: { from: 'a', to: B }, active: true })).active, false);
    assert.strictEqual(R.parseStore(JSON.stringify({ v: 1, last: null, active: true })).active, false);
    assert.strictEqual(R.parseStore(JSON.stringify({ v: 1, last: { from: A, to: B }, active: 'yes' })).active, false);
    assert.strictEqual(R.parseStore(JSON.stringify({ v: 1, lastN: { n: 0, unit: 'h' } })).lastN, null);
    assert.strictEqual(R.parseStore(JSON.stringify({ v: 1, lastN: { n: 3, unit: 'constructor' } })).lastN, null);
});

check('store_roundtrip', () => {
    const s = { v: 1, last: { from: A, to: B }, active: true, lastN: { n: 7, unit: 'd' } };
    assert.deepStrictEqual(R.parseStore(R.serializeStore(s)), s);
    assert.strictEqual(R.KEY, 'afh.range.v1');
});

check('in_history_edges', () => {
    assert.strictEqual(R.inHistory({ from: A - 100, to: A }, A, B), false);
    assert.strictEqual(R.inHistory({ from: A - 100, to: A + 1 }, A, B), true);
    assert.strictEqual(R.inHistory({ from: B, to: B + 100 }, A, B), true);
    assert.strictEqual(R.inHistory({ from: B + 1, to: B + 100 }, A, B), false);
    assert.strictEqual(R.inHistory({ from: A, to: B }, undefined, undefined), true);
});

check('restore_range_rules', () => {
    const st = { v: 1, last: { from: A, to: B }, active: true, lastN: null };
    assert.deepStrictEqual(R.restoreRange(st, { now: B + 1000 }), { from: A, to: B });
    assert.strictEqual(R.restoreRange(Object.assign({}, st, { active: false }), { now: B + 1000 }), null);
    assert.strictEqual(R.restoreRange(st, { now: A - 1000 }), null);
    assert.strictEqual(R.restoreRange(st, { now: B + 1000, first: B + 10, last: B + 500 }), null);
    assert.strictEqual(R.restoreRange(st, { now: B + 1000, first: A - 500, last: A - 10 }), null);
    assert.deepStrictEqual(R.restoreRange(st, { now: B + 1000, first: A - 500, last: B + 10 }), { from: A, to: B });
    assert.strictEqual(R.restoreRange(null, {}), null);
    assert.strictEqual(R.restoreRange(R.emptyStore(), { now: B }), null);
});

check('auto_policy', () => {
    const on = R.autoState({ from: A, to: B }, true);
    assert.strictEqual(on.checked, false);
    assert.strictEqual(on.disabled, true);
    assert.ok(on.title.indexOf('автообновление выключено') !== -1);
    assert.deepStrictEqual(R.autoState(null, true), { checked: true, disabled: false, title: 'Автообновление' });
    assert.strictEqual(R.autoState(null, false).checked, false);
    assert.strictEqual(R.autoState(null, undefined).checked, true);
});

check('latest_aborts_previous', () => {
    const l = R.createLatest();
    const a = l.begin();
    assert.strictEqual(l.isCurrent(a), true);
    assert.strictEqual(a.signal.aborted, false);
    const b = l.begin();
    assert.strictEqual(a.signal.aborted, true);
    assert.strictEqual(l.isCurrent(a), false);
    assert.strictEqual(l.isCurrent(b), true);
    assert.strictEqual(b.signal.aborted, false);
});

check('latest_cancel_and_null', () => {
    const l = R.createLatest();
    const a = l.begin();
    l.cancel();
    assert.strictEqual(a.signal.aborted, true);
    assert.strictEqual(l.isCurrent(a), false);
    assert.strictEqual(l.isCurrent(null), false);
    const b = l.begin();
    assert.strictEqual(l.isCurrent(b), true);
    assert.notStrictEqual(a.id, b.id);
});

(async () => {
    for (const [name, fn] of queue) {
        total++;
        try { await fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); }
    }
    console.log('TESTS: ' + passed + '/' + total);
    process.exit(passed === total ? 0 : 1);
})();
