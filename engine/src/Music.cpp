#include "ghostband/Music.h"
#include "ghostband/Rng.h"

#include <algorithm>
#include <cctype>

namespace gb {

int Chord::thirdSemitones() const
{
    switch (quality)
    {
        case ChordQuality::Power:      return -1;
        case ChordQuality::Sus2:       return 2;
        case ChordQuality::Sus4:       return 5;
        case ChordQuality::Minor:
        case ChordQuality::Minor7:
        case ChordQuality::Diminished: return 3;
        default:                       return 4;
    }
}

int Chord::fifthSemitones() const
{
    switch (quality)
    {
        case ChordQuality::Diminished: return 6;
        case ChordQuality::Augmented:  return 8;
        default:                       return 7;
    }
}

//==============================================================================

static std::string lower (std::string s)
{
    for (char& c : s)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return s;
}

Mode modeFromString (const std::string& s)
{
    const std::string m = lower (s);
    if (m == "major" || m == "ionian")        return Mode::Major;
    if (m == "harmonic_minor")                return Mode::HarmonicMinor;
    if (m == "phrygian")                      return Mode::Phrygian;
    if (m == "phrygian_dominant")             return Mode::PhrygianDominant;
    if (m == "dorian")                        return Mode::Dorian;
    if (m == "mixolydian")                    return Mode::Mixolydian;
    return Mode::NaturalMinor;
}

std::string modeToString (Mode m)
{
    switch (m)
    {
        case Mode::Major:            return "major";
        case Mode::HarmonicMinor:    return "harmonic_minor";
        case Mode::Phrygian:         return "phrygian";
        case Mode::PhrygianDominant: return "phrygian_dominant";
        case Mode::Dorian:           return "dorian";
        case Mode::Mixolydian:       return "mixolydian";
        default:                     return "natural_minor";
    }
}

const std::vector<int>& scaleIntervals (Mode m)
{
    static const std::vector<int> major            { 0, 2, 4, 5, 7, 9, 11 };
    static const std::vector<int> naturalMinor     { 0, 2, 3, 5, 7, 8, 10 };
    static const std::vector<int> harmonicMinor    { 0, 2, 3, 5, 7, 8, 11 };
    static const std::vector<int> phrygian         { 0, 1, 3, 5, 7, 8, 10 };
    static const std::vector<int> phrygianDominant { 0, 1, 4, 5, 7, 8, 10 };
    static const std::vector<int> dorian           { 0, 2, 3, 5, 7, 9, 10 };
    static const std::vector<int> mixolydian       { 0, 2, 4, 5, 7, 9, 10 };

    switch (m)
    {
        case Mode::Major:            return major;
        case Mode::HarmonicMinor:    return harmonicMinor;
        case Mode::Phrygian:         return phrygian;
        case Mode::PhrygianDominant: return phrygianDominant;
        case Mode::Dorian:           return dorian;
        case Mode::Mixolydian:       return mixolydian;
        default:                     return naturalMinor;
    }
}

int pitchClassFromName (const std::string& name, bool& ok)
{
    ok = false;
    if (name.empty()) return 0;

    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G

    char letter = static_cast<char> (std::toupper (static_cast<unsigned char> (name[0])));
    if (letter < 'A' || letter > 'G') return 0;

    int pc = base[letter - 'A'];
    size_t i = 1;
    while (i < name.size() && (name[i] == '#' || name[i] == 'b' || name[i] == 'B'
                               || name[i] == 's' || name[i] == 'S'))
    {
        // Only treat 'b' as a flat when it directly follows the letter, so "Bb"
        // reads as B-flat but "Bdim" does not lose its B.
        if (name[i] == '#' || name[i] == 's' || name[i] == 'S') pc += 1;
        else if (i == 1)                                        pc -= 1;
        else break;
        ++i;
    }

    ok = true;
    return ((pc % 12) + 12) % 12;
}

std::string pitchClassName (int pc)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
    return names[((pc % 12) + 12) % 12];
}

Chord parseChord (const std::string& text)
{
    Chord c;
    c.text = text;

    if (text.empty()) return c;

    // Split the root off the front, then classify whatever remains.
    size_t split = 1;
    if (text.size() > 1 && (text[1] == '#' || text[1] == 'b'))
        split = 2;

    bool ok = false;
    c.rootPc = pitchClassFromName (text.substr (0, split), ok);
    if (! ok) return c;

    const std::string suffix = lower (text.substr (split));

    if      (suffix.empty())                              c.quality = ChordQuality::Major;
    else if (suffix == "5")                               c.quality = ChordQuality::Power;
    else if (suffix == "maj7" || suffix == "M7")          c.quality = ChordQuality::Major7;
    else if (suffix == "m7"   || suffix == "min7")        c.quality = ChordQuality::Minor7;
    else if (suffix == "7")                               c.quality = ChordQuality::Dominant7;
    else if (suffix == "m"    || suffix == "min"
             || suffix == "-")                            c.quality = ChordQuality::Minor;
    else if (suffix == "dim"  || suffix == "o")           c.quality = ChordQuality::Diminished;
    else if (suffix == "aug"  || suffix == "+")           c.quality = ChordQuality::Augmented;
    else if (suffix == "sus2")                            c.quality = ChordQuality::Sus2;
    else if (suffix == "sus"  || suffix == "sus4")        c.quality = ChordQuality::Sus4;
    else                                                  c.quality = ChordQuality::Major;

    c.valid = true;
    return c;
}

