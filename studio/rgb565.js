(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.StudioRGB565 = factory();
})(typeof globalThis === 'undefined' ? this : globalThis, function () {
  'use strict';

  function fromHex(value) {
    if (!/^#?[0-9a-f]{6}$/i.test(value)) return null;
    const rgb = parseInt(value.replace('#', ''), 16);
    const red = Math.floor((((rgb >> 16) & 255) * 31 + 127) / 255);
    const green = Math.floor((((rgb >> 8) & 255) * 63 + 127) / 255);
    const blue = Math.floor(((rgb & 255) * 31 + 127) / 255);
    return '0x' + ((red << 11) | (green << 5) | blue).toString(16).padStart(4, '0').toUpperCase();
  }

  return { fromHex };
});
