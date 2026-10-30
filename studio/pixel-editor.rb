# The controls are Funicular; pointer strokes and raster I/O use the Canvas bridge.
class PixelEditorComponent < GamingComponent
  PALETTE = %w[#493b32 #fffaf0 #cd654e #efc28f #f4d477 #355438 #8da56d #d5dfb2
               #315c78 #76a9b8 #c0d9db #75446c #bd839b #e9bcc5 #805414 #a99a86]

  def initialize_state
    { ui: { 'width' => 16, 'height' => 16, 'count' => 1, 'selected' => 0,
            'tool' => 'pencil', 'color' => '#cd654e', 'undo' => false, 'redo' => false,
            'playing' => false, 'name' => 'sprite.raw', 'fps' => 8, 'message' => '', 'error' => false, 'apply' => translate_ui('素材に追加', 'Add to assets') } }
  end

  def bridge
    JS.global[:StudioBridge][:pixel]
  end

  def component_mounted
    listen('pixel-state') { |data| patch(ui: data) }
  end

  def component_updated
    bridge.renderCanvases if bridge
  end

  def action(name)
    bridge.action(name)
  end

  def tools
    tag(:div, class: 'pixel-tools', role: 'group', 'aria-label' => translate_ui('描画ツール', 'Drawing tools')) do
      [['pencil', translate_ui('鉛筆', 'Pencil'), 'B'], ['eraser', translate_ui('消しゴム', 'Eraser'), 'E'], ['fill', translate_ui('塗る', 'Fill'), 'F'], ['picker', translate_ui('色を取る', 'Pick color'), 'I']].each do |tool, label, key|
        tag(:button, class: 'button', type: 'button', 'data-pixel-tool' => tool,
           'aria-pressed' => (state[:ui]['tool'] == tool).to_s, title: key,
           onclick: -> { bridge.setTool(tool) }) { label }
      end
      tag(:button, id: 'pixel-undo', class: 'button', type: 'button', disabled: !state[:ui]['undo'], onclick: -> { action('undo') }) { translate_ui('戻す', 'Undo') }
      tag(:button, id: 'pixel-redo', class: 'button', type: 'button', disabled: !state[:ui]['redo'], onclick: -> { action('redo') }) { translate_ui('やり直す', 'Redo') }
    end
  end

  def palette
    tag(:div, id: 'pixel-palette', class: 'pixel-palette', role: 'group', 'aria-label' => translate_ui('色', 'Color')) do
      tag(:label, class: 'pixel-color-label') do
        tag(:span) { translate_ui('色', 'Color') }
        tag(:input, id: 'pixel-color', type: 'color', value: '#cd654e', 'aria-label' => translate_ui('描画色', 'Drawing color'),
           oninput: ->(event) { bridge.setColor(event.target[:value].to_s) })
      end
      PALETTE.each do |color|
        tag(:button, class: 'pixel-swatch', type: 'button', style: "--swatch: #{color}", 'data-color' => color,
           'aria-label' => color, 'aria-pressed' => (state[:ui]['color'] == color).to_s,
           onclick: -> { bridge.setColor(color) })
      end
    end
  end

  def frames
    tag(:div, id: 'pixel-frames', class: 'pixel-frames', role: 'group', 'aria-label' => translate_ui('フレーム', 'Frames')) do
      state[:ui]['count'].times do |index|
        tag(:button, class: 'pixel-frame', type: 'button', 'aria-label' => translate_ui("フレーム#{index + 1}", "Frame #{index + 1}"),
           'aria-pressed' => (state[:ui]['selected'] == index).to_s, onclick: -> { bridge.selectFrame(index) }) do
          tag(:canvas, 'data-frame' => index, 'aria-hidden' => 'true')
          tag(:span) { (index + 1).to_s }
        end
      end
    end
  end

  def frame_actions
    tag(:div, class: 'pixel-frame-actions') do
      [['add', translate_ui('追加', 'Add'), state[:ui]['count'] >= 32], ['duplicate', translate_ui('複製', 'Duplicate'), state[:ui]['count'] >= 32],
       ['delete', translate_ui('削除', 'Delete'), state[:ui]['count'] == 1], ['left', '←', state[:ui]['selected'] == 0],
       ['right', '→', state[:ui]['selected'] == state[:ui]['count'] - 1]].each do |name, label, disabled|
        attrs = { id: "pixel-#{name}", class: 'button', type: 'button', disabled: disabled,
                  onclick: -> { action(name) } }
        attrs['aria-label'] = name == 'left' ? translate_ui('フレームを左へ', 'Move frame left') : translate_ui('フレームを右へ', 'Move frame right') if ['left', 'right'].include?(name)
        tag(:button, attrs) { label }
      end
    end
  end

  def example_code
    ["require 'game'", 'Game.init',
     "sprite = Game::Sprite.load('#{state[:ui]['name']}', #{state[:ui]['width']}, #{state[:ui]['height']}, #{state[:ui]['count']})",
     '', 'loop do', '  Game::Display.clear(0xff9b)',
     "  frame = (Game::Time.millis * #{state[:ui]['fps']} / 1000) % #{state[:ui]['count']}",
     '  sprite.draw(100, 80, frame)', '  Game::Display.present', '  Game.tick(30)', 'end'].join("\n")
  end

  def render
    tag(:dialog, { "id" => "pixel-dialog", "class" => "pixel-dialog", "aria-labelledby" => "pixel-title" }) do
      tag(:header, { "class" => "panel-heading" }) do
        tag(:h2, { "id" => "pixel-title" }) { translate_ui('ドット絵', 'Pixel art') }
        tag(:button, { "id" => "pixel-close", "class" => "button", "type" => "button", onclick: -> { bridge.close } }) { translate_ui('閉じる', 'Close') }
      end
      tag(:div, { "class" => "pixel-body" }) do
        tag(:div, { "class" => "pixel-settings" }) do
          tag(:label) do
            tag(:span) { translate_ui('名前', 'Name') }
            tag(:input, { "id" => "pixel-name", "value" => "sprite", "maxlength" => "64", "autocomplete" => "off", oninput: -> { bridge.rename } })
          end
          tag(:form, { "id" => "pixel-size", "class" => "pixel-size", onsubmit: ->(event) { event.preventDefault; action("resize") } }) do
            tag(:label) do
              tag(:span) { translate_ui('幅', 'Width') }
              tag(:input, { "id" => "pixel-width", "type" => "number", "min" => "1", "max" => "64", "value" => "16", "required" => true })
            end
            tag(:span, { "aria-hidden" => "true" }) { "×" }
            tag(:label) do
              tag(:span) { translate_ui('高さ', 'Height') }
              tag(:input, { "id" => "pixel-height", "type" => "number", "min" => "1", "max" => "64", "value" => "16", "required" => true })
            end
            tag(:button, { "class" => "button", "type" => "submit" }) { translate_ui('サイズ変更', 'Resize') }
          end
        end
        tag(:div, { "class" => "pixel-workspace" }) do
          tag(:section, { "aria-label" => translate_ui('描画', 'Drawing') }) do
            tools
            palette
            tag(:div, { "class" => "pixel-drawing" }) do
              tag(:canvas, { "id" => "pixel-canvas", "width" => "384", "height" => "384", "tabindex" => "0", "role" => "img", "aria-label" => translate_ui('ドット絵。矢印キーで移動、Spaceで描画', 'Pixel art. Use arrow keys to move and Space to draw') })
            end
            tag(:div, { "class" => "pixel-options" }) do
              tag(:label) do
                tag(:input, { "id" => "pixel-grid", "type" => "checkbox", "checked" => true, onchange: -> { bridge.renderDrawing } })
                tag(:span) { translate_ui('グリッド', 'Grid') }
              end
              tag(:label) do
                tag(:input, { "id" => "pixel-onion", "type" => "checkbox", onchange: -> { bridge.renderDrawing } })
                tag(:span) { translate_ui('前のコマ', 'Previous frame') }
              end
            end
          end
          tag(:section, { "class" => "pixel-animation", "aria-label" => translate_ui('アニメーション', 'Animation') }) do
            tag(:h3) { translate_ui('アニメーション', 'Animation') }
            tag(:canvas, { "id" => "pixel-preview", "width" => "128", "height" => "128", "role" => "img", "aria-label" => translate_ui('アニメーションのプレビュー', 'Animation preview') })
            tag(:div, { "class" => "pixel-playback" }) do
              tag(:button, { "id" => "pixel-play", "class" => "button", "type" => "button", "aria-pressed" => state[:ui]["playing"].to_s, onclick: -> { bridge.togglePlay } }) { state[:ui]["playing"] ? translate_ui('停止', 'Stop') : translate_ui('再生', 'Play') }
              tag(:label) do
                tag(:input, { "id" => "pixel-fps", "type" => "number", "min" => "1", "max" => "24", "value" => "8", "aria-label" => translate_ui('再生速度', 'Playback speed'), "required" => true, onchange: -> { bridge.changeFps } })
                tag(:span) { "fps" }
              end
              tag(:span, { "id" => "pixel-position" }) { "1 / 1" }
            end
            frames
            frame_actions
            tag(:details, { "class" => "pixel-code-help" }) do
              tag(:summary) { "Ruby" }
              tag(:pre) do
                tag(:code, { "id" => "pixel-code" }) { example_code }
              end
            end
          end
        end
        tag(:p, { "id" => "pixel-status", "class" => "pixel-status #{state[:ui]['error'] ? 'error' : ''}", "role" => "status", "aria-live" => "polite" }) { state[:ui]["message"] }
      end
      tag(:footer, { "class" => "pixel-footer" }) do
        tag(:label, { "class" => "button upload-label" }) do
          tag(:span) { translate_ui('PNGを開く', 'Open PNG') }
          tag(:input, { "id" => "pixel-import", "class" => "file-input", "type" => "file", "accept" => ".png", "aria-label" => translate_ui('PNGを開く', 'Open PNG'), onchange: -> { bridge.importPng } })
        end
        tag(:button, { "id" => "pixel-download", "class" => "button", "type" => "button", onclick: -> { bridge.download } }) { translate_ui('PNG保存', 'Save PNG') }
        tag(:button, { "id" => "pixel-apply", "class" => "button button-primary", "type" => "button", onclick: -> { bridge.apply } }) { state[:ui]["apply"] }
      end
    end
  end
end
