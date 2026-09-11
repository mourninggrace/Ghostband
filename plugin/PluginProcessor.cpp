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

    // Before any profile is loaded, so the first plan already gets whatever has
    // been taught on this machine.
    loadLearnedControls();

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
        fills.store      (plan.fills);
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

// Where songs you write go, kept away from the ones that ship.
//
// Save as... used to open on whatever song was loaded, so starting from a
// preset put the save dialog inside the installed bundle - under Program Files,
// where writing needs elevation and where the next install has to be careful
// not to sweep the file away again. Songs of your own belong somewhere they are
// yours, and somewhere an installer never looks.
juce::File GhostbandProcessor::userSongsFolder()
{
    const juce::File dir =
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Ghostband")
            .getChildFile ("Songs");

    dir.createDirectory();
    return dir;
}

// Whether the loaded song is one that shipped inside the bundle. Saving over
// one of those is not a thing to attempt and fail at: it is a thing to turn
// into Save as..., pointed at the folder above.
bool GhostbandProcessor::planIsFactory() const
{
    // Deliberately NOT bundledPlansFolder(). That falls back to Documents when
    // there is no bundle - which is right for "where should Load plan open" and
    // catastrophic here, because your own songs live under Documents too. Asked
    // through the fallback, "is this a factory song" answers yes for every song
    // you have ever written, and Save silently turns into Save as... forever.
    const juce::File plans =
        juce::File::getSpecialLocation (juce::File::currentExecutableFile)
            .getParentDirectory()
            .getParentDirectory()
            .getChildFile ("Resources")
            .getChildFile ("plans");

    if (! plans.isDirectory())
        return false;

    const juce::File file = getPlanFile();
    return file.existsAsFile() && file.isAChildOf (plans);
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

    loadPhrase (plan.guitarProfile,  channelGuitar.load(),  guitarProfile,  haveGuitar);
    loadPhrase (plan.guitar2Profile, channelGuitar2.load(), guitar2Profile, haveGuitar2);
    loadPhrase (plan.pianoProfile,   channelPiano.load(),   pianoProfile,   havePiano);

    // The channel follows the INSTRUMENT, not the part it is filling.
    //
    // It used to be taken from the slot, so loading Shreddage as a song's only
    // guitar sent Shreddage's notes and Shreddage's articulation keyswitches to
    // whatever was on the guitar slot's channel - IRON 2 - which played the
    // handful that happened to fall inside its range and dropped the rest. The
    // rack has one constrainer per plugin; a plugin's channel does not change
    // because a song gave it a different job.
    //
    // The profile's own channel is the default, and a channel set in Settings
    // is remembered against the instrument, exactly as its taught controls are.
    const auto channelFor = [this] (const std::string& instrument, const std::string& id,
                                    int declared)
    {
        const std::string key = instrumentKeyFor (instrument, id);
        const auto found = learnedChannels.find (key);
        return juce::jlimit (1, 16, found != learnedChannels.end() ? found->second : declared);
    };

    kit.channel         = channelFor (kit.instrument,         kit.id,         kit.channel);
    bassProfile.channel = channelFor (bassProfile.instrument, bassProfile.id, bassProfile.channel);
    // Taught mappings override whatever the profile files carry.
    mergeLearnedControls (kit.controls,            kit.instrument,            kit.id);
    mergeLearnedControls (bassProfile.controls,    bassProfile.instrument,    bassProfile.id);
    if (haveGuitar)  mergeLearnedControls (guitarProfile.controls,  guitarProfile.instrument,  guitarProfile.id);
    if (haveGuitar2) mergeLearnedControls (guitar2Profile.controls, guitar2Profile.instrument, guitar2Profile.id);
    if (havePiano)   mergeLearnedControls (pianoProfile.controls,   pianoProfile.instrument,   pianoProfile.id);

    guitarProfile.channel  = channelFor (guitarProfile.instrument,  guitarProfile.id,  guitarProfile.channel);
    guitar2Profile.channel = channelFor (guitar2Profile.instrument, guitar2Profile.id, guitar2Profile.channel);
    pianoProfile.channel   = channelFor (pianoProfile.instrument,   pianoProfile.id,   pianoProfile.channel);

    // Two instruments on one channel is a doubling nobody asked for, and the
    // pair that collides is exactly the pair a twin-guitar song uses. Move the
    // second one rather than letting both play every note the other does.
    if (haveGuitar && haveGuitar2 && guitar2Profile.channel == guitarProfile.channel)
    {
        int wanted = 11;
        const auto taken = [&] (int c)
        {
            return c == guitarProfile.channel || c == kit.channel || c == bassProfile.channel
                || (havePiano && c == pianoProfile.channel);
        };
        while (wanted <= 16 && taken (wanted)) ++wanted;
        if (wanted <= 16)
            guitar2Profile.channel = wanted;
    }

    // The Settings boxes show what the instruments are actually on.
    channelDrums.store   (kit.channel);
    channelBass.store    (bassProfile.channel);
    if (haveGuitar)  channelGuitar.store  (guitarProfile.channel);
    if (haveGuitar2) channelGuitar2.store (guitar2Profile.channel);
    if (havePiano)   channelPiano.store   (pianoProfile.channel);

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

    // The song under the playhead is about to be replaced. Whatever the old one
    // was holding has to be released by the audio thread, and the new one
    // starts at its own beginning rather than wherever the last one had got to.
    flushPending.store (true);
    rewindPending.store (true);

    {
        const juce::ScopedLock sl (stateLock);
        plan     = loaded;
        planFile = file;

        // Adopt the plan's dials so the UI reflects the file that was just
        // loaded rather than whatever the sliders happened to be showing.
        complexity.store (plan.complexity);
        humanize.store   (plan.humanize);
        fills.store      (plan.fills);
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

            // The top of the bass, which nothing could reach until now.
            // BassProfile has carried a highest_note since it was written and
            // calibration never offered it, so the number has never been
            // checked against a real instrument by anyone.
            CalibrationStep hi;
            hi.label   = "bass highest note";
            hi.hint    = "the highest note the bass can play";
            hi.note    = bassProfile.highestNote;
            hi.channel = bassProfile.channel;
            hi.isDrum  = false;
            steps.push_back (hi);
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

        // The second guitar, which was simply missing from this list.
        //
        // Calibrate is how a profile stops being a guess, and Shreddage's is
        // the profile that most needs it: its range is written down as
        // "reasoned rather than measured - walk the Calibrate screen to confirm
        // it", and the screen did not offer it. The one instrument whose file
        // asks to be calibrated was the one instrument that could not be.
        addPhraseSteps (guitar2Profile, "guitar 2", haveGuitar2);

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

        // Setting a slot's channel sets the channel of the INSTRUMENT in that
        // slot, and remembers it against that instrument. Putting Shreddage in
        // the guitar slot and moving that slot to 11 should not mean IRON 2 is
        // on 11 the next time a song uses it there.
        const auto assign = [this] (int wanted, const std::string& instrument,
                                    const std::string& id, int& target)
        {
            target = juce::jlimit (1, 16, wanted);

            const std::string key = instrumentKeyFor (instrument, id);
            if (! key.empty())
                learnedChannels[key] = target;
        };

        assign (channelDrums.load(),   kit.instrument,            kit.id,            kit.channel);
        assign (channelBass.load(),    bassProfile.instrument,    bassProfile.id,    bassProfile.channel);
        if (haveGuitar)  assign (channelGuitar.load(),  guitarProfile.instrument,  guitarProfile.id,  guitarProfile.channel);
        if (haveGuitar2) assign (channelGuitar2.load(), guitar2Profile.instrument, guitar2Profile.id, guitar2Profile.channel);
        if (havePiano)   assign (channelPiano.load(),   pianoProfile.instrument,   pianoProfile.id,   pianoProfile.channel);

        saveLearnedControls();
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
            case 2:  f = { guitarProfile.channel,  chordIn (guitarProfile),  true }; break;
            case 4:  f = { guitar2Profile.channel, chordIn (guitar2Profile), true }; break;
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
        static const char* names[] = { "drums", "bass", "guitar", "piano", "guitar 2" };
        juce::String notes;
        for (int n : f.notes)
            notes += (notes.isEmpty() ? "" : " ") + juce::String (n);

        const juce::ScopedLock sl (stateLock);
        lastMidiReport = juce::String ("Test ") + names[juce::jlimit (0, 4, part)]
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
    // channelForPart, not a second copy of the same switch. The copy that used
    // to live here had the same hole - no case for the second guitar - so the
    // learn sweep went to the rhythm guitar. Two functions that must agree
    // about which instrument a part speaks to should not be two functions.
    const int channel = channelForPart (part);

    // Claim the wire for the length of the sweep.
    //
    // Change detection above already stops the levels repeating themselves, but
    // an edit that moves a level control's own CC would still legitimately send
    // one - and a MIDI Learn cannot tell a legitimate message from the sweep it
    // is waiting for. It takes the first thing it hears. So for as long as a
    // Teach is in flight, nothing else is allowed to speak.
    teachingUntil.store (juce::Time::getMillisecondCounter() + 2500);

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
        // Four is the second guitar, and it was missing from this switch, so it
        // fell through to the rhythm guitar's channel. Every Teach sweep and
        // every Test aimed at a GTR 2 control therefore went to IRON 2 on
        // channel 2 rather than to Shreddage on 11: the knob you were trying to
        // teach never saw the controller move, and a knob you were not looking
        // at did. That is the whole reason mappings kept landing on the wrong
        // instrument.
        case 4:  return guitar2Profile.channel;
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

//==============================================================================
// Taught control mappings, kept per instrument.
//
// One file, outside the project, shared by every song and every gig. A mapping
// says which of an instrument's knobs sits on which CC, which is a fact about
// the plugin in the rack rather than about the profile file naming it - and
// seven files describe one SSD5.

static juce::File& learnedControlsOverride()
{
    static juce::File f;
    return f;
}

void GhostbandProcessor::setLearnedControlsFileForTesting (const juce::File& f)
{
    learnedControlsOverride() = f;
}

juce::File GhostbandProcessor::learnedControlsFile()
{
    if (learnedControlsOverride() != juce::File())
        return learnedControlsOverride();

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Ghostband")
               .getChildFile ("learned-controls.json");
}

std::string GhostbandProcessor::instrumentKeyFor (const std::string& instrument,
                                                 const std::string& id)
{
    return instrument.empty() ? id : instrument;
}

void GhostbandProcessor::loadLearnedControls()
{
    learnedControls.clear();
    learnedChannels.clear();

    const juce::File f = learnedControlsFile();
    if (! f.existsAsFile())
        return;

    juce::var parsed = juce::JSON::parse (f.loadFileAsString());
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return;

    const auto readControls = [this] (const juce::DynamicObject& from)
    {
        for (const auto& entry : from.getProperties())
        {
            gb::ControlSet set;
            if (gb::parseControlsJson (entry.value.toString().toStdString(), set))
                learnedControls[entry.name.toString().toStdString()] = set;
        }
    };

    // The first version of this file was a flat map of instrument to controls.
    // Read it either way rather than throwing away what somebody has taught.
    if (obj->hasProperty ("controls") || obj->hasProperty ("channels"))
    {
        if (auto* c = obj->getProperty ("controls").getDynamicObject())
            readControls (*c);

        if (auto* ch = obj->getProperty ("channels").getDynamicObject())
            for (const auto& entry : ch->getProperties())
                learnedChannels[entry.name.toString().toStdString()]
                    = juce::jlimit (1, 16, static_cast<int> (entry.value));
    }
    else
    {
        readControls (*obj);
    }
}

void GhostbandProcessor::saveLearnedControls() const
{
    const juce::File f = learnedControlsFile();
    f.getParentDirectory().createDirectory();

    auto* controls = new juce::DynamicObject();
    for (const auto& entry : learnedControls)
        controls->setProperty (juce::Identifier (juce::String (entry.first)),
                               juce::String (entry.second.toJson()));

    auto* channels = new juce::DynamicObject();
    for (const auto& entry : learnedChannels)
        channels->setProperty (juce::Identifier (juce::String (entry.first)), entry.second);

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("controls", juce::var (controls));
    obj->setProperty ("channels", juce::var (channels));

    juce::var wrapper (obj);
    f.replaceWithText (juce::JSON::toString (wrapper, false));
}

//==============================================================================
// The take library.
//
// Same storage shape as the taught controls above - a JSON file beside them in
// %APPDATA%/Ghostband - and read from disk on every call rather than cached.
// Two Ghostbands in one rackspace is normal, and a cached list would mean each
// held a private copy and the last one to save silently erased the other's.

static juce::File& takesOverride()
{
    static juce::File f;
    return f;
}

void GhostbandProcessor::setTakesFileForTesting (const juce::File& f)
{
    takesOverride() = f;
}

juce::File GhostbandProcessor::takesFile()
{
    if (takesOverride() != juce::File())
        return takesOverride();

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Ghostband")
               .getChildFile ("takes.json");
}

std::vector<GhostbandProcessor::Take> GhostbandProcessor::readTakes() const
{
    std::vector<Take> out;

    const juce::File f = takesFile();
    if (! f.existsAsFile())
        return out;

    const juce::var parsed = juce::JSON::parse (f.loadFileAsString());
    const juce::Array<juce::var>* list = parsed.getArray();
    if (list == nullptr)
        return out;

    // A missing property reads back as a void var, which converts to zero. Read
    // through a default instead: a take written by an older build that had no
    // fills dial should recall at the dial's own default, not at silence.
    const auto number = [] (const juce::DynamicObject& o, const char* key, double fallback)
    {
        const juce::var v = o.getProperty (juce::Identifier (key));
        return v.isVoid() ? fallback : static_cast<double> (v);
    };

    for (const juce::var& v : *list)
    {
        auto* o = v.getDynamicObject();
        if (o == nullptr) continue;

        Take t;
        t.name     = o->getProperty ("name").toString();
        t.songName = o->getProperty ("song").toString();
        t.planPath = o->getProperty ("plan_path").toString();
        t.planJson = o->getProperty ("plan").toString();
        t.savedAt  = o->getProperty ("saved").toString();

        t.seed       = static_cast<int> (number (*o, "seed", 1.0));
        t.complexity = juce::jlimit (0.0, 1.0, number (*o, "complexity", 0.5));
        t.humanize   = juce::jlimit (0.0, 1.0, number (*o, "humanize",   0.5));
        t.fills      = juce::jlimit (0.0, 1.0, number (*o, "fills",      0.62));

        // A take with no name cannot be picked out of a list, and one with no
        // song cannot be played. Neither is worth carrying forward.
        if (t.name.isNotEmpty() && t.planJson.isNotEmpty())
            out.push_back (t);
    }

    return out;
}

bool GhostbandProcessor::writeTakes (const std::vector<Take>& takes) const
{
    const juce::File f = takesFile();
    f.getParentDirectory().createDirectory();

    juce::Array<juce::var> list;
    for (const Take& t : takes)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name",       t.name);
        o->setProperty ("song",       t.songName);
        o->setProperty ("plan_path",  t.planPath);
        o->setProperty ("saved",      t.savedAt);
        o->setProperty ("seed",       t.seed);
        o->setProperty ("complexity", t.complexity);
        o->setProperty ("humanize",   t.humanize);
        o->setProperty ("fills",      t.fills);

        // Last, and last for a reason: it is by far the longest value, and a
        // file anyone might open by hand reads better with the short fields at
        // the top of each entry. JSON::toString does the escaping, which is the
        // whole reason the plan is stored as a string rather than spliced in.
        o->setProperty ("plan", t.planJson);

        list.add (juce::var (o));
    }

    return f.replaceWithText (juce::JSON::toString (juce::var (list), true));
}

std::vector<GhostbandProcessor::Take> GhostbandProcessor::getTakes() const
{
    return readTakes();
}

bool GhostbandProcessor::saveTake (const juce::String& name, juce::String& error)
{
    const juce::String trimmed = name.trim();
    if (trimmed.isEmpty())
    {
        error = "Name the take first.";
        return false;
    }

    Take t;
    t.name    = trimmed;
    t.savedAt = juce::Time::getCurrentTime().formatted ("%Y-%m-%d");

    t.seed       = seed.load();
    t.complexity = complexity.load();
    t.humanize   = humanize.load();
    t.fills      = fills.load();

    {
        const juce::ScopedLock sl (stateLock);

        // The dials are written into the stored song as well as beside it. They
        // are read back from the fields beside it, so this changes nothing on
        // recall - but it means the stored text is the song AS PLAYED, and a
        // take lifted out of this file by hand is a working plan rather than
        // one carrying the dial positions of whatever it was saved from.
        gb::SongPlan asPlayed = plan;
        asPlayed.complexity = t.complexity;
        asPlayed.humanize   = t.humanize;
        asPlayed.fills      = t.fills;
        asPlayed.seed       = static_cast<unsigned> (t.seed);

        t.songName = juce::String (asPlayed.title);
        t.planJson = juce::String (asPlayed.toJson());
        t.planPath = planFile.existsAsFile() ? planFile.getFullPathName() : juce::String();
    }

    std::vector<Take> takes = readTakes();

    // Saving over a name already in the list replaces it. Pressing Save twice
    // with one name means one take everywhere else, and a library that answers
    // it with two rows called the same thing is a library you stop trusting.
    const auto same = std::find_if (takes.begin(), takes.end(),
                                    [&trimmed] (const Take& e)
                                    { return e.name.equalsIgnoreCase (trimmed); });

    if (same != takes.end())
        *same = t;
    else
        takes.push_back (t);

    if (! writeTakes (takes))
    {
        error = "Could not write " + takesFile().getFullPathName();
        return false;
    }

    return true;
}

void GhostbandProcessor::recallTake (int index)
{
    const std::vector<Take> takes = readTakes();
    if (index < 0 || index >= static_cast<int> (takes.size()))
        return;

    const Take& t = takes[static_cast<size_t> (index)];

    gb::SongPlan loaded;
    std::string error;

    if (! gb::SongPlan::parse (t.planJson.toStdString(),
                               ("take \"" + t.name + "\"").toStdString(),
                               loaded, error))
    {
        const juce::ScopedLock sl (stateLock);
        status.ok      = false;
        status.message = juce::String (error);
        stateChanged.sendChangeMessage();
        return;
    }

    // Same handover as loadPlan: the audio thread has to let go of the song
    // under the playhead, and the new one starts at its own beginning.
    flushPending.store (true);
    rewindPending.store (true);

    {
        const juce::ScopedLock sl (stateLock);
        plan = loaded;

        // The song still came from a file, and saying so is what keeps Reload
        // meaning "back to what is on disk" and keeps the host session able to
        // find the song again. If that file has since moved, the plan is still
        // whole - it is stored in the take - and the label falls back to the
        // song's own title, which is true rather than merely tidy.
        const juce::File source (t.planPath);
        planFile = source.existsAsFile() ? source : juce::File();

        complexity.store (t.complexity);
        humanize.store   (t.humanize);
        fills.store      (t.fills);
        seed.store       (t.seed);

        juce::String profileError;
        resolveProfiles (profileError);
        status.message = profileError;
    }

    regenerate();
}

void GhostbandProcessor::deleteTake (int index)
{
    std::vector<Take> takes = readTakes();
    if (index < 0 || index >= static_cast<int> (takes.size()))
        return;

    takes.erase (takes.begin() + index);
    writeTakes (takes);
}

// A part has one mix knob, so it reaches one control.
//
// "level" is the only follow that is answered by a fader rather than by the
// arrangement, and the fader is single. When several controls claimed it the
// knob wrote to all of them at once - on Shreddage that was volume, bite and
// the pickup selector moving together, which reads as the plugin being broken
// rather than as a mapping being wrong.
//
// The FIRST one wins and the rest are released to "none", which is the honest
// outcome: they were not being followed by anything, they were being dragged
// along. Kept as a free function so the two places that can create the state -
// loading from the store, and editing in Settings - cannot disagree about it.
static void enforceSingleLevelControl (gb::ControlSet& controls)
{
    bool seen = false;
    for (gb::ControlDef& c : controls.editable())
    {
        if (c.follows != "level")
            continue;

        if (seen) c.follows = "none";
        else      seen = true;
    }
}

void GhostbandProcessor::mergeLearnedControls (gb::ControlSet& controls,
                                               const std::string& instrument,
                                               const std::string& id)
{
    // A profile that does not name its instrument is its own instrument, which
    // is how every profile written before this behaves unchanged.
    const std::string key = instrument.empty() ? id : instrument;
    if (key.empty())
        return;

    const auto found = learnedControls.find (key);
    if (found != learnedControls.end())
    {
        // The store carries CC NUMBERS, and nothing else.
        //
        // It used to replace the whole control set - "what was taught wins over
        // what shipped" - which sounds right and is not. MIDI Learn teaches one
        // thing: which of the plugin's knobs sits on which controller. That is a
        // fact about the rack, and it is the reason this store exists at all,
        // because seven profile files describe one SSD5 and teaching it once
        // should be enough.
        //
        // "follows", "type", "positions", "low" and "high" are not taught by
        // anything. They are declared in the profile, next to the comment saying
        // why, under version control. Letting a cached copy of them win meant an
        // edit to a profile was silently reverted on load and then written back
        // over the file on the next save - which is exactly what happened to
        // Shreddage: the file said the tone knobs follow "lead", the store said
        // all three follow "level", the store won, and one mix knob moved three
        // knobs at once.
        //
        // So: the profile keeps its own intent, the store supplies the CC, and a
        // control only the store knows about comes across whole - there is no
        // profile opinion to prefer in that case, and that is what carries a
        // mapping taught against one SSD5 file to the other six.
        auto& mine = controls.editable();

        for (gb::ControlDef& c : mine)
        {
            const int taught = found->second.ccFor (c.name);
            if (taught >= 0)
                c.cc = taught;
        }

        for (const gb::ControlDef& stored : found->second.all())
        {
            const bool known = std::any_of (mine.begin(), mine.end(),
                                            [&stored] (const gb::ControlDef& c)
                                            { return c.name == stored.name; });
            if (! known)
                mine.push_back (stored);
        }

        enforceSingleLevelControl (controls);
        return;
    }

    // Nothing taught for this instrument yet, so the profile's own block seeds
    // the store. This is what carries mappings taught before the store existed
    // across without anyone redoing them.
    if (controls.any())
    {
        learnedControls[key] = controls;
        saveLearnedControls();
    }
}

gb::PhraseProfile* GhostbandProcessor::phraseProfileFor (int part)
{
    // 4 rather than 3, so every part index already saved in a session or used
    // as a key for a control mapping still means what it did.
    if (part == 4) return haveGuitar2 ? &guitar2Profile : nullptr;
    if (part == 3) return havePiano   ? &pianoProfile   : nullptr;
    if (part == 2) return haveGuitar  ? &guitarProfile  : nullptr;
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
        case 4:  return haveGuitar2;
        default: return true;      // drums and bass are always present
    }
}

