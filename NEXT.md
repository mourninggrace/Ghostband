# Where Ghostband stands, and what to do next

Last updated 2026-09-03, end of session 7. Everything described as done is
committed, pushed, installed, and covered by the harness.

## START HERE

**The band plays.** Drums, bass, guitar and piano all sound, in every preset,
with the articulations working and the mix knobs driving three of the four
instruments' own volumes. That was not true at the start of this session.

**The one known fault is `preset-blues`.** The user's words: it "doesn't sound
anything like blues and almost sounds really bad", cause unknown. Every other
preset was listened to and is fine. Nothing has been investigated yet - it is
the next job and it needs nothing from the user, because it is a question about
what the engine generates rather than about their rig.

Everything else outstanding is a want, not a fault.

## Still to do

1. **`preset-blues` sounds wrong.** Start here. The shuffle warp is the newest
   engine code and the most likely culprit; `"swing": 0.62` in the file, 1.0 is
   a full triplet and 0.4 is nearer a jump blues. Worth checking whether the
   swing is fighting the twelve-bar chord changes, and whether a shuffle on a
   half-time bridge does something silly.
2. **The SSD5 mix knob is the only dead one.** SSD5's CONTROL page CC list is
   fixed - hi-hat and articulation functions, no MIDI LEARN buttons - so it
   cannot be told to take a volume CC. But MODO taught us something worth
   trying: right-click MIDI Learn on the control itself worked there after the
   typed-assignment page did not, and SSD5's Map page does have MIDI LEARN
   buttons, so the mechanism exists somewhere. The user reports right-clicking
   SSD5's *mixer fader* gives no menu; its master output may be elsewhere. A
   Gig Performer gain block is the fallback and works today.
3. **The user manual PDF.** Long agreed, never written. About falls back to the
   README.
4. **The AI planner.** The last planned feature. User supplies their own key,
   must stay optional.
5. **An all-UJAM profile set** as a second rig to A/B. The user owns Virtual
   Drummer and Virtual Bassist. See the session 6 notes for why switching to it
   wholesale was argued against.

## Session 7 - what changed

Eleven bugs, and the bass went from silent to correct.

**The bass was inaudible because it was being played off the end of the neck.**
MODO's lowest sounding note in this rig is 40, not the 28 a four-string bass
would suggest, and no octave or transpose setting in MODO explains it. Nothing
below 40 makes a sound; it just lights up the fretboard past the end.

**Calibration was writing to one profile and silently losing the rest.** Only
the drum profile recorded where it was loaded from, so a bass range corrected
by ear was dropped without a word. Saving also regenerated the drum file from
the struct, throwing away its comments and its controls block, and stacked
another "_calibrated" onto the id every time. And a nudge landed on whichever
row was selected - which is the first one, so presses meant for the bass went
to the kick and moved a verified note by twenty semitones in silence. Save now
names every note that moved.

**A tenth of bass notes played over the note before them.** A bass is
monophonic and MODO is a physical model, so two notes competing for one string
meant the earlier note's release cut off the later one.

**The articulation map was invented.** MUTING is CC 9; Ghostband had been
sending CC 21, which is assigned to nothing. The keyswitches were wrong too,
and the Latch column mattered as much as the numbers: MODO's momentary switches
only apply while held, and Ghostband released every keyswitch just before the
note it was meant to modify.

**Loading a song over a playing one left the first ringing underneath it**, and
**the mix knobs did not match the instruments until one was moved**, and **the
mix reverted to full on every host restart** - the levels and channel
assignments were never in the saved state at all.

Added: a Pause of Ghostband's own, since the host transport tends to be left
running all session.

### The thing worth remembering about MODO

**Give MODO a controller by right-clicking the control and choosing MIDI Learn.
Not through its CONTROL page.** Assigning MASTER VOLUME to a CC there looks
identical and does nothing - it was set, read back correctly afterwards, and no
controller Ghostband sent ever moved it, which was confirmed twice by pointing
the same knob at MUTING as well. That page had already dropped an assignment
silently once. MIDI Learn worked first time, and is the same flow that had
already worked for IRON 2 and Virtual Pianist.

Ruling that out took proving Ghostband's side at the wire: the harness now
checks that a level control's CC actually leaves processBlock, rather than
trusting the status line, which only reports what was intended.

## Session 5 — the control mapping system## Session 5 — the control mapping system

The whole session went into one thing: letting the user drive an instrument's
own knobs, buttons, switches and selectors from the arrangement. It started as
three fixed knob slots and ended as an open table, because their instruments
have far more controls than any list could have guessed at.

**The shape they asked for, in their words:** "you pick the instrument, next to
it is a blank button you select and next to it is the teach this knob button. it
then saves to a list underneath, showing all buttons i mapped, however many
there is to map, all with custom labels so i know what they are." That is what
Settings now does.

**Four control types, because the widget is not the point — the behaviour is:**

| type | behaviour | widgets |
|------|-----------|---------|
| `knob` | sweeps continuously | knobs, sliders, faders |
| `switch` | fully off or fully on | buttons, toggles, latches |
| `select` | holds one of N choices for the section | dropdowns, multi-position switches |

A stepped slider is a `select`, not a `knob`. Selector positions are spread
endpoint to endpoint (p/(n-1)), which is how a host maps a stepped parameter.

**The follows vocabulary grew twice, both times because the user hit a real
gap the list did not cover:**

