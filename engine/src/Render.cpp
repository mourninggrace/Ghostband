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

//==============================================================================
//==============================================================================
// THE FILLS, AS A LEAD PLAYER PLAYS THEM.
//
// 2026-09-26, after a rework that measured well and sounded like nothing had
// changed: "the fills still consist of single notes hit with a second in
// between, no sustain, climbing up and down". Measured, he was right: the BED -
// a soft chord arpeggio in half notes and quarters, added to cure silence - was
// 85% of the notes and 84% of the time in every fills section; the answers the
// rework changed were 11%. And the instrument was switched to its Polyphonic
// fretting mode for fills, which forbids legato outright.
//
// The brief: Steve Vai, Eddie Van Halen, Kirk Hammett, Joe Satriani - combined
// into a voice Ghostband makes its own, with every articulation the instrument
// has. So a fills section is now built from LICKS and SUSTAIN, not from a bed:
//
//   licks    tapped triplet arpeggios and hammer-on pentatonic cascades (Van
//            Halen); three-note-per-string legato runs, pedal-point legato and
//            singing bends (Satriani); wide legato leaps, vocal bend-and-return,
//            ringing harmonics, chromatic climbs (Vai); pentatonic triplet box
//            runs, trills, repeated blues licks into a bend, pinch squeals,
//            rakes and chokes (Hammett). Each is a shape, not a recording: it is
//            built fresh from the song's key and the chord it lands on, and
//            varied every time it is played.
//   sustain  between licks the guitar SINGS: a chord tone held for a bar or
//            more, bent into and shaken - the Satriani/Vai sustain - or it rests.
//
// Each song draws a personality: which of these players it leans on, how often
// it answers, how much it sustains. A new seed, a new instance or a change of
// song is a different player. Solos are NOT built here and are untouched.
namespace fillvoice
{
    enum Space { Scale, Pent, ChordTones, Semis, MajPent, Harm, Dim, Exotic };

    enum Flag : unsigned
    {
        Slur = 1u, Vib = 2u, Target = 4u, Bend1 = 8u, Bend2 = 16u,
        Tap = 32u, Pinch = 64u, HarmF = 128u, Rake = 256u, Choke = 512u,
        // How a bend moves and how wide the shake is (2026-09-27): bend and
        // let down, struck already bent, a slide in, the B.B. King vibrato.
        Release = 1024u, PreBend = 2048u, Scoop = 4096u, WideVib = 8192u,
        // Palm-muted, clipped short, tremolo-picked (2026-09-27: "every
        // articulation used").
        Mute = 16384u, Stacc = 32768u, Trem = 65536u,
        // Slide, still sounding, into the next note (Hydra's Legato Slide).
        Glide = 131072u
    };

    // One note of a lick: when, how long, which pitch (a step in a space,
    // relative to the lick's anchor), and how it is played.
    struct LickNote { int on, len; Space sp; int st; unsigned f; };

    // A lick note's bend shape and vibrato, onto the note that plays it.
    static void expression (LeadIntent& li, unsigned f)
    {
        using BS = LeadIntent::BendShape;
        if (f & Scoop)   { li.bendShape = BS::Scoop; if (li.bendSemis == 0) li.bendSemis = 1; }
        if (f & Release) li.bendShape = BS::Release;
        if (f & PreBend) li.bendShape = BS::PreBend;
        li.wideVibrato = (f & WideVib) != 0;
    }

    // Every ladder a lick can climb, for one song's key. The new ones
    // (2026-09-27) are what the owner's list of solos is made of:
    //   majPent  the major pentatonic a blues player slides into over the one
    //            chord (Clapton, B.B., Mayer); in a minor song that is not a
    //            blues, the relative major's - the same notes as the minor's
    //   harm     harmonic minor, the neoclassical scale (Malmsteen, Rhoads,
    //            Blackmore); a major key keeps its own scale
    //   dim      the diminished seventh on the leading tone
    //   exotic   phrygian - the dark flat second of thrash (Friedman, Hammett)
    struct KeyLadders { std::vector<int> scale, pent, majPent, harm, dim, exotic; };

    static std::vector<int> ladderOf (std::initializer_list<int> ivs, int keyPc, int lowest, int highest);

    static const std::vector<int>* ladderFor (Space sp, const KeyLadders& k, const std::vector<int>& tones)
    {
        switch (sp)
        {
            case Scale:      return &k.scale;
            case Pent:       return &k.pent;
            case ChordTones: return &tones;
            case MajPent:    return &k.majPent;
            case Harm:       return &k.harm;
            case Dim:        return &k.dim;
            case Exotic:     return &k.exotic;
            case Semis:      break;
        }
        return nullptr;
    }

    enum Family { VanHalen, Satriani, Vai, Hammett, Anyone, kFamilies };

    struct Lick
    {
        std::vector<LickNote> notes;
        Family family = Anyone;
        bool   triplet = false;     // off the straight grid: not under a shuffle
        bool   fast    = false;     // wants a hot section
        bool   twin    = false;     // a line two guitars harmonise
        int    span() const
        {
            int e = 0;
            for (const LickNote& n : notes) e = std::max (e, n.on + n.len);
            return e;
        }
    };

    //--------------------------------------------------------------------------
    // The builders. Each returns a lick that ends on step 0 of its space - the
    // anchor, which is always a tone of the chord it lands on.

    static Lick tapArpeggio (Rng& r)           // Van Halen: tap, pull, hammer
    {
        Lick L; L.family = VanHalen; L.triplet = true; L.fast = true;
        const int hi = r.range (3, 4), mid = r.range (1, 2);
        const int groups = r.range (3, 6);
        int t = 0;
        for (int g = 0; g < groups; ++g)
        {
            L.notes.push_back ({ t,       80, ChordTones, hi,  Tap | Slur });
            L.notes.push_back ({ t + 80,  80, ChordTones, 0,   Tap | Slur });
            L.notes.push_back ({ t + 160, 80, ChordTones, mid, Tap | Slur });
            t += 240;
        }
        L.notes.push_back ({ t, 480, ChordTones, 0, Target | Vib });
        return L;
    }

    static Lick hammerCascade (Rng& r)         // Van Halen: pentatonic hammer/pull triplets down
    {
        Lick L; L.family = VanHalen; L.triplet = true; L.fast = true;
        const int groups = r.range (3, 5);
        int t = 0;
        for (int g = 0; g < groups; ++g)
        {
            const int top = groups - g + 1;
            L.notes.push_back ({ t,       80, Pent, top,     Slur });
            L.notes.push_back ({ t + 80,  80, Pent, top - 1, Slur });
            L.notes.push_back ({ t + 160, 80, Pent, top - 2, Slur });
            t += 240;
        }
        L.notes.push_back ({ t, 600, Pent, 0, Target | Vib });
        return L;
    }

    static Lick rakeIntoBend (Rng& r)          // Van Halen / Hammett
    {
        Lick L; L.family = r.chance (0.5) ? VanHalen : Hammett;
        if (r.chance (0.6)) L.notes.push_back ({ 0, 120, Scale, -1, 0 });
        const int at = L.notes.empty() ? 0 : 120;
        L.notes.push_back ({ at, r.range (600, 900), Scale, 0, Rake | Bend2 | Vib });
        return L;
    }

    static Lick pinchSqueal (Rng& r)           // Hammett / Van Halen
    {
        Lick L; L.family = r.chance (0.6) ? Hammett : VanHalen;
        const int n = r.range (1, 3);
        for (int i = 0; i < n; ++i)
            L.notes.push_back ({ i * 120, 120, Scale, i - n, 0 });
        L.notes.push_back ({ n * 120, r.range (480, 840), Scale, 0, Pinch | Vib });
        return L;
    }

    static Lick legatoRun (Rng& r)             // Satriani: three notes a string, one breath
    {
        Lick L; L.family = Satriani; L.fast = true;
        const int n = r.range (6, 10);
        const bool up = r.chance (0.7);
        for (int i = 0; i < n; ++i)
            L.notes.push_back ({ i * 120, 120, Scale, up ? i - n : n - i, Slur });
        L.notes.push_back ({ n * 120, 600, Scale, 0, Target | Vib });
        return L;
    }

    static Lick pedalLegato (Rng& r)           // Satriani: a pedal note above a moving line
    {
        Lick L; L.family = Satriani;
        const int m = r.range (3, 4);
        int t = 0;
        for (int k = m; k >= 1; --k)
        {
            L.notes.push_back ({ t,       120, Scale, 0,  Slur });
            L.notes.push_back ({ t + 120, 120, Scale, -k, Slur });
            t += 240;
        }
        L.notes.push_back ({ t, 480, Scale, 0, Target | Vib });
        return L;
    }

    static Lick singingBend (Rng& r)           // Satriani / Vai: the note that sings
    {
        Lick L; L.family = r.chance (0.5) ? Satriani : Vai;
        const int pick = r.range (1, 2);
        L.notes.push_back ({ 0, pick == 1 ? 240 : 120, Scale, -pick, 0 });
        L.notes.push_back ({ pick == 1 ? 240 : 120, r.range (720, 1200), Scale, 0, Bend2 | Vib | Target });
        return L;
    }

    static Lick descendingFours (Rng& r)       // Satriani: a sequence of four, falling
    {
        Lick L; L.family = Satriani; L.fast = true;
        const int groups = r.range (2, 3);
        int t = 0;
        for (int g = 0; g < groups; ++g)
        {
            const int base = groups - 1 - g;
            for (int k = 3; k >= 0; --k)
            {
                L.notes.push_back ({ t, 120, Scale, base + k - 1, Slur });
                t += 120;
            }
        }
        L.notes.push_back ({ t, 480, Scale, 0, Target | Vib });
        return L;
    }

    static Lick wideLeaps (Rng& r)             // Vai: legato across wide intervals
    {
        Lick L; L.family = Vai; L.triplet = true;
        static const int shapeA[] = { 0, 3, 1, 4, 2, 5 };
        static const int shapeB[] = { 5, 2, 4, 1, 3, 0 };
        const int* shape = r.chance (0.5) ? shapeA : shapeB;
        const int n = r.range (4, 6);
        const int last = shape[n - 1];
        for (int i = 0; i < n; ++i)
            L.notes.push_back ({ i * 160, 160, Pent, shape[i] - last + (i == n - 1 ? 0 : 0), Slur });
        L.notes.back().f = Target | Vib;
        L.notes.back().len = 640;
        return L;
    }

    static Lick bendAndReturn (Rng& r)         // Vai: bend, fall back, sing
    {
        Lick L; L.family = Vai;
        L.notes.push_back ({ 0,   360, Scale, 0,  Bend2 });
        L.notes.push_back ({ 360, 120, Scale, -1, Slur });
        L.notes.push_back ({ 480, r.range (480, 900), Scale, 0, Target | Vib });
        return L;
    }

    static Lick harmonics (Rng& r)             // Vai / Satriani: bells high on the neck
    {
        Lick L; L.family = r.chance (0.6) ? Vai : Satriani;
        const int n = r.range (2, 3);
        for (int i = 0; i < n; ++i)
            L.notes.push_back ({ i * 480, 480, ChordTones, n - 1 - i, HarmF });
        return L;
    }

    static Lick chromaticClimb (Rng& r)        // Vai: in from outside the key
    {
        Lick L; L.family = Vai;
        const int n = r.range (2, 4);
        for (int i = 0; i < n; ++i)
            L.notes.push_back ({ i * 120, 120, Semis, i - n, Slur });
        L.notes.push_back ({ n * 120, 600, Semis, 0, Target | Vib });
        return L;
    }

    static Lick tripletBox (Rng& r)            // Hammett: pentatonic triplets down the box
    {
        Lick L; L.family = Hammett; L.triplet = true; L.fast = true;
        const int groups = r.range (3, 4);
        int t = 0;
        for (int g = 0; g < groups; ++g)
        {
            const int top = groups - g + 1;
            for (int k = 0; k < 3; ++k)
            {
                L.notes.push_back ({ t, 160, Pent, top - k, 0 });
                t += 160;
            }
        }
        L.notes.push_back ({ t, 480, Pent, 0, Vib });
        return L;
    }

    static Lick trill (Rng& r)                 // Hammett / Van Halen
    {
        Lick L; L.family = r.chance (0.6) ? Hammett : VanHalen; L.triplet = true; L.fast = true;
        const int n = r.range (8, 12);
        const int upper = r.range (1, 2);
        for (int i = 0; i < n; ++i)
            L.notes.push_back ({ i * 80, 80, Scale, (i % 2 == 0) ? 0 : upper, Slur });
        L.notes.push_back ({ n * 80, 480, Scale, 0, Target | Vib });
        return L;
    }

    static Lick bluesRepeat (Rng& r)           // Hammett: say it three times, then lean on it
    {
        Lick L; L.family = Hammett;
        const int reps = r.range (2, 3);
        int t = 0;
        for (int k = 0; k < reps; ++k)
        {
            L.notes.push_back ({ t,       120, Pent, 2, Bend1 });
            L.notes.push_back ({ t + 120, 120, Pent, 1, Slur });
            L.notes.push_back ({ t + 240, 120, Pent, 0, Stacc });
            t += 360;
        }
        L.notes.push_back ({ t, 600, Pent, 0, Bend2 | Vib | Target });
        return L;
    }

    static Lick chokeStabs (Rng& r)            // Hammett: rhythm in the lead
    {
        Lick L; L.family = Hammett; L.fast = true;
        const int n = r.range (2, 3);
        for (int i = 0; i < n; ++i)
            L.notes.push_back ({ i * 240, 120, Scale, 0, Choke });
        L.notes.push_back ({ n * 240, 480, Scale, 0, Pinch | Vib });
        return L;
    }

    static Lick sweep (Rng& r)                 // Vai / Satriani: the chord, up and back
    {
        Lick L; L.family = r.chance (0.5) ? Vai : Satriani; L.triplet = true; L.fast = true;
        const int top = r.range (3, 4);
        int t = 0;
        for (int k = 0; k <= top; ++k) { L.notes.push_back ({ t, 80, ChordTones, k, Slur }); t += 80; }
        for (int k = top - 1; k >= 0; --k) { L.notes.push_back ({ t, 80, ChordTones, k, Slur }); t += 80; }
        L.notes.back().len = 480;
        L.notes.back().f = Target | Vib;
        return L;
    }

    static Lick pickupCall (Rng& r)            // anyone: two notes and a held one
    {
        Lick L; L.family = Anyone;
        const bool below = r.chance (0.6);
        L.notes.push_back ({ 0,   120, Scale, below ? -2 : 2, 0 });
        L.notes.push_back ({ 120, 120, Scale, below ? -1 : 1, Slur });
        L.notes.push_back ({ 240, 480, Scale, 0, Target | Vib });
        return L;
    }

    static Lick flurry (Rng& r)                // Van Halen / Satriani: a sextuplet burst up
    {
        Lick L; L.family = r.chance (0.5) ? VanHalen : Satriani; L.triplet = true; L.fast = true;
        const int n = r.range (6, 9);
        for (int i = 0; i < n; ++i)
            L.notes.push_back ({ i * 80, 80, Pent, i - n, Slur });
        L.notes.push_back ({ n * 80, 480, Pent, 0, Target | Vib | Bend1 });
        return L;
    }

    using Builder = Lick (*) (Rng&);
    static const Builder kBuilders[] = {
        tapArpeggio, hammerCascade, rakeIntoBend, pinchSqueal, legatoRun, pedalLegato,
        singingBend, descendingFours, wideLeaps, bendAndReturn, harmonics, chromaticClimb,
        tripletBox, trill, bluesRepeat, chokeStabs, sweep, pickupCall, flurry
    };
    static constexpr int kNumLicks = static_cast<int> (sizeof (kBuilders) / sizeof (kBuilders[0]));

    // Which family each builder mostly speaks for, for the personality.
    static const Family kLickFamily[kNumLicks] = {
        VanHalen, VanHalen, VanHalen, Hammett, Satriani, Satriani,
        Satriani, Satriani, Vai, Vai, Vai, Vai,
        Hammett, Hammett, Hammett, Hammett, Vai, Anyone, VanHalen
    };

