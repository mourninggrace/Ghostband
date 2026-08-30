#include "ghostband/Profile.h"
#include "ghostband/Json.h"
#include "ghostband/MidiFile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

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

static const char* kPhraseFeelNames[] =
{
    "silent", "sparse", "muted", "driving", "open", "busy"
};

static const size_t kNumPhraseFeels = sizeof (kPhraseFeelNames) / sizeof (kPhraseFeelNames[0]);

const char* phraseFeelName (PhraseFeel f)
{
    const size_t i = static_cast<size_t> (f);
    return i < kNumPhraseFeels ? kPhraseFeelNames[i] : "driving";
}

PhraseFeel phraseFeelFromName (const std::string& s, bool& ok)
{
    for (size_t i = 0; i < kNumPhraseFeels; ++i)
    {
        if (s == kPhraseFeelNames[i])
        {
            ok = true;
            return static_cast<PhraseFeel> (i);
        }
    }
    ok = false;
    return PhraseFeel::Driving;
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

// Either the short form, "drive": 22, or the full one:
//   "drive": { "cc": 22, "follows": "intensity", "low": 0.2, "high": 0.9 }
//
// Shared by every profile type, because a drum kit's knobs are declared exactly
// the way a guitar's are.
static void loadControls (const Json& j, ControlSet& out)
{
    const Json& controls = j["controls"];
    if (! controls.isObject())
        return;

    for (const std::string& key : controls.keys())
    {
        const Json& def = controls[key];
        ControlDef c;
        c.name = key;

        if (def.isObject())
        {
            c.cc        = clampInt (def.intOr ("cc", -1), -1, 127);
            c.follows   = def.stringOr ("follows", "intensity");
            c.type      = def.stringOr ("type", "knob");
            c.low       = def.numberOr ("low", 0.0);
            c.high      = def.numberOr ("high", 1.0);
            c.positions = clampInt (def.intOr ("positions", 0), 0, 128);
        }
        else
        {
            c.cc = clampInt (def.asInt (-1), -1, 127);
        }

        if (c.cc >= 0)
            out.editable().push_back (c);
    }
}

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

    loadControls (j, out.controls);
    out.sourcePath = path;

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

PhraseProfile::PhraseProfile()
    : phraseKeys (kNumPhraseFeels, -1)
{
    // The default is a plain pitched instrument, voicing chords in a middle
    // register. That is the safe fallback for an unknown target: pressing a
    // guessed phrase key triggers the wrong riff, whereas a chord in a normal
    // range just plays the chord. A file that wants phrase mode says so.
    phraseDriven = false;
    chordLowest  = 48;
    chordHighest = 72;
}

double ControlDef::valueAt (double t) const
{
    const auto clamp01 = [] (double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); };
    t = clamp01 (t);

    if (isSelect())
    {
        // A selector's declared range is in positions, not in a continuous
        // sweep: low and high say which of its choices this song is allowed to
        // use, and t picks one of those. Rounding to a position is the whole
        // point - landing between two choices would select neither.
        const int last = positions - 1;
        const auto toPos = [last, &clamp01] (double v)
        {
            const int p = static_cast<int> (clamp01 (v) * last + 0.5);
            return p < 0 ? 0 : (p > last ? last : p);
        };

        const int lo = toPos (low);
        const int hi = toPos (high);
        int p = lo + static_cast<int> (t * (hi - lo) + (hi >= lo ? 0.5 : -0.5));
        p = p < 0 ? 0 : (p > last ? last : p);

        return static_cast<double> (p) / static_cast<double> (last);
    }

    const double v = clamp01 (low + t * (high - low));

    // A switch still honours its declared range, which is what lets a control
    // be parked on: "follows": "fixed" with "low": 1 holds it down for the
    // whole song. Thresholding an unscaled t would have made that impossible.
    if (isSwitch())
        return v > 0.5 ? 1.0 : 0.0;

    return v;
}

int ControlSet::ccFor (const std::string& control) const
{
    for (const ControlDef& c : defs)
        if (c.name == control) return c.cc;
    return -1;
}

bool ControlSet::hasLevelControl() const
{
    for (const ControlDef& c : defs)
        if (c.follows == "level" && c.cc >= 0) return true;
    return false;
}

// Emits the controller moves an arrangement asked for.
//
// Both exclusions live here rather than only in the generator, because this is
// the one place a controller message is actually written and a guard anywhere
// else can be walked around. "none" means leave the instrument alone; "level"
// belongs to the part's mix knob, and letting the arrangement drive it too
// would have the two fighting over one controller.
void ControlSet::render (const std::vector<ControlIntent>& intents, int channel,
                         int leadTicks, MidiTrack& track) const
{
    for (const ControlIntent& c : intents)
    {
        const ControlDef* def = nullptr;
        for (const ControlDef& d : defs)
            if (d.name == c.control) { def = &d; break; }

        if (def == nullptr || def->cc < 0)
            continue;

        if (def->follows == "none" || def->follows == "level")
            continue;

        const int value = clampInt (static_cast<int> (c.amount * 127.0 + 0.5), 0, 127);
        track.addCC (std::max (0, c.tick - leadTicks), channel, def->cc, value);
    }
}

