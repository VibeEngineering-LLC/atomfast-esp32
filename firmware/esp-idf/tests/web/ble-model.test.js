const assert = require('assert');
const path = require('path');
const cp = require('child_process');
const B = require(process.env.HB_MODULE || path.join(__dirname, '..', '..', 'web', 'ble-model.js'));
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }
const near = (a, b, e) => Math.abs(a - b) <= e;
const py = (expr) => JSON.parse(cp.execFileSync('python', ['-c', 'import sys,json;sys.path.insert(0,sys.argv[1]);import mock_ble as m;print(json.dumps(' + expr + ',ensure_ascii=False))', __dirname], { encoding: 'utf8', env: Object.assign({}, process.env, { PYTHONIOENCODING: 'utf-8', PYTHONUTF8: '1' }) }));
const N = py("m.ble('normal')");
const E = py("m.ble('errors')");
const MANY = py("m.sessions('many')");
const get = (card, k) => { const r = card.rows.find(x => x.k.indexOf(k) === 0); return r ? r.v : undefined; };

check('fmtDur: границы единиц и округление', () => {
    const c = [[0, '0 с'], [45, '45 с'], [59.4, '59 с'], [59.6, '1 мин 00 с'], [60, '1 мин 00 с'], [302, '5 мин 02 с'], [3599.4, '59 мин 59 с'], [3599.6, '1 ч 00 мин'], [5432.123, '1 ч 30 мин'], [86399, '23 ч 59 мин'], [86400, '1 сут 0 ч'], [90000, '1 сут 1 ч'], [259200 + 7200, '3 сут 2 ч']];
    for (const [v, t] of c) assert.strictEqual(B.fmtDur(v), t, String(v));
    for (const v of [null, undefined, -1, NaN, Infinity, '5']) assert.strictEqual(B.fmtDur(v), 'н/д', String(v));
});

check('fmtUtc: местное время или н/д', () => {
    const d = new Date(1792893245 * 1000);
    const p = (n) => (n < 10 ? '0' : '') + n;
    assert.strictEqual(B.fmtUtc(1792893245), p(d.getDate()) + '.' + p(d.getMonth() + 1) + ' ' + p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds()));
    for (const v of [null, 0, -5, NaN, undefined]) assert.strictEqual(B.fmtUtc(v), 'н/д');
});

check('num: знаки, десятичные, отрицательный ноль, н/д', () => {
    assert.strictEqual(B.num(1.2345, 2), '1.23');
    assert.strictEqual(B.num(7), '7');
    assert.strictEqual(B.num(-71), '-71');
    assert.strictEqual(B.num(-0.001, 1), '0.0');
    assert.strictEqual(B.num(null), 'н/д');
    assert.strictEqual(B.num('5'), 'н/д');
    assert.strictEqual(B.num(NaN, 2), 'н/д');
});

check('state: нет объекта, стек не готов, готов', () => {
    assert.strictEqual(B.state(null), 'none');
    assert.strictEqual(B.state([]), 'none');
    assert.strictEqual(B.state({ ready: false }), 'notready');
    assert.strictEqual(B.state({ ready: true }), 'notready');
    assert.strictEqual(B.state(N), 'ok');
    assert.deepStrictEqual(B.cards({ ready: false }), []);
    assert.deepStrictEqual(B.cards(null), []);
});

check('reasonsText: все группы по порядку, нет данных -> н/д', () => {
    assert.strictEqual(B.reasonsText(N.link.reasons), 'таймаут 1 · удалённо 1 · локально 0 · не удалось установить 0 · L2CAP 0 · прочее 0');
    assert.strictEqual(B.reasonsText({ timeout: 2 }), 'таймаут 2 · удалённо н/д · локально н/д · не удалось установить н/д · L2CAP н/д · прочее н/д');
    assert.strictEqual(B.reasonsText(null), 'н/д');
});

check('cards: пять карточек в порядке спеки', () => {
    const c = B.cards(N);
    assert.deepStrictEqual(c.map(x => x.id), ['link', 'rate', 'params', 'sessions', 'scan']);
    assert.deepStrictEqual(c.map(x => x.title), ['Связь', 'Скорость', 'Параметры соединения', 'Сессии', 'Сканирование']);
    assert.deepStrictEqual(c.map(x => x.rows.length), [17, 25, 13, 5, 9]);
});

