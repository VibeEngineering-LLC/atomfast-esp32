var rgLatest = null;
var rgPrevAuto = true;
var rgPick = null;
var rgStore = null;

function rgEl(id) {
  return document.getElementById(id);
}

function rgNow() {
  return Math.floor(Date.now() / 1000);
}

function rgBounds() {
  if (!state.range && state.windowSec === 0 && state.rows.length) {
    const f = AFS.fullRange(state.rows);
    return { first: f.t0, last: f.t1 };
  }
  return { first: undefined, last: undefined };
}

function rgSay(text, cls) {
  const el = rgEl('rgMsg');
  el.className = 'result' + (cls ? ' ' + cls : '');
  el.textContent = text;
}

function rgSaveStore(active) {
  if (!rgStore) rgStore = AFRG.emptyStore();
  if (state.range) rgStore.last = { from: state.range.from, to: state.range.to };
  rgStore.active = !!active && !!rgStore.last;
  try {
    localStorage.setItem(AFRG.KEY, AFRG.serializeStore(rgStore));
  } catch (e) {
    /* ignore */
  }
}

function rgSyncAuto() {
  const a = rgEl('auto');
  const s = AFRG.autoState(state.range, rgPrevAuto);
  a.checked = s.checked;
  a.disabled = s.disabled;
  a.parentNode.title = s.title;
}

function rgSyncUi() {
  const r = state.range;
  setSeg(rgEl('segWin'), r ? 'range' : state.windowSec, 'data-win');
  rgEl('rangeLab').hidden = !r;
  rgEl('rangeLab').textContent = r ? AFRG.label(r.from, r.to) : '';
  rgEl('btnFull').hidden = !r;
  rgSyncAuto();
}

function rgRead() {
  return { from: AFRG.fromInput(rgEl('rgFrom').value), to: AFRG.fromInput(rgEl('rgTo').value) };
}

function rgCheck() {
  const r = rgRead();
  const b = rgBounds();
  const v = AFRG.validate(r, { now: rgNow(), first: b.first });
  const apply = rgEl('rgApply');
  apply.disabled = !v.ok;
  if (!v.ok) {
    rgSay(v.error, r.from === null && r.to === null ? '' : 'bad');
    return null;
  }
  const w = AFRG.rowWarning(state.kind, r.from, r.to, { rate: state.rateRaw || 0.5 });
  rgSay(w.warn ? w.text : 'Интервал: ' + AFRG.label(r.from, r.to), w.warn ? 'warn' : 'ok');
  return r;
}

function rgFill(r) {
  rgEl('rgFrom').value = AFRG.toInput(r.from);
  rgEl('rgTo').value = AFRG.toInput(r.to);
  rgCheck();
}

function rgClose() {
  rgEl('rangePanel').hidden = true;
  rgEl('btnRange').setAttribute('aria-expanded', 'false');
}

function rgOpen() {
  const now = rgNow();
  const box = rgEl('rgPresets');
  box.textContent = '';
  for (const p of AFRG.presets(now)) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'btn';
    b.textContent = p.label;
    b.dataset.preset = p.id;
    b.addEventListener('click', () => { rgFill(p); rgApply(p); });
    box.appendChild(b);
  }
  const b = rgBounds();
  rgEl('rgBounds').textContent = b.first ? 'История платы: ' + AFRG.label(b.first, b.last) : 'Границы истории плата не сообщает: если за интервал данных нет, график будет пустым.';
  rgFill(state.range || { from: now - 3600, to: now });
  rgEl('rangePanel').hidden = false;
  rgEl('btnRange').setAttribute('aria-expanded', 'true');
  rgEl('rgFrom').focus();
}

function rgToggle() {
  if (rgEl('rangePanel').hidden) rgOpen(); else rgClose();
}

function rgClear(reload) {
  if (!state.range) return;
  state.range = null;
  rgLatest.cancel();
  rgSaveStore(false);
  rgSyncUi();
  if (reload) fullLoad();
}

async function rgFetchEnv(r, signal) {
  const q = AFRG.query('env', r.from, r.to, MAX_ENV_ROWS);
  const res = await fetchText('/api/history.csv?' + q, { signal: signal });
  return AFS.parseEnvCsv(res.text).rows;
}

