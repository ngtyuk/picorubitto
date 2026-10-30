# Bundled after game_api_contract.rb by tools/build/build_checks.rb.
# Use core Kernel#print for logs; the native minimum profile has no puts.
module HardwareChecks
  TESTS = [
    ['RUBY / VM', :ruby_check],
    ['COMMON API', :api_check],
    ['2D / TEXTURE', :display_check],
    ['3D / RESOLUTION', :three_d_check],
    ['BUTTONS', :input_check],
    ['AUDIO', :audio_check],
    ['SAVE', :save_check],
    ['ASSET / GC', :asset_gc_check],
    ['FONT / CLIP', :font_check],
    ['PERF / 2D', :performance_2d_check],
    ['PERF / 3D', :performance_3d_check],
    ['PERF / RENDER', :performance_renderer_check],
    ['PERF / VM', :performance_vm_check],
    ['PERF / TRI', :performance_triangles_check],
    ['AUDIO / MIX', :audio_mix_check],
    ['AUDIO / DIAL', :audio_dial_check],
    ['AUDIO / RATE', :audio_rate_check]
  ]
  @results = {}
  @selected = 0

  def self.check(value, message)
    raise message unless value
  end

  def self.heading(title)
    Game::Display.clear(0xff9b)
    Game::Display.text(8, 8, title, 0x4328)
  end

  def self.footer(text)
    Game::Display.fill_rect(0, 218, 320, 22, 0xff9b)
    Game::Display.text(8, 222, text, 0x4328)
    Game::Display.present
  end

  def self.wait_choice
    while Game::Input.pressed?(Game::Input::A) || Game::Input.pressed?(Game::Input::B)
      Game.tick(30)
    end
    while true
      Game.tick(30)
      return true if Game::Input.just_pressed?(Game::Input::A)
      return false if Game::Input.just_pressed?(Game::Input::B)
    end
  end

  def self.ruby_check
    check((1 << 40) + 7 == 1099511627783, '64-bit integer')
    check(Math.sin(0.0).abs < 0.00001, 'Math.sin')
    check((1.25 * 4.0 - 5.0).abs < 0.00001, 'Float')
    check([1, 2, 3].map { |n| n * 2 }.join(',') == '2,4,6', 'Array/block/String')
    check({ 'key' => 42 }['key'] == 42, 'Hash')
    count = 0
    loop do
      count += 1
      break if count == 3
    end
    check(count == 3, 'Kernel.loop')
    begin
      raise ArgumentError, 'expected'
    rescue ArgumentError => error
      check(error.message == 'expected', 'exception')
    end
    GC.start
    true
  end

  def self.api_check
    run_game_api_contract
  end

  def self.display_check
    @texture ||= Game::Texture.load('texture.raw', 32, 32)
    @font ||= Game::Font.load('font.pbf')
    @sprite ||= Game::Sprite.load('document-kun.raw', 16, 16, 2)
    heading('2D / TEXTURE')
    Game::Display.fill_rect(16, 40, 64, 48, 0xf800)
    Game::Display.draw_circle(124, 64, 24, 0x07e0, 192)
    Game::Display.draw_line(170, 40, 288, 88, 3, 0x001f)
    @texture.draw(8, 104)
    @texture.draw(48, 104, 0, 0, 32, 32, 64, 64)
    @texture.draw(124, 104, 3, 5, 21, 13, 53, 37)
    @texture.draw(188, 104, 0, 0, 32, 32, 11, 7)
    @texture.draw(-7, 178, 3, 5, 21, 13, 53, 37)
    @texture.draw(305, 178, 3, 5, 21, 13, 53, 37)
    @sprite.draw_batch([232, 110, 0, 264, 110, 1])
    @font.draw('FONT', 104, 182, 12, 1, 0x4328)
    :visual
  end

  def self.three_d_check
    vertices = [-0.8, -0.5, 3.0, 0.8, -0.5, 3.0, 0.0, 0.8, 3.0]
    edges = [0, 1, 1, 2, 2, 0]
    [[160, 120], [320, 240], [160, 120]].each do |size|
      width, height = size
      print "[CHECK] 3D resolution=#{width}x#{height}", "\n"
      Game::ThreeD.set_viewport(0, 0, 320, 240)
      Game::ThreeD.set_resolution(width, height)
      started = Game::Time.millis
      30.times do |frame|
        Game::Display.clear(0x0841)
        Game::ThreeD.set_camera(0, 0, 0, (frame - 15) * 0.008, 0, 70, 0.1, 100)
        Game::ThreeD.begin_frame
        Game::ThreeD.draw_indexed_mesh(vertices, [0, 1, 2], [0x001f])
        Game::ThreeD.draw_wireframe(vertices, edges, 0xffff, false)
        Game::Display.text(8, 8, "3D #{width}X#{height}", 0xffff)
        Game::Display.present
        Game.tick(30)
      end
      print "[CHECK] 3D frames=30 elapsed_ms=#{Game::Time.millis - started}", "\n"
    end
    :visual
  end

  def self.input_check
    names = ['UP', 'DOWN', 'LEFT', 'RIGHT', 'A', 'B', 'L', 'R', 'START', 'SELECT']
    seen = 0
    while true
      Game.tick(30)
      seen |= Game::Input.state
      10.times do |index|
        bit = 1 << index
        seen |= bit if Game::Input.just_pressed?(bit)
      end
      heading('BUTTONS')
      10.times do |index|
        color = (seen & (1 << index)) == 0 ? 0x8410 : 0x2566
        Game::Display.text(16 + (index / 5) * 152, 42 + (index % 5) * 30, names[index], color)
      end
      footer('START: FINISH')
      break if Game::Input.just_pressed?(Game::Input::START)
    end
    print "[CHECK] buttons seen=0x#{seen.to_s(16)} expected=0x3ff", "\n"
    check(seen == 1023, "buttons seen=0x#{seen.to_s(16)}")
    true
  end

  def self.audio_check
    HardwareAudio.run
  end

  def self.audio_dial_check
    HardwareAudio.run(:dial)
  end

  def self.audio_mix_check
    HardwareAudio.run(:mix)
  end

  def self.audio_rate_check
    HardwareAudio.run(:rate)
  end

  def self.save_check
    slot = 'hardware-check'
    previous = Game::Save.read(slot)
    begin
      payload = "CHECK\x00\xff"
      check(Game::Save.write(slot, payload), 'save write')
      check(Game::Save.read(slot) == payload, 'save round trip')
      check(Game::Save.delete(slot), 'save delete')
      check(!Game::Save.exists?(slot), 'save exists after delete')
    ensure
      if previous.nil?
        Game::Save.delete(slot)
      else
        Game::Save.write(slot, previous)
      end
    end
    true
  end

  def self.asset_gc_scene(index, round, baseline)
    texture = Game::Texture.load('large/scene-' + index.to_s + '.raw', 512, 512)
    sprite = Game::Sprite.load('document-kun.raw', 16, 16, 2)
    font = Game::Font.load('font.pbf')
    sound = Game::Audio::Sound.load('beep.wav')
    GC.start
    check(Game::Assets.resident_bytes >= baseline + 524288, 'GC lost a live asset')
    check(texture.checksum == ASSET_GC_CHECKSUMS[index], 'large texture checksum')
    heading('ASSET / GC')
    texture.draw(0, 28, 0, 0, 512, 512, 320, 180)
    sprite.draw(16, 48, index % 2)
    font.draw('GC', 48, 48, 12, 1, 0xffff)
    footer('ROUND ' + (round + 1).to_s + '  IMAGE ' + (index + 1).to_s + '/32')
    if index == 0 && round == 0
      sound.play(128)
      sound = nil
      GC.start
      10.times { Game.tick(30) }
    end
    Game.tick(30)
    check(!Game::Input.just_pressed?(Game::Input::B), 'asset test cancelled')
    nil
  end

  def self.asset_gc_check
    Game::Audio.stop
    Game::Display.set_background(nil)
    Game::Assets.collect
    baseline = Game::Assets.resident_bytes
    total = 0
    3.times do |round|
      32.times do |index|
        asset_gc_scene(index, round, baseline)
        if round == 0
          GC.start
        else
          Game::Assets.collect
        end
        resident = Game::Assets.resident_bytes
        check(resident == baseline, 'asset data was not reclaimed')
        total += 524288
        print "[CHECK] GC round=#{round + 1} image=#{index + 1}/32 read=#{total} resident=#{resident} baseline=#{baseline}", "\n"
      end
    end
    check(total == 50331648, 'large asset byte count')
    true
  ensure
    Game::Audio.stop
    Game::Assets.collect
  end

  def self.font_reference(text, x, y, scale, spacing, color)
    patterns = [[2, 1, 89, 9, 6], [126, 17, 17, 17, 126], [127, 73, 73, 73, 54]]
    text.each_byte do |character|
      if character == 32
        x += 3 * scale + spacing
        next
      end
      index = character == 65 ? 1 : (character == 66 ? 2 : 0)
      left = x - (character == 65 ? scale : 0)
      top = y + (character == 66 ? 1 : 2) * scale
      5.times do |column|
        7.times do |row|
          if (patterns[index][column] & (1 << row)) != 0
            alpha = character == 66 ? 255 : [64, 128, 192, 255, 255][column]
            Game::Display.fill_rect(left + column * scale, top + row * scale, scale, scale, color, alpha)
          end
        end
      end
      x += 6 * scale + spacing
    end
  end

  def self.font_check
    font = Game::Font.load('font-check.pbf')
    check(font.measure('A B?', 12, 1) == 24, 'font spacing')
    check(font.measure("A\nB", 12, 1) == 6, 'font multiline')
    check(font.measure('A B?', 12, -1) == 18, 'font negative spacing')
    check(font.measure('A B?', 24, 1) == 45, 'font second size')
    check(font.measure('~', 12) == font.measure('?', 12), 'font fallback')
    check(font.measure("A\tB", 12) == 18, 'font control character fallback')
    check(font.measure('', 12) == 0, 'font empty text')
    accepted = false
    begin
      Game::Font.load('font-invalid.pbf')
      accepted = true
    rescue StandardError
    end
    check(!accepted, 'invalid font accepted')
    3.times do |round|
      started = Game::Time.millis
      sum = 0
      2000.times { sum += font.measure('A B?', 12, 1) }
      check(sum == 48000, 'font repeated measure')
      measured = Game::Time.millis - started
      started = Game::Time.millis
      300.times { |index| font.draw('A B?', 8, 32, 12, 1, (index * 97) % 65536) }
      drawn = Game::Time.millis - started
      print "[CHECK] FONT round=#{round + 1} measure_2000_ms=#{measured} draw_300_ms=#{drawn}", "\n"
      Game::Assets.collect
    end
    heading('FONT / CLIP')
    Game::Display.text(8, 28, 'TOP FONT / BELOW REF', 0x4328)
    Game::Display.text(8, 205, '日本語 漢字 山川海 A3', 0x4328)
    [1, 2].each do |scale|
      top = scale == 1 ? 54 : 116
      font.draw('A B?~', 32, top, scale * 12, 1, 0x001f)
      font_reference('A B?~', 32, top + 24, scale, 1, 0x001f)
    end
    font.draw("A\nB", 180, 54, 12, 1, 0x07e0)
    font_reference('A', 180, 86, 1, 1, 0x07e0)
    font_reference('B', 180, 98, 1, 1, 0x07e0)
    font.draw('A B?', -3, 184, 24, 1, 0xf800)
    font.draw('A B?', 315, 184, 24, 1, 0xf800)
    font.draw('A B?', 220, -8, 24, 1, 0x001f)
    font.draw('A B?', 220, 235, 24, 1, 0x001f)
    font.draw('', 20, 20, 12)
    font.draw('A', 100000, -100000, 24)
    :visual
  end

  def self.performance_2d_check
    @sprite = @texture = @font = @sound = nil
    HardwarePerformance.run_2d
  ensure
    Game::Assets.collect
  end

  def self.performance_3d_check
    @sprite = @texture = @font = @sound = nil
    HardwarePerformance.run_3d
  ensure
    Game::Assets.collect
  end

  def self.performance_renderer_check
    @sprite = @texture = @font = @sound = nil
    HardwarePerformance.run_renderer
  ensure
    Game::Assets.collect
  end

  def self.menu_first_row(selected)
    selected < 10 ? 0 : selected - 9
  end

  def self.performance_vm_check
    @sprite = @texture = @font = @sound = nil
    HardwareVMPerformance.run
  ensure
    Game::Assets.collect
  end

  def self.performance_triangles_check
    @sprite = @texture = @font = @sound = nil
    HardwarePerformance.run_triangles
  ensure
    Game::Assets.collect
  end

  def self.run(index)
    title, method = TESTS[index]
    print "[CHECK] BEGIN #{index + 1} #{title}", "\n"
    heading(title)
    Game::Display.text(8, 48, 'RUNNING', 0x4328)
    Game::Display.present
    begin
      result = case method
               when :ruby_check then ruby_check
               when :api_check then api_check
               when :display_check then display_check
               when :three_d_check then three_d_check
               when :input_check then input_check
               when :audio_check then audio_check
               when :audio_mix_check then audio_mix_check
               when :audio_dial_check then audio_dial_check
               when :audio_rate_check then audio_rate_check
               when :save_check then save_check
               when :asset_gc_check then asset_gc_check
               when :font_check then font_check
               when :performance_2d_check then performance_2d_check
               when :performance_3d_check then performance_3d_check
               when :performance_renderer_check then performance_renderer_check
               when :performance_vm_check then performance_vm_check
               when :performance_triangles_check then performance_triangles_check
               else raise ArgumentError, 'unknown check'
               end
      if result == :visual
        if method == :font_check
          # Keep the bottom-edge glyph visible while awaiting confirmation.
          Game::Display.text(8, 208, 'A: OK  B: FAIL', 0x4328)
          Game::Display.present
        else
          footer('A: OK  B: FAIL')
        end
        result = wait_choice
        @results[index] = result ? 'OK' : 'FAIL'
        print "[CHECK] #{@results[index]} #{title} (user confirmation)", "\n"
        return true
      end
      if result == :cancelled
        @results[index] = 'STOP'
        print "[CHECK] CANCEL #{title}", "\n"
        return false
      elsif result == :skipped
        @results[index] = 'SKIP'
        print "[CHECK] SKIP #{title} (dial disabled)", "\n"
      elsif result == :measured
        @results[index] = 'MEAS'
        heading(title)
        Game::Display.text(8, 48, 'MEASURED', 0x2566)
        if method == :performance_vm_check
          HardwareVMPerformance.summary
        else
          HardwarePerformance.summary
        end
        print "[CHECK] MEASURED #{title}", "\n"
      else
        @results[index] = 'PASS'
        print "[CHECK] PASS #{title}", "\n"
      end
    rescue StandardError => error
      @results[index] = 'FAIL'
      print "[CHECK] FAIL #{title}: #{error.class}: #{error.message}", "\n"
      print error.backtrace.join("\n"), "\n" if error.backtrace
      heading(title)
      Game::Display.text(8, 64, 'FAIL', 0xf800)
      Game::Display.text(8, 92, error.message.to_s[0, 25], 0x4328)
    end
    if @results[index] == 'PASS'
      heading(title)
      Game::Display.text(8, 64, 'PASS', 0x2566)
    end
    footer('A: NEXT  B: MENU')
    wait_choice
  end

  def self.run_all
    index = 0
    while index < TESTS.length
      break unless run(index)
      index += 1
    end
  end

  def self.start
    Game.init
    profile = defined?(HARDWARE_PERFORMANCE_PROFILE) ? HARDWARE_PERFORMANCE_PROFILE : 'standard'
    print "[CHECK] hardware suite ready profile=#{profile}", "\n"
    while true
      heading(profile == 'comparison' ? 'HARDWARE COMPARE' : 'HARDWARE CHECKS')
      rows = TESTS.length + 1
      first = menu_first_row(@selected)
      [rows - first, 10].min.times do |visible|
        row = first + visible
        title = row == 0 ? 'RUN ALL' : TESTS[row - 1][0]
        status = row == 0 ? '' : (@results[row - 1] || '-')
        color = row == @selected ? 0xcb29 : 0x4328
        Game::Display.text(8, 34 + visible * 18, "#{row == @selected ? '-' : ' '}#{title}", color)
        Game::Display.text(264, 34 + visible * 18, status, color)
      end
      footer('UP/DOWN  A: RUN')
      Game.tick(30)
      @selected = (@selected + rows - 1) % rows if Game::Input.just_pressed?(Game::Input::UP)
      @selected = (@selected + 1) % rows if Game::Input.just_pressed?(Game::Input::DOWN)
      next unless Game::Input.just_pressed?(Game::Input::A)
      if @selected == 0
        run_all
      else
        run(@selected - 1)
      end
    end
  end
end

HardwareChecks.start unless defined?(HARDWARE_SUITE_LIBRARY)
