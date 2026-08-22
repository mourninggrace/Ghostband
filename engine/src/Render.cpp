#include "ghostband/Render.h"
#include "ghostband/Groove.h"
#include "ghostband/MidiFile.h"
#include "ghostband/Music.h"
#include "ghostband/Rng.h"

#include <algorithm>

namespace gb {

static uint32_t hashString (const std::string& s)
{
    uint32_t h = 2166136261u;
    for (char c : s)
    {
        h ^= static_cast<unsigned char> (c);
        h *= 16777619u;
    }
    return h;
}

static Chord transposed (Chord c, int semitones)
{
    if (semitones == 0) return c;
    c.rootPc = (((c.rootPc + semitones) % 12) + 12) % 12;
    c.text   = pitchClassName (c.rootPc)
             + (c.quality == ChordQuality::Power ? "5"
                : c.quality == ChordQuality::Minor ? "m"
                : c.quality == ChordQuality::Diminished ? "dim" : "");
    return c;
}

static std::vector<Chord> chordsForSection (const SectionPlan& s,
                                            int keyPc,
                                            Mode mode,
                                            const std::string& style,
                                            int transpose,
                                            Rng& rng)
{
    if (s.chords.empty())
    {
        // Auto progressions are generated in the transposed key directly, so the
        // voice leading is worked out where the music actually sits.
        const int shiftedKey = (((keyPc + transpose) % 12) + 12) % 12;
        return autoProgression (shiftedKey, mode, style, s.role, s.bars, rng);
    }

    // A shorter list than the bar count repeats, which is how people actually
    // write charts: "Em C D" over eight bars means keep going round.
    std::vector<Chord> parsed;
    for (const std::string& text : s.chords)
    {
        Chord c = parseChord (text);
        if (! c.valid)
        {
            c.rootPc  = keyPc;
            c.quality = styleUsesPowerChords (style) ? ChordQuality::Power : ChordQuality::Minor;
            c.valid   = true;
            c.text    = pitchClassName (keyPc);
        }
        parsed.push_back (c);
    }

    std::vector<Chord> out;
    out.reserve (static_cast<size_t> (s.bars));
    for (int bar = 0; bar < s.bars; ++bar)
        out.push_back (transposed (parsed[static_cast<size_t> (bar) % parsed.size()], transpose));

    return out;
}

RenderResult renderPerformance (const SongPlan& plan,
                                const DrumProfile& kit,
                                const BassProfile& bass)
{
    RenderResult result;

    // Ask the instrument how low it can actually go in this tuning. Assuming
    // standard here would write every drop-C riff an octave high and quietly
    // throw away the only reason to drop-tune in the first place.
    const int lowestBass  = bass.lowestNoteFor (plan.bassTuning);
    const int highestBass = bass.highestNote;

    bool keyOk = false;
    int keyPc = pitchClassFromName (plan.key, keyOk);
    if (! keyOk) keyPc = 4;                       // E, the default

    const Mode mode = modeFromString (plan.mode);

    const int beatTicks = std::max (1, kPPQ * 4 / std::max (1, plan.timeSigDenominator));
    const int barTicks  = beatTicks * std::max (1, plan.timeSigNumerator);

    int tick = 0;
    int barCounter = 0;
    int lastSectionStart = 0;

    for (size_t si = 0; si < plan.sections.size(); ++si)
    {
        const SectionPlan& s = plan.sections[si];
        const bool isLastSection = (si + 1 == plan.sections.size());

        const std::string nextRole = isLastSection ? std::string ("ending")
                                                   : plan.sections[si + 1].role;

        // A section that is not allowed to vary derives its seed from its role
        // alone, so verse 1 and verse 2 come out bit-identical. Varying sections
        // derive from their position, so they differ but stay reproducible.
        const uint32_t salt = s.vary ? static_cast<uint32_t> (si + 1) * 7919u
                                     : hashString (s.role) | 1u;
        Rng rng (deriveSeed (plan.seed, salt));

        GrooveContext ctx;
        ctx.feel        = feelFromString (s.feel);
        ctx.intensity   = s.intensity;
        ctx.complexity  = plan.complexity;
        ctx.humanize    = plan.humanize;
        ctx.style       = plan.style;
        ctx.role        = s.role;
        ctx.beatTicks   = beatTicks;
        ctx.beatsPerBar = plan.timeSigNumerator;
        ctx.barTicks    = barTicks;
        ctx.lowestBassNote  = lowestBass;
        ctx.highestBassNote = highestBass;

        const SectionGroove groove = buildSectionGroove (ctx, rng);
        const std::vector<Chord> chords = chordsForSection (s, keyPc, mode, plan.style,
                                                            plan.transpose, rng);

        SectionReport report;
        report.name      = s.name;
        report.role      = s.role;
        report.bars      = s.bars;
        report.startBar  = barCounter;
        report.startTick = tick;
        report.endTick   = tick + s.bars * barTicks;
        report.intensity = s.intensity;
        report.feel      = s.feel;

        for (size_t i = 0; i < chords.size() && i < 8; ++i)
            report.chords += (i ? " " : "") + chords[i].text;
        if (chords.size() > 8) report.chords += " ...";

        Marker marker;
        marker.tick = tick;
        marker.text = s.name + " [" + report.chords + "]";
        result.performance.markers.push_back (marker);

        lastSectionStart = tick;

        const size_t drumsBefore = result.performance.drums.size();
        const size_t bassBefore  = result.performance.bass.size();

        for (int bar = 0; bar < s.bars; ++bar)
        {
            const int barStart = tick + bar * barTicks;
            const bool lastBar = (bar + 1 == s.bars);

            const BarGrid grid = buildBarGrid (ctx, groove, bar, rng);

            std::string fillSize = "none";
            if (s.fill == "small" || s.fill == "big")
            {
                if (lastBar) fillSize = s.fill;
            }
            else if (s.fill == "auto")
            {
                if (lastBar && ! isLastSection)
                    fillSize = (nextRole == "chorus" || s.bars >= 8) ? "big" : "small";
                else if (! lastBar && s.bars >= 8 && bar == s.bars / 2 - 1
                         && rng.chance (plan.complexity * 0.35))
                    fillSize = "small";
            }

            const bool playDrums = (s.plays == "full" || s.plays == "drums");
            const bool playBass  = (s.plays == "full" || s.plays == "bass");

            if (playDrums)
                generateDrumBar (ctx, groove, grid, barStart, bar == 0, fillSize,
                                 kit, rng, result.performance.drums);

            if (playBass)
            {
                const Chord& chord = chords[static_cast<size_t> (bar) % chords.size()];

                Chord next = chord;
                if (! lastBar)
                    next = chords[static_cast<size_t> (bar + 1) % chords.size()];
                else if (! isLastSection)
                    next = Chord();     // unknown until the next section is built

                generateBassBar (ctx, grid, barStart, chord, next, s.bassPattern,
                                 lastBar, rng, result.performance.bass);
            }
        }

        report.drumHits  = static_cast<int> (result.performance.drums.size() - drumsBefore);
        report.bassNotes = static_cast<int> (result.performance.bass.size() - bassBefore);
        result.sections.push_back (report);

        tick += s.bars * barTicks;
        barCounter += s.bars;
    }

    // ---- ending ----------------------------------------------------------
    const int endTick = tick;

    if (plan.ending == "hard_stop" || plan.ending == "cymbal_ring")
    {
        DrumIntent crash;
        crash.tick   = endTick;
        crash.voice  = kit.hasVoice (DrumVoice::Crash) ? DrumVoice::Crash : DrumVoice::Ride;
        crash.accent = 1.0;
        result.performance.drums.push_back (crash);

        DrumIntent kick;
        kick.tick   = endTick;
        kick.voice  = DrumVoice::Kick;
        kick.accent = 1.0;
        result.performance.drums.push_back (kick);

        if (! result.performance.bass.empty())
        {
            BassIntent b;
            b.tick          = endTick;
            b.pitch         = result.performance.bass.front().pitch;
            b.accent        = 1.0;
            b.artic         = BassArtic::Normal;
            b.durationTicks = (plan.ending == "cymbal_ring") ? barTicks * 2 : barTicks / 2;
            result.performance.bass.push_back (b);
        }

        tick = endTick + ((plan.ending == "cymbal_ring") ? barTicks * 2 : barTicks);
    }
    else if (plan.ending == "ritard")
    {
        // Slow the last bar to roughly seventy percent over eight steps.
        const int from = std::max (0, endTick - barTicks);
        for (int i = 0; i <= 8; ++i)
        {
            Performance::TempoPoint tp;
            tp.tick = from + (barTicks * i) / 8;
            tp.bpm  = plan.bpm * (1.0 - 0.30 * (static_cast<double> (i) / 8.0));
            result.performance.tempoMap.push_back (tp);
        }
        tick = endTick + barTicks;
    }
    else if (plan.ending == "fade")
    {
        for (DrumIntent& d : result.performance.drums)
        {
            if (d.tick < lastSectionStart) continue;
            const double p = static_cast<double> (d.tick - lastSectionStart)
                           / std::max (1, endTick - lastSectionStart);
            d.accent *= (1.0 - 0.75 * p);
        }
        for (BassIntent& b : result.performance.bass)
        {
            if (b.tick < lastSectionStart) continue;
            const double p = static_cast<double> (b.tick - lastSectionStart)
                           / std::max (1, endTick - lastSectionStart);
            b.accent *= (1.0 - 0.75 * p);
        }
        tick = endTick + barTicks;
    }

    result.performance.totalTicks = tick;
    result.totalBars = barCounter;
    result.durationSeconds = (static_cast<double> (tick) / kPPQ) * (60.0 / plan.bpm);

    return result;
}

//==============================================================================

bool writeMidi (const SongPlan& plan,
                const Performance& perf,
                const DrumProfile& kit,
                const BassProfile& bass,
                const std::string& path,
                std::string& error)
{
    MidiFile mf (kPPQ);

    MidiTrack conductor;
    conductor.name = plan.title.empty() ? "Ghostband" : plan.title;
    conductor.addTimeSignature (0, plan.timeSigNumerator, plan.timeSigDenominator);
    conductor.addTempo (0, plan.bpm);

    for (const Performance::TempoPoint& tp : perf.tempoMap)
        conductor.addTempo (tp.tick, tp.bpm);

    for (const Marker& m : perf.markers)
        conductor.addMarker (m.tick, m.text);

    mf.tracks.push_back (conductor);

    MidiTrack drums;
    drums.name = kit.name;
    kit.render (perf.drums, drums);
    mf.tracks.push_back (drums);

    MidiTrack bassTrack;
    bassTrack.name = bass.name;
    bass.render (perf.bass, bassTrack, plan.bassTuning);
    mf.tracks.push_back (bassTrack);

    return mf.write (path, error);
}

//==============================================================================

bool writeCalibrationMidi (const SongPlan& plan,
                           const DrumProfile& kit,
                           const BassProfile& bass,
                           const std::string& path,
                           std::string& error)
{
    MidiFile mf (kPPQ);

    MidiTrack conductor;
    conductor.name = "Ghostband calibration";
    conductor.addTimeSignature (0, 4, 4);
    conductor.addTempo (0, 90.0);

    std::vector<DrumIntent> drumHits;
    int tick = 0;

    for (int v = 0; v < static_cast<int> (DrumVoice::Count); ++v)
    {
        const DrumVoice voice = static_cast<DrumVoice> (v);
        if (! kit.hasVoice (voice)) continue;

        conductor.addMarker (tick, std::string (drumVoiceName (voice))
                                   + " = note " + std::to_string (kit.noteFor (voice)));

        // Three hits at rising velocity: enough to hear both the sample and
        // whether the velocity layers are landing where they should.
        for (int i = 0; i < 3; ++i)
        {
            DrumIntent d;
            d.tick   = tick + i * (kPPQ / 2);
            d.voice  = voice;
            d.accent = 0.3 + i * 0.32;
            drumHits.push_back (d);
        }
        tick += kPPQ * 2;
    }

    const int drumEnd = tick;
    std::vector<BassIntent> bassNotes;

    const int lowest = bass.lowestNoteFor (plan.bassTuning);

    conductor.addMarker (drumEnd, "bass lowest note (" + plan.bassTuning + ") = "
                                  + std::to_string (lowest));
    tick = drumEnd + kPPQ;

    for (int a = 0; a <= static_cast<int> (BassArtic::Pop); ++a)
    {
        const BassArtic artic = static_cast<BassArtic> (a);
        const ArticulationMapping m = bass.articulation (artic);
        if (! m.defined && artic != BassArtic::Normal) continue;

        std::string label = std::string ("bass ") + bassArticName (artic);
        if (m.hasKeyswitch) label += " (keyswitch " + std::to_string (m.keyswitch) + ")";
        if (m.hasCC)        label += " (CC " + std::to_string (m.cc)
                                     + " = " + std::to_string (m.ccValue) + ")";
        conductor.addMarker (tick, label);

        for (int i = 0; i < 4; ++i)
        {
            BassIntent b;
            b.tick          = tick + i * (kPPQ / 2);
            b.pitch         = lowest + (i % 2 == 0 ? 0 : 12);
            b.accent        = 0.75;
            b.artic         = artic;
            b.durationTicks = kPPQ / 3;
            bassNotes.push_back (b);
        }
        tick += kPPQ * 3;
    }

    mf.tracks.push_back (conductor);

    MidiTrack drumTrack;
    drumTrack.name = kit.name + " (calibration)";
    kit.render (drumHits, drumTrack);
    mf.tracks.push_back (drumTrack);

    MidiTrack bassTrack;
    bassTrack.name = bass.name + " (calibration)";
    bass.render (bassNotes, bassTrack, plan.bassTuning);
    mf.tracks.push_back (bassTrack);

    return mf.write (path, error);
}

} // namespace gb