    struct Personality
    {
        double family[kFamilies] {};
        double lick[kNumLicks] {};
        double answerSecond = 0.4;   // how often bar 2 of a group answers too
        double sustain      = 0.6;   // how often a free bar sings rather than rests
        double invert       = 0.3;   // how often a lick is turned upside down
        double feel[5]      {};      // straight, sextuplet rush, dotted, held note, breath
        double landing[5]   {};      // landing length: x0.5, x0.75, x1, x1.5, x2
    };

    // A player made of four: each song weights the four families differently
    // (one song is mostly Satriani with a little Van Halen, the next mostly
    // Hammett), and within a family favours some licks over others.
    static Personality personalityFor (uint32_t songSeed)
    {
        Rng r (deriveSeed (songSeed, 0xB1A5E5u));
        Personality p;
        for (double& f : p.family) f = 0.15 + r.unit() * r.unit() * 3.0;
        p.family[Anyone] = 0.6;
        for (int i = 0; i < kNumLicks; ++i)
            p.lick[i] = p.family[kLickFamily[i]] * (0.2 + r.unit() * 1.8);
        p.answerSecond = 0.20 + r.unit() * 0.45;
        p.sustain      = 0.35 + r.unit() * 0.55;
        p.invert       = 0.10 + r.unit() * 0.35;
        // How this player phrases time: one rushes runs into the landing, one
        // plays them dotted, one holds a note mid-run, one breathes. Squared,
        // so most songs lean clearly on one or two habits.
        for (double& f : p.feel)    f = 0.05 + r.unit() * r.unit() * 2.0;
        for (double& f : p.landing) f = 0.05 + r.unit() * r.unit() * 2.0;
        return p;
    }

    static int pickWeighted (Rng& r, const double* w, int n)
    {
        double total = 0.0;
        for (int i = 0; i < n; ++i) total += w[i];
        double pick = r.unit() * total;
        for (int i = 0; i < n; ++i)
            if ((pick -= w[i]) < 0.0) return i;
        return n - 1;
    }

    // THE SAME LICK NEVER PLAYED THE SAME WAY TWICE. The builders give a lick's
    // shape; this gives one playing of it its time: the run straight, rushed
    // as sextuplets, dotted long-short, with a note leaned on, or with a breath
    // in it - and a landing that is clipped, held, or left to ring. The first
    // version ended every lick in a straight run into a quarter-note landing,
    // and a quarter of all fills in the set shared three rhythms.
    static void vary (Lick& L, Rng& r, const Personality& who, bool shuffle)
    {
        const size_t n = L.notes.size();
        if (n == 0) return;

        if (n >= 4 && ! L.triplet)
        {
            std::vector<int>  ioi (n - 1);
            std::vector<bool> tied (n - 1), gap (n - 1, false);
            for (size_t i = 0; i + 1 < n; ++i)
            {
                ioi[i]  = L.notes[i + 1].on - L.notes[i].on;
                tied[i] = L.notes[i].len >= ioi[i];
            }
            switch (pickWeighted (r, who.feel, 5))
            {
                case 1:   // sextuplets: the run rushes into the landing
                    if (! shuffle)
                        for (int& d : ioi) if (d == 120) d = 80;
                    break;
                case 2:   // dotted: long-short, long-short (not under a shuffle: off its grid)
                {
                    if (shuffle) break;
                    bool longOne = r.chance (0.7);
                    for (size_t i = 0; i + 2 < n; ++i)
                        if (ioi[i] == 120) { ioi[i] = longOne ? 180 : 60; longOne = ! longOne; }
                    break;
                }
                case 3:   // one note leaned on, held twice as long
                {
                    const size_t k = static_cast<size_t> (r.range (0, static_cast<int> (n) - 3));
                    ioi[k] *= 2; tied[k] = true;
                    break;
                }
                case 4:   // a breath: a sixteenth of silence mid-run
                {
                    const size_t k = static_cast<size_t> (r.range (1, static_cast<int> (n) - 3));
                    ioi[k] += 120; gap[k] = true;
                    break;
                }
                default: break;
            }
            int t = L.notes[0].on;
            for (size_t i = 0; i < n; ++i)
            {
                L.notes[i].on = t;
                if (i + 1 < n)
                {
                    L.notes[i].len = gap[i]  ? std::min (L.notes[i].len, ioi[i] - 120)
                                   : tied[i] ? ioi[i]
                                             : std::min (L.notes[i].len, ioi[i]);
                    L.notes[i].len = std::max (30, L.notes[i].len);
                    t += ioi[i];
                }
            }
        }

        static constexpr double kLand[5] = { 0.5, 0.75, 1.0, 1.5, 2.0 };
        LickNote& last = L.notes.back();
        const int landed = static_cast<int> (last.len * kLand[pickWeighted (r, who.landing, 5)]);
        last.len = std::max (120, ((landed + 30) / 60) * 60);
    }

    // Every pitch of a pitch-class set between two notes, ascending.
    static std::vector<int> ladder (const std::vector<int>& pcs, int lowest, int highest)
    {
        std::vector<int> out;
        for (int p = lowest; p <= highest; ++p)
            for (int pc : pcs)
                if (((p % 12) + 12) % 12 == pc) { out.push_back (p); break; }
        return out;
    }

    static std::vector<int> ladderOf (std::initializer_list<int> ivs, int keyPc, int lowest, int highest)
    {
        std::vector<int> pcs;
        for (int iv : ivs) pcs.push_back ((keyPc + iv) % 12);
        return ladder (pcs, lowest, highest);
    }

    static KeyLadders makeLadders (const std::vector<int>& scale, int keyPc, bool blues, int lowest, int highest)
    {
        KeyLadders k;
        const bool minorish = std::find (scale.begin(), scale.end(), 3) != scale.end();
        std::vector<int> sc;
        for (int iv : scale) sc.push_back ((keyPc + iv) % 12);
        k.scale   = ladder (sc, lowest, highest);
        k.pent    = minorish ? ladderOf ({ 0, 3, 5, 7, 10 }, keyPc, lowest, highest)
                             : ladderOf ({ 0, 2, 4, 7, 9 },  keyPc, lowest, highest);
        k.majPent = (blues || ! minorish) ? ladderOf ({ 0, 2, 4, 7, 9 }, keyPc, lowest, highest) : k.pent;
        k.harm    = minorish ? ladderOf ({ 0, 2, 3, 5, 7, 8, 11 }, keyPc, lowest, highest) : k.scale;
        k.dim     = ladderOf ({ 2, 5, 8, 11 }, keyPc, lowest, highest);
        k.exotic  = minorish ? ladderOf ({ 0, 1, 3, 5, 7, 8, 10 }, keyPc, lowest, highest) : k.scale;
        return k;
    }

    static int nearestIndex (const std::vector<int>& v, int pitch)
    {
        int best = 0, dist = 1000;
        for (int i = 0; i < static_cast<int> (v.size()); ++i)
            if (std::abs (v[static_cast<size_t> (i)] - pitch) < dist)
            { dist = std::abs (v[static_cast<size_t> (i)] - pitch); best = i; }
        return best;
    }
}

//==============================================================================
// SOLOS, REBUILT (2026-09-26). The owner: "they sound empty, intermediate and
// as if to almost be hunting for notes to hit rather than with mindful intent".
// Measured before this: 41% of every move a single scale step, 21% of all solo
// notes inside a one-way run of four or more steps, a third of the time on a
// note long enough to sing. The old generator walked a scale degree at a time.
//
// A solo here is a STORY in two-bar phrases, each with a job:
//
//   statement    this solo's own MOTIF - a short vocal idea, a real rhythm,
//                at least one leap, a bent or shaken long note - said, then
//                said again over the next chord (it re-fits itself to it)
//   development  the vocabulary Ghostband's fills speak - singing bends,
//                bend-and-return, repeated blues licks, wide Vai leaps,
//                harmonics, pedal legato - and once, the motif comes back
//   build        faster and higher: legato runs, triplet boxes, sweeps,
//                trills, tapping, choke stabs - two ideas to a phrase
//   climax       the top of the solo: tapping, a pinch squeal, a flurry into
//                a screaming bend
//   resolution   home: the chord's root, held, shaken, to the end
//
// Every phrase LANDS on a tone of the chord under it, on a strong beat, and
// that landing is HELD with vibrato until a breath before the next phrase. The
// register climbs across the solo. Space before a phrase is filled with a sung
// note, never left empty. Every articulation Shreddage has is in play.
//
// The musical choices are named in SoloStyle so they can become controls.
namespace fillvoice
{
    struct SoloStyle
    {
        double shred      = 0.5;   // 0 = sings, 1 = shreds: how much of the build is fast
        double gestures   = 1.0;   // how freely pinches, taps, harmonics, rakes, chokes are used
        double motifReturn= 0.6;   // how likely the opening idea comes back mid-solo
        double climb      = 0.45;  // how far the register rises from start to climax (share of range)
        double busy       = 0.5;   // how much of each phrase is playing rather than holding
    };

    static Lick pentBends (Rng& r)             // Hammett / Page: the box, leaned on
    {
        Lick L; L.family = Hammett;
        int t = 0;
        L.notes.push_back ({ t, 360, Pent,  2, Bend2 });  t += 360;
        L.notes.push_back ({ t, 120, Pent,  1, Stacc });  t += 120;
        L.notes.push_back ({ t, 240, Pent, -1, 0 });      t += 240;
        if (r.chance (0.5)) { L.notes.push_back ({ t, 240, Pent, 1, Bend1 }); t += 240; }
        L.notes.push_back ({ t, 720, Pent, 0, Target | Vib });
        return L;
    }

    static Lick arpSkip (Rng& r)               // Vai / Satriani: the chord, skipped through
    {
        Lick L; L.family = r.chance (0.5) ? Vai : Satriani;
        static const int shape[] = { 2, 0, 3, 1, 4, 2, 3 };
        const int n   = r.range (3, 5);
        const int off = r.range (0, 2);
        const int dur = r.chance (0.5) ? 240 : 120;
        int t = 0;
        for (int i = 0; i < n; ++i) { L.notes.push_back ({ t, dur, ChordTones, shape[i + off] - 1, r.chance (0.3) ? Slur : 0u }); t += dur; }
        L.notes.push_back ({ t, 600, ChordTones, 0, Target | Vib });
        return L;
    }

    static Lick octaveCall (Rng& r)            // anyone: a cry from an octave up, falling home
    {
        Lick L; L.family = Anyone;
        L.notes.push_back ({ 0,   r.chance (0.5) ? 360 : 240, Pent, 5, Bend2 | Vib });
        const int at = L.notes.back().len;
        L.notes.push_back ({ at,       120, Pent, 3, 0 });
        L.notes.push_back ({ at + 120, 120, Pent, 2, Slur });
        L.notes.push_back ({ at + 240, 720, Pent, 0, Target | Vib });
        return L;
    }

