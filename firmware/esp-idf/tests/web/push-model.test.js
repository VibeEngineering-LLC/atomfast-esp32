'use strict';
const assert = require('assert');
const path = require('path');
const S = require(process.env.HC_MODULE || path.join(__dirname, '..', '..', 'web', 'push-model.js'));

/* ─── вспомогательные ─────────────────────────────────────────── */
const LIVE = {
  enabled: false, host: '', port: 8480, token_set: false,
  interval_s: 60, batch_rows: 500, timeout_s: 8,
  rings: ['raw', 'agg', 'env'],
  warn: { no_tls: true, public_addr: false },
  state: 'off', auth_stop: false, stack_hwm: 0,
  test: { state: 'idle', http: 0, ms: 0, accepted: 0, err: '', ts: 0, legacy: false },
  ring: {
    raw: { on: true, present: true, acked_seq: -1, epoch: 74, pending: 156894, last_ok_ts: 0, lag_s: 0, last_err: { code: 0, text: '' }, last_http: 0, sent_total: 0, fail_total: 0, retries: 0, last_batch_rows: 0, stopped: false },
    agg: { on: true, present: true, acked_seq: -1, epoch: 74, pending: 751, last_ok_ts: 0, lag_s: 0, last_err: { code: 0, text: '' }, last_http: 0, sent_total: 0, fail_total: 0, retries: 0, last_batch_rows: 0, stopped: false },
    env: { on: true, present: true, acked_seq: -1, epoch: 74, pending: 2985, last_ok_ts: 0, lag_s: 0, last_err: { code: 0, text: '' }, last_http: 0, sent_total: 0, fail_total: 0, retries: 0, last_batch_rows: 0, stopped: false }
  }
};

const WORKING = Object.assign({}, LIVE, {
  enabled: true,
  host: '192.0.2.10',
  token_set: true,
  state: 'idle'
});
WORKING.ring = Object.assign({}, LIVE.ring);
WORKING.ring.raw = Object.assign({}, LIVE.ring.raw, {
  acked_seq: 244614, pending: 12, sent_total: 156784, fail_total: 2,
  last_ok_ts: 1793351040, last_err: { code: 2, text: 'server' }
});

function F(o) {
  return Object.assign({ enabled: true, host: '192.0.2.10', port: '8480', token: 'tok_AbC123-xyz', clearToken: false }, o);
}

/* ─── счётчик и обёртка ───────────────────────────────────────── */
let passed = 0, total = 0;
function check(name, fn) {
  total++;
  try {
    fn();
    passed++;
    console.log('ok  ' + name);
  } catch (e) {
    console.log('FAIL ' + name + ': ' + (e && e.message ? e.message : String(e)));
  }
}

/* ─── 1. порт ─────────────────────────────────────────────────── */
check('порт: 0, 65536, 70000, abc, пусто, -1 отклоняются; 1, 8480, 65535 принимаются', function () {
  const bad = ['0', '65536', '70000', 'abc', '', '-1'];
  for (const v of bad) {
    const r = S.validateForm(F({ port: v }), true);
    assert.strictEqual(r.ok, false, 'порт ' + JSON.stringify(v) + ' должен отклоняться');
    assert.strictEqual(r.field, 'pushPort', 'field должен быть pushPort для ' + JSON.stringify(v));
  }
  const good = ['1', '8480', '65535'];
  for (const v of good) {
    const r = S.validateForm(F({ port: v }), true);
    assert.strictEqual(r.ok, true, 'порт ' + v + ' должен приниматься');
    assert.strictEqual(r.body.port, parseInt(v, 10), 'body.port должен быть числом ' + v);
  }
});

/* ─── 2. адрес пустой при включённой отправке ─────────────────── */
check('адрес: пустой при включённой отправке -> ошибка pushHost', function () {
  for (const host of ['', '   ']) {
    const r = S.validateForm(F({ host: host }), true);
    assert.strictEqual(r.ok, false, 'пустой адрес должен отклоняться');
    assert.strictEqual(r.field, 'pushHost', 'field должен быть pushHost');
    assert.strictEqual(r.error, 'Укажите адрес приёмника', 'error должен быть "Укажите адрес приёмника"');
  }
});

