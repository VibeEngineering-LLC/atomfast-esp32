(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const wait = P.wait || 2500;
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const res = [];
  const t = (n, cond, d) => { res.push({ n, ok: !!cond, d: d || '' }); };
  const cfg = q => fetch('/__mock/board?' + q, { method: 'POST' });
  const boardLog = async () => (await fetch('/__mock/board-log')).json();
  const ghLog = async () => (await fetch('/__mock/gh-log')).json();
  const otaLog = async () => (await fetch('/__mock/ota-log')).json();
  const count = (log, method, path) => log.filter(e => e.method === method && e.path === path).length;
  let cur = null;
  const open = async () => {
    if (cur) { try { cur.f.remove(); } catch (e) {} cur = null; }
    try { localStorage.clear(); } catch (e) {}
    const f = document.createElement('iframe');
    f.style.cssText = 'position:fixed;left:0;top:0;border:0;opacity:0;pointer-events:none;width:' + width + 'px;height:900px;';
    f.src = location.origin + '/system#firmware';
    document.body.appendChild(f);
    await new Promise(r => { f.onload = r; setTimeout(r, 15000); });
    await sleep(wait);
    const W = f.contentWindow;
    const D = f.contentDocument;
    cur = { f, W, D };
    return { f, W, D };
  };
  const waitFor = async (fn, ms) => { const t0 = Date.now(); while (Date.now() - t0 < ms) { if (fn()) return true; await sleep(300); } return false; };
  const click = (el, W) => el.dispatchEvent(new W.MouseEvent('click', { bubbles: true, cancelable: true }));
  const setChecked = (el, v, W) => { el.checked = v; el.dispatchEvent(new W.Event('change', { bubbles: true })); };
  const visible = el => !!el && el.offsetParent !== null;
  const txt = (D, id) => (D.getElementById(id) ? D.getElementById(id).textContent : '');
  const statusDone = (D, id, needle) => () => txt(D, id).indexOf(needle) !== -1;
  const runCheck = async (q) => {
    await cfg('check=' + q);
    const o = await open();
    click(o.D.getElementById('fbCheck'), o.W);
    await waitFor(() => (txt(o.D, 'fbStatus').length > 0 && txt(o.D, 'fbStatus').indexOf('проверяет') === -1), 15000);
    return o;
  };
  const installFlow = async (q, extraWait) => {
    await cfg('check=ok&' + q);
    const o = await open();
    click(o.D.getElementById('fbCheck'), o.W);
    await waitFor(() => visible(o.D.getElementById('fbInstall')), 15000);
    click(o.D.getElementById('fbInstall'), o.W);
    setChecked(o.D.getElementById('fbUnderstand'), true, o.W);
    const seen = [];
    const obs = new o.W.MutationObserver(() => { seen.push(txt(o.D, 'fbProgText')); });
    obs.observe(o.D.getElementById('fbProgText'), { childList: true, characterData: true, subtree: true });
    click(o.D.getElementById('fbGo'), o.W);
    await waitFor(() => { const s = txt(o.D, 'fbProgText'); return s.indexOf('Готово') === 0 || s.indexOf('Установка не удалась') === 0 || s.indexOf('Плата ответила') === 0 || s.indexOf('Установка уже идёт') === 0 || s.indexOf('Плата вернулась') === 0 || s.indexOf('Плата не') === 0; }, extraWait || 60000);
    obs.disconnect();
    return { o, seen };
  };
  try {
    await cfg('check=ok');
    const { W, D } = await open();
    const sw = D.getElementById('fbBrowserOn');
    const box = D.getElementById('fwBrowserBox');
    const repo = D.getElementById('fbRepo');
    t('fallback_off_by_default', !!sw && !sw.checked && !!box && box.hidden && !visible(D.getElementById('fwOldCard')) && !visible(D.getElementById('fwRepo')), 'checked=' + (sw && sw.checked) + ' boxHidden=' + (box && box.hidden));
    t('manual_upload_outside_fallback', visible(D.getElementById('fwManualCard')) && !box.contains(D.getElementById('fwManualCard')) && visible(D.getElementById('fwFile')), '');
    t('repo_readonly_value', !!repo && repo.readOnly === true && repo.value === 'VibeEngineering-LLC/atomfast-esp32' && visible(repo), 'ro=' + (repo && repo.readOnly) + ' v=' + (repo && repo.value));
    const bl = await boardLog();
    const gl = await ghLog();
    t('no_check_or_install_before_click', count(bl, 'GET', '/api/ota/github/check') === 0 && count(bl, 'POST', '/api/ota/github/install') === 0 && gl.length === 0, 'board=' + bl.length + ' gh=' + gl.length);
    const mem = D.getElementById('fbMem');
    t('memory_shown_on_load', !!mem && mem.textContent.indexOf('КБ') !== -1 && mem.dataset.level === 'ok', mem && (mem.textContent + ' ' + mem.dataset.level));
    setChecked(sw, true, W);
    t('fallback_switch_shows_old_flow', !box.hidden && visible(D.getElementById('fwRepo')) && visible(D.getElementById('fwCheck')), '');
    setChecked(sw, false, W);
    t('fallback_switch_hides_again', box.hidden && !visible(D.getElementById('fwRepo')), '');
  } catch (e) { t('fallback_group', false, e.message); }
  try {
    await cfg('check=ok&polls=2');
    const { W, D } = await open();
    click(D.getElementById('fbCheck'), W);
    click(D.getElementById('fbCheck'), W);
    const ok = await waitFor(() => visible(D.getElementById('fbTable')), 15000);
    const bl = await boardLog();
    const gl = await ghLog();
    const checks = bl.filter(e => e.path === '/api/ota/github/check');
    const row = D.querySelector('#fbRows tr');
    const btn = D.getElementById('fbInstall');
    t('check_polls_until_done_once', ok && checks.length === 3, 'checks=' + checks.length);
    t('check_sends_csrf', checks.length > 0 && checks.every(e => e.csrf === 'mock'), JSON.stringify(checks.map(e => e.csrf)));
    t('check_row_from_board_reply', !!row && row.children[0].textContent.indexOf('1.2.40') === 0 && row.children[1].textContent === 'Можно установить' && !!btn && !btn.disabled, row && row.textContent);
    t('check_status_and_current', txt(D, 'fbStatus') === 'Доступна версия 1.2.40' && txt(D, 'fbCurrent') === '1.2.39', txt(D, 'fbStatus') + ' | ' + txt(D, 'fbCurrent'));
    t('check_never_touches_github', gl.length === 0, 'gh=' + gl.length);
    const link = row && row.querySelector('a');
    t('check_notes_link_safe', !!link && link.href.indexOf('https://github.com/') === 0 && link.rel.indexOf('noopener') !== -1, link && link.href);
  } catch (e) { t('check_group', false, e.message); }
  const blocked = [['up_to_date', 'Установлена последняя версия'], ['downgrade_blocked', 'Понижение версии через плату заблокировано'], ['too_old', 'нужна ручная заливка .bin'], ['missing_assets', 'нет файла atomfast_gw.bin']];
  for (const [q, needle] of blocked) {
    try {
      const { W, D } = await runCheck(q);
      const row = D.querySelector('#fbRows tr');
      const btn = D.getElementById('fbInstall');
      t('blocked_' + q, !!row && row.children[1].textContent.indexOf(needle) !== -1 && !!btn && btn.disabled && btn.title.indexOf(needle) !== -1, row && row.textContent);
      click(btn, W);
      t('blocked_' + q + '_no_confirm', !visible(D.getElementById('fbConfirm')), '');
    } catch (e) { t('blocked_' + q, false, e.message); }
  }
  try {
    const { D } = await runCheck('up_to_date');
    const badge = D.querySelector('#fbRows .fw-badge');
    t('up_to_date_badge', !!badge && badge.textContent === 'установлена', badge && badge.textContent);
  } catch (e) { t('up_to_date_badge', false, e.message); }
  try {
    const { D } = await runCheck('no_internet');
    const st = D.getElementById('fbStatus');
    t('no_internet_message', !visible(D.getElementById('fbTable')) && st.className.indexOf('bad') !== -1 && st.textContent.indexOf('нет доступа в интернет') !== -1, st.className + ' ' + st.textContent);
  } catch (e) { t('no_internet_message', false, e.message); }
  try {
    const { D } = await runCheck('no_releases');
    const st = D.getElementById('fbStatus');
    t('no_releases_message', !visible(D.getElementById('fbTable')) && st.className.indexOf('bad') === -1 && st.textContent.indexOf('нет подходящих релизов') !== -1, st.className + ' ' + st.textContent);
  } catch (e) { t('no_releases_message', false, e.message); }
  try {
    const { D } = await runCheck('forbidden');
    t('forbidden_message', txt(D, 'fbStatus').indexOf('токен') !== -1 && !visible(D.getElementById('fbTable')), txt(D, 'fbStatus'));
  } catch (e) { t('forbidden_message', false, e.message); }
  try {
    const { D } = await runCheck('badjson');
    t('badjson_message', txt(D, 'fbStatus').indexOf('неожиданный ответ') !== -1 && !visible(D.getElementById('fbTable')), txt(D, 'fbStatus'));
  } catch (e) { t('badjson_message', false, e.message); }
  try {
    const { W, D } = await runCheck('ok');
    click(D.getElementById('fbInstall'), W);
    const conf = D.getElementById('fbConfirm');
    const go = D.getElementById('fbGo');
    t('confirm_text_from_to', visible(conf) && txt(D, 'fbConfirmText').indexOf('с 1.2.39 на 1.2.40') !== -1 && txt(D, 'fbConfirmText').indexOf('SHA-256') !== -1, txt(D, 'fbConfirmText'));
    t('confirm_heavy_warning', txt(D, 'fbWarn').indexOf('тяжёлая') !== -1 && txt(D, 'fbWarn').indexOf('низком запасе памяти') !== -1, txt(D, 'fbWarn'));
    t('confirm_loss_of_link', txt(D, 'fbConfirmText').indexOf('Связь с платой пропадёт') !== -1 && D.getElementById('fbUnderstand').parentNode.textContent.indexOf('связь пропадёт') !== -1, '');
    t('confirm_go_disabled_until_checked', go.disabled === true && !visible(D.getElementById('fbDowngradeRow')), 'disabled=' + go.disabled);
    setChecked(D.getElementById('fbUnderstand'), true, W);
    t('confirm_go_enabled_after_check', go.disabled === false, '');
    await waitFor(() => D.getElementById('fbMem').textContent.indexOf('КБ') !== -1, 3000);
    t('confirm_shows_free_memory', D.getElementById('fbMem').textContent.indexOf('96 КБ') !== -1, D.getElementById('fbMem').textContent);
    click(D.getElementById('fbCancel'), W);
    t('confirm_cancel_hides', !visible(conf), '');
    const bl = await boardLog();
    t('confirm_does_not_install', count(bl, 'POST', '/api/ota/github/install') === 0, '');
  } catch (e) { t('confirm_group', false, e.message); }
  for (const [q, level, needle] of [['free=55000&largest=30000', 'warn', 'запас ниже'], ['free=40000&largest=30000', 'bad', 'отложить'], ['free=98000&largest=9000', 'bad', 'отложить']]) {
    try {
      await cfg('check=ok&' + q);
      const { W, D } = await open();
      click(D.getElementById('fbCheck'), W);
      await waitFor(() => visible(D.getElementById('fbInstall')), 15000);
      click(D.getElementById('fbInstall'), W);
      await waitFor(() => D.getElementById('fbMem').dataset.level === level, 5000);
      const mem = D.getElementById('fbMem');
      setChecked(D.getElementById('fbUnderstand'), true, W);
      t('memory_' + level + '_' + q, mem.dataset.level === level && mem.textContent.indexOf(needle) !== -1 && D.getElementById('fbGo').disabled === false, mem.dataset.level + ' ' + mem.textContent);
    } catch (e) { t('memory_' + q, false, e.message); }
  }
  try {
    const { o, seen } = await installFlow('install=ok');
    const bl = await boardLog();
    const gl = await ghLog();
    const ol = await otaLog();
    const inst = bl.filter(e => e.method === 'POST' && e.path === '/api/ota/github/install');
    t('install_ok_final_text', txt(o.D, 'fbProgText') === 'Готово: на плате версия 1.2.40.', txt(o.D, 'fbProgText'));
    t('install_ok_requests', inst.length === 1 && inst[0].csrf === 'mock' && count(bl, 'GET', '/api/ota/github/progress') >= 6 && gl.length === 0 && ol.length === 0, 'inst=' + inst.length + ' prog=' + count(bl, 'GET', '/api/ota/github/progress') + ' gh=' + gl.length + ' ota=' + ol.length);
    t('install_ok_progress_texts', seen.some(s => s.indexOf('Скачивание образа: ') === 0 && s.indexOf('из') !== -1) && seen.some(s => s.indexOf('Проверка SHA-256') === 0) && seen.some(s => s.indexOf('Запись в запасной слот') === 0), JSON.stringify(seen.slice(0, 8)));
    t('install_ok_new_version_shown', txt(o.D, 'fbCurrent') === '1.2.40' && o.D.getElementById('fbProg').value === 100, txt(o.D, 'fbCurrent'));
  } catch (e) { t('install_ok', false, e.message); }
  try {
    const { o, seen } = await installFlow('install=stale');
    t('install_stale_error_ignored', txt(o.D, 'fbProgText') === 'Готово: на плате версия 1.2.40.' && !seen.some(s => s.indexOf('Установка не удалась') !== -1), txt(o.D, 'fbProgText') + ' seen=' + seen.length);
  } catch (e) { t('install_stale', false, e.message); }
  try {
    const { o } = await installFlow('install=fail_sha');
    const el = o.D.getElementById('fbProgText');
    t('install_fail_sha', el.className.indexOf('bad') !== -1 && el.textContent.indexOf('SHA-256 образа не совпала') !== -1 && el.textContent.indexOf('Прежняя прошивка осталась') !== -1 && txt(o.D, 'fbCurrent') === '1.2.39', el.textContent);
    const st = await (await fetch('/api/status')).json();
    t('install_fail_no_reboot', st.fw_version === '1.2.39', st.fw_version);
  } catch (e) { t('install_fail_sha', false, e.message); }
  try {
    const { o } = await installFlow('install=fail_oom');
    t('install_fail_oom', txt(o.D, 'fbProgText').indexOf('не хватило памяти') !== -1, txt(o.D, 'fbProgText'));
  } catch (e) { t('install_fail_oom', false, e.message); }
  try {
    const { o } = await installFlow('install=conflict');
    const bl = await boardLog();
    t('install_conflict_409', txt(o.D, 'fbProgText').indexOf('Установка уже идёт') !== -1 && count(bl, 'GET', '/api/ota/github/progress') === 1, txt(o.D, 'fbProgText') + ' prog=' + count(bl, 'GET', '/api/ota/github/progress'));
  } catch (e) { t('install_conflict_409', false, e.message); }
  for (const name of ['reboot_down', 'reboot_idle']) {
    try {
      const { o } = await installFlow('install=' + name);
      t('install_' + name, txt(o.D, 'fbProgText') === 'Готово: на плате версия 1.2.40.' && txt(o.D, 'fbCurrent') === '1.2.40', txt(o.D, 'fbProgText'));
    } catch (e) { t('install_' + name, false, e.message); }
  }
  try {
    const { o } = await installFlow('install=noreboot');
    const el = o.D.getElementById('fbProgText');
    t('install_not_applied_detected', el.className.indexOf('bad') !== -1 && el.textContent.indexOf('обновление не применилось') !== -1, el.textContent);
  } catch (e) { t('install_not_applied_detected', false, e.message); }
  try {
    await cfg('check=ok&pre=1');
    const { W, D } = await open();
    const pre = D.getElementById('fbPre');
    t('channel_loaded', pre.checked === true, '');
    setChecked(pre, false, W);
    await waitFor(statusDone(D, 'fbStatus', 'Канал'), 5000);
    let bl = await boardLog();
    let posts = bl.filter(e => e.method === 'POST' && e.path === '/api/ota/github/channel');
    t('channel_off_posts', posts.length === 1 && posts[0].body === '{"prerelease":false}' && posts[0].csrf === 'mock' && txt(D, 'fbStatus').indexOf('только стабильные') !== -1, JSON.stringify(posts));
    setChecked(pre, true, W);
    await waitFor(() => txt(D, 'fbStatus').indexOf('с пред-релизами') !== -1, 5000);
    bl = await boardLog();
    posts = bl.filter(e => e.method === 'POST' && e.path === '/api/ota/github/channel');
    t('channel_on_posts', posts.length === 2 && posts[1].body === '{"prerelease":true}', JSON.stringify(posts.map(p => p.body)));
  } catch (e) { t('channel_group', false, e.message); }
  try {
    await cfg('check=ok');
    const { W, D } = await open();
    click(D.getElementById('fbCheck'), W);
    await waitFor(() => visible(D.getElementById('fbInstall')), 15000);
    setChecked(D.getElementById('fbPre'), true, W);
    await waitFor(() => txt(D, 'fbStatus').indexOf('Канал') !== -1, 5000);
    t('channel_change_clears_table', !visible(D.getElementById('fbTable')), '');
  } catch (e) { t('channel_change_clears_table', false, e.message); }
  if (cur) { try { cur.f.remove(); } catch (e) {} cur = null; }
  const failed = [];
  for (const r of res) { if (!r.ok) failed.push(r.n + ': ' + r.d); }
  return JSON.stringify({ total: res.length, failed, res });
})()
