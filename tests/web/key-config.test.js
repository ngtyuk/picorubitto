'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const createKeys = require('../../studio/key-config.js');

function makeStorage(initial = {}) {
  const data = new Map(Object.entries(initial));
  return { getItem: (key) => data.get(key) ?? null, setItem: (key, value) => data.set(key, value) };
}

test('defaults preserve aliases and key matching ignores letter case', () => {
  const keys = createKeys(makeStorage());
  assert.equal(keys.buttonForKey('Z'), 'A');
  assert.equal(keys.buttonForKey('Enter'), 'A');
  assert.equal(keys.buttonForKey(' '), 'A');
  assert.equal(keys.buttonForKey('ArrowLeft'), 'LEFT');
});

test('assigning an existing key swaps actions and survives reload', () => {
  const storage = makeStorage();
  const keys = createKeys(storage);
  keys.assign('A', 'X');
  assert.equal(keys.buttonForKey('x'), 'A');
  assert.equal(keys.buttonForKey('z'), 'B');
  assert.equal(keys.buttonForKey('Enter'), 'B');
  const restored = createKeys(storage);
  assert.equal(restored.buttonForKey('X'), 'A');
  assert.equal(restored.buttonForKey('z'), 'B');
  restored.reset();
  assert.equal(restored.buttonForKey('z'), 'A');
  assert.equal(createKeys(storage).buttonForKey('x'), 'B');
});

test('invalid stored data and reserved keys cannot break defaults', () => {
  const storage = makeStorage({ 'picorubitto-studio-keys-v1': '{"A":["x"]}' });
  const keys = createKeys(storage);
  assert.equal(keys.buttonForKey('z'), 'A');
  assert.equal(keys.assign('A', 'Escape'), false);
  assert.equal(keys.assign('A', 'Tab'), false);
  assert.equal(keys.buttonForKey('z'), 'A');
});

test('a storage failure leaves the active mapping unchanged', () => {
  const keys = createKeys({ getItem: () => null, setItem: () => { throw new Error('blocked'); } });
  assert.throws(() => keys.assign('A', 'k'));
  assert.equal(keys.buttonForKey('z'), 'A');
  assert.equal(keys.buttonForKey('k'), undefined);
});
