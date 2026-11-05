(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const theme = P.theme || 'light';
  const step = P.step || 'table';
  const sleep = P.sleep || ((ms) => new Promise((r) => setTimeout(r, ms)));
  document.body.style.margin = '0';
  const out = document.getElementById('out');
  if (out) out.style.display = 'none';
  const f = document.createElement('iframe');
  f.style.position = 'static';
  f.style.display = 'block';
  f.style.border = '0';
  f.style.width = width + 'px';
  f.style.height = '1500px';
  f.src = location.origin + '/system?theme=' + theme + '&gh=' + encodeURIComponent(location.origin + '/__gh') + '#firmware';
  document.body.appendChild(f);
  await new Promise((res) => { f.onload = res; });
  await sleep(2500);
  const W = f.contentWindow;
  const D = f.contentDocument;
  const fwCheck = D.getElementById('fwCheck');
  if (fwCheck) fwCheck.click();
  await sleep(1500);
  if (step === 'confirm' || step === 'verify-bad') {
    const target = step === 'confirm' ? 'firmware-v1.2.40-rc1' : 'firmware-v1.2.39';
    const rows = D.querySelectorAll('table tr');
    for (const r of rows) {
      const c = r.querySelector('td');
      if (c && c.textContent.trim().startsWith(target)) {
        const btn = r.querySelector('button');
        if (btn) btn.click();
        break;
      }
    }
    const u = D.getElementById('fwUnderstand');
    if (u) {
      u.checked = true;
      u.dispatchEvent(new W.Event('change', { bubbles: true }));
    }
    const go = D.getElementById('fwGo');
    if (go) go.click();
    await sleep(2000);
  }
  return JSON.stringify({ ok: true });
})()
