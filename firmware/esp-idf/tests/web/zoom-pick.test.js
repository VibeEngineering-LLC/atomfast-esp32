var assert = require('assert');
var path = require('path');
var AFZP = require(process.env.HC_MODULE || path.join(__dirname, '..', '..', 'web', 'zoom-pick.js'));

var passed = 0, total = 0;
function check(name, fn) {
  total++;
  try { fn(); passed++; console.log('ok   - ' + name); }
  catch (e) { console.log('FAIL - ' + name + ': ' + e.message); }
}

check('25min -> raw, step~1.36, pts 0', function () {
  var from = 1000000, to = from + 1500;
  var st = { window_s: 60, raw: { count: 90000 }, agg: { count: 380 }, now_ts: to };
  var r = AFZP.pick({ from: from, to: to, widthPx: 1100, status: st, rawRate: 0.5 });
  assert.strictEqual(r.kind, 'raw');
  assert.ok(Math.abs(r.step - 1500 / 1100) < 0.01, 'step=' + r.step);
  assert.strictEqual(r.pts, 0);
});

check('4h -> raw, pts 1100', function () {
  var from = 1000000, to = from + 14400;
  var st = { window_s: 60, raw: { count: 90000 }, agg: { count: 380 }, now_ts: to };
  var r = AFZP.pick({ from: from, to: to, widthPx: 1100, status: st, rawRate: 0.5 });
  assert.strictEqual(r.kind, 'raw');
  assert.strictEqual(r.pts, 1100);
});

check('3 сут при нехватке агрегатов (после очистки) -> всё равно agg, не raw (T-53)', function () {
  var from = 1000000, to = from + 259200;
  var st = { window_s: 60, raw: { count: 90000 }, agg: { count: 380 }, now_ts: to };
  var r = AFZP.pick({ from: from, to: to, widthPx: 1100, status: st, rawRate: 0.5 });
  assert.strictEqual(r.kind, 'agg');
  assert.strictEqual(r.pts, 1100);
  assert.strictEqual(AFZP.pick({ from: from, to: to, widthPx: 1100, status: { window_s: 60, raw: { count: 90000 }, agg: { count: 0 } }, rawRate: 0.5 }).kind, 'agg');
});

check('3d sufficient.agg -> agg, pts 1100', function () {
  var from = 1000000, to = from + 259200;
  var st = { window_s: 60, raw: { count: 90000 }, agg: { count: 20160 }, now_ts: to };
  var r = AFZP.pick({ from: from, to: to, widthPx: 1100, status: st, rawRate: 0.5 });
  assert.strictEqual(r.kind, 'agg');
  assert.strictEqual(r.pts, 1100);
});

check('boundary 66000s -> agg (step=60)', function () {
  var from = 1000000, to = from + 66000;
  var st = { window_s: 60, raw: { count: 90000 }, agg: { count: 20160 }, now_ts: to };
  var r = AFZP.pick({ from: from, to: to, widthPx: 1100, status: st, rawRate: 0.5 });
  assert.strictEqual(r.kind, 'agg');
  assert.strictEqual(r.step, 60);
  assert.strictEqual(r.pts, 0);
});

check('шаг на границе 60 с: сырые только при шаге < 60, окно в прошлом при нехватке агрегатов — agg', function () {
  var from = 1000000, to = from + 70000;
  var st = { window_s: 60, raw: { count: 90000 }, agg: { count: 1500 }, now_ts: to + 200000 };
  assert.strictEqual(AFZP.pick({ from: from, to: to, widthPx: 1100, status: st, rawRate: 0.5 }).kind, 'agg');
  assert.strictEqual(AFZP.pick({ from: from, to: from + 65999, widthPx: 1100, status: st, rawRate: 0.5 }).kind, 'raw');
  assert.strictEqual(AFZP.pick({ from: from, to: from + 66000, widthPx: 1100, status: { window_s: 60, raw: { count: 5 }, agg: { count: 0 } }, rawRate: 0.5 }).kind, 'agg');
});

check('65999s -> raw', function () {
  var from = 1000000, to = from + 65999;
  var st = { window_s: 60, raw: { count: 90000 }, agg: { count: 20160 }, now_ts: to };
  var r = AFZP.pick({ from: from, to: to, widthPx: 1100, status: st, rawRate: 0.5 });
  assert.strictEqual(r.kind, 'raw');
});

check('width not-number -> 800', function () {
  var from = 1000000, to = from + 800;
  var st = { window_s: 60, raw: { count: 400 }, agg: { count: 14 }, now_ts: to };
  var r = AFZP.pick({ from: from, to: to, widthPx: 'abc', status: st, rawRate: 0.5 });
  assert.strictEqual(r.step, 1);
});

check('width 5000 -> 2000', function () {
  var from = 1000000, to = from + 2000;
  var st = { window_s: 60, raw: { count: 1000 }, agg: { count: 34 }, now_ts: to };
  var r = AFZP.pick({ from: from, to: to, widthPx: 5000, status: st, rawRate: 0.5 });
  assert.strictEqual(r.step, 1);
});

check('width 50 -> 200', function () {
  var from = 1000000, to = from + 200;
  var st = { window_s: 60, raw: { count: 100 }, agg: { count: 4 }, now_ts: to };
  var r = AFZP.pick({ from: from, to: to, widthPx: 50, status: st, rawRate: 0.5 });
  assert.strictEqual(r.step, 1);
});

check('span<=0 -> agg, pts 0', function () {
  var r = AFZP.pick({ from: 5000, to: 5000, widthPx: 1100, status: { window_s: 60, raw: { count: 100 }, agg: { count: 10 } }, rawRate: 0.5 });
  assert.strictEqual(r.kind, 'agg');
  assert.strictEqual(r.pts, 0);
});

console.log(passed + '/' + total);
if (passed < total) process.exit(1);
