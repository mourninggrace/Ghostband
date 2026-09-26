#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ghostband/Planner.h"
#include "ghostband/Profile.h"
#include "ghostband/Render.h"
#include "ghostband/SongPlan.h"

#include "GhostbandLookAndFeel.h"

#include <atomic>
#include <map>
#include <vector>

// How much of a UI freeze was Ghostband's own fault.
//
// The stall detector already separates "our timer callback was slow" from "our
// timer was never called". That was enough to rule out the per-frame work, and
// not enough to name a culprit, because TWO things happen on the message thread
// outside the timer callback and both look identical to someone else's code
// holding it:
//
//   * PAINTING. repaint() only marks the window dirty; the actual paint runs
//     later in the message loop, so a slow paint shows up as a GAP with zero
//     work recorded and would be blamed on the host.
//   * OUR OWN HANDLERS. A button that saves a file or reloads a plan blocks the
//     timer for exactly as long as it takes, and again the previous callback
//     reads as fast.
//
// So every piece of Ghostband that runs on the message thread and could take
// real time is wrapped in a Scope. The total is what we did between one tick
// and the next; against the gap, it says "us" or "not us" with no
// interpretation left over.
//
// Only counts on the message thread - the audio thread must never pay for a
// diagnostic, and the harness calls most of these from wherever it likes.
namespace gbdiag
{
    struct Work
    {
        // Milliseconds of message-thread time spent inside Ghostband since the
        // last timer tick, and the single longest piece of it.
        static double      total;
        static double      worst;
        static const char* worstName;

        static void add (const char* name, double ms);
        static void reset();
    };

    struct Scope
    {
        explicit Scope (const char* n);
        ~Scope();

        const char* name;
        double      start    = 0.0;
        bool        counting = false;

        Scope (const Scope&) = delete;
        Scope& operator= (const Scope&) = delete;
    };
}

// One line at the top of anything on the message thread that could be slow.
#define GB_WORK(name) const gbdiag::Scope gbWorkScope__ (name)

// Ghostband as a VST3.
//
// It emits MIDI and touches nothing else: audio passes through untouched, and
// the plugin's whole job is to play the arrangement out of its MIDI output so a
// Gig Performer rackspace can wire it into SSD5, MODO Bass, or anything else.
//
// A VST3 cannot see or control sibling plugins - the format sandboxes instances
// deliberately - so this never tries. It writes MIDI; the host does the routing.
class GhostbandProcessor : public juce::AudioProcessor
{
public:
    GhostbandProcessor();
    ~GhostbandProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ghostband"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Used by the editor.

    struct Status
    {
        bool          ok = false;
        bool          unverifiedProfiles = false;
        juce::String  planName   { "no plan loaded" };
        juce::String  message    { "Load a plan to begin." };
        juce::String  drumProfile;
        juce::String  bassProfile;
        juce::String  guitarProfile;   // empty when the song has no such part
        juce::String  guitar2Profile;
        juce::String  pianoProfile;
        juce::String  headline;
        int           bars    = 0;
        double        seconds = 0.0;
        int           drumHits  = 0;
        int           bassNotes = 0;

        // WHAT IS WRONG WITH THE SONG ITSELF, as distinct from what is wrong
        // with the instruments (which is what `message` carries).
        //
        // SongPlan::validate has always produced these - an unreadable chord, a
        // section whose `plays` names nothing, a chord cycle that does not
        // divide into the bars - and only the command line ever showed them.
        // Inside the plugin a mistyped chord silently becomes the key's root
        // and a mistyped `plays` silently produces a silent section, with
        // nothing on screen to say why. That is the worst kind of fault: the
        // song is wrong, the plugin is behaving exactly as designed, and there
        // is no thread to pull.
        juce::StringArray planWarnings;
    };

    void loadPlan (const juce::File& planFile);
    void regenerate();

    // Re-reads the current plan from disk, so a plan can be edited in a text
    // editor and heard without reloading the plugin. Falls back to the built-in
    // plan when there is no file.
    void reloadPlan();

    // Rerolls only the named sections. An empty list rerolls the whole song by
    // taking a new global seed, which is the old behaviour.
    void rerollSections (const std::vector<int>& indices);

    //==========================================================================
    // THE DICE. Everything at once, for the fun of it.
    //
    // Roll changes the performance; this changes the whole character - the
    // seed, the three dials, the tempo, the key, the mode, the style and the
    // bass tuning. `alsoNewSong` picks a different preset first, so it is "a
    // different song, played differently" rather than "this song, differently".
    //
    // RANDOM WITHIN MUSICAL BOUNDS, not uniform. A tempo drawn evenly from
    // 40..250 is nonsense most of the time; one drawn from what the style is
    // actually played at is a surprise you might keep. The whole point is to
    // land somewhere you would not have typed, often enough to be worth
    // pressing twice.
    //
    // Returns false when there was nothing to roll.
    bool rollTheDice (bool alsoNewSong);

    // One step back, and it exists BECAUSE of the dice.
    //
    // Rolling past something good with no way back is the one thing that would
    // make this frustrating rather than fun, and it is the sharp end of the
    // project having no undo at all. The state before the last roll is kept in
    // memory - the whole song as text, exactly as a take stores it - so this
    // puts it back note for note.
    bool undoTheDice();
    bool canUndoTheDice() const;

    Status                         getStatus() const;
    std::vector<gb::SectionReport> getSections() const;
    juce::File                     getPlanFile() const;

    // Where the preset songs live inside the installed bundle.
    juce::File                     bundledPlansFolder() const;

    // Where songs you write go: Documents/Ghostband/Songs, created on demand.
    // Separate from the presets on purpose - see the note on the definition.
    static juce::File              userSongsFolder();

