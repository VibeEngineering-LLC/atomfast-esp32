const assert = require('assert');
const fs = require('fs');
const path = require('path');

const A = require(process.env.HC_MODULE || path.join(__dirname, '..', '..', 'web', 'history-core.js'));

const DATA_DIR = process.env.HC_DATA_DIR || 'C:/Temp/atomfast_work/e8';
const RAW_FILE = path.join(DATA_DIR, 'e7_preota_raw.csv');
const AGG_FILE = path.join(DATA_DIR, 'e7_preota_agg.csv');
const STATUS_FILE = path.join(DATA_DIR, 'e7_preota_status.json');

let passed = 0;
let failed = 0;

function check(name, fn) {
    try {
        fn();
        console.log(`ok  ${name}`);
        passed++;
    } catch (e) {
        console.log(`FAIL ${name}: ${e.message}`);
        failed++;
    }
}

function closeTo(a, b, eps) {
    if (Math.abs(a - b) > eps) {
        throw new Error(`Expected ${a} to be close to ${b} (eps ${eps})`);
    }
}

// --- Oracles ---

function oracleParseCsv(text) {
    const lines = text.split('\n');
    const rows = [];
    let bad = 0;
    if (lines.length === 0) return { rows, bad };
    
    // Skip header
    let startIdx = 0;
    if (lines[0] && lines[0].trim() !== '') {
        startIdx = 1;
    }

    for (let i = startIdx; i < lines.length; i++) {
        let line = lines[i].trim();
        if (line === '') continue;
        
        const parts = line.split(',');
        if (parts.length < 10) {
            bad++;
            continue;
        }

        const ts = parseInt(parts[0], 10);
        const kind = parseInt(parts[1], 10);
        const dose_msv = parseFloat(parts[2]);
        const doserate_usvh = parseFloat(parts[3]);
        const pulses_2s = parseInt(parts[4], 10);
        const cps = parseFloat(parts[5]);
        const battery = parseFloat(parts[6]);
        const temp_c = parseFloat(parts[7]);
        const flags = parts[8];
        const n = parseInt(parts[9], 10);

        if (isNaN(ts) || isNaN(kind) || isNaN(dose_msv) || isNaN(doserate_usvh) || isNaN(pulses_2s) || isNaN(cps) || isNaN(battery) || isNaN(temp_c) || isNaN(n)) {
            bad++;
            continue;
        }

        rows.push({
            ts,
            kind,
            dose_msv,
            doserate_usvh,
            pulses_2s,
            cps,
            battery,
            temp_c,
            flags,
            n
        });
    }
    return { rows, bad };
}

function oracleRatePerSec(rows) {
    if (!rows || rows.length < 2) return 0;
    let firstTs = null;
    let lastTs = null;
    let count = 0;
    for (let i = 0; i < rows.length; i++) {
        const ts = rows[i].ts;
        if (ts > 0) {
            if (firstTs === null) firstTs = ts;
            lastTs = ts;
            count++;
        }
    }
    if (count < 2) return 0;
    const span = lastTs - firstTs;
    if (span <= 0) return 0;
    return (count - 1) / span;
}

function oracleSummarize(rows, field) {
    if (!rows || rows.length === 0) {
        return { n: 0, min: null, max: null, mean: null, last: null };
    }
    let min = Infinity;
    let max = -Infinity;
    let sum = 0;
    let last = null;
    let n = 0;
    for (let i = 0; i < rows.length; i++) {
        const v = rows[i][field];
        if (v === null || v === undefined || isNaN(v)) continue;
        if (v < min) min = v;
        if (v > max) max = v;
        sum += v;
        last = v;
        n++;
    }
    if (n === 0) {
        return { n: 0, min: null, max: null, mean: null, last: null };
    }
    return { n, min, max, mean: sum / n, last };
}

function oracleNiceTicks(min, max, count) {
    if (min === max) {
        min -= 1;
        max += 1;
    }
    const range = max - min;
    const roughStep = range / count;
    const magnitude = Math.pow(10, Math.floor(Math.log10(roughStep)));
    const residual = roughStep / magnitude;
    
    let niceStep;
    if (residual <= 1) niceStep = 1;
    else if (residual <= 2) niceStep = 2;
    else if (residual <= 5) niceStep = 5;
    else niceStep = 10;
    
    niceStep *= magnitude;
    
    const niceMin = Math.floor(min / niceStep) * niceStep;
    const niceMax = Math.ceil(max / niceStep) * niceStep;
    
    const ticks = [];
    for (let t = niceMin; t <= niceMax + niceStep * 0.001; t += niceStep) {
        ticks.push(t);
    }
    return ticks;
}

