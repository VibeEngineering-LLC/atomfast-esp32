(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const wait = P.wait || 2500;
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const res = [];
  const t = (n, cond, d) => { res.push({ n, ok: !!cond, d: d || '' }); };
  const GH = location.origin + '/__gh';
  const mockPost = p => fetch(p, { method: 'POST' });
  const mockLog = async p => (await fetch(p)).json();
  let cur = null;
  const open = async (extra) => {
    if (cur) { try { cur.f.remove(); } catch (e) {} cur = null; }
    if (!extra || !extra.keep) { try { localStorage.clear(); } catch (e) {} }
    const f = document.createElement('iframe');
    f.style.cssText = 'position:fixed;left:0;top:0;border:0;opacity:0;pointer-events:none;width:' + width + 'px;height:900px;';
    f.src = location.origin + '/system?gh=' + encodeURIComponent((extra && extra.gh) || GH) + '#firmware';
    document.body.appendChild(f);
    await new Promise(r => { f.onload = r; setTimeout(r, 15000); });
    await sleep(wait);
    const W = f.contentWindow;
    const D = f.contentDocument;
    if (!(extra && extra.noSwitch)) { const sw = D.getElementById('fbBrowserOn'); sw.checked = true; sw.dispatchEvent(new W.Event('change', { bubbles: true })); }
    cur = { f, W, D };
    return { f, W, D };
  };
  const waitFor = async (fn, ms) => { const t0 = Date.now(); while (Date.now() - t0 < ms) { if (fn()) return true; await sleep(300); } return false; };
  const click = (el, W) => el.dispatchEvent(new W.MouseEvent('click', { bubbles: true, cancelable: true }));
  const setChecked = (el, v, W) => { el.checked = v; el.dispatchEvent(new W.Event('change', { bubbles: true })); };
  const rowOf = (D, tag) => Array.from(D.querySelectorAll('#fwRows tr')).find(r => r.children[0].textContent.indexOf(tag) === 0);
  const btnIn = (row, W, txt) => Array.from(row.querySelectorAll('button')).find(b => b.textContent.indexOf(txt) !== -1);
  const pickFile = (D, W, bytes, name) => { const dt = new W.DataTransfer(); dt.items.add(new W.File([bytes], name)); const inp = D.getElementById('fwFile'); inp.files = dt.files; inp.dispatchEvent(new W.Event('change', { bubbles: true })); };
  const failed = [];
  try { await mockPost('/__mock/reset'); } catch (e) {}
  try {
    const { W, D } = await open();
    const sec = D.getElementById('sec-firmware');
    const links = Array.from(D.querySelectorAll('#sidelist a'));
    const iFw = links.findIndex(a => a.getAttribute('data-sec') === 'firmware');
    const iMt = links.findIndex(a => a.getAttribute('data-sec') === 'maint');
    const fwLink = links[iFw];
    t('section_in_nav', !!sec && !sec.hidden && iFw !== -1 && iMt !== -1 && iFw === iMt - 1 && fwLink && fwLink.textContent.trim() === 'Прошивки', 'sec=' + (sec && sec.hidden) + ' iFw=' + iFw + ' iMt=' + iMt + ' txt=' + (fwLink && fwLink.textContent.trim()));
  } catch (e) { t('section_in_nav', false, e.message); }
  try {
    const { W, D } = await open();
    const log = await mockLog('/__mock/gh-log');
    const tbl = D.getElementById('fwTable');
    t('no_github_before_check', Array.isArray(log) && log.length === 0 && !!tbl && tbl.hidden, 'log=' + (log && log.length) + ' hidden=' + (tbl && tbl.hidden));
  } catch (e) { t('no_github_before_check', false, e.message); }
  try {
    const { W, D } = await open();
    click(D.getElementById('fwCheck'), W);
    const ok = await waitFor(() => { const el = D.getElementById('fwTable'); return el && !el.hidden; }, 8000);
    const rows = Array.from(D.querySelectorAll('#fwRows tr'));
    const cur = D.getElementById('fwCurrent');
    const rc1 = rowOf(D, 'firmware-v1.2.40-rc1');
    const curRow = rowOf(D, 'firmware-v1.2.39');
    const r30 = rowOf(D, 'firmware-v1.2.30');
    const badge = (row, txt) => row && Array.from(row.querySelectorAll('.fw-badge')).some(b => b.textContent.indexOf(txt) !== -1);
    const sel = r30 && r30.querySelector('select');
    const opts = sel ? sel.querySelectorAll('option').length : 0;
    const allLinks = rows.every(r => { const a = r.querySelector('a'); return a && a.target === '_blank' && (a.rel || '').indexOf('noopener') !== -1 && a.textContent.indexOf('Заметки') !== -1; });
    t('check_lists_releases', ok && rows.length === 4 && cur && cur.textContent.trim() === '1.2.39' && badge(rc1, 'пред-релиз') && badge(curRow, 'установлена') && sel && opts === 2 && allLinks, 'ok=' + ok + ' rows=' + rows.length + ' cur=' + (cur && cur.textContent.trim()) + ' rc1=' + badge(rc1, 'пред-релиз') + ' curBadge=' + badge(curRow, 'установлена') + ' opts=' + opts + ' links=' + allLinks);
  } catch (e) { t('check_lists_releases', false, e.message); }
  try {
    const log = await mockLog('/__mock/gh-log');
    const e = log && log[0];
    const h = e && e.headers || {};
    const clean = !h.cookie && !h.authorization && !h['x-csrf-token'] && !h.referer;
    t('github_request_is_clean', log.length === 1 && e && e.path === '/__gh/repos/VibeEngineering-LLC/atomfast-esp32/releases?per_page=20' && h.accept === 'application/vnd.github+json' && clean, 'len=' + log.length + ' path=' + (e && e.path) + ' accept=' + h.accept + ' clean=' + clean);
  } catch (e) { t('github_request_is_clean', false, e.message); }
  try {
    const { W, D } = await open();
    click(D.getElementById('fwCheck'), W);
    await waitFor(() => { const el = D.getElementById('fwTable'); return el && !el.hidden; }, 8000);
    const row = rowOf(D, 'firmware-v1.2.39');
    const btn = btnIn(row, W, 'Переустановить');
    click(btn, W);
    const conf = D.getElementById('fwConfirm');
    const txt = D.getElementById('fwConfirmText');
    const go = D.getElementById('fwGo');
    const und = D.getElementById('fwUnderstand');
    const before = go.disabled;
    setChecked(und, true, W);
    const after = go.disabled;
    click(go, W);
    const vOk = await waitFor(() => { const el = D.getElementById('fwVerify'); return el && !el.hidden; }, 10000);
    const vtxt = D.getElementById('fwVerify').textContent;
    const up = D.getElementById('fwUpload');
    const ota = await mockLog('/__mock/ota-log');
    t('mismatch_blocks_upload', !!conf && !conf.hidden && txt.textContent.indexOf('Обновить прошивку с 1.2.39 на firmware-v1.2.39') !== -1 && before === true && after === false && vOk && vtxt.indexOf('SHA-256 НЕ совпала') !== -1 && up.hidden && ota.length === 0, 'conf=' + (!conf.hidden) + ' txt=' + (txt.textContent.indexOf('Обновить прошивку с 1.2.39 на firmware-v1.2.39') !== -1) + ' before=' + before + ' after=' + after + ' vOk=' + vOk + ' vtxt=' + (vtxt.indexOf('SHA-256 НЕ совпала') !== -1) + ' upHidden=' + up.hidden + ' ota=' + ota.length);
  } catch (e) { t('mismatch_blocks_upload', false, e.message); }
  try {
    const { W, D } = await open();
    click(D.getElementById('fwCheck'), W);
    await waitFor(() => { const el = D.getElementById('fwTable'); return el && !el.hidden; }, 8000);
    const row = rowOf(D, 'firmware-v1.2.30');
    const btn = btnIn(row, W, 'Установить');
    click(btn, W);
    const txt = D.getElementById('fwConfirmText');
    const dg = D.getElementById('fwDowngradeRow');
    const go = D.getElementById('fwGo');
    const und = D.getElementById('fwUnderstand');
    const dgo = D.getElementById('fwDowngradeOk');
    setChecked(und, true, W);
    const still = go.disabled;
    setChecked(dgo, true, W);
    const now = go.disabled;
    click(go, W);
    const vOk = await waitFor(() => { const el = D.getElementById('fwVerify'); return el && !el.hidden; }, 10000);
    const vtxt = D.getElementById('fwVerify').textContent;
    const up = D.getElementById('fwUpload');
    const ota = await mockLog('/__mock/ota-log');
    t('bad_magic_blocks_and_downgrade_gate', txt.textContent.indexOf('понижение версии') !== -1 && !dg.hidden && still === true && now === false && vOk && vtxt.indexOf('НЕ образ ESP') !== -1 && up.hidden && ota.length === 0, 'dg=' + (txt.textContent.indexOf('понижение версии') !== -1) + ' row=' + (!dg.hidden) + ' still=' + still + ' now=' + now + ' vOk=' + vOk + ' vtxt=' + (vtxt.indexOf('НЕ образ ESP') !== -1) + ' upHidden=' + up.hidden + ' ota=' + ota.length);
  } catch (e) { t('bad_magic_blocks_and_downgrade_gate', false, e.message); }
  try {
    const { W, D } = await open();
    click(D.getElementById('fwCheck'), W);
    await waitFor(() => { const el = D.getElementById('fwTable'); return el && !el.hidden; }, 8000);
    const row = rowOf(D, 'firmware-v1.2.30');
    const sel = row.querySelector('select');
    sel.value = '1';
    sel.dispatchEvent(new W.Event('change', { bubbles: true }));
    const btn = btnIn(row, W, 'Установить');
    click(btn, W);
    const txt = D.getElementById('fwConfirmText');
    t('second_asset_choice', txt.textContent.indexOf('atomfast-gw-c3.bin') !== -1, 'txt=' + txt.textContent);
  } catch (e) { t('second_asset_choice', false, e.message); }
  try {
    const { W, D } = await open();
    click(D.getElementById('fwCheck'), W);
    await waitFor(() => { const el = D.getElementById('fwTable'); return el && !el.hidden; }, 8000);
    const row = rowOf(D, 'firmware-v1.2.26-rc1');
    const btn = btnIn(row, W, 'Установить');
    click(btn, W);
    setChecked(D.getElementById('fwUnderstand'), true, W);
    setChecked(D.getElementById('fwDowngradeOk'), true, W);
    click(D.getElementById('fwGo'), W);
    const vOk = await waitFor(() => { const el = D.getElementById('fwVerify'); return el && !el.hidden; }, 10000);
    const vtxt = D.getElementById('fwVerify').textContent;
    const sum = D.getElementById('fwSumRow');
    const up = D.getElementById('fwUpload');
    const before = up.disabled;
    setChecked(D.getElementById('fwSumOk'), true, W);
    const after = up.disabled;
    click(D.getElementById('fwCancel'), W);
    const conf = D.getElementById('fwConfirm');
    t('unconfirmed_needs_extra_confirm', vOk && vtxt.indexOf('не подтверждена') !== -1 && !sum.hidden && !up.hidden && before === true && after === false && conf.hidden, 'vOk=' + vOk + ' vtxt=' + (vtxt.indexOf('не подтверждена') !== -1) + ' sum=' + (!sum.hidden) + ' upHidden=' + up.hidden + ' before=' + before + ' after=' + after + ' closed=' + conf.hidden);
  } catch (e) { t('unconfirmed_needs_extra_confirm', false, e.message); }
  try {
    const { W, D } = await open();
    click(D.getElementById('fwCheck'), W);
    await waitFor(() => { const el = D.getElementById('fwTable'); return el && !el.hidden; }, 8000);
    const row = rowOf(D, 'firmware-v1.2.40-rc1');
    const btn = btnIn(row, W, 'Установить');
    click(btn, W);
    setChecked(D.getElementById('fwUnderstand'), true, W);
    click(D.getElementById('fwGo'), W);
    const vOk = await waitFor(() => { const el = D.getElementById('fwVerify'); return el && !el.hidden; }, 10000);
    const vtxt = D.getElementById('fwVerify').textContent;
    const sum = D.getElementById('fwSumRow');
    const up = D.getElementById('fwUpload');
    click(up, W);
    const pOk = await waitFor(() => { const el = D.getElementById('fwProgText'); return el && el.textContent.indexOf('Готово') !== -1; }, 40000);
    const ptxt = D.getElementById('fwProgText').textContent;
    const ota = await mockLog('/__mock/ota-log');
    const e = ota && ota[0];
    const cur = D.getElementById('fwCurrent');
    t('confirmed_upload_flow', vOk && vtxt.indexOf('SHA-256 подтверждена') !== -1 && sum.hidden && !up.hidden && up.disabled === false && pOk && ptxt.indexOf('на плате версия 1.2.40-rc1') !== -1 && ota.length === 1 && e && e.len === 65536 && e.first === 233 && e.csrf === 'mock' && e.ctype === 'application/octet-stream' && cur && cur.textContent.trim() === '1.2.40-rc1', 'vOk=' + vOk + ' vtxt=' + (vtxt.indexOf('SHA-256 подтверждена') !== -1) + ' sumHidden=' + sum.hidden + ' up=' + (!up.hidden) + ' pOk=' + pOk + ' ptxt=' + (ptxt.indexOf('на плате версия 1.2.40-rc1') !== -1) + ' ota=' + ota.length + ' len=' + (e && e.len) + ' first=' + (e && e.first) + ' csrf=' + (e && e.csrf) + ' ctype=' + (e && e.ctype) + ' cur=' + (cur && cur.textContent.trim()));
  } catch (e) { t('confirmed_upload_flow', false, e.message); }
  try {
    await mockPost('/__mock/reset');
    const { W, D } = await open();
    const small = new Uint8Array(10);
    pickFile(D, W, small, 'small.bin');
    await sleep(500);
    const fi1 = D.getElementById('fwFileInfo').textContent;
    const go1 = D.getElementById('fwFileGo').disabled;
    const bad = new Uint8Array(2048);
    bad[0] = 0;
    pickFile(D, W, bad, 'bad.bin');
    await sleep(500);
    const fi2 = D.getElementById('fwFileInfo').textContent;
    const good = new Uint8Array(2048);
    good[0] = 0xE9;
    pickFile(D, W, good, 'good.bin');
    await sleep(500);
    const fi3 = D.getElementById('fwFileInfo').textContent;
    const go2 = D.getElementById('fwFileGo').disabled;
    setChecked(D.getElementById('fwFileOk'), true, W);
    const go3 = D.getElementById('fwFileGo').disabled;
    t('manual_upload_checks', fi1.indexOf('слишком маленький') !== -1 && go1 === true && fi2.indexOf('не образ прошивки') !== -1 && fi3.indexOf('проверка пройдена') !== -1 && go2 === true && go3 === false, 'fi1=' + (fi1.indexOf('слишком маленький') !== -1) + ' go1=' + go1 + ' fi2=' + (fi2.indexOf('не образ прошивки') !== -1) + ' fi3=' + (fi3.indexOf('проверка пройдена') !== -1) + ' go2=' + go2 + ' go3=' + go3);
  } catch (e) { t('manual_upload_checks', false, e.message); }
  try {
    const { W, D } = await open();
    const good = new Uint8Array(2048);
    good[0] = 0xE9;
    pickFile(D, W, good, 'good.bin');
    await sleep(500);
    setChecked(D.getElementById('fwFileOk'), true, W);
    click(D.getElementById('fwFileGo'), W);
    const pOk = await waitFor(() => { const el = D.getElementById('fwFileText'); return el && (el.textContent.indexOf('Готово') !== -1 || el.textContent.indexOf('вернулась') !== -1); }, 40000);
    const ota = await mockLog('/__mock/ota-log');
    const e = ota && ota[0];
    t('manual_upload_sends_to_ota', pOk && ota.length === 1 && e && e.len === 2048 && e.first === 233, 'pOk=' + pOk + ' ota=' + ota.length + ' len=' + (e && e.len) + ' first=' + (e && e.first));
  } catch (e) { t('manual_upload_sends_to_ota', false, e.message); }
  try {
    const { W, D } = await open();
    const repo = D.getElementById('fwRepo');
    const check = D.getElementById('fwCheck');
    const status = D.getElementById('fwStatus');
    const cases = [
      ['missing/none', '404'],
      ['limited/repo', 'Лимит запросов GitHub исчерпан'],
      ['broken/json', 'неожиданный ответ'],
      ['abc', 'формат: владелец/репозиторий'],
      ['empty/repo', 'нет опубликованных релизов']
    ];
    let all = true;
    let detail = '';
    for (const c of cases) {
      repo.value = c[0];
      repo.dispatchEvent(new W.Event('change', { bubbles: true }));
      click(check, W);
      await sleep(1500);
      const txt = status.textContent;
      const ok = txt.indexOf(c[1]) !== -1;
      if (!ok) { all = false; detail += c[0] + ':' + txt + ' '; }
    }
    t('error_messages', all, detail);
  } catch (e) { t('error_messages', false, e.message); }
  try {
    const { W, D } = await open({ gh: 'http://localhost:1/x' });
    click(D.getElementById('fwCheck'), W);
    const ok = await waitFor(() => D.getElementById('fwStatus').textContent.indexOf('Нет связи с GitHub') !== -1, 8000);
    t('network_error', ok, 'txt=' + D.getElementById('fwStatus').textContent);
  } catch (e) { t('network_error', false, e.message); }
  try {
    const { W, D } = await open();
    const repo = D.getElementById('fwRepo');
    repo.value = 'empty/repo';
    repo.dispatchEvent(new W.Event('change', { bubbles: true }));
    click(D.getElementById('fwCheck'), W);
    await sleep(1500);
    const stored = W.localStorage.getItem('afh.fw.repo');
    const before = (await mockLog('/__mock/gh-log')).length;
    const f = cur.f;
    await new Promise(r => { f.onload = r; f.contentWindow.location.reload(); setTimeout(r, 15000); });
    await sleep(wait);
    const D2 = f.contentDocument;
    const repo2 = D2.getElementById('fwRepo');
    const after = (await mockLog('/__mock/gh-log')).length;
    t('repo_persisted', stored === 'empty/repo' && repo2.value === 'empty/repo' && after === before, 'stored=' + stored + ' repo2=' + repo2.value + ' before=' + before + ' after=' + after);
  } catch (e) { t('repo_persisted', false, e.message); }
  try {
    const { W, D } = await open();
    const repo = D.getElementById('fwRepo');
    t('default_repo', repo.value === 'VibeEngineering-LLC/atomfast-esp32', 'val=' + repo.value);
  } catch (e) { t('default_repo', false, e.message); }
  if (cur) { try { cur.f.remove(); } catch (e) {} cur = null; }
  for (const r of res) { if (!r.ok) failed.push(r.n + ': ' + r.d); }
  return JSON.stringify({ total: res.length, failed, res });
})()
