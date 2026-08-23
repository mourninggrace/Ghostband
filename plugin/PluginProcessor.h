#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ghostband/Profile.h"
#include "ghostband/Render.h"
#include "ghostband/SongPlan.h"

#include <atomic>
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

    // Diagnostics for the harness: what the audio thread would actually play,
    // as opposed to what the engine says it generated.
    int getSequenceNoteOnCount (int channel) const;
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
    std::atomic<int> editorWidth  { 560 };
    std::atomic<int> editorHeight { 700 };

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

    void setKeyPitchClass (int pitchClass);
    void setStyle         (const juce::String& style);
    void setBassTuning    (const juce::String& tuning);

    int          getKeyPitchClass() const;
    juce::String getStyle() const;
    juce::String getBassTuning() const;

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

    void loadBuiltInPlan();
    void rebuildSequence (const gb::RenderResult& result,
                          const gb::DrumProfile& kitToUse,
                          const gb::BassProfile& bassToUse,
                          const gb::SongPlan& planToUse,
                          const gb::PhraseProfile* guitarToUse,
                          const gb::PhraseProfile* pianoToUse);
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
    gb::PhraseProfile             guitarProfile;
    gb::PhraseProfile             pianoProfile;
    bool                          haveGuitar = false;
    bool                          havePiano  = false;
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

    std::atomic<bool>             calibrating { false };
    std::vector<CalibrationStep>  calibrationSteps;
    bool                          calibrationEdited = false;

    // Where the previous block's window ended. Consecutive blocks are stitched
    // to this rather than recomputed from the host's ppq, because deriving both
    // ends of the window from floating-point ppq lets rounding overlap the
    // windows - and an overlapped window emits the same note twice.
    double nextExpectedTick = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GhostbandProcessor)
};
