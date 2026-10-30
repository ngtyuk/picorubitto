#!/usr/bin/env ruby
# frozen_string_literal: true

require 'fileutils'

project_root = File.expand_path('../..', __dir__)
output = File.expand_path(ARGV.fetch(0, File.join(project_root, 'examples/games/2d/assets/beep.wav')))
# Keep the generated tone at a rate supported by the RP2350 mixer.
sample_rate = 22_050
duration = 0.18
frequency = 880.0
amplitude = 100.0
sample_count = (sample_rate * duration).to_i
samples = String.new(encoding: Encoding::BINARY)

sample_count.times do |index|
  envelope = 1.0 - (index.to_f / sample_count)
  value = 128.0 + Math.sin(2.0 * Math::PI * frequency * index / sample_rate) * amplitude * envelope
  sample = [[value.round, 0].max, 255].min
  samples << [sample].pack('C')
end

fmt = [1, 1, sample_rate, sample_rate, 1, 8].pack('vvVVvv')
data_chunk = "data" + [samples.bytesize].pack('V') + samples
data_chunk << "\0" if samples.bytesize.odd?
body = "fmt " + [fmt.bytesize].pack('V') + fmt + data_chunk
wave = "RIFF" + [4 + body.bytesize].pack('V') + "WAVE" + body

FileUtils.mkdir_p(File.dirname(output))
File.binwrite(output, wave)
puts "Wrote #{output} (#{wave.bytesize} bytes)"
