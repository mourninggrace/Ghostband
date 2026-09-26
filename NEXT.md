# Where things stand

Short on purpose. This file had grown to a thousand lines of accreted session
history and listed things as open that had been finished for weeks, which made it
useless for the one job it has: telling whoever picks this up next what is true
right now. The history is in `docs/archive/NEXT-through-session-13.md`, and
nobody has to read it.

**Last touched 2026-09-26, session 26.**

## State

- **v2.0.0 released 2026-09-24** (first cut as v0.7.0 the same day and
  renumbered at the owner's request - the planner was v2's headline). Tagged,
  published, marked latest, and its checksum verified by downloading it back from GitHub.
- **v2.2.2 released 2026-09-26:** the seven fixes from the session 27 audit.
- **v2.2.1 released 2026-09-26:** the saved-key fix (session 26). The 2.2.1 build is installed on the
  owner's machine and confirmed by him: his existing key reads, no re-entry.
  (Superseded by 2.2.2.)
- **v2.2.0 released 2026-09-26:** planner model/effort + cost line, takes slide
  in, the repeated-lead-note fix, measured buffer, section-title ladder, Load plan
  menu, and Install.bat clearing Kontakt's leftover GP5 (session 25).
- **v2.1.0 released 2026-09-26:** the planner status-line fixes (session 23),
  audio-side stall logging and the Gig Performer launcher (session 24), and the
  front page's "Coming in version 3" banner.
- **The AI planner works for real.** First live call 2026-09-24: "Slow Burn Iron",
  23 s, 3,588 in / 2,261 out (about 6¢), served by claude-opus-5-5 with no
  fallback. The owner: "awesome so far".
- **The window is 1180x820, minimum 1020x820.** Both numbers are load-bearing:
  the minimum is set by the two-column Settings screen, not by the song screen,
  and the harness's `kMinW`/`kMinH` must move with it.
- **508 checks** pass on every build, and all 34 plans are swept.
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

## Session 28: the lead guitar's fills, measured and re-timed

"The lead guitar plays a lot of the same fills... sounding the same song to
song, the fills specifically, not the solos." Measured before touching anything
with a new harness mode, `--fillstats [seeds]`: every answering phrase (not the
bed) in every fills section, fingerprinted by rhythm, contour and
rhythm+intervals, plus a cosine similarity of per-song rhythm profiles.

BEFORE: 46% of fills in three rhythms (4, 3 even eighths; 4 even sixteenths);
contour straight up/down 41%; 3-4 notes 69%; songs 0.43 alike, seeds of one song
0.45. The shared-in-3+-songs figure saturates at this corpus size and is not a
useful target; top-3 share and pairwise similarity are.

