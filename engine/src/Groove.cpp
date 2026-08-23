#include "ghostband/Groove.h"
#include "ghostband/Profile.h"
#include "ghostband/Rng.h"

#include <algorithm>
#include <cctype>

namespace gb {

Feel feelFromString (const std::string& s)
{
    if (s == "half_time")   return Feel::HalfTime;
    if (s == "double_time") return Feel::DoubleTime;
    if (s == "blast")       return Feel::Blast;
    return Feel::Straight;
}

static bool contains (const std::vector<int>& v, int x)
{
    return std::find (v.begin(), v.end(), x) != v.end();
}

static bool isMetalStyle (const std::string& style)
{
    return style == "metal" || style == "thrash" || style == "groove_metal"
        || style == "doom"  || style == "sludge" || style == "prog_metal";
}

static double clamp01 (double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

static bool isHeavyStyle (const std::string& style)
{
    return isMetalStyle (style) || style == "hard_rock" || style == "punk";
}

//==============================================================================

// Which beats of the bar the kick cell applies to. Expressed as a shape rather
// than a literal list so odd time signatures still work.
enum class BeatMask { All, Even, FirstAndMid, AllButLast, FirstOnly, Offbeats };

static std::vector<int> expandMask (BeatMask m, int beats)
{
    std::vector<int> out;
    for (int b = 0; b < beats; ++b)
    {
        bool take = false;
        switch (m)
        {
            case BeatMask::All:         take = true; break;
            case BeatMask::Even:        take = (b % 2) == 0; break;
            case BeatMask::Offbeats:    take = (b % 2) == 1; break;
            case BeatMask::AllButLast:  take = b < beats - 1; break;
            case BeatMask::FirstOnly:   take = b == 0; break;
            case BeatMask::FirstAndMid: take = (b == 0 || b == beats / 2); break;
        }
        if (take) out.push_back (b);
    }
    if (out.empty()) out.push_back (0);
    return out;
}

struct KickOption
{
    BeatMask mask;
    std::vector<int> cell;   // 16th offsets within a beat
};

SectionGroove buildSectionGroove (const GrooveContext& ctx, Rng& rng)
{
    SectionGroove g;

    const int beats = std::max (1, ctx.beatsPerBar);
    const bool metal = isMetalStyle (ctx.style);
    const double in = ctx.intensity;

    // ---- kick ------------------------------------------------------------
    // Every band offers several genuinely different patterns rather than one or
    // two, because this is the single biggest thing a reroll can change: the
    // bass reads these onsets too, so a new kick pattern moves the whole band.
    std::vector<KickOption> pool;

    switch (ctx.feel)
    {
        case Feel::Blast:
            pool = { { BeatMask::All, { 0, 2 } },
                     { BeatMask::All, { 0, 1, 2, 3 } },
                     { BeatMask::All, { 0 } } };
            g.doubleKick = true;
            break;

        case Feel::HalfTime:
            pool = { { BeatMask::FirstAndMid, { 0 } },
                     { BeatMask::FirstOnly,   { 0, 3 } },
                     { BeatMask::FirstAndMid, { 0, 2 } },
                     { BeatMask::FirstOnly,   { 0 } },
                     { BeatMask::Even,        { 0 } } };
            break;

        case Feel::DoubleTime:
            pool = { { BeatMask::All, { 0, 2 } },
                     { BeatMask::All, { 0 } },
                     { BeatMask::All, { 0, 1, 2, 3 } },
                     { BeatMask::Even, { 0, 2, 3 } } };
            break;

        case Feel::Straight:
        default:
            if (metal && in > 0.78)
            {
                pool = { { BeatMask::All,  { 0, 1, 2, 3 } },
                         { BeatMask::All,  { 0, 2, 3 } },
                         { BeatMask::All,  { 0, 2 } },
                         { BeatMask::Even, { 0, 1, 2, 3 } } };
                g.doubleKick = true;
            }
            else if (metal && in > 0.48)
            {
                pool = { { BeatMask::All,  { 0, 2, 3 } },   // gallop
                         { BeatMask::All,  { 0, 1, 2 } },   // reverse gallop
                         { BeatMask::All,  { 0, 2 } },
                         { BeatMask::Even, { 0, 1, 2, 3 } },
                         { BeatMask::All,  { 0, 3 } } };
            }
            else if (metal && in > 0.30)
            {
                pool = { { BeatMask::All,  { 0, 2 } },
                         { BeatMask::All,  { 0 } },
                         { BeatMask::Even, { 0, 2, 3 } },
                         { BeatMask::AllButLast, { 0, 2 } } };
            }
            else if (in > 0.62)
            {
                pool = { { BeatMask::All,        { 0 } },
                         { BeatMask::All,        { 0, 2 } },
                         { BeatMask::Even,       { 0, 2, 3 } },
                         { BeatMask::AllButLast, { 0, 2 } },
                         { BeatMask::All,        { 0, 3 } } };
            }
            else if (in > 0.34)
            {
                pool = { { BeatMask::Even,        { 0, 3 } },
                         { BeatMask::AllButLast,  { 0 } },
                         { BeatMask::All,         { 0 } },
                         { BeatMask::Even,        { 0, 2 } },
                         { BeatMask::FirstAndMid, { 0, 2, 3 } } };
            }
            else
            {
                pool = { { BeatMask::FirstAndMid, { 0 } },
                         { BeatMask::Even,        { 0 } },
                         { BeatMask::FirstAndMid, { 0, 3 } },
                         { BeatMask::FirstOnly,   { 0, 2 } },
                         { BeatMask::AllButLast,  { 0 } } };
            }
            break;
    }

    const KickOption& chosen = pool[static_cast<size_t> (rng.below (static_cast<int> (pool.size())))];
    g.kickBeats = expandMask (chosen.mask, beats);
    g.kickCell  = chosen.cell;

    // A single alternate cell, used on the last beat of alternating bars. This
    // is what stops an eight-bar section sounding like one bar looped.
    g.kickCellAlt = g.kickCell;
    if (g.kickCell.size() < 3 && rng.chance (0.7))
        g.kickCellAlt.push_back (3);

    // ---- snare -----------------------------------------------------------
    if (ctx.feel == Feel::HalfTime)
        g.snareBeats = { beats >= 4 ? 2 : beats / 2 };
    else if (beats == 4)
        g.snareBeats = { 1, 3 };
    else if (beats >= 6)
        g.snareBeats = { beats / 2, beats - 1 };
    else
        g.snareBeats = { beats / 2 };

    // The backbeat itself stays where it belongs - moving it stops the groove
    // being the groove - but what happens around it varies.
    {
        const int roll = rng.below (100);
        if      (roll < 45) g.snareVariant = 0;   // plain
        else if (roll < 68) g.snareVariant = 1;   // pickup on the last 16th
        else if (roll < 85) g.snareVariant = 2;   // push on the last upbeat
        else                g.snareVariant = 3;   // ghost-heavy
    }

    // ---- cymbals ---------------------------------------------------------
    // Probabilities rather than thresholds, so two rolls at the same intensity
    // can legitimately differ.
    double rideChance = clamp01 (in - 0.30);
    if (ctx.role == "chorus") rideChance += 0.25;
    if (metal)                rideChance += 0.10;
    g.useRide = (ctx.feel == Feel::Blast) || rng.chance (clamp01 (rideChance));

    {
        // Weighted pick across quarters, eighths and sixteenths.
        int wQuarter = in < 0.30 ? 30 : 4;
        int wEighth  = 60;
        int wSixteen = static_cast<int> (10 + ctx.complexity * 45 * (in < 0.75 ? 1.0 : 0.4));
        if (ctx.feel == Feel::Blast || ctx.feel == Feel::DoubleTime) { wQuarter = 0; wSixteen /= 2; }

        const int total = std::max (1, wQuarter + wEighth + wSixteen);
        const int roll = rng.below (total);
        g.hatStep = (roll < wQuarter) ? 4 : ((roll < wQuarter + wEighth) ? 2 : 1);
    }

    g.openHatOnAnd = (! g.useRide) && in > 0.30 && rng.chance (0.45);

    // Ghost notes separate a groove from a drum machine, but they turn to mud at
    // high intensity where the backbeat should dominate.
    g.ghostDensity = ctx.complexity * 0.45 * (in > 0.8 ? 0.35 : 1.0);
    if (metal) g.ghostDensity *= 0.5;
    if (g.snareVariant == 3) g.ghostDensity *= 1.8;

    // ---- fills -----------------------------------------------------------
    g.fillShape = rng.below (5);

    return g;
}

std::string chooseBassPattern (const GrooveContext& ctx, Rng& rng)
{
    const bool metal = isMetalStyle (ctx.style);
    const double in = ctx.intensity;

    // Locking to the kick is the default in heavy styles and stays the most
    // likely draw, but it is no longer the only one - a bass that only ever
    // mirrors the kick makes every reroll sound the same even when the drums
    // changed underneath it.
    int lock = metal ? 60 : 35;
    int octave = static_cast<int> (10 + in * 25);
    int eighths = metal ? 15 : 40;
    int roots = in < 0.4 ? 20 : 5;

    const int total = lock + octave + eighths + roots;
    int roll = rng.below (total);

    if ((roll -= lock)    < 0) return "lock_kick";
    if ((roll -= octave)  < 0) return "lock_kick_octave";
    if ((roll -= eighths) < 0) return "eighths";
    return "roots";
}

//==============================================================================

// Draws one entry from a weighted pool. Weights are relative, and a zero weight
// removes an option entirely.
static PhraseFeel drawFeel (const std::vector<std::pair<PhraseFeel, int>>& pool, Rng& rng)
{
    int total = 0;
    for (const auto& e : pool) total += std::max (0, e.second);
    if (total <= 0) return PhraseFeel::Driving;

    int roll = rng.below (total);
    for (const auto& e : pool)
    {
        roll -= std::max (0, e.second);
        if (roll < 0) return e.first;
    }
    return pool.back().first;
}

PhraseFeel chooseGuitarFeel (const GrooveContext& ctx, Rng& rng)
{
    const double in = ctx.intensity;
    const bool heavy = isHeavyStyle (ctx.style);
    const bool halfTime = (ctx.feel == Feel::HalfTime);

    std::vector<std::pair<PhraseFeel, int>> pool;

    if (ctx.role == "intro")
    {
        pool = { { PhraseFeel::Sparse, 5 }, { PhraseFeel::Muted, 3 },
                 { PhraseFeel::Silent, 3 }, { PhraseFeel::Open, 1 } };
    }
    else if (in < 0.35)
    {
        pool = { { PhraseFeel::Sparse, 5 }, { PhraseFeel::Muted, 4 },
                 { PhraseFeel::Silent, 2 } };
    }
    else if (in < 0.60)
    {
        // The workhorse verse range. Muted chugging is the default in heavy
        // styles; cleaner styles lean on a steadier strum.
        pool = { { PhraseFeel::Muted, heavy ? 6 : 3 }, { PhraseFeel::Driving, 4 },
                 { PhraseFeel::Sparse, 2 } };
    }
    else if (in < 0.80)
    {
        pool = { { PhraseFeel::Driving, 5 }, { PhraseFeel::Open, 4 },
                 { PhraseFeel::Muted, 3 } };
    }
    else
    {
        pool = { { PhraseFeel::Open, 6 }, { PhraseFeel::Busy, 3 },
                 { PhraseFeel::Driving, 3 } };
    }

    // Half-time wants length, not chatter.
    if (halfTime)
        pool.push_back ({ PhraseFeel::Open, 4 });

    return drawFeel (pool, rng);
}

PhraseFeel choosePianoFeel (const GrooveContext& ctx, Rng& rng)
{
    const double in = ctx.intensity;
    const bool heavy = isHeavyStyle (ctx.style);

    std::vector<std::pair<PhraseFeel, int>> pool;

    // A piano competing with a distorted guitar is the fastest way to make a mix
    // sound cluttered, so in heavy styles it stays out of the quiet sections and
    // arrives with the chorus. That entrance is the point of it.
    if (ctx.role == "intro")
    {
        pool = { { PhraseFeel::Silent, heavy ? 6 : 3 }, { PhraseFeel::Sparse, 4 } };
    }
    else if (in < 0.35)
    {
        pool = { { PhraseFeel::Sparse, 5 }, { PhraseFeel::Silent, heavy ? 5 : 2 } };
    }
    else if (in < 0.60)
    {
        pool = { { PhraseFeel::Sparse, 4 }, { PhraseFeel::Driving, 3 },
                 { PhraseFeel::Silent, heavy ? 4 : 1 } };
    }
    else if (in < 0.80)
    {
        pool = { { PhraseFeel::Driving, 4 }, { PhraseFeel::Open, 5 },
                 { PhraseFeel::Sparse, 2 } };
    }
    else
    {
        pool = { { PhraseFeel::Open, 6 }, { PhraseFeel::Driving, 3 },
                 { PhraseFeel::Busy, heavy ? 1 : 3 } };
    }

    return drawFeel (pool, rng);
}

BarGrid buildBarGrid (const GrooveContext& ctx,
                      const SectionGroove& groove,
                      int barIndexInSection,
                      Rng& rng)
{
    BarGrid grid;
    grid.barTicks = ctx.barTicks;

    const int beats     = std::max (1, ctx.beatsPerBar);
    const int sixteenth = std::max (1, ctx.beatTicks / 4);

    for (int b = 0; b < beats; ++b)
    {
        if (! contains (groove.kickBeats, b))
            continue;

        const bool lastBeat = (b == beats - 1);
        const bool useAlt   = lastBeat && (barIndexInSection % 2 == 1);
        const std::vector<int>& cell = useAlt ? groove.kickCellAlt : groove.kickCell;

        for (int off : cell)
        {
            const int tick = b * ctx.beatTicks + off * sixteenth;
            if (tick < ctx.barTicks)
                grid.kickOnsets.push_back (tick);
        }
    }

    // An occasional extra pickup kick, weighted by complexity. Deliberately rare:
    // the kick pattern is the spine both the drums and the bass hang off, so it
    // has to stay recognisable bar to bar.
    if (rng.chance (ctx.complexity * 0.18) && beats >= 2)
    {
        const int tick = (beats - 1) * ctx.beatTicks + 3 * sixteenth;
        if (! contains (grid.kickOnsets, tick))
            grid.kickOnsets.push_back (tick);
    }

    for (int b : groove.snareBeats)
    {
        if (b >= 0 && b < beats)
            grid.snareOnsets.push_back (b * ctx.beatTicks);
    }

    if (ctx.feel == Feel::DoubleTime || ctx.feel == Feel::Blast)
    {
        for (int b = 0; b < beats; ++b)
        {
            const int tick = b * ctx.beatTicks + ctx.beatTicks / 2;
            if (! contains (grid.snareOnsets, tick))
                grid.snareOnsets.push_back (tick);
        }
    }

    // The section's backbeat treatment, applied on alternating bars so it reads
    // as a recurring detail rather than a tic on every bar.
    if (barIndexInSection % 2 == 1)
    {
        int extra = -1;
        if (groove.snareVariant == 1)
            extra = ctx.barTicks - sixteenth;                          // pickup into the next bar
        else if (groove.snareVariant == 2 && beats >= 2)
            extra = (beats - 1) * ctx.beatTicks + ctx.beatTicks / 2;   // push on the last upbeat

        if (extra > 0 && extra < ctx.barTicks && ! contains (grid.snareOnsets, extra))
            grid.snareOnsets.push_back (extra);
    }

    std::sort (grid.kickOnsets.begin(), grid.kickOnsets.end());
    grid.kickOnsets.erase (std::unique (grid.kickOnsets.begin(), grid.kickOnsets.end()),
                           grid.kickOnsets.end());
    std::sort (grid.snareOnsets.begin(), grid.snareOnsets.end());
    grid.snareOnsets.erase (std::unique (grid.snareOnsets.begin(), grid.snareOnsets.end()),
                            grid.snareOnsets.end());

    return grid;
}

//==============================================================================

static DrumVoice pickTom (const DrumProfile& kit, int index)
{
    static const DrumVoice toms[4] = { DrumVoice::Tom1, DrumVoice::Tom2,
                                       DrumVoice::Tom3, DrumVoice::Tom4 };
    const int wanted = std::max (0, std::min (3, index));

    if (kit.hasVoice (toms[wanted]))
        return toms[wanted];

    for (int i = wanted; i >= 0; --i)
        if (kit.hasVoice (toms[i])) return toms[i];
    for (int i = wanted; i < 4; ++i)
        if (kit.hasVoice (toms[i])) return toms[i];

    return DrumVoice::Snare;
}

static DrumVoice pickCrash (const DrumProfile& kit, Rng& rng)
{
    const bool haveA = kit.hasVoice (DrumVoice::Crash);
    const bool haveB = kit.hasVoice (DrumVoice::Crash2);

    if (haveA && haveB) return rng.chance (0.5) ? DrumVoice::Crash : DrumVoice::Crash2;
    if (haveA)          return DrumVoice::Crash;
    if (haveB)          return DrumVoice::Crash2;
    if (kit.hasVoice (DrumVoice::China)) return DrumVoice::China;
    return DrumVoice::Ride;
}

static void emit (std::vector<DrumIntent>& out, int tick, DrumVoice v, double accent,
                  const DrumProfile& kit)
{
    if (! kit.hasVoice (v)) return;
    DrumIntent d;
    d.tick   = std::max (0, tick);
    d.voice  = v;
    d.accent = std::max (0.02, std::min (1.0, accent));
    out.push_back (d);
}

void generateDrumBar (const GrooveContext& ctx,
                      const SectionGroove& groove,
                      const BarGrid& grid,
                      int barStartTick,
                      bool isFirstBarOfSection,
                      const std::string& fillSize,
                      const DrumProfile& kit,
                      Rng& rng,
                      std::vector<DrumIntent>& out)
{
    const int beats     = std::max (1, ctx.beatsPerBar);
    const int sixteenth = std::max (1, ctx.beatTicks / 4);
    const double in     = ctx.intensity;

    // How much of the end of the bar the fill takes over.
    int fillFromTick = ctx.barTicks;
    if (fillSize == "small")    fillFromTick = ctx.barTicks - ctx.beatTicks;
    else if (fillSize == "big") fillFromTick = ctx.barTicks - 2 * ctx.beatTicks;
    fillFromTick = std::max (0, fillFromTick);

    // Timing jitter. The kick stays nearly rigid because everything else -
    // including the bass - is measured against it; hats drift the most.
    const double jitterKick  = ctx.humanize * 2.0;
    const double jitterSnare = ctx.humanize * 4.0;
    const double jitterHat   = ctx.humanize * 6.0;

    if (isFirstBarOfSection && in >= 0.4)
        emit (out, barStartTick, pickCrash (kit, rng), 0.85 + in * 0.15, kit);

    // ---- kick ----
    for (int t : grid.kickOnsets)
    {
        if (t >= fillFromTick) continue;
        const bool downbeat = (t == 0);
        const double accent = downbeat ? (0.82 + in * 0.18)
                                       : (0.60 + in * 0.22 + rng.bipolar (0.05));
        emit (out, barStartTick + t + static_cast<int> (rng.bipolar (jitterKick)),
              DrumVoice::Kick, accent, kit);
    }

    // ---- snare ----
    for (int t : grid.snareOnsets)
    {
        if (t >= fillFromTick) continue;
        emit (out, barStartTick + t + static_cast<int> (rng.bipolar (jitterSnare)),
              DrumVoice::Snare, 0.78 + in * 0.22 + rng.bipolar (0.04), kit);
    }

    // ---- ghost notes ----
    if (groove.ghostDensity > 0.0 && kit.hasVoice (DrumVoice::Snare))
    {
        for (int s = 0; s < beats * 4; ++s)
        {
            const int t = s * sixteenth;
            if (t >= fillFromTick) continue;
            if (s % 4 == 0) continue;                       // never on the beat
            if (contains (grid.snareOnsets, t)) continue;
            if (contains (grid.kickOnsets, t)) continue;

            // The "e" and "a" of a beat are where ghosts actually sit.
            const double weight = (s % 2 == 1) ? 1.0 : 0.35;
            if (rng.chance (groove.ghostDensity * weight))
                emit (out, barStartTick + t + static_cast<int> (rng.bipolar (jitterSnare)),
                      DrumVoice::Snare, 0.12 + rng.unit() * 0.14, kit);
        }
    }

    // ---- hats or ride ----
    const DrumVoice timeKeeper =
        groove.useRide && kit.hasVoice (DrumVoice::Ride) ? DrumVoice::Ride : DrumVoice::HatClosed;

    const int step = std::max (1, groove.hatStep);
    for (int s = 0; s < beats * 4; s += step)
    {
        const int t = s * sixteenth;
        if (t >= fillFromTick) continue;

        const bool onBeat = (s % 4 == 0);
        double accent = onBeat ? 0.58 + in * 0.18 : 0.36 + in * 0.14;
        accent += rng.bipolar (0.05);

        DrumVoice v = timeKeeper;

        // Open hat on the last upbeat lifts into the next bar.
        if (groove.openHatOnAnd && s == beats * 4 - 2 && kit.hasVoice (DrumVoice::HatOpen))
        {
            v = DrumVoice::HatOpen;
            accent += 0.15;
        }

        emit (out, barStartTick + t + static_cast<int> (rng.bipolar (jitterHat)), v, accent, kit);
    }

    // ---- fill ----
    if (fillFromTick < ctx.barTicks)
    {
        const int steps = (ctx.barTicks - fillFromTick) / sixteenth;

        // Slower, heavier styles read better with an eighth-note fill; fast or
        // busy ones want sixteenths.
        const int fillStep = (in < 0.45 || ctx.style == "doom" || ctx.style == "sludge") ? 2 : 1;

        int tomIndex = 0;
        for (int s = 0; s < steps; s += fillStep)
        {
            const int t = fillFromTick + s * sixteenth;
            const double progress = steps > 1 ? static_cast<double> (s) / (steps - 1) : 1.0;
            double accent = 0.62 + progress * 0.32 + rng.bipolar (0.05);

            // Five distinct shapes rather than one. A fill is the most audible
            // moment in a bar, so having every one of them be the same tom
            // descent was most of why rerolling sounded like nothing changed.
            DrumVoice v = DrumVoice::Snare;
            bool play = true;

            switch (groove.fillShape)
            {
                case 1:     // snare roll, crescendo
                    v = DrumVoice::Snare;
                    accent = 0.40 + progress * 0.55;
                    break;

                case 2:     // strict alternation, snare against descending toms
                    v = (s % 2 == 0) ? DrumVoice::Snare : pickTom (kit, tomIndex++ % 4);
                    break;

                case 3:     // near silence, then one hard accent into the change
                    play = (s == 0) || (progress > 0.74);
                    v = (progress > 0.74) ? pickTom (kit, 3) : DrumVoice::Snare;
                    accent = (progress > 0.74) ? 0.95 : 0.55;
                    break;

                case 4:     // tom pairs walking down
                    v = pickTom (kit, (s / 2) % 4);
                    break;

                case 0:
                default:    // open on the snare, then walk down the toms
                    if (s == 0)                v = DrumVoice::Snare;
                    else if (rng.chance (0.3)) v = DrumVoice::Snare;
                    else                       v = pickTom (kit, tomIndex++ % 4);
                    break;
            }

            if (play)
                emit (out, barStartTick + t + static_cast<int> (rng.bipolar (jitterSnare)),
                      v, accent, kit);
        }

        // Keep the kick under the fill so the bar does not lose its floor.
        for (int t : grid.kickOnsets)
            if (t >= fillFromTick && rng.chance (0.45))
                emit (out, barStartTick + t, DrumVoice::Kick, 0.7 + in * 0.2, kit);
    }
}

//==============================================================================

static int rootInBassRange (int rootPc, int lowest)
{
    const int lowPc = ((lowest % 12) + 12) % 12;
    const int delta = (((rootPc - lowPc) % 12) + 12) % 12;
    return lowest + delta;
}

void generateBassBar (const GrooveContext& ctx,
                      const BarGrid& grid,
                      int barStartTick,
                      const Chord& chord,
                      const Chord& nextChord,
                      const std::string& pattern,
                      bool isLastBarOfSection,
                      Rng& rng,
                      std::vector<BassIntent>& out)
{
    const int beats     = std::max (1, ctx.beatsPerBar);
    const int sixteenth = std::max (1, ctx.beatTicks / 4);
    const double in     = ctx.intensity;
    const bool metal    = isMetalStyle (ctx.style);

    // Which onsets the bass plays. Reading grid.kickOnsets is the whole point:
    // the two parts are locked because they share one source of truth, not
    // because two generators happened to agree.
    std::vector<int> onsets;
    std::string p = pattern;

    if (p == "auto" || p.empty())
        p = isHeavyStyle (ctx.style) ? "lock_kick" : (in > 0.55 ? "eighths" : "lock_kick");

    if (p == "lock_kick" || p == "lock_kick_octave")
    {
        onsets = grid.kickOnsets;
        if (onsets.empty())
            onsets.push_back (0);
        // A bass part that only ever moves with the kick can leave long holes.
        // Anchor the downbeat so the harmony is always stated.
        if (onsets.front() != 0)
            onsets.insert (onsets.begin(), 0);
    }
    else if (p == "eighths")
    {
        for (int s = 0; s < beats * 4; s += 2) onsets.push_back (s * sixteenth);
    }
    else if (p == "sixteenths")
    {
        for (int s = 0; s < beats * 4; ++s) onsets.push_back (s * sixteenth);
    }
    else   // "roots" and anything unrecognised
    {
        onsets.push_back (0);
    }

    const int root = rootInBassRange (chord.rootPc, ctx.lowestBassNote);
    const int fifth = root + chord.fifthSemitones();

    // Micro-timing. Metal bass sits fractionally ahead of the kick, which is
    // what makes a gallop feel urgent; slower rock sits fractionally behind.
    const double basePush = metal ? -3.0 : (in > 0.6 ? -1.5 : 1.5);

    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const int t = onsets[i];
        if (t >= ctx.barTicks) continue;

        const int nextT = (i + 1 < onsets.size()) ? onsets[i + 1] : ctx.barTicks;
        int gap = nextT - t;
        if (gap <= 0) continue;

        int pitch = root;

        if (p == "lock_kick_octave" && (i % 2 == 1))
            pitch = root + 12;
        else if (! metal && gap >= ctx.beatTicks && rng.chance (0.18) && i > 0)
            pitch = fifth;                          // occasional fifth on long notes

        // Walk into the next section with a chromatic approach note.
        const bool lastOnset = (i + 1 == onsets.size());
        if (isLastBarOfSection && lastOnset && nextChord.valid && nextChord.rootPc != chord.rootPc)
        {
            const int target = rootInBassRange (nextChord.rootPc, ctx.lowestBassNote);
            pitch = target > pitch ? target - 1 : target + 1;
        }

        BassIntent b;
        b.tick   = std::max (0, barStartTick + t
                             + static_cast<int> (basePush + rng.bipolar (ctx.humanize * 3.0)));
        b.pitch  = pitch;
        b.accent = (t == 0 ? 0.80 : 0.62) + in * 0.20 + rng.bipolar (0.05);

        // Palm muting is the default voice of a heavy verse; it opens up as the
        // section gets louder.
        if (metal && in < 0.62)               b.artic = BassArtic::PalmMute;
        else if (isHeavyStyle (ctx.style) && in < 0.35) b.artic = BassArtic::PalmMute;
        else                                  b.artic = BassArtic::Normal;

        // Short and clipped when muted, otherwise let it ring to just before the
        // next attack so the line stays connected.
        const double sustain = (b.artic == BassArtic::PalmMute) ? 0.55 : 0.92;
        b.durationTicks = std::max (sixteenth / 2, static_cast<int> (gap * sustain));

        out.push_back (b);

        // Dead notes fill the space between attacks at higher complexity. They
        // are pitched but choked, and they are most of what makes a bass line
        // sound played rather than programmed.
        if (gap >= sixteenth * 2 && rng.chance (ctx.complexity * 0.30))
        {
            BassIntent d;
            d.tick          = barStartTick + t + gap - sixteenth;
            d.pitch         = pitch;
            d.accent        = 0.14 + rng.unit() * 0.10;
            d.artic         = BassArtic::Dead;
            d.durationTicks = std::max (1, sixteenth / 2);
            out.push_back (d);
        }
    }
}

} // namespace gb
