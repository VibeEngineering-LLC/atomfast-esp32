const assert = require('assert');
const path = require('path');
const X = require(process.env.HX_MODULE || path.join(__dirname, '..', '..', 'web', 'axes.js'));
let passed = 0;
let total = 0;
function check(name, fn) {
    total++;
    try { fn(); passed++; console.log('ok  ' + name); }
    catch (e) { console.log('FAIL ' + name + ': ' + e.message); }
}
function near(a, b, eps) { assert.ok(Math.abs(a - b) <= (eps || 1e-9), a + ' != ' + b); }

const DEFS = [
    {id:'rate',field:'doserate_usvh',unit:'мкЗв/ч',color:'--s1',group:'dose'},
    {id:'dose',field:'dose_msv',unit:'мЗв',color:'--s4',group:'dose'},
    {id:'cps',field:'cps',unit:'имп/с',color:'--s2',group:'count'},
    {id:'pulses',field:'pulses_2s',unit:'имп/2 с',color:'--s3',group:'count'},
    {id:'temp',field:'temp_c',unit:'°C',color:'--s5',group:'temp'},
    {id:'air_t',field:'air_t',unit:'°C',color:'--s6',group:'temp'},
    {id:'air_p',field:'air_p',unit:'мм рт. ст.',color:'--s7',group:'press'},
    {id:'air_h',field:'air_h',unit:'%',color:'--s1',group:'hum'}
];

// 1
check('group_ids', () => {
    assert.deepStrictEqual(X.GROUP_IDS, ['dose','count','temp','press','hum']);
    assert.strictEqual(X.GROUPS.length, 5);
    assert.strictEqual(X.GROUPS[0].zeroBase, false);
    assert.strictEqual(X.GROUPS[1].zeroBase, false);
    assert.strictEqual(X.GROUPS[2].zeroBase, false);
    assert.strictEqual(X.GROUPS[3].zeroBase, false);
    assert.strictEqual(X.GROUPS[4].zeroBase, false);
    assert.strictEqual(X.isGroup('temp'), true);
    assert.strictEqual(X.isGroup('x'), false);
    assert.strictEqual(X.isGroup(5), false);
    assert.strictEqual(X.isGroup(null), false);
});

// 2
check('group_of', () => {
    assert.strictEqual(X.groupOf({id:'cps',group:'count'}, null), 'count');
    assert.strictEqual(X.groupOf({id:'cps',group:'count'}, {cps:'press'}), 'count');
    assert.strictEqual(X.groupOf({id:'cps',group:'count'}, {cps:'zzz'}), 'count');
    assert.strictEqual(X.groupOf({id:'cps'}, null), 'count');
    assert.strictEqual(X.groupOf({id:'cps',group:'bad'}, null), 'count');
    assert.strictEqual(X.groupOf({id:'cps',group:'count'}, 'string'), 'count');
});

// 3
check('build_axes_2', () => {
    const r = X.buildAxes(DEFS, {rate:true, cps:true});
    assert.strictEqual(r.axes.length, 2);
    assert.deepStrictEqual(r.axes[0], { id:'dose', side:'L', slot:0, series:['rate'], fields:['doserate_usvh'], units:['мкЗв/ч'], colors:['--s1'], groups:['dose'], zeroBase:false });
    assert.strictEqual(r.axes[1].id, 'count');
    assert.strictEqual(r.axes[1].side, 'R');
    assert.strictEqual(r.axes[1].slot, 0);
    assert.deepStrictEqual(r.byGroup, { dose:0, count:1 });
});

