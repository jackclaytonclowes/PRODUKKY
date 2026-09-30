# CRATE — how it works

CRATE makes drums sound like they came out of a late-1980s sampler: the gritty, dusty, slightly swung sound of boom-bap records made on an SP-1200 or an Akai S900. Put it on a drum bus or a drum loop. It is an effect, not a sampler, so your drums, your sequencer and your tempo stay as they are.

Everything runs left to right and top to bottom on the panel, which is also the order the sound goes through it.

## Quick start

- Open the preset menu at the top right and try **SP, 45 on 33**, **Dusty break** and **S900, forty kilohertz**. Between them they cover most of what CRATE does.
- The **arrows** either side of the preset menu step to the previous and next preset without opening the list. They run through the factory presets and then yours, and wrap round at the ends.
- For a whole beat, try **Lo-fi bus, S900 at 22 kHz** or **Chopped soul break**. Presets are levelled, so switching compares sounds, not volumes.
- Turn **Mix** down to blend the effect with your clean drums.
- If it gets too loud, turn **Output** down. **Safety clip** stops anything going over 0 dB.

## Faded controls and tooltips

- A faded control does nothing with the current settings. Hover over it and the tooltip says why, for example "Env is off". You can still move it, ready for later.
- **Tips** at the top turns these hover notes on and off. The choice is remembered.
- **Guide** opens this page. Press it again to close it.

## Undo, compare and your presets

- **Undo** and **Redo** at the top take back and redo changes: each knob drag, each preset you pick. Ctrl + Z and Ctrl + Shift + Z do the same (Cmd on a Mac), when the panel has the keyboard.
- **A** and **B** are two settings to flip between, so you can compare an idea with what you had. B starts as a copy of A. **A to B** copies what you hear now into the other one. Each keeps its own undo, and both are saved with your song.
- **Save**, next to the preset menu, saves your settings as a preset of your own. They appear under **Your presets** in the menu, and **Show the presets folder** at the bottom of the menu opens the folder, in Documents/CRATE/Presets. Each preset is a small file you can back up, copy to another computer or send to someone.

## 01 Input

- **Input** sets how hard you hit the rest of the box. Pushing it makes the converter clip, which is part of the sound.
- **Mono** sums left and right, the way the old machines sampled.

## 02 Converter — where most of the character comes from

- **Machine** picks the sampler. **SP** is bright and gritty: little filtering, so high sounds fold back as crunchy aliasing. **S900** is darker and smoother: steep filters on the way in and out.
- **Tune** lowers or raises the sample rate without changing pitch or tempo. Down is grittier.
- **Pitch trick** copies the classic move of playing a record fast, sampling it, then slowing it back down on the sampler. It lowers the quality and adds the SP's particular grain. Try +5.
- **Clock** is the sample rate itself. 26 kHz is the SP-1200; the S900 ran up to 40 kHz.
- **Bits** is resolution. 12 is the classic; lower is noisier and rougher.
- **Compand** squashes quiet and loud differently, so loud hits get grainier. Off is how the real machines worked.
- **Anti-alias** controls how much of the crunchy fold-back gets through. Lower is crunchier.

## 03 Four-pole — the filter

- **Shape** is low pass (removes highs), band pass, high pass (removes lows) or band reject (a notch).
- **Poles** is how steep: 2 is gentle, 8 is a cliff.
- **Cutoff** is where the filter acts, and **Reso** makes it ring at that point.
- **Drive** pushes the filter into saturation.
- **Env** opens the filter on every drum hit and lets it close again: bright attack, dark body. That is the sound of the SP's first two outputs on kicks. **Decay** is how fast it closes.
- **Filter mix** blends the filtered sound with the unfiltered one.

## 04 Dust

- **Dust** adds hiss and crackle from an imaginary record, before the converter, so it gets crushed along with the drums. **Dust tone** makes it brighter or duller. It sounds the same every time you play the same bar.

## 05 Rhythm — a filter that moves in time

- **Mod** is how far the rhythm moves the filter, up or down. At 0 the rhythm is off and its other controls fade out.
- **Rhythm** is how often it moves, locked to your song's tempo (1/8, 1/16 and so on). **Free** uses **Rate** instead.
- **Shape** is the movement: smooth sine, triangle, ramps, an on/off square, a random value each step, or **Steps**. With Steps, draw your own eight-step pattern in the box below.
- **Groove** swings the movement, like drum machine swing.
- **Phase** offsets the right side for a stereo effect. **Glide** smooths the jumps.

## 06 Out

- **Mix** blends the whole effect with the dry drums, and **Output** is the final level.
- The meters show the level coming in and going out.

## 07 Feel — swing

- **Swing** pushes the off-beats late, like MPC or SP swing. 50% is straight; 54 to 62% is the usual boom-bap range. It needs your song playing, because it follows your DAW's grid.
- **Grid** chooses what gets swung: 1/8, 1/16 or 1/32.
- **Push** moves everything earlier or later, to sit the drums ahead of or behind the beat.
- The strip shows where each hit will land.
- CRATE reports a short delay to your DAW so it can pull hits earlier as well as later. Your DAW makes up for it automatically.

## Good to know

- Swing works on the finished drum sound, so it can smear a note held under the drums. It sounds best on drums alone.
- Nothing in CRATE is random from one playback to the next: the same bar always sounds the same.