int ControlSet::nextFreeCC() const
{
    for (int cc = 22; cc <= 119; ++cc)
    {
        bool taken = false;
        for (const ControlDef& c : defs)
            if (c.cc == cc) { taken = true; break; }

        // Skip the controllers that already mean something universally, so a
        // mapping never fights channel volume or the sustain pedal.
        if (cc == 64 || cc == 7 || cc == 1 || cc == 11 || cc == 10)
            continue;

        if (! taken) return cc;
    }
    return -1;
}

std::string PhraseProfile::toJson() const
{
    auto q = [] (const std::string& s) { return "\"" + s + "\""; };
    auto num = [] (double v)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.3g", v);
        return std::string (buf);
    };

    std::string j = "{\n";
    j += "  // Written by Ghostband. Control mappings were made in the plugin.\n\n";
    j += "  \"name\": " + q (name) + ",\n";
    j += "  \"id\": "   + q (id)   + ",\n";
    j += "  \"channel\": " + std::to_string (channel) + ",\n\n";
    j += "  \"mode\": " + q (phraseDriven ? "phrase" : "notes") + ",\n\n";
    j += "  \"needs_verification\": " + std::string (needsVerification ? "true" : "false") + ",\n";
    if (! verificationNote.empty())
        j += "  \"verification_note\": " + q (verificationNote) + ",\n";
    j += "\n";
    j += "  \"chord_zone\": { \"lowest_note\": " + std::to_string (chordLowest)
       + ", \"highest_note\": " + std::to_string (chordHighest) + " },\n\n";
    j += "  \"velocity_min\": " + std::to_string (velocityMin) + ",\n";
    j += "  \"velocity_max\": " + std::to_string (velocityMax) + ",\n";
    j += "  \"strum_ticks\": "  + std::to_string (strumTicks) + ",\n";

    if (phraseDriven)
    {
        j += "  \"phrase_lead_ticks\": " + std::to_string (phraseLeadTicks) + ",\n";
        j += "  \"phrase_blip_ticks\": " + std::to_string (phraseBlipTicks) + ",\n";
        j += "  \"phrase_velocity\": "   + std::to_string (phraseVelocity) + ",\n";

        std::string ph;
        for (size_t i = 0; i < phraseKeys.size(); ++i)
            if (phraseKeys[i] >= 0)
                ph += (ph.empty() ? "" : ",\n") + std::string ("    ")
                    + q (phraseFeelName (static_cast<PhraseFeel> (i))) + ": "
                    + std::to_string (phraseKeys[i]);
        if (! ph.empty())
            j += "\n  \"phrases\": {\n" + ph + "\n  },\n";
    }

    j += "\n  " + controlsJson() + "\n}\n";
    return j;
}

// Just the controls block, indented to sit inside a profile file.
std::string ControlSet::toJson() const
{
    auto q = [] (const std::string& s) { return "\"" + s + "\""; };
    auto num = [] (double v)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.3g", v);
        return std::string (buf);
    };

    std::string ctl;
    for (const ControlDef& c : defs)
        ctl += (ctl.empty() ? "" : ",\n") + std::string ("    ") + q (c.name)
             + ": { \"cc\": " + std::to_string (c.cc)
             + ", \"follows\": " + q (c.follows)
             + ", \"type\": " + q (c.type)
             + (c.positions >= 2 ? ", \"positions\": " + std::to_string (c.positions)
                                 : std::string())
             + ", \"low\": " + num (c.low)
             + ", \"high\": " + num (c.high) + " }";

    if (ctl.empty())
        return "\"controls\": {}";

    return "\"controls\": {\n" + ctl + "\n  }";
}

