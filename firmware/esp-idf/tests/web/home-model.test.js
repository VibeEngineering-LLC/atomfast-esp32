const assert = require('assert');
const path = require('path');
const M = require(process.env.HM_MODULE || path.join(__dirname, '..', '..', 'web', 'home-model.js'));
let passed = 0;
let total = 0;
function check(name, fn) {
    total++;
    try { fn(); passed++; console.log('ok  ' + name); }
    catch (e) { console.log('FAIL ' + name + ': ' + e.message); }
}
const DASH = '—';

const SN = { sensor: { status:'ok', model:'BMP280', addr:'0x76', temp:true, press:true, hum:false, t_c:26.51, p_mmhg:751.7, last_ok_s_ago:14 }, live: { has_rec:true, cps:0.5, rate_usvh:0.11, rate_urh:11.0, dose_usv:1.234, avg_cps:0.45 } };
const ST = { ble_connected:true, ble_ready:true, wifi_connected:true, fw_version:'0.1.0-e13', sntp_synced:true, time_source:'sntp', mqtt:{ enabled:1, connected:1 }, ble_last_rec:{ battery:27 } };

check('sensorCard basic', () => {
    const c = M.sensorCard(SN.sensor, 0);
    assert.strictEqual(c.id, 'sensor0');
    assert.strictEqual(c.model, 'BMP280');
    assert.strictEqual(c.addr, '0x76');
    assert.strictEqual(c.title, 'BMP280 (0x76)');
    assert.strictEqual(c.number, 1);
    assert.strictEqual(c.level, 'ok');
    assert.strictEqual(c.statusText, 'работает');
    assert.strictEqual(c.ageText, 'измерено 14 с назад');
    assert.deepStrictEqual(c.values, [
        { id:'t', label:'Температура', value:'26.5', unit:'°C' },
        { id:'p', label:'Давление', value:'751.7', unit:'мм рт. ст.' }
    ]);
});

check('sensorCard humidity', () => {
    const s = { ...SN.sensor, hum:true, h_pct:41.26 };
    const c = M.sensorCard(s, 0);
    assert.strictEqual(c.values.length, 3);
    assert.deepStrictEqual(c.values[2], { id:'h', label:'Влажность', value:'41.3', unit:'%' });
    
    const s2 = { ...SN.sensor, temp:false };
    const c2 = M.sensorCard(s2, 0);
    assert.strictEqual(c2.values.length, 1);
    assert.strictEqual(c2.values[0].id, 'p');
});

check('sensorCard statuses', () => {
    const cases = [
        ['init', 'mut', 'опрашивается'],
        ['disabled', 'mut', 'выключен'],
        ['no_sensor', 'warn', 'не найден'],
        ['bus_error', 'bad', 'ошибка шины I2C'],
        ['read_error', 'bad', 'не отвечает'],
        ['unsupported', 'warn', 'не поддерживается'],
        ['xyz', 'mut', 'состояние неизвестно']
    ];
    cases.forEach(([status, level, text]) => {
        const c = M.sensorCard({ status }, 0);
        assert.strictEqual(c.level, level, `status ${status} level`);
        assert.strictEqual(c.statusText, text, `status ${status} text`);
        assert.deepStrictEqual(c.values, [], `status ${status} values`);
        assert.strictEqual(c.ageText, '', `status ${status} age`);
    });
});

check('sensorCard defaults', () => {
    const c1 = M.sensorCard({}, 2);
    assert.strictEqual(c1.id, 'sensor2');
    assert.strictEqual(c1.model, 'Датчик');
    assert.strictEqual(c1.addr, '');
    assert.strictEqual(c1.title, 'Датчик');
    assert.strictEqual(c1.number, 3);
    assert.strictEqual(c1.name, '');

    const c2 = M.sensorCard(null, 0);
    assert.strictEqual(c2.model, 'Датчик');

    const c3 = M.sensorCard({ model:'SHT31', index:7, name:'Кухня' }, 0);
    assert.strictEqual(c3.number, 7);
    assert.strictEqual(c3.name, 'Кухня');
});

