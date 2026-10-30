#!/usr/bin/env ruby
# frozen_string_literal: true

require 'optparse'
require 'rbconfig'
require 'fileutils'
require_relative 'dependencies'
require_relative 'vm_profile'

if (RUBY_VERSION.split('.').map(&:to_i) <=> [3, 1, 0]) == -1
  abort 'workflow: Ruby 3.1 or newer is required; select the project Ruby before running workflow.rb'
end

ROOT = File.expand_path('../..', __dir__)
DEFAULT_PICORUBY_ROOT = PicoRubyGaming::Dependencies.directory('picoruby')
DEFAULT_MRBC = File.join(ROOT, 'build', 'mruby', 'mrbc', 'default', 'bin', 'mrbc')
DEFAULT_PICO_BOARD = 'pimoroni_pico_plus2_rp2350'
SUPPORTED_PICO_BOARDS = [
  DEFAULT_PICO_BOARD,
  'weact_studio_rp2350b_core'
].freeze
DEFAULT_ARM_TOOLCHAIN = ENV['ARM_TOOLCHAIN']

def fail_with(message)
  warn "workflow: #{message}"
  exit 1
end

def run_command(environment, *command, chdir: ROOT)
  puts "\n==> #{command.join(' ')}"
  ok = system(environment, *command, chdir: chdir)
  fail_with("command failed: #{command.first}") unless ok
end

def run_rake(environment, picoruby_root, *tasks)
  run_command(environment, RbConfig.ruby, '-S', 'rake', *tasks, chdir: picoruby_root)
end

def require_directory(path, description)
  fail_with("#{description} not found: #{path}") unless Dir.exist?(path)
end

def require_file(path, description)
  fail_with("#{description} not found: #{path}") unless File.file?(path)
end

def emscripten_environment(base_path = ENV['PATH'])
  root = ENV['EMSDK']
  unless root && File.file?(File.join(root, 'emsdk_env.sh'))
    fail_with('Emscripten not found; source /path/to/emsdk/emsdk_env.sh before building')
  end

  node = ENV['EMSDK_NODE']
  node ||= Dir[File.join(root, 'node', '*', 'bin', 'node')].sort.last
  python = ENV['EMSDK_PYTHON']
  python ||= Dir[File.join(root, 'python', '*', 'bin', 'python3')].sort.last
  path_entries = [root, File.join(root, 'upstream', 'emscripten'), base_path]
  environment = {
    'EMSDK' => root,
    'PATH' => path_entries.compact.join(File::PATH_SEPARATOR)
  }
  environment['EMSDK_NODE'] = node if node && File.file?(node)
  environment['EMSDK_PYTHON'] = python if python && File.file?(python)
  # Avoid a broken Homebrew Node.js taking precedence over the emsdk copy.
  environment['EM_NODE_JS'] = node if node && File.file?(node)
  environment
end

