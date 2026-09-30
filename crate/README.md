# CRATE — twelve-bit drum processor

**New to it? Read [`GUIDE.md`](GUIDE.md)** — how CRATE works, control by control, in plain
words. It is also built into the plugin (the Guide button at the top), and ships on the
disk image as "How it works.md". This README is the engineering detail behind it.

A drum bus processor after the sound of the late-1980s sampling boxes and the records
made on them: a 26.04 kHz sample clock, linear twelve-bit conversion, a switch between the
drum machine's filters and the rack sampler's, the "45 on 33" pitch trick, a four-pole
filter, the noise floor of the record the break came off, and a swing section that pulls
the off-beats late against the host's grid.

It is an insert effect, not a sampler. You keep your own drums and your own sequencer.

```
./build-macos-all.sh              # from the repo root: builds and installs BOTH plugins
./crate/build-macos.sh --dmg      # or just this one, plus a disk image to send people
npm run test:crate                # the measurement suite: compiler only, no JUCE
cmake --build build --target host_smoke && ./build/host_smoke_artefacts/Release/host_smoke
```

This branch carries both products — FRACTURE in `plugin/` and CRATE in `crate/` — so one
checkout and one command installs the pair. JUCE is fetched once into `.juce/` and shared
between the two builds.

## About the name

This is an original product in the spirit of a well-known sampler, not a copy of one.
"SP-1200" and "E-mu" are somebody else's trademarks, the panel here is our own, and no
part of that machine's code, firmware or artwork is in this repository. What a converter
does to a signal is physics and arithmetic; that part is fair game and is what has been
rebuilt from first principles.

## What is actually in the box

```
in ─ gain ─┬─ dry ────────────────────────────────────────────────┐
           ├─ + dust ─ converter ─ four-pole ────────── wet ───────┤
           └─ hit detector ─ envelope ──┘ (opens the cutoff)       │
                                        mix ─ feel (swing, push) ─ out ─ clip
```

**The converter** is where nearly all of the character lives, and it is four separate
things: an anti-alias filter, a slow sample clock, twelve-bit quantisation, and
zero-order-hold reconstruction. The last one is why the top end droops and why there is
image content above the clock.

**Machine decides the filters around the quantiser**, which is the real difference between
the two families of box:

- **SP** — a *gentle* anti-alias filter, so content above half the clock folds back, and a
  mild output stage that leaves the staircase's images in. Bright, gritty, aliased. This is
  what the plugin always did, so older patches are unchanged.
- **S900** — a six-pole Butterworth on the way in and another on the way out, near 0.4 of
  the clock. That is the response of the MF6 switched-capacitor filters the rack samplers
  used (36 dB an octave, no resonance; the S950 shows its "bandwidth" as the rate divided
  by 2.5). Almost nothing folds back and the images are removed, leaving twelve bits and a
  band limit: darker, rounder, cleaner.

The switch does not move Clock, Bits or Compand, because a switch that quietly moved three
other controls would make every knob a liar. The presets set them.

**The two machines pitched differently, so there are two pitch controls.**

- **Tune** moves the sample clock, which is how the S900 pitched: every voice had its own
  variable DAC clock. On a bus only half of that is wanted, so Tune scales the clock and
  leaves pitch and tempo alone. Down twelve semitones is a 13 kHz clock.
- **Pitch trick** is how the SP pitched, and it is the "45 on 33" trick: speed the record
  up, sample it, tune it back down on the machine. The SP's output clock never moved; it
  pitched down by reading memory with a fractional step and no interpolation, repeating
  some samples and not others (Yeh, Nolting and Smith, ICMC 2007). Those irregular repeats
  are inharmonic, and they are the grit. On a bus it streams: at +N semitones, samples are
  taken at clock / 2^(N/12) and read onto the fixed clock by holding the latest one. Pitch
  and tempo come out unchanged, the effective sample rate drops, and the timing snaps to a
  grid that does not divide evenly. 45 against 33 is 5.2 semitones; +5 or +6 is the usual
  advice.

**The four-pole runs at four times the host rate.** Its saturation sits inside a feedback
loop, and at full drive it is nearly a hard clipper whose harmonics run past the host's
Nyquist and fold back as tones no analogue filter makes. That is an artefact of this
plugin, not a sound of the hardware, so it is removed. The converter is deliberately left
at the host rate, since its aliasing *is* the sound. Twice the rate was tried and measured:
it still leaves a 13 kHz alias of a 5 kHz tone only 21 dB under the real harmonic, and 4x
puts it 65 dB under. It costs about 3% of one core for stereo at 48 kHz, and the dry path
is delayed to match so Mix never comb-filters.

