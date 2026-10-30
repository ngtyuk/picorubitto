# frozen_string_literal: true

# Cross-build configuration for the Pico Plus 2 game console.
# This file is loaded by the PicoRuby Rakefile with MRUBY_ROOT pointing at
# an external PicoRuby checkout. All generated files go to MRUBY_BUILD_DIR.

picoruby_root = ENV.fetch('PICORUBY_ROOT')
game_root = File.expand_path(__dir__)
require File.join(game_root, 'tools', 'runtime_profile.rb')
require File.join(game_root, 'tools', 'build', 'vm_profile.rb')
vm_profile = PicoRubyGaming::VMProfile.validate(ENV.fetch('GAME_VM_OPT', 'size'),
                                              debug: !!(ENV['PICORUBY_DEBUG'] || ENV['PICORB_DEBUG']))

MRuby::CrossBuild.new('pico-gaming') do |conf|
  conf.toolchain('gcc')

  conf.cc.command = ENV.fetch('ARM_NONE_EABI_GCC', 'arm-none-eabi-gcc')
  conf.linker.command = ENV.fetch('ARM_NONE_EABI_LD', 'arm-none-eabi-ld')
  conf.archiver.command = ENV.fetch('ARM_NONE_EABI_AR', 'arm-none-eabi-ar')
  conf.linker.flags << '-static'
  conf.cc.host_command = ENV.fetch('HOST_CC', 'cc')

  conf.cc.flags.flatten!
  conf.cc.flags.concat(%w[
    -mcpu=cortex-m33
    -mthumb
    -fno-strict-aliasing
    -fno-unroll-loops
    -mslow-flash-data
    -fshort-enums
    -Wall
    -Wno-format
    -Wno-unused-function
    -ffunction-sections
    -fdata-sections
  ])

  conf.cc.defines.concat(%w[
    PICORB_PLATFORM_RP2
    PICORB_VM_MRUBY
    MRB_USE_TASK_SCHEDULER
    MRB_TICK_UNIT=1
    MRB_TIMESLICE_TICK_COUNT=10
    MRB_UTF8_STRING
    MRB_INT64
    MRB_NO_BOXING
    MRB_32BIT
    PICORB_ALLOC_ESTALLOC
    PICORB_ALLOC_ALIGN=8
    MRB_USE_CUSTOM_RO_DATA_P
    MRB_LINK_TIME_RO_DATA_P
    USE_FAT_SD_DISK
    NO_CLOCK_GETTIME
    MAX_SYMBOLS_COUNT=2000
  ])
  conf.cc.defines << (ENV['PICORUBY_DEBUG'] ? 'PICORUBY_DEBUG=1' : 'NDEBUG=1')
  conf.cc.flags << (ENV['PICORUBY_DEBUG'] ? '-O0' : '-Os')
  conf.cc.flags << '-g3' if ENV['PICORUBY_DEBUG']
  conf.cc.flags << '-fomit-frame-pointer' unless ENV['PICORUBY_DEBUG']

  # Use the upstream PicoRuby profile, including generated version metadata.
  # mruby resolves and builds the matching host mrbc.
  conf.picoruby
  PicoRubyGaming::VMProfile.configure(conf, vm_profile)
  conf.cc.include_paths << File.join(picoruby_root, 'include')
  conf.cc.include_paths << File.join(picoruby_root, 'mrbgems', 'mruby-compiler', 'include')
  conf.cc.include_paths << File.join(picoruby_root, 'mrbgems', 'mruby-compiler', 'lib', 'prism', 'include')
  conf.cc.include_paths << File.join(picoruby_root, 'mrbgems', 'picoruby-mruby', 'include')
  conf.cc.include_paths << File.join(picoruby_root, 'mrbgems', 'picoruby-mruby', 'lib', 'mruby', 'include')
  conf.cc.include_paths << File.join(picoruby_root, 'mrbgems', 'picoruby-machine', 'include')

  conf.gembox 'minimum'
  # The game runtime owns storage and cartridge paths in C. Keep the
  # require surface needed by precompiled games without interactive devices.
  conf.gem core: 'picoruby-require'
  conf.gem core: 'picoruby-rng'
  PicoRubyGaming::RuntimeProfile.add_common_gems(conf, game_root)
  conf.gem gemdir: File.join(game_root, 'mrbgems', 'picoruby-game')

  # Keep the base runtime small, but include the standard collection and
  # math/numeric helpers needed by cartridge game logic. These are the mruby
  # extensions used by PicoRuby's normal stdlib gembox.
  conf.gem gemdir: File.join(picoruby_root, 'mrbgems', 'picoruby-mruby',
                             'lib', 'mruby', 'mrbgems', 'mruby-math')
  conf.gem gemdir: File.join(picoruby_root, 'mrbgems', 'picoruby-mruby',
                             'lib', 'mruby', 'mrbgems', 'mruby-array-ext')
  conf.gem gemdir: File.join(picoruby_root, 'mrbgems', 'picoruby-mruby',
                             'lib', 'mruby', 'mrbgems', 'mruby-numeric-ext')
end