    // A straight lick in a shuffle's time: every gap between onsets rounded to
    // whole swung eighths, at least one. Doubling the whole lick - what the
    // fills do - also turned its quarter notes into halves, and a blues solo
    // came out at three notes a bar.
    static void toSwungEighths (Lick& L)
    {
        const size_t n = L.notes.size();
        if (n == 0) return;
        int t = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const int ioi  = (i + 1 < n) ? L.notes[i + 1].on - L.notes[i].on : 0;
            const bool tied = (i + 1 < n) && L.notes[i].len >= ioi;
            const int ioi2 = (i + 1 < n) ? std::max (240, ((ioi + 120) / 240) * 240) : 0;
            L.notes[i].on = t;
            if (i + 1 < n)
                L.notes[i].len = tied ? ioi2 : std::min (std::max (120, L.notes[i].len), ioi2);
            t += ioi2;
        }
    }

    // BUSY steers WHICH licks, not only how many: up favours the ones with a
    // lot to say, down the ones with a few notes and room. A trim of zero is 1.
    static double busyWeight (const Lick& L, double busyTrim)
    {
        if (busyTrim == 0.0) return 1.0;
        const double n = std::max (1.0, static_cast<double> (L.notes.size()));
        return std::pow (n / 5.0, busyTrim * (busyTrim > 0.0 ? 2.4 : 1.6));
    }

    enum SoloRole { Develop = 1, Build = 2, Climax = 4 };

    //--------------------------------------------------------------------------
    // THE SCHOOLS (2026-09-27). The owner named the solos he wants Ghostband to
    // play like - One, Beyond the Realms of Death, Rising Force, Aces High,
    // Highway Star, Crossroads, Tornado of Souls, Floods, Sweet Child o' Mine,
    // Mr. Crowley, Bohemian Rhapsody, Voodoo Child, Stairway, and B.B. King
    // trading with John Mayer. None of them is copied here - a copied lick is
    // canned and not Ghostband's to play. What is taken is what makes each
    // school sound like itself, and every line is built fresh from that:
    //
    //   Blues         bends let back down, pre-bent cries, the box above the
    //                 root, major pentatonic slid into, repeated triplet
    //                 licks, a wide slow vibrato and room to breathe
    //   Melodic       a theme sequenced up the neck, slides into singing
    //                 notes, a climb to a peak
    //   Neoclassical  harmonic minor, pedal-point sixes, diminished sweeps
    //   NWOBHM        gallops, melodic themes (and one day twin harmonies)
    //   Thrash        tremolo bursts, the phrygian flat second, chromatic
    //                 menace, exotic legato, pinch squeals
    enum School { Blues, Melodic, Neo, Nwobhm, Thrash, kSchools };
    enum : unsigned { SBlues = 1u, SMelodic = 2u, SNeo = 4u, SNwobhm = 8u, SThrash = 16u, SAll = 31u };

    // ---- blues
    static Lick bbCry (Rng& r)                 // struck already bent, let down, sung
    {
        Lick L; L.family = Anyone;
        L.notes.push_back ({ 0,    r.chance (0.5) ? 480 : 360, Pent, 1, Bend2 | PreBend | WideVib });
        const int t = L.notes.back().len;
        L.notes.push_back ({ t,       240, Pent,  0, 0 });
        L.notes.push_back ({ t + 240, 240, Pent, -1, Slur });
        L.notes.push_back ({ t + 480, 840, Pent,  0, Target | WideVib });
        return L;
    }
    static Lick bendRelease (Rng& r)           // up, sing, let it down, walk home
    {
        Lick L; L.family = Anyone;
        L.notes.push_back ({ 0,    r.chance (0.5) ? 720 : 480, Pent, 1, Bend2 | Release | Vib });
        const int t = L.notes.back().len;
        L.notes.push_back ({ t,       240, Pent,  0, 0 });
        L.notes.push_back ({ t + 240, 240, Pent, -1, Slur });
        L.notes.push_back ({ t + 480, 720, Pent,  0, Target | WideVib });
        return L;
    }
    static Lick bbBox (Rng& r)                 // the notes above the root, leaned on
    {
        Lick L; L.family = Anyone;
        L.notes.push_back ({ 0,   240, Scale, 2, 0 });
        L.notes.push_back ({ 240, 480, Scale, 3, Bend2 | Release });
        L.notes.push_back ({ 720, 240, Scale, 1, r.chance (0.5) ? Scoop : 0u });
        L.notes.push_back ({ 960, 960, Scale, 0, Target | WideVib });
        return L;
    }
    static Lick majPentRun (Rng& r)            // sweet side of the blues, down in triplets
    {
        Lick L; L.family = Anyone; L.triplet = true;
        const int n = r.range (4, 6);
        for (int i = 0; i < n; ++i) L.notes.push_back ({ i * 160, 160, MajPent, n - i, i % 2 ? Slur : 0u });
        L.notes.push_back ({ n * 160, 640, MajPent, 0, Target | Scoop | Vib });
        return L;
    }
    static Lick bluesTriplets (Rng& r)         // one triplet cell, said until it burns
    {
        Lick L; L.family = Anyone; L.triplet = true; L.fast = true;
        const int reps = r.range (3, 4);
        int t = 0;
        for (int k = 0; k < reps; ++k)
        {
            L.notes.push_back ({ t,       160, Pent, 2, k == 0 ? Bend1 : 0u });
            L.notes.push_back ({ t + 160, 160, Pent, 1, Slur });
            L.notes.push_back ({ t + 320, 160, Pent, 0, 0 });
            t += 480;
        }
        L.notes.push_back ({ t, 720, Pent, 1, Bend2 | Release | WideVib | Target });
        L.notes.back().st = 0;
        return L;
    }
    // ---- melodic rock
    static Lick themeSequence (Rng& r)         // a little shape, walked up the neck
    {
        Lick L; L.family = Anyone; L.twin = true;
        static const int cellA[3] = { 0, 2, 1 };
        static const int cellB[3] = { 0, -1, 1 };
        const int* cell = r.chance (0.5) ? cellA : cellB;
        const int reps = r.range (3, 4);
        int t = 0;
        for (int k = 0; k < reps; ++k)
            for (int j = 0; j < 3; ++j) { L.notes.push_back ({ t, 240, Scale, k - reps + cell[j], j == 2 ? Slur : 0u }); t += 240; }
        L.notes.push_back ({ t, 720, Scale, 0, Target | Bend2 | Vib });
        return L;
    }
    static Lick slideSinger (Rng&)             // slide into it and let it sing
    {
        Lick L; L.family = Anyone;
        L.notes.push_back ({ 0,    240, Scale, -2, 0 });
        L.notes.push_back ({ 240,  720, Scale,  1, Scoop | Vib });
        L.notes.push_back ({ 960,  240, Scale, -1, 0 });
        L.notes.push_back ({ 1200, 960, Scale,  0, Target | Scoop | WideVib });
        return L;
    }
    static Lick climbToPeak (Rng& r)           // up in thirds and back a step, to the top
    {
        Lick L; L.family = Anyone;
        const int n = r.range (6, 8);
        for (int i = 0; i < n; ++i) L.notes.push_back ({ i * 240, 240, Scale, -n + (i / 2) * 2 + (i % 2 ? -1 : 1), 0 });
        L.notes.push_back ({ n * 240, 960, Scale, 0, Target | Bend2 | WideVib });
        return L;
    }
    // ---- neoclassical
    static Lick pedalSixes (Rng& r)            // a pedal note on top, the line falling under it
    {
        Lick L; L.family = Anyone; L.triplet = true; L.fast = true;
        const int n = r.range (5, 7);
        int t = 0;
        for (int k = 1; k <= n; ++k)
        {
            L.notes.push_back ({ t,      80, Harm,  0, Slur });
            L.notes.push_back ({ t + 80, 80, Harm, -k, Slur });
            t += 160;
        }
        L.notes.push_back ({ t, 480, Harm, 0, Target | Vib });
        return L;
    }
    static Lick dimSweep (Rng&)                // the diminished chord, swept up and back
    {
        Lick L; L.family = Anyone; L.triplet = true; L.fast = true;
        int t = 0;
        for (int k = -5; k <= -1; ++k) { L.notes.push_back ({ t, 80, Dim, k, Slur }); t += 80; }
        for (int k = -2; k >= -4; --k) { L.notes.push_back ({ t, 80, Dim, k, Slur }); t += 80; }
        L.notes.push_back ({ t, 560, ChordTones, 0, Target | Vib });
        return L;
    }
    static Lick harmSixes (Rng& r)             // harmonic minor in groups of six, falling
    {
        Lick L; L.family = Anyone; L.triplet = true; L.fast = true;
        static const int six[6] = { 0, -1, -2, -1, -2, -3 };
        const int groups = r.range (2, 3);
        int t = 0;
        for (int g = 0; g < groups; ++g)
            for (int j = 0; j < 6; ++j) { L.notes.push_back ({ t, 80, Harm, (groups - g) * 2 + six[j], 0 }); t += 80; }
        L.notes.push_back ({ t, 560, Harm, 0, Target | Vib });
        return L;
    }
    // ---- NWOBHM
    static Lick gallopRun (Rng& r)             // eighth, two sixteenths - the gallop, climbing
    {
        Lick L; L.family = Anyone; L.twin = true;
        const int n = r.range (3, 4);
        int t = 0;
        for (int k = 0; k < n; ++k)
        {
            const int base = -2 * n + 2 * k;
            // The gallop is palm-muted, the way a twin-guitar band plays it.
            L.notes.push_back ({ t,       240, Scale, base,     Mute });
            L.notes.push_back ({ t + 240, 120, Scale, base + 1, Mute });
            L.notes.push_back ({ t + 360, 120, Scale, base + 2, Mute });
            t += 480;
        }
        L.notes.push_back ({ t, 720, Scale, 0, Target | Bend2 | Vib });
        return L;
    }
    static Lick twinTheme (Rng& r)             // a sung theme, the kind two guitars harmonise
    {
        Lick L; L.family = Anyone; L.twin = true;
        const bool up = r.chance (0.5);
        // The theme's first move is a slide up (or down) the string.
        L.notes.push_back ({ 0,   480, Scale, up ? -3 : 3, Glide });
        L.notes.push_back ({ 480, 240, Scale, up ? -1 : 1, 0 });
        L.notes.push_back ({ 720, 240, Scale, up ?  1 : -1, 0 });
        L.notes.push_back ({ 960, 480, Scale, up ?  2 : -2, Vib });
        L.notes.push_back ({ 1440, 960, Scale, 0, Target | Vib });
        return L;
    }
    // ---- thrash
    static Lick tremoloBurst (Rng& r)          // one note picked as fast as it goes, then a step
    {
        Lick L; L.family = Anyone; L.fast = true;
        // Marked tremolo: on Hydra each run of one pitch is played as ONE
        // held note on its looping tremolo (key 18); anything else picks the
        // notes as written. The notes themselves are unchanged.
        const int a = r.range (4, 6), b = r.range (3, 4);
        int t = 0;
        for (int i = 0; i < a; ++i) { L.notes.push_back ({ t, 60, Scale, 1, Trem }); t += 60; }
        for (int i = 0; i < b; ++i) { L.notes.push_back ({ t, 60, Exotic, 2, Trem }); t += 60; }
        L.notes.push_back ({ t, 600, Scale, 0, Target | Pinch | Vib });
        return L;
    }
    static Lick phrygianRun (Rng& r)           // down the dark scale, flat second and all
    {
        Lick L; L.family = Anyone; L.fast = true;
        const int n = r.range (5, 7);
        for (int i = 0; i < n; ++i) L.notes.push_back ({ i * 120, 120, Exotic, n - i, i % 3 == 2 ? Slur : 0u });
        L.notes.push_back ({ n * 120, 600, Exotic, 0, Target | Vib });
        return L;
    }
    static Lick chromaticMenace (Rng&)         // crawling round the note, then a stab
    {
        Lick L; L.family = Anyone;
        L.notes.push_back ({ 0,   120, Semis,  0, Mute });
        L.notes.push_back ({ 120, 120, Semis,  1, Mute });
        L.notes.push_back ({ 240, 120, Semis,  0, Mute });
        L.notes.push_back ({ 360, 120, Semis, -1, Mute });
        L.notes.push_back ({ 480, 240, Semis,  3, Bend1 });
        L.notes.push_back ({ 720, 120, Semis,  1, 0 });
        L.notes.push_back ({ 840, 600, Semis,  0, Target | Pinch | Vib });
        return L;
    }
    static Lick exoticLegato (Rng& r)          // three to a string, up the phrygian
    {
        Lick L; L.family = Anyone; L.triplet = true; L.fast = true;
        const int groups = r.range (3, 4);
        int t = 0;
        for (int g = 0; g < groups; ++g)
            for (int j = 0; j < 3; ++j) { L.notes.push_back ({ t, 80, Exotic, -2 * groups + 2 * g + j, Slur }); t += 80; }
        L.notes.push_back ({ t, 560, Exotic, 0, Target | Bend1 | Vib });
        return L;
    }

    // What each song style draws on, before the song's own leaning.
    struct StyleSchools { const char* style; double w[kSchools]; };
    static const StyleSchools kStyleSchools[] = {
        //                    Blues Melodic Neo  NWOBHM Thrash
        { "blues",          { 1.00, 0.30, 0.00, 0.05, 0.00 } },
        { "hard_rock",      { 0.45, 0.60, 0.20, 0.25, 0.05 } },
        { "ballad",         { 0.25, 0.90, 0.10, 0.05, 0.00 } },
        { "emo",            { 0.10, 0.90, 0.05, 0.10, 0.10 } },
        { "alt_rock",       { 0.30, 0.80, 0.05, 0.10, 0.10 } },
        { "punk",           { 0.50, 0.50, 0.00, 0.10, 0.10 } },
        { "metal",          { 0.10, 0.25, 0.45, 0.60, 0.35 } },
        { "thrash",         { 0.05, 0.10, 0.25, 0.25, 0.90 } },
        { "groove_metal",   { 0.45, 0.20, 0.10, 0.10, 0.70 } },
        { "prog_metal",     { 0.05, 0.35, 0.60, 0.20, 0.50 } },
        { "doom",           { 0.60, 0.45, 0.10, 0.15, 0.15 } },
        { "sludge",         { 0.60, 0.25, 0.00, 0.05, 0.40 } },
    };

    // The song's own mix: its style's weights, each moved by the song seed, so
    // two thrash songs lean differently - one more Hammett, one more Friedman.
    static void schoolMix (const std::string& style, uint32_t songSeed, double out[kSchools])
    {
        const double* base = nullptr;
        for (const StyleSchools& ss : kStyleSchools)
            if (style == ss.style) base = ss.w;
        static const double kEven[kSchools] = { 0.4, 0.6, 0.2, 0.2, 0.2 };
        if (base == nullptr) base = kEven;
        Rng r (deriveSeed (songSeed, 0x5C400Lu));
        for (int i = 0; i < kSchools; ++i) out[i] = base[i] * (0.55 + r.unit() * 0.9);
    }

    struct SoloLick { Builder make; unsigned roles; Family family; unsigned schools = SAll; };
    static const SoloLick kSoloLicks[] = {
        { tapArpeggio,     Build | Climax,   VanHalen, SMelodic | SNeo | SThrash },
        { hammerCascade,   Build,            VanHalen, SMelodic | SThrash },
        { rakeIntoBend,    Develop | Climax, Hammett,  SBlues | SThrash | SNwobhm },
        { pinchSqueal,     Climax,           Hammett,  SThrash | SBlues },
        { legatoRun,       Develop | Build,  Satriani, SMelodic | SNeo },
        { pedalLegato,     Develop | Build,  Satriani, SNeo | SMelodic },
        { singingBend,     Develop | Climax, Satriani, SBlues | SMelodic },
        { descendingFours, Develop | Build,  Satriani, SNeo | SNwobhm | SMelodic },
        { wideLeaps,       Develop | Build,  Vai,      SMelodic | SThrash },
        { bendAndReturn,   Develop,          Vai,      SBlues | SMelodic },
        { harmonics,       Develop,          Vai,      SMelodic },
        { chromaticClimb,  Develop,          Vai,      SThrash | SBlues },
        { tripletBox,      Develop | Build,  Hammett,  SBlues | SNwobhm | SThrash },
        { trill,           Build | Climax,   Hammett,  SThrash | SNwobhm | SNeo },
        { bluesRepeat,     Develop,          Hammett,  SBlues | SNwobhm },
        { chokeStabs,      Build,            Hammett,  SThrash },
        { sweep,           Build | Climax,   Vai,      SNeo | SThrash },
        { pickupCall,      Develop,          Anyone,   SAll },
        { flurry,          Develop | Build | Climax, VanHalen, SMelodic | SNeo | SNwobhm },
        { pentBends,       Develop | Build,  Hammett,  SBlues | SNwobhm | SMelodic },
        { arpSkip,         Develop | Build,  Vai,      SMelodic | SNeo },
        { octaveCall,      Develop | Climax, Anyone,   SMelodic | SBlues },
        // the schools' own
        { bbCry,           Develop | Climax, Anyone,   SBlues },
        { bendRelease,     Develop | Build,  Anyone,   SBlues | SMelodic },
        { bbBox,           Develop,          Anyone,   SBlues },
        { majPentRun,      Develop | Build,  Anyone,   SBlues | SMelodic },
        { bluesTriplets,   Build | Climax,   Anyone,   SBlues },
        { themeSequence,   Develop | Build,  Anyone,   SMelodic | SNwobhm },
        { slideSinger,     Develop,          Anyone,   SMelodic },
        { climbToPeak,     Build | Climax,   Anyone,   SMelodic | SNwobhm },
        { pedalSixes,      Build | Climax,   Anyone,   SNeo },
        { dimSweep,        Build | Climax,   Anyone,   SNeo },
        { harmSixes,       Build,            Anyone,   SNeo },
        { gallopRun,       Develop | Build,  Anyone,   SNwobhm },
        { twinTheme,       Develop,          Anyone,   SNwobhm | SMelodic },
        { tremoloBurst,    Build | Climax,   Anyone,   SThrash },
        { phrygianRun,     Build,            Anyone,   SThrash },
        { chromaticMenace, Develop | Build,  Anyone,   SThrash },
        { exoticLegato,    Build | Climax,   Anyone,   SThrash | SNeo },
    };
    static constexpr int kNumSoloLicks = static_cast<int> (sizeof (kSoloLicks) / sizeof (kSoloLicks[0]));

    // The solo's own idea: a rhythm a singer could sing, a contour with a leap
    // in it, a long note that bends or shakes, landing on the chord.
    static Lick makeMotif (Rng& r, bool shuffle, const double* school, double busy)
    {
        struct Cell { int n; int on[7]; int len[7]; };
        // On eighths first (a shuffle can play these), then the rest. The busy
        // ones - more to say before the landing - come in as the player gets
        // busier: a sparse opening is a choice, not the only one there is.
        static const Cell sparse[] = {
            { 3, { 0, 240, 480 },            { 240, 240, 960 } },
            { 4, { 0, 480, 720, 960 },       { 480, 240, 240, 960 } },
            { 4, { 240, 480, 720, 1440 },    { 240, 240, 720, 960 } },
            { 3, { 0, 720, 960 },            { 720, 240, 960 } },
            { 4, { 0, 360, 480, 960 },       { 360, 120, 480, 960 } },
            { 5, { 0, 120, 240, 720, 960 },  { 120, 120, 480, 240, 960 } },
        };
        static const Cell dense[] = {
            { 6, { 0, 240, 480, 720, 960, 1200 },       { 240, 240, 240, 240, 240, 960 } },
            { 6, { 0, 240, 480, 960, 1200, 1440 },      { 240, 240, 480, 240, 240, 960 } },
            { 7, { 0, 240, 480, 720, 1200, 1440, 1680 },{ 240, 240, 240, 480, 240, 240, 960 } },
            { 6, { 0, 120, 240, 480, 720, 960 },        { 120, 120, 240, 240, 240, 960 } },
        };
        const bool useDense = r.chance (std::clamp ((busy - 0.35) * 1.6, 0.0, 0.9));
        const Cell& c = useDense ? dense[r.below (shuffle ? 3 : 4)]
                                 : sparse[r.below (shuffle ? 4 : 6)];

        Lick L; L.family = Anyone;
        // The motif speaks the song's leading school.
        int lead = 0;
        for (int i = 1; i < kSchools; ++i) if (school[i] > school[lead]) lead = i;
        const Space sp = lead == Blues  ? (r.chance (0.3) ? MajPent : Pent)
                       : lead == Neo    ? Harm
                       : lead == Thrash ? (r.chance (0.5) ? Exotic : Scale)
                       : (r.chance (0.6) ? Pent : Scale);
        int step = r.range (1, 4) * (r.chance (0.3) ? -1 : 1);
        bool leapt = false;
        for (int i = 0; i < c.n - 1; ++i)
        {
            unsigned f = 0;
            if (c.len[i] >= 360)
                f = lead == Blues   ? (r.chance (0.5) ? Bend2 | Release | WideVib : (r.chance (0.5) ? Bend2 | PreBend : WideVib))
                  : lead == Melodic ? (r.chance (0.4) ? Scoop | Vib : (r.chance (0.5) ? Bend2 : Vib))
                                    : (r.chance (0.5) ? Bend2 : Vib);
            else if (r.chance (0.35)) f = Slur;
            L.notes.push_back ({ c.on[i], c.len[i], sp, step, f });

            // Next step: never two single steps the same way running, and at
            // least one real leap somewhere in the idea.
            int d = 0;
            do { d = r.range (-3, 3); } while (d == 0 || (i > 0 && std::abs (d) == 1 && ! leapt && r.chance (0.6)));
            if (std::abs (d) >= 2) leapt = true;
            step += d;
            if (step == 0) step = d > 0 ? 1 : -1;
        }
        if (! leapt && ! L.notes.empty()) L.notes.back().st += (L.notes.back().st >= 0 ? 2 : -2);
        L.notes.push_back ({ c.on[c.n - 1], c.len[c.n - 1], sp, 0,
                             Target | (lead == Blues ? WideVib : Vib) | (r.chance (0.3) ? Bend2 : 0u) });
        return L;
    }
}

