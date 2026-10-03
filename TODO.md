# TODO

The living list: what is next, what is waiting on somebody's ears, and what is
deliberately not being done. The detailed engineering notes behind each item are
in [BACKLOG.md](BACKLOG.md); the questions only the owner can answer are in
[OPEN-QUESTIONS.md](OPEN-QUESTIONS.md).

*Last updated 2026-10-03, session 33 (branch fills-v3-wip ahead of v2.4.0; last two commits not yet installed). See NEXT.md.*

## In the middle of right now

All on branch `fills-v3-wip`, installed in the owner's GP5, NOT on main or released.

- [x] **Fills v3** (v2.4.0) - the lick engine (Vai / Van Halen / Hammett / Satriani vocabulary,
      every Shreddage articulation), per-playing timing, sung phrases between
      answers, shuffle-correct. Waiting on the owner's ears.
- [x] **Solos v3** (v2.4.0) - `generateLeadSolo`: motif + answer, AAB, development, build,
      climax, resolution; complexity/intuition now drive it. Waiting on ears.
- [x] **New knobs** (v2.4.0, verified 2026-10-02) - UNDER (guitar 2 fills level), BUSY per instrument, SHRED.
      Waiting on ears.
- [x] **Release it** (v2.4.0, 2026-10-02) - merge to main, CHANGELOG, MANUAL, README, screenshots, NEXT,
      cut the version. None of the docs describe session 29's work yet.

## Broken, or known to be short

- [x] **Takes do not remember BUSY / SHRED** (fixed 2026-10-02) - recalling a take plays it with the
      knobs as they are now.
- [x] **Bass BUSY up is weak in a shuffle** (fixed 2026-10-02: pushes + movement) (1248 -> 1271 notes on blues-3).
- [x] **Blues fills answers are short** (2026-10-03: line-end answers now reach back
      into the bar before; ~2 beats -> 3.1-4.3) - a shuffle bar leaves two thirds of a bar
      of room; they may need to reach into the bar before.
- [x] **Shreddage silent below ~40** (closed 2026-10-03: the owner walked 27-40 in Calibrate, all sound) - clamped around, never explained (BACKLOG C5).
      Needs one Calibrate run.
- [x] **Clean-up** (2026-10-02: 1,585 dead lines removed) - old `generateSolo` and `kSoloGestures` kept unused until the
      new solos are approved; the note-emitting loop is repeated three times.
- [ ] **Host freezes** (not Ghostband) and **Kontakt 8.13.1 hanging on GP5 exit**
      (installer works around it) - both outside this code.

## Next, in order (owner's requests) - updated session 30

0. [x] NOT A GHOSTBAND BUG (2026-10-03: the Iron 2 preset/effects; owner changed them and it sounds right; checked 0 chord overlaps in 34 songs) **GUITAR 1 PLAYS WRONG-SOUNDING CHORDS** (owner, 2026-10-02, end of session 32) - "a bunch of wrong notes/chords all the time, flat minor augmented". BLOCKS the v2.4.0 release. Suspect: making-room chord holds crossing chord changes (twoGuitars). Goal (owner): every instrument plays right-sounding notes at the right time, every song, every time.

0. [x] DONE 2026-10-02 (dials verified + fixed, audit done, v2.4.0): **AFTER today's items (owner, 2026-10-02):** (a) VERIFY everything works
       as intended - mainly that BUSY and SHRED audibly change things when
       turned; (b) a COMPREHENSIVE BUG AUDIT - find and fix everything. Without
       breaking anything approved (feedback-dont-break-the-good-stuff).

0. [x] **UNDER works** (2026-09-27). Kontakt 8 ignored CC 7; the owner MIDI-learned
       Kontakt's Vol slider to CC 85 (Settings > output volume > Teach). The GP5
       16-channel Audio Mixer block was making up the lost level (on/off only);
       disabled, UNDER fades properly. Guitar 2's balance is now the GTR 2 knob.
0. [x] FIXED 2026-10-02 (it was the volume reaching pre-amp gain; learned Output Volume now) **Solo doesn't sound louder than fills** even at full level (owner, 2026-09-27;
       the Vol slider does rise at the solo, and the GTR 2 knob is audible in it).
       Lead: at the solo Ghostband flips Hydra's pickup neck->bridge (CC22 0->127)
       and bite 28->99 - likely thinner/quieter. Owner parked it for now.
