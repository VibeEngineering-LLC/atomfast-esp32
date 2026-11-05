var sysCur = 'status';
var sysBusy = false;
var sysDirty = false; // section changed while a refresh was running
var sysData = { st: null, sy: null, sn: null, hs: null, bl: undefined, bs: [] };
var SYS_LS_KEY = 'af.sys.section';

function setChip(id, chip) {
  var el = document.getElementById(id);
  if (!el || !chip) return;
  el.className = 'chip ' + chip.level;
  var dot = el.querySelector('.dot');
  if (dot) dot.className = chip.level === 'mut' ? 'dot' : 'dot ' + chip.level;
  var t = el.querySelector('.t');
  if (t) t.textContent = chip.text;
}

function renderSensorCard(card) {
  var cardEl = document.createElement('div');
  cardEl.className = 'scard';

  var head = document.createElement('div');
  head.className = 'shead';

  var snum = document.createElement('span');
  snum.className = 'snum';
  snum.textContent = '#' + card.number;
  head.appendChild(snum);

  var stitle = document.createElement('span');
  stitle.className = 'stitle';
  stitle.textContent = card.name ? card.name : card.title;
  head.appendChild(stitle);

  var badge = document.createElement('span');
  badge.className = 'badge ' + card.level;
  badge.textContent = card.statusText;
  head.appendChild(badge);

  cardEl.appendChild(head);

  if (card.values && card.values.length) {
    for (var i = 0; i < card.values.length; i++) {
      var v = card.values[i];
      var sval = document.createElement('div');
      sval.className = 'sval';

      var sk = document.createElement('span');
      sk.className = 'sk';
      sk.textContent = v.label;
      sval.appendChild(sk);

      var sv = document.createElement('span');
      sv.className = 'sv';
      sv.textContent = v.value;
      sval.appendChild(sv);

      var su = document.createElement('span');
      su.className = 'su';
      su.textContent = v.unit;
      sval.appendChild(su);

      cardEl.appendChild(sval);
    }
  } else {
    var sempty = document.createElement('div');
    sempty.className = 'sempty';
    sempty.textContent = card.statusText;
    cardEl.appendChild(sempty);
  }

  if (card.ageText) {
    var sage = document.createElement('div');
    sage.className = 'sage';
    sage.textContent = card.ageText;
    cardEl.appendChild(sage);
  }

  return cardEl;
}

function renderSensors(cards) {
  var groups = document.getElementById('sensorGroups');
  var note = document.getElementById('sensorNote');
  if (groups) groups.textContent = '';
  if (!cards || !cards.length) {
    if (note) note.textContent = 'Датчики среды не обнаружены';
    return;
  }
  if (note) note.textContent = '';
  var groupsData = AFHM.groupByModel(cards);
  for (var i = 0; i < groupsData.length; i++) {
    var g = groupsData[i];
    var sgroup = document.createElement('div');
    sgroup.className = 'sgroup';

    var h3 = document.createElement('h3');
    h3.textContent = g.model + ' · ' + g.cards.length + ' шт.';
    sgroup.appendChild(h3);

    var scards = document.createElement('div');
    scards.className = 'scards';
    for (var j = 0; j < g.cards.length; j++) {
      scards.appendChild(renderSensorCard(g.cards[j]));
    }
    sgroup.appendChild(scards);

    if (groups) groups.appendChild(sgroup);
  }
}

function renderBle(st) {
  var setTxt = function (id, val) {
    var el = document.getElementById(id);
    if (el) el.textContent = val;
  };
  var numOrDash = function (v) {
    return (typeof v === 'number' && isFinite(v)) ? String(v) : '—';
  };
  if (st && typeof st === 'object') {
    setTxt('b_conn', AFU.yesNo(st.ble_connected));
    setTxt('b_ready', AFU.yesNo(st.ble_ready));
    setTxt('b_reconn', numOrDash(st.ble_reconnects));
    setTxt('b_rx', numOrDash(st.ble_rx_total));
    setTxt('b_bad', numOrDash(st.ble_bad_total));
    setTxt('b_err', numOrDash(st.ble_errors));
    var age = '—';
    if (st.ble_last_rec && typeof st.ble_last_rec === 'object' &&
        typeof st.ble_last_rec.age_ms === 'number' && isFinite(st.ble_last_rec.age_ms)) {
      age = Math.round(st.ble_last_rec.age_ms / 1000) + ' с';
    }
    setTxt('b_age', age);
  } else {
    setTxt('b_conn', '—');
    setTxt('b_ready', '—');
    setTxt('b_reconn', '—');
    setTxt('b_rx', '—');
    setTxt('b_bad', '—');
    setTxt('b_err', '—');
    setTxt('b_age', '—');
  }
}

