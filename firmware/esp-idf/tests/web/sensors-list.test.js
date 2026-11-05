const assert = require('assert');
const fs = require('fs');
const path = require('path');
const S = require(process.env.HL_MODULE || path.join(__dirname, '..', '..', 'web', 'sensors-list.js'));
let passed = 0, total = 0;
const queue = [];
function check(name, fn) { queue.push([name, fn]); }
const fx = (n) => JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'sensors-' + n + '.json'), 'utf8'));
const L = (n) => S.parseList(fx(n));
const one = (o) => S.normalizeOne(Object.assign({ sensor: 1, model: 'bmp280', addr: '0x76', enabled: true, status: 'ok', err: '', t_c: 20, p_mmhg: 740, h_pct: null, period_s: 30, n_ok: 10, n_err: 0, last_ok_s_ago: 5 }, o || {}));

check('nested_array_from_board_e16f', () => {
  // реальный ответ платы e16f: "sensors":[[{...}]] (двойные скобки из af_env_drv.c); страница обязана видеть датчик
  const j = { agg_s: 5, sensors: [[{ sensor: 1, model: 'bmp280', addr: '0x76', enabled: true, status: 'ok', err: '', caps: 3, temp: true, press: true, hum: false, t_c: 27.5, h_pct: null, p_mmhg: 760.2, period_s: 300, n_ok: 262, n_err: 0, last_ok_s_ago: 58 }]] };
  const r = S.parseList(j);
  assert.strictEqual(r.list.length, 1);
  assert.strictEqual(r.list[0].enabled, true);
  assert.strictEqual(r.list[0].model, 'bmp280');
  assert.strictEqual(r.list[0].t_c, 27.5);
  assert.strictEqual(S.summary(r.list).enabled, 1);
});

check('one_bmp280_shape', () => {
    const r = L('one');
    assert.strictEqual(r.legacy, false);
    assert.strictEqual(r.list.length, 1);
    const s = r.list[0];
    assert.strictEqual(s.sensor, 1);
    assert.strictEqual(s.model, 'bmp280');
    assert.strictEqual(s.name, 'BMP280');
    assert.strictEqual(s.kinds, 'T+P');
    assert.strictEqual(s.addr, '0x76');
    assert.strictEqual(s.t_c, 23.41);
    assert.strictEqual(s.p_mmhg, 745.23);
    assert.strictEqual(s.h_pct, null);
    assert.deepStrictEqual(s.caps, { t: true, p: true, h: false });
    assert.strictEqual(r.aggS, 10);
    assert.strictEqual(r.maxSensors, 8);
});

check('three_models_sorted_by_model_then_number', () => {
    const r = L('three');
    assert.deepStrictEqual(r.list.map(s => s.model + '#' + s.sensor), ['bmp280#1', 'sht3x#2', 'aht2x#3']);
    assert.deepStrictEqual(r.list.map(s => s.kinds), ['T+P', 'T+RH', 'T+RH']);
    assert.strictEqual(r.list[1].h_pct, 41.35);
    assert.strictEqual(r.list[2].t_off, -0.5);
});

check('two_same_distinguishable_by_number', () => {
    const r = L('two_same');
    assert.deepStrictEqual(r.list.map(s => s.sensor), [1, 2]);
    assert.deepStrictEqual(r.list.map(s => s.addr), ['0x76', '0x77']);
    assert.notStrictEqual(S.label(r.list[0]), S.label(r.list[1]));
    assert.strictEqual(S.label(r.list[1]), 'BMP280 (0x77), датчик 2');
    assert.strictEqual(r.list[1].p_off, 0.5);
});

check('all_off_states', () => {
    const r = L('all_off');
    assert.ok(r.list.every(s => S.statusInfo(s).key === 'off' && S.statusInfo(s).text === 'выключен'));
    assert.strictEqual(S.summary(r.list).text, 'Датчиков: 3 (включено 0)');
    assert.strictEqual(S.summary(r.list).hasErr, false);
    assert.strictEqual(S.badge(r.list).text, '0');
});

