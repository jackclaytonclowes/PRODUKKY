# FRACTURE — how it works

FRACTURE is a distortion that can split your sound into up to three frequency bands and drive each one differently. Around that are a bit crusher, a feedback loop that can ring at a musical note, a filter that can move in time with your song, and a modulation section to animate any of it.

The panel reads in the order the sound flows: top row first, left to right, then the next row.

## Quick start

- Pick a preset from the menu at the top right and change **Drive A** in the Drive panel. That is the heart of the plugin.
- Turn **Dry/wet** (in Output, bottom right) down to blend with the clean sound.
- **Auto gain** keeps the level steady as you add drive, so you hear the character, not just loudness.
- Presets named for a job (**Drum bus**, **Bass**, **808**, **Vocal**, **Amp**, **Mix bus**) are the quickest way in: pick the one that matches the track.
- The presets from **Tuned comb** onward use what only the plugin has: feedback that rings at a note, echoes locked to your tempo, and the filter rhythm. Start with **Tuned comb**, **Dub echo**, **Trance gate** and **Acid line**.
- Presets are levelled, so switching between them compares sounds, not volumes.
- **Copy patch** and **Paste patch** move a sound between the plugin and the browser version. The browser version does not have the newer features, so those parts stay behind.

## Faded controls and tooltips

- A faded control does nothing with the current settings. Hover over it and the tooltip says why, for example "Feedback is at 0%" or "Nothing in the matrix uses LFO 1". You can still move it, ready for later.
- **Tips** at the top turns these hover notes on and off. The choice is remembered.
- **Guide** opens this page. Press it again to close it.

## 01 Input & pre-filter

- **Input** sets how hard the sound hits everything after it.
- **Pre HP** and **Pre LP** trim lows and highs before the distortion. Cutting lows first keeps heavy drive from turning muddy.

## 02 Split

- **Bands**: 1 drives the whole sound. 2 or 3 split it into lows, mids and highs, so you can crush the top and keep the bass clean.
- **Split 1** and **Split 2** set where the bands divide.
- **Oversampling** makes heavy distortion cleaner and less harsh, at a small cost in CPU and delay. Leave it on 4x unless your computer struggles.

## 03 Drive

- The tabs choose which band you are editing.
- **Mode A** is the kind of distortion, from gentle tube warmth to hard clipping, wave folding and bit crushing. **Drive A** is how hard.
- **Stage B** adds a second distortion after the first, with its own **Mode B** and **Drive B**.
- **Tone** tilts the band brighter or darker between the two stages.
- **Mix** blends this band's distortion with its clean sound, and **Level** sets its volume. **Mute** and **Solo** help you hear one band at a time.

## 04 Crush & feedback

- **Crush** mixes in a bit crusher. **Bits** lowers resolution and **Downs.** lowers the sample rate.
- **Feedback** sends the sound back round a loop, for echoes, combs and ringing tones.
- **FB mode** sets how the loop is measured:
- **Time** sets its length in milliseconds (**FB time**), for echoes and metallic flanging.
- **Pitch** makes the loop ring at a musical note (**FB pitch**), like a tuned string. It stays in tune to within a cent.
- **Sync** makes each echo one note length at your song's tempo (**FB division**).
- **FB tone** darkens each repeat.
- **FB through drive** sends each echo back through the distortion, so it gets dirtier every time round. Great for growls.

## 05 Filter

- **Type** is low pass, high pass, band pass, notch or peak. **Off** bypasses it.
- **Circuit**: **Clean** is precise. **Analogue** and **Vintage** behave like a hardware filter that squelches when you push it.
- **Slope** is how steep, from gentle 12 dB to very steep 48 dB.
- **Cutoff** is where it acts and **Reso** makes it ring there. Turned right up on the analogue circuits, it whistles on its own.
- **Drive** and **Drift** belong to the analogue circuits: drive pushes the filter, and drift makes it wander slightly like real hardware.
- **Mix** blends the filtered sound with the unfiltered one.

## 06 Filter rhythm

- **Mod** is how far the rhythm moves the filter, up or down. At 0 the rhythm is off and its controls fade out.
- **Rhythm** is how often it moves, locked to your song's tempo. **Free** uses **Rate** instead.
- **Shape** is the movement, including **Steps**: draw your own eight-step pattern in the box.
- **Groove** swings it. **Phase** offsets the right side for stereo movement. **Glide** smooths the jumps.

## 07 Modulation

- Two **LFOs** (slow wobbles) and an **Envelope follower** (which follows how loud the input is) are the sources, along with the tremolo and the Perform controls.
- The **Matrix** connects them: choose a **Source**, a **Target** (almost any knob), and an amount. Up to six connections.
- The bars show what each source is doing right now.

## 08 Scope

- The top shows the sound's spectrum. The bottom shows the current band's distortion curve: flat is clean, bent is distorted.

## 09 Perform

- The **XY pad** and the two **Macro** knobs are for playing the plugin. Each is a source in the Matrix: pick **XY X**, **XY Y**, **Macro 1** or **Macro 2** as a slot's source, choose what it moves and how far, and one drag or one knob then moves all of it.
- Beside the knobs, the panel lists what each control is routed to. A control nobody routes is faded and does nothing.
- At 0 they add nothing, so routing one never changes the sound until you move it.
- Try the presets starting with **XY —** and **Macros —**.

## 10 Tremolo

- A volume chopper at the very end of the chain. **Sync** locks it to your tempo. **Shape** goes from smooth to hard chop, **Edge** softens the chop, **Duty** sets how long the loud part lasts, and **Spread** offsets left and right (180 is auto-pan).

## 11 Output

- **Dry/wet** blends the whole effect with the original, **Width** widens or narrows the stereo image, and **Output** is the final level.
- **Safety clip** stops anything going over 0 dB, even with extreme feedback.

## Good to know

- Anything that moves in time (LFOs set to a division, the rhythm, the tremolo, synced feedback) follows your song position, so the same bar sounds the same every time you play it.