**The four-pole can be opened by every hit**, the way the SP's outputs 1 and 2 were: an
SSM2044 there was opened by each note and snapped shut within milliseconds, which is the
murky, thumping kick and the filtered bass line — a bright attack on a dark body. **Env**
is how many octaves above Cutoff a hit opens it, **Decay** is the time constant it falls
back with, and Cutoff stays the resting point it closes down to. At Env 0 (the default)
the filter is exactly the static one it always was.

A bus has no notes, so the hits are found in the audio: a fast envelope against a slow
one, triggering when the fast one is 6 dB clear, with a 40 ms hold so one hit's ringing
cannot retrigger it and a -50 dBFS floor so dust and hiss never count. Both channels share
one envelope, so a hit opens both sides together.

**The four-pole is also a rhythmic filter**, after the parameter set of the classic
plug-in for it (not its look, and not its code). All of it is off by default, and four
poles of low pass at full mix is the filter it always was.

- **Shape** LP, BP, HP or BR, and **Poles** 2, 4, 6 or 8. Every combination comes out of
  the same resonant core, so changing either never re-tunes the resonance. Cutoff marks the
  -3 dB point for low and high pass at every slope, and the centre for band pass and band
  reject. The one-poles are bilinear rather than the simpler kind, because the simpler
  kind cannot make an exact notch: the first try measured 21 dB of band reject at 8 poles.
  **Filter mix** blends the filter against what went into it, inside the oversampling.
- **Rhythm** moves the cutoff. **Mod** is how far, in octaves, either way from Cutoff, which
  stays where the sweep rests. **Rhythm** is the division (Free uses **Rate** instead),
  **Shape** is sine, triangle, both saws, square, a random value per division, or **Steps**:
  eight bars you draw on the panel. **Groove** swings every second division late, the way a
  drum machine swings, so on Steps it is swung sixteenths. **Phase** offsets the right
  channel, up to opposite at 180, and **Glide** rounds the edges.
- The rhythm reads the song position, so the same bar moves the same way wherever the
  playhead starts, and Random is a hash of the position rather than a generator, so two
  bounces agree. With the transport stopped it keeps time at the host's tempo instead of
  freezing.
- There is no tap tempo. Inside a host the tempo comes from the host; in the standalone
  app the rhythm runs at the app's tempo or on Free.

**Twelve bits, linear, by default.** Both machines stored linear PCM. The first version of
this plugin defaulted to µ-law companding and called it the character, which was a guess
and was wrong. **Compand** is kept as an extra colour: it spends resolution on quiet
signals and takes it from loud ones, so hits get grainier as they get louder while tails
stay clean. Both directions are measured in the test suite.

**Dust goes in before the converter**, because that is the order it happened in: someone
sampled a noisy pressing and the sampler crushed the noise along with the drums. It is
seeded from the transport, so the same bar renders the same crackle every time.

## Two things about the feel section

**It needs the transport running.** The grid comes from the host. With playback stopped
the swing knob does nothing, and the panel says so rather than leaving you to wonder.

**It reports latency, deliberately.** The whole plugin sits behind a fixed delay (120 ms on
the 1/16 grid, plus 1 ms for the four-pole's oversampling) so Push can pull hits *earlier*
as well as later. The host compensates it
away; the only thing you hear is the relative movement. Changing the grid changes the base
delay and the plugin re-declares it.

The hard part was not the offsets, it was *when* the read pointer is allowed to move.
Moving a delay pointer does not shift an event: lengthening the delay re-reads audio that
already came out, and shortening it skips audio that never will. The first version
scheduled the move against the output clock and, on an impulse train, played every hit
twice — once on the beat and again a swung sixteenth later. Now the move is scheduled
against the position being *read*, in the gap before that step's hit, with a guard the size
of the jump. Both moves crossfade over 8 ms, so what smears is the tail of the previous hit
rather than the attack of the next one.

That smearing is the honest cost of swinging finished audio. A sampler moves the note and
pays nothing; a bus processor cannot. It is least audible on drums with decaying tails and
most audible on sustained material — try it on a loop with a chord under the drums before
deciding you like it.

## The panel

**Controls that do nothing right now are dimmed**, and the tooltip says why: Decay while
Env is off, Dust tone with no dust, Rate while the rhythm follows a division, the whole
rhythm while its Mod is off, everything on the wet path at 0% mix. They still work, so a
value can be set up before it matters. The rules are in `core/Relevance.h`, and the test
suite holds them to it. Every control has a one-line tooltip. **Tips**, at the
top, turns tooltips off and on; the choice is saved as a preference on the machine rather
than in the session, because it is not part of the sound. **Guide** opens `GUIDE.md` over
the panel. It is compiled in from the same file, so the two cannot say different things.

**Undo, Redo, A/B and Save** sit in the header, and work as FRACTURE's do (the code is
shared: `core/History.h` and `Source/Session.h`). One undo step per knob drag, one per
preset; host automation never fills the history; A and B each keep their own undo, and the
hidden side is saved with the session. Your presets are one JSON file each in
Documents/CRATE/Presets, written as `{id: value}` with choices by name
(`"machine": "S900"`), so they read clearly and survive new parameters being added: anything
a file leaves out loads at its default.

