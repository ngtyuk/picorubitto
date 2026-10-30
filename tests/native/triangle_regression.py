#!/usr/bin/env python3
"""Compare all triangle scan experiments with 41a47ed, including full dirty/depth."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
old = subprocess.check_output(['git', 'show', '41a47ed:src/game_platform.c'], cwd=root, text=True)
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
static int dirty[2048*4];
static unsigned dirty_count,edge_calls,pixel_visits,block_tests;
static void display_dirty_add(int x,int y,int w,int h) {
  assert(dirty_count<2048); int *r=dirty+dirty_count++*4;
  r[0]=x;r[1]=y;r[2]=w;r[3]=h;
}
'''
exercise = r'''
struct result { const void *pixels,*depth,*dirty; unsigned dirty_count,edges,pixels_tested,blocks; };
static uint32_t seed;
static uint32_t random32(void) { seed=seed*1664525u+1013904223u;return seed; }
static struct game_3d_projected_vertex vertex(float x,float y,float z) {
  return (struct game_3d_projected_vertex){.x=x,.y=y,.inverse_z=z,
    .x_fixed=(int64_t)llrintf(x*65536.0f),.y_fixed=(int64_t)llrintf(y*65536.0f),
    .inverse_z_fixed=game_3d_fixed_inverse_z(z)};
}
void RUN(unsigned test,struct result *out) {
  static uint16_t depth[320*240];
  static struct game_3d_fixed_vertex scratch[256];
  static struct game_3d_projection_entry cache[256];
  game_3d_depth_buffer=depth;game_3d_indexed_vertices=scratch;game_3d_projections=cache;
  game_3d_frame_active=false;
  unsigned mode=test%3;
  assert(game_3d_set_resolution(mode==0?160:mode==1?320:13,mode==0?120:mode==1?240:11));
  assert(game_3d_set_viewport(mode==2?17:0,mode==2?23:0,mode==2?211:320,mode==2?173:240));
  assert(game_3d_set_camera(0,0,0,0,0,70,0.1,100));
  for(unsigned i=0;i<sizeof(framebuffer);i++)framebuffer[i]=(uint8_t)(i*17u+test);
  memset(depth,0,sizeof(depth));dirty_count=edge_calls=pixel_visits=block_tests=0;
  assert(game_3d_begin_frame());seed=test+987;
  for(unsigned n=0;n<5;n++) {
    float x[3],y[3];
    for(unsigned i=0;i<3;i++) {
      x[i]=((int)(random32()%16000)-8000)/16.0f;
      y[i]=((int)(random32()%12000)-6000)/16.0f;
    }
    if(test%7==0) { x[0]=0;x[1]=0;x[2]=319;y[0]=0;y[1]=239;y[2]=239; }
    if(test%11==0) { x[0]=1.5f;x[1]=318.5f;x[2]=318.5f;y[0]=1.5f;y[1]=236;y[2]=235.5f; }
    if(test%13==0) y[2]=y[1]=y[0];
    if(test%17==0) { x[2]=x[1]=x[0]; }
    if(test%19==0) { x[0]=-4096;y[0]=-4096;x[1]=4096;y[1]=-4096;x[2]=0;y[2]=4096; }
    if(test%23==0) { x[0]=6.25f;y[0]=7.125f;x[1]=6.5f;y[1]=7.125f;x[2]=6.5f;y[2]=7.5f; }
    if(test%29==0) { x[0]=0.5f;y[0]=0.5f;x[1]=0.5f;y[1]=120.5f;x[2]=160.5f;y[2]=120.5f; }
    if(test%31==0) { x[0]=-10000;y[0]=119.25f;x[1]=10000;y[1]=120.75f;x[2]=10000;y[2]=120.25f; }
    float z=(n+1)*0.25f;
    struct game_3d_projected_vertex a=vertex(x[0],y[0],z);
    struct game_3d_projected_vertex b=vertex(x[1],y[1],test%11==0?z*0.75f:z);
    struct game_3d_projected_vertex c=vertex(x[2],y[2],test%11==0?z*0.25f:z);
    game_3d_raster_triangle(&a,&b,&c,(uint16_t)(0x1000+n*97));
    /* Equal depth and shared/reversed edges retain the old inclusion rule. */
    game_3d_raster_triangle(&a,&b,&c,0xffff);
    game_3d_raster_triangle(&b,&a,&c,0x07e0);
  }
  if(test%3==0) {
    assert(game_3d_draw_triangle(-1,-1,0.05f,0,1,4,1,-1,3,0xf800));
    assert(game_3d_draw_triangle(-1,-1,3,0,1,120,1,-1,3,0x07e0));
    assert(game_3d_draw_triangle(-40000,-1,4,0,1,4,1,-1,4,0x001f));
  }
  out->pixels=framebuffer;out->depth=depth;out->dirty=dirty;out->dirty_count=dirty_count;
  out->edges=edge_calls;out->pixels_tested=pixel_visits;out->blocks=block_tests;
}
void COUNT(unsigned shape,struct result *out) {
  static uint16_t depth[320*240];game_3d_depth_buffer=depth;
  game_3d_frame_active=false;assert(game_3d_set_resolution(320,240));
  assert(game_3d_set_viewport(0,0,320,240));assert(game_3d_set_camera(0,0,0,0,0,70,0.1,100));
  memset(depth,0,sizeof(depth));dirty_count=edge_calls=pixel_visits=block_tests=0;
  struct game_3d_projected_vertex a=vertex(1.5f,1.5f,1),b=vertex(318.5f,237,1),c=vertex(318.5f,236.5f,1);
  if(shape==0) {a=vertex(-8,-8,1);b=vertex(-8,248,1);c=vertex(328,248,1);}
  if(shape==2) {a=vertex(10,10,1);b=vertex(10,14,1);c=vertex(14,14,1);}
  if(shape==3) {a=vertex(-10000,119.25f,1);b=vertex(10000,120.75f,1);c=vertex(10000,120.25f,1);}
  game_3d_raster_triangle(&a,&b,&c,0xffff);
  out->edges=edge_calls;out->pixels_tested=pixel_visits;out->blocks=block_tests;
}
'''
main = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct result { const void *pixels,*depth,*dirty; unsigned dirty_count,edges,pixels_tested,blocks; };
void old_run(unsigned,struct result*);
void ref_run(unsigned,struct result*);void rows_run(unsigned,struct result*);
void spans_run(unsigned,struct result*);void blocks_run(unsigned,struct result*);
void old_count(unsigned,struct result*);void ref_count(unsigned,struct result*);
void rows_count(unsigned,struct result*);void spans_count(unsigned,struct result*);void blocks_count(unsigned,struct result*);
int main(void) {
  void (*variants[])(unsigned,struct result*)={ref_run,rows_run,spans_run,blocks_run};
  for(unsigned test=0;test<1200;test++) {
    struct result ref;old_run(test,&ref);
    for(unsigned v=0;v<4;v++) {
      struct result out;variants[v](test,&out);
      if(memcmp(ref.pixels,out.pixels,320*240*2) || memcmp(ref.depth,out.depth,320*240*2) ||
         ref.dirty_count!=out.dirty_count || memcmp(ref.dirty,out.dirty,ref.dirty_count*4*sizeof(int))) {
        fprintf(stderr,"triangle mismatch test=%u variant=%u\n",test,v);return 1;
      }
    }
  }
  puts("Triangle scans: PASS 4800 full color/depth/dirty comparisons");
  void (*counts[])(unsigned,struct result*)={old_count,ref_count,rows_count,spans_count,blocks_count};
  struct result measured[4][5];
  const char *names[]={"old","reference","rows","spans","blocks"};
  for(unsigned shape=0;shape<4;shape++) for(unsigned v=0;v<5;v++) {
    struct result out;counts[v](shape,&out);
    measured[shape][v]=out;
    printf("shape=%u variant=%s edge_calls=%u pixel_visits=%u block_tests=%u\n",shape,names[v],out.edges,out.pixels_tested,out.blocks);
  }
  for(unsigned shape=0;shape<3;shape++) {
    assert(measured[shape][0].pixels_tested==measured[shape][1].pixels_tested);
    assert(measured[shape][2].edges==4 && measured[shape][2].edges<measured[shape][1].edges);
  }
  assert(measured[1][3].pixels_tested<measured[1][4].pixels_tested);
  assert(measured[1][4].pixels_tested<measured[1][2].pixels_tested);
  assert(measured[1][4].blocks>0);
  assert(measured[2][2].pixels_tested==measured[2][3].pixels_tested);
  assert(measured[2][2].pixels_tested==measured[2][4].pixels_tested);
  for(unsigned v=1;v<5;v++) {
    assert(measured[3][v].edges==measured[3][0].edges);
    assert(measured[3][v].pixels_tested==measured[3][0].pixels_tested);
  }
}
'''
external=['game_3d_supported','game_3d_begin_frame','game_3d_set_viewport','game_3d_set_resolution',
          'game_3d_set_camera','game_3d_draw_triangle','game_3d_draw_indexed_mesh','game_3d_draw_wireframe','game_3d_draw_model_geometry']
with tempfile.TemporaryDirectory(prefix='game-triangles-') as directory:
    directory=Path(directory);objects=[]
    for name,text,mode in [('old',old,0),('ref',current,0),('rows',current,1),('spans',current,2),('blocks',current,3)]:
        prefix=text[text.index('struct game_3d_camera {'):text.index('struct game_font {')]
        renderer=text[text.index('\nbool\ngame_3d_supported('):text.index('\nbool\ngame_display_fill_rect(')]
        marker='\ngame_3d_fixed_edge('
        start=renderer.index(marker);brace=renderer.index('\n{',start)+2
        renderer=renderer[:brace]+'\n  edge_calls++;'+renderer[brace:]
        start=renderer.index('\ngame_3d_raster_triangle(')
        end=renderer.index('\nstatic int64_t\ngame_3d_fixed_pixel_coordinate',start)
        part=renderer[start:end]
        if name=='old':part=part.replace('for (int pixel_x = left; pixel_x <= right; pixel_x++) {','for (int pixel_x = left; pixel_x <= right; pixel_x++) { pixel_visits++;')
        else:
            part=part.replace('for (int pixel_x = begin_x; pixel_x <= end_x; pixel_x++) {','for (int pixel_x = begin_x; pixel_x <= end_x; pixel_x++) { pixel_visits++;')
            part=part.replace('for (int n = 0; n < count; n++, pixel_x++) {','for (int n = 0; n < count; n++, pixel_x++) { pixel_visits++;')
            part=part.replace('int count = right - pixel_x + 1;','block_tests++; int count = right - pixel_x + 1;')
        renderer=renderer[:start]+part+renderer[end:]
        definitions=f'#define RUN {name}_run\n#define COUNT {name}_count\n#define GAME_3D_TRIANGLE_MODE {mode}\n'
        definitions+='#define GAME_TFT_CORE1 1\n#define GAME_3D_DEPTH_SRAM 0\n#define GAME_3D_MODEL_SHARED 1\n#define GAME_3D_LINE_CLIP 1\n#define GAME_3D_PROJECTION_CACHE 1\n'
        definitions+='\n'.join(f'#define {f} {name}_{f}' for f in external)+'\n'
        path=directory/(name+'.c');path.write_text(definitions+header+prefix+renderer+exercise)
        obj=directory/(name+'.o')
        subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-fsanitize=address,undefined',
                        '-I'+str(root/'include'),'-c',str(path),'-o',str(obj)],check=True)
        objects.append(str(obj))
    path=directory/'main.c';path.write_text(main);binary=directory/'triangle-test'
    subprocess.run([os.environ.get('CC','cc'),'-O2','-fsanitize=address,undefined',str(path),*objects,'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=120)
