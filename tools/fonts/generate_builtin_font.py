#!/usr/bin/env python3
"""Regenerate the built-in 12px font from the pinned MaruMinya 1.500 TTF.

Requires Pillow 12.3.0; the TTF is obtained separately from the project URL in
MARUMINYA-OFL.txt. Normal builds use the checked-in bitmap data, not Pillow.
"""
import base64
import hashlib
from pathlib import Path
import re
import sys
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[2]
path = Path(sys.argv[1])
assert hashlib.sha256(path.read_bytes()).hexdigest() == 'b05f108a3433602545f1dcb8acef167aaf744965d8d9571045d5f2cdbe12f9e5', 'unexpected font version'
font = ImageFont.truetype(str(path), 12)
missing = bytes(font.getmask('\u0378'))
kana_path = root / 'include/game_kana_font.h'
kana_codes = [int(s, 16) for s in re.findall(r'^  \{0x([0-9a-f]+),', kana_path.read_text(), re.M)]
assert len(kana_codes) == 178

def rows(code, width):
    char = chr(code)
    assert bytes(font.getmask(char)) != missing, f'missing U+{code:04X}'
    img = Image.new('L', (width, 12))
    ImageDraw.Draw(img).text((0, 12), char, font=font, fill=255, anchor='ls')
    return [sum((1 << (width - 1 - col)) for col in range(width) if img.getpixel((col, row)) >= 128) for row in range(12)]

kana = [(c, rows(c, 12)) for c in kana_codes]
ascii_glyphs = [(c, rows(c, 6)) for c in range(32, 127)]
# EUC-JP rows B0-F4 encode the JIS X 0208 first and second kanji levels.
kanji_codes = set()
for high in range(0xb0, 0xf5):
    for low in range(0xa1, 0xff):
        try:
            kanji_codes.add(ord(bytes((high, low)).decode('euc_jp')))
        except UnicodeDecodeError:
            pass
assert len(kanji_codes) == 6355
kanji = [(c, rows(c, 12)) for c in sorted(kanji_codes)]
comment = 'MaruMinya 1.500 (SIL OFL 1.1); see tools/fonts/MARUMINYA-OFL.txt.'
header = '/* ' + comment + ' */\n#ifndef GAME_KANA_FONT_H\n#define GAME_KANA_FONT_H\n#include <stdint.h>\ntypedef struct { uint16_t codepoint; uint16_t rows[12]; } game_kana_glyph_t;\nstatic const game_kana_glyph_t game_kana_glyphs[] = {\n'
for c, data in kana:
    header += '  {0x%04x, {%s}}, /* %s */\n' % (c, ', '.join('0x%03x' % n for n in data), chr(c))
header += '};\n#define GAME_KANA_GLYPH_COUNT (sizeof(game_kana_glyphs) / sizeof(game_kana_glyphs[0]))\n#endif\n'
kana_path.write_text(header)
header = '/* ' + comment + ' */\n#ifndef GAME_KANJI_FONT_H\n#define GAME_KANJI_FONT_H\n#include "game_kana_font.h"\nstatic const game_kana_glyph_t game_kanji_glyphs[] = {\n'
for c, data in kanji:
    header += '  {0x%04x, {%s}}, /* %s */\n' % (c, ', '.join('0x%03x' % n for n in data), chr(c))
header += '};\n#define GAME_KANJI_GLYPH_COUNT (sizeof(game_kanji_glyphs) / sizeof(game_kanji_glyphs[0]))\n#endif\n'
(root / 'include/game_kanji_font.h').write_text(header)
# Sorted 26-byte records: big-endian codepoint followed by 12 big-endian rows.
packed = b''.join(c.to_bytes(2, 'big') + b''.join(n.to_bytes(2, 'big') for n in data) for c, data in kanji)
encoded = base64.b64encode(packed).decode('ascii')
kanji_js = '  // ' + comment + '\n  const kanjiGlyphData = Uint8Array.from(atob([\n'
kanji_js += ''.join("    '%s',\n" % encoded[i:i+100] for i in range(0, len(encoded), 100))
kanji_js += "  ].join('')), char => char.charCodeAt(0));"
header = '/* ' + comment + ' */\n#ifndef GAME_ASCII_FONT_H\n#define GAME_ASCII_FONT_H\n#include <stdint.h>\nstatic const uint8_t game_ascii_glyphs[95][12] = {\n'
for c, data in ascii_glyphs:
    header += '  {%s}, /* U+%04X */\n' % (', '.join('0x%02x' % n for n in data), c)
header += '};\n#endif\n'
(root / 'include/game_ascii_font.h').write_text(header)
kana_js = '  // ' + comment + '\n  const kanaGlyphs = new Map([\n'
for c, data in kana:
    kana_js += '    [0x%04x, [%s]], // %s\n' % (c, ', '.join('0x%03x' % n for n in data), chr(c))
kana_js += '  ]);'
ascii_js = '  // ' + comment + '\n  const asciiGlyphs = [\n'
for c, data in ascii_glyphs:
    ascii_js += '    [%s], // U+%04X\n' % (', '.join('0x%02x' % n for n in data), c)
ascii_js += '  ];'
for name in ('emulator/emulator.js', 'emulator/system-screen.js'):
    p = root / name
    source = p.read_text()
    if 'const kanaGlyphs' in source:
        source = re.sub(r'  // [^\n]+\n  const kanaGlyphs = new Map\(\[.*?\n  \]\);', lambda _: kana_js, source, count=1, flags=re.S)
        if 'const kanjiGlyphData' in source:
            source = re.sub(r'  // [^\n]+\n  const kanjiGlyphData = Uint8Array.from\(atob\(\[.*?\n  \]\.join\(\'\'\)\), char => char.charCodeAt\(0\)\);', lambda _: kanji_js, source, count=1, flags=re.S)
        else:
            source = source.replace(kana_js, kana_js + '\n\n' + kanji_js, 1)
    source = re.sub(r'  // [^\n]+\n  const asciiGlyphs = \[.*?\n  \];', lambda _: ascii_js, source, count=1, flags=re.S)
    p.write_text(source)
