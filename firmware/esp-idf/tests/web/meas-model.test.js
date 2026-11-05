const assert = require('assert');
const path = require('path');
const M = require(process.env.HG_MODULE || path.join(__dirname, '..', '..', 'web', 'meas-model.js'));
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }

check('defaults: возвращает объект с ожидаемыми значениями', () => {
    const d = M.defaults();
    assert.strictEqual(d.mode, 'single');
    assert.strictEqual(d.h, 0);
    assert.strictEqual(d.m, 10);
    assert.strictEqual(d.s, 0);
    assert.strictEqual(d.n, 5);
    assert.strictEqual(d.ph, 0);
    assert.strictEqual(d.pm, 0);
    assert.strictEqual(d.ps, 0);
    assert.strictEqual(d.retry, true);
    assert.strictEqual(d.lostMax, 2);
    assert.strictEqual(d.stopMode, 'time');
    assert.strictEqual(d.eps, 1);
    assert.strictEqual(d.tmin, 10);
    assert.strictEqual(d.tmax, 3600);
    assert.strictEqual(d.startMode, 'now');
    assert.strictEqual(d.fileStore, false);
    assert.strictEqual(d.prefix, 'AtomFast');
    assert.strictEqual(d.template, '{prefix}_{n:000}');
    assert.strictEqual(d.noOverwrite, true);
    assert.strictEqual(d.name, '');
});

check('buildBody: single time mode формирует t_s и не содержит sigma полей', () => {
    const f = M.defaults();
    f.h = 1; f.m = 0; f.s = 0;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, true);
    assert.strictEqual(res.body.n, 1);
    assert.strictEqual(res.body.stop_mode, 'time');
    assert.strictEqual(res.body.t_s, 3600);
    assert.strictEqual('endless' in res.body, false);
    assert.strictEqual('pause_s' in res.body, false);
    assert.strictEqual('eps_pct' in res.body, false);
    assert.strictEqual('t_min_s' in res.body, false);
    assert.strictEqual('t_max_s' in res.body, false);
});

check('buildBody: single sigma mode формирует eps_pct и не содержит t_s', () => {
    const f = M.defaults();
    f.stopMode = 'sigma';
    f.eps = 1;
    f.tmin = 10;
    f.tmax = 3600;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, true);
    assert.strictEqual(res.body.stop_mode, 'sigma');
    assert.strictEqual(res.body.eps_pct, 1);
    assert.strictEqual(res.body.t_min_s, 10);
    assert.strictEqual(res.body.t_max_s, 3600);
    assert.strictEqual('t_s' in res.body, false);
});

check('buildBody: series mode содержит pause_s и stop_mode time', () => {
    const f = M.defaults();
    f.mode = 'series';
    f.n = 5;
    f.ph = 1; f.pm = 0; f.ps = 0;
    f.stopMode = 'sigma'; // Должно быть проигнорировано
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, true);
    assert.strictEqual(res.body.n, 5);
    assert.strictEqual(res.body.pause_s, 3600);
    assert.strictEqual(res.body.stop_mode, 'time');
    assert.strictEqual('eps_pct' in res.body, false);
    assert.strictEqual('t_min_s' in res.body, false);
    assert.strictEqual('t_max_s' in res.body, false);
});

check('buildBody: series mode с n=1 возвращает ошибку поля n', () => {
    const f = M.defaults();
    f.mode = 'series';
    f.n = 1;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, false);
    assert.ok(res.errors.some(e => e.field === 'n'));
});

check('buildBody: series mode с n=1001 возвращает ошибку поля n', () => {
    const f = M.defaults();
    f.mode = 'series';
    f.n = 1001;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, false);
    assert.ok(res.errors.some(e => e.field === 'n'));
});

check('buildBody: endless mode содержит endless true и pause_s', () => {
    const f = M.defaults();
    f.mode = 'endless';
    f.ph = 1; f.pm = 0; f.ps = 0;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, true);
    assert.strictEqual(res.body.endless, true);
    assert.strictEqual('n' in res.body, false);
    assert.strictEqual(res.body.pause_s, 3600);
});

check('buildBody: t_s меньше 10 секунд возвращает ошибку поля t', () => {
    const f = M.defaults();
    f.h = 0; f.m = 0; f.s = 9;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, false);
    assert.ok(res.errors.some(e => e.field === 't'));
});

