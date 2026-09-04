#pragma once

#include <string>
#include <vector>

namespace gb {

// Musical intent, deliberately free of any plugin specifics. The generators emit
// these; a driver profile is the only thing that knows what a "kick" or a palm
// mute means in MIDI for a particular instrument. Nothing below may ever name a
// note number, a channel, or a product.
enum class DrumVoice
{
    Kick, Snare, SideStick, Rimshot,
    HatClosed, HatOpen, HatPedal,
    Ride, RideBell,
    Crash, Crash2, China, Splash,
    Tom1, Tom2, Tom3, Tom4,
    Count
};

const char* drumVoiceName     (DrumVoice v);
DrumVoice   drumVoiceFromName (const std::string& s, bool& ok);

struct DrumIntent
{
    int       tick   = 0;
    DrumVoice voice  = DrumVoice::Kick;
    double    accent = 0.7;      // 0..1, mapped to velocity by the profile
};

enum class BassArtic { Normal, PalmMute, Dead, Slide, Hammer, Slap, Pop };

const char* bassArticName     (BassArtic a);
BassArtic   bassArticFromName (const std::string& s, bool& ok);

struct BassIntent
{
    int       tick          = 0;
    int       durationTicks = 120;
    int       pitch         = 28;   // absolute MIDI note; profile clamps to range
    double    accent        = 0.7;
    BassArtic artic         = BassArtic::Normal;
};

// Phrase-driven instruments - the UJAM family - are a different dialect
// entirely. They do not want notes. You hold a chord in a low key zone and
// press a key in a phrase zone, and the instrument plays a professionally
// performed riff in that harmony. So Ghostband does not compose these parts:
// it chooses a phrase and supplies the chord. The generators still speak only
// in intent; a profile decides which keys that means for a given plugin.
enum class PhraseFeel
{
    Silent,      // the part sits this section out
    Sparse,      // long, open, minimal
    Muted,       // tight and palm-muted, tracking the kick
    Driving,     // steady rhythmic push
    Open,        // full sustained chords, the chorus lift
    Busy         // the most active option the instrument has
};

const char* phraseFeelName     (PhraseFeel f);
PhraseFeel  phraseFeelFromName (const std::string& s, bool& ok);

// Hold this harmony from tick for durationTicks. The profile voices it into
// whatever key range the target plugin reads chords from.
struct ChordIntent
{
    int    tick          = 0;
    int    durationTicks = 0;
    int    rootPc        = 0;   // 0..11
    int    thirdSemis    = 3;   // -1 for no third
    int    fifthSemis    = 7;
    int    seventhSemis  = -1;  // -1 for a plain triad
    double accent        = 0.7;
    bool   strumUp       = false;   // alternates, so chords do not all sweep alike
};

// Switch the instrument to this phrase. Emitted at section and phrase changes,
// never per note.
struct PhraseIntent
{
    int        tick   = 0;
    PhraseFeel feel   = PhraseFeel::Driving;
    double     accent = 0.8;
};

// A change to one of the instrument's own controls - gain, tone, an effect, a
// style. Ghostband cannot set another plugin's parameters directly, but these
// instruments listen to MIDI CC, so an arrangement decision can be expressed as
// a control move rather than only as notes. Which CC a name means is the
// profile's business; the generator only ever asks for "more drive".
struct ControlIntent
{
    int         tick   = 0;
    std::string control;      // "drive", "tone", "effect", "style"
    double      amount = 0.5; // 0..1, scaled to 0..127 by the profile
};

// One phrase-driven part: the chords it holds, the phrases it switches to, and
// the controls it moves.
struct PhrasePart
{
    std::vector<ChordIntent>   chords;
    std::vector<PhraseIntent>  phrases;
    std::vector<ControlIntent> controls;
};

struct Marker
{
    int         tick = 0;
    std::string text;
};

// Everything the renderer produces for one song, still plugin-agnostic.
struct Performance
{
    std::vector<DrumIntent> drums;
    std::vector<BassIntent> bass;

    // Drums and bass have knobs too - a room mic, a drive, the volume the mix
    // knob reaches. They are not phrase parts, so their control moves live here
    // rather than inside one.
    std::vector<ControlIntent> drumControls;
    std::vector<ControlIntent> bassControls;
    PhrasePart              guitar;
    PhrasePart              piano;
    std::vector<Marker>     markers;

    // Tempo changes, used only by endings that slow down.
    struct TempoPoint { int tick = 0; double bpm = 120.0; };
    std::vector<TempoPoint> tempoMap;

    int totalTicks = 0;
};

} // namespace gb
