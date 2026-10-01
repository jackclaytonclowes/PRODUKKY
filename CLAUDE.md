# FRACTURE and CRATE

Two JUCE 8 audio plugins (FRACTURE in `plugin/`, CRATE in `crate/`), FRACTURE's browser
version (`fx/fracture.html`) and a static download site (`plugins-site/`). `README.md` has
the overview; each product's README says what is verified and what is not.

## Commands

```
npm test                   # all suites that need no JUCE
npm run test:core          # FRACTURE DSP, bare compiler
npm run test:crate         # CRATE DSP, bare compiler
npm run test:sanitize      # both, under AddressSanitizer and UBSan (slow; run before a release)
npm run test:fx            # browser version, headless Chromium
npm run test:plugins-site  # the download site
npm run reference          # after changing shapers or presets in fx/fracture.html
npm run audition           # render every preset to WAV

cmake -S plugin -B build-frac -DCMAKE_BUILD_TYPE=Release && cmake --build build-frac --target host_smoke -j
xvfb-run -a build-frac/host_smoke_artefacts/Release/host_smoke shot.png      # same for crate/
```

On Linux, JUCE needs the X11, freetype and ALSA dev packages. Validate a VST3 build with
pluginval at `--strictness-level 10` across several `--random-seed`s before calling a
wrapper change done.

## How the code is split

- **`core/` never includes JUCE.** All DSP, the parameter table, relevance rules, presets and
  the undo stacks live there and are tested with a bare compiler. `Source/` is the wrapper
  and the editor, and stays thin.
- **Shared files are copies, kept identical:** `Source/Guide.h`, `Source/Session.h` and
  `core/History.h` are byte-for-byte the same in both products. Change one, copy it to the
  other. `Bauhaus.h` differs only in its header comment. `RhythmMod.h` is the same class in
  each product's namespace, but CRATE's copy carries its own division list, so keep the two
  in step by hand.
- **`fx/fracture.html` is the source of truth for FRACTURE's browser presets and shapers.**
  `npm run reference` regenerates `plugin/core/Presets.h` and the reference data; the
  plugin-only presets are in `plugin/core/FactoryPresets.h`.

## Rules that are load-bearing

- **Parameters are appended, never reordered or renamed.** Hosts save sessions and
  automation by id, and A/B slots and user presets store ids too. Matrix sources are
  appended after the existing ones for the same reason. New presets go at the end of the
  menu.
- **A dimmed control must really do nothing.** `core/Relevance.h` decides what the panel
  fades, and `test_core` moves every control it calls idle end to end and requires
  bit-identical output. Change the DSP, re-run it.
- **Every preset is levelled:** within 3 dB of the dry loop's loudness, K-weighted
  (`audition::loudness`, ITU BS.1770, not RMS: RMS reads a bright, distorted preset as
  quieter than it sounds), peak at most 0.95, and no two presets identical. FRACTURE's Init
  is the defaults and only held to 6 dB. The tests enforce it; fix a preset's level rather than loosening the
  test.
- **Every mouse control brackets its change in begin/endChangeGesture.** Undo takes one
  step when no gesture is open, so a control that skips this makes undo wrong. When a
  control moves from one parameter to the next, open the next gesture before closing the
  last one.
- **`setStateInformation` writes every parameter back** after `replaceState`. JUCE skips
  values that look unchanged after snapping, which left switches restored with fractional
  values. pluginval found it.
- **Control changes glide.** Crossovers, tilts and filter retunes ramp every 16 samples,
  because stepping them once per host block zippered by 6 to 8 dB. The zipper test compares
  512-sample and 16-sample automation.
- **The oversampler's callback is stateful, so it must be called in order.** Never write
  `down(f(a), f(b))`: C++ leaves the order of the two calls unspecified, and GCC reversed
  it, feeding every filter and anti-aliased shaper its sample pairs swapped (clang did not,
  so Mac builds never showed it). One call per statement. `test_core` feeds a ramp and
  fails on any step backwards.
- **Modulation runs on the engine's clock, not the host's block.** `Engine::process` works out
  the LFOs, envelope and matrix every `controlStep` (32) samples counted from reset, and a
  host block that ends mid-step just pauses the sample loop. Anything that depends on the
  host's block size makes a bounce differ from playback; `test_core` renders every preset at
  64, 100 and 1024 and requires the same sound. Per-step state the sample loop needs goes in
  `Step`, not in locals.
- **The shapers are anti-aliased (ADAA, `core/Shapers.h`).** A new shaper mode needs its
  antiderivative in `antiderivative()`, or an entry in `hasAntiderivative()` saying why
  not. Each anti-aliased stage adds half a sample at the oversampled rate, which
  `splitResponse()` includes so the tuned loop stays in tune. The browser version runs the
  same scheme in its `shaper` worklet: each mode in `fx/fracture.html`'s `MODES` carries
  `F`, which must match `antiderivative()` mode for mode.
- **Table (the drawn mode) is plugin-only and lives in `core/HarmonicTable.h`.** `shape()` has
  only a stand-in for it; the engine and the editor build the real curve from the 64 bar
  parameters. The bars are parameters so undo, A/B and presets carry them, marked not
  automatable. Position (`tblPos`) is the newest matrix target, so it stays last.
- **Tests measure; they do not restate the code.** Several early tests passed against
  broken code: a dry/wet check that compared a signal with itself, a pitch test that
  measured group delay. Prove each new test fails when the fix is removed.
- **Look at the screenshots.** `host_smoke` renders both editors to PNG, and the site test
  renders the site. Layout regressions do not fail assertions.

## Not yet known

Nobody has listened to either plugin in a real session. Every number on the panels is
measured; whether it sounds good is not. Do not claim otherwise in docs, presets or the
site. The Mac AU still needs `auval` on a real Mac (`build-macos.sh` runs it), and the
builds are unsigned.

## JUCE 8 notes

- There is no `Font::getStringWidth`; use `juce::GlyphArrangement::getStringWidth`.
- Only the base look (`Bauhaus.h`) styles components; it does not dim disabled buttons, so
  set alpha yourself (see `SessionBar`).