check('sensorCard age', () => {
    const base = { status:'ok', model:'BMP280' };
    assert.strictEqual(M.sensorCard({ ...base, last_ok_s_ago: 0 }, 0).ageText, 'измерено 0 с назад');
    assert.strictEqual(M.sensorCard({ ...base, last_ok_s_ago: -1 }, 0).ageText, '');
    assert.strictEqual(M.sensorCard({ ...base, last_ok_s_ago: 2.6 }, 0).ageText, 'измерено 3 с назад');
    assert.strictEqual(M.sensorCard({ ...base, last_ok_s_ago: 'x' }, 0).ageText, '');
});

check('sensorCards single', () => {
    const cards = M.sensorCards(SN);
    assert.strictEqual(cards.length, 1);
    assert.strictEqual(cards[0].id, 'sensor0');
    
    assert.deepStrictEqual(M.sensorCards(null), []);
    assert.deepStrictEqual(M.sensorCards({}), []);
    assert.deepStrictEqual(M.sensorCards({ sensor: 5 }), []);
});

check('sensorCards array', () => {
    const input = { sensors: [ 
        { sensor: { model:'BMP280', status:'ok' } }, 
        { model:'SHT31', status:'ok' }, 
        5, 
        null, 
        { sensor: { model:'BMP280', status:'no_sensor' } } 
    ] };
    const cards = M.sensorCards(input);
    assert.strictEqual(cards.length, 3);
    assert.strictEqual(cards[0].id, 'sensor0');
    assert.strictEqual(cards[1].id, 'sensor1');
    assert.strictEqual(cards[2].id, 'sensor2');
    assert.strictEqual(cards[0].model, 'BMP280');
    assert.strictEqual(cards[1].model, 'SHT31');
    assert.strictEqual(cards[2].model, 'BMP280');
});

check('groupByModel', () => {
    const input = { sensors: [ 
        { sensor: { model:'BMP280', status:'ok' } }, 
        { model:'SHT31', status:'ok' }, 
        5, 
        null, 
        { sensor: { model:'BMP280', status:'no_sensor' } } 
    ] };
    const cards = M.sensorCards(input);
    const groups = M.groupByModel(cards);
    assert.strictEqual(groups.length, 2);
    assert.strictEqual(groups[0].model, 'BMP280');
    assert.strictEqual(groups[0].cards.length, 2);
    assert.strictEqual(groups[1].model, 'SHT31');
    assert.strictEqual(groups[1].cards.length, 1);
    
    assert.deepStrictEqual(M.groupByModel('x'), []);
    assert.deepStrictEqual(M.groupByModel([]), []);
});

check('homeModel basic', () => {
    const h = M.homeModel(ST, SN);
    assert.deepStrictEqual(h.chips.ble, { level:'ok', text:'Дозиметр: подключён' });
    assert.deepStrictEqual(h.chips.wifi, { level:'ok', text:'Wi-Fi: подключён' });
    assert.deepStrictEqual(h.chips.mqtt, { level:'ok', text:'MQTT: подключён' });
    assert.deepStrictEqual(h.chips.time, { level:'ok', text:'Время: SNTP' });
    assert.strictEqual(h.fw, '0.1.0-e13');
    assert.strictEqual(h.rate, '0.110');
    assert.ok(!('rateUr' in h));
    assert.strictEqual(h.cps, '0.5');
    assert.strictEqual(h.avgCps, '0.45');
    assert.strictEqual(h.dose, '1.234');
    assert.strictEqual(h.battery, '27');
    assert.strictEqual(h.hasRec, true);
    assert.strictEqual(h.cards.length, 1);
});

check('homeModel ble', () => {
    const st1 = { ...ST, ble_connected: undefined, ble_ready: true };
    assert.deepStrictEqual(M.homeModel(st1, SN).chips.ble, { level:'warn', text:'Дозиметр: ищет прибор' });
    
    const st2 = { ...ST, ble_connected: false, ble_ready: false };
    assert.deepStrictEqual(M.homeModel(st2, SN).chips.ble, { level:'bad', text:'Дозиметр: нет связи' });
    
    assert.deepStrictEqual(M.homeModel(null, SN).chips.ble, { level:'bad', text:'Дозиметр: нет связи' });
});

