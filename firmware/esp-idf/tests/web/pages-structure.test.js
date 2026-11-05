const assert = require('assert');
const fs = require('fs');
const path = require('path');
const W = path.join(__dirname, '..', '..', 'web');
const SRC = fs.readFileSync(process.env.HP_SRC || path.join(W, 'history.src.html'), 'utf8');
const SYS = fs.readFileSync(process.env.HP_SYS || path.join(W, 'system-view.html'), 'utf8');
const CHROME = fs.readFileSync(process.env.HP_CHROME || path.join(W, 'ui-chrome.html'), 'utf8');
const AFU = require(path.join(W, 'ui-common.js'));
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }

const START = SRC.indexOf('<div data-view="settings">');
const END = SRC.indexOf('<section class="card" data-view="history" aria-label="Экспорт">');
const READINGS = START >= 0 && END > START ? SRC.slice(START, END) : '';

const GROUPS = {
    sensors: ['cfgEnabled', 'cfgTOff', 'cfgPOff', 'cfgAgg', 'cfgPeriod', 'btnCfgSave', 'cfgResult'],
    mqtt: ['mqState', 'mqEnabled', 'mqHost', 'mqPort', 'mqUser', 'mqPass', 'mqPassHint', 'mqPassClear', 'mqPeriod', 'mqPrefix', 'btnMqSave', 'mqResult'],
    maint: ['timeInd', 'btnTime', 'timeHint', 'timeResult', 'btnReboot', 'btnReconnect']
};
const ALL_IDS = [].concat(GROUPS.sensors, GROUPS.mqtt, GROUPS.maint);

function sections(html) {
    const out = {};
    const re = /<section class="sec" id="sec-(\w+)"[^>]*>([\s\S]*?)\n      <\/section>/g;
    let m;
    while ((m = re.exec(html)) !== null) out[m[1]] = m[2];
    return out;
}
function count(s, sub) { return s.split(sub).length - 1; }

check('readings_view_found', () => {
    assert.ok(READINGS.length > 50);
    assert.ok(READINGS.includes('id="readings"'));
    assert.ok(READINGS.includes('id="sensorLine"'));
    assert.ok(READINGS.includes('<h2>Показания</h2>'));
});

check('readings_has_no_forms', () => {
    assert.strictEqual(count(READINGS, '<input'), 0);
    assert.strictEqual(count(READINGS, '<button'), 0);
    assert.strictEqual(count(READINGS, '<form'), 0);
    assert.strictEqual(count(READINGS, '<select'), 0);
});

check('old_ids_present_once_on_system', () => {
    for (const id of ALL_IDS) assert.strictEqual(count(SYS, 'id="' + id + '"'), 1, id);
});

check('old_ids_absent_from_readings_and_main_source', () => {
    for (const id of ALL_IDS) {
        assert.strictEqual(count(READINGS, 'id="' + id + '"'), 0, 'readings ' + id);
        assert.strictEqual(count(SRC, 'id="' + id + '"'), 0, 'source ' + id);
    }
});

check('forms_in_right_sections', () => {
    const secs = sections(SYS);
    for (const g of Object.keys(GROUPS)) {
        assert.ok(secs[g], 'section ' + g);
        for (const id of GROUPS[g]) assert.ok(secs[g].includes('id="' + id + '"'), g + ' lacks ' + id);
        for (const other of Object.keys(secs)) {
            if (other === g) continue;
            for (const id of GROUPS[g]) assert.ok(!secs[other].includes('id="' + id + '"'), id + ' also in ' + other);
        }
    }
});

check('time_card_in_maintenance', () => {
    const secs = sections(SYS);
    assert.ok(secs.maint.includes('<h2>Время платы</h2>'));
    assert.ok(secs.maint.includes('Задать время из браузера'));
});

check('mqtt_form_next_to_status', () => {
    const mq = sections(SYS).mqtt;
    assert.ok(mq.indexOf('id="m_en"') >= 0 && mq.indexOf('id="mqHost"') > mq.indexOf('id="m_en"'));
});

check('nav_tabs', () => {
    const links = [];
    const re = /<a href="([^"]+)" data-nav="(\w+)">([^<]+)<\/a>/g;
    let m;
    while ((m = re.exec(CHROME)) !== null) links.push([m[1], m[2], m[3]]);
    assert.deepStrictEqual(links, [['/', 'history', 'Главная'], ['/readings', 'settings', 'Показания'], ['/measure', 'measure', 'Измерения'], ['/system', 'system', 'Система']]);
    assert.ok(!CHROME.includes('Настройки'));
});

check('page_aliases', () => {
    assert.strictEqual(AFU.pageOf('/readings'), 'settings');
    assert.strictEqual(AFU.pageOf('/settings'), 'settings');
    assert.strictEqual(AFU.pageOf('/system'), 'system');
    assert.strictEqual(AFU.pageOf('/measure'), 'measure');
    assert.strictEqual(AFU.pageOf('/series'), 'measure');
    assert.strictEqual(AFU.pageOf('/'), 'history');
});

check('no_dead_links_to_settings', () => {
    for (const s of [SRC, SYS, CHROME]) {
        assert.ok(!s.includes('href="/settings"'));
        assert.ok(!s.includes('на странице <a href="/settings">'));
    }
    assert.ok(count(SYS, 'href="/readings"') >= 1);
    assert.ok(SYS.includes('href="/"'));
});

check('old_reboot_hint_removed', () => {
    assert.ok(!SYS.includes('Перезагрузка платы и переподключение'));
    assert.ok(!SRC.includes('Перезагрузка платы и переподключение'));
});

check('api_paths_unchanged', () => {
    const calls = [
        "fetchJSON('/api/sensors')",
        "fetchJSON('/api/mqtt')", "postJSON('/api/mqtt', v.body)",
        "fetchJSON('/api/time', {", "fetchJSON('/api/csrf-token')",
        "postJSON('/api/ble-reconnect', {})", "postJSON('/api/reboot-esp', {})",
        "fetchJSON('/api/status')", "fetchJSON('/api/history/status')"
    ];
    for (const c of calls) assert.ok(SRC.includes(c), c);
    const UI = require('fs').readFileSync(require('path').join(__dirname, '..', '..', 'web', 'sensors-ui.js'), 'utf8');
    for (const c of ["snPost('/api/sensors', v.body", "snPost('/api/sensors/model'", "snPost('/api/sensors', { action: 'scan' }", "await postJSON(url, body)"]) assert.ok(UI.includes(c), c);
});

check('system_nav_has_sections_with_forms', () => {
    for (const s of ['sensors', 'mqtt', 'maint']) assert.ok(SYS.includes('data-sec="' + s + '"'), s);
});

console.log('TESTS: ' + passed + '/' + total); process.exit(passed === total ? 0 : 1);
