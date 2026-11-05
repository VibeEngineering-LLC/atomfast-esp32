const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const AFU = require(path.join(__dirname, '..', '..', 'web', 'ui-common.js'));
const SRC = fs.readFileSync(process.env.HF_MODULE || path.join(__dirname, '..', '..', 'web', 'ui-font.js'), 'utf8');
let passed = 0, total = 0;
function check(name, fn) { total++; try { fn(); passed++; console.log('ok  ' + name); } catch (e) { console.log('FAIL ' + name + ': ' + e.message); } }

function makeEnv(opts) {
    opts = opts || {};
    const styleVars = {};
    const buttons = [
        { attrs: { 'data-fs': '90', 'aria-pressed': 'false' }, handlers: {}, getAttribute(k) { return this.attrs[k]; }, setAttribute(k, v) { this.attrs[k] = String(v); }, addEventListener(ev, fn) { this.handlers[ev] = fn; } },
        { attrs: { 'data-fs': '100', 'aria-pressed': 'false' }, handlers: {}, getAttribute(k) { return this.attrs[k]; }, setAttribute(k, v) { this.attrs[k] = String(v); }, addEventListener(ev, fn) { this.handlers[ev] = fn; } },
        { attrs: { 'data-fs': '115', 'aria-pressed': 'false' }, handlers: {}, getAttribute(k) { return this.attrs[k]; }, setAttribute(k, v) { this.attrs[k] = String(v); }, addEventListener(ev, fn) { this.handlers[ev] = fn; } },
        { attrs: { 'data-fs': '130', 'aria-pressed': 'false' }, handlers: {}, getAttribute(k) { return this.attrs[k]; }, setAttribute(k, v) { this.attrs[k] = String(v); }, addEventListener(ev, fn) { this.handlers[ev] = fn; } }
    ];
    const resetBtn = { attrs: { 'data-fs': '100', 'aria-pressed': 'false' }, handlers: {}, getAttribute(k) { return this.attrs[k]; }, setAttribute(k, v) { this.attrs[k] = String(v); }, addEventListener(ev, fn) { this.handlers[ev] = fn; } };
    const document = {
        documentElement: { style: { setProperty(k, v) { styleVars[k] = v; } } },
        querySelectorAll(sel) { return sel === '#segFont button' ? buttons : []; },
        getElementById(id) { return id === 'btnFontReset' ? resetBtn : null; }
    };
    const storage = {};
    if (opts.stored !== undefined) storage['af.fs'] = opts.stored;
    const localStorage = opts.noStorage ? {
        getItem() { throw new Error('blocked'); },
        setItem() { throw new Error('blocked'); },
        removeItem() { throw new Error('blocked'); }
    } : {
        getItem(k) { return k in storage ? storage[k] : null; },
        setItem(k, v) { storage[k] = String(v); },
        removeItem(k) { delete storage[k]; }
    };
    const ctx = vm.createContext({ AFU, document, localStorage, String });
    vm.runInContext(SRC, ctx);
    return { ctx, styleVars, buttons, resetBtn, storage, run: code => vm.runInContext(code, ctx) };
}

check('applyFont 115', () => {
    const env = makeEnv({});
    const res = env.run('applyFont(115)');
    assert.strictEqual(res, 115);
    assert.strictEqual(env.styleVars['--fs'], '1.15');
    assert.strictEqual(env.buttons[0].attrs['aria-pressed'], 'false');
    assert.strictEqual(env.buttons[1].attrs['aria-pressed'], 'false');
    assert.strictEqual(env.buttons[2].attrs['aria-pressed'], 'true');
    assert.strictEqual(env.buttons[3].attrs['aria-pressed'], 'false');
});

check('applyFont invalid', () => {
    const env = makeEnv({});
    assert.strictEqual(env.run("applyFont('abc')"), 100);
    assert.strictEqual(env.styleVars['--fs'], '1');
    assert.strictEqual(env.run('applyFont(null)'), 100);
    assert.strictEqual(env.run("applyFont('95')"), 100);
});

check('saveFont valid', () => {
    const env = makeEnv({});
    assert.strictEqual(env.run("saveFont('130')"), 130);
    assert.strictEqual(env.storage['af.fs'], '130');
    assert.strictEqual(env.run("saveFont('90')"), 90);
    assert.strictEqual(env.storage['af.fs'], '90');
});

