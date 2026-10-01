# FRACTURE in a browser

One HTML file. Open `fracture.html` in a browser and it runs: no install, no server, nothing
leaves the machine. It is FRACTURE itself, not a sketch of it: the plugin's own DSP
(`plugin/core`), compiled to WebAssembly and carried inside the page, behind the plugin's
own panel, laid out panel for panel as the plugin lays it out.

```
npm run test:fx          # headless browser suite, 172 assertions
npm run test:fx:head     # same, in a visible browser
npm run wasm             # rebuild the engine into the page after changing plugin/core
open fx/fracture.html    # or just double-click it
```

## How it is built

```
plugin/core/*.h ─ fx/engine/fracture-wasm.cpp ─ clang --target=wasm32-wasi ─┐
                                                                            │ base64
fx/fracture.html:  ENGINE ─┬─ AudioWorklet 'fracture': the engine, 128 samples a block
                           └─ main thread: a second instance, for the parameter table
                              and what the panel draws (idle controls, the filter's
                              response, the rhythm's shape)
```

`fx/engine/fracture-wasm.cpp` exposes the core as plain C functions (set a parameter,
process a block, read the meters) and does no audio work of its own. `fx/engine/build.mjs`
compiles it, embeds it in the page with a hash of the sources it was built from, and
embeds three more things from the plugin's own sources: `plugin/GUIDE.md` (the guide the
plugin shows), the tooltips (`helpFor()` in the plugin's editor) and the version. So:

- **The sound cannot drift from the plugin.** There is one engine. A fix to the plugin is a
  fix here after `npm run wasm`, and `tests/fx.mjs` fails if the embedded engine is older
  than `plugin/core`, or the guide, tooltips or version differ from the plugin's.
- **Every plugin parameter is here,** with its id, range, default and choices taken from
  `ParamTable.h` at load: the harmonic table, tuned feedback (Time, Pitch, Sync, through
  the drive), the filter rhythm, the filter's mix, the macros and the XY pad, oversampling,
  per-band stereo. The one exception is the sidechain: a page has nothing to offer it, so
  the envelope always follows the input.
- **Patches move both ways.** The ids and value forms are the plugin's patch format: Copy
  patch here pastes into the plugin, and the plugin's Copy patch pastes here.

`npm run wasm` also renders every preset through the WebAssembly build and through the
same C++ built natively, sample by sample (`fx/engine/parity.mjs`). The first 2048 samples
agree to 2e-7 on all 49 presets, and 48 agree to 1e-5 throughout. The two use different
maths libraries, whose sin and exp can differ in the last bit; Radio's 10-bit crusher
turns that into a step or two at the moments a sample sits on a quantiser edge. The
plugin built on a Mac parts from the plugin built on Linux in exactly the same places.

Building needs clang with the wasm32 target, wasm-ld and a wasm32 C++ library (Ubuntu:
`clang lld wasi-libc libc++-18-dev-wasm32 libc++abi-18-dev-wasm32
libclang-rt-18-dev-wasm32`). Opening the page needs none of it.

## The panel

The plugin's header (Undo, Redo, A, B, A to B, Tips, Guide, the preset arrows and menu,
Save, Copy and Paste patch) over its eleven panels, in its three rows. Above it sits the
one thing that is the page's own, the **test signal** bar: Play, a file, the microphone,
generated sources, a Tempo box standing in for a DAW's transport, and Bypass.

- **Undo and A/B** are the plugin's `session::History` and `session::Workspace` rules: undo
  goes back one step from what is live, each side of A/B has its own history, and a switch
  is not a step.
- **Dimmed controls** are the engine's own `Relevance.h`: a dimmed control does nothing in
  the setup as it stands, and its tooltip says why ("Feedback is at 0%"). It still works,
  so a patch can be set up before it is switched in.
- **Save** writes the patch as a file, which is what a plugin preset file is; **Your presets
  ▸ Open a preset file…** in the menu loads one, from here or from the plugin.
- **The harmonic table** and **the guide** open over the panels, as in the plugin.
- **Tips** are the plugin's tooltips, word for word.

