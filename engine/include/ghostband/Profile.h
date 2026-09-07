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
// What a control should probably follow, guessed from what it is called.
//
// Choosing between intensity, lead, level, random, random once, fixed and none
// asks the owner of a rig to already know how this engine thinks - and the
// first person to map eleven controls got most of them wrong, not through
// carelessness but because there was nothing to go on. A knob named "volume"
// has one sensible answer; so does one named "tune".
//
// A SUGGESTION, never a decision. It is applied when a control is first named
// and never again, so anything chosen by hand afterwards stands.
struct ControlSuggestion
{
    std::string follows = "intensity";
    std::string type    = "knob";
    double      low     = 0.0;
    double      high    = 1.0;
    int         positions = 0;

    // Why, in a few words, so the interface can say it out loud rather than
    // changing a field under someone and hoping they notice.
    std::string because;
};

ControlSuggestion suggestControl (const std::string& name);

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

// Reads a set of taught controls out of JSON text of the form ControlSet::toJson
// writes - `"controls": { ... }` - so the same mappings can live somewhere other
// than inside a profile file. False if there is no controls block in it.
bool parseControlsJson (const std::string& text, ControlSet& out);

class DrumProfile
{
public:
    DrumProfile();

    std::string name = "Generic GM drums";
    std::string id   = "gm";

    // Which real plugin this profile drives, as opposed to which profile it is.
    //
    // Seven files describe the same SSD5 - Terry Date, Classic, Deluxe 1 and 2,
    // Designer and the rest - and they are all one plugin in one rack with one
    // set of MIDI Learn assignments. Taught control mappings belong to that
    // plugin, not to whichever file happens to be loaded, so they are stored
    // against this rather than against `id`. Falls back to `id` when a profile
    // does not say, which is what every profile written before this did.
    std::string instrument;

    // False when this instrument's volume cannot be reached from outside at all.
    //
    // SSD5 exposes exactly one named parameter, Bypass, and 2080 anonymous
    // "MIDI CC n|m" slots; its own Map page will not assign a volume CC, and it
    // does not implement CC 7. So no control here can follow "level", CC 7 is
    // not a fallback, and a mix knob for it is a knob that does nothing - which
    // this project has repeatedly found to be worse than no knob at all.
    //
    // The plugin hides that part's mix knob when this is false. Use a gain block
    // in the host instead. Default true: an instrument is assumed reachable
    // until somebody establishes otherwise.
    bool volumeReachable = true;


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

    // Which real plugin this profile drives, as opposed to which profile it is.
    //
    // Seven files describe the same SSD5 - Terry Date, Classic, Deluxe 1 and 2,
    // Designer and the rest - and they are all one plugin in one rack with one
    // set of MIDI Learn assignments. Taught control mappings belong to that
    // plugin, not to whichever file happens to be loaded, so they are stored
    // against this rather than against `id`. Falls back to `id` when a profile
    // does not say, which is what every profile written before this did.
    std::string instrument;

    // False when this instrument's volume cannot be reached from outside at all.
    //
    // SSD5 exposes exactly one named parameter, Bypass, and 2080 anonymous
    // "MIDI CC n|m" slots; its own Map page will not assign a volume CC, and it
    // does not implement CC 7. So no control here can follow "level", CC 7 is
    // not a fallback, and a mix knob for it is a knob that does nothing - which
    // this project has repeatedly found to be worse than no knob at all.
    //
    // The plugin hides that part's mix knob when this is false. Use a gain block
    // in the host instead. Default true: an instrument is assumed reachable
    // until somebody establishes otherwise.
    bool volumeReachable = true;


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

    // Which real plugin this profile drives, as opposed to which profile it is.
    //
    // Seven files describe the same SSD5 - Terry Date, Classic, Deluxe 1 and 2,
    // Designer and the rest - and they are all one plugin in one rack with one
    // set of MIDI Learn assignments. Taught control mappings belong to that
    // plugin, not to whichever file happens to be loaded, so they are stored
    // against this rather than against `id`. Falls back to `id` when a profile
    // does not say, which is what every profile written before this did.
    std::string instrument;

    // False when this instrument's volume cannot be reached from outside at all.
    //
    // SSD5 exposes exactly one named parameter, Bypass, and 2080 anonymous
    // "MIDI CC n|m" slots; its own Map page will not assign a volume CC, and it
    // does not implement CC 7. So no control here can follow "level", CC 7 is
    // not a fallback, and a mix knob for it is a knob that does nothing - which
    // this project has repeatedly found to be worse than no knob at all.
    //
    // The plugin hides that part's mix knob when this is false. Use a gain block
    // in the host instead. Default true: an instrument is assumed reachable
    // until somebody establishes otherwise.
    bool volumeReachable = true;


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

    // Articulation keyswitches, for an instrument that plays notes.
    //
    // A phrase-driven instrument's keyswitch IS the performance: you hold it and
    // it plays a riff. A notes instrument's keyswitch only chooses HOW the notes
    // it is sent will sound - palm muted, staccato, a power chord, tremolo - and
    // is tapped rather than held, because it latches. Same `phrases` block in
    // the file, because the thing being chosen is still the section's feel;
    // completely different emission.
    //
    // Off unless the profile says the numbers have been checked. A guessed
    // keyswitch is the one mistake in this project that has silenced a whole
    // song, so the same discipline the bass articulations use applies here: the
    // notes go out and the switches do not, until somebody has confirmed them.
    bool keyswitchesVerified = false;