function renderMqtt(st) {
  var setTxt = function (id, val) {
    var el = document.getElementById(id);
    if (el) el.textContent = val;
  };
  var numOrDash = function (v) {
    return (typeof v === 'number' && isFinite(v)) ? String(v) : '—';
  };
  var m = (st && typeof st === 'object') ? st.mqtt : null;
  if (!m || typeof m !== 'object') {
    setTxt('m_en', '—');
    setTxt('m_conn', '—');
    setTxt('m_pub', '—');
    setTxt('m_fail', '—');
    setTxt('m_err', '—');
    return;
  }
  setTxt('m_en', AFU.yesNo(m.enabled));
  setTxt('m_conn', AFU.yesNo(m.connected));
  setTxt('m_pub', numOrDash(m.published));
  setTxt('m_fail', numOrDash(m.failed));
  setTxt('m_err', (m.last_err && String(m.last_err).length) ? String(m.last_err) : '—');
}

function renderBadges() {
  var b = AFSEC.badges(sysData);
  for (var i = 0; i < AFSEC.IDS.length; i++) {
    var id = AFSEC.IDS[i];
    var el = document.getElementById('nb_' + id);
    if (el) {
      el.textContent = b[id].text;
      el.className = 'nb' + (b[id].level ? ' ' + b[id].level : '');
    }
    if (id === 'ble' && typeof AFBL === 'object' && sysData.bl && AFBL.state(sysData.bl) === 'ok') {
      var bb = AFBL.badge(sysData.bl, sysData.bs, Math.floor(Date.now() / 1000));
      if (bb.text !== '' || bb.level !== '') {
        b[id] = { text: bb.text !== '' ? bb.text : b[id].text, level: bb.level !== '' ? bb.level : b[id].level };
        if (el) { el.textContent = b[id].text; el.className = 'nb' + (b[id].level ? ' ' + b[id].level : ''); }
      }
    }
    var link = document.querySelector('#sidelist a[data-sec="' + id + '"]');
    if (link) link.setAttribute('data-level', b[id].level);
  }
  if (typeof scAvail === 'function') scAvail();
}

