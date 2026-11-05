(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const wait = P.wait || 2500;
  const KEY = 'afh.colors.v1';
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const res = [];
  const t = (n, cond, d) => { res.push({ n, ok: !!cond, d: d || '' }); };
  let cur = null;
  const open = async (path, pre, theme) => {
    if (cur) { try { cur.f.remove(); } catch (e) {} cur = null; }
    try { localStorage.clear(); if (pre !== undefined) localStorage.setItem(KEY, pre); } catch (e) {}
    const f = document.createElement('iframe');
    f.style.cssText = 'position:fixed;left:0;top:0;border:0;opacity:0;pointer-events:none;width:' + width + 'px;height:1100px;';
    const parts = path.split('#');
    f.src = location.origin + parts[0] + (parts[0].indexOf('?') === -1 ? '?' : '&') + 'theme=' + (theme || 'light') + (parts[1] ? '#' + parts[1] : '');
    document.body.appendChild(f);
    await new Promise(r => { f.onload = r; setTimeout(r, 15000); });
    await sleep(wait);
    cur = { f, W: f.contentWindow, D: f.contentDocument };
    return cur;
  };
  const click = (el, W) => el.dispatchEvent(new W.MouseEvent('click', { bubbles: true, cancelable: true }));
  const typeIn = (el, v, W) => { el.value = v; el.dispatchEvent(new W.Event('input', { bubbles: true })); };
  const selectIn = (el, v, W) => { el.value = v; el.dispatchEvent(new W.Event('change', { bubbles: true })); };
  const stored = W => { try { return JSON.parse(W.localStorage.getItem(KEY)); } catch (e) { return null; } };
  const cssv = (W, name) => W.getComputedStyle(W.document.documentElement).getPropertyValue(name).trim();
  const swatch = (D, hex) => D.querySelector('.clr-sw[data-hex="' + hex + '"]');
  const pixel = (D, x, y) => Array.from(D.getElementById('clrPrev').getContext('2d').getImageData(x, y, 1, 1).data).slice(0, 3).join(',');
  const msg = D => D.getElementById('clrMsg');
  try {
    const { W, D } = await open('/system#maint');
    const card = D.getElementById('clrCard');
    const pal = D.querySelectorAll('#clrPal .clr-sw');
    t('card_shape', !!card && card.offsetParent !== null && D.querySelectorAll('#clrSeries .clr-ser').length === 8 && pal.length === 18 && D.getElementById('clrHex').value === '#0b7f72', 'pal=' + pal.length + ' hex=' + D.getElementById('clrHex').value);
    const offL = Array.from(pal).filter(b => b.disabled).length;
    click(D.querySelector('#segScheme button[data-scheme="dark"]'), W);
    const offD = Array.from(D.querySelectorAll('#clrPal .clr-sw')).filter(b => b.disabled).length;
    t('unusable_marked_per_theme', offL >= 5 && offD >= 1 && swatch(D, '#1b5e20').disabled === true && swatch(D, '#1b5e20').title.indexOf('3:1') !== -1 && D.getElementById('clrHex').value === '#34d3c0', 'offL=' + offL + ' offD=' + offD);
    t('preview_background_follows_scheme', pixel(D, 2, 2) === '17,21,15', pixel(D, 2, 2));
    click(D.querySelector('#segScheme button[data-scheme="light"]'), W);
    t('preview_background_light', pixel(D, 2, 2) === '255,255,255', pixel(D, 2, 2));
    t('disabled_button_has_no_effect', (click(swatch(D, '#34d3c0'), W), stored(W) === null), JSON.stringify(stored(W)));
  } catch (e) { t('card_group', false, e.message); }
  try {
    const { W, D } = await open('/system#maint');
    click(D.querySelector('#clrSeries .clr-ser[data-id="rate"]'), W);
    click(swatch(D, '#2563c9'), W);
    const s = stored(W);
    t('pick_saved_and_applied', s && s.colors.light.rate === '#2563c9' && cssv(W, '--s1') === '#2563c9' && !!D.getElementById('afColors'), JSON.stringify(s) + ' ' + cssv(W, '--s1'));
    t('pick_leaves_other_series', cssv(W, '--s2') === '#c4501b', cssv(W, '--s2'));
    W.document.documentElement.setAttribute('data-theme', 'dark');
    t('pick_leaves_dark_theme', cssv(W, '--s1') === '#34d3c0' && !(s.colors.dark && s.colors.dark.rate), cssv(W, '--s1'));
    W.document.documentElement.setAttribute('data-theme', 'light');
    t('pick_message', msg(D).textContent === 'Цвет применён' && D.querySelector('#clrPal .clr-sw[data-hex="#2563c9"]').getAttribute('aria-pressed') === 'true', msg(D).textContent);
    const ser = D.querySelector('#clrSeries .clr-ser[data-id="rate"] path');
    t('series_swatch_updates', !!ser && ser.getAttribute('stroke') === '#2563c9', ser && ser.getAttribute('stroke'));
  } catch (e) { t('pick_group', false, e.message); }
  try {
    const { W, D } = await open('/system#maint');
    const hex = D.getElementById('clrHex');
    typeIn(hex, 'red', W);
    t('hex_bad_format', msg(D).className.indexOf('bad') !== -1 && msg(D).textContent.indexOf('#RRGGBB') !== -1 && stored(W) === null, msg(D).textContent);
    typeIn(hex, '#ffff00', W);
    t('hex_low_contrast', msg(D).className.indexOf('bad') !== -1 && msg(D).textContent.indexOf('ниже 3:1') !== -1 && stored(W) === null, msg(D).textContent);
    typeIn(hex, '#12', W);
    t('hex_partial_not_saved', stored(W) === null, '');
    typeIn(hex, '#AA3366', W);
    const s = stored(W);
    t('hex_valid_saved_lowercase', s && s.colors.light.rate === '#aa3366' && cssv(W, '--s1') === '#aa3366' && msg(D).className.indexOf('bad') === -1, JSON.stringify(s));
  } catch (e) { t('hex_group', false, e.message); }
  try {
    const { W, D } = await open('/system#maint');
    click(D.querySelector('#clrSeries .clr-ser[data-id="dose"]'), W);
    selectIn(D.getElementById('clrDash'), 'dashdot', W);
    selectIn(D.getElementById('clrWidth'), '4', W);
    const s = stored(W);
    const lw = W.eval("SERIES.find(function (d) { return d.id === 'dose'; }).lw");
    const dash = W.eval("JSON.stringify(SERIES.find(function (d) { return d.id === 'dose'; }).dash)");
    t('style_saved_and_applied', s && s.styles.dose && s.styles.dose.dash === 'dashdot' && s.styles.dose.width === 4 && lw === 4 && dash === '[10,3,2,3]', JSON.stringify(s) + ' lw=' + lw + ' dash=' + dash);
    click(D.getElementById('clrReset'), W);
    const s2 = stored(W);
    t('reset_series_clears_style', s2 && !s2.styles.dose && W.eval("SERIES.find(function (d) { return d.id === 'dose'; }).lw") === 2 && D.getElementById('clrWidth').value === '', JSON.stringify(s2));
  } catch (e) { t('style_group', false, e.message); }
  try {
    const { W, D } = await open('/system#maint');
    click(swatch(D, '#8250df'), W);
    click(D.querySelector('#clrSeries .clr-ser[data-id="cps"]'), W);
    click(swatch(D, '#c2255c'), W);
    click(D.getElementById('clrReset'), W);
    let s = stored(W);
    t('reset_one_keeps_other', s && s.colors.light.rate === '#8250df' && !s.colors.light.cps && cssv(W, '--s2') === '#c4501b' && cssv(W, '--s1') === '#8250df', JSON.stringify(s));
    click(D.getElementById('clrResetAll'), W);
    s = stored(W);
    t('reset_all_clears', s && Object.keys(s.colors.light).length === 0 && Object.keys(s.colors.dark).length === 0 && cssv(W, '--s1') === '#0b7f72', JSON.stringify(s));
  } catch (e) { t('reset_group', false, e.message); }
  try {
    const { W, D } = await open('/system#maint');
    click(D.getElementById('clrCb'), W);
    const s = stored(W);
    const okLight = cssv(W, '--s1') === '#0072b2' && cssv(W, '--s2') === '#d55e00' && cssv(W, '--s7') === '#000000';
    W.document.documentElement.setAttribute('data-theme', 'dark');
    const okDark = cssv(W, '--s7') === '#ffffff' && cssv(W, '--s1') === '#0072b2';
    W.document.documentElement.setAttribute('data-theme', 'light');
    const dashes = W.eval("JSON.stringify(SERIES.map(function (d) { return d.dash; }))");
    t('colorblind_both_themes', s && s.cb === true && Object.keys(s.colors.light).length === 8 && Object.keys(s.colors.dark).length === 8 && okLight && okDark, JSON.stringify(s && s.cb) + okLight + okDark);
    t('colorblind_distinct_dashes', new Set(JSON.parse(dashes).map(d => JSON.stringify(d))).size === 8 && D.getElementById('clrCb').getAttribute('aria-pressed') === 'true', dashes);
  } catch (e) { t('cb_group', false, e.message); }
  try {
    const { W } = await open('/system#maint', '{broken');
    t('broken_storage_defaults', cssv(W, '--s1') === '#0b7f72' && cssv(W, '--s7') === '#5c6b73', cssv(W, '--s1'));
    const o = await open('/system#maint', JSON.stringify({ v: 1, colors: { light: { rate: '#123456', cps: 'nope', air_p: '#12' }, dark: { rate: 5 } }, styles: { rate: { dash: 'zig', width: 99 } } }));
    t('invalid_values_ignored', cssv(o.W, '--s1') === '#123456' && cssv(o.W, '--s2') === '#c4501b' && cssv(o.W, '--s7') === '#5c6b73' && o.W.eval("SERIES[0].lw") === 2, cssv(o.W, '--s1') + ' ' + cssv(o.W, '--s2'));
    const old = await open('/system#maint', JSON.stringify({ v: 0, colors: { light: { rate: '#123456' } } }));
    t('old_version_defaults', cssv(old.W, '--s1') === '#0b7f72', cssv(old.W, '--s1'));
  } catch (e) { t('storage_group', false, e.message); }
  try {
    const { W, D } = await open('/', JSON.stringify({ v: 1, colors: { light: { rate: '#aa3366' }, dark: {} }, styles: { rate: { dash: 'dots', width: 3 } } }));
    const p = D.querySelector('#legend svg.sw path, .legend svg.sw path');
    const sum = D.querySelector('#summary .sum-row svg path');
    t('applies_before_render_on_chart_page', cssv(W, '--s1') === '#aa3366' && W.cssVar('--s1') === '#aa3366', cssv(W, '--s1'));
    t('legend_and_summary_use_color', !!p && !!sum && p.getAttribute('stroke') === '#aa3366' && sum.getAttribute('stroke') === '#aa3366', (p && p.getAttribute('stroke')) + ' ' + (sum && sum.getAttribute('stroke')));
    t('legend_dash_from_style', !!p && p.getAttribute('stroke-dasharray') === '2 3', p && p.getAttribute('stroke-dasharray'));
    t('no_color_ui_on_chart_page', !D.getElementById('clrCard'), '');
  } catch (e) { t('chart_group', false, e.message); }
  try {
    const { W, D } = await open('/system#maint');
    const sw = Array.from(D.querySelectorAll('#clrPal .clr-sw, #clrSeries .clr-ser'));
    const small = sw.filter(b => b.offsetWidth < 32 || b.offsetHeight < 32).length;
    const noName = sw.filter(b => !(b.getAttribute('aria-label') || b.textContent.trim())).length;
    t('targets_and_names', small === 0 && noName === 0, 'small=' + small + ' noName=' + noName);
  } catch (e) { t('targets_group', false, e.message); }
  if (cur) { try { cur.f.remove(); } catch (e) {} cur = null; }
  try {
    const { W, D } = await open('/system#maint', undefined, 'dark');
    click(D.querySelector('#segScheme button[data-scheme="dark"]'), W);
    click(swatch(D, '#f0c45a'), W);
    let s = stored(W);
    t('dark_scheme_palette_pick', s && s.colors.dark.rate === '#f0c45a' && !(s.colors.light && s.colors.light.rate) && cssv(W, '--s1') === '#f0c45a', JSON.stringify(s) + ' ' + cssv(W, '--s1'));
    typeIn(D.getElementById('clrHex'), '#ffee00', W);
    s = stored(W);
    t('dark_scheme_hex_pick', s && s.colors.dark.rate === '#ffee00' && msg(D).className.indexOf('bad') === -1, JSON.stringify(s) + ' ' + msg(D).textContent);
    click(D.querySelector('#segScheme button[data-scheme="light"]'), W);
    t('scheme_switch_shows_other_theme_value', D.getElementById('clrHex').value === '#0b7f72' && D.querySelectorAll('#clrPal .clr-sw:disabled').length >= 5, D.getElementById('clrHex').value);
    typeIn(D.getElementById('clrHex'), '#ffee00', W);
    t('light_scheme_rejects_dark_only_color', msg(D).textContent.indexOf('ниже 3:1') !== -1 && stored(W).colors.light.rate === undefined, msg(D).textContent);
  } catch (e) { t('dark_group', false, e.message); }
  const failed = [];
  for (const r of res) { if (!r.ok) failed.push(r.n + ': ' + r.d); }
  return JSON.stringify({ total: res.length, failed, res });
})()
