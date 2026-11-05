const assert = require('assert');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const F = require(process.env.HW_MODULE || path.join(__dirname, '..', '..', 'web', 'fw-releases.js'));
const FIX = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'gh-releases.json'), 'utf8'));
const SUBTLE = crypto.webcrypto.subtle;
let passed = 0, total = 0;
const queue = [];
function check(name, fn) { queue.push([name, fn]); }
function sha(bytes) { return crypto.createHash('sha256').update(Buffer.from(bytes)).digest('hex'); }
function image(n) { const b = new Uint8Array(n); b[0] = 0xE9; for (let i = 1; i < n; i++) b[i] = (i * 7) & 0xFF; return b; }
const hdr = (o) => (name) => (name in o ? o[name] : null);

check('repo_valid_forms', () => {
    assert.deepStrictEqual(F.validateRepo('VibeEngineering-LLC/atomfast-esp32'), { ok: true, repo: 'VibeEngineering-LLC/atomfast-esp32', error: '' });
    assert.strictEqual(F.validateRepo('  a/b  ').repo, 'a/b');
    assert.strictEqual(F.validateRepo('https://github.com/VibeEngineering-LLC/atomfast-esp32.git').repo, 'VibeEngineering-LLC/atomfast-esp32');
    assert.strictEqual(F.validateRepo('https://github.com/o/n/').repo, 'o/n');
    assert.strictEqual(F.DEFAULT_REPO, 'VibeEngineering-LLC/atomfast-esp32');
});

check('repo_invalid_forms', () => {
    for (const bad of ['', 'abc', 'a/b/c', 'a b/c', '-a/b', 'a/..', 'a/.', '../x', 'a/b c', null, 5]) {
        const r = F.validateRepo(bad);
        assert.strictEqual(r.ok, false, String(bad));
        assert.strictEqual(r.error, 'формат: владелец/репозиторий');
        assert.strictEqual(r.repo, '');
    }
});

check('urls', () => {
    assert.strictEqual(F.releasesUrl('o/n'), 'https://api.github.com/repos/o/n/releases?per_page=20');
    assert.strictEqual(F.releasesUrl('o/n', 'http://localhost:8791/__gh'), 'http://localhost:8791/__gh/repos/o/n/releases?per_page=20');
    assert.strictEqual(F.assetUrl('o/n', 12), 'https://api.github.com/repos/o/n/releases/assets/12');
    assert.strictEqual(F.assetUrl('o/n', 'x'), '');
    assert.strictEqual(F.assetUrl('o/n', -1), '');
    assert.strictEqual(F.assetUrl('o/n', 1.5), '');
});

check('is_github_url', () => {
    assert.strictEqual(F.isGithubUrl('https://api.github.com/repos/o/n/releases'), true);
    assert.strictEqual(F.isGithubUrl('https://api.github.com.evil.example/x'), false);
    assert.strictEqual(F.isGithubUrl('http://192.168.0.5/api/status'), false);
    assert.strictEqual(F.isGithubUrl(null), false);
    assert.strictEqual(F.isGithubUrl('http://localhost:8791/__gh/x', 'http://localhost:8791/__gh'), true);
});

check('request_shape_has_no_board_data', () => {
    const r = F.buildRequest('application/vnd.github+json');
    assert.deepStrictEqual(r, { method: 'GET', headers: { Accept: 'application/vnd.github+json' }, credentials: 'omit', referrerPolicy: 'no-referrer', mode: 'cors', cache: 'no-store' });
    assert.deepStrictEqual(Object.keys(r.headers), ['Accept']);
    assert.ok(!('body' in r));
});

check('github_get_passes_only_request_shape', () => {
    const seen = [];
    const fetchFn = (u, init) => { seen.push([u, init]); return 'ok'; };
    const out = F.githubGet(fetchFn, F.releasesUrl('o/n'), F.ACCEPT_JSON);
    assert.strictEqual(out, 'ok');
    assert.strictEqual(seen.length, 1);
    assert.strictEqual(seen[0][0], 'https://api.github.com/repos/o/n/releases?per_page=20');
    assert.deepStrictEqual(seen[0][1], F.buildRequest(F.ACCEPT_JSON));
});

