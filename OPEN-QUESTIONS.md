# Things I need from you

One file, so none of it has to be remembered. Kept current — anything answered
gets marked done and moved to the bottom rather than deleted, so the same
question does not get asked twice.

Last updated 2026-09-07, after the take library and the v0.1.0 release.

---

## 1. Try these and tell me if they are wrong

All shipped and installed today. Every one of them passes its own checks, which
is not the same as sounding or feeling right.

- **The take library.** Roll until something is good, Takes, name it, Save. Then
  move the seed and the dials, and Recall. **It should come back note for note.**
  If your ear says otherwise that is a real finding and I want it — the checks
  fingerprint every channel by note count and pitch sum, so a difference you can
  hear would mean the fingerprint is measuring the wrong thing.

- **The colour themes.** They were never reachable before today: the picker was
  laid out at zero height and could not be clicked. It is at the bottom of
  Settings now. Switch a few — especially **Paper**, the light one, which is
  where anything left on the old palette shows up worst.

- **Save as... now writes to `Documents\Ghostband\Songs`** instead of opening
  inside the installed bundle under Program Files. And **Save on a preset no
  longer tries to overwrite the preset** — it becomes Save as..., offering the
  same name in your own folder.

- **Two presets vanished from Load plan, on purpose.** `calibrate-shreddage` and
  `preset-twin-guitar` were deleted from the project sessions ago but were still
  sitting in the installed bundle, because the installer only ever added files.
  Say so if you actually wanted either of them; both are recoverable.

## 2. Questions I asked and you have not answered

Neither is blocking. Both change what I would build.

- **AmpliTube 5 and Guitar Rig 7 — still needed?** You asked several sessions ago
  whether they earn their place now that IRON 2 and Shreddage bring their own
  amps and cabinets. I never answered you, and then it became a question only you
  can answer. It affects how guitar tone gets shaped from here.

- **Did the drums come up?** Ghostband used to send a stale CC 7 to SSD5 on
  channel 10, frozen at whatever the drum mix knob was set to before that knob
  was removed. It is no longer sent. **If the drums sound bigger now, that was
  why. If they sound identical, SSD5 was ignoring it and nothing was ever wrong.**
  Either answer is useful; the second one closes the question for good.

## 3. One thing only you can diagnose

- **Shreddage goes quiet below about note 40.** It sounded during calibration and
  has been silent since, with nothing changed. Performance Style is ruled out by
  test — Mono Lead and Standard both behave the same.

  The suspect left is **the loaded patch's own string configuration or tuning**.
  Hydra is an eight string tuned F#1 B1 E2 A2 D3 G3 B3 E4; an eight string
  configured to play as a six has no F#1 or B1, which is exactly this symptom.
  Worth a look in Kontakt at which strings that patch thinks it has.

  Not urgent: `lowest_note` is clamped to 40, and 40 is E2 — no lead guitar has
  business below it, so no song loses anything today.

## 4. Decisions that steer what I build next

- **More presets?** There are **34**. The target you parked was 50. Say whether
  to carry on, and whether any style is under-served or missing.

- **The AI planner.** Still the last planned feature. It needs **your own API
  key**, and it must stay entirely optional — Ghostband has to work exactly as it
  does now without it. Worth doing, and large. Your call on when.

- **A separate user manual?** The README is the manual now and is current, with
  screenshots that regenerate themselves. The About screen has a button pointing
  at a manual that does not exist yet. Either write one, or point that button at
  the README and close the item.

- **When to cut the next release.** Agreed: periodically, after a substantial
  batch, not per commit. `Release.bat` does the work. Just say when.

## 5. Standing things, so they are written down once

- **Close Gig Performer before I install.** It holds the plugin open, and a copy
  that silently fails looks exactly like a fix that did not work. The installer
  waits 15 seconds and then refuses rather than misleading you.

- **Call time on the budget.** I cannot see the balance. You said stop at five
  dollars remaining; I will not know unless you tell me.

- **MINDst's Kick One Shot toggle** is still untested, and only matters if MINDst
  is ever revisited. Ghostband sends a note-off 30 ticks after each hit, and with
  One Shot off that note-off *ends* the sample — so every MINDst drum may have
  been truncated the whole time it was being judged against SSD5.

---

## Answered

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
