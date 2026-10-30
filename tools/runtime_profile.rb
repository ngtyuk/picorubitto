# frozen_string_literal: true

# Runtime profile shared by the RP2350 and WASM PicoRuby builds.
# Target-specific configurations should add their backend gems separately; the
# safe-to-share game build profile belongs in this single list.
module PicoRubyGaming
  module RuntimeProfile
    COMMON_GEMS = %w[picoruby-game-common].freeze

    module_function

    def add_common_gems(conf, project_root)
      COMMON_GEMS.each do |name|
        path = File.join(project_root, 'mrbgems', name)
        abort "common game gem not found: #{path}" unless Dir.exist?(path)

        conf.gem gemdir: path
      end
    end
  end
end