check('homeModel wifi', () => {
    const st1 = { ...ST, wifi_connected: false };
    assert.deepStrictEqual(M.homeModel(st1, SN).chips.wifi, { level:'bad', text:'Wi-Fi: нет' });
    
    const st2 = { ...ST, wifi_connected: 1 };
    assert.deepStrictEqual(M.homeModel(st2, SN).chips.wifi, { level:'bad', text:'Wi-Fi: нет' });
});

check('homeModel mqtt', () => {
    const st1 = { ...ST, mqtt: undefined };
    assert.deepStrictEqual(M.homeModel(st1, SN).chips.mqtt, { level:'mut', text:'MQTT: нет данных' });
    
    const st2 = { ...ST, mqtt: { enabled: 0, connected: 1 } };
    assert.deepStrictEqual(M.homeModel(st2, SN).chips.mqtt, { level:'mut', text:'MQTT: выключен' });
    
    const st3 = { ...ST, mqtt: { enabled: 1, connected: 0 } };
    assert.deepStrictEqual(M.homeModel(st3, SN).chips.mqtt, { level:'warn', text:'MQTT: не подключён' });
    
    const st4 = { ...ST, mqtt: 5 };
    assert.deepStrictEqual(M.homeModel(st4, SN).chips.mqtt, { level:'mut', text:'MQTT: нет данных' });
});

check('homeModel time', () => {
    const st1 = { ...ST, sntp_synced: false, time_source: 'manual' };
    assert.deepStrictEqual(M.homeModel(st1, SN).chips.time, { level:'warn', text:'Время: manual' });
    
    const st2 = { ...ST, sntp_synced: false, time_source: 'none' };
    assert.deepStrictEqual(M.homeModel(st2, SN).chips.time, { level:'bad', text:'Время: не задано' });
    
    const st3 = { ...ST, sntp_synced: false, time_source: '' };
    assert.deepStrictEqual(M.homeModel(st3, SN).chips.time, { level:'bad', text:'Время: не задано' });
    
    assert.deepStrictEqual(M.homeModel(null, SN).chips.time, { level:'bad', text:'Время: не задано' });
});

check('homeModel no live', () => {
    const sn1 = { sensor: SN.sensor };
    const h1 = M.homeModel(ST, sn1);
    assert.strictEqual(h1.rate, DASH);
    assert.ok(!('rateUr' in h1));
    assert.strictEqual(h1.cps, DASH);
    assert.strictEqual(h1.avgCps, DASH);
    assert.strictEqual(h1.dose, DASH);
    assert.strictEqual(h1.hasRec, false);
    
    const sn2 = { sensor: SN.sensor, live: { has_rec: false } };
    const h2 = M.homeModel(ST, sn2);
    assert.strictEqual(h2.rate, DASH);
    assert.strictEqual(h2.hasRec, false);
    
    const h3 = M.homeModel(ST, null);
    assert.strictEqual(h3.rate, DASH);
    assert.deepStrictEqual(h3.cards, []);
});

check('homeModel battery', () => {
    const st1 = { ...ST, ble_last_rec: undefined };
    assert.strictEqual(M.homeModel(st1, SN).battery, DASH);
    
    const st2 = { ...ST, ble_last_rec: { battery: 26.6 } };
    assert.strictEqual(M.homeModel(st2, SN).battery, '27');
    
    const st3 = { ...ST, ble_last_rec: { battery: 'x' } };
    assert.strictEqual(M.homeModel(st3, SN).battery, DASH);
    
    const st4 = { ...ST, ble_last_rec: null };
    assert.strictEqual(M.homeModel(st4, SN).battery, DASH);
});

check('homeModel fw', () => {
    assert.strictEqual(M.homeModel(null, SN).fw, DASH);
    
    const st1 = { ...ST, fw_version: 123 };
    assert.strictEqual(M.homeModel(st1, SN).fw, DASH);
});

check('homeModel time sntp strict', () => {
    const h = M.homeModel({ ...ST, sntp_synced: 1, time_source: 'manual' }, SN);
    assert.deepStrictEqual(h.chips.time, { level:'warn', text:'Время: manual' });
});