/* ─── 3. адрес пустой при выключенной отправке ────────────────── */
check('адрес: пустой при выключенной отправке допустим', function () {
  const r = S.validateForm(F({ enabled: false, host: '' }), true);
  assert.strictEqual(r.ok, true, 'пустой адрес при выключенной отправке допустим');
  assert.strictEqual(r.body.host, '', 'body.host должен быть пустой строкой');
});

/* ─── 4. адрес: валидные / невалидные ────────────────────────── */
check('адрес: допустимы IPv4 и имя; недопустимы 256.1.1.1, 1.2.3, a..b, -a.b, a_b.lan, пробел внутри, 64 символа', function () {
  const good = ['192.0.2.10', 'db.lan', 'raspi5-home.local'];
  for (const h of good) {
    const r = S.validateForm(F({ host: h }), true);
    assert.strictEqual(r.ok, true, 'адрес ' + h + ' должен приниматься');
  }
  const bad = ['256.1.1.1', '1.2.3', 'a..b', '-a.b', 'a_b.lan', 'a b.lan', 'a'.repeat(64)];
  for (const h of bad) {
    const r = S.validateForm(F({ host: h }), true);
    assert.strictEqual(r.ok, false, 'адрес ' + JSON.stringify(h) + ' должен отклоняться');
    assert.strictEqual(r.field, 'pushHost', 'field должен быть pushHost для ' + JSON.stringify(h));
  }
});

/* ─── 5. токен не показывается при пустом поле ────────────────── */
check('токен: не показывается в теле, если поле пустое', function () {
  const r = S.validateForm(F({ token: '' }), true);
  assert.strictEqual(r.ok, true, 'форма с пустым токеном должна проходить (tokenSet=true)');
  assert.ok(!('token' in r.body), 'в теле не должно быть ключа token');
  assert.ok(!('clear_token' in r.body), 'в теле не должно быть ключа clear_token');
});

/* ─── 6. токен кладётся в тело как есть ───────────────────────── */
check('токен: ввод кладётся в тело как есть', function () {
  const r = S.validateForm(F({ token: 'tok_AbC123-xyz' }), true);
  assert.strictEqual(r.ok, true, 'форма с токеном должна проходить');
  assert.strictEqual(r.body.token, 'tok_AbC123-xyz', 'body.token должен совпадать');
  assert.deepStrictEqual(Object.keys(r.body).sort(), ['enabled', 'host', 'port', 'token'], 'ключи тела должны быть ровно enabled, host, port, token');
});

/* ─── 7. токен: недопустимые символы и длина ──────────────────── */
check('токен: пробел, кавычка, обратная косая и 65 символов отклоняются', function () {
  const bad = ['a b', 'a"b', 'a\\b', 'x'.repeat(65)];
  for (const t of bad) {
    const r = S.validateForm(F({ token: t }), true);
    assert.strictEqual(r.ok, false, 'токен ' + JSON.stringify(t.slice(0, 10)) + '… должен отклоняться');
    assert.strictEqual(r.field, 'pushToken', 'field должен быть pushToken');
  }
  const r64 = S.validateForm(F({ token: 'x'.repeat(64) }), true);
  assert.strictEqual(r64.ok, true, 'токен из 64 символов должен приниматься');
});

/* ─── 8. включение без токена и без сохранённого ───────────────── */
check('токен: включение без токена и без сохранённого -> ошибка pushToken', function () {
  const r = S.validateForm(F({ token: '' }), false);
  assert.strictEqual(r.ok, false, 'включение без токена и без сохранённого должно отклоняться');
  assert.strictEqual(r.field, 'pushToken', 'field должен быть pushToken');
  assert.strictEqual(r.error, 'Укажите токен приёмника', 'error должен быть "Укажите токен приёмника"');
});

/* ─── 9. очистка токена ───────────────────────────────────────── */
check('очистка токена: флаг кладёт clear_token, вместе с вводом токена запрещён', function () {
  // разрешено: enabled=false, token='', clearToken=true, tokenSet=true
  const ok = S.validateForm(F({ token: '', clearToken: true, enabled: false }), true);
  assert.strictEqual(ok.ok, true, 'очистка токена при выключенной отправке допустима');
  assert.strictEqual(ok.body.clear_token, true, 'body.clear_token должен быть true');
  assert.ok(!('token' in ok.body), 'в теле не должно быть ключа token');

  // запрещено: токен введён И clearToken=true
  const bad = S.validateForm(F({ clearToken: true }), true);
  assert.strictEqual(bad.ok, false, 'очистка вместе с вводом токена должна отклоняться');
  assert.strictEqual(bad.field, 'pushTokenClear', 'field должен быть pushTokenClear');
});

