var fbRow = null;
var fbMemInfo = null;

function fbEl(id) {
  return document.getElementById(id);
}

function fbSleep(ms) {
  return new Promise(r => setTimeout(r, ms));
}

function fbSay(id, text, cls) {
  const el = fbEl(id);
  if (!el) return;
  el.className = 'result' + (cls ? ' ' + cls : '');
  el.textContent = text;
}

async function fbToken() {
  const t = await fetchJSON(AFFB.ROUTES.csrf);
  return t && typeof t.token === 'string' ? t.token : '';
}

async function fbRefreshMem() {
  let sy = null;
  try {
    sy = await fetchJSON(AFFB.ROUTES.system);
  } catch (e) {
    sy = null;
  }
  fbMemInfo = AFFB.memInfo(sy);
  const el = fbEl('fbMem');
  el.textContent = fbMemInfo.text;
  el.className = 'fw-msg ' + (fbMemInfo.level === 'ok' ? 'ok' : (fbMemInfo.level === 'warn' ? 'warn' : (fbMemInfo.level === 'bad' ? 'bad' : '')));
  el.dataset.level = fbMemInfo.level;
  return fbMemInfo;
}

function fbBadge(text, cls) {
  const s = document.createElement('span');
  s.className = 'fw-badge ' + cls;
  s.textContent = text;
  return s;
}

function fbRender(c) {
  const body = fbEl('fbRows');
  body.textContent = '';
  const tr = document.createElement('tr');
  const c1 = document.createElement('td');
  c1.textContent = c.latest;
  if (c.reason === 'up_to_date') c1.appendChild(fbBadge('установлена', 'ok'));
  if (c.installable) c1.appendChild(fbBadge('новее', 'ok'));
  const c2 = document.createElement('td');
  c2.textContent = c.reasonText;
  const c3 = document.createElement('td');
  if (c.htmlUrl) {
    const a = document.createElement('a');
    a.href = c.htmlUrl;
    a.target = '_blank';
    a.rel = 'noopener noreferrer';
    a.textContent = 'Заметки';
    c3.appendChild(a);
  } else {
    c3.textContent = '—';
  }
  const c4 = document.createElement('td');
  const b = document.createElement('button');
  b.type = 'button';
  b.className = 'btn';
  b.id = 'fbInstall';
  b.textContent = 'Установить';
  b.setAttribute('aria-label', 'Установить ' + c.latest);
  b.disabled = !c.installable;
  b.title = c.installable ? '' : c.reasonText;
  b.addEventListener('click', fbStart);
  c4.appendChild(b);
  tr.append(c1, c2, c3, c4);
  body.appendChild(tr);
  fbEl('fbTable').hidden = false;
}

function fbHideConfirm() {
  fbEl('fbConfirm').hidden = true;
}

function fbSyncGo() {
  if (!fbRow) return;
  const rel = AFFW.relation(fwCurrent, fbRow.latest);
  const c = AFFW.canInstall({
    understood: fbEl('fbUnderstand').checked,
    needSumConfirm: false,
    sumConfirmed: false,
    downgrade: rel === 'older',
    downgradeConfirmed: fbEl('fbDowngradeOk').checked
  });
  fbEl('fbGo').disabled = fwBusy || !c.ok;
  fbEl('fbGo').title = c.ok ? '' : c.reason;
}

function fbStart() {
  if (fwBusy || !fbRow || !fbRow.installable) return;
  const rel = AFFW.relation(fwCurrent, fbRow.latest);
  const row = { tag: fbRow.latest, relation: rel };
  fbEl('fbConfirmText').textContent = AFFW.confirmMessage(fwCurrent, row) + ' Плата сама скачает ' + AFFB.ASSET_NAME + ' с GitHub и сверит SHA-256 по ' + AFFB.SUMS_NAME + '.';
  fbEl('fbWarn').textContent = AFFB.HEAVY_WARNING;
  fbEl('fbUnderstand').checked = false;
  fbEl('fbDowngradeOk').checked = false;
  fbEl('fbDowngradeRow').hidden = rel !== 'older';
  fbEl('fbProg').hidden = true;
  fbSay('fbProgText', '', '');
  fbEl('fbConfirm').hidden = false;
  fbSyncGo();
  fbRefreshMem();
  fbEl('fbUnderstand').focus();
}

