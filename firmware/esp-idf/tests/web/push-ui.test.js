'use strict';

const path = require('path');
const fs = require('fs');
const vm = require('vm');
const assert = require('assert');

const MODEL = require('../../web/push-model.js');
const MODULE_PATH = process.env.PUSHUI_MODULE || path.join(__dirname, '..', '..', 'web', 'push-ui.js');

// ─── Образцы: настоящие ответы GET /api/push (T-71) ─────────────────────────
// push-live-idle.json снят с платы «.131»; остальные напечатаны настоящей af_push_json()
// (tests/host/probe_push_json.c), а не написаны вручную.
const FX = n => Object.freeze(JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', n + '.json'), 'utf8')));
const LIVE = FX('push-live-idle');
const WORKING = FX('push-working');
const STOPPED_ERR = FX('push-stopped-error');
const STOPPED_SENDING = FX('push-stopped-sending');

// ─── Утилиты ──────────────────────────────────────────────────────────────────
function tick(times) {
  const n = times || 10;
  let p = Promise.resolve();
  for (let i = 0; i < n; i++) {
    p = p.then(() => new Promise(r => setImmediate(r)));
  }
  return p;
}

async function press(env, id) {
  env.els[id].click();
  await tick();
}

// Проверка: подстрока отсутствует в textContent / value / className всех элементов
function assertNoSubstring(env, sub) {
  const ids = Object.keys(env.els);
  for (const id of ids) {
    const el = env.els[id];
    if (typeof el.textContent === 'string' && el.textContent.includes(sub)) {
      throw new Error(`подстрока "${sub}" найдена в textContent #${id}`);
    }
    if (typeof el.value === 'string' && el.value.includes(sub)) {
      throw new Error(`подстрока "${sub}" найдена в value #${id}`);
    }
    if (typeof el.className === 'string' && el.className.includes(sub)) {
      throw new Error(`подстрока "${sub}" найдена в className #${id}`);
    }
  }
}

// ─── Поддельный DOM ───────────────────────────────────────────────────────────
const EL_IDS = [
  'p_en', 'p_state', 'p_conn', 'p_sent', 'p_fail', 'p_err',
  'p_ring_raw', 'p_ring_agg', 'p_ring_env',
  'pushNote', 'pushWarn',
  'pushEnabled', 'pushHost', 'pushPort', 'pushToken',
  'pushTokenHint', 'pushTokenClear',
  'btnPushSave', 'btnPushTest', 'btnPushReset',
  'pushBackdrop', 'pushDlg', 'pushDlgTitle', 'pushDlgText',
  'pushDlgCancel', 'pushDlgOk',
  'pushResult', 'pushTestResult', 'sec-db'
];

const INITIALLY_HIDDEN = new Set(['pushBackdrop', 'pushDlg', 'pushWarn']);

function makeEnv(opts) {
  const env = { els: {}, docH: {}, calls: [], timers: [], clock: 0 };

  for (const id of EL_IDS) {
    env.els[id] = {
      id,
      value: '',
      checked: false,
      hidden: INITIALLY_HIDDEN.has(id),
      disabled: false,
      textContent: '',
      className: '',
      _h: {},
      addEventListener(t, f) {
        (this._h[t] = this._h[t] || []).push(f);
      },
      focus() { env.document.activeElement = this; },
      click() { (this._h.click || []).forEach(f => f({})); }
    };
  }

  env.document = {
    activeElement: null,
    visibilityState: 'visible',
    getElementById(id) { return env.els[id] || null; },
    addEventListener(t, f) {
      (env.docH[t] = env.docH[t] || []).push(f);
    }
  };

  // fetchJSON
  let qi = 0;
  function fetchJSON(url) {
    env.calls.push({ m: 'GET', url });
    if (url === '/api/push') {
      const q = opts.getQueue || [];
      if (q.length === 0) throw new Error('getQueue exhausted');
      const item = q[Math.min(qi, q.length - 1)];
      qi++;
      if (item instanceof Error) { if (!item.status) env.clock += 15000; throw item; }   // сбой без статуса = таймаут fetch 15 с; сбой опроса; item может быть и Promise (отложенный ответ)
      return item;
    }
    throw new Error('fetchJSON: unexpected url ' + url);
  }

  // postJSON
  function postJSON(url, body) {
    env.calls.push({ m: 'POST', url, body: JSON.parse(JSON.stringify(body)) });
    const p = (opts.post && opts.post[url]) || { ok: true, status: 200, data: { ok: true } };
    if (p instanceof Error) throw p;
    return p;
  }

  // VM-контекст
  const ctx = vm.createContext({
    document: env.document,
    fetchJSON,
    postJSON,
    AFPUSH: MODEL,
    setInterval: function (f) { env.timers.push(f); return 0; },
    setTimeout: function (f, ms) { env.clock += ms || 0; f(); return 0; },   // виртуальные часы: ожидание добавляет время
    console
  });

  const src = fs.readFileSync(MODULE_PATH, 'utf8');
  vm.runInContext(src, ctx, { filename: 'push-ui.js' });

  env.ctx = ctx;
  env.fetchJSON = fetchJSON;
  env.postJSON = postJSON;
  return env;
}

// ─── Асинхронный чекер ────────────────────────────────────────────────────────
let passed = 0, total = 0;

async function check(name, fn) {
  total++;
  try {
    await fn();
    passed++;
    console.log('ok  ' + name);
  } catch (e) {
    console.log('FAIL ' + name + ': ' + (e && e.message || e));
  }
}

// ─── Тесты ────────────────────────────────────────────────────────────────────
(async function main() {

  // 1
  await check('форма: поля заполняются из ответа платы, токен в поле не подставляется', async () => {
    const opts = { getQueue: [Object.assign({}, WORKING, { token: 'SECRET-TOKEN-xyz-0123456789' })] };
    const env = makeEnv(opts);
    await env.ctx.initPush();
    await tick();

    assert.strictEqual(env.els.pushHost.value, '192.0.2.10');
    assert.strictEqual(env.els.pushPort.value, '8480');
    assert.strictEqual(env.els.pushEnabled.checked, true);
    assert.strictEqual(env.els.pushToken.value, '');
    assertNoSubstring(env, 'SECRET');
  });

  // 2
  await check('статус: карточка и подсказка токена по ответу платы', async () => {
    const env = makeEnv({ getQueue: [WORKING] });
    await env.ctx.initPush();
    await tick();

    assert.strictEqual(env.els.p_en.textContent, 'да');
    assert.strictEqual(env.els.p_conn.textContent, 'есть');
    assert.strictEqual(env.els.p_sent.textContent, '30');
    assert(env.els.p_ring_raw.textContent.includes('подтверждено до № 10'),
      'p_ring_raw.textContent = ' + JSON.stringify(env.els.p_ring_raw.textContent));
    assert(env.els.pushTokenHint.textContent.includes('токен задан'),
      'pushTokenHint.textContent = ' + JSON.stringify(env.els.pushTokenHint.textContent));
  });

  // 3
  await check('предупреждение о нелокальном адресе показывается только при public_addr', async () => {
    // public_addr: true → предупреждение видно
    const withWarn = Object.assign({}, WORKING, { warn: { no_tls: true, public_addr: true } });
    const envA = makeEnv({ getQueue: [withWarn] });
    await envA.ctx.initPush();
    await tick();
    assert.strictEqual(envA.els.pushWarn.hidden, false, 'pushWarn должно быть видимо при public_addr');

    // public_addr: false (WORKING) → предупреждение скрыто
    const envB = makeEnv({ getQueue: [WORKING] });
    await envB.ctx.initPush();
    await tick();
    assert.strictEqual(envB.els.pushWarn.hidden, true, 'pushWarn должно быть скрыто без public_addr');
  });

  // 4
  await check('сохранение: ошибочный порт не уходит на плату', async () => {
    const env = makeEnv({ getQueue: [LIVE] });
    await env.ctx.initPush();
    await tick();

    env.els.pushHost.value = '192.0.2.10';
    env.els.pushPort.value = 'abc';
    env.els.pushToken.value = 'tok_AbC123-xyz';
    env.els.pushEnabled.checked = true;

    await press(env, 'btnPushSave');

    const posts = env.calls.filter(c => c.m === 'POST');
    assert.strictEqual(posts.length, 0, 'POST не должен быть отправлен');
    assert(env.els.pushResult.className.includes('bad'), 'className должно содержать bad');
    assert(env.els.pushResult.textContent.includes('Порт'), 'текст должен содержать "Порт"');
  });

  // 5
  await check('сохранение: тело запроса без токена, если поле пустое', async () => {
    const env = makeEnv({ getQueue: [WORKING] });
    await env.ctx.initPush();
    await tick();

    // Поля уже заполнены из WORKING; токен пуст
    assert.strictEqual(env.els.pushToken.value, '');

    await press(env, 'btnPushSave');

    const posts = env.calls.filter(c => c.m === 'POST' && c.url === '/api/push');
    assert.strictEqual(posts.length, 1, 'один POST на /api/push');
    assert.deepStrictEqual(posts[0].body, { enabled: true, host: '192.0.2.10', port: 8480 });
  });

  // 6
  await check('сохранение: введённый токен уходит один раз и поле очищается', async () => {
    const env = makeEnv({ getQueue: [LIVE] });
    await env.ctx.initPush();
    await tick();

    env.els.pushEnabled.checked = true;
    env.els.pushHost.value = '192.0.2.10';
    env.els.pushPort.value = '8480';
    env.els.pushToken.value = 'tok_AbC123-xyz';

    await press(env, 'btnPushSave');

    const posts = env.calls.filter(c => c.m === 'POST' && c.url === '/api/push');
    assert.strictEqual(posts.length, 1);
    assert.strictEqual(posts[0].body.token, 'tok_AbC123-xyz');

    assert.strictEqual(env.els.pushToken.value, '', 'поле токена должно быть очищено');
    assertNoSubstring(env, 'tok_AbC123');
    assert(env.els.pushResult.className.includes('ok'), 'className должно содержать ok');
  });

  // 7
  await check('очистка токена: уходит clear_token, а не пустой token', async () => {
    const env = makeEnv({ getQueue: [WORKING] });
    await env.ctx.initPush();
    await tick();

    env.els.pushTokenClear.checked = true;
    env.els.pushEnabled.checked = false;

    await press(env, 'btnPushSave');

    const posts = env.calls.filter(c => c.m === 'POST' && c.url === '/api/push');
    assert.strictEqual(posts.length, 1);
    assert.strictEqual(posts[0].body.clear_token, true);
    assert(!('token' in posts[0].body), 'ключ token не должен присутствовать');
  });

  // 8
  await check('проверка без адреса: ответ платы incomplete показан по-русски', async () => {
    const opts = {
      getQueue: [LIVE],
      post: { '/api/push/test': { ok: false, status: 409, data: { ok: false, err: 'incomplete' } } }
    };
    const env = makeEnv(opts);
    await env.ctx.initPush();
    await tick();

    const callsBefore = env.calls.length;
    await press(env, 'btnPushTest');

    assert(env.els.pushTestResult.textContent.includes('не заданы адрес приёмника и токен'),
      'текст = ' + JSON.stringify(env.els.pushTestResult.textContent));
    assert(env.els.pushTestResult.className.includes('bad'));

    const getAfter = env.calls.slice(callsBefore).filter(c => c.m === 'GET');
    assert.strictEqual(getAfter.length, 0, 'не должно быть GET /api/push после ошибки старта');
  });

  // 9
  await check('проверка: ожидает результат и показывает его', async () => {
    const q = [
      WORKING,
      Object.assign({}, WORKING, { test: { state: 'running', http: 0, ms: 0, accepted: -1, err: '', ts: 0, legacy: false } }),
      Object.assign({}, WORKING, { test: { state: 'done', http: 200, ms: 42, accepted: 0, err: '', ts: 1793351100, legacy: false } })
    ];
    const env = makeEnv({ getQueue: q });
    await env.ctx.initPush();
    await tick();

    await press(env, 'btnPushTest');
    await tick(60);

    assert.strictEqual(env.els.pushTestResult.textContent,
      'Соединение работает: приёмник ответил (HTTP 200) за 42 мс');
    assert(env.els.pushTestResult.className.includes('ok'));
  });

  // 10
  await check('сброс: нажатие кнопки только открывает диалог и ничего не отправляет', async () => {
    const env = makeEnv({ getQueue: [WORKING] });
    await env.ctx.initPush();
    await tick();

    await press(env, 'btnPushReset');

    assert.strictEqual(env.els.pushDlg.hidden, false);
    assert.strictEqual(env.els.pushBackdrop.hidden, false);

    const posts = env.calls.filter(c => c.m === 'POST');
    assert.strictEqual(posts.length, 0, 'POST не должен быть отправлен');
    assert(env.els.pushDlgText.textContent.includes('Данные на плате не удаляются'),
      'текст диалога = ' + JSON.stringify(env.els.pushDlgText.textContent));
  });

  // 11
  await check('сброс: подтверждение отправляет confirm:true на /api/push/reset', async () => {
    const env = makeEnv({ getQueue: [WORKING] });
    await env.ctx.initPush();
    await tick();

    env.ctx.pushResetOpen();
    await tick();
    await press(env, 'pushDlgOk');

    const posts = env.calls.filter(c => c.m === 'POST' && c.url === '/api/push/reset');
    assert.strictEqual(posts.length, 1, 'ровно один POST на /api/push/reset');
    assert.deepStrictEqual(posts[0].body, { confirm: true });

    assert.strictEqual(env.els.pushDlg.hidden, true);
    assert(env.els.pushResult.textContent.includes('сброшена'),
      'pushResult = ' + JSON.stringify(env.els.pushResult.textContent));
  });

  // 12
  await check('сброс: отмена закрывает диалог без отправки', async () => {
    const env = makeEnv({ getQueue: [WORKING] });
    await env.ctx.initPush();
    await tick();

    env.ctx.pushResetOpen();
    await tick();
    assert.strictEqual(env.els.pushDlg.hidden, false);

    await press(env, 'pushDlgCancel');

    assert.strictEqual(env.els.pushDlg.hidden, true);
    const posts = env.calls.filter(c => c.m === 'POST');
    assert.strictEqual(posts.length, 0, 'POST не должен быть отправлен');
  });

  // 14 (T-71, P2-1): ожидание выводится из timeout_s платы, а не константой 20 с
  await check('проверка: ожидание рассчитано из timeout_s платы, а не 20 с', async () => {
    const running = Object.assign({}, WORKING, { test: { state: 'running', http: 0, ms: 0, accepted: -1, err: '', ts: 0, legacy: false } });
    const done = Object.assign({}, WORKING, { test: { state: 'done', http: 200, ms: 42, accepted: 0, err: '', ts: 1793351100, legacy: false } });
    const q = [WORKING];
    for (let i = 0; i < 25; i++) q.push(running);   // плата занята проходом по кольцам дольше 20 с
    q.push(done);
    const env = makeEnv({ getQueue: q });
    await env.ctx.initPush();
    await tick();
    await press(env, 'btnPushTest');
    await tick(120);
    assert.strictEqual(WORKING.timeout_s, 8);
    assert.strictEqual(env.els.pushTestResult.textContent,
      'Соединение работает: приёмник ответил (HTTP 200) за 42 мс');
  });
  // 15 (T-71, P2-1): повторное нажатие во время ожидания не подменяет результат
  await check('проверка: повторное нажатие во время ожидания не запускает вторую проверку', async () => {
    const running = Object.assign({}, WORKING, { test: { state: 'running', http: 0, ms: 0, accepted: -1, err: '', ts: 0, legacy: false } });
    const done = Object.assign({}, WORKING, { test: { state: 'done', http: 200, ms: 42, accepted: 0, err: '', ts: 1793351100, legacy: false } });
    const env = makeEnv({ getQueue: [WORKING, running, done],
      post: { '/api/push/test': { ok: true, status: 202, data: { ok: true, started: true } } } });
    await env.ctx.initPush();
    await tick();
    env.els.btnPushTest.click();
    env.els.btnPushTest.click();   // второе нажатие, пока первая проверка ещё ждёт
    await tick(60);
    const posts = env.calls.filter(c => c.m === 'POST' && c.url === '/api/push/test');
    assert.strictEqual(posts.length, 1);
    assert.strictEqual(env.els.pushTestResult.textContent,
      'Соединение работает: приёмник ответил (HTTP 200) за 42 мс');
  });
  // 16 (T-71, P2-2): остановленное кольцо в реальном ответе платы показано на странице
  await check('остановленное кольцо: строка «остановлено» с подсказкой и общий статус без «идёт отправка»', async () => {
    const env = makeEnv({ getQueue: [STOPPED_SENDING] });   // настоящий JSON: state=sending при остановленном raw
    await env.ctx.initPush();
    await tick();
    assert(env.els.p_ring_raw.textContent.startsWith('остановлено'), env.els.p_ring_raw.textContent);
    assert(env.els.p_ring_raw.textContent.includes('кнопка «Сохранить»'));
    assert(!env.els.p_ring_agg.textContent.includes('остановлено'));
    assert(!env.els.p_state.textContent.includes('идёт отправка'), env.els.p_state.textContent);
    assert(env.els.p_state.textContent.includes('остановлено'));
  });
  // 13
  await check('сброс: подтверждающее окно браузера не используется', async () => {
    const src = fs.readFileSync(MODULE_PATH, 'utf8');
    // Убираем строки, начинающиеся с // или * (комментарии)
    const lines = src.split('\n').filter(l => {
      const t = l.trim();
      return !t.startsWith('//') && !t.startsWith('*');
    });
    const code = lines.join('\n');

    assert(!code.includes('confirm('), 'найдена подстрока confirm(');
    assert(!code.includes('alert('), 'найдена подстрока alert(');
  });

  function typeIn(env, id, value) { env.els[id].value = value; (env.els[id]._h.input || []).forEach(f => f({})); }
  function failure(status) { const e = new Error(status ? 'HTTP ' + status : 'network'); if (status) e.status = status; return e; }

  // T-75 форма: ввод до первого заполнения не затирается периодическим обновлением (P2-1)
  await check('форма: ввод до первого заполнения не затирается периодическим обновлением', async () => {
    const env = makeEnv({ getQueue: [failure(0), WORKING] });
    await env.ctx.initPush();
    await tick();
    typeIn(env, 'pushHost', '203.0.113.5');
    typeIn(env, 'pushPort', '9001');
    env.document.activeElement = env.els.btnPushSave;
    env.timers[0]();
    await tick();
    assert.strictEqual(env.els.pushHost.value, '203.0.113.5');
    assert.strictEqual(env.els.pushPort.value, '9001');
    assert.strictEqual(env.els.p_en.textContent, 'да');
  });

  // T-75 сохранение: после успеха форма получает значения платы, даже если шло периодическое обновление (P2-1)
  await check('сохранение: после успеха форма получает значения платы, даже если шло периодическое обновление', async () => {
    let resolveOld;
    const old = new Promise(r => { resolveOld = r; });
    const board = Object.assign({}, WORKING, { host: '198.51.100.7', port: 8481 });   // плата вернула свои значения
    const env = makeEnv({ getQueue: [LIVE, old, board] });
    await env.ctx.initPush();
    await tick();
    env.timers[0]();
    typeIn(env, 'pushHost', '192.0.2.10');
    typeIn(env, 'pushPort', '8480');
    typeIn(env, 'pushToken', 'tok_AbC123-xyz');
    env.els.pushEnabled.checked = true;
    env.document.activeElement = env.els.btnPushSave;
    env.els.btnPushSave.click();
    await tick();
    resolveOld(LIVE);
    await tick(30);
    const posts = env.calls.filter(c => c.m === 'POST' && c.url === '/api/push');
    assert.strictEqual(posts.length, 1);
    assert.strictEqual(env.els.pushHost.value, '198.51.100.7');
    assert.strictEqual(env.els.pushPort.value, '8481');
    assert.strictEqual(env.els.pushEnabled.checked, true);
    assert.strictEqual(env.els.pushToken.value, '');
    const gets = env.calls.filter(c => c.m === 'GET' && c.url === '/api/push');
    assert.ok(gets.length >= 3, 'ожидается не меньше 3 GET /api/push, получено ' + gets.length);
  });

  // T-75 проверка: одиночный сбой опроса не обрывает ожидание (P2-2)
  await check('проверка: одиночный сбой опроса не обрывает ожидание', async () => {
    const running = Object.assign({}, WORKING, { test: { state: 'running', http: 0, ms: 0, accepted: -1, err: '', ts: 0, legacy: false } });
    const done = Object.assign({}, WORKING, { test: { state: 'done', http: 200, ms: 42, accepted: 0, err: '', ts: 1793351100, legacy: false } });
    const env = makeEnv({
      getQueue: [WORKING, failure(0), running, done],
      post: { '/api/push/test': { ok: true, status: 202, data: { ok: true, started: true } } }
    });
    await env.ctx.initPush();
    await tick();
    await press(env, 'btnPushTest');
    await tick(60);
    assert.strictEqual(env.els.pushTestResult.textContent, 'Соединение работает: приёмник ответил (HTTP 200) за 42 мс');
    assert.ok(env.els.pushTestResult.className.includes('ok'));
  });

  // T-75 проверка: 401 при опросе показан как «нужна авторизация» (P2-2)
  await check('проверка: 401 при опросе показан как «нужна авторизация»', async () => {
    const env = makeEnv({
      getQueue: [WORKING, failure(401)],
      post: { '/api/push/test': { ok: true, status: 202, data: { ok: true, started: true } } }
    });
    await env.ctx.initPush();
    await tick();
    await press(env, 'btnPushTest');
    await tick(60);
    assert.strictEqual(env.els.pushTestResult.textContent, 'нужна авторизация');
    assert.ok(env.els.pushTestResult.className.includes('bad'));
  });

  // T-75 проверка: 401 при запуске показан как «нужна авторизация»
  await check('проверка: 401 при запуске показан как «нужна авторизация»', async () => {
    const env = makeEnv({
      getQueue: [WORKING],
      post: { '/api/push/test': failure(401) }
    });
    await env.ctx.initPush();
    await tick();
    await press(env, 'btnPushTest');
    await tick(60);
    assert.strictEqual(env.els.pushTestResult.textContent, 'нужна авторизация');
    assert.ok(env.els.pushTestResult.className.includes('bad'));
  });

  // T-75 проверка: опросы не проходят до конца — «нет связи с платой», а не «приёмник не ответил»
  await check('проверка: опросы не проходят до конца — «нет связи с платой», а не «приёмник не ответил»', async () => {
    const env = makeEnv({
      getQueue: [WORKING, failure(0)],
      post: { '/api/push/test': { ok: true, status: 202, data: { ok: true, started: true } } }
    });
    await env.ctx.initPush();
    await tick();
    await press(env, 'btnPushTest');
    await tick(60);
    assert.strictEqual(env.els.pushTestResult.textContent, 'нет связи с платой');
    assert.ok(env.els.pushTestResult.className.includes('bad'));
  });

  // T-75 проверка: неудача приёмника показана красным классом
  await check('проверка: неудача приёмника показана красным классом', async () => {
    const failDone = Object.assign({}, WORKING, { test: { state: 'done', http: 0, ms: 15, accepted: -1, err: 'connect', ts: 1793351100, legacy: false } });
    const env = makeEnv({
      getQueue: [WORKING, failDone],
      post: { '/api/push/test': { ok: true, status: 202, data: { ok: true, started: true } } }
    });
    await env.ctx.initPush();
    await tick();
    await press(env, 'btnPushTest');
    await tick(60);
    assert.ok(env.els.pushTestResult.className.includes('bad'));
    assert.ok(!env.els.pushTestResult.className.includes('ok'));
    assert.ok(env.els.pushTestResult.textContent.length > 0);
  });

  // T-75 сброс: клавиша Escape закрывает диалог без отправки
  await check('сброс: клавиша Escape закрывает диалог без отправки', async () => {
    const env = makeEnv({ getQueue: [WORKING] });
    await env.ctx.initPush();
    await tick();
    env.ctx.pushResetOpen();
    await tick();
    assert.strictEqual(env.els.pushDlg.hidden, false);
    (env.docH.keydown || []).forEach(f => f({ key: 'Enter' }));
    assert.strictEqual(env.els.pushDlg.hidden, false);
    (env.docH.keydown || []).forEach(f => f({ key: 'Escape' }));
    assert.strictEqual(env.els.pushDlg.hidden, true);
    assert.strictEqual(env.els.pushBackdrop.hidden, true);
    const posts = env.calls.filter(c => c.m === 'POST');
    assert.strictEqual(posts.length, 0);
  });

  // T-75 сохранение: при отказе платы поле токена очищается
  await check('сохранение: при отказе платы поле токена очищается', async () => {
    const env = makeEnv({
      getQueue: [LIVE],
      post: { '/api/push': { ok: false, status: 400, data: { ok: false, err: 'bad_value' } } }
    });
    await env.ctx.initPush();
    await tick();
    typeIn(env, 'pushHost', '192.0.2.10');
    typeIn(env, 'pushPort', '8480');
    typeIn(env, 'pushToken', 'tok_AbC123-xyz');
    env.els.pushEnabled.checked = true;
    await press(env, 'btnPushSave');
    const posts = env.calls.filter(c => c.m === 'POST' && c.url === '/api/push');
    assert.strictEqual(posts.length, 1);
    assert.strictEqual(env.els.pushToken.value, '');
    assert.ok(env.els.pushResult.className.includes('bad'));
    assertNoSubstring(env, 'tok_AbC123');
  });
  // T-76 проверка: три сбоя опроса подряд обрывают ожидание, время ограничено
  await check('проверка: три сбоя опроса подряд обрывают ожидание, время ограничено', async () => {
    const env = makeEnv({ getQueue: [WORKING, failure(0)], post: { '/api/push/test': { ok: true, status: 202, data: { ok: true, started: true } } } });
    await env.ctx.initPush();
    await tick();
    await press(env, 'btnPushTest');
    await tick(60);
    const polls = env.calls.filter(c => c.m === 'GET' && c.url === '/api/push').length - 1;
    assert.equal(polls, 3, `ожидаемо 3 опроса, получено ${polls}`);
    assert.ok(env.clock <= 3 * (1000 + 15000), `clock=${env.clock} должен быть ≤ ${3 * (1000 + 15000)}`);
    assert.equal(env.els.pushTestResult.textContent, 'нет связи с платой');
    assert.ok(env.els.pushTestResult.className.includes('bad'), `className=${env.els.pushTestResult.className}`);
    assert.equal(env.els.btnPushTest.disabled, false);
  });

  // T-76 проверка: счётчик сбоев сбрасывается успешным опросом
  await check('проверка: счётчик сбоев сбрасывается успешным опросом', async () => {
    const running = Object.assign({}, WORKING, { test: { state: 'running', http: 0, ms: 0, accepted: -1, err: '', ts: 0, legacy: false } });
    const done = Object.assign({}, WORKING, { test: { state: 'done', http: 200, ms: 42, accepted: 0, err: '', ts: 1793351100, legacy: false } });
    const env = makeEnv({ getQueue: [WORKING, failure(0), failure(0), running, failure(0), failure(0), done], post: { '/api/push/test': { ok: true, status: 202, data: { ok: true, started: true } } } });
    await env.ctx.initPush();
    await tick();
    await press(env, 'btnPushTest');
    await tick(60);
    assert.equal(env.els.pushTestResult.textContent, 'Соединение работает: приёмник ответил (HTTP 200) за 42 мс');
    assert.ok(env.els.pushTestResult.className.includes('ok'), `className=${env.els.pushTestResult.className}`);
  });

  // T-76 проверка: после сбоя и успешного опроса итог «приёмник не ответил», а не «нет связи»
  await check('проверка: после сбоя и успешного опроса итог «приёмник не ответил», а не «нет связи»', async () => {
const running = Object.assign({}, WORKING, { test: { state: 'running', http: 0, ms: 0, accepted: -1, err: '', ts: 0, legacy: false } });
    const env = makeEnv({ getQueue: [WORKING, failure(0), running], post: { '/api/push/test': { ok: true, status: 202, data: { ok: true, started: true } } } });
    await env.ctx.initPush();
    await tick();
    await press(env, 'btnPushTest');
    await tick(60);
    assert.ok(env.els.pushTestResult.textContent.startsWith('приёмник не ответил за 35 с'), `textContent=${env.els.pushTestResult.textContent}`);
    assert.ok(env.els.pushTestResult.className.includes('bad'), `className=${env.els.pushTestResult.className}`);
  });

  // T-76 проверка: 401 при опросе прекращает опрос сразу
  await check('проверка: 401 при опросе прекращает опрос сразу', async () => {
    const env = makeEnv({ getQueue: [WORKING, failure(401)], post: { '/api/push/test': { ok: true, status: 202, data: { ok: true, started: true } } } });
    await env.ctx.initPush();
    await tick();
    await press(env, 'btnPushTest');
    await tick(60);
    const polls = env.calls.filter(c => c.m === 'GET' && c.url === '/api/push').length - 1;
    assert.equal(polls, 1, `ожидаемо 1 опрос, получено ${polls}`);
    assert.equal(env.els.pushTestResult.textContent, 'нужна авторизация');
  });

  // T-76 форма: ввод после первого заполнения не затирается периодическим обновлением
  await check('форма: ввод после первого заполнения не затирается периодическим обновлением', async () => {
    const env = makeEnv({ getQueue: [WORKING, WORKING] });
    await env.ctx.initPush();
    await tick();
    typeIn(env, 'pushHost', '203.0.113.5');
    env.document.activeElement = env.els.btnPushSave;
    env.timers[0]();
    await tick();
    assert.equal(env.els.pushHost.value, '203.0.113.5', `value=${env.els.pushHost.value}`);
  });

  // T-76 сохранение: ввод после сохранения не затирается
  await check('сохранение: ввод после сохранения не затирается', async () => {
    const env = makeEnv({ getQueue: [LIVE, WORKING, WORKING] });
    await env.ctx.initPush();
    await tick();
    typeIn(env, 'pushHost', '192.0.2.10');
    typeIn(env, 'pushPort', '8480');
    typeIn(env, 'pushToken', 'tok_AbC123-xyz');
    env.els.pushEnabled.checked = true;
    await press(env, 'btnPushSave');
    await tick();
    typeIn(env, 'pushHost', '203.0.113.5');
    env.document.activeElement = env.els.btnPushSave;
    env.timers[0]();
    await tick();
    assert.equal(env.els.pushHost.value, '203.0.113.5', `value=${env.els.pushHost.value}`);
  });

  // T-76 сохранение: при отказе платы флажок «очистить токен» сбрасывается
  await check('сохранение: при отказе платы флажок «очистить токен» сбрасывается', async () => {
    const env = makeEnv({ getQueue: [WORKING], post: { '/api/push': { ok: false, status: 400, data: { ok: false, err: 'bad_value' } } } });
    await env.ctx.initPush();
    await tick();
    env.els.pushTokenClear.checked = true;
    env.els.pushEnabled.checked = false;   // очистка токена при включённой отправке плата не принимает
    await press(env, 'btnPushSave');
    await tick();
    const pushPosts = env.calls.filter(c => c.m === 'POST' && c.url === '/api/push');
    assert.equal(pushPosts.length, 1, `ожидаем 1 POST /api/push, получено ${pushPosts.length}`);
    assert.equal(pushPosts[0].body.clear_token, true);
    assert.equal(env.els.pushTokenClear.checked, false, 'флажок должен сброситься');
    assert.ok(env.els.pushResult.className.includes('bad'), `className=${env.els.pushResult.className}`);
  });
  // ─── Итог ───────────────────────────────────────────────────────────────────
  console.log('TESTS: ' + passed + '/' + total);
  process.exit(passed === total ? 0 : 1);

})();
