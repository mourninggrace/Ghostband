#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "ghostband/Groove.h"
#include "ghostband/MidiFile.h"
#include "ghostband/Music.h"

#include <algorithm>
#include <cmath>

GhostbandProcessor::GhostbandProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // The built-in defaults are a General MIDI drum map and a generic bass, so
    // the plugin is useful even before it finds any profile files on disk.
    status.drumProfile = kit.name;
    status.bassProfile = bassProfile.name;

    loadBuiltInPlan();
}

const char* GhostbandProcessor::builtInPlanJson()
{
    // Deliberately plain: mid-tempo hard rock, explicit chords, standard tuning,
    // and no profile paths - so it renders correctly against the General MIDI
    // defaults without depending on any file existing anywhere.
    return R"GB({
      "title": "Ghostband Starter",
      "key": "E", "mode": "natural_minor", "bpm": 104,
      "style": "hard_rock", "bass_tuning": "standard", "play_style": "pick",
      "complexity": 0.55, "humanize": 0.55, "seed": 7, "ending": "cymbal_ring",
      "sections": [
        { "name": "intro",   "bars": 4, "intensity": 0.28, "chords": ["Em"], "plays": "drums" },
        { "name": "verse1",  "bars": 8, "intensity": 0.45, "chords": ["Em","Em","C","D"] },
        { "name": "chorus1", "bars": 8, "intensity": 0.85, "chords": ["C","G","D","Em"], "bass": "eighths" },
        { "name": "verse2",  "bars": 8, "intensity": 0.50, "chords": ["Em","Em","C","D"] },
        { "name": "chorus2", "bars": 8, "intensity": 0.88, "chords": ["C","G","D","Em"], "bass": "eighths" },
        { "name": "bridge",  "bars": 8, "intensity": 0.35, "chords": ["Am","Am","C","D"],
          "feel": "half_time", "bass": "roots", "fill": "big" },
        { "name": "ending",  "bars": 4, "intensity": 0.70, "chords": ["C","D","Em","Em"], "fill": "none" }
      ]
    })GB";
}

bool GhostbandProcessor::planIsBuiltIn() const
{
    const juce::ScopedLock sl (stateLock);
    return ! planFile.existsAsFile();
}

