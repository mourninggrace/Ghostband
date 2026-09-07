"""Batch two: the styles the engine supports but nothing shipped for.

sludge and metal had no preset at all. The rest get a second song that is a
different song rather than the first one transposed - a different tempo, a
different mode, and the weight in a different place.
"""

S = []


def add(**kw):
    S.append(kw)


# ---------------------------------------------------------------- sludge ----
add(file="preset-sludge-1.json", title="Tar And Feather", style="sludge", bpm=72,
    key="A", mode="natural_minor", tuning="drop_c", seed=7401,
    complexity=0.42, humanize=0.70, fills=0.50, ending="fade",
    blurb="TAR AND FEATHER - sludge at 72 in drop C.\n"
          "Humanize is the highest of any preset here. Sludge is supposed to\n"
          "drag, and a metronomic one sounds like doom played badly.",
    sections=[
        ("intro", 8, 0.25, "half_time", ["Am"], dict(bass="roots", guitar="sparse", fill="none")),
        ("riff", 8, 0.72, "half_time", ["Am", "Am", "F", "G"], dict(bass="lock_kick", guitar="driving", fill="small")),
        ("verse1", 8, 0.56, "half_time", ["Am", "Am", "F", "G"], dict(bass="lock_kick", guitar="muted")),
        ("chorus1", 8, 0.86, "half_time", ["F", "G", "Am", "Am"], dict(bass="eighths", guitar="open", fill="big")),
        ("breakdown", 8, 0.20, "half_time", ["Am"], dict(bass="roots", guitar="sparse", fill="none")),
        ("verse2", 8, 0.62, "half_time", ["Am", "Am", "F", "G"], dict(bass="lock_kick", guitar="muted")),
        ("solo", 12, 0.78, "straight", ["Am", "F", "G", "Am"], dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus2", 8, 0.90, "half_time", ["F", "G", "Am", "Am"], dict(bass="eighths", guitar="open", fill="big")),
        ("ending", 8, 0.40, "half_time", ["Am"], dict(bass="roots", guitar="open", fill="none")),
    ])

add(file="preset-sludge-2.json", title="Low Tide", style="sludge", bpm=88,
    key="F", mode="phrygian", tuning="drop_d", seed=7402,
    complexity=0.50, humanize=0.64, fills=0.60, ending="hard_stop",
    blurb="LOW TIDE - sludge at 88, faster than the form usually allows.\n"
          "Phrygian in F, so the flat second sits a semitone off the root and\n"
          "the riff never quite resolves.",
    sections=[
        ("riff", 8, 0.78, "straight", ["Fm", "Fm", "Gb", "Fm"], dict(bass="lock_kick", guitar="muted", fill="none")),
        ("verse1", 8, 0.62, "straight", ["Fm", "Fm", "Db", "Eb"], dict(bass="lock_kick", guitar="muted")),
        ("chorus1", 8, 0.90, "straight", ["Db", "Eb", "Fm", "Fm"], dict(bass="eighths", guitar="driving", fill="big")),
        ("verse2", 8, 0.68, "straight", ["Fm", "Fm", "Db", "Eb"], dict(bass="lock_kick", guitar="muted")),
        ("bridge", 8, 0.34, "half_time", ["Db", "Db", "Eb", "Eb"], dict(bass="roots", guitar="sparse", fill="small")),
        ("solo", 12, 0.86, "straight", ["Fm", "Db", "Eb", "Fm"], dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus2", 8, 0.96, "straight", ["Db", "Eb", "Fm", "Fm"], dict(bass="eighths", guitar="driving", fill="big")),
        ("ending", 4, 0.80, "straight", ["Fm"], dict(bass="lock_kick", guitar="muted", fill="none")),
    ])

# ----------------------------------------------------------------- metal ----
add(file="preset-metal-1.json", title="Iron Weather", style="metal", bpm=142,
    key="E", mode="natural_minor", tuning="drop_d", seed=7403,
    complexity=0.60, humanize=0.40, fills=0.65, ending="hard_stop",
    blurb="IRON WEATHER - straight metal at 142, the tempo the style sits at\n"
          "when it is not trying to be thrash. Drop D, minor, no tricks.",
    sections=[
        ("intro", 4, 0.45, "straight", ["Em"], dict(bass="lock_kick", guitar="muted", fill="small")),
        ("riff", 8, 0.82, "straight", ["Em", "Em", "C", "D"], dict(bass="lock_kick", guitar="driving", fill="none")),
        ("verse1", 8, 0.66, "straight", ["Em", "Em", "G", "D"], dict(bass="lock_kick", guitar="muted")),
        ("chorus1", 8, 0.94, "straight", ["C", "D", "Em", "Em"], dict(bass="eighths", guitar="driving", fill="big")),
        ("verse2", 8, 0.70, "straight", ["Em", "Em", "G", "D"], dict(bass="lock_kick", guitar="muted")),
        ("chorus2", 8, 0.96, "straight", ["C", "D", "Em", "Em"], dict(bass="eighths", guitar="driving", fill="big")),
        ("solo", 12, 0.92, "double_time", ["Em", "C", "D", "Em"], dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus3", 8, 1.00, "straight", ["C", "D", "Em", "Em"], dict(bass="eighths", guitar="driving", fill="big")),
        ("ending", 4, 0.85, "straight", ["Em"], dict(bass="lock_kick", guitar="muted", fill="none")),
    ])

add(file="preset-metal-2.json", title="Winter Count", style="metal", bpm=108,
    key="G", mode="dorian", tuning="standard", seed=7404,
    complexity=0.54, humanize=0.50, fills=0.70, ending="ritard",
    blurb="WINTER COUNT - metal at 108 in dorian, with a piano under it.\n"
          "The major sixth against a minor key is what keeps it from being\n"
          "bleak, and the piano is there to make sure you hear that.",
    sections=[
        ("intro", 8, 0.30, "straight", ["Gm"], dict(bass="roots", guitar="sparse", piano="sparse", lead="piano")),
        ("verse1", 8, 0.58, "straight", ["Gm", "Gm", "F", "C"], dict(bass="lock_kick", guitar="muted", piano="sparse")),
        ("chorus1", 8, 0.88, "straight", ["F", "C", "Gm", "Gm"], dict(bass="eighths", guitar="driving", piano="open", fill="big")),
        ("verse2", 8, 0.64, "straight", ["Gm", "Gm", "F", "C"], dict(bass="lock_kick", guitar="muted", piano="sparse")),
        ("bridge", 8, 0.40, "half_time", ["Eb", "F", "Gm", "Gm"], dict(bass="roots", guitar="sparse", piano="driving", lead="piano", fill="small")),
        ("solo", 12, 0.86, "straight", ["Gm", "F", "Eb", "F"], dict(bass="eighths", guitar="driving", guitar2="solo", piano="sparse")),
        ("chorus2", 8, 0.94, "straight", ["F", "C", "Gm", "Gm"], dict(bass="eighths", guitar="driving", piano="open", fill="big")),
        ("ending", 8, 0.50, "half_time", ["Gm"], dict(bass="roots", guitar="open", piano="open", lead="both", fill="none")),
    ])

# -------------------------------------------------------------- alt rock ----
add(file="preset-alt-rock-2.json", title="Static Bloom", style="alt_rock", bpm=104,
    key="F", mode="major", tuning="standard", seed=7405,
    complexity=0.40, humanize=0.62, fills=0.72, ending="cymbal_ring",
    blurb="STATIC BLOOM - alt rock at 104 in a major key, which is rarer here\n"
          "than it should be. Quiet verses, enormous choruses, and a bridge\n"
          "that drops to almost nothing before the last one.",
    sections=[
        ("intro", 4, 0.24, "straight", ["F"], dict(bass="roots", guitar="sparse", fill="small")),
        ("verse1", 8, 0.34, "straight", ["F", "F", "Dm", "Bb"], dict(bass="roots", guitar="sparse")),
        ("prechorus1", 4, 0.62, "straight", ["Bb", "Bb", "C", "C"], dict(bass="eighths", guitar="muted")),
        ("chorus1", 8, 0.92, "straight", ["F", "C", "Dm", "Bb"], dict(bass="eighths", guitar="open", fill="big")),
        ("verse2", 8, 0.40, "straight", ["F", "F", "Dm", "Bb"], dict(bass="roots", guitar="sparse")),
        ("chorus2", 8, 0.94, "straight", ["F", "C", "Dm", "Bb"], dict(bass="eighths", guitar="open", fill="big")),
        ("bridge", 8, 0.20, "half_time", ["Dm", "Dm", "Bb", "Bb"], dict(bass="roots", guitar="sparse", fill="none")),
        ("solo", 12, 0.84, "straight", ["F", "C", "Dm", "Bb"], dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus3", 8, 1.00, "straight", ["F", "C", "Dm", "Bb"], dict(bass="eighths", guitar="open", fill="big")),
        ("ending", 4, 0.60, "straight", ["F"], dict(bass="roots", guitar="open", fill="none")),
    ])

add(file="preset-alt-rock-3.json", title="Handwriting", style="alt_rock", bpm=152,
    key="A", mode="natural_minor", tuning="standard", seed=7406,
    complexity=0.52, humanize=0.48, fills=0.58, ending="hard_stop",
    blurb="HANDWRITING - alt rock at 152, nearly punk tempo played with alt\n"
          "rock dynamics. No prechorus: the verse walks straight into the\n"
          "chorus and the song is shorter for it.",
    sections=[
        ("intro", 4, 0.50, "straight", ["Am"], dict(bass="eighths", guitar="driving", fill="small")),
        ("verse1", 8, 0.60, "straight", ["Am", "Am", "F", "G"], dict(bass="eighths", guitar="muted")),
        ("chorus1", 8, 0.90, "straight", ["F", "G", "Am", "Am"], dict(bass="eighths", guitar="open", fill="big")),
        ("verse2", 8, 0.66, "straight", ["Am", "Am", "F", "G"], dict(bass="eighths", guitar="muted")),
        ("chorus2", 8, 0.92, "straight", ["F", "G", "Am", "Am"], dict(bass="eighths", guitar="open", fill="big")),
        ("solo", 8, 0.88, "straight", ["Am", "F", "G", "Am"], dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus3", 8, 0.98, "straight", ["F", "G", "Am", "Am"], dict(bass="eighths", guitar="open", fill="big")),
        ("ending", 4, 0.75, "straight", ["Am"], dict(bass="eighths", guitar="driving", fill="none")),
    ])

# ------------------------------------------------------------------ emo -----
add(file="preset-emo-2.json", title="Half A Mile Out", style="emo", bpm=168,
    key="D", mode="major", tuning="standard", seed=7407,
    complexity=0.46, humanize=0.56, fills=0.66, ending="hard_stop",
    blurb="HALF A MILE OUT - emo at 168 in D major, fast and bright.\n"
          "The other emo preset gets its lift from twelve bar choruses against\n"
          "eight bar verses; this one is even throughout and lifts on\n"
          "intensity instead.",
    sections=[
        ("intro", 4, 0.42, "straight", ["D"], dict(bass="eighths", guitar="sparse", fill="small")),
        ("verse1", 8, 0.54, "straight", ["D", "A", "Bm", "G"], dict(bass="eighths", guitar="muted")),
        ("chorus1", 8, 0.90, "straight", ["G", "D", "A", "Bm"], dict(bass="eighths", guitar="open", fill="big")),
        ("verse2", 8, 0.60, "straight", ["D", "A", "Bm", "G"], dict(bass="eighths", guitar="muted")),
        ("chorus2", 8, 0.94, "straight", ["G", "D", "A", "Bm"], dict(bass="eighths", guitar="open", fill="big")),
        ("bridge", 8, 0.36, "straight", ["Bm", "Bm", "G", "G"], dict(bass="roots", guitar="sparse", fill="small")),
        ("solo", 12, 0.88, "straight", ["D", "A", "Bm", "G"], dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus3", 8, 1.00, "straight", ["G", "D", "A", "Bm"], dict(bass="eighths", guitar="open", fill="big")),
        ("ending", 4, 0.70, "straight", ["D"], dict(bass="eighths", guitar="open", fill="none")),
    ])

add(file="preset-emo-3.json", title="Four In The Morning", style="emo", bpm=92,
    key="C", mode="natural_minor", tuning="standard", seed=7408,
    complexity=0.38, humanize=0.66, fills=0.78, ending="fade",
    blurb="FOUR IN THE MORNING - emo slowed right down, with piano.\n"
          "Fills are the highest of any preset: at 92 there is room between\n"
          "everything, and a second guitar answering the line is most of what\n"
          "this arrangement is.",
    sections=[
        ("intro", 8, 0.20, "straight", ["Cm"], dict(bass="roots", guitar="sparse", piano="sparse", lead="piano")),
        ("verse1", 8, 0.36, "straight", ["Cm", "Ab", "Eb", "Bb"], dict(bass="roots", guitar="sparse", piano="driving", lead="piano")),
        ("chorus1", 8, 0.82, "straight", ["Ab", "Eb", "Bb", "Cm"], dict(bass="eighths", guitar="open", piano="open", fill="big")),
        ("verse2", 8, 0.42, "straight", ["Cm", "Ab", "Eb", "Bb"], dict(bass="roots", guitar="sparse", piano="driving", lead="piano")),
        ("chorus2", 8, 0.86, "straight", ["Ab", "Eb", "Bb", "Cm"], dict(bass="eighths", guitar="open", piano="open", fill="big")),
        ("solo", 12, 0.80, "straight", ["Cm", "Ab", "Eb", "Bb"], dict(bass="eighths", guitar="driving", guitar2="solo", piano="sparse")),
        ("chorus3", 8, 0.92, "straight", ["Ab", "Eb", "Bb", "Cm"], dict(bass="eighths", guitar="open", piano="open", lead="both", fill="big")),
        ("ending", 8, 0.40, "half_time", ["Cm"], dict(bass="roots", guitar="open", piano="open", lead="both", fill="none")),
    ])

# --------------------------------------------------------------- ballad -----
add(file="preset-ballad-2.json", title="Every Second Sunday", style="ballad", bpm=76,
    key="G", mode="major", tuning="standard", seed=7409,
    complexity=0.32, humanize=0.68, fills=0.70, ending="fade",
    blurb="EVERY SECOND SUNDAY - a ballad in a major key, which the other one\n"
          "is not. Guitar led rather than piano led, so the piano answers\n"
          "instead of carrying it.",
    sections=[
        ("intro", 8, 0.18, "straight", ["G"], dict(bass="roots", guitar="sparse", piano="sparse", lead="guitar")),
        ("verse1", 8, 0.32, "straight", ["G", "Em", "C", "D"], dict(bass="roots", guitar="open", piano="sparse")),
        ("verse2", 8, 0.42, "straight", ["G", "Em", "C", "D"], dict(bass="eighths", guitar="open", piano="sparse")),
        ("chorus1", 8, 0.76, "straight", ["C", "D", "G", "Em"], dict(bass="eighths", guitar="open", piano="open", lead="both", fill="big")),
        ("verse3", 8, 0.48, "straight", ["G", "Em", "C", "D"], dict(bass="eighths", guitar="open", piano="driving")),
        ("chorus2", 8, 0.82, "straight", ["C", "D", "G", "Em"], dict(bass="eighths", guitar="open", piano="open", lead="both", fill="big")),
        ("solo", 8, 0.72, "straight", ["G", "Em", "C", "D"], dict(bass="eighths", guitar="driving", guitar2="solo", piano="sparse")),
        ("chorus3", 8, 0.90, "straight", ["C", "D", "G", "Em"], dict(bass="eighths", guitar="open", piano="open", lead="both", fill="big")),
        ("ending", 8, 0.35, "half_time", ["G"], dict(bass="roots", guitar="open", piano="open", lead="both", fill="none")),
    ])

add(file="preset-ballad-3.json", title="The Quiet Part", style="ballad", bpm=58,
    key="E", mode="natural_minor", tuning="standard", seed=7410,
    complexity=0.26, humanize=0.72, fills=0.55, ending="ritard",
    blurb="THE QUIET PART - 58bpm, the slowest tempo in the set.\n"
          "No chorus at all: four verses that each arrive with one more\n"
          "instrument than the last, and a solo where the chorus would be.",
    sections=[
        ("intro", 8, 0.14, "half_time", ["Em"], dict(bass="roots", guitar="silent", piano="sparse", lead="piano", plays="drums+piano")),
        ("verse1", 8, 0.24, "half_time", ["Em", "C", "G", "D"], dict(bass="roots", guitar="silent", piano="sparse", lead="piano", plays="drums+bass+piano")),
        ("verse2", 8, 0.38, "half_time", ["Em", "C", "G", "D"], dict(bass="roots", guitar="open", piano="sparse", lead="piano")),
        ("verse3", 8, 0.52, "straight", ["Em", "C", "G", "D"], dict(bass="eighths", guitar="open", piano="driving", lead="both")),
        ("solo", 12, 0.68, "straight", ["Em", "C", "G", "D"], dict(bass="eighths", guitar="open", guitar2="solo", piano="sparse")),
        ("verse4", 8, 0.62, "straight", ["Em", "C", "G", "D"], dict(bass="eighths", guitar="open", piano="open", lead="both", fill="big")),
        ("ending", 8, 0.28, "half_time", ["Em"], dict(bass="roots", guitar="open", piano="open", lead="both", fill="none")),
    ])

# ---------------------------------------------------------------- blues -----
add(file="preset-blues-2.json", title="Nine Cent Rain", style="blues", bpm=112,
    key="A", mode="mixolydian", tuning="standard", seed=7411,
    complexity=0.44, humanize=0.60, fills=0.80, ending="hard_stop",
    blurb="NINE CENT RAIN - a fast shuffle in A, guitar led throughout.\n"
          "The other blues preset is a piano's song at 86; this one is the\n"
          "guitar's, and the piano only comps. Fills high, because in a blues\n"
          "the answer is half the tune.",
    sections=[
        ("intro", 4, 0.42, "straight", ["A7"], dict(bass="eighths", guitar="driving", piano="sparse", fill="small")),
        ("verse1", 12, 0.58, "straight", ["A7", "A7", "A7", "A7", "D7", "D7", "A7", "A7", "E7", "D7", "A7", "E7"],
         dict(bass="eighths", guitar="driving", piano="sparse")),
        ("verse2", 12, 0.66, "straight", ["A7", "A7", "A7", "A7", "D7", "D7", "A7", "A7", "E7", "D7", "A7", "E7"],
         dict(bass="eighths", guitar="driving", piano="sparse")),
        ("solo", 12, 0.86, "straight", ["A7", "A7", "A7", "A7", "D7", "D7", "A7", "A7", "E7", "D7", "A7", "E7"],
         dict(bass="eighths", guitar="driving", guitar2="solo", piano="driving")),
        ("verse3", 12, 0.76, "straight", ["A7", "A7", "A7", "A7", "D7", "D7", "A7", "A7", "E7", "D7", "A7", "E7"],
         dict(bass="eighths", guitar="driving", piano="driving", fill="big")),
        ("ending", 4, 0.70, "straight", ["A7"], dict(bass="eighths", guitar="open", piano="open", lead="both", fill="none")),
    ])

add(file="preset-blues-3.json", title="Cold Iron Sunday", style="blues", bpm=68,
    key="E", mode="natural_minor", tuning="standard", seed=7412,
    complexity=0.36, humanize=0.70, fills=0.72, ending="fade",
    blurb="COLD IRON SUNDAY - a minor blues at 68, which is a different animal\n"
          "from the shuffle. Slow, heavy, and the twelve bar form stretched\n"
          "until it nearly comes apart.",
    sections=[
        ("intro", 4, 0.20, "half_time", ["Em"], dict(bass="roots", guitar="sparse", piano="sparse", lead="piano")),
        ("verse1", 12, 0.40, "half_time", ["Em", "Em", "Em", "Em", "Am", "Am", "Em", "Em", "B7", "Am", "Em", "B7"],
         dict(bass="roots", guitar="sparse", piano="driving", lead="piano")),
        ("verse2", 12, 0.52, "straight", ["Em", "Em", "Em", "Em", "Am", "Am", "Em", "Em", "B7", "Am", "Em", "B7"],
         dict(bass="eighths", guitar="open", piano="driving", lead="piano")),
        ("solo", 12, 0.74, "straight", ["Em", "Em", "Em", "Em", "Am", "Am", "Em", "Em", "B7", "Am", "Em", "B7"],
         dict(bass="eighths", guitar="driving", guitar2="solo", piano="sparse")),
        ("verse3", 12, 0.68, "straight", ["Em", "Em", "Em", "Em", "Am", "Am", "Em", "Em", "B7", "Am", "Em", "B7"],
         dict(bass="eighths", guitar="open", piano="driving", lead="both", fill="big")),
        ("ending", 8, 0.30, "half_time", ["Em"], dict(bass="roots", guitar="open", piano="open", lead="both", fill="none")),
    ])

# ----------------------------------------------------------- prog metal -----
add(file="preset-prog-2.json", title="Seventeen Windows", style="prog_metal", bpm=126,
    key="D", mode="phrygian", tuning="drop_d", seed=7413,
    complexity=0.78, humanize=0.36, fills=0.68, ending="hard_stop",
    blurb="SEVENTEEN WINDOWS - prog at 126 with the highest complexity in the\n"
          "set. Sections of 5, 7, 9 and 11 bars, so nothing lands where the\n"
          "ear expects and the song never settles into a loop.",
    sections=[
        ("intro", 5, 0.40, "straight", ["Dm"], dict(bass="lock_kick", guitar="muted", fill="small")),
        ("theme", 7, 0.80, "straight", ["Dm", "Eb", "Dm", "C"], dict(bass="lock_kick", guitar="driving", fill="none")),
        ("verse1", 9, 0.62, "straight", ["Dm", "Dm", "Bb", "C"], dict(bass="lock_kick", guitar="muted")),
        ("heavy", 7, 0.92, "double_time", ["Dm", "Eb", "C", "Dm"], dict(bass="eighths", guitar="driving", fill="big")),
        ("verse2", 9, 0.66, "straight", ["Dm", "Dm", "Bb", "C"], dict(bass="lock_kick", guitar="muted")),
        ("quiet", 11, 0.24, "half_time", ["Bb", "C", "Dm", "Dm"], dict(bass="roots", guitar="sparse", fill="none")),
        ("solo", 11, 0.90, "double_time", ["Dm", "Bb", "C", "Eb"], dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus1", 7, 0.98, "straight", ["Bb", "C", "Dm", "Dm"], dict(bass="eighths", guitar="driving", fill="big")),
        ("ending", 5, 0.85, "straight", ["Dm"], dict(bass="lock_kick", guitar="muted", fill="none")),
    ])

add(file="preset-prog-3.json", title="Slow Machinery", style="prog_metal", bpm=88,
    key="B", mode="dorian", tuning="b_standard", seed=7414,
    complexity=0.70, humanize=0.44, fills=0.62, ending="ritard",
    blurb="SLOW MACHINERY - prog at 88 in B standard, with piano.\n"
          "Written the opposite way to Seventeen Windows: even eight bar\n"
          "sections throughout, so all the complexity has to come from the\n"
          "playing rather than from the shape.",
    sections=[
        ("intro", 8, 0.26, "half_time", ["Bm"], dict(bass="roots", guitar="sparse", piano="sparse", lead="piano")),
        ("theme", 8, 0.74, "straight", ["Bm", "Bm", "A", "E"], dict(bass="lock_kick", guitar="driving", piano="sparse", fill="none")),
        ("verse1", 8, 0.56, "straight", ["Bm", "Bm", "G", "A"], dict(bass="lock_kick", guitar="muted", piano="driving", lead="piano")),
        ("chorus1", 8, 0.88, "straight", ["G", "A", "Bm", "Bm"], dict(bass="eighths", guitar="driving", piano="open", fill="big")),
        ("verse2", 8, 0.60, "straight", ["Bm", "Bm", "G", "A"], dict(bass="lock_kick", guitar="muted", piano="driving", lead="piano")),
        ("development", 8, 0.78, "double_time", ["Bm", "A", "G", "E"], dict(bass="eighths", guitar="busy", piano="sparse")),
        ("solo", 12, 0.90, "straight", ["Bm", "G", "A", "E"], dict(bass="eighths", guitar="driving", guitar2="solo", piano="sparse")),
        ("chorus2", 8, 0.96, "straight", ["G", "A", "Bm", "Bm"], dict(bass="eighths", guitar="driving", piano="open", lead="both", fill="big")),
        ("ending", 8, 0.45, "half_time", ["Bm"], dict(bass="roots", guitar="open", piano="open", lead="both", fill="none")),
    ])

# ------------------------------------------------------- doom and groove ----
add(file="preset-doom-2.json", title="The Undertow", style="doom", bpm=54,
    key="D", mode="phrygian", tuning="drop_c", seed=7415,
    complexity=0.34, humanize=0.74, fills=0.40, ending="fade",
    blurb="THE UNDERTOW - 54bpm, slower even than Weight Of Water, and the\n"
          "slowest thing in the set. Two chords for six minutes. Fills are\n"
          "held low: the space IS the song.",
    sections=[
        ("intro", 8, 0.16, "half_time", ["Dm"], dict(bass="roots", guitar="sparse", fill="none")),
        ("riff", 8, 0.64, "half_time", ["Dm", "Dm", "Eb", "Dm"], dict(bass="lock_kick", guitar="driving", fill="small")),
        ("verse1", 8, 0.44, "half_time", ["Dm", "Dm", "Eb", "Dm"], dict(bass="lock_kick", guitar="muted")),
        ("chorus1", 8, 0.78, "half_time", ["Eb", "Dm", "Eb", "Dm"], dict(bass="eighths", guitar="open", fill="big")),
        ("breakdown", 8, 0.12, "half_time", ["Dm"], dict(bass="roots", guitar="sparse", fill="none")),
        ("verse2", 8, 0.50, "half_time", ["Dm", "Dm", "Eb", "Dm"], dict(bass="lock_kick", guitar="muted")),
        ("solo", 12, 0.70, "straight", ["Dm", "Eb", "Dm", "Eb"], dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("ending", 8, 0.34, "half_time", ["Dm"], dict(bass="roots", guitar="open", fill="none")),
    ])

add(file="preset-groove-2.json", title="Rust Belt", style="groove_metal", bpm=124,
    key="C", mode="phrygian_dominant", tuning="drop_c", seed=7416,
    complexity=0.62, humanize=0.46, fills=0.75, ending="hard_stop",
    blurb="RUST BELT - groove metal at 124, faster than Southern Teeth and\n"
          "straighter. Phrygian dominant over a drop C riff: the major third\n"
          "against the flat second is what makes it swagger.",
    sections=[
        ("riff", 8, 0.86, "straight", ["C", "Db", "C", "Bb"], dict(bass="lock_kick", guitar="muted", fill="none")),
        ("verse1", 8, 0.72, "straight", ["C", "C", "Ab", "Bb"], dict(bass="lock_kick", guitar="muted")),
        ("chorus1", 8, 0.94, "straight", ["Ab", "Bb", "C", "C"], dict(bass="eighths", guitar="driving", fill="big")),
        ("verse2", 8, 0.76, "straight", ["C", "C", "Ab", "Bb"], dict(bass="lock_kick", guitar="muted")),
        ("bridge", 8, 0.44, "half_time", ["Ab", "Ab", "Bb", "Bb"], dict(bass="roots", guitar="sparse", fill="small")),
        ("solo", 12, 0.92, "straight", ["C", "Ab", "Bb", "C"], dict(bass="eighths", guitar="driving", guitar2="solo")),
        ("chorus2", 8, 1.00, "straight", ["Ab", "Bb", "C", "C"], dict(bass="eighths", guitar="driving", fill="big")),
        ("ending", 4, 0.88, "straight", ["C"], dict(bass="lock_kick", guitar="muted", fill="none")),
    ])
