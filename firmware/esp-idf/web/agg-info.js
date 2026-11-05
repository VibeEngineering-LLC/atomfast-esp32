(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(); } else { root.AFAG = factory(); } })(this, function () { 'use strict';

var ND = 'н/д';
var RAW_TEXT = 'Сырые — каждое измерение (шаг ≈2 с)';

// Интервал агрегации: window_s из /api/history/status (константа прошивки AF_STORE_WINDOW_S, API её не меняет)
function windowOf(status) {
  var v = status && typeof status === 'object' ? status.window_s : undefined;
  return typeof v === 'number' && isFinite(v) && v > 0 && Math.floor(v) === v ? v : null;
}

function sec(n) { return n === null ? ND : n + ' с'; }

function aggText(n) {
  return 'Агрегаты — усреднённые за ' + sec(n) + ' точки, удобны для длинных окон; хранятся дольше';
}

// Подсказка кнопки (title)
function title(kind, n) {
  if (kind === 'raw') { return RAW_TEXT; }
  if (kind === 'agg') { return aggText(n); }
  return '';
}

// Строка под переключателем (e16g, T-41): вид выбирается автоматически, интервал агрегации здесь не упоминается
function note(kind, n) {
  if (kind === 'raw' || kind === 'agg') { return 'Масштаб подбирается автоматически по ширине графика.'; }
  return '';
}

// Подпись строки «Агрегаты» в разделе «Хранилище»
function storageLabel(n) { return 'усреднённые точки за ' + sec(n); }

function pickView(windowSec, widthPx, status, rawRate) {
    // Шаг 1: Нормализация ширины графика (target)
    var target;
    if (typeof widthPx === 'number' && isFinite(widthPx) && widthPx > 0) {
        target = Math.round(widthPx);
    } else {
        target = 800;
    }
    // Ограничение диапазона 200..2000
    if (target < 200) target = 200;
    if (target > 2000) target = 2000;

    // Шаг 2: Извлечение данных из status с проверками
    var rawCount = 0;
    var aggCount = 0;
    var aggStep = 60;

    if (status && typeof status === 'object') {
        // raw.count
        if (status.raw && typeof status.raw === 'object') {
            if (typeof status.raw.count === 'number' && isFinite(status.raw.count) && status.raw.count >= 0) {
                rawCount = status.raw.count;
            }
        }
        // agg.count
        if (status.agg && typeof status.agg === 'object') {
            if (typeof status.agg.count === 'number' && isFinite(status.agg.count) && status.agg.count >= 0) {
                aggCount = status.agg.count;
            }
        }
        // window_s (aggStep)
        if (typeof status.window_s === 'number' && isFinite(status.window_s) && status.window_s > 0) {
            aggStep = status.window_s;
        }
    }

    // Шаг 3: Нормализация окна времени (win)
    var win = 0;
    if (typeof windowSec === 'number' && isFinite(windowSec) && windowSec > 0) {
        win = windowSec;
    }

    // Нормализация частоты сырых записей (rate)
    var rate = 0.5;
    if (typeof rawRate === 'number' && isFinite(rawRate) && rawRate > 0) {
        rate = rawRate;
    }

    // Шаг 4: Покрытие сырого кольца в секундах
    var rawCover = rawCount / rate;

    // Шаг 5: Выбор типа данных (kind)
    var kind;
    if (win > 0 && rawCount > 0 && win <= rawCover) {
        kind = 'raw';
    } else {
        kind = 'agg';
    }

    // Шаг 6: Расчет ожидаемого числа строк (expected)
    var expected;
    if (kind === 'raw') {
        expected = Math.ceil(win * rate);
    } else {
        if (win > 0) {
            expected = Math.ceil(win / aggStep);
        } else {
            expected = aggCount;
            if (expected < 1) expected = 1;
        }
    }

    // Шаг 7: Определение параметра прореживания (pts)
    var pts;
    if (expected > 2 * target) {
        pts = target;
    } else {
        pts = 0;
    }

    // Шаг 8: Расчет начала окна (from)
    var from = null;
    if (win > 0 && status && typeof status === 'object') {
        if (typeof status.now_ts === 'number' && isFinite(status.now_ts) && status.now_ts > 0) {
            var calculatedFrom = Math.floor(status.now_ts - win);
            if (calculatedFrom >= 0) {
                from = calculatedFrom;
            }
        }
    }

    // Шаг 9: Возврат результата
    return {
        kind: kind,
        pts: pts,
        from: from,
        expected: expected
    };
}
return { ND: ND, pickView: pickView, windowOf: windowOf, title: title, note: note, storageLabel: storageLabel };
});
