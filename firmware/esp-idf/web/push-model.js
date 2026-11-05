(function (root, factory) {
  if (typeof module === 'object' && module.exports) {
    module.exports = factory();
  } else {
    root.AFPUSH = factory();
  }
})(this, function () {
  'use strict';

  var RING_LABELS = {
    raw: 'сырые',
    agg: 'агрегаты',
    env: 'датчики среды'
  };

  var RING_ORDER = ['raw', 'agg', 'env'];

  function isObj(v) {
    return v !== null && typeof v === 'object';
  }

  function isAlphaDigit(c) {
    return (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122);
  }

  function hostOk(s) {
    if (s.length < 1 || s.length > 63) return false;
    if (!isAlphaDigit(s.charCodeAt(0))) return false;
    if (!isAlphaDigit(s.charCodeAt(s.length - 1))) return false;
    var allDigitDot = true;
    for (var i = 0; i < s.length; i++) {
      var c = s.charCodeAt(i);
      if (c === 46) {
        if (i + 1 < s.length && s.charCodeAt(i + 1) === 46) return false;
      } else if (c >= 48 && c <= 57) {
        /* digit */
      } else if ((c >= 65 && c <= 90) || (c >= 97 && c <= 122)) {
        allDigitDot = false;
      } else if (c === 45) {
        allDigitDot = false;
      } else {
        return false;
      }
    }
    if (allDigitDot) {
      var parts = s.split('.');
      if (parts.length !== 4) return false;
      for (var p = 0; p < 4; p++) {
        if (parts[p].length < 1 || parts[p].length > 3) return false;
        var val = 0;
        for (var d = 0; d < parts[p].length; d++) {
          val = val * 10 + (parts[p].charCodeAt(d) - 48);
        }
        if (val > 255) return false;
      }
    }
    return true;
  }

  function validateForm(f, tokenSet) {
    if (!isObj(f)) f = {};

    var enabled = f.enabled === true;

    var hostStr = '';
    if (typeof f.host === 'string') hostStr = f.host.trim();

    if (hostStr.length > 63) {
      return { ok: false, error: 'Адрес приёмника слишком длинный (не более 63 символов)', field: 'pushHost' };
    }
    if (hostStr.length > 0 && !hostOk(hostStr)) {
      return { ok: false, error: 'Недопустимый адрес приёмника: допустимы IPv4-адрес или имя из латинских букв, цифр, точек и дефисов', field: 'pushHost' };
    }
    if (enabled && hostStr.length === 0) {
      return { ok: false, error: 'Укажите адрес приёмника', field: 'pushHost' };
    }

    var portStr = '';
    if (typeof f.port === 'string') portStr = f.port.trim();
    if (!/^\d+$/.test(portStr)) {
      return { ok: false, error: 'Порт должен быть целым числом от 1 до 65535', field: 'pushPort' };
    }
    var p = parseInt(portStr, 10);
    if (p < 1 || p > 65535) {
      return { ok: false, error: 'Порт должен быть от 1 до 65535', field: 'pushPort' };
    }

    var token = '';
    if (typeof f.token === 'string') token = f.token;
    if (token.length > 64) {
      return { ok: false, error: 'Токен слишком длинный (не более 64 символов)', field: 'pushToken' };
    }
    for (var i = 0; i < token.length; i++) {
      var cc = token.charCodeAt(i);
      if (cc < 33 || cc > 126 || cc === 34 || cc === 92) {
        return { ok: false, error: 'В токене допустимы только видимые символы ASCII без кавычек и обратной косой черты', field: 'pushToken' };
      }
    }

    var clearToken = f.clearToken === true;

    if (token.length > 0 && clearToken) {
      return { ok: false, error: 'Нельзя одновременно ввести токен и очистить сохранённый', field: 'pushTokenClear' };
    }
    if (enabled && clearToken && token.length === 0) {
      return { ok: false, error: 'Нельзя включить отправку и очистить токен', field: 'pushTokenClear' };
    }
    if (enabled && token.length === 0 && tokenSet !== true) {
      return { ok: false, error: 'Укажите токен приёмника', field: 'pushToken' };
    }

    var body = { enabled: enabled, host: hostStr, port: p };
    if (token.length > 0) body.token = token;
    if (clearToken && token.length === 0) body.clear_token = true;
    return { ok: true, body: body };
  }

  function tokenHint(j) {
    if (isObj(j) && j.token_set === true) {
      return 'токен задан (оставьте поле пустым, чтобы не менять)';
    }
    return 'токен не задан';
  }

  function anyStopped(j) {
    if (!isObj(j) || !isObj(j.ring)) return false;
    for (var i = 0; i < RING_ORDER.length; i++) {
      var r = j.ring[RING_ORDER[i]];
      if (isObj(r) && r.on !== false && r.stopped === true) return true;
    }
    return false;
  }

  function stateText(j) {
    if (!isObj(j)) return 'нет данных';
    if (j.auth_stop === true) return 'остановлено: приёмник отклонил токен';
    if (j.enabled === true && anyStopped(j)) return 'остановлено: есть кольцо с ошибкой (возобновить: кнопка «Сохранить»)';
    var st = j.state;
    if (st === 'off') return 'отправка выключена';
    if (st === 'idle') return 'включена, ждёт новых записей';
    if (st === 'sending') return 'идёт отправка';
    if (st === 'error') return 'ошибка отправки';
    return 'нет данных';
  }

  var ERR_TABLE = {
    '': '—',
    'net': 'нет связи с приёмником',
    'server': 'приёмник ответил ошибкой сервера',
    'auth': 'приёмник отклонил токен',
    'bad_batch': 'приёмник отклонил пачку данных',
    'too_big': 'пачка слишком велика для приёмника',
    'proto': 'непонятный ответ приёмника',
    'http4xx': 'приёмник ответил ошибкой запроса (4xx)',
    'no_device': 'у платы нет идентификатора прибора',
    'no_store': 'хранилище истории недоступно',
    'oom': 'не хватило памяти на плате'
  };

  function errText(name) {
    if (typeof name !== 'string') return '—';
    if (ERR_TABLE.hasOwnProperty(name)) return ERR_TABLE[name];
    return 'ошибка: ' + name;
  }

  function statusView(j) {
    if (!isObj(j)) {
      return { enabled: '—', conn: '—', sent: '—', fail: '—', lastErr: '—' };
    }
    var enabled = j.enabled === true;
    var enabledStr = enabled ? 'да' : 'нет';

    var connStr;
    if (!enabled) {
      connStr = '—';
    } else {
      if (j.state === 'error' || j.auth_stop === true || anyStopped(j)) {
        connStr = 'нет';
      } else {
        var ring = isObj(j.ring) ? j.ring : {};
        var hasOk = false;
        for (var i = 0; i < RING_ORDER.length; i++) {
          var r = ring[RING_ORDER[i]];
          if (isObj(r) && typeof r.last_ok_ts === 'number' && r.last_ok_ts > 0) {
            hasOk = true;
            break;
          }
        }
        connStr = hasOk ? 'есть' : 'ещё не было отправок';
      }
    }

    var ring2 = isObj(j.ring) ? j.ring : {};
    var sentSum = 0;
    var failSum = 0;
    for (var i2 = 0; i2 < RING_ORDER.length; i2++) {
      var r2 = ring2[RING_ORDER[i2]];
      if (isObj(r2)) {
        if (typeof r2.sent_total === 'number') sentSum += r2.sent_total;
        if (typeof r2.fail_total === 'number') failSum += r2.fail_total;
      }
    }

    var lastErr = '—';
    for (var i3 = 0; i3 < RING_ORDER.length; i3++) {
      var r3 = ring2[RING_ORDER[i3]];
      if (isObj(r3) && isObj(r3.last_err) && typeof r3.last_err.text === 'string' && r3.last_err.text.length > 0) {
        lastErr = errText(r3.last_err.text);
        break;
      }
    }

    return {
      enabled: enabledStr,
      conn: connStr,
      sent: String(sentSum),
      fail: String(failSum),
      lastErr: lastErr
    };
  }

  function ringRows(j) {
    var ring = (isObj(j) && isObj(j.ring)) ? j.ring : {};
    var result = [];
    for (var i = 0; i < RING_ORDER.length; i++) {
      var id = RING_ORDER[i];
      var label = RING_LABELS[id];
      var r = ring[id];
      var text;
      if (!isObj(r)) {
        text = 'нет данных';
      } else if (r.on === false) {
        text = 'не отправляется';
      } else {
        var pending = typeof r.pending === 'number' ? r.pending : 0;
        var ackedPart;
        if (typeof r.acked_seq === 'number' && r.acked_seq >= 0) {
          ackedPart = 'подтверждено до № ' + r.acked_seq;
        } else {
          ackedPart = 'ещё не отправляли';
        }
        var st = typeof r.sent_total === 'number' ? r.sent_total : 0;
        var ft = typeof r.fail_total === 'number' ? r.fail_total : 0;
        text = 'ждёт отправки ' + pending + '; ' + ackedPart + '; отправлено ' + st + ', ошибок ' + ft;
        if (r.stopped === true) {
          text = 'остановлено (' + errText(isObj(r.last_err) ? r.last_err.text : '') + '); возобновить: кнопка «Сохранить»; ' + text;
        }
      }
      result.push({ id: id, label: label, text: text });
    }
    return result;
  }

  // Сколько секунд ждать результат проверки: плата сначала доделывает проход по трём кольцам
  // (до timeout_s на каждое, af_push.c:407-423), затем сама проверка ждёт до timeout_s; плюс запас.
  function testWaitSeconds(j) {
    var t = (isObj(j) && typeof j.timeout_s === 'number' && j.timeout_s > 0) ? j.timeout_s : 30;
    return 4 * t + 3;
  }
  function testText(t) {
    if (!isObj(t)) return '';
    if (t.state === 'idle') return '';
    if (t.state === 'running') return 'проверка идёт…';
    if (t.state === 'done') {
      var http = typeof t.http === 'number' ? t.http : 0;
      var ms = typeof t.ms === 'number' ? t.ms : 0;
      if (typeof t.err === 'string' && t.err.length > 0) {
        var s = 'Не удалось: ' + errText(t.err);
        if (http > 0) s += ' (HTTP ' + http + ')';
        if (ms > 0) s += ', ' + ms + ' мс';
        return s;
      }
      var s2 = 'Соединение работает: приёмник ответил';
      if (http > 0) s2 += ' (HTTP ' + http + ')';
      s2 += ' за ' + ms + ' мс';
      if (t.legacy === true) s2 += '; приёмник старой версии';
      return s2;
    }
    return '';
  }

  function apiErrText(status, err) {
    if (status === 401) return 'нужна авторизация';
    if (status === 403) return 'отказ: недействительный токен страницы, обновите страницу';
    if (typeof err === 'string') {
      if (err === 'incomplete') return 'не заданы адрес приёмника и токен: сначала сохраните настройки с адресом и токеном';
      if (err === 'busy') return 'проверка уже идёт, подождите';
      if (err === 'not_ready') return 'отправка ещё запускается, повторите через несколько секунд';
      if (err === 'confirm_required') return 'нужно подтверждение';
      if (err === 'bad_value') return 'плата отклонила значения (проверьте адрес, порт и токен)';
      if (err === 'unknown_key' || err === 'bad_body' || err === 'json') return 'плата не поняла запрос (обновите страницу)';
    }
    if (status === 0 || typeof status !== 'number') return 'нет связи с платой';
    return 'плата отклонила запрос (' + status + ')';
  }

  function plural(n) {
    var d10 = n % 10;
    var d100 = n % 100;
    if (d10 === 1 && d100 !== 11) return 'запись';
    if (d10 >= 2 && d10 <= 4 && (d100 < 12 || d100 > 14)) return 'записи';
    return 'записей';
  }

  function resetConfirmText(j) {
    var ring = (isObj(j) && isObj(j.ring)) ? j.ring : {};
    var n = 0;
    for (var i = 0; i < RING_ORDER.length; i++) {
      var r = ring[RING_ORDER[i]];
      if (isObj(r) && r.on !== false) {
        if (typeof r.pending === 'number') n += r.pending;
      }
    }
    return 'Позиция отправки будет сброшена. Сейчас ждёт отправки ' + n + ' ' + plural(n) + '; после сброса отправка начнётся с самой старой записи, которая есть на плате. Данные на плате не удаляются, приёмник может получить часть записей повторно.';
  }

  return {
    testWaitSeconds: testWaitSeconds,
    validateForm: validateForm,
    tokenHint: tokenHint,
    stateText: stateText,
    errText: errText,
    statusView: statusView,
    ringRows: ringRows,
    testText: testText,
    apiErrText: apiErrText,
    resetConfirmText: resetConfirmText,
    RING_LABELS: RING_LABELS
  };
});