0. [ ] **Guitar 2 level is on CC7 in the owner's rig**, not CC85: his Settings Save
       (12:18) wrote an older in-memory set into the installed profile and the
       installer carried it. Kontakt learned CC7. Works, but CC7 is what other
       parts' fallbacks send (guitar 1 sends CC7 ch2) and Hydra is on Omni -
       settle on one CC and have Hydra listen on ch 11 only.
0. [x] SUPERSEDED 2026-10-02: the tick crashes Kontakt 8.13.1 every time; guitar 2 volume is now a LEARNED Instrument Header Output Volume (CC7, tick off). Was: **Warn when Kontakt's "Accept standard controllers" tick is lost** (it is
       saved with the Hydra PRESET and resets on every preset change - owner hit
       it 2026-09-27). The GTR 2 dial/UNDER reach Kontakt's rack volume (after
       the amp) only with it ticked; Hydra's own VOLUME knob is pre-amp gain.
0. [x] **Kontakt 8 crashed** while the GTR 2 dial was dragged (13:00, access
       violation inside Kontakt 8.vst3). Guitar 2's level now goes out only when
       it changes, at most ~20/s while playing, and not twice per move.
0. [x] **Piano dial did nothing** - Virtual Pianist had lost its CC28 learn; owner
       re-taught it (Settings > volume > Teach), works (2026-09-27).
0. [x] (owner mapped CC26, 2026-09-27) **Capo** - the last articulation row (22 of 23 confirmed by ear
       2026-09-27). Hydra's capo is a MIDI-learned knob, so it resets with the
       preset like the CC7 tick. Thrash note (27) is confirmed but not yet used
       in songs - fast same-pitch repeats are the place for it. DONE 2026-10-03: pedal riffs in thrash/groove/metal fills.
0. [x] Drums BUSY up is weak in a shuffle (fixed 2026-10-02: 668/810/919 on Nine Cent Rain).
0. [-] NOT NEEDED (owner, 2026-10-03: GP5 now closes all its processes on quit) One-click GP5 close that also clears Kontakt's hung process.

1. [x] (2026-09-27: twin harmony + guitar 1 makes room done; unison hits and trading bars not chosen) **Guitar 1 and guitar 2 playing together** - harmonies, unison hits, trading
       bars, guitar 1 leaving space - in fills, and especially solos.
2. [-] SHELVED 2026-09-27 (owner: no extra plugins; Hydra's own presets cover tone). **Effects that fit the part** - research each instrument's effect controls
       from its manual first. Known wall: Shreddage's Console effects have MIDI
       learn removed (BACKLOG D2). Also answer AmpliTube 5 / Guitar Rig 7 (C4).
3. [ ] **Capo** - check the Hydra manual for a real one; otherwise emulate open-string
       voicings up the neck.
4. [ ] **Calibrate: articulation test** - plays pinch, harmonic, tap, choke, rake,
       each named, to confirm the keyswitch map on the owner's Shreddage.
5. [x] (2026-10-03: CLIMB, THEME, TRICKS under GTR 2, layout A) **Curated controls** - which of the solo engine's other choices (motif
       return, register climb) deserve a knob. Few, not many.

## Waiting on the owner

- [ ] Install the last two commits (double-click resets, mix boxes) - close GP5 first; then his verdict on the boxes and the 850 window.
- [ ] Any audio dropout: note the time; read changes.log for "AUDIO DROPOUT" (says whether Ghostband was slow).
- [ ] Release v2.5.0 when he says (everything in CHANGELOG [Unreleased]).

- [ ] Listening verdict on everything in "In the middle".
- [ ] `stalls.log` after the next audio break-up.
- [ ] `bigMoment` fires only on chorus and solo - a musical judgement.
- [x] Defender exclusion for GP5 (owner, already in place 2026-09-27).
- [x] Guitar 2 lowest note re-set in Calibrate (owner, 2026-09-27 - it was set
      wrong; may explain the "silent below E2" workaround - listen for it).
- [x] Kontakt CC 7 tick; GP5 launcher re-pinned from the Start menu loader.
- [ ] AmpliTube 5 / Guitar Rig 7: does he own either, and should Ghostband
      drive an amp/FX plugin after Hydra (C4)?

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
