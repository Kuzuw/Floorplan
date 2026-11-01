# Architecture

This document explains how Floorplan is put together: the modules, the threads,
who owns which data, and how a step in the grid becomes a sample in the output
buffer. Read it before changing anything in the audio path.

## Overview

```
                ┌──────────────────────── message thread ────────────────────────┐
                │  UI components ── PatternStore (model + undo) ── PresetManager │
                │       │                  │ publish()                SampleLoader│
                │       │ APVTS            ▼                              │       │
                │       │ params    TripleBuffer<PatternBank>     SampleSlots     │
                └───────┼──────────────────┼──────────────────────────┼─────────┘
                        │ atomics          │ acquire (wait-free)      │ atomic ptr swap
                ┌───────▼──────────────────▼──────────────────────────▼─────────┐
 host play head │  FloorplanProcessor::processBlock                             │
 MIDI in ──────►│    readTransport → Sequencer::process → merge MIDI/GUI hits   │
                │    → DrumEngine::process → main bus + aux buses ──────────────┼──► audio out
                │    → writeMidiOutput ─────────────────────────────────────────┼──► MIDI out
                └──────────────────────────── audio thread ─────────────────────┘
```

The code is split into two layers:

| Layer | Directories | Depends on |
| --- | --- | --- |
| **Core** (`floorplan_core` static library) | `DSP/`, `Instruments/`, `Sequencer/`, `Audio/`, `Parameters/ParameterSpecs.h` | C++17 standard library only |
| **Plugin** | `Parameters/ParameterLayout.*`, `Presets/`, `UI/`, `PluginProcessor.*`, `PluginEditor.*` | JUCE |

Keeping the core free of JUCE means the DSP and sequencer can be unit-tested and
benchmarked with a plain compiler in seconds (`-DFLOORPLAN_BUILD_PLUGIN=OFF`),
and the plugin layer stays a thin adapter.

## Modules

### `Parameters/ParameterSpecs.h`: the single source of truth

A `constexpr` table describes every voice (`kVoiceSpecs`): its ID, display name,
synthesis engine, default MIDI note, and six synthesis parameters with ranges,
defaults, skew and units. Both the JUCE parameter layout and the DSP read this
table, so adding a parameter to a voice is one edit. Per-engine index
namespaces (`KickP::Tune`, `SnareP::Snappy`, …) make the DSP code readable
without string lookups.

An engine can add extra parameters for Advanced Mode (`kKickExtras`,
`kSnareExtras`, `kClapExtras`, returned by `extraParams()`). A voice receives
them in the same array, after its main six, so `numVoiceParams()` and
`voiceParamSpec()` cover both. Their defaults must leave the sound unchanged,
because projects and kits saved earlier do not contain them; tests compare each
voice at its defaults with a copy of its earlier code. `envelopePage()` lists
the controls of an engine's Pitch/Env tab in display order, and `paramPage()`
says which tab shows each parameter.

