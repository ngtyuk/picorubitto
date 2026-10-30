#!/usr/bin/env ruby
# frozen_string_literal: true

require 'fileutils'

# Original deterministic PCM fixtures. No recordings or external music needed.
module AudioCheckAssets
  RATE = 22_050
  BGM_MS = 4000

  def self.write_wav(path, milliseconds, rate: RATE, amplitude: 95)
    count = rate * milliseconds / 1000
    pcm = Array.new(count) do |index|
      time = index.to_f / rate
      edge = [index / (rate * 0.005), (count - 1 - index) / (rate * 0.005), 1.0].min
      value = yield(time, index) * edge
      [[(128 + value * amplitude).round, 0].max, 255].min
    end.pack('C*')
    # RIFF chunks are word-aligned, while the data size excludes the pad byte.
    padding = pcm.bytesize.odd? ? "\0" : ''
    header = 'RIFF' + [36 + pcm.bytesize + padding.bytesize].pack('V') + 'WAVEfmt ' +
             [16, 1, 1, rate, rate, 1, 8].pack('VvvVVvv') + 'data' + [pcm.bytesize].pack('V')
    File.binwrite(path, header + pcm + padding)
  end

  def self.generate(output)
    directory = File.join(output, 'audio')
    FileUtils.mkdir_p(directory)
    { 'beep-c5' => 523.25, 'beep-d5' => 587.33,
      'beep-e5' => 659.25, 'beep-g5' => 783.99 }.each do |name, frequency|
      write_wav(File.join(directory, name + '.wav'), 600) { |t| Math.sin(2 * Math::PI * frequency * t) }
    end
    write_wav(File.join(directory, 'jump.wav'), 900) do |t|
      Math.sin(2 * Math::PI * (240 * t + 650 * t * t)) * (1 - t / 0.9)
    end
    random = 0x12345
    write_wav(File.join(directory, 'hit.wav'), 900) do |t|
      random = (random * 1_664_525 + 1_013_904_223) & 0xffff_ffff
      noise = ((random >> 16) / 32767.5 - 1)
      (noise * 0.7 + Math.sin(2 * Math::PI * 90 * t) * 0.3) * (1 - t / 0.9)
    end
    write_wav(File.join(directory, 'coin.wav'), 900) do |t|
      frequency = t < 0.18 ? 1318.51 : 1760.0
      Math.sin(2 * Math::PI * frequency * t) * (1 - t / 0.9)
    end
    melody = [523.25, 659.25, 783.99, 659.25, 587.33, 698.46, 880.0, 698.46,
              659.25, 783.99, 1046.5, 783.99, 587.33, 698.46, 783.99, 587.33]
    bass = [130.81, 146.83, 164.81, 146.83]
    write_wav(File.join(directory, 'bgm.wav'), BGM_MS) do |t|
      beat = (t / 0.25).to_i
      position = t - beat * 0.25
      envelope = [position / 0.008, (0.25 - position) / 0.04, 1.0].min
      lead = Math.sin(2 * Math::PI * melody[beat % 16] * position) * envelope
      low = Math.sin(2 * Math::PI * bass[(beat / 4) % 4] * t)
      lead * 0.65 + low * 0.25
    end
    # A 256-sample period keeps the steady phases aligned after the native
    # 2x2048-sample DMA prefill. Compare the middle of each four-second sound.
    cycles = [6, 7, 8, 9]
    cycles.each_with_index do |cycle, tone|
      write_wav(File.join(directory, "mix-tone-#{tone}.wav"), 4000) do |_t, index|
        Math.sin(2 * Math::PI * cycle * (index % 256) / 256)
      end
    end
    write_wav(File.join(directory, 'mix-reference.wav'), 4000) do |_t, index|
      cycles.sum { |cycle| Math.sin(2 * Math::PI * cycle * (index % 256) / 256) } / 4
    end
    # Keep each WAV below the 128KiB runtime limit. Low-amplitude fixtures let
    # the probe use source gain 255 and a stable full-scale dial position.
    [48_000, 22_050].each do |rate|
      write_wav(File.join(output, "audio-rate-tone-#{rate}.wav"), 2500, rate: rate, amplitude: 32) do |time|
        Math.sin(2 * Math::PI * 1000 * time)
      end
    end
    write_wav(File.join(output, 'audio-rate-silence-48000.wav'), 2500, rate: 48_000) { 0 }
    puts "Audio fixtures: #{directory} (13 pad/mix WAVs + 3 rate probes)"
  end
end

AudioCheckAssets.generate(ARGV.fetch(0)) if $PROGRAM_NAME == __FILE__
