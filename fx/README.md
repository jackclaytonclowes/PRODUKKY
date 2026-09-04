# FRACTURE — multi-band, multi-FX distortion

One HTML file. Open `fracture.html` in a browser and it runs: no build step, no
dependencies, no server, nothing leaves the machine. It is a distortion unit in the
spirit of Output's *Thermal* (a band-split drive box: split the signal, distort each
band its own way, blend it back) and Minimal Audio's *Rift* (serial shaping stages,
destructive digital modes, feedback, a resonant filter after the drive, and modulation
patched to anything).

```
npm run test:fx          # headless smoke test, 48 assertions
npm run test:fx:head     # same, in a visible browser
open fx/fracture.html    # or just double-click it
```

## Signal flow

```
in ─ input gain ─┬─ dry ──────────────────────────────────────────────────────┐
                 └─ pre HP ─ pre LP ─ crossover split (1, 2 or 3 bands)       │
                       band n:  in ─┬─ dry ───────────────────────┐           │
                                    └─ stage A ─ tone ─ stage B ─ wet ─ level ─┤
                       bands summed ─┬─ crush/decimate ─ filter ─ wet ─────────┤
                                     └─ feedback: delay ─ tone ─ saturator ────┘
                 dry/wet mix ─ width (M/S) ─ output gain ─ safety clip ─ out
```

Per band: two serial drive stages, each with its own mode and drive, a post-drive tilt
tone control, band dry/wet, level, mute and solo. Crossovers are 4th-order
Linkwitz-Riley (two cascaded Butterworth sections), so the bands sum without a notch at
the split frequency.

## The 14 distortion modes

| | | |
|---|---|---|
| **Soft** tanh | **Tube** asymmetric tanh | **Warm** exponential saturation |
| **Diode** asymmetric exp clip | **Hard** clip | **Tape** soft with 3rd-order dip |
| **Fold** triangle wavefolder | **Sine** sine fold | **Warp** phase-modulated fold |
| **Wrap** discontinuous wrap-around | **Gap** dead-zone/crossover grit | **Rectify** half-wave |
| **Quantize** step quantiser | **Harmonics** Chebyshev 1/3/5 | |

Every mode has unity slope at the origin and saturates towards ±1, so changing mode at a
given drive setting does not jump in level. **Harmonics** is the one deliberate exception
— Chebyshev mixing gives it a shallow origin slope, and that is the character.

## Modulation

Two LFOs (sine, triangle, saw up/down, square, random sample-and-hold, random smooth) and
an envelope follower with attack/release/sensitivity, patched through a six-slot matrix to
any of the ~40 continuous parameters — drives, band mixes and levels, tone, crossovers,
filter cutoff and resonance, crush amount, feedback, width, output. Modulation runs at
control rate (one frame, smoothed into the AudioParams), which is why the knobs visibly
move under modulation.

## Things that are load-bearing

- **Drive is a gain into a fixed curve, never a rebuilt curve.** `WaveShaperNode` clamps
  its input to [-1,1], so each mode's curve is built once over the domain [-40, +40] and
  the pre-gain is set to `drive/40`. Feeding a signal at that gain evaluates `f(s*drive)`
  exactly, which makes drive an ordinary `AudioParam`: automatable and modulatable,
  without ever swapping a curve. Swapping curves at control rate clicks.
- **The safety clip does not oversample.** Every per-band shaper runs at 4x, but an
  oversampled shaper overshoots its own ceiling by ~20% (measured, because the resampling
  filters ring), which defeats the point of a last-stage limiter. The safety stage is a
  plain tanh with a -0.9 dBFS ceiling, so the output is genuinely bounded. The test
  asserts it.
- **The bit-crusher worklet loads from a `data:` URL.** Chrome refuses to load an
  `AudioWorklet` module from a `blob:` URL on a `file://` page, and double-clicking the
  file is exactly how this gets used. A `data:` URL is not a network fetch, so the page
  still makes no requests; `blob:` remains as a fallback.
- **The feedback loop has a saturator in it.** Delay → damping filter → tanh → amount.
  Without the saturator, 85% feedback into a hard-clipping stage runs away; with it, the
  loop is bounded. The test renders 0.6 s at 85% and asserts the peak.
- **Auto gain is `drive^-0.55` per stage.** A rough loudness match so that turning drive
  up is a change of character rather than just a change of level. It is a knob, not a
  law — turn it off to hear what the drive is really doing.

## What the test actually checks

The failure modes that matter in a distortion box do not show up by reading the code: a
shaper or feedback path that produces `NaN`/`Inf` (which silently kills the whole audio
graph for the rest of the session), and a chain that runs away in level. So `tests/fx.mjs`
builds the real graph in an `OfflineAudioContext`, renders noise through it, and asserts
the output is finite, audible and bounded — for all 14 modes at +32 dB through both
stages, with feedback at 85%, for every preset, and with the modulation matrix live. It
also checks every preset only references real parameters with in-range values, and takes a
screenshot to `tests/screenshots/fx-fracture.png`, because layout regressions do not fail
assertions.

## Not related to the bundle builder

This folder is a separate tool that happens to live in the same repository. It shares no
code with the record-bundling app in `src/`, is not part of `npm run build`, and is not
included in `npm test` — run `npm run test:fx` for it.
