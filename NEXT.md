# Where Ghostband stands, and what to do next

Last updated 2026-09-05, session 9. Everything described as done is committed,
pushed, installed, and covered by the harness.

## START HERE

**The blues sounds like blues now**, confirmed by ear. **The metal and rock demo
plans play the full band now** - they had named no guitar or piano profile, and
those parts are opt-in on the name, so a preset called "metal" rendered as drums
and bass. The harness now fails any plan that names a part it never plays.

**Shreddage 3 Hydra is playing, and Ghostband bends.** Confirmed by ear: the
solo flows, the held notes ring, and the bends are audible. That closes the run
of work that started with "the solo is too simple, a single note held and
changed after a few seconds".

**Its profile is `profiles/shreddage-3-hydra.json`**, reached through
`plans/calibrate-shreddage.json`. Shreddage needs three settings the manual
names and the profile records: Performance Style **Mono Lead (Mid/High)**, Poly
Input Latency **0**, Pitch Bend Range **2**. Bypass IRON 2 - both listen on
channel 2, and Ghostband has only one guitar part.

**What is still unverified** is the range. 30 to 88 is an eight-string's, and
the solo plays across 34 to 87 without a hole, which is evidence and not proof.
The Calibrate screen would settle it.

## The roadmap, as asked for on 2026-09-05

Ordered by how much of the instrument they unlock, not by size.

### THE BASS VOLUME KNOB, finally explained

`BassProfile::load` never read its own `controls` block. The drum and phrase
loaders both do; the bass one did not, so every mapping taught on a bass sat in
the file and was never loaded back - including the one following "level", which
is what the bass mix knob reaches. Without it the knob falls back to CC 7, and
MODO, like most instrument plugins, does not implement CC 7.

**The mapping was correct on disk the whole time and nothing ever picked it up.**
An earlier diagnosis in this project blamed MODO for forgetting its MIDI Learn
across a restart, on the evidence that re-teaching and saving produced a
byte-identical file. That evidence was real and the conclusion was wrong: the
file was identical because it was always right, and re-teaching appeared to fix
it because re-teaching also happens to reload nothing. One line fixes it.

### CHANNELS, as the rig has them

    1   bass            MODO Bass 2
    2   guitar          UJAM VG IRON 2      (rhythm)
    3   piano           UJAM Virtual Pianist
   10   drums           SSD5 Terry Date
   11   guitar 2        Shreddage 3 Hydra   (lead)
   12   drums 2         MINDst Drums        - constrainer exists, nothing sends to it yet

All five are the plugin's defaults and all are changeable in Settings. Channel
12 is waiting on the kit-per-song work below.

### 1. Two guitars - rhythm and lead  [DONE 2026-09-05]

Shipped. `guitar` and `guitar2`, each with a profile, a channel, a per-section
feel and a place in the lead-and-support handover. `preset-twin-guitar`
demonstrates it. Settings has a GUITAR 2 channel row and a Test button, the song
screen has a fifth mix knob, and MIDI Learn can map its controls.

Which one is "the lead" is not baked in - roles are per-section here. The only
default is that a second guitarist fronts the section, because nobody adds one
to play underneath the first.

### 1b. Two guitars - what is left

The decided one. Ghostband has **one** guitar part, so IRON 2 and Shreddage
cannot both play; a plan names one guitar profile and both instruments listen on
channel 2. A real arrangement wants a rhythm guitar holding the riff and a lead
over the top, which is also the only way a solo stops costing the harmony.

Touches: `SongPlan` (a second guitar profile and channel), `Performance` (a
second guitar part), `Render.cpp` (deciding which one leads and which comps -
the guitar/piano handover logic is the model), the Settings channel list, and
every plan that wants it. Big, and everything else on this list is easier once
it exists.

### 2. Articulation keyswitches for notes-mode instruments

Ghostband only emits keyswitches for phrase-driven instruments, where the
keyswitch *is* the performance and is held for its length. Shreddage is a notes
instrument whose keyswitches choose an articulation alongside the notes - palm
mute for a low riff, power chords for a chorus, tremolo where the section wants
it. Its whole map is already recorded in `profiles/shreddage-3-hydra.json`.

This is what turns Hydra from a better guitar sound into a guitarist.

### 3. A kit per song, and per section  [CONFIRMED SCOPE]

Confirmed: one kit per song or per section, **not** two kits playing at once.
So this is a `drum_profile` that a section can override, not a second drum part
- much smaller than it first looked. MINDst is profiled and its constrainer is
on channel 12 already.

SSD5 and MINDst both profiled, and the arrangement picking which kit plays what.
Worth checking the premise first: two kits at once is usually a mess, and what
is probably wanted is one kit per song or per section rather than per hit. A
section-level `drums:` naming a profile is a much smaller change than a second
drum part, and may be the whole feature.

### 4. The bass only ever played three articulations  [DONE 2026-09-06]

Fixed, and there were two faults behind it.