// A part's own complexity: the dial, moved by that part's BUSY trim. A trim
// of zero returns the dial itself, so no song changes until a knob does.
static double trimmedComplexity (double complexity, double trim)
{
    return trim == 0.0 ? complexity : std::clamp (complexity + trim * 0.5, 0.0, 1.0);
}

// HOW SOLO-LIKE A STYLE'S FILLS MAY BE (2026-09-27). The owner: "for blues,
// fills can easily sound like parts of a solo here and there, but for say a
// rock song, fills vary but mostly differ from the solo... you won't find a
// one size fits all". 0 keeps a fill to a short answer and held support; 1
// lets it play solo-length licks and sing full phrases between them.
static double fillSoloLikeness (const std::string& style)
{
    struct S { const char* style; double v; };
    static const S table[] = {
        { "blues", 0.8 }, { "doom", 0.5 }, { "sludge", 0.5 },
        { "hard_rock", 0.3 }, { "ballad", 0.3 },
        { "metal", 0.25 }, { "thrash", 0.25 }, { "groove_metal", 0.25 }, { "prog_metal", 0.25 },
        { "alt_rock", 0.2 }, { "emo", 0.2 }, { "punk", 0.2 },
    };
    for (const S& e : table) if (style == e.style) return e.v;
    return 0.3;
}

