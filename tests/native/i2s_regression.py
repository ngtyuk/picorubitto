#!/usr/bin/env python3
"""Check resampling, production I2S buffers and assembled PIO wire timing."""
from pathlib import Path
import json
import os
import re
import subprocess
import tempfile
import wave

root = Path(__file__).resolve().parents[2]
source = (root / 'src/game_platform.c').read_text()

def structure(name):
    start = source.index('struct ' + name + ' {')
    return source[start:source.index('\n};', start) + 3]

def function(name, result):
    start = source.index('\n' + name + '(')
    return result + source[start:source.index('\n}', start) + 2] + '\n'

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_audio_i2s.h"
#include "game_audio_dial.h"
static uint8_t master_volume = 255;
static uint8_t game_audio_master_volume(void) { return master_volume; }
typedef unsigned int uint;
static unsigned input_index, input_size;
static int32_t fixture(unsigned i) {return ((int32_t)(i%128)-64)*256;}
static bool read_fixture(int32_t *sample) {
  bool active=input_index<input_size;
  *sample=active?fixture(input_index):0; input_index++; return active;
}
#define GAME_AUDIO_I2S 1
#define GAME_AUDIO_MAX_VOICES 5
#define GAME_AUDIO_BGM_VOICE 4
#define GAME_AUDIO_BUFFER_SAMPLES 512
#define GAME_AUDIO_MIDDLE_LEVEL 128
#define GAME_AUDIO_FADE_MS 10
typedef struct game_audio_sound game_audio_sound_t;
'''
code += structure('game_audio_sound') + '\n' + structure('game_audio_voice')
code += '''
static struct game_audio_voice audio_voices[5];
static bool audio_bgm_loop;
static struct game_audio_resampler audio_resampler;
static struct game_audio_pcm_probe audio_pcm_probe;
static uint32_t audio_sample_rate;
static uint32_t audio_dma_buffers[2][512];
'''
for name, result in [('audio_fade_samples', 'static size_t'),
                     ('audio_envelope_gain', 'static uint8_t'),
                     ('audio_mix_sample', 'static bool'),
                     ('audio_i2s_read_sample', 'static bool'),
                     ('audio_fill_buffer', 'static bool')]:
    code += function(name, result)
code += r'''
int main(int argc, char **argv) {
  if (argc == 3) {
    FILE *file = fopen(argv[1], "rb"); assert(file);
    uint8_t header[44]; assert(fread(header, 1, 44, file) == 44);
    uint8_t data[128*1024]; size_t size = fread(data, 1, sizeof(data), file); fclose(file);
    game_audio_sound_t sound = {.sample_data=data, .sample_size=size, .sample_rate=(uint32_t)atoi(argv[2])};
    audio_sample_rate = sound.sample_rate;
    audio_voices[0] = (struct game_audio_voice){&sound, 0, 255, true};
    game_audio_pcm_probe_reset(&audio_pcm_probe);
    for (unsigned i=0; i<8; i++) assert(audio_fill_buffer(i%2));
    assert(audio_pcm_probe.frames == 4096);
    printf("%08x %u\n", audio_pcm_probe.fnv1a, audio_pcm_probe.max_step);
    return 0;
  }
  // Full-gain U8 PCM must remain centred and preserve both half-waves.
  // Use the real unsigned production constants, especially the midpoint.
  uint8_t flat[2048];
  game_audio_sound_t flat_sound = {.sample_data=flat, .sample_size=sizeof(flat), .sample_rate=22050};
  unsigned volumes[] = {0,1,32,64,128,192,255};
  for(unsigned byte=0; byte<=255; byte++) {
    memset(flat, (int)byte, sizeof(flat));
    for(unsigned v=0; v<sizeof(volumes)/sizeof(volumes[0]); v++) {
      memset(audio_voices,0,sizeof(audio_voices));
      audio_voices[0]=(struct game_audio_voice){&flat_sound,512,(uint8_t)volumes[v],true};
      int32_t sample=999;
      assert(audio_mix_sample(&sample));
      int32_t expected=((int32_t)byte-128)*(int32_t)volumes[v]/(255*8);
      assert(sample==expected);
    }
  }
  memset(audio_voices,0,sizeof(audio_voices));
  // Negative samples must keep their sign during attack/release as well.
  memset(flat,0,sizeof(flat));
  unsigned positions[] = {0,1,110,220,221,1828,2046,2047};
  for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);i++) {
    unsigned position=positions[i], gain=255;
    if(position<221) gain=position*255/221;
    unsigned remaining=2048-position;
    if(remaining<221 && remaining*255/221<gain)gain=remaining*255/221;
    audio_voices[0]=(struct game_audio_voice){&flat_sound,position,192,true};
    int32_t sample;
    assert(audio_mix_sample(&sample));
    int effective=(192*(int)gain+127)/255;
    assert(sample==(-128*effective)/(255*8));
  }
  // Equal opposite half-waves cancel before the four-voice average/clamp.
  uint8_t positive[2048], negative[2048];
  memset(positive,192,sizeof(positive));memset(negative,64,sizeof(negative));
  game_audio_sound_t pos_sound={.sample_data=positive,.sample_size=2048,.sample_rate=22050};
  game_audio_sound_t neg_sound={.sample_data=negative,.sample_size=2048,.sample_rate=22050};
  for(unsigned voices=2;voices<=4;voices+=2) {
    memset(audio_voices,0,sizeof(audio_voices));
    for(unsigned i=0;i<voices;i++)audio_voices[i]=(struct game_audio_voice){i%2?&neg_sound:&pos_sound,512,255,true};
    int32_t sample;assert(audio_mix_sample(&sample));assert(sample==0);
  }
  memset(audio_voices,0,sizeof(audio_voices));
  puts("U8 mixer: PASS all 256 bytes x 7 gains; negative fades; 2/4-voice cancellation; production constant types");
  // Exercise the complete phase range at the largest positive/negative delta,
  // then random signed endpoints. Reference arithmetic remains 64-bit.
  uint32_t random = 19;
  for(unsigned i=0; i<300000; i++) {
    random = random * 1664525u + 1013904223u;
    int32_t a = i<96000 ? -32768 : (int16_t)random;
    int32_t b = i<96000 ? 32767 : (int16_t)(random>>16);
    if(i<96000 && i>=48000) { a=32767; b=-32768; }
    uint32_t phase = i<96000 ? i%48000 : random%48000;
    int32_t expected = a + (int32_t)(((int64_t)b-a)*phase/48000);
    assert(game_audio_i2s_interpolate(a,b,phase)==expected);
  }
  game_audio_pcm_probe_reset(&audio_pcm_probe);
  for(unsigned i=0; i<4096; i++) game_audio_pcm_probe_sample(&audio_pcm_probe, 0);
  struct game_audio_pcm_probe full = audio_pcm_probe;
  game_audio_pcm_probe_sample(&audio_pcm_probe, 32767);
  assert(memcmp(&full, &audio_pcm_probe, sizeof(full)) == 0);
  assert(full.frames == 4096 && full.max_step == 0);
  game_audio_pcm_probe_reset(&audio_pcm_probe);
  assert(audio_pcm_probe.frames == 0 && audio_pcm_probe.fnv1a == 2166136261u);
  uint32_t rates[]={1,4000,8000,11025,22050,32000,44100,48000,88200,96000};
  unsigned long comparisons=0;
  for(unsigned rate_index=0;rate_index<sizeof(rates)/sizeof(rates[0]);rate_index++) {
    uint32_t rate=rates[rate_index];
    struct game_audio_resampler state={0}; input_index=0; input_size=37;
    uint64_t frames=((uint64_t)input_size*48000+rate-1)/rate;
    for(uint64_t frame=0;frame<frames+3;frame++) {
      bool active; uint32_t word=game_audio_i2s_word(&state,rate,read_fixture,&active);
      uint64_t position=frame*rate; unsigned index=(unsigned)(position/48000);
      int32_t a=index<input_size?fixture(index):0;
      int32_t b=index+1<input_size?fixture(index+1):0;
      int32_t expected=a+(int32_t)(((int64_t)b-a)*(position%48000)/48000);
      assert((int16_t)(word>>16)==expected && (int16_t)word==expected);
      assert(active==(frame<frames)); comparisons++;
    }
  }
  uint8_t data[1000];
  for(unsigned i=0;i<sizeof(data);i++)data[i]=(uint8_t)(i*17);
  game_audio_sound_t sound={.sample_data=data,.sample_size=sizeof(data),.sample_rate=22050};
  for(unsigned voices=1;voices<=4;voices++) {
    memset(audio_voices,0,sizeof(audio_voices));memset(&audio_resampler,0,sizeof(audio_resampler));
    audio_sample_rate=sound.sample_rate;
    for(unsigned i=0;i<voices;i++) audio_voices[i]=(struct game_audio_voice){&sound,0,192,true};
    unsigned valid=0;
    for(unsigned buffer=0;buffer<10;buffer++) {
      valid+=audio_fill_buffer(buffer%2);
      for(unsigned sample=0;sample<512;sample++) {
        uint32_t word=audio_dma_buffers[buffer%2][sample];
        assert((uint16_t)(word>>16)==(uint16_t)word);
        assert((int16_t)word>=-32768 && (int16_t)word<=32512);
      }
    }
    assert(valid==5); // 1000 source samples -> 2177 output frames -> 5 buffers.
    assert(!audio_fill_buffer(2));
  }
  uint32_t reference[512];
  for(unsigned master=0;master<=255;master++) {
    memset(audio_voices,0,sizeof(audio_voices));memset(&audio_resampler,0,sizeof(audio_resampler));
    audio_voices[0]=(struct game_audio_voice){&sound,0,192,true};
    master_volume=255; audio_fill_buffer(0); memcpy(reference,audio_dma_buffers[0],sizeof(reference));
    memset(&audio_resampler,0,sizeof(audio_resampler));audio_voices[0].position=0;audio_voices[0].playing=true;
    master_volume=(uint8_t)master; audio_fill_buffer(1);
    for(unsigned i=0;i<512;i++) {
      int32_t expected=(int16_t)reference[i]*(int32_t)master/255;
      assert((int16_t)audio_dma_buffers[1][i]==expected);
      assert((int16_t)(audio_dma_buffers[1][i]>>16)==expected);
    }
  }
  printf("I2S PCM: PASS %lu resampled frames; rates 1..96000; 1..4 voices; EOF and stereo\n",comparisons);
}
'''
# Preserve the types as well as the values used by the firmware. Re-declaring
# 128u as 128 hid unsigned promotion of the negative half-wave in the mixer.
for name in ['GAME_AUDIO_MIDDLE_LEVEL', 'GAME_AUDIO_FADE_MS', 'GAME_AUDIO_MAX_VOICES']:
    definition = re.search(r'^#define ' + name + r' .+$', source, re.MULTILINE)
    assert definition, name
    code, count = re.subn(r'^#define ' + name + r' .+$', definition[0], code, flags=re.MULTILINE)
    assert count == 1, name

with tempfile.TemporaryDirectory(prefix='gaming-i2s-') as temporary:
    directory = Path(temporary)
    path = directory / 'test.c'; path.write_text(code)
    binary = directory / 'test'
    for wide in [0, 1]:
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-g',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    f'-DGAME_AUDIO_I2S_WIDE_MATH={wide}',
                    '-I' + str(root / 'include'), str(path), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        subprocess.run(['ruby', str(root/'tools/fixtures/generate_audio_checks.rb'), str(directory)],
                       stdout=subprocess.DEVNULL, check=True)
        metrics = {}
        for name, rate in [('tone-48000',48000), ('tone-22050',22050), ('silence-48000',48000)]:
            wav = directory / ('audio-rate-' + name + '.wav')
            with wave.open(str(wav), 'rb') as reader:
                pcm = reader.readframes(reader.getnframes())
            # Independent absolute-position reference with exact signed truncation.
            samples = []
            for frame in range(4096):
                position, phase = divmod(frame*rate,48000)
                def sample(index):
                    fade = (rate*10+999)//1000
                    gain = min(255,index*255//fade,(len(pcm)-index)*255//fade)
                    value = (pcm[index]-128)*gain
                    return (abs(value)//(255*8))*(1 if value>=0 else -1)*256
                a,b = sample(position),sample(position+1)
                delta = (b-a)*phase
                samples.append(a+(abs(delta)//48000)*(1 if delta>=0 else -1))
            hashvalue=2166136261
            for sample in samples:
                for byte in [sample&255,(sample>>8)&255]:
                    hashvalue=((hashvalue^byte)*16777619)&0xffffffff
            step=max(abs(b-a) for a,b in zip(samples,samples[1:]))
            actual=subprocess.check_output([str(binary),str(wav),str(rate)],text=True).strip()
            assert actual==f'{hashvalue:08x} {step}', (wide,name,actual,hashvalue,step)
            metrics[name]={'pcm_frames':4096,'pcm_fnv1a':f'{hashvalue:08x}','pcm_max_step':step}
        print(f'I2S math={32 if wide==0 else 64}: PASS 300000 signed endpoint/phase cases; probes '+json.dumps(metrics))
        if os.environ.get('I2S_PROBE_OUTPUT'):
            Path(os.environ['I2S_PROBE_OUTPUT']).write_text(json.dumps(metrics,indent=2)+'\n')

    # Compare actual output hashes with independent samples at each ceiling.
    wav = directory / 'audio-rate-tone-48000.wav'
    with wave.open(str(wav), 'rb') as reader:
        pcm = reader.readframes(reader.getnframes())
    for gain in [0, 50, 100]:
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2',
                        '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        f'-DGAME_AUDIO_MAX_GAIN={gain}', '-I' + str(root / 'include'),
                        str(path), '-o', str(binary)], check=True)
        samples = []
        for index in range(4096):
            fade = 480
            envelope = min(255, index*255//fade, (len(pcm)-index)*255//fade)
            value = (pcm[index]-128)*envelope
            sample = (abs(value)//(255*8))*(1 if value>=0 else -1)*256
            samples.append((abs(sample)*gain//100)*(1 if sample>=0 else -1))
        checksum = 2166136261
        for sample in samples:
            for byte in [sample & 255, (sample >> 8) & 255]:
                checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
        step = max(abs(b-a) for a,b in zip(samples,samples[1:]))
        actual = subprocess.check_output([str(binary), str(wav), '48000'], text=True).strip()
        assert actual == f'{checksum:08x} {step}', (gain, actual, checksum, step)
    print('I2S output ceiling: PASS 0%, 50%, 100% production buffer')

    # Assemble the actual PIO program with the SDK tool, then execute its opcodes.
    assembler = os.environ.get('PIOASM')
    if not assembler:
        candidates = sorted(root.glob('build/*/pioasm-install/pioasm/pioasm'))
        assert candidates, 'Build engine --audio-output i2s first, or set PIOASM'
        assembler = str(candidates[0])
    output = directory / 'program.hex'
    subprocess.run([assembler, '-o', 'hex', str(root / 'src/game_audio_i2s.pio'), str(output)], check=True)
    program = [int(line, 16) for line in output.read_text().splitlines()]
    assert len(program) == 8
    words = [0x1234abcd, 0x80007fff, 0xffff0000, 0x5555aaaa]
    pc = 7; x = 0; shift = 0; remaining = 0; fifo = iter(words)
    data_pin = 0; previous_clock = 0; rising = []
    for cycle in range(64*len(words)+1):
        instruction = program[pc]
        side = (instruction >> 11) & 3
        opcode = instruction >> 13
        following = (pc+1) % len(program)
        if opcode == 3: # OUT PINS, 1; MSB-first autopull threshold 32.
            assert instruction & 0xff == 1
            if remaining == 0:
                shift = next(fifo); remaining = 32
            data_pin = shift >> 31
            shift = (shift << 1) & 0xffffffff; remaining -= 1
        elif opcode == 7: # SET X
            assert (instruction >> 5) & 7 == 1
            x = instruction & 31
        elif opcode == 0: # JMP X--
            assert (instruction >> 5) & 7 == 2
            if x != 0: following = instruction & 31
            x = (x-1) & 0xffffffff
        else: raise AssertionError(instruction)
        clock = side & 1
        if clock and not previous_clock: rising.append((cycle, side >> 1, data_pin))
        previous_clock = clock; pc = following
    assert all(b[0]-a[0] == 2 for a,b in zip(rising, rising[1:]))
    for frame, expected in enumerate(words):
        samples = rising[1+32*frame:1+32*(frame+1)]
        assert len(samples) == 32
        decoded = 0
        for bit, (_, ws, value) in enumerate(samples):
            assert ws == (1 if 15 <= bit < 31 else 0), (frame, bit, ws)
            decoded = (decoded << 1) | value
        assert decoded == expected, (frame, hex(decoded), hex(expected))
    print('I2S PIO: PASS assembled opcodes; MSB order, 16-bit stereo, WS one-bit delay, 32 BCLK/frame')

for arguments in [['--i2s-bclk', '25'], ['--i2s-data', '27']]:
    result = subprocess.run(['ruby', str(root / 'tools/build/workflow.rb'), 'engine',
                             '--audio-output', 'i2s', *arguments], capture_output=True, text=True)
    assert result.returncode != 0 and 'I2S uses free GPIOs' in result.stderr
print('I2S pin collision rejection: PASS')
result = subprocess.run(['ruby',str(root/'tools/build/workflow.rb'),'engine','--i2s-wide-math'], capture_output=True,text=True)
assert result.returncode != 0 and 'invalid option' in result.stderr
