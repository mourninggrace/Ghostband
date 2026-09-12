# Ghostband: what is left

Written 2026-09-06 at the end of session 12, checked against the code rather
than copied out of NEXT.md — several things that file still listed as open are
in fact done, and are noted at the bottom so nobody rediscovers them.

> **Anything that needs YOU is in [OPEN-QUESTIONS.md](OPEN-QUESTIONS.md).**
> This file is the engineering backlog; that one is the short list of questions,
> ear checks and decisions that cannot be settled from the code.

## Releases

- **v0.1.0, 2026-09-07** — the first one. Until then there was nothing to
  download: `build/` is ignored, no binary is tracked, so the only way in was
  cloning with submodules and owning Visual Studio.
- **v0.2.0, 2026-09-07** — takes, colour themes that actually work, one guitar
  tone per song, songs saved outside Program Files.
- **v0.2.1, 2026-09-11** — the structure editor's guitar 2 toggle.
- **v0.2.2, 2026-09-11** — the second guitar plays in its actual range; 166 notes
  across 19 presets recovered from a dead region.
- **v0.3.0, 2026-09-12** — the tracker song screen, tooltips on all 92 controls,
  the Neon theme, and a deadlock that could freeze the host.

`Release.bat` cuts one. It refuses a dirty tree or a failing build, and stages
the profiles and preset songs INTO the bundle - the build output has neither, and
a zip of it would install and then be unable to name an instrument or open a song.

**Cadence, agreed 2026-09-07:** update the release periodically, after a
substantial batch of changes, rather than per commit. Bump `project(Ghostband
VERSION ...)` in `CMakeLists.txt`, commit, run `Release.bat`, then
`gh release create`.

## VERSION 2 is led by the AI PLANNER

Decided 2026-09-07. The owner is keen on it and it is explicitly a v2 feature,
not something to squeeze into 0.x. Written up for readers in the README under
*Where this is going*; this is the engineering half.

**What it is.** A model writes the CHART - chords, section shape, intensity
curve, who leads where - and the existing engine renders it. Nothing downstream
changes.

**Why it is tractable.** The handoff already exists. A plan is a small, fully
specified JSON file and every shipped song is one, so a plan a model wrote is
indistinguishable from a plan a person wrote. That is the whole reason the plan
format was made a real file format rather than an internal structure.

**Three constraints, all already true of the architecture:**

  - **Never a dependency.** Works today with no key, no account, no network.
    `autoProgression` writes progressions locally. The planner raises the
    ceiling; it is never the floor.
  - **Never a subscription.** The user brings their own API key. Ghostband is
    donation-ware and a per-song cost billed against no revenue is a business
    model, not a feature.
  - **Never in the audio path.** Background thread; playback never waits on a
    network call. The processor's sequence swap was built for this and is
    already safe for it.

**The division of labour is the point.** The model does shape, feel and
intention - what models are good at. Everything that must be exactly right every
time stays in measured code: the kick/bass lock, the articulations, determinism,
and the guarantee that one seed means one song forever.

**Do not break.** The reference songs must still render 1231/629 and 996/423, and
a build with no key configured must behave exactly as this one does.

## The TAKE LIBRARY - DONE 2026-09-07

Built. A Takes button on the song screen, a name box, a Save, and a list with
Recall and Delete. Thirty-two checks pin it, the strongest being that a take
recalled after the dials, the key and the tempo have all been moved brings the
song back note for note - fingerprinted across all sixteen channels by note
count and pitch sum, not by note count alone.

Three decisions worth not re-arguing:

**The whole song is stored inside the take, not a path to it.** Key, style,
tempo and the chords live in the plan, and the structure editor changes them
without saving. A stored path would recall a song that had moved on, and would
die with the file. A take of a since-deleted preset still plays; there is a check
that says so.

**The rig is not part of a take.** Mix levels and channel assignments are how the
rack is wired, not how the band played. Recall leaves both alone.

**No factory takes.** A factory take is a preset under another name, and shipping
any would blur the only distinction that matters.

