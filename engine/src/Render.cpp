#include "ghostband/Render.h"
#include "ghostband/Groove.h"
#include "ghostband/MidiFile.h"
#include "ghostband/Music.h"
#include "ghostband/Rng.h"

#include <algorithm>
#include <cmath>

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

// How many times a note-driven instrument restates the chord within a bar. A
// phrase instrument performs its own rhythm, so this only applies when Ghostband
// has to supply one.
static int chordHitsPerBar (PhraseFeel feel)
{
    switch (feel)
    {
        case PhraseFeel::Silent:  return 0;
        case PhraseFeel::Sparse:  return 1;
        case PhraseFeel::Open:    return 1;
        case PhraseFeel::Muted:   return 2;
        case PhraseFeel::Driving: return 4;
        case PhraseFeel::Busy:    return 8;
        default:                  return 1;
    }
}

// Open and sparse ring on; the busier feels want separation between hits or the
// part turns into a wash.
static double chordSustain (PhraseFeel feel)
{
    switch (feel)
    {
        case PhraseFeel::Open:   return 0.98;
        case PhraseFeel::Sparse: return 0.94;
        case PhraseFeel::Muted:  return 0.55;
        case PhraseFeel::Busy:   return 0.60;
        default:                 return 0.72;
    }
}

// Where in the bar a note-driven part strikes, as sixteenth-note offsets.
//
// Evenly dividing the bar into two or four gives block chords on the beat,
// which is what made the guitar sound like one strum per chord. These are
// actual rhythms - syncopations, chugs, pushes - drawn from a pool so a section
// does not repeat one figure for eight bars.
static std::vector<int> chordRhythm (PhraseFeel feel, Rng& rng)
{
    std::vector<std::vector<int>> pool;

    switch (feel)
    {
        case PhraseFeel::Open:
            pool = { { 0 }, { 0, 8 } };
            break;

        case PhraseFeel::Sparse:
            pool = { { 0, 8 }, { 0, 6 }, { 0, 10 }, { 0 } };
            break;

        case PhraseFeel::Muted:
            // Chugs: eighths, a gallop, and a pushed figure that anticipates
            // the beat rather than landing on it.
            pool = { { 0, 2, 4, 6, 8, 10, 12, 14 },
                     { 0, 2, 3, 4, 6, 7, 8, 10, 11, 12, 14, 15 },
                     { 0, 3, 4, 7, 8, 11, 12, 15 },
                     { 0, 2, 4, 7, 8, 10, 12, 15 } };
            break;

        case PhraseFeel::Driving:
            pool = { { 0, 4, 6, 8, 12, 14 },
                     { 0, 2, 4, 8, 10, 12 },
                     { 0, 3, 6, 8, 11, 14 },
                     { 0, 4, 8, 11, 12 } };
            break;

        case PhraseFeel::Busy:
            pool = { { 0, 2, 3, 4, 6, 7, 8, 10, 11, 12, 14, 15 },
                     { 0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 14 },
                     { 0, 2, 4, 6, 8, 10, 12, 13, 14, 15 } };
            break;

        default:
            pool = { { 0 } };
            break;
    }

    return pool[static_cast<size_t> (rng.below (static_cast<int> (pool.size())))];
}

// A solo, as opposed to a run up and down a scale.
//
// What separates the two is almost entirely phrasing. A player states a short
// idea, stops, and answers it - and the silence between those ideas is doing as
// much work as the notes. So this is built out of phrases rather than notes: a
// phrase gets a rhythm, a shape and a note to aim at, plays for part of its
// span and rests for the remainder, and the next one answers it.
//
// Three things keep it from wandering:
//
//   - it moves mostly by step through the scale, because a line that leaps
//     everywhere reads as arpeggios rather than melody;
//   - every phrase lands on a chord tone of whatever chord is under it, so the
//     line agrees with the harmony without tracking it note for note;
//   - the rhythm of a phrase is sometimes reused with different notes, which is
//     the call and response that makes two phrases sound related.
struct SoloPhrase
{
    int  bars      = 2;
    bool answers   = false;   // reuses the previous phrase's rhythm
};

