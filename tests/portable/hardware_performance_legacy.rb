# Optional v7 array workload, bundled only by --compare-3d.
module HardwarePerformance
  class Flight
    def initialize
      @stars = Array.new(24 * 3, 0.0)
      @star_edges = []
      24.times { |index| @star_edges << index << index }
      @tunnel = Array.new(4 * 8 * 3, 0.0)
      @tunnel_edges = []
      HardwareFlight.build_repeated_edges(@tunnel_edges, HardwareFlight::BOX_EDGES, 8, 4)
      @rocks = Array.new(3 * 8 * 3, 0.0)
      @indices = []
      @colors = []
      3.times do |index|
        HardwareFlight::ROCK_TRIANGLES.each { |vertex| @indices << vertex + index * 8 }
        HardwareFlight::ROCK_FACE_COLORS.each { |color| @colors << color << color }
      end
      @fighters = Array.new(8 * 9 * 3, 0.0)
      @fighter_edges = []
      HardwareFlight.build_repeated_edges(@fighter_edges, HardwareFlight::FIGHTER_EDGES, 9, 8)
      @shots = Array.new((8 * 6 + 9) * 3, 0.0)
      @shot_edges = []
      HardwareFlight.build_repeated_edges(@shot_edges, HardwareFlight::SHOT_EDGES, 6, 8)
      HardwareFlight.build_repeated_edges(@shot_edges, HardwareFlight::FIGHTER_EDGES, 9, 1, 48, @shot_edges.length)
      @sound = Game::Audio::Sound.load('beep.wav')
    end

    def configure(width, _background)
      Game::ThreeD.set_viewport(0, 0, 320, 240)
      Game::ThreeD.set_resolution(width, width * 3 / 4)
    end

    def update(frame)
      @camera_x = Math.sin(frame * 0.031) * 1.8
      @camera_y = Math.sin(frame * 0.019) * 0.8
      24.times do |index|
        @stars[index * 3] = ((index * 37) % 100 - 50) / 8.0
        @stars[index * 3 + 1] = ((index * 61) % 70 - 35) / 8.0
        @stars[index * 3 + 2] = 3.0 + ((index * 29 - frame) % 330) / 10.0
      end
      offset = 0
      4.times do |index|
        z = 6.0 + (index * 80 - frame * 2) % 320 / 10.0
        offset = HardwareFlight.append_model_vertices(@tunnel, offset, HardwareFlight::BOX_VERTICES,
          6.0, 4.0, 0.08, 0.0, 0.0, 0.0, 0.0, z)
      end
      offset = 0
      HardwareFlight::ROCK_STARTS.each_with_index do |start, index|
        z = 6.0 + ((start[2] * 10).to_i - frame * 2) % 420 / 10.0
        offset = HardwareFlight.append_transformed_vertices(@rocks, offset, HardwareFlight::BOX_VERTICES,
          start[3], start[4], start[5], start[6] + frame * 0.013, 0.18,
          start[0], start[1], z)
      end
      offset = 0
      HardwareFlight::ENEMY_STARTS.each_with_index do |start, index|
        scale = 0.62 + index % 3 * 0.08
        z = 7.0 + ((start[2] * 10).to_i - frame * 3) % 550 / 10.0
        offset = HardwareFlight.append_model_vertices(@fighters, offset, HardwareFlight::FIGHTER_VERTICES,
          scale, scale, scale, Math::PI + Math.sin(frame * 0.035 + start[3]) * 0.35,
          Math.sin(frame * 0.022 + start[3]) * 0.18, start[0], start[1], z)
      end
      offset = 0
      8.times do |index|
        z = 4.0 + (frame * 4 + index * 45) % 400 / 10.0
        offset = HardwareFlight.append_model_vertices(@shots, offset, HardwareFlight::SHOT_VERTICES,
          1.0, 1.0, 1.0, 0.0, 0.0, @camera_x, @camera_y - 0.7, z)
      end
      HardwareFlight.append_model_vertices(@shots, offset, HardwareFlight::FIGHTER_VERTICES,
        0.62, 0.62, 0.62, 0.0, 0.0, @camera_x, @camera_y - 1.35, 3.4)
      @sound.play(64) if frame % 60 == 0
    end

    def draw(label, frame, round)
      Game::Display.begin_frame
      Game::Display.clear(0x0000)
      Game::ThreeD.set_camera(@camera_x, @camera_y, 0.0, 0.0, 0.0, 70, 0.1, 80)
      Game::ThreeD.begin_frame
      Game::ThreeD.draw_wireframe(@stars, @star_edges, 0x7bef, true)
      Game::ThreeD.draw_wireframe(@tunnel, @tunnel_edges, 0x03e0, true)
      Game::ThreeD.draw_indexed_mesh(@rocks, @indices, @colors)
      Game::ThreeD.draw_wireframe(@fighters, @fighter_edges, 0xff20, true)
      Game::ThreeD.draw_wireframe(@shots, @shot_edges, 0x07ff, true)
      Game::Display.draw_line(148, 120, 172, 120, 3, 0xffff)
      Game::Display.draw_line(160, 112, 160, 128, 3, 0xffff)
      Game::Display.text(8, 8, label + ' R' + round.to_s, 0xffff)
      Game::Display.text(8, 24, 'FRAME ' + (frame + 1).to_s + '  B: MENU', 0xffff)
    end
  end

end