check('github_get_refuses_foreign_url', () => {
    let calls = 0;
    const fetchFn = () => { calls++; return 'ok'; };
    assert.throws(() => F.githubGet(fetchFn, 'http://192.168.0.5/api/status', F.ACCEPT_JSON), /не GitHub/);
    assert.throws(() => F.githubGet(fetchFn, '/api/status', F.ACCEPT_JSON), /не GitHub/);
    assert.strictEqual(calls, 0);
});

check('parse_version_forms', () => {
    assert.deepStrictEqual(F.parseVersion('firmware-v1.2.40-rc1'), { major: 1, minor: 2, patch: 40, pre: 'rc', preNum: 1 });
    assert.deepStrictEqual(F.parseVersion('1.2.39'), { major: 1, minor: 2, patch: 39, pre: '', preNum: 0 });
    assert.deepStrictEqual(F.parseVersion('v1.2.39-dirty'), { major: 1, minor: 2, patch: 39, pre: '', preNum: 0 });
    assert.deepStrictEqual(F.parseVersion('0.1.0-e13'), { major: 0, minor: 1, patch: 0, pre: '', preNum: 0 });
    assert.deepStrictEqual(F.parseVersion('2.0.0-beta'), { major: 2, minor: 0, patch: 0, pre: 'beta', preNum: 0 });
    for (const bad of ['abc', '', '1.2', null, undefined, 5]) assert.strictEqual(F.parseVersion(bad), null, String(bad));
});

check('compare_versions', () => {
    assert.strictEqual(F.compareVersions('1.2.39', '1.2.40-rc1'), -1);
    assert.strictEqual(F.compareVersions('firmware-v1.2.40-rc1', '1.2.40'), -1);
    assert.strictEqual(F.compareVersions('1.2.40', 'firmware-v1.2.40-rc1'), 1);
    assert.strictEqual(F.compareVersions('1.2.40-rc2', '1.2.40-rc1'), 1);
    assert.strictEqual(F.compareVersions('1.2.40-beta1', '1.2.40-rc1'), -1);
    assert.strictEqual(F.compareVersions('1.2.40-alpha9', '1.2.40-beta1'), -1);
    assert.strictEqual(F.compareVersions('1.2.39', 'firmware-v1.2.39'), 0);
    assert.strictEqual(F.compareVersions('1.10.0', '1.9.9'), 1);
    assert.strictEqual(F.compareVersions('2.0.0', '1.99.99'), 1);
    assert.strictEqual(F.compareVersions('abc', '1.2.3'), null);
    assert.strictEqual(F.compareVersions('1.2.3', null), null);
});

check('relation_to_current', () => {
    assert.strictEqual(F.relation('1.2.39', 'firmware-v1.2.40-rc1'), 'newer');
    assert.strictEqual(F.relation('1.2.39', 'firmware-v1.2.39'), 'same');
    assert.strictEqual(F.relation('1.2.39', 'firmware-v1.2.30'), 'older');
    assert.strictEqual(F.relation('abc', 'firmware-v1.2.30'), 'unknown');
    assert.strictEqual(F.relation(null, 'firmware-v1.2.30'), 'unknown');
    assert.strictEqual(F.relation('1.2.40', 'firmware-v1.2.40-rc1'), 'older');
});

check('is_bin_asset', () => {
    assert.strictEqual(F.isBinAsset('atomfast-gw.bin'), true);
    assert.strictEqual(F.isBinAsset('x.BIN'), true);
    assert.strictEqual(F.isBinAsset('bootloader.bin'), false);
    assert.strictEqual(F.isBinAsset('partition-table.bin'), false);
    assert.strictEqual(F.isBinAsset('SHA256SUMS'), false);
    assert.strictEqual(F.isBinAsset('x.bin.sig'), false);
    assert.strictEqual(F.isBinAsset(null), false);
});

check('pick_assets_order', () => {
    const a = [{ id: 1, name: 'other.bin' }, { id: 2, name: 'atomfast-x.bin' }, { id: 3, name: 'bootloader.bin' }, { id: 4, name: 'AtomFast-y.bin' }];
    assert.deepStrictEqual(F.pickAssets(a).map(x => x.name), ['atomfast-x.bin', 'AtomFast-y.bin', 'other.bin']);
    assert.deepStrictEqual(F.pickAssets(null), []);
});

