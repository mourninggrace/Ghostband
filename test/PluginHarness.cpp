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
#include "ghostband/Json.h"
#include "ghostband/Planner.h"
#include "ghostband/Render.h"
#include "ghostband/SongPlan.h"

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
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

// Contrast ratio as the W3C defines it: (L1 + 0.05) / (L2 + 0.05) on relative
// luminance. 4.5 is their threshold for body text, 3.0 for large.
//
// A free function rather than a lambda inside one test, because the second
// caller - the drop-down menus, which are separate windows and take their
// colours from the LookAndFeel - is nowhere near the first.
double contrastRatio (juce::Colour a, juce::Colour b)
{
    const auto luminance = [] (juce::Colour c)
    {
        const auto channel = [] (double v)
        {
            v /= 255.0;
            return v <= 0.03928 ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4);
        };

        return 0.2126 * channel (c.getRed())
             + 0.7152 * channel (c.getGreen())
             + 0.0722 * channel (c.getBlue());
    };

    const double la = luminance (a), lb = luminance (b);
    return (juce::jmax (la, lb) + 0.05) / (juce::jmin (la, lb) + 0.05);
}

// The window's own minimum. Layout checks run here because below it the layout
// is allowed to run out of room - and because the minimum is the size at which
// a layout fault is most likely and least often looked at.
//
// It moved from 700x820 to 1020x820 with the rail and the two-column Settings.
// Settings is what sets it: a 460 column of channels beside a 496 column of the
// learn form. The song screen alone would be happy at 900.
constexpr int kMinW = 1020;
constexpr int kMinH = 820;

// Same idea as the local `describe` lambdas, at file scope so checks outside
// those blocks can name a component too.
inline juce::String describeComponent (juce::Component* c)
{
    if (auto* l = dynamic_cast<juce::Label*> (c))
        return "label \"" + l->getText().substring (0, 30) + "\"";
    if (auto* b = dynamic_cast<juce::TextButton*> (c))
        return "button \"" + b->getButtonText() + "\"";
    if (dynamic_cast<juce::ComboBox*> (c))    return "a combo box";
    if (dynamic_cast<juce::TextEditor*> (c))  return "a text box";
    if (dynamic_cast<juce::Slider*> (c))      return "a slider";
    return "a component";
}

int failures = 0;

void check (bool condition, const juce::String& what, const juce::String& detail = {})
{
    std::cout << (condition ? "  PASS  " : "  FAIL  ") << what;
    if (detail.isNotEmpty()) std::cout << "   (" << detail << ")";
    std::cout << "\n";
    if (! condition) ++failures;
}

// Dumps what each screen actually lays out, so a control that is present in the
// source and absent on screen can be seen rather than reasoned about. Run with
// "--audit [plan]". Not a test: a look.
void layoutAudit (GhostbandProcessor& proc, const juce::String& planPath,
                  int wantW = 0, int wantH = 0)
{
    if (planPath.isNotEmpty())
        proc.loadPlan (juce::File (planPath));

    auto* ed = proc.createEditorIfNeeded();
    if (ed == nullptr) { std::cout << "no editor\n"; return; }

    auto* gbEd = dynamic_cast<GhostbandEditor*> (ed);
    ed->setSize (wantW > 0 ? wantW : proc.editorWidth.load(),
                 wantH > 0 ? wantH : proc.editorHeight.load());

    const auto describe = [] (juce::Component* c) -> juce::String
    {
        if (auto* l = dynamic_cast<juce::Label*> (c))
            return "label \"" + l->getText().substring (0, 34) + "\"";
        if (auto* b = dynamic_cast<juce::TextButton*> (c))
            return "button \"" + b->getButtonText() + "\"";
        if (dynamic_cast<juce::ComboBox*> (c))    return "combo";
        if (dynamic_cast<juce::TextEditor*> (c))  return "text box";
        if (dynamic_cast<juce::Slider*> (c))      return "slider";
        if (dynamic_cast<juce::Viewport*> (c))    return "viewport";
        return "component";
    };

    std::cout << "window " << ed->getWidth() << "x" << ed->getHeight() << "\n";

    for (int screen = 0; screen < GhostbandEditor::numScreens; ++screen)
    {
        if (gbEd != nullptr) gbEd->showScreenForSnapshot (screen);
        ed->resized();

        std::cout << "\n--- " << GhostbandEditor::screenName (screen) << " ---\n";

        int hiddenCount = 0, bottom = 0, right = 0;
        for (int i = 0; i < ed->getNumChildComponents(); ++i)
        {
            juce::Component* c = ed->getChildComponent (i);
            if (c == nullptr) continue;

            const auto b = c->getBounds();
            if (! c->isVisible()) { ++hiddenCount; continue; }

            bottom = juce::jmax (bottom, b.getBottom());
            right  = juce::jmax (right,  b.getRight());

            std::cout << "  " << juce::String (b.getX()).paddedLeft (' ', 5)
                      << juce::String (b.getY()).paddedLeft (' ', 5)
                      << juce::String (b.getWidth()).paddedLeft (' ', 6)
                      << juce::String (b.getHeight()).paddedLeft (' ', 5)
                      << "  " << describe (c) << "\n";
        }
        std::cout << "  [" << hiddenCount << " hidden; content reaches "
                  << right << "x" << bottom << "]\n";
    }

    proc.editorBeingDeleted (ed);
    delete ed;
}

