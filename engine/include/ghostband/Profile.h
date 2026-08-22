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

} // namespace gb
