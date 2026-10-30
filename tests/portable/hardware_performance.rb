# Fixed game workloads. Only Game APIs are used by the cartridge.
module HardwarePerformance
  VERSION = 5
  PROFILE = defined?(HARDWARE_PERFORMANCE_PROFILE) ? HARDWARE_PERFORMANCE_PROFILE : 'standard'
  WARMUP = 30
  FRAMES = 120
  ROUNDS = 3
  FPS = 60
  @results = []

  def self.elapsed(started, finished)
    (finished - started) & 0xffffffff
  end

  def self.decimal(value)
    (value / 100).to_s + '.' + (value % 100 < 10 ? '0' : '') + (value % 100).to_s
  end

  def self.stats(samples)
    raise ArgumentError, 'empty performance samples' if samples.empty?
    sorted = samples.sort
    length = sorted.length
    total = 0
    sorted.each { |value| total += value }
    [total * 100 / length, (sorted[(length - 1) / 2] + sorted[length / 2]) / 2,
     sorted[(length * 95 + 99) / 100 - 1], sorted[length - 1]]
  end

  def self.measure(scene, label, round)
    updates = Array.new(FRAMES, 0)
    draws = Array.new(FRAMES, 0)
    presents = Array.new(FRAMES, 0)
    frames = Array.new(FRAMES, 0)
    # Collect and allocate outside the timed region; normal workload GC remains enabled.
    Game::Assets.collect
    # C-backed iterators add nested VM frames on RP2350. Keep orchestration
    # loops in Ruby so game transforms can use their usual block helpers.
    frame = 0
    while frame < WARMUP
      scene.update(frame)
      scene.draw(label, frame, round)
      Game::Display.present
      Game.tick(FPS)
      return nil if Game::Input.just_pressed?(Game::Input::B)
      frame += 1
    end
    started = Game::Time.millis
    previous = started
    frame = 0
    while frame < FRAMES
      begin_update = Game::Time.millis
      scene.update(frame)
      begin_draw = Game::Time.millis
      scene.draw(label, frame, round)
      begin_present = Game::Time.millis
      Game::Display.present
      after_present = Game::Time.millis
      Game.tick(FPS)
      finished = Game::Time.millis
      updates[frame] = elapsed(begin_update, begin_draw)
      draws[frame] = elapsed(begin_draw, begin_present)
      presents[frame] = elapsed(begin_present, after_present)
      frames[frame] = elapsed(previous, finished)
      previous = finished
      return nil if Game::Input.just_pressed?(Game::Input::B)
      frame += 1
    end
    duration = elapsed(started, previous)
    raise 'performance clock did not advance' if duration == 0
    frame_stats = stats(frames)
    print "[PERF] v=#{VERSION} scene=#{label} round=#{round} frames=#{FRAMES} elapsed_ms=#{duration} fps=#{decimal(FRAMES * 100000 / duration)}", "\n"
    print "[PERF] scene=#{label} round=#{round} frame_ms_mean=#{decimal(frame_stats[0])} median=#{frame_stats[1]} p95=#{frame_stats[2]} max=#{frame_stats[3]}", "\n"
    print "[PERF] scene=#{label} round=#{round} update_ms_mean=#{decimal(stats(updates)[0])} draw_ms_mean=#{decimal(stats(draws)[0])} present_ms_mean=#{decimal(stats(presents)[0])} resident_bytes=#{Game::Assets.resident_bytes}", "\n"
    FRAMES * 100000 / duration
  end

  def self.run(scene, cases)
    @results = []
    print "[PERF] BEGIN v=#{VERSION} profile=#{PROFILE} backend=#{Game::Runtime.backend} api=#{Game::Runtime.api_version} target_fps=#{FPS} warmup=#{WARMUP} frames=#{FRAMES} rounds=#{ROUNDS} audio=one_voice_per_60_frames input=B_cancel", "\n"
    print "[PERF] flight_source_sha256=#{HardwareFlight::SOURCE_SHA256}", "\n"
    case_index = 0
    while case_index < cases.length
      label, value, background = cases[case_index]
      scene.configure(value, background)
      rates = []
      round = 0
      while round < ROUNDS
        result = measure(scene, label, round + 1)
        if result.nil?
          print "[PERF] CANCEL scene=#{label} round=#{round + 1}", "\n"
          return :cancelled
        end
        rates << result
        round += 1
      end
      rates.sort!
      @results << [label, rates[ROUNDS / 2]]
      case_index += 1
    end
    :measured
  end

  def self.summary
    @results.each_with_index do |result, index|
      Game::Display.text(8, 80 + index * 28, result[0], 0x4328)
      Game::Display.text(188, 80 + index * 28, decimal(result[1]), 0x2566)
    end
    Game::Display.text(188, 52, 'FPS', 0x4328)
  end

  def self.run_2d
    Game::Audio.stop
    Game::Display.set_background(nil)
    Game::Assets.collect
    scene = Sprites.new
    run(scene, [['2D-32', 32, false], ['2D-128', 128, false],
                ['2D-256', 256, false], ['2D-256-BG', 256, true]])
  ensure
    Game::Audio.stop
    Game::Display.set_background(nil)
  end

  def self.run_3d
    Game::Audio.stop
    Game::Display.set_background(nil)
    Game::Assets.collect
    scene = FlightCases.new
    cases = if PROFILE == 'comparison'
              [['3D-160', 160, false], ['3D-M160', 160, true],
               ['3D-320', 320, false], ['3D-M320', 320, true]]
            else
              [['3D-M160', 160, true], ['3D-M320', 320, true]]
            end
    run(scene, cases)
  ensure
    Game::Audio.stop
    Game::Display.present
    Game::ThreeD.set_viewport(0, 0, 320, 240)
    Game::ThreeD.set_resolution(160, 120)
  end

  class Sprites
    def initialize
      @sprites = []
      @batches = []
      24.times do |index|
        @sprites << Game::Sprite.load('perf/sprite-' + index.to_s + '.raw', 32, 32, 2)
        @batches << []
      end
      @background = Game::Assets.read('perf/background.raw')
      @sound = Game::Audio::Sound.load('beep.wav')
    end

    def configure(count, background)
      @count = count
      @restore = background
      Game::Display.set_background(background ? @background : nil)
    end

    def update(frame)
      @batches.each { |batch| batch.clear }
      @count.times do |index|
        x = (index * 37 + frame * (1 + index % 3)) % 352 - 32
        y = 42 + (index * 23 + frame * (1 + index % 2)) % 156
        @batches[index % 24] << x << y << ((frame / 8 + index) % 2)
      end
      @sound.play(64) if frame % 60 == 0
    end

    def draw(label, frame, round)
      Game::Display.begin_frame
      Game::Display.clear(0x0842) unless @restore
      @sprites.each_with_index do |sprite, index|
        sprite.draw_batch(@batches[index]) unless @batches[index].empty?
      end
      Game::Display.fill_rect(0, 0, 320, 40, 0x0842)
      Game::Display.text(8, 8, label + ' R' + round.to_s, 0xffff)
      Game::Display.text(8, 24, 'FRAME ' + (frame + 1).to_s + '  B: MENU', 0xffff)
    end
  end

  def self.run_renderer
    Game::Audio.stop
    Game::Display.set_background(nil)
    Game::Assets.collect
    run(Renderer.new, [['SHR-160', 160, false], ['SHR-320', 320, false],
                       ['CLIP160', 160, true], ['CLIP320', 320, true]])
  ensure
    Game::Audio.stop
    Game::Display.present
    Game::ThreeD.set_viewport(0, 0, 320, 240)
    Game::ThreeD.set_resolution(160, 120)
  end

  def self.run_triangles
    Game::Audio.stop
    Game::Display.set_background(nil)
    Game::Assets.collect
    run(Triangles.new, [['TRI-SM', 320, 0], ['TRI-SL', 320, 1],
                        ['TRI-L160', 160, 2], ['TRI-L320', 320, 2]])
  ensure
    Game::Audio.stop
    Game::Display.present
    Game::ThreeD.set_viewport(0, 0, 320, 240)
    Game::ThreeD.set_resolution(160, 120)
  end

  # Few retained draw calls keep Ruby dispatch out of this raster comparison.
  class Triangles
    def initialize
      vertices = []
      faces = []
      colors = []
      64.times do |i|
        x = -3.5 + (i % 8) * 0.9
        y = -2.5 + (i / 8) * 0.65
        vertices << x << y << 0 << x + 0.18 << y << 0 << x + 0.18 << y + 0.18 << 0
        faces << i * 3 << i * 3 + 1 << i * 3 + 2
        colors << (i.even? ? 0x07e0 : 0xffe0)
      end
      @small = Game::ThreeD::Model.new(vertices, faces, colors)
      vertices = []
      2.times do |i|
        offset = (i - 0.5) * 0.12
        vertices << -4 << -3 + offset << 0 << 4 << 3 + offset << 0 << 4 << 3.025 + offset << 0
      end
      @sliver = Game::ThreeD::Model.new(vertices, [0,1,2,3,4,5], [0xffff,0xffe0])
      @large = Game::ThreeD::Model.new([-4,-3,0,4,-3,0,4,3,0,-4,3,0],
        [0,1,2,0,2,3], [0x07e0,0x001f])
      @pose = [0.0,0.0,4.0,0.0,0.0,0.0,1.0,1.0,1.0]
      @sound = Game::Audio::Sound.load('beep.wav')
    end
    def configure(width, kind)
      @model = kind == 0 ? @small : kind == 1 ? @sliver : @large
      Game::ThreeD.set_viewport(0, 0, 320, 240)
      Game::ThreeD.set_resolution(width, width * 3 / 4)
    end
    def update(frame)
      @pose[0] = (frame % 20 - 10) * 0.002
      @sound.play(64) if frame % 60 == 0
    end
    def draw(label, frame, round)
      Game::Display.clear(0x0842)
      Game::ThreeD.set_camera(0,0,0,0,0,70,0.1,100)
      Game::ThreeD.begin_frame
      @model.draw_instances(@pose)
      Game::Display.text(8,8,label + ' R' + round.to_s,0xffff)
      Game::Display.text(8,24,'FRAME ' + (frame+1).to_s + '  B: MENU',0xffff)
    end
  end

  # A shared-vertex mesh kept inside the screen, and eight long crossing lines.
  # The two scenes separate projection reuse from offscreen line traversal.
  class Renderer
    def initialize
      vertices = []
      indices = []
      colors = []
      edges = []
      8.times do |row|
        latitude = -1.25 + row * 2.5 / 7
        16.times do |column|
          longitude = column * Math::PI / 8
          vertices << Math.cos(latitude) * Math.cos(longitude)
          vertices << Math.sin(latitude)
          vertices << Math.cos(latitude) * Math.sin(longitude)
          index = row * 16 + column
          edges << index << row * 16 + (column + 1) % 16
          if row < 7
            below = index + 16
            right = row * 16 + (column + 1) % 16
            indices << index << right << below
            indices << right << right + 16 << below
            colors << (row.even? ? 0x03e0 : 0x041f) << (row.even? ? 0x03e0 : 0x041f)
            edges << index << below
          end
        end
      end
      @shared = Game::ThreeD::Model.new(vertices, indices, colors, edges, 0xffff)
      @long = Game::ThreeD::Model.new([
        -20, -0.6, 0, 20, -0.6, 0, -20, 0.6, 0, 20, 0.6, 0,
        -0.6, -20, 0, -0.6, 20, 0, 0.6, -20, 0, 0.6, 20, 0,
        -20, -20, 0, 20, 20, 0, -20, 20, 0, 20, -20, 0,
        -20, -9, 0, 20, 9, 0, -9, -20, 0, 9, 20, 0
      ], [], [], [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15], 0xffe0)
      @pose = [0.0, 0.0, 4.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0]
      @sound = Game::Audio::Sound.load('beep.wav')
    end
    def configure(width, long_lines)
      @long_lines = long_lines
      Game::ThreeD.set_viewport(0, 0, 320, 240)
      Game::ThreeD.set_resolution(width, width * 3 / 4)
    end
    def update(frame)
      @pose[0] = 0.0
      @pose[1] = 0.0
      @pose[2] = @long_lines ? 0.8 + (frame % 30) * 0.02 : 4.0
      @pose[3] = @long_lines ? 0.0 : frame * 0.013
      @pose[4] = @long_lines ? 0.0 : frame * 0.007
      @sound.play(64) if frame % 60 == 0
    end
    def draw(label, frame, round)
      Game::Display.clear(0x0842)
      Game::ThreeD.set_camera(0,0,0,0,0,70,0.1,100)
      Game::ThreeD.begin_frame
      (@long_lines ? @long : @shared).draw_instances(@pose)
      Game::Display.text(8,8,label + ' R' + round.to_s,0xffff)
      Game::Display.text(8,24,'FRAME ' + (frame+1).to_s + '  B: MENU',0xffff)
    end
  end

  # Construct/collect each path outside timing so inactive models do not alter
  # the baseline's allocation and retained-memory conditions.
  class FlightCases
    def configure(width, retained)
      @scene = nil
      Game::Assets.collect
      @scene = retained ? FlightModels.new : Flight.new
      @scene.configure(width, false)
    end
    def update(frame); @scene.update(frame); end
    def draw(label, frame, round); @scene.draw(label, frame, round); end
  end

  class FlightModels
    def configure(width, _background)
      Game::ThreeD.set_viewport(0, 0, 320, 240)
      Game::ThreeD.set_resolution(width, width * 3 / 4)
    end

    def initialize
      @stars = Array.new(24 * 3, 0.0)
      @star_edges = []
      24.times { |index| @star_edges << index << index }
      @tunnel_model = Game::ThreeD::Model.new(HardwareFlight::BOX_VERTICES.flatten,
        [], [], HardwareFlight::BOX_EDGES, 0x03e0)
      colors = []
      HardwareFlight::ROCK_FACE_COLORS.each { |color| colors << color << color }
      @rock_model = Game::ThreeD::Model.new(HardwareFlight::BOX_VERTICES.flatten,
        HardwareFlight::ROCK_TRIANGLES, colors)
      @fighter_model = Game::ThreeD::Model.new(HardwareFlight::FIGHTER_VERTICES.flatten,
        [], [], HardwareFlight::FIGHTER_EDGES, 0xff20)
      @player_model = Game::ThreeD::Model.new(HardwareFlight::FIGHTER_VERTICES.flatten,
        [], [], HardwareFlight::FIGHTER_EDGES, 0x07ff)
      @shot_model = Game::ThreeD::Model.new(HardwareFlight::SHOT_VERTICES.flatten,
        [], [], HardwareFlight::SHOT_EDGES, 0x07ff)
      @tunnel = Array.new(4 * 9, 0.0)
      @rocks = Array.new(3 * 9, 0.0)
      @fighters = Array.new(8 * 9, 0.0)
      @shots = Array.new(8 * 9, 0.0)
      @player = Array.new(9, 0.0)
      @sound = Game::Audio::Sound.load('beep.wav')
    end

    def pose(buffer, index, x, y, z, yaw, pitch, sx, sy, sz)
      offset = index * 9
      buffer[offset] = x
      buffer[offset+1] = y
      buffer[offset+2] = z
      buffer[offset+3] = yaw
      buffer[offset+4] = pitch
      buffer[offset+5] = 0.0
      buffer[offset+6] = sx
      buffer[offset+7] = sy
      buffer[offset+8] = sz
    end

    def update(frame)
      @camera_x = Math.sin(frame * 0.031) * 1.8
      @camera_y = Math.sin(frame * 0.019) * 0.8
      24.times do |index|
        @stars[index * 3] = ((index * 37) % 100 - 50) / 8.0
        @stars[index * 3 + 1] = ((index * 61) % 70 - 35) / 8.0
        @stars[index * 3 + 2] = 3.0 + ((index * 29 - frame) % 330) / 10.0
      end
      4.times do |index|
        z = 6.0 + (index * 80 - frame * 2) % 320 / 10.0
        pose(@tunnel, index, 0.0, 0.0, z, 0.0, 0.0, 6.0, 4.0, 0.08)
      end
      HardwareFlight::ROCK_STARTS.each_with_index do |start, index|
        z = 6.0 + ((start[2] * 10).to_i - frame * 2) % 420 / 10.0
        pose(@rocks, index, start[0], start[1], z, start[6] + frame * 0.013,
          0.18, start[3], start[4], start[5])
      end
      HardwareFlight::ENEMY_STARTS.each_with_index do |start, index|
        scale = 0.62 + index % 3 * 0.08
        z = 7.0 + ((start[2] * 10).to_i - frame * 3) % 550 / 10.0
        pose(@fighters, index, start[0], start[1], z,
          Math::PI + Math.sin(frame * 0.035 + start[3]) * 0.35,
          Math.sin(frame * 0.022 + start[3]) * 0.18, scale, scale, scale)
      end
      8.times do |index|
        z = 4.0 + (frame * 4 + index * 45) % 400 / 10.0
        pose(@shots, index, @camera_x, @camera_y - 0.7, z, 0.0, 0.0, 1.0, 1.0, 1.0)
      end
      pose(@player, 0, @camera_x, @camera_y - 1.35, 3.4, 0.0, 0.0, 0.62, 0.62, 0.62)
      @sound.play(64) if frame % 60 == 0
    end

    def draw(label, frame, round)
      Game::Display.begin_frame
      Game::Display.clear(0x0000)
      Game::ThreeD.set_camera(@camera_x, @camera_y, 0.0, 0.0, 0.0, 70, 0.1, 80)
      Game::ThreeD.begin_frame
      Game::ThreeD.draw_wireframe(@stars, @star_edges, 0x7bef, true)
      @tunnel_model.draw_instances(@tunnel)
      @rock_model.draw_instances(@rocks)
      @fighter_model.draw_instances(@fighters)
      @shot_model.draw_instances(@shots)
      @player_model.draw_instances(@player)
      Game::Display.draw_line(148, 120, 172, 120, 3, 0xffff)
      Game::Display.draw_line(160, 112, 160, 128, 3, 0xffff)
      Game::Display.text(8, 8, label + ' R' + round.to_s, 0xffff)
      Game::Display.text(8, 24, 'FRAME ' + (frame + 1).to_s + '  B: MENU', 0xffff)
    end
  end
end