check('parse_digest', () => {
    const h = 'a'.repeat(64);
    assert.strictEqual(F.parseDigest('sha256:' + h), h);
    assert.strictEqual(F.parseDigest('sha256:' + h.toUpperCase()), h);
    assert.strictEqual(F.parseDigest('sha256:abc'), null);
    assert.strictEqual(F.parseDigest('md5:' + h), null);
    assert.strictEqual(F.parseDigest(null), null);
});

check('parse_releases_fixture_rows', () => {
    const r = F.parseReleases(FIX, '1.2.39');
    assert.strictEqual(r.ok, true);
    assert.strictEqual(r.rows.length, 4);
    assert.deepStrictEqual(r.rows.map(x => x.tag), ['firmware-v1.2.40-rc1', 'firmware-v1.2.39', 'firmware-v1.2.30', 'firmware-v1.2.26-rc1']);
    assert.deepStrictEqual(r.rows.map(x => x.relation), ['newer', 'same', 'older', 'older']);
    assert.deepStrictEqual(r.rows.map(x => x.installed), [false, true, false, false]);
    assert.deepStrictEqual(r.rows.map(x => x.prerelease), [true, false, false, true]);
    assert.strictEqual(r.rows[0].date, '24.10.2026');
    assert.strictEqual(r.rows[1].date, '20.10.2026');
    assert.ok(r.rows.every(x => x.htmlUrl.indexOf('https://github.com/') === 0));
});

check('parse_releases_assets', () => {
    const r = F.parseReleases(FIX, '1.2.39');
    assert.strictEqual(r.rows[1].assets.length, 1);
    assert.deepStrictEqual(r.rows[1].assets[0], { id: 990022, name: 'atomfast-gw-idf-1.2.39.bin', size: 1310720, digest: '3'.repeat(64), url: 'https://api.github.com/repos/VibeEngineering-LLC/atomfast-esp32/releases/assets/990022' });
    assert.strictEqual(r.rows[1].sumsAsset.name, 'SHA256SUMS');
    assert.strictEqual(r.rows[0].sumsAsset, null);
    assert.deepStrictEqual(r.rows[2].assets.map(a => a.name), ['atomfast-gw-s3.bin', 'atomfast-gw-c3.bin']);
    assert.strictEqual(r.rows[3].assets[0].digest, null);
});

check('parse_releases_draft_skipped_and_junk', () => {
    const r = F.parseReleases(FIX.concat([null, 5, {}, { tag_name: 7 }, { tag_name: 'v9.9.9', draft: true }]), '1.2.39');
    assert.strictEqual(r.rows.length, 4);
    assert.ok(!r.rows.some(x => x.tag === 'firmware-v1.2.38-draft'));
});

check('parse_releases_bad_json', () => {
    for (const bad of [null, {}, 'x', 5]) {
        assert.deepStrictEqual(F.parseReleases(bad, '1.2.39'), { ok: false, rows: [], error: 'GitHub вернул неожиданный ответ' });
    }
});

check('parse_releases_dangerous_html_url', () => {
    const r = F.parseReleases([{ tag_name: 'v1.2.3', html_url: 'javascript:alert(1)', assets: [] }], '1.2.3');
    assert.strictEqual(r.rows[0].htmlUrl, '');
    assert.strictEqual(r.rows[0].date, '—');
    assert.strictEqual(r.rows[0].name, 'v1.2.3');
});

check('install_plan_downgrade', () => {
    const rows = F.parseReleases(FIX, '1.2.39').rows;
    assert.deepStrictEqual(F.installPlan('1.2.39', rows[2]), { downgrade: true, same: false, unknown: false });
    assert.deepStrictEqual(F.installPlan('1.2.39', rows[1]), { downgrade: false, same: true, unknown: false });
    assert.deepStrictEqual(F.installPlan('1.2.39', rows[0]), { downgrade: false, same: false, unknown: false });
    const u = F.parseReleases(FIX, 'abc').rows[0];
    assert.deepStrictEqual(F.installPlan('abc', u), { downgrade: false, same: false, unknown: true });
});

