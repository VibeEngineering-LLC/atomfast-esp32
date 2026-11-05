(async () => {
  const P = window.__LC || {};
  const width = P.width || 1280;
  const theme = P.theme || 'light';
  const step = P.step || 'select';
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  document.body.style.margin = '0';
  const out = document.getElementById('out');
  if (out) out.style.display = 'none';
  const f = document.createElement('iframe'); document.body.appendChild(f);
  f.style.position = 'static';
  f.style.display = 'block';
  f.style.border = '0';
  f.style.width = width + 'px';
  f.style.height = '1300px';
  f.src = location.origin + '/?theme=' + theme + '&vis=cps,air_p,air_t&sm=cps:wma:60,air_t:sma:300';
  await new Promise(r => { f.onload = r; });
  await sleep(2500);
  const W = f.contentWindow;
  const D = W.document;
  const canvas = D.getElementById('chart');
  const click = el => el.dispatchEvent(new W.MouseEvent('click', { bubbles: true, cancelable: true }));
  const stripX = id => {
    const a = W.axModel.axes.find(z => z.id === id);
    const sw = W.axisSlotsWidth();
    const L = W.layout();
    return a.side === 'L' ? L.left - (a.slot + 0.5) * sw : canvas.clientWidth - L.right + (a.slot + 0.5) * sw;
  };
  const pick = async id => {
    const r = canvas.getBoundingClientRect();
    for (const type of ['pointerdown', 'pointerup']) canvas.dispatchEvent(new W.MouseEvent(type, { clientX: r.left + stripX(id), clientY: r.top + 150, bubbles: true }));
    await sleep(200);
  };
  if (step === 'select') {
    await pick('temp');
    click(D.getElementById('axbPlus'));
    await sleep(300);
  } else if (step === 'panel') {
    click(D.getElementById('btnAxes'));
    await pick('press');
    const row = D.querySelector('#axpanel .axrow[data-axis="count"]');
    const z = row.querySelector('.axzero');
    z.checked = true;
    z.dispatchEvent(new W.Event('change', { bubbles: true }));
    await sleep(300);
  }
  return JSON.stringify({ ok: true })
})()
