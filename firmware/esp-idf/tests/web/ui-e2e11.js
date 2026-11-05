(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const wait = P.wait || 2500;
    const sleep = ms => new Promise(r => setTimeout(r, ms));
  const res = [];
  const t = (n, cond, d) => { res.push({ n, ok: !!cond, d: d || '' }); };
  const scen = async s => { await fetch('/__mock/sensors?scenario=' + s, { method: 'POST' }); };
  const slog = async () => (await fetch('/__mock/sensors-log')).json();
  let cur = null;
  const open = async (path, theme, pre) => {
    if (cur) { try { cur.f.remove(); } catch (e) {} cur = null; }
    try { localStorage.clear(); if (pre) localStorage.setItem(pre[0], pre[1]); } catch (e) {}
    const f = document.createElement('iframe');
    f.style.cssText = 'position:fixed;left:0;top:0;border:0;opacity:0;pointer-events:none;width:' + width + 'px;height:1000px;';
    const parts = path.split('#');
    f.src = location.origin + parts[0] + (parts[0].indexOf('?') === -1 ? '?' : '&') + 'theme=' + (theme || 'light') + (parts[1] ? '#' + parts[1] : '');
    document.body.appendChild(f);
    await new Promise(r => { f.onload = r; setTimeout(r, 15000); });
    await sleep(wait);
    cur = { f, W: f.contentWindow, D: f.contentDocument };
    return cur;
  };
  const posts = log => log.filter(x => x.method === 'POST');
  const click = (el, W) => el.dispatchEvent(new W.MouseEvent('click', { bubbles: true, cancelable: true }));
  const setSw = (inp, on, W) => { inp.checked = on; inp.dispatchEvent(new W.Event('change', { bubbles: true })); };
  const typeIn = (el, v, W) => { el.value = v; el.dispatchEvent(new W.Event('input', { bubbles: true })); };
  const visible = el => !!el && el.offsetParent !== null;
  const rowsOf = D => Array.from(D.querySelectorAll('#sensRows tr[data-sensor]'));
  const txt = (D, id) => (D.getElementById(id) ? D.getElementById(id).textContent : '');
  const st = (W, expr) => W.eval(expr);
  const rowOf = (D, n) => D.querySelector('#sensRows tr[data-sensor="' + n + '"]');
  const swOf = tr => tr.querySelector('input[type=checkbox]');
  const rowV = (D, label) => { const k = Array.from(D.querySelectorAll('#readings .kv')).find(r => r.querySelector('.k').textContent.indexOf(label) === 0); return k ? { k: k.querySelector('.k').textContent, v: k.querySelector('.v').textContent, title: k.querySelector('.v').title } : null; };
  const NOTE = 'Погрешность: статистическая (Пуассон, 1σ) для счётных величин; для датчиков среды — по паспорту. Систематическая погрешность прибора (калибровка, энергетическая зависимость, фон) не учтена';
  try {
    await scen('one');
    const { W, D } = await open('/readings');
    const g = l => (rowV(D, l) || {}).v;
    t('counts_with_poisson_error', g('CPS (мгновенный)') === '11.5 ± 2.4' && g('Импульсы за 2 с') === '23 ± 5' && g('CPS (среднее за окно)') === '15.8 ± 1.3', g('CPS (мгновенный)') + '|' + g('Импульсы за 2 с') + '|' + g('CPS (среднее за окно)'));
    t('dose_rate_relative_error_inherited', g('Мощность дозы, мкЗв/ч') === '0.064 ± 0.013' && g('Мощность дозы, мкР/ч') === '6.4 ± 1.3' && g('Мощность дозы (среднее за окно)') === '0.065 ± 0.005', g('Мощность дозы, мкЗв/ч') + '|' + g('Мощность дозы, мкР/ч') + '|' + g('Мощность дозы (среднее за окно)'));
    t('accumulated_dose_error_nd', g('Накопленная доза') === '194.054 ± н/д' && rowV(D, 'Накопленная доза').title === 'полное число импульсов в API недоступно: погрешность н/д', g('Накопленная доза'));
    t('labels_and_units_unchanged', rowV(D, 'CPS (мгновенный)').k === 'CPS (мгновенный), имп/с' && rowV(D, 'Накопленная доза').k === 'Накопленная доза, мкЗв' && rowV(D, 'Мощность дозы (среднее за окно)').k === 'Мощность дозы (среднее за окно), мкЗв/ч', rowV(D, 'CPS (мгновенный)').k);
    t('tooltips_explain_parts', rowV(D, 'CPS (мгновенный)').title.indexOf('статистическая (Пуассон, 1σ): 2.4') === 0 && rowV(D, 'Мощность дозы, мкЗв/ч').title.indexOf('Допущение') > 0 && rowV(D, 'Мощность дозы, мкР/ч').title.indexOf('Допущение') > 0 && rowV(D, 'Мощность дозы (среднее за окно)').title.indexOf('Допущение') > 0 && rowV(D, 'CPS (мгновенный)').title.indexOf('Допущение') < 0 && rowV(D, 'CPS (мгновенный)').title.indexOf('систематическая') < 0, rowV(D, 'CPS (мгновенный)').title);
    t('footnote_text', txt(D, 'errNote') === NOTE, txt(D, 'errNote'));
    t('battery_row_untouched', (rowV(D, 'Заряд аккумулятора') || {}).v === '27', (rowV(D, 'Заряд аккумулятора') || {}).v);
    const te = rowV(D, 'Температура среды');
    t('env_value_has_passport_nd', te.v.indexOf(' ± н/д') > 0 && te.title.indexOf('паспортная погрешность: н/д') === 0 && te.title.indexOf('дискретизация АЦП: н/д') > 0, te.v + '|' + te.title);
  } catch (e) { t('values_group', false, e.message); }
  try {
    const { W, D } = await open('/system#maint');
    const inp = D.getElementById('sysPct');
    const setv = v => { inp.value = v; inp.dispatchEvent(new W.Event('change', { bubbles: true })); };
    const ls = () => W.localStorage.getItem('afh.sys.v1');
    t('sys_card_on_system_only', !!inp && inp.value === '' && inp.getAttribute('placeholder') === 'не задана', '');
    setv('10');
    t('sys_saved_to_storage', ls() === '{"v":1,"pct":10}' && txt(D, 'sysPctMsg') === 'сохранено: 10 %' && D.getElementById('sysPctMsg').className.indexOf('ok') >= 0, ls() + '|' + txt(D, 'sysPctMsg'));
    setv('60');
    t('sys_out_of_range_rejected', ls() === '{"v":1,"pct":10}' && txt(D, 'sysPctMsg').indexOf('от 0 до 50') >= 0 && D.getElementById('sysPctMsg').className.indexOf('bad') >= 0, ls() + '|' + txt(D, 'sysPctMsg'));
    setv('abc');
    t('sys_garbage_rejected', ls() === '{"v":1,"pct":10}' && D.getElementById('sysPctMsg').className.indexOf('bad') >= 0, ls());
    setv('2,5');
    t('sys_comma_decimal', ls() === '{"v":1,"pct":2.5}' && txt(D, 'sysPctMsg') === 'сохранено: 2.5 %', ls());
    setv('');
    t('sys_cleared', ls() === null && txt(D, 'sysPctMsg') === 'систематическая часть не учитывается', ls() + '|' + txt(D, 'sysPctMsg'));
    const r = await open('/system#maint', 'light', ['afh.sys.v1', '{"v":1,"pct":20}']);
    t('sys_input_restored_from_storage', r.D.getElementById('sysPct').value === '20', r.D.getElementById('sysPct').value);
    const b = await open('/system#maint', 'light', ['afh.sys.v1', '{"v":1,"pct":99}']);
    t('sys_bad_storage_ignored', b.D.getElementById('sysPct').value === '', b.D.getElementById('sysPct').value);
  } catch (e) { t('sys_group', false, e.message); }
  try {
    await scen('one');
    const cps = D => rowV(D, 'CPS (мгновенный)');
    const a = await open('/readings', 'light', ['afh.sys.v1', '{"v":1,"pct":10}']);
    t('sys_adds_in_quadrature', cps(a.D).v === '11.5 ± 2.7' && cps(a.D).title.indexOf('систематическая (10 %): 1.2; итого: 2.7') >= 0 && txt(a.D, 'errNote').indexOf('задана вручную (10 %)') > 0, cps(a.D).v + '|' + cps(a.D).title);
    t('battery_row_untouched_with_sys', (rowV(a.D, 'Заряд аккумулятора') || {}).v === '27', '');
    const c = await open('/readings', 'light', ['afh.sys.v1', '{"v":1,"pct":2.5}']);
    t('sys_small_does_not_change_digits', cps(c.D).v === '11.5 ± 2.4' && cps(c.D).title.indexOf('систематическая (2.5 %): 0.29') >= 0, cps(c.D).v + cps(c.D).title);
    const d = await open('/readings', 'light', ['afh.sys.v1', '{"v":1,"pct":20}']);
    t('sys_20_percent', cps(d.D).v === '12 ± 3', cps(d.D).v);
    const e = await open('/readings', 'light', ['afh.sys.v1', '{"v":1,"pct":99}']);
    t('sys_bad_storage_ignored_on_readings', cps(e.D).v === '11.5 ± 2.4' && txt(e.D, 'errNote') === NOTE, cps(e.D).v);
    t('readings_page_has_no_inputs', e.D.getElementById('sysPct') === null, '');
  } catch (e) { t('sys_readings_group', false, e.message); }
  try {
    await scen('three');
    const { W, D } = await open('/readings');
    const cells = Array.from(D.querySelectorAll('#envTable td[data-kind]')).filter(c => c.textContent !== '—');
    t('env_table_cells_pm_nd', cells.length === 6 && cells.every(c => / ± н\/д$/.test(c.textContent) && c.title.indexOf('паспортная погрешность: н/д') === 0), cells.map(c => c.textContent).join('|'));
    const aht = D.querySelector('#envTable tr[data-sensor="3"] td[data-kind="t"]');
    t('env_cell_operator_correction_separate', aht.title.indexOf('поправка оператора (систематическая часть, отдельно): −0.5 °C') > 0 && aht.title.indexOf('±0.000095') > 0, aht.title);
    const sht = D.querySelector('#envTable tr[data-sensor="2"] td[data-kind="t"]');
    t('env_cell_adc_lower_bound', sht.title.indexOf('дискретизация АЦП (нижняя граница): ±0.0013 °C') > 0, sht.title);
  } catch (e) { t('env_group', false, e.message); }
  try {
    await scen('one_off');
    const { W, D } = await open('/readings');
    const te = rowV(D, 'Температура среды');
    const tp = rowV(D, 'Давление среды');
    t('single_sensor_tooltip_has_operator_correction', te.title.indexOf('поправка оператора (систематическая часть, отдельно): −0.5 °C') > 0 && tp.title.indexOf('+0.5 мм рт. ст.') > 0 && te.v.indexOf(' ± н/д') > 0, te.title + '|' + tp.title);
    await scen('one');
  } catch (e) { t('operator_correction_group', false, e.message); }
  try {
    await scen('one');
    const { W, D } = await open('/');
    const rows = Array.from(D.querySelectorAll('#summary .sum-row'));
    const cpsRow = rows.find(r => r.querySelector('.nm').textContent === 'CPS');
    t('chart_summary_cps_mean_last_with_error', !!cpsRow && (cpsRow.querySelector('.vals').textContent.match(/ ± /g) || []).length === 2, cpsRow && cpsRow.querySelector('.vals').textContent);
    t('chart_summary_note_assumptions', D.getElementById('summary').textContent.indexOf('σ/√n независимых отсчётов') > 0 && D.getElementById('summary').textContent.indexOf('Систематическая часть: не учтена') > 0, '');
    const doseRow = rows.find(r => r.querySelector('.nm').textContent === 'Мощность дозы');
    t('chart_summary_rate_row_has_error', !!doseRow && (doseRow.querySelector('.vals').textContent.match(/ ± /g) || []).length === 2, doseRow && doseRow.querySelector('.vals').textContent);
    const m0 = cpsRow.querySelector('.vals').textContent;
    const sys = await open('/', 'light', ['afh.sys.v1', '{"v":1,"pct":10}']);
    const m1 = Array.from(sys.D.querySelectorAll('#summary .sum-row')).find(r => r.querySelector('.nm').textContent === 'CPS').querySelector('.vals').textContent;
    t('chart_summary_error_grows_with_sys', m0 !== m1 && m1.match(/ ± ([0-9.]+)/)[1] !== m0.match(/ ± ([0-9.]+)/)[1], m0 + ' || ' + m1);
    t('chart_summary_uses_sys', sys.D.getElementById('summary').textContent.indexOf('Систематическая часть: 10 %') > 0, '');
  } catch (e) { t('chart_group', false, e.message); }
  const failed = [];
  for (const r of res) { if (!r.ok) failed.push(r.n + ': ' + r.d); }
  return JSON.stringify({ total: res.length, failed, res });
})()