check('cards: связь, значения по эталонному ответу', () => {
    const l = B.cards(N)[0];
    assert.strictEqual(get(l, 'Подключён'), 'да');
    assert.strictEqual(get(l, 'Длительность текущей сессии'), '1 ч 30 мин');
    assert.strictEqual(get(l, 'Доля времени на связи с загрузки'), '98.7');
    assert.strictEqual(get(l, 'Доля времени на связи за 24 ч'), '99.2');
    assert.strictEqual(get(l, 'Подключений с загрузки'), '3');
    assert.strictEqual(get(l, 'Время на связи за всё время'), '10 сут 13 ч');
    assert.strictEqual(get(l, 'Причина последнего разрыва'), 'supervision timeout (0x08)');
});

check('cards: ошибки и нет данных показываются как н/д, а не нули', () => {
    const c = B.cards(E);
    assert.strictEqual(get(c[0], 'Подключён'), 'нет');
    assert.strictEqual(get(c[0], 'Длительность текущей сессии'), 'нет активной сессии');
    assert.strictEqual(get(c[0], 'Начало текущей сессии'), 'н/д');
    assert.strictEqual(get(c[0], 'Неудачных попыток подряд сейчас'), '5');
    assert.strictEqual(get(c[1], 'Пакетов в секунду, мгновенно'), 'н/д');
    assert.strictEqual(get(c[1], 'Возраст последнего пакета'), 'н/д');
    assert.strictEqual(get(c[2], 'RSSI сейчас'), 'н/д');
    assert.strictEqual(get(c[2], 'MTU'), 'н/д');
    assert.strictEqual(get(c[2], 'Slave latency'), 'н/д');
    assert.strictEqual(get(c[0], 'Причина последнего разрыва'), 'conn failed to establish (0x3E)');
});

check('cards: скорость, интервалы и единицы в подписи', () => {
    const r = B.cards(N)[1];
    assert.strictEqual(get(r, 'Пакетов в секунду, мгновенно'), '0.500');
    assert.strictEqual(get(r, 'Пакетов в секунду, за 1 мин'), '0.499');
    assert.strictEqual(get(r, 'Байт в секунду, за сессию'), '6.5');
    assert.strictEqual(get(r, 'Интервал между пакетами, последний'), '2001');
    assert.strictEqual(r.rows.find(x => x.k.indexOf('Интервал между пакетами, последний') === 0).hint, 'номинал 2000 мс');
    assert.strictEqual(get(r, 'Джиттер'), '35.7');
    assert.strictEqual(get(r, 'Доля плохих пакетов, %'), '0.014');
    assert.strictEqual(get(r, 'Последний плохой пакет'), '20 мин 35 с');
});

check('cards: параметры, PHY н/д с пояснением, мощность P20 -> 15', () => {
    const p = B.cards(N)[2];
    const phy = p.rows.find(x => x.k === 'PHY передачи');
    assert.strictEqual(phy.v, 'н/д');
    assert.strictEqual(phy.hint, 'API BLE 5.0 выключен в этой сборке');
    assert.strictEqual(get(p, 'Уровень мощности'), '15 (P20)');
    assert.strictEqual(get(p, 'Мощность передачи, задана, дБм'), '20');
    assert.strictEqual(get(p, 'Интервал соединения, мс'), '30.00');
    assert.strictEqual(get(p, 'Supervision timeout, мс'), '4000');
    assert.strictEqual(get(p, 'RSSI сейчас, дБм'), '-71');
    assert.strictEqual(get(p, 'MTU, байт'), '23');
});

check('cards: сессии и сканирование', () => {
    const c = B.cards(N);
    assert.strictEqual(get(c[3], 'Самая длинная'), '2 ч 30 мин');
    assert.strictEqual(get(c[3], 'Самая короткая'), '12 с');
    assert.strictEqual(c[3].rows.find(x => x.k === 'Медианная').hint, 'по последним 3');
    assert.strictEqual(get(c[4], 'Окно сканирования, мс'), '99.0');
    assert.strictEqual(get(c[4], 'Адрес прибора (маска)'), 'AA:BB:**:**:CC:DD');
    assert.strictEqual(get(c[4], 'Имя прибора'), 'AtomFast');
    const empty = JSON.parse(JSON.stringify(N)); empty.link.ring_n = 0; empty.link.avg_session_s = null; empty.link.median_session_s = null; empty.link.last_session_s = null;
    const s = B.cards(empty)[3];
    assert.strictEqual(get(s, 'Средняя'), 'н/д');
    assert.strictEqual(s.rows.find(x => x.k === 'Средняя').hint, 'нет завершённых сессий');
});

