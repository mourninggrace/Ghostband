# Things I need from you

One file, so none of it has to be remembered. Answered items move to the bottom
with a date rather than being deleted, so the same question is never asked twice.

**Last updated 2026-09-13, end of session 18.**

---

## YOUR LIST

### 0. THE UI STALL — the plugin catches it itself now

Your words: *"sometimes the ghostband UI is frozen, playhead not moving, screen
not changing, but audio is still heard like normal and then suddenly it will
start working normally again."*

It has not recurred, so rather than theorise I measured. **Every part of
Ghostband's own frame is fast** — a full repaint is 4.5 ms, fetching the grid's
contents is 0.04 ms at its worst, and a whole reroll is 1.2 ms. None of that can
produce a multi-second freeze, which rules out the suspect I had written down
last session. Useful, but it means there is nothing here to fix by reasoning.

**So the plugin now catches it in the act.** Three numbers, and between them
they say which fault it is:

| what is measured | what it means |
|---|---|
| the **gap** between screen updates | a long gap with short work means we were never called — the message thread was busy elsewhere, and elsewhere means Gig Performer |
| the **work** inside one update | a long one means Ghostband did it, and it is mine to fix |
| **audio blocks during the gap** | still climbing means the audio thread ran the whole time and only the window was stuck; stopped too means the whole plugin was held up |

Anything over a quarter of a second is recorded. **You do not have to do
anything** — if it happens you will see a note appear beside the latency reading
in the bottom right, in amber, saying how many times and how long the worst was.
Hover it and it tells you which of the three it was, in words.

It is also written to a file, because the window that would show it is the thing
that was frozen and you might close the session before looking:

```
%APPDATA%\Ghostband\stalls.log
```

**All I need next time: send me that file, or the tooltip's wording.** That plus
what you were doing turns this from a hunt into a fix.

One thing that would genuinely help: it may be worse at the finer ROWS settings,
since 16th repaints four times as often as bar. If you ever catch it, note which
setting you were on — and if it only ever happens on 16th, that is the answer.

### 0a. Everything is installed and hash-verified

Installed 2026-09-13 11:19. The binary in your VST3 folder is the one that was
built and tested — checked by hash, not assumed.

**Nothing since v0.3.0 has been RELEASED though**, and there is a lot of it now:
the mix knobs, the ROWS selector, the stall detector, and the whole rail
layout. A release is due whenever you want one — `Release.bat` does the work.

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

### 0c. The rail shipped — three screens done, three left

You picked Option A and it is in. What changed:

- **Song.** A fixed 320px rail of controls down the left, the grid taking
  everything else. **802 x 572 where it was 760 x 461** — at one row per bar
  that is 26 bars on screen at once. The five mix knobs are five rows now,
  which is what gives a knob that cannot work a whole line to say why on.
- **Settings.** Two columns — channels on the left, MIDI learn on the right.
  It was one narrow column in a 1180-wide window, so an instrument name got 900
  pixels to say "MODO Bass 2" while the control list was squeezed to sixty.
- **Edit.** The form is a rail too. This was your "cockeyed": its six rows ended
  at six different x positions because each was as wide as its own contents
  happened to be. A rail gives them one right edge for free, and the
  arrangement list gets the full height.
- **The window** is 1180 x 820, landscape, minimum 1020 x 820. The minimum is
  set by Settings, not by the song screen.

**The thing worth trying deliberately:** grab the window edge and resize it.
Only the grid changes size now. Every control stays exactly where your hand
left it, at every size.

**Not done: Takes, Calibrate and About.** All three are a short header over one
long list, they already use the full width, and they look right at the new size
— so I left them rather than adding a rail for the sake of consistency. Say if
you want them matched anyway.

Three bugs turned up while looking at the rendered screens, all now fixed:
every middle dot in the interface was rendering as `A·` (juce::String's
constructor decodes UTF-8, its `operator+` decodes Latin-1); the About screen
had a stray hairline ruled through the middle of it; and shortening the window
handed the theme picker an 8-pixel-tall box — the same fault, in the same
place, as the one whose comment sits three lines above it.

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
