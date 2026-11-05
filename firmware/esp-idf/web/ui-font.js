const FS_KEY = 'af.fs';
function applyFont(v) {
  const pct = AFU.fontPct(v);
  document.documentElement.style.setProperty('--fs', String(AFU.fontScale(pct)));
  const btns = document.querySelectorAll('#segFont button');
  btns.forEach(b => {
    b.setAttribute('aria-pressed', String(b.getAttribute('data-fs') === String(pct)));
  });
  return pct;
}
function readFont() {
  try {
    return localStorage.getItem(FS_KEY);
  } catch (e) {
    return null;
  }
}
function saveFont(v) {
  const pct = AFU.fontPct(v);
  try {
    if (pct === 100) {
      localStorage.removeItem(FS_KEY);
    } else {
      localStorage.setItem(FS_KEY, String(pct));
    }
  } catch (e) {}
  return pct;
}
function resetFont() {
  try {
    localStorage.removeItem(FS_KEY);
  } catch (e) {}
  return applyFont(100);
}
function initFontUi() {
  applyFont(readFont());
  const btns = document.querySelectorAll('#segFont button');
  if (btns) {
    btns.forEach(b => {
      b.addEventListener('click', () => {
        applyFont(saveFont(b.getAttribute('data-fs')));
      });
    });
  }
  const btnReset = document.getElementById('btnFontReset');
  if (btnReset) {
    btnReset.addEventListener('click', () => {
      resetFont();
    });
  }
}
