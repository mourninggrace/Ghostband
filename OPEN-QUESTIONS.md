# Things I need from you

One file, so none of it has to be remembered. Kept current — anything answered
gets marked done and moved to the bottom rather than deleted, so the same
question does not get asked twice.

Last updated 2026-09-11.

---

## 1. Try these and tell me if they are wrong

- **The structure editor has a "guitar 2" toggle now**, on the PLAYS row beside
  the other four. Built 2026-09-11 because it was the only thing left that did
  not need you. **Not yet installed** — say when Gig Performer is closed. If you
  would rather not have it, it comes out as cleanly as it went in.

- **The guitar keeps one tone for a whole song now.** You reported that a
  recalled take played the same notes through different effects. It was not
  randomness leaking in — the notes AND the controls come back byte-identical,
  which is now pinned. The cause was the profiles: `finisher amount`, `width`,
  `focus` and `latch` followed `random`, which re-chooses at **every section
  boundary**, while `finisher` — which effect it actually is — followed
  `random once` and was held for the song. So Ghostband picked the effect once
  and then re-rolled how much of it every eight bars.

  All four are `random once` now, and the piano's `tone` and `ambience amount`
  with them. One tone per take; two takes of one song still differ. **Needs the
  install.** If you liked the movement, it is one word per line to put back.

- **The colour themes**, including the drop-down fix — Paper is fixed and is in
  the 0.2.0 build you are running. Its palette was never at fault; the menus took
  their colours from a table that did not follow the theme. All six screens were
  rendered on Paper afterwards and looked at.

## 2. One thing only you can diagnose

- **Shreddage goes quiet below about note 40.** It sounded during calibration and
  has been silent since, with nothing changed. Performance Style is ruled out by
  test — Mono Lead and Standard both behave the same.

  The suspect left is **the loaded patch's own string configuration or tuning**.
  Hydra is an eight string tuned F#1 B1 E2 A2 D3 G3 B3 E4; an eight string
  configured to play as a six has no F#1 or B1, which is exactly this symptom.
  Worth a look in Kontakt at which strings that patch thinks it has.

  You have said you want this diagnosed and fixed rather than clamped around, so
  it stays open until it is. `lowest_note` is pinned at 40 meanwhile, which is E2
  — no song loses anything today, but the instrument is still lying about its
  range and that is worth knowing.

## 3. Decisions that steer what I build next

- **When to cut the next release.** Agreed: periodically, after a substantial
  batch, not per commit. `Release.bat` does the work. Just say when.

## 4. Standing things, so they are written down once

- **Close Gig Performer before I install.** It holds the plugin open, and a copy
  that silently fails looks exactly like a fix that did not work. The installer
  waits 15 seconds and then refuses rather than misleading you.

- **Call time on the budget.** I cannot see the balance. You said stop at five
  dollars remaining; I will not know unless you tell me.

---

## Answered

- **The AI planner** — a version 2 feature, and the one leading it. Written up
  for readers in the README under *Where this is going*. *(2026-09-07)*
- **Does Ghostband need the host transport running?** Yes, and it now says so on
  its own status line as well as in the README. *(2026-09-07)*
- **Is Paper fixed?** Yes — in v0.2.0. *(2026-09-07)*
- **Do the recalled takes sound identical?** Notes yes, tone no — and the tone
  was the profiles re-rolling effects every section, not the takes. Fixed.
  *(2026-09-07)*
- **The two removed presets** — stay gone. *(2026-09-07)*
- **AmpliTube 5 and Guitar Rig 7** — not needed; dropped from the rig for now.
  An idea involving them may come back in a later version. *(2026-09-07)*
- **Did the drums come up?** Not noticeably. Closed either way: the stale CC 7 is
  gone and is not coming back. *(2026-09-07)*
- **More presets** — 34 is enough. More in version 2. *(2026-09-07)*
- **A separate user manual** — no. The README is the manual, and the About
  screen's button now opens it. *(2026-09-07)*
- **MINDst** — dropped, not revisiting. *(2026-09-07)*
- **Shreddage's pitch bend range** — set to 2, stray CC 20 automation removed.
  Ghostband does not touch the control. *(2026-09-07)*
- **Calibration** — Shreddage walked through it, along with IRON 2's top note
  (84 → 89) and Virtual Pianist's (72 → 95). *(2026-09-07)*
- **The Shreddage warning triangles** — overlapping rules compared one dimension
  at a time. Cleared by deleting the three velocity rules. *(2026-09-07)*
- **The all-UJAM rig for A/B** — a version 2 feature. *(2026-09-07)*
- **A running history of recent rolls** — offered, and not needed once the named
  takes existed. Not building it. *(2026-09-07)*
- **Takes vs Variations** — "Takes". *(2026-09-07)*
- **Version number for the first release** — 0.1.0 stands. *(2026-09-07)*
