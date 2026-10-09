# Changelog

What changed in each release of Ghostband, newest first. Every number here was
measured, not estimated; where something could not be measured yet, it says so.

Downloads are on the [releases page](https://github.com/mourninggrace/Ghostband/releases).

## [Unreleased]

### Changed
- **Guitar 1 is now Splash Sound's Power Riffer and Guitar Strum** (Kontakt),
  replacing UJAM Iron 2 at the owner's word. Power Riffer (electric, power
  chords, ch 13) plays the 25 rock and metal songs; Guitar Strum (acoustic,
  full chords, ch 12) the three ballads and three blues. The demo songs keep
  Iron 2, whose profile stays for anyone who has it.

### Added
- **Stroke instruments** ("mode": "strokes"): the chord is a held fretting
  key, every strum one key press - down, up, palm mute - ringing until the
  next stroke, and STOP before a rest longer than an eighth (not after a palm
  mute, which dies by itself). Ghostband still writes the rhythm. Old chord
  released before the next is fretted, so Power Riffer changes plainly
  rather than sliding by itself. Such an instrument plays no single notes, so
  guitar 1 keeps strumming where it would harmonise a twin line. Power
  Riffer's map read from its own script; Guitar Strum's from its manual's
  picture (marked needs-verification). New check: 462 strokes in Terminal
  Velocity, every one on the right fretted chord, never two held.

## [2.5.0] — 2026-10-03

The sound, by genre: guitar 1's amp, guitar, stomp box and finisher and the
piano's character and room chosen from the instrument makers' own genre
presets - and the songs already approved by ear pinned exactly as they were
heard. Plus longer blues answers, thrash-note pedal riffs, capo by key, the
solo knobs (CLIMB, THEME, TRICKS), each instrument in its own box on the mix,
double-click to reset everything, and an audio dropout log. Session 33.

### Changed
- **Each instrument's mix controls in their own box** (the owner: "too
  crowded"; layout B of three sketches). Drums and bass, guitar and piano two
  to a row - the name and why a knob is greyed on one line, LEVEL and BUSY on
  the next - and guitar 2 across the rail with all seven of its controls. The
  window's default and minimum height went from 820 to 850 to give them room.
- **Double-click resets every knob and slider.** The trims and levels already
  did; now the four feel dials go back to the song's own values and a
  section's intensity to what it had when picked. A check walks every slider.

### Added
- **Capo by key, for guitar 2.** A new control type, "capo": the song's key
  picks Hydra's capo the way a player would - the lowest fret (up to the 5th)
  that puts the key's tonic on an open string (E A D G B F#), so its shapes ring
  open. Hydra's capo moves where notes are fretted, not their pitch (manual
  p14). Seven songs, all in C or F, get fret 1; every other key is already
  open and gets none. Set the capo control to "none" in Settings to leave the
  instrument's own. New check.
- **The solo knobs: CLIMB, THEME, TRICKS** (layout A, chosen by the owner from
  three sketches): a third line under GTR 2. CLIMB - how far up the neck a solo
  rises (last-third peak 74.6 -> 86.4 from bottom to top, over 30 songs); THEME
  - how often the opening idea returns; TRICKS - pinches, taps, harmonics,
  rakes, chokes (0 at the bottom, 186 in the middle, 356 at the top). The middle
  of each is the approved solos, note for note (the solo lock holds). Saved
  with the session and in takes. The mix rows are 3 px tighter to fit it at
  the default window size.
- **Audio dropout detector.** A buffer the host delivers late (far longer than
  its length after the last) is written to changes.log as AUDIO DROPOUT, with
  the bar and what Ghostband itself spent on the buffer before - so a cut-out
  says whether it was Ghostband or something else.
- **Effects picked by genre.** A new control type, "style": each song style
  lists the settings the instrument maker's own genre presets use, and the song
  picks one for its whole length. Guitar 1 (VG Iron 2): amp, guitar and finisher
  - e.g. metal Metal/Crunch, Twang/Bite, Compressor/Warm Reamp/Saturize/Parallel
  Reamping/rooms; blues Cream/Crisp/Clean with spring and slap-back; ballads
  Clean/Crisp with warm reverb, concert room or a quarter delay. The piano
  (Virtual Pianist VOGUE): character and room - Power/Concert in the heavy
  styles, Emotional/Ballad and wide rooms in ballads. Taken from 313 Iron 2 and
  151 VOGUE presets (docs/research). Checked: 274 finisher choices across the
  songs, none outside its genre's list. Guitar 1's amp was "fixed" at Metal on
  every song before.
