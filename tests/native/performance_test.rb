require 'minitest/autorun'
require_relative '../../tools/build/checks_source'
eval(GamingCheckSource.flight, TOPLEVEL_BINDING, 'flight-fixture.rb')
require_relative '../portable/hardware_performance'
require_relative '../portable/hardware_performance_legacy'
require_relative '../portable/hardware_vm_performance'

module Game
  class << self
    attr_accessor :clock, :ticks, :cancel_at, :records
    def tick(_fps)
      self.clock += 6
      self.ticks += 1
    end
  end
  module Time
    def self.millis; Game.clock; end
  end
  module Input
    B = 32
    def self.just_pressed?(_); Game.ticks == Game.cancel_at; end
  end
  module Runtime
    def self.backend; 'test'; end
    def self.api_version; 15; end
  end
  module Assets
    def self.collect; end
    def self.resident_bytes; 1234; end
    def self.read(path); path; end
  end
  module Display
    def self.present; Game.clock += 2; end
    def self.set_background(value); Game.records << [:background, value]; end
    def self.begin_frame; end
    def self.clear(*); end
    def self.fill_rect(*); end
    def self.text(*); end
    def self.draw_line(*); end
  end
  module Audio
    def self.stop; Game.records << [:stop]; end
    class Sound
      def self.load(*); new; end
      def play(*); end
    end
  end
  class Sprite
    def self.load(*args)
      Game.records << [:load, *args]
      new
    end
    def draw_batch(placements); Game.records << [:sprites, placements.dup]; end
  end
  module ThreeD
    class Model
      def initialize(vertices, indices, colors, edges = [], color = 0xffff, depth = true)
        Game.records << [:model, vertices.dup, indices.dup, colors.dup, edges.dup]
        @vertices, @indices, @colors, @edges = vertices.dup, indices.dup, colors.dup, edges.dup
        @color, @depth = color, depth
      end
      def draw_instances(transforms)
        transforms.each_slice(9) do |tx,ty,tz,yaw,pitch,roll,sx,sy,sz|
          vertices = @vertices.each_slice(3).flat_map do |vx,vy,vz|
            x,y,z = vx*sx, vy*sy, vz*sz
            rx,rz = x*Math.cos(yaw)-z*Math.sin(yaw), x*Math.sin(yaw)+z*Math.cos(yaw)
            ry,z = y*Math.cos(pitch)-rz*Math.sin(pitch), y*Math.sin(pitch)+rz*Math.cos(pitch)
            [tx+rx*Math.cos(roll)-ry*Math.sin(roll), ty+rx*Math.sin(roll)+ry*Math.cos(roll), tz+z]
          end
          ThreeD.draw_indexed_mesh(vertices, @indices, @colors) unless @indices.empty?
          ThreeD.draw_wireframe(vertices, @edges, @color, @depth) unless @edges.empty?
        end
      end
    end
    def self.set_viewport(*args); Game.records << [:viewport, *args]; end
    def self.set_resolution(*args); Game.records << [:resolution, *args]; end
    def self.set_camera(*args); Game.records << [:camera, *args]; end
    def self.begin_frame; end
    def self.draw_wireframe(vertices, edges, color, depth)
      Game.records << [:wire, vertices.dup, edges.dup, color, depth]
    end
    def self.draw_indexed_mesh(vertices, indices, colors)
      Game.records << [:mesh, vertices.dup, indices.dup, colors.dup]
    end
  end
end

