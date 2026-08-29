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
        // Loaded with the band plan, so calibration is exercised with all four
        // instruments present rather than just drums and bass.
        const juce::File band = juce::File (planPath).getSiblingFile ("demo-band.json");
        proc.loadPlan (band.existsAsFile() ? band : juce::File (planPath));
        check (! proc.isCalibrating(), "does not start in calibration mode");

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
            }
        }
        else
        {
            check (false, "profile save test could set up a scratch copy",
                   src.getFullPathName());
        }

        tmp.deleteFile();
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
                // Parked, so the shot renders both the choices box and the
                // value box - the tightest the row ever gets.
                const char* folls[] = { "fixed",     "none",   "intensity" };
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

                struct Shot { int screen; int w, h; };
                std::vector<Shot> shots;
                for (int s = 0; s < GhostbandEditor::numScreens; ++s)
                    shots.push_back ({ s, 600, 720 });
                shots.push_back ({ 0, 900, 640 });   // song, resized wide

                for (const Shot& shot : shots)
                {
                    if (gbEd != nullptr) gbEd->showScreenForSnapshot (shot.screen);

                    ed->setSize (shot.w, shot.h);
                    const juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds(), true);

                    const juce::String label = juce::String (GhostbandEditor::screenName (shot.screen))
                                             + (shot.w > 700 ? "-wide" : "");
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
