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
    std::vector<Marker>     markers;

    // Tempo changes, used only by endings that slow down.
    struct TempoPoint { int tick = 0; double bpm = 120.0; };
    std::vector<TempoPoint> tempoMap;

    int totalTicks = 0;
};

} // namespace gb