check('buildBody: минуты 60 возвращают ошибку поля t', () => {
    const f = M.defaults();
    f.h = 0; f.m = 60; f.s = 0;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, false);
    assert.ok(res.errors.some(e => e.field === 't'));
});

check('buildBody: lost_max вне диапазона 0..60 возвращает ошибку поля lostMax', () => {
    const f = M.defaults();
    f.lostMax = 61;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, false);
    assert.ok(res.errors.some(e => e.field === 'lostMax'));
});

check('buildBody: retry false приводит к retry_invalid false', () => {
    const f = M.defaults();
    f.retry = false;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, true);
    assert.strictEqual(res.body.retry_invalid, false);
});

check('buildBody: fileStore true добавляет name_template, prefix, no_overwrite', () => {
    const f = M.defaults();
    f.fileStore = true;
    f.prefix = 'Test';
    f.template = '{prefix}_{n:000}';
    f.noOverwrite = true;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, true);
    assert.strictEqual(res.body.name_template, '{prefix}_{n:000}');
    assert.strictEqual(res.body.prefix, 'Test');
    assert.strictEqual(res.body.no_overwrite, true);
});

check('buildBody: fileStore false не содержит name_template, prefix, no_overwrite', () => {
    const f = M.defaults();
    f.fileStore = false;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, true);
    assert.strictEqual('name_template' in res.body, false);
    assert.strictEqual('prefix' in res.body, false);
    assert.strictEqual('no_overwrite' in res.body, false);
});

check('buildBody: eps 0.05 при sigma возвращает ошибку поля eps', () => {
    const f = M.defaults();
    f.stopMode = 'sigma';
    f.eps = 0.05;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, false);
    assert.ok(res.errors.some(e => e.field === 'eps'));
});

check('buildBody: eps 51 при sigma возвращает ошибку поля eps', () => {
    const f = M.defaults();
    f.stopMode = 'sigma';
    f.eps = 51;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, false);
    assert.ok(res.errors.some(e => e.field === 'eps'));
});

check('buildBody: tmin > tmax возвращает ошибку поля tmax', () => {
    const f = M.defaults();
    f.stopMode = 'sigma';
    f.tmin = 100;
    f.tmax = 50;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, false);
    assert.ok(res.errors.some(e => e.field === 'tmax'));
});

check('buildBody: name длиннее 32 байт UTF-8 возвращает ошибку поля name', () => {
    const f = M.defaults();
    f.name = 'абвгдеёжзийклмнопр'; // 17 русских букв = 34 байта
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, false);
    assert.ok(res.errors.some(e => e.field === 'name'));
});

check('buildBody: name ровно 16 русских букв проходит валидацию', () => {
    const f = M.defaults();
    f.name = 'абвгдеёжзийклмно'; // 16 русских букв = 32 байта
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, true);
});

check('buildBody: startMode delay с delayMin 5 формирует delay_s 300', () => {
    const f = M.defaults();
    f.startMode = 'delay';
    f.delayMin = 5;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, true);
    assert.strictEqual(res.body.start_mode, 'delay');
    assert.strictEqual(res.body.delay_s, 300);
});

check('buildBody: startMode delay с delayMin 0 возвращает ошибку поля delayMin', () => {
    const f = M.defaults();
    f.startMode = 'delay';
    f.delayMin = 0;
    const res = M.buildBody(f, {timeOk: true});
    assert.strictEqual(res.ok, false);
    assert.ok(res.errors.some(e => e.field === 'delayMin'));
});

check('buildBody: startMode at и timeOk false возвращает ошибку поля startAt', () => {
    const f = M.defaults();
    f.startMode = 'at';
    const res = M.buildBody(f, {timeOk: false});
    assert.strictEqual(res.ok, false);
    assert.ok(res.errors.some(e => e.field === 'startAt'));
});

check('totalSec: single time mode возвращает t_s', () => {
    const f = M.defaults();
    f.h = 0; f.m = 1; f.s = 0;
    assert.strictEqual(M.totalSec(f), 60);
});

check('totalSec: series mode возвращает n*(t+pause)', () => {
    const f = M.defaults();
    f.mode = 'series';
    f.n = 5;
    f.h = 0; f.m = 1; f.s = 0; // t=60
    f.ph = 0; f.pm = 0; f.ps = 10; // pause=10
    assert.strictEqual(M.totalSec(f), 5 * (60 + 10));
});

