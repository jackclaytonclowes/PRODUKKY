# Roadmap

Where FRACTURE, CRATE and STOOP stand, what to improve, and what could sit alongside them. Written
after 0.1.1. Effort: **S** a session, **M** a few, **L** weeks. Tick items off as they land.

## Found while planning: FRACTURE depended on the buffer size

The envelope follower, the LFOs and the matrix were worked out once per host block. Every
preset rendered over the audition loop at 64- and 1024-sample buffers, difference relative to
the output:

| Preset | 64 vs 1024 |
|---|---|
| Rift-ish — serial fold into filter | −4.8 dB |
| Harmonic pan | −8.0 dB |
| Bit rot | −10.9 dB |
| Ladder sweep | −12.7 dB |
| Squelch | −15.7 dB |
| Table — the harder you play | −19.6 dB |
| the other 41 | identical, or below −46 dB |

All six route the envelope or the tremolo to a target. At 1024 samples those targets moved
every 21 ms, so a 6 ms attack could not happen as set, and a bounce (which a host may run at a
different buffer size from playback) need not match what was heard. Rift-ish has the envelope
on Downsample, which jumped at every block edge; that may be part of the crackle reported on
it. CRATE differs only on the swing presets, by −43 to −65 dB.

Fix: modulation and smoothing on a fixed internal clock, and a test that renders at several
buffer sizes and requires the same output. First item of 0.1.2.

## Where things stand

**FRACTURE.** Flat-summing split, tuned feedback within a cent, audible aliasing at −35 to −38
dB in the worst cases, levelled presets, and a drawn-harmonic Table mode few distortions have.
Gaps: the drive curves are static (no bias shift, sag, tape hysteresis or head bump); no
mid/side per band; no sidechain; knobs cannot take a typed value; a preset change swaps every
value at once and keeps the old sound in the feedback loop; Table's recipe shifts with input
level; the browser version on the site still has the 0.1.0 aliasing.

**CRATE.** A thorough, measured converter model. Gaps: about 0.5 ms of hit lookahead; hits
closer than 40 ms merge; swing moves everything on an off-beat, not individual hits; no way to
keep the sub out of the converter; nothing shows how hard the converter is hit; two machines.

**Both.** Unsigned builds (Gatekeeper, Logic refusing to load until quarantine is cleared); no
version in the window; no licence file on a public repository that uses JUCE under the AGPL;
Mac only; nobody has judged the sound against references yet.

## Listening session (needs ears)

- Material: drum loop, bass DI, vocal, full mix, a sine sweep, one cymbal hit.
- Sine sweep through Wrap, Fold and Bits at high drive at 44.1 kHz: anything that does not
  follow the sweep is aliasing.
- Bounce offline and in real time, invert one: they should cancel.
- Switch presets during playback: clicks?
- Fast Drive and Cutoff automation at buffer 32 and 1024.
- All bands clean at Drive 1 against bypass on a kick: is the split's phase shift audible?
- 8 to 10 instances for CPU; mono with Width at 200%.
- Level-matched against Decapitator, Saturn 2 or Thermal (FRACTURE) and RC-20, Decimort or an
  SP-1200 emulation (CRATE).
- STOOP against an original SP-404's Vinyl Sim, an SX's and an SP-303's, on the same loop:
  Comp at 0, 50 and 100 (is ours as soft as theirs, does it pump the same way?), Wow flutter
  at full (is 1% once a revolution about right?), and Pitch at +7 with Feedback.
- Each preset: keep, fix or cut.

## FRACTURE

- [x] **M** Modulation and smoothing on a fixed clock, independent of the buffer size
- [x] **S** Version in the header
- [x] **S** Type a value into any knob (both plugins: click the number under it)
- [x] **S** Short fade and a feedback clear on preset change: measured instead of built. Switching
  between eight pairs of presets mid-note, the biggest jump is at least 7 dB under what the presets
  make on their own. Build it only if someone hears a click
- [x] **S** Table "Start from…": fill the bars with another mode's harmonics (and all four frames at rising drive)
- [x] **M** The anti-aliasing in the browser version: the plugin's oversampler and shapers in a worklet
- [x] **L** The browser version runs the plugin's engine: `plugin/core` compiled to WebAssembly and embedded, behind the plugin's own panel, with every control and every preset, checked against a native build on every preset
- [x] **M** Per-band stereo: L/R, mid only, side only
- [x] **M** Sidechain input for the envelope follower (still to check: old Logic sessions open)
- [x] **M** Spectrum view in the scope, harmonics labelled (input against output, and a harmonic readout)
- [ ] **M** Table "hold the recipe": level-independent harmonics
- [ ] **M** Feedback pitch snapped to a key
- [x] **M** Preset levelling by loudness (K-weighted), not RMS: both plugins, within 3 dB
- [ ] **M** Auto gain calibrated per mode on loudness. Measured: the fixed curve lets most
  modes rise 3 to 5 dB at middle drives, Gap 9 and Harmonics 20 dB at low drive. Changing it
  also changes how hard each stage drives the next, so most presets would sound different:
  after a listening session
