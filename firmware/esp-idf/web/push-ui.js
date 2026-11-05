/* web/push-ui.js — DOM-обвязка раздела «База данных» (/system#db)
 * Вклеивается после push-model.js (глобал AFPUSH).
 * Доступно: fetchJSON(url), postJSON(url, body).
 */

var pushDirty = false;
var pushFilled = false;
var pushBusy = false;
var pushLast = null;
var pushTestShown = false;

/* ── Вспомогательные ──────────────────────────────────────────── */

function pEl(id) {
  return document.getElementById(id);
}

function pushSay(id, cls, text) {
  var el = pEl(id);
  if (!el) return;
  el.className = 'result' + (cls ? ' ' + cls : '');
  el.textContent = text;
}

function pushSet(id, text) {
  var el = pEl(id);
  if (el) el.textContent = text;
}

/* ── Рендер статуса ───────────────────────────────────────────── */

function pushRender(j) {
  var sv = AFPUSH.statusView(j);

  pushSet('p_en', sv.enabled);
  pushSet('p_state', AFPUSH.stateText(j));
  pushSet('p_conn', sv.conn);
  pushSet('p_sent', sv.sent);
  pushSet('p_fail', sv.fail);
  pushSet('p_err', sv.lastErr);

  var rows = AFPUSH.ringRows(j);
  for (var i = 0; i < rows.length; i++) {
    pushSet('p_ring_' + rows[i].id, rows[i].text);
  }

  pushSet('pushTokenHint', AFPUSH.tokenHint(j));

  var warnEl = pEl('pushWarn');
  if (warnEl) {
    warnEl.hidden = !(j.warn && j.warn.public_addr === true);
  }

  /* pushTestResult не трогаем: результат проверки выводится только из pushTest */
}

/* ── Заполнение формы ─────────────────────────────────────────── */

function pushFill(j) {
  if (pushDirty) return;   // несохранённый ввод не затираем, в том числе до первого заполнения

  var focused = document.activeElement;
  var skip = focused && (
    focused.id === 'pushEnabled'  ||
    focused.id === 'pushHost'     ||
    focused.id === 'pushPort'     ||
    focused.id === 'pushToken'    ||
    focused.id === 'pushTokenClear'
  );
  if (skip) return;

  var el;

  el = pEl('pushEnabled');
  if (el) el.checked = j.enabled === true;

  el = pEl('pushHost');
  if (el) el.value = typeof j.host === 'string' ? j.host : '';

  el = pEl('pushPort');
  if (el) {
    el.value = (typeof j.port === 'number') ? String(j.port) : '';
  }

  el = pEl('pushToken');
  if (el) el.value = '';

  el = pEl('pushTokenClear');
  if (el) el.checked = false;

  pushFilled = true;
  pushDirty = false;
}

/* ── Обновление ───────────────────────────────────────────────── */

var pushInflight = null;

// force — дождаться идущего обновления и сделать новое
async function pushRefresh(force) {
  while (pushBusy) {
    if (!force) return;
    await pushInflight;
  }
  pushBusy = true;
  pushInflight = pushRefreshOnce();
  await pushInflight;
}

async function pushRefreshOnce() {
  try {
    var j = await fetchJSON('/api/push');
    pushLast = j;
    pushRender(j);
    pushFill(j);
  } catch (e) {
    pushSet('p_state', (e && e.status === 401)
      ? 'Нужна авторизация'
      : 'Нет связи с платой');
  } finally {
    pushBusy = false;
  }
}

/* ── Сохранение ───────────────────────────────────────────────── */

async function pushSave() {
  var f = {
    enabled:    pEl('pushEnabled').checked,
    host:       pEl('pushHost').value,
    port:       pEl('pushPort').value,
    token:      pEl('pushToken').value,
    clearToken: pEl('pushTokenClear').checked
  };

  var tokenSet = !!(pushLast && pushLast.token_set === true);
  var v = AFPUSH.validateForm(f, tokenSet);

  if (!v.ok) {
    pushSay('pushResult', 'bad', v.error);
    if (v.field) {
      var fe = pEl(v.field);
      if (fe) fe.focus();
    }
    return;
  }

  var btn = pEl('btnPushSave');
  if (btn) btn.disabled = true;
  pushSay('pushResult', '', 'сохраняю\u2026');

  try {
    var r = await postJSON('/api/push', v.body);
    if (r.ok) {
      pushSay('pushResult', 'ok',
        (r.data && r.data.persisted === false)
          ? 'применено, но не записано в память платы (действует до перезагрузки)'
          : 'сохранено');
      pushDirty = false;
      await pushRefresh(true);
    } else {
      pushSay('pushResult', 'bad', AFPUSH.apiErrText(r.status, r.data && r.data.err));
    }
  } catch (e) {
    pushSay('pushResult', 'bad', 'нет связи с платой');
  } finally {
    if (btn) btn.disabled = false;
    pEl('pushToken').value = '';
    pEl('pushTokenClear').checked = false;
  }
}

