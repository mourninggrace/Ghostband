# Where things stand

Short on purpose. This file had grown to a thousand lines of accreted session
history and listed things as open that had been finished for weeks, which made it
useless for the one job it has: telling whoever picks this up next what is true
right now. The history is in `docs/archive/NEXT-through-session-13.md`, and
nobody has to read it.

**Last touched 2026-09-24, session 23.**

## State

- **v2.0.0 released 2026-09-24** (first cut as v0.7.0 the same day and
  renumbered at the owner's request - the planner was v2's headline). Tagged,
  published, marked latest, and its checksum verified by downloading it back from GitHub.
- **Unreleased on `main`:** the planner status-line fixes from session 23
  (in CHANGELOG under Unreleased). Installed on the owner's machine; not yet a release.
- **The AI planner works for real.** First live call 2026-09-24: "Slow Burn Iron",
  23 s, 3,588 in / 2,261 out (about 6¢), served by claude-opus-5-5 with no
  fallback. The owner: "awesome so far".
- **The window is 1180x820, minimum 1020x820.** Both numbers are load-bearing:
  the minimum is set by the two-column Settings screen, not by the song screen,
  and the harness's `kMinW`/`kMinH` must move with it.
- **479 checks** pass on every build, and all 34 plans are swept.
- **The reference pins hold:** `demo-metal` renders 1231 drum hits / 629 bass
  notes, `demo-rock` 996 / 423. If either moves, something changed that was not
  meant to.
- **Run the suite against both reference songs, not just the default.** Passing
  `demo-metal` alone hid a broken check for four sessions. All 34 plans pass as
  of session 17; the sweep script is three lines and worth repeating.
- 34 preset songs, 14 driver profiles, 10 colour themes.
- Working tree clean, `main` pushed.

## The files that matter

| file | what it is |
|---|---|
| [OPEN-QUESTIONS.md](OPEN-QUESTIONS.md) | anything needing the owner — read this first |
| [TODO.md](TODO.md) | the living list, including what is deliberately not being done |
| [BACKLOG.md](BACKLOG.md) | the engineering notes behind it |
| [CHANGELOG.md](CHANGELOG.md) | what changed in each release — update it in the same commit |
| [docs/MANUAL.md](docs/MANUAL.md) | the manual, and it is current |
| [README.md](README.md) | the front page: badges, hero, highlights, honest limits |

**Screenshots come from `tools\screenshots.ps1`**, which renders the real editor
through the harness. When the UI changes, run it and commit what it writes. The
README's check-count badge is a number and has to be moved by hand.

## How to verify a change

```bash
Build.bat
build\ghostband_plugin_test_artefacts\Release\ghostband_plugin_test.exe
```

Add `--snapshot <dir>` to render every screen to PNG. **Look at them.** The
overlap checker is blind to painted content, to clipped text, to contrast, and —
until session 16 taught it otherwise — to a control laid out at zero size. Two
features shipped invisible because nobody opened the images.

Add `--audit [plan]` to print, for every screen, the bounds of every VISIBLE
child and a count of the hidden ones. This is the fastest way to answer "why is
that control not there" and "where is all the empty space", and it answers in
numbers rather than in opinions. It is a look, not a test: it runs and exits
without checking anything.

Run against **both** reference songs, and ideally all 34 plans:

```bash
for /f %p in ('dir /b plans\*.json') do build\ghostband_plugin_test_artefacts\Release\ghostband_plugin_test.exe plans\%p
```

To cut a release: `Release.bat`, then `gh release create`. It refuses a dirty
tree or a failing build.

## THE LOCK ORDER RULE

Read this before touching the processor.

**Never hold `sequenceLock` and `stateLock` at the same time, in either order,
and never let the audio thread block on `stateLock` at all.**

Breaking it froze Gig Performer solid on 2026-09-12: `processBlock` took
sequenceLock then stateLock, `getBeatTicks` took stateLock then sequenceLock, and
the tracker called the second thirty times a second. It does not present as an
audio glitch - the message thread SPINS on a SpinLock it can never take, so the
host stops repainting and stops answering the mouse.

`processBlock` now takes no stateLock at all; the one value it needed is
published to an atomic when the plan changes. There is a stress test that
hammers every accessor the editor's timer uses against a running audio thread.
**If that test hangs, this rule has been broken again** - there is no way to
report a deadlock from inside one.