    // True when the loaded song is one that shipped in the bundle, which is
    // read-only in practice and must not be saved over.
    bool                           planIsFactory() const;

    // The tempo the loaded song is written at.
    double                         getPlanBpm() const;

    // Tests must not write to the user's real mappings. The harness points this
    // at a temporary folder; without it a test run rewrote the channels and the
    // taught controls of every instrument on the machine.
    static void setLearnedControlsFileForTesting (const juce::File& f);

    // What a part is ACTUALLY set to once the learned store has been merged
    // over the shipped profile. Public because the difference between those two
    // is exactly the thing that cannot be worked out by reading either one:
    // the store supplies CC numbers and the profile supplies everything else,
    // and I got that backwards once by reasoning about it instead of printing
    // it. `--controls` in the harness prints it.
    const gb::ControlSet* controlSetForTesting (int part) const;

    //==========================================================================
    // Putting an instrument's mappings back to what its profile says.
    //
    // The learned store holds CC NUMBERS taught by MIDI Learn and nothing else;
    // everything about what a control MEANS comes from the profile. So the only
    // thing that can drift is a controller number - which is usually right,
    // because a taught CC is a fact about the rack rather than an opinion.
    //
    // Two ways it can be wrong, and they are why this exists:
    //
    //   * a profile corrects a CC and the store keeps the old one, silently;
    //   * a profile DROPS a control and the store puts it straight back, since
    //     a control only the store knows about is carried across whole. That
    //     one is permanent: there is no edit to the profile that removes it.
    //
    // Called "profile" rather than "shipped" on purpose. Saving mappings writes
    // the profile file too, so what this resets to is whatever that file says
    // now - which may be something you saved yesterday, not what was in the zip.
    // The instrument a part is filling, as the learned store keys it.
    std::string instrumentKeyForPart (int part) const;

    int controlsDifferingFromProfile (int part) const;
    juce::String controlDifferenceSummary (int part) const;
    bool resetControlsToProfile (int part, juce::String& error);

    //==========================================================================
    // Takes: one performance of one song, saved by name.
    //
    // A preset is a SONG - its chords, its sections, its tempo. A take is a
    // PERFORMANCE of that song, and the two need different words because they
    // are recalled for different reasons: you load a preset to play a different
    // song, and a take to hear the same song the way you heard it before.
    //
    // A take carries the whole plan, not a path to it. The seed alone does not
    // reproduce what was heard - complexity, humanize and fills all feed the
    // same RNG stream, and key, style, tempo and the chords live in the plan
    // itself, which the Edit screen can change without saving. Storing a path
    // would recall a song that had moved on. Storing the text costs a few
    // kilobytes each and cannot go stale.
    //
    // The mix levels and the channels are deliberately NOT in a take. Those are
    // how the rig is wired, not how the band played, and having a take reach
    // over and rebalance the rack would be surprising in the way that gets a
    // feature turned off.
    struct Take
    {
        juce::String name;       // what the owner called it
        juce::String songName;   // the plan's own name, so a list can say where it came from
        juce::String planPath;   // where that song came from, so Reload keeps meaning something
        juce::String planJson;   // the song itself, verbatim
        juce::String savedAt;    // yyyy-mm-dd, for the list
        int    seed       = 1;
        double complexity = 0.5;
        double humanize   = 0.5;
        double fills      = 0.62;

        // 0.5 in a take saved before the dial existed, which is also the value
        // that reproduces what those takes actually sounded like. See
        // gb::byIntuition: the default IS the old behaviour, so an old take
        // recalls note for note without needing to know the field is missing.
        double intuition  = 0.5;
    };

    std::vector<Take> getTakes() const;

    // Saves the current performance under `name`. An existing take of the same
    // name is replaced rather than duplicated - that is what pressing Save with
    // a name already in the list means everywhere else.
    bool saveTake (const juce::String& name, juce::String& error);

    void recallTake (int index);
    void deleteTake (int index);

    // Same reason as the learned-controls override: a test run must not write
    // over the owner's saved takes.
    static void setTakesFileForTesting (const juce::File& f);
    static juce::File takesFile();

    // Diagnostics for the harness: what the audio thread would actually play,
    // as opposed to what the engine says it generated.
    // `minNote` separates played notes from keyswitches, which are note-ons on
    // the same channel sitting below the instrument's playable range. Counting
    // them together conflates the performance with the articulation.
    int getSequenceNoteOnCount (int channel, int minNote = 0) const;

    // Every controller message on a channel, in order, as "cc=value" pairs.
    // The performance and the TONE are different claims: two renders can place
    // identical notes and still hand the guitar a different amp.
    juce::String getSequenceControllers (int channel) const;

    // The extremes actually written on a channel, or an inverted pair when the
    // channel is silent. Used to assert that nothing lands outside what the
    // instrument's profile says it can play.
    // The lowest and highest notes a part actually PLAYS, with anything its
    // profile declares as a keyswitch left out. An empty range when the part is
    // silent. See PhraseProfile::isSwitchNote for why the two must be told
    // apart: a keyswitch sits outside the playable range deliberately.
    juce::Range<int> getPlayedNoteRange (int part) const;

    int getSequenceLowestNote  (int channel) const;
    int getSequenceHighestNote (int channel) const;
    int getSequencePitchSum   (int channel) const;
    int getBarTicks() const;
    int getBeatTicks() const;

    //==========================================================================
    // What the tracker view reads.
    //
    // Every other view in this plugin shows a SUMMARY - counts, densities,
    // feels. This shows what is actually on the wire: the note, how hard, and
    // which articulation, per part, per beat. Given how much of this project has
    // been "what is Ghostband actually sending", that is worth a component.
    struct TrackerCell
    {
        int note     = -1;   // -1 for an empty cell
        int velocity = 0;
        int cc       = -1;   // an articulation or control change landing on this beat
        int ccValue  = 0;

