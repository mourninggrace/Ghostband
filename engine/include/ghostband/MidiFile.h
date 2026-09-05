#pragma once

#include <string>
#include <vector>

namespace gb {

// Event ordering within a single tick. Note-offs must land before note-ons so a
// repeated pitch is not cut short by its own predecessor, and controller moves
// must land before the note they are meant to affect.
enum EventOrder
{
    orderMeta    = 0,
    orderCC      = 1,
    orderNoteOff = 2,
    orderNoteOn  = 3
};

struct MidiEvent
{
    int tick  = 0;
    int order = orderNoteOn;
    std::vector<unsigned char> bytes;   // status byte + data, no delta time
};

class MidiTrack
{
public:
    std::string name;
    std::vector<MidiEvent> events;

    void addNoteOn        (int tick, int channel, int note, int velocity);
    void addNoteOff       (int tick, int channel, int note);
    void addCC            (int tick, int channel, int cc, int value);

    // Pitch bend, as a signed offset in semitones. The instrument's own bend
    // range is what turns that into a wheel position, so the caller works in
    // semitones and never in the 14-bit number - getting that conversion wrong
    // in two places is how a part ends up a quarter tone out.
    //
    // Ordered with the controllers, so a bend always lands before the note it
    // is bending.
    void addPitchBend     (int tick, int channel, double semitones, double rangeSemitones);
    void addMarker        (int tick, const std::string& text);
    void addText          (int tick, const std::string& text);
    void addTempo         (int tick, double bpm);
    void addTimeSignature (int tick, int numerator, int denominator);
};

class MidiFile
{
public:
    explicit MidiFile (int ppq = 480) : ticksPerQuarter (ppq) {}

    int ticksPerQuarter;
    std::vector<MidiTrack> tracks;

    bool write (const std::string& path, std::string& error) const;
};

} // namespace gb