function oracleTimeTicks(t0, t1, count, offMin) {
    if (t1 <= t0) return [];
    const span = t1 - t0;
    const targetStep = span / count;
    
    // Find a "nice" step that is a multiple of 60 (seconds)
    // The prompt implies specific steps like 600, 1800, 86400.
    // Let's try to match the logic: step should be such that (t + offset) % step === 0
    // And step is "nice".
    
    // Heuristic for nice steps in seconds:
    // 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400, 21600, 43200, 86400
    const niceSteps = [60, 120, 300, 600, 900, 1800, 3600, 7200, 14400, 21600, 43200, 86400, 172800, 604800];
    
    let bestStep = niceSteps[niceSteps.length - 1];
    let minDiff = Infinity;
    
    for (let s of niceSteps) {
        const diff = Math.abs(s - targetStep);
        if (diff < minDiff) {
            minDiff = diff;
            bestStep = s;
        }
    }
    
    const step = bestStep;
    const offset = offMin * 60;
    
    // First tick >= t0 such that (t + offset) % step === 0
    let first = t0;
    const rem = (first + offset) % step;
    if (rem !== 0) {
        first += (step - rem);
    }
    
    const ticks = [];
    for (let t = first; t <= t1; t += step) {
        ticks.push(t);
    }
    return ticks;
}

function oracleFmtTime(ts, mode, offMin) {
    const date = new Date((ts - offMin * 60) * 1000);
    const yyyy = date.getUTCFullYear();
    const mm = String(date.getUTCMonth() + 1).padStart(2, '0');
    const dd = String(date.getUTCDate()).padStart(2, '0');
    const hh = String(date.getUTCHours()).padStart(2, '0');
    const mi = String(date.getUTCMinutes()).padStart(2, '0');
    const ss = String(date.getUTCSeconds()).padStart(2, '0');
    
    if (mode === 'full') return `${dd}.${mm}.${yyyy} ${hh}:${mi}:${ss}`;
    if (mode === 's') return `${hh}:${mi}:${ss}`;
    if (mode === 'm') return `${hh}:${mi}`;
    if (mode === 'd') return `${dd}.${mm} ${hh}:${mi}`;
    return '';
}

function oracleFmtDuration(sec) {
    if (isNaN(sec) || !isFinite(sec)) return '—';
    if (sec < 60) return `${Math.floor(sec)} с`;
    if (sec < 3600) return `${Math.floor(sec / 60)} мин`;
    if (sec < 86400) {
        const h = Math.floor(sec / 3600);
        const m = Math.floor((sec % 3600) / 60);
        if (m === 0) return `${h} ч`;
        return `${h} ч ${m} мин`;
    }
    const d = Math.floor(sec / 86400);
    const h = Math.floor((sec % 86400) / 3600);
    if (h === 0) return `${d} д`;
    return `${d} д ${h} ч`;
}

function oracleFmtBytes(bytes) {
    if (isNaN(bytes) || !isFinite(bytes)) return '—';
    if (bytes < 1024) return `${bytes} Б`;
    if (bytes < 1024 * 1024) {
        const kb = bytes / 1024;
        return `${kb.toFixed(1)} КБ`;
    }
    const mb = bytes / (1024 * 1024);
    return `${mb.toFixed(1)} МБ`;
}

function oracleReserveSec(limit, count, rate) {
    if (rate === 0) return Infinity;
    const remaining = limit - count;
    if (remaining <= 0) return 0;
    return remaining / rate;
}

function oracleSinceFor(ring, windowSec, rate, maxRows) {
    const { seq, oldest, count } = ring;
    let want;
    if (windowSec === 0) {
        want = maxRows;
    } else {
        want = Math.ceil(windowSec * rate * 1.2) + 5;
        if (want > maxRows) want = maxRows;
    }
    let since = seq - want;
    if (since < oldest) since = oldest;
    return since;
}

