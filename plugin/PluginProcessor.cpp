#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "ghostband/Groove.h"
#include "ghostband/MidiFile.h"

#include <algorithm>

GhostbandProcessor::GhostbandProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // The built-in defaults are a General MIDI drum map and a generic bass, so
    // the plugin is useful even before it finds any profile files on disk.
    status.drumProfile = kit.name;
    status.bassProfile = bassProfile.name;
}

GhostbandProcessor::~GhostbandProcessor() = default;

void GhostbandProcessor::prepareToPlay (double, int) {}
void GhostbandProcessor::releaseResources() {}

bool GhostbandProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == out || in.isDisabled();
}

//==============================================================================

bool GhostbandProcessor::resolveProfiles (juce::String& error)
{
    // Profile paths in a plan are written relative to the plan itself, which is
    // how they stay portable. Fall back to the raw path, then to the built-in
    // defaults, so a missing file degrades instead of breaking.
    const juce::File base = planFile.existsAsFile() ? planFile.getParentDirectory()
                                                    : juce::File();

    auto resolve = [&base] (const std::string& p) -> juce::String
    {
        const juce::String path (p);

        if (base != juce::File())
        {
            // Plans live in plans/ and profiles in profiles/, so a plan naturally
            // writes "profiles/ssd5.json" - a path relative to the project root
            // rather than to itself. Try the plan's own folder first, then its
            // parent, which covers that layout without anyone having to think
            // about it.
            const juce::File candidates[] = { base.getChildFile (path),
                                              base.getParentDirectory().getChildFile (path) };
            for (const juce::File& c : candidates)
                if (c.existsAsFile()) return c.getFullPathName();
        }

        const juce::File direct (path);
        if (direct.existsAsFile()) return direct.getFullPathName();
        return {};
    };

    error.clear();

    const juce::String drumPath = resolve (plan.drumProfile);
    if (drumPath.isNotEmpty())
    {
        gb::DrumProfile loaded;
        std::string e;
        if (gb::DrumProfile::load (drumPath.toStdString(), loaded, e))
            kit = loaded;
        else
            error = juce::String (e);
    }

    const juce::String bassPath = resolve (plan.bassProfile);
    if (bassPath.isNotEmpty())
    {
        gb::BassProfile loaded;
        std::string e;
        if (gb::BassProfile::load (bassPath.toStdString(), loaded, e))
            bassProfile = loaded;
        else if (error.isEmpty())
            error = juce::String (e);
    }

    return error.isEmpty();
}

void GhostbandProcessor::loadPlan (const juce::File& file)
{
    gb::SongPlan loaded;
    std::string error;

    if (! gb::SongPlan::load (file.getFullPathName().toStdString(), loaded, error))
    {
        const juce::ScopedLock sl (stateLock);
        status.ok = false;
        status.message = juce::String (error);
        stateChanged.sendChangeMessage();
        return;
    }

    {
        const juce::ScopedLock sl (stateLock);
        plan     = loaded;
        planFile = file;

        // Adopt the plan's dials so the UI reflects the file that was just
        // loaded rather than whatever the sliders happened to be showing.
        complexity.store (plan.complexity);
        humanize.store   (plan.humanize);
        seed.store       (static_cast<int> (plan.seed));

        juce::String profileError;
        resolveProfiles (profileError);
        status.message = profileError;
    }

    regenerate();
}

