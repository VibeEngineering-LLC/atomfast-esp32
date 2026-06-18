(function() {
  'use strict';

  // Целевой порядок строк по подстроке Name-колонки.
  // Группы: приборные показания → BLE → GATT → WiFi → Народмон → Система.
  // Кнопки размещены сразу под их тематическими блоками.
  var TARGET_ORDER = [
    // 1. Приборные показания
    'AtomFast: сырой',
    'CPS (мгновенный)',
    'CPS (среднее',
    'Импульсы',
    'Мощность дозы (среднее',
    'Мощность дозы (µR/h)',
    'Мощность дозы',
    'Накопленная доза',
    'Батарея AtomFast',
    'Температура AtomFast',
    'Интервал агрегации',

    // 2. BLE-диагностика + кнопки
    'BLE: AtomFast подключён',
    'BLE: длина последней сессии',
    'BLE: реконнектов всего',
    'BLE: успешных коннектов',
    'RSSI AtomFast',
    'Сбросить счётчики BLE',
    'Переподключиться к AtomFast',

    // 3. GATT
    'GATT таблица',
    'Перечитать GATT',

    // 4. WiFi
    'Подключено к WiFi',
    'WiFi сигнал',
    'IP адрес',

    // 5. Народмон
    'Выгружать на Народмон',
    'Протокол Народмона',
    'Имя метрики: мощность дозы',
    'Имя метрики: накопленная доза',
    'Имя метрики: температура',
    'Имя метрики: батарея',

    // 6. Система
    'ESP32 MAC',
    'Uptime',
    'Перезагрузить'
  ];

  function deepQueryAll(selector, root) {
    root = root || document;
    var found = [];
    function walk(node) {
      if (!node) return;
      if (node.querySelectorAll) {
        try {
          var local = node.querySelectorAll(selector);
          for (var i = 0; i < local.length; i++) found.push(local[i]);
        } catch (e) {}
      }
      if (node.shadowRoot) walk(node.shadowRoot);
      var kids = node.children || [];
      for (var j = 0; j < kids.length; j++) walk(kids[j]);
    }
    walk(root);
    return found;
  }

  function getNameText(row) {
    var cell = row.querySelector('td, th');
    if (!cell) return '';
    return (cell.textContent || '').trim();
  }

  function priority(name) {
    for (var i = 0; i < TARGET_ORDER.length; i++) {
      if (name.indexOf(TARGET_ORDER[i]) !== -1) return i;
    }
    return 9999;
  }

  function reorderTable(table) {
    var tbody = table.querySelector('tbody');
    if (!tbody) return false;
    var rows = Array.prototype.slice.call(tbody.children).filter(function (n) {
      return n.tagName === 'TR';
    });
    if (rows.length < 3) return false;

    var orig = rows.slice();
    var sorted = rows.slice().sort(function (a, b) {
      var pa = priority(getNameText(a));
      var pb = priority(getNameText(b));
      if (pa !== pb) return pa - pb;
      return orig.indexOf(a) - orig.indexOf(b);
    });

    var changed = false;
    for (var i = 0; i < sorted.length; i++) {
      if (sorted[i] !== orig[i]) { changed = true; break; }
    }
    if (!changed) return false;

    sorted.forEach(function (r) { tbody.appendChild(r); });
    return true;
  }

  function tryReorder() {
    var tables = deepQueryAll('table');
    tables.forEach(function (t) { reorderTable(t); });
  }

  function limitLogHeight() {
    var sel = 'esp-log, #log, .log, esp-app-log, esp-log-card';
    var logs = deepQueryAll(sel);
    logs.forEach(function (el) {
      el.style.display = 'block';
      el.style.maxHeight = '240px';
      el.style.overflowY = 'auto';
    });
  }

  function tick() {
    try { tryReorder(); } catch (e) {}
    try { limitLogHeight(); } catch (e) {}
  }

  function startObserver() {
    tick();
    var debounce;
    var obs = new MutationObserver(function () {
      clearTimeout(debounce);
      debounce = setTimeout(tick, 200);
    });
    try {
      obs.observe(document.body, { childList: true, subtree: true });
    } catch (e) {}
    setInterval(tick, 2000);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', startObserver);
  } else {
    startObserver();
  }
})();