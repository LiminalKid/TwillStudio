// Twill Studio's site: the theme switch, the logo weaving itself, the playhead in the hero, the
// tabs following the scroll, the controls sheet, and keyboard shortcuts. No libraries.
(function () {
  'use strict';
  var root = document.documentElement;
  var reduceMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
  function cssVar(name) { return getComputedStyle(root).getPropertyValue(name).trim(); }

  // ---- Theme: paper or graphite, as in the app's settings ----

  var themeButtons = document.querySelectorAll('[data-set-theme]');
  function setTheme(t, remember) {
    root.setAttribute('data-theme', t);
    if (remember) { try { localStorage.setItem('twill-theme', t); } catch (e) {} }
    themeButtons.forEach(function (b) { b.setAttribute('aria-pressed', String(b.dataset.setTheme === t)); });
    drawMarks();
    drawWeave(weaveT);
  }
  themeButtons.forEach(function (b) {
    b.addEventListener('click', function () { setTheme(b.dataset.setTheme, true); });
  });
  function toggleTheme() { setTheme(root.getAttribute('data-theme') === 'dark' ? 'light' : 'dark', true); }

  // ---- The logo (logo.js, from tools/make-logo.py) ----

  var LOGO = window.TWILL_LOGO || [];
  var LW = LOGO.length ? LOGO[0].length : 0, LH = LOGO.length;
  var SWATCH_ROWS = 62;  // the swatch's rows; the words are below
  function colourOf(c) {
    if (c === '2') return cssVar('--logo-thread');
    if (c === '3') return '#E58A55';
    if (c === '4') return '#7DAE88';
    return null;  // 0 and 1: nothing (the swatch's gaps show the page)
  }

  // The little mark in the top bar and footer: the swatch alone, one pixel per thread cell.
  function drawMarks() {
    if (!LW) return;
    var x0 = 13, size = 29;  // the swatch starts 13 pixels in, in 2 x 2 cells
    document.querySelectorAll('canvas.mark').forEach(function (cv) {
      var g = cv.getContext('2d');
      g.clearRect(0, 0, size, size);
      for (var y = 0; y < size; y++) for (var x = 0; x < size; x++) {
        var col = colourOf(LOGO[y * 2][x0 + x * 2]);
        if (col) { g.fillStyle = col; g.fillRect(x, y, 1, 1); }
      }
    });
  }

  // The big logo in the hero weaves itself: the warp threads come down, the weft runs across
  // them (over two, under two, the pixels already say which), then the words, then the stitch.
  var weave = document.querySelector('canvas.weave');
  var weaveScale = 1;
  var weaveT = reduceMotion ? 99 : 0;  // how far the weaving has got, in seconds
  function appearAt(x, y, c) {
    if (y < SWATCH_ROWS) {
      if (c === '2') return 0.1 + (x / LW) * 0.5 + (y / SWATCH_ROWS) * 0.6;   // warp
      return 1.0 + (y / SWATCH_ROWS) * 0.5 + (x / LW) * 0.6;                  // weft
    }
    if (c === '3') return 2.7 + (x / LW) * 0.5;                              // the stitch
    if (y < 92) return 1.9 + (x / LW) * 0.7;                                 // TWILL
    return 2.4 + (x / LW) * 0.5;                                             // STUDIO
  }
  function drawWeave(t) {  // t in seconds since the start; large t draws it whole
    if (!weave || !LW) return;
    var dpr = window.devicePixelRatio || 1;
    var cssW = weave.clientWidth || 168;
    weaveScale = Math.max(1, Math.round(cssW / LW * dpr));
    var s = weaveScale;
    if (weave.width !== LW * s) { weave.width = LW * s; weave.height = LH * s; }
    var g = weave.getContext('2d');
    g.clearRect(0, 0, weave.width, weave.height);
    var fade = 0.18;
    for (var y = 0; y < LH; y++) {
      var row = LOGO[y];
      for (var x = 0; x < LW; x++) {
        var c = row[x];
        var col = colourOf(c);
        if (!col) continue;
        var a = t >= 99 ? 1 : Math.min(1, Math.max(0, (t - appearAt(x, y, c)) / fade));
        if (a <= 0) continue;
        g.globalAlpha = a;
        g.fillStyle = col;
        g.fillRect(x * s, y * s, s, s);
      }
    }
    g.globalAlpha = 1;
  }
  function animateWeave() {
    if (!weave) return;
    if (reduceMotion) { drawWeave(99); return; }
    var start = performance.now();
    (function step(now) {
      weaveT = (now - start) / 1000;
      if (weaveT >= 3.6) weaveT = 99;
      drawWeave(weaveT);
      if (weaveT < 99) requestAnimationFrame(step);
    })(start);
  }

  // ---- The playhead in the hero, and the transport readout in the top bar ----

  // In the screenshot (1024 x 768): bar 1 starts at x = 256 and a bar is 128 pixels wide; the
  // demo song is at 100 BPM in 4/4.
  var playhead = document.querySelector('.playhead');
  var readout = document.querySelector('.transport .pos');
  var START = 256 / 1024 * 100, BAR = 128 / 1024 * 100, BARS = (100 - START) / BAR;
  var BEATS_PER_SECOND = 100 / 60;
  function runPlayhead() {
    if (!playhead) return;
    if (reduceMotion) { if (readout) readout.textContent = '1.1'; return; }
    var start = performance.now(), lastText = '';
    (function step(now) {
      var beats = ((now - start) / 1000 * BEATS_PER_SECOND) % (BARS * 4);
      playhead.style.left = (START + beats / 4 * BAR) + '%';
      var text = (Math.floor(beats / 4) + 1) + '.' + (Math.floor(beats % 4) + 1);
      if (readout && text !== lastText) { readout.textContent = text; lastText = text; }
      requestAnimationFrame(step);
    })(start);
  }

  // ---- Tabs follow the scroll; screenshots get their brackets as they come into view ----

  var tabs = Array.prototype.slice.call(document.querySelectorAll('.tabs a'));
  var sections = tabs.map(function (a) { return document.querySelector(a.getAttribute('href')); });
  function updateTabs() {
    var y = window.scrollY + window.innerHeight * 0.35, current = -1;
    sections.forEach(function (s, i) { if (s && s.offsetTop <= y) current = i; });
    // Past the controls, GET IT stays lit through status and questions.
    tabs.forEach(function (a, i) { a.classList.toggle('active', i === current); });
  }
  window.addEventListener('scroll', updateTabs, { passive: true });
  window.addEventListener('resize', updateTabs);

  if ('IntersectionObserver' in window) {
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) { if (e.isIntersecting) { e.target.classList.add('seen'); io.unobserve(e.target); } });
    }, { threshold: 0.4 });
    document.querySelectorAll('.shot').forEach(function (el) { io.observe(el); });
  } else {
    document.querySelectorAll('.shot').forEach(function (el) { el.classList.add('seen'); });
  }

  // ---- The controls sheet: the app's Controls panel (src/app/App.cpp), on a handheld or a keyboard ----

  var CONTROLS = [
    ['EVERYWHERE', [
      ['Start', 'play / stop'], ['L1 or R1 + Start', 'play from cursor'], ['L2 / R2', 'prev / next view'],
      ['Sel + Left / Right', 'prev / next view'], ['Sel + B / A', 'undo / redo'], ['Sel + Y / R1', 'copy / paste'],
      ['Sel + X, or Menu', 'settings'], ['Menu + Start', 'quit']]],
    ['ARRANGEMENT', [
      ['A', 'new clip / select'], ['A + Left / Right', 'clip length'], ['B', 'delete clip'],
      ['B + Left / Right', 'move clip'], ['Y', 'duplicate clip'], ['X', 'open clip / panel'],
      ['L1 or R1 + D-pad', 'zoom / page']]],
    ['PIANO ROLL', [
      ['A', 'add / select note'], ['A + Left / Right', 'length'], ['A + Up / Down', 'pitch'], ['B', 'delete note'],
      ['B + D-pad', 'move notes'], ['A or B + L1 + L / R', 'fine'], ['A or B + R1 + U / D', 'octave'],
      ['Y + D-pad', 'select area'], ['Y + A', 'select all'], ['X', 'note / clip panel'],
      ['L1 or R1 + D-pad', 'grid / page'], ['Sel + Up / Down', 'notes / lane']]],
    ['SOUND', [
      ['Up / Down', 'choose'], ['Left / Right', 'change'], ['A', 'browse'], ['B', 'reset'], ['Y', 'listen'],
      ['X', 'track settings'], ['Sel + Up / Down', 'track']]],
    ['MIXER', [['A + D-pad', 'change'], ['B', 'reset'], ['Y', 'listen'], ['X', 'effects']]],
    ['PANELS', [['R1 + Left / Right', 'bigger steps'], ['B or X', 'close']]]
  ];
  // The desktop and browser keys for each button (Sdl2Platform.cpp, kKeys).
  var KEYBOARD = {
    'A': 'X', 'B': 'Z', 'X': 'S', 'Y': 'A', 'L1': 'Q', 'R1': 'W', 'L2': '1', 'R2': '2',
    'Start': 'Enter', 'Sel': 'Tab', 'Menu': 'Esc', 'D-pad': 'Arrows',
    'Left': '←', 'Right': '→', 'Up': '↑', 'Down': '↓', 'L': '←', 'R': '→', 'U': '↑', 'D': '↓'
  };
  var keyMode = 'pad';
  function label(b) {
    if (keyMode === 'keyboard') return KEYBOARD[b] || b;
    return { Sel: 'SEL', 'D-pad': 'D-PAD', L: 'LEFT', R: 'RIGHT', U: 'UP', D: 'DOWN' }[b] || b.toUpperCase();
  }
  // "A or B + L1 + L / R" -> [A] or [B] + [L1] + [LEFT / RIGHT]
  function renderKeys(text) {
    var out = document.createElement('span');
    out.className = 'keys';
    text.split(' + ').forEach(function (group, gi) {
      if (gi) out.appendChild(document.createTextNode('+'));
      group.split(/,? or /).forEach(function (alt, ai) {
        if (ai) out.appendChild(document.createTextNode('or'));
        var cap = document.createElement('span');
        cap.className = 'cap';
        cap.textContent = alt.split(' / ').map(label).join(' / ');
        out.appendChild(cap);
      });
    });
    return out;
  }
  var sheet = document.getElementById('control-sheet');
  function renderControls() {
    if (!sheet) return;
    sheet.textContent = '';
    CONTROLS.forEach(function (group) {
      var g = document.createElement('div');
      g.className = 'group';
      var h = document.createElement('h3');
      h.textContent = group[0];
      g.appendChild(h);
      group[1].forEach(function (r) {
        var row = document.createElement('div');
        row.className = 'row';
        row.appendChild(renderKeys(r[0]));
        var act = document.createElement('span');
        act.className = 'act';
        act.textContent = r[1];
        row.appendChild(act);
        g.appendChild(row);
      });
      sheet.appendChild(g);
    });
  }
  document.querySelectorAll('[data-keys]').forEach(function (b) {
    b.addEventListener('click', function () {
      keyMode = b.dataset.keys;
      document.querySelectorAll('[data-keys]').forEach(function (o) { o.setAttribute('aria-pressed', String(o === b)); });
      renderControls();
    });
  });

  // ---- Keys: Q / W for the previous and next section, X to play, T for the theme ----

  var stops = ['top', 'arrange', 'roll', 'sound', 'mix', 'controls', 'play', 'get', 'status', 'faq']
    .map(function (id) { return document.getElementById(id); }).filter(Boolean);
  function go(dir) {
    var y = window.scrollY + 80, i = 0;
    stops.forEach(function (s, k) { if (s.offsetTop <= y + 2) i = k; });
    var next = stops[Math.max(0, Math.min(stops.length - 1, i + dir))];
    next.scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth' });
  }
  document.addEventListener('keydown', function (e) {
    if (e.metaKey || e.ctrlKey || e.altKey || e.defaultPrevented) return;
    var tag = (e.target && e.target.tagName) || '';
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;
    var k = e.key.toLowerCase();
    if (k === 'q') go(-1);
    else if (k === 'w') go(1);
    else if (k === 'x') location.href = 'play/';
    else if (k === 't') toggleTheme();
  });

  // ---- Start ----

  setTheme(root.getAttribute('data-theme') || 'light', false);
  renderControls();
  updateTabs();
  animateWeave();
  runPlayhead();
  window.addEventListener('resize', function () { drawWeave(weaveT); });
})();
