var clState = null;
var clScheme = 'light';
var clSel = 'rate';
var clStyleEl = null;
var CL_DASH_NAMES = [['', 'По умолчанию'], ['solid', 'Сплошная'], ['dash', 'Штрих'], ['dots', 'Точки'], ['dashdot', 'Штрих-пунктир'], ['long', 'Длинный штрих'], ['short', 'Короткий штрих'], ['sparse', 'Редкие точки'], ['wide', 'Широкий штрих']];

function clEl(id) {
  return document.getElementById(id);
}

function clTheme() {
  const t = document.documentElement.getAttribute('data-theme');
  if (t === 'light' || t === 'dark') return t;
  return window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}

function clLoad() {
  let t = null;
  try {
    t = localStorage.getItem(AFCL.KEY);
  } catch (e) {
    t = null;
  }
  clState = t ? AFCL.parse(t) : AFCL.emptyState();
}

function clSave() {
  try {
    localStorage.setItem(AFCL.KEY, AFCL.serialize(clState));
  } catch (e) {
    /* ignore */
  }
}

function clApply() {
  if (!clStyleEl) {
    clStyleEl = document.createElement('style');
    clStyleEl.id = 'afColors';
    document.head.appendChild(clStyleEl);
  }
  clStyleEl.textContent = AFCL.cssText(clState);
  for (const def of SERIES) {
    const s = AFCL.effectiveStyle(clState, def.cid || def.id);
    def.dash = AFCL.tintDash(s.dash, def.tint || 0);
    def.lw = s.width;
  }
}

function clSay(text, cls) {
  const el = clEl('clrMsg');
  el.className = 'result' + (cls ? ' ' + cls : '');
  el.textContent = text;
}

function clDrawPreview() {
  const cv = clEl('clrPrev');
  const ctx = cv.getContext('2d');
  const w = cv.width;
  const h = cv.height;
  ctx.fillStyle = AFCL.BG[clScheme];
  ctx.fillRect(0, 0, w, h);
  ctx.strokeStyle = clScheme === 'dark' ? 'rgba(207,214,205,.12)' : 'rgba(28,38,28,.12)';
  ctx.lineWidth = 1;
  for (let g = 1; g < 4; g++) {
    ctx.beginPath();
    ctx.moveTo(0, Math.round(h * g / 4) + 0.5);
    ctx.lineTo(w, Math.round(h * g / 4) + 0.5);
    ctx.stroke();
  }
  AFCL.IDS.forEach((id, i) => {
    const s = AFCL.effectiveStyle(clState, id);
    ctx.strokeStyle = AFCL.effectiveColor(clState, clScheme, id);
    ctx.lineWidth = id === clSel ? s.width + 1 : s.width;
    ctx.setLineDash(s.dash);
    ctx.beginPath();
    for (let x = 0; x <= 40; x++) {
      const y = h * (0.12 + 0.76 * i / 6.5) + Math.sin(x / 5 + i * 1.3) * h * 0.06;
      if (x === 0) ctx.moveTo(0, y); else ctx.lineTo(x * w / 40, y);
    }
    ctx.stroke();
  });
  ctx.setLineDash([]);
}

function clRenderSeries() {
  const box = clEl('clrSeries');
  box.textContent = '';
  for (const def of SERIES) {
    if (def.tint) continue;
    const cid = def.cid || def.id;
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'clr-ser';
    b.dataset.id = cid;
    b.setAttribute('aria-pressed', cid === clSel ? 'true' : 'false');
    const s = AFCL.effectiveStyle(clState, cid);
    b.innerHTML = swatchSVG(AFCL.effectiveColor(clState, clScheme, cid), s.dash, 26);
    const nm = document.createElement('span');
    nm.textContent = def.baseName || def.name;
    b.appendChild(nm);
    b.addEventListener('click', () => { clSel = cid; clRenderAll(); });
    box.appendChild(b);
  }
}