Related: anything the message thread calls at timer rate must not walk the whole
sequence under that lock either. The audio thread takes it with a TRY-lock, so it
does not wait - it skips the block and sends nothing. `getTrackerCells` binary
searches the window instead.

## THE SECOND STALL IS MEASURED, AND IT IS NOT GHOSTBAND

Reported 2026-09-12:

> "sometimes the ghostband UI is frozen, playhead not moving, screen not
> changing, but audio is still heard like normal and then suddenly it will start
> working normally again"

**Not the session 16 deadlock.** That took the host down and never recovered.
This keeps the audio thread running and releases on its own, so the message
thread is being starved or blocked for seconds, not deadlocked.

**It was measured before anything was built** (`--timing` in the harness), and
the result killed the theory that was written here last session:

```
regenerate (a reroll)        1.221 ms
getTrackerCells  bar         0.040 ms
full repaint  16th           4.392 ms
```

Against a 33 ms budget. Nothing Ghostband does per frame can produce a
multi-second freeze, and the editor's timer is not spending its time in our
code. So there is nothing to fix by reasoning, and six wrong theories on the
Shreddage register is enough of a lesson about doing it anyway.

**The plugin catches it itself now.** `GhostbandEditor::noteTimerTick` records
three numbers whenever the timer misses its slot by more than 250 ms:

| measure | what it tells you |
|---|---|
| the GAP between callbacks | long gap + short work = we were never called. Someone else's message thread |
| the WORK inside one callback | long work = ours |
| `processor.audioBlocks` delta | still climbing = only the window was stuck; flat = the whole plugin was held up |

Shown in the footer beside the latency, explained in its tooltip as a
conclusion rather than as numbers, and appended to
`%APPDATA%\Ghostband\stalls.log` — because the window that would show it is
the thing that was frozen.

**IT HAS BEEN CAUGHT, and the answer is not us.** Eleven freezes in twenty-one
minutes on 2026-09-13 - 29.5 seconds of frozen window, worst a single 11.6
seconds. Every one recorded:

```
gap 11580 ms   ghostband 0.0 ms   audio 1086 blocks of 512 at 48.0k
```

Every gap divides by its block count to 10.6-10.8 ms against a theoretical 10.67
for 512 samples at 48k, so the audio thread ran continuously through all of
them, and Ghostband spent no measurable message-thread time - a figure that
includes PAINTING and every handler. The host's message thread was held by
something that is not this plugin.

**Not further diagnosable from inside Ghostband.** What is left is the owner's
to run: Windows Defender real-time protection and behaviour monitoring are on
with no exclusions (Kontakt streaming samples through a scanner is the classic
cause of exactly this), and bisecting the rackspace would settle it. Both are
written up in OPEN-QUESTIONS.

## Session 23: the planner's first real song, and a line you could not read

The first live planner call worked first time: 23 seconds, about 6¢, and the
owner liked the song. The only fault was on screen.

- **The explanation was cut off.** `writeStatus` is one 18px line in the rail and
  the planner writes one or two sentences. Making it taller was tried first: six
  lines is 68px, and at the 820px minimum that pushed the GTR 2 and PIANO mix
  rows to zero height (the zero-size check caught it). The owner chose from three
  mockups: **one line, cut at a word with "... more", and a click opens a
  `CallOutBox` with all of it.** `WriteStatusLine` in PluginEditor.h holds the
  full text; `fitWriteStatus()` fits it with the label's own font.
- **A refusal flashed and vanished.** `startWriting()` set the label directly and
  the next timer tick's `refreshPlannerControls()` overwrote it with an empty
  line. Refusals and the undo messages are now kept in `writeNote` and shown by
  the refresh itself, until the next write.
- Two new checks, both about what the owner can actually read: a refusal
  survives a refresh; a two-sentence explanation shows as one unsquashed line
  ending in "... more", and a click opens all of it. The fitting check failed
  against the old layout (6 lines needed, 1 available) before it was trusted.

## Session 22: the planner ships, and the guitar stops muting itself

Ended with **v2.0.0** (first cut as v0.7.0, renumbered).

**THE PALM MUTING WAS A PROFILE BUG, TWO WEEKS OLD.** The Hydra's per-note
gestures carried CC 40 values for a six-band layout set up on the morning of
2026-09-07; the controller was re-banded to five section articulations that day,
the note above the block was rewritten to say "declared empty" - and the block
was never emptied. Under the real layout a rake was a palm mute, a pinch was a
staccato and a tap was a POWER CHORD: 10.7% of lead notes, always the exposed
ones. Found by reading the profile against its own comments, then MEASURED before
being fixed. **Lesson: when a comment and the data under it disagree, the data is
what runs.**