function oracleNextSince(current, oldest, maxRows) {
    // Logic: if current < oldest, return oldest? 
    // Prompt: (0,0,10)=10; (5,20,10)=30; (50,20,10)=60.
    // It seems: next = current + maxRows? 
    // (0,0,10) -> 0+10=10.
    // (5,20,10) -> 5+10=15? No, result is 30.
    // Maybe it's related to oldest?
    // (5,20,10) -> oldest is 20. 20+10=30.
    // (50,20,10) -> oldest is 20. 20+10=30? No, result is 60.
    // Let's look at (50,20,10)=60. 50+10=60.
    // So if current >= oldest, next = current + maxRows.
    // If current < oldest, next = oldest + maxRows?
    // (0,0,10) -> 0 < 0 is false. 0+10=10.
    // (5,20,10) -> 5 < 20 is true. 20+10=30.
    // (50,20,10) -> 50 < 20 is false. 50+10=60.
    if (current < oldest) {
        return oldest + maxRows;
    }
    return current + maxRows;
}

// --- Load Data ---

let rawText = '';
let aggText = '';
let statusJson = {};

try {
    rawText = fs.readFileSync(RAW_FILE, 'utf8');
    aggText = fs.readFileSync(AGG_FILE, 'utf8');
    statusJson = JSON.parse(fs.readFileSync(STATUS_FILE, 'utf8'));
} catch (e) {
    console.error(`Failed to load data files: ${e.message}`);
    process.exit(1);
}

const rawParsed = oracleParseCsv(rawText);
const aggParsed = oracleParseCsv(aggText);

// --- Tests ---

// 1. parseCsv real raw file
check('parseCsv raw file length and first/last', () => {
    const res = A.parseCsv(rawText);
    assert.strictEqual(res.rows.length, 3263);
    assert.strictEqual(res.bad, 0);
    
    const first = res.rows[0];
    assert.strictEqual(first.ts, 1792853245);
    assert.strictEqual(first.dose_msv, 0.192269);
    assert.strictEqual(first.doserate_usvh, 0.0775837);
    assert.strictEqual(first.pulses_2s, 31);
    assert.strictEqual(first.n, 1);
    
    const last = res.rows[res.rows.length - 1];
    const oracleLast = rawParsed.rows[rawParsed.rows.length - 1];
    assert.strictEqual(last.ts, oracleLast.ts);
});

// 2. parseCsv real agg file
check('parseCsv agg file', () => {
    const res = A.parseCsv(aggText);
    assert.strictEqual(res.rows.length, 109);
    assert.strictEqual(res.bad, 0);
    
    const first = res.rows[0];
    assert.strictEqual(first.ts, 1792853220);
    assert.strictEqual(first.kind, 60);
    assert.strictEqual(first.n, 17);
});

// 3. CRLF handling
check('parseCsv CRLF', () => {
    const crlfText = rawText.replace(/\n/g, '\r\n');
    const resLf = A.parseCsv(rawText);
    const resCrlf = A.parseCsv(crlfText);
    assert.deepStrictEqual(resCrlf.rows, resLf.rows);
    assert.strictEqual(resCrlf.bad, resLf.bad);
});

// 4. Truncated last line
check('parseCsv truncated last line', () => {
    // Cut last 7 chars and trailing newline
    // rawText ends with \n usually.
    let truncated = rawText;
    if (truncated.endsWith('\n')) {
        truncated = truncated.slice(0, -1); // remove trailing newline
    }
    truncated = truncated.slice(0, -7); // remove last 7 chars
    
    const res = A.parseCsv(truncated);
    assert.strictEqual(res.rows.length, 3262);
    assert.strictEqual(res.bad, 1);
});

// 5. Garbage fields
check('parseCsv garbage fields', () => {
    const header = 'ts,kind,dose_msv,doserate_usvh,pulses_2s,cps,battery,temp_c,flags,n';
    const good1 = '1792853245,0,0.1,0.07,31,100,80,25,0,1';
    const bad1 = '1792853246,0,abc,0.07,31,100,80,25,0,1'; // abc in dose
    const bad2 = '1792853247,0,,0.07,31,100,80,25,0,1'; // empty dose
    const bad3 = '1792853248,0,0.1,0.07,31,100,80,25,0,1,extra'; // extra field
    const good2 = '1792853249,0,0.2,0.08,32,101,81,26,0,2';
    
    const text = [header, good1, bad1, bad2, bad3, good2].join('\n');
    const res = A.parseCsv(text);
    
    assert.strictEqual(res.rows.length, 2);
    assert.strictEqual(res.bad, 3);
    
    assert.strictEqual(res.rows[0].ts, 1792853245);
    assert.strictEqual(res.rows[1].ts, 1792853249);
});

