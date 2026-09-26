# TODO

The living list: what is next, what is waiting on somebody's ears, and what is
deliberately not being done. The detailed engineering notes behind each item are
in [BACKLOG.md](BACKLOG.md); the questions only the owner can answer are in
[OPEN-QUESTIONS.md](OPEN-QUESTIONS.md).

*Last updated 2026-09-24, with v2.0.0.*

## Waiting on a listen

These are built and measured. Whether they are *right* is a judgement only ears
can make.

Nothing waiting. The last round was answered on 2026-09-26 (see Done).

## Next

- [ ] **Read `stalls.log` after the next audio break-up.** `AUDIO FELL BEHIND`
      lines now say whether the missing time was spent in Ghostband.

- [ ] **`bigMoment` fires only on chorus and solo** — a musical judgement for the
      owner, not a bug.

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