The generator asked for Normal, PalmMute and Dead and never for hammer or
slide, though MODO maps both. And the code that would have produced a slide -
the chromatic approach into a chord change - was gated on the last bar of a
SECTION, which is precisely the bar where the caller passes an invalid chord
because the next section has not been built. **The condition could never once be
true.** It is gated on the next bar's chord now, which is known everywhere, and
restricted to busy bars with a short last note, because that is what a passing
note is.

A hammer is any step of a tone or less landing close behind the note before it,
tracked across the bar line so the note an approach note aims at is the one that
gets hammered. Both decisions are made from the notes and the grid with **no
randomness at all**, which is why every song written before this keeps the same
notes at the same times and simply plays them better - demo-metal is still
1231/629 and demo-rock 996/423.

Measured on demo-band: 24 slides, 10 hammers, 42 dead notes, palm mute and
normal. Five of MODO's seven; slap and pop are slap-bass technique and no style
asks for them.

Two smaller things fell out of it. An approach note a semitone below the lowest
string is not a quiet note, it is no note - the renderer drops anything out of
range - so a chord change became a hole whenever the target was the bottom of
the neck; it approaches from the other side now. And the transpose test counted
keyswitches as notes, so a hammer firing in one key and not another looked like
a regression when it is the articulation following the fingering; it counts
played notes only.

### 4b. Taught mappings are per instrument now  [DONE 2026-09-05]

They used to be saved into the profile file. Seven files describe one SSD5, so
teaching a knob on one taught it for one of seven. They now live in one store,
`%APPDATA%/Ghostband/learned-controls.json`, keyed by a profile's `instrument`
field, which falls back to its `id`. All seven SSD5 profiles declare
`"instrument": "ssd5"`, so a knob taught on any of them is taught for all.

The profile's own block is still written on save, so a shared profile carries a
sensible default, and it seeds the store the first time an instrument is seen -
which is how the mappings already taught were carried over without anyone
redoing them. Confirmed: MODO's 1, IRON 2's 14 and Virtual Pianist's 10 all
migrated on first run.

### 5. Colour themes, about eight of them

`GhostbandLookAndFeel` already routes every colour through `ghost::` constants,
so this is a palette table, a picker in Settings, and saving the choice with the
rest of the plugin state. Self-contained.

### 6. More presets, including variations within a genre

The existing set is one per style. Two or three per style, differing in tempo,
key and arrangement rather than only in name.

### 7. Bring the older presets up to date

They predate the solo generator, sevenths, bends and legato. Every plan with a
solo section should have something actually soloing in it unless another
instrument is taking it. Cheap, and it makes the shipped set representative of
what the engine can now do.

### 8. Known bug: a guitar that only solos disappears in the plugin

`rebuildSequence` adds the guitar only when `performance.guitar.chords` is
non-empty, and a soloing part deliberately has no chords. A plan whose guitar
solos the whole way through renders correctly from the CLI and silently plays no
guitar in the plugin. Small fix; nothing shipped hits it yet because every plan
comps somewhere.

## The new rig, and how each piece has to be profiled

Scanned on 2026-09-05. **None of this is profiled yet** and none of it is in any
plan.

| plugin | state | how it gets profiled |
|--------|-------|----------------------|
| **MINDst Drums** | installed, standalone VST3 | the probe can reach it directly |
| **Waves Bass Fingers** | installed, inside WaveShell | Calibrate screen only |
| **Waves Bass Slapper** | sample library present, bundle not confirmed | Calibrate screen only |
| **Shreddage 3 Hydra** | **not found on disk** - confirm it installed | Calibrate screen only |
| Kontakt 8 | installed | host for Shreddage |

**The dividing line is whether the instrument is its own plugin.** MINDst Drums
is a real VST3, so `ghostband_probe` can load it and measure the drum map, the
range and the lead behaviour without a human. Waves instruments live inside
WaveShell and Shreddage lives inside Kontakt - loading either of those headlessly
gives an empty shell, exactly like IRON 2 coming up in Player mode. Those must be
profiled through **Ghostband's own Calibrate screen inside Gig Performer**, where
the host already has the right thing loaded. That is what the Calibrate screen is
for and it has not been used in anger yet.

Assessment, to be confirmed by ear rather than taken on trust:

- **Drums: MINDst is a genuine replacement candidate**, and the reason is narrow.
  SSD5's mix knob is the one dead control in the rig - its CC map is fixed, no
  MIDI LEARN on that control - and MINDst maps CC for nearly everything, which
  closes the only known unfixable gap. It is also the one new plugin the probe
  can profile automatically. Against that: SSD5 Terry Date is calibrated,
  measured and working, and a new kit means a full drum map from scratch.
- **Bass: keep MODO as primary, add the Waves pair as alternates.** MODO is
  physical modelling with its articulations already mapped and verified. Bass
  Fingers and Bass Slapper are sampled and would be a different tone rather than
  a better one - Slapper is worth having as a profile for the one thing MODO is
  weakest at. Additions, not replacements.
- **Guitar: Shreddage for leads, IRON 2 stays for rhythm.** They are different
  jobs and IRON 2 is good at the one it has. Worth thinking about whether a plan
  should be able to name two guitar profiles rather than one, because "rhythm
  guitar and lead guitar" is a real arrangement and Ghostband currently has one
  guitar part.

