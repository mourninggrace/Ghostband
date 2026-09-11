#pragma once

#include "ghostband/Intent.h"
#include "ghostband/Profile.h"
#include "ghostband/SongPlan.h"

#include <string>
#include <vector>

namespace gb {

struct SectionReport
{
    std::string name;
    std::string role;
    int         bars      = 0;
    int         startBar  = 0;
    int         startTick = 0;   // lets a UI say which section is sounding right now
    int         endTick   = 0;
    double      intensity = 0.0;
    std::string feel;
    std::string chords;      // rendered as text, for the console summary
    int         drumHits    = 0;
    int         bassNotes   = 0;
    int         guitarChords  = 0;
    int         guitar2Chords = 0;
    int         pianoChords   = 0;

    // Chords are not the whole of what a guitar plays. A part set to "fills" or
    // "solo" writes a LEAD LINE, whose notes land in a different list entirely -
    // so counting only chords reports the second guitar as silent through
    // exactly the sections it is most audible in. Both numbers, and a UI that
    // wants "how much does this part play here" should add them.
    int         guitarNotes   = 0;
    int         guitar2Notes  = 0;
    std::string guitarFeel;      // empty when the part is not in this song
    std::string guitar2Feel;
    std::string pianoFeel;
};

struct RenderResult
{
    Performance performance;
    std::vector<SectionReport> sections;
    int    totalBars       = 0;
    double durationSeconds = 0.0;
};

// Plan -> plugin-agnostic intent. The profiles are consulted only for their
// capabilities: which pieces the kit actually has, and how low the bass can
// physically go in the requested tuning. No note numbers cross this boundary.
// Guitar and piano are optional: pass null and those parts are simply not
// generated, which is what keeps every plan written before they existed
// rendering byte-for-byte as it did. The profiles are consulted only for
// capabilities - how low the bass reaches, which drum pieces the kit has, and
// whether a phrase instrument performs its own rhythm or needs one supplied.
RenderResult renderPerformance (const SongPlan& plan,
                                const DrumProfile& kit,
                                const BassProfile& bass,
                                const PhraseProfile* guitar  = nullptr,
                                const PhraseProfile* piano   = nullptr,

                                // Appended rather than slotted in beside the
                                // first guitar, so every existing call still
                                // compiles and still means what it did.
                                const PhraseProfile* guitar2 = nullptr);

bool writeMidi (const SongPlan& plan,
                const Performance& perf,
                const DrumProfile& kit,
                const BassProfile& bass,
                const std::string& path,
                std::string& error,
                const PhraseProfile* guitar  = nullptr,
                const PhraseProfile* piano   = nullptr,
                const PhraseProfile* guitar2 = nullptr);

// Walks every mapped drum voice and bass articulation in turn, with a marker
// naming each one, so a profile can be checked against the real plugin by ear
// instead of being taken on trust.
bool writeCalibrationMidi (const SongPlan& plan,
                           const DrumProfile& kit,
                           const BassProfile& bass,
                           const std::string& path,
                           std::string& error);

} // namespace gb
