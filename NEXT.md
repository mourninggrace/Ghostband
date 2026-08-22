# Where Ghostband stands, and what to do next

Written 2026-08-22 at the end of the first build session. Everything described as
done is committed, building, and covered by the harness.

## Working today

- **Engine** — plan → groove → intent → profile → MIDI. No JUCE, no third-party
  code, no plugin names anywhere in the C++.
- **CLI** (`ghostband.exe`) — `render` and `calibrate`.
- **VST3** — installed and confirmed running in Gig Performer 5. Orange MIDI out
  pin present and wiring to SSD5 / MODO Bass 2 works.
- **Harness** (`ghostband_plugin_test`) — 32 checks, including CLI parity.

User has heard the demos and the plugin and signed off on the grooves.

## The one open musical problem: rerolling barely changes anything

**This is the next thing to work on.** The user's words: the changes from Roll
"seem to be so small they are almost non-existent."

### Why — this was diagnosed, not guessed

Auditing every `rng.` call in `Groove.cpp` shows the *skeleton* of a groove is
fully determined by intensity, style and feel, and only the *ornamentation* is
random:

| element | current state |
| --- | --- |
| snare placement | **no randomness at all** — purely `feel` + `beatsPerBar` |
| ride vs hats (`useRide`) | **no randomness** — a hard threshold on intensity |
| hat subdivision (`hatStep`) | **no randomness** — thresholds on intensity/complexity |
| kick pattern | only 2 options, and only inside some intensity bands |
| fill shape | **always the same** — open on snare, walk down the toms |
| bass pattern when `auto` | **deterministic** — always `lock_kick` for heavy styles |
| ghost notes, dead notes, jitter, crash choice | random ✓ |

So two rolls of the same section produce the same drum pattern with slightly
different ghost notes. That is exactly what "almost non-existent" sounds like.

### The plan

Give the *skeleton* real variety while keeping each section's identity. All of
this lives in `buildSectionGroove` / `generateDrumBar` / `generateBassBar`.

1. **Kick pools.** Replace the two hardcoded options with a pool of 4–6 genuinely
   different `(beat mask, cell)` pairs per intensity band. Express beat masks
   procedurally (`All`, `Even`, `FirstAndMid`, `AllButLast`, `FirstOnly`) so odd
   time signatures still work.
2. **Snare variants.** Add 3–4 backbeat treatments: plain; plain plus a pickup on
   the last 16th; plain plus the "and" of the last beat; ghost-heavy.
3. **Cymbals.** Randomise `useRide` and `hatStep` *within* sensible bands rather
   than switching on a hard threshold.
4. **Fill vocabulary.** Five shapes instead of one: tom descent, snare roll,
   alternating snare/tom, near-silence into a crash, tom pairs.
5. **Bass `auto`.** Weighted choice between `lock_kick`, `lock_kick_octave` and
   `eighths`, resolved **once per section** (in `Render.cpp`, using that
   section's RNG) so the part stays coherent across its bars.

A partial version of this was started and reverted to keep the tree coherent —
`SectionGroove` was to gain `snareVariant` and `fillShape`, plus a new
`chooseBassPattern (ctx, rng)`.

**Constraint:** every option must be idiomatic on its own. More variety is only
an improvement if every roll is still something a drummer would play.

## Other open items

- **Latching section loop.** Jumping to a section plays on into whatever follows.
  The user asked about clicking once to repeat a section until told otherwise.
  The jump-offset machinery already supports it; it needs a latch flag and UI.
- **Per-section reroll.** Currently Roll rerolls the whole song. Section seeds are
  already derived per section (`deriveSeed`), so this is mostly UI work.
- **The AI planner.** The remaining half of the agreed v1 brain: Claude writes the
  chart (chords, intensity curve, groove choices), the local engine renders it.
  The plan JSON is already the handoff format. Will need a background thread —
  the sequence swap is already built for it.
- **Audio pins.** Vestigial; they exist only so the plugin registers as an effect
  rather than a MIDI-effect. User asked about them and was told. Could be hidden
  by declaring the bus disabled-by-default, but that risks changing how GP
  enumerates a plugin that currently works. Do not churn this without a reason.
- **Profiles are still `[UNVERIFIED]`.** The SSD5 map is GM-derived and the MODO
  keyswitches are defaults, not confirmed. `ghostband calibrate` exists to settle
  this by ear; the user has not run it yet.
- **`mode` is not in the UI**, deliberately — it only affects sections with auto
  chords, so it would be a dead control on plans with written chords.

## Operational gotchas, all learned the hard way

- **Install with `Install.bat`, never PowerShell `Copy-Item -Recurse`.** Copy-Item
  onto an *existing* directory copies INTO it, giving
  `Ghostband.vst3\Ghostband.vst3\...` while the old DLL stays at top level. The
  install appears to succeed and changes nothing.
- **Close Gig Performer before installing.** It holds the DLL open.
  `Install.bat` refuses to run while `GigPerformer5.exe` is alive.
- **Never round or narrow a value that feeds the generator.** The RNG stream is
  chaotic; holding the dials as `std::atomic<float>` once made the plugin and the
  CLI produce different songs from the same seed. The harness guards this — pass
  the CLI's counts as trailing arguments.
- **Do not trust `getSampleRate()` blindly.** It returns 0 until the host sets it,
  and guessing a fallback plays the song at the wrong speed and drifts, which
  fails silently rather than loudly.
- **All Notes Off is not enough** to stop notes. Many instruments ignore it. The
  processor tracks sounding notes and releases them by name.

## Verifying a change

```bash
Build.bat
build\ghostband_plugin_test_artefacts\Release\ghostband_plugin_test.exe plans\demo-metal.json 1173 615
build\ghostband_plugin_test_artefacts\Release\ghostband_plugin_test.exe plans\demo-rock.json 868 319
```

If a change is *meant* to alter the generated notes, those two trailing counts
will change. Re-run the CLI on both plans, confirm the new numbers by ear, then
update them here and in the commands above.
