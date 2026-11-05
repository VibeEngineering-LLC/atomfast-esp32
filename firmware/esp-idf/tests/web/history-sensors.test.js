const assert = require('assert');
const path = require('path');
const N = require(process.env.HN_MODULE || path.join(__dirname, '..', '..', 'web', 'history-sensors.js'));
let passed = 0;
let total = 0;
function check(name, fn) {
    total++;
    try { fn(); passed++; console.log('ok  ' + name); }
    catch (e) { console.log('FAIL ' + name + ': ' + e.message); }
}

const DASH = '—';

const J = {
    "board":"ESP32-S3-DevKitC-1",
    "i2c":{"sda":8,"scl":9,"scan_done":1,"scan":["0x76"]},
    "sensor":{"status":"ok","model":"BMP280","addr":"0x76","chip_id":"0x58","caps":3,"temp":true,"press":true,"hum":false,"t_c":26.51,"p_mmhg":751.7,"t_raw_c":26.51,"p_raw_mmhg":751.7,"n_ok":12,"n_err":0,"store_ok":12,"store_fail":0,"last_ok_s_ago":14,"last_ok_ts":1761300000,"err":"","rec_rc":0},
    "cfg":{"enabled":1,"t_off":0.0,"p_off":0.0,"agg_s":5,"period_s":30,"nvs_ok":1},
    "live":{"ble":true,"has_rec":true,"cps":0.5,"rate_usvh":0.11,"rate_urh":11.0,"pulses_2s":1,"dose_usv":1.234,"avg_cps":0.45,"avg_rate_usvh":0.105,"avg_n":3,"avg_age_s":2}
};

function clone() {
    return JSON.parse(JSON.stringify(J));
}

check('fmt', () => {
    assert.strictEqual(N.fmt(26.514, 1), '26.5');
    assert.strictEqual(N.fmt(751.7, 1), '751.7');
    assert.strictEqual(N.fmt(0, 2), '0.00');
    assert.strictEqual(N.fmt(-0.04, 1), '0.0');
    assert.strictEqual(N.fmt(-0.5, 1), '-0.5');
    assert.strictEqual(N.fmt(null, 1), DASH);
    assert.strictEqual(N.fmt(undefined, 1), DASH);
    assert.strictEqual(N.fmt(NaN, 1), DASH);
    assert.strictEqual(N.fmt(Infinity, 1), DASH);
    assert.strictEqual(N.fmt('5', 1), DASH);
    assert.strictEqual(N.fmt(3, 0), '3');
});

check('readings: боевой ответ', () => {
    const r = N.readings(clone());
    assert.strictEqual(r.length, 9);
    const ids = r.map(x => x.id);
    assert.deepStrictEqual(ids, ['cps', 'rate', 'pulses', 'dose', 'avg_cps', 'avg_rate', 'air_t', 'air_p', 'battery']);
    
    const vals = r.map(x => x.value);
    assert.deepStrictEqual(vals, ['0.50', '0.1100', '1', '1.234', '0.450', '0.1050', '26.5', '751.7', '—']);
    
    const units = r.map(x => x.unit);
    assert.deepStrictEqual(units, ['имп/с', 'мкЗв/ч', 'имп', 'мкЗв', 'имп/с', 'мкЗв/ч', '°C', 'мм рт. ст.', '%']);
    
    assert.strictEqual(r[6].label, 'Температура среды');
    assert.strictEqual(r[7].label, 'Давление среды');
    assert.strictEqual(r[4].label, 'CPS (среднее за окно)');
});

check('readings: влажности нет у BMP280', () => {
    const r = N.readings(clone());
    const ids = r.map(x => x.id);
    assert.ok(!ids.includes('air_h'));
});