    // Vibrato, on the notes a phrase lands on.
    //
    // The other half of the gesture the bend starts: a guitarist slides up into
    // a note and then shakes it, and a held note with no vibrato is the single
    // most obvious tell that nobody is holding the instrument.
    //
    // Sent as a controller rather than a keyswitch, so like the bend the worst
    // case is that nothing happens - which is why it can be declared before it
    // has been heard. Shreddage takes it on the modwheel by default; anything
    // that does not is simply unaffected.
    //
    // Off unless a profile names the controller.
    int vibratoCC     = -1;
    int vibratoDepth  = 90;    // how far the controller is pushed, 0-127
    int vibratoTicks  = 160;   // how long it takes to come in

    // Bends, and whether this instrument has any.
    //
    // Off unless a profile says otherwise, and deliberately so. Pitch bend is a
    // channel message: it moves everything sounding on the channel, and one left
    // off centre leaves the whole part out of tune until something resets it. An
    // instrument that ignores bend loses nothing by never being sent one, and an
    // instrument that honours it must not be sent one until somebody has heard
    // that it does.
    //
    // `bendRangeSemitones` is the instrument's own wheel range, not a choice -
    // getting it wrong puts the bend short or sharp of the note it is aiming at.
    // Two semitones is the near-universal default.
    bool   canBend            = false;
    double bendRangeSemitones = 2.0;

    // How far a bend reaches for, and how long it takes to arrive. A whole tone
    // over about a sixteenth is the ordinary rock bend.
    double bendSemitones      = 2.0;
    int    bendTicks          = 90;

    // How far one lead note runs into the next, in ticks. Zero means they do not
    // touch, which is what every instrument here needed until now.
    //
    // Some libraries key their legato off exactly that overlap: a new note taken
    // while the previous one is still sounding is hammered on or pulled off
    // rather than picked, with no keyswitch involved. Shreddage does, and it is
    // the difference between a fast line and a fluid one. Ghostband could not
    // produce a legato note at all before this, because the lead renderer cut
    // every note to end precisely where the next began.
    //
    // Only ever a few ticks. This is a trigger, not a musical overlap - too much
    // and a monophonic instrument starts stealing its own voices.
    int legatoOverlapTicks = 0;

    // How far apart two notes can be and still be hammered on or pulled off.
    //
    // Shreddage's manual is explicit: the hammer/pull range defaults to two
    // semitones, and a wider interval simply will not trigger one. Overlapping
    // a leap therefore buys nothing and costs something - on a monophonic
    // instrument it is one more chance to steal a voice.
    //
    // It also matters musically. A player hammers within a position and picks
    // when the hand moves, so overlapping everything is not just useless at the
    // leaps, it is wrong at them. And a line where every note is a hammer-on
    // fades: Shreddage scales legato volume down until a non-legato note is
    // struck, so a run with no picked notes in it quietly disappears.
    int legatoMaxLeapSemitones = 2;

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

    // How an instrument is told which articulation to use.
    //
    // Two mechanisms, because instruments offer two. A KEYSWITCH is a note
    // outside the playable range; a CC is a controller value. They are declared
    // in the same "phrases" block, exactly as bass articulations already are:
    //
    //   "muted": 13                     a keyswitch on note 13
    //   "muted": { "cc": 40, "value": 20 }   the same thing on a controller
    //
    // The CC form is the safer of the two and worth preferring wherever an
    // instrument offers it. A controller nothing has learned does nothing at
    // all; a note aimed at the wrong instrument gets PLAYED, which is how a
    // guitar once received another guitar's articulation switches and treated
    // them as music.
    struct PhraseSwitch
    {
        int note  = -1;   // -1 when this feel is not mapped by note
        int cc    = -1;   // -1 when this feel is not mapped by controller
        int value = 0;    // the controller value that selects it

        bool mapped()   const { return note >= 0 || cc >= 0; }
        bool byControl() const { return cc >= 0; }
    };

    PhraseSwitch switchFor (PhraseFeel f) const;

    // The per-note gestures, declared the same way as the section feels above
    // and selected by the same mechanism - because on this instrument they ARE
    // the same mechanism. Shreddage has one active articulation at a time,
    // whether it is "palm muted for this verse" or "pinch harmonic on this one
    // note", so both live on one selector.
    //
    // That is what makes a per-note gesture cost three messages rather than
    // one: select it, play the note, put the section's own articulation back.
    // See restoreAfterArtic.
    PhraseSwitch switchFor (LeadArtic a) const;
    bool hasLeadArtic (LeadArtic a) const { return switchFor (a).mapped(); }

    // -1 when this instrument has no key for that feel, in which case the
    // generator's choice is quietly ignored rather than triggering the wrong one.
    // Notes only - a feel mapped to a controller reports -1 here, because there
    // is no note to press and callers that want one are asking about notes.
    int keyFor (PhraseFeel f) const;
    bool hasFeel (PhraseFeel f) const { return switchFor (f).mapped(); }

    static bool load (const std::string& path, PhraseProfile& out, std::string& error);

    void render (const PhrasePart& part, MidiTrack& track) const;

    // Every phrase key this profile defines, lowest first, for calibration.
    std::vector<std::pair<PhraseFeel, int>> allPhraseKeys() const;

    // -1 removes it. Public so a keyswitch map can be built without a file -
    // the harness needs one, and calibration will want to write one.
    void setKeyFor (PhraseFeel f, int note);

private:
    std::vector<PhraseSwitch> phraseKeys;   // indexed by PhraseFeel
    std::vector<PhraseSwitch> leadArtics;   // indexed by LeadArtic
};

} // namespace gb
