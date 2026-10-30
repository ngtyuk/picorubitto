# Button-driven listening checks; mixed voices always share a sample rate.
module HardwareAudio
  BGM_MS = 4000
  SCREEN_MODES = ['LIVE', 'DRAW', 'STATIC']
  NOTES = [[1, 'UP', 'beep-c5'], [2, 'DOWN', 'beep-d5'],
           [4, 'LEFT', 'beep-e5'], [8, 'RIGHT', 'beep-g5']]
  EFFECTS = [[16, 'A', 'jump'], [32, 'B', 'hit'], [64, 'L', 'coin']]

  class Player
    def initialize
      Game::Audio.stop
      @sounds = {}
      (NOTES + EFFECTS).each do |entry|
        @sounds[entry[2]] = Game::Audio::Sound.load('audio/' + entry[2] + '.wav')
      end
      @bgm = Game::Audio::Sound.load('audio/bgm.wav')
      @bgm_on = false
      @last = 'READY'
      @active = []
      print '[AUDIO CHECK] BEGIN rate=22050 bgm=1 se=4', "\n"
    end

    def start_bgm(_now)
      @bgm.play_bgm(160, true)
      @bgm_on = true
    end

    def play_sound(entry)
      name = entry[2]
      kept = []
      @active.each do |voice|
        if voice == name
          @sounds[name].stop
        else
          kept << voice
        end
      end
      @active = kept
      limit = 4
      while @active.length >= limit
        @sounds[@active.shift].stop
      end
      @sounds[name].play(192)
      # Keep handles until replacement: PCM scheduling can outlive a Ruby timer.
      @active << name
    end

    def update(pressed, now)
      return false if (pressed & 256) != 0 # START has priority over playback.
      if (pressed & 128) != 0
        Game::Audio.stop
        @active = []
        start_bgm(now)
        EFFECTS.each { |entry| play_sound(entry) }
        @last = '4 VOICES'
        print '[AUDIO CHECK] button=R mix=bgm,jump,hit,coin requested_voices=4', "\n"
      else
        if (pressed & 512) != 0
          if @bgm_on
            Game::Audio.stop_bgm
            @bgm_on = false
          else
            start_bgm(now)
          end
          @last = @bgm_on ? 'BGM ON' : 'BGM OFF'
          print '[AUDIO CHECK] button=SELECT bgm=', @bgm_on ? 'on' : 'off', "\n"
        end
        (NOTES + EFFECTS).each do |entry|
          next if (pressed & entry[0]) == 0
          play_sound(entry)
          @last = entry[1] + ' ' + entry[2]
          print '[AUDIO CHECK] button=', entry[1], ' sound=', entry[2], "\n"
        end
      end
      true
    end

    def draw
      Game::Display.clear(0xff9b)
      lines = ['AUDIO PAD', 'UP C5   DOWN D5', 'LEFT E5  RIGHT G5',
               'A JUMP  B HIT  L COIN', 'SELECT: BGM',
               'R: BGM AND 3 SFX', @bgm_on ? 'BGM: ON' : 'BGM: OFF', @last]
      lines.each_with_index { |line, index| Game::Display.text(8, 8 + index * 25, line, 0x4328) }
      Game::Display.text(8, 222, 'START: FINISH', 0x4328)
      Game::Display.present
    end

    def close
      Game::Audio.stop
      @bgm_on = false
      print '[AUDIO CHECK] END', "\n"
    end
  end

  class MixerProbe
    LEVELS = [32, 64, 128, 192]
    def initialize
      Game::Audio.stop
      @tones = []
      4.times { |index| @tones << Game::Audio::Sound.load('audio/mix-tone-' + index.to_s + '.wav') }
      @reference = Game::Audio::Sound.load('audio/mix-reference.wav')
      @level = 1
      @mode = 'READY'
      @screen_mode = 0
      @dirty = true
      print '[AUDIO MIX] BEGIN rate=22050 max_voices=4', "\n"
    end

    def update(pressed, _now)
      return false if (pressed & 256) != 0
      if (pressed & 512) != 0
        @screen_mode = (@screen_mode + 1) % SCREEN_MODES.length
        @dirty = true
        print '[AUDIO MIX] screen=', SCREEN_MODES[@screen_mode],
              ' master=', Game::Audio.master_volume.to_s, "\n"
      end
      @dirty = true if (pressed & (4 | 8 | 16 | 32 | 64 | 128)) != 0
      if (pressed & 4) != 0 || (pressed & 8) != 0
        @level -= 1 if (pressed & 4) != 0 && @level > 0
        @level += 1 if (pressed & 8) != 0 && @level < LEVELS.length - 1
        Game::Audio.stop
        @mode = 'READY'
        print '[AUDIO MIX] volume=', LEVELS[@level].to_s, "\n"
      end
      if (pressed & 128) != 0
        Game::Audio.stop
        @mode = 'STOP'
      elsif (pressed & (16 | 32 | 64)) != 0
        Game::Audio.stop
        if (pressed & 16) != 0
          @mode = 'SINGLE'
          @tones[0].play(LEVELS[@level])
        elsif (pressed & 32) != 0
          @mode = 'LIVE4'
          @tones.each { |sound| sound.play(LEVELS[@level]) }
        else
          @mode = 'PREMIX'
          @reference.play(LEVELS[@level])
        end
        print '[AUDIO MIX] mode=', @mode, ' volume=', LEVELS[@level].to_s,
              ' master=', Game::Audio.master_volume.to_s, "\n"
      end
      true
    end

    def draw
      return if @screen_mode == 2 && !@dirty
      Game::Display.clear(0xff9b)
      lines = ['AUDIO MIX', 'LEFT RIGHT: VOLUME', 'VOL ' + LEVELS[@level].to_s,
               'A: SINGLE TONE', 'B: LIVE 4 TONES', 'L: PREMIX 1 VOICE', 'R: STOP', @mode,
               'SEL SCREEN: ' + SCREEN_MODES[@screen_mode]]
      lines.each_with_index { |line, index| Game::Display.text(8, 8 + index * 23, line, 0x4328) }
      Game::Display.text(8, 222, 'START: FINISH', 0x4328)
      Game::Display.present if @screen_mode == 0 || @dirty
      @dirty = false
    end

    def close
      Game::Audio.stop
      print '[AUDIO MIX] END', "\n"
    end
  end

  class RateProbe
    SETTLE_MS = 200

    def initialize
      Game::Audio.stop
      @tone48 = Game::Audio::Sound.load('audio-rate-tone-48000.wav')
      @tone22 = Game::Audio::Sound.load('audio-rate-tone-22050.wav')
      @silence = Game::Audio::Sound.load('audio-rate-silence-48000.wav')
      @mode = 'READY'
      @screen_mode = 2
      @pending = nil
      @settle_until = nil
      @frame = 0
      @dirty = true
      print '[AUDIO RATE] BEGIN tone_hz=1000 duration_ms=2500 peak=32 source_volume=255 settle_ms=200 screen=STATIC', "\n"
    end

    def update(pressed, now)
      return false if (pressed & 256) != 0
      if (pressed & 512) != 0
        @screen_mode = (@screen_mode + 1) % SCREEN_MODES.length
        @dirty = true
        print '[AUDIO RATE] screen=', SCREEN_MODES[@screen_mode], "\n"
      end
      if (pressed & (16 | 32 | 64 | 128)) != 0
        # Stop before changing rates: overlap would violate the audio contract.
        Game::Audio.stop
        @pending = nil
        @settle_until = nil
        if (pressed & 128) != 0
          @mode = 'STOP'
        elsif (pressed & 16) != 0
          @mode = 'TONE 48000'
          @pending = @tone48
        elsif (pressed & 32) != 0
          @mode = 'TONE 22050'
          @pending = @tone22
        else
          @mode = 'SILENCE 48000'
          @pending = @silence
        end
        print '[AUDIO RATE] queued=', @mode, ' screen=', SCREEN_MODES[@screen_mode], "\n"
        @dirty = true
      end
      # The button label is transferred before starting audio. In STATIC this
      # leaves a 200ms margin for the asynchronous presenter to finish, with
      # no further present at playback start. This is a diagnostic margin,
      # not a hardware completion fence. A screen change while queued rearms it.
      if @pending && !@dirty && @settle_until && now >= @settle_until
        @pending.play(255)
        @pending = nil
        @settle_until = nil
        print '[AUDIO RATE] mode=', @mode, ' master=', Game::Audio.master_volume.to_s,
              ' screen=', SCREEN_MODES[@screen_mode], "\n"
      end
      true
    end

    def draw
      return if @screen_mode == 2 && !@dirty
      Game::Display.clear(0xff9b)
      lines = ['AUDIO RATE', 'A: 48 KHZ TONE', 'B: 22.05 KHZ TONE',
               'L: 48 KHZ SILENCE', 'R: STOP', 'SEL SCREEN: ' + SCREEN_MODES[@screen_mode],
               'DURATION: 2.5 SEC', @mode]
      lines.each_with_index { |line, index| Game::Display.text(8, 8 + index * 25, line, 0x4328) }
      Game::Display.fill_rect(8 + @frame % 280, 208, 24, 8, 0x2566)
      @frame += 5
      Game::Display.text(8, 222, 'START: FINISH', 0x4328)
      if @screen_mode == 0 || @dirty
        Game::Display.present
        @settle_until = Game::Time.millis + SETTLE_MS if @pending && @dirty
      end
      @dirty = false
    end

    def close
      @pending = nil
      @settle_until = nil
      Game::Audio.stop
      print '[AUDIO RATE] END', "\n"
    end
  end

  class DialProbe
    def initialize
      Game::Audio.stop
      @note = Game::Audio::Sound.load('audio/beep-c5.wav')
      @bgm = Game::Audio::Sound.load('audio/bgm.wav')
      @tones = []
      4.times { |index| @tones << Game::Audio::Sound.load('audio/mix-tone-' + index.to_s + '.wav') }
      @mode = 'READY'
      @screen_mode = 0
      @dirty = true
      @minimum = 4095
      @maximum = 0
      @muted = false
      @full = false
      @logged_at = -250
      print '[DIAL] BEGIN raw_range=0..4095 master_range=0..255', "\n"
      sample(0)
    end

    def sample(now)
      @raw = Game::Audio.dial_value
      @volume = Game::Audio.master_volume
      @minimum = @raw if @raw < @minimum
      @maximum = @raw if @raw > @maximum
      @muted = true if @volume == 0
      @full = true if @volume == 255
      if now - @logged_at >= 250
        @logged_at = now
        print '[DIAL] raw=', @raw.to_s, ' master=', @volume.to_s,
              ' min=', @minimum.to_s, ' max=', @maximum.to_s, "\n"
      end
    end

    def play_mode(now)
      Game::Audio.stop
      if @mode == '4 TONES'
        @tones.each { |tone| tone.play(192) }
      elsif @mode == 'BGM'
        @bgm.play(192)
      elsif @mode == 'BEEP'
        @note.play(192)
      end
      @repeat_at = now + BGM_MS
    end

    def update(pressed, now)
      sample(now)
      return false if (pressed & 256) != 0
      if (pressed & 64) != 0
        @screen_mode = (@screen_mode + 1) % SCREEN_MODES.length
        @dirty = true
        print '[DIAL] screen=', SCREEN_MODES[@screen_mode], "\n"
      end
      if (pressed & 128) != 0
        @mode = 'STOP'
      elsif (pressed & 512) != 0
        @mode = @mode == 'BGM' ? 'STOP' : 'BGM'
      elsif (pressed & 32) != 0
        @mode = '4 TONES'
      elsif (pressed & 16) != 0
        @mode = 'BEEP'
      end
      if (pressed & (16 | 32 | 128 | 512)) != 0
        @dirty = true
        play_mode(now)
        print '[DIAL] mode=', @mode, "\n"
      elsif (@mode == '4 TONES' || @mode == 'BGM') && now >= @repeat_at
        play_mode(now)
      end
      true
    end

    def draw
      return if @screen_mode == 2 && !@dirty
      Game::Display.clear(0xff9b)
      lines = ['AUDIO DIAL', 'RAW: ' + @raw.to_s + ' / 4095',
               'VOLUME: ' + @volume.to_s + ' / 255',
               'MIN: ' + @minimum.to_s + ' MAX: ' + @maximum.to_s,
               'L SCREEN: ' + SCREEN_MODES[@screen_mode],
               'A BEEP  B 4 TONES', 'SELECT BGM  R STOP', @mode]
      lines.each_with_index { |line, index| Game::Display.text(8, 8 + index * 25, line, 0x4328) }
      Game::Display.fill_rect(8, 208, 304, 8, 0xc616)
      bar_width = 304 * @volume / 255
      Game::Display.fill_rect(8, 208, bar_width, 8, 0x2566) if bar_width > 0
      Game::Display.text(8, 222, 'START: FINISH', 0x4328)
      Game::Display.present if @screen_mode == 0 || @dirty
      @dirty = false
    end

    def close
      Game::Audio.stop
      print '[DIAL] END min=', @minimum.to_s, ' max=', @maximum.to_s,
            ' mute_seen=', @muted.to_s, ' full_seen=', @full.to_s, "\n"
    end
  end

  def self.run(kind = :pad)
    raise 'audio capability' unless Game::Audio.supported?
    return :skipped if kind == :dial && !Game::Audio.dial_supported?
    player = case kind
             when :mix then MixerProbe.new
             when :dial then DialProbe.new
             when :rate then RateProbe.new
             else Player.new
             end
    player.draw
    while true
      Game.tick(30)
      pressed = 0
      10.times do |index|
        bit = 1 << index
        pressed |= bit if Game::Input.just_pressed?(bit)
      end
      break unless player.update(pressed, Game::Time.millis)
      player.draw
    end
    :visual
  ensure
    Game::Audio.stop
    player.close if player
    player = nil
    Game::Assets.collect
  end
end
