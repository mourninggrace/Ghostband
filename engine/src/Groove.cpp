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

static bool isHeavyStyle (const std::string& style)
{
    return isMetalStyle (style) || style == "hard_rock" || style == "punk";
}

//==============================================================================

SectionGroove buildSectionGroove (const GrooveContext& ctx, Rng& rng)
{
    SectionGroove g;

    const int beats = std::max (1, ctx.beatsPerBar);
    const bool metal = isMetalStyle (ctx.style);
    const double in = ctx.intensity;

    std::vector<int> allBeats;
    for (int b = 0; b < beats; ++b) allBeats.push_back (b);

    // ---- kick ------------------------------------------------------------
    // Cell offsets are in 16ths within a single beat: {0,2,3} is the gallop,
    // {0,1,2,3} is straight double kick, {0} is a plain quarter pulse.
    switch (ctx.feel)
    {
        case Feel::Blast:
            g.kickBeats  = allBeats;
            g.kickCell   = { 0, 2 };
            g.doubleKick = true;
            break;

        case Feel::HalfTime:
            g.kickBeats = { 0 };
            if (beats >= 3) g.kickBeats.push_back (beats >= 4 ? 2 : 1);
            g.kickCell  = in > 0.6 ? std::vector<int> { 0, 3 } : std::vector<int> { 0 };
            break;

        case Feel::DoubleTime:
            g.kickBeats = allBeats;
            g.kickCell  = { 0, 2 };
            break;

        case Feel::Straight:
        default:
            if (metal && in > 0.78)
            {
                g.kickBeats  = allBeats;
                g.kickCell   = rng.chance (0.5) ? std::vector<int> { 0, 1, 2, 3 }
                                                : std::vector<int> { 0, 2, 3 };
                g.doubleKick = true;
            }
            else if (metal && in > 0.48)
            {
                g.kickBeats = allBeats;
                g.kickCell  = rng.chance (0.6) ? std::vector<int> { 0, 2, 3 }   // gallop
                                               : std::vector<int> { 0, 1, 2 };  // reverse gallop
            }
            else if (metal && in > 0.30)
            {
                // Without this rung, metal falls straight from a gallop to a
                // sparse rock pattern, and two verses half a step apart in
                // intensity come out as different arrangements rather than
                // variations of one.
                g.kickBeats = allBeats;
                g.kickCell  = { 0, 2 };
            }
            else if (in > 0.62)
            {
                g.kickBeats = allBeats;
                g.kickCell  = { 0 };
            }
            else if (in > 0.34)
            {
                g.kickBeats = { 0 };
                if (beats >= 3) g.kickBeats.push_back (2);
                if (beats >= 4 && rng.chance (0.5)) g.kickBeats.push_back (3);
                g.kickCell = rng.chance (0.45) ? std::vector<int> { 0, 3 }
                                               : std::vector<int> { 0 };
            }
            else
            {
                g.kickBeats = { 0 };
                if (beats >= 3) g.kickBeats.push_back (2);
                g.kickCell = { 0 };
            }
            break;
    }

    // A single alternate cell, used on the last beat of alternating bars. This
    // is what stops an eight-bar section sounding like one bar looped.
    g.kickCellAlt = g.kickCell;
    if (g.kickCell.size() < 3 && rng.chance (0.7))
        g.kickCellAlt.push_back (3);

    // ---- snare -----------------------------------------------------------
    if (ctx.feel == Feel::HalfTime)
    {
        g.snareBeats = { beats >= 4 ? 2 : beats / 2 };
    }
    else if (beats == 4)
    {
        g.snareBeats = { 1, 3 };
    }
    else if (beats >= 6)
    {
        g.snareBeats = { beats / 2, beats - 1 };
    }
    else
    {
        g.snareBeats = { beats / 2 };
    }

    // ---- cymbals ---------------------------------------------------------
    g.useRide = (in > 0.66) || ctx.role == "chorus" || ctx.feel == Feel::Blast;

    if (ctx.feel == Feel::Blast || ctx.feel == Feel::DoubleTime)
        g.hatStep = 2;
    else if (ctx.complexity > 0.6 && in < 0.7)
        g.hatStep = 1;                      // sixteenth-note hat work
    else if (in < 0.25)
        g.hatStep = 4;                      // quarters, for sparse intros
    else
        g.hatStep = 2;

    g.openHatOnAnd = (! g.useRide) && in > 0.38 && in < 0.72 && rng.chance (0.6);

    // Ghost notes are what separate a groove from a drum machine, but they turn
    // to mud at high intensity where the backbeat should dominate.
    g.ghostDensity = ctx.complexity * 0.45 * (in > 0.8 ? 0.35 : 1.0);
    if (isMetalStyle (ctx.style)) g.ghostDensity *= 0.5;

    return g;
}

//==============================================================================

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
            const double accent = 0.62 + progress * 0.32 + rng.bipolar (0.05);

            // Open on the snare, then walk down the toms.
            DrumVoice v;
            if (s == 0)                       v = DrumVoice::Snare;
            else if (rng.chance (0.3))        v = DrumVoice::Snare;
            else                              v = pickTom (kit, tomIndex++ % 4);

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
