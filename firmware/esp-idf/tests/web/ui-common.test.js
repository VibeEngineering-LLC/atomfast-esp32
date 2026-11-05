const assert = require('assert');
const path = require('path');
const U = require(process.env.HU_MODULE || path.join(__dirname, '..', '..', 'web', 'ui-common.js'));
let passed = 0;
let total = 0;
function check(name, fn) {
    total++;
    try { fn(); passed++; console.log('ok  ' + name); }
    catch (e) { console.log('FAIL ' + name + ': ' + e.message); }
}
const DASH = '—';

// fmtNum
check('fmtNum basic', () => {
    assert.strictEqual(U.fmtNum(26.514, 1), '26.5');
    assert.strictEqual(U.fmtNum(751.7, 1), '751.7');
    assert.strictEqual(U.fmtNum(0, 2), '0.00');
});

check('fmtNum neg zero', () => {
    assert.strictEqual(U.fmtNum(-0.04, 1), '0.0');
    assert.strictEqual(U.fmtNum(-0.0004, 3), '0.000');
    assert.strictEqual(U.fmtNum(-0.4, 0), '0');
    assert.strictEqual(U.fmtNum(-0.5, 1), '-0.5');
    assert.strictEqual(U.fmtNum(-10, 0), '-10');
});

check('fmtNum invalid', () => {
    assert.strictEqual(U.fmtNum(NaN, 1), DASH);
    assert.strictEqual(U.fmtNum(Infinity, 1), DASH);
    assert.strictEqual(U.fmtNum('5', 1), DASH);
    assert.strictEqual(U.fmtNum(null, 1), DASH);
    assert.strictEqual(U.fmtNum(undefined, 1), DASH);
});

// pageOf
check('pageOf home', () => {
    assert.strictEqual(U.pageOf('/'), 'history');
    assert.strictEqual(U.pageOf(''), 'history');
    assert.strictEqual(U.pageOf('/index.html'), 'history');
});

check('pageOf known', () => {
    assert.strictEqual(U.pageOf('/history'), 'history');
    assert.strictEqual(U.pageOf('/settings'), 'settings');
    assert.strictEqual(U.pageOf('/readings'), 'settings');
    assert.strictEqual(U.pageOf('/system'), 'system');
});

check('pageOf trailing slash', () => {
    assert.strictEqual(U.pageOf('/history/'), 'history');
    assert.strictEqual(U.pageOf('/system/'), 'system');
    assert.strictEqual(U.pageOf('/settings/'), 'settings');
});

check('pageOf unknown', () => {
    assert.strictEqual(U.pageOf('/foo'), null);
    assert.strictEqual(U.pageOf('/history/x'), null);
    assert.strictEqual(U.pageOf('/api/status'), null);
    assert.strictEqual(U.pageOf(undefined), null);
    assert.strictEqual(U.pageOf(5), null);
    assert.strictEqual(U.pageOf(null), null);
});

// fmtUptime
check('fmtUptime basic', () => {
    assert.strictEqual(U.fmtUptime(0), '00:00:00');
    assert.strictEqual(U.fmtUptime(59), '00:00:59');
    assert.strictEqual(U.fmtUptime(3725), '01:02:05');
    assert.strictEqual(U.fmtUptime(86399), '23:59:59');
});

check('fmtUptime days', () => {
    assert.strictEqual(U.fmtUptime(86400), '1д 00:00:00');
    assert.strictEqual(U.fmtUptime(90061), '1д 01:01:01');
    assert.strictEqual(U.fmtUptime(172800), '2д 00:00:00');
    assert.strictEqual(U.fmtUptime(3725.9), '01:02:05');
});

check('fmtUptime invalid', () => {
    assert.strictEqual(U.fmtUptime(-1), DASH);
    assert.strictEqual(U.fmtUptime(NaN), DASH);
    assert.strictEqual(U.fmtUptime('10'), DASH);
    assert.strictEqual(U.fmtUptime(null), DASH);
    assert.strictEqual(U.fmtUptime(undefined), DASH);
    assert.strictEqual(U.fmtUptime(Infinity), DASH);
});

// fmtBytes
check('fmtBytes bytes', () => {
    assert.strictEqual(U.fmtBytes(0), '0 Б');
    assert.strictEqual(U.fmtBytes(512), '512 Б');
    assert.strictEqual(U.fmtBytes(1023), '1023 Б');
});