static void generateFills (const SectionPlan& s,
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
                           double fillAmount,
                           double iq,
                           uint32_t songSeed,
                           double complexity,
                           double shredTrim,
                           double busyTrim)
{
    using namespace fillvoice;

    if (chords.empty() || profile == nullptr || s.bars <= 0 || fillAmount <= 0.0 || barTicks <= 0)
        return;

    // Everything below touches only what THIS section adds. The first version
    // tidied the whole part and trimmed the last note of the solo before it.
    const size_t firstOfSection = out.lead.size();

    // The register: above the rhythm guitar, below the squeal zone.
    const int lowest  = std::max (profile->chordLowest, 52);
    const int highest = std::min (profile->chordHighest, 86);
    if (highest - lowest < 12)
        return;

    const std::vector<int>& scale = soloScale (mode, style);
    if (scale.empty())
        return;

    std::vector<int> scalePcs, pentPcs;
    for (int iv : scale) scalePcs.push_back ((keyPc + iv) % 12);
    const bool minorish = std::find (scale.begin(), scale.end(), 3) != scale.end();
    for (int iv : (minorish ? std::vector<int> { 0, 3, 5, 7, 10 } : std::vector<int> { 0, 2, 4, 7, 9 }))
        pentPcs.push_back ((keyPc + iv) % 12);

    const std::vector<int> scaleLadder = ladder (scalePcs, lowest, highest);
    const std::vector<int> pentLadder  = ladder (pentPcs,  lowest, highest);
    const KeyLadders kl = makeLadders (scale, keyPc, style == "blues", lowest, highest);

    const Personality who = personalityFor (songSeed);
    const int sectionEnd = sectionStartTick + s.bars * barTicks;
    const double soloLike = fillSoloLikeness (style);
    auto juce_clamp_tick = [] (int t, int lo, int hi) { return std::max (lo, std::min (hi, t)); };
    const bool hot     = s.intensity > 0.6;
    const bool shuffle = swing > 0.0;
    int lastLick = -1, beforeThat = -1;

    auto chordAt = [&] (int bar) -> const Chord& { return chords[static_cast<size_t> (bar) % chords.size()]; };

    auto chordPcs = [] (const Chord& c)
    {
        std::vector<int> v { c.rootPc };
        if (c.thirdSemitones() >= 0)   v.push_back ((c.rootPc + c.thirdSemitones()) % 12);
        v.push_back ((c.rootPc + c.fifthSemitones()) % 12);
        if (c.seventhSemitones() >= 0) v.push_back ((c.rootPc + c.seventhSemitones()) % 12);
        return v;
    };

    // Which stream the lick placing draws from: the section's, except for the
    // blues reach below, which has its own so not one other note moves.
    Rng* lr = &rng;

    // Places one lick so it ENDS at `endTick`, anchored on a tone of `chord`.
    auto playLick = [&] (Lick L, int endTick, const Chord& chord, bool fromBelow, int room)
    {
        // Trim from the front until it fits, keeping the landing: a long run
        // that would start in the front of the bar - where the part being
        // answered lives - comes in later and shorter instead.
        {
            const int scaleT = 1;   // already in the shuffle's time - see toSwungEighths
            // Down to three notes to fit; below that only if vary() stretched
            // the tail past the room (a held note, a breath).
            // An ANSWER, not a solo: at most a phrase's worth of notes (more
            // as BUSY rises). Heard 2026-09-27 on Nine Cent Rain's last verse:
            // "the fill sounded more like the solo continuing on".
            const size_t cap = static_cast<size_t> (std::clamp (5.0 + soloLike * 7.0 + busyTrim * 4.0, 4.0, 14.0));
            while (L.notes.size() > cap)
            {
                const int drop = L.notes[1].on;
                L.notes.erase (L.notes.begin());
                for (LickNote& n : L.notes) n.on -= drop;
            }
            while (L.notes.size() > 1 && L.span() * scaleT > room
                   && (L.notes.size() > 3
                       || (L.notes.back().on + 120) * scaleT > room))
            {
                const int drop = L.notes[1].on;
                L.notes.erase (L.notes.begin());
                for (LickNote& n : L.notes) n.on -= drop;
            }
            // A landing let ring past the room is cut back to it, never below a 16th.
            const int over = L.span() * scaleT - room;
            if (over > 0)
                L.notes.back().len = std::max (120, L.notes.back().len - (over + scaleT - 1) / scaleT);
        }
        // Upside down sometimes: the same shape, heard as a different lick.
        if (lr->chance (who.invert))
            for (LickNote& n : L.notes)
                if (n.sp != Semis) n.st = -n.st;

        // A shuffle's swing pass keeps only notes on the swung eighth: the
        // straight figures play there at double length, eighths instead of
        // sixteenths, which is what a shuffle player does anyway.
        // A triplet lick is already in a shuffle's time and plays at its own
        // speed, unswung. The first version banned them in a shuffle, and with
        // every straight lick doubled too long for its room, a blues song's
        // fills were held notes and nothing else.
        const int span = L.span();
        int start = endTick - span;
        if (shuffle)
        {
            const int g = L.triplet ? kPPQ / 3 : kPPQ / 2;
            start = sectionStartTick + ((start - sectionStartTick) / g) * g;
        }
        if (start < sectionStartTick)
            return false;

        const std::vector<int> tones = ladder (chordPcs (chord), lowest, highest);
        if (tones.empty())
            return false;

        // Anchor high enough to have room below for a lick that climbs into
        // it, low enough for one that falls - and a little random, so the same
        // lick lands in a different place on the neck.
        const int mid    = lowest + (highest - lowest) * (fromBelow ? 3 : 2) / 5;
        const int anchor = tones[static_cast<size_t> (nearestIndex (tones, mid + lr->range (-4, 4)))];

        for (const LickNote& n : L.notes)
        {
            int pitch = -1;
            auto from = [&] (const std::vector<int>& v)
            {
                const int i = nearestIndex (v, anchor) + n.st;
                return (i >= 0 && i < static_cast<int> (v.size())) ? v[static_cast<size_t> (i)] : -1;
            };
            if (const std::vector<int>* lad = ladderFor (n.sp, kl, tones)) pitch = from (*lad);
            else                                                         pitch = anchor + n.st;
            if (pitch < lowest || pitch > highest)
                continue;

            LeadIntent li;
            // Inside its own section, always: a jitter that nudged the first
            // note back a few ticks put it in the section before - a solo.
            li.tick          = juce_clamp_tick (start + n.on + static_cast<int> (lr->bipolar (humanize * 3.0)),
                                                sectionStartTick, sectionEnd - 1);
            li.durationTicks = std::max (1, n.len);
            li.pitch         = pitch;
            // Softer than a solo (which strikes about 0.95): the answer sits
            // behind the band and the solo steps out in front when it comes.
            li.accent        = std::min (0.88, 0.58 + soloLike * 0.08 + s.intensity * 0.14 + ((n.f & Target) ? 0.06 : 0.0) + lr->bipolar (0.04));
            li.target        = (n.f & Target) != 0;
            li.slur          = (n.f & Slur) != 0;
            li.glide         = (n.f & Glide) != 0;
            // THE FX KEYS in the songs, sparingly and without a random draw
            // (so not one approved note moves): a held landing now and then
            // falls off the neck, a lick now and then opens with a fret squeak
            // in the gap before it or slides in from up the neck.
            {
                const int beat = li.tick / 480;
                const bool plainStart = n.on == 0 && ! (n.f & (Bend1 | Bend2 | Scoop | Rake | Tap | PreBend));
                li.slideOff    = (n.f & Target) && n.len >= 600 && beat % 7 == 2;
                li.fretNoise   = n.on == 0 && beat % 8 == 3;
                li.neckSlideIn = plainStart && beat % 13 == 4;
            }
            li.vibrato       = (n.f & Vib) != 0;
            li.bendSemis     = (n.f & Bend2) ? 2 : ((n.f & Bend1) ? 1 : 0);
            expression (li, n.f);
            li.unswung       = shuffle && L.triplet;
            li.artic         = (n.f & Trem)  ? LeadArtic::Tremolo
                             : (n.f & Tap)   ? LeadArtic::Tap
                             : (n.f & Mute)  ? LeadArtic::Mute
                             : (n.f & Stacc) ? LeadArtic::Staccato
                             : (n.f & Pinch) ? LeadArtic::Pinch
                             : (n.f & HarmF)  ? LeadArtic::Harmonic
                             : (n.f & Rake)  ? LeadArtic::Rake
                             : (n.f & Choke) ? LeadArtic::Choke
                                             : LeadArtic::Normal;
            out.lead.push_back (li);
        }
        return true;
    };

    // Picks a lick for the room there is, by the song's personality.
    auto chooseLick = [&] (int room) -> int
    {
        double w[kNumLicks], total = 0.0;
        for (int i = 0; i < kNumLicks; ++i)
        {
            Rng probe (deriveSeed (songSeed, 0x51Du + static_cast<uint32_t> (i)));
            Lick L = kBuilders[i] (probe);
            if (shuffle && ! L.triplet) toSwungEighths (L);
            w[i] = who.lick[i];
            // Triplets are allowed in a shuffle now: they are its own time.
            // The landing and two notes before it must fit - a longer lick is
            // trimmed from the FRONT to its room, keeping where it lands.
            {
                const int n = static_cast<int> (L.notes.size());
                const int tail = n >= 3 ? L.span() - L.notes[static_cast<size_t> (n - 3)].on : L.span();
                if (tail > room) w[i] = 0.0;
            }
            // No intuition is plain answering: a few notes, no fireworks.
            if (iq < 0.25 && L.notes.size() > 4) w[i] = 0.0;
            if (L.fast && ! hot)                  w[i] *= byIntuition (iq, 0.2, 0.55, 0.8);
            if (! L.fast && hot)                  w[i] *= 0.7;
            // SHRED: up leans on the fast licks, down on the sung ones.
            if (shredTrim != 0.0)                 w[i] *= L.fast ? (1.0 + shredTrim) : (1.0 - shredTrim * 0.6);
            if (i == lastLick || i == beforeThat) w[i] *= 0.08;
            w[i] *= busyWeight (L, busyTrim);
            total += w[i];
        }
        if (total <= 0.0) return -1;
        double pick = lr->unit() * total;
        for (int i = 0; i < kNumLicks; ++i)
            if ((pick -= w[i]) < 0.0) return i;
        return kNumLicks - 1;
    };

    // Between licks the guitar SINGS - and singing is a PHRASE, not a note.
    // Heard on a blues: "most of the fills was nothing more than a single note
    // every 4 bars - boring, empty". So a free bar now usually gets a slow,
    // soft idea - a singing bend, a bend-and-return, the blues box leaned on -
    // voice-led from where the line was. Complexity decides how often; the
    // plain held note is what is left when it does not.
    int lastPitch = -1;
    auto sing = [&] (int from, int to, const Chord& chord)
    {
        static const Builder kSung[] = { singingBend, bendAndReturn, pentBends, pickupCall,
                                         octaveCall, bluesRepeat, harmonics, arpSkip };
        static constexpr int kNumSung = static_cast<int> (sizeof (kSung) / sizeof (kSung[0]));
        // Mostly a held note now; a short figure sometimes. The sung phrase
        // was a full six-note bent melody in every free bar, and with the
        // answers around it the guitar never stopped leading.
        if (to - from >= barTicks / 2 && rng.chance (std::clamp (0.12 + soloLike * 0.35 + complexity * 0.3 + busyTrim * 0.35, 0.0, 0.9)))
        {
            Rng lr (deriveSeed (static_cast<uint32_t> (rng.next()), 0x5E1Au));
            Lick P = kSung[lr.below (kNumSung)] (lr);
            while (P.notes.size() > static_cast<size_t> (3.0 + soloLike * 4.0))   // a figure, or in a blues a phrase
            {
                const int drop = P.notes[1].on;
                P.notes.erase (P.notes.begin());
                for (LickNote& n : P.notes) n.on -= drop;
            }
            if (shuffle && ! P.triplet) toSwungEighths (P);
            // Starts where the room starts, lands, and holds to its end.
            int at = from;
            if (shuffle) { const int g = P.triplet ? kPPQ / 3 : kPPQ / 2; at = sectionStartTick + ((at - sectionStartTick + g - 1) / g) * g; }
            while (P.notes.size() > 1 && at + P.notes.back().on > to - barTicks / 4)
            {
                const int drop = P.notes[1].on;
                P.notes.erase (P.notes.begin());
                for (LickNote& n : P.notes) n.on -= drop;
            }
            if (at + P.notes.back().on <= to - barTicks / 8)
            {
                P.notes.back().len = to - (at + P.notes.back().on);
                const std::vector<int> tones = ladder (chordPcs (chord), lowest + 3, highest - 3);
                if (! tones.empty())
                {
                    const int near   = lastPitch > 0 ? lastPitch : (lowest + highest) / 2;
                    const int anchor = tones[static_cast<size_t> (nearestIndex (tones, near + rng.range (-5, 5)))];
                    for (const LickNote& n : P.notes)
                    {
                        auto from2 = [&] (const std::vector<int>& v)
                        {
                            const int i = nearestIndex (v, anchor) + n.st;
                            return (i >= 0 && i < static_cast<int> (v.size())) ? v[static_cast<size_t> (i)] : -1;
                        };
                        const std::vector<int>* lad = ladderFor (n.sp, kl, tones);
                        const int pitch = lad != nullptr ? from2 (*lad) : anchor + n.st;
                        if (pitch < lowest || pitch > highest) continue;
                        LeadIntent li;
                        li.tick          = juce_clamp_tick (at + n.on + static_cast<int> (rng.bipolar (humanize * 3.0)),
                                                            sectionStartTick, sectionEnd - 1);
                        li.durationTicks = std::max (1, n.len);
                        li.pitch         = pitch;
                        li.accent        = std::min (0.8, 0.52 + s.intensity * 0.12 + rng.bipolar (0.04));
                        li.target        = (n.f & Target) != 0;
                        li.slur          = (n.f & Slur) != 0;
            li.glide         = (n.f & Glide) != 0;
            // THE FX KEYS in the songs, sparingly and without a random draw
            // (so not one approved note moves): a held landing now and then
            // falls off the neck, a lick now and then opens with a fret squeak
            // in the gap before it or slides in from up the neck.
            {
                const int beat = li.tick / 480;
                const bool plainStart = n.on == 0 && ! (n.f & (Bend1 | Bend2 | Scoop | Rake | Tap | PreBend));
                li.slideOff    = (n.f & Target) && n.len >= 600 && beat % 7 == 2;
                li.fretNoise   = n.on == 0 && beat % 8 == 3;
                li.neckSlideIn = plainStart && beat % 13 == 4;
            }
                        li.vibrato       = (n.f & Vib) != 0;
                        li.bendSemis     = (n.f & Bend2) ? 2 : ((n.f & Bend1) ? 1 : 0);
                        expression (li, n.f);
            expression (li, n.f);
                        li.unswung       = shuffle && P.triplet;
                        li.artic         = (n.f & HarmF) ? LeadArtic::Harmonic : LeadArtic::Normal;
                        li.bed           = true;     // support, not an answer
                        out.lead.push_back (li);
                        lastPitch = pitch;
                    }
                    return;
                }
            }
        }

        if (to - from < barTicks / 2)
            return;
        const std::vector<int> tones = ladder (chordPcs (chord), lowest + 5, highest - 2);
        if (tones.empty())
            return;
        LeadIntent li;
        li.tick          = juce_clamp_tick (from + static_cast<int> (rng.bipolar (humanize * 3.0)),
                                            sectionStartTick, sectionEnd - 1);
        li.durationTicks = to - from;
        li.pitch         = lastPitch > 0 ? tones[static_cast<size_t> (nearestIndex (tones, lastPitch + rng.range (-4, 4)))]
                                         : tones[static_cast<size_t> (rng.below (static_cast<int> (tones.size())))];
        lastPitch        = li.pitch;
        li.accent        = 0.54 + s.intensity * 0.12;
        li.bendSemis     = rng.chance (0.55) ? 2 : 0;
        li.vibrato       = true;
        li.bed           = true;     // support, not an answer - see LeadIntent::bed
        if (rng.chance (0.15)) li.artic = LeadArtic::Harmonic;
        out.lead.push_back (li);
    };

    // Four-bar groups. The last bar of each group always may answer (at one,
    // it always does); the second bar sometimes does too; the rest sing or rest.
    // WHERE AN ANSWER GOES IS INTUITION'S. At none, exactly what it always
    // was: the fourth bar of a group, starting on the half bar. As it rises,
    // answers may end on the bar line, come in early, reach back into the bar
    // before, and turn up in the second bar of a group too.
    const double wHalf  = byIntuition (iq, 100.0, 30.0, 12.0);
    const double wEnd   = byIntuition (iq,   0.0, 50.0, 45.0);
    const double wEarly = byIntuition (iq,   0.0, 20.0, 43.0);
    const double second = std::clamp (byIntuition (iq, 0.0, who.answerSecond, 0.85) * (0.6 + complexity * 0.9)
                                      * (1.0 + busyTrim * 0.9), 0.0, 0.95);
    // BUSY up also answers in the THIRD bar of a group, a busy player's habit.
    const double third = busyTrim > 0.0 ? busyTrim * 0.8 : 0.0;
    const double first = busyTrim > 0.5 ? (busyTrim - 0.5) * 0.9 : 0.0;   // and near the top, the first bar too
    int lastEnd = sectionStartTick;      // where the last note of this section ended
    bool answeredLastEnding = true;      // so the first line may rest
    int lastAnswerEnd = sectionStartTick;   // where the last ANSWER (not a sung bed) ended

    for (int bar = 0; bar < s.bars; ++bar)
    {
        const int barStart = sectionStartTick + bar * barTicks;
        const int barEnd   = barStart + barTicks;
        const int inGroup  = bar % 4;
        const bool lastOfSection = (bar == s.bars - 1);
        size_t before = out.lead.size();

        bool answer = false;
        if (inGroup == 3 || (lastOfSection && iq > 0.0))
        {
            answer = fillAmount >= 0.999 || rng.chance (0.35 + fillAmount * 0.65);
            // Never two line-endings running without an answer. Nine Cent Rain's
            // last verse drew two misses at 13% each and played eight bars of
            // held notes - chance, but an unlucky song is still a bad song.
            if (! answer && ! answeredLastEnding) answer = true;
            answeredLastEnding = answer;
        }
        else if (inGroup == 1)
            answer = rng.chance (second);
        else if (inGroup == 2 && third > 0.0)
            answer = rng.chance (third);
        else if (inGroup == 0 && first > 0.0)
            answer = rng.chance (first);

        int lickStart = barEnd;   // where the answer begins, if any
        if (answer)
        {
            double pick = rng.unit() * (wHalf + wEnd + wEarly);
            int start = -1, end = -1, room = 0;
            if ((pick -= wHalf) < 0.0)      { start = barStart + barTicks / 2; room = barTicks / 2; }
            else if ((pick -= wEnd) < 0.0)  { end = barEnd - (rng.chance (0.5) ? 0 : barTicks / 8);
                                              room = end - (barStart + barTicks / 3); }
            else                            { start = barStart + (barTicks * 3) / 8; room = barEnd - start; }

            const int i = chooseLick (room);
            if (i >= 0)
            {
                Rng lickRng (deriveSeed (static_cast<uint32_t> (rng.next()), 0xA11u + static_cast<uint32_t> (bar)));
                Lick L = kBuilders[i] (lickRng);
                vary (L, rng, who, shuffle);   // before placing: it changes the lick's length
                // In a shuffle: straight sixteenths become swung eighths, longer
                // notes keep their length. Doubling the whole lick left only the
                // triplet trills short enough to fit - a blues song's fills were
                // held notes and see-saws.
                if (shuffle && ! L.triplet) toSwungEighths (L);
                const int roomHere = end < 0 ? barEnd - start : room;
                if (end < 0) end = std::min (barEnd, start + L.span());
                if (playLick (L, end, chordAt (bar), rng.chance (0.6), roomHere))
                {
                    beforeThat = lastLick;
                    lastLick   = i;
                    int first = end;
                    for (size_t k = before; k < out.lead.size(); ++k) first = std::min (first, out.lead[k].tick);
                    lickStart = first;

                    // THE BLUES ANSWER REACHES BACK (2026-10-03). A twelve-bar
                    // line is the singer's for two bars and the guitar's for two;
                    // the answer above had only the back of the last one - two
                    // beats, the same as a rock fill. So at the end of a line the
                    // guitar starts talking in the bar before: a phrase from the
                    // back half of bar three, a breath, then the landing above.
                    // Its own stream: no other note in the song moves.
                    // BUSY moves it too: at a line's end always at the top and
                    // never at the bottom; turned up, the second and third bars'
                    // answers reach back into the bar before them as well.
                    if (style == "blues" && bar > 0 && (inGroup == 3 || ((inGroup == 1 || inGroup == 2) && busyTrim > 0.0)))
                    {
                        Rng reach (deriveSeed (songSeed, hashString ("blues reach/" + std::to_string (barStart))));
                        const int from  = std::max (barStart - barTicks / 2, lastAnswerEnd + kPPQ / 2);
                        const int upTo  = lickStart - kPPQ / 2;
                        const int roomR = upTo - from;
                        const double reachChance = inGroup != 3      ? busyTrim
                                                 : busyTrim >= 0.0   ? 0.75 + busyTrim * 0.25
                                                                     : 0.75 * (1.0 + busyTrim);
                        if (reach.chance (reachChance) && roomR >= kPPQ)
                        {
                            lr = &reach;
                            const int j = chooseLick (roomR);
                            if (j >= 0)
                            {
                                Rng lickRng2 (deriveSeed (static_cast<uint32_t> (reach.next()), 0xA12u + static_cast<uint32_t> (bar)));
                                Lick R = kBuilders[j] (lickRng2);
                                vary (R, reach, who, shuffle);
                                if (shuffle && ! R.triplet) toSwungEighths (R);
                                const size_t mark = out.lead.size();
                                const bool okR = playLick (R, upTo, chordAt (upTo >= barStart ? bar : bar - 1), reach.chance (0.5), roomR);
                                if (okR)
                                {
                                    int firstR = upTo;
                                    for (size_t k = mark; k < out.lead.size(); ++k) firstR = std::min (firstR, out.lead[k].tick);
                                    // What the bar before sang from there on gives way.
                                    for (size_t k = firstOfSection; k < mark; )
                                    {
                                        const LeadIntent& o = out.lead[k];
                                        if (o.bed && o.tick >= firstR && o.tick < barEnd)
                                        {
                                            out.lead.erase (out.lead.begin() + static_cast<long> (k));
                                            if (k < before) --before;
                                        }
                                        else ++k;
                                    }
                                    lickStart = firstR;
                                    beforeThat = lastLick;
                                    lastLick   = j;
                                }
                            }
                            lr = &rng;
                        }
                    }
                }
            }
        }

        // THE THRASH NOTE (2026-10-03, the owner: "add the thrash notes in songs
        // where it makes sense"). Hydra's D#0 re-picks the note already sounding
        // - "riff between this and another note for fast patterns" (manual
        // p35). So in the thrash family some answers become a pedal riff: a home
        // note under the line, picked twice or three times on the thrash key,
        // a moving note between, walking up to the answer's own landing. Fills
        // only (the solos stay as approved), never a twin lick (guitar 1
        // harmonises those), and on its own stream: no other note moves.
        {
            const double riffChance = style == "thrash"       ? 0.40
                                    : style == "groove_metal" ? 0.30
                                    : style == "metal"        ? 0.20 : 0.0;
            size_t landing = out.lead.size();
            int answerFrom = barEnd;
            bool twin = false;
            for (size_t k = before; k < out.lead.size(); ++k)
            {
                if (out.lead[k].bed) continue;
                answerFrom = std::min (answerFrom, out.lead[k].tick);
                twin = twin || out.lead[k].twin;
                if (landing == out.lead.size() || out.lead[k].tick > out.lead[landing].tick) landing = k;
            }
            Rng riff (deriveSeed (songSeed, hashString ("thrash riff/" + std::to_string (barStart))));
            const int sixteenth = kPPQ / 4;
            const int from = sectionStartTick + ((std::min (answerFrom, barStart + barTicks / 2) - sectionStartTick + sixteenth - 1) / sixteenth) * sixteenth;
            if (riffChance > 0.0 && ! shuffle && ! twin && landing < out.lead.size()
                && out.lead[landing].tick - from >= 3 * sixteenth && riff.chance (riffChance))
            {
                const LeadIntent land = out.lead[landing];
                // The home note: the chord's root at or below the landing.
                const Chord& ch = chordAt (bar);
                int home = land.pitch;
                while (home > lowest && ((home - ch.rootPc) % 12 + 12) % 12 != 0) --home;
                if (home < lowest) home += 12;
                const int homeAt = nearestIndex (scaleLadder, home);
                const bool repick = profile->thrashNoteKey >= 0;

                std::vector<LeadIntent> riffNotes;
                auto add = [&] (int tick, int len, int pitch, int repeats, double accent)
                {
                    LeadIntent li;
                    li.tick          = tick;
                    li.durationTicks = len;
                    li.pitch         = pitch;
                    li.accent        = std::min (0.9, accent + s.intensity * 0.1);
                    li.thrashRepeats = repeats;
                    riffNotes.push_back (li);
                };
                // Cells of home-home-move (or home-home-home-move), the moving
                // note climbing the key a step a cell.
                int t = from, step = 1;
                const int stop = land.tick;
                while (t + 3 * sixteenth <= stop)
                {
                    const int homes = (t + 4 * sixteenth <= stop && riff.chance (0.35)) ? 3 : 2;
                    const int mi = std::min (homeAt + step, static_cast<int> (scaleLadder.size()) - 1);
                    const int move = scaleLadder[static_cast<size_t> (std::max (0, mi))];
                    if (repick)
                        add (t, homes * sixteenth, home, homes - 1, 0.66);
                    else
                        for (int h = 0; h < homes; ++h) add (t + h * sixteenth, sixteenth, home, 0, 0.66);
                    t += homes * sixteenth;
                    add (t, sixteenth, move, 0, 0.76);
                    t += sixteenth;
                    step = step >= 4 ? 1 : step + 1;
                }
                if (! riffNotes.empty())
                {
                    // The lick's notes give way; its landing stays.
                    for (size_t k = out.lead.size(); k-- > before; )
                        if (! out.lead[k].bed && k != landing)
                            out.lead.erase (out.lead.begin() + static_cast<long> (k));
                    for (const LeadIntent& li : riffNotes)
                        out.lead.push_back (li);
                }
            }
        }

        for (size_t k = before; k < out.lead.size(); ++k)
            if (! out.lead[k].bed)
                lastAnswerEnd = std::max (lastAnswerEnd, out.lead[k].tick + out.lead[k].durationTicks);

        // Anything before the answer in this bar sings or rests - but never two
        // silent bars running: "a cut out in gtr lasts a second or two".
        const int singTo = std::min (barEnd, lickStart - barTicks / 16);
        // Sing if leaving this bar empty up to the answer would make the guitar
        // silent for most of a bar - judged by the outcome, not the gap so far.
        const bool mustSing = lickStart - lastEnd >= (barTicks * 3) / 4;
        if (singTo - barStart >= barTicks / 4
            && (mustSing || rng.chance (who.sustain * (0.5 + fillAmount * 0.5))))
            sing (barStart, singTo, chordAt (bar));

        for (size_t k = before; k < out.lead.size(); ++k)
            lastEnd = std::max (lastEnd, out.lead[k].tick + out.lead[k].durationTicks);
    }

    // One note at a time, as every lead line here is: a lick reaching back
    // into a bar that also sang cuts the sustain short rather than chording.
    std::sort (out.lead.begin() + static_cast<long> (firstOfSection), out.lead.end(),
               [] (const LeadIntent& a, const LeadIntent& b) { return a.tick < b.tick; });
    for (size_t i = firstOfSection; i + 1 < out.lead.size(); )
    {
        const int gap = out.lead[i + 1].tick - out.lead[i].tick;
        if (gap <= 0)
        {
            out.lead.erase (out.lead.begin() + static_cast<long> (out.lead[i].bed ? i : i + 1));
            continue;
        }
        out.lead[i].durationTicks = std::min (out.lead[i].durationTicks, gap);
        ++i;
    }
}

