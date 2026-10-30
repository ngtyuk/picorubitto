'use strict';
window.StudioBridge = { start: createStudioBridge };

function createStudioBridge() {
  const t = (ja, en) => window.StudioI18n.t(ja, en);

  const DEFAULT_SOURCE = [
    "require 'game'", '', 'Game.init', '', 'loop do',
    '  Game::Display.clear(0x0000)',
    "  Game::Display.text(88, 110, 'Hello, world!', 0xffff)",
    '  Game::Display.present', '  Game.tick(30)', 'end', ''
  ].join('\n');

  const editor = document.getElementById('editor');
  const lineNumbers = document.getElementById('line-numbers');
  const editorHighlight = document.getElementById('editor-highlight');
  const highlightedCode = document.getElementById('highlighted-code');
  let highlightedSource = null;
  const projectNameInput = document.getElementById('project-name');
  const sourceFile = document.getElementById('source-file');
  const assetFiles = document.getElementById('asset-files');
  const cursorPosition = document.getElementById('cursor-position');
  const runner = document.getElementById('runner');
  const pixelDialog = document.getElementById('pixel-dialog');
  const sampleDialog = document.getElementById('sample-dialog');

  const state = {
    projectName: 'my-game', assets: new Map(), lastBuild: null, pendingRun: null,
    runNumber: 0, requestNumber: 0, pendingRequests: new Map(), checkTimer: null,
    saveTimer: null, db: null, assetErrors: [], samples: []
  };

  const pixelEditor = new PixelArtEditor(pixelDialog, {
    onChange: scheduleSave,
    onApply({ name, bytes, pixelArt, sourceAssetName }) {
      if (!safeAssetName(name)) throw new Error(t('素材の名前を確認してください', 'Check the asset name'));
      if (state.assets.has(name) && (name !== sourceAssetName || !state.assets.get(name).pixelArt)) {
        throw new Error(t('名前を変えて追加してください', 'Choose a different name'));
      }
      state.assets.set(name, { name, bytes, sourceName: name,
        type: 'application/octet-stream', pixelArt: { ...pixelArt, sourceAssetName: name } });
      renderAssets();
      scheduleSave();
    }
  });

  const compiler = new Worker(window.GamingWeb.resource('compiler-worker.js'), { type: 'module' });
  compiler.addEventListener('message', (event) => {
    const request = state.pendingRequests.get(event.data.id);
    if (!request) return;
    state.pendingRequests.delete(event.data.id);
    request.resolve(event.data);
  });
  compiler.addEventListener('error', (event) => {
    const message = event.message || t('コンパイラ起動エラー', 'Could not start the compiler');
    state.pendingRequests.forEach((request) => request.reject(new Error(message)));
    state.pendingRequests.clear();
  });

  let publishTimer;
  let updates = {};
  function publish(values) {
    Object.assign(updates, values);
    clearTimeout(publishTimer);
    publishTimer = setTimeout(() => {
      const pending = updates;
      updates = {};
      window.GamingWeb.emit('studio-state', pending);
    }, 0);
  }
  function setPreviewStatus(message) { publish({ preview: message }); }
  function reportAssets() { window.GamingWeb.emit('studio-assets', state.assetErrors); }

  function goToLocation(line, column = 1) {
    const lines = editor.value.split('\n');
    let offset = 0;
    for (let index = 0; index < Math.max(0, line - 1); index += 1) offset += lines[index].length + 1;
    const position = Math.min(offset + Math.max(0, column - 1), editor.value.length);
    editor.focus();
    editor.setSelectionRange(position, position);
    editor.scrollTop = Math.max(0, (line - 4) * parseFloat(getComputedStyle(editor).lineHeight));
    updateEditorDecorations();
  }

  function syncEditorScroll() {
    // Match the text viewport, including the space taken by native scrollbars.
    editorHighlight.style.width = `${editor.clientWidth}px`;
    editorHighlight.style.height = `${editor.clientHeight}px`;
    lineNumbers.style.height = `${editor.clientHeight}px`;
    editorHighlight.scrollTop = editor.scrollTop;
    editorHighlight.scrollLeft = editor.scrollLeft;
    lineNumbers.scrollTop = editor.scrollTop;
  }

  function updateEditorDecorations() {
    if (window.Prism?.languages.ruby && highlightedSource !== editor.value) {
      // Prism escapes source text; the extra newline preserves the last empty line.
      highlightedCode.innerHTML = Prism.highlight(`${editor.value}\n`, Prism.languages.ruby, 'ruby');
      highlightedSource = editor.value;
      editor.parentElement.classList.add('highlight-ready');
    }
    const count = Math.max(1, editor.value.split('\n').length);
    lineNumbers.textContent = Array.from({ length: count }, (_, index) => String(index + 1)).join('\n');
    const line = editor.value.slice(0, editor.selectionStart).split('\n').length;
    const lastBreak = editor.value.lastIndexOf('\n', editor.selectionStart - 1);
    cursorPosition.textContent = `${line}:${editor.selectionStart - lastBreak}`;
    syncEditorScroll();
  }

  function invalidateBuild() {
    state.lastBuild = null;
    updateEditorDecorations();
    scheduleSyntaxCheck();
    scheduleSave();
  }

  function scheduleSyntaxCheck() {
    clearTimeout(state.checkTimer);
    state.checkTimer = setTimeout(() => window.GamingWeb.emit('studio-check', {}), 650);
  }

  function compileSource(mode, source) {
    return new Promise((resolve, reject) => {
      const id = ++state.requestNumber;
      state.pendingRequests.set(id, { resolve, reject });
      compiler.postMessage({ id, mode, source, language: window.StudioI18n.language });
    });
  }

  function slugify(value) {
    const slug = value.trim().replace(/[^A-Za-z0-9._-]+/g, '-').replace(/^-+|-+$/g, '');
    return slug || 'my-game';
  }

  function safeAssetName(name) {
    return /^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(name) && !name.includes('..');
  }

  async function convertPng(file) {
    const image = await createImageBitmap(file);
    const canvas = document.createElement('canvas');
    canvas.width = image.width;
    canvas.height = image.height;
    const context = canvas.getContext('2d', { willReadFrequently: true });
    context.imageSmoothingEnabled = false;
    context.drawImage(image, 0, 0);
    const pixels = context.getImageData(0, 0, image.width, image.height).data;
    const bytes = new Uint8Array(image.width * image.height * 2);
    for (let index = 0, offset = 0; index < pixels.length; index += 4, offset += 2) {
      let color = 0xf81f;
      if (pixels[index + 3] >= 128) {
        const red = Math.floor((pixels[index] * 31 + 127) / 255);
        const green = Math.floor((pixels[index + 1] * 63 + 127) / 255);
        const blue = Math.floor((pixels[index + 2] * 31 + 127) / 255);
        color = (red << 11) | (green << 5) | blue;
      }
      bytes[offset] = color >> 8;
      bytes[offset + 1] = color & 0xff;
    }
    if (typeof image.close === 'function') image.close();
    return { bytes, name: file.name.replace(/\.png$/i, '.raw'), sourceName: file.name };
  }

  async function addAsset(file) {
    const lowerName = file.name.toLowerCase();
    if (/\.(ttf|otf)$/i.test(lowerName)) throw new Error(t(`${file.name}: フォントはPBFで追加してください`, `${file.name}: Use a PBF font`));
    if (!/\.(png|raw|wav|pbf)$/i.test(lowerName)) throw new Error(t(`${file.name}: PNG / RAW / WAV / PBFを選んでください`, `${file.name}: Choose PNG, RAW, WAV, or PBF`));
    const asset = /\.png$/i.test(lowerName)
      ? await convertPng(file)
      : { bytes: new Uint8Array(await file.arrayBuffer()), name: file.name, sourceName: file.name };
    if (!safeAssetName(asset.name)) throw new Error(t(`${file.name}: 名前は英数字・ハイフン・アンダースコア・ドットで指定してください`, `${file.name}: Use letters, numbers, hyphens, underscores, or dots`));
    if (state.assets.has(asset.name)) throw new Error(t(`${asset.name}: 同名アセットがあります`, `${asset.name}: An asset with this name already exists`));
    state.assets.set(asset.name, {
      ...asset,
      type: lowerName.endsWith('.png') ? 'application/octet-stream' : (file.type || 'application/octet-stream')
    });
    renderAssets();
    scheduleSave();
  }

  function renderAssets() {
    const assets = [...state.assets.values()].sort((a, b) => a.name.localeCompare(b.name)).map((asset) => ({
      name: asset.name, sourceName: asset.sourceName, editable: !!asset.pixelArt
    }));
    window.GamingWeb.emit('studio-asset-list', assets);
  }

  function launchPreview(bytes) {
    const payload = {
      type: 'studio-run', projectName: slugify(state.projectName), mrb: bytes.slice().buffer,
      assets: [...state.assets.values()].map((asset) => ({ name: asset.name, type: asset.type, bytes: asset.bytes.slice().buffer }))
    };
    state.pendingRun = payload;
    setPreviewStatus(t('起動中…', 'Starting…'));
    runner.src = `runner.html?run=${++state.runNumber}&lang=${window.StudioI18n.language}`;
  }

  function stopPreview() {
    state.pendingRun = null;
    runner.src = `runner.html?stopped=${Date.now()}&lang=${window.StudioI18n.language}`;
    setPreviewStatus(t('停止中', 'Stopped'));
  }

  function crc32(bytes) {
    let crc = 0xffffffff;
    for (const byte of bytes) {
      crc ^= byte;
      for (let bit = 0; bit < 8; bit += 1) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
  }

  function zipHeader(nameBytes, crc, size, central, offset = 0) {
    const header = new Uint8Array(central ? 46 + nameBytes.length : 30 + nameBytes.length);
    const view = new DataView(header.buffer);
    view.setUint32(0, central ? 0x02014b50 : 0x04034b50, true);
    if (central) {
      view.setUint16(4, 20, true); view.setUint16(6, 20, true); view.setUint16(28, nameBytes.length, true);
      view.setUint32(16, crc, true); view.setUint32(20, size, true); view.setUint32(24, size, true); view.setUint32(42, offset, true);
      header.set(nameBytes, 46);
    } else {
      view.setUint16(4, 20, true); view.setUint32(14, crc, true); view.setUint32(18, size, true); view.setUint32(22, size, true);
      view.setUint16(26, nameBytes.length, true); header.set(nameBytes, 30);
    }
    return header;
  }

  function makeZip(entries) {
    const encoder = new TextEncoder();
    const localParts = [], centralParts = [];
    let offset = 0;
    entries.forEach((entry) => {
      const bytes = entry.bytes instanceof Uint8Array ? entry.bytes : new Uint8Array(entry.bytes);
      const nameBytes = encoder.encode(entry.name);
      const crc = crc32(bytes);
      const local = zipHeader(nameBytes, crc, bytes.length, false);
      localParts.push(local, bytes);
      centralParts.push(zipHeader(nameBytes, crc, bytes.length, true, offset));
      offset += local.length + bytes.length;
    });
    const centralSize = centralParts.reduce((total, part) => total + part.length, 0);
    const end = new Uint8Array(22);
    const endView = new DataView(end.buffer);
    endView.setUint32(0, 0x06054b50, true); endView.setUint16(8, entries.length, true); endView.setUint16(10, entries.length, true);
    endView.setUint32(12, centralSize, true); endView.setUint32(16, offset, true);
    const all = [...localParts, ...centralParts, end];
    const output = new Uint8Array(all.reduce((total, part) => total + part.length, 0));
    let cursor = 0;
    all.forEach((part) => { output.set(part, cursor); cursor += part.length; });
    return output;
  }

  function downloadZip() {
    if (!state.lastBuild) return 0;
    const entries = [{ name: 'game.mrb', bytes: state.lastBuild.bytes }];
    [...state.assets.values()].sort((a, b) => a.name.localeCompare(b.name)).forEach((asset) => entries.push({ name: `assets/${asset.name}`, bytes: asset.bytes }));
    const blob = new Blob([makeZip(entries)], { type: 'application/zip' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url; link.download = `${slugify(state.projectName)}-sd-card.zip`; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    return entries.length;
  }

  function openDatabase() {
    if (!('indexedDB' in window)) return Promise.resolve(null);
    return new Promise((resolve) => {
      const request = indexedDB.open('pico-ruby-gaming-studio', 1);
      request.onupgradeneeded = () => request.result.createObjectStore('projects', { keyPath: 'id' });
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => resolve(null);
    });
  }

  function scheduleSave() {
    clearTimeout(state.saveTimer);
    state.saveTimer = setTimeout(saveProject, 250);
  }

  function saveProject() {
    if (!state.db) { publish({ save: t('保存エラー', 'Save failed') }); return Promise.resolve(false); }
    const project = {
      id: 'current', projectName: state.projectName, source: editor.value,
      pixelArt: pixelEditor.serialize(),
      assets: [...state.assets.values()].map((asset) => ({ name: asset.name, sourceName: asset.sourceName,
        type: asset.type, bytes: asset.bytes.slice().buffer, pixelArt: asset.pixelArt }))
    };
    const transaction = state.db.transaction('projects', 'readwrite');
    transaction.objectStore('projects').put(project);
    return new Promise((resolve) => {
      transaction.oncomplete = () => { publish({ save: t('自動保存', 'Saved automatically') }); resolve(true); };
      transaction.onerror = transaction.onabort = () => { publish({ save: t('保存エラー', 'Save failed') }); resolve(false); };
    });
  }

  function loadProject(db) {
    if (!db) return Promise.resolve(null);
    return new Promise((resolve) => {
      const request = db.transaction('projects', 'readonly').objectStore('projects').get('current');
      request.onsuccess = () => resolve(request.result || null);
      request.onerror = () => resolve(null);
    });
  }

  function applyProject(project) {
    state.projectName = project.projectName || 'my-game';
    projectNameInput.value = state.projectName;
    editor.value = project.source || DEFAULT_SOURCE;
    state.assets.clear();
    state.assetErrors = [];
    (project.assets || []).forEach((asset) => {
      state.assets.set(asset.name, { name: asset.name, sourceName: asset.sourceName || asset.name,
        type: asset.type || 'application/octet-stream', bytes: new Uint8Array(asset.bytes), pixelArt: asset.pixelArt });
    });
    pixelEditor.load(project.pixelArt);
    renderAssets(); reportAssets(); updateEditorDecorations(); scheduleSyntaxCheck();
  }

  function isTypingTarget(target) {
    return target && typeof target.matches === 'function' && target.matches('textarea, input, select');
  }

  editor.addEventListener('scroll', syncEditorScroll);
  new ResizeObserver(syncEditorScroll).observe(editor);
  editor.addEventListener('click', updateEditorDecorations);
  editor.addEventListener('keyup', updateEditorDecorations);
  Object.assign(window.StudioBridge, {
    pixel: pixelEditor,
    editSource: invalidateBuild,
    projectName(value) { state.projectName = value; scheduleSave(); },
    cancelCheck() { clearTimeout(state.checkTimer); },
    source() { return editor.value; },
    builtCurrent() { return !!state.lastBuild && state.lastBuild.source === editor.value; },
    async compile(mode) {
      const source = editor.value;
      try {
        const result = await compileSource(mode, source);
        if (mode === 'build') state.lastBuild = result.ok ? { source, bytes: new Uint8Array(result.mrb).slice() } : null;
        return JSON.stringify({ ok: result.ok, diagnostics: result.diagnostics || [], bytes: result.mrb?.byteLength || 0 });
      } catch (error) {
        if (mode === 'build') state.lastBuild = null;
        return JSON.stringify({ ok: false, diagnostics: [{ severity: 'error', line: 0, message: error.message }] });
      }
    },
    launch() { if (state.lastBuild) launchPreview(state.lastBuild.bytes); },
    downloadZip,
    goToLocation,
    openSource() { sourceFile.click(); },
    async changeLanguage() {
      clearTimeout(state.saveTimer);
      if (!await saveProject()) {
        window.alert(t('プロジェクトを保存できないため、言語を切り替えられません。', 'Could not save the project, so the language was not changed.'));
        return;
      }
      window.StudioI18n.setLanguage(window.StudioI18n.language === 'ja' ? 'en' : 'ja');
    },
    showSamples() { if (!sampleDialog.open) sampleDialog.showModal(); },
    closeSamples() { sampleDialog.close(); },
    async openSample(id) {
      const sample = state.samples.find((item) => item.id === id);
      if (!sample) return;
      try {
        const base = `samples/${sample.id}/`;
        const get = async (path) => {
          const response = await fetch(window.GamingWeb.resource(base + path));
          if (!response.ok) throw new Error(t(`${path}を読み込めません`, `Could not load ${path}`));
          return response;
        };
        const [sourceResponse, ...assetResponses] = await Promise.all([
          get('main.rb'), ...sample.assets.map((name) => get(`assets/${name}`))
        ]);
        const source = await sourceResponse.text();
        const assets = await Promise.all(assetResponses.map(async (response, index) => ({
          name: sample.assets[index], bytes: await response.arrayBuffer(), type: 'application/octet-stream'
        })));
        if (!window.confirm(t(`現在のコードと素材を${sample.title}で置き換えますか？`, `Replace the current code and assets with ${sample.title}?`))) return;
        sampleDialog.close();
        applyProject({ projectName: sample.id, source, assets });
        state.lastBuild = null; stopPreview(); scheduleSave();
      } catch (error) {
        state.assetErrors = [{ severity: 'error', line: 0, column: 0, message: error.message }];
        reportAssets();
      }
    },
    openPixel(name) {
      const asset = state.assets.get(name);
      pixelEditor.open(asset?.pixelArt ? { ...asset.pixelArt, sourceAssetName: name } : null);
    },
    removeAsset(name) { state.assets.delete(name); renderAssets(); scheduleSave(); },
    async importSource() {
      const file = sourceFile.files?.[0];
      if (!file) return;
      editor.value = await file.text();
      state.projectName = file.name.replace(/\.rb$/i, '') || 'my-game';
      projectNameInput.value = state.projectName;
      sourceFile.value = '';
      invalidateBuild();
    },
    async importAssets() {
      const errors = [];
      state.assetErrors = [];
      for (const file of assetFiles.files || []) {
        try { await addAsset(file); } catch (error) { errors.push(error.message); }
      }
      assetFiles.value = '';
      state.assetErrors = errors.map((message) => ({ severity: 'error', line: 0, column: 0, message }));
      reportAssets();
    },
    newProject() {
      if (!window.confirm(t('現在のコードと素材を新規プロジェクトで置き換えますか？', 'Replace the current code and assets with a new project?'))) return;
      applyProject({ projectName: 'my-game', source: DEFAULT_SOURCE, assets: [] });
      state.lastBuild = null; stopPreview(); scheduleSave();
    }
  });
  window.GamingWeb.ready();

  const forwardedKeys = new Set(['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'z', 'Z', 'Enter', ' ', 'x', 'X', 'q', 'Q', 'e', 'E', 'p', 'P', 'o', 'O']);
  window.addEventListener('keydown', (event) => {
    if (pixelDialog.open || sampleDialog.open || isTypingTarget(event.target) || !forwardedKeys.has(event.key)) return;
    event.preventDefault();
    runner.contentWindow?.postMessage({ type: 'studio-key', action: 'keydown', key: event.key }, location.origin);
  });
  window.addEventListener('keyup', (event) => {
    if (pixelDialog.open || sampleDialog.open || isTypingTarget(event.target) || !forwardedKeys.has(event.key)) return;
    event.preventDefault();
    runner.contentWindow?.postMessage({ type: 'studio-key', action: 'keyup', key: event.key }, location.origin);
  });

  window.addEventListener('message', (event) => {
    if (event.source !== runner.contentWindow || event.origin !== location.origin) return;
    const data = event.data || {};
    if (data.type === 'studio-ready' && state.pendingRun) {
      const payload = state.pendingRun;
      state.pendingRun = null;
      const transfer = [payload.mrb, ...payload.assets.map((asset) => asset.bytes)];
      runner.contentWindow.postMessage(payload, location.origin, transfer);
    } else if (data.type === 'studio-status') {
      setPreviewStatus(data.message);
    } else if (data.type === 'studio-runtime-error') {
      setPreviewStatus(t('実行エラー', 'Runtime error'));
      window.GamingWeb.emit('studio-runtime-error', { message: data.message });
    }
  });

  (async () => {
    state.db = await openDatabase();
    const saved = await loadProject(state.db);
    applyProject(saved || { projectName: 'my-game', source: DEFAULT_SOURCE, assets: [] });
    publish({ ready: true });
    if (!state.db) publish({ save: t('保存エラー', 'Save failed') });
  })();

  (async () => {
    try {
      const response = await fetch(window.GamingWeb.resource('samples/manifest.json'));
      if (!response.ok) return;
      const samples = await response.json();
      if (!Array.isArray(samples)) return;
      state.samples = samples.filter((sample) => sample && /^[a-z0-9-]+$/.test(sample.id) &&
        typeof sample.title === 'string' && Array.isArray(sample.assets) &&
        sample.assets.every((name) => safeAssetName(name)));
      publish({ samples: state.samples.map(({ id, title }) => ({ id, title })) });
    } catch (error) { console.warn('サンプル一覧を読み込めません', error); }
  })();

}
