/**
 * Renders every CRATE and FRACTURE preset over a loop, with the real DSP cores
 * and nothing but a C++ compiler, into dist/audition/<plugin>/ as WAV files and
 * a manifest of their levels.
 *
 *   npm run audition                      the built-in test loops
 *   npm run audition -- my-break.wav      your own loop, through both plugins
 *
 * Nothing in this repository had been heard when these were written; the
 * renders are how it gets heard without a DAW. They found a preset that had
 * become a copy of another, and three presets 12 to 25 dB under the dry loop.
 */
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, '..', '..');
const out = path.join(root, 'dist', 'audition');
const input = process.argv[2] ? path.resolve(process.argv[2]) : null;

const compiler = ['c++', 'g++', 'clang++'].find(c => {
  try { execFileSync(c, ['--version'], { stdio: 'ignore' }); return true; } catch { return false; }
});
if (!compiler) { console.error('No C++ compiler found (tried c++, g++, clang++).'); process.exit(1); }

const targets = [
  { name: 'crate', src: 'crate.cpp', inc: ['crate/core', 'crate/Source'] },
  { name: 'fracture', src: 'fracture.cpp', inc: ['plugin/core'] },
];
for (const t of targets) {
  const bin = path.join(os.tmpdir(), `${t.name}-audition`);
  const dir = path.join(out, t.name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  execFileSync(compiler, ['-std=c++17', '-O2', ...t.inc.flatMap(i => ['-I', path.join(root, i)]),
                          '-I', here, '-o', bin, path.join(here, t.src)], { stdio: 'inherit' });
  const manifest = execFileSync(bin, input ? [dir, input] : [dir]).toString();
  fs.writeFileSync(path.join(dir, 'manifest.json'), manifest);
  const m = JSON.parse(manifest), dry = m[0].rms;
  console.log(`\n${t.name.toUpperCase()} — level against the dry loop`);
  for (const e of m.slice(1)) {
    const rel = 20 * Math.log10(Math.max(e.rms, 1e-9) / dry);
    const flag = (e.peak >= 0.999 ? '  at the ceiling' : '') + (rel < -10 ? '  quiet' : '') + (rel > 8 ? '  loud' : '');
    console.log(`  ${((rel >= 0 ? '+' : '-') + Math.abs(rel).toFixed(1)).padStart(6)} dB  ${e.name}${flag}`);
  }
}
console.log(`\nWAV files in ${path.relative(process.cwd(), out)}/`);