## What is verified, and how

`npm run test:crate` — 93 assertions, no JUCE needed (`VERBOSE=1` prints the measured
value behind every one). These are measurements, not smoke
tests, because nobody involved in building this has heard it:

- **the hold droops the top end** the way a sample-and-hold does, and 1 kHz passes at unity
- **content above half the clock folds back** into the band at a predictable frequency, and
  the anti-alias control moves it by more than 10 dB — so that knob does what it says
- **six decibels a bit**: 16 → 12 and 12 → 8 each cost about 24 dB of noise floor
- **companding makes loud hits grainier and quiet tails cleaner** — both directions
- **the S900 filters do their job**: 1 kHz at unity, a 20 kHz tone the SP folds back to
  6 kHz at about -4 dB is 20+ dB lower (the measured -72 dB flatters it: near the host's
  Nyquist the digital filter is steeper than the analogue one), and a hold image the SP
  leaves at -12 dB is taken below -40
- **the pitch trick keeps the pitch and the level** (1 kHz in, 1 kHz out, within 0.1 dB),
  **adds inharmonic residue** the plain converter does not have (about 11 dB at +6), and
  **lowers the effective sample rate**, so a 10 kHz tone folds to 8.4 kHz at +6 where the
  plain converter puts nothing
- **oversampling does its job**: at drive 8, a 5 kHz tone's audible aliases (3 and 13 kHz)
  sit 10 dB under the 15 kHz harmonic at the host rate and 65 dB under at 4x; dry and wet
  stay in step at a half mix to within 0.1 dB; reported latency still matches the measured
  latency sample for sample
- **every shape at every slope does what it says**: low and high pass 3 dB down on the
  mark to within 0.01 dB and falling at 6 dB an octave per pole, band pass at unity on the
  mark, band reject a true notch (below -200 dB). Filter mix at 0 takes it out exactly
- **the rhythm keeps time**: a synced square rises on the beat it should, the same bar
  comes out the same from two bars later, groove 66 puts the second of each pair at
  two thirds of the pair, the eight steps play in order, phase 180 mirrors the right
  channel, random agrees across renders, Free and a stopped transport run at the right
  speed, glide rounds the edges, and Mod +4 oct opens the filter by 12 dB on the high half
- **the half mix is predicted, not assumed**: the dry reference is the input shifted by the
  reported latency, and a deliberate one-sample error fails the test by 5 dB
- **no control zippers**: each is swept the way a host sends automation, once per 512-sample
  block, and the energy a sine picks up between its harmonics is compared with the same
  sweep in 16-sample blocks. Every control measured is under 1 dB; the suite fails above 2
- **every control the panel dims really does nothing**: in eight states of the box, each
  control the panel would dim (130 in all) is moved end to end and the output must be
  bit-identical
- **the hit envelope counts hits**: eight snare-like bursts in are eight triggers out, and
  a sustained tone triggers once at most. A flam (20, 25 or 30 ms, soft-then-loud or
  equal) is two triggers, each within 4 ms of its hit, and all eleven kicks and snares of
  the audition loop trigger within 2 ms while most of the hats between them do not. The
  first detector failed both: it fired a flam's second hit 40 ms late, in the first one's
  tail, and missed two of the eleven drums. Each hit brings the first 10 ms through about
  15 dB brighter against the resting filter, the tail 250 ms later is the resting filter
  to within 0.01 dB, and Decay is the time constant it claims (0.368 after one of them)
- **tune leaves the pitch alone** (the fundamental stays put, nothing appears an octave down)
- **the filter's marked cutoff is its -3 dB point** and the slope is about 24 dB an octave
  where it settles. The first version was 3 dB out because four cascaded one-poles reach
  -3 dB at 0.435 of their own corner, not 0.6436; the test found it
- **swing offsets are the milliseconds the grid says**: at 66% on a 1/16 grid at 90 BPM the
  off-beats land 55.6 ms late, the on-beats do not move, and **sixteen hits go in and
  sixteen come out** — that last one is what caught the double-triggering
- **push moves everything by the milliseconds it says**, and stopping the transport stops
  the swing
- **reported latency is the measured latency**, sample for sample
- dust is bit-identical across two renders of the same bar, and silent at zero
- 44.1 / 48 / 96 kHz, block sizes 16 to 1024, and everything at once: finite and bounded
- undo, redo and A/B: the same seventeen checks as FRACTURE's, on the same file