check('saveFont 100 removes', () => {
    const env = makeEnv({ stored: '130' });
    assert.strictEqual(env.run("saveFont('100')"), 100);
    assert.ok(!('af.fs' in env.storage));
    assert.strictEqual(env.run("saveFont('zzz')"), 100);
    assert.ok(!('af.fs' in env.storage));
});

check('readFont', () => {
    const env1 = makeEnv({ stored: '115' });
    assert.strictEqual(env1.run('readFont()'), '115');
    const env2 = makeEnv({});
    assert.strictEqual(env2.run('readFont()'), null);
    const env3 = makeEnv({ noStorage: true });
    assert.strictEqual(env3.run('readFont()'), null);
});

check('initFontUi stored', () => {
    const env = makeEnv({ stored: '130' });
    env.run('initFontUi()');
    assert.strictEqual(env.styleVars['--fs'], '1.3');
    assert.strictEqual(env.buttons[3].attrs['aria-pressed'], 'true');
    assert.strictEqual(env.buttons[0].attrs['aria-pressed'], 'false');
    assert.strictEqual(env.buttons[1].attrs['aria-pressed'], 'false');
    assert.strictEqual(env.buttons[2].attrs['aria-pressed'], 'false');
});

check('initFontUi default', () => {
    const env = makeEnv({});
    env.run('initFontUi()');
    assert.strictEqual(env.styleVars['--fs'], '1');
    assert.strictEqual(env.buttons[1].attrs['aria-pressed'], 'true');
    assert.strictEqual(env.buttons[0].attrs['aria-pressed'], 'false');
    assert.strictEqual(env.buttons[2].attrs['aria-pressed'], 'false');
    assert.strictEqual(env.buttons[3].attrs['aria-pressed'], 'false');
});

check('initFontUi invalid stored', () => {
    const env = makeEnv({ stored: 'junk' });
    env.run('initFontUi()');
    assert.strictEqual(env.styleVars['--fs'], '1');
    assert.strictEqual(env.buttons[1].attrs['aria-pressed'], 'true');
});

check('initFontUi click handlers', () => {
    const env = makeEnv({});
    env.run('initFontUi()');
    env.buttons[2].handlers.click();
    assert.strictEqual(env.styleVars['--fs'], '1.15');
    assert.strictEqual(env.storage['af.fs'], '115');
    assert.strictEqual(env.buttons[2].attrs['aria-pressed'], 'true');
    env.buttons[1].handlers.click();
    assert.strictEqual(env.styleVars['--fs'], '1');
    assert.ok(!('af.fs' in env.storage));
    assert.strictEqual(env.buttons[1].attrs['aria-pressed'], 'true');
});

check('reset button', () => {
    const env = makeEnv({ stored: '130' });
    env.run('initFontUi()');
    env.resetBtn.handlers.click();
    assert.strictEqual(env.styleVars['--fs'], '1');
    assert.ok(!('af.fs' in env.storage));
    assert.strictEqual(env.buttons[1].attrs['aria-pressed'], 'true');
});

check('noStorage mode', () => {
    const env = makeEnv({ noStorage: true });
    env.run('initFontUi()');
    env.buttons[3].handlers.click();
    assert.strictEqual(env.styleVars['--fs'], '1.3');
    env.resetBtn.handlers.click();
    assert.strictEqual(env.styleVars['--fs'], '1');
});

check('missing elements', () => {
    const styleVars = {};
    const document = {
        documentElement: { style: { setProperty(k, v) { styleVars[k] = v; } } },
        querySelectorAll() { return []; },
        getElementById() { return null; }
    };
    const storage = { 'af.fs': '115' };
    const localStorage = {
        getItem(k) { return k in storage ? storage[k] : null; },
        setItem(k, v) { storage[k] = String(v); },
        removeItem(k) { delete storage[k]; }
    };
    const ctx = vm.createContext({ AFU, document, localStorage, String });
    vm.runInContext(SRC, ctx);
    vm.runInContext('initFontUi()', ctx);
    assert.strictEqual(styleVars['--fs'], '1.15');
});

console.log('TESTS: ' + passed + '/' + total); process.exit(passed === total ? 0 : 1);
