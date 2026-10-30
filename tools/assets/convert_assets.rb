#!/usr/bin/env ruby
# frozen_string_literal: true

# Convert a game's source assets into the formats understood by the portable
# Game API. PNG files become big-endian RGB565 RAW files and TrueType/OpenType
# files become compact bitmap fonts; WAV and other files are copied unchanged.

require 'fileutils'
require 'json'
require 'optparse'
require 'rbconfig'

require_relative 'png_to_rgb565'

options = {
  scale: 1.0,
  key_color: 0xf81f,
  font_sizes: '8,10,12,14,16,18,20,24,28,32,40,48,60',
  clean: false
}

parser = OptionParser.new do |opts|
  opts.banner = 'Usage: convert_assets.rb --input DIR --output DIR [options]'
  opts.on('-i', '--input DIR', 'source asset directory') { |value| options[:input] = value }
  opts.on('-o', '--output DIR', 'converted asset directory') { |value| options[:output] = value }
  opts.on('--scale FACTOR', Float, 'nearest-neighbor scale for every PNG') do |value|
    options[:scale] = value
  end
  opts.on('--key-color COLOR', 'RGB565 transparent key, default F81F') do |value|
    options[:key_color] = Integer(value, 0)
  end
  opts.on('--font-sizes SIZES', 'comma-separated bitmap font sizes') do |value|
    options[:font_sizes] = value
  end
  opts.on('--clean', 'remove the output directory before conversion') { options[:clean] = true }
  opts.on('-h', '--help', 'show this help') do
    puts opts
    exit
  end
end
parser.parse!

abort parser.to_s unless options[:input] && options[:output]

input = File.expand_path(options[:input])
output = File.expand_path(options[:output])
abort "source directory not found: #{input}" unless Dir.exist?(input)
abort 'input and output directories must be different' if input == output
abort 'output directory must not be inside input directory' if output.start_with?("#{input}#{File::SEPARATOR}")
if options[:clean] && (output == File::SEPARATOR || output == Dir.pwd)
  abort 'refusing to clean the filesystem root or current directory'
end

begin
  abort 'scale must be greater than zero' unless options[:scale].positive?
  abort 'key color must be between 0 and 0xffff' unless options[:key_color].between?(0, 0xffff)
rescue NoMethodError
  abort 'scale must be a number and key color must be an integer'
end

FileUtils.rm_rf(output) if options[:clean]
FileUtils.mkdir_p(output)

entries = []
destination_sources = {}
files = Dir.glob(File.join(input, '**', '*'), File::FNM_DOTMATCH)
            .select { |path| File.file?(path) }
            .reject { |path| File.basename(path) == '.DS_Store' }
            .sort

files.each do |source|
  relative = source.delete_prefix("#{input}#{File::SEPARATOR}")
  relative = relative.tr(File::SEPARATOR, '/')
  extension = File.extname(relative)
  destination_relative = if extension.casecmp('.png').zero?
                           relative.sub(/\.png\z/i, '.raw')
                         elsif extension.casecmp('.ttf').zero? || extension.casecmp('.otf').zero?
                           relative.sub(/\.(ttf|otf)\z/i, '.pbf')
                         else
                           relative
                         end
  if destination_sources.key?(destination_relative)
    abort "asset output collision: #{destination_sources[destination_relative]} and #{relative} -> #{destination_relative}"
  end
  destination_sources[destination_relative] = relative
  destination = File.join(output, destination_relative)
  FileUtils.mkdir_p(File.dirname(destination))

  if destination_relative.end_with?('.raw') && File.extname(relative).casecmp('.png').zero?
    result = PngToRgb565.convert(
      source,
      destination,
      scale: options[:scale],
      key_color: options[:key_color]
    )
    entries << {
      'source' => relative,
      'path' => destination_relative,
      'type' => 'texture',
      'width' => result[:width],
      'height' => result[:height],
      'bytes' => result[:bytes],
      'key_color' => format('0x%04x', result[:key_color])
    }
    puts "Converted #{relative} -> #{destination_relative} (#{result[:width]}x#{result[:height]})"
  elsif destination_relative.end_with?('.pbf') &&
        (extension.casecmp('.ttf').zero? || extension.casecmp('.otf').zero?)
    converter = File.expand_path('ttf_to_bitmap_font.rb', __dir__)
    command = [RbConfig.ruby, converter, source, destination, '--sizes', options[:font_sizes]]
    abort "font conversion failed for #{relative}" unless system(*command)
    entries << {
      'source' => relative,
      'path' => destination_relative,
      'type' => 'font',
      'sizes' => options[:font_sizes].split(',').map(&:to_i),
      'bytes' => File.size(destination)
    }
    puts "Converted #{relative} -> #{destination_relative} (sizes #{options[:font_sizes]})"
  else
    FileUtils.copy_file(source, destination)
    entries << {
      'source' => relative,
      'path' => destination_relative,
      'type' => 'file',
      'bytes' => File.size(destination)
    }
    puts "Copied #{relative}"
  end
end

manifest = {
  'version' => 1,
  'format' => 'rgb565-be',
  'scale' => options[:scale],
  'key_color' => format('0x%04x', options[:key_color]),
  'assets' => entries
}
manifest_path = File.join(output, 'asset-manifest.json')
File.write(manifest_path, JSON.pretty_generate(manifest) + "\n")
puts "Wrote #{manifest_path}"