check('fmtBytes kib', () => {
    assert.strictEqual(U.fmtBytes(1024), '1.0 КиБ');
    assert.strictEqual(U.fmtBytes(98304), '96.0 КиБ');
    assert.strictEqual(U.fmtBytes(1048575), '1024.0 КиБ');
});

check('fmtBytes mib', () => {
    assert.strictEqual(U.fmtBytes(1048576), '1.00 МиБ');
    assert.strictEqual(U.fmtBytes(3298560), '3.15 МиБ');
});

check('fmtBytes invalid', () => {
    assert.strictEqual(U.fmtBytes(-1), DASH);
    assert.strictEqual(U.fmtBytes(NaN), DASH);
    assert.strictEqual(U.fmtBytes('5'), DASH);
    assert.strictEqual(U.fmtBytes(null), DASH);
    assert.strictEqual(U.fmtBytes(undefined), DASH);
});

// usedPct / pctLevel / freeLevel
check('usedPct basic', () => {
    assert.strictEqual(U.usedPct(317440, 1048576), 30);
    assert.strictEqual(U.usedPct(0, 100), 0);
    assert.strictEqual(U.usedPct(100, 100), 100);
    assert.strictEqual(U.usedPct(150, 100), 100);
    assert.strictEqual(U.usedPct(1, 3), 33);
});

check('usedPct invalid', () => {
    assert.strictEqual(U.usedPct(0, 0), null);
    assert.strictEqual(U.usedPct(0, -5), null);
    assert.strictEqual(U.usedPct(-1, 100), null);
    assert.strictEqual(U.usedPct(NaN, 100), null);
    assert.strictEqual(U.usedPct(100, undefined), null);
});

check('pctLevel', () => {
    assert.strictEqual(U.pctLevel(0), 'ok');
    assert.strictEqual(U.pctLevel(74), 'ok');
    assert.strictEqual(U.pctLevel(75), 'warn');
    assert.strictEqual(U.pctLevel(89), 'warn');
    assert.strictEqual(U.pctLevel(90), 'bad');
    assert.strictEqual(U.pctLevel(100), 'bad');
    assert.strictEqual(U.pctLevel(null), 'mut');
    assert.strictEqual(U.pctLevel(NaN), 'mut');
});

check('freeLevel', () => {
    assert.strictEqual(U.freeLevel(100000, 80000, 40000), 'ok');
    assert.strictEqual(U.freeLevel(80000, 80000, 40000), 'ok');
    assert.strictEqual(U.freeLevel(79999, 80000, 40000), 'warn');
    assert.strictEqual(U.freeLevel(40000, 80000, 40000), 'warn');
    assert.strictEqual(U.freeLevel(39999, 80000, 40000), 'bad');
    assert.strictEqual(U.freeLevel(NaN, 1, 1), 'mut');
    assert.strictEqual(U.freeLevel(undefined, 1, 1), 'mut');
});

// rssiQuality
check('rssiQuality high', () => {
    assert.deepStrictEqual(U.rssiQuality(-40), { pct: 100, label: 'отличный', level: 'ok' });
    assert.deepStrictEqual(U.rssiQuality(-55), { pct: 90, label: 'отличный', level: 'ok' });
    assert.deepStrictEqual(U.rssiQuality(-56), { pct: 88, label: 'хороший', level: 'ok' });
});

check('rssiQuality mid', () => {
    assert.deepStrictEqual(U.rssiQuality(-67), { pct: 66, label: 'хороший', level: 'ok' });
    assert.deepStrictEqual(U.rssiQuality(-68), { pct: 64, label: 'средний', level: 'warn' });
    assert.deepStrictEqual(U.rssiQuality(-75), { pct: 50, label: 'средний', level: 'warn' });
});

check('rssiQuality low', () => {
    assert.deepStrictEqual(U.rssiQuality(-76), { pct: 48, label: 'слабый', level: 'bad' });
    assert.deepStrictEqual(U.rssiQuality(-100), { pct: 0, label: 'слабый', level: 'bad' });
    assert.deepStrictEqual(U.rssiQuality(-120), { pct: 0, label: 'слабый', level: 'bad' });
    assert.deepStrictEqual(U.rssiQuality(-30), { pct: 100, label: 'отличный', level: 'ok' });
});

