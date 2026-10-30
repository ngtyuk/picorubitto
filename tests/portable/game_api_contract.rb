# Portable Game API contract test.
#
# Run this same cartridge on the RP2350 and in emulator/contract.html. A green
# screen means the shared language/API checks passed; a red screen means the
# target backend is missing part of the contract.
# Logs use core Kernel#print, also available without the native IO gem.
require 'game'

def contract_check(condition, message)
  raise "Game API contract failed: #{message}" unless condition
end

def contract_live_assets(baseline)
  texture = Game::Texture.load('texture.raw', 32, 32)
  sprite = Game::Sprite.load('texture.raw', 32, 32, 1)
  font = Game::Font.load('font.pbf')
  GC.start
  contract_check(Game::Assets.resident_bytes >= baseline + 4096, 'GC preserves live assets')
  contract_check(texture.checksum == 2725230221, 'loaded texture checksum')
  sprite.draw(0, 0, 0)
  font.measure('FONT', 12)
  true
end

def contract_model_lifetime
  vertices = [-1, -1, 0, 1, -1, 0, 0, 1, 0]
  indices = [0, 2, 1]
  colors = [0x07e0]
  edges = [0, 1, 1, 2, 2, 0]
  baseline = Game::Assets.resident_bytes
  model = Game::ThreeD::Model.new(vertices, indices, colors, edges, 0xffff, false)
  contract_check(Game::Assets.resident_bytes == baseline + 92, 'model storage accounting')
  vertices.clear
  indices.clear
  colors.clear
  edges.clear
  GC.start
  contract_check(model.draw_instances([0, 0, 4, 0, 0, 0, 1, 1, 1]).nil?, 'model snapshot and live GC')
  contract_check(model.draw_instances([]).nil?, 'empty instances')
  transforms = [0, 0, 4, 0.2, -0.1, 0.3, 1, 0.5, -1,
                1, 0, 6, 0, 0, 0, 0, 1, 1]
  contract_check(model.draw_instances(transforms).nil?, 'multiple model transforms')
  invalid = [[0], Array.new(129 * 9, 0), [0, 0, 4, 0, 0, 0, 32769, 1, 1]]
  index = 0
  while index < invalid.length
    caught = false
    begin
      model.draw_instances(invalid[index])
    rescue ArgumentError
      caught = true
    end
    contract_check(caught, 'invalid model instance bounds')
    index += 1
  end
  caught = false
  begin
    Game::ThreeD::Model.new([0, 0, 0], [0, 1, 0], [0xffff])
  rescue ArgumentError
    caught = true
  end
  contract_check(caught, 'model rejects invalid index')
  caught = false
  begin
    model.draw_instances([0, 0, 4, 0, 0, 0, '1', 1, 1])
  rescue TypeError
    caught = true
  end
  contract_check(caught, 'model rejects nonnumeric transform')
  true
end