static void generateLeadSolo (const SectionPlan& s,
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
                              double iq,
                              uint32_t songSeed,
                              double complexity,
                              double shredTrim,
                              double busyTrim)
{
    using namespace fillvoice;

    // COMPLEXITY AND INTUITION PLAY THE SOLO. Neither reached it at first, and
    // at a preset's default dials the solo was bland: "it wasn't till i cranked
    // the complexity and intuition all the way up that i got anything".
    SoloStyle how;
    how.shred = std::clamp (0.15 + complexity * 0.6 + iq * 0.3 + shredTrim * 0.6, 0.0, 1.0);
    how.busy  = std::clamp (0.2 + complexity * 0.9 + iq * 0.2, 0.0, 1.0);

    if (chords.empty() || profile == nullptr || s.bars <= 0 || barTicks <= 0)
        return;

    const size_t firstOfSection = out.lead.size();

    // A lead's register: over the rhythm guitar, up to where it screams.
    const int lowest  = std::max (profile->chordLowest, 50);
    const int highest = std::min (profile->chordHighest, 88);
    if (highest - lowest < 12)
        return;

    const std::vector<int>& scale = soloScale (mode, style);
    if (scale.empty())
        return;

    std::vector<int> scalePcs, pentPcs;
    for (int iv : scale) scalePcs.push_back ((keyPc + iv) % 12);
    const bool minorish = std::find (scale.begin(), scale.end(), 3) != scale.end();
    for (int iv : (minorish ? std::vector<int> { 0, 3, 5, 7, 10 } : std::vector<int> { 0, 2, 4, 7, 9 }))
        pentPcs.push_back ((keyPc + iv) % 12);
    const std::vector<int> scaleLadder = ladder (scalePcs, lowest, highest);
    const std::vector<int> pentLadder  = ladder (pentPcs,  lowest, highest);
    const KeyLadders kl = makeLadders (scale, keyPc, style == "blues", lowest, highest);

    // WHICH SCHOOL THIS SONG'S SOLOS COME FROM - see kStyleSchools.
    double school[kSchools];
    schoolMix (style, songSeed, school);

    const Personality who     = personalityFor (songSeed);
    const bool        shuffle = swing > 0.0;
    const bool        hot     = s.intensity > 0.6;
    const int         sectionEnd = sectionStartTick + s.bars * barTicks;

    auto clampTick = [&] (int t) { return std::max (sectionStartTick, std::min (sectionEnd - 1, t)); };
    auto chordAt   = [&] (int bar) -> const Chord& { return chords[static_cast<size_t> (bar) % chords.size()]; };
    auto chordPcs  = [] (const Chord& c)
    {
        std::vector<int> v { c.rootPc };
        if (c.thirdSemitones() >= 0)   v.push_back ((c.rootPc + c.thirdSemitones()) % 12);
        v.push_back ((c.rootPc + c.fifthSemitones()) % 12);
        if (c.seventhSemitones() >= 0) v.push_back ((c.rootPc + c.seventhSemitones()) % 12);
        return v;
    };

    // Plays a lick whose LANDING note starts at `landing`, anchored on a tone
    // of `chord` near `register`, the landing held to `holdTo`. The lick may
    // start no earlier than `earliest`: longer ones lose notes from the front.
    // Returns where it actually started, or -1.
    auto play = [&] (Lick L, int landing, int holdTo, int earliest, const Chord& chord,
                     int reg, bool timesScale, double lift, bool rootOnly) -> int
    {
        if (L.notes.empty()) return -1;
        // A triplet lick under a shuffle is already in the shuffle's time: it
        // plays at its own speed and the swing pass leaves it alone.
        if (shuffle && timesScale && ! L.triplet)
            toSwungEighths (L);
        const int sc = 1;
        while (L.notes.size() > 1 && L.notes.back().on * sc > landing - earliest)
        {
            const int drop = L.notes[1].on;
            L.notes.erase (L.notes.begin());
            for (LickNote& n : L.notes) n.on -= drop;
        }
        if (L.notes.back().on * sc > landing - earliest)
            return -1;
        L.notes.back().len = std::max (120 / sc, (holdTo - landing) / sc);

        if (rng.chance (who.invert))
            for (size_t i = 0; i + 1 < L.notes.size(); ++i)
                if (L.notes[i].sp != Semis) L.notes[i].st = -L.notes[i].st;

        // Under a shuffle every lick sits on its own grid - swung eighths for a
        // straight one, triplets for a triplet one. A straight lick chained in
        // front of a triplet one inherited a triplet position, sat off the
        // eighths, and the swing pass deleted it without a word.
        if (shuffle)
        {
            const int g = L.triplet ? kPPQ / 3 : kPPQ / 2;
            landing = sectionStartTick + ((landing - sectionStartTick) / g) * g;
            if (L.notes.back().on * sc > landing - earliest) return -1;
        }
        const int start = landing - L.notes.back().on * sc;

        // The chord's tones for the lick; the anchor - where it lands - from
        // the roots alone when the phrase must end home. (Once, the roots stood
        // in for the chord's tones everywhere, and a tapped arpeggio at the
        // end of a solo collapsed into octaves.)
        const std::vector<int> tones   = ladder (chordPcs (chord), lowest, highest);
        const std::vector<int> anchors = rootOnly ? ladder (std::vector<int> { chord.rootPc }, lowest, highest) : tones;
        if (tones.empty() || anchors.empty()) return -1;
        auto pitchOf = [&] (const LickNote& n, int anchor)
        {
            auto from = [&] (const std::vector<int>& v)
            {
                const int i = nearestIndex (v, anchor) + n.st;
                return (i >= 0 && i < static_cast<int> (v.size())) ? v[static_cast<size_t> (i)] : -1;
            };
            if (const std::vector<int>* lad = ladderFor (n.sp, kl, tones)) return from (*lad);
            return anchor + n.st;
        };

        // The whole lick on the neck: an anchor near the register, moved a
        // chord tone at a time until every note of the lick is playable. A
        // tapped arpeggio at the top of a solo lost its top note to the range
        // and came out as two pitches see-sawing.
        int ai = nearestIndex (anchors, reg + rng.range (-3, 3));
        auto misses = [&] (int a)
        {
            int m = 0;
            for (const LickNote& n : L.notes) { const int p = pitchOf (n, anchors[static_cast<size_t> (a)]); if (p < lowest || p > highest) ++m; }
            return m;
        };
        {
            int best = ai, bestMiss = misses (ai);
            for (int d = 1; d <= 4 && bestMiss > 0; ++d)
                for (int a : { ai - d, ai + d })
                    if (a >= 0 && a < static_cast<int> (anchors.size()))
                    {
                        const int m = misses (a);
                        if (m < bestMiss) { best = a; bestMiss = m; }
                    }
            ai = best;
        }
        const int anchor = anchors[static_cast<size_t> (ai)];

        for (const LickNote& n : L.notes)
        {
            const int pitch = pitchOf (n, anchor);
            if (pitch < lowest || pitch > highest)
                continue;

            LeadIntent li;
            li.tick          = clampTick (start + n.on * sc + static_cast<int> (rng.bipolar (humanize * 3.0)));
            li.durationTicks = std::max (1, n.len * sc);
            li.pitch         = pitch;
            li.twin          = L.twin;
            li.accent        = std::min (1.0, 0.80 + s.intensity * 0.14 + lift
                                               + ((n.f & Target) ? 0.08 : 0.0) + rng.bipolar (0.04));
            li.target        = (n.f & Target) != 0;
            li.slur          = (n.f & Slur) != 0;
            li.glide         = (n.f & Glide) != 0;
            // THE FX KEYS in the songs, sparingly and without a random draw
            // (so not one approved note moves): a held landing now and then
            // falls off the neck, a lick now and then opens with a fret squeak
            // in the gap before it or slides in from up the neck.
            {
                const int beat = li.tick / 480;
                const bool plainStart = n.on == 0 && ! (n.f & (Bend1 | Bend2 | Scoop | Rake | Tap | PreBend));
                li.slideOff    = (n.f & Target) && n.len >= 600 && beat % 7 == 2;
                li.fretNoise   = n.on == 0 && beat % 8 == 3;
                li.neckSlideIn = plainStart && beat % 13 == 4;
            }
            li.vibrato       = (n.f & Vib) != 0;
            li.bendSemis     = (n.f & Bend2) ? 2 : ((n.f & Bend1) ? 1 : 0);
            expression (li, n.f);
            li.unswung       = shuffle && L.triplet;
            const bool gesture = rng.chance (how.gestures);
            li.artic         = (n.f & Trem)    ? LeadArtic::Tremolo
                             : ! gesture       ? LeadArtic::Normal
                             : (n.f & Tap)     ? LeadArtic::Tap
                             : (n.f & Mute)    ? LeadArtic::Mute
                             : (n.f & Stacc)   ? LeadArtic::Staccato
                             : (n.f & Pinch)   ? LeadArtic::Pinch
                             : (n.f & HarmF)    ? LeadArtic::Harmonic
                             : (n.f & Rake)    ? LeadArtic::Rake
                             : (n.f & Choke)   ? LeadArtic::Choke
                                               : LeadArtic::Normal;
            out.lead.push_back (li);
        }
        return start;
    };

    // A sung note: one chord tone, held, bent into and shaken - what fills the
    // space before a phrase that comes in late, so a solo is never empty air.
    auto sing = [&] (int from, int to, const Chord& chord, int reg, double lift)
    {
        if (to - from < barTicks / 4) return;
        const std::vector<int> tones = ladder (chordPcs (chord), lowest, highest);
        if (tones.empty()) return;
        LeadIntent li;
        li.tick          = clampTick (from + static_cast<int> (rng.bipolar (humanize * 3.0)));
        li.durationTicks = to - from;
        li.pitch         = tones[static_cast<size_t> (nearestIndex (tones, reg + rng.range (-4, 4)))];
        li.accent        = std::min (0.98, 0.78 + s.intensity * 0.12 + lift);
        li.bendSemis     = rng.chance (0.6) ? 2 : 0;
        li.vibrato       = true;
        if (li.bendSemis == 0 && rng.chance (0.2 * how.gestures)) li.artic = LeadArtic::Harmonic;
        out.lead.push_back (li);
    };

    int recent[2] = { -1, -1 };
    auto choose = [&] (SoloRole role) -> int
    {
        double w[kNumSoloLicks], total = 0.0;
        for (int i = 0; i < kNumSoloLicks; ++i)
        {
            const SoloLick& sl = kSoloLicks[i];
            w[i] = 0.0;
            if ((sl.roles & role) == 0) continue;
            Rng probe (deriveSeed (songSeed, 0x5010Du + static_cast<uint32_t> (i)));
            const Lick L = sl.make (probe);
            // Triplets are how a player phrases over a shuffle - allowed here.
            w[i] = 0.3 + who.family[sl.family];
            {
                double ws = 0.0;
                for (int sc = 0; sc < kSchools; ++sc)
                    if (sl.schools & (1u << sc)) ws += school[sc];
                w[i] *= 0.02 + ws;   // outside the song's schools: all but never
            }
            // Over a shuffle, triplets season the middle of a solo and drive
            // its build - there they are how a blues player shreds.
            if (shuffle && L.triplet) w[i] *= (role == Develop) ? 0.5 : 0.8 + how.shred;
            if (role == Build)
                w[i] *= L.fast ? (0.4 + how.shred * 1.6) * (hot ? 1.3 : 1.0) * byIntuition (iq, 0.5, 1.0, 1.3)
                               : (1.6 - how.shred);
            // Fast playing is not only for the build: a shredder's middle has
            // runs in it too, between the sung ideas.
            if (role == Develop && L.fast) w[i] *= how.shred * 1.5;
            if (i == recent[0] || i == recent[1]) w[i] *= 0.06;
            w[i] *= busyWeight (L, busyTrim);
            total += w[i];
        }
        if (total <= 0.0) return -1;
        double pick = rng.unit() * total;
        for (int i = 0; i < kNumSoloLicks; ++i)
            if ((pick -= w[i]) < 0.0) { recent[1] = recent[0]; recent[0] = i; return i; }
        return -1;
    };
    auto build = [&] (int i) { Rng lr (deriveSeed (static_cast<uint32_t> (rng.next()), 0x50A1u)); return kSoloLicks[i].make (lr); };

    // ---- the phrases ----
    std::vector<std::pair<int, int>> slots;   // first bar, bars
    for (int b = 0; b < s.bars; ) { const int len = (s.bars - b >= 2) ? 2 : 1; slots.push_back ({ b, len }); b += len; }
    const int K = static_cast<int> (slots.size());

    const Lick motif = makeMotif (rng, shuffle, school, how.busy);
    const double motifBack = rng.unit();      // where in the middle it returns, if it does
    bool motifReturned = false;
    int lastEnd = sectionStartTick;
    std::vector<LeadIntent> firstPhrase;      // the opening phrase as played, for an AAB repeat

    for (int k = 0; k < K; ++k)
    {
        const double t     = K > 1 ? k / double (K - 1) : 1.0;
        const bool   final = (k == K - 1);
        const int slotStart = sectionStartTick + slots[static_cast<size_t> (k)].first * barTicks;
        const int bars      = slots[static_cast<size_t> (k)].second;
        const int slotEnd   = slotStart + bars * barTicks;
        const int lastBar   = slots[static_cast<size_t> (k)].first + bars - 1;
        const Chord& landChord = chordAt (lastBar);

        // The register rises through the solo; the dynamics with it.
        const int  reg  = lowest + static_cast<int> ((highest - lowest) * (0.28 + how.climb * t + (final && K > 1 ? 0.10 : 0.0)));
        const double lift = -0.05 + 0.13 * t;

        // AAB: the opening phrase, said again note for note - then answered.
        // The blues' own shape, and a strong one anywhere the chord allows it:
        // in a shuffle always a candidate, elsewhere only over the same chord.
        if (k == 1 && bars == 2 && slots[0].second == 2 && ! firstPhrase.empty())
        {
            const bool sameChord = chordAt (lastBar).rootPc == chordAt (slots[0].first + 1).rootPc;
            if (rng.chance (shuffle ? 0.8 : (sameChord ? 0.5 : 0.0)))
            {
                const size_t before = out.lead.size();
                for (LeadIntent li : firstPhrase)
                {
                    li.tick += slotStart - sectionStartTick;
                    li.accent = std::min (0.97, li.accent + 0.03);   // said again, a touch harder
                    if (li.tick < slotEnd) out.lead.push_back (li);
                }
                for (size_t q = before; q < out.lead.size(); ++q)
                    lastEnd = std::max (lastEnd, out.lead[q].tick + out.lead[q].durationTicks);
                motifReturned = true;   // said twice already: it does not come back as well
                continue;
            }
        }

        // What this phrase is FOR.
        Lick L;
        bool isMotif = false;
        SoloRole role = Develop;
        // Said, then (in a long solo) said again over the next chord; and in a
        // long solo it may come back once in the middle. In a short one, once
        // is the idea and the rest develops it - three statements in eight
        // bars was a solo that repeated itself.
        if (k == 0 || (k == 1 && K >= 6))
        { L = motif; isMotif = true; }
        else if (! final && K >= 6 && k >= 3 && ! motifReturned && t >= 0.35 + motifBack * 0.2 && t < 0.75
                 && rng.chance (how.motifReturn))
        { L = motif; isMotif = true; motifReturned = true; }
        else
        {
            // When the build starts is the player's: a singer builds late, a
            // shredder is under way by the second phrase.
            role = final ? Climax : (t < 0.5 - 0.3 * how.shred ? Develop : Build);
            const int i = choose (role);
            if (i < 0) continue;
            L = build (i);
            if (! L.triplet) vary (L, rng, who, shuffle);
        }

        // Where it lands, how long the landing sings, and the breath after.
        // The landing is HELD, but not for the rest of the phrase: the first
        // version held every landing to the next phrase and a solo came out as
        // a few long notes with a short lick between each.
        const int earliest = std::max (sectionStartTick, k > 0 ? std::max (lastEnd, slotStart - barTicks / 8) : slotStart);
        int landing;
        if (isMotif && ! final)
        {
            // The idea comes in promptly: on the first strong beat it fits by.
            landing = slotStart + bars * barTicks - barTicks / 2;
            for (int cand = slotStart + barTicks / 2; cand < slotStart + bars * barTicks; cand += barTicks / 2)
                if (cand - L.notes.back().on >= earliest) { landing = cand; break; }
        }
        else if (final)     landing = bars == 2 ? slotStart + barTicks : slotStart + barTicks / 2;
        else if (bars == 2) landing = slotStart + barTicks + (rng.chance (0.5) ? 0 : barTicks / 2);
        else                landing = slotStart + barTicks / 2;
        // A shuffle's phrases breathe shorter - the blues talks, it does not wait.
        const int breaths[3] = { barTicks / 8, shuffle ? barTicks / 8 : barTicks / 4,
                                 shuffle ? barTicks / 4 : (barTicks * 3) / 8 };
        const int breath  = breaths[rng.below (3)];
        // A shuffle phrases in more, shorter breaths - held less, said more.
        const int holdBase = shuffle ? (barTicks * 3) / 8
                           : (isMotif || role == Build) ? barTicks / 2 : (barTicks * 3) / 4;
        const int holdCap  = std::max (barTicks / 8, static_cast<int> (holdBase * (1.3 - how.busy * 0.6)
                                                                       * (1.0 - busyTrim * 0.7)));
        const int holdTo  = final ? sectionEnd - barTicks / 16
                                  : std::min (slotEnd - std::max (barTicks / 8, breath), landing + holdCap);

        const size_t before = out.lead.size();
        int start = play (L, landing, holdTo, earliest, landChord, reg, ! isMotif, lift, final);

        // CALL AND ANSWER: after the motif, in the same two bars, a lick
        // answers it - landing on the second bar's third beat.
        if (isMotif && ! final && bars == 2 && start >= 0)
        {
            const int from  = landing + holdCap + barTicks / 16;
            const int land2 = slotStart + barTicks + barTicks / 2;
            if (land2 - from >= barTicks / 4)
            {
                const int j = choose (Develop);
                if (j >= 0)
                {
                    Lick A = build (j);
                    if (! A.triplet) vary (A, rng, who, shuffle);
                    play (A, land2, slotEnd - std::max (barTicks / 8, breath), from, landChord, reg + 2, true, lift, false);
                }
            }
        }

        // Room before it: more ideas run into it - in the build always, in the
        // middle mostly - each landing briefly just ahead of the next. What is
        // left over, if a half bar or more, the guitar sings across.
        {
            const Chord& firstChord = chordAt (slots[static_cast<size_t> (k)].first);
            // A phrase is a line of ideas, chained back to the room's start - a
            // lick runs into the next one's landing - not one idea and a wait.
            // A busy player fills even an eighth of room; a sparse one leaves a quarter.
            const int minRoom = busyTrim < -0.3 ? barTicks
                              : (how.busy > 0.7 || busyTrim > 0.3) ? barTicks / 8 : barTicks / 4;
            const int maxMore = busyTrim > 0.3 ? 7 : 4;
            for (int more = 0; more < maxMore && start >= 0 && start - earliest >= minRoom; ++more)
            {
                // Not before the motif's FIRST statement: the solo's idea is
                // stated on its own. Said again later, it can be run into.
                if (isMotif && k == 0) break;
                const int j = choose (role == Develop ? Develop : Build);
                if (j < 0) break;
                Lick L2 = build (j);
                if (! L2.triplet) vary (L2, rng, who, shuffle);
                const int land2 = start - barTicks / 4;
                const int s2 = play (L2, land2, start - barTicks / 16, earliest, firstChord, reg - 3, true, lift, false);
                if (s2 < 0) break;
                start = s2;
            }
            if (start >= 0 && start - earliest >= barTicks / 2)
                sing (earliest, start - barTicks / 16, firstChord, reg - 2, lift);
        }

        for (size_t q = before; q < out.lead.size(); ++q)
            lastEnd = std::max (lastEnd, out.lead[q].tick + out.lead[q].durationTicks);

        // THE TAG. A phrase lands, holds, and used to leave the rest of its
        // bar empty until the next one began - 0.8 of a bar of nothing, every
        // time, in a blues solo the owner called "boring, empty". Where half a
        // bar or more is left, a short answer fills it: call and response
        // inside the phrase, as a blues player does without thinking.
        if (! final)
        {
            const int tagFrom = lastEnd + barTicks / 16;
            const int tagEnd  = slotEnd - std::max (barTicks / 8, breath);
            // BUSY down leaves the space; up fills even a quarter of a bar.
            const int tagRoom = busyTrim > 0.3 ? barTicks / 4 : (barTicks * 3) / 8;
            if (busyTrim > -0.3 && tagEnd - tagFrom >= tagRoom)
            {
                const int j = choose (role == Develop ? Develop : Build);
                if (j >= 0)
                {
                    Lick T = build (j);
                    if (! T.triplet) vary (T, rng, who, shuffle);
                    const int land = tagEnd - barTicks / 8;
                    if (play (T, land, tagEnd, tagFrom, chordAt (lastBar), reg + 1, true, lift, false) >= 0)
                        for (size_t q = before; q < out.lead.size(); ++q)
                            lastEnd = std::max (lastEnd, out.lead[q].tick + out.lead[q].durationTicks);
                }
            }
        }

        if (k == 0)
            for (size_t q = before; q < out.lead.size(); ++q)
                if (out.lead[q].tick < slotEnd) firstPhrase.push_back (out.lead[q]);
    }

    // One note at a time.
    std::sort (out.lead.begin() + static_cast<long> (firstOfSection), out.lead.end(),
               [] (const LeadIntent& a, const LeadIntent& b) { return a.tick < b.tick; });
    for (size_t i = firstOfSection; i + 1 < out.lead.size(); )
    {
        const int gap = out.lead[i + 1].tick - out.lead[i].tick;
        if (gap <= 0) { out.lead.erase (out.lead.begin() + static_cast<long> (i + 1)); continue; }
        out.lead[i].durationTicks = std::min (out.lead[i].durationTicks, gap);
        ++i;
    }
}


