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
  const RAWT = 'Сырые — каждое измерение (шаг ≈2 с)';
  const aggT = n => 'Агрегаты — усреднённые за ' + n + ' точки, удобны для длинных окон; хранятся дольше';
  const seg = (D, k) => D.querySelector('#segKind button[data-kind="' + k + '"]');
  const wrapStatus = (W, expr) => W.eval('(function () { var o = fetchJSON; fetchJSON = async function (u, a) { var r = await o(u, a); if (String(u).indexOf("/api/history/status") === 0) { ' + expr + ' } return r; }; })()');
  try {
    const { W, D } = await open('/');
    t('titles_on_both_buttons', seg(D, 'raw').title === RAWT && seg(D, 'agg').title === aggT('60 с'), seg(D, 'raw').title + '|' + seg(D, 'agg').title);
    t('buttons_described_by_note', seg(D, 'raw').getAttribute('aria-describedby') === 'kindNote' && seg(D, 'agg').getAttribute('aria-describedby') === 'kindNote' && !!D.getElementById('kindNote'), '');
    click(seg(D, 'raw'), W);
    await sleep(1200);
    t('note_raw', txt(D, 'kindNote') === RAWT + '.', txt(D, 'kindNote'));
    click(seg(D, 'agg'), W);
    await sleep(1200);
    t('note_agg_fixed_no_link', txt(D, 'kindNote') === aggT('60 с') + '. Интервал фиксирован в прошивке (60 с), настроить его нельзя.' && D.querySelectorAll('#kindNote a').length === 0, txt(D, 'kindNote'));
    t('note_is_live_region', D.getElementById('kindNote').getAttribute('aria-live') === 'polite', '');
  } catch (e) { t('titles_group', false, e.message); }
  try {
    const { W, D } = await open('/');
    wrapStatus(W, 'r.window_s = 30;');
    click(seg(D, 'agg'), W);
    await sleep(1200);
    t('interval_comes_from_status', txt(D, 'kindNote').indexOf('за 30 с точки') > 0 && txt(D, 'kindNote').indexOf('(30 с)') > 0 && seg(D, 'agg').title === aggT('30 с'), txt(D, 'kindNote'));
    wrapStatus(W, 'delete r.window_s;');
    click(D.getElementById('btnRefresh'), W);
    await sleep(1200);
    t('no_interval_nd', txt(D, 'kindNote').indexOf('за н/д точки') > 0 && txt(D, 'kindNote').indexOf('(н/д)') > 0 && seg(D, 'agg').title === aggT('н/д'), txt(D, 'kindNote'));
  } catch (e) { t('data_group', false, e.message); }
  try {
    const { W, D } = await open('/system#storage');
    const hint = () => { const r = Array.from(D.querySelectorAll('#store .kv')).find(x => x.querySelector('.k').firstChild.textContent === 'Агрегаты'); return r ? r.querySelector('.khint') : null; };
    t('storage_agg_caption', !!hint() && hint().textContent === 'усреднённые точки за 60 с', hint() && hint().textContent);
    const raw = Array.from(D.querySelectorAll('#store .kv')).find(x => x.querySelector('.k').textContent === 'Сырые');
    t('storage_raw_has_no_caption', !!raw && !raw.querySelector('.khint'), '');
    t('storage_reserve_still_shown', D.getElementById('store').textContent.indexOf('запас по времени') > 0, '');
    wrapStatus(W, 'r.window_s = 15;');
    W.eval('refresh()');
    await sleep(1500);
    t('storage_caption_from_data', hint() && hint().textContent === 'усреднённые точки за 15 с', hint() && hint().textContent);
    wrapStatus(W, 'delete r.window_s;');
    W.eval('refresh()');
    await sleep(1500);
    t('storage_caption_nd', hint() && hint().textContent === 'усреднённые точки за н/д', hint() && hint().textContent);
  } catch (e) { t('storage_group', false, e.message); }
  const failed = [];
  for (const r of res) { if (!r.ok) failed.push(r.n + ': ' + r.d); }
  return JSON.stringify({ total: res.length, failed, res });
})()
