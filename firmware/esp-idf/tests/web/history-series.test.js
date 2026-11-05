const assert = require('assert');
const fs = require('fs');
const path = require('path');

const S = require(process.env.HS_MODULE || path.join(__dirname, '..', '..', 'web', 'history-series.js'));
const A = require(path.join(__dirname, '..', '..', 'web', 'history-core.js'));

const DEFS = [
    {id:'rate',name:'Мощность дозы',unit:'мкЗв/ч',field:'doserate_usvh',axis:'L',color:'--s1',dash:[]},
    {id:'cps',name:'CPS',unit:'имп/с',field:'cps',axis:'R',color:'--s2',dash:[]},
    {id:'pulses',name:'Импульсы',unit:'имп/2 с',field:'pulses_2s',axis:'R',color:'--s3',dash:[6,4]},
    {id:'dose',name:'Доза',unit:'мЗв',field:'dose_msv',axis:'L',color:'--s4',dash:[]},
    {id:'temp',name:'Температура',unit:'°C',field:'temp_c',axis:'R',color:'--s5',dash:[]}
];
const ALL = {rate:true,cps:true,pulses:true,dose:true,temp:true};
const DEF = {rate:true,cps:true,pulses:false,dose:false,temp:false};

let passed = 0;
let total = 0;

function check(name, fn) {
    total++;
    try {
        fn();
        passed++;
        console.log(`ok  ${name}`);
    } catch (e) {
        console.log(`FAIL ${name}: ${e.message}`);
    }
}

function closeTo(a, b, eps) {
    if (typeof a !== 'number' || typeof b !== 'number') throw new Error('closeTo expects numbers');
    if (Math.abs(a - b) > eps) throw new Error(`Expected ${a} to be close to ${b} (eps ${eps})`);
}

function oracleNearestIndex(rows, ts) {
    if (!rows || rows.length === 0) return -1;
    let bestIdx = 0;
    let bestDiff = Math.abs(rows[0].ts - ts);
    for (let i = 1; i < rows.length; i++) {
        const diff = Math.abs(rows[i].ts - ts);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIdx = i;
        }
    }
    return bestIdx;
}

function oracleMinMax(rows, field) {
    let min = Infinity;
    let max = -Infinity;
    let found = false;
    for (const r of rows) {
        const v = r[field];
        if (v !== undefined && v !== null && !isNaN(v)) {
            found = true;
            if (v < min) min = v;
            if (v > max) max = v;
        }
    }
    if (!found) return null;
    return { min, max };
}

function niceTicks(min, max, count) {
    if (min === max) {
        return [min - 1, min, min + 1];
    }
    const range = max - min;
    const roughStep = range / count;
    const magnitude = Math.pow(10, Math.floor(Math.log10(roughStep)));
    const residual = roughStep / magnitude;
    let step;
    if (residual <= 1.5) step = 1 * magnitude;
    else if (residual <= 3) step = 2 * magnitude;
    else if (residual <= 7) step = 5 * magnitude;
    else step = 10 * magnitude;

    const start = Math.floor(min / step) * step;
    const end = Math.ceil(max / step) * step;
    const ticks = [];
    for (let v = start; v <= end + step * 0.001; v += step) {
        ticks.push(v);
    }
    return ticks;
}

function loadRealData() {
    const dataDir = process.env.HC_DATA_DIR || 'C:/Temp/atomfast_work/e8';
    const rawPath = path.join(dataDir, 'e7_preota_raw.csv');
    const aggPath = path.join(dataDir, 'e7_preota_agg.csv');
    const rawContent = fs.readFileSync(rawPath, 'utf8');
    const aggContent = fs.readFileSync(aggPath, 'utf8');
    const rawRows = A.parseCsv(rawContent).rows;
    const aggRows = A.parseCsv(aggContent).rows;
    return { rawRows, aggRows };
}

let realData;
try {
    realData = loadRealData();
} catch (e) {
    console.log(`FAIL setup: ${e.message}`);
    process.exit(1);
}

