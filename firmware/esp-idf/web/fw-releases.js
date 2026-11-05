(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(); } else { root.AFFW = factory(); } })(this, function () { 'use strict';

var DEFAULT_REPO = 'VibeEngineering-LLC/atomfast-esp32';
var API_BASE = 'https://api.github.com';
var MIN_IMAGE = 1024;
var MAX_IMAGE = 4194304;
var ACCEPT_JSON = 'application/vnd.github+json';
var ACCEPT_BIN = 'application/octet-stream';

function isFin(v) { return typeof v === 'number' && isFinite(v); }

function validateRepo(text) {
    if (typeof text !== 'string') {
        return { ok: false, repo: '', error: 'формат: владелец/репозиторий' };
    }
    var t = text.trim();
    var owner = '';
    var name = '';
    var m;
    if (t.indexOf('https://github.com/') === 0) {
        var rest = t.substring(19);
        if (rest.charAt(0) === '/') { rest = rest.substring(1); }
        if (rest.charAt(rest.length - 1) === '/') { rest = rest.substring(0, rest.length - 1); }
        if (rest.length > 4 && rest.substring(rest.length - 4) === '.git') {
            rest = rest.substring(0, rest.length - 4);
        }
        m = rest.match(/^([^/]+)\/([^/]+)$/);
        if (m) {
            owner = m[1];
            name = m[2];
        }
    } else {
        m = t.match(/^([^/]+)\/([^/]+)$/);
        if (m) {
            owner = m[1];
            name = m[2];
        }
    }
    if (!owner || !name) {
        return { ok: false, repo: '', error: 'формат: владелец/репозиторий' };
    }
    if (!/^[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})$/.test(owner)) {
        return { ok: false, repo: '', error: 'формат: владелец/репозиторий' };
    }
    if (!/^[A-Za-z0-9._-]{1,100}$/.test(name)) {
        return { ok: false, repo: '', error: 'формат: владелец/репозиторий' };
    }
    if (name === '.' || name === '..') {
        return { ok: false, repo: '', error: 'формат: владелец/репозиторий' };
    }
    return { ok: true, repo: owner + '/' + name, error: '' };
}

function releasesUrl(repo, base) {
    return (base || API_BASE) + '/repos/' + repo + '/releases?per_page=20';
}

function assetUrl(repo, id, base) {
    if (typeof id !== 'number' || !isFinite(id) || id <= 0 || Math.floor(id) !== id) {
        return '';
    }
    return (base || API_BASE) + '/repos/' + repo + '/releases/assets/' + id;
}

function isGithubUrl(url, base) {
    return typeof url === 'string' && url.indexOf((base || API_BASE) + '/') === 0;
}

function buildRequest(accept) {
    return {
        method: 'GET',
        headers: { Accept: accept },
        credentials: 'omit',
        referrerPolicy: 'no-referrer',
        mode: 'cors',
        cache: 'no-store'
    };
}

function githubGet(fetchFn, url, accept, base) {
    if (!isGithubUrl(url, base)) {
        throw new Error('не GitHub: запрос отклонён');
    }
    return fetchFn(url, buildRequest(accept));
}

function parseVersion(text) {
    if (typeof text !== 'string') {
        return null;
    }
    var m = text.match(/(\d+)\.(\d+)\.(\d+)(?:-([A-Za-z]+)(\d*))?/);
    if (!m) {
        return null;
    }
    var major = parseInt(m[1], 10);
    var minor = parseInt(m[2], 10);
    var patch = parseInt(m[3], 10);
    var preRaw = m[4] ? m[4].toLowerCase() : '';
    var preNum = m[5] ? parseInt(m[5], 10) : 0;
    var pre = '';
    if (preRaw === 'alpha' || preRaw === 'beta' || preRaw === 'rc') {
        pre = preRaw;
    } else {
        preNum = 0;
    }
    return { major: major, minor: minor, patch: patch, pre: pre, preNum: preNum };
}

function compareVersions(a, b) {
    var va = parseVersion(a);
    var vb = parseVersion(b);
    if (va === null || vb === null) {
        return null;
    }
    if (va.major !== vb.major) { return va.major < vb.major ? -1 : 1; }
    if (va.minor !== vb.minor) { return va.minor < vb.minor ? -1 : 1; }
    if (va.patch !== vb.patch) { return va.patch < vb.patch ? -1 : 1; }
    if (va.pre === '' && vb.pre === '') { return 0; }
    if (va.pre === '') { return 1; }
    if (vb.pre === '') { return -1; }
    var order = { alpha: 0, beta: 1, rc: 2 };
    var oa = order[va.pre];
    var ob = order[vb.pre];
    if (oa !== ob) { return oa < ob ? -1 : 1; }
    if (va.preNum !== vb.preNum) { return va.preNum < vb.preNum ? -1 : 1; }
    return 0;
}

function relation(current, tag) {
    var c = compareVersions(tag, current);
    if (c === null) { return 'unknown'; }
    if (c === 1) { return 'newer'; }
    if (c === -1) { return 'older'; }
    return 'same';
}

function isBinAsset(name) {
    if (typeof name !== 'string') { return false; }
    var lower = name.toLowerCase();
    if (lower.indexOf('bootloader') !== -1 || lower.indexOf('partition') !== -1) {
        return false;
    }
    return lower.substring(lower.length - 4) === '.bin';
}

function parseDigest(d) {
    if (typeof d !== 'string') { return null; }
    var m = d.match(/^sha256:([0-9a-f]{64})$/i);
    if (!m) { return null; }
    return m[1].toLowerCase();
}

function describeAsset(a) {
    return {
        id: a.id,
        name: a.name,
        size: a.size,
        digest: parseDigest(a.digest),
        url: a.url
    };
}

function pickAssets(assets) {
    if (!Array.isArray(assets)) { return []; }
    var filtered = [];
    for (var i = 0; i < assets.length; i++) {
        var a = assets[i];
        if (a && typeof a.name === 'string' && isBinAsset(a.name)) {
            filtered.push(a);
        }
    }
    filtered.sort(function (a, b) {
        var aHas = a.name.toLowerCase().indexOf('atomfast') !== -1;
        var bHas = b.name.toLowerCase().indexOf('atomfast') !== -1;
        if (aHas && !bHas) { return -1; }
        if (!aHas && bHas) { return 1; }
        return 0;
    });
    var result = [];
    for (var j = 0; j < filtered.length; j++) {
        result.push(describeAsset(filtered[j]));
    }
    return result;
}

function formatDate(iso) {
    if (typeof iso !== 'string') { return '—'; }
    var m = iso.match(/^(\d{4})-(\d{2})-(\d{2})T/);
    if (!m) { return '—'; }
    return m[3] + '.' + m[2] + '.' + m[1];
}

function parseReleases(json, currentVersion) {
    if (!Array.isArray(json)) {
        return { ok: false, rows: [], error: 'GitHub вернул неожиданный ответ' };
    }
    var rows = [];
    for (var i = 0; i < json.length; i++) {
        var item = json[i];
        if (!item || typeof item !== 'object') { continue; }
        if (typeof item.tag_name !== 'string') { continue; }
        if (item.draft === true) { continue; }
        var tag = item.tag_name;
        var name = item.name || tag;
        var date = formatDate(item.published_at || item.created_at);
        var prerelease = item.prerelease === true;
        var htmlUrl = '';
        if (typeof item.html_url === 'string' && item.html_url.indexOf('https://github.com/') === 0) {
            htmlUrl = item.html_url;
        }
        var rel = relation(currentVersion, tag);
        var installed = rel === 'same';
        var assets = pickAssets(item.assets);
        var sumsAsset = null;
        if (Array.isArray(item.assets)) {
            for (var k = 0; k < item.assets.length; k++) {
                var a = item.assets[k];
                if (a && typeof a.name === 'string') {
                    var n = a.name;
                    if (n === 'SHA256SUMS' || n === 'SHA256SUMS.txt' || n.toLowerCase().substring(n.length - 7) === '.sha256') {
                        sumsAsset = describeAsset(a);
                        break;
                    }
                }
            }
        }
        rows.push({
            tag: tag,
            name: name,
            date: date,
            prerelease: prerelease,
            htmlUrl: htmlUrl,
            relation: rel,
            installed: installed,
            assets: assets,
            sumsAsset: sumsAsset
        });
    }
    return { ok: true, rows: rows, error: '' };
}

function installPlan(currentVersion, row) {
    return {
        downgrade: row.relation === 'older',
        same: row.relation === 'same',
        unknown: row.relation === 'unknown'
    };
}

function confirmMessage(currentVersion, row) {
    var msg = 'Обновить прошивку с ' + (currentVersion ? currentVersion : 'неизвестной версии') + ' на ' + row.tag + '. Связь с платой пропадёт примерно на 30–60 с, плата перезагрузится.';
    if (row.relation === 'older') {
        msg += ' Внимание: это понижение версии.';
    }
    return msg;
}

function canInstall(state) {
    if (!state.understood) {
        return { ok: false, reason: 'отметьте «понимаю»' };
    }
    if (state.needSumConfirm && !state.sumConfirmed) {
        return { ok: false, reason: 'подтвердите установку без контрольной суммы' };
    }
    if (state.downgrade && !state.downgradeConfirmed) {
        return { ok: false, reason: 'подтвердите понижение версии' };
    }
    return { ok: true, reason: '' };
}

function httpErrorMessage(status, getHeader, nowSec) {
    var isRateLimit = false;
    if (status === 429) {
        isRateLimit = true;
    } else if (status === 403 && typeof getHeader === 'function' && getHeader('x-ratelimit-remaining') === '0') {
        isRateLimit = true;
    }
    if (isRateLimit) {
        var msg = 'Лимит запросов GitHub исчерпан';
        if (typeof getHeader === 'function') {
            var resetVal = getHeader('x-ratelimit-reset');
            if (resetVal !== null && resetVal !== undefined) {
                var resetNum = parseInt(resetVal, 10);
                if (isFinite(resetNum) && isFin(nowSec)) {
                    var N = Math.max(1, Math.ceil((resetNum - nowSec) / 60));
                    msg += ', повторите через ' + N + ' мин';
                }
            }
        }
        return msg;
    }
    if (status === 404) {
        return 'Репозиторий или релизы не найдены (404): проверьте имя репозитория';
    }
    if (status === 401) {
        return 'GitHub отклонил запрос (401)';
    }
    if (status === 403) {
        return 'GitHub отказал в доступе (403)';
    }
    if (status >= 500) {
        return 'GitHub временно недоступен (' + status + ')';
    }
    return 'Ошибка GitHub (' + status + ')';
}

function networkErrorMessage(phase) {
    if (phase === 'list') {
        return 'Нет связи с GitHub: проверьте интернет и блокировщики рекламы';
    }
    if (phase === 'download') {
        return 'Браузер не смог скачать файл с GitHub (возможно, запрет CORS или обрыв связи). Скачайте файл вручную по ссылке и загрузите его в блоке «Ручная заливка файла .bin»';
    }
    return 'Сетевая ошибка';
}

function formatSize(bytes) {
    if (!isFin(bytes) || bytes < 0) {
        return '—';
    }
    if (bytes < 1024) {
        return Math.round(bytes) + ' Б';
    }
    if (bytes < 1048576) {
        return (bytes / 1024).toFixed(1) + ' КиБ';
    }
    return (bytes / 1048576).toFixed(2) + ' МиБ';
}

function parseSums(text) {
    if (typeof text !== 'string') {
        return {};
    }
    var result = {};
    var lines = text.split('\n');
    for (var i = 0; i < lines.length; i++) {
        var line = lines[i].trim();
        if (line === '') { continue; }
        if (line.charAt(0) === '#') { continue; }
        var m = line.match(/^([0-9a-f]{64})[ \t]+(\*?)([^\s]+)$/i);
        if (!m) { continue; }
        var hex = m[1].toLowerCase();
        var name = m[3];
        result[name] = hex;
    }
    return result;
}

function hexOf(bytes) {
    var s = '';
    for (var i = 0; i < bytes.length; i++) {
        var b = bytes[i];
        var h = b.toString(16);
        if (h.length < 2) { h = '0' + h; }
        s += h;
    }
    return s;
}

async function sha256Hex(bytes, subtle) {
    var digest = await subtle.digest('SHA-256', bytes);
    return hexOf(new Uint8Array(digest));
}

async function verifyImage(bytes, asset, sums, subtle) {
    var sizeOk = !isFin(asset.size) || bytes.length === asset.size;
    var magicOk = bytes.length > 0 && bytes[0] === 0xE9;
    var expected = null;
    if (typeof asset.digest === 'string' && /^[0-9a-f]{64}$/i.test(asset.digest)) {
        expected = asset.digest.toLowerCase();
    } else if (sums && typeof sums === 'object' && typeof asset.name === 'string' && sums[asset.name]) {
        expected = sums[asset.name];
    }
    var sha = 'unconfirmed';
    var actual = null;
    if (expected && subtle) {
        actual = await sha256Hex(bytes, subtle);
        sha = actual === expected ? 'confirmed' : 'mismatch';
    }
    var errors = [];
    if (!sizeOk) {
        errors.push('размер файла не совпадает с GitHub');
    }
    if (!magicOk) {
        errors.push('не образ прошивки ESP (первый байт не 0xE9)');
    }
    if (sha === 'mismatch') {
        errors.push('контрольная сумма SHA-256 не совпала');
    }
    return {
        ok: sizeOk && magicOk && sha !== 'mismatch',
        sizeOk: sizeOk,
        magicOk: magicOk,
        sha: sha,
        expected: expected,
        actual: actual,
        errors: errors
    };
}

function baseFromQuery(hostname, search) {
    if (hostname !== 'localhost' && hostname !== '127.0.0.1') { return undefined; }
    if (typeof search !== 'string') { return undefined; }
    var m = search.match(/[?&]gh=([^&]*)/);
    if (!m) { return undefined; }
    var v;
    try { v = decodeURIComponent(m[1]); } catch (e) { return undefined; }
    v = v.replace(/\/+$/, '');
    if (!/^http:\/\/(localhost|127\.0\.0\.1):[0-9]{1,5}(\/[^@]*)?$/.test(v)) { return undefined; }
    return v;
}
function checkManualFile(bytes) {
    if (bytes.length < MIN_IMAGE) {
        return { ok: false, error: 'файл слишком маленький' };
    }
    if (bytes.length > MAX_IMAGE) {
        return { ok: false, error: 'файл больше 4 МиБ' };
    }
    if (bytes[0] !== 0xE9) {
        return { ok: false, error: 'не образ прошивки ESP (первый байт не 0xE9)' };
    }
    return { ok: true, error: '' };
}

return {
    DEFAULT_REPO: DEFAULT_REPO,
    baseFromQuery: baseFromQuery,
    API_BASE: API_BASE,
    MIN_IMAGE: MIN_IMAGE,
    MAX_IMAGE: MAX_IMAGE,
    ACCEPT_JSON: ACCEPT_JSON,
    ACCEPT_BIN: ACCEPT_BIN,
    validateRepo: validateRepo,
    releasesUrl: releasesUrl,
    assetUrl: assetUrl,
    isGithubUrl: isGithubUrl,
    buildRequest: buildRequest,
    githubGet: githubGet,
    parseVersion: parseVersion,
    compareVersions: compareVersions,
    relation: relation,
    isBinAsset: isBinAsset,
    parseDigest: parseDigest,
    describeAsset: describeAsset,
    pickAssets: pickAssets,
    formatDate: formatDate,
    parseReleases: parseReleases,
    installPlan: installPlan,
    confirmMessage: confirmMessage,
    canInstall: canInstall,
    httpErrorMessage: httpErrorMessage,
    networkErrorMessage: networkErrorMessage,
    formatSize: formatSize,
    parseSums: parseSums,
    hexOf: hexOf,
    sha256Hex: sha256Hex,
    verifyImage: verifyImage,
    checkManualFile: checkManualFile
};
});
