require 'funicular'
require 'json'

# Shared Funicular components and browser notifications.
class GamingComponent < Funicular::Component
  def translate_ui(ja, en)
    return ja unless JS.respond_to?(:global)
    page = JS.global[:GamingWeb][:page].to_s
    return ja unless page == 'studio' || page == 'runner'
    JS.global[:StudioI18n][:language].to_s == 'en' ? en : ja
  end
  def listen(name, &block)
    @subscriptions ||= []
    @subscriptions << JS.document.addEventListener(name) do |event|
      block.call(JSON.parse(event[:detail].to_s))
    end
  end

  def component_unmounted
    (@subscriptions || []).each { |id| JS::Object.removeEventListener(id) }
  end

  def component_raised(error)
    puts error.message
    puts error.backtrace
    JS.global[:GamingWeb].reportFailure(error.message)
  end

  def brand(href = 'index.html')
    tag(:a, class: 'brand-link', href: href) do
      tag(:img, class: 'pixel-mark', src: '../web/ruby-mark.svg', alt: '')
      tag(:span) { 'PicoRubitto' }
    end
  end

  def controls
    tag(:section, id: 'controls', class: 'controls', 'aria-label' => translate_ui('ゲーム操作', 'Game controls')) do
      tag(:div, class: 'dpad') do
        [['UP', 'up', '↑'], ['LEFT', 'left', '←'], ['RIGHT', 'right', '→'], ['DOWN', 'down', '↓']].each do |key, css, text|
          tag(:button, 'data-button' => key, class: css) { text }
        end
      end
      tag(:div, class: 'system-buttons') do
        tag(:button, 'data-button' => 'SELECT', class: 'button-select') { 'SEL' }
        tag(:button, 'data-button' => 'START', class: 'button-start') { 'START' }
      end
      tag(:div, class: 'action-buttons') do
        tag(:button, 'data-button' => 'B', class: 'button-b') { 'B' }
        tag(:button, 'data-button' => 'A', class: 'button-a') { 'A' }
      end
      tag(:div, class: 'shoulder-buttons') do
        tag(:button, 'data-button' => 'L', class: 'button-shoulder') { 'L' }
        tag(:button, 'data-button' => 'R', class: 'button-shoulder') { 'R' }
      end
    end
    tag(:label, class: 'audio-dial', for: 'audio-dial') do
      tag(:span) { translate_ui('音量ダイヤル', 'Volume dial') }
      tag(:input, id: 'audio-dial', type: 'range', min: '0', max: '4095', value: '4095')
    end
  end
end
