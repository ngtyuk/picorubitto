class EmulatorComponent < GamingComponent
  def initialize_state
    { games: [], selected: '', title: 'あそぶ', message: '読み込み中…' }
  end

  def browser
    JS.global[:EmulatorBrowser]
  end

  def source_asset_names(source)
    names = []
    offset = 0
    while offset < source.length
      quote = source[offset]
      unless quote == "'" || quote == '"'
        offset += 1
        next
      end
      ending = source.index(quote, offset + 1)
      break unless ending
      name = source[offset + 1, ending - offset - 1]
      if !name.include?('#{') && %w[.raw .wav .pbf].any? { |suffix| name.downcase.end_with?(suffix) }
        names << name unless names.include?(name)
      end
      offset = ending + 1
    end
    names
  end

  def component_mounted
    JS.global[:GamingWeb].ready
    page = JS.global[:GamingWeb][:page].to_s
    if page == 'local'
      games = JSON.parse(browser.discover.await.to_s)
      if games.empty?
        patch(message: 'local-games/にmain.rbを置いてください')
        return
      end
      requested = browser.requested.to_s
      game = games.find { |item| item['name'] == requested } || games[0]
      patch(games: games, selected: game['name'], title: game['name'], message: '起動中…')
      source = browser.readSource(game['name']).await.to_s
      required = source_asset_names(source)
      asset_game = game
      unless required.all? { |name| game['assets'].include?(name) }
        asset_game = games.find { |item| required.all? { |name| item['assets'].include?(name) } } || game
      end
      browser.start(game['name'], asset_game['name'], JSON.generate(asset_game['assets'])).await
    else
      browser.example(page).await
    end
  rescue => error
    JS.global[:console].error(error.message)
    JS.global[:GameSystemScreen].error('LOAD')
  end

  def picker
    tag(:section, class: 'game-picker', 'aria-label' => 'ゲーム選択') do
      tag(:label, for: 'game-select') { 'ゲーム' }
      tag(:select, id: 'game-select', disabled: state[:games].empty?,
         onchange: ->(event) { browser.selectGame(event.target[:value].to_s) }) do
        if state[:games].empty?
          tag(:option) { '読み込み中…' }
        else
          state[:games].each do |game|
            tag(:option, value: game['name'], selected: game['name'] == state[:selected]) { game['name'] }
          end
        end
      end
      tag(:span, id: 'game-count', class: 'game-count') { state[:games].empty? ? '' : "#{state[:games].length}本" }
    end
  end

  def render
    page = JS.global[:GamingWeb][:page].to_s
    title = { '2d' => '2Dサンプル', '3d' => '3Dサンプル', 'contract' => 'APIテスト' }[page] || state[:title]
    description = { '2d' => '図形・画像・音声', '3d' => 'ワイヤーフレーム飛行', 'contract' => '実機とブラウザの共通API' }[page]
    help = { '2d' => '矢印 移動 · Z 音 · X 停止 · Q/E L/R · P/O START/SELECT',
             '3d' => '矢印 移動 · Z 射撃 · X ブースト · P リセット · O HUD',
             'contract' => '緑の画面で合格' }[page] || '矢印 移動 · Z A · X B · Q/E L/R · P/O START/SELECT'
    tag(:main, class: 'shell') do
      tag(:header, class: 'site-header') do
        brand
        tag(:a, href: '../studio/') { 'つくる' }
      end
      tag(:h1, id: 'game-title') { title }
      tag(:p, class: 'description') { description } if description
      picker if page == 'local'
      tag(:section, class: 'machine', 'aria-label' => 'ゲーム画面') do
        tag(:canvas, id: 'screen', width: 320, height: 240)
        tag(:div, class: 'status-row') do
          tag(:span, id: 'runtime-status') { state[:message] }
          tag(:span, id: 'fps-status') { '-- fps' }
          tag(:span, id: 'audio-status') { '音声: 停止' } if page == '2d'
        end
      end
      controls if ['local', '2d'].include?(page)
      tag(:p, class: 'key-help') { help }
      tag(:nav, class: 'page-nav', 'aria-label' => 'ページ') do
        if page == 'local'
          tag(:a, href: '2d-example.html') { '2Dサンプル' }
          tag(:a, href: '3d-example.html') { '3Dサンプル' }
          tag(:a, href: 'contract.html') { 'APIテスト' }
        else
          tag(:a, href: 'index.html') { 'ゲーム一覧' }
        end
      end
    end
  end
end
