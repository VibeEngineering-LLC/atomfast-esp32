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
  const bscen = async s => { await fetch('/__mock/ble?scenario=' + s, { method: 'POST' }); };
  const cardOf = (D, id) => D.querySelector('#bleCards [data-card="' + id + '"]');
  const val = (D, cid, k) => { const r = Array.from(cardOf(D, cid).querySelectorAll('.kv')).find(x => x.querySelector('.k').firstChild.textContent.indexOf(k) === 0); return r ? r.querySelector('.v').textContent : undefined; };
  try {
    await bscen('normal');
    const { W, D } = await open('/system#ble');
    t('full_view_replaces_legacy', visible(D.getElementById('bleFull')) && !visible(D.getElementById('bleLegacy')), '');
    const ids = Array.from(D.querySelectorAll('#bleCards > .card')).map(c => c.dataset.card).join();
    t('five_cards_in_spec_order', ids === 'link,rate,params,sessions,scan', ids);
    const titles = Array.from(D.querySelectorAll('#bleCards > .card > h2')).map(h => h.textContent).join('|');
    t('card_titles', titles === 'Связь|Скорость|Параметры соединения|Сессии|Сканирование', titles);
    const counts = Array.from(D.querySelectorAll('#bleCards > .card')).map(c => c.querySelectorAll('.kv').length).join();
    t('row_counts', counts === '17,25,13,5,9', counts);
    t('link_values', val(D, 'link', 'Подключён') === 'да' && val(D, 'link', 'Длительность текущей сессии') === '1 ч 30 мин' && val(D, 'link', 'Доля времени на связи за 24 ч') === '99.2' && val(D, 'link', 'Причина последнего разрыва') === 'supervision timeout (0x08)', val(D, 'link', 'Подключён') + val(D, 'link', 'Причина последнего'));
    t('rate_values_units_in_labels', val(D, 'rate', 'Пакетов в секунду, мгновенно') === '0.500' && val(D, 'rate', 'Байт в секунду, за 1 мин') === '6.5' && val(D, 'rate', 'Джиттер') === '35.7', val(D, 'rate', 'Джиттер'));
    t('params_values_and_phy_nd', val(D, 'params', 'Интервал соединения, мс') === '30.00' && val(D, 'params', 'Уровень мощности') === '15 (P20)' && val(D, 'params', 'PHY передачи') === 'н/д' && cardOf(D, 'params').textContent.indexOf('API BLE 5.0 выключен в этой сборке') > 0, val(D, 'params', 'PHY передачи'));
    t('nd_values_muted_class', cardOf(D, 'params').querySelectorAll('.v.nd').length === 2, cardOf(D, 'params').querySelectorAll('.v.nd').length);
    t('scan_and_device', val(D, 'scan', 'Адрес прибора (маска)') === 'AA:BB:**:**:CC:DD' && val(D, 'scan', 'Окно сканирования, мс') === '99.0', '');
  } catch (e) { t('normal_group', false, e.message); }
  try {
    await bscen('normal');
    const { W, D } = await open('/system#ble');
    const hist = Array.from(D.querySelectorAll('#bleHist .histrow'));
    t('histogram_eight_bins_with_shares', hist.length === 8 && hist[4].querySelector('.histfill').style.width === '59%' && hist[4].querySelector('.histcnt').textContent === '800' && hist[0].querySelector('.histlab').textContent === 'выше -40', hist.map(r => r.querySelector('.histfill').style.width).join());
    const rows = D.querySelectorAll('#bleSessions tbody tr');
    t('sessions_table', rows.length === 3 && D.querySelectorAll('#bleSessions thead th').length === 8 && rows[0].cells[2].textContent === '1 ч 30 мин' && rows[1].cells[1].textContent === 'н/д', rows.length + ' ' + (rows[0] && rows[0].textContent));
    const a = D.getElementById('bleCsv');
    const r = await fetch('/api/ble/sessions.csv');
    const csv = await r.text();
    t('csv_link_and_content', a && a.getAttribute('href') === '/api/ble/sessions.csv' && a.hasAttribute('download') && (r.headers.get('content-type') || '').indexOf('text/csv') === 0 && csv.split('\n')[0] === 'idx,start_utc,end_utc,dur_s,pkts,bad,reason_code,reason,rssi_min_dbm,rssi_avg_dbm,rssi_max_dbm,setup_ms' && csv.trim().split('\n').length === 4, (a && a.outerHTML) + r.headers.get('content-type'));
    await sleep(11000);
    const sp = D.getElementById('bleSparkPps');
    t('sparkline_after_polls', !!sp && !!sp.querySelector('svg path') && /отсчётов [3-9] из 120/.test(sp.textContent) && !!D.querySelector('#bleSparkRssi svg'), sp && sp.textContent);
  } catch (e) { t('detail_group', false, e.message); }
  try {
    await bscen('nodata');
    const { W, D } = await open('/system#ble');
    t('not_ready_message', visible(D.getElementById('bleFull')) && !visible(D.getElementById('bleLegacy')) && D.getElementById('bleCards').textContent.indexOf('Стек BLE ещё не готов') >= 0 && D.querySelectorAll('#bleCards > .card').length === 1, D.getElementById('bleCards').textContent);
    await bscen('old');
    const o = await open('/system#ble');
    t('old_firmware_falls_back_to_legacy', visible(o.D.getElementById('bleLegacy')) && !visible(o.D.getElementById('bleFull')) && o.D.getElementById('b_conn').textContent !== '' && o.D.getElementById('bleLegacy').textContent.indexOf('/api/ble') > 0, '');
  } catch (e) { t('states_group', false, e.message); }
  try {
    await bscen('errors');
    const { W, D } = await open('/system#ble');
    t('errors_scenario_nd_not_zero', val(D, 'link', 'Подключён') === 'нет' && val(D, 'link', 'Неудачных попыток подряд сейчас') === '5' && val(D, 'rate', 'Пакетов в секунду, мгновенно') === 'н/д' && val(D, 'params', 'RSSI сейчас') === 'н/д', val(D, 'rate', 'Пакетов в секунду, мгновенно'));
    const nb = D.getElementById('nb_ble');
    t('badge_bad_when_uptime_low', nb && nb.classList.contains('bad') && D.querySelector('#sidelist a[data-sec="ble"]').getAttribute('data-level') === 'bad', nb && nb.className);
    await bscen('many');
    const m = await open('/system#ble');
    t('many_sessions_32_rows', m.D.querySelectorAll('#bleSessions tbody tr').length === 32 && val(m.D, 'link', 'Подключений с загрузки') === '41', m.D.querySelectorAll('#bleSessions tbody tr').length);
    const nbm = m.D.getElementById('nb_ble');
    const sl = await (await fetch('/api/ble/sessions')).json();
    const want = sl.filter(x => x.end_utc !== null && Date.now() / 1000 - x.end_utc <= 86400).length;
    t('badge_warn_with_count', nbm && nbm.classList.contains('warn') && nbm.textContent === String(want) && want > 0 && want < 32, nbm && nbm.className + '|' + nbm.textContent + '|' + want);
  } catch (e) { t('errors_group', false, e.message); }
  try {
    await bscen('normal');
    const { W, D } = await open('/system#ble');
    const n0 = (await (await fetch('/__mock/ble-n')).json()).n;
    await sleep(11500);
    const n1 = (await (await fetch('/__mock/ble-n')).json()).n;
    t('polls_every_five_seconds_while_open', n1 - n0 >= 2 && n1 - n0 <= 3, String(n1 - n0));
    const sp = D.getElementById('bleSparkPps');
    t('sparkline_path_and_values', !!sp && sp.querySelector('path').getAttribute('d').length > 10 && sp.textContent.indexOf('мин 0.500 · макс 0.500 пакетов/с') > 0 && D.getElementById('bleSparkRssi').textContent.indexOf('мин -71 · макс -71 дБм') > 0, sp && sp.textContent);
    t('cards_not_duplicated_after_polls', D.querySelectorAll('#bleCards > .card').length === 5 && D.querySelectorAll('#bleSessions').length === 1, D.querySelectorAll('#bleCards > .card').length);
    await bscen('old');
    await sleep(6500);
    t('switch_to_legacy_when_api_disappears', visible(D.getElementById('bleLegacy')) && !visible(D.getElementById('bleFull')), '');
    await bscen('normal');
    await sleep(6500);
    t('switch_back_to_full_when_api_returns', visible(D.getElementById('bleFull')) && !visible(D.getElementById('bleLegacy')), '');
  } catch (e) { t('polling_group', false, e.message); }
  try {
    await bscen('old');
    const { W, D } = await open('/system#ble');
    t('legacy_filled_and_no_error_pill', D.getElementById('b_conn').textContent !== '—' && D.getElementById('b_ready').textContent !== '—' && txt(D, 'sysNote') === '', D.getElementById('b_conn').textContent + '|' + txt(D, 'sysNote'));
    await bscen('normal');
  } catch (e) { t('legacy_group', false, e.message); }
  try {
    await bscen('noscan');
    const { W, D } = await open('/system#ble');
    const rows = Array.from(cardOf(D, 'scan').querySelectorAll('.kv'));
    const keys = rows.map(r => r.querySelector('.k').firstChild.textContent);
    t('noscan_single_message_row', keys[0] === 'Сканирование' && keys.indexOf('Сканирований с загрузки') < 0 && val(D, 'scan', 'Сканирование') === 'не выполнялось: плата подключена к сохранённому адресу', keys.join('|'));
    t('noscan_marks_skipped_rows', ['Реклам в последнем скане', 'Найдено приборов в последнем скане', 'От начала скана до обнаружения прибора, мс'].every(k => val(D, 'scan', k) === 'н/д (скан не шёл)') && !rows.some(r => r.querySelector('.v').textContent === '0'), '');
    const nm = rows.find(r => r.querySelector('.k').firstChild.textContent === 'Имя прибора');
    t('noscan_name_nd_with_reason', nm.querySelector('.v').textContent === 'н/д' && nm.querySelector('.khint').textContent === 'имя не принимается в рекламе' && val(D, 'scan', 'Адрес прибора (маска)') === 'AA:BB:**:**:CC:DD', '');
    t('every_scan_row_has_hint', rows.length === 9 && rows.every(r => r.querySelector('.khint') && r.querySelector('.khint').textContent.length > 5), rows.length);
    await bscen('normal');
    const n = await open('/system#ble');
    t('scans_made_old_view', val(n.D, 'scan', 'Сканирований с загрузки') === '120' && val(n.D, 'scan', 'Реклам в последнем скане') === '14' && val(n.D, 'scan', 'Сканирование') === undefined, '');
  } catch (e) { t('noscan_group', false, e.message); }
  try {
    await bscen('normal');
    const { W, D } = await open('/system#ble');
    const w0 = st(W, "document.documentElement.scrollWidth <= document.documentElement.clientWidth + 1");
    t('no_horizontal_scroll', w0 === true, '');
    await bscen('normal');
  } catch (e) { t('layout_group', false, e.message); }
  const failed = [];
  for (const r of res) { if (!r.ok) failed.push(r.n + ': ' + r.d); }
  return JSON.stringify({ total: res.length, failed, res });
})()