async function loadRange() {
  const r = state.range;
  if (!r) return;
  const tk = rgLatest.begin();
  state.busy = true;
  state.ovr = null; state.zl = null;
  try {
    state.kind = AFRG.rowWarning('raw', r.from, r.to, { rate: state.rateRaw || 0.5 }).warn ? 'agg' : 'raw';   // e16e (T-34): вид выбирается автоматически
    const maxRows = state.kind === 'raw' ? MAX_RAW_ROWS : MAX_AGG_ROWS;
    const res = await fetchText('/api/history.csv?' + AFRG.query(state.kind, r.from, r.to, maxRows), { signal: tk.signal });
    const parsed = AFH.parseCsv(res.text);
    let env = [];
    try {
      env = await rgFetchEnv(r, tk.signal);
    } catch (e) {
      if (tk.signal.aborted) throw e;
      env = [];
    }
    if (!rgLatest.isCurrent(tk)) return;
    if (state.rangeRestored && !parsed.rows.length) {
      state.rangeRestored = false;
      rgClear(false);
      state.windowSec = 0;
      state.kind = 'agg';
      setSeg(rgEl('segKind'), 'agg', 'data-kind');
      rgSyncUi();
      state.busy = false;
      fullLoad();
      return;
    }
    state.rangeRestored = false;
    if (state.kind === 'raw' && parsed.rows.length) state.rateRaw = AFH.ratePerSec(parsed.rows);
    const split = AFH.splitKnown(parsed.rows);
    const rows = split.known;
    state.env = env;
    applyEnv(rows);
    state.rows = rows;
    state.unknown = split.unknown;
    state.errors = [];
    if (parsed.bad) state.errors.push('Пропущено строк с ошибками: ' + parsed.bad);
    if (parsed.rows.length >= maxRows) state.errors.push('Интервал обрезан: показаны первые ' + maxRows + ' строк, сузьте интервал или выберите Агрегаты');
    if (!rows.length) state.errors.push('За выбранный интервал данных нет');
    const full = AFS.fullRange(rows);
    state.view = full ? { t0: full.t0, t1: full.t1 } : null;
    state.prevFull = full ? { t0: full.t0, t1: full.t1 } : null;
    cursor = null;
    pillOk();
    redraw();
  } catch (e) {
    if (tk.signal.aborted) return;
    pillErr(e);
  } finally {
    if (rgLatest.isCurrent(tk)) state.busy = false;
  }
}

function rgApply(r) {
  const range = r || rgRead();
  const b = rgBounds();
  const v = AFRG.validate(range, { now: rgNow(), first: b.first });
  if (!v.ok) {
    rgSay(v.error, 'bad');
    return false;
  }
  if (!state.range) rgPrevAuto = rgEl('auto').checked;
  state.range = { from: range.from, to: range.to };
  rgSaveStore(true);
  rgSyncUi();
  rgClose();
  loadRange();
  return true;
}

function rgLastN() {
  const n = parseInt(rgEl('rgN').value, 10);
  const unit = rgEl('rgUnit').value;
  const r = AFRG.lastN(n, unit, rgNow());
  if (!r) {
    rgSay('Укажите целое число от 1', 'bad');
    return;
  }
  if (!rgStore) rgStore = AFRG.emptyStore();
  rgStore.lastN = { n: n, unit: unit };
  rgFill(r);
  rgApply(r);
}

async function rgCopy() {
  const r = state.range || rgCheck();
  if (!r) return;
  const link = location.origin + AFRG.linkFor(location.pathname, r);
  rgSay(link, 'ok');
  try {
    await navigator.clipboard.writeText(link);
  } catch (e) {
    /* ссылка осталась в строке состояния */
  }
}

function rgPickBox() {
  let el = rgEl('pickBox');
  if (!el) {
    el = document.createElement('div');
    el.id = 'pickBox';
    el.className = 'pickbox';
    el.hidden = true;
    canvas.parentNode.style.position = 'relative';
    canvas.parentNode.appendChild(el);
  }
  return el;
}

function rgPickMode(on) {
  state.pick = on;
  rgEl('btnPick').setAttribute('aria-pressed', on ? 'true' : 'false');
  canvas.classList.toggle('picking', on);
  if (!on) {
    rgPick = null;
    rgPickBox().hidden = true;
  }
}

