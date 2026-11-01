# Pattern file format

Floorplan exports patterns as plain JSON so you can version them, share them,
edit them by hand, or generate them from scripts.

| Extension | Content | Menu |
| --- | --- | --- |
| `.fppattern` | One pattern | *SETTINGS → Import pattern… / Export pattern…* |
| `.fpbank` | All 16 pattern slots | *SETTINGS → Import bank… / Export bank…* |

Files are UTF-8 JSON. The current format version is **1**.

## Pattern

```json
{
  "format": "floorplan-pattern",
  "version": 1,
  "length": 16,
  "tracks": {
    "kick":  [ { "i": 0 }, { "i": 4 }, { "i": 8 }, { "i": 12 } ],
    "snare": [],
    "chh":   [ { "i": 2, "v": 90 }, { "i": 6, "v": 90, "r": 2 }, { "i": 10, "v": 90 }, { "i": 14, "v": 90, "p": 60 } ],
    "ohh":   [ { "i": 2, "v": 110, "m": 8 } ],
    "clap":  [ { "i": 4, "a": true }, { "i": 12, "a": true, "f": true } ],
    "perc":  [ { "i": 7, "t": 7, "d": -40 } ],
    "tom":   [],
    "cym":   []
  }
}
```

### Top-level fields

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `format` | string | yes | Must be `"floorplan-pattern"` |
| `version` | integer | yes | Format version. Files with a version newer than the plugin understands are rejected rather than misread. |
| `length` | integer | no (16) | Pattern length in steps, 1–64. The UI offers 8, 12, 16, 24, 32 and 64. |
| `tracks` | object | yes | One array per voice, keyed by voice ID |

### Voice IDs

| ID | Voice |
| --- | --- |
| `kick` | Kick |
| `snare` | Snare |
| `chh` | Closed Hat |
| `ohh` | Open Hat |
| `clap` | Clap |
| `perc` | Percussion |
| `tom` | Low Tom |
| `cym` | Cymbal |

A missing voice means an empty track. Unknown keys are ignored.

### Step objects

Each entry in a track array is an active step. Steps that are off are simply
not listed. Every field except `i` is optional, and the default is used when
it is absent. Floorplan writes only the non-default fields.

| Key | Type | Range | Default | Meaning |
| --- | --- | --- | --- | --- |
| `i` | integer | 0–63 | (required) | Step index |
| `v` | integer | 1–127 | 100 | Velocity |
| `p` | integer | 0–100 | 100 | Probability in percent |
| `m` | integer | −50–50 | 0 | Micro timing in percent of a step (negative = early) |
| `a` | boolean | | false | Accent (adds the global *Accent* amount) |
| `f` | boolean | | false | Flam (half-velocity grace note, then the full hit 12% of a step later) |
| `r` | integer | 1–4 | 1 | Ratchet: number of evenly spaced hits within the step |
| `t` | integer | −24–24 | 0 | Tune lock in semitones, added to the voice's tune |
| `d` | integer | −100–100 | 0 | Decay lock: multiplies the decay time by 2^(d/50), so −100 is ¼× and +100 is 4× |

## Bank

```json
{
  "format": "floorplan-bank",
  "version": 1,
  "patterns": [ { "format": "floorplan-pattern", "version": 1, "length": 16, "tracks": { } }, … ]
}
```

`patterns` holds up to 16 pattern objects, in slot order. Extra entries are
ignored, and missing or invalid entries leave that slot empty.

## Validation

Import is defensive. Malformed JSON, a wrong `format`, or a newer `version`
rejects the whole file with an error message. Inside a valid file, every
number is clamped to its range and out-of-range step indices are dropped, so no
file can put the sequencer into an invalid state. This is covered by the
`pattern_json_round_trip` and `pattern_json_rejects_garbage` tests.

## Compatibility promise

- Readers must ignore unknown fields. New optional fields can be added without bumping `version`.
- `version` is bumped only when an existing field changes meaning. Newer Floorplan versions keep reading all older versions.

## Plugin state

Patterns are also embedded in the plugin state your DAW saves. The `PATTERNS`
element contains exactly this bank JSON. See
[ARCHITECTURE.md](ARCHITECTURE.md#state-and-versioning).
