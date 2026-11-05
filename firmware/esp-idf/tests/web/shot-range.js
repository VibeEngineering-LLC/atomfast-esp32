(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280, theme = P.theme || 'light', step = P.step || 'default';
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  document.body.style.margin = '0';
  try { localStorage.clear(); } catch (e) {}
  const f = document.createElement('iframe');
  f.style.cssText = 'display:block;border:0;width:' + width + 'px;height:1500px;';
  f.src = location.origin + '/?theme=' + theme + (step === 'range' ? '&from=1792853845&to=1792856445' : '');
  document.body.appendChild(f);
  await new Promise((res) => { f.onload = res; });
  await sleep(3500);
  const D = f.contentDocument, W = f.contentWindow;
  const go = (id) => D.getElementById(id).dispatchEvent(new W.MouseEvent('click', { bubbles: true }));
  const put = (id, v) => { const el = D.getElementById(id); el.value = v; el.dispatchEvent(new W.Event('input', { bubbles: true })); };
  const cv = D.getElementById('chart'), r = cv.getBoundingClientRect(), n = Math.floor(Date.now() / 1000);
  const ev = (t, x) => cv.dispatchEvent(new W.PointerEvent(t, { bubbles: true, pointerId: 1, clientX: r.left + x, clientY: r.top + 80 }));
  if (step === 'panel' || step === 'warn') go('btnRange');
  if (step === 'warn') { put('rgFrom', W.AFRG.toInput(n - 172800)); put('rgTo', W.AFRG.toInput(n - 60)); }
  if (step === 'pick') { go('btnPick'); ev('pointerdown', r.width * 0.35); ev('pointermove', r.width * 0.65); }
  await sleep(1500);
  return JSON.stringify({ ok: true });
})()