function loadStorageData() {
  return fetchJSON('/api/history/status').then(function (hs) {
    if (!state.spans) state.spans = {};
    if (state.spansAt != null && Date.now() - state.spansAt < 60000) {
      return hs;
    }
    function ts(text) {
      if (!text) return null;
      var lines = text.split('\n');
      if (lines.length < 2) return null;
      var line = lines[1];
      var i = line.indexOf(',');
      var part = i >= 0 ? line.substring(0, i) : line;
      var n = parseInt(part, 10);
      return isFinite(n) ? n : null;
    }
    var chain = Promise.resolve();
    ['raw', 'agg', 'env'].forEach(function (k) {
      chain = chain.then(function () {
        var r = hs[k];
        if (!r || r.count < 2 || !isFinite(r.oldest) || !isFinite(r.seq)) return;
        return fetchText('/api/history.csv?kind=' + k + '&since=' + r.oldest + '&limit=1').then(function (r1) {
          var first = ts(r1.text);
          if (first === null) return;
          return fetchText('/api/history.csv?kind=' + k + '&since=' + (r.seq - 1) + '&limit=1').then(function (r2) {
            var last = ts(r2.text);
            if (last !== null && last >= first) state.spans[k] = last - first;
          });
        });
      }).catch(function () {});
    });
    return chain.then(function () {
      state.spansAt = Date.now();
      return hs;
    });
  });
}
function refresh() {
  if (sysBusy) return;
  sysBusy = true;
  var need = AFSEC.needs(sysCur);
  var chain = Promise.resolve();
  var rendering = false;
  for (var i = 0; i < need.length; i++) {
    (function (kind) {
      chain = chain.then(function () {
        if (kind === 'status') return fetchJSON('/api/status').then(function (v) { sysData.st = v; });
        if (kind === 'system') return fetchJSON('/api/system').then(function (v) { sysData.sy = v; });
        if (kind === 'sensors') return fetchJSON('/api/sensors').then(function (v) { sysData.sn = v; });
        if (kind === 'storage') return loadStorageData().then(function (v) { sysData.hs = v; });
        if (kind === 'ble') return fetchJSON('/api/ble').then(function (v) {
          sysData.bl = v;
          if (typeof blPush === 'function') {
            rendering = true;
            blPush(v);
            rendering = false;
          }
        }, function (e) { if (e && e.status === 404) { sysData.bl = null; return; } throw e; });
        if (kind === 'sessions') return fetchJSON('/api/ble/sessions?limit=32').then(function (v) { sysData.bs = v; }, function (e) { if (e && e.status === 404) { sysData.bs = []; return; } throw e; });
      });
    })(need[i]);
  }
  chain.then(function () {
    rendering = true;
    if (sysCur === 'status') {
      renderSystem(sysData.st, sysData.sy);
    } else if (sysCur === 'system' || sysCur === 'wifi' || sysCur === 'maint') {
      renderSystem(sysData.st, sysData.sy);
    } else if (sysCur === 'ble') {
      renderBle(sysData.st);
      if (typeof blRender === 'function') blRender();
    } else if (sysCur === 'sensors') {
      renderSensors(AFHM.sensorCards(sysData.sn));
    } else if (sysCur === 'mqtt') {
      renderMqtt(sysData.st);
    } else if (sysCur === 'storage') {
      renderStore(sysData.hs);
    }
    renderBadges();
    var note = document.getElementById('sysNote');
    if (note) note.textContent = '';
    var d = new Date();
    var p2 = function (n) { return (n < 10 ? '0' : '') + n; };
    var ts = p2(d.getHours()) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds());
    setPill('ok', 'обновлено ' + ts);
  }).catch(function (e) {
    var note = document.getElementById('sysNote');
    if (!rendering && e && e.status === 401) {
      if (note) note.textContent = 'Нужна авторизация';
      setPill('warn', 'нужна авторизация');
    } else {
      var message = 'нет связи с платой';
      if (rendering) message = 'ошибка отображения данных';
      else if (e && e.status) message = 'ошибка платы (HTTP ' + e.status + ')';
      else if (e && e.name === 'AbortError') message = 'плата не ответила вовремя';
      else if (e && e.name === 'SyntaxError') message = 'некорректный ответ платы';
      if (note) note.textContent = message;
      setPill('bad', message);
    }
  }).then(function () {
    sysBusy = false;
    if (sysDirty) { sysDirty = false; refresh(); }
  });
}

function activate(id, fromUser) {
  if (AFSEC.IDS.indexOf(id) === -1) id = 'status';
  sysCur = id;
  var secs = document.querySelectorAll('.sec');
  for (var i = 0; i < secs.length; i++) {
    secs[i].hidden = secs[i].getAttribute('data-sec') !== id;
  }
  var links = document.querySelectorAll('#sidelist a[data-sec]');
  for (var j = 0; j < links.length; j++) {
    if (links[j].getAttribute('data-sec') === id) {
      links[j].setAttribute('aria-current', 'true');
    } else {
      links[j].removeAttribute('aria-current');
    }
  }
  try {
    localStorage.setItem(SYS_LS_KEY, id);
  } catch (e) {}
  if (sysBusy) sysDirty = true;
  if (id === 'db' && typeof pushRefresh === 'function') pushRefresh();
  refresh();
}

function readStored() {
  try {
    return localStorage.getItem(SYS_LS_KEY);
  } catch (e) {
    return null;
  }
}

function initSystemView() {
  activate(AFSEC.pick(location.hash, readStored()));
  if (typeof AFSET !== 'undefined') AFSET.init();
  if (typeof AFSEL !== 'undefined') AFSEL.init();
  if (AFSEC.fromHash(location.hash) === null) {
    try {
      history.replaceState(null, '', AFSEC.toHash(sysCur));
    } catch (e) {}
  }
  window.addEventListener('hashchange', function () {
    var id = AFSEC.fromHash(location.hash);
    if (id !== null) activate(id, true);
  });
  setInterval(function () {
    if (document.visibilityState === 'visible') refresh();
  }, 5000);
}
