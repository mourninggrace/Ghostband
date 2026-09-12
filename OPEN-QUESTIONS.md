# Things I need from you

One file, so none of it has to be remembered. Answered items move to the bottom
with a date rather than being deleted, so the same question is never asked twice.

**Last updated 2026-09-12, end of session 17.**

---

## YOUR LIST

### 0. THE UI STALL — I need details, this one is not fixed

Your words: *"sometimes the ghostband UI is frozen, playhead not moving, screen
not changing, but audio is still heard like normal and then suddenly it will
start working normally again."*

**This is a different fault from the freeze we fixed last session.** That one
took Gig Performer down with it and never came back. Yours keeps playing and
recovers on its own, which means the audio thread is fine and only the window is
stuck. I have written up the three suspects in NEXT.md and have not started.

**Next time it happens, these are the details that would crack it:**

- **How long** — a second, five, thirty?
- **What you had just done.** Had you just rerolled, loaded a plan, saved a
  take, changed a dial, switched screens?
- **Which screen** was showing, and was the transport running?
- **Does it recover on its own,** or when you click something?
- **Is it worse on a big song** than a small one?

There is no log for this. What you were doing IS the evidence.

### 0a. Install session 17's work when you are back

You had to go before Gig Performer could be closed, so **what is in your VST3
folder is still v0.3.0.** Everything below from this session is committed and
tested but not installed. Say the word and it takes a minute.

### 0b. The mix knobs, and the ROWS selector — try these first

**All five mix knobs are always on screen now.** They used to be hidden when
they could not do anything, which is why the piano knob "went missing". A knob
that cannot work is greyed with the reason on it:

- `PIANO · not in song` — this song has no piano.
- `DRUMS · no reach` — **this is the one you have had all along.** SSD5's volume
  cannot be addressed from outside by any controller, so that knob has never
  worked on your rig and never said why. Put a gain plugin after SSD5 in Gig
  Performer if you want to ride the drums.
- `GTR 2 · CC7` — the knob is guessing with a controller most instruments
  ignore. Teach that instrument its volume control in Settings and it becomes
  real.

**The tracker has a ROWS selector** on the song screen — you asked what the
other options were, so rather than pick one for you, all four are there:

| setting | one row is | what it is good for |
|---|---|---|
| **bar** | a whole bar | the shape of an arrangement at a glance. A cell reads `×7` when seven hits landed in it |
| **beat** | a beat | what you have now. Reads like a chart |
| **8th** | half a beat | off-beat placement — pushes, swing, where the guitar sits against the kick |
| **16th** | a quarter beat | a real tracker. Every hi-hat exactly where it is, at the cost of showing about a bar and a half at a time |

Nothing about the music changes — only how closely you are looking at it. Try
all four and tell me which one should be the default.

### 0c. The screens still need laying out — I have the measurements, not a plan

You said things look cockeyed, space is wasted, and the default size is wrong.
You are right, and here is the evidence rather than my opinion. I dumped every
screen's real geometry (`--audit` in the harness, new this session):

- **Your monitor is 2560×1440. The plugin opens at 800×960** — 31% of the width
  and 67% of the height. It is a portrait window holding landscape content.
- **On the song screen the controls stop at about x=450 and the window is 780
  wide.** The right-hand 45% of every control row is empty. The mix row ends at
  x=271 with 500 pixels of nothing beside it.
- **The tracker — the thing you actually watch — gets 461 of 960 pixels.** Less
  than half the window is the view; the rest is chrome.
- **The Edit screen's form rows end at five different x positions** (204, 302,
  442, 552, 780). That raggedness is exactly what "cockeyed" is.
- **Settings** gives a 522-pixel-wide label to a 30-character instrument name,
  then squeezes the control list into a 180-pixel viewport with space going
  spare above it.

**I did not start rebuilding, deliberately** — last time I redid a UI without
showing you options first you told me every app we build looks the same. The
honest structural fix is to stop stacking everything in one narrow column, and
there are two credible ways to do that:

- **A left rail.** Controls in a fixed ~300px column down the left, the tracker
  taking the entire right side, full height. Biggest possible grid; everything
  stays visible.
- **A wide header strip.** Controls in two rows across the full width up top,
  tracker below getting ~65% of the height. Less of a departure, still fixes the
  dead space.

Plus a wider default — something like 1280×860 instead of 800×960.

**Pick one, or say "show me" and I will mock both up before writing code.**

### 0d. Animations — yes, and here is where they would earn their keep

Your note: *"little cool animations here and there where they make sense, the
kinda animation you might see in very stable very expensive software by big
companies."*

Agreed, and the phrase that matters is *where they make sense*. Expensive
software animates to explain a change, never to decorate — 120–200 ms, eased,
and never in the way. Candidates, best first:

1. **Screen changes** cross-fade instead of cutting, so you can see that you
   moved rather than inferring it.
2. **The playhead row** eases between rows instead of snapping — the single
   biggest "this feels expensive" change, and the tracker is where your eyes
   already are.
3. **The section ribbon** lights the queued section with a slow pulse until the
   bar line it is waiting for.
4. **A reroll** sweeps the new arrangement in from the left rather than
   replacing it in one frame.
5. **Knobs** ease to their value when a take is recalled, so you can see which
   ones moved.

**One caution I want on the record:** the window already has a 30 Hz timer
driving the tracker, and there is an unexplained UI stall (item 0 above).
Animation adds message-thread work in exactly the place that is already
suspect. I would rather find that stall first, then animate — otherwise a new
frame budget lands on top of a fault we do not understand yet.

### 0e. The tracker, the playhead and the song names

All installed. Worth a look:

- The playhead now **stops at the last bar** instead of running on. Stop and
  start your host to play again — not rewind: on its own clock Ghostband ignores
  the host's position entirely.
- Songs are no longer all called **"Untitled"**. Existing takes keep the wrong
  name because it was baked in when you saved them; re-saving under the same
  name fixes one in place.
- The tracker's row size is now a control rather than a constant — see 0b.

### 0f. Only the song screen was ever redesigned

Worth knowing while you think about 0c: Calibrate, Edit, Settings, Takes and
About have **never been reconsidered.** They still use the old form layout from
before the overhaul and merely inherit the new palette, which is a large part of
why the plugin reads as inconsistent. Whichever direction you pick in 0c, the
other five screens should follow it rather than being left as they are.

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
