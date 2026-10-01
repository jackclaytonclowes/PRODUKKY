# FRACTURE — multi-band, multi-FX distortion

One HTML file. Open `fracture.html` in a browser and it runs: no build step, no
dependencies, no server, nothing leaves the machine. It is a distortion unit in the
spirit of Output's *Thermal* (a band-split drive box: split the signal, distort each
band its own way, blend it back) and Minimal Audio's *Rift* (serial shaping stages,
destructive digital modes, feedback, a resonant filter after the drive, and modulation
patched to anything).

```
npm run test:fx          # headless smoke test, 121 assertions
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
tone control, band dry/wet, level, mute and solo, and **Stereo**: L/R drives the whole
image, Mid drives only what the two channels share and passes the sides through clean,
Side the reverse. It is done with gain matrices around the band (encode, a clean path for
the half that is not driven, decode), delayed to match the shaper so the halves rejoin
exactly, and it is the plugin's per-band M/S. Crossovers are 4th-order
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

- **Drive is a gain into a fixed shaper, never a rebuilt curve.** The shaper clamps its
  input to [-1,1] and evaluates each mode over [-40, +40], and the pre-gain is set to
  `drive/40`. Feeding a signal at that gain evaluates `f(s*drive)` exactly, which makes
  drive an ordinary `AudioParam`: automatable and modulatable, without ever swapping a
  curve. Swapping curves at control rate clicks.
- **The shaper is the plugin's, in a worklet.** 4x oversampling with the plugin's
  half-band filters, and first-order antiderivative anti-aliasing: each mode in `MODES`
  carries `F`, its antiderivative, and the output is the average of `f` over each step,
  `(F(x) - F(x1)) / (x - x1)`. `WaveShaperNode`'s own `'4x'` could do neither, and Wrap on
  a 3.7 kHz tone left its aliasing louder than the note; now it is 15 dB or more under
  the old shaper and well under the note. Fed the same input, the worklet matches the
  plugin's `Oversampler` and `AntialiasedShaper` to float32 rounding. The filters are in
  polyphase form (the same sums, a quarter of the arithmetic): about 3% of a core per
  stereo stage, and a bypassed stage B is skipped. If the worklet cannot load, a
  `WaveShaperNode` with the same curve stands in, as before.
- **`F` must match the plugin's `antiderivative()`** in `plugin/core/Shapers.h`, mode for
  mode. The suite checks each `F` against the area under `f` over 200 random stretches.
- **Every wet path is 96 samples late, and the dry paths wait for it.** Each shaper worklet
  is 48 samples (the filters' latency), and stage B is a second one, so the path around
  stage B is delayed 48 samples and every dry path 96: a band is equally late with stage B
  on or off, the bands sum in step, and a half mix does not comb-filter. The suite renders
  dry and wet apart and requires them to line up to within a sample (the pre-filters' own
  delay); without the delays they are 48 or 96 samples apart.
- **The safety clip does not oversample.** Every per-band shaper runs at 4x, but an
  oversampled shaper overshoots its own ceiling by ~20% (measured, because the resampling
  filters ring), which defeats the point of a last-stage limiter. The safety stage is a
  plain tanh with a -0.9 dBFS ceiling, so the output is genuinely bounded. The test
  asserts it.
- **The worklet loads from a `data:` URL.** It carries the bit crusher, the ladder, the drive shapers and
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

## The harmonic table

**Table** is a drive mode you draw, the plugin's own (`plugin/core/HarmonicTable.h`)
ported: four frames of sixteen bars, one per harmonic, and a **Position** that morphs
from frame 1 at 0% to frame 4 at 100%. Choose it as Mode A or B in a band and the panel
opens under Drive (or press **Harmonic table**). The curve is a sum of Chebyshev
polynomials, so a sine that fills it comes out with exactly the harmonics drawn; it is
normalised by the sum of the bars, shifted so silence stays silent, and anti-aliased from
its antiderivative like the other modes. **Start from…** fills a frame with another mode's
harmonics, or all four at rising drive; **Wobble with LFO 1** routes an LFO to Position.

It is not in `MODES`. That list is the fourteen curves the plugin is checked against
point by point, and Table is drawn rather than computed, so it sits beside them as mode
14, which is its number in the plugin too. The worklet gets the frames by message and
Position as an AudioParam, and rebuilds the curve every sixteen samples while Position
moves. The ids (`tblPos`, `tb1h1` … `tb4h16`) are the plugin's, so a table copied out of
one pastes into the other. The plugin's three Table presets and its two Stereo presets
are shown here too, from a list of their own (`PLUGIN_PRESETS`) so they do not flow back
into the presets the plugin is generated from; the test requires each to equal the
plugin's copy.

## The Scope, and the test sounds

The spectrum draws the input as a grey line over the output's bars, so the gap between
them is what FRACTURE added. Play one note and the line under it names the note and reads
out the 2nd to 6th harmonics in the output, in dB under the fundamental: Soft shows a 3rd
and no 2nd, Tube a 2nd. It is the plugin's reader (`plugin/core/Harmonics.h`) ported, with
a longer frame (8192 points) so it reaches down to about 41 Hz rather than the plugin's
90 Hz. Chords, drums and noise are not read, rather than read wrongly.

The sources: pink noise, a 110 Hz sine, a **45 Hz sub sine** (the fundamental a kick or an
808 sits on, and where a split, a drive or a filter that thins the low end shows first),
a plucked loop, a loaded file, or the microphone. All are generated in the page.

Click the number under any knob to type a value, in the units shown: `2.2k`, `-6 dB`,
`50%`, `/4`; drive is typed in dB. Enter applies it, Escape puts it back.

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
synced division counts the tempo in the box. Per-band Stereo is checked by rendering
known mid and side signals: Mid on a mono signal is L/R, Mid leaves a side-only signal
clean, Side leaves a mono one clean, all to under −60 dB. The harmonic reader is checked
on synthetic spectra (a 2nd at −20 dB reads as −20), against a chord and a note below its
range, and through the engine's own analysers; typed values, the preset headings (against
the plugin's table) and the sub sine are checked too. The harmonic table is checked on the
curve (a full sine comes out as the bars, silence stays silent, F is its antiderivative,
nothing exceeds full scale), through the engine (a 2nd drawn at 50% comes out at −6.0 dB,
Position at 50% blends two frames bar by bar, two stages at +32 dB stay bounded), and in
the panel (drawing, double-click, Start from, Wobble). It
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