CHANGE (Render.cpp, answering only): `reshapeFill` (arch/leap-in) before the
landing is aimed; `retimeFill` after it, from FillFigure (12 figures, swing-safe
subset under a shuffle, capacity filter so figures do not thin phrases); per-song
`FillPersonality` from the song seed (figure weights x0.25-2.5, turn/leap odds,
start-position habit scaling Intuition's weights); not either of the last two
figures. Own streams, so SOLO CHECKSUM efd8d818 IS IDENTICAL before and after.

AFTER: top-3 11-13%, songs 0.11 alike, seeds 0.15. Fill answer notes down 15%
(7464 -> 6352), mostly the one-bent-note figure, which is sparse by design - ask
him whether fills now feel too sparse. The check "at one it takes every opening"
demanded ONLY bars 4/8/12/16; stricter than its claim since session 21, it broke
when the stream drew differently. It now asserts every opening taken.

**Then:** he heard fills still walking the scale (measured 23.4% stepwise, now
10.4% via the walk breaker in `reshapeFill`), asked fills to sit back in the mix
(per-section guitar 2 level, `kFillsLevel` 0.7), and feared the solos had been
broken ("slow single note pickers"). They had NOT: v2.1.0 vs now in all 33 solo
songs - 2596 notes both, 43.4% legato both; only the repeated-note fix differs.
Solos are now LOCKED by fingerprint (0x1b0d3285 at two seeds). He is nervous
about regressions - prove before claiming, and keep solos out of any fill work.

**My mistake this session:** `git checkout <file>` to "check" for changes
discarded the uncommitted harness work. Recovered by re-running the scratchpad
scripts in order (same numbers after). Commit before experiments; inspect with
`git diff`, never checkout.

## Session 28, part 3: optimisation, measured

`--perf [plan] [seconds]` in the harness: load, window, first frame, full redraw
(SOFTWARE image - createComponentSnapshot is a Direct2D image on JUCE 8 and costs
~150 ms per read-back whatever is drawn; the first report believed it), reroll,
CPU via QueryProcessCycleTime (GetProcessTimes moves in 15.6 ms steps = the whole
signal), medians of three, the harness loop alone as a floor (0.85%: it polls,
a host sleeps), per-piece message-thread ms via gbdiag::Profile, renderer.

Findings: load 6 ms, window 22 ms, first frame 16 ms, full redraw 11 ms (grid
3 ms), reroll 1 ms, audio ~0. Idle: the key-file check 30/s was the biggest
piece (2-4 ms/s -> 0.2, cached + plannerKeyGeneration). Timer drops to 10 Hz when
stopped, not animating, mouse elsewhere, planner idle: Ghostband's idle share
0.95% -> 0.75% of a core. Playing ~4%: mostly frame presentation of the 60 fps
glide/flare, which he said to LEAVE ALONE. Renderer is Direct2D (GPU) already;
nothing to move to the GPU, and OpenGL stays declined.

## Session 27: a bug audit, and seven real bugs

He asked for a comprehensive audit, find and fix everything. Done by hand, no
agents. Method: hunt the classes that have bitten before or that the suite is
blind to, and PROVE each with a check that fails before touching code.

1. **Locale + precision** (`Json.h` formatNumber/parseNumber, to_chars/from_chars).
   %.4g + strtod: de-DE/fr-FR wrote "120,5"; 4 digits lost dice-set dials, so a
   rolled song saved and reloaded changed. Check: C/de-DE/fr-FR exact round trip.
2. **No upper limits on load** (kMaxSectionBars 256, kMaxSections 128, time sig
   <=32 and a power of two, transpose +-24; Json::asInt saturates). 2,000,000 bars
   gave a negative length and a 36 s render. The section editor uses the limit.
3. **Chord re-strikes** (`PhraseProfile::render` chord path): strum offset let
   tones ring past the next chord; 26 in 5 songs. Collect, trim per pitch, emit.
   The plugin-stream check had passed - the plugin plays at its own dials - so a
   new check renders each song at its OWN settings (failed 15 on blues-3).
4. **UTF-8 paths** (`gb::utf8Path` everywhere the engine opens a file). Narrow
   paths meant non-ASCII usernames could not load/save. Check uses "Zoe-Mueller-Zh".
5. **Profile save deleted before rename** -> std::filesystem::rename replaces.
6. **Hanging notes after regenerate** (`releaseOrphanedNotes`, audio thread,
   after every swap): only 3 of 18 regenerate paths flushed. Check plays, rerolls
   mid-bar, plays 12 bars: demo-rock left ch 10 note 42 held.
7. (Session 26's key bug was the first of the kind; this audit swept for the rest
   of its shape - no other pointer into a dead temporary was found.)

Checked and found sound: every division by a song value is guarded; the planner
callback is a weak reference on the message thread; the old outside audit's
theme/channel-17/timer-wrap items were already fixed or guarded.

## Session 26: the key that would not stay saved - a dangling pointer

The logging added in session 25 paid off within the hour: "Windows error 13"
(ERROR_INVALID_DATA) four times, on a key file untouched since it was saved and
used successfully minutes earlier, and which decrypted fine from PowerShell with
the right entropy. So the ENTROPY was wrong at read time: `blobOf (std::string
(kEntropy, ...))` pointed at a temporary freed at the end of the line (24 chars,
heap-allocated past SSO), and DPAPI read reused memory. Now a function-static.
Round-trip check with heap churn: old code 146/300 failures, fixed 0/300. The
refused-file copy is now kept once per distinct file, not per attempt.

## Session 25: the planner's model and effort, chosen in Settings

The owner picked "two drop-downs + cost" from three mockups. `gb::plannerModels()`
in Planner.h is the one list (Fable 5.1, Opus 5.5, Opus 5, Sonnet 5, with list
prices), and each entry says whether `fallbacks: "default"` is DOCUMENTED for it:
Fable 5.1, Opus 5.5 and Opus 5 yes, **Sonnet 5 not documented, so not sent**. The
beta header goes with it. Haiku was left out because it takes no effort. Effort
sets the answer's room (16k, 20k at high, 24k at xhigh/max), and the HTTP receive
timeout went from five minutes to ten.

Kept in `planner-settings.json` beside the key file; unknown values fall back
to Opus 5.5 / medium. The cost line parses "song written by the planner ... (model,
N in / M out)" from changes.log and prices the average at the chosen model; it is
recomputed at most every 2 s while visible. The Settings bottom block grew 66 px;
the zero-size check caught the first layout, which had none.

