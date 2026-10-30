# Ruby-only kernels: timing excludes display, audio, validation and Game.tick.
# Normal GC remains enabled inside work; gc_ms measures a separate explicit GC.
module HardwareVMPerformance
  VERSION = 2
  WARMUP = 30
  SAMPLES = 120
  ROUNDS = 3
  CASES = ['VM-NUM', 'VM-ARRAY', 'VM-STRING']
  EXPECTED = [125361, 404, 1178]
  @results = []

  def self.work(kind)
    case kind
    when 0
      value = 7
      sum = 0
      i = 0
      while i < 256
        value = (value * 1103515245 + 12345) & 0x7fffffff
        sum += (value >> 8) % 997
        i += 1
      end
      sum
    when 1
      sum = 0
      round = 0
      # v1's 16 passes took about 0.5s per sample on RP2350.
      # Keep 120 samples and 3 rounds without repeating this work 16 times.
      while round < 1
        values = Array.new(32) { |i| (i * 37 + round * 11) % 101 }
        mapped = values.map { |n| (n * n + 3) % 257 }.sort
        lookup = {}
        mapped.each_with_index { |n, i| lookup[i] = n }
        sum += lookup[0] + lookup[31] + mapped.reverse[7]
        round += 1
      end
      sum
    when 2
      sum = 0
      round = 0
      while round < 24
        text = 'pico,ruby,game,' + round.to_s + ',0123456789'
        pieces = text.split(',')
        joined = pieces.reverse.join(':')
        sum += joined.length + joined[0, 10].length + pieces[3].to_i
        round += 1
      end
      sum
    else
      raise ArgumentError, 'unknown VM workload'
    end
  end

  def self.run
    @results = []
    Game::Audio.stop
    Game::Display.set_background(nil)
    Game::Assets.collect
    sound = Game::Audio::Sound.load('beep.wav')
    print "[VM PERF] BEGIN v=#{VERSION} backend=#{Game::Runtime.backend} api=#{Game::Runtime.api_version} warmup=#{WARMUP} samples=#{SAMPLES} rounds=#{ROUNDS} normal_gc=on timing=ruby_work_only tick=outside audio=one_voice_per_60_samples input=B_cancel", "\n"
    kind = 0
    while kind < CASES.length
      means = []
      round = 0
      while round < ROUNDS
        samples = Array.new(SAMPLES, 0)
        Game::Assets.collect
        sample = 0
        while sample < WARMUP + SAMPLES
          started = Game::Time.millis
          checksum = work(kind)
          finished = Game::Time.millis
          raise 'VM workload checksum mismatch' unless checksum == EXPECTED[kind]
          samples[sample - WARMUP] = HardwarePerformance.elapsed(started, finished) if sample >= WARMUP
          if sample % 30 == 0
            Game::Display.clear(0xff9b)
            Game::Display.text(8, 8, 'PERF / VM', 0x4328)
            Game::Display.text(8, 60, CASES[kind], 0x4328)
            Game::Display.text(8, 90, "ROUND #{round + 1}/#{ROUNDS}", 0x4328)
            Game::Display.text(8, 222, 'B: MENU', 0x4328)
            Game::Display.present
          end
          sound.play(160) if sample % 60 == 0
          Game.tick(60)
          if Game::Input.just_pressed?(Game::Input::B)
            print "[VM PERF] CANCEL scene=#{CASES[kind]} round=#{round + 1}", "\n"
            return :cancelled
          end
          sample += 1
        end
        started = Game::Time.millis
        GC.start
        gc_ms = HardwarePerformance.elapsed(started, Game::Time.millis)
        stats = HardwarePerformance.stats(samples)
        means << stats[0]
        print "[VM PERF] v=#{VERSION} scene=#{CASES[kind]} round=#{round + 1} samples=#{SAMPLES} work_ms_mean=#{HardwarePerformance.decimal(stats[0])} median=#{stats[1]} p95=#{stats[2]} max=#{stats[3]} checksum=#{EXPECTED[kind]} gc_ms=#{gc_ms} resident_bytes=#{Game::Assets.resident_bytes}", "\n"
        round += 1
      end
      means.sort!
      @results << [CASES[kind], means[ROUNDS / 2]]
      kind += 1
    end
    :measured
  ensure
    Game::Audio.stop
  end

  def self.summary
    Game::Display.text(188, 52, 'MS/WORK', 0x4328)
    @results.each_with_index do |result, index|
      Game::Display.text(8, 80 + index * 28, result[0], 0x4328)
      Game::Display.text(188, 80 + index * 28, HardwarePerformance.decimal(result[1]), 0x2566)
    end
  end
end
