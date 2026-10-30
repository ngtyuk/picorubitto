#!/usr/bin/env python3
"""Run the production model binding on matching mruby; capture renderer input."""
import os
import json
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
picoruby = Path(os.environ.get('PICORUBY_ROOT', root / 'deps/picoruby'))
mruby = picoruby / 'mrbgems/picoruby-mruby/lib/mruby'
build = root / 'build/tests/minimum-profile'
library = build / 'minimum-profile/lib/libmruby.a'
if not library.exists():
    subprocess.run(['python3', str(root / 'tests/native/minimum_profile_regression.py')], check=True)

harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mruby.h>
#include <mruby/array.h>
#include <mruby/irep.h>
#include "game_platform.h"
void game_model_init(mrb_state *, struct RClass *);
size_t game_model_resident_bytes(void);
static bool active;
static unsigned calls;
static float captured[256*3];
static size_t captured_count;
static uint16_t signature[12];
bool game_3d_draw_indexed_mesh(const float *v,size_t nv,const uint16_t *i,size_t nt,const uint16_t *c) {
  if (!active) return false;
  if (nt) {
    calls++; captured_count=nv*3; memcpy(captured,v,nv*3*sizeof(float));
    signature[0]=i[0]; signature[1]=i[1]; signature[2]=i[2]; signature[3]=c[0];
  }
  return true;
}
bool game_3d_draw_wireframe(const float *v,size_t nv,const uint16_t *e,size_t ne,uint16_t color,bool depth) {
  if (!active) return false;
  calls++; captured_count=nv*3; memcpy(captured,v,nv*3*sizeof(float));
  if (ne>=3) memcpy(signature+4,e,6*sizeof(uint16_t));
  signature[10]=color; signature[11]=depth;
  return true;
}
bool game_3d_draw_model_geometry(const float *v,size_t nv,const uint16_t *i,size_t nt,
                               const uint16_t *c,const uint16_t *e,size_t ne,
                               uint16_t color,bool depth) {
  return game_3d_draw_indexed_mesh(v,nv,i,nt,c) &&
    (ne==0 || game_3d_draw_wireframe(v,nv,e,ne,color,depth));
}
static mrb_value start(mrb_state *m,mrb_value s) { active=true; return mrb_nil_value(); }
static mrb_value end(mrb_state *m,mrb_value s) { active=false; return mrb_nil_value(); }
static mrb_value vertices(mrb_state *m,mrb_value s) {
  mrb_value result=mrb_ary_new_capa(m,captured_count);
  for(size_t i=0;i<captured_count;i++) mrb_ary_push(m,result,mrb_float_value(m,captured[i]));
  return result;
}
static mrb_value sig(mrb_state *m,mrb_value s) {
  mrb_value result=mrb_ary_new_capa(m,12);
  for(size_t i=0;i<12;i++) mrb_ary_push(m,result,mrb_int_value(m,signature[i]));
  return result;
}
static mrb_value bytes(mrb_state *m,mrb_value s) { return mrb_int_value(m,game_model_resident_bytes()); }
static mrb_value count(mrb_state *m,mrb_value s) { return mrb_int_value(m,calls); }
int main(int argc,char **argv) {
  assert(argc==2);
  FILE *f=fopen(argv[1],"rb"); assert(f); fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
  unsigned char *rom=malloc(size); assert(rom); assert(fread(rom,1,size,f)==(size_t)size); fclose(f);
  mrb_state *m=mrb_open(); assert(m);
  struct RClass *game=mrb_define_module(m,"Game");
  struct RClass *three=mrb_define_module_under(m,game,"ThreeD");
  game_model_init(m,three);
  mrb_define_module_function(m,three,"begin_frame",start,MRB_ARGS_NONE());
  mrb_define_module_function(m,three,"end_frame",end,MRB_ARGS_NONE());
  struct RClass *test=mrb_define_module(m,"ModelTest");
  mrb_define_module_function(m,test,"vertices",vertices,MRB_ARGS_NONE());
  mrb_define_module_function(m,test,"signature",sig,MRB_ARGS_NONE());
  mrb_define_module_function(m,test,"bytes",bytes,MRB_ARGS_NONE());
  mrb_define_module_function(m,test,"calls",count,MRB_ARGS_NONE());
  mrb_load_irep_buf(m,rom,size);
  int result=m->exc!=NULL;
  if(result) mrb_print_error(m);
  mrb_close(m); free(rom); assert(game_model_resident_bytes()==0);
  return result;
}
'''
with tempfile.TemporaryDirectory(prefix='game-model-test-') as temporary:
    directory = Path(temporary)
    source = directory / 'main.c'
    source.write_text(harness)
    rom = directory / 'checks.mrb'
    subprocess.run([str(build / 'mrbc/default/bin/mrbc'), '-o', str(rom),
                    str(root / 'tests/native/model_checks.rb')], check=True)
    binary = directory / 'model-test'
    subprocess.run([os.environ.get('CC', 'cc'), '-O2', '-g', '-fsanitize=address,undefined',
                    '-DMRB_INT64', '-DMRB_NO_BOXING', '-DMRB_UTF8_STRING',
                    '-I'+str(mruby/'include'), '-I'+str(build/'minimum-profile/include'),
                    '-I'+str(root/'include'), str(source),
                    str(root/'mrbgems/picoruby-game/src/mruby/model.c'), str(library),
                    '-lm', '-o', str(binary)], check=True)
    run = subprocess.run([str(binary), str(rom)], check=True, capture_output=True, text=True)
    cases = []
    for line in run.stdout.splitlines():
        if line.startswith('MODEL_CASE '):
            pose, vertices = line.removeprefix('MODEL_CASE ').split('|')
            cases.append({'transform': [float(v) for v in pose.split(',')],
                          'vertices': [float(v) for v in vertices.split(',')]})
        else:
            print(line)
    (root/'build/tests/model-cases.json').write_text(json.dumps(cases)+'\n')

    # Retained transforms can move only a suffix past the fixed-point limit.
    # Exercise the production wireframe fallback, including its earlier prefix.
    platform = (root/'src/game_platform.c').read_text()
    start = platform.index('\ngame_3d_draw_wireframe(')+1
    end = platform.index('\n}',start)+2
    fallback = r'''