check('rssiQuality invalid', () => {
    assert.deepStrictEqual(U.rssiQuality(NaN), { pct: null, label: DASH, level: 'mut' });
    assert.deepStrictEqual(U.rssiQuality(null), { pct: null, label: DASH, level: 'mut' });
    assert.deepStrictEqual(U.rssiQuality(undefined), { pct: null, label: DASH, level: 'mut' });
    assert.deepStrictEqual(U.rssiQuality('-50'), { pct: null, label: DASH, level: 'mut' });
});

// yesNo
check('yesNo truthy', () => {
    assert.strictEqual(U.yesNo(true), 'да');
    assert.strictEqual(U.yesNo(1), 'да');
    assert.strictEqual(U.yesNo('1'), 'да');
    assert.strictEqual(U.yesNo('true'), 'да');
    assert.strictEqual(U.yesNo('yes'), 'да');
});

check('yesNo falsy', () => {
    assert.strictEqual(U.yesNo(false), 'нет');
    assert.strictEqual(U.yesNo(0), 'нет');
    assert.strictEqual(U.yesNo('0'), 'нет');
    assert.strictEqual(U.yesNo('false'), 'нет');
    assert.strictEqual(U.yesNo('no'), 'нет');
});

check('yesNo other', () => {
    assert.strictEqual(U.yesNo(null), DASH);
    assert.strictEqual(U.yesNo(undefined), DASH);
    assert.strictEqual(U.yesNo('maybe'), 'maybe');
    assert.strictEqual(U.yesNo(2), '2');
});

// heroModel
const st_base = { wifi_connected: true, ble_ready: true, ble_connected: true, fw_version: '0.1.0-e13', uptime_s: 93784 };
const sn_base = { live: { has_rec: true, rate_usvh: 0.11, cps: 0.5 }, sensor: { status: 'ok', temp: true, press: true, t_c: 26.51, p_mmhg: 751.7 } };

check('heroModel basic', () => {
    const m = U.heroModel(st_base, sn_base);
    assert.strictEqual(m.wifi, 'ok');
    assert.strictEqual(m.ble, 'ok');
    assert.strictEqual(m.bleText, 'подключён');
    assert.strictEqual(m.rate, '0.110');
    assert.strictEqual(m.cps, '0.5');
    assert.strictEqual(m.airT, '26.5');
    assert.strictEqual(m.airP, '751.7');
    assert.strictEqual(m.airOk, true);
    assert.strictEqual(m.airNote, 'BMP280');
    assert.strictEqual(m.fw, '0.1.0-e13');
    assert.strictEqual(m.uptime, '1д 02:03:04');
});

check('heroModel ble states', () => {
    let m = U.heroModel({ ...st_base, ble_connected: undefined }, sn_base);
    assert.strictEqual(m.ble, 'warn');
    assert.strictEqual(m.bleText, 'ищет прибор');
    
    m = U.heroModel({ ...st_base, ble_ready: false, ble_connected: false }, sn_base);
    assert.strictEqual(m.ble, 'bad');
    assert.strictEqual(m.bleText, 'нет');
    
    m = U.heroModel(null, sn_base);
    assert.strictEqual(m.wifi, 'bad');
    assert.strictEqual(m.ble, 'bad');
    assert.strictEqual(m.fw, DASH);
    assert.strictEqual(m.uptime, DASH);
});

check('heroModel wifi states', () => {
    let m = U.heroModel({ ...st_base, wifi_connected: false }, sn_base);
    assert.strictEqual(m.wifi, 'bad');
    
    m = U.heroModel({ ...st_base, wifi_connected: 1 }, sn_base);
    assert.strictEqual(m.wifi, 'bad');
});

check('heroModel live states', () => {
    let m = U.heroModel(st_base, { sensor: sn_base.sensor });
    assert.strictEqual(m.rate, DASH);
    assert.strictEqual(m.cps, DASH);
    
    m = U.heroModel(st_base, { live: { has_rec: false, rate_usvh: 0.11, cps: 0.5 }, sensor: sn_base.sensor });
    assert.strictEqual(m.rate, DASH);
    assert.strictEqual(m.cps, DASH);
    
    m = U.heroModel(st_base, { live: null, sensor: sn_base.sensor });
    assert.strictEqual(m.rate, DASH);
});

