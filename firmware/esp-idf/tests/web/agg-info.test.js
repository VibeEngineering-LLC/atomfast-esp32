const assert = require('assert');
const path = require('path');
const A = require(process.env.HG_MODULE || path.join(__dirname, '..', '..', 'web', 'agg-info.js'));
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }

check('интервал берётся из window_s ответа, а не константа', () => {
    assert.strictEqual(A.windowOf({ window_s: 60 }), 60);
    assert.strictEqual(A.windowOf({ window_s: 30 }), 30);
    assert.strictEqual(A.windowOf({ window_s: 120, raw: {} }), 120);
});

check('без данных или с мусором интервал null', () => {
    for (const v of [null, undefined, {}, [], 'x', { window_s: 0 }, { window_s: -5 }, { window_s: 1.5 }, { window_s: '60' }, { window_s: NaN }, { window_s: Infinity }, { window_s: null }]) assert.strictEqual(A.windowOf(v), null, JSON.stringify(v));
});

check('подсказка на обеих кнопках, у агрегатов с числом', () => {
    assert.strictEqual(A.title('raw', 60), 'Сырые — каждое измерение (шаг ≈2 с)');
    assert.strictEqual(A.title('raw', null), 'Сырые — каждое измерение (шаг ≈2 с)');
    assert.strictEqual(A.title('agg', 60), 'Агрегаты — усреднённые за 60 с точки, удобны для длинных окон; хранятся дольше');
    assert.strictEqual(A.title('agg', 15), 'Агрегаты — усреднённые за 15 с точки, удобны для длинных окон; хранятся дольше');
    assert.strictEqual(A.title('x', 60), '');
});

check('без данных: н/д вместо числа', () => {
    assert.strictEqual(A.title('agg', null), 'Агрегаты — усреднённые за н/д точки, удобны для длинных окон; хранятся дольше');
    assert.ok(A.note('agg', null).indexOf('автоматически') > 0);
    assert.strictEqual(A.storageLabel(null), 'усреднённые точки за н/д');
});

check('строка под переключателем: выбранный вид, без ссылки на настройки', () => {
    assert.strictEqual(A.note('raw', 60), 'Масштаб подбирается автоматически по ширине графика.');
    assert.strictEqual(A.note('agg', 60), 'Масштаб подбирается автоматически по ширине графика.');
    assert.ok(A.note('agg', 60).indexOf('фиксирован') < 0 && A.note('agg', 60).indexOf('60') < 0);
    assert.strictEqual(A.note('x', 60), '');
    assert.ok(A.note('agg', 60).indexOf('Настройки') < 0 && A.note('agg', 60).indexOf('/system') < 0);
});

check('подпись в «Хранилище»', () => {
    assert.strictEqual(A.storageLabel(60), 'усреднённые точки за 60 с');
    assert.strictEqual(A.storageLabel(45), 'усреднённые точки за 45 с');
});

