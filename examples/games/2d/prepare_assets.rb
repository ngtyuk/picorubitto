#!/usr/bin/env ruby
# frozen_string_literal: true

# Keep source images separate from the runtime assets consumed by PicoRuby.

require_relative '../../../tools/assets/png_to_rgb565'

source = File.expand_path('source-assets/document-kun.png', __dir__)
destination = File.expand_path('assets/document-kun.raw', __dir__)

abort "source image not found: #{source}" unless File.file?(source)

result = PngToRgb565.convert(source, destination, scale: 1.0, key_color: 0xf81f)
puts "Prepared #{destination} (#{result[:width]}x#{result[:height]})"
