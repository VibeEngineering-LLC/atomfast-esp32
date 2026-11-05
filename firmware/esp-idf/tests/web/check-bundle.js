// e16g: синтаксическая проверка встроенных скриптов собранной web/history.html (node --check по каждому <script>).
// Ловит поломку склейки (например, перевод строки внутри строкового литерала после вклейки фрагмента). Запуск: node tests/web/check-bundle.js
const fs = require('fs'), os = require('os'), path = require('path'), cp = require('child_process');
const html = fs.readFileSync(process.argv[2] || path.join(__dirname, '..', '..', 'web', 'history.html'), 'utf8');
const re = /<script[^>]*>([\s\S]*?)<\/script>/g;
let m, i = 0, bad = 0;
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'af_bundle_'));
try {
  while ((m = re.exec(html)) !== null) {
    if (!m[1].trim()) continue;
    const p = path.join(temp, 'script_' + (++i) + '.js');
    fs.writeFileSync(p, m[1], 'utf8');
    const r = cp.spawnSync(process.execPath, ['--check', p], { encoding: 'utf8' });
    if (r.status !== 0) { bad++; console.log('FAIL script ' + i + ': ' + (r.error || r.stderr || '').toString().split('\n').slice(0, 5).join(' | ')); }
  }
} finally {
  fs.rmSync(temp, { recursive: true, force: true });
}
if (i === 0) { bad++; console.log('FAIL no inline scripts'); }
console.log('scripts: ' + i + ', syntax errors: ' + bad);
process.exit(bad ? 1 : 0);