## Session 9 - what changed

**CMake is back**, version 4.4.3 at `C:\Program Files\CMake`, on PATH. The
installer offered only repair or uninstall because it was already there. `build/`
was deleted and reconfigured from scratch, because a new CMake pointed at a cache
written by Visual Studio's bundled one is what produced the "Visual Studio 18
2026" mess. **Both reference songs render identically under it** - demo-metal
1231/629, demo-rock 996/423 - so the arrangement does not depend on the
toolchain. The project needs 3.20 and JUCE needs 3.22, both clear of CMake 4's
cutoff.



**Ghostband bends now.** It emitted no pitch bend anywhere before this;
`LeadIntent::target` marked the notes a bend belonged on and nothing consumed
it.

A bend goes **into** the note, not away from it: the sounding note starts a tone
below the target and the wheel carries it up to the written pitch. That is what
a guitarist does, and it is the only version that keeps the harmony - the
landing note was chosen because it is a chord tone, so bending up away from it
would leave the line off the chord at the exact moment it is meant to agree with
it. The climb eases out over six steps rather than being linear, because a bend
is quick off the fret and slow as it arrives.

**It is off unless a profile declares it**, and that is deliberate. Pitch bend
is a channel message: it moves everything sounding on the channel, and one left
off centre leaves the part transposed for every note afterwards - which sounds
like Ghostband put the guitar out of tune, not like a bug. So every bend is
followed by a return to centre, no note is ever started while the wheel is off
centre, and panic centres the wheel on all sixteen channels before it sends
anything else. All three are asserted in the harness against the rendered MIDI,
because none of them is visible in the intents.

A profile opts in with `"bend": { "range_semitones": 2, "reach_semitones": 2,
"ticks": 90 }`. `range_semitones` must match the instrument's own wheel range or
the bend lands short or sharp.

### IRON 2 and the lead guitar question

`vg-iron2.json` declares bend **untested**, so it can be heard. If the solo's
held notes sound a whole tone flat, IRON 2 is ignoring the wheel - delete the
block and it goes back to plain notes.

What is settled: IRON 2 exposes 24 parameters and **not one is an
articulation**. No bend, vibrato, slide or legato control; the only pitch
control is a global `Tune`. Parameters are global to the plugin rather than
per-mode, so that holds in Instrument mode too. It is a riff machine, and it
was never going to shred.

Guitar Rig 7 is **not** the answer and was talked out of: it is an effect, not
an instrument, it makes no sound from MIDI, and AmpliTube 5 is already
installed. The gap is a lead *instrument*. Ordered recommendation given: Ample
Sound (standalone VST3, no Kontakt tax, cheapest, lowest integration risk),
Shreddage 3 (most controllable via TACT, but check the Kontakt requirement),
Heavier7Strings, Orange Tree Evolution, Prominy. Avoid anything sold as a
"player" or "pattern" instrument - that is IRON 2's category.

The probe has a `--mode lead` that answers this by measurement: pitch bend range,
real sustain length, and legato versus re-attack. **Run it against a demo before
buying anything.** Note that it loads a fresh instance, which for IRON 2 comes up
in Player mode, and `programs: 0` means there is no preset slot to switch it -
so its numbers for IRON 2 are about a looping riff and should be ignored.



**The solo was a melody, and what was wanted was a lead.** Reported as "too
simple, single note being held and changed after a few seconds", and the
measurement agreed exactly: 3.3 notes a bar, a median gap of a quarter note, and
silences of a bar and a half. The reference given was Satriani, Vai and Hammett.

The old generator wrote two bar phrases, mostly stepwise, with a note to land on
and a rest to answer into. That is a good blues vocabulary and it cannot be
turned into a lead by raising a density knob - a busier wandering line is still
a wandering line. So it is rebuilt around five devices, each of which makes many
notes out of one idea:

| device | what it is |
|--------|------------|
| RUN | straight subdivisions through the scale, turning round at the end of the neck |
| SEQUENCE | a three or four note cell moved one scale step per repeat - the most identifiable thing in the style |
| PEDAL | a fixed high note alternating with a line moving underneath it |
| LICK | a short motif played two or three times unchanged |
| LAND | a fast approach into one long chord tone - the breath |

Density is a property of the device rather than a setting: a run is sixteen
notes in a bar because that is what a run is. Measured on `preset-thrash`:
**12.9 notes per bar over 22 semitones**, reading as sequence, lick, pedal,
breath, descending sequence, lick, breath, pedal. The blues, which is swung and
so gets an eighth grid, went from 3.3 to 6.8.

Two things learned building it. Devices are drawn by **weight**, not evenly -
an even draw gave four plain runs in eight phrases and never fired the lick
once. And the anti-repeat looks back **two** phrases, because a pedal at both
ends of a solo reads as one idea used twice however far apart it lands.