// Rhythms for one bar, as sixteenth offsets. Deliberately full of holes - a
// solo that plays on every subdivision is a scale exercise.
static std::vector<int> soloRhythm (Rng& rng, bool busy)
{
    static const std::vector<std::vector<int>> sparse = {
        { 0, 6 }, { 0, 4, 10 }, { 4, 10 }, { 0, 8 }, { 6, 10 }, { 0, 3, 8 },
    };
    static const std::vector<std::vector<int>> dense = {
        { 0, 2, 4, 8, 10 }, { 0, 4, 6, 8, 12 }, { 2, 4, 6, 10, 12 },
        { 0, 2, 6, 8, 10, 14 }, { 0, 4, 8, 10, 12 },
    };

    const auto& pool = busy ? dense : sparse;
    return pool[static_cast<size_t> (rng.below (static_cast<int> (pool.size())))];
}

static void generateSolo (const SectionPlan& s,
                          const std::vector<Chord>& chords,
                          int sectionStartTick,
                          int barTicks,
                          int keyPc,
                          Mode mode,
                          const std::string& style,
                          const PhraseProfile* profile,
                          double humanize,
                          Rng& rng,
                          PhrasePart& out)
{
    if (chords.empty() || profile == nullptr)
        return;

    const std::vector<int>& scale = soloScale (mode, style);
    if (scale.empty())
        return;

    // Sit the line in the upper half of the instrument's range, so it is above
    // whatever is holding the chords rather than buried inside them.
    const int zoneLow  = profile->chordLowest;
    const int zoneHigh = profile->chordHighest;
    const int centre   = zoneLow + (zoneHigh - zoneLow) * 2 / 3;

    // Degrees are counted from the key, so the line is in the key rather than
    // chasing every chord. Chord tones are how it agrees with the harmony.
    const auto pitchForDegree = [&] (int degree)
    {
        const int size   = static_cast<int> (scale.size());
        const int octave = static_cast<int> (std::floor (degree / static_cast<double> (size)));
        int       step   = degree - octave * size;
        if (step < 0) step += size;

        return centre + octave * 12 + scale[static_cast<size_t> (step)]
             + (((keyPc - (centre % 12)) % 12) + 12) % 12;
    };

    const int sixteenth = barTicks / 16;
    int degree = 0;
    std::vector<int> lastRhythm;

    for (int bar = 0; bar < s.bars; )
    {
        // Two bars is the natural unit: one to say something, one to answer.
        const int phraseBars = (bar + 2 <= s.bars && rng.chance (0.75)) ? 2 : 1;
        const bool answering = ! lastRhythm.empty() && rng.chance (0.45);
        const bool busy      = s.intensity > 0.72 && rng.chance (0.5);

        // A phrase plays for its first bar or two and then leaves room. Without
        // this the part never stops, and a solo that never stops is a texture.
        const int playBars = phraseBars;

        // The shape this phrase traces. Rising phrases build, falling ones
        // resolve, and an arch does both - which is most of what a melody is.
        const int shape = rng.below (4);   // 0 rise, 1 fall, 2 arch, 3 hold

        for (int b = 0; b < playBars && bar + b < s.bars; ++b)
        {
            const int barIndex = bar + b;
            const Chord& chord = chords[static_cast<size_t> (barIndex) % chords.size()];

            std::vector<int> rhythm = (answering && b < static_cast<int> (lastRhythm.size()))
                                        ? lastRhythm
                                        : soloRhythm (rng, busy);
            if (! answering)
                lastRhythm = rhythm;

            const int barStart = sectionStartTick + barIndex * barTicks;

            for (size_t i = 0; i < rhythm.size(); ++i)
            {
                const bool last = (i + 1 == rhythm.size());

                // Step through the scale, leaping only occasionally. A line that
                // leaps constantly stops sounding like one line.
                if (shape == 0)      degree += rng.chance (0.75) ? 1 : 2;
                else if (shape == 1) degree -= rng.chance (0.75) ? 1 : 2;
                else if (shape == 2) degree += (i * 2 < rhythm.size()) ? 1 : -1;
                else                 degree += rng.chance (0.5) ? 1 : -1;

                // Keep it in the register it started in rather than climbing out
                // of the instrument over eight bars.
                if (degree >  9) degree -= static_cast<int> (scale.size());
                if (degree < -6) degree += static_cast<int> (scale.size());

                int pitch = pitchForDegree (degree);

                // The note a phrase lands on belongs to the chord underneath it.
                // This is the whole trick: the line is free in between and
                // agrees with the harmony where it matters.
                if (last)
                {
                    const int wanted[3] = { chord.rootPc,
                                            (chord.rootPc + std::max (0, chord.thirdSemitones())) % 12,
                                            (chord.rootPc + chord.fifthSemitones()) % 12 };

                    int best = pitch, bestDistance = 99;
                    for (int candidate = pitch - 3; candidate <= pitch + 3; ++candidate)
                        for (int w : wanted)
                            if (((candidate % 12) + 12) % 12 == w
                                && std::abs (candidate - pitch) < bestDistance)
                            {
                                best = candidate;
                                bestDistance = std::abs (candidate - pitch);
                            }
                    pitch = best;
                }

                while (pitch > zoneHigh) pitch -= 12;
                while (pitch < zoneLow)  pitch += 12;

                LeadIntent n;
                n.pitch  = pitch;
                n.tick   = barStart + rhythm[i] * sixteenth
                         + static_cast<int> (rng.bipolar (humanize * 6.0));
                n.target = last;

                // A landing note is held; the ones on the way are short, which
                // is what leaves the air between phrases audible.
                const int nextOffset = last ? 16 : rhythm[i + 1];
                n.durationTicks = std::max (sixteenth,
                                            (nextOffset - rhythm[i]) * sixteenth
                                                * (last ? 3 : 1) / (last ? 2 : 1));

                n.accent = (last ? 0.9 : 0.68) + s.intensity * 0.1 + rng.bipolar (0.05);

                out.lead.push_back (n);
            }
        }

        // The rest. A phrase that is answered gets less room, because the answer
        // is the thing filling it.
        bar += phraseBars + (answering ? 0 : (rng.chance (0.5) ? 1 : 0));
    }

    // One voice. A landing note is deliberately long - that is what makes it
    // land - and long enough to run into whatever comes next, so each note
    // stops where the following one starts. A note before a rest keeps its full
    // length, because ringing into silence is the point of the rest.
    for (size_t i = 0; i + 1 < out.lead.size(); ++i)
    {
        const int room = out.lead[i + 1].tick - out.lead[i].tick;
        if (room > 0)
            out.lead[i].durationTicks = std::min (out.lead[i].durationTicks, room);
    }
}