class PerformanceTest < Minitest::Test
  class ClockScene
    attr_reader :frames, :configurations
    def initialize; @frames = []; @configurations = []; end
    def configure(*args); @configurations << args; end
    def update(frame); @frames << frame; Game.clock += 3; end
    def draw(*); Game.clock += 5; end
  end

  def setup
    Game.clock = 0
    Game.ticks = 0
    Game.cancel_at = -1
    Game.records = []
  end

  def test_statistics_use_nearest_rank_p95_and_handle_clock_wrap
    assert_equal [250, 2, 4, 4], HardwarePerformance.stats([4, 2, 1, 3])
    assert_equal [5050, 50, 95, 100], HardwarePerformance.stats((1..100).to_a)
    assert_equal [700, 7, 7, 7], HardwarePerformance.stats([7])
    assert_raises(ArgumentError) { HardwarePerformance.stats([]) }
    assert_equal 6, HardwarePerformance.elapsed(0xfffffffd, 3)
    assert_equal '0.05', HardwarePerformance.decimal(5)
    assert_equal '60.00', HardwarePerformance.decimal(6000)
  end

  def test_phase_timing_excludes_warmup_and_replays_identical_frames
    scene = ClockScene.new
    log, = capture_io do
      assert_equal :measured, HardwarePerformance.run(scene, [['clock', 1, false]])
    end
    assert_equal [[1, false]], scene.configurations
    sequence = (0...30).to_a + (0...120).to_a
    assert_equal sequence * 3, scene.frames
    assert_equal 450, Game.ticks
    assert_equal 3, log.scan('elapsed_ms=1920 fps=62.50').length
    assert_equal 3, log.scan('frame_ms_mean=16.00 median=16 p95=16 max=16').length
    assert_equal 3, log.scan('update_ms_mean=3.00 draw_ms_mean=5.00 present_ms_mean=2.00').length
  end

  def test_cancel_is_distinct_from_completion_during_warmup_and_sampling
    [2, 35].each do |cancel_at|
      setup
      Game.cancel_at = cancel_at
      log, = capture_io do
        assert_equal :cancelled, HardwarePerformance.run(ClockScene.new, [['cancel', 1, false]])
      end
      assert_equal cancel_at, Game.ticks
      assert_includes log, '[PERF] CANCEL scene=cancel round=1'
      refute_includes log, 'elapsed_ms='
    end
  end

  def test_sprite_workload_is_repeatable_and_covers_all_sheets
    scene = HardwarePerformance::Sprites.new
    loads = Game.records.select { |r| r[0] == :load }
    assert_equal 24, loads.length
    assert_equal 24, loads.map { |r| r[1] }.uniq.length
    assert loads.all? { |r| r[2..4] == [32, 32, 2] }
    scene.configure(256, true)
    assert_equal [:background, 'perf/background.raw'], Game.records.last
    scene.update(42)
    Game.records = []
    scene.draw('2D', 42, 1)
    expected = Game.records
    placements = expected.flat_map { |r| r[1] }
    assert_equal 256 * 3, placements.length
    placements.each_slice(3) do |x, y, frame|
      assert(-32 <= x && x < 320 && 42 <= y && y < 198)
      assert_includes [0, 1], frame
    end
    scene.update(3)
    scene.update(42)
    Game.records = []
    scene.draw('2D', 42, 1)
    assert_equal expected, Game.records
  end

  def test_flight_workload_uses_valid_batches_and_repeats_poses
    scene = HardwarePerformance::Flight.new
    scene.configure(320, false)
    assert_equal [:resolution, 320, 240], Game.records.last
    scene.update(42)
    Game.records = []
    scene.draw('3D', 42, 1)
    expected = Game.records
    batches = expected.select { |r| [:wire, :mesh].include?(r[0]) }
    assert_equal 5, batches.length
    assert_equal [24, 32, 24, 72, 57], batches.map { |r| r[1].length / 3 }
    batches.each do |_, vertices, indices|
      assert_equal 0, vertices.length % 3
      assert indices.all? { |i| i >= 0 && i < vertices.length / 3 }
      assert_operator vertices.length / 3, :<=, 256
    end
    scene.update(3)
    scene.update(42)
    Game.records = []
    scene.draw('3D', 42, 1)
    assert_equal expected, Game.records
  end

  def test_cancelled_flight_restores_audio_and_resolution
    Game.cancel_at = 1
    capture_io { assert_equal :cancelled, HardwarePerformance.run_3d }
    assert_equal [:resolution, 160, 120], Game.records.last
    assert_includes Game.records, [:stop]
  end

  def test_standard_flight_measures_only_retained_models
    labels = []
    original = HardwarePerformance.method(:measure)
    HardwarePerformance.define_singleton_method(:measure) do |scene, label, round|
      labels << [label, round]
      6000
    end
    log, = capture_io { assert_equal :measured, HardwarePerformance.run_3d }
    assert_equal [['3D-M160', 1], ['3D-M160', 2], ['3D-M160', 3],
                  ['3D-M320', 1], ['3D-M320', 2], ['3D-M320', 3]], labels
    assert_includes log, 'profile=standard'
    assert_equal 10, Game.records.count { |record| record[0] == :model }
    assert_equal [:resolution, 160, 120], Game.records.last
  ensure
    HardwarePerformance.define_singleton_method(:measure, original) if original
  end

  def primitives(records)
    records.flat_map do |type, vertices, indices, colors, depth|
      case type
      when :wire
        indices.each_slice(2).map { |edge| [colors, depth, *edge.flat_map { |i| vertices[i*3,3] }] }
      when :mesh
        indices.each_slice(3).with_index.map { |face, i| [colors[i], *face.flat_map { |v| vertices[v*3,3] }] }
      else []
      end
    end
  end

  def test_retained_flight_replays_the_original_geometry_and_draw_order
    legacy = HardwarePerformance::Flight.new
    retained = HardwarePerformance::FlightModels.new
    [0, 29, 42, 119].each do |frame|
      Game.records = []
      legacy.update(frame)
      legacy.draw('3D', frame, 1)
      expected = primitives(Game.records)
      Game.records = []
      retained.update(frame)
      retained.draw('3D', frame, 1)
      actual = primitives(Game.records)
      assert_equal expected.length, actual.length
      expected.zip(actual).each do |a,b|
        assert_equal a.length, b.length
        a.zip(b).each do |x,y|
          x.is_a?(Numeric) ? assert_in_delta(x,y,0.000001) : assert_equal(x,y)
        end
      end
    end
  end

  def test_renderer_workloads_use_valid_shared_mesh_and_long_lines
    scene = HardwarePerformance::Renderer.new
    models = Game.records.select { |r| r[0] == :model }
    assert_equal [128, 16], models.map { |r| r[1].length / 3 }
    assert_equal 224, models[0][3].length
    models.each do |_, vertices, indices, colors, edges|
      assert_operator vertices.length / 3, :<=, 256
      assert_equal indices.length / 3, colors.length
      assert (indices + edges).all? { |i| i >= 0 && i < vertices.length / 3 }
    end
    scene.configure(160, false)
    scene.update(42)
    Game.records = []
    scene.draw('SHR-160',42,1)
    first = Game.records
    scene.update(3)
    scene.update(42)
    Game.records = []
    scene.draw('SHR-160',42,1)
    assert_equal first, Game.records
    scene.configure(320, true)
    scene.update(0)
    Game.records = []
    scene.draw('CLIP320',0,1)
    assert_equal 8, primitives(Game.records).length
  end

  def test_vm_kernels_have_fixed_results_and_validate_kind
    3.times do
      assert_equal [125361, 404, 1178], (0..2).map { |kind| HardwareVMPerformance.work(kind) }
    end
    assert_raises(ArgumentError) { HardwareVMPerformance.work(3) }
  end

  def test_triangle_workloads_replay_small_sliver_and_large_models
    scene = HardwarePerformance::Triangles.new
    models = Game.records.select { |r| r[0] == :model }
    assert_equal [192, 6, 4], models.map { |r| r[1].length / 3 }
    assert_equal [64, 2, 2], models.map { |r| r[2].length / 3 }
    models.each do |_, vertices, faces, colors|
      assert_equal faces.length / 3, colors.length
      assert faces.all? { |v| v >= 0 && v < vertices.length / 3 }
    end
    [0, 1, 2].each do |kind|
      scene.configure(kind == 2 ? 160 : 320, kind)
      scene.update(42)
      Game.records = []
      scene.draw('TRI',42,1)
      expected = Game.records
      assert_equal (kind == 0 ? 64 : 2), primitives(expected).length
      scene.update(3)
      scene.update(42)
      Game.records = []
      scene.draw('TRI',42,1)
      assert_equal expected, Game.records
    end
  end

  def test_vm_timing_excludes_display_tick_and_validates_every_sample
    original = HardwareVMPerformance.method(:work)
    calls = []
    HardwareVMPerformance.define_singleton_method(:work) do |kind|
      calls << kind
      Game.clock += 4
      HardwareVMPerformance::EXPECTED[kind]
    end
    log, = capture_io { assert_equal :measured, HardwareVMPerformance.run }
    assert_equal [450, 450, 450], (0..2).map { |kind| calls.count(kind) }
    assert_equal 1350, Game.ticks
    assert_equal 9, log.scan('work_ms_mean=4.00 median=4 p95=4 max=4').length
    assert_includes log, 'normal_gc=on timing=ruby_work_only'
    assert_includes log, '[VM PERF] BEGIN v=2'
    assert_equal 9, log.scan('[VM PERF] v=2 scene=').length
    assert_equal [:stop], Game.records.last
  ensure
    HardwareVMPerformance.define_singleton_method(:work, original) if original
  end

  def test_vm_cancellation_and_bad_result_do_not_report_completion
    [2, 35].each do |cancel_at|
      setup
      Game.cancel_at = cancel_at
      log, = capture_io { assert_equal :cancelled, HardwareVMPerformance.run }
      assert_includes log, '[VM PERF] CANCEL scene=VM-NUM round=1'
      refute_includes log, 'work_ms_mean='
      assert_equal [:stop], Game.records.last
    end
    setup
    original = HardwareVMPerformance.method(:work)
    HardwareVMPerformance.define_singleton_method(:work) { |_| -1 }
    capture_io { assert_raises(RuntimeError) { HardwareVMPerformance.run } }
    assert_equal [:stop], Game.records.last
  ensure
    HardwareVMPerformance.define_singleton_method(:work, original) if original
  end
end