/* ─── 10. очистка токена при включённой отправке ──────────────── */
check('очистка токена при включённой отправке запрещена', function () {
  const r = S.validateForm(F({ token: '', clearToken: true }), true);
  assert.strictEqual(r.ok, false, 'очистка токена при включённой отправке должна отклоняться');
  assert.strictEqual(r.field, 'pushTokenClear', 'field должен быть pushTokenClear');
});

/* ─── 11. tokenHint ───────────────────────────────────────────── */
check('tokenHint: задан / не задан', function () {
  const w = S.tokenHint(WORKING);
  assert.strictEqual(typeof w, 'string', 'tokenHint(WORKING) должен быть строкой');
  assert.ok(w.length > 0, 'tokenHint(WORKING) не должен быть пустым');
  assert.ok(w.includes('задан'), 'tokenHint(WORKING) должен содержать "задан"');
  assert.ok(!w.includes('не задан'), 'tokenHint(WORKING) не должен содержать "не задан"');

  const l = S.tokenHint(LIVE);
  assert.strictEqual(typeof l, 'string', 'tokenHint(LIVE) должен быть строкой');
  assert.ok(l.length > 0, 'tokenHint(LIVE) не должен быть пустым');
  assert.ok(l.includes('не задан'), 'tokenHint(LIVE) должен содержать "не задан"');
});

/* ─── 12. stateText ───────────────────────────────────────────── */
check('stateText: все состояния и остановка по токену', function () {
  const states = [
    { state: 'off' },
    { state: 'idle' },
    { state: 'sending' },
    { state: 'error' },
    { state: 'weird' },
    {},
    null
  ];
  const results = states.map(function (s) {
    const t = S.stateText(s);
    assert.strictEqual(typeof t, 'string', 'stateText должен возвращать строку');
    assert.ok(t.length > 0, 'stateText не должен быть пустым');
    return t;
  });
  // известные состояния различны
  assert.notStrictEqual(results[0], results[1], 'off ≠ idle');
  assert.notStrictEqual(results[1], results[2], 'idle ≠ sending');
  assert.notStrictEqual(results[2], results[3], 'sending ≠ error');

  // auth_stop
  const authStop = S.stateText(Object.assign({}, WORKING, { auth_stop: true }));
  assert.strictEqual(typeof authStop, 'string');
  assert.ok(authStop.includes('отклонил токен'), 'stateText с auth_stop должен содержать "отклонил токен"');
});

/* ─── 13. errText ─────────────────────────────────────────────── */
check('errText: все коды из прошивки по-русски', function () {
  const codes = ['net', 'server', 'auth', 'bad_batch', 'too_big', 'proto', 'http4xx', 'no_device', 'no_store', 'oom'];
  const texts = codes.map(function (c) {
    const t = S.errText(c);
    assert.strictEqual(typeof t, 'string', 'errText(' + c + ') должен быть строкой');
    assert.ok(t.length > 0, 'errText(' + c + ') не должен быть пустым');
    assert.ok(/[а-яё]/i.test(t), 'errText(' + c + ') должен содержать кириллицу');
    return t;
  });
  // все десять различны
  const unique = new Set(texts);
  assert.strictEqual(unique.size, 10, 'все десять errText должны различаться');

  assert.strictEqual(S.errText(''), '—');
  assert.strictEqual(S.errText(undefined), '—');
  assert.strictEqual(S.errText('zzz'), 'ошибка: zzz');
});

/* ─── 14. statusView: выключено ───────────────────────────────── */
check('statusView: выключено на живом образце', function () {
  const v = S.statusView(LIVE);
  assert.strictEqual(v.enabled, 'нет', 'enabled должен быть "нет"');
  assert.strictEqual(v.conn, '—', 'conn должен быть "—"');
  assert.strictEqual(v.sent, '0', 'sent должен быть "0"');
  assert.strictEqual(v.fail, '0', 'fail должен быть "0"');
  assert.strictEqual(v.lastErr, '—', 'lastErr должен быть "—"');
});