Under a shuffle the grid drops to eighths, because the swing pass removes
anything that is not on a swung eighth and a sixteenth run would otherwise come
out with half its notes missing. `plan.swing` is threaded into the generator
for exactly this.

**The harness could not have caught the original fault.** Every phrasing check -
one voice, in range, mostly stepwise, stops to breathe - passed while the solo
played 3.3 notes a bar, because a line with almost nothing in it phrases
beautifully. There are now checks for notes per bar, for restating an idea (any
four note pitch pattern occurring twice), and for using more than one octave.

### If the solo needs adjusting

| complaint | where to look, in `generateSolo` |
|-----------|----------------------------------|
| too busy / too sparse | `slotsPerBar`, and the `weights` array - more LAND is more air |
| too mechanical, too scalar | lower the RUN weight further, raise SEQUENCE and LICK |
| not enough repetition | the LICK rebuild odds (`rng.chance (0.45)`) - lower means the motif is kept longer |
| never rests | the `mustLand` chance, and the whole-bar skip at the bottom of the loop |
| wrong register | `voice.tonic` picks the lowest tonic in the profile's chord zone; the whole line hangs off it |
| wrong landing notes | the chord-tone search on the phrase's last step |



**The metal preset was drums and bass.** `demo-metal.json` and `demo-rock.json`
predate guitar and piano support and never gained the two profile lines, and a
part with no profile named is never generated at all - so the parts were not
quiet, they did not exist. Both name all four now and every section says what
the guitar and piano play. The metal plan keeps the piano out of everything but
the half-time bridge on purpose; a piano comping a chorus at 168 fights the
rhythm guitar for the same space and wins nothing.

**Nothing caught it**, which is the more interesting half. The parity numbers
pinned in this file are drums and bass only, so a plan could lose half the band
and still pass every check. The harness now asserts that any part a plan names a
profile for actually reaches the sequence. That immediately found `preset-punk`
declaring a piano profile directly under a comment saying it deliberately has no
piano; the stray line is gone.

**The solo broke per-section reroll isolation.** `generateSolo` drew from the
shared song rng, and it draws a variable number of values - how many depends on
how many phrases it plays and whether each is answered - so every section after
a solo drew from a different place in the stream. It has its own derived stream
now. Nothing else in `generatePhrasePart` touches the shared rng, which is why
nothing else had ever broken this.

**The swung section recount was off by a boundary note.** Humanize nudges a
section's own downbeat a tick or two *before* its start, and the recount added
in session 8 attributes by tick, so that note was counted in the previous
section - and rerolling one section changed the number reported for its
neighbour. The window is shifted back half a subdivision now. Confirmed against
the rendered MIDI: blues had drum hits at 7678 and a bass note at 53758 sitting
just the wrong side of a boundary. Per-section counts sum exactly to the totals.

**Latency is shown in the footer**, bottom right, on every screen. It reads a
flat `latency 0.0 ms   buffer 512`, which is the truth: events are placed at
their own sample offset inside the block the host asked for, so Ghostband adds
none. It is read from the processor rather than hard-coded, so if it ever does
take a lookahead the number moves on its own instead of becoming a lie.

### The About screen, botched twice, and why

The first fix was real but incomplete. Session 8 fixed the *measurement* - it
had been handing `drawFittedText` a fixed height per paragraph and letting JUCE
stack the overflow. What was left was a **repaint** fault, and it looked
identical from the outside.

`resized()` never called `repaint()`. Every other screen is built from child
components, and a component that is moved or resized redraws itself, so those
screens heal on their own and nobody ever missed it. The About screen is painted
straight onto the canvas and its text is wrapped to the width it was painted at.
Growing the window redrew only the newly uncovered strip, so the old narrow wrap
stayed underneath the new wide one - two layouts of the same paragraphs on top
of each other, each with pieces of the other missing. `resized()` repaints the
whole canvas now.

**Neither the overlap checker nor the snapshots could ever have caught this.**
Both render into a fresh image, which is a full repaint by definition; the fault
only exists on a screen already painted at a different size. The snapshot set
now includes About at 1020x1400 because that is the size it broke at, and that
does catch layout faults at large widths - but a resize fault needs a live
window. Worth remembering before trusting a green snapshot run on this screen.

### Install merges now instead of clobbering

