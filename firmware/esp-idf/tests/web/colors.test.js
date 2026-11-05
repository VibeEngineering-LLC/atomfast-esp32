const assert = require('assert');
const fs = require('fs');
const path = require('path');
const C = require(process.env.HK_MODULE || path.join(__dirname, '..', '..', 'web', 'colors.js'));
const CSS = fs.readFileSync(path.join(__dirname, '..', '..', 'web', 'ui-theme.css'), 'utf8');
let passed = 0, total = 0;
const queue = [];
function check(name, fn) { queue.push([name, fn]); }
const near = (a, b, e) => Math.abs(a - b) <= e;
const E = () => C.emptyState();
const tokens = (block) => { const o = {}; const re = /--s([1-8]):(#[0-9a-fA-F]{6})/g; let m; while ((m = re.exec(block))) o['s' + m[1]] = m[2].toLowerCase(); return o; };

check('hex_valid_forms', () => {
    for (const h of ['#000000', '#FFFFFF', '#0b7f72', '#AbCdEf']) assert.strictEqual(C.hexOk(h), true, h);
});

check('hex_invalid_forms', () => {
    for (const h of ['000000', '#fff', '#ggg000', '#12345', '#1234567', ' #123456', '#123456 ', '', null, undefined, 123456, '#12 456']) assert.strictEqual(C.hexOk(h), false, String(h));
});

check('luminance_reference', () => {
    assert.ok(near(C.luminance('#ffffff'), 1, 1e-9));
    assert.ok(near(C.luminance('#000000'), 0, 1e-9));
    assert.ok(near(C.luminance('#808080'), 0.2159, 0.0005));
    assert.ok(near(C.luminance('#ff0000'), 0.2126, 1e-4));
    assert.ok(near(C.luminance('#00ff00'), 0.7152, 1e-4));
    assert.ok(near(C.luminance('#0000ff'), 0.0722, 1e-4));
});

check('contrast_reference_pairs', () => {
    assert.ok(near(C.contrast('#000000', '#ffffff'), 21, 1e-9));
    assert.ok(near(C.contrast('#777777', '#ffffff'), 4.478, 0.005));
    assert.ok(near(C.contrast('#767676', '#ffffff'), 4.542, 0.005));
    assert.ok(near(C.contrast('#595959', '#ffffff'), 7.005, 0.005));
    assert.ok(near(C.contrast('#808080', '#ffffff'), 3.949, 0.005));
    assert.ok(near(C.contrast('#949494', '#ffffff'), 3.033, 0.005));
    assert.ok(near(C.contrast('#ffffff', '#ffffff'), 1, 1e-9));
});

check('contrast_symmetric', () => {
    assert.strictEqual(C.contrast('#0b7f72', '#11150f'), C.contrast('#11150f', '#0b7f72'));
});

check('contrast_uses_gamma', () => {
    assert.ok(C.contrast('#808080', '#ffffff') > 3.9 && C.contrast('#808080', '#ffffff') < 4.0);
    assert.ok(C.contrast('#34d3c0', '#11150f') > 9 && C.contrast('#34d3c0', '#11150f') < 10.5);
});

check('backgrounds_match_theme_css', () => {
    assert.strictEqual(C.BG.light, '#ffffff');
    assert.strictEqual(C.BG.dark, '#11150f');
    const lightBlock = CSS.slice(CSS.indexOf(':root{'), CSS.indexOf(':root[data-theme="dark"]'));
    const darkBlock = CSS.slice(CSS.indexOf(':root[data-theme="dark"]'), CSS.indexOf('@media (prefers-color-scheme: dark)'));
    assert.ok(/--card:#ffffff;/.test(lightBlock));
    assert.ok(/--card:#11150f;/.test(darkBlock));
});

check('defaults_match_design_tokens', () => {
    const lightBlock = CSS.slice(CSS.indexOf(':root{'), CSS.indexOf(':root[data-theme="dark"]'));
    const darkBlock = CSS.slice(CSS.indexOf(':root[data-theme="dark"]'), CSS.indexOf('@media (prefers-color-scheme: dark)'));
    const l = tokens(lightBlock);
    const d = tokens(darkBlock);
    C.IDS.forEach((id, i) => {
        assert.strictEqual(C.DEFAULTS.light[id], l['s' + (i + 1)], 'light ' + id);
        assert.strictEqual(C.DEFAULTS.dark[id], d['s' + (i + 1)], 'dark ' + id);
    });
});

check('defaults_pass_contrast', () => {
    for (const t of ['light', 'dark']) for (const id of C.IDS) assert.strictEqual(C.usable(C.DEFAULTS[t][id], t), true, t + ' ' + id);
});

check('series_ids_order', () => {
    assert.deepStrictEqual(C.IDS, ['rate', 'cps', 'pulses', 'dose', 'temp', 'air_t', 'air_p', 'air_h']);
    assert.strictEqual(C.varName('rate'), '--s1');
    assert.strictEqual(C.varName('air_p'), '--s7');
    assert.strictEqual(C.varName('air_h'), '--s8');
});

check('check_format_error', () => {
    const r = C.check('red', 'light');
    assert.strictEqual(r.ok, false);
    assert.ok(r.error.indexOf('#RRGGBB') !== -1);
});

check('check_contrast_threshold', () => {
    assert.strictEqual(C.MIN_CONTRAST, 3);
    assert.strictEqual(C.check('#949494', 'light').ok, true);
    assert.strictEqual(C.check('#959595', 'light').ok, false);
    assert.ok(C.check('#959595', 'light').error.indexOf('ниже 3:1') !== -1);
    assert.ok(C.check('#959595', 'light').error.indexOf('3.0') !== -1);
});

check('check_per_theme', () => {
    assert.strictEqual(C.usable('#ffff00', 'light'), false);
    assert.strictEqual(C.usable('#ffff00', 'dark'), true);
    assert.strictEqual(C.usable('#0b7f72', 'dark'), true);
    assert.strictEqual(C.usable('#1b5e20', 'dark'), false);
    assert.strictEqual(C.usable('#1b5e20', 'light'), true);
    assert.strictEqual(C.check('#000000', 'nope').ok, false);
});

check('palette_shape', () => {
    assert.strictEqual(C.PALETTE.length, 18);
    assert.strictEqual(new Set(C.PALETTE).size, 18);
    for (const h of C.PALETTE) {
        assert.strictEqual(C.hexOk(h), true, h);
        assert.ok(C.usable(h, 'light') || C.usable(h, 'dark'), h);
    }
});

check('palette_marks_unusable_per_theme', () => {
    const offL = C.PALETTE.filter(h => !C.usable(h, 'light'));
    const offD = C.PALETTE.filter(h => !C.usable(h, 'dark'));
    assert.ok(offL.length >= 5, 'light off ' + offL.length);
    assert.ok(offD.length >= 1, 'dark off ' + offD.length);
    assert.ok(offL.length < 18 && offD.length < 18);
});

check('set_color_normalizes_and_isolates_theme', () => {
    const r = C.setColor(E(), 'dark', 'rate', '#FF00AA');
    assert.strictEqual(r.ok, true);
    assert.strictEqual(r.state.colors.dark.rate, '#ff00aa');
    assert.strictEqual(r.state.colors.light.rate, undefined);
    assert.strictEqual(C.effectiveColor(r.state, 'dark', 'rate'), '#ff00aa');
    assert.strictEqual(C.effectiveColor(r.state, 'light', 'rate'), C.DEFAULTS.light.rate);
});

check('set_color_light_does_not_touch_dark', () => {
    const r = C.setColor(E(), 'light', 'cps', '#112233');
    assert.strictEqual(r.ok, true);
    assert.deepStrictEqual(r.state.colors.dark, {});
    assert.strictEqual(r.state.colors.light.cps, '#112233');
});

check('set_color_rejects_bad_without_change', () => {
    const s = E();
    for (const [t, id, h] of [['light', 'rate', 'zzz'], ['light', 'rate', '#ffff00'], ['dark', 'rate', '#000000'], ['light', 'nope', '#112233'], ['nope', 'rate', '#112233'], ['light', 'rate', '']]) {
        const r = C.setColor(s, t, id, h);
        assert.strictEqual(r.ok, false, t + id + h);
        assert.strictEqual(r.state, s);
        assert.ok(r.error.length > 5);
    }
});

check('set_color_is_immutable', () => {
    const s = E();
    const r = C.setColor(s, 'light', 'rate', '#112233');
    assert.deepStrictEqual(s, E());
    assert.notStrictEqual(r.state, s);
});

check('reset_color_scope', () => {
    let s = C.setColor(E(), 'light', 'rate', '#112233').state;
    s = C.setColor(s, 'dark', 'rate', '#ddeeff').state;
    s = C.setColor(s, 'light', 'cps', '#223344').state;
    const r = C.resetColor(s, 'light', 'rate');
    assert.strictEqual(r.colors.light.rate, undefined);
    assert.strictEqual(r.colors.dark.rate, '#ddeeff');
    assert.strictEqual(r.colors.light.cps, '#223344');
});

check('reset_color_clears_style_of_series', () => {
    let s = C.setStyle(E(), 'rate', 'dots', 3).state;
    s = C.setStyle(s, 'cps', 'dash', 1).state;
    const r = C.resetColor(s, 'light', 'rate');
    assert.strictEqual(r.styles.rate, undefined);
    assert.deepStrictEqual(r.styles.cps, { dash: 'dash', width: 1 });
});

check('reset_all', () => {
    let s = C.setColor(E(), 'light', 'rate', '#112233').state;
    s = C.setStyle(s, 'rate', 'dots', 3).state;
    assert.deepStrictEqual(C.resetAll(s), E());
});

check('style_defaults_match_legacy_series', () => {
    const d = (id) => C.effectiveStyle(E(), id);
    assert.deepStrictEqual(d('rate').dash, []);
    assert.deepStrictEqual(d('cps').dash, []);
    assert.deepStrictEqual(d('pulses').dash, [6, 4]);
    assert.deepStrictEqual(d('dose').dash, [2, 3]);
    assert.deepStrictEqual(d('temp').dash, [10, 3, 2, 3]);
    assert.deepStrictEqual(d('air_t').dash, [8, 3]);
    assert.deepStrictEqual(d('air_p').dash, [3, 2]);
    assert.strictEqual(d('rate').width, 2);
});

check('style_set_and_default_removal', () => {
    const r = C.setStyle(E(), 'rate', 'dots', 4);
    assert.strictEqual(r.ok, true);
    assert.deepStrictEqual(C.effectiveStyle(r.state, 'rate'), { dashKey: 'dots', dash: [2, 3], width: 4 });
    const back = C.setStyle(r.state, 'rate', '', 0);
    assert.strictEqual(back.state.styles.rate, undefined);
    const partial = C.setStyle(E(), 'cps', '', 3).state;
    assert.deepStrictEqual(partial.styles.cps, { width: 3 });
    assert.deepStrictEqual(C.effectiveStyle(partial, 'cps').dash, []);
});

check('style_width_bounds', () => {
    for (const w of [1, 2, 3, 4]) assert.strictEqual(C.setStyle(E(), 'rate', '', w).ok, true, String(w));
    for (const w of [5, -1, 2.5, '2', NaN, null]) assert.strictEqual(C.setStyle(E(), 'rate', '', w).ok, false, String(w));
});

check('style_rejects_unknown', () => {
    assert.strictEqual(C.setStyle(E(), 'rate', 'zigzag', 2).ok, false);
    assert.strictEqual(C.setStyle(E(), 'nope', 'dash', 2).ok, false);
    assert.strictEqual(C.setStyle(E(), 'rate', 'constructor', 2).ok, false);
});

check('parse_garbage_gives_default', () => {
    for (const t of ['', 'x', '{', 'null', '[]', '5', '{"v":2}', '{"v":"1"}', '{}']) assert.deepStrictEqual(C.parse(t), E(), t);
});

check('parse_keeps_valid_ignores_invalid', () => {
    const txt = JSON.stringify({ v: 1, colors: { light: { rate: '#ABCDEF', cps: 'red', pulses: '#12', nope: '#112233' }, dark: { rate: '#102030', dose: 5 } }, styles: { rate: { dash: 'dots', width: 3 }, cps: { dash: 'zig', width: 9 }, dose: { width: 1.5 } }, cb: true });
    const s = C.parse(txt);
    assert.deepStrictEqual(s.colors.light, { rate: '#abcdef' });
    assert.deepStrictEqual(s.colors.dark, { rate: '#102030' });
    assert.deepStrictEqual(s.styles, { rate: { dash: 'dots', width: 3 } });
    assert.strictEqual(s.cb, true);
});

check('parse_non_object_sections', () => {
    const s = C.parse(JSON.stringify({ v: 1, colors: 5, styles: [] }));
    assert.deepStrictEqual(s, E());
    assert.deepStrictEqual(C.parse(JSON.stringify({ v: 1, colors: { light: 'x', dark: null }, styles: { rate: 'x' } })), E());
});

check('serialize_roundtrip', () => {
    let s = C.setColor(E(), 'light', 'rate', '#112233').state;
    s = C.setColor(s, 'dark', 'air_p', '#ddeeff').state;
    s = C.setStyle(s, 'dose', 'dash', 4).state;
    assert.deepStrictEqual(C.parse(C.serialize(s)), s);
    assert.deepStrictEqual(C.parse(C.serialize(C.colorblind())), C.colorblind());
});

check('storage_key', () => {
    assert.strictEqual(C.KEY, 'afh.colors.v1');
});

check('css_empty_for_defaults', () => {
    assert.strictEqual(C.cssText(E()), '');
});

check('css_light_only', () => {
    const css = C.cssText(C.setColor(E(), 'light', 'cps', '#112233').state);
    assert.strictEqual(css, ':root{--s2:#112233;}');
});

check('css_dark_two_blocks', () => {
    const css = C.cssText(C.setColor(E(), 'dark', 'air_p', '#ddeeff').state);
    assert.strictEqual(css, ':root[data-theme="dark"]{--s7:#ddeeff;}@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--s7:#ddeeff;}}');
    assert.ok(css.indexOf(':root{') === -1);
});

check('css_themes_do_not_mix', () => {
    let s = C.setColor(E(), 'light', 'rate', '#112233').state;
    s = C.setColor(s, 'dark', 'rate', '#ddeeff').state;
    const css = C.cssText(s);
    assert.ok(css.startsWith(':root{--s1:#112233;}'));
    assert.strictEqual(css.split('#112233').length - 1, 1);
    assert.strictEqual(css.split('#ddeeff').length - 1, 2);
});

check('cb_palettes_contrast', () => {
    for (const t of ['light', 'dark']) {
        assert.strictEqual(C.CB[t].length, 8);
        for (const h of C.CB[t]) assert.ok(C.usable(h, t), t + ' ' + h + ' ' + C.contrast(h, C.BG[t]));
        assert.strictEqual(new Set(C.CB[t]).size, 8);
    }
});

check('cb_state_applies_both_themes', () => {
    const s = C.colorblind();
    assert.strictEqual(s.cb, true);
    C.IDS.forEach((id, i) => {
        assert.strictEqual(C.effectiveColor(s, 'light', id), C.CB.light[i]);
        assert.strictEqual(C.effectiveColor(s, 'dark', id), C.CB.dark[i]);
    });
});

check('cb_pairwise_distinct_normal_vision', () => {
    for (const t of ['light', 'dark']) assert.ok(C.minPairDelta(C.CB[t], 'none') >= 15, t + ' ' + C.minPairDelta(C.CB[t], 'none'));
});

check('cb_pairwise_distinct_cvd', () => {
    for (const t of ['light', 'dark']) for (const m of ['deu', 'pro']) assert.ok(C.minPairDelta(C.CB[t], m) >= 5, t + ' ' + m + ' ' + C.minPairDelta(C.CB[t], m));
});

check('cb_line_styles_all_different', () => {
    const s = C.colorblind();
    const seen = new Set(C.IDS.map(id => JSON.stringify(C.effectiveStyle(s, id).dash)));
    assert.strictEqual(seen.size, 8);
});

check('default_palette_not_enough_for_cvd_without_styles', () => {
    assert.ok(C.minPairDelta(C.IDS.map(id => C.DEFAULTS.light[id]), 'deu') < 15);
});

check('manual_edit_leaves_cb_mode', () => {
    const s = C.setColor(C.colorblind(), 'light', 'rate', '#112233').state;
    assert.strictEqual(s.cb, false);
});

check('delta_e_basics', () => {
    assert.strictEqual(C.deltaE('#123456', '#123456', 'none'), 0);
    assert.ok(near(C.deltaE('#000000', '#ffffff', 'none'), 100, 0.5));
    assert.ok(near(C.deltaE('#ff0000', '#00ff00', 'none'), C.deltaE('#00ff00', '#ff0000', 'none'), 1e-9));
    assert.ok(C.deltaE('#d55e00', '#009e73', 'deu') < C.deltaE('#d55e00', '#009e73', 'none'));
});

check('effective_color_default_is_per_theme', () => {
    assert.strictEqual(C.effectiveColor(E(), 'dark', 'rate'), C.DEFAULTS.dark.rate);
    assert.strictEqual(C.effectiveColor(E(), 'light', 'air_p'), C.DEFAULTS.light.air_p);
    assert.notStrictEqual(C.DEFAULTS.dark.rate, C.DEFAULTS.light.rate);
});

check('parse_rejects_other_versions_with_data', () => {
    assert.deepStrictEqual(C.parse(JSON.stringify({ v: 2, colors: { light: { rate: '#123456' }, dark: {} }, styles: {} })), E());
    assert.deepStrictEqual(C.parse(JSON.stringify({ colors: { light: { rate: '#123456' } } })), E());
});

check('parse_cb_flag_strict', () => {
    assert.strictEqual(C.parse(JSON.stringify({ v: 1, cb: 'yes' })).cb, false);
    assert.strictEqual(C.parse(JSON.stringify({ v: 1, cb: 1 })).cb, false);
    assert.strictEqual(C.parse(JSON.stringify({ v: 1, cb: true })).cb, true);
});

check('cb_is_okabe_ito', () => {
    const oi = ['#0072b2', '#d55e00', '#009e73', '#cc79a7'];
    for (const t of ['light', 'dark']) assert.deepStrictEqual(C.CB[t].slice(0, 4), oi);
    assert.strictEqual(C.CB.light[6], '#000000');
    assert.strictEqual(C.CB.dark[6], '#ffffff');
    assert.strictEqual(C.CB.dark[4], '#e69f00');
    assert.strictEqual(C.CB.dark[5], '#56b4e9');
});
check('tint_mixes_toward_black_on_light_white_on_dark', () => {
    assert.strictEqual(C.tint('#c2255c', 0, 'light'), '#c2255c');
    assert.strictEqual(C.tint('#ff0000', 1, 'light'), '#c70000');
    assert.strictEqual(C.tint('#000000', 1, 'dark'), '#383838');
    assert.strictEqual(C.tint('bad', 2, 'light'), 'bad');
    assert.strictEqual(C.tint('#c2255c', -1, 'light'), '#c2255c');
    assert.strictEqual(C.tint('#c2255c', NaN, 'dark'), '#c2255c');
    assert.strictEqual(C.tint('#808080', 99, 'light'), C.tint('#808080', 7, 'light'));
});

check('tint_keeps_contrast_for_all_eight_sensors', () => {
    for (const t of ['light', 'dark']) for (const id of ['air_t', 'air_p', 'air_h']) {
        const seen = new Set();
        for (let k = 0; k < 8; k++) {
            const h = C.tint(C.DEFAULTS[t][id], k, t);
            assert.ok(C.usable(h, t), t + ' ' + id + ' ' + k + ' ' + h);
            seen.add(h);
        }
        assert.strictEqual(seen.size, 8, t + ' ' + id);
    }
});

check('восьмой цвет и штрих рядов: влажность', () => {
    assert.strictEqual(C.DEFAULTS.light.air_h, '#6b6b00');
    assert.strictEqual(C.DEFAULTS.dark.air_h, '#d0d36a');
    assert.strictEqual(C.CB.light[7], '#767676');
    assert.strictEqual(C.CB.dark[7], '#a0a0a0');
    assert.strictEqual(new Set(['air_t', 'air_p', 'air_h'].map(id => C.DEFAULT_DASH[id])).size, 3);
});

check('tint_dash_distinct_per_index', () => {
    assert.deepStrictEqual(C.tintDash([8, 3], 0), [8, 3]);
    const seen = new Set();
    for (let k = 1; k <= 7; k++) seen.add(JSON.stringify(C.tintDash([], k)));
    assert.strictEqual(seen.size, 7);
    const d = C.tintDash([], 1); d.push(9);
    assert.deepStrictEqual(C.tintDash([], 1), [6, 3]);
});

(async () => {
    for (const [name, fn] of queue) {
        total++;
        try { await fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); }
    }
console.log('TESTS: ' + passed + '/' + total);
    process.exit(passed === total ? 0 : 1);
})();
