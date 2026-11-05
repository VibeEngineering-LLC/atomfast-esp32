const assert = require('assert');
const path = require('path');
const S = require(process.env.HSR_MODULE || path.join(__dirname, '..', '..', 'web', 'history-series.js'));
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }
const H = 'ts,kind,sensor,model,temp_c,hum_pct,press_mmhg,press_pa,flags\n';

check('parse: колонки sensor, model, hum_pct', () => {
    const r = S.parseEnvCsv(H + '100,env,2,sht3x,21.5,40.2,,,0\n100,env,1,bmp280,22.1,,745.2,99353,0\n');
    assert.strictEqual(r.rows.length, 2);
    assert.deepStrictEqual(r.rows[0], { ts: 100, sensor: 2, model: 'sht3x', air_t: 21.5, air_p: NaN, air_h: 40.2, flags: 0 });
    assert.strictEqual(r.rows[1].sensor, 1);
    assert.strictEqual(r.rows[1].air_p, 745.2);
    assert.ok(Number.isNaN(r.rows[1].air_h));
});

check('parse: старый CSV без sensor даёт датчик 1', () => {
    const r = S.parseEnvCsv('ts,kind,temp_c,press_mmhg,press_pa,flags\n100,env,20,740,98658,0\n');
    assert.strictEqual(r.rows[0].sensor, 1);
    assert.strictEqual(r.rows[0].model, '');
    assert.ok(Number.isNaN(r.rows[0].air_h));
});

check('parse: номер вне 1..8 или дробный -> 1; строка только с влажностью принимается; пустая отвергается', () => {
    const r = S.parseEnvCsv(H + '1,env,9,x,1,,,,0\n2,env,2.5,x,1,,,,0\n3,env,3,sht3x,,55,,,0\n4,env,4,sht3x,,,,,0\n5,env,0,x,1,,,,0\n6,env,8,x,1,,,,0\n');
    assert.deepStrictEqual(r.rows.map(x => x.sensor), [1, 1, 3, 1, 8]);
    assert.strictEqual(r.bad, 1);
});

const mk = (ts, sensor, t, p, h) => ({ ts, sensor, model: '', air_t: t, air_p: p, air_h: h, flags: 0 });

check('merge: один датчик пишет только air_t/air_p/air_h', () => {
    const rows = [{ ts: 105 }];
    const n = S.mergeEnv(rows, [mk(100, 1, 20, 740, NaN)], 120);
    assert.strictEqual(n, 1);
    assert.strictEqual(rows[0].air_t, 20);
    assert.ok(!('air_t_1' in rows[0]));
    assert.ok(Number.isNaN(rows[0].air_h));
});

check('merge: несколько датчиков -> поля air_x_N и старые от наименьшего номера', () => {
    const env = [mk(100, 2, 25, NaN, 40), mk(100, 1, 20, 740, NaN), mk(160, 2, 26, NaN, 41)];
    const rows = [{ ts: 110 }, { ts: 170 }];
    S.mergeEnv(rows, env, 120);
    assert.strictEqual(rows[0].air_t_1, 20);
    assert.strictEqual(rows[0].air_t_2, 25);
    assert.strictEqual(rows[0].air_h_2, 40);
    assert.strictEqual(rows[0].air_p_1, 740);
    assert.strictEqual(rows[0].air_t, 20);
    assert.strictEqual(rows[1].air_t_2, 26);
    assert.strictEqual(rows[1].air_t_1, 20);
    assert.strictEqual(rows[1].air_h_2, 41);
});

check('merge: разрыв ровно maxGap ещё допустим', () => {
    const rows = [{ ts: 220 }];
    S.mergeEnv(rows, [mk(100, 1, 20, 740, NaN)], 120);
    assert.strictEqual(rows[0].air_t, 20);
    S.mergeEnv(rows, [mk(99, 1, 20, 740, NaN)], 120);
    assert.ok(Number.isNaN(rows[0].air_t));
});

check('merge: окно maxGap действует на каждый датчик отдельно', () => {
    const env = [mk(0, 1, 20, 740, NaN), mk(500, 2, 25, NaN, 40)];
    const rows = [{ ts: 510 }];
    S.mergeEnv(rows, env, 100);
    assert.ok(Number.isNaN(rows[0].air_t_1));
    assert.strictEqual(rows[0].air_t_2, 25);
    assert.ok(Number.isNaN(rows[0].air_t));
});

check('merge: строка раньше первой env-точки датчика получает NaN; пустой env не падает', () => {
    const rows = [{ ts: 50 }];
    S.mergeEnv(rows, [mk(100, 1, 1, 1, 1), mk(100, 2, 2, 2, 2)], 120);
    assert.ok(Number.isNaN(rows[0].air_t_2) && Number.isNaN(rows[0].air_t));
    assert.strictEqual(S.mergeEnv([{ ts: 1 }], [], 120), 0);
    assert.strictEqual(S.mergeEnv([{ ts: 1 }], null, 120), 0);
});

check('merge: счётчик считает строки с любым значением, включая влажность', () => {
    const rows = [{ ts: 100 }, { ts: 1000 }];
    assert.strictEqual(S.mergeEnv(rows, [mk(100, 3, NaN, NaN, 55)], 120), 1);
});
console.log('TESTS: ' + passed + '/' + total);
process.exit(passed === total ? 0 : 1);
