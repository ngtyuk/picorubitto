/* Run against real Canvas implementations, keeping the old renderer external. */
(async function () {
  'use strict';
  const output = document.getElementById('results');
  const previews = document.getElementById('previews');
  const baselineUrl = new URL('../../build/tests/canvas-baseline.js', location.href);
  const currentUrl = new URL('../../emulator/emulator.js', location.href);
  const background = new Uint8Array(320 * 240 * 2);
  for (let i = 0; i < background.length; i += 2) {
    background[i] = 0x21;
    background[i + 1] = 0x04;
  }
  const backgroundUrl = URL.createObjectURL(new Blob([background]));
  const lines = [];
  let passed = 0;
  function log(message) { lines.push(message); output.textContent = lines.join('\n'); }
  function check(value, message) { if (!value) throw new Error(message); }
  function equalPixels(a, b, name) {
    check(a.length === b.length && a.every((value, index) => value === b[index]), `${name}: pixels differ`);
  }
  async function fixture(scriptUrl, width = 160, height = 120, viewport = [0, 0, 320, 240]) {
    const frame = document.createElement('iframe');
    frame.title = scriptUrl === baselineUrl ? '旧版' : '更新版';
    previews.appendChild(frame);
    const doc = frame.contentDocument;
    doc.open();
    doc.write('<!doctype html><canvas id="screen" width="320" height="240"></canvas><span id="runtime-status"></span><span id="fps-status"></span>');
    doc.close();
    const win = frame.contentWindow;
    win.GAME_ASSET_BASE = new URL('../portable/assets/', location.href).href;
    win.GAME_ASSETS = ['texture.raw', 'font.pbf', 'background.raw'];
    win.GAME_ASSET_URLS = { 'background.raw': backgroundUrl };
    const canvas = doc.getElementById('screen');
    const context = canvas.getContext('2d', { alpha: false });
    const originals = {};
    const counts = { get: 0, put: 0, getMs: 0, putMs: 0 };
    for (const [method, counter] of [['getImageData', 'get'], ['putImageData', 'put']]) {
      const original = context[method].bind(context);
      originals[method] = original;
      context[method] = (...args) => {
        const start = performance.now();
        const result = original(...args);
        counts[counter]++;
        counts[`${counter}Ms`] += performance.now() - start;
        return result;
      };
    }
    const readPixels = context.getImageData.bind(context);
    const script = doc.createElement('script');
    script.src = `${scriptUrl.href}?test=${Date.now()}`;
    await new Promise((resolve, reject) => {
      script.onload = resolve;
      script.onerror = () => reject(new Error(`Cannot load ${scriptUrl.pathname}; see docs/compatibility-testing.md`));
      doc.body.appendChild(script);
    });
    const g = win.GameEmulator;
    await g.ready;
    check(g.set3DResolution(width, height), 'resolution setup');
    check(g.set3DViewport(...viewport), 'viewport setup');
    g.set3DCamera(0, 0, 0, 0, 0, 70, 0.1, 100);
    const sprite = g.loadSprite('texture.raw', 32, 32, 1, 0xf81f);
    const texture = g.loadTexture('texture.raw', 32, 32, 0xf81f);
    const font = g.loadFont('font.pbf');
    return {
      g, sprite, texture, font, counts,
      disableCounters() { Object.assign(context, originals); },
      pixels() {
        // Exclude the test's own readback from renderer counters.
        const saved = { ...counts };
        const data = readPixels(0, 0, 320, 240).data;
        Object.assign(counts, saved);
        return data;
      },
      resetCounts() { Object.assign(counts, { get: 0, put: 0, getMs: 0, putMs: 0 }); },
      close() { frame.remove(); }
    };
  }
  const vertices = [-1, -0.7, 4, 1, -0.7, 4, 0, 1, 4];
  const edges = [0, 1, 1, 2, 2, 0];
  function wire(g, color = 0xf800, depth = false) { g.draw3DWireframe(vertices, edges, color, depth); }
  function triangle(g, color = 0x001f) { g.draw3DTriangle(...vertices, color); }
  function start(g) { g.clear(0x2104); g.threeDBeginFrame(); }
  async function compare(name, render, expectedPuts, options = []) {
    const old = await fixture(baselineUrl, ...options);
    const current = await fixture(currentUrl, ...options);
    try {
      render(old); render(current);
      equalPixels(old.pixels(), current.pixels(), name);
      if (expectedPuts !== null) check(current.counts.put === expectedPuts, `${name}: put=${current.counts.put}, expected ${expectedPuts}`);
      log(`PASS ${name}: put ${old.counts.put} → ${current.counts.put}, get ${old.counts.get} → ${current.counts.get}`);
      passed++;
    } finally { old.close(); current.close(); }
  }
  try {
    for (const [name, options] of [
      ['160×120 / 2x', []], ['320×240 / 1x', [320, 240]],
      ['small viewport / fractional scale', [97, 73, [31, 19, 181, 133]]]
    ]) {
      await compare(`all 3D APIs, ${name}`, ({ g }) => {
        start(g); triangle(g);
        g.draw3DTriangles([...vertices, 0x07e0]);
        g.draw3DIndexedMesh(vertices, [0, 1, 2], [0xffff]);
        wire(g); wire(g, 0x07ff, true); g.present(); g.present();
      }, 1, options);
    }
    await compare('2D background → 8 wire batches → HUD', ({ g }) => {
      start(g);
      for (let i = 0; i < 8; i++) wire(g, i % 2 ? 0xf800 : 0xffff);
      g.drawText(8, 8, 'HUD', 0xffe0); g.present();
    }, 1);
    await compare('fully depth-occluded draw', ({ g }) => {
      start(g); triangle(g); triangle(g, 0x07e0); g.present();
    }, 1);
    await compare('single triangle', ({ g }) => { start(g); triangle(g); g.present(); }, 1);
    await compare('empty and fully culled batches', ({ g }) => {
      start(g); g.draw3DTriangles([]); g.draw3DIndexedMesh([], [], []);
      g.draw3DWireframe([], [], 0xffff);
      g.draw3DWireframe([-100, 0, 4, -90, 0, 4], [0, 1], 0xffff);
      g.draw3DWireframe([0, 0, -2, 1, 0, -2], [0, 1], 0xffff);
      g.draw3DTriangle(100, 0, 4, 101, 0, 4, 100, 1, 4, 0xffff);
      g.present();
    }, 0);
    await compare('rejected resolution/viewport changes preserve pending pixels', ({ g }) => {
      start(g); wire(g);
      check(g.set3DResolution(80, 60) === false, 'mid-frame resolution rejected');
      check(g.set3DViewport(10, 10, 100, 100) === false, 'mid-frame viewport rejected');
      g.present();
    }, 1);
    await compare('pending 3D superseded by clear', ({ g }) => {
      start(g); wire(g); g.clear(0x07e0); g.present();
    }, 0);
    await compare('background restoration', ({ g }) => {
      g.setBackground('background.raw'); g.threeDBeginFrame(); wire(g);
      g.beginFrame(); g.present();
    }, 2);
    await compare('same background is a no-op', ({ g }) => {
      g.setBackground('background.raw'); g.threeDBeginFrame(); wire(g);
      g.setBackground('background.raw'); g.present();
    }, 2);
    await compare('new 3D frame flushes pending output and resets depth', ({ g }) => {
      start(g); triangle(g); g.threeDBeginFrame(); wire(g, 0xffff, true); g.present();
    }, 2);
    await compare('restart synchronization', ({ g }) => {
      start(g); triangle(g); g.resetTiming(); g.present();
    }, 1);
    await compare('consecutive frames', ({ g }) => {
      for (let i = 0; i < 3; i++) { start(g); wire(g, 0x001f + i); g.present(); }
    }, 3);
    await compare('2D-only frames', ({ g }) => {
      g.clear(0xf800); g.fillRect(2, 3, 20, 20, 0x07e0); g.drawText(8, 8, '2D', 0xffff); g.present();
    }, 0);
    const boundaries = [
      ['rectangle', f => f.g.fillRect(0, 0, 8, 8, 0x07e0)],
      ['alpha rectangle', f => f.g.fillRect(0, 0, 8, 8, 0x07e0, 127)],
      ['circle', f => f.g.drawCircle(4, 4, 3, 0x07e0)],
      ['line', f => f.g.drawLine(1, 1, 7, 7, 2, 0x07e0)],
      ['text', f => f.g.drawText(0, 0, 'A', 0x07e0)],
      ['PBF font', f => f.g.drawFont(f.font, 0, 0, 'FONT', 12, 1, 0x07e0)],
      ['sprite', f => f.g.drawSprite(f.sprite, 0, 0, 0)],
      ['sprite batch', f => f.g.drawSprites(f.sprite, [0, 0, 0, 16, 16, 0])],
      ['texture', f => f.g.drawTexture(f.texture, 0, 0, 0, 0, 32, 32, 40, 30)],
      ['RGB565', f => f.g.blitAsset('texture.raw', 0, 0, 32, 32)],
      ['RGB565 region', f => f.g.blitAssetRegion('texture.raw', 0, 0, 12, 12, 32, 2, 3)]
    ];
    for (const [name, draw2D] of boundaries) {
      // Compare against independently rendered 2D pixels in a region untouched by 3D.
      const reference = await fixture(baselineUrl);
      const current = await fixture(currentUrl);
      try {
        reference.g.clear(0x2104); draw2D(reference); reference.g.present();
        start(current.g); triangle(current.g); draw2D(current); wire(current.g); current.g.present();
        const a = reference.pixels(), b = current.pixels();
        for (let y = 0; y < 40; y++) {
          for (let x = 0; x < 60; x++) {
            const offset = (y * 320 + x) * 4;
            for (let c = 0; c < 4; c++) check(a[offset + c] === b[offset + c], `${name}: 2D lost at ${x},${y}`);
          }
        }
        check(current.counts.get === 2, `${name}: next 3D run must refresh Canvas pixels`);
        log(`PASS 3D → ${name} → 3D (preserves 2D pixels)`); passed++;
      } finally { reference.close(); current.close(); }
    }
    const resized = await fixture(currentUrl);
    try {
      for (const width of [160, 320, 160]) {
        const height = width === 160 ? 120 : 240;
        check(resized.g.set3DResolution(width, height), 'resolution between frames');
        check(resized.g.set3DViewport(0, 0, 320, 240), 'viewport between frames');
        const reference = await fixture(baselineUrl, width, height);
        try {
          for (const f of [reference, resized]) { start(f.g); wire(f.g); f.g.present(); }
          equalPixels(reference.pixels(), resized.pixels(), 'repeated resolution changes');
        } finally { reference.close(); }
      }
      log('PASS resolution changes 160x120 → 320x240 → 160x120 between frames'); passed++;
    } finally { resized.close(); }
    log(`\n${passed} cases passed. Rendering benchmark (no Ruby VM / frame pacing):`);
    log(navigator.userAgent);
    for (const [kind, batches] of [['wire', 1], ['wire', 8], ['triangle', 1]]) {
      for (const [name, url] of [['old', baselineUrl], ['new', currentUrl]]) {
        const f = await fixture(url);
        const draw = () => {
          start(f.g);
          for (let i = 0; i < batches; i++) {
            if (kind === 'wire') wire(f.g, i % 2 ? 0xffff : 0xf800);
            else triangle(f.g);
          }
          f.g.drawText(8, 8, 'HUD', 0xffe0); f.g.present();
        };
        for (let i = 0; i < 40; i++) draw();
        f.resetCounts(); draw();
        log(`${name} ${kind} batches=${batches}: get=${f.counts.get} put=${f.counts.put}`);
        f.disableCounters();
        for (let run = 0; run < 3; run++) {
          const times = [];
          const runStart = performance.now();
          for (let i = 0; i < 1800; i++) {
            const started = performance.now(); draw(); times.push(performance.now() - started);
          }
          const mean = (performance.now() - runStart) / 1800;
          times.sort((a, b) => a - b);
          log(`${name} ${kind} batches=${batches} run=${run + 1}: mean=${mean.toFixed(4)}ms median=${times[900].toFixed(3)}ms p95=${times[1710].toFixed(3)}ms max=${times[1799].toFixed(3)}ms (Canvas counters disabled)`);
          await new Promise(resolve => requestAnimationFrame(resolve));
        }
        f.close();
      }
    }
    log('\nPASS'); document.title = `${passed} PASS · Canvas描画`;
  } finally { URL.revokeObjectURL(backgroundUrl); }
})().catch(error => {
  document.getElementById('results').textContent += `\nFAIL: ${error.message}`;
  document.title = 'FAIL · Canvas描画'; console.error(error);
});
