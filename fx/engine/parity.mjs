/**
 * The browser's engine against the plugin's: every preset in fx/fracture.html,
 * rendered by the WebAssembly build the page carries and by the same C++
 * compiled for this machine, sample by sample. Run by `npm run wasm`.
 *
 * The two use different maths libraries (wasi-libc's and the system's), whose
 * sin and exp can differ in the last bit. Through most patches that stays in
 * the last bit (all but one preset agree to under 1e-6 throughout). Two things
 * make more of it: a crusher, where a last-bit difference that lands on a
 * quantiser step comes out as a whole step (Radio, two steps of 10 bits), and
 * a feedback loop through a folding shaper, where it grows as any chaotic
 * system grows any difference. The plugin built on a Mac parts from the plugin
 * built on Linux in the same places. So the first 2048 samples must agree to
 * 1e-5, and the whole render is reported.
 */
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

export function parity(root, wasmBytes, pluginPresets){
  const src = fs.readFileSync(path.join(root, 'fx', 'fracture.html'), 'utf8');
  const grab = (a, b) => { const i = src.indexOf(a); return src.slice(i + a.length, src.indexOf(b, i)); };
  const presets = [...new Function('return [' + grab('const PRESETS = [', '\n];') + ']')(), ...pluginPresets];
  const exe = path.join(os.tmpdir(), 'fracture-native-' + process.pid);
  execFileSync('clang++', ['-std=c++17', '-O2', '-w', path.join(root, 'fx', 'engine', 'native.cpp'), '-o', exe]);
  const x = new WebAssembly.Instance(new WebAssembly.Module(wasmBytes),
    { wasi_snapshot_preview1: { fd_close: () => 0, fd_seek: () => 0, fd_write: () => 0 } }).exports;
  x._initialize();
  const str = p => { const b = new Uint8Array(x.memory.buffer); let e = p; while (b[e]) e++; return new TextDecoder().decode(b.subarray(p, e)); };
  x.fx_init(48000);
  const table = [];
  for (let i = 0; i < x.fx_param_count(); i++){
    const choices = [];
    for (let k = 0; k < x.fx_param_choices(i); k++) choices.push(str(x.fx_param_choice_id(i, k)));
    table.push({ id: str(x.fx_param_id(i)), kind: x.fx_param_kind(i), choices });
  }
  const toEngine = (p, v) => p.kind === 2 ? (v ? 1 : 0) : p.kind === 1 ? Math.max(0, p.choices.indexOf(String(v))) : +v;
  const blocks = 200, worst = [];
  let failed = 0;
  for (const pr of presets){
    const pairs = table.map((p, i) => p.id in pr.v ? [i, toEngine(p, pr.v[p.id])] : null).filter(Boolean);
    const native = new Float32Array(execFileSync(exe, [String(blocks)], { input: pairs.map(q => q.join(' ')).join('\n') + '\n', maxBuffer: 1 << 26 }).buffer.slice(0));
    x.fx_init(48000);
    for (const [i, v] of pairs) x.fx_set(i, v);
    x.fx_transport(120, 0, 0);
    let s = 7, k = 0, early = 0, all = 0;
    for (let b = 0; b < blocks; b++){
      const L = new Float32Array(x.memory.buffer, x.fx_buffer(0), 128), R = new Float32Array(x.memory.buffer, x.fx_buffer(1), 128);
      for (let n = 0; n < 128; n++){ s = (Math.imul(s, 1103515245) + 12345) >>> 0; const v = Math.fround((Math.fround((s >>> 9)/4194304) - 1)*0.4); L[n] = v; R[n] = Math.fround(-0.5*v); }
      x.fx_process(128);
      for (const buf of [L, R]) for (let n = 0; n < 128; n++, k++){
        const d = Math.abs(buf[n] - native[k]);
        all = Math.max(all, d);
        if (b < 8) early = Math.max(early, d);
      }
    }
    if (!(early < 1e-5)) failed++;
    worst.push([pr.name, early, all]);
  }
  fs.rmSync(exe, { force: true });
  return { failed, worst };
}
