var scPending = null;
var scBusy = false;
var scStep = 1;

function scEl(id) { return document.getElementById(id); }

function scSay(text, cls) {
  var el = scEl('storeResult');
  if (!el) { return; }
  el.className = 'result' + (cls ? ' ' + cls : '');
  el.textContent = text;
}

function scAvail() {
  var host = scEl('storeBtns');
  if (!host) { return; }
  var ok = typeof sysData === 'object' && sysData !== null && AFSC.available(sysData.st);
  Array.prototype.forEach.call(host.querySelectorAll('button'), function (b) { b.disabled = !ok || scBusy; });
  var hint = scEl('storeAvail');
  if (hint) { hint.hidden = ok; }
}

function scClose() {
  scPending = null;
  scStep = 1;
  var w = scEl('scWord');
  if (w) { w.value = ''; }
  var wb = scEl('scWordBox');
  if (wb) { wb.hidden = true; }
  var dlg = scEl('scDlg');
  if (dlg) { dlg.hidden = true; }
  var bd = scEl('scBackdrop');
  if (bd) { bd.hidden = true; }
}

function scOpen(id) {
  if (scBusy || !AFSC.find(id) || !(typeof sysData === 'object' && AFSC.available(sysData.st))) { return; }
  scPending = id;
  scStep = 1;
  var hs = (typeof sysData === 'object' && sysData !== null) ? sysData.hs : null;
  if (hs !== null && (typeof hs !== 'object' || Array.isArray(hs))) { hs = null; }
  scEl('scDlgTitle').textContent = AFSC.find(id).label + ': подтверждение';
  scEl('scDlgText').textContent = AFSC.confirmText(id, hs);
  scEl('scWord').value = '';
  var wordBox = scEl('scWordBox');
  if (wordBox) { wordBox.hidden = true; }
  var okBtn = scEl('scDlgOk');
  if (AFSC.needsWord(id)) {
    okBtn.textContent = 'Продолжить';
  } else {
    okBtn.textContent = 'Очистить';
  }
  okBtn.disabled = false;
  scEl('scBackdrop').hidden = false;
  scEl('scDlg').hidden = false;
  scEl('scDlgCancel').focus();
}

function scOk() {
  if (scBusy || !scPending) { return; }
  if (scStep === 1 && AFSC.needsWord(scPending)) {
    scStep = 2;
    var wordBox = scEl('scWordBox');
    if (wordBox) { wordBox.hidden = false; }
    scEl('scDlgOk').textContent = 'Очистить';
    scEl('scDlgOk').disabled = !AFSC.wordOk(scEl('scWord').value);
    scEl('scWord').focus();
    return;
  }
  if (AFSC.needsWord(scPending)) {
    if (!AFSC.wordOk(scEl('scWord').value)) { return; }
  }
  scDo();
}

async function scDo() {
  var id = scPending;
  if (!id || scBusy) { return; }
  if (AFSC.needsWord(id) && !AFSC.wordOk(scEl('scWord').value)) { return; }
  scBusy = true;
  scEl('scDlgOk').disabled = true;
  scAvail();
  scSay('очищаю…', '');
  try {
    var r = await postJSON(AFSC.urlFor(id), AFSC.bodyFor(id));
    if (r.ok) {
      scSay(AFSC.resultText(id, r.data), 'ok');
      scClose();
      state.spansAt = null; refresh();
    } else {
      scSay(AFSC.errorText(r.status), 'bad');
    }
  } catch (e) {
    scSay('нет связи с платой', 'bad');
  } finally {
    scBusy = false;
    scAvail();
  }
}

function initStoreClear() {
  var host = scEl('storeBtns');
  if (!host || host.children.length) { return; }
  AFSC.ACTIONS.forEach(function (a) {
    var b = document.createElement('button');
    b.type = 'button';
    b.className = 'btn' + (a.id === 'all' || a.id === 'log' ? ' danger' : '');
    b.id = 'scBtn_' + a.id;
    b.textContent = a.label;
    b.disabled = true;
    b.addEventListener('click', function () { scOpen(a.id); });
    host.appendChild(b);
  });
  scEl('scWord').addEventListener('input', function () {
    if (scStep === 2) {
      scEl('scDlgOk').disabled = scBusy || !AFSC.wordOk(scEl('scWord').value);
    }
  });
  scEl('scDlgOk').addEventListener('click', scOk);
  scEl('scDlgCancel').addEventListener('click', scClose);
  var backdrop = scEl('scBackdrop');
  if (backdrop) { backdrop.addEventListener('click', scClose); }
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape' && scPending) { scClose(); } });
  scAvail();
}
