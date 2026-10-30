# APIs implemented in Ruby here are shared by the RP2350 and WASM backends.
module Game
  module Display
    # x is the left edge, center, or right edge of the text's 12px cells.
    def self.text_aligned(x, y, text, alignment = :left, color = 0xffff)
      unless x.is_a?(Integer) && y.is_a?(Integer) && color.is_a?(Integer)
        raise TypeError, 'coordinates and color must be integers'
      end
      raise TypeError, 'text must be a string' unless text.is_a?(String)
      raise ArgumentError, 'text must not contain NUL' if text.include?("\0")
      unless color >= 0 && color <= 0xffff
        raise ArgumentError, 'RGB565 color must be 0..65535'
      end
      unless x >= -2147483648 && x <= 2147483647 && y >= -2147483648 && y <= 2147483647
        raise ArgumentError, 'coordinates must fit in 32 bits'
      end
      case alignment
      when :left
        offset = 0
      when :center
        offset = text.length * 6
      when :right
        offset = text.length * 12
      else
        raise ArgumentError, 'alignment must be :left, :center, or :right'
      end
      start_x = x - offset
      start_x = -2147483648 if start_x < -2147483648
      text(start_x, y, text, color)
      nil
    end
  end
end
