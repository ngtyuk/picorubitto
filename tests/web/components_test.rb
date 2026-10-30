# Run against the real Funicular classes in the same PicoRuby checkout as the WASM build.
require 'minitest/autorun'
require 'json'

root = ENV.fetch('PICORUBY_ROOT', File.expand_path('../../deps/picoruby', __dir__))
framework = File.join(root, 'mrbgems/picoruby-funicular/mrblib')
abort 'Set PICORUBY_ROOT to the checkout used to build the browser runtime' unless File.directory?(framework)
ENV['FUNICULAR_ENV'] = 'production'
$LOADED_FEATURES << 'js.rb'
module JS
  class Object; end
end
$LOAD_PATH.unshift(framework)
%w[0_tags version environment_inquirer funicular debug styles vdom differ runtime view_context component].each { |name| require name }
require_relative '../../web/components'
require_relative '../../studio/studio'
require_relative '../../emulator/shell'

class BrowserResult
  def initialize(value, &on_await)
    @value = value
    @on_await = on_await
  end

  def await
    @on_await&.call
    JSON.generate(@value)
  end
end

class BrowserFixture
  attr_accessor :source, :result, :on_await
  attr_reader :launches, :downloads, :compiles

  def initialize
    @source = 'Game.init'
    @result = { 'ok' => true, 'bytes' => 100, 'diagnostics' => [] }
    @launches = @downloads = 0
    @compiles = []
    @built = false
  end

  def compile(mode)
    @compiles << mode
    @built = @result['ok'] if mode == 'build'
    BrowserResult.new(@result, &@on_await)
  end

  def builtCurrent
    @built
  end

  def cancelCheck; end

  def launch
    @launches += 1
  end

  def downloadZip
    @downloads += 1
    2
  end
end

class StudioUnderTest < StudioComponent
  attr_reader :browser_fixture

  def initialize
    super
    @browser_fixture = BrowserFixture.new
    @busy = false
    @asset_errors = []
    @mounted = true
  end

  def bridge
    @browser_fixture
  end

  # Keep real Funicular state normalization/patching, without a browser DOM.
  def re_render; end
end

class WebComponentsTest < Minitest::Test
  def test_stale_syntax_result_does_not_replace_the_current_view
    studio = StudioUnderTest.new
    studio.bridge.result = { 'ok' => false, 'diagnostics' => [{ 'message' => 'old error' }] }
    studio.bridge.on_await = -> { studio.bridge.source = 'Game.tick(30)' }
    studio.check_source
    refute_equal 'エラー', studio.state[:ui]['build']
    assert_empty studio.state[:ui]['diagnostics']
  end

  def test_failed_build_keeps_the_previous_game_and_releases_controls
    studio = StudioUnderTest.new
    studio.bridge.result = { 'ok' => false, 'diagnostics' => [{ 'message' => 'syntax error' }] }
    refute studio.build
    assert_equal 0, studio.bridge.launches
    assert_equal false, studio.state[:ui]['busy']
    assert_equal 'error', studio.state[:ui]['kind']
    assert_equal 'syntax error', studio.state[:ui]['diagnostics'][0]['message']
  end

  def test_successful_build_launches_once_and_reports_the_binary_size
    studio = StudioUnderTest.new
    assert studio.build
    assert_equal 1, studio.bridge.launches
    assert_equal false, studio.state[:ui]['busy']
    assert_equal 'game.mrb · 100 B', studio.state[:ui]['diagnostics'][0]['message']
  end

  def test_zip_builds_once_and_reuses_the_current_binary
    studio = StudioUnderTest.new
    studio.download
    studio.download
    assert_equal ['build'], studio.bridge.compiles
    assert_equal 2, studio.bridge.downloads
    assert_equal 0, studio.bridge.launches
  end

  def test_failed_build_does_not_download_a_zip
    studio = StudioUnderTest.new
    studio.bridge.result = { 'ok' => false, 'diagnostics' => [] }
    studio.download
    assert_equal 0, studio.bridge.downloads
  end

  def test_asset_discovery_handles_static_names_and_skips_interpolation
    source = %q{a = 'hero.raw'; b = "sound.WAV"; c = 'hero.raw'; d = "font.pbf"; e = "map#{n}.raw"}
    assert_equal ['hero.raw', 'sound.WAV', 'font.pbf'], EmulatorComponent.new.source_asset_names(source)
  end
end
