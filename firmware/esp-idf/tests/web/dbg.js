(async () => {
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const P = window.__LC || {};
  const f = document.createElement('iframe');
  f.style.cssText = 'position:fixed;left:0;top:0;width:' + (P.width || 1280) + 'px;height:1000px;opacity:0';
  f.src = location.origin + '/system?theme=light#ble';
  document.body.appendChild(f);
  await new Promise(r => { f.onload = r; });
  await sleep(3500);
  const W = f.contentWindow, D = f.contentDocument;
  const cw = D.documentElement.clientWidth;
  const bad = Array.from(D.querySelectorAll('body *')).filter(e => e.getBoundingClientRect().right > cw + 1 && e.offsetParent !== null).slice(0, 12).map(e => e.tagName + '#' + e.id + '.' + e.className + ' r=' + Math.round(e.getBoundingClientRect().right) + ' w=' + Math.round(e.getBoundingClientRect().width));
  return JSON.stringify({ cw, sw: D.documentElement.scrollWidth, bad });
})()