/* ─── 15. statusView: работающая отправка ─────────────────────── */
check('statusView: работающая отправка', function () {
  const v = S.statusView(WORKING);
  assert.strictEqual(v.enabled, 'да', 'enabled должен быть "да"');
  assert.strictEqual(v.conn, 'есть', 'conn должен быть "есть"');
  assert.strictEqual(v.sent, '156784', 'sent должен быть "156784"');
  assert.strictEqual(v.fail, '2', 'fail должен быть "2"');
  assert.ok(v.lastErr.includes('ответил ошибкой сервера'), 'lastErr должен содержать "ответил ошибкой сервера"');
});

/* ─── 16. statusView: ошибка и остановка ──────────────────────── */
check('statusView: ошибка и остановка дают «нет»', function () {
  const err = S.statusView(Object.assign({}, WORKING, { state: 'error' }));
  assert.strictEqual(err.conn, 'нет', 'при state=error conn должен быть "нет"');

  const stop = S.statusView(Object.assign({}, WORKING, { auth_stop: true }));
  assert.strictEqual(stop.conn, 'нет', 'при auth_stop conn должен быть "нет"');

  const nullV = S.statusView(null);
  assert.strictEqual(nullV.enabled, '—', 'statusView(null).enabled должен быть "—"');
});

/* ─── 17. ringRows ────────────────────────────────────────────── */
check('ringRows: три кольца по порядку, тексты по образцу', function () {
  // WORKING
  const rw = S.ringRows(WORKING);
  assert.strictEqual(rw.length, 3, 'должно быть 3 кольца');
  assert.deepStrictEqual(rw.map(function (r) { return r.id; }), ['raw', 'agg', 'env'], 'id по порядку');
  assert.deepStrictEqual(rw.map(function (r) { return r.label; }), ['сырые', 'агрегаты', 'датчики среды'], 'labels');
  assert.strictEqual(rw[0].text, 'ждёт отправки 12; подтверждено до № 244614; отправлено 156784, ошибок 2', 'raw text WORKING');

  // LIVE agg
  const rl = S.ringRows(LIVE);
  assert.strictEqual(rl[1].text, 'ждёт отправки 751; ещё не отправляли; отправлено 0, ошибок 0', 'agg text LIVE');

  // on:false
  const offStatus = Object.assign({}, WORKING);
  offStatus.ring = Object.assign({}, WORKING.ring);
  offStatus.ring.agg = Object.assign({}, WORKING.ring.agg, { on: false });
  const ro = S.ringRows(offStatus);
  assert.strictEqual(ro[1].text, 'не отправляется', 'кольцо on:false → "не отправляется"');

  // пустой объект
  const re = S.ringRows({});
  assert.strictEqual(re.length, 3, 'пустой объект → 3 строки');
  for (let i = 0; i < 3; i++) {
    assert.strictEqual(re[i].text, 'нет данных', 'пустой объект → "нет данных"');
  }
});

/* ─── 18. testText ────────────────────────────────────────────── */
check('testText: idle пуст, running, успех, отказ токена, обрыв', function () {
  assert.strictEqual(S.testText({ state: 'idle' }), '', 'idle → пустая строка');
  assert.strictEqual(S.testText({ state: 'running' }), 'проверка идёт…', 'running → "проверка идёт…"');

  const ok = S.testText({ state: 'done', err: '', http: 200, ms: 42, accepted: 0, legacy: false });
  assert.strictEqual(ok, 'Соединение работает: приёмник ответил (HTTP 200) за 42 мс', 'успех');

  const auth = S.testText({ state: 'done', err: 'auth', http: 401, ms: 15 });
  assert.strictEqual(auth, 'Не удалось: приёмник отклонил токен (HTTP 401), 15 мс', 'отказ токена');

  const net = S.testText({ state: 'done', err: 'net', http: 0, ms: 0 });
  assert.strictEqual(net, 'Не удалось: нет связи с приёмником', 'обрыв');

  const legacy = S.testText({ state: 'done', err: '', http: 200, ms: 42, accepted: 0, legacy: true });
  assert.ok(legacy.includes('; приёмник старой версии'), 'legacy:true добавляет пометку');
});

