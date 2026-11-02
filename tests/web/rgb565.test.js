'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { fromHex } = require('../../studio/rgb565.js');

test('converts RGB colors into Ruby-ready RGB565 values', () => {
  for (const [rgb, expected] of [
    ['#000000', '0x0000'], ['#ffffff', '0xFFFF'], ['ff0000', '0xF800'],
    ['#00ff00', '0x07E0'], ['#0000FF', '0x001F'], ['#ff00ff', '0xF81F'],
    ['#cd654e', '0xCB29'], ['#050305', '0x0821']
  ]) assert.equal(fromHex(rgb), expected, rgb);
});

test('rejects incomplete and invalid RGB input', () => {
  for (const rgb of ['', '#abc', '12345g', '#1234567', '0x123456']) assert.equal(fromHex(rgb), null);
});
