# Browser Studio: Funicular owns the page, actions, assets and diagnostics.
class StudioComponent < GamingComponent
  def initialize_state
    { ui: { "build" => translate_ui('準備中', 'Preparing'), "preview" => translate_ui('待機中', 'Waiting'), "save" => translate_ui('自動保存', 'Saved automatically'),
            "kind" => "", "busy" => false, "ready" => false, "assets" => [], "diagnostics" => [], "samples" => [] } }
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
      diagnostics(errors, translate_ui('構文OK', 'Syntax OK'))
    end
    listen('studio-check') { check_source }
    listen('studio-runtime-error') do |data|
      update_view('preview' => translate_ui('実行エラー', 'Runtime error'))
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
    update_view('build' => translate_ui('構文チェック中', 'Checking syntax'), 'kind' => '')
    result = JSON.parse(bridge.compile('check').await.to_s)
    return if source != bridge.source.to_s || @busy
    update_view('build' => result['ok'] ? translate_ui('構文OK', 'Syntax OK') : translate_ui('エラー', 'Error'), 'kind' => result['ok'] ? 'ok' : 'error')
    diagnostics(result['ok'] ? @asset_errors : result['diagnostics'], translate_ui('構文OK', 'Syntax OK'))
  end

  def build(run = true)
    return false if @busy
    bridge.cancelCheck
    @busy = true
    update_view('busy' => true, 'build' => translate_ui('ビルド中', 'Building'), 'kind' => '', 'preview' => translate_ui('ビルド中…', 'Building…'))
    begin
      result = JSON.parse(bridge.compile('build').await.to_s)
      unless result['ok']
        update_view('build' => translate_ui('エラー', 'Error'), 'kind' => 'error', 'preview' => translate_ui('ログを確認してください', 'Check the log'))
        diagnostics(result['diagnostics'])
        return false
      end
      update_view('build' => translate_ui('ビルド成功', 'Build succeeded'), 'kind' => 'ok')
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
    diagnostics(@asset_errors, translate_ui("ZIPを保存 · #{count}ファイル", "ZIP saved · #{count} files")) if count > 0
  end

  def render_assets
    tag(:div, id: 'asset-list', class: 'asset-list', 'aria-live' => 'polite') do
      state[:ui]['assets'].each do |asset|
        name = asset['name']
        tag(:div, class: 'asset-row', key: name) do
          tag(:div, class: 'asset-meta') do
            tag(:span) { name }
            if asset['sourceName'] != name
              tag(:span, class: 'asset-source') { translate_ui("(#{asset['sourceName']}から変換)", "(converted from #{asset['sourceName']})") }
            end
          end
          tag(:div, class: 'asset-actions') do
            if asset['editable']
              tag(:button, class: 'asset-edit', type: 'button', 'aria-label' => translate_ui("#{name}を編集", "Edit #{name}"),
                 onclick: -> { bridge.openPixel(name) }) { translate_ui('編集', 'Edit') }
            end
            tag(:button, class: 'asset-remove', type: 'button', title: translate_ui("#{name}を削除", "Remove #{name}"),
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
          brand('https://github.com/ngtyuk/picorubitto')
          tag(:h1) { tag(:a, { "href" => "./" }) { "Studio" } }
          tag(:a, { "href" => "docs.html", "class" => "docs-link", "target" => "_blank", "rel" => "noopener" }) { translate_ui('ドキュメント', 'Docs') }
        end
        tag(:div, { "class" => "toolbar", "aria-label" => translate_ui('プロジェクト操作', 'Project actions') }) do
          tag(:input, { "id" => "project-name", "class" => "project-name", "value" => "my-game", "aria-label" => translate_ui('プロジェクト名', 'Project name'), oninput: ->(event) { bridge.projectName(event.target[:value].to_s) } })
          tag(:button, { "id" => "new-project", "class" => "button button-quiet", "type" => "button", onclick: -> { bridge.newProject } }) { translate_ui('新規', 'New') }
          tag(:button, { "id" => "import-source", "class" => "button button-quiet", "type" => "button", onclick: -> { bridge.openSource } }) { translate_ui('開く', 'Open') }
          tag(:button, { "id" => "open-samples", "class" => "button button-quiet", "type" => "button", onclick: -> { bridge.showSamples } }) { translate_ui('サンプルゲーム', 'Sample games') }
          tag(:input, { "id" => "source-file", "type" => "file", "accept" => ".rb,text/plain", "hidden" => true, onchange: -> { bridge.importSource } })
          tag(:button, { "id" => "download-zip", disabled: state[:ui]["busy"], "class" => "button button-accent", "type" => "button", onclick: -> { download } }) { translate_ui('書き出す', 'Export') }
          tag(:button, { "id" => "change-language", disabled: !state[:ui]['ready'], "class" => "button button-quiet", "type" => "button", "aria-label" => translate_ui('英語に切り替え', 'Switch to Japanese'), onclick: -> { bridge.changeLanguage } }) { translate_ui('English', '日本語') }
        end
      end
      tag(:main, { "class" => "studio-shell" }) do
        tag(:section, { "class" => "panel editor-panel", "aria-label" => translate_ui('Rubyコード', 'Ruby code') }) do
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
              tag(:textarea, { "id" => "editor", "wrap" => "off", "spellcheck" => "false", "autocapitalize" => "off", "autocomplete" => "off", "autocorrect" => "off", "aria-label" => translate_ui('main.rbのRubyコード', 'Ruby code in main.rb'), oninput: -> { bridge.editSource } })
            end
          end
          tag(:div, { "class" => "editor-footer" }) do
            tag(:span, { "id" => "cursor-position" }) { "1:1" }
            tag(:span) { "Ruby" }
          end
          tag(:div, { "class" => "assets-section" }) do
            tag(:div, { "class" => "section-heading" }) do
              tag(:div) do
                tag(:h3) { translate_ui('素材', 'Assets') }
              end
              tag(:button, { "id" => "open-pixel-editor", "class" => "button button-quiet", "type" => "button", onclick: -> { bridge.openPixel } }) { translate_ui('ドット絵', 'Pixel art') }
              tag(:label, { "class" => "button button-quiet upload-label" }) do
                tag(:span) { translate_ui('素材を追加', 'Add assets') }
                tag(:input, { "id" => "asset-files", "class" => "file-input", "type" => "file", "accept" => ".png,.raw,.wav,.pbf", "multiple" => true, "aria-label" => translate_ui('素材を追加', 'Add assets'), onchange: -> { bridge.importAssets } })
              end
            end
            tag(:p, { "class" => "hint" }) { "PNG / RAW / WAV / PBF" }
            render_assets
          end
        end
        tag(:section, { "class" => "panel preview-panel", "aria-label" => translate_ui('エミュレータ', 'Emulator') }) do
          tag(:div, { "class" => "panel-heading" }) do
            tag(:div) do
              tag(:h2) { translate_ui('ゲーム', 'Game') }
            end
            tag(:div, { "class" => "preview-actions" }) do
              tag(:button, { "id" => "build-run", disabled: state[:ui]["busy"], "class" => "button button-primary", "type" => "button", onclick: -> { build } }) { translate_ui('実行', 'Run') }
              tag(:button, { "id" => "restart-preview", disabled: state[:ui]["busy"], "class" => "button button-quiet", "type" => "button", onclick: -> { restart } }) { translate_ui('再起動', 'Restart') }
            end
          end
          tag(:div, { "class" => "preview-frame" }) do
            tag(:iframe, { "id" => "runner", "title" => translate_ui('PicoRubittoエミュレータ', 'PicoRubitto emulator'), "scrolling" => "no", "src" => "runner.html?boot=1&lang=#{translate_ui('ja', 'en')}" })
          end
          tag(:div, { "class" => "preview-footer" }) do
            tag(:span, { "id" => "preview-status" }) { state[:ui]["preview"] }
          end
        end
      end
      tag(:section, { "class" => "diagnostics panel", "aria-label" => translate_ui('ビルド結果', 'Build results') }) do
        tag(:div, { "class" => "panel-heading diagnostics-heading" }) do
          tag(:div) do
            tag(:h2) { translate_ui('ログ', 'Log') }
          end
          tag(:span, { "id" => "save-status", "class" => "muted" }) { state[:ui]["save"] }
        end
        render_diagnostics
      end
      tag(:footer, { "class" => "footer" }) do
        tag(:a, { "href" => "../docs/hardware.md" }) { translate_ui('実機であそぶ', 'Play on hardware') }
        tag(:a, { "href" => "../emulator/", "target" => "_blank", "rel" => "noreferrer" }) { translate_ui('ゲーム一覧', 'Games') }
      end
      tag(:dialog, { "id" => "sample-dialog", "class" => "sample-dialog", "aria-labelledby" => "sample-title" }) do
        tag(:header, { "class" => "panel-heading" }) do
          tag(:h2, { "id" => "sample-title" }) { translate_ui('サンプルゲーム', 'Sample games') }
          tag(:button, { "class" => "button button-quiet", "type" => "button", onclick: -> { bridge.closeSamples } }) { translate_ui('閉じる', 'Close') }
        end
        tag(:div, { "class" => "sample-body" }) do
          tag(:p, { "class" => "hint" }) { translate_ui('選んだゲームのコードと素材を読み込みます。', 'Load the selected game’s code and assets.') }
          if state[:ui]['samples'].empty?
            tag(:p, { "class" => "hint" }) { translate_ui('サンプルゲームはありません。', 'No sample games are available.') }
          else
            state[:ui]['samples'].each do |sample|
              tag(:button, { "class" => "button sample-item", "type" => "button", onclick: -> { bridge.openSample(sample['id']) } }) { sample['title'] }
            end
          end
        end
      end
      component(PixelEditorComponent)
    end
  end
end
