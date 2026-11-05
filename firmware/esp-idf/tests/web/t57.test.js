// T-57: P2-5 (устаревший ответ зума отбрасывается при обзорной загрузке), P2-6 (запас по времени среды от периода)
const fs = require('fs'), path = require('path'), assert = require('assert');
const src = fs.readFileSync(path.join(__dirname, '..', '..', 'web', 'history.src.html'), 'utf8');
let ok = 0, bad = 0;
function check(n, f) { try { f(); ok++; } catch (e) { bad++; console.log('FAIL ' + n + ': ' + e.message); } }
const fl = src.slice(src.indexOf('async function fullLoad()'));
const body = fl.slice(0, fl.indexOf('const status = await fetchJSON'));
check('P2-5 fullLoad увеличивает zlSeq и снимает таймер зума', () => {
  assert.ok(/zlSeq\+\+/.test(body));
  assert.ok(/clearTimeout\(zlTimer\)/.test(body));
});
check('P2-5 zoom-load отбрасывает ответ при смене zlSeq после загрузки', () => {
  const z = fs.readFileSync(path.join(__dirname, '..', '..', 'web', 'zoom-load.js'), 'utf8');
  const after = z.slice(z.indexOf('await fetchText(url)'));
  assert.ok(/if \(my !== zlSeq\) return;/.test(after.slice(0, 80)));
});
check('P2-6 период среды берётся из cfg.period_s, по умолчанию 300 (не 30)', () => {
  assert.ok(!/kind === 'env' \? 1 \/ 30/.test(src));
  assert.ok(/lastSensors\.cfg\.period_s : 300/.test(src));
});
console.log('TESTS: ' + ok + '/' + (ok + bad));
process.exit(bad ? 1 : 0);
