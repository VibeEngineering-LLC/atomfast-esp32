(async () => {
  const P = window.__LC || {};
  const widths = P.widths || [375, 768, 1024, 1440, 2560];
  const paths = P.paths || ['/'];
  const themes = P.themes || ['light', 'dark'];
  const wait = P.wait || 2500;
  const results = [];
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const vis = el => el && el.offsetParent !== null && el.getBoundingClientRect().width > 0;
  const id = el => el.tagName.toLowerCase() + (el.className && typeof el.className === 'string' ? '.' + el.className.trim().split(/\s+/).join('.') : '') + (el.id ? '#' + el.id : '');
  const inter = (a, b) => {
    const x = Math.max(0, Math.min(a.right, b.right) - Math.max(a.left, b.left));
    const y = Math.max(0, Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top));
    return x * y;
  };
  for (const path of paths) {
    for (const width of widths) {
      for (const theme of themes) {
        const hash = path.includes('#') ? path.split('#')[1] : '';
        const pathname = path.split('#')[0];
        const url = location.origin + pathname + '?theme=' + theme + (hash ? '#' + hash : '');
        try { if (P.fs) localStorage.setItem('af.fs', String(P.fs)); else localStorage.removeItem('af.fs'); } catch (e) {}
        const f = document.createElement('iframe');
        f.style.cssText = 'position:fixed;left:0;top:0;border:0;opacity:0;pointer-events:none;';
        f.width = width;
        f.height = 900;
        f.src = url;
        document.body.appendChild(f);
        await new Promise(res => { f.onload = res; });
        await sleep(wait);
        let D = null;
        try { D = f.contentDocument; } catch (e) { D = null; }
        if (D && P.rgState) {
          const W2 = f.contentWindow, n2 = Math.floor(Date.now() / 1000);
          const put = (i, v) => { const e = D.getElementById(i); e.value = v; e.dispatchEvent(new W2.Event('input', { bubbles: true })); };
          D.getElementById('btnRange').dispatchEvent(new W2.MouseEvent('click', { bubbles: true }));
          put('rgFrom', W2.AFRG.toInput(n2 - 172800)); put('rgTo', W2.AFRG.toInput(n2 - 60));
          await sleep(500);
        }
        if (D && P.fwState) {
          await fetch('/__mock/board?check=ok&install=hold&free=40000&largest=30000', { method: 'POST' });
          const W = f.contentWindow;
          const tick = (i) => { const e = D.getElementById(i); if (e) { e.checked = true; e.dispatchEvent(new W.Event('change', { bubbles: true })); } };
          const press = (i) => { const e = D.getElementById(i); if (e) e.click(); };
          tick('fbBrowserOn'); press('fbCheck'); await sleep(6000);
          press('fbInstall'); tick('fbUnderstand'); await sleep(1500); press('fbGo'); await sleep(4000);
        }
        let toolbarH = 0, hscroll = false, contentW = 0, fullWidth = false, overlaps = 0, overlapList = [], cols = [], chart = null, widthSw = true;
        if (D) {
          const de = D.documentElement;
          hscroll = de.scrollWidth > de.clientWidth + 1;
          const main = D.querySelector('main.wrap');
          if (main) contentW = main.getBoundingClientRect().width;
          const expectedMin = Math.min(2400, de.clientWidth) - 48 - 2;
          if (width <= 2400) fullWidth = contentW >= expectedMin;
          else fullWidth = contentW >= 2400 - 2;
          const scope = main || D.body;
          const els = Array.from(scope.querySelectorAll('.card, .tile, .scard, .statusbar, .sidelist')).filter(vis);
          for (let i = 0; i < els.length; i++) {
            for (let j = i + 1; j < els.length; j++) {
              const a = els[i], b = els[j];
              if (!a.contains(b) && !b.contains(a)) {
                if (inter(a.getBoundingClientRect(), b.getBoundingClientRect()) > 4) {
                  overlaps++;
                  if (overlapList.length < 3) overlapList.push(id(a) + ' x ' + id(b));
                }
              }
            }
          }
          const docW = de.clientWidth;
          const gridEls = Array.from(D.querySelectorAll('.grid2, .sysgrid')).filter(vis);
          for (const g of gridEls) {
            let colEls = [];
            if (g.classList.contains('sysgrid')) colEls = Array.from(g.querySelectorAll(':scope > .sidelist, :scope > .secpane')).filter(vis);
            else colEls = Array.from(g.querySelectorAll(':scope > .col')).filter(vis);
            if (colEls.length >= 2 && docW >= 1024) {
              const bottoms = colEls.map(c => c.getBoundingClientRect().bottom);
              const colDiff = Math.abs(bottoms[0] - bottoms[1]);
              const cards = Array.from(g.querySelectorAll('.card, .tile, .scard')).filter(vis);
              const maxCardH = cards.length ? Math.max(...cards.map(c => c.getBoundingClientRect().height)) : 0;
              let ok;
              if (g.classList.contains('sysgrid')) {
                const left = colEls[0].getBoundingClientRect().width;
                ok = left > 200 && left < 320;
              } else {
                ok = colDiff <= maxCardH;
              }
              cols.push({ sel: id(g), colDiff, maxCardH, ok });
            }
          }
          const cv = D.querySelector('canvas#chart');
          if (cv && vis(cv)) {
            const r = cv.getBoundingClientRect();
            const ratio = contentW ? r.width / contentW : 0;
            chart = { w: r.width, h: r.height, ratio, ok: ratio >= 0.85 && r.height >= 320 };
          }
          const tb = Array.from(D.querySelectorAll('.toolbar .tg > *, .toolbar .seg button, .legend .leg')).filter(vis);
          for (let i = 0; i < tb.length; i++) for (let j = i + 1; j < tb.length; j++) if (!tb[i].contains(tb[j]) && !tb[j].contains(tb[i]) && inter(tb[i].getBoundingClientRect(), tb[j].getBoundingClientRect()) > 4) { overlaps++; if (overlapList.length < 3) overlapList.push(id(tb[i]) + ' x ' + id(tb[j])); }
          const touch = Array.from(D.querySelectorAll('button, a[href], .switch, label.leg, input[type=text]')).filter(vis).filter(e => !e.closest('.hint, .statusbar'));
          const small = touch.filter(e => e.getBoundingClientRect().height < (P.touch || 32) - 0.5).map(id);
          const noName = Array.from(D.querySelectorAll('button, a[href], input, select, canvas[tabindex]')).filter(vis).filter(e => !((e.getAttribute('aria-label') || '').trim() || (e.textContent || '').trim() || (e.labels && e.labels.length && e.labels[0].textContent.trim()) || (e.getAttribute('aria-labelledby') || '').trim())).map(id);
          hscroll = hscroll || small.length > 0 || noName.length > 0;
          if (small.length) overlapList.push('small:' + small.slice(0, 8).join(','));
          if (noName.length) overlapList.push('noname:' + noName.slice(0, 3).join(','));
          const tbx = D.querySelector('.toolbar'); toolbarH = tbx && vis(tbx) ? Math.round(tbx.getBoundingClientRect().height) : 0;
          const conts = Array.from(D.querySelectorAll('.scards, .tiles')).filter(vis);
          for (const c of conts) {
            const cr = c.getBoundingClientRect();
            for (const ch of Array.from(c.children).filter(vis)) {
              if (ch.getBoundingClientRect().right > cr.right + 1) { widthSw = false; break; }
            }
          }
        }
        const ok = !hscroll && fullWidth && overlaps === 0 && cols.every(c => c.ok) && (chart ? chart.ok : true) && widthSw;
        results.push({ path, width, theme, fs: P.fs || 100, toolbarH, hscroll, fullWidth, contentW, overlaps, overlapList, cols, chart, ok });
        f.remove();
      }
    }
  }
  const failed = results.filter(r => !r.ok);
  return JSON.stringify({
    total: results.length,
    failed: failed.length,
    results: results.map(r => r.ok ? { path: r.path, width: r.width, theme: r.theme } : r)
  });
})()