check('readings: влажность только при hum=true', () => {
    // hum=true, h_pct=48.26
    let j = clone();
    j.sensor.hum = true;
    j.sensor.h_pct = 48.26;
    let r = N.readings(j);
    assert.strictEqual(r.length, 10);
    assert.strictEqual(r[8].id, 'air_h');
    assert.strictEqual(r[8].label, 'Влажность среды');
    assert.strictEqual(r[8].value, '48.3');
    assert.strictEqual(r[8].unit, '%');

    // hum=true, h_pct=null
    j = clone();
    j.sensor.hum = true;
    j.sensor.h_pct = null;
    r = N.readings(j);
    assert.strictEqual(r.length, 10);
    assert.strictEqual(r[8].id, 'air_h');
    assert.strictEqual(r[8].value, DASH);

    // hum='true' (строка)
    j = clone();
    j.sensor.hum = 'true';
    r = N.readings(j);
    const ids = r.map(x => x.id);
    assert.ok(!ids.includes('air_h'));
});

check('readings: воздух не показывается, если датчик не ok', () => {
    const statuses = ['no_sensor', 'bus_error', 'disabled', 'read_error', 'unsupported', 'init'];
    statuses.forEach(st => {
        const j = clone();
        j.sensor.status = st;
        // t_c и p_mmhg остаются числами
        const r = N.readings(j);
        const airT = r.find(x => x.id === 'air_t');
        const airP = r.find(x => x.id === 'air_p');
        assert.strictEqual(airT.value, DASH);
        assert.strictEqual(airP.value, DASH);
        
        // hum=true при status != ok - элемента air_h нет
        j.sensor.hum = true;
        j.sensor.h_pct = 50;
        const r2 = N.readings(j);
        const ids = r2.map(x => x.id);
        assert.ok(!ids.includes('air_h'));
    });
});

check('readings: caps', () => {
    // temp=false
    let j = clone();
    j.sensor.temp = false;
    let r = N.readings(j);
    assert.strictEqual(r.find(x => x.id === 'air_t').value, DASH);
    assert.strictEqual(r.find(x => x.id === 'air_p').value, '751.7');

    // press=false
    j = clone();
    j.sensor.press = false;
    r = N.readings(j);
    assert.strictEqual(r.find(x => x.id === 'air_p').value, DASH);
    assert.strictEqual(r.find(x => x.id === 'air_t').value, '26.5');
});

check('readings: нет записи BLE', () => {
    // has_rec=false
    let j = clone();
    j.live.has_rec = false;
    let r = N.readings(j);
    assert.strictEqual(r.find(x => x.id === 'pulses').value, DASH);
    // остальные live-значения показываются
    assert.strictEqual(r.find(x => x.id === 'cps').value, '0.50');
    assert.strictEqual(r.find(x => x.id === 'rate').value, '0.1100');
    assert.strictEqual(r.find(x => x.id === 'rate_ur'), undefined);
    assert.strictEqual(r.find(x => x.id === 'dose').value, '1.234');
    assert.strictEqual(r.find(x => x.id === 'avg_cps').value, '0.450');
    assert.strictEqual(r.find(x => x.id === 'avg_rate').value, '0.1050');

    // cps=null
    j = clone();
    j.live.cps = null;
    r = N.readings(j);
    assert.strictEqual(r.find(x => x.id === 'cps').value, DASH);

    // avg_cps=null, avg_rate_usvh=null
    j = clone();
    j.live.avg_cps = null;
    j.live.avg_rate_usvh = null;
    r = N.readings(j);
    assert.strictEqual(r.find(x => x.id === 'avg_cps').value, DASH);
    assert.strictEqual(r.find(x => x.id === 'avg_rate').value, DASH);
});

check('readings: пустые входы', () => {
    const inputs = [null, undefined, {}, 'x'];
    inputs.forEach(inp => {
        const r = N.readings(inp);
        assert.strictEqual(r.length, 9);
        r.forEach(item => {
            assert.strictEqual(item.value, DASH);
        });
    });

    // {live:{cps:1}}
    const r = N.readings({live:{cps:1}});
    assert.strictEqual(r.length, 9);
    assert.strictEqual(r.find(x => x.id === 'cps').value, '1.00');
    assert.strictEqual(r.find(x => x.id === 'air_t').value, DASH);
    assert.strictEqual(r.find(x => x.id === 'air_p').value, DASH);
});

