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
RenderResult renderPerformance (const SongPlan& plan,
                                const DrumProfile& kit,
                                const BassProfile& bass);

bool writeMidi (const SongPlan& plan,
                const Performance& perf,
                const DrumProfile& kit,
                const BassProfile& bass,
                const std::string& path,
                std::string& error);

// Walks every mapped drum voice and bass articulation in turn, with a marker
// naming each one, so a profile can be checked against the real plugin by ear
// instead of being taken on trust.
bool writeCalibrationMidi (const SongPlan& plan,
                           const DrumProfile& kit,
                           const BassProfile& bass,
                           const std::string& path,
                           std::string& error);

} // namespace gb
