const assert = require('assert');
const path = require('path');
const E = require(process.env.HES_MODULE || path.join(__dirname, '..', '..', 'web', 'env-series.js'));
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }
const rows = (list) => list.map(([sensor, model]) => ({ ts: 1, sensor, model }));
const ids = (d) => d.map(x => x.id).join(',');

check('пустой env -> T и P датчика 1 со старыми id и именами', () => {
    const d = E.build([]);
    assert.strictEqual(ids(d), 'air_t,air_p');
    assert.deepStrictEqual(d.map(x => x.name), ['Температура среды', 'Давление среды']);
    assert.deepStrictEqual(d.map(x => x.color), ['--s6', '--s7']);
});

check('один SHT3x -> air_t и air_h (--s8), без давления', () => {
    const d = E.build(rows([[1, 'sht3x']]));
    assert.strictEqual(ids(d), 'air_t,air_h');
    assert.strictEqual(d[1].color, '--s8');
    assert.strictEqual(d[1].group, 'hum');
    assert.strictEqual(d[1].unit, '%');
    assert.strictEqual(d[1].field, 'air_h');
    assert.strictEqual(d[1].cid, 'air_h');
    assert.strictEqual(d[1].name, 'Влажность среды');
    assert.deepStrictEqual(d[1].dash, [16, 5]);
});

check('три разные модели -> ряды по датчикам, имена с номером и моделью', () => {
    const d = E.build(rows([[1, 'bmp280'], [2, 'sht3x'], [3, 'aht2x']]));
    assert.strictEqual(ids(d), 'air_t_1,air_t_2,air_t_3,air_p_1,air_h_2,air_h_3');
    assert.strictEqual(d[1].name, 'T среды · №2 SHT3x');
    assert.strictEqual(d[1].baseName, 'Температура среды');
    assert.strictEqual(d[1].field, 'air_t_2');
    assert.deepStrictEqual(d.map(x => x.tint), [0, 1, 2, 0, 1, 2]);
    assert.strictEqual(d[2].color, '--s6|2');
    assert.strictEqual(d[1].cid, 'air_t');
    assert.strictEqual(d[4].cid, 'air_h');
    assert.strictEqual(d[4].color, '--s8|1');
});

check('два одинаковых датчика различаются номером', () => {
    const d = E.build(rows([[1, 'bmp280'], [2, 'bmp280']]));
    assert.deepStrictEqual(d.filter(x => x.envKind === 'p').map(x => x.name), ['P среды · №1 BMP280', 'P среды · №2 BMP280']);
});

check('восемь датчиков: не больше 8 рядов температуры, разные цвета и штрихи', () => {
    const list = []; for (let i = 1; i <= 8; i++) list.push([i, 'sht3x']);
    const d = E.build(rows(list));
    const t = d.filter(x => x.envKind === 't');
    assert.strictEqual(t.length, 8);
    assert.strictEqual(new Set(t.map(x => x.color)).size, 8);
    assert.strictEqual(new Set(d.map(x => x.id)).size, d.length);
});

check('signature меняется при смене состава и имён, но не при повторе', () => {
    const a = E.build(rows([[1, 'bmp280']]));
    assert.strictEqual(E.signature(a), E.signature(E.build(rows([[1, 'bmp280'], [1, 'bmp280']]))));
    assert.notStrictEqual(E.signature(a), E.signature(E.build(rows([[1, 'bmp280'], [2, 'sht3x']]))));
    assert.notStrictEqual(E.signature(a), E.signature(E.build(rows([[1, 'bme280']]))));
    const s1 = E.build(rows([[1, 'bmp280'], [2, 'sht3x']]));
    const s2 = E.build(rows([[1, 'bmp280'], [2, 'aht2x']]));
    assert.strictEqual(s1.map(x => x.id).join(), s2.map(x => x.id).join());
    assert.notStrictEqual(E.signature(s1), E.signature(s2));
});

check('build возвращает независимые массивы штрихов', () => {
    const d = E.build([]);
    d[0].dash.push(1);
    assert.deepStrictEqual(E.build([])[0].dash, [8, 3]);
});
console.log('TESTS: ' + passed + '/' + total);
process.exit(passed === total ? 0 : 1);
