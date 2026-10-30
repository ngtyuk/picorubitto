/* Real Canvas comparison; prepare the old script and fixtures as documented. */
(async function () {
  'use strict';
  const lines = [];
  const results = document.getElementById('results');
  function log(message) { lines.push(message); results.textContent = lines.join('\n'); }
  function check(value, message) { if (!value) throw new Error(message); }
  function largeFont(width, height, alpha) {
    const bytes = new Uint8Array(56 + width * height);
    bytes.set([80, 66, 70, 49]);
    const view = new DataView(bytes.buffer);
    for (const [at, value] of [[4, 63], [6, 1], [8, 1], [16, 12], [18, 12], [24, 1],
      [36, width], [38, height], [44, width]]) view.setUint16(at, value);
    for (const [at, value] of [[12, bytes.length], [28, 36], [32, 56], [48, 56], [52, width * height]]) view.setUint32(at, value);
    bytes.fill(alpha, 56);
    return URL.createObjectURL(new Blob([bytes]));
  }
  const largeUrl = largeFont(100, 100, 255);
  const oversizeUrl = largeFont(400, 800, 128);
  async function fixture(url, current) {
    const frame = document.createElement('iframe');
    document.getElementById('previews').appendChild(frame);
    const doc = frame.contentDocument;
    doc.open();
    doc.write('<!doctype html><canvas id="screen" width="320" height="240"></canvas><span id="runtime-status"></span><span id="fps-status"></span>');
    doc.close();
    const win = frame.contentWindow;
    win.GAME_ASSET_BASE = new URL('../../build/tests/font-fixtures/', location.href).href;
    win.GAME_ASSETS = ['font-check.pbf', 'font-invalid.pbf', 'font-solid.pbf', 'font-large.pbf', 'font-oversize.pbf'];
    win.GAME_ASSET_URLS = {
      'font-solid.pbf': new URL('../portable/assets/font.pbf', location.href).href,
      'font-large.pbf': largeUrl, 'font-oversize.pbf': oversizeUrl
    };
    const ctx = doc.getElementById('screen').getContext('2d', { alpha: false });
    const calls = { fill: 0, draw: 0, layers: 0 };
    const originals = {};
    for (const [name, key] of [['fillRect', 'fill'], ['drawImage', 'draw']]) {
      const original = ctx[name].bind(ctx);
      originals[name] = original;
      ctx[name] = (...args) => { calls[key]++; return original(...args); };
    }
    const create = doc.createElement.bind(doc);
    doc.createElement = (...args) => {
      if (args[0] === 'canvas') calls.layers++;
      return create(...args);
    };
    let source = await (await fetch(url, { cache: 'no-store' })).text();
    if (current) {
      check(source.includes('const fontImages = new Map();'), 'cache instrumentation anchor');
      source = source.replace('window.GameEmulator =',
        'window.fontCacheStats = () => ({entries: fontImages.size, bytes: fontImageBytes}); window.GameEmulator =');
    }
    const script = create('script');
    script.textContent = source;
    doc.body.appendChild(script);
    const g = win.GameEmulator;
    await g.ready;
    const font = g.loadFont('font-check.pbf');
    const solidFont = g.loadFont('font-solid.pbf');
    const large = g.loadFont('font-large.pbf');
    const oversize = g.loadFont('font-oversize.pbf');
    return { frame, win, g, font, solidFont, large, oversize, calls,
      disableCounters() { Object.assign(ctx, originals); doc.createElement = create; },
      reset() { calls.fill = calls.draw = calls.layers = 0; },
      pixels() { return ctx.getImageData(0, 0, 320, 240).data; },
      close() { frame.remove(); }
    };
  }
  function equalPixels(old, current, name) {
    const a = old.pixels(), b = current.pixels();
    let differences = 0, maximum = 0;
    for (let i = 0; i < a.length; i++) {
      if (a[i] !== b[i]) { differences++; maximum = Math.max(maximum, Math.abs(a[i] - b[i])); }
    }
    const first = a.findIndex((v, i) => v !== b[i]);
    check(differences === 0, `${name}: ${differences} channels differ, max=${maximum}, first=${first} ${a[first]}->${b[first]}`);
  }
  let old, current;
  let passed = 0;
  try {
    old = await fixture('../../build/tests/font-baseline/emulator.js', false);
    current = await fixture('../../emulator/emulator.js', true);
    const cases = [
      ['alpha and negative bearing', 'A B?~', 32, 32, 12, 1, 0x6b5a],
      ['second size', 'A B?~', 32, 60, 24, 1, 0xf81f],
      ['multiline and spacing', 'A B?\nB A', 70, 70, 24, -2, 0x001f],
      ['empty', '', 0, 0, 12, 0, 0xffff],
      ['left and top', 'A B?~', -3, -8, 24, 1, 0x07e0],
      ['right and bottom', 'A B?~', 315, 235, 24, 1, 0xf800],
      ['fully clipped', 'A B?', -300, -200, 24, 1, 0xffff],
      ['large coordinates', 'A B?', 2147483647, -2147483648, 24, 1, 0xffff],
      ['transparent space', '   ', 20, 20, 12, 0, 0xffff]
    ];
    function compare(name, draw) {
      old.g.clear(0x39e7); current.g.clear(0x39e7);
      draw(old); draw(current); equalPixels(old, current, name); passed++;
    }
    for (const [name, text, x, y, size, spacing, color] of cases) {
      compare(name, f => f.g.drawFont(f.font, x, y, text, size, spacing, color));
      log(`PASS ${name}`);
    }
    let random = 9041;
    const next = () => (random = (Math.imul(random, 1664525) + 1013904223) >>> 0);
    for (let i = 0; i < 150; i++) {
      const x = next() % 400 - 40, y = next() % 320 - 40;
      const spacing = next() % 9 - 4, color = next() & 0xffff;
      compare(`random ${i}`, f => f.g.drawFont(f.font, x, y, 'A B?~\nB A', i % 2 ? 12 : 24, spacing, color));
    }
    log('PASS 150 deterministic clipping/color cases');
    compare('opaque glyph image', f => f.g.drawFont(f.solidFont, 10, 20, 'FONT\nFONT', 12, -1, 0xf81f));
    compare('opaque glyph image clipping', f => f.g.drawFont(f.solidFont, -2, -4, 'FONT', 12, 0, 0x07e0));
    current.g.collectAssets(); current.reset();
    current.g.drawFont(current.solidFont, 10, 20, 'FFFF', 12, 0, 0xffff);
    check(current.calls.draw === 4 && current.calls.fill === 0 && current.calls.layers === 1, 'opaque image reuse');
    compare('oversized glyph fallback', f => f.g.drawFont(f.oversize, -200, -700, '?', 12, 0, 0x07ff));
    for (let color = 0; color < 40; color++) current.g.drawFont(current.large, 10, 20, '?', 12, 0, color);
    check(current.win.fontCacheStats().bytes <= 1024 * 1024, 'RGBA byte budget');
    compare('large glyph after byte eviction', f => f.g.drawFont(f.large, 10, 20, '?', 12, 0, 1));
    log('PASS opaque images, byte-budget eviction and oversized fallback');
    check(current.g.measureFont(current.font, 'A B?', 12, 1) === 24, 'measure');
    check(current.g.measureFont(current.font, 'A\nB', 12, 1) === 6, 'multiline spacing');
    check(current.g.measureFont(current.font, '', 12, 0) === 0, 'empty measure');
    for (const f of [old, current]) {
      let rejected = false;
      try { f.g.loadFont('font-invalid.pbf'); } catch (_) { rejected = true; }
      check(rejected, 'invalid glyph offset rejected');
      rejected = false;
      try { f.g.drawFont(f.font, 0, 0, 'A', 13, 0, 0xffff); } catch (_) { rejected = true; }
      check(rejected, 'missing size rejected');
    }
    log('PASS width, invalid PBF and missing size');
    current.g.collectAssets(); current.reset();
    current.g.drawFont(current.font, 20, 20, 'AAAA', 12, 0, 0xffff);
    check(current.calls.layers === 0 && current.calls.fill > 0, 'alpha glyph reuse');
    check(current.win.fontCacheStats().entries === 1, 'one repeated glyph');
    current.reset();
    current.g.drawFont(current.font, -300, -200, 'AAAA', 12, 0, 0x07e0);
    check(current.calls.draw === 0 && current.calls.layers === 0, 'clipping before allocation');
    for (let color = 0; color < 600; color++) current.g.drawFont(current.font, 20, 20, 'A', 24, 0, color);
    let stats = current.win.fontCacheStats();
    check(stats.entries <= 256 && stats.bytes <= 1024 * 1024, 'global cache bound');
    compare('eviction regeneration', f => f.g.drawFont(f.font, 20, 20, 'A B?~', 24, 1, 1));
    const other = current.g.loadFont('font-check.pbf');
    current.g.drawFont(other, 20, 20, 'B', 12, 0, 0xffff);
    const before = current.win.fontCacheStats().entries;
    current.g.releaseAssetHandle(other);
    check(current.win.fontCacheStats().entries === before - 1, 'handle GC evicts only its glyphs');
    current.g.collectAssets();
    check(current.win.fontCacheStats().entries === 0 && current.win.fontCacheStats().bytes === 0, 'explicit collection');
    compare('live font after collection', f => f.g.drawFont(f.font, 20, 20, 'A B?', 12, 1, 0x001f));
    log('PASS glyph reuse, cache eviction, handle GC, live-font regeneration');
    const drawCalls = [];
    for (const f of [old, current]) {
      f.reset();
      f.g.drawFont(f.font, 20, 20, 'A B?~', 24, 1, 0xffff);
      drawCalls.push({ ...f.calls });
      f.disableCounters();
      for (let i = 0; i < 100; i++) f.g.drawFont(f.font, 20, 20, 'A B?~', 24, 1, 0xffff);
    }
    for (let round = 0; round < 3; round++) {
      const times = [];
      for (const f of [old, current]) {
        f.g.drawFont(f.font, 20, 20, 'A B?~', 24, 1, 0xffff);
        f.reset();
        const start = performance.now();
        for (let i = 0; i < 1500; i++) f.g.drawFont(f.font, 20, 20, 'A B?~', 24, 1, 0xffff);
        times.push((performance.now() - start).toFixed(2));
      }
      log(`BENCH round=${round + 1} old_ms=${times[0]} current_ms=${times[1]} fill_per_call=${drawCalls[0].fill}->${drawCalls[1].fill} draw_per_call=${drawCalls[1].draw}`);
    }
    for (let round = 0; round < 3; round++) {
      const loads = [], cold = [];
      for (const f of [old, current]) {
        let start = performance.now();
        for (let i = 0; i < 1000; i++) f.g.releaseAssetHandle(f.g.loadFont('font-check.pbf'));
        loads.push((performance.now() - start).toFixed(2));
        start = performance.now();
        for (let i = 0; i < 300; i++) {
          f.g.collectAssets();
          f.g.drawFont(f.font, 20, 20, 'A B?~', 24, 1, i);
        }
        cold.push((performance.now() - start).toFixed(2));
      }
      log(`COLD round=${round + 1} load_1000_ms=${loads.join('->')} draw_300_ms=${cold.join('->')}`);
    }
    stats = current.win.fontCacheStats();
    log(`PASS ${passed} pixel comparisons; cache=${stats.entries} glyphs / ${stats.bytes} budgeted bytes`);
    window.fontTestResult = { passed: true, comparisons: passed, lines: lines };
  } catch (error) {
    log(`FAIL ${error.stack || error}`);
    window.fontTestResult = { passed: false, error: String(error), lines: lines };
  } finally {
    if (window.fontTestResult?.passed) {
      if (old) old.close();
      if (current) current.close();
    }
    URL.revokeObjectURL(largeUrl);
    URL.revokeObjectURL(oversizeUrl);
  }
}());