// Arrangement decisions expressed as control moves rather than notes: more
// drive where the section is loud, brighter where it leads, the effect brought
// in for the biggest moments and pulled out of the quiet ones. What each name
// actually reaches is the profile's business, and an unmapped one is ignored.
static void addControls (const ControlSet* set, const std::string& profileId,
                         std::vector<ControlIntent>& out, int tick,
                         double intensity, bool leading, const std::string& role,
                         double throughSong, uint32_t sectionSeed, uint32_t songSeed)
{
    if (set == nullptr)
        return;

    const bool bigMoment = (role == "chorus" || role == "solo") && intensity > 0.7;

    // Every control the profile declares gets a value, whatever it is called.
    // The generator does not need to know what knob it is reaching - only what
    // the section is like - which is what lets a profile map as many of an
    // instrument's controls as its owner cares to.
    for (const ControlDef& def : set->all())
    {
        // A mapping worth remembering is not always a mapping worth driving.
        // Without this the only way to leave a control alone was "fixed", which
        // does not leave it alone at all - it pins it to the bottom of its
        // range, so a mix knob ends up at zero and an effect gets switched off.
        if (def.follows == "none")
            continue;

        // A level control belongs to the mix knob on the song screen, not to
        // the arrangement. Driving it here would mean the section fought the
        // knob for the same control, and the knob would appear not to work.
        if (def.follows == "level")
            continue;

        double t = 0.5;

        const bool rollsEachSection = def.follows == "random";
        const bool rollsOncePerSong  = def.follows == "random once";

        if      (def.follows == "lead")   t = leading ? 0.85 : 0.25;
        else if (def.follows == "peaks")  t = bigMoment ? 1.0 : (intensity < 0.4 ? 0.0 : 0.25);
        else if (def.follows == "rising") t = throughSong;
        else if (def.follows == "fixed")  t = 0.0;   // parks at `low`, whatever that means for its type
        else if (rollsEachSection || rollsOncePerSong)
        {
            // Some of an instrument's controls have no right answer to tie to
            // the arrangement: which amp, which character, which effect. They
            // change the sound rather than the dynamics, and any of them is
            // valid, so the useful thing to do is choose.
            //
            // Each control draws from its own stream rather than the section's,
            // so adding a mapping cannot shift a single note of a song that was
            // already right. The stream is still derived from the seed, so a
            // song stays reproducible and a reroll genuinely rerolls it.
            //
            // "random once" is keyed off the song rather than the section, so
            // one choice holds for the whole song - nobody swaps amps between
            // the verse and the chorus. "random" is keyed off the section, so a
            // pedal can come in for the chorus and go away again.
            const uint32_t base = rollsOncePerSong ? songSeed : sectionSeed;
            const uint32_t salt = hashString (profileId + "/" + def.name);

            Rng ctlRng (deriveSeed (base, salt));
            t = ctlRng.unit();
        }
        else                              t = intensity;   // "intensity"

        // A supporting part is held back across the board, so it sits behind
        // the lead rather than competing for the same space. A parked or rolled
        // control is exempt: it is a choice of sound, not a level, and skewing
        // it low would just mean the supporting guitar always got amp one.
        if (! leading && def.follows != "fixed"
                      && ! rollsEachSection && ! rollsOncePerSong)
            t *= 0.6;

        ControlIntent c;
        c.tick    = tick;
        c.control = def.name;

        // What a knob, a switch and a selector each make of that is the
        // control's own business. This is emitted once at the top of the
        // section and held, so a selector genuinely stays on one choice for the
        // section rather than hunting through its positions.
        c.amount = def.valueAt (t);

        out.push_back (c);
    }
}

