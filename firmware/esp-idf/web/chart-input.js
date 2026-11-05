function setAxisManual(ax, min, max) {
  axCfg = AFAX.setManual(axCfg, ax.id, min, max);
  saveAxesLS();
}

function viewFull() {
  return state.ovr || AFS.fullRange(state.rows);   // e16g: при детальной загрузке границы - общий диапазон обзора
}

function clampView(t0, t1) {
  const full = viewFull();
  if (!full) return { t0, t1 };
  const span = t1 - t0;
  const fullSpan = full.t1 - full.t0;
  if (span >= fullSpan) return { t0: full.t0, t1: full.t1 };
  if (t0 < full.t0) {
    t0 = full.t0;
    t1 = t0 + span;
  }
  if (t1 > full.t1) {
    t1 = full.t1;
    t0 = t1 - span;
  }
  return { t0, t1 };
}

function resetAll() {
  if (state.ovr) { state.ovr = null; state.zl = null; state.view = null; state.prevFull = null; fullLoad(); return; }   // e16g: вернуться к обзору
  const full = viewFull();
  state.view = full ? { t0: full.t0, t1: full.t1 } : null;
  cursor = null;
  axCfg = {};
  saveAxesLS();
  redraw();
}

let down = null;

canvas.addEventListener('pointerdown', e => {
  const p = canvasPos(e);
  const ax = axisAtX(p.x);
  down = {
    x: p.x,
    y: p.y,
    moved: false,
    ax: ax,
    v0: state.view ? { t0: state.view.t0, t1: state.view.t1 } : (viewFull() ? { t0: viewFull().t0, t1: viewFull().t1 } : null),
    r0: ax ? { min: ax.min, max: ax.max } : null
  };
  canvas.setPointerCapture && canvas.setPointerCapture(e.pointerId);
});

canvas.addEventListener('pointermove', e => {
  const p = canvasPos(e);
  if (down) {
    const dx = p.x - down.x;
    const dy = p.y - down.y;
    if (!down.moved && Math.abs(dx) <= 6 && Math.abs(dy) <= 6) {
      return;
    }
    down.moved = true;
    const L = layout();
    if (down.ax && down.r0) {
      const plotH = canvas.clientHeight - L.top - L.bottom;
      const frac = dy / plotH;
      const r = AFAX.panRange(down.r0.min, down.r0.max, frac);
      setAxisManual(down.ax, r.min, r.max);
    } else if (down.v0) {
      const plotW = canvas.clientWidth - L.left - L.right;
      const dt = -dx / plotW * (down.v0.t1 - down.v0.t0);
      state.view = clampView(down.v0.t0 + dt, down.v0.t1 + dt);
    }
    redraw();
    return;
  }
  if (e.pointerType === 'mouse' || !down) {
    const t = timeAt(p.x);
    if (t != null) setCursorFromTime(t, false);
  }
});

canvas.addEventListener('pointerup', e => {
  const p = canvasPos(e);
  if (down && !down.moved) {
    if (down.ax) {
      selectAxis(down.ax.id);
    } else {
      const t = timeAt(p.x);
      if (t != null) setCursorFromTime(t, false);
    }
  }
  if (down && down.moved && down.v0 && typeof zlSchedule === 'function') zlSchedule();
  down = null;
  drag = null;
  draw();
});
canvas.addEventListener('pointerleave', () => {
  if (!down) {
    cursor = null;
    draw();
  }
});

canvas.addEventListener('wheel', e => {
  e.preventDefault();
  const p = canvasPos(e);
  const ax = axisAtX(p.x);
  if (ax) {
    const L = layout();
    const plotH = canvas.clientHeight - L.top - L.bottom;
    let anchorFrac = 1 - (p.y - L.top) / plotH;
    anchorFrac = Math.max(0, Math.min(1, anchorFrac));
    const factor = e.deltaY > 0 ? 1.25 : 0.8;
    const r = AFAX.zoomRange(ax.min, ax.max, factor, anchorFrac);
    setAxisManual(ax, r.min, r.max);
    redraw();
    return;
  }
  const t = timeAt(p.x);
  if (t == null) return;
  const factor = e.deltaY > 0 ? 1.25 : 0.8;
  const full = viewFull();
  const cur = state.view || (full ? { t0: full.t0, t1: full.t1 } : null);
  if (!cur) return;
  let nv = AFS.zoomRange(cur.t0, cur.t1, t, factor, 10);
  if (full) {
    nv.t0 = Math.max(nv.t0, full.t0);
    nv.t1 = Math.min(nv.t1, full.t1);
  }
  state.view = nv;
  redraw();
  if (typeof zlSchedule === 'function') zlSchedule();   // e16g (T-46/T-49): перезапрос данных под новое окно
}, { passive: false });

canvas.addEventListener('dblclick', e => {
  const p = canvasPos(e);
  const ax = axisAtX(p.x);
  if (!ax) {
    resetAll();
    return;
  }
  if (p.y < layout().top) {
    openAxEdit(ax);
    return;
  }
  axCfg = AFAX.fitAxes(axCfg, [ax.id]);
  saveAxesLS();
  selectAxis(ax.id);
});

const btnReset = document.getElementById('btnReset');
const btnZoomIn = document.getElementById('btnZoomIn');
const btnZoomOut = document.getElementById('btnZoomOut');
const btnFit = document.getElementById('btnFit');
const btnAxes = document.getElementById('btnAxes');

function axisScope() {
  if (!axModel || !axModel.axes) return [];
  return AFAX.scaleTargets(axModel.axes, selAxis, allAxes);
}

function axisZoom(factor) {
  const ids = axisScope();
  if (!ids.length) return false;
  axCfg = AFAX.zoomAxes(axCfg, axModel.axes, ids, factor);
  saveAxesLS();
  redraw();
  return true;
}

