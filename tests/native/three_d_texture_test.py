#!/usr/bin/env python3
"""Exercise the production experimental rasterizer against direct barycentric sampling."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/game_platform.c').read_text()
prefix = source[source.index('struct game_3d_camera {'):source.index('struct game_font {')]
renderer = source[source.index('\nbool\ngame_3d_supported('):source.index('\nbool\ngame_display_fill_rect(')]
texture = source[source.index('struct game_texture {'):source.index('struct game_3d_camera {')]
header = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "game_platform.h"
#define GAME_TFT_CORE1 0
static uint8_t framebuffer[GAME_FRAMEBUFFER_BYTES];
static bool tft_ready = true;
static void display_dirty_add(int x,int y,int w,int h) {
  assert(x>=0 && y>=0 && w>0 && h>0 && x+w<=320 && y+h<=240);
}
'''
exercise = r'''
static uint8_t pixels[32*32*2];
static game_texture_t texture = {.pixels=pixels,.width=32,.height=32,.key_color=0xf81f};
static unsigned checked;
static void begin(int width,int height) {
  static uint16_t depth[320*240];
  game_3d_frame_active=false;game_3d_depth_buffer=depth;
  assert(game_3d_set_resolution(width,height));
  assert(game_3d_set_viewport(0,0,width,height));
  assert(game_3d_set_camera(0,0,0,0,0,70,0.2,40));
  memset(framebuffer,0,sizeof(framebuffer));assert(game_3d_begin_frame());
}
static unsigned color(int x,int y) {
  unsigned offset=(y*320+x)*2;return (framebuffer[offset]<<8)|framebuffer[offset+1];
}
static double edge(double ax,double ay,double bx,double by,double x,double y) {
  return (x-ax)*(by-ay)-(y-ay)*(bx-ax);
}
int main(void) {
  for(int y=0;y<32;y++)for(int x=0;x<32;x++) {
    unsigned c=1+y*32+x;pixels[(y*32+x)*2]=c>>8;pixels[(y*32+x)*2+1]=c;
  }
  for(int test=0;test<32;test++) {
    int width=test%2?160:320,height=test%2?120:240;
    bool perspective=(test/2)%2;
    begin(width,height);
    float v[]={-1.3f,-0.9f,2,0.03f,0.97f, 0.1f,1.1f,3.5f,0.2f,0.04f, 1.8f,-0.7f,5,0.94f,0.82f};
    v[0]+=(test/4)*0.15f;v[5]-=(test/4)*0.1f;
    for(int i=0;i<5;i++){float temp=v[5+i];v[5+i]=v[10+i];v[10+i]=temp;}
    assert(game_3d_draw_textured_triangles(&texture,v,1,perspective));
    double sx[3],sy[3],q[3];
    for(int i=0;i<3;i++) {
      q[i]=1.0/v[i*5+2];sx[i]=width*0.5+v[i*5]*game_3d_camera.focal_length*q[i];
      sy[i]=height*0.5-v[i*5+1]*game_3d_camera.focal_length*q[i];
    }
    double area=edge(sx[0],sy[0],sx[1],sy[1],sx[2],sy[2]);
    for(int y=0;y<height;y++)for(int x=0;x<width;x++) {
      double w[3];bool inside=true,boundary=false;
      for(int i=0;i<3;i++) {
        int j=(i+1)%3,k=(i+2)%3;w[i]=edge(sx[j],sy[j],sx[k],sy[k],x+0.5,y+0.5)/area;
        if(w[i]<0)inside=false;if(fabs(w[i])<0.00002)boundary=true;
      }
      if(boundary)continue;
      unsigned expected=0;
      if(inside) {
        double u=0,vv=0,z=0;
        for(int i=0;i<3;i++) {
          double f=perspective?q[i]:1;u+=w[i]*v[i*5+3]*f;vv+=w[i]*v[i*5+4]*f;z+=w[i]*q[i];
        }
        if(perspective){u/=z;vv/=z;}
        if(fabs(u*32-round(u*32))<0.0002 || fabs(vv*32-round(vv*32))<0.0002)continue;
        expected=1+(int)(vv*32)*32+(int)(u*32);
      }
      if(color(x,y)!=expected){fprintf(stderr,"case=%d (%d,%d) got=%u expected=%u\n",test,x,y,color(x,y),expected);return 1;}
      checked++;
    }
  }
  /* Two triangles must cover a quad without a diagonal crack. */
  begin(160,120);
  float quad[]={-1,-1,2,0,1, 1,-1,2,1,1, 1,1,2,1,0,
                -1,-1,2,0,1, 1,1,2,1,0, -1,1,2,0,0};
  assert(game_3d_draw_textured_triangles(&texture,quad,2,true));
  for(int y=10;y<110;y++)for(int x=30;x<130;x++)assert(color(x,y)!=0);
  /* A transparent foreground must leave both color and depth untouched. */
  begin(160,120);
  float near[]={-1,-1,2,0,0, 1,-1,2,0,0, 0,1,2,0,0};
  pixels[0]=0xf8;pixels[1]=0x1f;
  assert(game_3d_draw_textured_triangles(&texture,near,1,true));
  assert(color(80,60)==0 && game_3d_depth_buffer[60*160+80]==0);
  assert(game_3d_draw_triangle(-1,-1,3,1,-1,3,0,1,3,0x07e0));
  assert(color(80,60)==0x07e0);
  pixels[0]=0xff;pixels[1]=0xff;
  assert(game_3d_draw_textured_triangles(&texture,near,1,true));
  assert(color(80,60)==0xffff);
  assert(game_3d_draw_triangle(-1,-1,3,1,-1,3,0,1,3,0xf800));
  assert(color(80,60)==0xffff);
  /* UV must follow the camera-space clipping interpolation. */
  struct game_3d_uv_vertex input[]={{{-1,-1,0.1f},0,0},{{0,1,2},0.5f,1},{{1,-1,2},1,0}},out[8];
  int count=game_3d_clip_uv(input,3,0.2f,true,out);assert(count==4);
  assert(fabsf(out[0].u-(0.5f*0.1f/1.9f))<0.00001f);
  float clipped[]={-1,-1,0.1f,0,0, 1,-1,2,1,0, 0,1,2,0.5f,1};
  begin(160,120);assert(game_3d_draw_textured_triangles(&texture,clipped,1,true));
  assert(color(80,60)!=0);
  clipped[12]=60;begin(160,120);assert(game_3d_draw_textured_triangles(&texture,clipped,1,true));
  assert(color(80,60)!=0);
  assert(game_3d_draw_textured_triangles(&texture,NULL,0,true));
  near[3]=-0.1f;assert(!game_3d_draw_textured_triangles(&texture,near,1,true));
  near[3]=NAN;assert(!game_3d_draw_textured_triangles(&texture,near,1,true));
  near[3]=0;assert(!game_3d_draw_textured_triangles(&texture,near,257,true));
  game_3d_frame_active=false;assert(!game_3d_draw_textured_triangles(&texture,near,1,true));
  printf("PASS %u independent color samples, affine/perspective, near/far UV clipping, cutout depth, occlusion, validation\n",checked);
}
'''
with tempfile.TemporaryDirectory(prefix='texture3d-') as temp:
    path = Path(temp)/'test.c'
    path.write_text(header+texture+prefix+renderer+'\n#include "game_3d_texture.inc"\n'+exercise)
    binary = Path(temp)/'test'
    subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-fsanitize=address,undefined',
                    '-I'+str(root/'include'),'-I'+str(root/'src'),str(path),'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

fixture = root / 'build/tests/texture3d-reference.raw'
fixture.parent.mkdir(parents=True, exist_ok=True)
fixture.write_bytes(b''.join(n.to_bytes(2, 'big') for n in range(1, 1025)))
