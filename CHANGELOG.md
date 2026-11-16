# What changed

The plugins are still beta: measured by machine, not yet judged by ear. Save your project
before you try a new version on something that matters.

## Since 0.2.0

**Bigger text, less empty space** in all three plugins. Knob captions, values, panel titles,
menus, buttons and the built-in guide are a sixth to a fifth larger, and the panels are
packed to their controls: CRATE's Feel strip sits beside its knobs, Safety clip under Out's,
and each row's controls spread across their panel instead of bunching at its left;
FRACTURE's meters moved to the Output panel and its modulation matrix fills its width.
CRATE's window is 1120 x 794 (was 1080 x 810), FRACTURE's 1760 x 1000 (was 990), STOOP's
1080 x 620. The browser version of FRACTURE keeps its own sizes for now.

**STOOP, a third plugin** (`stoop/`, version 0.1.0): a chain of sampler effects after the
original SP-404's. Its vinyl simulator has the knob the SX dropped, **Comp**, beside Noise and
Wow flutter: the record's compression, then its surface (hiss, crackle, rumble and a scratch
that ticks once a revolution, laid on after the compression so it never pumps), then the
turntable (a wobble once a revolution at 33, 45 or 78, a slow drift and a fast flutter).
Before it come a pitch shifter over two octaves with Feedback and Balance, a resonant filter
with drive, and a lo-fi crusher, each with its own switch. 22 presets, levelled. Built,
tested and validated by machine (87 measurements, the host smoke test, pluginval at
strictness 10) on Linux, and added to the Mac and Windows build scripts and CI. Not yet on
the disk image, the installer or the site, nobody has heard it, and nobody has compared it
with the hardware.

**Windows.** Both plugins now build for 64-bit Windows as VST3 and standalone apps, with an
installer that puts the VST3s in `C:\Program Files\Common Files\VST3`, where Ableton Live 12
and every other Windows host look. The read-me in the download walks through Live's plug-in
settings and the sidechain. The C++ runtime is linked in, so nothing else needs installing.
Checked by machine (the DSP tests, the host smoke test and pluginval on a Windows runner);
nobody has opened them in Live on Windows yet.

**The browser version is now FRACTURE itself.** It runs the plugin's own engine, compiled to
WebAssembly and carried inside the page, behind the plugin's own panel, laid out the same.
So it has everything the plugin has: the harmonic table, tuned and synced feedback and
feedback through the drive, the filter rhythm and the filter's mix and response display,
the Perform panel's XY pad and macros, oversampling, Undo and Redo, A/B, the guide and the
tooltips, and all 49 presets under the plugin's headings. A patch copies either way. Only
the sidechain stays in your DAW. The page checks itself against the plugin: every preset
renders the same through both, to within the last bit of a float.

It also has a new test sound, a **45 Hz sub sine**, for hearing what a setting does to the
low end, and the Scope's harmonic readout reaches down to it.

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
