#include "ghostband/MidiFile.h"

#include <algorithm>
#include <fstream>

namespace gb {

static int clampInt (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void MidiTrack::addNoteOn (int tick, int channel, int note, int velocity)
{
    MidiEvent e;
    e.tick  = tick < 0 ? 0 : tick;
    e.order = orderNoteOn;
    e.bytes = { static_cast<unsigned char> (0x90 | (clampInt (channel, 1, 16) - 1)),
                static_cast<unsigned char> (clampInt (note, 0, 127)),
                static_cast<unsigned char> (clampInt (velocity, 1, 127)) };
    events.push_back (e);
}

void MidiTrack::addNoteOff (int tick, int channel, int note)
{
    MidiEvent e;
    e.tick  = tick < 0 ? 0 : tick;
    e.order = orderNoteOff;
    e.bytes = { static_cast<unsigned char> (0x80 | (clampInt (channel, 1, 16) - 1)),
                static_cast<unsigned char> (clampInt (note, 0, 127)),
                static_cast<unsigned char> (64) };
    events.push_back (e);
}

void MidiTrack::addCC (int tick, int channel, int cc, int value)
{
    MidiEvent e;
    e.tick  = tick < 0 ? 0 : tick;
    e.order = orderCC;
    e.bytes = { static_cast<unsigned char> (0xB0 | (clampInt (channel, 1, 16) - 1)),
                static_cast<unsigned char> (clampInt (cc, 0, 127)),
                static_cast<unsigned char> (clampInt (value, 0, 127)) };
    events.push_back (e);
}

static void pushMeta (std::vector<MidiEvent>& events, int tick, unsigned char metaType,
                      const std::vector<unsigned char>& payload)
{
    MidiEvent e;
    e.tick  = tick < 0 ? 0 : tick;
    e.order = orderMeta;
    e.bytes.push_back (0xFF);
    e.bytes.push_back (metaType);

    // Meta length is itself a variable-length quantity. Every payload we emit is
    // short, but encode it properly rather than assuming a single byte.
    unsigned len = static_cast<unsigned> (payload.size());
    unsigned char buf[5];
    int n = 0;
    buf[n++] = static_cast<unsigned char> (len & 0x7F);
    while ((len >>= 7) > 0)
        buf[n++] = static_cast<unsigned char> ((len & 0x7F) | 0x80);
    while (n > 0)
        e.bytes.push_back (buf[--n]);

    e.bytes.insert (e.bytes.end(), payload.begin(), payload.end());
    events.push_back (e);
}

void MidiTrack::addPitchBend (int tick, int channel, double semitones, double rangeSemitones)
{
    const double range = rangeSemitones > 0.01 ? rangeSemitones : 2.0;
    double fraction = semitones / range;
    if (fraction >  1.0) fraction =  1.0;
    if (fraction < -1.0) fraction = -1.0;

    // 8192 is centre; 0 and 16383 are the extremes. 8191 rather than 8192 on the
    // way up because the range above centre is one step shorter than the one
    // below it, and asking for full bend has to produce 16383 exactly.
    const int value = 8192 + static_cast<int> (fraction * (fraction >= 0.0 ? 8191.0 : 8192.0));
    const int clamped = clampInt (value, 0, 16383);

    MidiEvent e;
    e.tick  = tick < 0 ? 0 : tick;
    e.order = orderCC;
    e.bytes = { static_cast<unsigned char> (0xE0 | (clampInt (channel, 1, 16) - 1)),
                static_cast<unsigned char> (clamped & 0x7F),
                static_cast<unsigned char> ((clamped >> 7) & 0x7F) };
    events.push_back (e);
}

void MidiTrack::addMarker (int tick, const std::string& text)
{
    pushMeta (events, tick, 0x06, std::vector<unsigned char> (text.begin(), text.end()));
}

void MidiTrack::addText (int tick, const std::string& text)
{
    pushMeta (events, tick, 0x01, std::vector<unsigned char> (text.begin(), text.end()));
}

void MidiTrack::addTempo (int tick, double bpm)
{
    if (bpm < 1.0) bpm = 1.0;
    unsigned usPerQuarter = static_cast<unsigned> (60000000.0 / bpm + 0.5);
    if (usPerQuarter > 0xFFFFFFu) usPerQuarter = 0xFFFFFFu;

    pushMeta (events, tick, 0x51, {
        static_cast<unsigned char> ((usPerQuarter >> 16) & 0xFF),
        static_cast<unsigned char> ((usPerQuarter >> 8)  & 0xFF),
        static_cast<unsigned char> ( usPerQuarter        & 0xFF) });
}

void MidiTrack::addTimeSignature (int tick, int numerator, int denominator)
{
    int dd = 0;
    int d = denominator > 0 ? denominator : 4;
    while (d > 1) { d >>= 1; ++dd; }

    pushMeta (events, tick, 0x58, {
        static_cast<unsigned char> (clampInt (numerator, 1, 255)),
        static_cast<unsigned char> (dd),
        24, 8 });
}

//==============================================================================

static void writeVLQ (std::vector<unsigned char>& out, unsigned value)
{
    unsigned char buf[5];
    int n = 0;
    buf[n++] = static_cast<unsigned char> (value & 0x7F);
    while ((value >>= 7) > 0)
        buf[n++] = static_cast<unsigned char> ((value & 0x7F) | 0x80);
    while (n > 0)
        out.push_back (buf[--n]);
}

static void writeU32 (std::vector<unsigned char>& out, unsigned v)
{
    out.push_back (static_cast<unsigned char> ((v >> 24) & 0xFF));
    out.push_back (static_cast<unsigned char> ((v >> 16) & 0xFF));
    out.push_back (static_cast<unsigned char> ((v >> 8)  & 0xFF));
    out.push_back (static_cast<unsigned char> ( v        & 0xFF));
}

static void writeU16 (std::vector<unsigned char>& out, unsigned v)
{
    out.push_back (static_cast<unsigned char> ((v >> 8) & 0xFF));
    out.push_back (static_cast<unsigned char> ( v       & 0xFF));
}

bool MidiFile::write (const std::string& path, std::string& error) const
{
    std::vector<unsigned char> out;

    // Header chunk: format 1, one tempo/marker track plus one track per part.
    const char* mthd = "MThd";
    out.insert (out.end(), mthd, mthd + 4);
    writeU32 (out, 6);
    writeU16 (out, 1);
    writeU16 (out, static_cast<unsigned> (tracks.size()));
    writeU16 (out, static_cast<unsigned> (ticksPerQuarter));

    for (const MidiTrack& track : tracks)
    {
        std::vector<MidiEvent> sorted = track.events;

        // stable_sort, so events sharing a tick and a priority keep the order the
        // generator emitted them in. Reproducibility depends on this.
        std::stable_sort (sorted.begin(), sorted.end(),
                          [] (const MidiEvent& a, const MidiEvent& b)
                          {
                              if (a.tick != b.tick) return a.tick < b.tick;
                              return a.order < b.order;
                          });

        std::vector<unsigned char> body;

        if (! track.name.empty())
        {
            writeVLQ (body, 0);
            body.push_back (0xFF);
            body.push_back (0x03);
            writeVLQ (body, static_cast<unsigned> (track.name.size()));
            body.insert (body.end(), track.name.begin(), track.name.end());
        }

        int last = 0;
        for (const MidiEvent& e : sorted)
        {
            int delta = e.tick - last;
            if (delta < 0) delta = 0;
            writeVLQ (body, static_cast<unsigned> (delta));
            body.insert (body.end(), e.bytes.begin(), e.bytes.end());
            last = e.tick;
        }

        writeVLQ (body, 0);
        body.push_back (0xFF);
        body.push_back (0x2F);
        body.push_back (0x00);

        const char* mtrk = "MTrk";
        out.insert (out.end(), mtrk, mtrk + 4);
        writeU32 (out, static_cast<unsigned> (body.size()));
        out.insert (out.end(), body.begin(), body.end());
    }

    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    if (! f)
    {
        error = "could not open " + path + " for writing";
        return false;
    }

    f.write (reinterpret_cast<const char*> (out.data()), static_cast<std::streamsize> (out.size()));
    if (! f)
    {
        error = "failed while writing " + path;
        return false;
    }

    return true;
}

} // namespace gb
