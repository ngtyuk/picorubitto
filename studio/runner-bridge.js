(() => {
  'use strict';
  let pending;
  let running = false;
  const activeUrls = [];
  function send(type, payload = {}) {
    if (parent !== window) parent.postMessage({ type, ...payload }, location.origin);
  }
  function report(error) {
    send('studio-runtime-error', { message: error?.message || String(error) });
  }
  window.RunnerBrowser = {
    ready() { send('studio-ready'); },
    async launch() {
      const payload = pending;
      pending = null;
      if (!payload || running) return;
      running = true;
      window.GAME_ASSET_BASE = '../emulator/assets/';
      window.GAME_ASSET_URLS = Object.create(null);
      window.GAME_ASSETS = [];
      window.GAME_ROOT = 'studio';
      for (const asset of payload.assets || []) {
        const url = URL.createObjectURL(new Blob([asset.bytes], { type: asset.type || 'application/octet-stream' }));
        activeUrls.push(url);
        window.GAME_ASSETS.push(asset.name);
        window.GAME_ASSET_URLS[asset.name] = url;
      }
      const originalError = console.error.bind(console);
      console.error = (...args) => {
        originalError(...args);
        send('studio-runtime-error', { message: args.map(String).join(' ') });
      };
      try {
        await window.GamingWeb.runGame('', payload.mrb);
        send('studio-status', { message: '実行中' });
      } catch (error) { window.GameSystemScreen.error('LOAD'); report(error); }
    }
  };
  window.addEventListener('message', (event) => {
    if (event.source !== parent || event.origin !== location.origin) return;
    const data = event.data || {};
    if (data.type === 'studio-run' && data.mrb && !running) {
      pending = data;
      window.GamingWeb.emit('runner-state', { running: true, title: data.projectName || 'ゲーム', launch: true });
    }
    if (data.type === 'studio-key') window.dispatchEvent(new KeyboardEvent(data.action, { key: data.key, bubbles: true }));
  });
  window.addEventListener('pagehide', () => activeUrls.forEach((url) => URL.revokeObjectURL(url)));
  window.addEventListener('error', (event) => report(event.error || event.message));
  window.addEventListener('unhandledrejection', (event) => report(event.reason));
})();