        // How many note-ons landed in this cell, not just the one being shown.
        // At one row per beat this is nearly always 0 or 1 and says nothing; at
        // one row per BAR a cell can cover sixteen hi-hats, and a view that
        // showed one of them and no sign of the other fifteen would be lying.
        int hits     = 0;

        // A KEYSWITCH IN THIS CELL, KEPT APART FROM THE MUSIC.
        //
        // It used to compete for `note`, and win: a switch goes out at a fixed
        // velocity, so it beat any quieter note in the same cell and the grid
        // showed an instruction where a note was played. It also read as one -
        // `A7` in the GTR 2 column, two octaves above anything that instrument
        // owns.
        //
        // So it gets its own field and its own drawing. `switchValue` is the
        // velocity, which for the hand switch IS THE FRET and is the one case
        // where the number beside a switch means something.
        gb::PhraseProfile::SwitchKind switchKind = gb::PhraseProfile::SwitchKind::None;
        int switchValue = 0;

        // How many switches landed here, because more than one can. At a bar
        // per row the fretting mode and the hand position share a cell - they
        // are sent together, at the top of a section - and showing one of them
        // with no sign of the other is the same lie the hit count exists to
        // avoid. The view marks it rather than trying to fit both.
        int switchCount = 0;
    };

    // `rows` rows starting at `firstTick`, for each channel given, row-major:
    // row 0's cells first, then row 1's. One pass under one lock, because the
    // view needs eighty of these per repaint and eighty walks of the sequence
    // would be eighty times the work for the same answer.
    //
    // ticksPerRow of 0 means one row per beat. Anything else is the row height
    // in ticks, so the same call serves one row per bar and one row per 16th.
    std::vector<TrackerCell> getTrackerCells (int firstTick, int rows,
                                              const std::vector<int>& channels,
                                              int ticksPerRow = 0) const;

    // Counts calls to processBlock, and exists purely so a UI freeze can be
    // ATTRIBUTED rather than guessed at.
    //
    // The report was "the UI is frozen, playhead not moving, but audio is still
    // heard like normal, and then suddenly it works again". If this counter
    // keeps climbing across a gap in the editor's timer, the audio thread was
    // running the whole time and only the MESSAGE thread was stuck - which
    // rules out every explanation involving a lock the audio thread holds, and
    // points at the host's message thread instead of at ours. If it stops
    // climbing too, the whole plugin stopped and it is a different fault.
    //
    // Relaxed ordering: this is a diagnostic, and making the audio thread pay
    // for a memory barrier to service a readout would be its own bug.
    std::atomic<unsigned> audioBlocks { 0 };

    // WHAT GHOSTBAND ITSELF SPENDS ON THE AUDIO THREAD, and how much audio the
    // host actually asked for. Added 2026-09-26 after the owner's interface
    // locked into a looping noise that only unplugging it cured, and the log
    // showed the audio had stopped for up to a second - with no way to say
    // whether any of that second was inside processBlock.
    //
    // Samples, not blocks: a host may send blocks smaller than the size it
    // announced, and elapsed time times the sample rate is exact either way.
    // Timing is in high-resolution ticks (QueryPerformanceCounter on Windows),
    // added with fetch_add and maxed with a compare loop - one writer, one
    // reader that takes and zeroes, nothing that can block.
    std::atomic<juce::uint64> audioSamples       { 0 };
    std::atomic<juce::int64>  audioWorkTicks     { 0 };
    std::atomic<juce::int64>  audioWorstBlockTicks { 0 };

    void noteAudioWork (juce::int64 ticks) noexcept
    {
        audioWorkTicks.fetch_add (ticks, std::memory_order_relaxed);
        juce::int64 worst = audioWorstBlockTicks.load (std::memory_order_relaxed);
        while (ticks > worst
               && ! audioWorstBlockTicks.compare_exchange_weak (worst, ticks, std::memory_order_relaxed)) {}
    }

    // Taken by the editor's timer, which zeroes both so each reading covers
    // only the time since the last one.
    struct AudioWork { double totalMs = 0.0, worstBlockMs = 0.0; };
    AudioWork takeAudioWork() noexcept
    {
        const double msPerTick = 1000.0 / (double) juce::Time::getHighResolutionTicksPerSecond();
        return { (double) audioWorkTicks.exchange (0, std::memory_order_relaxed) * msPerTick,
                 (double) audioWorstBlockTicks.exchange (0, std::memory_order_relaxed) * msPerTick };
    }

    // Where a stall report is written, beside the takes and the learned
    // controls, so it can be found and sent without hunting.
    static juce::File stallLogFile();

    //==========================================================================
    // THE CHANGE LOG. Every change, when it happened, and what it was before.
    //
    // A rig accumulates decisions and forgets them. "It sounded better
    // yesterday" is unanswerable without a record, and so is "when did this
    // channel move" - the plugin knows both and had been throwing them away.
    //
    // Two rules that make the difference between a log and a stream of noise:
    //
    //   * FROM and TO, not just the new value. "seed 88345" says nothing;
    //     "seed 88345 -> 4242" is the change, which is what was asked for.
    //   * Continuous controls are logged when they SETTLE, not while they move.
    //     A mix knob dragged across its range is one change, not two hundred.
    //     The editor's timer does that coalescing - see snapshotForLog.
    static juce::File changeLogFile();
    static void setChangeLogFileForTesting (const juce::File& f);