check('b29 B04: ext=false или поле опущено -> доля за 24 ч н/д, бейдж не красный; ext=true как раньше', () => {
    const now = MANY[0].end_utc + 3600;
    const off = JSON.parse(JSON.stringify(N)); off.ext = false; delete off.link.up_24h_pct;
    assert.strictEqual(get(B.cards(off)[0], 'Доля времени на связи за 24 ч'), 'н/д');
    assert.deepStrictEqual(B.badge(off, [], now), { text: '', level: '' });
    const offz = JSON.parse(JSON.stringify(N)); offz.ext = false; offz.link.up_24h_pct = 0;
    assert.strictEqual(get(B.cards(offz)[0], 'Доля времени на связи за 24 ч'), 'н/д');
    assert.strictEqual(B.badge(offz, [], now).level, '');
    const miss = JSON.parse(JSON.stringify(N)); delete miss.link.up_24h_pct;
    assert.strictEqual(get(B.cards(miss)[0], 'Доля времени на связи за 24 ч'), 'н/д');
    assert.strictEqual(B.badge(miss, [], now).level, '');
    const on = JSON.parse(JSON.stringify(N)); on.ext = true; on.link.up_24h_pct = 0;
    assert.strictEqual(get(B.cards(on)[0], 'Доля времени на связи за 24 ч'), '0.0');
    assert.strictEqual(B.badge(on, [], now).level, 'bad');
});

check('b29 B05: нет поля -> н/д, а не состояние (нет сессии / не было)', () => {
    const sp = B.cards({ ready: true, link: {} })[0];
    assert.strictEqual(get(sp, 'Длительность текущей сессии'), 'н/д');
    assert.strictEqual(get(sp, 'Подключён'), 'н/д');
    const off = JSON.parse(JSON.stringify(N)); off.connected = false;
    assert.strictEqual(get(B.cards(off)[0], 'Длительность текущей сессии'), 'нет активной сессии');
    const nobad = JSON.parse(JSON.stringify(N)); delete nobad.rate.last_bad_age_s;
    assert.strictEqual(get(B.cards(nobad)[1], 'Последний плохой пакет'), 'н/д');
    nobad.rate.last_bad_age_s = null;
    assert.strictEqual(get(B.cards(nobad)[1], 'Последний плохой пакет'), 'не было');
    const norate = JSON.parse(JSON.stringify(N)); delete norate.rate;
    assert.strictEqual(get(B.cards(norate)[1], 'Последний плохой пакет'), 'н/д');
});

check('histBars: границы корзин и доли', () => {
    const h = B.histBars(N.params);
    assert.strictEqual(h.length, 8);
    assert.deepStrictEqual(h.map(x => x.label), ['выше -40', '-40...-49', '-50...-59', '-60...-69', '-70...-79', '-80...-89', '-90...-99', '-100 и ниже']);
    assert.deepStrictEqual(h.map(x => x.count), [0, 2, 10, 120, 800, 400, 20, 0]);
    assert.ok(near(h[4].frac, 800 / 1352, 1e-12) && near(h.reduce((a, x) => a + x.frac, 0), 1, 1e-12));
    assert.deepStrictEqual(B.histBars({ rssi_hist: [0, 0] }).map(x => x.frac), [0, 0]);
    assert.deepStrictEqual(B.histBars(null), []);
});

check('sessionRows: колонки, н/д вместо пустых значений', () => {
    const r = B.sessionRows(MANY);
    assert.strictEqual(r.length, 32);
    assert.strictEqual(r[0].idx, '1');
    assert.strictEqual(r[0].dur, '1 ч 30 мин');
    assert.strictEqual(r[0].reason, 'supervision timeout (8)');
    assert.strictEqual(r[0].rssi, '-88 / -72 / -60');
    assert.strictEqual(r[0].pkts, '2716');
    assert.strictEqual(r[0].setup, 'н/д');
    assert.strictEqual(r[1].start, 'н/д');
    assert.strictEqual(r[2].setup, '1260');
    const odd = B.sessionRows([{ idx: 0, dur_s: 5, reason: '', rssi_min_dbm: null, rssi_avg_dbm: null, rssi_max_dbm: null }]);
    assert.strictEqual(odd[0].reason, 'н/д');
    assert.strictEqual(odd[0].rssi, 'н/д');
});

check('parseSessions: только массив объектов с idx и dur_s', () => {
    assert.deepStrictEqual(B.parseSessions(null), []);
    assert.deepStrictEqual(B.parseSessions({ idx: 0, dur_s: 1 }), []);
    assert.deepStrictEqual(B.parseSessions([1, null, { idx: 0 }, { dur_s: 3 }, { idx: 1, dur_s: 2 }]), [{ idx: 1, dur_s: 2 }]);
});

