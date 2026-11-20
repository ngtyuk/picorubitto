#!/usr/bin/env python3
"""Exercise experimental XYZUV validation through actual mruby C bindings."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binding-source', type=Path,
                    default=root / 'mrbgems/picoruby-game/src/mruby/game.c')
args = parser.parse_args()
picoruby = Path(os.environ.get('PICORUBY_ROOT', root / 'deps/picoruby'))
mruby = picoruby / 'mrbgems/picoruby-mruby/lib/mruby'
build = root / 'build/tests/minimum-profile-method-cache256'
library = build / 'minimum-profile/lib/libmruby.a'
if not library.exists():
    subprocess.run(['python3', str(root / 'tests/native/minimum_profile_regression.py')],
                   env=dict(os.environ, GAME_METHOD_CACHE_SIZE='256'), check=True)
binding = args.binding_source.read_text()


def function(name, result):
    start = binding.index('\n' + name + '(') + 1
    end = binding.index('\n}', start) + 2
    return '\n' + result + '\n' + binding[start:end] + '\n'


harness = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/array.h>
#include <mruby/irep.h>
typedef struct { int unused; } game_texture_t;
static game_texture_t texture;
static const struct mrb_data_type game_texture_type={"Game::Texture",NULL};
static bool game_3d_draw_textured_triangles(const game_texture_t *t,
  const float *values, size_t count, bool perspective) {
  assert(t==&texture && count<=256);
  for(size_t i=0;i<count*15;i++) assert(isfinite(values[i]));
  (void)perspective;
  return true;
}
'''
harness += function('game_3d_number', 'static bool')
harness += function('three_d_draw_textured_triangles', 'static mrb_value')
harness += r'''
int main(int argc, char **argv) {
  assert(argc==2);
  FILE *file=fopen(argv[1],"rb");assert(file);
  fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
  unsigned char *rom=malloc(size);assert(rom);
  assert(fread(rom,1,size,file)==(size_t)size);fclose(file);
  mrb_state *mrb=mrb_open();assert(mrb && !mrb->exc);
  struct RClass *game=mrb_define_module(mrb,"Game");
  struct RClass *klass=mrb_define_class_under(mrb,game,"Texture",mrb->object_class);
  MRB_SET_INSTANCE_TT(klass,MRB_TT_DATA);
  mrb_define_global_const(mrb,"TEXTURE",
    mrb_obj_value(mrb_data_object_alloc(mrb,klass,&texture,&game_texture_type)));
  struct RClass *three=mrb_define_module_under(mrb,game,"ThreeD");
  mrb_define_module_function(mrb,three,"draw_textured_triangles",
                            three_d_draw_textured_triangles,MRB_ARGS_ARG(2,1));
  mrb_load_irep_buf(mrb,rom,size);
  int failed=mrb->exc!=NULL;if(failed)mrb_print_error(mrb);
  mrb_close(mrb);free(rom);return failed;
}
'''
probe = r'''
def expect_error(type, texture, values)
  caught = false
  begin
    Game::ThreeD.draw_textured_triangles(texture, values)
  rescue type
    caught = true
  end
  raise "expected #{type}" unless caught
end
triangle = [-1, -1, 3, 0, 1, 1, -1, 3, 1, 1, 0, 1, 3, 0.5, 0]
[true, false].each do |perspective|
  raise 'return' unless Game::ThreeD.draw_textured_triangles(TEXTURE, triangle, perspective).nil?
end
raise 'empty return' unless Game::ThreeD.draw_textured_triangles(TEXTURE, []).nil?
expect_error(TypeError, Object.new, triangle)
expect_error(ArgumentError, TEXTURE, 'not an array')
[1, 14, 16, 15*257].each { |count| expect_error(ArgumentError, TEXTURE, Array.new(count, 0)) }
[0, 3].each do |index|
  [nil, true, '0'].each do |value|
    values=triangle.dup;values[index]=value;expect_error(TypeError, TEXTURE, values)
  end
  [Float::NAN, Float::INFINITY, -Float::INFINITY, 1.0e100].each do |value|
    values=triangle.dup;values[index]=value;expect_error(ArgumentError, TEXTURE, values)
  end
end
[32769, -32769].each do |value|
  values=triangle.dup;values[0]=value;expect_error(ArgumentError, TEXTURE, values)
end
[1.1, -0.1].each do |value|
  values=triangle.dup;values[3]=value;expect_error(ArgumentError, TEXTURE, values)
end
puts '3D TEXTURE BINDING PASS: real mruby arguments, types, limits, non-finite XYZ/UV'
'''
with tempfile.TemporaryDirectory(prefix='texture3d-binding-') as temporary:
    directory = Path(temporary)
    (directory / 'main.c').write_text(harness)
    (directory / 'probe.rb').write_text(probe)
    rom = directory / 'probe.mrb'
    subprocess.run([str(build / 'mrbc/default/bin/mrbc'), '-o', str(rom), str(directory / 'probe.rb')], check=True)
    binary = directory / 'probe'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-g', '-fsanitize=address,undefined',
                    '-DMRB_INT64', '-DMRB_NO_BOXING', '-DMRB_UTF8_STRING', '-DPICORB_VM_MRUBY',
                    '-DMRB_CONSTRAINED_BASELINE_PROFILE=1', '-DMRB_METHOD_CACHE_SIZE=256',
                    '-I'+str(mruby / 'include'), '-I'+str(build / 'minimum-profile/include'),
                    str(directory / 'main.c'), str(library), '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(rom)], check=True)
