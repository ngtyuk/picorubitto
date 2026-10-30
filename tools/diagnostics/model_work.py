#!/usr/bin/env python3
"""Count production Model work on the host; these counts are not RP2350 timings."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--no-model-shared', action='store_true')
args = parser.parse_args()

root = Path(__file__).resolve().parents[2]
picoruby = Path(os.environ.get('PICORUBY_ROOT', root / 'deps/picoruby'))
mruby = picoruby / 'mrbgems/picoruby-mruby/lib/mruby'
build = root / 'build/tests/minimum-profile'
library = build / 'minimum-profile/lib/libmruby.a'
if not library.exists():
    subprocess.run(['python3', str(root / 'tests/native/minimum_profile_regression.py')], check=True)

platform = (root / 'src/game_platform.c').read_text()
model = (root / 'mrbgems/picoruby-game/src/mruby/model.c').read_text()
prefix = platform[platform.index('struct game_3d_camera {'):platform.index('struct game_font {')]
renderer = platform[platform.index('\nbool\ngame_3d_supported('):platform.index('\nbool\ngame_display_fill_rect(')]


def instrument(text, function, counter):
    start = text.index(function + '(')
    brace = text.index('\n{', start) + 2
    return text[:brace] + '\n  ' + counter + ';' + text[brace:]


for function in ['game_3d_transform_vertex_fixed', 'game_3d_transform_vertex']:
    renderer = instrument(renderer, function, 'audit_camera_vertices++')
for function in ['game_3d_project_fixed_vertex', 'game_3d_project_vertex']:
    renderer = instrument(renderer, function, 'audit_projections++')
# model_transform has its opening brace on the following line too.
model = instrument(model, 'model_transform', 'audit_model_vertices += model->vertices')

header = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mruby.h>
#include <mruby/irep.h>
#include "game_platform.h"
#define GAME_TFT_CORE1 1
#define GAME_3D_DEPTH_SRAM 0
#define GAME_3D_LINE_CLIP 1
#define GAME_3D_PROJECTION_CACHE 1
static unsigned audit_camera_vertices, audit_projections, audit_model_vertices, audit_dirty;
static bool tft_ready=true;
static uint8_t framebuffer[GAME_FRAMEBUFFER_BYTES];
static void display_dirty_add(int x,int y,int w,int h) { audit_dirty++; }
'''
harness = r'''
static char scene[64];
static mrb_value audit_begin(mrb_state *mrb,mrb_value self) {
  const char *name; mrb_get_args(mrb,"z",&name);
  snprintf(scene,sizeof(scene),"%s",name);
  static uint16_t depth[320*240];
  static struct game_3d_fixed_vertex vertices[256];
  static struct game_3d_projection_entry cache[256];
  game_3d_depth_buffer=depth; game_3d_indexed_vertices=vertices;
  game_3d_projections=cache; game_3d_frame_active=false;
  assert(game_3d_set_resolution(160,120));
  assert(game_3d_set_viewport(0,0,320,240));
  assert(game_3d_set_camera(0,0,0,0,0,70,0.1,100));
  assert(game_3d_begin_frame());
  memset(framebuffer,0,sizeof(framebuffer));
  audit_camera_vertices=audit_projections=audit_model_vertices=audit_dirty=0;
  return mrb_nil_value();
}
static mrb_value audit_end(mrb_state *mrb,mrb_value self) {
  unsigned pixels=0;
  for(size_t i=0;i<sizeof(framebuffer);i+=2) if(framebuffer[i]||framebuffer[i+1]) pixels++;
  printf("{\"scene\":\"%s\",\"model_vertices\":%u,\"camera_vertices\":%u,"
         "\"projections\":%u,\"dirty_rects\":%u,\"colored_output_pixels\":%u}\n",
         scene,audit_model_vertices,audit_camera_vertices,audit_projections,audit_dirty,pixels);
  game_3d_frame_active=false;
  return mrb_nil_value();
}
int main(int argc,char **argv) {
  assert(argc==2);
  FILE *file=fopen(argv[1],"rb"); assert(file);
  assert(!fseek(file,0,SEEK_END)); long size=ftell(file); assert(size>0); rewind(file);
  void *rom=malloc((size_t)size); assert(rom);
  assert(fread(rom,1,(size_t)size,file)==(size_t)size); fclose(file);
  mrb_state *mrb=mrb_open(); assert(mrb);
  struct RClass *game=mrb_define_module(mrb,"Game");
  struct RClass *three=mrb_define_module_under(mrb,game,"ThreeD");
  game_model_init(mrb,three);
  struct RClass *audit=mrb_define_module(mrb,"Audit");
  mrb_define_module_function(mrb,audit,"begin",audit_begin,MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb,audit,"end",audit_end,MRB_ARGS_NONE());
  mrb_load_irep_buf(mrb,rom,(size_t)size);
  int failed=mrb->exc!=NULL;
  if(failed) mrb_print_error(mrb);
  mrb_close(mrb); free(rom);
  return failed;
}
'''
ruby = r'''
# One 128-vertex fan, 127 front-facing triangles, 128 edges. Both primitive
# types reference every vertex, so repeated transform/projection is visible.
vertices = [0.0,0.0,0.0]
127.times do |index|
  angle = index * 2.0 * Math::PI / 127
  vertices << Math.cos(angle) << Math.sin(angle) << 0.0
end
indices = []
edges = []
127.times do |index|
  a = index + 1
  b = a == 127 ? 1 : a + 1
  indices << 0 << a << b
  edges << a << b
end
edges << 0 << 1
colors = Array.new(127,0x07e0)
filled = Game::ThreeD::Model.new(vertices,indices,colors)
wire = Game::ThreeD::Model.new(vertices,[],[],edges,0xffff)
both = Game::ThreeD::Model.new(vertices,indices,colors,edges,0xffff)
pose = [0,0,4,0,0,0,1,1,1]
[['visible_mesh',filled,pose],['visible_wire',wire,pose],['visible_both',both,pose],
 ['outside_right_both',both,[1000,0,4,0,0,0,1,1,1]],
 ['behind_camera_both',both,[0,0,-4,0,0,0,1,1,1]],
 ['beyond_far_both',both,[0,0,120,0,0,0,1,1,1]],
 ['outside_right_128',both,Array.new(128){[1000,0,4,0,0,0,1,1,1]}.flatten]].each do |item|
  Audit.begin(item[0])
  item[1].draw_instances(item[2])
  Audit.end
end
'''

with tempfile.TemporaryDirectory(prefix='game-model-work-') as temporary:
    directory = Path(temporary)
    source = directory / 'audit.c'
    source.write_text(f'#define GAME_3D_MODEL_SHARED {int(not args.no_model_shared)}\n' +
                      header + prefix + renderer + model + harness)
    fixture = directory / 'fixture.rb'
    fixture.write_text(ruby)
    rom = directory / 'fixture.mrb'
    subprocess.run([str(build / 'mrbc/default/bin/mrbc'), '-o', str(rom), str(fixture)], check=True)
    binary = directory / 'audit'
    subprocess.run([os.environ.get('CC', 'cc'), '-O2', '-g', '-fsanitize=address,undefined',
                    '-DMRB_INT64', '-DMRB_NO_BOXING', '-DMRB_UTF8_STRING',
                    '-I'+str(mruby/'include'), '-I'+str(build/'minimum-profile/include'),
                    '-I'+str(root/'include'), str(source), str(library), '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(rom)], check=True, timeout=120)