// 4
check('build_axes_3', () => {
    const r = X.buildAxes(DEFS, {cps:true, air_p:true, air_t:true});
    assert.deepStrictEqual(r.axes.map(a => a.id), ['count','temp','press']);
    assert.strictEqual(r.axes[0].side, 'L'); assert.strictEqual(r.axes[0].slot, 0);
    assert.strictEqual(r.axes[1].side, 'R'); assert.strictEqual(r.axes[1].slot, 0);
    assert.strictEqual(r.axes[2].side, 'L'); assert.strictEqual(r.axes[2].slot, 1);
    assert.strictEqual(r.axes[0].zeroBase, false);
    assert.strictEqual(r.axes[1].zeroBase, false);
    assert.strictEqual(r.axes[2].zeroBase, false);
    assert.deepStrictEqual(r.byGroup, { count:0, temp:1, press:2 });
});

// 5
check('build_axes_temp_merge', () => {
    const r = X.buildAxes(DEFS, {temp:true, air_t:true});
    assert.strictEqual(r.axes.length, 1);
    assert.strictEqual(r.axes[0].id, 'temp');
    assert.deepStrictEqual(r.axes[0].series, ['temp','air_t']);
    assert.deepStrictEqual(r.axes[0].units, ['°C']);
    assert.deepStrictEqual(r.axes[0].colors, ['--s5','--s6']);
});

// 6
check('build_axes_dose_merge', () => {
    const r = X.buildAxes(DEFS, {rate:true, dose:true});
    assert.strictEqual(r.axes.length, 1);
    assert.deepStrictEqual(r.axes[0].units, ['мкЗв/ч','мЗв']);
});

// 7
check('build_axes_4', () => {
    const r = X.buildAxes(DEFS, {rate:true, cps:true, temp:true, air_p:true});
    assert.strictEqual(r.axes.length, 4);
    assert.strictEqual(r.axes[0].side, 'L'); assert.strictEqual(r.axes[0].slot, 0);
    assert.strictEqual(r.axes[1].side, 'R'); assert.strictEqual(r.axes[1].slot, 0);
    assert.strictEqual(r.axes[2].side, 'L'); assert.strictEqual(r.axes[2].slot, 1);
    assert.strictEqual(r.axes[3].side, 'R'); assert.strictEqual(r.axes[3].slot, 1);
});

// 8
check('build_axes_all', () => {
    const vis = {rate:true, dose:true, cps:true, pulses:true, temp:true, air_t:true, air_p:true, air_h:true};
    const r = X.buildAxes(DEFS, vis, null);
    assert.strictEqual(r.axes.length, 4);
    assert.deepStrictEqual(r.axes[3].groups, ['press','hum']);
    assert.ok(r.axes[3].series.includes('air_p'));
    assert.ok(r.axes[3].series.includes('air_h'));
    assert.strictEqual(r.axes[3].zeroBase, false);
    assert.deepStrictEqual(r.byGroup, { dose:0, count:1, temp:2, press:3, hum:3 });
});

// 9
check('build_axes_empty', () => {
    let r = X.buildAxes(DEFS, {});
    assert.deepStrictEqual(r, { axes: [], byGroup: {} });
    r = X.buildAxes(DEFS, {rate:false});
    assert.deepStrictEqual(r, { axes: [], byGroup: {} });
    r = X.buildAxes(DEFS, {rate:'yes'});
    assert.deepStrictEqual(r, { axes: [], byGroup: {} });
});

// 12
check('auto_range_basic', () => {
    const r = X.autoRange([{a:10},{a:20}], ['a'], false);
    near(r.min, 9, 1e-9);
    near(r.max, 21, 1e-9);
});

// 13
check('auto_range_no_forced_zero', () => {
    const r = X.autoRange([{a:10},{a:20}], ['a'], false);
    assert.ok(r.min > 0);
});

// 13b
check('auto_range_include_zero', () => {
    let r = X.autoRange([{a:10},{a:20}], ['a'], true);
    near(r.min, 0, 1e-9);
    near(r.max, 21, 1e-9);
    r = X.autoRange([{a:-5},{a:-1}], ['a'], true);
    near(r.min, -5.4, 1e-9);
    near(r.max, 0, 1e-9);
});

