(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(require('./uncertainty.js')); } else { root.AFMB = factory(root.AFUN); } })(this, function (AFUN) { 'use strict';
var ND = 'н/д';
function isNum(v) { return typeof v === 'number' && isFinite(v); }
function pad2(n) { return (n < 10 ? '0' : '') + n; }
function clock(sec) {
  if (!isNum(sec) || sec < 0) { return '--:--'; }
  var s = Math.floor(sec);
  var m = Math.floor(s / 60);
  var r = s % 60;
  return pad2(m) + ':' + pad2(r);
}
function toLocalInput(utc) {
  if (!isNum(utc)) { return ''; }
  var d = new Date(utc * 1000);
  return d.getFullYear() + '-' + pad2(d.getMonth() + 1) + '-' + pad2(d.getDate()) + 'T' + pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds());
}
function fromLocalInput(str) {
  if (typeof str !== 'string') { return null; }
  var m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})(?::(\d{2}))?$/.exec(str);
  if (!m) { return null; }
  var y = parseInt(m[1], 10);
  var mo = parseInt(m[2], 10);
  var d = parseInt(m[3], 10);
  var h = parseInt(m[4], 10);
  var mi = parseInt(m[5], 10);
  var s = m[6] ? parseInt(m[6], 10) : 0;
  var dt = new Date(y, mo - 1, d, h, mi, s);
  if (dt.getMonth() !== mo - 1 || dt.getDate() !== d) { return null; }
  return Math.floor(dt.getTime() / 1000);
}
function journalRange(view, nowUtc) {
  if (!view || typeof view !== 'object') { return null; }
  var t0 = view.t0;
  var t1 = view.t1;
  if (!isNum(t0) || !isNum(t1) || t1 <= t0) { return null; }
  var from = Math.floor(t0);
  var to = Math.ceil(t1);
  if (isNum(nowUtc) && to > nowUtc) { to = Math.floor(nowUtc); }
  if (to <= from) { return null; }
  return { from: from, to: to };
}
function num3(v) {
  return AFUN.rateEps(v, undefined).value;
}
function rateParts(usv, eps) {
  var r = AFUN.rateEps(usv, isNum(eps) ? eps : undefined);
  return { value: r.value, eps: r.eps && r.eps !== '—' ? '± ' + r.eps + ' %' : '' };
}
function boardModel(run, live, nowUtc) {
  if (!run || typeof run !== 'object') { return { active: false }; }
  var active = run.state === 'wait' || run.state === 'running' || run.state === 'paused';
  var modeText = ND;
  if (run.mode === 'single') { modeText = 'Разовое'; }
  else if (run.mode === 'series') { modeText = 'Серия'; }
  else if (run.mode === 'journal') { modeText = 'Из журнала'; }
  var windowText = '';
  if (run.mode === 'series' && isNum(run.n) && run.n > 0) {
    var idx = (live && isNum(live.i)) ? live.i : (isNum(run.cur_i) ? run.cur_i : 1);
    windowText = 'окно ' + idx + ' из ' + run.n;
  }
  var title = 'Замер ';
  if (typeof run.name === 'string' && run.name !== '') { title += '«' + run.name + '» '; }
  title += '· ' + modeText;
  if (windowText) { title += ', ' + windowText; }
  var startText = '';
  if (isNum(run.start_utc) && run.start_utc > 0) {
    var sd = new Date(run.start_utc * 1000);
    startText = 'старт ' + pad2(sd.getHours()) + ':' + pad2(sd.getMinutes()) + ':' + pad2(sd.getSeconds());
  }
  var rate = rateParts(live ? live.usv_h : null, live ? live.eps_pct : null);
  var elapsedSec = (live && isNum(live.elapsed_ms)) ? live.elapsed_ms / 1000 : 0;
  var totalSec = null;
  if (run.stop_mode !== 'sigma' && isNum(run.t_s) && run.t_s > 0) { totalSec = run.t_s; }
  var pct = null;
  if (totalSec !== null) { pct = Math.max(0, Math.min(100, Math.round(elapsedSec / totalSec * 100))); }
  var elapsedText = clock(elapsedSec);
  var totalText = (totalSec === null) ? '--:--' : clock(totalSec);
  var leftText = '';
  if (live && isNum(live.remaining_ms) && run.stop_mode !== 'sigma') { leftText = 'осталось ' + clock(live.remaining_ms / 1000); }
  var counts = (live && isNum(live.counts)) ? String(live.counts) : '—';
  var cpsText = (live && isNum(live.cps)) ? num3(live.cps) : '—';
  var lostVal = (live && isNum(live.lost)) ? live.lost : 0;
  var lostMax = isNum(run.lost_max) ? run.lost_max : 0;
  var lostText = lostVal + ' / ' + lostMax;
  var lostBad = !!(live && isNum(live.lost) && live.lost > lostMax);
  var paused = run.state === 'paused';
  var phaseText = '';
  if (live && live.phase === 'gap') { phaseText = 'пауза между окнами'; }
  else if (live && live.phase === 'waitstart') { phaseText = 'ожидание старта'; }
  else if (run.state === 'wait') { phaseText = 'ожидание старта'; }
  else if (run.state === 'paused') { phaseText = 'на паузе'; }
  var canPause = run.state === 'running' || run.state === 'wait';
  var canResume = run.state === 'paused';
  var canStop = active;
  return {
    active: active,
    modeText: modeText,
    windowText: windowText,
    title: title,
    startText: startText,
    rate: rate,
    elapsedSec: elapsedSec,
    totalSec: totalSec,
    pct: pct,
    elapsedText: elapsedText,
    totalText: totalText,
    leftText: leftText,
    counts: counts,
    cpsText: cpsText,
    lostText: lostText,
    lostBad: lostBad,
    paused: paused,
    phaseText: phaseText,
    canPause: canPause,
    canResume: canResume,
    canStop: canStop
  };
}
function journalErrText(code) {
  if (typeof code !== 'string' || code === '') { return 'Нет связи с платой'; }
  if (code === 'bad_range') { return 'Неверный интервал: «с» должно быть раньше «по», длина не больше 36 часов'; }
  if (code === 'no_data') { return 'В журнале за этот интервал нет записей'; }
  if (code === 'too_long') { return 'Интервал слишком длинный: принято больше 65535 пакетов'; }
  if (code === 'store_busy') { return 'Журнал занят, повторите через несколько секунд'; }
  if (code === 'no_mem') { return 'Не хватило памяти на плате'; }
  if (code === 'busy') { return 'Нет свободного места под результат: все 8 мест заняты идущими замерами'; }
  if (code === 'bad_value' || code === 'bad_query' || code === 'json' || code === 'unknown_key') { return 'Запрос отклонён платой (неверные значения)'; }
  return 'Ошибка: ' + String(code);
}
function journalRows(res, gaps, from, to) {
  function fmt(u) {
    var d = new Date(u * 1000);
    return pad2(d.getDate()) + '.' + pad2(d.getMonth() + 1) + ' ' + pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds());
  }
  var rows = [];
  rows.push(['Интервал', fmt(from) + ' — ' + fmt(to)]);
  rows.push(['Длительность', clock(to - from)]);
  var re = AFUN.rateEps(res.usv_h, isNum(res.eps_pct) ? res.eps_pct : undefined);
  var usvStr = re.value;
  if (re.eps && re.eps !== '—') { usvStr += ' ± ' + re.eps + ' %'; }
  rows.push(['Средняя мощность дозы, мкЗв/ч', usvStr]);
  rows.push(['Погрешность счёта, %', isNum(res.eps_pct) ? AFUN.rateEps(1, res.eps_pct).eps : '—']);
  rows.push(['Импульсов', isNum(res.counts) ? String(res.counts) : ND]);
  rows.push(['Принято пакетов', isNum(res.npk) ? String(res.npk) : ND]);
  var lostStr = isNum(res.lost) ? String(res.lost) : ND;
  if (isNum(gaps) && gaps > 0) { lostStr += ' (разрывов: ' + gaps + ')'; }
  rows.push(['Пропущено пакетов', lostStr]);
  return rows;
}
function prevValues(rows, maxN) {
  if (!rows || typeof rows !== 'object' || !rows.length) { return []; }
  if (!isNum(maxN) || maxN <= 0) { maxN = 8; }
  var out = [];
  for (var i = rows.length - 1; i >= 0 && out.length < maxN; i--) {
    var r = rows[i];
    if (r && r.valid === true) { out.push(AFUN.rateEps(r.usv_h, isNum(r.eps_pct) ? r.eps_pct : undefined).value); }
  }
  return out.reverse();   // от старых к новым
}
function sparkPath(vals, w, h, pad) {
  if (!isNum(w) || !isNum(h)) { return ''; }
  if (!isNum(pad) || pad < 0) { pad = 2; }
  var arr = [];
  if (vals && typeof vals === 'object' && vals.length) {
    for (var i = 0; i < vals.length; i++) {
      if (isNum(vals[i])) { arr.push(vals[i]); }
    }
  }
  if (arr.length === 0) { return ''; }
  var min = arr[0];
  var max = arr[0];
  for (var j = 1; j < arr.length; j++) {
    if (arr[j] < min) { min = arr[j]; }
    if (arr[j] > max) { max = arr[j]; }
  }
  var range = max - min;
  var midY = (h / 2).toFixed(1);
  var parts = [];
  for (var k = 0; k < arr.length; k++) {
    var x;
    if (arr.length === 1) { x = w / 2; }
    else { x = pad + (w - 2 * pad) * (k / (arr.length - 1)); }
    var y;
    if (range === 0) { y = h / 2; }
    else { y = (h - pad) - (h - 2 * pad) * ((arr[k] - min) / range); }
    var cmd = (k === 0) ? 'M' : 'L';
    parts.push(cmd + x.toFixed(1) + ' ' + y.toFixed(1));
  }
  return parts.join(' ');
}
function runLabel(run) {
  if (!run || typeof run !== 'object') { return ''; }
  var s = 'замер ' + run.id;   // e16g (T-42): «№» в моноширинном шрифте читался как «%»
  if (run.name) { s += ' ' + run.name; }
  if (run.mode === 'journal') { s += ' · из журнала'; }
  return s;
}
return { ND: ND, isNum: isNum, clock: clock, pad2: pad2, toLocalInput: toLocalInput, fromLocalInput: fromLocalInput, journalRange: journalRange, num3: num3, rateParts: rateParts, boardModel: boardModel, journalErrText: journalErrText, journalRows: journalRows, prevValues: prevValues, sparkPath: sparkPath, runLabel: runLabel };
});