- **A song can pin its own sound.** An optional `"sound"` block in a song file
  - per part ("guitar", "guitar2", "piano"), control name -> value 0..1 - sends
  exactly those settings whatever the profile's control follows, "none"
  included (never a level). Nine Cent Rain, Brass Hour, Long Way Round and
  Terminal Velocity, approved by ear in v2.4.0, are pinned to exactly what
  v2.4.0 sent guitar 1 and the piano: the genre picks had moved Nine Cent Rain
  to a clean amp, spring reverb and a Concert piano, and the owner heard it was
  wrong. Checked: 43 pinned settings in 4 songs reach the instrument exactly and
  survive a save; the genre check skips pinned settings.

### Changed
- **Blues fill answers reach back into the bar before.** A twelve-bar line is
  the singer's for two bars and the guitar's for two, but guitar 2's answer had
  only the back half of the last bar - two beats, the same as a rock fill. At
  the end of a line (three times in four) it now starts talking in the back
  half of the bar before: a phrase, a breath, then the landing. Blues answers
  average 3.1 / 3.6 / 4.3 beats in the three blues songs (were about 2); rock
  and metal unchanged. Its own random stream, so no other note in any song
  moves (the solo lock holds). BUSY moves it: never at the bottom, always at
  the top, where the second and third bars' answers reach back too.
- **Iron 2's finisher effect really is picked now.** CC30 turned out to be
  the finisher's on/off switch, not the effect (the owner, 2026-10-03): the
  genre "finisher picks" only ever switched it on or off, and the preset's
  own effect played. The owner mapped the effect selector to CC37; the picks
  moved there and CC30 is held on. Approved songs keep what they were heard
  with: Nine Cent Rain on with Parallel Reamping (his preset's), Iron Weather,
  Brass Hour, Long Way Round and Terminal Velocity off.
- **Iron 2's stomp box, by genre.** The owner confirmed the map (variance
  CC27, amount CC28, stomp box on/off CC29) and mapped the pedal selector to
  CC36. All three were "fixed" at 0 - the stomp box OFF on every song, though
  311 of UJAM's 313 presets have it on. Now it is on, the pedal is picked by
  genre from the drives, boosts, compressor and chorus UJAM's rock/metal/pop
  presets use (metal: Hard Distortion/Booster/Distortion/Overdrive; alt/punk:
  Warm Drive/Overdrive...; ballad: Compressor/Chorus/Booster), and amount and
  variance follow the genre medians of those presets. The five songs approved
  by ear (Nine Cent Rain, Brass Hour, Long Way Round, Terminal Velocity, Iron
  Weather) pin the three as they were heard: off, 0, 0.
- **The thrash note, in the songs.** Hydra's D#0 re-picks the note already
  sounding - "riff between this and another note for fast patterns" (manual
  p35). In thrash (40% of answers), groove metal (30%) and metal (20%) some
  fill answers become a pedal riff: the chord's root re-picked on the thrash
  key, a moving note between climbing the key, into the answer's own landing.
  Fills only - the solos are untouched - never a twin lick, own random stream
  (no other note moves). An instrument without a thrash key picks the notes.
  3-7 re-picks a song in the thrash family, none anywhere else (new check).

### Fixed
- **Calibrate's Save says when nothing changed.** It rewrote every file and
  said "Saved" even with nothing nudged, which looked exactly like a save that
  worked. Now: "Nothing changed - nothing to save", and no file is touched
  (new check). A real save names every note that moved, in every part, not
  just the drums.
- **The installer keeps a calibrated bass top note.** It carried the lowest
  note and chord zones but not "highest_note", so the owner's bass top (79,
  calibrated 2026-10-03) would have reset on the next install. Also set in
  the shipped profile.
- **No more dice on effects.** Guitar 1 (VG Iron 2) and the piano (Virtual
  Pianist) had their effects rolled per song - one of Iron 2's 63 finishers
  (among them 4 Bit, Demonizer, Stutter Destruction and Warm Octave Cloud, a
  pitched layer that sounds like wrong, sour chords) plus amount, width, focus;
  the piano's character, tone, FX and ambience. Now Ghostband leaves them as
  the chosen preset has them. UJAM's own genre presets are tabulated in
  docs/research for the genre-picked effects that come next.

