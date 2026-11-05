var zlTimer = null;   // таймер отложенного запуска
var zlSeq = 0;        // номер запуска: устаревшие ответы отбрасываются

function zlSchedule() {
  if (zlTimer) clearTimeout(zlTimer);
  zlTimer = setTimeout(zlRun, 350);
}

async function zlRun() {
  // 1. Ранний выход при неактуальных условиях
  if (PAGE !== 'history') return;
  if (state.range) return;
  if (!state.view) return;
  if (!(state.view.t1 > state.view.t0)) return;

  // 2. Определяем общий диапазон и пролёты
  var ovr = state.ovr || AFS.fullRange(state.rows);
  if (!ovr) return;
  var view = state.view;
  var span = view.t1 - view.t0;
  var ovrSpan = ovr.t1 - ovr.t0;

  // 3. Вернулись к полному виду — перезагружаем обзор
  if (span >= 0.98 * ovrSpan) {
    if (state.ovr) {
      state.ovr = null;
      state.zl = null;
      state.view = null;
      state.prevFull = null;
      await fullLoad();
    }
    return;
  }

  // 4. Окно запроса с запасом в полпролёта
  var pad = span / 2;
  var from = Math.max(ovr.t0, Math.floor(view.t0 - pad));
  var to = Math.min(ovr.t1, Math.ceil(view.t1 + pad));

  // 5. Данные уже подходят — не грузим
  if (state.zl) {
    var zl = state.zl;
    if (view.t0 >= zl.from && view.t1 <= zl.to) {
      var ratio = span / zl.span;
      if (ratio >= 0.5 && ratio <= 2) return;
    }
  }

  try {
    // 6. Определяем режим загрузки
    var my = ++zlSeq;
    var status = await fetchJSON('/api/history/status');
    if (my !== zlSeq) return; // устаревший запуск
    var p = AFZP.pick({
      from: from,
      to: to,
      widthPx: canvas.clientWidth * (to - from) / span,
      status: status,
      rawRate: state.rateRaw || 0.5
    });

    // 7. Формируем URL
    var url = '/api/history.csv?kind=' + p.kind
      + (p.pts > 0 ? '&pts=' + p.pts : '')
      + '&from=' + from
      + '&to=' + to
      + '&limit=' + (p.pts > 0 ? 200000 : 20000);

    // 8. Загрузка и парсинг
    var res = await fetchText(url);
    if (my !== zlSeq) return; // пришёл более новый запуск
    var parsed = AFH.parseCsv(res.text);
    var rows = AFH.splitKnown(parsed.rows).known;

    // 9. Обновляем состояние и перерисовываем
    if (!state.ovr) {
      state.ovr = { t0: ovr.t0, t1: ovr.t1 };
    }
    state.kind = p.kind;
    state.rows = rows;
    applyEnv(rows);
    state.zl = { from: from, to: to, span: span, kind: p.kind, step: p.step };
    pillOk();
    redraw();
  } catch (e) {
    // 10. Ошибка не должна ломать страницу
    pillErr(e);
  }
}

// Отладочный экспорт
window.zlState = function () {
  return { ovr: state.ovr, zl: state.zl, rows: state.rows.length, kind: state.kind };
};
