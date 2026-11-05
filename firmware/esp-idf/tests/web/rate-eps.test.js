const assert = require('assert');
const path = require('path');
const U = require(process.env.HU_MODULE || path.join(__dirname, '..', '..', 'web', 'uncertainty.js'));

var passed = 0, total = 0;
function check(name, fn) {
  total++;
  try { fn(); passed++; console.log('ok - ' + name); }
  catch (e) { console.log('FAIL - ' + name + ': ' + e.message); }
}

check('(0.0759, 4.23)', function () {
  var r = U.rateEps(0.0759, 4.23);
  assert.strictEqual(r.value, '0.076');
  assert.strictEqual(r.eps, '4.2');
});

check('(0.0794586, 3.02199)', function () {
  var r = U.rateEps(0.0794586, 3.02199);
  assert.strictEqual(r.value, '0.0795');
  assert.strictEqual(r.eps, '3.0');
});

check('(0.0779581, 0.9648)', function () {
  var r = U.rateEps(0.0779581, 0.9648);
  assert.strictEqual(r.value, '0.0780');
  assert.strictEqual(r.eps, '1.0');
});

check('(0.0777227, 0.395758)', function () {
  var r = U.rateEps(0.0777227, 0.395758);
  assert.strictEqual(r.value, '0.0777');
  assert.strictEqual(r.eps, '0.4');
});

check('(0.0788617, 0.196018)', function () {
  var r = U.rateEps(0.0788617, 0.196018);
  assert.strictEqual(r.value, '0.07886');
  assert.strictEqual(r.eps, '0.2');
});

check('(0.0790608, 0.11308)', function () {
  var r = U.rateEps(0.0790608, 0.11308);
  assert.strictEqual(r.value, '0.07906');
  assert.strictEqual(r.eps, '0.1');
});

check('(7.99, 3.7) rounding changes decade', function () {
  var r = U.rateEps(7.99, 3.7);
  assert.strictEqual(r.value, '8.0');
  assert.strictEqual(r.eps, '3.7');
});

check('(0.1, 9.96)', function () {
  var r = U.rateEps(0.1, 9.96);
  assert.strictEqual(r.value, '0.100');
  assert.strictEqual(r.eps, '10.0');
});

check('(50, 150)', function () {
  var r = U.rateEps(50, 150);
  assert.strictEqual(r.value, '50');
  assert.strictEqual(r.eps, '150.0');
});

check('(0, 5) zero value', function () {
  var r = U.rateEps(0, 5);
  assert.strictEqual(r.value, '0');
  assert.strictEqual(r.eps, '5.0');
});

check('(5, 0) zero eps', function () {
  var r = U.rateEps(5, 0);
  assert.strictEqual(r.value, '5');
  assert.strictEqual(r.eps, '0.0');
});

check('(NaN, 3) no value', function () {
  var r = U.rateEps(NaN, 3);
  assert.strictEqual(r.value, '\u2014');
  assert.strictEqual(r.eps, '\u2014');
});

check('(0.08, NaN) no eps', function () {
  var r = U.rateEps(0.08, NaN);
  assert.strictEqual(r.value, '0.08');
  assert.strictEqual(r.eps, '');
});

check('(0.08, -1) negative eps', function () {
  var r = U.rateEps(0.08, -1);
  assert.strictEqual(r.value, '0.08');
  assert.strictEqual(r.eps, '');
});

check('(123456, 0.5) large value', function () {
  var r = U.rateEps(123456, 0.5);
  assert.strictEqual(r.value, '123500');
  assert.strictEqual(r.eps, '0.5');
});

check('(1.2e-9, 10) exponential', function () {
  var r = U.rateEps(1.2e-9, 10);
  assert.strictEqual(r.value, '1.20e-9');
  assert.strictEqual(r.eps, '10.0');
});

check('unit: (1234.5, 2, мкЗв/ч) → мЗв/ч', function () {
  var r = U.rateEps(1234.5, 2, '\u043C\u043A\u0417\u0432/\u0447');
  assert.strictEqual(r.value, '1.235');
  assert.strictEqual(r.eps, '2.0');
  assert.strictEqual(r.unit, '\u043C\u0417\u0432/\u0447');
});

check('unit: (0.0004, 10, мкЗв/ч) → нЗв/ч', function () {
  var r = U.rateEps(0.0004, 10, '\u043C\u043A\u0417\u0432/\u0447');
  assert.strictEqual(r.value, '0.40');
  assert.strictEqual(r.eps, '10.0');
  assert.strictEqual(r.unit, '\u043D\u0417\u0432/\u0447');
});

check('unit: (123456, 0.5, мкЗв/ч) → мЗв/ч', function () {
  var r = U.rateEps(123456, 0.5, '\u043C\u043A\u0417\u0432/\u0447');
  assert.strictEqual(r.value, '123.5');
  assert.strictEqual(r.eps, '0.5');
  assert.strictEqual(r.unit, '\u043C\u0417\u0432/\u0447');
});

check('text: (0.0759, 4.23, мкЗв/ч)', function () {
  var r = U.rateEps(0.0759, 4.23, '\u043C\u043A\u0417\u0432/\u0447');
  assert.strictEqual(r.text, '0.076 \u00B1 4.2 % \u043C\u043A\u0417\u0432/\u0447');
});

check('text: (1.2e-9, 10) exponential text', function () {
  var r = U.rateEps(1.2e-9, 10);
  assert.strictEqual(r.text, '1.20e-9 \u00B1 10.0 %');
});

check('граница смены единицы: 1000 -> мЗв/ч, 999.9 остаётся, 0.001 остаётся, 0.00099 -> нЗв/ч', function () {
  var u = 'мкЗв/ч';
  var a = U.rateEps(1000, 2, u);
  assert.strictEqual(a.unit, 'мЗв/ч');
  assert.strictEqual(a.value, '1.000');
  assert.strictEqual(U.rateEps(999.9, 2, u).unit, u);
  var b = U.rateEps(0.001, 5, u);
  assert.strictEqual(b.unit, u);
  assert.strictEqual(b.value, '0.00100');
  assert.strictEqual(U.rateEps(0.00099, 5, u).unit, 'нЗв/ч');
});

check('большая экспонента: (2.5e12, 4) -> 2.50e12; порог 1e-4 для мантиссы', function () {
  assert.strictEqual(U.rateEps(2.5e12, 4).value, '2.50e12');
  assert.strictEqual(U.rateEps(5e-5, 10).value, '5.0e-5');
  assert.strictEqual(U.rateEps(0.00012, 10).value, '0.000120');
});

check('отрицательное значение: (-0.0759, 4.23) -> -0.076', function () {
  assert.strictEqual(U.rateEps(-0.0759, 4.23).value, '-0.076');
});

console.log('TESTS: ' + passed + '/' + total);
if (passed !== total) process.exit(1);
