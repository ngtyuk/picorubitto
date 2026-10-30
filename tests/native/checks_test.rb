require 'minitest/autorun'
require 'tmpdir'
require 'open3'
require 'digest'
require 'json'

HARDWARE_SUITE_LIBRARY = true
module Game
  def self.init; end
  def self.tick(_fps)
    Input.advance
  end
  module Input
    A = 16
    B = 32
    START = 256
    def self.taps=(values)
      @taps = values
    end
    def self.advance
      @current = @taps.shift || 0
    end
    def self.state
      0 # A short tap may be released before the next frame sample.
    end
    def self.just_pressed?(bit)
      @current == bit
    end
  end
  module Display
    def self.text(*); end
    def self.present; end
  end
end
require_relative '../portable/hardware_suite'
# Match the minimum profile: print is core, puts/send require extra gems.
HardwareChecks.singleton_class.class_eval { undef_method :puts, :send }

class ChecksTest < Minitest::Test
  def test_every_case_dispatches_without_send
    called = []
    methods = HardwareChecks::TESTS.map { |_, name| name }
    replacements = methods.to_h { |name| [name, -> { called << name; true }] }
    with_controls(replacements) do
      capture_io { methods.each_index { |index| assert HardwareChecks.run(index) } }
    end
    assert_equal methods, called
  end

  def test_suite_reaches_the_menu_without_puts
    original_tick = Game.method(:tick)
    Game.define_singleton_method(:tick) { |_| throw :menu_ready }
    with_controls({}) do
      output, = capture_io { catch(:menu_ready) { HardwareChecks.start } }
      assert_includes output, '[CHECK] hardware suite ready'
    end
  ensure
    Game.define_singleton_method(:tick, original_tick)
  end

  def test_disabled_dial_is_recorded_as_skipped
    with_controls(audio_dial_check: -> { :skipped }) do
      output, = capture_io { assert HardwareChecks.run(15) }
      assert_includes output, '[CHECK] SKIP AUDIO / DIAL (dial disabled)'
      refute_includes output, '[CHECK] PASS AUDIO / DIAL'
    end
  end

  def test_menu_keeps_the_last_cases_visible
    assert_equal 0, HardwareChecks.menu_first_row(0)
    assert_equal 0, HardwareChecks.menu_first_row(9)
    assert_equal 1, HardwareChecks.menu_first_row(10)
    assert_equal 2, HardwareChecks.menu_first_row(11)
    assert_equal 3, HardwareChecks.menu_first_row(12)
    assert_equal 4, HardwareChecks.menu_first_row(13)
    assert_equal 5, HardwareChecks.menu_first_row(14)
    assert_equal 6, HardwareChecks.menu_first_row(15)
  end

  def test_run_all_stops_on_cancel_and_visits_all_cases_on_completion
    called = []
    with_controls(run: ->(index) { called << index; index != 2 }) { HardwareChecks.run_all }
    assert_equal [0, 1, 2], called
    called.clear
    with_controls(run: ->(index) { called << index; true }) { HardwareChecks.run_all }
    assert_equal (0...HardwareChecks::TESTS.length).to_a, called
  end

  def with_controls(replacements)
    replacements = { heading: ->(*) {}, footer: ->(*) {}, wait_choice: -> { true } }.merge(replacements)
    originals = replacements.to_h { |name, _| [name, HardwareChecks.method(name)] }
    replacements.each { |name, implementation| HardwareChecks.define_singleton_method(name, &implementation) }
    yield
  ensure
    originals.each { |name, implementation| HardwareChecks.define_singleton_method(name, implementation) }
  end

  def test_failed_case_reports_the_exception_and_allows_the_next_case
    with_controls(ruby_check: -> { raise ArgumentError, 'diagnostic failure' }, api_check: -> { true }) do
      output, = capture_io { assert HardwareChecks.run(0) }
      assert_includes output, '[CHECK] FAIL RUBY / VM: ArgumentError: diagnostic failure'
      output, = capture_io { assert HardwareChecks.run(1) }
      assert_includes output, '[CHECK] PASS COMMON API'
    end
  end

  def test_menu_choice_can_stop_a_run_after_a_failure
    with_controls(wait_choice: -> { false }, ruby_check: -> { raise 'stop here' }) do
      capture_io { refute HardwareChecks.run(0) }
    end
  end

  def test_input_check_records_short_taps_and_start_is_last
    Game::Input.taps = [1, 2, 4, 8, 16, 32, 64, 128, 512, 256]
    with_controls({}) do
      output, = capture_io { assert HardwareChecks.input_check }
      assert_includes output, 'seen=0x3ff expected=0x3ff'
    end
  end

  def test_builder_compiles_one_cartridge_and_copies_exact_assets
    root = File.expand_path('../..', __dir__)
    compiler = ENV.fetch('CHECKS_MRBC', File.join(root, 'build/mruby/mrbc/default/bin/mrbc'))
    skip 'Build the matching host mrbc first' unless File.file?(compiler)
    Dir.mktmpdir('gaming-checks-') do |output|
      log, status = Open3.capture2e('ruby', File.join(root, 'tools/build/build_checks.rb'),
                                   '--output', output, '--mrbc', compiler)
      assert status.success?, log
      binary = File.binread(File.join(output, 'game.mrb'))
      assert_equal 'RITE0400', binary[0, 8]
      assert_equal binary.bytesize, binary[8, 4].unpack1('N')
      source = File.read(File.join(output, 'main.rb'))
      assert_equal 1, source.scan('def run_game_api_contract').length
      assert_includes source, 'HardwareChecks.start'
      assert_includes source, 'HARDWARE_PERFORMANCE_PROFILE = "standard"'
      refute_includes source, "  class Flight\n"
      assert_includes source, 'class FlightModels'
      assert_includes source, 'module HardwareVMPerformance'
      assert_includes source, 'module HardwareAudio'
      assert_equal 13, Dir[File.join(output, 'assets/audio/*.wav')].length
      assert_equal 3, Dir[File.join(output, 'assets/audio-rate-*.wav')].length
      large = Dir[File.join(output, 'assets/large/*.raw')].sort
      assert_equal 32, large.length
      assert_equal 16 * 1024 * 1024, large.sum { |file| File.size(file) }
      assert_equal 32, large.map { |file| Digest::SHA256.file(file).hexdigest }.uniq.length
      expected = JSON.parse(source[/ASSET_GC_CHECKSUMS = (\[.*\])/, 1])
      32.times do |index|
        actual = File.binread(File.join(output, 'assets/large', "scene-#{index}.raw")).bytes.reduce(2166136261) { |hash, byte| ((hash ^ byte) * 16777619) & 0xffffffff }
        assert_equal expected[index], actual
      end
      %w[texture.raw font.pbf document-kun.raw beep.wav].each do |name|
        directory = %w[texture.raw font.pbf].include?(name) ? 'tests/portable/assets' : 'examples/games/2d/assets'
        assert_equal File.binread(File.join(root, directory, name)),
                     File.binread(File.join(output, 'assets', name))
      end
      font = File.binread(File.join(output, 'assets/font-check.pbf'))
      invalid = File.binread(File.join(output, 'assets/font-invalid.pbf'))
      assert_equal 'PBF1', font[0, 4]
      assert_equal 2, font[8, 2].unpack1('n')
      assert_equal font.bytesize, font[12, 4].unpack1('N')
      refute_equal font, invalid
      sheets = Dir[File.join(output, 'assets/perf/sprite-*.raw')]
      assert_equal 24, sheets.length
      assert_equal 24 * 4096, sheets.sum { |file| File.size(file) }
      assert_equal 24, sheets.map { |file| Digest::SHA256.file(file).hexdigest }.uniq.length
      assert_equal 153600, File.size(File.join(output, 'assets/perf/background.raw'))
      assert_includes source, 'module HardwareFlight'
      assert_includes source, Digest::SHA256.file(File.join(root, 'examples/games/3d/main.rb')).hexdigest
    end
  end

  def test_comparison_builder_includes_legacy_profile_and_valid_bytecode
    root = File.expand_path('../..', __dir__)
    compiler = ENV.fetch('CHECKS_MRBC', File.join(root, 'build/mruby/mrbc/default/bin/mrbc'))
    skip 'Build the matching host mrbc first' unless File.file?(compiler)
    Dir.mktmpdir('gaming-comparison-') do |output|
      log, status = Open3.capture2e('ruby', File.join(root, 'tools/build/build_checks.rb'),
                                   '--compare-3d', '--output', output, '--mrbc', compiler)
      assert status.success?, log
      source = File.read(File.join(output, 'main.rb'))
      assert_includes source, 'HARDWARE_PERFORMANCE_PROFILE = "comparison"'
      assert_includes source, "  class Flight\n"
      assert_equal 'RITE0400', File.binread(File.join(output, 'game.mrb'))[0, 8]
      assert_includes log, 'Performance profile: comparison'
    end
  end

  def test_audio_only_builder_uses_the_shared_pad_without_large_or_render_assets
    root = File.expand_path('../..', __dir__)
    compiler = ENV.fetch('CHECKS_MRBC', File.join(root, 'build/mruby/mrbc/default/bin/mrbc'))
    skip 'Build the matching host mrbc first' unless File.file?(compiler)
    Dir.mktmpdir('gaming-audio-') do |output|
      command = ['ruby', File.join(root, 'tools/build/build_checks.rb'),
                 '--audio-only', '--output', output, '--mrbc', compiler]
      log, status = Open3.capture2e(*command)
      assert status.success?, log
      binary = File.binread(File.join(output, 'game.mrb'))
      assert_equal 'RITE0400', binary[0, 8]
      assert_equal binary.bytesize, binary[8, 4].unpack1('N')
      source = File.read(File.join(output, 'main.rb'))
      assert_includes source, 'HardwareAudio.run(:pad)'
      assert_includes source, 'A: RESTART'
      refute_includes source, 'HardwareChecks.start'
      refute_includes source, 'module HardwareFlight'
      assert_equal 16, Dir[File.join(output, 'assets/**/*.wav')].length
      assert_empty Dir[File.join(output, 'assets/**/*.{raw,pbf}')]
      assert File.file?(File.join(output, 'assets/audio/bgm.wav'))
      %w[jump hit coin].each { |name| assert File.file?(File.join(output, "assets/audio/#{name}.wav")) }
      log, status = Open3.capture2e(*command, '--compare-3d')
      refute status.success?
      assert_includes log, 'mutually exclusive'
      assert_equal binary, File.binread(File.join(output, 'game.mrb'))
    end
  end
end