check('badge: число разрывов за 24 ч и цвет по доле на связи', () => {
    const now = MANY[0].end_utc + 3600;
    assert.deepStrictEqual(B.badge(N, MANY.slice(0, 3), now), { text: '2', level: '' });
    const old = MANY.map((s) => Object.assign({}, s, { end_utc: now - 90000 }));
    assert.strictEqual(B.badge(N, old, now).text, '0');
    const lv = (p) => B.badge(Object.assign({}, N, { link: Object.assign({}, N.link, { up_24h_pct: p }) }), [], now).level;
    assert.deepStrictEqual([lv(99.9), lv(95), lv(94.9), lv(80), lv(79.9), lv(null)], ['', '', 'warn', 'warn', 'bad', '']);
    assert.deepStrictEqual(B.badge({ ready: false }, MANY, now), { text: '', level: '' });
    assert.strictEqual(B.badge(N, [], now).text, '');
    assert.strictEqual(B.badge(N, MANY, null).text, '');
    const future = [{ idx: 0, dur_s: 1, end_utc: now + 500 }];
    assert.strictEqual(B.badge(N, future, now).text, '0');
});

check('pushSample: кольцо ограничено 120, не число -> null', () => {
    let r = [];
    for (let i = 0; i < 130; i++) r = B.pushSample(r, i);
    assert.strictEqual(r.length, 120);
    assert.strictEqual(r[0], 10);
    assert.strictEqual(r[119], 129);
    assert.strictEqual(B.pushSample([1], NaN)[1], null);
    assert.strictEqual(B.pushSample(null, 5).length, 1);
    const a = [1, 2]; B.pushSample(a, 3); assert.strictEqual(a.length, 2);
});

check('sparkPath: масштаб, разрывы на null, мало точек', () => {
    assert.deepStrictEqual(B.sparkPath([], 100, 20), { d: '', min: null, max: null });
    assert.deepStrictEqual(B.sparkPath([5], 100, 20), { d: '', min: null, max: null });
    const full = []; for (let i = 0; i < 120; i++) full.push(i % 2);
    const p = B.sparkPath(full, 119, 21);
    assert.ok(p.d.indexOf('M0 20') === 0 || p.d.indexOf('M0 1') === 0);
    assert.strictEqual(p.min, 0); assert.strictEqual(p.max, 1);
    const g = B.sparkPath([1, 2, null, 3, 4], 119, 21);
    assert.strictEqual((g.d.match(/M/g) || []).length, 2);
    const flat = B.sparkPath([7, 7, 7], 119, 21);
    assert.ok(flat.d.length > 0 && flat.min === 7 && flat.max === 7);
});

check('точечные случаи: готовность, да/нет/н/д, формат кода причины, не было', () => {
    assert.strictEqual(B.state({ ready: false, link: {} }), 'notready');
    const nb = JSON.parse(JSON.stringify(N)); delete nb.connected;
    assert.strictEqual(get(B.cards(nb)[0], 'Подключён'), 'н/д');
    const off = JSON.parse(JSON.stringify(N)); off.connected = false;
    assert.strictEqual(get(B.cards(off)[0], 'Начало текущей сессии'), 'н/д');
    const c10 = JSON.parse(JSON.stringify(N)); c10.link.last_reason_code = 10; c10.link.last_reason = 'x';
    assert.strictEqual(get(B.cards(c10)[0], 'Причина последнего разрыва'), 'x (0x0A)');
    c10.link.last_reason_code = 255;
    assert.strictEqual(get(B.cards(c10)[0], 'Причина последнего разрыва'), 'x (0xFF)');
    const nobad = JSON.parse(JSON.stringify(N)); nobad.rate.last_bad_age_s = null;
    assert.strictEqual(get(B.cards(nobad)[1], 'Последний плохой пакет'), 'не было');
    assert.strictEqual(get(B.cards(N)[1], 'Интервал между пакетами, среднее за сессию'), '2004.2');
    assert.strictEqual(get(B.cards(N)[1], 'Интервал между пакетами, среднее за 10 мин'), '2001.5');
});

check('histBars: отрицательные и нечисловые счётчики не входят в сумму', () => {
    const h = B.histBars({ rssi_hist: [-5, 10, 'x'] });
    assert.deepStrictEqual(h.map(x => x.count), [0, 10, 0]);
    assert.strictEqual(h[1].frac, 1);
});

