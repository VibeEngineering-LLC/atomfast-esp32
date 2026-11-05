(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const wait = P.wait || 2500;
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const res = [];
  const t = (n, cond, d) => res.push({ n, ok: !!cond, d: String(d) });
  try { try { localStorage.clear(); } catch (e) {} } catch (e) {}
  const f = document.createElement('iframe');
  f.style.cssText = 'position:fixed;left:0;top:0;border:0;opacity:0;pointer-events:none;';
  f.width = width;
  f.height = 900;
  f.src = location.origin + '/?theme=light&vis=cps,air_p,air_t';
  document.body.appendChild(f);
  await new Promise(r => { f.onload = r; });
  await sleep(wait);
  const W = f.contentWindow;
  const D = f.contentDocument;
  const q = s => D.querySelector(s);
  const qa = s => Array.from(D.querySelectorAll(s));
  const canvas = q('#chart');
  const rect = canvas.getBoundingClientRect();
  const wheel = (x, y, dy) => canvas.dispatchEvent(new W.WheelEvent('wheel', { clientX: rect.left + x, clientY: rect.top + y, deltaY: dy, bubbles: true, cancelable: true }));
  const ptr = (type, x, y) => canvas.dispatchEvent(new W.MouseEvent(type, { clientX: rect.left + x, clientY: rect.top + y, bubbles: true }));
  const ax = () => JSON.parse(W.localStorage.getItem('afh.axes.v1') || 'null');
  const click = el => el.dispatchEvent(new W.MouseEvent('click', { bubbles: true, cancelable: true }));
  const setVal = (el, v) => { el.value = v; el.dispatchEvent(new W.Event('input', { bubbles: true })); };
  const fireChange = el => el.dispatchEvent(new W.Event('change', { bubbles: true }));
  const row = a => qa('#axpanel .axrow').find(r => r.getAttribute('data-axis') === a);
  const seg = (r, m) => r.querySelector('.seg [data-mode="' + m + '"]');
  const grp = g => qa('#legend .lgroup').find(x => x.getAttribute('data-group') === g);
  const leg = a => qa('#legend .leg').find(x => { const i = x.querySelector('input'); return i && i.getAttribute('aria-label') === a; });
  const winBtn = w => qa('#segWin [data-win]').find(b => b.getAttribute('data-win') === w);
  const btnAxes = q('#btnAxes');
  const axpanel = q('#axpanel');
  const btnReset = q('#btnReset');
  const toolbar = q('.toolbar');
  const summary = q('#summary');
  const legend = q('#legend');
  const groups = qa('#legend .lgroup');
  const legs = qa('#legend .leg');
  const cbs = qa('#legend .leg input[type=checkbox]');
  const checked = cbs.filter(c => c.checked);
  const checkedNames = checked.map(c => c.getAttribute('aria-label') || '');
  const axrows = qa('#axpanel .axrow');
  const rowCount = row('count');
  const minEl = rowCount ? rowCount.querySelector('.axmin') : null;
  const maxEl = rowCount ? rowCount.querySelector('.axmax') : null;
  const errEl = rowCount ? rowCount.querySelector('.axerr') : null;
  const autoBtn = rowCount ? seg(rowCount, 'auto') : null;
  const manualBtn = rowCount ? seg(rowCount, 'manual') : null;
  const winBtns = qa('#segWin [data-win]');
  const winVals = winBtns.map(b => b.getAttribute('data-win'));
  const win86400 = winBtn('86400');
  const toolbarEls = toolbar ? qa('.toolbar button, .toolbar input') : [];
  const unnamed = toolbarEls.filter(el => {
    const al = el.getAttribute('aria-label');
    const tc = (el.textContent || '').trim();
    return !(al && al.trim()) && !(tc && tc.trim());
  });
  const tbH = toolbar ? toolbar.getBoundingClientRect().height : 0;
  const fs = W.getComputedStyle(f.contentDocument.documentElement).getPropertyValue('--fs').trim();
  const bodyFs = W.getComputedStyle(f.contentDocument.body).fontSize;
  try {
    const ok = groups.length === 4 && groups.every(g => g.getAttribute('data-group')) &&
      groups.map(g => g.getAttribute('data-group')).join(',') === 'dose,count,temp,press' &&
      legs.every(l => { const i = l.querySelector('input[type=checkbox]'); return i && (i.getAttribute('aria-label') || '').trim(); });
    t('legend_groups', ok, 'groups=' + groups.map(g => g.getAttribute('data-group')).join(',') + ' legs=' + legs.length);
  } catch (e) { t('legend_groups', false, e.message); }
  try {
    const ok = checked.length === 3 &&
      checkedNames.some(n => n.indexOf('CPS') !== -1) &&
      checkedNames.some(n => n.indexOf('Температура среды') !== -1) &&
      checkedNames.some(n => n.indexOf('Давление среды') !== -1);
    t('legend_checked', ok, 'checked=' + checked.length + ' names=' + checkedNames.join('|'));
  } catch (e) { t('legend_checked', false, e.message); }
  try {
    const h0 = axpanel.hasAttribute('hidden');
    const a0 = btnAxes.getAttribute('aria-expanded');
    click(btnAxes); await sleep(150);
    const h1 = axpanel.hasAttribute('hidden');
    const a1 = btnAxes.getAttribute('aria-expanded');
    click(btnAxes); await sleep(150);
    const h2 = axpanel.hasAttribute('hidden');
    const a2 = btnAxes.getAttribute('aria-expanded');
    t('axes_btn_toggle', h0 === true && a0 === 'false' && h1 === false && a1 === 'true' && h2 === true && a2 === 'false',
      'h0=' + h0 + ' a0=' + a0 + ' h1=' + h1 + ' a1=' + a1 + ' h2=' + h2 + ' a2=' + a2);
  } catch (e) { t('axes_btn_toggle', false, e.message); }
  try {
    click(btnAxes); await sleep(150);
    const ok = axrows.length === 3 &&
      axrows.every(r => r.getAttribute('data-axis')) &&
      axrows.map(r => r.getAttribute('data-axis')).sort().join(',') === 'count,press,temp' &&
      axrows.every(r => {
        const a = r.querySelector('.seg [data-mode="auto"]');
        const m = r.querySelector('.seg [data-mode="manual"]');
        const mn = r.querySelector('.axmin');
        const mx = r.querySelector('.axmax');
        return a && m && a.getAttribute('aria-pressed') === 'true' && mn && mx &&
          (mn.getAttribute('aria-label') || '').trim() && (mx.getAttribute('aria-label') || '').trim();
      });
    t('axes_rows', ok, 'rows=' + axrows.map(r => r.getAttribute('data-axis')).join(','));
  } catch (e) { t('axes_rows', false, e.message); }
  try {
    setVal(minEl, '5'); setVal(maxEl, '1'); fireChange(maxEl); await sleep(150);
    const err1 = (errEl.textContent || '').trim();
    const st1 = ax();
    const bad = st1 && st1.cfg && st1.cfg.count && st1.cfg.count.mode === 'manual';
    setVal(minEl, '0'); setVal(maxEl, '100,5'); fireChange(maxEl); await sleep(150);
    const err2 = (errEl.textContent || '').trim();
    const st2 = ax();
    const c2 = st2 && st2.cfg && st2.cfg.count;
    const ok = err1.indexOf('min должен быть меньше max') !== -1 && !bad &&
      err2 === '' && c2 && c2.mode === 'manual' && c2.min === 0 && c2.max === 100.5 &&
      manualBtn.getAttribute('aria-pressed') === 'true';
    t('axes_manual_validate', ok, 'err1=' + err1 + ' bad=' + bad + ' err2=' + err2 + ' c2=' + JSON.stringify(c2) + ' manual=' + manualBtn.getAttribute('aria-pressed'));
  } catch (e) { t('axes_manual_validate', false, e.message); }
  try {
    const axX = W.layout().left - W.axisSlotsWidth() / 2;
    const s0 = ax().cfg.count;
    const d0 = s0.max - s0.min;
    wheel(axX, 150, 100); await sleep(150);
    const s1 = ax().cfg.count;
    const d1 = s1.max - s1.min;
    wheel(axX, 150, -100); await sleep(150);
    wheel(axX, 150, -100); await sleep(150);
    const s2 = ax().cfg.count;
    const d2 = s2.max - s2.min;
    t('axes_wheel_y', d1 > d0 && d2 < d1, 'd0=' + d0 + ' d1=' + d1 + ' d2=' + d2);
  } catch (e) { t('axes_wheel_y', false, e.message); }
  try {
    click(autoBtn); await sleep(150);
    const st = ax();
    const c = st && st.cfg && st.cfg.count;
    const ok = (c === undefined || c === null || (c.mode === 'auto' && c.min === undefined && c.max === undefined)) &&
      autoBtn.getAttribute('aria-pressed') === 'true';
    t('axes_auto_back', ok, 'c=' + JSON.stringify(c) + ' auto=' + autoBtn.getAttribute('aria-pressed'));
  } catch (e) { t('axes_auto_back', false, e.message); }
  try {
    const before = ax();
    const beforeKeys = before && before.cfg ? Object.keys(before.cfg).sort().join(',') : '';
    wheel(rect.width / 2, 150, -100); await sleep(150);
    const after = ax();
    const afterKeys = after && after.cfg ? Object.keys(after.cfg).sort().join(',') : '';
    const same = beforeKeys === afterKeys;
    canvas.dispatchEvent(new W.MouseEvent('dblclick', { bubbles: true })); await sleep(150);
    const st = ax(); const cleared = st === null || Object.keys(st.cfg || {}).length === 0;
    t('wheel_plot_x', same && cleared, 'beforeKeys=' + beforeKeys + ' afterKeys=' + afterKeys + ' cleared=' + cleared);
  } catch (e) { t('wheel_plot_x', false, e.message); }
  try {
    const ok = winBtns.length === 5 &&
      winVals.sort().join(',') === '0,21600,3600,86400,range' &&
      winBtns.every(b => b.getAttribute('data-win'));
    click(win86400); await sleep(150);
    const pressed = winBtns.filter(b => b.getAttribute('aria-pressed') === 'true');
    const only = pressed.length === 1 && pressed[0] === win86400;
    t('period_buttons', ok && only, 'vals=' + winVals.join(',') + ' pressed=' + pressed.length);
  } catch (e) { t('period_buttons', false, e.message); }
  try {
    const legs2 = qa('#legend .leg');
    const extra = qa('#legend button, #axpanel .axrow [data-assign], .axbtn, .swap');
    const badge = (legend.textContent || '').indexOf('лев') !== -1 || (legend.textContent || '').indexOf('прав') !== -1;
    const kids = legs2.every(l => l.querySelectorAll('input').length === 1 && l.querySelector('svg') && l.querySelector('.nm'));
    const st = ax();
    t('no_axis_switch', extra.length === 0 && !badge && kids && !(st && st.override), 'extra=' + extra.length + ' badge=' + badge + ' kids=' + kids);
  } catch (e) { t('no_axis_switch', false, e.message); }
  try {
    click(btnReset); await sleep(150);
    const st = ax(); const cleared = st === null || Object.keys(st.cfg || {}).length === 0;
    t('reset_button', cleared, 'cleared=' + cleared);
  } catch (e) { t('reset_button', false, e.message); }
  try {
    const ok = unnamed.length === 0;
    t('toolbar_labels', ok, 'unnamed=' + unnamed.length + ' total=' + toolbarEls.length);
  } catch (e) { t('toolbar_labels', false, e.message); }
  try {
    const ok = (width >= 1024 ? tbH < 120 : true) && tbH < 260;
    t('toolbar_height', ok, 'height=' + tbH + ' width=' + width);
  } catch (e) { t('toolbar_height', false, e.message); }
  try {
    W.localStorage.setItem('af.fs', '130');
    await new Promise(r => { f.onload = r; f.contentWindow.location.reload(); });
    await sleep(wait);
    const fs = W.getComputedStyle(f.contentDocument.documentElement).getPropertyValue('--fs').trim();
    const bodyFs = W.getComputedStyle(f.contentDocument.body).fontSize;
    const ok = fs === '1.3' && bodyFs === '18.2px';
    W.localStorage.removeItem('af.fs');
    t('font_setting', ok, 'fs=' + fs + ' bodyFs=' + bodyFs);
  } catch (e) { t('font_setting', false, e.message); }
  f.remove();
  return JSON.stringify({ total: res.length, failed: res.filter(r => !r.ok).map(r => r.n + ': ' + r.d), res })
})()