const rawRows = realData.rawRows;
const aggRows = realData.aggRows;

// 1. visibleDefs
check('visibleDefs DEF', () => {
    const res = S.visibleDefs(DEFS, DEF);
    const ids = res.map(d => d.id);
    assert.deepStrictEqual(ids, ['rate', 'cps']);
});
check('visibleDefs empty', () => {
    const res = S.visibleDefs(DEFS, {});
    assert.deepStrictEqual(res, []);
});
check('visibleDefs truthy not true', () => {
    const vis = { rate: 1, cps: 0, pulses: 0, dose: 0, temp: 0 };
    const res = S.visibleDefs(DEFS, vis);
    const ids = res.map(d => d.id);
    assert.deepStrictEqual(ids, []);
});

// 2. axisDefs
check('axisDefs ALL L', () => {
    const res = S.axisDefs(DEFS, ALL, 'L');
    const ids = res.map(d => d.id);
    assert.deepStrictEqual(ids, ['rate', 'dose']);
});
check('axisDefs ALL R', () => {
    const res = S.axisDefs(DEFS, ALL, 'R');
    const ids = res.map(d => d.id);
    assert.deepStrictEqual(ids, ['cps', 'pulses', 'temp']);
});
check('axisDefs DEF L', () => {
    const res = S.axisDefs(DEFS, DEF, 'L');
    const ids = res.map(d => d.id);
    assert.deepStrictEqual(ids, ['rate']);
});

// 3. axisLabel
check('axisLabel ALL L', () => {
    const res = S.axisLabel(DEFS, ALL, 'L');
    assert.strictEqual(res, 'мкЗв/ч, мЗв');
});
check('axisLabel DEF R', () => {
    const res = S.axisLabel(DEFS, DEF, 'R');
    assert.strictEqual(res, 'имп/с');
});
check('axisLabel empty', () => {
    const res = S.axisLabel(DEFS, {}, 'L');
    assert.strictEqual(res, '');
});
check('axisLabel unique units', () => {
    const customDefs = [
        {id:'a', unit:'X', axis:'L'},
        {id:'b', unit:'X', axis:'L'}
    ];
    const vis = {a:true, b:true};
    const res = S.axisLabel(customDefs, vis, 'L');
    assert.strictEqual(res, 'X');
});

// 4. nearestIndex
check('nearestIndex hand-made', () => {
    const rows = [{ts:10},{ts:20},{ts:30},{ts:40}];
    assert.strictEqual(S.nearestIndex(rows, 10), 0);
    assert.strictEqual(S.nearestIndex(rows, 14), 0);
    assert.strictEqual(S.nearestIndex(rows, 16), 1);
    assert.strictEqual(S.nearestIndex(rows, 15), 0);
    assert.strictEqual(S.nearestIndex(rows, 25), 1);
    assert.strictEqual(S.nearestIndex(rows, 31), 2);
    assert.strictEqual(S.nearestIndex(rows, 100), 3);
    assert.strictEqual(S.nearestIndex(rows, -5), 0);
    assert.strictEqual(S.nearestIndex([], 10), -1);
});
check('nearestIndex real data', () => {
    const t0 = rawRows[0].ts;
    const t1 = rawRows[rawRows.length - 1].ts;
    const probes = [];
    for (let i = 0; i < 50; i++) {
        probes.push(t0 + (t1 - t0) * (i / 49));
    }
    for (const ts of probes) {
        const expected = oracleNearestIndex(rawRows, ts);
        const actual = S.nearestIndex(rawRows, ts);
        assert.strictEqual(actual, expected, `ts ${ts}: expected ${expected}, got ${actual}`);
    }
});

