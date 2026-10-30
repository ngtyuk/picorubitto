require 'minitest/autorun'
require 'tmpdir'
require 'digest'
require_relative '../../tools/fixtures/generate_audio_checks'

module Game
  def self.tick(_fps)
    Input.advance
  end
  module Input
    class << self
      attr_accessor :events, :current
      def advance; self.current = events.shift || 256; end
      def just_pressed?(bit); (current & bit) != 0; end
    end
  end
  module Time
    class << self
      attr_accessor :now
      def millis; now; end
    end
  end
  module Assets
    def self.collect; end
  end
  module Display
    def self.clear(*); self.clears = (clears || 0) + 1; end
    def self.text(*); end
    def self.present; self.presents = (presents || 0) + 1; end
    class << self
      attr_accessor :rectangles, :presents, :clears
      def fill_rect(x, y, width, height, color)
        raise ArgumentError, 'rectangle width and height must be positive' if width <= 0 || height <= 0
        (self.rectangles ||= []) << [x, y, width, height, color]
      end
    end
  end
  module Audio
    class << self
      attr_accessor :events, :fail_path, :dial_enabled, :raw, :master
      def supported?; true; end
      def dial_supported?; dial_enabled; end
      def dial_value; raw; end
      def master_volume; master; end
      def stop; events << [:stop_all]; end
      def stop_bgm; events << [:stop_bgm]; end
    end
    class Sound
      def self.load(path)
        raise 'fixture load failure' if path == Audio.fail_path
        new(path)
      end
      def initialize(path); @path = path; end
      def play(volume); Audio.events << [:play, @path, volume]; end
      def play_bgm(volume, loop = false); Audio.events << [:play_bgm, @path, volume, loop]; end
      def stop; Audio.events << [:stop, @path]; end
    end
  end
end
require_relative '../portable/hardware_audio'

