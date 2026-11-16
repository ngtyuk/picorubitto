#!/usr/bin/env python3
"""Run Texture#draw through real mruby argument parsing and production drawing.

Only the TFT transport and texture allocation are host fixtures. Full framebuffer
comparisons cover defaults, explicit scaling, clipping, transparency and errors.
Also emit fixtures for tests/web/texture-draw.html under build/tests/.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binding-source', type=Path,
                    default=root / 'mrbgems/picoruby-game/src/mruby/game.c')
args = parser.parse_args()
picoruby = Path(os.environ.get('PICORUBY_ROOT', root / 'deps/picoruby'))
mruby = picoruby / 'mrbgems/picoruby-mruby/lib/mruby'
build = root / 'build/tests/minimum-profile'
library = build / 'minimum-profile/lib/libmruby.a'
if not library.exists():
    subprocess.run(['python3', str(root / 'tests/native/minimum_profile_regression.py')], check=True)

cases = []
for mode, key in [('keyed', 0xf81f), ('opaque', 0xf81f), ('custom', 0x1234)]:
    for name, values, source, destination in [
        ('whole-2', [10, 20], [0, 0, 512, 24], [512, 24]),
        ('crop-6', [10, 20, 96, 0, 32, 24], [96, 0, 32, 24], [32, 24]),
        ('crop-both-6', [10, 20, 96, 3, 32, 7], [96, 3, 32, 7], [32, 7]),
        ('width-only-7', [10, 20, 96, 3, 32, 7, 64], [96, 3, 32, 7], [64, 7]),
        ('explicit-8', [10, 20, 96, 3, 32, 7, 32, 7], [96, 3, 32, 7], [32, 7]),
        ('enlarge-8', [10, 20, 96, 3, 32, 7, 64, 14], [96, 3, 32, 7], [64, 14]),
        ('shrink-8', [10, 20, 96, 3, 32, 8, 16, 4], [96, 3, 32, 8], [16, 4]),
        ('fractional-8', [10, 20, 96, 3, 32, 7, 53, 19], [96, 3, 32, 7], [53, 19]),
        ('left-top-6', [-7, -4, 96, 3, 32, 7], [96, 3, 32, 7], [32, 7]),
        ('right-bottom-6', [311, 237, 480, 17, 32, 7], [480, 17, 32, 7], [32, 7]),
        ('clipped-scale-7', [-7, 237, 96, 3, 32, 7, 64], [96, 3, 32, 7], [64, 7]),
        ('offscreen-2', [320, 240], [0, 0, 512, 24], [512, 24]),
        ('source-defaults-3', [10, 20, 0], [0, 0, 512, 24], [512, 24]),
        ('source-defaults-4', [10, 20, 0, 0], [0, 0, 512, 24], [512, 24]),
        ('height-default-5', [10, 20, 96, 0, 32], [96, 0, 32, 24], [32, 24]),
    ]:
        cases.append(dict(name=f'{mode}/{name}', args=values, source=source,
                          destination=destination, mode=mode, key=key, error=None))

for name, values, error in [
    ('source-negative-x', [0, 0, -1, 0, 32, 7], 'ArgumentError'),
    ('source-negative-y', [0, 0, 0, -1, 32, 7], 'ArgumentError'),
    ('source-zero-width', [0, 0, 0, 0, 0, 7], 'ArgumentError'),
    ('source-zero-height', [0, 0, 0, 0, 32, 0], 'ArgumentError'),
    ('source-past-width', [0, 0, 481, 0, 32, 7], 'ArgumentError'),
    ('source-past-height', [0, 0, 0, 18, 32, 7], 'ArgumentError'),
    ('source-x-default-width', [0, 0, 1], 'ArgumentError'),
    ('source-y-default-height', [0, 0, 0, 1], 'ArgumentError'),
    ('destination-zero-width', [0, 0, 96, 3, 32, 7, 0], 'ArgumentError'),
    ('destination-zero-height', [0, 0, 96, 3, 32, 7, 64, 0], 'ArgumentError'),
    ('destination-negative-width', [0, 0, 96, 3, 32, 7, -1], 'ArgumentError'),
    ('destination-negative-height', [0, 0, 96, 3, 32, 7, 64, -1], 'ArgumentError'),
    ('destination-overflow', [0, 0, 96, 3, 32, 7, 2147483648], 'ArgumentError'),
    ('position-overflow', [2147483648, 0], 'ArgumentError'),
    ('too-few', [0], 'ArgumentError'),
    ('too-many', [0] * 9, 'ArgumentError'),
    ('nil-is-explicit-width', [0, 0, 96, 3, 32, 7, None], 'TypeError'),
    ('nil-is-explicit-height', [0, 0, 96, 3, 32, 7, 64, None], 'TypeError'),
]:
    cases.append(dict(name=name, args=values, source=[0, 0, 0, 0],
                      destination=[0, 0], mode='keyed', key=0xf81f, error=error))

platform = (root / 'src/game_platform.c').read_text()
binding = args.binding_source.read_text()


def function(text, name, result):
    start = text.index('\n' + name + '(') + 1
    end = text.index('\n}', start) + 2
    return '\n' + result + '\n' + text[start:end] + '\n'


def structure(name):
    start = platform.index('struct ' + name + ' {')
    end = platform.index('\n};', start) + 3
    return platform[start:end] + '\n'


harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/irep.h>
#define GAME_DISPLAY_WIDTH 320u
#define GAME_DISPLAY_HEIGHT 240u
#define FRAME_BYTES (320u*240u*2u)
static uint8_t framebuffer[FRAME_BYTES], expected[FRAME_BYTES];
static bool tft_ready = true;
static int dirty[4], dirty_calls;
static void display_dirty_add(int x, int y, int w, int h) {
  dirty_calls++; dirty[0]=x; dirty[1]=y; dirty[2]=w; dirty[3]=h;
}
'''
harness += structure('game_texture') + 'typedef struct game_texture game_texture_t;\n'
harness += structure('game_texture_sample_axis')
for name, result in [
    ('draw_sprite_transparent_row', 'static void'),
    ('texture_sample_axis', 'static struct game_texture_sample_axis'),
    ('texture_sample_advance', 'static void'),
    ('draw_texture_scaled', 'static void'),
    ('game_display_draw_texture', 'bool'),
]:
    harness += function(platform, name, result)
harness += r'''
static const struct mrb_data_type game_texture_type = {"Game::Texture", NULL};
static uint16_t game_texture_width(const game_texture_t *t) { return t->width; }
static uint16_t game_texture_height(const game_texture_t *t) { return t->height; }
'''
harness += function(binding, 'texture_draw', 'static mrb_value')
harness += r'''
struct draw_case {
  const char *name;
  int x,y,sx,sy,sw,sh,dw,dh;
  bool invalid,opaque;
  uint16_t key;
};
static const struct draw_case cases[] = {
'''
for case in cases:
    xy = [0, 0] if case['error'] else (case['args'] + [0, 0])[:2]
    fields = [case['name'], *xy, *case['source'], *case['destination'],
              bool(case['error']), case['mode'] == 'opaque', case['key']]
    harness += '  {' + ','.join(json.dumps(value) for value in fields) + '},\n'
harness += r'''
};
static game_texture_t texture;
static mrb_value begin_case(mrb_state *mrb, mrb_value self) {
  (void)self;
  mrb_int index; mrb_get_args(mrb,"i",&index);
  assert(index>=0 && (size_t)index<sizeof(cases)/sizeof(cases[0]));
  const struct draw_case *c=&cases[index];
  texture.opaque=c->opaque; texture.key_color=c->key;
  for(size_t i=0;i<texture.bytes/2;i++) {
    uint16_t color=(uint16_t)(i*719u+123u);
    if(color==texture.key_color)color^=1;
    if(!texture.opaque && i%5==0)color=texture.key_color;
    texture.pixels[i*2]=color>>8; texture.pixels[i*2+1]=color&255;
  }
  for(size_t i=0;i<FRAME_BYTES;i++)framebuffer[i]=(uint8_t)(i*17u+i/640u*31u);
  memcpy(expected,framebuffer,FRAME_BYTES); dirty_calls=0;
  if(!c->invalid) {
    for(int y=0;y<240;y++)for(int x=0;x<320;x++) {
      int64_t rx=(int64_t)x-c->x, ry=(int64_t)y-c->y;
      if(rx<0||rx>=c->dw||ry<0||ry>=c->dh)continue;
      size_t sx=(size_t)(c->sx+rx*c->sw/c->dw),sy=(size_t)(c->sy+ry*c->sh/c->dh);
      size_t from=(sy*texture.width+sx)*2u,to=((size_t)y*320+x)*2u;
      uint16_t color=(uint16_t)((texture.pixels[from]<<8)|texture.pixels[from+1]);
      if(texture.opaque||color!=texture.key_color)memcpy(expected+to,texture.pixels+from,2);
    }
  }
  return mrb_nil_value();
}
static mrb_value verify_case(mrb_state *mrb, mrb_value self) {
  (void)self;
  mrb_int index; mrb_get_args(mrb,"i",&index);
  const struct draw_case *c=&cases[index];
  bool ok=memcmp(expected,framebuffer,FRAME_BYTES)==0 && dirty_calls==!c->invalid;
  if(!c->invalid)ok=ok && dirty[0]==c->x && dirty[1]==c->y && dirty[2]==c->dw && dirty[3]==c->dh;
  if(!ok) {
    fprintf(stderr,"FAIL %s: expected dst=%dx%d, actual=%dx%d; framebuffer/dirty mismatch\n",
            c->name,c->dw,c->dh,dirty[2],dirty[3]);
    mrb_raise(mrb,E_RUNTIME_ERROR,"Texture#draw framebuffer mismatch");
  }
  return mrb_nil_value();
}
int main(int argc,char **argv) {
  assert(argc==2);
  FILE *f=fopen(argv[1],"rb"); assert(f); fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
  unsigned char *rom=malloc((size_t)size); assert(rom);
  assert(fread(rom,1,(size_t)size,f)==(size_t)size); fclose(f);
  texture.width=512; texture.height=24; texture.bytes=512u*24u*2u;
  texture.pixels=malloc(texture.bytes); assert(texture.pixels);
  mrb_state *mrb=mrb_open(); assert(mrb);
  struct RClass *game=mrb_define_module(mrb,"Game");
  struct RClass *klass=mrb_define_class_under(mrb,game,"Texture",mrb->object_class);
  MRB_SET_INSTANCE_TT(klass,MRB_TT_DATA);
  mrb_define_method(mrb,klass,"draw",texture_draw,MRB_ARGS_ARG(2,6));
  mrb_define_global_const(mrb,"TEXTURE",mrb_obj_value(mrb_data_object_alloc(mrb,klass,&texture,&game_texture_type)));
  struct RClass *test=mrb_define_module(mrb,"TextureTest");
  mrb_define_module_function(mrb,test,"begin_case",begin_case,MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb,test,"verify_case",verify_case,MRB_ARGS_REQ(1));
  mrb_load_irep_buf(mrb,rom,(size_t)size);
  int failed=mrb->exc!=NULL;
  if(failed)mrb_print_error(mrb);
  else printf("TEXTURE BINDING PASS %zu (real mruby arguments + production framebuffer)\n",
              sizeof(cases)/sizeof(cases[0]));
  mrb_close(mrb); free(texture.pixels); free(rom);
  return failed;
}
'''

checks = []
for index, case in enumerate(cases):
    values = ','.join('nil' if value is None else str(value) for value in case['args'])
    checks.append(f'TextureTest.begin_case({index})\n')
    if case['error']:
        checks.append(f"caught=false\nbegin\n  TEXTURE.draw({values})\nrescue {case['error']}\n  caught=true\nend\n"
                      f"raise 'expected {case['error']}: {case['name']}' unless caught\n")
    else:
        checks.append(f"raise 'draw return: {case['name']}' unless TEXTURE.draw({values}).nil?\n")
    checks.append(f'TextureTest.verify_case({index})\n')

output = root / 'build/tests/texture-draw'
output.mkdir(parents=True, exist_ok=True)
(output / 'cases.json').write_text(json.dumps(cases) + '\n')
for mode, key in [('keyed', 0xf81f), ('opaque', 0xf81f), ('custom', 0x1234)]:
    pixels = bytearray()
    for index in range(512 * 24):
        color = (index * 719 + 123) & 0xffff
        if color == key:
            color ^= 1
        if mode != 'opaque' and index % 5 == 0:
            color = key
        pixels.extend(color.to_bytes(2, 'big'))
    (output / f'{mode}.raw').write_bytes(pixels)

with tempfile.TemporaryDirectory(prefix='game-texture-binding-') as temporary:
    directory = Path(temporary)
    source = directory / 'main.c'
    source.write_text(harness)
    ruby = directory / 'checks.rb'
    ruby.write_text(''.join(checks))
    rom = directory / 'checks.mrb'
    subprocess.run([str(build / 'mrbc/default/bin/mrbc'), '-o', str(rom), str(ruby)], check=True)
    binary = directory / 'texture-test'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter',
                    '-g', '-fsanitize=address,undefined', '-DMRB_INT64', '-DMRB_NO_BOXING',
                    '-DMRB_UTF8_STRING', '-DMRB_CONSTRAINED_BASELINE_PROFILE=1',
                    '-DPICORB_VM_MRUBY', '-I'+str(mruby / 'include'),
                    '-I'+str(build / 'minimum-profile/include'), str(source), str(library),
                    '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(rom)], check=True)