// 5. sliceRange
check('sliceRange hand-made', () => {
    const rows = [{ts:10},{ts:20},{ts:30},{ts:40}];
    let res = S.sliceRange(rows, 20, 30);
    assert.strictEqual(res.length, 2);
    assert.strictEqual(res[0].ts, 20);
    assert.strictEqual(res[1].ts, 30);

    res = S.sliceRange(rows, 21, 29);
    assert.strictEqual(res.length, 0);

    res = S.sliceRange(rows, 0, 100);
    assert.strictEqual(res.length, 4);

    res = S.sliceRange(rows, 30, 30);
    assert.strictEqual(res.length, 1);
    assert.strictEqual(res[0].ts, 30);
});
check('sliceRange real data', () => {
    const a = rawRows[1000].ts;
    const b = rawRows[2000].ts;
    const res = S.sliceRange(rawRows, a, b);
    const oracle = rawRows.filter(r => r.ts >= a && r.ts <= b);
    assert.strictEqual(res.length, oracle.length);
    if (res.length > 0) {
        assert.strictEqual(res[0].ts, oracle[0].ts);
        assert.strictEqual(res[res.length - 1].ts, oracle[oracle.length - 1].ts);
    }
    assert.notStrictEqual(res, rawRows);
});

// 6. axisScale
check('axisScale empty rows', () => {
    const res = S.axisScale([], ['a'], 5);
    assert.strictEqual(res, null);
});
check('axisScale empty fields', () => {
    const res = S.axisScale([{a:1}], [], 5);
    assert.strictEqual(res, null);
});
check('axisScale all NaN', () => {
    const res = S.axisScale([{a:NaN},{a:undefined}], ['a'], 5);
    assert.strictEqual(res, null);
});
check('axisScale hand-made', () => {
    const rows = [{a:0},{a:10}];
    const res = S.axisScale(rows, ['a'], 5);
    const ticks = res.ticks;
    assert.ok(ticks[0] <= -0.5);
    assert.ok(ticks[ticks.length - 1] >= 10.5);
    assert.strictEqual(res.min, ticks[0]);
    assert.strictEqual(res.max, ticks[ticks.length - 1]);
    const spacing = ticks[1] - ticks[0];
    for (let i = 2; i < ticks.length; i++) {
        closeTo(ticks[i] - ticks[i-1], spacing, 1e-9);
    }
});
check('axisScale equal values', () => {
    const rows = [{a:5},{a:5}];
    const res = S.axisScale(rows, ['a'], 5);
    assert.ok(res.ticks[0] < 5);
    assert.ok(res.ticks[res.ticks.length - 1] > 5);
});
check('axisScale two fields', () => {
    const rows = [{a:1,b:100},{a:2,b:200}];
    const res = S.axisScale(rows, ['a','b'], 5);
    assert.ok(res.min <= 1);
    assert.ok(res.max >= 200);
});
check('axisScale missing fields', () => {
    const rows = [{a:1},{x:5},{a:3}];
    const res = S.axisScale(rows, ['a'], 5);
    assert.ok(res.min > -5);
    assert.ok(res.max < 10);
});

// 7. scaleFor
check('scaleFor real data L', () => {
    const res = S.scaleFor(rawRows, DEFS, DEF, 'L', 5);
    const oracle = oracleMinMax(rawRows, 'doserate_usvh');
    assert.ok(res.min <= oracle.min);
    assert.ok(res.max >= oracle.max);
    const range = oracle.max - oracle.min;
    assert.ok(res.max - oracle.max < range * 0.5);
});
check('scaleFor real data R', () => {
    const res = S.scaleFor(rawRows, DEFS, DEF, 'R', 5);
    const oracle = oracleMinMax(rawRows, 'cps');
    assert.ok(res.min <= oracle.min);
    assert.ok(res.max >= oracle.max);
});
check('scaleFor real data ALL R', () => {
    const res = S.scaleFor(rawRows, DEFS, ALL, 'R', 5);
    const oracleCps = oracleMinMax(rawRows, 'cps');
    const oraclePulses = oracleMinMax(rawRows, 'pulses_2s');
    const oracleTemp = oracleMinMax(rawRows, 'temp_c');
    const min = Math.min(oracleCps.min, oraclePulses.min, oracleTemp.min);
    const max = Math.max(oracleCps.max, oraclePulses.max, oracleTemp.max);
    assert.ok(res.min <= min);
    assert.ok(res.max >= max);
});
check('scaleFor no visible', () => {
    const res = S.scaleFor(rawRows, DEFS, {}, 'L', 5);
    assert.strictEqual(res, null);
});
check('scaleFor independence', () => {
    const l = S.scaleFor(rawRows, DEFS, DEF, 'L', 5);
    const r = S.scaleFor(rawRows, DEFS, DEF, 'R', 5);
    assert.ok(r.max > 10 * l.max);
});

