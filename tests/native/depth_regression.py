#!/usr/bin/env python3
"""Exercise production depth-buffer routing and compare SRAM/PSRAM rendering."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/game_platform.c').read_text()
prefix = source[source.index('struct game_3d_camera {'):source.index('struct game_font {')]
renderer = source[source.index('\nbool\ngame_3d_supported('):source.index('\nbool\ngame_display_fill_rect(')]
header = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "game_platform.h"
#define printf(...) ((int)0)
static bool tft_ready=true;
static uint8_t framebuffer[GAME_FRAMEBUFFER_BYTES];
static unsigned dirty[4*2048],dirty_count;
static void display_dirty_add(int x,int y,int w,int h) {
  assert(dirty_count<2048);
  unsigned *r=dirty+dirty_count++*4;
  r[0]=x; r[1]=y; r[2]=w; r[3]=h;
}
'''
exercise = r'''
struct result { const void *pixels,*depth,*dirty; unsigned dirty_count; };
void RUN(unsigned test,struct result *out) {
  static uint16_t fallback[320*240],snapshot[320*240];
  static struct game_3d_fixed_vertex vertices[256];
  const unsigned sizes[][2]={{160,120},{320,240},{320,60},{320,61},
    {1,1},{159,120},{159,121},{13,11}};
  unsigned w=sizes[test%8][0],h=sizes[test%8][1],pixels=w*h;
  game_3d_frame_active=false;
  game_3d_depth_buffer=fallback;
#if GAME_TFT_CORE1 && GAME_3D_DEPTH_SRAM
  game_3d_depth_psram=fallback;
  uint16_t *expected=pixels<=19200?game_3d_depth_low_storage:fallback;
  for(unsigned i=0;i<19200;i++) game_3d_depth_low_storage[i]=0xa55a;
#else
  uint16_t *expected=fallback;
#endif
  for(unsigned i=0;i<320*240;i++) fallback[i]=0xa55a;
  game_3d_indexed_vertices=vertices;
  assert(game_3d_set_resolution(w,h));
  assert(game_3d_depth_buffer==expected && game_3d_supported());
  assert(!game_3d_set_resolution(0,h) && !game_3d_set_resolution(321,h));
  assert(game_3d_depth_buffer==expected && game_3d_internal_width==w);
  /* A failed focal-length update rolls back dimensions and buffer selection. */
  game_3d_camera.fov_degrees=NAN;
  assert(!game_3d_set_resolution(w==320?160:320,120));
  assert(game_3d_depth_buffer==expected && game_3d_internal_width==w);
  game_3d_camera.fov_degrees=70;
  assert(game_3d_set_camera(0,0,0,0,0,70,0.1,100));
  assert(game_3d_set_viewport(0,0,320,240));
  assert(game_3d_begin_frame());
  for(unsigned i=0;i<pixels;i++) assert(expected[i]==0);
  unsigned capacity=expected==fallback?320*240:19200;
  for(unsigned i=pixels;i<capacity;i++) assert(expected[i]==0xa55a);
  assert(!game_3d_set_resolution(320,240));
  assert(game_3d_depth_buffer==expected);
  memset(framebuffer,0,sizeof(framebuffer)); dirty_count=0;
  float points[]={-1,-1,3,0,1,3,1,-1,3,-1,1,4};
  uint16_t faces[]={0,1,2,1,0,3},colors[]={0xf800,0x001f};
  uint16_t edges[]={0,1,1,2,2,0,0,3};
  assert(game_3d_draw_indexed_mesh(points,4,faces,2,colors));
  assert(game_3d_draw_wireframe(points,4,edges,4,0xffff,true));
  assert(game_3d_draw_triangle(-1,-1,2,0,1,2,1,-1,2,0x07e0));
  memset(snapshot,0,sizeof(snapshot)); memcpy(snapshot,expected,pixels*2);
  out->pixels=framebuffer; out->depth=snapshot;
  out->dirty=dirty; out->dirty_count=dirty_count;
  /* Beginning again clears depth written by the preceding draw. */
  assert(game_3d_begin_frame());
  for(unsigned i=0;i<pixels;i++) assert(expected[i]==0);
  game_3d_frame_active=false;
#if GAME_TFT_CORE1 && GAME_3D_DEPTH_SRAM
  /* Preserve full-resolution capability semantics on PSRAM allocation failure. */
  game_3d_depth_psram=NULL;
  assert(game_3d_set_resolution(160,120));
  assert(!game_3d_supported() && !game_3d_begin_frame());
  assert(!game_3d_frame_active);
  game_3d_depth_psram=fallback;
  assert(game_3d_set_resolution(320,240) && game_3d_supported());
#endif
  tft_ready=false; assert(!game_3d_begin_frame()); tft_ready=true;
}
'''
main = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct result { const void *pixels,*depth,*dirty; unsigned dirty_count; };
void off_run(unsigned,struct result*);
void on_run(unsigned,struct result*);
void core0_run(unsigned,struct result*);
int main(void) {
  for(unsigned test=0;test<800;test++) {
    struct result ref,on,core0; off_run(test,&ref);
    on_run(test,&on); core0_run(test,&core0);
    struct result *variants[]={&on,&core0};
    for(unsigned i=0;i<2;i++) {
      struct result *v=variants[i];
      assert(!memcmp(ref.pixels,v->pixels,320*240*2));
      assert(!memcmp(ref.depth,v->depth,320*240*2));
      assert(ref.dirty_count==v->dirty_count);
      assert(!memcmp(ref.dirty,v->dirty,ref.dirty_count*4*sizeof(unsigned)));
    }
  }
  puts("Depth routing: PASS 1600 color/depth/dirty comparisons; threshold, repeat, rollback, clear, failure");
}
'''
external = ['game_3d_supported','game_3d_begin_frame','game_3d_set_viewport',
            'game_3d_set_resolution','game_3d_set_camera','game_3d_draw_triangle',
            'game_3d_draw_indexed_mesh','game_3d_draw_wireframe',
            'game_3d_draw_model_geometry']
with tempfile.TemporaryDirectory(prefix='game-depth-') as temporary:
    directory = Path(temporary)
    objects = []
    for name,core,depth in [('off',1,0),('on',1,1),('core0',0,1)]:
        definitions = f'#define GAME_TFT_CORE1 {core}\n#define GAME_3D_DEPTH_SRAM {depth}\n'
        definitions += '#define GAME_3D_LINE_CLIP 1\n#define GAME_3D_PROJECTION_CACHE 0\n'
        definitions += f'#define RUN {name}_run\n'
        definitions += '\n'.join(f'#define {f} {name}_{f}' for f in external)+'\n'
        path = directory/(name+'.c')
        path.write_text(definitions+header+prefix+renderer+exercise)
        obj = directory/(name+'.o')
        subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-fsanitize=address,undefined',
                        '-I'+str(root/'include'),'-c',str(path),'-o',str(obj)],check=True)
        objects.append(str(obj))
    path=directory/'main.c';path.write_text(main)
    binary=directory/'depth-test'
    subprocess.run([os.environ.get('CC','cc'),'-O2','-fsanitize=address,undefined',
                    str(path),*objects,'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=120)
