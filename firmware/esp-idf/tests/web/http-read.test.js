'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

const template = fs.readFileSync(path.join(__dirname, '../../web/history.src.html'), 'utf8');
const source = template.slice(template.indexOf('// ---- fetch with timeout ----'), template.indexOf('// ---- pill ----'));

function environment(fetch) {
  const timers = new Map();
  let next = 0;
  const ctx = vm.createContext({
    fetch, AbortController, FETCH_TIMEOUT: 15000,
    setTimeout(fn) { timers.set(++next, fn); return next; },
    clearTimeout(id) { timers.delete(id); }
  });
  vm.runInContext(source, ctx);
  return { ctx, timers };
}

function response(values = {}) {
  return { status: 200, ok: true, json: async () => ({ ok: true }), text: async () => 'csv',
    headers: new Headers(), ...values };
}

test('JSON and CSV retain request options, status and pagination contracts', async () => {
  const requests = [];
  const { ctx, timers } = environment(async (url, opts) => {
    requests.push({ url, opts });
    return response({ headers: new Headers({ 'X-AF-Seq': '17', 'X-AF-Ds': 'next=18;bucket=9' }) });
  });
  assert.deepEqual(await ctx.fetchJSON('/api/status'), { ok: true });
  const headers = { 'X-Test': 'kept' };
  const options = { method: 'GET', cache: 'reload', credentials: 'omit', headers };
  assert.deepEqual(JSON.parse(JSON.stringify(await ctx.fetchText('/history.csv', options))),
    { text: 'csv', seq: '17', next: '18', ds: true });
  assert.equal(requests[0].opts.credentials, 'same-origin');
  assert.equal(requests[0].opts.cache, 'no-store');
  assert.equal(requests[1].opts.cache, 'reload');
  assert.equal(requests[1].opts.credentials, 'omit');
  assert.equal(requests[1].opts.headers, headers);
  assert.equal(options.signal, undefined);
  assert.equal(timers.size, 0);
});

test('CSV retains null pagination when response headers are absent or do not contain next', async () => {
  const { ctx } = environment(async () => response());
  assert.deepEqual(JSON.parse(JSON.stringify(await ctx.fetchText('/csv'))),
    { text: 'csv', seq: null, next: null, ds: false });
  const second = environment(async () => response({ headers: new Headers({ 'X-AF-Ds': 'bucket=5' }) }));
  assert.equal((await second.ctx.fetchText('/csv')).next, null);
  assert.equal((await second.ctx.fetchText('/csv')).ds, true);
});

test('401, 403 and 500 retain their status, do not decode bodies or retry', async () => {
  for (const method of ['fetchJSON', 'fetchText']) {
    for (const status of [401, 403, 500]) {
      let calls = 0;
      const { ctx, timers } = environment(async () => {
        calls++;
        return response({ ok: false, status,
          json() { throw new Error('must not read'); }, text() { throw new Error('must not read'); } });
      });
      await assert.rejects(ctx[method]('/api'), e => e.status === status
        && e.message === (status === 401 ? '401' : 'HTTP ' + status));
      assert.equal(calls, 1);
      assert.equal(timers.size, 0);
    }
  }
});

test('timeouts cover body parsing and preserve AbortError', async () => {
  for (const method of ['fetchJSON', 'fetchText']) {
    const { ctx, timers } = environment(async (url, opts) => response({
      json: () => waitForAbort(opts.signal), text: () => waitForAbort(opts.signal)
    }));
    const pending = ctx[method]('/slow-body');
    await new Promise(setImmediate);
    assert.equal(timers.size, 1);
    const rejected = assert.rejects(pending, e => e.name === 'AbortError');
    [...timers.values()][0]();
    await rejected;
    assert.equal(timers.size, 0);
  }
});

function waitForAbort(signal) {
  return new Promise((resolve, reject) => {
    if (signal.aborted) reject(signal.reason);
    else signal.addEventListener('abort', () => reject(signal.reason), { once: true });
  });
}

test('external abort works before and during a request, and removes its listener', async () => {
  for (const alreadyAborted of [false, true]) {
    const controller = new AbortController();
    if (alreadyAborted) controller.abort();
    let added = 0, removed = 0;
    const signal = {
      get aborted() { return controller.signal.aborted; },
      addEventListener(...args) { added++; controller.signal.addEventListener(...args); },
      removeEventListener(...args) { removed++; controller.signal.removeEventListener(...args); }
    };
    const { ctx, timers } = environment((url, opts) => waitForAbort(opts.signal));
    const pending = ctx.fetchJSON('/cancel', { signal });
    const rejected = assert.rejects(pending, e => e.name === 'AbortError');
    controller.abort();
    await rejected;
    assert.equal(added, alreadyAborted ? 0 : 1);
    assert.equal(removed, 1);
    assert.equal(timers.size, 0);
  }
});

test('JSON syntax, network and body read errors retain their identity and clean up', async () => {
  for (const error of [new SyntaxError('bad JSON'), new TypeError('offline'), new Error('body interrupted')]) {
    const bodyError = environment(async () => response({ json: async () => { throw error; } }));
    await assert.rejects(bodyError.ctx.fetchJSON('/api'), e => e === error);
    assert.equal(bodyError.timers.size, 0);
    const networkError = environment(async () => { throw error; });
    await assert.rejects(networkError.ctx.fetchText('/csv'), e => e === error);
    assert.equal(networkError.timers.size, 0);
  }
});

test('a completed request removes its external abort listener', async () => {
  const controller = new AbortController();
  const listeners = new Set();
  const signal = {
    aborted: false,
    addEventListener(type, callback) { listeners.add(callback); controller.signal.addEventListener(type, callback); },
    removeEventListener(type, callback) { listeners.delete(callback); controller.signal.removeEventListener(type, callback); }
  };
  const { ctx } = environment(async () => response());
  await ctx.fetchJSON('/api', { signal });
  assert.equal(listeners.size, 0);
});
