var FW_REPO_KEY = 'afh.fw.repo';
var fwRows = [];
var fwCurrent = null;
var fwSel = null;
var fwBytes = null;
var fwVerified = null;
var fwBusy = false;
var fwFileBytes = null;
function fwBase() {
  return AFFW.baseFromQuery(location.hostname, location.search);
}

function fwEl(id) {
  return document.getElementById(id);
}

function fwSay(id, text, cls) {
  const el = fwEl(id);
  if (!el) return;
  el.className = 'result' + (cls ? ' ' + cls : '');
  el.textContent = text;
}

function fwLoadRepo() {
  let v = AFFW.DEFAULT_REPO;
  try {
    v = localStorage.getItem(FW_REPO_KEY) || v;
  } catch (e) {
    /* ignore */
  }
  fwEl('fwRepo').value = v;
}

function fwSaveRepo(repo) {
  try {
    localStorage.setItem(FW_REPO_KEY, repo);
  } catch (e) {
    /* ignore */
  }
}

async function fwReadCurrent() {
  try {
    const s = await fetchJSON('/api/status');
    fwCurrent = (s && typeof s.fw_version === 'string') ? s.fw_version : null;
  } catch (e) {
    fwCurrent = null;
  }
  fwEl('fwCurrent').textContent = fwCurrent || '—';
  return fwCurrent;
}

function fwFail(resp) {
  return AFFW.httpErrorMessage(resp.status, n => resp.headers.get(n), Math.floor(Date.now() / 1000));
}

function fwBadge(text, cls) {
  const s = document.createElement('span');
  s.className = 'fw-badge ' + cls;
  s.textContent = text;
  return s;
}

function fwRender() {
  const body = fwEl('fwRows');
  body.textContent = '';
  for (const row of fwRows) {
    const tr = document.createElement('tr');
    const c1 = document.createElement('td');
    c1.textContent = row.tag;
    if (row.prerelease) c1.appendChild(fwBadge('пред-релиз', 'warn'));
    if (row.installed) c1.appendChild(fwBadge('установлена', 'ok'));
    const c2 = document.createElement('td');
    c2.textContent = row.date;
    const c3 = document.createElement('td');
    if (row.assets.length > 1) {
      const sel = document.createElement('select');
      sel.setAttribute('aria-label', 'Файл образа для ' + row.tag);
      row.assets.forEach((a, i) => {
        const o = document.createElement('option');
        o.value = String(i);
        o.textContent = a.name + ' (' + AFFW.formatSize(a.size) + ')';
        sel.appendChild(o);
      });
      sel.addEventListener('change', () => { row.pick = parseInt(sel.value, 10); });
      c3.appendChild(sel);
    } else if (row.assets.length === 1) {
      c3.textContent = row.assets[0].name + ' (' + AFFW.formatSize(row.assets[0].size) + ')';
    } else {
      c3.textContent = 'нет файла .bin';
    }
    const c4 = document.createElement('td');
    if (row.htmlUrl) {
      const a = document.createElement('a');
      a.href = row.htmlUrl;
      a.target = '_blank';
      a.rel = 'noopener noreferrer';
      a.textContent = 'Заметки';
      c4.appendChild(a);
    } else {
      c4.textContent = '—';
    }
    const c5 = document.createElement('td');
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'btn';
    b.textContent = row.installed ? 'Переустановить' : 'Установить';
    b.setAttribute('aria-label', b.textContent + ' ' + row.tag);
    b.disabled = row.assets.length === 0;
    b.addEventListener('click', () => fwStart(row));
    c5.appendChild(b);
    tr.append(c1, c2, c3, c4, c5);
    body.appendChild(tr);
  }
  fwEl('fwTable').hidden = fwRows.length === 0;
}

function fwHideConfirm() {
  fwSel = null;
  fwBytes = null;
  fwVerified = null;
  fwEl('fwConfirm').hidden = true;
}

function fwSyncGo() {
  if (!fwSel) return;
  const plan = AFFW.installPlan(fwCurrent, fwSel.row);
  const c = AFFW.canInstall({
    understood: fwEl('fwUnderstand').checked,
    needSumConfirm: false,
    sumConfirmed: false,
    downgrade: plan.downgrade,
    downgradeConfirmed: fwEl('fwDowngradeOk').checked
  });
  fwEl('fwGo').disabled = fwBusy || !c.ok;
  fwEl('fwGo').title = c.ok ? '' : c.reason;
}

