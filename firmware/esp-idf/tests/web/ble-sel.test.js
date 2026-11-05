'use strict';
// Нод-тест для модуля ble-sel (web)
const assert = require('assert');
const path = require('path');

const S = require(process.env.HS_MODULE || path.join(__dirname, '..', '..', 'web', 'ble-sel.js'));

let passed = 0;
let total = 0;

function check(name, fn) {
  total++;
  try {
    fn();
    passed++;
    console.log('ok  ' + name);
  } catch (e) {
    console.log('FAIL ' + name + ': ' + e.message);
  }
}

// 1. pollMs
check('pollMs', () => {
  assert.strictEqual(S.pollMs('scan'), 1000);
  assert.strictEqual(S.pollMs('stopping'), 1000);
  assert.strictEqual(S.pollMs('opening'), 1000);
  assert.strictEqual(S.pollMs('confirming'), 1000);
  assert.strictEqual(S.pollMs('forgetting'), 1000);
  assert.strictEqual(S.pollMs('live'), 5000);
  assert.strictEqual(S.pollMs('wait'), 5000);
  assert.strictEqual(S.pollMs('list'), 5000);
  assert.strictEqual(S.pollMs('scan', true), 0);
  assert.strictEqual(S.pollMs('live', true), 0);
  assert.strictEqual(S.pollMs('wait', true), 0);
  assert.strictEqual(S.pollMs('list', true), 0);
});

// 2. parseAddr
check('parseAddr', () => {
  assert.strictEqual(S.parseAddr('AA:BB:CC:DD:EE:FF'), 'AA:BB:CC:DD:EE:FF');
  assert.strictEqual(S.parseAddr('aa-bb-cc-dd-ee-ff'), 'AA:BB:CC:DD:EE:FF');
  assert.strictEqual(S.parseAddr('AABBCCDDEEFF'), 'AA:BB:CC:DD:EE:FF');
  assert.strictEqual(S.parseAddr('AA:BB:CC:DD:EE'), null);
  assert.strictEqual(S.parseAddr('AA:BB:CC:DD:EE:FF:11'), null);
  assert.strictEqual(S.parseAddr('GG:HH:II:JJ:KK:LL'), null);
  assert.strictEqual(S.parseAddr(''), null);
  assert.strictEqual(S.parseAddr(' AA:BB:CC:DD:EE:FF'), null);
  assert.strictEqual(S.parseAddr('AA:BB-CC:DD:EE:FF'), null);
  assert.strictEqual(S.parseAddr(undefined), null);
  assert.strictEqual(S.parseAddr(5), null);
});

// 3. viewModel — head и флаги по состояниям
check('viewModel_states', () => {
  const tgt = { name: 'AtomFast', addr: 'AA:BB:CC:DD:EE:FF', kind: 'atomfast' };

  let vm = S.viewModel({ state: 'live', tgt: tgt });
  assert.ok(vm.head.indexOf('Подключён:') !== -1);
  assert.strictEqual(vm.canPick, false);
  assert.strictEqual(vm.canRescan, true);
  assert.strictEqual(vm.canForget, true);
  assert.strictEqual(vm.canCancel, false);

  vm = S.viewModel({ state: 'wait', tgt: tgt, lost_s: 60 });
  assert.ok(vm.head.indexOf('Связь потеряна') !== -1);
  assert.strictEqual(vm.canPick, true);
  assert.strictEqual(vm.canRescan, true);
  assert.strictEqual(vm.canForget, true);
  assert.strictEqual(vm.canCancel, false);

  vm = S.viewModel({ state: 'scan' });
  assert.ok(vm.head.indexOf('Сканирую эфир') !== -1);
  assert.strictEqual(vm.canPick, false);
  assert.strictEqual(vm.canRescan, false);
  assert.strictEqual(vm.canForget, false);
  assert.strictEqual(vm.canCancel, false);

  vm = S.viewModel({ state: 'stopping' });
  assert.ok(vm.head.indexOf('Сканирую эфир') !== -1);
  assert.strictEqual(vm.canPick, false);
  assert.strictEqual(vm.canRescan, false);
  assert.strictEqual(vm.canForget, false);
  assert.strictEqual(vm.canCancel, false);

  vm = S.viewModel({ state: 'list', cands: [{ tok: 1 }], tgt: tgt });
  assert.ok(vm.head.indexOf('Найдено:') !== -1);
  assert.strictEqual(vm.canPick, true);
  assert.strictEqual(vm.canRescan, true);
  assert.strictEqual(vm.canForget, false);
  assert.strictEqual(vm.canCancel, true);

  vm = S.viewModel({ state: 'list', cands: [{ tok: 1 }] });
  assert.ok(vm.head.indexOf('Найдено:') !== -1);
  assert.strictEqual(vm.canPick, true);
  assert.strictEqual(vm.canRescan, true);
  assert.strictEqual(vm.canForget, false);
  assert.strictEqual(vm.canCancel, false);

  vm = S.viewModel({ state: 'opening' });
  assert.ok(vm.head.indexOf('Подключаюсь') !== -1);
  assert.strictEqual(vm.canPick, false);
  assert.strictEqual(vm.canRescan, false);
  assert.strictEqual(vm.canForget, false);
  assert.strictEqual(vm.canCancel, false);

  vm = S.viewModel({ state: 'confirming' });
  assert.ok(vm.head.indexOf('Проверяю прибор') !== -1);
  assert.strictEqual(vm.canPick, false);
  assert.strictEqual(vm.canRescan, false);
  assert.strictEqual(vm.canForget, false);
  assert.strictEqual(vm.canCancel, false);

  vm = S.viewModel({ state: 'forgetting' });
  assert.ok(vm.head.indexOf('Забываю прибор') !== -1);
  assert.strictEqual(vm.canPick, false);
  assert.strictEqual(vm.canRescan, false);
  assert.strictEqual(vm.canForget, false);
  assert.strictEqual(vm.canCancel, false);
});

