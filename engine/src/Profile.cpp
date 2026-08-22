#include "ghostband/Profile.h"
#include "ghostband/Json.h"
#include "ghostband/MidiFile.h"

#include <algorithm>
#include <cctype>

namespace gb {

//==============================================================================
// Intent vocabulary. These names are the contract between the generators and
// every profile JSON file, so they must stay stable once profiles exist in the
// wild.

static const char* kDrumVoiceNames[] =
{
    "kick", "snare", "sidestick", "rimshot",
    "hat_closed", "hat_open", "hat_pedal",
    "ride", "ride_bell",
    "crash", "crash2", "china", "splash",
    "tom1", "tom2", "tom3", "tom4"
};

const char* drumVoiceName (DrumVoice v)
{
    const size_t i = static_cast<size_t> (v);
    return i < static_cast<size_t> (DrumVoice::Count) ? kDrumVoiceNames[i] : "kick";
}

DrumVoice drumVoiceFromName (const std::string& s, bool& ok)
{
    for (size_t i = 0; i < static_cast<size_t> (DrumVoice::Count); ++i)
    {
        if (s == kDrumVoiceNames[i])
        {
            ok = true;
            return static_cast<DrumVoice> (i);
        }
    }
    ok = false;
    return DrumVoice::Kick;
}

static const char* kBassArticNames[] =
{
    "normal", "palm_mute", "dead", "slide", "hammer", "slap", "pop"
};

static const size_t kNumBassArtics = sizeof (kBassArticNames) / sizeof (kBassArticNames[0]);

const char* bassArticName (BassArtic a)
{
    const size_t i = static_cast<size_t> (a);
    return i < kNumBassArtics ? kBassArticNames[i] : "normal";
}

BassArtic bassArticFromName (const std::string& s, bool& ok)
{
    for (size_t i = 0; i < kNumBassArtics; ++i)
    {
        if (s == kBassArticNames[i])
        {
            ok = true;
            return static_cast<BassArtic> (i);
        }
    }
    ok = false;
    return BassArtic::Normal;
}

//==============================================================================

static int clampInt (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int velocityFor (double accent, int lo, int hi)
{
    if (accent < 0.0) accent = 0.0;
    if (accent > 1.0) accent = 1.0;
    return clampInt (lo + static_cast<int> (accent * (hi - lo) + 0.5), 1, 127);
}

//==============================================================================

DrumProfile::DrumProfile()
    : noteMap (static_cast<size_t> (DrumVoice::Count), -1)
{
    // General MIDI defaults. Most drum libraries ship a GM-compatible mode, so
    // an unknown plugin makes broadly correct sounds before anyone writes it a
    // profile of its own.
    noteMap[static_cast<size_t> (DrumVoice::Kick)]      = 36;
    noteMap[static_cast<size_t> (DrumVoice::Snare)]     = 38;
    noteMap[static_cast<size_t> (DrumVoice::SideStick)] = 37;
    noteMap[static_cast<size_t> (DrumVoice::Rimshot)]   = 40;
    noteMap[static_cast<size_t> (DrumVoice::HatClosed)] = 42;
    noteMap[static_cast<size_t> (DrumVoice::HatPedal)]  = 44;
    noteMap[static_cast<size_t> (DrumVoice::HatOpen)]   = 46;
    noteMap[static_cast<size_t> (DrumVoice::Ride)]      = 51;
    noteMap[static_cast<size_t> (DrumVoice::RideBell)]  = 53;
    noteMap[static_cast<size_t> (DrumVoice::Crash)]     = 49;
    noteMap[static_cast<size_t> (DrumVoice::Crash2)]    = 57;
    noteMap[static_cast<size_t> (DrumVoice::China)]     = 52;
    noteMap[static_cast<size_t> (DrumVoice::Splash)]    = 55;
    noteMap[static_cast<size_t> (DrumVoice::Tom1)]      = 48;
    noteMap[static_cast<size_t> (DrumVoice::Tom2)]      = 47;
    noteMap[static_cast<size_t> (DrumVoice::Tom3)]      = 45;
    noteMap[static_cast<size_t> (DrumVoice::Tom4)]      = 43;
}

int DrumProfile::noteFor (DrumVoice v) const
{
    const size_t i = static_cast<size_t> (v);
    return i < noteMap.size() ? noteMap[i] : -1;
}

// Directory portion of a path, including the trailing separator.
static std::string directoryOf (const std::string& path)
{
    const size_t slash = path.find_last_of ("/\\");
    return slash == std::string::npos ? std::string() : path.substr (0, slash + 1);
}

bool DrumProfile::loadImpl (const std::string& path, DrumProfile& out,
                            std::string& error, int depth)
{
    if (depth > 8)
    {
        error = path + ": profile inheritance is nested too deeply (a cycle?)";
        return false;
    }

    Json j;
    if (! Json::parseFile (path, j, error))
        return false;

    if (! j.isObject())
    {
        error = path + ": top level must be a JSON object";
        return false;
    }

    // A kit variant states only what differs from its base. The five SSD5 kits
    // share one map and differ mostly in which pieces exist, so each variant is
    // a handful of lines rather than a copy of the whole thing.
    if (j.has ("inherits"))
    {
        const std::string parent = directoryOf (path) + j["inherits"].asString();
        if (! loadImpl (parent, out, error, depth + 1))
        {
            error = path + ": while loading inherited profile: " + error;
            return false;
        }
    }
    else
    {
        out = DrumProfile();
    }

    out.name              = j.stringOr ("name", out.name);
    out.id                = j.stringOr ("id", out.id);
    out.channel           = clampInt (j.intOr ("channel", out.channel), 1, 16);
    out.velocityMin       = clampInt (j.intOr ("velocity_min", out.velocityMin), 1, 127);
    out.velocityMax       = clampInt (j.intOr ("velocity_max", out.velocityMax), 1, 127);
    out.hitTicks          = std::max (1, j.intOr ("hit_ticks", out.hitTicks));
    out.needsVerification = j.boolOr ("needs_verification", false);
    out.verificationNote  = j.stringOr ("verification_note", "");

    if (out.velocityMax < out.velocityMin)
        std::swap (out.velocityMin, out.velocityMax);

    const Json& notes = j["notes"];
    if (notes.isObject())
    {
        for (const std::string& key : notes.keys())
        {
            bool ok = false;
            const DrumVoice v = drumVoiceFromName (key, ok);
            if (! ok)
            {
                error = path + ": unknown drum voice \"" + key + "\"";
                return false;
            }

            const Json& value = notes[key];

            // null means "this kit does not have that voice", which is different
            // from omitting the key (keep the inherited default).
            out.noteMap[static_cast<size_t> (v)] =
                value.isNull() ? -1 : clampInt (value.asInt (-1), -1, 127);
        }
    }

    return true;
}

bool DrumProfile::load (const std::string& path, DrumProfile& out, std::string& error)
{
    return loadImpl (path, out, error, 0);
}

void DrumProfile::render (const std::vector<DrumIntent>& intents, MidiTrack& track) const
{
    for (const DrumIntent& d : intents)
    {
        const int note = noteFor (d.voice);
        if (note < 0)
            continue;   // kit has no such piece; the generator already chose a fallback

        const int vel = velocityFor (d.accent, velocityMin, velocityMax);
        track.addNoteOn  (d.tick, channel, note, vel);
        track.addNoteOff (d.tick + hitTicks, channel, note);
    }
}

//==============================================================================

BassProfile::BassProfile()
    : artics (kNumBassArtics)
{
    lowestByTuning = { { "standard", 28 }, { "drop_d", 26 }, { "drop_c", 24 }, { "b_standard", 23 } };
}

int BassProfile::lowestNoteFor (const std::string& tuning) const
{
    for (const auto& kv : lowestByTuning)
        if (kv.first == tuning) return kv.second;

    // Unknown tuning: assume standard rather than guessing a lower one, so we
    // never write notes below what the instrument can actually play.
    for (const auto& kv : lowestByTuning)
        if (kv.first == "standard") return kv.second;

    return 28;
}

ArticulationMapping BassProfile::articulation (BassArtic a) const
{
    const size_t i = static_cast<size_t> (a);
    return i < artics.size() ? artics[i] : ArticulationMapping();
}

bool BassProfile::load (const std::string& path, BassProfile& out, std::string& error)
{
    Json j;
    if (! Json::parseFile (path, j, error))
        return false;

    if (! j.isObject())
    {
        error = path + ": top level must be a JSON object";
        return false;
    }

    out = BassProfile();

    out.name               = j.stringOr ("name", out.name);
    out.id                 = j.stringOr ("id", out.id);
    out.channel            = clampInt (j.intOr ("channel", out.channel), 1, 16);
    out.velocityMin        = clampInt (j.intOr ("velocity_min", out.velocityMin), 1, 127);
    out.velocityMax        = clampInt (j.intOr ("velocity_max", out.velocityMax), 1, 127);
    out.highestNote        = clampInt (j.intOr ("highest_note", out.highestNote), 0, 127);
    out.keyswitchLeadTicks = std::max (1, j.intOr ("keyswitch_lead_ticks", out.keyswitchLeadTicks));
    out.keyswitchVelocity  = clampInt (j.intOr ("keyswitch_velocity", out.keyswitchVelocity), 1, 127);
    out.needsVerification  = j.boolOr ("needs_verification", false);
    out.verificationNote   = j.stringOr ("verification_note", "");

    if (out.velocityMax < out.velocityMin)
        std::swap (out.velocityMin, out.velocityMax);

    const Json& low = j["lowest_note"];
    if (low.isObject())
    {
        out.lowestByTuning.clear();
        for (const std::string& key : low.keys())
            out.lowestByTuning.emplace_back (key, clampInt (low[key].asInt (28), 0, 127));
    }

    const Json& arts = j["articulations"];
    if (arts.isObject())
    {
        for (const std::string& key : arts.keys())
        {
            bool ok = false;
            const BassArtic a = bassArticFromName (key, ok);
            if (! ok)
            {
                error = path + ": unknown bass articulation \"" + key + "\"";
                return false;
            }

            const Json& def = arts[key];
            ArticulationMapping m;
            m.defined = true;

            if (def.has ("keyswitch"))
            {
                m.hasKeyswitch = true;
                m.keyswitch    = clampInt (def.intOr ("keyswitch", 0), 0, 127);
            }
            if (def.has ("cc"))
            {
                m.hasCC   = true;
                m.cc      = clampInt (def.intOr ("cc", 1), 0, 127);
                m.ccValue = clampInt (def.intOr ("value", 0), 0, 127);
            }

            out.artics[static_cast<size_t> (a)] = m;
        }
    }

    return true;
}

void BassProfile::render (const std::vector<BassIntent>& intents,
                          MidiTrack& track,
                          const std::string& tuning) const
{
    const int lowest = lowestNoteFor (tuning);

    // Only re-send an articulation when it actually changes. Re-stating it on
    // every note floods the plugin and, on some instruments, retriggers.
    int lastArtic = -1;

    for (const BassIntent& b : intents)
    {
        // Fold anything below the instrument into range instead of clamping every
        // low note onto the same pitch, which would flatten a riff into a drone.
        int pitch = b.pitch;
        while (pitch < lowest)        pitch += 12;
        while (pitch > highestNote)   pitch -= 12;
        if (pitch < lowest) continue;             // range too narrow to place it

        const int articIndex = static_cast<int> (b.artic);
        if (articIndex != lastArtic)
        {
            const ArticulationMapping m = articulation (b.artic);
            if (m.defined)
            {
                if (m.hasCC)
                    track.addCC (std::max (0, b.tick - keyswitchLeadTicks), channel, m.cc, m.ccValue);

                if (m.hasKeyswitch)
                {
                    const int ksOn = std::max (0, b.tick - keyswitchLeadTicks);
                    track.addNoteOn  (ksOn, channel, m.keyswitch, keyswitchVelocity);
                    track.addNoteOff (std::max (ksOn + 1, b.tick - 1), channel, m.keyswitch);
                }
            }
            lastArtic = articIndex;
        }

        const int vel = velocityFor (b.accent, velocityMin, velocityMax);
        const int dur = std::max (1, b.durationTicks);

        track.addNoteOn  (b.tick, channel, pitch, vel);
        track.addNoteOff (b.tick + dur, channel, pitch);
    }
}

} // namespace gb
