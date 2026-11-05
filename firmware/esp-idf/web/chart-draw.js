var axModel = null;
var axRangeKey = null;

function fontFactor(){
  var v = parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--fs'));
  return (isFinite(v) && v > 0) ? v : 1;
}
function fpx(n){
  return (Math.round(n * fontFactor() * 10) / 10) + 'px';
}
// П.5: поля рядов оси (прямые значения, без сглаживания)
function axisFields(ax) {
  var out = [];
  for (var i = 0; i < ax.series.length; i++) {
    var d = SERIES.find(function (x) { return x.id === ax.series[i]; });
    if (d) out.push(d.field);
  }
  return out;
}

function axisModel(viewRows){
  var built = AFAX.buildAxes(SERIES, state.vis);
  for (var i = 0; i < built.axes.length; i++) {
    var ax = built.axes[i];
    var auto = AFAX.autoRange(viewRows, axisFields(ax), AFAX.zeroOf(axCfg, ax.id));
    var r = AFAX.resolve(axCfg[ax.id], auto);
    if (r == null) { ax.min = 0; ax.max = 1; ax.mode = 'auto'; }
    else { ax.min = r.min; ax.max = r.max; ax.mode = r.mode; }
    ax.auto = auto;
    ax.ticks = AFAX.niceTicks(ax.min, ax.max, 5);
  }
  axModel = { axes: built.axes, byGroup: built.byGroup };
  return axModel;
}
function axisSlotsWidth(){
  return Math.round(58 * fontFactor());
}
function layout(){
  var axes = AFAX.buildAxes(SERIES, state.vis).axes;
  var nL = 0, nR = 0;
  for (var i = 0; i < axes.length; i++) {
    if (axes[i].side === 'L') nL++; else nR++;
  }
  return {
    left: nL > 0 ? 8 + nL * axisSlotsWidth() : 12,
    right: nR > 0 ? 8 + nR * axisSlotsWidth() : 12,
    top: Math.round(24 * fontFactor()),
    bottom: Math.round(46 * fontFactor())
  };
}
function axisAtX(x){
  if (!axModel) return null;
  var L = layout();
  var plotW = canvas.clientWidth - L.left - L.right;
  var w = axisSlotsWidth();
  var slot, ax, i;
  if (x < L.left) {
    slot = Math.floor((L.left - x) / w);
    for (i = 0; i < axModel.axes.length; i++) {
      ax = axModel.axes[i];
      if (ax.side === 'L' && ax.slot === slot) return ax;
    }
    return null;
  }
  if (x > L.left + plotW) {
    slot = Math.floor((x - L.left - plotW) / w);
    for (i = 0; i < axModel.axes.length; i++) {
      ax = axModel.axes[i];
      if (ax.side === 'R' && ax.slot === slot) return ax;
    }
    return null;
  }
  return null;
}
function draw(){
  var dpr = window.devicePixelRatio || 1;
  var cssW = canvas.clientWidth;
  var cssH = canvas.clientHeight;
  canvas.width = Math.round(cssW * dpr);
  canvas.height = Math.round(cssH * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssW, cssH);

  var L = layout();
  var plotW = cssW - L.left - L.right;
  var plotH = cssH - L.top - L.bottom;
  var top = L.top, left = L.left;

  var defs = AFS.visibleDefs(SERIES, state.vis);

  if (!state.rows.length) {
    ctx.fillStyle = cssVar('--muted');
    ctx.font = fpx(14) + ' system-ui, sans-serif';
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.fillText('Нет данных за выбранное окно', cssW/2, cssH/2);
    updateAria();
    return;
  }
  if (!defs.length) {
    ctx.fillStyle = cssVar('--muted');
    ctx.font = fpx(14) + ' system-ui, sans-serif';
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.fillText('Выберите хотя бы один ряд', cssW/2, cssH/2);
    updateAria();
    return;
  }

  var view;
  if (state.pendingView && state.rows.length) {
    var full = AFS.fullRange(state.rows);
    view = { t0: Math.max(full.t0, full.t1 - state.pendingView), t1: full.t1 };
    state.view = view;
    state.pendingView = 0;
  } else {
    view = state.view || AFS.fullRange(state.rows);
  }
  if (!view) {
    ctx.fillStyle = cssVar('--muted');
    ctx.font = fpx(14) + ' system-ui, sans-serif';
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.fillText('Нет данных за выбранное окно', cssW/2, cssH/2);
    updateAria();
    return;
  }
  var t0 = view.t0, t1 = view.t1;
  var viewRows = AFS.sliceRange(state.rows, t0, t1);

  // E-1: смена интервала (Интервал/сброс) - оси в авто; первый вызов только запоминает ключ
  var rk = state.range ? state.range.from + ':' + state.range.to : '';
  if (typeof axRangeKey !== 'undefined' && axRangeKey !== null && axRangeKey !== rk) { axCfg = AFAX.fitAll(axCfg); saveAxesLS(); }
  axRangeKey = rk;

  var model = axisModel(viewRows);

  var span = t1 - t0;
  var tickCount = cssW < 640 ? 5 : 8;
  var plan = AFTX.axisPlan(t0, t1, tickCount);

  // grid + time labels (second line: dates), day boundaries
  ctx.lineWidth = 1;
  ctx.font = fpx(12) + ' system-ui, sans-serif';
  ctx.textBaseline = 'top';
  var lastDateRight = -1e9;
  for (var ti = 0; ti < plan.ticks.length; ti++) {
    var tk = plan.ticks[ti];
    var x = AFS.xToPx(tk.ts, t0, t1, left, plotW);
    if (x < left - 1 || x > left + plotW + 1) continue;
    ctx.strokeStyle = cssVar('--grid');
    ctx.beginPath(); ctx.moveTo(x, top); ctx.lineTo(x, top + plotH); ctx.stroke();
    ctx.fillStyle = cssVar('--muted');
    ctx.textAlign = 'center';
    ctx.fillText(tk.label, x, top + plotH + 6);
    if (tk.date) {
      ctx.fillStyle = cssVar('--text');
      var dw = ctx.measureText(tk.date).width;
      var dx = Math.max(left + dw / 2, Math.min(x, left + plotW - dw / 2));
      if (dx - dw / 2 > lastDateRight + 6) {
        ctx.fillText(tk.date, dx, top + plotH + 6 + Math.round(15 * fontFactor()));
        lastDateRight = dx + dw / 2;
      }
    }
  }
  for (var mi = 0; mi < plan.marks.length; mi++) {
    var mk = plan.marks[mi];
    var mx = AFS.xToPx(mk.ts, t0, t1, left, plotW);
    if (mx < left || mx > left + plotW) continue;
    ctx.strokeStyle = cssVar('--muted');
    ctx.globalAlpha = 0.6;
    ctx.setLineDash([5, 4]);
    ctx.beginPath(); ctx.moveTo(mx, top); ctx.lineTo(mx, top + plotH); ctx.stroke();
    ctx.setLineDash([]);
    ctx.globalAlpha = 1;
    ctx.fillStyle = cssVar('--muted');
    ctx.textAlign = 'left';
    var cap = mk.date;
    // e16f (T-35): подпись границы суток только датой "27.10", без дня недели ("Вт" читалось как ватты)
    ctx.fillText(cap, mx + 4, top + 2);
  }
  // y grid (first axis ticks)
  var gridAxis = model.axes[0];
  if (gridAxis && gridAxis.ticks) {
    ctx.strokeStyle = cssVar('--grid');
    for (var gi = 0; gi < gridAxis.ticks.length; gi++) {
      var gv = gridAxis.ticks[gi];
      var gy = AFS.yToPx(gv, gridAxis.min, gridAxis.max, top, plotH);
      ctx.beginPath(); ctx.moveTo(left, gy); ctx.lineTo(left + plotW, gy); ctx.stroke();
    }
  }

  // axis labels
  function tickLabel(v, ticks) {
    var st = ticks.length > 1 ? Math.abs(ticks[1] - ticks[0]) : 0;
    if (st > 0 && st < 1 && Math.abs(v) < 1e6) return v.toFixed(Math.min(4, Math.ceil(-Math.log(st) / Math.LN10 - 1e-9)));
    return fmtCompact(v);
  }
  var slotW = axisSlotsWidth();
  for (var ai = 0; ai < model.axes.length; ai++) {
    var ax = model.axes[ai];
    var onAxis = defs.filter(function(d){ return AFAX.groupOf(d) === ax.id; });
    var color = (onAxis.length === 1) ? cssVar(onAxis[0].color) : cssVar('--muted');

    // tick numbers
    ctx.fillStyle = color;
    ctx.font = fpx(12) + ' system-ui, sans-serif';
    ctx.textBaseline = 'top';
    for (var k = 0; k < ax.ticks.length; k++) {
      var v = ax.ticks[k];
      var y = AFS.yToPx(v, ax.min, ax.max, top, plotH);
      if (ax.side === 'L') {
        ctx.textAlign = 'right';
        ctx.fillText(tickLabel(v, ax.ticks), left - 6 - ax.slot * slotW, y - 6);
      } else {
        ctx.textAlign = 'left';
        ctx.fillText(tickLabel(v, ax.ticks), left + plotW + 6 + ax.slot * slotW, y - 6);
      }
    }

    // T-54: линия оси рисуется всегда (раньше только для slot >= 1, у нулевого слота линии не было)
    var al = AFAX.axisLines([ax], left, plotW, top, plotH, slotW)[0];
    ctx.strokeStyle = color;
    ctx.globalAlpha = 0.55;
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(al.x + 0.5, al.y0); ctx.lineTo(al.x + 0.5, al.y1); ctx.stroke();
    ctx.globalAlpha = 1;

    if (ax.id === selAxis) {
      ctx.fillStyle = color;
      ctx.globalAlpha = 0.1;
      var hx = (ax.side === 'L') ? (left - (ax.slot + 1) * slotW) : (left + plotW + ax.slot * slotW);
      ctx.fillRect(hx, top, slotW, plotH);
      ctx.globalAlpha = 1;
    }
    // axis title (units)
    var title = AFAX.axisTitle(ax);
    if (ax.mode === 'manual') title += ' ✎';
    ctx.font = fpx(12) + ' system-ui, sans-serif';
    ctx.fillStyle = color;
    ctx.textBaseline = 'top';
    var maxW = slotW - 8;
    var tw = ctx.measureText(title).width;
    if (tw > maxW) {
      while (title.length > 1 && ctx.measureText(title + '…').width > maxW) title = title.slice(0, -1);
      title += '…';
    }
    if (ax.side === 'L') {
      ctx.textAlign = 'right';
      ctx.fillText(title, left - ax.slot * slotW - 4, 4);
    } else {
      ctx.textAlign = 'left';
      ctx.fillText(title, left + plotW + ax.slot * slotW + 4, 4);
    }
  }

  // series
  var maxPoints = Math.max(200, Math.floor(plotW));
  function strokeLine(def, field, scale, width, alpha) {
    var pts = AFS.plotPoints(viewRows, { field: field }, maxPoints);
    if (!pts || pts.length < 2) return;
    ctx.globalAlpha = alpha;
    ctx.strokeStyle = cssVar(def.color);
    ctx.lineWidth = width;
    ctx.setLineDash(def.dash || []);
    ctx.lineJoin = 'round'; ctx.lineCap = 'round';
    ctx.beginPath();
    for (var pi = 0; pi < pts.length; pi++) {
      var px = AFS.xToPx(pts[pi].x, t0, t1, left, plotW);
      var py = AFS.yToPx(pts[pi].y, scale.min, scale.max, top, plotH);
      if (pi === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
    }
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.globalAlpha = 1;
  }
  for (var di = 0; di < defs.length; di++) {
    var def = defs[di];
    var scale = model.axes[model.byGroup[AFAX.groupOf(def)]];
    if (!scale) continue;
    strokeLine(def, def.field, scale, def.lw || 2, 1);
  }
  // cursor crosshair + tooltip
  if (cursor) {
    var cv = AFS.cursorValues(state.rows, SERIES, state.vis, cursor.ts);
    if (cv) {
      var cx = AFS.xToPx(cv.ts, t0, t1, left, plotW);
      ctx.strokeStyle = cssVar('--muted');
      ctx.lineWidth = 1;
      ctx.setLineDash([4,3]);
      ctx.beginPath(); ctx.moveTo(cx, top); ctx.lineTo(cx, top + plotH); ctx.stroke();
      ctx.setLineDash([]);
      // dots
      for (var ci = 0; ci < cv.items.length; ci++) {
        var it = cv.items[ci];
        var sdef = null;
        for (var si = 0; si < SERIES.length; si++) { if (SERIES[si].id === it.id) { sdef = SERIES[si]; break; } }
        var sc = sdef ? model.axes[model.byGroup[AFAX.groupOf(sdef)]] : null;
        if (!sc || it.value == null) continue;
        var cy = AFS.yToPx(it.value, sc.min, sc.max, top, plotH);
        ctx.fillStyle = cssVar(it.color);
        ctx.beginPath(); ctx.arc(cx, cy, 3.5, 0, Math.PI*2); ctx.fill();
        ctx.strokeStyle = cssVar('--card'); ctx.lineWidth = 1.5; ctx.stroke();
      }
      drawTooltip(cv, cx, top, plotH, left, plotW, cssW);
    }
  }
  updateAxisPanel(model);
  updateAxBar();
  updateAria();
}
function drawTooltip(cv, cx, top, plotH, left, plotW, cssW){
  var off = offMin();
  var lines = [];
  lines.push({t: AFTX.tooltipTime(cv.ts), color: null});
  for (var i = 0; i < cv.items.length; i++) {
    var it = cv.items[i];
    lines.push({t: it.name + '  ' + (it.value == null ? '—' : fmtCompact(it.value, decOf(it.id)) + ' ' + it.unit), color: it.color});
  }
  ctx.font = fpx(13) + ' system-ui, sans-serif';
  var maxW = 0;
  for (var li = 0; li < lines.length; li++) maxW = Math.max(maxW, ctx.measureText(lines[li].t).width);
  var pad = Math.round(8 * fontFactor()), lh = Math.round(16 * fontFactor());
  var boxW = maxW + pad*2 + 14;
  var boxH = lines.length*lh + pad*2;
  var bx = cx + 12;
  if (bx + boxW > cssW - 4) bx = cx - 12 - boxW;
  if (bx < 4) bx = 4;
  var by = top + 8;
  if (by + boxH > top + plotH) by = top + plotH - boxH;
  ctx.fillStyle = cssVar('--tip-bg');
  ctx.strokeStyle = cssVar('--tip-border');
  ctx.lineWidth = 1;
  roundRect(bx, by, boxW, boxH, 8);
  ctx.fill(); ctx.stroke();
  ctx.textAlign = 'left'; ctx.textBaseline = 'top';
  var y = by + pad;
  for (var li2 = 0; li2 < lines.length; li2++) {
    var l = lines[li2];
    if (l.color) {
      ctx.fillStyle = cssVar(l.color);
      ctx.fillRect(bx + pad, y + 3, 8, 8);
      ctx.fillStyle = cssVar('--text');
      ctx.font = fpx(13) + ' system-ui, sans-serif';
      ctx.fillText(l.t, bx + pad + 14, y);
    } else {
      ctx.fillStyle = cssVar('--text');
      ctx.font = 'bold ' + fpx(13) + ' system-ui, sans-serif';
      ctx.fillText(l.t, bx + pad, y);
      ctx.font = fpx(13) + ' system-ui, sans-serif';
    }
    y += lh;
  }
}