def run_game_api_contract
  print '[CONTRACT] language / API surface', "\n"
  contract_check(Game::API_VERSION == 16, 'API version')
  contract_check(Game::Runtime.api_version == Game::API_VERSION, 'runtime API version')
  contract_check(Game::Runtime.backend.is_a?(String), 'runtime backend')
  contract_check(Game::Runtime.capability?(Game::Capabilities::DISPLAY), 'display capability')
  contract_check(Game::Runtime.capability?(Game::Capabilities::INPUT), 'input capability')
  contract_check(Game::Runtime.capability?(Game::Capabilities::ASSETS), 'asset capability')
  contract_check(Game::Runtime.capability?(Game::Capabilities::SPRITES), 'sprite capability')
  contract_check(Game::Runtime.capability?(Game::Capabilities::TEXTURES), 'texture capability')
  contract_check(Game::Runtime.capability?(Game::Capabilities::FONTS), 'font capability')
  contract_check(Game::Runtime.capability?(Game::Capabilities::SHAPES), 'shape capability')
  contract_check(
    Game::Runtime.capability?(Game::Capabilities::AUDIO_MIXING),
    'audio mixing capability'
  )
  contract_check(Game::Capabilities::THREE_D == 2048, '3D capability constant')
  contract_check(Game::Runtime.capability?(Game::Capabilities::THREE_D),
                 '3D capability')
  contract_check(Game::Capabilities::THREE_D_MODELS == 4096, 'model capability constant')
  contract_check(Game::Runtime.capability?(Game::Capabilities::THREE_D_MODELS), 'model capability')
  contract_check([Game::ThreeD::Model::MAX_VERTICES, Game::ThreeD::Model::MAX_TRIANGLES,
    Game::ThreeD::Model::MAX_EDGES, Game::ThreeD::Model::MAX_INSTANCES] == [256, 1024, 2048, 128],
    'model limits')
  contract_check(Math.sin(0.0).abs < 0.0001, 'math compatibility')
  contract_check(
    Game::Runtime.capability?(Game::Capabilities::DISPLAY | Game::Capabilities::INPUT),
    'combined capability'
  )
  contract_check(!Game::Runtime.capability?(0), 'empty capability')
  contract_check(Game::Display.width == 320, 'display width')
  contract_check(Game::Display.height == 240, 'display height')
  contract_check(Game::Display.respond_to?(:text), 'display text')
  contract_check(Game::Display.respond_to?(:draw_text), 'display draw_text')
  contract_check(Game::Display.respond_to?(:text_aligned), 'display text alignment')
  contract_check(Game::Display.respond_to?(:set_background), 'display background')
  contract_check(Game::Display.respond_to?(:begin_frame), 'display begin_frame')
  contract_check(Game::Display.respond_to?(:blit_rgb565_region), 'display region blit')
  contract_check(Game::Display.respond_to?(:draw_circle), 'display circle')
  contract_check(Game::Display.respond_to?(:draw_line), 'display line')
  contract_check(Game::ThreeD.respond_to?(:begin_frame), '3D begin frame')
  contract_check(Game::ThreeD.respond_to?(:set_viewport), '3D viewport')
  contract_check(Game::ThreeD.respond_to?(:set_resolution), '3D resolution')
  contract_check(Game::ThreeD.respond_to?(:set_camera), '3D camera')
  contract_check(Game::ThreeD.respond_to?(:draw_triangle), '3D triangle')
  contract_check(Game::ThreeD.respond_to?(:draw_triangles), '3D triangle batch')
  contract_check(Game::ThreeD.respond_to?(:draw_indexed_mesh), '3D indexed mesh')
  contract_check(Game::ThreeD.respond_to?(:draw_wireframe), '3D wireframe')
  contract_check(Game::Time.respond_to?(:millis), 'time millis')
  contract_check(Game::Time.millis.is_a?(Integer), 'time millis return type')
  contract_check(Game::Time.respond_to?(:delta_ms), 'time delta')
  contract_check(Game::Time.delta_ms.is_a?(Integer), 'time delta return type')
  contract_check(Game::Time.delta_ms >= 0, 'time delta non-negative')
  contract_check(
    [Game::Input::UP, Game::Input::DOWN, Game::Input::LEFT, Game::Input::RIGHT,
     Game::Input::A, Game::Input::B, Game::Input::L, Game::Input::R,
     Game::Input::START, Game::Input::SELECT] ==
    [1, 2, 4, 8, 16, 32, 64, 128, 256, 512],
    'input constants'
  )
  contract_check(Game::Input.respond_to?(:pressed?), 'input pressed query')
  contract_check(Game::Input.respond_to?(:just_pressed?), 'input just pressed query')
  contract_check(Game::Input.respond_to?(:just_released?), 'input just released query')
  contract_check(
    [Game::Input::A, Game::Input::B, Game::Input::L, Game::Input::R,
     Game::Input::START, Game::Input::SELECT].all? do |button|
      (Game::Input.pressed?(button) == true ||
       Game::Input.pressed?(button) == false) &&
      (Game::Input.just_pressed?(button) == true ||
       Game::Input.just_pressed?(button) == false) &&
      (Game::Input.just_released?(button) == true ||
       Game::Input.just_released?(button) == false)
    end,
    'input event query return types'
  )
  invalid_button_rejected = false
  begin
    Game::Input.pressed?(0)
  rescue ArgumentError
    invalid_button_rejected = true
  end
  contract_check(invalid_button_rejected, 'invalid input button')
  contract_check(Game::Sprite.respond_to?(:load), 'sprite loader')
  contract_check(Game::Texture.respond_to?(:load), 'texture loader')
  contract_check(Game::Font.respond_to?(:load), 'font loader')
  contract_check(Game::Audio::Sound.respond_to?(:load), 'sound loader')
  contract_check(Game::Audio.respond_to?(:supported?), 'audio capability query')
  contract_check(Game::Audio.respond_to?(:play), 'audio play')
  contract_check(Game::Audio.respond_to?(:play_bgm) && Game::Audio.respond_to?(:play_se) && Game::Audio.respond_to?(:stop_bgm), 'separate audio channels')
  contract_check(Game::Audio.respond_to?(:stop), 'audio stop')
  contract_check(Game::Audio.respond_to?(:busy?), 'audio busy query')
  contract_check(Game::Audio.supported?, 'audio support')
  contract_check(Game::Audio.respond_to?(:dial_supported?), 'audio dial support query')
  contract_check(Game::Audio.respond_to?(:dial_value), 'audio dial value query')
  contract_check(Game::Audio.respond_to?(:master_volume), 'audio master volume query')
  dial = Game::Audio.dial_value
  master = Game::Audio.master_volume
  contract_check(master.is_a?(Integer) && master >= 0 && master <= 255, 'audio master volume range')
  contract_check(Game::Runtime.capability?(Game::Capabilities::AUDIO_DIAL) == Game::Audio.dial_supported?, 'audio dial capability')
  if Game::Audio.dial_supported?
    contract_check(dial.is_a?(Integer) && dial >= 0 && dial <= 4095, 'audio ADC range')
  else
    contract_check(dial.nil? && master == 255, 'audio dial default')
  end

  contract_check(
    Game::Runtime.capability?(Game::Capabilities::AUDIO),
    'audio capability'
  )
  contract_check(Game::Save.respond_to?(:supported?), 'save capability query')
  contract_check(Game::Save.respond_to?(:exists?), 'save exists query')
  contract_check(Game::Save.respond_to?(:read), 'save read')
  contract_check(Game::Save.respond_to?(:write), 'save write')
  contract_check(Game::Save.respond_to?(:delete), 'save delete')
  contract_check(Game::Save.supported?, 'save support')
  contract_check(
    Game::Runtime.capability?(Game::Capabilities::SAVE),
    'save capability'
  )
  contract_check(Game::Collision.respond_to?(:rects_overlap?), 'collision query')
  contract_check(
    Game::Runtime.capability?(Game::Capabilities::COLLISION),
    'collision capability'
  )
  contract_check(
    Game::Collision.rects_overlap?(10, 10, 20, 20, 25, 25, 20, 20) == true,
    'collision overlap'
  )
  contract_check(
    Game::Collision.rects_overlap?(10, 10, 20, 20, 30, 10, 20, 20) == false,
    'collision edge touch'
  )
  contract_check(
    Game::Collision.rects_overlap?(10, 10, 20, 20, 40, 10, 20, 20) == false,
    'collision separated'
  )
  invalid_collision_rejected = false
  begin
    Game::Collision.rects_overlap?(0, 0, 0, 1, 0, 0, 1, 1)
  rescue ArgumentError
    invalid_collision_rejected = true
  end
  contract_check(invalid_collision_rejected, 'invalid collision rectangle')
  invalid_3d_camera_rejected = false
  begin
    Game::ThreeD.set_camera(0, 0, 0, 0, 0, 70, 1, 0.5)
  rescue ArgumentError
    invalid_3d_camera_rejected = true
  end
  contract_check(invalid_3d_camera_rejected, 'invalid 3D camera')
  invalid_3d_viewport_rejected = false
  begin
    Game::ThreeD.set_viewport(320, 0, 1, 1)
  rescue ArgumentError
    invalid_3d_viewport_rejected = true
  end
  contract_check(invalid_3d_viewport_rejected, 'invalid 3D viewport')
  invalid_3d_resolution_rejected = false
  begin
    Game::ThreeD.set_resolution(321, 120)
  rescue ArgumentError
    invalid_3d_resolution_rejected = true
  end
  contract_check(invalid_3d_resolution_rejected, 'invalid 3D resolution')
  invalid_3d_batch_rejected = false
  begin
    Game::ThreeD.draw_triangles([0, 0, 1])
  rescue ArgumentError
    invalid_3d_batch_rejected = true
  end
  contract_check(invalid_3d_batch_rejected, 'invalid 3D triangle batch')
  invalid_3d_indexed_mesh_rejected = false
  begin
    Game::ThreeD.draw_indexed_mesh([0, 0, 1], [0, 1, 2], [0xffff])
  rescue ArgumentError
    invalid_3d_indexed_mesh_rejected = true
  end
  contract_check(invalid_3d_indexed_mesh_rejected, 'invalid 3D indexed mesh')
  invalid_3d_wireframe_rejected = false
  begin
    Game::ThreeD.draw_wireframe([0, 0, 1], [0, 1], 0xffff)
  rescue ArgumentError
    invalid_3d_wireframe_rejected = true
  end
  contract_check(invalid_3d_wireframe_rejected, 'invalid 3D wireframe')
  contract_check(!Game::Audio.busy?, 'audio starts idle')
  contract_check(0.even? && 1.odd? && !1.even?, 'numeric compatibility')

  print '[CONTRACT] init / save', "\n"
  Game.init
  contract_check(Game::Assets.respond_to?(:collect), 'asset collection')
  contract_check(Game::Assets.respond_to?(:resident_bytes), 'asset resident bytes')
  contract_check(Game::Display.set_background(nil).nil?, 'release background')
  Game::Assets.collect
  baseline = Game::Assets.resident_bytes
  contract_live_assets(baseline)
  contract_check(Game::Assets.collect.nil?, 'asset collection return')
  contract_check(Game::Assets.resident_bytes == baseline, 'GC reclaims dead assets')
  contract_check(Game::Time.delta_ms == 0, 'time delta starts at zero')
  save_slot = 'contract'
  save_payload = "SAVE\x00\xffDATA"
  contract_check(Game::Save.delete(save_slot), 'save cleanup')
  contract_check(Game::Save.read(save_slot).nil?, 'missing save')
  contract_check(Game::Save.write(save_slot, save_payload), 'save write result')
  contract_check(Game::Save.exists?(save_slot), 'save exists after write')
  loaded_save = Game::Save.read(save_slot)
  contract_check(loaded_save == save_payload, 'save round trip')
  contract_check(Game::Save.delete(save_slot), 'save delete result')
  contract_check(!Game::Save.exists?(save_slot), 'save absent after delete')
  print '[CONTRACT] display', "\n"
  Game::Display.begin_frame
  Game::Display.clear(0x07e0)
  Game::Display.fill_rect(16, 16, 288, 16, 0xffff)
  Game::Display.fill_rect(16, 48, 96, 96, 0xf800)
  Game::Display.fill_rect(112, 48, 96, 96, 0x001f)
  Game::Display.fill_rect(208, 48, 96, 96, 0xffe0)
  Game::Display.fill_rect(24, 56, 48, 48, 0xf800, 96)
  Game::Display.draw_circle(160, 96, 32, 0xffff, 160)
  Game::Display.draw_line(224, 56, 288, 120, 3, 0x0000, 192)
  print '[CONTRACT] 3D', "\n"
  Game::ThreeD.set_viewport(40, 30, 240, 180)
  Game::ThreeD.set_resolution(160, 120)
  Game::ThreeD.set_camera(0, 0, 0, 0, 0, 70, 0.1, 100)
  Game::ThreeD.begin_frame
  Game::ThreeD.draw_triangle(-1.2, -0.8, 4, 1.2, -0.8, 4, 0, 1.2, 4, 0xffff)
  Game::ThreeD.draw_triangles([
    -0.7, -0.5, 2.0, 0.7, -0.5, 2.0, 0, 0.7, 2.0, 0xf800,
    -0.3, -0.2, 1.5, 0.3, -0.2, 1.5, 0, 0.4, 1.5, 0x001f
  ])
  Game::ThreeD.draw_indexed_mesh(
    [-0.8, -0.6, 3.0, 0.8, -0.6, 3.0, 0.8, 0.6, 3.0, -0.8, 0.6, 3.0],
    # Keep the second triangle non-sequential to exercise indexed lookup.
    [0, 1, 2, 0, 2, 3],
    [0x07e0, 0x001f]
  )
  Game::ThreeD.draw_wireframe(
    [-0.8, -0.6, 3.0, 0.8, -0.6, 3.0, 0.8, 0.6, 3.0, -0.8, 0.6, 3.0],
    [0, 1, 1, 2, 2, 3, 3, 0],
    0xffff
  )
  Game::ThreeD.draw_wireframe(
    [-0.4, -0.3, 2.0, 0.4, -0.3, 2.0], [0, 1], 0xf800, false
  )
  Game::ThreeD.draw_wireframe(
    [-0.2, 0.0, 0.05, 0.2, 0.0, 2.0], [0, 1], 0x07e0
  )
  Game::ThreeD.draw_wireframe(
    [-0.2, 0.0, 0.05, 0.2, 0.0, 0.05], [0, 1], 0x001f
  )
  print '[CONTRACT] texture / sprite / font', "\n"
  texture = Game::Texture.load('texture.raw', 32, 32)
  contract_check(texture.width == 32, 'texture width')
  contract_check(texture.height == 32, 'texture height')
  texture.draw(16, 72)
  texture.draw(224, 72, 0, 0, 32, 32, 64, 64)
  # Non-integer sampling, downscaling, axis-only scaling and clipped regions.
  texture.draw(-7, 180, 3, 5, 21, 13, 53, 37)
  texture.draw(305, 225, 3, 5, 21, 13, 53, 37)
  texture.draw(120, 180, 0, 0, 32, 32, 11, 7)
  texture.draw(144, 180, 4, 4, 16, 16, 16, 47)
  texture.draw(168, 180, 4, 4, 16, 16, 47, 16)
  sprite = Game::Sprite.load('texture.raw', 32, 32, 1)
  contract_check(sprite.respond_to?(:draw_batch), 'sprite batch draw')
  sprite.draw_batch([112, 112, 0, 152, 112, 0])
  font = Game::Font.load('font.pbf')
  contract_check(font.measure('FONT', 12) == 24, 'font measure')
  contract_check(font.measure("FONT\nF", 12, 1) == 27, 'multiline font spacing')
  contract_check(font.measure('', 12) == 0, 'empty font measure')
  contract_check(font.measure("F\tF", 12, 1) == 20, 'font control character fallback')
  font.draw('FONT', 8, 160, 12, 1, 0xffff)
  contract_check('漢字A'.length == 3, 'UTF-8 character count for text alignment')
  contract_check(Game::Display.text_aligned(160, 200, 'かな 漢字 ABC', :center, 0xffff).nil?, 'center aligned text')
  contract_check(Game::Display.text_aligned(320, 215, '右揃え', :right).nil?, 'right aligned text')
  contract_check(Game::Display.text_aligned(0, 215, '左揃え').nil?, 'left aligned text')
  bad_alignment = false
  begin
    Game::Display.text_aligned(0, 0, '文字', :middle)
  rescue ArgumentError
    bad_alignment = true
  end
  contract_check(bad_alignment, 'invalid text alignment')
  Game::Display.draw_text(-4, 220, '日本語', 0xffff)
  print '[CONTRACT] retained models', "\n"
  model_baseline = Game::Assets.resident_bytes
  contract_check(contract_model_lifetime, 'retained model contract')
  Game::Assets.collect
  contract_check(Game::Assets.resident_bytes == model_baseline, 'model GC returns storage')
  font.draw("F\nF", 240, 160, 12, 1, 0xffff)
  Game::Display.present
  print '[CONTRACT] depth resolution switching', "\n"
  2.times do
    [[160, 120], [320, 240], [320, 60], [320, 61], [1, 1], [160, 120]].each do |size|
      contract_check(Game::ThreeD.set_resolution(size[0], size[1]).nil?,
                     'depth resolution switch')
      contract_check(Game::ThreeD.begin_frame.nil?, 'depth frame after switch')
      Game::Display.present
    end
  end
  contract_check(Game::ThreeD.set_resolution(160, 120).nil?, '3D resolution after present')
  contract_check(Game::ThreeD.set_viewport(0, 0, 320, 240).nil?, '3D viewport after present')
  true
end

unless defined?(HARDWARE_SUITE)
  begin
    run_game_api_contract
    print '[CONTRACT] PASS', "\n"
  rescue StandardError => error
    print "[CONTRACT] FAIL #{error.class}: #{error.message}", "\n"
    print error.backtrace.join("\n"), "\n" if error.backtrace
    Game::Display.clear(0xf800)
    Game::Display.text(8, 8, 'CONTRACT FAIL', 0xffff)
    Game::Display.text(8, 32, error.message.to_s[0, 25], 0xffff)
    Game::Display.present
  end
  while true
    Game.tick(2)
  end
end
