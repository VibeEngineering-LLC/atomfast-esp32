var blRingPps = [];
var blRingRssi = [];

function blEl(id) { return document.getElementById(id); }

function blNode(tag, cls, text) {
  var n = document.createElement(tag);
  if (cls) { n.className = cls; }
  if (text !== undefined) { n.textContent = text; }
  return n;
}

function blCardEl(card) {
  var sec = blNode('section', 'card');
  sec.setAttribute('data-card', card.id);
  sec.appendChild(blNode('h2', '', card.title));
  card.rows.forEach(function (r) {
    var row = blNode('div', 'kv');
    var k = blNode('span', 'k', r.k);
    if (r.hint) { k.appendChild(blNode('small', 'khint', r.hint)); }
    var v = blNode('span', 'v num' + (r.v === AFBL.ND ? ' nd' : ''), r.v);
    row.appendChild(k);
    row.appendChild(v);
    sec.appendChild(row);
  });
  return sec;
}

function blSpark(id, title, ring, unit, dec) {
  var box = blNode('div', 'spark');
  box.id = id;
  box.appendChild(blNode('div', 'sparkcap', title));
  var p = AFBL.sparkPath(ring, 240, 48);
  if (!p.d) {
    box.appendChild(blNode('div', 'hint', 'н/д: накоплено меньше двух отсчётов'));
    return box;
  }
  var svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 240 48');
  svg.setAttribute('class', 'sparksvg');
  svg.setAttribute('role', 'img');
  svg.setAttribute('aria-label', title);
  var path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
  path.setAttribute('d', p.d);
  path.setAttribute('fill', 'none');
  path.setAttribute('stroke', 'var(--s1)');
  path.setAttribute('stroke-width', '1.6');
  svg.appendChild(path);
  box.appendChild(svg);
  box.appendChild(blNode('div', 'hint', 'мин ' + AFBL.num(p.min, dec) + ' · макс ' + AFBL.num(p.max, dec) + ' ' + unit + ' · отсчётов ' + ring.filter(AFBL.isNum).length + ' из ' + AFBL.RING_MAX));
  return box;
}

function blHist(p) {
  var box = blNode('div', 'hist');
  box.id = 'bleHist';
  box.appendChild(blNode('div', 'sparkcap', 'Гистограмма RSSI за сессию, дБм (корзины по 10 дБ)'));
  AFBL.histBars(p).forEach(function (b) {
    var row = blNode('div', 'histrow');
    row.appendChild(blNode('span', 'histlab', b.label));
    var bar = blNode('span', 'histbar');
    var fill = blNode('span', 'histfill');
    fill.style.width = Math.round(b.frac * 100) + '%';
    bar.appendChild(fill);
    row.appendChild(bar);
    row.appendChild(blNode('span', 'histcnt num', String(b.count)));
    box.appendChild(row);
  });
  return box;
}

function blSessions(list) {
  var wrap = blNode('div', 'fw-wrap');
  var tbl = blNode('table', 'fw-table');
  tbl.id = 'bleSessions';
  var head = blNode('thead');
  var hr = blNode('tr');
  ['№', 'Начало', 'Длительность', 'Причина разрыва', 'Пакетов', 'Плохих', 'RSSI мин/сред/макс, дБм', 'Установка, мс'].forEach(function (t) { hr.appendChild(blNode('th', '', t)); });
  head.appendChild(hr);
  tbl.appendChild(head);
  var body = blNode('tbody');
  AFBL.sessionRows(list).forEach(function (r) {
    var tr = blNode('tr');
    [r.idx, r.start, r.dur, r.reason, r.pkts, r.bad, r.rssi, r.setup].forEach(function (t, i) { tr.appendChild(blNode('td', i >= 4 || i === 0 ? 'num' : '', t)); });
    body.appendChild(tr);
  });
  tbl.appendChild(body);
  wrap.appendChild(tbl);
  return wrap;
}

function blPush(b) {
  if (AFBL.state(b) !== 'ok') { return; }
  blRingPps = AFBL.pushSample(blRingPps, b.rate && b.rate.pps_inst);
  blRingRssi = AFBL.pushSample(blRingRssi, b.params && b.params.rssi_dbm);
}

function blRender() {
  var full = blEl('bleFull');
  var legacy = blEl('bleLegacy');
  if (!full || !legacy) { return; }
  var b = sysData.bl;
  var st = b === undefined || b === null ? 'legacy' : AFBL.state(b);
  legacy.hidden = st === 'ok' || st === 'notready';
  full.hidden = st !== 'ok' && st !== 'notready';
  if (full.hidden) { return; }
  var host = blEl('bleCards');
  host.textContent = '';
  if (st === 'notready') {
    var c = blNode('section', 'card');
    c.appendChild(blNode('h2', '', 'BLE'));
    c.appendChild(blNode('div', 'hint', 'Стек BLE ещё не готов, данных нет (н/д). Страница обновится сама.'));
    host.appendChild(c);
    return;
  }
  AFBL.cards(b).forEach(function (card) {
    var el = blCardEl(card);
    if (card.id === 'rate') {
      el.appendChild(blSpark('bleSparkPps', 'Пакетов в секунду за последние 10 минут', blRingPps, 'пакетов/с', 3));
    }
    if (card.id === 'params') {
      el.appendChild(blSpark('bleSparkRssi', 'RSSI за последние 10 минут', blRingRssi, 'дБм', 0));
      el.appendChild(blHist(b.params));
    }
    if (card.id === 'sessions') {
      var list = sysData.bs || [];
      if (list.length) {
        el.appendChild(blSessions(list));
      } else {
        el.appendChild(blNode('div', 'hint', 'Завершённых сессий пока нет (н/д).'));
      }
      var a = blNode('a', 'btn', 'Скачать CSV');
      a.id = 'bleCsv';
      a.href = '/api/ble/sessions.csv';
      a.setAttribute('download', 'ble_sessions.csv');
      el.appendChild(a);
    }
    host.appendChild(el);
  });
}
