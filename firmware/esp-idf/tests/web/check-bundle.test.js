'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const { test } = require('node:test');
const checker = path.join(__dirname, 'check-bundle.js');

test('bundle syntax checker rejects empty or malformed inline scripts', () => {
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'af_check_fixture_'));
  try {
    for (const [html, valid] of [
      ['<p>no scripts</p>', false], ['<script>  </script>', false],
      ['<script>const broken = ;</script>', false], ['<script>const good = 1;</script>', true]
    ]) {
      const file = path.join(temp, 'page.html');
      fs.writeFileSync(file, html);
      const result = cp.spawnSync(process.execPath, [checker, file], { encoding: 'utf8' });
      assert.equal(result.status === 0, valid, result.stdout + result.stderr);
    }
  } finally {
    fs.rmSync(temp, { recursive: true, force: true });
  }
});

test('simultaneous syntax checks use separate temporary files and clean them up', async () => {
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'af_parallel_fixture_'));
  try {
    const file = path.join(temp, 'page.html');
    fs.writeFileSync(file, '<script>const good = 1;</script>');
    const results = await Promise.all([1, 2].map(() => new Promise((resolve, reject) => {
      cp.execFile(process.execPath, [checker, file], { env: { ...process.env, TMP: temp, TEMP: temp, TMPDIR: temp } },
        (error, stdout) => error ? reject(error) : resolve(stdout));
    })));
    assert.ok(results.every(result => result.includes('syntax errors: 0')));
    assert.deepEqual(fs.readdirSync(temp), ['page.html']);
  } finally {
    fs.rmSync(temp, { recursive: true, force: true });
  }
});
