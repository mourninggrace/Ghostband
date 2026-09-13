# Things I need from you

One file, so none of it has to be remembered. Answered items move to the bottom
with a date rather than being deleted, so the same question is never asked twice.

**Last updated 2026-09-13, end of session 19.**

---

## YOUR LIST

### 0. THE UI STALL — ANSWERED, and it is not Ghostband. Over to you.

Caught eleven times on 2026-09-13, between 11:11 and 11:32: **29.5 seconds of
frozen window in twenty-one minutes, the worst a single 11.6-second hang.**
Every one of them said the same thing:

```
gap 11580 ms   ghostband 0.0 ms   audio 1086 blocks of 512 at 48.0k
```

Two numbers settle it. **Ghostband used 0.0 ms** of interface time during every
freeze — and that figure includes painting and every button handler, which were
the two holes I closed before trusting it. And **the audio thread never missed a
block**: every gap divides by its block count to 10.6–10.8 ms against a
theoretical 10.67 for 512 samples at 48 kHz. Through the 11.6-second freeze it
ran 1,086 consecutive blocks.

So the plugin was fine and the *host's* interface thread was held by something
else. There is nothing left to fix in Ghostband, and I cannot see outside it.

**Two things that would settle what is holding it, both yours to run:**

**1. Defender exclusions.** Real-time protection and behaviour monitoring are
both on, with no exclusions I can read. Defender scanning sample files as
Kontakt streams them is the classic cause of exactly this shape of freeze. In an
**administrator** PowerShell:

```powershell
Add-MpPreference -ExclusionPath "C:\Program Files\Common Files\VST3","C:\Program Files\Native Instruments","C:\Users\strin\Documents\Native Instruments"
```

Add your sample-library drive too if it lives elsewhere. Reversible with
`Remove-MpPreference -ExclusionPath ...`. I will not change security settings
myself, which is why this is a command rather than something already done.

**2. Bisect the rackspace.** Ghostband alone in a fresh rackspace, ten minutes
of playing. Still stalls → Gig Performer or Windows. Doesn't → add Kontakt back,
then the rest.

Either way, `stalls.log` keeps score, and the footer turns amber when it
happens. If the log ever shows a line where **ghostband** is a big number rather
than 0.0, that one IS mine and I want to see it.

### 0a. v0.4.0 is out

https://github.com/mourninggrace/Ghostband/releases/tag/v0.4.0

Published, marked latest, installed on your machine, and the download's checksum
verified by fetching it back from GitHub. Nothing is sitting unreleased.

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

Nothing about the music changes — only how closely you are looking at it.
**You chose `bar` and it is the default now**, which is also the cheapest: a
quarter as many rows as a beat, so the grid repaints least often on the setting
it opens on.

### 0c. The grid is clickable, and one limit is worth knowing

Click any row and a strip opens on that bar:

    BAR 3   intro     CHORD [Em]   FEEL [straight]
                      Reroll section | Open in editor | Close

**What it does not offer is note editing, and that is a real limit rather than
an oversight.** A note in the grid is the *output* of a seed and a plan. There
is nowhere to put a hand-placed one and no way to keep it through a reroll. What
you can change is what actually decides those notes — the chord under the bar
and the feel of its section — and both are heard immediately.

If you want genuine per-note hand edits, that is buildable and it is a session
of its own: it needs somewhere to store an override, a rule for what a reroll
does to it, and a decision about whether a take carries it. **Say the word and
I will spec it properly first.**

One behaviour to know: editing a bar in a section whose chords were automatic
**pins that section to what it was already playing** and changes only your bar.
Otherwise its other bars would drift the next time the song regenerated.

### 0c2. Row shading — tell me if it is now too much

Every line should be distinguishable from its neighbours at every zoom, with the
downbeat strongest, beats in the middle, a zebra either side, and every fourth
bar marked as a phrase edge. The contrast is two numbers if it reads as too
subtle or too busy.

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

**The caution I had is now half answered.** Ghostband's own frame turned out to
be cheap — 4.5 ms for a full repaint against a 33 ms budget — so there is real
headroom and animation is affordable. What is still unknown is the stall, and
it is unknown in the direction that matters: if it turns out to be Ghostband
starving its own message thread, animation makes it worse.

So the order I would pick is **animate the cheap, self-contained ones now** (the
screen cross-fade, the queued-section pulse, knobs easing on recall — none of
them run while the grid is scrolling) and **leave the playhead easing until the
stall is understood**, because that one runs continuously in the exact place the
fault would live. Say if you would rather have all of it and take the risk.

### 0e. The tracker, the playhead and the song names

All installed. Worth a look:

- The playhead now **stops at the last bar** instead of running on. Stop and
  start your host to play again — not rewind: on its own clock Ghostband ignores
  the host's position entirely.
- Songs are no longer all called **"Untitled"**. Existing takes keep the wrong
  name because it was baked in when you saved them; re-saving under the same
  name fixes one in place.
- The tracker's row size is now a control rather than a constant — see 0b.

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
