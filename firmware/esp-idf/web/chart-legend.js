var axCfg = {};
var axPanelSig = '';
var selAxis = null;
var allAxes = false;
const AX_LS_KEY = 'afh.axes.v1';

function fmtAxisVal(v) {
  return typeof v === 'number' && isFinite(v) ? String(Number(v.toPrecision(6))) : '';
}
function loadAxesLS() {
  try {
    const r = AFAX.restore(localStorage.getItem(AX_LS_KEY) || '');
    axCfg = r.cfg;
  } catch (e) {
    /* оставить как есть */
  }
}

function saveAxesLS() {
  try {
    const empty = !axCfg || Object.keys(axCfg).length === 0;
    if (empty) {
      localStorage.removeItem(AX_LS_KEY);
    } else {
      localStorage.setItem(AX_LS_KEY, AFAX.serialize(axCfg));
    }
  } catch (e) {
    /* ignore */
  }
}

function buildLegend() {
  const el = document.getElementById('legend');
  el.innerHTML = '';
  for (const gid of AFAX.GROUP_IDS) {
    const defs = SERIES.filter(d => AFAX.groupOf(d) === gid);
    if (!defs.length) continue;
    const g = document.createElement('div');
    g.className = 'lgroup';
    g.setAttribute('data-group', gid);
    for (const def of defs) {
      const label = document.createElement('label');
      label.className = 'leg';
      label.title = def.name + ', ' + def.unit;
      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.checked = !!state.vis[def.id];
      cb.setAttribute('aria-label', def.name);
      cb.addEventListener('change', () => {
        state.vis[def.id] = cb.checked;
        saveLS();
        redraw();
      });
      const sw = document.createElement('span');
      sw.innerHTML = swatchSVG(cssVar(def.color), def.dash, 26);
      const nm = document.createElement('span');
      nm.className = 'nm';
      nm.textContent = def.name;
      const un = document.createElement('span');
      un.className = 'un';
      un.textContent = def.unit;
      label.appendChild(cb);
      label.appendChild(sw);
      label.appendChild(nm);
      label.appendChild(un);
      g.appendChild(label);
    }
    el.appendChild(g);
  }
}

