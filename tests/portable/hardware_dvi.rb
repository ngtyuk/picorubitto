# Standalone display cartridge. Draw in the shared 320x240 RGB565 coordinates.
require 'game'

module HardwareDVI
  PATTERNS = ['RGB565 COLORS', 'BORDER AND GRID', '1 PX PATTERNS', 'MOTION']
  COLORS = [0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0x0000]
  COLOR_NAMES = ['W', 'Y', 'C', 'G', 'M', 'R', 'B', 'K']
  PAGE_MS = 5000

  def self.color_bars
    COLORS.each_with_index do |color, index|
      x = index * 40
      Game::Display.fill_rect(x, 28, 40, 64, color)
      Game::Display.text(x + 16, 96, COLOR_NAMES[index], 0xffff)
    end

    Game::Display.text(12, 118, 'R', 0xffff)
    Game::Display.text(12, 141, 'G', 0xffff)
    Game::Display.text(12, 164, 'B', 0xffff)
    Game::Display.text(12, 187, 'W', 0xffff)
    64.times do |step|
      x = 32 + step * 4
      rb = step >> 1
      Game::Display.fill_rect(x, 118, 4, 16, rb << 11)
      Game::Display.fill_rect(x, 141, 4, 16, step << 5)
      Game::Display.fill_rect(x, 164, 4, 16, rb)
      Game::Display.fill_rect(x, 187, 4, 16, (rb << 11) | (step << 5) | rb)
    end
  end

  def self.border_and_grid
    Game::Display.fill_rect(0, 0, 320, 1, 0xffff)
    Game::Display.fill_rect(0, 239, 320, 1, 0xffff)
    Game::Display.fill_rect(0, 1, 1, 238, 0xffff)
    Game::Display.fill_rect(319, 1, 1, 238, 0xffff)
    Game::Display.fill_rect(1, 1, 8, 8, 0xf800)
    Game::Display.fill_rect(311, 1, 8, 8, 0x07e0)
    Game::Display.fill_rect(1, 231, 8, 8, 0x001f)
    Game::Display.fill_rect(311, 231, 8, 8, 0xffe0)

    7.times do |column|
      Game::Display.fill_rect(40 + column * 40, 40, 1, 161, 0x7bef)
    end
    5.times do |row|
      Game::Display.fill_rect(40, 40 + row * 40, 241, 1, 0x7bef)
    end
    Game::Display.fill_rect(144, 120, 33, 1, 0xffff)
    Game::Display.fill_rect(160, 104, 1, 33, 0xffff)
    Game::Display.fill_rect(0, 120, 12, 1, 0xffff)
    Game::Display.fill_rect(308, 120, 12, 1, 0xffff)
    Game::Display.fill_rect(160, 0, 1, 12, 0xffff)
    Game::Display.fill_rect(160, 228, 1, 12, 0xffff)
  end

  def self.pixel_patterns
    [24, 128, 232].each do |x|
      Game::Display.fill_rect(x - 1, 79, 66, 66, 0x7bef)
      Game::Display.fill_rect(x, 80, 64, 64, 0x0000)
    end
    32.times do |index|
      Game::Display.fill_rect(24 + index * 2, 80, 1, 64, 0xffff)
      Game::Display.fill_rect(128, 80 + index * 2, 64, 1, 0xffff)
    end
    64.times do |row|
      32.times do |column|
        Game::Display.fill_rect(232 + column * 2 + (row & 1), 80 + row, 1, 1, 0xffff)
      end
    end
    Game::Display.text(24, 154, 'V STRIPE', 0xffff)
    Game::Display.text(128, 154, 'H STRIPE', 0xffff)
    Game::Display.text(232, 154, 'CHECKER', 0xffff)
  end

  def self.motion(phase)
    x = 8 + (phase <= 300 ? phase : 600 - phase)
    7.times do |column|
      Game::Display.fill_rect(40 + column * 40, 32, 1, 176, 0x39e7)
    end
    Game::Display.fill_rect(x, 32, 3, 176, 0xffff)
    Game::Display.fill_rect(x - 4, 32, 11, 8, 0x07e0)
    Game::Display.fill_rect(x - 4, 200, 11, 8, 0x07e0)
  end

  def self.draw(page, automatic, phase)
    Game::Display.begin_frame
    Game::Display.clear(0x0000)
    Game::Display.text(16, 13, PATTERNS[page], 0xffff)
    Game::Display.text(260, 13, automatic ? 'AUTO' : 'HOLD', 0x07ff)
    Game::Display.text(16, 215, 'A NEXT B PREV START MODE', 0xffff)
    case page
    when 0 then color_bars
    when 1 then border_and_grid
    when 2 then pixel_patterns
    when 3 then motion(phase)
    end
    Game::Display.present
  end

  def self.run
    Game.init
    page = 0
    automatic = true
    phase = 0
    page_started = Game::Time.millis
    redraw = true

    loop do
      if redraw
        print "[DVI] #{PATTERNS[page]} #{automatic ? 'AUTO' : 'HOLD'}", "\n"
      end
      if redraw || page == 3
        draw(page, automatic, phase)
        phase = (phase + 3) % 600 if page == 3
      end
      redraw = false
      Game.tick(60)

      now = Game::Time.millis
      if Game::Input.just_pressed?(Game::Input::A)
        page = (page + 1) % PATTERNS.length
        automatic = false
        redraw = true
      elsif Game::Input.just_pressed?(Game::Input::B)
        page = (page - 1) % PATTERNS.length
        automatic = false
        redraw = true
      elsif Game::Input.just_pressed?(Game::Input::START)
        automatic = !automatic
        redraw = true
      elsif automatic && ((now - page_started) & 0xffffffff) >= PAGE_MS
        page = (page + 1) % PATTERNS.length
        redraw = true
      end
      if redraw
        page_started = now
        phase = 0
      end
    end
  end
end

HardwareDVI.run
