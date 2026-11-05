const assert = require('assert');
const path = require('path');
const S = require(process.env.HS_MODULE || path.join(__dirname, '..', '..', 'web', 'history-series.js'));
let passed = 0;
let total = 0;
function check(name, fn) {
    total++;
    try { fn(); passed++; console.log('ok  ' + name); }
    catch (e) { console.log('FAIL ' + name + ': ' + e.message); }
}

// 1. Боевые строки
check('parseEnvCsv: боевые строки', () => {
    const text = "ts,kind,temp_c,press_mmhg,press_pa,flags\n1761300000,env,26.50,751.70,100218,3\n1761300030,env,26.51,751.69,100214,3\n";
    const res = S.parseEnvCsv(text);
    assert.strictEqual(res.rows.length, 2);
    assert.strictEqual(res.bad, 0);
    assert.strictEqual(res.rows[0].ts, 1761300000);
    assert.strictEqual(res.rows[0].air_t, 26.5);
    assert.strictEqual(res.rows[0].air_p, 751.7);
    assert.strictEqual(res.rows[0].flags, 3);
    assert.strictEqual(res.rows[1].air_t, 26.51);
});

// 2. Пустые каналы
check('parseEnvCsv: пустые каналы', () => {
    const text = "ts,kind,temp_c,press_mmhg,press_pa,flags\n1761300060,env,-5.50,,,1\n1761300090,env,,750.06,100000,2\n";
    const res = S.parseEnvCsv(text);
    assert.strictEqual(res.rows.length, 2);
    assert.strictEqual(res.rows[0].air_t, -5.5);
    assert.ok(isNaN(res.rows[0].air_p));
    assert.ok(isNaN(res.rows[1].air_t));
    assert.strictEqual(res.rows[1].air_p, 750.06);
    assert.strictEqual(res.bad, 0);
});

// 3. Брак
check('parseEnvCsv: брак', () => {
    const text = "ts,kind,temp_c,press_mmhg,press_pa,flags\n1,env,3,4,5,6,7\nabc,env,26.5,751.7,100218,3\n1761300120,env,,,,0\n1761300150,env,26.5,751.7,100218,3\n";
    const res = S.parseEnvCsv(text);
    assert.strictEqual(res.rows.length, 1);
    assert.strictEqual(res.bad, 3);
});

// 3a. Колонка kind: у env это текст 'env'; число (прежнее 2, период 60) не принимается (e11, решение оператора)
check('parseEnvCsv: kind=env, число отвергается', () => {
    const NL = String.fromCharCode(10);
    const text = ['ts,kind,temp_c,press_mmhg,press_pa,flags', '1761300000,env,26.50,751.70,100218,3', '1761300030,2,26.50,751.70,100218,3', '1761300060,60,26.50,751.70,100218,3', '1761300090,,26.50,751.70,100218,3', ''].join(NL);
    const res = S.parseEnvCsv(text);
    assert.strictEqual(res.rows.length, 1);
    assert.strictEqual(res.rows[0].ts, 1761300000);
    assert.strictEqual(res.bad, 3);
});

// 4. Заголовок без ts
check('parseEnvCsv: заголовок без ts', () => {
    const res1 = S.parseEnvCsv("a,b,c\n1,2,3\n4,5,6\n");
    assert.strictEqual(res1.rows.length, 0);
    assert.strictEqual(res1.bad, 2);
    
    const res2 = S.parseEnvCsv("");
    assert.strictEqual(res2.rows.length, 0);
    assert.strictEqual(res2.bad, 0);
    
    const res3 = S.parseEnvCsv("ts,kind,temp_c,press_mmhg,press_pa,flags");
    assert.strictEqual(res3.rows.length, 0);
    assert.strictEqual(res3.bad, 0);
});

// 5. Разделители строк
check('parseEnvCsv: разделители строк', () => {
    const header = "ts,kind,temp_c,press_mmhg,press_pa,flags";
    const line1 = "1761300000,env,26.50,751.70,100218,3";
    const line2 = "1761300030,env,26.51,751.69,100214,3";
    
    const res1 = S.parseEnvCsv(header + "\r\n" + line1 + "\r\n" + line2 + "\r\n");
    assert.strictEqual(res1.rows.length, 2);
    
    const res2 = S.parseEnvCsv(header + "\r" + line1 + "\r" + line2 + "\r");
    assert.strictEqual(res2.rows.length, 2);
    
    const res3 = S.parseEnvCsv(header + "\n" + line1 + "\n" + line2 + "\n\n\n");
    assert.strictEqual(res3.rows.length, 2);
});

