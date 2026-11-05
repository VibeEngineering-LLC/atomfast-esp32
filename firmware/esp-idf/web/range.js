(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(); } else { root.AFRG = factory(); } })(this, function () { 'use strict';

var KEY = 'afh.range.v1';
var MIN_SPAN = 60;
var FUTURE_SLACK = 60;
var TS_MIN = 1000000000;
var TS_MAX = 4294967295;
var UNITS = { min: 60, h: 3600, d: 86400 };
var UNIT_NAMES = { min: 'мин', h: 'ч', d: 'сут' };
var DEFAULT_LIMITS = { raw: 4000, agg: 21000, rate: 0.5, windowS: 60 };

function p2(n) { return n < 10 ? '0' + n : '' + n; }
function isTs(v) { return typeof v === 'number' && Math.floor(v) === v && v >= TS_MIN && v <= TS_MAX; }

function toInput(ts) {
  var d = new Date(ts * 1000);
  return d.getFullYear() + '-' + p2(d.getMonth() + 1) + '-' + p2(d.getDate()) + 'T' + p2(d.getHours()) + ':' + p2(d.getMinutes());
}

function localTs(y, mo, d, h, mi, s) {
  var dt = new Date(y, mo - 1, d, h, mi, s, 0);
  if (dt.getFullYear() !== y || dt.getMonth() !== mo - 1 || dt.getDate() !== d || dt.getHours() !== h || dt.getMinutes() !== mi) { return null; }
  return Math.floor(dt.getTime() / 1000);
}

function fromInput(s) {
  var m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})(?::(\d{2}))?$/.exec(typeof s === 'string' ? s : '');
  if (!m) { return null; }
  var sec = m[6] === undefined ? 0 : +m[6];
  if (sec > 59) { return null; }
  var ts = localTs(+m[1], +m[2], +m[3], +m[4], +m[5], sec);
  return ts !== null && isTs(ts) ? ts : null;
}

function label(from, to) {
  var f = function (ts) {
    var d = new Date(ts * 1000);
    return p2(d.getDate()) + '.' + p2(d.getMonth() + 1) + ' ' + p2(d.getHours()) + ':' + p2(d.getMinutes());
  };
  return f(from) + ' — ' + f(to);
}

function validate(r, ctx) {
  ctx = ctx || {};
  if (!r || !isTs(r.from) || !isTs(r.to)) { return { ok: false, error: 'Укажите начало и конец интервала' }; }
  if (r.from >= r.to) { return { ok: false, error: 'Начало должно быть раньше конца' }; }
  if (r.to - r.from < MIN_SPAN) { return { ok: false, error: 'Интервал короче одной минуты' }; }
  if (isTs(ctx.now) && r.to > ctx.now + FUTURE_SLACK) { return { ok: false, error: 'Конец интервала не может быть в будущем' }; }
  if (isTs(ctx.first) && r.from < ctx.first) { return { ok: false, error: 'Начало раньше начала истории платы (' + label(ctx.first, ctx.first).split(' — ')[0] + ')' }; }
  return { ok: true, error: '' };
}

function midnight(ts, dayShift) {
  var d = new Date(ts * 1000);
  return Math.floor(new Date(d.getFullYear(), d.getMonth(), d.getDate() + dayShift, 0, 0, 0, 0).getTime() / 1000);
}

function presets(now) {
  var d = new Date(now * 1000);
  var back = (d.getDay() + 6) % 7;
  var monday = midnight(now, -back);
  return [
    { id: 'yesterday', label: 'Вчера', from: midnight(now, -1), to: midnight(now, 0) },
    { id: 'today', label: 'Сегодня', from: midnight(now, 0), to: now },
    { id: 'last3d', label: 'Последние 3 дня', from: now - 3 * 86400, to: now },
    { id: 'last7d', label: 'Последние 7 дней', from: now - 7 * 86400, to: now },
    { id: 'prevweek', label: 'Прошлая неделя', from: midnight(monday, -7), to: monday }
  ];
}

function lastN(n, unit, now) {
  if (typeof n !== 'number' || Math.floor(n) !== n || n < 1 || n > 99999) { return null; }
  if (!Object.prototype.hasOwnProperty.call(UNITS, unit)) { return null; }
  var from = now - n * UNITS[unit];
  return isTs(from) && isTs(now) ? { from: from, to: now } : null;
}

function spanLabel(sec) {
  if (sec >= 172800) { return (Math.round(sec / 8640) / 10) + ' сут'; }
  if (sec >= 3600) { return (Math.round(sec / 360) / 10) + ' ч'; }
  return Math.round(sec / 60) + ' мин';
}

function periodOf(kind, o) { return kind === 'raw' ? 1 / o.rate : o.windowS; }

function estimate(from, to, periodSec) { return Math.ceil((to - from) / periodSec); }

function maxSpan(kind, opts) {
  var o = Object.assign({}, DEFAULT_LIMITS, opts || {});
  return Math.floor((kind === 'raw' ? o.raw : o.agg) * periodOf(kind, o));
}

