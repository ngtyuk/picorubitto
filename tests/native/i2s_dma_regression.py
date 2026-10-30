#!/usr/bin/env python3
"""Run production chained-DMA functions against a clocked DMA / W1C model."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/game_platform.c').read_text()

def function(name, result):
    start = source.index('\n' + name + '(')
    return result + source[start:source.index('\n}', start) + 2] + '\n'

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "game_audio_i2s.h"
#define GAME_AUDIO_I2S 1
#define GAME_AUDIO_I2S_CHAIN_DMA 1
#define GAME_AUDIO_I2S_RATE 48000u
#define GAME_AUDIO_BUFFER_SAMPLES 512u
#define GAME_AUDIO_MAX_VOICES 5u
#define GAME_AUDIO_SE_VOICES 4u
#define GAME_AUDIO_MAX_SE_BYTES (128u*1024u)
#define GAME_AUDIO_BGM_VOICE 4u
#define DMA_SIZE_32 2
#define PIO_FDEBUG_TXSTALL_LSB 24
using uint = unsigned;
// Real peripheral registers clear written bits, rather than store the mask.
struct w1c {
  uint32_t bits = 0;
  operator uint32_t() const { return bits; }
  void operator=(uint32_t mask) { bits &= ~mask; }
  void raise(uint32_t mask) { bits |= mask; }
};
static struct { w1c ints0; } dma_device, *dma_hw = &dma_device;
static struct { uint32_t txf[4]; w1c fdebug; } pio_device, *audio_pio = &pio_device;
struct dma_channel_config { uint chain; uint ring; };
struct dma_model {
  dma_channel_config config = {};
  const void *read = nullptr;
  uint count = 0;
  bool busy = false, irq = false;
};
static dma_model channels[2];
static int audio_dma_channel = 0, audio_dma_peer = 1, audio_sm = 0;
static bool audio_playing, audio_output_ready = true, i2s_enabled;
static uint32_t audio_sample_rate;
static bool audio_buffer_valid[2];
static uint audio_current_buffer;
static uint32_t audio_dma_buffers[2][512] __attribute__((aligned(2048)));
typedef struct { const uint8_t *sample_data; size_t sample_size; uint32_t sample_rate; size_t wav_bytes; } game_audio_sound_t;
static struct { const game_audio_sound_t *sound; size_t position; uint8_t volume; bool playing; } audio_voices[5];
static bool audio_bgm_loop;
static uint32_t audio_i2s_blocks, audio_i2s_stalls, audio_i2s_late_refills;
static uint32_t audio_i2s_late_irqs, audio_i2s_refill_max_us;
static struct game_audio_pcm_probe audio_pcm_probe;
static uint32_t now_us, refill_us = 1000;
static bool fill_valid[2] = {true, true}, complete_during_refill;
static uint fills, starts, automatic_starts, writes_to_active;
static bool irq_disabled;
static uint32_t time_us_32(void) { return now_us; }
static uint32_t save_and_disable_interrupts(void) { bool previous=irq_disabled;irq_disabled=true;return previous; }
static void restore_interrupts(uint32_t previous) { irq_disabled=previous; }
static void audio_i2s_stop(void) { i2s_enabled=false; }
static bool pio_sm_is_tx_fifo_empty(void *, uint) { assert(channels[0].busy); return false; }
static void tight_loop_contents(void) {}
static uint pio_get_dreq(void *, uint, bool) { return 66; }
static void pio_sm_set_enabled(void *, uint, bool value) { i2s_enabled=value; }
static void dma_channel_set_irq0_enabled(uint ch, bool value) { channels[ch].irq=value; }
static bool dma_channel_is_busy(uint ch) { return channels[ch].busy; }
static dma_channel_config dma_channel_get_default_config(uint ch) { return {ch,0}; }
static dma_channel_config dma_get_channel_config(uint ch) { return channels[ch].config; }
static void channel_config_set_transfer_data_size(dma_channel_config *, int size) { assert(size==DMA_SIZE_32); }
static void channel_config_set_read_increment(dma_channel_config *, bool value) { assert(value); }
static void channel_config_set_write_increment(dma_channel_config *, bool value) { assert(!value); }
static void channel_config_set_dreq(dma_channel_config *, uint value) { assert(value==66); }
static void channel_config_set_ring(dma_channel_config *config, bool write, uint bits) { assert(!write && bits==11);config->ring=bits; }
static void channel_config_set_chain_to(dma_channel_config *config, uint ch) { config->chain=ch; }
static void dma_channel_set_config(uint ch, dma_channel_config *config, bool trigger) { assert(!trigger);channels[ch].config=*config; }
static void dma_channel_abort(uint ch) { assert(channels[ch].config.chain==ch);channels[ch].busy=false; }
static void dma_channel_set_trans_count(uint ch, uint count, bool trigger) { assert(!channels[ch].busy);channels[ch].count=count;if(trigger) channels[ch].busy=true; }
static void dma_channel_configure(uint ch, dma_channel_config *config, volatile void *dst, const void *src, uint count, bool trigger) {
  assert(!channels[ch].busy && dst==&audio_pio->txf[0]);
  assert(src==audio_dma_buffers[ch] && ((uintptr_t)src&2047)==0);
  assert(config->ring==11 && config->chain==(ch^1));
  bool irq_enabled=channels[ch].irq;
  channels[ch]={*config,src,count,trigger,irq_enabled};
  if(trigger) starts++;
}
static void complete(uint ch) {
  assert(channels[ch].busy);
  channels[ch].busy=false;
  if(channels[ch].irq) dma_hw->ints0.raise(1u<<ch);
  uint next=channels[ch].config.chain;
  if(next!=ch && channels[next].count) {
    // A read ring wraps the address on every block, including delayed IRQs.
    assert(channels[next].read==audio_dma_buffers[next] && channels[next].config.ring==11);
    channels[next].busy=true; automatic_starts++;
  }
}
static bool audio_fill_buffer(uint index) {
  if(audio_playing && channels[index].busy) writes_to_active++;
  assert(writes_to_active==0); fills++;
  now_us+=refill_us;
  if(complete_during_refill) { complete(index^1); complete_during_refill=false; }
  return fill_valid[index];
}
'''
for name, result in [('audio_i2s_abort_dma', 'static void'),
                     ('audio_i2s_check_stall', 'static void'),
                     ('audio_i2s_refill', 'static bool'),
                     ('audio_i2s_report_stats', 'static void'),
                     ('audio_dma_irq_handler', 'static void'),
                     ('game_audio_stop', 'void'),
                     ('audio_sound_play_kind', 'static bool'),
                     ('game_audio_sound_play', 'bool'),
                     ('game_audio_sound_play_bgm', 'bool'),
                     ('audio_i2s_watchdog', 'static void')]:
    code += function(name, result)
code += r'''
int main(void) {
  uint8_t samples[1]={128};game_audio_sound_t sound={samples,1,22050,0};
  assert(game_audio_sound_play(&sound,192));
  assert(starts==1 && channels[0].busy && !channels[1].busy);
  for(unsigned block=0;block<1000;block++) {
    unsigned done=block%2;
    complete(done);
    assert(channels[done^1].busy); // Already running BEFORE the IRQ is served.
    now_us+=500; // CPU delayed by another interrupt, well beyond the PIO FIFO.
    audio_dma_irq_handler();
    assert(!channels[done].busy && channels[done^1].busy);
    assert(!dma_hw->ints0 && audio_buffer_valid[done]);
  }
  assert(automatic_starts==1000 && audio_i2s_stalls==0 && audio_i2s_late_irqs==0);
  assert(audio_i2s_refill_max_us==1000 && audio_i2s_late_refills==0);
  assert(game_audio_sound_play(&sound,100) && starts==1); // Adding a voice keeps DMA.
  sound.sample_rate=44100;assert(!game_audio_sound_play(&sound,100));sound.sample_rate=22050;
  // Both IRQs pending: do not overwrite the already-active buffer.
  complete(0);complete(1);
  uint previous_fills=fills;
  audio_dma_irq_handler();
  assert(channels[0].busy && fills==previous_fills+1 && audio_i2s_late_irqs==1);
  // A late refill also remains inside its ring and is observable.
  complete(0);refill_us=11000;complete_during_refill=true;
  audio_dma_irq_handler();
  assert(audio_i2s_late_refills==1 && audio_i2s_late_irqs>=2);
  assert(channels[0].busy && !channels[1].busy);
  refill_us=1000;
  audio_pio->fdebug.raise(1u<<24);
  audio_dma_irq_handler(); // Completion of channel 1 occurred during the refill.
  assert(audio_i2s_stalls==1 && !audio_pio->fdebug);
  // Finish the currently playing block, marking its freed buffer silent/EOF.
  fill_valid[0]=false;
  complete(0);audio_dma_irq_handler();
  assert(!audio_buffer_valid[0] && channels[0].count==0 && channels[1].busy);
  // A new BGM arrives while the last old buffer is draining.
  for(auto &voice:audio_voices) voice.playing=false;
  assert(game_audio_sound_play_bgm(&sound, 180, true));
  fill_valid[0]=true;
  complete(1);audio_dma_irq_handler();
  assert(audio_playing && audio_voices[4].playing && channels[0].busy);
  assert(audio_buffer_valid[0] && audio_buffer_valid[1]);
  // Normal EOF still stops once the new voice has finished.
  for(auto &voice:audio_voices) voice.playing=false;
  fill_valid[0]=fill_valid[1]=false;
  complete(0);audio_dma_irq_handler();
  complete(1);audio_dma_irq_handler();
  assert(!audio_playing && !i2s_enabled && !channels[0].busy && !channels[1].busy);
  assert(!channels[0].irq && !channels[1].irq);
  // A fresh stream must restore both chain links after EOF/stop.
  fill_valid[0]=fill_valid[1]=true;
  assert(game_audio_sound_play(&sound,100) && starts==2);
  assert(channels[0].config.chain==1 && channels[1].config.chain==0);
  game_audio_stop();
  assert(!audio_playing && !channels[0].busy && !channels[1].busy && !irq_disabled);
  for(auto &voice:audio_voices)assert(!voice.playing && voice.sound==nullptr);
  // A short source has no second valid buffer; it must not loop stale PCM.
  fill_valid[0]=true; fill_valid[1]=false;
  assert(game_audio_sound_play(&sound,100));
  assert(channels[1].count==0 && !channels[1].busy);
  for(auto &voice:audio_voices) voice.playing=false;
  complete(0);audio_dma_irq_handler();
  assert(!audio_playing && !channels[0].busy && !channels[1].busy);
  game_audio_stop();
  // A new voice also survives when the second initial buffer was empty.
  fill_valid[0]=true; fill_valid[1]=false;
  assert(game_audio_sound_play(&sound,100));
  assert(game_audio_sound_play_bgm(&sound,180,true));
  fill_valid[1]=true; fill_valid[0]=false;
  complete(0);audio_dma_irq_handler();
  assert(audio_playing && audio_voices[4].playing && channels[1].busy);
  assert(audio_buffer_valid[1] && !audio_buffer_valid[0]);
  for(auto &voice:audio_voices) voice.playing=false;
  complete(1);audio_dma_irq_handler();
  assert(!audio_playing && !channels[0].busy && !channels[1].busy);
  // A stalled chain reports busy forever; the main-loop watchdog must
  // restart its looping BGM without stopping a healthy chain prematurely.
  fill_valid[0]=fill_valid[1]=true;
  assert(game_audio_sound_play_bgm(&sound, 180, true));
  uint started_before=starts;
  audio_i2s_watchdog();
  now_us+=249999; audio_i2s_watchdog();
  assert(starts==started_before && audio_playing);
  now_us++; audio_i2s_watchdog();
  assert(starts==started_before+1 && audio_playing && i2s_enabled);
  assert(audio_voices[4].playing && audio_voices[4].sound==&sound && audio_voices[4].volume==180 && audio_bgm_loop);
  audio_i2s_watchdog();
  for(unsigned i=0;i<30;i++) {
    complete(i%2); audio_dma_irq_handler();
    now_us+=10000; audio_i2s_watchdog();
  }
  assert(starts==started_before+1); // Continuous progress is never reset.
  game_audio_stop(); audio_i2s_watchdog();
  // A stalled short SE can also be restarted if no BGM is active.
  assert(game_audio_sound_play(&sound,100));
  started_before=starts; audio_i2s_watchdog(); now_us+=250000; audio_i2s_watchdog();
  assert(starts==started_before+1 && audio_voices[0].playing && audio_voices[0].volume==100);
  game_audio_stop(); audio_i2s_watchdog();
  // When only buffered audio remains, stale voice pointers are not restarted.
  assert(game_audio_sound_play_bgm(&sound,180,true));
  audio_voices[4].playing=false;
  started_before=starts; audio_i2s_watchdog(); now_us+=250000; audio_i2s_watchdog();
  assert(starts==started_before && !audio_playing);
  audio_i2s_watchdog();
  puts("I2S chained DMA: PASS 1000 switches, delayed IRQs, EOF arrival and stop/restart");
}
'''
with tempfile.TemporaryDirectory(prefix='game-i2s-dma-') as temporary:
    directory=Path(temporary)
    path=directory/'test.cpp';path.write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror',
                    '-Wno-unused-variable','-fsanitize=address,undefined','-I'+str(root/'include'),str(path),'-o',str(directory/'test')],check=True)
    subprocess.run([str(directory/'test')],check=True)

result = subprocess.run(['ruby',str(root/'tools/build/workflow.rb'),'engine','--i2s-single-dma'],
                        capture_output=True,text=True)
assert result.returncode != 0 and 'invalid option' in result.stderr
print('Removed single-DMA CLI rejected: PASS')