check('totalSec: endless mode возвращает null', () => {
    const f = M.defaults();
    f.mode = 'endless';
    assert.strictEqual(M.totalSec(f), null);
});

check('totalSec: single sigma mode возвращает null', () => {
    const f = M.defaults();
    f.stopMode = 'sigma';
    assert.strictEqual(M.totalSec(f), null);
});

check('sigmaEstimate: eps=1, cps=10 возвращает n=10000, sec=1000, tooLong=false', () => {
    const res = M.sigmaEstimate(1, 10, 3600);
    assert.strictEqual(res.n, 10000);
    assert.strictEqual(res.sec, 1000);
    assert.strictEqual(res.tooLong, false);
});

check('sigmaEstimate: tmax=500 возвращает tooLong=true', () => {
    const res = M.sigmaEstimate(1, 10, 500);
    assert.strictEqual(res.tooLong, true);
});

check('sigmaEstimate: cps=0 возвращает null', () => {
    const res = M.sigmaEstimate(1, 0, 3600);
    assert.strictEqual(res, null);
});

check('sigmaEstimate: eps вне 0.1..50 возвращает null', () => {
    assert.strictEqual(M.sigmaEstimate(0.05, 10, 3600), null);
    assert.strictEqual(M.sigmaEstimate(51, 10, 3600), null);
});

check('errorText: 409 busy содержит конфликт и уже идёт', () => {
    const text = M.errorText(409, 'busy');
    assert.ok(text.includes('конфликт'));
    assert.ok(text.includes('уже идёт'));
});

check('errorText: 403 содержит токен', () => {
    const text = M.errorText(403, '');
    assert.ok(text.includes('токен'));
});

check('errorText: 400 sigma_only_single содержит только в разовом', () => {
    const text = M.errorText(400, 'sigma_only_single');
    assert.ok(text.includes('только в разовом'));
});

check('errorText: 500 пустая ошибка возвращает ошибка 500', () => {
    const text = M.errorText(500, '');
    assert.strictEqual(text, 'ошибка 500');
});

check('errorText: неизвестный код err zzz при 400 содержит (zzz)', () => {
    const text = M.errorText(400, 'zzz');
    assert.ok(text.includes('(zzz)'));
});

check('parse: не объект возвращает null', () => {
    assert.strictEqual(M.parse(null), null);
    assert.strictEqual(M.parse('string'), null);
});

check('parse: ok !== true возвращает null', () => {
    assert.strictEqual(M.parse({ok: false}), null);
});

check('parse: нет runs возвращает null', () => {
    assert.strictEqual(M.parse({ok: true}), null);
});

check('parse: корректный ответ возвращает объект с runs', () => {
    const j = {
        ok: true,
        time_ok: true,
        active_id: 7,
        runs: [
            {run: {id: 7}, sum: {n_valid: 1}, live: {phase: 'measuring'}}
        ]
    };
    const res = M.parse(j);
    assert.strictEqual(res.timeOk, true);
    assert.strictEqual(res.activeId, 7);
    assert.strictEqual(res.runs.length, 1);
    assert.strictEqual(res.runs[0].run.id, 7);
});

check('parse: записи без run.id отбрасываются', () => {
    const j = {
        ok: true,
        runs: [
            {sum: {n_valid: 1}}, // нет run
            {run: {id: 3}, sum: {n_valid: 1}}
        ]
    };
    const res = M.parse(j);
    assert.strictEqual(res.runs.length, 1);
    assert.strictEqual(res.runs[0].run.id, 3);
});

check('activeRun: возвращает run по activeId', () => {
    const p = {
        activeId: 'id1',
        runs: [
            {run: {id: 'id1'}, sum: {}, live: {}},
            {run: {id: 'id2'}, sum: {}, live: {}}
        ]
    };
    const res = M.activeRun(p);
    assert.strictEqual(res.run.id, 'id1');
});

check('activeRun: null если нет активного', () => {
    const p = {
        activeId: 'id3',
        runs: [
            {run: {id: 'id1'}, sum: {}, live: {}}
        ]
    };
    assert.strictEqual(M.activeRun(p), null);
});

check('badge: нет активного возвращает active false', () => {
    const p = {
        activeId: null,
        runs: []
    };
    const res = M.badge(p);
    assert.strictEqual(res.active, false);
});

