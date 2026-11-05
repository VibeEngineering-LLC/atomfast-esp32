(function (root, factory) { if (typeof module === 'object' && module.exports) { module.exports = factory(); } else { root.AFFB = factory(); } })(this, function () { 'use strict';

var ROUTES = {
  csrf: '/api/csrf-token',
  check: '/api/ota/github/check',
  install: '/api/ota/github/install',
  progress: '/api/ota/github/progress',
  channel: '/api/ota/github/channel',
  system: '/api/system'
};
var REPO = 'VibeEngineering-LLC/atomfast-esp32';
var ASSET_NAME = 'atomfast_gw.bin';
var SUMS_NAME = 'SHA256SUMS.txt';
var MEM_NEED_MIN = 18432;
var MEM_NEED_MAX = 30720;
var MEM_WARN = 61440;
var MEM_BAD = 45056;
var BLOCK_MIN = 12288;
var CHECK_POLL_MS = 1500;
var CHECK_MAX_MS = 60000;
var PROGRESS_POLL_MS = 1000;
var STALE_POLLS = 4;
var HEAVY_WARNING = 'Операция тяжёлая: плата сама держит TLS-соединение с GitHub и расходует около 18–30 КБ внутренней памяти. Не запускайте её при низком запасе памяти.';

var REASONS = {
  no_internet: 'У платы нет доступа в интернет',
  network_error: 'Плата не смогла связаться с GitHub',
  no_releases: 'В репозитории нет подходящих релизов',
  up_to_date: 'Установлена последняя версия',
  downgrade_blocked: 'Понижение версии через плату заблокировано',
  too_old_for_wifi_ota: 'Релиз слишком старый для обновления по Wi-Fi, нужна ручная заливка .bin',
  release_missing_assets: 'В релизе нет файла atomfast_gw.bin или SHA256SUMS.txt'
};

var STATES = {
  idle: 'Ожидание',
  checking: 'Проверка релиза',
  downloading: 'Скачивание образа',
  verifying: 'Проверка SHA-256 и образа',
  installing: 'Запись в запасной слот',
  done: 'Установлено, плата перезагружается',
  error: 'Ошибка'
};

var ERRORS = {
  no_release_cached: 'плата не нашла релиз для установки',
  sums_download_failed: 'не удалось скачать SHA256SUMS.txt',
  sha256sums_missing_entry: 'в SHA256SUMS.txt нет записи для atomfast_gw.bin',
  no_ota_partition: 'на плате нет раздела для обновления',
  ota_busy: 'другая заливка уже идёт',
  ota_begin_failed: 'плата не смогла начать запись образа',
  oom: 'не хватило памяти',
  asset_http_status: 'GitHub отдал файл с ошибкой',
  image_too_large_for_partition: 'образ не помещается в раздел',
  download_or_image_error: 'обрыв скачивания или повреждённый образ',
  incomplete_image: 'образ скачан не полностью',
  sha256_mismatch: 'SHA-256 образа не совпала с SHA256SUMS.txt',
  ota_end_failed: 'плата не приняла образ при завершении записи',
  set_boot_partition_failed: 'плата не смогла выбрать новый образ для загрузки'
};

function hasOwn(o, k) { return Object.prototype.hasOwnProperty.call(o, k); }

function num(v) { return typeof v === 'number' && isFinite(v) && v >= 0 ? v : 0; }

function reasonText(reason, installable) {
  if (installable) { return 'Можно установить'; }
  if (hasOwn(REASONS, reason)) { return REASONS[reason]; }
  return reason ? 'Причина: ' + reason : 'Установка недоступна';
}

function safeUrl(u) {
  if (typeof u !== 'string') { return ''; }
  return /^https:\/\/github\.com\/[^\s"'<>]*$/.test(u) ? u : '';
}

function parseCheck(o) {
  var bad = { state: 'bad', error: 'Плата вернула неожиданный ответ проверки' };
  if (!o || typeof o !== 'object') { return bad; }
  if (o.state === 'checking') { return { state: 'checking' }; }
  if (o.state !== 'done') { return bad; }
  var latest = typeof o.latest === 'string' ? o.latest : '';
  var current = typeof o.current === 'string' && o.current !== '?' ? o.current : '';
  var reason = typeof o.reason === 'string' ? o.reason : '';
  var installable = o.installable === true && latest !== '';
  return {
    state: 'done',
    current: current,
    latest: latest,
    newer: o.newer === true,
    installable: installable,
    reason: reason,
    reasonText: reasonText(reason, installable),
    htmlUrl: safeUrl(o.html_url)
  };
}

function errorText(code) {
  var base = hasOwn(ERRORS, code) ? ERRORS[code] : (code ? 'код ' + code : 'причина не указана');
  return 'Установка не удалась: ' + base + '. Прежняя прошивка осталась.';
}

function parseProgress(o) {
  if (!o || typeof o !== 'object' || typeof o.state !== 'string' || !hasOwn(STATES, o.state)) {
    return { state: 'bad', bytes: 0, total: 0, percent: 0, error: '', errorText: '', label: 'Неожиданный ответ платы', terminal: false, active: false };
  }
  var bytes = num(o.bytes);
  var total = num(o.total);
  var err = typeof o.error === 'string' ? o.error : '';
  return {
    state: o.state,
    bytes: bytes,
    total: total,
    percent: total > 0 ? Math.min(100, Math.round(bytes * 100 / total)) : 0,
    error: err,
    errorText: o.state === 'error' ? errorText(err) : '',
    label: STATES[o.state],
    terminal: o.state === 'done' || o.state === 'error',
    active: o.state === 'checking' || o.state === 'downloading' || o.state === 'verifying' || o.state === 'installing'
  };
}

function isStale(baseline, cur, seenActive, polls) {
  if (seenActive || polls >= STALE_POLLS) { return false; }
  if (cur.state === 'idle') { return true; }
  if (!baseline || !cur.terminal) { return false; }
  return baseline.state === cur.state && baseline.error === cur.error && baseline.bytes === cur.bytes;
}

function installError(status) {
  if (status === 403) { return 'Плата отклонила запрос: токен безопасности недействителен. Обновите страницу.'; }
  if (status === 409) { return 'Установка уже идёт или не хватило памяти для запуска. Прежняя прошивка осталась.'; }
  return 'Плата ответила ошибкой ' + status + '. Прежняя прошивка осталась.';
}

function checkError(status) {
  if (status === 403) { return 'Плата отклонила запрос: токен безопасности недействителен. Обновите страницу.'; }
  return 'Плата ответила на проверку ошибкой ' + status;
}

function kb(v) { return Math.round(v / 1024) + ' КБ'; }

function memInfo(sy) {
  var free = sy && typeof sy.int_free === 'number' && isFinite(sy.int_free) && sy.int_free >= 0 ? sy.int_free : null;
  var largest = sy && typeof sy.int_largest === 'number' && isFinite(sy.int_largest) && sy.int_largest >= 0 ? sy.int_largest : null;
  if (free === null) {
    return { known: false, free: null, largest: largest, level: 'unknown', text: 'Свободная внутренняя память платы неизвестна' };
  }
  var level = 'ok';
  if (free < MEM_WARN) { level = 'warn'; }
  if (free < MEM_BAD || (largest !== null && largest < BLOCK_MIN)) { level = 'bad'; }
  var text = 'Свободно внутренней памяти: ' + kb(free);
  if (largest !== null) { text += ', крупнейший блок ' + kb(largest); }
  if (level === 'warn') { text += ' (запас ниже ' + kb(MEM_WARN) + ')'; }
  if (level === 'bad') { text += ' (запас мал, установку лучше отложить)'; }
  return { known: true, free: free, largest: largest, level: level, text: text };
}

return {
  ROUTES: ROUTES,
  REPO: REPO,
  ASSET_NAME: ASSET_NAME,
  SUMS_NAME: SUMS_NAME,
  MEM_NEED_MIN: MEM_NEED_MIN,
  MEM_NEED_MAX: MEM_NEED_MAX,
  MEM_WARN: MEM_WARN,
  MEM_BAD: MEM_BAD,
  BLOCK_MIN: BLOCK_MIN,
  CHECK_POLL_MS: CHECK_POLL_MS,
  CHECK_MAX_MS: CHECK_MAX_MS,
  PROGRESS_POLL_MS: PROGRESS_POLL_MS,
  STALE_POLLS: STALE_POLLS,
  HEAVY_WARNING: HEAVY_WARNING,
  reasonText: reasonText,
  safeUrl: safeUrl,
  parseCheck: parseCheck,
  errorText: errorText,
  parseProgress: parseProgress,
  isStale: isStale,
  installError: installError,
  checkError: checkError,
  memInfo: memInfo
};
});
