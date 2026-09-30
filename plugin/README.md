# FRACTURE — VST3 / AU plugin

**New to it? Read [`GUIDE.md`](GUIDE.md)** — how FRACTURE works, control by control, in plain
words. It is also built into the plugin (the Guide button at the top), and ships on the
disk image as "How it works.md". This README is the engineering detail behind it.

A native port of `../fx/fracture.html`. Same signal path, same fourteen shapers, same
parameter ids, same browser presets — so a patch copied out of the browser version loads
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

To put both plugins on a download page, see `plugins-site/README.md`: one command builds a
static site around the disk images, ready for Vercel or Render.

```
./plugin/build-macos.sh --dmg        # build, install, and pack plugin/dist/FRACTURE-0.1.0-macOS.dmg
```

The disk image carries the Audio Unit, the VST3, the standalone app, an installer and a
read-me, laid out over a Béton clair background that the packaging script draws (no binary
in the repository: `scripts/lib/png.mjs` draws it). `Install FRACTURE.command` copies
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
  Relevance.h       which controls do nothing right now, and why: the panel dims them
  RhythmMod.h       the filter's rhythm: shapes, steps, groove, phase, glide
  FilterResponse.h  the post filter's response, as the panel draws it
  History.h         undo, redo and A/B, on whole-patch snapshots (shared with CRATE)
  ParamTable.h      ONE parameter table — the DSP, the host and patch import share it
  Presets.h         generated from the browser presets; do not edit
  FactoryPresets.h  the menu: the browser's presets, then the ones only the plugin can play
  FractureCore.h    the whole processor
Source/             the JUCE wrapper: parameters, state, latency, and the interface
  Session.h         when an undo step is taken, A/B in the saved state, your presets
                    (shared with CRATE)