bool GhostbandProcessor::partVolumeReachable (int part) const
{
    const juce::ScopedLock sl (stateLock);
    switch (part)
    {
        case 0:  return kit.volumeReachable;
        case 1:  return bassProfile.volumeReachable;
        case 2:  return ! haveGuitar  || guitarProfile.volumeReachable;
        case 3:  return ! havePiano   || pianoProfile.volumeReachable;
        case 4:  return ! haveGuitar2 || guitar2Profile.volumeReachable;
        default: return true;
    }
}

gb::ControlSet* GhostbandProcessor::controlSetFor (int part)
{
    switch (part)
    {
        case 0:  return &kit.controls;
        case 1:  return &bassProfile.controls;
        case 2:  return haveGuitar  ? &guitarProfile.controls  : nullptr;
        case 4:  return haveGuitar2 ? &guitar2Profile.controls : nullptr;
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
        case 2:  return haveGuitar  ? guitarProfile.sourcePath  : std::string();
        case 4:  return haveGuitar2 ? guitar2Profile.sourcePath : std::string();
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

        // Naming a control is when Ghostband can help, and the only time it
        // should.
        //
        // Choosing between intensity, lead, level, random, random once, fixed
        // and none asks the owner of a rig to already know how this engine
        // thinks. The first person to map eleven controls got most of them
        // wrong - not carelessly, there was nothing to go on - and one of them,
        // a pitch bend range set to change per song, would have detuned every
        // bend in the solos.
        //
        // So: guess from the name, ONCE, and only while the control still has
        // the default everything. The moment anything has been chosen by hand
        // that choice stands, including through later renames.
        const std::string typed = slot.name.trim().isEmpty()
                                    ? std::string ("control")
                                    : slot.name.trim().toStdString();

        const bool untouched = (c.follows == "intensity" && c.type == "knob"
                                && c.low == 0.0 && c.high == 1.0);
        const bool renamed   = (typed != c.name);

        c.name    = typed;
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

        // The suggestion, applied after the fields above so it can see whether
        // the edit itself changed anything. An edit that sets follows by hand
        // is not overruled: untouched was measured before the assignment.
        if (renamed && untouched && c.follows == "intensity")
        {
            const gb::ControlSuggestion sug = gb::suggestControl (c.name);
            c.follows   = sug.follows;
            c.type      = sug.type;
            c.low       = sug.low;
            c.high      = sug.high;
            if (sug.positions >= 2) c.positions = sug.positions;

            lastSuggestion = juce::String (c.name) + " follows \"" + sug.follows
                           + "\" - " + sug.because;
        }

        // Choosing "level" here takes it off whatever had it before, because
        // the part has one mix knob and it reaches one control. Done in this
        // direction - the just-edited control wins - since the alternative is
        // the list quietly refusing the choice the owner made a moment ago.
        if (c.follows == "level")
            for (int i = 0; i < static_cast<int> (list.size()); ++i)
                if (i != index && list[static_cast<size_t> (i)].follows == "level")
                    list[static_cast<size_t> (i)].follows = "none";
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

        // Written to the store first, because that is the copy that matters -
        // it is what every song and every other profile for this instrument
        // will read. The profile file is written too, so a profile shared with
        // somebody else still carries a sensible starting point.
        const gb::ControlSet* set  = nullptr;
        const std::string*    inst = nullptr;
        const std::string*    id   = nullptr;

        switch (part)
        {
            case 0:  set = &kit.controls;            inst = &kit.instrument;            id = &kit.id;            break;
            case 1:  set = &bassProfile.controls;    inst = &bassProfile.instrument;    id = &bassProfile.id;    break;
            case 2:  set = &guitarProfile.controls;  inst = &guitarProfile.instrument;  id = &guitarProfile.id;  break;
            case 4:  set = &guitar2Profile.controls; inst = &guitar2Profile.instrument; id = &guitar2Profile.id; break;
            case 3:  set = &pianoProfile.controls;   inst = &pianoProfile.instrument;   id = &pianoProfile.id;   break;
            // Named rather than left to "default", which meant any part index
            // this function did not recognise wrote its mappings into the
            // PIANO. Callers all clamp to 0..4 today, so it never fired - but
            // the last two faults in this file were both a missing case
            // silently resolving to the wrong instrument, and a save is the
            // worst place to find the third.
            default: break;
        }

        if (set != nullptr)
        {
            const std::string key = inst->empty() ? *id : *inst;
            if (! key.empty())
            {
                learnedControls[key] = *set;
                saveLearnedControls();
            }
        }

        switch (part)
        {
            case 0:  ok = kit.save (path, e);            break;
            case 1:  ok = bassProfile.save (path, e);    break;
            case 2:  ok = guitarProfile.save (path, e);  break;
            case 4:  ok = guitar2Profile.save (path, e); break;
            case 3:  ok = pianoProfile.save (path, e);   break;
            default: e = "unknown part"; ok = false;     break;
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

// Called whenever the instruments behind the parts may have changed, so the
// knobs and the plugins agree before a note is played rather than after the
// first knob move.
void GhostbandProcessor::refreshLevels()
{
    sendLevels();
    levelsPending.store (true);
}

void GhostbandProcessor::sendLevels()
{
    // Where each mix knob has to reach.
    //
    // SILENT WHEN NOTHING HAS CHANGED, and that is not an optimisation.
    //
    // regenerate() ends by re-sending the levels, and every edit in the
    // Settings list regenerates. So merely selecting a control fired a
    // controller message at the instrument - and if that instrument had a knob
    // sitting in MIDI Learn, the knob latched onto THAT message instead of the
    // sweep the Teach button was about to send. Shreddage's Pitch Bend Range
    // learned CC 20, which is the second guitar's level, because CC 20 arrived
    // while it was listening.
    //
    // Ghostband was teaching the wrong control to the wrong knob, and doing it
    // while the owner watched and did everything right.
    //
    // This used to send CC 7 - channel volume - and nothing else, on an
    // assumption never tested against a real instrument. Most instrument
    // plugins do not implement CC 7 at all; they expose volume as their own
    // parameter. So the knobs sent a message nobody was listening to and
    // appeared, correctly, to do nothing.
    //
    // Now a part's knob drives whichever of its controls is mapped with
    // follows "level", taught through MIDI Learn like any other.
    //
    // CC 7 is the fallback when a part has no such control, and it was added on
    // the reasoning that it "costs nothing and is right for anything that does
    // respond". That is exactly half true. Kontakt DOES answer CC 7 as
    // instrument volume, so when Shreddage briefly had nothing following
    // "level" the GTR 2 knob became a hidden volume control on it: any position
    // below full turned the guitar down, the host session saved that position,
    // and the solo came back sounding like only the rhythm guitar was playing
    // it. A fallback is not free when it lands on something that listens.
    //
    // So it is now suppressed for a part whose profile says its volume cannot
    // be reached. That flag means "no message controls this", and sending one
    // anyway contradicts the flag - worse, the knob is HIDDEN in that case, so
    // CC 7 kept going out at a level nobody could see or correct. SSD5 sat in
    // exactly that state on channel 10.
    struct Message { int channel, cc, value; };
    std::vector<Message> out;

    {
        const juce::ScopedLock sl (stateLock);

        struct Part { int channel; float level; const gb::ControlSet* set; bool reachable; };
        const Part parts[5] = {
            { kit.channel,            levelDrums.load(),   &kit.controls,          kit.volumeReachable },
            { bassProfile.channel,    levelBass.load(),    &bassProfile.controls,  bassProfile.volumeReachable },
            { guitarProfile.channel,  levelGuitar.load(),  haveGuitar  ? &guitarProfile.controls  : nullptr, ! haveGuitar  || guitarProfile.volumeReachable },
            { guitar2Profile.channel, levelGuitar2.load(), haveGuitar2 ? &guitar2Profile.controls : nullptr, ! haveGuitar2 || guitar2Profile.volumeReachable },
            { pianoProfile.channel,   levelPiano.load(),   havePiano   ? &pianoProfile.controls   : nullptr, ! havePiano   || pianoProfile.volumeReachable },
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

                    // One knob, one control. Two places upstream now make a
                    // second "level" impossible to create, but a store written
                    // before them still exists on this machine and on anyone
                    // else's, and a fader that writes to three controllers is
                    // not a fault worth trusting a migration to catch.
                    break;
                }
            }

            if (! taught && p.reachable)
                out.push_back ({ p.channel, 7,
                                 juce::jlimit (0, 127, juce::roundToInt (p.level * 127.0f)) });
        }
    }

    {
        // In the order the parts array above is built, which is NOT the order
        // the part indices run in. This read "piano" against guitar 2's message
        // and "guitar 2" against the piano's, so the one line that reports where
        // a mix knob went was lying about two of the five - and that line is
        // what a mapping problem gets diagnosed from.
        static const char* names[] = { "drums", "bass", "guitar", "guitar 2", "piano" };
        juce::String report;
        for (size_t i = 0; i < out.size() && i < 8; ++i)
        {
            const Message& m = out[i];
            report += (report.isEmpty() ? "" : "   ")
                    + juce::String (names[juce::jlimit (0, 4, static_cast<int> (i))])
                    + " CC" + juce::String (m.cc)
                    + (m.cc == 7 ? "(untaught)" : "")
                    + " ch" + juce::String (m.channel)
                    + "=" + juce::String (m.value);
        }

        const juce::ScopedLock sl (stateLock);
        lastMidiReport = "Levels sent -  " + report;
    }

    // Not a word while a Teach is in flight. See teachControl.
    if (juce::Time::getMillisecondCounter() < static_cast<juce::uint32> (teachingUntil.load()))
        return;

    // Nothing to say unless something actually moved. A repeat of what the
    // instruments were already told is pure noise on the wire, and noise on
    // the wire is what a MIDI Learn latches onto.
    {
        const juce::ScopedLock sl (stateLock);

        std::vector<int> now;
        now.reserve (out.size() * 3);
        for (const Message& m : out)
        {
            now.push_back (m.channel);
            now.push_back (m.cc);
            now.push_back (m.value);
        }

        if (now == lastLevelsSent)
            return;

        lastLevelsSent = std::move (now);
    }

    const juce::SpinLock::ScopedLockType lock (auditionLock);
    levelMessages.clear();
    for (const Message& m : out)
    {
        const juce::MidiMessage msg = juce::MidiMessage::controllerEvent (m.channel, m.cc,
                                                                         m.value);
        levelMessages.push_back (msg);
        pendingAuditions.push_back ({ 0, msg });
    }
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
    // Only the notes block is replaced. Regenerating the whole file from the
    // struct threw away everything the file said that the struct does not hold:
    // the comments recording what was measured, and the controls block with any
    // mapping saved into it. Calibrating a kit destroyed its knob mappings, and
    // stacked another "_calibrated" onto the id every single time.
    // What actually moved, so a save can never quietly change something nobody
    // meant to touch. Entering calibration selects the first row, so a nudge
    // aimed at a row further down lands on the kick until the row is clicked -
    // which is exactly how a verified kick silently became note 56.
    juce::StringArray changed;

    juce::StringArray entries;
    for (const CalibrationStep& s : steps)
    {
        if (! s.isDrum) continue;

        bool ok = false;
        const gb::DrumVoice v = gb::drumVoiceFromName (s.label.replace (" ", "_").toStdString(), ok);
        if (ok && kit.noteFor (v) != s.note)
            changed.add (s.label + " " + juce::String (kit.noteFor (v))
                         + juce::String (juce::CharPointer_UTF8 ("â")) + juce::String (s.note));

        entries.add ("    \"" + s.label.replace (" ", "_") + "\": " + juce::String (s.note));
    }

    const std::string notesBlock = ("\"notes\": {\n" + entries.joinIntoString (",\n")
                                    + "\n  }").toStdString();

    const juce::File target (drumPath);
    const juce::File backup = target.getSiblingFile (target.getFileNameWithoutExtension()
                                                     + "-before-calibration.json");
    if (target.existsAsFile() && ! backup.existsAsFile())
        target.copyFileTo (backup);   // never destroy the shipped map

    {
        std::string e;
        if (! gb::spliceProfileBlock (target.getFullPathName().toStdString(),
                                      "notes", notesBlock, e))
        {
            error = juce::String (e);
            return false;
        }
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

        for (const CalibrationStep& st : steps)
        {
            if (st.label != "bass highest note") continue;

            bassProfile.highestNote = st.note;
            spliceInto (bassProfile.sourcePath, "bass range", "highest_note",
                        "\"highest_note\": " + std::to_string (bassProfile.highestNote));
        }

        // Guitar and piano: the range their chords are voiced into.
        struct Zone { const char* low; const char* high; gb::PhraseProfile* p;
                      const char* what; bool present; };
        // THREE, not two. The second guitar was added to the step list yesterday
        // and not to this, so its steps played, showed a note, took a nudge -
        // and were thrown away on save. Silently: the screen said "saved".
        //
        // That is the fourth instance of one shape in two days, and this one was
        // written by the same commit that fixed the third. Adding a part means
        // walking every list that names parts by hand, and the way to stop
        // paying for it is the harness check below, which now fails if a part
        // can be calibrated but not saved.
        const Zone zones[3] = {
            { "guitar lowest chord note",   "guitar highest chord note",   &guitarProfile,
              "guitar range",   haveGuitar },
            { "guitar 2 lowest chord note", "guitar 2 highest chord note", &guitar2Profile,
              "guitar 2 range", haveGuitar2 },
            { "piano lowest chord note",    "piano highest chord note",    &pianoProfile,
              "piano range",    havePiano },
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

            // Walking this screen IS the verification the flag is asking for,
            // so saving clears it. Without this an instrument stayed "unverified"
            // after being calibrated, and the warning at the bottom of the song
            // screen went on naming profiles that had just been checked by hand
            // - which is how a warning stops being read.
            if (z.p->needsVerification)
            {
                z.p->needsVerification = false;
                spliceInto (z.p->sourcePath, z.what, "needs_verification",
                            "\"needs_verification\": false");
            }
        }

        calibrationEdited = false;
    }

    if (! failed.isEmpty())
        error = "Saved, but " + failed.joinIntoString ("; ");
    else
    {
        juce::String what = changed.isEmpty() ? juce::String ("no drum notes changed")
                                              : changed.joinIntoString (", ");
        if (! alsoSaved.isEmpty())
            what += ".  Also saved the " + alsoSaved.joinIntoString (", ");
        error = "Saved: " + what + ".";
    }

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
    e.guitar2   = s.playsGuitar2;
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

        s.playsDrums   = edit.drums;
        s.playsBass    = edit.bass;
        s.playsGuitar  = edit.guitar;
        s.playsGuitar2 = edit.guitar2;
        s.playsPiano   = edit.piano;

        // Note what this line now does that it did not before. Until the editor
        // had a toggle for it, playsGuitar2 was deliberately NOT written here -
        // four toggles writing five flags would have cleared the fifth, and
        // renaming a section would have deleted its solo. It is written now
        // because there is finally a control that means it.

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

    // Never overwrite a file without leaving the previous version behind - but
    // not beside it. Written as a sibling, the backup turned up in Load plan as
    // a song in its own right, so the list offered "demo band" and "demo band
    // previous" and nothing explained which was which. It goes in a backups
    // folder now, where it is still one click away and is not a preset.
    if (target.existsAsFile())
    {
        const juce::File backups = target.getParentDirectory().getChildFile ("backups");
        backups.createDirectory();
        target.copyFileTo (backups.getChildFile (target.getFileNameWithoutExtension()
                                                 + "-previous.json"));
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

void GhostbandProcessor::setPlanBpm (double bpm)
{
    // Clamped rather than trusted. A zero would divide by itself in the tick
    // maths and a wild value would render a song hours long.
    const double wanted = juce::jlimit (20.0, 300.0, bpm);

    {
        const juce::ScopedLock sl (stateLock);
        if (std::abs (plan.bpm - wanted) < 0.001)
            return;
        plan.bpm = wanted;
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
    gb::PhraseProfile workingGuitar, workingGuitar2, workingPiano;
    bool withGuitar = false, withGuitar2 = false, withPiano = false;

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
        workingGuitar  = guitarProfile;
        workingGuitar2 = guitar2Profile;
        workingPiano   = pianoProfile;
        withGuitar     = haveGuitar;
        withGuitar2    = haveGuitar2;
        withPiano      = havePiano;
    }

    const gb::PhraseProfile* guitarPtr  = withGuitar  ? &workingGuitar  : nullptr;
    const gb::PhraseProfile* guitar2Ptr = withGuitar2 ? &workingGuitar2 : nullptr;
    const gb::PhraseProfile* pianoPtr   = withPiano   ? &workingPiano   : nullptr;

    working.complexity = complexity.load();
    working.humanize   = humanize.load();
    working.fills      = fills.load();
    working.seed       = static_cast<unsigned> (std::max (1, seed.load()));

    // Generation is pure arithmetic and completes in well under a millisecond
    // for a song this size, so it runs on the message thread. When the AI
    // planner lands it will need a network call, and that will have to move to
    // a background thread - the sequence swap below is already built for it.
    const gb::RenderResult result = gb::renderPerformance (working, workingKit, workingBass,
                                                           guitarPtr, pianoPtr, guitar2Ptr);

    rebuildSequence (result, workingKit, workingBass, working, guitarPtr, pianoPtr, guitar2Ptr);

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
        status.guitarProfile  = withGuitar  ? juce::String (workingGuitar.name)  : juce::String();
        status.guitar2Profile = withGuitar2 ? juce::String (workingGuitar2.name) : juce::String();
        status.pianoProfile  = withPiano  ? juce::String (workingPiano.name)  : juce::String();

        // The second guitar was missing from this list, so a song could warn
        // about nothing or stay silent about a profile nobody had checked.
        // Fifth place in this file where a list of parts was written before
        // guitar 2 existed and never revisited.
        status.unverifiedProfiles = workingKit.needsVerification
                                 || workingBass.needsVerification
                                 || (withGuitar  && workingGuitar.needsVerification)
                                 || (withGuitar2 && workingGuitar2.needsVerification)
                                 || (withPiano   && workingPiano.needsVerification);

        status.headline = juce::String (working.key) + " " + juce::String (working.mode).replace ("_", " ")
                        + "   " + juce::String (working.bpm, 0) + " bpm   "
                        + juce::String (working.timeSigNumerator) + "/"
                        + juce::String (working.timeSigDenominator) + "   "
                        + juce::String (working.style).replace ("_", " ");

        status.message = status.unverifiedProfiles
                       ? "Profiles are unverified - calibrate before trusting the mapping."
                       : juce::String();
    }

    // The instruments behind the parts may have just changed, and the knobs are
    // wherever the user left them. Tell the instruments now rather than waiting
    // for somebody to move a knob before the mix is what the screen says it is.
    refreshLevels();

    stateChanged.sendChangeMessage();
}

void GhostbandProcessor::rebuildSequence (const gb::RenderResult& result,
                                          const gb::DrumProfile& kitToUse,
                                          const gb::BassProfile& bassToUse,
                                          const gb::SongPlan& planToUse,
                                          const gb::PhraseProfile* guitarToUse,
                                          const gb::PhraseProfile* pianoToUse,
                                          const gb::PhraseProfile* guitar2ToUse)
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

    // A part counts as present if it has chords OR a lead line. Testing chords
    // alone dropped any part that only ever solos - and a soloing part has no
    // chords by design, one player cannot comp and solo at once. A plan whose
    // guitar solos the whole way through played no guitar at all here, while
    // the CLI rendered it perfectly.
    const auto appendPart = [&append] (const gb::PhraseProfile* profile,
                                       const gb::PhrasePart& part)
    {
        if (profile == nullptr || (part.chords.empty() && part.lead.empty()))
            return;

        gb::MidiTrack t;
        profile->render (part, t);
        append (t);
    };

    appendPart (guitarToUse,  result.performance.guitar);
    appendPart (guitar2ToUse, result.performance.guitar2);
    appendPart (pianoToUse,   result.performance.piano);

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
        // Centre the wheel. A stop in the middle of a bend leaves the
        // instrument transposed, and it stays that way through every later
        // note until something resets it - which reads as the plugin having
        // put the guitar out of tune, not as a stop landing awkwardly.
        midi.addEvent (juce::MidiMessage::pitchWheel (ch, 8192), sampleOffset);

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

    // A song swapped in under a running playhead leaves the old one's notes
    // with nothing to stop them. Release them before the new song starts.
    if (flushPending.exchange (false))
        sendAllNotesOff (midi, 0);

    if (rewindPending.exchange (false))
    {
        planTick = 0.0;
        nextExpectedTick = -1.0;
        jumpOffset = 0.0;
        activeSection.store (-1);
    }

    // Paused is Ghostband's own stop, not the host's. The host transport is
    // often left running all session, so stopping the band and stopping the
    // host are not the same action.
    if (paused.load())
    {
        if (wasPlaying)
        {
            sendAllNotesOff (midi, 0);
            wasPlaying = false;
            transportRunning.store (false);
            activeSection.store (-1);
        }
        return;
    }

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
            // Whatever sendLevels last decided, which is the taught volume
            // control for a part that has one and CC 7 only as a fallback.
            // This used to send CC 7 on four hard-coded channels, so an
            // instrument reached through a taught control was never told the
            // knob position until somebody moved the knob - the mix was wrong
            // from the moment the song started until it was touched.
            for (const juce::MidiMessage& m : levelMessages)
                midi.addEvent (m, 0);
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

int GhostbandProcessor::getSequenceNoteOnCount (int channel, int minNote) const
{
    const juce::SpinLock::ScopedLockType lock (sequenceLock);
    int n = 0;
    for (const TimedMessage& m : sequence)
        if (m.message.isNoteOn() && m.message.getChannel() == channel
            && m.message.getNoteNumber() >= minNote)
            ++n;
    return n;
}

juce::String GhostbandProcessor::getSequenceControllers (int channel) const
{
    const juce::SpinLock::ScopedLockType lock (sequenceLock);
    juce::String out;
    for (const TimedMessage& m : sequence)
        if (m.message.isController() && m.message.getChannel() == channel)
            out << m.message.getControllerNumber() << "="
                << m.message.getControllerValue() << " ";
    return out.trim();
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
    xml.setAttribute ("fills",      fills.load());
    xml.setAttribute ("theme",      theme.load());
    xml.setAttribute ("seed",       seed.load());
    xml.setAttribute ("editorW",    editorWidth.load());
    xml.setAttribute ("editorH",    editorHeight.load());
    xml.setAttribute ("planTempo",  usePlanTempo.load());

    // The mix and the channel assignments are part of how a rig is set up, not
    // scratch values. Leaving them out meant every knob sprang back to full and
    // every channel back to its default the moment the host was restarted.
    xml.setAttribute ("levelDrums",  levelDrums.load());
    xml.setAttribute ("levelBass",   levelBass.load());
    xml.setAttribute ("levelGuitar",  levelGuitar.load());
    xml.setAttribute ("levelGuitar2", levelGuitar2.load());
    xml.setAttribute ("levelPiano",   levelPiano.load());

    xml.setAttribute ("chDrums",  channelDrums.load());
    xml.setAttribute ("chBass",   channelBass.load());
    xml.setAttribute ("chGuitar",  channelGuitar.load());
    xml.setAttribute ("chGuitar2", channelGuitar2.load());
    xml.setAttribute ("chPiano",   channelPiano.load());
    copyXmlToBinary (xml, destData);
}

void GhostbandProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName ("GhostbandState"))
        return;

    complexity.store (xml->getDoubleAttribute ("complexity", 0.5));
    humanize.store   (xml->getDoubleAttribute ("humanize", 0.5));
    fills.store      (juce::jlimit (0.0, 1.0,
                          xml->getDoubleAttribute ("fills", 0.62)));

    // applyTheme ignores an index it does not have, so a session saved by a
    // later build naming a theme this one lacks keeps the default rather than
    // landing on whichever happens to sit at that number.
    theme.store (xml->getIntAttribute ("theme", 0));
    ghost::applyTheme (theme.load());
    seed.store       (xml->getIntAttribute ("seed", 1));
    // The floor moved with the type: 560x690 was a size at which 11pt body text
    // just fitted, and nothing readable fits in it. The default matches the
    // header, so a session saved before this opens at the new size rather than
    // at a cramped old one.
    editorWidth.store  (juce::jlimit (700, 2400, xml->getIntAttribute ("editorW", 800)));
    editorHeight.store (juce::jlimit (820, 2200, xml->getIntAttribute ("editorH", 960)));
    usePlanTempo.store (xml->getBoolAttribute ("planTempo", true));

    const auto level = [&xml] (const char* key)
    {
        return static_cast<float> (juce::jlimit (0.0, 1.0,
                                       xml->getDoubleAttribute (key, 1.0)));
    };
    levelDrums.store  (level ("levelDrums"));
    levelBass.store   (level ("levelBass"));
    levelGuitar.store  (level ("levelGuitar"));
    levelGuitar2.store (level ("levelGuitar2"));
    levelPiano.store  (level ("levelPiano"));

    channelDrums.store  (juce::jlimit (1, 16, xml->getIntAttribute ("chDrums", 10)));
    channelBass.store   (juce::jlimit (1, 16, xml->getIntAttribute ("chBass", 1)));
    channelGuitar.store  (juce::jlimit (1, 16, xml->getIntAttribute ("chGuitar", 2)));
    channelGuitar2.store (juce::jlimit (1, 16, xml->getIntAttribute ("chGuitar2", 11)));
    channelPiano.store  (juce::jlimit (1, 16, xml->getIntAttribute ("chPiano", 3)));

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