check('error_states', () => {
    const r = L('error');
    const by = {};
    r.list.forEach(s => { by[s.model] = S.statusInfo(s); });
    assert.deepStrictEqual(by.bmp280, { key: 'ok', text: 'включён', cls: 'ok' });
    assert.deepStrictEqual(by.sht3x, { key: 'error', text: 'ошибка: i2c_nack', cls: 'bad' });
    assert.deepStrictEqual(by.aht2x, { key: 'silent', text: 'не отвечает', cls: 'warn' });
    assert.strictEqual(S.summary(r.list).hasErr, true);
    assert.deepStrictEqual(S.badge(r.list), { show: true, text: '3', cls: 'warn' });
});

check('eight_sensors', () => {
    const r = L('eight');
    assert.strictEqual(r.list.length, 8);
    assert.deepStrictEqual(r.list.map(s => s.sensor).sort(), [1, 2, 3, 4, 5, 6, 7, 8]);
    assert.deepStrictEqual(r.list.map(s => s.model), ['bmp280', 'bmp280', 'bme280', 'bme280', 'sht3x', 'sht3x', 'aht2x', 'aht2x']);
    assert.strictEqual(S.summary(r.list).text, 'Датчиков: 8 (включено 8)');
});

check('none_state', () => {
    const r = L('none');
    assert.deepStrictEqual(r.list, []);
    assert.strictEqual(S.summary(r.list).text, 'Датчиков: 0 (включено 0)');
    assert.strictEqual(S.badge(r.list).show, false);
    assert.deepStrictEqual(S.groups(r.list), []);
});

check('legacy_contract', () => {
    const r = S.parseList({ sensor: { status: 'ok', model: 'bmp280', addr: '0x76', temp: true, press: true, hum: false, t_c: 20, p_mmhg: 740, last_ok_s_ago: 3, n_ok: 5 }, cfg: { enabled: 1, t_off: 0.5, p_off: 0, agg_s: 12, period_s: 30 } });
    assert.strictEqual(r.legacy, true);
    assert.strictEqual(r.list.length, 1);
    assert.strictEqual(r.list[0].sensor, 1);
    assert.strictEqual(r.list[0].status, 'ok');
    assert.strictEqual(r.list[0].t_off, 0.5);
    assert.strictEqual(r.aggS, 12);
    assert.deepStrictEqual(S.parseList({ sensor: { status: 'no_sensor' } }).list, []);
    assert.strictEqual(S.parseList({ sensor: { status: 'disabled', model: 'bmp280', addr: '0x76' }, cfg: { enabled: 0 } }).list[0].status, 'off');
    const e = S.parseList({ sensor: { status: 'read_error', model: 'bmp280', addr: '0x76', n_ok: 3 }, cfg: { enabled: 1 } }).list[0];
    assert.strictEqual(e.status, 'err');
    assert.strictEqual(e.err, 'read_error');
});

check('parse_garbage', () => {
    for (const v of [null, undefined, 5, 'x', [], {}]) {
        const r = S.parseList(v);
        assert.deepStrictEqual(r.list, [], JSON.stringify(v));
    }
    assert.strictEqual(S.parseList({ sensors: 'x' }).list.length, 0);
    assert.strictEqual(S.parseList({ sensors: [] }).legacy, false);
});

check('normalize_rejects_bad_numbers', () => {
    for (const n of [0, 9, 1.5, '1', null, -1, NaN]) assert.strictEqual(S.normalizeOne({ sensor: n, model: 'bmp280' }), null, String(n));
    assert.strictEqual(S.normalizeOne(null), null);
    assert.strictEqual(S.normalizeOne('x'), null);
});