// 14
check('auto_range_neg', () => {
    const r = X.autoRange([{a:-5},{a:5}], ['a'], true);
    near(r.min, -6, 1e-9);
    near(r.max, 6, 1e-9);
});

// 15
check('auto_range_pressure', () => {
    const r = X.autoRange([{p:758.0},{p:758.6}], ['p'], false);
    near(r.min, 757.94, 1e-6);
    near(r.max, 758.66, 1e-6);
    assert.ok(r.min > 700);
});

// 15b
check('auto_range_repro_cps', () => {
    const rows = [{cps:14.9},{cps:17.2},{cps:19.8},{cps:16.1}];
    const r = X.autoRange(rows, ['cps'], false);
    near(r.min, 14.41, 1e-9);
    near(r.max, 20.29, 1e-9);
    assert.ok(r.min > 14);
});

// 16
check('auto_range_flat', () => {
    let r = X.autoRange([{a:758},{a:758}], ['a'], false);
    near(r.min, 742.84, 1e-9);
    near(r.max, 773.16, 1e-9);
    r = X.autoRange([{a:0},{a:0}], ['a'], false);
    near(r.min, -1, 1e-9);
    near(r.max, 1, 1e-9);
    r = X.autoRange([{a:100},{a:100.005}], ['a'], false);
    near(r.min, 98.0024, 1e-9);
    near(r.max, 102.0026, 1e-9);
});

// 17
check('auto_range_multi', () => {
    const r = X.autoRange([{a:1,b:'x'},{a:NaN,b:3}], ['a','b'], false);
    near(r.min, 0.8, 1e-9);
    near(r.max, 3.2, 1e-9);
});

// 18
check('auto_range_null', () => {
    assert.strictEqual(X.autoRange([], ['a'], false), null);
    assert.strictEqual(X.autoRange([{a:'x'}], ['a'], false), null);
    assert.strictEqual(X.autoRange([{a:1}], [], false), null);
});

// 19
check('auto_range_separate', () => {
    const rows = [{cps:17,p:757.9},{cps:19,p:758.4}];
    const r1 = X.autoRange(rows, ['cps'], false);
    assert.ok(r1.min > 16 && r1.max < 20);
    const r2 = X.autoRange(rows, ['p'], false);
    assert.ok(r2.min > 757 && r2.max < 759);
});

// 19b
check('zero_flag_cfg', () => {
    let c = X.setZero({}, 'count', true);
    assert.deepStrictEqual(c, { count: { mode: 'auto', zero: true } });
    assert.strictEqual(X.zeroOf(c, 'count'), true);
    assert.strictEqual(X.zeroOf(c, 'temp'), false);
    assert.strictEqual(X.zeroOf(null, 'count'), false);
    c = X.setManual(c, 'count', 1, 5);
    assert.deepStrictEqual(c, { count: { mode: 'manual', min: 1, max: 5, zero: true } });
    c = X.setAuto(c, 'count');
    assert.deepStrictEqual(c, { count: { mode: 'auto', zero: true } });
    c = X.setZero(c, 'count', false);
    assert.deepStrictEqual(c, { count: { mode: 'auto' } });
    assert.deepStrictEqual(X.setZero({}, 'zzz', true), {});
});

// 19c
check('zero_flag_roundtrip', () => {
    const src = { count: { mode: 'auto', zero: true }, temp: { mode: 'manual', min: 1, max: 2, zero: true }, press: { mode: 'auto', zero: 'yes' } };
    const r = X.restore(X.serialize(src));
    assert.deepStrictEqual(r.cfg.count, { mode: 'auto', zero: true });
    assert.deepStrictEqual(r.cfg.temp, { mode: 'manual', min: 1, max: 2, zero: true });
    assert.deepStrictEqual(r.cfg.press, { mode: 'auto' });
});
// 20
check('nice_ticks_basic', () => {
    assert.deepStrictEqual(X.niceTicks(0, 10, 5), [0,2,4,6,8,10]);
    assert.deepStrictEqual(X.niceTicks(0, 1, 5), [0,0.2,0.4,0.6,0.8,1]);
    const t = X.niceTicks(757.95, 758.65, 5);
    assert.ok(t.length >= 3 && t.length <= 8);
    for (let i = 0; i < t.length; i++) {
        assert.ok(t[i] >= 757.95 && t[i] <= 758.65);
    }
    for (let i = 1; i < t.length; i++) {
        near(t[i] - t[i-1], t[1] - t[0], 1e-9);
    }
});

