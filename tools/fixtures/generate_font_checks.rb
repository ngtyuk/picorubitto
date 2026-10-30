#!/usr/bin/env ruby
# Deterministic PBF coverage: two sizes, negative bearings, empty/opaque/alpha glyphs.
require 'fileutils'

output = File.expand_path(ARGV.fetch(0, 'build/tests/hardware-suite/assets'))
FileUtils.mkdir_p(output)
first = 32
count = 35 # space through B; other characters use '?'.
sizes = [12, 24]
patterns = {
  '?' => [2, 1, 0x59, 9, 6],
  'A' => [0x7e, 0x11, 0x11, 0x11, 0x7e],
  'B' => [0x7f, 0x49, 0x49, 0x49, 0x36]
}
headers = ''.b
records = ''.b
bitmaps = ''.b
bitmap_base = 16 + sizes.length * 20 + sizes.length * count * 20
sizes.each_with_index do |size, index|
  scale = size / 12
  records_offset = 16 + sizes.length * 20 + index * count * 20
  headers << [size, size, 9 * scale, 0, count, 0, records_offset, bitmap_base].pack('n2s>2n2N2')
  count.times do |glyph|
    character = (first + glyph).chr
    empty = character == ' '
    width = empty ? 0 : 5 * scale
    height = empty ? 0 : 7 * scale
    pixels = (0...height).flat_map do |y|
      (0...width).map do |x|
        on = patterns.fetch(character, patterns['?'])[x / scale] & (1 << (y / scale))
        on.zero? ? 0 : (character == 'B' ? 255 : [64, 128, 192, 255, 255][x / scale])
      end
    end.pack('C*')
    bearing_x = character == 'A' ? -scale : 0
    bearing_y = (character == 'B' ? 8 : 7) * scale
    advance = (empty ? 3 : 6) * scale
    records << [width, height, bearing_x, bearing_y, advance, 0,
                bitmap_base + bitmaps.bytesize, pixels.bytesize].pack('n2s>3nN2')
    bitmaps << pixels
  end
end
bytes = 'PBF1'.b + [first, count, sizes.length, 0].pack('n4') +
        [bitmap_base + bitmaps.bytesize].pack('N') + headers + records + bitmaps
File.binwrite(File.join(output, 'font-check.pbf'), bytes)
invalid = bytes.dup
invalid[16 + sizes.length * 20 + 12, 4] = [bytes.bytesize + 1].pack('N')
File.binwrite(File.join(output, 'font-invalid.pbf'), invalid)
puts "Font checks: #{output} (#{bytes.bytesize} bytes each)"
