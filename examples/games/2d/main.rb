# 2D API example: input, shapes, text, sprites, texture, audio, save data,
# collision, and frame timing in one small playable scene.
require 'game'

BUTTONS = [
  ['UP', Game::Input::UP, 52, 34],
  ['DOWN', Game::Input::DOWN, 82, 34],
  ['LEFT', Game::Input::LEFT, 124, 34],
  ['RIGHT', Game::Input::RIGHT, 166, 34],
  ['A', Game::Input::A, 220, 34],
  ['B', Game::Input::B, 52, 48],
  ['L', Game::Input::L, 82, 48],
  ['R', Game::Input::R, 100, 48],
  ['START', Game::Input::START, 124, 48],
  ['SELECT', Game::Input::SELECT, 178, 48]
]

def pressed_button_names(buttons)
  names = []
  BUTTONS.each do |name, bit, _x, _y|
    names << name if (buttons & bit) != 0
  end
  names.empty? ? 'NONE' : names.join(' ')
end

Game.init
sprite = Game::Sprite.load('document-kun.raw', 16, 16, 2)
texture = Game::Texture.load('document-kun.raw', 32, 16)
beep = Game::Audio::Sound.load('beep.wav') if Game::Audio.supported?

sprite_x = 148
sprite_y = 126
sprite_frame = 0
target_x = 220
target_y = 122
show_shapes = true
frame_counter = 0
fps_started_ms = Game::Time.millis
fps_frames = 0
measured_fps = 0
saved_score = nil
message = 'USE A/B SOUND  L/R OPTIONS'

loop do
  buttons = Game::Input.state
  frame_counter += 1

  now_ms = Game::Time.millis
  fps_frames += 1
  elapsed_ms = now_ms - fps_started_ms
  if elapsed_ms >= 1000
    measured_fps = fps_frames * 1000 / elapsed_ms
    fps_frames = 0
    fps_started_ms = now_ms
  end

  sprite_x -= 2 if (buttons & Game::Input::LEFT) != 0
  sprite_x += 2 if (buttons & Game::Input::RIGHT) != 0
  sprite_y -= 2 if (buttons & Game::Input::UP) != 0
  sprite_y += 2 if (buttons & Game::Input::DOWN) != 0
  sprite_x = 132 if sprite_x < 132
  sprite_x = Game::Display.width - sprite.width - 18 if sprite_x > Game::Display.width - sprite.width - 18
  sprite_y = 92 if sprite_y < 92
  sprite_y = 164 if sprite_y > 164

  if Game::Input.just_pressed?(Game::Input::A)
    beep.play(220) if beep
    message = 'A: BEEP PLAY'
  end
  if Game::Input.just_pressed?(Game::Input::B)
    Game::Audio.stop if beep
    message = 'B: AUDIO STOP'
  end
  if Game::Input.just_pressed?(Game::Input::L)
    sprite_frame = (sprite_frame + 1) % sprite.frames
    message = "L: SPRITE FRAME #{sprite_frame}"
  end
  if Game::Input.just_pressed?(Game::Input::R)
    show_shapes = !show_shapes
    message = show_shapes ? 'R: SHAPES ON' : 'R: SHAPES OFF'
  end
  if Game::Input.just_pressed?(Game::Input::START)
    if Game::Save.supported?
      score = frame_counter / 10
      Game::Save.write('2d-example', score.to_s)
      saved_score = score
      message = "START: SAVED #{score}"
    else
      message = 'START: SAVE UNSUPPORTED'
    end
  end
  if Game::Input.just_pressed?(Game::Input::SELECT)
    if Game::Save.supported?
      value = Game::Save.read('2d-example')
      saved_score = value.nil? ? nil : value.to_i
      message = saved_score ? "SELECT: LOADED #{saved_score}" : 'SELECT: NO SAVE'
    else
      message = 'SELECT: SAVE UNSUPPORTED'
    end
  end

  collision = Game::Collision.rects_overlap?(
    sprite_x, sprite_y, sprite.width, sprite.height,
    target_x, target_y, 28, 28
  )
  sprite_frame = (frame_counter / 20) % sprite.frames if (buttons & Game::Input::L) == 0

  Game::Display.begin_frame
  Game::Display.clear(0x0841)
  Game::Display.text(8, 4, '2D GAME EXAMPLE', 0xffff)
  Game::Display.draw_text(252, 4, "FPS #{measured_fps}", 0x07e0)
  Game::Display.draw_text(8, 20, "PRESSED: #{pressed_button_names(buttons)}", 0xffff)

  BUTTONS.each do |name, bit, x, y|
    color = (buttons & bit) != 0 ? 0x07e0 : 0x7bef
    Game::Display.draw_text(x, y, name, color)
  end

  Game::Display.fill_rect(8, 76, 304, 108, 0x18c3)
  if show_shapes
    # Rectangle, circle, line, and alpha blending examples.
    Game::Display.fill_rect(16, 94, 44, 28, 0xf800, 192)
    Game::Display.fill_rect(42, 110, 44, 28, 0x001f, 128)
    Game::Display.draw_circle(104, 112, 20, 0xffe0, 192)
    Game::Display.draw_line(16, 158, 112, 92, 3, 0xffff, 224)
    Game::Display.draw_line(16, 164, 112, 164, 1, 0x07ff)
  end

  Game::Display.fill_rect(target_x, target_y, 28, 28, collision ? 0xf800 : 0x39e7, 160)
  Game::Display.draw_line(target_x, target_y, target_x + 28, target_y, 2, 0xffff)
  Game::Display.draw_line(target_x + 28, target_y, target_x + 28, target_y + 28, 2, 0xffff)
  Game::Display.draw_line(target_x + 28, target_y + 28, target_x, target_y + 28, 2, 0xffff)
  Game::Display.draw_line(target_x, target_y + 28, target_x, target_y, 2, 0xffff)

  sprite.draw(sprite_x, sprite_y, sprite_frame)
  sprite.draw_batch([282, 150, 0])
  texture.draw(270, 92)

  Game::Display.draw_text(8, 190, "COLLISION #{collision ? 'HIT' : 'MISS'}", collision ? 0xf800 : 0x07e0)
  audio_state = beep && Game::Audio.busy? ? 'PLAYING' : 'IDLE'
  Game::Display.draw_text(188, 190, "AUDIO #{audio_state}", 0xffff)
  Game::Display.draw_text(8, 206, "SAVE #{saved_score || '-'}", 0xffff)
  Game::Display.draw_text(104, 206, message, 0xffff)
  Game::Display.draw_text(8, 222, 'A/B SOUND  L/R OPTIONS', 0x7bef)
  Game::Display.present
  Game.tick(60)
end