check('normalize_unknown_model', () => {
    const s = S.normalizeOne({ sensor: 3, model: 'xyz', status: 'ok', enabled: true });
    assert.strictEqual(s.name, 'xyz');
    assert.strictEqual(s.kinds, '');
    assert.deepStrictEqual(s.caps, { t: false, p: false, h: false });
    assert.strictEqual(S.normalizeOne({ sensor: 3, model: 'constructor' }).name, 'constructor');
    assert.strictEqual(S.normalizeOne({ sensor: 3 }).name, '?');
});

check('caps_from_booleans_override_model', () => {
    const s = one({ model: 'bmp280', temp: true, press: false, hum: true });
    assert.deepStrictEqual(s.caps, { t: true, p: false, h: true });
    assert.strictEqual(s.kinds, 'T+RH');
});

check('temp_flag_false_overrides_model', () => {
    const s = one({ model: 'sht3x', temp: false, press: false, hum: true });
    assert.deepStrictEqual(s.caps, { t: false, p: false, h: true });
});

check('normalize_status_addr_values', () => {
    assert.strictEqual(one({ status: 'weird' }).status, 'err');
    assert.strictEqual(one({ addr: '0X76' }).addr, '');
    assert.strictEqual(one({ addr: '0x7A' }).addr, '0x7a');
    assert.strictEqual(one({ addr: '76' }).addr, '');
    assert.strictEqual(one({ model: 'bmp280', h_pct: 40 }).h_pct, null);
    assert.strictEqual(one({ model: 'sht3x', h_pct: 40, t_c: 21 }).h_pct, 40);
    assert.strictEqual(one({ model: 'sht3x', p_mmhg: 700 }).p_mmhg, null);
    assert.strictEqual(one({ enabled: 'yes' }).enabled, false);
    assert.strictEqual(one({ last_ok_s_ago: 'x' }).last_ok_s_ago, -1);
});

check('dedupe_by_number_and_nested_element', () => {
    const r = S.parseList({ sensors: [{ sensor: 2, model: 'sht3x' }, { sensor: 2, model: 'aht2x' }, { sensor: { sensor: 1, model: 'bmp280' } }] });
    assert.deepStrictEqual(r.list.map(s => s.model + '#' + s.sensor), ['bmp280#1', 'sht3x#2']);
});

check('sort_unknown_models_last', () => {
    const l = S.sortList([one({ sensor: 1, model: 'zzz' }), one({ sensor: 2, model: 'aht2x' }), one({ sensor: 3, model: 'bmp280' }), one({ sensor: 4, model: 'bmp280' })]);
    assert.deepStrictEqual(l.map(s => s.sensor), [3, 4, 2, 1]);
});

check('status_off_by_enabled', () => {
    assert.strictEqual(S.statusInfo(one({ enabled: false, status: 'ok' })).key, 'off');
    assert.strictEqual(S.statusInfo(one({ enabled: true, status: 'off' })).key, 'off');
});

check('status_stale_boundary', () => {
    assert.strictEqual(S.statusInfo(one({ period_s: 30, last_ok_s_ago: 90 })).key, 'ok');
    assert.strictEqual(S.statusInfo(one({ period_s: 30, last_ok_s_ago: 91 })).key, 'silent');
    assert.strictEqual(S.statusInfo(one({ period_s: 5, last_ok_s_ago: 30 })).key, 'ok');
    assert.strictEqual(S.statusInfo(one({ period_s: 5, last_ok_s_ago: 31 })).key, 'silent');
    assert.strictEqual(S.statusInfo(one({ last_ok_s_ago: -1 })).key, 'ok');
});

check('status_error_texts', () => {
    assert.strictEqual(S.statusInfo(one({ status: 'err', err: 'error', n_ok: 4 })).text, 'ошибка');
    assert.strictEqual(S.statusInfo(one({ status: 'err', err: '', n_ok: 4 })).text, 'ошибка');
    assert.strictEqual(S.statusInfo(one({ status: 'err', err: 'init', n_ok: 0 })).key, 'silent');
});

