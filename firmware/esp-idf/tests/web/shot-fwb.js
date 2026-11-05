(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const theme = P.theme || 'light';
  const step = P.step || 'table';
  const q = P.q || 'check=ok';
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  document.body.style.margin = '0';
  const out = document.getElementById('out');
  if (out) out.style.display = 'none';
  await fetch('/__mock/board?' + q, { method: 'POST' });
  const f = document.createElement('iframe');
  f.style.position = 'static';
  f.style.display = 'block';
  f.style.border = '0';
  f.style.width = width + 'px';
  f.style.height = '1700px';
  f.src = location.origin + '/system?theme=' + theme + '&gh=' + encodeURIComponent(location.origin + '/__gh') + '#firmware';
  document.body.appendChild(f);
  await new Promise((res) => { f.onload = res; });
  await sleep(2500);
  const W = f.contentWindow;
  const D = f.contentDocument;
  const tick = (id) => {
    const el = D.getElementById(id);
    if (el) {
      el.checked = true;
      el.dispatchEvent(new W.Event('change', { bubbles: true }));
    }
  };
  const press = (id) => {
    const el = D.getElementById(id);
    if (el) el.click();
  };
  if (step !== 'initial') {
    press('fbCheck');
    await sleep(6000);
  }
  if (step === 'fallback') {
    tick('fbBrowserOn');
    press('fwCheck');
    await sleep(2500);
  }
  if (step === 'confirm' || step === 'progress' || step === 'fail') {
    press('fbInstall');
    tick('fbUnderstand');
    await sleep(1500);
  }
  if (step === 'progress' || step === 'fail') {
    press('fbGo');
    await sleep(6000);
  }
  return JSON.stringify({ ok: true });
})()
