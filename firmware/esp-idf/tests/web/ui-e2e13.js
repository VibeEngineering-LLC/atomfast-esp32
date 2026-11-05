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
  const cardOf = (D, id) => D.querySelector('#bleCards [data-card="' + id + '"]');
  const val = (D, cid, k) => { const r = Array.from(cardOf(D, cid).querySelectorAll('.kv')).find(x => x.querySelector('.k').firstChild.textContent.indexOf(k) === 0); return r ? r.querySelector('.v').textContent : undefined; };
  const doOpen = () => open('/system#storage');
  const btn = (D, id) => D.getElementById('scBtn_' + id);
  const word = (D, W, v) => typeIn(D.getElementById('scWord'), v, W);
  const run = async (D, W, id, w) => { click(btn(D, id), W); word(D, W, w); click(D.getElementById('scDlgOk'), W); await sleep(1500); };
  try {
    await cmode('ok');
    const { W, D } = await doOpen();
    const labels = Array.from(D.querySelectorAll('#storeBtns button')).map(b => b.textContent).join('|');
    t('five_buttons_in_order', labels === 'Очистить сырые|Очистить агрегаты|Очистить датчики среды|Очистить всё|Очистить журнал', labels);
    t('enabled_after_status', Array.from(D.querySelectorAll('#storeBtns button')).every(b => !b.disabled) && !visible(D.getElementById('storeAvail')), '');
    t('csv_links', D.getElementById('storeCsvRaw').getAttribute('href') === '/api/history.csv?kind=raw' && D.getElementById('storeCsvAgg').getAttribute('href') === '/api/history.csv?kind=agg' && D.getElementById('storeCsvEnv').getAttribute('href') === '/api/history.csv?kind=env' && D.getElementById('storeCsvRaw').hasAttribute('download'), '');
    t('dialog_hidden_initially', !visible(D.getElementById('scDlg')), '');
    click(btn(D, 'raw'), W);
    const tx = D.getElementById('scDlg').textContent;
    t('dialog_text', visible(D.getElementById('scDlg')) && tx.indexOf('сырые записи') > 0 && tx.indexOf('seq не сбрасывается') > 0 && tx.indexOf('epoch') > 0 && tx.indexOf('Данные вернуть нельзя') > 0 && tx.indexOf('«ОЧИСТИТЬ»') > 0 && D.getElementById('scDlgOk').disabled === true, tx);
    click(D.getElementById('scDlgOk'), W);
    await sleep(500);
    t('no_post_without_word', (await clog()).length === 0, '');
    for (const wrong of ['очистить', 'ОЧИСТИТ', 'CLEAR', ' ']) {
      word(D, W, wrong);
      await W.eval('scDo()');
      t('no_post_with_wrong_word_' + wrong.trim(), (await clog()).length === 0 && D.getElementById('scDlgOk').disabled === true, wrong);
    }
    word(D, W, 'ОЧИСТИТЬ');
    t('ok_enabled_with_word', D.getElementById('scDlgOk').disabled === false, '');
    click(D.getElementById('scDlgOk'), W);
    await sleep(1500);
    const lg = await clog();
    t('post_raw_body_and_csrf', lg.length === 1 && lg[0].path === '/api/history/clear' && lg[0].raw === '{"ring":"raw","confirm":"CLEAR"}' && lg[0].csrf === 'mock', JSON.stringify(lg));
    t('result_shown_dialog_closed', txt(D, 'storeResult') === 'Очищено: сырые 1200' && D.getElementById('storeResult').className.indexOf('ok') >= 0 && !visible(D.getElementById('scDlg')), txt(D, 'storeResult'));
  } catch (e) { t('flow_group', false, e.message); }
  try {
    await cmode('ok');
    const { W, D } = await doOpen();
    const want = { agg: ['agg', 'Очищено: агрегаты 80'], env: ['env', 'Очищено: датчики среды 400'], all: ['all', 'Очищено: сырые 1200, агрегаты 80, датчики среды 400'] };
    for (const id of ['agg', 'env', 'all']) {
      await run(D, W, id, 'ОЧИСТИТЬ');
      const l = (await clog()).pop();
      t('post_' + id, l && l.path === '/api/history/clear' && l.raw === '{"ring":"' + want[id][0] + '","confirm":"CLEAR"}' && txt(D, 'storeResult') === want[id][1], JSON.stringify(l) + txt(D, 'storeResult'));
    }
    await run(D, W, 'log', 'ОЧИСТИТЬ');
    const l = (await clog()).pop();
    t('post_log', l && l.path === '/api/log/clear' && l.raw === '{"confirm":"CLEAR"}' && txt(D, 'storeResult') === 'Журнал очищен', JSON.stringify(l));
    click(btn(D, 'log'), W);
    t('log_dialog_has_no_seq_text', D.getElementById('scDlgText').textContent.indexOf('seq') < 0 && D.getElementById('scDlgText').textContent.indexOf('журнал событий') > 0, D.getElementById('scDlgText').textContent);
  } catch (e) { t('rings_group', false, e.message); }
  try {
    await cmode('ok');
    const { W, D } = await doOpen();
    click(btn(D, 'all'), W);
    click(D.getElementById('scDlgCancel'), W);
    t('cancel_closes_without_post', !visible(D.getElementById('scDlg')) && (await clog()).length === 0, '');
    click(btn(D, 'agg'), W);
    D.dispatchEvent(new W.KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    t('escape_closes', !visible(D.getElementById('scDlg')), '');
    click(btn(D, 'agg'), W);
    word(D, W, 'ОЧИСТИТЬ');
    click(btn(D, 'env'), W);
    t('reopen_resets_word', D.getElementById('scWord').value === '' && D.getElementById('scDlgOk').disabled === true && D.getElementById('scDlgTitle').textContent.indexOf('датчики среды') > 0, D.getElementById('scDlgTitle').textContent);
  } catch (e) { t('cancel_group', false, e.message); }
  try {
    await cmode('busy');
    const { W, D } = await doOpen();
    await run(D, W, 'raw', 'ОЧИСТИТЬ');
    t('error_503_text', txt(D, 'storeResult').indexOf('503') > 0 && D.getElementById('storeResult').className.indexOf('bad') >= 0 && visible(D.getElementById('scDlg')) && btn(D, 'raw').disabled === false, txt(D, 'storeResult'));
    await cmode('nopersist');
    await run(D, W, 'raw', 'ОЧИСТИТЬ');
    t('not_persisted_note', txt(D, 'storeResult').indexOf('во флеш не записано') > 0, txt(D, 'storeResult'));
    await cmode('ok');
  } catch (e) { t('error_group', false, e.message); }
  try {
    const { W, D } = await doOpen();
    W.eval('sysData.st = null; scAvail();');
    t('disabled_without_status', Array.from(D.querySelectorAll('#storeBtns button')).every(b => b.disabled) && visible(D.getElementById('storeAvail')), '');
    click(btn(D, 'all'), W);
    t('click_without_status_opens_nothing', !visible(D.getElementById('scDlg')), '');
  } catch (e) { t('avail_group', false, e.message); }
  try {
    await cmode('ok');
    const { W, D } = await doOpen();
    t('danger_style_on_all_and_log_only', ['all', 'log'].every(i => btn(D, i).classList.contains('danger')) && ['raw', 'agg', 'env'].every(i => !btn(D, i).classList.contains('danger')), '');
    W.eval('window.__rf = 0; refresh = function () { window.__rf++; };');
    click(btn(D, 'raw'), W);
    word(D, W, 'ОЧИСТИТЬ');
    click(D.getElementById('scDlgOk'), W);
    click(D.getElementById('scDlgOk'), W);
    await W.eval('scDo()');
    await sleep(1800);
    t('double_click_sends_one_post', (await clog()).length === 1, String((await clog()).length));
    t('refresh_after_success', W.eval('window.__rf') === 1, String(W.eval('window.__rf')));
    W.eval('postJSON = function () { return Promise.reject(new Error("x")); };');
    click(btn(D, 'agg'), W);
    word(D, W, 'ОЧИСТИТЬ');
    click(D.getElementById('scDlgOk'), W);
    await sleep(600);
    t('network_failure_text', txt(D, 'storeResult') === 'нет связи с платой' && D.getElementById('storeResult').className.indexOf('bad') >= 0 && btn(D, 'raw').disabled === false, txt(D, 'storeResult'));
  } catch (e) { t('busy_group', false, e.message); }
  try {
    await cmode('ok');
    const { W, D } = await doOpen();
    W.eval('postJSON = function () { return new Promise(function (res) { window.__res = res; }); };');
    click(btn(D, 'env'), W);
    word(D, W, 'ОЧИСТИТЬ');
    click(D.getElementById('scDlgOk'), W);
    await sleep(300);
    t('buttons_disabled_while_request_runs', Array.from(D.querySelectorAll('#storeBtns button')).every(b => b.disabled) && D.getElementById('scDlgOk').disabled === true && txt(D, 'storeResult') === 'очищаю…', txt(D, 'storeResult'));
    W.eval('window.__res({ ok: true, status: 200, data: { removed: { env: 1 } } })');
    await sleep(500);
    t('buttons_enabled_after_request', Array.from(D.querySelectorAll('#storeBtns button')).every(b => !b.disabled) && txt(D, 'storeResult') === 'Очищено: датчики среды 1', txt(D, 'storeResult'));
  } catch (e) { t('slow_group', false, e.message); }
  const failed = [];
  for (const r of res) { if (!r.ok) failed.push(r.n + ': ' + r.d); }
  return JSON.stringify({ total: res.length, failed, res });
})()
