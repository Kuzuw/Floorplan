# Roadmap

The roadmap is a rough plan, not a promise. Items move as contributors pick
them up. Open an issue to discuss anything you want to work on.

## 0.1: foundation (released)

- [x] VST3 + Standalone, CMake, CI on Windows, macOS and Linux
- [x] Eight synthesized voices with sample replacement
- [x] Sample-accurate sequencer: 16 patterns, up to 64 steps, per-step velocity/probability/micro/accent/flam/ratchet/locks
- [x] Swing and groove templates, pattern chain, Euclidean generator, randomizer, undo/redo
- [x] Mixer with filter, drive, routing to 8 extra outputs; master clip and compressor
- [x] MIDI learn, note mapping, MIDI out
- [x] Kits, JSON pattern format, versioned state
- [x] Resizable hardware-style UI
- [x] Unit tests and benchmark

## 0.2: verification and polish

- [ ] Run the [DAW testing checklist](DAW_TESTING.md) in Reaper, Bitwig, Ableton Live, FL Studio, Cubase and Studio One, and publish the results
- [ ] Signed/notarized macOS builds and release packaging (installer or zip)
- [ ] Release workflow that attaches binaries to GitHub releases
- [ ] Accessibility: keyboard focus order, screen-reader titles for all controls
- [ ] Tooltips for every control, and a quick-start overlay

## 0.3: sequencing depth

- [ ] Per-track pattern length (polymeter) and per-track resolution
- [ ] More per-step locks: filter, drive, pan, level, and any synthesis parameter
- [ ] Conditional trigs (1st, not-1st, every N cycles, fill)
- [ ] Fill mode and pattern variations A/B
- [ ] Song mode / scene arrangement
- [ ] Drag a pattern out to the DAW as a MIDI clip

## 0.4: sound

- [ ] Layering: sample + synthesis on the same voice
- [ ] Sample start, reverse and pitch envelope
- [ ] Per-voice transient shaper and bit-crusher
- [ ] Optional mono-compatible stereo width on hats and clap
- [ ] More factory kits from contributors

## Formats

- [ ] CLAP (via clap-juce-extensions)
- [ ] AU (macOS), once a maintainer can test it in Logic
- [ ] LV2 (Linux)

## Explicitly out of scope

- Web-based or Electron UIs
- Built-in sample libraries large enough to bloat the repository
- Features that require network access
