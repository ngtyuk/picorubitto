#!/usr/bin/env python3
"""Compare production PBF rendering with a scalar reference under ASan/UBSan."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path, help='also measure an earlier game_platform.c')
args = parser.parse_args()


def harness(source, baseline=False):
    def function(name, result):
        start = source.index('\n' + name + '(') + 1
        end = source.index('\n}', start) + 2
        code = '\n' + result + '\n' + source[start:end] + '\n'
        if name == 'font_validate':
            code = code.replace('{', '{ validations++;', 1)
        return code

    start = source.index('struct game_font {')
    end = source.index('\n};', start) + 3
    code = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#define GAME_DISPLAY_WIDTH 320u
#define GAME_DISPLAY_HEIGHT 240u
#define GAME_FONT_HEADER_BYTES 16u
#define GAME_FONT_SIZE_HEADER_BYTES 20u
#define GAME_FONT_GLYPH_RECORD_BYTES 20u
#define GAME_FONT_MAX_BYTES (512u * 1024u)
#define FRAME_BYTES (320u * 240u * 2u)
static uint8_t framebuffer[FRAME_BYTES], expected[FRAME_BYTES];
static uint8_t *fixture;
static size_t fixture_bytes, asset_resident_bytes, allocated;
static unsigned validations, dirty_calls;
static bool fail_read, short_read;
static size_t fail_allocation;
static int dirty[4];
static void *asset_alloc(size_t bytes) {
  if (fail_allocation && --fail_allocation == 0) return NULL;
  void *p = calloc(1, bytes); if (p) allocated++; return p;
}
static void asset_free(void *p) { if (p) { allocated--; free(p); } }
static bool game_storage_file_size(const char *path, size_t *bytes) {
  if (!path) return false;
  *bytes = fixture_bytes; return true;
}
static bool game_storage_read_file(const char *path, uint8_t *data, size_t size, size_t *loaded) {
  (void)path; memcpy(data, fixture, size); *loaded = short_read ? size - 1 : size;
  return !fail_read;
}
static void display_dirty_add(int x, int y, int w, int h) {
  dirty_calls++; dirty[0] = x; dirty[1] = y; dirty[2] = w; dirty[3] = h;
  assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= 320 && y + h <= 240);
}
'''
    code += source[start:end] + '\ntypedef struct game_font game_font_t;\n'
    for name, result in [
        ('display_blend_pixel', 'static void'), ('font_be16', 'static uint16_t'),
        ('font_be16_signed', 'static int16_t'), ('font_be32', 'static uint32_t'),
        ('font_range_valid', 'static bool'), ('font_validate', 'static bool'),
        ('font_size_header', 'static const uint8_t *'),
        ('font_glyph_record', 'static const uint8_t *'), ('game_font_load', 'game_font_t *'),
        ('game_font_measure', 'int'), ('game_display_draw_font', 'bool'),
        ('game_font_release', 'void')
    ]:
        code += function(name, result)
    code += r'''
static void reference(game_font_t *font, const char *text, int x, int y,
                      uint16_t size, int spacing, uint16_t color) {
  const uint8_t *header = font_size_header(font, size); assert(header);
  int64_t cursor = x, baseline = (int64_t)y + font_be16_signed(header + 4);
  while (*text) {
    if (*text == '\n') { cursor = x; baseline += font_be16(header + 2); text++; continue; }
    const uint8_t *record = font_glyph_record(font, header, *text++); assert(record);
    int width = font_be16(record), height = font_be16(record + 2);
    const uint8_t *bitmap = font->data + font_be32(record + 12);
    int64_t left = cursor + font_be16_signed(record + 4);
    int64_t top = baseline - font_be16_signed(record + 6);
    for (int gy = 0; gy < height; gy++) for (int gx = 0; gx < width; gx++) {
      int64_t px = left + gx, py = top + gy;
      if (px >= 0 && py >= 0 && px < 320 && py < 240)
        display_blend_pixel((int)px, (int)py, color, bitmap[gy * width + gx]);
    }
    cursor += (int64_t)font_be16_signed(record + 8) + spacing;
  }
}
static void be32(uint8_t *p, uint32_t value) {
  p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}
static void rejection_tests(void) {
  size_t original_bytes = fixture_bytes;
  const size_t offsets[] = {0, 6, 8, 12, 16, 18, 24, 28, 32, 56, 68, 72};
  for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
    size_t at = offsets[i]; uint8_t saved[4]; memcpy(saved, fixture + at, 4);
    memset(fixture + at, at == 16 || at == 18 ? 0 : 0xff, 4);
    assert(game_font_load("bad.pbf") == NULL && allocated == 0 && asset_resident_bytes == 0);
    memcpy(fixture + at, saved, 4);
  }
  fixture_bytes = 15; assert(!game_font_load("short.pbf")); fixture_bytes = original_bytes;
  short_read = true; assert(!game_font_load("short.pbf")); short_read = false;
  fail_read = true; assert(!game_font_load("read.pbf")); fail_read = false;
  fail_allocation = 1; assert(!game_font_load("oom.pbf"));
  fail_allocation = 2; assert(!game_font_load("oom.pbf"));
  assert(allocated == 0 && asset_resident_bytes == 0);
  /* A valid header with an escaping bitmap must still fail the load. */
  uint32_t offset = font_be32(fixture + 68); be32(fixture + 68, fixture_bytes + 1);
  assert(!game_font_load("offset.pbf")); be32(fixture + 68, offset);
}
static uint32_t random_state = 9041;
static uint32_t random_next(void) { random_state = random_state * 1664525u + 1013904223u; return random_state; }
static unsigned comparisons;
static void compare(game_font_t *font, const char *text, int x, int y, int size, int spacing, uint16_t color) {
  for (size_t i = 0; i < FRAME_BYTES; i++) framebuffer[i] = (uint8_t)(i * 17 + i / 640 * 31);
  reference(font, text, x, y, size, spacing, color); memcpy(expected, framebuffer, FRAME_BYTES);
  for (size_t i = 0; i < FRAME_BYTES; i++) framebuffer[i] = (uint8_t)(i * 17 + i / 640 * 31);
  dirty_calls = 0;
  assert(game_display_draw_font(font, x, y, text, size, spacing, color));
  assert(memcmp(framebuffer, expected, FRAME_BYTES) == 0);
  comparisons++;
}
static double milliseconds(void) {
  struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
int main(int argc, char **argv) {
  assert(argc == 2); FILE *file = fopen(argv[1], "rb"); assert(file);
  fseek(file, 0, SEEK_END); fixture_bytes = (size_t)ftell(file); rewind(file);
  fixture = malloc(fixture_bytes); assert(fread(fixture, 1, fixture_bytes, file) == fixture_bytes); fclose(file);
  rejection_tests(); validations = 0;
  game_font_t *font = game_font_load("font.pbf"); assert(font && validations == 1);
  assert(game_font_measure(font, "A B?", 12, 1) == 24);
  assert(game_font_measure(font, "A\nB", 12, 1) == 6);
  assert(game_font_measure(font, "A B?", 24, 1) == 45);
  assert(game_font_measure(font, "A B?", 12, -1) == 18);
  assert(game_font_measure(font, "~", 12, 0) == game_font_measure(font, "?", 12, 0));
  assert(game_font_measure(font, "", 12, 0) == 0);
  assert(game_font_measure(font, "A", 13, 0) == -1);
  assert(!game_display_draw_font(font, 0, 0, "A", 13, 0, 0xffff));
  assert(!game_display_draw_font(NULL, 0, 0, "A", 12, 0, 0xffff));
  const int positions[][2] = {{0,0},{-3,-4},{315,235},{319,239},{-300,-200},
    {INT_MIN,INT_MAX},{INT_MAX,INT_MIN},{INT_MAX,0},{0,INT_MAX}};
  for (int size = 12; size <= 24; size += 12) for (size_t i = 0; i < sizeof(positions)/sizeof(positions[0]); i++)
    compare(font, "A B?~\nB A", positions[i][0], positions[i][1], size, -1, 0x6b5a);
  for (int i = 0; i < 700; i++)
    compare(font, "A B?~\nB A", (int)(random_next()%400)-40, (int)(random_next()%320)-40,
            (i%2+1)*12, (int)(random_next()%9)-4, (uint16_t)random_next());
  compare(font, "", 0, 0, 12, 0, 0xffff);
  compare(font, "A A", 0, 0, 12, INT_MAX, 0xffff);
  compare(font, "A A", 0, 0, 12, INT_MIN, 0xffff);
  unsigned steady_validations = validations;
  for (int round = 0; round < 3; round++) {
    double started = milliseconds(); volatile int total = 0;
    for (int i = 0; i < 20000; i++) total += game_font_measure(font, "A B?", 12, 1);
    double measure_ms = milliseconds() - started; assert(total == 480000);
    started = milliseconds();
    for (int i = 0; i < 3000; i++) assert(game_display_draw_font(font, -20, -10, "A B?~", 24, 1, 0xffff));
    printf("FONT_HOST round=%d measure_20000_ms=%.3f draw_3000_ms=%.3f\n", round+1, measure_ms, milliseconds()-started);
  }
  STEADY_CHECK
  game_font_release(font); assert(allocated == 0 && asset_resident_bytes == 0); free(fixture);
  printf("font regression: PASS %u framebuffer comparisons; steady validations=%u\n", comparisons, validations-steady_validations);
}
'''
    return code.replace('STEADY_CHECK', '(void)steady_validations;' if baseline else 'assert(validations == 1);')


with tempfile.TemporaryDirectory(prefix='game-font-test-') as directory:
    directory = Path(directory)
    subprocess.run(['ruby', str(root / 'tools/fixtures/generate_font_checks.rb'), str(directory)], check=True)
    sources = [('current', root / 'src/game_platform.c')]
    if args.baseline:
        sources.insert(0, ('baseline', args.baseline))
    for label, path in sources:
        generated = directory / f'{label}.c'
        generated.write_text(harness(path.read_text(), label == 'baseline'))
        executable = directory / label
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', str(generated), '-o', str(executable)], check=True)
        print(label, flush=True)
        subprocess.run([str(executable), str(directory / 'font-check.pbf')], check=True)