// 6. Header without ts / Empty string
check('parseCsv header without ts and empty', () => {
    const textNoTs = 'kind,dose_msv\n0,0.1\n0,0.2';
    const res1 = A.parseCsv(textNoTs);
    assert.deepStrictEqual(res1.rows, []);
    assert.strictEqual(res1.bad, 2); // 2 data lines
    
    const res2 = A.parseCsv('');
    assert.deepStrictEqual(res2.rows, []);
    assert.strictEqual(res2.bad, 0);
});

// hasTime / splitKnown
check('hasTime and splitKnown', () => {
    assert.strictEqual(A.hasTime(0), false);
    assert.strictEqual(A.hasTime(1699999999), false);
    assert.strictEqual(A.hasTime(1700000000), true);
    assert.strictEqual(A.hasTime(NaN), false);
    
    const rows = [
        { ts: 0, kind: 0, dose_msv: 0, doserate_usvh: 0, pulses_2s: 0, cps: 0, battery: 0, temp_c: 0, flags: '', n: 1 },
        { ts: 1792853245, kind: 0, dose_msv: 0, doserate_usvh: 0, pulses_2s: 0, cps: 0, battery: 0, temp_c: 0, flags: '', n: 1 },
        { ts: 5, kind: 0, dose_msv: 0, doserate_usvh: 0, pulses_2s: 0, cps: 0, battery: 0, temp_c: 0, flags: '', n: 1 }
    ];
    
    const split = A.splitKnown(rows);
    assert.strictEqual(split.known.length, 1);
    assert.strictEqual(split.unknown, 2);
    assert.strictEqual(split.known[0].ts, 1792853245);
});

// windowRows
check('windowRows real data', () => {
    const rows = rawParsed.rows;
    const lastTs = rows[rows.length - 1].ts;
    const windowSec = 3600;
    
    const res = A.windowRows(rows, windowSec);
    
    // Oracle: rows with ts >= lastTs - 3600
    const oracleRows = rows.filter(r => r.ts >= lastTs - windowSec);
    
    assert.strictEqual(res.length, oracleRows.length);
    assert.strictEqual(res[0].ts, oracleRows[0].ts);
    
    // windowSec 0 returns all
    const res0 = A.windowRows(rows, 0);
    assert.strictEqual(res0.length, 3263);
    
    // windowSec 1
    const res1 = A.windowRows(rows, 1);
    const oracle1 = rows.filter(r => r.ts >= lastTs - 1);
    assert.strictEqual(res1.length, oracle1.length);
    
    // Empty input
    const resEmpty = A.windowRows([], 3600);
    assert.deepStrictEqual(resEmpty, []);
});

// downsample
check('downsample real data', () => {
    const rows = rawParsed.rows;
    const field = 'doserate_usvh';
    const maxPoints = 200;
    
    const res = A.downsample(rows, field, maxPoints);
    
    assert.ok(res.length <= 200);
    assert.ok(res.length > 100);
    
    // Oracle min/max
    let min = Infinity;
    let max = -Infinity;
    for (let r of rows) {
        if (r[field] < min) min = r[field];
        if (r[field] > max) max = r[field];
    }
    
    let foundMin = false;
    let foundMax = false;
    for (let p of res) {
        if (p.y === min) foundMin = true;
        if (p.y === max) foundMax = true;
    }
    assert.ok(foundMin, 'Min not found');
    assert.ok(foundMax, 'Max not found');
    
    // x non-decreasing
    for (let i = 1; i < res.length; i++) {
        assert.ok(res[i].x >= res[i-1].x);
    }
    
    // Every point exists in source
    const sourceMap = new Map();
    for (let r of rows) {
        sourceMap.set(r.ts + '|' + r[field], 1);
    }
    for (let p of res) {
        assert.ok(sourceMap.has(p.x + '|' + p.y), `point ${p.x}|${p.y} not in source`);
    }
    
    // maxPoints >= n returns n points
    const resAll = A.downsample(rows, field, 3263);
    assert.strictEqual(resAll.length, 3263);
    for (let i = 0; i < 3263; i++) {
        assert.strictEqual(resAll[i].x, rows[i].ts);
        assert.strictEqual(resAll[i].y, rows[i][field]);
    }
});

