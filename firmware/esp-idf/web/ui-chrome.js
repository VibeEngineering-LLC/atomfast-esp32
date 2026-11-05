(function () { 'use strict';
  var navLinks = document.querySelectorAll('nav.tabs a[data-nav]');
  var page = AFU.pageOf(location.pathname);
  for (var i = 0; i < navLinks.length; i++) {
    var a = navLinks[i];
    if (a.getAttribute('data-nav') === page) {
      a.setAttribute('aria-current', 'page');
    } else {
      a.removeAttribute('aria-current');
    }
  }

  function apply(t) {
    document.documentElement.setAttribute('data-theme', t);
  }

  function current() {
    var t = document.documentElement.getAttribute('data-theme');
    if (t === 'light' || t === 'dark') return t;
    if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) return 'dark';
    return 'light';
  }

  var urlTheme = null;
  try {
    urlTheme = new URLSearchParams(location.search).get('theme');
  } catch (e) {}

  if (urlTheme === 'light' || urlTheme === 'dark') {
    apply(urlTheme);
  } else {
    var stored = null;
    try {
      stored = localStorage.getItem('af.theme');
    } catch (e) {}
    if (stored === 'light' || stored === 'dark') {
      apply(stored);
    }
  }

  var themeBtn = document.getElementById('themeBtn');
  if (themeBtn) {
    function label() {
      themeBtn.textContent = current() === 'dark' ? '\u25D0 \u0442\u0451\u043C\u043D\u0430\u044F' : '\u25D0 \u0441\u0432\u0435\u0442\u043B\u0430\u044F';
    }
    label();
    themeBtn.addEventListener('click', function () {
      var next = current() === 'dark' ? 'light' : 'dark';
      apply(next);
      try {
        localStorage.setItem('af.theme', next);
      } catch (e) {}
      label();
    });
    try {
      if (window.matchMedia) {
        var mq = window.matchMedia('(prefers-color-scheme: dark)');
        if (mq.addEventListener) {
          mq.addEventListener('change', label);
        }
      }
    } catch (e) {}
  }
})();
