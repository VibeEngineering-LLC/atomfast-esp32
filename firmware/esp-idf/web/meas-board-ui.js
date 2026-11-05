var mbJrn = null;
var mbSparkAt = 0;

function mbRender() {
  var a = msData ? AFMS.activeRun(msData) : null;
  if (!a) {
    msEl('mbBoard').hidden = true;
    msEl('msNewCard').hidden = false;
    return;
  }
  var m = AFMB.boardModel(a.run, a.live, msData.nowUtc);
  msEl('mbBoard').hidden = false;
  msEl('msNewCard').hidden = true;

  msEl('mbTitle').textContent = m.title;
  msEl('mbSub').textContent = [m.startText, m.phaseText].filter(Boolean).join(' · ');
  msEl('mbRate').textContent = m.rate.value;
  msEl('mbEps').textContent = m.rate.eps;
  msEl('mbElapsed').textContent = m.elapsedText;
  msEl('mbTotal').textContent = m.totalText;
  msEl('mbLeft').textContent = m.leftText;
  msEl('mbCounts').textContent = m.counts;
  msEl('mbCps').textContent = m.cpsText;
  msEl('mbLost').textContent = m.lostText;
  msEl('mbLost').parentNode.classList.toggle('bad', m.lostBad);

  // прогресс
  if (m.pct === null) {
    msEl('msBar').hidden = true;
  } else {
    msEl('msBar').hidden = false;
    msEl('msBarFill').style.width = m.pct + '%';
    msEl('msBar').setAttribute('aria-valuenow', String(m.pct));
  }

  // кнопки
  msEl('msPause').hidden = !m.canPause;
  msEl('msResume').hidden = !m.canResume;
  msEl('msStopBtn').disabled = msBusy || !m.canStop;
  msEl('msPause').disabled = msBusy;
  msEl('msResume').disabled = msBusy;

  // предыдущие окна серии
  if (a.run && a.run.mode === 'series') {
    var pv = AFMB.prevValues(msRows, 8);
    if (pv.length) {
      msEl('mbPrev').hidden = false;
      msEl('mbPrev').textContent = 'Предыдущие окна, мкЗв/ч: ' + pv.join(' · ');
    } else {
      msEl('mbPrev').hidden = true;
    }
  } else {
    msEl('mbPrev').hidden = true;
  }

  mbSpark(a, m);
}

async function mbSpark(a, m) {
  var measuring = a.live && a.live.phase === 'measuring';
  if (!measuring) {
    msEl('mbSparkPath').setAttribute('d', '');
    return;
  }
  if (!(msData.timeOk === true && typeof msData.nowUtc === 'number' && m.elapsedSec >= 6)) {
    return;
  }
  if (Date.now() - mbSparkAt < 4000) {
    return;
  }
  var from = Math.floor(msData.nowUtc - m.elapsedSec);
  mbSparkAt = Date.now();
  try {
    var r = await fetch('/api/history.csv?kind=raw&pts=60&from=' + from, {
      credentials: 'same-origin',
      cache: 'no-store'
    });
    if (!r.ok) {
      return;
    }
    var text = await r.text();
    var lines = text.split('\n');
    var vals = [];
    for (var i = 1; i < lines.length; i++) {
      var line = lines[i];
      if (!line) {
        continue;
      }
      var cols = line.split(',');
      var v = Number(cols[3]);
      if (isFinite(v)) {
        vals.push(v);
      }
    }
    if (vals.length >= 2) {
      msEl('mbSparkPath').setAttribute('d', AFMB.sparkPath(vals, 300, 48, 2));
    } else {
      msEl('mbSparkPath').setAttribute('d', '');
    }
  } catch (e) {
    // ошибка сети не ломает мини-график
  }
}

function mbRenderResultsInfo() {
  var runs = msData ? msData.runs : [];
  msEl('mbResN').textContent = String(runs.length);
  var em = msEl('mbResEmpty'); if (em) { em.hidden = runs.length > 0; var rc = msEl('msResCard'); rc.classList.toggle('empty', !runs.length); if (!runs.length) { rc.open = true; } }   // e16g (T-50): пустое состояние
  var x = null;
  for (var i = 0; i < runs.length; i++) {
    var id = runs[i].run && runs[i].run.id;
    if (typeof id === 'number' && (x === null || id > (x.run && x.run.id))) {
      x = runs[i];
    }
  }
  if (x) {
    var mean = x.sum && typeof x.sum.mean_usvh === 'number' ? AFUN.rateEps(x.sum.mean_usvh, x.sum.counts > 0 ? 100 / Math.sqrt(x.sum.counts) : undefined).value + ' мкЗв/ч' : '';
    msEl('mbResLast').textContent = '· ' + AFMB.runLabel(x.run) + (mean ? ': ' + mean : '');
  } else {
    msEl('mbResLast').textContent = '';
  }
}

