const assert = require('assert');
const path = require('path');
const S = require(process.env.HC_MODULE || path.join(__dirname, '..', '..', 'web', 'store-clear.js'));
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }

check('кнопки по порядку и подписи', () => {
    assert.deepStrictEqual(S.ACTIONS.map(a => a.id), ['raw', 'agg', 'env', 'all', 'log']);
    assert.deepStrictEqual(S.ACTIONS.map(a => a.label), ['Очистить сырые', 'Очистить агрегаты', 'Очистить датчики среды', 'Очистить всё', 'Очистить журнал']);
});

check('тело: ring соответствует кнопке, confirm строго CLEAR', () => {
    for (const r of ['raw', 'agg', 'env', 'all']) assert.deepStrictEqual(S.bodyFor(r), { ring: r, confirm: 'CLEAR' });
    assert.deepStrictEqual(S.bodyFor('log'), { confirm: 'CLEAR' });
    assert.strictEqual(S.bodyFor('x'), null);
    assert.strictEqual(S.bodyFor(undefined), null);
    assert.strictEqual(S.CONFIRM, 'CLEAR');
});

check('адреса: история и журнал различаются', () => {
    for (const r of ['raw', 'agg', 'env', 'all']) assert.strictEqual(S.urlFor(r), '/api/history/clear');
    assert.strictEqual(S.urlFor('log'), '/api/log/clear');
    assert.strictEqual(S.urlFor('x'), null);
});

check('слово подтверждения: только «стереть»', () => {
    assert.strictEqual(S.wordOk('стереть'), true);
    assert.strictEqual(S.wordOk('СТЕРЕТЬ'), true);
    assert.strictEqual(S.wordOk('  стереть \n'), true);
    for (const v of ['', 'ОЧИСТИТЬ', 'очистить', 'стерет', 'стереть!', 'стереть всё', 'CLEAR', null, undefined, 5]) assert.strictEqual(S.wordOk(v), false, String(v));
});

check('доступность: только объект ответа /api/status', () => {
    assert.strictEqual(S.available({ wifi_connected: true }), true);
    assert.strictEqual(S.available({}), true);
    for (const v of [null, undefined, [], 'x', 0]) assert.strictEqual(S.available(v), false);
});

check('предупреждение: seq, epoch, нельзя вернуть; у журнала без seq', () => {
    const w = S.warnText('raw');
    assert.ok(w.indexOf('сырые записи') > 0 && w.indexOf('Данные вернуть нельзя') > 0 && w.indexOf('seq не сбрасывается') > 0 && w.indexOf('epoch') > 0);
    assert.ok(S.warnText('all').indexOf('всю историю') > 0);
    const l = S.warnText('log');
    assert.ok(l.indexOf('журнал событий') > 0 && l.indexOf('seq') < 0);
    assert.strictEqual(S.warnText('x'), '');
});

check('результат: removed по кольцам, число, неизвестная форма, persisted=false', () => {
    assert.strictEqual(S.resultText('all', { removed: { raw: 5, agg: 2, env: 0 } }), 'Очищено: сырые 5, агрегаты 2, датчики среды 0');
    assert.strictEqual(S.resultText('raw', { removed: 7 }), 'Очищено: сырые 7');
    assert.strictEqual(S.resultText('env', { removed: { env: 3 } }), 'Очищено: датчики среды 3');
    assert.strictEqual(S.resultText('raw', {}), 'Готово');
    assert.strictEqual(S.resultText('raw', null), 'Готово');
    assert.strictEqual(S.resultText('raw', { removed: 'x' }), 'Готово');
    assert.strictEqual(S.resultText('raw', { removed: { raw: Infinity } }), 'Готово');
    assert.strictEqual(S.resultText('raw', { removed: NaN }), 'Готово');
    assert.strictEqual(S.resultText('raw', { removed: Infinity }), 'Готово');
    assert.strictEqual(S.resultText('log', { ok: true }), 'Журнал очищен');
    assert.strictEqual(S.resultText('agg', { removed: 1, persisted: false }), 'Очищено: агрегаты 1 (во флеш не записано, действует до перезагрузки)');
    assert.strictEqual(S.resultText('x', {}), '');
});

