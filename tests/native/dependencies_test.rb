require 'minitest/autorun'
require 'tmpdir'
require 'fileutils'
require_relative '../../tools/build/dependencies'

class DependenciesTest < Minitest::Test
  Dependencies = PicoRubyGaming::Dependencies

  def setup
    @temporary = Dir.mktmpdir('gaming-dependencies-')
    @remote = File.join(@temporary, 'upstream')
    Dependencies.capture('git', 'init', @remote)
    git_remote('config', 'user.name', 'Dependency Test')
    git_remote('config', 'user.email', 'dependency-test@example.invalid')
    File.write(File.join(@remote, 'source.txt'), 'release one')
    git_remote('add', 'source.txt')
    git_remote('commit', '-m', 'first release')
    @first = git_remote('rev-parse', 'HEAD')
    File.write(File.join(@remote, 'source.txt'), 'release two')
    git_remote('commit', '-am', 'second release')
    @second = git_remote('rev-parse', 'HEAD')
    @checkout = File.join(@temporary, 'deps', 'runtime')
  end

  def teardown
    FileUtils.remove_entry(@temporary)
  end

  def git_remote(*args)
    Dependencies.git(@remote, *args)
  end

  def install(revision = @first, url: @remote)
    lock = { 'runtime' => { 'version' => 'test', 'path' => 'deps/runtime', 'url' => url, 'revision' => revision } }
    capture_io { Dependencies.setup(root: @temporary, lock: lock) }
  end

  def test_initial_setup_is_pinned_and_repeatable
    install
    assert_equal @first, Dependencies.revision(@checkout)
    install
    assert_equal 'release one', File.read(File.join(@checkout, 'source.txt'))
  end

  def test_clean_checkout_can_advance_to_a_new_pin
    install
    install(@second)
    assert_equal @second, Dependencies.revision(@checkout)
    assert_equal 'release two', File.read(File.join(@checkout, 'source.txt'))
  end

  def test_local_changes_are_preserved_when_an_update_is_requested
    install
    File.write(File.join(@checkout, 'source.txt'), 'local work')
    assert_raises(RuntimeError) { install(@second) }
    assert_equal @first, Dependencies.revision(@checkout)
    assert_equal 'local work', File.read(File.join(@checkout, 'source.txt'))
  end

  def test_existing_non_repository_directory_is_preserved
    FileUtils.mkdir_p(@checkout)
    File.write(File.join(@checkout, 'notes.txt'), 'keep me')
    assert_raises(RuntimeError) { install }
    assert_equal 'keep me', File.read(File.join(@checkout, 'notes.txt'))
  end

  def test_unexpected_remote_is_preserved
    install
    Dependencies.git(@checkout, 'remote', 'set-url', 'origin', '/some/other/repository')
    assert_raises(RuntimeError) { install(@second) }
    assert_equal @first, Dependencies.revision(@checkout)
  end
end
