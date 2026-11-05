(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const wait = P.wait || 2500;
    const sleep = ms => new Promise(r => setTimeout(r, ms));
  const res = [];
  const t = (n, cond, d) => { res.push({ n, ok: !!cond, d: d || '' }); };
    let cur = null;
  const open = async (path, theme) => {
    if (cur) { try { cur.f.remove(); } catch (e) {} cur = null; }
    try { localStorage.clear(); } catch (e) {}
    const f = document.createElement('iframe');
    f.style.cssText = 'position:fixed;left:0;top:0;border:0;opacity:0;pointer-events:none;width:' + width + 'px;height:1000px;';
    const parts = path.split('#');
    f.src = location.origin + parts[0] + (parts[0].indexOf('?') === -1 ? '?' : '&') + 'theme=' + (theme || 'light') + (parts[1] ? '#' + parts[1] : '');
    document.body.appendChild(f);
    await new Promise(r => { f.onload = r; setTimeout(r, 15000); });
    await sleep(wait);
    cur = { f, W: f.contentWindow, D: f.contentDocument };
    return cur;
  };
  const click = (el, W) => el.dispatchEvent(new W.MouseEvent('click', { bubbles: true, cancelable: true }));
  const visible = el => !!el && el.offsetParent !== null;
  const txt = (D, id) => (D.getElementById(id) ? D.getElementById(id).textContent : '');
  const st = (W, expr) => W.eval(expr);
  const cmode = async m => { await fetch('/__mock/clear?mode=' + m, { method: 'POST' }); };
  const clog = async () => (await fetch('/__mock/clear-log')).json();
  const typeIn = (el, v, W) => { el.value = v; el.dispatchEvent(new W.Event('input', { bubbles: true })); };
  try {
  const vis = el => !!el && !el.closest('[hidden]');
  try {
    await fetch('/__mock/meas?scenario=series_done', {method:'POST'});
    const {W, D} = await open('/measure');
    t('page_attr', D.documentElement.getAttribute('data-page') === 'measure');
    const tabs = Array.from(D.querySelectorAll('nav.tabs a')).map(a => a.textContent.trim());
    t('tabs_order', tabs.join('|') === 'Главная|Показания|Измерения|Система', tabs.join('|'));
    const mLink = D.querySelector('nav.tabs a[data-nav="measure"]');
    t('aria_current', mLink && mLink.getAttribute('aria-current') === 'page');
    t('tbl_rows', D.querySelectorAll('#msTbl tr').length === 6, String(D.querySelectorAll('#msTbl tr').length));
    t('sum_rows', D.querySelectorAll('#msSum .kv').length > 0);
    const csvHref = D.querySelector('#msCsv') ? D.querySelector('#msCsv').getAttribute('href') : '';
    t('csv_href', typeof csvHref === 'string' && csvHref.indexOf('/api/meas/results.csv?id=') === 0, csvHref);
  } catch (e) { t('g1', false, e.message); }
  
  try {
    await fetch('/__mock/meas?scenario=empty', {method:'POST'});
    const {W, D} = await open('/measure');
    const single = D.querySelector('[data-ms="single"]');
    t('single_visible', single && !single.hasAttribute('hidden'));
    t('single_time_visible', !D.querySelector('[data-ms="time"]').hasAttribute('hidden'));
    D.querySelector('#msMode button[data-mode="series"]').click();
    await sleep(150);
    t('series_n_visible', !D.querySelector('[data-ms="series"]').hasAttribute('hidden'));
    t('series_pause_visible', !D.querySelector('[data-ms="gap"]').hasAttribute('hidden'));
    t('series_single_hidden', D.querySelector('[data-ms="single"]').hasAttribute('hidden'));
    D.querySelector('#msMode button[data-mode="endless"]').click();
    await sleep(150);
    t('endless_n_hidden', D.querySelector('[data-ms="series"]').hasAttribute('hidden'));
    D.querySelector('#msMode button[data-mode="single"]').click();
    await sleep(150);
    D.querySelector('#msStop button[data-stop="sigma"]').click();
    await sleep(150);
    t('sigma_visible', !D.querySelector('[data-ms="sigma"]').hasAttribute('hidden'));
    t('time_hidden', D.querySelector('[data-ms="time"]').hasAttribute('hidden'));
    D.querySelector('#msMode button[data-mode="series"]').click();
    await sleep(150);
    t('stopmode_time', W.eval('msF.stopMode') === 'time', String(W.eval('msF.stopMode')));
  } catch (e) { t('g2', false, e.message); }
  
  try {
    await fetch('/__mock/meas?scenario=empty', {method:'POST'});
    const {W, D} = await open('/measure');
    D.querySelector('#msMode button[data-mode="series"]').click();
    await sleep(150);
    typeIn(D.querySelector('#msN'), '3', W);
    typeIn(D.querySelector('#msH'), '0', W);
    typeIn(D.querySelector('#msM'), '0', W);
    typeIn(D.querySelector('#msS'), '30', W);
    typeIn(D.querySelector('#msPs'), '5', W);
    typeIn(D.querySelector('#msName'), 'Проба', W);
    await sleep(150);
    D.querySelector('#msGo').click();
    await sleep(1500);
    const log = (await (await fetch('/__mock/meas-log')).json());
    const last = log[log.length - 1];
    t('req_path', last && last.path === '/api/meas/start', last && last.path);
    t('req_csrf', last && last.csrf && String(last.csrf).length > 0);
    const body = last ? JSON.stringify(last.body) : '';
    t('req_n', body.indexOf('"n":3') !== -1 || body.indexOf('n:3') !== -1, body);
    t('req_ts', body.indexOf('"t_s":30') !== -1 || body.indexOf('t_s:30') !== -1);
    t('req_pause', body.indexOf('"pause_s":5') !== -1 || body.indexOf('pause_s:5') !== -1);
    t('req_name', body.indexOf('Проба') !== -1);
    t('req_stopmode', body.indexOf('"stop_mode":"time"') !== -1 || body.indexOf('stop_mode:time') !== -1);
    t('req_no_eps', body.indexOf('eps_pct') === -1);
    t('req_no_endless', body.indexOf('endless') === -1);
    t('result_started', (txt(D, 'msResult') || '').indexOf('запущено') !== -1, txt(D, 'msResult'));
    t('go_disabled', D.querySelector('#msGo').disabled);
    await sleep(3500);
    const badge = D.querySelector('#msBadge');
    t('badge_visible', badge && !badge.hasAttribute('hidden'));
    t('badge_text', badge && badge.textContent.trim() === '0/3', badge && badge.textContent.trim());
  } catch (e) { t('g3', false, e.message); }
  
  try {
    await fetch('/__mock/meas?scenario=empty', {method:'POST'});
    const {W, D} = await open('/measure');
    D.querySelector('#msMode button[data-mode="single"]').click();
    await sleep(150);
    D.querySelector('#msStop button[data-stop="sigma"]').click();
    await sleep(150);
    typeIn(D.querySelector('#msEps'), '2', W);
    typeIn(D.querySelector('#msTmin'), '10', W);
    typeIn(D.querySelector('#msTmax'), '600', W);
    await sleep(150);
    D.querySelector('#msGo').click();
    await sleep(1500);
    const log = (await (await fetch('/__mock/meas-log')).json());
    const last = log[log.length - 1];
    const body = last ? JSON.stringify(last.body) : '';
    t('s_stopmode', body.indexOf('"stop_mode":"sigma"') !== -1 || body.indexOf('stop_mode:sigma') !== -1, body);
    t('s_eps', body.indexOf('"eps_pct":2') !== -1 || body.indexOf('eps_pct:2') !== -1);
    t('s_tmin', body.indexOf('"t_min_s":10') !== -1 || body.indexOf('t_min_s:10') !== -1);
    t('s_tmax', body.indexOf('"t_max_s":600') !== -1 || body.indexOf('t_max_s:600') !== -1);
    t('s_no_ts', body.indexOf('t_s') === -1);
    t('s_n1', body.indexOf('"n":1') !== -1 || body.indexOf('n:1') !== -1);
  } catch (e) { t('g4', false, e.message); }
  
  try {
    await fetch('/__mock/meas?scenario=empty', {method:'POST'});
    const {W, D} = await open('/measure');
    D.querySelector('#msMode button[data-mode="series"]').click();
    await sleep(150);
    typeIn(D.querySelector('#msN'), '1', W);
    await sleep(150);
    D.querySelector('#msGo').click();
    await sleep(300);
    const errs = Array.from(D.querySelectorAll('#msErrs li')).map(li => li.textContent).join(' ');
    t('err_n', errs.indexOf('измерени') !== -1 || errs.indexOf('число') !== -1, errs);
    const log = (await (await fetch('/__mock/meas-log')).json());
    t('no_start', log.length === 0, String(log.length));
  } catch (e) { t('g5', false, e.message); }
  
  try {
    await fetch('/__mock/meas?scenario=running', {method:'POST'});
    const {W, D} = await open('/measure');
    W.eval('msGo()');
    await sleep(300);
    t('busy_msg', (txt(D, 'msResult') || '').indexOf('уже идёт') !== -1, txt(D, 'msResult'));
    await fetch('/__mock/meas?scenario=notime', {method:'POST'});
    const {W: W2, D: D2} = await open('/measure');
    const sel = D2.querySelector('#msStart');
    sel.value = 'delay';
    sel.dispatchEvent(new Event('change', {bubbles: true}));
    await sleep(150);
    W2.eval('msGo()');
    await sleep(300);
    t('notime_msg', (txt(D2, 'msResult') || '').indexOf('не синхронизировано') !== -1, txt(D2, 'msResult'));
    const opts = Array.from(D2.querySelectorAll('#msStart option'));
    const atOpt = opts.find(o => o.value === 'at');
    const delayOpt = opts.find(o => o.value === 'delay');
    t('at_disabled', atOpt && atOpt.disabled);
    t('delay_disabled', delayOpt && delayOpt.disabled);
  } catch (e) { t('g6', false, e.message); }
  
  try {
    await fetch('/__mock/meas?scenario=empty', {method:'POST'});
    const {W, D} = await open('/measure');
    W.eval("fetchJSON = (function (o) { return async function (u, a) { if (String(u).indexOf('/api/csrf-token') === 0) { return {}; } return o(u, a); }; })(fetchJSON)");
    D.querySelector('#msMode button[data-mode="series"]').click();
    await sleep(150);
    typeIn(D.querySelector('#msN'), '2', W);
    typeIn(D.querySelector('#msS'), '30', W);
    await sleep(150);
    D.querySelector('#msGo').click();
    await sleep(500);
    t('csrf_msg', (txt(D, 'msResult') || '').indexOf('CSRF') !== -1, txt(D, 'msResult'));
    const log = (await (await fetch('/__mock/meas-log')).json());
    t('csrf_empty', log.length === 0, String(log.length));
  } catch (e) { t('g7', false, e.message); }
  
  try {
    await fetch('/__mock/meas?scenario=running', {method:'POST'});
    const {W, D} = await open('/measure');
    t('pause_en', !D.querySelector('#msPause').disabled);
    t('resume_dis', D.querySelector('#msResume').disabled);
    D.querySelector('#msPause').click();
    await sleep(1500);
    t('resume_en', !D.querySelector('#msResume').disabled);
    D.querySelector('#msResume').click();
    await sleep(300);
    D.querySelector('#msStopBtn').click();
    await sleep(500);
    const log = (await (await fetch('/__mock/meas-log')).json());
    const paths = log.map(r => r.path);
    const iPause = paths.indexOf('/api/meas/pause');
    const iResume = paths.indexOf('/api/meas/resume');
    const iStop = paths.indexOf('/api/meas/stop');
    t('order', iPause !== -1 && iResume !== -1 && iStop !== -1 && iPause < iResume && iResume < iStop, paths.join(','));
    const ok = log.every(r => {
      const b = r.body;
      const id = (typeof b === 'object' && b !== null) ? b.id : (typeof b === 'string' ? (JSON.parse(b).id) : undefined);
      return typeof id === 'number';
    });
    t('body_id_num', ok);
  } catch (e) { t('g8', false, e.message); }
  
  try {
    await fetch('/__mock/meas?scenario=series_done', {method:'POST'});
    const {W, D} = await open('/measure');
    t('del_en', !D.querySelector('#msDel').disabled);
    D.querySelector('#msDel').click();
    await sleep(200);
    t('dlg_open', !D.querySelector('#msDlg').hasAttribute('hidden'));
    let log = (await (await fetch('/__mock/meas-log')).json());
    t('no_del_yet', log.filter(r => r.path === '/api/meas/delete').length === 0);
    D.querySelector('#msDlgCancel').click();
    await sleep(200);
    t('dlg_closed', D.querySelector('#msDlg').hasAttribute('hidden'));
    log = (await (await fetch('/__mock/meas-log')).json());
    t('log_empty', log.length === 0, String(log.length));
    D.querySelector('#msDel').click();
    await sleep(200);
    D.querySelector('#msDlgOk').click();
    await sleep(1500);
    log = (await (await fetch('/__mock/meas-log')).json());
    const del = log.find(r => r.path === '/api/meas/delete');
    t('del_logged', !!del);
    const delId = del ? (typeof del.body === 'object' ? del.body.id : JSON.parse(del.body).id) : undefined;
    t('del_id_num', typeof delId === 'number');
    const rows = D.querySelectorAll('#msTbl tr').length;
    const note = (txt(D, 'msTblNote') || '').toLowerCase();
    t('run_removed', !Array.from(D.querySelectorAll('#msRun option')).some(o => o.value === String(delId)), 'rows=' + rows + ' note=' + note);
  } catch (e) { t('g9', false, e.message); }
  
  try {
    await fetch('/__mock/meas?scenario=series_done', {method:'POST'});
    const {W, D} = await open('/measure');
    t('svg_present', !!D.querySelector('#msPlot svg'));
    t('msinv_ge0', D.querySelectorAll('#msPlot circle.msinv').length >= 0);
    const dataRows = Array.from(D.querySelectorAll('#msTbl tr')).slice(1);
    let numeric = 0;
    dataRows.forEach(tr => {
      const cells = Array.from(tr.querySelectorAll('td'));
      if (cells.some(td => /^\d/.test(td.textContent.trim()))) numeric++;
    });
    const pts = D.querySelectorAll('#msPlot circle.mspt').length;
    t('pts_match', pts === numeric, 'pts=' + pts + ' numeric=' + numeric);
    D.querySelector('#msView button[data-vw="hist"]').click();
    await sleep(200);
    t('hist_rects', D.querySelectorAll('#msPlot rect.mshist').length > 0);
    D.querySelector('#msView button[data-vw="dose"]').click();
    await sleep(200);
    t('dose_note', (txt(D, 'msPlotNote') || '').toLowerCase().indexOf('доза') !== -1, txt(D, 'msPlotNote'));
    D.querySelector('#msView button[data-vw="line"]').click();
    await sleep(200);
    D.querySelector('#msFld button[data-fld="usv_h"]').click();
    await sleep(200);
    t('usv_note', (txt(D, 'msPlotNote') || '').indexOf('мкЗв/ч') !== -1, txt(D, 'msPlotNote'));
    await fetch('/__mock/meas?scenario=empty', {method:'POST'});
    const {W: W2, D: D2} = await open('/measure');
    t('empty_note', (txt(D2, 'msPlotNote') || '').indexOf('н/д') !== -1, txt(D2, 'msPlotNote'));
    t('empty_no_svg', !D2.querySelector('#msPlot svg'));
  } catch (e) { t('g10', false, e.message); }
  
  try {
    await fetch('/__mock/meas?scenario=big', {method:'POST'});
    const {W, D} = await open('/measure');
    const rows = D.querySelectorAll('#msTbl tr').length;
    t('big_rows', rows <= 201, String(rows));
    t('big_note', (txt(D, 'msTblNote') || '').indexOf('Показаны последние') !== -1, txt(D, 'msTblNote'));
    t('big_svg', !!D.querySelector('#msPlot svg'));
  } catch (e) { t('g11', false, e.message); }
  
  try {
    await fetch('/__mock/meas?scenario=empty', {method:'POST'});
    const {W, D} = await open('/measure');
    t('sum_empty', D.querySelectorAll('#msSum .kv').length === 0);
    t('del_dis', D.querySelector('#msDel').disabled);
    const csvHref = D.querySelector('#msCsv') ? D.querySelector('#msCsv').getAttribute('href') : '';
    t('csv_no_href', !csvHref || csvHref === '' || csvHref === 'null');
  } catch (e) { t('g12', false, e.message); }
  
  try {
    await fetch('/__mock/meas?scenario=running', {method:'POST'});
    const {W, D} = await open('/');
    const badge = D.querySelector('#msBadge');
    t('badge_exists', !!badge);
    t('badge_not_hidden', badge && !badge.hasAttribute('hidden'));
    t('no_measureview', !D.querySelector('#measureView'));
  } catch (e) { t('g13', false, e.message); }
    try {
      const reset = async n => { await fetch('/__mock/meas?scenario=' + n, {method:'POST'}); };
      await reset('running');
      let o = await open('/measure');
      t('del_disabled_while_running', o.D.getElementById('msDel').disabled === true);
      await reset('empty');
      o = await open('/measure');
      t('stop_disabled_idle', o.D.getElementById('msStopBtn').disabled === true);
      t('pause_disabled_idle', o.D.getElementById('msPause').disabled === true);
      t('timehint_hidden_synced', o.D.getElementById('msTimeHint').hidden === true);
      await reset('notime');
      o = await open('/measure');
      t('timehint_visible_notime', o.D.getElementById('msTimeHint').hidden === false);
      await reset('series_done');
      o = await open('/');
      const bd = o.D.getElementById('msBadge');
      t('badge_hidden_idle', !!bd && bd.hidden === true);
    } catch (e) { t('g14', false, e.message); }
  } catch (e) { t('outer', false, e.message); }
  const failed = [];
  for (const r of res) { if (!r.ok) failed.push(r.n + ': ' + r.d); }
  return JSON.stringify({ total: res.length, failed, res });
})()
