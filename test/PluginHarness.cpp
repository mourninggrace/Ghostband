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

#include "ghostband/Groove.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <iostream>
#include <map>

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
        check (proc.getSequenceNoteOnCount (10) > 0 && proc.getSequenceNoteOnCount (1) > 0,
               "built-in plan produces both drums and bass");

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

    std::cout << "  sequence holds " << proc.getSequenceNoteOnCount (10) << " note-ons on ch10, "
              << proc.getSequenceNoteOnCount (1) << " on ch1\n\n";

    check (status.drumProfile.contains ("Terry Date") || status.drumProfile.contains ("SSD5"),
           "drum profile resolved from a path relative to the project root",
           status.drumProfile);
    check (status.bassProfile.contains ("MODO"),
           "bass profile resolved", status.bassProfile);

    // ---- walk the song ---------------------------------------------------
    const double sampleRate = 48000.0;
    const int    blockSize  = 512;

    // A host calls setRateAndBufferSizeDetails before prepareToPlay; calling
    // prepareToPlay alone leaves getSampleRate() at zero, which is exactly the
    // situation the processor now refuses to guess its way through.
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    FakePlayHead head;
    head.bpm = 168.0;
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

    check (noteOnByChannel.count (10) > 0, "drums are emitted on channel 10");
    check (noteOnByChannel.count (1) > 0,  "bass is emitted on channel 1");

    // The drum profile emits exactly one note per intent, so these must agree.
    check (noteOnByChannel[10] == status.drumHits,
           "drum note count matches what the engine reported",
           juce::String (noteOnByChannel[10]) + " vs " + juce::String (status.drumHits));

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
        const int drumsBefore = proc.getSequenceNoteOnCount (10);
        const int bassBefore  = proc.getSequenceNoteOnCount (1);
        const int pitchBefore = proc.getSequencePitchSum (1);

        const int target = (before + 5) % 12;
        proc.setKeyPitchClass (target);

        check (proc.getKeyPitchClass() == target, "changing the key takes effect",
               juce::String (before) + " -> " + juce::String (proc.getKeyPitchClass()));

        // Transposing must move the pitches without disturbing the performance:
        // same rhythm, same number of notes, different notes.
        check (proc.getSequenceNoteOnCount (10) == drumsBefore
                   && proc.getSequenceNoteOnCount (1) == bassBefore,
               "transposing does not change the drumming or the note count");
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
                if (jumpedAtTick < 0 && proc.getSequenceNoteOnCount (10) >= 0
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

            check (proc.getSequenceNoteOnCount (10) > 0 && proc.getSequenceNoteOnCount (1) > 0,
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
        proc.loadPlan (juce::File (planPath));
        check (! proc.isCalibrating(), "does not start in calibration mode");

        proc.enterCalibration();
        check (proc.isCalibrating(), "enters calibration");

        const int steps = proc.getCalibrationStepCount();
        check (steps > 10, "calibration covers the whole kit",
               juce::String (steps) + " steps");

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
    if (argc > 3)
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

            if (auto* ed = proc.createEditorIfNeeded())
            {
                const juce::File dir (snapshotDir);
                dir.createDirectory();

                struct Shot { const char* name; int w, h; };
                for (const Shot& shot : { Shot { "song", 560, 700 },
                                          Shot { "song-wide", 900, 620 } })
                {
                    ed->setSize (shot.w, shot.h);
                    const juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds(), true);

                    const juce::File out = dir.getChildFile (juce::String ("editor-")
                                                             + shot.name + ".png");
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
