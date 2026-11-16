# frozen_string_literal: true

# Runtime profile shared by the RP2350 and WASM PicoRuby builds.
# Target-specific configurations should add their backend gems separately; the
# safe-to-share game build profile belongs in this single list.
module PicoRubyGaming
  module RuntimeProfile
    RUBY_GEMS = %w[
      mruby-math mruby-array-ext mruby-numeric-ext mruby-string-ext mruby-metaprog
    ].freeze
    COMMON_GEMS = %w[picoruby-game-common].freeze

    module_function

    def add_ruby_gems(conf, mruby_root)
      RUBY_GEMS.each do |name|
        path = File.join(mruby_root, 'mrbgems', name)
        abort "common Ruby gem not found: #{path}" unless Dir.exist?(path)

        conf.gem gemdir: path
      end
    end

    def add_common_gems(conf, project_root)
      COMMON_GEMS.each do |name|
        path = File.join(project_root, 'mrbgems', name)
        abort "common game gem not found: #{path}" unless Dir.exist?(path)

        conf.gem gemdir: path
      end
    end
  end
end