check('badge: граница ровно 24 часа включена, момент разрыва сейчас включён', () => {
    const now = 2000000;
    const mk = (d) => [{ idx: 0, dur_s: 1, end_utc: now - d }];
    assert.strictEqual(B.badge(N, mk(86400), now).text, '1');
    assert.strictEqual(B.badge(N, mk(86401), now).text, '0');
    assert.strictEqual(B.badge(N, mk(0), now).text, '1');
    assert.strictEqual(B.badge(N, mk(-1), now).text, '0');
});

check('sparkPath: плоский ряд без NaN, короткий ряд прижат вправо', () => {
    const flat = B.sparkPath([7, 7, 7], 119, 21);
    assert.ok(flat.d.indexOf('NaN') < 0);
    const p = B.sparkPath([1, 2, 3], 119, 21);
    assert.ok(p.d.indexOf('M117 ') === 0, p.d);
    assert.ok(p.d.indexOf('L119 ') > 0);
});

const NOSCAN = py("m.ble('noscan')");
const scanRows = (b) => B.cards(b)[4].rows;
const rowOf = (rows, k) => rows.find(x => x.k === k);

check('сканирование: подключён и сканов 0 -> одна строка вместо нулей', () => {
    const r = scanRows(NOSCAN);
    assert.strictEqual(r.length, 9);
    assert.strictEqual(r[0].k, 'Сканирование');
    assert.strictEqual(r[0].v, 'не выполнялось: плата подключена к сохранённому адресу');
    assert.strictEqual(rowOf(r, 'Сканирований с загрузки'), undefined);
    for (const k of ['Реклам в последнем скане', 'Найдено приборов в последнем скане', 'От начала скана до обнаружения прибора, мс']) assert.strictEqual(rowOf(r, k).v, 'н/д (скан не шёл)', k);
    assert.ok(!r.some(x => x.v === '0'));
});

check('сканирование: параметры и адрес остаются, имя н/д с пояснением', () => {
    const r = scanRows(NOSCAN);
    assert.strictEqual(rowOf(r, 'Интервал сканирования, мс').v, '100.0');
    assert.strictEqual(rowOf(r, 'Окно сканирования, мс').v, '99.0');
    assert.strictEqual(rowOf(r, 'Адрес прибора (маска)').v, 'AA:BB:**:**:CC:DD');
    const nm = rowOf(r, 'Имя прибора');
    assert.strictEqual(nm.v, 'н/д');
    assert.strictEqual(nm.hint, 'имя не принимается в рекламе');
});

check('сканирование: сканы были или не подключён -> прежний вид', () => {
    const r = scanRows(N);
    assert.strictEqual(r.length, 9);
    assert.strictEqual(rowOf(r, 'Сканирований с загрузки').v, '120');
    assert.strictEqual(rowOf(r, 'Сканирование'), undefined);
    assert.strictEqual(rowOf(r, 'Имя прибора').v, 'AtomFast');
    assert.strictEqual(rowOf(r, 'Реклам в последнем скане').v, '14');
    assert.strictEqual(rowOf(r, 'Найдено приборов в последнем скане').v, '1');
    assert.strictEqual(rowOf(r, 'От начала скана до обнаружения прибора, мс').v, '1250');
    assert.strictEqual(rowOf(r, 'Имя прибора').hint, 'из рекламы прибора');
    const off = JSON.parse(JSON.stringify(NOSCAN)); off.connected = false;
    const o = scanRows(off);
    assert.strictEqual(rowOf(o, 'Сканирований с загрузки').v, '0');
    assert.strictEqual(rowOf(o, 'Найдено приборов в последнем скане').v, '0');
    const un = JSON.parse(JSON.stringify(NOSCAN)); delete un.connected;
    assert.strictEqual(rowOf(scanRows(un), 'Сканирований с загрузки').v, '0');
    const nm = JSON.parse(JSON.stringify(NOSCAN)); nm.scan.scans = null;
    assert.strictEqual(rowOf(scanRows(nm), 'Сканирований с загрузки').v, 'н/д');
    const run = JSON.parse(JSON.stringify(NOSCAN)); run.scan.scans = 1; run.scan.adv_last = 5;
    assert.strictEqual(rowOf(scanRows(run), 'Реклам в последнем скане').v, '5');
});

check('сканирование: у каждой строки есть подсказка', () => {
    for (const b of [N, NOSCAN]) for (const row of scanRows(b)) assert.ok(row.hint && row.hint.length > 5, row.k);
});
console.log('TESTS: ' + passed + '/' + total);
process.exit(passed === total ? 0 : 1);