### The roll history - asked for, then dropped

The original request was "keep a running history of the seeds used ... so if the
seeds are sitting at 88345 and I liked what I heard, I can save that seed
setting". The library answers the second half. The first half - an automatic
history of recent rolls, so the good one from two rolls ago is reachable after
you have rolled past it - was offered and declined on 2026-09-07: the named saves
turned out to be the whole of what was wanted. Not building it.

## A3, the lead guitar playing fills - DONE 2026-09-07

Confirmed by ear across the presets. The second guitar answers through the
verses and choruses of all eleven songs, there is a FILLS dial for how much,
and where each song stays quiet is recorded in its plan.

## DECIDED: next is B1, articulations over MIDI CC

Chosen 2026-09-07, replacing an earlier decision to do B1 first.

**Why the change.** B1 was argued on risk: Ghostband picks articulations by
sending notes, and a note aimed at the wrong instrument gets played as music.
True, and measured across all eleven plans, Ghostband sends Shreddage exactly
ONE keyswitch per song - note 12, Sustain, once. The danger is one note, sitting
far below anything playable, and the damage that actually happened came from the
channel bug, which is fixed and pinned. B1 remains correct engineering with a
much smaller payoff than it was sold with.

The reason it is one keyswitch is the interesting part: Shreddage only plays the
solo section, so it only ever gets one feel, so it only ever needs one
articulation. Fix that and B1 becomes worth doing.

**What A3 is.** The second guitar currently either solos or is silent. A real
second guitarist answers the vocal line in the gaps, doubles the riff through a
chorus, and plays a pickup into the next section. Largest audible improvement
left on this list.

**Where the work is.** `Render.cpp`, in the chordal block that already decides
which of guitar / guitar2 / piano leads and which supports — the guitar/piano
handover is the model to copy. Today `guitar2Feel` comes from
`chooseGuitarFeel`, and a section either names `"solo"` or it does not. What is
missing is a third state between soloing and comping: sparse, answering, and
out of the way of whichever part is leading.

**Do not break.** Reference songs must still render 1231/629 and 996/423. RNG
draws must stay where they are for sections that name no second guitar, or every
existing song changes - the chordal block is already written to layer guitar2 on
top without consuming from the shared stream when it is absent, and that
property is what keeps the pins valid.

**Then B1**, which will have several articulations per song to switch between
and will finally be worth what it costs.

---

## B1, articulations over MIDI CC — deferred, not dropped

Second in the queue, after the fills above give it something to do.

Ghostband picks Shreddage's articulations by **sending notes 12–23**, and a note
aimed at the wrong instrument gets played as music. A CC that nothing has
learned does nothing at all — the safest mechanism available, replacing the
least safe.

**What it needs.** `PhraseProfile` holds `phraseKeys` — notes — and nothing
else. `BassProfile` articulations already support both a keyswitch and a
CC-with-value, so the shape to copy exists in the same file. The work is:

  1. `PhraseProfile` learns a CC form alongside the note form, inside the same
     `phrases` block, so every existing profile keeps working untouched.
  2. `Profile.cpp` emits whichever form the profile declares.
  3. Shreddage's profile switches to CC once the numbers are known.

**What it cannot do.** Kontakt will not load headlessly, so nothing can discover
which CC each articulation ends up on. Those numbers get typed in by hand on
both sides - in Shreddage's Articulations page via its Map button, and in the
profile. Expect that to be the fiddly part, and expect the Teach fix from
session 12 to matter here: a knob in MIDI Learn takes the first controller it
hears.

**Do not break.** The reference songs must still render 1231/629 and 996/423,
and a profile that declares note keyswitches must behave exactly as it does now.

---

The rest is for reading and thinking about; nothing else is scheduled.

---

## A. Things you asked for that are not built

### A1. Colour themes, about eight
**Size: small. Risk: none.**
Every colour already routes through `ghost::` constants in
`GhostbandLookAndFeel.h`, so this is a palette table, a picker on the Settings
screen, and saving the choice with the rest of the plugin state. Self-contained
— it cannot break anything that makes sound.