// What a part plays when it is not leading the section: long and out of the
// way. A supporting part that keeps its own busy feel is just two instruments
// competing, which is the thing this exists to prevent.
static PhraseFeel supportFeel (PhraseFeel wanted)
{
    switch (wanted)
    {
        case PhraseFeel::Silent: return PhraseFeel::Silent;
        case PhraseFeel::Busy:
        case PhraseFeel::Driving: return PhraseFeel::Sparse;
        default:                  return PhraseFeel::Open;
    }
}

static void generatePhrasePart (const SectionPlan& s,
                                const std::vector<Chord>& chords,
                                int sectionStartTick,
                                int barTicks,
                                int keyPc,
                                Mode mode,
                                const std::string& style,
                                const PhraseProfile* profile,
                                PhraseFeel feel,
                                double humanize,
                                bool supporting,
                                double throughSong,
                                uint32_t sectionSeed,
                                uint32_t songSeed,
                                Rng& rng,
                                PhrasePart& out)
{
    PhraseIntent pi;
    pi.tick   = sectionStartTick;
    pi.feel   = feel;
    pi.accent = 0.6 + s.intensity * 0.4;
    out.phrases.push_back (pi);

    if (feel == PhraseFeel::Silent || chords.empty())
        return;

    addControls (profile != nullptr ? &profile->controls : nullptr,
                 profile != nullptr ? profile->id : std::string(),
                 out.controls, sectionStartTick, s.intensity, ! supporting, s.role,
                 throughSong, sectionSeed, songSeed);

    // A player either comps or solos. Doing both at once is not a thing a
    // guitarist can physically do, and layering a line over your own chords is
    // most of what makes generated music sound generated.
    if (feel == PhraseFeel::Solo)
    {
        // Its own stream, derived from the section seed.
        //
        // The solo draws a variable number of values - how many depends on how
        // many phrases it decides to play and whether each one is answered - so
        // running it off the shared song rng left every section after it drawing
        // from a different place in that stream. Rerolling one section then moved
        // the drums in a later one, which is the exact promise per-section
        // rerolls make. Nothing else in this function touches the shared stream,
        // which is why nothing else broke it.
        Rng soloRng (deriveSeed (sectionSeed, 0x50100u));
        generateSolo (s, chords, sectionStartTick, barTicks, keyPc, mode, style,
                      profile, humanize, soloRng, out);
        return;
    }

    const bool phraseDriven = (profile != nullptr && profile->isPhraseDriven());
    const int hits = phraseDriven ? 1 : chordHitsPerBar (feel);
    if (hits <= 0)
        return;

    for (int bar = 0; bar < s.bars; ++bar)
    {
        const Chord& c = chords[static_cast<size_t> (bar) % chords.size()];

        // A power chord carries no third, which is useless to anything that has
        // to voice the harmony. Recover the third the power chord stands in for.
        int third   = c.thirdSemitones();
        int fifth   = c.fifthSemitones();
        int seventh = c.seventhSemitones();
        if (third < 0)
        {
            const ChordQuality q = diatonicTriadQuality (c.rootPc, keyPc, mode);
            third = (q == ChordQuality::Minor || q == ChordQuality::Diminished) ? 3 : 4;
            fifth = (q == ChordQuality::Diminished) ? 6 : 7;
        }

        const int barStart = sectionStartTick + bar * barTicks;

        // A phrase instrument keeps performing while the chord is held, so a
        // repeated chord is extended rather than retriggered - retriggering
        // restarts the riff mid-bar and sounds like a stutter.
        if (phraseDriven && ! out.chords.empty())
        {
            ChordIntent& prev = out.chords.back();
            if (prev.rootPc == c.rootPc && prev.thirdSemis == third
                && prev.tick + prev.durationTicks >= barStart)
            {
                prev.durationTicks += barTicks;
                continue;
            }
        }

        // A note-driven part plays a rhythm; a phrase instrument is simply
        // handed the harmony and performs its own.
        const int sixteenth = std::max (1, barTicks / 16);
        const std::vector<int> pattern = phraseDriven ? std::vector<int> { 0 }
                                                      : chordRhythm (feel, rng);

        const int hitCount = static_cast<int> (pattern.size());
        for (int h = 0; h < hitCount; ++h)
        {
            const int step = pattern[static_cast<size_t> (h)] * sixteenth;
            const int nextOffset = (h + 1 < hitCount)
                                     ? pattern[static_cast<size_t> (h + 1)] * sixteenth
                                     : barTicks;
            ChordIntent ci;
            ci.strumUp = (h % 2) == 1;

            // No timing jitter for a phrase instrument. It performs its own
            // rhythm, so jittering the chord only risks opening a gap - and a
            // gap of even a few ticks reads to the instrument as "no chord
            // held", which stops the phrase and restarts it on the next chord.
            // That is what made the guitar cut out after a second at a time.
            ci.tick = phraseDriven
                        ? barStart
                        : std::max (0, barStart + step
                                       + static_cast<int> (rng.bipolar (humanize * 3.0)));

            // Overlap the next chord rather than meeting it exactly, so a phrase
            // instrument never sees a moment with nothing held. A note-driven
            // part is cut short of the next strike instead, which is what gives
            // a muted chug its separation.
            const int gap = std::max (sixteenth, nextOffset - step);
            ci.durationTicks = phraseDriven
                                 ? barTicks + kPPQ / 8
                                 : std::max (sixteenth / 2,
                                             static_cast<int> (gap * chordSustain (feel)));
            ci.rootPc        = c.rootPc;
            ci.thirdSemis    = third;
            ci.fifthSemis    = fifth;
            ci.seventhSemis  = seventh;
            ci.accent        = (h == 0 ? 0.78 : 0.62) + s.intensity * 0.22
                             + rng.bipolar (0.04);

            // A supporting part sits back and gets out of the lead's register.
            // Dropping the third keeps it from doubling the harmony note for
            // note, so it reads as a pad rather than a second rhythm part.
            if (supporting)
            {
                ci.accent       *= 0.72;
                ci.thirdSemis    = -1;
                ci.fifthSemis    = 7;
                ci.seventhSemis  = -1;
            }

            out.chords.push_back (ci);
        }
    }
}