void GhostbandProcessor::loadBuiltInPlan()
{
    gb::SongPlan loaded;
    std::string error;

    if (gb::SongPlan::parse (builtInPlanJson(), "built-in plan", loaded, error))
    {
        const juce::ScopedLock sl (stateLock);
        plan     = loaded;
        planFile = juce::File();
        complexity.store (plan.complexity);
        humanize.store   (plan.humanize);
        seed.store       (static_cast<int> (plan.seed));
    }
    else
    {
        jassertfalse;   // the built-in plan is compiled in; it must always parse
        const juce::ScopedLock sl (stateLock);
        status.message = juce::String (error);
        return;
    }

    regenerate();
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

    // Guitar and piano are opt-in. A plan that names no profile has no such
    // part, and the flags below are what stop it being generated at all.
    haveGuitar = false;
    havePiano  = false;

    if (! plan.guitarProfile.empty())
    {
        const juce::String path = resolve (plan.guitarProfile);
        gb::PhraseProfile loaded;
        std::string e;
        if (path.isNotEmpty() && gb::PhraseProfile::load (path.toStdString(), loaded, e))
        {
            guitarProfile = loaded;
            haveGuitar = true;
        }
        else if (error.isEmpty())
            error = e.empty() ? ("could not find " + juce::String (plan.guitarProfile))
                              : juce::String (e);
    }

    if (! plan.pianoProfile.empty())
    {
        const juce::String path = resolve (plan.pianoProfile);
        gb::PhraseProfile loaded;
        std::string e;
        if (path.isNotEmpty() && gb::PhraseProfile::load (path.toStdString(), loaded, e))
        {
            pianoProfile = loaded;
            havePiano = true;
        }
        else if (error.isEmpty())
            error = e.empty() ? ("could not find " + juce::String (plan.pianoProfile))
                              : juce::String (e);
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

int GhostbandProcessor::getKeyPitchClass() const
{
    const juce::ScopedLock sl (stateLock);
    bool ok = false;
    const int base = gb::pitchClassFromName (plan.key, ok);
    return ((((ok ? base : 4) + plan.transpose) % 12) + 12) % 12;
}

void GhostbandProcessor::setKeyPitchClass (int pitchClass)
{
    {
        const juce::ScopedLock sl (stateLock);
        bool ok = false;
        const int base = gb::pitchClassFromName (plan.key, ok);
        plan.transpose = (((pitchClass - (ok ? base : 4)) % 12) + 12) % 12;
    }
    regenerate();
}

juce::String GhostbandProcessor::getStyle() const
{
    const juce::ScopedLock sl (stateLock);
    return juce::String (plan.style);
}

void GhostbandProcessor::setStyle (const juce::String& style)
{
    {
        const juce::ScopedLock sl (stateLock);
        plan.style = style.toStdString();
    }
    regenerate();
}

juce::String GhostbandProcessor::getBassTuning() const
{
    const juce::ScopedLock sl (stateLock);
    return juce::String (plan.bassTuning);
}

void GhostbandProcessor::setBassTuning (const juce::String& tuning)
{
    {
        const juce::ScopedLock sl (stateLock);
        plan.bassTuning = tuning.toStdString();
    }
    regenerate();
}

void GhostbandProcessor::reloadPlan()
{
    const juce::File current = getPlanFile();

    if (current.existsAsFile())
        loadPlan (current);
    else
        loadBuiltInPlan();
}

void GhostbandProcessor::regenerate()
{
    gb::SongPlan working;
    gb::DrumProfile workingKit;
    gb::BassProfile workingBass;
    gb::PhraseProfile workingGuitar, workingPiano;
    bool withGuitar = false, withPiano = false;

    {
        const juce::ScopedLock sl (stateLock);
        if (plan.sections.empty())
        {
            status.ok = false;
            status.message = "Load a plan to begin.";
            stateChanged.sendChangeMessage();
            return;
        }
        working       = plan;
        workingKit    = kit;
        workingBass   = bassProfile;
        workingGuitar = guitarProfile;
        workingPiano  = pianoProfile;
        withGuitar    = haveGuitar;
        withPiano     = havePiano;
    }

    const gb::PhraseProfile* guitarPtr = withGuitar ? &workingGuitar : nullptr;
    const gb::PhraseProfile* pianoPtr  = withPiano  ? &workingPiano  : nullptr;

    working.complexity = complexity.load();
    working.humanize   = humanize.load();
    working.seed       = static_cast<unsigned> (std::max (1, seed.load()));

    // Generation is pure arithmetic and completes in well under a millisecond
    // for a song this size, so it runs on the message thread. When the AI
    // planner lands it will need a network call, and that will have to move to
    // a background thread - the sequence swap below is already built for it.
    const gb::RenderResult result = gb::renderPerformance (working, workingKit, workingBass,
                                                           guitarPtr, pianoPtr);

    rebuildSequence (result, workingKit, workingBass, working, guitarPtr, pianoPtr);

    {
        const juce::ScopedLock sl (stateLock);
        plan     = working;
        sections = result.sections;

        status.ok        = true;
        status.planName  = planFile.existsAsFile()
                             ? planFile.getFileNameWithoutExtension()
                             : juce::String (working.title) + "   (built-in)";
        status.bars      = result.totalBars;
        status.seconds   = result.durationSeconds;
        status.drumHits  = static_cast<int> (result.performance.drums.size());
        status.bassNotes = static_cast<int> (result.performance.bass.size());
        status.drumProfile   = workingKit.name;
        status.bassProfile   = workingBass.name;
        status.guitarProfile = withGuitar ? juce::String (workingGuitar.name) : juce::String();
        status.pianoProfile  = withPiano  ? juce::String (workingPiano.name)  : juce::String();

        status.unverifiedProfiles = workingKit.needsVerification
                                 || workingBass.needsVerification
                                 || (withGuitar && workingGuitar.needsVerification)
                                 || (withPiano  && workingPiano.needsVerification);

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
                                          const gb::SongPlan& planToUse,
                                          const gb::PhraseProfile* guitarToUse,
                                          const gb::PhraseProfile* pianoToUse)
{
    // Reuse the exact same profile rendering the CLI uses, then flatten the two
    // tracks into one time-ordered stream the audio thread can walk. The
    // profiles arrive as arguments rather than being read off the members,
    // which would be an unlocked read of state the message thread can change.
    gb::MidiTrack drums, bass;
    kitToUse.render (result.performance.drums, drums);
    bassToUse.render (result.performance.bass, bass, planToUse.bassTuning);

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

    if (guitarToUse != nullptr && ! result.performance.guitar.chords.empty())
    {
        gb::MidiTrack t;
        guitarToUse->render (result.performance.guitar, t);
        append (t);
    }

    if (pianoToUse != nullptr && ! result.performance.piano.chords.empty())
    {
        gb::MidiTrack t;
        pianoToUse->render (result.performance.piano, t);
        append (t);
    }

    std::stable_sort (built.begin(), built.end(),
                      [] (const TimedMessage& a, const TimedMessage& b)
                      {
                          if (a.tick != b.tick) return a.tick < b.tick;
                          return a.order < b.order;
                      });

    const int endTick = result.performance.totalTicks;

    std::vector<SectionRange> ranges;
    ranges.reserve (result.sections.size());
    for (const gb::SectionReport& s : result.sections)
        ranges.push_back ({ s.startTick, s.endTick });

    const int beat = std::max (1, gb::kPPQ * 4 / std::max (1, planToUse.timeSigDenominator));
    const int bar  = beat * std::max (1, planToUse.timeSigNumerator);

    {
        const juce::SpinLock::ScopedLockType lock (sequenceLock);
        sequence.swap (built);
        sectionRanges.swap (ranges);
        sequenceEndTick = endTick;
        barTicks = bar;
    }
}

//==============================================================================

void GhostbandProcessor::sendAllNotesOff (juce::MidiBuffer& midi, int sampleOffset)
{
    // Explicit note-offs for everything currently sounding. All Notes Off is a
    // controller message and a great many instruments simply ignore it, so
    // leaning on it alone leaves notes hanging - the most audible failure this
    // plugin could have, and exactly what happened across a section jump.
    for (int ch = 0; ch < 16; ++ch)
    {
        for (int note = 0; note < 128; ++note)
        {
            while (activeNoteCount[ch][note] > 0)
            {
                midi.addEvent (juce::MidiMessage::noteOff (ch + 1, note), sampleOffset);
                --activeNoteCount[ch][note];
            }
        }
    }

    // Belt and braces for anything holding a note we did not start.
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
            transportRunning.store (false);
            activeSection.store (-1);
        }
        return;
    }

    if (! wasPlaying)
    {
        wasPlaying = true;
        nextExpectedTick = -1.0;   // fresh start: trust the host's position
        jumpOffset = 0.0;          // and start the song from the top
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

    nextExpectedTick = windowStart + blockTicks;

    transportRunning.store (true);
    hostBpm.store (bpm);

    // Song position is the host position shifted by whatever section jumps have
    // happened. Everything below works in song time.
    double songStart = windowStart + jumpOffset;
    const double songEnd = songStart + blockTicks;

    // Emits the sequence over a span of song time, placing each event relative
    // to a given sample position. Split spans are how a mid-block jump works.
    auto emitSpan = [&] (double fromTick, double toTick, double sampleAtFrom)
    {
        // Binary search rather than a running cursor, so scrubbing, looping and
        // jumping are all handled without any state that can fall out of sync.
        auto it = std::lower_bound (sequence.begin(), sequence.end(), fromTick,
                                    [] (const TimedMessage& m, double t) { return m.tick < t; });

        for (; it != sequence.end() && it->tick < toTick; ++it)
        {
            const int offset = juce::jlimit (0, numSamples - 1,
                                             static_cast<int> (sampleAtFrom
                                                 + (it->tick - fromTick) * samplesPerTick));
            midi.addEvent (it->message, offset);
            ++diagnostics.eventsEmitted;

            const int ch = it->message.getChannel() - 1;
            if (ch >= 0 && ch < 16)
            {
                const int note = it->message.getNoteNumber();
                if (note >= 0 && note < 128)
                {
                    if (it->message.isNoteOn() && activeNoteCount[ch][note] < 255)
                        ++activeNoteCount[ch][note];
                    else if (it->message.isNoteOff() && activeNoteCount[ch][note] > 0)
                        --activeNoteCount[ch][note];
                }
            }
        }
    };

    const int queued = queuedSection.load();
    const bool jumpPending = queued >= 0 && queued < static_cast<int> (sectionRanges.size())
                             && barTicks > 0;

    if (jumpPending)
    {
        // Land on the next bar line so the band never falls off the beat. If the
        // block already starts exactly on one, go immediately.
        double boundary = std::ceil (songStart / barTicks) * barTicks;

        if (boundary < songEnd)
        {
            const double sampleAtBoundary = (boundary - songStart) * samplesPerTick;

            emitSpan (songStart, boundary, 0.0);

            // Cut every sounding note before moving, or anything ringing across
            // the seam hangs for the rest of the song.
            sendAllNotesOff (midi, juce::jlimit (0, numSamples - 1,
                                                 static_cast<int> (sampleAtBoundary)));

            const double target = sectionRanges[static_cast<size_t> (queued)].startTick;
            jumpOffset += target - boundary;

            emitSpan (target, target + (songEnd - boundary), sampleAtBoundary);

            queuedSection.store (-1);
            songStart = target;   // for the reporting below

            const int tickNow = static_cast<int> (target);
            playbackTick.store (tickNow);
            activeSection.store (queued);
            return;
        }
    }

    playbackTick.store (static_cast<int> (songStart));

    int nowIn = -1;
    for (size_t i = 0; i < sectionRanges.size(); ++i)
        if (songStart >= sectionRanges[i].startTick && songStart < sectionRanges[i].endTick)
            { nowIn = static_cast<int> (i); break; }
    activeSection.store (nowIn);

    if (songStart >= sequenceEndTick)
        return;   // the song has finished; it does not loop on its own

    emitSpan (songStart, songEnd, 0.0);
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

int GhostbandProcessor::getSequencePitchSum (int channel) const
{
    const juce::SpinLock::ScopedLockType lock (sequenceLock);
    int sum = 0;
    for (const TimedMessage& m : sequence)
        if (m.message.isNoteOn() && m.message.getChannel() == channel)
            sum += m.message.getNoteNumber();
    return sum;
}

int GhostbandProcessor::getBarTicks() const
{
    const juce::SpinLock::ScopedLockType lock (sequenceLock);
    return barTicks;
}

void GhostbandProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("GhostbandState");
    xml.setAttribute ("plan",       getPlanFile().getFullPathName());
    xml.setAttribute ("complexity", complexity.load());
    xml.setAttribute ("humanize",   humanize.load());
    xml.setAttribute ("seed",       seed.load());
    xml.setAttribute ("editorW",    editorWidth.load());
    xml.setAttribute ("editorH",    editorHeight.load());
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
    editorWidth.store  (juce::jlimit (460, 2200, xml->getIntAttribute ("editorW", 560)));
    editorHeight.store (juce::jlimit (520, 2000, xml->getIntAttribute ("editorH", 700)));

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
