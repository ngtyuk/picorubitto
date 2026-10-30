#!/usr/bin/env ruby
# Reproducible stress assets, generated rather than stored as game media.
require 'fileutils'
output = File.expand_path(ARGV.fetch(0, 'build/tests/hardware-suite/assets'))
directory = File.join(output, 'perf')
FileUtils.mkdir_p(directory)
palette = [0x07ff, 0xffe0, 0xf800, 0x07e0, 0x001f, 0xfc10]
24.times do |index|
  pixels = (0...32).map do |y|
    (0...64).map do |x|
      local_x = x % 32
      frame = x / 32
      if index % 3 != 0 && (local_x - 15) ** 2 + (y - 15) ** 2 > 210
        0xf81f
      elsif y >= 12 && y < 20 && local_x >= 8 && local_x < 23
        bit = (index >> ((local_x - 8) / 3)) & 1
        bit == frame ? 0xffff : 0x0000
      else
        palette[(index + frame + local_x / 8 + y / 8) % palette.length]
      end
    end.pack('n*')
  end.join
  File.binwrite(File.join(directory, "sprite-#{index}.raw"), pixels)
end
background = (0...240).map do |y|
  (0...320).map { |x| (x / 20 + y / 20) % 2 == 0 ? 0x0842 : 0x1083 }.pack('n*')
end.join
File.binwrite(File.join(directory, 'background.raw'), background)
puts "Performance assets: #{directory} (24 two-frame sheets + background)"
