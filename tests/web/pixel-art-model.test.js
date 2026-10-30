'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const PixelArtModel = require('../../studio/pixel-art-model.js');
const RED = 0xff0000ff, GREEN = 0x00ff00ff, BLUE = 0x0000ffff;

function dot(model, x, y, color = RED) {
  model.beginStroke();
  model.line(x, y, x, y, color);
  model.endStroke();
}

test('a continuous stroke is gap-free and one undo operation', () => {
  const model = new PixelArtModel({ width: 8, height: 8 });
  model.beginStroke();
  model.line(0, 0, 7, 7, RED);
  model.line(7, 7, 0, 7, GREEN);
  model.endStroke();
  for (let i = 0; i < 7; i++) assert.equal(model.pixel(i, i), RED);
  for (let i = 0; i < 8; i++) assert.equal(model.pixel(i, 7), GREEN);
  assert(model.undo());
  assert(model.frames[0].every((pixel) => pixel === 0));
  assert(!model.undo());
  assert(model.redo());
  assert.equal(model.pixel(0, 0), RED);
  assert.equal(model.pixel(7, 7), GREEN);
});

test('fill respects four-connected boundaries and transparent pixels', () => {
  const model = new PixelArtModel({ width: 5, height: 5 });
  model.beginStroke();
  model.line(2, 0, 2, 4, RED);
  model.endStroke();
  assert(model.fill(0, 0, GREEN));
  assert.equal(model.pixel(1, 4), GREEN);
  assert.equal(model.pixel(2, 4), RED);
  assert.equal(model.pixel(3, 4), 0);
  assert(!model.fill(0, 0, GREEN));
  model.undo();
  assert.equal(model.pixel(0, 0), 0);
  assert.equal(model.pixel(2, 0), RED);
  assert(model.fill(2, 0, 0));
  assert.equal(model.pixel(2, 4), 0);
});

test('frame duplication, ordering and deletion retain independent pixel data', () => {
  const model = new PixelArtModel({ width: 2, height: 2 });
  dot(model, 0, 0);
  model.addFrame(true);
  dot(model, 0, 0, GREEN);
  assert.equal(model.frames[0][0], RED);
  assert.equal(model.frames[1][0], GREEN);
  model.moveFrame(-1);
  assert.equal(model.frames[0][0], GREEN);
  model.deleteFrame();
  assert.equal(model.frames[0][0], RED);
  model.undo();
  assert.equal(model.frames.length, 2);
  assert.equal(model.frames[0][0], GREEN);
  model.redo();
  assert.equal(model.frames.length, 1);
  assert(!model.deleteFrame());
});

test('resize pads and crops each frame, and undo restores cropped pixels', () => {
  const model = new PixelArtModel({ width: 3, height: 2 });
  dot(model, 2, 1);
  model.addFrame(true);
  model.resize(1, 1);
  assert(model.frames.every((frame) => frame.length === 1 && frame[0] === 0));
  model.undo();
  assert.equal(model.width, 3);
  assert(model.frames.every((frame) => frame[5] === RED));
  model.resize(4, 4);
  assert.equal(model.pixel(2, 1), RED);
  assert.equal(model.pixel(3, 3), 0);
});

test('sprite sheets interleave rows horizontally and encode big-endian RGB565', () => {
  const model = new PixelArtModel({ width: 2, height: 2,
    frames: [[RED, 0, GREEN, BLUE], [0xffffffff, 0xff00ffff, 0, RED]] });
  const rgba = model.rgbaSheet();
  assert.equal(rgba.width, 4);
  assert.equal(rgba.height, 2);
  assert.deepEqual([...rgba.bytes.slice(0, 4)], [255, 0, 0, 255]);
  assert.deepEqual([...rgba.bytes.slice(8, 12)], [255, 255, 255, 255]);
  const raw = new DataView(model.rawSheet().buffer);
  assert.deepEqual(Array.from({ length: 8 }, (_, i) => raw.getUint16(i * 2)),
    [0xf800, 0xf81f, 0xffff, 0xf81e, 0x07e0, 0x001f, 0xf81f, 0xf800]);
});

test('PNG pixel import splits frames and preserves transparency through export', () => {
  const original = new PixelArtModel({ width: 2, height: 2,
    frames: [[RED, 0, GREEN, BLUE], [BLUE, RED, 0, GREEN]] });
  const sheet = original.rgbaSheet();
  const imported = new PixelArtModel({ width: 2, height: 2 });
  imported.importSheet(sheet.bytes, sheet.width, sheet.height);
  assert.deepEqual(imported.frames, original.frames);
  imported.undo();
  assert.equal(imported.frames.length, 1);
  imported.redo();
  assert.deepEqual(imported.rawSheet(), original.rawSheet());
  const transparent = new Uint8ClampedArray([255, 255, 255, 127, 255, 0, 0, 128]);
  const alpha = new PixelArtModel({ width: 1, height: 1 });
  alpha.importSheet(transparent, 2, 1);
  assert.equal(alpha.frames[0][0], 0);
  assert.equal(alpha.frames[1][0], RED);
});

test('serialized drafts are independent and restore animation settings', () => {
  const model = new PixelArtModel();
  model.name = 'player';
  model.fps = 12;
  dot(model, 1, 1);
  model.addFrame(true);
  const restored = new PixelArtModel(structuredClone(model.serialize()));
  assert.equal(restored.name, 'player');
  assert.equal(restored.fps, 12);
  assert.equal(restored.selected, 1);
  assert.deepEqual(restored.frames, model.frames);
  dot(restored, 1, 1, BLUE);
  assert.equal(model.pixel(1, 1), RED);
});

test('size, frame and history bounds remain usable at the limits', () => {
  for (const width of [0, 65, NaN, 1.5]) assert.throws(() => new PixelArtModel({ width }));
  assert.throws(() => new PixelArtModel({ width: 2, height: 2, frames: [[0]] }));
  const model = new PixelArtModel({ width: 64, height: 64 });
  for (let i = 1; i < 32; i++) assert(model.addFrame());
  assert(!model.addFrame());
  for (let i = 0; i < 40; i++) dot(model, i, 1);
  assert(model.undoStack.length <= 30);
  assert(model.undoStack.reduce((sum, item) => sum + item.frames.length * item.width * item.height * 4, 0) <= 8 * 1024 * 1024);
  assert(model.undo());
  assert(model.redo());
});