def engine_workflow(argv)
  options = {
    picoruby_root: ENV.fetch('PICORUBY_ROOT', DEFAULT_PICORUBY_ROOT),
    board: DEFAULT_PICO_BOARD,
    build_root: File.join(ROOT, 'build'),
    firmware_dir: nil,
    arm_toolchain: DEFAULT_ARM_TOOLCHAIN,
    build_type: 'Release',
    vm_opt: 'size',
    audio_output: 'pwm',
    audio_max_gain: 100,
    i2s_bclk: 26,
    i2s_data: nil,
    volume_knob: false,
    battery_warning: false,
    firmware: true,
    wasm: true,
    emulator: true,
    profiling: false,
    tft_clock_mhz: nil,
    tft_stripe_rows: nil,
    dirty_merge_extra_pixels: nil
  }

  parser = OptionParser.new do |opts|
    opts.banner = 'Usage: workflow.rb engine [options]'
    opts.on('--picoruby-root PATH', 'PicoRuby checkout') { |value| options[:picoruby_root] = value }
    opts.on('--board BOARD', SUPPORTED_PICO_BOARDS,
            "RP2350 board (#{SUPPORTED_PICO_BOARDS.join(', ')})") do |value|
      options[:board] = value
    end
    opts.on('--build-root PATH', 'generated build root') { |value| options[:build_root] = value }
    opts.on('--firmware-dir PATH', 'CMake firmware build directory') { |value| options[:firmware_dir] = value }
    opts.on('--arm-toolchain PATH', 'arm-none-eabi/bin directory') { |value| options[:arm_toolchain] = value }
    opts.on('--debug', 'build a Debug firmware') { options[:build_type] = 'Debug' }
    opts.on('--vm-opt PROFILE', PicoRubyGaming::VMProfile::NAMES,
            'RP2350 mruby core: size (-Os, default) or speed (-O2)') { |value| options[:vm_opt] = value }
    opts.on('--audio-output OUTPUT', %w[pwm i2s], 'audio transport (default: pwm)') { |value| options[:audio_output] = value }
    opts.on('--audio-max-gain PERCENT', Integer, 'output gain ceiling: 0..100 (default: 100)') { |value| options[:audio_max_gain] = value }
    opts.on('--i2s-bclk GPIO', Integer, 'I2S BCLK pin, WS is GPIO+1 (default: 26)') { |value| options[:i2s_bclk] = value }
    opts.on('--battery-warning', 'WeAct GP40 battery monitor and GP24 status LED') { options[:battery_warning] = true }
    opts.on('--volume-knob', 'ADC2/GP42 master volume (Pico Plus header GP28)') { options[:volume_knob] = true }
    opts.on('--i2s-data GPIO', Integer, 'I2S data pin (default: 28; with volume knob: 17)') { |value| options[:i2s_data] = value }
    opts.on('--profiling', 'build firmware with USB serial frame profiling') { options[:profiling] = true }
    opts.on('--tft-clock MHZ', Integer, 'set TFT SPI clock in MHz') { |value| options[:tft_clock_mhz] = value }
    opts.on('--tft-stripe-rows ROWS', Integer, 'set the SRAM TFT transfer stripe height') do |value|
      options[:tft_stripe_rows] = value
    end
    opts.on('--dirty-merge-extra PIXELS', Integer,
            'set the extra pixel budget for dirty-rectangle merging') do |value|
      options[:dirty_merge_extra_pixels] = value
    end
    opts.on('--firmware-only', 'skip WASM and emulator runtime') do
      options[:wasm] = false
      options[:emulator] = false
    end
    opts.on('--wasm-only', 'skip RP2350 firmware') { options[:firmware] = false }
    opts.on('--no-emulator-runtime', 'do not copy WASM files into emulator/picoruby') do
      options[:emulator] = false
    end
    opts.on('-h', '--help', 'show this help') do
      puts opts
      exit
    end
  end
  parser.parse!(argv)
  fail_with('--audio-max-gain must be an integer from 0 to 100') unless (0..100).cover?(options[:audio_max_gain])
  fail_with('--battery-warning requires WeAct RP2350B Core') if options[:battery_warning] && options[:board] != 'weact_studio_rp2350b_core'
  default_i2s_data = options[:volume_knob] ? 17 : 28
  options[:i2s_data] ||= default_i2s_data
  i2s_pin_variant = if options[:audio_output] == 'i2s' &&
                      (options[:i2s_bclk] != 26 || options[:i2s_data] != default_i2s_data)
                      "bclk#{options[:i2s_bclk]}-data#{options[:i2s_data]}"
                    end
  if options[:audio_output] == 'i2s' &&
     (![26, 27].include?(options[:i2s_bclk]) ||
      ![17, 26, 27, 28].include?(options[:i2s_data]) ||
      [options[:i2s_bclk], options[:i2s_bclk] + 1].include?(options[:i2s_data]))
    fail_with('I2S uses free GPIOs 17 or 26..28; BCLK/WS must be adjacent and DATA distinct')
  end
  dial_data_pins = options[:board] == 'weact_studio_rp2350b_core' ? [17, 28] : [17]
  if options[:volume_knob] && options[:audio_output] == 'i2s' &&
     (options[:i2s_bclk] != 26 || !dial_data_pins.include?(options[:i2s_data]))
    fail_with('I2S with volume knob requires BCLK=26, WS=27, DATA=17 (WeAct also allows DATA=28)')
  end
  begin
    PicoRubyGaming::VMProfile.validate(options[:vm_opt],
      debug: options[:build_type] == 'Debug' || !!(ENV['PICORUBY_DEBUG'] || ENV['PICORB_DEBUG']))
  rescue ArgumentError => error
    fail_with(error.message)
  end
  fail_with('--vm-opt speed requires an RP2350 firmware build') if options[:vm_opt] == 'speed' && !options[:firmware]

  if options[:tft_clock_mhz] && !options[:tft_clock_mhz].between?(1, 100)
    fail_with('TFT clock must be between 1 and 100 MHz')
  end
  if options[:tft_stripe_rows] && options[:tft_stripe_rows] < 1
    fail_with('TFT stripe rows must be positive')
  end
  if options[:dirty_merge_extra_pixels] && options[:dirty_merge_extra_pixels] < 0
    fail_with('dirty merge extra pixels must be non-negative')
  end
  picoruby_root = File.expand_path(options[:picoruby_root])
  build_root = File.expand_path(options[:build_root])
  # MRUBY_BUILD_DIR is the parent directory; mruby appends the config name.
  mruby_build_root = PicoRubyGaming::VMProfile.build_root(build_root, options[:vm_opt])
  mruby_build_dir = File.join(mruby_build_root, 'pico-gaming')
  if options[:firmware_dir]
    firmware_dir = File.expand_path(options[:firmware_dir])
  else
    firmware_suffix = options[:board] == DEFAULT_PICO_BOARD ? '' : "-#{options[:board].tr('_', '-')}"
    firmware_suffix += '-vm-speed' if options[:vm_opt] == 'speed'
    firmware_suffix += '-audio-i2s' if options[:audio_output] == 'i2s'
    firmware_suffix += '-battery-warning' if options[:battery_warning]
    firmware_suffix += '-volume-knob' if options[:volume_knob]
    firmware_suffix += "-audio-gain#{options[:audio_max_gain]}" unless options[:audio_max_gain] == 100
    firmware_suffix += "-#{i2s_pin_variant}" if i2s_pin_variant
    firmware_dir = File.join(build_root, "firmware-release#{firmware_suffix}")
  end
  mruby_config = File.join(ROOT, 'mruby_config.rb')

  require_directory(picoruby_root, 'PicoRuby checkout')
  require_file(File.join(picoruby_root, 'include', 'picoruby.h'), 'PicoRuby header')
  require_file(mruby_config, 'cross-build configuration')

  common_environment = {
    'PICORUBY_ROOT' => picoruby_root,
    'MRUBY_BUILD_DIR' => mruby_build_root,
    'MRUBY_CONFIG' => mruby_config,
    'PICORB_DEBUG' => ENV['PICORUBY_DEBUG'],
    'GAME_OPTIMIZE_FOR_SPEED' => '1',
    'GAME_VM_OPT' => options[:vm_opt],
    'PATH' => [File.dirname(RbConfig.ruby), ENV['PATH']].compact.join(File::PATH_SEPARATOR)
  }

  if options[:firmware]
    arm_toolchain = options[:arm_toolchain]
    fail_with('Arm GNU Toolchain not found; pass --arm-toolchain PATH') unless arm_toolchain && Dir.exist?(arm_toolchain)
    arm_gcc = File.join(arm_toolchain, 'arm-none-eabi-gcc')
    arm_ld = File.join(arm_toolchain, 'arm-none-eabi-ld')
    arm_ar = File.join(arm_toolchain, 'arm-none-eabi-ar')
    require_file(arm_gcc, 'Arm C compiler')
    require_file(arm_ld, 'Arm linker')
    require_file(arm_ar, 'Arm archiver')

    common_environment = common_environment.merge(
      'ARM_TOOLCHAIN' => arm_toolchain,
      'ARM_NONE_EABI_GCC' => arm_gcc,
      'ARM_NONE_EABI_LD' => arm_ld,
      'ARM_NONE_EABI_AR' => arm_ar
    )

    vm_settings = {
      'vm_opt' => options[:vm_opt],
      'vm_scope' => 'picoruby-mruby',
      'picoruby_root' => picoruby_root,
      'picoruby_revision' => PicoRubyGaming::Dependencies.revision(picoruby_root),
      'arm_gcc' => arm_gcc,
      'arm_version' => PicoRubyGaming::Dependencies.capture(arm_gcc, '--version'),
      'game_optimize_for_speed' => true,
      'debug' => ENV['PICORUBY_DEBUG'] || ENV['PICORB_DEBUG'],
      'environment_flags' => %w[CFLAGS CXXFLAGS CPPFLAGS LDFLAGS HOST_CC].to_h { |key| [key, ENV[key]] },
      'configuration' => [mruby_config, File.join(ROOT, 'tools/build/vm_profile.rb'),
                          File.join(ROOT, 'mrbgems/picoruby-game/mrbgem.rake')].to_h do |path|
        [path, Digest::SHA256.file(path).hexdigest]
      end
    }
    PicoRubyGaming::VMProfile.prepare(mruby_build_dir, vm_settings)
    run_rake(common_environment, picoruby_root, 'build')
    PicoRubyGaming::VMProfile.record(mruby_build_dir, vm_settings)

    cmake_args = [
      'cmake', '-S', ROOT, '-B', firmware_dir,
      "-DCMAKE_BUILD_TYPE=#{options[:build_type]}",
      "-DPICO_BOARD=#{options[:board]}",
      "-DPICORUBY_ROOT=#{picoruby_root}",
      "-DPICORUBY_BUILD_DIR=#{mruby_build_dir}",
      "-DPICO_TOOLCHAIN_PATH=#{arm_toolchain}",
      '-DPICO_NO_PICOTOOL=1'
    ]
    cmake_args.concat([
      "-DGAME_ENABLE_PROFILING=#{options[:profiling] ? 'ON' : 'OFF'}",
      "-DGAME_BATTERY_WARNING=#{options[:battery_warning] ? 'ON' : 'OFF'}",
      "-DGAME_AUDIO_VOLUME_KNOB=#{options[:volume_knob] ? 'ON' : 'OFF'}",
      "-DGAME_AUDIO_MAX_GAIN=#{options[:audio_max_gain]}",
      "-DGAME_AUDIO_I2S=#{options[:audio_output] == 'i2s' ? 'ON' : 'OFF'}",
      "-DGAME_AUDIO_I2S_BCLK_PIN=#{options[:i2s_bclk]}",
      "-DGAME_AUDIO_I2S_DATA_PIN=#{options[:i2s_data]}",
      "-DGAME_TFT_SPI_HZ=#{(options[:tft_clock_mhz] || 75) * 1000000}",
      "-DGAME_TFT_STRIPE_ROWS=#{options[:tft_stripe_rows] || 32}",
      "-DGAME_DIRTY_RECT_MERGE_EXTRA_PIXELS=#{options[:dirty_merge_extra_pixels] || 256}",
      '-DGAME_SPRITE_CACHE_BYTES=65536'
    ])
    run_command(common_environment, *cmake_args)
    run_command(common_environment, 'cmake', '--build', firmware_dir)
  end

  if options[:wasm]
    wasm_config = File.join(ROOT, 'emulator', 'picoruby-wasm-gaming.rb')
    require_file(wasm_config, 'PicoRuby WASM configuration')

    # Use a project-owned config so the browser build is explicit and stable.
    # The browser adapter implements Game over the standard upstream WASM gems.
    wasm_environment = emscripten_environment(common_environment['PATH']).merge(
      'PICORUBY_ROOT' => picoruby_root,
      'CONFIG' => wasm_config
    )
    run_rake(wasm_environment, picoruby_root)
    if options[:emulator]
      run_command({}, RbConfig.ruby, File.join(ROOT, 'emulator', 'setup_runtime.rb'), picoruby_root,
                  chdir: File.join(ROOT, 'emulator'))
    end
  end

  puts "\nEngine workflow complete."
  puts "Pico board: #{options[:board]}" if options[:firmware]
  puts "VM core: #{options[:vm_opt]} (#{options[:vm_opt] == 'speed' ? '-O2' : '-Os'})" if options[:firmware]
  puts "Firmware: #{File.join(firmware_dir, 'pico_ruby_gaming.uf2')}" if options[:firmware]
  puts "WASM runtime: #{File.join(ROOT, 'emulator', 'picoruby')}" if options[:wasm] && options[:emulator]
