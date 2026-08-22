#include "ghostband/SongPlan.h"
#include "ghostband/Json.h"
#include "ghostband/Music.h"

#include <cctype>

namespace gb {

static std::string toLower (std::string s)
{
    for (char& c : s)
        c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return s;
}

std::string inferRole (const std::string& sectionName)
{
    std::string s = toLower (sectionName);

    // Drop trailing digits and separators: "verse_2" and "verse2" both -> "verse".
    while (! s.empty() && (std::isdigit (static_cast<unsigned char> (s.back()))
                           || s.back() == '_' || s.back() == '-' || s.back() == ' '))
        s.pop_back();

    if (s == "intro"      || s == "count_in")  return "intro";
    if (s == "verse"      || s == "vs")        return "verse";
    if (s == "chorus"     || s == "hook")      return "chorus";
    if (s == "prechorus"  || s == "pre")       return "verse";
    if (s == "bridge"     || s == "middle")    return "bridge";
    if (s == "solo"       || s == "lead")      return "solo";
    if (s == "breakdown"  || s == "break")     return "breakdown";
    if (s == "outro"      || s == "ending")    return "ending";
    return s.empty() ? "verse" : s;
}

static void loadSection (const Json& j, SectionPlan& s, int index)
{
    s.name = j.stringOr ("name", "section" + std::to_string (index + 1));
    s.role = j.stringOr ("role", inferRole (s.name));

    s.bars      = j.intOr    ("bars", 8);
    s.intensity = j.numberOr ("intensity", 0.5);
    s.feel      = toLower (j.stringOr ("feel", "straight"));

    s.chords      = j.stringArray ("chords");
    s.bassPattern = toLower (j.stringOr ("bass", j.stringOr ("bass_pattern", "auto")));
    s.fill        = toLower (j.stringOr ("fill", "auto"));
    s.plays       = toLower (j.stringOr ("plays", "full"));
    s.vary        = j.boolOr  ("vary", true);

    if (s.intensity < 0.0) s.intensity = 0.0;
    if (s.intensity > 1.0) s.intensity = 1.0;
    if (s.bars < 1)        s.bars = 1;
}

static bool fromJson (const Json& j, const std::string& sourceName,
                      SongPlan& out, std::string& error)
{
    if (! j.isObject())
    {
        error = sourceName + ": top level must be a JSON object";
        return false;
    }

    out = SongPlan();

    out.title = j.stringOr ("title", "Untitled");
    out.key   = j.stringOr ("key", "E");
    out.mode  = toLower (j.stringOr ("mode", "natural_minor"));
    out.bpm   = j.numberOr ("bpm", 120.0);

    if (j.has ("time_signature") && j["time_signature"].isArray() && j["time_signature"].size() >= 2)
    {
        out.timeSigNumerator   = j["time_signature"][0].asInt (4);
        out.timeSigDenominator = j["time_signature"][1].asInt (4);
    }

    out.style      = toLower (j.stringOr ("style", "hard_rock"));
    out.bassTuning = toLower (j.stringOr ("bass_tuning", "standard"));
    out.playStyle  = toLower (j.stringOr ("play_style", "pick"));

    out.complexity = j.numberOr ("complexity", 0.5);
    out.humanize   = j.numberOr ("humanize", 0.5);
    out.seed       = static_cast<unsigned> (j.intOr ("seed", 1));
    out.ending     = toLower (j.stringOr ("ending", "hard_stop"));

    out.drumProfile = j.stringOr ("drum_profile", out.drumProfile);
    out.bassProfile = j.stringOr ("bass_profile", out.bassProfile);

    if (out.bpm < 20.0)  out.bpm = 20.0;
    if (out.bpm > 300.0) out.bpm = 300.0;
    if (out.complexity < 0.0) out.complexity = 0.0;
    if (out.complexity > 1.0) out.complexity = 1.0;
    if (out.humanize   < 0.0) out.humanize   = 0.0;
    if (out.humanize   > 1.0) out.humanize   = 1.0;
    if (out.timeSigNumerator   < 1) out.timeSigNumerator   = 4;
    if (out.timeSigDenominator < 1) out.timeSigDenominator = 4;

    const Json& secs = j["sections"];
    if (! secs.isArray() || secs.size() == 0)
    {
        error = sourceName + ": \"sections\" must be a non-empty array";
        return false;
    }

    for (size_t i = 0; i < secs.size(); ++i)
    {
        SectionPlan s;
        loadSection (secs[i], s, static_cast<int> (i));
        out.sections.push_back (s);
    }

    return true;
}

bool SongPlan::load (const std::string& path, SongPlan& out, std::string& error)
{
    Json j;
    if (! Json::parseFile (path, j, error))
        return false;

    return fromJson (j, path, out, error);
}

bool SongPlan::parse (const std::string& text, const std::string& sourceName,
                      SongPlan& out, std::string& error)
{
    std::string parseError;
    const Json j = Json::parse (text, parseError);

    if (! parseError.empty())
    {
        error = sourceName + ": " + parseError;
        return false;
    }

    return fromJson (j, sourceName, out, error);
}

std::vector<std::string> SongPlan::validate() const
{
    std::vector<std::string> warnings;

    bool keyOk = false;
    pitchClassFromName (key, keyOk);
    if (! keyOk)
        warnings.push_back ("key \"" + key + "\" is not a recognised note name; falling back to E");

    static const char* feels[] = { "straight", "half_time", "double_time", "blast" };
    static const char* plays[] = { "full", "drums", "bass", "none" };
    static const char* fills[] = { "auto", "none", "small", "big" };

    for (const SectionPlan& s : sections)
    {
        bool found = false;
        for (const char* f : feels) if (s.feel == f) { found = true; break; }
        if (! found)
            warnings.push_back ("section \"" + s.name + "\": unknown feel \"" + s.feel + "\"; using straight");

        found = false;
        for (const char* p : plays) if (s.plays == p) { found = true; break; }
        if (! found)
            warnings.push_back ("section \"" + s.name + "\": unknown plays \"" + s.plays + "\"; using full");

        found = false;
        for (const char* f : fills) if (s.fill == f) { found = true; break; }
        if (! found)
            warnings.push_back ("section \"" + s.name + "\": unknown fill \"" + s.fill + "\"; using auto");

        for (const std::string& ch : s.chords)
        {
            if (! parseChord (ch).valid)
                warnings.push_back ("section \"" + s.name + "\": cannot read chord \"" + ch + "\"");
        }

        // A shorter chord list repeating over the bars is normal and intended -
        // "Em C D" over eight bars means keep going round. Only say something
        // when the cycle does not divide evenly, because then the progression
        // lands somewhere different each time through and that is usually a typo.
        if (! s.chords.empty()
            && static_cast<int> (s.chords.size()) < s.bars
            && s.bars % static_cast<int> (s.chords.size()) != 0)
        {
            warnings.push_back ("section \"" + s.name + "\": " + std::to_string (s.chords.size())
                                + " chords do not divide evenly into " + std::to_string (s.bars)
                                + " bars, so the cycle will land differently each repeat");
        }
    }

    return warnings;
}

} // namespace gb