    // `what` names the thing; from/to are optional and make it a change rather
    // than an event. Safe from any thread but meant for the message thread -
    // it opens a file, so never call it from processBlock.
    void logChange (const juce::String& what) const;
    void logChange (const juce::String& what, const juce::String& from,
                    const juce::String& to) const;

    // Off while a session is being restored, so reopening a rackspace does not
    // write twenty lines claiming somebody just set every control by hand.
    // A restore is one line, and it says so.
    mutable std::atomic<bool> changeLogQuiet { false };

    // Same reason as the learned-controls and takes overrides: a test run must
    // not append to the owner's real log. Forgetting this once already rewrote
    // the channels of every instrument on this machine.
    static void setStallLogFileForTesting (const juce::File& f);

    //==========================================================================
    // THE AI PLANNER, the plugin's half. gb::Planner is the engine's half and
    // is pure; this is the part with a key, a thread and a socket.
    //
    // The three rules from the backlog hold here too: with no key nothing runs
    // and nothing else changes; the key is the owner's own; and the request is
    // made on its own thread, so playback never waits on a network.

    // THE KEY, encrypted for this Windows user (see SecretStore). There is
    // deliberately no getter: the key goes from the store into one request
    // header inside the job and nowhere else - not the editor, not a log, not
    // a plan, not a take.
    static juce::File plannerKeyFile();

    // Which model writes, and how hard it thinks. Chosen in Settings, kept
    // beside the key (planner-settings.json) so it holds in every session.
    // Anything unreadable falls back to the defaults in gb::PlannerSettings.
    gb::PlannerSettings getPlannerSettings() const;
    void setPlannerModel  (const juce::String& id);
    void setPlannerEffort (const juce::String& effort);

    // "about 6c a song ...": the average tokens of the songs this machine has
    // actually written (from changes.log), priced at the chosen model's rates.
    juce::String plannerCostEstimate() const;
    juce::String writingWithEffort { "medium" };   // the effort the song in flight was asked at
    static void setPlannerKeyFileForTesting (const juce::File& f);

    bool setPlannerKey (const juce::String& key);
    void clearPlannerKey();
    bool hasPlannerKey() const;

    // Decrypts the saved key and throws it away again: can Windows still read
    // it? Asked once when the editor opens, so Settings can say so before
    // Write does. Never exposes the key.
    bool plannerKeyReadable() const;
    static void logChangeStatic (const juce::String& what);

    // Written songs are saved as ordinary plan files in here, so Reload, Takes
    // and the file browser all treat them like any other song - and so a song
    // somebody paid for is never lost to a reroll.
    static juce::File writtenSongsFolder();
    static void setUserSongsFolderForTesting (const juce::File& f);

    // Starts a request. False, with a reason in `whyNot`, when it cannot: no
    // key, or one already running. The result arrives on the message thread.
    bool writeSong (const juce::String& request, juce::String& whyNot);

    // EXACTLY WHAT IS SENT, apart from the key: the owner's words, the song on
    // screen as context, and which parts this rig has. Public so the harness
    // can read it - "what does this send to a third party" is a question with
    // a checkable answer, and it is checked.
    gb::PlannerBrief makePlannerBrief (const juce::String& request) const;

    struct PlannerStatus
    {
        bool         busy      = false;
        bool         lastOk    = false;
        bool         anyResult = false;
        juce::String message;            // the explanation, or what went wrong
        juce::String servedBy;
        int          inputTokens  = 0;
        int          outputTokens = 0;
        juce::uint32 startedMs    = 0;
    };

    PlannerStatus getPlannerStatus() const;

    // What a finished request does: the chart becomes the song, keeping this
    // rig's plugins, seed and dials, saved as a file and loaded through the
    // same path as every other song - with one step back, the same one the dice
    // has. Public because the harness drives it with canned charts.
    bool applyWrittenSong (const gb::PlannerResult& result, juce::String& error);

    struct Diagnostics
    {
        int    blocks        = 0;
        int    eventsEmitted = 0;
        int    stitchMisses  = 0;   // blocks where the host position was not contiguous
        double worstGap      = 0.0; // largest |hostTick - nextExpectedTick| seen
    };
    Diagnostics diagnostics;
    void resetDiagnostics() { diagnostics = Diagnostics(); }

    // Live transport position, so the editor can show which section is sounding.
    // Hearing a change is much easier when you can see what you are hearing.
    // Editor size, kept here so it survives closing the window and is saved
    // with the rest of the plugin state.
    // Big enough that everything fits on first open. The old default cut the
    // section list off before anything had been resized.
    // Bigger, because the type inside it got bigger. 620x780 was sized around
    // 11pt body text; readable type needs somewhere to sit or it just clips.
    // LANDSCAPE, because the content is. The old 800x960 was a portrait window
    // holding a grid that runs left to right, on a monitor 2560 wide - it used
    // 31% of the width and 67% of the height, and the song screen's controls
    // stopped 45% short of its right edge. See layOutSongScreen.
    std::atomic<int> editorWidth  { 1180 };
    std::atomic<int> editorHeight { 820 };

    // How much music one tracker row covers: 0 bar, 1 beat, 2 eighth, 3
    // sixteenth. Which one is right depends entirely on what you are looking
    // for - a bar per row to read the shape of an arrangement, a sixteenth per
    // row to see where a hat actually sits. That is not a decision to make on
    // someone else's behalf, so it is a control rather than a constant.
    //
    // A BAR is the default, chosen by the owner after trying all four. It is
    // also the cheapest: a quarter as many rows as a beat and a sixteenth as
    // many as a 16th, so the grid repaints least often on the setting most
    // people will open on.
    // How long a beat is, published whenever the song is rebuilt.
    //
    // getBeatTicks used to take stateLock for the time signature and then
    // sequenceLock for the bar length - two separate acquisitions, deliberately
    // not nested (see THE LOCK ORDER RULE), which fixed the deadlock and left a
    // subtler fault behind: a regenerate landing between them returns a
    // numerator from one song and a bar length from another. Harmless on screen
    // and wrong, and it cost two lock acquisitions thirty times a second.
    //
    // Both numbers are known together at the moment the sequence is swapped, so
    // publishing the answer is cheaper than computing it from two sources that
    // can disagree.
    std::atomic<int> beatTicksForUi { 96 };

