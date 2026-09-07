// Drives GhostbandProcessor without a host.
//
// "It compiles" and "it emits the right MIDI at the right time" are different
// claims. This makes the second one testable: it fakes a playhead, walks the
// whole song block by block, and checks what actually comes out of processBlock.
//
// The strongest assertion here is the last one - the plugin and the CLI must
// produce byte-identical note counts for the same plan, because they run the
// same engine through the same profiles. If those ever diverge, one of the two
// paths has grown a bug the other does not have.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "ghostband/Groove.h"
#include "ghostband/MidiFile.h"
#include "ghostband/Render.h"
#include "ghostband/SongPlan.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <iostream>
#include <map>
#include <set>

namespace {

class FakePlayHead : public juce::AudioPlayHead
{
public:
    double ppq = 0.0;
    double bpm = 120.0;
    bool   playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        return info;
    }
};

int failures = 0;

void check (bool condition, const juce::String& what, const juce::String& detail = {})
{
    std::cout << (condition ? "  PASS  " : "  FAIL  ") << what;
    if (detail.isNotEmpty()) std::cout << "   (" << detail << ")";
    std::cout << "\n";
    if (! condition) ++failures;
}

} // namespace

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String planPath = argc > 1 ? juce::String (argv[1])
                                           : juce::String ("C:/Projects/Ghostband/plans/demo-metal.json");

    std::cout << "\nGhostband plugin harness\n========================\n\n";
    std::cout << "plan: " << planPath << "\n\n";

    // Before any processor exists, because one reads the store on construction.
    // A test run must never rewrite the channels and taught controls of every
    // instrument on the machine, and for one run of this harness it did.
    const juce::File testStore = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("ghostband-harness")
                                     .getChildFile ("learned-controls.json");
    testStore.deleteFile();
    GhostbandProcessor::setLearnedControlsFileForTesting (testStore);

    GhostbandProcessor proc;

    check (proc.producesMidi(), "plugin declares MIDI output");
    check (! proc.isMidiEffect(), "plugin is a normal effect, not a MIDI-effect category");

    // A freshly constructed plugin must already have something to play. Adding
    // it to a rackspace and getting silence until you find a file on disk is a
    // dead end, and it was the plugin's actual first-run behaviour.
    {
        const auto fresh = proc.getStatus();
        check (fresh.ok, "plays out of the box with no plan loaded", fresh.message);
        check (proc.planIsBuiltIn(), "reports that it is on the built-in plan");
        check (! proc.getSections().empty(), "built-in plan has an arrangement",
               juce::String (static_cast<int> (proc.getSections().size())) + " sections");
        check (proc.getSequenceNoteOnCount (proc.channelDrums.load()) > 0 && proc.getSequenceNoteOnCount (1) > 0,
               "built-in plan produces both drums and bass");

        // The built-in plan used to name no profiles at all, so guitar and piano
        // could never sound on it - which is exactly how "guitar and piano are
        // definitely not playing" was reported. It is a full band now, and a
        // missing profile falls back to generic rather than dropping the part.
        check (proc.getSequenceNoteOnCount (2) > 0,
               "built-in plan plays guitar out of the box",
               juce::String (proc.getSequenceNoteOnCount (2)) + " note-ons on ch2");
        check (proc.getSequenceNoteOnCount (3) > 0,
               "built-in plan plays piano out of the box",
               juce::String (proc.getSequenceNoteOnCount (3)) + " note-ons on ch3");

        const auto sections = proc.getSections();
        bool ticksSane = ! sections.empty() && sections.front().startTick == 0;
        for (size_t i = 1; i < sections.size(); ++i)
            if (sections[i].startTick != sections[i - 1].endTick) ticksSane = false;
        check (ticksSane, "section tick ranges are contiguous (the UI playhead depends on it)");
    }

    proc.loadPlan (juce::File (planPath));

    const auto status = proc.getStatus();
    check (status.ok, "plan loaded and rendered", status.message);
    if (! status.ok)
    {
        std::cout << "\ncannot continue without a rendered plan\n";
        return 1;
    }

    std::cout << "\n  " << status.headline << "\n";
    std::cout << "  " << status.bars << " bars, " << juce::String (status.seconds, 1) << "s, "
              << status.drumHits << " drum hits, " << status.bassNotes << " bass notes\n";
    std::cout << "  drums: " << status.drumProfile << "\n  bass : " << status.bassProfile << "\n\n";

    std::cout << "  sequence holds " << proc.getSequenceNoteOnCount (proc.channelDrums.load()) << " note-ons on ch10, "
              << proc.getSequenceNoteOnCount (1) << " on ch1\n\n";

    // That a profile resolved, not which one. Naming the kit here meant swapping
    // kits looked like a broken path.
    check (status.drumProfile.isNotEmpty()
               && ! status.drumProfile.contains ("not found"),
           "drum profile resolved from a path relative to the project root",
           status.drumProfile);
    check (status.bassProfile.contains ("MODO"),
           "bass profile resolved", status.bassProfile);

    // Every part the plan names must actually reach the sequence.
    //
    // demo-metal named a drum profile and a bass profile and nothing else, so
    // it rendered as drums and bass on a preset called "metal" - the parts were
    // not silent, they were never generated, because guitar and piano are
    // opt-in on the profile being named. Nothing caught it: the parity numbers
    // are drums and bass only, so a plan could lose half the band and still
    // pass. This ties the check to what the plan asked for rather than to a
    // fixed count, so it holds for any plan passed on the command line.
    // Asked on the channel the instrument is actually on, not on a fixed one.
    // A channel follows the instrument now, so "the guitar" is on 2 when it is
    // IRON 2 and on 11 when it is Shreddage.
    if (status.guitarProfile.isNotEmpty())
    {
        const int ch = proc.channelGuitar.load();
        check (proc.getSequenceNoteOnCount (ch) > 0,
               "plan names a guitar profile and the guitar plays",
               juce::String (proc.getSequenceNoteOnCount (ch)) + " note-ons on ch"
                   + juce::String (ch));
    }

    if (status.guitar2Profile.isNotEmpty())
    {
        const int ch = proc.channelGuitar2.load();
        check (proc.getSequenceNoteOnCount (ch) > 0,
               "plan names a second guitar and it plays",
               juce::String (proc.getSequenceNoteOnCount (ch)) + " note-ons on ch"
                   + juce::String (ch));
    }

    // Two instruments on one channel is a doubling nobody asked for, and it is
    // how Shreddage's notes and its articulation keyswitches ended up being sent
    // to IRON 2 - which played the few that fell in its range and dropped the
    // rest, so the guitar simply went missing.
    if (status.guitarProfile.isNotEmpty() && status.guitar2Profile.isNotEmpty())
        check (proc.channelGuitar.load() != proc.channelGuitar2.load(),
               "and the two guitars are not on the same channel",
               "gtr " + juce::String (proc.channelGuitar.load())
                   + ", gtr2 " + juce::String (proc.channelGuitar2.load()));

    if (status.pianoProfile.isNotEmpty())
        check (proc.getSequenceNoteOnCount (proc.channelPiano.load()) > 0,
               "plan names a piano profile and the piano plays",
               juce::String (proc.getSequenceNoteOnCount (proc.channelPiano.load()))
                   + " note-ons on ch" + juce::String (proc.channelPiano.load()));

    // ---- walk the song ---------------------------------------------------
    const double sampleRate = 48000.0;
    const int    blockSize  = 512;

    // A host calls setRateAndBufferSizeDetails before prepareToPlay; calling
    // prepareToPlay alone leaves getSampleRate() at zero, which is exactly the
    // situation the processor now refuses to guess its way through.
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    FakePlayHead head;

    // The host tempo has to be the song's own, not a fixed number that happened
    // to match one demo plan. A real host playing this song is set to its tempo,
    // and Ghostband's own clock runs at it too, so hard-coding 168 here made
    // every check silently wrong for any song not written at 168 - it walked
    // demo-rock at nearly twice its speed and then counted the notes it missed.
    head.bpm = proc.getPlanBpm();
    proc.setPlayHead (&head);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    // Long enough to cover the whole arrangement plus the ending.
    const double totalQuarters = (status.bars + 4) * 4.0;
    const double quartersPerBlock = (blockSize / sampleRate) * (head.bpm / 60.0);
    const int    numBlocks = static_cast<int> (totalQuarters / quartersPerBlock) + 1;

    std::map<int, int> noteOnByChannel;
    std::map<int, int> noteOffByChannel;
    int    totalEvents = 0;
    int    ccEvents    = 0;
    double firstEventQuarter = -1.0;
    int    outOfRangeOffsets = 0;

    // Absolute tick, channel and note of every note-on, so a duplicate emission
    // can be told apart from a genuinely extra event.
    std::map<juce::String, int> noteOnKeys;
    const double ticksPerSample = 1.0 / ((60.0 / (head.bpm * gb::kPPQ)) * sampleRate);

    for (int b = 0; b < numBlocks; ++b)
    {
        head.ppq = b * quartersPerBlock;
        buffer.clear();
        midi.clear();

        proc.processBlock (buffer, midi);

        for (const juce::MidiMessageMetadata m : midi)
        {
            ++totalEvents;

            if (m.samplePosition < 0 || m.samplePosition >= blockSize)
                ++outOfRangeOffsets;

            const juce::MidiMessage msg = m.getMessage();
            if (msg.isNoteOn())
            {
                noteOnByChannel[msg.getChannel()]++;

                const int absTick = juce::roundToInt (head.ppq * gb::kPPQ
                                                      + m.samplePosition * ticksPerSample);
                noteOnKeys[juce::String (absTick) + ":" + juce::String (msg.getChannel())
                           + ":" + juce::String (msg.getNoteNumber())]++;

                if (firstEventQuarter < 0.0)
                    firstEventQuarter = head.ppq + (m.samplePosition / sampleRate) * (head.bpm / 60.0);
            }
            else if (msg.isNoteOff())
            {
                noteOffByChannel[msg.getChannel()]++;
            }
            else if (msg.isController())
            {
                ++ccEvents;
            }
        }
    }

    const auto diag = proc.diagnostics;
    std::cout << "  blocks " << diag.blocks
              << ", processBlock emitted " << diag.eventsEmitted
              << ", stitch misses " << diag.stitchMisses
              << ", worst gap " << juce::String (diag.worstGap, 6) << " ticks\n";
    std::cout << "  events emitted while playing: " << totalEvents << "\n";
    for (const auto& kv : noteOnByChannel)
        std::cout << "    channel " << kv.first << ": " << kv.second << " note-on, "
                  << noteOffByChannel[kv.first] << " note-off\n";
    std::cout << "    controllers: " << ccEvents << "\n\n";

    int duplicated = 0, distinct = 0;
    for (const auto& kv : noteOnKeys)
    {
        ++distinct;
        if (kv.second > 1) duplicated += kv.second - 1;
    }
    std::cout << "    distinct note-on positions: " << distinct
              << ", repeats at the same position: " << duplicated << "\n\n";

    check (duplicated == 0, "no note is emitted twice at the same position",
           juce::String (duplicated) + " repeats");

    check (totalEvents > 0, "MIDI is emitted while the transport is running");
    check (outOfRangeOffsets == 0, "every event lands inside its block",
           juce::String (outOfRangeOffsets) + " out of range");
    check (firstEventQuarter >= 0.0 && firstEventQuarter < 0.05,
           "the song starts on the downbeat",
           "first note-on at quarter " + juce::String (firstEventQuarter, 4));

    int totalOn = 0, totalOff = 0;
    for (const auto& kv : noteOnByChannel)  totalOn  += kv.second;
    for (const auto& kv : noteOffByChannel) totalOff += kv.second;
    check (totalOn == totalOff, "every note-on is matched by a note-off",
           juce::String (totalOn) + " on vs " + juce::String (totalOff) + " off");

    // On the kit's own channel, which is the kit's to decide - MINDst is on 12,
    // SSD5 on 10 - not a constant the test gets to assume.
    const int drumCh = proc.channelDrums.load();
    const int bassCh = proc.channelBass.load();

    check (noteOnByChannel.count (drumCh) > 0, "drums are emitted on their channel",
           "channel " + juce::String (drumCh));
    check (noteOnByChannel.count (bassCh) > 0, "bass is emitted on its channel",
           "channel " + juce::String (bassCh));

    // The drum profile emits exactly one note per intent, so these must agree.
    check (noteOnByChannel[drumCh] == status.drumHits,
           "drum note count matches what the engine reported",
           juce::String (noteOnByChannel[drumCh]) + " vs " + juce::String (status.drumHits));

    // Bass carries keyswitches on top of the played notes, so it must be at
    // least the reported count and not wildly more.
    check (noteOnByChannel[1] >= status.bassNotes,
           "bass note count covers every rendered note",
           juce::String (noteOnByChannel[1]) + " vs " + juce::String (status.bassNotes));

    // ---- stopping --------------------------------------------------------
    head.playing = false;
    buffer.clear();
    midi.clear();
    proc.processBlock (buffer, midi);

    bool sawAllNotesOff = false;
    for (const juce::MidiMessageMetadata m : midi)
        if (m.getMessage().isAllNotesOff())
            sawAllNotesOff = true;

    check (sawAllNotesOff, "stopping the transport sends all-notes-off");

    // A second stopped block must be silent, or the plugin would spam the host.
    midi.clear();
    proc.processBlock (buffer, midi);
    check (midi.isEmpty(), "no further output once stopped",
           juce::String (midi.getNumEvents()) + " events");

    // ---- determinism -----------------------------------------------------
    proc.regenerate();
    const auto again = proc.getStatus();
    check (again.drumHits == status.drumHits && again.bassNotes == status.bassNotes,
           "regenerating with the same seed gives the same song",
           juce::String (again.drumHits) + "/" + juce::String (again.bassNotes));

    proc.seed.store (proc.seed.load() + 1);
    proc.regenerate();
    const auto rolled = proc.getStatus();
    check (rolled.drumHits != status.drumHits || rolled.bassNotes != status.bassNotes,
           "a different seed gives a different song",
           juce::String (rolled.drumHits) + "/" + juce::String (rolled.bassNotes));

    // ---- key changes ----------------------------------------------------
    proc.loadPlan (juce::File (planPath));
    {
        const int before      = proc.getKeyPitchClass();
        // Played notes only. Keyswitches are note-ons on the same channel below
        // the instrument's range - MODO's sit at 13, 15, 22 and 24 - and a
        // hammer-on firing in one key and not another is the articulation
        // following the fingering, which is the bass playing correctly rather
        // than the performance changing. Counting them as notes conflated the
        // two and made a correct change look like a regression.
        const int kPlayed = 26;

        const int drumsBefore = proc.getSequenceNoteOnCount (proc.channelDrums.load());
        const int bassBefore  = proc.getSequenceNoteOnCount (1, kPlayed);
        const int pitchBefore = proc.getSequencePitchSum (1);

        const int target = (before + 5) % 12;
        proc.setKeyPitchClass (target);

        check (proc.getKeyPitchClass() == target, "changing the key takes effect",
               juce::String (before) + " -> " + juce::String (proc.getKeyPitchClass()));

        // Transposing must move the pitches without disturbing the performance:
        // same rhythm, same number of notes, different notes.
        check (proc.getSequenceNoteOnCount (proc.channelDrums.load()) == drumsBefore
                   && proc.getSequenceNoteOnCount (1, kPlayed) == bassBefore,
               "transposing does not change the drumming or the note count",
               juce::String (proc.getSequenceNoteOnCount (proc.channelDrums.load())) + "/"
                   + juce::String (proc.getSequenceNoteOnCount (1, kPlayed))
                   + " was " + juce::String (drumsBefore) + "/" + juce::String (bassBefore));
        check (proc.getSequencePitchSum (1) != pitchBefore,
               "transposing actually moves the bass pitches",
               juce::String (pitchBefore) + " -> " + juce::String (proc.getSequencePitchSum (1)));

        proc.setKeyPitchClass (before);
    }

    // ---- live section jumping -------------------------------------------
    {
        proc.loadPlan (juce::File (planPath));
        const auto secs = proc.getSections();
        const int barTicks = proc.getBarTicks();

        head.playing = true;
        head.ppq = 0.0;
        proc.setPlayHead (&head);

        // Counted per pitch, not in total. "Nothing hangs" means no pitch is
        // left sounding - a surplus note-off is a no-op on every instrument,
        // whereas a surplus note-on is a stuck note. Comparing bare totals
        // conflates the harmless direction with the dangerous one.
        std::map<int, int> openByPitch;
        int jumpedAtTick = -1;
        int blocksAfterQueue = 0;
        const int targetSection = 3;
        bool queuedYet = false;

        for (int b = 0; b < 900; ++b)
        {
            head.ppq = b * quartersPerBlock;
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);

            for (const juce::MidiMessageMetadata m : midi)
            {
                const juce::MidiMessage msg = m.getMessage();
                const int key = msg.getChannel() * 1000 + msg.getNoteNumber();
                if (msg.isNoteOn())       ++openByPitch[key];
                else if (msg.isNoteOff()) openByPitch[key] = juce::jmax (0, openByPitch[key] - 1);
            }

            // Queue a jump once we are a little way into the song.
            if (! queuedYet && b == 60)
            {
                proc.queueSection (targetSection);
                queuedYet = true;
            }

            if (queuedYet)
            {
                ++blocksAfterQueue;
                if (jumpedAtTick < 0 && proc.getSequenceNoteOnCount (proc.channelDrums.load()) >= 0
                    && proc.activeSection.load() == targetSection)
                    jumpedAtTick = proc.playbackTick.load();
            }
        }

        check (jumpedAtTick >= 0, "clicking a section jumps to it");
        check (jumpedAtTick == secs[targetSection].startTick,
               "the jump lands exactly on the start of that section",
               juce::String (jumpedAtTick) + " vs "
                   + juce::String (secs[static_cast<size_t> (targetSection)].startTick));
        check (barTicks > 0 && secs[targetSection].startTick % barTicks == 0,
               "and that start is on a bar line",
               "bar = " + juce::String (barTicks) + " ticks");
        check (blocksAfterQueue > 0 && blocksAfterQueue < 900,
               "the jump happened promptly, not at the end of the song");
        // The walk ends part-way through the song, so notes ringing at that
        // moment are correct, not hung. Stop the transport first - releasing
        // everything on stop is the actual guarantee - and then check.
        head.playing = false;
        buffer.clear();
        midi.clear();
        proc.processBlock (buffer, midi);
        for (const juce::MidiMessageMetadata m : midi)
        {
            const juce::MidiMessage msg = m.getMessage();
            const int key = msg.getChannel() * 1000 + msg.getNoteNumber();
            if (msg.isNoteOn())       ++openByPitch[key];
            else if (msg.isNoteOff()) openByPitch[key] = juce::jmax (0, openByPitch[key] - 1);
        }

        int stillSounding = 0;
        for (const auto& kv : openByPitch)
            if (kv.second > 0) ++stillSounding;
        check (stillSounding == 0, "nothing is left sounding after a jump and a stop",
               juce::String (stillSounding) + " pitches still open");

        head.playing = false;
        buffer.clear(); midi.clear();
        proc.processBlock (buffer, midi);
        proc.setPlayHead (nullptr);
    }

    // ---- the full band --------------------------------------------------
    // Guitar and piano are opt-in, so this also proves the opt-in works: the
    // plans above must still have produced nothing on channels 2 and 3.
    {
        const juce::File band = juce::File (planPath).getSiblingFile ("demo-band.json");
        if (band.existsAsFile())
        {
            // Only meaningful for a plan that genuinely has neither. Asserting it
            // unconditionally made every full band plan report a false failure,
            // which is worse than no check at all - a suite that cries wolf on
            // correct songs stops being read.
            if (! proc.partIsInSong (2) && ! proc.partIsInSong (3))
            check (proc.getSequenceNoteOnCount (2) == 0 && proc.getSequenceNoteOnCount (3) == 0,
                   "a plan without guitar/piano produces nothing on their channels",
                   juce::String (proc.getSequenceNoteOnCount (2)) + "/"
                       + juce::String (proc.getSequenceNoteOnCount (3)));

            proc.loadPlan (band);
            const auto s = proc.getStatus();

            check (s.ok, "full band plan loads", s.message);
            check (s.guitarProfile.contains ("IRON"), "guitar profile resolved", s.guitarProfile);
            check (s.pianoProfile.contains ("Pianist"), "piano profile resolved", s.pianoProfile);

            const int gtr = proc.getSequenceNoteOnCount (2);
            const int pno = proc.getSequenceNoteOnCount (3);
            check (gtr > 0, "guitar plays", juce::String (gtr) + " note-ons on ch2");
            check (pno > 0, "piano plays",  juce::String (pno) + " note-ons on ch3");

            check (proc.getSequenceNoteOnCount (proc.channelDrums.load()) > 0 && proc.getSequenceNoteOnCount (1) > 0,
                   "drums and bass still play alongside them");

            // The intro is "drums+bass", so the guitar must not start at tick 0.
            // Getting this wrong is how a list-valued "plays" silently becomes
            // "everything", which is exactly the bug this caught once already.
            const auto sections = proc.getSections();
            bool introHasGuitar = false;
            for (const gb::SectionReport& sec : sections)
                if (sec.name == "intro" && sec.guitarChords > 0) introHasGuitar = true;
            check (! introHasGuitar, "a section that excludes the guitar has none");

            bool introHasDrums = false;
            for (const gb::SectionReport& sec : sections)
                if (sec.name == "intro" && sec.drumHits > 0) introHasDrums = true;
            check (introHasDrums, "but a section listing drums+bass still has drums");
        }
    }

    // ---- plan round-trip ---------------------------------------------------
    // Saving is only safe if writing a plan out and reading it back produces the
    // same song. Anything the loader reads must be written, and this is what
    // catches a field that gets added to one side and not the other.
    {
        proc.loadPlan (juce::File (planPath));
        const auto original = proc.getStatus();
        const auto originalSections = proc.getSections();

        const juce::String text = proc.planAsText();
        check (text.contains ("\"sections\""), "a plan serialises to something plausible",
               juce::String (text.length()) + " chars");

        const juce::File tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("ghostband-roundtrip.json");
        tmp.replaceWithText (text);

        proc.loadPlan (tmp);
        const auto reloaded = proc.getStatus();
        const auto reloadedSections = proc.getSections();

        check (reloaded.ok, "the serialised plan loads back", reloaded.message);
        check (reloaded.bars == original.bars, "round-trip preserves the bar count",
               juce::String (original.bars) + " -> " + juce::String (reloaded.bars));
        check (reloaded.drumHits == original.drumHits && reloaded.bassNotes == original.bassNotes,
               "round-trip produces an identical song",
               juce::String (original.drumHits) + "/" + juce::String (original.bassNotes)
                   + " -> " + juce::String (reloaded.drumHits) + "/"
                   + juce::String (reloaded.bassNotes));

        bool sectionsMatch = originalSections.size() == reloadedSections.size();
        for (size_t i = 0; sectionsMatch && i < originalSections.size(); ++i)
            sectionsMatch = originalSections[i].name == reloadedSections[i].name
                         && originalSections[i].bars == reloadedSections[i].bars
                         && originalSections[i].drumHits == reloadedSections[i].drumHits;
        check (sectionsMatch, "and every section survives intact");

        tmp.deleteFile();
    }

    // ---- structure editing --------------------------------------------------
    {
        proc.loadPlan (juce::File (planPath));
        const int before = proc.getSectionCount();

        proc.addSection (0);
        check (proc.getSectionCount() == before + 1, "adding a section works",
               juce::String (before) + " -> " + juce::String (proc.getSectionCount()));

        proc.deleteSection (1);
        check (proc.getSectionCount() == before, "deleting a section works");

        auto edit = proc.getSectionEdit (1);
        const juce::String originalName = edit.name;
        edit.name = "chorus9";
        edit.bars = 12;
        proc.applySectionEdit (1, edit);

        const auto after = proc.getSectionEdit (1);
        check (after.name == "chorus9" && after.bars == 12, "editing a section applies",
               after.name + ", " + juce::String (after.bars) + " bars");

        const auto sections = proc.getSections();
        check (sections.size() > 1 && sections[1].role == "chorus",
               "renaming a section re-infers its role, so it behaves like one",
               sections.size() > 1 ? juce::String (sections[1].role) : juce::String ("?"));

        // Deleting down to nothing would leave a song that cannot render.
        while (proc.getSectionCount() > 1) proc.deleteSection (0);
        proc.deleteSection (0);
        check (proc.getSectionCount() == 1, "the last section cannot be deleted");

        proc.loadPlan (juce::File (planPath));
        juce::ignoreUnused (originalName);
    }

    // ---- a plan survives being written and read back -------------------------
    // Everything the Edit screen changes is held in a SongPlan and saved by
    // toJson, so anything toJson cannot spell is silently lost the moment you
    // press Save - and lost in a file that still looks perfectly reasonable.
    //
    // The second guitar was added long after this serialiser was written, and
    // both of playsString's shortcuts predate it: "full" was returned for a
    // section playing drums, bass, guitar and piano with the second guitar OFF
    // (so saving turned it back on), and "none" for a section where ONLY the
    // second guitar played (so a lead break saved as silence).
    //
    // Rather than check the parts that are known to have broken, this walks
    // every plan in the folder and compares every field of every section.
    {
        const juce::File plansDir = juce::File (planPath).getParentDirectory();
        juce::Array<juce::File> plans;
        plansDir.findChildFiles (plans, juce::File::findFiles, false, "*.json");

        int checked = 0, mismatched = 0;
        juce::String firstBad;

        for (const juce::File& f : plans)
        {
            gb::SongPlan a, b;
            std::string err;

            if (! gb::SongPlan::load (f.getFullPathName().toStdString(), a, err))
                continue;

            if (! gb::SongPlan::parse (a.toJson(), "round-trip", b, err))
            {
                ++mismatched;
                if (firstBad.isEmpty())
                    firstBad = f.getFileName() + " will not re-parse: " + juce::String (err);
                continue;
            }

            ++checked;

            const auto note = [&firstBad, &f] (const juce::String& what, size_t i)
            {
                if (firstBad.isEmpty())
                    firstBad = f.getFileName() + " section " + juce::String ((int) i)
                             + ": " + what;
            };

            if (a.sections.size() != b.sections.size())
            {
                ++mismatched;
                note ("section count changed", 0);
                continue;
            }

            bool bad = false;
            for (size_t i = 0; i < a.sections.size(); ++i)
            {
                const gb::SectionPlan& x = a.sections[i];
                const gb::SectionPlan& y = b.sections[i];

                const auto differs = [&] (bool cond, const char* what)
                {
                    if (! cond) return false;
                    bad = true;
                    note (what, i);
                    return true;
                };

                differs (x.name != y.name,                 "name");
                differs (x.role != y.role,                 "role");
                differs (x.bars != y.bars,                 "bars");
                differs (std::abs (x.intensity - y.intensity) > 0.001, "intensity");
                differs (x.feel != y.feel,                 "feel");
                differs (x.chords != y.chords,             "chords");
                differs (x.bassPattern != y.bassPattern,   "bass pattern");
                differs (x.fill != y.fill,                 "fill");
                differs (x.playsDrums != y.playsDrums,     "plays drums");
                differs (x.playsBass != y.playsBass,       "plays bass");
                differs (x.playsGuitar != y.playsGuitar,   "plays guitar");
                differs (x.playsGuitar2 != y.playsGuitar2, "PLAYS SECOND GUITAR");
                differs (x.playsPiano != y.playsPiano,     "plays piano");
                differs (x.guitarPhrase != y.guitarPhrase,   "guitar phrase");
                differs (x.guitar2Phrase != y.guitar2Phrase, "SECOND GUITAR PHRASE");
                differs (x.pianoPhrase != y.pianoPhrase,     "piano phrase");
                differs (x.lead != y.lead,                 "lead");
                differs (x.vary != y.vary,                 "vary");
            }

            if (bad) ++mismatched;
        }

        check (checked > 5, "there are plans to round-trip",
               juce::String (checked) + " parsed");
        check (mismatched == 0, "every plan survives being saved and read back",
               mismatched == 0 ? juce::String ("all ") + juce::String (checked) + " intact"
                               : juce::String (mismatched) + " changed - first: " + firstBad);

        // The two shortcuts, built on purpose. No plan in the folder happens to
        // hit either today, so the sweep above would have passed with the bug
        // still in place - and a test that cannot fail is not a test.
        {
            gb::SongPlan made;
            std::string err;
            gb::SongPlan::parse (
                "{ \"name\": \"shortcut probe\", \"bpm\": 120,"
                "  \"drum_profile\": \"profiles/gm-drums.json\","
                "  \"bass_profile\": \"profiles/modo-bass-2.json\","
                "  \"sections\": ["
                "    { \"name\": \"everything but the lead\", \"bars\": 4,"
                "      \"plays\": \"drums+bass+guitar+piano\" },"
                "    { \"name\": \"lead alone\", \"bars\": 4,"
                "      \"plays\": \"guitar2\" } ]}",
                "probe", made, err);

            check (made.sections.size() == 2, "the shortcut probe plan parses",
                   juce::String ((int) made.sections.size()) + " sections");

            if (made.sections.size() == 2)
            {
                gb::SongPlan back;
                const bool ok = gb::SongPlan::parse (made.toJson(), "probe2", back, err);

                check (ok && back.sections.size() == 2,
                       "and survives a round trip", juce::String (err));

                if (ok && back.sections.size() == 2)
                {
                    // "full" used to swallow this: everything else on, second
                    // guitar off, saved as "full", reloaded with it back ON.
                    check (! back.sections[0].playsGuitar2,
                           "a section with every part BUT the second guitar keeps it off",
                           back.sections[0].playsGuitar2 ? "it came back switched on"
                                                         : "still off");

                    // "none" used to swallow this: only the second guitar on,
                    // saved as silence, and the lead break lost.
                    check (back.sections[1].playsGuitar2,
                           "and a section played by the second guitar alone survives",
                           back.sections[1].playsGuitar2 ? "still playing"
                                                         : "came back silent");
                }
            }
        }
    }

    // ---- per-section reroll -----------------------------------------------
    // The whole promise is that rerolling one section cannot disturb another.
    // That is a property of how section seeds are derived, and it is exactly the
    // kind of thing that quietly stops being true, so it is asserted.
    {
        proc.loadPlan (juce::File (planPath));
        const auto before = proc.getSections();
        check (before.size() > 4, "enough sections to test a targeted reroll");

        const int target = 2;
        proc.rerollSections ({ target });
        const auto after = proc.getSections();

        check (after.size() == before.size(), "reroll does not change the section count");

        bool targetChanged = false;
        int othersChanged = 0;
        for (size_t i = 0; i < before.size() && i < after.size(); ++i)
        {
            const bool changed = before[i].drumHits != after[i].drumHits
                              || before[i].bassNotes != after[i].bassNotes;
            if (static_cast<int> (i) == target) targetChanged = changed;
            else if (changed)                   ++othersChanged;
        }

        check (targetChanged, "the rerolled section actually changed",
               juce::String (before[target].drumHits) + "/" + juce::String (before[target].bassNotes)
                   + " -> " + juce::String (after[target].drumHits) + "/"
                   + juce::String (after[target].bassNotes));

        check (othersChanged == 0, "and no other section moved",
               juce::String (othersChanged) + " others changed");

        // Rerolling again must keep moving, not settle back.
        proc.rerollSections ({ target });
        const auto third = proc.getSections();
        check (third[target].drumHits != after[target].drumHits
                   || third[target].bassNotes != after[target].bassNotes,
               "a second reroll of the same section moves again");

        proc.loadPlan (juce::File (planPath));   // reset for later checks
    }

    // ---- calibration ------------------------------------------------------
    {
        // Loaded with the band plan, so calibration is exercised with all four
        // instruments present rather than just drums and bass.
        const juce::File band = juce::File (planPath).getSiblingFile ("demo-band.json");
        proc.loadPlan (band.existsAsFile() ? band : juce::File (planPath));
        check (! proc.isCalibrating(), "does not start in calibration mode");

        // ---- calibration reaches every instrument in the song ---------------
        // Calibrate is how a profile stops being a guess, and it walked guitar
        // and piano but not the second guitar - so Shreddage, whose profile
        // says in as many words that its range is "reasoned rather than
        // measured - walk the Calibrate screen to confirm it", was the one
        // instrument that could not be walked.
        //
        // Checked by channel rather than by label, because a channel is what a
        // step actually reaches and a label is only what it claims.
        {
            proc.enterCalibration();

            std::set<int> channels;
            for (int i = 0; i < proc.getCalibrationStepCount(); ++i)
                channels.insert (proc.getCalibrationStep (i).channel);

            const auto reaches = [&channels] (int ch) { return channels.count (ch) > 0; };

            check (reaches (proc.channelDrums.load()) && reaches (proc.channelBass.load()),
                   "calibration reaches drums and bass");

            for (int part = 2; part <= 4; ++part)
            {
                if (! proc.partIsInSong (part))
                    continue;

                const int ch = proc.channelForPart (part);
                static const char* names[5] = { "drums", "bass", "guitar", "piano", "guitar 2" };

                check (reaches (ch),
                       juce::String ("calibration reaches ") + names[part],
                       "channel " + juce::String (ch)
                           + (reaches (ch) ? " is stepped through"
                                           : " is never played - it cannot be calibrated"));
            }

            // ---- and every step it offers can actually be saved -----------
            // The second guitar was added to the step list and not to the save,
            // so its steps played, showed a note, took a nudge, and were thrown
            // away - while the screen said "saved". A calibration you cannot
            // keep is worse than one you cannot run: you believe it worked.
            //
            // saveCalibration matches steps BY LABEL, so the labels are the
            // contract. Checked here rather than trusting two lists written in
            // different functions to agree.
            {
                std::set<juce::String> offered;
                for (int i = 0; i < proc.getCalibrationStepCount(); ++i)
                    offered.insert (proc.getCalibrationStep (i).label);

                static const char* mustSave[] = {
                    "bass lowest note", "bass highest note",
                    "guitar lowest chord note",   "guitar highest chord note",
                    "guitar 2 lowest chord note", "guitar 2 highest chord note",
                    "piano lowest chord note",    "piano highest chord note",
                };

                // A label is only required when its part is in this song.
                const auto partOf = [] (const juce::String& l)
                {
                    if (l.startsWith ("bass"))      return 1;
                    if (l.startsWith ("guitar 2"))  return 4;
                    if (l.startsWith ("guitar"))    return 2;
                    return 3;
                };

                juce::StringArray missing;
                for (const char* label : mustSave)
                    if (proc.partIsInSong (partOf (label)) && offered.count (label) == 0)
                        missing.add (label);

                check (missing.isEmpty(),
                       "every range the save knows how to write is offered as a step",
                       missing.isEmpty() ? juce::String ("all present")
                                         : "never offered: " + missing.joinIntoString (", "));
            }

            proc.exitCalibration();
        }

        proc.enterCalibration();
        check (proc.isCalibrating(), "enters calibration");

        const int steps = proc.getCalibrationStepCount();
        check (steps > 10, "calibration covers the whole kit",
               juce::String (steps) + " steps");

        // Every part the song has must be calibratable, not just the drums -
        // "only the drums showed up" was the report.
        juce::StringArray labels;
        for (int i = 0; i < steps; ++i) labels.add (proc.getCalibrationStep (i).label);
        const juce::String all = labels.joinIntoString (" | ");

        check (all.contains ("bass"),   "calibration includes the bass");
        check (all.contains ("guitar"), "calibration includes the guitar",
               band.existsAsFile() ? juce::String() : juce::String ("no band plan to test with"));
        check (all.contains ("piano"),  "calibration includes the piano");

        const auto before = proc.getCalibrationStep (0);
        proc.nudgeCalibrationNote (0, +1);
        const auto after = proc.getCalibrationStep (0);
        check (after.note == before.note + 1, "nudging moves the note",
               juce::String (before.note) + " -> " + juce::String (after.note));
        check (proc.calibrationHasEdits(), "edits are tracked so Save can be offered");

        // The whole point is that this works with the host stopped - otherwise
        // you cannot calibrate without starting a full band over the top.
        head.playing = false;
        proc.setPlayHead (&head);
        proc.auditionStep (0);

        bool heard = false;
        for (int b = 0; b < 4 && ! heard; ++b)
        {
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);
            for (const juce::MidiMessageMetadata m : midi)
                if (m.getMessage().isNoteOn()) heard = true;
        }
        check (heard, "auditioning sounds a note with the transport stopped");

        // Put it back, and never call saveCalibration here - it would overwrite
        // the shipped profile from a test run.
        proc.nudgeCalibrationNote (0, -1);
        proc.exitCalibration();
        check (! proc.isCalibrating(), "leaves calibration");
        proc.setPlayHead (nullptr);
    }

    proc.setPlayHead (nullptr);

    // Parity with the CLI. Pass the counts the CLI prints for the same plan and
    // the harness will hold the plugin to them. They must agree exactly: both
    // run the same engine through the same profiles, so any divergence means one
    // path has grown a bug - as happened once already, when the dials were held
    // as float and rounding sent the RNG down a different branch.
    // argv[2] and argv[3] are the expected counts - but only when they are
    // counts. Running with "--snapshot <dir>" put a flag and a path in those
    // slots, both of which read as zero, so the harness held the plugin to
    // 0 drum hits and 0 bass notes and reported a parity failure that was
    // nothing but its own argument parsing.
    const bool countsGiven = argc > 3 && ! juce::String (argv[2]).startsWith ("--");

    if (countsGiven)
    {
        proc.loadPlan (juce::File (planPath));   // reset the dials the rolls changed
        const auto reloaded = proc.getStatus();

        std::cout << "  parity state: seed " << proc.seed.load()
                  << ", complexity " << proc.complexity.load()
                  << ", humanize " << proc.humanize.load()
                  << ", key pc " << proc.getKeyPitchClass()
                  << ", " << reloaded.headline << "\n";
        const int expectedDrums = juce::String (argv[2]).getIntValue();
        const int expectedBass  = juce::String (argv[3]).getIntValue();

        check (reloaded.drumHits == expectedDrums && reloaded.bassNotes == expectedBass,
               "plugin output matches the CLI for the same plan and seed",
               juce::String (reloaded.drumHits) + "/" + juce::String (reloaded.bassNotes)
                   + " vs CLI " + juce::String (expectedDrums) + "/" + juce::String (expectedBass));
    }

    // ---- editor size is remembered -----------------------------------------
    // The editor used to clobber the remembered size during construction, so it
    // opened at the minimum however the user left it. Closing and reopening is
    // the exact thing that was broken, so that is what is tested.
    {
        proc.editorWidth.store (900);
        proc.editorHeight.store (820);

        if (auto* ed = proc.createEditorIfNeeded())
        {
            check (ed->getWidth() == 900 && ed->getHeight() == 820,
                   "the editor opens at the remembered size",
                   juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()));

            ed->setSize (760, 900);
            proc.editorBeingDeleted (ed);
            delete ed;

            check (proc.editorWidth.load() == 760 && proc.editorHeight.load() == 900,
                   "resizing is remembered after the window closes",
                   juce::String (proc.editorWidth.load()) + "x"
                       + juce::String (proc.editorHeight.load()));
        }

        if (auto* ed = proc.createEditorIfNeeded())
        {
            check (ed->getWidth() == 760 && ed->getHeight() == 900,
                   "and comes back on reopening",
                   juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()));
            proc.editorBeingDeleted (ed);
            delete ed;
        }
    }

    // ---- control types -----------------------------------------------------
    // A selector that lands one choice off is not audibly "a bit wrong", it is
    // the wrong amp. The arithmetic is worth asserting rather than eyeballing.
    {
        const auto ccOf = [] (const gb::PhraseProfile::ControlDef& d, double t)
        {
            const double v = d.valueAt (t);
            return juce::jlimit (0, 127, static_cast<int> (v * 127.0 + 0.5));
        };

        gb::PhraseProfile::ControlDef knob;
        check (ccOf (knob, 0.0) == 0 && ccOf (knob, 1.0) == 127
                   && ccOf (knob, 0.5) == 64,
               "a knob sweeps its whole range");

        gb::PhraseProfile::ControlDef ranged;
        ranged.low = 0.25; ranged.high = 0.75;
        check (ccOf (ranged, 0.0) == 32 && ccOf (ranged, 1.0) == 95,
               "a knob stays inside its declared range",
               juce::String (ccOf (ranged, 0.0)) + ".." + juce::String (ccOf (ranged, 1.0)));

        gb::PhraseProfile::ControlDef sw;
        sw.type = "switch";
        check (ccOf (sw, 0.0) == 0 && ccOf (sw, 1.0) == 127 && ccOf (sw, 0.49) == 0
                   && ccOf (sw, 0.51) == 127,
               "a switch is only ever fully off or fully on");

        // The bug this replaced: "fixed" drove t to zero, so a switch could be
        // parked off but never on, which made a latch button unmappable.
        gb::PhraseProfile::ControlDef latched;
        latched.type = "switch"; latched.low = 1.0; latched.high = 1.0;
        check (ccOf (latched, 0.0) == 127,
               "a switch declared on stays on when it is parked");

        // Positions are spread endpoint to endpoint, which is how a host maps a
        // stepped parameter: first choice fully down, last fully up.
        gb::PhraseProfile::ControlDef five;
        five.type = "select"; five.positions = 5;

        std::set<int> landed;
        bool onlyOnPositions = true;
        for (int i = 0; i <= 100; ++i)
        {
            const int cc = ccOf (five, i / 100.0);
            landed.insert (cc);
            if (cc != 0 && cc != 32 && cc != 64 && cc != 95 && cc != 127)
                onlyOnPositions = false;
        }

        check (onlyOnPositions, "a selector only ever lands on one of its choices");
        check (landed.size() == 5, "a 5-way selector reaches all five choices",
               juce::String (static_cast<int> (landed.size())) + " distinct values");
        check (ccOf (five, 0.0) == 0 && ccOf (five, 1.0) == 127,
               "a selector reaches its first and last choice");

        gb::PhraseProfile::ControlDef three;
        three.type = "select"; three.positions = 3;
        check (ccOf (three, 0.0) == 0 && ccOf (three, 0.5) == 64 && ccOf (three, 1.0) == 127,
               "a 3-way selector puts its middle choice in the middle");

        // Narrowing a selector keeps a song out of the choices it should not use.
        gb::PhraseProfile::ControlDef upper;
        upper.type = "select"; upper.positions = 5; upper.low = 0.5; upper.high = 1.0;
        check (ccOf (upper, 0.0) == 64 && ccOf (upper, 1.0) == 127,
               "a narrowed selector never leaves its declared choices");

        // A stompbox list runs to thirty, which is well past any count worth
        // offering in a dropdown - and every one of those thirty has to be
        // reachable and distinct, or two of them are the same pedal.
        gb::PhraseProfile::ControlDef stomp;
        stomp.type = "select"; stomp.positions = 30;

        std::set<int> stompValues;
        for (int i = 0; i <= 2000; ++i)
            stompValues.insert (ccOf (stomp, i / 2000.0));

        check (stompValues.size() == 30, "a 30-way selector reaches all thirty choices",
               juce::String (static_cast<int> (stompValues.size())) + " distinct values");
        check (ccOf (stomp, 0.0) == 0 && ccOf (stomp, 1.0) == 127,
               "a 30-way selector reaches its first and last choice");

        // The widest a controller can express. Beyond this two choices would
        // have to share a value.
        gb::PhraseProfile::ControlDef widest;
        widest.type = "select"; widest.positions = 128;

        std::set<int> widestValues;
        for (int i = 0; i <= 5000; ++i)
            widestValues.insert (ccOf (widest, i / 5000.0));

        check (widestValues.size() == 128, "a 128-way selector still has no two choices alike",
               juce::String (static_cast<int> (widestValues.size())) + " distinct values");

        // Parking a selector on a chosen position, which is what "fixed" plus a
        // value has to mean. Position 3 of 6 sits two steps up from the bottom.
        gb::PhraseProfile::ControlDef parked;
        parked.type = "select"; parked.positions = 6;
        parked.low = parked.high = 2.0 / 5.0;
        check (ccOf (parked, 0.0) == 51 && ccOf (parked, 1.0) == 51,
               "a parked selector stays on its chosen position whatever the section",
               juce::String (ccOf (parked, 0.0)) + " / " + juce::String (ccOf (parked, 1.0)));

        // An inverted range, which is the whole answer for a control that reads
        // backwards. The engine interpolates either way round, so putting the
        // higher end first is all inversion needs to be.
        gb::PhraseProfile::ControlDef backwards;
        backwards.low = 1.0; backwards.high = 0.0;
        check (ccOf (backwards, 0.0) == 127 && ccOf (backwards, 1.0) == 0
                   && ccOf (backwards, 0.25) == 95,
               "a knob with its range reversed runs backwards");

        gb::PhraseProfile::ControlDef backSwitch;
        backSwitch.type = "switch"; backSwitch.low = 1.0; backSwitch.high = 0.0;
        check (ccOf (backSwitch, 0.0) == 127 && ccOf (backSwitch, 1.0) == 0,
               "a switch with its range reversed is on when it would be off");

        gb::PhraseProfile::ControlDef backSelect;
        backSelect.type = "select"; backSelect.positions = 6;
        backSelect.low = 1.0; backSelect.high = 0.0;
        check (ccOf (backSelect, 0.0) == 127 && ccOf (backSelect, 1.0) == 0,
               "a selector with its range reversed counts down");

        // Narrowing a long list to one bank of it - the reason ranges exist for
        // a rolled control at all. A 62-way list, held inside the first 30.
        gb::PhraseProfile::ControlDef bank;
        bank.type = "select"; bank.positions = 62;
        bank.low = 0.0; bank.high = 29.0 / 61.0;

        bool stayedInBank = true;
        for (int i = 0; i <= 500; ++i)
        {
            const int pos = juce::roundToInt (ccOf (bank, i / 500.0) / 127.0 * 61.0);
            if (pos < 0 || pos > 29) stayedInBank = false;
        }
        check (stayedInBank, "a narrowed selector never leaves its bank");

        // A "select" with too few positions is not a selector at all; it must
        // degrade to a plain sweep rather than divide by zero.
        gb::PhraseProfile::ControlDef broken;
        broken.type = "select"; broken.positions = 1;
        check (ccOf (broken, 0.0) == 0 && ccOf (broken, 1.0) == 127,
               "a selector with too few choices degrades to a sweep");
    }

    // ---- a control set to "none" is left alone -----------------------------
    // "fixed" was the only way to say "do not automate this", and it does the
    // opposite: it drives the control to the bottom of its range, so a mix knob
    // lands on zero and an effect gets switched off. "none" sends nothing.
    {
        gb::PhraseProfile prof;
        prof.channel = 2;

        gb::PhraseProfile::ControlDef driven;
        driven.name = "driven"; driven.cc = 40; driven.follows = "intensity";
        prof.editableControls().push_back (driven);

        gb::PhraseProfile::ControlDef left;
        left.name = "left alone"; left.cc = 41; left.follows = "none";
        prof.editableControls().push_back (left);

        gb::PhrasePart part;
        part.controls.push_back ({ 0, "driven",     0.8 });
        part.controls.push_back ({ 0, "left alone", 0.8 });

        gb::MidiTrack track;
        prof.render (part, track);

        int on40 = 0, on41 = 0;
        for (const gb::MidiEvent& ev : track.events)
            if (ev.bytes.size() >= 3 && (ev.bytes[0] & 0xF0) == 0xB0)
            {
                if (ev.bytes[1] == 40) ++on40;
                if (ev.bytes[1] == 41) ++on41;
            }

        check (on40 > 0, "a driven control is sent");
        check (on41 == 0, "a control set to none is never sent",
               juce::String (on41) + " messages");
    }

    // ---- the mix knobs reach something --------------------------------------
    // The knobs sent CC 7 and nothing else, on the assumption that instruments
    // respond to channel volume. Most do not, so the knobs moved nothing at all.
    // A control taught as "level" belongs to the knob, and the arrangement must
    // keep its hands off it or the two fight over the same controller.
    {
        gb::PhraseProfile prof;
        prof.channel = 2;

        gb::PhraseProfile::ControlDef vol;
        vol.name = "volume"; vol.cc = 42; vol.follows = "level";
        prof.editableControls().push_back (vol);

        gb::PhrasePart part;
        part.controls.push_back ({ 0, "volume", 0.9 });

        gb::MidiTrack track;
        prof.render (part, track);

        int sent = 0;
        for (const gb::MidiEvent& ev : track.events)
            if (ev.bytes.size() >= 3 && (ev.bytes[0] & 0xF0) == 0xB0 && ev.bytes[1] == 42)
                ++sent;

        check (sent == 0, "the arrangement never drives a level control",
               juce::String (sent) + " messages");

        // And the knob's own travel still runs through the control's range, so
        // a gain that reads backwards can be inverted like anything else.
        gb::PhraseProfile::ControlDef backwards;
        backwards.follows = "level"; backwards.low = 1.0; backwards.high = 0.0;
        check (juce::roundToInt (backwards.valueAt (0.0) * 127.0) == 127
                   && juce::roundToInt (backwards.valueAt (1.0) * 127.0) == 0,
               "a level control honours an inverted range");
    }

    // ---- random controls ----------------------------------------------------
    // Some controls have no right answer to tie to the arrangement, so they get
    // chosen instead. Two things have to hold: the choice must be reproducible
    // from the seed, and adding one must not move a single note of a song that
    // was already right.
    {
        gb::SongPlan plan;
        std::string planErr;
        const bool planOk = gb::SongPlan::load (juce::File (planPath).getFullPathName().toStdString(),
                                                plan, planErr);
        check (planOk, "plan loads for the random-control test", juce::String (planErr));

        if (planOk)
        {
            gb::DrumProfile kit;
            gb::BassProfile bass;

            // A guitar with one of each kind of rolled control.
            const auto guitarWith = [] (const std::string& follows)
            {
                gb::PhraseProfile g;
                g.id = "test_guitar";
                g.phraseDriven = false;
                g.chordLowest = 60; g.chordHighest = 84;

                gb::PhraseProfile::ControlDef amp;
                amp.name = "amp"; amp.cc = 23; amp.type = "select";
                amp.positions = 6; amp.follows = follows;
                g.editableControls().push_back (amp);
                return g;
            };

            // Where each section's controls end up, section by section.
            const auto ampPerSection = [&] (const gb::PhraseProfile& g)
            {
                const gb::RenderResult r = gb::renderPerformance (plan, kit, bass, &g, nullptr);

                std::vector<double> values;
                for (const gb::ControlIntent& c : r.performance.guitar.controls)
                    if (c.control == "amp")
                        values.push_back (c.amount);
                return values;
            };

            const gb::PhraseProfile once = guitarWith ("random once");
            const gb::PhraseProfile each = guitarWith ("random");

            const std::vector<double> onceValues = ampPerSection (once);
            const std::vector<double> eachValues = ampPerSection (each);

            check (onceValues.size() > 2, "a rolled control is sent in every section",
                   juce::String (static_cast<int> (onceValues.size())) + " sections");

            const auto distinct = [] (const std::vector<double>& v)
            {
                std::set<int> seen;
                for (double d : v) seen.insert (juce::roundToInt (d * 127.0));
                return static_cast<int> (seen.size());
            };

            check (distinct (onceValues) == 1,
                   "random once holds one choice for the whole song",
                   juce::String (distinct (onceValues)) + " distinct values");

            check (distinct (eachValues) > 1,
                   "random picks again from section to section",
                   juce::String (distinct (eachValues)) + " distinct values");

            // Reproducible, or a song is not a song.
            check (ampPerSection (each) == eachValues,
                   "the same seed rolls the same choices again");

            gb::SongPlan reseeded = plan;
            reseeded.seed = plan.seed + 1u;
            {
                const gb::RenderResult r = gb::renderPerformance (reseeded, kit, bass, &each, nullptr);
                std::vector<double> other;
                for (const gb::ControlIntent& c : r.performance.guitar.controls)
                    if (c.control == "amp") other.push_back (c.amount);
                check (other != eachValues, "a different seed rolls different choices");
            }

            // Every rolled value still has to be one of the selector's six
            // positions - a random amp that lands between two amps is no amp.
            bool onPositions = true;
            for (double d : eachValues)
            {
                const int cc = juce::roundToInt (d * 127.0);
                if (cc != 0 && cc != 25 && cc != 51 && cc != 76 && cc != 102 && cc != 127)
                    onPositions = false;
            }
            check (onPositions, "a rolled selector still lands on a real position");

            // The one that matters most: a rolled control draws from its own
            // stream, so adding one cannot shift the notes of a finished song.
            gb::PhraseProfile plainGuitar = guitarWith ("random");
            plainGuitar.editableControls().clear();

            const gb::RenderResult withCtl = gb::renderPerformance (plan, kit, bass, &each, nullptr);
            const gb::RenderResult without = gb::renderPerformance (plan, kit, bass, &plainGuitar, nullptr);

            bool sameNotes = withCtl.sections.size() == without.sections.size();
            for (size_t i = 0; sameNotes && i < withCtl.sections.size(); ++i)
                sameNotes = withCtl.sections[i].drumHits     == without.sections[i].drumHits
                         && withCtl.sections[i].bassNotes    == without.sections[i].bassNotes
                         && withCtl.sections[i].guitarChords == without.sections[i].guitarChords;

            check (sameNotes, "adding a rolled control moves no note of the song");
        }
    }

    // ---- swing ---------------------------------------------------------------
    // A shuffle is the same groove on a different grid. The two things that
    // matter: it must move the offbeats late and leave the downbeats alone, and
    // with no swing asked for it must move nothing at all - every song written
    // before this existed has to render byte for byte as it did.
    {
        gb::SongPlan plan;
        std::string err;
        const bool ok = gb::SongPlan::load (juce::File (planPath).getFullPathName().toStdString(),
                                            plan, err);
        check (ok, "plan loads for the swing test", juce::String (err));

        if (ok)
        {
            gb::DrumProfile kit;
            gb::BassProfile bass;

            const auto drumTicks = [&] (double swing)
            {
                gb::SongPlan p = plan;
                p.swing = swing;
                const gb::RenderResult r = gb::renderPerformance (p, kit, bass, nullptr, nullptr);

                std::vector<int> ticks;
                for (const gb::DrumIntent& d : r.performance.drums)
                    ticks.push_back (d.tick);
                return ticks;
            };

            const std::vector<int> straight = drumTicks (0.0);
            const std::vector<int> swung    = drumTicks (0.62);

            check (! straight.empty(), "the swing test has drums to look at");
            // Swinging thins before it warps. A shuffle is a triplet feel and a
            // straight sixteenth is not a rhythm inside one, so the sixteenths
            // between the eighths are dropped - which is what turns a sixteenth
            // ride into the eighth ride a shuffle actually plays. It must only
            // ever remove, never invent.
            check (swung.size() > 0 && swung.size() < straight.size(),
                   "swinging thins the sixteenths out and adds nothing",
                   juce::String ((int) straight.size()) + " straight, "
                       + juce::String ((int) swung.size()) + " swung");

            bool everySwungOnsetIsOnAnEighth = true;
            for (int t : swung)
            {
                const double pos = (t % 480) / 480.0;
                const int nearest = static_cast<int> (pos * 4.0 + 0.5) % 4;
                if (nearest == 1 || nearest == 3)
                    everySwungOnsetIsOnAnEighth = false;
            }
            check (everySwungOnsetIsOnAnEighth,
                   "and nothing is left on a straight sixteenth");

            // What matters is not that a tick is unchanged - humanize has
            // already nudged these a few ticks off the grid, and the warp
            // scales that nudge with everything else. It is that a downbeat is
            // still a downbeat afterwards and an eighth has moved late.
            const int beat = 480;   // kPPQ
            int stayedOnTheBeat = 0, driftedOffTheBeat = 0, pushedLate = 0;

            for (int t : swung)
            {
                const double pos = (t % beat) / static_cast<double> (beat);

                if (pos < 0.05 || pos > 0.95)   ++stayedOnTheBeat;
                else if (pos > 0.55)            ++pushedLate;
                else                            ++driftedOffTheBeat;
            }

            check (driftedOffTheBeat == 0,
                   "every onset is either on the beat or late on the swung eighth",
                   juce::String (driftedOffTheBeat) + " landed somewhere else");
            check (stayedOnTheBeat > 0, "downbeats are still downbeats",
                   juce::String (stayedOnTheBeat));
            check (pushedLate > 0, "the offbeats are pushed late",
                   juce::String (pushedLate) + " moved late");

            // The one that protects every song written before swing existed.
            check (drumTicks (0.0) == straight, "no swing moves nothing at all");

            // A full triplet puts the offbeat two thirds of the way through the
            // beat, which is the definition rather than a preference.
            gb::SongPlan full = plan;
            full.swing = 1.0;
            const gb::RenderResult r = gb::renderPerformance (full, kit, bass, nullptr, nullptr);

            bool sawTriplet = false, allInsideBeat = true;
            for (const gb::DrumIntent& d : r.performance.drums)
            {
                const int within = d.tick % beat;
                if (within == 320) sawTriplet = true;     // 2/3 of 480
                if (within >= beat) allInsideBeat = false;
            }
            check (sawTriplet, "a full shuffle lands the offbeat on the triplet");
            check (allInsideBeat, "no note is warped past the end of its beat");
        }
    }

    // ---- a solo is a line, not a scale ---------------------------------------
    // The difference between the two is phrasing, so that is what gets checked:
    // one note at a time, mostly stepwise, and with silence in it. A part that
    // plays on every subdivision is a texture however good the notes are.
    {
        gb::SongPlan plan;
        std::string err;
        if (gb::SongPlan::load (juce::File (planPath).getParentDirectory()
                                    .getChildFile ("preset-blues.json")
                                    .getFullPathName().toStdString(), plan, err))
        {
            gb::DrumProfile kit;
            gb::BassProfile bass;

            gb::PhraseProfile guitar;
            guitar.id = "solo_test";
            guitar.phraseDriven = false;
            guitar.chordLowest  = 60;
            guitar.chordHighest = 84;

            // Both guitars, and the lead line from whichever one is soloing.
            // Shreddage is the solo guitar in every song that has a solo, so the
            // line moved to guitar2 and this was reading an empty part.
            const gb::RenderResult r = gb::renderPerformance (plan, kit, bass, &guitar,
                                                              nullptr, &guitar);
            const auto& lead = ! r.performance.guitar2.lead.empty()
                                 ? r.performance.guitar2.lead
                                 : r.performance.guitar.lead;

            check (! lead.empty(), "a solo section produces a melodic line",
                   juce::String ((int) lead.size()) + " notes");

            if (! lead.empty())
            {
                bool monophonic = true, inRange = true;
                int  leaps = 0, steps = 0;

                for (size_t i = 0; i < lead.size(); ++i)
                {
                    if (lead[i].pitch < 60 || lead[i].pitch > 84) inRange = false;

                    if (i + 1 < lead.size())
                    {
                        if (lead[i].tick + lead[i].durationTicks > lead[i + 1].tick + 1)
                            monophonic = false;

                        const int interval = std::abs (lead[i + 1].pitch - lead[i].pitch);
                        if (interval > 4) ++leaps; else ++steps;
                    }
                }

                check (monophonic, "one note at a time - a player has one voice");
                check (inRange, "and every note is inside the instrument's range");
                check (steps > leaps, "it moves mostly by step rather than leaping",
                       juce::String (steps) + " steps, " + juce::String (leaps) + " leaps");

                // Silence is the thing that makes it phrase. Without a real rest
                // somewhere it is a texture, whatever the notes are.
                int longestRest = 0;
                for (size_t i = 0; i + 1 < lead.size(); ++i)
                    longestRest = std::max (longestRest,
                                            lead[i + 1].tick
                                                - (lead[i].tick + lead[i].durationTicks));

                check (longestRest > 240, "and it stops to breathe",
                       "longest rest " + juce::String (longestRest) + " ticks");

                // Density, which is the check that was missing. Every test
                // above passed while the solo was playing 3.3 notes a bar with
                // gaps of a bar and a half - one note held and then changed,
                // which is exactly how it was reported. Phrasing checks cannot
                // see that, because a line with almost nothing in it phrases
                // beautifully.
                //
                // The blues is a shuffle, so the swing pass leaves eighths and
                // drops anything between them: eight slots in a bar, and a
                // solo worth the name uses most of them.
                int soloBars = 0, soloNotes = 0;
                for (const gb::SectionReport& sec : r.sections)
                    if (sec.name == "solo")
                    {
                        soloBars += sec.bars;
                        for (const gb::LeadIntent& n : lead)
                            if (n.tick >= sec.startTick && n.tick < sec.endTick)
                                ++soloNotes;
                    }

                const double perBar = soloBars > 0 ? soloNotes / (double) soloBars : 0.0;
                check (perBar >= 5.0, "and it plays like a solo rather than holding one note",
                       juce::String (perBar, 1) + " notes per bar over "
                           + juce::String (soloBars) + " bars");

                // A run has no internal shape; a sequence or a repeated lick is
                // the same shape restated, and that is what separates this from
                // a scale exercise. Finding any four-note pitch pattern that
                // occurs twice is the cheapest evidence one of them fired.
                bool restated = false;
                for (size_t i = 0; i + 4 <= lead.size() && ! restated; ++i)
                    for (size_t j = i + 1; j + 4 <= lead.size() && ! restated; ++j)
                    {
                        int same = 0;
                        for (int k = 0; k < 4; ++k)
                            if (lead[i + k].pitch == lead[j + k].pitch) ++same;
                        if (same == 4) restated = true;
                    }

                check (restated, "and it restates an idea rather than only running");

                // Two octaves are available and a lead that stays inside one of
                // them sounds like it never left first position.
                int lowest = 127, highest = 0;
                for (const gb::LeadIntent& n : lead)
                {
                    lowest  = std::min (lowest,  n.pitch);
                    highest = std::max (highest, n.pitch);
                }

                check (highest - lowest >= 12, "and it uses more than one octave",
                       juce::String (highest - lowest) + " semitones");

                // ---- bends must always come home ----------------------------
                // Pitch bend is a channel message: it moves everything sounding
                // on that channel, and one left off centre leaves the whole part
                // transposed for every note after it. That failure is silent in
                // the intents and only audible as "the plugin put my guitar out
                // of tune", so it is asserted on the MIDI itself.
                gb::PhraseProfile bendy = guitar;
                bendy.canBend            = true;
                bendy.bendRangeSemitones = 2.0;
                bendy.bendSemitones      = 2.0;
                bendy.bendTicks          = 90;

                // The lead alone. Rendering the whole part would fold the
                // comped chords in, and a chord is three notes at once by
                // definition - which makes any count of "notes sounding
                // together" meaningless. That is exactly how the first version
                // of this check reported 318 unwanted overlaps in a part that
                // had none.
                gb::PhrasePart leadOnly;
                leadOnly.lead = lead;

                gb::MidiTrack track;
                bendy.render (leadOnly, track);

                std::vector<gb::MidiEvent> ordered = track.events;
                std::stable_sort (ordered.begin(), ordered.end(),
                                  [] (const gb::MidiEvent& a, const gb::MidiEvent& b)
                                  {
                                      if (a.tick != b.tick) return a.tick < b.tick;
                                      return a.order < b.order;
                                  });

                int  bends = 0, notesWhileBent = 0, lastValue = 8192;
                bool bentNow = false;

                for (const gb::MidiEvent& e : ordered)
                {
                    if (e.bytes.size() < 3) continue;

                    const int status = e.bytes[0] & 0xF0;

                    if (status == 0xE0)
                    {
                        ++bends;
                        lastValue = (e.bytes[2] << 7) | e.bytes[1];
                        bentNow   = (lastValue != 8192);
                    }
                    else if (status == 0x90 && e.bytes[2] > 0 && bentNow)
                    {
                        // A note started while the wheel is off centre is a note
                        // sounding at the wrong pitch, unless it is the note the
                        // bend was written for - and that one is started before
                        // the wheel moves, not after.
                        ++notesWhileBent;
                    }
                }

                check (bends > 0, "a bendable profile actually bends",
                       juce::String (bends) + " wheel moves");
                check (lastValue == 8192, "and the wheel is left at centre",
                       "final value " + juce::String (lastValue));
                check (notesWhileBent == 0,
                       "and no note is ever started while the wheel is off centre",
                       juce::String (notesWhileBent) + " would sound at the wrong pitch");

                // ---- vibrato comes home too ---------------------------------
                // A modwheel left up shakes every note after it, and unlike a
                // stuck bend it does not sound out of tune - it sounds like the
                // instrument is broken in a way nobody can point at.
                gb::PhraseProfile shaky = bendy;
                shaky.vibratoCC    = 1;
                shaky.vibratoDepth = 90;
                shaky.vibratoTicks = 160;

                gb::MidiTrack vibTrack;
                shaky.render (leadOnly, vibTrack);

                std::vector<gb::MidiEvent> vibSeq = vibTrack.events;
                std::stable_sort (vibSeq.begin(), vibSeq.end(),
                                  [] (const gb::MidiEvent& a, const gb::MidiEvent& b)
                                  {
                                      if (a.tick != b.tick) return a.tick < b.tick;
                                      return a.order < b.order;
                                  });

                int moves = 0, wheelNow = 0, notesWhileShaking = 0;
                for (const gb::MidiEvent& e : vibSeq)
                {
                    if (e.bytes.size() < 3) continue;
                    const int status = e.bytes[0] & 0xF0;

                    if (status == 0xB0 && e.bytes[1] == 1)
                    {
                        ++moves;
                        wheelNow = e.bytes[2];
                    }
                    else if (status == 0x90 && e.bytes[2] > 0 && wheelNow != 0)
                    {
                        ++notesWhileShaking;
                    }
                }

                check (moves > 0, "held notes are given vibrato",
                       juce::String (moves) + " modwheel moves");
                check (wheelNow == 0, "and the wheel is left down",
                       "final value " + juce::String (wheelNow));
                check (notesWhileShaking == 0,
                       "and no note is started while it is still up",
                       juce::String (notesWhileShaking) + " would shake unasked");

                // ---- legato is an overlap, and only the overlap it asked for --
                // A library that detects legato rather than keyswitching it fires
                // on a note taken while the previous one still sounds, so the
                // lead renderer has to leave that overlap in. It used to cut
                // every note to end exactly where the next began, which made a
                // legato note impossible to produce. The risk in the other
                // direction is a line that stacks up and starts stealing its own
                // voices, so the overlap is asserted to be the declared one and
                // no more.
                gb::PhraseProfile smooth = guitar;
                smooth.legatoOverlapTicks = 12;

                gb::MidiTrack legatoTrack;
                smooth.render (leadOnly, legatoTrack);

                std::map<int, int> openAt;          // pitch -> tick it started
                int overlaps = 0, worstOverlap = 0;

                std::vector<gb::MidiEvent> seq = legatoTrack.events;
                std::stable_sort (seq.begin(), seq.end(),
                                  [] (const gb::MidiEvent& a, const gb::MidiEvent& b)
                                  {
                                      if (a.tick != b.tick) return a.tick < b.tick;
                                      return a.order < b.order;
                                  });

                int sounding = 0;
                for (const gb::MidiEvent& e : seq)
                {
                    if (e.bytes.size() < 3) continue;
                    const int status = e.bytes[0] & 0xF0;

                    if (status == 0x90 && e.bytes[2] > 0)
                    {
                        if (sounding > 0) ++overlaps;
                        ++sounding;
                        openAt[e.bytes[1]] = e.tick;
                    }
                    else if (status == 0x80 || (status == 0x90 && e.bytes[2] == 0))
                    {
                        if (sounding > 0) --sounding;
                        const auto it = openAt.find (e.bytes[1]);
                        if (it != openAt.end()) openAt.erase (it);
                    }
                }

                check (overlaps > 0, "a legato profile overlaps its lead notes",
                       juce::String (overlaps) + " notes taken while the last still sounds");

                // Nothing may overlap into a note the phrase lands on. The
                // outgoing note-off would arrive inside it, and a monophonic
                // instrument reads that as release - the held note sounds for
                // the length of the overlap and then stops dead. Invisible on a
                // run of sixteenths, a dead second on a phrase ending.
                int heldCutShort = 0;
                for (size_t k = 0; k + 1 < lead.size(); ++k)
                {
                    const gb::LeadIntent& next = lead[k + 1];
                    if (! next.target) continue;

                    for (const gb::MidiEvent& e : seq)
                    {
                        if (e.bytes.size() < 3) continue;
                        const int status = e.bytes[0] & 0xF0;
                        const bool isOff = (status == 0x80) || (status == 0x90 && e.bytes[2] == 0);
                        if (! isOff) continue;

                        const int heldEnd = next.tick + std::max (1, next.durationTicks);
                        if (e.tick > next.tick && e.tick < heldEnd)
                            ++heldCutShort;
                    }
                }

                check (heldCutShort == 0,
                       "and never into a note the phrase lands on",
                       juce::String (heldCutShort) + " held notes would be cut short");
                check (sounding == 0, "and every lead note is still released");

                // The default profile must be unaffected: an instrument that did
                // not ask for legato must keep getting notes that do not touch.
                int plainOverlaps = 0, plainSounding = 0;
                std::vector<gb::MidiEvent> plain = track.events;
                std::stable_sort (plain.begin(), plain.end(),
                                  [] (const gb::MidiEvent& a, const gb::MidiEvent& b)
                                  {
                                      if (a.tick != b.tick) return a.tick < b.tick;
                                      return a.order < b.order;
                                  });
                for (const gb::MidiEvent& e : plain)
                {
                    if (e.bytes.size() < 3) continue;
                    const int status = e.bytes[0] & 0xF0;
                    if (status == 0x90 && e.bytes[2] > 0)
                    {
                        if (plainSounding > 0) ++plainOverlaps;
                        ++plainSounding;
                    }
                    else if (status == 0x80 || (status == 0x90 && e.bytes[2] == 0))
                    {
                        if (plainSounding > 0) --plainSounding;
                    }
                }

                check (plainOverlaps == 0,
                       "and an instrument that did not ask for legato still gets none",
                       juce::String (plainOverlaps) + " unwanted overlaps");

                // A soloing part stops comping. Both at once is not something
                // one player can do.
                const gb::PhrasePart& soloist = ! r.performance.guitar2.lead.empty()
                                                  ? r.performance.guitar2
                                                  : r.performance.guitar;

                bool compedDuringSolo = false;
                for (const gb::SectionReport& sec : r.sections)
                    if (sec.name == "solo")
                        for (const gb::ChordIntent& c : soloist.chords)
                            if (c.tick >= sec.startTick && c.tick < sec.endTick)
                                compedDuringSolo = true;

                check (! compedDuringSolo, "and stops playing chords while it does");
            }
        }
    }

    // ---- a notes instrument can be told how to play -------------------------
    // A phrase instrument's keyswitch IS the performance; a notes instrument's
    // only chooses how the notes it is sent will sound. Ghostband only ever
    // emitted the first kind, so Shreddage's palm mute, power chords and
    // tremolo were mapped, documented and unreachable.
    //
    // The guard matters as much as the feature: a guessed keyswitch has
    // silenced a whole song in this project before, so nothing is sent until a
    // profile says its numbers were checked.
    {
        gb::PhraseProfile g;
        g.phraseDriven = false;
        g.chordLowest  = 40;
        g.chordHighest = 84;
        g.setKeyFor (gb::PhraseFeel::Muted,   13);
        g.setKeyFor (gb::PhraseFeel::Driving, 15);

        gb::PhrasePart part;
        gb::ChordIntent c; c.tick = 0; c.durationTicks = 480; c.rootPc = 4;
        c.thirdSemis = 3; c.fifthSemis = 7;
        part.chords.push_back (c);
        c.tick = 1920; part.chords.push_back (c);

        gb::PhraseIntent a; a.tick = 0;    a.feel = gb::PhraseFeel::Muted;   part.phrases.push_back (a);
        gb::PhraseIntent b; b.tick = 1920; b.feel = gb::PhraseFeel::Driving; part.phrases.push_back (b);

        const auto switchesIn = [] (const gb::PhraseProfile& profile, const gb::PhrasePart& p)
        {
            gb::MidiTrack t;
            profile.render (p, t);
            int n = 0;
            for (const gb::MidiEvent& e : t.events)
                if (e.bytes.size() >= 3 && (e.bytes[0] & 0xF0) == 0x90
                    && e.bytes[2] > 0 && e.bytes[1] < 26)
                    ++n;
            return n;
        };

        g.keyswitchesVerified = false;
        check (switchesIn (g, part) == 0,
               "an unverified keyswitch map sends nothing",
               juce::String (switchesIn (g, part)) + " sent");

        g.keyswitchesVerified = true;
        check (switchesIn (g, part) == 2,
               "and a verified one switches articulation per section",
               juce::String (switchesIn (g, part)) + " switches");

        // Restating a latching switch on every section floods the instrument.
        part.phrases[1].feel = gb::PhraseFeel::Muted;
        check (switchesIn (g, part) == 1,
               "and never restates one that has not changed",
               juce::String (switchesIn (g, part)) + " sent for two identical sections");
    }

    // ---- the bass plays more than one way -----------------------------------
    // MODO maps seven articulations and the generator asked for three, so a bass
    // that could hammer and slide picked every note. Worse, the code that would
    // have produced a slide - the chromatic approach into a chord change - was
    // gated on the last bar of a section, where the caller passes an invalid
    // chord, so the condition could never once be true.
    //
    // Asserted on the intents rather than the MIDI, because a profile that does
    // not map an articulation renders it silently as a plain note, which would
    // hide the generator going back to asking for nothing.
    {
        gb::SongPlan plan;
        std::string err;
        if (gb::SongPlan::load (juce::File (planPath).getFullPathName().toStdString(), plan, err))
        {
            gb::DrumProfile kit;
            gb::BassProfile bass;
            const gb::RenderResult r = gb::renderPerformance (plan, kit, bass);

            std::set<int> used;
            for (const gb::BassIntent& b : r.performance.bass)
                used.insert (static_cast<int> (b.artic));

            juce::StringArray names;
            for (int a : used) names.add (gb::bassArticName (static_cast<gb::BassArtic> (a)));

            check (used.size() >= 2, "the bass plays in more than one way",
                   names.joinIntoString (", "));

            // A note the instrument cannot reach is not a quiet note, it is no
            // note - the renderer drops it - so an approach note walking off the
            // bottom of the neck turned a chord change into a hole.
            int outOfRange = 0;
            for (const gb::BassIntent& b : r.performance.bass)
                if (b.pitch < bass.lowestNoteFor (plan.bassTuning) || b.pitch > bass.highestNote)
                    ++outOfRange;

            check (outOfRange == 0, "and every note it writes is on the instrument",
                   juce::String (outOfRange) + " would be dropped");
        }
    }

    // ---- a kit only plays the pieces it has --------------------------------
    // A drum profile inherits General MIDI for any voice its file does not
    // mention, which is what makes an unknown plugin usable straight away - and
    // is a trap when the kit genuinely lacks a piece. MINDst has three toms.
    // Omitting the fourth left it on GM's note 43, which measured silent on that
    // kit, so eleven hits a song went into a hole that nothing reported. Writing
    // null is what says "there is no such piece", and the generator then falls
    // back to a tom that exists.
    {
        const juce::File prof = juce::File (planPath).getParentDirectory()
                                    .getParentDirectory()
                                    .getChildFile ("profiles")
                                    .getChildFile ("mndst-drums.json");

        if (prof.existsAsFile())
        {
            gb::DrumProfile kit;
            std::string err;
            const bool ok = gb::DrumProfile::load (prof.getFullPathName().toStdString(), kit, err);

            check (ok, "the MINDst profile loads", juce::String (err));

            if (ok)
            {
                check (! kit.hasVoice (gb::DrumVoice::Tom4),
                       "a null voice really is absent, not inherited from General MIDI",
                       "tom4 note " + juce::String (kit.noteFor (gb::DrumVoice::Tom4)));

                check (kit.hasVoice (gb::DrumVoice::Tom1)
                           && kit.hasVoice (gb::DrumVoice::Tom2)
                           && kit.hasVoice (gb::DrumVoice::Tom3),
                       "and the three toms it does have survived the load",
                       juce::String (kit.noteFor (gb::DrumVoice::Tom1)) + "/"
                           + juce::String (kit.noteFor (gb::DrumVoice::Tom2)) + "/"
                           + juce::String (kit.noteFor (gb::DrumVoice::Tom3)));
            }
        }
    }

    // ---- saving a profile keeps the profile --------------------------------
    // Pressing Save once rewrote a driver profile from scratch and took every
    // comment in it along with it - and in these files the comments are the
    // measured findings about the instrument, not decoration. A save must edit
    // the controls block and leave the rest of the file alone.
    {
        const juce::File src (juce::File (planPath).getParentDirectory()
                                  .getParentDirectory()
                                  .getChildFile ("profiles")
                                  .getChildFile ("vg-iron2.json"));
        const juce::File tmp (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("gb-save-test.json"));
        tmp.deleteFile();

        if (src.existsAsFile() && src.copyFileTo (tmp))
        {
            gb::PhraseProfile prof;
            std::string err;
            const bool loaded = gb::PhraseProfile::load (tmp.getFullPathName().toStdString(),
                                                         prof, err);
            check (loaded, "a documented profile loads", juce::String (err));

            if (loaded)
            {
                gb::PhraseProfile::ControlDef c;
                c.name = "cab"; c.cc = 41; c.type = "select"; c.positions = 4;
                prof.editableControls().push_back (c);

                std::string saveErr;
                check (prof.save (tmp.getFullPathName().toStdString(), saveErr),
                       "the profile saves", juce::String (saveErr));

                const juce::String afterText = tmp.loadFileAsString();

                // The line that records why the guitar range is what it is.
                check (afterText.contains ("60 to 89"),
                       "saving keeps the measured findings written in the profile");
                check (afterText.contains ("Player mode"),
                       "saving keeps the note about which mode the file is for");

                int keptComments = 0;
                for (const auto& line : juce::StringArray::fromLines (afterText))
                    if (line.trim().startsWith ("//")) ++keptComments;
                check (keptComments > 20, "saving keeps the profile's comments",
                       juce::String (keptComments) + " comment lines survived");

                check (afterText.contains ("\"cab\"")
                           && afterText.contains ("\"positions\": 4"),
                       "and the new mapping is actually written");

                // It has to survive a round trip, not merely look right.
                gb::PhraseProfile back;
                std::string backErr;
                const bool reloaded = gb::PhraseProfile::load (tmp.getFullPathName().toStdString(),
                                                              back, backErr);
                bool foundCab = false;
                for (const auto& d : back.allControls())
                    if (d.name == "cab" && d.type == "select" && d.positions == 4)
                        foundCab = true;

                check (reloaded && foundCab, "and reads back as a 4-way selector",
                       juce::String (backErr));

                // Saving twice must not drift: the second save has to be a
                // no-op on everything outside the controls block.
                check (prof.save (tmp.getFullPathName().toStdString(), saveErr),
                       "the profile saves again", juce::String (saveErr));
                check (tmp.loadFileAsString() == afterText,
                       "saving twice changes nothing the second time");

                // ---- a hostile control name -----------------------------
                // The name is free text typed into the Settings screen, and it
                // is written straight back into the profile. Nothing escaped
                // it, so one double quote produced a file that was no longer
                // JSON - and the next load failed, fell back to the generic
                // profile, and the instrument silently lost its note range,
                // its keyswitches and its bends. Two keystrokes.
                {
                    gb::PhraseProfile::ControlDef nasty;
                    nasty.name = "wah \"the\" pedal \\ 100%\ttone";
                    nasty.cc = 42;
                    prof.editableControls().push_back (nasty);

                    std::string nastyErr;
                    check (prof.save (tmp.getFullPathName().toStdString(), nastyErr),
                           "a profile saves with a quote in a control name",
                           juce::String (nastyErr));

                    gb::PhraseProfile reread;
                    std::string rereadErr;
                    const bool ok = gb::PhraseProfile::load (
                        tmp.getFullPathName().toStdString(), reread, rereadErr);

                    check (ok, "and the file is still valid JSON afterwards",
                           juce::String (rereadErr));

                    // The measurements have to still be there. This is the
                    // failure that costs an evening: the file parses as
                    // something, just not as this instrument any more.
                    check (ok && tmp.loadFileAsString().contains ("60 to 89"),
                           "and the measured findings are still in it");

                    bool foundNasty = false;
                    for (const auto& d : reread.allControls())
                        if (d.name == nasty.name) foundNasty = true;

                    check (foundNasty, "and the name reads back exactly as typed",
                           juce::String (static_cast<int> (reread.allControls().size()))
                               + " controls after reload");

                    prof.editableControls().pop_back();
                    prof.save (tmp.getFullPathName().toStdString(), nastyErr);
                }

                // ---- the block's name, quoted inside a string value -------
                // findNamedBlock looks for "controls" in the raw file text. It
                // skipped // comments but NOT strings, so a note that happened
                // to quote the key was matched as if it were the block, and the
                // save then spliced over everything after that note - the
                // measurements included.
                //
                // A decoy nested inside the real block would not test this: the
                // real key still comes first. The trap needs a string value
                // ABOVE the block, which is exactly where a verification note
                // lives.
                {
                    juce::String text = tmp.loadFileAsString();
                    const int nameAt = text.indexOf ("\"name\"");

                    if (nameAt > 0)
                    {
                        text = text.substring (0, nameAt)
                             + "\"decoy_note\": \"the \\\"controls\\\": block is below this\",\n  "
                             + text.substring (nameAt);
                        tmp.replaceWithText (text);

                        gb::PhraseProfile trap;
                        std::string trapErr;
                        const bool loadedTrap = gb::PhraseProfile::load (
                            tmp.getFullPathName().toStdString(), trap, trapErr);

                        check (loadedTrap, "a profile quoting \"controls\" in a note loads",
                               juce::String (trapErr));

                        std::string decoyErr;
                        const bool saved = trap.save (tmp.getFullPathName().toStdString(),
                                                      decoyErr);

                        gb::PhraseProfile reread;
                        std::string rereadErr;
                        const bool ok = gb::PhraseProfile::load (
                            tmp.getFullPathName().toStdString(), reread, rereadErr);

                        check (saved && ok,
                               "and saving it finds the real block, not the note",
                               juce::String (rereadErr));
                        check (ok && tmp.loadFileAsString().contains ("60 to 89"),
                               "so the findings below that note survive the save");
                        check (ok && reread.allControls().size() == trap.allControls().size(),
                               "and the controls are still all there",
                               juce::String (static_cast<int> (reread.allControls().size()))
                                   + " of "
                                   + juce::String (static_cast<int> (trap.allControls().size())));
                    }
                }

                // Back to a clean copy for the tests below.
                src.copyFileTo (tmp);

                // ---- splicing a SCALAR must not eat the next object -------
                // findNamedBlock used to locate a value by finding the next
                // "{" after the key, which is right for every block that is an
                // object and catastrophic for one that is not. Writing
                // "highest_note": 67 sent the search past the number and into
                // the next object in the file - and the splice replaced
                // everything from the key to the end of THAT.
                //
                // It ate MODO's whole articulations block: thirty-nine lines of
                // keyswitches, controllers and the reasoning for each, none of
                // it regenerable. The file stayed valid JSON, so nothing looked
                // wrong.
                {
                    const juce::File bass ("C:/Projects/Ghostband/profiles/modo-bass-2.json");
                    const juce::File bassTmp = juce::File::getSpecialLocation (
                        juce::File::tempDirectory).getChildFile ("gb-scalar-test.json");

                    if (bass.existsAsFile() && bass.copyFileTo (bassTmp))
                    {
                        const juce::String before = bassTmp.loadFileAsString();
                        const int wasLines = juce::StringArray::fromLines (before).size();

                        std::string spliceErr;
                        const bool ok = gb::spliceProfileBlock (
                            bassTmp.getFullPathName().toStdString(), "highest_note",
                            "\"highest_note\": 71", spliceErr);

                        const juce::String after = bassTmp.loadFileAsString();
                        const int nowLines = juce::StringArray::fromLines (after).size();

                        check (ok, "a scalar splices into a profile",
                               juce::String (spliceErr));

                        check (after.contains ("\"highest_note\": 71"),
                               "and the new value is there");

                        // The block that used to get eaten.
                        check (after.contains ("\"articulations\""),
                               "and the articulations block is still present");
                        check (after.contains ("\"palm_mute\"") && after.contains ("\"slide\""),
                               "with its measured keyswitches intact");
                        check (after.contains ("keyswitch_lead_ticks"),
                               "and the settings between them too");

                        check (nowLines >= wasLines - 1,
                               "and nothing else was swallowed",
                               juce::String (wasLines) + " lines -> " + juce::String (nowLines));

                        // Still parseable as the profile it was.
                        gb::BassProfile reread;
                        std::string rereadErr;
                        const bool loaded = gb::BassProfile::load (
                            bassTmp.getFullPathName().toStdString(), reread, rereadErr);
                        check (loaded && reread.highestNote == 71,
                               "and it reads back as a bass with the new top",
                               juce::String (rereadErr));

                        bassTmp.deleteFile();
                    }
                }

                // ---- a failed save leaves the profile intact --------------
                // Saving used to open the file with trunc, destroying it before
                // knowing the write would succeed. Simulated by aiming a save
                // at a path that cannot be written.
                {
                    const juce::String before = tmp.loadFileAsString();

                    std::string failErr;
                    const bool wrote = prof.save (
                        (tmp.getFullPathName() + "/nope/deeper.json").toStdString(),
                        failErr);

                    check (! wrote, "a save to an impossible path fails",
                           juce::String (failErr));
                    check (tmp.loadFileAsString() == before,
                           "and leaves the real profile byte for byte as it was");
                }
            }
        }
        else
        {
            check (false, "profile save test could set up a scratch copy",
                   src.getFullPathName());
        }

        tmp.deleteFile();
    }

    // ---- nothing overlaps anything ------------------------------------------
    // A label keeps its bounds when a screen stops laying it out. If that screen
    // still leaves it visible, it lands on top of whatever the new screen put
    // there - text over text, both unreadable. That has now shipped three times,
    // twice caught only because the user sent a screenshot.
    //
    // Every one of these is a direct child of the editor, so the invariant is
    // simple: on any given screen, no two visible ones may overlap.
    {
        if (auto* ed = proc.createEditorIfNeeded())
        {
            auto* gbEd = dynamic_cast<GhostbandEditor*> (ed);

            // ---- ctrl-click a section, then press Roll ----------------------
            // rerollSections is asserted elsewhere and works. What was never
            // covered is the wiring from the click to that call: a selection
            // vector filled by one lambda and read by another. Reported as
            // "the button says Reroll 2 sections and clicking it does nothing".
            if (gbEd != nullptr)
            {
                proc.loadPlan (juce::File (planPath));

                const auto before = proc.getSections();
                const int  target = 2;

                gbEd->ctrlClickSectionForTesting (target);
                check (gbEd->rerollSelectionSizeForTesting() == 1,
                       "ctrl-clicking a section selects it",
                       juce::String (gbEd->rerollSelectionSizeForTesting()) + " selected");

                gbEd->pressRollForTesting();
                const auto after = proc.getSections();

                bool targetMoved = false;
                int  othersMoved = 0;
                for (size_t i = 0; i < before.size() && i < after.size(); ++i)
                {
                    const bool moved = before[i].drumHits  != after[i].drumHits
                                    || before[i].bassNotes != after[i].bassNotes;
                    if (static_cast<int> (i) == target) targetMoved = moved;
                    else if (moved)                     ++othersMoved;
                }

                check (targetMoved,
                       "and pressing Roll rerolls it through the editor's own path",
                       juce::String (before[target].drumHits) + "/"
                           + juce::String (before[target].bassNotes) + " -> "
                           + juce::String (after[target].drumHits) + "/"
                           + juce::String (after[target].bassNotes));

                check (othersMoved == 0,
                       "leaving every other section alone",
                       juce::String (othersMoved) + " others moved");

                // A section carried by a lone guitar reported "0 / 0" on screen
                // and stayed there through every reroll, because the counts
                // column only knew about drums and bass. It read as an empty
                // section and made a working reroll look dead. So: no section
                // that plays ANYTHING may total zero.
                int emptyLooking = 0;
                for (const auto& sec : after)
                {
                    const int total = sec.drumHits + sec.bassNotes + sec.guitarChords
                                    + sec.guitar2Chords + sec.pianoChords;
                    const bool silent = sec.guitarFeel == "silent"
                                     && sec.guitar2Feel == "silent"
                                     && sec.pianoFeel == "silent";
                    if (total == 0 && ! silent) ++emptyLooking;
                }

                check (emptyLooking == 0,
                       "no sounding section reports itself as empty",
                       juce::String (emptyLooking) + " sections total zero");

                // And the seed must NOT change - that is the whole-song reroll,
                // and it is what makes a section reroll look like it did
                // nothing: the seed box is the only thing on screen that moves.
                gbEd->ctrlClickSectionForTesting (target);   // deselect
                check (gbEd->rerollSelectionSizeForTesting() == 0,
                       "and ctrl-clicking again deselects it");

                proc.loadPlan (juce::File (planPath));
            }

            // What a component is, for a message that says which one to go fix.
            const auto describe = [] (juce::Component* c) -> juce::String
            {
                if (auto* l = dynamic_cast<juce::Label*> (c))
                    return "label \"" + l->getText().substring (0, 30) + "\"";
                if (auto* b = dynamic_cast<juce::TextButton*> (c))
                    return "button \"" + b->getButtonText() + "\"";
                if (dynamic_cast<juce::ComboBox*> (c))    return "a combo box";
                if (dynamic_cast<juce::TextEditor*> (c))  return "a text box";
                if (dynamic_cast<juce::Slider*> (c))      return "a slider";
                if (dynamic_cast<juce::Viewport*> (c))    return "a viewport";
                return "a component";
            };

            for (int screen = 0; screen < GhostbandEditor::numScreens; ++screen)
            {
                if (gbEd != nullptr) gbEd->showScreenForSnapshot (screen);
                ed->setSize (600, 720);

                std::vector<juce::Component*> shown;
                for (int i = 0; i < ed->getNumChildComponents(); ++i)
                {
                    juce::Component* c = ed->getChildComponent (i);
                    if (c != nullptr && c->isVisible() && ! c->getBounds().isEmpty())
                        shown.push_back (c);
                }

                juce::StringArray collisions;
                for (size_t a = 0; a < shown.size(); ++a)
                    for (size_t b = a + 1; b < shown.size(); ++b)
                    {
                        const auto ra = shown[a]->getBounds();
                        const auto rb = shown[b]->getBounds();
                        if (! ra.intersects (rb))
                            continue;

                        // A one-pixel touch from adjacent rows is not a collision;
                        // real bleed-through overlaps by a readable amount.
                        const auto hit = ra.getIntersection (rb);
                        if (hit.getWidth() < 6 || hit.getHeight() < 6)
                            continue;

                        collisions.add (describe (shown[a]) + "  over  " + describe (shown[b]));
                    }

                check (collisions.isEmpty(),
                       juce::String ("nothing overlaps on the ")
                           + GhostbandEditor::screenName (screen) + " screen",
                       collisions.joinIntoString ("; "));
            }

            proc.editorBeingDeleted (ed);
            delete ed;
        }
    }

    // ---- a level control actually leaves the plugin --------------------------
    // The status line reports what sendLevels decided to send, which is not the
    // same claim as "it came out of processBlock". A mix knob that moves the
    // right controller in the report and nothing in the room is exactly the
    // gap between those two, so this checks the wire rather than the intent.
    {
        const int bass = 1;

        // Give the bass a level control the way the profile does.
        while (proc.getControlCount (bass) > 0)
            proc.removeControl (bass, 0);

        proc.addControl (bass);
        auto slot = proc.getControl (bass, 0);
        slot.name    = "volume";
        slot.follows = "level";
        slot.low     = 0.0;
        slot.high    = 1.0;
        proc.updateControl (bass, 0, slot);

        const int cc = proc.getControl (bass, 0).cc;
        check (cc >= 0, "the bass level control has a CC", juce::String (cc));
        check (proc.levelIsTaught (bass), "the bass counts as having a taught level");

        proc.levelBass.store (0.25f);
        proc.sendLevels();

        // Walk a few blocks and watch what actually comes out.
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer out;
        int seen = -1;

        for (int i = 0; i < 8 && seen < 0; ++i)
        {
            out.clear();
            proc.processBlock (buf, out);
            for (const auto meta : out)
            {
                const auto m = meta.getMessage();
                if (m.isController() && m.getControllerNumber() == cc
                    && m.getChannel() == 1)
                    seen = m.getControllerValue();
            }
        }

        check (seen >= 0, "the level controller reaches the plugin's MIDI output",
               seen < 0 ? juce::String ("never seen on CC ") + juce::String (cc)
                        : juce::String (seen));

        check (seen > 25 && seen < 40, "and carries the knob's position",
               juce::String (seen) + " for a knob at 0.25");

        while (proc.getControlCount (bass) > 0)
            proc.removeControl (bass, 0);
    }

    // ---- one mix knob moves one control -------------------------------------
    // Shreddage arrived with volume, bite and the pickup selector all following
    // "level", and the GTR 2 mix knob moved all three at once: turn it up and
    // the guitar got louder, brighter and changed pickup together. Take away
    // bite and it moved volume and the pickup instead, which is what a fader
    // wired to a bus rather than to a channel does.
    //
    // Nothing about that was visible in the mapping list, because each row on
    // its own was a perfectly reasonable mapping. Only the set was wrong.
    {
        const int bass = 1;

        while (proc.getControlCount (bass) > 0)
            proc.removeControl (bass, 0);

        const auto followLevel = [&proc, bass] (int index, const char* name)
        {
            proc.addControl (bass);
            auto s = proc.getControl (bass, index);
            s.name    = name;
            s.follows = "level";
            s.low     = 0.0;
            s.high    = 1.0;
            proc.updateControl (bass, index, s);
        };

        followLevel (0, "volume");
        followLevel (1, "bite");
        followLevel (2, "signal");

        int following = 0;
        for (int i = 0; i < proc.getControlCount (bass); ++i)
            if (proc.getControl (bass, i).follows == "level")
                ++following;

        check (following == 1, "only one control can follow the mix knob",
               juce::String (following) + " of 3 follow \"level\"");

        // And the one that follows it is the one chosen last, not the one that
        // happened to be first in the list - refusing the owner's most recent
        // choice would be its own kind of wrong.
        check (proc.getControl (bass, 2).follows == "level",
               "the control just set to \"level\" is the one that keeps it",
               proc.getControl (bass, 2).follows);

        // The wire, not the list. Exactly one of these three controllers may
        // carry the knob - the arrangement drives others on this channel, and
        // counting every controller on it would fail on a healthy plugin.
        std::set<int> mine;
        for (int i = 0; i < 3; ++i)
            mine.insert (proc.getControl (bass, i).cc);

        // Drain first. Every control edit re-sends the levels, so by now the
        // queue holds a message from each intermediate state this test built on
        // the way here - which is correct behaviour and looks exactly like the
        // fault being tested for.
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer out;

        for (int i = 0; i < 8; ++i) { out.clear(); proc.processBlock (buf, out); }

        proc.levelBass.store (0.75f);
        proc.sendLevels();

        std::set<int> moved;
        std::map<int, juce::String> values;

        for (int i = 0; i < 8; ++i)
        {
            out.clear();
            proc.processBlock (buf, out);
            for (const auto meta : out)
            {
                const auto m = meta.getMessage();
                if (m.isController() && m.getChannel() == 1
                    && mine.count (m.getControllerNumber()) > 0)
                {
                    moved.insert (m.getControllerNumber());
                    values[m.getControllerNumber()]
                        += juce::String (m.getControllerValue()) + ",";
                }
            }
        }

        juce::String which;
        for (int cc : moved) which += juce::String (cc) + "=" + values[cc] + " ";

        check (moved.size() == 1, "and one mix knob writes one controller",
               juce::String ((int) moved.size()) + " of its 3 moved: " + which
                   + " (level is on CC "
                   + juce::String (proc.getControl (bass, 2).cc) + ")");

        while (proc.getControlCount (bass) > 0)
            proc.removeControl (bass, 0);
    }

    // ---- a profile's intent outlives the taught store ------------------------
    // The store remembers which knob sits on which CC, because seven profile
    // files describe one SSD5 and teaching it once should be enough. It used to
    // remember far more than that - it replaced the profile's whole controls
    // block - so "follows", the range, and the type came back from a cache
    // instead of from the file.
    //
    // That is not a cosmetic difference. An edit to a profile was reverted on
    // load and then written back over the file on the next save, so a fix
    // committed to git could be undone by opening the plugin. It cost a real
    // fix to Shreddage's tone controls, which is why this is pinned.
    {
        const juce::File plan ("C:/Projects/Ghostband/plans/preset-thrash.json");

        if (plan.existsAsFile())
        {
            proc.loadPlan (plan);

            const int gtr2 = 4;
            const int count = proc.getControlCount (gtr2);

            // The profile says its three tone controls follow "lead": up and
            // brighter when this guitar is out front. Only its CC numbers are
            // the store's business.
            int levels = 0, leads = 0;
            for (int i = 0; i < count; ++i)
            {
                const auto c = proc.getControl (gtr2, i);
                if (c.follows == "level") ++levels;
                if (c.follows == "lead")  ++leads;
            }

            check (count == 0 || levels <= 1,
                   "a loaded profile never brings more than one \"level\" control",
                   juce::String (levels) + " of " + juce::String (count));

            check (count == 0 || leads > 0,
                   "the profile's own \"follows\" survives the taught store",
                   juce::String (leads) + " control(s) still follow \"lead\"");

            // And the knob reaches something. A part whose profile declares
            // controls but none following "level" has a mix knob that falls
            // back to CC 7 - which Kontakt did not answer, so the GTR 2 knob
            // moved nothing while looking exactly like one that worked.
            // NOT an assertion that a level control exists. Whether one does
            // is the owner's decision, made in the Settings screen and saved
            // into a file they edit - this test used to fail simply because
            // they parked the volume instead, which is a legitimate choice and
            // not a fault in anything.
            //
            // What IS the plugin's business is that the knob and the profile
            // agree, so that is what is checked.
            const bool taught = proc.levelIsTaught (gtr2);

            check (taught == (levels == 1),
                   "the mix knob agrees with the profile about being taught",
                   juce::String (levels) + " level control(s), plugin says "
                       + (taught ? "taught" : "not taught"));

            // On the wire, on Shreddage's own channel. Twice now a mix knob has
            // been declared fixed on the strength of the code reading right.
            juce::AudioBuffer<float> buf (2, blockSize);
            juce::MidiBuffer out;

            for (int i = 0; i < 8; ++i) { out.clear(); proc.processBlock (buf, out); }

            proc.levelGuitar2.store (0.60f);
            proc.sendLevels();

            int cc = -1, value = -1, channel = -1;
            for (int i = 0; i < 8 && cc < 0; ++i)
            {
                out.clear();
                proc.processBlock (buf, out);
                for (const auto meta : out)
                {
                    const auto m = meta.getMessage();
                    if (m.isController() && m.getChannel() == proc.channelGuitar2.load())
                    {
                        cc = m.getControllerNumber();
                        value = m.getControllerValue();
                        channel = m.getChannel();
                    }
                }
            }

            // A taught level goes out on its own controller; an untaught one
            // falls back to CC 7. Either is correct - sending nothing at all,
            // or sending both, is not.
            check (cc > 0 && (taught ? cc != 7 : cc == 7),
                   "the mix knob leaves the plugin on the controller it claims",
                   "CC" + juce::String (cc) + " ch" + juce::String (channel)
                       + "=" + juce::String (value)
                       + (taught ? " (taught)" : " (fallback)"));

            // And CC 7 must not be going there as well.
            //
            // This is the best account of a whole evening lost. When Shreddage
            // had no control following "level", the mix knob fell back to CC 7
            // on channel 11 - and Kontakt DOES answer CC 7 as instrument
            // volume. Any knob position below full therefore turned Shreddage
            // down, the position was saved into the host session, and the solo
            // section came back sounding like only the rhythm guitar was
            // playing it. The fallback was written on the reasoning that it
            // "costs nothing and is right for anything that does respond",
            // which is exactly half true: it is not free when it lands on an
            // instrument that responds and nobody expects it to.
            bool sawCC7 = false;
            for (int i = 0; i < 8; ++i)
            {
                out.clear();
                proc.processBlock (buf, out);
                for (const auto meta : out)
                {
                    const auto m = meta.getMessage();
                    if (m.isController() && m.getControllerNumber() == 7
                        && m.getChannel() == proc.channelGuitar2.load())
                        sawCC7 = true;
                }
            }

            check (! taught || ! sawCC7,
                   "and no CC 7 follows it onto the same channel",
                   sawCC7 ? "CC 7 still sent - it can mute a Kontakt instrument"
                          : "none");

            // The same trap, one part over. SSD5 declares that its volume
            // cannot be reached, so its mix knob is not drawn at all - and the
            // CC 7 fallback went on being sent to channel 10 anyway, carrying a
            // level nobody could see or correct. A hidden knob that still
            // transmits is worse than either a working one or none.
            const int chDrums = proc.channelDrums.load();
            proc.levelDrums.store (0.20f);
            proc.sendLevels();

            bool drumsCC7 = false;
            for (int i = 0; i < 8; ++i)
            {
                out.clear();
                proc.processBlock (buf, out);
                for (const auto meta : out)
                {
                    const auto m = meta.getMessage();
                    if (m.isController() && m.getControllerNumber() == 7
                        && m.getChannel() == chDrums)
                        drumsCC7 = true;
                }
            }

            check (proc.partVolumeReachable (0) || ! drumsCC7,
                   "a part with no reachable volume transmits nothing at all",
                   drumsCC7 ? "CC 7 still going to ch" + juce::String (chDrums)
                            : "silent, as its hidden knob implies");

            proc.levelDrums.store (1.0f);

            // ---- where the two guitars actually end up ----------------------
            // The CLI resolves channels straight from the profiles. The PLUGIN
            // does not: it layers a saved slot channel, a per-instrument learned
            // channel and a collision resolver on top, and none of that is
            // exercised by rendering a plan from the command line. "The solo is
            // playing on IRON 2" is a claim about this path, so this is the
            // path that has to be measured.
            const int chGtr  = proc.channelGuitar.load();
            const int chGtr2 = proc.channelGuitar2.load();

            check (chGtr != chGtr2,
                   "the two guitars are on different channels",
                   "guitar ch" + juce::String (chGtr)
                       + ", guitar 2 ch" + juce::String (chGtr2));

            const int onGtr  = proc.getSequenceNoteOnCount (chGtr);
            const int onGtr2 = proc.getSequenceNoteOnCount (chGtr2);

            check (onGtr2 > 0,
                   "the second guitar is actually sent notes",
                   juce::String (onGtr2) + " note-ons on ch" + juce::String (chGtr2)
                       + " (rhythm guitar has " + juce::String (onGtr)
                       + " on ch" + juce::String (chGtr) + ")");

            check (chGtr2 == 11,
                   "and on the channel Shreddage's profile asks for",
                   "ch" + juce::String (chGtr2));
        }
    }

    // ---- the rig survives a restart -----------------------------------------
    // The mix and the channel assignments are part of how a rig is set up. They
    // were not in the saved state at all, so every knob sprang back to full and
    // every channel to its default whenever the host was restarted.
    {
        proc.levelDrums.store  (0.30f);
        proc.levelBass.store   (0.55f);
        proc.levelGuitar.store (0.80f);
        proc.levelPiano.store  (0.20f);
        // Through the real path, because that is what remembers a channel now -
        // it belongs to the instrument, not to the slot or to the saved state.
        proc.channelGuitar.store (7);
        proc.applyChannels();

        juce::MemoryBlock saved;
        proc.getStateInformation (saved);

        // Move everything somewhere else, then restore.
        proc.levelDrums.store  (1.0f);
        proc.levelBass.store   (1.0f);
        proc.levelGuitar.store (1.0f);
        proc.levelPiano.store  (1.0f);
        proc.channelGuitar.store (2);

        proc.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

        const auto near = [] (float a, float b) { return std::abs (a - b) < 0.02f; };

        check (near (proc.levelDrums.load(),  0.30f)
                   && near (proc.levelBass.load(),   0.55f)
                   && near (proc.levelGuitar.load(), 0.80f)
                   && near (proc.levelPiano.load(),  0.20f),
               "the mix survives a restart",
               juce::String (proc.levelDrums.load(), 2) + " "
                   + juce::String (proc.levelBass.load(), 2) + " "
                   + juce::String (proc.levelGuitar.load(), 2) + " "
                   + juce::String (proc.levelPiano.load(), 2));

        check (proc.channelGuitar.load() == 7, "and so do the channel assignments",
               juce::String (proc.channelGuitar.load()));

        proc.levelDrums.store  (1.0f);
        proc.levelBass.store   (1.0f);
        proc.levelGuitar.store (1.0f);
        proc.levelPiano.store  (1.0f);
        proc.channelGuitar.store (2);
    }

    // ---- editor snapshots --------------------------------------------------
    // A layout bug is invisible to every check above. Rendering the editor to a
    // PNG makes the one thing these tests cannot assert - what it actually looks
    // like - reviewable without installing the plugin in a host.
    {
        const juce::String snapshotDir = [&]
        {
            for (int i = 1; i < argc - 1; ++i)
                if (juce::String (argv[i]) == "--snapshot") return juce::String (argv[i + 1]);
            return juce::String();
        }();

        if (snapshotDir.isNotEmpty())
        {
            proc.loadPlan (juce::File (planPath).getSiblingFile ("demo-band.json"));

            // Populate the guitar's mapping list so the Settings shot shows the
            // table doing its job. An empty list cannot reveal a layout bug in
            // the rows, and the rows are the part that keeps growing.
            {
                const int guitar = 2;

                // Clear first so the selector ends up as row zero and is the
                // one selected in the shot. The choices box only appears for a
                // selector, and a screen nobody renders is where layout bugs
                // live - two have shipped that way already.
                while (proc.getControlCount (guitar) > 0)
                    proc.removeControl (guitar, 0);

                const int before  = proc.getControlCount (guitar);
                const char* names[] = { "amp model", "latch",  "presence" };
                const char* types[] = { "select",    "switch", "knob"  };
                // Driven, so the shot renders the choices box and the range
                // pair together - the tightest those rows ever get.
                const char* folls[] = { "random once", "none",  "intensity" };
                int firstAdded = -1;

                for (int i = 0; i < 3; ++i)
                {
                    proc.addControl (guitar);
                    const int idx = proc.getControlCount (guitar) - 1;
                    if (idx < 0) break;
                    if (firstAdded < 0) firstAdded = idx;

                    auto slot = proc.getControl (guitar, idx);
                    slot.name      = names[i];
                    slot.type      = types[i];
                    slot.follows   = folls[i];
                    slot.positions = 30;   // a real stompbox list, not a tidy number
                    proc.updateControl (guitar, idx, slot);
                }

                check (proc.getControlCount (guitar) == before + 3,
                       "controls can be added, named and retyped through the plugin",
                       juce::String (proc.getControlCount (guitar)) + " mapped");

                const auto sel = proc.getControl (guitar, firstAdded);
                check (sel.type == "select" && sel.positions == 30
                           && sel.name == "amp model",
                       "a selector keeps its name and choice count",
                       sel.name + " / " + sel.type + " / " + juce::String (sel.positions));

                // Distinct CCs matter more than which ones: two controls on one
                // CC would move together whatever they are named.
                std::set<int> ccs;
                bool allDistinct = true;
                for (int i = 0; i < proc.getControlCount (guitar); ++i)
                    if (! ccs.insert (proc.getControl (guitar, i).cc).second)
                        allDistinct = false;
                check (allDistinct, "no two controls share a CC");
            }

            if (auto* ed = proc.createEditorIfNeeded())
            {
                const juce::File dir (snapshotDir);
                dir.createDirectory();

                auto* gbEd = dynamic_cast<GhostbandEditor*> (ed);

                struct Shot { int screen; int w, h; const char* suffix; };
                std::vector<Shot> shots;
                for (int s = 0; s < GhostbandEditor::numScreens; ++s)
                    shots.push_back ({ s, 600, 720, "" });
                shots.push_back ({ 0, 900, 640, "-wide" });   // song, resized wide

                // The set that ships in docs/screenshots and is linked from the
                // README. Rendered rather than captured by hand, so they can be
                // regenerated the moment the UI changes instead of slowly going
                // stale - a screenshot of a version nobody runs any more is
                // worse than none. Sized to match the window the plugin is
                // actually used at.
                // Sized per screen rather than uniformly: a settings page with a
                // scrolling control list needs the height, and the song screen
                // at that height is mostly empty floor.
                shots.push_back ({ 0, 1000,  900, "-docs" });   // song
                shots.push_back ({ 1, 1000, 1280, "-docs" });   // calibrate
                shots.push_back ({ 2, 1000,  980, "-docs" });   // edit
                shots.push_back ({ 3, 1000, 1320, "-docs" });   // settings
                shots.push_back ({ 4, 1000,  760, "-docs" });   // about

                // About at the size it is actually used at. It is the one
                // screen painted straight onto the canvas rather than built
                // from child components, so the overlap checker is blind to it
                // - and it has now shipped broken twice. A big window is where
                // it broke both times.
                shots.push_back ({ 4, 1020, 1400, "-wide" });

                for (const Shot& shot : shots)
                {
                    if (gbEd != nullptr) gbEd->showScreenForSnapshot (shot.screen);

                    ed->setSize (shot.w, shot.h);
                    const juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds(), true);

                    const juce::String label = juce::String (GhostbandEditor::screenName (shot.screen))
                                             + juce::String (shot.suffix);
                    const juce::File out = dir.getChildFile ("editor-" + label + ".png");
                    out.deleteFile();
                    juce::FileOutputStream stream (out);
                    if (stream.openedOk())
                    {
                        juce::PNGImageFormat png;
                        png.writeImageToStream (img, stream);
                        std::cout << "  snapshot: " << out.getFullPathName() << "\n";
                    }
                }

                proc.editorBeingDeleted (ed);
                delete ed;
            }
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : "FAILURES: " + juce::String (failures))
              << "\n\n";
    return failures == 0 ? 0 : 1;
}
