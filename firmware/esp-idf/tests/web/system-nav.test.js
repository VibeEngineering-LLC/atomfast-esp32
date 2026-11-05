const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const W = (n) => path.join(__dirname, '..', '..', 'web', n);
const AFU = require(W('ui-common.js'));
const AFHM = require(W('home-model.js'));
const AFSEC = require(W('sections.js'));
const AFH = require(W('history-core.js'));
const SRC = fs.readFileSync(process.env.HN_MODULE || W('system-nav.js'), 'utf8');
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }
async function checkAsync(name, fn) { total++; try { await fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }

class El {
  constructor(tag) {
    this.tagName = tag;
    this.className = '';
    this._t = '';
    this.hidden = false;
    this.style = {};
    this.children = [];
    this.attrs = {};
  }
  get textContent() { return this._t; }
  set textContent(v) { this._t = v; if (v === '') this.children = []; }
  appendChild(c) { this.children.push(c); return c; }
  setAttribute(k, v) { this.attrs[k] = String(v); }
  getAttribute(k) { return (k in this.attrs) ? this.attrs[k] : null; }
  removeAttribute(k) { delete this.attrs[k]; }
  querySelector(sel) {
    const cls = sel.replace('.', '');
    for (const c of this.children) {
      if (c.className.split(' ').includes(cls)) return c;
      const r = c.querySelector(sel);
      if (r) return r;
    }
    return null;
  }
  addEventListener() {}
}

function makeEnv(opts) {
  const els = {};
  const secs = AFSEC.IDS.map(id => { const e = new El('div'); e.attrs['data-sec'] = id; return e; });
  const links = AFSEC.IDS.map(id => { const e = new El('a'); e.attrs['data-sec'] = id; return e; });
  const document = {
    getElementById(id) { if (!els[id]) els[id] = new El('div'); return els[id]; },
    createElement(tag) { return new El(tag); },
    querySelector(sel) { const m = /data-sec="(\w+)"/.exec(sel); return m ? (links.find(l => l.attrs['data-sec'] === m[1]) || null) : null; },
    querySelectorAll(sel) { if (sel === '.sec') return secs; if (sel === '#sidelist a[data-sec]') return links; return []; },
    visibilityState: 'visible'
  };
  ['c_ble', 'c_wifi', 'c_mqtt', 'c_time'].forEach(id => {
    const e = new El('div');
    const dEl = new El('span'); dEl.className = 'dot';
    const tEl = new El('span'); tEl.className = 't';
    e.appendChild(dEl);
    e.appendChild(tEl);
    els[id] = e;
  });
  const location = { hash: opts.hash || '' };
  const storage = {};
  if (opts.stored) storage['af.sys.section'] = opts.stored;
  const localStorage = {
    getItem: k => (k in storage ? storage[k] : null),
    setItem: (k, v) => { storage[k] = String(v); }
  };
  const history = { replaced: [], replaceState(a, b, url) { this.replaced.push(url); } };
  const window = { listeners: {}, addEventListener(ev, fn) { this.listeners[ev] = fn; } };
  const timers = [];
  const setInterval = (fn, ms) => { timers.push({ fn, ms }); return timers.length; };
  const calls = [];
  const fetchJSON = async (url) => {
    calls.push(url);
    if (opts.responses && url in opts.responses) {
      const r = opts.responses[url];
      if (r instanceof Error) throw r;
      if (r && r.__err) throw Object.assign(new Error('x'), { status: r.__err });
      return r;
    }
    throw Object.assign(new Error('x'), { status: 404 });
  };
  const fetchText = async (url) => { calls.push(url); return { text: typeof opts.csv === 'function' ? opts.csv(url) : opts.csv || 'ts,kind\n1792859805,0\n', seq: '1' }; };
  const pills = [];
  const setPill = (cls, text) => pills.push({ cls, text });
  const renderCalls = { system: [], store: [] };
  const renderSystem = (st, sy) => renderCalls.system.push({ st, sy });
  const renderStore = (hs) => renderCalls.store.push(hs);
  const state = { rows: [] };
  const ctx = vm.createContext({
    AFU, AFHM, AFSEC, AFH, document, location, localStorage, history, window, setInterval,
    fetchJSON, fetchText, setPill, renderSystem, renderStore, state,
    String, Number, Math, Date, Array, Object, JSON, Promise, isFinite
  });
  vm.runInContext(SRC, ctx);
  return { ctx, els, secs, links, location, storage, history, window, timers, calls, pills, renderCalls, state, run: code => vm.runInContext(code, ctx) };
}

const ST = { wifi_connected:true, ble_connected:true, ble_ready:true, ble_errors:0, ble_reconnects:2, ble_rx_total:100, ble_bad_total:1, ble_last_rec:{ age_ms:2500 }, fw_version:'0.1.0-e13', sntp_synced:true, time_source:'sntp', mqtt:{ enabled:1, connected:1, published:24, failed:0, last_err:'' } };
const SY = { rssi:-58, free_heap:3298560, int_free:98304, flash_total:1048576, flash_used:317440 };
const SN = { sensor:{ status:'ok', model:'BMP280', addr:'0x76', temp:true, press:true, t_c:26.5, p_mmhg:751.7, last_ok_s_ago:14 }, live:{ has_rec:true, cps:0.5, rate_usvh:0.11, rate_urh:11.0, dose_usv:1.234, avg_cps:0.45 } };
const HS = { raw:{ count:100, limit:1000, oldest:5, seq:105 }, persist:{ flush_errs:0 } };
const RESP = { '/api/status': ST, '/api/system': SY, '/api/sensors': SN, '/api/history/status': HS };

async function main() {
  await checkAsync('activate', async () => {
    const env = makeEnv({ responses: RESP });
    await env.run("activate('storage')");
    await new Promise(r => setTimeout(r, 20));
    const sec = env.secs.find(s => s.attrs['data-sec'] === 'storage');
    assert.strictEqual(sec.hidden, false);
    env.secs.forEach(s => { if (s.attrs['data-sec'] !== 'storage') assert.strictEqual(s.hidden, true); });
    const link = env.links.find(l => l.attrs['data-sec'] === 'storage');
    assert.strictEqual(link.attrs['aria-current'], 'true');
    env.links.forEach(l => { if (l.attrs['data-sec'] !== 'storage') assert.ok(!('aria-current' in l.attrs)); });
    assert.strictEqual(env.storage['af.sys.section'], 'storage');
    assert.strictEqual(env.run('sysCur'), 'storage');
  });

  await checkAsync('activate_unknown', async () => {
    const env = makeEnv({ responses: RESP });
    await env.run("activate('zzz')");
    await new Promise(r => setTimeout(r, 20));
    assert.strictEqual(env.run('sysCur'), 'status');
    const sec = env.secs.find(s => s.attrs['data-sec'] === 'status');
    assert.strictEqual(sec.hidden, false);
  });

  await checkAsync('data_wifi_system', async () => {
    const env = makeEnv({ responses: RESP });
    await env.run("activate('wifi')");
    await new Promise(r => setTimeout(r, 20));
    assert.ok(env.calls.includes('/api/status'));
    assert.ok(env.calls.includes('/api/system'));
    assert.ok(!env.calls.includes('/api/sensors'));
    assert.strictEqual(env.renderCalls.system.length, 1);
    assert.deepStrictEqual(env.renderCalls.system[0].st, ST);
    assert.deepStrictEqual(env.renderCalls.system[0].sy, SY);
    const env2 = makeEnv({ responses: RESP });
    await env2.run("activate('maint')");
    await new Promise(r => setTimeout(r, 20));
    assert.ok(env2.calls.includes('/api/status'));
    assert.ok(env2.calls.includes('/api/system'));
  });

  await checkAsync('section_storage', async () => {
    const env = makeEnv({ responses: RESP });
    await env.run("activate('storage')");
    await new Promise(r => setTimeout(r, 20));
    assert.ok(env.calls.includes('/api/history/status'));
    assert.ok(env.calls.some(c => c.startsWith('/api/history.csv?kind=raw&since=5&limit=1')));
    assert.ok(env.calls.some(c => c.startsWith('/api/history.csv?kind=raw&since=104&limit=1')));
    assert.ok(env.calls.includes('/api/status'));
    assert.strictEqual(env.renderCalls.store.length, 1);
    assert.deepStrictEqual(env.renderCalls.store[0], HS);
    assert.strictEqual(env.state.spans.raw, 0);   // оба запроса возвращают одну метку
  });

  await checkAsync('ble_mqtt', async () => {
    const env = makeEnv({ responses: RESP });
    await env.run("activate('ble')");
    await new Promise(r => setTimeout(r, 20));
    assert.strictEqual(env.els.b_conn.textContent, 'да');
    assert.strictEqual(env.els.b_ready.textContent, 'да');
    assert.strictEqual(env.els.b_reconn.textContent, '2');
    assert.strictEqual(env.els.b_rx.textContent, '100');
    assert.strictEqual(env.els.b_bad.textContent, '1');
    assert.strictEqual(env.els.b_err.textContent, '0');
    assert.strictEqual(env.els.b_age.textContent, '3 с');
    const env2 = makeEnv({ responses: RESP });
    await env2.run("activate('mqtt')");
    await new Promise(r => setTimeout(r, 20));
    assert.strictEqual(env2.els.m_en.textContent, 'да');
    assert.strictEqual(env2.els.m_conn.textContent, 'да');
    assert.strictEqual(env2.els.m_pub.textContent, '24');
    assert.strictEqual(env2.els.m_fail.textContent, '0');
    assert.strictEqual(env2.els.m_err.textContent, '\u2014');
  });

  await checkAsync('sensors', async () => {
    const env = makeEnv({ responses: RESP });
    await env.run("activate('sensors')");
    await new Promise(r => setTimeout(r, 20));
    assert.ok(env.calls.includes('/api/sensors'));
    assert.ok(!env.calls.includes('/api/status'));
    assert.strictEqual(env.els.sensorGroups.children.length, 1);
    const g = env.els.sensorGroups.children[0];
    assert.strictEqual(g.children[0].textContent, 'BMP280 · 1 шт.');
    assert.strictEqual(g.children[1].children.length, 1);
    const card = g.children[1].children[0];
    assert.strictEqual(card.className, 'scard');
    const shead = card.children[0];
    assert.strictEqual(shead.children[0].textContent, '#1');
    assert.strictEqual(shead.children[1].textContent, 'BMP280 (0x76)');
    assert.strictEqual(shead.children[2].className, 'badge ok');
    assert.strictEqual(shead.children[2].textContent, 'работает');
    const svals = card.children.filter(c => c.className === 'sval');
    assert.strictEqual(svals.length, 2);
    assert.strictEqual(card.children[card.children.length - 1].className, 'sage');
    assert.strictEqual(card.children[card.children.length - 1].textContent, 'измерено 14 с назад');
  });

  await checkAsync('badges', async () => {
    const env = makeEnv({ responses: RESP });
    await env.run("activate('wifi')");
    await new Promise(r => setTimeout(r, 20));
    assert.strictEqual(env.els.nb_wifi.className, 'nb');
    assert.strictEqual(env.els.nb_status.className, 'nb');
    const ST2 = Object.assign({}, ST, { wifi_connected: false });
    const env2 = makeEnv({ responses: { '/api/status': ST2, '/api/system': SY } });
    await env2.run("activate('wifi')");
    await new Promise(r => setTimeout(r, 20));
    assert.strictEqual(env2.els.nb_status.className, 'nb bad');
    const link = env2.links.find(l => l.attrs['data-sec'] === 'status');
    assert.strictEqual(link.attrs['data-level'], 'bad');
    const ST3 = Object.assign({}, ST, { ble_errors: 3 });
    const env3 = makeEnv({ responses: { '/api/status': ST3, '/api/system': SY } });
    await env3.run("activate('ble')");
    await new Promise(r => setTimeout(r, 20));
    assert.strictEqual(env3.els.nb_ble.textContent, '3');
    assert.strictEqual(env3.els.nb_ble.className, 'nb warn');
  });

  await checkAsync('errors', async () => {
    const env = makeEnv({ responses: { '/api/status': { __err: 401 } } });
    await env.run("activate('status')");
    await new Promise(r => setTimeout(r, 20));
    assert.strictEqual(env.els.sysNote.textContent, 'Нужна авторизация');
    assert.deepStrictEqual(env.pills[env.pills.length - 1], { cls: 'warn', text: 'нужна авторизация' });
    const env2 = makeEnv({ responses: { '/api/status': { __err: 500 } } });
    await env2.run("activate('status')");
    await new Promise(r => setTimeout(r, 20));
    assert.strictEqual(env2.els.sysNote.textContent, 'ошибка платы (HTTP 500)');
    assert.deepStrictEqual(env2.pills[env2.pills.length - 1], { cls: 'bad', text: 'ошибка платы (HTTP 500)' });
    const env3 = makeEnv({ responses: RESP });
    await env3.run("activate('status')");
    await new Promise(r => setTimeout(r, 20));
    assert.strictEqual(env3.els.sysNote.textContent, '');
    assert.strictEqual(env3.pills[env3.pills.length - 1].cls, 'ok');
    assert.ok(/^обновлено \d\d:\d\d:\d\d$/.test(env3.pills[env3.pills.length - 1].text));
    const env4 = makeEnv({ responses: RESP });
    env4.run("renderSystem = function () { throw new Error('render'); }");
    await env4.run("activate('wifi')");
    await new Promise(r => setTimeout(r, 20));
    assert.deepStrictEqual(env4.pills[env4.pills.length - 1], { cls: 'bad', text: 'ошибка отображения данных' });
  });

  await checkAsync('status_failure_and_recovery', async () => {
    for (const [error, text] of [
      [Object.assign(new Error(), { name: 'AbortError' }), 'плата не ответила вовремя'],
      [new SyntaxError(), 'некорректный ответ платы'],
      [new TypeError(), 'нет связи с платой'],
    ]) {
      const responses = { ...RESP, '/api/status': error };
      const env = makeEnv({ responses });
      env.run("activate('db')");
      await new Promise(setImmediate);
      assert.deepStrictEqual(env.pills.at(-1), { cls: 'bad', text });
      assert.strictEqual(env.run('sysBusy'), false);
      responses['/api/status'] = ST;
      env.run('refresh()');
      await new Promise(setImmediate);
      assert.strictEqual(env.pills.at(-1).cls, 'ok');
      assert.strictEqual(env.els.sysNote.textContent, '');
    }
  });

  await checkAsync('ble_render_failure_is_not_network_failure', async () => {
    const env = makeEnv({ responses: { ...RESP, '/api/ble': {} } });
    env.run("blPush = function () { throw new Error('render'); }; activate('ble')");
    await new Promise(setImmediate);
    assert.deepStrictEqual(env.pills.at(-1), { cls: 'bad', text: 'ошибка отображения данных' });
  });

  await checkAsync('overlap_protection', async () => {
    const env = makeEnv({ responses: RESP });
    env.run('refresh()');
    env.run('refresh()');
    await new Promise(r => setTimeout(r, 20));
    const count = env.calls.filter(c => c === '/api/status').length;
    assert.strictEqual(count, 1);
  });

  await checkAsync('initSystemView', async () => {
    const env = makeEnv({ hash: '#mqtt', stored: 'ble', responses: RESP });
    env.run('initSystemView()');
    assert.strictEqual(env.run('sysCur'), 'mqtt');
    assert.deepStrictEqual(env.history.replaced, []);
    assert.strictEqual(typeof env.window.listeners.hashchange, 'function');
    assert.strictEqual(env.timers.length, 1);
    assert.strictEqual(env.timers[0].ms, 5000);
    const env2 = makeEnv({ hash: '', stored: 'ble', responses: RESP });
    env2.run('initSystemView()');
    assert.strictEqual(env2.run('sysCur'), 'ble');
    assert.deepStrictEqual(env2.history.replaced, ['#ble']);
    const env3 = makeEnv({ hash: '#bad', responses: RESP });
    env3.run('initSystemView()');
    assert.strictEqual(env3.run('sysCur'), 'status');
    assert.deepStrictEqual(env3.history.replaced, ['#status']);
    env.location.hash = '#storage';
    env.window.listeners.hashchange();
    assert.strictEqual(env.run('sysCur'), 'storage');
    const before = env.run('sysCur');
    env.location.hash = '#nonsense';
    env.window.listeners.hashchange();
    assert.strictEqual(env.run('sysCur'), before);
  });

  await checkAsync('timer_hidden_tab', async () => {
    const env = makeEnv({ hash: '#mqtt', stored: 'ble', responses: RESP });
    env.run('initSystemView()');
    await new Promise(r => setTimeout(r, 20));
    env.ctx.document.visibilityState = 'hidden';
    const before = env.calls.length;
    env.timers[0].fn();
    await new Promise(r => setTimeout(r, 20));
    assert.strictEqual(env.calls.length, before);
    env.ctx.document.visibilityState = 'visible';
    env.timers[0].fn();
    await new Promise(r => setTimeout(r, 20));
    assert.ok(env.calls.length > before);
  });

  await checkAsync('section switch during refresh', async () => {
    const env = makeEnv({ responses: RESP });
    env.run("activate('status')");
    env.run("activate('storage')");
    await new Promise(r => setTimeout(r, 60));
    assert.strictEqual(env.run('sysCur'), 'storage');
    assert.strictEqual(env.renderCalls.store.some(h => h && h.raw && h.raw.count === 100), true);
  });

  await checkAsync('storage rows order and small ring', async () => {
    const csv = url => (url.indexOf('since=5&') !== -1 ? 'ts,kind' + String.fromCharCode(10) + '1792859805,0' : 'ts,kind' + String.fromCharCode(10) + '1792860805,0');
    const env = makeEnv({ responses: RESP, csv });
    await env.run("activate('storage')");
    await new Promise(r => setTimeout(r, 40));
    assert.strictEqual(env.state.spans.raw, 1000);
    const HS1 = { raw: { count: 1, limit: 1000 }, persist: { flush_errs: 0 } };
    const env2 = makeEnv({ responses: Object.assign({}, RESP, { '/api/history/status': HS1 }) });
    await env2.run("activate('storage')");
    await new Promise(r => setTimeout(r, 40));
    assert.strictEqual(env2.calls.filter(c => c.indexOf('/api/history.csv') === 0).length, 0);
  });

  await checkAsync('sensors empty note and mqtt connected flag', async () => {
    const env = makeEnv({ responses: Object.assign({}, RESP, { '/api/sensors': {} }) });
    await env.run("activate('sensors')");
    await new Promise(r => setTimeout(r, 40));
    assert.strictEqual(env.els.sensorNote.textContent, 'Датчики среды не обнаружены');
    const ST4 = Object.assign({}, ST, { mqtt: { enabled: 1, connected: 0, published: 3, failed: 0, last_err: 'auth' } });
    const env2 = makeEnv({ responses: Object.assign({}, RESP, { '/api/status': ST4 }) });
    await env2.run("activate('mqtt')");
    await new Promise(r => setTimeout(r, 40));
    assert.strictEqual(env2.els.m_conn.textContent, 'нет');
    assert.strictEqual(env2.els.m_err.textContent, 'auth');
  });

  console.log('TESTS: ' + passed + '/' + total);
  process.exit(passed === total ? 0 : 1);
}

main().catch(e => { console.error(e); process.exit(1); });
