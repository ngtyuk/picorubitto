# frozen_string_literal: true

require 'digest'
require 'json'
require 'fileutils'

module PicoRubyGaming
  # The RP2350 experiment changes only picoruby-mruby's compiler, including
  # its VM, GC and core collections. Other gems and the host/WASM stay fixed.
  module VMProfile
    NAMES = %w[size speed].freeze
    DEFAULT = 'speed'.freeze
    module_function

    def validate(name, debug: false)
      raise ArgumentError, "unknown VM optimization: #{name}" unless NAMES.include?(name)
      raise ArgumentError, 'VM speed comparison requires a release build' if name == 'speed' && debug
      name
    end

    def default_for(firmware: true, debug: false)
      firmware && !debug ? DEFAULT : 'size'
    end

    def host_compiler(root)
      candidates = [DEFAULT, 'size'].uniq.map do |name|
        File.join(build_root(root, name), 'mrbc', 'default', 'bin', 'mrbc')
      end
      candidates.find { |path| File.file?(path) } || candidates.first
    end

    def firmware_suffix(name, default: DEFAULT)
      validate(name)
      name == default ? '' : "-vm-#{name}"
    end

    def configure(conf, name)
      validate(name)
      return if name == 'size'
      conf.gems['picoruby-mruby'].build_config_initializer = proc do |spec|
        spec.cc.flags.flatten!
        spec.cc.flags.reject! { |flag| flag.match?(/\A-O(?:[0-3sgz]|fast)\z/) }
        spec.cc.flags << '-O2'
      end
    end

    def build_root(root, name)
      validate(name)
      File.join(root, name == 'size' ? 'mruby' : 'mruby-vm-speed')
    end

    # Rake does not necessarily invalidate an existing object when only an
    # environment flag changes. Invalidate the cross target, never the checkout
    # or the host compiler; write this receipt only after a successful build.
    def prepare(target, settings)
      receipt = File.join(target, 'gaming-build.json')
      current = File.file?(receipt) ? JSON.parse(File.read(receipt)) : nil
      return if current == settings
      FileUtils.rm_rf(target)
    rescue JSON::ParserError
      FileUtils.rm_rf(target)
    end

    def record(target, settings)
      File.write(File.join(target, 'gaming-build.json'), JSON.pretty_generate(settings) + "\n")
    end
  end
end
