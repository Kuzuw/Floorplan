# The Character section

The Character section makes the drum machine sound less like a sample player
and more like an instrument: soft hits are darker, no two hits are exactly
alike, and the master bus can add saturation and shape attacks. It is off by
default. Off, the output is bit-identical to a build without the section
(checked against a reference render made before the section existed).

Open it from the mixer: **MIXER / CHARACTER** in the mixer's title row. The
**ON** switch and the four knobs are ordinary parameters, so they are
automatable and saved with the project and with kit presets. A small orange
dot beside **MASTER** shows that the section is on, on either page.

This is a generic design. It is not modelled on, and does not claim to
emulate, any particular drum machine or circuit.

## Controls

| Control | Range (default) | What it does |
| --- | --- | --- |
| ON | off / on (off) | Switches the whole section. Off is clean and bit-identical. It fades in and out over one block, so switching never clicks. |
| Color | 0–100% (30%) | Parallel saturation on the main bus. |
| Punch | −100% to +100% (0%) | Transient shaper on the main bus. Right boosts attacks, left softens them. |
| Vary | 0–100% (30%) | Small per-hit differences in tune, decay, level and tone. |
| Vel Tone | 0–100% (50%) | How much softer hits are also duller. |

The Groove section's **VARIATION** seed also chooses which variation Vary
uses, as it already did for step probabilities.

### Vel Tone: velocity changes the timbre

Each hit gets a *tone* value: 1 at full velocity, lower for soft hits
(`tone = 1 − VelTone × (1 − velocity)`). Each engine uses it on the parts that
make it bright:

| Engine | What a soft hit changes |
| --- | --- |
| Kick | Less pitch sweep and a quieter click |
| Snare | Less wire noise and a lower wire high-pass |
| Clap | Lower band-pass centre |
| Metal (hats, cymbal) | Darker partials and less stick noise |
| Tone (percussion, tom) | Less noise and less FM "metal" |

Loaded samples ignore it (their level still follows velocity).

The unit test `velocity_changes_the_tone` checks that a hit at velocity 0.3 is
measurably darker than one at 1.0 for the kick, snare, clap, closed hat and
percussion, and that velocity only changes the level when the section is off.

### Vary: bounded, repeatable variation

At 100%, each hit is moved by at most:

| | Limit |
| --- | --- |
| Tune | ±0.2 semitone |
| Decay | ±12% |
| Level | ±1 dB |
| Tone | ±0.15 |

The amount scales linearly with Vary. The values come from a hash of the
seed, the hit and the voice, not from a running random generator, so:

- a sequencer hit is identified by its step number counted from the start of
  the song, its voice and its repeat (ratchet or flam). Consecutive bars
  differ, but a given bar sounds the same on every playback, render and
  bounce, like step probability.
- a MIDI hit while the host plays is identified by its song position, so a
  clip renders the same way each time.
- pads and MIDI with the transport stopped take the next hit number, so they
  vary but are not repeatable.

### Color: saturation

The signal goes through a soft clipper with a small DC offset at its input.
The offset makes the curve asymmetric, so it adds even harmonics as well as
odd ones. A DC blocker removes the offset afterwards. The curve never has a
gain above 1, so Color never makes anything louder: quiet signals pass at
about −0.3 dB and peaks are rounded off. Color crossfades from the clean
signal to the saturated one.

### Punch: transient shaping

The stereo power is averaged over 5 ms and over 20 ms. While the 5 ms average
is above the 20 ms one, the level is rising, which means an attack. During an
attack the gain moves toward up to +6 dB (Punch +100%) or −6 dB
(Punch −100%). Sustained and decaying sound is not changed. The gain is
smoothed over 0.7 ms.

## Choke groups

Every voice has a **Choked By** selector on the Advanced Mode **OUTPUT** tab.
A hit on the chosen voice cuts this one off with a short fade. The default is
the classic pairing: the open hat is choked by the closed hat, and nothing
else is choked. *None* lets the open hat ring. Any pairing is possible, for
example a cymbal cut by the kick. A voice set to choke itself is ignored,
since a retrigger already restarts it. Projects saved before this setting
existed load with the defaults, so they sound as before.

## Measurements

Demo render: 4 bars at 124 BPM of the default pattern plus one MIDI hit per
voice, 48 kHz, made with `floorplan_plugin_tests --render <wav> [id=value...]`.

| Settings | RMS | Peak |
| --- | ---: | ---: |
| Character off (reference) | −16.59 dBFS | −2.69 dBFS |
| On, defaults | −16.82 dBFS | −3.03 dBFS |
| On, Color 100%, no Vary or Vel Tone | −17.57 dBFS | −4.08 dBFS |
| On, Color 100%, Punch 60%, Vary 100%, Vel Tone 100% | −16.87 dBFS | −3.64 dBFS |
| Master clip off, Character off | −16.94 dBFS | −1.07 dBFS |
| Master clip off, Punch +100% only | −14.88 dBFS | +2.39 dBFS |
| Master clip off, Punch −100% only | −18.29 dBFS | −1.91 dBFS |

CPU (`floorplan_bench`, same machine and run, µs per block):

| Scenario | Off | Character fully on |
| --- | ---: | ---: |
| Typical groove, 48 kHz, 128 | 7.2 | 11.2 |
| Dense worst case, 48 kHz, 128 | 41.3 | 45.7 |
| Dense worst case, 96 kHz, 256 | 78.8 | 82.9 |

With the section off, the cost is the same as before it existed, within the
benchmark's run-to-run noise.

## Limitations

- Color and Punch work on the main bus only. Voices routed to a separate
  output get Vel Tone and Vary, but not Color or Punch.
- Punch +100% can push peaks above 0 dBFS when the master soft clipper is off
  (+2.4 dBFS in the demo render). The clipper is on by default.
- Negative Punch also lowers very short sounds, such as closed hats, because
  almost all of such a sound is attack.
- Synthesis parameters, including the Vel Tone and Vary changes, are latched
  when a hit starts, as before.
- Per-step parameter locks still cover only tune and decay, and all tracks
  share one pattern length.
- Not yet checked in a DAW. The automated tests drive the processor the way a
  host does, but no host has been run.
