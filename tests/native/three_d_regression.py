#!/usr/bin/env python3
"""Compare production 3D color/depth/dirty output with the pre-#2/#4 renderer."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path)
parser.add_argument('--triangle-mode', type=int, choices=range(4), default=0)
args = parser.parse_args()
baseline = args.baseline.read_text() if args.baseline else subprocess.check_output(
    ['git', 'show', '692f2c0:src/game_platform.c'], cwd=root, text=True)
current = (root/'src/game_platform.c').read_text()
header = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "game_platform.h"
#define GAME_TFT_CORE1 1
static uint8_t framebuffer[GAME_FRAMEBUFFER_BYTES];
static bool tft_ready=true;
static unsigned projections, samples;
static int dirty[2048*4];
static unsigned dirty_count;
static void display_dirty_add(int x,int y,int w,int h) {
  assert(x>=0 && y>=0 && w>0 && h>0 && x+w<=320 && y+h<=240);
  assert(dirty_count<2048);
  int *rect=dirty+dirty_count++*4;
  rect[0]=x; rect[1]=y; rect[2]=w; rect[3]=h;
}
'''
exercise = r'''
struct result { const void *pixels,*depth; unsigned projections,samples; const int *dirty; unsigned dirty_count; };
static uint32_t seed;
static uint32_t random32(void) { seed=seed*1664525u+1013904223u; return seed; }
void RUN(unsigned test,struct result *out) {
  static uint16_t depth[320*240];
  static struct game_3d_fixed_vertex vertices[256];
#if TEST_NEW
  static struct game_3d_projection_entry cache[256];
  game_3d_projections=cache;
#if GAME_3D_LINE_CLIP
  static bool checked_math;
  if (!checked_math) {
    for(uint64_t major=1;major<=64;major++) for(uint64_t minor=0;minor<=major;minor++) {
      for(uint64_t step=0;step<=major;step++) {
        uint64_t remainder, value=step*minor+(major-1)/2;
        assert(game_3d_line_minor_at(step,major,minor,&remainder)==value/major);
        assert(remainder==value%major);
      }
    }
    seed=123;
    for(unsigned i=0;i<500;i++) {
      uint64_t major=(UINT64_C(1)<<46)+((uint64_t)random32()<<8);
      uint64_t minor=((uint64_t)random32()<<12)%major;
      uint64_t step=((uint64_t)random32()<<12)%major;
      uint64_t remainder;
      __uint128_t value=(__uint128_t)step*minor+(major-1)/2;
      assert(game_3d_line_minor_at(step,major,minor,&remainder)==value/major);
      assert(remainder==value%major);
    }
    checked_math=true;
  }
#endif
#endif
  game_3d_depth_buffer=depth; game_3d_indexed_vertices=vertices;
  game_3d_frame_active=false;
  unsigned resolution=test%3;
  assert(game_3d_set_resolution(resolution==0?160:resolution==1?320:13,
                               resolution==0?120:resolution==1?240:11));
  assert(game_3d_set_viewport(resolution==2?17:0,resolution==2?23:0,
                            resolution==2?211:320,resolution==2?173:240));
  assert(game_3d_set_camera(0,0,0,0,0,70,0.1,100));
  for(size_t i=0;i<sizeof(framebuffer);i++) framebuffer[i]=(uint8_t)(i*17u+test);
  memset(depth,0,sizeof(depth)); dirty_count=0;
  assert(game_3d_begin_frame()); projections=samples=0; seed=test+1234;
  if (test>=4000) {
    memset(framebuffer,0,sizeof(framebuffer));
    int64_t huge=INT64_C(1000000000000)*65536;
    struct game_3d_projected_vertex a={.x_fixed=-huge,.y_fixed=-huge,.inverse_z_fixed=65536};
    struct game_3d_projected_vertex b={.x_fixed=huge,.y_fixed=huge,.inverse_z_fixed=65536};
    game_3d_raster_line(test==4000?&a:&b,test==4000?&b:&a,0xffff,true);
  } else if (test<2000) {
    /* All octants, clipped endpoints, strict half-way ties, depth crossings. */
    for (unsigned line=0;line<4;line++) {
      int x0=(int)(random32()%1200)-450, y0=(int)(random32()%1000)-400;
      int x1=(int)(random32()%1200)-450, y1=(int)(random32()%1000)-400;
      if(test%7==0) y1=y0;
      if(test%11==0) x1=x0;
      if(test%13==0) {x1=x0; y1=y0;}
      if(test%17==0) {x0=-4096; x1=4096; y0=17; y1=120;}
      struct game_3d_projected_vertex a={.x_fixed=(int64_t)x0*65536,.y_fixed=(int64_t)y0*65536,
        .inverse_z_fixed=(int32_t)(random32()%120000)-40000};
      struct game_3d_projected_vertex b={.x_fixed=(int64_t)x1*65536,.y_fixed=(int64_t)y1*65536,
        .inverse_z_fixed=(int32_t)(random32()%120000)-40000};
      game_3d_raster_line(&a,&b,(uint16_t)(random32()|1),test%2==0);
      game_3d_raster_line(&b,&a,0x3bef,true);
    }
  } else {
    float points[256*3]; uint16_t edges[256*4];
    unsigned count=test%4==0?256:8;
    for(unsigned i=0;i<count;i++) {
      points[i*3]=(float)((int)(i%4)-2)*0.7f;
      points[i*3+1]=(float)((int)(i/4%4)-2)*0.6f;
      points[i*3+2]=3.0f+(i%3)*0.5f;
      edges[i*4]=i; edges[i*4+1]=(i+1)%count;
      edges[i*4+2]=i; edges[i*4+3]=(i+3)%count;
    }
    if(test%7==0) points[2]=0.05f;
    if(test%11==0) points[5]=120;
    if(test%13==0) points[0]=40000;
    if(test%17==0) points[2]=-1;
#if TEST_NEW
    if(test%19==0) game_3d_projections=NULL; /* allocation-failure fallback */
    if(test%23==0) game_3d_projection_generation=UINT32_MAX;
#endif
    assert(game_3d_draw_wireframe(points,count,edges,count*2,0xff20,true));
    /* Same pointer, changed contents and camera, another draw in one frame. */
    points[0]+=0.4f;
    assert(game_3d_set_camera(0.2f,0.1f,0,0.1f,0.05f,70,0.1,100));
    assert(game_3d_draw_wireframe(points,count,edges,count*2,0x07ff,false));
    uint16_t faces[]={0,2,1,1,2,3,0,1,2,0,4,1};
    uint16_t colors[]={0xf800,0x07e0,0x001f,0xffe0};
    assert(game_3d_draw_indexed_mesh(points,count,faces,4,colors));
    assert(game_3d_draw_triangle(-1,-1,2,0,1,2,1,-1,2,0xffff));
  }
  out->pixels=framebuffer; out->depth=depth;
  out->projections=projections; out->samples=samples;
  out->dirty=dirty; out->dirty_count=dirty_count;
}
'''


def source(text, name, new, clip=0, cache=0):
    # Keep every production 3D transform, clipper and rasterizer in place.
    prefix = text[text.index('struct game_3d_camera {'):text.index('struct game_font {')]
    renderer = text[text.index('\nbool\ngame_3d_supported('):text.index('\nbool\ngame_display_fill_rect(')]
    for function in ['game_3d_project_fixed_vertex', 'game_3d_project_vertex']:
        start = renderer.index('\n'+function+'(')
        brace = renderer.index('\n{',start)+2
        renderer = renderer[:brace]+'\n  projections++;'+renderer[brace:]
    loop = 'for (int64_t sample = begin; ; sample++) {' if new else 'for (;;) {'
    start = renderer.index('\ngame_3d_raster_line(')
    before, after = renderer[:start], renderer[start:]
    renderer = before+after.replace(loop,loop+' samples++;',1)
    external = ['game_3d_supported', 'game_3d_begin_frame', 'game_3d_set_viewport',
                'game_3d_set_resolution', 'game_3d_set_camera', 'game_3d_draw_triangle',
                'game_3d_draw_indexed_mesh', 'game_3d_draw_wireframe',
                'game_3d_draw_model_geometry']
    definitions = '\n'.join(f'#define {f} {name}_{f}' for f in external)
    return (f'#define GAME_3D_TRIANGLE_MODE {args.triangle_mode}\n'
            f'#define TEST_NEW {int(new)}\n#define GAME_3D_LINE_CLIP {clip}\n'
            f'#define GAME_3D_PROJECTION_CACHE {cache}\n#define RUN {name}_run\n'
            +definitions+'\n'+header+prefix+renderer+exercise)


main = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct result { const void *pixels,*depth; unsigned projections,samples; const int *dirty; unsigned dirty_count; };
void old_run(unsigned,struct result *);
void base_run(unsigned,struct result *);
void clip_run(unsigned,struct result *);
void cache_run(unsigned,struct result *);
void both_run(unsigned,struct result *);
int main(void) {
  void (*variants[])(unsigned,struct result *)={base_run,clip_run,cache_run,both_run};
  unsigned long long projections[5]={0},samples[5]={0};
  for(unsigned test=0;test<2400;test++) {
    struct result reference; old_run(test,&reference);
    projections[0]+=reference.projections; samples[0]+=reference.samples;
    for(unsigned v=0;v<4;v++) {
      struct result actual; variants[v](test,&actual);
      if(memcmp(reference.pixels,actual.pixels,320*240*2) ||
         memcmp(reference.depth,actual.depth,320*240*2) ||
         reference.dirty_count!=actual.dirty_count ||
         memcmp(reference.dirty,actual.dirty,reference.dirty_count*4*sizeof(int))) {
        fprintf(stderr,"3D mismatch case=%u variant=%u\n",test,v); return 1;
      }
      projections[v+1]+=actual.projections; samples[v+1]+=actual.samples;
    }
  }
  assert(projections[3]<projections[1] && projections[4]==projections[3]);
  assert(samples[2]<samples[1] && samples[4]==samples[2]);
  for(unsigned test=4000;test<=4003;test+=3) {
    struct result huge; both_run(test,&huge);
    assert(huge.samples<=320);
    const unsigned char *pixels=huge.pixels;
    for(unsigned y=0;y<240;y++) for(unsigned x=0;x<320;x++) {
      unsigned expected=x==y?255:0;
      assert(pixels[(y*320+x)*2]==expected && pixels[(y*320+x)*2+1]==expected);
    }
  }
  puts("3D regression: PASS 9600 full color/depth/dirty comparisons");
  for(unsigned v=0;v<5;v++) printf("variant=%u projections=%llu line_samples=%llu\n",v,projections[v],samples[v]);
}
'''
with tempfile.TemporaryDirectory(prefix='game-3d-test-') as temporary:
    directory = Path(temporary)
    objects = []
    variants = [('old', baseline, False, 0, 0), ('base', current, True, 0, 0),
                ('clip', current, True, 1, 0), ('cache', current, True, 0, 1),
                ('both', current, True, 1, 1)]
    for name, text, new, clip, cache in variants:
        path = directory/(name+'.c')
        path.write_text(source(text,name,new,clip,cache))
        obj = directory/(name+'.o')
        subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-fsanitize=address,undefined',
                        '-I'+str(root/'include'),'-c',str(path),'-o',str(obj)],check=True)
        objects.append(str(obj))
    path = directory/'main.c'; path.write_text(main)
    binary = directory/'3d-test'
    subprocess.run([os.environ.get('CC','cc'),'-O2','-fsanitize=address,undefined',
                    str(path),*objects,'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=120)
