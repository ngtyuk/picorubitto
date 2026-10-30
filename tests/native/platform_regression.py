#!/usr/bin/env python3
"""Run production input, dirty-rectangle and asset loaders against host fixtures."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/game_platform.c').read_text()


def function(name, result):
    start = source.index('\n' + name + '(') + 1
    end = source.index('\n}', start) + 2
    return '\n' + result + '\n' + source[start:end] + '\n'


def structure(name):
    start = source.index('struct ' + name + ' {')
    end = source.index('\n};', start) + 3
    return source[start:end] + '\n'


harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "game_platform.h"
#include "estalloc.h"
static void game_audio_dial_poll(void) {}
typedef unsigned int uint;
static uint32_t save_and_disable_interrupts(void) { return 0; }
static void restore_interrupts(uint32_t state) { (void)state; }
#define INPUT_COUNT 10
#define INPUT_DEBOUNCE_US 5000u
#define GAME_DISPLAY_WIDTH 320u
#define GAME_DISPLAY_HEIGHT 240u
#define GAME_DIRTY_RECT_LIMIT 64u
#define GAME_DIRTY_RECT_MERGE_EXTRA_PIXELS 256u
#define GAME_SPRITE_CACHE_BYTES 2048u
#define GAME_AUDIO_MAX_VOICES 4u
#define GAME_AUDIO_MAX_WAV_BYTES (128u * 1024u)
#define GAME_FONT_HEADER_BYTES 16u
#define GAME_FONT_MAX_BYTES (512u * 1024u)
static uint8_t arena[65536] __attribute__((aligned(8)));
#define GAME_MEDIA_END_ADDRESS ((uintptr_t)(arena + sizeof(arena)))
static uint8_t *media_next = arena;
static uint8_t sprite_cache[GAME_SPRITE_CACHE_BYTES] __attribute__((aligned(8)));
static size_t sprite_cache_next;
static ESTALLOC *asset_heap, *sprite_heap;
static size_t asset_resident_bytes;
static uint32_t input_stable, input_candidate, input_changed_at[INPUT_COUNT];
static uint32_t input_frame_state, input_previous_state;
static bool input_timer_ready = true;
static const uint8_t button_pins[INPUT_COUNT] = {0,1,2,3,4,5,6,7,8,9};
static uint32_t now_us, pins;
static uint32_t time_us_32(void) { return now_us; }
static bool gpio_get(uint8_t pin) { return !(pins & (1u << pin)); }
static bool sd_ready = true, fail_read, invalid_format, short_read;
static size_t fixture_size = 32;
static unsigned reads;
bool game_storage_file_size(const char *path, size_t *size) {
  if (!path || !sd_ready) return false;
  *size = fixture_size; return true;
}
bool game_storage_read_file(const char *path, uint8_t *data, size_t size, size_t *loaded) {
  (void)path; reads++; memset(data, 0, size); *loaded = short_read ? size - 1 : size;
  return !fail_read;
}
static bool font_validate(const uint8_t *data, size_t size) {
  (void)data; (void)size; return !invalid_format;
}
static uint16_t font_be16(const uint8_t *data) { return (data[0] << 8) | data[1]; }
static bool audio_parse_wav(const uint8_t *data, size_t size, const uint8_t **samples,
                             size_t *sample_size, uint32_t *rate) {
  *samples = data; *sample_size = size; *rate = 22050; return !invalid_format;
}
'''
for name in ['game_dirty_rect', 'game_sprite', 'game_texture', 'game_font',
             'game_audio_sound']:
    harness += structure(name)
for name in ['sprite', 'texture', 'font', 'audio_sound']:
    harness += f'typedef struct game_{name} game_{name}_t;\n'
harness += structure('game_audio_voice')
harness += 'static struct game_audio_voice audio_voices[4];\nstatic game_audio_sound_t *loaded_sounds;\n'
harness += structure('game_audio_cached_sound')
harness += 'static struct game_audio_cached_sound *audio_cached_sounds;\n'
for name, result in [
    ('input_poll_state', 'static uint32_t'),
    ('game_input_begin_frame', 'void'),
    ('game_input_just_pressed', 'bool'),
    ('game_input_just_released', 'bool'),
    ('display_dirty_should_merge', 'static bool'),
    ('display_dirty_add_to', 'static void'),
    ('media_alloc', 'static void *'),
    ('asset_alloc', 'static void *'),
    ('asset_free', 'static void'),
    ('sprite_cache_free', 'static void'),
    ('sprite_cache_alloc', 'static uint8_t *'),
    ('resolve_asset_path', 'static bool'),
    ('game_sprite_load', 'game_sprite_t *'),
    ('game_texture_load', 'game_texture_t *'),
    ('game_texture_checksum', 'uint32_t'),
    ('game_font_load', 'game_font_t *'),
    ('game_audio_sound_load', 'game_audio_sound_t *'),
    ('game_audio_sound_load_cached', 'game_audio_sound_t *'),
    ('game_sprite_release', 'void'),
    ('game_texture_release', 'void'),
    ('game_font_release', 'void'),
    ('audio_sound_in_use', 'static bool'),
    ('game_assets_sweep', 'void'),
    ('game_audio_sound_release', 'void'),
    ('game_assets_collect', 'void'),
    ('game_assets_resident_bytes', 'size_t'),
]:
    harness += function(name, result)
harness += r'''
static void input_test(void) {
  pins = 1; now_us = 1000; input_poll_state();
  /* Another button bouncing must not delay button 0's acceptance. */
  for (now_us = 2000; now_us <= 6000; now_us += 1000) {
    pins ^= 2; input_poll_state();
  }
  assert(input_stable == 1 && input_frame_state == 0);
  game_input_begin_frame();
  assert(input_frame_state == 1 && game_input_just_pressed(1));
  game_input_begin_frame(); assert(!game_input_just_pressed(1));
  pins = 0; now_us = 10000; input_poll_state();
  now_us = 14999; input_poll_state(); assert(input_stable == 1);
  now_us = 15000; input_poll_state(); game_input_begin_frame();
  assert(game_input_just_released(1));
  /* Unsigned time subtraction must work across the 32-bit timer wrap. */
  now_us = UINT32_MAX - 2000; pins = 4; input_poll_state();
  now_us += 5000; input_poll_state(); assert(input_stable == 4);
  input_timer_ready = false;
  pins = 0; now_us += 1000; game_input_begin_frame();
  now_us += 5000; game_input_begin_frame(); assert(input_frame_state == 0);
}
static bool covers(const struct game_dirty_rect *r, size_t count, int x, int y) {
  for (size_t i = 0; i < count; i++)
    if (x >= r[i].x0 && x <= r[i].x1 && y >= r[i].y0 && y <= r[i].y1) return true;
  return false;
}
static void dirty_test(void) {
  struct game_dirty_rect r[64]; size_t count = 0;
  display_dirty_add_to(r, &count, 0, 100, 320, 1);
  display_dirty_add_to(r, &count, 100, 0, 1, 240);
  assert(count == 2);
  for (int y = 0; y < 240; y++) assert(covers(r, count, 100, y));
  for (int x = 0; x < 320; x++) assert(covers(r, count, x, 100));
  count = 0;
  display_dirty_add_to(r, &count, 10, 10, 20, 20);
  display_dirty_add_to(r, &count, 15, 15, 5, 5); assert(count == 1);
  display_dirty_add_to(r, &count, 31, 10, 20, 20); assert(count == 1);
  count = 0;
  display_dirty_add_to(r, &count, -5, -5, 10, 10);
  assert(count == 1 && r[0].x0 == 0 && r[0].y0 == 0 && r[0].x1 == 4);
  display_dirty_add_to(r, &count, INT_MAX, INT_MAX, INT_MAX, INT_MAX);
  assert(count == 1);
  /* A fragmented set that cannot merge must fall back without losing pixels. */
  count = 64;
  for (size_t i = 0; i < count; i++) r[i] = (struct game_dirty_rect){0,0,0,0};
  display_dirty_add_to(r, &count, 319, 239, 1, 1);
  assert(count == 1 && covers(r, count, 0, 0) && covers(r, count, 319, 239));
}
static void failed_loads(void) {
  ESTALLOC_STAT before = {0};
  if (asset_heap) { est_take_statistics(asset_heap); before = asset_heap->stat; }
  assert(!game_sprite_load("bad.raw", 4, 4, 1, 0xf81f));
  assert(!game_texture_load("bad.raw", 4, 4, 0xf81f));
  assert(!game_font_load("bad.pbf"));
  assert(!game_audio_sound_load_cached("bad.wav"));
  assert(game_assets_resident_bytes() == 0 && audio_cached_sounds == NULL);
  est_take_statistics(asset_heap);
  if (before.total) assert(asset_heap->stat.free == before.free);
}
static void asset_test(void) {
  uint8_t *permanent = media_alloc(1024, 8); assert(permanent); memset(permanent, 0xa5, 1024);
  void *init = asset_alloc(1); assert(init); asset_free(init);
  assert(!media_alloc(1, 8));
  fail_read = true;
  for (int i = 0; i < 3; i++) failed_loads();
  fail_read = false; short_read = true; failed_loads(); short_read = false;
  invalid_format = true;
  assert(!game_font_load("bad.pbf"));
  assert(!game_audio_sound_load_cached("bad.wav"));
  assert(game_assets_resident_bytes() == 0);
  invalid_format = false;
  est_take_statistics(asset_heap); size_t initial_free = asset_heap->stat.free;
  /* Allocation failure after a descriptor must not leak that descriptor. */
  void *full = asset_alloc(asset_heap->stat.max_free - 128);
  assert(full); fixture_size = 1024;
  assert(!game_texture_load("large.raw", 32, 16, 0xf81f));
  asset_free(full); fixture_size = 32;
  est_take_statistics(asset_heap); assert(asset_heap->stat.free == initial_free);
  game_sprite_t *sprite = game_sprite_load("sprite.raw", 4, 4, 1, 0xf81f);
  assert(sprite && sprite->cached);
  game_texture_t *held = game_texture_load("texture.raw", 4, 4, 0xf81f);
  game_font_t *font = game_font_load("font.pbf"); assert(held && font);
  held->pixels[0] = 0x12;
  assert(game_texture_checksum(held) == UINT32_C(3689174359));
  size_t baseline = game_assets_resident_bytes();
  for (int i = 0; i < 1024; i++) {
    fixture_size = 32768;
    game_texture_t *texture = game_texture_load("scene.raw", 128, 128, 0xf81f);
    assert(texture && texture->bytes == 32768); game_texture_release(texture);
    fixture_size = 4096;
    game_sprite_t *large = game_sprite_load("large.raw", 32, 32, 2, 0xf81f);
    assert(large && !large->cached); game_sprite_release(large);
    fixture_size = 32;
    game_font_t *next = game_font_load("font.pbf"); assert(next); game_font_release(next);
    assert(game_assets_resident_bytes() == baseline && held->pixels[0] == 0x12);
    assert(permanent[0] == 0xa5 && permanent[1023] == 0xa5);
  }
  /* One sound remains alive after its Ruby owner disappears during playback. */
  game_audio_sound_t *sound = game_audio_sound_load("tone.wav"); assert(sound);
  audio_voices[0].sound = sound; audio_voices[0].playing = true;
  audio_voices[1].sound = sound; audio_voices[1].playing = true;
  game_audio_sound_release(sound);
  assert(game_assets_resident_bytes() == baseline + 32 && loaded_sounds == sound);
  audio_voices[0].playing = false; game_assets_sweep();
  assert(game_assets_resident_bytes() == baseline + 32);
  audio_voices[1].playing = false; game_assets_sweep();
  assert(game_assets_resident_bytes() == baseline && audio_voices[0].sound == NULL);
  sound = game_audio_sound_load_cached("tone.wav"); assert(sound);
  unsigned loaded = reads;
  for (int i = 0; i < 1000; i++) assert(game_audio_sound_load_cached("tone.wav") == sound);
  assert(reads == loaded);
  for (int i = 0; i < 100; i++) {
    char filename[32]; snprintf(filename, sizeof(filename), "tone%d.wav", i);
    assert(game_audio_sound_load_cached(filename));
    assert(game_assets_resident_bytes() <= baseline + 8 * 32);
  }
  game_assets_collect(); assert(game_assets_resident_bytes() == baseline);
  game_sprite_release(sprite); game_texture_release(held); game_font_release(font);
  assert(game_assets_resident_bytes() == 0);
  est_take_statistics(asset_heap); assert(asset_heap->stat.free == initial_free);
}
int main(void) {
  input_test(); dirty_test(); asset_test();
  puts("platform regression: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='game-platform-test-') as directory:
    path = Path(directory)
    (path / 'platform.c').write_text(harness)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(root / 'include'),
                    '-I' + str(root / 'deps/picoruby/mrbgems/picoruby-machine/lib/estalloc'),
                    str(root / 'deps/picoruby/mrbgems/picoruby-machine/lib/estalloc/estalloc.c'), str(path / 'platform.c'),
                    '-o', str(path / 'platform')], check=True)
    subprocess.run([str(path / 'platform')], check=True)
