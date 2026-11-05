const assert = require('assert');
const path = require('path');
const B = require(process.env.HG_MODULE || path.join(__dirname, '..', '..', 'web', 'meas-board.js'));
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }

// 1. clock
check('clock', () => {
  assert.strictEqual(B.clock(0), '00:00');
  assert.strictEqual(B.clock(5), '00:05');
  assert.strictEqual(B.clock(65), '01:05');
  assert.strictEqual(B.clock(600), '10:00');
  assert.strictEqual(B.clock(3725), '62:05');
  assert.strictEqual(B.clock(59.9), '00:59');
  assert.strictEqual(B.clock(-1), '--:--');
  assert.strictEqual(B.clock(NaN), '--:--');
  assert.strictEqual(B.clock(null), '--:--');
  assert.strictEqual(B.clock('5'), '--:--');
});

// 2. num3
check('num3', () => {
  assert.strictEqual(B.num3(0.1234), '0.123');
  assert.strictEqual(B.num3(12.3456), '12.3');
  assert.strictEqual(B.num3(1234.5), '1230');
  assert.strictEqual(B.num3(0), '0');
  assert.strictEqual(B.num3(null), '—');
  assert.strictEqual(B.num3(NaN), '—');
  assert.strictEqual(B.num3(0.05), '0.05');
});

// 3. rateParts
check('rateParts', () => {
  assert.deepStrictEqual(B.rateParts(0.1234, 2.34), { value: '0.1234', eps: '± 2.3 %' });
  assert.deepStrictEqual(B.rateParts(null, null), { value: '—', eps: '' });
});

// 4. toLocalInput / fromLocalInput
check('toLocalInput/fromLocalInput', () => {
  const u1 = 1761600000;
  assert.strictEqual(B.fromLocalInput(B.toLocalInput(u1)), u1);
  const u2 = 1700000000;
  assert.strictEqual(B.fromLocalInput(B.toLocalInput(u2)), u2);
  const u3 = 1735689599;
  assert.strictEqual(B.fromLocalInput(B.toLocalInput(u3)), u3);
  assert.strictEqual(B.fromLocalInput('2025-10-27T14:05'), Math.floor(new Date(2025, 9, 27, 14, 5, 0).getTime() / 1000));
  assert.strictEqual(B.fromLocalInput('2025-10-27T14:05:09'), Math.floor(new Date(2025, 9, 27, 14, 5, 0).getTime() / 1000) + 9);
  assert.strictEqual(B.fromLocalInput(''), null);
  assert.strictEqual(B.fromLocalInput('abc'), null);
  assert.strictEqual(B.fromLocalInput('2025-13-01T10:00'), null);
  assert.strictEqual(B.fromLocalInput('2025-02-30T10:00'), null);
  assert.strictEqual(B.fromLocalInput(null), null);
  assert.strictEqual(B.fromLocalInput(undefined), null);
  assert.strictEqual(B.fromLocalInput(12345), null);
  assert.strictEqual(B.toLocalInput(null), '');
  assert.match(B.toLocalInput(1761600000), /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}$/);
});

// 5. journalRange
check('journalRange', () => {
  assert.deepStrictEqual(B.journalRange({ t0: 1000.2, t1: 2000.7 }, 5000), { from: 1000, to: 2001 });
  assert.deepStrictEqual(B.journalRange({ t0: 1000, t1: 2000 }, 1500), { from: 1000, to: 1500 });
  assert.strictEqual(B.journalRange({ t0: 1000, t1: 1000 }, 5000), null);
  assert.strictEqual(B.journalRange({ t0: 2000, t1: 1000 }), null);
  assert.strictEqual(B.journalRange(null, 1), null);
  assert.strictEqual(B.journalRange({}, 1), null);
  assert.strictEqual(B.journalRange({ t0: 'a', t1: 2 }, 5), null);
  assert.strictEqual(B.journalRange({ t0: 1000, t1: 2000 }, 900), null);
  assert.deepStrictEqual(B.journalRange({ t0: 1000, t1: 2000 }), { from: 1000, to: 2000 });
});