// 8. cursorValues
check('cursorValues empty', () => {
    const res = S.cursorValues([], DEFS, DEF, 10);
    assert.strictEqual(res, null);
});
check('cursorValues real data', () => {
    const ts = rawRows[500].ts + 0.4;
    const res = S.cursorValues(rawRows, DEFS, DEF, ts);
    assert.strictEqual(res.index, 500);
    assert.strictEqual(res.ts, rawRows[500].ts);
    assert.strictEqual(res.items.length, 2);
    assert.strictEqual(res.items[0].id, 'rate');
    assert.strictEqual(res.items[1].id, 'cps');
    assert.strictEqual(res.items[0].value, rawRows[500].doserate_usvh);
    assert.strictEqual(res.items[1].value, rawRows[500].cps);
    assert.strictEqual(res.items[0].color, '--s1');
    assert.strictEqual(res.items[0].unit, 'мкЗв/ч');
    assert.strictEqual(res.items[0].axis, 'L');
});
check('cursorValues ALL', () => {
    const ts = rawRows[500].ts;
    const res = S.cursorValues(rawRows, DEFS, ALL, ts);
    assert.strictEqual(res.items.length, 5);
    const ids = res.items.map(i => i.id);
    assert.deepStrictEqual(ids, ['rate', 'cps', 'pulses', 'dose', 'temp']);
});
check('cursorValues missing field', () => {
    const rows = [{ts:10, a:1}];
    const defs = [{id:'a', field:'a', axis:'L', color:'c', unit:'u'}, {id:'b', field:'b', axis:'L', color:'c', unit:'u'}];
    const vis = {a:true, b:true};
    const res = S.cursorValues(rows, defs, vis, 10);
    assert.strictEqual(res.items[0].value, 1);
    assert.strictEqual(res.items[1].value, null);
});

// 9. plotPoints
check('plotPoints hand-made', () => {
    const rows = [{ts:1,v:1},{ts:2},{ts:3,v:NaN},{ts:4,v:4}];
    const def = {field:'v'};
    const res = S.plotPoints(rows, def, 10);
    assert.deepStrictEqual(res, [{x:1,y:1},{x:4,y:4}]);
});
check('plotPoints real data', () => {
    const def = {field:'doserate_usvh'};
    const res = S.plotPoints(rawRows, def, 150);
    assert.ok(res.length <= 150);
    const oracle = oracleMinMax(rawRows, 'doserate_usvh');
    let foundMin = false;
    let foundMax = false;
    for (const p of res) {
        if (p.y === oracle.min) foundMin = true;
        if (p.y === oracle.max) foundMax = true;
    }
    assert.ok(foundMin, 'min not found');
    assert.ok(foundMax, 'max not found');
    for (let i = 1; i < res.length; i++) {
        assert.ok(res[i].x >= res[i-1].x);
    }
});

// 10. yToPx
check('yToPx', () => {
    assert.strictEqual(S.yToPx(0, 0, 10, 20, 100), 120);
    assert.strictEqual(S.yToPx(10, 0, 10, 20, 100), 20);
    assert.strictEqual(S.yToPx(5, 0, 10, 20, 100), 70);
    assert.strictEqual(S.yToPx(3, 3, 3, 20, 100), 70);
});

