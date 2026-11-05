const assert = require('assert');
const path = require('path');
const B = require(process.env.HB_MODULE || path.join(__dirname, '..', '..', 'web', 'fw-board.js'));
const F = require(path.join(__dirname, '..', '..', 'web', 'fw-releases.js'));
let passed = 0, total = 0;
const queue = [];
function check(name, fn) { queue.push([name, fn]); }

const ERR_CODES = ['no_release_cached', 'sums_download_failed', 'sha256sums_missing_entry', 'no_ota_partition', 'ota_busy', 'ota_begin_failed', 'oom', 'asset_http_status', 'image_too_large_for_partition', 'download_or_image_error', 'incomplete_image', 'sha256_mismatch', 'ota_end_failed', 'set_boot_partition_failed'];
const REASONS = ['no_internet', 'network_error', 'no_releases', 'up_to_date', 'downgrade_blocked', 'too_old_for_wifi_ota', 'release_missing_assets'];
const done = (o) => Object.assign({ state: 'done', current: '1.2.39', latest: 'firmware-v1.2.40', newer: true, installable: true, reason: '', html_url: 'https://github.com/VibeEngineering-LLC/atomfast-esp32/releases/tag/firmware-v1.2.40' }, o || {});

check('routes_exact', () => {
    assert.deepStrictEqual(B.ROUTES, {
        csrf: '/api/csrf-token',
        check: '/api/ota/github/check',
        install: '/api/ota/github/install',
        progress: '/api/ota/github/progress',
        channel: '/api/ota/github/channel',
        system: '/api/system'
    });
});

check('repo_and_assets', () => {
    assert.strictEqual(B.REPO, 'VibeEngineering-LLC/atomfast-esp32');
    assert.strictEqual(B.REPO, F.DEFAULT_REPO);
    assert.strictEqual(B.ASSET_NAME, 'atomfast_gw.bin');
    assert.strictEqual(B.SUMS_NAME, 'SHA256SUMS.txt');
});

check('memory_constants_order', () => {
    assert.strictEqual(B.MEM_NEED_MIN, 18432);
    assert.strictEqual(B.MEM_NEED_MAX, 30720);
    assert.strictEqual(B.MEM_WARN, 61440);
    assert.strictEqual(B.MEM_BAD, 45056);
    assert.strictEqual(B.BLOCK_MIN, 12288);
    assert.ok(B.MEM_NEED_MIN < B.MEM_NEED_MAX && B.MEM_NEED_MAX < B.MEM_BAD && B.MEM_BAD < B.MEM_WARN);
});

check('timing_constants', () => {
    assert.strictEqual(B.CHECK_POLL_MS, 1500);
    assert.strictEqual(B.CHECK_MAX_MS, 60000);
    assert.strictEqual(B.PROGRESS_POLL_MS, 1000);
    assert.strictEqual(B.STALE_POLLS, 4);
});

check('heavy_warning_text', () => {
    assert.ok(B.HEAVY_WARNING.indexOf('тяжёлая') !== -1);
    assert.ok(B.HEAVY_WARNING.indexOf('низком запасе памяти') !== -1);
    assert.ok(B.HEAVY_WARNING.indexOf('18–30 КБ') !== -1);
});

check('check_checking', () => {
    assert.deepStrictEqual(B.parseCheck({ state: 'checking' }), { state: 'checking' });
});

check('check_bad_inputs', () => {
    for (const v of [null, undefined, 'x', 5, [], {}, { state: 'idle' }, { state: 'done2' }, { latest: 'a' }]) {
        const r = B.parseCheck(v);
        assert.strictEqual(r.state, 'bad', JSON.stringify(v));
        assert.ok(r.error.length > 10);
    }
});

check('check_done_installable', () => {
    const r = B.parseCheck(done());
    assert.strictEqual(r.state, 'done');
    assert.strictEqual(r.current, '1.2.39');
    assert.strictEqual(r.latest, 'firmware-v1.2.40');
    assert.strictEqual(r.newer, true);
    assert.strictEqual(r.installable, true);
    assert.strictEqual(r.reasonText, 'Можно установить');
    assert.strictEqual(r.htmlUrl, 'https://github.com/VibeEngineering-LLC/atomfast-esp32/releases/tag/firmware-v1.2.40');
});

