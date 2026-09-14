# FRACTURE — VST3 / AU plugin

A native port of `../fx/fracture.html`. Same signal path, same fourteen shapers, same
parameter ids, same thirteen presets — so a patch copied out of the browser version loads
here and means the same thing.

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

## Sending it to other people

```
./plugin/build-macos.sh --dmg        # build, install, and pack plugin/dist/FRACTURE-0.1.0-macOS.dmg
```

The disk image carries the Audio Unit, the VST3, the standalone app, an installer and a
read-me, laid out over a Béton clair background that the packaging script draws (no binary
in the repository — same trick the extension icons use). `Install FRACTURE.command` copies
both plugins into the recipient's own plug-in folders, **strips the quarantine flag**, and
runs `auval` in front of them so they can see it pass.

That quarantine step is the whole reason for having an installer rather than a
drag-and-drop image. macOS marks anything downloaded as quarantined, and a quarantined
plug-in is refused *silently* by the host — Logic simply does not list it, with no error
to explain why. A drag-install would leave every recipient with an invisible failure.

Two things to tell people, both of which `READ ME FIRST.txt` also says:

- **The first launch needs a right-click.** The build is unsigned, so double-clicking the
  installer gets "unidentified developer". Right-click → Open → Open gets past it. Signing
  and notarising with a Developer ID ($99/year) is the only way to remove that step.
- **It has been tested by machine, not by ear.** The read-me says so, and asks for the
  specific things worth listening for: zipper noise on fast knob moves, whether the crush
  aliases in a broken way rather than a nasty one, and whether a bounce matches playback.

The CI workflow builds the same disk image and attaches it to the run; pushing a `v*` tag
also cuts a release with the DMG on it. Both links need a GitHub login while the repository
is private, so the simplest thing is to download the DMG once yourself and send the file.

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
  AnalogFilter.h    the nonlinear ladder: zero-delay feedback, saturation, drift
  Tremolo.h         shape, duty, edge and spread, locked to the host's bar
  Sync.h            the transport, and the note divisions that read from it
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

## The filter, and the tremolo

Two sections do more than the browser's Web Audio nodes could, and both are off by
default so that every patch made before they existed still sounds the same.

**Circuit** turns the post filter from a pair of biquads into a four-pole ladder.
`Clean` is the biquad pair, unchanged. `Analogue` and `Vintage` are a zero-delay-feedback
ladder (`core/AnalogFilter.h`) with saturation *inside* the loop, which is what makes
resonance squelch against a loud signal rather than ring through it, and what keeps
self-oscillation at a usable level instead of a divergence. `Vintage` saturates
asymmetrically — even harmonics, not just odd — and loses the top octave the way a real
one does. Every response is mixed from the same four taps, so changing Type or Slope
never re-tunes the resonance.

- **Drive** is a gain into the input stage, with most of it taken back out again, so it
  is audible as drive and not as level.
- **Drift** wanders the cutoff and resonance slowly and independently per channel. It is
  a few per cent, and it is the difference between two channels and one channel twice.
- The mark on **Cutoff** is the pole corner: it is where the resonance sings, which is the
  only calibration that lets a resonant sweep be played in tune. A four-pole is 3 dB down
  at 0.435 of that, so at the same number the ladder is darker than the biquad. That is
  the character of a four-pole and not an error.

**Tremolo** (`core/Tremolo.h`) is last in the chain, after the dry/wet, because an insert
tremolo modulates everything on the track. Shape morphs continuously from sine through
triangle to a hard chop; Duty decides how much of the cycle is the loud half; Edge is a
real slew on the result, because a square wave with instant edges is a click and no
hardware tremolo has ever switched that fast; Spread offsets the right channel, and at
180° it is auto-pan. It is also a modulation source, so the same rhythm that chops the
level can sweep the filter.

**Sync** appears on the tremolo and on both LFOs as one list whose first entry is `Free`,
rather than a toggle plus a division that can disagree with each other. A division takes
its phase from the host's song position, so the same bar sounds the same wherever you drop
the playhead, and keeps running at the host's tempo while the transport is stopped.

## What is verified, and where

`npm run test:core` — 79 assertions, no JUCE needed:

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
- all thirteen browser presets load and render, and every value in every browser patch
  maps to a plugin parameter
- 44.1 / 48 / 96 kHz, and block sizes from 16 to 1024

and, for the ladder and the tremolo, measured from rendered audio rather than read back
from the coefficients, because a nonlinear feedback loop has no coefficients to read:

- the analogue circuit agrees with the clean one in the passband, is 3 dB down at 0.435 of
  its mark (a four-pole's own character), and rolls off at 12 and 24 dB an octave
- resonance self-oscillates **at the frequency the knob points at**, and stays inside the
  rails while it does
- filter drive adds harmonics (under 3% at 1x, over 10% at 10x) without being a volume
  knob, and the vintage circuit puts three times the second harmonic of the clean one
- drift moves the two channels apart, and at zero they are sample-identical
- a free tremolo runs at the rate it says; a synced one counts the host's eighths, lands
  its loud half on the beat, and renders the same bar identically from two bars later
- depth, duty and edge each do the thing they are named after, 180° of spread is auto-pan,
  and zero depth is the bypassed signal to within 1e-7
- a synced LFO follows the host's quarters, keeps moving while the transport is stopped,
  and hands control back to the rate knob on Free

`host_smoke` — 29 assertions at the host level: parameters exposed, latency reported,
blocks run without NaN, every preset renders, state round-trips, a browser patch imports
and comes back out unchanged, a half-size editor still draws its bottom-right corner, and
**the editor renders to a PNG** at both sizes so the interface can be looked at without
opening a DAW.

## Honest gaps

- **Nothing here has been heard.** It builds clean and the numbers are right, but the
  container that produced it has no audio device and no plugin host. The first real test is
  yours: load it, and listen for zipper noise on fast modulation (parameters are smoothed
  per block, filter coefficients recomputed per block) and for aliasing on the crush.
- The oversampler is correct but not cheap — around 780 multiply-adds per sample per band
  per channel at 4x. If CPU matters, swap `Oversampler` for `juce::dsp::Oversampling`,
  which is polyphase; the interface is a drop-in.
- The tremolo and the LFOs sync to the host, but nothing else does: there is no
  tempo-locked feedback time, which is the obvious next one.
- The tremolo's shape is drawn on the panel from the same arithmetic the DSP uses, but the
  drawing does not know about modulation of depth or duty, so a heavily modulated tremolo
  is shown at its unmodulated shape.
- GUI resizing is uniform scaling only: the interface is laid out once at 1180 x 1190 and
  the whole canvas is scaled to the window, so nothing is ever cropped, but nothing
  reflows either. The aspect ratio is fixed and the window opens smaller than the design
  size on a screen that cannot fit it.
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