// 21
check('nice_ticks_edge', () => {
    assert.deepStrictEqual(X.niceTicks(5, 5, 5), []);
    assert.deepStrictEqual(X.niceTicks(10, 0, 5), []);
    assert.deepStrictEqual(X.niceTicks(NaN, 1, 5), []);
    const t = X.niceTicks(0, 100, 0);
    assert.ok(t.length > 0);
    assert.strictEqual(t[0], 0);
    assert.ok(t[t.length-1] <= 100);
});

// 22
check('nice_ticks_sym', () => {
    const t = X.niceTicks(-1, 1, 4);
    assert.ok(t.includes(0));
    near(t[0] + t[t.length-1], 0, 1e-9);
});

// 23
check('validate_ok', () => {
    let r = X.validateRange('0', '10');
    assert.deepStrictEqual(r, { ok:true, min:0, max:10, error:'' });
    r = X.validateRange(' 1,5 ', '2,5');
    assert.strictEqual(r.ok, true);
    near(r.min, 1.5, 1e-9);
    near(r.max, 2.5, 1e-9);
    r = X.validateRange(-3, 4);
    assert.strictEqual(r.ok, true);
    r = X.validateRange('1e2','2e2');
    assert.strictEqual(r.ok, true);
    near(r.min, 100, 1e-9);
    near(r.max, 200, 1e-9);
    r = X.validateRange('.5','1.');
    assert.strictEqual(r.ok, true);
    near(r.min, 0.5, 1e-9);
    near(r.max, 1, 1e-9);
});

// 24
check('validate_err_num', () => {
    let r = X.validateRange('a','5');
    assert.deepStrictEqual(r, { ok:false, min:null, max:null, error:'введите числа' });
    r = X.validateRange('', '5');
    assert.deepStrictEqual(r, { ok:false, min:null, max:null, error:'введите числа' });
    r = X.validateRange('1','');
    assert.deepStrictEqual(r, { ok:false, min:null, max:null, error:'введите числа' });
    r = X.validateRange(null, 5);
    assert.deepStrictEqual(r, { ok:false, min:null, max:null, error:'введите числа' });
    r = X.validateRange('1,2,3', '5');
    assert.deepStrictEqual(r, { ok:false, min:null, max:null, error:'введите числа' });
    r = X.validateRange('Infinity','5');
    assert.deepStrictEqual(r, { ok:false, min:null, max:null, error:'введите числа' });
});

// 25
check('validate_err_order', () => {
    let r = X.validateRange('5','5');
    assert.deepStrictEqual(r, { ok:false, min:null, max:null, error:'min должен быть меньше max' });
    r = X.validateRange('10','2');
    assert.deepStrictEqual(r, { ok:false, min:null, max:null, error:'min должен быть меньше max' });
    r = X.validateRange('-1','-2');
    assert.deepStrictEqual(r, { ok:false, min:null, max:null, error:'min должен быть меньше max' });
});