check('check_installable_needs_latest', () => {
    const r = B.parseCheck(done({ latest: '', installable: true }));
    assert.strictEqual(r.installable, false);
    assert.strictEqual(B.parseCheck(done({ installable: 'true' })).installable, false);
    assert.strictEqual(B.parseCheck(done({ installable: 1 })).installable, false);
});

check('check_current_unknown_marker', () => {
    assert.strictEqual(B.parseCheck(done({ current: '?' })).current, '');
    assert.strictEqual(B.parseCheck(done({ current: 5 })).current, '');
});

check('check_newer_strict_boolean', () => {
    assert.strictEqual(B.parseCheck(done({ newer: 'yes' })).newer, false);
    assert.strictEqual(B.parseCheck(done({ newer: false })).newer, false);
});

check('check_reason_texts', () => {
    const seen = new Set();
    for (const code of REASONS) {
        const r = B.parseCheck(done({ installable: false, newer: false, reason: code }));
        assert.strictEqual(r.reason, code);
        assert.ok(r.reasonText.length > 8, code);
        assert.ok(r.reasonText.indexOf('Причина:') === -1, code);
        seen.add(r.reasonText);
    }
    assert.strictEqual(seen.size, REASONS.length);
});

check('check_reason_specific_wording', () => {
    assert.strictEqual(B.reasonText('up_to_date', false), 'Установлена последняя версия');
    assert.ok(B.reasonText('downgrade_blocked', false).indexOf('Понижение') === 0);
    assert.ok(B.reasonText('too_old_for_wifi_ota', false).indexOf('.bin') !== -1);
    assert.ok(B.reasonText('no_internet', false).indexOf('интернет') !== -1);
});

check('check_reason_unknown_and_empty', () => {
    assert.strictEqual(B.reasonText('weird', false), 'Причина: weird');
    assert.strictEqual(B.reasonText('', false), 'Установка недоступна');
    assert.strictEqual(B.reasonText('constructor', false), 'Причина: constructor');
    assert.strictEqual(B.reasonText('up_to_date', true), 'Можно установить');
});

check('url_safe_only_github_https', () => {
    assert.strictEqual(B.safeUrl('https://github.com/a/b'), 'https://github.com/a/b');
    for (const u of ['http://github.com/a/b', 'javascript:alert(1)', 'https://evil.example/a', 'https://github.com.evil.example/a', 'https://github.com/a b', 'https://github.com/a"b', '', null, 5]) {
        assert.strictEqual(B.safeUrl(u), '', String(u));
    }
});

check('progress_all_states', () => {
    const labels = new Set();
    for (const s of ['idle', 'checking', 'downloading', 'verifying', 'installing', 'done', 'error']) {
        const p = B.parseProgress({ state: s, bytes: 0, total: 0, error: '' });
        assert.strictEqual(p.state, s);
        assert.ok(p.label.length > 3);
        labels.add(p.label);
    }
    assert.strictEqual(labels.size, 7);
});

check('progress_flags', () => {
    const f = (s) => { const p = B.parseProgress({ state: s }); return [p.active, p.terminal]; };
    assert.deepStrictEqual(f('idle'), [false, false]);
    assert.deepStrictEqual(f('checking'), [true, false]);
    assert.deepStrictEqual(f('downloading'), [true, false]);
    assert.deepStrictEqual(f('verifying'), [true, false]);
    assert.deepStrictEqual(f('installing'), [true, false]);
    assert.deepStrictEqual(f('done'), [false, true]);
    assert.deepStrictEqual(f('error'), [false, true]);
});

check('progress_percent', () => {
    assert.strictEqual(B.parseProgress({ state: 'downloading', bytes: 500, total: 1000 }).percent, 50);
    assert.strictEqual(B.parseProgress({ state: 'downloading', bytes: 1, total: 3 }).percent, 33);
    assert.strictEqual(B.parseProgress({ state: 'downloading', bytes: 2, total: 3 }).percent, 67);
    assert.strictEqual(B.parseProgress({ state: 'downloading', bytes: 2000, total: 1000 }).percent, 100);
    assert.strictEqual(B.parseProgress({ state: 'downloading', bytes: 5, total: 0 }).percent, 0);
});

