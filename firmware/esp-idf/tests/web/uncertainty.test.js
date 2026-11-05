const assert = require('assert');
const path = require('path');
const U = require(process.env.HU_MODULE || path.join(__dirname, '..', '..', 'web', 'uncertainty.js'));
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }
const near = (a, b, e) => Math.abs(a - b) <= e;
const F = (v, e) => U.format(v, e).text;

check('эталон: 23 импульса за 2 с -> 11.5 ± 2.4', () => {
    const c = U.countStat(23, 2);
    assert.ok(near(c.value, 11.5, 1e-12) && near(c.sigma, Math.sqrt(23) / 2, 1e-12));
    assert.strictEqual(F(c.value, c.sigma), '11.5 ± 2.4');
});

check('эталон: N = 79 даёт относительную погрешность 11 %', () => {
    const a = U.avgCpsStat(79 / 60, 60);
    assert.ok(near(a.n, 79, 1e-9));
    assert.ok(near(a.sigma / a.value, 1 / Math.sqrt(79), 1e-12));
    assert.strictEqual(Math.round(a.sigma / a.value * 100), 11);
});

check('эталон: округление 17.66 ± 0.62 -> 17.7 ± 0.6 и 9.96 ± 0.96 -> 10.0 ± 1.0', () => {
    assert.strictEqual(F(17.66, 0.62), '17.7 ± 0.6');
    assert.strictEqual(F(9.96, 0.96), '10.0 ± 1.0');
});

check('эталон: 0.0644 ± 0.0003 (N = 46000)', () => {
    const r = U.rateStat(0.0644, 46000);
    assert.ok(near(r.sigma, 0.0644 / Math.sqrt(46000), 1e-15));
    assert.strictEqual(F(r.value, r.sigma), '0.0644 ± 0.0003');
});

check('правило разряда: первая цифра 1 или 2 -> две значащие, иначе одна', () => {
    assert.strictEqual(F(5, 0.12), '5.00 ± 0.12');
    assert.strictEqual(F(5, 0.29), '5.00 ± 0.29');
    assert.strictEqual(F(5, 0.31), '5.0 ± 0.3');
    assert.strictEqual(F(5, 0.99), '5.0 ± 1.0');
    assert.strictEqual(F(100, 19), '100 ± 19');
    assert.strictEqual(F(100, 31), '100 ± 30');
});

check('округление значения до разряда погрешности, хвостовые нули сохраняются', () => {
    assert.strictEqual(F(1.5, 0.05), '1.50 ± 0.05');
    assert.strictEqual(F(2, 0.3), '2.0 ± 0.3');
    assert.strictEqual(F(12345, 260), '12350 ± 260');
    assert.strictEqual(F(123456, 4000), '123000 ± 4000');
});

check('краевые случаи: err >= value, ноль, NaN, отрицательная, малая и огромная', () => {
    assert.strictEqual(U.format(5, 7).weak, true);
    assert.strictEqual(U.format(5, 5).weak, true);
    assert.strictEqual(U.format(-5, 5).weak, true);
    assert.strictEqual(U.format(5, 1).weak, false);
    assert.strictEqual(F(0, 0.5), '0.0 ± 0.5');
    assert.strictEqual(F(0.5, 0), '0.50 ± 0');
    assert.strictEqual(F(NaN, 1), 'н/д');
    assert.strictEqual(F(3, NaN), 'н/д');
    assert.strictEqual(F(3, -1), 'н/д');
    assert.strictEqual(F(3, Infinity), 'н/д');
    assert.strictEqual(F(3, undefined), '3.00 ± н/д');
    assert.strictEqual(F(1.2345678, 0.0000003), '1.2345678 ± 0.0000003');
    assert.strictEqual(F(-0.0001, 0.3), '0.0 ± 0.3');
    assert.strictEqual(F(2.5e17, 3e15), '2.50e+17 ± 3.0e+15');
    assert.strictEqual(F(5e15, 3e14), '5.00e+15 ± 3.0e+14');
    assert.strictEqual(F(-0.04, 0.3), '0.0 ± 0.3');
    assert.strictEqual(F(-0.4, 0.3), '-0.4 ± 0.3');
});

check('N = 0: верхняя граница 1 импульс, а не нулевая погрешность', () => {
    const c = U.countStat(0, 2);
    assert.strictEqual(c.zero, true);
    assert.strictEqual(F(c.value, c.sigma), '0.0 ± 0.5');
    assert.strictEqual(U.pulsesStat(0).sigma, 1);
    assert.strictEqual(U.avgCpsStat(0, 60).zero, true);
});

