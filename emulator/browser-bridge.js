// Browser I/O for the Funicular emulator page. Selection and asset policy live in Ruby.
(() => {
  'use strict';
  const root = new URL('../local-games/', document.baseURI);
  const sources = new Map();
  async function entries(url) {
    const response = await fetch(url, { cache: 'no-store' });
    if (!response.ok) return [];
    const parsed = new DOMParser().parseFromString(await response.text(), 'text/html');
    return [...parsed.querySelectorAll('a[href]')].flatMap((link) => {
      const href = link.getAttribute('href');
      if (!href || /^[/.#]/.test(href)) return [];
      const directory = href.endsWith('/');
      let name;
      try { name = decodeURIComponent(directory ? href.slice(0, -1) : href); } catch { return []; }
      return /^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(name) && !name.includes('..') ? [{ name, directory }] : [];
    });
  }
  window.EmulatorBrowser = {
    requested() { return new URLSearchParams(location.search).get('game'); },
    selectGame(name) {
      const query = new URLSearchParams(location.search);
      query.set('game', name);
      location.search = query.toString();
    },
    async discover() {
      const directories = (await entries(root)).filter((item) => item.directory);
      const games = await Promise.all(directories.map(async ({ name }) => {
        const base = new URL(`${name}/`, root);
        if (!(await entries(base)).some((item) => !item.directory && item.name === 'main.rb')) return null;
        const assets = (await entries(new URL('assets/', base)))
          .filter((item) => !item.directory && /\.(raw|wav|pbf)$/i.test(item.name)).map((item) => item.name).sort();
        return { name, assets };
      }));
      return JSON.stringify(games.filter(Boolean).sort((a, b) => a.name.localeCompare(b.name)));
    },
    async readSource(name) {
      const response = await fetch(new URL(`${name}/main.rb`, root), { cache: 'no-store' });
      if (!response.ok) throw new Error(`ゲーム本体を確認してください: ${name}`);
      const source = await response.text();
      sources.set(name, source);
      return source;
    },
    async start(name, assetName, assets) {
      window.GAME_ROOT = new URL(`${name}/`, root).href;
      window.GAME_ASSET_BASE = new URL(`${assetName}/assets/`, root).href;
      window.GAME_ASSETS = JSON.parse(assets);
      document.title = `PicoRuby Gaming - ${name}`;
      await window.GamingWeb.runGame(sources.get(name));
    },
    async example(page) {
      const configurations = {
        '2d': ['../examples/games/2d/main.rb', '../examples/games/2d/assets/', ['document-kun.raw', 'beep.wav']],
        '3d': ['../examples/games/3d/main.rb', '', []],
        contract: ['../tests/portable/game_api_contract.rb', '../tests/portable/assets/', ['texture.raw', 'font.pbf']]
      };
      const [source, assetBase, assets] = configurations[page];
      window.GAME_ASSET_BASE = assetBase;
      window.GAME_ASSETS = assets;
      const response = await fetch(source, { cache: 'no-store' });
      if (!response.ok) throw new Error(`${source}を確認してください`);
      await window.GamingWeb.runGame(await response.text());
    }
  };
})();