// Replaces just the "controls" block in an existing profile and leaves every
// other byte of the file alone.
//
// Rewriting the whole file from toJson() cost a profile all of its comments the
// first time someone pressed Save - and in these files the comments are the
// findings: which note range a plugin actually sounds in, why it is not the
// range you would expect, which of its two modes the file is for. None of that
// can be regenerated by writing the struct back out. So a save edits in place
// and touches nothing it did not come to change.
//
// Returns false when there is no controls block to replace, so the caller can
// fall back to writing a fresh file.
static bool spliceControls (const std::string& path, const std::string& block,
                            std::string& out)
{
    std::ifstream in (path, std::ios::binary);
    if (! in) return false;

    std::string text ((std::istreambuf_iterator<char> (in)),
                       std::istreambuf_iterator<char>());
    if (text.empty()) return false;

    // Find the key outside of a comment, so the explanation written above a
    // controls block is never mistaken for the block itself.
    size_t at = std::string::npos;
    for (size_t i = 0; i + 1 < text.size(); ++i)
    {
        if (text[i] == '/' && text[i + 1] == '/')
        {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }

        if (text.compare (i, 10, "\"controls\"") == 0) { at = i; break; }
    }

    if (at == std::string::npos) return false;

    const size_t open = text.find ('{', at);
    if (open == std::string::npos) return false;

    // Walk to the matching close brace. A control's name is user-typed, so a
    // brace inside a string still has to be ignored.
    int depth = 0;
    bool inString = false;
    size_t close = std::string::npos;

    for (size_t i = open; i < text.size(); ++i)
    {
        const char ch = text[i];

        if (inString)
        {
            if (ch == '\\') { ++i; continue; }
            if (ch == '"') inString = false;
            continue;
        }

        if (ch == '"') { inString = true; continue; }
        if (ch == '{') ++depth;
        else if (ch == '}' && --depth == 0) { close = i; break; }
    }

    if (close == std::string::npos) return false;

    out = text.substr (0, at) + block + text.substr (close + 1);
    return true;
}

bool saveControlsInto (const std::string& path, const ControlSet& controls,
                       std::string& error)
{
    std::string text;
    if (! spliceControls (path, controls.toJson(), text))
    {
        // Nothing to splice into means the file has no controls block at all.
        // Adding one is safe; rewriting the file from a struct is not, so the
        // block is appended rather than the file regenerated.
        error = path + ": no \"controls\" block to write into";
        return false;
    }

    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    if (! f)
    {
        error = "could not open " + path + " for writing";
        return false;
    }

    f.write (text.data(), static_cast<std::streamsize> (text.size()));
    if (! f)
    {
        error = "failed while writing " + path;
        return false;
    }
    return true;
}

bool PhraseProfile::save (const std::string& path, std::string& error) const
{
    if (saveControlsInto (path, controls, error))
        return true;

    // A phrase profile can be written from scratch, because toJson knows how to
    // spell one. The other profile types cannot, so only this one falls back.
    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    if (! f)
    {
        error = "could not open " + path + " for writing";
        return false;
    }

    const std::string text = toJson();
    f.write (text.data(), static_cast<std::streamsize> (text.size()));
    if (! f)
    {
        error = "failed while writing " + path;
        return false;
    }

    error.clear();
    return true;
}

int PhraseProfile::keyFor (PhraseFeel f) const
{
    const size_t i = static_cast<size_t> (f);
    return i < phraseKeys.size() ? phraseKeys[i] : -1;
}

std::vector<std::pair<PhraseFeel, int>> PhraseProfile::allPhraseKeys() const
{
    std::vector<std::pair<PhraseFeel, int>> out;
    for (size_t i = 0; i < phraseKeys.size(); ++i)
        if (phraseKeys[i] >= 0)
            out.emplace_back (static_cast<PhraseFeel> (i), phraseKeys[i]);
    return out;
}

bool PhraseProfile::load (const std::string& path, PhraseProfile& out, std::string& error)
{
    Json j;
    if (! Json::parseFile (path, j, error))
        return false;

    if (! j.isObject())
    {
        error = path + ": top level must be a JSON object";
        return false;
    }

    out = PhraseProfile();

    out.name              = j.stringOr ("name", out.name);
    out.id                = j.stringOr ("id", out.id);
    out.channel           = clampInt (j.intOr ("channel", out.channel), 1, 16);
    out.velocityMin       = clampInt (j.intOr ("velocity_min", out.velocityMin), 1, 127);
    out.velocityMax       = clampInt (j.intOr ("velocity_max", out.velocityMax), 1, 127);
    out.phraseDriven      = (j.stringOr ("mode", "phrase") != "notes");
    out.phraseLeadTicks   = std::max (1, j.intOr ("phrase_lead_ticks", out.phraseLeadTicks));
    out.phraseBlipTicks   = std::max (1, j.intOr ("phrase_blip_ticks", out.phraseBlipTicks));
    out.phraseVelocity    = clampInt (j.intOr ("phrase_velocity", out.phraseVelocity), 1, 127);
    out.strumTicks        = clampInt (j.intOr ("strum_ticks", out.strumTicks), 0, 240);
    out.sourcePath        = path;
    out.needsVerification = j.boolOr ("needs_verification", false);
    out.verificationNote  = j.stringOr ("verification_note", "");

    if (out.velocityMax < out.velocityMin)
        std::swap (out.velocityMin, out.velocityMax);

    const Json& zone = j["chord_zone"];
    if (zone.isObject())
    {
        out.chordLowest  = clampInt (zone.intOr ("lowest_note", out.chordLowest), 0, 127);
        out.chordHighest = clampInt (zone.intOr ("highest_note", out.chordHighest), 0, 127);
        if (out.chordHighest < out.chordLowest)
            std::swap (out.chordLowest, out.chordHighest);
    }

    loadControls (j, out.controls);

    const Json& phrases = j["phrases"];
    if (phrases.isObject())
    {
        for (const std::string& key : phrases.keys())
        {
            bool ok = false;
            const PhraseFeel f = phraseFeelFromName (key, ok);
            if (! ok)
            {
                error = path + ": unknown phrase feel \"" + key + "\"";
                return false;
            }

            const Json& value = phrases[key];
            out.phraseKeys[static_cast<size_t> (f)] =
                value.isNull() ? -1 : clampInt (value.asInt (-1), -1, 127);
        }
    }

    return true;
}