// 4. viewModel — rows
check('viewModel_rows', () => {
  const cands = [
    { tok: 1, name: 'AtomFast', kind: 'atomfast', rssi: -48, addr: 'AA:BB:CC:DD:EE:FF', age_ms: 1200, is_tgt: true },
    { tok: 2, name: 'AtomSwift', kind: 'atomswift', rssi: -71, addr: '11:22:33:44:55:66', age_ms: 1200, is_tgt: false }
  ];
  const vm = S.viewModel({ state: 'list', cands: cands });
  assert.strictEqual(vm.rows.length, 2);
  assert.strictEqual(vm.rows[0].age, '1.2');
  assert.strictEqual(vm.rows[1].age, '1.2');
  assert.strictEqual(vm.rows[0].isTgt, true);
  assert.strictEqual(vm.rows[1].isTgt, false);
  assert.strictEqual(vm.rows[0].label, 'AtomFast');
  assert.strictEqual(vm.rows[1].label, 'AtomSwift');
});

// 5. viewModel — lost_s минимальное
check('viewModel_lost_s', () => {
  let vm = S.viewModel({ state: 'wait', tgt: { name: 'X', addr: 'AA:BB:CC:DD:EE:FF' }, lost_s: 130 });
  assert.ok(vm.head.indexOf('2 мин') !== -1);

  vm = S.viewModel({ state: 'wait', tgt: { name: 'X', addr: 'AA:BB:CC:DD:EE:FF' }, lost_s: 10 });
  assert.ok(vm.head.indexOf('1 мин') !== -1);
});

// 6. errText
check('errText', () => {
  const t = S.errText(409, { error: 'need_clear', records: 5 });
  assert.ok(t.indexOf('5') !== -1);

  const texts = [
    S.errText(409, { error: 'need_replace' }),
    S.errText(409, { error: 'need_clear', records: 5 }),
    S.errText(409, { error: 'meas_active' }),
    S.errText(409, { error: 'stale' }),
    S.errText(409, { error: 'cooldown' }),
    S.errText(409, { error: 'busy' }),
    S.errText(409, { error: 'live' }),
    S.errText(404, {}),
    S.errText(400, {}),
    S.errText(403, {}),
    S.errText(503, {})
  ];
  for (const s of texts) {
    assert.ok(typeof s === 'string' && s.length > 0, 'text must be non-empty');
  }
  const set = new Set(texts);
  assert.strictEqual(set.size, texts.length, 'all texts must be unique');

  const unk = S.errText(418, {});
  assert.ok(unk.indexOf('418') !== -1);
});

// 7. confirmText
check('confirmText', () => {
  const d = { tgt: { name: 'AtomFast', addr: 'AA:BB:CC:DD:EE:FF' } };
  const forget = S.confirmText('forget', d);
  assert.ok(forget.indexOf('AtomFast') !== -1);
  assert.ok(forget.indexOf('AA:BB:CC:DD:EE:FF') !== -1);

  const change = S.confirmText('change', d);
  const clear = S.confirmText('clear', d);
  assert.ok(change.length > 0);
  assert.ok(clear.length > 0);
  assert.notStrictEqual(change, clear);
});

check('noteText_hint', () => {
  const seen = {};
  ['deferred','incompatible','clear_failed','tgt_write_failed','meas_active','aborted','close_failed','scan_start_failed','no_cand'].forEach((k) => {
    const t = S.noteText(k);
    assert.ok(t.length > 0 && t.indexOf('Ошибка') === -1, k);
    assert.ok(!seen[t], 'dup ' + k);
    seen[t] = 1;
  });
  assert.ok(S.noteText('zzz').indexOf('zzz') !== -1);
  const vm = S.viewModel({ state: 'list', tgt: null, cands: [], err: 'clear_failed' });
  assert.strictEqual(vm.hint, S.noteText('clear_failed'));
  const unnamed = S.viewModel({ state: 'live', tgt: {}, cands: [], err: 'name_unsaved' });
  assert.strictEqual(unnamed.hint, 'Прибор сохранён, но его имя не удалось записать');
  assert.strictEqual(S.viewModel({ state: 'list', tgt: null, cands: [], err: null }).hint, '');
});

// Итог
console.log('TESTS: ' + passed + '/' + total);
process.exit(passed === total ? 0 : 1);
