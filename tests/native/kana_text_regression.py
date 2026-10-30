#!/usr/bin/env python3
"""Compare production C and JavaScript text pixels, including clipping and fallback."""
import base64
import ctypes
import json
import re
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
c = (root / 'src/game_platform.c').read_text()
js = (root / 'emulator/emulator.js').read_text()
def function(name, result):
    start = c.index('\n' + name + '(') + 1
    end = c.index('\n}', start) + 2
    return result + '\n' + c[start:end] + '\n'

harness = '''#include <limits.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include "game_kana_font.h"\n#include "game_kanji_font.h"\n#include "game_ascii_font.h"\n#define GAME_DISPLAY_WIDTH 320\n#define GAME_DISPLAY_HEIGHT 240\nstatic uint8_t framebuffer[320*240*2];\nstatic void display_dirty_add(int x,int y,int w,int h) {(void)x;(void)y;(void)w;(void)h;}\n'''
for name, result in [('text_next_utf8', 'static uint32_t'), ('text_kana_glyph', 'static const uint16_t *'), ('draw_text', 'static void')]:
    harness += function(name, result)
harness += '''\n#include <string.h>\nconst uint8_t *render(const char *text,int x,int y) { memset(framebuffer,0,sizeof(framebuffer)); draw_text(x,y,text,0xffff); return framebuffer; }\n'''
start = js.index('  // MaruMinya 1.500')
end = js.index('\n  ];', js.index('  const asciiGlyphs = [', start)) + len('\n  ];')
a = js.index('  function kanjiGlyph('); b = js.index('\n  }', js.index('  function drawText(', a)) + len('\n  }')
node = '''const fs = require('fs');\nlet buffer; let context = {fillStyle: '', fillRect(x,y,w,h) { for(let dy=0;dy<h;dy++) for(let dx=0;dx<w;dx++) if(x+dx>=0&&x+dx<320&&y+dy>=0&&y+dy<240) buffer[(y+dy)*320+x+dx]=1; }};\nfunction beforeCanvasDraw() {}\nfunction rgb565ToRgb() { return [255,255,255]; }\n''' + js[start:end] + '\n' + js[a:b] + '''\nfor (const [s,x,y] of JSON.parse(fs.readFileSync(0,'utf8'))) { buffer=Buffer.alloc(320*240); drawText(x,y,s,65535); console.log(buffer.toString('base64')); }\n'''
header = (root / 'include/game_kana_font.h').read_text()
characters = [chr(int(value, 16)) for value in re.findall(r'^  \{0x([0-9a-f]+),', header, re.MULTILINE)]
assert len(characters) == 178 and len(set(characters)) == 178
kanji_header = (root / 'include/game_kanji_font.h').read_text()
entries = re.findall(r'^  \{0x([0-9a-f]+), \{([^}]+)\}\}', kanji_header, re.MULTILINE)
assert len(entries) == 6355
kanji = [(int(code, 16), [int(v, 16) for v in values.split(', ')]) for code, values in entries]
expected_codes = set()
for high in range(0xb0, 0xf5):
    for low in range(0xa1, 0xff):
        try:
            expected_codes.add(ord(bytes((high, low)).decode('euc_jp')))
        except UnicodeDecodeError:
            pass
assert [code for code, _ in kanji] == sorted(expected_codes)
match = re.search(r'const kanjiGlyphData = Uint8Array.from\(atob\(\[([\s\S]*?)\]\.join', js)
assert match
packed = base64.b64decode(''.join(re.findall(r"'([A-Za-z0-9+/=]+)'", match.group(1))))
assert packed == b''.join(code.to_bytes(2, 'big') + b''.join(row.to_bytes(2, 'big') for row in rows) for code, rows in kanji)
cases = [(character, 8, 8) for character in characters if character != '　']
cases += [(chr(code), 8, 8) for code, _ in kanji[::137]]
cases += [(chr(codepoint), 8, 8) for codepoint in range(33, 127)]
cases += [('あいうえお カタカナ がぱっゃー！？「。」', 4, 4), ('Aあz😀漢字B', 0, 20), ('漢字表示左端', -7, -4), ('山川海空', 305, 233), ('かきくけこ', -7, -4), ('アイウエオ', 305, 233), ('ぁぃぅぇぉゔヷヺ', 7, 40)]
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp); (tmp/'test.c').write_text(harness); (tmp/'test.js').write_text(node)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-I'+str(root/'include'), str(tmp/'test.c'), '-o', str(tmp/'test.dylib')], check=True)
    lib = ctypes.CDLL(str(tmp/'test.dylib')); lib.render.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int]; lib.render.restype = ctypes.POINTER(ctypes.c_uint8)
    output = subprocess.check_output(['node', str(tmp/'test.js')], input=json.dumps(cases).encode()).splitlines()
    assert len(output) == len(cases)
    for (text,x,y), line in zip(cases, output):
        expected = base64.b64decode(line); pixels = lib.render(text.encode(),x,y)
        assert bytes(pixels[i*2] != 0 for i in range(320*240)) == expected, (text,x,y)
        assert any(expected), (text,x,y)
    def picture(text):
        data = lib.render(text, 0, 0)
        return bytes(data[i] for i in range(320*240*2))
    # Representative source glyphs verify this is MaruMinya, not merely that
    # the two backends happen to agree with each other.
    def cell_rows(text):
        pixels = picture(text)
        return [sum((1 << (11-x)) for x in range(12) if pixels[(y*320+x)*2]) for y in range(12)]
    assert cell_rows('あ'.encode()) == [0x000, 0x100, 0xffe, 0x100, 0x1f8, 0x324, 0x522, 0x522, 0x942, 0x942, 0x884, 0x718]
    assert cell_rows(b'A') == [v << 3 for v in [0x00, 0x00, 0x1c, 0x22, 0x22, 0x22, 0x22, 0x3e, 0x22, 0x22, 0x22, 0x00]]
    assert picture('漢'.encode()) != picture(b'?')
    assert picture('海'.encode()) != picture(b'?')
    assert picture('𠮷'.encode()) == picture(b'?')
    assert picture('😀'.encode()) == picture(b'?')
    assert picture('あ'.encode()) != picture(b'?')
    assert picture(b'a') != picture(b'A')
    assert picture(b'3') != picture(b'?')
    assert picture(b'!') != picture(b'?')
    assert picture(b'\xe3') == picture(b'?')
print('built-in text: 6,355 kanji data and C/JavaScript pixel parity PASS')
