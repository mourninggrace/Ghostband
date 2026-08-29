# Where Ghostband stands, and what to do next

Last updated 2026-08-24, end of session 4. Everything described as done is
committed, pushed, installed, and covered by the harness.

## Session 4 — what changed, and what is still open

The user tested and reported three faults, all real and all mine:

1. **Guitar and piano silent.** `loadBuiltInPlan` never called `resolveProfiles`,
   so profiles were never loaded and those parts were never enabled; and the
   built-in plan named no profiles at all. Fixed, and profiles now ship inside
   the VST3 bundle so the built-in song works on any machine.
2. **The guitar cut out after a fraction of a second.** Phrase keys must be held,
   not tapped — measured at 3% sustain for a 50ms press. Fixed.
3. **Calibrate showed only drums.** Same root cause as (1); it now covers every
   part the song has.

The user then switched IRON 2 and Virtual Pianist to **Instrument mode**, which
needs a different profile from Player mode. Instrument mode is now the default.

**Still open, in the user's words:**

- The interface has been redesigned flat in red / purple / black / silver /
  white, per their direction, but they have not yet said whether it lands.
- **Settings and About are a first pass.** Settings has per-part MIDI channel,
  reload profiles and reset window size. About has version, author, description,
  licence and a manual button. They asked for "the usual stuff" — expect gaps.
- **The user manual PDF does not exist.** Agreed to write later; the About
  button falls back to the README until it does.
- Whether Player mode now sounds better than Instrument mode is untested.

## State

Public at <https://github.com/mourninggrace/Ghostband> under **AGPLv3**. Free,
with a Ko-fi donate button (`ko-fi.com/kyleyeroshefsky11806`). Not being sold —
see `COMMERCIAL.md`, which is retained for its licensing research but no longer
describes the plan.

**Working, in the user's Gig Performer rig:**

- Four parts — drums, bass, guitar, piano — driving SSD5, MODO Bass 2,
  UJAM IRON 2 and UJAM Virtual Pianist (both in **Instrument mode**).
- Live section jumping, landing on the next bar line.
- Rerolling, whole song or ctrl-clicked sections only.
- In-plugin calibration covering every part, tuned by ear.
- Song structure editor with save.
- Per-part level knobs, sent as MIDI CC 7.
- An unlimited control mapping table per instrument, built in Settings and
  saved into the driver profile. Each entry is named by whoever made it, and
  is a **knob** (sweeps), a **switch** (on or off) or a **select** (holds one
  of N choices for the section). Teach sweeps the CC so the instrument's MIDI
  Learn can latch onto it.
- Five screens: song, calibrate, edit, settings, about.
- Flat UI in red / purple / black / silver / white, resizable, drawn
  procedurally so it scales across mixed-DPI monitors.

**The rig needs one MIDI Channel Constrainer per instrument** (drums 10, bass 1,
guitar 2, piano 3), because instrument plugins are omni. Channels are now also
settable in the plugin's Settings screen.

**Build:** `Build.bat`. JUCE 8.0.15 is a pinned submodule; clone with
`--recursive`. The C++ runtime links statically, so the plugin has no
dependencies beyond Windows.

**Install:** `Install.bat`. It refuses to run while Gig Performer is open,
because a host holds the DLL and the copy would silently do nothing.

**Driver profiles carry the findings.** The comments in `profiles/*.json` record
what was measured rather than assumed - IRON 2 sounds from 60 to 89 and is
silent below it, which is why the chord zone is 60-84 and not a guitar's range.
Saving a mapping from the plugin splices only the controls block, so those notes
survive; do not replace that with a whole-file rewrite.

## The only remaining planned item

**The AI planner.** Claude writes the *chart* — chords, intensity curve, groove
choices, section shape — and the local engine renders it, exactly as agreed in
session 1. Decisions already made:

- **The user supplies their own API key.** Ghostband is donation-ware, so a
  per-generation cost against no revenue makes no sense.
- **It must remain entirely optional.** Everything works today with no key and
  no network; `autoProgression` already writes progressions locally. The planner
  raises musical sophistication, it is not a dependency, and playback must never
  wait on a network call.
- It needs a background thread. The sequence swap in the processor was built for
  this and is already safe for it.

## Waiting on the user

They are testing the current build and reporting back. The two things most likely
to have rough edges, because they are newest and least exercised:

1. **The structure editor loop** — Edit song, rearrange, rename, change chords,
   Save as. Entirely untested by anyone but the harness.
2. **How the redesign actually reads** on their monitors.