check('sensorCard real sensors[]: модель, номер, off/err, enabled', () => {
    const three = JSON.parse(JSON.stringify(require('./fixtures/sensors-three.json')));
    const cards = M.sensorCards(three);
    assert.strictEqual(cards.length, three.sensors.length);
    cards.forEach((c, i) => {
        assert.strictEqual(c.number, three.sensors[i].sensor);
        assert.ok(/^[A-Z]/.test(c.model), c.model);
    });
    const off = M.sensorCard({ sensor: 4, model: 'sht3x', status: 'off', enabled: false }, 0);
    assert.strictEqual(off.statusText, 'выключен');
    assert.strictEqual(off.enabled, false);
    assert.strictEqual(off.number, 4);
    assert.strictEqual(off.model, 'SHT3x');
    assert.strictEqual(off.values.length, 0);
    const err = M.sensorCard({ sensor: 2, model: 'aht2x', status: 'err', enabled: true }, 0);
    assert.strictEqual(err.statusText, 'ошибка');
    assert.strictEqual(err.level, 'warn');
    assert.strictEqual(err.enabled, true);
    assert.strictEqual(M.sensorCard({ status: 'ok' }, 0).enabled, true);
    assert.strictEqual(M.sensorCard({ status: 'off' }, 0).enabled, false);
    assert.strictEqual(M.sensorCard({ sensor: 9, status: 'ok' }, 0).number, 1);
});

check('homeModel: живой ответ с live.rate_urh не даёт ни одной величины с «мкР»', () => {
    const h = M.homeModel(ST, SN);
    assert.strictEqual(SN.live.rate_urh, 11.0);
    assert.strictEqual(JSON.stringify(h).indexOf('мкР'), -1);
    assert.ok(Object.keys(h).every(k => !/ur/i.test(k.replace('sensorCards', ''))), Object.keys(h).join());
});

check('diskUsage: (0, 0) и нет поля -> н/д', () => {
    assert.deepStrictEqual(M.diskUsage(0, 0), { pct: null, text: 'н/д', level: 'ok' });
    assert.strictEqual(M.diskUsage(5, 0).text, 'н/д');
    assert.strictEqual(M.diskUsage(undefined, 100).text, 'н/д');
    assert.strictEqual(M.diskUsage(5, undefined).text, 'н/д');
    assert.strictEqual(M.diskUsage(5, null).text, 'н/д');
    assert.strictEqual(M.diskUsage('5', 100).text, 'н/д');
});

check('diskUsage: боевой раздел storage 0xBE0000 = 12451840 Б, 1048576 Б -> 8 %', () => {
    const d = M.diskUsage(1048576, 12451840);
    assert.deepStrictEqual(d, { pct: 8, text: '8 %', level: 'ok' });
    assert.strictEqual(M.diskUsage(0, 12451840).text, '0 %');
});

check('diskUsage: округление к ближайшему целому (8.42 -> 8, 8.5 -> 9, 8.49 -> 8)', () => {
    assert.strictEqual(M.diskUsage(1048576, 12451840).pct, 8);
    assert.strictEqual(M.diskUsage(85, 1000).pct, 9);
    assert.strictEqual(M.diskUsage(84.9, 1000).pct, 8);
    assert.strictEqual(M.diskUsage(1, 3).pct, 33);
    assert.strictEqual(M.diskUsage(2, 3).pct, 67);
});

check('diskUsage: used > total -> 100 %, пороги цвета 80 % и 95 %', () => {
    assert.deepStrictEqual(M.diskUsage(13000000, 12451840), { pct: 100, text: '100 %', level: 'crit' });
    assert.strictEqual(M.diskUsage(79, 100).level, 'ok');
    assert.strictEqual(M.diskUsage(80, 100).level, 'warn');
    assert.strictEqual(M.diskUsage(94, 100).level, 'warn');
    assert.strictEqual(M.diskUsage(95, 100).level, 'crit');
    assert.strictEqual(M.diskUsage(-5, 100).pct, 0);
});

console.log('TESTS: ' + passed + '/' + total);
process.exit(passed === total ? 0 : 1);
