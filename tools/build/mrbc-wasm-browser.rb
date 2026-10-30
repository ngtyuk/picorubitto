# frozen_string_literal: true

# Browser-targeted PicoRuby compiler configuration.
#
# The compiler uses Emscripten's in-memory filesystem. The studio runs the
# generated mrbc module in a Web Worker and supplies source files through
# Module.FS, so it does not need Node's host filesystem.
MRuby::CrossBuild.new('mrbc-wasm-browser') do |conf|
  toolchain :clang

  conf.cc.command = 'emcc'
  conf.linker.command = 'emcc'
  conf.archiver.command = 'emar'

  conf.linker.flags << '-sWASM=1'
  conf.linker.flags << '-sMODULARIZE=1'
  conf.linker.flags << '-sEXPORT_ES6=1'
  conf.linker.flags << '-sENVIRONMENT=web,worker'
  conf.linker.flags << '-sALLOW_MEMORY_GROWTH=1'
  conf.linker.flags << '-sFORCE_FILESYSTEM=1'
  conf.linker.flags << '-sEXPORTED_RUNTIME_METHODS=["callMain","FS"]'
  conf.linker.flags << '-sEXPORTED_FUNCTIONS=["_main"]'
  conf.linker.flags << '-sNO_EXIT_RUNTIME=1'

  conf.exts.executable = '.js'

  conf.ports :posix
  conf.set_build_info
  conf.cc.defines.concat(%w[PICORB_PLATFORM_POSIX PICORB_PLATFORM_WASM MRB_32BIT MRB_INT64 MRB_NO_BOXING])
  conf.gem core: 'mruby-compiler'
  conf.gem core: 'mruby-bin-mrbc'

  # The mruby build system uses this path for the compiler binary. Emscripten
  # emits the matching mrbc.js and mrbc.wasm pair beside it.
  conf.instance_variable_set :@mrbcfile, 'bin/mrbc.wasm'
  conf.disable_libmruby
end
