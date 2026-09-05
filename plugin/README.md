# FRACTURE — VST3 / AU plugin

A native port of `../fx/fracture.html`. Same signal path, same fourteen shapers, same
parameter ids, same nine presets — so a patch copied out of the browser version loads here
and means the same thing.

## Getting an Audio Unit for Logic

An AU is a macOS binary, so it has to be built on a Mac. Two ways:

**On your Mac** — one command, given Xcode command line tools
(`xcode-select --install`) and CMake (`brew install cmake`):

```
./plugin/build-macos.sh
```

It fetches JUCE, builds a universal (arm64 + x86_64) AU, VST3 and standalone app, installs
them into `~/Library/Audio/Plug-Ins/`, and then runs `auval -v aufx Frcd Frct` — the same
validation Logic runs before it will load a plugin. Restart Logic and FRACTURE appears
under Audio Units > Fracture > Distortion.

**Without a Mac toolchain** — GitHub builds it for you.
`.github/workflows/plugin-macos.yml` runs on a macOS runner: Actions tab > "Build macOS
plugin" > Run workflow, then download the `FRACTURE-macOS` artifact from the finished run.
It runs the DSP tests and `auval` on the way through, so a green run means a validated
plugin. On a private repository macOS runner minutes bill at 10x, so each ~10 minute build
costs about 100 minutes of quota — worth knowing before wiring it to every push.

Either way the binary is unsigned. That is fine for your own machine; distributing it to
anyone else means signing and notarising it with a Developer ID, or they will have to strip
the quarantine flag by hand.

## Building by hand

```
cmake -B build -DCMAKE_BUILD_TYPE=Release          # fetches JUCE 8.0.15
cmake --build build --target Fracture_VST3 -j      # also Fracture_AU on macOS
cmake --build build --target Fracture_Standalone -j

npm run test:core                                  # DSP tests, compiler only, no JUCE
cmake --build build --target host_smoke && ./build/host_smoke_artefacts/Release/host_smoke
```