check('downsample spike', () => {
    // Hand-made 1000-row series
    const rows = [];
    for (let i = 0; i < 1000; i++) {
        rows.push({ ts: i, doserate_usvh: 1 });
    }
    rows[537].doserate_usvh = 999;
    
    const res = A.downsample(rows, 'doserate_usvh', 100);
    
    let foundSpike = false;
    for (let p of res) {
        if (p.y === 999) {
            foundSpike = true;
            break;
        }
    }
    assert.ok(foundSpike, 'Spike not found in downsampled data');
});

// niceTicks
check('niceTicks basic', () => {
    const t1 = A.niceTicks(0, 10, 5);
    assert.deepStrictEqual(t1, [0, 2, 4, 6, 8, 10]);
    
    const t2 = A.niceTicks(0.05, 0.09, 4);
    assert.ok(t2[0] <= 0.05);
    assert.ok(t2[t2.length - 1] >= 0.09);
    for (let i = 1; i < t2.length; i++) {
        closeTo(t2[i] - t2[i-1], t2[1] - t2[0], 1e-9);
    }
    
    const t3 = A.niceTicks(5, 5, 5);
    assert.ok(t3[0] < 5);
    assert.ok(t3[t3.length - 1] > 5);
    
    const t4 = A.niceTicks(0, 0, 5);
    assert.ok(t4[0] < 0);
    assert.ok(t4[t4.length - 1] > 0);
    
    // Real data coverage
    let min = Infinity;
    let max = -Infinity;
    for (let r of rawParsed.rows) {
        if (r.doserate_usvh < min) min = r.doserate_usvh;
        if (r.doserate_usvh > max) max = r.doserate_usvh;
    }
    const t5 = A.niceTicks(min, max, 5);
    assert.ok(t5[0] <= min);
    assert.ok(t5[t5.length - 1] >= max);
});

// timeTicks
check('timeTicks offMin 0', () => {
    const t0 = 1792853245;
    const t1 = t0 + 3600;
    const ticks = A.timeTicks(t0, t1, 6, 0);
    
    assert.ok(ticks.length > 0);
    assert.ok(ticks[0] >= t0);
    assert.ok(ticks[ticks.length - 1] <= t1);
    
    for (let i = 1; i < ticks.length; i++) {
        assert.ok(ticks[i] > ticks[i-1]);
        closeTo(ticks[i] - ticks[i-1], 600, 1e-9);
    }
});

check('timeTicks offMin -180', () => {
    const t0 = 1792853245;
    const t1 = t0 + 86400 * 3;
    const ticks = A.timeTicks(t0, t1, 3, -180);
    
    assert.ok(ticks.length > 0);
    for (let t of ticks) {
        assert.strictEqual((t + 180 * 60) % 86400, 0);
    }
    for (let i = 1; i < ticks.length; i++) {
        closeTo(ticks[i] - ticks[i-1], 86400, 1e-9);
    }
});

check('timeTicks offMin 300', () => {
    const t0 = 1792853245;
    const t1 = t0 + 7200;
    const ticks = A.timeTicks(t0, t1, 4, 300);
    
    assert.ok(ticks.length > 0);
    for (let t of ticks) {
        assert.strictEqual((t - 300 * 60) % 1800, 0);
    }
    for (let i = 1; i < ticks.length; i++) {
        closeTo(ticks[i] - ticks[i-1], 1800, 1e-9);
    }
});

check('timeTicks t1 <= t0', () => {
    const ticks = A.timeTicks(100, 100, 6, 0);
    assert.deepStrictEqual(ticks, []);
});