function fwSyncUpload() {
  if (!fwSel || !fwVerified) return;
  const plan = AFFW.installPlan(fwCurrent, fwSel.row);
  const c = AFFW.canInstall({
    understood: fwEl('fwUnderstand').checked,
    needSumConfirm: fwVerified.sha === 'unconfirmed',
    sumConfirmed: fwEl('fwSumOk').checked,
    downgrade: plan.downgrade,
    downgradeConfirmed: fwEl('fwDowngradeOk').checked
  });
  fwEl('fwUpload').disabled = fwBusy || !fwVerified.ok || !c.ok;
  fwEl('fwUpload').title = c.ok ? '' : c.reason;
}

function fwStart(row) {
  if (fwBusy) return;
  const asset = row.assets[row.pick || 0];
  if (!asset) return;
  fwSel = { row: row, asset: asset };
  fwBytes = null;
  fwVerified = null;
  const plan = AFFW.installPlan(fwCurrent, row);
  fwEl('fwConfirmText').textContent = AFFW.confirmMessage(fwCurrent, row) + ' Файл: ' + asset.name + ' (' + AFFW.formatSize(asset.size) + ').';
  fwEl('fwUnderstand').checked = false;
  fwEl('fwDowngradeOk').checked = false;
  fwEl('fwDowngradeRow').hidden = !plan.downgrade;
  fwEl('fwSumOk').checked = false;
  fwEl('fwSumRow').hidden = true;
  fwEl('fwVerify').hidden = true;
  fwEl('fwUpload').hidden = true;
  fwEl('fwUpload').disabled = true;
  fwEl('fwProg').hidden = true;
  fwSay('fwProgText', '', '');
  fwEl('fwConfirm').hidden = false;
  fwSyncGo();
  fwEl('fwUnderstand').focus();
}

async function fwDownload(url, total, onProgress) {
  const resp = await AFFW.githubGet(fetch, url, AFFW.ACCEPT_BIN, fwBase());
  if (!resp.ok) {
    const err = new Error(fwFail(resp));
    err.http = true;
    throw err;
  }
  const len = parseInt(resp.headers.get('content-length') || '', 10);
  const size = isFinite(len) && len > 0 ? len : total;
  if (!resp.body || !resp.body.getReader) {
    const buf = new Uint8Array(await resp.arrayBuffer());
    onProgress(buf.length, buf.length);
    return buf;
  }
  const reader = resp.body.getReader();
  const chunks = [];
  let got = 0;
  for (;;) {
    const r = await reader.read();
    if (r.done) break;
    chunks.push(r.value);
    got += r.value.length;
    onProgress(got, size);
    if (got > AFFW.MAX_IMAGE + 1024) {
      const err = new Error('файл больше 4 МиБ');
      err.http = true;
      throw err;
    }
  }
  const out = new Uint8Array(got);
  let pos = 0;
  for (const c of chunks) {
    out.set(c, pos);
    pos += c.length;
  }
  return out;
}

function fwShowVerify() {
  const v = fwVerified;
  const box = fwEl('fwVerify');
  box.hidden = false;
  const lines = [];
  lines.push('Размер: ' + (v.sizeOk ? 'совпадает' : 'НЕ совпадает') + '; заголовок образа: ' + (v.magicOk ? 'ESP (0xE9)' : 'НЕ образ ESP'));
  if (v.sha === 'confirmed') {
    lines.push('SHA-256 подтверждена (' + v.actual.slice(0, 12) + '…)');
  } else if (v.sha === 'mismatch') {
    lines.push('SHA-256 НЕ совпала');
  } else {
    lines.push('Контрольная сумма не подтверждена: у релиза нет эталона');
  }
  box.textContent = lines.join('. ');
  box.className = 'fw-msg ' + (v.ok ? (v.sha === 'confirmed' ? 'ok' : 'warn') : 'bad');
  fwEl('fwSumRow').hidden = v.sha !== 'unconfirmed' || !v.ok;
  fwEl('fwUpload').hidden = !v.ok;
  fwSay('fwProgText', v.ok ? 'Файл проверен.' : 'Заливка заблокирована: ' + v.errors.join('; '), v.ok ? 'ok' : 'bad');
}