function axisFit() {
  const ids = axisScope();
  const all = axModel && axModel.axes ? axModel.axes.map(a => a.id) : [];
  axCfg = AFAX.fitAxes(axCfg, ids.length ? ids : all);
  saveAxesLS();
  redraw();
}

function axisResetSel() {
  const ids = axisScope();
  if (!ids.length) {
    resetAll();
    return;
  }
  axCfg = AFAX.resetAxes(axCfg, ids);
  saveAxesLS();
  redraw();
}

function selectAxis(id) {
  selAxis = id;
  redraw();
}

function updateAxBar() {
  const bar = document.getElementById('axbar');
  if (!bar) return;
  const ax = axModel && axModel.axes ? axModel.axes.find(a => a.id === selAxis) : null;
  if (!ax) {
    bar.hidden = true;
    return;
  }
  bar.hidden = false;
  document.getElementById('axbName').textContent = 'Ось: ' + AFAX.axisTitle(ax);
  document.getElementById('axbMode').textContent = ax.mode === 'manual' ? 'ручной' : 'авто';
  document.getElementById('axbAll').checked = allAxes;
}

function openAxEdit(ax) {
  const f = document.getElementById('axedit');
  if (!f) return;
  selAxis = ax.id;
  f.hidden = false;
  f.setAttribute('data-axis', ax.id);
  document.getElementById('axeMin').value = fmtAxisVal(ax.min);
  document.getElementById('axeMax').value = fmtAxisVal(ax.max);
  document.getElementById('axeErr').textContent = '';
  document.getElementById('axeMin').focus();
  redraw();
}

const axEditForm = document.getElementById('axedit');
if (axEditForm) {
  axEditForm.addEventListener('submit', e => {
    e.preventDefault();
    const r = AFAX.setAxisText(axCfg, axEditForm.getAttribute('data-axis'), document.getElementById('axeMin').value, document.getElementById('axeMax').value);
    if (!r.ok) {
      document.getElementById('axeErr').textContent = r.error;
      return;
    }
    axCfg = r.cfg;
    saveAxesLS();
    axEditForm.hidden = true;
    redraw();
  });
  axEditForm.addEventListener('keydown', e => {
    if (e.key === 'Escape') axEditForm.hidden = true;
  });
}

document.getElementById('axbPlus').addEventListener('click', () => { axisZoom(0.8); });
document.getElementById('axbMinus').addEventListener('click', () => { axisZoom(1.25); });
document.getElementById('axbFit').addEventListener('click', () => { axisFit(); });
document.getElementById('axbReset').addEventListener('click', () => { axisResetSel(); });
document.getElementById('axbAll').addEventListener('change', e => {
  allAxes = e.target.checked;
  redraw();
});

btnReset.addEventListener('click', () => {
  axisResetSel();
});

btnZoomIn.addEventListener('click', () => {
  if (axisZoom(0.8)) return;
  const full = viewFull();
  const cur = state.view || (full ? { t0: full.t0, t1: full.t1 } : null);
  if (!cur) return;
  const center = (cur.t0 + cur.t1) / 2;
  const nv = AFS.zoomRange(cur.t0, cur.t1, center, 0.8, 10);
  state.view = clampView(nv.t0, nv.t1);
  redraw();
  if (typeof zlSchedule === 'function') zlSchedule();   // e16g (T-46/T-49): перезапрос данных под новое окно
});

btnZoomOut.addEventListener('click', () => {
  if (axisZoom(1.25)) return;
  const full = viewFull();
  const cur = state.view || (full ? { t0: full.t0, t1: full.t1 } : null);
  if (!cur) return;
  const center = (cur.t0 + cur.t1) / 2;
  const nv = AFS.zoomRange(cur.t0, cur.t1, center, 1.25, 10);
  state.view = clampView(nv.t0, nv.t1);
  redraw();
  if (typeof zlSchedule === 'function') zlSchedule();   // e16g (T-46/T-49): перезапрос данных под новое окно
});

btnFit.addEventListener('click', () => {
  axisFit();
});
btnAxes.addEventListener('click', () => {
  const panel = document.getElementById('axpanel');
  if (!panel) return;
  const hidden = panel.hasAttribute('hidden');
  if (hidden) {
    panel.removeAttribute('hidden');
    btnAxes.setAttribute('aria-expanded', 'true');
  } else {
    panel.setAttribute('hidden', '');
    btnAxes.setAttribute('aria-expanded', 'false');
  }
});

document.getElementById('segWin').addEventListener('click', e => {
  const b = e.target.closest('button');
  if (!b) return;
  if (b.id === 'btnRange') return;
  rgClear(false);
  axCfg = AFAX.fitAll(axCfg); saveAxesLS();   // E-1: смена периода - ось Y снова под видимые данные
  const w = parseInt(b.dataset.win, 10);
  if (w === 21600) {
    const p = AFAX.quickPeriod('6h');
    state.windowSec = p.windowSec;
    state.pendingView = p.viewSec;
  } else {
    state.windowSec = w;
    state.pendingView = 0;
  }
  state.kind = defaultKindForWindow(state.windowSec);
  state.view = null; state.prevFull = null;   // e16e (T-34): смена окна («Вся» и другие) сбрасывает вид - иначе устаревший view даёт пустой график
  setSeg(document.getElementById('segWin'), w, 'data-win');
  setSeg(document.getElementById('segKind'), state.kind, 'data-kind');
  saveLS();
  fullLoad();
});

document.getElementById('segKind').addEventListener('click', e => {
  const b = e.target.closest('button');
  if (!b) return;
  state.kind = b.dataset.kind;
  setSeg(document.getElementById('segKind'), state.kind, 'data-kind');
  saveLS();
  fullLoad();
});