// 11. xToPx
check('xToPx', () => {
    assert.strictEqual(S.xToPx(50, 0, 100, 10, 200), 110);
    assert.strictEqual(S.xToPx(0, 0, 100, 10, 200), 10);
    assert.strictEqual(S.xToPx(50, 100, 0, 10, 200), 10);
});
check('pxToTime', () => {
    for (const t of [0, 25, 50, 100]) {
        const px = S.xToPx(t, 0, 100, 10, 200);
        const back = S.pxToTime(px, 0, 100, 10, 200);
        closeTo(back, t, 1e-9);
    }
    assert.strictEqual(S.pxToTime(-50, 0, 100, 10, 200), 0);
    assert.strictEqual(S.pxToTime(1000, 0, 100, 10, 200), 100);
    assert.strictEqual(S.pxToTime(50, 0, 100, 10, 0), 0);
});

// 12. zoomRange
check('zoomRange', () => {
    let res = S.zoomRange(0, 100, 50, 0.5, 10);
    assert.strictEqual(res.t0, 25);
    assert.strictEqual(res.t1, 75);

    res = S.zoomRange(0, 100, 0, 0.5, 10);
    assert.strictEqual(res.t0, 0);
    assert.strictEqual(res.t1, 50);

    res = S.zoomRange(0, 100, 50, 1.25, 10);
    assert.strictEqual(res.t0, -12.5);
    assert.strictEqual(res.t1, 112.5);

    res = S.zoomRange(0, 100, 50, 0.01, 10);
    assert.strictEqual(res.t0, 45);
    assert.strictEqual(res.t1, 55);
});

// 13. fullRange
check('fullRange empty', () => {
    const res = S.fullRange([]);
    assert.strictEqual(res, null);
});
check('fullRange real data', () => {
    const res = S.fullRange(rawRows);
    assert.strictEqual(res.t0, rawRows[0].ts);
    assert.strictEqual(res.t1, rawRows[rawRows.length - 1].ts);
});
check('fullRange one row', () => {
    const res = S.fullRange([{ts:100}]);
    assert.strictEqual(res.t0, 100);
    assert.strictEqual(res.t1, 101);
});

// 14. End-to-end
check('end-to-end', () => {
    const view = S.fullRange(rawRows);
    const viewRows = S.sliceRange(rawRows, view.t0, view.t1);
    assert.strictEqual(viewRows.length, 3263);

    const lScale = S.scaleFor(rawRows, DEFS, DEF, 'L', 5);
    const rScale = S.scaleFor(rawRows, DEFS, DEF, 'R', 5);
    assert.ok(lScale !== null);
    assert.ok(rScale !== null);

    const ts = rawRows[1234].ts;
    const cursor = S.cursorValues(rawRows, DEFS, DEF, ts);
    const firstSame = rawRows.find(r => r.ts === ts); // ts имеет секундное разрешение: при совпадении берётся ранняя строка
    assert.strictEqual(cursor.items[0].value, firstSame.doserate_usvh);
    assert.strictEqual(cursor.items[1].value, firstSame.cps);

    const points = S.plotPoints(rawRows, {field:'doserate_usvh'}, 150);
    const left = 56;
    const width = 300;
    const top = 24;
    const height = 250;
    const t0 = view.t0;
    const t1 = view.t1;
    const min = lScale.min;
    const max = lScale.max;

    for (const p of points) {
        const px = S.xToPx(p.x, t0, t1, left, width);
        const py = S.yToPx(p.y, min, max, top, height);
        assert.ok(px >= left - 1e-9 && px <= left + width + 1e-9, `px ${px} out of bounds`);
        assert.ok(py >= top - 1e-9 && py <= top + height + 1e-9, `py ${py} out of bounds`);
    }
});

check('cursorValues NaN value gives null', () => {
    const defs = [{id:'a', field:'a', axis:'L', color:'c', unit:'u', name:'A'}];
    const res = S.cursorValues([{ts:1, a:NaN}], defs, {a:true}, 1);
    assert.strictEqual(res.items[0].value, null);
});
console.log(`TESTS: ${passed}/${total}`);
if (passed < total) {
    process.exit(1);
}