// 6. Привязка по последнему
check('mergeEnv: привязка по последнему', () => {
    const rows = [];
    for (let i = 0; i < 30; i++) {
        rows.push({ ts: 1761300000 + i * 2, air_t: NaN, air_p: NaN });
    }
    const env = [
        { ts: 1761300000 - 1, air_t: 26.5, air_p: 751.7 },
        { ts: 1761300029, air_t: 26.6, air_p: 751.6 }
    ];
    const ret = S.mergeEnv(rows, env, 120);
    assert.strictEqual(ret, 30);
    
    // Строки с ts < 1761300029 имеют air_t == 26.5
    // ts: 1761300000, 1761300002, ..., 1761300028 (15 строк, i=0..14)
    for (let i = 0; i < 15; i++) {
        assert.strictEqual(rows[i].air_t, 26.5);
    }
    
    // Строки с ts >= 1761300029 имеют air_t == 26.6 и air_p == 751.6
    // ts: 1761300030 (i=15), ..., 1761300058 (i=29)
    for (let i = 15; i < 30; i++) {
        assert.strictEqual(rows[i].air_t, 26.6);
        assert.strictEqual(rows[i].air_p, 751.6);
    }
    
    // Граница: строка ts=1761300030 (i=15) уже 26.6
    assert.strictEqual(rows[15].air_t, 26.6);
    // ts=1761300028 (i=14) ещё 26.5
    assert.strictEqual(rows[14].air_t, 26.5);
});

// 7. Окно maxGap
check('mergeEnv: окно maxGap', () => {
    // maxGap 150
    let rows1 = [
        { ts: 1000, air_t: NaN, air_p: NaN },
        { ts: 1100, air_t: NaN, air_p: NaN },
        { ts: 1200, air_t: NaN, air_p: NaN },
        { ts: 1300, air_t: NaN, air_p: NaN }
    ];
    let env1 = [{ ts: 1000, air_t: 1, air_p: 2 }];
    let ret1 = S.mergeEnv(rows1, env1, 150);
    assert.strictEqual(ret1, 2);
    assert.strictEqual(rows1[0].air_t, 1);
    assert.strictEqual(rows1[1].air_t, 1);
    assert.ok(isNaN(rows1[2].air_t));
    assert.ok(isNaN(rows1[2].air_p));
    assert.ok(isNaN(rows1[3].air_t));
    assert.ok(isNaN(rows1[3].air_p));
    
    // Граница включительно: maxGap 100
    let rows2 = [
        { ts: 1000, air_t: NaN, air_p: NaN },
        { ts: 1100, air_t: NaN, air_p: NaN },
        { ts: 1200, air_t: NaN, air_p: NaN },
        { ts: 1300, air_t: NaN, air_p: NaN }
    ];
    let ret2 = S.mergeEnv(rows2, env1, 100);
    assert.strictEqual(ret2, 2);
    
    // maxGap 99
    let rows3 = [
        { ts: 1000, air_t: NaN, air_p: NaN },
        { ts: 1100, air_t: NaN, air_p: NaN },
        { ts: 1200, air_t: NaN, air_p: NaN },
        { ts: 1300, air_t: NaN, air_p: NaN }
    ];
    let ret3 = S.mergeEnv(rows3, env1, 99);
    assert.strictEqual(ret3, 1);
});

// 8. Строки раньше первого env
check('mergeEnv: строки раньше первого env', () => {
    const rows = [
        { ts: 50, air_t: NaN, air_p: NaN },
        { ts: 150, air_t: NaN, air_p: NaN }
    ];
    const env = [{ ts: 100, air_t: 5, air_p: 6 }];
    const ret = S.mergeEnv(rows, env, 120);
    assert.strictEqual(ret, 1);
    assert.ok(isNaN(rows[0].air_t));
    assert.ok(isNaN(rows[0].air_p));
    assert.strictEqual(rows[1].air_t, 5);
});

