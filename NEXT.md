# Where things stand

Short on purpose. This file had grown to a thousand lines of accreted session
history and listed things as open that had been finished for weeks, which made it
useless for the one job it has: telling whoever picks this up next what is true
right now. The history is in `docs/archive/NEXT-through-session-13.md`, and
nobody has to read it.

**Last touched 2026-09-12, end of session 17.**

## State

- **v0.3.0 released.** Tagged, published, and the asset's checksum verified by
  downloading it back from GitHub.
- **Session 17's work is committed but NOT installed and NOT released.** The
  owner had to leave before Gig Performer could be closed, so what is in his
  VST3 folder is still v0.3.0. First job next session: ask him to close GP5,
  run `Install.bat`, and let him try the ROWS selector.
- **322 checks** pass on every build.
- **The reference pins hold:** `demo-metal` renders 1231 drum hits / 629 bass
  notes, `demo-rock` 996 / 423. If either moves, something changed that was not
  meant to.
- **Run the suite against both reference songs, not just the default.** Passing
  `demo-metal` alone hid a broken check for four sessions. All 34 plans pass as
  of session 17; the sweep script is three lines and worth repeating.
- 34 preset songs, 14 driver profiles, 10 colour themes.
- Working tree clean, `main` pushed.

## The three files that matter

| file | what it is |
|---|---|
| [OPEN-QUESTIONS.md](OPEN-QUESTIONS.md) | anything needing the owner — read this first |
| [BACKLOG.md](BACKLOG.md) | the engineering list |
| [README.md](README.md) | the manual, and it is current |

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

## THERE IS A SECOND STALL, AND IT IS NOT FIXED

Reported 2026-09-12, session 17, and **nothing has been done about it yet**:

> "sometimes the ghostband UI is frozen, playhead not moving, screen not
> changing, but audio is still heard like normal and then suddenly it will start
> working normally again"

**This is not the session 16 deadlock.** That one takes the whole host down and
never recovers. This one leaves the audio thread running normally and releases
on its own after a while, which rules out a deadlock and points at the message
thread being *starved or blocked for seconds at a time* — the editor's 30 Hz
timer not getting to run, or getting stuck behind something slow.

Suspects, in the order worth checking:

1. **The editor's timer spinning on `sequenceLock`.** It is a `juce::SpinLock`,
   so the message thread BUSY-WAITS rather than sleeping. A regeneration holds
   it while it rebuilds forty thousand events; on a big plan that is not
   instant, and every 33 ms the timer wakes up and burns CPU fighting for it.
   Recovering "suddenly" is exactly what finishing a long hold looks like.
2. **`refreshFromProcessor` doing real work at timer rate.** Anything that
   rebuilds a list, re-reads a profile, or touches the filesystem in there will
   stall the same way.
3. **A `stateLock` hold on the message thread** while a background regeneration
   has it.

The cheap first move is instrumentation, not a guess: time each timer callback
and record the worst one seen, then show it beside the latency reading. Six
theories died by measurement on the Shreddage fault, and one manual page settled
it. Measure first.

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
