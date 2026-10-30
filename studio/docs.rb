# Documentation chrome is rendered by Funicular; the Markdown bridge fills the article.
class DocsComponent < GamingComponent
  def component_mounted
    JS.global[:GamingWeb].ready
    JS.global[:DocsBrowser].load
  end

  def render
    tag(:div) do
      tag(:header, class: 'docs-header') do
        tag(:div, class: 'docs-brand') do
          brand('https://github.com/ngtyuk/picorubitto')
          tag(:a, class: 'studio-link', href: './') { 'Studio' }
          tag(:span, class: 'current-page') { translate_ui('ドキュメント', 'Docs') }
        end
        tag(:button, class: 'language-button', type: 'button', onclick: -> { JS.global[:DocsBrowser].changeLanguage }) { translate_ui('English', '日本語') }
      end
      tag(:main, class: 'docs-layout') do
        tag(:nav, class: 'docs-nav', 'aria-label' => translate_ui('APIリファレンスの目次', 'API reference contents')) do
          tag(:h2) { translate_ui('目次', 'Contents') }
          tag(:div, id: 'docs-contents')
        end
        tag(:article, class: 'docs-article') do
          tag(:p, class: 'language-note', lang: 'en') { 'The API reference is currently available in Japanese.' } if translate_ui('ja', 'en') == 'en'
          tag(:div, id: 'docs-status', role: 'status') { translate_ui('読み込み中…', 'Loading…') }
          tag(:div, id: 'docs-content', lang: 'ja')
        end
      end
    end
  end
end