check('счётные величины: неверные входы дают NaN', () => {
    assert.ok(Number.isNaN(U.countStat(-1, 2).sigma) && Number.isNaN(U.countStat(5, 0).sigma) && Number.isNaN(U.countStat('5', 2).sigma));
    assert.ok(Number.isNaN(U.avgCpsStat(1, 0).sigma) && Number.isNaN(U.avgCpsStat(NaN, 60).sigma));
    assert.ok(Number.isNaN(U.rateStat(0.1, 0).sigma) && Number.isNaN(U.rateStat(-1, 10).sigma));
});

check('накопленная доза: погрешность только при известном N_total', () => {
    assert.ok(Number.isNaN(U.doseStat(1.234, undefined).sigma));
    assert.ok(near(U.doseStat(2, 10000).sigma, 0.02, 1e-12));
    assert.ok(Number.isNaN(U.doseStat(2, 0).sigma) && Number.isNaN(U.doseStat(-1, 100).sigma));
    assert.strictEqual(F(2, U.doseStat(2, 10000).sigma), '2.000 ± 0.020');
    assert.strictEqual(F(1.234, U.doseStat(1.234, undefined).sigma), 'н/д');
});

check('систематика складывается квадратично', () => {
    const t = U.total(100, 3, 4);
    assert.ok(near(t.sigma, 5, 1e-12) && t.stat === 3 && t.sys === 4);
    assert.strictEqual(U.total(100, 3, null).sigma, 3);
    assert.strictEqual(U.total(100, 3, 'abc').sigma, 3);
    assert.strictEqual(U.total(100, 3, 51).sigma, 3);
    assert.ok(near(U.total(-100, 3, 4).sys, 4, 1e-12));
    assert.ok(Number.isNaN(U.total(100, NaN, 4).sigma));
});

check('sysPctOf: границы 0..50, запятая, пустая строка', () => {
    assert.strictEqual(U.sysPctOf(0), 0);
    assert.strictEqual(U.sysPctOf(50), 50);
    assert.strictEqual(U.sysPctOf('2,5'), 2.5);
    assert.strictEqual(U.sysPctOf(''), null);
    assert.strictEqual(U.sysPctOf(-1), null);
    assert.strictEqual(U.sysPctOf(50.01), null);
    assert.strictEqual(U.sysPctOf(NaN), null);
});

check('среднее n значений: sigma/sqrt(n)', () => {
    assert.ok(near(U.mean(3, 9), 1, 1e-12));
    assert.ok(Number.isNaN(U.mean(3, 0)) && Number.isNaN(U.mean(-1, 4)));
});

check('датчики среды: шаг квантования из формул, паспорт н/д', () => {
    assert.ok(near(U.envLsb('sht3x', 't'), 175 / 65535, 1e-15));
    assert.ok(near(U.envLsb('sht3x', 'h'), 100 / 65535, 1e-15));
    assert.ok(near(U.envLsb('aht2x', 't'), 200 / 1048576, 1e-15));
    assert.ok(near(U.envLsb('aht2x', 'h'), 100 / 1048576, 1e-15));
    assert.strictEqual(U.envLsb('bmp280', 't'), null);
    assert.strictEqual(U.envLsb('unknown', 't'), null);
    assert.strictEqual(U.envLsb('sht3x', 'p'), null);
    assert.strictEqual(U.passport('sht3x', 't'), null);
});

const LIVE = { has_rec: true, cps: 11.5, pulses_2s: 23, rate_usvh: 0.0644, rate_urh: 6.44, dose_usv: 1.234, avg_cps: 79 / 60, avg_rate_usvh: 0.07 };

check('liveErrors: эталонные строки таблицы «Показания»', () => {
    const o = U.liveErrors(LIVE, 60, null);
    assert.strictEqual(o.cps.text, '11.5 ± 2.4');
    assert.strictEqual(o.pulses.text, '23 ± 5');
    assert.strictEqual(o.avg_cps.text, '1.32 ± 0.15');
    assert.strictEqual(o.rate.text, '0.064 ± 0.013');
    assert.strictEqual(o.rate_ur, undefined);
    assert.ok(near(o.rate.sigma / o.rate.value, 1 / Math.sqrt(23), 1e-12));
    assert.ok(near(o.avg_rate.sigma / o.avg_rate.value, 1 / Math.sqrt(79), 1e-12));
});