// Arrangement decisions expressed as control moves rather than notes: more
// drive where the section is loud, brighter where it leads, the effect brought
// in for the biggest moments and pulled out of the quiet ones. What each name
// actually reaches is the profile's business, and an unmapped one is ignored.
static void addControls (const ControlSet* set, const std::string& profileId,
                         std::vector<ControlIntent>& out, int tick,
                         double intensity, bool leading, const std::string& role,
                         double throughSong, uint32_t sectionSeed, uint32_t songSeed,
                         const std::string& style = std::string(),
                         const std::map<std::string, double>* pins = nullptr)
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
        if (def.follows == "none" && (pins == nullptr || pins->count (def.name) == 0))
            continue;

        // A level control belongs to the mix knob on the song screen, not to
        // the arrangement. Driving it here would mean the section fought the
        // knob for the same control, and the knob would appear not to work.
        if (def.follows == "level")
            continue;

        // The song pins this one: its approved sound, sent as it is.
        if (pins != nullptr)
        {
            const auto pin = pins->find (def.name);
            if (pin != pins->end())
            {
                ControlIntent c;
                c.tick    = tick;
                c.control = def.name;
                c.amount  = pin->second;
                c.pinned  = true;
                out.push_back (c);
                continue;
            }
        }

        double t = 0.5;

        const bool rollsEachSection = def.follows == "random";
        const bool rollsOncePerSong  = def.follows == "random once";
        const bool byStyle           = def.follows == "style";

        if (byStyle)
        {
            // The song's style's list, else "default", else leave it alone.
            const std::vector<double>* list = nullptr;
            const std::vector<double>* fallback = nullptr;
            for (const auto& entry : def.picks)
            {
                std::string keys = "," + entry.first + ",";
                keys.erase (std::remove (keys.begin(), keys.end(), ' '), keys.end());
                if (keys.find ("," + style + ",") != std::string::npos) { list = &entry.second; break; }
                if (keys == ",default,") fallback = &entry.second;
            }
            if (list == nullptr) list = fallback;
            if (list == nullptr || list->empty())
                continue;

            // One of them for the whole song, from the control's own stream:
            // a reroll may choose another, and no note anywhere moves.
            Rng pickRng (deriveSeed (songSeed, hashString (profileId + "/" + def.name + "/style")));
            const double v = (*list)[static_cast<size_t> (pickRng.below (static_cast<int> (list->size())))];

            ControlIntent c;
            c.tick    = tick;
            c.control = def.name;
            c.amount  = def.valueAt (v);
            out.push_back (c);
            continue;
        }

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

// Guitar 1 around guitar 2, within one section. See the call site.
static void twoGuitars (PhrasePart& g1, size_t g1ChordsFrom, size_t g1LeadFrom,
                        const PhrasePart& g2, size_t g2LeadFrom,
                        PhraseFeel g2Feel, const std::string& style, int keyPc, Mode mode)
{
    (void) g1LeadFrom;
    auto& chords = g1.chords;

    // Drop guitar 1's strums starting inside [from, to) and stop anything of
    // its that is still ringing at `from`.
    const auto clearSpan = [&chords, g1ChordsFrom] (int from, int to, bool holdInstead)
    {
        ChordIntent* sounding = nullptr;
        for (size_t c = g1ChordsFrom; c < chords.size(); ++c)
            if (chords[c].tick <= from + 30) sounding = &chords[c];

        std::vector<ChordIntent> kept (chords.begin(), chords.begin() + static_cast<std::ptrdiff_t> (g1ChordsFrom));
        for (size_t c = g1ChordsFrom; c < chords.size(); ++c)
        {
            ChordIntent ch = chords[c];
            const bool inside = ch.tick > from + 30 && ch.tick < to;
            if (inside) continue;
            if (&chords[c] == sounding)
            {
                if (holdInstead) ch.durationTicks = std::max (ch.durationTicks, to - ch.tick);
                else if (ch.tick < from) ch.durationTicks = std::max (1, std::min (ch.durationTicks, from - ch.tick));
                else continue;   // a strum right on the twin line's first note: the harmony replaces it
            }
            kept.push_back (ch);
        }
        chords.swap (kept);
    };

    // ---- 1. TWIN HARMONY: the lines written for two guitars, in the styles
    // that have twin guitars (Maiden, Lizzy, Priest, Megadeth).
    static const char* twinStyles[] = { "metal", "hard_rock", "prog_metal", "thrash", "groove_metal", "doom" };
    const bool twinStyle = std::any_of (std::begin (twinStyles), std::end (twinStyles),
                                        [&style] (const char* s) { return style == s; });

    if (twinStyle && g2Feel == PhraseFeel::Solo)
    {
        const std::vector<int>& scale = soloScale (mode, style);
        const auto inKey = [&] (int p)
        {
            const int pc = ((p - keyPc) % 12 + 12) % 12;
            return std::find (scale.begin(), scale.end(), pc) != scale.end();
        };
        // A diatonic third below: whichever of three or four semitones down is
        // in the key; a passing note keeps the interval the line last used.
        int lastInterval = 3;
        const auto thirdBelow = [&] (int p)
        {
            if (inKey (p - 3) && ! inKey (p - 4)) lastInterval = 3;
            else if (inKey (p - 4) && ! inKey (p - 3)) lastInterval = 4;
            return p - lastInterval;
        };

        // And the solo's LAST phrase, whatever it was built from: the big
        // close is where a twin-guitar band harmonises, so every solo in these
        // styles ends on one - the twin licks add more along the way.
        size_t lastPhraseFrom = g2.lead.size();
        {
            size_t k = g2.lead.size();
            while (k > g2LeadFrom && g2.lead[k - 1].bed) --k;
            if (k > g2LeadFrom)
            {
                size_t a = k - 1;
                while (a > g2LeadFrom && ! g2.lead[a - 1].bed
                       && g2.lead[a].tick - (g2.lead[a - 1].tick + g2.lead[a - 1].durationTicks) < 240)
                    --a;
                lastPhraseFrom = a;
            }
        }
        const auto harmonised = [&] (size_t k)
        {
            return ! g2.lead[k].bed && (g2.lead[k].twin || k >= lastPhraseFrom);
        };

        size_t i = g2LeadFrom;
        while (i < g2.lead.size())
        {
            if (! harmonised (i)) { ++i; continue; }
            size_t j = i;
            while (j + 1 < g2.lead.size() && harmonised (j + 1)
                   && g2.lead[j + 1].tick - (g2.lead[j].tick + g2.lead[j].durationTicks) < 240)
                ++j;

            const int from = g2.lead[i].tick;
            const int to   = g2.lead[j].tick + g2.lead[j].durationTicks;
            clearSpan (from, to, false);

            for (size_t k = i; k <= j; ++k)
            {
                LeadIntent h = g2.lead[k];
                h.pitch  = thirdBelow (h.pitch);
                h.accent = h.accent * 0.92;
                h.artic  = LeadArtic::Normal;      // the rhythm guitar has no Hydra keys
                h.glide = h.neckSlideIn = h.slideOff = h.fretNoise = false;
                h.thrashRepeats = 0;
                h.pick  = LeadIntent::Pick::Auto;
                h.twin  = false;
                g1.lead.push_back (h);
            }
            i = j + 1;
        }
    }

    // ---- 2. GUITAR 1 MAKES ROOM. Under guitar 2's answers it holds the chord
    // instead of strumming over them; under a solo it plays a little softer.
    if (g2Feel == PhraseFeel::Fills)
    {
        size_t i = g2LeadFrom;
        while (i < g2.lead.size())
        {
            if (g2.lead[i].bed) { ++i; continue; }
            size_t j = i;
            while (j + 1 < g2.lead.size() && ! g2.lead[j + 1].bed
                   && g2.lead[j + 1].tick - (g2.lead[j].tick + g2.lead[j].durationTicks) < 240)
                ++j;
            clearSpan (g2.lead[i].tick, g2.lead[j].tick + g2.lead[j].durationTicks, true);
            i = j + 1;
        }
    }
    else if (g2Feel == PhraseFeel::Solo)
    {
        for (size_t c = g1ChordsFrom; c < chords.size(); ++c)
            chords[c].accent *= 0.88;
    }
}