check('age_text', () => {
    const a = (n) => S.ageText({ last_ok_s_ago: n });
    assert.strictEqual(a(-1), 'нет измерений');
    assert.strictEqual(a(0), '0 с назад');
    assert.strictEqual(a(59), '59 с назад');
    assert.strictEqual(a(60), '1 мин назад');
    assert.strictEqual(a(3599), '59 мин назад');
    assert.strictEqual(a(3600), '1 ч назад');
    assert.strictEqual(a(7300), '2 ч назад');
});

check('fmt_num', () => {
    assert.strictEqual(S.fmtNum(null, 1), 'н/д');
    assert.strictEqual(S.fmtNum(NaN, 1), 'н/д');
    assert.strictEqual(S.fmtNum(undefined, 1), 'н/д');
    assert.strictEqual(S.fmtNum(-0.04, 1), '0.0');
    assert.strictEqual(S.fmtNum(-1.26, 1), '-1.3');
    assert.strictEqual(S.fmtNum(23.456, 1), '23.5');
});

check('value_parts_by_model', () => {
    const keys = (m) => S.valueParts(one({ model: m, h_pct: 40, t_c: 21, p_mmhg: 740 })).map(p => p.key).join('');
    assert.strictEqual(keys('bmp280'), 'tp');
    assert.strictEqual(keys('bme280'), 'tph');
    assert.strictEqual(keys('sht3x'), 'th');
    assert.strictEqual(keys('aht2x'), 'th');
});

check('value_parts_texts_and_missing', () => {
    const p = S.valueParts(one({ model: 'bme280', t_c: 21.04, p_mmhg: 740.5, h_pct: 45.55 }));
    assert.deepStrictEqual(p.map(x => x.text + ' ' + x.unit), ['21.0 °C', '740.5 мм рт. ст.', '45.5 %'].map((x, i) => i === 2 ? '45.6 %' : x).map((x, i) => i === 2 ? S.fmtNum(45.55, 1) + ' %' : x));
    const e = S.valueParts(one({ model: 'sht3x', status: 'err', n_ok: 0 }));
    assert.deepStrictEqual(e.map(x => x.text), ['н/д', 'н/д']);
});

check('labels', () => {
    const s = one({ model: 'sht3x', sensor: 2, addr: '0x44' });
    assert.strictEqual(S.label(s), 'SHT3x (0x44), датчик 2');
    assert.strictEqual(S.shortLabel(s), '№2 SHT3x');
    assert.strictEqual(S.seriesName('t', s), 'T среды · №2 SHT3x');
    assert.strictEqual(S.seriesName('p', s), 'P среды · №2 SHT3x');
    assert.strictEqual(S.seriesName('h', s), 'RH среды · №2 SHT3x');
    assert.strictEqual(S.label(one({ addr: '' })), 'BMP280, датчик 1');
});

check('groups_by_model', () => {
    const g = S.groups(L('three').list);
    assert.deepStrictEqual(g.map(x => x.model), ['bmp280', 'sht3x', 'aht2x']);
    assert.deepStrictEqual(g.map(x => x.name), ['BMP280', 'SHT3x', 'AHT2x']);
    assert.ok(g.every(x => x.sensors.every(s => s.model === x.model)));
    const two = S.groups(L('two_same').list);
    assert.strictEqual(two.length, 1);
    assert.strictEqual(two[0].count, 2);
});

check('group_enable_flags', () => {
    const l = L('two_same').list;
    assert.deepStrictEqual(S.groups(l).map(g => [g.enabledCount, g.allEnabled, g.anyEnabled]), [[2, true, true]]);
    const mixed = l.map((s, i) => Object.assign({}, s, { enabled: i === 0 }));
    assert.deepStrictEqual(S.groups(mixed).map(g => [g.enabledCount, g.allEnabled, g.anyEnabled]), [[1, false, true]]);
    const off = l.map(s => Object.assign({}, s, { enabled: false }));
    assert.deepStrictEqual(S.groups(off).map(g => [g.enabledCount, g.allEnabled, g.anyEnabled]), [[0, false, false]]);
});