function fbShowCheck(c) {
  fbRow = c;
  if (c.current) {
    fwCurrent = c.current;
    fbEl('fbCurrent').textContent = c.current;
  }
  if (!c.latest) {
    const net = c.reason === 'no_internet' || c.reason === 'network_error';
    fbSay('fbStatus', c.reasonText, net ? 'bad' : '');
    return;
  }
  fbRender(c);
  fbSay('fbStatus', c.installable ? 'Доступна версия ' + c.latest : c.reasonText, c.installable ? 'ok' : '');
}

async function fbCheck() {
  if (fwBusy) return;
  fwBusy = true;
  fbRow = null;
  fbHideConfirm();
  fbEl('fbTable').hidden = true;
  fbSay('fbStatus', 'Плата проверяет GitHub…', '');
  try {
    const token = await fbToken();
    const maxPolls = Math.ceil(AFFB.CHECK_MAX_MS / AFFB.CHECK_POLL_MS);
    for (let i = 0; i < maxPolls; i++) {
      const r = await fetch(AFFB.ROUTES.check, { headers: { 'X-CSRF-Token': token }, cache: 'no-store' });
      if (!r.ok) {
        fbSay('fbStatus', AFFB.checkError(r.status), 'bad');
        return;
      }
      let j = null;
      try {
        j = await r.json();
      } catch (e) {
        j = null;
      }
      const c = AFFB.parseCheck(j);
      if (c.state === 'bad') {
        fbSay('fbStatus', c.error, 'bad');
        return;
      }
      if (c.state === 'done') {
        fbShowCheck(c);
        return;
      }
      await fbSleep(AFFB.CHECK_POLL_MS);
    }
    fbSay('fbStatus', 'Плата не завершила проверку за ' + Math.round(AFFB.CHECK_MAX_MS / 1000) + ' с', 'bad');
  } catch (e) {
    fbSay('fbStatus', 'Нет связи с платой', 'bad');
  } finally {
    fwBusy = false;
  }
}

async function fbWatch(baseline, say, prog) {
  let seen = false;
  let polls = 0;
  let fails = 0;
  for (;;) {
    let p = null;
    try {
      const r = await fetch(AFFB.ROUTES.progress, { cache: 'no-store' });
      p = AFFB.parseProgress(await r.json());
      fails = 0;
    } catch (e) {
      fails++;
      if (seen || fails >= 3) return 'down';
      await fbSleep(AFFB.PROGRESS_POLL_MS);
      continue;
    }
    polls++;
    if (polls > 900) return 'timeout';
    if (AFFB.isStale(baseline, p, seen, polls)) {
      await fbSleep(AFFB.PROGRESS_POLL_MS);
      continue;
    }
    if (p.state === 'idle' && seen) return 'down';
    if (p.active) seen = true;
    if (p.state === 'error') {
      say(p.errorText, 'bad');
      return 'error';
    }
    prog.value = p.percent;
    say(p.label + (p.total ? ': ' + AFFW.formatSize(p.bytes) + ' из ' + AFFW.formatSize(p.total) : ''), '');
    if (p.state === 'done') return 'done';
    await fbSleep(AFFB.PROGRESS_POLL_MS);
  }
}

async function fbFinish(before, say, prog) {
  say('Плата перезагружается…', '');
  const w = await fwWaitReboot(before, t => say(t, ''));
  if (!w.ok) {
    say('Плата не ответила за 150 с после установки. Проверьте питание и Wi-Fi; при неудаче плата вернётся на прежнюю прошивку.', 'bad');
    return;
  }
  await fwReadCurrent();
  fbEl('fbCurrent').textContent = fwCurrent || '—';
  prog.value = 100;
  if (before && fwCurrent === before) {
    say('Плата вернулась с версией ' + fwCurrent + ': обновление не применилось, осталась прежняя прошивка.', 'bad');
  } else {
    say('Готово: на плате версия ' + (fwCurrent || '—') + '.', 'ok');
  }
}