/* ── Проверка связи ───────────────────────────────────────────── */

var pushTestBusy = false;

function pushLinkText(e) {
  // 401 — нужна авторизация; иначе — нет связи с платой
  return (e && e.status === 401) ? 'нужна авторизация' : 'нет связи с платой';
}

async function pushTest() {
  if (pushTestBusy) return;   // повторное нажатие не подменяет текст «проверка идёт» и результат
  pushTestBusy = true;
  var btn = pEl('btnPushTest');
  if (btn) btn.disabled = true;
  pushSay('pushTestResult', '', 'проверка идёт…');

  try {
    var r = await postJSON('/api/push/test', {});
    if (!r.ok) {
      pushSay('pushTestResult', 'bad', AFPUSH.apiErrText(r.status, r.data && r.data.err));
      return;
    }

    var done = false;
    var pollErr = null;
    var fails = 0; // сбоев опроса подряд
    var maxPoll = AFPUSH.testWaitSeconds(pushLast);
    for (var i = 0; i < maxPoll && !done; i++) {
      await new Promise(function (ok) { setTimeout(ok, 1000); });
      var j;
      try {
        j = await fetchJSON('/api/push');
        pollErr = null;
        fails = 0;
      } catch (e) {
        pollErr = e;
        fails++;
        if (e && e.status === 401) {
          break; // авторизация сама не появится
        }
        if (fails >= 3) {
          break; // предел: 3 сбоя подряд, плата недоступна
        }
        continue; // одиночный сбой опроса не обрывает ожидание
      }
      pushLast = j;
      var t = j.test;
      if (t && t.state === 'done') {
        var txt = AFPUSH.testText(t);
        var cls = (t.err ? 'bad' : 'ok');
        pushSay('pushTestResult', cls, txt);
        pushTestShown = true;
        pushRender(j);
        done = true;
      }
    }

    if (!done) {
      if (pollErr) {
        pushSay('pushTestResult', 'bad', pushLinkText(pollErr));
      } else {
        pushSay('pushTestResult', 'bad', 'приёмник не ответил за ' + maxPoll + ' с');
      }
      pushTestShown = true;
    }
  } catch (e) {
    pushSay('pushTestResult', 'bad', pushLinkText(e));
    pushTestShown = true;
  } finally {
    if (btn) btn.disabled = false;
    pushTestBusy = false;
  }
}

/* ── Сброс позиции ────────────────────────────────────────────── */

function pushResetOpen() {
  if (!pushLast) {
    pushSay('pushResult', 'bad', 'нет данных от платы');
    return;
  }
  pEl('pushDlgText').textContent = AFPUSH.resetConfirmText(pushLast);
  pEl('pushBackdrop').hidden = false;
  pEl('pushDlg').hidden = false;
  pEl('pushDlgCancel').focus();
}

function pushResetClose() {
  pEl('pushDlg').hidden = true;
  pEl('pushBackdrop').hidden = true;
}

async function pushResetOk() {
  pushResetClose();
  try {
    var r = await postJSON('/api/push/reset', { confirm: true });
    if (r.ok) {
      pushSay('pushResult', 'ok', 'позиция отправки сброшена');
      await pushRefresh(true);
    } else {
      pushSay('pushResult', 'bad', AFPUSH.apiErrText(r.status, r.data && r.data.err));
    }
  } catch (e) {
    pushSay('pushResult', 'bad', 'нет связи с платой');
  }
}

/* ── Инициализация ────────────────────────────────────────────── */

function initPush() {
  if (!pEl('btnPushSave')) return;

  /* Кнопки */
  pEl('btnPushSave').addEventListener('click', pushSave);
  pEl('btnPushTest').addEventListener('click', pushTest);
  pEl('btnPushReset').addEventListener('click', pushResetOpen);
  pEl('pushDlgCancel').addEventListener('click', pushResetClose);
  pEl('pushBackdrop').addEventListener('click', pushResetClose);
  pEl('pushDlgOk').addEventListener('click', pushResetOk);

  /* Пометка «изменено» */
  var dirtyFields = ['pushEnabled', 'pushHost', 'pushPort', 'pushToken', 'pushTokenClear'];
  for (var i = 0; i < dirtyFields.length; i++) {
    (function (id) {
      var el = pEl(id);
      if (!el) return;
      el.addEventListener('input',  function () { pushDirty = true; });
      el.addEventListener('change', function () { pushDirty = true; });
    })(dirtyFields[i]);
  }

  /* Escape закрывает диалог */
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape' && !pEl('pushDlg').hidden) {
      pushResetClose();
    }
  });

  /* Первый загрузочный цикл */
  pushRefresh();

  /* Периодическое обновление */
  setInterval(function () {
    if (document.visibilityState !== 'visible') return;
    var sec = pEl('sec-db');
    if (!sec || sec.hidden) return;
    pushRefresh();
  }, 5000);
}
