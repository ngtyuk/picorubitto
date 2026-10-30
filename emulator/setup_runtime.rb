#!/usr/bin/env ruby
# frozen_string_literal: true

require 'fileutils'
require 'json'
require 'open3'
require 'digest'

picoruby_root = File.expand_path(
  ARGV.fetch(0, ENV.fetch('PICORUBY_ROOT', File.expand_path('../deps/picoruby', __dir__)))
)
source_dir = File.join(picoruby_root, 'mrbgems', 'picoruby-wasm', 'npm', 'picoruby', 'dist')
target_dir = File.expand_path('picoruby', __dir__)
required = %w[init.iife.js picoruby.js picoruby.wasm]
missing = required.reject { |name| File.file?(File.join(source_dir, name)) }

unless missing.empty?
  abort <<~MESSAGE
    PicoRuby WASMの生成物が不足しています: #{missing.join(', ')}

    先に次を実行してください。
      cd #{File.expand_path('..', __dir__)}
      ruby tools/build/workflow.rb engine --wasm-only
  MESSAGE
end

FileUtils.mkdir_p(target_dir)
required.each do |name|
  source = File.join(source_dir, name)
  target = File.join(target_dir, name)
  if name == 'init.iife.js'
    # The upstream runner spends 16ms in a synchronous batch and then uses a
    # zero-delay timer. In browsers that combination produces roughly 20.8ms
    # runner periods, which turns Game.tick(60) into an accidental 48 FPS cap.
    # The emulator is a visual game target, so align scheduler turns with the
    # browser's refresh cycle instead.
    contents = File.read(source)
    timer_call = '        setTimeout(run, delay);'
    frame_call = '        requestAnimationFrame(run);'
    contents = contents.sub(timer_call, frame_call) unless contents.include?(frame_call)
    unless contents.include?(frame_call)
      abort 'PicoRuby WASM scheduler format is unsupported; expected setTimeout(run, delay)'
    end
    File.write(target, contents)
  else
    FileUtils.cp(source, target)
  end
end

revision, status = Open3.capture2('git', '-C', picoruby_root, 'rev-parse', 'HEAD')
abort 'Cannot identify the PicoRuby build revision' unless status.success?
version = File.read(File.join(picoruby_root, 'include', 'version.h'))[/PICORUBY_VERSION "([^"]+)"/, 1]
funicular = File.read(File.join(picoruby_root, 'mrbgems', 'picoruby-funicular', 'mrblib', 'version.rb'))[/VERSION = '([^']+)'/, 1]
runtime_inputs = required.map { |name| File.join(target_dir, name) } +
  %w[emulator.js game_adapter.rb].map { |name| File.join(__dir__, name) }
File.write(File.join(target_dir, 'version.json'), JSON.pretty_generate({
  picoruby: version, revision: revision.strip, funicular: funicular, bytecode: 'RITE0400',
  build: runtime_inputs.reduce(Digest::SHA256.new) { |hash, file| hash.update(File.binread(file)) }.hexdigest
}) + "\n")
puts "Copied PicoRuby WASM runtime to #{target_dir}"
