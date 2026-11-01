# Changelog

All notable changes to this project are documented in this file. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Advanced Mode (ADVANCED in the top bar). The sound panel widens into a tabbed inspector (Source, Tone, Output) for the selected track, and the sequencer stays where it is. The mode is saved with the project and changes no sound or parameter.

### Changed

- Swing moved from the groove panel to the top bar.
- New flat interface: a cream body, black outlined keys and step pads, one orange accent, and colour-coded track badges (yellow for tonal voices, red for noise, blue for metal).
- Step pads show velocity as fill height. The selected step gets an orange ring.
- Knobs show their value while dragging. Compact knobs show it on hover.
- Track modules hold mute, solo, an activity dot and an audition badge. The mixer strips keep pan, fader and meter.
- The minimum window size is now 75% of the design size (was 60%), so labels stay readable.

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

[Unreleased]: https://github.com/Kuzuw/Floorplan/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/Kuzuw/Floorplan/releases/tag/v0.1.0