    std::atomic<int> trackerZoom { 0 };
    static constexpr int numTrackerZooms = 4;

    // Play at the song's own tempo rather than the host's.
    //
    // A VST3 cannot set the host's tempo - there is no such call in the format -
    // so the only way a song can be heard at the tempo it was written at,
    // without setting the host by hand every time, is for Ghostband to keep its
    // own clock. When this is on the host transport still starts and stops it;
    // only the rate comes from the plan.
    //
    // The cost is real and worth knowing: anything else in the rackspace that
    // syncs to the host - a tempo-locked delay, say - stays on the host's tempo
    // and will not agree with the band.
    std::atomic<bool> usePlanTempo { true };

    // Ghostband's own play state, separate from the host's transport.
    //
    // The host's transport is often left running for a whole session, so
    // "start and stop the band" and "start and stop the host" are not the same
    // action. Paused releases every sounding note and freezes the song where it
    // stands; playing again carries on from there.
    std::atomic<bool> paused { false };

    // True once the playhead has passed the last bar while the host is still
    // running. Distinct from `paused`, which is somebody choosing to stop.
    std::atomic<bool> songFinished { false };
    void togglePaused()
    {
        const bool nowPaused = ! paused.load();
        paused.store (nowPaused);

        // Coming off pause starts a performance, so the "song ended" notice
        // goes with it. Left set, the interface would keep explaining why the
        // band had stopped while it was playing.
        if (! nowPaused)
            songFinished.store (false);
    }

    // Set when the song underneath the playhead has been replaced. The audio
    // thread releases everything still sounding before the new one starts,
    // because a note from the song that has just been swapped away has nothing
    // left to turn it off - which is why loading a second song on top of a
    // playing one left the first one ringing over it.
    std::atomic<bool> flushPending { false };
    std::atomic<bool> rewindPending { false };

    std::atomic<int>    playbackTick     { 0 };
    std::atomic<bool>   transportRunning { false };
    std::atomic<double> hostBpm          { 0.0 };

    // The plan's own tempo, published for the AUDIO THREAD.
    //
    // It used to read plan.bpm under stateLock from inside processBlock, while
    // already holding sequenceLock - the other half of the deadlock that froze
    // the host. The audio thread must never block on a lock the message thread
    // can hold, so the one value it needs is published here instead.
    std::atomic<double> planBpmForAudio  { 0.0 };
    std::atomic<int>    activeSection    { -1 };

    // Live section jumping. Clicking a section queues it; the jump lands on the
    // next bar line so the band never falls off the beat. -1 cancels.
    std::atomic<int> queuedSection { -1 };
    void queueSection (int index) { queuedSection.store (index); }

    // Song controls. These write to the plan and regenerate, so they are message
    // thread only. Key transposes rather than only affecting auto progressions,
    // because a plan with written chords would otherwise ignore it entirely.
    //==========================================================================
    // Calibration.
    //
    // A driver profile is a claim about which MIDI note makes which sound, and
    // those claims are often wrong. Verifying them by playing a file and reading
    // marker timecodes off a stopwatch is not a reasonable thing to ask of
    // anyone. This does it by ear instead: pick a voice, hear it, nudge the note
    // until it sounds right, save.
    //
    // It deliberately does not need the host transport - you press a button and
    // hear the note immediately.
    struct CalibrationStep
    {
        juce::String label;      // "snare", "palm mute", "phrase: driving"
        juce::String hint;       // what it should sound like
        int  note    = 36;
        int  channel = 10;
        bool isDrum  = true;     // drums are one-shots; pitched notes are held

    };

    void enterCalibration();
    void exitCalibration();
    bool isCalibrating() const { return calibrating.load(); }

    int  getCalibrationStepCount() const;
    CalibrationStep getCalibrationStep (int index) const;

    void auditionStep (int index);
    void nudgeCalibrationNote (int index, int delta);

    // Writes the corrected map back over the profile the plan pointed at.
    bool saveCalibration (juce::String& error);
    bool calibrationHasEdits() const { return calibrationEdited; }

    //==========================================================================
    // Song structure editing.
    //
    // Sections could previously only be defined by hand-editing a plan JSON,
    // which for a user with no text-editing workflow meant the song structure
    // was not editable at all - every song was one whose skeleton came with the
    // plugin. Editing implies saving, so that lands here too.
    struct SectionEdit
    {
        juce::String name;
        int          bars      = 8;
        double       intensity = 0.5;
        juce::String feel      = "straight";
        juce::String chords;                  // space separated, as typed
        juce::String fill      = "auto";
        juce::String lead      = "auto";      // auto / guitar / piano / both
        bool drums = true, bass = true, guitar = true, piano = true;

        // The second guitar was missing from this struct, so the structure
        // editor could not turn it on or off - the sixth list in this codebase
        // written before guitar2 existed. It defaults to FALSE rather than true
        // like the others, and that is deliberate: every other flag here is
        // overwritten by getSectionEdit before anybody reads it, but a default
        // that silently switched on a second guitar for a caller that forgot to
        // set it would add a part to a song rather than drop one.
        bool guitar2 = false;
    };

