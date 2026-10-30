(() => {
  'use strict';
  let language;
  try { language = localStorage.getItem('picorubitto-studio-language'); } catch (_) { /* storage can be unavailable */ }
  if (language !== 'ja' && language !== 'en') language = navigator.language.toLowerCase().startsWith('ja') ? 'ja' : 'en';
  document.documentElement.lang = language;
  const bootStatus = document.getElementById('boot-status');
  window.StudioI18n = {
    language,
    t(ja, en) { return this.language === 'en' ? en : ja; },
    setLanguage(value) {
      if (value !== 'ja' && value !== 'en') return;
      try { localStorage.setItem('picorubitto-studio-language', value); } catch (_) { /* use URL for this visit */ }
      const url = new URL(location.href);
      url.searchParams.set('lang', value);
      location.assign(url.href);
    }
  };
  const requested = new URL(location.href).searchParams.get('lang');
  if (requested === 'ja' || requested === 'en') {
    window.StudioI18n.language = requested;
    document.documentElement.lang = requested;
    try { localStorage.setItem('picorubitto-studio-language', requested); } catch (_) { /* storage can be unavailable */ }
  }
  if (bootStatus) bootStatus.textContent = window.StudioI18n.t('読み込み中…', 'Loading…');
})();