check('1ч, 900px, 100k raw', () => {
  const r = A.pickView(3600, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.kind, 'raw');
  assert.strictEqual(r.pts, 0);
  assert.strictEqual(r.expected, 1800);
  assert.strictEqual(r.from, 1699996400);
});
check('24ч, 900px, 100k raw', () => {
  const r = A.pickView(86400, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.kind, 'raw');
  assert.strictEqual(r.pts, 900);
  assert.strictEqual(r.expected, 43200);
});
check('7дн, 1200px, 100k raw, 9k agg', () => {
  const r = A.pickView(604800, 1200, { raw: { count: 100000 }, agg: { count: 9000 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.kind, 'agg');
  assert.strictEqual(r.pts, 1200);
  assert.strictEqual(r.expected, 10080);
});
check('Вся история, 1000px, 20k agg', () => {
  const r = A.pickView(0, 1000, { raw: { count: 0 }, agg: { count: 20000 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.kind, 'agg');
  assert.strictEqual(r.pts, 1000);
  assert.strictEqual(r.expected, 20000);
  assert.strictEqual(r.from, null);
});
check('Вся история, 0 agg', () => {
  const r = A.pickView(0, 1000, { raw: { count: 0 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.kind, 'agg');
  assert.strictEqual(r.pts, 0);
  assert.strictEqual(r.expected, 1);
});
check('3600с, 0 raw', () => {
  const r = A.pickView(3600, 900, { raw: { count: 0 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.kind, 'agg');
  assert.strictEqual(r.pts, 0);
  assert.strictEqual(r.expected, 60);
});
check('Ширина 50 -> 200', () => {
  const r = A.pickView(604800, 50, { raw: { count: 0 }, agg: { count: 9000 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.pts, 200);
});
check('Ширина 5000 -> 2000', () => {
  const r = A.pickView(604800, 5000, { raw: { count: 0 }, agg: { count: 9000 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.pts, 2000);
});
check('Ширина NaN -> 800', () => {
  const r = A.pickView(604800, NaN, { raw: { count: 0 }, agg: { count: 9000 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.pts, 800);
});
check('Ширина undefined -> 800', () => {
  const r = A.pickView(604800, undefined, { raw: { count: 0 }, agg: { count: 9000 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.pts, 800);
});
check('Ширина Infinity -> 800', () => {
  const r = A.pickView(604800, Infinity, { raw: { count: 0 }, agg: { count: 9000 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.pts, 800);
});
check('Ширина 0 -> 800', () => {
  const r = A.pickView(604800, 0, { raw: { count: 0 }, agg: { count: 9000 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.pts, 800);
});
check('Ширина -5 -> 800', () => {
  const r = A.pickView(604800, -5, { raw: { count: 0 }, agg: { count: 9000 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.pts, 800);
});
check('Ширина строка -> 800', () => {
  const r = A.pickView(604800, '700', { raw: { count: 0 }, agg: { count: 9000 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.pts, 800);
});
check('rate 0 -> 0.5', () => {
  const r = A.pickView(3600, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 0);
  assert.strictEqual(r.expected, 1800);
});
check('rate -1 -> 0.5', () => {
  const r = A.pickView(3600, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, -1);
  assert.strictEqual(r.expected, 1800);
});
check('rate NaN -> 0.5', () => {
  const r = A.pickView(3600, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, NaN);
  assert.strictEqual(r.expected, 1800);
});
check('rate undefined -> 0.5', () => {
  const r = A.pickView(3600, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, undefined);
  assert.strictEqual(r.expected, 1800);
});
check('rate строка -> 0.5', () => {
  const r = A.pickView(3600, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 'x');
  assert.strictEqual(r.expected, 1800);
});
check('window_s 0 -> 60', () => {
  const r = A.pickView(604800, 1200, { raw: { count: 0 }, agg: { count: 9000 }, window_s: 0, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.expected, 10080);
});
check('window_s -3 -> 60', () => {
  const r = A.pickView(604800, 1200, { raw: { count: 0 }, agg: { count: 9000 }, window_s: -3, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.expected, 10080);
});
check('window_s строка -> 60', () => {
  const r = A.pickView(604800, 1200, { raw: { count: 0 }, agg: { count: 9000 }, window_s: '60', now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.expected, 10080);
});
check('window_s NaN -> 60', () => {
  const r = A.pickView(604800, 1200, { raw: { count: 0 }, agg: { count: 9000 }, window_s: NaN, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.expected, 10080);
});
check('window_s отсутствует -> 60', () => {
  const r = A.pickView(604800, 1200, { raw: { count: 0 }, agg: { count: 9000 }, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.expected, 10080);
});
check('pickView() не падает', () => {
  const r = A.pickView();
  assert.ok(r.kind === 'raw' || r.kind === 'agg');
  assert.strictEqual(typeof r.pts, 'number');
  assert.ok(r.from === null || typeof r.from === 'number');
});
check('pickView(null...) не падает', () => {
  const r = A.pickView(null, null, null, null);
  assert.ok(r.kind === 'raw' || r.kind === 'agg');
  assert.strictEqual(typeof r.pts, 'number');
  assert.ok(r.from === null || typeof r.from === 'number');
});
check('pickView(status строка) не падает', () => {
  const r = A.pickView(3600, 900, 'x', 0.5);
  assert.ok(r.kind === 'raw' || r.kind === 'agg');
  assert.strictEqual(typeof r.pts, 'number');
  assert.ok(r.from === null || typeof r.from === 'number');
});
check('pickView(status массив) не падает', () => {
  const r = A.pickView(3600, 900, [], 0.5);
  assert.ok(r.kind === 'raw' || r.kind === 'agg');
  assert.strictEqual(typeof r.pts, 'number');
  assert.ok(r.from === null || typeof r.from === 'number');
});
check('pickView(status битый) не падает', () => {
  const r = A.pickView(3600, 900, { raw: null, agg: 5 }, 0.5);
  assert.ok(r.kind === 'raw' || r.kind === 'agg');
  assert.strictEqual(typeof r.pts, 'number');
  assert.ok(r.from === null || typeof r.from === 'number');
});
check('now_ts отсутствует -> null', () => {
  const r = A.pickView(3600, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60 }, 0.5);
  assert.strictEqual(r.from, null);
});
check('now_ts NaN -> null', () => {
  const r = A.pickView(3600, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: NaN }, 0.5);
  assert.strictEqual(r.from, null);
});
check('now_ts 0 -> null', () => {
  const r = A.pickView(3600, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: 0 }, 0.5);
  assert.strictEqual(r.from, null);
});
check('now_ts отрицательное -> null', () => {
  const r = A.pickView(3600, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: -100 }, 0.5);
  assert.strictEqual(r.from, null);
});
check('now_ts 1700000000, окно 3600', () => {
  const r = A.pickView(3600, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.from, 1699996400);
});
check('Окно > now_ts -> null', () => {
  const r = A.pickView(2000000000, 900, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.from, null);
});
check('Граница покрытия: 3600 -> raw', () => {
  const r = A.pickView(3600, 900, { raw: { count: 1800 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.kind, 'raw');
});
check('Граница покрытия: 3601 -> agg', () => {
  const r = A.pickView(3601, 900, { raw: { count: 1800 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.kind, 'agg');
});
check('Прореживание: 2000с -> pts 0', () => {
  const r = A.pickView(2000, 500, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.pts, 0);
});
check('Прореживание: 2002с -> pts 500', () => {
  const r = A.pickView(2002, 500, { raw: { count: 100000 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 0.5);
  assert.strictEqual(r.pts, 500);
});
check('e16e: ширина округляется до целого (899.6 -> 900)', () => {
    const r = A.pickView(604800, 899.6, { raw: { count: 0 }, agg: { count: 9000 }, window_s: 60, now_ts: 1700000000 }, 0.5);
    assert.strictEqual(r.pts, 900);
});

check('e16e: ожидаемое число строк округляется вверх (raw: 1000.5 -> 1001)', () => {
    const r = A.pickView(2001, 500, { raw: { count: 10000 }, agg: { count: 0 }, window_s: 60, now_ts: 1700000000 }, 0.5);
    assert.strictEqual(r.kind, 'raw');
    assert.strictEqual(r.expected, 1001);
    assert.strictEqual(r.pts, 500);
});

check('e16e: ожидаемое число строк округляется вверх (agg: 2000.02 -> 2001)', () => {
    const r = A.pickView(120001, 1000, { raw: { count: 0 }, agg: { count: 5000 }, window_s: 60, now_ts: 1700000000 }, 0.5);
    assert.strictEqual(r.kind, 'agg');
    assert.strictEqual(r.expected, 2001);
    assert.strictEqual(r.pts, 1000);
});

console.log('TESTS: ' + passed + '/' + total);
process.exit(passed === total ? 0 : 1);