- [x] **M** Preset browser with tags and favourites (both plugins): grouped headings, favourites on top; a search box is still to do
- [ ] **L** Dynamic Tube and Tape: bias follows level, sag, emphasis, hysteresis, head bump
- [ ] **L** 8x quality for bounces, latency held constant
- [ ] **L** Cheaper (polyphase) oversampler and a low-latency mode
- [ ] **L** A layout that reflows, and a larger-text option (the text itself is now a sixth
  larger in all three plugins, and the panels packed tighter)
- [ ] **S** The browser version's panel at the plugin's new text sizes

## CRATE

- [x] **S** Version in the header
- [x] **S** Keep the sub: a crossover so the low end skips the converter (and the four-pole)
- [x] **S** A "bits in use" meter by the converter
- [x] **S** Flams: the detector now looks for a rise, with a 10 ms hold. It also finds all eleven
  drums in the audition loop (it found nine). Equal-level rolls still merge: needs spectral onsets
- [x] **M** Optional 5 ms lookahead for the hit envelope (measured: the gain is in the first 0.25 ms)
- [ ] **M** More machines: S950, MPC60, SP-12
- [ ] **M** Dust in time with the song
- [ ] **L** Per-hit swing (probably belongs in CHOP)

## STOOP

- [x] **L** First version: the vinyl sim with the original's Comp (the cutting, the surface,
  the turntable, at 33, 45 or 78), the pitch shifter with feedback, filter and drive, lo-fi,
  22 presets; 87 measurements, the host smoke test, pluginval at strictness 10 on six seeds
- [ ] **M** Disk image, Windows installer and a page on the site: after the `shared/` item
  below, rather than a third hand-made copy of each
- [ ] **S** The SX's Frequency knob (the playback system's tone), and a switch between the
  original's vinyl sim and the SX's, so the two can be compared in one place
- [ ] **S** Drift and flutter re-seeded from the song position as well as the revolution, so a
  replay wobbles exactly the same
- [ ] **M** More of the original's effects: delay, the isolator, the BPM looper
- [ ] **M** A cleaner pitch shifter (splices placed where the waveform matches) as a second
  mode beside the warbly one

## Shared and shipping

- [x] **S** Installer re-signs ad hoc; the site says to use the installer, not drag
- [ ] **S** A licence (AGPLv3 to match JUCE's free licence, or a paid JUCE licence)
- [x] **S** Stale docs: "fourteen modes", "44 presets"
- [ ] **M** Developer ID signing and notarisation in CI ($99 a year)
- [x] **M** Windows VST3 and standalone, built with MSVC in CI (`windows.yml`): DSP tests, host
  smoke test, pluginval at strictness 10, zip and installer. Not yet opened in Live by a person
- [ ] **M** Linux VST3 and CLAP builds
- [ ] **S** Steinberg's VST3 validator and pluginval on the Mac in CI
- [ ] **S** Before-and-after audio previews on the site
- [ ] **M** Shared code in one `shared/` folder, and build, disk image and site driven by a
  list of plugins. Meant to come before a third plugin; STOOP came first, as copies (the
  build scripts and CI list it by hand; the disk image, installer and site do not yet)

## New plugins (working names)

1. **SLAB**: drum bus clipper and transient shaper. Reuses the ADAA shapers, oversampler, hit
   envelope and crossovers. S–M.
2. **SPOOL**: tape and vinyl: hysteresis, head bump, wow and flutter, hiss, crackle, rumble.
   Groundwork for FRACTURE's dynamic Tape. M–L. STOOP's turntable and surface noise are a
   start on the vinyl half.
3. **RELAY**: tape and dub echo from FRACTURE's feedback loop. M.
4. **LUSTRE**: harmonic exciter for mixing and mastering, the Table idea made precise. M.
5. **SHUTTER**: rhythmic gate and volume shaper. S–M, crowded market.
6. **CHOP**: resampler and chopper: slices on hits, real drop-sample pitch, per-hit swing. L.
7. **PLATE**: lo-fi spring and plate reverb, tail through CRATE's converter. M–L, judged by ear.
8. **Wavetable synth** from the harmonic frames. Largest by far.

## Order

- **0.1.2**: fixed-clock modulation; listening fixes; versions, typed values, preset-change fade,
  flam fix, Start from…; browser anti-aliasing; docs, installer re-sign, site warning, licence.
- **0.2**: mid/side, sidechain, spectrum, preset browser; keep the sub, lookahead, bits meter;
  loudness levelling; signing. (CLAP set aside; Windows added after 0.2.0.)
- **0.3**: dynamic Tube and Tape, bounce quality, cheaper oversampler, more CRATE machines.
- **Then**: `shared/`, SLAB, SPOOL.
