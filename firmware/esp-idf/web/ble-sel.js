(function (root, factory) {
  if (typeof module === 'object' && module.exports) {
    module.exports = factory();
  } else {
    root.AFSEL = factory();
  }
})(this, function () {
  'use strict';

  /* ── Чистая часть (без DOM) ── */

  var FAST = { scan: 1, stopping: 1, opening: 1, confirming: 1, forgetting: 1 };
  var KIND_LBL = { atomfast: 'AtomFast', atomswift: 'AtomSwift', family: 'Семейство' };

  function kLbl(k) { return KIND_LBL[k] || k || ''; }

  function pollMs(state, hidden) {
    if (hidden) { return 0; }
    return FAST[state] ? 1000 : 5000;
  }

  function errText(code, body) {
    body = body || {};
    var e = body.error || '';
    if (code === 409) {
      if (e === 'need_replace') { return 'Связь будет разорвана на время выбора. Нажмите «Сменить прибор» ещё раз для подтверждения'; }
      if (e === 'need_clear')   { return 'История будет стёрта (записей: ' + (body.records != null ? body.records : '?') + ')'; }
      if (e === 'meas_active')  { return 'Идёт измерение — сначала остановите его'; }
      if (e === 'stale')        { return 'Список устарел, обновите'; }
      if (e === 'cooldown')     { return 'Подождите несколько секунд'; }
      if (e === 'busy')         { return 'Устройство занято, повторите'; }
      if (e === 'live')         { return 'Прибор уже подключён'; }
    }
    if (code === 404) { return 'Прибор не найден в списке'; }
    if (code === 400) { return 'Неверный запрос'; }
    if (code === 403) { return 'Сессия устарела, обновите страницу'; }
    if (code === 503) { return 'Bluetooth не готов'; }
    return 'Ошибка ' + code;
  }

  /* err/note в JSON прошивки - строки (ble__jnote) */
  function noteText(s) {
    if (s === 'deferred') { return 'Команда отложена до конца попытки подключения'; }
    if (s === 'incompatible') { return 'Прибор не подходит: формат пакетов не совпал'; }
    if (s === 'clear_failed') { return 'Не удалось стереть историю, прибор не сменён'; }
    if (s === 'tgt_write_failed') { return 'Не удалось сохранить прибор в памяти'; }
    if (s === 'name_unsaved') { return 'Прибор сохранён, но его имя не удалось записать'; }
    if (s === 'meas_active') { return 'Идёт измерение — сначала остановите его'; }
    if (s === 'aborted') { return 'Смена прибора прервана'; }
    if (s === 'close_failed') { return 'Не удалось разорвать связь'; }
    if (s === 'scan_start_failed') { return 'Не удалось запустить сканирование'; }
    if (s === 'no_cand') { return 'Прибор не найден в списке'; }
    return 'Ошибка: ' + s;
  }

  function confirmText(kind, d) {
    d = d || {};
    var tgt = d.tgt || {};
    var nm = tgt.name || kLbl(tgt.kind) || 'прибор';
    var ad = tgt.addr || '';
    if (kind === 'change') { return 'Связь будет разорвана на время выбора (до 3 мин). Продолжить?'; }
    if (kind === 'clear')  { return 'История ' + nm + ' (' + ad + ') будет стёрта. Продолжить?'; }
    if (kind === 'forget') { return 'Забыть ' + nm + ' (' + ad + ')? История будет стёрта, запись остановится.'; }
    return 'Продолжить?';
  }

  function parseAddr(s) {
    if (typeof s !== 'string') { return null; }
    if (!/^(?:[0-9A-Fa-f]{12}|[0-9A-Fa-f]{2}([:-])(?:[0-9A-Fa-f]{2}\1){4}[0-9A-Fa-f]{2})$/.test(s)) { return null; }
    var t = s.replace(/[:\-]/g, '').toUpperCase();
    if (t.length !== 12) { return null; }
    for (var i = 0; i < 12; i++) {
      var c = t.charCodeAt(i);
      if (!((c >= 48 && c <= 57) || (c >= 65 && c <= 70))) { return null; }
    }
    return t.slice(0,2)+':'+t.slice(2,4)+':'+t.slice(4,6)+':'+t.slice(6,8)+':'+t.slice(8,10)+':'+t.slice(10,12);
  }

  function viewModel(d) {
    if (!d || typeof d !== 'object') {
      return { head:'', rows:[], canRescan:false, canForget:false, canCancel:false, canPick:false, hint:'' };
    }
    var st = d.state || '';
    var cands = Array.isArray(d.cands) ? d.cands : [];
    var tgt = d.tgt || null;
    var head = '';

    if (st === 'live' && tgt) {
      head = 'Подключён: ' + (tgt.name || kLbl(tgt.kind)) + ' (' + tgt.addr + ')';
    } else if (st === 'wait') {
      var mins = Math.max(1, Math.round((d.lost_s || 0) / 60));
      head = 'Связь потеряна ' + mins + ' мин, пробую каждые до 60 с';
    } else if (st === 'scan' || st === 'stopping') {
      head = 'Сканирую эфир…';
    } else if (st === 'list') {
      head = 'Найдено: ' + cands.length;
    } else if (st === 'opening') {
      head = d.note === 'deferred' ? 'Ждём завершения попытки подключения' : 'Подключаюсь…';
    } else if (st === 'confirming') {
      head = 'Проверяю прибор…';
    } else if (st === 'forgetting') {
      head = 'Забываю прибор…';
    }

    var rows = [];
    for (var i = 0; i < cands.length; i++) {
      var c = cands[i];
      rows.push({
        tok: c.tok, name: c.name || '', kind: c.kind || '',
        rssi: c.rssi != null ? c.rssi : 0, addr: c.addr || '',
        age: ((c.age_ms != null ? c.age_ms : 0) / 1000).toFixed(1),
        isTgt: !!c.is_tgt, label: kLbl(c.kind)
      });
    }

    var canPick = (st === 'list' || st === 'wait');
    var canRescan = (st === 'live' || st === 'wait' || st === 'list');
    var canForget = (st === 'live' || st === 'wait') && !!tgt;
    var canCancel = (st === 'list') && !!tgt;
    var hint = d.err ? noteText(d.err) : '';

    return { head:head, rows:rows, canRescan:canRescan, canForget:canForget, canCancel:canCancel, canPick:canPick, hint:hint };
  }

  /* ── DOM-часть ── */

  var init = function () {};

  if (typeof document !== 'undefined') {
    init = function () {
      var elHead = document.getElementById('sel_head');
      if (!elHead) { return; }

      var elRows = document.getElementById('sel_rows');
      var elMsg = document.getElementById('sel_msg');
      var elRescan = document.getElementById('sel_rescan');
      var elForget = document.getElementById('sel_forget');
      var elCancel = document.getElementById('sel_cancel');
      var elAddr = document.getElementById('sel_manual_addr');
      var elManBtn = document.getElementById('sel_manual_btn');

      var last = null, timer = null, busy = false;

      function render(d) {
        var vm = viewModel(d);
        elHead.textContent = vm.head;
        elRows.innerHTML = '';
        for (var i = 0; i < vm.rows.length; i++) {
          var r = vm.rows[i];
          var tr = document.createElement('tr');
          var cells = [String(i+1), r.name, r.label, r.rssi+' дБм', r.addr, r.age+' с'];
          for (var j = 0; j < cells.length; j++) {
            var td = document.createElement('td');
            td.textContent = cells[j];
            tr.appendChild(td);
          }
          var tdB = document.createElement('td');
          var btn = document.createElement('button');
          btn.textContent = 'Подключить';
          btn.disabled = !vm.canPick;
          (function (tok) { btn.addEventListener('click', function () { doSelect(tok); }); })(r.tok);
          tdB.appendChild(btn);
          tr.appendChild(tdB);
          elRows.appendChild(tr);
        }
        var st = d.state || '';
        elRescan.textContent = st === 'live' ? 'Сменить прибор…' : 'Пересканировать эфир';
        elRescan.disabled = !vm.canRescan;
        elForget.disabled = !vm.canForget;
        elCancel.style.display = vm.canCancel ? '' : 'none';
        elAddr.disabled = !vm.canPick;
        elManBtn.disabled = !vm.canPick;
        elMsg.textContent = vm.hint || '';
      }

      function post(url, body, cb) {
        fetch('/api/csrf-token')
          .then(function (r) { return r.json(); })
          .then(function (tk) {
            return fetch(url, {
              method: 'POST',
              headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': tk.token },
              body: JSON.stringify(body)
            });
          })
          .then(function (r) {
            return r.json().then(function (b) { cb({ status: r.status, body: b || {} }); })
              .catch(function () { cb({ status: r.status, body: {} }); });
          })
          .catch(function () { cb({ status: 0, body: {} }); });
      }

      function showErr(resp) {
        elMsg.textContent = resp.status === 0 ? 'Нет связи' : errText(resp.status, resp.body);
      }

      function doSelect(tok) {
        if (busy || !last) { return; }
        busy = true;
        post('/api/ble/select', { gen: last.gen, tok: tok, confirm_clear: false }, function (resp) {
          busy = false;
          if (resp.status === 409 && resp.body.error === 'need_clear') {
            if (window.confirm(confirmText('clear', last))) {
              busy = true;
              post('/api/ble/select', { gen: last.gen, tok: tok, confirm_clear: true }, function (r2) {
                busy = false;
                if (r2.status === 200 || r2.status === 202) { poll(); } else { showErr(r2); poll(); }
              });
            }
          } else if (resp.status === 200 || resp.status === 202) { poll(); }
          else { showErr(resp); }
        });
      }

      function doRescan() {
        if (busy || !last) { return; }
        var isLive = last.state === 'live';
        if (isLive && !window.confirm(confirmText('change', last))) { return; }
        busy = true;
        post('/api/ble/rescan', { replace: isLive }, function (resp) {
          busy = false;
          if (resp.status === 200 || resp.status === 202) { poll(); } else { showErr(resp); }
        });
      }

      function doForget() {
        if (busy || !last) { return; }
        if (!window.confirm(confirmText('forget', last))) { return; }
        busy = true;
        post('/api/ble/forget', { confirm: true }, function (resp) {
          busy = false;
          if (resp.status === 200 || resp.status === 202) { poll(); } else { showErr(resp); }
        });
      }

      function doCancel() {
        if (busy) { return; }
        busy = true;
        post('/api/ble/cancel', {}, function (resp) {
          busy = false;
          if (resp.status === 200) { poll(); } else { showErr(resp); }
        });
      }

      function doManual() {
        if (busy) { return; }
        var addr = parseAddr(elAddr.value.trim());
        if (!addr) { elMsg.textContent = 'Неверный адрес'; return; }
        busy = true;
        post('/api/ble/manual', { addr: addr, atype: 0, confirm_clear: false }, function (resp) {
          busy = false;
          if (resp.status === 409 && resp.body.error === 'need_clear') {
            if (window.confirm(confirmText('clear', last))) {
              busy = true;
              post('/api/ble/manual', { addr: addr, atype: 0, confirm_clear: true }, function (r2) {
                busy = false;
                if (r2.status === 200 || r2.status === 202) { poll(); } else { showErr(r2); poll(); }
              });
            }
          } else if (resp.status === 200 || resp.status === 202) { elAddr.value = ''; poll(); }
          else { showErr(resp); }
        });
      }

      function schedule(d) {
        if (timer) { clearTimeout(timer); timer = null; }
        var ms = pollMs(d ? d.state : 'wait', document.hidden);
        if (ms > 0) { timer = setTimeout(poll, ms); }
      }

      function poll() {
        if (document.hidden) { return; }
        fetch('/api/ble/cands')
          .then(function (r) { return r.json(); })
          .then(function (d) { last = d; render(d); schedule(d); })
          .catch(function () { elMsg.textContent = 'Нет связи с устройством'; schedule({ state: 'wait' }); });
      }

      elRescan.addEventListener('click', doRescan);
      elForget.addEventListener('click', doForget);
      elCancel.addEventListener('click', doCancel);
      elManBtn.addEventListener('click', doManual);

      document.addEventListener('visibilitychange', function () {
        if (!document.hidden) { poll(); }
        else if (timer) { clearTimeout(timer); timer = null; }
      });

      poll();
    };
  }

  return {
    pollMs: pollMs,
    viewModel: viewModel,
    errText: errText,
    noteText: noteText,
    confirmText: confirmText,
    parseAddr: parseAddr,
    init: init
  };
});
