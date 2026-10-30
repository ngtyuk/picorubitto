#!/usr/bin/env python3
"""Check the actual firmware screen layout and built-in PCM without hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/game_platform.c').read_text()

def function(name, result):
    start = source.index('\n' + name + '(')
    return result + source[start:source.index('\n}', start) + 2] + '\n'

harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "game_boot_chime.h"
#include "game_system_palette.h"
#include "game_ascii_font.h"
#define GAME_TFT_CORE1 1
#define GAME_DISPLAY_WIDTH 320
#define GAME_DISPLAY_HEIGHT 240
#define GAME_FRAMEBUFFER_BYTES (320*240*2)
static bool tft_ready;
static bool tft_core1_started=true;
static uint8_t frame0[GAME_FRAMEBUFFER_BYTES], frame1[GAME_FRAMEBUFFER_BYTES];
static uint8_t *framebuffers[2]={frame0,frame1};
static int waits;
static void tft_core1_wait_for_slot(int slot) { assert(slot==waits); waits++; }
static int presents, rectangles, labels;
static const char *text[8];
static void game_display_clear(uint16_t color) { (void)color; }
static void display_dirty_add(int x,int y,int w,int h) { assert(x==0 && y==0 && w==320 && h==240); }
static bool game_display_fill_rect(int x,int y,int w,int h,uint16_t color,uint8_t alpha) {
  (void)color; (void)alpha; assert(x>=0 && y>=0 && w>0 && h>0 && x+w<=320 && y+h<=240);
  if (presents==0 && rectangles>=2) {
    assert(w==2 && h==2 && x>=40 && x<280 && y>=108 && y<132);
  }
  rectangles++; return true;
}
static void draw_text(int x,int y,const char *s,uint16_t color) {
  (void)color; assert(x>=0 && y>=0 && x+(int)strlen(s)*12<=320 && y+14<=240);
  text[labels++]=s;
}
static bool game_display_present(void) { presents++; return true; }
'''
assert '"PICORUBITTO"' in function('draw_boot_title', 'static void')
assert 'LOADING' not in function('game_display_boot', 'bool')
harness += function('draw_boot_title', 'static void')
harness += function('game_display_boot', 'bool')
harness += function('game_display_error', 'void')
harness += r'''
int main(void) {
  assert(game_boot_screen_remaining_ms(0)==3000);
  assert(game_boot_screen_remaining_ms(250000)==2750);
  assert(game_boot_screen_remaining_ms(2999999)==1);
  assert(game_boot_screen_remaining_ms(3000000)==0);
  assert(game_boot_screen_remaining_ms(5000000)==0);
  uint8_t samples[GAME_BOOT_CHIME_SAMPLES]; game_boot_chime_generate(samples);
  assert(samples[0]==128 && samples[GAME_BOOT_CHIME_SAMPLES-1]==128);
  unsigned low=255, high=0;
  for(unsigned i=0;i<GAME_BOOT_CHIME_SAMPLES;i++) { if(samples[i]<low)low=samples[i]; if(samples[i]>high)high=samples[i]; }
  assert(low<100 && high>156 && GAME_BOOT_CHIME_RATE==8000 && GAME_BOOT_CHIME_SAMPLES==1600);
  assert(!game_display_boot() && presents==0);
  memset(frame0,0xaa,sizeof(frame0));memset(frame1,0xaa,sizeof(frame1));
  tft_ready=true; assert(game_display_boot()); assert(presents==1 && rectangles>2 && labels==0 && waits==2);
  for(unsigned i=0;i<GAME_FRAMEBUFFER_BYTES;i++) assert(frame0[i]==0 && frame1[i]==0);
  const char *titles[]={"BOOT","CARTRIDGE","VM","GAME"};
  const char *codes[]={"E01","E02","E03","E04"};
  for(unsigned i=0;i<4;i++) {
    labels=rectangles=0;game_display_error(titles[i],"diagnostic detail");
    assert(rectangles==5 && labels==5 && !strcmp(text[4],codes[i]));
    assert(!strcmp(text[0],"GAME STOPPED"));
  }
  assert(!strcmp(text[1],"GAME ERROR"));
  tft_ready=false; labels=0;game_display_error("GAME", "detail");assert(labels==0);
  puts("Boot/error layout and 200ms PCM: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='game-boot-') as tmp:
    path = Path(tmp) / 'test.c'
    path.write_text(harness)
    binary = Path(tmp) / 'test'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(root / 'include'), str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
