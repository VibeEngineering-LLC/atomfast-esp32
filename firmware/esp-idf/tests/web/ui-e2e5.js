(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const wait = P.wait || 2500;
  const sleep = (ms) => new Promise(r => setTimeout(r, ms));
  const res = [];
  const t = (n, c, d) => {
    try {
      if (c) res.push({ n, ok: true });
      else res.push({ n, ok: false, d: d || 'fail' });
    } catch (e) {
      res.push({ n, ok: false, d: (e && e.message) || 'err' });
    }
  };
  let cur = null;
  const open = async (url, keep) => {
    if (cur) {
      try { cur.remove(); } catch (e) {}
    }
    if (!keep) { try { localStorage.clear(); } catch (e) {} }
    const f = document.createElement('iframe');
    f.style.cssText = 'position:fixed;left:0;top:0;border:0;opacity:0;pointer-events:none;width:' + width + 'px;height:900px;';
    f.src = location.origin + url;
    document.body.appendChild(f);
    cur = f;
    await new Promise(r => { f.onload = r; });
    await sleep(wait);
    return { f, W: f.contentWindow, D: f.contentDocument };
  };
  try {
    try {
      const { D } = await open('/readings');
      const r = D.getElementById('readings');
      const kvs = r ? r.querySelectorAll('.kv') : [];
      const noCfg = !D.getElementById('cfgEnabled') && !D.getElementById('mqHost') && !D.getElementById('btnTime') && !D.getElementById('btnCfgSave');
      const nav = D.querySelector('nav.tabs a[data-nav="settings"]');
      const navOk = nav && nav.getAttribute('aria-current') === 'page' && nav.textContent.trim() === 'Показания';
      const tabs = Array.from(D.querySelectorAll('nav.tabs a')).map(a => a.textContent.trim());
      const tabsOk = tabs[0] === 'Главная' && tabs[1] === 'Показания' && tabs[2] === 'Измерения' && tabs[3] === 'Система';
      const chart = D.getElementById('chart');
      const chartHidden = !chart || chart.offsetParent === null;
      t('readings_page', r && kvs.length >= 3 && noCfg && navOk && tabsOk && chartHidden, 'readings page structure mismatch');
    } catch (e) { t('readings_page', false, e.message); }
    try {
      const { D } = await open('/readings');
      const row = Array.from(D.querySelectorAll('#readings .kv')).find(k => k.textContent.indexOf('аккумулятора') >= 0);
      t('readings_battery_row', !!row && row.querySelector('.k').textContent.trim() === 'Заряд аккумулятора прибора, %' && row.querySelector('.v').textContent.trim() === '27', row ? row.textContent : 'no row');
    } catch (e) { t('readings_battery_row', false, e.message); }
    try {
      const { D } = await open('/settings');
      const r = D.getElementById('readings');
      const kvs = r ? r.querySelectorAll('.kv') : [];
      const noCfg = !D.getElementById('cfgEnabled');
      const nav = D.querySelector('nav.tabs a[data-nav="settings"]');
      const navOk = nav && nav.getAttribute('aria-current') === 'page' && nav.textContent.trim() === 'Показания';
      t('settings_alias', r && kvs.length >= 1 && noCfg && navOk, 'settings alias mismatch');
    } catch (e) { t('settings_alias', false, e.message); }
    try {
      const { D } = await open('/system#sensors');
      const sec = D.getElementById('sec-sensors');
      const notHidden = sec && !sec.hidden;
      const ids = ['cfgEnabled', 'cfgTOff', 'cfgPOff', 'cfgAgg', 'cfgPeriod', 'btnCfgSave'];
      const allExist = ids.every(id => sec && sec.querySelector('#' + id));
      const period = D.getElementById('cfgPeriod');
      const periodFilled = period && period.value.trim().length > 0;
      t('sensor_form_on_system', notHidden && allExist && periodFilled, 'sensor form on system mismatch');
    } catch (e) { t('sensor_form_on_system', false, e.message); }
    try {
      const { D } = await open('/system#mqtt');
      const sec = D.getElementById('sec-mqtt');
      const notHidden = sec && !sec.hidden;
      const ids = ['mqHost', 'mqPort', 'mqUser', 'mqPass', 'mqPassClear', 'mqPeriod', 'mqPrefix', 'btnMqSave', 'm_en'];
      const allExist = ids.every(id => sec && sec.querySelector('#' + id));
      const state = D.getElementById('mqState');
      const stateText = state && state.textContent.trim().length > 0;
      t('mqtt_form_on_system', notHidden && allExist && stateText, 'mqtt form on system mismatch');
    } catch (e) { t('mqtt_form_on_system', false, e.message); }
    try {
      const { D } = await open('/system#maint');
      const sec = D.getElementById('sec-maint');
      const notHidden = sec && !sec.hidden;
      const ids = ['btnTime', 'timeInd', 'btnReboot', 'btnReconnect'];
      const allExist = ids.every(id => sec && sec.querySelector('#' + id));
      const timeInd = D.getElementById('timeInd');
      const timeText = timeInd && timeInd.textContent.trim().length > 0;
      t('time_card_in_maint', notHidden && allExist && timeText, 'time card in maint mismatch');
    } catch (e) { t('time_card_in_maint', false, e.message); }
    try {
      try { localStorage.setItem('af.sys.section', 'mqtt'); } catch (e) {}
      const { D, W } = await open('/system', true);
      const secMqtt = D.getElementById('sec-mqtt');
      const secSensors = D.getElementById('sec-sensors');
      const mqttVisible = secMqtt && !secMqtt.hidden;
      const sensorsHidden = secSensors && secSensors.hidden;
      const hashOk = W.location.hash === '#mqtt';
      t('stored_section_works', mqttVisible && sensorsHidden && hashOk, 'stored section works mismatch');
    } catch (e) { t('stored_section_works', false, e.message); }
    try {
      const { D, W } = await open('/system#sensors');
      const calls = [];
      const origFetch = W.fetch;
      W.fetch = function (url, opts) {
        calls.push({ url: String(url), method: (opts && opts.method) || 'GET' });
        return origFetch.apply(this, arguments);
      };
      const period = D.getElementById('cfgPeriod');
      if (period) {
        period.value = '30';
        period.dispatchEvent(new W.Event('input', { bubbles: true }));
      }
      const btn = D.getElementById('btnCfgSave');
      if (btn) btn.click();
      await sleep(600);
      const hasSensors = calls.some(c => c.url.endsWith('/api/sensors') && c.method === 'POST');
      const hasCsrf = calls.some(c => c.url.endsWith('/api/csrf-token') && c.method === 'GET');
      t('save_goes_to_same_api', hasSensors && hasCsrf, 'save goes to same api mismatch');
    } catch (e) { t('save_goes_to_same_api', false, e.message); }
    try {
      const { D } = await open('/system#summary');
      const links = Array.from(D.querySelectorAll('a[href]'));
      const noSettings = !links.some(a => a.getAttribute('href') === '/settings');
      const hasReadings = links.some(a => a.getAttribute('href') === '/readings');
      t('no_settings_links', noSettings && hasReadings, 'no settings links mismatch');
    } catch (e) { t('no_settings_links', false, e.message); }
    try {
      const { D } = await open('/');
      const noCfg = !D.getElementById('cfgEnabled');
      const noBtnTime = !D.getElementById('btnTime');
      const noSidelist = !D.getElementById('sidelist');
      const hasChart = !!D.getElementById('chart');
      t('home_has_no_system_dom', noCfg && noBtnTime && noSidelist && hasChart, 'home has no system dom mismatch');
    } catch (e) { t('home_has_no_system_dom', false, e.message); }
  } finally {
    if (cur) {
      try { cur.remove(); } catch (e) {}
    }
  }
  const failed = res.filter(r => !r.ok).map(r => r.n + ': ' + (r.d || 'fail'));
  return JSON.stringify({ total: res.length, failed, res });
})()