// fmtTime
check('fmtTime basic', () => {
    assert.strictEqual(A.fmtTime(0, 'full', 0), '01.01.1970 00:00:00');
    
    const ts = 1792853245;
    const expected = oracleFmtTime(ts, 'full', 0);
    assert.strictEqual(A.fmtTime(ts, 'full', 0), expected);
    
    const expectedOff = oracleFmtTime(ts, 'full', -180);
    assert.strictEqual(A.fmtTime(ts, 'full', -180), expectedOff);
    
    const full = A.fmtTime(ts, 'full', 0);
    const s = A.fmtTime(ts, 's', 0);
    const m = A.fmtTime(ts, 'm', 0);
    const d = A.fmtTime(ts, 'd', 0);
    
    assert.ok(full.includes(s));
    assert.ok(full.includes(m));
    assert.strictEqual(d, full.slice(0, 5) + ' ' + full.slice(11, 16));
});

check('tickMode', () => {
    assert.strictEqual(A.tickMode(599), 's');
    assert.strictEqual(A.tickMode(600), 'm');
    assert.strictEqual(A.tickMode(86399), 'm');
    assert.strictEqual(A.tickMode(86400), 'd');
});

// fmtDuration
check('fmtDuration', () => {
    assert.strictEqual(A.fmtDuration(45), '45 с');
    assert.strictEqual(A.fmtDuration(60), '1 мин');
    assert.strictEqual(A.fmtDuration(3599), '59 мин');
    assert.strictEqual(A.fmtDuration(3600), '1 ч');
    assert.strictEqual(A.fmtDuration(3660), '1 ч 1 мин');
    assert.strictEqual(A.fmtDuration(86400), '1 д');
    assert.strictEqual(A.fmtDuration(90000), '1 д 1 ч');
    assert.strictEqual(A.fmtDuration(NaN), '—');
    assert.strictEqual(A.fmtDuration(Infinity), '—');
});

// fmtBytes
check('fmtBytes', () => {
    assert.strictEqual(A.fmtBytes(500), '500 Б');
    assert.strictEqual(A.fmtBytes(1024), '1.0 КБ');
    assert.strictEqual(A.fmtBytes(1536), '1.5 КБ');
    assert.strictEqual(A.fmtBytes(1048576), '1.0 МБ');
    assert.strictEqual(A.fmtBytes(4194300), '4.0 МБ');
    assert.strictEqual(A.fmtBytes(NaN), '—');
});

// reserveSec
check('reserveSec', () => {
    assert.strictEqual(A.reserveSec(10, 10, 1), 0);
    assert.strictEqual(A.reserveSec(11, 10, 1), 0);
    assert.strictEqual(A.reserveSec(0, 100, 2), 50);
    assert.strictEqual(A.reserveSec(0, 100, 0), Infinity);
    
    // Real data
    const limit = statusJson.raw.limit;
    const count = rawParsed.rows.length;
    const rate = oracleRatePerSec(rawParsed.rows);
    
    const expected = oracleReserveSec(limit, count, rate);
    const actual = A.reserveSec(count, limit, rate);
    
    if (isFinite(expected)) {
        closeTo(actual, expected, 1e-6);
    } else {
        assert.strictEqual(actual, expected);
    }
});

// ratePerSec
check('ratePerSec', () => {
    const rate = A.ratePerSec(rawParsed.rows);
    const expected = oracleRatePerSec(rawParsed.rows);
    closeTo(rate, expected, 1e-9);
    
    assert.strictEqual(A.ratePerSec([]), 0);
    assert.strictEqual(A.ratePerSec([{ ts: 100, kind: 0, dose_msv: 0, doserate_usvh: 0, pulses_2s: 0, cps: 0, battery: 0, temp_c: 0, flags: '', n: 1 }]), 0);
    
    const sameTs = [
        { ts: 100, kind: 0, dose_msv: 0, doserate_usvh: 0, pulses_2s: 0, cps: 0, battery: 0, temp_c: 0, flags: '', n: 1 },
        { ts: 100, kind: 0, dose_msv: 0, doserate_usvh: 0, pulses_2s: 0, cps: 0, battery: 0, temp_c: 0, flags: '', n: 1 }
    ];
    assert.strictEqual(A.ratePerSec(sameTs), 0);
    
    const rows = [
        { ts: 0, kind: 0, dose_msv: 0, doserate_usvh: 0, pulses_2s: 0, cps: 0, battery: 0, temp_c: 0, flags: '', n: 1 },
        { ts: 0, kind: 0, dose_msv: 0, doserate_usvh: 0, pulses_2s: 0, cps: 0, battery: 0, temp_c: 0, flags: '', n: 1 },
        { ts: 1792853245, kind: 0, dose_msv: 0, doserate_usvh: 0, pulses_2s: 0, cps: 0, battery: 0, temp_c: 0, flags: '', n: 1 },
        { ts: 1792853255, kind: 0, dose_msv: 0, doserate_usvh: 0, pulses_2s: 0, cps: 0, battery: 0, temp_c: 0, flags: '', n: 1 }
    ];
    const rate2 = A.ratePerSec(rows);
    closeTo(rate2, 1/10, 1e-9);
});

