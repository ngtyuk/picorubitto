(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory;
  else {
    let storage;
    try { storage = root.localStorage; } catch (_) { /* Storage can be unavailable. */ }
    root.StudioKeys = factory(storage);
  }
})(typeof globalThis === 'undefined' ? this : globalThis, function (storage) {
  'use strict';
  const storageKey = 'picorubitto-studio-keys-v1';
  const defaults = {
    UP: ['ArrowUp'], DOWN: ['ArrowDown'], LEFT: ['ArrowLeft'], RIGHT: ['ArrowRight'],
    A: ['z', 'Enter', ' '], B: ['x'], L: ['q'], R: ['e'], START: ['p'], SELECT: ['o']
  };
  const actions = Object.keys(defaults);
  const copyDefaults = () => Object.fromEntries(actions.map((action) => [action, [...defaults[action]]]));
  const normalize = (key) => typeof key === 'string' && key.length === 1 && key !== ' ' ? key.toLowerCase() : key;
  function validKey(key) {
    return typeof key === 'string' && key.length > 0 && key.length <= 32 &&
      !['Escape', 'Tab', 'Shift', 'Control', 'Alt', 'Meta', 'CapsLock', 'Dead', 'Unidentified'].includes(key);
  }
  function valid(map) {
    if (!map || typeof map !== 'object') return false;
    const seen = new Set();
    return actions.every((action) => Array.isArray(map[action]) && map[action].length > 0 && map[action].every((key) => {
      const value = normalize(key);
      if (!validKey(value) || seen.has(value)) return false;
      seen.add(value); return true;
    }));
  }
  let mapping = copyDefaults();
  function reload() {
    try {
      const value = JSON.parse(storage?.getItem(storageKey) || 'null');
      mapping = valid(value) ? Object.fromEntries(actions.map((action) => [action, value[action].map(normalize)])) : copyDefaults();
    } catch (_) { mapping = copyDefaults(); }
    return mapping;
  }
  function save(next) {
    if (!valid(next)) throw new Error('Invalid key configuration');
    if (!storage) throw new Error('Storage unavailable');
    storage.setItem(storageKey, JSON.stringify(next));
    mapping = next;
  }
  function assign(action, key) {
    key = normalize(key);
    if (!actions.includes(action) || !validKey(key)) return false;
    const next = Object.fromEntries(actions.map((name) => [name, [...mapping[name]]]));
    const other = actions.find((name) => name !== action && next[name].includes(key));
    if (other) next[other] = next[action];
    next[action] = [key];
    save(next);
    return true;
  }
  function buttonForKey(key) {
    key = normalize(key);
    return actions.find((action) => mapping[action].includes(key));
  }
  function display(action) {
    return mapping[action].map((key) => key === ' ' ? 'Space' : key.length === 1 ? key.toUpperCase() : key.replace(/^Arrow/, '')).join(' / ');
  }
  reload();
  return { actions, assign, buttonForKey, display, reload, reset() { save(copyDefaults()); }, storageKey, validKey, normalize };
});
