/**
 * Builds and runs the measurement suite with nothing but a C++ compiler — the
 * DSP has no dependency on JUCE, which is the point of keeping it in core/.
 *
 *   npm run test:crate
 */
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const out = path.join(os.tmpdir(), 'crate-test-core');

const compiler = ['c++', 'g++', 'clang++'].find(c => {
  try { execFileSync(c, ['--version'], { stdio: 'ignore' }); return true; } catch { return false; }
});
if (!compiler) {
  console.error('No C++ compiler found (tried c++, g++, clang++).');
  process.exit(1);
}
console.log(`compiling with ${compiler}`);
// SANITIZE=1 (npm run test:sanitize) builds with AddressSanitizer and UBSan,
// which fail the run on any read past a buffer or undefined arithmetic. A read
// one float past CRATE's swing delay went unnoticed until this found it.
const sanitize = process.env.SANITIZE ? ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                                         '-fno-sanitize-recover=all'] : ['-O2'];
execFileSync(compiler, ['-std=c++17', ...sanitize, '-Wall', '-Wextra',
                        '-I', path.join(here, '..', 'core'),
                        '-o', out, path.join(here, 'test_core.cpp')],
            { stdio: 'inherit' });
execFileSync(out, [], { stdio: 'inherit' });