check('summary_and_badge', () => {
    const r = L('three');
    assert.strictEqual(S.summary(r.list).text, 'Датчиков: 3 (включено 3)');
    assert.deepStrictEqual(S.badge(r.list), { show: true, text: '3', cls: 'ok' });
    const off = r.list.map((s, i) => Object.assign({}, s, { enabled: i !== 1, status: i === 1 ? 'off' : s.status }));
    assert.strictEqual(S.summary(off).enabled, 2);
});

check('cfg_fields_by_model', () => {
    const f = (m) => S.cfgFields(one({ model: m })).join(',');
    assert.strictEqual(f('bmp280'), 'enabled,t_off,p_off,period_s');
    assert.strictEqual(f('bme280'), 'enabled,t_off,p_off,h_off,period_s');
    assert.strictEqual(f('sht3x'), 'enabled,t_off,h_off,period_s');
    assert.strictEqual(f('aht2x'), 'enabled,t_off,h_off,period_s');
});

check('validate_field_ranges', () => {
    assert.deepStrictEqual(S.validateField('t_off', '5'), { ok: true, value: 5 });
    assert.deepStrictEqual(S.validateField('t_off', '-5'), { ok: true, value: -5 });
    assert.strictEqual(S.validateField('t_off', '5.1').ok, false);
    assert.strictEqual(S.validateField('p_off', '20').ok, true);
    assert.strictEqual(S.validateField('p_off', '-20.1').ok, false);
    assert.strictEqual(S.validateField('h_off', '10').ok, true);
    assert.strictEqual(S.validateField('h_off', '10.1').ok, false);
    assert.strictEqual(S.validateField('period_s', '5').ok, true);
    assert.strictEqual(S.validateField('period_s', '600').ok, true);
    assert.strictEqual(S.validateField('period_s', '4').ok, false);
    assert.strictEqual(S.validateField('period_s', '601').ok, false);
    assert.strictEqual(S.validateField('period_s', '5.5').ok, false);
    assert.strictEqual(S.validateField('agg_s', '1').ok, true);
    assert.strictEqual(S.validateField('agg_s', '60').ok, true);
    assert.strictEqual(S.validateField('agg_s', '0').ok, false);
    assert.strictEqual(S.validateField('agg_s', '61').ok, false);
});

check('validate_field_parsing', () => {
    assert.deepStrictEqual(S.validateField('t_off', '1,5'), { ok: true, value: 1.5 });
    assert.deepStrictEqual(S.validateField('t_off', ' 0.04 '), { ok: true, value: 0 });
    assert.strictEqual(S.validateField('t_off', '').error, 'введите число');
    assert.strictEqual(S.validateField('t_off', 'abc').error, 'введите число');
    assert.strictEqual(S.validateField('t_off', null).ok, false);
    assert.strictEqual(S.validateField('nope', '1').ok, false);
    assert.strictEqual(S.validateField('constructor', '1').ok, false);
    assert.ok(S.validateField('t_off', '9').error.indexOf('-5') !== -1);
});

check('sensor_patch_bmp280', () => {
    const r = S.sensorPatch(one({ model: 'bmp280', sensor: 3 }), { enabled: true, t_off: '0,5', p_off: '-1', h_off: '9', period_s: '30' });
    assert.deepStrictEqual(r, { ok: true, body: { sensor: 3, enabled: true, t_off: 0.5, p_off: -1, period_s: 30 } });
});

check('sensor_patch_sht3x_has_humidity_no_pressure', () => {
    const r = S.sensorPatch(one({ model: 'sht3x', sensor: 2 }), { enabled: false, t_off: 0, p_off: 99, h_off: 2, period_s: 60 });
    assert.deepStrictEqual(r.body, { sensor: 2, enabled: false, t_off: 0, h_off: 2, period_s: 60 });
});

