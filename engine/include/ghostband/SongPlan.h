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

    // "full", "none", or a list: "drums+bass", "drums,guitar". Parsed into the
    // flags below, which are what the renderer actually reads.
    std::string plays       = "full";
    bool playsDrums  = true;
    bool playsBass   = true;
    bool playsGuitar = true;
    bool playsGuitar2 = true;
    bool playsPiano  = true;

    // auto/silent/sparse/muted/driving/open/busy. "auto" lets the section's
    // intensity and feel choose.
    std::string guitarPhrase  = "auto";
    std::string guitar2Phrase = "auto";
    std::string pianoPhrase   = "auto";

    // Which chordal part is out front here: auto/guitar/guitar2/piano/both.
    // "auto" weighs the section - a distorted guitar wins a loud one, a piano
    // wins a half-time or quiet one - which is right most of the time, but not
    // when you already know what you want. "both" lets them share, which is
    // occasionally what a big chorus wants and usually a mess.
    std::string lead = "auto";

    bool        vary        = true;         // false makes a repeated section identical

    // Bumped to reroll this section and only this section. It is folded into
    // the section's seed, and because every section derives its own RNG from
    // that seed, changing one provably cannot disturb another.
    unsigned    reroll      = 0;
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

    // Semitones to shift every chord, written or generated. Changing "key" on a
    // plan whose chords are spelled out has to move those chords too, otherwise
    // it silently does nothing at all.
    int transpose = 0;

    // How far the offbeats are pushed late, 0 straight to 1 full triplet.
    //
    // A shuffle is not a groove the generator writes - it is the same groove
    // played on a different grid - so it is applied as a warp of the finished
    // performance rather than threaded through every pattern. Zero leaves every
    // tick exactly where it was, which is why adding this changed no existing
    // song by a single note.
    double   swing = 0.0;

    double   complexity = 0.5;              // fills, ghost notes, busyness
    double   humanize   = 0.5;              // timing and velocity looseness

    // How often the answering guitar takes an opening it is offered.
    //
    // A fill can only land at the end of a four-bar group; this decides how
    // many of those it actually plays. 0 silences fills entirely without
    // having to edit every section, which is the point - "turn it off" should
    // be one control, not nine edits.
    //
    // 0.62 is what the feature was tuned and listened to at. It is a plan
    // field rather than a constant so a song can be busier or barer than that
    // without changing the engine.
    double   fills      = 0.62;

    // HOW MUCH THE BAND PLAYS WHAT IT FEELS LIKE RATHER THAN WHAT IS OBVIOUS.
    //
    // Low: the expected note in the expected place, the same way every time -
    // a fill lands on the fourth bar, dead on the half, drawn from the two or
    // three devices anybody learns first. High: it anticipates the hole instead
    // of waiting for it, varies an idea when it says it again, reaches for the
    // chord rather than the scale, and goes outside the key and comes back.
    //
    // It reaches every instrument, not only the lead: the bass takes the fifth
    // on a long note more often, the drums open the hat and lean on the ghost
    // notes. The same disposition, applied where each part has a choice.
    //
    // NOT A QUALITY CONTROL. A tight, literal band is the right sound for
    // plenty of music and it is what the low end gives you.
    //
    // 0.5 IS EXACTLY WHAT THE ENGINE DID BEFORE THIS EXISTED, which is what
    // lets it be added without changing a single note of the 34 preset songs
    // or moving the two reference pins. See byIntuition in Groove.h.
    double   intuition  = 0.5;
    unsigned seed       = 1;
    std::string ending  = "hard_stop";      // hard_stop/ritard/cymbal_ring/fade

    std::string drumProfile = "profiles/ssd5.json";
    std::string bassProfile = "profiles/modo-bass-2.json";

    // Empty means the part does not exist in this song at all. Guitar and piano
    // are opt-in precisely so that adding them to the engine leaves every plan
    // written before them rendering exactly as it did.
    std::string guitarProfile;
    std::string guitar2Profile;
    std::string pianoProfile;

    bool hasGuitar()  const { return ! guitarProfile.empty(); }
    bool hasGuitar2() const { return ! guitar2Profile.empty(); }
    bool hasPiano()  const { return ! pianoProfile.empty(); }

    std::vector<SectionPlan> sections;

    static bool load (const std::string& path, SongPlan& out, std::string& error);

    // Same, from text already in memory. The plugin ships a built-in plan so it
    // does something the moment it is added to a rackspace, rather than sitting
    // inert until someone finds a file.
    static bool parse (const std::string& text, const std::string& sourceName,
                       SongPlan& out, std::string& error);

    // Serialises back to the same format load() reads. Round-tripping a plan
    // through this must produce an identical song - the harness checks it - so
    // anything the loader reads has to be written here too.
    std::string toJson() const;

    // Non-fatal problems worth telling the user about before they hit play.
    // Everything worth saying about the song: things that are WRONG, and things
    // that are merely unusual. What the command line prints.
    std::vector<std::string> validate() const;

    // Only the things that are wrong.
    //
    // The two are separate because the plugin puts this on screen, and a
    // warning that fires on correct music is one nobody reads. "4 chords do not
    // divide evenly into 7 bars" is a typo in a rock song and the entire point
    // of a prog one - preset-prog-2 does it in seven sections deliberately, and
    // preset-emo once. Reporting those as faults would have meant two of the
    // thirty-four shipped songs opening with a warning about nothing.
    std::vector<std::string> faults() const;

private:
    std::vector<std::string> collectWarnings (bool includeAdvice) const;

public:
};

// "verse2" -> "verse". Lets the plan name sections naturally without also
// having to state the role every time.
std::string inferRole (const std::string& sectionName);

} // namespace gb