// Alt rock, emo, ballad and blues are deliberately absent: they voice full
// triads. A power chord carries no third, and the third is most of what those
// styles are made of - a ballad in fifths is a ballad with the tune taken out.
bool styleUsesPowerChords (const std::string& style)
{
    const std::string s = lower (style);
    return s == "metal"       || s == "thrash"      || s == "groove_metal"
        || s == "doom"        || s == "sludge"      || s == "hard_rock"
        || s == "punk"        || s == "prog_metal";
}

//==============================================================================

static ChordQuality triadQuality (const std::vector<int>& sc, int degree)
{
    const int r = sc[static_cast<size_t> (degree % 7)];
    const int t = sc[static_cast<size_t> ((degree + 2) % 7)];
    const int f = sc[static_cast<size_t> ((degree + 4) % 7)];

    const int third = (((t - r) % 12) + 12) % 12;
    const int fifth = (((f - r) % 12) + 12) % 12;

    if (fifth == 6)                 return ChordQuality::Diminished;
    if (fifth == 8 && third == 4)   return ChordQuality::Augmented;
    return third == 3 ? ChordQuality::Minor : ChordQuality::Major;
}

ChordQuality diatonicTriadQuality (int rootPc, int keyPc, Mode mode)
{
    const std::vector<int>& sc = scaleIntervals (mode);
    const int wanted = (((rootPc - keyPc) % 12) + 12) % 12;

    for (int degree = 0; degree < 7; ++degree)
        if (sc[static_cast<size_t> (degree)] == wanted)
            return triadQuality (sc, degree);

    // Not in the key - very common in rock, where a bVII or a bIII is borrowed
    // freely. Assume it behaves like the tonic does.
    return triadQuality (sc, 0);
}

std::vector<Chord> autoProgression (int keyPc,
                                    Mode mode,
                                    const std::string& style,
                                    const std::string& role,
                                    int bars,
                                    Rng& rng)
{
    const std::vector<int>& sc = scaleIntervals (mode);
    const std::string r = lower (role);
    const bool phrygianish = (mode == Mode::Phrygian || mode == Mode::PhrygianDominant);

    // Progressions as scale degrees, 0 = tonic. Chosen for rock and metal rather
    // than being general-purpose: heavy on i-VI-III-VII and root pedals.
    std::vector<std::vector<int>> pool;

    if (r == "intro" || r == "verse")
    {
        pool = { { 0, 0, 0, 0 },
                 { 0, 0, 5, 6 },
                 { 0, 6, 0, 6 },
                 { 0, 0, 3, 0 },
                 { 0, 5, 0, 6 } };
        if (phrygianish) pool.push_back ({ 0, 1, 0, 0 });
    }
    else if (r == "chorus")
    {
        pool = { { 0, 5, 2, 6 },
                 { 5, 6, 0, 0 },
                 { 0, 6, 5, 6 },
                 { 5, 2, 6, 0 } };
    }
    else if (r == "bridge")
    {
        pool = { { 3, 3, 5, 6 },
                 { 5, 5, 2, 6 },
                 { 3, 6, 0, 0 },
                 { 5, 0, 3, 6 } };
        if (phrygianish) pool.push_back ({ 1, 1, 0, 0 });
    }
    else if (r == "solo")
    {
        pool = { { 0, 5, 2, 6 },
                 { 0, 6, 5, 6 },
                 { 0, 3, 5, 6 } };
    }
    else   // ending, breakdown, anything unnamed
    {
        pool = { { 0, 6, 0, 0 },
                 { 0, 5, 6, 0 },
                 { 0, 0, 0, 0 } };
    }

    // Metal leans harder on a static root pedal than rock does; bias the draw
    // rather than removing the option, so it stays possible either way.
    if (styleUsesPowerChords (style) && rng.chance (0.35))
        pool.insert (pool.begin(), { 0, 0, 0, 0 });

    const std::vector<int>& cycle = pool[static_cast<size_t> (rng.below (static_cast<int> (pool.size())))];

    std::vector<Chord> out;
    out.reserve (static_cast<size_t> (std::max (bars, 0)));

    const bool power = styleUsesPowerChords (style);

    for (int bar = 0; bar < bars; ++bar)
    {
        const int degree = cycle[static_cast<size_t> (bar) % cycle.size()];

        Chord c;
        c.rootPc  = ((keyPc + sc[static_cast<size_t> (degree % 7)]) % 12 + 12) % 12;
        c.quality = power ? ChordQuality::Power : triadQuality (sc, degree);
        c.valid   = true;
        c.text    = pitchClassName (c.rootPc)
                  + (c.quality == ChordQuality::Power ? "5"
                     : c.quality == ChordQuality::Minor ? "m"
                     : c.quality == ChordQuality::Diminished ? "dim" : "");
        out.push_back (c);
    }

    return out;
}

} // namespace gb
