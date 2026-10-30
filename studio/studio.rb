# Browser Studio: Funicular owns the page, actions, assets and diagnostics.
class StudioComponent < GamingComponent
  def initialize_state
    { ui: { "build" => "準備中", "preview" => "待機中", "save" => "自動保存",
            "kind" => "", "busy" => false, "assets" => [], "diagnostics" => [], "samples" => [] } }
  end

  def bridge
    JS.global[:StudioBridge]
  end

  def component_mounted
    @busy = false
    @asset_errors = []
    listen('studio-state') { |data| update_view(data) }
    listen('studio-asset-list') { |assets| update_view('assets' => assets) }
    listen('studio-assets') do |errors|
      @asset_errors = errors
      diagnostics(errors, '構文OK')
    end
    listen('studio-check') { check_source }
    listen('studio-runtime-error') do |data|
      update_view('preview' => '実行エラー')
      diagnostics([{ 'severity' => 'error', 'line' => 0, 'message' => data['message'] }])
    end
    bridge.start
  end

  def update_view(values)
    patch(ui: state[:ui].merge(values))
  end

  def diagnostics(items, fallback = 'OK')
    items = [{ 'severity' => 'ok', 'line' => 0, 'message' => fallback }] if items.empty?
    update_view('diagnostics' => items)
  end

  def check_source
    return if @busy
    source = bridge.source.to_s
    update_view('build' => '構文チェック中', 'kind' => '')
    result = JSON.parse(bridge.compile('check').await.to_s)
    return if source != bridge.source.to_s || @busy
    update_view('build' => result['ok'] ? '構文OK' : 'エラー', 'kind' => result['ok'] ? 'ok' : 'error')
    diagnostics(result['ok'] ? @asset_errors : result['diagnostics'], '構文OK')
  end

  def build(run = true)
    return false if @busy
    bridge.cancelCheck
    @busy = true
    update_view('busy' => true, 'build' => 'ビルド中', 'kind' => '', 'preview' => 'ビルド中…')
    begin
      result = JSON.parse(bridge.compile('build').await.to_s)
      unless result['ok']
        update_view('build' => 'エラー', 'kind' => 'error', 'preview' => 'ログを確認してください')
        diagnostics(result['diagnostics'])
        return false
      end
      update_view('build' => 'ビルド成功', 'kind' => 'ok')
      diagnostics(@asset_errors, "game.mrb · #{result['bytes']} B")
      bridge.launch if run
      true
    ensure
      @busy = false
      update_view('busy' => false)
    end
  end

  def restart
    bridge.builtCurrent == true ? bridge.launch : build
  end

  def download
    return if @busy
    return unless bridge.builtCurrent == true || build(false)
    count = bridge.downloadZip.to_i
    diagnostics(@asset_errors, "ZIPを保存 · #{count}ファイル") if count > 0
  end

  def render_assets
    tag(:div, id: 'asset-list', class: 'asset-list', 'aria-live' => 'polite') do
      state[:ui]['assets'].each do |asset|
        name = asset['name']
        tag(:div, class: 'asset-row', key: name) do
          tag(:div, class: 'asset-meta') do
            tag(:span) { name }
            if asset['sourceName'] != name
              tag(:span, class: 'asset-source') { "(#{asset['sourceName']}から変換)" }
            end
          end
          tag(:div, class: 'asset-actions') do
            if asset['editable']
              tag(:button, class: 'asset-edit', type: 'button', 'aria-label' => "#{name}を編集",
                 onclick: -> { bridge.openPixel(name) }) { '編集' }
            end
            tag(:button, class: 'asset-remove', type: 'button', title: "#{name}を削除",
               onclick: -> { bridge.removeAsset(name) }) { '×' }
          end
        end
      end
    end
  end

  def render_diagnostics
    tag(:div, id: 'diagnostics-list', class: 'diagnostics-list', role: 'log', 'aria-live' => 'polite') do
      state[:ui]['diagnostics'].each do |item|
        tag(:div, class: "diagnostic #{item['severity']}") do
          tag(:div, class: 'diagnostic-location') do
            if item['line'].to_i > 0
              line = item['line'].to_i
              column = item['column'].to_i
              tag(:button, type: 'button', onclick: -> { bridge.goToLocation(line, column) }) { "#{line}:#{column}" }
            else
              tag(:span) { 'build' }
            end
          end
          tag(:div) { item['message'] }
        end
      end
    end
  end

  def render
    tag(:div) do
      tag(:header, { "class" => "topbar" }) do
        tag(:div, { "class" => "brand" }) do
          brand('../emulator/')
          tag(:h1) { "Studio" }
        end
        tag(:div, { "class" => "toolbar", "aria-label" => "プロジェクト操作" }) do
          tag(:input, { "id" => "project-name", "class" => "project-name", "value" => "my-game", "aria-label" => "プロジェクト名", oninput: ->(event) { bridge.projectName(event.target[:value].to_s) } })
          tag(:button, { "id" => "new-project", "class" => "button button-quiet", "type" => "button", onclick: -> { bridge.newProject } }) { "新規" }
          tag(:button, { "id" => "import-source", "class" => "button button-quiet", "type" => "button", onclick: -> { bridge.openSource } }) { "開く" }
          state[:ui]['samples'].each do |sample|
            tag(:button, { "class" => "button button-quiet", "type" => "button", onclick: -> { bridge.openSample(sample['id']) } }) { sample['title'] }
          end
          tag(:input, { "id" => "source-file", "type" => "file", "accept" => ".rb,text/plain", "hidden" => true, onchange: -> { bridge.importSource } })
          tag(:button, { "id" => "build-run", disabled: state[:ui]["busy"], "class" => "button button-primary", "type" => "button", onclick: -> { build } }) { "実行" }
          tag(:button, { "id" => "download-zip", disabled: state[:ui]["busy"], "class" => "button button-accent", "type" => "button", onclick: -> { download } }) { "SD用ZIP" }
        end
      end
      tag(:main, { "class" => "studio-shell" }) do
        tag(:section, { "class" => "panel editor-panel", "aria-label" => "Rubyコード" }) do
          tag(:div, { "class" => "panel-heading" }) do
            tag(:div) do
              tag(:h2) { "main.rb" }
            end
            tag(:span, { "id" => "build-status", "class" => "status-chip #{state[:ui]['kind']}" }) { state[:ui]["build"] }
          end
          tag(:div, { "class" => "editor-wrap" }) do
            tag(:div, { "id" => "line-numbers", "class" => "line-numbers", "aria-hidden" => "true" }) { "1" }
            tag(:div, { "class" => "editor-surface" }) do
              tag(:pre, { "id" => "editor-highlight", "class" => "editor-highlight", "aria-hidden" => "true" }) do
                tag(:code, { "id" => "highlighted-code" })
              end
              tag(:textarea, { "id" => "editor", "wrap" => "off", "spellcheck" => "false", "autocapitalize" => "off", "autocomplete" => "off", "autocorrect" => "off", "aria-label" => "main.rbのRubyコード", oninput: -> { bridge.editSource } })
            end
          end
          tag(:div, { "class" => "editor-footer" }) do
            tag(:span, { "id" => "cursor-position" }) { "1:1" }
            tag(:span) { "Ruby" }
          end
          tag(:div, { "class" => "assets-section" }) do
            tag(:div, { "class" => "section-heading" }) do
              tag(:div) do
                tag(:h3) { "素材" }
              end
              tag(:button, { "id" => "open-pixel-editor", "class" => "button button-quiet", "type" => "button", onclick: -> { bridge.openPixel } }) { "ドット絵" }
              tag(:label, { "class" => "button button-quiet upload-label" }) do
                tag(:span) { "素材を追加" }
                tag(:input, { "id" => "asset-files", "class" => "file-input", "type" => "file", "accept" => ".png,.raw,.wav,.pbf", "multiple" => true, "aria-label" => "素材を追加", onchange: -> { bridge.importAssets } })
              end
            end
            tag(:p, { "class" => "hint" }) { "PNG / RAW / WAV / PBF" }
            render_assets
          end
        end
        tag(:section, { "class" => "panel preview-panel", "aria-label" => "エミュレータ" }) do
          tag(:div, { "class" => "panel-heading" }) do
            tag(:div) do
              tag(:h2) { "ゲーム" }
            end
            tag(:button, { "id" => "restart-preview", disabled: state[:ui]["busy"], "class" => "button button-quiet", "type" => "button", onclick: -> { restart } }) { "再起動" }
          end
          tag(:div, { "class" => "preview-frame" }) do
            tag(:iframe, { "id" => "runner", "title" => "PicoRubittoエミュレータ", "scrolling" => "no", "src" => "runner.html?boot=1" })
          end
          tag(:div, { "class" => "preview-footer" }) do
            tag(:span, { "id" => "preview-status" }) { state[:ui]["preview"] }
          end
        end
      end
      tag(:section, { "class" => "diagnostics panel", "aria-label" => "ビルド結果" }) do
        tag(:div, { "class" => "panel-heading diagnostics-heading" }) do
          tag(:div) do
            tag(:h2) { "ログ" }
          end
          tag(:span, { "id" => "save-status", "class" => "muted" }) { state[:ui]["save"] }
        end
        render_diagnostics
      end
      tag(:footer, { "class" => "footer" }) do
        tag(:a, { "href" => "../docs/hardware.md" }) { "実機であそぶ" }
        tag(:a, { "href" => "../emulator/", "target" => "_blank", "rel" => "noreferrer" }) { "ゲーム一覧" }
      end
      component(PixelEditorComponent)
    end
  end
end
