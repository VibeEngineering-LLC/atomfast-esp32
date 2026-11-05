(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const wait = P.wait || 2500;
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  try { localStorage.clear(); } catch (e) {}
  const f = document.createElement('iframe');
  f.style.cssText = 'position:fixed;left:0;top:0;border:0;opacity:0;pointer-events:none;width:' + width + 'px;height:900px;';
  f.src = location.origin + '/?theme=light&vis=cps,air_p,air_t';
  document.body.appendChild(f);
  await new Promise(r => { f.onload = r; });
  await sleep(wait);
  const W = f.contentWindow;
  const D = f.contentDocument;
  const q = s => D.querySelector(s);
  const qa = s => D.querySelectorAll(s);
  const canvas = q('#chart');
  const rect = () => canvas.getBoundingClientRect();
  const click = el => el.dispatchEvent(new W.MouseEvent('click', { bubbles: true, cancelable: true }));
  const ptr = (type, x, y) => canvas.dispatchEvent(new W.MouseEvent(type, { clientX: rect().left + x, clientY: rect().top + y, bubbles: true }));
  const ax = () => JSON.parse(W.localStorage.getItem('afh.axes.v1') || 'null');
  const cfgKeys = () => Object.keys((ax() || {}).cfg || {}).sort();
  const stripX = id => { const a = W.axModel.axes.find(z => z.id === id); const sw = W.axisSlotsWidth(); const L = W.layout(); return a.side === 'L' ? L.left - (a.slot + 0.5) * sw : canvas.clientWidth - L.right + (a.slot + 0.5) * sw; };
  const pick = async id => { ptr('pointerdown', stripX(id), 150); ptr('pointerup', stripX(id), 150); await sleep(150); };
  const t = (n, cond, d) => { if (!cond) failed.push(n + ': ' + d); };
  const failed = [];
  try {
    const bar = q('#axbar');
    t('select_by_strip', bar && bar.hidden === true, 'bar not hidden before');
    await pick('temp');
    const bar2 = q('#axbar');
    const name = q('#axbName');
    const mode = q('#axbMode');
    t('select_by_strip', bar2 && bar2.hidden === false, 'bar still hidden after pick');
    t('select_by_strip', name && name.textContent.includes('Ось:') && name.textContent.includes('°C'), 'name wrong: ' + (name ? name.textContent : 'null'));
    t('select_by_strip', mode && mode.textContent === 'авто', 'mode wrong: ' + (mode ? mode.textContent : 'null'));
  } catch (e) { failed.push('select_by_strip: ' + e.message); }
  try {
    const before = W.axModel.axes.find(a => a.id === 'temp');
    const beforeRange = before.max - before.min;
    click(q('#axbPlus'));
    await sleep(150);
    const after = W.axModel.axes.find(a => a.id === 'temp');
    const afterRange = after.max - after.min;
    t('plus_only_selected', JSON.stringify(cfgKeys()) === JSON.stringify(['temp']), 'keys: ' + JSON.stringify(cfgKeys()));
    t('plus_only_selected', ax().cfg.temp.mode === 'manual', 'mode: ' + ax().cfg.temp.mode);
    t('plus_only_selected', q('#axbMode').textContent === 'ручной', 'mode text: ' + q('#axbMode').textContent);
    t('plus_only_selected', afterRange < 0.9 * beforeRange, 'range not reduced: ' + afterRange + ' vs ' + beforeRange);
  } catch (e) { failed.push('plus_only_selected: ' + e.message); }
  try {
    const count = W.axModel.axes.find(a => a.id === 'count');
    t('other_axes_untouched', count.mode === 'auto', 'count mode: ' + count.mode);
    t('other_axes_untouched', !('count' in (ax().cfg || {})), 'count in cfg');
  } catch (e) { failed.push('other_axes_untouched: ' + e.message); }
  try {
    const before = W.axModel.axes.find(a => a.id === 'temp');
    const bMin = before.min, bMax = before.max;
    W.redraw(); W.redraw(); W.redraw();
    await sleep(150);
    const after = W.axModel.axes.find(a => a.id === 'temp');
    t('manual_survives_redraw', after.min === bMin && after.max === bMax, 'min/max changed: ' + after.min + ',' + after.max);
    const press = W.axModel.axes.find(a => a.id === 'press');
    t('manual_survives_redraw', press.mode === 'auto', 'press mode: ' + press.mode);
  } catch (e) { failed.push('manual_survives_redraw: ' + e.message); }
  try {
    const all = q('#axbAll');
    all.checked = true;
    all.dispatchEvent(new W.Event('change', { bubbles: true }));
    await sleep(100);
    click(q('#axbMinus'));
    await sleep(150);
    const keys = cfgKeys();
    t('all_axes_checkbox', keys.includes('count') && keys.includes('press') && keys.includes('temp'), 'keys: ' + JSON.stringify(keys));
    all.checked = false;
    all.dispatchEvent(new W.Event('change', { bubbles: true }));
    await sleep(100);
  } catch (e) { failed.push('all_axes_checkbox: ' + e.message); }
  try {
    await pick('press');
    click(q('#axbFit'));
    await sleep(150);
    const cfg = ax().cfg || {};
    const pressCfg = cfg.press;
    const pressAxis = W.axModel.axes.find(a => a.id === 'press');
    t('fit_selected', pressCfg === undefined || (pressCfg.mode === 'auto' && Object.keys(pressCfg).length === 1), 'press cfg: ' + JSON.stringify(pressCfg));
    t('fit_selected', pressAxis.mode === 'auto', 'press mode: ' + pressAxis.mode);
  } catch (e) { failed.push('fit_selected: ' + e.message); }
  try {
    await pick('temp');
    canvas.dispatchEvent(new W.MouseEvent('dblclick', { clientX: rect().left + stripX('temp'), clientY: rect().top + 150, bubbles: true }));
    await sleep(150);
    const temp = W.axModel.axes.find(a => a.id === 'temp');
    t('dblclick_strip_auto', temp.mode === 'auto', 'temp mode: ' + temp.mode);
  } catch (e) { failed.push('dblclick_strip_auto: ' + e.message); }
  try {
    canvas.dispatchEvent(new W.MouseEvent('dblclick', { clientX: rect().left + stripX('temp'), clientY: rect().top + 4, bubbles: true }));
    await sleep(150);
    const edit = q('#axedit');
    t('edit_invalid_then_valid', edit && edit.hidden === false, 'edit not visible');
    q('#axeMin').value = '5';
    q('#axeMax').value = '1';
    edit.dispatchEvent(new W.Event('submit', { bubbles: true, cancelable: true }));
    await sleep(150);
    const err = q('#axeErr');
    t('edit_invalid_then_valid', err && err.textContent.includes('min должен быть меньше max'), 'err: ' + (err ? err.textContent : 'null'));
    t('edit_invalid_then_valid', q('#axedit').hidden === false, 'form closed after invalid');
    q('#axeMin').value = '26,1';
    q('#axeMax').value = '26,9';
    q('#axedit').dispatchEvent(new W.Event('submit', { bubbles: true, cancelable: true }));
    await sleep(150);
    t('edit_invalid_then_valid', q('#axedit').hidden === true, 'form still visible after valid');
    const cfg = ax().cfg.temp;
    t('edit_invalid_then_valid', cfg && cfg.mode === 'manual' && cfg.min === 26.1 && cfg.max === 26.9, 'cfg: ' + JSON.stringify(cfg));
  } catch (e) { failed.push('edit_invalid_then_valid: ' + e.message); }
  try {
    await pick('count');
    click(q('#axbReset')); await sleep(150);
    t('toolbar_buttons_follow_selection', !('count' in (ax().cfg || {})), 'count not reset');
    click(q('#btnZoomIn'));
    await sleep(150);
    const cfg1 = ax().cfg || {};
    t('toolbar_buttons_follow_selection', cfg1.count && cfg1.count.mode === 'manual', 'count cfg: ' + JSON.stringify(cfg1.count));
    const keysBefore = JSON.stringify(cfgKeys());
    W.selAxis = null;
    W.allAxes = false;
    click(q('#btnZoomIn'));
    await sleep(150);
    const keysAfter = JSON.stringify(cfgKeys());
    t('toolbar_buttons_follow_selection', keysBefore === keysAfter, 'keys changed: ' + keysBefore + ' -> ' + keysAfter);
  } catch (e) { failed.push('toolbar_buttons_follow_selection: ' + e.message); }
  try {
    click(q('#btnAxes'));
    await sleep(150);
    const row = q('#axpanel .axrow[data-axis="press"] .axname');
    click(row);
    await sleep(150);
    const rowEl = q('#axpanel .axrow[data-axis="press"]');
    t('panel_row_select', rowEl && rowEl.classList.contains('axsel'), 'row not selected');
    const bar = q('#axbar');
    const name = q('#axbName');
    t('panel_row_select', bar && bar.hidden === false && name && name.textContent.includes('мм рт'), 'bar/name wrong: ' + (name ? name.textContent : 'null'));
  } catch (e) { failed.push('panel_row_select: ' + e.message); }
  try {
    await pick('temp');
    const hadCount = 'count' in (ax().cfg || {});
    click(q('#axbReset'));
    await sleep(150);
    const cfg = ax().cfg || {};
    t('reset_selected', !('temp' in cfg), 'temp still in cfg');
    if (hadCount) t('reset_selected', 'count' in cfg, 'count lost');
  } catch (e) { failed.push('reset_selected: ' + e.message); }
  try {
    click(q('#axpanel .axrow[data-axis="count"] button[data-mode="auto"]')); await sleep(150);
    const cb = q('#axpanel .axrow[data-axis="count"] .axzero');
    cb.checked = true;
    cb.dispatchEvent(new W.Event('change', { bubbles: true }));
    await sleep(150);
    const cfg = ax().cfg || {};
    t('zero_checkbox_panel', cfg.count && cfg.count.zero === true, 'count.zero: ' + JSON.stringify(cfg.count));
    t('zero_checkbox_panel', q('#axpanel .axrow[data-axis="count"] .axzero').checked === true, 'checkbox state lost after redraw');
    const countAxis = W.axModel.axes.find(a => a.id === 'count');
    t('zero_checkbox_panel', countAxis.min <= 0, 'min: ' + countAxis.min);
  } catch (e) { failed.push('zero_checkbox_panel: ' + e.message); }
  f.remove();
  return JSON.stringify({ total: 12, failed, res: failed.length === 0 ? 'ok' : 'fail' });
})()
