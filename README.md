# Ghostband

A MIDI brain that plays *your* instrument plugins to build full songs.

Ghostband does not make any sound of its own. It writes an arrangement and
performs it through the instruments you already own — SSD5 for drums, MODO Bass 2
for bass — by sending them MIDI. The eventual form is a VST3 that sits in a Gig
Performer rackspace with its MIDI out wired to each instrument. This repository is
the stage before that: a command-line renderer that writes the same arrangement to
a `.mid` file, so the musical engine can be judged by ear in Reaper long before any
plugin scaffolding exists.

**Status:** v1, drums and bass only. Rock and metal. No guitars, no keys, no live
following — those build on this engine rather than replacing it.

Two things get built from the same engine:

- **`build\bin\ghostband.exe`** — the CLI renderer, for judging grooves in Reaper.
- **`build\GhostbandPlugin_artefacts\Release\VST3\Ghostband.vst3`** — the plugin,
  for playing them live in Gig Performer.

They are held to producing byte-identical output; see *Determinism* below.

## Build

Needs CMake 3.20+ and an MSVC toolchain. JUCE 8.x is not vendored — the build
points at the Polygraph checkout by default, so it works with no network access.
Override with `-DGHOSTBAND_JUCE_PATH=<path>`.

```bash
cd C:\Projects\Ghostband && Build.bat
```

## The plugin

Copy `Ghostband.vst3` into your VST3 folder, then in a Gig Performer rackspace:

1. Add Ghostband and your instruments. **It ships with a plan built in**, so it
   has a song ready the moment it loads — no file needed to hear it work.
2. In the wiring view, drag from Ghostband's **orange MIDI output pin** to the
   MIDI input of SSD5 and MODO Bass 2. One output feeds many inputs.
3. Start the transport.

Drums go out on channel 10 and bass on channel 1 by default — both set by the
driver profiles, not hardcoded. Ghostband never touches audio; it passes through
untouched so the audio pins can be left unwired.

Note that a host holds the plugin DLL open while it is loaded, so **close the
host before reinstalling** or the copy will silently leave the old build behind.

### The controls

- **Load plan...** — swap in a plan file. Its own profiles come with it.
- **Reload** — re-read the current plan from disk. Edit the JSON in a text
  editor, hit Reload, hear it. Returns to the built-in plan if no file is loaded.
- **Key** — transposes the whole song. It moves written chords, not just
  generated ones; a key control that only affected auto progressions would
  silently do nothing on most plans.
- **Style / Bass tuning** — change how the band plays and how low it sits.
- **Roll** — new seed, regenerate. Same chords, same structure, same section
  lengths; different drumming. Which kick pattern, where the fills land, how many
  ghost and dead notes. It is a different take by the same band, not a different
  song.
- **Complexity / Humanize** — regenerate on their own a moment after you stop
  moving them. There is no Generate button to remember.

The section list lights up and a playhead line tracks the song as it plays, so
you can see which section you are hearing.

### Jumping sections live

**Click any section to go there.** The jump is queued — the clicked section is
marked NEXT — and lands on the next bar line, so the transition stays in time
rather than lurching mid-beat. Clicking the section already playing restarts it
at the next bar, which is how you hold a chorus for another eight bars.

Every sounding note is released by name at the seam. Relying on All Notes Off
alone is not enough: it is a controller message and many instruments ignore it,
which left a note hanging across the jump until the harness caught it.

### Why there is no tempo control

The host owns the tempo. Ghostband reads it from the transport every block and
displays it, but does not set it — set it in Gig Performer. A BPM knob here would
be a dead control that looks live. Note that the plan's `bpm` field is only used
by the CLI when writing a MIDI file; generation itself is tempo-independent,
because everything is expressed in ticks.

### Why there are audio pins

There is a stereo audio in and out, and they do nothing useful. They exist
because declaring an audio bus is what makes the plugin register as a normal
effect rather than a MIDI-effect, which is what makes hosts place it sensibly.
Audio wired in passes through untouched; audio is never *routed* through
Ghostband, because a VST3 cannot see its sibling plugins' output. Wire your
instruments straight to the audio out and leave these unconnected.

They are also the hook for a later feature — a plugin that can hear what it is
producing could check its own output — but today they are vestigial.

### Windows DPI

The plugin is built with `JUCE_WIN_PER_MONITOR_DPI_AWARE=0`. Without it, dragging
the editor to a monitor with different scaling left every control dead — the
window drew correctly but hit-testing used the wrong scale factor, so clicks
landed nowhere. The host owns the window, so the host should own the scaling.

## Use

```bash
build\bin\ghostband.exe render plans\demo-metal.json -o out\demo-metal.mid
```

Then in Reaper: import the `.mid`, route the drum track to SSD5 and the bass track
to MODO Bass 2. Section names appear as project markers.

Options:

| flag | meaning |
| --- | --- |
| `-o, --out <file>` | output path (default: `<plan name>.mid`) |
| `--seed <n>` | override the plan's seed without editing it |
| `--drums <file>` | drum driver profile |
| `--bass <file>` | bass driver profile |
| `--tuning <name>` | `standard`, `drop_d`, `drop_c`, `b_standard` |

## Calibration — read this before trusting the output

The SSD5 and MODO Bass 2 profiles that ship here are **derived, not verified**.
The drum map uses the General MIDI positions SSD5's default mapping follows, and
the MODO keyswitches are sensible defaults rather than confirmed assignments. The
CLI says `[UNVERIFIED]` on every run for exactly that reason.