check('confirm_message', () => {
    const rows = F.parseReleases(FIX, '1.2.39').rows;
    assert.strictEqual(F.confirmMessage('1.2.39', rows[0]), 'Обновить прошивку с 1.2.39 на firmware-v1.2.40-rc1. Связь с платой пропадёт примерно на 30–60 с, плата перезагрузится.');
    assert.strictEqual(F.confirmMessage('1.2.39', rows[2]), 'Обновить прошивку с 1.2.39 на firmware-v1.2.30. Связь с платой пропадёт примерно на 30–60 с, плата перезагрузится. Внимание: это понижение версии.');
    assert.strictEqual(F.confirmMessage(null, rows[0]), 'Обновить прошивку с неизвестной версии на firmware-v1.2.40-rc1. Связь с платой пропадёт примерно на 30–60 с, плата перезагрузится.');
});

check('can_install_order', () => {
    const s = { understood: true, needSumConfirm: false, sumConfirmed: false, downgrade: false, downgradeConfirmed: false };
    assert.deepStrictEqual(F.canInstall(s), { ok: true, reason: '' });
    assert.deepStrictEqual(F.canInstall(Object.assign({}, s, { understood: false })), { ok: false, reason: 'отметьте «понимаю»' });
    assert.deepStrictEqual(F.canInstall(Object.assign({}, s, { needSumConfirm: true })), { ok: false, reason: 'подтвердите установку без контрольной суммы' });
    assert.deepStrictEqual(F.canInstall(Object.assign({}, s, { needSumConfirm: true, sumConfirmed: true })), { ok: true, reason: '' });
    assert.deepStrictEqual(F.canInstall(Object.assign({}, s, { downgrade: true })), { ok: false, reason: 'подтвердите понижение версии' });
    assert.deepStrictEqual(F.canInstall(Object.assign({}, s, { downgrade: true, downgradeConfirmed: true })), { ok: true, reason: '' });
    assert.deepStrictEqual(F.canInstall(Object.assign({}, s, { understood: false, needSumConfirm: true, downgrade: true })), { ok: false, reason: 'отметьте «понимаю»' });
});

check('http_errors', () => {
    const limit = hdr({ 'x-ratelimit-remaining': '0', 'x-ratelimit-reset': '1000090' });
    assert.strictEqual(F.httpErrorMessage(403, limit, 1000000), 'Лимит запросов GitHub исчерпан, повторите через 2 мин');
    assert.strictEqual(F.httpErrorMessage(403, limit, 1000085), 'Лимит запросов GitHub исчерпан, повторите через 1 мин');
    assert.strictEqual(F.httpErrorMessage(403, hdr({ 'x-ratelimit-remaining': '0' }), 1000000), 'Лимит запросов GitHub исчерпан');
    assert.strictEqual(F.httpErrorMessage(429, hdr({}), 1000000), 'Лимит запросов GitHub исчерпан');
    assert.strictEqual(F.httpErrorMessage(403, hdr({ 'x-ratelimit-remaining': '12' }), 1000000), 'GitHub отказал в доступе (403)');
    assert.strictEqual(F.httpErrorMessage(404, hdr({}), 0), 'Репозиторий или релизы не найдены (404): проверьте имя репозитория');
    assert.strictEqual(F.httpErrorMessage(401, hdr({}), 0), 'GitHub отклонил запрос (401)');
    assert.strictEqual(F.httpErrorMessage(502, hdr({}), 0), 'GitHub временно недоступен (502)');
    assert.strictEqual(F.httpErrorMessage(418, null, 0), 'Ошибка GitHub (418)');
});

check('network_errors', () => {
    assert.strictEqual(F.networkErrorMessage('list'), 'Нет связи с GitHub: проверьте интернет и блокировщики рекламы');
    assert.ok(F.networkErrorMessage('download').indexOf('Скачайте файл вручную') > 0);
    assert.ok(F.networkErrorMessage('download').indexOf('CORS') > 0);
    assert.strictEqual(F.networkErrorMessage('zzz'), 'Сетевая ошибка');
});

check('format_size', () => {
    assert.strictEqual(F.formatSize(500), '500 Б');
    assert.strictEqual(F.formatSize(1536), '1.5 КиБ');
    assert.strictEqual(F.formatSize(1310720), '1.25 МиБ');
    assert.strictEqual(F.formatSize(NaN), '—');
    assert.strictEqual(F.formatSize(-1), '—');
});