## Verifying a change

```bash
Build.bat
build\ghostband_plugin_test_artefacts\Release\ghostband_plugin_test.exe plans\demo-metal.json 1231 629
build\ghostband_plugin_test_artefacts\Release\ghostband_plugin_test.exe plans\demo-rock.json 996 423
```

Add `--snapshot <dir>` to render the editor to PNGs. Every other check is blind
to layout; rendering it caught three real bugs the first time it ran.

If a change is *meant* to alter the generated notes, those trailing counts will
change. Re-render all three demo plans, confirm by ear, then update the numbers
here and in the README.

## Things that will bite again if forgotten

- **Never ask this user to verify a MIDI map by ear against a stopwatch.** It was
  tried; they have no DAW and it was an unreasonable ask. Build measurement tools
  instead — `ghostband_probe` and the in-plugin Calibrate exist for this.
- **Install with `Install.bat`, never PowerShell `Copy-Item -Recurse`.** Copy-Item
  onto an existing directory copies *into* it, leaving the old DLL in place while
  appearing to succeed.
- **Never round or narrow a value that feeds the generator.** The RNG stream is
  chaotic; holding the dials as `float` once made the plugin and CLI produce
  different songs from the same seed.
- **Do not trust `getSampleRate()` blindly.** It returns 0 until the host sets it,
  and guessing a fallback plays at the wrong speed and drifts silently.
- **All Notes Off is not enough** to stop notes — many instruments ignore it. The
  processor tracks what is sounding and releases each note by name.
- **Instrument plugins are omni.** MIDI-channel separation does nothing on its
  own; the user's rig uses Gig Performer's MIDI Channel Constrainer, one per
  instrument. Do not resurrect the multi-instance workaround.
- **`ghostband_probe` cannot make SSD5 or MODO sound** — almost certainly
  licensing. Ruled out headless format, message pumping, and editor creation. It
  does work on UJAM plugins.
- **Reference counts in the docs go stale** whenever seed derivation changes.
  That has already caused one false parity failure.

## Measured facts worth keeping

- **UJAM instruments have two modes and each needs its own profile.** In
  *Instrument* mode they play the notes they are sent; in *Player* mode you hold
  a chord in a low zone and hold a phrase key. Using the wrong profile sounds
  broken rather than merely wrong. Instrument mode is the default here; the
  measured Player mapping is kept in `vg-iron2-player.json`.
- **Phrase keys must be HELD, not tapped.** Measured: pressing one for ~50ms
  gives 3% sustain and the sound stops after 0.17s. This was reported as "the
  guitar cuts out" and it was real. Note how it was missed first time — the
  earlier sustain test held the key for the whole measurement, so it answered
  "does this key keep playing" rather than "does Ghostband hold it long enough".
  **When a test passes and the user still reports a fault, check the test is
  asking the user's question.**
- **IRON 2** (player mode): notes 12–59 silent (chord zone), **60–89 phrase
  zone**, 90–120 silent. Density by attacks over 5s: key 79 = 1, 73 = 2, 70 = 4,
  67 = 7, 88 = 16. The first mapping was guessed from pitch and had it backwards.
  Measured on style *"120 bpm – Arm Pit"*.
- **Virtual Pianist** could not be mapped by probe — output ~5× quieter, no
  phrase signature, boundaries moving between runs, AssetManager errors on load.
  It is driven as a plain piano instead, which is the safe failure mode.
- **MODO Bass 2** exposes 2081 parameters named `MIDI CC 0|n` — a complete CC
  map, which will make its articulation profile straightforward.
- **The kick/bass lock** sits at 89–93% of bass attacks within 12 ticks of a
  kick, averaging 2.5 ticks ahead. This is the thing that makes it sound like a
  band; check it survives any change to groove generation.

## Ideas raised but not scheduled

- **"Connect it to any plugin and it just works."** Reachable. Generic profiles
  already cover GM-compatible drum plugins and ordinary pitched instruments; a
  known map is a small JSON file; and auto-mapping by measurement is proven on
  UJAM. The blocker for licensed plugins is that they will not sound in a
  headless host — the answer is the vestigial audio *input* pins: route an
  instrument's audio back into Ghostband and it can measure its own output from
  inside the host, where everything is licensed and working.
- **Latching section loop** — click once and a section repeats until told
  otherwise. The jump-offset machinery already supports it.
- **Live following** — Ghostband comping behind what the user plays. The largest
  unbuilt idea, and the one that would need chord detection and tempo tracking.
