(async () => {
  // height of everything between the nav bar and the chart canvas, per width, on page "/"
  const P = window.__LC || {};
  const out = [];
  for (const width of (P.widths || [1280])) {
    const f = document.createElement('iframe');
    f.style.cssText = 'position:fixed;left:0;top:0;border:0;opacity:0;pointer-events:none;';
    f.width = width; f.height = 900; f.src = location.origin + '/?theme=dark';
    document.body.appendChild(f);
    await new Promise(r => { f.onload = r; });
    await new Promise(r => setTimeout(r, P.wait || 1500));
    const D = f.contentDocument, nav = D.querySelector('nav.tabs'), cv = D.getElementById('chart');
    const nb = nav.getBoundingClientRect().bottom, ct = cv.getBoundingClientRect().top;
    out.push({ width, navBottom: Math.round(nb), chartTop: Math.round(ct), panelsH: Math.round(ct - nb), chartH: Math.round(cv.getBoundingClientRect().height) });
    f.remove();
  }
  return JSON.stringify(out);
})()