`host_smoke` adds 70 more at the host level, including a synthetic transport: the plugin
sees the tempo, notices when playback stops, re-declares its latency when the grid changes,
recalls all twenty-six presets, round-trips its state, and paints its editor to a PNG — twice,
the second time at half size, checking the corner panels are scaled rather than cropped. It
also takes undo, A/B and your presets through a real parameter tree: a drag is one step, a
session saved on B reopens on B with A held, a saved preset reads as named choices and
loads back, and the header's buttons and Ctrl + Shift + Z do what they say.

## The presets

Twenty-six, grouped by what they demonstrate: the machines and their outputs (**SP,
outputs 1-2 / 3-4 / 7-8**, **S900, forty kilohertz**, **S950, bass lifted out**), the pitch
trick (**SP, 45 on 33**), the filter rhythm (the five **Rhythm:** presets), feel (**Off the
grid**, **Behind the beat**, **Drunk sixteenths**), and starting points for a whole beat
(**Lo-fi bus, S900 at 22 kHz**, **Chopped soul break**, **Quarter-note filter pump**). New
ones are appended, so a saved program number keeps pointing where it did.

Every preset is levelled on the audition loop to within 6 dB of the dry drums and kept off
the ceiling, and the test suite fails if one drifts or if two become the same sound. The
listening renders found both: "Twelve bit, straight" had become a copy of Init once the
default went linear (it is now "Twelve bit, companded", the old default's sound), and
"Rhythm: swung band pass" sat 14.5 dB down.

## Honest gaps

- **Nothing here has been heard.** No audio device in the machine that built it. Every
  number on the panel is verified; whether it sounds good is not.
- The pitch trick is a streaming model of drop-sample playback, not a replay of it: the
  SP read a stored sample at a fractional step, whereas this holds the latest input
  sample on the fixed clock. The two produce the same kind of irregular repeat and the
  same drop in effective rate; whether they are indistinguishable has not been listened
  for.
- The hit envelope has only a little lookahead: the detector hears the input before the
  oversampler's filters do, which gives it about half a millisecond, enough to bring the
  first millisecond of each hit through 2.5 dB brighter than a detector with none. The
  hardware knew about the note before the sound did; matching that fully needs real
  lookahead, and so more latency. Flams now trigger twice, but a roll of equal hits 20 or
  30 ms apart still reads as one or two, because each lands on the tails of the last;
  separating those needs a detector that looks at the spectrum.
- The oversampler removes the audible aliases of the four-pole's drive but not a
  harmonic sitting just past the host's Nyquist: 25 kHz folds to 23 kHz at 48 kHz, in
  the half-band filter's transition. Inaudible at 48 kHz; at 44.1 kHz the same harmonic
  lands near 19 kHz.
- The S900 filter is modelled as its response, not its circuit: switched-capacitor
  clock feedthrough and the MF6's own noise are not in it.
- Swing is grid-locked, so it moves everything sitting on an off-beat, not individual hits.
  Per-hit humanising needs transient detection and is a separate build.
- No tempo-synced dust, no per-band anything, no MIDI.
- `pluginval` passes at strictness 10 (its maximum, fuzzing included) on the Linux VST3
  build, editor tests included under a virtual display, across six random seeds. The
  first run failed: a switch restored from a saved state kept the fractional value a host
  had left on it (0.21 instead of 0) because JUCE's `replaceState` skips a parameter whose
  snapped value looks unchanged, so `setStateInformation` now writes every parameter back.
  The AU still needs `auval` on a Mac (the build script runs it), and Steinberg's own VST3
  validator has not been run.
- `npm run audition` renders every preset over a loop (or your own WAV) with the real DSP,
  so it can be heard without a DAW.
- The JUCE splash screen is on, since turning it off needs a JUCE licence.

## Layout

```
core/           the DSP. No JUCE, no dependencies, no allocation in the audio path
  Converter.h   clock, companding, quantisation, zero-order hold
  Ladder.h      the four-pole: LP, BP, HP, BR at 2, 4, 6 or 8 poles
  Feel.h        swing and push against the host grid
  Dust.h        hiss and crackle, seeded from the transport
  HitEnv.h      finds hits in the audio and gives the four-pole its envelope
  Oversampler.h runs the four-pole at 4x, and keeps the dry path in step
  RhythmMod.h   the rhythm: divisions, shapes, steps, groove, phase, glide
  Relevance.h   which controls do nothing right now, and why: the panel dims them
  History.h     undo, redo and A/B, on whole-patch snapshots (shared with FRACTURE)
  ParamTable.h  one table, shared by the DSP, the host and the editor
  CrateCore.h   the whole processor
Source/         the JUCE wrapper and the interface
  Session.h     when an undo step is taken, A/B in the saved state, your presets
                (shared with FRACTURE)
tests/          the measurement suite and the host-level smoke test
packaging/      disk image for testers
```
