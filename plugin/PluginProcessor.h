#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ghostband/Profile.h"
#include "ghostband/Render.h"
#include "ghostband/SongPlan.h"

#include <atomic>
#include <map>
#include <vector>

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
    int getSequencePitchSum   (int channel) const;
    int getBarTicks() const;

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
    std::atomic<int> editorWidth  { 800 };
    std::atomic<int> editorHeight { 960 };

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
    void togglePaused()  { paused.store (! paused.load()); }

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

        // A note to hold while `note` sounds, or -1 for none.
        //
        // Whether an instrument can reach a note is two questions, not one. A
        // sampled guitar picks a STRING per note, and a note that arrives while
        // another is still sounding is a legato transition on the string
        // already in use rather than a fresh pick - so it can be perfectly
        // audible alone and silent underneath something else.
        //
        // Ghostband writes 78% of its low second-guitar notes overlapped, so
        // that is not a hypothetical. Testing it by hand needs two hands at a
        // keyboard, which is not available to somebody clicking Kontakt's
        // on-screen keys with a mouse - so the plugin holds the note itself.
        int  under   = -1;
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

    void addSection (int afterIndex);
    void deleteSection (int index);
    void moveSection (int index, int delta);

    bool savePlan (const juce::File& target, juce::String& error);
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
    std::atomic<int> theme { 0 };

    std::atomic<double> fills { 0.62 };

    std::atomic<double> complexity { 0.5 };
    std::atomic<double> humanize   { 0.5 };
    std::atomic<int>    seed       { 1 };

    juce::ChangeBroadcaster stateChanged;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GhostbandProcessor)
};