The Pitch/Env tab draws `describeEnvelopes()` (`Voices.cpp`): each voice
computes its curves from the same formulas as its DSP (kick, snare) or runs
its own envelope code (the clap's `ClapEnvelope`).

`ParameterLayout.cpp` turns the table into an `AudioProcessorValueTreeState`
layout of 155 parameters. The extras sit in a last "Advanced" group, so adding
them never moves an existing parameter's index. `ParameterCache` resolves every parameter's raw
`std::atomic<float>*` once at construction, so the audio thread reads plain
atomics and never looks anything up by string.

### `DSP/`: building blocks

- `SineTable`: 2048-point wavetable with linear interpolation, shared by every voice.
- `DSPCore.h`: `Svf` (topology-preserving transform state-variable filter, stable under fast modulation), `OnePoleHp`, `BlepSquare` (PolyBLEP anti-aliased square), xorshift `Noise`, `softClip` (rational, smooth, bounded to ±1), `LinearRamp` for click-free gain changes, and exponential `decayCoef` helpers.
- `Compressor.h`: a feed-forward bus compressor. The envelope detector runs on every sample, and the gain computer (log/exp) runs every 16 samples, which is cheap and smooth enough for a drum bus.

### `Instruments/`: voices

`DrumVoice` is the abstract base class: `trigger(params, info)`, `choke()`,
`render(L, R, n)`, `isActive()`. Five engines implement it:

| Engine | Voices | Synthesis |
| --- | --- | --- |
| `KickVoice` | Kick | Sine body with exponential pitch envelope (Bend, Bend Time), hold stage (Punch), noise and sine click |
| `SnareVoice` | Snare | Two detuned sine modes (shell) plus high-passed noise through an SVF (wires) |
| `ClapVoice` | Clap | Band-passed noise; multi-burst envelope (Bursts, Spread) and a diffuse tail |
| `MetalVoice` | Closed Hat, Open Hat, Cymbal | Six PolyBLEP squares at inharmonic ratios, ring-summed with noise, band- and high-passed |
| `ToneVoice` | Percussion, Low Tom | Sine with pitch sweep, an FM "metal" operator and a noise transient |
| `SampleVoice` | any voice with a sample loaded | Linear-interpolated playback with tune and decay envelope |

Design rules for voices:

- **Synthesis parameters are latched on trigger.** Every hit reads its own parameter snapshot, so automation never zips mid-hit and the voice stays branch-light. The channel strip (level, pan, filter, drive) is continuous.
- **Click-free retrigger.** On a retrigger, the last output sample becomes a short decaying offset (`declick`), so a re-hit never jumps.
- **Idle voices cost nothing.** `isActive()` goes false once the envelope falls below −80 dB, and the engine skips the voice and its strip entirely.
- **Choke groups.** The closed hat chokes the open hat with a fast, smooth fade.

### `Audio/DrumEngine`

`DrumEngine::process` renders one block:

1. Events (already sorted by sample offset) split the block into sub-blocks; each event triggers its voice exactly at its offset.
2. Each active voice renders into a scratch buffer, then goes through its strip: **drive → bipolar filter → level/pan ramps → bus**. The bus is the main mix or one of eight aux outputs. Output routing falls back to the main mix when the host has the aux bus disabled.
3. The master chain runs: **volume ramp → compressor → soft clip**, followed by peak metering.

All buffers are sized in `prepare()`. If a host sends a block larger than it
announced, the engine processes it in chunks rather than allocating.

`SampleSlots` hands sample data from the message thread to the audio thread.
See [Threading](#threading-and-ownership).

### `Sequencer/`

- `Pattern.h`: plain-old-data model. A `Step` is 8 bytes (on, velocity, probability, micro, flags, ratchet, tune lock, decay lock). A `Track` holds 64 steps, a `Pattern` holds a length plus 8 tracks, and a `PatternBank` holds 16 patterns (about 64 KB, trivially copyable). Algorithms work on this model directly: `euclidean`, `rotate`, `doubleLength`, `randomiseTrack`, `sanitise`.
- `Sequencer.h`: converts a transport window into `TriggerEvent`s. See [Timing model](#timing-model).

### `Presets/`

- `PatternStore`: the message-thread pattern model, plus 128 levels of undo/redo. Every edit publishes a fresh copy of the bank to the audio thread through a `TripleBuffer`.
- `PatternSerializer`: JSON import/export ([format](PATTERN_FORMAT.md)). Every value is clamped on import, so a hand-edited or malicious file cannot put the sequencer in an invalid state.
- `PresetManager`: kit presets (sound parameters only, never patterns). Factory kits live in code; user kits are `.fpkit` XML files in `Documents/Floorplan/Kits`.
- `FactoryContent`: factory kits and patterns. Patterns are written in a tiny text DSL (`"x...x...x...x..."`), so they stay readable and diff-friendly.
- `SampleLoader`: decodes WAV/AIFF/FLAC/Ogg files of up to 30 s, mono or stereo, into memory on the message thread. `SampleVoice` corrects for the file's sample rate during playback.

### `UI/`

One `MainComponent` is laid out at a fixed design size of 1280×800 and scaled
with an `AffineTransform` by the editor, so every element scales uniformly from
60% to 200%. A single 30 Hz `Timer` polls the processor's atomics (meters, play
position, trigger counters) and repaints only what changed. UI components never
call into the audio thread. They write parameters through the APVTS, edit
patterns through `PatternStore`, and queue auditions through a FIFO.

## Threading and ownership

| Data | Owner / writer | Reader(s) | Mechanism |
| --- | --- | --- | --- |
| Parameter values | Host or UI (any thread) | Audio thread | `std::atomic<float>` inside the APVTS, read through `ParameterCache` |
| Pattern bank | Message thread (`PatternStore`) | Audio thread | Wait-free `TripleBuffer<PatternBank>`: the writer publishes a full copy, the reader grabs the newest at the start of each block |
| Undo history | Message thread | Message thread | `CriticalSection`, held only on non-audio threads, because a host may call `getStateInformation` from a worker thread |
| Sample data | Message thread (`SampleLoader`) | Audio thread | `SampleSlots`: per-voice atomic pointers *pending → active → retired*. The audio thread swaps in pending data at block start and parks the old pointer in *retired*. The message thread frees retired data in its timer. The audio thread never frees memory. |
| GUI auditions | Message thread | Audio thread | `AbstractFifo` of `TriggerEvent` (128 entries) |
| MIDI-learn results, CC values | Audio thread | Message thread | `AbstractFifo` of small POD messages (512 entries), drained by the processor's 20 Hz timer, which then sets parameters with proper gestures |
| Note map, CC map, learn state | Message thread | Audio thread | `std::atomic<int>` arrays |
| Meters, play position, BPM | Audio thread | UI | Relaxed atomics, polled at 30 Hz |
| Engine, sequencer, scratch buffers | Audio thread | Nobody else | Single owner, set up in `prepareToPlay` |

### Audio-thread rules

Everything reachable from `processBlock`:

- performs no heap allocation or deallocation
- takes no locks and makes no blocking calls
- does no file, network or console I/O, and no logging
- runs in bounded time (at most 512 events per block, 8 voices)

There are two narrow exceptions, both outside normal operation:

1. `juce::MidiBuffer::addEvent` (MIDI output only) can allocate if the host hands over a MIDI buffer with too little reserved capacity. Most hosts pre-allocate. The MIDI output is off by default.
2. If a host violates its own announced maximum block size, the mono fallback buffer is resized once. Normal hosts never do this.

## Timing model

The sequencer never uses wall-clock time. Each block, it receives a
`TransportState` (playing, PPQ position at the first sample, BPM, sample rate,
block length) from either the host play head or the internal clock.

1. **Window.** The block covers the PPQ interval `[start, end)` with `start = ppq − ½·samplePpq` and `end = start + n·samplePpq`. Shifting the window back by half a sample makes consecutive windows exactly contiguous. Each step falls into exactly one block, and is placed on the *nearest* sample (`llround`) rather than always rounding down. The result is identical whatever the buffer size, which the test `sequencer_is_block_size_invariant` verifies.
2. **Step grid.** Each step's nominal position is `stepIndex × stepPpq`. At straight resolutions, swing delays odd steps by `(2 × swing − 1) × stepPpq`, so 50% is straight and 75% delays by half a step. Groove templates and micro timing add per-step offsets of at most ±½ step.
3. **Pattern position.** The step index within the pattern is derived from the song position: `hostStep mod length`. If the transport jumps (a loop, seek or restart, detected as a deviation of more than ¼ step from the expected position), the sequencer resynchronizes to the song position instead of continuing its own counter. Looped playback therefore always lines up with the bar.
4. **Ratchets** split the step into 2–4 evenly spaced hits, with the repeats at 85% velocity. A **flam** plays a half-velocity grace note on the step, followed by the full hit 12% of a step later.
5. **Probability** is decided by a deterministic hash of (absolute step index, voice, seed). The same song position always gives the same result, so offline bounces match real-time playback, and changing the *Variation* seed gives a new but repeatable variation.
6. **Pattern switching.** A new pattern selection takes effect at the next pattern-cycle boundary. With chain mode on, the cycle index selects the chain slot.

Events from the sequencer, MIDI input and GUI auditions are merged and
insertion-sorted by offset, which is cheap for the small, nearly sorted lists
involved, before going to the engine.

## State and versioning

`getStateInformation` writes an XML document:

```xml
<FLOORPLAN stateVersion="1" pluginVersion="0.1.0">
  <PARAMS> … APVTS parameter tree … </PARAMS>
  <PATTERNS>{ "format": "floorplan-bank", "version": 1, … }</PATTERNS>
  <SAMPLES><SAMPLE voice="kick" path="C:/Samples/kick.wav"/></SAMPLES>
  <MIDI kick_note="36" snare_note="38" …><CC cc="74" param="kick_tune"/></MIDI>
</FLOORPLAN>
```

- Unknown elements are ignored. A parameter missing from the saved state is set to its default (not left at its current value), so newer and older versions degrade gracefully.
- `stateVersion` is bumped whenever the meaning of saved data changes. `setStateInformation` then migrates. Version 0 (a bare APVTS tree from early builds) is still accepted.
- Samples are stored by path only. A missing file is flagged in the UI and the voice falls back to synthesis, so a project always loads.

## Adding a voice parameter or a new voice

1. Add or adjust the entry in `kVoiceSpecs` (`ParameterSpecs.h`), or add an Advanced Mode parameter to the engine's extras table with a default that keeps the old sound. Never reuse or rename an existing ID.
2. Read the new parameter index in the engine class under `Instruments/`.
3. If the parameter count changes, update the expected count in `parameters_are_automatable`.
4. Add a test in `CoreTests.cpp`: the output must be finite and audible, and must decay to silence at both extremes of the new parameter.
5. Run `floorplan_bench` before and after.
