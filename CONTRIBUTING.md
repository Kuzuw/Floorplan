# Contributing to Floorplan

Thanks for helping out! Floorplan is a small project, so a focused pull request
is the fastest way to get a change in. This page explains how to build, the
rules the code follows, and what reviewers look for.

## Ways to help

- **Test it in your DAW.** Follow [docs/DAW_TESTING.md](docs/DAW_TESTING.md) and open an issue with the results. Good and bad reports are both useful.
- **Report bugs.** Use the bug report template. Include the host, OS, plugin format, buffer size and sample rate, and steps to reproduce.
- **Sound design.** Better factory kits and patterns are very welcome. They live in `Source/Presets/FactoryContent.cpp`.
- **Code.** Check the [roadmap](docs/ROADMAP.md) and the open issues. For anything bigger than a bug fix, open an issue first so we can agree on the approach.

## Building

See [Building from source](README.md#building-from-source) in the README. For
day-to-day work on the DSP or sequencer, you can skip JUCE entirely:

```bash
cmake -S . -B build-core -DFLOORPLAN_BUILD_PLUGIN=OFF
```

```bash
cmake --build build-core --config Release
```

```bash
ctest --test-dir build-core -C Release --output-on-failure
```

## Ground rules for code

### Real-time safety is not negotiable

Anything reachable from `FloorplanProcessor::processBlock` runs on the audio
thread and must **never**:

- allocate or free memory (`new`, `std::vector::push_back`, `juce::String`, `std::function` captures, …)
- take a lock or wait on another thread
- do file, network or console I/O, including logging
- call into the GUI

Communicate with the audio thread through the existing mechanisms: atomics,
`TripleBuffer`, `juce::AbstractFifo` queues, or the `SampleSlots` hand-off. The
threading model is described in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
A pull request that breaks these rules will not be merged, however useful the
feature is.

### Keep the core independent

`Source/DSP`, `Source/Instruments`, `Source/Sequencer`, `Source/Audio` and
`Source/Parameters/ParameterSpecs.h` must not include JUCE. They are built as
the `floorplan_core` library, tested without JUCE, and benchmarked.

### Style

- C++17. Follow the style of the surrounding code: 4-space indent, braces on their own line, `camelCase` functions and variables, `PascalCase` types, `kConstant` constants.
- The `.clang-format` file describes the layout. You don't have to reformat old code, but new code should follow it.
- The build must be warning-free on MSVC (`/W4`), GCC and Clang (`-Wall -Wextra`).
- Comments explain *why*, not *what*.
- Prefer simple, readable DSP over clever micro-optimizations, unless `floorplan_bench` shows a measurable win.

### Parameters and state

- Parameters are defined once in `ParameterSpecs.h` / `ParameterLayout.cpp`. **Never rename or remove a parameter ID**, because that breaks existing projects and automation. If a parameter becomes obsolete, keep it and hide it.
- Changing the meaning of saved data requires bumping `kStateVersion` (plugin state) or `kPatternFormatVersion` (pattern JSON), plus a migration path and a test that loads the old format.

### Tests

- New DSP or sequencer behavior needs a test in `Tests/CoreTests.cpp`. Processor or state changes need one in `Tests/PluginTests.cpp`.
- Register new tests in `Tests/CMakeLists.txt` so CTest picks them up.
- If your change touches the audio path, run `floorplan_bench` before and after and put both numbers in the pull request.

## Pull requests

1. Fork the repository and create a branch from `main`.
2. Keep commits focused, and use imperative commit messages ("Add per-step filter lock").
3. Make sure the build is warning-free and `ctest` passes.
4. Update `CHANGELOG.md` under **Unreleased** and any docs your change affects.
5. Open the pull request and fill in the template.

By contributing, you agree that your contributions are licensed under the
project's license (GPL-3.0-or-later).

## Code of conduct

Everyone taking part in this project is expected to follow the
[Code of Conduct](CODE_OF_CONDUCT.md).
