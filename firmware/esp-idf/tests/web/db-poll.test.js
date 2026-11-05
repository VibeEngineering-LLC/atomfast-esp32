'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');
const MODEL = require('../../web/push-model.js');
const live = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures/push-live-idle.json'), 'utf8'));

function simulation(options = {}) {
  let clock = 0;
  const timers = [];
  const calls = [];
  const elements = new Map();
  const sections = ['db', 'wifi'].map(id => {
    const section = { hidden: true, getAttribute: () => id };
    elements.set('sec-' + id, section);
    return section;
  });
  const getElement = id => {
    if (!elements.has(id)) elements.set(id, { id, value: '', checked: false, style: {},
      addEventListener() {}, focus() {}, getAttribute() { return ''; } });
    return elements.get(id);
  };
  const document = { visibilityState: 'visible', activeElement: null,
    getElementById: getElement, querySelectorAll: selector => selector === '.sec' ? sections : [],
    querySelector: () => null, addEventListener() {} };
  const ctx = vm.createContext({
    document, AFPUSH: MODEL, console,
    AFSEC: { IDS: ['db', 'wifi'], needs: id => id === 'db' ? ['status'] : ['status', 'system'],
      fromHash: hash => hash.slice(1), pick: hash => hash.slice(1) },
    localStorage: { getItem() {}, setItem() {} }, location: { hash: '#db' },
    history: { replaceState() {} }, window: { addEventListener() {} },
    setPill() {},
    setInterval(fn, ms) { timers.push({ fn, ms, at: clock + ms }); return timers.length; },
    setTimeout(fn, ms) { timers.push({ fn, at: clock + ms }); return timers.length; },
    fetchJSON(url) {
      calls.push({ url, at: clock });
      return new Promise((resolve, reject) => timers.push({ at: clock + (options.delay || 20), fn() {
        if (url === '/api/push' && options.error) reject(options.error);
        else resolve(url === '/api/push' ? live : {});
      } }));
    },
    postJSON: async () => ({ ok: true, data: {} })
  });
  vm.runInContext(fs.readFileSync(process.env.DB_PUSH_MODULE || path.join(__dirname, '../../web/push-ui.js'), 'utf8'), ctx);
  vm.runInContext(fs.readFileSync(process.env.DB_NAV_MODULE || path.join(__dirname, '../../web/system-nav.js'), 'utf8'), ctx);
  vm.runInContext('renderBadges = function () {}; renderSystem = function () {}; initPush(); initSystemView();', ctx);
  async function advance(to) {
    await new Promise(setImmediate);
    while (timers.some(t => t.at <= to)) {
      timers.sort((a, b) => a.at - b.at);
      const timer = timers.shift();
      clock = timer.at;
      if (timer.ms) { timer.at += timer.ms; timers.push(timer); }
      timer.fn();
      await new Promise(setImmediate);
    }
    clock = to;
  }
  return { ctx, calls, document, advance, getElement, options };
}

test('visible DB has one periodic GET every 5 seconds over a controlled minute', async () => {
  const env = simulation();
  await env.advance(60030);
  const requests = env.calls.filter(c => c.url === '/api/push');
  console.log('DB_POLL_MEASUREMENT ' + JSON.stringify(requests.map(c => c.at)));
  assert.equal(requests.length, 13);
  assert.ok(requests.slice(1).every((c, i) => c.at - requests[i].at === 5000));
});

test('hidden page and hidden DB stop periodic GET, opening DB refreshes immediately', async () => {
  const env = simulation();
  await env.advance(100);
  env.ctx.activate('wifi');
  await env.advance(15000);
  assert.equal(env.calls.filter(c => c.url === '/api/push').length, 1);
  env.ctx.activate('db');
  await env.advance(15100);
  assert.equal(env.calls.filter(c => c.url === '/api/push').at(-1).at, 15000);
  env.document.visibilityState = 'hidden';
  await env.advance(30000);
  assert.equal(env.calls.filter(c => c.url === '/api/push').length, 2);
});

test('slow requests never overlap, errors recover and force waits then starts a fresh GET', async () => {
  const env = simulation({ delay: 6000, error: new Error('offline') });
  await env.advance(12000);
  assert.equal(env.calls.filter(c => c.url === '/api/push').length, 2);
  env.options.error = null;
  const forced = env.ctx.pushRefresh(true);
  await env.advance(24000);
  await forced;
  assert.equal(env.getElement('p_state').textContent, MODEL.stateText(live));
  const push = env.calls.filter(c => c.url === '/api/push');
  assert.ok(push.slice(1).every((c, i) => c.at - push[i].at >= 6000));
});

test('periodic refresh preserves unsaved input', async () => {
  const env = simulation();
  await env.advance(100);
  env.getElement('pushHost').value = '192.0.2.99';
  env.ctx.pushDirty = true;
  await env.advance(10030);
  assert.equal(env.getElement('pushHost').value, '192.0.2.99');
});