- `intensity`, `lead`, `peaks`, `rising` — as before.
- `fixed` now takes a **VALUE**, in the control's own units. It previously
  parked everything at the bottom of its range, which is why seven of the
  user's nine mappings were silently being driven to zero.
- `none` — send nothing at all, leave the instrument as set. This did not
  exist, and `fixed` was being used for it, which does the opposite.
- `random` and `random once` — for controls with no right answer: which amp,
  which character, which pedal. In their words, these "can be changed at any
  time for any reason, depending on the sound you are going for." `random once`
  holds one choice for the song (nobody swaps amps mid-song); `random` picks
  again each section (a pedal in for the chorus).

**RANGE** — two ends in the control's own units, for any driven control. Putting
the higher number first **inverts** it, which is the whole answer for a control
that reads backwards. Narrowing it keeps a rolled selector inside one bank of a
long list.

**Send and Walk.** Teach sweeps fast, which is what MIDI Learn needs and useless
for reading an instrument's display — the user tried to count a list from one
and could not. `Send` parks a control on the typed value and sends it once.
`Walk the list` steps a selector through every position at half a second each,
slow enough to count.

### Two real bugs found by reading their saved data, not by testing

1. **Save destroyed the profile it saved to.** `toJson()` rewrote the whole file
   and took all forty comment lines of `vg-iron2.json` with it — including the
   measured 60-89 range finding. `save()` now splices only the controls block.
   The harness loads a saved file back and asserts the findings are still in it.
2. **A switch could be parked off but never on.** `fixed` drove the value to
   zero before the declared range applied, so a latch button was unmappable.

### Asked for, agreed, not yet built

- **An all-UJAM profile set as a second rig.** The user owns Virtual Drummer and
  Virtual Bassist as well as IRON 2 and Virtual Pianist. They considered
  switching to it wholesale and were talked out of it - UJAM's drummer and
  bassist are phrase players, so Ghostband would stop writing the parts and
  start picking from UJAM's prerecorded grooves, losing the kick/bass lock and
  intensity actually driving the playing. But they do want the profile set built
  later so the same song can be A/B'd through both rigs. Profiles are data;
  nothing in the engine has to change for it.
- **Possibly replacing IRON 2 and Virtual Pianist with dedicated instrument
  libraries** - something less player, more instrument. Adding one is a JSON
  file. Making a sampled guitar sound like a guitarist rather than a keyboard
  playing guitar samples would need guitar articulations - palm mute, slide,
  hammer-on - the way BassProfile already has them. PhraseProfile has chord
  zones, strum spread and phrase keys but no articulation system.

### The mix knobs, and where each part's volume lives

Guitar and piano work: a control taught with follows "level" is driven by the
part's mix knob. Both were confirmed moving the real instrument.

**SSD5 cannot be volume-controlled over MIDI at all.** Its Map page has a CC
mode, but unlike Notes mode the rows carry no MIDI LEARN button - the CC map is
fixed to hi-hat and articulation functions and is not user-assignable. A Gig
Performer gain block after SSD5 is the answer for drums, not a workaround.

MODO Bass 2 has a real Control page for assigning CCs to parameters, so its
volume should be reachable. Not yet done.

### Still open from this session

- The user is mid-way through mapping IRON 2 and Virtual Pianist. The finisher
  preset list is unresolved: it shows 30 FX and 32 Ambience entries, and whether
  that is one 62-way parameter or two separate ones was not determined. `Walk
  the list` and `Send` exist to answer it — VALUE 1 then VALUE 31 says which.
- An adversarial review of the whole control system was running at end of
  session (six dimensions, each finding challenged by a skeptic). Its results
  were not read. Re-run it or check the workflow transcript.
- Profiles remain `[UNVERIFIED]` for SSD5 and MODO — still needs a Calibrate run.

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
  saved into the driver profile. Each entry is named by whoever made it and is
  a **knob**, a **switch** or a **select**; each says what it follows, and a
  driven one has a range while a parked one has a value. Teach sweeps the CC so
  MIDI Learn can latch on; Send and Walk exist for reading the instrument back.
  See the session 5 notes above for the full vocabulary.
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

- **Never send an unverified articulation.** A guessed keyswitch or controller
  can do far worse than nothing: MODO Bass went completely silent for whole
  songs because a mute CC nobody had checked was being sent as though it were
  known, while the notes beside it were perfectly correct. Notes are the safe
  part of a profile and articulations are the risky part, so a profile marked
  `needs_verification` now emits notes only. Diagnosing it took a MIDI dump
  showing CC 21 going out 43 times a song.
- **A test that hard-codes a number will eventually be wrong about it.** The
  harness pinned the host tempo at 168, which happened to be demo-metal's, so
  every check silently walked demo-rock at nearly twice its speed and then
  counted the notes it had missed. It reads the song's own tempo now.

- **A generated writer must never clobber a hand-written file.** Saving a
  mapping used to rewrite the whole profile from the struct, which threw away
  every comment in it — and in these files the comments are the measured
  findings, not decoration. Splice the one block that changed.
- **Rolled and derived values must draw from their own seed stream.** Adding a
  control that uses the section generator's RNG would shift every note of a song
  that was already right. Each one derives from the song seed and its own name.
- **When the user reaches for an option that is nearly right, the option they
  want is probably missing.** Seven of nine mappings on `fixed` was not a
  preference; it was the absence of `none` and of `random`. Read the data they
  produce, not just the faults they report.

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
