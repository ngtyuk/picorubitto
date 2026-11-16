// Verify the real WASM stdout byte stream, including every newline.
import fs from 'node:fs/promises';
import createModule from '../../emulator/picoruby/picoruby.js';

globalThis.window = globalThis;
let completed = false;
const errors = [];
const mod = await createModule({
  locateFile: () => 'http://127.0.0.1:8080/emulator/picoruby/picoruby.wasm',
  print: line => { if (line === '[CONSOLE] END') completed = true; },
  printErr: line => errors.push(line)
});
if (mod.ccall('picorb_init', 'number', [], []) !== 0) throw new Error('VM init failed');
const stdout = mod.FS.getStream(1);
const original = stdout.stream_ops.write;
const chunks = [];
stdout.stream_ops = {...stdout.stream_ops, write(stream, buffer, offset, length, position) {
  chunks.push(Buffer.from(buffer.subarray(offset, offset + length)));
  return original(stream, buffer, offset, length, position);
}};
const source = await fs.readFile(new URL('../portable/game_api_contract.rb', import.meta.url), 'utf8');
const probe = "HARDWARE_SUITE = true\n" + source.replace("require 'game'\n", '') + '\nrun_console_output_contract\n';
if (mod.ccall('picorb_create_task', 'number', ['string'], [probe]) !== 0) throw new Error('probe compilation failed');
const deadline = performance.now() + 10000;
while (!completed && !errors.length && performance.now() < deadline) {
  mod._mrb_tick_wasm();
  for (let i = 0; i < 100; i++) {
    const status = mod._mrb_run_step_status();
    if (status < 0 || status === 2) throw new Error('probe task failed: ' + errors.join('\n'));
    if (status === 0) break;
  }
  await new Promise(resolve => setTimeout(resolve, 1));
}
const actual = Buffer.concat(chunks);
const expected = await fs.readFile(new URL('../portable/console_output.txt', import.meta.url));
if (!completed || errors.length || !actual.equals(expected)) {
  throw new Error('stdout mismatch: ' + JSON.stringify({actual: actual.toString(), expected: expected.toString(), errors}));
}
console.log(`CONSOLE OUTPUT PASS ${actual.length} bytes (real WASM stdout)`);
