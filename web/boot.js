// Start the Funicular page before browser services or game tasks touch its DOM.
const page = document.body.dataset.page;
const uiText = (ja, en) => window.StudioI18n ? window.StudioI18n.t(ja, en) : ja;
const runtime = new URL('../emulator/picoruby/picoruby.js', import.meta.url);
let module;
let gameActive = false;
window.GamingWeb = {
  page,
  version: null,
  resource(src) {
    const url = new URL(src, document.baseURI);
    if (this.version) url.searchParams.set('v', this.version.build || this.version.revision);
    return url.href;
  },
  emit(name, value) {
    document.dispatchEvent(new CustomEvent(name, { detail: JSON.stringify(value) }));
  },
  reportFailure(message) {
    if (page !== 'studio' && window.GameSystemScreen && document.getElementById('screen')) {
      window.GameSystemScreen.error('SYSTEM');
      console.error(message);
      return;
    }
    const status = document.getElementById('boot-status');
    status.hidden = false;
    status.textContent = uiText(`起動エラー: ${message}`, `Startup error: ${message}`);
    console.error(message);
  },
  async loadScript(src) {
    return new Promise((resolve, reject) => {
      const script = document.createElement('script');
      script.src = this.resource(src);
      if (script.src.includes('/prism-core.')) script.setAttribute('data-manual', '');
      script.onload = resolve;
      script.onerror = () => reject(new Error(uiText(`${src}を読み込めません`, `Could not load ${src}`)));
      document.body.appendChild(script);
    });
  },
  async runGame(source, mrb = null) {
    gameActive = true;
    await this.loadScript(new URL('../emulator/emulator.js', import.meta.url).href);
    await window.GameEmulator.ready;
    if (window.GameSystemScreen && window.GameSystemScreen.hasError()) return;
    window.GameEmulator.clear(0);
    const response = await fetch(new URL('../emulator/game_adapter.rb', import.meta.url), { cache: 'no-store' });
    if (!response.ok) throw new Error(uiText('ゲームアダプタを読み込めません', 'Could not load the game adapter'));
    const code = await response.text();
    if (module.ccall('picorb_create_task', 'number', ['string'], [code + '\n' + (source || '')]) !== 0) {
      throw new Error(uiText('ゲームを読み込めません', 'Could not load the game'));
    }
    if (mrb) {
      const bytes = new Uint8Array(mrb);
      const pointer = module._malloc(bytes.length);
      if (!pointer) throw new Error(uiText('ゲーム用メモリを確認してください', 'Not enough memory for the game'));
      try {
        module.HEAPU8.set(bytes, pointer);
        const result = module.ccall('picorb_create_task_from_mrb', 'number', ['number', 'number'], [pointer, bytes.length]);
        if (result !== 0) throw new Error(uiText('game.mrbを確認してください', 'Check game.mrb'));
      } finally { module._free(pointer); }
    }
  },
  ready() {
    document.getElementById('boot-status').hidden = true;
  }
};

try {
  const manifest = await fetch(new URL('../emulator/picoruby/version.json', import.meta.url), { cache: 'no-store' });
  if (!manifest.ok) throw new Error(uiText('workflow.rb studio でWASMを生成してください', 'Run workflow.rb studio to build WebAssembly'));
  window.GamingWeb.version = await manifest.json();
  const pageScripts = page === 'studio'
    ? ['vendor/prism/prism-core.min.js', 'vendor/prism/prism-clike.min.js', 'vendor/prism/prism-ruby.min.js',
       'pixel-art-model.js', 'pixel-editor.js', 'studio-bridge.js']
    : page === 'runner' ? ['../emulator/system-screen.js', 'runner-bridge.js'] : ['system-screen.js', 'browser-bridge.js'];
  for (const script of pageScripts) await window.GamingWeb.loadScript(script);
  const scripts = Array.from(document.querySelectorAll('script[type="text/ruby"]'));
  const [{ default: createModule }, sources] = await Promise.all([
    import(window.GamingWeb.resource(runtime.href)),
    Promise.all(scripts.map(async (script) => {
      const response = await fetch(script.src, { cache: 'no-store' });
      if (!response.ok) throw new Error(uiText(`${script.src}を読み込めません`, `Could not load ${script.src}`));
      return response.text();
    }))
  ]);
  module = await createModule({
    locateFile(name) { return window.GamingWeb.resource(new URL(name, runtime).href); }
  });
  window.picorubyModule = module;
  if (module.ccall('picorb_init', 'number', [], []) !== 0) throw new Error(uiText('PicoRubyを初期化できません', 'Could not initialize PicoRuby'));
  module.ccall('picorb_create_task', 'number', ['string'], [sources.join('\n')]);
  let lastTick = performance.now();
  function step() {
    const now = performance.now();
    let ticks = 0;
    while (now - lastTick >= 4 && ticks++ < 10) { module._mrb_tick_wasm(); lastTick += 4; }
    if (now - lastTick >= 4) lastTick = now;
    // UI-only pages yield quickly; game pages keep the emulator's 16ms slice.
    const end = performance.now() + (gameActive ? 16 : 0.5);
    do {
      const status = module._mrb_run_step_status();
      if (status === 2 && gameActive && window.GameSystemScreen) {
        if (window.GameEmulator) window.GameEmulator.audioStop();
        window.GameSystemScreen.error('GAME');
        gameActive = false;
      }
      if (status < 0) {
        window.GamingWeb.reportFailure(uiText('PicoRubyのタスクを実行できません', 'Could not run the PicoRuby task'));
        return;
      }
      if (status === 0) break;
    } while (performance.now() < end);
    requestAnimationFrame(step);
  }
  requestAnimationFrame(step);
} catch (error) { window.GamingWeb.reportFailure(error.message); }
