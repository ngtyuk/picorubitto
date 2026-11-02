'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { change } = require('../../studio/editor-indent.js');

function apply(text, start, end, outdent = false) {
  const edit = change(text, start, end, outdent);
  return { text: text.slice(0, edit.from) + edit.replacement + text.slice(edit.to), start: edit.start, end: edit.end };
}

test('Tab inserts two spaces at the cursor', () => {
  assert.deepEqual(apply('abcd', 2, 2), { text: 'ab  cd', start: 4, end: 4 });
});

test('Tab indents selected lines and excludes an unselected final line', () => {
  assert.deepEqual(apply('a\nb\nc', 0, 4), { text: '  a\n  b\nc', start: 2, end: 8 });
  assert.deepEqual(apply('\na', 0, 1), { text: '  \na', start: 2, end: 3 });
});

test('Shift+Tab removes up to two spaces or one tab from selected lines', () => {
  assert.deepEqual(apply('  a\n b\n\tc', 2, 9, true), { text: 'a\nb\nc', start: 0, end: 5 });
  assert.deepEqual(apply('  a', 1, 1, true), { text: 'a', start: 0, end: 0 });
  assert.deepEqual(apply('abc', 1, 1, true), { text: 'abc', start: 1, end: 1 });
});