**SOLOS STREAMED; FILLS SECTIONS WENT DEAD.** Measured in seconds, the two
complaints were different problems. Solos were 5% silent but half sixteenths and
2% a beat or longer - so each phrase now picks a pulse. Fills sections were 77%
silent with gaps up to 44.7 s - so between answers the second guitar picks a
soft arpeggio of the chord (`LeadIntent::bed`), breaking off before each answer.
The pulse weights are NAMED BY STRIDE: on a shuffle one step is already an
eighth, and weights written as note values made the blues solo 60% quarters.

**THE PLANNER.** Engine half pure and checked on canned responses; plugin half
with a DPAPI-encrypted key (no getter anywhere), a cancellable WebInputStream on
its own thread, a weak-referenced callback, and written songs saved as ordinary
plan files in Documents\Ghostband\Songs\Written then loaded through loadPlan.
Model chosen by the owner: `claude-opus-5-5`, effort `medium` set explicitly.

**A PRIVACY LEAK FOUND BY WRITING THE MANUAL.** The song sent as context carried
its profile file paths. Found while writing "what is sent"; stripped in
`makePlannerBrief`, and checked - the check was made to FAIL with the leak put
back before it was trusted.

**THREE WAYS A CHECK LIED, ALL CAUGHT:** a check with a side effect in its
arguments (MSVC evaluates right to left, so the detail printed pre-undo state); a
username that is a prefix of the word "string" (search for a PATH, not a
substring); and a restored source file whose older timestamp made the build skip
it, so the "restored" binary still had the bug. **Touch a file you restore.**

## Session 21: the lead guitar, and a dial for instinct

Ended with **v0.6.0**.

**THE COMPLAINT WAS STRUCTURAL, AND THE CODE SAID SO ONCE IT WAS MEASURED.**
"The fills and the lead guitar always sound the same almost from song to song."
Five cell shapes, two devices making up 83% of every fill, and - the one that
matters - the LICK COULD ONLY GO UP. Its motif incremented the degree, full
stop, and the lick is the most-used device there is.

Measured across 34 songs at four seeds: 57.6% of lead phrases ascending against
17.3% descending. Three and a half to one. Now 26.6 / 26.6.

**THE MEASUREMENT IS THE LESSON HERE.** The first metric tried was "distinct
interval shapes as a fraction of phrases played", which moved from 71.3% to
75.1% and said the change was marginal. It was the wrong metric: a lick
transposed up a tone is a new shape and the same lick. What an ear calls
sameness is CHARACTER - direction, interval class, phrase length - and measuring
that found the ascending lean in one pass. When a number disagrees with the
report, suspect the number.

**AND THE RHYTHM OF A FILL WAS A METRONOME.** Bar 4, 8, 12 or the last, always
starting exactly half way through. Twenty-two cells and nine devices do not help
a metronome. Now: the fourth bar is the commonest rather than the only one,
five start positions including a pickup that crosses the bar line, and never two
bars running.

**TWO CHECKS SAID "ONLY" AND "NEVER", AND WERE HALF THE REASON IT COULD NOT
CHANGE.** Pinned exactly, they made the identical rhythm a requirement. A
tendency is worth holding; an absolute is what makes an engine sound like an
engine. They are majorities now, plus a new one that FAILS if fills land only on
the fourth bar - the check that would have caught the original complaint.

**INTUITION, and the rule that made it safe.** A fourth dial: how much the band
plays what it feels like rather than what is obvious, reaching the lead, the
bass and the drums. Every use goes through `gb::byIntuition`, which returns
today's number precisely at 0.5 - so adding a control changed nothing about the
34 presets and left both pins untouched. That rule is the reusable part: a new
global control whose default is A SENSIBLE MIDDLE rewrites everything; one whose
default is THE OLD BEHAVIOUR costs nothing to add.

**TWO LATENT BUGS FELL OUT, both older than the work.** A lead line could play
two notes on one tick - the monophonic pass clamped each note to the gap before
the next and skipped the zero case, which is the one case clamping cannot fix.
And a grid column is 114 pixels of which the note, velocity and hit count were
spending 96, so a control change drew "cc" and its number fell off the end. In
every window size this plugin has ever had.

**A KEYSWITCH IS NOT A NOTE.** `A7` in the GTR 2 column was defended in the
v0.5.0 notes as the grid showing the wire. It was also hiding real notes: a
switch goes out at a fixed velocity and won the one line a cell has.

