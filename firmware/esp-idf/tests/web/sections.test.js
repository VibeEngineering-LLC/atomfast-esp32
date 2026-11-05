const assert = require('assert');
const path = require('path');
const S = require(process.env.HS_MODULE || path.join(__dirname, '..', '..', 'web', 'sections.js'));
let passed = 0;
let total = 0;
function check(name, fn) {
    total++;
    try { fn(); passed++; console.log('ok  ' + name); }
    catch (e) { console.log('FAIL ' + name + ': ' + e.message); }
}

check('ids_structure', () => {
    assert.deepStrictEqual(S.IDS, ['status','system','wifi','ble','sensors','mqtt','db','storage','firmware','maint']);
    assert.strictEqual(S.SECTIONS.length, 10);
    S.SECTIONS.forEach((sec, i) => assert.strictEqual(sec.id, S.IDS[i]));
    assert.deepStrictEqual(S.SECTIONS[6], { id:'db', title:'База данных' });
    assert.deepStrictEqual(S.SECTIONS[7], { id:'storage', title:'Хранилище' });
    assert.deepStrictEqual(S.SECTIONS[8], { id:'firmware', title:'Прошивки' });
    assert.strictEqual(S.fromHash('#firmware'), 'firmware');
    assert.strictEqual(S.toHash('firmware'), '#firmware');
    assert.deepStrictEqual(S.needs('firmware'), ['status']);
    assert.strictEqual(S.SECTIONS[5].title, 'MQTT / Home Assistant');
    assert.strictEqual(S.SECTIONS[2].title, 'Wi-Fi и сеть');
});

check('from_hash_valid', () => {
    assert.strictEqual(S.fromHash('#storage'), 'storage');
    assert.strictEqual(S.fromHash('storage'), 'storage');
    assert.strictEqual(S.fromHash('#maint'), 'maint');
});

check('from_hash_invalid', () => {
    assert.strictEqual(S.fromHash('#nope'), null);
    assert.strictEqual(S.fromHash(''), null);
    assert.strictEqual(S.fromHash('#'), null);
    assert.strictEqual(S.fromHash(null), null);
    assert.strictEqual(S.fromHash(5), null);
    assert.strictEqual(S.fromHash('##storage'), null);
    assert.strictEqual(S.fromHash('#Storage'), null);
});

check('pick_priority', () => {
    assert.strictEqual(S.pick('#wifi', 'ble'), 'wifi');
    assert.strictEqual(S.pick('', 'ble'), 'ble');
    assert.strictEqual(S.pick('#bad', 'mqtt'), 'mqtt');
    assert.strictEqual(S.pick('', 'bad'), 'status');
    assert.strictEqual(S.pick('', null), 'status');
    assert.strictEqual(S.pick(undefined, undefined), 'status');
    assert.strictEqual(S.pick('#bad', 5), 'status');
});

check('to_hash', () => {
    assert.strictEqual(S.toHash('storage'), '#storage');
    assert.strictEqual(S.toHash('summary'), '#status');
    assert.strictEqual(S.fromHash('#summary'), 'status');
    assert.strictEqual(S.toHash('zzz'), '#status');
    assert.strictEqual(S.toHash(null), '#status');
});

check('needs_mapping', () => {
    assert.deepStrictEqual(S.needs('ble'), ['status','ble','sessions']);
    assert.deepStrictEqual(S.needs('status'), ['status']);
    assert.deepStrictEqual(S.needs('system'), ['status','system']);
    assert.deepStrictEqual(S.needs('wifi'), ['status','system']);
    assert.deepStrictEqual(S.needs('ble'), ['status','ble','sessions']);
    assert.deepStrictEqual(S.needs('sensors'), ['sensors']);
    assert.deepStrictEqual(S.needs('mqtt'), ['status']);
    assert.deepStrictEqual(S.needs('storage'), ['status','storage']);
    assert.deepStrictEqual(S.needs('maint'), ['status','system']);
    assert.deepStrictEqual(S.needs('zzz'), ['status']);
});

check('needs_immutable', () => {
    const arr = S.needs('ble');
    arr.push('x');
    assert.deepStrictEqual(S.needs('ble'), ['status','ble','sessions']);
});

check('badges_empty', () => {
    const b1 = S.badges(null);
    assert.deepStrictEqual(Object.keys(b1), S.IDS);
    S.IDS.forEach(id => assert.deepStrictEqual(b1[id], { text:'', level:'' }));
    
    const b2 = S.badges({});
    assert.deepStrictEqual(Object.keys(b2), S.IDS);
    S.IDS.forEach(id => assert.deepStrictEqual(b2[id], { text:'', level:'' }));
});