// 9. Повторный вызов перезаписывает
check('mergeEnv: повторный вызов перезаписывает', () => {
    const rows = [
        { ts: 100, air_t: NaN, air_p: NaN },
        { ts: 200, air_t: NaN, air_p: NaN }
    ];
    const env1 = [{ ts: 90, air_t: 1, air_p: 1 }];
    
    // rows[0]: 100-90=10 <= 120 -> 1
    // rows[1]: 200-90=110 <= 120 -> 1
    // возврат 2
    const ret1 = S.mergeEnv(rows, env1, 120);
    assert.strictEqual(ret1, 2);
    assert.strictEqual(rows[0].air_t, 1);
    assert.strictEqual(rows[1].air_t, 1);
    
    // Пустой env -> всем NaN, возврат 0
    const ret2 = S.mergeEnv(rows, [], 120);
    assert.strictEqual(ret2, 0);
    assert.ok(isNaN(rows[0].air_t));
    assert.ok(isNaN(rows[1].air_t));
    
    // null env -> всем NaN, возврат 0
    const ret3 = S.mergeEnv(rows, null, 120);
    assert.strictEqual(ret3, 0);
    assert.ok(isNaN(rows[0].air_t));
    assert.ok(isNaN(rows[1].air_t));
});

// 10. Канал NaN не считается
check('mergeEnv: канал NaN не считается', () => {
    // Оба NaN
    let rows1 = [{ ts: 110, air_t: NaN, air_p: NaN }];
    let env1 = [{ ts: 100, air_t: NaN, air_p: NaN }];
    let ret1 = S.mergeEnv(rows1, env1, 120);
    assert.strictEqual(ret1, 0);
    assert.ok(isNaN(rows1[0].air_t));
    assert.ok(isNaN(rows1[0].air_p));
    
    // Один NaN, один число
    let rows2 = [{ ts: 110, air_t: NaN, air_p: NaN }];
    let env2 = [{ ts: 100, air_t: NaN, air_p: 750 }];
    let ret2 = S.mergeEnv(rows2, env2, 120);
    assert.strictEqual(ret2, 1);
    assert.strictEqual(rows2[0].air_p, 750);
    assert.ok(isNaN(rows2[0].air_t));
});

// 11. maxGap по умолчанию
check('mergeEnv: maxGap по умолчанию', () => {
    // maxGap undefined, NaN, -5, 'x' -> 120
    // rows ts 220, env ts 100: разница 120 <= 120 -> возврат 1
    let rows1 = [{ ts: 220, air_t: NaN, air_p: NaN }];
    let env1 = [{ ts: 100, air_t: 1, air_p: 1 }];
    
    let ret1 = S.mergeEnv(rows1, env1, undefined);
    assert.strictEqual(ret1, 1);
    
    let rows2 = [{ ts: 220, air_t: NaN, air_p: NaN }];
    let ret2 = S.mergeEnv(rows2, env1, NaN);
    assert.strictEqual(ret2, 1);
    
    let rows3 = [{ ts: 220, air_t: NaN, air_p: NaN }];
    let ret3 = S.mergeEnv(rows3, env1, -5);
    assert.strictEqual(ret3, 1);
    
    let rows4 = [{ ts: 220, air_t: NaN, air_p: NaN }];
    let ret4 = S.mergeEnv(rows4, env1, 'x');
    assert.strictEqual(ret4, 1);
    
    // rows ts 221, env ts 100: разница 121 > 120 -> возврат 0
    let rows5 = [{ ts: 221, air_t: NaN, air_p: NaN }];
    let ret5 = S.mergeEnv(rows5, env1, undefined);
    assert.strictEqual(ret5, 0);
});

// 12. Не массив rows
check('mergeEnv: не массив rows', () => {
    assert.strictEqual(S.mergeEnv(null, [], 120), 0);
    assert.strictEqual(S.mergeEnv(undefined, [], 120), 0);
    assert.strictEqual(S.mergeEnv({}, [], 120), 0);
});

// 13. Линейный проход на больших данных
check('mergeEnv: линейный проход на больших данных', () => {
    const ts0 = 1761300000;
    const env = [];
    for (let i = 0; i < 5000; i++) {
        env.push({ ts: ts0 + i * 30, air_t: 26.5 + i * 0.001, air_p: 751.7 - i * 0.001 });
    }
    
    const rows = [];
    for (let i = 0; i < 40000; i++) {
        rows.push({ ts: ts0 + i * 2, air_t: NaN, air_p: NaN });
    }
    
    const ret = S.mergeEnv(rows, env, 120);
    assert.strictEqual(ret, 40000);
    
    // Проверка значений для нескольких строк
    // env index = Math.floor(i * 2 / 30)
    for (const i of [0, 100, 1000, 39999]) {
        const envIdx = Math.floor(i * 2 / 30);
        if (envIdx < env.length) {
            assert.strictEqual(rows[i].air_t, env[envIdx].air_t);
            assert.strictEqual(rows[i].air_p, env[envIdx].air_p);
        }
    }
});

console.log('TESTS: ' + passed + '/' + total);
if (passed < total) { process.exit(1); }