function buildAxisPanel() {
  const panel = document.getElementById('axpanel');
  panel.innerHTML = '';
  if (!axModel || !axModel.axes || !axModel.axes.length) {
    const p = document.createElement('div');
    p.className = 'axempty';
    p.textContent = 'Нет данных для осей';
    panel.appendChild(p);
    axPanelSig = '';
    return;
  }
  for (const ax of axModel.axes) {
    const title = AFAX.axisTitle(ax);
    const row = document.createElement('div');
    row.className = 'axrow';
    row.setAttribute('data-axis', ax.id);

    const name = document.createElement('div');
    name.className = 'axname';
    (ax.colors || []).forEach(c => {
      const d = document.createElement('span');
      d.className = 'axdot';
      d.style.background = cssVar(c);
      name.appendChild(d);
    });
    name.appendChild(document.createTextNode(title));
    name.style.cursor = 'pointer';
    name.addEventListener('click', () => { selectAxis(ax.id); });
    row.appendChild(name);

    const seg = document.createElement('div');
    seg.className = 'seg';
    const bAuto = document.createElement('button');
    bAuto.type = 'button';
    bAuto.setAttribute('data-mode', 'auto');
    bAuto.textContent = 'Авто';
    bAuto.setAttribute('aria-pressed', ax.mode === 'auto' ? 'true' : 'false');
    bAuto.addEventListener('click', () => {
      axCfg = AFAX.setAuto(axCfg, ax.id);
      saveAxesLS();
      redraw();
    });
    const bMan = document.createElement('button');
    bMan.type = 'button';
    bMan.setAttribute('data-mode', 'manual');
    bMan.textContent = 'Ручной';
    bMan.setAttribute('aria-pressed', ax.mode === 'manual' ? 'true' : 'false');
    bMan.addEventListener('click', () => {
      const cur = axModel && axModel.axes ? axModel.axes.find(a => a.id === ax.id) : null;
      const mn = cur ? cur.min : ax.min;
      const mx = cur ? cur.max : ax.max;
      axCfg = AFAX.setManual(axCfg, ax.id, mn, mx);
      saveAxesLS();
      redraw();
    });
    seg.appendChild(bAuto);
    seg.appendChild(bMan);
    row.appendChild(seg);

    const minI = document.createElement('input');
    minI.type = 'text';
    minI.className = 'axmin';
    minI.setAttribute('inputmode', 'decimal');
    minI.setAttribute('autocomplete', 'off');
    minI.setAttribute('aria-label', 'Минимум оси ' + title);
    minI.value = fmtAxisVal(ax.min);
    const maxI = document.createElement('input');
    maxI.type = 'text';
    maxI.className = 'axmax';
    maxI.setAttribute('inputmode', 'decimal');
    maxI.setAttribute('autocomplete', 'off');
    maxI.setAttribute('aria-label', 'Максимум оси ' + title);
    maxI.value = fmtAxisVal(ax.max);
    if (ax.mode === 'auto') {
      minI.setAttribute('data-auto', 'true');
      maxI.setAttribute('data-auto', 'true');
    }
    const apply = () => {
      const v = AFAX.validateRange(minI.value, maxI.value);
      if (v.ok) {
        axCfg = AFAX.setManual(axCfg, ax.id, v.min, v.max);
        saveAxesLS();
        err.textContent = '';
        redraw();
      } else {
        err.textContent = v.error;
      }
    };
    minI.addEventListener('change', apply);
    maxI.addEventListener('change', apply);
    minI.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); apply(); } });
    maxI.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); apply(); } });
    row.appendChild(minI);
    row.appendChild(maxI);

    const zl = document.createElement('label');
    zl.className = 'axzl';
    const zc = document.createElement('input');
    zc.type = 'checkbox';
    zc.className = 'axzero';
    zc.checked = AFAX.zeroOf(axCfg, ax.id);
    zc.setAttribute('aria-label', 'Включать ноль на оси ' + title);
    zc.addEventListener('change', () => {
      axCfg = AFAX.setZero(axCfg, ax.id, zc.checked);
      saveAxesLS();
      redraw();
    });
    zl.appendChild(zc);
    zl.appendChild(document.createTextNode(' Включать ноль'));
    row.appendChild(zl);
    const fit = document.createElement('button');
    fit.type = 'button';
    fit.className = 'axfit';
    fit.textContent = 'По данным';
    fit.addEventListener('click', () => {
      if (ax.auto) {
        axCfg = AFAX.setManual(axCfg, ax.id, ax.auto.min, ax.auto.max);
      }
      saveAxesLS();
      redraw();
    });
    row.appendChild(fit);

    const err = document.createElement('div');
    err.className = 'axerr';
    err.setAttribute('role', 'alert');
    row.appendChild(err);

    panel.appendChild(row);
  }
  axPanelSig = axModel.axes.map(a => a.id).join('|');
}

function updateAxisPanel(model) {
  const sig = (model && model.axes ? model.axes.map(a => a.id).join('|') : '');
  if (sig !== axPanelSig) {
    axPanelSig = sig;
    buildAxisPanel();
  }
  if (!model || !model.axes) return;
  const panel = document.getElementById('axpanel');
  for (const ax of model.axes) {
    const row = panel.querySelector('.axrow[data-axis="' + ax.id + '"]');
    if (!row) continue;
    const bAuto = row.querySelector('button[data-mode="auto"]');
    const bMan = row.querySelector('button[data-mode="manual"]');
    if (bAuto) bAuto.setAttribute('aria-pressed', ax.mode === 'auto' ? 'true' : 'false');
    if (bMan) bMan.setAttribute('aria-pressed', ax.mode === 'manual' ? 'true' : 'false');
    const zc2 = row.querySelector('.axzero');
    if (zc2) zc2.checked = AFAX.zeroOf(axCfg, ax.id);
    row.classList.toggle('axsel', ax.id === selAxis);
    const minI = row.querySelector('.axmin');
    const maxI = row.querySelector('.axmax');
    if (minI && document.activeElement !== minI) minI.value = fmtAxisVal(ax.min);
    if (maxI && document.activeElement !== maxI) maxI.value = fmtAxisVal(ax.max);
    if (ax.mode === 'auto') {
      if (minI) minI.setAttribute('data-auto', 'true');
      if (maxI) maxI.setAttribute('data-auto', 'true');
    } else {
      if (minI) minI.removeAttribute('data-auto');
      if (maxI) maxI.removeAttribute('data-auto');
    }
  }
}