The drawings are the page's: the scope (spectrum with the input over it, the live filter
curve, the harmonic readout, the band's transfer curve), the filter's response, the
rhythm's steps, the pad, the tremolo's cycle and the meters. Where a drawing needs the
engine's arithmetic it asks the main-thread instance (the filter response, the rhythm's
shape, the shapers); the knob rings and the live filter follow the values the worklet
reports thirty times a second, modulation included.

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

## The Scope, and the test sounds

The spectrum draws the input as a grey line over the output's bars, so the gap between
them is what FRACTURE added, and the filter's response live over both. Play one note and
the line under it names the note and reads out the 2nd to 6th harmonics in the output, in
dB under the fundamental: Soft shows a 3rd and no 2nd, Tube a 2nd. It is the plugin's
reader (`plugin/core/Harmonics.h`) in JavaScript, with a longer frame (8192 points) so it
reaches down to about 41 Hz rather than the plugin's 90 Hz. Chords, drums and noise are
not read, rather than read wrongly.

The sources: pink noise, a 110 Hz sine, a **45 Hz sub sine** (the fundamental a kick or an
808 sits on, and where a split, a drive or a filter that thins the low end shows first),
a plucked loop, a loaded file, or the microphone. All are generated in the page. The
Tempo box is the transport: while a source plays, synced LFOs, the tremolo, the filter
rhythm and FB Sync follow it from bar 1.

Click the number under any knob to type a value, in the units shown: `2.2k`, `-6 dB`,
`50%`, `/4`, and on FB pitch a note (`A2`, `C3 +50c`); drive is typed in dB. Enter applies
it, Escape puts it back.

## What is load-bearing

- **`MODES` stays, and stays at fourteen.** The engine does not use it, but the plugin's
  tests check its shapers point by point against it (`npm run reference`), and the
  transfer-curve drawings and Start from use it. Table is not in it, because Table is
  drawn rather than computed.
- **The browser presets in `PRESETS` are the plugin's source** for its first 22 factory
  presets (`plugin/core/Presets.h`, generated). The plugin's other 27 are shown too, from
  `PLUGIN_PRESETS`, embedded from `FactoryPresets.h` with the menu's headings, so the menu
  is the plugin's, 49 presets in its order; they are kept apart so they never flow back
  into what the plugin is generated from.
- **An offline render sets its patch before `initWorklet()`.** The values a node is created
  with are in from the first sample; later ones go by message, which a live context
  delivers between blocks but an `OfflineAudioContext` may not deliver before it renders.
- **The worklet loads from a `data:` URL, then `blob:`.** Chrome refuses a `blob:` worklet on
  a `file://` page, and double-clicking the file is how this gets used; the hosted page's
  policy allows `blob:`. Neither is a network request. The hosted page's policy also has
  `'wasm-unsafe-eval'`, which permits compiling WebAssembly and nothing else.

## What the test checks

`tests/fx.mjs` loads the page in headless Chromium and renders the real engine in an
`OfflineAudioContext`. That the engine is the plugin's: the embedded build's source hash,
the guide, the tooltips and the version against the plugin's own files, and all 185
parameters. That it is finite, audible and bounded: all 14 modes at +32 dB through both
stages, feedback at 85%, every preset, two Table stages with every bar drawn. That what
came with the plugin's engine works, measured from the audio: FB Pitch rings at the note
within a cent (measured as the plugin's own test measures it, by the fundamental's phase,
because an autocorrelation measures the loop's group delay), FB Time at its time, the
filter rhythm repeats every 1/8 at the page's tempo and flips halfway, the matrix moves
the level from LFO 1, a macro and the pad, FB through drive changes the sound and stays
bounded, Filter mix at 0 is unfiltered, and oversampling cuts Wrap's aliasing. That the
earlier checks still hold through the new engine: per-band stereo (Mid leaves a side-only
signal clean, to under −60 dB), the harmonic table's curve and its harmonics through the
engine (a 2nd drawn at 50% comes out at −6.0 dB), the ladder, the tremolo and its spread,
the harmonic reader. And the panel: the eleven panels in the plugin's order, every plugin
control present, relevance dimming and its reasons, undo and redo, A/B with a history
each, the filter display's drag, the steps, the pad, a macro's routes, the guide, the
preset arrows, typed values and pasted patches. It takes screenshots to
`tests/screenshots/`, because layout regressions do not fail assertions.

## Testing it on its own

`npm run test:fx` runs this file's suite alone; `npm test` runs it with everything else.
