// Fixtures come from tests/native/texture_binding_regression.py, never a game.
import createModule from '../../emulator/picoruby/picoruby.js';

const output = document.getElementById('results');
const check = (condition, message) => { if (!condition) throw new Error(message); };
try {
  const base = new URL('../../build/tests/texture-draw/', import.meta.url);
  const cases = await (await fetch(new URL('cases.json', base))).json();
  window.GAME_ASSET_BASE = base.href;
  window.GAME_ASSETS = ['keyed.raw', 'opaque.raw', 'custom.raw'];
  const script = document.createElement('script');
  script.src = '../../emulator/emulator.js';
  await new Promise((resolve, reject) => {
    script.onload = resolve; script.onerror = reject; document.body.appendChild(script);
  });
  const g = window.GameEmulator;
  await g.ready;
  let completed = 0, sequence = 0;
  const errors = [];
  const mod = await createModule({
    locateFile: name => new URL(`../../emulator/picoruby/${name}`, import.meta.url).href,
    print: line => {
      if (line.startsWith('TEXTURE_DRAW_DONE ')) completed = Number(line.slice(18));
      else console.log(line);
    },
    printErr: line => { errors.push(line); console.error(line); }
  });
  check(mod.ccall('picorb_init', 'number', [], []) === 0, 'VM init');
  async function ruby(source) {
    const id = ++sequence;
    check(mod.ccall('picorb_create_task', 'number', ['string'],
      [source + `\nprint 'TEXTURE_DRAW_DONE ${id}', "\\n"`]) === 0, 'Ruby compilation');
    const deadline = performance.now() + 10000;
    while (completed !== id && !errors.length && performance.now() < deadline) {
      mod._mrb_tick_wasm();
      for (let step = 0; step < 100; step++) {
        const status = mod._mrb_run_step_status();
        check(status >= 0 && status !== 2, 'Ruby task exception');
        if (status === 0) break;
      }
      await new Promise(resolve => setTimeout(resolve, 0));
    }
    check(completed === id && !errors.length, 'Ruby task completion: ' + errors.join('\n'));
  }
  const adapter = await (await fetch('../../emulator/game_adapter.rb')).text();
  await ruby(adapter + `
$textures = [Game::Texture.load('keyed.raw',512,24),
             Game::Texture.load('opaque.raw',512,24),
             Game::Texture.load('custom.raw',512,24,0x1234)]`);
  const context = document.getElementById('screen').getContext('2d');
  const originalDraw = context.drawImage.bind(context);
  let calls = [];
  context.drawImage = (image, ...args) => { calls.push(args); return originalDraw(image, ...args); };
  let checked = 0;
  for (const c of cases.filter(c => !c.error)) {
    const texture = ['keyed', 'opaque', 'custom'].indexOf(c.mode);
    g.clear(0x39e7); calls = [];
    await ruby(`raise 'draw return' unless $textures[${texture}].draw(${c.args.join(',')}).nil?`);
    g.present();
    const expectedArgs = [...c.source, ...c.args.slice(0, 2), ...c.destination];
    check(calls.length === 1 && JSON.stringify(calls[0]) === JSON.stringify(expectedArgs),
      c.name + ': resolved drawImage arguments');
    const actual = context.getImageData(0, 0, 320, 240).data;
    g.clear(0x39e7);
    const explicit = [...c.args.slice(0, 2), ...c.source, ...c.destination];
    await ruby(`$textures[${texture}].draw(${explicit.join(',')})`);
    g.present();
    const reference = context.getImageData(0, 0, 320, 240).data;
    check(actual.every((byte, i) => byte === reference[i]), c.name + ': full Canvas equivalence');
    checked++;
  }
  output.textContent = `PASS ${checked} real WASM calls: 2–8 arguments, keyed/custom/opaque, ` +
    'crop, screen clipping, explicit scaling\nTEXTURE DRAW PASS';
} catch (error) {
  output.textContent = `FAIL ${error.stack || error}`;
  console.error(error);
}
