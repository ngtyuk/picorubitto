require 'minitest/autorun'
require 'tmpdir'
require 'open3'
require 'fileutils'

class ClockProfileTest < Minitest::Test
  ROOT = File.expand_path('../..', __dir__)
  PIZERO = 'waveshare_rp2350_pizero'

  def configure(profile, board = 'weact_studio_rp2350b_core', diagnostics: false, dvi: false)
    Dir.mktmpdir('gaming-clock-') do |dir|
      FileUtils.mkdir_p(File.join(dir, 'sdk-child'))
      File.write(File.join(dir, 'sdk-child/CMakeLists.txt'), <<~CMAKE)
        get_directory_property(definitions COMPILE_DEFINITIONS)
        file(WRITE "${CMAKE_BINARY_DIR}/inherited.txt" "${definitions}")
      CMAKE
      File.write(File.join(dir, 'CMakeLists.txt'), <<~CMAKE)
        cmake_minimum_required(VERSION 3.22)
        project(clock_test NONE)
        include("#{ROOT}/cmake/clock_profile.cmake")
        add_subdirectory(sdk-child)
      CMAKE
      output, status = Open3.capture2e('cmake', '-S', dir, '-B', File.join(dir, 'build'),
        "-DGAME_CLOCK_PROFILE=#{profile}", "-DPICO_BOARD=#{board}",
        "-DGAME_CLOCK_DIAGNOSTICS=#{diagnostics ? 'ON' : 'OFF'}", "-DGAME_DVI=#{dvi ? 'ON' : 'OFF'}")
      definitions = status.success? ? File.read(File.join(dir, 'build/inherited.txt')).split(';') : []
      return [definitions, output, status]
    end
  end

  def test_profiles_reach_sdk_child_and_keep_external_memory_at_or_below_75mhz
    {150 => '1_10', 200 => '1_15', 250 => '1_20', 300 => '1_25'}.each do |mhz, voltage|
      definitions, output, status = configure("weact-#{mhz}")
      assert status.success?, output
      settings = definitions.to_h { |item| item.split('=', 2) }
      sys = Integer(settings.fetch('SYS_CLK_HZ'))
      vco = Integer(settings.fetch('PLL_SYS_VCO_FREQ_HZ'))
      assert_equal mhz * 1_000_000, sys
      assert_equal sys, vco / Integer(settings.fetch('PLL_SYS_POSTDIV1')) / Integer(settings.fetch('PLL_SYS_POSTDIV2'))
      assert_equal 0, vco % 12_000_000
      assert_includes 750_000_000..1_600_000_000, vco
      assert_operator sys / Integer(settings.fetch('PICO_FLASH_SPI_CLKDIV')), :<=, 75_000_000
      assert_equal settings['PICO_FLASH_SPI_CLKDIV'], settings['PICO_FLASH_SPI_RXDELAY']
      assert_equal '75000000', settings['PICO_DEFAULT_PSRAM_MAX_FREQ']
      assert_equal "VREG_VOLTAGE_#{voltage}", settings['SYS_CLK_VREG_VOLTAGE_MIN']
      assert_equal '1', settings['SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST']
      assert_equal '1', settings['GAME_CLOCK_CUSTOM']
      assert_equal '0', settings['GAME_CLOCK_DIAGNOSTICS']
    end
  end

  def test_stock_preserves_sdk_clock_and_memory_defaults
    definitions, output, status = configure('stock', 'pimoroni_pico_plus2_rp2350')
    assert status.success?, output
    assert_equal ['GAME_CLOCK_PROFILE_NAME="stock"', 'GAME_CLOCK_CUSTOM=0', 'GAME_CLOCK_DIAGNOSTICS=0'], definitions
  end

  def test_diagnostics_are_opt_in_and_do_not_change_clock_voltage_or_memory_timing
    normal, _, normal_status = configure('weact-300')
    diagnostics, output, diagnostics_status = configure('weact-300', diagnostics: true)
    assert normal_status.success?
    assert diagnostics_status.success?, output
    assert_includes diagnostics, 'GAME_CLOCK_DIAGNOSTICS=1'
    assert_equal normal.reject { |item| item.start_with?('GAME_CLOCK_DIAGNOSTICS=') },
                 diagnostics.reject { |item| item.start_with?('GAME_CLOCK_DIAGNOSTICS=') }
    _, output, status = configure('stock', diagnostics: true)
    refute status.success?
    assert_includes output, 'requires a WeAct clock profile'
  end

  def test_unsupported_board_and_unknown_profile_are_rejected
    _, output, status = configure('weact-300', 'pimoroni_pico_plus2_rp2350')
    refute status.success?
    assert_includes output, 'require WeAct'
    _, output, status = configure('weact-400')
    refute status.success?
    assert_includes output, 'Unknown GAME_CLOCK_PROFILE'
  end

  def test_pizero_diagnostics_use_the_stock_clock
    definitions, output, status = configure('stock', PIZERO, diagnostics: true)
    assert status.success?, output
    assert_equal ['GAME_CLOCK_PROFILE_NAME="stock"', 'GAME_CLOCK_CUSTOM=0', 'GAME_CLOCK_DIAGNOSTICS=1'], definitions
  end

  def test_pizero_dvi_sets_tmds_clock_and_keeps_external_memory_below_75mhz
    definitions, output, status = configure('stock', PIZERO, diagnostics: true, dvi: true)
    assert status.success?, output
    settings = definitions.to_h { |item| item.split('=', 2) }
    assert_equal '"pizero-dvi"', settings['GAME_CLOCK_PROFILE_NAME']
    assert_equal '252000000', settings['SYS_CLK_HZ']
    assert_equal 252_000_000, Integer(settings.fetch('PLL_SYS_VCO_FREQ_HZ')) /
                             Integer(settings.fetch('PLL_SYS_POSTDIV1')) /
                             Integer(settings.fetch('PLL_SYS_POSTDIV2'))
    assert_equal '4', settings['PICO_FLASH_SPI_CLKDIV']
    assert_equal '75000000', settings['PICO_DEFAULT_PSRAM_MAX_FREQ']
    assert_equal 'VREG_VOLTAGE_1_20', settings['SYS_CLK_VREG_VOLTAGE_MIN']
    assert_equal '1', settings['GAME_CLOCK_DIAGNOSTICS']
    _, output, status = configure('stock', dvi: true)
    refute status.success?
    assert_includes output, 'DVI requires Waveshare RP2350-PiZero'
  end

  def test_cli_rejects_profile_without_weact_firmware
    [%w[--clock-profile weact-300],
     %w[--board weact_studio_rp2350b_core --clock-profile weact-200 --wasm-only]].each do |args|
      output, status = Open3.capture2e('ruby', File.join(ROOT, 'tools/build/workflow.rb'), 'engine', *args)
      refute status.success?
      assert_includes output, '--clock-profile requires'
    end
  end

  def test_cli_rejects_diagnostics_without_clock_profile
    output, status = Open3.capture2e('ruby', File.join(ROOT, 'tools/build/workflow.rb'),
      'engine', '--board', 'weact_studio_rp2350b_core', '--clock-diagnostics')
    refute status.success?
    assert_includes output, '--clock-diagnostics requires'
  end

  def test_cli_rejects_pizero_options_that_conflict_with_onboard_wiring_or_require_firmware
    {
      %w[--volume-knob] => 'GP42 is wired to the microSD slot',
      %w[--audio-output i2s --i2s-data 28] => 'PiZero I2S requires BCLK=26, WS=27, DATA=17',
      %w[--audio-output i2s --i2s-bclk 27] => 'PiZero I2S requires BCLK=26, WS=27, DATA=17',
      %w[--clock-diagnostics --wasm-only] => 'requires an RP2350 firmware build',
      %w[--display-output dvi --wasm-only] => 'dvi requires a PiZero firmware build'
    }.each do |args, message|
      output, status = Open3.capture2e('ruby', File.join(ROOT, 'tools/build/workflow.rb'),
        'engine', '--board', PIZERO, *args)
      refute status.success?
      assert_includes output, message
    end
  end
end
