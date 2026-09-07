"""The preset songs, as decisions.

Each entry is a song someone would write, not a variation on the one above it.
Where two share a style they differ in tempo, key, structure and where the
weight sits - a slow one and a fast one in the same genre are not the same song
with a knob turned.

A section is (name, bars, intensity, feel, chords, options).
"""

S = []


def add(**kw):
    S.append(kw)


# ---------------------------------------------------------------- thrash ----
add(file="preset-thrash-2.json", title="Cold Open", style="thrash", bpm=196,
    key="E", mode="phrygian", tuning="drop_d", seed=7301,
    complexity=0.72, humanize=0.28, fills=0.55, ending="hard_stop",
    blurb="COLD OPEN - thrash at 196, and no introduction.\n"
          "\n"
          "Written against Terminal Velocity rather than beside it: that song\n"
          "builds, this one is already going when you arrive and stops without\n"
          "warning. Two bars of riff, then the whole song, then nothing.\n"
          "\n"
          "Humanize is low on purpose. At this tempo any looseness reads as a\n"
          "band that cannot keep up.",
    sections=[
        ("riff",    4, 0.88, "double_time", ["Em"],
         dict(bass="lock_kick", guitar="muted", fill="none")),
        ("verse1",  8, 0.80, "double_time", ["Em", "Em", "F", "Em"],
         dict(bass="lock_kick", guitar="muted")),
        ("chorus1", 8, 0.97, "double_time", ["C", "D", "Em", "Em"],
         dict(bass="eighths", guitar="driving", fill="big")),
        ("verse2",  8, 0.84, "double_time", ["Em", "Em", "F", "G"],
         dict(bass="lock_kick", guitar="muted")),
        ("chorus2", 8, 1.00, "double_time", ["C", "D", "Em", "Em"],
         dict(bass="eighths", guitar="driving", fill="big")),
        ("solo",   16, 0.95, "double_time", ["Em", "Em", "C", "D"],
         dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus3", 8, 1.00, "double_time", ["C", "D", "Em", "Em"],
         dict(bass="eighths", guitar="driving", fill="big")),
        ("outro",   2, 0.95, "double_time", ["Em"],
         dict(bass="lock_kick", guitar="muted", fill="none")),
    ])

add(file="preset-thrash-3.json", title="The Long Silence", style="thrash", bpm=150,
    key="B", mode="phrygian_dominant", tuning="b_standard", seed=7302,
    complexity=0.66, humanize=0.34, fills=0.70, ending="ritard",
    blurb="THE LONG SILENCE - thrash that stops dead in the middle.\n"
          "\n"
          "Slower than thrash usually sits, in B standard, and built around a\n"
          "breakdown that runs eight bars at almost nothing. The whole song is\n"
          "there to make that silence land.\n"
          "\n"
          "Phrygian dominant rather than plain phrygian: the major third against\n"
          "a flat second is what makes it sound eastern rather than merely dark.",
    sections=[
        ("intro",     4, 0.30, "half_time",   ["Bm"],
         dict(bass="roots", guitar="sparse", fill="small")),
        ("riff",      8, 0.82, "straight",    ["Bm", "Bm", "C", "Bm"],
         dict(bass="lock_kick", guitar="muted", fill="none")),
        ("verse1",    8, 0.70, "straight",    ["Bm", "Bm", "G", "A"],
         dict(bass="lock_kick", guitar="muted")),
        ("chorus1",   8, 0.94, "straight",    ["G", "A", "Bm", "Bm"],
         dict(bass="eighths", guitar="driving")),
        ("breakdown", 8, 0.18, "half_time",   ["Bm"],
         dict(bass="roots", guitar="sparse", fill="none")),
        ("verse2",    8, 0.76, "straight",    ["Bm", "Bm", "G", "A"],
         dict(bass="lock_kick", guitar="muted")),
        ("solo",     12, 0.90, "double_time", ["Bm", "G", "A", "Bm"],
         dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus2",   8, 1.00, "straight",    ["G", "A", "Bm", "Bm"],
         dict(bass="eighths", guitar="driving", fill="big")),
        ("ending",    4, 0.60, "half_time",   ["Bm"],
         dict(bass="roots", guitar="open", fill="none")),
    ])