    int         getSectionCount() const;
    SectionEdit getSectionEdit (int index) const;
    void        applySectionEdit (int index, const SectionEdit& edit);

    //==========================================================================
    // One bar, by its number in the song. What the tracker clicks on.
    //
    // A row of the grid is a moment in TIME; a section is the thing that can be
    // edited. These translate between the two so a click on bar 34 can reach
    // the chord playing there.
    //
    // Editing one bar of a section whose chords were chosen automatically PINS
    // the whole section to what it was already playing and changes only that
    // bar. That is the honest behaviour and the interface says so: the
    // alternative is for the other seven bars to be free to move next time the
    // song regenerates, which nobody means by "change this chord".
    int          sectionIndexForBar (int bar) const;
    juce::String chordAtBar (int bar) const;
    bool         setChordAtBar (int bar, const juce::String& chord, juce::String& error);

    void addSection (int afterIndex);
    void deleteSection (int index);
    void moveSection (int index, int delta);

    bool savePlan (const juce::File& target, juce::String& error);
    // Whether the song has been changed since it was last written to disk.
    //
    // The flag was set by every edit and read by nothing - six writers, no
    // readers, which is a promise the code makes and does not keep. It matters
    // because edits live only in memory: change a chord, load another song, and
    // the change is gone with no warning and nothing on screen that could have
    // warned you.
    bool planHasUnsavedEdits() const { return planDirty; }
    juce::String planAsText() const;

    void setKeyPitchClass (int pitchClass);
    void setStyle         (const juce::String& style);

    // The tempo the song is written at. Changing it re-renders, because tempo
    // is not a playback speed here - the generators subdivide against it, so a
    // faster song is arranged differently rather than merely played faster.
    void setPlanBpm       (double bpm);
    void setBassTuning    (const juce::String& tuning);

    int          getKeyPitchClass() const;
    juce::String getStyle() const;
    juce::String getBassTuning() const;

    // The mode genuinely matters now that sections can be left without written
    // chords: it decides which progressions get drawn and how a key is spelled.
    void         setMode (const juce::String& mode);
    juce::String getMode() const;

    // A plan compiled into the binary, so the plugin plays something the moment
    // it is added to a rackspace instead of sitting inert until a file is found.
    static const char* builtInPlanJson();
    bool planIsBuiltIn() const;

    // Dial overrides applied on top of whatever the plan file says.
    //
    // These are double, not float, and that is load-bearing. The generator's RNG
    // stream is chaotic: rounding 0.6 to a float shifts one comparison, and from
    // that point the whole arrangement diverges. Holding them as float made the
    // plugin produce a different song than the CLI for the same plan and seed,
    // which breaks the guarantee that a seed means one specific song.
    // Per-part level, 0..1, sent as MIDI CC 7 on each part's channel.
    //
    // Deliberately CC 7 rather than velocity scaling: velocity picks which
    // sample layer a sampler plays, so scaling it makes a quiet kick a
    // *different* kick rather than a softer one. CC 7 changes level and leaves
    // the performance alone.
    // MIDI channel per part. These override whatever the driver profiles say,
    // because which channel an instrument listens on is a property of the user's
    // rig rather than of the plugin they happen to be driving.
    std::atomic<int> channelDrums  { 10 };
    std::atomic<int> channelBass   { 1 };
    std::atomic<int> channelGuitar  { 2 };
    std::atomic<int> channelPiano   { 3 };

    // The second guitarist. Eleven, because that is where the rig puts it and
    // because ten is the drums - a second guitar on a channel a drum machine
    // might also be listening to is a bad default however free it looks.
    // Changeable in Settings like every other channel.
    std::atomic<int> channelGuitar2 { 11 };

    void applyChannels();   // re-reads the atomics into the loaded profiles

    // Plays a few obvious notes on one part's channel, right now, with the
    // transport stopped. Turns "nothing is playing" into "drums arrive and bass
    // does not", which points straight at that part's routing instead of
    // leaving it to guesswork.
    void testPart (int part);   // 0 drums, 1 bass, 2 guitar, 3 piano

    // Sweeps a CC on a part's channel so an instrument in MIDI-Learn mode can
    // latch onto it. A sweep rather than a single value, because most learn
    // implementations want to see a control move, not one message.
    void teachControl (int part, int cc);

    // Sends one control at one value, right now and once. Teach sweeps, which
    // is what MIDI Learn needs and is useless for reading an instrument's
    // display: it is over before you can see where it landed. This parks the
    // control so you can look at it.
    void sendControlNow (int part, int index);
    int  channelForPart (int part) const;

    // Steps a control through every one of its positions, holding each long
    // enough to read, and reports how many there were. The only reliable way to
    // find out how many choices a list really has is to watch it go past.
    void walkControl (int part, int index);

    //==========================================================================
    // Control mappings.
    //
    // How many knobs are worth automating is the owner's decision, not
    // something a fixed set of slots can anticipate - so the list grows, each
    // entry carries the name its owner gave it, and it saves back to the
    // profile so it travels with the instrument.
    struct ControlSlot
    {
        juce::String name    = "new control";
        int          cc      = -1;
        juce::String follows = "intensity";
        // "knob" sweeps, "switch" is on or off, "select" holds one of
        // `positions` choices for the section.
        juce::String type    = "knob";
        int          positions = 0;
        double       low     = 0.0;
        double       high    = 1.0;
    };

    int         getControlCount (int part) const;
    ControlSlot getControl (int part, int index) const;
    void        addControl (int part);
    void        removeControl (int part, int index);
    void        updateControl (int part, int index, const ControlSlot& slot);
    void        teachControlSlot (int part, int index);
    bool        saveControls (int part, juce::String& error);

