#!/usr/bin/env python3
"""Exercise VM placement with real mruby and PicoRuby's Estalloc allocator."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
picoruby = Path(os.environ.get('PICORUBY_ROOT', root / 'deps/picoruby'))
mruby = picoruby / 'mrbgems/picoruby-mruby/lib/mruby'
machine = picoruby / 'mrbgems/picoruby-machine'


def check_firmware(psram, sram, toolchain):
    def inspect(directory):
        elf = Path(directory) / 'pico_ruby_gaming.elf'
        output = subprocess.check_output([str(toolchain / 'arm-none-eabi-nm'), '-S', str(elf)], text=True)
        symbols = {}
        for line in output.splitlines():
            fields = line.split()
            if len(fields) in (3, 4):
                symbols[fields[-1]] = (int(fields[0], 16), int(fields[1], 16) if len(fields) == 4 else 0)
        output = subprocess.check_output([str(toolchain / 'arm-none-eabi-size'), '-A', str(elf)], text=True)
        sections = {fields[0]: int(fields[1]) for line in output.splitlines()
                    if len(fields := line.split()) == 3 and fields[0].startswith('.')}
        calls = {}
        for name in ['mrb_open_core', 'mrb_free']:
            calls[name] = subprocess.check_output([str(toolchain / 'arm-none-eabi-objdump'), '-d',
                                                  '--disassemble='+name, str(elf)], text=True)
        return symbols, sections, calls

    baseline, base_sections, base_calls = inspect(psram)
    candidate, sections, calls = inspect(sram)
    assert 'vm_state' not in baseline and '__wrap_mrb_basic_alloc_func' not in baseline
    address, state_bytes = candidate['vm_state']
    assert 0x20000000 <= address < address + state_bytes <= candidate['__bss_end__'][0]
    assert address % 8 == 0
    assert '<__wrap_mrb_basic_alloc_func>' in calls['mrb_open_core']
    assert '<__wrap_mrb_basic_alloc_func>' in calls['mrb_free']
    assert '<mrb_basic_alloc_func>' in base_calls['mrb_open_core']
    for symbol in ['framebuffers', 'sprite_cache', 'game_3d_depth_low_storage']:
        assert baseline[symbol][1] == candidate[symbol][1], symbol
    for section in ['.stack_dummy', '.stack1_dummy']:
        assert base_sections[section] == sections[section], section
    assert sections['.heap'] >= 8192
    headroom = candidate['__HeapLimit'][0] - candidate['__bss_end__'][0]
    assert headroom >= 8192
    assert state_bytes <= sections['.bss'] - base_sections['.bss'] < state_bytes + 32
    report = dict(state_address=hex(address), state_bytes=state_bytes, c_heap_headroom=headroom,
                  c_heap_reserved=sections['.heap'], core0_stack=sections['.stack_dummy'],
                  core1_stack=sections['.stack1_dummy'], framebuffer_bytes=candidate['framebuffers'][1],
                  sprite_cache_bytes=candidate['sprite_cache'][1], allocator_wrapping_verified=True)
    print('VM STATE ELF PASS ' + json.dumps(report))


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--firmware-psram', type=Path)
parser.add_argument('--firmware-sram', type=Path)
parser.add_argument('--arm-toolchain', type=Path,
                    default=Path(os.environ.get('ARM_TOOLCHAIN', '/usr/bin')))
args = parser.parse_args()
if args.firmware_psram or args.firmware_sram:
    if not (args.firmware_psram and args.firmware_sram):
        parser.error('provide both firmware directories')
    check_firmware(args.firmware_psram, args.firmware_sram, args.arm_toolchain)
    raise SystemExit(0)

source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <mruby.h>
#include <mruby/irep.h>
#include "game_vm.h"
#include "picorb_heap.h"
static unsigned char heap[4*1024*1024] __attribute__((aligned(8)));
static unsigned char other_heap[4*1024*1024] __attribute__((aligned(8)));
int main(int argc, char **argv) {
  assert(argc==2);
  FILE *file=fopen(argv[1],"rb"); assert(file);
  fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
  unsigned char *rom=malloc(size); assert(rom);
  assert(fread(rom,1,size,file)==(size_t)size); fclose(file);
  assert(game_vm_open(NULL,0)==NULL);
  unsigned used_after_close=0;
  for (unsigned round=0;round<3;round++) {
    mrb_state *mrb=game_vm_open(heap,sizeof(heap)); assert(mrb && !mrb->exc);
    assert((uintptr_t)mrb%8==0);
    assert(picorb_heap_owns(mrb)==!GAME_VM_STATE_SRAM);
    assert(picorb_heap_owns(mrb->c));
#if GAME_VM_STATE_SRAM
    /* Reject a second live console VM before it can reset the shared heap. */
    assert(game_vm_open(other_heap,sizeof(other_heap))==NULL);
    assert(picorb_heap_owns(mrb->c));
    assert(mrb_basic_alloc_func(mrb,sizeof(mrb_state)+1)==NULL);
    assert(picorb_heap_owns(mrb->c));
#endif
    /* Same-size ordinary requests must not take the special state block. */
    unsigned char *ordinary=mrb_malloc(mrb,sizeof(mrb_state)); assert(ordinary);
    assert(picorb_heap_owns(ordinary) && (void *)ordinary!=(void *)mrb);
    for(unsigned i=0;i<sizeof(mrb_state);i++) ordinary[i]=(unsigned char)i;
    ordinary=mrb_realloc(mrb,ordinary,sizeof(mrb_state)+4096); assert(ordinary);
    for(unsigned i=0;i<sizeof(mrb_state);i++) assert(ordinary[i]==(unsigned char)i);
    mrb_free(mrb,ordinary);
    mrb_load_irep_buf(mrb,rom,size);
    if(mrb->exc) {mrb_print_error(mrb); return 1;}
    mrb_full_gc(mrb);
    mrb_close(mrb);
    ESTALLOC_STAT stats; assert(picorb_heap_stat(&stats)==0);
    if(round) assert(stats.used==used_after_close);
    used_after_close=stats.used;
  }
  free(rom);
  printf("VM STATE PASS region=%s state_bytes=%zu (3 open/close cycles)\n",
         GAME_VM_STATE_SRAM ? "sram" : "psram",sizeof(mrb_state));
}
'''
stress = r'''
run_ruby_runtime_contract
rows = Array.new(64) { |i| [i, i * 3] }
100.times do
  total = 0
  table = {}
  rows.each do |row|
    total += row[0] + row[1]
    table[row[0]] = row[1]
  end
  raise 'array/hash workload' unless total == 8064 && table[63] == 189 && table.size == 64
  bytes = (0..255).map { |i| i.chr }.join
  raise 'byte workload' unless bytes.bytesize == 256 && bytes.getbyte(255) == 255
  GC.start
end
'''
for entries in [0, 256]:
    build = root / 'build/tests' / ('minimum-profile' + (f'-method-cache{entries}' if entries else ''))
    library = build / 'minimum-profile/lib/libmruby.a'
    if not library.exists():
        subprocess.run(['python3', str(root / 'tests/native/minimum_profile_regression.py')],
                       env=dict(os.environ, GAME_METHOD_CACHE_SIZE=str(entries)), check=True)
    with tempfile.TemporaryDirectory(prefix='game-vm-state-') as name:
        directory = Path(name)
        (directory / 'main.c').write_text(source)
        # The minimal host gem set has no allocator-statistics presyms. Only
        # that reporting function interns its names dynamically in this test.
        presym = directory / 'allocator-presym.h'
        presym.write_text('#include <mruby.h>\n#include <mruby/presym.h>\n'
                          '#undef MRB_SYM\n#define MRB_SYM(name) mrb_intern_cstr(mrb, #name)\n')
        contract = (root / 'tests/portable/game_api_contract.rb').read_text().replace("require 'game'\n", '')
        (directory / 'probe.rb').write_text('HARDWARE_SUITE=true\n' + contract + '\n' + stress)
        rom = directory / 'probe.mrb'
        subprocess.run([str(build / 'mrbc/default/bin/mrbc'), '-o', str(rom), str(directory / 'probe.rb')], check=True)
        flags = ['-g', '-fsanitize=address,undefined', '-DMRB_INT64', '-DMRB_NO_BOXING',
                 '-DMRB_UTF8_STRING', '-DMRB_CONSTRAINED_BASELINE_PROFILE=1',
                 '-DPICORB_VM_MRUBY', '-DPICORB_ALLOC_ESTALLOC', '-DESTALLOC_ALIGNMENT=8',
                 '-I'+str(mruby / 'include'), '-I'+str(build / 'minimum-profile/include'),
                 '-I'+str(machine / 'include'), '-I'+str(root / 'include')]
        if entries:
            flags.append(f'-DMRB_METHOD_CACHE_SIZE={entries}')
        compiler = os.environ.get('CC', 'cc')
        for sram in [0, 1]:
            objects = []
            units = [root / 'src/game_vm.c', machine / 'src/mruby/alloc.c',
                     machine / 'src/heap.c', machine / 'src/estalloc.c', directory / 'main.c']
            for i, unit in enumerate(units):
                aliases = []
                # Mach-O ld lacks GNU --wrap. Resolve the same two allocator
                # entry points by renaming only the wrapper and real allocator.
                if sram and i == 0:
                    aliases = ['-D__wrap_mrb_basic_alloc_func=mrb_basic_alloc_func',
                               '-D__real_mrb_basic_alloc_func=game_real_alloc']
                if sram and i == 1:
                    aliases = ['-Dmrb_basic_alloc_func=game_real_alloc']
                if i == 1:
                    aliases += ['-include', str(presym)]
                obj = directory / f'{sram}-{i}.o'
                subprocess.run([compiler, *flags, f'-DGAME_VM_STATE_SRAM={sram}', *aliases,
                                '-c', str(unit), '-o', str(obj)], check=True)
                objects.append(str(obj))
            binary = directory / f'probe-{sram}'
            subprocess.run([compiler, '-fsanitize=address,undefined', *objects,
                            str(library), '-lm', '-o', str(binary)], check=True)
            run = subprocess.run([str(binary), str(rom)], check=True, capture_output=True, text=True)
            assert run.stdout.count('[CONTRACT] common Ruby extensions') == 3, run.stdout
            print(f'cache={entries}: ' + run.stdout.strip().splitlines()[-1])

invalid = subprocess.run(['ruby', str(root / 'tools/build/workflow.rb'), 'engine',
                          '--vm-state', 'sram', '--wasm-only'], capture_output=True, text=True)
assert invalid.returncode != 0 and '--vm-state sram requires an RP2350 firmware build' in invalid.stderr
print('VM STATE PASS: real allocator, common Ruby contracts, arrays/hash/bytes/GC, CLI validation')
