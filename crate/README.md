# CRATE — twelve-bit drum processor

A drum bus processor after the sound of the late-1980s sampling boxes and the records
made on them: a 26.04 kHz sample clock, companded twelve-bit conversion, a four-pole
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
           └─ + dust ─ converter ─ four-pole ────────── wet ───────┤
                                        mix ─ feel (swing, push) ─ out ─ clip
```

**The converter** is where nearly all of the character lives, and it is four separate
things: a *gentle* anti-alias filter (not a brickwall — content above half the clock is
supposed to fold back), a slow sample clock, companded twelve-bit quantisation, and
zero-order-hold reconstruction. The last one is why the top end droops and why there is
image content above the clock.

**Tune moves the clock without moving the pitch.** On the hardware, pitching a sample down
slowed the clock and the audio together. On a bus you only want the first half of that, so
Tune scales the sample clock and leaves your tempo alone: down twelve semitones is a 13 kHz
clock, which is the classic "pitched down for grit" sound with the loop still in time.

**Compand is a trade, not an amount of dirt.** µ-law spends resolution on quiet signals and
takes it from loud ones, so hits get grainier as they get louder while tails stay clean —
the opposite of a linear twelve-bit converter. At 0 you get the linear one. Both directions
are measured in the test suite.

**Dust goes in before the converter**, because that is the order it happened in: someone
sampled a noisy pressing and the sampler crushed the noise along with the drums. It is
seeded from the transport, so the same bar renders the same crackle every time.

## Two things about the feel section

**It needs the transport running.** The grid comes from the host. With playback stopped
the swing knob does nothing, and the panel says so rather than leaving you to wonder.

**It reports latency, deliberately.** The whole plugin sits behind a fixed delay (120 ms on
the 1/16 grid) so Push can pull hits *earlier* as well as later. The host compensates it
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

## What is verified, and how

`npm run test:crate` — 36 assertions, no JUCE needed. These are measurements, not smoke
tests, because nobody involved in building this has heard it:

- **the hold droops the top end** the way a sample-and-hold does, and 1 kHz passes at unity
- **content above half the clock folds back** into the band at a predictable frequency, and
  the anti-alias control moves it by more than 10 dB — so that knob does what it says
- **six decibels a bit**: 16 → 12 and 12 → 8 each cost about 24 dB of noise floor
- **companding makes loud hits grainier and quiet tails cleaner** — both directions
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

`host_smoke` adds 21 more at the host level, including a synthetic transport: the plugin
sees the tempo, notices when playback stops, re-declares its latency when the grid changes,
recalls all nine presets, round-trips its state, and paints its editor to a PNG.

## Honest gaps

- **Nothing here has been heard.** No audio device in the machine that built it. Every
  number on the panel is verified; whether it sounds good is not.
- The four-pole runs at the host rate with no oversampling. Its saturation will alias at
  high drive with bright material. If that turns out to matter, the fix is the same
  oversampler the other product uses.
- Swing is grid-locked, so it moves everything sitting on an off-beat, not individual hits.
  Per-hit humanising needs transient detection and is a separate build.
- No tempo-synced dust, no per-band anything, no MIDI.
- `pluginval` has not been run. Do that before trusting it in a session.
- The JUCE splash screen is on, since turning it off needs a JUCE licence.

## Layout

```
core/           the DSP. No JUCE, no dependencies, no allocation in the audio path
  Converter.h   clock, companding, quantisation, zero-order hold
  Ladder.h      the four-pole
  Feel.h        swing and push against the host grid
  Dust.h        hiss and crackle, seeded from the transport
  ParamTable.h  one table, shared by the DSP, the host and the editor
  CrateCore.h   the whole processor
Source/         the JUCE wrapper and the interface
tests/          the measurement suite and the host-level smoke test
packaging/      disk image for testers
```