check('badge: серия n=5 done=2 возвращает text 2/5 и active true', () => {
    const p = {
        activeId: 'id1',
        runs: [
            {
                run: {id: 'id1', n: 5, done: 2, state: 'running'},
                sum: {n_valid: 2},
                live: {}
            }
        ]
    };
    const res = M.badge(p);
    assert.strictEqual(res.active, true);
    assert.strictEqual(res.text, '2/5');
});

check('badge: разовое n=1 возвращает text ●', () => {
    const p = {
        activeId: 'id1',
        runs: [
            {
                run: {id: 'id1', n: 1, state: 'running'},
                sum: {n_valid: 0},
                live: {}
            }
        ]
    };
    const res = M.badge(p);
    assert.strictEqual(res.active, true);
    assert.strictEqual(res.text, '●');
});

check('badge: state paused возвращает paused true', () => {
    const p = {
        activeId: 'id1',
        runs: [
            {
                run: {id: 'id1', n: 5, state: 'paused'},
                sum: {n_valid: 2},
                live: {}
            }
        ]
    };
    const res = M.badge(p);
    assert.strictEqual(res.paused, true);
});

check('progress: n=4, t_s=100, done=1, elapsed 50000 возвращает 0.375', () => {
    const run = {n: 4, t_s: 100, done: 1, state: 'running'};
    const live = {phase: 'measuring', elapsed_ms: 50000};
    const res = M.progress(run, live);
    assert.strictEqual(res, 0.375);
});

check('progress: stop_mode sigma возвращает null', () => {
    const run = {n: 4, t_s: 100, stop_mode: 'sigma', state: 'running'};
    const live = {phase: 'measuring', elapsed_ms: 50000};
    assert.strictEqual(M.progress(run, live), null);
});

check('progress: n=0 возвращает null', () => {
    const run = {n: 0, t_s: 100, state: 'running'};
    const live = {phase: 'measuring', elapsed_ms: 50000};
    assert.strictEqual(M.progress(run, live), null);
});

check('progress: значение в диапазоне [0,1]', () => {
    const run = {n: 4, t_s: 100, done: 1, state: 'running'};
    const live = {phase: 'measuring', elapsed_ms: 50000};
    const res = M.progress(run, live);
    assert.ok(res >= 0 && res <= 1);
});

check('dur: 59 секунд возвращает 59 с', () => {
    assert.strictEqual(M.dur(59), '59 с');
});

check('dur: 61 секунд возвращает 1 мин 01 с', () => {
    assert.strictEqual(M.dur(61), '1 мин 01 с');
});

check('dur: 3700 секунд возвращает 1 ч 1 мин', () => {
    assert.strictEqual(M.dur(3700), '1 ч 1 мин');
});

check('dur: NaN возвращает н/д', () => {
    assert.strictEqual(M.dur(NaN), 'н/д');
});

check('dur: отрицательное значение возвращает н/д', () => {
    assert.strictEqual(M.dur(-10), 'н/д');
});

check('fnum: NaN возвращает н/д', () => {
    assert.strictEqual(M.fnum(NaN), 'н/д');
});

check('fnum: 0 возвращает 0', () => {
    assert.strictEqual(M.fnum(0), '0');
});

check('fv: NaN возвращает н/д', () => {
    assert.strictEqual(M.fv(NaN, 0.5), 'н/д');
});

check('fv: 5 и 0.5 содержит ±', () => {
    const res = M.fv(5, 0.5);
    assert.ok(res.includes(' ± '));
});

check('chi2Text: NaN возвращает н/д', () => {
    assert.strictEqual(M.chi2Text(NaN), 'н/д');
});

check('chi2Text: 1.0 содержит ≈1', () => {
    const res = M.chi2Text(1.0);
    assert.ok(res.includes('≈1'));
});

check('chi2Text: 0.2 содержит меньше статистического', () => {
    const res = M.chi2Text(0.2);
    assert.ok(res.includes('меньше статистического'));
});

check('chi2Text: 5 содержит ≫1', () => {
    const res = M.chi2Text(5);
    assert.ok(res.includes('≫1'));
});

check('sumRows: sum без n_valid возвращает пустой массив', () => {
    const res = M.sumRows({});
    assert.strictEqual(res.length, 0);
});

check('sumRows: n_valid 5 возвращает массив пар с Действительных измерений', () => {
    const res = M.sumRows({n_valid: 5});
    assert.strictEqual(res.length > 0, true);
    assert.strictEqual(res[0][0], 'Действительных измерений');
    assert.strictEqual(res[0][1], '5');
});

