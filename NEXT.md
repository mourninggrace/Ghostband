# Where Ghostband stands, and what to do next

Last updated 2026-08-23, end of session 3. Everything described as done is
committed, pushed, installed, and covered by the harness.

## State

Public at <https://github.com/mourninggrace/Ghostband> under **AGPLv3**. Free,
with a Ko-fi donate button (`ko-fi.com/kyleyeroshefsky11806`). Not being sold —
see `COMMERCIAL.md`, which is retained for its licensing research but no longer
describes the plan.

**Working, in the user's Gig Performer rig:**

- Four parts — drums, bass, guitar, piano — driving SSD5, MODO Bass 2,
  UJAM IRON 2 and UJAM Virtual Pianist.
- Live section jumping, landing on the next bar line.
- Rerolling, whole song or ctrl-clicked sections only.
- In-plugin calibration: tune a drum map by ear, no MIDI knowledge needed.
- Song structure editor with save.
- Dark industrial UI, resizable, procedurally drawn.

**Build:** `Build.bat`. JUCE 8.0.15 is a pinned submodule; clone with
`--recursive`. The C++ runtime links statically, so the plugin has no
dependencies beyond Windows.

**Install:** `Install.bat`. It refuses to run while Gig Performer is open,
because a host holds the DLL and the copy would silently do nothing.

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

- **IRON 2**: notes 12–59 silent (chord zone), **60–89 phrase zone**, 90–120
  silent. Attack density and brightness rise with pitch, so low key = sparse,
  high key = busy. Measured on style *"120 bpm – Arm Pit"*.
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