// appendRows
check('appendRows', () => {
    const res1 = A.appendRows([1, 2, 3], [4, 5], 4);
    assert.deepStrictEqual(res1, [2, 3, 4, 5]);
    
    const res2 = A.appendRows([], [1], 10);
    assert.deepStrictEqual(res2, [1]);
    
    const a = [1, 2, 3];
    const b = [4, 5];
    A.appendRows(a, b, 4);
    assert.deepStrictEqual(a, [1, 2, 3]);
    assert.deepStrictEqual(b, [4, 5]);
});

// sinceFor
check('sinceFor', () => {
    const ring = { seq: 10000, oldest: 0, count: 10000 };
    
    assert.strictEqual(A.sinceFor(ring, 3600, 0.5, 4000), 7835);
    assert.strictEqual(A.sinceFor(ring, 3600, 0.5, 1000), 9000);
    assert.strictEqual(A.sinceFor(ring, 0, 0.5, 4000), 6000);
    assert.strictEqual(A.sinceFor(ring, 3600, 0, 4000), 6000);
    
    const ring2 = { seq: 100, oldest: 90, count: 10 };
    assert.strictEqual(A.sinceFor(ring2, 0, 0.5, 4000), 90);
    
    const ring3 = { seq: 100, oldest: 0, count: 100 };
    assert.strictEqual(A.sinceFor(ring3, 0, 0.5, 4000), 0);
});

// nextSince
check('nextSince', () => {
    assert.strictEqual(A.nextSince(0, 0, 10), 10);
    assert.strictEqual(A.nextSince(5, 20, 10), 30);
    assert.strictEqual(A.nextSince(50, 20, 10), 60);
});

// summarize
check('summarize', () => {
    const resEmpty = A.summarize([], 'doserate_usvh');
    assert.strictEqual(resEmpty.n, 0);
    assert.strictEqual(resEmpty.min, null);
    assert.strictEqual(resEmpty.max, null);
    assert.strictEqual(resEmpty.mean, null);
    assert.strictEqual(resEmpty.last, null);
    
    const res = A.summarize(rawParsed.rows, 'doserate_usvh');
    const oracle = oracleSummarize(rawParsed.rows, 'doserate_usvh');
    
    assert.strictEqual(res.n, oracle.n);
    assert.strictEqual(res.min, oracle.min);
    assert.strictEqual(res.max, oracle.max);
    closeTo(res.mean, oracle.mean, 1e-9);
    assert.strictEqual(res.last, oracle.last);
});

// Pipeline
check('pipeline', () => {
    const parsed = A.parseCsv(rawText);
    const split = A.splitKnown(parsed.rows);
    const windowed = A.windowRows(split.known, 3600);
    const downsampled = A.downsample(windowed, 'doserate_usvh', 120);
    
    assert.ok(downsampled.length > 0);
    assert.ok(downsampled.length <= 120);
    
    for (let i = 1; i < downsampled.length; i++) {
        assert.ok(downsampled[i].x >= downsampled[i-1].x);
    }
    
    const lastTs = split.known[split.known.length - 1].ts;
    for (let p of downsampled) {
        assert.ok(p.x >= lastTs - 3600);
        assert.ok(p.x <= lastTs);
    }
});

// --- End ---
// доп. кейсы по итогам мутационной приёмки (#SA-3)
check('niceTicks step 5', () => { assert.deepStrictEqual(A.niceTicks(0, 15, 5), [0, 5, 10, 15]); });
check('reserveSec full ring and zero rate', () => { assert.strictEqual(A.reserveSec(10, 10, 0), 0); });
check('downsample flat series has no duplicates', () => {
    const rows = []; for (let i = 0; i < 1000; i++) rows.push({ ts: 1792853245 + i, v: 7 });
    const r = A.downsample(rows, 'v', 100);
    assert.strictEqual(r.length, 50);
});