To check them by ear:

```bash
build\bin\ghostband.exe calibrate -o out\calibration.mid
```

That file plays every mapped drum voice and every bass articulation in turn, with
a project marker naming what *should* be sounding underneath it. Anything that
does not match, fix in the profile JSON. It is a text edit, not a rebuild.

## The plan file

You own the skeleton — key, tempo, style, and the full section order. Everything
left on `auto` is what the engine decides (and later, what the AI planner decides).

### Song level

| field | values |
| --- | --- |
| `key` / `mode` | `E`, `F#`… / `natural_minor`, `harmonic_minor`, `phrygian`, `phrygian_dominant`, `dorian`, `mixolydian`, `major` |
| `bpm` | 20–300 |
| `time_signature` | `[4,4]`, `[7,8]`, `[5,4]`… |
| `style` | `hard_rock`, `metal`, `thrash`, `groove_metal`, `doom`, `sludge`, `punk`, `prog_metal`, `alt_rock` |
| `bass_tuning` | `standard`, `drop_d`, `drop_c`, `b_standard` |
| `play_style` | `pick`, `finger`, `slap` |
| `complexity` | 0–1: fills, ghost notes, dead notes |
| `humanize` | 0–1: timing and velocity looseness |
| `seed` | any integer — same seed always gives the same song |
| `ending` | `hard_stop`, `ritard`, `cymbal_ring`, `fade` |

### Section level

| field | values |
| --- | --- |
| `name` | `verse1`, `chorus2`… the role is inferred from the name |
| `bars` | length |
| `intensity` | 0–1: the single biggest lever on how the section behaves |
| `feel` | `straight`, `half_time`, `double_time`, `blast` |
| `chords` | `["Em","Em","C","D"]` — a shorter list repeats |
| `bass` | `auto`, `lock_kick`, `lock_kick_octave`, `eighths`, `sixteenths`, `roots` |
| `fill` | `auto`, `none`, `small`, `big` |
| `plays` | `full`, `drums`, `bass`, `none` |
| `vary` | `false` makes a repeated section bit-identical to its sibling |

## Architecture

Three layers, and the boundaries between them are the point.

```
SongPlan  ──▶  Groove  ──▶  Intent  ──▶  Profile  ──▶  MIDI
 (yours)      (musical)   (abstract)   (per-plugin)
```

**Nothing about SSD5 or MODO Bass exists in the C++ source.** The generators emit
abstract intent — `kick, accent 0.8`, `bass root, palm-muted` — and a JSON driver
profile is the only thing that turns that into note numbers, keyswitches and CCs
for a specific instrument. Adding a plugin is a data file. Adding a new *role*
(guitars, keys) is engine work.

Profiles support `"inherits"`, so the five SSD5 kits share one map and state only
what differs.

### The kick/bass lock

Rock and metal live or die on whether the kick and the bass agree. Ghostband does
not generate them separately and hope: `BarGrid.kickOnsets` is built once per bar
and **both** the drum generator and the bass generator read it. They lock because
they share one source of truth.

Measured on the demo renders: metal lands 92.5% of bass attacks within 12 ticks of
a kick, averaging 2.5 ticks *ahead* of it — the small push that makes a gallop feel
urgent. Rock comes in at 53%, correctly lower, because its choruses are written to
pump straight eighths against the kick rather than mirror it.

### Determinism

The PRNG is hand-rolled xorshift32 rather than `<random>`, because the standard
distributions are not specified to give identical sequences across implementations
and a seed has to mean the same thing forever. Same plan plus same seed always
produces the same song. Section seeds are derived, so re-rolling one section
cannot disturb another.

This is more fragile than it looks, and it is worth knowing why. The generator's
RNG stream is chaotic: flip one comparison and every draw after it changes, so the
arrangement diverges completely. The plugin once held its complexity and humanize
dials as `float`; rounding 0.6 to 0.60000002 was enough to send the plugin and the
CLI down different branches and produce different songs from the same seed. They
are `double` now. **Anything that feeds a generator parameter must preserve the
exact value** — no float round-trips, no rounding for display.

### Testing

`ghostband_plugin_test` instantiates the processor with a fake playhead and walks
the whole song block by block, checking what actually leaves `processBlock`:
notes balanced, nothing emitted twice, nothing outside its block, all-notes-off on
stop, and the seed behaving.

```bash
build\ghostband_plugin_test_artefacts\Release\ghostband_plugin_test.exe plans\demo-metal.json 1173 615
```

The two trailing numbers are the drum and bass counts the CLI prints for that
plan. Passing them makes the harness hold the plugin to CLI parity — the check
that caught the float bug above.

It also caught the plugin trusting `getSampleRate()` when the host had not set it
yet: the old code fell back to 44100, which does not fail loudly, it just plays
the song at the wrong speed and drifts further out of step every block. It now
emits silence until the rate is known.

## What is deliberately not here yet

- Guitars and keys (new roles, real engine work)
- The AI planner — the plan format is already the handoff point for it
- Live following, chord detection
- The VST3 wrapper. The engine has no JUCE dependency and no audio-thread
  assumptions specifically so that wrapping it later is mechanical.
- A control lane for CC/OSC, for driving plugins that expose only audio pins and
  have to be steered through Gig Performer widgets instead of notes. The profile
  format is shaped to take it without a rewrite.
