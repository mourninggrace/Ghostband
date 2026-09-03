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
    // A full band, not a two-piece. It names the profiles that ship inside the
    // plugin bundle, so guitar and piano play out of the box - the previous
    // version named no profiles at all, which meant the default song could only
    // ever be drums and bass and looked exactly like guitar and piano being
    // broken.
    //
    // Bass is pinned to lock_kick and eighths rather than left on auto: at low
    // intensities auto can legitimately draw "roots", one note per bar, which
    // on a first listen reads as the bass dropping out.
    return R"GB({
      "title": "Ghostband Starter",
      "key": "E", "mode": "natural_minor", "bpm": 104,
      "style": "hard_rock", "bass_tuning": "standard", "play_style": "pick",
      "complexity": 0.55, "humanize": 0.55, "seed": 7, "ending": "cymbal_ring",

      "drum_profile":   "profiles/ssd5-terry-date.json",
      "bass_profile":   "profiles/modo-bass-2.json",
      "guitar_profile": "profiles/vg-iron2.json",
      "piano_profile":  "profiles/virtual-pianist.json",

      "sections": [
        { "name": "intro",   "bars": 4, "intensity": 0.30, "chords": ["Em"],
          "plays": "drums+bass", "bass": "lock_kick" },
        { "name": "verse1",  "bars": 8, "intensity": 0.45, "chords": ["Em","Em","C","D"],
          "plays": "drums+bass+guitar", "bass": "lock_kick", "guitar": "muted" },
        { "name": "chorus1", "bars": 8, "intensity": 0.85, "chords": ["C","G","D","Em"],
          "bass": "eighths", "guitar": "open", "piano": "open" },
        { "name": "verse2",  "bars": 8, "intensity": 0.50, "chords": ["Em","Em","C","D"],
          "plays": "drums+bass+guitar", "bass": "lock_kick", "guitar": "muted" },
        { "name": "chorus2", "bars": 8, "intensity": 0.88, "chords": ["C","G","D","Em"],
          "bass": "eighths", "guitar": "open", "piano": "open" },
        { "name": "bridge",  "bars": 8, "intensity": 0.35, "chords": ["Am","Am","C","D"],
          "feel": "half_time", "bass": "lock_kick", "fill": "big",
          "plays": "drums+bass+piano", "piano": "sparse" },
        { "name": "solo",    "bars": 8, "intensity": 0.80, "chords": ["Em","C","G","D"],
          "bass": "eighths", "guitar": "driving", "piano": "driving" },
        { "name": "ending",  "bars": 4, "intensity": 0.70, "chords": ["C","D","Em","Em"],
          "bass": "lock_kick", "fill": "none", "guitar": "open", "piano": "open" }
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

        // This was missing, and it is why the built-in song had no guitar or
        // piano: without it the profiles are never loaded, so the parts are
        // never enabled and calibration has nothing to show for them either.
        juce::String profileError;
        resolveProfiles (profileError);
        status.message = profileError;
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

// The preset songs ship inside the plugin, so Load plan opens on them rather
// than on an empty Documents folder. Falls back to Documents if the bundle was
// installed without them.
double GhostbandProcessor::getPlanBpm() const
{
    const juce::ScopedLock sl (stateLock);
    return plan.bpm > 0.0 ? plan.bpm : 120.0;
}

juce::File GhostbandProcessor::bundledPlansFolder() const
{
    const juce::File plans =
        juce::File::getSpecialLocation (juce::File::currentExecutableFile)
            .getParentDirectory()
            .getParentDirectory()
            .getChildFile ("Resources")
            .getChildFile ("plans");

    return plans.isDirectory()
             ? plans
             : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}

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

    // Profiles are installed inside the plugin bundle, so the built-in plan can
    // name real ones and still work on a machine that has no copy of this
    // repository. currentExecutableFile is the plugin's own binary, not the
    // host's.
    const juce::File bundleResources =
        juce::File::getSpecialLocation (juce::File::currentExecutableFile)
            .getParentDirectory()          // .../Contents/x86_64-win
            .getParentDirectory()          // .../Contents
            .getChildFile ("Resources");

    auto resolve = [&base, &bundleResources] (const std::string& p) -> juce::String
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

        const juce::File bundled = bundleResources.getChildFile (path);
        if (bundled.existsAsFile()) return bundled.getFullPathName();

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

    // A named profile that cannot be found falls back to the generic defaults
    // rather than dropping the part. Silently removing an instrument the song
    // asked for is indistinguishable from the feature being broken - which is
    // exactly how it was reported.
    auto loadPhrase = [&resolve, &error] (const std::string& named, int channel,
                                          gb::PhraseProfile& target, bool& present)
    {
        present = false;
        if (named.empty())
            return;

        const juce::String path = resolve (named);
        gb::PhraseProfile loaded;
        std::string e;

        if (path.isNotEmpty() && gb::PhraseProfile::load (path.toStdString(), loaded, e))
        {
            target = loaded;
            present = true;
            return;
        }

        target = gb::PhraseProfile();
        target.channel = channel;
        target.name    = "generic (profile not found)";
        target.needsVerification = true;
        target.verificationNote  = "Could not find " + named + " - playing plain chords.";
        present = true;

        if (error.isEmpty())
            error = "Using generic settings: " + juce::String (named) + " was not found.";
    };

    loadPhrase (plan.guitarProfile, channelGuitar.load(), guitarProfile, haveGuitar);
    loadPhrase (plan.pianoProfile,  channelPiano.load(),  pianoProfile,  havePiano);

    // The user's channel assignment wins over whatever a profile happens to say.
    kit.channel           = juce::jlimit (1, 16, channelDrums.load());
    bassProfile.channel   = juce::jlimit (1, 16, channelBass.load());
    guitarProfile.channel = juce::jlimit (1, 16, channelGuitar.load());
    pianoProfile.channel  = juce::jlimit (1, 16, channelPiano.load());

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

//==============================================================================
// Calibration

void GhostbandProcessor::enterCalibration()
{
    std::vector<CalibrationStep> steps;

    {
        const juce::ScopedLock sl (stateLock);

        // Drums first: seventeen voices is the biggest and most error-prone map,
        // and the one the headless probe could not read.
        for (int v = 0; v < static_cast<int> (gb::DrumVoice::Count); ++v)
        {
            const gb::DrumVoice voice = static_cast<gb::DrumVoice> (v);
            const int note = kit.noteFor (voice);
            if (note < 0) continue;

            CalibrationStep s;
            s.label   = juce::String (gb::drumVoiceName (voice)).replace ("_", " ");
            s.hint    = "should sound like a " + s.label;
            s.note    = note;
            s.channel = kit.channel;
            s.isDrum  = true;
            steps.push_back (s);
        }

        // Then the bass, whose lowest playable note decides how heavy a drop
        // tuning actually sounds.
        {
            CalibrationStep s;
            s.label   = "bass lowest note";
            s.hint    = "the lowest note the bass can play in " + juce::String (plan.bassTuning);
            s.note    = bassProfile.lowestNoteFor (plan.bassTuning);
            s.channel = bassProfile.channel;
            s.isDrum  = false;
            steps.push_back (s);

            CalibrationStep oct;
            oct.label   = "bass octave up";
            oct.hint    = "an octave above the lowest note";
            oct.note    = juce::jmin (127, s.note + 12);
            oct.channel = bassProfile.channel;
            oct.isDrum  = false;
            steps.push_back (oct);
        }

        // Guitar and piano. A phrase instrument gets its phrase keys, since a
        // wrong one triggers the wrong riff; a note-driven one gets the top and
        // bottom of the range it will be voiced into.
        auto addPhraseSteps = [&steps] (const gb::PhraseProfile& p, const juce::String& what,
                                        bool present)
        {
            if (! present) return;

            if (p.isPhraseDriven())
            {
                for (const auto& key : p.allPhraseKeys())
                {
                    CalibrationStep s;
                    s.label   = what + " phrase: " + juce::String (gb::phraseFeelName (key.first));
                    s.hint    = "should start a " + juce::String (gb::phraseFeelName (key.first))
                              + " " + what + " part";
                    s.note    = key.second;
                    s.channel = p.channel;
                    s.isDrum  = false;
                    steps.push_back (s);
                }
            }

            CalibrationStep lo;
            lo.label   = what + " lowest chord note";
            lo.hint    = "the bottom of the range " + what + " chords are voiced into";
            lo.note    = p.chordLowest;
            lo.channel = p.channel;
            lo.isDrum  = false;
            steps.push_back (lo);

            CalibrationStep hi;
            hi.label   = what + " highest chord note";
            hi.hint    = "the top of that range";
            hi.note    = p.chordHighest;
            hi.channel = p.channel;
            hi.isDrum  = false;
            steps.push_back (hi);
        };

        addPhraseSteps (guitarProfile, "guitar", haveGuitar);
        addPhraseSteps (pianoProfile,  "piano",  havePiano);
    }

    {
        const juce::ScopedLock sl (stateLock);
        calibrationSteps = std::move (steps);
        calibrationEdited = false;
    }

    calibrating.store (true);
    stateChanged.sendChangeMessage();
}

void GhostbandProcessor::exitCalibration()
{
    calibrating.store (false);
    stateChanged.sendChangeMessage();
}

int GhostbandProcessor::getCalibrationStepCount() const
{
    const juce::ScopedLock sl (stateLock);
    return static_cast<int> (calibrationSteps.size());
}

GhostbandProcessor::CalibrationStep GhostbandProcessor::getCalibrationStep (int index) const
{
    const juce::ScopedLock sl (stateLock);
    if (index < 0 || index >= static_cast<int> (calibrationSteps.size()))
        return {};
    return calibrationSteps[static_cast<size_t> (index)];
}

void GhostbandProcessor::applyChannels()
{
    {
        const juce::ScopedLock sl (stateLock);
        kit.channel           = juce::jlimit (1, 16, channelDrums.load());
        bassProfile.channel   = juce::jlimit (1, 16, channelBass.load());
        guitarProfile.channel = juce::jlimit (1, 16, channelGuitar.load());
        pianoProfile.channel  = juce::jlimit (1, 16, channelPiano.load());
    }
    regenerate();
    sendLevels();
}

void GhostbandProcessor::testPart (int part)
{
    // Deliberately unmistakable: a drum pattern, a walking bass figure, and a
    // held chord for the pitched parts. If you hear nothing, that part's MIDI is
    // not arriving.
    struct Figure { int channel; std::vector<int> notes; bool chord; };

    Figure f;
    {
        const juce::ScopedLock sl (stateLock);

        // A pitched part is tested inside the range its own profile says the
        // instrument sounds in, never at fixed notes.
        //
        // The guitar was hard-coded to 52, 55, 59 - every one of which is in
        // IRON 2's silent keyswitch zone, which is why Test sent visible MIDI
        // and made no sound whatsoever. A guessed range is exactly what the
        // profiles exist to replace.
        const auto chordIn = [] (const gb::PhraseProfile& prof)
        {
            const int lo   = juce::jlimit (0, 120, prof.chordLowest);
            const int span = juce::jmax (7, prof.chordHighest - lo);
            const int root = lo + juce::jmin (2, span / 8);

            return std::vector<int> { root,
                                      juce::jmin (127, root + 4),
                                      juce::jmin (127, root + 7) };
        };

        switch (part)
        {
            case 1:
            {
                // Walks up from the lowest note the profile claims, through an
                // octave above it. If only the higher notes sound, the claimed
                // bottom of the range is wrong - which is a thing worth being
                // able to hear rather than having to guess at.
                const int lo = juce::jlimit (12, 100, bassProfile.lowestNoteFor (plan.bassTuning));
                f = { bassProfile.channel, { lo, lo + 5, lo + 12, lo + 17 }, false };
                break;
            }
            case 2:  f = { guitarProfile.channel, chordIn (guitarProfile), true }; break;
            case 3:  f = { pianoProfile.channel,  chordIn (pianoProfile),  true }; break;
            default: f = { kit.channel, { kit.noteFor (gb::DrumVoice::Kick),
                                          kit.noteFor (gb::DrumVoice::HatClosed),
                                          kit.noteFor (gb::DrumVoice::Snare),
                                          kit.noteFor (gb::DrumVoice::HatClosed) }, false };
                     break;
        }

        // A kit that is missing a piece reports -1 for it; dropping those beats
        // is better than firing note -1 at something.
        f.notes.erase (std::remove_if (f.notes.begin(), f.notes.end(),
                                       [] (int n) { return n < 0 || n > 127; }),
                       f.notes.end());
    }

    if (f.notes.empty())
        return;

    {
        static const char* names[] = { "drums", "bass", "guitar", "piano" };
        juce::String notes;
        for (int n : f.notes)
            notes += (notes.isEmpty() ? "" : " ") + juce::String (n);

        const juce::ScopedLock sl (stateLock);
        lastMidiReport = juce::String ("Test ") + names[juce::jlimit (0, 3, part)]
                       + ": notes " + notes + " on channel " + juce::String (f.channel);
    }

    const double sr = juce::jmax (8000.0, getSampleRate());
    const int step = static_cast<int> (0.32 * sr);
    const int hold = static_cast<int> (0.28 * sr);

    const juce::SpinLock::ScopedLockType lock (auditionLock);

    // Make sure the part is audible before testing it - a level knob left down
    // would look exactly like broken routing.
    pendingAuditions.push_back ({ 0, juce::MidiMessage::controllerEvent (f.channel, 7, 127) });

    if (f.chord)
    {
        for (int n : f.notes)
        {
            pendingAuditions.push_back ({ 0, juce::MidiMessage::noteOn (f.channel, n, (juce::uint8) 100) });
            pendingAuditions.push_back ({ static_cast<int> (1.2 * sr),
                                          juce::MidiMessage::noteOff (f.channel, n) });
        }
    }
    else
    {
        int at = 0;
        for (int n : f.notes)
        {
            pendingAuditions.push_back ({ at, juce::MidiMessage::noteOn (f.channel, n, (juce::uint8) 105) });
            pendingAuditions.push_back ({ at + hold, juce::MidiMessage::noteOff (f.channel, n) });
            at += step;
        }
    }
}

void GhostbandProcessor::teachControl (int part, int cc)
{
    int channel = 2;
    {
        const juce::ScopedLock sl (stateLock);
        switch (part)
        {
            case 0:  channel = kit.channel;           break;
            case 1:  channel = bassProfile.channel;   break;
            case 3:  channel = pianoProfile.channel;  break;
            default: channel = guitarProfile.channel; break;
        }
    }

    const double sr = juce::jmax (8000.0, getSampleRate());

    const juce::SpinLock::ScopedLockType lock (auditionLock);
    // A full sweep down and back up over about a second, so the movement is
    // unmistakable to whatever is listening.
    for (int i = 0; i <= 24; ++i)
    {
        const int value = (i <= 12) ? (i * 127 / 12) : ((24 - i) * 127 / 12);
        pendingAuditions.push_back ({ static_cast<int> (i * 0.04 * sr),
                                      juce::MidiMessage::controllerEvent (channel, cc, value) });
    }
}

// The channel a part speaks on. Duplicated nowhere else, because getting it
// wrong sends a controller message to the wrong instrument.
int GhostbandProcessor::channelForPart (int part) const
{
    const juce::ScopedLock sl (stateLock);
    switch (part)
    {
        case 0:  return kit.channel;
        case 1:  return bassProfile.channel;
        case 3:  return pianoProfile.channel;
        default: return guitarProfile.channel;
    }
}

void GhostbandProcessor::sendControlNow (int part, int index)
{
    int cc = -1;
    double amount = 0.0;

    {
        const juce::ScopedLock sl (stateLock);
        const auto* prof = controlSetFor (part);
        if (prof == nullptr || index < 0
            || index >= static_cast<int> (prof->all().size()))
            return;

        const auto& def = prof->all()[static_cast<size_t> (index)];
        cc = def.cc;

        // valueAt(0) is the bottom of the declared range, which for a parked
        // control is the value it was parked at.
        amount = def.valueAt (0.0);
    }

    if (cc < 0)
        return;

    const int value   = juce::jlimit (0, 127, juce::roundToInt (amount * 127.0));

    // Resolved before the audition lock is taken: channelForPart wants the
    // state lock, and taking the two in this order here and the other order
    // anywhere else is how a deadlock gets built.
    const int channel = channelForPart (part);

    const juce::SpinLock::ScopedLockType lock (auditionLock);
    pendingAuditions.push_back ({ 0, juce::MidiMessage::controllerEvent (channel, cc, value) });
}

void GhostbandProcessor::walkControl (int part, int index)
{
    int cc = -1, positions = 0;

    {
        const juce::ScopedLock sl (stateLock);
        const auto* prof = controlSetFor (part);
        if (prof == nullptr || index < 0
            || index >= static_cast<int> (prof->all().size()))
            return;

        const auto& def = prof->all()[static_cast<size_t> (index)];
        cc        = def.cc;
        positions = def.isSelect() ? def.positions : 0;
    }

    if (cc < 0 || positions < 2)
        return;

    const double sr      = juce::jmax (8000.0, getSampleRate());
    const int    channel = channelForPart (part);

    // Half a second on each, which is slow enough to read a name off a plugin's
    // display and still finishes a thirty-way list in fifteen seconds.
    const juce::SpinLock::ScopedLockType lock (auditionLock);
    for (int p = 0; p < positions; ++p)
    {
        const int value = juce::jlimit (0, 127,
                              juce::roundToInt (p * 127.0 / (positions - 1)));
        pendingAuditions.push_back ({ static_cast<int> (p * 0.5 * sr),
                                      juce::MidiMessage::controllerEvent (channel, cc, value) });
    }
}

gb::PhraseProfile* GhostbandProcessor::phraseProfileFor (int part)
{
    if (part == 3) return havePiano  ? &pianoProfile  : nullptr;
    if (part == 2) return haveGuitar ? &guitarProfile : nullptr;
    return nullptr;
}

// Controls belong to every part, not only the two phrase ones. Drums and bass
// have knobs worth reaching and volumes the mix knobs have to find.
// Guitar and piano are optional - a plan that names no profile for them has no
// such instrument, and neither its controls nor its Test button mean anything.
// Saying so is the difference between "this song has no guitar" and what looked
// like every saved mapping being lost.
bool GhostbandProcessor::partIsInSong (int part) const
{
    const juce::ScopedLock sl (stateLock);
    switch (part)
    {
        case 2:  return haveGuitar;
        case 3:  return havePiano;
        default: return true;      // drums and bass are always present
    }
}

gb::ControlSet* GhostbandProcessor::controlSetFor (int part)
{
    switch (part)
    {
        case 0:  return &kit.controls;
        case 1:  return &bassProfile.controls;
        case 2:  return haveGuitar ? &guitarProfile.controls : nullptr;
        case 3:  return havePiano  ? &pianoProfile.controls  : nullptr;
        default: return nullptr;
    }
}

const gb::ControlSet* GhostbandProcessor::controlSetFor (int part) const
{
    return const_cast<GhostbandProcessor*> (this)->controlSetFor (part);
}

// Where a part's mappings are written back to.
std::string GhostbandProcessor::controlSourcePath (int part) const
{
    switch (part)
    {
        case 0:  return kit.sourcePath;
        case 1:  return bassProfile.sourcePath;
        case 2:  return haveGuitar ? guitarProfile.sourcePath : std::string();
        case 3:  return havePiano  ? pianoProfile.sourcePath  : std::string();
        default: return {};
    }
}

const gb::PhraseProfile* GhostbandProcessor::phraseProfileFor (int part) const
{
    return const_cast<GhostbandProcessor*> (this)->phraseProfileFor (part);
}

juce::String GhostbandProcessor::controlOwnerName (int part) const
{
    const juce::ScopedLock sl (stateLock);
    const auto* p = phraseProfileFor (part);
    return p != nullptr ? juce::String (p->name) : juce::String ("no instrument");
}

int GhostbandProcessor::getControlCount (int part) const
{
    const juce::ScopedLock sl (stateLock);
    const auto* p = controlSetFor (part);
    return p != nullptr ? static_cast<int> (p->all().size()) : 0;
}

GhostbandProcessor::ControlSlot GhostbandProcessor::getControl (int part, int index) const
{
    const juce::ScopedLock sl (stateLock);
    ControlSlot s;
    const auto* p = controlSetFor (part);
    if (p == nullptr || index < 0 || index >= static_cast<int> (p->all().size()))
        return s;

    const auto& c = p->all()[static_cast<size_t> (index)];
    s.name    = c.name;
    s.cc      = c.cc;
    s.follows = c.follows;
    s.type      = c.type;
    s.positions = c.positions;
    s.low       = c.low;
    s.high      = c.high;
    return s;
}

void GhostbandProcessor::addControl (int part)
{
    {
        const juce::ScopedLock sl (stateLock);
        auto* p = controlSetFor (part);
        if (p == nullptr) return;

        gb::ControlDef c;
        c.name    = "new control";
        c.cc      = p->nextFreeCC();
        c.follows = "intensity";
        if (c.cc < 0) return;   // every controller already spoken for

        p->editable().push_back (c);
    }
    stateChanged.sendChangeMessage();
}

void GhostbandProcessor::removeControl (int part, int index)
{
    {
        const juce::ScopedLock sl (stateLock);
        auto* p = controlSetFor (part);
        if (p == nullptr) return;
        auto& list = p->editable();
        if (index < 0 || index >= static_cast<int> (list.size())) return;
        list.erase (list.begin() + static_cast<std::ptrdiff_t> (index));
    }
    regenerate();
}

void GhostbandProcessor::updateControl (int part, int index, const ControlSlot& slot)
{
    {
        const juce::ScopedLock sl (stateLock);
        auto* p = controlSetFor (part);
        if (p == nullptr) return;
        auto& list = p->editable();
        if (index < 0 || index >= static_cast<int> (list.size())) return;

        auto& c = list[static_cast<size_t> (index)];
        c.name    = slot.name.trim().isEmpty() ? "control" : slot.name.trim().toStdString();
        c.follows = slot.follows.toStdString();
        c.type      = slot.type.toStdString();
        c.positions = juce::jlimit (0, 128, slot.positions);
        c.low       = juce::jlimit (0.0, 1.0, slot.low);
        c.high      = juce::jlimit (0.0, 1.0, slot.high);

        // A selector with fewer than two positions is not a selector. Rather
        // than silently behaving as a knob, give it a sane default the moment
        // the type is chosen, so the list never shows an impossible mapping.
        if (c.type == "select" && c.positions < 2)
            c.positions = 3;
    }
    regenerate();
}

void GhostbandProcessor::teachControlSlot (int part, int index)
{
    const ControlSlot s = getControl (part, index);
    if (s.cc >= 0)
        teachControl (part, s.cc);
}

bool GhostbandProcessor::saveControls (int part, juce::String& error)
{
    std::string path, e;
    bool ok = false;

    {
        const juce::ScopedLock sl (stateLock);

        path = controlSourcePath (part);
        if (path.empty())
        {
            error = "That part has no instrument profile to save into.";
            return false;
        }

        switch (part)
        {
            case 0:  ok = kit.save (path, e);           break;
            case 1:  ok = bassProfile.save (path, e);   break;
            case 2:  ok = guitarProfile.save (path, e); break;
            default: ok = pianoProfile.save (path, e);  break;
        }
    }

    if (! ok)
    {
        error = juce::String (e);
        return false;
    }

    error.clear();
    return true;
}

void GhostbandProcessor::sendLevels()
{
    // Where each mix knob has to reach.
    //
    // This used to send CC 7 - channel volume - and nothing else, on an
    // assumption never tested against a real instrument. Most instrument
    // plugins do not implement CC 7 at all; they expose volume as their own
    // parameter. So the knobs sent a message nobody was listening to and
    // appeared, correctly, to do nothing.
    //
    // Now a part's knob drives whichever of its controls is mapped with
    // follows "level", taught through MIDI Learn like any other. CC 7 is still
    // sent when a part has no such control, because it costs nothing and is
    // right for anything that does respond.
    struct Message { int channel, cc, value; };
    std::vector<Message> out;

    {
        const juce::ScopedLock sl (stateLock);

        struct Part { int channel; float level; const gb::ControlSet* set; };
        const Part parts[4] = {
            { kit.channel,           levelDrums.load(),  &kit.controls },
            { bassProfile.channel,   levelBass.load(),   &bassProfile.controls },
            { guitarProfile.channel, levelGuitar.load(), haveGuitar ? &guitarProfile.controls : nullptr },
            { pianoProfile.channel,  levelPiano.load(),  havePiano  ? &pianoProfile.controls  : nullptr },
        };

        for (const Part& p : parts)
        {
            bool taught = false;

            if (p.set != nullptr)
            {
                for (const auto& def : p.set->all())
                {
                    if (def.follows != "level" || def.cc < 0)
                        continue;

                    // The knob's position runs through the control's own range,
                    // so a level control can be limited or inverted like any
                    // other - some instruments run their gain backwards.
                    const int value = juce::jlimit (0, 127,
                                          juce::roundToInt (def.valueAt (p.level) * 127.0));
                    out.push_back ({ p.channel, def.cc, value });
                    taught = true;
                }
            }

            if (! taught)
                out.push_back ({ p.channel, 7,
                                 juce::jlimit (0, 127, juce::roundToInt (p.level * 127.0f)) });
        }
    }

    {
        static const char* names[] = { "drums", "bass", "guitar", "piano" };
        juce::String report;
        for (size_t i = 0; i < out.size() && i < 8; ++i)
        {
            const Message& m = out[i];
            report += (report.isEmpty() ? "" : "   ")
                    + juce::String (names[juce::jlimit (0, 3, static_cast<int> (i))])
                    + " CC" + juce::String (m.cc)
                    + (m.cc == 7 ? "(untaught)" : "")
                    + " ch" + juce::String (m.channel)
                    + "=" + juce::String (m.value);
        }

        const juce::ScopedLock sl (stateLock);
        lastMidiReport = "Levels sent -  " + report;
    }

    const juce::SpinLock::ScopedLockType lock (auditionLock);
    for (const Message& m : out)
        pendingAuditions.push_back ({ 0, juce::MidiMessage::controllerEvent (m.channel, m.cc,
                                                                            m.value) });
}

juce::String GhostbandProcessor::getLastMidiReport() const
{
    const juce::ScopedLock sl (stateLock);
    return lastMidiReport;
}

// True when this part has a control taught to follow its mix knob, so the UI
// can say whether the knob reaches anything real.
bool GhostbandProcessor::levelIsTaught (int part) const
{
    const juce::ScopedLock sl (stateLock);
    const auto* p = controlSetFor (part);
    return p != nullptr && p->hasLevelControl();
}

void GhostbandProcessor::auditionStep (int index)
{
    const CalibrationStep s = getCalibrationStep (index);
    if (s.note < 0) return;

    // A drum is a one-shot, so a short note is plenty. A pitched note needs to
    // ring long enough to judge its pitch.
    const double holdSeconds = s.isDrum ? 0.12 : 0.9;
    const int holdSamples = static_cast<int> (holdSeconds * juce::jmax (8000.0, getSampleRate()));

    const juce::SpinLock::ScopedLockType lock (auditionLock);
    pendingAuditions.push_back ({ 0, juce::MidiMessage::noteOn (s.channel, s.note, (juce::uint8) 100) });
    pendingAuditions.push_back ({ holdSamples, juce::MidiMessage::noteOff (s.channel, s.note) });
}

void GhostbandProcessor::nudgeCalibrationNote (int index, int delta)
{
    {
        const juce::ScopedLock sl (stateLock);
        if (index < 0 || index >= static_cast<int> (calibrationSteps.size()))
            return;
        auto& s = calibrationSteps[static_cast<size_t> (index)];
        s.note = juce::jlimit (0, 127, s.note + delta);
        calibrationEdited = true;
    }
    auditionStep (index);   // hearing the result immediately is the whole point
    stateChanged.sendChangeMessage();
}

bool GhostbandProcessor::saveCalibration (juce::String& error)
{
    juce::String drumPath;
    std::vector<CalibrationStep> steps;

    {
        const juce::ScopedLock sl (stateLock);
        steps = calibrationSteps;

        const juce::File base = planFile.existsAsFile() ? planFile.getParentDirectory() : juce::File();
        const juce::String rel (plan.drumProfile);
        if (base != juce::File())
        {
            const juce::File a = base.getChildFile (rel);
            const juce::File b = base.getParentDirectory().getChildFile (rel);
            if (a.existsAsFile()) drumPath = a.getFullPathName();
            else if (b.existsAsFile()) drumPath = b.getFullPathName();
        }
        if (drumPath.isEmpty() && juce::File (rel).existsAsFile())
            drumPath = rel;
    }

    if (drumPath.isEmpty())
    {
        error = "No drum profile file to write to. Load a plan that names one.";
        return false;
    }

    // Written as a complete standalone profile rather than patched in place:
    // the file may inherit from a base, and silently rewriting an inherited file
    // would change every other kit that shares it.
    juce::String json;
    json << "{\n"
         << "  // Calibrated in Ghostband on this machine, by ear.\n"
         << "  // Any note here was confirmed against the real plugin.\n\n"
         << "  \"name\": \"" << juce::String (kit.name).replace ("\"", "'")
                                 .replace (" (calibrated)", "") << " (calibrated)\",\n"
         << "  \"id\": \"" << juce::String (kit.id) << "_calibrated\",\n"
         << "  \"channel\": " << kit.channel << ",\n\n"
         << "  \"needs_verification\": false,\n\n"
         << "  \"velocity_min\": " << kit.velocityMin << ",\n"
         << "  \"velocity_max\": " << kit.velocityMax << ",\n\n"
         << "  \"notes\": {\n";

    juce::StringArray entries;
    for (const CalibrationStep& s : steps)
    {
        if (! s.isDrum) continue;
        entries.add ("    \"" + s.label.replace (" ", "_") + "\": " + juce::String (s.note));
    }
    json << entries.joinIntoString (",\n") << "\n  }\n}\n";

    const juce::File target (drumPath);
    const juce::File backup = target.getSiblingFile (target.getFileNameWithoutExtension()
                                                     + "-before-calibration.json");
    if (target.existsAsFile() && ! backup.existsAsFile())
        target.copyFileTo (backup);   // never destroy the shipped map

    if (! target.replaceWithText (json))
    {
        error = "Could not write " + target.getFullPathName();
        return false;
    }

    // The drum map was only ever half the job. Every other part could be
    // nudged and auditioned in the calibrate list and then quietly discarded,
    // so a bass range corrected by ear never reached the file - which is how
    // MODO ended up being sent notes below the lowest one it can sound.
    juce::StringArray alsoSaved, failed;
    {
        const juce::ScopedLock sl (stateLock);

        const auto spliceInto = [&] (const std::string& path, const juce::String& what,
                                     const std::string& key, const std::string& block)
        {
            if (path.empty()) return;
            std::string e;
            if (gb::spliceProfileBlock (path, key, block, e)) alsoSaved.add (what);
            else                                             failed.add (what + ": " + e);
        };

        // Bass: the lowest note for the tuning this song is in. The others are
        // written back unchanged so the block stays complete.
        for (const CalibrationStep& st : steps)
        {
            if (st.label != "bass lowest note") continue;

            bassProfile.setLowestNoteFor (plan.bassTuning, st.note);

            std::string block = "\"lowest_note\": {\n";
            bool first = true;
            for (const auto& kv : bassProfile.allLowestNotes())
            {
                block += (first ? "" : ",\n") + std::string ("    \"") + kv.first
                       + "\": " + std::to_string (kv.second);
                first = false;
            }
            block += "\n  }";

            spliceInto (bassProfile.sourcePath, "bass range", "lowest_note", block);
        }

        // Guitar and piano: the range their chords are voiced into.
        struct Zone { const char* low; const char* high; gb::PhraseProfile* p;
                      const char* what; bool present; };
        const Zone zones[2] = {
            { "guitar lowest chord note", "guitar highest chord note", &guitarProfile,
              "guitar range", haveGuitar },
            { "piano lowest chord note",  "piano highest chord note",  &pianoProfile,
              "piano range",  havePiano },
        };

        for (const Zone& z : zones)
        {
            if (! z.present) continue;

            for (const CalibrationStep& st : steps)
            {
                if (st.label == z.low)  z.p->chordLowest  = st.note;
                if (st.label == z.high) z.p->chordHighest = st.note;
            }

            if (z.p->chordHighest < z.p->chordLowest)
                std::swap (z.p->chordLowest, z.p->chordHighest);

            spliceInto (z.p->sourcePath, z.what, "chord_zone",
                        "\"chord_zone\": { \"lowest_note\": "
                          + std::to_string (z.p->chordLowest)
                          + ", \"highest_note\": " + std::to_string (z.p->chordHighest) + " }");
        }

        calibrationEdited = false;
    }

    if (! failed.isEmpty())
        error = "Saved the kit, but " + failed.joinIntoString ("; ");
    else if (! alsoSaved.isEmpty())
        error = "Saved the kit and the " + alsoSaved.joinIntoString (", ") + ".";

    reloadPlan();
    return true;
}

//==============================================================================
// Song structure editing

int GhostbandProcessor::getSectionCount() const
{
    const juce::ScopedLock sl (stateLock);
    return static_cast<int> (plan.sections.size());
}

GhostbandProcessor::SectionEdit GhostbandProcessor::getSectionEdit (int index) const
{
    const juce::ScopedLock sl (stateLock);
    SectionEdit e;
    if (index < 0 || index >= static_cast<int> (plan.sections.size()))
        return e;

    const gb::SectionPlan& s = plan.sections[static_cast<size_t> (index)];
    e.name      = s.name;
    e.bars      = s.bars;
    e.intensity = s.intensity;
    e.feel      = s.feel;
    e.fill      = s.fill;
    e.lead      = s.lead;
    e.drums     = s.playsDrums;
    e.bass      = s.playsBass;
    e.guitar    = s.playsGuitar;
    e.piano     = s.playsPiano;

    juce::StringArray chords;
    for (const std::string& c : s.chords) chords.add (c);
    e.chords = chords.joinIntoString (" ");

    return e;
}

void GhostbandProcessor::applySectionEdit (int index, const SectionEdit& edit)
{
    {
        const juce::ScopedLock sl (stateLock);
        if (index < 0 || index >= static_cast<int> (plan.sections.size()))
            return;

        gb::SectionPlan& s = plan.sections[static_cast<size_t> (index)];
        s.name      = edit.name.trim().toStdString();
        s.bars      = juce::jlimit (1, 512, edit.bars);
        s.intensity = juce::jlimit (0.0, 1.0, edit.intensity);
        s.feel      = edit.feel.toStdString();
        s.fill      = edit.fill.toStdString();
        s.lead      = edit.lead.toStdString();

        s.playsDrums  = edit.drums;
        s.playsBass   = edit.bass;
        s.playsGuitar = edit.guitar;
        s.playsPiano  = edit.piano;

        // The role drives which progression families and phrases get drawn, and
        // it is inferred from the name - so renaming a section to "chorus"
        // genuinely makes it behave like one.
        s.role = gb::inferRole (s.name);

        s.chords.clear();
        for (const juce::String& c : juce::StringArray::fromTokens (edit.chords, " ,", ""))
            if (c.trim().isNotEmpty())
                s.chords.push_back (c.trim().toStdString());
    }

    planDirty = true;
    regenerate();
}

void GhostbandProcessor::addSection (int afterIndex)
{
    {
        const juce::ScopedLock sl (stateLock);

        gb::SectionPlan s;
        // Copy the neighbour rather than starting from defaults: a new section
        // is nearly always a variation on the one before it.
        if (afterIndex >= 0 && afterIndex < static_cast<int> (plan.sections.size()))
            s = plan.sections[static_cast<size_t> (afterIndex)];

        s.name   = s.name.empty() ? "section" : s.name + " copy";
        s.role   = gb::inferRole (s.name);
        s.reroll = 0;

        const size_t at = static_cast<size_t> (juce::jlimit (0, static_cast<int> (plan.sections.size()),
                                                             afterIndex + 1));
        plan.sections.insert (plan.sections.begin() + static_cast<std::ptrdiff_t> (at), s);
    }

    planDirty = true;
    regenerate();
}

void GhostbandProcessor::deleteSection (int index)
{
    {
        const juce::ScopedLock sl (stateLock);
        // A song with no sections cannot render, so the last one stays.
        if (plan.sections.size() <= 1) return;
        if (index < 0 || index >= static_cast<int> (plan.sections.size())) return;
        plan.sections.erase (plan.sections.begin() + static_cast<std::ptrdiff_t> (index));
    }

    planDirty = true;
    regenerate();
}

void GhostbandProcessor::moveSection (int index, int delta)
{
    {
        const juce::ScopedLock sl (stateLock);
        const int count = static_cast<int> (plan.sections.size());
        const int to = index + delta;
        if (index < 0 || index >= count || to < 0 || to >= count) return;
        std::swap (plan.sections[static_cast<size_t> (index)],
                   plan.sections[static_cast<size_t> (to)]);
    }

    planDirty = true;
    regenerate();
}

juce::String GhostbandProcessor::planAsText() const
{
    const juce::ScopedLock sl (stateLock);
    return juce::String (plan.toJson());
}

bool GhostbandProcessor::savePlan (const juce::File& target, juce::String& error)
{
    if (target == juce::File())
    {
        error = "No file to save to.";
        return false;
    }

    const juce::String text = planAsText();

    // Never overwrite a file without leaving the previous version behind.
    if (target.existsAsFile())
    {
        const juce::File backup = target.getSiblingFile (target.getFileNameWithoutExtension()
                                                         + "-previous.json");
        target.copyFileTo (backup);
    }

    if (! target.getParentDirectory().exists())
        target.getParentDirectory().createDirectory();

    if (! target.replaceWithText (text))
    {
        error = "Could not write " + target.getFullPathName();
        return false;
    }

    {
        const juce::ScopedLock sl (stateLock);
        planFile = target;
    }

    planDirty = false;
    stateChanged.sendChangeMessage();
    return true;
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

juce::String GhostbandProcessor::getMode() const
{
    const juce::ScopedLock sl (stateLock);
    return juce::String (plan.mode);
}

void GhostbandProcessor::setMode (const juce::String& mode)
{
    {
        const juce::ScopedLock sl (stateLock);
        plan.mode = mode.toStdString();
    }
    planDirty = true;
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

void GhostbandProcessor::rerollSections (const std::vector<int>& indices)
{
    if (indices.empty())
        return;

    {
        const juce::ScopedLock sl (stateLock);
        for (int i : indices)
            if (i >= 0 && i < static_cast<int> (plan.sections.size()))
                ++plan.sections[static_cast<size_t> (i)].reroll;
    }

    regenerate();
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
    kitToUse.controls.render (result.performance.drumControls, kitToUse.channel, 0, drums);

    bassToUse.render (result.performance.bass, bass, planToUse.bassTuning);
    bassToUse.controls.render (result.performance.bassControls, bassToUse.channel,
                               bassToUse.keyswitchLeadTicks, bass);

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

    // Auditioned notes are emitted regardless of transport state, because
    // calibration has to work with the host stopped. Try-locked, so a missed
    // block just delays a note nobody is timing.
    {
        const juce::SpinLock::ScopedTryLockType auditions (auditionLock);
        if (auditions.isLocked() && ! pendingAuditions.empty())
        {
            for (size_t i = 0; i < pendingAuditions.size(); )
            {
                PendingMessage& p = pendingAuditions[i];
                if (p.samplesUntil < numSamples)
                {
                    midi.addEvent (p.message, juce::jmax (0, p.samplesUntil));
                    pendingAuditions.erase (pendingAuditions.begin()
                                            + static_cast<std::ptrdiff_t> (i));
                }
                else
                {
                    p.samplesUntil -= numSamples;
                    ++i;
                }
            }
        }
    }

    // While calibrating, the song stays silent - otherwise the part you are
    // trying to identify is buried under a full band.
    if (calibrating.load())
    {
        if (wasPlaying)
        {
            sendAllNotesOff (midi, 0);
            wasPlaying = false;
        }
        return;
    }

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
            planTick = 0.0;
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
        levelsPending.store (true);
    }

    // Restate the levels at the top of a run, so an instrument that was
    // reloaded or reset since the last change still gets them.
    if (levelsPending.exchange (false))
    {
        const juce::SpinLock::ScopedTryLockType levels (auditionLock);
        if (levels.isLocked())
        {
            struct P { int ch; float v; };
            const P parts[4] = { { 10, levelDrums.load() }, { 1, levelBass.load() },
                                 { 2,  levelGuitar.load() }, { 3, levelPiano.load() } };
            for (const P& p : parts)
                midi.addEvent (juce::MidiMessage::controllerEvent (
                                   p.ch, 7, juce::jlimit (0, 127, juce::roundToInt (p.v * 127.0f))), 0);
        }
    }

    if (sequence.empty())
        return;

    // Which clock the song runs on. The plan's tempo is the one it was written
    // at; the host's is whatever the rackspace happens to be set to, and having
    // to match those by hand for every song is the thing this avoids.
    const bool   ownClock = usePlanTempo.load();
    const double hostBpmNow = pos->getBpm().orFallback (120.0);

    double bpm = hostBpmNow;
    if (ownClock)
    {
        const juce::ScopedLock sl (stateLock);
        if (plan.bpm > 0.0)
            bpm = plan.bpm;
    }

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

    const double blockTicks = numSamples / samplesPerTick;

    // On its own clock there is no host position to read: the song simply
    // advances a block's worth of ticks at the plan's tempo each time round.
    const double hostTick = ownClock ? planTick : ppq * gb::kPPQ;
    if (ownClock)
        planTick += blockTicks;

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

    // Always the host's, because this is what the UI reports and reporting the
    // plan's tempo back to the user as "the host tempo" would say nothing.
    hostBpm.store (hostBpmNow);

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
    xml.setAttribute ("planTempo",  usePlanTempo.load());
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
    editorWidth.store  (juce::jlimit (560, 2200, xml->getIntAttribute ("editorW", 620)));
    editorHeight.store (juce::jlimit (690, 2000, xml->getIntAttribute ("editorH", 780)));
    usePlanTempo.store (xml->getBoolAttribute ("planTempo", true));

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