/* ─── 19. apiErrText ──────────────────────────────────────────── */
check('apiErrText: коды платы по-русски', function () {
  assert.ok(S.apiErrText(409, 'incomplete').includes('не заданы адрес приёмника и токен'), '409 incomplete');
  assert.ok(S.apiErrText(409, 'busy').includes('проверка уже идёт'), '409 busy');
  assert.ok(S.apiErrText(503, 'not_ready').includes('ещё запускается'), '503 not_ready');
  assert.ok(S.apiErrText(400, 'bad_value').includes('плата отклонила значения'), '400 bad_value');
  assert.strictEqual(S.apiErrText(401, ''), 'нужна авторизация', '401');
  assert.ok(S.apiErrText(403, '').includes('недействительный токен страницы'), '403');
  assert.strictEqual(S.apiErrText(0, ''), 'нет связи с платой', '0');
  assert.strictEqual(S.apiErrText(418, ''), 'плата отклонила запрос (418)', '418');
});

/* ─── 20. resetConfirmText ────────────────────────────────────── */
check('resetConfirmText: число записей и склонение', function () {
  // LIVE: 156894 + 751 + 2985 = 160630
  const tl = S.resetConfirmText(LIVE);
  assert.ok(tl.includes('160630 записей'), 'LIVE содержит "160630 записей"');
  assert.ok(tl.includes('Данные на плате не удаляются'), 'содержит предупреждение');

  // склонение
  const expected = [
    [1, '1 запись'],
    [2, '2 записи'],
    [5, '5 записей'],
    [11, '11 записей'],
    [21, '21 запись']
  ];
  for (const pair of expected) {
    const n = pair[0], frag = pair[1];
    const st = { ring: { raw: { pending: n, on: true }, agg: { pending: 0, on: true }, env: { pending: 0, on: true } } };
    const t = S.resetConfirmText(st);
    assert.ok(t.includes(frag), 'pending=' + n + ' → должен содержать "' + frag + '", получено: ' + t);
  }

  // on:false не входит в сумму
  const off = { ring: { raw: { pending: 5, on: true }, agg: { pending: 3, on: false }, env: { pending: 2, on: true } } };
  const to = S.resetConfirmText(off);
  assert.ok(to.includes('7 записей'), 'on:false не входит: 5+2=7, а не 10');
});

/* ─── 22-25 (T-71, P2-1/P2-2): образцы — настоящий JSON af_push_json(), tests/host/probe_push_json.c ─── */
const FXJ = function (n) { return JSON.parse(require('fs').readFileSync(path.join(__dirname, 'fixtures', n + '.json'), 'utf8')); };
check('ringRows: остановленное кольцо помечено и подсказывает «Сохранить»', function () {
  const rows = S.ringRows(FXJ('push-stopped-error'));
  assert.ok(rows[0].text.indexOf('остановлено') === 0, rows[0].text);
  assert.ok(rows[0].text.indexOf('приёмник отклонил пачку данных') > 0);
  assert.ok(rows[0].text.indexOf('кнопка «Сохранить»') > 0);
  assert.ok(rows[1].text.indexOf('остановлено') < 0);
});
check('stateText: при остановленном кольце не пишет «идёт отправка»', function () {
  const t = S.stateText(FXJ('push-stopped-sending'));
  assert.ok(t.indexOf('остановлено') === 0 && t.indexOf('Сохранить') > 0, t);
  assert.ok(t.indexOf('идёт отправка') < 0);
});
check('statusView: остановленное кольцо даёт соединение «нет»', function () {
  assert.strictEqual(S.statusView(FXJ('push-stopped-sending')).conn, 'нет');
  assert.strictEqual(S.statusView(FXJ('push-working')).conn, 'есть');
});
check('testWaitSeconds: из timeout_s платы, по умолчанию — максимум 30 с', function () {
  assert.strictEqual(S.testWaitSeconds({ timeout_s: 8 }), 35);
  assert.strictEqual(S.testWaitSeconds({ timeout_s: 30 }), 123);
  assert.strictEqual(S.testWaitSeconds({}), 123);
  assert.strictEqual(S.testWaitSeconds(null), 123);
});
/* ─── 21. экспорт ─────────────────────────────────────────────── */
check('экспорт: ровно ожидаемые имена', function () {
  const expected = ['RING_LABELS', 'apiErrText', 'errText', 'resetConfirmText', 'ringRows', 'statusView', 'stateText', 'testText', 'testWaitSeconds', 'tokenHint', 'validateForm'].sort();
  assert.deepStrictEqual(Object.keys(S).sort(), expected, 'имена экспорта должны совпадать');
});

/* ─── итог ────────────────────────────────────────────────────── */
console.log('TESTS: ' + passed + '/' + total);
process.exit(passed === total ? 0 : 1);