// Where a beat position lands once the beat is swung.
//
// Straight eighths sit at 0 and 1/2 of the beat; a full triplet shuffle puts
// the offbeat at 2/3. So the midpoint moves to 1/2 + amount/6, and everything
// either side of it is stretched or squeezed to match. Doing it as a warp of
// the whole beat rather than as a rule about eighth notes means sixteenths
// inside the long half stay long and the ones inside the short half stay
// short, which is what a shuffle actually sounds like - and it costs nothing
// when the amount is zero.
static double swungPosition (double pos, double amount)
{
    if (amount <= 0.0)
        return pos;

    const double mid = 0.5 + amount / 6.0;

    return pos < 0.5 ? pos * (mid / 0.5)
                     : mid + (pos - 0.5) * ((1.0 - mid) / 0.5);
}

static int swungTick (int tick, double amount, int beatTicks)
{
    if (amount <= 0.0 || beatTicks <= 0 || tick < 0)
        return tick;

    const int    beat = tick / beatTicks;
    const double pos  = static_cast<double> (tick % beatTicks) / beatTicks;

    return beat * beatTicks
         + static_cast<int> (swungPosition (pos, amount) * beatTicks + 0.5);
}

// Applies the swing to a finished performance. Onsets and note ends are warped
// together, so a note that was a beat long stays a beat long rather than
// growing over whatever follows it.
// Whether an onset belongs on a swung grid at all.
//
// A shuffle is a triplet feel, and a straight sixteenth is not a rhythm that
// exists inside one. The generators subdivide in sixteenths because that is
// what every other style wants, so warping their output gave onsets at 0, 0.30,
// 0.60 and 0.80 of the beat: four notes lurching against a three-feel, which is
// what the blues preset sounded like. A shuffle ride plays eighths.
//
// So for a swung song the sixteenths between the eighths are dropped before the
// warp, which turns a sixteenth ride into an eighth one and thins fills to
// match. Humanize has already nudged these off the grid by a few ticks, so the
// test is which sixteenth an onset is nearest rather than which one it is on.
static bool sitsOnASwungEighth (int tick, int beatTicks)
{
    if (beatTicks <= 0)
        return true;

    const double pos = static_cast<double> (tick % beatTicks) / beatTicks;

    // Nearest sixteenth, as a quarter of a beat.
    const int nearest = static_cast<int> (pos * 4.0 + 0.5) % 4;

    return nearest == 0 || nearest == 2;   // the beat and the eighth, not the e or the a
}

