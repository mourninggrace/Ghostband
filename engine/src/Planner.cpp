#include "ghostband/Planner.h"
#include "ghostband/Json.h"

#include <algorithm>
#include <cstdio>

namespace gb {

//==============================================================================
// JSON text.
//
// The request carries the user's own words, and people type quotes, newlines,
// tabs and the occasional emoji into a text box. Every one of those has to
// arrive as the same text rather than as a broken request, so this escapes
// everything below 0x20 and passes UTF-8 through untouched.
std::string jsonQuote (const std::string& text)
{
    std::string out;
    out.reserve (text.size() + 16);
    out += '"';

    for (unsigned char c : text)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf (buf, sizeof (buf), "\\u%04x", static_cast<unsigned> (c));
                    out += buf;
                }
                else
                {
                    out += static_cast<char> (c);
                }
        }
    }

    out += '"';
    return out;
}

//==============================================================================
// WHAT THE MODEL IS HELD TO.
//
// Structured output against a JSON schema, so the answer is a chart or it is
// nothing - there is no "here is your song:" preamble to strip, no markdown
// fence to find, and no half-finished object to guess at.
//
// The keys are THE PLAN FILE'S OWN KEYS. That is the design: the model's answer
// is a plan fragment, loaded by exactly the same code as a song a person typed,
// so there is no second format to keep in step with the first.
//
// Deliberately conservative in what it asks of the schema - types, enums,
// required and additionalProperties, nothing more exotic. Ranges (tempo, bar
// counts, intensity) are enforced in code after parsing, where a violation can
// be clamped and reported instead of failing the whole request.
std::string plannerOutputSchema()
{
    return R"SCHEMA({
  "type": "object",
  "properties": {
    "title":       { "type": "string" },
    "explanation": { "type": "string" },
    "key":   { "type": "string",
               "enum": ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"] },
    "mode":  { "type": "string",
               "enum": ["major","natural_minor","harmonic_minor","dorian",
                        "phrygian","phrygian_dominant","mixolydian"] },
    "bpm":   { "type": "number" },
    "time_signature": { "type": "array", "items": { "type": "integer" } },
    "style": { "type": "string",
               "enum": ["hard_rock","metal","thrash","groove_metal","doom","sludge",
                        "punk","prog_metal","alt_rock","emo","ballad","blues"] },
    "bass_tuning": { "type": "string",
                     "enum": ["standard","drop_d","drop_c","b_standard"] },
    "ending": { "type": "string",
                "enum": ["hard_stop","ritard","cymbal_ring","fade"] },
    "sections": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "name":      { "type": "string" },
          "bars":      { "type": "integer" },
          "intensity": { "type": "number" },
          "feel":   { "type": "string",
                      "enum": ["straight","half_time","double_time","blast"] },
          "chords": { "type": "array", "items": { "type": "string" } },
          "plays":  { "type": "string" },
          "guitar":  { "type": "string",
                       "enum": ["auto","silent","sparse","muted","driving","open","busy"] },
          "guitar2": { "type": "string",
                       "enum": ["auto","silent","sparse","muted","driving","open","busy",
                                "fills","solo"] },
          "piano":   { "type": "string",
                       "enum": ["auto","silent","sparse","driving","open","busy"] },
          "lead":    { "type": "string",
                       "enum": ["auto","guitar","guitar2","piano","both"] },
          "fill":    { "type": "string",
                       "enum": ["auto","none","small","big"] }
        },
        "required": ["name","bars","intensity","feel","chords","plays",
                     "guitar","guitar2","piano","lead","fill"],
        "additionalProperties": false
      }
    }
  },
  "required": ["title","explanation","key","mode","bpm","time_signature","style",
               "bass_tuning","ending","sections"],
  "additionalProperties": false
})SCHEMA";
}

// Plain and short on purpose. Models of this generation follow a system prompt
// closely, and one written in capitals and absolutes makes them rigid - so this
// says what the engine does with each field and trusts the model with the rest.
static const char* kSystemPrompt = R"PROMPT(You write song charts for Ghostband, an arranging engine that performs a full rock band - drums, bass, a rhythm guitar, a lead guitar and a piano - through a musician's own instrument plugins.

You write the SHAPE of the song and the engine plays it. You never write notes, rhythms or drum patterns: the engine generates those from what you decide, locks the bass to the kick, and handles every articulation. What you decide is what a good arranger decides - the sections, their lengths, the harmony, how the energy rises and falls, and who is out front where.

