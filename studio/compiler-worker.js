/* global self */
'use strict';

const compilerBase = new URL('./runtime/', self.location.href);

function compilerUrl(path) {
  const url = new URL(path, compilerBase);
  const revision = new URL(self.location.href).searchParams.get('v');
  if (revision) url.searchParams.set('v', revision);
  return url.href;
}

function parseDiagnostics(lines) {
  return lines.filter(Boolean).map((line) => {
    const match = line.match(/:(\d+):(\d+):\s*(.*)$/);
    if (!match) return { severity: 'error', line: 0, column: 0, message: line };
    return {
      severity: 'error', line: Number(match[1]), column: Number(match[2]), message: match[3]
    };
  });
}

let compilerPromise;
let activeOutput;

async function createCompiler() {
  const moduleSource = await import(compilerUrl('mrbc.js'));
  return moduleSource.default({
    locateFile(path) { return compilerUrl(path); },
    noInitialRun: true,
    print(line) { if (activeOutput) activeOutput.stdout.push(String(line)); },
    printErr(line) { if (activeOutput) activeOutput.stderr.push(String(line)); }
  });
}

async function getCompiler() {
  if (!compilerPromise) {
    compilerPromise = createCompiler();
    compilerPromise.catch(() => { compilerPromise = null; });
  }
  return compilerPromise;
}

function ensureDirectory(fs, path) {
  try {
    fs.readdir(path);
  } catch (_error) {
    fs.mkdir(path);
  }
}

function removeFileIfPresent(fs, path) {
  try { fs.unlink(path); } catch (_error) { /* first run or previous failed build */ }
}

async function compile(request) {
  const t = (ja, en) => request.language === 'en' ? en : ja;
  const output = { stdout: [], stderr: [] };
  let module = null;
  try {
    module = await getCompiler();
    const fs = module.FS;
    if (!fs) throw new Error(t('mrbcの仮想ファイルシステムが利用できません', 'The compiler filesystem is unavailable'));
    ensureDirectory(fs, '/studio');
    removeFileIfPresent(fs, '/studio/main.rb');
    removeFileIfPresent(fs, '/studio/game.mrb');
    fs.writeFile('/studio/main.rb', request.source);
    activeOutput = output;

    // Emscripten's callMain adds argv[0] itself. Pass only compiler switches
    // and input paths, otherwise mrbc treats the extra label as a source.
    const args = request.mode === 'check'
      ? ['-c', '/studio/main.rb']
      : ['-o', '/studio/game.mrb', '/studio/main.rb'];
    let exitCode = 0;
    try {
      const result = module.callMain(args);
      exitCode = Number.isInteger(result) ? result : 0;
    } catch (error) {
      exitCode = Number.isInteger(error && error.status) ? error.status : 1;
    }

    activeOutput = null;
    const diagnostics = parseDiagnostics(output.stderr);
    if (exitCode !== 0 && diagnostics.length === 0) {
      diagnostics.push({ severity: 'error', line: 0, column: 0, message: t('mrbcがコンパイルに失敗しました', 'Compilation failed') });
    }
    if (exitCode !== 0) return { ok: false, diagnostics };
    if (request.mode === 'check') return { ok: true, diagnostics: [] };

    let bytes;
    try {
      bytes = fs.readFile('/studio/game.mrb');
    } catch (error) {
      const files = fs.readdir('/studio').join(', ');
      const details = [...output.stderr, ...output.stdout].join(' | ');
      throw new Error(t(`${error.message} (生成ファイル: ${files || 'なし'}${details ? `; ${details}` : ''})`, `${error.message} (generated files: ${files || 'none'}${details ? `; ${details}` : ''})`));
    }
    const buffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
    return { ok: true, diagnostics: [], mrb: buffer };
  } catch (error) {
    return {
      ok: false,
      diagnostics: [{ severity: 'error', line: 0, column: 0, message: error && error.message ? error.message : String(error) }]
    };
  } finally {
    activeOutput = null;
  }
}

let compileQueue = Promise.resolve();

self.addEventListener('message', async (event) => {
  const request = event.data || {};
  if (!request.id || (request.mode !== 'check' && request.mode !== 'build')) return;
  compileQueue = compileQueue.then(async () => {
    const result = await compile(request);
    self.postMessage({ id: request.id, ...result }, result.mrb ? [result.mrb] : []);
  }).catch((error) => {
    self.postMessage({
      id: request.id,
      ok: false,
      diagnostics: [{ severity: 'error', line: 0, column: 0, message: error.message || String(error) }]
    });
  });
});
