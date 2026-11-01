# Development log

A short record of how 0.1 was built, what was verified, and what is still open.
Newest entries go on top.

## 2026-11-01: 0.1.0

### Environment

- Windows 11 Home (10.0.26200), Intel Core i7-9750H @ 2.6 GHz
- Visual Studio 2026 (generator "Visual Studio 18 2026"), MSVC 19.50 (toolset 14.50.35717), CMake 4.1.2 (bundled with VS)
- JUCE 8.0.16 (`8.0.16-security` tag) as a git submodule

### Decisions

- **JUCE + CMake.** JUCE is the most widely used, best-tested way to ship VST3 and Standalone from one C++ codebase. The license works for a GPL project under its AGPLv3 option (see `THIRD_PARTY_NOTICES.md`).
- **JUCE-free core.** The DSP, sequencer and engine form a plain C++17 library. Tests and benchmarks compile in seconds and need no GUI.
- **Synthesis first, samples optional.** No bundled audio, so the repository stays tiny, and every factory kit is just parameter values.
- **Parameters latched at trigger time** for synthesis controls. This is cheaper, and it is how most drum machines behave. The channel strip stays continuous for automation and mixing.
- **Whole-bank triple buffer** instead of fine-grained lock-free edits. A bank is about 64 KB, and copying it on each UI edit is negligible. In exchange, the audio thread always sees a consistent pattern.
- **Half-sample-offset PPQ windows.** The first version rounded events down, which put a step at sample 5999 instead of 6000 and gave different results for different buffer sizes. Moving the window by half a sample and rounding to the nearest sample made the output block-size invariant (`sequencer_is_block_size_invariant`).
- **Fixed design size, scaled UI.** One layout at 1280×800 scaled by an affine transform keeps all proportions exact from 60% to 200%. It avoids a second, responsive layout to maintain.

### Verified

- Release build of VST3 and Standalone with **zero compiler warnings** (`/W4`).
- **43/43 tests pass** (`ctest -C Release`): 33 core tests and 10 plugin tests.
- Standalone app launches and stays responsive (about 87 MB working set).
- The UI screenshot in the README is rendered from the real editor (`floorplan_plugin_tests --screenshot`).
- Benchmark (`floorplan_bench`, 60 s of audio per scenario): typical groove 0.32% of one core at 48 kHz/128, worst case 1.98% at 48 kHz/128 and 3.64% at 96 kHz/256.

### Not verified yet

- **No third-party DAW has been tested.** See `docs/DAW_TESTING.md`.
- macOS and Linux builds are configured in CI but have not been built on a local machine.
- No audio-device listening test of the Standalone build was recorded. The audio path is covered by the automated tests only.

### Bugs found along the way

- Off-by-one-sample step placement (see above).
- The mute/solo test heard the tail of a previously soloed voice. The test now uses a fresh engine.
- The click-free retrigger test counted the kick's deliberate click transient. It now runs with Click at 0.
- The randomizer at intensity 0 could drop downbeats. Strong positions now always fire.
- Step-menu item IDs for velocity and tune overlapped. They now use fixed ID bases.
- The plugin bus layout check used a compile-time Standalone flag, which is wrong in a multi-format build. It is now a runtime host check.

### Known limitations

- `MidiBuffer::addEvent` may allocate if a host gives a MIDI buffer with no reserved space (MIDI out only, off by default).
- Undo covers pattern edits only. Sound changes use the host's parameter undo.
- Only tune and decay can be locked per step, and all tracks share one length.
- No AU or CLAP build yet.

### Next steps

See [ROADMAP.md](ROADMAP.md). The first priority is real-host testing.
