# Changelog

All notable changes to this project are documented in this file. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Character section** (mixer → CHARACTER), off by default and bit-identical when off. See [docs/CHARACTER.md](docs/CHARACTER.md). It has four controls:
  - **Vel Tone:** soft hits are darker as well as quieter. Each synth engine maps it to its own brightness controls.
  - **Vary:** bounded per-hit variation of tune, decay, level and tone. It is repeatable: the same song position gives the same hit, and the Groove VARIATION seed picks the variation.
  - **Color:** asymmetric parallel saturation that never raises the level.
  - **Punch:** an attack shaper, ±6 dB.
- **Choked By** for every voice (Advanced Mode → OUTPUT). The default keeps the open hat choked by the closed hat; *None* or any other pairing is possible.
- 13 new parameters, appended after Sequencer On so no existing index moves. Older projects load with the section off and the default chokes.
- `floorplan_plugin_tests --render <wav> [id=value...]` renders a fixed demo to a 32-bit float WAV, used for level and bit-identity checks.
- Eight tests for the Character section and choke groups:
  - off is bit-identical;
  - velocity changes the tone;
  - variation is bounded and reproducible;
  - configurable chokes;
  - Color's harmonics and level;
  - Punch's attack-to-body ratio;
  - block-size and sample-rate safety;
  - every new control changes the sound.
- Benchmark scenarios with the Character section on.
- App icon: the wordmark's stacked bars on a cream key. It shows on the Standalone app's window and taskbar button, its shortcuts, the macOS Dock and the Windows installer.
- **Sequencer On** parameter (*SETTINGS → Internal sequencer on*), automatable. When it is off, the display shows `SEQ OFF` and only MIDI input and the pads play. It is the last parameter, so no existing index moves.
- *SETTINGS → Each instrument to its own output* and *All instruments to the main output*, so multi-out routing takes one click.
- *SETTINGS → MIDI note map*: the current map, with note names as Ableton Live and FL Studio show them.
- MIDI All Sound Off (CC 120) fades out every voice.
- A sample that is missing from its saved path is looked for by file name under `Documents/Floorplan/Samples`, including subfolders. *SETTINGS → Open samples folder* opens it.
- `scripts/install-vst3.ps1` copies a local build to the Windows VST3 folder.
- [docs/HOSTS.md](docs/HOSTS.md): installing, the sequencer, MIDI clips, automation, separate outputs, MIDI output and samples in FL Studio and Ableton Live. Not yet checked in either host.
- Ten plugin tests that drive the processor like a host. They cover:
  - MIDI mapping, velocity and note-offs;
  - All Sound Off;
  - preview and host hand-over;
  - invalid host timing and count-in positions;
  - the sequencer switch;
  - MIDI/sequencer duplicates;
  - fast and slow automation;
  - finding moved samples;
  - block sizes from 1 to 4096 samples and sample-rate changes;
  - every output bus layout.

### Changed

- New visual design language across the whole interface:
  - **Palette:** Primary orange `#FF5E00` (deep `#A33A00` for selections), Secondary charcoal `#403D39`, Tertiary grey `#8A857C` and Neutral cream `#EBE7DF`.
  - **Type:** Space Grotesk for headlines and the wordmark, Geist for text and labels. Both are embedded, under the SIL Open Font License.
  - **Shapes:** borderless rounded cards, near-square buttons in Outlined, Inverted, Primary and orange action styles, and flat Neutral step pads and track rows.
  - **Meters:** segmented LED meters on dark windows, with an L/R legend on the master meter.
  - Instrument badges use the palette: orange for the tonal voices, deep orange for snare and clap, and grey for the metals.
