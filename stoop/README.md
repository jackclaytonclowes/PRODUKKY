# STOOP — sampler effects and vinyl

**New to it? Read [`GUIDE.md`](GUIDE.md)** — how STOOP works, control by control, in plain
words. It is also built into the plugin (the Guide button at the top). This README is the
engineering detail behind it.

An insert effect after the effects of the original pocket sampler of the mid-2000s, the
one a generation of loops was resampled through: its pitch shifter, its filter and drive,
a lo-fi crusher, and above all its vinyl simulator, with the knob its successor took away.

```
./build-macos-all.sh              # from the repo root: builds and installs all three plugins
./stoop/build-macos.sh            # or just this one
npm run test:stoop                # the measurement suite: compiler only, no JUCE
cmake -S stoop -B build-stoop && cmake --build build-stoop --target host_smoke -j
xvfb-run -a build-stoop/host_smoke_artefacts/Release/host_smoke shot.png
```

## About the name

This is an original product in the spirit of a well-known sampler, not a copy of one.
"SP-404" and "Roland" are somebody else's trademarks, the panel here is our own, and no
part of that machine's code, firmware or artwork is in this repository. What it does to a
signal was worked out from its manuals and from what its owners say, and rebuilt from first
principles. Nobody involved has had one to measure.

## What is known, and what is guessed

From the manuals:

- The original's **Vinyl Sim** had three knobs: COMPRESSOR, NOISE LEVEL and WOW FLUTTER
  (shown as CMP, noS and FLu).
- The SX that replaced it kept Noise and Wow Flutter and put **Frequency**, the playback
  system's tone, where the compressor had been; compression became an effect of its own.
  The current model has both: its "303 VinylSim" has Comp, its "404 VinylSim" has Frequency.
- The original's **Pitch** effect shifted over two octaves either way, with Feedback and
  Balance. **Filter + Drive** and **Lo-Fi** were among its effects too.

From owners, on forums (not checked): the original's vinyl sim sounds softer and less tinny
than the SX's, and its flutter is stronger. Owners also say the SP-303's vinyl compression
sounds different again, grittier.

Not known: how Roland built any of it. Every curve, time constant and rate below is our own
choice, made to be measured, and is not a measurement of the hardware.

## What is in the box

```
in ─ gain ─ mono ─┬─ dry ─ delay (matches the wet path) ───────────────────┐
                  └─ pitch ─ filter + drive (4x) ─ lo-fi ─ vinyl sim ─ wet ─┤
                                                                     mix ─ out ─ clip
vinyl sim:  comp (the cutting) ─ + noise (the surface) ─ wow flutter (the turntable)
```

The original ran one effect at a time, and the way to stack them was to resample: play the
pad through one effect, record it, play that through the next. STOOP runs all four at once,
in the order that workflow usually went: pitch the sample, colour it, crush it, and put it
on a record last, so the vinyl's compression and wow sit over everything.

Each section has its own switch, as each effect had its button. A section that is off keeps
running silently, so switching it on mid-bar picks up the audio where it already is, and the
switch crossfades over 10 ms. Off, it is multiplied by exactly zero, so its controls cannot
change a bit of the output.

**The latency is fixed**: the turntable's resting delay (8 ms) plus the filter's
oversampling (48 samples), 432 samples at 48 kHz, whatever is switched on, so the host's
compensation never has to change. The pitch shifter's own delay is part of its sound and is
not reported, as on any shifter of its kind.

### The vinyl sim

**Comp is the record's cutting.** Vinyl cannot hold a master's dynamics, so records were cut
compressed and limited, and that is part of how a record sounds. A peak-sensing compressor,
the two channels linked:

- One knob moves the threshold from -6 to -36 dBFS and the ratio from 1:1 to 10:1 together,
  with a 10 dB soft knee. On a -6 dBFS peak that takes off 5.2 dB at 25%, 12.3 at 50%, 19.6
  at 75% and 27.0 at 100%.
