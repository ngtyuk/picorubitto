# Loaded around the production suite by minimum_profile_regression.py.
HARDWARE_SUITE_LIBRARY = true
ASSET_GC_CHECKSUMS = Array.new(32, 123)
class MenuShown < Exception; end

# Hardware is stubbed; Ruby operations and the suite runner use the real VM.
module Game
  @menu_probe = false
  @millis = 0
  def self.init; end
  def self.probe_menu
    @menu_probe = true
  end
  def self.tick(_fps)
    raise MenuShown if @menu_probe
    @millis += 33
    Assets.finish_scene
  end
  def self.millis
    @millis
  end
  module Display
    def self.clear(*); end
    def self.text(*); end
    def self.fill_rect(*); end
    def self.draw_circle(*); end
    def self.draw_line(*); end
    def self.present; end
    def self.set_background(*); end
    def self.begin_frame; end
  end
  module ThreeD
    class Model
      def initialize(*); end
      def draw_instances(*); end
    end
    def self.set_viewport(*); end
    def self.set_resolution(*); end
    def self.set_camera(*); end
    def self.begin_frame; end
    def self.draw_indexed_mesh(*); end
    def self.draw_wireframe(*); end
  end
  module Runtime
    def self.backend; 'mock'; end
    def self.api_version; 15; end
  end
  class Texture
    def self.load(path, *)
      Game::Assets.loaded(path)
      new(path)
    end
    def initialize(path)
      @path = path
    end
    def checksum
      123
    end
    def draw(*); end
  end
  class Font < Texture
    def self.load(path, *)
      raise 'invalid fixture font' if path == 'font-invalid.pbf'
      super
    end
    def measure(text, size, spacing = 0)
      maximum = 0
      text.split("\n").each do |line|
        width = 0
        line.each_byte { |byte| width += (byte == 32 ? 3 : 6) * (size / 12) + spacing }
        width -= spacing unless line.empty?
        maximum = width if width > maximum
      end
      maximum
    end
  end
  class Sprite < Texture
    def draw_batch(*); end
  end
  module Audio
    def self.dial_supported?; true; end
    def self.dial_value; 2048; end
    def self.master_volume; 127; end
    def self.supported?
      true
    end
    def self.stop; end
    class Sound < Texture
      def play(*); end
      def stop; end
    end
  end
  module Input
    A = 16
    B = 32
    START = 256
    def self.pressed?(_bit)
      false
    end
    def self.just_pressed?(bit)
      bit != B
    end
    def self.state
      1023
    end
  end
  module Time
    def self.millis
      Game.millis
    end
  end
  module Assets
    @resident = 0
    def self.loaded(path)
      @resident += 524288 if path[0, 6] == 'large/'
    end
    def self.read(path); path; end
    def self.collect
      @resident = 0
      nil
    end
    # Hardware memory is mocked; native ASan and WASM tests verify its lifetime.
    def self.finish_scene
      @resident = 0
    end
    def self.resident_bytes
      @resident
    end
  end
  module Save
    @slots = {}
    def self.read(slot)
      @slots[slot]
    end
    def self.write(slot, data)
      @slots[slot] = data
      true
    end
    def self.delete(slot)
      @slots.delete(slot)
      true
    end
    def self.exists?(slot)
      @slots.key?(slot)
    end
  end
end

# The C Game API contract is verified separately in WASM and on the board.
def run_game_api_contract
  true
end

# SUITE_INSERTION_POINT

raise 'unexpected IO gem' if HardwareChecks.respond_to?(:puts, true)
raise 'unexpected kernel-ext gem' if HardwareChecks.respond_to?(:send)
print '[MINIMUM] hardware and Game API are stubbed', "\n"
audio = HardwareAudio::Player.new
audio.update(1 | 2 | 4 | 8, 0)
audio.update(512, 10)
audio.update(16 | 32 | 64, 20)
audio.update(128, 30)
audio.update(0, 5000)
audio.update(512, 5010)
raise 'audio START ignored' if audio.update(256, 5020)
audio.close
audio = nil
Game::Save.write('hardware-check', "original\x00\xfe")
HardwareChecks.run_all
raise 'previous save lost' unless Game::Save.read('hardware-check') == "original\x00\xfe"

# Exercise the production exception reporting and continue path on this VM.
module HardwareChecks
  def self.ruby_check
    raise ArgumentError, 'expected smoke failure'
  end
end
raise 'failure could not continue' unless HardwareChecks.run(0)
Game.probe_menu
begin
  HardwareChecks.start
rescue MenuShown
  print '[MINIMUM] menu ready', "\n"
end
print '[MINIMUM] PASS', "\n"