- The eight extra outputs are now active by default, so hosts that only offer default-active outputs list them. They stay silent until a voice is routed to them, and the main mix is unchanged.
- Output choices are named after their voice: *Out 2 (Kick)* … *Out 9 (Cymbal)*.
- Pressing play in the host ends a preview started with Floorplan's PLAY button. Before, the preview kept running underneath and resumed when the host stopped.
- A preview in a DAW uses the host's tempo instead of Floorplan's internal tempo.
- A MIDI or pad hit within 2 ms of a sequencer hit on the same drum replaces it (with the louder velocity) instead of playing twice.

### Fixed

- A host tempo of zero, a negative tempo, or a NaN or infinite position is now ignored instead of being used.
- A non-finite sample in the output resets the engine and mutes that block, so the host never receives NaN or infinity.
- MIDI CCs 120–127 (channel mode messages) are no longer MIDI-learned or mapped to knobs.
- A MIDI output note-off now uses the note that was sent. Before, changing the note map while a note was held could leave the note hanging in the receiving instrument.

## [0.1.2] - 2026-11-01

### Added

- Six Advanced parameters for the closed hat, open hat and cymbal, all automatable:
  - Attack and Hold: a fade-in of up to 500 ms and a hold at full level before the decay.
  - Tail: a second decay, four times slower, mixed in under the main one.
  - Detune: moves the six partials away from their fixed ratios.
  - High Cut: a low-pass on the whole voice, switched off at its maximum.
  - Stick: a short click of high-passed noise at the start of the hit.
- Six Advanced parameters for the percussion and low tom:
  - Attack and Hold, as on the kick.
  - Noise Tone and Noise Length: the noise transient's high-pass cutoff and decay, previously fixed at 1.5 kHz and 30 ms.
  - Metal Tune: the pitch of the Metal modulator, in semitones.
  - Shape: waveshapes the sine towards a square.
- Pitch/Env tab for the hats, cymbal, percussion and tom:
  - Hats and cymbal: the amplitude envelope, including the tail.
  - Percussion and tom: amplitude, noise and pitch.

### Changed

- Every voice now has Advanced controls. Projects and kits from earlier versions load the new parameters at defaults that sound like those voices did in 0.1.1. Tests compare each voice with a copy of the 0.1.1 code.
- The new parameters come after all existing ones, including the kick, snare and clap parameters of 0.1.1, so no existing parameter index changes.

## [0.1.1] - 2026-11-01

### Added

- Advanced Mode (ADVANCED in the top bar). The sound panel widens into a tabbed inspector (Source, Pitch/Env, Tone, Output) for the selected track, and the sequencer stays where it is. The mode is saved with the project and changes no sound or parameter.
- Five Advanced kick parameters, all automatable:
  - Attack: a 0–30 ms fade-in of the body.
  - Shape: waveshapes the sine body towards a square.
  - Sub: a clean sine at the tuned pitch with a longer tail.
  - Click Tone and Click Length: the click's high-pass cutoff and decay time, previously fixed at 1.8 kHz and 7 ms.
- Five Advanced snare parameters:
  - Drop and Drop Time: how far above its pitch the shell starts and how fast it settles. These were fixed at +50% and 6 ms.
  - Ring: the level of the shell's upper mode.
  - Wire Tone: a low-pass on the wires, switched off at its maximum.
  - Wire Delay: up to 20 ms between the shell and the wires.
- Four Advanced clap parameters:
  - Attack: how fast each hand hit rises. It was fixed at 0.4 ms.
  - Burst Length: scales the decay of each short hit.
  - Tail Level: the tail's peak relative to the hits.
  - Jitter: random spacing of the hits, different on every hit.
- Pitch/Env tab for the kick, snare and clap. It draws each voice's envelopes above their controls:
  - Kick: amplitude and pitch.
  - Snare: body, wires and pitch.
  - Clap: the burst train.
  The kick and snare curves use the same formulas as the sound engine. The clap curve runs the clap's own envelope code.

### Changed