### A2. More presets, and variations within a genre
**Size: small each, adds up. Risk: none.**
The shipped set is one plan per style. Two or three per style, differing in
tempo, key and arrangement rather than only in name. Pure data; no engine work.

### A3. The lead guitar playing fills, not only solos
**Size: medium. Risk: medium — it changes what every twin-guitar song sounds
like.**
Your idea. Shreddage currently either solos or is silent. A real second
guitarist answers the vocal line in the gaps, doubles the riff in a chorus, and
plays a pickup into the next section. This is the largest musical improvement
left on the list.

### A4. The user manual
**Size: medium. Risk: none.**
Agreed several sessions ago, never started. The About screen already has a
button pointing at it.

### A5. The AI planner
**Size: large. Risk: medium.**
The last planned feature. You supply your own key; it must stay entirely
optional, and Ghostband must work exactly as it does now without it.

### A6. An all-UJAM profile set, as a second rig
**Size: small — profiles are data. Risk: none.**
So the same song can be A/B'd through two rigs. Worth remembering why the
wholesale switch was talked out of: UJAM's drummer and bassist are phrase
players, so Ghostband would stop writing the parts and start picking from
prerecorded grooves, losing the kick/bass lock and intensity actually driving
the playing.

---

## B. Engine work that removes a real risk

### B1. Articulations over MIDI CC instead of keyswitch notes
**Size: medium. Risk: low to build, removes a high risk.**
This is the one I would argue for first.

Ghostband selects Shreddage's articulations by **sending notes 12–23**. A note
aimed at the wrong instrument gets *played* — that is exactly the IRON 2
disaster, where keyswitches landed on a guitar that treated them as music. A CC
that nothing has learned does nothing at all.

You found that Shreddage can drive articulations from MIDI CC (the Map button on
the Articulations page). The engine cannot use it yet: `PhraseProfile` holds
`phraseKeys` — notes — and has no CC path, though `BassProfile` articulations
already have exactly that. So the pattern exists and needs porting.

**Kontakt cannot be probed headlessly**, so the CC numbers have to be typed in by
hand on both sides. That is the whole cost.

### B2. Guitar articulations for phrase instruments
**Size: large. Risk: medium.**
Only worth it if you ever replace IRON 2 with a sampled guitar library.
`PhraseProfile` has chord zones, strum spread and phrase keys but no
articulation system — no palm mute, slide, or hammer-on the way `BassProfile`
has them. This is what separates "a guitarist" from "a keyboard playing guitar
samples".

---

## C. Things only you can settle

### C1. Shreddage's pitch bend range  [DONE 2026-09-07]
The stray CC 20 automation was removed and the knob set permanently to 2.
Ghostband does not touch this control, so it stays right.

### C2. Walk Shreddage through Calibrate  [DONE 2026-09-07]
Done, along with IRON 2's top note (84 → 89) and Virtual Pianist's (72 → 95).

Shreddage measured 28 at the bottom — two below the 30 that was reasoned from
its tuning — but the low register has not sounded reliably since, so
`lowest_note` is set to **40** rather than to the number that was measured once.
See C5.

### C5. Why Shreddage goes quiet below about 40
**Unexplained, and clamped around rather than solved.**

It sounded during calibration and has been silent since, with nothing changed.
Performance Style is ruled out by test: it has been Mono Lead (Mid/High)
throughout, and switching to Standard did not bring the notes back. The suspect
left is the loaded patch's own string configuration or tuning — an eight string
playing as six has no F#1 or B1, which is exactly this symptom.

**Measured 2026-09-11, and the earlier note here was wrong.** It claimed
`lowest_note` was clamped to 40 and nothing was lost. The profile actually says
28 - the owner set it back on purpose - and parsing every rendered preset shows
**166 of 5,884 second-guitar notes across 19 of 34 presets fall below 40**, sent
and silently dropped. preset-sludge-2 loses 26, preset-metal-1 loses 20.

