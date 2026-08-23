#pragma once

#include "ghostband/Intent.h"

#include <string>
#include <vector>

namespace gb {

class MidiTrack;

// How one articulation is requested from a target plugin: a keyswitch note, a
// controller move, or both. Anything a profile does not define simply plays
// as a normal note, so an incomplete profile degrades instead of breaking.
struct ArticulationMapping
{
    bool defined     = false;
    bool hasKeyswitch = false;
    int  keyswitch    = 0;
    bool hasCC        = false;
    int  cc           = 0;
    int  ccValue      = 0;
};

class DrumProfile
{
public:
    DrumProfile();

    std::string name = "Generic GM drums";
    std::string id   = "gm";
    int  channel     = 10;
    int  velocityMin = 25;
    int  velocityMax = 127;
    int  hitTicks    = 30;      // note length for one-shots

    // True when the mapping is derived rather than confirmed against the real
    // plugin. The CLI reports this so an unverified map is never mistaken for
    // a tested one.
    bool        needsVerification = false;
    std::string verificationNote;

    // Returns -1 when this kit has no such voice, letting the generator
    // substitute rather than emit a note that triggers the wrong sample.
    int noteFor (DrumVoice v) const;

    bool hasVoice (DrumVoice v) const { return noteFor (v) >= 0; }

    static bool load (const std::string& path, DrumProfile& out, std::string& error);

    void render (const std::vector<DrumIntent>& intents, MidiTrack& track) const;

private:
    // depth guards against a profile that inherits from itself.
    static bool loadImpl (const std::string& path, DrumProfile& out,
                          std::string& error, int depth);

    std::vector<int> noteMap;
};

class BassProfile
{
public:
    BassProfile();

    std::string name = "Generic bass";
    std::string id   = "generic_bass";
    int  channel     = 1;
    int  velocityMin = 30;
    int  velocityMax = 127;
    int  highestNote = 67;

    // Keyswitches must arrive before the note they modify, never on the same
    // tick, or a plugin can apply them one note late.
    int keyswitchLeadTicks = 30;
    int keyswitchVelocity  = 100;

    bool        needsVerification = false;
    std::string verificationNote;

    int lowestNoteFor (const std::string& tuning) const;

    ArticulationMapping articulation (BassArtic a) const;

    static bool load (const std::string& path, BassProfile& out, std::string& error);

    void render (const std::vector<BassIntent>& intents,
                 MidiTrack& track,
                 const std::string& tuning) const;

private:
    std::vector<std::pair<std::string, int>> lowestByTuning;
    std::vector<ArticulationMapping> artics;
};

// Driver for a phrase-driven instrument: the UJAM family and anything else that
// wants a chord held in one key zone and a phrase selected from another. The
// engine never names a key here - it says "muted" or "open" and this decides
// what that means for one specific plugin.
class PhraseProfile
{
public:
    PhraseProfile();

    std::string name = "Generic phrase instrument";
    std::string id   = "generic_phrase";
    int  channel     = 2;
    int  velocityMin = 60;
    int  velocityMax = 127;

    // Two kinds of target live behind this one class.
    //
    // Phrase-driven (UJAM guitars): hold a chord in a low key zone, press a key
    // in a phrase zone, and the instrument performs the riff. Ghostband states
    // the harmony and picks the phrase; the rhythm belongs to the instrument.
    //
    // Note-driven (an ordinary piano): there is no phrase to pick, so Ghostband
    // has to supply the rhythm itself by emitting chords repeatedly. The feel
    // then decides how often, rather than which phrase key to press.
    bool phraseDriven = true;

    bool isPhraseDriven() const { return phraseDriven; }

    // Where chords are voiced. Roots are folded into this range.
    int  chordLowest  = 24;
    int  chordHighest = 47;

    // Phrase keys are momentary: a short blip switches the active phrase, and
    // it must land before the chord it applies to.
    int  phraseLeadTicks   = 60;
    int  phraseBlipTicks   = 40;
    int  phraseVelocity    = 100;

    bool        needsVerification = false;
    std::string verificationNote;

    // -1 when this instrument has no key for that feel, in which case the
    // generator's choice is quietly ignored rather than triggering the wrong one.
    int keyFor (PhraseFeel f) const;
    bool hasFeel (PhraseFeel f) const { return keyFor (f) >= 0; }

    static bool load (const std::string& path, PhraseProfile& out, std::string& error);

    void render (const PhrasePart& part, MidiTrack& track) const;

    // Every phrase key this profile defines, lowest first, for calibration.
    std::vector<std::pair<PhraseFeel, int>> allPhraseKeys() const;

private:
    std::vector<int> phraseKeys;   // indexed by PhraseFeel
};

} // namespace gb