What each field does:
- sections: in playing order. Name them the way a band would (intro, verse1, chorus1, verse2, bridge, solo, chorus3, outro); the name tells the engine the section's role.
- bars: whole bars. Real songs mostly use 4, 8, 12 and 16.
- intensity: 0 to 1. Drives density and dynamics across every part. The intensity curve over the song is most of what makes it feel arranged rather than listed.
- feel: straight, half_time (heavy and spacious), double_time (driving), blast (extreme metal only).
- chords: one per bar, or a shorter list that repeats over the section. Use a root (A to G, with # or b) plus one of: nothing (major), m, 5 (power chord), 7, maj7, m7, dim, aug, sus2, sus4. Nothing else - no slash chords, no extensions beyond those.
- plays: which parts play - "full", or a list such as "drums+bass+guitar". Leaving a part out for a section is an arranging decision; an intro or a breakdown is often better for it.
- guitar: the rhythm guitar's texture - silent, sparse, muted (palm-muted chug), driving, open (ringing chords), busy - or auto.
- guitar2: the lead guitar - solo (a lead line for the whole section), fills (short answers in the gaps, the job for most of a song), or a texture like the rhythm guitar, or silent.
- piano: silent, sparse, driving, open, busy, or auto.
- lead: which chordal part is out front: guitar, guitar2, piano, both, or auto.
- fill: the drum fill leading OUT of this section - none, small, big, or auto.
- ending: how the song stops - hard_stop, ritard, cymbal_ring, fade.
- explanation: one or two sentences for the musician about what you did and why.

Write a song a real band would play: sections that earn their length, harmony that suits the style and key, and an arc. Aim for two to five minutes unless asked otherwise. Only use the parts the musician says their rig has.)PROMPT";

//==============================================================================
PlannerRequest buildPlannerRequest (const PlannerBrief& brief, const PlannerSettings& settings)
{
    PlannerRequest r;
    r.url = "https://api.anthropic.com/v1/messages";

    // No x-api-key here, deliberately - see the note on PlannerRequest.
    r.headers = {
        { "content-type",      "application/json" },
        { "anthropic-version", "2023-06-01" },

        // REFUSAL FALLBACKS, opted into by default. If the model's safety
        // classifiers decline a request, the API re-runs it on another model
        // inside the same call rather than handing back a refusal. A song
        // chart should essentially never trip one, but "a death metal song
        // about war" is exactly the kind of brief that might, and a planner
        // that sometimes answers with nothing is worse than one that always
        // answers. "default" lets Anthropic choose the substitute by the
        // reason for the decline, so there is no fallback model to keep
        // current here.
        { "anthropic-beta",    "server-side-fallback-2026-07-01" },
    };

    std::string rig = "The rig has drums and bass";
    if (brief.hasGuitar)  rig += ", a rhythm guitar";
    if (brief.hasGuitar2) rig += ", a lead guitar";
    if (brief.hasPiano)   rig += ", a piano";
    rig += ". Leave out any part it does not have.";

    std::string user = "Write a song chart.\n\nWhat I want: " + brief.request + "\n\n" + rig;

    if (! brief.currentPlanJson.empty())
        user += "\n\nThe song currently loaded, if I am asking for a change to it:\n"
              + brief.currentPlanJson;

    std::string b;
    b += "{\n";
    b += "  \"model\": " + jsonQuote (settings.model) + ",\n";
    b += "  \"max_tokens\": " + std::to_string (std::max (1024, settings.maxTokens)) + ",\n";
    b += "  \"fallbacks\": \"default\",\n";
    b += "  \"thinking\": { \"type\": \"adaptive\" },\n";
    b += "  \"output_config\": {\n";
    b += "    \"effort\": " + jsonQuote (settings.effort) + ",\n";
    b += "    \"format\": { \"type\": \"json_schema\", \"schema\": " + plannerOutputSchema() + " }\n";
    b += "  },\n";
    b += "  \"system\": " + jsonQuote (kSystemPrompt) + ",\n";
    b += "  \"messages\": [ { \"role\": \"user\", \"content\": " + jsonQuote (user) + " } ]\n";
    b += "}\n";

    r.body = b;
    return r;
}

//==============================================================================
// An HTTP failure, said the way somebody can act on it. Anthropic's own message
// is included where it helps; it is their text about the request and never
// contains the key, which was only ever in a header.
static std::string describeHttpFailure (int status, const Json& j)
{
    const std::string type    = j["error"].stringOr ("type", "");
    const std::string message = j["error"].stringOr ("message", "");
    const std::string detail  = message.empty() ? std::string() : " (" + message + ")";

    if (status == 0)
        return "Could not reach api.anthropic.com. Check the internet connection - "
               "Ghostband carries on playing either way.";

    if (status == 401 || type == "authentication_error")
        return "Anthropic did not accept the API key. Check it in Settings.";

    if (status == 403 || type == "permission_error")
        return "That API key is not allowed to use this model" + detail + ".";

    if (status == 404 || type == "not_found_error")
        return "Anthropic does not recognise that model name. Check it in Settings" + detail + ".";

    if (status == 429 || type == "rate_limit_error")
        return "Anthropic is limiting requests on this key - either too many at once, "
               "or the account is out of credit. Wait a minute, or check the billing "
               "page in the Anthropic console.";

    if (status == 529 || type == "overloaded_error" || status >= 500)
        return "Anthropic's servers are busy or having trouble right now (HTTP "
               + std::to_string (status) + "). Try again in a minute.";

    return "Anthropic refused the request (HTTP " + std::to_string (status) + ")" + detail + ".";
}