end

def game_workflow(argv)
  options = {
    compiler: ENV.fetch('PICORBC', DEFAULT_MRBC),
    output: nil,
    source: nil,
    prepare_assets: false,
    convert_assets: false,
    asset_scale: 1.0,
    asset_key_color: 0xf81f,
    font_sizes: '8,10,12,14,16,18,20,24,28,32,40,48,60'
  }

  parser = OptionParser.new do |opts|
    opts.banner = 'Usage: workflow.rb game GAME_DIR [options]'
    opts.on('--mrbc PATH', 'matching PicoRuby compiler executable') { |value| options[:compiler] = value }
    opts.on('--source PATH', 'Ruby entrypoint; defaults to GAME_DIR/main.rb') { |value| options[:source] = value }
    opts.on('--output PATH', 'cartridge output directory') { |value| options[:output] = value }
    opts.on('--prepare-assets', 'run GAME_DIR/prepare_assets.rb before compilation') { options[:prepare_assets] = true }
    opts.on('--convert-assets', 'convert PNG and font assets to runtime formats') { options[:convert_assets] = true }
    opts.on('--asset-scale FACTOR', Float, 'nearest-neighbor scale for PNG assets') do |value|
      options[:asset_scale] = value
    end
    opts.on('--asset-key-color COLOR', 'RGB565 transparent key for PNG assets') do |value|
      options[:asset_key_color] = Integer(value, 0)
    end
    opts.on('--font-sizes SIZES', 'comma-separated bitmap font sizes') do |value|
      options[:font_sizes] = value
    end
    opts.on('-h', '--help', 'show this help') do
      puts opts
      exit
    end
  end
  parser.parse!(argv)

  game_dir_arg = argv.shift
  fail_with('GAME_DIR is required') unless game_dir_arg
  game_dir = File.expand_path(game_dir_arg)
  source = File.expand_path(options[:source] || File.join(game_dir, 'main.rb'))
  output = File.expand_path(options[:output] || File.join(ROOT, 'build', 'games', File.basename(game_dir)))
  compiler = File.expand_path(options[:compiler])

  require_directory(game_dir, 'game directory')
  require_file(compiler, 'PicoRuby host compiler; run workflow.rb engine first')
  require_file(source, 'game entrypoint')

  if options[:prepare_assets]
    prepare_script = File.join(game_dir, 'prepare_assets.rb')
    require_file(prepare_script, 'asset preparation script')
    run_command({}, RbConfig.ruby, prepare_script, chdir: game_dir)
  end

  build_command = [RbConfig.ruby, File.join(ROOT, 'tools', 'build', 'build_game.rb'),
                   '--source', source, '--output', output, '--mrbc', compiler]
  if options[:convert_assets]
    build_command.concat([
      '--convert-assets',
      '--asset-scale', options[:asset_scale].to_s,
      '--asset-key-color', format('0x%04x', options[:asset_key_color]),
      '--font-sizes', options[:font_sizes]
    ])
  end
  run_command({}, *build_command)

  rom = File.join(output, 'game.mrb')
  require_file(rom, 'generated game.mrb')
  puts "\nGame workflow complete."
  puts "Copy this directory to the SD card: #{output}"
  puts "Browser emulator uses the source and assets directly; reload it after changes."