async function fwGo() {
  if (!fwSel || fwBusy) return;
  const row = fwSel.row;
  const asset = fwSel.asset;
  const repo = AFFW.validateRepo(fwEl('fwRepo').value).repo;
  const base = fwBase();
  fwBusy = true;
  fwSyncGo();
  fwEl('fwProg').hidden = false;
  fwEl('fwProg').value = 0;
  fwEl('fwVerify').hidden = true;
  fwEl('fwUpload').hidden = true;
  fwSay('fwProgText', 'Скачивание…', '');
  try {
    const bytes = await fwDownload(AFFW.assetUrl(repo, asset.id, base), asset.size, (g, t) => {
      fwEl('fwProg').value = t ? Math.min(100, Math.round(g * 100 / t)) : 0;
      fwSay('fwProgText', 'Скачано ' + AFFW.formatSize(g) + (t ? ' из ' + AFFW.formatSize(t) : ''), '');
    });
    let sums = null;
    if (!asset.digest && row.sumsAsset) {
      try {
        const sr = await AFFW.githubGet(fetch, AFFW.assetUrl(repo, row.sumsAsset.id, base), AFFW.ACCEPT_BIN, base);
        if (sr.ok) sums = AFFW.parseSums(await sr.text());
      } catch (e) {
        sums = null;
      }
    }
    const subtle = (window.crypto && window.crypto.subtle) ? window.crypto.subtle : null;
    fwVerified = await AFFW.verifyImage(bytes, asset, sums, subtle);
    fwBytes = bytes;
    fwShowVerify();
  } catch (e) {
    fwBytes = null;
    fwVerified = null;
    const msg = e && e.http ? e.message : AFFW.networkErrorMessage('download');
    fwSay('fwProgText', msg + (row.htmlUrl ? ' Страница релиза: ' + row.htmlUrl : ''), 'bad');
  } finally {
    fwBusy = false;
    fwSyncGo();
    fwSyncUpload();
  }
}

function fwPostImage(bytes, onProgress) {
  return fetchJSON('/api/csrf-token').then(tok => new Promise((resolve, reject) => {
    const x = new XMLHttpRequest();
    x.open('POST', '/api/ota');
    x.setRequestHeader('X-CSRF-Token', tok.token || '');
    x.setRequestHeader('Content-Type', 'application/octet-stream');
    x.upload.onprogress = e => {
      if (e.lengthComputable) onProgress(e.loaded, e.total);
    };
    x.onload = () => resolve({ status: x.status, text: x.responseText });
    x.onerror = () => reject(new Error('связь с платой прервана при заливке'));
    x.ontimeout = () => reject(new Error('плата не ответила при заливке'));
    x.timeout = 300000;
    x.send(bytes);
  }));
}

async function fwWaitReboot(beforeVersion, say) {
  let wentDown = false;
  const t0 = Date.now();
  while (Date.now() - t0 < 150000) {
    await new Promise(r => setTimeout(r, 2000));
    try {
      const ctl = new AbortController();
      const timer = setTimeout(() => ctl.abort(), 3000);
      const r = await fetch('/api/status', { cache: 'no-store', signal: ctl.signal });
      clearTimeout(timer);
      if (!r.ok) {
        wentDown = true;
        say('Плата перезагружается…');
        continue;
      }
      const s = await r.json();
      const ver = (s && typeof s.fw_version === 'string') ? s.fw_version : null;
      if (wentDown || (ver && ver !== beforeVersion)) return { ok: true, version: ver, wentDown: wentDown };
      say('Ожидание перезагрузки платы…');
    } catch (e) {
      wentDown = true;
      say('Плата перезагружается, связи нет…');
    }
  }
  return { ok: false, version: null, wentDown: wentDown };
}

async function fwFlash(bytes, ids) {
  const prog = fwEl(ids.prog);
  const say = (t, c) => fwSay(ids.text, t, c || '');
  fwBusy = true;
  prog.hidden = false;
  prog.value = 0;
  say('Заливка на плату…');
  const before = fwCurrent;
  try {
    const r = await fwPostImage(bytes, (l, t) => {
      prog.value = Math.round(l * 100 / t);
      say('Заливка: ' + AFFW.formatSize(l) + ' из ' + AFFW.formatSize(t));
    });
    if (r.status < 200 || r.status >= 300) {
      say('Плата отклонила образ (' + r.status + '): ' + String(r.text || '').slice(0, 200) + '. Прежняя прошивка осталась.', 'bad');
      return;
    }
    prog.value = 100;
    const w = await fwWaitReboot(before, t => say(t));
    if (!w.ok) {
      say('Плата не ответила за 150 с после заливки. Проверьте питание и Wi-Fi; при неудаче плата вернётся на прежнюю прошивку.', 'bad');
      return;
    }
    await fwReadCurrent();
    if (before && fwCurrent === before) {
      say('Плата вернулась с версией ' + fwCurrent + ': обновление не применилось, осталась прежняя прошивка.', 'bad');
    } else {
      say('Готово: на плате версия ' + (fwCurrent || '—') + '.', 'ok');
    }
  } catch (e) {
    say((e && e.message ? e.message : 'ошибка заливки') + '. Плата остаётся на прежней прошивке.', 'bad');
  } finally {
    fwBusy = false;
  }
}

