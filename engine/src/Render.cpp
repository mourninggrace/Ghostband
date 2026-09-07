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

// A lead solo, as opposed to a melody.
//
// The first version of this wrote melodies: two bar phrases, mostly stepwise, a
// note to land on, and a rest to answer into. Measured on the blues it played
// 3.3 notes a bar with a median gap of a quarter note and silences of a bar and
// a half - which was reported, accurately, as one note being held and then
// changed. That is a reasonable blues vocabulary and completely wrong for the
// thing asked for, which is Satriani, Vai and Hammett.
//
// What that style is built from is not "more notes" - turning the density up on
// a wandering line only makes a longer wandering line. It is a handful of
// devices, each of which generates a lot of notes out of one idea:
//
//   RUN       straight subdivisions through the scale, turning round when it
//             reaches the end of the neck. What the others are spelled against,
//             and on its own the least interesting of them.
//   SEQUENCE  a three or four note cell moved one scale step per repeat. The
//             most identifiable device in the style: a run has no internal
//             shape, a sequence is one shape restated at a new pitch. A three
//             note cell over a four note grid also walks its accent across the
//             beat, which is where the momentum comes from.
//   PEDAL     a fixed high note alternating with a line moving underneath it.
//   LICK      a short motif played two or three times unchanged, which is how a
//             solo says something rather than merely moving.
//   LAND      a fast approach into one long chord tone. This is the breath. The
//             others are exercises without it.
//
// Density is a property of the device rather than a knob: a run is sixteen
// notes in a bar because that is what a run is. What intensity buys is which
// devices are in the hat and how much room is left between them.
struct SoloVoice
{
    const std::vector<int>* scale = nullptr;
    int tonic   = 60;   // pitch of degree 0
    int lowest  = 60;
    int highest = 84;
    int top     = 12;   // highest degree that still fits under `highest`

    int pitchFor (int degree) const
    {
        const int size   = static_cast<int> (scale->size());
        const int octave = static_cast<int> (std::floor (degree / static_cast<double> (size)));
        int       step   = degree - octave * size;
        if (step < 0) step += size;
        return tonic + octave * 12 + (*scale)[static_cast<size_t> (step)];
    }
};

// One note of a phrase, placed on the phrase's own grid rather than in ticks,
// so a device can be written without caring what the subdivision is.
struct SoloStep
{
    int    slot   = 0;
    int    degree = 0;
    int    length = 1;
    double accent = 0.8;
    bool   target = false;
};

// Cells worth transposing. Shapes rather than intervals: the first is a plain
// ascent, the later ones turn back on themselves, which is what stops a long
// sequence from sounding like a scale played slowly. -99 ends a short cell.
static const int kSoloCells[5][4] = {
    { 0, 1, 2, -99 },
    { 0, 1, 2, 3 },
    { 0, 2, 1, -99 },
    { 0, 1, 2, 1 },
    { 0, 2, 1, 3 },
};

// Two ways to use one vocabulary.
//
// Continuous is a solo: phrases back to back for the whole section, because
// that is what the section is for. Answering is a second guitarist behind
// somebody else's part - the same devices and the same phrasing, placed only
// where the harmony leaves room, which in practice is the end of each four-bar
// group and the run-up into whatever comes next.
//
// Sharing the generator rather than writing a second one is deliberate: a fill
// that phrases differently from the solos in the same song does not sound like
// the same player.
enum class SoloShape { Continuous, Answering };