// 6. boardModel — разовый замер
check('boardModel single', () => {
  const run = { id: 3, name: 'Фон', mode: 'single', state: 'running', n: 1, done: 0, cur_i: 1, t_s: 600, lost_max: 2, stop_mode: 'time', start_utc: 1761600000 };
  const live = { i: 1, phase: 'measuring', elapsed_ms: 150000, remaining_ms: 450000, counts: 420, npk: 75, lost: 1, cps: 2.8, cps_sigma: 0.14, eps_pct: 4.88, usv_h: 0.1234 };
  const m = B.boardModel(run, live, 0);
  assert.strictEqual(m.active, true);
  assert.strictEqual(m.title, 'Замер «Фон» · Разовое');
  assert.strictEqual(m.windowText, '');
  assert.strictEqual(m.rate.value, '0.123');
  assert.strictEqual(m.rate.eps, '± 4.9 %');
  assert.strictEqual(m.pct, 25);
  assert.strictEqual(m.elapsedText, '02:30');
  assert.strictEqual(m.totalText, '10:00');
  assert.strictEqual(m.leftText, 'осталось 07:30');
  assert.strictEqual(m.counts, '420');
  assert.strictEqual(m.cpsText, '2.8');
  assert.strictEqual(m.lostText, '1 / 2');
  assert.strictEqual(m.lostBad, false);
  assert.strictEqual(m.canPause, true);
  assert.strictEqual(m.canResume, false);
  assert.strictEqual(m.canStop, true);
  assert.strictEqual(m.paused, false);
  assert.match(m.startText, /^старт \d{2}:\d{2}:\d{2}$/);
});

// 7. boardModel — серия
check('boardModel series', () => {
  const run = { id: 3, name: 'Фон', mode: 'series', state: 'running', n: 10, done: 0, cur_i: 1, t_s: 600, lost_max: 2, stop_mode: 'time', start_utc: 1761600000 };
  const live = { i: 3, phase: 'measuring', elapsed_ms: 150000, remaining_ms: 450000, counts: 420, npk: 75, lost: 1, cps: 2.8, cps_sigma: 0.14, eps_pct: 4.88, usv_h: 0.1234 };
  const m = B.boardModel(run, live, 0);
  assert.strictEqual(m.windowText, 'окно 3 из 10');
  assert.strictEqual(m.title, 'Замер «Фон» · Серия, окно 3 из 10');
  const m2 = B.boardModel(run, null, 0);
  assert.strictEqual(m2.windowText, 'окно 1 из 10');
  const liveGap = { i: 3, phase: 'gap', elapsed_ms: 150000, remaining_ms: 450000, counts: 420, npk: 75, lost: 1, cps: 2.8, cps_sigma: 0.14, eps_pct: 4.88, usv_h: 0.1234 };
  const m3 = B.boardModel(run, liveGap, 0);
  assert.strictEqual(m3.phaseText, 'пауза между окнами');
});

// 8. boardModel — пауза и крайние случаи
check('boardModel edge cases', () => {
  const baseRun = { id: 3, name: 'Фон', mode: 'single', state: 'running', n: 1, done: 0, cur_i: 1, t_s: 600, lost_max: 2, stop_mode: 'time', start_utc: 1761600000 };
  const baseLive = { i: 1, phase: 'measuring', elapsed_ms: 150000, remaining_ms: 450000, counts: 420, npk: 75, lost: 1, cps: 2.8, cps_sigma: 0.14, eps_pct: 4.88, usv_h: 0.1234 };
  
  const runPaused = { ...baseRun, state: 'paused' };
  const mPaused = B.boardModel(runPaused, baseLive, 0);
  assert.strictEqual(mPaused.paused, true);
  assert.strictEqual(mPaused.canResume, true);
  assert.strictEqual(mPaused.canPause, false);
  assert.strictEqual(mPaused.phaseText, 'на паузе');

  const runDone = { ...baseRun, state: 'done' };
  const mDone = B.boardModel(runDone, baseLive, 0);
  assert.strictEqual(mDone.active, false);
  assert.strictEqual(mDone.canStop, false);

  const runSigma = { ...baseRun, stop_mode: 'sigma' };
  const mSigma = B.boardModel(runSigma, baseLive, 0);
  assert.strictEqual(mSigma.pct, null);
  assert.strictEqual(mSigma.totalText, '--:--');
  assert.strictEqual(mSigma.leftText, '');

  const mNoLive = B.boardModel(baseRun, null, 0);
  assert.strictEqual(mNoLive.rate.value, '—');
  assert.strictEqual(mNoLive.counts, '—');
  assert.strictEqual(mNoLive.lostText, '0 / 2');

  const liveLost = { ...baseLive, lost: 5 };
  const mLost = B.boardModel(baseRun, liveLost, 0);
  assert.strictEqual(mLost.lostBad, true);

  assert.deepStrictEqual(B.boardModel(null, null, 0), { active: false });

  const runNoName = { ...baseRun, name: '' };
  const mNoName = B.boardModel(runNoName, baseLive, 0);
  assert.strictEqual(mNoName.title, 'Замер · Разовое');

  const liveOver = { ...baseLive, elapsed_ms: 700000 };
  const mOver = B.boardModel(baseRun, liveOver, 0);
  assert.strictEqual(mOver.pct, 100);
});

