# frozen_string_literal: true

# PicoRuby WASM configuration for the RP2350 game emulator.
#
# Both targets use the PicoRuby 4.0.5 mruby/Prism ABI. Browser-only gems and
# the Game adapter are added to the standard upstream WASM profile.
project_root = File.expand_path('..', __dir__)
require File.join(project_root, 'tools', 'runtime_profile.rb')

MRuby::CrossBuild.new('picoruby-wasm') do |conf|
  conf.generate_package_json_from_template(
    "#{MRUBY_ROOT}/mrbgems/picoruby-wasm/npm/picoruby/package.json.template",
    "#{MRUBY_ROOT}/mrbgems/picoruby-wasm/npm/picoruby/package.json"
  )

  toolchain :clang

  conf.cc.defines << 'MRB_TICK_UNIT=4'
  conf.cc.defines << 'MRB_TIMESLICE_TICK_COUNT=1'
  conf.cc.defines << 'MRB_INT64'

  conf.cc.command = 'emcc'
  conf.linker.command = 'emcc'
  conf.archiver.command = 'emar'

  conf.ports :posix
  conf.cc.defines.concat(%w[PICORB_PLATFORM_POSIX PICORB_PLATFORM_WASM MRB_32BIT MRB_NO_BOXING MRB_UTF8_STRING])
  conf.picoruby(alloc_estalloc: false)

  conf.gembox 'mruby-posix'
  conf.gembox 'stdlib'
  PicoRubyGaming::RuntimeProfile.add_common_gems(conf, project_root)

  if ENV['PICORUBY_DEBUG']
    conf.cc.defines << 'MRB_USE_DEBUG_HOOK'
    conf.gem gemdir: "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-binding"
  end
  conf.gem gemdir: "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-math"
  conf.gem core: 'picoruby-wasm'
  conf.gem core: 'picoruby-indexeddb'
  conf.gem core: 'picoruby-funicular'
  conf.gem core: 'picoruby-markdown'
  conf.gem core: 'picoruby-drb'

  # Keep an opt-in diagnostic build available for browser/runtime failures.
  # The normal emulator build remains optimized and assertion-free.
  if ENV['PICORUBY_WASM_ASSERTIONS']
    conf.linker.flags << '-sASSERTIONS=2'
    conf.linker.flags << '-sSAFE_HEAP=1'
  end
end
