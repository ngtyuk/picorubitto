#!/usr/bin/env python3
"""Verify disabled/enabled VM layouts and populated caches with real host VMs."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
picoruby = Path(os.environ.get('PICORUBY_ROOT', root / 'deps/picoruby'))
mruby = picoruby / 'mrbgems/picoruby-mruby/lib/mruby'
source = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <mruby.h>
#include <mruby/irep.h>
int main(int argc, char **argv) {
  assert(argc==2);
  FILE *file=fopen(argv[1],"rb"); assert(file);
  fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
  unsigned char *rom=malloc(size); assert(rom);
  assert(fread(rom,1,size,file)==(size_t)size); fclose(file);
  mrb_state *mrb=mrb_open(); assert(mrb);
  mrb_load_irep_buf(mrb,rom,size);
  if(mrb->exc) {mrb_print_error(mrb); return 1;}
#ifdef MRB_NO_METHOD_CACHE
  printf("METHOD_CACHE_ABI 0 %zu 0 0\n",sizeof(mrb_state));
#else
  unsigned populated=0;
  for(unsigned i=0;i<MRB_METHOD_CACHE_SIZE;i++) if(mrb->cache[i].c)populated++;
  assert(populated>0);
  printf("METHOD_CACHE_ABI %u %zu %zu %u\n",MRB_METHOD_CACHE_SIZE,
         sizeof(mrb_state),sizeof(mrb->cache),populated);
#endif
  mrb_close(mrb); free(rom); return 0;
}
'''
report = []
for entries in [0, 256]:
    build = root / 'build/tests' / ('minimum-profile' + (f'-method-cache{entries}' if entries else ''))
    library = build / 'minimum-profile/lib/libmruby.a'
    if not library.exists():
        subprocess.run(['python3', str(root / 'tests/native/minimum_profile_regression.py')],
                       env=dict(os.environ, GAME_METHOD_CACHE_SIZE=str(entries)), check=True)
    with tempfile.TemporaryDirectory(prefix='game-method-cache-') as name:
        directory = Path(name)
        (directory / 'main.c').write_text(source)
        contract = (root / 'tests/portable/game_api_contract.rb').read_text().replace("require 'game'\n", '')
        (directory / 'probe.rb').write_text('HARDWARE_SUITE=true\n' + contract + '\nrun_method_lookup_contract\n')
        rom = directory / 'probe.mrb'
        subprocess.run([str(build / 'mrbc/default/bin/mrbc'), '-o', str(rom), str(directory / 'probe.rb')], check=True)
        flags = ['-DMRB_INT64', '-DMRB_NO_BOXING', '-DMRB_UTF8_STRING',
                 '-DMRB_CONSTRAINED_BASELINE_PROFILE=1', '-DPICORB_VM_MRUBY']
        if entries:
            flags.append(f'-DMRB_METHOD_CACHE_SIZE={entries}')
        binary = directory / 'probe'
        subprocess.run([os.environ.get('CC', 'cc'), '-g', '-fsanitize=address,undefined', *flags,
                        '-I'+str(mruby / 'include'), '-I'+str(build / 'minimum-profile/include'),
                        str(directory / 'main.c'), str(library), '-lm', '-o', str(binary)], check=True)
        run = subprocess.run([str(binary), str(rom)], check=True, capture_output=True, text=True)
        print(run.stdout, end='')
        fields = run.stdout.strip().split()
        assert fields[0] == 'METHOD_CACHE_ABI' and int(fields[1]) == entries, fields
        report.append(dict(entries=entries, state_bytes=int(fields[2]),
                           cache_bytes=int(fields[3]), populated=int(fields[4])))
assert report[1]['state_bytes'] - report[0]['state_bytes'] == report[1]['cache_bytes'], report
(root / 'build/tests/method-cache-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
print('METHOD CACHE PASS: real VM layout, active cache, redefinition/alias/remove/include/prepend/singleton/GC')
