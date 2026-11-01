# DAW testing guide

Floorplan's automated tests drive the processor directly, with a simulated host
play head. They can't replace a real host. This checklist is what a host
compatibility report should cover. Please run it in your DAW and
[open an issue](../../issues/new?template=daw_report.md) with the results.
How Floorplan is meant to behave in each host is described in
[HOSTS.md](HOSTS.md).

## Compatibility status

| Host | OS | Format | Status | Notes |
| --- | --- | --- | --- | --- |
| Floorplan Standalone | Windows 11 | Standalone | Launches, renders UI | Verified locally by the maintainer. No full audio-device pass yet. |
| FL Studio | Windows | VST3 | Not tested | |
| Ableton Live | Windows | VST3 | Not tested | |
| *(your DAW here)* | | VST3 | Not tested | |

Only hosts that have actually passed the checklist are marked as working.
Everything else is unverified.

## What the automated tests already cover

These run on every commit (`ctest`). A host test does not need to repeat them
in detail, but a host can still behave differently from the simulation:

- MIDI: every default note plays its voice and only that voice. Velocity scales the level. Note-offs and velocity-0 notes trigger nothing. Overlapping notes retrigger. Note learn and reset work. All Sound Off silences, All Notes Off does not.
- Transport: one bar gives exactly four kicks, also with block sizes of 1 to 4096 samples mixed, above the announced maximum, and after a change from 44.1 to 96 kHz. Loops, mid-pattern starts, tempo changes and pattern switching are covered too. Count-in positions below zero line up. NaN, zero or negative tempo, NaN or infinite positions and missing information leave the audio finite and in time.
- Preview: it runs at the host tempo while the host is stopped, ends when the host starts, and nothing plays after the host stops. The sequencer on/off switch works.
- MIDI and sequencer: hits on the same drum within 2 ms play once; hits 10 ms apart play twice.
- Outputs: three bus layouts (all on, main only, every other output off). Each voice routed to its output appears there and nowhere else, or on Main if that output is disabled.
- Automation: a 30 dB level change every 43 ms reaches the sound with no sample-to-sample jump above 0.03. A one-second fade rises steadily.
- State: a fresh processor restores parameters, all pattern data, routing, the sequencer switch and the MIDI note map. Old states load new parameters at their defaults. A moved sample is found by name in the samples folder; a missing one is reported and synthesis plays.

## Setup

1. Build in Release (see the README) or download a CI artifact.
2. Install `Floorplan.vst3` (see [HOSTS.md](HOSTS.md#installing)) and rescan.
3. Note your host name and version, OS, sample rate and buffer size.

## Checklist

### Loading

- [ ] The plugin shows up as an instrument (category *Instrument | Drum*), not an effect
- [ ] It loads without errors. The UI opens and is not blank.
- [ ] The UI resizes by dragging the corner and keeps its proportions
- [ ] Closing and reopening the editor works; repeat 5 times
- [ ] Two instances in one project work independently

### Internal sequencer

- [ ] With no MIDI clips, press play in the host: the default pattern plays from beat 1, in time with the host metronome
- [ ] The header shows `HOST` and the host tempo
- [ ] Click steps on and off during playback: the change is heard on the next pass
- [ ] Change velocity, swing (66% sounds like a shuffle) and micro timing: heard as expected and still in time
- [ ] Switch patterns and use the chain: changes happen at the end of the current pattern
- [ ] With the host stopped, **PLAY** previews the pattern at the host tempo. Pressing play in the host ends the preview without a doubled pattern. Stopping the host stops everything.
- [ ] *SETTINGS → Internal sequencer on* unticked: the display shows `SEQ OFF` and the pattern stops; MIDI notes still play

### Transport sync

- [ ] Change the tempo during playback, including tempo automation: the sequencer follows
- [ ] Loop a 1-bar and a 2-bar region: no doubled or missing hits at the loop point
- [ ] Start playback in the middle of a bar, and on an odd 16th: steps line up with the grid
- [ ] Seek with the playhead during playback: the pattern continues from the new position with no burst of hits
- [ ] Stop: sequencing stops immediately (tails may ring out)
- [ ] Change the buffer size (for example 64 → 1024) and the sample rate: timing does not change
- [ ] **Ableton:** both Session clips and the Arrangement drive the pattern; launching a scene mid-song keeps the grid
- [ ] **FL Studio:** both Pattern mode and Song mode drive the pattern; looping a pattern in Pattern mode has no doubled hits

### MIDI input

- [ ] The notes in the [note map](HOSTS.md#playing-floorplan-from-midi-clips) play the right voices (FL Studio: C3 = kick; Ableton: C1 = kick)
- [ ] Velocity changes loudness
- [ ] Long and short notes sound the same; nothing hangs after stopping
- [ ] *Learn MIDI note* (right-click an instrument) remaps a voice, and the map survives saving the project
- [ ] A clip with the same kicks as the pattern, played while the pattern runs, does not double the kicks
- [ ] The pads in the UI and the keys `A`–`K` play sounds (the host may intercept key presses; note it)

### Automation

- [ ] Parameters show readable names and values in the host's automation list (**Ableton:** use *Configure*)
- [ ] Record automation by moving a knob (for example *Kick Tune*), then play it back
- [ ] Automate *Snare Level* quickly (several changes per beat): no clicks
- [ ] Automate a slow fade of *Master Volume* over several bars: smooth
- [ ] Automate *Pattern* and *Swing*
- [ ] Mute/Solo automation works
- [ ] Host undo of a parameter change works (if the host supports it)

### Separate outputs

- [ ] *SETTINGS → Each instrument to its own output*: each voice appears on its own output (*Out 2 (Kick)* … *Out 9 (Cymbal)*) and leaves the main mix
- [ ] Route the outputs to separate mixer tracks / audio tracks as described in [HOSTS.md](HOSTS.md#separate-outputs): no voice is heard twice
- [ ] Per-voice level and pan still work on the separate outputs
- [ ] Disable some outputs in the host (if it allows): those voices fall back to the main mix
- [ ] *SETTINGS → All instruments to the main output* restores a single stereo mix

### State

- [ ] Change sounds, patterns (velocity, probability, micro timing), routing, a MIDI note and a CC mapping, load a sample, enable Advanced Mode, then save the project, close the host, and reopen it: everything is restored
- [ ] Rename or move the sample file and reopen: the project loads, the voice is marked missing and plays synthesis
- [ ] Put the sample in `Documents\Floorplan\Samples` (any subfolder) and reopen: it is found
- [ ] Load a project saved with an older Floorplan version: it sounds the same
- [ ] Load each factory kit and a user kit: patterns are unchanged
- [ ] Duplicate the track or instance: the copy matches the original

### Rendering

- [ ] An offline export matches real-time playback, including probability steps and swing
- [ ] Rendering faster than real time produces no glitches
- [ ] Export with separate outputs (if the host can render stems): each stem holds only its voice

### Performance

- [ ] Note the host's CPU meter with one instance playing the House Classic pattern
- [ ] No dropouts at 64 or 128 samples buffer size

### MIDI output (optional)

- [ ] Enable *SETTINGS → Send sequencer notes to MIDI output* and route Floorplan's MIDI to another track ([HOSTS.md](HOSTS.md#midi-output)). The notes are on channel 10, in time, with the step velocities.

## Reporting

Use the **DAW compatibility report** issue template and paste the checklist
with your results. Logs, screenshots and the exact host version help a lot.
