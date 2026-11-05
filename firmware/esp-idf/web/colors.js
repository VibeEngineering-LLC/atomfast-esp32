(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(); } else { root.AFCL = factory(); } })(this, function () { 'use strict';

var KEY = 'afh.colors.v1';
var IDS = ['rate', 'cps', 'pulses', 'dose', 'temp', 'air_t', 'air_p', 'air_h'];
var BG = { light: '#ffffff', dark: '#11150f' };
var MIN_CONTRAST = 3;
var DEFAULTS = {
  light: { rate: '#0a7467', cps: '#1f5fbf', pulses: '#8a5a00', dose: '#b04a00', temp: '#a8156b', air_t: '#3d7a1c', air_p: '#0b6a99', air_h: '#6b6b00' },
  dark: { rate: '#34d3c0', cps: '#6ea8ff', pulses: '#f0c45a', dose: '#ff9a55', temp: '#ff6ec0', air_t: '#9ad17a', air_p: '#7ed4ff', air_h: '#d0d36a' }
};
var DASHES = {
  solid: [], dash: [6, 4], dots: [2, 3], dashdot: [10, 3, 2, 3], long: [8, 3], short: [3, 2], sparse: [1, 6], wide: [16, 5]
};
var DEFAULT_DASH = { rate: 'solid', cps: 'solid', pulses: 'dash', dose: 'dots', temp: 'dashdot', air_t: 'long', air_p: 'short', air_h: 'wide' };
var DEFAULT_WIDTH = 2;
var PALETTE = ['#0b7f72', '#c4501b', '#2563c9', '#8250df', '#9a6700', '#c2255c', '#5c6b73', '#37474f',
  '#34d3c0', '#ff9b5e', '#6ea8ff', '#c297ff', '#f0c45a', '#ff7a9c', '#9fb0b8', '#1b5e20', '#0e7490', '#5ad1e6'];
var CB = {
  light: ['#0072b2', '#d55e00', '#009e73', '#cc79a7', '#c48700', '#4999c6', '#000000', '#767676'],
  dark: ['#0072b2', '#d55e00', '#009e73', '#cc79a7', '#e69f00', '#56b4e9', '#ffffff', '#a0a0a0']
};
var CB_DASH = ['solid', 'dash', 'dots', 'dashdot', 'wide', 'sparse', 'short', 'long'];

function hasOwn(o, k) { return Object.prototype.hasOwnProperty.call(o, k); }
function emptyState() { return { v: 1, colors: { light: {}, dark: {} }, styles: {}, cb: false }; }
function clone(st) { return JSON.parse(JSON.stringify(st)); }
function themeOk(t) { return t === 'light' || t === 'dark'; }
function idOk(id) { return IDS.indexOf(id) !== -1; }
function hexOk(s) { return typeof s === 'string' && /^#[0-9a-fA-F]{6}$/.test(s); }
function rgb(h) { return [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)]; }

function chan(c) {
  var v = c / 255;
  return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
}

function luminance(h) {
  var c = rgb(h);
  return 0.2126 * chan(c[0]) + 0.7152 * chan(c[1]) + 0.0722 * chan(c[2]);
}

function contrast(a, b) {
  var x = luminance(a);
  var y = luminance(b);
  return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05);
}

function check(hex, theme) {
  if (!hexOk(hex)) { return { ok: false, error: 'Формат: #RRGGBB (шесть шестнадцатеричных цифр)', ratio: 0 }; }
  if (!themeOk(theme)) { return { ok: false, error: 'Неизвестная тема', ratio: 0 }; }
  var r = contrast(hex, BG[theme]);
  if (r < MIN_CONTRAST) { return { ok: false, error: 'Контраст ' + r.toFixed(1) + ':1 к фону графика ниже 3:1', ratio: r }; }
  return { ok: true, error: '', ratio: r };
}

function usable(hex, theme) { return check(hex, theme).ok; }

function bad(st, error) { return { ok: false, error: error, state: st }; }

function setColor(st, theme, id, hex) {
  if (!themeOk(theme) || !idOk(id)) { return bad(st, 'Неизвестный ряд или тема'); }
  var c = check(hex, theme);
  if (!c.ok) { return bad(st, c.error); }
  var n = clone(st);
  n.colors[theme][id] = hex.toLowerCase();
  n.cb = false;
  return { ok: true, error: '', state: n };
}

function resetColor(st, theme, id) {
  var n = clone(st);
  if (themeOk(theme) && idOk(id)) { delete n.colors[theme][id]; }
  if (idOk(id)) { delete n.styles[id]; }
  n.cb = false;
  return n;
}

function resetAll() { return emptyState(); }

function setStyle(st, id, dashKey, width) {
  if (!idOk(id)) { return bad(st, 'Неизвестный ряд'); }
  if (dashKey !== '' && !hasOwn(DASHES, dashKey)) { return bad(st, 'Неизвестный стиль линии'); }
  if (width !== 0 && !(typeof width === 'number' && width >= 1 && width <= 4 && Math.floor(width) === width)) { return bad(st, 'Толщина: от 1 до 4 px'); }
  var n = clone(st);
  var s = {};
  if (dashKey !== '') { s.dash = dashKey; }
  if (width !== 0) { s.width = width; }
  if (s.dash === undefined && s.width === undefined) { delete n.styles[id]; } else { n.styles[id] = s; }
  n.cb = false;
  return { ok: true, error: '', state: n };
}

function effectiveColor(st, theme, id) {
  var m = st.colors[theme];
  return m && hasOwn(m, id) ? m[id] : DEFAULTS[theme][id];
}

function effectiveStyle(st, id) {
  var s = hasOwn(st.styles, id) ? st.styles[id] : {};
  var key = s.dash !== undefined ? s.dash : DEFAULT_DASH[id];
  return { dashKey: key, dash: DASHES[key].slice(), width: s.width !== undefined ? s.width : DEFAULT_WIDTH };
}

function parse(text) {
  var st = emptyState();
  var o = null;
  try { o = JSON.parse(text); } catch (e) { return st; }
  if (!o || typeof o !== 'object' || o.v !== 1) { return st; }
  ['light', 'dark'].forEach(function (t) {
    var m = o.colors && typeof o.colors === 'object' ? o.colors[t] : null;
    if (!m || typeof m !== 'object') { return; }
    IDS.forEach(function (id) {
      if (hasOwn(m, id) && hexOk(m[id])) { st.colors[t][id] = m[id].toLowerCase(); }
    });
  });
  var ss = o.styles && typeof o.styles === 'object' ? o.styles : {};
  IDS.forEach(function (id) {
    if (!hasOwn(ss, id) || !ss[id] || typeof ss[id] !== 'object') { return; }
    var s = {};
    if (typeof ss[id].dash === 'string' && hasOwn(DASHES, ss[id].dash)) { s.dash = ss[id].dash; }
    var w = ss[id].width;
    if (typeof w === 'number' && w >= 1 && w <= 4 && Math.floor(w) === w) { s.width = w; }
    if (s.dash !== undefined || s.width !== undefined) { st.styles[id] = s; }
  });
  st.cb = o.cb === true;
  return st;
}

function serialize(st) { return JSON.stringify(st); }

function varName(id) { return '--s' + (IDS.indexOf(id) + 1); }

function block(st, theme) {
  var parts = [];
  IDS.forEach(function (id) {
    if (hasOwn(st.colors[theme], id)) { parts.push(varName(id) + ':' + st.colors[theme][id] + ';'); }
  });
  return parts.join('');
}

function cssText(st) {
  var l = block(st, 'light');
  var d = block(st, 'dark');
  var out = '';
  if (l) { out += ':root{' + l + '}'; }
  if (d) {
    out += ':root[data-theme="dark"]{' + d + '}';
    out += '@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){' + d + '}}';
  }
  return out;
}

function colorblind() {
  var st = emptyState();
  IDS.forEach(function (id, i) {
    st.colors.light[id] = CB.light[i];
    st.colors.dark[id] = CB.dark[i];
    st.styles[id] = { dash: CB_DASH[i] };
  });
  st.cb = true;
  return st;
}

function tint(hex, k, scheme) {
  if (!hexOk(hex) || !(k > 0)) { return hex; }
  var t = 1 - Math.pow(0.78, Math.min(7, Math.floor(k)));
  var target = scheme === 'dark' ? 255 : 0;
  var c = rgb(hex).map(function (v) { return Math.round(v + (target - v) * t); });
  return '#' + c.map(function (v) { return (v < 16 ? '0' : '') + v.toString(16); }).join('');
}

var TINT_DASH = [[6, 3], [2, 3], [10, 3, 2, 3], [1, 5], [14, 4], [4, 2, 1, 2], [8, 8]];
function tintDash(dash, k) {
  if (!(k > 0)) { return dash; }
  return TINT_DASH[(Math.floor(k) - 1) % TINT_DASH.length].slice();
}

function lin3(h) { return rgb(h).map(chan); }

function toSrgb(v) {
  v = Math.max(0, Math.min(1, v));
  return v <= 0.0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1 / 2.4) - 0.055;
}