// e10: summarize не учитывает строки без значения поля (воздух есть не у каждой строки)
check('summarize skips missing and non-finite values', () => {
    const rows = [{ts:1, air_p: NaN}, {ts:2, air_p: 751.0}, {ts:3}, {ts:4, air_p: 752.0}, {ts:5, air_p: NaN}];
    const r = A.summarize(rows, 'air_p');
    assert.strictEqual(r.n, 2);
    assert.strictEqual(r.min, 751.0);
    assert.strictEqual(r.max, 752.0);
    assert.strictEqual(r.mean, 751.5);
    assert.strictEqual(r.last, 752.0);
    const none = A.summarize([{ts:1, air_p: NaN}, {ts:2}], 'air_p');
    assert.strictEqual(none.n, 0);
    assert.strictEqual(none.min, null);
    assert.strictEqual(none.mean, null);
    assert.strictEqual(none.last, null);
});

// e16i П.5: pickKind. Образец боевого вида — status старой прошивки (C:/Temp/atomfast_work/e8/e7_preota_status.json: нет поля enabled, agg.count = 109)
const OLD_STATUS = {"ready":1,"window_s":60,"samples":3263,"ts0_samples":0,"win_rejected":0,"agg_closed":109,"q_dropped":0,"bytes":4194300,"now_ts":1792859771,"raw":{"cap":181491,"limit":181491,"count":3263,"seq":3263,"oldest":0,"dropped":0,"rejected":0,"epoch":0},"agg":{"cap":20160,"limit":20160,"count":109,"seq":109,"oldest":0,"dropped":0,"rejected":0,"epoch":0}};
const withEn = (agg, cnt) => Object.assign({}, OLD_STATUS, { enabled: { raw: 1, agg: agg, env: 1 }, agg: Object.assign({}, OLD_STATUS.agg, { count: cnt }) });

check('pickKind: enabled x count (4 комбинации) - agg только при enabled.agg=1 и count>=2', () => {
    assert.strictEqual(A.pickKind(withEn(1, 109), null), 'agg');
    assert.strictEqual(A.pickKind(withEn(1, 0), null), 'raw');
    assert.strictEqual(A.pickKind(withEn(0, 109), null), 'raw');
    assert.strictEqual(A.pickKind(withEn(0, 0), null), 'raw');
});

check('pickKind: граница count 2 -> agg, count 1 -> raw; сохранённое окно/kind игнорируется при enabled', () => {
    assert.strictEqual(A.pickKind(withEn(1, 2), null), 'agg');
    assert.strictEqual(A.pickKind(withEn(1, 1), null), 'raw');
    assert.strictEqual(A.pickKind(withEn(1, 109), 3600), 'agg');
    assert.strictEqual(A.pickKind(withEn(0, 109), { windowSec: 86400, kind: 'agg' }), 'raw');
    assert.strictEqual(A.pickKind(withEn(1, 109), { windowSec: 3600, kind: 'raw' }), 'agg');
    const noAgg = withEn(1, 109); delete noAgg.agg;
    assert.strictEqual(A.pickKind(noAgg, null), 'raw');
});

check('pickKind: нет поля enabled (старая прошивка) -> как defaultKindForWindow: 3600 -> raw, иначе agg', () => {
    assert.strictEqual(A.pickKind(OLD_STATUS, 3600), 'raw');
    assert.strictEqual(A.pickKind(OLD_STATUS, { windowSec: 3600 }), 'raw');
    assert.strictEqual(A.pickKind(OLD_STATUS, 86400), 'agg');
    assert.strictEqual(A.pickKind(OLD_STATUS, 0), 'agg');
    assert.strictEqual(A.pickKind(OLD_STATUS, null), 'agg');
    assert.strictEqual(A.pickKind(null, 3600), 'raw');
    assert.strictEqual(A.pickKind(undefined, undefined), 'agg');
});

console.log(`TESTS: ${passed}/${passed + failed}`);
if (failed > 0) {
    process.exit(1);
} else {
    process.exit(0);
}
