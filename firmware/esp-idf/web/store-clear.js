(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(); } else { root.AFSC = factory(); } })(this, function () { 'use strict';

var WORD = 'стереть';
var CONFIRM = 'CLEAR';
var ACTIONS = [
  { id: 'raw', label: 'Очистить сырые', url: '/api/history/clear', ring: 'raw', what: 'сырые записи' },
  { id: 'agg', label: 'Очистить агрегаты', url: '/api/history/clear', ring: 'agg', what: 'агрегаты' },
  { id: 'env', label: 'Очистить датчики среды', url: '/api/history/clear', ring: 'env', what: 'записи датчиков среды' },
  { id: 'all', label: 'Очистить всё', url: '/api/history/clear', ring: 'all', what: 'всю историю (сырые, агрегаты, датчики среды)' },
  { id: 'log', label: 'Очистить журнал', url: '/api/log/clear', ring: null, what: 'журнал событий платы' },
];

function find(id) {
  for (var i = 0; i < ACTIONS.length; i++) { if (ACTIONS[i].id === id) { return ACTIONS[i]; } }
  return null;
}

function bodyFor(id) {
  var a = find(id);
  if (!a) { return null; }
  return a.ring === null ? { confirm: CONFIRM } : { ring: a.ring, confirm: CONFIRM };
}

function urlFor(id) { var a = find(id); return a ? a.url : null; }

function wordOk(text) { return typeof text === 'string' && text.trim().toLowerCase() === WORD; }

function available(st) { return st !== null && typeof st === 'object' && !Array.isArray(st); }

function needsWord(id) {
  return id === 'raw' || id === 'agg' || id === 'env' || id === 'all' || id === 'log';
}

function countFor(id, hs) {
  var a = find(id);
  if (!a || id === 'log') { return null; }
  if (id === 'all') {
    var r = countFor('raw', hs);
    var g = countFor('agg', hs);
    var e = countFor('env', hs);
    if (r !== null && g !== null && e !== null) { return r + g + e; }
    return null;
  }
  if (!hs || typeof hs !== 'object') { return null; }
  var ring = hs[id];
  if (!ring || typeof ring !== 'object') { return null; }
  var c = ring.count;
  if (typeof c === 'number' && isFinite(c)) { return c; }
  return null;
}

function plural(n) {
  var m10 = n % 10;
  var m100 = n % 100;
  if (m10 === 1 && m100 !== 11) { return 'запись'; }
  if (m10 >= 2 && m10 <= 4 && (m100 < 10 || m100 >= 20)) { return 'записи'; }
  return 'записей';
}

function confirmText(id, hs) {
  var a = find(id);
  if (!a) { return ''; }
  var what = a.what;
  var mid = '';
  if (id === 'all') {
    var nr = countFor('raw', hs);
    var na = countFor('agg', hs);
    var ne = countFor('env', hs);
    if (nr !== null && na !== null && ne !== null) {
      mid = ' \u2014 \u0441\u044b\u0440\u044b\u0435 ' + nr + ', \u0430\u0433\u0440\u0435\u0433\u0430\u0442\u044b ' + na + ', \u0434\u0430\u0442\u0447\u0438\u043a\u0438 \u0441\u0440\u0435\u0434\u044b ' + ne + ' (\u0432\u0441\u0435\u0433\u043e ' + (nr + na + ne) + ')';
    }
  } else {
    var n = countFor(id, hs);
    if (n !== null) {
      mid = ' \u2014 ' + n + ' ' + plural(n);
    }
  }
  var text = '\u0411\u0443\u0434\u0435\u0442 \u0443\u0434\u0430\u043b\u0435\u043d\u043e: ' + what + mid + '. \u0414\u0430\u043d\u043d\u044b\u0435 \u0432\u0435\u0440\u043d\u0443\u0442\u044c \u043d\u0435\u043b\u044c\u0437\u044f.';
  if (a.ring !== null) {
    text += ' \u0421\u0447\u0451\u0442\u0447\u0438\u043a seq \u043d\u0435 \u0441\u0431\u0440\u0430\u0441\u044b\u0432\u0430\u0435\u0442\u0441\u044f, epoch \u043e\u0447\u0438\u0449\u0435\u043d\u043d\u044b\u0445 \u043a\u043e\u043b\u0435\u0446 \u0432\u044b\u0440\u0430\u0441\u0442\u0435\u0442 \u043d\u0430 1: \u043e\u0442\u043a\u0440\u044b\u0442\u044b\u0435 \u0433\u0440\u0430\u0444\u0438\u043a\u0438 \u0438 \u0432\u044b\u0433\u0440\u0443\u0437\u043a\u0438 \u043f\u0435\u0440\u0435\u0447\u0438\u0442\u0430\u044e\u0442 \u0438\u0441\u0442\u043e\u0440\u0438\u044e.';
  }
  return text;
}

function warnText(id) {
  var a = find(id);
  if (!a) { return ''; }
  var base = 'Будет очищено: ' + a.what + '. Данные вернуть нельзя.';
  if (a.ring === null) { return base; }
  return base + ' Счётчик seq не сбрасывается, epoch очищенных колец вырастет на 1: открытые графики и выгрузки перечитают историю.';
}

var RING_NAMES = { raw: 'сырые', agg: 'агрегаты', env: 'датчики среды' };


function resultText(id, data) {
  var a = find(id);
  if (!a) { return ''; }
  var d = data && typeof data === 'object' ? data : {};
  if (a.ring === null) { return 'Журнал очищен'; }
  var rem = d.removed;
  var parts = [];
  if (rem && typeof rem === 'object') {
    Object.keys(RING_NAMES).forEach(function (k) {
      if (typeof rem[k] === 'number' && isFinite(rem[k])) { parts.push(RING_NAMES[k] + ' ' + rem[k]); }
    });
  } else if (typeof rem === 'number' && isFinite(rem)) {
    parts.push((RING_NAMES[a.ring] || 'записей') + ' ' + rem);
  }
  var out = parts.length ? 'Очищено: ' + parts.join(', ') : 'Готово';
  if (d.persisted === false) { out += ' (во флеш не записано, действует до перезагрузки)'; }
  return out;
}

function errorText(status) {
  if (status === 400) { return 'плата отклонила запрос (400): проверьте подтверждение'; }
  if (status === 401) { return 'нужна авторизация'; }
  if (status === 403) { return 'отказ: недействительный токен, обновите страницу'; }
  if (status === 503) { return 'плата занята или файловая система не готова (503), повторите позже'; }
  if (status === 500) { return 'ошибка платы (500)'; }
  return 'плата отклонила запрос (' + status + ')';
}

return { WORD: WORD, CONFIRM: CONFIRM, ACTIONS: ACTIONS, find: find, bodyFor: bodyFor, urlFor: urlFor, wordOk: wordOk, available: available, needsWord: needsWord, countFor: countFor, confirmText: confirmText, warnText: warnText, resultText: resultText, errorText: errorText };
});
