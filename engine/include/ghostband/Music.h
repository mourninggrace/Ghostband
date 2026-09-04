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

    // -1 for a plain triad. The seventh was parsed and then thrown away, so an
    // A7 voiced exactly like an A - which is why a twelve bar came out sounding
    // like a folk song with a shuffle on it. In blues the flat seventh over a
    // major third is not decoration, it is the sound.
    int seventhSemitones() const;
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

// What a solo draws on, which is not the same as what the chords are built
// from. Over dominant sevenths the mode's major third fights the blue third, so
// blues gets the minor pentatonic with the flat fifth added - the notes a
// player actually reaches for - while everything else solos from its own mode.
const std::vector<int>& soloScale (Mode m, const std::string& style);

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

// The triad quality a root implies inside a key. Power chords carry no third,
// which is fine for a bass but useless to a phrase-driven instrument that has
// to be told major or minor - so this recovers the harmony the power chord is
// standing in for. Falls back to the mode's tonic quality when the root is not
// diatonic.
ChordQuality diatonicTriadQuality (int rootPc, int keyPc, Mode mode);

} // namespace gb
