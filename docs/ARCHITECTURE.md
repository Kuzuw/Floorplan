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
`kSnareExtras`, `kClapExtras`, `kMetalExtras`, `kToneExtras`, returned by
`extraParams()`). A voice receives
them in the same array, after its main six, so `numVoiceParams()` and
`voiceParamSpec()` cover both. Their defaults must leave the sound unchanged,
because projects and kits saved earlier do not contain them; tests compare each
voice at its defaults with a copy of its earlier code. `envelopePage()` lists
the controls of an engine's Pitch/Env tab in display order, and `paramPage()`
says which tab shows each parameter.

The Pitch/Env tab draws `describeEnvelopes()` (`Voices.cpp`): each voice
computes its curves from the same formulas as its DSP (kick, snare, hats,
cymbal, perc, tom) or runs its own envelope code (the clap's `ClapEnvelope`).

`ParameterLayout.cpp` turns the table into an `AudioProcessorValueTreeState`
layout of 199 parameters. The extras sit in an "Advanced" group after all the
original parameters, so adding them never moves an existing parameter's index.
Within that group they are ordered by `extrasGeneration()`: each new batch of
extras goes after the ones already released, even when it belongs to a voice
listed earlier. Parameters added later still go in groups at the very end, in
the order they were added: "Options" (*Sequencer On*), then "Character" (its
five controls), then "Choke" (one *Choked By* per voice). `ParameterCache` resolves every parameter's raw
`std::atomic<float>*` once at construction, so the audio thread reads plain
atomics and never looks anything up by string.

### `DSP/`: building blocks

- `SineTable`: 2048-point wavetable with linear interpolation, shared by every voice.
- `DSPCore.h`: `Svf` (topology-preserving transform state-variable filter, stable under fast modulation), `OnePoleHp`, `BlepSquare` (PolyBLEP anti-aliased square), xorshift `Noise`, `softClip` (rational, smooth, bounded to ±1), `LinearRamp` for click-free gain changes, and exponential `decayCoef` helpers.
- `Compressor.h`: a feed-forward bus compressor. The envelope detector runs on every sample, and the gain computer (log/exp) runs every 16 samples, which is cheap and smooth enough for a drum bus.
- `Character.h`: the master bus Character stage, Punch (a transient shaper comparing 5 ms and 20 ms power averages) then Color (an asymmetric soft clipper with a DC blocker, crossfaded in parallel). Its settings ramp across each block; once switched off and faded out it is skipped entirely. See [CHARACTER.md](CHARACTER.md).

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
- **Choke groups.** Each voice's `chokedBy` names the voice whose hits cut it off with a fast, smooth fade. By default only the closed hat chokes the open hat.
- **Per-hit tone.** `TriggerInfo::tone` (1 = as set) scales the parts of each engine that make it bright, through `toneScale(depth)`, which is exactly 1 at tone 1. The Character section lowers it for soft hits and varies it per hit; when the section is off it stays 1 and every voice is bit-identical to before.

### `Audio/DrumEngine`

`DrumEngine::process` renders one block:

1. Events (already sorted by sample offset) split the block into sub-blocks; each event triggers its voice exactly at its offset. With the Character section on, `applyCharacter` first adjusts the hit's tone, tune, decay and level from its velocity and a hash of (seed, `hitId`, voice). The sequencer gives every hit an id from its absolute step, voice and repeat; the processor gives MIDI hits one from their song position, so renders repeat exactly.
2. Each active voice renders into a scratch buffer, then goes through its strip: **drive → bipolar filter → level/pan ramps → bus**. The bus is the main mix or one of eight aux outputs. Output routing falls back to the main mix when the host has the aux bus disabled. The aux buses are active by default (so every host lists them) and stay silent until a voice's Output parameter selects one.
3. The master chain runs: **volume ramp → Character (Punch, Color) → compressor → soft clip**, followed by peak metering. The aux buses bypass the master chain.

`chokeAll()` fades out every voice (MIDI All Sound Off). After the engine, the
processor checks the output's bit patterns for NaN or infinity: every main-bus
sample and the last sample of every other channel. If it finds one, it resets
the engine and mutes that block. The check uses bit patterns because fast-math
can optimise `std::isfinite` away.

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

The look lives in `LookAndFeel.h/.cpp`. The `colours` namespace holds the
palette tokens: Primary orange, Secondary charcoal, Tertiary grey, Neutral
cream and their shades. `font()`, `monoFont()`, `labelFont()` and
`headlineFont()` return the embedded Geist and Space Grotesk faces (binary data
target `floorplan_fonts`). `KeyStyle` picks one of the button styles: Outlined,
Inverted, the bright orange action, or Primary. Components draw with these
helpers instead of hard-coded colours or fonts.

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
2. If a host violates its own announced maximum block size and also gives the main bus only one channel, the mono fallback buffer is resized once. Normal hosts never do this.

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
involved, before going to the engine. A sequencer event within 2 ms of a MIDI
or GUI event for the same voice is dropped, and the live event keeps the higher
velocity. This way a clip recorded from the MIDI output and played along with
the pattern does not double the hits. The **Sequencer On** parameter takes the
sequencer out entirely.

**Transport source** (`readTransport`): a host that is playing always drives
the sequencer, and the moment it starts, it ends a preview started with the
editor's PLAY button. Otherwise the internal clock runs while the preview is
on, at the host's tempo when there is a play head, or at *Internal Tempo* in
the Standalone app. A non-finite or non-positive tempo, or a non-finite
position, counts as missing. Floorplan then falls back to its last valid tempo
and counts samples itself.

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
- Samples are stored by path only. If the path no longer exists, `findMissingSample` searches `Documents/Floorplan/Samples` recursively for the same file name, accepting either path separator. A file that is still missing is flagged in the UI, the voice falls back to synthesis, and the saved path is kept, so a project always loads and never loses the reference.
- MIDI output note-offs remember their note number, so a note-map change while a note is held cannot leave it hanging.

## Adding a voice parameter or a new voice

1. Add or adjust the entry in `kVoiceSpecs` (`ParameterSpecs.h`), or add an Advanced Mode parameter to the engine's extras table with a default that keeps the old sound. Never reuse or rename an existing ID.
2. Read the new parameter index in the engine class under `Instruments/`.
3. If the parameter count changes, update the expected count in `parameters_are_automatable`.
4. Add a test in `CoreTests.cpp`: the output must be finite and audible, and must decay to silence at both extremes of the new parameter.
5. Run `floorplan_bench` before and after.