// 26
check('resolve_basic', () => {
    let r = X.resolve({mode:'manual',min:1,max:2}, {min:0,max:10});
    assert.deepStrictEqual(r, { min:1, max:2, mode:'manual' });
    r = X.resolve({mode:'auto'}, {min:0,max:10});
    assert.deepStrictEqual(r, { min:0, max:10, mode:'auto' });
    r = X.resolve(undefined, {min:0,max:10});
    assert.deepStrictEqual(r, { min:0, max:10, mode:'auto' });
    assert.strictEqual(X.resolve(null, null), null);
    assert.strictEqual(X.resolve({mode:'auto'}, null), null);
});

// 27
check('resolve_fallback', () => {
    let r = X.resolve({mode:'manual',min:3,max:3}, {min:0,max:10});
    assert.strictEqual(r.mode, 'auto');
    r = X.resolve({mode:'manual',min:5,max:1}, {min:0,max:10});
    assert.strictEqual(r.mode, 'auto');
    r = X.resolve({mode:'manual',min:'1',max:2}, {min:0,max:10});
    assert.strictEqual(r.mode, 'auto');
    r = X.resolve({mode:'manual',min:NaN,max:2}, {min:0,max:10});
    assert.strictEqual(r.mode, 'auto');
    r = X.resolve({mode:'manual',min:1,max:2}, null);
    assert.deepStrictEqual(r, { min:1, max:2, mode:'manual' });
    r = X.resolve({mode:'manual',min:5,max:1}, null);
    assert.strictEqual(r, null);
});

// 28
check('zoom_basic', () => {
    let r = X.zoomRange(0, 10, 0.5, 0.5);
    assert.deepStrictEqual(r, { min:2.5, max:7.5 });
    r = X.zoomRange(0, 10, 0.5, 0);
    assert.deepStrictEqual(r, { min:0, max:5 });
    r = X.zoomRange(0, 10, 0.5, 1);
    assert.deepStrictEqual(r, { min:5, max:10 });
    r = X.zoomRange(0, 10, 2, 0.5);
    assert.deepStrictEqual(r, { min:-5, max:15 });
});

// 29
check('zoom_anchor_clamp', () => {
    let r1 = X.zoomRange(0, 10, 0.5, -3);
    let r2 = X.zoomRange(0, 10, 0.5, 0);
    assert.deepStrictEqual(r1, r2);
    r1 = X.zoomRange(0, 10, 0.5, 9);
    r2 = X.zoomRange(0, 10, 0.5, 1);
    assert.deepStrictEqual(r1, r2);
});

// 30
check('zoom_invalid', () => {
    let r = X.zoomRange(0, 10, 0, 0.5);
    assert.deepStrictEqual(r, { min:0, max:10 });
    r = X.zoomRange(0, 10, -1, 0.5);
    assert.deepStrictEqual(r, { min:0, max:10 });
    r = X.zoomRange(5, 5, 0.5, 0.5);
    assert.deepStrictEqual(r, { min:5, max:5 });
    r = X.zoomRange(NaN, 10, 0.5, 0.5);
    assert.ok(isNaN(r.min));
    assert.strictEqual(r.max, 10);
});

// 31
check('pan_basic', () => {
    let r = X.panRange(0, 10, 0.1);
    assert.deepStrictEqual(r, { min:1, max:11 });
    r = X.panRange(0, 10, -0.5);
    assert.deepStrictEqual(r, { min:-5, max:5 });
    r = X.panRange(0, 10, NaN);
    assert.deepStrictEqual(r, { min:0, max:10 });
    r = X.panRange('a', 10, 0.1);
    assert.deepStrictEqual(r, { min:'a', max:10 });
});

// 32
check('set_manual_basic', () => {
    const cfg = {};
    const r = X.setManual(cfg, 'temp', 20, 30);
    assert.deepStrictEqual(r, { temp: { mode:'manual', min:20, max:30 } });
    assert.deepStrictEqual(cfg, {});
    const r2 = X.setManual(null, 'temp', 1, 2);
    assert.deepStrictEqual(r2, { temp: { mode:'manual', min:1, max:2 } });
});

