(function (root, factory) {
  if (typeof module === 'object' && module.exports) {
    module.exports = factory();
  } else {
    root.AFSET = factory();
  }
})(this, function () {
  'use strict';

  /* ───────────────────────── Чистая часть (без DOM) ───────────────────────── */

  var WINDOWS = [
    { s: 60,   t: '60 с' },
    { s: 120,  t: '2 мин' },
    { s: 180,  t: '3 мин' },
    { s: 300,  t: '5 мин' },
    { s: 600,  t: '10 мин' },
    { s: 900,  t: '15 мин' },
    { s: 1200, t: '20 мин' }
  ];

  // Хелпер: значение — конечное число?
  function isNum(v) {
    return typeof v === 'number' && isFinite(v);
  }

  // Хелпер: секунды входят в WINDOWS?
  function inWindows(v) {
    var i;
    for (i = 0; i < WINDOWS.length; i++) {
      if (WINDOWS[i].s === v) { return true; }
    }
    return false;
  }

  /**
   * parse(j) — разобрать ответ GET /api/settings.
   * Возвращает {raw,agg,env,window_s,sigma_k,ble_ext} или null.
   */
  function parse(j) {
    // Вход должен быть объектом (не null, не массив)
    if (typeof j !== 'object' || j === null) { return null; }
    // store — объект
    if (typeof j.store !== 'object' || j.store === null) { return null; }
    // Числовые поля
    if (!isNum(j.store.raw) || !isNum(j.store.agg) || !isNum(j.store.env)) { return null; }
    if (!isNum(j.window_s)) { return null; }
    if (!isNum(j.sigma_k)) { return null; }
    if (!isNum(j.ble_ext)) { return null; }
    // Диапазоны
    if (!inWindows(j.window_s)) { return null; }
    if (j.sigma_k < 1 || j.sigma_k > 3) { return null; }
    return {
      raw:      j.store.raw === 1,
      agg:      j.store.agg === 1,
      env:      j.store.env === 1,
      window_s: j.window_s,
      sigma_k:  j.sigma_k,
      ble_ext:  j.ble_ext === 1
    };
  }

  /**
   * diff(cur, want) — патч для POST /api/settings только из изменившихся полей.
   */
  function diff(cur, want) {
    var patch = {};
    var store = {};
    var hasStore = false;

    if (cur.raw !== want.raw) { store.raw = want.raw ? 1 : 0; hasStore = true; }
    if (cur.agg !== want.agg) { store.agg = want.agg ? 1 : 0; hasStore = true; }
    if (cur.env !== want.env) { store.env = want.env ? 1 : 0; hasStore = true; }
    if (hasStore) { patch.store = store; }

    if (cur.window_s !== want.window_s) { patch.window_s = want.window_s; }
    if (cur.sigma_k !== want.sigma_k)   { patch.sigma_k  = want.sigma_k; }
    if (cur.ble_ext !== want.ble_ext)   { patch.ble_ext  = want.ble_ext ? 1 : 0; }

    return patch;
  }

  /**
   * needConfirm(resp) — true при 409 + body.error === 'need_confirm'.
   */
  function needConfirm(resp) {
    return resp.status === 409 &&
           resp.body !== null && resp.body !== undefined &&
           resp.body.error === 'need_confirm';
  }

  /**
   * confirmText(body) — текст диалога подтверждения.
   */
  function confirmText(body) {
    return 'Агрегаты (' + body.agg_count + ' записей) будут стёрты, сырые останутся. Продолжить?';
  }

  /**
   * errText(resp) — человекочитаемое сообщение об ошибке.
   */
  function errText(resp) {
    var e = (resp.body && resp.body.error) || '';
    if (e === 'unknown_field') { return 'Прошивка не знает этого поля'; }
    if (e === 'bad_value')     { return 'Недопустимое значение'; }
    if (e === 'nvs_failed')    { return 'Не удалось сохранить в память платы'; }
    if (e === 'need_confirm')  { return 'Нужно подтверждение'; }
    if (e === 'syntax')        { return 'Ошибка запроса'; }
    if (e === 'busy')          { return 'Очистка занята, повторите позже'; }
    return 'Ошибка ' + resp.status;
  }

  /* ───────────────────────── DOM-часть (браузер) ──────────────────────────── */

  // По умолчанию — noop (Node-окружение)
  var init = function () {};

  if (typeof document !== 'undefined') {
    init = function () {

      // Элементы
      var elWin   = document.getElementById('set_win');
      if (!elWin) { return; } // безопасно выходим, если разметки нет

      var elRaw   = document.getElementById('set_st_raw');
      var elAgg   = document.getElementById('set_st_agg');
      var elEnv   = document.getElementById('set_st_env');
      var elBle   = document.getElementById('set_ble_ext');
      var elSigma = document.getElementById('set_sigma');
      var elMsg   = document.getElementById('set_msg');

      var cur  = null;  // текущее состояние (результат parse)
      var busy = false; // защита от повторного apply

      /* Заполнение select-ов (вызывается один раз) */
      function buildWinOptions() {
        var i, opt;
        elWin.length = 0;
        for (i = 0; i < WINDOWS.length; i++) {
          opt = document.createElement('option');
          opt.value = String(WINDOWS[i].s);
          opt.textContent = WINDOWS[i].t;
          elWin.appendChild(opt);
        }
      }

      function buildSigmaOptions() {
        var vals = [1, 2, 3];
        var i, opt;
        elSigma.length = 0;
        for (i = 0; i < vals.length; i++) {
          opt = document.createElement('option');
          opt.value = String(vals[i]);
          opt.textContent = vals[i] + '\u03C3';
          elSigma.appendChild(opt);
        }
      }

      /* Заполнить форму из объекта состояния */
      function fill(st) {
        elRaw.checked   = st.raw;
        elAgg.checked   = st.agg;
        elEnv.checked   = st.env;
        elBle.checked   = st.ble_ext;
        elWin.value     = String(st.window_s);
        elSigma.value   = String(st.sigma_k);
        gK = st.sigma_k;
      }

      /* Собрать want из DOM */
      function collect() {
        return {
          raw:      elRaw.checked,
          agg:      elAgg.checked,
          env:      elEnv.checked,
          window_s: parseInt(elWin.value, 10),
          sigma_k:  parseInt(elSigma.value, 10),
          ble_ext:  elBle.checked
        };
      }

      /* Установить / снять disabled у всех полей */
      function setDisabled(v) {
        elRaw.disabled   = v;
        elAgg.disabled   = v;
        elEnv.disabled   = v;
        elBle.disabled   = v;
        elWin.disabled   = v;
        elSigma.disabled = v;
      }

      /* Загрузка текущего состояния с платы */
      function load() {
        fetch('/api/settings')
          .then(function (r) { return r.json(); })
          .then(function (data) {
            var st = parse(data);
            if (st === null) {
              elMsg.textContent = 'Настройки недоступны (старая прошивка)';
              setDisabled(true);
            } else {
              setDisabled(false);
              fill(st);
              cur = st;
            }
          })
          .catch(function () {
            elMsg.textContent = 'Нет связи с платой';
          });
      }

      /* POST с CSRF-токеном; onResp — обработчик ответа, onErr — сеть */
      function post(patch, onResp, onErr) {
        fetch('/api/csrf-token')
          .then(function (r) { return r.json(); })
          .then(function (tk) {
            return fetch('/api/settings', {
              method: 'POST',
              headers: {
                'Content-Type': 'application/json',
                'X-CSRF-Token': tk.token
              },
              body: JSON.stringify(patch)
            });
          })
          .then(function (r) {
            // Читаем тело как JSON; при не-JSON — пустой объект
            return r.json().then(function (body) {
              onResp({ status: r.status, body: body || {} });
            }).catch(function () {
              onResp({ status: r.status, body: {} });
            });
          })
          .catch(function () {
            onErr();
          });
      }

      /* Обработка ответа после POST */
      function handleResp(resp, patch) {
        if (needConfirm(resp)) {
          var ok = window.confirm(confirmText(resp.body));
          if (ok) {
            // Повторный запрос с confirm_clear_agg
            var patch2 = {};
            var k;
            for (k in patch) {
              if (patch.hasOwnProperty(k)) { patch2[k] = patch[k]; }
            }
            patch2.confirm_clear_agg = 1;
            post(patch2, function (resp2) {
              busy = false;
              if (resp2.status === 200) {
                elMsg.textContent = 'Сохранено';
              } else {
                elMsg.textContent = errText(resp2);
              }
              load();
            }, function () {
              busy = false;
              elMsg.textContent = 'Нет связи с платой';
              load();
            });
          } else {
            // Откат DOM к текущему
            busy = false;
            fill(cur);
          }
        } else if (resp.status === 200) {
          busy = false;
          elMsg.textContent = 'Сохранено';
          load();
        } else {
          busy = false;
          elMsg.textContent = errText(resp);
          load();
        }
      }

      /* apply — собрать want, вычислить патч, отправить */
      function apply() {
        if (busy)  { return; }
        if (cur === null) { return; }

        var want  = collect();
        var patch = diff(cur, want);

        // Патч пустой — выходим
        if (patch.store === undefined &&
            patch.window_s === undefined &&
            patch.sigma_k === undefined &&
            patch.ble_ext === undefined) {
          return;
        }

        busy = true;

        post(patch, function (resp) {
          handleResp(resp, patch);
        }, function () {
          busy = false;
          elMsg.textContent = 'Нет связи с платой';
          load();
        });
      }

      /* Слушатели change на всех шести элементах */
      function onChange() { apply(); }

      elRaw.addEventListener('change', onChange);
      elAgg.addEventListener('change', onChange);
      elEnv.addEventListener('change', onChange);
      elBle.addEventListener('change', onChange);
      elWin.addEventListener('change', onChange);
      elSigma.addEventListener('change', onChange);

      /* Первичная отрисовка select-ов и загрузка */
      buildWinOptions();
      buildSigmaOptions();
      load();
    };
  }

  /* Коэффициент охвата k для страниц: читается один раз при загрузке (1 до ответа платы) */
  var gK = 1;
  if (typeof fetch === 'function' && typeof document !== 'undefined') {
    fetch('/api/settings').then(function (r) { return r.json(); }).then(function (j) {
      var p = parse(j);
      if (p) { gK = p.sigma_k; }
    }).catch(function () {});
  }

  /* ───────────────────────── Экспорт фабрики ──────────────────────────────── */

  return {
    WINDOWS:      WINDOWS,
    parse:        parse,
    diff:         diff,
    needConfirm:  needConfirm,
    confirmText:  confirmText,
    errText:      errText,
    init:         init,
    k:            function () { return gK; }
  };
});