check('sensorLine', () => {
    // боевой
    let j = clone();
    assert.strictEqual(N.sensorLine(j), 'Датчик: BMP280 (0x76), измерено 14 с назад');

    // last_ok_s_ago=-1
    j = clone();
    j.sensor.last_ok_s_ago = -1;
    assert.strictEqual(N.sensorLine(j), 'Датчик: BMP280 (0x76), ожидание первого измерения');

    // last_ok_s_ago=null
    j = clone();
    j.sensor.last_ok_s_ago = null;
    assert.strictEqual(N.sensorLine(j), 'Датчик: BMP280 (0x76), ожидание первого измерения');

    // last_ok_s_ago=14.6
    j = clone();
    j.sensor.last_ok_s_ago = 14.6;
    assert.strictEqual(N.sensorLine(j), 'Датчик: BMP280 (0x76), измерено 15 с назад');

    // no_sensor
    j = clone();
    j.sensor.status = 'no_sensor';
    assert.strictEqual(N.sensorLine(j), 'Датчик среды не найден (проверьте подключение I2C)');

    // bus_error
    j = clone();
    j.sensor.status = 'bus_error';
    assert.strictEqual(N.sensorLine(j), 'Ошибка шины I2C');

    // disabled
    j = clone();
    j.sensor.status = 'disabled';
    assert.strictEqual(N.sensorLine(j), 'Датчик выключен в настройках');

    // read_error с err 'read'
    j = clone();
    j.sensor.status = 'read_error';
    j.sensor.err = 'read';
    assert.strictEqual(N.sensorLine(j), 'Датчик найден, но не отвечает (read)');

    // read_error с err ''
    j = clone();
    j.sensor.status = 'read_error';
    j.sensor.err = '';
    assert.strictEqual(N.sensorLine(j), 'Датчик найден, но не отвечает');

    // unsupported, model 'BME280'
    j = clone();
    j.sensor.status = 'unsupported';
    j.sensor.model = 'BME280';
    assert.strictEqual(N.sensorLine(j), 'Датчик BME280 пока не поддерживается');

    // init
    j = clone();
    j.sensor.status = 'init';
    assert.strictEqual(N.sensorLine(j), 'Датчик опрашивается…');

    // status 'xyz'
    j = clone();
    j.sensor.status = 'xyz';
    assert.strictEqual(N.sensorLine(j), 'Состояние датчика неизвестно');

    // null и {}
    assert.strictEqual(N.sensorLine(null), 'Нет данных от платы');
    assert.strictEqual(N.sensorLine({}), 'Нет данных от платы');
});

check('validateCfg: годные значения', () => {
    // строки
    let res = N.validateCfg({enabled:true, t_off:'1,5', p_off:'-2.25', agg_s:'5', period_s:'30'});
    assert.strictEqual(res.ok, true);
    assert.strictEqual(res.body.enabled, true);
    assert.strictEqual(res.body.t_off, 1.5);
    assert.strictEqual(res.body.p_off, Math.round(-2.25*10)/10); // -2.2
    assert.strictEqual(res.body.agg_s, 5);
    assert.strictEqual(res.body.period_s, 30);

    // числа
    res = N.validateCfg({enabled:false, t_off:0, p_off:0, agg_s:1, period_s:5});
    assert.strictEqual(res.ok, true);
    assert.strictEqual(res.body.enabled, false);

    // границы включительно
    const bounds = [
        {t_off: -5}, {t_off: 5},
        {p_off: -20}, {p_off: 20},
        {agg_s: 1}, {agg_s: 60},
        {period_s: 5}, {period_s: 600}
    ];
    bounds.forEach(b => {
        const input = {enabled:true, t_off:0, p_off:0, agg_s:5, period_s:30};
        Object.assign(input, b);
        res = N.validateCfg(input);
        assert.strictEqual(res.ok, true, 'bound ' + JSON.stringify(b));
    });

    // enabled варианты
    assert.strictEqual(N.validateCfg({enabled:'true', t_off:0, p_off:0, agg_s:5, period_s:30}).body.enabled, true);
    assert.strictEqual(N.validateCfg({enabled:1, t_off:0, p_off:0, agg_s:5, period_s:30}).body.enabled, true);
    assert.strictEqual(N.validateCfg({enabled:'false', t_off:0, p_off:0, agg_s:5, period_s:30}).body.enabled, false);
    assert.strictEqual(N.validateCfg({enabled:0, t_off:0, p_off:0, agg_s:5, period_s:30}).body.enabled, false);
    assert.strictEqual(N.validateCfg({enabled:undefined, t_off:0, p_off:0, agg_s:5, period_s:30}).body.enabled, false);
    assert.strictEqual(N.validateCfg({enabled:'yes', t_off:0, p_off:0, agg_s:5, period_s:30}).body.enabled, false);
});

