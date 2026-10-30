#!/usr/bin/env ruby
# frozen_string_literal: true

require 'fileutils'
require 'optparse'
require 'rbconfig'

options = {
  output: File.expand_path('../../build/games/manual', __dir__),
  compiler: ENV['PICORBC'] || File.expand_path('../../build/mruby/mrbc/default/bin/mrbc', __dir__),
  convert_assets: false,
  asset_scale: 1.0,
  asset_key_color: 0xf81f,
  font_sizes: '8,10,12,14,16,18,20,24,28,32,40,48,60'
}

parser = OptionParser.new do |opts|
  opts.banner = 'Usage: build_game.rb --source GAME.rb [options]'
  opts.on('-s', '--source PATH', 'Ruby entrypoint') { |value| options[:source] = value }
  opts.on('-o', '--output PATH', 'cartridge output directory') { |value| options[:output] = value }
  opts.on('--mrbc PATH', '--picorbc PATH', 'PicoRuby compiler executable') { |value| options[:compiler] = value }
  opts.on('--convert-assets', 'convert PNG and font assets to runtime formats') { options[:convert_assets] = true }
  opts.on('--asset-scale FACTOR', Float, 'nearest-neighbor scale for PNG assets') do |value|
    options[:asset_scale] = value
  end
  opts.on('--asset-key-color COLOR', 'RGB565 transparent key for PNG assets') do |value|
    options[:asset_key_color] = Integer(value, 0)
  end
  opts.on('--font-sizes SIZES', 'comma-separated bitmap font sizes') do |value|
    options[:font_sizes] = value
  end
  opts.on('-h', '--help', 'show this help') do
    puts opts
    exit
  end
end
parser.parse!

abort parser.to_s unless options[:source]

source = File.expand_path(options[:source])
output = File.expand_path(options[:output])
compiler = File.expand_path(options[:compiler])
assets = File.join(File.dirname(source), 'assets')
rom = File.join(output, 'game.mrb')

abort "source not found: #{source}" unless File.file?(source)
abort "mrbc not found: #{compiler}" unless File.file?(compiler)

FileUtils.mkdir_p(output)
FileUtils.rm_f(rom)
rom_basename = File.basename(rom)
Dir.children(output).each do |entry|
  next if entry == rom_basename
  next unless entry.casecmp?(rom_basename)

  legacy_rom = File.join(output, entry)
  FileUtils.rm_f(legacy_rom) if File.file?(legacy_rom)
end

command = [compiler, '-o', rom, source]
abort 'PicoRuby compilation failed' unless system(*command)
abort 'compiler did not create game.mrb' unless File.file?(rom) && File.size(rom).positive?

destination_assets = File.join(output, 'assets')
if Dir.exist?(assets)
  if options[:convert_assets]
    converter = File.expand_path('../assets/convert_assets.rb', __dir__)
    command = [RbConfig.ruby, converter,
               '--input', assets,
               '--output', destination_assets,
               '--scale', options[:asset_scale].to_s,
               '--key-color', format('0x%04x', options[:asset_key_color]),
               '--font-sizes', options[:font_sizes],
               '--clean']
    abort 'asset conversion failed' unless system(*command)
  else
    FileUtils.rm_rf(destination_assets)
    FileUtils.cp_r(assets, destination_assets)
  end
end

puts "Wrote #{rom} (#{File.size(rom)} bytes)"
puts "Copied #{destination_assets}" if Dir.exist?(destination_assets)
