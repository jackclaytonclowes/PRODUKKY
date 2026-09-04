/**
 * Builds and runs the DSP tests with nothing but a C++ compiler — no CMake, no
 * JUCE. The core is deliberately dependency-free so this is all it takes.
 *
 *   npm run test:core
 */
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
const out = path.join(os.tmpdir(), 'fracture-test-core');

const compiler = ['c++', 'g++', 'clang++'].find(c => {
  try { execFileSync(c, ['--version'], { stdio: 'ignore' }); return true; } catch { return false; }
});
if (!compiler) {
  console.error('No C++ compiler found (tried c++, g++, clang++).');
  process.exit(1);
}
for (const f of ['shaper_reference.csv', 'presets.json']) {
  if (!fs.existsSync(path.join(here, f))) {
    console.error(`Missing plugin/tests/${f} — run: npm run reference`);
    process.exit(1);
  }
}

console.log(`compiling with ${compiler}`);
execFileSync(compiler, ['-std=c++17', '-O2', '-Wall', '-Wextra',
                        '-I', path.join(root, 'plugin', 'core'),
                        '-o', out, path.join(here, 'test_core.cpp')],
            { stdio: 'inherit' });
execFileSync(out, [path.join(here, 'shaper_reference.csv'), path.join(here, 'presets.json')],
             { stdio: 'inherit' });
