#!/usr/bin/env ruby
# frozen_string_literal: true

require 'fileutils'
require 'optparse'
require 'rbconfig'
require_relative 'checks_source'

root = File.expand_path('../..', __dir__)
options = {
  output: nil,
  compare_3d: false,
  audio_only: false,
  compiler: File.join(root, 'build/mruby/mrbc/default/bin/mrbc')
}
OptionParser.new do |parser|
  parser.banner = 'Usage: build_checks.rb [--compare-3d | --audio-only] [--output DIR] [--mrbc PATH]'
  parser.on('--compare-3d', 'include legacy array rendering for explicit comparison') { options[:compare_3d] = true }
  parser.on('--audio-only', 'boot directly into the BGM and sound-effects pad') { options[:audio_only] = true }
  parser.on('--output DIR') { |value| options[:output] = File.expand_path(value) }
  parser.on('--mrbc PATH') { |value| options[:compiler] = File.expand_path(value) }
end.parse!
abort '--audio-only and --compare-3d are mutually exclusive' if options[:audio_only] && options[:compare_3d]
abort "mrbc not found: #{options[:compiler]}; build the engine first" unless File.file?(options[:compiler])

def compile_cartridge(output, compiler, source)
  File.write(File.join(output, 'main.rb'), source)
  rom = File.join(output, 'game.mrb')
  FileUtils.rm_f(rom)
  abort 'check cartridge compilation failed' unless system(compiler, '-o', rom, File.join(output, 'main.rb'))
  checksum = File.binread(rom).bytes.reduce(2_166_136_261) { |sum, byte| ((sum ^ byte) * 16_777_619) & 0xffff_ffff }
  puts "Checks: #{rom} (#{File.size(rom)} bytes, fnv1a=#{format('%08x', checksum)})"
end

if options[:audio_only]
  output = options[:output] || File.join(root, 'build/tests/audio-suite')
  FileUtils.mkdir_p(File.join(output, 'assets'))
  abort 'audio fixture generation failed' unless system(RbConfig.ruby,
    File.join(root, 'tools/fixtures/generate_audio_checks.rb'), File.join(output, 'assets'))
  source = File.read(File.join(root, 'tests/portable/hardware_audio.rb')) + "\n" +
           File.read(File.join(root, 'tests/portable/hardware_audio_standalone.rb'))
  compile_cartridge(output, options[:compiler], source)
  puts 'Audio-only: BGM + three sound effects; START stops, A restarts the pad.'
  puts 'Copy game.mrb and assets/ to the SD root.'
  exit
end

profile = options[:compare_3d] ? 'comparison' : 'standard'
output = options[:output] || File.join(root, "build/tests/hardware-suite#{options[:compare_3d] ? '-comparison' : ''}")
FileUtils.mkdir_p(File.join(output, 'assets'))
%w[texture.raw font.pbf].each do |name|
  FileUtils.cp(File.join(root, 'tests/portable/assets', name), File.join(output, 'assets', name))
end
%w[document-kun.raw beep.wav].each do |name|
  FileUtils.cp(File.join(root, 'examples/games/2d/assets', name), File.join(output, 'assets', name))
end
abort 'font fixture generation failed' unless system(RbConfig.ruby,
  File.join(root, 'tools/fixtures/generate_font_checks.rb'), File.join(output, 'assets'))
abort 'performance fixture generation failed' unless system(RbConfig.ruby,
  File.join(root, 'tools/fixtures/generate_performance_assets.rb'), File.join(output, 'assets'))
abort 'audio fixture generation failed' unless system(RbConfig.ruby,
  File.join(root, 'tools/fixtures/generate_audio_checks.rb'), File.join(output, 'assets'))
# A 16MiB virtual cartridge, generated locally rather than committed.
large = File.join(output, 'assets/large')
FileUtils.mkdir_p(large)
palette = [0xf800, 0x07e0, 0x001f, 0xffe0, 0x07ff, 0xfc00]
checksums = 32.times.map do |index|
  band = 8.times.map { |bit| [(index & (1 << bit)) == 0 ? 0x0000 : 0xffff].pack('n') * 64 }.join * 16
  pixels = band + (0...496).map do |y|
    8.times.map { |x| [palette[(x + y / 64 + index) % palette.length]].pack('n') * 64 }.join
  end.join
  File.binwrite(File.join(large, "scene-#{index}.raw"), pixels)
  pixels.bytes.reduce(2_166_136_261) { |sum, byte| ((sum ^ byte) * 16_777_619) & 0xffff_ffff }
end
# Compile a single file, so the cartridge needs no source loader or Ruby files on SD.
sources = %w[game_api_contract.rb hardware_performance.rb hardware_vm_performance.rb hardware_audio.rb]
sources << 'hardware_performance_legacy.rb' if options[:compare_3d]
sources << 'hardware_suite.rb'
source = "HARDWARE_SUITE = true\nHARDWARE_PERFORMANCE_PROFILE = #{profile.inspect}\nASSET_GC_CHECKSUMS = #{checksums.inspect}\n" + GamingCheckSource.flight + sources.map do |name|
  File.read(File.join(root, 'tests/portable', name))
end.join("\n")
compile_cartridge(output, options[:compiler], source)
puts "Performance profile: #{profile}"
puts 'Large assets: 32 x 512KiB = 16MiB; ASSET / GC reads three rounds.'
puts 'Copy game.mrb and assets/ to the SD root.'
