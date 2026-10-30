#!/usr/bin/env python3
"""Exercise production WAV allocation limits and failure cleanup on the host."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/game_platform.c').read_text()
def part(start, end):
    at = source.index(start)
    return source[at:source.index(end, at) + len(end)]
def function(name):
    at = source.index('\n' + name + '(')
    return source[at:source.index('\n}', at) + 2]

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define GAME_AUDIO_MAX_WAV_BYTES (1024u * 1024u)
static bool sd_ready=true;
static size_t requested, allocations, asset_resident_bytes;
typedef struct game_audio_sound game_audio_sound_t;
'''
code += part('struct game_audio_sound {', '\n};')
code += r'''
static game_audio_sound_t *loaded_sounds;
static bool game_storage_file_size(const char *path, size_t *size) { (void)path; *size=requested; return true; }
static void *asset_alloc(size_t size) { allocations++; return calloc(1,size); }
static void asset_free(void *ptr) { free(ptr); }
static bool game_storage_read_file(const char *path, uint8_t *data, size_t cap, size_t *loaded) { (void)path; (void)data; *loaded=cap; return true; }
static bool audio_parse_wav(const uint8_t *data, size_t size, const uint8_t **samples, size_t *count, uint32_t *rate) { *samples=data; *count=size; *rate=22050; return true; }
'''
code += '\ngame_audio_sound_t *\n' + function('game_audio_sound_load')
code += r'''
int main(void) {
  requested=1024u*1024u; game_audio_sound_t *s=game_audio_sound_load("x.wav");
  assert(s && s->wav_bytes==requested && asset_resident_bytes==requested && allocations==2);
  free(s->wav_data); free(s); loaded_sounds=NULL;
  allocations=0; requested=1024u*1024u+1;
  assert(!game_audio_sound_load("x.wav") && allocations==0);
  requested=0; assert(!game_audio_sound_load("x.wav") && allocations==0);
  puts("BGM WAV allocation limit: PASS 1 MiB accepted, larger rejected");
}
'''
with tempfile.TemporaryDirectory(prefix='audio-size-') as tmp:
    file = Path(tmp) / 'test.c'; file.write_text(code)
    exe = Path(tmp) / 'test'
    subprocess.run([os.environ.get('CC','cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(file), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