check('sumRows: n_invalid 2 содержит недействительных 2', () => {
    const res = M.sumRows({n_valid: 5, n_invalid: 2});
    assert.ok(res[0][1].includes('недействительных 2'));
});

check('resultRows: строка с cps null возвращает н/д', () => {
    const rows = [{cps: null, valid: true, attempt: 1}];
    const res = M.resultRows(rows);
    assert.strictEqual(res[0].cps, 'н/д');
});

check('resultRows: valid false возвращает valid false и status содержит недействительно', () => {
    const rows = [{cps: 10, valid: false, attempt: 1}];
    const res = M.resultRows(rows);
    assert.strictEqual(res[0].valid, false);
    assert.ok(res[0].status.includes('недействительно'));
});

check('resultRows: attempt 2 возвращает status содержит попытка 2', () => {
    const rows = [{cps: 10, valid: true, attempt: 2}];
    const res = M.resultRows(rows);
    assert.ok(res[0].status.includes('попытка 2'));
});

check('resultRows: не массив возвращает пустой массив', () => {
    assert.strictEqual(M.resultRows(null).length, 0);
    assert.strictEqual(M.resultRows('string').length, 0);
});

check('points: строки без числового y отбрасываются', () => {
    const rows = [
        {i: 1, cps: 10, valid: true},
        {i: 2, cps: null, valid: true},
        {i: 3, cps: 20, valid: true}
    ];
    const res = M.points(rows, 'cps', 'index');
    assert.strictEqual(res.pts.length, 2);
});

check('points: xmode time и t0_utc > 0 возвращает byTime true и x = t0_utc', () => {
    const rows = [
        {i: 1, cps: 10, valid: true, t0_utc: 1000},
        {i: 2, cps: 20, valid: true, t0_utc: 2000}
    ];
    const res = M.points(rows, 'cps', 'time');
    assert.strictEqual(res.byTime, true);
    assert.strictEqual(res.pts[0].x, 1000);
    assert.strictEqual(res.pts[1].x, 2000);
});

check('points: xmode time и t0_utc = 0 возвращает x = i', () => {
    const rows = [
        {i: 1, cps: 10, valid: true, t0_utc: 0},
        {i: 2, cps: 20, valid: true, t0_utc: 0}
    ];
    const res = M.points(rows, 'cps', 'time');
    assert.strictEqual(res.byTime, false);
    assert.strictEqual(res.pts[0].x, 1);
    assert.strictEqual(res.pts[1].x, 2);
});

check('points: поля ur_h больше нет; запрос ur_h = откат на cps, подписей «мкР» нет', () => {
    const rows = [
        {i: 1, cps: 3, usv_h: 0.05, counts: 7, valid: true},
        {i: 2, cps: 4, usv_h: 0.1, counts: 9, valid: true}
    ];
    const res = M.points(rows, 'ur_h', 'index');
    assert.strictEqual(res.label, M.points(rows, 'cps', 'index').label);
    assert.deepStrictEqual(res.pts.map(p => p.y), [3, 4]);
    ['cps', 'usv_h', 'counts', 'ur_h'].forEach(f => {
        assert.strictEqual(M.points(rows, f, 'index').label.indexOf('мкР'), -1);
    });
    assert.deepStrictEqual(M.points([{i: 1, usv_h: 0.05, valid: true}], 'ur_h', 'index').pts, []);
});

check('stats: только valid, mean 5, sd sqrt(2), n 2', () => {
    const pts = [
        {y: 4, valid: true},
        {y: 6, valid: true},
        {y: 100, valid: false}
    ];
    const res = M.stats(pts, 'cps');
    assert.strictEqual(res.n, 2);
    assert.strictEqual(res.mean, 5);
    assert.strictEqual(res.sd, Math.sqrt(2));
});

check('stats: пустой массив возвращает n 0, mean null', () => {
    const res = M.stats([], 'cps');
    assert.strictEqual(res.n, 0);
    assert.strictEqual(res.mean, null);
});

check('stats: один valid возвращает sd null', () => {
    const pts = [{y: 5, valid: true}];
    const res = M.stats(pts, 'cps');
    assert.strictEqual(res.sd, null);
});

check('histBins: [1,2,3,4] с k=2 возвращает 2 корзины, сумма count 4', () => {
    const res = M.histBins([1, 2, 3, 4], 2).bins;
    assert.strictEqual(res.length, 2);
    const total = res.reduce((sum, bin) => sum + bin.count, 0);
    assert.strictEqual(total, 4);
});

