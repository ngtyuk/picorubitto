#!/usr/bin/env ruby
# Convert a TrueType/OpenType font into the compact bitmap format used by
# Game::Font. Rasterization happens on the host; RP2350 only reads alpha maps.

require 'fileutils'
require 'open3'
require 'rbconfig'
require 'shellwords'
require 'tmpdir'

def usage
  warn 'Usage: ruby tools/assets/ttf_to_bitmap_font.rb INPUT.ttf OUTPUT.pbf --sizes 12,16,24'
  exit 2
end

input = ARGV.shift
output = ARGV.shift
usage if input.nil? || output.nil?

sizes = nil
until ARGV.empty?
  option = ARGV.shift
  if option == '--sizes'
    sizes = ARGV.shift
  else
    usage
  end
end
usage if sizes.nil? || sizes !~ /\A[0-9]+(?:,[0-9]+)*\z/ || sizes.split(',').any? { |size| size.to_i <= 0 }

unless File.file?(input)
  warn "Input font not found: #{input}"
  exit 1
end

config = ENV.fetch('FREETYPE_CONFIG', 'freetype-config')
begin
  cflags = `#{Shellwords.escape(config)} --cflags`.split
  libs = `#{Shellwords.escape(config)} --libs`.split
rescue Errno::ENOENT
  cflags = []
  libs = []
end
if cflags.empty? || libs.empty?
  warn 'FreeType development tools are required (freetype-config --cflags --libs).'
  exit 1
end

cc = ENV.fetch('CC', RbConfig::CONFIG.fetch('CC', 'cc'))
source = File.expand_path('ttf_to_bitmap_font.c', __dir__)
FileUtils.mkdir_p(File.dirname(File.expand_path(output)))

Dir.mktmpdir('picoruby-font-') do |directory|
  executable = File.join(directory, 'ttf_to_bitmap_font')
  command = [cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-o', executable,
             source, *cflags, *libs]
  stdout, stderr, status = Open3.capture3(*command)
  unless status.success?
    warn stdout unless stdout.empty?
    warn stderr unless stderr.empty?
    warn 'Could not compile the FreeType bitmap-font converter.'
    exit 1
  end

  stdout, stderr, status = Open3.capture3(executable, input, output, sizes)
  unless status.success?
    warn stdout unless stdout.empty?
    warn stderr unless stderr.empty?
    exit(status.exitstatus || 1)
  end
end

puts "Wrote #{output} (sizes: #{sizes})"
