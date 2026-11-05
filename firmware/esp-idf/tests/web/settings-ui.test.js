'use strict';
// Нод-тест для модуля settings-ui (web)
const assert = require('assert');
const path = require('path');

const S = require(process.env.HS_MODULE || path.join(__dirname, '..', '..', 'web', 'settings-ui.js'));

let passed = 0;
let total = 0;

// Обёртка: инкремент total, вызов fn, при успехе passed++, при ошибке вывод FAIL
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

// Базовый «сырой» объект настроек
const ST = { store: { raw: 1, agg: 1, env: 1 }, window_s: 60, sigma_k: 1, ble_ext: 0 };
const cur = S.parse(ST);

// 1. parse_ok — корректное разложение
check('parse_ok', () => {
  assert.deepStrictEqual(cur, { raw: true, agg: true, env: true, window_s: 60, sigma_k: 1, ble_ext: false });
});

// 2. parse_null — невалидные входы дают null
check('parse_null', () => {
  assert.strictEqual(S.parse(null), null);
  assert.strictEqual(S.parse({}), null);
  assert.strictEqual(S.parse(Object.assign({}, ST, { window_s: 61 })), null);
  assert.strictEqual(S.parse(Object.assign({}, ST, { sigma_k: 4 })), null);
  assert.strictEqual(S.parse(Object.assign({}, ST, { sigma_k: 0 })), null);
});

// 3. diff_only_changed — только изменённые поля
check('diff_only_changed', () => {
  const w = Object.assign({}, cur, { raw: false, window_s: 300 });
  assert.deepStrictEqual(S.diff(cur, w), { store: { raw: 0 }, window_s: 300 });
});

// 4. diff_empty — идентичные объекты дают пустой diff
check('diff_empty', () => {
  assert.deepStrictEqual(S.diff(cur, cur), {});
});

// 5. diff_multi — несколько одновременных изменений
check('diff_multi', () => {
  const w = Object.assign({}, cur, { sigma_k: 2, ble_ext: true, env: false });
  assert.deepStrictEqual(S.diff(cur, w), { store: { env: 0 }, sigma_k: 2, ble_ext: 1 });
});

// 6. need_confirm — распознавание кода подтверждения
check('need_confirm', () => {
  assert.strictEqual(S.needConfirm({ status: 409, body: { error: 'need_confirm' } }), true);
  assert.strictEqual(S.needConfirm({ status: 409, body: { error: 'x' } }), false);
  assert.strictEqual(S.needConfirm({ status: 400, body: { error: 'need_confirm' } }), false);
});

// 7. confirm_text — строка подтверждения
check('confirm_text', () => {
  assert.strictEqual(
    S.confirmText({ agg_count: 3631 }),
    'Агрегаты (3631 записей) будут стёрты, сырые останутся. Продолжить?'
  );
});

// 8. err_text — перевод ошибок в человекочитаемый текст
check('err_text', () => {
  assert.strictEqual(S.errText({ status: 400, body: { error: 'bad_value' } }), 'Недопустимое значение');
  assert.strictEqual(S.errText({ status: 500, body: { error: 'nvs_failed' } }), 'Не удалось сохранить в память платы');
  assert.strictEqual(S.errText({ status: 418, body: {} }), 'Ошибка 418');
});

// 9. windows — таблица разрешённых окон
check('windows', () => {
  assert.strictEqual(S.WINDOWS.length, 7);
  assert.deepStrictEqual(S.WINDOWS.map(x => x.s), [60, 120, 180, 300, 600, 900, 1200]);
});

// Итог
console.log('TESTS: ' + passed + '/' + total);
process.exit(passed === total ? 0 : 1);
