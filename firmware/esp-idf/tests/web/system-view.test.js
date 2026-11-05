const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const AFU = require(path.join(__dirname, '..', '..', 'web', 'ui-common.js'));
const SRC = fs.readFileSync(process.env.HV_MODULE || path.join(__dirname, '..', '..', 'web', 'system-view.js'), 'utf8');
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }

function makeEnv(responses) {
  const els = {};
  const pills = [];
  const timers = [];
  const fetchCalls = [];
  const document = {
    visibilityState: 'visible',
    getElementById(id) {
      if (!els[id]) els[id] = { id, textContent: '', className: '', style: {} };
      return els[id];
    }
  };
  function setPill(cls, text) { pills.push({ cls, text }); }
  async function fetchJSON(url) {
    fetchCalls.push(url);
    const r = responses[url];
    if (typeof r === 'function') return r();
    if (r && typeof r === 'object' && '__err' in r) {
      throw Object.assign(new Error('x'), { status: r.__err });
    }
    return r;
  }
  function setInterval(fn, ms) { timers.push({ fn, ms }); return timers.length; }
  const ctx = vm.createContext({
    AFU, document, setInterval, String, Date, Promise,
    pills, setPill, fetchJSON, fetchCalls,
    console, setTimeout, clearTimeout,
    Math, JSON, Object, Array, Number, Boolean, RegExp, Error,
    parseInt, parseFloat, isNaN,
    globalThis: null
  });
  ctx.globalThis = ctx;
  vm.runInContext(SRC, ctx);
  return { ctx, els, pills, timers, fetchCalls, run: code => vm.runInContext(code, ctx) };
}

const ST = { fw_version: '0.1.0-e13', running_partition: 'ota_0', uptime_s: 3725, boot_count: 14, reset_reason: 'software', time_source: 'none', sntp_synced: false };
const SY = { free_heap: 3298560, min_free_heap: 3201024, psram_total: 8386560, psram_free: 7712768, int_free: 98304, int_min: 93215, flash_total: 1048576, flash_used: 317440, rssi: -58, ssid: 'demo-net', net_mode: 'sta' };

function checkAsync(name, fn) {
  return (async () => {
    total++;
    try { await fn(); passed++; console.log('ok  ' + name); }
    catch (e) { console.log('FAIL ' + name + ': ' + e.message); }
  })();
}

