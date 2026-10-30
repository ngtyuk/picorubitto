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

  def test_only_mruby_core_compiler_changes
    vm = OpenStruct.new(cc: OpenStruct.new(flags: [['-Os', '-fno-unroll-loops'], '-fshort-enums']))
    game = OpenStruct.new(cc: OpenStruct.new(flags: ['-Os', '-O2']))
    conf = OpenStruct.new(gems: { 'picoruby-mruby' => vm, 'picoruby-game' => game })
    Profile.configure(conf, 'speed')
    vm.build_config_initializer.call(vm)
    assert_equal ['-fno-unroll-loops', '-fshort-enums', '-O2'], vm.cc.flags
    assert_equal ['-Os', '-O2'], game.cc.flags
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