check('sensor_patch_errors_and_agg', () => {
    const bad = S.sensorPatch(one({ model: 'bmp280' }), { enabled: true, t_off: '9', p_off: '0', period_s: '30' });
    assert.strictEqual(bad.ok, false);
    assert.strictEqual(bad.field, 't_off');
    const agg = S.sensorPatch(one({ model: 'bmp280' }), { enabled: true, t_off: '0', p_off: '0', period_s: '30', agg_s: '10' });
    assert.strictEqual(agg.body.agg_s, 10);
    assert.strictEqual(S.sensorPatch(one({ model: 'bmp280' }), { enabled: true, t_off: '0', p_off: '0', period_s: '30', agg_s: '99' }).field, 'agg_s');
    assert.strictEqual(S.sensorPatch(one({ model: 'bmp280' }), { enabled: 'true', t_off: '0', p_off: '0', period_s: '30' }).body.enabled, false);
});

check('toggle_model_and_remove_bodies', () => {
    assert.deepStrictEqual(S.toggleBody(2, true), { sensor: 2, enabled: true });
    assert.deepStrictEqual(S.toggleBody(2, 0), { sensor: 2, enabled: false });
    assert.strictEqual(S.toggleBody(0, true), null);
    assert.strictEqual(S.toggleBody(9, true), null);
    assert.deepStrictEqual(S.modelBody('bmp280', false), { model: 'bmp280', enabled: false });
    assert.strictEqual(S.modelBody('nope', true), null);
    assert.strictEqual(S.modelBody('constructor', true), null);
    assert.deepStrictEqual(S.removeBody(4), { action: 'remove', sensor: 4 });
    assert.strictEqual(S.removeBody(9), null);
    assert.strictEqual(S.removeBody('1'), null);
});

check('removable_only_never_answered', () => {
    assert.strictEqual(S.removable(one({ n_ok: 0 })), true);
    assert.strictEqual(S.removable(one({ n_ok: 1 })), false);
    const r = L('error').list.map(s => [s.sensor, S.removable(s)]);
    assert.deepStrictEqual(r.sort(), [[1, false], [2, false], [3, true]]);
});

check('add_body_and_addresses', () => {
    assert.deepStrictEqual(S.addBody('sht3x', '0x44'), { action: 'add', model: 'sht3x', addr: '0x44', enabled: false });
    assert.strictEqual(S.addBody('sht3x', '0x76'), null);
    assert.strictEqual(S.addBody('aht2x', '0x39'), null);
    assert.strictEqual(S.addBody('nope', '0x44'), null);
    assert.deepStrictEqual(S.addrChoices('bmp280'), ['0x76', '0x77']);
    assert.deepStrictEqual(S.addrChoices('aht2x'), ['0x38']);
    assert.deepStrictEqual(S.addrChoices('x'), []);
});

check('addr_taken_and_can_add', () => {
    const l = L('three').list;
    assert.strictEqual(S.addrTaken(l, '0x44'), true);
    assert.strictEqual(S.addrTaken(l, '0x45'), false);
    assert.strictEqual(S.canAdd(l), true);
    assert.strictEqual(S.canAdd(L('eight').list), false);
    assert.strictEqual(S.canAdd(l, 3), false);
});

check('scan_text', () => {
    assert.strictEqual(S.scanText({ ok: true, found: 2, changed: 1 }), 'Найдено 2, новых 1');
    assert.strictEqual(S.scanText({ found: 0 }), 'Найдено 0, новых 0');
    assert.strictEqual(S.scanText({ found: 3 }), 'Найдено 3, новых 0');
    assert.strictEqual(S.scanText({ found: -1 }), 'Скан выполнен');
    assert.strictEqual(S.scanText(null), 'Скан выполнен');
    assert.strictEqual(S.scanText({}), 'Скан выполнен');
});

