(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(require('./sensors-list.js')); } else { root.AFES = factory(root.AFSL); } })(this, function (AFSL) { 'use strict';

var BASE = { t: '--s6', p: '--s7', h: '--s8' };
var DASH = { t: [8, 3], p: [3, 2], h: [16, 5] };
var NAME = { t: 'Температура среды', p: 'Давление среды', h: 'Влажность среды' };

function one(d, multi) {
  return {
    id: d.id, envKind: d.kind, sensor: d.sensor, tint: d.tint, cid: 'air_' + d.kind,
    group: d.group, baseName: NAME[d.kind], name: multi ? d.name : NAME[d.kind], unit: d.unit, field: d.field,
    axis: 'R', color: d.tint > 0 ? BASE[d.kind] + '|' + d.tint : BASE[d.kind], dash: DASH[d.kind].slice(), on: false, dec: d.dec
  };
}

function build(envRows) {
  var defs = AFSL.seriesDefsFromEnv(envRows);
  if (!defs.length) { defs = AFSL.seriesDefsFromEnv([{ sensor: 1, model: 'bmp280' }]); }
  var multi = defs.some(function (d) { return d.sensor !== defs[0].sensor; });
  return defs.map(function (d) { return one(d, multi); });
}

function signature(defs) { return defs.map(function (d) { return d.id + ':' + d.name; }).join('|'); }

return { build: build, signature: signature, BASE: BASE };
});