    // Guitar and piano are optional. A song that names no profile for one has
    // no such instrument, and neither its controls nor its Test button mean
    // anything - saying so is the difference between "this song has no guitar"
    // and what looks like every saved mapping being lost.
    bool        partIsInSong (int part) const;

    // False when this part's instrument has no volume anything outside it can
    // reach. The mix knob is hidden rather than offered and left inert.
    bool        partVolumeReachable (int part) const;

    // The note range a phrase instrument's profile says it can actually play.
    // Empty for a part that is not a phrase instrument or is not in the song.
    //
    // Exists so the harness can assert that Ghostband never writes outside it.
    // Shreddage's chord zone said 28 for weeks while the guitar's lowest string
    // is 40 - so 166 notes across 19 presets were written into a region with no
    // samples in it, and three of the notes just below that region are FX
    // keyswitches that would have changed how everything after them played.
    juce::Range<int> getPlayableRange (int part) const;

    // The last thing Test or a mix knob actually put on the wire, in words.
    //
    // Two faults today were invisible MIDI: notes sent into a range where the
    // instrument is silent, and a controller nobody was listening to. Both
    // looked identical from outside - something is clearly happening, and
    // nothing can be heard. Saying what was sent turns that into a fact.
    juce::String getLastMidiReport() const;
    juce::String controlOwnerName (int part) const;

    std::atomic<float> levelDrums  { 1.0f };
    std::atomic<float> levelBass   { 1.0f };
    std::atomic<float> levelGuitar  { 1.0f };
    std::atomic<float> levelGuitar2 { 1.0f };
    std::atomic<float> levelPiano   { 1.0f };

    // Queues the current levels for delivery. Safe to call at any time; the
    // messages go out whether or not the transport is running.
    void sendLevels();
    void refreshLevels();
    bool levelIsTaught (int part) const;

    // How often the answering guitar takes an opening it is offered. Zero
    // silences fills across the whole song without editing a section, which is
    // the point: "turn it off" should be one control rather than nine edits.
    // Which colour theme the interface draws in. Kept on the processor rather
    // than in the editor, because the editor is destroyed every time the window
    // closes and a theme that forgets itself on close is not a setting.
    // Blueprint, the last entry in kThemes. A number rather than a name because
    // that is what a saved session stores; see the note beside the palette.
    std::atomic<int> theme { ghost::numThemes - 1 };

    std::atomic<double> fills { 0.62 };

    // HOW MUCH THE BAND PLAYS WHAT IT FEELS LIKE rather than what is obvious,
    // across every part rather than only the lead. See gb::byIntuition: 0.5 is
    // exactly what the engine did before this existed, which is what lets the
    // dial ship without changing a single preset song.
    std::atomic<double> intuition { 0.5 };

    std::atomic<double> complexity { 0.5 };
    std::atomic<double> humanize   { 0.5 };
    std::atomic<int>    seed       { 1 };

    juce::ChangeBroadcaster stateChanged;

    // What the band actually SENDS, checked for a note started while the same
    // pitch is still held on that channel. The second note is then released by
    // the first one's note-off - cut short - on any instrument that tracks
    // notes by pitch, which is all of them. Returns "ch 11: 7" per offending
    // channel, empty when clean. For the harness; takes the sequence lock only.
    juce::String restrikesForTesting() const;

private:
    struct TimedMessage
    {
        int               tick  = 0;
        int               order = 0;
        juce::MidiMessage message;
    };

    gb::PhraseProfile*       phraseProfileFor (int part);
    const gb::PhraseProfile* phraseProfileFor (int part) const;

    // Controls belong to all four parts; only two of them are phrase profiles.
    // A drum kit and a bass have knobs worth reaching, and their volumes have
    // to be findable by the mix knobs like everything else.
    gb::ControlSet*       controlSetFor (int part);
    const gb::ControlSet* controlSetFor (int part) const;
    std::string           controlSourcePath (int part) const;

    void loadBuiltInPlan();
    void rebuildSequence (const gb::RenderResult& result,
                          const gb::DrumProfile& kitToUse,
                          const gb::BassProfile& bassToUse,
                          const gb::SongPlan& planToUse,
                          const gb::PhraseProfile* guitarToUse,
                          const gb::PhraseProfile* pianoToUse,
                          const gb::PhraseProfile* guitar2ToUse = nullptr);
    void sendAllNotesOff (juce::MidiBuffer& midi, int sampleOffset);
    bool resolveProfiles (juce::String& error);

    // The audio thread only ever try-locks this. Missing one block during a
    // regenerate is inaudible; blocking the audio thread would not be.
    struct SectionRange { int startTick = 0; int endTick = 0; };

    juce::SpinLock                sequenceLock;
    std::vector<TimedMessage>     sequence;
    std::vector<SectionRange>     sectionRanges;
    int                           sequenceEndTick = 0;
    int                           barTicks        = 1920;

    // Song position is host position plus this. A section jump moves the offset
    // rather than trying to move the host, which keeps scrubbing and looping
    // working and keeps the plugin honest about whose clock it is following.
    double jumpOffset = 0.0;

    mutable juce::CriticalSection stateLock;
    gb::SongPlan                  plan;
    gb::DrumProfile               kit;
    gb::BassProfile               bassProfile;
    // ---- taught control mappings, kept per instrument rather than per file ----
    //
    // A mapping is something you taught on your machine: which of this
    // instrument's knobs is on which CC. That describes the plugin sitting in
    // the rack, not the profile file that happens to name it - and seven
    // different files describe one SSD5. Storing them in the profile meant
    // teaching the same knob seven times.
    //
    // So they live here, keyed by a profile's `instrument` (falling back to its
    // `id`), and are written to one file outside the project so they survive
    // every song, every profile and every gig.
    std::map<std::string, gb::ControlSet> learnedControls;