class HardwareAudioTest < Minitest::Test
  def setup
    Game::Display.rectangles = []
    Game::Display.presents = 0
    Game::Display.clears = 0
    Game::Audio.events = []
    Game::Audio.fail_path = nil
    Game::Audio.dial_enabled = true
    Game::Audio.raw = 4095
    Game::Audio.master = 255
    Game::Time.now = 100
  end

  def test_dial_sweep_playback_and_stop
    control = nil
    log, = capture_io do
      Game::Audio.raw = 0
      Game::Audio.master = 0
      control = HardwareAudio::DialProbe.new
      control.draw
      assert control.update(16, 10)
      assert_equal [:play, 'audio/beep-c5.wav', 192], Game::Audio.events.last
      Game::Audio.raw = 4095
      Game::Audio.master = 255
      assert control.update(32, 300)
      assert_equal 4, Game::Audio.events.last(4).count { |entry| entry[0] == :play }
      assert control.update(0, 4300) # Repeats all four tones.
      assert control.update(512, 4400)
      assert_equal [:play, 'audio/bgm.wav', 192], Game::Audio.events.last
      assert control.update(0, 8400)
      assert control.update(128, 8500)
      assert_equal [:stop_all], Game::Audio.events.last
      assert_equal false, control.update(256 | 16, 8600)
      control.close
    end
    assert_includes log, 'min=0 max=4095 mute_seen=true full_seen=true'
    assert_includes log, 'raw=4095 master=255'
  end

  def test_dial_bar_handles_every_master_volume_with_native_rectangle_rules
    control = nil
    capture_io { control = HardwareAudio::DialProbe.new }
    256.times do |volume|
      Game::Audio.master = volume
      Game::Audio.raw = volume * 4095 / 255
      Game::Display.rectangles.clear
      capture_io { control.sample(volume * 250) }
      control.draw
      expected = [[8, 208, 304, 8, 0xc616]]
      expected << [8, 208, 304 * volume / 255, 8, 0x2566] if volume > 0
      assert_equal expected, Game::Display.rectangles, "master volume #{volume}"
    end
  end

  def test_disabled_dial_skips_without_loading_sounds
    Game::Audio.dial_enabled = false
    Game::Audio.fail_path = 'audio/beep-c5.wav'
    assert_equal :skipped, HardwareAudio.run(:dial)
    assert_equal [[:stop_all]], Game::Audio.events
  end

  def player
    value = nil
    capture_io { value = HardwareAudio::Player.new }
    Game::Audio.events.clear
    value
  end

  def test_each_button_plays_its_own_note_or_effect
    (HardwareAudio::NOTES + HardwareAudio::EFFECTS).each do |bit, label, name|
      control = player
      Game::Audio.events.clear
      log, = capture_io { assert control.update(bit, 0) }
      assert_equal [[:play, "audio/#{name}.wav", 192]], Game::Audio.events
      assert_includes log, "button=#{label} sound=#{name}"
    end
    control = player
    capture_io { control.update(16, 0) }
    assert control.update(0, 1)
    assert_equal 1, Game::Audio.events.length # A held button creates no new edge.
  end

  def test_fast_taps_keep_music_and_never_exceed_four_requested_voices
    control = player
    capture_io do
      [1, 2, 4, 8, 512, 16, 32, 64, 16, 128, 512].each_with_index do |bit, index|
        control.update(bit, index * 10)
      end
    end
    live = []
    Game::Audio.events.each do |event|
      case event[0]
      when :stop_all then live.clear
      when :stop then live.delete(event[1])
      when :play
        refute_includes live, event[1]
        live << event[1]
        assert_operator live.length, :<=, 4
      end
    end
    assert_equal %w[audio/jump.wav audio/hit.wav audio/coin.wav].sort, live.sort
  end

  def test_bgm_toggle_stops_only_bgm_and_repeats_without_catch_up_bursts
    control = player
    capture_io { control.update(512, 10) }
    assert_equal [:play_bgm, 'audio/bgm.wav', 160, true], Game::Audio.events.last
    Game::Audio.events.clear
    control.update(0, 4009)
    assert_empty Game::Audio.events
    capture_io { control.update(0, 4010) }
    assert_empty Game::Audio.events # The engine loops without another request.
    capture_io { control.update(0, 40_000) }
    assert_empty Game::Audio.events
    Game::Audio.events.clear
    capture_io { control.update(512, 40_001) }
    assert_equal [[:stop_bgm]], Game::Audio.events
    Game::Audio.events.clear
    control.update(0, 80_000)
    assert_empty Game::Audio.events
  end

  def test_four_voice_mix_clears_previous_playback_then_requests_exactly_four
    control = player
    log, = capture_io { control.update(128 | 16 | 512, 123) }
    assert_equal [:stop_all], Game::Audio.events.first
    plays = Game::Audio.events.select { |entry| [:play, :play_bgm].include?(entry[0]) }
    assert_equal %w[audio/bgm.wav audio/jump.wav audio/hit.wav audio/coin.wav], plays.map { |entry| entry[1] }
    assert_includes log, 'requested_voices=4'
    Game::Audio.events.clear
    capture_io { control.update(16 | 32 | 64, 200) }
    assert_equal 3, Game::Audio.events.count { |entry| entry[0] == :play }
    Game::Audio.events.clear
    refute control.update(256 | 128, 201)
    assert_empty Game::Audio.events
  end

  def test_runner_uses_button_edges_and_stops_on_exit_or_error
    Game::Input.events = [1, 0, 512, 128, 256]
    log, = capture_io { assert_equal :visual, HardwareAudio.run }
    assert_includes log, 'button=UP'
    assert_includes log, 'button=SELECT'
    assert_includes log, 'button=R'
    assert_includes log, '[AUDIO CHECK] END'
    assert_equal [:stop_all], Game::Audio.events.last
    Game::Audio.events.clear
    Game::Audio.fail_path = 'audio/hit.wav'
    assert_raises(RuntimeError) { capture_io { HardwareAudio.run } }
    assert_equal [:stop_all], Game::Audio.events.last
  end

  def test_clean_mix_probe_selects_one_or_four_voices_and_bounded_volume
    control = nil
    capture_io { control = HardwareAudio::MixerProbe.new }
    [[16, 1], [32, 4], [64, 1]].each do |button, count|
      Game::Audio.events.clear
      capture_io { assert control.update(button, 0) }
      plays = Game::Audio.events.select { |entry| [:play, :play_bgm].include?(entry[0]) }
      assert_equal count, plays.length
      assert plays.all? { |entry| entry[2] == 64 }
      assert_equal 'audio/mix-reference.wav', plays[0][1] if button == 64
    end
    capture_io { 8.times { control.update(8, 0) } }
    Game::Audio.events.clear
    capture_io { control.update(32, 0) }
    assert Game::Audio.events.select { |entry| entry[0] == :play }.all? { |entry| entry[2] == 192 }
    capture_io { 8.times { control.update(4, 0) } }
    Game::Audio.events.clear
    capture_io { control.update(16, 0) }
    assert_equal [:play, 'audio/mix-tone-0.wav', 32], Game::Audio.events.last
    control.update(128, 0)
    assert_equal [:stop_all], Game::Audio.events.last
    Game::Audio.events.clear
    refute control.update(256 | 32, 0)
    assert_empty Game::Audio.events
  end

  def test_pcm_fixtures_are_distinct_reproducible_and_fit_the_audio_contract
    Dir.mktmpdir('audio-checks-') do |output|
      capture_io { AudioCheckAssets.generate(output) }
      files = Dir[File.join(output, 'audio/*.wav')].sort
      assert_equal 13, files.length
      hashes = files.map { |path| Digest::SHA256.file(path).hexdigest }
      assert_equal 13, hashes.uniq.length
      files.each do |path|
        bytes = File.binread(path)
        assert_equal 'RIFF', bytes[0, 4]
        assert_equal bytes.bytesize - 8, bytes[4, 4].unpack1('V')
        assert_equal 'WAVEfmt ', bytes[8, 8]
        assert_equal [16, 1, 1, 22_050, 22_050, 1, 8], bytes[16, 20].unpack('VvvVVvv')
        assert_equal 'data', bytes[36, 4]
        size = bytes[40, 4].unpack1('V')
        duration = File.basename(path) == 'bgm.wav' || File.basename(path).start_with?('mix-') ? 4000 : File.basename(path).start_with?('beep-') ? 600 : 900
        assert_equal 22_050 * duration / 1000, size
        assert_operator bytes.bytesize, :<=, 128 * 1024
        assert_equal 128, bytes.getbyte(44)
        assert_equal 128, bytes.getbyte(44 + size - 1)
        assert_operator bytes.byteslice(44, size).bytes.uniq.length, :>, 100
      end
      # Check actual waveform pitch, rather than only matching generator metadata.
      pcm = File.binread(File.join(output, 'audio/beep-c5.wav'))[44..].bytes
      crossings = pcm.each_cons(2).count { |left, right| left < 128 && right >= 128 }
      assert_in_delta 523.25 * 0.6, crossings, 2
      capture_io { AudioCheckAssets.generate(output) }
      assert_equal hashes, files.map { |path| Digest::SHA256.file(path).hexdigest }
    end
  end

  def test_rate_probe_changes_rate_only_after_stopping_and_never_repeats
    control = nil
    capture_io { control = HardwareAudio::RateProbe.new }
    Game::Audio.events.clear
    [[16, 'tone-48000'], [32, 'tone-22050'], [64, 'silence-48000']].each_with_index do |entry, index|
      button, name = entry
      now = index * 20_000 + 100
      capture_io { assert control.update(button, now) }
      assert_equal [[:stop_all]], Game::Audio.events
      # Neither an update nor the button's display transfer starts playback.
      assert control.update(0, now + 10_000)
      assert_equal [[:stop_all]], Game::Audio.events
      Game::Time.now = now + 10_000
      control.draw
      assert_equal [[:stop_all]], Game::Audio.events
      assert control.update(0, now + 10_199)
      assert_equal [[:stop_all]], Game::Audio.events
      log, = capture_io { assert control.update(0, now + 10_200) }
      assert_equal [[:stop_all], [:play, "audio-rate-#{name}.wav", 255]], Game::Audio.events
      assert_includes log, 'master=255'
      presents = Game::Display.presents
      5.times { control.draw }
      assert_equal presents, Game::Display.presents
      Game::Audio.events.clear
      assert control.update(0, now + 11_000)
      assert_empty Game::Audio.events
    end
    capture_io { control.update(128 | 16, 60_000) }
    assert_equal [[:stop_all]], Game::Audio.events
    Game::Audio.events.clear
    refute control.update(256 | 32, 61_000)
    assert_empty Game::Audio.events
    control.close
    assert_equal [[:stop_all]], Game::Audio.events
  end

  def test_rate_probe_screen_switch_does_not_restart_audio_and_static_stays_quiet
    control = nil
    capture_io { control = HardwareAudio::RateProbe.new; control.update(16, 100) }
    control.draw
    capture_io { control.update(0, 300) }
    Game::Audio.events.clear
    clears = Game::Display.clears
    presents = Game::Display.presents
    10.times { control.draw }
    assert_equal clears, Game::Display.clears
    assert_equal presents, Game::Display.presents
    ['LIVE', 'DRAW', 'STATIC'].each do |mode|
      log, = capture_io { assert control.update(512, 400) }
      assert_includes log, "screen=#{mode}"
      control.draw # One transfer makes the changed label visible.
      clears = Game::Display.clears
      presents = Game::Display.presents
      10.times { assert control.update(0, 500); control.draw }
      assert_equal clears + (mode == 'STATIC' ? 0 : 10), Game::Display.clears
      assert_equal presents + (mode == 'LIVE' ? 10 : 0), Game::Display.presents
      assert_empty Game::Audio.events
    end
  end

  def test_rate_probe_rearms_pending_audio_after_a_screen_transfer_and_stop_cancels
    control = nil
    capture_io { control = HardwareAudio::RateProbe.new; control.update(16, 100) }
    control.draw
    Game::Audio.events.clear
    Game::Time.now = 250
    capture_io { control.update(512, 250) }
    control.draw
    assert control.update(0, 300)
    assert_empty Game::Audio.events
    log, = capture_io { assert control.update(0, 450) }
    assert_equal [[:play, 'audio-rate-tone-48000.wav', 255]], Game::Audio.events
    assert_includes log, 'screen=LIVE'
    capture_io { control.update(32, 500) }
    control.draw
    capture_io { control.update(128 | 16, 600) }
    control.draw
    Game::Audio.events.clear
    assert control.update(0, 10_000)
    assert_empty Game::Audio.events
    capture_io { control.update(64, 11_000) }
    control.draw
    refute control.update(256 | 16, 12_000)
    control.close
    Game::Audio.events.clear
    assert control.update(0, 13_000)
    assert_empty Game::Audio.events
  end

  def test_mix_screen_toggle_leaves_the_playing_sound_and_gain_untouched
    control = nil
    capture_io { control = HardwareAudio::MixerProbe.new; control.update(16, 0) }
    Game::Audio.events.clear
    3.times { control.draw }
    assert_equal 3, Game::Display.presents
    log, = capture_io { assert control.update(512, 100) }
    assert_includes log, '[AUDIO MIX] screen=DRAW'
    assert_empty Game::Audio.events
    control.draw
    assert_equal 4, Game::Display.presents
    50.times { assert control.update(0, 200); control.draw }
    assert_equal 4, Game::Display.presents
    assert_equal 54, Game::Display.clears
    assert_empty Game::Audio.events
    capture_io { assert control.update(512, 300) }
    control.draw
    assert_equal 5, Game::Display.presents
    assert_equal 55, Game::Display.clears
    50.times { assert control.update(0, 350); control.draw }
    assert_equal 5, Game::Display.presents
    assert_equal 55, Game::Display.clears
    capture_io { assert control.update(512, 375) }
    3.times { control.draw }
    assert_equal 8, Game::Display.presents
    assert_empty Game::Audio.events
    refute control.update(256 | 512 | 16, 400)
    assert_empty Game::Audio.events
  end

  def test_dial_static_screen_keeps_sampling_and_repeat_behavior_without_extra_audio_events
    control = nil
    capture_io { control = HardwareAudio::DialProbe.new; control.update(32, 0) }
    Game::Audio.events.clear
    control.draw
    log, = capture_io { assert control.update(64, 100) }
    assert_includes log, '[DIAL] screen=DRAW'
    control.draw
    assert_equal 2, Game::Display.presents
    50.times { control.draw }
    assert_equal 52, Game::Display.clears
    assert_equal 2, Game::Display.presents
    capture_io { assert control.update(64, 200) }
    control.draw
    assert_equal 3, Game::Display.presents
    Game::Audio.raw = 2000
    Game::Audio.master = 128
    log, = capture_io { assert control.update(0, 300) }
    assert_includes log, 'raw=2000 master=128'
    50.times { control.draw }
    assert_equal 3, Game::Display.presents
    assert_equal 53, Game::Display.clears
    assert_empty Game::Audio.events
    capture_io { assert control.update(64, 350) }
    3.times { control.draw }
    assert_equal 6, Game::Display.presents
    assert_empty Game::Audio.events
    # The four-second audio repeat is independent of display selection.
    capture_io { assert control.update(0, 4000) }
    assert_equal [:stop_all], Game::Audio.events.first
    assert_equal 4, Game::Audio.events.count { |entry| entry[0] == :play }
  end

  def test_rate_probe_draws_only_when_a_selection_changes_and_cleans_up_on_load_error
    control = nil
    capture_io { control = HardwareAudio::RateProbe.new }
    control.draw
    assert_equal 1, Game::Display.presents
    100.times { control.update(0, 100); control.draw }
    assert_equal 1, Game::Display.presents
    capture_io { control.update(16, 200) }
    control.draw
    assert_equal 2, Game::Display.presents
    Game::Audio.fail_path = 'audio-rate-tone-22050.wav'
    assert_raises(RuntimeError) { capture_io { HardwareAudio.run(:rate) } }
    assert_equal [:stop_all], Game::Audio.events.last
  end

  def test_rate_fixtures_have_matching_pitch_and_level_with_true_digital_silence
    Dir.mktmpdir('audio-rate-') do |output|
      capture_io { AudioCheckAssets.generate(output) }
      paths = Dir[File.join(output, 'audio-rate-*.wav')].sort
      assert_equal 3, paths.length
      hashes = paths.map { |path| Digest::SHA256.file(path).hexdigest }
      paths.each do |path|
        bytes = File.binread(path)
        rate = File.basename(path).include?('22050') ? 22_050 : 48_000
        assert_equal [16, 1, 1, rate, rate, 1, 8], bytes[16, 20].unpack('VvvVVvv')
        assert_equal bytes.bytesize - 8, bytes[4, 4].unpack1('V')
        count = bytes[40, 4].unpack1('V')
        assert_equal rate * 2500 / 1000, count
        assert_operator bytes.bytesize, :<=, 128 * 1024
        pcm = bytes.byteslice(44, count).bytes
        assert_equal 128, pcm.first
        assert_equal 128, pcm.last
        if File.basename(path).include?('silence')
          assert_equal [128], pcm.uniq
        else
          assert_equal [96, 160], pcm.minmax
          # Compare a whole second away from the fade, including its last edge.
          steady = pcm[rate / 2, rate + 1]
          crossings = steady.each_cons(2).count { |a, b| a < 128 && b >= 128 }
          assert_in_delta 1000, crossings, 1
        end
      end
      capture_io { AudioCheckAssets.generate(output) }
      assert_equal hashes, paths.map { |path| Digest::SHA256.file(path).hexdigest }
    end
  end
end
