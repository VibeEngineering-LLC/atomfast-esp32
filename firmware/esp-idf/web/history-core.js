(function (root, factory) { if (typeof module === 'object' && module.exports) module.exports = factory(); else root.AFH = factory(); })(this, function () {
  function parseCsv(text) {
    var lines = String(text).split(/\r\n|\n|\r/);
    var header = null, data = [];
    for (var i = 0; i < lines.length; i++) {
      var l = lines[i];
      if (l === '') continue;
      if (header === null) { header = l; } else { data.push(l); }
    }
    if (header === null) return { rows: [], bad: 0 };
    var cols = header.split(',');
    if (cols.indexOf('ts') === -1) return { rows: [], bad: data.length };
    var rows = [], bad = 0;
    for (var j = 0; j < data.length; j++) {
      var parts = data[j].split(',');
      if (parts.length !== cols.length) { bad++; continue; }
      var row = {}, ok = true;
      for (var k = 0; k < cols.length; k++) {
        var raw = parts[k];
        var v = raw === '' ? NaN : Number(raw);
        if (!isFinite(v)) { ok = false; break; }
        row[cols[k]] = v;
      }
      if (ok) rows.push(row); else bad++;
    }
    return { rows: rows, bad: bad };
  }
  function hasTime(ts) { return isFinite(ts) && ts >= 1700000000; }
  function splitKnown(rows) {
    var known = [], unknown = 0;
    for (var i = 0; i < rows.length; i++) { if (hasTime(rows[i].ts)) known.push(rows[i]); else unknown++; }
    return { known: known, unknown: unknown };
  }
  function windowRows(rows, windowSec) {
    if (!rows.length) return [];
    if (!(windowSec > 0) || !isFinite(windowSec)) return rows;
    var last = rows[rows.length - 1].ts, cut = last - windowSec, out = [];
    for (var i = 0; i < rows.length; i++) if (rows[i].ts >= cut) out.push(rows[i]);
    return out;
  }
  function downsample(rows, field, maxPoints) {
    var n = rows.length;
    if (n <= maxPoints) {
      var all = [];
      for (var i = 0; i < n; i++) all.push({ x: rows[i].ts, y: rows[i][field] });
      return all;
    }
    var B = Math.floor(maxPoints / 2);
    if (B < 1) B = 1;
    var out = [];
    for (var b = 0; b < B; b++) {
      var s = Math.floor(b * n / B), e = Math.floor((b + 1) * n / B);
      if (e <= s) continue;
      var mi = s, ma = s;
      for (var k = s + 1; k < e; k++) {
        if (rows[k][field] < rows[mi][field]) mi = k;
        if (rows[k][field] > rows[ma][field]) ma = k;
      }
      if (mi === ma) out.push({ x: rows[mi].ts, y: rows[mi][field] });
      else if (mi < ma) { out.push({ x: rows[mi].ts, y: rows[mi][field] }); out.push({ x: rows[ma].ts, y: rows[ma][field] }); }
      else { out.push({ x: rows[ma].ts, y: rows[ma][field] }); out.push({ x: rows[mi].ts, y: rows[mi][field] }); }
    }
    return out;
  }
  function niceTicks(min, max, count) {
    if (min === max) { var w = min === 0 ? 1 : Math.abs(min) * 0.05; min -= w; max += w; }
    var range = max - min;
    var raw = range / (count > 0 ? count : 1);
    var mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10));
    var norm = raw / mag, step;
    if (norm <= 1) step = mag; else if (norm <= 2) step = 2 * mag; else if (norm <= 5) step = 5 * mag; else step = 10 * mag;
    var start = Math.floor(min / step) * step, out = [];
    if (!(step > 0) || start + step === start) return [min, max]; // WEB-02: шаг ниже ULP — цикл не пошёл бы
    for (var v = start; ; v += step) { out.push(Number(v.toPrecision(12))); if (v >= max - step * 1e-9) break; }
    return out;
  }
  function timeTicks(t0, t1, count, offMin) {
    if (t1 <= t0) return [];
    var steps = [1,2,5,10,15,30,60,120,300,600,900,1800,3600,7200,10800,21600,43200,86400];
    var step = steps[steps.length - 1];
    for (var i = 0; i < steps.length; i++) { if ((t1 - t0) / steps[i] <= count) { step = steps[i]; break; } }
    var off = offMin * 60;
    var l0 = t0 - off, l1 = t1 - off;
    var start = Math.ceil(l0 / step) * step, out = [];
    for (var v = start; v <= l1; v += step) out.push(v + off);
    return out;
  }
  function pad(n) { return (n < 10 ? '0' : '') + n; }
  function fmtTime(ts, mode, offMin) {
    var d = new Date((ts - offMin * 60) * 1000);
    var H = d.getUTCHours(), M = d.getUTCMinutes(), S = d.getUTCSeconds(), D = d.getUTCDate(), Mo = d.getUTCMonth() + 1, Y = d.getUTCFullYear();
    if (mode === 's') return pad(H) + ':' + pad(M) + ':' + pad(S);
    if (mode === 'm') return pad(H) + ':' + pad(M);
    if (mode === 'd') return pad(D) + '.' + pad(Mo) + ' ' + pad(H) + ':' + pad(M);
    return pad(D) + '.' + pad(Mo) + '.' + Y + ' ' + pad(H) + ':' + pad(M) + ':' + pad(S);
  }
  function tickMode(spanSec) { return spanSec < 600 ? 's' : (spanSec < 86400 ? 'm' : 'd'); }
  function fmtDuration(sec) {
    if (!isFinite(sec)) return '—';
    sec = Math.floor(sec);
    if (sec < 60) return sec + ' с';
    if (sec < 3600) return Math.floor(sec / 60) + ' мин';
    if (sec < 86400) { var h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60); return h + ' ч' + (m ? ' ' + m + ' мин' : ''); }
    var d = Math.floor(sec / 86400), h2 = Math.floor((sec % 86400) / 3600);
    return d + ' д' + (h2 ? ' ' + h2 + ' ч' : '');
  }
  function fmtBytes(n) {
    if (!isFinite(n)) return '—';
    if (n < 1024) return Math.floor(n) + ' Б';
    if (n < 1048576) return (n / 1024).toFixed(1) + ' КБ';
    return (n / 1048576).toFixed(1) + ' МБ';
  }
  function reserveSec(count, limit, ratePerSec) {
    if (limit <= count) return 0;
    if (!(ratePerSec > 0) || !isFinite(ratePerSec)) return Infinity;
    return (limit - count) / ratePerSec;
  }
  function ratePerSec(rows) {
    var first = null, last = null, n = 0;
    for (var i = 0; i < rows.length; i++) {
      if (hasTime(rows[i].ts)) { if (first === null) first = rows[i].ts; last = rows[i].ts; n++; }
    }
    if (n < 2 || last - first <= 0) return 0;
    return (n - 1) / (last - first);
  }
  function appendRows(existing, incoming, maxRows) {
    var a = existing.concat(incoming);
    if (a.length > maxRows) a = a.slice(a.length - maxRows);
    return a;
  }
  function sinceFor(statusRing, windowSec, ratePerSec, maxRows) {
    var want;
    if (windowSec > 0) {
      if (ratePerSec > 0) want = Math.ceil(windowSec * ratePerSec * 1.2) + 5;
      else want = maxRows;
    } else want = Infinity;
    if (want > maxRows) want = maxRows;
    var r = Math.max(statusRing.oldest, statusRing.seq - want);
    return r < 0 ? 0 : r;
  }
  function nextSince(sinceUsed, oldest, received) {
    var m = sinceUsed > oldest ? sinceUsed : oldest;
    return m + received;
  }
  function summarize(rows, field) {
    var n = rows.length;
    if (n === 0) return { n: 0, min: null, max: null, mean: null, last: null };
    var min = Infinity, max = -Infinity, sum = 0, cnt = 0, last = null;
    for (var i = 0; i < n; i++) {
      var v = rows[i][field];
      if (typeof v !== 'number' || !isFinite(v)) continue; // e10: строки без значения (воздух есть не везде) не искажают итог
      if (v < min) min = v;
      if (v > max) max = v;
      sum += v;
      cnt++;
      last = v;
    }
    if (cnt === 0) return { n: 0, min: null, max: null, mean: null, last: null };
    return { n: cnt, min: min, max: max, mean: sum / cnt, last: last };
  }
  // Источник ряда по умолчанию (e16i П.5). status = /api/history/status, saved = сохранённое окно (с) либо { windowSec }.
  // Есть enabled (новая прошивка): 'agg' только при enabled.agg=1 и agg.count >= 2, иначе 'raw'; saved.kind при этом игнорируется.
  // Нет enabled (старая прошивка): прежнее defaultKindForWindow: окно 3600 с -> 'raw', иначе 'agg'.
  function pickKind(status, saved) {
    var en = status && typeof status === 'object' ? (status.en !== undefined ? status.en : status.enabled) : undefined;
    if (en && typeof en === 'object') {
      var cnt = status.agg && typeof status.agg === 'object' ? status.agg.count : undefined;
      return (Number(en.agg) === 1 && typeof cnt === 'number' && cnt >= 2) ? 'agg' : 'raw';
    }
    var w = saved && typeof saved === 'object' ? saved.windowSec : saved;
    return w === 3600 ? 'raw' : 'agg';
  }
  return {
    pickKind: pickKind, parseCsv: parseCsv, hasTime: hasTime, splitKnown: splitKnown, windowRows: windowRows,
    downsample: downsample, niceTicks: niceTicks, timeTicks: timeTicks, fmtTime: fmtTime,
    tickMode: tickMode, fmtDuration: fmtDuration, fmtBytes: fmtBytes, reserveSec: reserveSec,
    ratePerSec: ratePerSec, appendRows: appendRows, sinceFor: sinceFor, nextSince: nextSince, summarize: summarize
  };
});