- Attack 3 ms and release 120 ms, applied to the gain reduction in decibels (the "smooth,
  decoupled" detector of Giannoulis, Massberg and Reiss, 2012). The level it reacts to is
  the louder channel's peak, held 10 ms before it falls: without the hold, every zero
  crossing looked like silence, the detector began to release twice a cycle, and the attack
  measured 9 ms instead of 3. Ten milliseconds is half a cycle of 50 Hz, so a bass note does
  not ripple the gain either.
- Make-up is automatic: whatever the curve takes off a -12 dBFS peak, it gives back. On the
  audition loop that holds the loudness within 1.1 dB from Comp 0 to 100, while the spread
  of its 10 ms levels narrows from 5.0 to 3.3 dB.
- Its overshoots, the first milliseconds of a hit before the gain has closed, are rounded
  off by a soft limit (transparent below 0.7, never past full scale) rather than clipped.
  That rounding is a guess at the "softer" owners describe.

**Noise is the record's surface**, so it goes on after the cutting: the crackle is never
pumped by the music. Hiss (700 Hz to 7 kHz) rises as the square of the knob; crackle comes
n² × 45 times a second, each click's size a cubed uniform draw, so mostly small ticks with
now and then a loud one, sitting more on one channel than the other; rumble below 20 Hz
rises with n; and a scratch ticks once a revolution at n³. The same random numbers are drawn
every sample whatever the settings, so turning Noise up makes the same crackles louder and
more of them. Everything is seeded from the song position, so a bar crackles the same way
every time it is played from the same place. At 25% the noise sits at -62.7 dBFS RMS with
peaks at -32; at 50%, -52.9 RMS, peaks -21; at 100%, -40.7 RMS, peaks -6.

**Wow flutter is the turntable**: a delay line read at a moving position, the speed of the
audio being one minus the rate the delay changes. Three things move it, all scaled by the
knob:

- **wow**, a sine once a revolution, as an off-centre record does: 1% either way at full
  (17 cents), at 0.56 Hz for 33, 0.75 for 45 and 1.3 for 78;
- **drift**, the motor wandering: a new random speed every 0.6 s through two one-poles at
  0.35 Hz, about 0.2% at full;
- **flutter**, sines at 6.3 and 10.1 Hz, 0.15% each at full, their sizes wandering between
  40% and 100%.

The line rests at 8 ms. With Wow flutter at 0 the read lands exactly on a sample and the
audio comes through bit for bit. The platter glides to a new **Speed** through two
quarter-second one-poles: a single one-pole's rate of change jumps the instant the target
moves, and since the wow's swing depends on the speed, the first version jumped the pitch 20
cents on a switch.

### The pitch shifter

Two read heads run through a delay line at the new speed, half a 40 ms window apart, each
faded out by a sin² window before it jumps back (pitch up) or forward (pitch down) a window,
and the two fades always sum to one. That is the real-time shifter of that era, and it sounds
like one: a tone whose period does not divide the window is spliced out of phase, so some of
its energy spreads into sidebands at the splice rate, 1 / (|ratio - 1| × 40 ms), 12 Hz for a
fifth up. That warble, and a slight doubling, are the character. **Feedback** writes the
shifted output back into the delay line, so it is shifted again on every pass, 0.9 of the
level at most, so the loop always dies away. **Balance** mixes the shifted signal with the
input. Pitch defaults to an octave down, because at 0 a shifter of this kind does nothing a
shifter is for.

### Filter + drive

A two-pole state-variable filter in the bilinear (TPT) form, low, band or high pass, Q from
0.71 to 20, then tanh(g·x) scaled by 1 / sqrt(g·tanh g), with g from 0.01 to 20. At Drive 0
the curve is straight to within -100 dB; at full, a -40 dBFS signal comes up 13 dB while a
-12 dBFS one barely moves, so half the drive is heard as level and half as saturation. The
whole section runs at four times the host rate, which takes the drive's audible aliases from
-8.8 dB under its harmonics to -53.4 dB. Cutoff and Resonance glide across each host block,
retuned every 16 samples.

### Lo-fi

A sample-and-hold clock with no filter before or after it, so what is above half the Rate
folds back as grit and the staircase's images stay in, then rounding to 2^(bits - 1) steps
either side of zero. The clock is shared by both channels. CRATE is the place for a careful
model of a particular converter; this is the lo-fi effect.

## The panel

Panels run in signal order. The vinyl sim has two displays of its own: the compressor's curve
at the current Comp, make-up included, drawn from the same function the DSP uses, with a dot
where the detector is now and a bar for the gain it is taking off; and the record, turning at
the Speed set, sitting off centre by as much as the wow swings, with the scratch in red and
how far off speed it is playing now, in cents.

**Controls that do nothing right now are dimmed**, and the tooltip says why: a section's
knobs while it is switched off, Pitch and Feedback at Balance 0, Speed with no wow and no
noise, everything after the split at Mix 0. The rules are in `core/Relevance.h`, and the test
suite holds them to it. **Undo, Redo, A/B, Save, Tips and Guide** work as they do in the
other two plugins (the code is shared: `core/History.h`, `Source/Session.h`, `Source/Guide.h`).
Your presets are JSON files in Documents/STOOP/Presets, choices by name (`"speed": "45 rpm"`).

## What is verified, and how

`npm run test:stoop` — 87 assertions, no JUCE needed (`VERBOSE=1` prints the measured value
behind every one). Measurements, not smoke tests, because nobody involved has heard it:

- **the path**: with its knobs at zero the box is a pure delay of exactly the latency it
  reports; the vinyl sim at zero is bit for bit the vinyl sim switched off; a half mix of dry
  and wet is the input, so the two are in step; the latency is the same on every preset
- **Comp**: 10:1 above the threshold at 100 (a 0.100 slope), 1:1 below with the quiet parts
  lifted by the make-up (21.6 dB); 5.5:1 at 50; attack 3.00 ms; release 120 ms after the
  10 ms hold (130.8 ms from the drop); the channels share one gain to within 0.01 dB; the
  loop's loudness held within 1.1 dB at every setting
- **Wow flutter**: at full, 17.2, 17.5 and 17.3 cents once a revolution at 33, 45 and 78;
  flutter at 6.3 and 10.1 Hz, about 1.8 cents each; 23.5 cents at the worst moment; 0.15
  cents of average detune; linear in the knob; and switching Speed mid-tone moves the pitch
  no more than the interpolator's own jitter (1.1 cents between cycles; a one-pole glide
  jumped 20)