static void applySwing (Performance& perf, double amount, int beatTicks)
{
    if (amount <= 0.0)
        return;

    const auto onGrid = [beatTicks] (int tick) { return sitsOnASwungEighth (tick, beatTicks); };

    {
        const auto thin = [&onGrid] (auto& v)
        {
            v.erase (std::remove_if (v.begin(), v.end(),
                                     [&onGrid] (const auto& e) { return ! onGrid (e.tick); }),
                     v.end());
        };

        thin (perf.drums);
        thin (perf.bass);
        thin (perf.guitar.chords);
        thin (perf.piano.chords);
        thin (perf.guitar.lead);
        thin (perf.piano.lead);
    }

    const auto warp = [amount, beatTicks] (int t) { return swungTick (t, amount, beatTicks); };

    const auto warpHeld = [&warp] (int& tick, int& duration)
    {
        const int end = warp (tick + duration);
        tick     = warp (tick);
        duration = std::max (1, end - tick);
    };

    for (DrumIntent& d : perf.drums)
        d.tick = warp (d.tick);

    for (BassIntent& b : perf.bass)
        warpHeld (b.tick, b.durationTicks);

    for (PhrasePart* part : { &perf.guitar, &perf.piano })
    {
        for (ChordIntent& c : part->chords)
            warpHeld (c.tick, c.durationTicks);

        for (LeadIntent& n : part->lead)
            warpHeld (n.tick, n.durationTicks);

        for (PhraseIntent& ph : part->phrases)
            ph.tick = warp (ph.tick);

        // Control moves sit on section boundaries, which are on the beat and
        // therefore unmoved - warped anyway so nothing depends on that holding.
        for (ControlIntent& c : part->controls)
            c.tick = warp (c.tick);
    }

    for (ControlIntent& c : perf.drumControls) c.tick = warp (c.tick);
    for (ControlIntent& c : perf.bassControls) c.tick = warp (c.tick);

    for (Marker& m : perf.markers)
        m.tick = warp (m.tick);
}

