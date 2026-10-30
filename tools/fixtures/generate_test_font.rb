#!/usr/bin/env ruby
# Generate the tiny deterministic bitmap font used by the portable contract.

project_root = File.expand_path('../..', __dir__)
output = File.expand_path(ARGV.fetch(0, File.join(project_root, 'tests/portable/assets/font.pbf')))
sizes = [12]
first_char = 32
glyph_count = 95

patterns = {
  'A' => [0x7e, 0x11, 0x11, 0x11, 0x7e],
  'C' => [0x3e, 0x41, 0x41, 0x41, 0x22],
  'F' => [0x7f, 0x09, 0x09, 0x09, 0x01],
  'N' => [0x7f, 0x04, 0x08, 0x10, 0x7f],
  'O' => [0x3e, 0x41, 0x41, 0x41, 0x3e],
  'T' => [0x01, 0x01, 0x7f, 0x01, 0x01],
  '?' => [0x02, 0x01, 0x59, 0x09, 0x06]
}

glyphs = (0...glyph_count).map do |index|
  character = (first_char + index).chr
  if character == ' '
    { width: 0, height: 0, bearing_x: 0, bearing_y: 0, advance: 6, bitmap: ''.b }
  else
    columns = patterns.fetch(character, patterns['?'])
    bitmap = columns.flat_map do |column|
      (0...7).map { |row| (column & (1 << row)).zero? ? 0 : 255 }
    end.pack('C*')
    { width: 5, height: 7, bearing_x: 0, bearing_y: 7, advance: 6, bitmap: bitmap }
  end
end

header_bytes = 16
size_header_bytes = 20
record_bytes = 20
records_base = header_bytes + sizes.length * size_header_bytes
bitmap_base = records_base + sizes.length * glyph_count * record_bytes
bitmap_cursor = bitmap_base
records = []
bitmaps = ''.b

sizes.each do |pixel_size|
  size_records = []
  glyphs.each do |glyph|
    size_records << [glyph[:width], glyph[:height], glyph[:bearing_x], glyph[:bearing_y],
                     glyph[:advance], 0, bitmap_cursor, glyph[:bitmap].bytesize]
    bitmap_cursor += glyph[:bitmap].bytesize
    bitmaps << glyph[:bitmap]
  end
  records << {
    pixel_size: pixel_size,
    line_height: 12,
    ascender: 9,
    records_offset: records_base + records.length * glyph_count * record_bytes,
    bitmap_offset: bitmap_cursor - bitmaps.bytesize,
    glyphs: size_records
  }
end

data = +'PBF1'.b
data << [first_char, glyph_count, sizes.length, 0].pack('n4')
data << [bitmap_cursor].pack('N')
records.each do |font_size|
  data << [font_size[:pixel_size], font_size[:line_height]].pack('n2')
  data << [font_size[:ascender], 0].pack('s>2')
  data << [glyph_count, 0].pack('n2')
  data << [font_size[:records_offset], font_size[:bitmap_offset]].pack('N2')
  font_size[:glyphs].each do |glyph|
    data << glyph.pack('n2s>3nN2')
  end
end
data << bitmaps
raise 'test font size mismatch' unless data.bytesize == bitmap_cursor

File.binwrite(output, data)
puts "Wrote #{output} (#{data.bytesize} bytes)"
