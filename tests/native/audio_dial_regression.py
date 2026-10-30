#!/usr/bin/env python3
"""Exercise the real ADC driver, mapping and gain without attached hardware."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
stubs = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define GPIO_IN 0
#define GPIO_FUNC_NULL 0
static uint32_t now;
static unsigned adc_calls, alias_calls, gpio_calls, channel;
static uint16_t input;
static bool dither;
static void adc_init(void) {}
static void adc_gpio_init(unsigned pin) { assert(pin == 42); gpio_calls++; }
static void adc_select_input(unsigned value) { assert(value == 2); channel = value; }
static uint16_t adc_read(void) { adc_calls++; return input + (dither ? (adc_calls - 1) % 4 : 0); }
static uint32_t time_us_32(void) { return now; }
#ifdef PIMORONI_PICO_PLUS2_RP2350
static void gpio_init(unsigned pin) { assert(pin == 28); alias_calls++; }
static void gpio_set_dir(unsigned pin, unsigned dir) { assert(pin == 28 && dir == GPIO_IN); }
static void gpio_set_function(unsigned pin, unsigned fn) { assert(pin == 28 && fn == GPIO_FUNC_NULL); }
static void gpio_disable_pulls(unsigned pin) { assert(pin == 28); }
static void gpio_set_input_enabled(unsigned pin, bool enabled) { assert(pin == 28 && !enabled); }
#endif
'''
test = r'''
#include <assert.h>
#include "game_platform.h"
#include "game_audio_dial.h"
int main(void) {
  unsigned previous = 0;
  for(unsigned raw=0;raw<4096;raw++) {
    unsigned volume=game_audio_dial_map(raw);
    assert(volume>=previous); previous=volume;
    if(raw<=128) assert(volume==0);
    if(raw>=3800) assert(volume==255);
  }
  assert(game_audio_dial_map(2)==0);
  assert(game_audio_dial_map(128)==0);
  assert(game_audio_dial_map(1964)==127);
  assert(game_audio_dial_map(2048)==133);
  assert(game_audio_dial_map(3799)==254);
  assert(game_audio_dial_map(3800)==255);
  assert(game_audio_dial_map(3888)==255); // Reported physical maximum in v21.
  assert(game_audio_dial_map(3945)==255); // Earlier physical maximum in v20.
  for(int sample=-32768;sample<=32767;sample++) {
    assert(game_audio_apply_volume(sample,0)==0);
    assert(game_audio_apply_volume(sample,255)==sample);
    assert(game_audio_apply_volume(sample,127)==sample*127/255);
  }
#if GAME_AUDIO_VOLUME_KNOB
  assert(!game_audio_dial_supported());
  now=UINT32_MAX-10000u;input=2048;
  game_audio_dial_init();
  assert(game_audio_dial_supported() && game_audio_dial_value()==2048);
  assert(adc_calls==5 && channel==2 && gpio_calls==1);
#ifdef PIMORONI_PICO_PLUS2_RP2350
  assert(alias_calls==1);
#else
  assert(alias_calls==0);
#endif
  uint8_t middle=game_audio_master_volume();
  input=4095;now+=19999;game_audio_dial_poll();assert(adc_calls==5);
  now+=1;game_audio_dial_poll();assert(adc_calls==9);
  assert(game_audio_dial_value()==4095 && game_audio_master_volume()>middle);
  assert(game_audio_master_volume()<255); // Smoothing, rather than a hard step.
  for(unsigned i=0;i<32;i++){ now+=20000;game_audio_dial_poll(); }
  assert(game_audio_master_volume()==255);
  input=2;
  for(unsigned i=0;i<32;i++){ now+=20000;game_audio_dial_poll(); }
  assert(game_audio_master_volume()==0 && game_audio_dial_value()==2);
  input=3888;
  for(unsigned i=0;i<32;i++){ now+=20000;game_audio_dial_poll(); }
  assert(game_audio_master_volume()==255 && game_audio_dial_value()==3888);
  // Endpoint noise must stay in the dead zones without moving the raw reading.
  for(unsigned i=0;i<100;i++) {
    input=(i%2)?3870:3890;now+=20000;game_audio_dial_poll();
    assert(game_audio_master_volume()==255 && game_audio_dial_value()==input);
  }
  input=2;
  for(unsigned i=0;i<32;i++){ now+=20000;game_audio_dial_poll(); }
  for(unsigned i=0;i<100;i++) {
    input=(i%2)?1:4;now+=20000;game_audio_dial_poll();
    assert(game_audio_master_volume()==0 && game_audio_dial_value()==input);
  }
  input=1000;dither=true;now+=20000;game_audio_dial_poll();
  assert(game_audio_dial_value()==1002); // Four conversions, rounded mean.
  dither=false;input=2048;
  for(unsigned i=0;i<32;i++){ now+=20000;game_audio_dial_poll(); }
  for(unsigned i=0;i<100;i++) {
    input=(i%2)?2047:2049;now+=20000;game_audio_dial_poll();
    assert(game_audio_master_volume()==133);
  }
#else
  game_audio_dial_init();game_audio_dial_poll();
  assert(!game_audio_dial_supported() && game_audio_master_volume()==255);
#endif
}
'''
with tempfile.TemporaryDirectory(prefix='gaming-dial-') as temp:
    directory = Path(temp)
    (directory/'pico').mkdir();(directory/'hardware').mkdir()
    (directory/'pico/stdlib.h').write_text(stubs)
    (directory/'hardware/adc.h').write_text('')
    (directory/'test.c').write_text('#include "' + str(root/'src/game_audio_dial.c') + '"\n' + test)
    for enabled, board in [(0, False), (1, False), (1, True)]:
        flags=[f'-DGAME_AUDIO_VOLUME_KNOB={enabled}']
        if board: flags+=['-DPIMORONI_PICO_PLUS2_RP2350']
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-I'+str(directory),'-I'+str(root/'include'),
                        *flags,str(directory/'test.c'),
                        '-o',str(directory/'test')],check=True)
        subprocess.run([str(directory/'test')],check=True)
for pins in [['--i2s-data','28'], ['--i2s-bclk','27']]:
    # Only unsafe explicit overrides must fail; default resolution is checked by builds.
    result=subprocess.run(['ruby',str(root/'tools/build/workflow.rb'),'engine',
                           '--volume-knob','--audio-output','i2s',*pins],capture_output=True,text=True)
    assert result.returncode != 0 and 'volume knob requires' in result.stderr
# Check board-specific preflight without starting a compiler or touching a
# build tree. A valid pin set reaches the deliberately absent dependency.
with tempfile.TemporaryDirectory(prefix='dial-pin-policy-') as temporary:
    missing = str(Path(temporary)/'missing-picoruby')
    for board, data, accepted in [('weact_studio_rp2350b_core',28,True),
                                  ('weact_studio_rp2350b_core',17,True),
                                  ('pimoroni_pico_plus2_rp2350',28,False)]:
        result=subprocess.run(['ruby',str(root/'tools/build/workflow.rb'),'engine',
                               '--board',board,'--volume-knob','--audio-output','i2s',
                               '--i2s-data',str(data),'--picoruby-root',missing],capture_output=True,text=True)
        assert result.returncode != 0
        if accepted:
            assert 'PicoRuby checkout not found' in result.stderr, result.stderr
            assert 'volume knob requires' not in result.stderr
        else:
            assert 'volume knob requires' in result.stderr
    # Direct CMake use must retain the Pico Plus GP28/ADC collision guard.
    result=subprocess.run(['cmake','-S',str(root),'-B',str(Path(temporary)/'unsafe-pico'),
                           '-DPICO_BOARD=pimoroni_pico_plus2_rp2350',
                           '-DGAME_AUDIO_I2S=ON','-DGAME_AUDIO_VOLUME_KNOB=ON',
                           '-DGAME_AUDIO_I2S_DATA_PIN=28'],capture_output=True,text=True)
    assert result.returncode != 0 and 'volume knob requires' in result.stderr, result.stderr
print('AUDIO DIAL: PASS disabled/Pico Plus/WeAct; all ADC codes, signed PCM, polling wrap, smoothing, pins')
