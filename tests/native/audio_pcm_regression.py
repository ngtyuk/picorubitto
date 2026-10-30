#!/usr/bin/env python3
"""Render the production PCM mixer, including its two-buffer initial prefill.

This verifies digital samples, not IRQ deadlines, PWM filtering, or the amplifier.
"""
from pathlib import Path
import argparse
import json
import os
import re
import subprocess
import tempfile
import wave

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, help='export captured PCM WAVs and metrics')
args = parser.parse_args()
source = (root / 'src/game_platform.c').read_text()

def structure(name):
    start = source.index('struct ' + name + ' {')
    return source[start:source.index('\n};', start) + 3]

def function(name, result):
    start = source.index('\n' + name + '(')
    return result + source[start:source.index('\n}', start) + 2] + '\n'

harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "game_audio_dial.h"
static uint8_t master_volume = 255;
static uint8_t game_audio_master_volume(void) { return master_volume; }
typedef unsigned int uint;
#define GAME_AUDIO_MAX_VOICES 5
#define GAME_AUDIO_BGM_VOICE 4
#define GAME_AUDIO_BUFFER_SAMPLES 2048
#define GAME_AUDIO_MIDDLE_LEVEL 128
#define GAME_AUDIO_FADE_MS 10
typedef struct game_audio_sound game_audio_sound_t;
'''
harness += structure('game_audio_sound') + '\n' + structure('game_audio_voice')
harness += '\nstatic struct game_audio_voice audio_voices[5];\nstatic bool audio_bgm_loop;\nstatic uint8_t audio_dma_buffers[2][2048];\n'
for name, result in [('audio_fade_samples', 'static size_t'),
                     ('audio_envelope_gain', 'static uint8_t'),
                     ('audio_mix_sample', 'static bool'),
                     ('audio_fill_buffer', 'static bool')]:
    harness += function(name, result)
harness += r'''
int main(int argc, char **argv) {
  /* BGM remains unchanged when silent SE starts and ends; worst-case mix fits. */
  uint8_t bgm_data[1024], se_data[1024];
  for (unsigned i=0;i<1024;i++) { bgm_data[i]=200; se_data[i]=128; }
  game_audio_sound_t bgm = {.sample_data=bgm_data,.sample_size=1024,.sample_rate=1000};
  game_audio_sound_t se = {.sample_data=se_data,.sample_size=1024,.sample_rate=1000};
  audio_voices[4]=(struct game_audio_voice){&bgm,100,255,true};
  int32_t baseline, mixed;
  assert(audio_mix_sample(&baseline) && baseline==36);
  for (unsigned i=0;i<4;i++) audio_voices[i]=(struct game_audio_voice){&se,100,255,true};
  assert(audio_mix_sample(&mixed) && mixed==baseline);
  for (unsigned i=0;i<1024;i++) se_data[i]=255;
  assert(audio_mix_sample(&mixed) && mixed==baseline+4*15);
  for (unsigned i=0;i<4;i++) audio_voices[i].playing=false;
  assert(audio_mix_sample(&mixed) && mixed==baseline);
  for (unsigned i=0;i<5;i++) audio_voices[i].playing=false;
  /* Loop crosses the end without a fade or an inactive sample. */
  audio_bgm_loop=true;
  audio_voices[4]=(struct game_audio_voice){&bgm,1023,255,true};
  assert(audio_mix_sample(&mixed) && mixed==36 && audio_voices[4].position==0);
  assert(audio_mix_sample(&mixed) && mixed==36 && audio_voices[4].position==1);
  audio_bgm_loop=false;
  audio_voices[4].position=1023;
  assert(audio_mix_sample(&mixed) && !audio_voices[4].playing);
  assert(!audio_mix_sample(&mixed));
  if (getenv("MASTER_VOLUME")) master_volume = (uint8_t)atoi(getenv("MASTER_VOLUME"));
  assert(argc >= 4); int count = atoi(argv[1]);
  assert(count >= 1 && count <= 4 && argc == 2 + 2 * count);
  game_audio_sound_t sounds[4] = {0}; size_t total = 0;
  for(int i=0;i<count;i++) {
    FILE *file=fopen(argv[2+2*i],"rb"); assert(file);
    assert(fseek(file,0,SEEK_END)==0); long length=ftell(file); assert(length>0);
    rewind(file); uint8_t *samples=malloc((size_t)length); assert(samples);
    assert(fread(samples,1,(size_t)length,file)==(size_t)length); fclose(file);
    sounds[i].sample_data=samples; sounds[i].sample_size=(size_t)length;
    sounds[i].sample_rate=22050;
    audio_voices[i].sound=&sounds[i];
    audio_voices[i].volume=(uint8_t)atoi(argv[3+2*i]);
    audio_voices[i].playing=(i==0);
    size_t end=(size_t)length+(i==0?0:4096);
    if(end>total) total=end;
  }
  for(size_t offset=0;offset<total;offset+=2048) {
    if(offset==4096) for(int i=1;i<count;i++) audio_voices[i].playing=true;
    audio_fill_buffer(0);
    size_t size=total-offset; if(size>2048) size=2048;
    assert(fwrite(audio_dma_buffers[0],1,size,stdout)==size);
  }
  for(int i=0;i<count;i++) free((void*)sounds[i].sample_data);
}
'''
for name in ['GAME_AUDIO_MIDDLE_LEVEL', 'GAME_AUDIO_FADE_MS', 'GAME_AUDIO_MAX_VOICES']:
    definition = re.search(r'^#define ' + name + r' .+$', source, re.MULTILINE)
    assert definition, name
    harness, count = re.subn(r'^#define ' + name + r' .+$', definition[0], harness, flags=re.MULTILINE)
    assert count == 1, name

metrics = {}
captures = {}
with tempfile.TemporaryDirectory(prefix='gaming-audio-pcm-') as directory:
    directory = Path(directory)
    subprocess.run(['ruby', str(root / 'tools/fixtures/generate_audio_checks.rb'), str(directory)],
                   check=True, stdout=subprocess.DEVNULL)
    fixtures = {}
    for path in (directory / 'audio').glob('*.wav'):
        with wave.open(str(path), 'rb') as reader:
            assert (reader.getnchannels(), reader.getsampwidth(), reader.getframerate()) == (1, 1, 22050)
            pcm = reader.readframes(reader.getnframes())
        raw = directory / (path.stem + '.pcm')
        raw.write_bytes(pcm)
        fixtures[path.stem] = raw
    code = directory / 'mixer.c'
    code.write_text(harness)
    binary = directory / 'mixer'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-g',
                    '-fsanitize=address,undefined', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'include'), str(code), '-o', str(binary)], check=True)

    def capture(label, voices):
        command = [str(binary), str(len(voices))]
        for name, volume in voices:
            command += [str(fixtures[name]), str(volume)]
        data = subprocess.check_output(command)
        captures[label] = data
        metrics[label] = {'samples': len(data), 'minimum': min(data), 'maximum': max(data),
                          'rail_samples': data.count(0) + data.count(255)}
        assert metrics[label]['rail_samples'] == 0, (label, metrics[label])
        return data

    capture('v17-bgm-effects', [('bgm', 160), ('jump', 192), ('hit', 192), ('coin', 192)])
    for volume in [32, 64, 128, 192]:
        live = capture(f'live4-{volume}', [(f'mix-tone-{i}', volume) for i in range(4)])
        premix = capture(f'premix-{volume}', [('mix-reference', volume)])
        capture(f'single-{volume}', [('mix-tone-0', volume)])
        # Ignore the prefill, attack, and mismatched natural ending times.
        differences = [abs(int((a-128)/4) - (b-128)) for a,b in zip(live[8192:65536], premix[8192:65536])]
        error = max(differences)
        assert error <= 2, (volume, error)
        metrics[f'live4-{volume}']['steady_max_error_vs_premix_lsb'] = error

    command = [str(binary), '1', str(fixtures['mix-tone-0']), '192']
    reference = captures['single-192']
    for master in [0, 1, 32, 127, 255]:
        data = subprocess.check_output(command, env={**os.environ, 'MASTER_VOLUME': str(master)})
        expected = bytes(128 + int((sample - 128) * master / 255) for sample in reference)
        assert data == expected, master
    print('PWM master gain: PASS exact centred PCM at 0, 1, 32, 127, 255')
    for gain in [0, 50, 100]:
        limited = directory / f'mixer-{gain}'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-g',
                        '-fsanitize=address,undefined', '-Wall', '-Wextra', '-Werror',
                        f'-DGAME_AUDIO_MAX_GAIN={gain}', '-I' + str(root / 'include'),
                        str(code), '-o', str(limited)], check=True)
        for master in [0, 127, 255]:
            data = subprocess.check_output([str(limited), *command[1:]],
                                           env={**os.environ, 'MASTER_VOLUME': str(master)})
            expected = bytes(128 + int(int((sample - 128) * master / 255) * gain / 100)
                             for sample in reference)
            assert data == expected, (gain, master)
    print('PWM output ceiling: PASS 0%, 50%, 100% with master 0, 127, 255')

if args.output:
    args.output.mkdir(parents=True, exist_ok=True)
    for label, data in captures.items():
        with wave.open(str(args.output / (label + '.wav')), 'wb') as writer:
            writer.setnchannels(1); writer.setsampwidth(1); writer.setframerate(22050)
            writer.writeframes(data)
    (args.output / 'metrics.json').write_text(json.dumps(metrics, indent=2) + '\n')
print(json.dumps(metrics, indent=2))
print('Production PCM: PASS; no rail samples; scaled live/premix within 2 LSB. Hardware untested.')