RenderResult renderPerformance (const SongPlan& plan,
                                const DrumProfile& kit,
                                const BassProfile& bass,
                                const PhraseProfile* guitar,
                                const PhraseProfile* piano)
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
        uint32_t salt = s.vary ? static_cast<uint32_t> (si + 1) * 7919u
                               : hashString (s.role) | 1u;

        // Fold in this section's own reroll counter. Rerolling one section
        // changes its seed and nothing else's, which is what makes rerolling a
        // single chorus safe when the rest of the song is already right.
        salt = deriveSeed (salt, s.reroll + 1u);

        const uint32_t sectionSeed = deriveSeed (plan.seed, salt);
        Rng rng (sectionSeed);

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

        // How far through the song this section sits, for any control the user
        // wants building across the whole thing. Section scope, because all
        // four parts ask for it.
        const double throughSong = plan.sections.size() > 1
                                     ? static_cast<double> (si) / (plan.sections.size() - 1)
                                     : 1.0;

        // Drums and bass are not phrase parts, but their controls follow the
        // arrangement exactly the same way. Both always count as leading: they
        // are the rhythm section, never the part being kept out of the way.
        addControls (&kit.controls, kit.id, result.performance.drumControls,
                     tick, s.intensity, true, s.role, throughSong,
                     sectionSeed, plan.seed);
        addControls (&bass.controls, bass.id, result.performance.bassControls,
                     tick, s.intensity, true, s.role, throughSong,
                     sectionSeed, plan.seed);

        // Resolved once per section rather than per bar, so the bass keeps one
        // identity across the section while still varying between rerolls.
        const std::string bassPattern = (s.bassPattern.empty() || s.bassPattern == "auto")
                                          ? chooseBassPattern (ctx, rng)
                                          : s.bassPattern;
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

            // Read the parsed flags, not the raw string. Comparing the string
            // against "full" and "drums" silently dropped every section that
            // used a list like "drums+bass" - they rendered completely empty.
            const bool playDrums = s.playsDrums;
            const bool playBass  = s.playsBass;

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

                generateBassBar (ctx, grid, barStart, chord, next, bassPattern,
                                 lastBar, rng, result.performance.bass);
            }
        }

        // ---- guitar and piano ----
        // Generated per section rather than per bar: a phrase instrument is
        // told what to play once and then left alone, and even a note-driven
        // one wants a single coherent treatment across the section.
        //
        // Two chordal instruments both playing a full part fight each other, so
        // one leads the section and the other supports. The support part is
        // thinned to long held chords and dropped an octave clear of the lead,
        // which is what a real arrangement does rather than simply turning it
        // down.
        {
            const bool playGuitar = (guitar != nullptr && s.playsGuitar);
            const bool playPiano  = (piano  != nullptr && s.playsPiano);

            bool guitarExplicit = false, pianoExplicit = false;
            PhraseFeel guitarFeel = phraseFeelFromName (s.guitarPhrase, guitarExplicit);
            PhraseFeel pianoFeel  = phraseFeelFromName (s.pianoPhrase,  pianoExplicit);
            if (! guitarExplicit) guitarFeel = chooseGuitarFeel (ctx, rng);
            if (! pianoExplicit)  pianoFeel  = choosePianoFeel (ctx, rng);

            bool guitarSupports = false, pianoSupports = false;

            if (playGuitar && playPiano
                && guitarFeel != PhraseFeel::Silent && pianoFeel != PhraseFeel::Silent)
            {
                // A distorted guitar wins a loud section; a piano wins a quiet
                // or half-time one, where a guitar would only get in the way.
                int guitarLeadWeight = styleUsesPowerChords (plan.style) ? 65 : 45;
                if (ctx.feel == Feel::HalfTime)   guitarLeadWeight -= 30;
                if (s.intensity > 0.75)           guitarLeadWeight += 20;
                if (s.intensity < 0.40)           guitarLeadWeight -= 20;
                if (s.role == "bridge")           guitarLeadWeight -= 25;

                bool guitarLeads = rng.below (100) < std::max (5, std::min (95, guitarLeadWeight));

                // An explicit choice always wins. Weighing the section is right
                // most of the time, but not when you already know what you want
                // out front.
                if      (s.lead == "guitar") guitarLeads = true;
                else if (s.lead == "piano")  guitarLeads = false;

                guitarSupports = ! guitarLeads;
                pianoSupports  = guitarLeads;

                // "both" is occasionally what a big chorus wants and usually a
                // mess, so it is available and never chosen automatically.
                if (s.lead == "both")
                    guitarSupports = pianoSupports = false;
            }

            if (playGuitar)
            {
                const size_t before = result.performance.guitar.chords.size();
                generatePhrasePart (s, chords, tick, barTicks, keyPc, mode, plan.style,
                                    guitar,
                                    guitarSupports ? supportFeel (guitarFeel) : guitarFeel,
                                    plan.humanize, guitarSupports, throughSong,
                                    sectionSeed, plan.seed, rng,
                                    result.performance.guitar);
                report.guitarChords = static_cast<int> (result.performance.guitar.chords.size() - before);
                report.guitarFeel   = std::string (phraseFeelName (guitarSupports ? supportFeel (guitarFeel)
                                                                                  : guitarFeel))
                                    // Only ever say who is leading when there are
                                    // two parts to lead. A silent piano was being
                                    // reported as "silent (lead)", which is both
                                    // meaningless and alarming to read.
                                    + (playPiano && pianoFeel  != PhraseFeel::Silent
                                                 && guitarFeel != PhraseFeel::Silent
                                         ? (guitarSupports ? " (support)" : " (lead)") : "");
            }

            if (playPiano)
            {
                const size_t before = result.performance.piano.chords.size();
                generatePhrasePart (s, chords, tick, barTicks, keyPc, mode, plan.style,
                                    piano,
                                    pianoSupports ? supportFeel (pianoFeel) : pianoFeel,
                                    plan.humanize, pianoSupports, throughSong,
                                    sectionSeed, plan.seed, rng,
                                    result.performance.piano);
                report.pianoChords = static_cast<int> (result.performance.piano.chords.size() - before);
                report.pianoFeel   = std::string (phraseFeelName (pianoSupports ? supportFeel (pianoFeel)
                                                                                : pianoFeel))
                                   + (playGuitar && guitarFeel != PhraseFeel::Silent
                                                 && pianoFeel  != PhraseFeel::Silent
                                        ? (pianoSupports ? " (support)" : " (lead)") : "");
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

    // Last, on the finished performance: a shuffle is the same groove played on
    // a different grid, not a different groove. Doing it here means every
    // generator stays straight-ahead and none of them has to know.
    applySwing (result.performance, plan.swing, beatTicks);

    // The per-section counts were taken as each section was generated, which is
    // before the swing thinned the sixteenths out of it - so the report claimed
    // three hundred drum hits in a bar where a hundred and seventy play. Count
    // again from what actually survived. Section boundaries sit on the beat and
    // the warp leaves those alone, so the ranges still hold.
    if (plan.swing > 0.0)
    {
        // Humanize nudges a note either side of the position it was written
        // for, and the note it nudges backwards hardest is a section's own
        // downbeat - which lands a tick or two before the boundary and gets
        // counted in the section before it. That made rerolling one section
        // change the numbers reported for its neighbour, which reads as the
        // per-section reroll leaking when nothing about the playing had moved.
        //
        // So the window is shifted back by half a subdivision. Drift is always
        // smaller than that by construction, and the last real position in a
        // section is a whole sixteenth clear of its end, so nothing legitimate
        // falls in the gap.
        const int drift = std::max (1, barTicks / 32);

        for (SectionReport& sec : result.sections)
        {
            const auto within = [&sec, drift] (int tick)
            {
                return tick >= sec.startTick - drift && tick < sec.endTick - drift;
            };

            sec.drumHits = sec.bassNotes = sec.guitarChords = sec.pianoChords = 0;

            for (const DrumIntent& d : result.performance.drums)
                if (within (d.tick)) ++sec.drumHits;

            for (const BassIntent& b : result.performance.bass)
                if (within (b.tick)) ++sec.bassNotes;

            for (const ChordIntent& c : result.performance.guitar.chords)
                if (within (c.tick)) ++sec.guitarChords;

            for (const ChordIntent& c : result.performance.piano.chords)
                if (within (c.tick)) ++sec.pianoChords;
        }
    }

    return result;
}

//==============================================================================

bool writeMidi (const SongPlan& plan,
                const Performance& perf,
                const DrumProfile& kit,
                const BassProfile& bass,
                const std::string& path,
                std::string& error,
                const PhraseProfile* guitar,
                const PhraseProfile* piano)
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
    kit.controls.render (perf.drumControls, kit.channel, 0, drums);
    mf.tracks.push_back (drums);

    MidiTrack bassTrack;
    bassTrack.name = bass.name;
    bass.render (perf.bass, bassTrack, plan.bassTuning);
    bass.controls.render (perf.bassControls, bass.channel, bass.keyswitchLeadTicks, bassTrack);
    mf.tracks.push_back (bassTrack);

    // Only emitted when the song actually has the part, so a plan without a
    // guitar does not gain an empty track.
    if (guitar != nullptr && ! perf.guitar.chords.empty())
    {
        MidiTrack t;
        t.name = guitar->name;
        guitar->render (perf.guitar, t);
        mf.tracks.push_back (t);
    }

    if (piano != nullptr && ! perf.piano.chords.empty())
    {
        MidiTrack t;
        t.name = piano->name;
        piano->render (perf.piano, t);
        mf.tracks.push_back (t);
    }

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
