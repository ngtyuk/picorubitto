require 'minitest/autorun'
require 'tmpdir'
require 'ostruct'
require_relative '../../tools/build/vm_profile'

class VMProfileTest < Minitest::Test
  Profile = PicoRubyGaming::VMProfile

  def test_invalid_and_debug_speed_profiles_are_rejected
    assert_equal 'size', Profile.validate('size', debug: true)
    assert_raises(ArgumentError) { Profile.validate('fast') }
    assert_raises(ArgumentError) { Profile.validate('speed', debug: true) }
  end

  def test_release_defaults_to_speed_without_breaking_debug_or_wasm_only
    assert_equal 'speed', Profile.default_for
    assert_equal 'size', Profile.default_for(debug: true)
    assert_equal 'size', Profile.default_for(firmware: false)
    assert_equal '', Profile.firmware_suffix('speed')
    assert_equal '-vm-size', Profile.firmware_suffix('size')
    assert_equal '', Profile.firmware_suffix('size', default: Profile.default_for(debug: true))
    assert_equal 256, Profile.method_cache_default_for
    assert_equal 0, Profile.method_cache_default_for(debug: true)
    assert_equal 0, Profile.method_cache_default_for(firmware: false)
  end

  def test_host_compiler_prefers_the_default_build_with_legacy_fallback
    Dir.mktmpdir('gaming-compiler-default-') do |root|
      speed = File.join(Profile.build_root(root, 'speed'), 'mrbc/default/bin/mrbc')
      size = File.join(Profile.build_root(root, 'size'), 'mrbc/default/bin/mrbc')
      assert_equal speed, Profile.host_compiler(root)
      FileUtils.mkdir_p(File.dirname(size))
      File.write(size, 'legacy host compiler')
      assert_equal size, Profile.host_compiler(root)
      FileUtils.mkdir_p(File.dirname(speed))
      File.write(speed, 'default host compiler')
      assert_equal speed, Profile.host_compiler(root)
    end
  end

  def test_host_compiler_prefers_production_cache_and_can_use_comparison_or_legacy_builds
    Dir.mktmpdir('gaming-cache-compiler-') do |root|
      legacy = File.join(root, 'mruby-vm-speed-method-cache256/mrbc/default/bin/mrbc')
      disabled = File.join(Profile.build_root(root, 'speed', method_cache: 0), 'mrbc/default/bin/mrbc')
      production = File.join(Profile.build_root(root, 'speed'), 'mrbc/default/bin/mrbc')
      [legacy, disabled, production].each do |path|
        FileUtils.mkdir_p(File.dirname(path))
        File.write(path, 'host compiler')
        assert_equal path, Profile.host_compiler(root)
      end
    end
  end

  def test_only_mruby_core_compiler_changes
    vm = OpenStruct.new(cc: OpenStruct.new(flags: [['-Os', '-fno-unroll-loops'], '-fshort-enums']))
    game = OpenStruct.new(cc: OpenStruct.new(flags: ['-Os', '-O2']))
    conf = OpenStruct.new(gems: { 'picoruby-mruby' => vm, 'picoruby-game' => game })
    Profile.configure(conf, 'speed')
    vm.build_config_initializer.call(vm)
    assert_equal ['-fno-unroll-loops', '-fshort-enums', '-O2'], vm.cc.flags
    assert_equal ['-Os', '-O2'], game.cc.flags
  end

  def test_method_cache_is_build_wide_and_has_separate_outputs
    conf = OpenStruct.new(defines: [])
    Profile.configure_method_cache(conf, 0)
    assert_empty conf.defines
    Profile.configure_method_cache(conf, 256)
    assert_equal ['MRB_METHOD_CACHE_SIZE=256'], conf.defines
    assert_equal '-method-cache-off', Profile.method_cache_suffix(0)
    assert_equal '', Profile.method_cache_suffix(256)
    assert_equal '', Profile.method_cache_suffix(0, default: 0)
    assert_equal '-method-cache256', Profile.method_cache_suffix(256, default: 0)
    refute_equal Profile.build_root('/tmp/game', 'speed'),
                 Profile.build_root('/tmp/game', 'speed', method_cache: 0)
    assert_equal '/tmp/game/mruby-vm-speed', Profile.build_root('/tmp/game', 'speed')
    assert_equal '/tmp/game/mruby-vm-speed-method-cache-off',
                 Profile.build_root('/tmp/game', 'speed', method_cache: 0)
    assert_equal 128, Profile.method_cache_size('128')
    [-1, 1, 3, 2048, 'auto', '256.0'].each do |value|
      assert_raises(ArgumentError) { Profile.method_cache_size(value) }
    end
  end

  def test_separate_roots_and_changed_settings_invalidate_only_cross_target
    Dir.mktmpdir('gaming-vm-profile-') do |root|
      size = Profile.build_root(root, 'size')
      speed = Profile.build_root(root, 'speed')
      refute_equal size, speed
      target = File.join(speed, 'pico-gaming')
      host = File.join(speed, 'mrbc', 'keep')
      FileUtils.mkdir_p(File.dirname(host))
      File.write(host, 'host compiler')
      FileUtils.mkdir_p(target)
      object = File.join(target, 'vm.o')
      File.write(object, 'stale before receipt')
      settings = { 'vm_opt' => 'speed', 'game_optimize_for_speed' => true }
      Profile.prepare(target, settings)
      refute File.exist?(object)
      FileUtils.mkdir_p(target)
      File.write(object, 'built')
      Profile.record(target, settings)
      Profile.prepare(target, settings)
      assert_equal 'built', File.read(object)
      Profile.prepare(target, settings.merge('game_optimize_for_speed' => false))
      refute File.exist?(object)
      assert_equal 'host compiler', File.read(host)
      # Interrupted/invalid receipt must never certify old objects.
      FileUtils.mkdir_p(target)
      File.write(File.join(target, 'gaming-build.json'), '{')
      File.write(object, 'partial')
      Profile.prepare(target, settings)
      refute File.exist?(object)
    end
  end
end