check('parse_sums', () => {
    const h1 = 'a'.repeat(64), h2 = 'B'.repeat(64);
    const t = '# комментарий\n\n' + h1 + '  atomfast.bin\n' + h2 + ' *other.bin\nбитая строка\n';
    assert.deepStrictEqual(F.parseSums(t), { 'atomfast.bin': h1, 'other.bin': 'b'.repeat(64) });
    assert.deepStrictEqual(F.parseSums(null), {});
});

check('sha256_hex_matches_node', async () => {
    const b = image(3000);
    assert.strictEqual(await F.sha256Hex(b, SUBTLE), sha(b));
    assert.strictEqual(F.hexOf(new Uint8Array([0, 15, 255])), '000fff');
});

check('verify_image_confirmed_by_digest', async () => {
    const b = image(2048);
    const r = await F.verifyImage(b, { name: 'a.bin', size: 2048, digest: sha(b) }, null, SUBTLE);
    assert.strictEqual(r.ok, true);
    assert.strictEqual(r.sha, 'confirmed');
    assert.deepStrictEqual(r.errors, []);
    assert.strictEqual(r.actual, r.expected);
});

check('verify_image_digest_mismatch', async () => {
    const b = image(2048);
    const good = sha(b);
    b[100] ^= 1;
    const r = await F.verifyImage(b, { name: 'a.bin', size: 2048, digest: good }, null, SUBTLE);
    assert.strictEqual(r.ok, false);
    assert.strictEqual(r.sha, 'mismatch');
    assert.deepStrictEqual(r.errors, ['контрольная сумма SHA-256 не совпала']);
});

check('verify_image_size_mismatch', async () => {
    const b = image(2048);
    const r = await F.verifyImage(b, { name: 'a.bin', size: 2049, digest: sha(b) }, null, SUBTLE);
    assert.strictEqual(r.ok, false);
    assert.strictEqual(r.sizeOk, false);
    assert.deepStrictEqual(r.errors, ['размер файла не совпадает с GitHub']);
});

check('verify_image_bad_magic', async () => {
    const b = image(2048);
    b[0] = 0x00;
    const r = await F.verifyImage(b, { name: 'a.bin', size: 2048, digest: sha(b) }, null, SUBTLE);
    assert.strictEqual(r.ok, false);
    assert.strictEqual(r.magicOk, false);
    assert.deepStrictEqual(r.errors, ['не образ прошивки ESP (первый байт не 0xE9)']);
});

check('verify_image_unconfirmed_without_reference', async () => {
    const b = image(2048);
    const r = await F.verifyImage(b, { name: 'a.bin', size: 2048, digest: null }, null, SUBTLE);
    assert.strictEqual(r.ok, true);
    assert.strictEqual(r.sha, 'unconfirmed');
    assert.strictEqual(r.expected, null);
    assert.strictEqual(r.actual, null);
});

check('verify_image_by_sums_file', async () => {
    const b = image(2048);
    const ok = await F.verifyImage(b, { name: 'a.bin', size: 2048, digest: null }, { 'a.bin': sha(b) }, SUBTLE);
    assert.strictEqual(ok.sha, 'confirmed');
    const bad = await F.verifyImage(b, { name: 'a.bin', size: 2048, digest: null }, { 'a.bin': '0'.repeat(64) }, SUBTLE);
    assert.strictEqual(bad.sha, 'mismatch');
    assert.strictEqual(bad.ok, false);
});

check('verify_image_without_subtle_is_unconfirmed', async () => {
    const b = image(2048);
    const r = await F.verifyImage(b, { name: 'a.bin', size: 2048, digest: sha(b) }, null, null);
    assert.strictEqual(r.sha, 'unconfirmed');
    assert.strictEqual(r.ok, true);
});

check('verify_image_order_of_errors', async () => {
    const b = image(2048);
    b[0] = 0;
    const r = await F.verifyImage(b, { name: 'a.bin', size: 99, digest: '1'.repeat(64) }, null, SUBTLE);
    assert.deepStrictEqual(r.errors, ['размер файла не совпадает с GitHub', 'не образ прошивки ESP (первый байт не 0xE9)', 'контрольная сумма SHA-256 не совпала']);
});