`COPY_PLUGIN_AFTER_BUILD` is on, so a successful build installs into your user plugin
folder. On macOS that is `~/Library/Audio/Plug-Ins/VST3` and `.../Components`; Logic will
find the AU after a restart (`killall -9 AudioComponentRegistrar` if it doesn't).

Verified against JUCE 8.0.15 (what the build pins) and JUCE 9 on Linux.

## Layout

```
core/               the DSP. No JUCE, no dependencies, no allocation in the audio path
  Shapers.h         the 14 modes, ported function for function from the JavaScript
  Biquad.h          RBJ cookbook, the same formulas Web Audio uses
  Oversampler.h     2x/4x linear-phase FIR, plus the delay lines
  Crusher.h         bit depth and decimation, a port of the AudioWorklet
  Modulation.h      two LFOs, envelope follower, the six-slot matrix
  ParamTable.h      ONE parameter table — the DSP, the host and patch import share it
  Presets.h         generated from the browser presets; do not edit
  FractureCore.h    the whole processor
Source/             the JUCE wrapper: parameters, state, latency, and the interface
tests/              the DSP tests, the host-level smoke test, and the reference data
tools/              regenerates the reference data from fx/fracture.html
```

The split is the point: **the DSP does not know JUCE exists**, so it builds and is tested
with a bare compiler, and the plugin layer stays thin enough to read in one sitting.

## What the port changed on purpose

The browser version's constraints are not a plugin's constraints, so five things are
deliberately different, and the tests pin each one:

- **Drive evaluates `f(x · drive)` per sample.** The browser had to bake a fixed curve over
  [-40, +40] and pre-scale into it, because `WaveShaperNode` clamps its input and swapping
  curves at control rate clicks. Natively that machinery is unnecessary, so the
  curve-interpolation error is gone too.
- **Oversampling is real work now.** `oversample: '4x'` was one string; here it is a
  65-tap linear-phase FIR pair per band, which is why the plugin reports 48 samples of
  latency at 4x, 32 at 2x, 0 with it off. `test_core` measures the actual delay through the
  chain and fails if it disagrees with what the plugin tells the host.
- **The band's dry/wet crossfade happens inside the oversampled region**, so both paths
  share the up- and downsampling filters exactly and no compensation delay is needed.
- **The three-band split allpass-compensates the low band**, so the bands sum flat — the
  browser split sequentially without it. The test sweeps twelve frequencies and requires
  the sum to stay inside 0.5 dB.
- **The safety clip is a unity-knee limiter.** The browser used `tanh(1.4x)/tanh(1.4)`,
  whose slope at the origin is 1.58 — it quietly added about 4 dB and made the feedback
  loop gain more than its own amount setting. The shape here is transparent below 0.7 and
  saturates to exactly 1.0, so "feedback 85%" really is a decaying loop.
- **The post filter's resonance is a true Q**, and its "off" setting bypasses it rather
  than parking an allpass at 20 kHz. Web Audio reads `Q` in *decibels* for lowpass and
  highpass — a spec quirk — so cutoff and resonance feel slightly different from the same
  numbers in the browser. Everything else about a patch transfers exactly.
- **The pre-filters drop out of circuit at their extremes.** A 2nd-order highpass parked at
  20 Hz still costs 0.8 dB at 30 Hz, which the browser version paid on every default patch.

Two things the plugin gains from being a plugin: modulation runs per block instead of at
animation-frame rate, and the random LFO shapes are seeded from the transport position, so
two bounces of the same bar are identical. `test_core` renders the same passage twice and
requires the samples to match bit for bit.

## What is verified, and where

`npm run test:core` — 50 assertions, no JUCE needed:

- **every shaper matches the JavaScript to 1e-12** across 9,114 points, including the
  half-steps where JS and C round differently. `tools/make-reference.mjs` pulls the
  functions straight out of `fx/fracture.html`, so a mistyped formula fails here rather
  than shipping as a different-sounding plugin
- all 14 modes at maximum drive through both stages: finite, bounded, audible
- feedback at 85% for 0.6 s stays bounded; the crusher changes the signal; the worst mode
  is contained by the safety clip
- one, two and three bands audible; muting every band silences the wet path; solo works
- reported latency is the measured latency; the three bands sum flat
- a seeded render is bit-identical twice over
- all nine browser presets load and render, and every value in every browser patch maps to
  a plugin parameter
- 44.1 / 48 / 96 kHz, and block sizes from 16 to 1024

`host_smoke` — 23 assertions at the host level: parameters exposed, latency reported,
blocks run without NaN, every preset renders, state round-trips, a browser patch imports
and comes back out unchanged, and **the editor renders to a PNG** so the interface can be
looked at without opening a DAW.

## Honest gaps

- **Nothing here has been heard.** It builds clean and the numbers are right, but the
  container that produced it has no audio device and no plugin host. The first real test is
  yours: load it, and listen for zipper noise on fast modulation (parameters are smoothed
  per block, filter coefficients recomputed per block) and for aliasing on the crush.
- The oversampler is correct but not cheap — around 780 multiply-adds per sample per band
  per channel at 4x. If CPU matters, swap `Oversampler` for `juce::dsp::Oversampling`,
  which is polyphase; the interface is a drop-in.
- No tempo sync yet. The host tempo is available now, so LFO-per-beat is a small addition.
- No GUI resizing beyond uniform scaling, and the editor is 1180 x 1190 by design.
- `pluginval` has not been run — do that before you trust it in a session.
- The JUCE splash screen is left on, since disabling it needs a JUCE licence. `AGPL`/GPL
  or a paid licence also decides whether you can distribute a build; the VST3 SDK is
  separately dual-licensed by Steinberg.

## Regenerating the reference data

```
npm run reference        # after changing the shapers or the presets in fx/fracture.html
```

That rewrites `tests/shaper_reference.csv`, `tests/presets.json` and `core/Presets.h`.
Never edit those three by hand — `fx/fracture.html` is the source of truth for the maths.