Two theories are dead. Not a 6-string library: the owner confirms an 8-string.
Not a keyswitch collision: Shreddage's switches sit at 12-23 and 108/114.

The floor is at exactly 40 = E2 = a standard 6-string's low E, which is not a
coincidence worth ignoring. An 8-string Hydra bottoms at 30 (F#1). The profile's
28 matches neither and sits below even the lowest string, so that measurement was
wrong independently of whatever the patch is doing.

Waiting on one Calibrate run to turn the inference into a measurement.

### C3. Did the drums come up?
Ghostband was sending a stale CC 7 to SSD5 on channel 10, frozen at whatever the
drum mix knob was set to before that knob was removed from the screen. It is no
longer sent. If your drums sound bigger now, that was why; if identical, SSD5
was ignoring it and nothing was ever wrong.

### C4. AmpliTube 5 and Guitar Rig 7
You asked several sessions ago whether these are still needed now that IRON 2
and Shreddage bring their own amps and cabinets. **I never answered.** Worth
deciding, because it affects how guitar tone gets shaped from here.

---

## D. Known limitations, not bugs

### D1. SSD5's volume cannot be reached over MIDI
Its CC map is fixed — hi-hat and articulation functions, no MIDI Learn — so no
controller can move its level. Hence no drums mix knob. A Gig Performer gain
block is the fallback and works today. If SSD5's Map page turns out to have MIDI
Learn buttons after all, the knob returns on its own: the profile just drops
`"volume_reachable": false`.

### D2. Console's controls cannot be automated
Shreddage's amps, distortion, cabinets, EQ and delay have had CC learn removed
deliberately, per Console's own FAQ. So Ghostband cannot switch Shreddage from a
clean tone to a lead tone — it can only push a fixed tone harder, which is what
a guitarist does anyway.

### D3. Kontakt cannot be probed headlessly
Loading it outside a host gives an empty instance, so Shreddage can only be
calibrated from inside Gig Performer. Same wall as IRON 2 in Player mode.

---

## E. Ideas raised, never scheduled

- **Latching section loop** — click once and a section repeats until told
  otherwise. The jump-offset machinery already supports it.
- **Live following** — Ghostband comping behind what you play. The largest
  unbuilt idea; needs chord detection and tempo tracking.
- **"Connect it to any plugin and it just works."** Auto-mapping by measurement
  is already proven on UJAM. The blocker for licensed plugins is that they will
  not sound in a headless host — the answer is Ghostband's vestigial audio
  *input* pins: route an instrument's audio back in and it can measure its own
  output from inside the host, where everything is licensed and working.
- **MINDst, revisited.** Shelved because SSD5 sounds better, which is the right
  reason. One thing was never tested: its Kick **One Shot** toggle. Ghostband
  sends a note-off 30 ticks after each hit, and with One Shot off that note-off
  *ends* the sample — so every MINDst drum may have been truncated the whole
  time it was being judged. `SwitchDrums mndst` brings it back.

---

## F. Housekeeping

### F1. NEXT.md has become a log, not a plan
It is over a thousand lines of accreted session history, and it listed at least
two items as open that were finished sessions ago. Worth splitting: a short
"where things stand" file, this backlog, and an archive nobody has to read.

---

## Already done, despite what NEXT.md says

Checked in the code today:

- **"A guitar that only solos disappears in the plugin"** — fixed. The plugin
  counts a part present if it has chords *or* a lead line.
- **"Bring the older presets up to date so every solo section has something
  soloing"** — done. All eleven plans have the second guitar soloing.
- **Section-only reroll** — worked all along; the section rows just could not
  show it, because the counts column was blind to guitar and piano. An intro
  carried by one guitar read "0 / 0" and stayed there through every reroll.
- **Adjustable BPM, and the host/plan tempo switch** — done.
- **Taught mappings global per instrument** — done, and as of today the store no
  longer overwrites what the profile declares.