check('validateCfg: ошибки по полям', () => {
    const base = {enabled:true, t_off:0, p_off:0, agg_s:5, period_s:30};
    
    let res = N.validateCfg({...base, t_off: 5.01});
    assert.strictEqual(res.ok, false);
    assert.strictEqual(res.field, 't_off');
    assert.strictEqual(res.error, 'поправка температуры: от -5 до 5 °C');

    res = N.validateCfg({...base, t_off: -5.01});
    assert.strictEqual(res.ok, false);
    assert.strictEqual(res.field, 't_off');
    assert.strictEqual(res.error, 'поправка температуры: от -5 до 5 °C');

    res = N.validateCfg({...base, p_off: 20.01});
    assert.strictEqual(res.ok, false);
    assert.strictEqual(res.field, 'p_off');
    assert.strictEqual(res.error, 'поправка давления: от -20 до 20 мм рт. ст.');

    res = N.validateCfg({...base, p_off: -20.5});
    assert.strictEqual(res.ok, false);
    assert.strictEqual(res.field, 'p_off');
    assert.strictEqual(res.error, 'поправка давления: от -20 до 20 мм рт. ст.');

    res = N.validateCfg({...base, agg_s: 0});
    assert.strictEqual(res.ok, false);
    assert.strictEqual(res.field, 'agg_s');
    assert.strictEqual(res.error, 'интервал агрегации: целое от 1 до 60 с');

    res = N.validateCfg({...base, agg_s: 61});
    assert.strictEqual(res.ok, false);
    assert.strictEqual(res.field, 'agg_s');
    assert.strictEqual(res.error, 'интервал агрегации: целое от 1 до 60 с');

    res = N.validateCfg({...base, agg_s: 2.5});
    assert.strictEqual(res.ok, false);
    assert.strictEqual(res.field, 'agg_s');
    assert.strictEqual(res.error, 'интервал агрегации: целое от 1 до 60 с');

    res = N.validateCfg({...base, period_s: 4});
    assert.strictEqual(res.ok, false);
    assert.strictEqual(res.field, 'period_s');
    assert.strictEqual(res.error, 'период измерения: целое от 5 до 600 с');

    res = N.validateCfg({...base, period_s: 601});
    assert.strictEqual(res.ok, false);
    assert.strictEqual(res.field, 'period_s');
    assert.strictEqual(res.error, 'период измерения: целое от 5 до 600 с');

    res = N.validateCfg({...base, period_s: 10.5});
    assert.strictEqual(res.ok, false);
    assert.strictEqual(res.field, 'period_s');
    assert.strictEqual(res.error, 'период измерения: целое от 5 до 600 с');
});

check('validateCfg: нечисловой ввод', () => {
    const badVals = ['', '   ', 'abc', '12abc', NaN, Infinity, undefined, null, '1e999'];
    const fields = ['t_off', 'p_off', 'agg_s', 'period_s'];
    
    fields.forEach(f => {
        badVals.forEach(v => {
            const input = {enabled:true, t_off:0, p_off:0, agg_s:5, period_s:30};
            input[f] = v;
            const res = N.validateCfg(input);
            assert.strictEqual(res.ok, false, `field ${f}, val ${v}`);
            assert.strictEqual(res.field, f, `field ${f}, val ${v}`);
            assert.strictEqual(res.error, 'введите число', `field ${f}, val ${v}`);
        });
    });

    // Порядок проверки полей
    let res = N.validateCfg({enabled:true, t_off:'x', p_off:0, agg_s:0, period_s:30});
    assert.strictEqual(res.field, 't_off');

    res = N.validateCfg({enabled:true, t_off:0, p_off:99, agg_s:0, period_s:30});
    assert.strictEqual(res.field, 'p_off');

    res = N.validateCfg({enabled:true, t_off:0, p_off:0, agg_s:0, period_s:1});
    assert.strictEqual(res.field, 'agg_s');
});