// 9. journalErrText
check('journalErrText', () => {
  const codes = ['bad_range', 'no_data', 'too_long', 'store_busy', 'no_mem', 'busy', 'bad_value'];
  const texts = codes.map(c => B.journalErrText(c));
  texts.forEach(t => assert.ok(typeof t === 'string' && t.length > 0));
  const unique = new Set(texts);
  assert.strictEqual(unique.size, 7);
  assert.strictEqual(B.journalErrText('bad_value'), B.journalErrText('bad_query'));
  assert.strictEqual(B.journalErrText('bad_value'), B.journalErrText('json'));
  assert.strictEqual(B.journalErrText('bad_value'), B.journalErrText('unknown_key'));
  assert.strictEqual(B.journalErrText(''), 'Нет связи с платой');
  assert.strictEqual(B.journalErrText(undefined), 'Нет связи с платой');
  assert.strictEqual(B.journalErrText('xyz'), 'Ошибка: xyz');
});

// 10. journalRows
check('journalRows', () => {
  const res = { counts: 1050, npk: 30, lost: 0, usv_h: 0.08, eps_pct: 3.09, cps: 17.5 };
  const from = 1761600000;
  const to = 1761600060;
  const rows = B.journalRows(res, 0, from, to);
  assert.strictEqual(rows.length, 7);
  const names = rows.map(r => r[0]);
  assert.deepStrictEqual(names, ['Интервал', 'Длительность', 'Средняя мощность дозы, мкЗв/ч', 'Погрешность счёта, %', 'Импульсов', 'Принято пакетов', 'Пропущено пакетов']);
  assert.strictEqual(rows[1][1], '01:00');
  assert.strictEqual(rows[2][1], '0.0800 ± 3.1 %');
  assert.strictEqual(rows[3][1], '3.1');
  assert.strictEqual(rows[4][1], '1050');
  assert.strictEqual(rows[5][1], '30');
  assert.strictEqual(rows[6][1], '0');
  
  const rows2 = B.journalRows({ ...res, lost: 7 }, 2, from, to);
  assert.strictEqual(rows2[6][1], '7 (разрывов: 2)');
  
  const rows3 = B.journalRows({ ...res, eps_pct: null }, 0, from, to);
  assert.strictEqual(rows3[2][1], '0.08');
  assert.strictEqual(rows3[3][1], '—');
});

// 11. prevValues
check('prevValues', () => {
  const rows = [
    { valid: true, usv_h: 0.1 },
    { valid: false, usv_h: 0.9 },
    { valid: true, usv_h: 0.2 },
    { valid: true, usv_h: 0.3 }
  ];
  assert.deepStrictEqual(B.prevValues(rows), ['0.1', '0.2', '0.3']);
  assert.deepStrictEqual(B.prevValues(rows, 2), ['0.2', '0.3']);
  assert.deepStrictEqual(B.prevValues(null), []);
  assert.deepStrictEqual(B.prevValues('x'), []);
  
  const manyRows = [];
  for (let i = 0; i < 20; i++) {
    manyRows.push({ valid: true, usv_h: i });
  }
  const res = B.prevValues(manyRows);
  assert.strictEqual(res.length, 8);
  assert.deepStrictEqual(res, ['12', '13', '14', '15', '16', '17', '18', '19']);
});

// 12. sparkPath
check('sparkPath', () => {
  assert.strictEqual(B.sparkPath([1, 3], 30, 20, 2), 'M2.0 18.0 L28.0 2.0');
  assert.strictEqual(B.sparkPath([5, 5, 5], 30, 20, 2), 'M2.0 10.0 L15.0 10.0 L28.0 10.0');
  assert.strictEqual(B.sparkPath([7], 30, 20, 2), 'M15.0 10.0');
  assert.strictEqual(B.sparkPath([], 30, 20), '');
  assert.strictEqual(B.sparkPath([1, null, 3, NaN], 30, 20, 2), 'M2.0 18.0 L28.0 2.0');
  assert.strictEqual(B.sparkPath([1, 2], 'x', 20), '');
});

// 13. runLabel
check('runLabel', () => {
  assert.strictEqual(B.runLabel({ id: 5, name: 'Фон', mode: 'journal' }), 'замер 5 Фон · из журнала');
  assert.strictEqual(B.runLabel({ id: 6, name: '', mode: 'single' }), 'замер 6');
  assert.strictEqual(B.runLabel(null), '');
});

console.log('TESTS ' + total + ' PASSED ' + passed);
process.exit(passed === total ? 0 : 1);