- **Noise**: silent at 0; the levels above; bit-identical from the same song position and
  different from another; **not compressed** (tone plus noise, through Comp 100, is the
  tone plus the noise as it is on silence, to 2e-7); and the scratch, folded over twenty
  revolutions at 78, stands 12.5 times over the median, where it should be to 0.5 ms
- **Pitch**: +12, -12 and +7 land on their notes, measured as the energy within half a
  semitone of the note (so the splice sidebands count) and 84 to 152 dB over the original;
  the warble costs a fifth 0.9 dB; Balance 50 keeps both; Feedback climbs in octaves, each
  4 dB quieter; full feedback at any pitch stays bounded
- **Filter + drive**: low and high pass 3.01 dB down on the mark and 24.1 dB down two
  octaves away; band pass at unity; Resonance 100 is +26.0 dB; Drive 0 leaves harmonics at
  -126 dB, 50 at -60, 75 at -28 and 100 at -10; the level claim above; and the aliases
- **Lo-fi**: 24.0 and 24.6 dB of noise floor for each four bits (measured on a tone whose
  period is 487 samples, so the rounding error is noise, not a pattern); a 7 kHz tone at a
  12 kHz clock folds to 5 kHz at 2.8 dB over itself, and not at all at 48 kHz
- **switches**: switching the vinyl sim on mid-tone is exactly the off render crossfading
  into the on render over 10 ms, to 2e-8
- **every dimmed control really does nothing**: in seven states, each control the panel
  would dim (63 in all) is moved end to end and the output must be bit-identical
- **the presets**: all 22 different from each other, every one within 1.4 dB of the dry
  loop's K-weighted loudness (the limit is 3) with peaks at 0.88 or under, each with a heading
- **buffer size**: every preset renders bit for bit the same at 64, 100 and 1024 samples a
  block, because everything that moves moves once a sample, counted from reset
- **no zipper**: ten controls swept once per 512-sample block against the same sweep in
  16-sample blocks; the worst adds 0.1 dB of energy between the harmonics (the suite fails
  at 2)
- **a mono track** gets, sample for sample, what the left side of a stereo one does. That
  check found the scratch's decay running only on the second channel, so on a mono track a
  tick never died away
- 44.1, 48 and 96 kHz, blocks of 1 to 4096 samples, mono, and everything at once at full:
  finite and bounded