check('validateCfg: не объект', () => {
    const inputs = [null, undefined, 'x', 5];
    inputs.forEach(inp => {
        const res = N.validateCfg(inp);
        assert.strictEqual(res.ok, false);
        assert.strictEqual(res.field, '');
        assert.strictEqual(res.error, 'нет данных');
    });
});

check('cfgToForm', () => {
    // боевой
    let j = clone();
    let res = N.cfgToForm(j);
    assert.deepStrictEqual(res, {enabled:true, t_off:0, p_off:0, agg_s:5, period_s:30});

    // cfg.enabled=0
    j = clone();
    j.cfg.enabled = 0;
    res = N.cfgToForm(j);
    assert.strictEqual(res.enabled, false);

    // без cfg
    j = clone();
    delete j.cfg;
    res = N.cfgToForm(j);
    assert.strictEqual(res, null);

    // null, undefined, 'x'
    assert.strictEqual(N.cfgToForm(null), null);
    assert.strictEqual(N.cfgToForm(undefined), null);
    assert.strictEqual(N.cfgToForm('x'), null);
});

// долг #SA-3 (выжившие мутанты): нулевой возраст измерения, округление отрицательной поправки температуры
check('sensorLine: возраст 0 с и округление поправок', () => {
    const j = JSON.parse(JSON.stringify(J));
    j.sensor.last_ok_s_ago = 0;
    assert.strictEqual(N.sensorLine(j), 'Датчик: BMP280 (0x76), измерено 0 с назад');
    const v = N.validateCfg({ enabled: true, t_off: '-1.25', p_off: 0, agg_s: 5, period_s: 30 });
    assert.strictEqual(v.ok, true);
    assert.strictEqual(v.body.t_off, Math.round(-12.5) / 10);
});
console.log('TESTS: ' + passed + '/' + total);
if (passed < total) {
    process.exit(1);
}

check('readings: аккумулятор из /api/status', () => {
    const st = (b) => ({ ble_last_rec: { battery: b } });
    const get = (r) => r.find(x => x.id === 'battery');
    assert.strictEqual(get(N.readings(clone(), st(27))).value, '27');
    assert.strictEqual(get(N.readings(clone(), st(26.6))).value, '27');
    assert.strictEqual(get(N.readings(clone(), st(0))).value, '0');
    assert.strictEqual(get(N.readings(clone(), { ble_last_rec: null })).value, '—');
    assert.strictEqual(get(N.readings(clone(), { ble_last_rec: {} })).value, '—');
    assert.strictEqual(get(N.readings(clone(), { ble_last_rec: { battery: '27' } })).value, '—');
    assert.strictEqual(get(N.readings(clone())).value, '—');
    assert.strictEqual(get(N.readings(clone(), st(NaN))).value, '—');
    assert.strictEqual(get(N.readings(clone(), st(Infinity))).value, '—');
    const b = get(N.readings(clone(), st(27)));
    assert.strictEqual(b.unit, '%');
    assert.ok(b.label.indexOf('аккумулятор') >= 0);
});

check('readings: живой ответ с live.rate_urh не даёт ни одной величины с «мкР»', () => {
    const r = N.readings(clone());
    assert.strictEqual(clone().live.rate_urh, 11.0);
    assert.strictEqual(JSON.stringify(r).indexOf('мкР'), -1);
    assert.ok(r.every(x => x.id.indexOf('ur') === -1 || x.id === 'pulses'), r.map(x => x.id).join());
});