check('heroModel sensor states', () => {
    let m = U.heroModel(st_base, { live: sn_base.live, sensor: { status: 'disabled' } });
    assert.strictEqual(m.airNote, 'датчик выключен');
    assert.strictEqual(m.airOk, false);
    assert.strictEqual(m.airT, DASH);
    
    m = U.heroModel(st_base, { live: sn_base.live, sensor: { status: 'no_sensor' } });
    assert.strictEqual(m.airNote, 'датчик не найден');
    
    m = U.heroModel(st_base, { live: sn_base.live, sensor: { status: 'bus_error' } });
    assert.strictEqual(m.airNote, 'ошибка шины I2C');
    
    m = U.heroModel(st_base, { live: sn_base.live, sensor: { status: 'read_error' } });
    assert.strictEqual(m.airNote, 'датчик не отвечает');
    
    m = U.heroModel(st_base, { live: sn_base.live });
    assert.strictEqual(m.airNote, 'нет данных');
    
    m = U.heroModel(st_base, null);
    assert.strictEqual(m.airNote, 'нет данных');
    assert.strictEqual(m.rate, DASH);
});

check('heroModel sensor flags', () => {
    let m = U.heroModel(st_base, { live: sn_base.live, sensor: { status: 'ok', temp: false, press: true, t_c: 26.51, p_mmhg: 751.7 } });
    assert.strictEqual(m.airT, DASH);
    assert.strictEqual(m.airP, '751.7');
    
    m = U.heroModel(st_base, { live: sn_base.live, sensor: { status: 'ok', temp: true, press: false, t_c: 26.51, p_mmhg: 751.7 } });
    assert.strictEqual(m.airP, DASH);
    assert.strictEqual(m.airT, '26.5');
});

// sysModel
const st_sys = { fw_version: '0.1.0-e13', running_partition: 'ota_0', uptime_s: 3725, boot_count: 14, reset_reason: 'software', time_source: 'none', sntp_synced: false };
const sy_sys = { free_heap: 3298560, min_free_heap: 3201024, psram_total: 8386560, psram_free: 7712768, int_free: 98304, int_min: 93215, flash_total: 1048576, flash_used: 317440, rssi: -58, ssid: 'demo-net', net_mode: 'sta' };

check('sysModel basic', () => {
    const m = U.sysModel(st_sys, sy_sys);
    assert.strictEqual(m.fw, '0.1.0-e13');
    assert.strictEqual(m.part, 'ota_0');
    assert.strictEqual(m.uptime, '01:02:05');
    assert.strictEqual(m.boots, '14');
    assert.strictEqual(m.reset, 'software');
    assert.strictEqual(m.heapFree, '3.15 МиБ');
    assert.strictEqual(m.heapMin, '3.05 МиБ');
    assert.strictEqual(m.heapLevel, 'ok');
    assert.strictEqual(m.intFree, '96.0 КиБ');
    assert.strictEqual(m.intMin, '91.0 КиБ');
    assert.strictEqual(m.intLevel, 'ok');
    assert.strictEqual(m.psramFree, '7.36 МиБ');
    assert.strictEqual(m.psramTotal, '8.00 МиБ');
    assert.strictEqual(m.flashPct, 30);
    assert.strictEqual(m.flashLevel, 'ok');
    assert.strictEqual(m.flashText, '310.0 КиБ из 1.00 МиБ');
    assert.strictEqual(m.rssiText, '-58 дБм');
    assert.strictEqual(m.rssi.label, 'хороший');
    assert.strictEqual(m.rssi.level, 'ok');
    assert.strictEqual(m.rssi.pct, 84);
    assert.strictEqual(m.ssid, 'demo-net');
    assert.strictEqual(m.netMode, 'sta');
    assert.strictEqual(m.timeSource, 'none');
    assert.strictEqual(m.sntp, 'нет');
});

check('sysModel mem levels', () => {
    let m = U.sysModel(st_sys, { ...sy_sys, free_heap: 70000 });
    assert.strictEqual(m.heapLevel, 'warn');
    
    m = U.sysModel(st_sys, { ...sy_sys, free_heap: 30000 });
    assert.strictEqual(m.heapLevel, 'bad');
    
    m = U.sysModel(st_sys, { ...sy_sys, int_free: 50000 });
    assert.strictEqual(m.intLevel, 'warn');
    
    m = U.sysModel(st_sys, { ...sy_sys, int_free: 20000 });
    assert.strictEqual(m.intLevel, 'bad');
    
    m = U.sysModel(st_sys, { ...sy_sys, int_free: 60000 });
    assert.strictEqual(m.intLevel, 'ok');
});

