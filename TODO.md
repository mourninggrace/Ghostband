# TODO

The living list: what is next, what is waiting on somebody's ears, and what is
deliberately not being done. The detailed engineering notes behind each item are
in [BACKLOG.md](BACKLOG.md); the questions only the owner can answer are in
[OPEN-QUESTIONS.md](OPEN-QUESTIONS.md).

*Last updated 2026-09-26, session 29 (branch fills-v3-wip, installed, not yet released).*

## In the middle of right now

All on branch `fills-v3-wip`, installed in the owner's GP5, NOT on main or released.

- [ ] **Fills v3** - the lick engine (Vai / Van Halen / Hammett / Satriani vocabulary,
      every Shreddage articulation), per-playing timing, sung phrases between
      answers, shuffle-correct. Waiting on the owner's ears.
- [ ] **Solos v3** - `generateLeadSolo`: motif + answer, AAB, development, build,
      climax, resolution; complexity/intuition now drive it. Waiting on ears.
- [ ] **New knobs** - UNDER (guitar 2 fills level), BUSY per instrument, SHRED.
      Waiting on ears.
- [ ] **Release it** - merge to main, CHANGELOG, MANUAL, README, screenshots, NEXT,
      cut the version. None of the docs describe session 29's work yet.

## Broken, or known to be short

- [ ] **Takes do not remember BUSY / SHRED** - recalling a take plays it with the
      knobs as they are now.
- [ ] **Bass BUSY up is weak in a shuffle** (1248 -> 1271 notes on blues-3).
- [ ] **Blues fills answers are short** - a shuffle bar leaves two thirds of a bar
      of room; they may need to reach into the bar before.
- [ ] **Shreddage silent below ~40** - clamped around, never explained (BACKLOG C5).
      Needs one Calibrate run.
- [ ] **Clean-up** - old `generateSolo` and `kSoloGestures` kept unused until the
      new solos are approved; the note-emitting loop is repeated three times.
- [ ] **Host freezes** (not Ghostband) and **Kontakt 8.13.1 hanging on GP5 exit**
      (installer works around it) - both outside this code.

## Next, in order (owner's requests) - updated session 30

0. [ ] **UNDER does nothing** (owner, 2026-09-27; it was never BUSY). It moves
       Hydra's pre-FX gain. Profile now puts the level on CC 7 (Kontakt output)
       but his taught store still says CC 20 - migrate it; owner ticks Kontakt's
       "Accept standard controllers for Volume (#7) and Pan (#10)".
0. [ ] **The ten articulations not sent yet** (legato slide as a phrase tool,
       palm-mute lead, staccato, tremolo, FX 24-27, picking mode, capo) - now
       releasable, since Sustain presses C-1. Each confirmed in the test.
0. [ ] Drums BUSY up is weak in a shuffle (+6%).
0. [ ] One-click GP5 close that also clears Kontakt's hung process.

1. [ ] **Guitar 1 and guitar 2 playing together** - harmonies, unison hits, trading
       bars, guitar 1 leaving space - in fills, and especially solos.
2. [ ] **Effects that fit the part** - research each instrument's effect controls
       from its manual first. Known wall: Shreddage's Console effects have MIDI
       learn removed (BACKLOG D2). Also answer AmpliTube 5 / Guitar Rig 7 (C4).
3. [ ] **Capo** - check the Hydra manual for a real one; otherwise emulate open-string
       voicings up the neck.
4. [ ] **Calibrate: articulation test** - plays pinch, harmonic, tap, choke, rake,
       each named, to confirm the keyswitch map on the owner's Shreddage.
5. [ ] **Curated controls** - which of the solo engine's other choices (motif
       return, register climb) deserve a knob. Few, not many.

## Waiting on the owner

- [ ] Listening verdict on everything in "In the middle".
- [ ] `stalls.log` after the next audio break-up.
- [ ] `bigMoment` fires only on chorus and solo - a musical judgement.
- [ ] Defender exclusions / bisecting the rackspace for the host freezes.
- [ ] One Calibrate run for Shreddage's low register.

## Version 3

Announced on the front page, so people know what is coming.

- [ ] **Export MIDI** — a button that writes what Ghostband plays for the loaded
      song to a standard MIDI file (one track per part) for any DAW or plugin.
      The CLI already exports, and as of the 2026-09-26 audit its MIDI has no
      re-struck notes in any of the 34 songs (lead and chords both fixed).
- [ ] **Per-note hand edits in the grid** — needs a spec first: where an override
      is stored, what a reroll does to it, whether a take carries it.
- [ ] **More presets**, and more variations within a genre.

## Later

- [ ] **Live following** — comping behind what is actually played. Needs chord
      detection and tempo tracking; the largest unbuilt idea.

## Waiting on the owner's rig

- [ ] **The window freezes** that are not Ghostband's — proven by the stall log to
      come from the host side. Defender exclusions or bisecting the rackspace would
      settle what is holding it. See OPEN-QUESTIONS item 0.

## Deliberately not doing

Decided, with the reason, so they are not re-proposed.

- **Chords on the second guitar.** It is a lead voice; a second chordal part is
  what the rhythm guitar and piano are for.
- **GPU / OpenGL rendering.** Nothing in the interface is GPU-bound — a full
  repaint is under 5 ms — and OpenGL inside plugin windows is a known cause of
  host crashes.
- **A hidden Intuition counter that "improves" over time.** It would break the one
  guarantee everything else rests on: one seed, one song, forever. Intuition is an
  explicit, saved dial instead.
- **A hosted or subscription planner.** The planner is optional and uses your own
  key. Ghostband is free, and a per-song cost billed to a project with no revenue
  is a business model, not a feature.
- **Ghostband changing security settings on the owner's machine**, such as adding
  Defender exclusions. It can say what would help; it does not do it.
- **Shreddage's Keyboard Mode for fret control.** It disables the instrument's own
  string choice; keyswitches do the job without that cost.
- **Shreddage's Force String.** Its keyswitch layout is not documented, and a
  switch that pins every note to the wrong string is worse than none.
- **A second, all-UJAM rig** for A/B comparisons. The owner is no longer
  interested (2026-09-26).
- **Per-note lead gestures on the Hydra** until its CC 40 is re-banded to carry
  them — values chosen for one layout and read under another were the whole of the
  v2.0.0 palm-mute bug.

## Done

- [x] **Session 28 (2026-09-26)** — v2.3.0: fills re-timed with a per-song
      personality and scale walks broken (measured: top-3 rhythms 46% -> 12%,
      songs 0.43 -> 0.11 alike, walks 23% -> 10%); guitar 2 fills level; the solo
      lock; idle timer at 10 Hz and no per-frame disk check; `--fillstats` and
      `--perf` harness reports. Drawing confirmed on the GPU (Direct2D).

- [x] **Session 26 (2026-09-26)** — v2.2.0 (planner model/effort + cost line, takes
      slide in, repeated-lead-note fix, measured buffer, section-title ladder, Load
      plan menu, installer clears Kontakt's leftover GP5) and v2.2.1 (saved key
      sometimes refused: dangling entropy pointer). All confirmed by the owner in GP5.

- [x] **The listening round** *(2026-09-26)* — fills-section arpeggio "about
      right"; lead fills "more mixed up", as intended; lead amount "improved
      exponentially"; the odd string scrape now "sounded like it belonged there
      rather than the only option it had"; Intuition "works correctly".

- [x] **The first real planner song** *(2026-09-24)* — "Slow Burn Iron", 23 s,
      3,588 in / 2,261 out, about 6¢. The owner's verdict: "awesome so far".
      Its explanation was cut off on screen; fixed with "... more" and a click.