`Install.bat` no longer xcopies over the bundle's profiles. `ghostband.exe
install-profiles <from> <to>` copies each file but carries across the four
blocks the plugin writes back - `controls`, `notes`, `lowest_note`,
`chord_zone` - wherever the installed copy differs from what was shipped last
time. A pristine copy of each shipped profile is kept in
`Contents/Resources/.profiles-shipped` so a user's edit can be told apart from
a default that simply changed between versions; with no such copy, anything
differing from the incoming file is treated as a user edit, which is the safe
way round. It prints every block it keeps.

Verified both directions: a mapping that existed only in the bundle survived an
install and was reported, and a changed shipped default still reached a file
nobody had touched.

### The MODO master volume knob, which is not a Ghostband bug

Reported as the bass volume control being dead after a reboot, fixed by
re-teaching and saving the map. Worth writing down because the evidence says
otherwise: `profiles/modo-bass-2.json` was rewritten by that save at 05:49 and
is **byte-identical to HEAD** - the mapping (CC 22, follows `level`, channel 1)
was already correct on disk and was never lost. Ghostband also restates every
level at the top of each run, so it is sent without touching the knob.

What was lost is MODO Bass 2's own MIDI Learn - its memory that CC 22 drives
master volume. That lives in MODO's plugin state, which Gig Performer stores
inside the saved gig. Teach it, then **save the gig**, or MODO comes back with
no learn and Ghostband's CC 22 goes nowhere. Re-teaching in Ghostband will
appear to fix it while changing nothing in the file, which is exactly what
happened.

One real hazard found while checking this: `Install.bat` copies `profiles\*.json`
over the bundle with `xcopy /Y`. Anyone whose profiles resolve to the installed
bundle rather than to this repo loses their taught mappings on every install.
It has not bitten this machine - saves here land in the repo copy - but it is a
live trap for a user without the repository.

## The solo generator, since it is new and the thing most likely to be worked on

Lives in `generateSolo` in `engine/src/Render.cpp`. Built out of phrases rather
than notes, because that is what separates a solo from a scale:

- a phrase gets a **rhythm** from a pool that is deliberately full of holes
  (`soloRhythm`), a **shape** - rising, falling, an arch, or holding - and a
  **note to land on**;
- it then rests, and the next phrase sometimes **answers** it by reusing its
  rhythm with different notes;
- it moves mostly **by step**, leaping only occasionally, because a line that
  leaps constantly reads as arpeggios;
- every phrase **lands on a chord tone** of whatever chord is underneath, which
  is how it agrees with the harmony without tracking it note for note;
- over blues it draws from `soloScale` - minor pentatonic plus the flat fifth -
  rather than the mode, because the mode's major third fights the blue third.

The knobs, if it needs adjusting:

| complaint | where to look |
|-----------|---------------|
| too busy / too sparse | the `sparse` and `dense` pools in `soloRhythm`, and the `busy` threshold on section intensity |
| too jumpy | the `rng.chance (0.75)` step-versus-leap odds in the shape switch |
| aimless | the shape distribution, and how often a phrase answers (`rng.chance (0.45)`) |
| no room to breathe | the rest added after an unanswered phrase at the bottom of the bar loop |
| wrong landing notes | the chord-tone search on `last`, which looks within three semitones |

A soloing part stops comping - one player, one pair of hands - so the harmony
has to be held by something else in that section. `solo` is a phrase feel, set
per part in a plan (`"guitar": "solo"`). **It is not exposed in the Edit
screen**, which has never shown per-part feels for any value.

## Still to do

1. **The guitar solo needs work.** Ask what is wrong with it first.
2. **The SSD5 mix knob is the only dead one.** Its CC map is fixed - hi-hat and
   articulation functions, no MIDI LEARN buttons - so it cannot take a volume
   CC. But MODO taught us that right-click MIDI Learn on the control itself
   works where a typed-assignment page does not, and SSD5 does have MIDI LEARN
   buttons on its Map page, so the mechanism exists somewhere. A Gig Performer
   gain block is the fallback and works today.
3. **The user manual PDF.** Long agreed, never written.
4. **The AI planner.** The last planned feature. User supplies their own key.
5. **An all-UJAM profile set** as a second rig to A/B against.

## Session 8 - what changed

**The blues.** Three separate faults, in the order they were found:

The swing warp was correct and was being applied to the wrong thing. A shuffle
is a triplet feel and a straight sixteenth is not a rhythm inside one, but the
generators subdivide in sixteenths because that is what every other style wants
- so warping their output put onsets at 0, 0.30, 0.60 and 0.80 of the beat, four
notes lurching against a three-feel. Sixteenths are dropped before the warp now.

The bass was a drone: the verses asked for `roots`, one note a bar, which reads
as restraint anywhere else and as nothing at all here.

And the harmony was simply wrong. **A twelve bar in A is A7, D7, E7** - the flat
seventh over a major third is the sound, not a flourish. The engine parsed `A7`
and had `Dominant7` in its enum, then threw the seventh away: `Chord` reported a
third and a fifth and nothing else, so an A7 voiced identically to an A. That
was the one that mattered.

**A solo generator**, described above. Ghostband had no melodic generation at
all before this.

**The About screen** was unreadable - it handed `drawFittedText` a fixed height
per paragraph and JUCE drew the overflow on top of itself. Every block is
measured now. Contact details and an Email Kyle button added.

### Two things about the machine

**The standalone CMake at `C:\Program Files\CMake` is gone** and `vswhere`
reports only VS 2022 - no VS 18, which the old build cache demanded. `Build.bat`
failed with "cmake is not recognized". It now falls back to the CMake that ships
with Visual Studio, and everything was rebuilt from scratch against VS 2022.
**Both reference songs render note for note identically**, so the arrangement
does not depend on the toolchain.

**The overlap checker has a blind spot.** It walks the editor's child components,
so anything painted directly onto the canvas - which is what the About screen is
- is invisible to it. That is how a screen of overlapping text shipped.

## Session 5 — the control mapping system## Session 5 — the control mapping system## Session 5 — the control mapping system

The whole session went into one thing: letting the user drive an instrument's
own knobs, buttons, switches and selectors from the arrangement. It started as
three fixed knob slots and ended as an open table, because their instruments
have far more controls than any list could have guessed at.

**The shape they asked for, in their words:** "you pick the instrument, next to
it is a blank button you select and next to it is the teach this knob button. it
then saves to a list underneath, showing all buttons i mapped, however many
there is to map, all with custom labels so i know what they are." That is what
Settings now does.

**Four control types, because the widget is not the point — the behaviour is:**

| type | behaviour | widgets |
|------|-----------|---------|
| `knob` | sweeps continuously | knobs, sliders, faders |
| `switch` | fully off or fully on | buttons, toggles, latches |
| `select` | holds one of N choices for the section | dropdowns, multi-position switches |

A stepped slider is a `select`, not a `knob`. Selector positions are spread
endpoint to endpoint (p/(n-1)), which is how a host maps a stepped parameter.

**The follows vocabulary grew twice, both times because the user hit a real
gap the list did not cover:**

- `intensity`, `lead`, `peaks`, `rising` — as before.
- `fixed` now takes a **VALUE**, in the control's own units. It previously
  parked everything at the bottom of its range, which is why seven of the
  user's nine mappings were silently being driven to zero.
- `none` — send nothing at all, leave the instrument as set. This did not
  exist, and `fixed` was being used for it, which does the opposite.
- `random` and `random once` — for controls with no right answer: which amp,
  which character, which pedal. In their words, these "can be changed at any
  time for any reason, depending on the sound you are going for." `random once`
  holds one choice for the song (nobody swaps amps mid-song); `random` picks
  again each section (a pedal in for the chorus).

**RANGE** — two ends in the control's own units, for any driven control. Putting
the higher number first **inverts** it, which is the whole answer for a control
that reads backwards. Narrowing it keeps a rolled selector inside one bank of a
long list.

**Send and Walk.** Teach sweeps fast, which is what MIDI Learn needs and useless
for reading an instrument's display — the user tried to count a list from one
and could not. `Send` parks a control on the typed value and sends it once.
`Walk the list` steps a selector through every position at half a second each,
slow enough to count.

### Two real bugs found by reading their saved data, not by testing

1. **Save destroyed the profile it saved to.** `toJson()` rewrote the whole file
   and took all forty comment lines of `vg-iron2.json` with it — including the
   measured 60-89 range finding. `save()` now splices only the controls block.
   The harness loads a saved file back and asserts the findings are still in it.
2. **A switch could be parked off but never on.** `fixed` drove the value to
   zero before the declared range applied, so a latch button was unmappable.

### Asked for, agreed, not yet built

- **An all-UJAM profile set as a second rig.** The user owns Virtual Drummer and
  Virtual Bassist as well as IRON 2 and Virtual Pianist. They considered
  switching to it wholesale and were talked out of it - UJAM's drummer and
  bassist are phrase players, so Ghostband would stop writing the parts and
  start picking from UJAM's prerecorded grooves, losing the kick/bass lock and
  intensity actually driving the playing. But they do want the profile set built
  later so the same song can be A/B'd through both rigs. Profiles are data;
  nothing in the engine has to change for it.
- **Possibly replacing IRON 2 and Virtual Pianist with dedicated instrument
  libraries** - something less player, more instrument. Adding one is a JSON
  file. Making a sampled guitar sound like a guitarist rather than a keyboard
  playing guitar samples would need guitar articulations - palm mute, slide,
  hammer-on - the way BassProfile already has them. PhraseProfile has chord
  zones, strum spread and phrase keys but no articulation system.

### The mix knobs, and where each part's volume lives

Guitar and piano work: a control taught with follows "level" is driven by the
part's mix knob. Both were confirmed moving the real instrument.

**SSD5 cannot be volume-controlled over MIDI at all.** Its Map page has a CC
mode, but unlike Notes mode the rows carry no MIDI LEARN button - the CC map is
fixed to hi-hat and articulation functions and is not user-assignable. A Gig
Performer gain block after SSD5 is the answer for drums, not a workaround.

MODO Bass 2 has a real Control page for assigning CCs to parameters, so its
volume should be reachable. Not yet done.

### Still open from this session

- The user is mid-way through mapping IRON 2 and Virtual Pianist. The finisher
  preset list is unresolved: it shows 30 FX and 32 Ambience entries, and whether
  that is one 62-way parameter or two separate ones was not determined. `Walk
  the list` and `Send` exist to answer it — VALUE 1 then VALUE 31 says which.
- An adversarial review of the whole control system was running at end of
  session (six dimensions, each finding challenged by a skeptic). Its results
  were not read. Re-run it or check the workflow transcript.
- Profiles remain `[UNVERIFIED]` for SSD5 and MODO — still needs a Calibrate run.

## Session 4 — what changed, and what is still open

The user tested and reported three faults, all real and all mine:

1. **Guitar and piano silent.** `loadBuiltInPlan` never called `resolveProfiles`,
   so profiles were never loaded and those parts were never enabled; and the
   built-in plan named no profiles at all. Fixed, and profiles now ship inside
   the VST3 bundle so the built-in song works on any machine.
2. **The guitar cut out after a fraction of a second.** Phrase keys must be held,
   not tapped — measured at 3% sustain for a 50ms press. Fixed.
3. **Calibrate showed only drums.** Same root cause as (1); it now covers every
   part the song has.

The user then switched IRON 2 and Virtual Pianist to **Instrument mode**, which
needs a different profile from Player mode. Instrument mode is now the default.

**Still open, in the user's words:**

- The interface has been redesigned flat in red / purple / black / silver /
  white, per their direction, but they have not yet said whether it lands.
- **Settings and About are a first pass.** Settings has per-part MIDI channel,
  reload profiles and reset window size. About has version, author, description,
  licence and a manual button. They asked for "the usual stuff" — expect gaps.
- **The user manual PDF does not exist.** Agreed to write later; the About
  button falls back to the README until it does.
- Whether Player mode now sounds better than Instrument mode is untested.

## State

Public at <https://github.com/mourninggrace/Ghostband> under **AGPLv3**. Free,
with a Ko-fi donate button (`ko-fi.com/kyleyeroshefsky11806`). Not being sold —
see `COMMERCIAL.md`, which is retained for its licensing research but no longer
describes the plan.

**Working, in the user's Gig Performer rig:**

- Four parts — drums, bass, guitar, piano — driving SSD5, MODO Bass 2,
  UJAM IRON 2 and UJAM Virtual Pianist (both in **Instrument mode**).
- Live section jumping, landing on the next bar line.
- Rerolling, whole song or ctrl-clicked sections only.
- In-plugin calibration covering every part, tuned by ear.
- Song structure editor with save.
- Per-part level knobs, sent as MIDI CC 7.
- An unlimited control mapping table per instrument, built in Settings and
  saved into the driver profile. Each entry is named by whoever made it and is
  a **knob**, a **switch** or a **select**; each says what it follows, and a
  driven one has a range while a parked one has a value. Teach sweeps the CC so
  MIDI Learn can latch on; Send and Walk exist for reading the instrument back.
  See the session 5 notes above for the full vocabulary.
- Five screens: song, calibrate, edit, settings, about.
- Flat UI in red / purple / black / silver / white, resizable, drawn
  procedurally so it scales across mixed-DPI monitors.

**The rig needs one MIDI Channel Constrainer per instrument** (drums 10, bass 1,
guitar 2, piano 3), because instrument plugins are omni. Channels are now also
settable in the plugin's Settings screen.

**Build:** `Build.bat`. JUCE 8.0.15 is a pinned submodule; clone with
`--recursive`. The C++ runtime links statically, so the plugin has no
dependencies beyond Windows.

**Install:** `Install.bat`. It refuses to run while Gig Performer is open,
because a host holds the DLL and the copy would silently do nothing.

**Driver profiles carry the findings.** The comments in `profiles/*.json` record
what was measured rather than assumed - IRON 2 sounds from 60 to 89 and is
silent below it, which is why the chord zone is 60-84 and not a guitar's range.
Saving a mapping from the plugin splices only the controls block, so those notes
survive; do not replace that with a whole-file rewrite.

## The only remaining planned item

**The AI planner.** Claude writes the *chart* — chords, intensity curve, groove
choices, section shape — and the local engine renders it, exactly as agreed in
session 1. Decisions already made:

- **The user supplies their own API key.** Ghostband is donation-ware, so a
  per-generation cost against no revenue makes no sense.
- **It must remain entirely optional.** Everything works today with no key and
  no network; `autoProgression` already writes progressions locally. The planner
  raises musical sophistication, it is not a dependency, and playback must never
  wait on a network call.
- It needs a background thread. The sequence swap in the processor was built for
  this and is already safe for it.

## Waiting on the user

They are testing the current build and reporting back. The two things most likely
to have rough edges, because they are newest and least exercised:

1. **The structure editor loop** — Edit song, rearrange, rename, change chords,
   Save as. Entirely untested by anyone but the harness.
2. **How the redesign actually reads** on their monitors.

## Verifying a change

```bash
Build.bat
build\ghostband_plugin_test_artefacts\Release\ghostband_plugin_test.exe plans\demo-metal.json 1231 629
build\ghostband_plugin_test_artefacts\Release\ghostband_plugin_test.exe plans\demo-rock.json 996 423
```

Add `--snapshot <dir>` to render the editor to PNGs. Every other check is blind
to layout; rendering it caught three real bugs the first time it ran.

If a change is *meant* to alter the generated notes, those trailing counts will
change. Re-render all three demo plans, confirm by ear, then update the numbers
here and in the README.

## Things that will bite again if forgotten

- **Never send an unverified articulation.** A guessed keyswitch or controller
  can do far worse than nothing: MODO Bass went completely silent for whole
  songs because a mute CC nobody had checked was being sent as though it were
  known, while the notes beside it were perfectly correct. Notes are the safe
  part of a profile and articulations are the risky part, so a profile marked
  `needs_verification` now emits notes only. Diagnosing it took a MIDI dump
  showing CC 21 going out 43 times a song.
- **A test that hard-codes a number will eventually be wrong about it.** The
  harness pinned the host tempo at 168, which happened to be demo-metal's, so
  every check silently walked demo-rock at nearly twice its speed and then
  counted the notes it had missed. It reads the song's own tempo now.

- **A generated writer must never clobber a hand-written file.** Saving a
  mapping used to rewrite the whole profile from the struct, which threw away
  every comment in it — and in these files the comments are the measured
  findings, not decoration. Splice the one block that changed.
- **Rolled and derived values must draw from their own seed stream.** Adding a
  control that uses the section generator's RNG would shift every note of a song
  that was already right. Each one derives from the song seed and its own name.
- **When the user reaches for an option that is nearly right, the option they
  want is probably missing.** Seven of nine mappings on `fixed` was not a
  preference; it was the absence of `none` and of `random`. Read the data they
  produce, not just the faults they report.

- **Never ask this user to verify a MIDI map by ear against a stopwatch.** It was
  tried; they have no DAW and it was an unreasonable ask. Build measurement tools
  instead — `ghostband_probe` and the in-plugin Calibrate exist for this.
- **Install with `Install.bat`, never PowerShell `Copy-Item -Recurse`.** Copy-Item
  onto an existing directory copies *into* it, leaving the old DLL in place while
  appearing to succeed.
- **Never round or narrow a value that feeds the generator.** The RNG stream is
  chaotic; holding the dials as `float` once made the plugin and CLI produce
  different songs from the same seed.
- **Do not trust `getSampleRate()` blindly.** It returns 0 until the host sets it,
  and guessing a fallback plays at the wrong speed and drifts silently.
- **All Notes Off is not enough** to stop notes — many instruments ignore it. The
  processor tracks what is sounding and releases each note by name.
- **Instrument plugins are omni.** MIDI-channel separation does nothing on its
  own; the user's rig uses Gig Performer's MIDI Channel Constrainer, one per
  instrument. Do not resurrect the multi-instance workaround.
- **`ghostband_probe` cannot make SSD5 or MODO sound** — almost certainly
  licensing. Ruled out headless format, message pumping, and editor creation. It
  does work on UJAM plugins.
- **Reference counts in the docs go stale** whenever seed derivation changes.
  That has already caused one false parity failure.

## Measured facts worth keeping

- **UJAM instruments have two modes and each needs its own profile.** In
  *Instrument* mode they play the notes they are sent; in *Player* mode you hold
  a chord in a low zone and hold a phrase key. Using the wrong profile sounds
  broken rather than merely wrong. Instrument mode is the default here; the
  measured Player mapping is kept in `vg-iron2-player.json`.
- **Phrase keys must be HELD, not tapped.** Measured: pressing one for ~50ms
  gives 3% sustain and the sound stops after 0.17s. This was reported as "the
  guitar cuts out" and it was real. Note how it was missed first time — the
  earlier sustain test held the key for the whole measurement, so it answered
  "does this key keep playing" rather than "does Ghostband hold it long enough".
  **When a test passes and the user still reports a fault, check the test is
  asking the user's question.**
- **IRON 2** (player mode): notes 12–59 silent (chord zone), **60–89 phrase
  zone**, 90–120 silent. Density by attacks over 5s: key 79 = 1, 73 = 2, 70 = 4,
  67 = 7, 88 = 16. The first mapping was guessed from pitch and had it backwards.
  Measured on style *"120 bpm – Arm Pit"*.
- **Virtual Pianist** could not be mapped by probe — output ~5× quieter, no
  phrase signature, boundaries moving between runs, AssetManager errors on load.
  It is driven as a plain piano instead, which is the safe failure mode.
- **MODO Bass 2** exposes 2081 parameters named `MIDI CC 0|n` — a complete CC
  map, which will make its articulation profile straightforward.
- **The kick/bass lock** sits at 89–93% of bass attacks within 12 ticks of a
  kick, averaging 2.5 ticks ahead. This is the thing that makes it sound like a
  band; check it survives any change to groove generation.

## Ideas raised but not scheduled

- **"Connect it to any plugin and it just works."** Reachable. Generic profiles
  already cover GM-compatible drum plugins and ordinary pitched instruments; a
  known map is a small JSON file; and auto-mapping by measurement is proven on
  UJAM. The blocker for licensed plugins is that they will not sound in a
  headless host — the answer is the vestigial audio *input* pins: route an
  instrument's audio back into Ghostband and it can measure its own output from
  inside the host, where everything is licensed and working.
- **Latching section loop** — click once and a section repeats until told
  otherwise. The jump-offset machinery already supports it.
- **Live following** — Ghostband comping behind what the user plays. The largest
  unbuilt idea, and the one that would need chord detection and tempo tracking.
