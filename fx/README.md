# FRACTURE — multi-band, multi-FX distortion

One HTML file. Open `fracture.html` in a browser and it runs: no build step, no
dependencies, no server, nothing leaves the machine. It is a distortion unit in the
spirit of Output's *Thermal* (a band-split drive box: split the signal, distort each
band its own way, blend it back) and Minimal Audio's *Rift* (serial shaping stages,
destructive digital modes, feedback, a resonant filter after the drive, and modulation
patched to anything).

```
npm run test:fx          # headless smoke test, 59 assertions
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
                 dry/wet mix ─ width (M/S) ─ tremolo ─ output gain ─ safety ─ out
```

Per band: two serial drive stages, each with its own mode and drive, a post-drive tilt
tone control, band dry/wet, level, mute and solo. Crossovers are 4th-order
Linkwitz-Riley (two cascaded Butterworth sections), so the bands sum without a notch at
the split frequency.

## The 14 distortion modes

| | | |
|---|---|---|
| **Soft** tanh | **Tube** tanh, top half capped at 0.6 | **Warm** exponential saturation |
| **Diode** asymmetric exp clip | **Hard** clip | **Tape** soft with 3rd-order dip |
| **Fold** triangle wavefolder | **Sine** sine fold | **Warp** phase-modulated fold |
| **Wrap** discontinuous wrap-around | **Gap** dead-zone/crossover grit | **Rectify** half-wave |
| **Quantize** step quantiser | **Harmonics** Chebyshev 1/3/5 | |

Every mode has unity slope at the origin and stays within ±1, so changing mode at a given
drive setting does not jump in level. **Harmonics** is the one deliberate exception —
Chebyshev mixing gives it a shallow origin slope, and that is the character.

**Tube** tops out at 0.6 on its positive half and at 1 on its negative half. The two halves
bending differently is what adds even harmonics (the 2nd, an octave up, above all), which
is the warmth. Soft is symmetric and adds none. The first Tube, `(e^0.8x − e^−1.2x) /
(e^0.8x + e^−1.2x)`, looked asymmetric but reduces to `tanh(x)` exactly, so until this was
found Tube and Soft were the same sound. `test_core` now requires Tube to carry the 2nd
harmonic and Soft not to.

## The look

Béton clair: Corbusier primaries on concrete, ported from the design canvas in `design/`.
Flat colour, zero radius, no shadows and no gradients except the dials' own arcs — the
concrete greys are the only texture, and the section bars are numbered 01–09.

Colour is functional, never decorative, and each panel sets a `--fill` its dials inherit:

| | |
|---|---|
| **yellow** `#e9b21f` | adds harmonics — drive, stage B, auto gain |
| **red** `#c0392f` | destroys or limits — crush, feedback, safety clip, envelope, mute |
| **blue** `#1e4b8f` | shapes — filter, tone, LFOs, modulation, solo |
| **ink** `#17150f` | structure — type, rules, section bars, unity controls |

So a knob's arc says what kind of thing it does, and a few knobs override their panel: tone
is blue inside the yellow drive panel, mix and level are ink. The one place colour marks
identity instead of function is the spectrum, whose three regions follow the live crossover
points. Modulation shows as a blue tick outside the dial marking where the knob is set,
while the arc follows the value modulation has pushed it to — it reads in a screenshot and
in grayscale, which a glow does not.

Type is a heavy grotesque from the system stack (`Arial Black` / Helvetica / Arial), not the
Archivo the mockups use: the page makes no requests, and that promise outranks the typeface.

## The filter, and the tremolo

The post filter has a **Circuit** control. `Clean` is the pair of biquads it has always
been. `Analogue` and `Vintage` are a four-pole ladder in an AudioWorklet: zero-delay
feedback, saturation *inside* the loop, and a cutoff that drifts a few per cent per
channel. Saturation in the loop is what makes resonance squelch against a loud signal
instead of ringing through it, and what keeps self-oscillation at a usable level rather
than a divergence; `Vintage` saturates asymmetrically, for even harmonics, and loses the
top octave. **Drive** pushes the input stage with most of the level taken back out again,
and **Slope** picks 12 or 24 dB — every response is mixed from the same four taps, so
changing type or slope never re-tunes the resonance.

The mark on **Cutoff** is the pole corner: where the resonance sings, which is the only
calibration that lets a resonant sweep be played in tune. A four-pole is 3 dB down at
0.435 of that, so at the same number the ladder is darker than the biquad — the character
of a four-pole, not an error.

**Tremolo** (panel 09) is last in the chain, after the dry/wet, because an insert tremolo
modulates everything. **Shape** morphs continuously from sine through triangle to a hard
chop, **Duty** decides how much of the cycle is the loud half, **Edge** is a real slew on
the result (a square wave with instant edges is a click, and no hardware tremolo switched
that fast), and **Spread** offsets the right channel — 180° is auto-pan. It is also a
modulation source, so the rhythm chopping the level can sweep the filter at the same time.
The panel draws one cycle of the curve as it will actually sound, from the same arithmetic
the worklet runs.

**Sync** on the tremolo and on both LFOs is one list whose first entry is `Free`, rather
than a toggle and a division that can disagree with each other. A page has no transport to
ask, so the tempo comes from the **Tempo** box next to the transport; in the plugin it
comes from the host, and there a division takes its phase from the song position.

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
- **The worklet loads from a `data:` URL.** It carries the bit crusher, the ladder and
  the tremolo, because none of the three is expressible as a graph of built-in nodes, and
  from a `data:` URL because Chrome refuses to load an `AudioWorklet` module from a
  `blob:` URL on a `file://` page, and double-clicking the
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
stages, with feedback at 85%, for every preset, and with the modulation matrix live. The
ladder and the tremolo are measured the same way, from the audio: that the ladder is
audibly not the biquad, that a self-oscillating one stays inside the rails, that full
depth chops the level and 180° of spread anticorrelates the two channels, and that a
synced division counts the tempo in the box. It
also checks every preset only references real parameters with in-range values, and takes a
screenshot to `tests/screenshots/fx-fracture.png`, because layout regressions do not fail
assertions.

## The plugin

`../plugin` is a native port of this file: same signal path, same shapers, same parameter
ids, same presets, same ladder and tremolo, built as a VST3 / AU with JUCE. The DSP there has no dependency on JUCE
and is checked against *this* file's arithmetic — `npm run test:core` compares all fourteen
shapers against 9,114 points generated from `fracture.html` itself, so the two cannot drift
apart. `plugin/README.md` lists what the port deliberately changed and why.

## Testing it on its own

`npm run test:fx` runs this file's suite alone; `npm test` runs it with everything else.
