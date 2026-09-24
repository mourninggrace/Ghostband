# TODO

The living list: what is next, what is waiting on somebody's ears, and what is
deliberately not being done. The detailed engineering notes behind each item are
in [BACKLOG.md](BACKLOG.md); the questions only the owner can answer are in
[OPEN-QUESTIONS.md](OPEN-QUESTIONS.md).

*Last updated 2026-09-24, with v2.0.0.*

## Waiting on a listen

These are built and measured. Whether they are *right* is a judgement only ears
can make.

- [ ] **The first real planner song** — what it wrote, how long it took, and what
      it cost. The change log records the tokens; the price per song has not been
      measured yet.
- [ ] **The fills-section arpeggio** — too busy, too present, or right? Half notes
      when quiet, quarters in the middle, eighths when loud: two numbers to move.
- [ ] **Do the lead guitar's fills still sound familiar** from song to song?
- [ ] **Does the lead play enough** now that solos favour eighths and quarters?
- [ ] **Does Intuition's useful range sit where the dial is,** or is it crammed
      into one corner?

## Next

- [ ] **Planner settings in the interface** — model and effort are fixed in code
      (Claude Opus 5.5, medium) and should be choosable in Settings.
- [ ] **Takes sliding into the list** — the last animation from the agreed list.
- [ ] **The CLI's MIDI export overlaps notes on the lead channel** (the plugin does
      not). Found while measuring the lead; not yet traced.
- [ ] **Per-note hand edits in the grid** — needs a spec first: where an override
      is stored, what a reroll does to it, whether a take carries it.
- [ ] **`bigMoment` fires only on chorus and solo** — a musical judgement for the
      owner, not a bug.

## Later

- [ ] **Live following** — comping behind what is actually played. Needs chord
      detection and tempo tracking; the largest unbuilt idea.
- [ ] **A second rig, all UJAM**, for A/B-ing the same song through two sets of
      instruments.
- [ ] **More presets**, and more variations within a genre.

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
- **Per-note lead gestures on the Hydra** until its CC 40 is re-banded to carry
  them — values chosen for one layout and read under another were the whole of the
  v2.0.0 palm-mute bug.
