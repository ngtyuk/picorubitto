#!/usr/bin/env python3
"""Check production texture sampling against the scalar nearest-neighbor rule."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/game_platform.c').read_text()
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path,
                    help='also benchmark against an earlier game_platform.c')
args = parser.parse_args()


def function(text, name, result):
    start = text.index('\n' + name + '(') + 1
    end = text.index('\n}', start) + 2
    return '\n' + result + '\n' + text[start:end] + '\n'


def structure(name):
    start = source.index('struct ' + name + ' {')
    end = source.index('\n};', start) + 3
    return source[start:end] + '\n'


harness = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#define GAME_DISPLAY_WIDTH 320u
#define GAME_DISPLAY_HEIGHT 240u
#define FRAME_BYTES (GAME_DISPLAY_WIDTH * GAME_DISPLAY_HEIGHT * 2u)
static uint8_t framebuffer[FRAME_BYTES];
static uint8_t expected[FRAME_BYTES];
static bool tft_ready = true;
static unsigned dirty_calls;
static int dirty_args[4];
static void display_dirty_add(int x, int y, int w, int h) {
  dirty_calls++;
  dirty_args[0] = x; dirty_args[1] = y; dirty_args[2] = w; dirty_args[3] = h;
}
'''
harness += structure('game_texture')
harness += 'typedef struct game_texture game_texture_t;\n'
harness += structure('game_texture_sample_axis')
for name, result in [
    ('draw_sprite_transparent_row', 'static void'),
    ('texture_sample_axis', 'static struct game_texture_sample_axis'),
    ('texture_sample_advance', 'static void'),
    ('draw_texture_scaled', 'static void'),
    ('game_display_draw_texture', 'bool'),
]:
    harness += function(source, name, result)

harness += r'''
struct draw_case { int x, y, sx, sy, sw, sh, dw, dh; };
static unsigned checked;
static uint32_t random_state = 0x2d565;
static uint32_t next_random(void) {
  random_state = random_state * 1664525u + 1013904223u;
  return random_state;
}
static game_texture_t texture_new(uint16_t width, uint16_t height, bool opaque) {
  game_texture_t texture = {0};
  texture.width = width; texture.height = height;
  texture.key_color = 0xf81f; texture.opaque = opaque;
  texture.bytes = (size_t)width * height * 2u;
  texture.pixels = malloc(texture.bytes);
  assert(texture.pixels);
  for (size_t i = 0; i < texture.bytes / 2u; i++) {
    uint16_t color = (uint16_t)((i * 301u + i / width * 719u) & 0xffffu);
    if (color == texture.key_color) color ^= 1u;
    if (!opaque && i % 3u == 0) color = texture.key_color;
    texture.pixels[i * 2u] = (uint8_t)(color >> 8);
    texture.pixels[i * 2u + 1u] = (uint8_t)color;
  }
  return texture;
}
static void check_draw(const game_texture_t *texture, struct draw_case c,
                       bool valid) {
  /* Different backgrounds on adjacent rows expose transparent row-copy bugs. */
  for (size_t i = 0; i < FRAME_BYTES; i++) {
    expected[i] = framebuffer[i] = (uint8_t)(i * 17u + i / 640u * 31u);
  }
  if (valid) {
    for (int py = 0; py < (int)GAME_DISPLAY_HEIGHT; py++) {
      int64_t ry = (int64_t)py - c.y;
      if (ry < 0 || ry >= c.dh) continue;
      size_t sy = (size_t)(c.sy + ry * c.sh / c.dh);
      for (int px = 0; px < (int)GAME_DISPLAY_WIDTH; px++) {
        int64_t rx = (int64_t)px - c.x;
        if (rx < 0 || rx >= c.dw) continue;
        size_t sx = (size_t)(c.sx + rx * c.sw / c.dw);
        size_t src = (sy * texture->width + sx) * 2u;
        size_t dst = ((size_t)py * GAME_DISPLAY_WIDTH + (size_t)px) * 2u;
        uint16_t color = (uint16_t)((texture->pixels[src] << 8) |
                                   texture->pixels[src + 1u]);
        if (texture->opaque || color != texture->key_color) {
          expected[dst] = texture->pixels[src];
          expected[dst + 1u] = texture->pixels[src + 1u];
        }
      }
    }
  }
  dirty_calls = 0;
  bool result = game_display_draw_texture(texture, c.x, c.y, c.sx, c.sy,
                                          c.sw, c.sh, c.dw, c.dh);
  if (result != valid || memcmp(expected, framebuffer, FRAME_BYTES) != 0) {
    fprintf(stderr, "texture mismatch: x=%d y=%d src=%d,%d %dx%d dst=%dx%d\n",
            c.x, c.y, c.sx, c.sy, c.sw, c.sh, c.dw, c.dh);
    abort();
  }
  assert(dirty_calls == (valid ? 1u : 0u));
  if (valid) {
    assert(dirty_args[0] == c.x && dirty_args[1] == c.y);
    assert(dirty_args[2] == c.dw && dirty_args[3] == c.dh);
  }
  checked++;
}
static void sampling_test(void) {
  const struct draw_case cases[] = {
    {0,0,0,0,32,24,32,24}, {17,19,0,0,32,24,64,48},
    {-7,-9,3,5,21,13,63,52}, {300,225,3,5,21,13,63,52},
    {-13,-11,5,7,19,11,53,37}, {1,3,5,7,19,11,7,5},
    {-2,-1,5,7,19,11,7,5}, {7,5,0,0,32,24,320,240},
    {3,4,0,0,32,24,32,97}, {3,4,0,0,32,24,101,24},
    {319,239,31,23,1,1,320,240}, {0,0,0,0,1,1,320,240},
    {320,240,0,0,32,24,400,300}, {-1000,-1000,0,0,32,24,65,49},
    {INT_MAX,INT_MAX,0,0,32,24,INT_MAX,INT_MAX},
    {INT_MIN,INT_MIN,0,0,32,24,INT_MAX,INT_MAX},
    {-INT_MAX+319,-INT_MAX+239,0,0,32,24,INT_MAX,INT_MAX},
    {-1073741701,-1073741719,0,0,32,24,INT_MAX,INT_MAX},
    {1,2,0,0,32,24,1,239}, {1,2,0,0,32,24,319,1}
  };
  for (int opaque = 0; opaque <= 1; opaque++) {
    game_texture_t t = texture_new(32, 24, opaque != 0);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
      check_draw(&t, cases[i], true);
    for (int i = 0; i < 2000; i++) {
      int sx = (int)(next_random() % 32u), sy = (int)(next_random() % 24u);
      struct draw_case c = {
        (int)(next_random() % 700u) - 330,
        (int)(next_random() % 540u) - 250,
        sx, sy, 1 + (int)(next_random() % (32u - (unsigned)sx)),
        1 + (int)(next_random() % (24u - (unsigned)sy)),
        1 + (int)(next_random() % 1000u), 1 + (int)(next_random() % 800u)
      };
      check_draw(&t, c, true);
    }
    const struct draw_case invalid[] = {
      {0,0,-1,0,1,1,2,2}, {0,0,0,-1,1,1,2,2},
      {0,0,32,0,1,1,2,2}, {0,0,0,24,1,1,2,2},
      {0,0,1,0,32,1,2,2}, {0,0,0,1,1,24,2,2},
      {0,0,0,0,0,1,2,2}, {0,0,0,0,1,0,2,2},
      {0,0,0,0,1,1,0,2}, {0,0,0,0,1,1,2,-1},
      {0,0,INT_MAX,0,1,1,2,2}, {0,0,0,0,INT_MAX,1,2,2}
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
      check_draw(&t, invalid[i], false);
    check_draw(NULL, cases[0], false);
    tft_ready = false; check_draw(&t, cases[0], false); tft_ready = true;
    free(t.pixels);
  }
  game_texture_t wide = texture_new(UINT16_MAX, 1, false);
  game_texture_t tall = texture_new(1, UINT16_MAX, true);
  check_draw(&wide, (struct draw_case){0,0,0,0,65535,1,320,240}, true);
  check_draw(&wide, (struct draw_case){-101,1,65000,0,535,1,INT_MAX,238}, true);
  check_draw(&tall, (struct draw_case){0,0,0,0,1,65535,320,240}, true);
  check_draw(&tall, (struct draw_case){1,-101,0,65000,1,535,319,INT_MAX}, true);
  free(wide.pixels); free(tall.pixels);
  printf("texture regression: PASS (%u full framebuffer comparisons)\n", checked);
}
'''

if args.baseline:
    baseline = args.baseline.read_text()
    harness += function(baseline, 'game_display_draw_texture', 'bool').replace(
        'game_display_draw_texture(', 'baseline_draw_texture(')
    harness += r'''
typedef bool (*draw_fn)(const game_texture_t *, int, int, int, int, int, int, int, int);
static volatile uint32_t checksum;
static double seconds(void) {
  struct timespec t;
  assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
  return (double)t.tv_sec + (double)t.tv_nsec / 1000000000.0;
}
static double benchmark(draw_fn draw, const game_texture_t *t, struct draw_case c,
                        int count) {
  double start = seconds();
  for (int i = 0; i < count; i++) {
    draw(t, c.x, c.y, c.sx, c.sy, c.sw, c.sh, c.dw, c.dh);
    checksum += framebuffer[(unsigned)i % FRAME_BYTES];
  }
  return (seconds() - start) * 1000000.0 / count;
}
static double median(double *v) {
  if (v[0] > v[1]) { double t=v[0]; v[0]=v[1]; v[1]=t; }
  if (v[1] > v[2]) { double t=v[1]; v[1]=v[2]; v[2]=t; }
  if (v[0] > v[1]) { double t=v[0]; v[0]=v[1]; v[1]=t; }
  return v[1];
}
static void benchmarks(void) {
  const char *names[] = {"1:1", "2x", "3x", "noninteger", "downscale",
                        "vertical-only", "horizontal-only", "full-screen", "single-row"};
  const struct draw_case cases[] = {
    {3,5,0,0,32,24,32,24}, {3,5,0,0,32,24,64,48},
    {3,5,0,0,32,24,96,72}, {-7,-9,3,5,21,13,83,61},
    {3,5,0,0,32,24,11,7}, {3,5,0,0,32,24,32,97},
    {3,5,0,0,32,24,101,24}, {0,0,0,0,32,24,320,240},
    {0,0,0,0,32,24,320,1}
  };
  puts("Host-only -O2 comparison (median of 3 runs, microseconds/draw; NOT RP2350 FPS)");
  for (int opaque = 0; opaque <= 1; opaque++) {
    game_texture_t t = texture_new(32, 24, opaque != 0);
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
      int count = 8000000 / (cases[i].dw * cases[i].dh);
      if (count < 100) count = 100;
      if (count > 20000) count = 20000;
      double old[3], current[3];
      benchmark(game_display_draw_texture, &t, cases[i], 100);
      benchmark(baseline_draw_texture, &t, cases[i], 100);
      for (int trial = 0; trial < 3; trial++) {
        /* Alternate order to reduce warm-cache and scheduling bias. */
        if (trial & 1) {
          current[trial] = benchmark(game_display_draw_texture, &t, cases[i], count);
          old[trial] = benchmark(baseline_draw_texture, &t, cases[i], count);
        } else {
          old[trial] = benchmark(baseline_draw_texture, &t, cases[i], count);
          current[trial] = benchmark(game_display_draw_texture, &t, cases[i], count);
        }
      }
      double before = median(old), after = median(current);
      printf("%s %-15s old=%8.3f new=%8.3f speedup=%.2fx\n",
             opaque ? "opaque" : "keyed ", names[i], before, after, before / after);
    }
    free(t.pixels);
  }
}
int main(void) { sampling_test(); benchmarks(); }
'''
else:
    harness += 'int main(void) { sampling_test(); }\n'

with tempfile.TemporaryDirectory(prefix='game-texture-test-') as directory:
    path = Path(directory)
    (path / 'texture.c').write_text(harness)
    compiler = os.environ.get('CC', 'cc')
    flags = ['-O2'] if args.baseline else ['-fsanitize=address,undefined']
    subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    *flags, str(path / 'texture.c'), '-o', str(path / 'texture')], check=True)
    subprocess.run([str(path / 'texture')], check=True)
