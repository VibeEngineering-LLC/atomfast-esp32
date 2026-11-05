(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(); } else { root.AFSL = factory(); } })(this, function () { 'use strict';

var MAX_SENSORS = 8;
var STALE_MIN_S = 30;
var ORDER = ['bmp280', 'bme280', 'sht3x', 'aht2x'];
var MODELS = {
  bmp280: { name: 'BMP280', caps: { t: true, p: true, h: false }, addrs: ['0x76', '0x77'] },
  bme280: { name: 'BME280', caps: { t: true, p: true, h: true }, addrs: ['0x76', '0x77'] },
  sht3x: { name: 'SHT3x', caps: { t: true, p: false, h: true }, addrs: ['0x44', '0x45'] },
  aht2x: { name: 'AHT2x', caps: { t: true, p: false, h: true }, addrs: ['0x38'] }
};
var LIMITS = {
  t_off: { min: -5, max: 5, int: false, text: 'поправка температуры: от -5 до 5 °C' },
  p_off: { min: -20, max: 20, int: false, text: 'поправка давления: от -20 до 20 мм рт. ст.' },
  h_off: { min: -10, max: 10, int: false, text: 'поправка влажности: от -10 до 10 %RH' },
  period_s: { min: 5, max: 600, int: true, text: 'период измерения: целое от 5 до 600 с' },
  agg_s: { min: 1, max: 60, int: true, text: 'интервал агрегации: целое от 1 до 60 с' }
};
var KIND_NAMES = { t: 'T среды', p: 'P среды', h: 'RH среды' };
var KIND_UNITS = { t: '°C', p: 'мм рт. ст.', h: '%' };
var KIND_GROUPS = { t: 'temp', p: 'press', h: 'hum' };

function hasOwn(o, k) { return Object.prototype.hasOwnProperty.call(o, k); }
function num(v) { return typeof v === 'number' && isFinite(v) ? v : null; }
function isInt(v, lo, hi) { return typeof v === 'number' && Math.floor(v) === v && v >= lo && v <= hi; }

function modelInfo(key) {
  if (typeof key === 'string' && hasOwn(MODELS, key)) { return MODELS[key]; }
  return { name: typeof key === 'string' && key ? key : '?', caps: { t: false, p: false, h: false }, addrs: [] };
}

function kindsOf(caps) {
  if (!caps.t && !caps.p && !caps.h) { return ''; }
  return (caps.t ? 'T' : '') + (caps.p ? (caps.t ? '+P' : 'P') : '') + (caps.h ? (caps.t || caps.p ? '+RH' : 'RH') : '');
}

function capsOf(o, info) {
  return {
    t: typeof o.temp === 'boolean' ? o.temp : info.caps.t,
    p: typeof o.press === 'boolean' ? o.press : info.caps.p,
    h: typeof o.hum === 'boolean' ? o.hum : info.caps.h
  };
}

function normalizeOne(o) {
  if (!o || typeof o !== 'object' || !isInt(o.sensor, 1, MAX_SENSORS)) { return null; }
  var model = typeof o.model === 'string' ? o.model : '';
  var info = modelInfo(model);
  var caps = capsOf(o, info);
  var status = o.status === 'ok' || o.status === 'off' || o.status === 'err' ? o.status : 'err';
  var addr = typeof o.addr === 'string' && /^0x[0-9a-fA-F]{2}$/.test(o.addr) ? '0x' + o.addr.slice(2).toLowerCase() : '';
  var ago = num(o.last_ok_s_ago);
  return {
    sensor: o.sensor, model: model, name: info.name, kinds: kindsOf(caps), caps: caps, addr: addr,
    enabled: o.enabled === true, status: status, err: typeof o.err === 'string' ? o.err : '',
    t_c: caps.t && status === 'ok' ? num(o.t_c) : null, p_mmhg: caps.p && status === 'ok' ? num(o.p_mmhg) : null, h_pct: caps.h && status === 'ok' ? num(o.h_pct) : null,
    t_off: num(o.t_off) || 0, p_off: num(o.p_off) || 0, h_off: num(o.h_off) || 0, period_s: num(o.period_s) || 0,
    n_ok: isInt(o.n_ok, 0, 4294967295) ? o.n_ok : 0, n_err: isInt(o.n_err, 0, 4294967295) ? o.n_err : 0,
    last_ok_s_ago: ago === null ? -1 : Math.round(ago)
  };
}

function legacyOne(j) {
  var s = j.sensor;
  if (!s || typeof s !== 'object') { return null; }
  var st = typeof s.status === 'string' ? s.status : 'init';
  if (st === 'no_sensor' || st === 'unsupported' && !s.model) { return null; }
  var cfg = j.cfg && typeof j.cfg === 'object' ? j.cfg : {};
  var status = st === 'ok' ? 'ok' : (st === 'disabled' ? 'off' : 'err');
  return normalizeOne({
    sensor: 1, model: s.model, addr: s.addr, enabled: cfg.enabled === undefined ? st !== 'disabled' : cfg.enabled === true,
    status: status, err: status === 'err' ? (st === 'init' ? 'init' : st) : '', temp: s.temp, press: s.press, hum: s.hum,
    t_c: s.t_c, p_mmhg: s.p_mmhg, h_pct: s.h_pct, t_off: cfg.t_off, p_off: cfg.p_off, h_off: cfg.h_off, period_s: cfg.period_s,
    n_ok: s.n_ok, n_err: s.n_err, last_ok_s_ago: s.last_ok_s_ago
  });
}

function sortList(list) {
  var rank = function (m) { var i = ORDER.indexOf(m); return i === -1 ? ORDER.length : i; };
  return list.slice().sort(function (a, b) {
    var d = rank(a.model) - rank(b.model);
    if (d !== 0) { return d; }
    if (a.model !== b.model) { return a.model < b.model ? -1 : 1; }
    return a.sensor - b.sensor;
  });
}

function parseList(j) {
  var out = { list: [], legacy: false, aggS: null, maxSensors: MAX_SENSORS, lastScan: null };
  if (!j || typeof j !== 'object') { return out; }
  var agg = num(j.agg_s);
  out.aggS = agg !== null ? agg : (j.cfg && typeof j.cfg === 'object' ? num(j.cfg.agg_s) : null);
  if (isInt(j.max_sensors, 1, 64)) { out.maxSensors = j.max_sensors; }
  if (j.last_scan && typeof j.last_scan === 'object') { out.lastScan = { n: num(j.last_scan.n), changed: num(j.last_scan.changed), state: num(j.last_scan.state) }; }
  if (Array.isArray(j.sensors)) {
    var seen = {};
    j.sensors.reduce(function (acc, x) { return acc.concat(x); }, []).forEach(function (o) {   // e16g (T-47): плата отдавала [[...]]
      var n = normalizeOne(o && o.sensor && typeof o.sensor === 'object' && o.sensor.sensor !== undefined ? o.sensor : o);
      if (n && !hasOwn(seen, n.sensor)) { seen[n.sensor] = true; out.list.push(n); }
    });
    out.list = sortList(out.list);
    return out;
  }
  var l = legacyOne(j);
  out.legacy = true;
  out.list = l ? [l] : [];
  return out;
}

function statusInfo(s) {
  if (s.status === 'off' || !s.enabled) { return { key: 'off', text: 'выключен', cls: 'mut' }; }
  if (s.status === 'ok') {
    var stale = s.last_ok_s_ago >= 0 && s.last_ok_s_ago > Math.max(STALE_MIN_S, 3 * s.period_s);
    return stale ? { key: 'silent', text: 'не отвечает', cls: 'warn' } : { key: 'ok', text: 'включён', cls: 'ok' };
  }
  if (s.n_ok === 0) { return { key: 'silent', text: 'не отвечает', cls: 'warn' }; }
  return { key: 'error', text: 'ошибка' + (s.err && s.err !== 'error' ? ': ' + s.err : ''), cls: 'bad' };
}

function ageText(s) {
  var n = s.last_ok_s_ago;
  if (!(n >= 0)) { return 'нет измерений'; }
  if (n < 60) { return n + ' с назад'; }
  if (n < 3600) { return Math.floor(n / 60) + ' мин назад'; }
  return Math.floor(n / 3600) + ' ч назад';
}

function fmtNum(v, dec) {
  if (typeof v !== 'number' || !isFinite(v)) { return 'н/д'; }
  var t = v.toFixed(dec);
  return /^-0(\.0+)?$/.test(t) ? t.slice(1) : t;
}

function valueParts(s) {
  var parts = [];
  if (s.caps.t) { parts.push({ key: 't', label: 'T', text: fmtNum(s.t_c, 1), unit: KIND_UNITS.t }); }
  if (s.caps.p) { parts.push({ key: 'p', label: 'P', text: fmtNum(s.p_mmhg, 1), unit: KIND_UNITS.p }); }
  if (s.caps.h) { parts.push({ key: 'h', label: 'RH', text: fmtNum(s.h_pct, 1), unit: KIND_UNITS.h }); }
  return parts;
}

function label(s) { return s.name + (s.addr ? ' (' + s.addr + ')' : '') + ', датчик ' + s.sensor; }
function shortLabel(s) { return '№' + s.sensor + ' ' + s.name; }
function seriesName(kind, s) { return (hasOwn(KIND_NAMES, kind) ? KIND_NAMES[kind] : kind) + ' · ' + shortLabel(s); }

function groups(list) {
  var out = [];
  sortList(list).forEach(function (s) {
    var g = out.length ? out[out.length - 1] : null;
    if (!g || g.model !== s.model) {
      g = { model: s.model, name: s.name, kinds: s.kinds, sensors: [], count: 0, enabledCount: 0, allEnabled: true, anyEnabled: false };
      out.push(g);
    }
    g.sensors.push(s);
    g.count++;
    if (s.enabled) { g.enabledCount++; g.anyEnabled = true; } else { g.allEnabled = false; }
  });
  return out;
}

function summary(list) {
  var enabled = 0;
  var hasErr = false;
  list.forEach(function (s) {
    if (!s.enabled) { return; }
    enabled++;
    var k = statusInfo(s).key;
    if (k === 'error' || k === 'silent') { hasErr = true; }
  });
  return { total: list.length, enabled: enabled, hasErr: hasErr, text: 'Датчиков: ' + list.length + ' (включено ' + enabled + ')' };
}

function badge(list) {
  var s = summary(list);
  return { show: s.total > 0, text: String(s.enabled), cls: s.hasErr ? 'warn' : 'ok' };
}

function cfgFields(s) {
  var f = ['enabled', 't_off'];
  if (s.caps.p) { f.push('p_off'); }
  if (s.caps.h) { f.push('h_off'); }
  f.push('period_s');
  return f;
}

function validateField(name, v) {
  if (!hasOwn(LIMITS, name)) { return { ok: false, error: 'неизвестное поле' }; }
  var lim = LIMITS[name];
  var n = typeof v === 'string' ? Number(v.replace(',', '.').trim() === '' ? NaN : v.replace(',', '.').trim()) : v;
  if (typeof n !== 'number' || !isFinite(n)) { return { ok: false, error: 'введите число' }; }
  if (n < lim.min || n > lim.max || (lim.int && Math.floor(n) !== n)) { return { ok: false, error: lim.text }; }
  return { ok: true, value: lim.int ? n : Math.round(n * 10) / 10 };
}

function sensorPatch(s, input) {
  var body = { sensor: s.sensor, enabled: input.enabled === true };
  var fields = cfgFields(s).concat(input.agg_s !== undefined ? ['agg_s'] : []);
  for (var i = 0; i < fields.length; i++) {
    var f = fields[i];
    if (f === 'enabled') { continue; }
    var r = validateField(f, input[f]);
    if (!r.ok) { return { ok: false, field: f, error: r.error }; }
    body[f] = r.value;
  }
  return { ok: true, body: body };
}

function toggleBody(n, enabled) { return isInt(n, 1, MAX_SENSORS) ? { sensor: n, enabled: !!enabled } : null; }
function modelBody(model, enabled) { return typeof model === 'string' && hasOwn(MODELS, model) ? { model: model, enabled: !!enabled } : null; }
function removeBody(n) { return isInt(n, 1, MAX_SENSORS) ? { action: 'remove', sensor: n } : null; }
function addrChoices(model) { return typeof model === 'string' && hasOwn(MODELS, model) ? MODELS[model].addrs.slice() : []; }

function addBody(model, addr) {
  if (addrChoices(model).indexOf(addr) === -1) { return null; }
  return { action: 'add', model: model, addr: addr, enabled: false };
}

function addrTaken(list, addr) { return list.some(function (s) { return s.addr === addr; }); }
function canAdd(list, max) { return list.length < (max || MAX_SENSORS); }
function removable(s) { return s.n_ok === 0; }

function scanText(r) {
  if (!r || typeof r !== 'object' || !isInt(r.found, 0, 99)) { return 'Скан выполнен'; }
  var changed = isInt(r.changed, 0, 99) ? r.changed : 0;
  return 'Найдено ' + r.found + ', новых ' + changed;
}

function enabledWithData(list) {
  return sortList(list).filter(function (s) { return s.enabled && s.status === 'ok'; });
}

function seriesDefs(list) {
  var act = enabledWithData(list);
  var multi = act.length > 1;
  var defs = [];
  var order = { t: 0, p: 1, h: 2 };
  var fields = { t: 'air_t', p: 'air_p', h: 'air_h' };
  var decs = { t: 1, p: 1, h: 1 };
  act.forEach(function (s, i) {
    ['t', 'p', 'h'].forEach(function (k) {
      if (!s.caps[k]) { return; }
      if (!multi && defs.some(function (d) { return d.kind === k; })) { return; }
      defs.push({
        id: multi ? fields[k] + '_' + s.sensor : fields[k], kind: k, sensor: s.sensor, tint: multi ? i : 0,
        name: multi ? seriesName(k, s) : KIND_NAMES[k] + ' · ' + shortLabel(s), unit: KIND_UNITS[k], group: KIND_GROUPS[k],
        field: multi ? fields[k] + '_' + s.sensor : fields[k], dec: decs[k], order: order[k]
      });
    });
  });
  return defs.sort(function (a, b) { return a.order - b.order || a.sensor - b.sensor; });
}

function envSensors(rows) {
  var seen = {};
  var out = [];
  (Array.isArray(rows) ? rows : []).forEach(function (r) {
    var n = r && isInt(r.sensor, 1, MAX_SENSORS) ? r.sensor : 1;
    if (hasOwn(seen, n)) { return; }
    seen[n] = true;
    var model = r && typeof r.model === 'string' && r.model !== '' ? r.model : 'bmp280';
    var s = normalizeOne({ sensor: n, model: model, enabled: true, status: 'ok', t_c: 0, p_mmhg: 0, h_pct: 0, n_ok: 1, last_ok_s_ago: 0, addr: '' });
    if (s) { out.push(s); }
  });
  return sortList(out);
}

function seriesDefsFromEnv(rows) { return seriesDefs(envSensors(rows)); }

return {
  MAX_SENSORS: MAX_SENSORS, ORDER: ORDER, MODELS: MODELS, LIMITS: LIMITS, STALE_MIN_S: STALE_MIN_S,
  modelInfo: modelInfo, kindsOf: kindsOf, normalizeOne: normalizeOne, parseList: parseList, sortList: sortList,
  statusInfo: statusInfo, ageText: ageText, fmtNum: fmtNum, valueParts: valueParts, label: label, shortLabel: shortLabel,
  seriesName: seriesName, groups: groups, summary: summary, badge: badge, cfgFields: cfgFields, validateField: validateField,
  sensorPatch: sensorPatch, toggleBody: toggleBody, modelBody: modelBody, removeBody: removeBody, addrChoices: addrChoices,
  addBody: addBody, addrTaken: addrTaken, canAdd: canAdd, removable: removable, scanText: scanText,
  enabledWithData: enabledWithData, seriesDefs: seriesDefs, envSensors: envSensors, seriesDefsFromEnv: seriesDefsFromEnv
};
});