Six new checks (487). The fallback check was made to fail by sending fallbacks
to every model: it named Sonnet 5.

### Also in session 25: takes slide in, a lead bug, and an honest footer

**Takes slide into the list.** `TakeList::arriveAll` on OPENING the screen
(`takesWereShowing`), `arriveRow` on save; 44 px, 300 ms, 30 ms stagger capped at
10 rows. The first check failed with 0 px because `showScreenForSnapshot`
settles every animation by design; it now presses the Takes button instead.

**The "CLI lead overlap" was a plugin bug too.** Measured with a scratch MIDI
reader: ch 11 had 4-11 SAME-PITCH RE-STRIKES a song. Legato in
`PhraseProfile::render` overlapped a note into the next when the leap was
<= max_leap_semitones, and 0 qualified: note-on, same note-on, note-off - the
repeat died after 12 ticks. The plugin shares `render`, so it sent the identical
pair; the TODO's "the plugin does not" was wrong. Fixed with `leap > 0`.
`restrikesForTesting()` scans the plugin's outgoing sequence; it failed with "ch
11: 4" on the unfixed code. **Left open, in TODO under Version 3:** rhythm-guitar
chords re-striking a shared note (3 in preset-blues via the CLI).

**The footer** said "latency 0.0 ms" (always zero; Ghostband adds none) and
getBlockSize() (the host's announced maximum: 512 on a 1024 rig). It now shows
samples/blocks measured each second in `checkAudioKeptUp`, falling back to the
announced size marked "(announced)"; stall and AUDIO FELL BEHIND lines use it too.

**Install.bat clears Kontakt's leftover GP5** on its first lock refusal, via
`StartGigPerformer.ps1 -ClearOnly` (same rule: no window, >20 s; logged "(by the
installer)"; never starts anything). Tested against ping/charmap, then against
his live GP5: untouched, nothing logged, install correctly refused.

### Later in session 25: the cost line and an unreadable key

**His saved key was refused by Windows** ("could not be read on this Windows
account") two days after saving; he re-entered it. Harness proven innocent (key
redirected for the whole run from main()); SecretStore unchanged since written; the
new file decrypts fine. Cause unknown and the evidence was overwritten by the
re-save. Now: `readPlannerKey` logs `planner key could not be read   Windows error
N` and keeps `planner-key-unreadable-<date>.bin`; the editor checks once at open
(`plannerKeyReadable`) and says so in the Write line and the key field.

**The cost ignored effort.** Songs now log "(model, effort, N in / M out)"; old
lines have no effort and were medium. Same model+effort -> measured average;
otherwise "roughly", output scaled by 0.75/1.0/1.3/1.7/2.2 (low..max; low/high from
the API docs' Fable 5 runs, xhigh/max extrapolated). Two-line label, bottom block
+16.

**Section titles** in the tracker ribbon were clipped (boxes are to scale; a
2-bar section is ~15 px). Measured across all 34 plans: 16 of 296 cut at the
default size, 40 at the minimum, and far more in planner-written songs. He chose
abbreviations + hover from three mockups: `TrackerView::fitSectionName` walks a
fixed ladder per base name, keeping the number; ArrangementView uses it too. The
fit check failed with 4 clipped on preset-punk when the ladder was bypassed.

**Load plan is a menu** (his pick of three): `gatherLoadEntries` builds Presets
(bundle, grouped by file-name genre, titled from the plan), Written (newest first,
"<title>   Mon DD" from the planner's file stamp), My songs (`mySongsFolder()`,
override-aware), Browse. Empty sections are greyed with a reason. The harness has
no bundle, so the Presets part is only visible in the installed plugin.

## Session 24: the interface that looped, and timing the audio thread

The owner's Focusrite locked into a steady echoing noise mid-song on the
morning of 2026-09-26, and only unplugging it cured it. That is an ASIO driver
replaying its last buffer after audio stopped arriving.

**What the log already showed:** five timer gaps between 04:56:22 and 04:57:32
whose audio block counts do NOT account for the gap (1,047 ms with 3 blocks;
normal is one block per 10.67 ms). So the whole host stalled, audio included,
for up to a second. Three of the five fell while the band was PAUSED (04:56:07
to 04:57:04), when Ghostband does almost nothing. The PC had booted at about
04:46; Windows was installing a Defender update at 04:57:30. Windows logged no
driver or USB error and no GP5 crash. **Leading suspect: post-boot Windows
activity, not Ghostband. Not proven.** A windowless GigPerformer5 process
started at 04:53 that morning, the session with the fault, was still alive
hours later when this session installed. Worth ending before GP5 starts again.

**What was missing, now added:** Ghostband's own audio-thread time. processBlock
is timed from its first line to every return (`TimeThisBlock`, high-resolution
ticks, fetch_add plus a compare-exchange max, no locks), and counts samples.
`checkAudioKeptUp` runs once a second from the timer: if the host asked for more
than 100 ms less audio than the second contained (and asked for some), it
writes `AUDIO FELL BEHIND` with our total and worst block against the block
budget. Capped at 200 lines a session. Every window-stall line carries the same
two figures. This also catches an audio stall with a healthy window, which the
timer-gap detector never could.

Two checks: a short second is logged with our time beside it (made to fail with
the threshold disabled first), and a full second is not.
`juce::String (double, 0)` means full precision, not zero decimals - use
`roundToInt`. And `Build.bat` does not run from the bash tool; a "rebuild" there
silently left the deliberately broken binary in place. Build through PowerShell.

### And why Gig Performer never quits: Kontakt 8

He asked why he keeps having to end GigPerformer5.exe in Task Manager after
Quit. **Measured with `ghostband_probe` as a bare host** (load, play two notes,
unload, time the exit): Ghostband 5/5 clean in under a second; SSD5, MODO Bass 2,
IRON 2 clean in 3-15 s; **Kontakt 8 (8.13.1) never exited, 2 of 2**, after
printing its last result - a hang on shutdown, the exact symptom. Virtual Pianist
exited once with 0xC0000374 (heap corruption on exit), a crash rather than a hang,
seen once in two runs. Neither is ours to fix.

Fix on his side, with no change of habit: `tools/StartGigPerformer.ps1` (+ `.vbs`
so no console flashes) installed to `Documents\Ghostband\Launcher\`, and his TASKBAR
PIN now points at it (the original pin is backed up beside it as
`Gig Performer 5 - original taskbar pin.lnk`). It kills GP5 copies with no window
older than 20 s, logs to `gp-launcher.log`, activates a live session instead of
starting a second. Tested against ping (old one cleared, young one spared) and
charmap (windowed one kept, no second started). **First install was wrong:** it went to %LOCALAPPDATA%\Ghostband, and this
Claude app is MSIX-packaged, so a NEW folder under AppData created from its tools
is silently redirected into Packages\Claude_...\LocalCache. The pin (an existing
file, edited in place) was real; the script it pointed at was not, and wscript
said "Can not find script file". The tests had passed because they ran inside the
same sandbox. Moved to Documents, which is not redirected. Side effect: the running GP5 may
show as its own taskbar button beside the pin, because the pin now launches
wscript.

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