function rgPickFinish() {
  const p = rgPick;
  rgPickMode(false);
  if (!p) return;
  const t0 = timeAt(Math.min(p.x0, p.x1));
  const t1 = timeAt(Math.max(p.x0, p.x1));
  const r = AFRG.selectionToRange(t0, t1, 10);
  if (!r) return;
  const full = AFS.fullRange(state.rows);
  if (full && full.t0 <= r.from && full.t1 >= r.to) {
    state.view = { t0: r.from, t1: r.to };
    redraw();
    return;
  }
  rgApply(r);
}

function rgFull() {
  if (state.range) {
    rgClear(true);
    return;
  }
  const f = AFS.fullRange(state.rows);
  state.view = f ? { t0: f.t0, t1: f.t1 } : null;
  redraw();
}

function rgBindPick() {
  canvas.addEventListener('pointerdown', e => {
    if (!state.pick) return;
    e.stopImmediatePropagation();
    const x = canvasPos(e).x;
    rgPick = { x0: x, x1: x };
    try {
      canvas.setPointerCapture(e.pointerId);
    } catch (err) {
      /* синтетическое событие */
    }
  }, true);
  canvas.addEventListener('pointermove', e => {
    if (!state.pick || !rgPick) return;
    e.stopImmediatePropagation();
    rgPick.x1 = canvasPos(e).x;
    const box = rgPickBox();
    const L = layout();
    const a = Math.max(L.left, Math.min(rgPick.x0, rgPick.x1));
    const b = Math.min(canvas.clientWidth - L.right, Math.max(rgPick.x0, rgPick.x1));
    box.style.left = (canvas.offsetLeft + a) + 'px';
    box.style.width = Math.max(0, b - a) + 'px';
    box.style.top = (canvas.offsetTop + L.top) + 'px';
    box.style.height = (canvas.clientHeight - L.top - L.bottom) + 'px';
    box.hidden = false;
  }, true);
  canvas.addEventListener('pointerup', e => {
    if (!state.pick || !rgPick) return;
    e.stopImmediatePropagation();
    rgPick.x1 = canvasPos(e).x;
    rgPickFinish();
  }, true);
}

function initRangeUi() {
  rgLatest = AFRG.createLatest();
  state.range = null;
  state.pick = false;
  let st = null;
  try {
    st = localStorage.getItem(AFRG.KEY);
  } catch (e) {
    st = null;
  }
  rgStore = st ? AFRG.parseStore(st) : AFRG.emptyStore();
  if (rgStore.lastN) {
    rgEl('rgN').value = String(rgStore.lastN.n);
    rgEl('rgUnit').value = rgStore.lastN.unit;
  }
  rgEl('btnRange').addEventListener('click', rgToggle);
  rgEl('rgApply').addEventListener('click', () => rgApply());
  rgEl('rgCancel').addEventListener('click', rgClose);
  rgEl('rgFrom').addEventListener('input', rgCheck);
  rgEl('rgTo').addEventListener('input', rgCheck);
  rgEl('rgLast').addEventListener('click', rgLastN);
  rgEl('rgCopy').addEventListener('click', rgCopy);
  rgEl('btnPick').addEventListener('click', () => rgPickMode(!state.pick));
  rgEl('btnFull').addEventListener('click', rgFull);
  document.addEventListener('keydown', e => {
    if (e.key !== 'Escape') return;
    if (state.pick) rgPickMode(false);
    rgClose();
  });
  rgBindPick();
  const fromUrl = AFRG.parseUrl(location.search);
  const restored = fromUrl || AFRG.restoreRange(rgStore, { now: rgNow() });
  if (!restored) {
    rgSyncUi();
    return false;
  }
  rgPrevAuto = rgEl('auto').checked;
  state.range = { from: restored.from, to: restored.to };
  state.rangeRestored = !fromUrl;
  state.kind = AFRG.rowWarning('raw', restored.from, restored.to, { rate: 0.5 }).warn ? 'agg' : 'raw';
  setSeg(rgEl('segKind'), state.kind, 'data-kind');
  rgSaveStore(true);
  rgSyncUi();
  loadRange();
  return true;
}