function clRenderPalette() {
  const box = clEl('clrPal');
  box.textContent = '';
  const cur = AFCL.effectiveColor(clState, clScheme, clSel);
  for (const hex of AFCL.PALETTE) {
    const b = document.createElement('button');
    const ok = AFCL.usable(hex, clScheme);
    b.type = 'button';
    b.className = 'clr-sw' + (ok ? '' : ' off');
    b.style.background = hex;
    b.dataset.hex = hex;
    b.disabled = !ok;
    b.title = ok ? hex : hex + ': контраст к фону ниже 3:1 на этой теме';
    b.setAttribute('aria-label', 'Цвет ' + hex + (ok ? '' : ', недоступен на этой теме'));
    b.setAttribute('aria-pressed', hex === cur ? 'true' : 'false');
    b.addEventListener('click', () => clSet(hex));
    box.appendChild(b);
  }
}

function clRenderAll() {
  const cur = AFCL.effectiveColor(clState, clScheme, clSel);
  const st = clState.styles[clSel] || {};
  clEl('clrHex').value = cur;
  clEl('clrDash').value = st.dash || '';
  clEl('clrWidth').value = st.width ? String(st.width) : '';
  clEl('clrCb').setAttribute('aria-pressed', clState.cb ? 'true' : 'false');
  for (const b of clEl('segScheme').querySelectorAll('button')) {
    b.setAttribute('aria-pressed', b.dataset.scheme === clScheme ? 'true' : 'false');
  }
  clRenderSeries();
  clRenderPalette();
  clDrawPreview();
}

function clCommit(msg) {
  clSave();
  clApply();
  clRenderAll();
  clSay(msg || '', msg ? 'ok' : '');
}

function clSet(hex) {
  const r = AFCL.setColor(clState, clScheme, clSel, hex);
  if (!r.ok) {
    clSay(r.error, 'bad');
    return;
  }
  clState = r.state;
  clCommit('Цвет применён');
}

function clHexInput() {
  const v = clEl('clrHex').value.trim();
  if (v === '') {
    clSay('', '');
    return;
  }
  const c = AFCL.check(v, clScheme);
  if (!c.ok) {
    clSay(c.error, 'bad');
    return;
  }
  clSet(v);
}

function clStyleChange() {
  const w = parseInt(clEl('clrWidth').value, 10);
  const r = AFCL.setStyle(clState, clSel, clEl('clrDash').value, isFinite(w) ? w : 0);
  if (!r.ok) {
    clSay(r.error, 'bad');
    return;
  }
  clState = r.state;
  clCommit('Стиль линии применён');
}

function initColors() {
  clLoad();
  clScheme = clTheme();
  clApply();
  if (!clEl('clrCard')) return;
  for (const o of CL_DASH_NAMES) {
    const op = document.createElement('option');
    op.value = o[0];
    op.textContent = o[1];
    clEl('clrDash').appendChild(op);
  }
  clEl('clrHex').addEventListener('input', clHexInput);
  clEl('clrDash').addEventListener('change', clStyleChange);
  clEl('clrWidth').addEventListener('change', clStyleChange);
  clEl('clrReset').addEventListener('click', () => {
    clState = AFCL.resetColor(clState, clScheme, clSel);
    clCommit('Ряд сброшен');
  });
  clEl('clrResetAll').addEventListener('click', () => {
    clState = AFCL.resetAll();
    clCommit('Все цвета сброшены');
  });
  clEl('clrCb').addEventListener('click', () => {
    clState = AFCL.colorblind();
    clCommit('Включена палитра для дальтоников (Okabe–Ito), у рядов разные стили линий');
  });
  for (const b of clEl('segScheme').querySelectorAll('button')) {
    b.addEventListener('click', () => { clScheme = b.dataset.scheme; clSay('', ''); clRenderAll(); });
  }
  clRenderAll();
}