check('liveErrors: накопленная доза без N_total -> погрешность н/д', () => {
    const o = U.liveErrors(LIVE, 60, null);
    assert.ok(Number.isNaN(o.dose.sigma));
    assert.strictEqual(o.dose.value, 1.234);
});

check('liveErrors: нет записи или нет ключей -> нет строк, а не выдуманные числа', () => {
    assert.deepStrictEqual(Object.keys(U.liveErrors({}, 60, null)), []);
    assert.deepStrictEqual(Object.keys(U.liveErrors(null, 60, null)), []);
    const o = U.liveErrors({ has_rec: false, cps: 1, pulses_2s: 2, rate_usvh: 1, rate_urh: 1, avg_cps: 1 }, 60, null);
    assert.deepStrictEqual(Object.keys(o), ['avg_cps']);
    assert.ok(Number.isNaN(U.liveErrors(LIVE, undefined, null).avg_cps.sigma));
});

check('liveErrors: систематика складывается квадратично и разбита на части', () => {
    const a = U.liveErrors(LIVE, 60, null).cps;
    const b = U.liveErrors(LIVE, 60, 10).cps;
    assert.ok(near(b.sys, 1.15, 1e-12) && near(b.stat, a.stat, 1e-12));
    assert.ok(near(b.sigma, Math.sqrt(a.stat * a.stat + 1.15 * 1.15), 1e-12));
    assert.strictEqual(b.text, '11.5 ± 2.7');
    assert.strictEqual(a.sys, 0);
});

check('seriesStats: среднее и последнее по сырым строкам, итоговый N общий', () => {
    const rows = []; for (let i = 0; i < 10; i++) rows.push({ pulses_2s: 20 + i, doserate_usvh: 0.1 });
    const r = U.seriesStats('cps', rows, null);
    assert.ok(near(r.mean.value, 245 / 20, 1e-12) && near(r.mean.sigma, Math.sqrt(245) / 20, 1e-12));
    assert.strictEqual(r.mean.text, '12.3 ± 0.8');
    assert.strictEqual(r.last.text, '14.5 ± 2.7');
    assert.ok(near(U.seriesStats('pulses', rows, null).mean.sigma, Math.sqrt(245) / 10, 1e-12));
    const q = U.seriesStats('rate', rows, null);
    assert.ok(near(q.mean.sigma / q.mean.value, 1 / Math.sqrt(245), 1e-12));
    assert.strictEqual(q.last.text, '0.100 ± 0.019');
});

check('seriesStats: строки агрегатов взвешиваются числом отсчётов n', () => {
    const r = U.seriesStats('cps', [{ pulses_2s: 20, n: 30, doserate_usvh: 0.1 }, { pulses_2s: 10, n: 10, doserate_usvh: 0.1 }], null);
    assert.ok(near(r.mean.value, (600 + 100) / 80, 1e-12));
    assert.ok(near(r.mean.sigma, Math.sqrt(700) / 80, 1e-12));
    assert.ok(near(r.last.value, 5, 1e-12) && near(r.last.sigma, Math.sqrt(100) / 20, 1e-12));
    const pl = U.seriesStats('pulses', [{ pulses_2s: 20, n: 30, doserate_usvh: 0.1 }], null).last;
    assert.ok(near(pl.value, 20, 1e-12) && near(pl.sigma, Math.sqrt(600) / 30, 1e-12));
    const rt = U.seriesStats('rate', [{ pulses_2s: 20, n: 30, doserate_usvh: 0.1 }, { pulses_2s: 20, n: 10, doserate_usvh: 0.4 }], null);
    assert.ok(near(rt.mean.value, 0.175, 1e-12));
    const sy = U.seriesStats('cps', [{ pulses_2s: 20 }, { pulses_2s: 20 }], 10);
    assert.ok(near(sy.mean.sigma, Math.sqrt(Math.pow(Math.sqrt(40) / 4, 2) + 1), 1e-12));
    assert.ok(near(sy.last.sigma, Math.sqrt(Math.pow(Math.sqrt(20) / 2, 2) + 1), 1e-12));
});

check('seriesStats: другие ряды, пустые строки и строки без импульсов', () => {
    assert.strictEqual(U.seriesStats('dose', [{ pulses_2s: 1 }], null), null);
    assert.strictEqual(U.seriesStats('cps', [], null), null);
    assert.strictEqual(U.seriesStats('cps', [{ cps: 3 }], null), null);
    assert.strictEqual(U.seriesStats('cps', null, null), null);
    assert.deepStrictEqual(Object.keys(U.liveErrors(5, 60, null)), []);
    assert.deepStrictEqual(Object.keys(U.liveErrors('x', 60, null)), []);
    const z = U.seriesStats('cps', [{ pulses_2s: 0 }, { pulses_2s: 0 }], null);
    assert.strictEqual(z.mean.text, '0.00 ± 0.25');
});