check('manual_file_checks', () => {
    assert.deepStrictEqual(F.checkManualFile(image(2048)), { ok: true, error: '' });
    assert.deepStrictEqual(F.checkManualFile(new Uint8Array(0)), { ok: false, error: 'файл слишком маленький' });
    assert.deepStrictEqual(F.checkManualFile(image(1023)), { ok: false, error: 'файл слишком маленький' });
    assert.deepStrictEqual(F.checkManualFile(image(1024)), { ok: true, error: '' });
    assert.deepStrictEqual(F.checkManualFile({ length: 4194305, 0: 0xE9 }), { ok: false, error: 'файл больше 4 МиБ' });
    assert.deepStrictEqual(F.checkManualFile({ length: 4194304, 0: 0xE9 }), { ok: true, error: '' });
    const bad = image(2048);
    bad[0] = 0x7F;
    assert.deepStrictEqual(F.checkManualFile(bad), { ok: false, error: 'не образ прошивки ESP (первый байт не 0xE9)' });
});

check('http_error_boundaries', () => {
    assert.strictEqual(F.httpErrorMessage(500, hdr({}), 0), 'GitHub временно недоступен (500)');
    assert.strictEqual(F.httpErrorMessage(499, hdr({}), 0), 'Ошибка GitHub (499)');
});

check('digest_exact_length', () => {
    assert.strictEqual(F.parseDigest('sha256:' + 'abcdef0123'), null);
    assert.strictEqual(F.parseDigest('sha256:' + 'a'.repeat(63)), null);
    assert.strictEqual(F.parseDigest('sha256:' + 'a'.repeat(65)), null);
});

check('format_size_boundaries', () => {
    assert.strictEqual(F.formatSize(1024), '1.0 КиБ');
    assert.strictEqual(F.formatSize(1048575), '1024.0 КиБ');
    assert.strictEqual(F.formatSize(1048576), '1.00 МиБ');
});

check('asset_url_rejects_fraction_and_zero', () => {
    assert.strictEqual(F.assetUrl('o/n', 0), '');
    assert.strictEqual(F.assetUrl('o/n', 2.5), '');
});

check('repo_url_forms_strict', () => {
    assert.strictEqual(F.validateRepo('https://github.com/o/n.git').repo, 'o/n');
    assert.strictEqual(F.validateRepo('https://github.com/o/n.git/').repo, 'o/n');
    assert.strictEqual(F.validateRepo('https://github.com/o').ok, false);
    assert.strictEqual(F.validateRepo('https://example.com/o/n').ok, false);
    assert.strictEqual(F.validateRepo('a/b.git').repo, 'a/b.git');
});
check('base_from_query_only_on_localhost', () => {
    const q = '?gh=' + encodeURIComponent('http://localhost:8791/__gh');
    assert.strictEqual(F.baseFromQuery('localhost', q), 'http://localhost:8791/__gh');
    assert.strictEqual(F.baseFromQuery('127.0.0.1', q), 'http://localhost:8791/__gh');
    assert.strictEqual(F.baseFromQuery('192.0.2.131', q), undefined);
    assert.strictEqual(F.baseFromQuery('atomfast.local', q), undefined);
    assert.strictEqual(F.baseFromQuery('localhost.evil.example', q), undefined);
});

check('base_from_query_param_name_exact', () => {
    assert.strictEqual(F.baseFromQuery('localhost', '?xgh=' + encodeURIComponent('http://localhost:1/x')), undefined);
});

check('base_from_query_rejects_foreign_targets', () => {
    for (const bad of ['https://evil.example/api', 'http://evil.example:80/x', 'http://localhost.evil.example:1/x', 'file:///etc/passwd', 'javascript:alert(1)', 'http://localhost:@evil.example/x', 'http://localhost:80@evil.example/x', 'http://localhost:abc/x']) {
        assert.strictEqual(F.baseFromQuery('localhost', '?gh=' + encodeURIComponent(bad)), undefined, bad);
    }
    assert.strictEqual(F.baseFromQuery('localhost', ''), undefined);
    assert.strictEqual(F.baseFromQuery('localhost', '?vis=cps'), undefined);
    assert.strictEqual(F.baseFromQuery('localhost', null), undefined);
    assert.strictEqual(F.baseFromQuery('localhost', '?x=1&gh=' + encodeURIComponent('http://127.0.0.1:9/x/')), 'http://127.0.0.1:9/x');
});
(async () => {
    for (const [name, fn] of queue) {
        total++;
        try { await fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); }
    }
    console.log('TESTS: ' + passed + '/' + total);
    process.exit(passed === total ? 0 : 1);
})();
