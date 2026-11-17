# frozen_string_literal: true

require 'digest'
require 'json'
require 'fileutils'

module PicoRubyGaming
  # VM optimization changes only picoruby-mruby's compiler. The method
  # cache changes mrb_state's layout, so its define must apply to every gem.
  module VMProfile
    NAMES = %w[size speed].freeze
    DEFAULT = 'speed'.freeze
    DEFAULT_METHOD_CACHE_SIZE = 256
    METHOD_CACHE_SIZES = [0, 64, 128, 256, 512, 1024].freeze
    module_function

    def validate(name, debug: false)
      raise ArgumentError, "unknown VM optimization: #{name}" unless NAMES.include?(name)
      raise ArgumentError, 'VM speed comparison requires a release build' if name == 'speed' && debug
      name
    end

    def default_for(firmware: true, debug: false)
      firmware && !debug ? DEFAULT : 'size'
    end

    def method_cache_default_for(firmware: true, debug: false)
      firmware && !debug ? DEFAULT_METHOD_CACHE_SIZE : 0
    end

    def host_compiler(root)
      profiles = [DEFAULT, 'size'].uniq
      candidates = profiles.map do |name|
        File.join(build_root(root, name), 'mrbc', 'default', 'bin', 'mrbc')
      end
      (METHOD_CACHE_SIZES - [DEFAULT_METHOD_CACHE_SIZE]).each do |size|
        profiles.each do |name|
          candidates << File.join(build_root(root, name, method_cache: size), 'mrbc', 'default', 'bin', 'mrbc')
        end
      end
      # Earlier comparison builds kept cache256 in a suffixed directory.
      profiles.each do |name|
        base = name == 'size' ? 'mruby' : 'mruby-vm-speed'
        candidates << File.join(root, "#{base}-method-cache256", 'mrbc', 'default', 'bin', 'mrbc')
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

    def method_cache_size(value)
      size = Integer(value.to_s, 10)
      raise ArgumentError unless METHOD_CACHE_SIZES.include?(size)
      size
    rescue ArgumentError, TypeError
      raise ArgumentError, "method cache entries must be #{METHOD_CACHE_SIZES.join(', ')}"
    end

    def configure_method_cache(conf, value)
      size = method_cache_size(value)
      # This changes sizeof(mrb_state): use build-wide defines for every gem.
      conf.defines << "MRB_METHOD_CACHE_SIZE=#{size}" if size.positive?
    end

    def method_cache_suffix(value, default: DEFAULT_METHOD_CACHE_SIZE)
      size = method_cache_size(value)
      return '' if size == method_cache_size(default)
      size.zero? ? '-method-cache-off' : "-method-cache#{size}"
    end

    def build_root(root, name, method_cache: DEFAULT_METHOD_CACHE_SIZE)
      validate(name)
      File.join(root, (name == 'size' ? 'mruby' : 'mruby-vm-speed') + method_cache_suffix(method_cache))
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