#include <assert.h>
#include <math.h>
#include "game_platform.h"
struct vertex { float x,y,z; };
static bool game_3d_frame_active=true;
static struct vertex fixed[256], game_3d_wireframe_float_vertices[256];
static struct vertex *game_3d_indexed_vertices=fixed;
static struct vertex game_3d_camera;
static unsigned rendered;
static void game_3d_projection_begin(void) { }
static bool game_3d_supported(void) { return true; }
static bool game_3d_fixed_value_supported(float v) { return fabsf(v)<=32768; }
static struct vertex game_3d_transform_vertex_fixed(float x,float y,float z) { return (struct vertex){x,y,z}; }
static struct vertex game_3d_transform_vertex(float x,float y,float z) { return (struct vertex){x,y,z}; }
static void game_3d_render_fixed_line(const struct vertex *a,const struct vertex *b,uint16_t c,bool d) { assert(0); }
static void game_3d_render_float_line(const struct vertex *a,const struct vertex *b,uint16_t c,bool d) {
  assert(a->x==1 && a->y==2 && a->z==3 && b->x==40000 && b->y==5 && b->z==6); rendered++;
}
'''.replace('static bool game_3d_supported', 'bool game_3d_supported')
    fallback += '\nbool\n'+platform[start:end]+r'''
int main(void) {
  float vertices[]={1,2,3,40000,5,6}; uint16_t edges[]={0,1};
  assert(game_3d_draw_wireframe(vertices,2,edges,1,0xffff,true)); assert(rendered==1);
  return 0;
}
'''
    source.write_text(fallback)
    subprocess.run([os.environ.get('CC','cc'),'-O2','-fsanitize=address,undefined',
                    '-I'+str(root/'include'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    print('wireframe mixed-range fallback: PASS')