// 33
check('set_manual_invalid', () => {
    assert.deepStrictEqual(X.setManual({}, 'temp', 5, 5), {});
    assert.deepStrictEqual(X.setManual({}, 'temp', 6, 5), {});
    assert.deepStrictEqual(X.setManual({}, 'bad', 1, 2), {});
    assert.deepStrictEqual(X.setManual({}, 'temp', 'a', 2), {});
    assert.deepStrictEqual(X.setManual({}, 'temp', 1, NaN), {});
    const cfg = { count:{mode:'auto'} };
    const r = X.setManual(cfg, 'temp', 5, 5);
    assert.deepStrictEqual(r, { count:{mode:'auto'} });
    assert.notStrictEqual(r, cfg);
});

// 34
check('set_auto', () => {
    const cfg = { temp:{mode:'manual',min:1,max:2} };
    const r = X.setAuto(cfg, 'temp');
    assert.deepStrictEqual(r, { temp: { mode:'auto' } });
    assert.deepStrictEqual(cfg, { temp:{mode:'manual',min:1,max:2} });
    assert.deepStrictEqual(X.setAuto({}, 'zzz'), {});
});

// 35
check('serialize_roundtrip', () => {
    const s = X.serialize({ temp:{mode:'manual',min:20,max:30}, count:{mode:'auto'} });
    const r = X.restore(s);
    assert.deepStrictEqual(r, { cfg: { temp:{mode:'manual',min:20,max:30}, count:{mode:'auto'} } });
    const s2 = X.serialize(null, null);
    assert.deepStrictEqual(JSON.parse(s2), { v:1, cfg:{} });
});

// 36
check('restore_garbage', () => {
    assert.deepStrictEqual(X.restore(''), { cfg:{} });
    assert.deepStrictEqual(X.restore('{'), { cfg:{} });
    assert.deepStrictEqual(X.restore('null'), { cfg:{} });
    assert.deepStrictEqual(X.restore('[]'), { cfg:{} });
    assert.deepStrictEqual(X.restore('{"v":2,"cfg":{}}'), { cfg:{} });
    assert.deepStrictEqual(X.restore(5), { cfg:{} });
    assert.deepStrictEqual(X.restore(undefined), { cfg:{} });
});

// 37
check('restore_clean', () => {
    const input = JSON.stringify({
        v:1,
        cfg:{
            temp:{mode:'manual',min:5,max:1},
            bad:{mode:'auto'},
            press:{mode:'manual',min:1,max:2},
            hum:'x',
            count:{mode:'weird'}
        },
        override:{ cps:'press' }
    });
    const r = X.restore(input);
    assert.deepStrictEqual(r.cfg, {
        temp:{mode:'auto'},
        press:{mode:'manual',min:1,max:2},
        hum:{mode:'auto'},
        count:{mode:'auto'}
    });
    assert.strictEqual(r.override, undefined);
});

// 40
check('quick_period', () => {
    assert.deepStrictEqual(X.quickPeriod('1h'), { windowSec:3600, viewSec:3600 });
    assert.deepStrictEqual(X.quickPeriod('6h'), { windowSec:86400, viewSec:21600 });
    assert.deepStrictEqual(X.quickPeriod('24h'), { windowSec:86400, viewSec:86400 });
    assert.deepStrictEqual(X.quickPeriod('all'), { windowSec:0, viewSec:0 });
    assert.strictEqual(X.quickPeriod('7d'), null);
    assert.strictEqual(X.quickPeriod(undefined), null);
});

// 41
check('axis_title', () => {
    assert.strictEqual(X.axisTitle({ units:['мкЗв/ч','мЗв'] }), 'мкЗв/ч, мЗв');
    assert.strictEqual(X.axisTitle({ units:[] }), '');
    assert.strictEqual(X.axisTitle({}), '');
    assert.strictEqual(X.axisTitle(null), '');
});

