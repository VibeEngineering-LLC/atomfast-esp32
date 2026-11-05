(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(); } else { root.AFZP = factory(); } })(this, function () { 'use strict';

  var STEP_RAW_MAX = 60;

  function normWidth(w) {
    if (typeof w !== 'number' || !isFinite(w) || w <= 0) return 800;
    if (w < 200) w = 200;
    if (w > 2000) w = 2000;
    return Math.round(w);
  }

  function step(fromTs, toTs, widthPx) {
    if (typeof fromTs !== 'number' || typeof toTs !== 'number') return 0;
    if (!isFinite(fromTs) || !isFinite(toTs) || toTs <= fromTs) return 0;
    var w = normWidth(widthPx);
    return (toTs - fromTs) / w;
  }

  function pick(o) {
    var from = (typeof o.from === 'number' && isFinite(o.from)) ? o.from : 0;
    var to   = (typeof o.to   === 'number' && isFinite(o.to))   ? o.to   : 0;
    var rawRate = (typeof o.rawRate === 'number' && isFinite(o.rawRate) && o.rawRate > 0)
      ? o.rawRate : 0.5;

    var st   = step(from, to, o.widthPx);
    var span = to - from;

    if (!(span > 0)) {
      return { kind: 'agg', pts: 0, from: from, to: to, step: 0 };
    }

    var kind = st < STEP_RAW_MAX ? 'raw' : 'agg';
    var en0 = (o.status && typeof o.status === 'object') ? (o.status.en !== undefined ? o.status.en : o.status.enabled) : null;
    if (kind === 'agg' && en0 && typeof en0 === 'object' && Number(en0.agg) !== 1 && Number(en0.raw) === 1) kind = 'raw'; /* b25 F09 */

    var status = (o.status && typeof o.status === 'object') ? o.status : {};
    var windowS = (typeof status.window_s === 'number' && isFinite(status.window_s) && status.window_s > 0)
      ? status.window_s : 60;
    // T-53 (оператор 29.10): шаг >= 60 с на пиксель - ТОЛЬКО агрегаты, даже если их мало (сырые с min/max рисуются густой полосой); пустой агрегат лечится пересчётом

    var expected;
    if (kind === 'raw') {
      expected = Math.ceil(span * rawRate);
    } else {
      expected = Math.ceil(span / windowS);
    }

    var widthNorm = normWidth(o.widthPx);
    var pts = (expected > widthNorm) ? widthNorm : 0;

    return { kind: kind, pts: pts, from: from, to: to, step: st };
  }

  return { STEP_RAW_MAX: STEP_RAW_MAX, pick: pick, step: step };
});
