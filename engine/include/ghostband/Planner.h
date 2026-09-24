#pragma once

#include "ghostband/SongPlan.h"

#include <string>
#include <utility>
#include <vector>

namespace gb {

//==============================================================================
// THE AI PLANNER: a model writes the CHART, the engine plays it.
//
// The chart is chords, section shape, intensity curve and who leads where - the
// things a model is good at. Everything that has to be exactly right every time
// stays in measured code: the kick and bass locking together, articulations,
// determinism, one seed meaning one song forever. Nothing downstream of the
// plan changes, which is the entire reason the plan was made a real file format
// rather than an internal structure: a plan a model wrote is indistinguishable
// from a plan a person wrote, and is loaded by the same code.
//
// This half is PURE. It builds the request and reads the response and never
// touches a network, a thread or a key - so every part of it can be checked by
// the harness with canned responses, at no cost and with no account. The
// plugin owns the other half: the key, the background thread, the socket.
//
// THREE RULES, decided with the owner on 2026-09-07 and not up for revision:
//
//   NEVER A DEPENDENCY.    No key, no network, no account: Ghostband behaves
//                          exactly as it did before this file existed.
//   NEVER A SUBSCRIPTION.  The user brings their own key. Ghostband is free,
//                          and a per-song cost billed against no revenue is a
//                          business model, not a feature.
//   NEVER IN THE AUDIO PATH. The call happens on a background thread and
//                          playback never waits on it.

struct PlannerSettings
{
    // The skill this was written against says to default to the current
    // Opus and never to downgrade for cost on the user's behalf - the key and
    // the bill are theirs, so the choice is too, and it is a setting.
    std::string model  = "claude-opus-5";

    // "high" by default: writing a chart that reads like a song rather than a
    // list of sections is exactly the kind of work that repays thinking.
    std::string effort = "high";

    // Non-streaming, so kept inside what one HTTP read comfortably holds. A
    // chart is a few thousand tokens; the rest is room to think.
    int maxTokens = 16000;
};

// What the user asked for, and what the rig can actually play.
struct PlannerBrief
{
    // In their own words: "a slow doom song that ends in a blast beat".
    std::string request;

    // Optional. The song on screen, as plan JSON, for "a variation of this" or
    // "make the chorus bigger". Empty writes a new song from nothing.
    std::string currentPlanJson;

    // Which of the optional parts this rig has. A chart that hands a verse to a
    // piano nobody loaded is a silent verse; the model is told what exists.
    bool hasGuitar  = true;
    bool hasGuitar2 = true;
    bool hasPiano   = true;
};

// The HTTP request, complete EXCEPT FOR THE KEY.
//
// The key never passes through the engine - not into this struct, not into a
// log, not into a plan. The plugin adds the one header that carries it at the
// moment of sending, which keeps the list of places a key can leak to one.
struct PlannerRequest
{
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

PlannerRequest buildPlannerRequest (const PlannerBrief& brief, const PlannerSettings& settings);

struct PlannerResult
{
    bool ok = false;

    // The chart, parsed by the same loader as a hand-written song. Only the
    // chart fields mean anything; see mergeChart for what is kept from the
    // song it replaces.
    SongPlan plan;

    // Why it failed, in words an owner can act on. Never contains the key,
    // and never the raw response body - which can be long and is not written
    // for people.
    std::string error;

    // Things the loader noticed about a chart it still accepted - an unreadable
    // chord, a clamped tempo. The engine plays through all of them, the same as
    // it does for a person's typo, and says so.
    std::vector<std::string> notes;

    // What the model said about its own chart, in a sentence or two.
    std::string explanation;

    // Which model actually answered. It can differ from the one asked for: a
    // declined request is re-run on another model server-side.
    std::string servedBy;

    int inputTokens  = 0;
    int outputTokens = 0;
};

// Reads an HTTP response from the Messages API. The status matters as much as
// the body: a 401 is a key problem, a 429 is a quota problem, a 529 is
// Anthropic's, and each wants a different sentence.
PlannerResult parsePlannerResponse (int httpStatus, const std::string& body);

// The chart from `written`, everything else from `current`.
//
// A model writes the SONG: title, key, mode, tempo, time, style, tuning, the
// sections and the ending. It does not choose which plugins are loaded, which
// seed is playing, or where the performer's dials sit - those belong to the
// person in front of the rig, and a chart that silently reset their humanize
// setting would be a chart that changed things nobody asked it to.
SongPlan mergeChart (const SongPlan& current, const SongPlan& written);

// Exposed for the harness: the JSON schema the model's answer is held to, and
// the escaping used for the user's own words.
std::string plannerOutputSchema();
std::string jsonQuote (const std::string& text);

} // namespace gb
