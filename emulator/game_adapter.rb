# RP2350のGame APIをCanvas・キーボード・仮想SDへ置き換えるブラウザ実装。
require 'js'

module Game
  API_VERSION = 16 unless const_defined?(:API_VERSION)
  SAVE_MAX_BYTES = 32 * 1024 unless const_defined?(:SAVE_MAX_BYTES)
  SAVE_SLOT_MAX = 24 unless const_defined?(:SAVE_SLOT_MAX)

  module Capabilities
    DISPLAY = 1 << 0 unless const_defined?(:DISPLAY)
    INPUT = 1 << 1 unless const_defined?(:INPUT)
    ASSETS = 1 << 2 unless const_defined?(:ASSETS)
    SPRITES = 1 << 3 unless const_defined?(:SPRITES)
    AUDIO = 1 << 4 unless const_defined?(:AUDIO)
    SAVE = 1 << 5 unless const_defined?(:SAVE)
    COLLISION = 1 << 6 unless const_defined?(:COLLISION)
    TEXTURES = 1 << 7 unless const_defined?(:TEXTURES)
    FONTS = 1 << 8 unless const_defined?(:FONTS)
    SHAPES = 1 << 9 unless const_defined?(:SHAPES)
    AUDIO_MIXING = 1 << 10 unless const_defined?(:AUDIO_MIXING)
    THREE_D = 1 << 11 unless const_defined?(:THREE_D)
    THREE_D_MODELS = 1 << 12 unless const_defined?(:THREE_D_MODELS)
    AUDIO_DIAL = 1 << 13 unless const_defined?(:AUDIO_DIAL)
  end

  module Runtime
    def self.api_version
      Game::API_VERSION
    end

    def self.backend
      'wasm'
    end

    def self.capabilities
      capabilities = Game::Capabilities::DISPLAY |
        Game::Capabilities::INPUT |
        Game::Capabilities::ASSETS |
        Game::Capabilities::SPRITES |
        Game::Capabilities::COLLISION |
        Game::Capabilities::TEXTURES |
        Game::Capabilities::FONTS |
        Game::Capabilities::SHAPES |
        Game::Capabilities::AUDIO_MIXING |
        Game::Capabilities::THREE_D |
        Game::Capabilities::THREE_D_MODELS
      capabilities |= Game::Capabilities::AUDIO if Game::Audio.supported?
      capabilities |= Game::Capabilities::SAVE if Game::Save.supported?
      capabilities |= Game::Capabilities::AUDIO_DIAL if Game::Audio.dial_supported?
      capabilities
    end

    def self.capability?(capability)
      capability = capability.to_i
      capability > 0 && (capabilities & capability) == capability
    end
  end

  class Asset
    attr_reader :path

    def initialize(path)
      @path = path.to_s
    end
  end

  module Input
    UP = 1 << 0
    DOWN = 1 << 1
    LEFT = 1 << 2
    RIGHT = 1 << 3
    A = 1 << 4
    B = 1 << 5
    L = 1 << 6
    R = 1 << 7
    START = 1 << 8
    SELECT = 1 << 9

    def self.state
      JS.global[:GameEmulator][:inputState].to_i
    end

    def self.pressed?(button)
      mask = normalize_button(button)
      (state & mask) != 0
    end

    def self.just_pressed?(button)
      mask = normalize_button(button)
      (JS.global[:GameEmulator][:inputJustPressed].to_i & mask) != 0
    end

    def self.just_released?(button)
      mask = normalize_button(button)
      (JS.global[:GameEmulator][:inputJustReleased].to_i & mask) != 0
    end

    def self.normalize_button(button)
      mask = button.to_i
      unless mask > 0 && (mask & ~(UP | DOWN | LEFT | RIGHT | A | B | L | R |
                                   START | SELECT)) == 0 &&
             (mask & (mask - 1)) == 0
        raise ArgumentError, 'button must be one Game::Input button constant'
      end
      mask
    end
  end

  module Display
    def self.width
      320
    end

    def self.height
      240
    end

    def self.clear(color)
      JS.global[:GameEmulator].clear(color)
    end

    def self.set_background(data)
      if data.nil?
        JS.global[:GameEmulator].setBackground(nil)
        return nil
      end
      unless data.is_a?(Game::Asset)
        raise ArgumentError, 'background data must be a Game::Asset'
      end
      JS.global[:GameEmulator].setBackground(data.path)
    end

    def self.begin_frame
      JS.global[:GameEmulator].beginFrame
    end

    def self.fill_rect(x, y, width, height, color, alpha = 255)
      JS.global[:GameEmulator].fillRect(x, y, width, height, color, alpha)
    end

    def self.draw_circle(x, y, radius, color, alpha = 255)
      JS.global[:GameEmulator].drawCircle(x, y, radius, color, alpha)
    end

    def self.draw_line(x1, y1, x2, y2, thickness, color, alpha = 255)
      JS.global[:GameEmulator].drawLine(x1, y1, x2, y2, thickness, color, alpha)
    end

    def self.text(x, y, text, color = 0xffff)
      JS.global[:GameEmulator].drawText(x, y, text, color)
    end

    def self.draw_text(x, y, text, color = 0xffff)
      self.text(x, y, text, color)
    end

    def self.blit_rgb565(x, y, width, height, data)
      unless data.is_a?(Game::Asset)
        raise ArgumentError, 'browser emulator expects Game::Asset data'
      end
      JS.global[:GameEmulator].blitAsset(data.path, x, y, width, height)
    end

    def self.blit_rgb565_region(x, y, width, height, data, source_width, source_x, source_y)
      unless data.is_a?(Game::Asset)
        raise ArgumentError, 'browser emulator expects Game::Asset data'
      end
      JS.global[:GameEmulator].blitAssetRegion(
        data.path, x, y, width, height, source_width, source_x, source_y
      )
    end

    def self.present
      JS.global[:GameEmulator].present
    end
  end

  module ThreeD
    def self.begin_frame
      JS.global[:GameEmulator].threeDBeginFrame
      nil
    end

    def self.set_viewport(x, y, width, height)
      values = [x, y, width, height]
      unless values.all? { |value| value.is_a?(Integer) }
        raise TypeError, '3D viewport values must be integers'
      end
      if x < 0 || y < 0 || width <= 0 || height <= 0 ||
         x + width > 320 || y + height > 240
        raise ArgumentError, 'invalid 3D viewport'
      end
      unless JS.global[:GameEmulator].set3DViewport(x, y, width, height)
        raise ArgumentError, 'invalid 3D viewport'
      end
      nil
    end

    def self.set_resolution(width, height)
      values = [width, height]
      unless values.all? { |value| value.is_a?(Integer) }
        raise TypeError, '3D internal resolution values must be integers'
      end
      if width <= 0 || height <= 0 || width > 320 || height > 240
        raise ArgumentError, 'invalid 3D internal resolution'
      end
      unless JS.global[:GameEmulator].set3DResolution(width, height)
        raise ArgumentError, 'invalid 3D internal resolution'
      end
      nil
    end

    def self.set_camera(x, y, z, yaw, pitch, fov_degrees = 70.0,
                        near_clip = 0.1, far_clip = 100.0)
      values = [x, y, z, yaw, pitch, fov_degrees, near_clip, far_clip]
      unless values.all? { |value| value.is_a?(Numeric) }
        raise TypeError, '3D camera values must be numbers'
      end
      unless values.all? { |value| finite_number?(value) }
        raise ArgumentError, '3D camera values must be finite'
      end
      if fov_degrees <= 1 || fov_degrees >= 179 || near_clip <= 0 ||
         far_clip <= near_clip
        raise ArgumentError, 'invalid 3D camera parameters'
      end
      JS.global[:GameEmulator].set3DCamera(
        js_number(x), js_number(y), js_number(z), js_number(yaw),
        js_number(pitch), js_number(fov_degrees), js_number(near_clip),
        js_number(far_clip)
      )
      nil
    end

    def self.draw_triangle(x1, y1, z1, x2, y2, z2, x3, y3, z3, color)
      coordinates = [x1, y1, z1, x2, y2, z2, x3, y3, z3]
      unless coordinates.all? { |value| value.is_a?(Numeric) }
        raise TypeError, '3D coordinates must be numbers'
      end
      unless coordinates.all? { |value| finite_number?(value) }
        raise ArgumentError, '3D coordinates must be finite'
      end
      unless color.is_a?(Integer)
        raise TypeError, '3D color must be an integer'
      end
      unless color >= 0 && color <= 0xffff
        raise ArgumentError, '3D color must be an RGB565 integer'
      end
      JS.global[:GameEmulator].draw3DTriangle(
        js_number(x1), js_number(y1), js_number(z1),
        js_number(x2), js_number(y2), js_number(z2),
        js_number(x3), js_number(y3), js_number(z3), color
      )
      nil
    end

    def self.draw_triangles(triangles)
      unless triangles.is_a?(Array) && triangles.length % 10 == 0
        raise ArgumentError, '3D triangle batch must contain 10-value records'
      end
      triangles.each_with_index do |value, index|
        if index % 10 == 9
          raise TypeError, '3D color must be an integer' unless value.is_a?(Integer)
          raise ArgumentError, '3D color must be an RGB565 integer' unless value >= 0 && value <= 0xffff
        elsif value.is_a?(Numeric)
          raise ArgumentError, '3D coordinates must be finite' unless finite_number?(value)
        elsif !value.is_a?(Numeric)
          raise TypeError, '3D coordinates must be numbers'
        end
      end
      js_triangles = triangles.map { |value| js_number(value) }
      JS.global[:GameEmulator].draw3DTriangles(JS::Bridge.to_js(js_triangles))
      nil
    end

    def self.draw_indexed_mesh(vertices, indices, colors)
      unless vertices.is_a?(Array) && vertices.length % 3 == 0 &&
             indices.is_a?(Array) && indices.length % 3 == 0 &&
             colors.is_a?(Array) && colors.length == indices.length / 3
        raise ArgumentError, 'invalid 3D indexed mesh arrays'
      end
      if vertices.length / 3 > 256
        raise ArgumentError, '3D indexed mesh has too many vertices'
      end
      vertices.each do |value|
        raise TypeError, '3D mesh vertices must be numbers' unless value.is_a?(Numeric)
        raise ArgumentError, '3D mesh vertices must be finite' unless finite_number?(value)
      end
      indices.each do |value|
        raise TypeError, '3D mesh indices must be integers' unless value.is_a?(Integer)
        unless value >= 0 && value < vertices.length / 3
          raise ArgumentError, '3D mesh index is out of range'
        end
      end
      colors.each do |value|
        raise TypeError, '3D mesh colors must be integers' unless value.is_a?(Integer)
        unless value >= 0 && value <= 0xffff
          raise ArgumentError, '3D mesh color must be an RGB565 integer'
        end
      end
      JS.global[:GameEmulator].draw3DIndexedMesh(
        JS::Bridge.to_js(vertices.map { |value| js_number(value) }),
        JS::Bridge.to_js(indices),
        JS::Bridge.to_js(colors)
      )
      nil
    end

    def self.draw_wireframe(vertices, edges, color, depth_test = true)
      unless vertices.is_a?(Array) && vertices.length % 3 == 0 &&
             edges.is_a?(Array) && edges.length % 2 == 0
        raise ArgumentError, '3D wireframe arrays must contain XYZ and edge pairs'
      end
      unless color.is_a?(Integer)
        raise TypeError, 'wireframe color must be an integer'
      end
      unless color >= 0 && color <= 0xffff
        raise ArgumentError, 'wireframe color must be an RGB565 integer'
      end
      unless depth_test == true || depth_test == false
        raise TypeError, 'wireframe depth_test must be true or false'
      end
      if vertices.length / 3 > 256
        raise ArgumentError, '3D wireframe has too many vertices'
      end
      vertices.each do |value|
        raise TypeError, '3D wireframe vertices must be numbers' unless value.is_a?(Numeric)
        raise ArgumentError, '3D wireframe vertices must be finite' unless finite_number?(value)
      end
      edges.each do |value|
        raise TypeError, '3D wireframe edges must be integers' unless value.is_a?(Integer)
        unless value >= 0 && value < vertices.length / 3
          raise ArgumentError, '3D wireframe edge index is out of range'
        end
      end
      JS.global[:GameEmulator].draw3DWireframe(
        JS::Bridge.to_js(vertices.map { |value| js_number(value) }),
        JS::Bridge.to_js(edges), color, depth_test
      )
      nil
    end

    class Model
      MAX_VERTICES = 256
      MAX_TRIANGLES = 1024
      MAX_EDGES = 2048
      MAX_INSTANCES = 128

      def initialize(vertices, indices, colors, edges = [], color = 0xffff, depth_test = true)
        raise RuntimeError, 'model is already initialized' if @lease
        unless vertices.is_a?(Array) && indices.is_a?(Array) && colors.is_a?(Array) && edges.is_a?(Array)
          raise TypeError, 'model data must be Arrays'
        end
        count = vertices.length / 3
        unless vertices.length % 3 == 0 && count > 0 && count <= MAX_VERTICES &&
               indices.length % 3 == 0 && indices.length / 3 <= MAX_TRIANGLES &&
               colors.length == indices.length / 3 && edges.length % 2 == 0 &&
               edges.length / 2 <= MAX_EDGES
          raise ArgumentError, 'invalid model arrays or limits'
        end
        vertices.each { |value| validate_number(value) }
        validate_integers(indices, count)
        validate_integers(colors, 65536)
        validate_integers(edges, count)
        raise TypeError, 'model color must be an integer' unless color.is_a?(Integer)
        raise ArgumentError, 'model color is out of range' unless color >= 0 && color <= 65535
        unless depth_test == true || depth_test == false
          raise TypeError, 'model depth_test must be true or false'
        end
        @lease = Game::AssetLease.new
        # Send values as text, not bridge-owned JS arrays whose reference-table
        # entries outlive Ruby GC. Only the numeric model handle persists.
        @lease.attach(JS.global[:GameEmulator].create3DModelJSON(
          JSON.generate([vertices, indices, colors, edges, color, depth_test])).to_i)
      end

      def draw_instances(transforms)
        raise TypeError, 'instances must be an Array' unless transforms.is_a?(Array)
        unless transforms.length % 9 == 0 && transforms.length / 9 <= MAX_INSTANCES
          raise ArgumentError, 'instances must contain at most 128 nine-value transforms'
        end
        transforms.each { |value| validate_number(value) }
        unless JS.global[:GameEmulator].draw3DModelJSON(@lease.id, JSON.generate(transforms))
          raise ArgumentError, 'invalid model draw or frame not begun'
        end
        nil
      end

      def initialize_copy(other)
        raise TypeError, 'models cannot be copied; share the model instead'
      end

      def validate_number(value)
        unless value.is_a?(Integer) || value.is_a?(Float)
          raise TypeError, 'model values must be Integer or Float'
        end
        unless Game::ThreeD.finite_number?(value) && value.abs <= 32768
          raise ArgumentError, 'model values must be finite and within +/-32768'
        end
      end

      def validate_integers(values, limit)
        values.each do |value|
          raise TypeError, 'model indices and colors must be integers' unless value.is_a?(Integer)
          unless value >= 0 && value < limit
            raise ArgumentError, 'model index or color is out of range'
          end
        end
      end
    end

    def self.js_number(value)
      value.is_a?(Float) ? value.to_s : value
    end

    def self.finite_number?(value)
      value.is_a?(Integer) || value.finite?
    end
  end

  module Collision
    def self.rects_overlap?(x1, y1, width1, height1, x2, y2, width2, height2)
      if width1 <= 0 || height1 <= 0 || width2 <= 0 || height2 <= 0
        raise ArgumentError, 'rectangle width and height must be positive'
      end

      x1 < x2 + width2 && x2 < x1 + width1 &&
        y1 < y2 + height2 && y2 < y1 + height1
    end
  end

  module Assets
    def self.collect
      GC.start
      JS.global[:GameEmulator].collectAssets
      nil
    end

    def self.resident_bytes
      JS.global[:GameEmulator].assetResidentBytes.to_i
    end

    def self.read(path)
      Game::Asset.new(path)
    end
  end

  module Save
    HEX_DIGITS = '0123456789abcdef' unless const_defined?(:HEX_DIGITS)

    def self.supported?
      JS.global[:GameEmulator][:saveSupported] == true
    end

    def self.exists?(slot)
      slot = normalize_slot(slot)
      result = JS.global[:GameEmulator].saveExists(slot)
      raise RuntimeError, 'save storage is unavailable' unless result == true || result == false
      result == true
    end

    def self.read(slot)
      slot = normalize_slot(slot)
      raise RuntimeError, 'save storage is unavailable' unless supported?
      encoded = JS.global[:GameEmulator].loadSave(slot)
      return nil if encoded.nil?

      decode(encoded.to_s)
    end

    def self.write(slot, data)
      slot = normalize_slot(slot)
      unless data.is_a?(String)
        raise TypeError, 'save data must be a String'
      end
      if data.bytesize > Game::SAVE_MAX_BYTES
        raise RangeError, 'save data must be at most 32768 bytes'
      end
      result = JS.global[:GameEmulator].saveData(slot, encode(data))
      raise RuntimeError, 'save data write failed' unless result == true
      true
    end

    def self.delete(slot)
      slot = normalize_slot(slot)
      result = JS.global[:GameEmulator].deleteSave(slot)
      raise RuntimeError, 'save data delete failed' unless result == true
      true
    end

    def self.normalize_slot(slot)
      unless slot.is_a?(String) && slot.bytesize > 0 && slot.bytesize <= Game::SAVE_SLOT_MAX
        raise ArgumentError, 'save slot must be 1..24 ASCII letters, numbers, _ or -'
      end
      index = 0
      while index < slot.bytesize
        byte = slot.getbyte(index)
        alphanumeric = (byte >= 65 && byte <= 90) ||
                       (byte >= 97 && byte <= 122) ||
                       (byte >= 48 && byte <= 57)
        unless alphanumeric || byte == 95 || byte == 45
          raise ArgumentError, 'save slot must be 1..24 ASCII letters, numbers, _ or -'
        end
        index += 1
      end
      slot
    end

    def self.encode(data)
      encoded = ''
      index = 0
      while index < data.bytesize
        byte = data.getbyte(index)
        encoded << HEX_DIGITS[(byte >> 4) & 0x0f]
        encoded << HEX_DIGITS[byte & 0x0f]
        index += 1
      end
      encoded
    end

    def self.decode(encoded)
      if encoded.bytesize > Game::SAVE_MAX_BYTES * 2 || (encoded.bytesize & 1) != 0
        raise RuntimeError, 'save data is invalid'
      end
      decoded = ''
      index = 0
      while index < encoded.bytesize
        high = hex_value(encoded.getbyte(index))
        low = hex_value(encoded.getbyte(index + 1))
        raise RuntimeError, 'save data is invalid' if high < 0 || low < 0

        decoded << ((high << 4) | low).chr
        index += 2
      end
      decoded
    end

    def self.hex_value(byte)
      return byte - 48 if byte >= 48 && byte <= 57
      return byte - 55 if byte >= 65 && byte <= 70
      return byte - 87 if byte >= 97 && byte <= 102

      -1
    end
  end

  module Time
    def self.millis
      JS.global[:GameEmulator].timeMillis.to_i
    end

    def self.delta_ms
      JS.global[:GameEmulator][:timeDeltaMs].to_i
    end
  end

  class Sprite
    def self.load(path, frame_width, frame_height, frame_count, key_color = 0xf81f)
      handle = Game::AssetLease.new
      handle.attach(JS.global[:GameEmulator].loadSprite(
        path, frame_width, frame_height, frame_count, key_color
      ).to_i)
      new(handle, frame_width, frame_height, frame_count)
    end

    def initialize(handle, frame_width, frame_height, frame_count)
      @handle = handle
      @frame_width = frame_width
      @frame_height = frame_height
      @frame_count = frame_count
    end

    def draw(x, y, frame)
      JS.global[:GameEmulator].drawSprite(@handle.id, x, y, frame)
    end

    def draw_batch(placements)
      valid = placements.is_a?(Array) && placements.length % 3 == 0
      placements.each { |value| valid = false unless value.is_a?(Integer) } if valid
      unless valid
        raise ArgumentError, 'sprite batch must contain integer x, y, frame triples'
      end
      JS.global[:GameEmulator].drawSprites(@handle.id, JS::Bridge.to_js(placements))
      nil
    end

    def width
      @frame_width
    end

    def height
      @frame_height
    end

    def frames
      @frame_count
    end
  end

  class Texture
    def self.load(path, width, height, key_color = 0xf81f)
      handle = Game::AssetLease.new
      handle.attach(JS.global[:GameEmulator].loadTexture(
        path, width, height, key_color
      ).to_i)
      new(handle, width, height)
    end

    def initialize(handle, width, height)
      @handle = handle
      @width = width
      @height = height
    end

    def draw(x, y, source_x = 0, source_y = 0, source_width = @width,
             source_height = @height, destination_width = source_width,
             destination_height = source_height)
      JS.global[:GameEmulator].drawTexture(
        @handle.id, x, y, source_x, source_y, source_width, source_height,
        destination_width, destination_height
      )
    end

    def checksum
      JS.global[:GameEmulator].textureChecksum(@handle.id).to_i
    end

    def width
      @width
    end

    def height
      @height
    end
  end

  class Font
    def self.load(path)
      handle = Game::AssetLease.new
      handle.attach(JS.global[:GameEmulator].loadFont(path).to_i)
      new(handle)
    end

    def initialize(handle)
      @handle = handle
    end

    def measure(text, size, spacing = 0)
      # The upstream multi-argument bridge does not escape control characters.
      JS.global[:GameEmulator].measureFontJSON(@handle.id, JSON.generate(text), size, spacing).to_i
    end

    def draw(text, x, y, size, spacing = 0, color = 0xffff)
      JS.global[:GameEmulator].drawFontJSON(
        @handle.id, x, y, JSON.generate(text), size, spacing, color
      )
      nil
    end
  end

  module Audio
    def self.dial_supported?
      JS.global[:GameEmulator][:audioDialSupported] == true
    end

    def self.dial_value
      dial_supported? ? JS.global[:GameEmulator][:audioDialValue].to_i : nil
    end

    def self.master_volume
      JS.global[:GameEmulator][:audioMasterVolume].to_i
    end

    def self.supported?
      JS.global[:GameEmulator][:audioSupported] == true
    end

    def self.play(path, volume = 255)
      JS.global[:GameEmulator].audioPlay(path, volume)
      true
    end

    def self.play_se(path, volume = 255)
      play(path, volume)
    end

    def self.play_bgm(path, volume = 255, loop = false)
      JS.global[:GameEmulator].audioPlay(path, volume, true, loop)
      true
    end

    def self.stop_bgm
      JS.global[:GameEmulator].audioStopBgm
      nil
    end

    def self.stop
      JS.global[:GameEmulator].audioStop
      nil
    end

    def self.busy?
      JS.global[:GameEmulator][:audioBusy] == true
    end

    class Sound
      def self.load(path)
        handle = Game::AssetLease.new
        handle.attach(JS.global[:GameEmulator].loadSound(path).to_i)
        new(handle)
      end

      def initialize(handle)
        @handle = handle
      end

      def play(volume = 255)
        JS.global[:GameEmulator].soundPlay(@handle.id, volume)
        true
      end

      def play_se(volume = 255)
        play(volume)
      end

      def play_bgm(volume = 255, loop = false)
        JS.global[:GameEmulator].soundPlayBgm(@handle.id, volume, loop)
        true
      end

      def stop
        JS.global[:GameEmulator].soundStop(@handle.id)
        nil
      end
    end
  end

  def self.init
    JS.global[:GameEmulator][:ready].await
    JS.global[:GameEmulator].resetTiming
    true
  end

  def self.tick(fps)
    raise ArgumentError, 'fps must be positive' if fps <= 0
    sleep_ms((1000 / fps).to_i)
    JS.global[:GameEmulator].beginInputFrame
  end
end

# The adapter is loaded as a task, so register it after its definitions exist.
# PicoRuby 4.x resolves require against the actual loaded-feature list.
$LOADED_FEATURES << 'game' unless $LOADED_FEATURES.include?('game')
