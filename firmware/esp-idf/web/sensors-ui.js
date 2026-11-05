var snParsed = null;
var snSel = 0;
var snBusy = false;
var snCfgDirty = false;

function snEl(id) {
  return document.getElementById(id);
}

function snSay(id, text, cls) {
  const el = snEl(id);
  if (!el) return;
  el.className = 'result' + (cls ? ' ' + cls : '');
  el.textContent = text;
}

function snCell(tr, text, cls) {
  const td = document.createElement('td');
  td.textContent = text;
  if (cls) td.className = cls;
  tr.appendChild(td);
  return td;
}

function snSwitch(label, checked, handler) {
  const lab = document.createElement('label');
  lab.className = 'switch';
  const inp = document.createElement('input');
  inp.type = 'checkbox';
  inp.checked = checked;
  inp.setAttribute('aria-label', label);
  inp.addEventListener('change', () => handler(inp.checked, inp));
  const knob = document.createElement('span');
  knob.className = 'knob';
  knob.setAttribute('aria-hidden', 'true');
  lab.append(inp, knob);
  return { lab: lab, inp: inp };
}

function snStatusBadge(s) {
  const st = AFSL.statusInfo(s);
  const b = document.createElement('span');
  b.className = 'fw-badge ' + (st.cls === 'ok' ? 'ok' : (st.cls === 'mut' ? '' : 'warn'));
  b.textContent = st.text;
  b.dataset.state = st.key;
  return b;
}

function snValuesText(s) {
  return AFSL.valueParts(s).map(p => p.label + ' ' + p.text + ' ' + p.unit).join(' · ');
}

var snLastSig = '';

function snD_upd(el, prop, val) {
  if (el[prop] !== val) el[prop] = val;
}

function snGroupRow(body, g) {
  const tr = document.createElement('tr');
  tr.className = 'sens-grp';
  tr.dataset.model = g.model;
  const td = document.createElement('td');
  td.colSpan = 9;
  const t = document.createElement('strong');
  t.textContent = g.name + (g.kinds ? ' · ' + g.kinds : '') + ' · ' + g.count + ' шт.';
  td.appendChild(t);
  const sw = snSwitch('Все датчики ' + g.name, g.allEnabled, on => snModelToggle(g.model, on));
  sw.inp.dataset.model = g.model;
  sw.inp.indeterminate = g.anyEnabled && !g.allEnabled;
  const cap = document.createElement('span');
  cap.className = 'muted';
  cap.textContent = ' все датчики модели';
  td.append(' ', sw.lab, cap);
  tr.appendChild(td);
  body.appendChild(tr);
}

function snSensorRow(body, s) {
  const tr = document.createElement('tr');
  tr.dataset.sensor = String(s.sensor);
  tr.dataset.sn = String(s.sensor);
  if (s.sensor === snSel) tr.className = 'sel';
  snCell(tr, String(s.sensor), 'num');
  snCell(tr, s.name + (s.kinds ? ' (' + s.kinds + ')' : ''));
  snCell(tr, s.addr || '—', 'num');
  const st = document.createElement('td');
  st.dataset.snF = 'state';
  st.appendChild(snStatusBadge(s));
  tr.appendChild(st);
  const vtd = snCell(tr, snValuesText(s) || '—', 'num');
  vtd.dataset.snF = 'values';
  const atd = snCell(tr, AFSL.ageText(s));
  atd.dataset.snF = 'age';
  snCell(tr, String(s.n_err), 'num');
  const sw = document.createElement('td');
  const x = snSwitch('Датчик №' + s.sensor + ' включён', s.enabled, on => snToggle(s.sensor, on));
  x.inp.dataset.sensor = String(s.sensor);
  sw.appendChild(x.lab);
  tr.appendChild(sw);
  const act = document.createElement('td');
  const pick = document.createElement('button');
  pick.type = 'button';
  pick.className = 'btn';
  pick.textContent = 'Настроить';
  pick.setAttribute('aria-label', 'Настроить датчик №' + s.sensor);
  pick.addEventListener('click', () => { snSel = s.sensor; snCfgDirty = false; snRender(snParsed, true); });
  act.appendChild(pick);
  if (AFSL.removable(s)) {
    const del = document.createElement('button');
    del.type = 'button';
    del.className = 'btn danger';
    del.textContent = 'Удалить';
    del.setAttribute('aria-label', 'Удалить датчик №' + s.sensor);
    del.addEventListener('click', () => snRemove(s));
    act.appendChild(del);
  }
  tr.appendChild(act);
  body.appendChild(tr);
}

