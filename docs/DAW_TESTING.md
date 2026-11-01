# DAW testing guide

Floorplan's automated tests drive the processor directly, with a simulated host
play head. They can't replace a real host. This checklist is what a host
compatibility report should cover. Please run it in your DAW and
[open an issue](../../issues/new?template=daw_report.md) with the results.

## Compatibility status

| Host | OS | Format | Status | Notes |
| --- | --- | --- | --- | --- |
| Floorplan Standalone | Windows 11 | Standalone | Launches, renders UI | Verified locally by the maintainer. No full audio-device pass yet. |
| *(your DAW here)* | | VST3 | Not tested | |

Only hosts that have actually passed the checklist are marked as working.
Everything else is unverified.

## Setup

1. Build in Release (see the README) or download a CI artifact.
2. Copy `Floorplan.vst3` to your VST3 folder (see [Installing](../README.md#installing)) and rescan.
3. Note your host name and version, OS, sample rate and buffer size.

## Checklist

### Loading

- [ ] The plugin shows up as an instrument (category *Instrument | Drum*)
- [ ] It loads without errors. The UI opens and is not blank.
- [ ] The UI resizes by dragging the corner and keeps its proportions
- [ ] Closing and reopening the editor works; repeat 5 times
- [ ] Two instances in one project work independently

### Sound and MIDI input

- [ ] MIDI notes C1, D1, F#1 and A#1 play the kick, snare, closed hat and open hat
- [ ] Velocity changes loudness
- [ ] The pads in the UI and the keyboard keys `A`–`K` play sounds (the host may intercept key presses, which is fine to note)
- [ ] *Learn MIDI note* (right-click an instrument) remaps a voice

### Transport sync

- [ ] Press play in the host: the sequencer starts on beat 1, in time with the host metronome
- [ ] The header shows `HOST SYNC` and the host tempo
- [ ] Change the tempo during playback (including tempo automation if supported): the sequencer follows
- [ ] Loop a 1-bar and a 2-bar region: no doubled or missing hits at the loop point
- [ ] Start playback in the middle of a bar, and on an odd 16th: steps line up with the grid
- [ ] Stop: sound stops sequencing immediately (tails may ring out)
- [ ] Swing at 66% sounds like a shuffle and stays in time
- [ ] Change the buffer size (e.g. 64 → 1024): timing does not change

### Automation

- [ ] Parameters show readable names and values in the host's automation list
- [ ] Record automation by moving a knob (for example *Kick Tune*), then play it back
- [ ] Automate *Pattern* to switch patterns: changes happen at the end of the current pattern
- [ ] Mute/Solo automation works

### Multi-out

- [ ] Enable the additional outputs in the host
- [ ] Set *Kick → Out 2* in the sound panel: the kick appears on output 2 and leaves the main mix
- [ ] Disable the extra outputs: the kick falls back to the main mix

### State

- [ ] Change sounds, patterns, a MIDI CC mapping and load a sample, then save the project, close and reopen it: everything is restored
- [ ] Rename or move the sample file and reopen: the project loads, the voice is marked as missing and plays synthesis
- [ ] Duplicate the track or instance: the copy matches the original
- [ ] Host undo of a parameter change works (if the host supports it)

### Rendering

- [ ] An offline bounce/export matches real-time playback, including probability steps
- [ ] Rendering faster than real time produces no glitches

### Performance

- [ ] Note the host's CPU meter with one instance playing the House Classic pattern
- [ ] No dropouts at 64 or 128 samples buffer size

### MIDI output (optional)

- [ ] Enable *MENU → Send sequencer notes to MIDI output*, route Floorplan's MIDI to another instrument: the notes are on channel 10 and in time

## Reporting

Use the **DAW compatibility report** issue template and paste the checklist
with your results. Logs, screenshots and the exact host version help a lot.
