(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.StudioIndent = factory();
})(typeof globalThis === 'undefined' ? this : globalThis, function () {
  'use strict';

  function change(text, start, end, outdent) {
    if (!outdent && start === end) {
      return { from: start, to: end, replacement: '  ', start: start + 2, end: end + 2 };
    }
    const from = start === 0 ? 0 : text.lastIndexOf('\n', start - 1) + 1;
    const last = end > start && text[end - 1] === '\n' ? end - 1 : end;
    const nextBreak = text.indexOf('\n', last);
    const to = nextBreak < 0 ? text.length : nextBreak;
    const lines = text.slice(from, to).split('\n');
    let position = from;
    let newStart = start;
    let newEnd = end;
    const changed = lines.map((line) => {
      const remove = outdent ? (line.startsWith('\t') ? 1 : (line.match(/^ {1,2}/)?.[0].length || 0)) : 0;
      const added = outdent ? 0 : 2;
      if (position <= start) newStart += added - Math.min(remove, start - position);
      if (position <= end) newEnd += added - Math.min(remove, end - position);
      position += line.length + 1;
      return outdent ? line.slice(remove) : '  ' + line;
    });
    return { from, to, replacement: changed.join('\n'), start: newStart, end: newEnd };
  }

  return { change };
});