check('ошибки платы по коду', () => {
    assert.ok(S.errorText(400).indexOf('400') > 0);
    assert.strictEqual(S.errorText(401), 'нужна авторизация');
    assert.ok(S.errorText(403).indexOf('токен') > 0);
    assert.ok(S.errorText(503).indexOf('503') > 0 && S.errorText(503).indexOf('занята') > 0);
    assert.ok(S.errorText(500).indexOf('500') > 0);
    assert.strictEqual(S.errorText(418), 'плата отклонила запрос (418)');
});

check('needsWord: чужой id слова не требует', () => {
    for (const r of ['x', undefined, '']) assert.strictEqual(S.needsWord(r), false);
});
// образец: /api/history/status платы .131 от 30.10 (raw 137040, agg 89, env 2967)
const HS131 = { raw: { count: 137040 }, agg: { count: 89 }, env: { count: 2967 } };
check('слово «стереть»: сырые (137040 записей)', () => {
    assert.strictEqual(S.needsWord('raw'), true);
    assert.ok(S.confirmText('raw', HS131).indexOf('137040 записей') > 0);
});
check('слово «стереть»: агрегаты (89 записей)', () => {
    assert.strictEqual(S.needsWord('agg'), true);
    assert.ok(S.confirmText('agg', HS131).indexOf('89 записей') > 0);
});
check('слово «стереть»: датчики среды (2967 записей)', () => {
    assert.strictEqual(S.needsWord('env'), true);
    assert.ok(S.confirmText('env', HS131).indexOf('2967 записей') > 0);
});
check('слово «стереть»: всё и журнал', () => {
    assert.strictEqual(S.needsWord('all'), true);
    assert.strictEqual(S.needsWord('log'), true);
});

check('countFor: числа колец, сумма для all, null без данных и для журнала', () => {
    const hs = { raw: { count: 91284 }, agg: { count: 382 }, env: { count: 2656 } };
    assert.strictEqual(S.countFor('raw', hs), 91284);
    assert.strictEqual(S.countFor('agg', hs), 382);
    assert.strictEqual(S.countFor('env', hs), 2656);
    assert.strictEqual(S.countFor('all', hs), 91284 + 382 + 2656);
    assert.strictEqual(S.countFor('log', hs), null);
    assert.strictEqual(S.countFor('raw', null), null);
    assert.strictEqual(S.countFor('all', { raw: { count: 5 }, agg: {} , env: { count: 1 } }), null);
    assert.strictEqual(S.countFor('x', hs), null);
});

check('confirmText: название стираемого, число записей, склонение', () => {
    const hs = { raw: { count: 91284 }, agg: { count: 21 }, env: { count: 2 } };
    assert.ok(S.confirmText('raw', hs).indexOf('Будет удалено:') === 0);
    assert.ok(S.confirmText('raw', hs).indexOf('сырые записи') > 0 && S.confirmText('raw', hs).indexOf('91284 записи') > 0);
    assert.ok(S.confirmText('agg', hs).indexOf('21 запись') > 0);
    assert.ok(S.confirmText('env', hs).indexOf('2 записи') > 0);
    const all = S.confirmText('all', hs);
    assert.ok(all.indexOf('сырые 91284') > 0 && all.indexOf('агрегаты 21') > 0 && all.indexOf('всего 91307') > 0);
    assert.ok(S.confirmText('log', hs).indexOf('журнал событий') > 0);
    assert.ok(S.confirmText('raw', null).indexOf('записей') < 0);
    assert.strictEqual(S.confirmText('x', hs), '');
    assert.ok(S.confirmText('raw', hs).indexOf('seq') > 0 && S.confirmText('log', hs).indexOf('seq') < 0);
    assert.ok(S.confirmText('agg', { agg: { count: 11 } }).indexOf('11 записей') > 0 && S.confirmText('agg', { agg: { count: 12 } }).indexOf('12 записей') > 0);
});

console.log('TESTS: ' + passed + '/' + total);
process.exit(passed === total ? 0 : 1);