const ST = { wifi_connected:true, ble_connected:true, ble_ready:true, ble_errors:0, mqtt:{ enabled:1, connected:1, failed:0 } };
const SY = { free_heap:3298560, int_free:98304, flash_total:1048576, flash_used:317440, rssi:-58 };
const SN = { sensor:{ status:'ok', model:'BMP280', temp:true, press:true, t_c:26.5, p_mmhg:751.7 } };
const HS = { raw:{ count:100, limit:1000 }, persist:{ flush_errs:0 } };

check('badges_ok', () => {
    const b = S.badges({st:ST, sy:SY, sn:SN, hs:HS});
    assert.deepStrictEqual(b.status, { text:'', level:'' });
    assert.strictEqual(b.system.level, '');
    assert.strictEqual(b.wifi.level, '');
    assert.deepStrictEqual(b.ble, { text:'', level:'' });
    assert.deepStrictEqual(b.sensors, { text:'1', level:'' });
    assert.deepStrictEqual(b.mqtt, { text:'', level:'' });
    assert.deepStrictEqual(b.storage, { text:'', level:'' });
    assert.strictEqual(b.summary, undefined);
    assert.deepStrictEqual(b.maint, { text:'', level:'' });
});

check('status_bad', () => {
    const st = { wifi_connected:false, ble_connected:true, ble_ready:true, ble_errors:0 };
    const b = S.badges({st:st, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.status.level, 'bad');
});

check('status_warn', () => {
    const st = { wifi_connected:true, ble_connected:false, ble_ready:true, ble_errors:0 };
    const b = S.badges({st:st, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.status.level, 'warn');
});

check('status_null', () => {
    const b = S.badges({st:null, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.status.level, '');
});

check('system_warn_flash', () => {
    const sy = { ...SY, flash_used:800000 };
    const b = S.badges({st:ST, sy:sy, sn:SN, hs:HS});
    assert.strictEqual(b.system.level, 'warn');
});

check('system_bad_flash', () => {
    const sy = { ...SY, flash_used:950000 };
    const b = S.badges({st:ST, sy:sy, sn:SN, hs:HS});
    assert.strictEqual(b.system.level, 'bad');
});

check('system_bad_int', () => {
    const sy = { ...SY, int_free:20000 };
    const b = S.badges({st:ST, sy:sy, sn:SN, hs:HS});
    assert.strictEqual(b.system.level, 'bad');
});

check('system_warn_heap', () => {
    const sy = { ...SY, free_heap:70000 };
    const b = S.badges({st:ST, sy:sy, sn:SN, hs:HS});
    assert.strictEqual(b.system.level, 'warn');
});

check('system_null', () => {
    const b = S.badges({st:ST, sy:null, sn:SN, hs:HS});
    assert.strictEqual(b.system.level, '');
});

check('wifi_warn', () => {
    const sy = { ...SY, rssi:-70 };
    const b = S.badges({st:ST, sy:sy, sn:SN, hs:HS});
    assert.strictEqual(b.wifi.level, 'warn');
});

check('wifi_bad', () => {
    const sy = { ...SY, rssi:-80 };
    const b = S.badges({st:ST, sy:sy, sn:SN, hs:HS});
    assert.strictEqual(b.wifi.level, 'bad');
});

check('wifi_bad_conn', () => {
    const st = { ...ST, wifi_connected:false };
    const b = S.badges({st:st, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.wifi.level, 'bad');
});

check('ble_warn_errs', () => {
    const st = { ...ST, ble_errors:3 };
    const b = S.badges({st:st, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.ble.text, '3');
    assert.strictEqual(b.ble.level, 'warn');
});

check('ble_warn_conn', () => {
    const st = { ...ST, ble_connected:false, ble_ready:true };
    const b = S.badges({st:st, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.ble.level, 'warn');
});

check('ble_bad_conn', () => {
    const st = { ...ST, ble_connected:false, ble_ready:false };
    const b = S.badges({st:st, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.ble.level, 'bad');
});

check('ble_round', () => {
    const st = { ...ST, ble_errors:2.6 };
    const b = S.badges({st:st, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.ble.text, '3');
});

check('ble_zero', () => {
    const st = { ...ST, ble_errors:0 };
    const b = S.badges({st:st, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.ble.text, '');
});

check('sensors_bad', () => {
    const sn = { sensor:{ status:'bus_error', model:'BMP280' } };
    const b = S.badges({st:ST, sy:SY, sn:sn, hs:HS});
    assert.strictEqual(b.sensors.text, '1');
    assert.strictEqual(b.sensors.level, 'bad');
});

check('sensors_warn', () => {
    const sn = { sensor:{ status:'no_sensor', model:'BMP280' } };
    const b = S.badges({st:ST, sy:SY, sn:sn, hs:HS});
    assert.strictEqual(b.sensors.level, 'warn');
});

check('sensors_multi', () => {
    const sn = { sensors: [ { sensor:{status:'ok', model:'A'} }, { sensor:{status:'bus_error', model:'B'} } ] };
    const b = S.badges({st:ST, sy:SY, sn:sn, hs:HS});
    assert.strictEqual(b.sensors.text, '2');
    assert.strictEqual(b.sensors.level, 'bad');
});

check('sensors_null', () => {
    const b = S.badges({st:ST, sy:SY, sn:null, hs:HS});
    assert.strictEqual(b.sensors.text, '');
    assert.strictEqual(b.sensors.level, '');
});

check('mqtt_warn', () => {
    const st = { ...ST, mqtt:{ enabled:1, connected:0, failed:0 } };
    const b = S.badges({st:st, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.mqtt.level, 'warn');
});

check('mqtt_failed', () => {
    const st = { ...ST, mqtt:{ enabled:1, connected:1, failed:4 } };
    const b = S.badges({st:st, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.mqtt.text, '4');
    assert.strictEqual(b.mqtt.level, '');
});

check('mqtt_disabled', () => {
    const st = { ...ST, mqtt:{ enabled:0, connected:0, failed:0 } };
    const b = S.badges({st:st, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.mqtt.level, '');
});

check('mqtt_missing', () => {
    const st = { wifi_connected:true, ble_connected:true, ble_ready:true, ble_errors:0 };
    const b = S.badges({st:st, sy:SY, sn:SN, hs:HS});
    assert.strictEqual(b.mqtt.level, '');
});

check('storage_flush', () => {
    const hs = { raw:{ count:100, limit:1000 }, persist:{ flush_errs:2 } };
    const b = S.badges({st:ST, sy:SY, sn:SN, hs:hs});
    assert.strictEqual(b.storage.text, '2');
    assert.strictEqual(b.storage.level, 'bad');
});

check('storage_warn', () => {
    const hs = { raw:{ count:800, limit:1000 }, persist:{ flush_errs:0 } };
    const b = S.badges({st:ST, sy:SY, sn:SN, hs:hs});
    assert.strictEqual(b.storage.level, 'warn');
    assert.strictEqual(b.storage.text, '');
});

check('storage_bad', () => {
    const hs = { raw:{ count:1000, limit:1000 }, persist:{ flush_errs:0 } };
    const b = S.badges({st:ST, sy:SY, sn:SN, hs:hs});
    assert.strictEqual(b.storage.level, 'bad');
});

check('storage_ok', () => {
    const hs = { raw:{ count:799, limit:1000 }, persist:{ flush_errs:0 } };
    const b = S.badges({st:ST, sy:SY, sn:SN, hs:hs});
    assert.strictEqual(b.storage.level, '');
});

check('storage_null', () => {
    const b = S.badges({st:ST, sy:SY, sn:SN, hs:null});
    assert.strictEqual(b.storage.text, '');
    assert.strictEqual(b.storage.level, '');
});

check('storage_priority', () => {
    const hs = { raw:{ count:1000, limit:1000 }, persist:{ flush_errs:2 } };
    const b = S.badges({st:ST, sy:SY, sn:SN, hs:hs});
    assert.strictEqual(b.storage.text, '2');
    assert.strictEqual(b.storage.level, 'bad');
});

check('extra boundary cases', () => {
    const ST0 = { ble_connected: true, ble_ready: true, mqtt: { enabled: 1, connected: 1, failed: 1 }, ble_errors: 1 };
    const b = S.badges({ st: ST0 });
    assert.strictEqual(b.status.level, 'bad');
    assert.deepStrictEqual(b.ble, { text: '1', level: 'warn' });
    assert.strictEqual(b.mqtt.text, '1');
    const sn = { sensors: [ { sensor: { status: 'bus_error', model: 'A' } }, { sensor: { status: 'ok', model: 'B' } } ] };
    assert.strictEqual(S.badges({ sn }).sensors.level, 'bad');
});

check('sensors_badge_counts_enabled_only', () => {
    const sn = { sensors: [ { sensor:1, model:'bmp280', status:'ok', enabled:true }, { sensor:2, model:'sht3x', status:'off', enabled:false }, { sensor:3, model:'aht2x', status:'ok', enabled:true } ] };
    const b = S.badges({ sn });
    assert.strictEqual(b.sensors.text, '2');
    assert.strictEqual(b.sensors.level, '');
});

check('sensors_badge_warn_on_enabled_error_not_on_off', () => {
    const e = { sensors: [ { sensor:1, model:'bmp280', status:'ok', enabled:true }, { sensor:2, model:'sht3x', status:'err', enabled:true } ] };
    assert.strictEqual(S.badges({ sn: e }).sensors.level, 'warn');
    const o = { sensors: [ { sensor:1, model:'bmp280', status:'ok', enabled:true }, { sensor:2, model:'sht3x', status:'off', enabled:false } ] };
    assert.strictEqual(S.badges({ sn: o }).sensors.level, '');
    assert.strictEqual(S.badges({ sn: o }).sensors.text, '1');
});

console.log('TESTS: ' + passed + '/' + total); process.exit(passed === total ? 0 : 1);
