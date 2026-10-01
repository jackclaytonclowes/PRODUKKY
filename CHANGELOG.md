# What changed

Both plugins are still beta: measured by machine, not yet judged by ear. Save your project
before you try a new version on something that matters.

## Since 0.2.0

**The browser version** has caught up: Stereo per band (L/R, Mid, Side), the Scope's input
line and harmonic readout, typed values, and the preset menu under the plugin's headings.
It also has a new test sound, a **45 Hz sub sine**, for hearing what a setting does to the
low end.

**FRACTURE** — the harmonic readout could take a low note, below what it can read, for a
higher one (a 60 Hz tone read as about 82 Hz), off the edge of its own peak. It now says
nothing instead. Fixed in the next release; the browser version has the fix already.

## 0.2.0

**FRACTURE**
- **Mid/side per band.** Each band can drive the whole image (L/R), only what the two
  channels share (Mid, the sides stay clean) or only the width (Side). Two presets show it.
- **Sidechain.** The envelope follower can listen to a sidechain instead of the track: send
  a kick to it and the distortion can duck on every hit. With nothing connected it follows
  the track as before.
- **The Scope reads out harmonics.** Play one note and the line under the spectrum names it
  and shows the harmonics the drive adds, in dB under the note. The input is drawn over the
  output, so the difference is what FRACTURE added.
- **Start from** in the harmonic table fills a frame with another mode's harmonics, or all
  four frames with one mode at rising drive.
- **The buffer size no longer changes the sound.** Modulation was worked out once per block
  your DAW sent, so six presets (Rift-ish most of all) sounded different at different
  buffer sizes, and a bounce could differ from playback. Fixed.

**CRATE**
- **Keep sub.** Everything below it skips the converter and the filter, so the kick keeps
  its weight while the rest is crushed. A preset shows it: Crushed top, clean sub.
- **Bits in use.** A meter by the converter shows how many of its bits the drums reach.
- **Lookahead.** Holds the sound back 5 ms so the hit envelope opens just before each hit.
  The difference is in the first quarter of a millisecond: keep it if you can hear it.
- **Flams and every drum hit.** The hit detector fired the second hit of a flam late, in
  the first one's tail, and missed some kicks and snares. Fixed.
- **A read past the end of the swing buffer.** A swung preset could pick up a stray value
  from memory, which could click. Fixed, and the same flaw in FRACTURE's feedback delay.

**Both**
- **Type a value**: click the number under any knob.
- **Preset menus** are grouped under headings, with **favourites** at the top.
- **Presets levelled by loudness**, not RMS, within 3 dB of the dry sound, so switching
  compares sounds rather than volumes.
- The version is shown beside each plugin's name.
- The installer re-signs each plugin where it lands. Use the installer rather than dragging
  the plugins in: a dragged copy keeps macOS's download mark, and Logic refuses it.

**The browser version** now has the plugin's anti-aliasing, so it no longer crackles.

**Worth checking**, and not yet tried by anyone: open a Logic project saved with 0.1.1 and
make sure FRACTURE still loads in it, now that it has a sidechain input.

## 0.1.1

Anti-aliased drive modes (the crackle on Rift-ish), a Tube that sounds different from Soft,
the harmonic table, preset arrows, and preset names that read as written.

## 0.1.0

The first release.