function snRender(parsed, force) {
  snParsed = parsed;
  const list = parsed.list;
  if (!snSelected()) snSel = list.length === 1 ? list[0].sensor : 0;
  if (!snEl('sensCard')) return;
  const groups = AFSL.groups(list);
  const sigParts = [];
  for (const g of groups) {
    for (const s of g.sensors) sigParts.push([s.sensor, g.model, s.addr, s.enabled, g.model]);
  }
  const sig = JSON.stringify(sigParts) + '|' + snSel;
  const sum = AFSL.summary(list);
  snD_upd(snEl('sensSummary'), 'textContent', sum.text);
  snD_upd(snEl('sensSummary'), 'className', 'sens-sum' + (sum.hasErr ? ' warn' : ''));
  snD_upd(snEl('sensEmpty'), 'hidden', list.length > 0);
  snD_upd(snEl('sensTable'), 'hidden', list.length === 0);
  snD_upd(snEl('btnAdd'), 'disabled', !AFSL.canAdd(list, parsed.maxSensors));
  const body = snEl('sensRows');
  if (sig === snLastSig && !force) {
    for (const g of groups) {
      for (const s of g.sensors) {
        const tr = body.querySelector('[data-sn="' + s.sensor + '"]');
        if (!tr) continue;
        const vtd = tr.querySelector('[data-sn-f="values"]');
        if (vtd) snD_upd(vtd, 'textContent', snValuesText(s) || '—');
        const std = tr.querySelector('[data-sn-f="state"]');
        if (std) {
          const badge = std.firstChild;
          if (badge && badge.tagName === 'SPAN') {
            const st = AFSL.statusInfo(s);
            snD_upd(badge, 'className', 'fw-badge ' + (st.cls === 'ok' ? 'ok' : (st.cls === 'mut' ? '' : 'warn')));
            snD_upd(badge, 'textContent', st.text);
          }
        }
        const atd = tr.querySelector('[data-sn-f="age"]');
        if (atd) snD_upd(atd, 'textContent', AFSL.ageText(s));
      }
    }
  } else {
    if (body.childNodes.length) body.textContent = '';
    for (const g of groups) {
      snGroupRow(body, g);
      for (const s of g.sensors) snSensorRow(body, s);
    }
    snLastSig = sig;
  }
  snFillCfg(!!force);
}

function snRenderEnvTable(container, parsed) {
  const act = parsed.list.filter(s => s.enabled);
  if (!act.length) {
    const row = document.createElement('div');
    row.className = 'kv';
    row.innerHTML = '<span class="k">Датчики среды</span><span class="v num">нет включённых</span>';
    container.appendChild(row);
    return;
  }
  const wrap = document.createElement('div');
  wrap.className = 'fw-wrap';
  const tbl = document.createElement('table');
  tbl.className = 'fw-table env-table';
  tbl.id = 'envTable';
  tbl.innerHTML = '<thead><tr><th>Датчик</th><th>T, °C</th><th>P, мм рт. ст.</th><th>RH, %</th><th>Измерено</th></tr></thead>';
  const tb = document.createElement('tbody');
  for (const s of act) {
    const tr = document.createElement('tr');
    tr.dataset.sensor = String(s.sensor);
    snCell(tr, AFSL.label(s));
    for (const k of ['t', 'p', 'h']) {
      const part = AFSL.valueParts(s).find(p => p.key === k);
      snCell(tr, part ? part.text : '—', 'num').dataset.kind = k;
    }
    snCell(tr, AFSL.statusInfo(s).key === 'ok' ? AFSL.ageText(s) : AFSL.statusInfo(s).text);
    tb.appendChild(tr);
  }
  tbl.appendChild(tb);
  wrap.appendChild(tbl);
  container.appendChild(wrap);
}