## Session 20: an audit, motion, Hydra's neck, and a die

Ended with **v0.5.0**.

**THE DICE.** One click rolls the song's whole character - seed, all three
dials, tempo, key, mode - and ctrl-click picks a different preset first. It
rolls WITHIN MUSICAL BOUNDS: the tempo is nudged a quarter either way rather
than redrawn, because a tempo drawn evenly from 40..250 is nonsense most of the
time, and the modes lean minor because this is a rock engine.

**Right-click puts it back, and that is the part that matters.** Rolling past
one you liked with no way back is what would make it frustrating instead of
fun, and it is the sharp end of this project having no undo. One step; the
state is kept as the song's own text, which is what a take stores. Pinned by
FINGERPRINT rather than by the dials - the dials are what was rolled, and a song
returning with the right numbers over different notes would pass a check on them
and be wrong.

**HYDRA'S NECK, and it needed the manual twice.** See the section above for the
fretting modes. Session 20 also added Set Hand (p28), which is unusual: ONE
keyswitch note where the VELOCITY is the fret. Heavy work at fret 1, answers at
5, solos at 9 - and Moving Lead ascends from wherever the hand is, so the two
together are most of what separates a lead voice from a rhythm one.

**Force String is deliberately NOT used.** The manual says it "sets up a
keyswitch note for each string, plus a note to disable" without saying which
note is which, and a switch that pins every note to the wrong string is a worse
failure than not having it.

## Session 20 details: the audit, and motion

**An external audit reported 29 findings and most of the headline ones were
not there** - the CRITICAL and three of eight HIGH describe code that does not
exist. Every finding was checked one at a time and the verdicts are in
`audit/VERDICT.md`, so nobody re-runs it against the same false positives.
Six were real, and the best of them led somewhere the audit had not looked:
`parseChord` tested `"M7"` AFTER lowercasing the suffix, so that branch was
unreachable and **CM7 played as C minor seven**.

**THE LESSON THAT KEEPS EARNING ITS KEEP: read the instrument's manual.**
Asked whether Hydra's fretboard mapping could improve the second guitar. The
premise was Keyboard Mode (p9), which DISABLES the string-selection algorithm -
the opposite of useful. What is worth having is on p16: four keyswitches that
set the Fretting Mode, which is Ghostband's own mechanism and needs no MIDI
Learn. Moving Lead (116) gives a solo three octaves at one hand position where
the default gives two; Polyphonic (117) makes every note of an answer sound
rather than triggering legato. `profiles/shreddage-3-hydra.json` has a
`fretting` block now, and deleting it turns the whole thing off.

**Motion, and it took two attempts.** The first was a veil fading from the
background colour - correct, measurable, and invisible, because dark-on-dark
over 150 ms with a cubic ease-out is already at 12% opacity by its halfway
point. Reported as no animation whatsoever, fairly. **Opacity was the wrong
instrument**: the content now arrives 44 pixels SIDEWAYS over 300 ms and knows
which way, and every turnable knob leaves a trail between where it was and
where it is. The animation clock still sleeps when nothing is moving.

**And a playhead bug the owner diagnosed exactly**: "it's like the playhead
starts at 17 and doesn't start moving until the song passes 17". The grid keeps
the current row a third of the way down, so paint() lit `rows/3` - true almost
always, and false for the first third of a screenful, because the window cannot
scroll above bar one. It is computed from the tick now.

## Session 19: editing the grid, and a release

The song screen's grid is now clickable: a row opens a strip on the bar it is
in, carrying that bar's CHORD and its section's FEEL, plus reroll and a jump
into the structure editor. What it offers is deliberately what is AUTHORED - a
note in the grid is the output of a seed and a plan, there is nowhere to put a
hand-placed one and no way to keep it through a reroll. Editing a bar of an
automatic section PINS that section to what it was already playing; anything
else would let its other bars drift on the next regenerate.

**A note-overrides layer is the obvious next ask and has not been built.** It
would need somewhere to store a hand-placed note, a rule for what happens to it
when the section is rerolled, and a decision about whether a take carries it.

Row shading was rewritten. It shaded downbeats, and at one row per bar - the
default - every row IS a downbeat, so the grid was a wall of identical stripes.
Three tiers now, and the zebra alternates on position-in-bar where a bar holds
several rows and on BAR NUMBER where it does not.