check('series_defs_single_sensor_keeps_legacy_ids', () => {
    const d = S.seriesDefs(L('one').list);
    assert.deepStrictEqual(d.map(x => x.id), ['air_t', 'air_p']);
    assert.deepStrictEqual(d.map(x => x.group), ['temp', 'press']);
    assert.strictEqual(d[0].name, 'T среды · №1 BMP280');
});

check('series_defs_multi_three_models', () => {
    const d = S.seriesDefs(L('three').list);
    assert.deepStrictEqual(d.map(x => x.id), ['air_t_1', 'air_t_2', 'air_t_3', 'air_p_1', 'air_h_2', 'air_h_3']);
    assert.deepStrictEqual(d.map(x => x.name), ['T среды · №1 BMP280', 'T среды · №2 SHT3x', 'T среды · №3 AHT2x', 'P среды · №1 BMP280', 'RH среды · №2 SHT3x', 'RH среды · №3 AHT2x']);
    assert.ok(d.every(x => x.field === x.id));
    assert.deepStrictEqual(d.filter(x => x.kind === 'h').map(x => x.unit + x.group), ['%hum', '%hum']);
    assert.deepStrictEqual(d.filter(x => x.kind === 't').map(x => x.tint), [0, 1, 2]);
});

check('series_defs_two_same', () => {
    const d = S.seriesDefs(L('two_same').list);
    assert.deepStrictEqual(d.map(x => x.id), ['air_t_1', 'air_t_2', 'air_p_1', 'air_p_2']);
});

check('series_defs_none_when_nothing_answers', () => {
    assert.deepStrictEqual(S.seriesDefs(L('all_off').list), []);
    assert.deepStrictEqual(S.seriesDefs(L('none').list), []);
    assert.deepStrictEqual(S.seriesDefs(L('error').list).map(x => x.id), ['air_t', 'air_p']);
});

check('series_defs_eight_counts', () => {
    const d = S.seriesDefs(L('eight').list);
    assert.strictEqual(d.filter(x => x.kind === 't').length, 8);
    assert.strictEqual(d.filter(x => x.kind === 'p').length, 4);
    assert.strictEqual(d.filter(x => x.kind === 'h').length, 6);
    assert.strictEqual(new Set(d.map(x => x.id)).size, d.length);
});

check('parse_list_misc_fields', () => {
    const r = S.parseList({ agg_s: 7, max_sensors: 5, last_scan: { n: 3, changed: 1, state: 0 }, sensors: [] });
    assert.strictEqual(r.aggS, 7);
    assert.strictEqual(r.maxSensors, 5);
    assert.deepStrictEqual(r.lastScan, { n: 3, changed: 1, state: 0 });
    assert.strictEqual(S.parseList({ max_sensors: 'x', sensors: [] }).maxSensors, 8);
    assert.strictEqual(S.parseList({ cfg: { agg_s: 9 }, sensors: [] }).aggS, 9);
});

check('models_table_matches_firmware_contract', () => {
    assert.deepStrictEqual(S.ORDER, ['bmp280', 'bme280', 'sht3x', 'aht2x']);
    assert.deepStrictEqual(S.MODELS.bmp280.addrs, ['0x76', '0x77']);
    assert.deepStrictEqual(S.MODELS.sht3x.addrs, ['0x44', '0x45']);
    assert.deepStrictEqual(S.MODELS.aht2x.addrs, ['0x38']);
    assert.strictEqual(S.MAX_SENSORS, 8);
    assert.strictEqual(S.kindsOf({ t: true, p: false, h: true }), 'T+RH');
    assert.strictEqual(S.kindsOf({ t: true, p: true, h: true }), 'T+P+RH');
    assert.strictEqual(S.kindsOf({ t: false, p: false, h: false }), '');
});

(async () => {
    for (const [name, fn] of queue) {
        total++;
        try { await fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); }
    }
    console.log('TESTS: ' + passed + '/' + total);
    process.exit(passed === total ? 0 : 1);
})();
