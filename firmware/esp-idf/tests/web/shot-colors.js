(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280, theme = P.theme || 'light', step = P.step || 'default';
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  document.body.style.margin = '0';
  const out = document.getElementById('out'); if (out) out.style.display = 'none';
  try { localStorage.clear(); } catch (e) {}
  if (step === 'chart') localStorage.setItem('afh.colors.v1', JSON.stringify({ v: 1, colors: { light: { rate: '#aa3366', cps: '#2563c9' }, dark: { rate: '#ffb000', cps: '#6ea8ff' } }, styles: { cps: { dash: 'dash', width: 3 } } }));
  const f = document.createElement('iframe');
  f.style.cssText = 'display:block;border:0;width:' + width + 'px;height:1900px;';
  const paths = { table: '/system#sensors', cfg: '/system#sensors', readings: '/readings', rsys: '/readings', schart: '/', ble: '/system#ble', bleh: '/system#ble', storage: '/system#storage', scdlg: '/system#storage' };
  if (step === 'rsys') localStorage.setItem('afh.sys.v1', JSON.stringify({ v: 1, pct: 5 }));
  const pp = (paths[step] || (step === 'chart' ? '/' : '/system#maint')).split('#');
  f.src = location.origin + pp[0] + '?theme=' + theme + (pp[1] ? '#' + pp[1] : '');
  document.body.appendChild(f);
  await new Promise((res) => { f.onload = res; });
  await sleep(3500);
  const D = f.contentDocument, W = f.contentWindow;
  const go = (el) => { if (el) el.dispatchEvent(new W.MouseEvent('click', { bubbles: true })); };
  if (step === 'custom') { go(D.querySelector('.clr-ser[data-id="cps"]')); go(D.querySelector('.clr-sw[data-hex="#8250df"]')); go(D.querySelector('.clr-ser[data-id="dose"]')); }
  if (step === 'cfg') go(D.querySelector('#sensRows tr[data-sensor="2"] button[aria-label^="Настроить"]'));
  if (step === 'schart') { W.eval("for (const d of SERIES) if (d.envKind) state.vis[d.id] = true; state.vis.rate = false; state.vis.cps = false; buildLegend(); redraw();"); }
  if (step === 'bleh') { const t = D.querySelector('#bleCards [data-card="sessions"]'); if (t) t.scrollIntoView(); }
  if (step === 'scdlg') { go(D.getElementById('scBtn_all')); const w = D.getElementById('scWord'); if (w) { w.value = 'ОЧИСТ'; } }
  if (step === 'cb') { go(D.getElementById('clrCb')); go(D.querySelector('#segScheme button[data-scheme="dark"]')); }
  await sleep(1500);
  return JSON.stringify({ ok: true });
})()