PlannerResult parsePlannerResponse (int httpStatus, const std::string& body)
{
    PlannerResult r;

    std::string parseError;
    const Json j = body.empty() ? Json() : Json::parse (body, parseError);

    if (httpStatus != 200)
    {
        r.error = describeHttpFailure (httpStatus, j);
        return r;
    }

    if (! parseError.empty() || ! j.isObject())
    {
        r.error = "Anthropic's answer could not be read. Try again.";
        return r;
    }

    r.servedBy     = j.stringOr ("model", "");
    r.inputTokens  = j["usage"].intOr ("input_tokens", 0);
    r.outputTokens = j["usage"].intOr ("output_tokens", 0);

    // STOP REASON BEFORE CONTENT. A refusal and a truncation both arrive as a
    // perfectly good 200, and reading the content first would try to load half
    // a song, or no song.
    const std::string stop = j.stringOr ("stop_reason", "");

    if (stop == "refusal")
    {
        r.error = "The model declined to write this one, even after being passed to a "
                  "second model. Try describing the song differently.";
        return r;
    }

    if (stop == "max_tokens")
    {
        r.error = "The model ran out of room before the chart was finished. Asking for "
                  "a shorter song, or fewer sections, usually fixes it.";
        return r;
    }

    // The chart is the text. With a JSON schema on the output there is one text
    // block and it is the object; joining them all is only a guard against a
    // split, not an expectation of prose.
    std::string text;
    const Json& content = j["content"];
    for (size_t i = 0; i < content.size(); ++i)
        if (content[i].stringOr ("type", "") == "text")
            text += content[i].stringOr ("text", "");

    if (text.empty())
    {
        r.error = "The model's answer had no chart in it. Try again.";
        return r;
    }

    std::string chartError;
    const Json chart = Json::parse (text, chartError);
    if (! chartError.empty() || ! chart.isObject())
    {
        r.error = "The model's chart was not readable. Try again.";
        return r;
    }

    r.explanation = chart.stringOr ("explanation", "");

    // THE SAME LOADER AS A SONG A PERSON WROTE. Everything it forgives in a
    // hand-typed plan it forgives here, and everything it catches it catches.
    std::string loadError;
    if (! SongPlan::parse (text, "the planner", r.plan, loadError))
    {
        r.error = "The chart could not be loaded: " + loadError;
        return r;
    }

    // RANGES, enforced here rather than in the schema. Clamped and reported,
    // because a chart with one section of 200 bars is a mistake in one number
    // and not a reason to throw the other eleven sections away.
    if (r.plan.bpm < 40.0 || r.plan.bpm > 250.0)
    {
        const double was = r.plan.bpm;
        r.plan.bpm = std::max (40.0, std::min (250.0, r.plan.bpm));
        r.notes.push_back ("tempo " + std::to_string (static_cast<int> (was))
                           + " was outside 40-250 and was set to "
                           + std::to_string (static_cast<int> (r.plan.bpm)));
    }

    if (r.plan.timeSigNumerator < 2 || r.plan.timeSigNumerator > 13
        || (r.plan.timeSigDenominator != 4 && r.plan.timeSigDenominator != 8))
    {
        r.notes.push_back ("time signature " + std::to_string (r.plan.timeSigNumerator) + "/"
                           + std::to_string (r.plan.timeSigDenominator)
                           + " is not one the engine plays, so it is 4/4");
        r.plan.timeSigNumerator   = 4;
        r.plan.timeSigDenominator = 4;
    }

    if (r.plan.sections.size() > 32)
    {
        r.plan.sections.resize (32);
        r.notes.push_back ("the chart had more than 32 sections; the first 32 are kept");
    }

    int totalBars = 0;
    for (SectionPlan& s : r.plan.sections)
    {
        if (s.bars < 1 || s.bars > 64)
        {
            const int was = s.bars;
            s.bars = std::max (1, std::min (64, s.bars));
            r.notes.push_back ("section \"" + s.name + "\" was " + std::to_string (was)
                               + " bars and is now " + std::to_string (s.bars));
        }

        s.intensity = std::max (0.0, std::min (1.0, s.intensity));
        totalBars  += s.bars;
    }

    if (r.plan.sections.empty() || totalBars <= 0)
    {
        r.error = "The chart had no sections in it. Try again.";
        return r;
    }

    // Whatever else the loader has to say - an unreadable chord, a section
    // whose parts name nothing. It plays through all of them.
    for (const std::string& f : r.plan.faults())
        r.notes.push_back (f);

    r.ok = true;
    return r;
}

//==============================================================================
SongPlan mergeChart (const SongPlan& current, const SongPlan& written)
{
    SongPlan out = current;

    out.title              = written.title;
    out.key                = written.key;
    out.mode               = written.mode;
    out.bpm                = written.bpm;
    out.timeSigNumerator   = written.timeSigNumerator;
    out.timeSigDenominator = written.timeSigDenominator;
    out.style              = written.style;
    out.bassTuning         = written.bassTuning;
    out.ending             = written.ending;
    out.swing              = written.swing;
    out.sections           = written.sections;

    // Written chords are absolute, in the key the chart names. A transpose left
    // over from the previous song would move them somewhere nobody chose.
    out.transpose = 0;

    return out;
}

} // namespace gb
