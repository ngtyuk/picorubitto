#!/usr/bin/env ruby
# frozen_string_literal: true

begin
  require 'chunky_png'
rescue LoadError
  abort 'chunky_png is required: gem install chunky_png'
end

require 'fileutils'
module PngToRgb565
  module_function

  def convert(input, output, scale: 1.0, key_color: 0xf81f)
    raise ArgumentError, 'scale must be greater than zero' unless scale.positive?
    raise ArgumentError, 'key color must be between 0 and 0xffff' unless key_color.between?(0, 0xffff)

    image = ChunkyPNG::Image.from_file(input)
    width = [(image.width * scale).round, 1].max
    height = [(image.height * scale).round, 1].max
    bytes = String.new(encoding: Encoding::BINARY)

    height.times do |y|
      source_y = [(y / scale).floor, image.height - 1].min
      width.times do |x|
        source_x = [(x / scale).floor, image.width - 1].min
        pixel = image[source_x, source_y]
        alpha = ChunkyPNG::Color.a(pixel)
        color = if alpha < 128
                  key_color
                else
                  red = ChunkyPNG::Color.r(pixel)
                  green = ChunkyPNG::Color.g(pixel)
                  blue = ChunkyPNG::Color.b(pixel)
                  ((red * 31 + 127) / 255 << 11) |
                    ((green * 63 + 127) / 255 << 5) |
                    ((blue * 31 + 127) / 255)
                end
        bytes << [color].pack('n')
      end
    end

    FileUtils.mkdir_p(File.dirname(File.expand_path(output)))
    File.binwrite(output, bytes)
    { width: width, height: height, bytes: bytes.bytesize, key_color: key_color }
  end
end
if $PROGRAM_NAME == __FILE__
  require 'optparse'

  options = { scale: 1.0, key_color: 0xf81f }
  parser = OptionParser.new do |opts|
    opts.banner = 'Usage: png_to_rgb565.rb INPUT.png OUTPUT.raw [options]'
    opts.on('--scale FACTOR', Float, 'nearest-neighbor scale, e.g. 0.5') do |value|
      options[:scale] = value
    end
    opts.on('--key-color COLOR', 'RGB565 transparent key, default F81F') do |value|
      options[:key_color] = Integer(value, 0)
    end
  end
  parser.parse!

  abort parser.to_s unless ARGV.length == 2
  input, output = ARGV
  begin
    result = PngToRgb565.convert(input, output, **options)
  rescue ArgumentError => e
    abort e.message
  end
  puts "Wrote #{output} (#{result[:width]}x#{result[:height]}, #{result[:bytes]} bytes)"
end
