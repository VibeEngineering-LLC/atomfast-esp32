(function (root, factory) {
  if (typeof module === 'object' && module.exports) { module.exports = factory(require('./uncertainty.js'), require('./axes.js')); } else { root.AFMS = factory(root.AFUN, root.AFAX); }
})(this, function (AFUN, AFAX) { 'use strict';

var ND = 'н/д';
var MODES = { single: 'Разовое измерение', series: 'Серия', endless: 'Непрерывное', journal: 'Из журнала' };
var STATES = { wait: 'ожидание старта', running: 'идёт', paused: 'на паузе', done: 'завершено', aborted: 'прервано', error: 'ошибка' };
var PHASES = { none: '', waitstart: 'ожидание старта', measuring: 'измерение', gap: 'пауза между измерениями' };
var REASONS = { none: '', time: 'по времени', sigma: 'по погрешности', limit: 'лимит времени', manual: 'остановлено вручную', error: 'потери выше допуска' };
var API_ERRORS = {
  bad_body: 'неверное тело запроса', json: 'неверный JSON', unknown_key: 'неизвестное поле в запросе', bad_value: 'недопустимое значение параметра',
  sigma_only_single: 'остановка по погрешности возможна только в разовом измерении', not_found: 'такого запуска нет', time_not_synced: 'время на плате не синхронизировано (SNTP)',
  busy: 'уже идёт другое измерение или заняты все 8 мест', bad_state: 'действие не подходит текущему состоянию запуска', no_mem: 'не хватило памяти на плате'
};

function isNum(v) { return typeof v === 'number' && isFinite(v); }
function isObj(v) { return v !== null && typeof v === 'object' && !Array.isArray(v); }
function label(map, k) { return Object.prototype.hasOwnProperty.call(map, k) ? map[k] : ND; }

function utf8Len(s) {
  var n = 0;
  for (var i = 0; i < s.length; i++) {
    var c = s.charCodeAt(i);
    if (c < 0x80) { n += 1; } else if (c < 0x800) { n += 2; } else if (c >= 0xD800 && c < 0xDC00) { n += 4; i++; } else { n += 3; }
  }
  return n;
}

function errorText(status, err) {
  var t = Object.prototype.hasOwnProperty.call(API_ERRORS, err) ? API_ERRORS[err] : '';
  var head = status === 400 ? 'запрос отклонён' : (status === 404 ? 'не найдено' : (status === 409 ? 'конфликт состояния' : (status === 503 ? 'плата не может выполнить' : (status === 403 ? 'отказ: недействительный токен, обновите страницу' : 'ошибка ' + status))));
  if (status === 403) { return head; }
  return head + (t ? ': ' + t : (err ? ' (' + err + ')' : ''));
}

function defaults() {
  return { mode: 'single', name: '', h: 0, m: 10, s: 0, n: 5, ph: 0, pm: 0, ps: 0, retry: true, lostMax: 2, stopMode: 'time', eps: 1, tmin: 10, tmax: 3600,
           startMode: 'now', startAt: '', delayMin: 5, fileStore: false, prefix: 'AtomFast', template: '{prefix}_{n:000}', noOverwrite: true };
}


function numOf(v) {
  if (typeof v === 'number') { return v; }
  if (typeof v !== 'string' || v.trim() === '') { return NaN; }
  return Number(v.trim().replace(',', '.'));
}

function intIn(v, lo, hi) { var n = numOf(v); return isNum(n) && Math.floor(n) === n && n >= lo && n <= hi ? n : null; }

function hms(h, m, s) {
  var a = intIn(h, 0, 1000), b = intIn(m, 0, 59), c = intIn(s, 0, 59);
  return a === null || b === null || c === null ? NaN : a * 3600 + b * 60 + c;
}

var TEMPLATE_LABELS = ['{prefix}', '{n:000}', '{date}', '{time}'];

function templateOk(t) {
  if (typeof t !== 'string' || t.length < 1 || t.length > 48) { return false; }
  var rest = t;
  TEMPLATE_LABELS.forEach(function (l) { rest = rest.split(l).join(''); });
  return /^[A-Za-z0-9._ -]*$/.test(rest);
}

function nameOk(s) { return typeof s === 'string' && utf8Len(s) <= 32 && !/[\\\u0000-\u001f\u007f]/.test(s); }

// Параметры формы -> тело POST /api/meas/start. Стоп по погрешности только в разовом; в серии и непрерывном поля sigma не отправляются.
function buildBody(f, opts) {
  var o = opts || {};
  var err = [];
  var b = {};
  var single = f.mode === 'single', endless = f.mode === 'endless';
  if (!nameOk(f.name)) { err.push({ field: 'name', text: 'название: до 32 байт, без обратной косой черты и управляющих символов' }); }
  b.name = f.name;
  if (endless) { b.endless = true; } else if (single) { b.n = 1; } else {
    var n = intIn(f.n, 2, 1000);
    if (n === null) { err.push({ field: 'n', text: 'число измерений: целое от 2 до 1000' }); } else { b.n = n; }
  }
  var sigma = single && f.stopMode === 'sigma';
  if (!sigma) {
    var t = hms(f.h, f.m, f.s);
    if (!isNum(t) || t < 10 || t > 3600000) { err.push({ field: 't', text: 'время измерения: от 10 с до 1000 ч (чч/мин 0-59/с 0-59)' }); } else { b.t_s = t; }
  }
  if (!single) {
    var p = hms(f.ph, f.pm, f.ps);
    if (!isNum(p) || p > 3600000) { err.push({ field: 'pause', text: 'пауза: от 0 до 1000 ч' }); } else { b.pause_s = p; }
  }
  b.retry_invalid = !!f.retry;
  var lm = intIn(f.lostMax, 0, 60);
  if (lm === null) { err.push({ field: 'lostMax', text: 'допустимо потерянных пакетов: 0-60' }); } else { b.lost_max = lm; }
  b.stop_mode = sigma ? 'sigma' : 'time';
  if (sigma) {
    var e = numOf(f.eps), a = intIn(f.tmin, 1, 3600000), z = intIn(f.tmax, 1, 3600000);
    if (!isNum(e) || e < 0.1 || e > 50) { err.push({ field: 'eps', text: 'погрешность: от 0,1 до 50 %' }); } else { b.eps_pct = e; }
    if (a === null) { err.push({ field: 'tmin', text: 'минимальное время: целое, с' }); } else { b.t_min_s = a; }
    if (z === null) { err.push({ field: 'tmax', text: 'предел времени обязателен: целое, с' }); } else { b.t_max_s = z; }
    if (a !== null && z !== null && a > z) { err.push({ field: 'tmax', text: 'предел времени не может быть меньше минимального' }); }
  }
  b.start_mode = f.startMode === 'at' || f.startMode === 'delay' ? f.startMode : 'now';
  if (b.start_mode === 'at') {
    if (o.timeOk === false) { err.push({ field: 'startAt', text: 'время на плате не синхронизировано: «В указанное время» недоступно' }); }
    var ms = typeof f.startAt === 'string' && f.startAt !== '' ? new Date(f.startAt).getTime() : NaN;
    if (!isNum(ms)) { err.push({ field: 'startAt', text: 'укажите дату и время старта' }); } else { b.start_at = Math.floor(ms / 1000); }
  }
  if (b.start_mode === 'delay') {
    var d = intIn(f.delayMin, 1, 10080);
    if (d === null) { err.push({ field: 'delayMin', text: 'задержка: от 1 до 10080 минут' }); } else { b.delay_s = d * 60; }
  }
  b.file_store = !!f.fileStore;
  if (f.fileStore) {
    if (!templateOk(f.template)) { err.push({ field: 'template', text: 'шаблон: метки {prefix} {n:000} {date} {time}, символы A-Za-z0-9._- пробел, до 48' }); }
    if (typeof f.prefix !== 'string' || !/^[A-Za-z0-9._ -]{1,32}$/.test(f.prefix)) { err.push({ field: 'prefix', text: 'префикс: A-Za-z0-9._- пробел, до 32' }); }
    b.name_template = f.template; b.prefix = f.prefix; b.no_overwrite = !!f.noOverwrite;
  }
  return err.length ? { ok: false, errors: err } : { ok: true, body: b };
}

// Общее время N x (T + пауза); бесконечное и стоп по погрешности: null
function totalSec(f) {
  if (f.mode === 'endless' || (f.mode === 'single' && f.stopMode === 'sigma')) { return null; }
  var t = hms(f.h, f.m, f.s);
  if (!isNum(t)) { return null; }
  if (f.mode === 'single') { return t; }
  var n = intIn(f.n, 2, 1000);
  if (n === null) { return null; }
  var p = hms(f.ph, f.pm, f.ps);
  return isNum(p) ? n * (t + p) : null;
}

// Оценка времени до погрешности eps (%) при текущем CPS: N = 1e4/eps^2, t = N/CPS
function sigmaEstimate(epsPct, cps, tmax) {
  if (!isNum(epsPct) || epsPct < 0.1 || epsPct > 50 || !isNum(cps) || !(cps > 0)) { return null; }
  var n = Math.ceil(1e4 / (epsPct * epsPct) - 1e-9);
  var t = n / cps;
  return { n: n, sec: t, tooLong: isNum(tmax) && t > tmax };
}


function parse(j) {
  if (!isObj(j) || j.ok !== true || !Array.isArray(j.runs)) { return null; }
  var runs = j.runs.filter(function (x) { return isObj(x) && isObj(x.run) && isNum(x.run.id); }).map(function (x) {
    return { run: x.run, sum: isObj(x.sum) ? x.sum : {}, live: isObj(x.live) ? x.live : null };
  });
  return { timeOk: j.time_ok === true, nowUtc: isNum(j.now_utc) ? j.now_utc : null, activeId: isNum(j.active_id) ? j.active_id : null, limits: isObj(j.limits) ? j.limits : {}, runs: runs };
}

function activeRun(p) {
  if (!p || p.activeId === null) { return null; }
  for (var i = 0; i < p.runs.length; i++) { if (p.runs[i].run.id === p.activeId) { return p.runs[i]; } }
  return null;
}

function dur(sec) {
  if (!isNum(sec) || sec < 0) { return ND; }
  var s = Math.round(sec), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60;
  return h > 0 ? h + ' ч ' + m + ' мин' : (m > 0 ? m + ' мин ' + (r < 10 ? '0' : '') + r + ' с' : r + ' с');
}

// Доля выполненного: серия по числу измерений и времени текущего; бесконечное и стоп по погрешности: null
function progress(run, live) {
  if (!run || !isNum(run.n) || run.n <= 0 || !isNum(run.t_s) || run.t_s <= 0 || run.stop_mode === 'sigma') { return null; }
  var cur = live && isNum(live.elapsed_ms) && live.phase === 'measuring' ? Math.min(1, live.elapsed_ms / 1000 / run.t_s) : 0;
  var done = isNum(run.done) ? run.done : 0;
  return Math.max(0, Math.min(1, (done + cur) / run.n));
}

// Бейдж вкладки: идущее (или ожидающее, или на паузе) измерение
function badge(p) {
  var a = activeRun(p);
  if (!a) { return { text: '', active: false, title: '' }; }
  var r = a.run;
  var text = isNum(r.n) && r.n > 1 ? (isNum(r.done) ? r.done : 0) + '/' + r.n : '●';
  var st = label(STATES, r.state);
  return { text: text, active: true, paused: r.state === 'paused', title: 'Измерение ' + st };
}

function fv(value, sigma) {
  if (!isNum(value)) { return ND; }
  return AFUN.format(value, isNum(sigma) ? sigma : undefined, { fallbackDec: 2, k: (typeof AFSET !== 'undefined' ? AFSET.k() : 1) }).text;
}

// число без погрешности (она не определена): три значащих разряда, без «± н/д»
function fnum(v) {
  if (!isNum(v)) { return ND; }
  return v === 0 ? '0' : String(Number(v.toPrecision(3))); // WEB-03: ровно три значащие цифры
}

function liveRows(run, live) {
  if (!run || !live) { return []; }
  var rows = [
    ['Измерение', (isNum(live.i) ? live.i : ND) + (isNum(run.n) && run.n > 0 ? ' из ' + run.n : '')],
    ['Фаза', label(PHASES, live.phase)],
    ['Прошло', dur(isNum(live.elapsed_ms) ? live.elapsed_ms / 1000 : NaN)],
    ['Осталось', run.stop_mode === 'sigma' ? ND : dur(isNum(live.remaining_ms) ? live.remaining_ms / 1000 : NaN)],
    ['Импульсов', isNum(live.counts) ? String(live.counts) : ND],
    ['Принято пакетов, потеряно', (isNum(live.npk) ? live.npk : ND) + ', ' + (isNum(live.lost) ? live.lost : ND)],
    ['CPS, имп/с', fv(live.cps, live.cps_sigma)],
    ['Относительная погрешность счёта, %', isNum(live.eps_pct) ? AFUN.format(live.eps_pct, live.eps_pct / 10).text.split(' ')[0] : ND]
  ];
  if (run.stop_mode === 'sigma') {
    rows.push(['Нужно импульсов до цели', isNum(live.n_target) ? String(live.n_target) : ND]);
    rows.push(['Оценка до цели', isNum(live.eta_s) ? dur(live.eta_s) : ND]);
  }
  return rows;
}

// χ²/ndf: ≈1 разброс статистический, много больше 1 есть нестатистические вариации
function chi2Text(c) {
  if (!isNum(c)) { return ND; }
  var v = AFUN.format(c, c / 10).value;
  if (c < 0.5) { return v + ': разброс меньше статистического (мало измерений или завышена погрешность)'; }
  if (c <= 1.5) { return v + ': ≈1, разброс статистический'; }
  if (c <= 3) { return v + ': заметно больше 1, возможны нестатистические вариации'; }
  return v + ': ≫1, есть нестатистические вариации';
}

function sumRows(sum, mode) {
  if (!isObj(sum) || !isNum(sum.n_valid)) { return []; }
  var nv = sum.n_valid;
  var mean = nv > 1 ? fv(sum.mean_cps, sum.sem_cps) : fv(sum.mean_cps, sum.pooled_sigma);
  var rate = AFUN.rateStat(sum.mean_usvh, sum.counts);
  var dose = AFUN.doseStat(sum.dose_usv, sum.counts);
  return [
    ['Действительных измерений', nv + (isNum(sum.n_invalid) && sum.n_invalid > 0 ? ' (недействительных ' + sum.n_invalid + ', в статистику не входят)' : '')],
    [nv > 1 ? 'Среднее CPS ± погрешность среднего, имп/с' : 'CPS, имп/с', mean],
    ['СКО CPS, имп/с', isNum(sum.sd_cps) ? fv(sum.sd_cps, sum.sd_cps / Math.sqrt(2 * Math.max(1, nv - 1))) : ND],
    ['Размах CPS, имп/с', isNum(sum.min_cps) && isNum(sum.max_cps) ? fnum(sum.min_cps) + ' ... ' + fnum(sum.max_cps) : ND],
    ['Суммарный CPS по всем импульсам, имп/с', fv(sum.pooled_cps, sum.pooled_sigma)],
    ['χ²/ndf', chi2Text(sum.chi2_ndf)],
    ['Средняя мощность дозы, мкЗв/ч', isNum(sum.mean_usvh) ? fv(sum.mean_usvh, rate.sigma) : ND],
    ['Накопленная доза за измерения, мкЗв', isNum(sum.dose_usv) ? fv(sum.dose_usv, dose.sigma) : ND]
  ];
}

function pad2(n) { return (n < 10 ? '0' : '') + n; }

function fmtUtc(sec) {
  if (!isNum(sec) || sec <= 0) { return ND; }
  var d = new Date(sec * 1000);
  return pad2(d.getDate()) + '.' + pad2(d.getMonth() + 1) + ' ' + pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds());
}

function rowStatus(r, lostMax) {   // T-56: подписи причин по-русски (потери считаются по приёму пакетов, допуск — параметр замера)
  var s = r.valid === true ? 'действительно' : 'недействительно: потеряно пакетов ' + (isNum(r.lost) ? r.lost : ND) + (isNum(lostMax) ? ' при допуске ' + lostMax : '');
  if (isNum(r.attempt) && r.attempt > 1) { s += ', попытка ' + r.attempt + ' из 3'; }
  if (r.partial === true) { s += ', неполное окно'; }
  if (r.goal_missed === true) { s += ', цель не достигнута'; }
  if (r.stop_reason && r.stop_reason !== 'time' && r.stop_reason !== 'none') { s += ' · ' + label(REASONS, r.stop_reason); }
  return s;
}

function resultRows(rows, lostMax) {
  return (Array.isArray(rows) ? rows : []).filter(isObj).map(function (r) {
    return { i: isNum(r.i) ? String(r.i) : ND, start: fmtUtc(r.t0_utc), t: dur(isNum(r.t_ms) ? r.t_ms / 1000 : NaN), counts: isNum(r.counts) ? String(r.counts) : ND,
             cps: fv(r.cps, r.cps_sigma), usv: fv(r.usv_h, r.usv_h_sigma), status: rowStatus(r, lostMax), valid: r.valid === true };
  });
}

var FIELDS = {
  cps: { label: 'CPS, имп/с', y: function (r) { return r.cps; }, s: function (r) { return r.cps_sigma; } },
  usv_h: { label: 'Мощность дозы, мкЗв/ч', y: function (r) { return r.usv_h; }, s: function (r) { return r.usv_h_sigma; } },
  counts: { label: 'Импульсы за измерение', y: function (r) { return r.counts; }, s: function (r) { return isNum(r.counts) ? AFUN.pulsesStat(r.counts).sigma : NaN; } }
};

// Точки графика: ось X по номеру или по времени начала (если время известно у всех строк)
function points(rows, field, xmode) {
  var f = Object.prototype.hasOwnProperty.call(FIELDS, field) ? FIELDS[field] : FIELDS.cps;
  var list = (Array.isArray(rows) ? rows : []).filter(function (r) { return isObj(r) && isNum(f.y(r)) && isNum(r.i); });
  var byTime = xmode === 'time' && list.length > 0 && list.every(function (r) { return isNum(r.t0_utc) && r.t0_utc > 0; });
  return { byTime: byTime, label: f.label, pts: list.map(function (r) {
    return { x: byTime ? r.t0_utc : r.i, i: r.i, y: f.y(r), s: isNum(f.s(r)) ? f.s(r) : 0, valid: r.valid === true, attempt: isNum(r.attempt) ? r.attempt : 1, t: isNum(r.t_ms) ? r.t_ms / 1000 : NaN };
  }) };
}

// Среднее, СКО (n-1) по действительным; ожидаемый пуассоновский разброс: sqrt(среднее*T)/T для CPS, иначе средняя sigma точек
function stats(pts, field) {
  var v = pts.filter(function (p) { return p.valid; });
  var n = v.length;
  if (n === 0) { return { n: 0, mean: null, sd: null, poisson: null }; }
  var mean = v.reduce(function (a, p) { return a + p.y; }, 0) / n;
  var sd = n > 1 ? Math.sqrt(v.reduce(function (a, p) { return a + (p.y - mean) * (p.y - mean); }, 0) / (n - 1)) : null;
  var poisson;
  if (field === 'cps' || field === undefined) {
    var tb = v.reduce(function (a, p) { return a + (isNum(p.t) ? p.t : 0); }, 0) / n;
    poisson = tb > 0 && mean >= 0 ? Math.sqrt(mean * tb) / tb : null;
  } else { poisson = v.reduce(function (a, p) { return a + p.s; }, 0) / n; }
  return { n: n, mean: mean, sd: sd, poisson: poisson };
}

function histBins(values, nBins) {
  var v = (Array.isArray(values) ? values : []).filter(isNum).slice().sort(function (a, b) { return a - b; });
  var n = v.length;
  if (n === 0) { return { n: 0, bins: [] }; }
  var lo = v[0], hi = v[n - 1];
  if (hi === lo) { return { n: n, bins: [{ lo: lo, hi: hi, count: n }] }; }
  var k = intIn(nBins, 1, 100);
  if (k === null) {
    var q = function (p) { var x = (n - 1) * p, a = Math.floor(x); return v[a] + (v[Math.min(n - 1, a + 1)] - v[a]) * (x - a); };
    var w = 2 * (q(0.75) - q(0.25)) * Math.pow(n, -1 / 3);
    k = w > 0 ? Math.ceil((hi - lo) / w) : Math.ceil(Math.sqrt(n));
    k = Math.max(1, Math.min(50, k));
  }
  var bins = [];
  for (var i = 0; i < k; i++) { bins.push({ lo: lo + (hi - lo) * i / k, hi: lo + (hi - lo) * (i + 1) / k, count: 0 }); }
  v.forEach(function (x) { var b = Math.min(k - 1, Math.floor((x - lo) / (hi - lo) * k)); bins[b].count++; });
  return { n: n, bins: bins };
}

// Накопленная доза серии, мкЗв: сумма средней мощности дозы строки на её длительность, только действительные
function doseSeries(rows) {
  var acc = 0;
  var out = [];
  (Array.isArray(rows) ? rows : []).forEach(function (r) {
    if (!isObj(r) || r.valid !== true || !isNum(r.usv_h) || !isNum(r.t_ms) || !isNum(r.i)) { return; }
    acc += r.usv_h * r.t_ms / 3600000;
    out.push({ x: r.i, i: r.i, y: acc, s: 0, valid: true, attempt: 1, t: r.t_ms / 1000 });
  });
  return out;
}

// Модель графика: границы по данным (с запасом на sigma), метки осей, пересчёт в пиксели
function plotModel(pts, w, h, o) {
  var op = o || {};
  var padL = 52, padR = 10, padT = 10, padB = 28;
  var P = (Array.isArray(pts) ? pts : []).filter(function (p) { return isObj(p) && isNum(p.x) && isNum(p.y); });
  if (P.length === 0) { return { empty: true, pts: [], xt: [], yt: [] }; }
  var x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
  P.forEach(function (p) {
    x0 = Math.min(x0, p.x); x1 = Math.max(x1, p.x);
    y0 = Math.min(y0, p.y - (op.noErr ? 0 : p.s)); y1 = Math.max(y1, p.y + (op.noErr ? 0 : p.s));
  });
  if (op.bands && isNum(op.bands.mean) && isNum(op.bands.sd)) { y0 = Math.min(y0, op.bands.mean - 2 * op.bands.sd); y1 = Math.max(y1, op.bands.mean + 2 * op.bands.sd); }
  if (op.zero && y0 > 0) { y0 = 0; }
  if (x1 === x0) { x0 -= 1; x1 += 1; }
  if (y1 === y0) { var d = Math.abs(y0) * 0.1 || 1; y0 -= d; y1 += d; }
  var padY = (y1 - y0) * 0.05;
  if (!(op.zero && y0 === 0)) { y0 -= padY; }
  y1 += padY;
  var iw = Math.max(1, w - padL - padR), ih = Math.max(1, h - padT - padB);
  var sx = function (v) { return padL + (v - x0) / (x1 - x0) * iw; };
  var sy = function (v) { return padT + ih - (v - y0) / (y1 - y0) * ih; };
  var xt = AFAX.niceTicks(x0, x1, Math.max(2, Math.floor(iw / 70)));
  var yt = AFAX.niceTicks(y0, y1, Math.max(2, Math.floor(ih / 40)));
  return { empty: false, w: w, h: h, padL: padL, padR: padR, padT: padT, padB: padB, x0: x0, x1: x1, y0: y0, y1: y1, sx: sx, sy: sy,
           xt: xt.map(function (v) { return { v: v, px: sx(v) }; }), yt: yt.map(function (v) { return { v: v, py: sy(v) }; }),
           pts: P.map(function (p) { return { px: sx(p.x), py: sy(p.y), lo: sy(p.y - p.s), hi: sy(p.y + p.s), p: p }; }) };
}

function pointTitle(p, label) {
  return 'Измерение ' + p.i + ': ' + label + ' ' + (p.s > 0 ? fv(p.y, p.s) : fnum(p.y)) + (p.valid ? '' : ' (недействительно)') + (p.attempt > 1 ? ', попытка ' + p.attempt : '');
}




return { ND: ND, MODES: MODES
, STATES: STATES, PHASES: PHASES, REASONS: REASONS, API_ERRORS: API_ERRORS, isNum: isNum, label: label, utf8Len: utf8Len, errorText: errorText, defaults: defaults, numOf: numOf, intIn: intIn, hms: hms, templateOk: templateOk, nameOk: nameOk, buildBody: buildBody, totalSec: totalSec, sigmaEstimate: sigmaEstimate, parse: parse, activeRun: activeRun, dur: dur, progress: progress, badge: badge, fv: fv, liveRows: liveRows, chi2Text: chi2Text, sumRows: sumRows, fnum: fnum, fmtUtc: fmtUtc, rowStatus: rowStatus, resultRows: resultRows, FIELDS: FIELDS, points: points, stats: stats, histBins: histBins, doseSeries: doseSeries, plotModel: plotModel, pointTitle: pointTitle
};
});