void PhraseProfile::render (const PhrasePart& part, MidiTrack& track) const
{
    // Phrase keys are HELD, not tapped.
    //
    // This was measured, after a user reported the guitar cutting out: pressing
    // a phrase key and releasing it after ~50ms gives three per cent sustain and
    // the sound stops after about a sixth of a second. These instruments play
    // for as long as the key is down - they do not latch - so each phrase is
    // held until the next one begins, or until the last chord ends.
    if (phraseDriven && ! part.phrases.empty())
    {
        // Where the part stops sounding at all.
        int partEnd = 0;
        for (const ChordIntent& c : part.chords)
            partEnd = std::max (partEnd, c.tick + c.durationTicks);

        for (size_t i = 0; i < part.phrases.size(); ++i)
        {
            const PhraseIntent& p = part.phrases[i];
            const int key = keyFor (p.feel);
            if (key < 0)
                continue;   // this instrument has no such phrase; leave it as it was

            const int on = std::max (0, p.tick - phraseLeadTicks);

            // Held until the next phrase change, with a short gap so the two
            // holds do not overlap on the same key.
            int off = partEnd;
            for (size_t j = i + 1; j < part.phrases.size(); ++j)
            {
                if (keyFor (part.phrases[j].feel) < 0) continue;
                off = std::max (on + 1, part.phrases[j].tick - phraseLeadTicks - phraseBlipTicks);
                break;
            }

            if (off <= on)
                continue;

            track.addNoteOn  (on, channel, key, phraseVelocity);
            track.addNoteOff (off, channel, key);
        }
    }

    // Control moves go out before anything they are meant to affect. A control
    // the profile does not map is skipped in silence: the generator is allowed
    // to ask for "more drive" from an instrument that has no such knob.
    controls.render (part.controls, channel, phraseLeadTicks, track);

    const int zoneSpan = chordHighest - chordLowest;

    for (const ChordIntent& c : part.chords)
    {
        if (c.durationTicks <= 0)
            continue;

        const int lowPc = ((chordLowest % 12) + 12) % 12;
        const int root  = chordLowest + ((((c.rootPc - lowPc) % 12) + 12) % 12);

        // Voice the triad inside the zone. A phrase instrument reads the chord
        // from these notes, so the third has to be there - it is the only way it
        // can know major from minor.
        std::vector<int> notes { root };
        if (c.thirdSemis >= 0) notes.push_back (root + c.thirdSemis);
        notes.push_back (root + c.fifthSemis);

        const int vel = velocityFor (c.accent, velocityMin, velocityMax);

        // Strum. Alternating direction, so consecutive chords do not all sweep
        // the same way, and the trailing notes slightly softer as a real pick
        // loses energy across the strings.
        std::vector<int> voiced;
        for (int n : notes)
        {
            while (n > chordHighest && zoneSpan >= 12) n -= 12;
            if (n >= chordLowest && n <= chordHighest) voiced.push_back (n);
        }
        if (voiced.empty())
            continue;

        std::sort (voiced.begin(), voiced.end());
        if (c.strumUp)
            std::reverse (voiced.begin(), voiced.end());

        for (size_t i = 0; i < voiced.size(); ++i)
        {
            const int offset = strumTicks * static_cast<int> (i);
            const int noteVel = clampInt (vel - static_cast<int> (i) * 4, 1, 127);

            track.addNoteOn  (c.tick + offset, channel, voiced[i], noteVel);
            track.addNoteOff (c.tick + offset + c.durationTicks, channel, voiced[i]);
        }
    }
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

        // An unverified articulation map is a guess, and a guessed keyswitch or
        // controller can do far worse than nothing: MODO went completely silent
        // for whole songs because a CC nobody had checked was being sent as
        // though it were known. Notes are the safe part of a profile and
        // articulations are the risky part, so until the map is confirmed only
        // the notes go out. A plain bass line is a much better failure than no
        // bass at all.
        const int articIndex = needsVerification ? 0 : static_cast<int> (b.artic);
        if (articIndex != lastArtic && ! needsVerification)
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