async function fwCheck() {
  if (fwBusy) return;
  const v = AFFW.validateRepo(fwEl('fwRepo').value);
  if (!v.ok) {
    fwSay('fwStatus', v.error, 'bad');
    return;
  }
  fwSaveRepo(v.repo);
  fwSay('fwStatus', 'Запрос к GitHub…', '');
  fwEl('fwTable').hidden = true;
  fwHideConfirm();
  const base = fwBase();
  let resp;
  try {
    resp = await AFFW.githubGet(fetch, AFFW.releasesUrl(v.repo, base), AFFW.ACCEPT_JSON, base);
  } catch (e) {
    fwSay('fwStatus', AFFW.networkErrorMessage('list'), 'bad');
    return;
  }
  if (!resp.ok) {
    fwSay('fwStatus', fwFail(resp), 'bad');
    return;
  }
  let json;
  try {
    json = await resp.json();
  } catch (e) {
    fwSay('fwStatus', 'GitHub вернул неожиданный ответ', 'bad');
    return;
  }
  await fwReadCurrent();
  const r = AFFW.parseReleases(json, fwCurrent);
  if (!r.ok) {
    fwSay('fwStatus', r.error, 'bad');
    return;
  }
  fwRows = r.rows;
  fwRender();
  fwSay('fwStatus', r.rows.length ? 'Релизов: ' + r.rows.length : 'В репозитории нет опубликованных релизов', r.rows.length ? 'ok' : '');
}

function fwSyncFile() {
  fwEl('fwFileGo').disabled = fwBusy || !fwFileBytes || !fwEl('fwFileOk').checked;
}

async function fwFileChange() {
  const f = fwEl('fwFile').files && fwEl('fwFile').files[0];
  fwFileBytes = null;
  fwEl('fwFileGo').disabled = true;
  if (!f) {
    fwSay('fwFileInfo', '', '');
    return;
  }
  const buf = new Uint8Array(await f.arrayBuffer());
  const c = AFFW.checkManualFile(buf);
  if (!c.ok) {
    fwSay('fwFileInfo', f.name + ': ' + c.error, 'bad');
    return;
  }
  fwFileBytes = buf;
  fwSay('fwFileInfo', f.name + ' (' + AFFW.formatSize(buf.length) + '): проверка пройдена', 'ok');
  fwSyncFile();
}

function initFirmwareUi() {
  if (!fwEl('fwRepo')) return;
  fwLoadRepo();
  fwEl('fwCheck').addEventListener('click', fwCheck);
  fwEl('fwCancel').addEventListener('click', fwHideConfirm);
  fwEl('fwUnderstand').addEventListener('change', () => { fwSyncGo(); fwSyncUpload(); });
  fwEl('fwDowngradeOk').addEventListener('change', () => { fwSyncGo(); fwSyncUpload(); });
  fwEl('fwSumOk').addEventListener('change', fwSyncUpload);
  fwEl('fwGo').addEventListener('click', fwGo);
  fwEl('fwUpload').addEventListener('click', () => {
    if (!fwBytes || !fwVerified || !fwVerified.ok) return;
    fwEl('fwUpload').disabled = true;
    fwEl('fwGo').disabled = true;
    fwFlash(fwBytes, { prog: 'fwProg', text: 'fwProgText' }).then(() => { fwSyncGo(); fwSyncUpload(); });
  });
  fwEl('fwFile').addEventListener('change', fwFileChange);
  fwEl('fwFileOk').addEventListener('change', fwSyncFile);
  fwEl('fwFileGo').addEventListener('click', () => {
    if (!fwFileBytes) return;
    fwEl('fwFileGo').disabled = true;
    fwFlash(fwFileBytes, { prog: 'fwFileProg', text: 'fwFileText' }).then(fwSyncFile);
  });
  fwReadCurrent();
}
