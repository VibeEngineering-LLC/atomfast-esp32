var msF = AFMS.defaults();
var msData = null;
var msTimeOk = null;
var msSel = null;
var msRows = [];
var msRowsKey = '';
var msFld = 'cps';
var msVw = 'line';
var msBusy = false;
var msDelPending = null;
var msPlotW = 0;

function msEl(id) { return document.getElementById(id); }

function msSay(id, text, cls) {
  var el = msEl(id);
  if (!el) { return; }
  el.className = 'result' + (cls ? ' ' + cls : '');
  el.textContent = text;
}

function msKv(host, rows) {
  host.textContent = '';
  rows.forEach(function (r) {
    var d = document.createElement('div');
    d.className = 'kv';
    var k = document.createElement('span');
    k.className = 'k';
    k.textContent = r[0];
    var v = document.createElement('span');
    v.className = 'v';
    v.textContent = r[1];
    d.appendChild(k);
    d.appendChild(v);
    host.appendChild(d);
  });
}

function msSetSeg(host, attr, val) {
  Array.prototype.forEach.call(host.querySelectorAll('button'), function (b) { b.setAttribute('aria-pressed', b.getAttribute(attr) === val ? 'true' : 'false'); });
}

// POST с токеном: без токена запрос НЕ уходит
async function msPost(url, body) {
  var tok = '';
  try { var t = await fetchJSON('/api/csrf-token'); tok = t && typeof t.token === 'string' ? t.token : ''; } catch (e) { tok = ''; }
  if (!tok) { return { ok: false, status: 403, data: null, noToken: true }; }
  var ctrl = new AbortController();
  var to = setTimeout(function () { ctrl.abort(); }, 15000);
  try {
    var r = await fetch(url, { method: 'POST', credentials: 'same-origin', cache: 'no-store', headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': tok }, body: JSON.stringify(body || {}), signal: ctrl.signal });
    var data = null;
    try { data = await r.json(); } catch (e) { data = null; }
    return { ok: r.ok && !!data && data.ok === true, status: r.status, data: data };
  } finally { clearTimeout(to); }
}

function msErrOf(r) {
  if (r.noToken) { return 'не получен токен защиты (CSRF): запрос не отправлен, обновите страницу'; }
  return AFMS.errorText(r.status, r.data && typeof r.data.err === 'string' ? r.data.err : '');
}

// ---- форма ----
var MS_MAP = [['msName', 'name'], ['msH', 'h'], ['msM', 'm'], ['msS', 's'], ['msEps', 'eps'], ['msTmin', 'tmin'], ['msTmax', 'tmax'], ['msN', 'n'],
              ['msPh', 'ph'], ['msPm', 'pm'], ['msPs', 'ps'], ['msLost', 'lostMax'], ['msStart', 'startMode'], ['msAt', 'startAt'], ['msDelay', 'delayMin'],
              ['msPrefix', 'prefix'], ['msTpl', 'template']];
var MS_CHK = [['msRetry', 'retry'], ['msFile', 'fileStore'], ['msNoOver', 'noOverwrite']];

function msFillForm() {
  MS_MAP.forEach(function (p) { msEl(p[0]).value = String(msF[p[1]]); });
  MS_CHK.forEach(function (p) { msEl(p[0]).checked = !!msF[p[1]]; });
}

function msReadForm() {
  MS_MAP.forEach(function (p) { msF[p[1]] = msEl(p[0]).value; });
  MS_CHK.forEach(function (p) { msF[p[1]] = msEl(p[0]).checked; });
}

function msVisible() {
  var single = msF.mode === 'single', sigma = single && msF.stopMode === 'sigma';
  var show = { single: single, time: !sigma, sigma: sigma, series: msF.mode === 'series', gap: !single, at: msF.startMode === 'at', delay: msF.startMode === 'delay', file: !!msF.fileStore };
  Array.prototype.forEach.call(msEl('msForm').querySelectorAll('[data-ms]'), function (el) { el.hidden = !show[el.getAttribute('data-ms')]; });
}

var MS_NOTES = {
  single: 'Одно измерение: остановка по времени или по достижении заданной погрешности счёта.',
  series: 'Серия: заданное число измерений одинаковой длительности, между ними пауза. Остановка только по времени.',
  journal: ''
};

function msRenderForm() {
  msSetSeg(msEl('msMode'), 'data-mode', msF.mode);
  msSetSeg(msEl('msStop'), 'data-stop', msF.stopMode);
  msEl('msModeNote').textContent = MS_NOTES[msF.mode] || '';
  msVisible();
  var tot = AFMS.totalSec(msF);
  msEl('msTotal').textContent = tot === null ? (msF.mode === 'endless' ? 'Общая длительность: не ограничена.' : (msF.mode === 'single' && msF.stopMode === 'sigma' ? 'Длительность зависит от скорости счёта (предел времени задан выше).' : 'Общая длительность: н/д.')) : 'Общая длительность: ' + AFMS.dur(tot) + (msF.mode === 'series' ? ' (N × (время + пауза))' : '') + '.';
  var act = msData ? AFMS.activeRun(msData) : null;
  msEl('msGo').disabled = msBusy || !!act;
  msEl('msTimeHint').hidden = msTimeOk !== false;
  var opt = msEl('msStart').options;
  for (var i = 0; i < opt.length; i++) { if (opt[i].value !== 'now') { opt[i].disabled = msTimeOk === false; } }
  if (msF.mode === 'single' && msF.stopMode === 'sigma') {
    var e = AFMS.numOf(msF.eps);
    var est = act && act.live ? AFMS.sigmaEstimate(e, act.live.cps, AFMS.numOf(msF.tmax)) : null;
    msEl('msSigmaHint').textContent = 'N = 10000 / ε²: импульсов до цели ' + (e >= 0.1 && e <= 50 ? Math.ceil(1e4 / (e * e) - 1e-9) : AFMS.ND) + (est ? '; при текущем счёте ≈ ' + AFMS.dur(est.sec) + (est.tooLong ? ' (дольше предела времени)' : '') : '; время зависит от скорости счёта.');
  }
  mbApplyMode();
}

async function msGo() {
  msReadForm();
  var b = AFMS.buildBody(msF, { timeOk: msTimeOk });
  var ul = msEl('msErrs');
  ul.textContent = '';
  if (!b.ok) {
    b.errors.forEach(function (e) { var li = document.createElement('li'); li.textContent = e.text; ul.appendChild(li); });
    msSay('msResult', 'Исправьте поля: запрос не отправлен.', 'bad');
    return;
  }
  msBusy = true;
  msRenderForm();
  msSay('msResult', 'запускаю…', '');
  try {
    var r = await msPost('/api/meas/start', b.body);
    if (r.ok) { msSel = r.data && typeof r.data.id === 'number' ? r.data.id : msSel; msSay('msResult', 'Измерение запущено (запуск ' + r.data.id + ').', 'ok'); }
    else { msSay('msResult', msErrOf(r), 'bad'); }
  } catch (e) { msSay('msResult', 'нет связи с платой', 'bad'); }
  finally { msBusy = false; await msRefresh(); }
}

async function msAct(act) {
  var a = msData ? AFMS.activeRun(msData) : null;
  if (!a || msBusy) { return; }
  msBusy = true;
  try {
    var r = await msPost('/api/meas/' + act, { id: a.run.id });
    msSay('msLiveResult', r.ok ? 'Команда выполнена.' : msErrOf(r), r.ok ? 'ok' : 'bad');
  } catch (e) { msSay('msLiveResult', 'нет связи с платой', 'bad'); }
  finally { msBusy = false; await msRefresh(); }
}

// ---- текущее (e16f, T-39: табло в meas-board-ui.js) ----
function msRenderLive() { mbRender(); }
// ---- результаты ----
function msRunLabel(x) {
  var r = x.run;
  return '№' + r.id + (r.name ? ' ' + r.name : '') + ' · ' + AFMS.label(AFMS.MODES, r.mode) + ' · ' + AFMS.label(AFMS.STATES, r.state) + ' · ' + (typeof r.done === 'number' ? r.done : AFMS.ND) + (typeof r.n === 'number' && r.n > 0 ? '/' + r.n : '');
}

function msSelRun() {
  if (!msData || !msData.runs.length) { return null; }
  for (var i = 0; i < msData.runs.length; i++) { if (msData.runs[i].run.id === msSel) { return msData.runs[i]; } }
  var a = AFMS.activeRun(msData);
  var pick = a || msData.runs[msData.runs.length - 1];
  msSel = pick.run.id;
  return pick;
}

function msRenderSelect() {
  var sel = msEl('msRun');
  var runs = msData ? msData.runs : [];
  var key = runs.map(msRunLabel).join('|') + '#' + msSel;
  if (sel.getAttribute('data-key') === key) { return; }
  sel.setAttribute('data-key', key);
  sel.textContent = '';
  runs.forEach(function (x) { var o = document.createElement('option'); o.value = String(x.run.id); o.textContent = msRunLabel(x); sel.appendChild(o); });
  if (msSel !== null) { sel.value = String(msSel); }
  sel.disabled = runs.length === 0;
}

function msRenderTable() {
  var t = msEl('msTbl');
  var cap = t.querySelector('caption');
  t.textContent = '';
  if (cap) { t.appendChild(cap); }
  var rows = AFMS.resultRows(msRows, (msSelRun() || {run: {}}).run.lost_max);
  var head = document.createElement('tr');
  ['№', 'Старт', 'Длит.', 'Импульсов', 'CPS, имп/с', 'мкЗв/ч', 'Статус'].forEach(function (h) { var th = document.createElement('th'); th.scope = 'col'; th.textContent = h; head.appendChild(th); });
  t.appendChild(head);
  var shown = rows.slice(-200);
  shown.forEach(function (r) {
    var tr = document.createElement('tr');
    if (!r.valid) { tr.className = 'msbad'; }
    [r.i, r.start, r.t, r.counts, r.cps, r.usv, r.status].forEach(function (c) { var td = document.createElement('td'); td.textContent = c; tr.appendChild(td); });
    t.appendChild(tr);
  });
  msEl('msTblNote').textContent = rows.length === 0 ? 'Результатов пока нет (н/д).' : (rows.length > shown.length ? 'Показаны последние ' + shown.length + ' из ' + rows.length + '; полный набор в CSV.' : '');
}

async function msLoadRows(x) {
  var key = x.run.id + ':' + (typeof x.run.done === 'number' ? x.run.done : -1) + ':' + x.run.state;
  if (key === msRowsKey) { return; }
  try {
    var j = await fetchJSON('/api/meas/results?id=' + x.run.id + '&limit=1000');
    msRows = j && j.ok === true && Array.isArray(j.rows) ? j.rows : [];
    msRowsKey = key;
  } catch (e) { msRows = []; msRowsKey = ''; }
}

function msRenderResults() {
  var x = msSelRun();
  msRenderSelect();
  var del = msEl('msDel');
  var csv = msEl('msCsv');
  if (!x) {
    msEl('msRunInfo').textContent = 'Запусков нет (н/д).';
    msKv(msEl('msSum'), []);
    msRows = []; msRowsKey = '';
    del.disabled = true;
    csv.setAttribute('aria-disabled', 'true');
    csv.removeAttribute('href');
  } else {
    var r = x.run;
    var why = AFMS.label(AFMS.REASONS, r.reason);
    msEl('msRunInfo').textContent = AFMS.label(AFMS.MODES, r.mode) + ', ' + AFMS.label(AFMS.STATES, r.state) + (why && why !== AFMS.ND ? ', остановка ' + why : '') + '.';
    msKv(msEl('msSum'), AFMS.sumRows(x.sum, r.mode));
    del.disabled = msBusy || !(r.state === 'done' || r.state === 'aborted' || r.state === 'error');
    csv.removeAttribute('aria-disabled');
    csv.setAttribute('href', '/api/meas/results.csv?id=' + r.id);
  }
  msRenderTable();
  msRenderPlot();
  mbRenderResultsInfo();
}

function msDelOpen() {
  var x = msSelRun();
  if (!x || msEl('msDel').disabled) { return; }
  msDelPending = x.run.id;
  msEl('msDlgText').textContent = 'Будет удалён запуск №' + x.run.id + (x.run.name ? ' «' + x.run.name + '»' : '') + ' со всеми результатами (' + (typeof x.run.done === 'number' ? x.run.done : AFMS.ND) + ' изм.). Это нельзя отменить.';
  msEl('msDlg').hidden = false;
  msEl('msDlgCancel').focus();
}

function msDelClose() { msDelPending = null; msEl('msDlg').hidden = true; }

async function msDelDo() {
  var id = msDelPending;
  if (id === null || msBusy) { return; }
  msBusy = true;
  try {
    var r = await msPost('/api/meas/delete', { id: id });
    msSay('msDelResult', r.ok ? 'Запуск удалён.' : msErrOf(r), r.ok ? 'ok' : 'bad');
    if (r.ok) { msDelClose(); msSel = null; msRowsKey = ''; }
  } catch (e) { msSay('msDelResult', 'нет связи с платой', 'bad'); }
  finally { msBusy = false; await msRefresh(); }
}

// ---- график ----
var MS_NS = 'http://www.w3.org/2000/svg';

function msSvg(name, attrs, text) {
  var e = document.createElementNS(MS_NS, name);
  Object.keys(attrs || {}).forEach(function (k) { e.setAttribute(k, String(attrs[k])); });
  if (text !== undefined) { e.textContent = text; }
  return e;
}

function msTick(v) { return String(Number(v.toPrecision(6))); }

function msAxes(svg, m, byTime, xLabel, xFree) {
  m.yt.forEach(function (t) {
    svg.appendChild(msSvg('line', { class: 'msgrid', x1: m.padL, x2: m.w - m.padR, y1: t.py, y2: t.py }));
    svg.appendChild(msSvg('text', { class: 'msax', x: m.padL - 6, y: t.py + 4, 'text-anchor': 'end' }, msTick(t.v)));
  });
  m.xt.forEach(function (t) {
    if (!byTime && !xFree && Math.floor(t.v) !== t.v) { return; }
    svg.appendChild(msSvg('line', { class: 'msgrid', x1: t.px, x2: t.px, y1: m.padT, y2: m.h - m.padB }));
    svg.appendChild(msSvg('text', { class: 'msax', x: t.px, y: m.h - m.padB + 16, 'text-anchor': 'middle' }, byTime ? AFMS.fmtUtc(t.v).slice(6) : msTick(t.v)));
  });
  svg.appendChild(msSvg('line', { class: 'msaxis', x1: m.padL, x2: m.w - m.padR, y1: m.h - m.padB, y2: m.h - m.padB }));
  svg.appendChild(msSvg('line', { class: 'msaxis', x1: m.padL, x2: m.padL, y1: m.padT, y2: m.h - m.padB }));
  if (xLabel) { svg.appendChild(msSvg('text', { class: 'msax', x: m.w - m.padR, y: m.h - 2, 'text-anchor': 'end' }, xLabel)); }
}

function msDrawLine(svg, m, st, label) {
  if (st && st.mean !== null && st.sd !== null) {
    var yb = function (a, b) { return Math.min(m.sy(a), m.sy(b)); };
    svg.appendChild(msSvg('rect', { class: 'msb2', x: m.padL, width: m.w - m.padL - m.padR, y: yb(st.mean + 2 * st.sd, st.mean - 2 * st.sd), height: Math.abs(m.sy(st.mean + 2 * st.sd) - m.sy(st.mean - 2 * st.sd)) }));
    svg.appendChild(msSvg('rect', { class: 'msb1', x: m.padL, width: m.w - m.padL - m.padR, y: yb(st.mean + st.sd, st.mean - st.sd), height: Math.abs(m.sy(st.mean + st.sd) - m.sy(st.mean - st.sd)) }));
  }
  if (st && st.mean !== null) { svg.appendChild(msSvg('line', { class: 'msmean', x1: m.padL, x2: m.w - m.padR, y1: m.sy(st.mean), y2: m.sy(st.mean) })); }
  if (st && st.mean !== null && st.poisson !== null) {
    [st.poisson, -st.poisson].forEach(function (d) { svg.appendChild(msSvg('line', { class: 'mspois', x1: m.padL, x2: m.w - m.padR, y1: m.sy(st.mean + d), y2: m.sy(st.mean + d) })); });
  }
  var path = m.pts.filter(function (q) { return q.p.valid; }).map(function (q, i) { return (i ? 'L' : 'M') + q.px.toFixed(1) + ' ' + q.py.toFixed(1); }).join(' ');
  if (path) { svg.appendChild(msSvg('path', { class: 'msline', d: path })); }
  var small = m.pts.length > 200;
  m.pts.forEach(function (q) {
    if (q.p.s > 0) { svg.appendChild(msSvg('line', { class: 'mserr', x1: q.px, x2: q.px, y1: q.lo, y2: q.hi })); }
    var c = msSvg('circle', { class: q.p.valid ? 'mspt' : 'mspt msinv', cx: q.px, cy: q.py, r: small ? 2 : 3.5 });
    c.appendChild(msSvg('title', {}, AFMS.pointTitle(q.p, label)));
    svg.appendChild(c);
  });
}

function msDrawHist(svg, w, h, vals, st) {
  var hb = AFMS.histBins(vals);
  if (hb.n === 0) { return false; }
  var max = hb.bins.reduce(function (a, b) { return Math.max(a, b.count); }, 0);
  var m = AFMS.plotModel([{ x: hb.bins[0].lo, y: 0, s: 0 }, { x: hb.bins[hb.bins.length - 1].hi, y: max, s: 0 }], w, h, { zero: true, noErr: true });
  msAxes(svg, m, false, '', true);
  hb.bins.forEach(function (b) {
    var r = msSvg('rect', { class: 'mshist', x: m.sx(b.lo) + 0.5, width: Math.max(1, m.sx(b.hi) - m.sx(b.lo) - 1), y: m.sy(b.count), height: Math.max(0, m.sy(0) - m.sy(b.count)) });
    r.appendChild(msSvg('title', {}, msTick(b.lo) + ' ... ' + msTick(b.hi) + ': ' + b.count));
    svg.appendChild(r);
  });
  if (st && st.mean !== null) { svg.appendChild(msSvg('line', { class: 'msmean', x1: m.sx(st.mean), x2: m.sx(st.mean), y1: m.padT, y2: m.h - m.padB })); }
  return true;
}

function msStatText(st) {
  if (!st || st.n === 0) { return 'Действительных измерений нет (н/д).'; }
  var sem = st.sd !== null ? st.sd / Math.sqrt(st.n) : undefined;
  var t = 'Среднее ' + AFMS.fv(st.mean, sem) + '; СКО ' + (st.sd === null ? AFMS.ND : AFMS.fnum(st.sd)) + '; статистический разброс (Пуассон) ' + (st.poisson === null ? AFMS.ND : AFMS.fnum(st.poisson));
  if (st.sd !== null && st.poisson !== null && st.poisson > 0) { t += '; СКО / Пуассон = ' + AFMS.fnum(st.sd / st.poisson); }
  return t + '; по ' + st.n + ' действительным (недействительные показаны пустыми кружками и в статистику не входят).';
}

function msRenderPlot() {
  var host = msEl('msPlot');
  host.textContent = '';
  var note = msEl('msPlotNote');
  var stEl = msEl('msStat');
  stEl.textContent = '';
  var x = msSelRun();
  if (!x || msRows.length === 0) { note.textContent = 'Нет результатов для графика (н/д).'; return; }
  var w = Math.max(280, Math.floor(host.clientWidth || 600));
  var h = w < 420 ? 220 : 270;
  var svg = msSvg('svg', { width: w, height: h, viewBox: '0 0 ' + w + ' ' + h, role: 'img', 'aria-label': 'График результатов запуска ' + x.run.id });
  var ser = msVw === 'dose' ? { byTime: false, label: 'Накопленная доза, мкЗв', pts: AFMS.doseSeries(msRows) } : AFMS.points(msRows, msFld, msVw === 'time' ? 'time' : 'line');
  var st = msVw === 'dose' ? null : AFMS.stats(ser.pts, msFld);
  var ok = ser.pts.length > 0;
  var nm = ser.label + (msVw === 'hist' ? ': распределение по измерениям' : '');
  if (ok && msVw === 'hist') { ok = msDrawHist(svg, w, h, ser.pts.filter(function (p) { return p.valid; }).map(function (p) { return p.y; }), st); }
  else if (ok) {
    var m = AFMS.plotModel(ser.pts, w, h, { bands: st && st.sd !== null ? st : null, zero: msFld === 'counts' });
    msAxes(svg, m, ser.byTime, msVw === 'dose' ? 'номер измерения' : (ser.byTime ? 'время старта' : 'номер измерения'));
    msDrawLine(svg, m, st, ser.label);
  }
  note.textContent = ok ? nm + (msVw === 'time' && !ser.byTime ? '. Время старта неизвестно (нет синхронизации времени): ось по номеру.' : '') : 'Нет подходящих данных (н/д).';
  if (ok) { host.appendChild(svg); stEl.textContent = msVw === 'dose' ? 'Накопленная доза за ' + ser.pts.length + ' действительных измерений: ' + AFMS.fnum(ser.pts[ser.pts.length - 1].y) + ' мкЗв (мощность дозы × длительность; без погрешности).' : msStatText(st); }
}

// ---- опрос и бейдж ----
async function msRefresh() {
  var j = null;
  try { j = await fetchJSON('/api/meas'); } catch (e) { j = null; }
  msData = AFMS.parse(j);
  msTimeOk = msData ? msData.timeOk : null;
  msRenderBadge();
  if (PAGE !== 'measure') { return; }
  var x = msSelRun();
  if (x) { await msLoadRows(x); }
  msRenderForm();
  msRenderLive();
  msRenderResults();
}

function msRenderBadge() {
  var a = document.querySelector('nav.tabs a[data-nav="measure"]');
  if (!a) { return; }
  var b = document.getElementById('msBadge');
  if (!b) { b = document.createElement('span'); b.id = 'msBadge'; b.className = 'badge ok msbadge'; b.hidden = true; a.appendChild(b); }
  var bd = AFMS.badge(msData);
  b.hidden = !bd.active;
  b.textContent = bd.text;
  b.title = bd.title;
  b.className = 'badge msbadge ' + (bd.paused ? 'warn' : 'ok');
  if (bd.active) { a.setAttribute('aria-label', 'Измерения: ' + bd.title); } else { a.removeAttribute('aria-label'); }
}

function initMeasBadge() {
  msRefresh();
  setInterval(function () { if (document.visibilityState === 'visible') { msRefresh(); } }, PAGE === 'measure' ? 2500 : 10000);
}

function initMeasure() {
  msFillForm();
  msEl('msMode').addEventListener('click', function (e) { var b = e.target.closest('button[data-mode]'); if (b) { msReadForm(); msF.mode = b.getAttribute('data-mode'); if (msF.mode !== 'single') { msF.stopMode = 'time'; } msRenderForm(); } });
  msEl('msStop').addEventListener('click', function (e) { var b = e.target.closest('button[data-stop]'); if (b) { msReadForm(); msF.stopMode = b.getAttribute('data-stop'); msRenderForm(); } });
  msEl('msForm').addEventListener('input', function () { msReadForm(); msRenderForm(); });
  msEl('msForm').addEventListener('change', function () { msReadForm(); msRenderForm(); });
  msEl('msGo').addEventListener('click', msGo);
  msEl('msPause').addEventListener('click', function () { msAct('pause'); });
  msEl('msResume').addEventListener('click', function () { msAct('resume'); });
  msEl('msStopBtn').addEventListener('click', function () { msAct('stop'); });
  msEl('msRun').addEventListener('change', function () { msSel = Number(msEl('msRun').value); msRowsKey = ''; msRefresh(); });
  msEl('msDel').addEventListener('click', msDelOpen);
  msEl('msDlgOk').addEventListener('click', msDelDo);
  msEl('msDlgCancel').addEventListener('click', msDelClose);
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape' && msDelPending !== null) { msDelClose(); } });
  msEl('msFld').addEventListener('click', function (e) { var b = e.target.closest('button[data-fld]'); if (b) { msFld = b.getAttribute('data-fld'); msSetSeg(msEl('msFld'), 'data-fld', msFld); msRenderPlot(); } });
  msEl('msView').addEventListener('click', function (e) { var b = e.target.closest('button[data-vw]'); if (b) { msVw = b.getAttribute('data-vw'); msSetSeg(msEl('msView'), 'data-vw', msVw); msRenderPlot(); } });
  var rz = null;
  window.addEventListener('resize', function () { if (rz) { cancelAnimationFrame(rz); } rz = requestAnimationFrame(function () { rz = null; msRenderPlot(); }); });
  mbInit();
  msRenderForm();
  msRenderLive();
  msRenderResults();
}