## [2.4.0] — 2026-10-03

The lead guitar, finished: new solos and fills, every Shreddage Hydra
articulation, twin guitars, and the mix knobs (BUSY, SHRED, UNDER) - all heard
and approved by ear. Then a bug audit. Sessions 29-32.

### Changed
- **The lead guitar's fills are licks now, not a bed.** A vocabulary drawn from
  Van Halen, Satriani, Vai and Hammett - tapped arpeggios, hammer-on cascades,
  legato runs, singing bends, wide leaps, harmonics, blues repeats, choke stabs,
  sweeps, rakes and pinch squeals - on Shreddage's own keyswitches, legato in
  Moving Lead mode. Every playing of a lick gets its own timing; three rhythms
  went from 25.8% of fills to 11.4%, songs 0.27 to 0.11 alike.
- **Between answers the lead sings a phrase** instead of holding one note.
- **The solos are rebuilt** (the owner: "empty, intermediate... hunting for notes").
  A solo tells a story: a motif and its answer (AAB in a blues), development,
  a build, a climax, a held and bent root. Solo notes inside one-way scale runs
  21% -> 12%. Complexity and Intuition now drive it; neither reached it before.

### Fixed (session 32)
- **Kontakt 8.13.1 crashed** (three times, the same null read on its UI thread)
  around bursts of the CC 7 that Hydra's rack volume answers once "Accept
  standard controllers" is ticked. Guitar 2's level now has ONE sender: never
  two in a block, never within 50 ms, and restatements no longer stack the
  knob's level and the section's on the same instant. Stopped, it speaks only
  when the knob moves. Checked under a dragged dial, pause, resume and a jump.

### Fixed (session 32) - the dials, verified
- **Drums BUSY acts directly**: hats a subdivision denser/sparser, more/fewer
  ghosts and pickup kicks (in a shuffle the hats stop at swung eighths and the
  busyness goes to ghosts). It used to reweight a roll, and turning it up could
  land on a SPARSER groove (Iron Weather 1656 -> 1609).
- **Bass BUSY up drives**: held notes split with an off-beat push (octave or
  root), and an eighth-note line moves more (octaves, fifths) - up was +5%.
- **SHRED edits the approved solo instead of re-rolling it**: up turns held notes
  and rests into sixteenth runs into the next note, down thins fast runs. It
  went the wrong way on a third of the songs; now on none (checked, 33 songs).
- **Takes remember BUSY and SHRED** (they recalled at whatever the dials said),
  and the BUSY/SHRED knobs follow a recall instead of only reading on open.
- Audit: a bar jump replayed the latest key of any kind at velocity 100 - a
  Hydra fret squeak or slide noise on landing, or the hand put on the wrong
  fret (that key's velocity is the fret). Now only state keys (feel, neck,
  hand, articulation, pick direction), each at its own velocity.
- Audit: a section or bar jump queued just before a song load fired in the
  new song - a load now cancels it.
- Audit: two values shared between plugin instances made per-instance (the
  bar-jump restatement table, the volume-log counter); a flaky watchdog test
  fixed; the old solo generator and its helpers removed (1,585 dead lines -
  the approved solos are byte-for-byte unchanged).
- Checks: every BUSY dial busier up and sparser down on 3 songs x 5 instruments;
  SHRED never the wrong way in any song. At 0 nothing moves - the approved
  solo lock holds.

