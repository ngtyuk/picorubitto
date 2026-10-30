# Preview chrome shares the same Funicular controls as the standalone emulator.
class RunnerComponent < GamingComponent
  def initialize_state
    { running: false, title: 'ゲーム', error: '' }
  end

  def component_mounted
    listen('runner-state') do |data|
      patch(running: data['running'], title: data['title'], error: data['error'] || '')
      JS.global[:RunnerBrowser].launch if data['launch']
    end
    JS.global[:GamingWeb].ready
    JS.global[:RunnerBrowser].ready
  end

  def render
    tag(:main, class: 'shell') do
      tag(:h1, id: 'game-title') { state[:title] }
      tag(:p, id: 'game-description', class: 'description') { state[:error] }
      tag(:section, id: 'waiting', class: 'waiting', style: state[:running] ? 'display:none' : '') { '実行でスタート' }
      tag(:div, style: state[:running] ? '' : 'display:none') do
        tag(:section, id: 'machine', class: 'machine', 'aria-label' => 'ゲーム画面') do
          tag(:canvas, id: 'screen', width: 320, height: 240)
          tag(:div, class: 'status-row') do
            tag(:span, id: 'runtime-status') { '読み込み中…' }
            tag(:span, id: 'fps-status') { '-- fps' }
            tag(:span, id: 'audio-status') { '音声: 停止' }
          end
        end
        controls
      end
      tag(:p, class: 'preview-note') { '矢印 移動 · Z A · X B · Q/E L/R · P/O START/SELECT' }
    end
  end
end