check('progress_bad_numbers', () => {
    const p = B.parseProgress({ state: 'downloading', bytes: -5, total: 'x' });
    assert.strictEqual(p.bytes, 0);
    assert.strictEqual(p.total, 0);
    assert.strictEqual(B.parseProgress({ state: 'downloading', bytes: NaN, total: Infinity }).total, 0);
    assert.strictEqual(B.parseProgress({ state: 'downloading', bytes: 7, total: 9 }).bytes, 7);
});

check('progress_bad_state', () => {
    for (const v of [null, 5, 'x', {}, { state: 5 }, { state: 'nope' }, { state: 'constructor' }, { state: '__proto__' }, { state: 'toString' }]) {
        const p = B.parseProgress(v);
        assert.strictEqual(p.state, 'bad', JSON.stringify(v));
        assert.strictEqual(p.active, false);
        assert.strictEqual(p.terminal, false);
    }
});

check('progress_error_text_only_for_error', () => {
    assert.strictEqual(B.parseProgress({ state: 'downloading', error: 'oom' }).errorText, '');
    assert.ok(B.parseProgress({ state: 'error', error: 'oom' }).errorText.indexOf('не хватило памяти') !== -1);
    assert.strictEqual(B.parseProgress({ state: 'error', error: 5 }).error, '');
});

check('error_texts_all_codes', () => {
    const seen = new Set();
    for (const c of ERR_CODES) {
        const t = B.errorText(c);
        assert.ok(t.indexOf('Установка не удалась: ') === 0, c);
        assert.ok(t.indexOf('Прежняя прошивка осталась.') !== -1, c);
        assert.ok(t.indexOf('код ') === -1, c);
        seen.add(t);
    }
    assert.strictEqual(seen.size, ERR_CODES.length);
});

check('error_text_specific', () => {
    assert.ok(B.errorText('sha256_mismatch').indexOf('SHA-256') !== -1);
    assert.ok(B.errorText('sums_download_failed').indexOf('SHA256SUMS.txt') !== -1);
    assert.ok(B.errorText('image_too_large_for_partition').indexOf('раздел') !== -1);
    assert.ok(B.errorText('incomplete_image').indexOf('не полностью') !== -1);
});

check('error_text_unknown', () => {
    assert.ok(B.errorText('xyz').indexOf('код xyz') !== -1);
    assert.ok(B.errorText('').indexOf('причина не указана') !== -1);
    assert.ok(B.errorText('constructor').indexOf('код constructor') !== -1);
});

check('stale_idle_early', () => {
    const idle = B.parseProgress({ state: 'idle' });
    assert.strictEqual(B.isStale(null, idle, false, 1), true);
    assert.strictEqual(B.isStale(null, idle, false, 3), true);
    assert.strictEqual(B.isStale(null, idle, false, 4), false);
    assert.strictEqual(B.isStale(null, idle, true, 1), false);
});

check('stale_terminal_same_as_baseline', () => {
    const base = B.parseProgress({ state: 'error', error: 'oom', bytes: 10, total: 20 });
    const same = B.parseProgress({ state: 'error', error: 'oom', bytes: 10, total: 20 });
    assert.strictEqual(B.isStale(base, same, false, 1), true);
    assert.strictEqual(B.isStale(base, same, false, 4), false);
    assert.strictEqual(B.isStale(base, same, true, 1), false);
});

check('stale_terminal_different', () => {
    const base = B.parseProgress({ state: 'error', error: 'oom', bytes: 10, total: 20 });
    assert.strictEqual(B.isStale(base, B.parseProgress({ state: 'error', error: 'sha256_mismatch', bytes: 10, total: 20 }), false, 1), false);
    assert.strictEqual(B.isStale(base, B.parseProgress({ state: 'error', error: 'oom', bytes: 11, total: 20 }), false, 1), false);
    assert.strictEqual(B.isStale(base, B.parseProgress({ state: 'done', error: '', bytes: 10, total: 20 }), false, 1), false);
    assert.strictEqual(B.isStale(null, B.parseProgress({ state: 'done' }), false, 1), false);
});