check('histBins: значение max попадает в последнюю корзину', () => {
    const res = M.histBins([1, 2, 3, 4], 2).bins;
    const lastBin = res[res.length - 1];
    assert.ok(lastBin.count > 0);
});

check('histBins: все одинаковые возвращает одну корзину count n', () => {
    const res = M.histBins([5, 5, 5], 2).bins;
    assert.strictEqual(res.length, 1);
    assert.strictEqual(res[0].count, 3);
});

check('histBins: пустой массив возвращает bins []', () => {
    const res = M.histBins([], 2).bins;
    assert.strictEqual(res.length, 0);
});

check('doseSeries: накопление usv_h*t_ms/3600000 только по valid true', () => {
    const rows = [
        {i: 1, usv_h: 3.6, t_ms: 3600000, valid: true},
        {i: 2, usv_h: 3.6, t_ms: 3600000, valid: true},
        {i: 3, usv_h: 100, t_ms: 3600000, valid: false}
    ];
    const res = M.doseSeries(rows);
    assert.strictEqual(res.length, 2);
    assert.strictEqual(res[0].y, 3.6);
    assert.strictEqual(res[1].y, 7.2);
});

check('doseSeries: строки invalid пропускаются', () => {
    const rows = [
        {usv_h: 3.6, t_ms: 3600000, valid: false}
    ];
    const res = M.doseSeries(rows);
    assert.strictEqual(res.length, 0);
});

check('plotModel: пусто возвращает empty true', () => {
    const res = M.plotModel([], 100, 100, {});
    assert.strictEqual(res.empty, true);
});

check('plotModel: одна точка не даёт NaN в sx/sy', () => {
    const pts = [{x: 0, y: 10, s: 0, valid: true}];
    const res = M.plotModel(pts, 100, 100, {});
    assert.ok(!isNaN(res.pts[0].px));
    assert.ok(!isNaN(res.pts[0].py));
});

check('plotModel: y растёт -> py убывает', () => {
    const pts = [
        {x: 0, y: 10, s: 0, valid: true},
        {x: 1, y: 20, s: 0, valid: true}
    ];
    const res = M.plotModel(pts, 100, 100, {});
    assert.ok(res.pts[0].py > res.pts[1].py);
});

check('plotModel: o.zero и все y>0 -> y0 <= 0', () => {
    const pts = [
        {x: 0, y: 10, s: 0, valid: true},
        {x: 1, y: 20, s: 0, valid: true}
    ];
    const res = M.plotModel(pts, 100, 100, {zero: true});
    assert.ok(res.y0 <= 0);
});

check('plotModel: errbar точка y=10, s=2 -> y0 <= 8 и y1 >= 12', () => {
    const pts = [{x: 0, y: 10, s: 2, valid: true}];
    const res = M.plotModel(pts, 100, 100, {});
    assert.ok(res.y0 <= 8);
    assert.ok(res.y1 >= 12);
});

check('pointTitle: содержит Измерение 3, недействительно, попытка 2', () => {
    const res = M.pointTitle({i: 3, y: 5, s: 0.5, valid: false, attempt: 2}, 'CPS');
    assert.ok(res.includes('Измерение 3'));
    assert.ok(res.includes('недействительно'));
    assert.ok(res.includes('попытка 2'));
});

check('buildBody: старт в указанное время при timeOk=false даёт ошибку startAt', () => {
    const f = M.defaults(); f.startMode = 'at'; f.startAt = '2030-01-01T10:00';
    const bad = M.buildBody(f, {timeOk: false});
    assert.strictEqual(bad.ok, false);
    assert.ok(bad.errors.some(e => e.field === 'startAt'));
    const good = M.buildBody(f, {timeOk: true});
    assert.strictEqual(good.ok, true);
    assert.strictEqual(typeof good.body.start_at, 'number');
});

check('resultRows: неполное измерение помечено, полное нет', () => {
    const a = M.resultRows([{i: 1, valid: true, partial: true}])[0].status;
    const b = M.resultRows([{i: 1, valid: true, partial: false}])[0].status;
    assert.ok(a.includes('неполное'));
    assert.ok(!b.includes('неполное'));
});

console.log(passed + '/' + total);
process.exit(passed === total ? 0 : 1);
