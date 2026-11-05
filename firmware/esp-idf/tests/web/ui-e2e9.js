(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const wait = P.wait || 2500;
  const A0 = 1792853245;
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const res = [];
  const t = (n, cond, d) => { res.push({ n, ok: !!cond, d: d || '' }); };
  const cfg = q => fetch('/__mock/hist?' + q, { method: 'POST' });
  const hlog = async () => (await fetch('/__mock/hist-log')).json();
  let cur = null;
  const open = async (path, pre) => {
    if (cur) { try { cur.f.remove(); } catch (e) {} cur = null; }
    try { localStorage.clear(); if (pre !== undefined) localStorage.setItem('afh.range.v1', pre); } catch (e) {}
    const f = document.createElement('iframe');
    f.style.cssText = 'position:fixed;left:0;top:0;border:0;opacity:0;pointer-events:none;width:' + width + 'px;height:1000px;';
    const parts = path.split('#');
    f.src = location.origin + parts[0] + (parts[0].indexOf('?') === -1 ? '?' : '&') + 'theme=light' + (parts[1] ? '#' + parts[1] : '');
    document.body.appendChild(f);
    await new Promise(r => { f.onload = r; setTimeout(r, 15000); });
    await sleep(wait);
    cur = { f, W: f.contentWindow, D: f.contentDocument };
    return cur;
  };
  const waitFor = async (fn, ms) => { const t0 = Date.now(); while (Date.now() - t0 < ms) { if (fn()) return true; await sleep(300); } return false; };
  const click = (el, W) => el.dispatchEvent(new W.MouseEvent('click', { bubbles: true, cancelable: true }));
  const typeIn = (el, v, W) => { el.value = v; el.dispatchEvent(new W.Event('input', { bubbles: true })); };
  const visible = el => !!el && el.offsetParent !== null;
  const txt = (D, id) => (D.getElementById(id) ? D.getElementById(id).textContent : '');
  const st = (W, expr) => W.eval(expr);
  const store = W => { try { return JSON.parse(W.localStorage.getItem('afh.range.v1')); } catch (e) { return null; } };
  const setRange = (D, W, from, to) => { typeIn(D.getElementById('rgFrom'), W.AFRG.toInput(from), W); typeIn(D.getElementById('rgTo'), W.AFRG.toInput(to), W); };
  const ptr = (W, el, type, x) => { const r = el.getBoundingClientRect(); el.dispatchEvent(new W.PointerEvent(type, { bubbles: true, cancelable: true, pointerId: 1, clientX: r.left + x, clientY: r.top + 60 })); };
  const qOf = (log, i) => new URLSearchParams(log.q[i]);
  const mainQ = log => log.q.filter(x => x.indexOf('kind=env') === -1);
  const fl = v => v - (v % 60);
  try {
    await cfg('delay=0');
    const { W, D } = await open('/');
    const btns = Array.from(D.querySelectorAll('#segWin button'));
    const rects = btns.map(b => b.getBoundingClientRect());
    const cs = btns.map(b => W.getComputedStyle(b));
    const minH = width < 768 ? 40 : 32;
    t('segment_has_interval_item', btns.length === 5 && btns[4].id === 'btnRange' && btns[4].textContent.trim() === 'Интервал' && !!btns[4].querySelector('svg'), btns.map(b => b.textContent.trim()).join('|'));
    t('segment_equal_padding_and_borders', cs.every(c => c.paddingLeft === cs[0].paddingLeft && c.paddingRight === cs[0].paddingRight && c.borderLeftWidth === cs[0].borderLeftWidth && c.borderTopWidth === cs[0].borderTopWidth), cs.map(c => c.paddingLeft + '/' + c.borderLeftWidth).join(' '));
    t('segment_visible_gaps_between_items', rects.every((r, i) => i === 0 || r.top !== rects[i - 1].top || Math.abs(r.left - rects[i - 1].right - 1) < 0.6), rects.map(r => Math.round(r.left) + '-' + Math.round(r.right)).join(' '));
    t('segment_targets', rects.every(r => r.height >= minH && r.width >= 44), rects.map(r => Math.round(r.width) + 'x' + Math.round(r.height)).join(' '));
    t('segment_same_height_per_row', rects.every(r => Math.abs(r.height - rects[0].height) < 0.6), '');
    t('toolbar_extra_buttons', D.getElementById('btnPick').getBoundingClientRect().height >= minH && !visible(D.getElementById('btnFull')) && !visible(D.getElementById('rangeLab')), '');
  } catch (e) { t('segment_group', false, e.message); }
  try {
    const { W, D } = await open('/');
    const panel = D.getElementById('rangePanel');
    click(D.getElementById('btnRange'), W);
    const presets = Array.from(D.querySelectorAll('#rgPresets button')).map(b => b.textContent);
    t('panel_opens_with_defaults', visible(panel) && D.getElementById('btnRange').getAttribute('aria-expanded') === 'true' && D.getElementById('rgFrom').value.length === 16 && D.getElementById('rgTo').value.length === 16 && presets.join('|') === 'Вчера|Сегодня|Последние 3 дня|Последние 7 дней|Прошлая неделя', presets.join('|'));
    t('panel_has_last_n_and_actions', !!D.getElementById('rgN') && D.getElementById('rgUnit').options.length === 3 && !!D.getElementById('rgApply') && !!D.getElementById('rgCancel') && !!D.getElementById('rgCopy'), '');
    D.dispatchEvent(new W.KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    t('escape_closes_panel', !visible(panel) && D.getElementById('btnRange').getAttribute('aria-expanded') === 'false', '');
    click(D.getElementById('btnRange'), W);
    click(D.getElementById('rgCancel'), W);
    t('cancel_closes_panel_without_request', !visible(panel) && (await hlog()).q.length === 0, '');
  } catch (e) { t('panel_group', false, e.message); }
  try {
    const { W, D } = await open('/');
    click(D.getElementById('btnRange'), W);
    const now = Math.floor(Date.now() / 1000);
    setRange(D, W, A0 + 3600, A0 + 600);
    t('invalid_order_blocks_apply', D.getElementById('rgApply').disabled === true && txt(D, 'rgMsg').indexOf('раньше') !== -1 && D.getElementById('rgMsg').className.indexOf('bad') !== -1, txt(D, 'rgMsg'));
    setRange(D, W, now - 3600, now + 7200);
    t('future_end_blocks_apply', D.getElementById('rgApply').disabled === true && txt(D, 'rgMsg').indexOf('будущем') !== -1, txt(D, 'rgMsg'));
    typeIn(D.getElementById('rgFrom'), '', W);
    t('empty_field_blocks_apply', D.getElementById('rgApply').disabled === true, txt(D, 'rgMsg'));
    setRange(D, W, A0 + 600, A0 + 1800);
    t('valid_range_enables_apply', D.getElementById('rgApply').disabled === false && D.getElementById('rgMsg').className.indexOf('bad') === -1, txt(D, 'rgMsg'));
    setRange(D, W, now - 2 * 86400, now - 60);
    t('long_raw_interval_warns_before_request', D.getElementById('rgMsg').className.indexOf('warn') !== -1 && txt(D, 'rgMsg').indexOf('Агрегаты') !== -1 && (await hlog()).q.length === 0, txt(D, 'rgMsg'));
  } catch (e) { t('validation_group', false, e.message); }
  try {
    await cfg('delay=0');
    const { W, D } = await open('/');
    const status0 = (await hlog()).status;
    click(D.getElementById('btnRange'), W);
    const f = A0 + 600 - ((A0 + 600) % 60), tt = A0 + 1800 - ((A0 + 1800) % 60);
    setRange(D, W, f, tt);
    click(D.getElementById('rgApply'), W);
    await waitFor(() => (st(W, 'state.rows.length') > 0 && st(W, 'state.range') !== null && st(W, 'state.rows[state.rows.length - 1].ts') < tt), 8000);
    const log = await hlog();
    const q = log.q.length ? qOf(log, 0) : new URLSearchParams('');
    const rows = st(W, 'state.rows.length');
    const first = st(W, 'state.rows[0].ts');
    const last = st(W, 'state.rows[state.rows.length - 1].ts');
    t('apply_requests_from_to_in_ts', mainQ(log).length === 1 && q.get('from') === String(f) && q.get('to') === String(tt) && q.get('kind') === 'raw' && q.get('limit') === '4000', log.q.join(' '));
    t('apply_data_only_inside_interval', rows > 100 && first >= f && last < tt, rows + ' ' + first + ' ' + last);
    t('apply_marks_segment_and_label', D.getElementById('btnRange').getAttribute('aria-pressed') === 'true' && D.querySelectorAll('#segWin button[aria-pressed="true"]').length === 1 && visible(D.getElementById('rangeLab')) && txt(D, 'rangeLab') === W.AFRG.label(f, tt) && !visible(D.getElementById('rangePanel')), txt(D, 'rangeLab'));
    t('apply_shows_interval_under_chart', D.getElementById('summary').textContent.indexOf('Интервал: ' + W.AFRG.label(f, tt)) !== -1, D.getElementById('summary').textContent.slice(0, 80));
    const a = D.getElementById('auto');
    t('auto_refresh_disabled_in_range', a.checked === false && a.disabled === true && a.parentNode.title.indexOf('автообновление выключено') !== -1, a.checked + ' ' + a.disabled);
    await sleep(13000);
    const log2 = await hlog();
    t('no_polling_in_range', log2.status === log.status && mainQ(log2).length === 1, log.status + ' -> ' + log2.status);
    const s = store(W);
    t('range_saved_to_storage', s && s.v === 1 && s.active === true && s.last.from === f && s.last.to === tt, JSON.stringify(s));
    click(D.querySelector('#segWin button[data-win="21600"]'), W);
    await waitFor(() => st(W, 'state.range') === null, 5000);
    const s2 = store(W);
    t('window_button_leaves_range_and_restores_auto', st(W, 'state.range') === null && D.getElementById('btnRange').getAttribute('aria-pressed') === 'false' && D.querySelector('#segWin button[data-win="21600"]').getAttribute('aria-pressed') === 'true' && !visible(D.getElementById('rangeLab')) && a.disabled === false && a.checked === true && s2 && s2.active === false, JSON.stringify(s2));
    await sleep(7000);
    const log3 = await hlog();
    t('polling_resumes_after_range', log3.status > log2.status && status0 >= 0, log2.status + ' -> ' + log3.status);
  } catch (e) { t('apply_group', false, e.message); }
  try {
    await cfg('delay=0');
    const { W, D } = await open('/');
    click(D.getElementById('btnRange'), W);
    const pre = W.AFRG.presets(Math.floor(Date.now() / 1000)).find(p => p.id === 'yesterday');
    click(D.querySelector('#rgPresets button[data-preset="yesterday"]'), W);
    await waitFor(() => st(W, 'state.range') !== null, 5000);
    await sleep(1000);
    const log = await hlog();
    const q = log.q.length ? qOf(log, 0) : new URLSearchParams('');
    t('preset_applies_immediately', mainQ(log).length === 1 && q.get('from') === String(pre.from) && q.get('to') === String(pre.to) && txt(D, 'rangeLab') === W.AFRG.label(pre.from, pre.to), log.q.join(' '));
    click(D.getElementById('btnRange'), W);
    D.getElementById('rgN').value = '2';
    D.getElementById('rgUnit').value = 'h';
    click(D.getElementById('rgLast'), W);
    await sleep(1500);
    const log2 = await hlog();
    const q2 = new URLSearchParams(mainQ(log2).pop() || '');
    t('last_n_hours_applies', mainQ(log2).length === 2 && Math.abs(+q2.get('to') - Math.floor(Date.now() / 1000)) < 30 && +q2.get('to') - +q2.get('from') === 7200, log2.q.join(' '));
    t('last_n_saved', store(W) && store(W).lastN && store(W).lastN.n === 2 && store(W).lastN.unit === 'h', JSON.stringify(store(W)));
  } catch (e) { t('preset_group', false, e.message); }
  try {
    await cfg('delay=0');
    const f = A0 + 600, tt = A0 + 1800;
    const o = await open('/?from=' + f + '&to=' + tt);
    await waitFor(() => st(o.W, 'state.rows.length') > 0, 8000);
    const log = await hlog();
    t('url_range_applied_at_load', st(o.W, 'state.range.from') === f && st(o.W, 'state.range.to') === tt && mainQ(log).length === 1 && qOf(log, 0).get('from') === String(f) && o.D.getElementById('btnRange').getAttribute('aria-pressed') === 'true' && o.D.getElementById('auto').disabled === true, log.q.join(' '));
    const iso = await open('/?from=2026-10-24T10:00:00Z&to=2026-10-24T11:00:00Z');
    await sleep(1500);
    t('url_iso_range_applied', st(iso.W, 'state.range.to') - st(iso.W, 'state.range.from') === 3600 && st(iso.W, 'state.range.from') === Date.UTC(2026, 9, 24, 10) / 1000, JSON.stringify(st(iso.W, 'state.range')));
    await cfg('delay=0');
    const bad = await open('/?from=abc&to=5');
    await sleep(500);
    const logb = await hlog();
    t('url_garbage_ignored', st(bad.W, 'state.range') === null && logb.q.length === 0 && bad.D.querySelector('#segWin button[data-win="3600"]').getAttribute('aria-pressed') === 'true', JSON.stringify(st(bad.W, 'state.range')));
    const rev = await open('/?from=' + tt + '&to=' + f);
    t('url_reversed_ignored', st(rev.W, 'state.range') === null, '');
  } catch (e) { t('url_group', false, e.message); }
  try {
    await cfg('delay=0');
    const f = A0 + 600, tt = A0 + 1800;
    const o = await open('/', JSON.stringify({ v: 1, last: { from: f, to: tt }, active: true, lastN: null }));
    await waitFor(() => st(o.W, 'state.rows.length') > 0, 8000);
    t('stored_range_restored', st(o.W, 'state.range.from') === f && o.D.getElementById('btnRange').getAttribute('aria-pressed') === 'true' && st(o.W, 'state.rows.length') > 100, JSON.stringify(st(o.W, 'state.range')));
    const old = await open('/', JSON.stringify({ v: 1, last: { from: 1500000000, to: 1500003600 }, active: true, lastN: null }));
    await sleep(4000);
    t('stale_stored_range_falls_back_to_all', st(old.W, 'state.range') === null && st(old.W, 'state.windowSec') === 0 && old.D.querySelector('#segWin button[data-win="0"]').getAttribute('aria-pressed') === 'true' && old.D.getElementById('btnRange').getAttribute('aria-pressed') === 'false' && st(old.W, 'state.rows.length') > 0, JSON.stringify(st(old.W, 'state.range')) + ' win=' + st(old.W, 'state.windowSec'));
    const junk = await open('/', '{broken');
    t('broken_storage_ignored', st(junk.W, 'state.range') === null, '');
    const inactive = await open('/', JSON.stringify({ v: 1, last: { from: f, to: tt }, active: false, lastN: null }));
    t('inactive_storage_not_restored', st(inactive.W, 'state.range') === null, '');
  } catch (e) { t('restore_group', false, e.message); }
  try {
    await cfg('delay=1500');
    const { W, D } = await open('/');
    await cfg('delay=1500');
    const f1 = fl(A0 + 600), t1 = fl(A0 + 900), f2 = fl(A0 + 1800), t2 = fl(A0 + 2400);
    click(D.getElementById('btnRange'), W);
    setRange(D, W, f1, t1);
    click(D.getElementById('rgApply'), W);
    await sleep(200);
    click(D.getElementById('btnRange'), W);
    setRange(D, W, f2, t2);
    click(D.getElementById('rgApply'), W);
    await sleep(5000);
    const log = await hlog();
    const first = st(W, 'state.rows[0].ts');
    const last = st(W, 'state.rows[state.rows.length - 1].ts');
    t('latest_request_wins', mainQ(log).length === 2 && first >= f2 && last < t2 && st(W, 'state.range.from') === f2 && st(W, 'state.busy') === false, log.q.length + ' ' + first + ' ' + last);
    await cfg('delay=0');
  } catch (e) { t('latest_group', false, e.message); }
  try {
    await cfg('delay=0');
    const { W, D } = await open('/');
    const cv = D.getElementById('chart');
    click(D.getElementById('btnPick'), W);
    t('pick_mode_toggles', D.getElementById('btnPick').getAttribute('aria-pressed') === 'true' && cv.classList.contains('picking') && st(W, 'state.pick') === true, '');
    const w0 = st(W, 'state.view.t1 - state.view.t0');
    const r = cv.getBoundingClientRect();
    ptr(W, cv, 'pointerdown', r.width * 0.30);
    ptr(W, cv, 'pointermove', r.width * 0.45);
    t('pick_shows_box_while_dragging', visible(D.getElementById('pickBox')), '');
    ptr(W, cv, 'pointerup', r.width * 0.60);
    const w1 = st(W, 'state.view.t1 - state.view.t0');
    const log = await hlog();
    t('pick_zooms_inside_loaded_data', w1 > 0 && w1 < w0 * 0.7 && log.q.length === 0 && st(W, 'state.pick') === false && !visible(D.getElementById('pickBox')) && D.getElementById('btnPick').getAttribute('aria-pressed') === 'false', w0 + ' -> ' + w1);
    click(D.getElementById('btnPick'), W);
    D.dispatchEvent(new W.KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    t('escape_cancels_pick_mode', st(W, 'state.pick') === false, '');
    click(D.getElementById('btnPick'), W);
    ptr(W, cv, 'pointerdown', r.width * 0.30);
    ptr(W, cv, 'pointerup', r.width * 0.30);
    t('tiny_selection_ignored', st(W, 'state.view.t1 - state.view.t0') === w1, '');
  } catch (e) { t('pick_group', false, e.message); }
  try {
    await cfg('delay=0');
    const { W, D } = await open('/');
    const f = fl(A0 + 600), tt = fl(A0 + 3600);
    click(D.getElementById('btnRange'), W);
    setRange(D, W, f, tt);
    click(D.getElementById('rgApply'), W);
    await waitFor(() => st(W, 'state.rows.length') > 0, 8000);
    click(D.getElementById('btnRange'), W);
    click(D.getElementById('rgCopy'), W);
    await sleep(300);
    const link = txt(D, 'rgMsg');
    t('copy_link_has_range', link === location.origin + '/?from=' + f + '&to=' + tt && !!W.AFRG.parseUrl(link.slice(link.indexOf('?'))), link);
    const cv = D.getElementById('chart');
    const r = cv.getBoundingClientRect();
    click(D.getElementById('btnPick'), W);
    ptr(W, cv, 'pointerdown', r.width * 0.30);
    ptr(W, cv, 'pointerup', r.width * 0.60);
    t('pick_inside_range_zooms_without_request', mainQ(await hlog()).length === 1 && st(W, 'state.view.t1 - state.view.t0') < (tt - f) * 0.6, '');
    t('full_window_button_visible_in_range', visible(D.getElementById('btnFull')), '');
    click(D.getElementById('btnFull'), W);
    await waitFor(() => st(W, 'state.range') === null, 5000);
    t('full_window_button_leaves_range', st(W, 'state.range') === null && !visible(D.getElementById('btnFull')) && D.getElementById('btnRange').getAttribute('aria-pressed') === 'false', '');
  } catch (e) { t('link_group', false, e.message); }
  if (cur) { try { cur.f.remove(); } catch (e) {} cur = null; }
  try {
    await cfg('delay=0');
    const f = A0 + 600, tt = A0 + 1800;
    const { W, D } = await open('/?from=' + f + '&to=' + tt);
    await waitFor(() => st(W, 'state.rows.length') > 0 && st(W, 'state.rows[state.rows.length - 1].ts') < tt, 8000);
    click(D.getElementById('btnRefresh'), W);
    await sleep(1500);
    let log = await hlog();
    t('refresh_in_range_reloads_same_range', mainQ(log).length === 2 && qOf({ q: mainQ(log) }, 1).get('from') === String(f) && st(W, 'state.rows[state.rows.length - 1].ts') < tt && st(W, 'state.range.from') === f, mainQ(log).join(' '));
    click(D.querySelector('#segKind button[data-kind="agg"]'), W);
    await sleep(1500);
    log = await hlog();
    const last = qOf({ q: mainQ(log) }, mainQ(log).length - 1);
    t('kind_switch_in_range_requests_agg_for_same_interval', last.get('kind') === 'agg' && last.get('from') === String(f) && last.get('to') === String(tt) && st(W, 'state.range') !== null, mainQ(log).join(' '));
  } catch (e) { t('refresh_group', false, e.message); }
  const failed = [];
  for (const r of res) { if (!r.ok) failed.push(r.n + ': ' + r.d); }
  return JSON.stringify({ total: res.length, failed, res });
})()