tests/              the DSP tests, the host-level smoke test, and the reference data
tools/              regenerates the reference data from fx/fracture.html
```

The split is the point: **the DSP does not know JUCE exists**, so it builds and is tested
with a bare compiler, and the plugin layer stays thin enough to read in one sitting.

## What the port changed on purpose

The browser version's constraints are not a plugin's constraints, so six things are
deliberately different, and the tests pin each one:

- **Drive evaluates `f(x · drive)` per sample.** The browser had to bake a fixed curve over
  [-40, +40] and pre-scale into it, because `WaveShaperNode` clamps its input and swapping
  curves at control rate clicks. Natively that machinery is unnecessary, so the
  curve-interpolation error is gone too.
- **The shapers are anti-aliased.** The first listening report was crackle and high-end
  fizz, worst on the Rift preset. Measured, it was aliasing: Wrap jumps from +1 to -1 in
  one sample, and at 4x a 3.7 kHz tone came out with its fold-back 7.5 dB under the note.
  Every shaper with a closed-form antiderivative now uses first-order ADAA
  (`core/Shapers.h`): it outputs the average of the curve over the step between samples.
  In the audible band that takes Wrap from -7.5 to -38 dB on that tone, Rift's fold into
  wrap from -13 to -35 dB at 1.2 kHz, and Fold, Hard, Gap, Rectify and Harmonics down by 20
  to 35 dB, while a low note's first twenty harmonics move by 0.001 dB. Warp is already
  clean and Quantize's steps are its sound, so both are left alone. Each stage adds half a
  sample of delay at the oversampled rate, which the tuned feedback loop now allows for.
  The browser version is unchanged and aliases as it always did.
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

**Slope** now runs to 36 and 48 dB, and the filter has a **Filter rhythm** section, after
the parameter set of FilterFreak (not its look, and not its code). All of it is new, so
it is at its neutral setting in every existing patch: 12 and 24 dB keep their indices,
Filter mix defaults to 100% and Rhythm mod to off.

- **36 and 48 dB** add two or four plain one-poles after the ladder, at the same corner,
  so the extra slope does not re-tune the resonance or move the mark; on the clean circuit
  they are more biquads in the cascade. Peak stops at two sections, since cascading a
  9 dB boost four times makes a spike rather than a steeper shape.
- **Filter mix** blends the filter against what went into it, which is what makes a
  resonant sweep usable on a full mix.
- **Rhythm mod** is how far, in octaves, either way from Cutoff, which stays the resting
  point. **Rhythm** is the same division list the LFOs and the tremolo use (Free runs at
  **Rate**). **Shape** is sine, triangle, both saws, square, a random value per division,
  or **Steps**: eight bars drawn on the panel. **Groove** swings every second division
  late, so on Steps it is swung sixteenths; **Phase** offsets the right channel; **Glide**
  rounds the edges. The filter is retuned every 16 samples.
- It reads the song position, so the same bar moves the same way wherever the playhead
  starts, and Random is a hash of the position, so two bounces agree. It is read at the
  middle of each sample: at 120 BPM a sixteenth is exactly 6000 samples at 48 kHz, so
  step edges sat exactly on sample instants and rounding in the song position decided
  which side they fell, which a test caught at -47 dB of difference between two bars.
- Rhythm mod, Filter mix, Rate, Groove, Phase and Glide are matrix destinations, added at
  the end of the list so every saved matrix keeps its targets. The eight steps are not.
- None of this is in the browser version, so a patch copied out of the plugin carries ids
  the browser does not know about. Patches from the browser still load here unchanged.

**The feedback loop can be tuned**, after Rift. **FB mode** sets what its length is:
**Time** is the 1–250 ms it always was (and the default, so no patch changes); **Pitch**
makes the loop ring at a note, from C1 to C7, as a comb or a resonator; **Sync** makes each
repeat one note division at the host's tempo. **FB through drive** sends the repeats back
in before the split instead of after it, so every repeat is split and driven again — the
thing that turns a comb into a growl. **FB pitch** is a matrix destination, so a loop can
be swept in tune by an LFO or the envelope.

The pitch is exact because the loop is tuned as a whole, not just its delay line. The
tone filter, the DC blocker and the linear interpolation all have a phase at the target
note, and through the drive so do the oversampler (48 samples at 4x, most of a cycle at
1 kHz), the crossover and each band's DC blockers. The plugin computes that phase from
the same filters the audio runs through and shortens the delay by it. Measured, the
fundamental lands within 0.1 cent from A1 to A6, and within a cent through the drive at
4x with three bands. Without the correction it is up to 66 cents out on its own and more
than four semitones out through the drive; the test was run that way to check it fails.

Two things the tuning cannot do. The shapers and the loop's saturator are memoryless, so
they do not move the fundamental, but driven hard they add harmonics and so change what
the note sounds like. And a high note through the oversampler can need a loop shorter
than the oversampler's own latency: there, it rings an octave down rather than out of
tune. In Pitch mode the DC blocker drops from 40 Hz to 10 Hz, because at 40 Hz it bends
the phase of a low note's partials apart; in Time mode it is where it always was.

**Sync** appears on the tremolo and on both LFOs as one list whose first entry is `Free`,
rather than a toggle plus a division that can disagree with each other. A division takes
its phase from the host's song position, so the same bar sounds the same wherever you drop
the playhead, and keeps running at the host's tempo while the transport is stopped.

## Seeing the filter

The Scope draws the post filter's frequency response over the live spectrum, on the same
log axis, with a tag naming the type and where it sits ("Notch · 1.40 kHz"). It follows the
cutoff as it is right now, so a rhythm or an LFO sweep is visible as it happens, and it
includes the filter's own mix, so a notch at half mix is drawn half as deep.

The curve is computed from the filter's own coefficients (`core/FilterResponse.h`), not
sketched: the clean cascade exactly, and the ladder as its small-signal response, including
the slightly-under-unity slope of the Vintage circuit's offset input stage, which was the
only thing a first version missed. The test suite renders tones through the real engine at
every type, circuit and slope, resonant, and requires the measured level to match the drawn
one: it does to under 0.001 dB at 269 points, and fails by 7.5 dB if the ladder's feedback
is left out of the model. What the curve does not show is drive's saturation and the
channel drift, which depend on the signal rather than the knobs.

The Filter panel has the same curve in its own display, laid out in three columns (what the
filter is, the curve it makes, the knobs that move it) the way the hardware-shaped
rhythmic filters are. The display is also a handle: dragging across moves the cutoff and up
and down the resonance, relative to where they were, as one host gesture each; Shift makes
it fine and a double-click resets both. Both drawings call one function, so the panel and
the Scope cannot disagree about where the filter is. The canvas grew to 1760 x 990 for it;
a 1440 x 900 screen shows it at 77%.

## Playing it: the XY pad and the macros

With about 150 controls, the drive section needs a way to be played rather than set. The
**Perform** panel has an XY pad and two macro knobs, and all four are sources in the
modulation matrix (Macro 1, Macro 2, XY X, XY Y), appended after the existing sources so a
saved slot keeps its meaning. Route one to as many targets as the six slots allow and a
single gesture moves them together, which is how Thermal and Rift make depth playable.
Beside the knobs the panel says, in words, what each control is routed to.

They are unipolar and start at 0, and at 0 a routed control adds nothing, bit for bit, so
routing one never changes a sound until it moves. An unrouted control is dimmed, and the
pad dims only when neither axis is routed. The pad writes both axes as one host gesture,
so a drag automates as a single move.

## The panel

It is laid out wide, in three rows that follow the signal: what goes in, the split and the
drive; the crush and feedback loop, the filter and its rhythm; then modulation, the scope,
and the tremolo and output that end the chain. It used to be one tall column at
1320 x 1376, which on a 1440 x 900 laptop opened at 57% and set its 9-point captions at
about 5 points. At 1760 x 990 the same screen opens it at 77%.

**Controls that do nothing right now are dimmed**, and their tooltip says why: FB pitch
while the loop is timed in milliseconds, the filter's Drive and Drift on the clean
circuit, an LFO that no matrix slot uses, the whole rhythm while its Mod is off. They still
work, so a value can be set up before it matters. The rules live in `core/Relevance.h`, and
they take modulation into account: a section switched off by a base value of zero comes
back to life if the matrix is moving that value. Every control has a one-line tooltip. **Tips**, at the
top, turns tooltips off and on; the choice is saved as a preference on the machine rather
than in the session, because it is not part of the sound. **Guide** opens `GUIDE.md` over
the panel. It is compiled in from the same file, so the two cannot say different things.

**Undo, Redo, A/B and Save** sit in the header. An undo step is taken when a gesture ends,
not on every value, so a whole knob drag is one step, and so is a drag on the pad or a
stroke across the drawn steps: the step waits until no control is mid-gesture. A preset or
a paste is one step too. Host automation sends no gestures, so playback never fills the
history, though undo still takes back an automated change that is live when you press it.
A and B each keep their own history, so undo cannot cross a switch and leave the panel
showing A's settings with B lit. The hidden side is saved in the session by parameter id,
and taken out of the tree again on load, so a session from before A/B existed opens on A
and an older build ignores it. The stacks are `core/History.h`, which has no JUCE in it and
is tested with the DSP; `Source/Session.h` decides when to use them. Both files are the
same in CRATE.

Your presets are saved as browser patches, one JSON file each, in Documents/FRACTURE/Presets:
the same text Copy patch produces, so a saved preset also pastes into `fx/fracture.html`.
The menu rereads the folder every time it opens.

## What is verified, and where

`npm run test:core` — 134 assertions, no JUCE needed (`VERBOSE=1` prints what each one
measured):

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
- **every factory preset is levelled**: over the audition loop each one sits within 6 dB of
  the dry signal and off the ceiling, no two sound the same, and each of the
  plugin-only presets uses something the browser does not have. The level check fails on
  the presets as they were before, which were 25 dB down, 12 dB down and 10 dB over
- every browser preset loads and renders, and every value in every browser patch
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
- 12, 24, 36 and 48 dB an octave on both circuits (measured 11.9 to 49.0); a 48 dB band
  pass peaks where and as loud as the 24 dB one; a 36 dB notch is 45 dB deep on its
  corner; filter mix at 0 takes the filter out exactly; the rhythm opens the filter by
  12 dB on the beat on both circuits and renders the same bar identically from two bars
  later; and the new matrix destinations are at the end of the list
- **every control the panel dims really does nothing**: in twelve states of the box, each
  of the 694 controls the panel would dim is moved end to end and the output must be
  bit-identical (a deliberately false rule fails it at once)
- a tuned loop's fundamental is within a cent of the note from A1 to A6, and through the
  drive at 4x with three bands, measured by phase advance rather than by autocorrelation
  (which reports the spacing of the repeats, not the pitch of the note); a synced loop
  repeats on the division to the sample; through the drive the third repeat carries
  40 dB more third harmonic than without; and the worst case (wrap at full drive, 85%,
  through the drive) stays inside the rails
- undo and redo walk the stack in order, a fresh edit drops the redo, the stack is capped,
  and a value that did not round-trip exactly is not taken for an edit; A and B keep
  separate histories, B opens as a copy of A, and a switch is never an undo step

`host_smoke` — 101 assertions at the host level: parameters exposed, latency reported,
blocks run without NaN, every preset renders, state round-trips, a browser patch imports
and comes back out unchanged; a knob drag is one undo step (a version that committed on
every value fails five checks), and so are a pad drag, a stroke across the steps, a preset
and a paste; a session saved on B reopens on B with A held; your presets save, list in
natural order and load back; the header's buttons and Ctrl + Z work; a half-size editor still draws its bottom-right corner, and
**the editor renders to a PNG** at both sizes so the interface can be looked at without
opening a DAW.

## Honest gaps

- **Nothing here has been heard.** It builds clean and the numbers are right, but the
  container that produced it has no audio device and no plugin host. The first real test is
  yours: load it, and listen for aliasing on the crush and for anything that clicks.
- **Zipper noise is measured, not assumed.** A sine is run through each control while it is
  swept the way a host sends automation, once per 512-sample block, and the energy between
  the harmonics is compared with the same sweep sent in 16-sample blocks. The band Tone and
  the crossovers measured +7.8 and +6.2 dB of zipper, because their filters were retuned
  once a block; they now glide across the block, retuned every 16 samples, and measure
  +0.0. Every control in the test is under 1 dB, and the suite fails above 2. The one
  preset that moves them (Motion, whose envelope drives band 2's tone) renders differently
  for it; every other preset is byte-identical.
- The oversampler is correct but not cheap — around 780 multiply-adds per sample per band
  per channel at 4x. If CPU matters, swap `Oversampler` for `juce::dsp::Oversampling`,
  which is polyphase; the interface is a drop-in.
- Rift takes the feedback pitch from incoming MIDI. This does not: an effect that accepts
  MIDI is a different kind of Audio Unit (`aumf` rather than `aufx`), and changing it would
  re-register the plugin and break every Logic session that already uses it. Set FB pitch
  by hand, or automate it.
- Sync mode's loop is capped at 2.1 seconds, which is a 1/4 at 30 BPM or a bar at 115;
  longer divisions are clamped to it.
- The tremolo's shape is drawn on the panel from the same arithmetic the DSP uses, but the
  drawing does not know about modulation of depth or duty, so a heavily modulated tremolo
  is shown at its unmodulated shape.
- GUI resizing is uniform scaling only: the interface is laid out once at 1760 x 990 and
  the whole canvas is scaled to the window, so nothing is ever cropped, but nothing
  reflows either. The aspect ratio is fixed and the window opens smaller than the design
  size on a screen that cannot fit it.
- `pluginval` passes at strictness 10 (its maximum, fuzzing included) on the Linux VST3
  build, editor tests included under a virtual display, across six random seeds. The
  first run failed: a switch restored from a saved state kept the fractional value a host
  had left on it (0.21 instead of 0) because JUCE's `replaceState` skips a parameter whose
  snapped value looks unchanged, so `setStateInformation` now writes every parameter back.
  The AU still needs `auval` on a Mac (the build script runs it), and Steinberg's own VST3
  validator has not been run.
- `npm run audition` renders every preset over a loop (or your own WAV) with the real DSP,
  so it can be heard without a DAW.
- The JUCE splash screen is left on, since disabling it needs a JUCE licence. `AGPL`/GPL
  or a paid licence also decides whether you can distribute a build; the VST3 SDK is
  separately dual-licensed by Steinberg.

## The presets

Forty-four, in two groups.

**Twenty-two browser presets**, from `fx/fracture.html`, which both versions play. The first
thirteen show off the drive section; the other nine are chosen by use rather than by
feature, for what goes on a drum bus (**Drum bus — glue and crunch**, **Kick & snare**), a
bass (**Bass — harmonics driven, sub clean**, **808**), a vocal (**Vocal — warm
presence**), a guitar-like part (**Amp — crunchy rhythm**, **Fuzz**), a mix (**Mix bus — a
touch of tape**) or an effect (**Radio — the AM band**).

**Twenty-two plugin-only presets**, in `core/FactoryPresets.h`, which need what the browser does
not have:
- tuned feedback: **Tuned comb**, **Resonator**, **Growl**, **Kick tuned to the key** (a C1
  resonator under the low band), **Snare ring** (G4), **Comb on the fifth**
- synced feedback: **Dub echo**, **Tape slap**, **Quarter-note echo into the fold**,
  **Stutter** (1/32 echoes back through the drive)
- the filter rhythm: **Gated sixteenths**, **Wah on the quarter note**, **Swung notch**,
  **Random steps**, **Trance gate** (sixteenth steps at 48 dB), **Acid line** (a resonant
  ladder on drawn steps), **Notch drift** (slow, the two sides opposite)
- the 48 dB slope and the filter's mix: **Cliff**, **Parallel crunch**
- the Perform panel: **XY — drive across, cutoff up**, **XY — the loop's pitch across,
  feedback up**, **Macros — 1 folds and crushes, 2 widens and repeats**. These are
  levelled at every corner of the pad and both ends of each macro, not only where they
  are saved, because that is where they will be dragged

They use the same patch JSON and the same import as everything else. A patch copied from a
plugin-only preset into the browser loses the parts the browser does not have. Adding a
browser preset moves the plugin-only ones down the menu, which is harmless: a host saves the
plugin's whole state with a session, not the number of the preset it started from.

Every preset was levelled on the audition loop (`npm run audition`) to within a few dB of
the dry signal, so switching presets compares sounds, not volumes. Three browser presets
were far out (Speaker in a bin 25 dB down, Harmonic pan 12 dB down, Rift-ish 10 dB over and
into the limiter); they were fixed in `fx/fracture.html` itself and regenerated, so the
browser and the plugin still agree, and the browser suite still passes.

## Regenerating the reference data

```
npm run reference        # after changing the shapers or the presets in fx/fracture.html
```

That rewrites `tests/shaper_reference.csv`, `tests/presets.json` and `core/Presets.h`.
Never edit those three by hand — `fx/fracture.html` is the source of truth for the maths.