check('sysModel flash levels', () => {
    let m = U.sysModel(st_sys, { ...sy_sys, flash_used: 800000, flash_total: 1000000 });
    assert.strictEqual(m.flashPct, 80);
    assert.strictEqual(m.flashLevel, 'warn');
    
    m = U.sysModel(st_sys, { ...sy_sys, flash_used: 950000, flash_total: 1000000 });
    assert.strictEqual(m.flashPct, 95);
    assert.strictEqual(m.flashLevel, 'bad');
});

check('sysModel nulls', () => {
    const m = U.sysModel(null, null);
    assert.strictEqual(m.fw, DASH);
    assert.strictEqual(m.part, DASH);
    assert.strictEqual(m.uptime, DASH);
    assert.strictEqual(m.boots, DASH);
    assert.strictEqual(m.reset, DASH);
    assert.strictEqual(m.heapFree, DASH);
    assert.strictEqual(m.intFree, DASH);
    assert.strictEqual(m.psramFree, DASH);
    assert.strictEqual(m.psramTotal, DASH);
    assert.strictEqual(m.ssid, DASH);
    assert.strictEqual(m.netMode, DASH);
    assert.strictEqual(m.timeSource, DASH);
    assert.strictEqual(m.rssiText, DASH);
    assert.strictEqual(m.sntp, DASH);
    assert.strictEqual(m.flashPct, null);
    assert.strictEqual(m.flashLevel, 'mut');
    assert.strictEqual(m.heapLevel, 'mut');
    assert.strictEqual(m.intLevel, 'mut');
    assert.strictEqual(m.rssi.level, 'mut');
    assert.strictEqual(m.flashText, DASH + ' из ' + DASH);
});

check('sysModel invalid types', () => {
    let m = U.sysModel({ ...st_sys, boot_count: '14' }, sy_sys);
    assert.strictEqual(m.boots, DASH);
    
    m = U.sysModel({ ...st_sys, running_partition: 123 }, sy_sys);
    assert.strictEqual(m.part, DASH);
    
    m = U.sysModel(st_sys, { ...sy_sys, ssid: 123 });
    assert.strictEqual(m.ssid, DASH);
    
    m = U.sysModel(st_sys, { ...sy_sys, rssi: '-58' });
    assert.strictEqual(m.rssiText, DASH);
});

check('sysModel sntp', () => {
    let m = U.sysModel({ ...st_sys, sntp_synced: true }, sy_sys);
    assert.strictEqual(m.sntp, 'да');
    
    m = U.sysModel({ ...st_sys, sntp_synced: undefined }, sy_sys);
    assert.strictEqual(m.sntp, DASH);
});

check('fontPct valid values', () => {
    for (const v of ['90', '100', '115', '130']) assert.strictEqual(U.fontPct(v), parseInt(v, 10));
    assert.strictEqual(U.fontPct(115), 115);
    assert.strictEqual(U.fontPct(' 130 '), 130);
});
check('fontPct invalid falls back to default', () => {
    for (const v of ['95', '110', 'abc', '', null, undefined, '1150', 120, NaN, {}, '9', '115abc', '130px']) assert.strictEqual(U.fontPct(v), 100);
});
check('fontScale factor', () => {
    assert.strictEqual(U.fontScale('130'), 1.3);
    assert.strictEqual(U.fontScale('90'), 0.9);
    assert.strictEqual(U.fontScale('115'), 1.15);
    assert.strictEqual(U.fontScale('x'), 1);
    assert.strictEqual(U.fontScale(undefined), 1);
});
check('FONT_SCALES list', () => {
    assert.deepStrictEqual(U.FONT_SCALES.map(s => s.pct), [90, 100, 115, 130]);
    assert.deepStrictEqual(U.FONT_SCALES.map(s => s.label), ['Мелкий', 'Обычный', 'Крупный', 'Очень крупный']);
});

console.log('TESTS: ' + passed + '/' + total); process.exit(passed === total ? 0 : 1);
