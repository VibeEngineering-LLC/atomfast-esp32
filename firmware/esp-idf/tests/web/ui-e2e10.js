(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const wait = P.wait || 2500;
    const sleep = ms => new Promise(r => setTimeout(r, ms));
  const res = [];
  const t = (n, cond, d) => { res.push({ n, ok: !!cond, d: d || '' }); };
  const scen = async s => { await fetch('/__mock/sensors?scenario=' + s, { method: 'POST' }); };
  const slog = async () => (await fetch('/__mock/sensors-log')).json();
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
  const posts = log => log.filter(x => x.method === 'POST');
  const click = (el, W) => el.dispatchEvent(new W.MouseEvent('click', { bubbles: true, cancelable: true }));
  const setSw = (inp, on, W) => { inp.checked = on; inp.dispatchEvent(new W.Event('change', { bubbles: true })); };
  const typeIn = (el, v, W) => { el.value = v; el.dispatchEvent(new W.Event('input', { bubbles: true })); };
  const visible = el => !!el && el.offsetParent !== null;
  const rowsOf = D => Array.from(D.querySelectorAll('#sensRows tr[data-sensor]'));
  const txt = (D, id) => (D.getElementById(id) ? D.getElementById(id).textContent : '');
  const st = (W, expr) => W.eval(expr);
  const rowOf = (D, n) => D.querySelector('#sensRows tr[data-sensor="' + n + '"]');
  const swOf = tr => tr.querySelector('input[type=checkbox]');
  try {
    await scen('three');
    const { W, D } = await open('/system#sensors');
    const rows = rowsOf(D);
    t('table_rows_and_groups', rows.length === 3 && D.querySelectorAll('#sensRows .sens-grp').length === 3 && visible(D.getElementById('sensTable')) && !visible(D.getElementById('sensEmpty')), rows.length + ' grp=' + D.querySelectorAll('#sensRows .sens-grp').length);
    t('summary_and_badge', txt(D, 'sensSummary') === 'Датчики: 3 (включено 3)'.replace('Датчики', 'Датчиков') && txt(D, 'nb_sensors') === '3', txt(D, 'sensSummary') + ' / ' + txt(D, 'nb_sensors'));
    const r2 = rowOf(D, 2).textContent;
    t('row_model_kinds_addr_values', r2.indexOf('SHT3x') >= 0 && r2.indexOf('T+RH') >= 0 && r2.indexOf('0x44') >= 0 && r2.indexOf('41.') >= 0 && rowOf(D, 1).textContent.indexOf('T+P') >= 0, r2);
    const order = Array.from(D.querySelectorAll('#sensRows tr')).map(tr => tr.classList.contains('sens-grp') ? 'G' : tr.dataset.sensor).join('');
    t('groups_sorted_by_model_then_number', order === 'G1G2G3', order);
    setSw(swOf(rowOf(D, 2)), false, W);
    await sleep(1500);
    const log = await slog();
    const pb = posts(log).filter(x => x.path === '/api/sensors');
    t('toggle_posts_sensor_and_enabled', pb.length === 1 && pb[0].body.sensor === 2 && pb[0].body.enabled === false && pb[0].csrf === 'mock', JSON.stringify(pb));
    t('toggle_updates_summary_badge_state', txt(D, 'sensSummary').indexOf('включено 2') >= 0 && txt(D, 'nb_sensors') === '2' && rowOf(D, 2).textContent.indexOf('выключен') >= 0, txt(D, 'sensSummary') + ' / ' + txt(D, 'nb_sensors'));
  } catch (e) { t('table_group', false, e.message); }
  try {
    await scen('three');
    const { W, D } = await open('/system#sensors');
    const g = D.querySelector('#sensRows .sens-grp[data-model="sht3x"] input[type=checkbox]');
    setSw(g, false, W);
    await sleep(1500);
    const log = await slog();
    const pm = posts(log).filter(x => x.path === '/api/sensors/model');
    t('model_toggle_posts_model', pm.length === 1 && pm[0].body.model === 'sht3x' && pm[0].body.enabled === false, JSON.stringify(pm));
    t('model_toggle_leaves_other_models', swOf(rowOf(D, 1)).checked === true && swOf(rowOf(D, 3)).checked === true && swOf(rowOf(D, 2)).checked === false, '');
  } catch (e) { t('model_group', false, e.message); }
  try {
    await scen('error');
    const { W, D } = await open('/system#sensors');
    const link = D.querySelector('#sidelist a[data-sec="sensors"]');
    t('error_badge_is_warn', link && link.getAttribute('data-level') === 'warn' && D.getElementById('nb_sensors').classList.contains('warn'), link && link.getAttribute('data-level'));
    t('error_row_text', rowOf(D, 2).textContent.indexOf('ошибка') >= 0 && rowOf(D, 2).textContent.indexOf('н/д') >= 0, rowOf(D, 2).textContent);
    const bc = n => rowOf(D, n).querySelector('.fw-badge').className;
    t('status_badge_classes', bc(1).split(' ').indexOf('ok') >= 0 && bc(2).split(' ').indexOf('warn') >= 0 && bc(3).split(' ').indexOf('ok') < 0, bc(1) + '|' + bc(2) + '|' + bc(3));
    const dels = Array.from(D.querySelectorAll('#sensRows button.danger'));
    t('remove_only_for_never_answered', dels.length === 1 && dels[0].closest('tr').dataset.sensor === '3', dels.length);
    W.confirm = () => false;
    click(dels[0], W);
    await sleep(800);
    t('remove_declined_no_post', posts(await slog()).length === 0, '');
    W.confirm = () => true;
    click(dels[0], W);
    await sleep(1500);
    const pr = posts(await slog());
    t('remove_confirmed_posts_remove', pr.length === 1 && pr[0].body.action === 'remove' && pr[0].body.sensor === 3 && !rowOf(D, 3), JSON.stringify(pr));
  } catch (e) { t('error_group', false, e.message); }
  try {
    await scen('none');
    const { W, D } = await open('/system#sensors');
    t('empty_state_with_scan', visible(D.getElementById('sensEmpty')) && !visible(D.getElementById('sensTable')) && visible(D.getElementById('btnScan')) && D.getElementById('sensCard').textContent.indexOf('4 КБ') >= 0 && txt(D, 'sensSummary') === 'Датчиков: 0 (включено 0)', txt(D, 'sensSummary'));
    click(D.getElementById('btnScan'), W);
    await sleep(1800);
    const ps = posts(await slog());
    t('scan_posts_and_reports', ps.length === 1 && ps[0].body.action === 'scan' && txt(D, 'sensScanMsg') === 'Найдено 2, новых 2' && rowsOf(D).length === 2, JSON.stringify(ps) + txt(D, 'sensScanMsg'));
  } catch (e) { t('scan_group', false, e.message); }
  try {
    await scen('one');
    const { W, D } = await open('/system#sensors');
    const mod = D.getElementById('addModel');
    t('add_form_lists_all_models', mod.options.length === 4 && D.getElementById('addAddr').options.length === 2, mod.options.length);
    mod.value = 'sht3x'; mod.dispatchEvent(new W.Event('change', { bubbles: true }));
    t('add_addr_follows_model', Array.from(D.getElementById('addAddr').options).map(o => o.value).join() === '0x44,0x45', '');
    mod.value = 'bmp280'; mod.dispatchEvent(new W.Event('change', { bubbles: true }));
    D.getElementById('addAddr').value = '0x76';
    click(D.getElementById('btnAdd'), W);
    await sleep(600);
    t('add_taken_address_refused_without_post', posts(await slog()).length === 0 && txt(D, 'sensAddMsg').indexOf('занят') >= 0, txt(D, 'sensAddMsg'));
    mod.value = 'sht3x'; mod.dispatchEvent(new W.Event('change', { bubbles: true }));
    D.getElementById('addAddr').value = '0x45';
    click(D.getElementById('btnAdd'), W);
    await sleep(1800);
    const pa = posts(await slog());
    t('add_posts_model_and_addr', pa.length === 1 && pa[0].body.action === 'add' && pa[0].body.model === 'sht3x' && pa[0].body.addr === '0x45' && pa[0].body.enabled === false && rowsOf(D).length === 2, JSON.stringify(pa));
  } catch (e) { t('add_group', false, e.message); }
  try {
    await scen('three');
    const { W, D } = await open('/system#sensors');
    const pick = n => click(rowOf(D, n).querySelector('button[aria-label^="Настроить"]'), W);
    const cap = c => { const l = D.querySelector('#sensCfgForm [data-cap="' + c + '"]'); return !!l && !l.hidden; };
    t('cfg_card_empty_hint_until_pick', visible(D.getElementById('sensCfgPick')) && !visible(D.getElementById('sensCfgForm')), '');
    pick(1);
    t('cfg_fields_follow_capabilities_bmp', cap('t') && cap('p') && !cap('h') && txt(D, 'sensCfgTitle').indexOf('№1') >= 0, txt(D, 'sensCfgTitle'));
    pick(2);
    t('cfg_fields_follow_capabilities_sht', cap('t') && !cap('p') && cap('h') && D.getElementById('cfgHOff').value === '0', txt(D, 'sensCfgTitle'));
    typeIn(D.getElementById('cfgTOff'), '0.5', W);
    click(D.getElementById('btnCfgSave'), W);
    await sleep(1800);
    const pc = posts(await slog()).filter(x => x.body.sensor === 2 && x.body.action === undefined);
    t('cfg_save_posts_patch_for_selected', pc.length === 1 && pc[0].body.t_off === 0.5 && pc[0].body.h_off === 0 && pc[0].body.p_off === undefined, JSON.stringify(pc));
    typeIn(D.getElementById('cfgTOff'), '9', W);
    click(D.getElementById('btnCfgSave'), W);
    await sleep(600);
    t('cfg_invalid_value_blocked', /^(?!сохранено)/.test(txt(D, 'cfgResult')) && txt(D, 'cfgResult').length > 0 && posts(await slog()).filter(x => x.body.t_off === 9).length === 0, txt(D, 'cfgResult'));
  } catch (e) { t('cfg_group', false, e.message); }
  try {
    await scen('three');
    const { W, D } = await open('/readings');
    const tb = D.getElementById('envTable');
    const trs = tb ? Array.from(tb.querySelectorAll('tbody tr')) : [];
    t('readings_env_table_per_sensor', trs.length === 3 && tb.querySelector('th').textContent === 'Датчик' && trs[0].cells[0].textContent.indexOf('№') < 0 && trs[0].cells[0].textContent.indexOf('BMP280') >= 0 && trs[1].cells[0].textContent.indexOf('SHT3x') >= 0, trs.map(r => r.cells[0].textContent).join('|'));
    t('readings_env_cells_by_capability', trs[0].querySelector('[data-kind="h"]').textContent === '—' && trs[1].querySelector('[data-kind="p"]').textContent === '—' && /^\d+\.\d( ± н\/д)?$/.test(trs[1].querySelector('[data-kind="h"]').textContent) && /^\d+\.\d( ± н\/д)?$/.test(trs[0].querySelector('[data-kind="t"]').textContent), trs.map(r => r.textContent).join('|'));
    t('readings_old_air_rows_removed_in_multi', !Array.from(D.querySelectorAll('#readings .kv .k')).some(k => k.textContent.indexOf('Температура среды') === 0), '');
  } catch (e) { t('readings_multi_group', false, e.message); }
  try {
    await scen('one');
    const { W, D } = await open('/readings');
    const ks = Array.from(D.querySelectorAll('#readings .kv .k')).map(k => k.textContent);
    t('readings_single_old_view_with_caption', !D.getElementById('envTable') && ks.some(k => k.indexOf('Температура среды') === 0) && txt(D, 'envCaption') === 'BMP280 (0x76), датчик 1', ks.join('|') + txt(D, 'envCaption'));
    await scen('all_off');
    const o = await open('/readings');
    t('readings_all_off_message', !o.D.getElementById('envTable') && o.D.getElementById('readings').textContent.indexOf('нет включённых') >= 0, '');
  } catch (e) { t('readings_single_group', false, e.message); }
  try {
    await scen('three');
    const { W, D } = await open('/');
    const ids = st(W, "JSON.stringify(SERIES.filter(function (d) { return d.envKind; }).map(function (d) { return d.id; }))");
    t('chart_series_per_sensor', ids === '["air_t_1","air_t_2","air_t_3","air_p_1","air_h_2","air_h_3"]', ids);
    const names = Array.from(D.querySelectorAll('#legend label')).map(l => l.textContent.trim());
    t('legend_names_sensor_and_model', names.some(x => x.indexOf('T среды · №2 SHT3x') === 0) && names.some(x => x.indexOf('RH среды · №3 AHT2x') === 0) && !names.some(x => x.indexOf('Температура среды') === 0), names.join('|'));
    const c0 = st(W, "cssVar('--s6')"), c1 = st(W, "cssVar('--s6|1')"), c2 = st(W, "cssVar('--s6|2')");
    t('tinted_colors_differ_and_valid', /^#[0-9a-f]{6}$/.test(c1) && c0 !== c1 && c1 !== c2 && c0 !== c2, c0 + ' ' + c1 + ' ' + c2);
    const last = st(W, "JSON.stringify(state.rows[state.rows.length - 1])");
    const lr = JSON.parse(last);
    t('rows_have_per_sensor_fields', lr.air_t_1 > 0 && lr.air_t_2 > 0 && lr.air_h_2 > 0 && lr.air_h_3 > 0 && lr.air_p_1 > 0 && lr.air_t === lr.air_t_1, last.slice(0, 300));
    st(W, "state.vis.air_h_2 = true; state.vis.air_t_2 = true; redraw(); 0");
    const ax = st(W, "JSON.stringify(AFAX.buildAxes(SERIES, state.vis).axes.map(function (a) { return a.series; }))");
    t('humidity_on_own_axis', JSON.parse(ax).some(a => a.indexOf('air_h_2') >= 0 && a.every(x => x.indexOf('air_h') === 0)), ax);
    const dashes = st(W, "JSON.stringify(SERIES.filter(function (d) { return d.envKind === 't'; }).map(function (d) { return d.dash; }))");
    t('per_sensor_dash_differs', new Set(JSON.parse(dashes).map(x => JSON.stringify(x))).size === 3, dashes);
  } catch (e) { t('chart_multi_group', false, e.message); }
  try {
    await scen('one');
    const { W } = await open('/');
    const ids = st(W, "JSON.stringify(SERIES.filter(function (d) { return d.envKind; }).map(function (d) { return d.id; }))");
    t('chart_single_sensor_keeps_old_ids', ids === '["air_t","air_p"]', ids);
    await scen('eight');
    const e8 = await open('/');
    const n = st(e8.W, "SERIES.filter(function (d) { return d.envKind === 't'; }).length");
    const cols = st(e8.W, "new Set(SERIES.filter(function (d) { return d.envKind === 't'; }).map(function (d) { return cssVar(d.color); })).size");
    t('chart_eight_sensors', n === 8 && cols === 8, n + ' ' + cols + ' ' + st(e8.W, "SERIES.length"));
    const sys = await open('/system#sensors');
    t('table_eight_rows_add_disabled', rowsOf(sys.D).length === 8 && sys.D.getElementById('btnAdd').disabled === true, rowsOf(sys.D).length);
  } catch (e) { t('chart_single_eight_group', false, e.message); }
  try {
    await scen('eight');
    const { W, D } = await open('/system#sensors');
    const gsw = m => D.querySelector('#sensRows .sens-grp[data-model="' + m + '"] input[type=checkbox]');
    t('group_switch_checked_when_all_on', gsw('bmp280').checked === true && gsw('bmp280').indeterminate === false, '');
    setSw(swOf(rowOf(D, 1)), false, W);
    await sleep(1800);
    t('group_switch_indeterminate_when_partial', gsw('bmp280').indeterminate === true && gsw('sht3x').indeterminate === false && gsw('sht3x').checked === true, gsw('bmp280').indeterminate + ' ' + gsw('bmp280').checked);
  } catch (e) { t('indeterminate_group', false, e.message); }
  try {
    await scen('three');
    const { W } = await open('/');
    st(W, "state.vis.air_t_2 = true; state.vis.air_h_3 = false; 0");
    const one = "[{ts:1,sensor:1,model:'bmp280',air_t:1,air_p:1,air_h:NaN,flags:0}]";
    st(W, "syncEnvSeries(" + one + "); 0");
    const mid = st(W, "JSON.stringify({ids: SERIES.filter(function (d) { return d.envKind; }).map(function (d) { return d.id; }), t: state.vis.air_t, old: state.vis.air_t_2 === undefined})");
    t('sync_to_single_carries_visibility_and_drops_old', mid === '{"ids":["air_t","air_p"],"t":true,"old":true}', mid);
    st(W, "syncEnvSeries([{ts:1,sensor:1,model:'bmp280',air_t:1,air_p:1,air_h:NaN,flags:0},{ts:1,sensor:2,model:'sht3x',air_t:1,air_p:NaN,air_h:1,flags:0}]); 0");
    const back = st(W, "JSON.stringify({a: state.vis.air_t_1, b: state.vis.air_t_2, c: state.vis.air_h_2, ax: state.axis.air_t_2})");
    t('sync_to_multi_carries_kind_visibility', back === '{"a":true,"b":true,"c":false,"ax":"R"}', back);
    t('sync_same_set_is_noop', st(W, "syncEnvSeries([{ts:2,sensor:1,model:'bmp280',air_t:2,air_p:2,air_h:NaN,flags:0},{ts:2,sensor:2,model:'sht3x',air_t:2,air_p:NaN,air_h:2,flags:0}])") === false, '');
    const lg = Array.from(W.document.querySelectorAll('#legend label')).map(l => l.textContent).join('|');
    t('legend_rebuilt_after_sync', lg.indexOf('RH среды · №2 SHT3x') >= 0 && lg.indexOf('P среды · №1 BMP280') >= 0, lg);
  } catch (e) { t('sync_group', false, e.message); }
  try {
    const { W } = await open('/', 'dark');
    const lum = c => st(W, "AFCL.luminance('" + c + "')");
    const a = st(W, "cssVar('--s6')"), b = st(W, "cssVar('--s6|3')");
    t('dark_theme_tints_toward_white', lum(b) > lum(a), a + ' ' + b);
    const l = await open('/', 'light');
    const a2 = st(l.W, "cssVar('--s6')"), b2 = st(l.W, "cssVar('--s6|3')");
    t('light_theme_tints_toward_black', st(l.W, "AFCL.luminance('" + b2 + "')") < st(l.W, "AFCL.luminance('" + a2 + "')"), a2 + ' ' + b2);
  } catch (e) { t('tint_theme_group', false, e.message); }
//__MORE__
  const failed = [];
  for (const r of res) { if (!r.ok) failed.push(r.n + ': ' + r.d); }
  return JSON.stringify({ total: res.length, failed, res });
})()
