# Changelog

What changed in each release of Ghostband, newest first. Every number here was
measured, not estimated; where something could not be measured yet, it says so.

Downloads are on the [releases page](https://github.com/mourninggrace/Ghostband/releases).

## [Unreleased]

### Added
- **Choose the planner's model and effort in Settings.** Claude Fable 5.1, Opus
  5.5 (the default), Opus 5 or Sonnet 5, at low, medium, high, xhigh or max
  effort, kept for every session. Beneath them, what a song costs with that model,
  priced from the token counts of the songs you have written. The refusal fallback
  is sent only to the models documented to take it, higher effort gets more room
  to answer, and the planner now waits up to ten minutes for a slow answer.
- **The cost line follows effort, and shows its whole sentence.** Choosing an
  effort changed nothing before, because the figure was priced from past songs
  regardless of effort. Each written song now logs its effort. The line prices
  songs written at the chosen model *and* effort from their real tokens, and
  otherwise says "roughly", scaled from your songs. (Scale factors: low and high
  from Anthropic's published runs; xhigh and max extrapolated.) It gets two lines,
  so it is never cut off.
- **A saved key Windows will not decrypt is reported when Ghostband opens,**
  not when Write is pressed. The refusal is logged with Windows' own error code,
  and the unreadable file is kept, so the cause can be found if it happens again.
- **Takes slide into their list.** Opening Takes brings the rows in from the
  side one after another, and a take you save slides into its place. The last
  of the agreed animations.

- **`Install.bat` clears a Gig Performer that quit but did not exit.** The copy
  Kontakt keeps alive holds the plugin file, so installs were refused until it was
  ended in Task Manager. The installer now runs the launcher's own rule: a copy with
  no window, over 20 seconds old, is ended and logged. A session that is open, with
  its window, is never touched.

### Fixed
- **Section titles above the grid were cut off.** Each section's box is as wide
  as the section is long, so a short one got "PRECH" or "BRIDG". A title now uses
  the longest of a fixed set of forms that fits (PRECHORUS1, PRE-CH1, PRE1, P1;
  BREAKDOWN, BRKDN, BD), and hovering it shows the full name.
- **A repeated lead note was sometimes cut to a blip.** Legato runs each note a
  little into the next so the instrument hears a hammer-on, and a repeat of the
  same pitch counted as "close enough" to slur. The second note then started
  while the first was held, and the first one's release stopped both. That was 4
  to 11 notes a song, in the plugin as well as the command line's MIDI files. A
  repeated note is picked again now, and a check reads the plugin's outgoing
  stream to make sure no part ever starts a note that is still held.
- **The footer shows the real buffer.** It read "latency 0.0 ms", which Ghostband
  could never be anything but, and the buffer size the host *announced* (512)
  rather than the one it *sends* (1024 on the owner's rig). It now shows the
  buffer measured from what arrives, its rate, and how long one lasts:
  `buffer 1024 at 48.0k · 21.3 ms`. The stall log uses the same figure.

## [2.1.0] — 2026-09-26

Fixes from the planner's first real songs, a sharper eye on audio drop-outs, and
a cure for Gig Performer staying alive after Quit. The front page now says what
is coming in version 3.

### Added
- **"Coming in version 3" on the front page:** exporting a song as MIDI for any
  DAW or plugin, editing single notes in the grid, and more presets.
- **`tools/StartGigPerformer.ps1`: a Gig Performer launcher that clears leftovers.**
  Kontakt 8 (8.13.1) hangs while shutting down: in a bare test host it finished
  its work and never let the process exit, two runs out of two. SSD5, MODO Bass 2,
  IRON 2 and Ghostband all exited cleanly. So quitting a Gig Performer session with
  a Kontakt instrument leaves `GigPerformer5.exe` running with no window. The
  launcher ends any copy that has no window and is over 20 seconds old, logs it
  to `%APPDATA%\Ghostband\gp-launcher.log`, then starts Gig Performer. If a real
  session is already open, it brings that session to the front instead. A `.vbs`
  wrapper runs it with no console window, for a taskbar pin to point at.
- **The stall log now covers the audio side too.** Ghostband times its own
  audio processing, and once a second checks that the host asked for as much
  audio as the second contained. A shortfall of more than a tenth of a second is
  written to `stalls.log` as `AUDIO FELL BEHIND`, with Ghostband's own audio time
  and its slowest block beside it. So the next time the sound breaks up, the log
  says whether any of the missing time was spent in Ghostband. Every window-stall
  line carries the same two figures. Prompted by an interface that locked into a
  looping noise on 2026-09-26, while the log showed up to a second of audio
  missing and could not say where that time had gone.

### Fixed
- **The planner's explanation is readable in full.** The line under **Write** was
  one line wide, so a two-sentence explanation was cut off mid-word with no way
  to read the rest. Text too long for the line now ends in "... more" at a whole
  word, and clicking it opens the whole explanation in a bubble. Nothing is
  squashed and nothing moves.
- **A refusal to write no longer vanishes.** When Write said no (an empty box, a
  key that could not be read), the reason was shown and wiped by the next screen
  refresh a fraction of a second later, too fast to read. It now stays until the
  next write, and so do the right-click "put back" messages.

### Measured
- **What a written song costs.** The first real planner song took 23 seconds
  and used 3,588 tokens in and 2,261 out: about 6¢ at Claude Opus 5.5's
  $4 / $20 per million tokens (September 2026 prices).

## [2.0.0] — 2026-09-24

Version 2, because its headline — the AI planner — was always planned as version 2's. First published as 0.7.0 the same day with identical content, then renumbered.

### Added
- **Write a song with the AI planner** *(optional)*. Describe a song in a sentence
  on the song screen and Claude Opus 5.5 writes the chart — sections, chords, key,
  tempo, style, the intensity curve and who plays where. It replaces the current
  song at once; right-click **Write** puts the old one back. Needs your own
  Anthropic API key, stored with Windows' per-user encryption and never shown,
  logged or saved into a song. Every written song is kept as an ordinary song file
  in `Documents\Ghostband\Songs\Written`. Nothing is sent without a key, and what
  is sent carries no file paths and no Windows username — checked on every build.
- **Fills sections no longer go silent.** Between its answers the second guitar
  picks a soft arpeggio of the chord, breaking off just before each answer.
  Silent 77% of the time → 4%; longest silence 44.7 s → 3.2 s.
- `docs/MANUAL.md`, this changelog, `TODO.md`, and `tools/screenshots.ps1`, which
  regenerates every screenshot from the real plugin.

### Changed
- **Solos ring instead of streaming.** Each phrase picks a pulse — mostly eighths
  and quarters, with fast runs as bursts. Typical solo note a sixteenth → an
  eighth; notes a beat or longer 2% → 21%.
- **No whole-bar rests in the middle of a solo.**
- The README is a front page now; the full manual moved to `docs/MANUAL.md`.

### Fixed
- **Palm mutes, staccatos and even power chords on lead notes.** The Shreddage
  Hydra profile sent per-note gestures on a controller layout the instrument had
  been re-banded away from; 10.7% of every lead note landed on the wrong
  articulation, always the most exposed ones. The lead now plays 100% sustain.
- **The stall detector logged hundreds of phantom freezes.** It now counts a gap
  only when the host was actually running the plugin, still records any gap
  Ghostband causes itself, dates every line, and rolls its log at 2 MB.
- The test suite no longer writes into the owner's stall log.

## [0.6.0] — 2026-09-19

### Added
- **The Intuition dial** — how much the band plays what it feels like rather than
  what is obvious, reaching the lead, the bass and the drums. Its middle is exactly
  the previous behaviour, so no existing song changed.
- The lit row eases between rows, a section flares as the playhead crosses into it,
  and a reroll sweeps the new arrangement in.

### Changed
- **The lead guitar's vocabulary** widened from five cell shapes and two devices to
  twenty-two and nine, and the lick varies when it repeats. Phrases ascending vs
  descending: 57.6% / 17.3% → 26.6% / 26.6%.
- **Fills are no longer pinned to beat 3 of every 4th bar** — the fourth bar is the
  commonest place, not the only one, and a fill can start before the bar line.
- Keyswitches are drawn in the grid as what they do (`neck`, `fret 9`) instead of as
  notes, and a control change shows its number.

### Fixed
- A lead line could play two notes on one tick.
- A held note could run past the end of its phrase into the next.

## [0.5.0] — 2026-09-14

### Added
- **The dice** — rolls the whole song's character; right-click puts it back.
- The Shreddage lead is told where on the neck to play: fretting modes and hand
  position, both by keyswitch.
- Motion throughout the interface.

### Fixed
- The playhead lit the wrong row at the start of a song.
- `CM7` played as C minor seven.
- A song's own problems (an unreadable chord, a silent section) now reach the screen.

## [0.4.0] — 2026-09-13

### Added
- The rail layout and a 1180 × 820 landscape window.
- Click a grid row to edit that bar's chord and its section's feel.
- Selectable row resolution, row shading that tells every line apart, a change
  log, Reset to profile, and a song that rewinds itself at the end.
- A stall detector that says whether a freeze was Ghostband or the host.

### Fixed
- The mix knobs no longer vanish when a part cannot be reached; they grey out and
  say why.

## [0.3.0] — 2026-09-12

### Added
- The tracker song screen, tooltips on all 92 controls, and the Neon theme.

### Fixed
- A deadlock that could freeze the host.

## [0.2.2] — 2026-09-11

### Fixed
- The second guitar plays in its actual range: 166 notes across 19 presets
  recovered from a dead region.

## [0.2.1] — 2026-09-11

### Fixed
- The structure editor's guitar 2 toggle.

## [0.2.0] — 2026-09-07

### Added
- Takes, colour themes that actually work, one guitar tone per song, and songs
  saved outside Program Files.

## [0.1.0] — 2026-09-07

The first public build. Until then the only way in was cloning with submodules
and owning Visual Studio.

[2.0.0]: https://github.com/mourninggrace/Ghostband/releases/tag/v2.0.0
[0.6.0]: https://github.com/mourninggrace/Ghostband/releases/tag/v0.6.0
[0.5.0]: https://github.com/mourninggrace/Ghostband/releases/tag/v0.5.0
[0.4.0]: https://github.com/mourninggrace/Ghostband/releases/tag/v0.4.0
[0.3.0]: https://github.com/mourninggrace/Ghostband/releases/tag/v0.3.0
[0.2.2]: https://github.com/mourninggrace/Ghostband/releases/tag/v0.2.2
[0.2.1]: https://github.com/mourninggrace/Ghostband/releases/tag/v0.2.1
[0.2.0]: https://github.com/mourninggrace/Ghostband/releases/tag/v0.2.0
[0.1.0]: https://github.com/mourninggrace/Ghostband/releases/tag/v0.1.0
