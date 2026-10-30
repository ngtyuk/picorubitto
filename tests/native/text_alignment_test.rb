# Exercise the shared implementation and its exact coordinates without a display.
require_relative '../../mrbgems/picoruby-game-common/mrblib/game_common'
module Game::Display
  class << self
    attr_accessor :drawn
    def text(x, y, value, color)
      self.drawn = [x, y, value, color]
      nil
    end
  end
end

def check(expected, actual)
  raise "expected #{expected.inspect}, got #{actual.inspect}" unless expected == actual
end

def rejects(error)
  begin
    yield
  rescue error
    return
  end
  raise "expected #{error}"
end

d = Game::Display
check(nil, d.text_aligned(160, 20, '日本A', :center))
check([142, 20, '日本A', 0xffff], d.drawn)
d.text_aligned(320, 4, '漢字', :right, 0x1234)
check([296, 4, '漢字', 0x1234], d.drawn)
d.text_aligned(-2, 3, 'abc')
check([-2, 3, 'abc', 0xffff], d.drawn)
d.text_aligned(0, 3, '', :right)
check([0, 3, '', 0xffff], d.drawn)
d.text_aligned(-2147483648, 3, '字', :right)
check([-2147483648, 3, '字', 0xffff], d.drawn)
rejects(ArgumentError) { d.text_aligned(0, 0, 'x', :middle) }
rejects(ArgumentError) { d.text_aligned(0, 0, "x\0y") }
rejects(ArgumentError) { d.text_aligned(2147483648, 0, 'x') }
rejects(ArgumentError) { d.text_aligned(0, 0, 'x', :left, -1) }
rejects(TypeError) { d.text_aligned(0, 0, 1) }
rejects(TypeError) { d.text_aligned(0.5, 0, 'x') }
puts 'text alignment: PASS'
