# frozen_string_literal: true

require 'json'
require 'open3'
require 'fileutils'

module PicoRubyGaming
  # Only project-owned checkouts are updated. Caller-supplied PICORUBY_ROOT is
  # a build input, never a target of setup.
  module Dependencies
    ROOT = File.expand_path('../..', __dir__)
    LOCK = JSON.parse(File.read(File.join(__dir__, 'dependencies.lock.json'))).freeze

    module_function

    def directory(name)
      File.join(ROOT, LOCK.fetch(name).fetch('path'))
    end

    def capture(*command)
      output, status = Open3.capture2e(*command)
      raise "#{command.join(' ')}: #{output.strip}" unless status.success?
      output.strip
    end

    def git(checkout, *args)
      capture('git', '-C', checkout, *args)
    end

    def revision(checkout)
      return nil unless File.exist?(File.join(checkout, '.git'))
      git(checkout, 'rev-parse', 'HEAD')
    end

    def setup(root: ROOT, lock: LOCK)
      lock.each do |name, entry|
        checkout = File.join(root, entry.fetch('path'))
        unless File.exist?(File.join(checkout, '.git'))
          raise "#{checkout} already exists; use an empty dependency path" if File.exist?(checkout)
          FileUtils.mkdir_p(File.dirname(checkout))
          capture('git', 'init', checkout)
          git(checkout, 'remote', 'add', 'origin', entry.fetch('url'))
        end
        remote = git(checkout, 'remote', 'get-url', 'origin').delete_suffix('.git')
        unless remote == entry.fetch('url').delete_suffix('.git')
          raise "#{checkout}: origin differs from dependencies.lock.json"
        end
        # An initial, empty checkout has no HEAD yet.
        head = begin
          git(checkout, 'rev-parse', 'HEAD')
        rescue RuntimeError
          nil
        end
        changes = git(checkout, 'status', '--porcelain')
        raise "#{checkout}: local changes found; keep them and use another checkout" unless changes.empty?
        if head != entry.fetch('revision')
          git(checkout, 'fetch', '--depth', '1', 'origin', entry.fetch('revision'))
          git(checkout, 'sparse-checkout', 'set', *entry['sparse']) if entry['sparse']
          git(checkout, 'switch', '--detach', entry.fetch('revision'))
        end
        if entry['submodules']
          git(checkout, 'submodule', 'update', '--init', '--recursive', '--depth', '1', *entry['submodules'])
        end
        (entry['components'] || {}).each do |component, version|
          actual = revision(File.join(checkout, version.fetch('path')))
          raise "#{component}: expected #{version['revision']}, got #{actual}" unless actual == version.fetch('revision')
        end
        puts "#{name}: #{entry.fetch('version')} (#{entry.fetch('revision')[0, 12]})"
      end
    end

    def report
      LOCK.each do |name, entry|
        actual = revision(directory(name))
        status = actual == entry.fetch('revision') ? 'locked' : "expected #{entry.fetch('revision')[0, 12]}"
        puts "#{name}: #{entry.fetch('version')} / #{actual ? actual[0, 12] : 'not installed'} (#{status})"
        (entry['components'] || {}).each do |component, version|
          puts "  #{component}: #{version.fetch('version')} / #{revision(File.join(directory(name), version.fetch('path')))&.slice(0, 12) || 'not installed'}"
        end
      end
    end
  end
end
