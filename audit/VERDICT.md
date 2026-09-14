# What the audit found, and what was actually there

Checked 2026-09-14 against the code, finding by finding. The report is
`ghostband-comprehensive-audit.md`; this is the reply.

**Six fixed, one found that the audit walked past, nine false, the rest are
features or judgement calls.** Nothing here is a criticism of running the
audit — two of the six had been sitting in plain sight for weeks, and the
chord-parser bug it *led* to is the most musically significant thing found in
this codebase for several sessions.

---

## Fixed

### The plugin never validated a loaded song — report H5, and the real half of engine H1

`SongPlan::validate()` has produced useful warnings since it was written, and
only `cli/main.cpp` ever called it. In the plugin, a mistyped chord silently
became the key's root and a `plays` naming nothing silently produced a silent
section.

Now computed in `regenerate()` — not `loadPlan` — because the song can be
changed without being loaded, and warnings from load time would describe the
song as it was when it was opened. The status line names the first, the tooltip
carries the rest, and each is written to the change log.

### `parseChord` — NOT IN THE REPORT, and worse than what was

The report put the silent chord fallback at `Render.cpp` L58, in the `!c.valid`
branch. That branch almost never runs, because `parseChord` marks nearly
everything valid. Two real bugs were one level down:

- **`"M7"` was unreachable.** It was compared *after* the suffix had been
  lowercased, so `CM7` fell through to the `"m7"` branch below it. **A major
  seventh played as a minor seventh** — the two chords furthest apart in the
  vocabulary.
- **An unrecognised suffix became Major with `valid = true`**, so `Em7b5` played
  as E major and `validate()` had nothing to complain about. It still plays as a
  major triad on the right root — the sensible fallback — but `Chord` now
  carries `qualityUnderstood` so it can be reported.

All 34 shipped songs use only `""`, `"m"` and `"7"`, checked before changing
anything, so neither correction moves a note.

### Faults and advice are now separate — came out of the fix, not the report

Surfacing every warning made two shipped presets open with complaints about
nothing. *"4 chords do not divide evenly into 7 bars"* is a typo in a rock song
and the entire point of a prog one; `preset-prog-2` does it in seven sections
deliberately. `faults()` is what the plugin shows; `validate()` still carries
everything for the command line.

### `planDirty` had six writers and no readers — report M2, half right

The report said it is never read. It *is* read, by `planHasUnsavedEdits()` — but
nothing called that, so the effect was the same. Edits live in memory only, so
loading another song throws them away. The song's name now carries the mark.

Wiring it up immediately exposed a second bug it had been hiding: **the flag was
only ever cleared by saving**, so an edit made before loading another song left
the new one permanently marked as edited.

### `getBeatTicks` split read — report M1, correct

Real. Two separate lock acquisitions (deliberately not nested — nesting them was
the deadlock that froze the host) can return a time signature from one song and
a bar length from another. Both halves are known together when the sequence is
swapped, so the answer is published there as one atomic. Also removes two lock
acquisitions from a path the tracker hits 30×/sec.

### `teachingUntil` truncated to 32 bits — report H3, correct

Real, and unreachable: the counter wraps every 49.7 days and the arithmetic is
unsigned, so near the wrap the guard that stops the mix knobs talking over a
MIDI Learn does nothing. Free to remove, so removed.

---

## False — the code does not do what the report says

### C1, "CRITICAL: `activeNoteCount` race between audio and message threads"

**`sendAllNotesOff` is audio-thread only.** It takes a `juce::MidiBuffer&`, which
only exists inside `processBlock`, and all six call sites are inside
`processBlock`. There is no second thread. Nothing to synchronise.

### H1, "`DynamicObject` leak in `saveTake()`"

**Every `new juce::DynamicObject` is wrapped in a `juce::var` within three
lines** — `list.add (juce::var (o))`. `var` is reference-counted and takes
ownership. Same at the other three sites.

### H2, "theme loaded without bounds checking → palette crash"

**`applyTheme` guards and returns**: `if (index < 0 || index >= numThemes)
return;`. So does `themeName`. The report's own wording — *"if `applyTheme`
indexes by the raw value"* — is a conditional it did not check.

### H6, "`sendAllNotesOff` includes channel 17"

`for (int ch = 1; ch <= 16; ++ch) allNotesOff (ch)` sends on channels **1–16**.
JUCE's `allNotesOff` takes a 1-based channel. The report appears to have applied
the `ch + 1` from the *first* loop — which is 0-based, and also correct — to the
second.

### L1, "empty destructor loses learned controls"

`saveLearnedControls()` is called at four sites, on every change. Nothing is
pending at teardown.

### Engine H2, "bend note-off can precede the natural note end"

The note-off is clamped: `off = std::max (off, end)`. It cannot land before
`end`.

---

## True, but features rather than bugs

| Report | Verdict |
|---|---|
| **H4** no `AudioProcessorParameter`, no host automation | True and deliberate so far. Automating FILLS from a host would be genuinely useful live — worth asking for, not a defect. |
| **M5** no undo/redo | True. A real gap, and a feature. |
| **Engine M2** `bigMoment` only fires on chorus/solo | True. A climactic bridge gets no peak. This is a **musical judgement**, and changing it alters the tone of all 34 presets — not something to change on a static analyser's say-so. Owner's call. |
| **M3** profile paths not sandboxed | True. A plan is a local file the user chose to open, and the worst case is reading a different JSON file and failing to parse it. Low. |
| **H7** optional profile failures leave `status.ok` true | True and intended: a song whose piano profile is missing still plays. The message says so. |

---

## Also worth recording

The report's own numbers are unreliable: it claims **20,291 LOC** for
`PluginProcessor.h/cpp` (actual: **4,263**), and counts `build/` — generated
files — as audited source. Treat its measurements as unverified; the findings
still had to be checked one at a time, and that was worth doing.