check('nice_ticks_step5_upper', () => {
    assert.deepStrictEqual(X.niceTicks(0, 22, 5), [0, 5, 10, 15, 20]);
});
check('restore_wrong_version', () => {
    const r = X.restore(JSON.stringify({ v: 2, cfg: { temp: { mode: 'manual', min: 1, max: 2 } } }));
    assert.deepStrictEqual(r, { cfg: {} });
});
check('restore_ignores_old_override', () => {
    const r = X.restore(JSON.stringify({ v: 1, cfg: { temp: { mode: 'auto' } }, override: { cps: 'press' } }));
    assert.deepStrictEqual(r, { cfg: { temp: { mode: 'auto' } } });
    assert.strictEqual(JSON.parse(X.serialize({ temp: { mode: 'auto' } })).override, undefined);
});
check('build_axes_fifth_group_merges', () => {
    const defs = [
        { id: 'a', field: 'a', group: 'dose', unit: 'u1', color: 'c1' },
        { id: 'b', field: 'b', group: 'count', unit: 'u2', color: 'c2' },
        { id: 'c', field: 'c', group: 'temp', unit: 'u3', color: 'c3' },
        { id: 'd', field: 'd', group: 'press', unit: 'u4', color: 'c4' },
        { id: 'e', field: 'e', group: 'hum', unit: 'u5', color: 'c5' }
    ];
    const r = X.buildAxes(defs, { a: true, b: true, c: true, d: true, e: true });
    assert.strictEqual(r.axes.length, 4);
    assert.deepStrictEqual(r.axes[3].groups, ['press', 'hum']);
    assert.strictEqual(r.byGroup.hum, 3);
});
const AXES3 = [
    { id: 'count', min: 10, max: 20, auto: { min: 10, max: 20 } },
    { id: 'temp', min: 26, max: 27, auto: { min: 26, max: 27 } },
    { id: 'press', min: 757, max: 759, auto: { min: 757, max: 759 } }
];

check('targets_selected_only', () => {
    assert.deepStrictEqual(X.scaleTargets(AXES3, 'temp', false), ['temp']);
    assert.deepStrictEqual(X.scaleTargets(AXES3, 'zzz', false), []);
    assert.deepStrictEqual(X.scaleTargets(AXES3, null, false), []);
    assert.deepStrictEqual(X.scaleTargets(null, 'temp', false), []);
});

check('targets_all_axes', () => {
    assert.deepStrictEqual(X.scaleTargets(AXES3, 'temp', true), ['count', 'temp', 'press']);
    assert.deepStrictEqual(X.scaleTargets(AXES3, null, true), ['count', 'temp', 'press']);
});

check('zoom_one_axis_independent', () => {
    const cfg = X.zoomAxes({}, AXES3, ['temp'], 2);
    assert.deepStrictEqual(Object.keys(cfg), ['temp']);
    assert.strictEqual(cfg.temp.mode, 'manual');
    near(cfg.temp.max - cfg.temp.min, 2, 1e-9);
    near((cfg.temp.max + cfg.temp.min) / 2, 26.5, 1e-9);
});

check('zoom_all_axes', () => {
    const cfg = X.zoomAxes({}, AXES3, ['count', 'temp', 'press'], 0.5);
    near(cfg.count.max - cfg.count.min, 5, 1e-9);
    near(cfg.temp.max - cfg.temp.min, 0.5, 1e-9);
    near(cfg.press.max - cfg.press.min, 1, 1e-9);
});

check('zoom_keeps_other_entries', () => {
    const before = { count: { mode: 'manual', min: 0, max: 100 } };
    const cfg = X.zoomAxes(before, AXES3, ['temp'], 2);
    assert.deepStrictEqual(cfg.count, { mode: 'manual', min: 0, max: 100 });
    assert.deepStrictEqual(before, { count: { mode: 'manual', min: 0, max: 100 } });
});

