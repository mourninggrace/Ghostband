"""Writes preset songs from a compact spec.

The boilerplate in a plan file is identical from song to song - the same
profile paths, the same section keys, the same fills-and-silence decisions
that were settled once and apply everywhere. Only the ARRANGEMENT differs,
and that is the part worth a person's attention.

So a preset is defined here as a few lines of decisions and this expands it.
Nothing it produces is a template a listener could hear: the chords, the
lengths, the intensities and the section order all come from the spec.

The rules baked in are the ones already argued out elsewhere and true of every
song, so that no future preset has to rediscover them:

  - no fills in an intro, a first verse, a bridge, a breakdown or an ending
  - the section named "solo" gives guitar2 the solo and keeps guitar driving
    underneath, because a soloing player cannot also hold the harmony
  - a part named in "plays" is always told what to play, never left to the
    engine to guess for

Run from the project root:  python tools/make_presets.py
"""
import io
import os

PROFILES = {
    "drum_profile":    "profiles/ssd5-terry-date.json",
    "bass_profile":    "profiles/modo-bass-2.json",
    "guitar_profile":  "profiles/vg-iron2.json",
    "guitar2_profile": "profiles/shreddage-3-hydra.json",
    "piano_profile":   "profiles/virtual-pianist.json",
}

# Sections that never get an answering guitar, decided once. An intro is
# establishing something, a first verse should arrive before the answers do, a
# breakdown is made of space and an ending on open chords wants to ring.
NEVER_FILLS = ("intro", "verse1", "bridge", "breakdown", "quiet", "ending",
               "theme", "outro", "interlude")


def section(sec, has_piano):
    """One section object. `sec` is (name, bars, intensity, feel, chords, opts)."""
    name, bars, intensity, feel, chords, opts = sec

    parts = ["drums", "bass"]
    if opts.get("guitar", "driving") != "silent":
        parts.append("guitar")

    solo = (opts.get("guitar2") == "solo")
    fills = (name not in NEVER_FILLS) and not solo and opts.get("guitar2") != "silent"

    if solo or fills:
        parts.append("guitar2")
    if has_piano and opts.get("piano", "silent") != "silent":
        parts.append("piano")

    if "plays" in opts:
        parts = opts["plays"].split("+")

    g2 = "solo" if solo else ("fills" if fills else "silent")

    body = [
        '      "name": "%s", "bars": %d, "intensity": %.2f,' % (name, bars, intensity),
        '      "feel": "%s", "plays": "%s", "fill": "%s",'
        % (feel, "+".join(parts), opts.get("fill", "auto")),
        '      "chords": [%s],' % ", ".join('"%s"' % c for c in chords),
    ]

    tail = ['"bass": "%s"' % opts.get("bass", "auto"),
            '"guitar": "%s"' % opts.get("guitar", "driving"),
            '"guitar2": "%s"' % g2]

    if "piano" in parts:
        tail.append('"piano": "%s"' % opts.get("piano", "sparse"))

    tail.append('"lead": "%s"' % opts.get("lead", "guitar2" if solo else "guitar"))

    body.append("      " + ", ".join(tail))
    return "    {\n" + "\n".join(body) + "\n    }"


def write(spec, out_dir="plans"):
    has_piano = any(s[5].get("piano", "silent") != "silent" for s in spec["sections"])

    lines = ["{",
             "  // %s" % spec["blurb"].replace("\n", "\n  // "),
             "",
             '  "name": "%s",' % spec["title"],
             '  "bpm": %d,' % spec["bpm"],
             '  "key": "%s", "mode": "%s",' % (spec["key"], spec["mode"]),
             '  "style": "%s",' % spec["style"],
             '  "bass_tuning": "%s",' % spec.get("tuning", "standard"),
             '  "seed": %d,' % spec["seed"],
             '  "complexity": %.2f, "humanize": %.2f, "fills": %.2f,'
             % (spec.get("complexity", 0.5), spec.get("humanize", 0.5),
                spec.get("fills", 0.62)),
             '  "ending": "%s",' % spec.get("ending", "hard_stop"),
             ""]

    for key, path in PROFILES.items():
        if key == "piano_profile" and not has_piano:
            continue
        lines.append('  "%s": "%s",' % (key, path))

    # The last profile line KEEPS its comma - "sections" follows it. Stripping
    # it here was a reflex from writing the last entry of a list, and produced
    # seven files that all failed to parse in the same place.
    lines.append("")
    lines.append('  "sections": [')
    lines.append(",\n".join(section(s, has_piano) for s in spec["sections"]))
    lines.append("  ]")
    lines.append("}")

    path = os.path.join(out_dir, spec["file"])
    io.open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    return path