check('envNote: паспорт н/д, поправка отдельно со знаком, шаг АЦП как нижняя граница', () => {
    assert.strictEqual(U.envNote('sht3x', 't', -0.5), 'паспортная погрешность: н/д; поправка оператора (систематическая часть, отдельно): −0.5 °C; дискретизация АЦП (нижняя граница): ±0.0013 °C');
    assert.strictEqual(U.envNote('aht2x', 'h', 0), 'паспортная погрешность: н/д; дискретизация АЦП (нижняя граница): ±0.000048 %');
    assert.strictEqual(U.envNote('bmp280', 'p', 1.5), 'паспортная погрешность: н/д; поправка оператора (систематическая часть, отдельно): +1.5 мм рт. ст.; дискретизация АЦП: н/д');
    assert.strictEqual(U.envNote('bmp280', 't', null), 'паспортная погрешность: н/д; дискретизация АЦП: н/д °C'.replace(' °C', ''));
});
//__MORE__
check('k·σ: N = 100, σ = √N = 10, k = 2 -> 20 (пересчёт python: 2 * sqrt(100) = 20.0)', () => {
    const s = U.pulsesStat(100);
    assert.strictEqual(s.sigma, 10);
    assert.strictEqual(U.total(100, 10, null, 2).sigma, 20);
    assert.strictEqual(U.format(100, 10, { k: 2 }).text, '100 ± 20');
    const p = U.liveErrors({ has_rec: true, pulses_2s: 100 }, 60, null, 2).pulses;
    assert.strictEqual(p.sigma, 20);
    assert.strictEqual(p.stat, 20);
    assert.strictEqual(p.text, '100 ± 20');
});

check('k·σ: total = k·sqrt(stat² + sys²); stat и sys возвращаются с k; k = 1, 2, 3', () => {
    [1, 2, 3].forEach(k => {
        const t = U.total(100, 10, 10, k);
        assert.ok(near(t.sigma, k * Math.sqrt(100 + 100), 1e-12));
        assert.ok(near(t.stat, k * 10, 1e-12) && near(t.sys, k * 10, 1e-12));
        assert.ok(near(t.sigma * t.sigma, t.stat * t.stat + t.sys * t.sys, 1e-9));
    });
    assert.strictEqual(U.format(100, 10, { k: 3 }).text, '100 ± 30');
});

check('k·σ: k применяется к σ, а не к значению; mean остаётся σ/√n', () => {
    const f = U.format(1.234, 0.5, { k: 2 });
    assert.strictEqual(f.value, '1.2');
    assert.strictEqual(f.err, '1.0');
    assert.strictEqual(U.mean(10, 4), 5);
    const s = U.seriesStats('pulses', [{ pulses_2s: 100, n: 1 }], null, 2);
    assert.strictEqual(s.mean.value, 100);
    assert.strictEqual(s.mean.sigma, 20);
});

check('k·σ: k = 1, не задан, мусор -> прежний вывод побайтно', () => {
    const base = JSON.stringify(U.liveErrors(LIVE, 60, 5));
    assert.strictEqual(JSON.stringify(U.liveErrors(LIVE, 60, 5, 1)), base);
    [undefined, null, 0, -2, NaN, 'x'].forEach(k => {
        assert.strictEqual(JSON.stringify(U.liveErrors(LIVE, 60, 5, k)), base);
        assert.strictEqual(U.format(17.66, 0.62, { k: k }).text, '17.7 ± 0.6');
    });
    assert.strictEqual(U.format(17.66, 0.62).text, U.format(17.66, 0.62, { k: 1 }).text);
});

check('k·σ: в живом ответе с live.rate_urh нет величины мкР', () => {
    const o = U.liveErrors(LIVE, 60, null, 2);
    assert.strictEqual(LIVE.rate_urh, 6.44);
    assert.deepStrictEqual(Object.keys(o), ['cps', 'pulses', 'rate', 'avg_cps', 'avg_rate', 'dose']);
    assert.strictEqual(JSON.stringify(o).indexOf('мкР'), -1);
});
console.log('TESTS: ' + passed + '/' + total);
process.exit(passed === total ? 0 : 1);
