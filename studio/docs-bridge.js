(() => {
  'use strict';
  const t = (ja, en) => window.StudioI18n.t(ja, en);
  window.DocsBrowser = {
    changeLanguage() {
      window.StudioI18n.setLanguage(window.StudioI18n.language === 'ja' ? 'en' : 'ja');
    },
    async load() {
      const status = document.getElementById('docs-status');
      const article = document.getElementById('docs-content');
      const contents = document.getElementById('docs-contents');
      try {
        const response = await fetch(window.GamingWeb.resource('../docs/api-reference.md'));
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        // This file is part of the trusted, versioned site package, not user input.
        article.innerHTML = marked.parse(await response.text());
        const used = new Map();
        article.querySelectorAll('h2, h3').forEach((heading) => {
          const base = heading.textContent.trim().toLowerCase().replace(/[^\p{L}\p{N}_-]+/gu, '-').replace(/^-|-$/g, '') || 'section';
          const count = used.get(base) || 0;
          used.set(base, count + 1);
          heading.id = count ? `${base}-${count}` : base;
          if (heading.tagName !== 'H2') return;
          const link = document.createElement('a');
          link.href = `#${heading.id}`;
          link.textContent = heading.textContent;
          contents.appendChild(link);
        });
        article.querySelectorAll('a[href]').forEach((link) => {
          const href = link.getAttribute('href');
          if (!href.startsWith('#') && !/^[a-z][a-z0-9+.-]*:/i.test(href)) {
            link.href = new URL(href, 'https://github.com/ngtyuk/picorubitto/blob/main/docs/').href;
          }
        });
        status.hidden = true;
        if (location.hash) document.getElementById(decodeURIComponent(location.hash.slice(1)))?.scrollIntoView();
      } catch (error) {
        status.textContent = t('APIリファレンスを読み込めませんでした。', 'Could not load the API reference.');
        console.error(error);
      }
    }
  };
})();