- undo, redo and A/B: the same seventeen checks as the other two plugins, on the same file

Each of the checks that guard a rule was broken on purpose to see it fail: a false dim
claim, the compressor's gain worked out once a block, the noise moved in front of the
compressor, the switches without their fade, and the dry path one sample out. Each was
caught, by the check written for it.

`npm run test:sanitize` runs the suite under AddressSanitizer and UBSan: clean.

`host_smoke` adds 72 more at the host level: parameters exposed, a latency that does not move
whatever is switched, a transport jump re-seeding the record so a bar replayed from the same
place crackles the same way (the first pass's tail is gone from the slowest filter, the 20 Hz
rumble, by 200 ms: 3e-4 at 50 ms, 4e-9 at 200), every preset recalled and named as written,
state round-tripped, undo, A/B and your presets through a real parameter tree, the compressor
panel reading the gain the compressor is taking off and the record turning, typed values, the
preset menu's headings, favourites and arrows, and the editor painted to PNG at full and half
size.

`pluginval` passes at strictness 10 on the Linux VST3, editor tests included under a virtual
display, with random seeds 1, 2, 3, 42, 1234 and 98765.

## The presets

Twenty-two, under headings: **Start** (Init: the vinyl sim on, a little of each knob), **Vinyl
sim** (Vinyl sim, the original; Comp only; Full pressure; Worn 45; Shellac 78; Warped; Dust and
nothing else), **Pitch** (Down an octave; Fifth up, doubled; Climbing fifths; Chipmunk soul),
**Filter and drive** (Through the wall; Radio in the kitchen; Driven low pass; Thin and
driven), **Lo-fi** (Eight-bit sampler; Crushed to four bits) and **Whole loop** (Resampled
twice; Slowed and dusty; Late-night loop; Half the box). They are levelled on the audition
loop with music, drums, bass and a chord, because this is a box for whole loops. New ones are
appended, so a saved program number keeps pointing where it did.

## Honest gaps

- **Nothing here has been heard**, and nothing has been compared with an original SP-404, an
  SX or an SP-303. Every number on the panel is verified; whether it sounds like the hardware,
  or good, is not. The comp in particular is our own design for the job the knob's name
  describes, not a model of Roland's.
- No Frequency knob (the SX's playback tone). Filter + drive covers it roughly; a dedicated
  playback response, and a switch between the original's vinyl sim and the SX's, would be a
  small addition if wanted.
- The pitch shifter has no splice alignment, by design: it warbles. A shifter that searched
  for the best splice point would be cleaner and less like the era.
- Only four of the original's effects. Delay, the isolator, the looper and the rest are not
  here.
- The wow's revolution follows the song position after a transport jump, but its drift and
  flutter carry on from where they were, so a replay from the same place wobbles the same
  once-a-revolution way but not with exactly the same drift.
- No disk image, installer or download-site page yet: the packaging and the site carry
  FRACTURE and CRATE only. The build scripts and CI build, test and validate STOOP alongside
  them.
- The AU still needs `auval` on a Mac (`build-macos.sh` runs it), and the builds are unsigned.
- The JUCE splash screen is on, since turning it off needs a JUCE licence.

## Layout

```
core/            the DSP. No JUCE, no dependencies, no allocation in the audio path
  Vinyl.h        the vinyl sim: the cutting (comp), the surface (noise), the turntable (wow)
  PitchShifter.h the two-head delay-line shifter, and the fractional delay both it and the turntable read
  FilterDrive.h  the state-variable filter and the drive after it
  LoFi.h         sample-and-hold and bits
  Oversampler.h  runs Filter + drive at 4x, and keeps the dry path in step (shared design)
  Biquad.h       shapes the noise
  Relevance.h    which controls do nothing right now, and why: the panel dims them
  History.h      undo, redo and A/B (shared with FRACTURE and CRATE)
  ParamTable.h   one table, shared by the DSP, the host and the editor
  StoopCore.h    the whole processor
Source/          the JUCE wrapper and the interface
  Presets.h      the factory presets and their headings
  Session.h      when an undo step is taken, A/B in the saved state, your presets (shared)
  Guide.h        the built-in guide (shared)
tests/           the measurement suite and the host-level smoke test
```
