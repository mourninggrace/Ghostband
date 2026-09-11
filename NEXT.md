# Where things stand

Short on purpose. This file had grown to a thousand lines of accreted session
history and listed things as open that had been finished for weeks, which made it
useless for the one job it has: telling whoever picks this up next what is true
right now. The history is in `docs/archive/NEXT-through-session-13.md`, and
nobody has to read it.

**Last touched 2026-09-11, end of session 14.**

## State

- **v0.2.2 released.** Tagged, published, and the asset's checksum verified by
  downloading it back from GitHub.
- **301 checks** pass on every build.
- **The reference pins hold:** `demo-metal` renders 1231 drum hits / 629 bass
  notes, `demo-rock` 996 / 423. If either moves, something changed that was not
  meant to.
- 34 preset songs, 14 driver profiles, 8 colour themes.
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
until this session taught it otherwise — to a control laid out at zero size. Two
features shipped invisible because nobody opened the images.

To cut a release: `Release.bat`, then `gh release create`. It refuses a dirty
tree or a failing build.

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