async function main() {
  // 1
  {
    const env = makeEnv({});
    env.ctx.ST = ST; env.ctx.SY = SY;
    env.run('renderSystem(ST, SY)');
    check('1 renderSystem basic', () => {
      assert.strictEqual(env.els.sy_fw.textContent, '0.1.0-e13');
      assert.strictEqual(env.els.sy_part.textContent, 'ota_0');
      assert.strictEqual(env.els.sy_up.textContent, '01:02:05');
      assert.strictEqual(env.els.sy_boot.textContent, '14');
      assert.strictEqual(env.els.sy_reset.textContent, 'software');
      assert.strictEqual(env.els.sy_net.textContent, 'sta');
      assert.strictEqual(env.els.sy_ssid.textContent, 'demo-net');
      assert.strictEqual(env.els.sy_sntp.textContent, 'нет');
      assert.strictEqual(env.els.sy_psram.textContent, '7.36 МиБ / 8.00 МиБ');
    });
  }
  // 1b (V-20): ap_pass_default показывается предупреждением
  {
    const env = makeEnv({});
    env.ctx.ST = ST; env.ctx.SY = Object.assign({}, SY, { ap_pass_default: true });
    env.run('renderSystem(ST, SY)');
    check('1b ap_pass_default true -> warn visible', () => { assert.strictEqual(env.els.sy_ap_warn.hidden, false); });
    env.ctx.SY = Object.assign({}, SY, { ap_pass_default: false });
    env.run('renderSystem(ST, SY)');
    check('1c ap_pass_default false -> hidden', () => { assert.strictEqual(env.els.sy_ap_warn.hidden, true); });
    env.ctx.SY = SY;
    env.run('renderSystem(ST, SY)');
    check('1d field absent -> hidden', () => { assert.strictEqual(env.els.sy_ap_warn.hidden, true); });
  }
  // 2
  {
    const env = makeEnv({});
    env.ctx.ST = ST; env.ctx.SY = SY;
    env.run('renderSystem(ST, SY)');
    check('2 pills', () => {
      assert.strictEqual(env.els.sy_heap_big.textContent, '3.15 МиБ');
      assert.strictEqual(env.els.sy_heap_big.className, 'big ok');
      assert.strictEqual(env.els.sy_int_big.textContent, '96.0 КиБ');
      assert.strictEqual(env.els.sy_int_big.className, 'big ok');
      assert.strictEqual(env.els.sy_flash_big.textContent, '30 %');
      assert.strictEqual(env.els.sy_flash_big.className, 'big ok');
      assert.strictEqual(env.els.sy_rssi_big.textContent, '-58 дБм');
      assert.strictEqual(env.els.sy_rssi_big.className, 'big ok');
    });
  }
  // 3
  {
    const env = makeEnv({});
    env.ctx.ST = ST; env.ctx.SY = SY;
    env.run('renderSystem(ST, SY)');
    check('3 bars', () => {
      assert.strictEqual(env.els.sy_flash_bar.style.width, '30%');
      assert.strictEqual(env.els.sy_flash_barwrap.className, 'bar');
      assert.strictEqual(env.els.sy_rssi_bar.style.width, '84%');
      assert.strictEqual(env.els.sy_rssi_barwrap.className, 'bar');
      assert.strictEqual(env.els.sy_rssi_label.textContent, 'хороший');
      assert.strictEqual(env.els.sy_flash_txt.textContent, '310.0 КиБ из 1.00 МиБ');
    });
  }
  // 4
  {
    const SY2 = Object.assign({}, SY, { flash_used: 950000, flash_total: 1000000, rssi: -80, int_free: 20000 });
    const env = makeEnv({});
    env.ctx.ST = ST; env.ctx.SY = SY2;
    env.run('renderSystem(ST, SY)');
    check('4a bad levels', () => {
      assert.strictEqual(env.els.sy_flash_big.className, 'big bad');
      assert.strictEqual(env.els.sy_flash_barwrap.className, 'bar bad');
      assert.strictEqual(env.els.sy_flash_bar.style.width, '95%');
      assert.strictEqual(env.els.sy_rssi_big.className, 'big bad');
      assert.strictEqual(env.els.sy_rssi_barwrap.className, 'bar bad');
      assert.strictEqual(env.els.sy_int_big.className, 'big bad');
    });
    const SY3 = Object.assign({}, SY, { flash_used: 800000, flash_total: 1000000, rssi: -70 });
    const env3 = makeEnv({});
    env3.ctx.ST = ST; env3.ctx.SY = SY3;
    env3.run('renderSystem(ST, SY)');
    check('4b warn levels', () => {
      assert.strictEqual(env3.els.sy_flash_barwrap.className, 'bar warn');
      assert.strictEqual(env3.els.sy_rssi_barwrap.className, 'bar warn');
      assert.strictEqual(env3.els.sy_flash_big.className, 'big warn');
    });
  }
  // 5
  {
    const env = makeEnv({});
    env.run('renderSystem(null, null)');
    check('5 no data', () => {
      assert.strictEqual(env.els.sy_fw.textContent, '\u2014');
      assert.strictEqual(env.els.sy_flash_big.textContent, '\u2014');
      assert.strictEqual(env.els.sy_flash_big.className, 'big');
      assert.strictEqual(env.els.sy_flash_bar.style.width, '0%');
      assert.strictEqual(env.els.sy_rssi_bar.style.width, '0%');
      assert.strictEqual(env.els.sy_heap_big.className, 'big');
    });
  }
  // 6
  {
    const env = makeEnv({});
    env.run("setSysResult('ok', 'готово')");
    check('6 setSysResult', () => {
      assert.strictEqual(env.els.sysResult.className, 'result ok');
      assert.strictEqual(env.els.sysResult.textContent, 'готово');
    });
  }
  // 7
  // 6b: levels are taken from the matching field (heap vs internal vs flash vs rssi)
  {
    const env = makeEnv({});
    env.ctx.ST = ST; env.ctx.SY = Object.assign({}, SY, { free_heap: 70000 });
    env.run('renderSystem(ST, SY)');
    check('6b heap level independent of int', () => {
      assert.strictEqual(env.els.sy_heap_big.className, 'big warn');
      assert.strictEqual(env.els.sy_int_big.className, 'big ok');
    });
    const env2 = makeEnv({});
    env2.ctx.ST = ST; env2.ctx.SY = Object.assign({}, SY, { rssi: -80, int_free: 50000 });
    env2.run('renderSystem(ST, SY)');
    check('6c rssi level independent of flash', () => {
      assert.strictEqual(env2.els.sy_rssi_big.className, 'big bad');
      assert.strictEqual(env2.els.sy_flash_big.className, 'big ok');
      assert.strictEqual(env2.els.sy_int_big.className, 'big warn');
      assert.strictEqual(env2.els.sy_heap_big.className, 'big ok');
    });
  }
  // 7: wifi/ble rows of the Status section (moved from the old home page)
  {
    const env = makeEnv({});
    env.ctx.ST = Object.assign({}, ST, { wifi_connected: true, ble_ready: false });
    env.ctx.SY = SY;
    env.run('renderSystem(ST, SY)');
    check('7 wifi and ble rows', () => {
      assert.strictEqual(env.els.sy_wifi.textContent, 'да');
      assert.strictEqual(env.els.sy_ble.textContent, 'нет');
    });
    const env2 = makeEnv({});
    env2.run('renderSystem(null, null)');
    check('7b wifi and ble rows without data', () => {
      assert.strictEqual(env2.els.sy_wifi.textContent, '—');
      assert.strictEqual(env2.els.sy_ble.textContent, '—');
    });
  }
  console.log('TESTS: ' + passed + '/' + total);
  process.exit(passed === total ? 0 : 1);
}

main().catch(e => { console.error(e); process.exit(1); });