end

def doctor_workflow
  PicoRubyGaming::Dependencies.report
  puts "Ruby: #{RbConfig.ruby} (#{RUBY_VERSION})"
  puts "Project: #{ROOT}"
  puts "PicoRuby: #{ENV.fetch('PICORUBY_ROOT', DEFAULT_PICORUBY_ROOT)}"
  puts "Arm toolchain: #{DEFAULT_ARM_TOOLCHAIN || '(not found)'}"
  puts "Emscripten: #{ENV['EMSDK'] || '(not configured; source emsdk_env.sh)'}"
  puts "Run `ruby tools/build/workflow.rb engine` to perform a full engine rebuild."
end

def studio_workflow(argv)
  options = {
    picoruby_root: ENV.fetch('PICORUBY_ROOT', DEFAULT_PICORUBY_ROOT),
    output: File.join(ROOT, 'studio', 'runtime')
  }

  parser = OptionParser.new do |opts|
    opts.banner = 'Usage: workflow.rb studio [options]'
    opts.on('--picoruby-root PATH', 'PicoRuby checkout') { |value| options[:picoruby_root] = value }
    opts.on('--output PATH', 'generated browser runtime directory') { |value| options[:output] = value }
    opts.on('-h', '--help', 'show this help') do
      puts opts
      exit
    end
  end
  parser.parse!(argv)

  picoruby_root = File.expand_path(options[:picoruby_root])
  output = File.expand_path(options[:output])
  compiler_config = File.join(ROOT, 'tools', 'build', 'mrbc-wasm-browser.rb')
  require_directory(picoruby_root, 'PicoRuby checkout')
  require_file(File.join(picoruby_root, 'include', 'picoruby.h'), 'PicoRuby header')
  require_file(compiler_config, 'browser compiler configuration')

  # The studio reuses the normal emulator runtime. Build it here so one
  # command prepares every browser artifact needed by the static app.
  engine_workflow(['--picoruby-root', picoruby_root, '--wasm-only'])

  build_root = File.join(ROOT, 'build', 'studio-mruby')
  environment = emscripten_environment(
    [File.dirname(RbConfig.ruby), ENV['PATH']].compact.join(File::PATH_SEPARATOR)
  ).merge(
    'PICORUBY_ROOT' => picoruby_root,
    'MRUBY_BUILD_DIR' => build_root,
    'MRUBY_CONFIG' => compiler_config
  )
  run_rake(environment, picoruby_root)

  compiler_build_dir = File.join(build_root, 'mrbc-wasm-browser')
  compiler_js = File.join(compiler_build_dir, 'bin', 'mrbc.js')
  compiler_wasm = File.join(compiler_build_dir, 'bin', 'mrbc.wasm')
  require_file(compiler_js, 'browser mrbc JavaScript module')
  require_file(compiler_wasm, 'browser mrbc WebAssembly module')

  FileUtils.mkdir_p(output)
  FileUtils.cp(compiler_js, File.join(output, 'mrbc.js'))
  FileUtils.cp(compiler_wasm, File.join(output, 'mrbc.wasm'))
  FileUtils.cp(File.join(ROOT, 'emulator', 'picoruby', 'version.json'), File.join(output, 'version.json'))
  FileUtils.rm_f(%w[picorbc.js picorbc.wasm].map { |name| File.join(output, name) })

  puts "\nStudio workflow complete."
  puts "Compiler runtime: #{output}"
  puts "Open: http://localhost:8080/studio/"
end

mode = ARGV.shift
case mode
when 'engine'
  engine_workflow(ARGV)
when 'game'
  game_workflow(ARGV)
when 'studio'
  studio_workflow(ARGV)
when 'checks'
  run_command({}, RbConfig.ruby, File.join(ROOT, 'tools/build/build_checks.rb'), *ARGV)
when 'setup'
  PicoRubyGaming::Dependencies.setup
when 'doctor'
  doctor_workflow
else
  puts <<~USAGE
    Usage:
      ruby tools/build/workflow.rb doctor
      ruby tools/build/workflow.rb setup
      ruby tools/build/workflow.rb engine [options]
      ruby tools/build/workflow.rb game GAME_DIR [options]
      ruby tools/build/workflow.rb studio [options]
      ruby tools/build/workflow.rb checks [--compare-3d | --audio-only] [--output DIR] [--mrbc PATH]

    Typical commands:
      ruby tools/build/workflow.rb engine
      ruby tools/build/workflow.rb game local-games/my-game
  USAGE
  exit(mode ? 1 : 0)
end
