#!/usr/bin/env python3
"""Compare Model surface/edge sharing with the previous production renderer."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--triangle-mode', type=int, choices=range(4), default=0)
args = parser.parse_args()
old = subprocess.check_output(['git', 'show', '6acee9e:src/game_platform.c'], cwd=root, text=True)
current = (root / 'src/game_platform.c').read_text()
header = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_platform.h"
static uint8_t framebuffer[GAME_FRAMEBUFFER_BYTES];
static bool tft_ready=true;
static int dirty[8192*4];
static unsigned dirty_count;
static void display_dirty_add(int x,int y,int w,int h) {
  assert(dirty_count<8192);
  int *r=dirty+dirty_count++*4; r[0]=x;r[1]=y;r[2]=w;r[3]=h;
}
'''
legacy = r'''
bool game_3d_draw_model_geometry(const float *v,size_t nv,const uint16_t *i,size_t nt,
                               const uint16_t *c,const uint16_t *e,size_t ne,
                               uint16_t color,bool depth) {
  return game_3d_draw_indexed_mesh(v,nv,i,nt,c) &&
    (ne==0 || game_3d_draw_wireframe(v,nv,e,ne,color,depth));
}
'''
exercise = r'''
struct result { const void *pixels,*depth; const int *dirty; unsigned dirty_count; bool success; };
void RUN(unsigned test,struct result *out) {
  static uint16_t depth[320*240];
  static struct game_3d_fixed_vertex scratch[256];
  static struct game_3d_projection_entry cache[256];
  game_3d_depth_buffer=depth; game_3d_indexed_vertices=scratch;
  game_3d_projections=test%29==0?NULL:cache;
  if(test%31==0) game_3d_projection_generation=UINT32_MAX;
  game_3d_frame_active=false;
  unsigned mode=test%3;
  assert(game_3d_set_resolution(mode==0?160:mode==1?320:13,mode==0?120:mode==1?240:11));
  assert(game_3d_set_viewport(mode==2?17:0,mode==2?23:0,mode==2?211:320,mode==2?173:240));
  assert(game_3d_set_camera(0,0,0,0,0,70,0.1,100));
  memset(framebuffer,0x21,sizeof(framebuffer)); memset(depth,0,sizeof(depth)); dirty_count=0;
  assert(game_3d_begin_frame());
  unsigned count=test%7==0?256:test%5==0?128:8;
  float vertices[256*3]; uint16_t edges[256*4],faces[256*3],colors[256];
  float yaw=(test%19)*0.13f, scale=test%11==0?-1.0f:1.0f;
  for(unsigned i=0;i<count;i++) {
    float x=(float)((int)(i%4)-2)*0.5f*scale;
    float y=(float)((int)((i/4)%4)-2)*0.4f;
    float z=(float)(i%3)*0.3f;
    vertices[i*3]=x*cosf(yaw)-z*sinf(yaw);
    vertices[i*3+1]=y; vertices[i*3+2]=4+x*sinf(yaw)+z*cosf(yaw);
    faces[i*3]=i;faces[i*3+1]=(i+1)%count;faces[i*3+2]=(i+3)%count;
    colors[i]=(uint16_t)(i*97+0x1000);
    edges[i*4]=i;edges[i*4+1]=(i+1)%count;
    edges[i*4+2]=i;edges[i*4+3]=(i+3)%count;
  }
  if(test%13==0) vertices[2]=0.05f;
  if(test%17==0) vertices[5]=120;
  if(test%19==0) vertices[2]=-1;
  if(test%23==0) vertices[(count-1)*3]=40000; /* mixed float fallback */
  if(test%37==0) assert(game_3d_set_camera(40000,0,0,0,0,70,0.1,100));
  unsigned triangles=test%4==1 || test%4==3?0:count;
  unsigned lines=test%4==2 || test%4==3?0:count*2;
  out->success=game_3d_draw_model_geometry(vertices,count,faces,triangles,colors,
                                          edges,lines,0xffff,test%2==0);
  /* Same array, new contents/camera, multiple instances and a later array API.
   * Sharing must stop at the instance boundary, including epoch rollover. */
  vertices[0]+=0.4f;
  assert(game_3d_set_camera(0.2f,0.1f,0,0.1f,0.05f,70,0.1,100));
  out->success &= game_3d_draw_model_geometry(vertices,count,faces,triangles,colors,
                                             edges,lines,0xf81f,test%2!=0);
  out->success &= game_3d_draw_wireframe(vertices,count,edges,2,0x07ff,false);
  out->pixels=framebuffer;out->depth=depth;out->dirty=dirty;out->dirty_count=dirty_count;
}
'''
main = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
struct result { const void *pixels,*depth; const int *dirty; unsigned dirty_count; bool success; };
void old_run(unsigned,struct result*); void off_run(unsigned,struct result*);
void on_run(unsigned,struct result*); void nocache_run(unsigned,struct result*);
int main(void) {
  void (*variants[])(unsigned,struct result*)={off_run,on_run,nocache_run};
  for(unsigned test=0;test<600;test++) {
    struct result ref;old_run(test,&ref);assert(ref.success);
    for(unsigned i=0;i<3;i++) {
      struct result v;variants[i](test,&v);
      assert(v.success==ref.success);
      assert(!memcmp(ref.pixels,v.pixels,320*240*2));
      assert(!memcmp(ref.depth,v.depth,320*240*2));
      assert(ref.dirty_count==v.dirty_count);
      assert(!memcmp(ref.dirty,v.dirty,ref.dirty_count*4*sizeof(int)));
    }
  }
  puts("Model sharing: PASS 1800 full color/depth/dirty comparisons");
}
'''
external = ['game_3d_supported','game_3d_begin_frame','game_3d_set_viewport',
            'game_3d_set_resolution','game_3d_set_camera','game_3d_draw_triangle',
            'game_3d_draw_indexed_mesh','game_3d_draw_wireframe','game_3d_draw_model_geometry']
with tempfile.TemporaryDirectory(prefix='game-model-shared-') as temporary:
    directory=Path(temporary);objects=[]
    for name,text,shared,cache in [('old',old,0,1),('off',current,0,1),
                                   ('on',current,1,1),('nocache',current,1,0)]:
        prefix=text[text.index('struct game_3d_camera {'):text.index('struct game_font {')]
        renderer=text[text.index('\nbool\ngame_3d_supported('):text.index('\nbool\ngame_display_fill_rect(')]
        definitions=f'#define RUN {name}_run\n#define GAME_TFT_CORE1 1\n#define GAME_3D_DEPTH_SRAM 0\n'
        definitions+=f'#define GAME_3D_TRIANGLE_MODE {args.triangle_mode}\n'
        definitions+=f'#define GAME_3D_LINE_CLIP 1\n#define GAME_3D_PROJECTION_CACHE {cache}\n#define GAME_3D_MODEL_SHARED {shared}\n'
        definitions+='\n'.join(f'#define {f} {name}_{f}' for f in external)+'\n'
        path=directory/(name+'.c')
        path.write_text(definitions+header+prefix+renderer+(legacy if name=='old' else '')+exercise)
        obj=directory/(name+'.o')
        subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-fsanitize=address,undefined',
                        '-I'+str(root/'include'),'-c',str(path),'-o',str(obj)],check=True)
        objects.append(str(obj))
    path=directory/'main.c';path.write_text(main);binary=directory/'model-shared-test'
    subprocess.run([os.environ.get('CC','cc'),'-O2','-fsanitize=address,undefined',str(path),
                    *objects,'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=120)