var CVD = {
  deu: [[0.367322, 0.860646, -0.227968], [0.280085, 0.672501, 0.047413], [-0.01182, 0.04294, 0.968881]],
  pro: [[0.152286, 1.052583, -0.204868], [0.114503, 0.786281, 0.099216], [-0.003882, -0.048116, 1.051998]]
};

function lab(c) {
  var x = (0.4124 * c[0] + 0.3576 * c[1] + 0.1805 * c[2]) / 0.95047;
  var y = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
  var z = (0.0193 * c[0] + 0.1192 * c[1] + 0.9505 * c[2]) / 1.08883;
  var f = function (t) { return t > 0.008856 ? Math.cbrt(t) : 7.787 * t + 16 / 116; };
  return [116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))];
}

function labOf(h, mode) {
  var c = lin3(h);
  if (mode === 'deu' || mode === 'pro') {
    var m = CVD[mode];
    c = m.map(function (r) { return r[0] * c[0] + r[1] * c[1] + r[2] * c[2]; });
    c = c.map(function (v) { return Math.pow(toSrgb(v), 2.2); });
  }
  return lab(c.map(function (v) { return Math.max(0, Math.min(1, v)); }));
}

function deltaE(a, b, mode) {
  var x = labOf(a, mode);
  var y = labOf(b, mode);
  return Math.sqrt(Math.pow(x[0] - y[0], 2) + Math.pow(x[1] - y[1], 2) + Math.pow(x[2] - y[2], 2));
}

function minPairDelta(list, mode) {
  var m = Infinity;
  for (var i = 0; i < list.length; i++) {
    for (var j = i + 1; j < list.length; j++) { m = Math.min(m, deltaE(list[i], list[j], mode)); }
  }
  return m;
}

return {
  KEY: KEY, IDS: IDS, BG: BG, MIN_CONTRAST: MIN_CONTRAST, DEFAULTS: DEFAULTS, DASHES: DASHES, DEFAULT_DASH: DEFAULT_DASH,
  DEFAULT_WIDTH: DEFAULT_WIDTH, PALETTE: PALETTE, CB: CB, CB_DASH: CB_DASH,
  emptyState: emptyState, hexOk: hexOk, luminance: luminance, contrast: contrast, check: check, usable: usable,
  setColor: setColor, resetColor: resetColor, resetAll: resetAll, setStyle: setStyle,
  effectiveColor: effectiveColor, effectiveStyle: effectiveStyle, parse: parse, serialize: serialize,
  varName: varName, tint: tint, tintDash: tintDash, cssText: cssText, colorblind: colorblind, deltaE: deltaE, minPairDelta: minPairDelta
};
});
