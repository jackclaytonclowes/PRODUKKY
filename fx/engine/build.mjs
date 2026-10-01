/**
 * Compiles the plugin's engine (plugin/core, through fx/engine/fracture-wasm.cpp)
 * to WebAssembly and embeds it in fx/fracture.html, so the browser version runs
 * FRACTURE's own DSP and the page is still one file that makes no requests.
 *
 *   npm run wasm
 *
 * Needs clang with the wasm32 target, wasm-ld and a wasm32-wasi C++ library
 * (Ubuntu: clang lld wasi-libc libc++-18-dev-wasm32 libc++abi-18-dev-wasm32
 * libclang-rt-18-dev-wasm32). Nobody opening the page needs any of this: the
 * compiled engine is committed inside the page.
 *
 * Next to it the page records a hash of the sources it was built from.
 * tests/fx.mjs recomputes that hash, so changing plugin/core without running
 * this fails the browser suite rather than leaving the page on an old engine.
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
const page = path.join(root, 'fx', 'fracture.html');

export function sourceHash(){
  const files = [path.join(here, 'fracture-wasm.cpp'),
    ...fs.readdirSync(path.join(root, 'plugin', 'core')).filter(f => f.endsWith('.h')).sort()
      .map(f => path.join(root, 'plugin', 'core', f))];
  const h = crypto.createHash('sha256');
  for (const f of files){ h.update(path.relative(root, f)); h.update('\0'); h.update(fs.readFileSync(f)); h.update('\0'); }
  return h.digest('hex').slice(0, 16);
}

// What the page carries besides the engine, each from the plugin's own source:
// the guide (GUIDE.md, which the plugin compiles in), the tooltips (helpFor()
// in the plugin's editor) and the version (plugin/CMakeLists.txt). The test
// calls these too, so a stale copy in the page fails it.
export function guideText(){ return fs.readFileSync(path.join(root, 'plugin', 'GUIDE.md'), 'utf8'); }
export function tips(){
  const src = fs.readFileSync(path.join(root, 'plugin', 'Source', 'PluginEditor.cpp'), 'utf8');
  const a = src.indexOf('static juce::String helpFor('), b = src.indexOf('};', a);
  if (a < 0 || b < 0) throw new Error('cannot find helpFor() in plugin/Source/PluginEditor.cpp');
  const out = {};
  for (const m of src.slice(a, b).matchAll(/\{ "([^"]+)", "((?:[^"\\]|\\.)*)" \}/g)) out[m[1]] = JSON.parse('"' + m[2] + '"');
  return out;
}
// the plugin's own presets and the menu's headings, from plugin/core/FactoryPresets.h
export function pluginPresets(){
  const src = fs.readFileSync(path.join(root, 'plugin', 'core', 'FactoryPresets.h'), 'utf8');
  const a = src.indexOf('pluginOnlyPresets()'), b = src.indexOf('return all;', a);
  return [...src.slice(a, b).matchAll(/\{ "((?:[^"\\]|\\.)+)",\s*R"JSON\((.*?)\)JSON" \}/gs)]
    .map(m => ({ name: JSON.parse('"' + m[1] + '"'), v: JSON.parse(m[2]) }));
}
export function presetHeadings(){
  const src = fs.readFileSync(path.join(root, 'plugin', 'core', 'FactoryPresets.h'), 'utf8');
  const a = src.indexOf('presetCategory('), b = src.indexOf('};', a);
  return Object.fromEntries([...src.slice(a, b).matchAll(/\{ "((?:[^"\\]|\\.)+)", "([^"]+)" \}/g)].map(m => [JSON.parse('"' + m[1] + '"'), m[2]]));
}
export function version(){
  const m = /project\(\s*Fracture\b[^)]*VERSION\s+([0-9.]+)/i.exec(fs.readFileSync(path.join(root, 'plugin', 'CMakeLists.txt'), 'utf8'));
  if (!m) throw new Error('cannot find the version in plugin/CMakeLists.txt');
  return m[1];
}
function splice(html, start, end, body){
  const a = html.indexOf(start), b = html.indexOf(end, a + start.length);
  if (a < 0 || b < 0) throw new Error(`fx/fracture.html has no ${start} ... ${end} block`);
  return html.slice(0, a + start.length) + body + html.slice(b);
}

if (process.argv[1] === fileURLToPath(import.meta.url)){
  const out = path.join(here, 'fracture.wasm');
  execFileSync('clang++', ['--target=wasm32-wasi', '--sysroot=/usr', '-std=c++17', '-O3', '-fno-exceptions',
    '-mexec-model=reactor', '-Wl,--strip-all', '-Wl,--export=_initialize',
    path.join(here, 'fracture-wasm.cpp'), '-o', out], { stdio: 'inherit' });
  const bytes = fs.readFileSync(out);
  fs.rmSync(out);
  // the WebAssembly build against the same C++ built natively, every preset
  const { parity } = await import('./parity.mjs');
  const r = parity(root, bytes, pluginPresets());
  const chaotic = r.worst.filter(([, , all]) => all > 1e-5);
  console.log(`parity — ${r.worst.length} presets: the first 2048 samples agree to ` +
    `${Math.max(...r.worst.map(w => w[1])).toExponential(1)}; ${r.worst.length - chaotic.length} agree to 1e-5 throughout` +
    (chaotic.length ? `, and ${chaotic.length} part later, where a crusher's step or a loop through a fold turns a last-bit difference into more (${chaotic.map(w => w[0].split(' —')[0]).join(', ')})` : ''));
  if (r.failed) throw new Error(`${r.failed} presets differ between the WebAssembly and native builds from the first samples`);
  const b64 = bytes.toString('base64');
  let html = fs.readFileSync(page, 'utf8');
  html = splice(html, '/*WASM:START*/', '/*WASM:END*/', `{ source: '${sourceHash()}', bytes: '${b64}' }`);
  html = splice(html, '/*GUIDE:START*/', '/*GUIDE:END*/', JSON.stringify(guideText()));
  html = splice(html, '/*TIPS:START*/', '/*TIPS:END*/', JSON.stringify(tips()));
  html = splice(html, '/*VERSION*/', '/*VERSION*/', `'${version()}'`);
  html = splice(html, '/*PLUGIN_PRESETS:START*/', '/*PLUGIN_PRESETS:END*/', JSON.stringify(pluginPresets()));
  html = splice(html, '/*HEADINGS:START*/', '/*HEADINGS:END*/', JSON.stringify(presetHeadings()));
  fs.writeFileSync(page, html);
  console.log(`fx/fracture.html — engine embedded, ${bytes.length} bytes (${b64.length} as base64), source ${sourceHash()}`);
}