### Added (session 31)
- **Palm-muted lead, staccato and tremolo picking on guitar 2** (Hydra keys 13, 14,
  18 - the owner's latching keys, released by his C-1 Sustain rule). The NWOBHM
  gallop and the thrash crawl are palm-muted, blues and rock stabs can be
  staccato, and a thrash tremolo burst is one held note on Hydra's looping
  tremolo (other instruments pick it out as written). Not one approved solo
  note moved. Calibrate > GTR 2 rows 14-16 now play.
- **Legato slide, the FX keys and picking mode on guitar 2**: slides between notes
  (C0 held), fret noise (25), slide in from / off the neck (26), thrash note (27,
  test row only until heard), pick direction (108-111: alternate, down-picked
  when palm-muted). Used sparingly and without random draws - no approved solo
  note moved. Calibrate rows 13 and 17-21 play; 22 of 23 rows live (capo left).
- **Click a bar to jump there** while the band plays (on the next bar line);
  stopped, a click still edits the row. Landing mid-section re-sends every
  instrument's latest articulation, latched key, controls and pick direction.
- **Twin guitars.** In metal, hard rock, prog, thrash, groove and doom, guitar 1
  stops strumming and plays guitar 2's line a diatonic third below - on the
  licks written for two guitars (twin theme, sequence, gallop) and on every
  solo's closing phrase. 399 harmony notes across 18 songs; none in blues,
  punk, emo, alt rock, ballads or sludge.
- **Guitar 1 makes room**: it holds its chord under guitar 2's fill answers
  instead of strumming over them, and plays ~12% softer under a solo.
- **Palm-mute dynamics**: the beat struck harder and more open, the sixteenths
  lighter and tighter (Hydra: velocity = how muted).
- **Levels restated** 3 and 8 s after audio starts and before every Calibrate
  audition, so an instrument that loads late still matches its dial.
- **UNDER drops evenly in dB** (1.8 dB per 10%, 100% silent), default 20%.
- **Guitar 2's level is sent gently** - on a change only, ~20/s at most - after
  Kontakt 8 crashed under a burst while the dial was dragged.
- **changes.log records every guitar 2 level change** (value, bar, fills or full).

### Fixed (session 31)
- **UNDER read backwards** (100% meant no drop) - the knob now shows the drop.
- A latching articulation key could stay held into the next gesture; it is now
  always released unless the next note wants the same key.

### Added (session 30)
- **Solo schools** from the owner's list of reference solos - blues, melodic
  rock, neoclassical, NWOBHM, thrash - built from each school's techniques
  (nothing transcribed). Bend-and-release, pre-bends, slides, wide vibrato.
- **Calibrate: DRUMS | BASS | GTR 2**, with a guitar 2 articulation test that
  plays each articulation through the real renderer, to be marked by ear.
- **Bass BUSY up** walks into chord changes with approach notes.

### Fixed (session 30)
- **Hydra spoke the wrong articulation map.** The owner's TACT map puts
  Sustain, Rake, Pinch, Harmonics, Tapping and Choke on CC 40 bands; Ghostband
  sent the manual's keys. And Staccato was found LATCHED - sections switched to
  keys that nothing released - clipping every note. Now: his bands, Sustain
  presses C-1 to release latches, rake leads into its note, slide-in is a real
  Legato Slide (C0). All 13 test rows confirmed right by ear.
- **Solos struck softly** (accent 0.7 vs v2.3.0's 0.9) - back to 0.95.
- **Fills sounded like the solo continuing**: answers capped and softer, held
  support between them, never two line-endings unanswered, and how solo-like a
  fill may be now depends on the style (blues most, alt/emo/punk least).
- **Guitar 2 BUSY** acts directly: 72 / 157 / 212 notes on Nine Cent Rain.

### Added
- **UNDER** - how far guitar 2 drops while it plays fills (default 45%, was a fixed 70%).
- **BUSY** on every MIX row and **SHRED** for guitar 2 - trims whose middle is
  the song exactly as it was; each checked to move only its own instrument.

### Fixed
- **Guitar 2 never swung.** In every shuffle song it played dead straight against
  the band. It swings now, and triplet licks play in the shuffle's own time.
- **Blues fills were held notes and trills** - straight licks doubled for a
  shuffle no longer fitted. They are fitted to swung eighths now.

## [2.3.0] — 2026-09-26

The lead guitar's fills, reworked so they stop sounding the same from song to
song and stop walking the scale; guitar 2 sitting back under its fills; the
solos locked exactly as they were; and a lighter window when idle.

### Changed
- **The lead guitar's fills stop sounding the same from song to song.** Measured
  across every song at six seeds: three rhythms - a few even eighths or
  sixteenths - were 46% of all fills, and two different songs' fill habits were
  0.43 alike. Fills are now re-timed from a vocabulary of real lead figures
  (dotted and snapped, gallops, syncopations, triplets, fast flurries, a pickup
  into one long bent note, phrases with a gap inside), sometimes arch or leap in
  rather than walking the scale, and each song has its own leanings, including
  where in the bar it tends to come in. The three commonest rhythms are now about
  12% of fills, and songs are 0.11 alike; two seeds of the same song, 0.15.
  Solos, drums and bass are note for note what they were.
- **Fills no longer walk up and down the scale.** Still heard after the above,
  and measured at 23% of fill phrases. A fill that comes out as a plain scale walk
  is now re-spelled as broken thirds, broken fourths, a pedal against one anchor
  note, or a zig-zag. Now 10%, much of that quick two-note trills.
- **Guitar 2 sits back under its fills and comes forward for its solos.** In
  sections where it answers, its level control is sent about 6 dB below the GTR 2
  knob; in its solos, and everywhere else, the knob's own level.
- **Lighter on the computer when idle.** With the band stopped, nothing moving
  and the mouse elsewhere, the window updates 10 times a second instead of 30 -
  back to 30 the moment anything plays, moves or the mouse comes over it. And
  the window no longer asks the disk whether an API key is saved thirty times a
  second. Measured with a new harness report (--perf): Ghostband's share of a CPU
  core with its window open and idle fell from about 0.95% to 0.75%; playing,
  about 4%. Drawing already runs on the GPU (Direct2D).
- **The solos are locked.** A fingerprint of every solo note in every song is
  pinned in the tests; checked first against v2.1.0's output in all 33 songs with
  solos (same notes, lengths and legato). Solos change only when asked for.

## [2.2.2] — 2026-09-26

From a bug audit on 2026-09-26. Every fix below was first shown failing by a
new check, then fixed, then the check passed.

### Fixed
- **A note could be left hanging after a reroll.** Rolling, changing the seed or
  moving a dial while the band plays rebuilds the song under notes that are
  sounding, and their note-offs were in the version just replaced. A hi-hat was
  left held on demo-rock. Now any sounding note the new version will not end
  within two bars is released at once.
- **Rhythm-guitar and piano chords re-struck a note still held from the chord
  before,** and the old note's release cut the new one short: 26 times across
  five songs, mostly the blues. Every note now ends when its pitch is struck again.
- **Saved songs changed after the dice.** The dice sets dials at full precision,
  and songs were saved with only four digits, so a rolled song saved and reloaded
  played differently. Numbers are now written exactly.
- **Songs could not be saved or read under some Windows language settings.**
  Numbers followed the system locale, so under German or French formatting a
  song was written as "120,5" and could not be read back. Now locale-free.
- **Folders named with accents or non-Latin letters.** Users named, say, Zoë or
  Müller, or with Cyrillic or Asian names, could not load or save songs and
  profiles under their own Documents. Every file path is now handled as Unicode.
- **A song file could ask for the impossible.** 2,000,000 bars made the song's
  length negative and took 36 seconds to render; "1000/3" time was accepted.
  Loaded songs are held to 1 to 256 bars a section, 128 sections, a sensible
  time signature and a transpose of at most two octaves.
- **A profile save could lose the profile** if moving the new file into place
  failed, because the old one was deleted first. It is now replaced in one step.

## [2.2.1] — 2026-09-26

One fix: a saved API key could be refused as unreadable. If you saw "Windows
could not read the saved key", install this - your key will read again without
entering it.

### Fixed
- **A saved API key was sometimes refused as unreadable.** The fixed "entropy"
  bytes mixed into the key's encryption were built as a temporary that was freed
  before Windows read them, so decryption read whatever memory held by then. In a
  test of 300 save-and-read round trips the old code failed 146; it now fails
  none. Keys saved before this still read: they were encrypted correctly, and only
  the reading side was affected. Found from the Windows error code the previous
  release began logging (13, invalid data).

## [2.2.0] — 2026-09-26

Choose the planner's model and effort, a Load plan menu that reaches every song,
section titles you can read, and a lead-guitar fix you could hear.

### Added
- **Choose the planner's model and effort in Settings.** Claude Fable 5.1, Opus
  5.5 (the default), Opus 5 or Sonnet 5, at low, medium, high, xhigh or max
  effort, kept for every session. Beneath them, what a song costs with that model,
  priced from the token counts of the songs you have written. The refusal fallback
  is sent only to the models documented to take it, higher effort gets more room
  to answer, and the planner now waits up to ten minutes for a slow answer.
- **Load plan is a menu.** Presets (all 34, by genre), songs the planner wrote
  (newest first, with dates), your own saved songs, and "Browse for a file..." for
  anything else. The song playing now is ticked. Before, the file dialog opened
  wherever the loaded song lived, so after the planner wrote one the presets were
  buried in the plugin's install folder.
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
