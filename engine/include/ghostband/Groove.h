#pragma once

#include "ghostband/Intent.h"
#include "ghostband/Music.h"

#include <string>
#include <vector>

namespace gb {

class Rng;
class DrumProfile;

// Ticks per quarter note for the whole engine. 480 divides cleanly by 16ths
// (120), 8th triplets (160) and 32nds (60), so nothing lands on a fraction.
constexpr int kPPQ = 480;

enum class Feel { Straight, HalfTime, DoubleTime, Blast };

Feel feelFromString (const std::string& s);

struct GrooveContext
{
    Feel        feel        = Feel::Straight;
    double      intensity   = 0.5;
    double      complexity  = 0.5;
    double      humanize    = 0.5;
    std::string style       = "hard_rock";
    std::string role        = "verse";

    int beatTicks   = kPPQ;
    int beatsPerBar = 4;
    int barTicks    = kPPQ * 4;

    int lowestBassNote = 28;    // supplied by the bass profile, not assumed here
    int highestBassNote = 67;
};

// Chosen once per section so the section keeps a single rhythmic identity, then
// varied bar to bar. Picking per bar instead produces incoherent mush.
struct SectionGroove
{
    std::vector<int> kickCell;      // 16th offsets within one beat
    std::vector<int> kickCellAlt;   // substituted on the last beat of some bars
    std::vector<int> kickBeats;     // 0-indexed beats the kick cell applies to
    std::vector<int> snareBeats;    // 0-indexed beats carrying the backbeat
    int    hatStep       = 2;       // in 16ths: 2 = eighths, 1 = sixteenths
    bool   useRide       = false;
    bool   openHatOnAnd  = false;
    double ghostDensity  = 0.0;
    bool   doubleKick    = false;
};

// The rhythmic skeleton of one bar. Kick and bass both read `kickOnsets`, and
// that shared read is the entire reason the two parts lock together.
struct BarGrid
{
    int barTicks = kPPQ * 4;
    std::vector<int> kickOnsets;
    std::vector<int> snareOnsets;
};

SectionGroove buildSectionGroove (const GrooveContext& ctx, Rng& rng);

BarGrid buildBarGrid (const GrooveContext& ctx,
                      const SectionGroove& groove,
                      int barIndexInSection,
                      Rng& rng);

void generateDrumBar (const GrooveContext& ctx,
                      const SectionGroove& groove,
                      const BarGrid& grid,
                      int barStartTick,
                      bool isFirstBarOfSection,
                      const std::string& fillSize,       // none/small/big
                      const DrumProfile& kit,
                      Rng& rng,
                      std::vector<DrumIntent>& out);

// Which phrase a phrase-driven part should be playing in this section.
//
// These are drawn from weighted pools rather than switched on thresholds. That
// is deliberate: the drum skeleton was built on thresholds and ended up with no
// audible variation between rerolls at all, which is the one thing these parts
// must not repeat.
PhraseFeel chooseGuitarFeel (const GrooveContext& ctx, Rng& rng);
PhraseFeel choosePianoFeel  (const GrooveContext& ctx, Rng& rng);

void generateBassBar (const GrooveContext& ctx,
                      const BarGrid& grid,
                      int barStartTick,
                      const Chord& chord,
                      const Chord& nextChord,
                      const std::string& pattern,        // auto/lock_kick/...
                      bool isLastBarOfSection,
                      Rng& rng,
                      std::vector<BassIntent>& out);

} // namespace gb