function mbApplyMode() {
  var j = msF.mode === 'journal';
  msEl('msNormal').hidden = j;
  msEl('mbJrn').hidden = !j;
  msEl('msGo').hidden = j;
  msEl('msTotal').hidden = j;
  if (j) {
    msEl('msModeNote').textContent = 'Из журнала: средняя мощность дозы и погрешность по уже сохранённым сырым данным за выбранный интервал; новый замер не запускается.';
  }
}

function mbChartView() {
  if (typeof state !== 'undefined' && state && state.view && typeof state.view.t0 === 'number') {
    return { t0: state.view.t0, t1: state.view.t1 };
  }
  try {
    var raw = localStorage.getItem('af.chartView');
    if (raw) {
      var o = JSON.parse(raw);
      if (o && isFinite(o.t0) && isFinite(o.t1)) {
        return { t0: o.t0, t1: o.t1 };
      }
    }
  } catch (e) {
    // игнорируем повреждённое хранилище
  }
  return null;
}

function mbTakeClick() {
  var rg = AFMB.journalRange(mbChartView(), msData ? msData.nowUtc : null);
  if (rg === null) {
    msSay('mbJrnMsg', 'На графике нет выбранного участка: откройте вкладку «История», выберите участок (или масштаб) и вернитесь сюда.', 'bad');
    return;
  }
  msEl('mbFrom').value = AFMB.toLocalInput(rg.from);
  msEl('mbTo').value = AFMB.toLocalInput(rg.to);
  mbStale();
  msSay('mbJrnMsg', 'Подставлен участок графика. Нажмите «Посчитать».', 'ok');
}

function mbStale() {
  mbJrn = null;
  msEl('mbSave').disabled = true;
  msKv(msEl('mbJrnRes'), []);
}

async function mbCalcClick() {
  var from = AFMB.fromLocalInput(msEl('mbFrom').value);
  var to = AFMB.fromLocalInput(msEl('mbTo').value);
  if (from === null || to === null) {
    msSay('mbJrnMsg', 'Заполните поля «с» и «по» (дата и время).', 'bad');
    return;
  }
  if (to <= from || to - from > 36 * 3600) {
    msSay('mbJrnMsg', AFMB.journalErrText('bad_range'), 'bad');
    return;
  }
  if (msBusy) {
    return;
  }
  msBusy = true;
  msSay('mbJrnMsg', 'считаю на плате…', '');
  msEl('mbCalc').disabled = true;
  try {
    var r = await fetch('/api/meas/journal?from=' + from + '&to=' + to, {
      credentials: 'same-origin',
      cache: 'no-store'
    });
    var d = null;
    try {
      d = await r.json();
    } catch (e) {
      d = null;
    }
    if (r.ok && d && d.ok === true && d.res) {
      mbJrn = { from: from, to: to, res: d.res, gaps: d.gaps };
      msKv(msEl('mbJrnRes'), AFMB.journalRows(d.res, d.gaps, from, to));
      msEl('mbSave').disabled = false;
      msSay('mbJrnMsg', 'Расчёт готов. Сохраните его в результаты или измените интервал.', 'ok');
    } else {
      mbStale();
      msSay('mbJrnMsg', AFMB.journalErrText(d && typeof d.err === 'string' ? d.err : ''), 'bad');
    }
  } catch (e) {
    mbStale();
    msSay('mbJrnMsg', AFMB.journalErrText(''), 'bad');
  } finally {
    msBusy = false;
    msEl('mbCalc').disabled = false;
  }
}

async function mbSaveClick() {
  if (!mbJrn || msBusy) {
    return;
  }
  msBusy = true;
  msEl('mbSave').disabled = true;
  try {
    var r = await msPost('/api/meas/journal-save', {
      name: msEl('msName').value,
      from: mbJrn.from,
      to: mbJrn.to
    });
    if (r.ok) {
      msSel = r.data && typeof r.data.id === 'number' ? r.data.id : msSel;
      msRowsKey = '';
      msSay('mbJrnMsg', 'Сохранено в результаты (запуск ' + r.data.id + ', из журнала).', 'ok');
      msEl('msResCard').open = true;
    } else {
      msEl('mbSave').disabled = false;
      msSay('mbJrnMsg', r.noToken ? msErrOf(r) : AFMB.journalErrText(r.data && typeof r.data.err === 'string' ? r.data.err : ''), 'bad');
    }
  } catch (e) {
    msEl('mbSave').disabled = false;
    msSay('mbJrnMsg', AFMB.journalErrText(''), 'bad');
  } finally {
    msBusy = false;
    await msRefresh();
  }
}

function mbInit() {
  msEl('mbTake').addEventListener('click', mbTakeClick);
  msEl('mbCalc').addEventListener('click', mbCalcClick);
  msEl('mbSave').addEventListener('click', mbSaveClick);
  msEl('mbFrom').addEventListener('input', mbStale);
  msEl('mbTo').addEventListener('input', mbStale);
  mbApplyMode();
}
