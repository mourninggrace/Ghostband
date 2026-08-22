#pragma once

#include <string>
#include <vector>

namespace gb {

// One section of the song. The user owns this level entirely: they choose the
// order, the lengths and the feel. Anything left as "auto" is what the planner
// (or, for now, the local defaults) is allowed to decide.
struct SectionPlan
{
    std::string name;                       // "verse1", "chorus2"
    std::string role;                       // intro/verse/chorus/bridge/solo/ending/breakdown
    int         bars      = 8;
    double      intensity = 0.5;            // 0..1, drives density and velocity
    std::string feel      = "straight";     // straight/half_time/double_time/blast

    std::vector<std::string> chords;        // one per bar; empty means auto

    std::string bassPattern = "auto";       // auto/lock_kick/lock_kick_octave/eighths/sixteenths/roots
    std::string fill        = "auto";       // auto/none/small/big
    std::string plays       = "full";       // full/drums/bass/none
    bool        vary        = true;         // false makes a repeated section identical
};

struct SongPlan
{
    std::string title  = "Untitled";
    std::string key    = "E";
    std::string mode   = "natural_minor";
    double      bpm    = 120.0;

    int timeSigNumerator   = 4;
    int timeSigDenominator = 4;

    std::string style      = "hard_rock";
    std::string bassTuning = "standard";    // standard/drop_d/drop_c/b_standard
    std::string playStyle  = "pick";        // pick/finger/slap

    double   complexity = 0.5;              // fills, ghost notes, busyness
    double   humanize   = 0.5;              // timing and velocity looseness
    unsigned seed       = 1;
    std::string ending  = "hard_stop";      // hard_stop/ritard/cymbal_ring/fade

    std::string drumProfile = "profiles/ssd5.json";
    std::string bassProfile = "profiles/modo-bass-2.json";

    std::vector<SectionPlan> sections;

    static bool load (const std::string& path, SongPlan& out, std::string& error);

    // Non-fatal problems worth telling the user about before they hit play.
    std::vector<std::string> validate() const;
};

// "verse2" -> "verse". Lets the plan name sections naturally without also
// having to state the role every time.
std::string inferRole (const std::string& sectionName);

} // namespace gb