# ------------------------------------------------------------- hard rock ----
add(file="preset-hard-rock-2.json", title="Paper Crown", style="hard_rock", bpm=118,
    key="A", mode="mixolydian", tuning="standard", seed=7303,
    complexity=0.48, humanize=0.58, fills=0.65, ending="cymbal_ring",
    blurb="PAPER CROWN - hard rock in mixolydian, which is the mode that makes a\n"
          "riff sound like a swagger rather than a threat.\n"
          "\n"
          "Written to sit on the piano as much as the guitar: the piano leads\n"
          "both verses and the guitar takes every chorus, so the handover happens\n"
          "four times and you hear the song change hands.",
    sections=[
        ("intro",      4, 0.35, "straight", ["A"],
         dict(bass="roots", guitar="sparse", piano="sparse", lead="piano")),
        ("verse1",     8, 0.52, "straight", ["A", "A", "G", "D"],
         dict(bass="eighths", guitar="muted", piano="driving", lead="piano")),
        ("prechorus1", 4, 0.72, "straight", ["D", "D", "G", "G"],
         dict(bass="eighths", guitar="driving", piano="sparse")),
        ("chorus1",    8, 0.90, "straight", ["A", "G", "D", "A"],
         dict(bass="eighths", guitar="open", piano="open", lead="guitar", fill="big")),
        ("verse2",     8, 0.56, "straight", ["A", "A", "G", "D"],
         dict(bass="eighths", guitar="muted", piano="driving", lead="piano")),
        ("chorus2",    8, 0.92, "straight", ["A", "G", "D", "A"],
         dict(bass="eighths", guitar="open", piano="open", lead="guitar", fill="big")),
        ("solo",      12, 0.88, "straight", ["A", "G", "D", "A"],
         dict(bass="eighths", guitar="driving", guitar2="solo", piano="sparse")),
        ("chorus3",    8, 0.98, "straight", ["A", "G", "D", "A"],
         dict(bass="eighths", guitar="open", piano="open", lead="both", fill="big")),
        ("ending",     4, 0.70, "straight", ["A"],
         dict(bass="roots", guitar="open", piano="open", lead="both", fill="none")),
    ])

add(file="preset-hard-rock-3.json", title="Hollow Bell", style="hard_rock", bpm=84,
    key="D", mode="dorian", tuning="drop_d", seed=7304,
    complexity=0.44, humanize=0.62, fills=0.75, ending="fade",
    blurb="HOLLOW BELL - hard rock slowed to 84, which is where a riff stops\n"
          "being fast and starts being heavy.\n"
          "\n"
          "Dorian, so the sixth is major and the whole thing hangs between sad\n"
          "and hopeful instead of committing. Drop D for the weight. Fills are\n"
          "high because at this tempo there is room between the chords and a\n"
          "second guitar has somewhere to be.",
    sections=[
        ("intro",     8, 0.28, "half_time", ["Dm"],
         dict(bass="roots", guitar="sparse", fill="small")),
        ("verse1",    8, 0.48, "half_time", ["Dm", "Dm", "C", "G"],
         dict(bass="lock_kick", guitar="muted")),
        ("chorus1",   8, 0.84, "straight",  ["G", "C", "Dm", "Dm"],
         dict(bass="eighths", guitar="open", fill="big")),
        ("verse2",    8, 0.54, "half_time", ["Dm", "Dm", "C", "G"],
         dict(bass="lock_kick", guitar="muted")),
        ("chorus2",   8, 0.88, "straight",  ["G", "C", "Dm", "Dm"],
         dict(bass="eighths", guitar="open", fill="big")),
        ("bridge",    8, 0.40, "half_time", ["Bb", "C", "Dm", "Dm"],
         dict(bass="roots", guitar="sparse", fill="small")),
        ("solo",     12, 0.86, "straight",  ["Dm", "C", "Bb", "C"],
         dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus3",   8, 0.94, "straight",  ["G", "C", "Dm", "Dm"],
         dict(bass="eighths", guitar="open", fill="big")),
        ("ending",    8, 0.50, "half_time", ["Dm"],
         dict(bass="roots", guitar="open", fill="none")),
    ])

# ------------------------------------------------------------------ punk ----
add(file="preset-punk-2.json", title="Nothing To Declare", style="punk", bpm=204,
    key="C", mode="major", tuning="standard", seed=7305,
    complexity=0.30, humanize=0.40, fills=0.35, ending="hard_stop",
    blurb="NOTHING TO DECLARE - 204bpm, major key, ninety seconds, no solo.\n"
          "\n"
          "The other punk preset is restraint at 178. This one is what happens\n"
          "when you take the brakes off entirely: three chords, no bridge, and a\n"
          "chorus that arrives twice as often as it should.\n"
          "\n"
          "Complexity is deliberately low. Punk played cleverly is not punk.",
    sections=[
        ("intro",   2, 0.60, "straight", ["C"],
         dict(bass="eighths", guitar="driving", fill="small")),
        ("verse1",  8, 0.78, "straight", ["C", "C", "F", "G"],
         dict(bass="eighths", guitar="driving")),
        ("chorus1", 4, 0.94, "straight", ["F", "G", "C", "C"],
         dict(bass="eighths", guitar="busy", fill="small")),
        ("verse2",  8, 0.82, "straight", ["C", "C", "F", "G"],
         dict(bass="eighths", guitar="driving")),
        ("chorus2", 4, 0.96, "straight", ["F", "G", "C", "C"],
         dict(bass="eighths", guitar="busy", fill="small")),
        ("verse3",  8, 0.86, "straight", ["C", "C", "F", "G"],
         dict(bass="eighths", guitar="driving")),
        ("chorus3", 8, 1.00, "straight", ["F", "G", "C", "C"],
         dict(bass="eighths", guitar="busy", fill="big")),
        ("ending",  2, 0.90, "straight", ["C"],
         dict(bass="eighths", guitar="driving", fill="none")),
    ])

