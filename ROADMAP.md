# Roadmap

Where FRACTURE and CRATE stand, what to improve, and what could sit alongside them. Written
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
- Each preset: keep, fix or cut.

## FRACTURE

- [x] **M** Modulation and smoothing on a fixed clock, independent of the buffer size
- [ ] **S** Version in the header
- [ ] **S** Type a value into any knob
- [ ] **S** Short fade and a feedback clear on preset change
- [ ] **S** Table "Start from…": fill the bars with another mode's harmonics
- [ ] **S** The anti-aliasing in the browser version
- [ ] **M** Per-band stereo: L/R, mid only, side only
- [ ] **M** Sidechain input for the envelope follower (check old Logic sessions still open)
- [ ] **M** Spectrum view in the scope, harmonics labelled
- [ ] **M** Table "hold the recipe": level-independent harmonics
- [ ] **M** Feedback pitch snapped to a key
- [ ] **M** Auto gain and preset levelling by loudness (K-weighted), not RMS
- [ ] **M** Preset browser with tags and favourites (both plugins)
- [ ] **L** Dynamic Tube and Tape: bias follows level, sag, emphasis, hysteresis, head bump
- [ ] **L** 8x quality for bounces, latency held constant
- [ ] **L** Cheaper (polyphase) oversampler and a low-latency mode
- [ ] **L** A layout that reflows, and a larger-text option

## CRATE

- [ ] **S** Version in the header
- [ ] **S** Keep the sub: a crossover so the low end skips the converter
- [ ] **S** A "bits in use" meter by the converter
- [ ] **S** Flams: re-arm the hit detector on a level drop, not a fixed 40 ms hold
- [ ] **M** Optional 5 ms lookahead for the hit envelope
- [ ] **M** More machines: S950, MPC60, SP-12
- [ ] **M** Dust in time with the song
- [ ] **L** Per-hit swing (probably belongs in CHOP)

## Shared and shipping

- [ ] **S** Installer re-signs ad hoc; the site says to use the installer, not drag
- [ ] **S** A licence (AGPLv3 to match JUCE's free licence, or a paid JUCE licence)
- [x] **S** Stale docs: "fourteen modes", "44 presets"
- [ ] **M** Developer ID signing and notarisation in CI ($99 a year)
- [ ] **M** Windows VST3, Linux VST3 and CLAP builds
- [ ] **S** Steinberg's VST3 validator and pluginval on the Mac in CI
- [ ] **S** Before-and-after audio previews on the site
- [ ] **M** Before a third plugin: shared code in one `shared/` folder, and build, disk image and
  site driven by a list of plugins

## New plugins (working names)

1. **SLAB**: drum bus clipper and transient shaper. Reuses the ADAA shapers, oversampler, hit
   envelope and crossovers. S–M.
2. **SPOOL**: tape and vinyl: hysteresis, head bump, wow and flutter, hiss, crackle, rumble.
   Groundwork for FRACTURE's dynamic Tape. M–L.
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
  loudness levelling; Windows and CLAP; signing.
- **0.3**: dynamic Tube and Tape, bounce quality, cheaper oversampler, more CRATE machines.
- **Then**: `shared/`, SLAB, SPOOL.
