(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(); } else { root.AFUN = factory(); } })(this, function () { 'use strict';

var ND = 'н/д';
var MAX_SYS_PCT = 50;

function isNum(x) { return typeof x === 'number' && isFinite(x); }

function roundTo(v, dec) {
  var str = String(v);
  var r;
  if (str.indexOf('e') >= 0) { var f = Math.pow(10, dec); r = Math.round(v * f) / f; }
  else { r = Number(Math.round(Number(str + 'e' + dec)) + 'e' + (-dec)); }
  return r === 0 ? 0 : r;
}

function decimalsFor(err) {
  if (!isNum(err) || !(err > 0)) { return null; }
  var exp = Math.floor(Math.log10(err));
  var first = Math.floor(err / Math.pow(10, exp) + 1e-12);
  var sig = (first === 1 || first === 2) ? 2 : 1;
  return Math.max(-12, Math.min(12, sig - 1 - exp));
}

function fixed(v, dec) {
  var r = roundTo(v, dec);
  if (dec >= 0) { return r.toFixed(dec); }
  return String(r);
}

// Коэффициент охвата k (расширенная неопределённость U = k*sigma): нет/не число/не положительное -> 1 (поведение прежнее)
function kOf(k) { return isNum(k) && k > 0 ? k : 1; }

function format(value, err, opts) {
  var o = opts || {};
  var kf = kOf(o.k);
  if (kf !== 1 && isNum(err)) { err = err * kf; }
  if (!isNum(value) || (err !== undefined && err !== null && !isNum(err))) { return { text: ND, value: ND, err: ND, decimals: null, weak: false }; }
  if (err === undefined || err === null) { return { text: fixed(value, o.fallbackDec === undefined ? 2 : o.fallbackDec) + ' ± ' + ND, value: fixed(value, 2), err: ND, decimals: null, weak: false }; }
  if (err < 0) { return { text: ND, value: ND, err: ND, decimals: null, weak: false }; }
  if (err === 0) { var d0 = o.fallbackDec === undefined ? 2 : o.fallbackDec; return { text: fixed(value, d0) + ' ± 0', value: fixed(value, d0), err: '0', decimals: d0, weak: false }; }
  if (Math.abs(value) >= 1e15 || err >= 1e15) {
    var ev = value.toExponential(2);
    var ee = err.toExponential(1);
    return { text: ev + ' ± ' + ee, value: ev, err: ee, decimals: null, weak: err >= Math.abs(value) };
  }
  var dec = decimalsFor(err);
  var es = fixed(err, dec);
  var vs = fixed(value, dec);
  return { text: vs + ' ± ' + es, value: vs, err: es, decimals: dec, weak: err >= Math.abs(value) };
}

// Пуассон: N импульсов за t секунд -> CPS = N/t, sigma = sqrt(N)/t; при N = 0 берётся верхняя граница 1 импульс
function countStat(n, t) {
  if (!isNum(n) || n < 0 || !isNum(t) || !(t > 0)) { return { value: NaN, sigma: NaN, zero: false }; }
  return { value: n / t, sigma: (n > 0 ? Math.sqrt(n) : 1) / t, zero: n === 0 };
}

function pulsesStat(n) {
  if (!isNum(n) || n < 0) { return { value: NaN, sigma: NaN, zero: false }; }
  return { value: n, sigma: n > 0 ? Math.sqrt(n) : 1, zero: n === 0 };
}

// Среднее CPS за окно: N = cps_avg * окно
function avgCpsStat(cpsAvg, windowS) {
  if (!isNum(cpsAvg) || cpsAvg < 0 || !isNum(windowS) || !(windowS > 0)) { return { value: NaN, sigma: NaN, zero: false }; }
  var n = cpsAvg * windowS;
  return { value: cpsAvg, sigma: (n > 0 ? Math.sqrt(n) : 1) / windowS, zero: n === 0, n: n };
}

// Мощность дозы: относительная погрешность наследуется от счёта (линейная связь), sigma = rate / sqrt(N)
function rateStat(rate, n) {
  if (!isNum(rate) || rate < 0 || !isNum(n) || !(n > 0)) { return { value: rate, sigma: NaN }; }
  return { value: rate, sigma: rate / Math.sqrt(n) };
}

// Накопленная доза: только если известно полное число импульсов
function doseStat(dose, nTotal) {
  if (!isNum(dose) || dose < 0 || !isNum(nTotal) || !(nTotal > 0)) { return { value: dose, sigma: NaN }; }
  return { value: dose, sigma: dose / Math.sqrt(nTotal) };
}

function sysPctOf(x) {
  var n = typeof x === 'string' ? Number(x.replace(',', '.')) : x;
  if (typeof x === 'string' && x.trim() === '') { return null; }
  if (!isNum(n) || n < 0 || n > MAX_SYS_PCT) { return null; }
  return n;
}

// Сумма статистической и систематической частей: квадратично
// k (необязательный, по умолчанию 1): U = k*sqrt(stat^2 + sys^2); stat и sys возвращаются уже умноженными на k
function total(value, sigmaStat, sysPct, k) {
  var sp = sysPctOf(sysPct);
  var kf = kOf(k);
  if (!isNum(sigmaStat) || sigmaStat < 0) { return { sigma: NaN, stat: sigmaStat, sys: 0 }; }
  var sys = sp === null || !isNum(value) ? 0 : Math.abs(value) * sp / 100;
  if (kf === 1) { return { sigma: Math.sqrt(sigmaStat * sigmaStat + sys * sys), stat: sigmaStat, sys: sys }; }
  return { sigma: kf * Math.sqrt(sigmaStat * sigmaStat + sys * sys), stat: kf * sigmaStat, sys: kf * sys };
}

// Среднее n независимых значений: sigma / sqrt(n)
function mean(sigma, n) {
  if (!isNum(sigma) || sigma < 0 || !isNum(n) || !(n >= 1)) { return NaN; }
  return sigma / Math.sqrt(n);
}

// Шаг квантования (младший разряд АЦП) из формул преобразования e15-report.md §3: SHT3x T = -45 + 175*S/(2^16-1), RH = 100*S/(2^16-1);
// AHT2x RH = S/2^20*100, T = S/2^20*200 - 50. Для BMP280/BME280 формула в отчёте не содержит шага: н/д.
var LSB = {
  sht3x: { t: 175 / 65535, h: 100 / 65535 },
  aht2x: { t: 200 / 1048576, h: 100 / 1048576 }
};

function envLsb(model, kind) {
  if (!Object.prototype.hasOwnProperty.call(LSB, model)) { return null; }
  var v = LSB[model][kind];
  return isNum(v) ? v : null;
}

// Паспортные погрешности (Bosch BME280 DS002, Sensirion SHT3x-DIS, ASAIR AHT20): числа в e15-report.md §3 не приведены, поэтому н/д
function passport(model, kind) {
  return null;
}

// Живые величины из /api/sensors.live: значение, статистическая часть (Пуассон), систематическая и итог (корень из суммы квадратов)
// Мощность дозы на плате не считается: её присылает прибор (float32 из пакета), поэтому относительная погрешность берётся равной
// счётной (допущение о пропорциональности мощности дозы счёту; сглаживание в приборе не учтено).
function part(stat, sysPct, k) {
  var t = total(stat.value, stat.sigma, sysPct, k);
  return { value: stat.value, stat: t.stat, sys: t.sys, sigma: t.sigma, text: format(stat.value, t.sigma).text, zero: !!stat.zero };
}

function liveErrors(live, aggS, sysPct, k) {
  var out = {};
  if (!live || typeof live !== 'object') { return out; }
  var n2 = isNum(live.pulses_2s) ? live.pulses_2s : NaN;
  if (live.has_rec === true && isNum(live.cps)) { out.cps = part(countStat(live.cps * 2, 2), sysPct, k); }
  if (live.has_rec === true && isNum(n2)) { out.pulses = part(pulsesStat(n2), sysPct, k); }
  if (live.has_rec === true && isNum(live.rate_usvh)) { out.rate = part(rateStat(live.rate_usvh, n2), sysPct, k); }
  var a = avgCpsStat(live.avg_cps, aggS);
  if (isNum(live.avg_cps)) { out.avg_cps = part(a, sysPct, k); }
  if (isNum(live.avg_rate_usvh)) { out.avg_rate = part(rateStat(live.avg_rate_usvh, a.n), sysPct, k); }
  if (isNum(live.dose_usv)) { out.dose = part(doseStat(live.dose_usv, undefined), sysPct, k); }
  return out;
}

// Сводка ряда по строкам истории: среднее и последнее с погрешностью. Строка агрегата несёт n отсчётов (вес), сырая — 1.
function seriesStats(id, rows, sysPct, k) {
  if (id !== 'cps' && id !== 'pulses' && id !== 'rate') { return null; }
  var N = 0, S = 0, R = 0, last = null;
  (Array.isArray(rows) ? rows : []).forEach(function (r) {
    if (!r || !isNum(r.pulses_2s)) { return; }
    var w = isNum(r.n) && r.n > 0 ? r.n : 1;
    N += r.pulses_2s * w; S += w;
    if (isNum(r.doserate_usvh)) { R += r.doserate_usvh * w; }
    last = { n: r.pulses_2s * w, w: w, rate: r.doserate_usvh };
  });
  if (!(S > 0) || last === null) { return null; }
  var sigN = N > 0 ? Math.sqrt(N) : 1;
  var m, l;
  if (id === 'cps') { m = { value: N / (2 * S), sigma: sigN / (2 * S) }; l = { value: last.n / (2 * last.w), sigma: (last.n > 0 ? Math.sqrt(last.n) : 1) / (2 * last.w) }; }
  else if (id === 'pulses') { m = { value: N / S, sigma: sigN / S }; l = { value: last.n / last.w, sigma: (last.n > 0 ? Math.sqrt(last.n) : 1) / last.w }; }
  else { m = rateStat(R / S, N); l = rateStat(last.rate, last.n); }
  var mt = total(m.value, m.sigma, sysPct, k);
  var lt = total(l.value, l.sigma, sysPct, k);
  return { mean: { value: m.value, sigma: mt.sigma, text: format(m.value, mt.sigma).text }, last: { value: l.value, sigma: lt.sigma, text: format(l.value, lt.sigma).text } };
}

var UNITS = { t: '°C', p: 'мм рт. ст.', h: '%' };

// Пояснение к значению датчика среды: паспорт, поправка оператора (систематическая, отдельно), шаг АЦП как нижняя граница
function envNote(model, kind, off) {
  var unit = UNITS[kind] || '';
  var out = ['паспортная погрешность: ' + ND];
  if (isNum(off) && off !== 0) { out.push('поправка оператора (систематическая часть, отдельно): ' + (off > 0 ? '+' : '−') + Math.abs(off) + ' ' + unit); }
  var q = envLsb(model, kind);
  out.push(q === null ? 'дискретизация АЦП: ' + ND : 'дискретизация АЦП (нижняя граница): ±' + String(Number((q / 2).toPrecision(2))) + ' ' + unit);
  return out.join('; ');
}

//__FUNCS__

function rateEps(value, epsPct, unit) {
  if (!isNum(value)) {
    return { value: '\u2014', eps: '\u2014', unit: unit, text: '\u2014' };
  }

  if (unit === '\u043C\u043A\u0417\u0432/\u0447') {
    if (Math.abs(value) >= 1000) {
      value = value / 1000;
      unit = '\u043C\u0417\u0432/\u0447';
    } else if (value !== 0 && Math.abs(value) < 0.001) {
      value = value * 1000;
      unit = '\u043D\u0417\u0432/\u0447';
    }
  }

  if (!isNum(epsPct) || epsPct < 0) {
    var v3 = String(Number(value.toPrecision(3)));
    var t = v3 + (unit != null && unit !== '' ? ' ' + unit : '');
    return { value: v3, eps: '', unit: unit, text: t };
  }

  var eps = epsPct.toFixed(1);

  if (value === 0) {
    var t0 = '0 \u00B1 ' + eps + ' %' + (unit != null && unit !== '' ? ' ' + unit : '');
    return { value: '0', eps: eps, unit: unit, text: t0 };
  }

  if (epsPct === 0) {
    var v3b = String(Number(value.toPrecision(3)));
    var t0b = v3b + ' \u00B1 ' + eps + ' %' + (unit != null && unit !== '' ? ' ' + unit : '');
    return { value: v3b, eps: eps, unit: unit, text: t0b };
  }

  var abs = Math.abs(value) * epsPct / 100;
  var dec = decimalsFor(abs);
  var rounded = roundTo(abs, dec);
  var dec2 = decimalsFor(rounded);
  if (dec2 !== null) dec = dec2;

  var valueStr;
  if (Math.abs(value) < 1e-4 || Math.abs(value) >= 1e9) {
    var e = Math.floor(Math.log(Math.abs(value)) / Math.LN10);
    var eabs = Math.floor(Math.log(abs) / Math.LN10);
    var firstDigit = Math.floor(abs / Math.pow(10, eabs));
    var sig = (firstDigit === 1 || firstDigit === 2) ? 2 : 1;
    var md = Math.max(0, e - eabs + (sig - 1));
    valueStr = value.toExponential(md).replace('e+', 'e');
  } else {
    valueStr = fixed(value, dec);
  }

  var text = valueStr + ' \u00B1 ' + eps + ' %' + (unit != null && unit !== '' ? ' ' + unit : '');

  return { value: valueStr, eps: eps, unit: unit, text: text };
}
return {
  ND: ND, MAX_SYS_PCT: MAX_SYS_PCT, isNum: isNum
, kOf: kOf, roundTo: roundTo, decimalsFor: decimalsFor, format: format
, countStat: countStat, pulsesStat: pulsesStat, avgCpsStat: avgCpsStat, rateStat: rateStat, doseStat: doseStat, sysPctOf: sysPctOf, total: total, mean: mean
, envLsb: envLsb, passport: passport, LSB: LSB
, liveErrors: liveErrors, seriesStats: seriesStats
, envNote: envNote
, rateEps: rateEps
//__EXPORTS__
};
});