# --------------------------------------------------------------- doom -------
add(file="preset-doom-1.json", title="Weight Of Water", style="doom", bpm=62,
    key="C", mode="natural_minor", tuning="drop_c", seed=7306,
    complexity=0.38, humanize=0.66, fills=0.45, ending="fade",
    blurb="WEIGHT OF WATER - doom at 62, drop C, four chords in five minutes.\n"
          "\n"
          "The first doom preset, and the slowest song here by a wide margin. A\n"
          "riff at this tempo has to be worth waiting through, so there is only\n"
          "one and everything else is arrangement around it.\n"
          "\n"
          "Fills are held back: at 62bpm a busy answering guitar fills the space\n"
          "the song is made of.",
    sections=[
        ("intro",     8, 0.22, "half_time", ["Cm"],
         dict(bass="roots", guitar="sparse", fill="none")),
        ("riff",      8, 0.66, "half_time", ["Cm", "Cm", "Ab", "Bb"],
         dict(bass="lock_kick", guitar="driving", fill="small")),
        ("verse1",    8, 0.52, "half_time", ["Cm", "Cm", "Ab", "Bb"],
         dict(bass="lock_kick", guitar="muted")),
        ("chorus1",   8, 0.80, "half_time", ["Ab", "Bb", "Cm", "Cm"],
         dict(bass="eighths", guitar="open", fill="big")),
        ("verse2",    8, 0.56, "half_time", ["Cm", "Cm", "Ab", "Bb"],
         dict(bass="lock_kick", guitar="muted")),
        ("breakdown", 8, 0.16, "half_time", ["Cm"],
         dict(bass="roots", guitar="sparse", fill="none")),
        ("solo",     12, 0.74, "straight",  ["Cm", "Ab", "Bb", "Cm"],
         dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus2",   8, 0.88, "half_time", ["Ab", "Bb", "Cm", "Cm"],
         dict(bass="eighths", guitar="open", fill="big")),
        ("ending",    8, 0.44, "half_time", ["Cm"],
         dict(bass="roots", guitar="open", fill="none")),
    ])

# ------------------------------------------------------------ groove metal --
add(file="preset-groove-1.json", title="Southern Teeth", style="groove_metal", bpm=96,
    key="D", mode="natural_minor", tuning="drop_d", seed=7307,
    complexity=0.58, humanize=0.52, fills=0.80, ending="hard_stop",
    blurb="SOUTHERN TEETH - groove metal at 96, drop D, and the preset that\n"
          "leans hardest on the second guitar.\n"
          "\n"
          "Fills are set high and the style weights pinch harmonics up, which is\n"
          "the point: in this idiom the squeal at the end of a phrase is not a\n"
          "flourish, it is how the phrase ends. Written after the owner said so\n"
          "about Dimebag Darrell, and he was right.\n"
          "\n"
          "The groove is in the space. Half-time verses against straight\n"
          "choruses, so the song lurches rather than accelerates.",
    sections=[
        ("intro",   4, 0.50, "half_time", ["Dm"],
         dict(bass="lock_kick", guitar="muted", fill="small")),
        ("riff",    8, 0.84, "half_time", ["Dm", "Dm", "F", "C"],
         dict(bass="lock_kick", guitar="muted", fill="none")),
        ("verse1",  8, 0.70, "half_time", ["Dm", "Dm", "Bb", "C"],
         dict(bass="lock_kick", guitar="muted")),
        ("chorus1", 8, 0.92, "straight",  ["Bb", "C", "Dm", "Dm"],
         dict(bass="eighths", guitar="driving", fill="big")),
        ("verse2",  8, 0.74, "half_time", ["Dm", "Dm", "Bb", "C"],
         dict(bass="lock_kick", guitar="muted")),
        ("chorus2", 8, 0.95, "straight",  ["Bb", "C", "Dm", "Dm"],
         dict(bass="eighths", guitar="driving", fill="big")),
        ("solo",   12, 0.90, "straight",  ["Dm", "F", "C", "Dm"],
         dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus3", 8, 1.00, "straight",  ["Bb", "C", "Dm", "Dm"],
         dict(bass="eighths", guitar="driving", fill="big")),
        ("ending",  4, 0.80, "half_time", ["Dm"],
         dict(bass="lock_kick", guitar="muted", fill="none")),
    ])