void GhostbandProcessor::regenerate()
{
    gb::SongPlan working;
    gb::DrumProfile workingKit;
    gb::BassProfile workingBass;

    {
        const juce::ScopedLock sl (stateLock);
        if (plan.sections.empty())
        {
            status.ok = false;
            status.message = "Load a plan to begin.";
            stateChanged.sendChangeMessage();
            return;
        }
        working     = plan;
        workingKit  = kit;
        workingBass = bassProfile;
    }

    working.complexity = complexity.load();
    working.humanize   = humanize.load();
    working.seed       = static_cast<unsigned> (std::max (1, seed.load()));

    // Generation is pure arithmetic and completes in well under a millisecond
    // for a song this size, so it runs on the message thread. When the AI
    // planner lands it will need a network call, and that will have to move to
    // a background thread - the sequence swap below is already built for it.
    const gb::RenderResult result = gb::renderPerformance (working, workingKit, workingBass);

    rebuildSequence (result, workingKit, workingBass, working.bassTuning);

    {
        const juce::ScopedLock sl (stateLock);
        plan     = working;
        sections = result.sections;

        status.ok        = true;
        status.planName  = planFile.existsAsFile() ? planFile.getFileNameWithoutExtension()
                                                   : juce::String (working.title);
        status.bars      = result.totalBars;
        status.seconds   = result.durationSeconds;
        status.drumHits  = static_cast<int> (result.performance.drums.size());
        status.bassNotes = static_cast<int> (result.performance.bass.size());
        status.drumProfile = workingKit.name;
        status.bassProfile = workingBass.name;
        status.unverifiedProfiles = workingKit.needsVerification || workingBass.needsVerification;

        status.headline = juce::String (working.key) + " " + juce::String (working.mode).replace ("_", " ")
                        + "   " + juce::String (working.bpm, 0) + " bpm   "
                        + juce::String (working.timeSigNumerator) + "/"
                        + juce::String (working.timeSigDenominator) + "   "
                        + juce::String (working.style).replace ("_", " ");

        status.message = status.unverifiedProfiles
                       ? "Profiles are unverified - calibrate before trusting the mapping."
                       : juce::String();
    }

    stateChanged.sendChangeMessage();
}

void GhostbandProcessor::rebuildSequence (const gb::RenderResult& result,
                                          const gb::DrumProfile& kitToUse,
                                          const gb::BassProfile& bassToUse,
                                          const std::string& tuning)
{
    // Reuse the exact same profile rendering the CLI uses, then flatten the two
    // tracks into one time-ordered stream the audio thread can walk. The
    // profiles arrive as arguments rather than being read off the members,
    // which would be an unlocked read of state the message thread can change.
    gb::MidiTrack drums, bass;
    kitToUse.render (result.performance.drums, drums);
    bassToUse.render (result.performance.bass, bass, tuning);

    std::vector<TimedMessage> built;
    built.reserve (drums.events.size() + bass.events.size());

    auto append = [&built] (const gb::MidiTrack& track)
    {
        for (const gb::MidiEvent& e : track.events)
        {
            if (e.bytes.empty() || e.bytes[0] == 0xFF)
                continue;   // meta events are for files, not for a live stream

            TimedMessage tm;
            tm.tick    = e.tick;
            tm.order   = e.order;
            tm.message = juce::MidiMessage (e.bytes.data(), static_cast<int> (e.bytes.size()));
            built.push_back (tm);
        }
    };

    append (drums);
    append (bass);

    std::stable_sort (built.begin(), built.end(),
                      [] (const TimedMessage& a, const TimedMessage& b)
                      {
                          if (a.tick != b.tick) return a.tick < b.tick;
                          return a.order < b.order;
                      });

    const int endTick = result.performance.totalTicks;

    {
        const juce::SpinLock::ScopedLockType lock (sequenceLock);
        sequence.swap (built);
        sequenceEndTick = endTick;
    }
}

//==============================================================================

void GhostbandProcessor::sendAllNotesOff (juce::MidiBuffer& midi, int sampleOffset)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        midi.addEvent (juce::MidiMessage::allNotesOff (ch), sampleOffset);
        midi.addEvent (juce::MidiMessage::allSoundOff (ch), sampleOffset);
    }
}

void GhostbandProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Audio passes through untouched; only spare output channels get cleared.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Ghostband produces MIDI rather than forwarding it. Anything arriving on
    // the input is dropped for now; live following will consume it later.
    midi.clear();

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const juce::SpinLock::ScopedTryLockType lock (sequenceLock);
    if (! lock.isLocked())
        return;

    juce::AudioPlayHead* ph = getPlayHead();
    if (ph == nullptr)
        return;

    const auto pos = ph->getPosition();
    if (! pos.hasValue())
        return;

    if (! pos->getIsPlaying())
    {
        if (wasPlaying)
        {
            sendAllNotesOff (midi, 0);
            wasPlaying = false;
            nextExpectedTick = -1.0;
        }
        return;
    }

    if (! wasPlaying)
    {
        wasPlaying = true;
        nextExpectedTick = -1.0;   // fresh start: trust the host's position
    }

    if (sequence.empty())
        return;

    const double bpm = pos->getBpm().orFallback (120.0);
    const double ppq = pos->getPpqPosition().orFallback (0.0);
    const double sr  = getSampleRate();

    // Falling back to a guessed sample rate here would not fail loudly - it
    // would play the whole song at the wrong speed against the host's timeline
    // and drift a little further out of step every block. Silence is the honest
    // answer until the host has told us the rate.
    if (bpm <= 0.0 || sr <= 0.0)
        return;

    const double samplesPerTick = (60.0 / (bpm * gb::kPPQ)) * sr;
    if (samplesPerTick <= 0.0)
        return;

    const double hostTick   = ppq * gb::kPPQ;
    const double blockTicks = numSamples / samplesPerTick;

    // Stitch consecutive blocks to where the last one ended. Computing both
    // edges from the host's ppq independently lets rounding make one block's
    // end overlap the next block's start, which emits the note sitting on that
    // boundary twice - and then its note-off only once, so the note hangs.
    // A jump larger than a block means the host seeked or looped, and then the
    // host's own position is the only trustworthy answer.
    double windowStart = hostTick;
    if (nextExpectedTick >= 0.0)
    {
        const double gap = std::abs (hostTick - nextExpectedTick);
        diagnostics.worstGap = std::max (diagnostics.worstGap, gap);
        if (gap < blockTicks * 0.5)
            windowStart = nextExpectedTick;
        else
            ++diagnostics.stitchMisses;
    }
    ++diagnostics.blocks;

    const double windowEnd = windowStart + blockTicks;
    nextExpectedTick = windowEnd;

    if (windowStart >= sequenceEndTick)
        return;   // the song has finished; it does not loop

    // Binary search rather than a running cursor, so looping and scrubbing in
    // the host are handled without any state to get out of sync.
    const auto first = std::lower_bound (sequence.begin(), sequence.end(), windowStart,
                                         [] (const TimedMessage& m, double t)
                                         { return m.tick < t; });

    for (auto it = first; it != sequence.end() && it->tick < windowEnd; ++it)
    {
        const int offset = juce::jlimit (0, numSamples - 1,
                                         static_cast<int> ((it->tick - windowStart) * samplesPerTick));
        midi.addEvent (it->message, offset);
        ++diagnostics.eventsEmitted;
    }
}

//==============================================================================

GhostbandProcessor::Status GhostbandProcessor::getStatus() const
{
    const juce::ScopedLock sl (stateLock);
    return status;
}

std::vector<gb::SectionReport> GhostbandProcessor::getSections() const
{
    const juce::ScopedLock sl (stateLock);
    return sections;
}

juce::File GhostbandProcessor::getPlanFile() const
{
    const juce::ScopedLock sl (stateLock);
    return planFile;
}

int GhostbandProcessor::getSequenceNoteOnCount (int channel) const
{
    const juce::SpinLock::ScopedLockType lock (sequenceLock);
    int n = 0;
    for (const TimedMessage& m : sequence)
        if (m.message.isNoteOn() && m.message.getChannel() == channel)
            ++n;
    return n;
}

void GhostbandProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("GhostbandState");
    xml.setAttribute ("plan",       getPlanFile().getFullPathName());
    xml.setAttribute ("complexity", complexity.load());
    xml.setAttribute ("humanize",   humanize.load());
    xml.setAttribute ("seed",       seed.load());
    copyXmlToBinary (xml, destData);
}

void GhostbandProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName ("GhostbandState"))
        return;

    complexity.store (xml->getDoubleAttribute ("complexity", 0.5));
    humanize.store   (xml->getDoubleAttribute ("humanize", 0.5));
    seed.store       (xml->getIntAttribute ("seed", 1));

    const juce::File file (xml->getStringAttribute ("plan"));
    if (file.existsAsFile())
        loadPlan (file);
    else
        stateChanged.sendChangeMessage();
}

juce::AudioProcessorEditor* GhostbandProcessor::createEditor()
{
    return new GhostbandEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GhostbandProcessor();
}
