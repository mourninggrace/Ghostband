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

    // Some keyswitches latch - a brief press changes a mode that stays changed -
    // and some are momentary, applying only while the key is held. MODO shows
    // which is which in a Latch column: its playing styles latch, its ghost,
    // hammer and force-slap switches do not. A momentary switch blipped before
    // the note has already been released by the time the note sounds, so it
    // does nothing whatsoever.
    bool hold         = false;
};

// One of the instrument's own knobs, and what the arrangement should make it
// follow. Any number of these can be declared - these instruments have far
// more controls than a fixed set of names could cover, so the profile names
// them and the generator drives whatever it finds.
struct ControlDef
{
    std::string name;
    int    cc      = -1;
    // intensity - tracks how loud the section is
    // lead      - up when this part leads, down when it supports
    // peaks     - on for choruses and solos, off in quiet sections
    // rising    - climbs across the song, for something that should build
    // fixed     - parked at `low`, and sent once per section
    // none      - never sent at all, leaving whatever the instrument is set
    //             to alone. "fixed" is not this: it actively drives the
    //             control to the bottom of its range.
    // level     - driven by the part's mix knob rather than by the
    //             arrangement. This is how a mix knob reaches an
    //             instrument's own volume: CC 7 is channel volume and most
    //             instrument plugins ignore it outright, which is why the
    //             mix knobs appeared to do nothing at all.
    //
    // Some controls have no right answer to tie to the arrangement - which
    // amp, which character, which pedal. They change the sound rather than
    // the dynamics, so the useful thing is to choose one:
    //
    // random      - a fresh choice every section, for something that can
    //               come and go, like a pedal kicking in for the chorus
    // random once - one choice held for the whole song, for something a
    //               band would not change mid-song, like the amp
    //
    // Both draw from a stream derived from the song seed, so the result is
    // reproducible and a reroll rerolls it.
    std::string follows = "intensity";

    // "knob"   sweeps continuously through its range.
    // "switch" lands on fully off or fully on, because a button has no
    //          meaningful middle and half a switch is not a thing an
    //          instrument can be.
    // "select" is a chooser with a fixed number of named positions - an amp
    //          model, a voicing, a mic. It picks one position and holds it
    //          for the whole section, because a selector that drifts
    //          between its choices mid-section is a fault, not a
    //          performance. Needs `positions` to know how many there are.
    std::string type = "knob";

    double low  = 0.0;      // value at the bottom of its range
    double high = 1.0;      // value at the top

    // How many choices a "select" offers. Ignored by the other types.
    // A selector's positions are evenly spread across the controller's
    // range, which is how a host maps a stepped parameter: position p of n
    // sits at p/(n-1), so the first is fully down and the last fully up.
    int positions = 0;

    bool isSwitch() const { return type == "switch"; }
    bool isSelect() const { return type == "select" && positions >= 2; }

    // The value this control sits at when `t` of its range is called for,
    // as 0..1 across the controller. Kept here rather than in the generator
    // so the plugin's list, the CLI and the renderer cannot disagree about
    // what a mapping actually does.
    double valueAt (double t) const;
};


// The controls one profile declares, and everything done with them. Held by
// every profile type rather than only the phrase one: a drum kit and a bass
// have knobs worth reaching too, and the mix knobs need somewhere to land.
class ControlSet
{
public:
    bool any() const                                 { return ! defs.empty(); }
    const std::vector<ControlDef>& all() const       { return defs; }

    // Editable, because which knobs are worth automating is the owner's
    // decision and there is no sensible fixed list of them.
    std::vector<ControlDef>& editable()              { return defs; }

    int ccFor (const std::string& control) const;    // -1 when not mapped

    // The next CC not already spoken for, so a newly added control never
    // collides with one that has already been taught.
    int nextFreeCC() const;

    // Only the controls block, indented to sit inside a profile file. save()
    // splices this into the existing file rather than rewriting it, so a
    // profile's comments - which are the measured findings about the
    // instrument - survive being saved over.
    std::string toJson() const;

    // Emits the CC moves an arrangement asked for. Controls the arrangement
    // must not touch - "none" and "level" - are dropped here, which is the one
    // place a controller message actually gets written.
    void render (const std::vector<ControlIntent>& intents, int channel,
                 int leadTicks, MidiTrack& track) const;

    // True when some control is taught to follow the part's mix knob.
    bool hasLevelControl() const;

private:
    std::vector<ControlDef> defs;
};

// Splices a controls block into an existing profile file, leaving every other
// byte alone. Shared by all three profile types.
bool saveControlsInto (const std::string& path, const ControlSet& controls,
                       std::string& error);

// Replaces one named block in a profile file, leaving every other byte alone.
// `block` is the whole replacement including the key.
bool spliceProfileBlock (const std::string& path, const std::string& key,
                         const std::string& block, std::string& error);

// Reads one named block back out, including the key, in the exact form
// spliceProfileBlock would write. False if the file has no such block.
//
// This is what lets an install merge rather than clobber: the four blocks the
// plugin ever writes back - a taught control map, a calibrated drum map, a bass
// range and a chord zone - can be lifted out of the copy already installed and
// carried into the incoming one.
bool extractProfileBlock (const std::string& path, const std::string& key,
                          std::string& block);

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

    // A kit has knobs too - room, overheads, bleed - and its volume is reached
    // the same way every other instrument's is.
    ControlSet  controls;
    std::string sourcePath;

    bool save (const std::string& path, std::string& error) const
    {
        return saveControlsInto (path, controls, error);
    }

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

    // A range can be confirmed by ear long before anybody works out what this
    // instrument's keyswitches mean, and a guessed keyswitch can silence it
    // outright - so the two are tracked separately and articulations are only
    // sent once somebody has actually checked them.
    bool articulationsVerified = false;

    int lowestNoteFor (const std::string& tuning) const;

    // For writing a calibrated range back to the profile file.
    const std::vector<std::pair<std::string, int>>& allLowestNotes() const
    {
        return lowestByTuning;
    }

    void setLowestNoteFor (const std::string& tuning, int note);

    ArticulationMapping articulation (BassArtic a) const;

    ControlSet  controls;
    std::string sourcePath;

    bool save (const std::string& path, std::string& error) const
    {
        return saveControlsInto (path, controls, error);
    }

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

    // Strum spread: how far apart the notes of one chord are struck. A real
    // guitarist never sounds a chord's notes simultaneously, and simultaneous
    // notes are most of why a generated guitar part sounds like a keyboard.
    // Zero for a piano, where the notes genuinely do land together.
    int  strumTicks = 0;

    // Controls are declared per profile; see gb::ControlSet.
    using ControlDef = gb::ControlDef;

    ControlSet controls;

    // Kept as thin forwards so every existing call site still reads the same.
    int  ccFor (const std::string& c) const        { return controls.ccFor (c); }
    bool hasControls() const                       { return controls.any(); }
    const std::vector<ControlDef>& allControls() const { return controls.all(); }
    std::vector<ControlDef>& editableControls()    { return controls.editable(); }
    int  nextFreeCC() const                        { return controls.nextFreeCC(); }
    std::string controlsJson() const               { return controls.toJson(); }

    // Writes the profile back in the format load() reads, so a mapping made in
    // the plugin survives and travels with the profile.
    std::string toJson() const;

    bool save (const std::string& path, std::string& error) const;

    std::string sourcePath;   // where this was loaded from, for saving back

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