check('manual_not_recomputed_by_auto', () => {
    const cfg = X.zoomAxes({}, AXES3, ['temp'], 2);
    const newAuto = { min: 100, max: 200 };
    const r1 = X.resolve(cfg.temp, newAuto);
    assert.strictEqual(r1.mode, 'manual');
    near(r1.min, 26 - 0.5, 1e-9);
    const r2 = X.resolve(cfg.count, newAuto);
    assert.strictEqual(r2.mode, 'auto');
    assert.strictEqual(r2.min, 100);
});

check('manual_auto_transition', () => {
    let cfg = X.zoomAxes({}, AXES3, ['press'], 2);
    assert.strictEqual(cfg.press.mode, 'manual');
    cfg = X.fitAxes(cfg, ['press']);
    assert.deepStrictEqual(cfg.press, { mode: 'auto' });
    cfg = X.zoomAxes(cfg, AXES3, ['press'], 2);
    cfg = X.resetAxes(cfg, ['press']);
    assert.strictEqual('press' in cfg, false);
});

check('reset_only_listed', () => {
    const cfg = { count: { mode: 'manual', min: 1, max: 2 }, temp: { mode: 'manual', min: 3, max: 4 }, press: { mode: 'auto', zero: true } };
    const r = X.resetAxes(cfg, ['temp']);
    assert.deepStrictEqual(Object.keys(r), ['count', 'press']);
    assert.deepStrictEqual(r.press, { mode: 'auto', zero: true });
});

check('axis_text_invalid', () => {
    const base = { temp: { mode: 'auto' } };
    let r = X.setAxisText(base, 'temp', '5', '1');
    assert.strictEqual(r.ok, false);
    assert.strictEqual(r.error, 'min должен быть меньше max');
    assert.deepStrictEqual(r.cfg, base);
    r = X.setAxisText(base, 'temp', 'abc', '1');
    assert.strictEqual(r.ok, false);
    assert.strictEqual(r.error, 'введите числа');
    r = X.setAxisText(base, 'temp', '25,5', '30');
    assert.strictEqual(r.ok, true);
    assert.deepStrictEqual(r.cfg.temp, { mode: 'manual', min: 25.5, max: 30 });
});

check('per_axis_persistence', () => {
    let cfg = X.zoomAxes({}, AXES3, ['temp'], 2);
    cfg = X.setZero(cfg, 'count', true);
    const back = X.restore(X.serialize(cfg)).cfg;
    assert.deepStrictEqual(back.temp, cfg.temp);
    assert.deepStrictEqual(back.count, { mode: 'auto', zero: true });
    assert.strictEqual('press' in back, false);
});

check('old_format_reads', () => {
    const old = JSON.stringify({ v: 1, cfg: { temp: { mode: 'manual', min: 1, max: 2 } }, override: { cps: 'press' } });
    const r = X.restore(old);
    assert.deepStrictEqual(r, { cfg: { temp: { mode: 'manual', min: 1, max: 2 } } });
});
check('axis_lines: линия у каждой оси, включая нулевой слот (T-54)', () => {
    const axes = [{ id: 'dose', side: 'L', slot: 0 }, { id: 'count', side: 'R', slot: 0 }, { id: 'press', side: 'R', slot: 1 }, { id: 'temp', side: 'L', slot: 1 }];
    const l = X.axisLines(axes, 60, 800, 10, 300, 56);
    assert.strictEqual(l.length, 4);
    assert.deepStrictEqual(l.map(o => o.id), ['dose', 'count', 'press', 'temp']);
    assert.deepStrictEqual(l.map(o => o.x), [60, 860, 916, 4]);
    for (const o of l) { assert.strictEqual(o.y0, 10); assert.strictEqual(o.y1, 310); }
    assert.deepStrictEqual(X.axisLines([], 0, 1, 0, 1, 1), []);
});

console.log('TESTS: ' + passed + '/' + total);
process.exit(passed === total ? 0 : 1);