    // Each instrument's controls EXACTLY AS ITS PROFILE FILE DECLARED THEM,
    // captured before the learned store is merged over the top. Keyed by
    // instrument, like the store itself, so it survives a change of song.
    //
    // Without this there is nowhere to reset TO. The merged set is all the
    // plugin ever holds, the file has already been read and closed, and the
    // difference between the two is invisible from either one on its own.
    std::map<std::string, gb::ControlSet> profileControls;

    // What the song was before the last dice roll: the plan as text plus the
    // three dials and the seed, which is precisely what a take carries. Empty
    // until something has been rolled.
    juce::String diceUndoJson;
    int          diceUndoSeed = 1;
    double       diceUndoComplexity = 0.5, diceUndoHumanize = 0.5, diceUndoFills = 0.62;
    double       diceUndoIntuition  = 0.5;
    juce::File   diceUndoFile;

    // And the channel, for exactly the same reason.
    //
    // A channel belongs to the instrument, not to the part it is filling: the
    // rack has one constrainer per plugin, and IRON 2 is on 2 whether it is the
    // song's only guitar or its second. Overriding a profile's channel with the
    // slot's meant loading Shreddage into the guitar slot sent Shreddage's
    // notes AND its articulation keyswitches to IRON 2 on channel 2 - which
    // played the parts that happened to fall in IRON 2's range and silently
    // dropped the rest.
    std::map<std::string, int> learnedChannels;

    static std::string instrumentKeyFor (const std::string& instrument,
                                         const std::string& id);

    static juce::File   learnedControlsFile();
    void                loadLearnedControls();
    void                saveLearnedControls() const;

    // The takes are read from disk on demand rather than cached, so two
    // instances of the plugin in one rackspace see each other's saves instead
    // of each holding a private copy and the last one to write winning.
    std::vector<Take>   readTakes() const;
    bool                writeTakes (const std::vector<Take>& takes) const;

    // Store wins where it has an entry; otherwise the profile's own block seeds
    // the store, which is how the mappings already taught are carried over
    // without anyone having to redo them.
    void                mergeLearnedControls (gb::ControlSet& controls,
                                              const std::string& instrument,
                                              const std::string& id);

    gb::PhraseProfile             guitarProfile;
    gb::PhraseProfile             guitar2Profile;
    gb::PhraseProfile             pianoProfile;
    bool                          haveGuitar  = false;
    bool                          haveGuitar2 = false;
    bool                          havePiano   = false;
    std::vector<gb::SectionReport> sections;
    Status                        status;
    juce::File                    planFile;

    bool wasPlaying = false;

    // What is actually sounding, so a stop or a section jump can release it by
    // name. Fixed size and never resized, so nothing allocates on the audio
    // thread. Counted rather than flagged because a drum voice can retrigger
    // before its previous note-off has gone out.
    unsigned char activeNoteCount[16][128] = {};

    // Notes the message thread wants sounded right now, for auditioning during
    // calibration. The audio thread only ever try-locks this, so a missed block
    // costs nothing.
    struct PendingMessage { int samplesUntil = 0; juce::MidiMessage message; };
    juce::SpinLock                auditionLock;
    std::vector<PendingMessage>   pendingAuditions;

    std::atomic<bool>             levelsPending { true };
    juce::String                  lastMidiReport;

    // The level messages as sendLevels last worked them out, kept so the audio
    // thread can restate them at the top of a run without needing the state
    // lock to find out which controller each part's volume actually lives on.
    std::vector<juce::MidiMessage> levelMessages;
    std::atomic<bool>             calibrating { false };
    std::vector<CalibrationStep>  calibrationSteps;

    // The last set of level messages actually sent, as flat channel/cc/value
    // triples. Compared before sending so an unchanged mix says nothing - a
    // repeated controller message is noise, and noise is what a knob sitting in
    // MIDI Learn latches onto instead of the sweep meant for it.
    std::vector<int>              lastLevelsSent;

public:
    // What the last naming suggested and why, so the interface can say it out
    // loud. Changing a field under somebody and hoping they notice is how you
    // end up with a mapping nobody chose and nobody can explain.
    juce::String getLastSuggestion() const { return lastSuggestion; }
    void clearLastSuggestion()             { lastSuggestion.clear(); }

private:
    juce::String                  lastSuggestion;

    // Millisecond counter until which a Teach sweep owns the wire and nothing
    // else may transmit. A MIDI Learn takes the first controller it hears.
    std::atomic<juce::int64>      teachingUntil { 0 };
    bool                          calibrationEdited = false;
    bool                          planDirty = false;

    // Where the previous block's window ended. Consecutive blocks are stitched
    // to this rather than recomputed from the host's ppq, because deriving both
    // ends of the window from floating-point ppq lets rounding overlap the
    // windows - and an overlapped window emits the same note twice.
    double nextExpectedTick = -1.0;

    // Ghostband's own clock, used when it is not following the host's. Runs
    // from zero at each transport start.
    double planTick = 0.0;

    //==========================================================================
    // The planner's job. Its own thread; see PlannerJob in the .cpp.
    class PlannerJob;
    std::unique_ptr<PlannerJob> plannerJob;

    mutable juce::CriticalSection plannerLock;
    PlannerStatus                 plannerStatus;

    void plannerFinished (const gb::PlannerResult& result, const juce::String& request);

    // The dice's one step back, shared: anything that replaces the whole song
    // records where it was first. False when there is no song to remember.
    bool rememberForUndo();

    JUCE_DECLARE_WEAK_REFERENCEABLE (GhostbandProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GhostbandProcessor)
};