static void generateSolo (const SectionPlan& s,
                          const std::vector<Chord>& chords,
                          int sectionStartTick,
                          int barTicks,
                          int keyPc,
                          Mode mode,
                          const std::string& style,
                          double swing,
                          const PhraseProfile* profile,
                          double humanize,
                          Rng& rng,
                          PhrasePart& out,
                          SoloShape shape = SoloShape::Continuous)
{
    if (chords.empty() || profile == nullptr || s.bars <= 0)
        return;

    const bool answering = (shape == SoloShape::Answering);

    SoloVoice voice;
    voice.scale = &soloScale (mode, style);
    if (voice.scale->empty())
        return;

    const int scaleSize = static_cast<int> (voice.scale->size());

    voice.lowest  = profile->chordLowest;
    voice.highest = profile->chordHighest;
    if (voice.highest - voice.lowest < 12)
        return;

    // Degree zero is the key's own note, taken as low in the range as it will
    // sit. A lead wants the room above it rather than below: everything here
    // climbs, and a run that starts high has nowhere to go.
    voice.tonic = ((keyPc % 12) + 12) % 12;
    while (voice.tonic < voice.lowest) voice.tonic += 12;

    voice.top = 0;
    while (voice.pitchFor (voice.top + 1) <= voice.highest) ++voice.top;
    if (voice.top < 4)
        return;   // no room to play a line in

    // The grid. Sixteenths, except under a shuffle: the swing pass drops
    // anything not sitting on a swung eighth, because a straight sixteenth is
    // not a rhythm inside a triplet feel - so a sixteenth run there would come
    // out with half its notes missing. Eighths survive it whole.
    const int slotsPerBar = (swing > 0.0) ? 8 : 16;
    const int slotTicks   = barTicks / slotsPerBar;
    if (slotTicks <= 0)
        return;

    const bool hot = s.intensity > 0.6;

    std::vector<SoloStep> steps;
    std::vector<SoloStep> motif;   // kept so a later LICK can quote it back
    int degree = voice.top / 2;

    // The last two, not just the last. Looking back one phrase let a pedal
    // figure open a solo and then close it with something else in between,
    // which reads as the same idea twice however far apart it lands.
    int lastDevice = -1, beforeThat = -1;

    for (int bar = 0; bar < s.bars; )
    {
        const int room = s.bars - bar;

        // ---- where a fill is allowed to be at all ----
        //
        // A four-bar phrase ends on its fourth bar, and that is where the
        // singer stops and the second guitar answers. Anywhere else and it is
        // playing over the part it is supposed to be supporting.
        //
        // The last bar of the section always counts, however the bars divide,
        // because the run-up into the next section is the other thing a second
        // guitarist reliably plays.
        if (answering)
        {
            const bool endOfFour  = ((bar + 1) % 4 == 0);
            const bool lastBar    = (bar + 1 == s.bars);

            if (! endOfFour && ! lastBar)
            {
                ++bar;
                continue;
            }

            // And not every one of them. A fill in all four openings is a
            // second solo; leaving some alone is what makes the ones that
            // land read as answers rather than as a part.
            if (! lastBar && ! rng.chance (0.62))
            {
                ++bar;
                continue;
            }
        }

        // Every third or fourth phrase has to breathe, or the section is one
        // unbroken run and nothing inside it registers as an idea.
        const bool mustLand = (lastDevice >= 0 && lastDevice != 4 && rng.chance (0.38));

        // A weighted draw rather than an even one, because the devices are not
        // worth the same. RUN is deliberately the rarest: it is the one with no
        // internal shape, and a solo built mostly of runs is a scale exercise
        // however fast it is played. The first pass at this drew evenly and
        // came out four runs in eight phrases with the lick never firing once.
        //
        // A quiet section trades runs and pedals for held notes; nothing else
        // about the vocabulary changes, because a slow player is not a
        // different player.
        const int weights[5] = { hot ? 12 : 6,     // run
                                 30,               // sequence
                                 hot ? 16 : 8,     // pedal
                                 26,               // lick
                                 hot ? 16 : 40 };  // land

        // A fill has one bar and somebody else's part underneath it, so it
        // wants the devices with a shape and an ending. SEQUENCE and PEDAL are
        // ideas that need room to develop; used inside a single bar they read
        // as a fragment of something rather than as a phrase. LICK and LAND
        // both start and finish where they are put.
        // RUN is cut hardest of all here. There are only three or four fills in
        // a section, so each one is heard as a statement rather than as part of
        // a stream - and a scale up the neck is the one device with no internal
        // shape, which the solo code already says about it. Three fills and all
        // three a scale ascent is what the first pass produced.
        const int fillWeights[5] = { hot ? 5 : 2,     // run
                                     8,               // sequence
                                     4,               // pedal
                                     46,              // lick
                                     37 };            // land

        const int* w = answering ? fillWeights : weights;

        const auto draw = [&]
        {
            int total = 0;
            for (int i = 0; i < 5; ++i) total += w[i];

            int pick = rng.below (total);
            for (int i = 0; i < 5; ++i)
            {
                pick -= w[i];
                if (pick < 0) return i;
            }
            return 4;
        };

        int device = mustLand ? 4 : draw();

        // Not the same device as either of the last two. Two sequences back to
        // back read as one long sequence that lost its way, and a pedal at both
        // ends of a solo reads as one idea used twice.
        for (int tries = 0; tries < 4 && (device == lastDevice || device == beforeThat); ++tries)
            device = draw();

        // Never open with the breath. A solo that begins by resting for half a
        // bar and then running up to one held note has not started yet.
        if (! answering && lastDevice < 0 && device == 4)
            device = 1;

        const int bars  = answering ? 1
                        : ((device == 4) ? 1 : ((room >= 2 && rng.chance (0.7)) ? 2 : 1));

        // A fill lives in the BACK of its bar.
        //
        // Given a whole bar it filled the whole bar - eleven notes of repeating
        // cell, which is a run wearing a fill's job. An answer comes after the
        // thing it answers: the singer or the riff has the first half, the
        // second guitar gets the last two beats. Half a bar also puts a natural
        // ceiling on the note count without capping it arbitrarily.
        const int slotOffset = answering ? slotsPerBar / 2 : 0;
        const int slots      = answering ? (slotsPerBar - slotOffset)
                                         : bars * slotsPerBar;

        steps.clear();

        switch (device)
        {
            case 0:   // RUN
            {
                int dir = rng.chance (0.5) ? 1 : -1;
                for (int i = 0; i < slots; ++i)
                {
                    steps.push_back ({ i, degree, 1, 0.70, false });
                    degree += dir;

                    // Turn round at either end rather than folding by an octave,
                    // which would break the line in the middle of a run.
                    if (degree >= voice.top) { degree = voice.top; dir = -1; }
                    if (degree <= 0)         { degree = 0;         dir =  1; }
                }
                break;
            }

            case 1:   // SEQUENCE
            {
                const int* cell = kSoloCells[rng.below (5)];
                int len = 0;
                while (len < 4 && cell[len] != -99) ++len;

                const int stepPer = rng.chance (0.5) ? 1 : -1;
                int base = degree;

                for (int i = 0; i < slots; ++i)
                {
                    const int repeat = i / len;
                    const int within = i % len;
                    int d = base + repeat * stepPer + cell[within];

                    // Fold by a whole octave when it walks off the neck, so the
                    // shape of the cell survives the move.
                    while (d > voice.top) { d -= scaleSize; base -= scaleSize; }
                    while (d < 0)         { d += scaleSize; base += scaleSize; }

                    steps.push_back ({ i, d, 1, within == 0 ? 0.82 : 0.68, false });
                    degree = d;
                }
                break;
            }

            case 2:   // PEDAL
            {
                const int pedal = std::min (voice.top, degree + 4 + rng.below (3));
                const int floor_ = std::max (0, pedal - 7);
                int under = std::max (0, pedal - 5);
                const int dir = rng.chance (0.6) ? -1 : 1;

                for (int i = 0; i < slots; ++i)
                {
                    if (i % 2 == 0)
                    {
                        steps.push_back ({ i, pedal, 1, 0.80, false });
                    }
                    else
                    {
                        steps.push_back ({ i, under, 1, 0.66, false });
                        under += dir;
                        if (under >= pedal)  under = floor_;
                        if (under <  floor_) under = std::max (floor_, pedal - 1);
                    }
                }
                degree = under;
                break;
            }

            case 3:   // LICK
            {
                if (motif.empty() || rng.chance (0.45))
                {
                    // A short burst with a hole in it, which is what makes it a
                    // motif rather than a fragment of a run.
                    motif.clear();
                    const int len = 4 + rng.below (3);
                    int d = degree;
                    for (int i = 0, slot = 0; i < len; ++i)
                    {
                        motif.push_back ({ slot, d, 1, i == 0 ? 0.86 : 0.70, false });
                        slot += (rng.chance (0.75) ? 1 : 2);
                        d    += (rng.chance (0.65) ? 1 : 2);
                        if (d > voice.top) d -= scaleSize;
                    }
                }

                const int span = motif.back().slot + 2;
                for (int rep = 0; rep * span < slots; ++rep)
                    for (const SoloStep& m : motif)
                    {
                        const int slot = rep * span + m.slot;
                        if (slot >= slots) break;
                        steps.push_back ({ slot, m.degree, m.length, m.accent, false });
                    }

                if (! steps.empty())
                    degree = steps.back().degree;
                break;
            }

            default:  // LAND
            {
                // A fast approach, then one note held. The approach is what
                // makes the held note sound arrived at rather than merely next.
                //
                // It runs up or down, and it starts at the top of the bar.
                //
                // It used to start late - as far in as three quarters of the
                // way through - so the bar opened with silence and only then
                // ran into the held note. In a solo at this tempo that is most
                // of a second of nothing, twice, and it was heard exactly that
                // way: "it just stopped for a second or two". The breath in
                // this phrase is meant to be the held note, which is a sound;
                // it was a rest, which is not.
                const int  approach = 3 + rng.below (5);
                const bool rising   = rng.chance (0.6);
                const int  start    = rng.below (2);

                int d = rising ? std::max (0, degree - approach)
                               : std::min (voice.top, degree + approach);

                for (int i = 0; i < approach; ++i)
                {
                    steps.push_back ({ start + i, std::max (0, std::min (voice.top, d)),
                                       1, 0.66, false });
                    d += rising ? 1 : -1;
                }

                d = std::max (0, std::min (voice.top, d));

                const int holdSlot = start + approach;
                steps.push_back ({ holdSlot, std::min (voice.top, d),
                                   std::max (2, slots - holdSlot), 0.95, true });
                degree = std::min (voice.top, d);
                break;
            }
        }

        // The last note of a phrase belongs to the chord underneath it. Done on
        // the degree before anything is emitted, so the line stays on one grid
        // and no pitch has to be nudged afterwards.
        if (! steps.empty())
        {
            SoloStep& landing = steps.back();
            const Chord& chord = chords[static_cast<size_t> (bar + bars - 1) % chords.size()];
            const int wanted[3] = { chord.rootPc,
                                    (chord.rootPc + std::max (0, chord.thirdSemitones())) % 12,
                                    (chord.rootPc + chord.fifthSemitones()) % 12 };

            int best = landing.degree, bestDistance = 99;
            for (int candidate = landing.degree - 2; candidate <= landing.degree + 2; ++candidate)
            {
                if (candidate < 0 || candidate > voice.top) continue;

                const int pc = ((voice.pitchFor (candidate) % 12) + 12) % 12;
                for (int w : wanted)
                    if (pc == w && std::abs (candidate - landing.degree) < bestDistance)
                    {
                        best = candidate;
                        bestDistance = std::abs (candidate - landing.degree);
                    }
            }

            landing.degree = best;
            degree = best;
        }

        // ---- emit ----
        const int barStart = sectionStartTick + bar * barTicks;

        for (const SoloStep& st : steps)
        {
            // A fill may not spill into the next bar. LAND places its held note
            // at start + approach, which can sit past the end of a short
            // phrase - harmless across a whole bar, and in answering mode it
            // put a stray note on the downbeat of the bar the fill was
            // deliberately staying out of.
            if (answering && slotOffset + st.slot >= bars * slotsPerBar)
                continue;

            const int clamped = std::max (0, std::min (voice.top, st.degree));
            const int pitch   = voice.pitchFor (clamped);
            if (pitch < voice.lowest || pitch > voice.highest)
                continue;

            LeadIntent n;
            n.tick = barStart + (slotOffset + st.slot) * slotTicks
                   + static_cast<int> (rng.bipolar (humanize * 3.0));
            n.pitch         = pitch;
            n.durationTicks = std::max (1, st.length * slotTicks);
            // A fill answers somebody; it does not compete with them. Backing
            // off the accent is what keeps it behind the rhythm guitar without
            // needing a mix move, and it is what a player does anyway - you do
            // not dig in on a two-note answer the way you do on a solo.
            n.accent        = std::min (1.0, st.accent + s.intensity * 0.12
                                                       + rng.bipolar (0.04)
                                                       - (answering ? 0.14 : 0.0));
            n.target        = st.target;

            out.lead.push_back (n);
        }

        beforeThat = lastDevice;
        lastDevice = device;
        bar += bars;

        // A whole bar off is rare now. It was one in five, and a silent bar in
        // the middle of a solo at this tempo is a long time to wait - the held
        // note at the end of a LAND is where the air is supposed to come from.
        if (! answering && device != 4 && room > bars + 2 && rng.chance (0.08))
            ++bar;
    }

    // One note at a time. The profile truncates overlaps on the way out as well,
    // but leaving them in the intents means anything reading those - the harness
    // included - sees a chord that is not being played.
    std::sort (out.lead.begin(), out.lead.end(),
               [] (const LeadIntent& a, const LeadIntent& b) { return a.tick < b.tick; });

    for (size_t i = 0; i + 1 < out.lead.size(); ++i)
    {
        const int gap = out.lead[i + 1].tick - out.lead[i].tick;
        if (gap > 0)
            out.lead[i].durationTicks = std::min (out.lead[i].durationTicks, gap);
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

        // Fills are already the supporting form of a lead line - answering in
        // the gaps IS the quiet job. Turning it into held chords would take
        // away the only thing it does.
        case PhraseFeel::Fills:   return PhraseFeel::Fills;

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
                                double swing,
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
    if (feel == PhraseFeel::Solo || feel == PhraseFeel::Fills)
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
        // Fills draw from a different salt than solos. A section that changes
        // its mind between the two would otherwise reuse the same phrase
        // choices, and the fill would be the opening of the solo it is not
        // playing.
        const bool fills = (feel == PhraseFeel::Fills);

        Rng soloRng (deriveSeed (sectionSeed, fills ? 0x5F111u : 0x50100u));
        generateSolo (s, chords, sectionStartTick, barTicks, keyPc, mode, style,
                      swing, profile, humanize, soloRng, out,
                      fills ? SoloShape::Answering : SoloShape::Continuous);
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
                                const PhraseProfile* piano,
                                const PhraseProfile* guitar2)
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

        // ---- the chordal parts: guitar, second guitar, piano ----
        // Generated per section rather than per bar: a phrase instrument is
        // told what to play once and then left alone, and even a note-driven
        // one wants a single coherent treatment across the section.
        //
        // Chordal instruments all playing a full part fight each other, so one
        // leads the section and the rest support. A supporting part is thinned
        // to long held chords and dropped an octave clear of the lead, which is
        // what a real arrangement does rather than simply turning it down.
        {
            const bool playGuitar  = (guitar  != nullptr && s.playsGuitar);
            const bool playPiano   = (piano   != nullptr && s.playsPiano);
            const bool playGuitar2 = (guitar2 != nullptr && s.playsGuitar2);

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

            // ---- the second guitar, layered on top of that decision ----
            //
            // Everything above is left exactly as it was, RNG included, so a
            // song that names no second guitar renders byte for byte as it
            // always did. The stream only moves when there is a second
            // guitarist to move it.
            bool       guitar2Explicit = false;
            PhraseFeel guitar2Feel     = PhraseFeel::Silent;
            bool       guitar2Supports = false;

            if (playGuitar2)
            {
                guitar2Feel = phraseFeelFromName (s.guitar2Phrase, guitar2Explicit);
                if (! guitar2Explicit)
                    guitar2Feel = chooseGuitarFeel (ctx, rng);
            }

            if (playGuitar2 && guitar2Feel != PhraseFeel::Silent)
            {
                const bool guitarSounds = playGuitar && guitarFeel != PhraseFeel::Silent;
                const bool pianoSounds  = playPiano  && pianoFeel  != PhraseFeel::Silent;

                // Nobody adds a second guitarist to have them play underneath
                // the first, so the newcomer fronts the section unless it is
                // told otherwise. Which one is the "lead guitar" is therefore
                // not baked into the engine at all - it is whichever the
                // section puts out front, and the same pair can swap.
                std::string leader = s.lead;
                if (leader != "guitar" && leader != "guitar2"
                    && leader != "piano" && leader != "both")
                    leader = "guitar2";

                if (leader == "both")
                {
                    guitarSupports = pianoSupports = guitar2Supports = false;
                }
                else
                {
                    guitarSupports  = guitarSounds && leader != "guitar";
                    pianoSupports   = pianoSounds  && leader != "piano";
                    guitar2Supports =                 leader != "guitar2";
                }
            }

            // How many are actually sounding, which is what decides whether
            // saying who leads means anything. A lone part reported as
            // "driving (lead)" is leading nobody.
            const int sounding = (playGuitar  && guitarFeel  != PhraseFeel::Silent ? 1 : 0)
                               + (playGuitar2 && guitar2Feel != PhraseFeel::Silent ? 1 : 0)
                               + (playPiano   && pianoFeel   != PhraseFeel::Silent ? 1 : 0);

            const auto role = [sounding] (bool supports)
            {
                return sounding < 2 ? std::string()
                                    : std::string (supports ? " (support)" : " (lead)");
            };

            const auto play = [&] (const PhraseProfile* profile, PhraseFeel feel,
                                   bool supports, PhrasePart& out,
                                   int& count, std::string& feelName)
            {
                const size_t before = out.chords.size();
                generatePhrasePart (s, chords, tick, barTicks, keyPc, mode, plan.style,
                                    plan.swing, profile,
                                    supports ? supportFeel (feel) : feel,
                                    plan.humanize, supports, throughSong,
                                    sectionSeed, plan.seed, rng, out);
                count = static_cast<int> (out.chords.size() - before);
                feelName = std::string (phraseFeelName (supports ? supportFeel (feel) : feel))
                         + (feel == PhraseFeel::Silent ? std::string() : role (supports));
            };

            if (playGuitar)
                play (guitar, guitarFeel, guitarSupports, result.performance.guitar,
                      report.guitarChords, report.guitarFeel);

            if (playGuitar2)
                play (guitar2, guitar2Feel, guitar2Supports, result.performance.guitar2,
                      report.guitar2Chords, report.guitar2Feel);

            if (playPiano)
                play (piano, pianoFeel, pianoSupports, result.performance.piano,
                      report.pianoChords, report.pianoFeel);
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
                const PhraseProfile* piano,
                const PhraseProfile* guitar2)
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
    //
    // A part counts as present if it has chords OR a lead line. Testing chords
    // alone dropped any part that only ever soloed - and a soloing part has no
    // chords by design, because one player cannot comp and solo at once. A plan
    // whose guitar solos the whole way through wrote no guitar track at all.
    const auto emit = [&mf, &perf] (const PhraseProfile* profile, const PhrasePart& part)
    {
        if (profile == nullptr || (part.chords.empty() && part.lead.empty()))
            return;

        MidiTrack t;
        t.name = profile->name;
        profile->render (part, t);
        mf.tracks.push_back (t);
    };

    emit (guitar,  perf.guitar);
    emit (guitar2, perf.guitar2);
    emit (piano,   perf.piano);

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