function rowWarning(kind, from, to, opts) {
  var o = Object.assign({}, DEFAULT_LIMITS, opts || {});
  var limit = kind === 'raw' ? o.raw : o.agg;
  var rows = estimate(from, to, periodOf(kind, o));
  if (rows <= limit) { return { warn: false, rows: rows, limit: limit, suggestAgg: false, text: '' }; }
  if (kind === 'raw') {
    var aggRows = estimate(from, to, o.windowS);
    var okAgg = aggRows <= o.agg;
    return {
      warn: true, rows: rows, limit: limit, suggestAgg: okAgg,
      text: 'Сырых строк ожидается около ' + rows + ', предел ' + limit + ' (интервал до ' + spanLabel(maxSpan('raw', opts)) + '): ' + (okAgg ? 'выберите «Агрегаты».' : 'сузьте интервал.')
    };
  }
  return { warn: true, rows: rows, limit: limit, suggestAgg: false, text: 'Агрегатов ожидается около ' + rows + ', предел ' + limit + ' (интервал до ' + spanLabel(maxSpan('agg', opts)) + '): сузьте интервал.' };
}

function query(kind, from, to, limit) {
  if (['raw', 'agg', 'env'].indexOf(kind) === -1 || !isTs(from) || !isTs(to) || from >= to) { return ''; }
  return 'kind=' + kind + '&from=' + from + '&to=' + to + '&limit=' + (Math.floor(limit) > 0 ? Math.floor(limit) : 1);
}

function parseOne(v) {
  if (typeof v !== 'string') { return null; }
  if (/^\d{1,10}$/.test(v)) { return isTs(+v) ? +v : null; }
  var m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})(?::(\d{2}))?(Z|[+-]\d{2}:\d{2})?$/.exec(v);
  if (!m) { return null; }
  if (m[7] === undefined) {
    var sec = m[6] === undefined ? 0 : +m[6];
    if (sec > 59) { return null; }
    var l = localTs(+m[1], +m[2], +m[3], +m[4], +m[5], sec);
    return l !== null && isTs(l) ? l : null;
  }
  var t = Date.parse(v);
  if (!isFinite(t)) { return null; }
  t = Math.floor(t / 1000);
  return isTs(t) ? t : null;
}

function parseUrl(search) {
  var q = null;
  try { q = new URLSearchParams(typeof search === 'string' ? search : ''); } catch (e) { return null; }
  var f = parseOne(q.get('from'));
  var t = parseOne(q.get('to'));
  if (f === null || t === null || f >= t) { return null; }
  return { from: f, to: t };
}

function linkFor(base, r) { return base + '?from=' + r.from + '&to=' + r.to; }

function selectionToRange(t0, t1, minSpan) {
  if (!isFinite(t0) || !isFinite(t1)) { return null; }
  var from = Math.floor(Math.min(t0, t1));
  var to = Math.ceil(Math.max(t0, t1));
  if (to - from < (minSpan || 10)) { return null; }
  return isTs(from) && isTs(to) ? { from: from, to: to } : null;
}

function emptyStore() { return { v: 1, last: null, active: false, lastN: null }; }

function parseStore(text) {
  var st = emptyStore();
  var o = null;
  try { o = JSON.parse(text); } catch (e) { return st; }
  if (!o || typeof o !== 'object' || o.v !== 1) { return st; }
  if (o.last && isTs(o.last.from) && isTs(o.last.to) && o.last.from < o.last.to) { st.last = { from: o.last.from, to: o.last.to }; }
  st.active = o.active === true && st.last !== null;
  var n = o.lastN;
  if (n && typeof n.n === 'number' && Math.floor(n.n) === n.n && n.n >= 1 && n.n <= 99999 && Object.prototype.hasOwnProperty.call(UNITS, n.unit)) { st.lastN = { n: n.n, unit: n.unit }; }
  return st;
}

function serializeStore(st) { return JSON.stringify(st); }

function inHistory(r, first, last) {
  if (isTs(first) && r.to <= first) { return false; }
  if (isTs(last) && r.from > last) { return false; }
  return true;
}

function restoreRange(st, ctx) {
  ctx = ctx || {};
  if (!st || !st.active || !st.last) { return null; }
  var v = validate(st.last, { now: ctx.now });
  if (!v.ok) { return null; }
  return inHistory(st.last, ctx.first, ctx.last) ? { from: st.last.from, to: st.last.to } : null;
}

function autoState(range, prevAuto) {
  if (range) { return { checked: false, disabled: true, title: 'В режиме интервала автообновление выключено; вернитесь к окну Час, 6 ч, Сутки или Вся' }; }
  return { checked: prevAuto !== false, disabled: false, title: 'Автообновление' };
}

function createLatest() {
  var id = 0;
  var ctrl = null;
  return {
    begin: function () {
      if (ctrl) { ctrl.abort(); }
      id++;
      ctrl = new AbortController();
      return { id: id, signal: ctrl.signal };
    },
    isCurrent: function (t) { return !!t && t.id === id; },
    cancel: function () {
      if (ctrl) { ctrl.abort(); }
      id++;
      ctrl = null;
    }
  };
}

return {
  KEY: KEY, MIN_SPAN: MIN_SPAN, FUTURE_SLACK: FUTURE_SLACK, UNITS: UNITS, UNIT_NAMES: UNIT_NAMES, DEFAULT_LIMITS: DEFAULT_LIMITS,
  isTs: isTs, toInput: toInput, fromInput: fromInput, label: label, validate: validate, presets: presets, lastN: lastN,
  spanLabel: spanLabel, estimate: estimate, maxSpan: maxSpan, rowWarning: rowWarning, query: query, parseUrl: parseUrl,
  linkFor: linkFor, selectionToRange: selectionToRange, emptyStore: emptyStore, parseStore: parseStore,
  serializeStore: serializeStore, inHistory: inHistory, restoreRange: restoreRange, autoState: autoState, createLatest: createLatest
};
});