async function fbGo() {
  if (fwBusy || !fbRow) return;
  fwBusy = true;
  fbSyncGo();
  const prog = fbEl('fbProg');
  prog.hidden = false;
  prog.value = 0;
  const say = (t, c) => fbSay('fbProgText', t, c || '');
  const before = fwCurrent;
  try {
    await fbRefreshMem();
    let baseline = null;
    try {
      const pb = await fetch(AFFB.ROUTES.progress, { cache: 'no-store' });
      baseline = AFFB.parseProgress(await pb.json());
    } catch (e) {
      baseline = null;
    }
    const token = await fbToken();
    const r = await fetch(AFFB.ROUTES.install, { method: 'POST', headers: { 'X-CSRF-Token': token } });
    if (!r.ok) {
      say(AFFB.installError(r.status), 'bad');
      return;
    }
    say('Установка запущена…', '');
    const out = await fbWatch(baseline, say, prog);
    if (out === 'error') return;
    if (out === 'timeout') {
      say('Плата не сообщила о завершении установки. Проверьте плату вручную.', 'bad');
      return;
    }
    await fbFinish(before, say, prog);
  } catch (e) {
    say('Нет связи с платой. Прежняя прошивка остаётся, если установка не завершилась.', 'bad');
  } finally {
    fwBusy = false;
    fbSyncGo();
  }
}

async function fbLoadChannel() {
  try {
    const r = await fetch(AFFB.ROUTES.channel, { cache: 'no-store' });
    const j = await r.json();
    fbEl('fbPre').checked = !!(j && j.prerelease === true);
  } catch (e) {
    fbEl('fbPre').disabled = true;
  }
}

async function fbSetChannel() {
  const box = fbEl('fbPre');
  const want = box.checked;
  try {
    const token = await fbToken();
    const r = await fetch(AFFB.ROUTES.channel, {
      method: 'POST',
      headers: { 'X-CSRF-Token': token, 'Content-Type': 'application/json' },
      body: JSON.stringify({ prerelease: want })
    });
    if (!r.ok) throw new Error('status ' + r.status);
    fbRow = null;
    fbEl('fbTable').hidden = true;
    fbHideConfirm();
    fbSay('fbStatus', want ? 'Канал: с пред-релизами. Проверьте обновления заново.' : 'Канал: только стабильные релизы. Проверьте обновления заново.', '');
  } catch (e) {
    box.checked = !want;
    fbSay('fbStatus', 'Не удалось переключить канал', 'bad');
  }
}

function fbToggleBrowser() {
  const on = fbEl('fbBrowserOn').checked;
  fbEl('fwBrowserBox').hidden = !on;
  if (!on) fwHideConfirm();
}

function initBoardFw() {
  if (!fbEl('fbCheck')) return;
  fbEl('fbRepo').value = AFFB.REPO;
  fbEl('fbCheck').addEventListener('click', fbCheck);
  fbEl('fbCancel').addEventListener('click', fbHideConfirm);
  fbEl('fbUnderstand').addEventListener('change', fbSyncGo);
  fbEl('fbDowngradeOk').addEventListener('change', fbSyncGo);
  fbEl('fbGo').addEventListener('click', fbGo);
  fbEl('fbPre').addEventListener('change', fbSetChannel);
  fbEl('fbBrowserOn').addEventListener('change', fbToggleBrowser);
  fbEl('fbBrowserOn').checked = false;
  fbEl('fwBrowserBox').hidden = true;
  fbLoadChannel();
  fbRefreshMem();
  fwReadCurrent().then(() => { fbEl('fbCurrent').textContent = fwCurrent || '—'; });
}
