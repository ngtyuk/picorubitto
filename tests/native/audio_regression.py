#!/usr/bin/env python3
"""Exercise the production audio IRQ/play functions with a simulated DMA device."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/game_platform.c').read_text()

def function(name):
    start = source.index('\n' + name + '(') + 1
    end = source.index('\n}', start) + 2
    return source[start:end]

harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef unsigned int uint;
#define GAME_AUDIO_MAX_VOICES 5
#define GAME_AUDIO_SE_VOICES 4
#define GAME_AUDIO_MAX_SE_BYTES (128u*1024u)
#define GAME_AUDIO_BGM_VOICE 4
#define GAME_AUDIO_BUFFER_SAMPLES 2048
#define GAME_AUDIO_MIDDLE_LEVEL 128
#define PWM_CHAN_A 0
#define DMA_SIZE_8 0
#define DMA_SIZE_32 2
#define clk_sys 0
typedef int dma_channel_config;
typedef struct { const uint8_t *sample_data; size_t sample_size; uint32_t sample_rate; size_t wav_bytes; } game_audio_sound_t;
static struct { const game_audio_sound_t *sound; size_t position; uint8_t volume; bool playing; } audio_voices[5];
static bool audio_bgm_loop;
static struct { uint32_t ints0; } dma_device, *dma_hw = &dma_device;
static struct { struct { uint32_t cc; } slice[1]; } pwm_device, *pwm_hw = &pwm_device;
static int audio_dma_channel = 0, audio_dma_timer = 0;
static uint audio_pwm_slice, audio_pwm_channel, audio_current_buffer;
static bool audio_output_ready = true, audio_playing, audio_buffer_valid[2];
static uint32_t audio_sample_rate;
static uint8_t audio_dma_buffers[2][2048];
static bool irq_disabled, finish_before_lock, transfer_running, refill_during_irq;
static int starts, fills;
static uint32_t save_and_disable_interrupts(void) {
  if (finish_before_lock) { audio_playing = false; audio_sample_rate = 0; finish_before_lock = false; }
  bool previous = irq_disabled; irq_disabled = true; return previous;
}
static void restore_interrupts(uint32_t previous) { irq_disabled = previous; }
static bool audio_fill_buffer(uint index) {
  assert(index < 2);
  if (refill_during_irq) assert(transfer_running);
  fills++; return true;
}
static void dma_channel_set_read_addr(uint channel, const void *ptr, bool trigger) {
  (void)channel; (void)trigger; assert(ptr == audio_dma_buffers[audio_current_buffer]);
}
static void dma_channel_set_trans_count(uint channel, uint count, bool trigger) {
  (void)channel; assert(count == 2048 && trigger); transfer_running = true;
}
static void dma_channel_set_irq0_enabled(uint channel, bool enabled) { (void)channel; (void)enabled; }
static void audio_pwm_set_level(uint level) { assert(level == 128); }
void game_audio_stop(void);
static void dma_channel_abort(uint channel) { (void)channel; transfer_running=false; }
static void audio_ramp_to_middle(void) {}
#if GAME_AUDIO_I2S
#include "game_audio_i2s.h"
static struct game_audio_pcm_probe audio_pcm_probe;
#define PIO_FDEBUG_TXSTALL_LSB 24
static uint32_t audio_i2s_blocks, audio_i2s_stalls, audio_i2s_late_irqs;
static uint32_t audio_i2s_late_refills, audio_i2s_refill_max_us;
static uint32_t time_us_32(void) { static uint32_t now; return ++now; }
static struct { uint32_t fdebug, txf[4]; } pio_device, *audio_pio=&pio_device;
static int audio_sm=0;
static bool i2s_enabled;
static void audio_i2s_stop(void) { i2s_enabled=false; }
static bool pio_sm_is_tx_fifo_empty(void *pio, uint sm) { (void)pio; (void)sm; return false; }
static void tight_loop_contents(void) {}
static void pio_sm_set_enabled(void *pio, uint sm, bool enabled) {
  assert(pio==audio_pio && sm==0);i2s_enabled=enabled;
}
static uint pio_get_dreq(void *pio, uint sm, bool tx) {
  assert(pio==audio_pio && sm==0 && tx);return 66;
}
#endif
static uint32_t clock_get_hz(int clock) { (void)clock; return 150000000; }
static void audio_select_dma_timer_fraction(uint32_t clock, uint32_t rate, uint16_t *n, uint16_t *d) {
  (void)clock; (void)rate; *n = 1; *d = 100;
}
static void dma_timer_set_fraction(uint timer, uint16_t n, uint16_t d) { (void)timer; (void)n; (void)d; }
static dma_channel_config dma_channel_get_default_config(uint channel) { (void)channel; return 0; }
static void channel_config_set_transfer_data_size(dma_channel_config *c, int v) { (void)c; (void)v; }
static void channel_config_set_read_increment(dma_channel_config *c, bool v) { (void)c; (void)v; }
static void channel_config_set_write_increment(dma_channel_config *c, bool v) { (void)c; (void)v; }
static void channel_config_set_dreq(dma_channel_config *c, uint v) { (void)c; (void)v; }
static uint dma_get_timer_dreq(uint timer) { return timer; }
static void dma_channel_configure(uint channel, dma_channel_config *c, volatile void *dst, const void *src, uint count, bool trigger) {
  (void)channel; (void)c; (void)dst; (void)src; assert(count == 2048 && trigger); starts++;
}
'''
for name, result in [('audio_i2s_check_stall', 'static void'), ('audio_i2s_refill', 'static bool'), ('audio_i2s_report_stats', 'static void')]:
    harness += '\n#if GAME_AUDIO_I2S\n' + result + '\n' + function(name) + '\n#endif\n'
harness += '\nvoid\n' + function('game_audio_stop')
harness += '\nstatic void\n' + function('audio_dma_irq_handler')
harness += '\nstatic bool\n' + function('audio_sound_play_kind')
harness += '\nbool\n' + function('game_audio_sound_play')
harness += '\nbool\n' + function('game_audio_sound_play_bgm')
harness += '\nvoid\n' + function('game_audio_bgm_stop')
harness += r'''
int main(void) {
  uint8_t samples[1] = {128};
  game_audio_sound_t sound = {samples, 1, 22050, 0};
  audio_playing = true; audio_buffer_valid[1] = true; dma_hw->ints0 = 1;
  refill_during_irq = true;
  audio_dma_irq_handler();
  assert(audio_current_buffer == 1 && fills == 1 && audio_buffer_valid[0]);
  refill_during_irq = false;

  /* Simulate completion at the last interruptible point before registration. */
  audio_playing = true; audio_sample_rate = 22050; finish_before_lock = true;
  assert(game_audio_sound_play(&sound, 255));
  assert(starts == 1 && audio_playing && !irq_disabled);
  assert(audio_voices[0].sound == &sound && audio_voices[0].playing);

  /* SE limit is enforced even for an already loaded sound. */
  sound.wav_bytes=128u*1024u+1;
  assert(!game_audio_sound_play(&sound, 255));
  sound.wav_bytes=128u*1024u;
  /* Adding a voice must not restart an active DMA stream. */
  assert(game_audio_sound_play(&sound, 128));
  assert(starts == 1 && audio_voices[1].playing && !irq_disabled);
  sound.sample_rate = 44100;
  assert(!game_audio_sound_play(&sound, 255) && !irq_disabled);
  sound.sample_rate = 22050;
  for (uint i = 0; i < 4; i++) audio_voices[i].playing = true;
  assert(game_audio_sound_play(&sound, 255) && !irq_disabled && starts == 1);

  assert(game_audio_sound_play_bgm(&sound, 200, true));
  assert(audio_voices[4].playing && audio_voices[4].volume == 200 && audio_bgm_loop);
  assert(game_audio_sound_play_bgm(&sound, 100, false));
  assert(audio_voices[4].playing && audio_voices[4].volume == 100 && !audio_bgm_loop);
  game_audio_bgm_stop();
  assert(!audio_voices[4].playing && audio_voices[0].playing);
  for (uint i = 0; i < 5; i++) audio_voices[i].playing = false;
  audio_buffer_valid[audio_current_buffer ^ 1] = false;
  dma_hw->ints0 = 1;
  audio_dma_irq_handler();
  assert(!audio_playing && audio_sample_rate == 0);
  for (uint i = 0; i < 5; i++) assert(!audio_voices[i].playing);
  assert(game_audio_sound_play(&sound, 128));
  assert(starts==2 && audio_playing);
  /* A voice arrives while the next output buffer is already marked EOF. */
  audio_buffer_valid[audio_current_buffer ^ 1] = false;
  assert(game_audio_sound_play_bgm(&sound, 180, true));
  dma_hw->ints0 = 1;
  audio_dma_irq_handler();
  assert(audio_playing && audio_voices[4].playing);
  assert(audio_buffer_valid[audio_current_buffer]);
  game_audio_stop();
  assert(!audio_playing && audio_sample_rate==0 && !audio_buffer_valid[0] && !audio_buffer_valid[1]);
  for(uint i=0;i<5;i++)assert(!audio_voices[i].playing && audio_voices[i].sound==NULL);
#if GAME_AUDIO_I2S
  assert(!i2s_enabled);
#endif
  puts("audio regression: PASS");
}
'''
for name in ['GAME_AUDIO_MIDDLE_LEVEL', 'GAME_AUDIO_MAX_VOICES']:
    definition = re.search(r'^#define ' + name + r' .+$', source, re.MULTILINE)
    assert definition, name
    harness, count = re.subn(r'^#define ' + name + r' .+$', definition[0], harness, flags=re.MULTILINE)
    assert count == 1, name

with tempfile.TemporaryDirectory(prefix='game-audio-test-') as directory:
    path = Path(directory)
    for mode in [0, 1]:
        text = harness
        if mode:
            text = text.replace('2048', '512')
            text = text.replace('static uint8_t audio_dma_buffers', 'static uint32_t audio_dma_buffers')
        (path / 'audio.c').write_text(text)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-Wno-unused-variable', '-fsanitize=address,undefined',
                        f'-DGAME_AUDIO_I2S={mode}', '-DGAME_AUDIO_I2S_CHAIN_DMA=0', '-I'+str(root/'include'), str(path / 'audio.c'), '-o', str(path / 'audio')], check=True)
        subprocess.run([str(path / 'audio')], check=True)
