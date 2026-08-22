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
        const int expectedDrums = juce::String (argv[2]).getIntValue();
        const int expectedBass  = juce::String (argv[3]).getIntValue();

        check (reloaded.drumHits == expectedDrums && reloaded.bassNotes == expectedBass,
               "plugin output matches the CLI for the same plan and seed",
               juce::String (reloaded.drumHits) + "/" + juce::String (reloaded.bassNotes)
                   + " vs CLI " + juce::String (expectedDrums) + "/" + juce::String (expectedBass));
    }

    std::cout << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : "FAILURES: " + juce::String (failures))
              << "\n\n";
    return failures == 0 ? 0 : 1;
}