async function snPost(url, body, doneMsg, msgId) {
  if (snBusy) return null;
  snBusy = true;
  try {
    const r = await postJSON(url, body);
    if (r.ok) {
      snSay(msgId, (r.data && r.data.persisted === false) ? 'применено, но не записано в память платы (действует до перезагрузки)' : doneMsg, 'ok');
    } else {
      snSay(msgId, r.status === 403 ? 'отказ: недействительный токен, обновите страницу' : (r.status === 409 ? 'адрес занят или список полон' : (r.status === 503 ? 'плата занята, повторите позже' : (r.status === 404 ? 'такого датчика нет' : 'плата отклонила запрос (' + r.status + ')'))), 'bad');
    }
    return r;
  } catch (e) {
    snSay(msgId, 'нет связи с платой', 'bad');
    return null;
  } finally {
    snBusy = false;
    loadSensors();
  }
}

async function snToggle(n, on) {
  const body = AFSL.toggleBody(n, on);
  if (body) await snPost('/api/sensors', body, on ? 'датчик №' + n + ' включён' : 'датчик №' + n + ' выключен', 'sensScanMsg');
}

async function snModelToggle(model, on) {
  const body = AFSL.modelBody(model, on);
  if (body) await snPost('/api/sensors/model', body, on ? 'модель включена' : 'модель выключена', 'sensScanMsg');
}

async function snScan() {
  snSay('sensScanMsg', 'Сканирую шину I2C…', '');
  const r = await snPost('/api/sensors', { action: 'scan' }, '', 'sensScanMsg');
  if (r && r.ok) snSay('sensScanMsg', AFSL.scanText(r.data), 'ok');
}

async function snAdd() {
  const model = snEl('addModel').value;
  const addr = snEl('addAddr').value;
  const body = AFSL.addBody(model, addr);
  if (!body) {
    snSay('sensAddMsg', 'Для этой модели такой адрес недопустим', 'bad');
    return;
  }
  if (AFSL.addrTaken(snParsed ? snParsed.list : [], addr)) {
    snSay('sensAddMsg', 'Адрес ' + addr + ' уже занят другим датчиком', 'bad');
    return;
  }
  await snPost('/api/sensors', body, 'датчик добавлен (выключен), включите его в таблице', 'sensAddMsg');
}

async function snRemove(s) {
  if (!confirm('Удалить датчик №' + s.sensor + ' (' + AFSL.label(s) + ')? Он ни разу не отвечал.')) return;
  const body = AFSL.removeBody(s.sensor);
  if (body) await snPost('/api/sensors', body, 'датчик удалён', 'sensScanMsg');
}

async function snSave() {
  const s = snSelected();
  if (!s) return;
  const v = AFSL.sensorPatch(s, {
    enabled: snEl('cfgEnabled').checked, t_off: snEl('cfgTOff').value, p_off: snEl('cfgPOff').value,
    h_off: snEl('cfgHOff').value, period_s: snEl('cfgPeriod').value, agg_s: snEl('cfgAgg').value
  });
  if (!v.ok) {
    snSay('cfgResult', v.error, 'bad');
    const ids = { t_off: 'cfgTOff', p_off: 'cfgPOff', h_off: 'cfgHOff', period_s: 'cfgPeriod', agg_s: 'cfgAgg' };
    if (ids[v.field]) snEl(ids[v.field]).focus();
    return;
  }
  snSay('cfgResult', 'сохраняю…', '');
  const r = await snPost('/api/sensors', v.body, 'сохранено', 'cfgResult');
  if (r && r.ok) snCfgDirty = false;
}

function snAddrOptions() {
  const sel = snEl('addAddr');
  sel.textContent = '';
  for (const a of AFSL.addrChoices(snEl('addModel').value)) {
    const o = document.createElement('option');
    o.value = a;
    o.textContent = a;
    sel.appendChild(o);
  }
}

function initSensorsList() {
  if (!snEl('sensCard')) return;
  for (const k of AFSL.ORDER) {
    const o = document.createElement('option');
    o.value = k;
    o.textContent = AFSL.MODELS[k].name;
    snEl('addModel').appendChild(o);
  }
  snAddrOptions();
  snEl('addModel').addEventListener('change', snAddrOptions);
  snEl('btnScan').addEventListener('click', snScan);
  snEl('btnAdd').addEventListener('click', snAdd);
  snEl('btnCfgSave').addEventListener('click', snSave);
  for (const id of ['cfgEnabled', 'cfgTOff', 'cfgPOff', 'cfgHOff', 'cfgAgg', 'cfgPeriod']) {
    snEl(id).addEventListener('input', () => { snCfgDirty = true; });
    snEl(id).addEventListener('change', () => { snCfgDirty = true; });
  }
}