Also: a change log (`%APPDATA%\Ghostband\changes.log`), polled from a snapshot
rather than reported from forty call sites, so a knob drag is one line; Reset to
profile in Settings; the song rewinds itself and pauses when it ends; the
transport button follows the state instead of only its own clicks.

**The lesson, and it is the same one as last session.** The edit strip's first
version overran at the window's minimum and drew the feel box on top of a
button. Nothing caught it, because the strip only exists after a CLICK and every
layout sweep walks the screens without touching anything. Both sweeps now
perform the gesture first, and there is a snapshot of it open. **A state that
needs a gesture to reach needs the gesture in the test.**

## Session 18: the rail, and catching the stall instead of guessing at it

Three screens rebuilt around a fixed 320px rail after the owner picked one of
two mocked-up directions - **mocked first, built second**, which is the standing
correction from session 15 and it worked: he chose in one message.

- **Song.** Controls in the rail, grid takes the rest. 802x572 where it was
  760x461. The five mix knobs became five ROWS, which is what finally gives a
  knob that cannot work a whole line to say why on.
- **Settings.** Two columns. It was one narrow column in a 1180-wide window.
- **Edit.** The form is a rail too. Its six rows had ended at six different x
  positions because each was as wide as its own contents - which is precisely
  what "things seem cock eyed" was.
- Takes, Calibrate and About are left alone: a short header over one long list,
  already using the full width, and they look right at the new size.

The property that is easy to miss and worth protecting: **only the grid resizes
now.** Every control keeps its position at every window size.

**Three faults found by LOOKING at rendered snapshots**, which is what NEXT.md
has told every reader to do since session 15:

1. Every middle dot in the interface rendered as `A-`. `juce::String`'s
   CONSTRUCTOR from a `const char*` decodes UTF-8; its `operator+` and
   `operator+=` decode the same bytes as Latin-1. Wrong since the mix labels
   were written. Use `juce::String::fromUTF8`, never `+ "\xNN"`.
2. The About screen was ruled through the middle by a hairline. It is the only
   screen with no status block, so it never calls `layOutFooter`, so
   `footerRule` kept whatever the LAST screen set. Painted onto the canvas, so
   invisible to every child-component check.
3. Shortening the window handed the theme picker an 8-pixel-tall box - the same
   fault, in the same place, as the one whose comment sits three lines above it.
   `removeFromBottom (jmin (74, jmax (0, h - 60)))` protects the LIST by
   shrinking the CONTROLS. **A list can scroll and a combo box cannot, so when
   they compete for the last pixels the list is what gives.**

Two checks were weaker than they read:

- The zero-size check tested "not zero", which is not the property that matters.
  A control a person has to hit with a mouse now needs 16 pixels; labels keep
  the floor of 4, because an 11-pixel caption is a legitimate thing.
- The overlap check ran at 600x720 - below every floor the window has ever had,
  testing a layout that cannot occur. Both layout checks and the snapshots now
  run at the window's real minimum, from one pair of constants.

## Session 17: nothing vanishes, and the row is yours to set

Started with "the piano mix knob is missing, which means there could be other
dials or buttons or whatever that is missing as well and I just don't realize it
yet." Swept all 34 plans through the editor and dumped the layout: the knob was
present on every song that has a piano and absent on the one that does not,
which is what the code intended. He confirmed it himself mid-session by loading
another preset.

**It was still the right complaint.** A control that silently disappears is
indistinguishable from one that is broken, and one silent absence makes every
other control suspect - which is exactly the doubt he described. His drum knob
is hidden *permanently* on an SSD5 rig for the same reason and he had no way to
know why. So all five mix knobs are now always laid out in the same places, and
one that cannot work is greyed with the reason written on it - `not in song`,
`no reach`, `CC7` - and the fix in its tooltip.

**The tracker got a ROWS selector**: bar, beat, 8th, 16th. One row per beat was
picked in session 16 without anything to compare it against, and his words were
"if i don't try them i have nothing to compare it to". `TrackerCell` now carries
a hit count, because at one row per bar a cell can cover sixteen hi-hats and
showing the loudest with no sign of the rest would be a lie. `TrackerView`'s
`beatTicks` was doing two jobs and is now `rowTicks` (how much music a row
covers) and `beatTicks` (where the beat is), and the bar.beat.sub label is
computed in ticks so it stays right in 5/4 and 7/8.

