'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');

function fixture() {
  const drawn = [];
  const ctx = { fillStyle: '', fillRect(x, y, width, height) { drawn.push([x, y, width, height, this.fillStyle]); } };
  const buttons = [];
  let reloads = 0;
  const status = { textContent: '', classList: { add(value) { assert.equal(value, 'error'); } } };
  const row = { appendChild(button) { buttons.push(button); } };
  const elements = { screen: { getContext() { return ctx; } }, 'runtime-status': status };
  const document = {
    getElementById(id) { return id === 'system-retry' ? buttons[0] : elements[id] || null; },
    querySelector() { return row; },
    createElement() { return { addEventListener(_name, fn) { this.click = fn; } }; }
  };
  const sandbox = { window: {}, document,
    location: { reload() { reloads++; } }, console };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../../emulator/system-screen.js'), 'utf8'), sandbox);
  return { screen: sandbox.window.GameSystemScreen, drawn, buttons, status, reloads: () => reloads };
}

test('error screen works before assets or VM and retry reloads', () => {
  const f = fixture();
  assert.equal(f.drawn.length, 0);
  assert.equal(f.screen.hasError(), false);
  f.screen.error('LOAD');
  assert.deepEqual(f.drawn[0].slice(0, 4), [0, 0, 320, 240]);
  assert.equal(f.drawn[0][4], '#521021');
  assert.equal(f.drawn[1][4], '#ad1831');
  assert.equal(f.status.textContent, 'エラー');
  assert.equal(f.screen.hasError(), true);
  assert.equal(f.buttons.length, 1);
  assert.equal(f.buttons[0].hidden, false);
  const count = f.drawn.length;
  f.screen.error('GAME');
  assert.equal(f.drawn.length, count);
  f.buttons[0].click();
  assert.equal(f.reloads(), 1);
});

test('system screen uses the same built-in ASCII glyphs as the emulator', () => {
  function glyphs(file) {
    const source = fs.readFileSync(path.join(__dirname, '../../emulator', file), 'utf8');
    const match = source.match(/const asciiGlyphs = \[([\s\S]*?)\n  \];/);
    assert.ok(match, `${file} ASCII font exists`);
    return JSON.parse(JSON.stringify(vm.runInNewContext(`[${match[1]}\n]`)));
  }
  assert.deepEqual(glyphs('system-screen.js'), glyphs('emulator.js'));
});