- Swing moved from the groove panel to the top bar.
- New flat interface: a cream body, black outlined keys and step pads, one orange accent, and colour-coded track badges (yellow for tonal voices, red for noise, blue for metal).
- Step pads show velocity as fill height. The selected step gets an orange ring.
- Knobs show their value while dragging. Compact knobs show it on hover.
- Track modules hold mute, solo, an activity dot and an audition badge. The mixer strips keep pan, fader and meter.
- The minimum window size is now 75% of the design size (was 60%), so labels stay readable.
- Loading a project now sets any parameter it does not contain to its default. Before, such parameters kept whatever value they had. Projects and kits from 0.1.0 load the new kick, snare and clap parameters at defaults that sound like those voices did in 0.1.0. Tests compare each one with a copy of the 0.1.0 code.
- New parameters are added after all existing ones, so existing parameter indices do not change.
- Millisecond times in the voices now convert to whole samples by rounding instead of cutting off the fraction. This covers the kick's attack and hold, the clap's spread and the snare's wire delay. The result is the same on every compiler, and at most one sample away from 0.1.0.
- Linux is not supported for now: CI no longer builds or tests it, and the README lists Windows and macOS only.
- CI: failing test output now appears as annotations, which anyone can read without signing in.

## [0.1.0] - 2026-11-01

First public release.

### Added

- VST3 and Standalone builds for Windows, macOS and Linux (JUCE 8, CMake).
- Installers: a Windows setup (x64) and a macOS package (Apple silicon and Intel), each with a choice of VST3 plugin and Standalone app, plus a plain VST3 zip per platform. Nothing is code-signed yet.
- Eight synthesized voices: Kick, Snare, Closed Hat, Open Hat (choked by the closed hat), Clap, Percussion, Low Tom and Cymbal, each with six sound controls.
- Optional WAV/AIFF/FLAC/Ogg sample playback per voice, loaded off the audio thread. Missing samples are reported and fall back to synthesis.
- Per-voice channel strip: level, pan, mute, solo, bipolar filter, drive and output routing to eight extra stereo buses.
- Master section with volume, soft clipper, bus compressor and meters.
- Step sequencer with 16 patterns; lengths of 8 to 64 steps; five step resolutions; per-step velocity, probability, micro timing, accent, flam, ratchet, and tune and decay locks.
- Swing, six groove templates, quantized pattern switching and an eight-slot pattern chain.
- Pattern tools: copy, paste, clear, double, rotate, Euclidean generator, randomize, and 128-level undo/redo.
- Sample-accurate sync to the host transport (tempo changes, loops, jumps), plus an internal clock for the Standalone app and stopped hosts.
- MIDI note input with learnable note mapping, MIDI CC learn for every knob, and optional MIDI output of the sequence.
- 141 automatable parameters.
- Eight factory kits and nine factory patterns. User kits are saved to `Documents/Floorplan/Kits`.
- Pattern and bank import/export as documented JSON.
- Versioned plugin state, with migration from development-build states.
- Hardware-style interface: a moulded-plastic body with separate screwed-down module plates, extruded keys with travel, ribbed knobs, grooved faders, and LCD windows with 7-segment pattern/step and BPM readouts. Step LEDs show the playhead and voice LEDs flash on each hit.
- Resizable interface (60% to 200%) with keyboard pads, keyboard shortcuts and drag-and-drop for samples.
- 43 automated tests and a performance benchmark.

### Known limitations

- Not yet verified in third-party DAWs (see `docs/DAW_TESTING.md`).
- Synthesis parameters are applied on the next hit, not to a sound that is already playing. The channel strip and master respond continuously.
- Only tune and decay can be locked per step.
- All tracks in a pattern share one length.

[Unreleased]: https://github.com/Kuzuw/Floorplan/compare/v0.1.2...HEAD
[0.1.2]: https://github.com/Kuzuw/Floorplan/releases/tag/v0.1.2
[0.1.1]: https://github.com/Kuzuw/Floorplan/releases/tag/v0.1.1
[0.1.0]: https://github.com/Kuzuw/Floorplan/releases/tag/v0.1.0