**A test had been picking the wrong section since it was written.** It looked
for `guitar2Feel != "silent"` - but a part switched off never reaches the code
that names a feel, so its feel is EMPTY, and `empty != "silent"` is true. On
`demo-metal` the first section happens to play the second guitar, so it passed;
`demo-rock` failed three checks the first time it was ever run. The comment forty
lines below the bug had described the trap accurately the whole time.

## Session 16: a tracker, tooltips, and the freeze

The song screen is a TRACKER now - one row per beat, one column per player, and
the contents are what Ghostband is actually SENDING: note, velocity, and any
articulation landing on that beat. Chosen from ten mockups across two rounds
after the arrangement grid was rejected for being another summary. A `Neon` theme
is the default; the view derives each part's hue from the palette and takes
saturation and brightness from it, so one implementation is neon on a dark ground
and ink on a light one.

Tooltips on all 92 controls, explaining the CHOICES rather than naming the
control, pinned per screen.

Fixed: the playhead ran past the end of the song (now parks at the last bar
line); every generated preset was called "Untitled" because the generator writes
"name" and SongPlan only read "title"; and the freeze above.

## Session 15: the interface was overhauled, and is not finished

The owner's words: "every app we create together has the same general look...
i'm looking for something fresh and flashy, something that's maybe never been
done before". Fair, and a fair hit on my defaults - dark panels, hairline
borders, label-on-the-left rows, every time. An earlier attempt the same day was
a palette change wearing a structural change's clothes, and he said so.

Three directions were mocked up before any code was written, and he chose the
arrangement grid drawn in a blueprint aesthetic.

**What exists now.** A new `ArrangementView` replaces the section list on the
song screen: sections run left to right, each as wide as it is long in bars; five
lanes run down, one per player, inked in proportion to how much that part plays
there; the intensity curve rides above and the playhead sweeps across. A
`Blueprint` theme - cool paper, slate ink, one blue accent - is the new default.
It was added at the END of kThemes on purpose: a saved session stores the theme
as a NUMBER, so inserting one at the front would repaint every existing rackspace
in somebody else's colours.

**What is NOT done.** Only the song screen has been reconsidered. Calibrate,
Edit, Settings, Takes and About all still use the old form layout and merely
inherit the new palette. The wordmark, the knobs and the buttons are untouched.
This is a first cut of a redesign, not a finished one.

**One engine change came out of it.** `SectionReport` now carries `guitarNotes`
and `guitar2Notes` beside the chord counts. A part playing fills or a solo writes
a LEAD LINE and no chords at all, so the second guitar was drawn as silent
through exactly the sections it is loudest in. Anything asking "how much does
this part play here" wants chords PLUS notes.

## Session 14, in one paragraph

Shreddage's silent low register is SOLVED after five sessions and six wrong
theories: its profile claimed a floor of 28, the guitar is drop-tuned to low E so
its real floor is 40, and MIDI 27 is the *thrash* keyswitch, which re-triggers the
last-played note. That one keyswitch explains every contradiction the fault ever
produced. 166 notes across 19 presets stop being thrown into a dead region. The
answer was in the manual, which is session 10's lesson word for word. Also
shipped: the structure editor's guitar 2 toggle, and an installer that tests
whether the plugin file is actually locked instead of scanning for a process name.
Built and then deleted in the same session: a wake note and a held-note
calibration step, both of which were solving a problem that turned out not to
exist.

## What is next

**Version 2, led by the AI planner.** Described for readers in the README under
*Where this is going*; the engineering notes are in BACKLOG.md. Nothing is
started.

One decision is open and nothing is scheduled:

- **Should the second guitar play chords?** It never writes more than two
  simultaneous notes today - overlapping pairs, which is what makes Shreddage
  play legato and is why the solos sound right. Real chords would be engine work:
  three- and four-note voicings. The owner has been asked.

## The one lesson worth carrying

Every fault this session was found by **measuring, not by reasoning**, and
several were things previously reported as done:

- The theme picker had never once been on screen. It was laid out at zero height,
  and the overlap checker skips empty bounds by design.
- Switching a theme left 62 of 66 labels on the old palette, and the drop-down
  menus on a palette nothing could reach.
- A recalled take played the same notes through a different tone, because the
  profiles re-chose effect settings at every section boundary.
- A test written for the popup menu **passed while the bug was live**, because it
  compared a stale colour against another stale colour instead of against what
  was actually painted.

The pattern: assert against what the user experiences, not against the thing you
just wrote. Then revert the fix and confirm the check fails. Every pin added this
session was proved that way, and the last one in that list is why it matters.
