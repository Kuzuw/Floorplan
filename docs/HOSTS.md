# Using Floorplan in FL Studio and Ableton Live

Floorplan is one standard VST3 instrument (category *Instrument | Drum*) with
MIDI input, a stereo main output and eight optional stereo outputs. FL Studio,
Ableton Live and every other host load the same plugin and the same code; there
are no host-specific builds or modes.

> **Status:** nothing on this page has been checked in FL Studio or Ableton
> Live yet. The behaviour described below is what the plugin does and what the
> automated tests check against a simulated host. The host menu names come from
> the hosts' documentation and may differ between versions. Please run the
> [checklist](DAW_TESTING.md) and report what you find.

- [Installing](#installing)
- [The internal sequencer in a DAW](#the-internal-sequencer-in-a-daw)
- [Playing Floorplan from MIDI clips](#playing-floorplan-from-midi-clips)
- [Automation](#automation)
- [Separate outputs](#separate-outputs)
- [MIDI output](#midi-output)
- [Saving projects and samples](#saving-projects-and-samples)
- [Known limitations](#known-limitations)

## Installing

Use the installer from the [Releases](https://github.com/Kuzuw/Floorplan/releases)
page, or install your own build:

```powershell
cmake --build build --config Release
# In an administrator PowerShell, from the repository root:
powershell -ExecutionPolicy Bypass -File scripts\install-vst3.ps1
```

The script copies `Floorplan.vst3` to `C:\Program Files\Common Files\VST3\`,
replacing an older copy. Close any host that has Floorplan loaded first. Pass
`-Destination <folder>` to install somewhere else.

### FL Studio

1. *Options → Manage plugins*. `C:\Program Files\Common Files\VST3` is scanned by default.
2. Click *Find installed plugins* (or *Find more plugins* / *Rescan*, depending on the version).
3. Find **Floorplan** under *Installed → Generators → VST3* and star it so it appears in the Channel Rack's *Add* menu.
4. In the Channel Rack, click **+** and choose **Floorplan**.

If it does not appear: check that the scan log in the Plugin Manager lists
`Floorplan.vst3` without an error, that the bundle contains
`Contents\x86_64-win\Floorplan.vst3`, and that you use the 64-bit FL Studio.

### Ableton Live

1. *Settings (Preferences) → Plug-Ins*. Turn on **Use VST3 Plug-In System Folders**.
2. Click **Rescan** (hold `Alt` while clicking for a full rescan if Floorplan was installed before).
3. In the browser, open *Plug-Ins → VST3 → Floorplan Contributors → Floorplan*.
4. Drag it onto a MIDI track (or double-click it with a MIDI track selected).

If it does not appear, look in Live's `Log.txt` for a line mentioning
`Floorplan.vst3`, and check that the file is in the system VST3 folder above.

## The internal sequencer in a DAW

Floorplan's own step sequencer is meant to be the main way to use it. You do
not need any MIDI clips: load Floorplan, press play in the host, and the
pattern plays.

| Situation | What Floorplan does |
| --- | --- |
| Host playing | The sequencer follows the host's tempo and song position, sample-accurately. Patterns are aligned to the start of the song (beat 0), so starting at bar 5 beat 3 plays exactly what would be heard there. |
| Host loops, seeks or restarts | It re-aligns at the new position. Steps before the new position are not played late, and no step plays twice. |
| Tempo changes or tempo automation | Followed block by block. |
| Host stopped | Sequencing stops at once; sounds already playing ring out. |
| **PLAY** (or `Space`) while the host is stopped | *Preview*: the pattern plays on Floorplan's own clock at the host's tempo. |
| Host starts during a preview | The preview ends and the host takes over from its own position, so the pattern never plays twice. When the host stops again, Floorplan stops. |
| Host sends no usable timing (no tempo, no position, NaN) | Floorplan uses its last valid tempo (or 120 BPM) and counts samples itself, so it keeps playing in time with itself. |
| Positions before zero (count-in or pre-roll) | Played on the same grid, so beat −1 sounds like the last beat of a bar. |

**Sequencer on/off:** *SETTINGS → Internal sequencer on*, also the automatable
parameter **Sequencer On**. When it is off the display shows `SEQ OFF`, and
only MIDI input and the pads make sound. Use this when you program drums only
with MIDI clips.

Pattern selection (**Pattern**) and the chain are parameters too. A pattern
change waits for the end of the current pattern. Swing, groove, micro timing,
probability, ratchets and flams are calculated from the song position. This
means a bounce or export sounds exactly like real-time playback, and
probability steps give the same result on every pass for the same
**Variation Seed**.

## Playing Floorplan from MIDI clips

Each voice listens to one MIDI note on any channel. The defaults are the
General MIDI drum notes. Hosts name octaves differently: Ableton Live calls
note 60 *C3*, FL Studio calls it *C5*.

| Voice | Note | Ableton Live | FL Studio |
| --- | --- | --- | --- |
| Kick | 36 | C1 | C3 |
| Percussion | 37 | C#1 | C#3 |
| Snare | 38 | D1 | D3 |
| Clap | 39 | D#1 | D#3 |
| Low Tom | 41 | F1 | F3 |
| Closed Hat | 42 | F#1 | F#3 |
| Open Hat | 46 | A#1 | A#3 |
| Cymbal | 49 | C#2 | C#4 |

*SETTINGS → MIDI note map* shows the current map with both names. To change a
note, right-click an instrument, choose *Learn MIDI note* and play the new
note. *SETTINGS → Reset MIDI note map* restores the defaults. The map is saved
with the project.

- **FL Studio:** open the Floorplan channel's Piano Roll and draw notes on the rows listed above (*C3* = kick).
- **Ableton Live:** create a MIDI clip on the Floorplan track. In the clip editor, the *Fold* button shows only rows that have notes.

How notes behave:

- **Velocity** sets the level of each hit (and the accent amount for sequencer accents).
- **Note length does not matter.** Every voice is a one-shot: note-offs are ignored, so a note can never hang, and short and long notes sound the same.
- **Repeated or overlapping notes** on the same drum retrigger it, with a short crossfade against clicks. Different drums play together freely.
- **All Sound Off (CC 120)** fades out every voice within a few milliseconds. **All Notes Off (CC 123)**, which hosts send on stop, changes nothing, because no note is held.
- **MIDI and the sequencer together:** both play. If a MIDI note and a sequencer step hit the same drum within 2 ms, Floorplan plays it once, at the louder velocity. This covers playing back a clip recorded from Floorplan's MIDI output while the pattern still runs. Hits further apart than 2 ms both play.

**Ableton Drum Rack:** Floorplan is a complete kit, so put it directly on a
MIDI track rather than inside a Drum Rack pad. A pad passes on only its own
note (C1 by default), so Floorplan inside a pad would only play the kick.
Floorplan has no special Drum Rack integration.

## Automation

All 199 parameters are standard VST3 parameters with stable IDs, readable
names (for example *Kick Tune*, *Snare Level*, *Swing*) and units. Automation
changes the sound engine directly. The editor follows automation, and moving
a knob records automation through the usual begin/end gesture, without
feedback loops.

- Level, pan, filter, drive, master volume, clip and compressor are smoothed every block, so fast automation does not click.
- Synthesis parameters (tune, decay, envelope stages and so on) apply from the next hit, the way drum machines latch a sound when it is triggered.
- **FL Studio:** move a knob, then *Tools → Last tweaked → Create automation clip*. Or arm recording with automation enabled and move knobs during playback.
- **Ableton Live:** Live shows a plugin's parameters automatically only up to a limit, which Floorplan exceeds. Click the device's **Configure** button and move the knobs you want, so they appear in the device panel and the automation choosers. Or arm automation and move knobs while recording.

Any knob can also be mapped to a MIDI CC (right-click → *MIDI Learn*).

## Separate outputs

The plugin always has a stereo **Main** output carrying everything that is not
routed elsewhere. It also has eight stereo outputs, *Out 2 (Kick)* to *Out 9
(Cymbal)*, active by default and silent until used.

1. In Floorplan, choose where each voice goes. Either *SETTINGS → Each instrument to its own output (Out 2–9)*, or each voice's **OUTPUT** box on the Advanced *Output* tab. The routing is saved with the project.
2. A routed voice leaves the main mix and plays only on its output, with its level, pan, filter and drive. If the host has disabled that output, the voice falls back to Main, so it is never lost or doubled.
3. Master volume, clip and compressor apply to the main output only.

**FL Studio:** open the plugin wrapper's settings (the gear icon), then the
*Processing* tab. Under *Output ports*, give each output its own Mixer track
(or use the auto-map option where your version has it). Then route Floorplan
on the Channel Rack to the Mixer track for *Main*.

**Ableton Live:** create an audio track for each output. Set its *Audio From*
to the Floorplan track, then choose the output (for example *Out 3 (Snare)*)
in the chooser below, and set *Monitor* to *In*. The Floorplan track itself
carries *Main*.

To go back to a single stereo mix: *SETTINGS → All instruments to the main output*.

## MIDI output

*SETTINGS → Send sequencer notes to MIDI output* sends every sequencer hit as a
note on channel 10. The note number comes from the note map, the velocity from
the step (+10 for accents), and each note lasts 50 ms. Notes are sent at the
same sample as the sound. MIDI input is not echoed.

- **Ableton Live:** on another MIDI track, set *MIDI From* to the Floorplan track, arm it and record. This captures the pattern as a clip.
- **FL Studio:** support for MIDI output from VST3 plugins depends on the version. In the wrapper's settings, set a MIDI output port and give a receiving instrument the same input port.

MIDI output has not been checked in either host. If you record the output and
play the clip back into Floorplan while the sequencer runs, the 2 ms rule
above prevents doubled hits. Usually you want to switch the sequencer off
then.

## Saving projects and samples

The host saves Floorplan's state inside the project. The state is versioned
and includes:

- all parameters, mixer settings and the Advanced Mode controls;
- all 16 patterns with every step's velocity, probability, micro timing, ratchet, flam, accent and locks;
- pattern selection and chain, swing and groove, the variation seed;
- the MIDI note map, MIDI CC mappings and output routing;
- the sample file references.

Projects from older Floorplan versions load. Parameters they do not contain
get their defaults, which sound like the older version did.

**Samples are stored by file path, never as audio.** FL Studio's and Ableton
Live's *collect files* features do not know about them. When a project opens
and a sample is no longer at its saved path, Floorplan looks for a file with
the same name anywhere under `Documents\Floorplan\Samples` (*SETTINGS → Open
samples folder*). Keep your Floorplan samples there and projects find them
again after a move or on another computer. A sample that is not found is
marked missing in the editor, the voice plays its synthesized sound, and the
saved path is kept, so the reference is not lost.

Sound presets (kits, *Documents\Floorplan\Kits*) and patterns (exported
`.fppattern` / `.fpbank` files) are separate. Loading a kit never changes your
patterns.

## Known limitations

- **No DAW testing yet.** Neither FL Studio nor Ableton Live has been run with this build. Treat every host-specific step above as unverified until someone reports results.
- **Bars and time signatures:** at 1/16 resolution a 16-step pattern covers four beats from the start of the song. In 4/4 that is one bar. In other time signatures, patterns line up with the song start, not with each bar.
- **Synthesis parameters latch at each hit.** Automating a voice's tune or decay changes the next hit, not one already sounding.
- **Only tune and decay can be locked per step.**
- **Stop behaviour is fixed:** sequencing always stops with the host. There is no "keep running when the host stops" option.
- **The 2 ms duplicate rule works within one audio block.** A sequencer hit and a MIDI note that fall on either side of a block boundary can both play.
- **Separate outputs** are active by default so that hosts can list them. A host that shows every active output may show nine stereo outputs on the track even when none is used.