check('stale_active_never', () => {
    const base = B.parseProgress({ state: 'error', error: 'oom' });
    assert.strictEqual(B.isStale(base, B.parseProgress({ state: 'downloading', bytes: 0, total: 0 }), false, 1), false);
    assert.strictEqual(B.isStale(null, B.parseProgress({ state: 'checking' }), false, 1), false);
});

check('install_error_statuses', () => {
    assert.ok(B.installError(403).indexOf('токен') !== -1);
    assert.ok(B.installError(409).indexOf('не хватило памяти') !== -1);
    assert.ok(B.installError(409).indexOf('Прежняя прошивка осталась') !== -1);
    assert.ok(B.installError(500).indexOf('500') !== -1);
});

check('check_error_statuses', () => {
    assert.ok(B.checkError(403).indexOf('токен') !== -1);
    assert.ok(B.checkError(503).indexOf('503') !== -1);
});

check('mem_ok', () => {
    const m = B.memInfo({ int_free: 96000, int_largest: 40000 });
    assert.strictEqual(m.level, 'ok');
    assert.strictEqual(m.known, true);
    assert.strictEqual(m.free, 96000);
    assert.ok(m.text.indexOf('94 КБ') !== -1);
    assert.ok(m.text.indexOf('крупнейший блок 39 КБ') !== -1);
});

check('mem_warn_boundary', () => {
    assert.strictEqual(B.memInfo({ int_free: 61440, int_largest: 40000 }).level, 'ok');
    assert.strictEqual(B.memInfo({ int_free: 61439, int_largest: 40000 }).level, 'warn');
    assert.strictEqual(B.memInfo({ int_free: 45056, int_largest: 40000 }).level, 'warn');
    assert.ok(B.memInfo({ int_free: 50000 }).text.indexOf('запас ниже') !== -1);
});

check('mem_bad_boundary', () => {
    assert.strictEqual(B.memInfo({ int_free: 45055, int_largest: 40000 }).level, 'bad');
    assert.strictEqual(B.memInfo({ int_free: 10000 }).level, 'bad');
    assert.ok(B.memInfo({ int_free: 10000 }).text.indexOf('отложить') !== -1);
});

check('mem_block_boundary', () => {
    assert.strictEqual(B.memInfo({ int_free: 98000, int_largest: 12288 }).level, 'ok');
    assert.strictEqual(B.memInfo({ int_free: 98000, int_largest: 12287 }).level, 'bad');
    assert.strictEqual(B.memInfo({ int_free: 98000, int_largest: 0 }).level, 'bad');
});

check('mem_without_largest', () => {
    const m = B.memInfo({ int_free: 98000 });
    assert.strictEqual(m.level, 'ok');
    assert.strictEqual(m.largest, null);
    assert.ok(m.text.indexOf('крупнейший') === -1);
});

check('mem_unknown', () => {
    for (const v of [null, undefined, {}, { int_free: 'x' }, { int_free: -1 }, { int_free: NaN }, { int_free: Infinity }]) {
        const m = B.memInfo(v);
        assert.strictEqual(m.known, false, JSON.stringify(v));
        assert.strictEqual(m.level, 'unknown');
        assert.strictEqual(m.free, null);
    }
});

check('flow_with_downgrade_message', () => {
    const row = { tag: 'firmware-v1.2.40', relation: F.relation('1.2.39', 'firmware-v1.2.40') };
    assert.strictEqual(row.relation, 'newer');
    const msg = F.confirmMessage('1.2.39', row);
    assert.ok(msg.indexOf('с 1.2.39 на firmware-v1.2.40') !== -1);
    assert.ok(msg.indexOf('понижение') === -1);
});

(async () => {
    for (const [name, fn] of queue) {
        total++;
        try { await fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); }
    }
    console.log('TESTS: ' + passed + '/' + total);
    process.exit(passed === total ? 0 : 1);
})();