// SHRED (2026-10-02, the owner: "i need to know they actually change things
// when adjusted as intended"). It used to reweight which licks a solo drew,
// which re-rolled the whole solo: on a third of the songs turning it UP made
// the solo SLOWER (Brass Hour 58% -> 22% of notes at sixteenth speed). Now it
// EDITS the line that SHRED 0 plays - which is the approved one, untouched:
//   up    a held note before a gap becomes a sixteenth run stepping through
//         the key into the next note, more of them the further it goes;
//   down  runs of fast notes keep every other note, each held longer.
// Its own stream, so the base line never moves.
static void shredEdit (std::vector<LeadIntent>& lead, size_t from, double shred,
                       int keyPc, Mode mode, const std::string& style,
                       const PhraseProfile* profile, uint32_t seed)
{
    if (shred == 0.0 || from >= lead.size())
        return;

    Rng r (seed);
    const int lowest  = std::max (profile != nullptr ? profile->chordLowest  : 40, 50);
    const int highest = std::min (profile != nullptr ? profile->chordHighest : 96, 88);
    const std::vector<int>& scale = soloScale (mode, style);
    const auto inKey = [&] (int p)
    {
        const int pc = ((p - keyPc) % 12 + 12) % 12;
        return scale.empty() || std::find (scale.begin(), scale.end(), pc) != scale.end();
    };
    const auto step = [&] (int p, int dir)   // the next note of the key, up or down
    {
        for (int k = 1; k <= 3; ++k)
            if (inKey (p + dir * k)) return p + dir * k;
        return p + dir * 2;
    };

    std::vector<LeadIntent> out (lead.begin(), lead.begin() + static_cast<std::ptrdiff_t> (from));

    if (shred > 0.0)
    {
        for (size_t i = from; i < lead.size(); ++i)
        {
            const LeadIntent& n = lead[i];
            const bool haveNext = i + 1 < lead.size();
            const int  room     = haveNext ? lead[i + 1].tick - n.tick : 0;
            const int  sixteenth = 120;
            const int  k        = std::clamp ((room - 240) / sixteenth, 3, 3 + static_cast<int> (shred * 4.0));
            const int  runStart = haveNext ? lead[i + 1].tick - k * sixteenth : 0;

            // Where a player fills in: a held note, or a rest after a note,
            // before the next one. A landing keeps at least a beat and a half,
            // a bend the time to arrive.
            const bool restAfter = room - n.durationTicks >= 360;
            const bool candidate = haveNext && ! n.bed && ! lead[i + 1].bed && n.thrashRepeats == 0
                                && n.artic == LeadArtic::Normal && room >= 480
                                && (n.durationTicks >= 360 || restAfter)
                                && runStart - n.tick >= (n.target ? 720 : n.bendSemis > 0 ? 480 : 120);
            if (! candidate || ! r.chance (std::min (1.0, shred)))
            {
                out.push_back (n);
                continue;
            }

            // Held for the first part (or as written, before a rest), then a
            // run of sixteenths walking into the next note.
            const int target = lead[i + 1].pitch;
            const int dir    = n.pitch <= target ? 1 : -1;   // approach from the side we come from

            LeadIntent head = n;
            head.durationTicks = std::max (60, std::min (n.durationTicks, runStart - n.tick));
            head.slideOff = false;
            out.push_back (head);

            std::vector<int> pitches;
            int p = target;
            for (int q = 0; q < k; ++q) { p = step (p, -dir); pitches.push_back (p); }
            std::reverse (pitches.begin(), pitches.end());

            for (int q = 0; q < k; ++q)
            {
                const int pitch = std::clamp (pitches[static_cast<size_t> (q)], lowest, highest);
                LeadIntent rn;
                rn.tick          = runStart + q * sixteenth;
                rn.durationTicks = sixteenth;
                rn.pitch         = pitch;
                rn.accent        = std::min (1.0, n.accent * 0.94 + (q == 0 ? 0.04 : 0.0));
                rn.slur          = q % 2 == 1;   // picked and hammered, a real run
                rn.unswung       = true;          // a run is its own time
                out.push_back (rn);
            }
        }
    }
    else
    {
        const double thin = std::min (1.0, -shred);
        size_t i = from;
        while (i < lead.size())
        {
            size_t j = i;
            while (j + 1 < lead.size() && ! lead[j + 1].bed && ! lead[j].bed
                   && lead[j + 1].tick - lead[j].tick <= 130)
                ++j;

            if (j - i + 1 >= 3 && r.chance (thin))
            {
                // Every other note of the run, the landing always kept.
                for (size_t q = i; q <= j; ++q)
                {
                    const bool keep = (q - i) % 2 == 0 || q == j || lead[q].target;
                    if (! keep) continue;
                    LeadIntent kept = lead[q];
                    size_t nx = q + 1;
                    while (nx <= j && ! ((nx - i) % 2 == 0 || nx == j || lead[nx].target)) ++nx;
                    if (nx <= j) kept.durationTicks = std::max (kept.durationTicks, lead[nx].tick - kept.tick);
                    kept.slur = false;
                    out.push_back (kept);
                }
            }
            else
            {
                for (size_t q = i; q <= j; ++q) out.push_back (lead[q]);
            }
            i = j + 1;
        }
    }

    lead.swap (out);
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
                                PhrasePart& out,
                                double fillAmount,
                                double iq,
                                double complexity,
                                double busyTrim,
                                double shredTrim,
                                const std::map<std::string, double>* pins)
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
                 throughSong, sectionSeed, songSeed, style, pins);

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

        // FILLS: the lick engine above. SOLOS: exactly as approved - their own
        // generator, and none of the per-note articulations it asks for, which
        // were never sounded on this rig (the map was empty) and stay silent
        // until the owner says otherwise.
        if (fills)
        {
            const size_t before = out.lead.size();
            generateFills (s, chords, sectionStartTick, barTicks, keyPc, mode, style,
                           swing, profile, humanize, soloRng, out, fillAmount, iq, songSeed, complexity, 0.0, busyTrim);
            shredEdit (out.lead, before, shredTrim * 0.5, keyPc, mode, style, profile,
                       deriveSeed (sectionSeed, 0x5ED0Fu));
            return;
        }

        // SOLOS v3 (2026-09-26), at the owner's word: "they need more
        // substance". The old generator stays below, unused, until this has
        // been heard.
        const size_t before = out.lead.size();
        generateLeadSolo (s, chords, sectionStartTick, barTicks, keyPc, mode, style,
                          swing, profile, humanize, soloRng, out, iq, songSeed, complexity, 0.0, busyTrim);
        shredEdit (out.lead, before, shredTrim, keyPc, mode, style, profile,
                   deriveSeed (sectionSeed, 0x5ED05u));
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
        std::vector<int> pattern = phraseDriven ? std::vector<int> { 0 }
                                                : chordRhythm (feel, rng);

        // BUSY. Up: passing hits in the gaps and a push on the and-of-four.
        // Down: hits dropped, the downbeat kept. Its own stream, so a trim of
        // zero draws nothing and changes nothing.
        if (! phraseDriven && busyTrim != 0.0 && ! pattern.empty())
        {
            Rng br (deriveSeed (sectionSeed, 0xB0517u + static_cast<uint32_t> (bar)));
            if (busyTrim > 0.0)
            {
                std::vector<int> more;
                for (size_t h = 0; h < pattern.size(); ++h)
                {
                    more.push_back (pattern[h]);
                    const int nx = h + 1 < pattern.size() ? pattern[h + 1] : 16;
                    if (nx - pattern[h] >= 4 && br.chance (busyTrim * 0.7))
                        more.push_back (pattern[h] + (nx - pattern[h]) / 2);
                }
                if (more.back() < 13 && br.chance (busyTrim * 0.5))
                    more.push_back (14);
                pattern = more;
            }
            else
            {
                std::vector<int> fewer { pattern.front() };
                for (size_t h = 1; h < pattern.size(); ++h)
                    if (! br.chance (-busyTrim * 0.7))
                        fewer.push_back (pattern[h]);
                pattern = fewer;
            }
        }

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

    // Lead notes written in triplet time are already swung - kept and unmoved.
    const auto thinLead = [&onGrid] (std::vector<LeadIntent>& v)
    {
        v.erase (std::remove_if (v.begin(), v.end(),
                                 [&onGrid] (const LeadIntent& e) { return ! e.unswung && ! onGrid (e.tick); }),
                 v.end());
    };

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
        thinLead (perf.guitar.lead);
        thin (perf.guitar2.chords);
        thinLead (perf.guitar2.lead);
        thinLead (perf.piano.lead);
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

    // Guitar 2 too. It was left out when it was added: in every shuffle song
    // the whole band swung and the lead guitar played dead straight against it.
    for (PhrasePart* part : { &perf.guitar, &perf.guitar2, &perf.piano })
    {
        for (ChordIntent& c : part->chords)
            warpHeld (c.tick, c.durationTicks);

        for (LeadIntent& n : part->lead)
            if (! n.unswung)
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

    // A line holding both swung notes and triplet ones can come out of the
    // warp crossed - a swung offbeat pushed past the triplet after it. One
    // note at a time again, in order, each ending where the next begins.
    for (PhrasePart* part : { &perf.guitar, &perf.guitar2, &perf.piano })
    {
        auto& v = part->lead;
        if (std::none_of (v.begin(), v.end(), [] (const LeadIntent& n) { return n.unswung; }))
            continue;
        std::stable_sort (v.begin(), v.end(), [] (const LeadIntent& a, const LeadIntent& b) { return a.tick < b.tick; });
        for (size_t i = 0; i + 1 < v.size(); )
        {
            const int gap = v[i + 1].tick - v[i].tick;
            if (gap <= 0) { v.erase (v.begin() + static_cast<long> (i + 1)); continue; }
            v[i].durationTicks = std::min (v[i].durationTicks, gap);
            ++i;
        }
    }
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
        // The song's complexity, NOT trimmed by BUSY: the trim reweighted the
        // rolls and could land on a sparser groove. BUSY acts directly instead
        // (chooseGroove, the pickup kick).
        ctx.complexity  = plan.complexity;
        ctx.swung       = plan.swing > 0.0;
        ctx.busyTrim    = plan.busy[0];
        ctx.humanize    = plan.humanize;
        ctx.intuition   = plan.intuition;
        ctx.style       = plan.style;
        ctx.role        = s.role;
        ctx.beatTicks   = beatTicks;
        ctx.beatsPerBar = plan.timeSigNumerator;
        ctx.barTicks    = barTicks;
        ctx.lowestBassNote  = lowestBass;
        ctx.highestBassNote = highestBass;

        const SectionGroove groove = buildSectionGroove (ctx, rng);

        // The bass reads its own complexity; the grid it locks to is the drums'.
        GrooveContext ctxBass = ctx;
        ctxBass.complexity = trimmedComplexity (plan.complexity, plan.busy[1]);
        ctxBass.busyTrim   = plan.busy[1];

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
                                          ? chooseBassPattern (ctxBass, rng)
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
                         && rng.chance (ctx.complexity * 0.35))
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

                const size_t bassBefore = result.performance.bass.size();
                generateBassBar (ctxBass, grid, barStart, chord, next, bassPattern,
                                 lastBar, rng, result.performance.bass);

                // BUSY down: notes between the beats dropped, the beats kept.
                // Its own stream - at zero it draws nothing.
                if (plan.busy[1] < 0.0)
                {
                    Rng br (deriveSeed (sectionSeed, 0xBA55u + static_cast<uint32_t> (bar)));
                    auto& b = result.performance.bass;
                    for (size_t q = bassBefore; q < b.size(); )
                    {
                        const bool onBeat = ((b[q].tick - barStart) % beatTicks) < beatTicks / 16
                                         || ((b[q].tick - barStart) % beatTicks) > beatTicks - beatTicks / 16;
                        if (! onBeat && br.chance (-plan.busy[1] * 0.7))
                            b.erase (b.begin() + static_cast<long> (q));
                        else
                            ++q;
                    }
                }

                // BUSY up, DRIVEN (2026-10-02: up added only ~5%). A note held
                // a beat or longer is split: the root on the beat, a push on
                // the off-beat - the octave, or the root again - the way a
                // bassist leans into a part. Swung eighths in a shuffle land on
                // its grid. Its own stream; at zero it draws nothing.
                if (plan.busy[1] > 0.0)
                {
                    Rng dr (deriveSeed (sectionSeed, 0xBA57u + static_cast<uint32_t> (bar)));
                    auto& bb = result.performance.bass;
                    const int eighth = beatTicks / 2;
                    const int pushAt = plan.swing ? (beatTicks * 2) / 3 : eighth;
                    std::vector<BassIntent> added;
                    for (size_t q = bassBefore; q < bb.size(); ++q)
                    {
                        BassIntent& b = bb[q];
                        if (b.artic == BassArtic::Dead || b.durationTicks < beatTicks - beatTicks / 8) continue;
                        if (! dr.chance (std::min (0.95, plan.busy[1] * 0.9))) continue;
                        BassIntent push = b;
                        push.tick          = b.tick + pushAt;
                        push.durationTicks = std::max (1, b.durationTicks - pushAt);
                        push.pitch         = dr.chance (0.5) ? b.pitch + 12 : b.pitch;
                        push.accent        = std::max (0.3, b.accent * 0.85);
                        b.durationTicks    = pushAt - 6;
                        added.push_back (push);
                    }
                    bb.insert (bb.end(), added.begin(), added.end());
                    std::stable_sort (bb.begin() + static_cast<std::ptrdiff_t> (bassBefore), bb.end(),
                                      [] (const BassIntent& x, const BassIntent& y) { return x.tick < y.tick; });

                    // And MOVEMENT where the line is already all eighths (a
                    // shuffle's walking bass has nothing long to split): a
                    // repeated root becomes the octave, or the fifth on beat
                    // three - a busier bassist moves more, not only plays more.
                    for (size_t q = bassBefore + 1; q < bb.size(); ++q)
                    {
                        BassIntent& b = bb[q];
                        if (b.artic == BassArtic::Dead || b.pitch != bb[q - 1].pitch) continue;
                        const int inBar = (b.tick - barStart) % barTicks;
                        if (inBar < beatTicks / 8) continue;   // the downbeat stays the root
                        if (! dr.chance (std::min (0.9, plan.busy[1] * 0.12))) continue;
                        const bool beatThree = std::abs (inBar - 2 * beatTicks) < beatTicks / 8;
                        b.pitch += beatThree ? 7 : 12;
                    }
                }

                // BUSY up: an APPROACH NOTE on the bar's last eighth, a semitone
                // into the next chord's root - what a bassist adds to walk into
                // a change. On the eighth, so a shuffle keeps it (the dead notes
                // BUSY added before fell between eighths and were swung away:
                // 1248 -> 1271 notes). Its own stream; at zero it draws nothing.
                auto& bl = result.performance.bass;
                if (plan.busy[1] > 0.0 && bl.size() > bassBefore && (! lastBar || ! isLastSection))
                {
                    Rng br (deriveSeed (sectionSeed, 0xBA56u + static_cast<uint32_t> (bar)));
                    if (br.chance (std::min (0.95, plan.busy[1] * 0.95)))
                    {
                        const int eighth = beatTicks / 2;
                        const int at     = barStart + barTicks - eighth;
                        int ref = bl.back().pitch;
                        for (size_t q = bassBefore; q < bl.size(); ++q)
                            if (bl[q].tick < at) ref = bl[q].pitch;

                        // Make room: nothing of this bar may start on or after it.
                        for (size_t q = bassBefore; q < bl.size(); )
                        {
                            if (bl[q].tick >= at) bl.erase (bl.begin() + static_cast<long> (q));
                            else { bl[q].durationTicks = std::min (bl[q].durationTicks, at - bl[q].tick); ++q; }
                        }

                        int target = ref;
                        for (int d = 0; d <= 6; ++d)
                        {
                            if (((ref + d) % 12 + 12) % 12 == next.rootPc) { target = ref + d; break; }
                            if (((ref - d) % 12 + 12) % 12 == next.rootPc) { target = ref - d; break; }
                        }
                        int approach = target + (br.chance (0.6) ? -1 : 1);
                        if (approach < lowestBass)  approach += 2;
                        if (approach > highestBass) approach -= 2;

                        BassIntent a;
                        a.tick          = at;
                        a.durationTicks = eighth - eighth / 8;
                        a.pitch         = approach;
                        a.accent        = 0.72;
                        bl.push_back (a);
                    }
                }
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
                                   int& count, std::string& feelName,
                                   int* leadCount = nullptr)
            {
                const size_t before     = out.chords.size();
                const size_t leadBefore = out.lead.size();
                const int part = (&out == &result.performance.guitar)  ? 2
                               : (&out == &result.performance.guitar2) ? 3 : 4;
                const auto pinned = plan.sound.find (part == 2 ? "guitar" : part == 3 ? "guitar2" : "piano");
                generatePhrasePart (s, chords, tick, barTicks, keyPc, mode, plan.style,
                                    plan.swing, profile,
                                    supports ? supportFeel (feel) : feel,
                                    plan.humanize, supports, throughSong,
                                    sectionSeed, plan.seed, rng, out, plan.fills,
                                    plan.intuition,
                                    trimmedComplexity (plan.complexity, plan.busy[part]),
                                    plan.busy[part], plan.shredTrim,
                                    pinned != plan.sound.end() ? &pinned->second : nullptr);
                count = static_cast<int> (out.chords.size() - before);
                if (leadCount != nullptr)
                    *leadCount = static_cast<int> (out.lead.size() - leadBefore);
                feelName = std::string (phraseFeelName (supports ? supportFeel (feel) : feel))
                         + (feel == PhraseFeel::Silent ? std::string() : role (supports));
            };

            const size_t g1ChordsBefore = result.performance.guitar.chords.size();
            const size_t g1LeadBefore   = result.performance.guitar.lead.size();
            const size_t g2LeadBefore   = result.performance.guitar2.lead.size();

            if (playGuitar)
                play (guitar, guitarFeel, guitarSupports, result.performance.guitar,
                      report.guitarChords, report.guitarFeel, &report.guitarNotes);

            if (playGuitar2)
                play (guitar2, guitar2Feel, guitar2Supports, result.performance.guitar2,
                      report.guitar2Chords, report.guitar2Feel, &report.guitar2Notes);

            // ---- THE TWO GUITARS TOGETHER (2026-09-27, the owner chose twin
            // harmony and guitar 1 making room). Guitar 2's notes are never
            // touched here - only guitar 1 moves - so an approved solo stays
            // exactly as approved. No random draws either.
            if (playGuitar && playGuitar2 && guitarFeel != PhraseFeel::Silent
                && guitar2Feel != PhraseFeel::Silent)
                twoGuitars (result.performance.guitar, g1ChordsBefore, g1LeadBefore,
                            result.performance.guitar2, g2LeadBefore,
                            guitar2Feel, plan.style, keyPc, mode);

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
