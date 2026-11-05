(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(); } else { root.AFBL = factory(); } })(this, function () { 'use strict';

var ND = 'н/д';
var RING_MAX = 120;
var REASON_GROUPS = [['timeout', 'таймаут'], ['remote', 'удалённо'], ['local', 'локально'], ['establish', 'не удалось установить'], ['l2cap', 'L2CAP'], ['other', 'прочее']];

function isObj(v) { return v !== null && typeof v === 'object' && !Array.isArray(v); }
function isNum(v) { return typeof v === 'number' && isFinite(v); }
function pad2(n) { return (n < 10 ? '0' : '') + n; }

function num(v, dec) {
  if (!isNum(v)) { return ND; }
  var s = v.toFixed(dec === undefined ? 0 : dec);
  return s.charAt(0) === '-' && Number(s) === 0 ? s.slice(1) : s;
}

// Длительность: секунды -> "45 с", "5 мин 02 с", "1 ч 30 мин", "2 сут 3 ч"
function fmtDur(sec) {
  if (!isNum(sec) || sec < 0) { return ND; }
  var s = Math.round(sec);
  if (s < 60) { return s + ' с'; }
  if (s < 3600) { return Math.floor(s / 60) + ' мин ' + pad2(s % 60) + ' с'; }
  if (s < 86400) { return Math.floor(s / 3600) + ' ч ' + pad2(Math.floor((s % 3600) / 60)) + ' мин'; }
  return Math.floor(s / 86400) + ' сут ' + Math.floor((s % 86400) / 3600) + ' ч';
}

// Метка времени UTC (секунды) -> местное "ДД.ММ ЧЧ:ММ:СС"; null или 0 -> н/д
function fmtUtc(sec) {
  if (!isNum(sec) || sec <= 0) { return ND; }
  var d = new Date(sec * 1000);
  return pad2(d.getDate()) + '.' + pad2(d.getMonth() + 1) + ' ' + pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds());
}

// Состояние ответа /api/ble: none (нет объекта), notready (стек не готов), ok
function state(j) {
  if (!isObj(j)) { return 'none'; }
  return j.ready === true && isObj(j.link) ? 'ok' : 'notready';
}

function reasonsText(r) {
  if (!isObj(r)) { return ND; }
  return REASON_GROUPS.map(function (g) { return g[1] + ' ' + (isNum(r[g[0]]) ? r[g[0]] : ND); }).join(' · ');
}

function yn(v) { return v === true ? 'да' : (v === false ? 'нет' : ND); }

function row(k, v, hint) { var r = { k: k, v: v }; if (hint) { r.hint = hint; } return r; }

// b29 B04: доля за 24 ч есть только при ext !== false и числовом поле; иначе нет данных (не 0 %)
function up24(b) { return b.ext === false || !isObj(b.link) ? null : b.link.up_24h_pct; }

function cardLink(b) {
  var l = b.link;
  var reason = l.last_reason ? l.last_reason + (isNum(l.last_reason_code) ? ' (0x' + (l.last_reason_code < 16 ? '0' : '') + l.last_reason_code.toString(16).toUpperCase() + ')' : '') : ND;
  return { id: 'link', title: 'Связь', rows: [
    row('Подключён', yn(b.connected)),
    row('Длительность текущей сессии', b.connected === true ? fmtDur(l.session_s) : (b.connected === false ? 'нет активной сессии' : ND)),
    row('Начало текущей сессии', b.connected === true ? fmtUtc(l.session_start_utc) : ND, b.connected === true && !isNum(l.session_start_utc) ? 'время на плате не синхронизировано (SNTP)' : ''),
    row('Доля времени на связи с загрузки, %', num(l.up_boot_pct, 1)),
    row('Доля времени на связи за 24 ч, %', num(up24(b), 1)),
    row('Подключений с загрузки', num(l.connects)),
    row('Отключений с загрузки', num(l.disconnects)),
    row('Попыток подключения с загрузки', num(l.attempts)),
    row('Неудачных попыток с загрузки', num(l.fails)),
    row('Неудачных попыток подряд сейчас', num(l.fail_streak)),
    row('Подключений за всё время', num(l.connects_total)),
    row('Отключений за всё время', num(l.disconnects_total)),
    row('Неудачных попыток за всё время', num(l.fails_total)),
    row('Время на связи за всё время', fmtDur(l.conn_total_s)),
    row('Причина последнего разрыва', reason, l.last_reason_group ? 'группа: ' + l.last_reason_group : ''),
    row('Причины разрывов с загрузки', reasonsText(l.reasons)),
    row('Причины разрывов за всё время', reasonsText(l.reasons_total))
  ] };
}

function cardRate(b) {
  var r = isObj(b.rate) ? b.rate : {};
  var nom = isNum(r.nominal_ms) ? r.nominal_ms : ND;
  return { id: 'rate', title: 'Скорость', rows: [
    row('Пакетов в секунду, мгновенно', num(r.pps_inst, 3)),
    row('Пакетов в секунду, за 1 мин', num(r.pps_1m, 3)),
    row('Пакетов в секунду, за 10 мин', num(r.pps_10m, 3)),
    row('Пакетов в секунду, за сессию', num(r.pps_sess, 3)),
    row('Байт в секунду, мгновенно', num(r.bps_inst, 1)),
    row('Байт в секунду, за 1 мин', num(r.bps_1m, 1)),
    row('Байт в секунду, за 10 мин', num(r.bps_10m, 1)),
    row('Байт в секунду, за сессию', num(r.bps_sess, 1)),
    row('Интервал между пакетами, последний, мс', num(r.iv_last_ms), 'номинал ' + nom + ' мс'),
    row('Интервал между пакетами, минимум за сессию, мс', num(r.iv_min_ms)),
    row('Интервал между пакетами, среднее за сессию, мс', num(r.iv_avg_ms, 1)),
    row('Интервал между пакетами, максимум за сессию, мс', num(r.iv_max_ms)),
    row('Интервал между пакетами, минимум за 10 мин, мс', num(r.iv10_min_ms)),
    row('Интервал между пакетами, среднее за 10 мин, мс', num(r.iv10_avg_ms, 1)),
    row('Интервал между пакетами, максимум за 10 мин, мс', num(r.iv10_max_ms)),
    row('Джиттер интервала (стандартное отклонение), мс', num(r.jitter_ms, 1)),
    row('Пауз длиннее 3 номиналов, штук', num(r.pauses)),
    row('Максимальная пауза, мс', num(r.pause_max_ms)),
    row('Принято пакетов за сессию', num(r.pkts_session)),
    row('Плохих пакетов за сессию', num(r.bad_session)),
    row('Принято пакетов с загрузки', num(r.pkts_boot)),
    row('Плохих пакетов с загрузки', num(r.bad_boot)),
    row('Доля плохих пакетов, %', num(r.bad_pct, 3)),
    row('Последний плохой пакет, назад', isNum(r.last_bad_age_s) ? fmtDur(r.last_bad_age_s) : (r.last_bad_age_s === null ? 'не было' : ND)),
    row('Возраст последнего пакета, мс', num(r.last_pkt_age_ms))
  ] };
}

function cardParams(b) {
  var p = isObj(b.params) ? b.params : {};
  var phyHint = typeof p.phy_note === 'string' && p.phy_note.indexOf('n/a') === 0 ? 'API BLE 5.0 выключен в этой сборке' : (typeof p.phy_note === 'string' ? p.phy_note : '');
  var eff = isNum(p.tx_eff_level) ? p.tx_eff_level + (p.tx_eff_level === 15 ? ' (P20)' : '') : ND;
  return { id: 'params', title: 'Параметры соединения', rows: [
    row('Интервал соединения, мс', num(p.interval_ms, 2)),
    row('Slave latency, событий', num(p.latency)),
    row('Supervision timeout, мс', num(p.timeout_ms)),
    row('MTU, байт', num(p.mtu)),
    row('PHY передачи', typeof p.phy_tx === 'string' ? p.phy_tx : ND, phyHint),
    row('PHY приёма', typeof p.phy_rx === 'string' ? p.phy_rx : ND, phyHint),
    row('Мощность передачи, задана, дБм', num(p.tx_set_dbm)),
    row('Уровень мощности, фактический (по sdkconfig)', eff),
    row('RSSI сейчас, дБм', num(p.rssi_dbm)),
    row('RSSI минимум за сессию, дБм', num(p.rssi_min_dbm)),
    row('RSSI среднее за сессию, дБм', num(p.rssi_avg_dbm)),
    row('RSSI максимум за сессию, дБм', num(p.rssi_max_dbm)),
    row('Время установки соединения, мс', num(p.setup_ms))
  ] };
}

var SCAN_NONE = 'не выполнялось: плата подключена к сохранённому адресу';
var SCAN_IDLE = ND + ' (скан не шёл)';

function cardScan(b) {
  var s = isObj(b.scan) ? b.scan : {};
  var d = isObj(b.device) ? b.device : {};
  var idle = b.connected === true && s.scans === 0;
  var nm = typeof d.name === 'string' ? d.name : ND;
  var rows = [
    row('Интервал сканирования, мс', num(s.interval_ms, 1), 'как часто плата начинает цикл приёма реклам'),
    row('Окно сканирования, мс', num(s.window_ms, 1), 'сколько из каждого интервала плата слушает эфир')
  ];
  if (idle) {
    rows.unshift(row('Сканирование', SCAN_NONE, 'после подключения плата не ищет прибор заново, пока соединение живо'));
    rows.push(row('Реклам в последнем скане', SCAN_IDLE, 'пакетов-объявлений, принятых за последний скан'));
    rows.push(row('Найдено приборов в последнем скане', SCAN_IDLE, 'сколько из них оказались нужным прибором'));
    rows.push(row('От начала скана до обнаружения прибора, мс', SCAN_IDLE, 'сколько искали прибор в последний раз'));
  } else {
    rows.push(row('Сканирований с загрузки', num(s.scans), 'сколько раз плата запускала поиск прибора'));
    rows.push(row('Реклам в последнем скане', num(s.adv_last), 'пакетов-объявлений, принятых за последний скан'));
    rows.push(row('Найдено приборов в последнем скане', num(s.found_last), 'сколько из них оказались нужным прибором'));
    rows.push(row('От начала скана до обнаружения прибора, мс', num(s.detect_ms), 'сколько искали прибор в последний раз'));
  }
  rows.push(row('Адрес прибора (маска)', typeof d.addr === 'string' ? d.addr : ND, 'часть адреса скрыта звёздочками'));
  rows.push(row('Тип адреса', typeof d.addr_type === 'string' ? d.addr_type : ND, 'public: постоянный, random: меняющийся'));
  rows.push(row('Имя прибора', nm, nm === ND ? 'имя не принимается в рекламе' : 'из рекламы прибора'));
  return { id: 'scan', title: 'Сканирование', rows: rows };
}

function cardSessionStats(b) {
  var l = b.link;
  var n = isNum(l.ring_n) ? l.ring_n : 0;
  var tail = n > 0 ? 'по последним ' + n : 'нет завершённых сессий';
  return { id: 'sessions', title: 'Сессии', rows: [
    row('Последняя завершённая', fmtDur(l.last_session_s)),
    row('Самая длинная', fmtDur(l.longest_session_s)),
    row('Самая короткая', fmtDur(l.shortest_session_s)),
    row('Средняя', fmtDur(l.avg_session_s), tail),
    row('Медианная', fmtDur(l.median_session_s), tail)
  ] };
}

// Карточки раздела: пустой массив, если стек не готов или объекта нет
function cards(b) {
  if (state(b) !== 'ok') { return []; }
  // П.10: при b.ext === false (расширенная статистика выключена) только основная карточка; нет поля (старая прошивка) — как раньше
  if (b.ext === false) { return [cardLink(b)]; }
  return [cardLink(b), cardRate(b), cardParams(b), cardSessionStats(b), cardScan(b)];
}

// Гистограмма RSSI: 8 корзин по 10 дБ (прошивка: корзина 0 = выше -40 дБм, корзина k = от -(30+10k) до -(39+10k), последняя = -100 и ниже)
function histBars(p) {
  var h = isObj(p) && Array.isArray(p.rssi_hist) ? p.rssi_hist : [];
  var total = 0;
  h.forEach(function (v) { if (isNum(v) && v > 0) { total += v; } });
  return h.map(function (v, i) {
    var c = isNum(v) && v > 0 ? v : 0;
    var label = i === 0 ? 'выше -40' : (i === h.length - 1 ? '-100 и ниже' : '-' + (30 + 10 * i) + '...-' + (39 + 10 * i));
    return { label: label, count: c, frac: total > 0 ? c / total : 0 };
  });
}

// Журнал сессий: принимает только массив объектов; битые элементы пропускаются
function parseSessions(j) {
  if (!Array.isArray(j)) { return []; }
  return j.filter(function (x) { return isObj(x) && isNum(x.idx) && isNum(x.dur_s); });
}

function sessionRows(list) {
  return parseSessions(list).map(function (s) {
    var rssi = isNum(s.rssi_min_dbm) || isNum(s.rssi_avg_dbm) || isNum(s.rssi_max_dbm) ? num(s.rssi_min_dbm) + ' / ' + num(s.rssi_avg_dbm) + ' / ' + num(s.rssi_max_dbm) : ND;
    return {
      idx: String(s.idx + 1),
      start: fmtUtc(s.start_utc),
      dur: fmtDur(s.dur_s),
      reason: typeof s.reason === 'string' && s.reason !== '' ? s.reason + (isNum(s.reason_code) ? ' (' + s.reason_code + ')' : '') : ND,
      pkts: num(s.pkts),
      bad: num(s.bad),
      rssi: rssi,
      setup: num(s.setup_ms)
    };
  });
}

// Бейдж в списке слева: число разрывов за сутки по журналу (если у сессий известно время) и цвет по доле времени на связи за 24 ч
function badge(b, sessions, nowUtc) {
  if (state(b) !== 'ok') { return { text: '', level: '' }; }
  var pct = up24(b);
  var level = !isNum(pct) ? '' : (pct >= 95 ? '' : (pct >= 80 ? 'warn' : 'bad'));
  var list = parseSessions(sessions).filter(function (s) { return isNum(s.end_utc); });
  var text = '';
  if (list.length > 0 && isNum(nowUtc)) {
    text = String(list.filter(function (s) { return nowUtc - s.end_utc <= 86400 && nowUtc - s.end_utc >= 0; }).length);
  }
  return { text: text, level: level };
}

// Кольцо отсчётов для мини-графиков: последние RING_MAX значений, нет данных -> null
function pushSample(ring, v) {
  var r = Array.isArray(ring) ? ring.slice() : [];
  r.push(isNum(v) ? v : null);
  while (r.length > RING_MAX) { r.shift(); }
  return r;
}

// Путь SVG для мини-графика: разрывы в местах null, масштаб по min/max значений
function sparkPath(ring, w, h) {
  var vals = (Array.isArray(ring) ? ring : []).filter(isNum);
  if (vals.length < 2) { return { d: '', min: null, max: null }; }
  var min = Math.min.apply(null, vals);
  var max = Math.max.apply(null, vals);
  var span = max - min || 1;
  var step = w / (RING_MAX - 1);
  var off = (RING_MAX - ring.length) * step;
  var d = '';
  var pen = false;
  ring.forEach(function (v, i) {
    if (!isNum(v)) { pen = false; return; }
    var x = Math.round((off + i * step) * 10) / 10;
    var y = Math.round((h - 1 - (v - min) / span * (h - 2)) * 10) / 10;
    d += (pen ? 'L' : 'M') + x + ' ' + y;
    pen = true;
  });
  return { d: d, min: min, max: max };
}


return {
  ND: ND, RING_MAX: RING_MAX, REASON_GROUPS: REASON_GROUPS, isObj: isObj, isNum: isNum, num: num
, fmtDur: fmtDur, fmtUtc: fmtUtc, state: state, reasonsText: reasonsText, cards: cards, histBars: histBars, parseSessions: parseSessions, sessionRows: sessionRows, badge: badge, pushSample: pushSample, sparkPath: sparkPath
};
});
