# Things I need from you

One file, so none of it has to be remembered. Answered items move to the bottom
with a date rather than being deleted, so the same question is never asked twice.

**Last updated 2026-09-11, after v0.2.2.**

---

## YOUR LIST

### 0. The interface overhaul — look at it and steer

Not released, and not finished. Only the **song screen** has actually been
redesigned; Calibrate, Edit, Settings, Takes and About still use the old form
layout and simply inherit the new palette.

What changed: the section list is gone from the song screen, replaced by an
**arrangement grid** - sections left to right, each as wide as it is long, five
player lanes down, inked by how much each part plays there, with the intensity
curve above and the playhead sweeping across. The **Blueprint** theme is the new
default.

**Tell me which way to go:** keep pushing (the other five screens, the wordmark,
the knobs, the buttons), change direction, or stop here. The old look is one
theme away - every other palette still works.

### 1. Listen to the low end

**v0.2.2 is installed.** Shreddage's profile claimed a floor of 28; the real one
is 40, so 166 second-guitar notes across 19 of the 34 presets were being written
below the guitar's lowest string and silently dropped. They play now.

Worst affected, so the best places to hear it: **preset-sludge-2** (26 notes),
**preset-metal-1** (20), **preset-blues-2** (16), **preset-thrash-2** (13).

### 2. Second guitar chords — ANSWERED, no.

Ghostband currently never writes more than **two** simultaneous notes on guitar 2.
It writes overlapping *pairs*, which is what makes Shreddage play legato, and that
legato is why the solos sound the way they do. Measured: all 34 presets do it,
2,892 note-ons land while another is still sounding, and the maximum is always 2.

So there are no chords to un-mute. Turning Hydra's **Mono** off would not add any;
it would only convert 2,892 legato transitions into separately-picked pairs, which
would probably make the solos worse. **Leave Mono Lead on.**

If you want real chords from the second guitar in support sections, that is engine
work — writing three- and four-note voicings — and I will do it when you say.

### 3. Nothing else

Everything else on this list is either mine or already settled.

---

## MY LIST — nothing is in flight

Candidates, whenever you want them:

- **Version 2, led by the AI planner.** You are keen, it is large, nothing is
  started. Written up for readers in the README under *Where this is going*.
- **Guitar 2 chord voicings** — see your item 2 above.
- **More presets** — parked at 34, agreed to revisit in v2.
- **An all-UJAM second rig**, so a song can be A/B'd through two sets of
  instruments. Agreed as a v2 feature.

---

## STANDING

- **Close Gig Performer before I install.** The installer now tests whether the
  plugin file is actually locked rather than scanning for a process name, so a
  leftover or unrelated process no longer blocks it — but a genuinely loaded
  rackspace still does, correctly.
- **Call time on the budget.** I cannot see the balance; I will not know unless
  you tell me.
- **Releases** are cut periodically after a batch of changes, not per commit.
  `Release.bat` does the work — just say when.

---

## ANSWERED

### Shreddage's silent low register — SOLVED 2026-09-11

Five sessions, six wrong theories, settled by reading the manual.

**The profile was wrong about the instrument.** Hydra is an eight-string but not
at concert tuning — Impact Soundworks sampled an Ibanez Iron Label 8-string
**drop-tuned to low E**, so its lowest string is E1 = MIDI 40 in Kontakt's
numbering (C-2 = 0, manual p19). There is nothing below it. "Silent below 40 when
played by hand" — the original report — was simply true.

**And 27 is a keyswitch.** MIDI 24–27 are FX keyswitches; 27 is *thrash*, which
re-triggers the last-played note. That explains every contradiction: it "sounded
like the highest note" because it re-triggered the highest note; it was silent on
a cold instrument because there was no last note to re-trigger; and it started
working the moment anything had been played inside Kontakt.

Dead theories, each killed by measurement: a 6-string library; a keyswitch
collision; an articulation; legato reach; Ghostband auditioning the wrong note;
and background loading being disallowed. **Every one of them asked *which note*,
because the report was phrased as a range. The answer was about *what the
instrument is*.**

### Everything else

- **The held-note calibration step** — removed; it answered its question.
  *(2026-09-11)*
- **The wake note** — built at your request, then deleted once the real cause was
  found. It was warming a register that was never asleep. *(2026-09-11)*
- **The guitar 2 toggle** in the structure editor — shipped in v0.2.1.
  *(2026-09-11)*
- **Does Ghostband play the wrong note in Calibrate?** — no, pinned across every
  step. What you heard was the thrash keyswitch. *(2026-09-11)*
- **The AI planner** — a version 2 feature, and the one leading it. *(2026-09-07)*
- **Does Ghostband need the host transport running?** — yes, and it now says so on
  its own status line as well as in the README. *(2026-09-07)*
- **Is Paper fixed?** — yes, in v0.2.0. *(2026-09-07)*
- **Do recalled takes sound identical?** — notes yes; the tone was the profiles
  re-rolling effects every section, now fixed. *(2026-09-07)*
- **The two removed presets** — stay gone. *(2026-09-07)*
- **AmpliTube 5 and Guitar Rig 7** — not needed; dropped for now, possibly
  revisited later. *(2026-09-07)*
- **Did the drums come up?** — not noticeably. Closed either way. *(2026-09-07)*
- **More presets** — 34 is enough; more in version 2. *(2026-09-07)*
- **A separate user manual** — no. The README is the manual, and the About
  screen's button opens it. *(2026-09-07)*
- **MINDst** — dropped, not revisiting. *(2026-09-07)*
- **A running history of recent rolls** — offered and declined once named takes
  existed. *(2026-09-07)*
- **Takes vs Variations** — "Takes". *(2026-09-07)*
- **Shreddage's pitch bend range** — set to 2, stray CC 20 automation removed.
  *(2026-09-07)*
- **The Shreddage warning triangles** — cleared by deleting the three velocity
  rules. *(2026-09-07)*
- **The all-UJAM rig for A/B** — a version 2 feature. *(2026-09-07)*
