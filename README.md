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

## Build

Needs CMake 3.20+ and an MSVC toolchain.

```bash
cd C:\Projects\Ghostband && Build.bat
```

The executable lands in `build\bin\ghostband.exe`.

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

## What is deliberately not here yet

- Guitars and keys (new roles, real engine work)
- The AI planner — the plan format is already the handoff point for it
- Live following, chord detection
- The VST3 wrapper. The engine has no JUCE dependency and no audio-thread
  assumptions specifically so that wrapping it later is mechanical.
- A control lane for CC/OSC, for driving plugins that expose only audio pins and
  have to be steered through Gig Performer widgets instead of notes. The profile
  format is shaped to take it without a rewrite.