// Times what the message thread actually does, so "the window froze for a
// while" can be attributed rather than guessed at. Run with "--timing [plan]".
// Not a test: a stopwatch.
void timingAudit (GhostbandProcessor& proc, const juce::String& planPath)
{
    if (planPath.isNotEmpty())
        proc.loadPlan (juce::File (planPath));

    const auto ms = [] (std::function<void()> f, int reps)
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < reps; ++i) f();
        return (juce::Time::getMillisecondCounterHiRes() - t0) / reps;
    };

    const int beat = proc.getBeatTicks();
    const int bar  = proc.getBarTicks();
    const std::vector<int> channels {
        proc.channelDrums.load(), proc.channelBass.load(), proc.channelGuitar.load(),
        proc.channelGuitar2.load(), proc.channelPiano.load() };

    std::cout << "\nwhat the message thread does, per call, in ms\n";
    std::cout << "  (the editor's timer runs 30 times a second, so anything\n"
                 "   above ~5 ms here is a visible cost and above ~33 ms is a\n"
                 "   timer that cannot keep up)\n\n";

    const auto line = [] (const char* what, double v)
    {
        std::cout << "  " << juce::String (what).paddedRight (' ', 34)
                  << juce::String (v, 3).paddedLeft (' ', 9) << " ms"
                  << (v > 33.0 ? "   <-- CANNOT KEEP UP"
                               : v > 5.0 ? "   <-- visible" : "")
                  << "\n";
    };

    line ("regenerate (a reroll)",      ms ([&] { proc.regenerate(); }, 10));
    line ("getSections (copies)",       ms ([&] { auto s = proc.getSections(); (void) s; }, 200));
    line ("getBeatTicks",               ms ([&] { (void) proc.getBeatTicks(); }, 2000));
    line ("getStatus",                  ms ([&] { auto s = proc.getStatus(); (void) s; }, 200));

    const char* names[4] = { "getTrackerCells  bar", "getTrackerCells  beat",
                             "getTrackerCells  8th", "getTrackerCells  16th" };
    const int perRow[4]  = { bar, beat, juce::jmax (1, beat / 2), juce::jmax (1, beat / 4) };
    for (int z = 0; z < 4; ++z)
    {
        const int rows = 48;
        line (names[z], ms ([&] { auto c = proc.getTrackerCells (0, rows, channels, perRow[z]);
                                  (void) c; }, 200));
    }

    // The one that actually paints. Text rendering is the expensive part of a
    // tracker and it scales with the row count, so the finest zoom is the one
    // worth knowing about.
    if (auto* ed = proc.createEditorIfNeeded())
    {
        auto* gbEd = dynamic_cast<GhostbandEditor*> (ed);
        if (gbEd != nullptr) gbEd->showScreenForSnapshot (0);
        ed->setSize (kMinW, kMinH);

        juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);

        // WARM UP FIRST. The first paint of a session caches glyphs, builds the
        // image's graphics context and touches every colour lookup once, and
        // whichever measurement happens to go first absorbs all of it. That put
        // three milliseconds on "bar" purely for being measured first, which
        // reads as the default zoom being the expensive one when it is the
        // cheapest - a quarter as many rows as a beat.
        for (int i = 0; i < 5; ++i)
        {
            juce::Graphics g (img);
            ed->paintEntireComponent (g, true);
        }

        for (int z = 0; z < 4; ++z)
        {
            proc.trackerZoom.store (z);
            static const char* pn[4] = { "full repaint  bar", "full repaint  beat",
                                         "full repaint  8th", "full repaint  16th" };
            line (pn[z], ms ([&]
            {
                juce::Graphics g (img);
                ed->paintEntireComponent (g, true);
            }, 20));
        }
        proc.trackerZoom.store (0);

        proc.editorBeingDeleted (ed);
        delete ed;
    }

    std::cout << "\n";
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

    // AND THE STALL LOG, FOR THE SAME REASON AND IT HAD THE SAME FAULT.
    //
    // The stall checks redirected this file around themselves and then reset it
    // to {} - which pointed it back at the owner's real one for the whole rest
    // of the run. Every editor this harness builds afterwards ticks a timer,
    // and any sleep longer than the stall threshold wrote a line into
    // %APPDATA%\Ghostband\stalls.log.
    //
    // Found by accident, while reading that very file to work out why a fix to
    // the detector had not worked: fourteen lines in it shared not one line
    // with the log the plugin had actually produced, and Gig Performer was not
    // even running. Some of what I had been reasoning about was my own test
    // suite.
    //
    // Redirected for the WHOLE RUN. The stall block below still points it at a
    // file of its own, and now puts it back here rather than at nothing.
    const juce::File harnessStallLog =
        juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("ghostband-harness")
            .getChildFile ("stalls-harness.log");

    harnessStallLog.deleteFile();
    GhostbandProcessor::setStallLogFileForTesting (harnessStallLog);

    // And the change log, for exactly the same reason. A test run must not
    // append a few hundred lines of invented history to the owner's record of
    // what he actually changed - a log nobody can trust is worse than no log.
    const juce::File testChangeLog = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                         .getChildFile ("ghostband-harness")
                                         .getChildFile ("changes.log");
    testChangeLog.deleteFile();
    GhostbandProcessor::setChangeLogFileForTesting (testChangeLog);

    GhostbandProcessor proc;

    if (argc > 1 && juce::String (argv[1]) == "--audit")
    {
        layoutAudit (proc, argc > 2 ? juce::String (argv[2]) : juce::String(),
                     argc > 3 ? juce::String (argv[3]).getIntValue() : 0,
                     argc > 4 ? juce::String (argv[4]).getIntValue() : 0);
        return 0;
    }

    if (argc > 1 && juce::String (argv[1]) == "--controls")
    {
        // What a rig is ACTUALLY set to, which is neither what the profile says
        // nor what the learned store says: the store supplies CC numbers and
        // the profile supplies everything else. Reading either file alone gives
        // the wrong answer, and reading the merge code gave me the wrong answer
        // too - so it gets printed.
        //
        //   --controls [plan] [learned-controls.json]
        //
        // The third argument points at a real store, so a particular machine's
        // rig can be resolved rather than guessed at.
        const juce::String planArg = argc > 2 ? juce::String (argv[2]) : juce::String();

        const auto dump = [] (const GhostbandProcessor& p2)
        {
            static const char* pn[5] = { "drums", "bass", "guitar", "piano", "guitar 2" };

            for (int part = 0; part < 5; ++part)
            {
                std::cout << "  " << pn[part] << "\n";
                const gb::ControlSet* set = p2.controlSetForTesting (part);
                if (set == nullptr) { std::cout << "      (not in this song)\n"; continue; }
                for (const gb::ControlDef& c : set->all())
                    std::cout << "      cc" << juce::String (c.cc).paddedLeft (' ', 3)
                              << "  " << juce::String (c.name).paddedRight (' ', 22)
                              << "follows " << c.follows << "\n";
            }
        };

        if (argc > 3)
        {
            GhostbandProcessor::setLearnedControlsFileForTesting (juce::File (juce::String (argv[3])));
            GhostbandProcessor fresh;
            if (planArg.isNotEmpty()) fresh.loadPlan (juce::File (planArg));

            std::cout << "\nstore: " << argv[3] << "\n\n";
            dump (fresh);
            std::cout << "\n";
            return 0;
        }

        if (planArg.isNotEmpty()) proc.loadPlan (juce::File (planArg));
        std::cout << "\nno store - the shipped profiles alone\n\n";
        dump (proc);
        std::cout << "\n";
        return 0;
    }

    if (argc > 1 && juce::String (argv[1]) == "--timing")
    {
        timingAudit (proc, argc > 2 ? juce::String (argv[2]) : juce::String());
        return 0;
    }

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

        // NOTHING DIFFERS FROM THE PROFILE ON A CLEAN LOAD, and this has to be
        // checked before anything in the suite has touched a mapping.
        //
        // The Settings screen shows the count in amber next to a Reset button.
        // If a fresh rig reported differences it would be crying wolf on every
        // launch, and the one time it mattered nobody would look. Every part,
        // including the ones this song does not have - "not in the song" must
        // read as zero rather than as everything missing.
        juce::StringArray noisy;
        for (int part = 0; part < 5; ++part)
            if (proc.controlsDifferingFromProfile (part) != 0)
                noisy.add (juce::String (part) + ": "
                           + proc.controlDifferenceSummary (part).replace ("\n", "; "));

        check (noisy.isEmpty(),
               "a freshly loaded rig reports nothing out of step with its profiles",
               noisy.isEmpty() ? juce::String ("all five parts agree")
                               : noisy.joinIntoString ("   |   "));
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
    // A SONG THAT HAS PLAYED TO ITS END HAS PAUSED ITSELF, so anything below
    // that expects a playing band has to start it again. This is not the test
    // being bent to fit: it is the same thing a person does, and the pause is
    // covered on its own further down.
    //
    // And it has to be PLAYING before stopping it means anything - all-notes-off
    // is sent on the transition out of playing, and the auto-pause already made
    // that transition (sending its own all-notes-off on the way). So: resume,
    // play a block, and then stop the thing that is actually running.
    proc.paused.store (false);
    proc.songFinished.store (false);

    head.playing = true;
    head.ppq = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        head.ppq = i * quartersPerBlock;
        buffer.clear();
        midi.clear();
        proc.processBlock (buffer, midi);
    }

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
    // A SONG THAT HAS PLAYED TO ITS END HAS PAUSED ITSELF, so anything below
    // that expects a playing band has to start it again. This is not the test
    // being bent to fit: it is the same thing a person does, and the pause is
    // covered on its own further down.
    proc.paused.store (false);
    proc.songFinished.store (false);

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

        // ---- nobody plays without being told what to play ----------------
        // A section can list a part in "plays" and never say what it should
        // do. The part is then present with no instruction, so the engine
        // chooses a chordal feel for it - which for the LEAD guitar means
        // comping random chords behind the band, the one thing a second
        // guitarist is there not to do.
        //
        // Four sections were in that state, all created by a script that
        // widened "plays" and then failed to add the phrase because it looked
        // for a "guitar" key those sections did not have. Silent failure, and
        // the result was audible.
        {
            int unbriefed = 0;
            juce::String firstBad;

            for (const juce::File& f : plans)
            {
                gb::SongPlan pl;
                std::string e;
                if (! gb::SongPlan::load (f.getFullPathName().toStdString(), pl, e))
                    continue;

                for (const gb::SectionPlan& sec : pl.sections)
                {
                    if (! sec.playsGuitar2) continue;
                    if (sec.guitar2Phrase != "auto") continue;

                    ++unbriefed;
                    if (firstBad.isEmpty())
                        firstBad = f.getFileName() + " / " + juce::String (sec.name);
                }
            }

            check (unbriefed == 0,
                   "no section leaves the lead guitar playing without an instruction",
                   unbriefed == 0 ? juce::String ("all briefed")
                                  : juce::String (unbriefed) + " unbriefed, first: " + firstBad);
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

    // ---- the second guitar answering, rather than soloing or silent ---------
    // A fill is a lead line placed where the harmony leaves room. It sits under
    // whoever it is answering rather than on top of them, it stays out of the
    // front of the bar, and it lands MOSTLY at the end of a four-bar group.
    //
    // The first pass had none of that. Given a whole bar it filled the whole
    // bar - eleven notes of repeating cell, which is a run wearing a fill's
    // job - and a held note could start past the end of its own phrase and
    // land on the downbeat of the bar it was staying out of.
    //
    // THESE CHECKS USED TO SAY "ONLY" AND "NEVER", and they were wrong to.
    // Pinned exactly, they made the rhythm of a fill identical in every song
    // ever played - reported as "every fill landing on beat 3 of every 4th bar
    // is no good and will need to change", and the checks were half the reason
    // it could not. A tendency is the thing worth holding; an absolute is what
    // makes an engine sound like an engine. So they are now majorities with the
    // floor set well below what the weights produce, which still catches the
    // fault they were written for - a fill wandering into the front of the bar,
    // or spraying evenly across every bar - without forbidding a player from
    // ever anticipating one.
    {
        gb::SongPlan plan;
        std::string err;

        const bool made = gb::SongPlan::parse (
            "{ \"name\": \"fills\", \"bpm\": 150, \"key\": \"E\","
            "  \"style\": \"thrash\", \"seed\": 4242,"
            "  \"drum_profile\": \"C:/Projects/Ghostband/profiles/ssd5-terry-date.json\","
            "  \"bass_profile\": \"C:/Projects/Ghostband/profiles/modo-bass-2.json\","
            "  \"guitar_profile\": \"C:/Projects/Ghostband/profiles/vg-iron2.json\","
            "  \"guitar2_profile\": \"C:/Projects/Ghostband/profiles/shreddage-3-hydra.json\","
            "  \"sections\": [ { \"name\": \"verse\", \"bars\": 16,"
            "      \"intensity\": 0.8, \"chords\": [\"Em\"],"
            "      \"plays\": \"drums+bass+guitar+guitar2\","
            "      \"guitar\": \"driving\", \"guitar2\": \"fills\","
            "      \"lead\": \"guitar\" } ] }",
            "fills", plan, err);

        check (made, "a plan can ask the second guitar for fills", juce::String (err));

        if (made)
        {
            gb::DrumProfile kit;
            gb::BassProfile bass;
            gb::PhraseProfile gtr, gtr2;
            std::string e;

            gb::DrumProfile::load  ("C:/Projects/Ghostband/profiles/ssd5-terry-date.json", kit, e);
            gb::BassProfile::load  ("C:/Projects/Ghostband/profiles/modo-bass-2.json", bass, e);
            gb::PhraseProfile::load ("C:/Projects/Ghostband/profiles/vg-iron2.json", gtr, e);
            gb::PhraseProfile::load ("C:/Projects/Ghostband/profiles/shreddage-3-hydra.json", gtr2, e);

            const gb::RenderResult r = gb::renderPerformance (plan, kit, bass, &gtr, nullptr, &gtr2);

            const int barTicks = gb::kPPQ * 4;
            int notes = 0, offBoundary = 0, inFrontHalf = 0;
            double accentSum = 0.0;

            for (const gb::LeadIntent& n : r.performance.guitar2.lead)
            {
                ++notes;
                accentSum += n.accent;

                const int bar    = n.tick / barTicks;
                const int within = n.tick % barTicks;

                if ((bar + 1) % 4 != 0)              ++offBoundary;

                // A THIRD of the bar, not a half. A pickup is counted against
                // the bar it lands IN, so an anticipation legitimately shows up
                // early here - but nothing may start in the first third, which
                // is where the part being answered actually lives.
                if (within < barTicks / 3 - 20)      ++inFrontHalf;
            }

            check (notes > 6, "the second guitar actually plays fills",
                   juce::String (notes) + " notes across 16 bars");

            const int onBoundary = notes - offBoundary;

            check (onBoundary * 2 > notes,
                   "and mostly at the end of a four-bar group, where the singer stops",
                   juce::String (onBoundary) + " of " + juce::String (notes)
                       + " notes on the boundary");

            // But NOT all of them, or the rhythm of a fill is a metronome and
            // every song has the same one. This is the check that would have
            // caught the original complaint.
            check (offBoundary > 0,
                   "and not ONLY there - the fourth bar is a tendency, not a rule",
                   juce::String (offBoundary) + " notes elsewhere");

            check (inFrontHalf == 0,
                   "and never in the front of the bar, where the part it answers is",
                   juce::String (inFrontHalf) + " notes in the front third");

            // ---- THE INTUITION DIAL, MEASURED AT BOTH ENDS ----------------
            //
            // The claim is that it decides how much the band plays what it
            // feels like rather than what is obvious, and the claim has to be
            // checkable or it is a knob with a story attached.
            //
            // Measured on the one thing the owner named: WHERE A FILL LANDS. At
            // the bottom every fill is on the fourth bar, dead on the half -
            // which is not a limitation, it is the tight literal band that end
            // of the dial is for, and it happens to be exactly what this engine
            // did before any of this existed. At the top they spread.
            {
                const auto placement = [&] (double iq)
                {
                    gb::SongPlan p = plan;
                    p.intuition = iq;

                    const gb::RenderResult rr =
                        gb::renderPerformance (p, kit, bass, &gtr, nullptr, &gtr2);

                    int onFourth = 0, onHalf = 0, total = 0;

                    for (const gb::LeadIntent& n : rr.performance.guitar2.lead)
                    {
                        ++total;
                        const int bar    = n.tick / barTicks;
                        const int within = n.tick % barTicks;

                        if ((bar + 1) % 4 == 0)                  ++onFourth;
                        if (std::abs (within - barTicks / 2) < 40) ++onHalf;
                    }

                    return std::array<int, 3> { onFourth, onHalf, total };
                };

                const auto rigid = placement (0.0);
                const auto loose = placement (1.0);

                check (rigid[2] > 0 && loose[2] > 0,
                       "the second guitar plays fills at both ends of the dial",
                       juce::String (rigid[2]) + " notes at 0, "
                           + juce::String (loose[2]) + " at 1");

                // AT THE BOTTOM, NOTHING STRAYS. Every note on the fourth bar
                // of a four-bar group - the whole of what the dial's low end
                // promises, and the exact behaviour that was hard-coded until
                // this session.
                check (rigid[0] == rigid[2],
                       "at no intuition a fill lands only on the fourth bar",
                       juce::String (rigid[2] - rigid[0]) + " notes strayed");

                // AND AT THE TOP, SOMETHING DOES. If this ever came out equal
                // the dial would be doing nothing at the end where it is
                // supposed to do the most, which is the failure a knob like
                // this has.
                check (loose[0] < loose[2],
                       "at full intuition they stop being tied to it",
                       juce::String (loose[2] - loose[0]) + " of "
                           + juce::String (loose[2]) + " notes elsewhere");

                // The same again for WHERE IN THE BAR, which was the other half
                // of the complaint.
                const double rigidHalf = rigid[2] > 0 ? rigid[1] / (double) rigid[2] : 0.0;
                const double looseHalf = loose[2] > 0 ? loose[1] / (double) loose[2] : 0.0;

                check (rigidHalf > looseHalf,
                       "and a low dial crowds them onto the half bar more than a high one",
                       juce::String (rigidHalf * 100.0, 0) + "% vs "
                           + juce::String (looseHalf * 100.0, 0) + "%");
            }

            // AND THE MIDDLE CHANGES NOTHING, which is the promise that let
            // this dial be added at all.
            //
            // Without it, adding a control would have rewritten all 34 preset
            // songs on the day it landed and forced the two reference pins to
            // be re-cut - and a pin you re-cut whenever it fails is not a pin.
            // So the default is not "a sensible middle", it is THE OLD
            // BEHAVIOUR, and this is what says so.
            {
                gb::SongPlan mid = plan;
                mid.intuition = 0.5;

                const gb::RenderResult a =
                    gb::renderPerformance (plan, kit, bass, &gtr, nullptr, &gtr2);
                const gb::RenderResult b =
                    gb::renderPerformance (mid, kit, bass, &gtr, nullptr, &gtr2);

                bool identical = a.performance.guitar2.lead.size()
                                   == b.performance.guitar2.lead.size();

                for (size_t i = 0; identical && i < a.performance.guitar2.lead.size(); ++i)
                    identical = a.performance.guitar2.lead[i].tick
                                    == b.performance.guitar2.lead[i].tick
                             && a.performance.guitar2.lead[i].pitch
                                    == b.performance.guitar2.lead[i].pitch;

                check (identical,
                       "and the dial's default is the old behaviour, note for note",
                       juce::String ((int) a.performance.guitar2.lead.size()) + " vs "
                           + juce::String ((int) b.performance.guitar2.lead.size()) + " notes");
            }

            // Under, not over. The rhythm guitar is leading this section.
            const double avg = notes > 0 ? accentSum / notes : 1.0;
            check (avg < 0.95, "and softer than a solo would be",
                   juce::String (avg, 2) + " average accent");

            // The rhythm guitar must be untouched by any of it.
            check (! r.performance.guitar.chords.empty(),
                   "while the rhythm guitar keeps playing underneath",
                   juce::String ((int) r.performance.guitar.chords.size()) + " chords");

            // ---- the FILLS dial, at both ends -------------------------
            // Off has to be OFF. The last bar of a section is deliberately
            // exempt from the "does it take this opening" roll, so without a
            // check at zero the dial would still play one fill per section -
            // and a control labelled none that plays anyway is not a control.
            gb::SongPlan quiet = plan;
            quiet.fills = 0.0;
            const gb::RenderResult off =
                gb::renderPerformance (quiet, kit, bass, &gtr, nullptr, &gtr2);

            check (off.performance.guitar2.lead.empty(),
                   "the fills dial at zero silences them completely",
                   juce::String ((int) off.performance.guitar2.lead.size()) + " notes left");

            gb::SongPlan busy = plan;
            busy.fills = 1.0;
            const gb::RenderResult full =
                gb::renderPerformance (busy, kit, bass, &gtr, nullptr, &gtr2);

            // Not "more notes than at 0.62" - this section is sixteen bars and
            // offers exactly four openings, which 0.62 happened to take all of.
            // The claim worth making is that at one, every opening is used.
            std::set<int> filled;
            for (const gb::LeadIntent& n : full.performance.guitar2.lead)
                filled.insert (n.tick / barTicks + 1);

            check (filled.size() == 4 && filled.count (4) && filled.count (8)
                       && filled.count (12) && filled.count (16),
                   "and at one it takes every opening it is offered",
                   juce::String ((int) filled.size()) + " of 4 four-bar boundaries");

            // Turning fills off must not disturb anything else. The answering
            // guitar draws from its own derived stream precisely so that
            // silencing it cannot move the drums.
            check (off.sections.size() == r.sections.size()
                       && off.sections[0].drumHits  == r.sections[0].drumHits
                       && off.sections[0].bassNotes == r.sections[0].bassNotes,
                   "and silencing them moves nothing else in the song",
                   juce::String (r.sections[0].drumHits) + "/"
                       + juce::String (r.sections[0].bassNotes) + " either way");
        }
    }

    // ---- an articulation can be a controller instead of a note --------------
    // A keyswitch is a note outside the playable range, and a note aimed at the
    // wrong instrument gets PLAYED - which is exactly what happened when a
    // guitar received another guitar's articulation switches and treated every
    // one of them as music. A controller nothing has learned does nothing at
    // all, so where an instrument offers both, the controller is the safe form.
    //
    // Both must work, and a profile written the old way must behave exactly as
    // it did.
    //
    // Written as its own file rather than by patching a shipped profile. The
    // first version copied shreddage-3-hydra.json and string-replaced one line
    // in it - and broke the moment that profile moved to controllers on its
    // own, because the replace silently matched nothing and the test started
    // asserting against whatever the real file happened to say that day. A test
    // that reads the thing it is testing is not a test.
    {
        const juce::File tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("gb-artic-test.json");

        // One line, no newline escapes. JSON does not need them and this
        // literal has been eaten twice by tooling on the way in.
        tmp.replaceWithText (
            "{ \"name\": \"artic probe\", \"id\": \"artic_probe\","
            "  \"channel\": 11, \"mode\": \"notes\","
            "  \"chord_zone\": { \"lowest_note\": 40, \"highest_note\": 88 },"
            "  \"keyswitches_verified\": true,"
            "  \"phrases\": { \"open\": 12, \"driving\": 15,"
            "    \"muted\": { \"cc\": 40, \"value\": 37, \"keyswitch\": 13 } } }");

        {
            gb::PhraseProfile prof;
            std::string e;
            const bool loaded = gb::PhraseProfile::load (tmp.getFullPathName().toStdString(),
                                                        prof, e);

            check (loaded, "a profile mixing keyswitches and controllers loads",
                   juce::String (e));

            const auto muted = prof.switchFor (gb::PhraseFeel::Muted);
            check (loaded && muted.byControl() && muted.cc == 40 && muted.value == 37,
                   "a feel on a controller reads back as one",
                   "cc " + juce::String (muted.cc) + " = " + juce::String (muted.value));

            // The one that matters. A controller wins, so there is no note at
            // all - otherwise Calibrate offers a step pressing a key that
            // nothing ever sends, which is a test of something not happening.
            check (loaded && prof.keyFor (gb::PhraseFeel::Muted) < 0,
                   "and has no note anywhere in the engine",
                   juce::String (prof.keyFor (gb::PhraseFeel::Muted)));

            bool offered = false;
            for (const auto& k : prof.allPhraseKeys())
                if (k.first == gb::PhraseFeel::Muted) offered = true;

            check (! offered, "so calibration does not offer it as a note to play");

            check (loaded && prof.keyFor (gb::PhraseFeel::Driving) == 15
                       && prof.keyFor (gb::PhraseFeel::Open) == 12,
                   "while keyswitched feels keep their notes",
                   juce::String (prof.keyFor (gb::PhraseFeel::Open)) + " / "
                       + juce::String (prof.keyFor (gb::PhraseFeel::Driving)));

            std::string saveErr;
            check (prof.save (tmp.getFullPathName().toStdString(), saveErr),
                   "both forms save", juce::String (saveErr));

            gb::PhraseProfile reread;
            const bool ok = gb::PhraseProfile::load (tmp.getFullPathName().toStdString(),
                                                    reread, e);
            const auto back = reread.switchFor (gb::PhraseFeel::Muted);

            check (ok && back.byControl() && back.cc == 40 && back.value == 37
                       && back.note == 13,
                   "and come back unchanged, the recorded keyswitch included",
                   juce::String (e));

            check (ok && reread.keyFor (gb::PhraseFeel::Open) == 12,
                   "including the ones that were notes to begin with");

            
            // ---- per-note gestures ----------------------------------
            // A gesture on one note, not a style for a section. Declared the
            // same way, because on this instrument they are selected the same
            // way: one active articulation at a time, from one control.
            //
            // The important property is that an UNMAPPED gesture is silence.
            // The generator asks for pinch harmonics from every lead profile;
            // most instruments have none, and asking must cost nothing.
            {
                const juce::File g = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                         .getChildFile ("gb-gesture-test.json");

                g.replaceWithText (
                    "{ \"name\": \"gesture probe\", \"id\": \"gesture_probe\","
                    "  \"channel\": 11, \"mode\": \"notes\","
                    "  \"chord_zone\": { \"lowest_note\": 40, \"highest_note\": 88 },"
                    "  \"phrases\": { \"solo\": { \"cc\": 40, \"value\": 10 } },"
                    "  \"lead_articulations\": {"
                    "    \"pinch\": { \"cc\": 40, \"value\": 84 },"
                    "    \"rake\":  { \"cc\": 40, \"value\": 71 } } }");

                gb::PhraseProfile gp;
                std::string ge;
                const bool ok = gb::PhraseProfile::load (g.getFullPathName().toStdString(),
                                                        gp, ge);

                check (ok, "a profile can declare per-note gestures", juce::String (ge));

                const auto pinch = gp.switchFor (gb::LeadArtic::Pinch);
                check (ok && pinch.byControl() && pinch.value == 84,
                       "and a gesture reads back with its own value",
                       "cc " + juce::String (pinch.cc) + " = " + juce::String (pinch.value));

                // The one that has to be true of every other instrument in the
                // rig: asking for a gesture nothing declares costs nothing.
                check (ok && ! gp.hasLeadArtic (gb::LeadArtic::Choke),
                       "and one it does not declare is simply absent");

                gb::PhraseProfile none;
                check (! none.hasLeadArtic (gb::LeadArtic::Pinch),
                       "a profile that declares none has none",
                       "so asking an instrument without them costs nothing");

                g.deleteFile();
            }

            tmp.deleteFile();
        }
    }

    // ---- every theme stays readable ----------------------------------------
    // A theme is eleven numbers, and the easiest possible way to quietly undo
    // the type and contrast work. "dim" carries the section detail, every field
    // label and the whole footer - it was the colour behind "almost all writing
    // is completely un-readable", and a new palette could put it right back
    // there without anyone noticing until they were on a stage.
    //
    // Contrast ratio as the W3C defines it: (L1 + 0.05) / (L2 + 0.05) on
    // relative luminance. 4.5 is their threshold for body text; 3.0 is the one
    // for large text, and everything here is 13pt or above on a dark ground.
    {
        const auto luminance = [] (juce::Colour c)
        {
            const auto channel = [] (double v)
            {
                v /= 255.0;
                return v <= 0.03928 ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4);
            };

            return 0.2126 * channel (c.getRed())
                 + 0.7152 * channel (c.getGreen())
                 + 0.0722 * channel (c.getBlue());
        };

        const auto contrast = [&luminance] (juce::Colour a, juce::Colour b)
        {
            const double la = luminance (a), lb = luminance (b);
            const double hi = std::max (la, lb), lo = std::min (la, lb);
            return (hi + 0.05) / (lo + 0.05);
        };

        int tooFaint = 0;
        juce::String worst;
        double worstRatio = 99.0;

        for (int i = 0; i < ghost::numThemes; ++i)
        {
            const ghost::Theme& t = ghost::kThemes[i];

            // Every surface text is actually drawn on, not just the page.
            //
            // This used to check `background` alone, and that is how the light
            // theme shipped with an unreadable menu: a popup is painted on
            // cardRaised, which nothing here looked at. Rows sit on cards, and
            // menus on raised ones, so all three have to hold.
            struct Ground { const char* where; juce::uint32 c; };
            const Ground grounds[] = {
                { "on the page",   t.background },
                { "on a card",     t.card       },
                { "on a menu",     t.cardRaised },
            };

            struct Pair { const char* what; juce::uint32 c; double least; };
            const Pair pairs[] = {
                { "text",   t.text,   7.0 },   // headings: should be comfortable
                { "silver", t.silver, 4.5 },   // values
                { "dim",    t.dim,    4.5 },   // labels, captions, the footer
                { "warn",   t.warn,   3.0 },   // a colour, so judged as large text
            };

            for (const Ground& gr : grounds)
                for (const Pair& pr : pairs)
                {
                    const double r = contrast (juce::Colour (gr.c), juce::Colour (pr.c));
                    if (r < pr.least)
                    {
                        ++tooFaint;
                        if (r < worstRatio)
                        {
                            worstRatio = r;
                            worst = juce::String (t.name) + " " + pr.what + " "
                                  + gr.where + " at " + juce::String (r, 2)
                                  + ", wants " + juce::String (pr.least, 1);
                        }
                    }
                }
        }

        check (tooFaint == 0, "every theme keeps its text readable on its own background",
               tooFaint == 0 ? juce::String (ghost::numThemes) + " themes"
                             : juce::String (tooFaint) + " too faint, worst: " + worst);

        // And the accent has to be visible ON a card, not only on the page -
        // it is what "active" is drawn in, and cards are lighter than the page.
        int accentFaint = 0;
        for (int i = 0; i < ghost::numThemes; ++i)
        {
            const ghost::Theme& t = ghost::kThemes[i];
            if (contrast (juce::Colour (t.card), juce::Colour (t.accentA)) < 2.5)
                ++accentFaint;
        }

        check (accentFaint == 0, "and its accent shows against a card",
               juce::String (accentFaint) + " themes where it does not");

        // Switching must actually change what the drawing code reads. These are
        // references into the palette rather than copies precisely so that a
        // hundred and eighty call sites did not have to change - and a copy
        // slipping back in would freeze every one of them on theme zero.
        ghost::applyTheme (0);
        const juce::Colour first = ghost::colours::background;
        ghost::applyTheme (3);
        const juce::Colour third = ghost::colours::background;
        ghost::applyTheme (0);

        check (first != third, "switching a theme changes the palette",
               first.toDisplayString (false) + " -> " + third.toDisplayString (false));

        check (ghost::colours::background == first,
               "and switching back restores it");

        // An index this build does not have is ignored, not clamped. A session
        // saved by a later version should keep the default rather than land on
        // whichever theme happens to sit at that number - which would move
        // again on the next release.
        ghost::applyTheme (999);
        check (ghost::currentTheme() == 0, "an unknown theme index is ignored",
               juce::String (ghost::currentTheme()));
    }

    // ---- naming a control suggests what it should follow --------------------
    // The names below are real ones off the instruments in this rig, and the
    // expectations are what a person who knows the instrument would say. The
    // first owner to map eleven controls got most of them wrong - not
    // carelessly, there was nothing to go on - and one of them, a pitch bend
    // range set to change per song, would have detuned every bend in the solos.
    {
        struct Case { const char* name; const char* follows; };
        static const Case cases[] = {
            // the mix knob's target
            { "volume",               "level" },
            { "master volume",        "level" },
            { "output",               "level" },

            // settings: one right value, so nothing is sent
            { "pitch bend range",     "none" },
            { "tune",                 "none" },
            { "invert MIDI channels", "none" },
            { "anti-repetition",      "none" },
            { "MIDI guitar mode",     "none" },

            // brightness and voice, up when out front
            { "tone",                 "lead" },
            { "bite",                 "lead" },
            { "presence",             "lead" },
            { "signal",               "lead" },
            { "pickup",               "lead" },

            // how hard the section is played
            { "drive",                "intensity" },
            { "xtra attack",          "intensity" },
            { "dynamics",             "intensity" },

            // a choice held for the whole song
            { "amp model",            "random once" },
            { "character",            "random once" },
            { "cab",                  "random once" },

            // colour, free to move
            { "ambience amount",      "random" },
            { "finisher",             "random" },
            { "width",                "random" },

            // a lead player's gesture
            { "unison bend",          "lead" },
            { "vibrato",              "lead" },
        };

        int wrong = 0;
        juce::String firstWrong;

        for (const Case& c : cases)
        {
            const gb::ControlSuggestion sug = gb::suggestControl (c.name);
            if (sug.follows != c.follows)
            {
                ++wrong;
                if (firstWrong.isEmpty())
                    firstWrong = juce::String (c.name) + " -> " + sug.follows
                               + ", wanted " + c.follows;
            }
        }

        check (wrong == 0, "control names suggest the right thing to follow",
               wrong == 0 ? juce::String ((int) (sizeof (cases) / sizeof (cases[0])))
                              + " names"
                          : juce::String (wrong) + " wrong, first: " + firstWrong);

        // The dangerous one, called out on its own. A pitch bend range that
        // moves re-tunes the ruler every bend is measured against.
        check (gb::suggestControl ("pitch bend range").follows == "none",
               "and a pitch bend range is never automated");

        // A name nothing matches must not guess wildly.
        const gb::ControlSuggestion unknown = gb::suggestControl ("wibble");
        check (unknown.follows == "intensity" && unknown.type == "knob",
               "an unrecognised name falls back to the plain default",
               juce::String (unknown.follows) + " / " + unknown.type);
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

        // ---- every step auditions the note it displays ----------------------
        // Reported: clicking the highest note played it, then clicking the
        // LOWEST note sounded the same high pitch again. Two explanations fit -
        // Ghostband sent the wrong note, or the low note was silent and what
        // was heard was the previous note still ringing. Only the first is a
        // bug here, and only measuring the wire can tell them apart.
        {
            juce::StringArray wrong;

            for (int i = 0; i < proc.getCalibrationStepCount(); ++i)
            {
                const auto st = proc.getCalibrationStep (i);
                if (st.note < 0) continue;

                proc.auditionStep (i);

                std::set<int> sent;
                for (int b = 0; b < 400; ++b)
                {
                    buffer.clear();
                    midi.clear();
                    proc.processBlock (buffer, midi);
                    for (const juce::MidiMessageMetadata m : midi)
                        if (m.getMessage().isNoteOn()
                                && m.getMessage().getChannel() == st.channel)
                            sent.insert (m.getMessage().getNoteNumber());
                }

                // The step's own note must be among what went out, and nothing
                // may go out that the step did not ask for.
                const std::set<int> allowed { st.note };

                if (sent.count (st.note) == 0)
                    wrong.add (st.label + " sent nothing");
                else
                    for (int n : sent)
                        if (allowed.count (n) == 0)
                            wrong.add (st.label + " also sent " + juce::String (n));
            }

            check (wrong.isEmpty(),
                   "every calibration step auditions exactly the note it shows",
                   wrong.isEmpty() ? juce::String (proc.getCalibrationStepCount()) + " steps"
                                   : wrong.joinIntoString ("; "));
        }

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
        proc.editorWidth.store (kMinW);
        proc.editorHeight.store (kMinH);

        if (auto* ed = proc.createEditorIfNeeded())
        {
            check (ed->getWidth() == kMinW && ed->getHeight() == kMinH,
                   "the editor opens at the remembered size",
                   juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()));

            ed->setSize (kMinW + 60, kMinH + 80);
            proc.editorBeingDeleted (ed);
            delete ed;

            check (proc.editorWidth.load() == kMinW + 60 && proc.editorHeight.load() == kMinH + 80,
                   "resizing is remembered after the window closes",
                   juce::String (proc.editorWidth.load()) + "x"
                       + juce::String (proc.editorHeight.load()));
        }

        if (auto* ed = proc.createEditorIfNeeded())
        {
            check (ed->getWidth() == kMinW + 60 && ed->getHeight() == kMinH + 80,
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

                // AND SAY WHICH, when it is not. "monophonic == false" sends
                // you reading the whole generator; the offending pair names the
                // device in one line.
                // AND SAY WHICH, when it is not.
                //
                // "monophonic == false" sends you reading the whole generator.
                // The offending pair names the fault in one line, and it earned
                // its keep immediately: the pair it printed was two notes on
                // the SAME tick, which is a different bug from a note held too
                // long and is fixed in a different place.
                juce::String clash;
                for (size_t i = 0; i + 1 < lead.size() && clash.isEmpty(); ++i)
                    if (lead[i].tick + lead[i].durationTicks > lead[i + 1].tick + 1)
                        clash = "note " + juce::String (lead[i].pitch)
                              + " at " + juce::String (lead[i].tick)
                              + " for " + juce::String (lead[i].durationTicks)
                              + " runs into " + juce::String (lead[i + 1].pitch)
                              + " at " + juce::String (lead[i + 1].tick);

                check (monophonic, "one note at a time - a player has one voice", clash);

                // AND NEVER TWO ON THE SAME TICK, which the clamp above cannot
                // fix and used to skip: a duration of nought is not a note, so
                // the pair survived and a line that is one guitarist with one
                // neck played a two-note chord. Checked separately from the
                // overlap because it is a separate fault with a separate fix.
                int sameTick = 0;
                for (size_t i = 0; i + 1 < lead.size(); ++i)
                    if (lead[i].tick == lead[i + 1].tick)
                        ++sameTick;

                check (sameTick == 0, "and never two notes on one tick",
                       juce::String (sameTick) + " coincident pairs");
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

                // At the window's own MINIMUM, not below it. This ran at
                // 600x720, which was already under the old floor of 700x820 and
                // is well under the new one - a size the window cannot be put
                // into, testing a layout nobody can see. It passed for years
                // because every screen was a single column, which degrades by
                // clipping; a two-column screen degrades by stacking its
                // columns on top of each other, and that is what it started
                // reporting.
                //
                // Below the minimum the layout is allowed to run out of room -
                // the same rule the zero-size check states. At and above it,
                // nothing may overlap.
                ed->setSize (kMinW, kMinH);

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

            // AND ONCE MORE ON THE SONG SCREEN WITH THE QUICK EDIT STRIP OPEN.
            //
            // The sweep above walks the screens without touching anything, and
            // the strip only exists after a click - so its first version drew
            // the feel box on top of the Reroll button at the window's minimum
            // width and every check passed. A state that needs a gesture to
            // reach needs the gesture in the test.
            if (gbEd != nullptr && proc.getBarTicks() > 0)
            {
                gbEd->showScreenForSnapshot (0);
                ed->setSize (kMinW, kMinH);
                gbEd->clickTrackerRowForTesting (2 * proc.getBarTicks());
                ed->setSize (kMinW, kMinH);

                // The strip arrives from below, so measure it where it lands.
                // Mid-slide it really is over the transport line, and a check
                // that caught that would be reporting the animation rather than
                // the layout.
                gbEd->settleAnimationsForTesting();

                std::vector<juce::Component*> shown;
                for (int i = 0; i < ed->getNumChildComponents(); ++i)
                {
                    juce::Component* c = ed->getChildComponent (i);
                    if (c != nullptr && c->isVisible() && ! c->getBounds().isEmpty())
                        shown.push_back (c);
                }

                juce::StringArray collisions, vanished;
                for (size_t a = 0; a < shown.size(); ++a)
                {
                    const auto ra = shown[a]->getBounds();

                    const bool isControl = dynamic_cast<juce::Button*> (shown[a])     != nullptr
                                        || dynamic_cast<juce::ComboBox*> (shown[a])   != nullptr
                                        || dynamic_cast<juce::Slider*> (shown[a])     != nullptr
                                        || dynamic_cast<juce::TextEditor*> (shown[a]) != nullptr;
                    if (ra.getWidth() < (isControl ? 16 : 4) || ra.getHeight() < (isControl ? 16 : 4))
                        vanished.add (describeComponent (shown[a]) + " ("
                                      + juce::String (ra.getWidth()) + "x"
                                      + juce::String (ra.getHeight()) + ")");

                    for (size_t b = a + 1; b < shown.size(); ++b)
                    {
                        const auto hit = ra.getIntersection (shown[b]->getBounds());
                        if (hit.getWidth() >= 6 && hit.getHeight() >= 6)
                            collisions.add (describeComponent (shown[a]) + "  over  "
                                            + describeComponent (shown[b]));
                    }
                }

                check (collisions.isEmpty(),
                       "nothing overlaps with the quick edit strip open",
                       collisions.joinIntoString ("; "));

                check (vanished.isEmpty(),
                       "and nothing on it is squeezed out of existence",
                       vanished.joinIntoString ("; "));

                gbEd->clickTrackerRowForTesting (2 * proc.getBarTicks());   // close it again
            }

            // ---- the guitar 2 toggle, driven through the form ----------------
            // applySectionEdit WRITES playsGuitar2 now. It used to leave the
            // flag alone, and that was the only thing protecting the second
            // guitar from a form with no control for it - four toggles writing
            // five flags would have cleared the fifth, so renaming a section
            // would have deleted its solo.
            //
            // That protection is gone by design, replaced by the toggle being
            // filled in. Which means the test has to go through the FORM: the
            // existing round-trip check reads getSectionEdit straight into
            // applySectionEdit and never goes near the control that would be
            // wrong.
            if (gbEd != nullptr)
            {
                proc.loadPlan (juce::File (planPath));

                // EMPTY IS NOT THE SAME AS "silent", and this loop got that
                // wrong for as long as it existed. The comment forty lines
                // below has said since it was written that a part switched off
                // never reaches the code that names a feel, so its feel comes
                // back empty - and `feel != "silent"` is true of an empty
                // string. On demo-metal the first section happens to play the
                // second guitar and the bug is invisible; on demo-rock it does
                // not, so this picked a section with the toggle already off and
                // then failed three checks for asking it to be on.
                //
                // The suite only ever ran demo-metal by default, which is why a
                // reference song exists in the plural.
                int target = -1;
                const auto before = proc.getSections();
                for (size_t i = 0; i < before.size(); ++i)
                {
                    const juce::String feel (before[i].guitar2Feel);
                    if (feel.isNotEmpty() && feel != "silent")
                    {
                        target = static_cast<int> (i);
                        break;
                    }
                }

                check (target >= 0, "a section plays the second guitar to begin with",
                       target >= 0 ? before[static_cast<size_t> (target)].name
                                   : juce::String ("none"));

                if (target >= 0)
                {
                    const juce::String was = before[static_cast<size_t> (target)].guitar2Feel;
                    const int ch = proc.channelGuitar2.load();

                    // Counted on the WIRE, per part, rather than read off the
                    // section report. Two earlier versions of this check were
                    // wrong about the report and passed or failed for reasons
                    // that had nothing to do with the toggle: a part that is
                    // switched off never reaches the code that names a feel, so
                    // its feel is empty rather than "silent"; and a part
                    // playing FILLS contributes a lead line, so its chord count
                    // is legitimately zero either way. Note-ons are neither.
                    const int notesOn = proc.getSequenceNoteOnCount (ch);
                    check (notesOn > 0, "the second guitar is playing to begin with",
                           juce::String (notesOn) + " note-ons on ch" + juce::String (ch));

                    gbEd->editSectionForTesting (target);
                    check (gbEd->sectionGuitar2ForTesting(),
                           "opening it in the editor shows the toggle ON");

                    // Touch the form the way changing any other field does.
                    gbEd->commitSectionEditForTesting();
                    check (proc.getSections()[static_cast<size_t> (target)].guitar2Feel == was
                               && proc.getSequenceNoteOnCount (ch) == notesOn,
                           "and committing the form does not silence it",
                           juce::String (proc.getSequenceNoteOnCount (ch)) + " note-ons");

                    gbEd->setSectionGuitar2ForTesting (false);
                    const int notesOff = proc.getSequenceNoteOnCount (ch);
                    check (notesOff < notesOn,
                           "switching the toggle off takes that section away",
                           juce::String (notesOn) + " -> " + juce::String (notesOff)
                               + " note-ons");

                    gbEd->setSectionGuitar2ForTesting (true);
                    check (proc.getSequenceNoteOnCount (ch) == notesOn
                               && juce::String (proc.getSections()[static_cast<size_t> (target)]
                                                    .guitar2Feel) == was,
                           "and switching it back on brings it in, playing exactly as before",
                           juce::String (proc.getSequenceNoteOnCount (ch)) + " note-ons, "
                               + was);

                    // Every other part must be where it was. A toggle that also
                    // moved its neighbours would pass every check above.
                    const auto after = proc.getSections();
                    bool othersHeld = true;
                    for (size_t i = 0; i < before.size() && i < after.size(); ++i)
                        if (before[i].guitarFeel != after[i].guitarFeel
                                || before[i].pianoFeel != after[i].pianoFeel
                                || before[i].drumHits != after[i].drumHits
                                || before[i].bassNotes != after[i].bassNotes)
                            othersHeld = false;

                    check (othersHeld, "and leaves every other part exactly as it was");
                }

                gbEd->showScreenForSnapshot (0);
            }

            // ---- switching theme with the window open ------------------------
            // Starting on a theme and switching to one are different paths, and
            // only the first was ever exercised: the picker sat in a rectangle
            // of zero height and could not be clicked. The second path has to
            // re-apply every colour a component was handed when it was built -
            // an explicit Label colour survives any LookAndFeel change - and
            // without that, half the interface stays on the old palette.
            if (gbEd != nullptr)
            {
                gbEd->showScreenForSnapshot (0);
                ed->setSize (kMinW, kMinH);

                gbEd->setThemeForTesting (0);

                juce::Array<juce::Colour> before;
                for (int i = 0; i < ed->getNumChildComponents(); ++i)
                    if (auto* l = dynamic_cast<juce::Label*> (ed->getChildComponent (i)))
                        before.add (l->findColour (juce::Label::textColourId));

                // Paper: the light one, so anything left behind is left behind
                // in the most visible way there is.
                //
                // Found BY NAME. This used to be "the last theme", which was
                // true until a theme was added after it - and then the check
                // quietly started testing a different palette than the one it
                // names, which is worse than failing.
                int paper = ghost::numThemes - 1;
                for (int i = 0; i < ghost::numThemes; ++i)
                    if (juce::String (ghost::themeName (i)) == "Paper") paper = i;
                gbEd->setThemeForTesting (paper);

                check (juce::String (ghost::themeName (paper)) == "Paper",
                       "there is a light theme to switch to", ghost::themeName (paper));

                juce::Array<juce::Colour> after;
                for (int i = 0; i < ed->getNumChildComponents(); ++i)
                    if (auto* l = dynamic_cast<juce::Label*> (ed->getChildComponent (i)))
                        after.add (l->findColour (juce::Label::textColourId));

                int moved = 0, stuck = 0;
                for (int i = 0; i < before.size() && i < after.size(); ++i)
                    (before[i] == after[i] ? stuck : moved)++;

                check (before.size() > 10, "there are labels to check",
                       juce::String (before.size()));
                check (stuck == 0 && moved > 0,
                       "every label follows a theme change made with the window open",
                       juce::String (moved) + " moved, " + juce::String (stuck) + " stuck");

                // The palette itself must have gone light, or the check above
                // would pass on two dark themes that merely differ.
                check (ghost::colours::background.getPerceivedBrightness() > 0.5f,
                       "and the light theme really is light",
                       juce::String (ghost::colours::background.getPerceivedBrightness(), 2));

                // Text on its own background has to stay readable. A theme that
                // applies but cannot be read is not an applied theme.
                float worst = 99.0f;
                for (int i = 0; i < after.size(); ++i)
                    worst = juce::jmin (worst,
                                        std::abs (after[i].getPerceivedBrightness()
                                                  - ghost::colours::background.getPerceivedBrightness()));

                check (worst > 0.15f,
                       "and every label still contrasts with the background it sits on",
                       "closest " + juce::String (worst, 2));

                // ---- the drop-down menus follow too ----
                // A ComboBox's popup is its own window and takes its colours
                // from the LookAndFeel, not from the box that opened it, so
                // nothing above can see it. The LookAndFeel set its table once
                // in its constructor and never again: the popup's background is
                // painted live and went light with the theme while its text
                // stayed near-white, which is how picking the light theme made
                // the theme list itself unreadable.
                juce::ComboBox* anyBox = nullptr;
                for (int i = 0; i < ed->getNumChildComponents() && anyBox == nullptr; ++i)
                    anyBox = dynamic_cast<juce::ComboBox*> (ed->getChildComponent (i));

                check (anyBox != nullptr, "there is a combo box to check");

                if (anyBox != nullptr)
                {
                    juce::LookAndFeel& lf = anyBox->getLookAndFeel();
                    const juce::Colour menuText = lf.findColour (juce::PopupMenu::textColourId);

                    // Against what is PAINTED, not against the table's own idea
                    // of the background. drawPopupMenuBackground fills with
                    // colours::cardRaised live, so the two can disagree - and
                    // when they did, the menu went light while its text stayed
                    // near-white. Comparing the table against itself passed
                    // happily through exactly that bug, because both halves
                    // were stale together.
                    const juce::Colour menuBack = ghost::colours::cardRaised;

                    check (contrastRatio (menuBack, menuText) >= 4.5,
                           "a drop-down menu is readable after a theme change",
                           "text on the menu it is actually painted on, at "
                               + juce::String (contrastRatio (menuBack, menuText), 2));

                    // And the menu really did go light with everything else,
                    // rather than passing by having stayed dark.
                    check (menuBack.getPerceivedBrightness() > 0.5f,
                           "and the menu itself followed the theme",
                           juce::String (menuBack.getPerceivedBrightness(), 2));

                    check (lf.findColour (juce::Label::textColourId)
                               .getPerceivedBrightness() < 0.5f,
                           "and so did the look-and-feel's default text colour",
                           juce::String (lf.findColour (juce::Label::textColourId)
                                             .getPerceivedBrightness(), 2));
                }

                // Back to the DEFAULT, not to theme zero. Leaving it on zero
                // meant every screenshot after this point rendered in a palette
                // nobody ships with - which is how a whole redesign came back
                // looking like the thing it replaced.
                gbEd->setThemeForTesting (ghost::numThemes - 1);
            }

            // ---- every control explains itself ------------------------------
            // Asked for in those words: hover anything and be told what it is,
            // what it does, and what the choices mean. The vocabulary this
            // plugin invented - follows, feels, fills, takes - is not guessable
            // and was documented only in the README.
            //
            // Pinned because a tooltip is invisible until somebody hovers: a
            // control added next year with no tooltip looks exactly like one
            // with a tooltip until the day it matters.
            for (int screen = 0; screen < GhostbandEditor::numScreens; ++screen)
            {
                if (gbEd != nullptr) gbEd->showScreenForSnapshot (screen);
                ed->setSize (kMinW, kMinH);

                juce::StringArray silent;
                int explained = 0;

                for (int i = 0; i < ed->getNumChildComponents(); ++i)
                {
                    juce::Component* c = ed->getChildComponent (i);
                    if (c == nullptr || ! c->isVisible())
                        continue;

                    // Controls only. A label is a caption; it has nothing to
                    // explain that it is not already saying.
                    const bool isControl = dynamic_cast<juce::Button*> (c)      != nullptr
                                        || dynamic_cast<juce::ComboBox*> (c)    != nullptr
                                        || dynamic_cast<juce::Slider*> (c)      != nullptr
                                        || dynamic_cast<juce::TextEditor*> (c)  != nullptr;
                    if (! isControl)
                        continue;

                    auto* t = dynamic_cast<juce::SettableTooltipClient*> (c);
                    if (t == nullptr || t->getTooltip().isEmpty())
                        silent.add (describe (c));
                    else
                        ++explained;
                }

                check (silent.isEmpty(),
                       juce::String ("every control on the ")
                           + GhostbandEditor::screenName (screen) + " screen explains itself",
                       silent.isEmpty() ? juce::String (explained) + " controls"
                                        : silent.joinIntoString ("; "));
            }

            // ---- nothing visible has been squeezed out of existence ---------
            // The loop above SKIPS components with empty bounds - it has to, or
            // every unlaid-out label counts as sitting on top of every other
            // one. That skip is a blind spot, and the theme picker lived in it:
            // constructed, made visible, laid out in a rectangle of zero height
            // by a screen that had counted its remaining pixels wrong, and
            // therefore absent from a build that reported the feature as done.
            //
            // A visible child that the current screen positions must have real
            // area. Run at the window's own minimum size, because below that
            // the layout is allowed to run out of room.
            for (int screen = 0; screen < GhostbandEditor::numScreens; ++screen)
            {
                if (gbEd != nullptr) gbEd->showScreenForSnapshot (screen);
                ed->setSize (kMinW, kMinH);

                juce::StringArray vanished;
                for (int i = 0; i < ed->getNumChildComponents(); ++i)
                {
                    juce::Component* c = ed->getChildComponent (i);
                    if (c == nullptr || ! c->isVisible())
                        continue;

                    // FOUR PIXELS WAS NOT ENOUGH OF A FLOOR.
                    //
                    // This caught a theme picker laid out at zero height, and
                    // then let the same picker through at EIGHT pixels when the
                    // window got shorter - visible, technically, and impossible
                    // to read or click. "Not zero" is not the property that
                    // matters; "usable" is.
                    //
                    // So a control that a person has to hit with a mouse gets a
                    // real floor. 16 is below anything in this plugin - the
                    // smallest is a 26-pixel row - and well above the point
                    // where a combo box stops being one. Labels keep the old
                    // floor, because an 11-pixel caption is a legitimate thing
                    // and several are exactly that.
                    const bool isControl = dynamic_cast<juce::Button*> (c)     != nullptr
                                        || dynamic_cast<juce::ComboBox*> (c)   != nullptr
                                        || dynamic_cast<juce::Slider*> (c)     != nullptr
                                        || dynamic_cast<juce::TextEditor*> (c) != nullptr;
                    const int floor = isControl ? 16 : 4;

                    if (c->getWidth() < floor || c->getHeight() < floor)
                        vanished.add (describe (c) + " ("
                                      + juce::String (c->getWidth()) + "x"
                                      + juce::String (c->getHeight()) + ")");
                }

                check (vanished.isEmpty(),
                       juce::String ("nothing is laid out at zero size on the ")
                           + GhostbandEditor::screenName (screen) + " screen",
                       vanished.joinIntoString ("; "));
            }

            proc.editorBeingDeleted (ed);
            delete ed;
        }
    }

    // ---- no control disappears without saying why ---------------------------
    // The zero-size check above cannot see this, and neither can the overlap
    // check or the snapshots: all three skip components that are not visible,
    // which is exactly the state being tested for. A control that is simply
    // absent looks identical to a control that was never built, and the report
    // that found this was "the piano mix knob is missing... which means there
    // could be other dials missing as well and I just don't realize it yet" -
    // one silent absence is enough to make every other control suspect.
    //
    // Run on a song with NO piano and a drum kit whose volume nothing outside
    // it can reach, because that is the case where two of the five knobs can do
    // nothing at all. Both must still be on screen, both must say why.
    {
        const juce::File noPiano ("C:/Projects/Ghostband/plans/preset-punk.json");
        if (noPiano.existsAsFile())
        {
            proc.loadPlan (noPiano);

            if (auto* ed = proc.createEditorIfNeeded())
            {
                auto* gbEd = dynamic_cast<GhostbandEditor*> (ed);
                if (gbEd != nullptr) gbEd->showScreenForSnapshot (0);   // song
                ed->setSize (kMinW, kMinH);

                juce::StringArray seen;
                for (int i = 0; i < ed->getNumChildComponents(); ++i)
                {
                    auto* l = dynamic_cast<juce::Label*> (ed->getChildComponent (i));
                    if (l == nullptr || ! l->isVisible() || l->getWidth() < 4)
                        continue;
                    seen.add (l->getText());
                }

                const auto shown = [&seen] (const juce::String& part) -> juce::String
                {
                    for (const juce::String& s : seen)
                        if (s == part || s.startsWith (part + " "))
                            return s;
                    return {};
                };

                juce::StringArray missing;
                for (const char* part : { "DRUMS", "BASS", "GTR", "GTR 2", "PIANO" })
                    if (shown (part).isEmpty())
                        missing.add (part);

                check (missing.isEmpty(),
                       "every mix knob is on screen even when it can do nothing",
                       missing.isEmpty() ? juce::String ("all five")
                                         : "absent: " + missing.joinIntoString (", "));

                // And the two that cannot work say so, rather than looking
                // exactly like the three that can.
                check (shown ("PIANO").contains ("not in song"),
                       "a part the song does not have says so on its knob",
                       shown ("PIANO"));
                check (shown ("DRUMS").contains ("no reach"),
                       "a part whose volume cannot be addressed says so on its knob",
                       shown ("DRUMS"));

                proc.editorBeingDeleted (ed);
                delete ed;
            }

            // Back to the plan the run was started on. Leaving a different song
            // loaded would make every check after this one depend on the order
            // they happen to run in, which is how a suite starts passing for
            // the wrong reason.
            proc.loadPlan (juce::File (planPath));
        }
    }

    // ---- the stall detector catches a stall ---------------------------------
    // There is no fix in this yet, on purpose. The owner reported the window
    // freezing while the music kept playing, it has not recurred, and every
    // piece of Ghostband's own frame was MEASURED and is fast - a full repaint
    // is under 5 ms, a whole reroll 1.2 ms. So there is nothing to fix by
    // reasoning about it, and six wrong theories on the Shreddage fault is
    // enough of a lesson. The plugin has to catch it in the act.
    //
    // What has to work is the bookkeeping between two ticks, and a real timer
    // cannot be made to miss its slot on demand - so the callback is driven by
    // hand with a real delay between the calls.
    {
        const juce::File testLog = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                       .getChildFile ("ghostband-harness")
                                       .getChildFile ("stalls.log");
        testLog.deleteFile();
        GhostbandProcessor::setStallLogFileForTesting (testLog);

        if (auto* ed = proc.createEditorIfNeeded())
        {
            auto* gbEd = dynamic_cast<GhostbandEditor*> (ed);
            if (gbEd != nullptr)
            {
                gbEd->showScreenForSnapshot (0);
                ed->setSize (kMinW, kMinH);

                // Two ticks close together: normal, and nothing is recorded.
                gbEd->runTimerForTesting();
                juce::Thread::sleep (20);
                gbEd->runTimerForTesting();

                check (gbEd->stallCountForTesting() == 0,
                       "a timer running on time records no stall",
                       juce::String (gbEd->stallCountForTesting()));

                // Now a real gap, and the audio thread kept running through it.
                const unsigned before = proc.audioBlocks.load();
                proc.audioBlocks.fetch_add (17);
                juce::Thread::sleep (400);
                gbEd->runTimerForTesting();

                check (gbEd->stallCountForTesting() == 1,
                       "a gap in the timer is caught",
                       juce::String (gbEd->stallCountForTesting()) + " recorded");

                // THE ATTRIBUTION IS THE WHOLE POINT, and it has to be able to
                // come out both ways or it is not a diagnosis.
                //
                // This one: the timer was not called, Ghostband did nothing in
                // the meantime, and the audio thread kept working. That can
                // only mean the message thread was held by something else.
                const juce::String notUs = gbEd->stallDetailForTesting();
                check (notUs.contains ("Not Ghostband"),
                       "a gap with no work of ours is not blamed on us",
                       notUs.fromFirstOccurrenceOf ("froze", true, false)
                            .upToFirstOccurrenceOf ("held it.", true, false));

                check (proc.audioBlocks.load() == before + 17,
                       "the audio block counter is what told it so",
                       juce::String (proc.audioBlocks.load() - before) + " blocks");

                // And the other way. A stall where GHOSTBAND held the message
                // thread must be blamed on Ghostband and must name the piece -
                // otherwise the first real fault of our own would be reported
                // to the owner as his host's problem, confidently and wrongly.
                //
                // This is not hypothetical: painting happens OUTSIDE the timer
                // callback, so before gbdiag::Work existed a slow paint of ours
                // looked exactly like a starved thread.
                {
                    gbEd->runTimerForTesting();      // start a clean window
                    {
                        GB_WORK ("a deliberately slow thing");
                        juce::Thread::sleep (400);
                    }
                    gbEd->runTimerForTesting();

                    const juce::String ourFault = gbEd->stallDetailForTesting();
                    check (ourFault.contains ("Ghostband did this")
                               && ourFault.contains ("a deliberately slow thing"),
                           "and work done between ticks IS blamed on us, by name",
                           ourFault.fromFirstOccurrenceOf ("Ghostband did this", true, false)
                                   .upToFirstOccurrenceOf ("\n", false, false));
                }

                // Written down as well as shown, because the window that would
                // show it is the thing that was frozen, and the session may be
                // closed before anyone looks.
                check (testLog.existsAsFile() && testLog.loadFileAsString().contains ("gap "),
                       "the stall is written to a log that outlives the session",
                       testLog.existsAsFile() ? testLog.loadFileAsString().trim()
                                              : juce::String ("no file"));

                // AND THE DAY, not just the clock. The first log off a real rig
                // carried the time only, so a line from three days earlier read
                // exactly like one from five minutes ago and no stall could be
                // lined up against what was being changed at the time.
                check (testLog.loadFileAsString()
                           .contains (juce::Time::getCurrentTime().formatted ("%Y-%m-%d")),
                       "and the log line says which day it was",
                       testLog.loadFileAsString().upToFirstOccurrenceOf ("gap", false, false).trim());

                // A GAP WITH NO AUDIO IS NOT A FREEZE EITHER, AND THIS IS THE
                // CHECK THE FIRST FIX SHOULD HAVE HAD.
                //
                // The first attempt gated on isShowing() and shipped on
                // reasoning alone. The next log killed it in ninety minutes:
                // 630 more lines, same flat 600 ms, the first of them ten
                // minutes AFTER the fix was installed. The window was on
                // screen; something else was imposing the period.
                //
                // The discriminator was in every line of the log the whole
                // time. audioBlocks counts EVERY processBlock, transport or
                // not, so zero across a gap means the host never called the
                // plugin at all - it was in an inactive rackspace, or the audio
                // engine was off. Nothing was sounding and nothing was moving.
                //
                // It is also the exact opposite of the reported fault, which
                // was "the playhead is not moving but audio is still heard like
                // normal". That has audio blocks by definition.
                {
                    const int  before  = gbEd->stallCountForTesting();
                    const auto sizeWas = testLog.getSize();

                    // A real gap, and deliberately NO blocks added.
                    gbEd->runTimerForTesting();
                    juce::Thread::sleep (400);
                    gbEd->runTimerForTesting();

                    check (gbEd->stallCountForTesting() == before,
                           "a gap while the host was not processing us is not a freeze",
                           juce::String (gbEd->stallCountForTesting() - before) + " recorded");

                    // BUT IT IS NOT SILENTLY DROPPED. One summary line, so the
                    // file says this is happening. A detector that hides what
                    // it decided to ignore is how the last theory survived a
                    // whole day unchallenged.
                    check (testLog.getSize() > sizeWas,
                           "and the log says so once, rather than not at all",
                           juce::String (testLog.getSize() - sizeWas) + " bytes");

                    check (testLog.loadFileAsString().contains ("not processing this plugin"),
                           "naming what it was, not just that it happened");
                }

                // AND OUR OWN FAULT IS NEVER DROPPED, whatever the audio was
                // doing. The gate above could hide a gap Ghostband caused
                // itself if the host happened not to be running - and the one
                // promise made to the owner about this file is that a line
                // where ghostband is a big number IS ours and he will see it.
                // The harness caught exactly this the moment the gate went in.
                {
                    const int before = gbEd->stallCountForTesting();

                    gbEd->runTimerForTesting();
                    {
                        GB_WORK ("our own slow thing, with the host idle");
                        juce::Thread::sleep (400);
                    }
                    gbEd->runTimerForTesting();

                    check (gbEd->stallCountForTesting() > before,
                           "but a gap WE caused is recorded even with no audio running",
                           juce::String (gbEd->stallCountForTesting() - before) + " recorded");

                    check (gbEd->stallDetailForTesting().contains ("our own slow thing"),
                           "and it is still named");
                }

                // A GAP WHILE THE WINDOW IS NOT ON SCREEN IS NOT A STALL.
                //
                // This is the single most valuable line in the detector, and it
                // was learned the expensive way. The first real log ran to 1,118
                // lines in three days; 1,109 of them were recorded with the
                // transport stopped, and 1,104 of THOSE were a gap of exactly
                // 600-601 ms. Ghostband's timer asks for 33 ms and nothing in it
                // asks for 600, so a flat 600 a thousand times over is Windows
                // throttling a window that is alive but hidden behind one of the
                // host's panel tabs. Nine lines in that file were real, and they
                // were the ones the noise buried.
                {
                    const int  before   = gbEd->stallCountForTesting();
                    const auto sizeWas  = testLog.getSize();

                    gbEd->runTimerOffScreenForTesting();
                    juce::Thread::sleep (400);
                    gbEd->runTimerOffScreenForTesting();

                    check (gbEd->stallCountForTesting() == before,
                           "a gap with the window off screen is not a stall anybody saw",
                           juce::String (gbEd->stallCountForTesting() - before) + " recorded");

                    check (testLog.getSize() == sizeWas,
                           "and nothing of it reaches the log",
                           juce::String (testLog.getSize() - sizeWas) + " bytes added");
                }
            }

            proc.editorBeingDeleted (ed);
            delete ed;
        }

        // Never leave a test pointing at a real file.
        // BACK TO THE HARNESS FILE, NOT TO NOTHING. Resetting to {} pointed
        // this at the owner's real stall log for the remainder of the run - see
        // the note in main().
        GhostbandProcessor::setStallLogFileForTesting (harnessStallLog);
        testLog.deleteFile();
    }

    // ---- saving mappings never destroys the reasoning in a profile ---------
    // Writing a controls block regenerates it from the ControlSet, so every
    // comment inside it is discarded. That is not theoretical: removing one
    // control from Shreddage through the Settings screen on 2026-09-13 took
    // fifty lines of annotation with it - why the pickup is a three-position
    // select, why xtra attack follows intensity, why four settings are on
    // "none". The mappings were fine; the reasoning was gone, and there was no
    // other copy.
    //
    // A backup is written whenever the file has comments to lose.
    {
        const juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("ghostband-harness");
        dir.createDirectory();

        const juce::File prof = dir.getChildFile ("commented-profile.json");
        const juce::File bak  = dir.getChildFile ("commented-profile.json.bak");
        prof.deleteFile();
        bak.deleteFile();

        prof.replaceWithText (
            "{\n"
            "  \"name\": \"Test\",\n"
            "  \"id\": \"test\",\n"
            "  \"channel\": 5,\n"
            "  \"controls\": {\n"
            "    // THE REASONING. This line is the whole point of the check.\n"
            "    \"tone\": { \"cc\": 40, \"follows\": \"none\", \"type\": \"knob\", \"low\": 0, \"high\": 1 }\n"
            "  }\n"
            "}\n");

        gb::ControlSet set;
        gb::PhraseProfile::ControlDef def;
        def.name = "tone"; def.cc = 40; def.follows = "none";
        set.editable().push_back (def);

        std::string err;
        const bool wrote = gb::saveControlsInto (prof.getFullPathName().toStdString(), set, err);

        check (wrote, "a profile's mappings can be written back", err);

        check (bak.existsAsFile(),
               "and the version with the comments in it is kept beside it",
               bak.existsAsFile() ? bak.getFileName() : juce::String ("no backup written"));

        check (bak.existsAsFile()
                   && bak.loadFileAsString().contains ("THE REASONING"),
               "so nothing that was written by hand is lost by pressing Save",
               bak.existsAsFile() ? juce::String (bak.getSize()) + " bytes"
                                  : juce::String ("-"));

        // Named so an installer globbing *.json cannot ship one.
        check (! bak.getFileName().endsWith (".json"),
               "and the backup cannot be mistaken for a profile",
               bak.getFileName());

        prof.deleteFile();
        bak.deleteFile();
    }

    // ---- the dice, and the way back --------------------------------------
    // It changes the song's whole character at once, which is the fun of it and
    // also the danger: rolling past one you liked with no way back is what
    // would make it frustrating rather than fun. So the undo is the part worth
    // pinning, and it is pinned by FINGERPRINT rather than by the dials - the
    // dials are what was rolled, and a song that came back with the right
    // numbers over different notes would pass a check on them and be wrong.
    {
        proc.loadPlan (juce::File (planPath));

        const auto fingerprint = [&proc]
        {
            juce::String f;
            for (int ch = 1; ch <= 16; ++ch)
            {
                const int n = proc.getSequenceNoteOnCount (ch);
                if (n > 0)
                    f << ch << ":" << n << "/" << proc.getSequencePitchSum (ch) << " ";
            }
            return f.trim();
        };

        const juce::String before = fingerprint();
        const int    wasSeed = proc.seed.load();
        const double wasBpm  = proc.getPlanBpm();

        check (before.isNotEmpty(), "there is a song to roll", before);
        check (! proc.canUndoTheDice(), "and nothing to put back before the first roll");

        check (proc.rollTheDice (false), "the dice rolls");

        const juce::String after = fingerprint();
        check (after != before, "and the song is genuinely different afterwards",
               before + "   ->   " + after);

        check (proc.seed.load() != wasSeed, "the seed moved",
               juce::String (wasSeed) + " -> " + juce::String (proc.seed.load()));

        // A NUDGE, NOT A REDRAW. A tempo drawn evenly from 40..250 is nonsense
        // most of the time; the song was written at a tempo that suits its
        // feel, and this has to stay recognisably that song.
        const double nowBpm = proc.getPlanBpm();
        check (nowBpm >= wasBpm * 0.7 && nowBpm <= wasBpm * 1.35,
               "the tempo is nudged rather than redrawn",
               juce::String (wasBpm, 0) + " -> " + juce::String (nowBpm, 0));

        check (proc.canUndoTheDice(), "and there is now something to put back");

        check (proc.undoTheDice(), "the undo runs");

        check (fingerprint() == before,
               "and it puts the song back note for note",
               fingerprint() == before ? juce::String ("identical")
                                       : before + "   got   " + fingerprint());

        check (proc.seed.load() == wasSeed, "with the seed it had",
               juce::String (proc.seed.load()));

        // ONE step. Twice must not roll forward again - a dice with a redo is
        // a toggle, which is not what anybody means by a dice.
        check (! proc.canUndoTheDice(), "and one step is all there is");
        check (! proc.undoTheDice(), "so pressing it again does nothing");

        // Everything it touched has to leave a playable song behind, not just a
        // different one. Rolled twenty times and checked each landing.
        bool alwaysPlayable = true;
        juce::String worst;
        for (int i = 0; i < 20 && alwaysPlayable; ++i)
        {
            proc.rollTheDice (false);

            const auto st = proc.getStatus();
            const double bpm = proc.getPlanBpm();

            if (! st.ok || st.drumHits <= 0 || bpm < 40.0 || bpm > 260.0)
            {
                alwaysPlayable = false;
                worst = "ok=" + juce::String (st.ok ? 1 : 0)
                      + " hits=" + juce::String (st.drumHits)
                      + " bpm=" + juce::String (bpm, 0);
            }
        }

        check (alwaysPlayable,
               "twenty rolls all land on a song that plays",
               alwaysPlayable ? juce::String ("all twenty") : worst);

        proc.loadPlan (juce::File (planPath));
    }

    // ---- the lit row is where the music is --------------------------------
    // The grid scrolls to keep the current row a third of the way down, so the
    // lit row IS a third of the way down almost all the time. Almost: the
    // window cannot scroll above bar one, so for the first third of a screenful
    // the playhead walks down through rows that are already on screen while the
    // grid stays still.
    //
    // Lighting rows/3 regardless meant the highlight sat on bar 9 - or bar 17
    // in a tall window - from the moment the song started and did not move
    // until the song reached it. Reported in exactly those words: "it's like
    // the playhead starts at 17 and doesn't start moving until the song passes
    // 17". A still image cannot show it, because the row it lights looks
    // perfectly reasonable.
    {
        proc.loadPlan (juce::File (planPath));

        if (auto* ed = proc.createEditorIfNeeded())
        {
            auto* gbEd = dynamic_cast<GhostbandEditor*> (ed);
            if (gbEd != nullptr)
            {
                gbEd->showScreenForSnapshot (0);
                ed->setSize (kMinW, 1400);          // tall, where the fault was worst

                // Bar one, transport running.
                proc.transportRunning.store (true);
                proc.playbackTick.store (0);
                gbEd->runTimerForTesting();

                check (gbEd->litTrackerRowForTesting() == 0,
                       "at the start of a song the lit row is the FIRST row",
                       juce::String (gbEd->litTrackerRowForTesting()));

                // A few bars in, still before the grid can scroll.
                const int bar = proc.getBarTicks();
                proc.playbackTick.store (bar * 3);
                gbEd->runTimerForTesting();

                check (gbEd->litTrackerRowForTesting() == 3,
                       "three bars in it is the fourth row, not a fixed one",
                       juce::String (gbEd->litTrackerRowForTesting()));

                // And once the song is deep enough that the grid scrolls, the
                // lit row must still be the one holding the playhead's tick.
                proc.playbackTick.store (bar * 30);
                gbEd->runTimerForTesting();

                const int lit = gbEd->litTrackerRowForTesting();
                check (lit >= 0 && lit < gbEd->visibleTrackerRowsForTesting(),
                       "and once the grid scrolls it is still on screen",
                       juce::String (lit));

                proc.transportRunning.store (false);
                proc.playbackTick.store (0);
            }

            proc.editorBeingDeleted (ed);
            delete ed;
        }
    }

    // ---- motion costs nothing when nothing is moving -----------------------
    // The interface animates a screen change, a queued jump and the three dials
    // on a take recall. All three run on a 60 Hz clock that must SLEEP the rest
    // of the time - the editor's own timer already reads the sequence thirty
    // times a second for the grid, and a second timer spinning behind it for no
    // reason would be exactly the kind of thing this plugin has spent two
    // sessions proving it does not do.
    {
        // The curve first, on its own, because everything else depends on it
        // arriving exactly rather than approximately.
        Eased e;
        e.set (0.0f);
        e.moveTo (1.0f, 100);

        check (e.busy(), "an eased value that has been given a target is moving");

        float last = e.value();
        bool wentBackwards = false;
        for (int i = 0; i < 20 && e.busy(); ++i)
        {
            e.advance (10);
            if (e.value() < last - 0.0001f) wentBackwards = true;
            last = e.value();
        }

        check (! wentBackwards, "and it only ever moves towards its target");

        // EXACTLY, not nearly. A knob that settles at 0.998 of where it was
        // told to go is a knob showing the wrong number for the rest of the
        // session, and the value on screen is what somebody reads back.
        check (! e.busy() && std::abs (e.value() - 1.0f) < 0.0001f,
               "and it arrives exactly on its target and stops",
               juce::String (e.value(), 6));

        // A change too small to see is not worth a timer.
        Eased tiny;
        tiny.set (0.5f);
        tiny.moveTo (0.5f, 100);
        check (! tiny.busy(), "a value told to go where it already is does not animate");

        // And the window itself: settled means settled.
        if (auto* ed = proc.createEditorIfNeeded())
        {
            auto* gbEd = dynamic_cast<GhostbandEditor*> (ed);
            if (gbEd != nullptr)
            {
                ed->setSize (kMinW, kMinH);

                // showScreenForSnapshot settles deliberately - a check or a
                // rendered image must see the window as it ends up, not a frame
                // of it on the way there.
                gbEd->showScreenForSnapshot (3);      // settings
                check (gbEd->animationsIdleForTesting(),
                       "switching screens for a snapshot leaves nothing moving");

                // AND THE REAL PATH DOES THE OPPOSITE. A button press must
                // actually start something, or every check above is testing a
                // feature that does not run - which is precisely what "i'm not
                // seeing any animations whatsoever" would look like from here.
                gbEd->changeScreenForTesting (0);     // song, the way a button does it
                check (! gbEd->animationsIdleForTesting(),
                       "changing screens the way a button does starts a fade");

                gbEd->showScreenForSnapshot (0);      // song
                check (gbEd->animationsIdleForTesting(),
                       "and nothing is left covering the window afterwards");
            }

            proc.editorBeingDeleted (ed);
            delete ed;
        }
    }

    // ---- the grid's own motion ---------------------------------------------
    //
    // THE ONE THING THESE HAVE TO PROTECT is that litRow() stays exact. The
    // highlight sitting on the wrong row is this project's longest-lived visible
    // fault - four sessions of it lighting bar 17 while the song played from bar
    // one - and easing the band is precisely the kind of change that could
    // quietly reintroduce it. So the eased number is allowed to differ from the
    // exact one only while it is travelling, and never once it has settled.
    {
        TrackerView t;
        t.setSize (900, 600);

        std::vector<gb::SectionReport> secs (2);
        secs[0].name = "intro";  secs[0].bars = 4;
        secs[0].startTick = 0;      secs[0].endTick = 1536;
        secs[1].name = "chorus"; secs[1].bars = 4;
        secs[1].startTick = 1536;   secs[1].endTick = 3072;
        t.setSections (secs);

        // One row per bar, which is the default and the coarsest - a step every
        // two seconds, which is the case a static highlight reads worst in.
        t.setCells ({}, 0, 384, 96, 384);

        t.setPlayhead (0);
        t.advanceMotion (16);

        check (std::abs (t.paintedRowForTesting() - (float) t.litRow()) < 0.0001f,
               "the band starts exactly on the playhead's row, it does not glide in",
               juce::String (t.paintedRowForTesting(), 4) + " vs " + juce::String (t.litRow()));

        // A step of one row. It must MOVE rather than teleport, which means
        // reporting itself busy and sitting between the two rows meanwhile.
        t.setPlayhead (384);
        const bool busy = t.advanceMotion (16);
        const float mid = t.paintedRowForTesting();

        check (busy && mid > 0.0f && mid < 1.0f,
               "a step to the next row travels rather than teleporting",
               juce::String (mid, 4));

        // AND IT ARRIVES EXACTLY. A band that settles at 0.98 of a row is a
        // highlight permanently half a row off, which is the original fault
        // wearing a different hat.
        int frames = 0;
        while (t.advanceMotion (16) && frames < 200) ++frames;

        check (frames < 200 && t.paintedRowForTesting() == (float) t.litRow(),
               "and lands exactly on the exact row, then stops",
               juce::String (t.paintedRowForTesting(), 6) + " vs "
                   + juce::String (t.litRow()) + ", " + juce::String (frames) + " frames");

        check (! t.motionPending(), "with nothing left pending to wake the clock for");

        // A JUMP IS NOT A STEP. Restarting the song, scrolling, or changing the
        // zoom can move the lit row by a screenful; easing across that reads as
        // the highlight falling down the window rather than as music playing.
        t.setPlayhead (384 * 12);
        t.advanceMotion (16);

        check (t.paintedRowForTesting() == (float) t.litRow(),
               "a jump of many rows arrives at once instead of sliding down the window",
               juce::String (t.paintedRowForTesting(), 4) + " vs " + juce::String (t.litRow()));

        // Crossing into a section flares its block in the ribbon. The playhead
        // being inside it was already drawn; this marks the MOMENT, which is
        // the thing the shape of a song is made of.
        t.setPlayhead (100);
        t.advanceMotion (16);
        const float before = t.ribbonFlareForTesting (1);

        t.setPlayhead (1600);                 // into the chorus
        t.advanceMotion (16);

        check (before == 0.0f && t.ribbonFlareForTesting (1) > 0.5f,
               "crossing into a section flares it in the ribbon",
               juce::String (t.ribbonFlareForTesting (1), 3));

        // And it fades out rather than staying lit, or the ribbon ends up with
        // every section it has ever played permanently brightened.
        for (int i = 0; i < 120 && t.ribbonFlareForTesting (1) > 0.0f; ++i)
            t.advanceMotion (16);

        check (t.ribbonFlareForTesting (1) == 0.0f,
               "and fades back to the steady lit state within a second or so");

        // The sweep: a reroll changes every note and almost nothing about how
        // the grid looks, so this is the feedback that it happened.
        check (! t.sweepingForTesting(), "nothing sweeps until something is rerolled");

        t.sweepIn();
        check (t.sweepingForTesting() && t.motionPending(),
               "a reroll sweeps the new arrangement in");

        frames = 0;
        while (t.advanceMotion (16) && frames < 200) ++frames;

        check (frames < 200 && ! t.sweepingForTesting(),
               "and it finishes, leaving nothing over the grid",
               juce::String (frames) + " frames");

        // Stopping puts the band away entirely - there is no current row when
        // nothing is playing, and a band left behind would claim there was.
        t.setPlayhead (-1);
        t.advanceMotion (16);

        check (t.paintedRowForTesting() < 0.0f && t.litRow() < 0,
               "and stopping puts the band away rather than leaving it somewhere",
               juce::String (t.paintedRowForTesting(), 3));

        // settleMotion is what every measuring check leans on: after it, the
        // eased number and the exact one are the same number.
        t.setPlayhead (384 * 3);
        t.setPlayhead (384 * 4);
        t.settleMotion();

        check (t.paintedRowForTesting() == (float) t.litRow() && ! t.motionPending(),
               "settling the grid makes the eased row and the exact row agree",
               juce::String (t.paintedRowForTesting(), 4) + " vs " + juce::String (t.litRow()));
    }

    // ---- THE AI PLANNER, the half that needs no key -------------------------
    //
    // Everything here runs on canned responses. No network, no account, no
    // cost - which is the reason the planner was split so the engine half is
    // pure: every sentence an owner could ever see from it can be produced and
    // checked here, including the ones for a rejected key, a refusal, a server
    // that is down and a chart that is half nonsense.
    {
        // ---- the request -------------------------------------------------
        gb::PlannerBrief brief;
        brief.request = "a slow doom song that ends in a blast beat";

        const gb::PlannerSettings settings;
        const gb::PlannerRequest  req = gb::buildPlannerRequest (brief, settings);

        std::string perr;
        const gb::Json body = gb::Json::parse (req.body, perr);

        check (perr.empty() && body.isObject(),
               "the planner's request is valid JSON", juce::String (perr));

        check (body.stringOr ("model", "") == "claude-opus-5",
               "and asks for the current Opus by default",
               juce::String (body.stringOr ("model", "?")));

        check (body["thinking"].stringOr ("type", "") == "adaptive"
                   && body["output_config"].stringOr ("effort", "") == "high",
               "with adaptive thinking at high effort");

        check (body["output_config"]["format"].stringOr ("type", "") == "json_schema"
                   && body["output_config"]["format"]["schema"].isObject(),
               "and its answer held to a JSON schema, so it is a chart or nothing");

        check (body.stringOr ("fallbacks", "") == "default",
               "and a declined request re-runs on another model rather than failing");

        bool version = false, beta = false, keyHeader = false;
        for (const auto& h : req.headers)
        {
            if (h.first == "anthropic-version" && h.second == "2023-06-01") version = true;
            if (h.first == "anthropic-beta" && h.second == "server-side-fallback-2026-07-01") beta = true;
            if (juce::String (h.first).equalsIgnoreCase ("x-api-key")) keyHeader = true;
        }

        check (version && beta, "with the version header and the fallback beta");

        // THE KEY NEVER PASSES THROUGH THE ENGINE. Not a header here, not a
        // byte of the body. The plugin adds the one header that carries it at
        // the moment of sending, which keeps the places a key can leak to one.
        check (! keyHeader && req.body.find ("sk-ant") == std::string::npos,
               "and the engine never sees the API key at all");

        // The user's own words, with everything people actually type into a
        // text box. They have to arrive as the same words, not a broken request.
        {
            gb::PlannerBrief awkward;
            awkward.request = "a \"heavy\" song\nwith C:\\paths, a\ttab, and caf\xc3\xa9 \xf0\x9f\xa4\x98";

            const gb::PlannerRequest r2 = gb::buildPlannerRequest (awkward, settings);
            std::string e2;
            const gb::Json b2 = gb::Json::parse (r2.body, e2);
            const std::string sent = b2["messages"][0].stringOr ("content", "");

            check (e2.empty() && sent.find (awkward.request) != std::string::npos,
                   "quotes, newlines, backslashes and emoji arrive exactly as typed",
                   juce::String (e2));
        }

        // A chart that hands a verse to a piano nobody loaded is a silent verse.
        {
            gb::PlannerBrief noPiano = brief;
            noPiano.hasPiano = false;

            const gb::PlannerRequest r3 = gb::buildPlannerRequest (noPiano, settings);
            std::string e3;
            const std::string sent = gb::Json::parse (r3.body, e3)["messages"][0].stringOr ("content", "");

            check (sent.find ("a piano") == std::string::npos
                       && sent.find ("a lead guitar") != std::string::npos,
                   "the model is told which parts this rig actually has");
        }

        std::string se;
        check (gb::Json::parse (gb::plannerOutputSchema(), se).isObject() && se.empty(),
               "the output schema is itself valid JSON", juce::String (se));

        // ---- the response ------------------------------------------------
        // A chart as the model would write it, wrapped the way the API wraps it.
        const auto chartJson = [] (const std::string& bpm, const std::string& firstChord,
                                   const std::string& sectionsOverride)
        {
            const std::string sections = ! sectionsOverride.empty() ? sectionsOverride :
                R"([ { "name": "intro", "bars": 4, "intensity": 0.3, "feel": "half_time",
                       "chords": [")" + firstChord + R"(", "C"], "plays": "drums+bass+guitar",
                       "guitar": "open", "guitar2": "silent", "piano": "silent",
                       "lead": "guitar", "fill": "small" },
                     { "name": "verse1", "bars": 8, "intensity": 0.5, "feel": "straight",
                       "chords": ["Em", "C", "D", "Em"], "plays": "full",
                       "guitar": "muted", "guitar2": "fills", "piano": "sparse",
                       "lead": "guitar", "fill": "auto" },
                     { "name": "chorus1", "bars": 8, "intensity": 0.85, "feel": "straight",
                       "chords": ["C", "G", "D", "Em"], "plays": "full",
                       "guitar": "driving", "guitar2": "fills", "piano": "open",
                       "lead": "both", "fill": "big" } ])";

            return std::string (R"({ "title": "Planned", "explanation": "A slow build into a loud chorus.",
                "key": "E", "mode": "natural_minor", "bpm": )") + bpm + R"(,
                "time_signature": [4, 4], "style": "doom", "bass_tuning": "drop_c",
                "ending": "cymbal_ring", "sections": )" + sections + " }";
        };

        const auto wrap = [] (const std::string& stop, const std::string& text)
        {
            return std::string (R"({ "id": "msg_x", "type": "message", "role": "assistant",
                "model": "claude-opus-5", "stop_reason": ")") + stop + R"(",
                "usage": { "input_tokens": 1800, "output_tokens": 950 },
                "content": [ { "type": "thinking", "thinking": "" },
                             { "type": "text", "text": )" + gb::jsonQuote (text) + " } ] }";
        };

        const gb::PlannerResult good = gb::parsePlannerResponse (200, wrap ("end_turn", chartJson ("72", "Em", "")));

        check (good.ok && good.plan.sections.size() == 3,
               "a good answer loads as a three-section song",
               juce::String (good.error) + " " + juce::String ((int) good.plan.sections.size()));

        check (good.plan.style == "doom" && good.plan.bassTuning == "drop_c"
                   && good.plan.sections[0].feel == "half_time"
                   && good.plan.sections[1].guitar2Phrase == "fills",
               "and every field lands where the same key in a hand-written plan would");

        check (good.explanation == "A slow build into a loud chorus."
                   && good.servedBy == "claude-opus-5"
                   && good.inputTokens == 1800 && good.outputTokens == 950,
               "with the model's own explanation, who answered, and what it cost");

        // A PERSON'S TYPO IS FORGIVEN; SO IS A MODEL'S. Same loader, same rule:
        // an unreadable chord plays as the key and is reported, and the other
        // eleven sections are not thrown away over it.
        const gb::PlannerResult typo = gb::parsePlannerResponse (200, wrap ("end_turn", chartJson ("72", "Xq", "")));
        bool mentioned = false;
        for (const auto& n : typo.notes) if (n.find ("Xq") != std::string::npos) mentioned = true;

        check (typo.ok && mentioned,
               "a chart with an unreadable chord still loads, and says which chord");

        const gb::PlannerResult fast = gb::parsePlannerResponse (200, wrap ("end_turn", chartJson ("400", "Em", "")));
        check (fast.ok && fast.plan.bpm == 250.0 && ! fast.notes.empty(),
               "an impossible tempo is clamped and reported, not fatal",
               juce::String (fast.plan.bpm));

        // THE STOP REASON IS READ BEFORE THE CONTENT. A refusal and a
        // truncation both arrive as a perfectly good 200.
        const gb::PlannerResult refused = gb::parsePlannerResponse (200, wrap ("refusal", ""));
        check (! refused.ok && refused.error.find ("declined") != std::string::npos,
               "a declined request says so rather than loading nothing",
               juce::String (refused.error));

        const gb::PlannerResult cut = gb::parsePlannerResponse (200, wrap ("max_tokens", "{ \"title\": \"half"));
        check (! cut.ok && cut.error.find ("ran out of room") != std::string::npos,
               "a chart cut off mid-way is refused rather than half-loaded",
               juce::String (cut.error));

        const gb::PlannerResult notJson = gb::parsePlannerResponse (200, wrap ("end_turn", "Here is your song!"));
        check (! notJson.ok, "an answer that is not a chart is refused",
               juce::String (notJson.error));

        const gb::PlannerResult empty = gb::parsePlannerResponse (200, wrap ("end_turn", chartJson ("72", "Em", "[]")));
        check (! empty.ok, "a chart with no sections is refused", juce::String (empty.error));

        // EVERY FAILURE AN OWNER CAN SEE, in words they can act on.
        const std::string err401 = R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})";
        const std::string err429 = R"({"type":"error","error":{"type":"rate_limit_error","message":"rate limited"}})";
        const std::string err529 = R"({"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})";

        check (gb::parsePlannerResponse (401, err401).error.find ("API key") != std::string::npos,
               "a rejected key is named as the key");
        check (gb::parsePlannerResponse (429, err429).error.find ("credit") != std::string::npos,
               "a rate limit mentions that it may be the account's credit");
        check (gb::parsePlannerResponse (529, err529).error.find ("busy") != std::string::npos,
               "an overloaded server is Anthropic's problem and says to try again");
        check (gb::parsePlannerResponse (0, "").error.find ("internet") != std::string::npos,
               "no connection at all is said plainly, with the band still playing");

        // ---- the merge ---------------------------------------------------
        // The model writes the song. It does not choose the plugins, the seed
        // or the performer's dials - a chart that silently reset humanize
        // would change something nobody asked it to.
        {
            gb::SongPlan current;
            std::string ce;
            gb::SongPlan::load ("C:/Projects/Ghostband/plans/demo-metal.json", current, ce);
            current.seed = 4242;  current.humanize = 0.83;  current.intuition = 0.2;
            current.transpose = 3;

            const gb::SongPlan merged = gb::mergeChart (current, good.plan);

            check (merged.drumProfile == current.drumProfile
                       && merged.guitar2Profile == current.guitar2Profile
                       && merged.seed == 4242 && merged.humanize == 0.83 && merged.intuition == 0.2,
                   "a planned song keeps this rig's plugins, seed and dials");

            check (merged.style == "doom" && merged.bpm == 72.0 && merged.sections.size() == 3
                       && merged.transpose == 0,
                   "and takes the chart's style, tempo and sections, with no stale transpose");

            // And it PLAYS. The whole point of the plan being the handoff is
            // that nothing downstream has to know where it came from.
            gb::DrumProfile pk;  gb::BassProfile pb;  std::string pe;
            gb::DrumProfile::load (merged.drumProfile, pk, pe);
            gb::BassProfile::load (merged.bassProfile, pb, pe);

            const gb::RenderResult rr = gb::renderPerformance (merged, pk, pb, nullptr, nullptr, nullptr);

            check (rr.performance.drums.size() > 20 && rr.performance.bass.size() > 10,
                   "and a planned song renders like any other song",
                   juce::String ((int) rr.performance.drums.size()) + " drum hits, "
                       + juce::String ((int) rr.performance.bass.size()) + " bass notes");
        }
    }

    // ---- a song's own problems reach the screen ----------------------------
    // SongPlan::validate has produced these since it was written, and only the
    // command-line renderer ever printed them. Inside the plugin a mistyped
    // chord silently becomes the KEY's root and a `plays` naming nothing
    // silently produces a silent section - the engine behaving exactly as
    // designed, which is why neither leaves a mark anywhere else on screen.
    //
    // Found by an external audit of the codebase, and the only finding in it
    // that was both real and worth the change.
    {
        const juce::File bad = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("ghostband-harness")
                                   .getChildFile ("typos.json");
        bad.getParentDirectory().createDirectory();
        bad.replaceWithText (
            "{\n"
            "  \"name\": \"Typos\",\n"
            "  \"key\": \"E\", \"mode\": \"natural_minor\", \"bpm\": 120,\n"
            "  \"style\": \"hard_rock\",\n"
            "  \"sections\": [\n"
            "    { \"name\": \"verse1\", \"bars\": 4, \"intensity\": 0.5,\n"
            "      \"plays\": \"drums+bass\", \"feel\": \"straight\", \"fill\": \"auto\",\n"
            "      \"chords\": [\"Em\", \"Em7b5\", \"C\", \"Xq\"] }\n"
            "  ]\n"
            "}\n");

        proc.loadPlan (bad);
        const auto st = proc.getStatus();

        check (st.ok, "a song with a typo in it still loads and plays", st.message);

        // TWO different faults, reported differently on purpose.
        //
        // "Em7b5" is a real chord with a root Ghostband reads and a quality it
        // has no voicing for, so it PLAYS - a major triad on E - and says so.
        // Silently, that was a half-diminished coming out as its opposite.
        //
        // "Xq" is not a note at all, so the root goes too and the bar falls
        // back to the key. A different sentence, because it is a different
        // thing to have to go and fix.
        bool namedTheQuality = false, namedTheJunk = false;
        for (const juce::String& w : st.planWarnings)
        {
            if (w.contains ("Em7b5")) namedTheQuality = true;
            if (w.contains ("Xq"))    namedTheJunk    = true;
        }

        check (namedTheQuality,
               "a chord whose quality is not understood is reported by name",
               st.planWarnings.isEmpty() ? juce::String ("nothing reported")
                                         : st.planWarnings.joinIntoString ("; "));

        check (namedTheJunk, "and so is one that is not a chord at all",
               st.planWarnings.joinIntoString ("; "));

        // The one the audit pointed at, and the one it got wrong about which
        // branch produced it: case. "M7" is a major seventh; the parser
        // lowercased the suffix before testing it, so the M7 branch was
        // unreachable and CM7 matched "m7" one line below - major seven playing
        // as minor seven.
        check (gb::parseChord ("CM7").quality == gb::ChordQuality::Major7,
               "CM7 is a major seventh, not a minor one");
        check (gb::parseChord ("Cm7").quality == gb::ChordQuality::Minor7,
               "and Cm7 is still a minor seventh");
        check (gb::parseChord ("Cmaj7").quality == gb::ChordQuality::Major7,
               "and Cmaj7 still works as it always did");

        // It has to reach the WINDOW, not just the struct. The whole fault was
        // a warning that existed and was never shown.
        if (auto* ed = proc.createEditorIfNeeded())
        {
            auto* gbEd = dynamic_cast<GhostbandEditor*> (ed);
            if (gbEd != nullptr)
            {
                gbEd->showScreenForSnapshot (0);
                ed->setSize (kMinW, kMinH);

                juce::String onScreen;
                for (int i = 0; i < ed->getNumChildComponents(); ++i)
                    if (auto* l = dynamic_cast<juce::Label*> (ed->getChildComponent (i)))
                        if (l->isVisible() && l->getText().contains ("to fix in this song"))
                            onScreen = l->getText();

                check (onScreen.isNotEmpty(),
                       "and it is on screen rather than only in the struct",
                       onScreen.isEmpty() ? juce::String ("the status line says nothing")
                                          : onScreen);
            }

            proc.editorBeingDeleted (ed);
            delete ed;
        }

        // And a clean song must say nothing, or the warning becomes wallpaper.
        proc.loadPlan (juce::File (planPath));
        check (proc.getStatus().planWarnings.isEmpty(),
               "a song with nothing wrong reports nothing",
               proc.getStatus().planWarnings.joinIntoString ("; "));

        // INCLUDING THE ONES THAT DO SOMETHING UNUSUAL ON PURPOSE.
        //
        // "4 chords do not divide evenly into 7 bars" is a typo in a rock song
        // and the entire point of a prog one. preset-prog-2 does it in seven
        // sections deliberately; surfacing that as a fault would have meant two
        // of the thirty-four shipped songs opening with a warning about
        // nothing, and a warning that fires on correct music is one nobody
        // reads. The observation still exists - the command line prints it -
        // it is just not a fault.
        {
            const juce::File prog ("C:/Projects/Ghostband/plans/preset-prog-2.json");
            if (prog.existsAsFile())
            {
                proc.loadPlan (prog);
                check (proc.getStatus().planWarnings.isEmpty(),
                       "a song whose odd bar counts are the point reports no faults",
                       proc.getStatus().planWarnings.joinIntoString ("; "));

                gb::SongPlan p;
                std::string err;
                if (gb::SongPlan::load (prog.getFullPathName().toStdString(), p, err))
                    check (! p.validate().empty() && p.faults().empty(),
                           "though the observation is still there for the command line",
                           juce::String (static_cast<int> (p.validate().size()))
                               + " notes, " + juce::String (static_cast<int> (p.faults().size()))
                               + " faults");

                proc.loadPlan (juce::File (planPath));
            }
        }

        bad.deleteFile();
    }

    // ---- an edited song says it is unsaved ---------------------------------
    // The flag behind this existed with six writers and no readers. Edits live
    // in memory only, so loading another song throws them away - silently,
    // until now.
    {
        check (! proc.planHasUnsavedEdits(),
               "a freshly loaded song is not marked as edited");

        auto edit = proc.getSectionEdit (0);
        edit.chords = "Em C G D";
        proc.applySectionEdit (0, edit);

        check (proc.planHasUnsavedEdits(),
               "editing a section marks the song as unsaved");

        // Reloading from disk is the same as never having edited it.
        proc.loadPlan (juce::File (planPath));
        check (! proc.planHasUnsavedEdits(),
               "and loading a song again clears the mark");
    }

    // ---- how long a beat is, without two locks to disagree ------------------
    // getBeatTicks took stateLock for the time signature and sequenceLock for
    // the bar length, deliberately not nested (that nesting was the deadlock
    // that froze the host). Separate acquisitions cannot deadlock and CAN
    // disagree: a regenerate between them returns a numerator from one song and
    // a bar length from another. It is published as one value now.
    {
        const int beat = proc.getBeatTicks();
        const int bar  = proc.getBarTicks();

        check (beat > 0 && bar > 0 && bar % beat == 0,
               "a bar is a whole number of beats",
               juce::String (bar) + " / " + juce::String (beat));

        // It must follow the song, not sit on a constant that happens to fit.
        const int wasNumerator = bar / beat;
        check (wasNumerator >= 2 && wasNumerator <= 16,
               "and the beat length matches the time signature",
               juce::String (wasNumerator) + " beats to the bar");

        // Hammering it against a running audio thread must never disagree with
        // the bar length, which is what the two-lock version could do.
        bool everWrong = false;
        for (int i = 0; i < 2000; ++i)
        {
            const int b = proc.getBeatTicks();
            if (b <= 0 || proc.getBarTicks() % b != 0) everWrong = true;
        }
        check (! everWrong, "and it stays consistent under repeated reads");
    }

    // ---- mappings can be put back to what the profile says ------------------
    // Only a CC can drift: the store holds controller numbers and the profile
    // supplies everything else, every load. So the reset undoes exactly two
    // things, and both are checked - a number that is stale, and a control the
    // profile no longer has. The second one has no other cure, because a
    // control the store knows about is put back on every load whatever the
    // profile says.
    //
    // The stale CC is built by writing a STORE, not by calling updateControl:
    // the form does not edit controller numbers, MIDI Learn does, so an edit
    // through the form could never produce this state and a test that used one
    // would pass without exercising anything.
    {
        const juce::File staleStore = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                          .getChildFile ("ghostband-harness")
                                          .getChildFile ("stale-store.json");
        staleStore.getParentDirectory().createDirectory();

        // Just the one control, on the wrong number. Everything else about the
        // guitar comes from its profile, which is the behaviour being relied on.
        staleStore.replaceWithText (
            "{\n"
            "  \"controls\": {\n"
            "    \"vg_iron2_instrument\": \"\\\"controls\\\": {\\n"
            "\\\"drive\\\": { \\\"cc\\\": 99, \\\"follows\\\": \\\"intensity\\\", "
            "\\\"type\\\": \\\"knob\\\", \\\"low\\\": 0, \\\"high\\\": 1 }\\n }\"\n"
            "  }\n"
            "}\n");

        GhostbandProcessor::setLearnedControlsFileForTesting (staleStore);
        {
            GhostbandProcessor stale;
            stale.loadPlan (juce::File (planPath));

            const int part = 2;      // guitar
            const int before = stale.getControlCount (part);

            check (before > 0, "the guitar has mappings to begin with",
                   juce::String (before));

            const juce::String summary = stale.controlDifferenceSummary (part);

            check (stale.controlsDifferingFromProfile (part) == 1
                       && summary.contains ("drive") && summary.contains ("99"),
                   "a stale controller number in the store is spotted",
                   summary.replace ("\n", "; "));

            // And a control the profile has never heard of, which is the case
            // that cannot be fixed by editing the profile.
            stale.addControl (part);
            check (stale.controlsDifferingFromProfile (part) == 2,
                   "so is a control the profile does not have",
                   stale.controlDifferenceSummary (part).replace ("\n", "; "));

            juce::String err;
            check (stale.resetControlsToProfile (part, err), "the reset runs", err);

            check (stale.controlsDifferingFromProfile (part) == 0,
                   "and afterwards nothing differs from the profile",
                   stale.controlDifferenceSummary (part).replace ("\n", "; "));

            check (stale.getControlCount (part) == before,
                   "the invented control is gone",
                   juce::String (stale.getControlCount (part)) + " vs " + juce::String (before));

            bool driveBack = false;
            for (int i = 0; i < stale.getControlCount (part); ++i)
                if (stale.getControl (part, i).name == "drive")
                    driveBack = stale.getControl (part, i).cc != 99;

            check (driveBack, "and the stale number is back to what the profile says");
        }

        // Never leave a test pointing at somebody's real mappings.
        GhostbandProcessor::setLearnedControlsFileForTesting (testStore);
        staleStore.deleteFile();
    }

    // ---- every change is written down, once ---------------------------------
    // Asked for in these words: "a running log that ghostband writes to in real
    // time - every time a change is made, it goes in the log, every change,
    // every time... a user can open the log and see what changes were made, at
    // what time they were made on what day".
    //
    // The two things that decide whether that is a log or a pile of noise are
    // both checked: it records what a value WAS as well as what it became, and
    // a control dragged across its range is ONE line rather than one per pixel.
    {
        const juce::File log = GhostbandProcessor::changeLogFile();
        log.deleteFile();

        if (auto* ed = proc.createEditorIfNeeded())
        {
            auto* gbEd = dynamic_cast<GhostbandEditor*> (ed);
            if (gbEd != nullptr)
            {
                gbEd->showScreenForSnapshot (0);
                ed->setSize (kMinW, kMinH);

                // Settling takes two ticks either side of the delay: one to
                // notice the value moved, one to find it has stopped moving.
                const auto settle = [gbEd]
                {
                    gbEd->runTimerForTesting();
                    juce::Thread::sleep (600);
                    gbEd->runTimerForTesting();
                };

                // The first look is the baseline and must say nothing - the
                // state at startup is not a change anybody made.
                settle();

                check (! log.existsAsFile() || log.loadFileAsString().trim().isEmpty(),
                       "opening the window logs nothing on its own",
                       log.existsAsFile() ? log.loadFileAsString().trim()
                                          : juce::String ("no file"));

                // A drag: many values in quick succession, then it settles.
                const int wasSeed = proc.seed.load();
                for (int i = 1; i <= 8; ++i)
                {
                    proc.levelBass.store (i / 10.0f);
                    gbEd->runTimerForTesting();
                }
                proc.seed.store (wasSeed + 1);
                settle();

                const juce::String text = log.existsAsFile() ? log.loadFileAsString()
                                                             : juce::String();
                const juce::StringArray lines = juce::StringArray::fromLines (text.trim());

                check (text.contains ("bass level") && text.contains ("80%"),
                       "a control that moved is in the log at the value it settled on",
                       text.trim().replace ("\n", " | "));

                int bassLines = 0;
                for (const juce::String& l : lines)
                    if (l.contains ("bass level")) ++bassLines;

                check (bassLines == 1,
                       "and a drag across its range is ONE line, not one per step",
                       juce::String (bassLines) + " lines for eight moves");

                check (text.contains ("seed") && text.contains ("->"),
                       "a change records what it was as well as what it became",
                       text.fromFirstOccurrenceOf ("seed", true, false)
                           .upToFirstOccurrenceOf ("\n", false, false));

                // The date and the time, because "when" was half the request.
                const juce::String today = juce::Time::getCurrentTime().formatted ("%Y-%m-%d");
                check (text.contains (today),
                       "and it is stamped with the day it happened", today);
            }

            proc.editorBeingDeleted (ed);
            delete ed;
        }

        log.deleteFile();
    }

    // ---- clicking a row of the grid edits the bar it is in -------------------
    // Asked for as "click on any line anywhere in the tracker and make quick
    // edits". What is editable there is what is AUTHORED - the chord under the
    // bar and the feel of its section - because a note in the grid is the
    // output of a seed and a plan, with nowhere to put a hand-placed one and no
    // way to keep it through a reroll.
    //
    // Driven through the view's own callback rather than by calling the open
    // function, so the wiring from click to strip is exercised as well.
    {
        proc.loadPlan (juce::File (planPath));

        const juce::File log = GhostbandProcessor::changeLogFile();
        log.deleteFile();

        if (auto* ed = proc.createEditorIfNeeded())
        {
            auto* gbEd = dynamic_cast<GhostbandEditor*> (ed);
            if (gbEd != nullptr)
            {
                gbEd->showScreenForSnapshot (0);      // song
                ed->setSize (kMinW, kMinH);

                const int barTicks = proc.getBarTicks();

                check (barTicks > 0, "the song has bars to click on",
                       juce::String (barTicks));

                // The third bar, which is far enough in to be inside a real
                // section rather than on a boundary.
                const int wantedBar = 2;
                gbEd->clickTrackerRowForTesting (wantedBar * barTicks);

                check (gbEd->trackerEditBarForTesting() == wantedBar,
                       "clicking a row opens the strip on the bar that row is in",
                       juce::String (gbEd->trackerEditBarForTesting() + 1));

                const juce::String was = proc.chordAtBar (wantedBar);

                check (was.isNotEmpty(),
                       "a bar always has a chord, even in a section that chose its own",
                       was);

                check (gbEd->trackerChordForTesting() == was,
                       "and the strip shows the one that is actually playing there",
                       gbEd->trackerChordForTesting() + " vs " + was);

                // Change it. Something that is definitely not what was there.
                const juce::String wanted = was == "Bb" ? "Db" : "Bb";
                gbEd->typeTrackerChordForTesting (wanted);

                check (proc.chordAtBar (wantedBar) == wanted,
                       "typing a chord changes that bar",
                       proc.chordAtBar (wantedBar));

                // ...and ONLY that bar. A section whose chords were automatic
                // gets pinned to what it was already playing, so its
                // neighbours must come back exactly as they were.
                const int section = proc.sectionIndexForBar (wantedBar);
                const auto secs = proc.getSections();
                bool neighboursHeld = true;
                juce::String moved;

                if (section >= 0)
                {
                    const auto& rep = secs[static_cast<size_t> (section)];
                    for (int b = rep.startBar; b < rep.startBar + rep.bars; ++b)
                    {
                        if (b == wantedBar) continue;
                        if (proc.chordAtBar (b).isEmpty()) { neighboursHeld = false; moved = "bar "
                            + juce::String (b + 1) + " lost its chord"; }
                    }
                }

                check (neighboursHeld,
                       "and every other bar of that section keeps a chord of its own", moved);

                // It is a change, so it is in the log, with what it was.
                const juce::String text = log.existsAsFile() ? log.loadFileAsString()
                                                             : juce::String();
                check (text.contains ("chord at bar " + juce::String (wantedBar + 1))
                           && text.contains (was) && text.contains (wanted),
                       "the edit goes in the change log with what it was before",
                       text.trim().replace ("\n", " | "));

                // Clicking the same row again closes it, which is what a thing
                // that opened on a click should do.
                gbEd->clickTrackerRowForTesting (wantedBar * barTicks);
                check (gbEd->trackerEditBarForTesting() < 0,
                       "clicking the same row again closes the strip",
                       juce::String (gbEd->trackerEditBarForTesting()));

                // And every control on it explains itself, which the tooltip
                // sweep cannot see because it skips anything not on screen.
                gbEd->clickTrackerRowForTesting (wantedBar * barTicks);
                ed->setSize (kMinW, kMinH);

                juce::StringArray silent;
                for (int i = 0; i < ed->getNumChildComponents(); ++i)
                {
                    juce::Component* c = ed->getChildComponent (i);
                    if (c == nullptr || ! c->isVisible() || c->getBounds().isEmpty())
                        continue;

                    const bool isControl = dynamic_cast<juce::Button*> (c)     != nullptr
                                        || dynamic_cast<juce::ComboBox*> (c)   != nullptr
                                        || dynamic_cast<juce::TextEditor*> (c) != nullptr;
                    if (! isControl) continue;

                    auto* t = dynamic_cast<juce::SettableTooltipClient*> (c);
                    if (t == nullptr || t->getTooltip().isEmpty())
                        silent.add (describeComponent (c));
                }

                check (silent.isEmpty(),
                       "every control on the open edit strip explains itself",
                       silent.joinIntoString ("; "));

                gbEd->clickTrackerRowForTesting (wantedBar * barTicks);
            }

            proc.editorBeingDeleted (ed);
            delete ed;
        }

        log.deleteFile();

        // The chord edit changed the song, so put it back for whatever runs
        // next - the reference counts downstream depend on it.
        proc.loadPlan (juce::File (planPath));
    }

    // ---- the transport button says what is true ----------------------------
    // Its text was set only inside its own onClick handler, so it started life
    // reading "Pause" and stayed there until somebody pressed it. Pause the
    // band, close the plugin window, reopen it, and you get a fresh editor
    // reading "Pause" over a band that is already paused - the one control
    // whose whole job is to say which of two states you are in, stating the
    // opposite, with no way out but pressing it twice.
    //
    // Read the way a person reads it: find the button by its text among the
    // editor's children. Asking the editor what it thinks the text is would
    // pass on a button that never made it to the screen.
    {
        const auto transportText = [] (juce::AudioProcessorEditor* ed) -> juce::String
        {
            for (int i = 0; i < ed->getNumChildComponents(); ++i)
                if (auto* b = dynamic_cast<juce::TextButton*> (ed->getChildComponent (i)))
                    if (b->isVisible() && (b->getButtonText() == "Play"
                                           || b->getButtonText() == "Pause"))
                        return b->getButtonText();
            return {};
        };

        proc.paused.store (true);

        if (auto* ed = proc.createEditorIfNeeded())
        {
            auto* gbEd = dynamic_cast<GhostbandEditor*> (ed);
            if (gbEd != nullptr)
            {
                gbEd->showScreenForSnapshot (0);      // song
                ed->setSize (kMinW, kMinH);
                gbEd->runTimerForTesting();

                check (transportText (ed) == "Play",
                       "a window opened while the band is paused offers to PLAY",
                       transportText (ed));

                proc.paused.store (false);
                gbEd->runTimerForTesting();

                check (transportText (ed) == "Pause",
                       "and it goes back to PAUSE when the band is running",
                       transportText (ed));
            }

            proc.editorBeingDeleted (ed);
            delete ed;
        }

        proc.paused.store (false);
    }

    // ---- one row of the tracker is as much music as you asked for -----------
    // Four zooms, and the only one that was ever exercised was the middle one.
    // The arithmetic that places a note in a row is the same at every zoom and
    // the arithmetic that labels the row is not, so a bad label is the likely
    // failure and it is invisible unless the counts are compared.
    {
        const int beat = proc.getBeatTicks();
        const int bar  = proc.getBarTicks();

        const std::vector<int> channels {
            proc.channelDrums.load(), proc.channelBass.load(),
            proc.channelGuitar.load(), proc.channelGuitar2.load(),
            proc.channelPiano.load() };

        check (bar > beat && beat > 4,
               "a bar is more than a beat and a beat has room to divide",
               juce::String (beat) + " / " + juce::String (bar));

        // The same eight bars, read at four resolutions. However finely it is
        // sliced, the same notes must be accounted for - a zoom that loses a
        // hit is worse than no zoom at all.
        const int spanTicks = 8 * bar;
        int totals[4] = { 0, 0, 0, 0 };
        const int perRow[4] = { bar, beat, juce::jmax (1, beat / 2), juce::jmax (1, beat / 4) };

        for (int z = 0; z < 4; ++z)
        {
            const int rows = spanTicks / perRow[z];
            const auto cells = proc.getTrackerCells (0, rows, channels, perRow[z]);
            for (const auto& c : cells)
                totals[z] += c.hits;
        }

        check (totals[0] == totals[1] && totals[1] == totals[2] && totals[2] == totals[3],
               "every zoom accounts for exactly the same notes",
               juce::String (totals[0]) + " / " + juce::String (totals[1]) + " / "
                   + juce::String (totals[2]) + " / " + juce::String (totals[3]));

        check (totals[0] > 0, "and there were notes there to account for",
               juce::String (totals[0]));

        // A coarser row holds more per cell. If this ever came out equal, the
        // resolution would be having no effect and the selector would be a
        // control that does nothing - the exact fault this session started on.
        const auto busiest = [&] (int ticksPerRow)
        {
            const int rows = spanTicks / ticksPerRow;
            const auto cells = proc.getTrackerCells (0, rows, channels, ticksPerRow);
            int most = 0;
            for (const auto& c : cells) most = juce::jmax (most, c.hits);
            return most;
        };

        check (busiest (bar) > busiest (juce::jmax (1, beat / 4)),
               "a bar per row packs more into a cell than a sixteenth per row",
               juce::String (busiest (bar)) + " vs "
                   + juce::String (busiest (juce::jmax (1, beat / 4))));

        // ---- A KEYSWITCH IS NOT A NOTE, AND THE GRID MUST NOT SAY IT IS ----
        //
        // "you'll see A7 and friends in the GTR 2 column - those are the
        // keyswitches, not played notes". True at the time and defended as the
        // grid showing the wire, which it is. It still read as the second
        // guitar playing a note two octaves above anything it owns, and it was
        // worse than cosmetic: a switch goes out at a FIXED velocity, so it won
        // the one line a cell has and hid a quieter note that really was
        // played.
        //
        // So a switch now has its own field, the note field is music only, and
        // the hit count counts strikes rather than wiring.
        {
            int switchesSeen = 0, playedTooHigh = 0, hitsOnSwitchOnly = 0;

            // THE WHOLE SONG, not the eight bars above. A keyswitch goes out at
            // the top of a section, and the first eight bars of a song are one
            // or two sections - so a window that size can legitimately contain
            // none and the check would pass by seeing nothing, which is the
            // failure mode a check like this exists to avoid.
            const int rows = juce::jmax (1, proc.getStatus().bars);
            const auto cells = proc.getTrackerCells (0, rows, channels, bar);

            for (const auto& c : cells)
            {
                if (c.switchKind != gb::PhraseProfile::SwitchKind::None)
                {
                    ++switchesSeen;

                    // A cell whose only event was a switch must report no hits.
                    if (c.note < 0 && c.hits > 0)
                        ++hitsOnSwitchOnly;
                }

                // Nothing in the NOTE field may sit above the top of a guitar
                // neck. That is the shape the fault had, and it is the shape a
                // regression would have.
                if (c.note >= 104)
                    ++playedTooHigh;
            }

            check (switchesSeen > 0,
                   "the grid can see a keyswitch at all",
                   juce::String (switchesSeen) + " cells carry one");

            check (playedTooHigh == 0,
                   "and a keyswitch never reaches the grid as a note that was played",
                   juce::String (playedTooHigh) + " cells show an unplayable note");

            check (hitsOnSwitchOnly == 0,
                   "and wiring is not counted as a strike",
                   juce::String (hitsOnSwitchOnly) + " cells count a switch as a hit");
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

    // ---- editing a section leaves the second guitar alone --------------------
    // The structure editor has toggles for drums, bass, guitar and piano and
    // none for guitar 2 - the same shape of gap that has now bitten five other
    // lists written before the second guitar existed. The dangerous version of
    // that would be applySectionEdit writing all five from four toggles and
    // silently clearing the fifth, which would delete the solo from a song by
    // opening the editor and changing its name.
    //
    // It does not: playsGuitar2 is not touched at all. This is what says so, so
    // that adding the missing toggle later cannot quietly introduce the bad
    // version of it.
    {
        proc.loadPlan (juce::File (planPath));

        int target = -1;
        const auto sections = proc.getSections();
        for (size_t i = 0; i < sections.size(); ++i)
            if (sections[i].guitar2Feel != "silent") { target = static_cast<int> (i); break; }

        check (target >= 0, "the reference song has a section with a second guitar",
               target >= 0 ? sections[static_cast<size_t> (target)].name
                           : juce::String ("none found"));

        if (target >= 0)
        {
            const juce::String was = sections[static_cast<size_t> (target)].guitar2Feel;

            // Exactly what the editor sends: every field it knows about, which
            // is every field except this one.
            GhostbandProcessor::SectionEdit e = proc.getSectionEdit (target);
            e.bars = juce::jmax (1, e.bars);
            proc.applySectionEdit (target, e);

            const auto after = proc.getSections();
            check (after[static_cast<size_t> (target)].guitar2Feel == was,
                   "and a round trip through the editor does not silence it",
                   was + " -> " + after[static_cast<size_t> (target)].guitar2Feel);
        }
    }

    // ---- your songs are kept apart from the ones that ship -------------------
    // Save as... used to open on whatever song was loaded, so starting from a
    // preset put the dialog inside the bundle under Program Files - which needs
    // elevation to write, and which the installer then has to be careful not to
    // sweep away again.
    {
        const juce::File songs = GhostbandProcessor::userSongsFolder();

        check (songs.isDirectory(), "the user songs folder is created on demand",
               songs.getFullPathName());
        // Only askable where a bundle actually exists. Where it does not,
        // bundledPlansFolder() IS the Documents fallback and the songs folder
        // is legitimately inside it - which is the whole reason planIsFactory
        // refuses to ask the question that way, checked immediately below.
        const juce::File bundle = proc.bundledPlansFolder();
        const bool realBundle = bundle.getFileName() == "plans"
                             && bundle.getParentDirectory().getFileName() == "Resources";

        if (realBundle)
            check (! songs.isAChildOf (bundle) && songs != bundle,
                   "and is not inside the folder the presets ship in",
                   bundle.getFullPathName());
        else
            check (true, "no bundle in this tree, so the presets folder is the fallback",
                   "which is why planIsFactory does not use it");

        // The trap this is really here for. bundledPlansFolder() falls back to
        // Documents when there is no bundle, and the songs folder is under
        // Documents - so a factory test written against that fallback answers
        // yes for every song you have ever written, and Save never saves again.
        proc.loadPlan (juce::File (planPath));
        check (! proc.planIsFactory(),
               "a song loaded from outside a bundle is not a factory preset",
               proc.getPlanFile().getFullPathName());

        const juce::File mine = songs.getChildFile ("harness-song.json");
        mine.deleteFile();
        juce::String err;
        check (proc.savePlan (mine, err), "a song saves into your songs folder", err);
        check (mine.existsAsFile(), "and the file is really there");

        proc.loadPlan (mine);
        check (! proc.planIsFactory(),
               "a song in your own folder is never treated as a factory preset",
               "this is what would make Save stop working");

        mine.deleteFile();
        juce::File (mine.getParentDirectory().getChildFile ("backups")).deleteRecursively();
        proc.loadPlan (juce::File (planPath));
    }

    // ---- the UI and the audio thread do not deadlock ---------------------------
    // Gig Performer froze solid and stopped answering. The cause was a
    // lock-order inversion introduced the same day:
    //
    //     audio thread   processBlock:  sequenceLock  ->  stateLock
    //     message thread getBeatTicks:  stateLock     ->  sequenceLock
    //
    // Interleave those and both stop forever. It does not present as an audio
    // glitch: the message thread SPINS on a SpinLock it can never take, so the
    // whole host stops repainting and stops answering the mouse. The tracker
    // called it thirty times a second, so it was a matter of minutes.
    //
    // THE LOCK ORDER RULE, since it is not otherwise written down anywhere:
    // never hold sequenceLock and stateLock at the same time, in either order,
    // and never let the audio thread block on stateLock at all.
    //
    // IF THIS TEST HANGS, that rule has been broken again. A hang is the
    // failure - there is no way to report a deadlock from inside one.
    {
        proc.loadPlan (juce::File (planPath));
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        FakePlayHead h;
        h.bpm = proc.getPlanBpm();
        h.playing = true;
        proc.setPlayHead (&h);

        struct AudioThread : public juce::Thread
        {
            AudioThread (GhostbandProcessor& p, FakePlayHead& head, double sr, int bs)
                : juce::Thread ("audio"), proc (p), ph (head), rate (sr), block (bs) {}

            void run() override
            {
                juce::AudioBuffer<float> buf (2, block);
                juce::MidiBuffer mb;
                const double perBlock = (block / rate) * (ph.bpm / 60.0);

                for (int i = 0; i < 4000 && ! threadShouldExit(); ++i)
                {
                    ph.ppq = i * perBlock;
                    buf.clear(); mb.clear();
                    proc.processBlock (buf, mb);
                }
                finished = true;
            }

            GhostbandProcessor& proc;
            FakePlayHead& ph;
            double rate; int block;
            std::atomic<bool> finished { false };
        };

        AudioThread audio (proc, h, sampleRate, blockSize);
        audio.startThread();

        // Everything the editor's timer touches, as hard as it can, while the
        // audio thread is running.
        const std::vector<int> chans { proc.channelDrums.load(), proc.channelBass.load(),
                                       proc.channelGuitar.load(), proc.channelGuitar2.load(),
                                       proc.channelPiano.load() };
        int reads = 0;
        for (int i = 0; i < 4000; ++i)
        {
            reads += proc.getBeatTicks() > 0 ? 1 : 0;
            reads += proc.getBarTicks()  > 0 ? 1 : 0;
            (void) proc.getTrackerCells (i * 96, 20, chans);
            (void) proc.getSections();
            (void) proc.getStatus();
        }

        const bool done = audio.waitForThreadToExit (10000);
        if (! done) audio.stopThread (2000);

        check (done, "the UI can hammer the processor while it plays, without deadlocking",
               juce::String (reads) + " reads against 4000 audio blocks");

        h.playing = false;
        proc.setPlayHead (&h);
        juce::AudioBuffer<float> b (2, blockSize);
        juce::MidiBuffer m;
        proc.processBlock (b, m);
    }

    // ---- the playhead stops when the song does ---------------------------------
    // Reported: "when the song ends, the playhead just keeps going, only the
    // arrangement is blank." A rackspace transport is left running for a whole
    // set, so the host does not stop when the song does - and a playhead
    // sweeping through empty space looks exactly like the plugin still playing
    // something you cannot hear.
    {
        proc.loadPlan (juce::File (planPath));

        // BOTH, and the comment at the top of this file says why: prepareToPlay
        // alone leaves getSampleRate() at zero, and processBlock then returns
        // before it ever advances anything. Walked straight into it.
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        FakePlayHead h;
        h.bpm = proc.getPlanBpm();
        h.playing = true;
        proc.setPlayHead (&h);

        const auto sections = proc.getSections();
        const int endTick = sections.empty() ? 0 : sections.back().endTick;
        check (endTick > 0, "the song has an end to reach", juce::String (endTick));

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer mb;

        // Walk to just before the end, then well past it.
        const double quartersPerBlock = (blockSize / sampleRate) * (h.bpm / 60.0);
        const double endQuarter = endTick / double (gb::kPPQ);

        int atEnd = -1, wayPast = -1;
        bool finishedAtEnd = false, finishedPast = false;

        int blocksRun = 0;
        for (double q = 0.0; q < endQuarter * 1.5; q += quartersPerBlock)
        {
            h.ppq = q;
            buf.clear(); mb.clear();
            proc.processBlock (buf, mb);
            ++blocksRun;

            const int t = proc.playbackTick.load();
            if (atEnd < 0 && proc.songFinished.load())
            {
                atEnd = t;
                finishedAtEnd = true;
            }
        }
        wayPast = proc.playbackTick.load();
        finishedPast = proc.songFinished.load();

        check (finishedAtEnd && finishedPast,
               "the plugin knows the song has finished",
               juce::String (blocksRun) + " blocks played");

        check (atEnd <= endTick,
               "the playhead stops at the last bar instead of running on",
               juce::String (atEnd) + " with the song ending at " + juce::String (endTick));

        // AND THEN IT GOES BACK TO THE TOP, PAUSED. Asked for in these words:
        // "when a song finishes playing, it should automatically move the
        // playhead back to the beginning, so i then have to only press play
        // again for it to start the song once more."
        //
        // The rewind happens once the last chord has been RELEASED, not at the
        // last bar line - rewinding at the bar line would cut the final chord
        // off mid-ring, so the two events are deliberately apart and both are
        // checked.
        check (wayPast == 0,
               "and once the last chord has released it parks back at the top",
               juce::String (wayPast));

        check (proc.paused.load(),
               "and pauses itself rather than looping round again");

        // One button, which is the whole point. Not two actions in the host.
        proc.togglePaused();

        check (! proc.songFinished.load(),
               "pressing play clears the notice that the song had ended");

        h.playing = true;
        h.ppq = 0.0;
        buf.clear(); mb.clear();
        proc.processBlock (buf, mb);

        // Long enough to reach the first note of a SPARSE opening. Forty blocks
        // was enough for demo-metal, which starts on a downbeat, and not for
        // the ballads, which open on a few piano notes at low intensity - so
        // the check failed on three of the thirty-four plans while the song was
        // playing perfectly. "No note in the next fraction of a second" is not
        // the same claim as "silent", and the test was making the wrong one.
        int firstNoteAt = -1;
        for (int i = 0; i < 400 && firstNoteAt < 0; ++i)
        {
            for (const juce::MidiMessageMetadata m : mb)
                if (m.getMessage().isNoteOn()) firstNoteAt = proc.playbackTick.load();

            if (firstNoteAt < 0)
            {
                buf.clear(); mb.clear();
                proc.processBlock (buf, mb);
            }
        }

        check (firstNoteAt >= 0 && firstNoteAt < endTick / 4,
               "and it plays again from the beginning",
               firstNoteAt < 0 ? juce::String ("SILENT for 400 blocks")
                               : "first note at tick " + juce::String (firstNoteAt)
                                     + " of " + juce::String (endTick));

        h.playing = false;
        proc.setPlayHead (&h);
    }

    // ---- a song is called what its file says ----------------------------------
    // Every generated preset writes "name"; this only ever read "title". So all
    // thirty-four were called "Untitled" wherever a song's name is shown, which
    // surfaced in the take library as two takes of different songs both reading
    // "Untitled" - the one thing that list exists to tell apart.
    {
        int untitled = 0;
        juce::StringArray named;

        const juce::File plansDir = juce::File (planPath).getParentDirectory();
        for (const juce::File& f : plansDir.findChildFiles (juce::File::findFiles, false, "*.json"))
        {
            if (f.getFileName().contains ("previous")) continue;

            gb::SongPlan plan;
            std::string error;
            if (! gb::SongPlan::load (f.getFullPathName().toStdString(), plan, error))
                continue;

            if (juce::String (plan.title) == "Untitled") ++untitled;
            else if (named.size() < 3) named.add (juce::String (plan.title));
        }

        check (untitled == 0, "every shipped song knows its own name",
               untitled == 0 ? named.joinIntoString (", ") + ", ..."
                             : juce::String (untitled) + " still say Untitled");
    }

    // ---- nothing is written outside what the instrument can play ---------------
    // Shreddage's profile said its chord zone bottomed out at 28 for weeks. The
    // guitar's lowest string is E1 = 40, drop-tuned; there is nothing below it.
    // So 166 notes across 19 presets were written into a region with no samples
    // in it, and the notes immediately below that region are not empty either -
    // 24 to 27 are FX keyswitches, and 27 is "thrash", which RE-TRIGGERS THE
    // LAST-PLAYED NOTE. A generator that wanders down there does not merely go
    // quiet, it changes what the instrument does next.
    //
    // Swept across every shipped plan rather than the loaded one, because that
    // is where the 166 were hiding.
    //
    // WHAT THIS DOES NOT CATCH, said plainly so nobody trusts it further than it
    // goes: it asserts the generator stays inside the range the PROFILE
    // declares. It cannot tell you the profile is right. Dropping the declared
    // floor back to 28 makes this pass again, because the notes are then inside
    // a zone that is itself wrong. Only the manual, or an ear, settles that -
    // which is exactly how this went unnoticed for five sessions.
    {
        struct Part { int index; const char* what; };
        const Part parts[] = { { 2, "guitar" }, { 3, "piano" }, { 4, "guitar 2" } };

        juce::StringArray strays;
        int checkedPlans = 0, notesChecked = 0;

        const juce::File plansDir = juce::File (planPath).getParentDirectory();
        for (const juce::File& f : plansDir.findChildFiles (juce::File::findFiles, false, "*.json"))
        {
            if (f.getFileName().contains ("previous")) continue;

            proc.loadPlan (f);
            if (! proc.getStatus().ok) continue;
            ++checkedPlans;

            for (const Part& part : parts)
            {
                const auto range = proc.getPlayableRange (part.index);
                if (range.isEmpty()) continue;

                const int ch = part.index == 2 ? proc.channelGuitar.load()
                             : part.index == 3 ? proc.channelPiano.load()
                                               : proc.channelGuitar2.load();
                if (ch < 1) continue;

                // MUSIC ONLY. A keyswitch sits outside the playable range on
                // purpose - that is what stops it being heard - so asking for
                // the lowest and highest NOTE-ON reports every one of them as a
                // note the instrument cannot play. Hydra's fretting-mode
                // switches at 114-117 made this check fail on all thirty-four
                // plans at once, which is the right answer to the wrong
                // question.
                const auto played = proc.getPlayedNoteRange (part.index);
                if (played.isEmpty()) continue;   // part silent in this song

                const int lowest  = played.getStart();
                const int highest = played.getEnd();

                notesChecked += proc.getSequenceNoteOnCount (ch);

                if (lowest < range.getStart() || highest > range.getEnd())
                    strays.add (f.getFileNameWithoutExtension() + " " + part.what + " wrote "
                                + juce::String (lowest) + ".." + juce::String (highest)
                                + " into " + juce::String (range.getStart()) + ".."
                                + juce::String (range.getEnd()));
            }
        }

        check (checkedPlans > 20, "every shipped plan was swept",
               juce::String (checkedPlans) + " plans, "
                   + juce::String (notesChecked) + " notes");

        // AND THE SWITCHES ARE ACTUALLY GOING OUT. The filter above stops
        // keyswitches being reported as stray notes; without this it would also
        // hide a profile that had stopped sending them at all, which is the
        // same silence the fretting modes are meant to end.
        {
            proc.loadPlan (juce::File (planPath));

            int fretting = 0;
            for (int n = 114; n <= 117; ++n)
                fretting += proc.getSequenceNoteOnCount (proc.channelGuitar2.load(), n)
                          - proc.getSequenceNoteOnCount (proc.channelGuitar2.load(), n + 1);

            check (fretting > 0,
                   "the second guitar is told where on the neck to play",
                   juce::String (fretting) + " fretting-mode keyswitches");

            // And where the HAND sits, which is the same note every time with
            // the fret as its velocity (manual p28). One note, so counting it
            // is enough; the velocities are what differ and they are the
            // profile's business.
            const int ch11 = proc.channelGuitar2.load();
            const int hand = proc.getSequenceNoteOnCount (ch11, 10)
                           - proc.getSequenceNoteOnCount (ch11, 11);

            check (hand > 0,
                   "and where on the neck its hand should sit",
                   juce::String (hand) + " hand-position keyswitches");
        }

        check (strays.isEmpty(),
               "no plan writes a note the instrument cannot play",
               strays.isEmpty() ? juce::String ("all inside their chord zones")
                                : strays.joinIntoString ("; "));

        proc.loadPlan (juce::File (planPath));
    }

    // ---- the take library ----------------------------------------------------
    // A take promises one thing: that what you heard comes back. Everything
    // below is that promise taken apart - the performance is identical, the
    // library holds more than one, a name used twice means one take, and the
    // rig is left alone.
    {
        const juce::File takesStore = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                          .getChildFile ("ghostband-harness")
                                          .getChildFile ("takes.json");
        takesStore.deleteFile();
        GhostbandProcessor::setTakesFileForTesting (takesStore);

        check (proc.getTakes().empty(), "a fresh library is empty");

        juce::String error;

        // Cleared before each save, or a later success reports the message from
        // an earlier failure and the run reads as though every take failed.
        const auto saved = [&proc, &error] (const juce::String& name)
        {
            error.clear();
            return proc.saveTake (name, error);
        };

        check (! saved ("   "), "a take with no name is refused", error);
        check (proc.getTakes().empty(), "and nothing was written");

        // A fingerprint of the whole performance, not of one part. Note counts
        // alone would pass for a song playing the right number of wrong notes,
        // so the pitch sums go in beside them - a transposed or re-rolled part
        // moves the sum even when the count holds.
        const auto fingerprint = [&proc]
        {
            juce::String f;
            for (int ch = 1; ch <= 16; ++ch)
            {
                const int n = proc.getSequenceNoteOnCount (ch);
                if (n > 0)
                    f << ch << ":" << n << "/" << proc.getSequencePitchSum (ch) << " ";
            }
            return f.trim();
        };

        proc.loadPlan (juce::File (planPath));
        proc.seed.store (88345);
        proc.complexity.store (0.73);
        proc.humanize.store   (0.31);
        proc.fills.store      (0.90);
        proc.regenerate();

        const juce::String wanted = fingerprint();
        check (wanted.isNotEmpty(), "the performance to be saved is not silent", wanted);

        const juce::String wantedCC = proc.getSequenceControllers (proc.channelGuitar.load());

        check (saved ("chunky verse"), "a take saves", error);
        check (proc.getTakes().size() == 1, "and the library has one take",
               juce::String (static_cast<int> (proc.getTakes().size())));

        // Everything a take carries, moved somewhere else. If recall reads any
        // of it from the live plugin rather than from the take, this is what
        // catches it.
        proc.seed.store (11);
        proc.complexity.store (0.10);
        proc.humanize.store   (0.90);
        proc.fills.store      (0.00);
        proc.setKeyPitchClass (7);
        proc.setPlanBpm (171.0);
        proc.regenerate();

        check (fingerprint() != wanted, "moving the dials really does change the song",
               "was " + wanted);

        proc.recallTake (0);

        check (fingerprint() == wanted, "and recalling the take brings it back note for note",
               fingerprint());

        // The TONE, which is a different claim from the performance. Reported:
        // a recalled take plays the same notes through different guitar
        // effects. Ghostband picks an amp and an effect per song from the seed,
        // so if the notes come back and the controls do not, the fault is in
        // how they are sent rather than in how they are chosen.
        check (proc.getSequenceControllers (proc.channelGuitar.load()) == wantedCC,
               "and hands the guitar the same controls it did before",
               proc.getSequenceControllers (proc.channelGuitar.load()));

        // One tone per take, not one per section.
        //
        // A control that follows "random once" is chosen from the SONG seed and
        // held; one that follows "random" is chosen from the SECTION seed and
        // re-chosen at every boundary. The guitar's effect shaping was on the
        // second, so which effect was picked once per song while how much of it
        // moved every eight bars - the riff was identical and the tone was not,
        // which is what made a recalled take sound wrong.
        {
            std::map<int, std::set<int>> valuesFor;
            for (const juce::String& pair
                     : juce::StringArray::fromTokens (
                           proc.getSequenceControllers (proc.channelGuitar.load()), " ", ""))
            {
                if (! pair.contains ("=")) continue;
                valuesFor[pair.upToFirstOccurrenceOf ("=", false, false).getIntValue()]
                    .insert (pair.fromFirstOccurrenceOf ("=", false, false).getIntValue());
            }

            // The intensity-driven ones SHOULD move: drive and pickup follow how
            // hard the section is played, which is the arrangement doing its job.
            const std::set<int> mayMove { 22, 25 };

            juce::StringArray drifting;
            for (const auto& entry : valuesFor)
                if (entry.second.size() > 1 && mayMove.count (entry.first) == 0)
                    drifting.add ("CC " + juce::String (entry.first) + " took "
                                  + juce::String (static_cast<int> (entry.second.size()))
                                  + " values");

            check (drifting.isEmpty(),
                   "the guitar keeps one tone for the whole song",
                   drifting.isEmpty() ? juce::String (static_cast<int> (valuesFor.size()))
                                            + " controls, only drive and pickup move"
                                      : drifting.joinIntoString ("; "));
        }
        check (proc.seed.load() == 88345, "the seed came back",
               juce::String (proc.seed.load()));
        check (std::abs (proc.complexity.load() - 0.73) < 0.001
                   && std::abs (proc.humanize.load() - 0.31) < 0.001
                   && std::abs (proc.fills.load() - 0.90) < 0.001,
               "and so did all three dials",
               juce::String (proc.complexity.load(), 2) + " "
                   + juce::String (proc.humanize.load(), 2) + " "
                   + juce::String (proc.fills.load(), 2));

        // Tempo and key live in the plan, not in an atomic, and the Edit screen
        // can change them without anything being saved to disk. A take that
        // stored only a path to the song would have recalled them at 171bpm in
        // G, which is a different song rather than a different take.
        check (std::abs (proc.getPlanBpm() - 171.0) > 0.5,
               "the tempo came back with the take, not from the live plugin",
               juce::String (proc.getPlanBpm(), 1) + " bpm");

        // ---- the rig is not part of a take ----
        proc.levelGuitar.store (0.42f);
        proc.channelGuitar.store (9);
        proc.applyChannels();
        proc.recallTake (0);
        check (std::abs (proc.levelGuitar.load() - 0.42f) < 0.01f
                   && proc.channelGuitar.load() == 9,
               "recalling a take leaves the mix and the channels alone",
               juce::String (proc.levelGuitar.load(), 2) + " on ch"
                   + juce::String (proc.channelGuitar.load()));
        proc.levelGuitar.store (1.0f);
        proc.channelGuitar.store (2);
        proc.applyChannels();

        // ---- a second take, and a name used twice ----
        proc.seed.store (4242);
        proc.regenerate();
        const juce::String second = fingerprint();
        check (saved ("open chorus"), "a second take saves", error);
        check (proc.getTakes().size() == 2, "the library holds both",
               juce::String (static_cast<int> (proc.getTakes().size())));

        proc.seed.store (777);
        proc.regenerate();
        check (saved ("CHUNKY VERSE"), "saving over a name succeeds", error);
        check (proc.getTakes().size() == 2,
               "and replaces that take rather than making a second of the same name",
               juce::String (static_cast<int> (proc.getTakes().size())));

        // The one that was overwritten must now recall the NEW performance, and
        // the untouched one must be exactly where it was. Overwriting the wrong
        // row is the failure this catches.
        const auto indexOf = [&proc] (const juce::String& name)
        {
            const auto all = proc.getTakes();
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].name.equalsIgnoreCase (name)) return static_cast<int> (i);
            return -1;
        };

        proc.seed.store (1);
        proc.regenerate();
        proc.recallTake (indexOf ("open chorus"));
        check (fingerprint() == second, "the take that was not overwritten is untouched");

        proc.recallTake (indexOf ("chunky verse"));
        check (proc.seed.load() == 777, "and the overwritten one holds the newer performance",
               juce::String (proc.seed.load()));

        // ---- names people actually type ----
        // A double quote in a control name destroyed a profile once, because it
        // was spliced into JSON unescaped. Takes are written through
        // JSON::toString, which escapes - and this is what says so.
        const juce::String hostile = "Kyle's \"big\" take \\ 2am";
        check (saved (hostile), "a take with quotes and a backslash saves", error);
        {
            const auto all = proc.getTakes();
            const bool found = std::any_of (all.begin(), all.end(),
                                            [&hostile] (const GhostbandProcessor::Take& t)
                                            { return t.name == hostile; });
            check (found, "and reads back with its name intact", hostile);
            check (all.size() == 3, "without damaging the takes either side of it",
                   juce::String (static_cast<int> (all.size())));
        }

        // ---- a take outlives the file it came from ----
        // The song is stored in the take, so a preset that is moved, renamed or
        // deleted does not take the saved performances with it.
        {
            const juce::File moved = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                         .getChildFile ("ghostband-harness")
                                         .getChildFile ("temporary-song.json");
            moved.getParentDirectory().createDirectory();
            juce::File (planPath).copyFileTo (moved);

            proc.loadPlan (moved);
            proc.seed.store (5150);
            proc.regenerate();
            const juce::String orphan = fingerprint();
            check (saved ("from a song since deleted"), "a take of it saves", error);

            moved.deleteFile();
            proc.loadPlan (juce::File (planPath));
            proc.seed.store (2);
            proc.regenerate();

            proc.recallTake (indexOf ("from a song since deleted"));
            check (fingerprint() == orphan,
                   "a take still plays after its song file is deleted", fingerprint());
            check (proc.getStatus().ok, "and the plugin is not left in an error state",
                   proc.getStatus().message);
        }

        // ---- delete ----
        {
            const int before = static_cast<int> (proc.getTakes().size());
            const int target = indexOf ("open chorus");
            proc.deleteTake (target);
            const auto after = proc.getTakes();
            check (static_cast<int> (after.size()) == before - 1, "delete removes one take",
                   juce::String (before) + " -> " + juce::String (static_cast<int> (after.size())));
            check (indexOf ("open chorus") < 0, "and removes the one that was asked for");
            check (indexOf ("chunky verse") >= 0, "leaving the others alone");

            // Out of range must do nothing rather than take a neighbour with it.
            const int n = static_cast<int> (proc.getTakes().size());
            proc.deleteTake (-1);
            proc.deleteTake (n + 5);
            check (static_cast<int> (proc.getTakes().size()) == n,
                   "deleting a take that is not there deletes nothing");
            proc.recallTake (-1);
            proc.recallTake (n + 5);
            check (proc.getStatus().ok, "and recalling one that is not there does nothing",
                   proc.getStatus().message);
        }

        // ---- the library is on disk, not in the plugin ----
        // Two Ghostbands in one rackspace is normal. The takes are re-read on
        // every call precisely so the second instance sees the first one's
        // saves instead of each holding a private copy.
        {
            GhostbandProcessor other;
            check (other.getTakes().size() == proc.getTakes().size()
                       && ! other.getTakes().empty(),
                   "a second plugin instance sees the same library",
                   juce::String (static_cast<int> (other.getTakes().size())) + " takes");
        }

        // Left populated rather than cleared, so the snapshot below renders the
        // list with rows in it. An empty state is worth one look and this
        // screen is mostly its list; a screenshot of the placeholder text says
        // nothing about whether a row fits or how it reads.
        proc.loadPlan (juce::File (planPath));
        proc.seed.store (88345);
        proc.complexity.store (0.73);
        proc.humanize.store (0.31);
        proc.fills.store (0.90);
        proc.regenerate();
        saved ("chunky verse, take 3");

        proc.seed.store (4242);
        proc.complexity.store (0.40);
        proc.fills.store (0.20);
        proc.regenerate();
        saved ("straighter, less answering");

        proc.loadPlan (juce::File (planPath));
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
                // At the MINIMUM, because that is the hardest size the window
                // can actually be put into and it is the one nobody looks at.
                // These used to render at 600x720, which is below any floor the
                // window has ever had - so the images showed a layout that
                // cannot occur, and a real fault at the real minimum would not
                // have appeared in any of them.
                for (int s = 0; s < GhostbandEditor::numScreens; ++s)
                    shots.push_back ({ s, kMinW, kMinH, "" });
                shots.push_back ({ 0, 1600, 900, "-wide" });   // song, resized wide

                // The set that ships in docs/screenshots and is linked from the
                // README. Rendered rather than captured by hand, so they can be
                // regenerated the moment the UI changes instead of slowly going
                // stale - a screenshot of a version nobody runs any more is
                // worse than none. Sized to match the window the plugin is
                // actually used at.
                // Sized per screen rather than uniformly: a settings page with a
                // scrolling control list needs the height, and the song screen
                // at that height is mostly empty floor.
                shots.push_back ({ 0, 1180,  820, "-docs" });   // song
                shots.push_back ({ 1, 1180,  980, "-docs" });   // calibrate
                shots.push_back ({ 2, 1180,  980, "-docs" });   // edit
                shots.push_back ({ 3, 1180,  900, "-docs" });   // settings
                shots.push_back ({ 4, 1180,  820, "-docs" });   // about
                shots.push_back ({ 5, 1180,  900, "-docs" });   // takes

                // About at the size it is actually used at. It is the one
                // screen painted straight onto the canvas rather than built
                // from child components, so the overlap checker is blind to it
                // - and it has now shipped broken twice. A big window is where
                // it broke both times.
                shots.push_back ({ 4, 1020, 1400, "-wide" });

                // FRAMES OF THE FADE, so it can be looked at rather than
                // reasoned about. A 150 ms cubic ease-out is already down to
                // 12% opacity at its halfway point, which is a flicker rather
                // than a transition - and that is not visible in any still.
                shots.push_back ({ 0, 1180, 820, "-glow" });

                for (int f = 0; f < 4; ++f)
                    shots.push_back ({ 0, 1180, 820, f == 0 ? "-fade-100"
                                                   : f == 1 ? "-fade-70"
                                                   : f == 2 ? "-fade-40" : "-fade-15" });

                // The song screen with the quick edit strip OPEN. It is only
                // on screen after a click, so every other image in this set
                // shows a layout that has never been looked at with it there.
                // The click happens in the render loop below, not here: every
                // shot switches screens, and leaving one closes the strip.
                if (gbEd != nullptr && proc.getBarTicks() > 0)
                    shots.push_back ({ 0, 1180, 820, "-editing" });

                for (const Shot& shot : shots)
                {
                    if (gbEd != nullptr) gbEd->showScreenForSnapshot (shot.screen);

                    ed->setSize (shot.w, shot.h);

                    if (gbEd != nullptr)
                    {
                        const juce::String tag (shot.suffix);
                        gbEd->setFadeForTesting (tag == "-fade-100" ? 1.00f
                                               : tag == "-fade-70"  ? 0.70f
                                               : tag == "-fade-40"  ? 0.40f
                                               : tag == "-fade-15"  ? 0.15f : 0.0f);

                        // A frame of the knob trails, so they can be looked at
                        // rather than imagined.
                        gbEd->setDialGlowForTesting (tag == "-glow" ? 1.0f : 0.0f,
                                                     tag == "-glow" ? 0.22f : 0.0f);
                    }

                    // The one shot that needs a gesture first.
                    if (gbEd != nullptr && juce::String (shot.suffix) == "-editing")
                    {
                        gbEd->clickTrackerRowForTesting (2 * proc.getBarTicks());
                        ed->setSize (shot.w, shot.h);
                    }

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

                // The light theme, applied the way a person applies it: with
                // the window already open. Every colour a component was handed
                // when it was built has to be re-applied for this to be
                // readable - 62 of 66 labels used to stay on the old palette,
                // which on Paper means text the exact colour of the page.
                if (gbEd != nullptr)
                {
                    // BY NAME. This said numThemes - 1 and meant Paper, until a
                    // theme was added after Paper - and then the light-theme
                    // screenshot quietly began rendering a dark one. Second time
                    // this exact assumption has broken in this file.
                    int paperShot = 0;
                    for (int i = 0; i < ghost::numThemes; ++i)
                        if (juce::String (ghost::themeName (i)) == "Paper") paperShot = i;

                    gbEd->setThemeForTesting (paperShot);

                    // Every screen, not two. "The theme list went unreadable"
                    // came with "there are probably other parts you cannot read
                    // either", which was the right instinct: a palette that
                    // half-applies breaks wherever it was not looked at.
                    for (int screen = 0; screen < GhostbandEditor::numScreens; ++screen)
                    {
                        gbEd->showScreenForSnapshot (screen);
                        ed->setSize (1000, screen == 3 ? 1320 : (screen == 1 ? 1280 : 900));

                        const juce::Image img =
                            ed->createComponentSnapshot (ed->getLocalBounds(), true);

                        const juce::File out = dir.getChildFile (
                            "editor-" + juce::String (GhostbandEditor::screenName (screen))
                                + "-paper.png");
                        out.deleteFile();
                        juce::FileOutputStream stream (out);
                        if (stream.openedOk())
                        {
                            juce::PNGImageFormat png;
                            png.writeImageToStream (img, stream);
                            std::cout << "  snapshot: " << out.getFullPathName() << std::endl;
                        }
                    }

                    gbEd->setThemeForTesting (0);
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
