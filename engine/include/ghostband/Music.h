#pragma once

#include <string>
#include <vector>

namespace gb {

class Rng;

enum class ChordQuality
{
    Major, Minor, Power, Diminished, Augmented,
    Sus2, Sus4, Major7, Minor7, Dominant7
};

struct Chord
{
    int          rootPc  = 0;                    // 0..11, C = 0
    ChordQuality quality = ChordQuality::Minor;
    bool         valid   = false;
    std::string  text;                           // as written, for markers and logs

    // Semitones above the root. Power chords report no third.
    int thirdSemitones() const;                  // -1 when the chord has no third
    int fifthSemitones() const;
};

enum class Mode
{
    Major, NaturalMinor, HarmonicMinor,
    Phrygian, PhrygianDominant, Dorian, Mixolydian
};

Mode        modeFromString (const std::string& s);
std::string modeToString   (Mode m);

// Returns 0..11, or leaves ok false when the name is not a pitch.
int         pitchClassFromName (const std::string& name, bool& ok);
std::string pitchClassName     (int pc);

const std::vector<int>& scaleIntervals (Mode m);

// Accepts forms like E, Em, E5, F#m, Bb, Am7, Gsus4, Bdim.
Chord parseChord (const std::string& text);

// Builds one chord per bar. Style decides whether the result is power chords
// (metal, hard rock, punk) or scale triads; role biases which progression
// families get picked, so a chorus does not open the same way a verse does.
std::vector<Chord> autoProgression (int keyPc,
                                    Mode mode,
                                    const std::string& style,
                                    const std::string& role,
                                    int bars,
                                    Rng& rng);

// True for styles that should be voiced as power chords throughout.
bool styleUsesPowerChords (const std::string& style);

} // namespace gb
