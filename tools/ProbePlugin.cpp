// Loads a VST3 headlessly, plays one note at a time, and measures what came out.
//
// The point is to work out a plugin's MIDI map without a human in the loop. We
// cannot listen, but we can measure, and measurement answers the questions that
// actually matter:
//
//   * Is this note mapped at all, or is it silent?
//   * Is it bright or dark, short or long?  (hi-hat vs kick, keyswitch vs note)
//   * Where does a phrase-driven instrument's chord zone end and its phrase
//     zone begin?  A chord key alone behaves very differently from a phrase key.
//
// That is enough to verify a drum map, find a bass instrument's playable range
// and its silent keyswitches, and locate a UJAM instrument's key zones.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {

struct NoteMeasurement
{
    int    note      = 0;
    float  peak      = 0.0f;
    float  rms       = 0.0f;
    float  brightness = 0.0f;   // 0 = dark, 1 = very bright
    double decaySecs = 0.0;
    bool   sounded   = false;
};

// Crude but robust brightness: mean absolute first difference over mean absolute
// level. No FFT needed, and it separates a kick from a hi-hat decisively.
float brightnessOf (const juce::AudioBuffer<float>& buf, int numSamples)
{
    double diff = 0.0, level = 0.0;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* d = buf.getReadPointer (ch);
        for (int i = 1; i < numSamples; ++i)
        {
            diff  += std::abs (d[i] - d[i - 1]);
            level += std::abs (d[i]);
        }
    }
    if (level < 1.0e-9) return 0.0f;
    return static_cast<float> (juce::jlimit (0.0, 1.0, diff / level / 2.0));
}

// Fundamental frequency by normalised autocorrelation.
//
// A distorted guitar is full of harmonics and a spectrum peak can easily land
// on the wrong one; autocorrelation locks to the period instead, which is the
// fundamental whatever is stacked on top of it. This only has to answer one
// question - did the pitch move when a bend was sent - so absolute accuracy
// matters far less than not being fooled by an octave.
// `expectedHz` bounds the search to within an octave either side of the note
// that was played. Without it the search ran from 60 Hz upward and locked onto a
// lag eight periods long - it reported C5 as 65 Hz, three octaves down, because
// a periodic signal correlates with itself just as well at any multiple of its
// period. Bounding it makes an octave error arithmetically impossible, and an
// octave error is the one failure that would have made this test lie.
double estimatePitch (const std::vector<float>& x, size_t from, size_t len, double sr,
                      double expectedHz = 0.0)
{
    const double lowHz  = expectedHz > 20.0 ? expectedHz / 2.0 : 60.0;
    const double highHz = expectedHz > 20.0 ? expectedHz * 2.0 : 1600.0;

    const int minLag = std::max (2, static_cast<int> (sr / highHz));
    const int maxLag = static_cast<int> (sr / lowHz);

    if (len < 64 || from + len + maxLag > x.size()) return 0.0;

    double best = 0.0;
    int    bestLag = 0;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double num = 0.0, e1 = 0.0, e2 = 0.0;
        for (size_t i = 0; i < len; ++i)
        {
            const double a = x[from + i], b = x[from + i + lag];
            num += a * b;  e1 += a * a;  e2 += b * b;
        }

        const double denom = std::sqrt (e1 * e2);
        if (denom < 1.0e-12) continue;

        const double r = num / denom;
        if (r > best) { best = r; bestLag = lag; }
    }

    return (bestLag > 0 && best > 0.3) ? sr / bestLag : 0.0;
}

class Probe
{
public:
    bool load (const juce::String& path, juce::String& error)
    {
        // JUCE 8 split the processors module and removed addDefaultFormats().
        // The UI-capable variant is used rather than the headless one because
        // several sampled instruments only initialise their engine when an
        // editor is created - the headless format refuses to make one, and the
        // plugin then measures as silent on every note.
        juce::addDefaultFormatsToManager (formatManager);

        juce::OwnedArray<juce::PluginDescription> found;
        for (juce::AudioPluginFormat* format : formatManager.getFormats())
            if (format->fileMightContainThisPluginType (path))
                format->findAllTypesForFile (found, path);

        if (found.isEmpty())
        {
            error = "no plugin types found in " + path;
            return false;
        }

        instance = formatManager.createPluginInstance (*found[0], sampleRate, blockSize, error);
        if (instance == nullptr)
            return false;

        instance->enableAllBuses();
        instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
        instance->prepareToPlay (sampleRate, blockSize);

        name = instance->getName();
        return true;
    }

    // Creates the plugin's editor without showing it. Nothing is drawn, but the
    // plugin gets the initialisation it may be relying on.
    bool openEditor()
    {
        if (instance == nullptr || ! instance->hasEditor())
            return false;

        editor.reset (instance->createEditorIfNeeded());
        if (editor == nullptr)
            return false;

        editor->setOpaque (true);
        editor->setSize (juce::jmax (100, editor->getWidth()),
                         juce::jmax (100, editor->getHeight()));
        editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
        editor->setVisible (false);
        return true;
    }

    void closeEditor() { editor.reset(); }

    juce::String getName() const { return name; }

    // A silent probe is ambiguous: the plugin might have no content loaded, be
    // waiting on authorisation, or be listening on another channel. Printing
    // what it says about itself distinguishes those.
    void describe() const
    {
        if (instance == nullptr) return;

        std::cout << "latency : " << instance->getLatencySamples() << " samples\n";
        std::cout << "programs: " << instance->getNumPrograms();
        if (instance->getNumPrograms() > 0)
            std::cout << "  (current: \"" << instance->getProgramName (instance->getCurrentProgram()) << "\")";
        std::cout << "\n";

        const auto& params = instance->getParameters();
        std::cout << "params  : " << params.size() << "\n";

        for (int i = 0; i < juce::jmin (24, params.size()); ++i)
            std::cout << "    [" << i << "] " << params[i]->getName (40)
                      << " = " << params[i]->getCurrentValueAsText() << "\n";

        std::cout << "buses   : in " << instance->getTotalNumInputChannels()
                  << ", out " << instance->getTotalNumOutputChannels() << "\n";
    }

    // Dumps every parameter whose name is not one of the generic MIDI CC
    // passthroughs. A plugin's style or preset selector shows up here, which
    // saves asking a human to read it off a UI.
    void dumpInterestingParameters() const
    {
        if (instance == nullptr) return;

        const auto& params = instance->getParameters();
        std::cout << "\ninteresting parameters (excluding generic MIDI CC slots)\n";
        std::cout << "-------------------------------------------------------\n";

        int shown = 0;
        for (int i = 0; i < params.size(); ++i)
        {
            const juce::String n = params[i]->getName (60);
            if (n.startsWithIgnoreCase ("MIDI CC")) continue;

            std::cout << "  [" << i << "] " << n
                      << "  =  " << params[i]->getCurrentValueAsText();

            const auto choices = params[i]->getAllValueStrings();
            if (choices.size() > 1 && choices.size() <= 40)
                std::cout << "   {" << choices.joinIntoString (" | ") << "}";

            std::cout << "\n";
            if (++shown > 200) { std::cout << "  ... truncated\n"; break; }
        }
        std::cout << shown << " non-CC parameters\n";

        std::cout << "buses   : in " << instance->getTotalNumInputChannels()
                  << ", out " << instance->getTotalNumOutputChannels() << "\n";
    }

    // Some instruments only stream their content once transport is rolling.
    void setPlayingTransport (bool shouldPlay)
    {
        playHead.playing = shouldPlay;
        if (instance != nullptr)
            instance->setPlayHead (&playHead);
    }

    struct SimplePlayHead : juce::AudioPlayHead
    {
        bool playing = false;
        double ppq = 0.0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            info.setBpm (120.0);
            info.setPpqPosition (ppq);
            info.setTimeInSeconds (ppq * 0.5);
            return info;
        }
    };

    SimplePlayHead playHead;

    int outputChannels() const
    {
        return instance != nullptr ? instance->getTotalNumOutputChannels() : 0;
    }

    // Sampled instruments load their content asynchronously on the message
    // thread. A console app has a MessageManager but never runs its loop, so
    // without pumping it here the plugin never finishes loading and every note
    // measures as silence - which looks exactly like an unmapped note.
    static void pump (int milliseconds)
    {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil (milliseconds);
    }

    void warmUp (double seconds)
    {
        juce::AudioBuffer<float> buf (juce::jmax (2, outputChannels()), blockSize);
        juce::MidiBuffer midi;

        const int steps = juce::jmax (1, static_cast<int> (seconds * 10));
        for (int s = 0; s < steps; ++s)
        {
            pump (100);
            for (int i = 0; i < 10; ++i)
            {
                buf.clear();
                midi.clear();
                instance->processBlock (buf, midi);
            }
        }
    }

    NoteMeasurement measureNote (int note, int channel, double holdSecs, double tailSecs,
                                 int velocity = 100)
    {
        NoteMeasurement m;
        m.note = note;

        const int channels = juce::jmax (2, outputChannels());
        juce::AudioBuffer<float> buf (channels, blockSize);
        juce::MidiBuffer midi;

        // Flush whatever the previous note left ringing, and give the plugin a
        // moment of message-thread time in case it streams on demand.
        pump (5);
        for (int i = 0; i < 8; ++i)
        {
            buf.clear(); midi.clear();
            instance->processBlock (buf, midi);
        }

        const int holdBlocks = juce::jmax (1, static_cast<int> (holdSecs * sampleRate / blockSize));
        const int tailBlocks = juce::jmax (1, static_cast<int> (tailSecs * sampleRate / blockSize));

        double sumSquares = 0.0;
        int    totalSamples = 0;
        double brightAccum = 0.0;
        int    brightBlocks = 0;
        int    lastLoudBlock = -1;

        for (int b = 0; b < holdBlocks + tailBlocks; ++b)
        {
            buf.clear();
            midi.clear();

            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (channel, note,
                                                          static_cast<juce::uint8> (velocity)), 0);
            else if (b == holdBlocks)
                midi.addEvent (juce::MidiMessage::noteOff (channel, note), 0);

            instance->processBlock (buf, midi);

            float blockPeak = 0.0f;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                blockPeak = juce::jmax (blockPeak, buf.getMagnitude (ch, 0, blockSize));

            m.peak = juce::jmax (m.peak, blockPeak);

            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            {
                const float* d = buf.getReadPointer (ch);
                for (int i = 0; i < blockSize; ++i)
                    sumSquares += static_cast<double> (d[i]) * d[i];
            }
            totalSamples += blockSize * buf.getNumChannels();

            if (blockPeak > 0.0005f)
            {
                lastLoudBlock = b;
                brightAccum += brightnessOf (buf, blockSize);
                ++brightBlocks;
            }
        }

        m.rms       = totalSamples > 0 ? static_cast<float> (std::sqrt (sumSquares / totalSamples)) : 0.0f;
        m.brightness = brightBlocks > 0 ? static_cast<float> (brightAccum / brightBlocks) : 0.0f;
        m.decaySecs = lastLoudBlock >= 0 ? (lastLoudBlock + 1) * blockSize / sampleRate : 0.0;
        m.sounded   = m.peak > 0.0015f;

        return m;
    }

    // Holds a chord and measures it. A phrase-driven instrument answers very
    // differently to a chord than to a phrase key, which is how the zones are
    // found.
    NoteMeasurement measureChord (const std::vector<int>& notes, int channel,
                                  double holdSecs, int velocity = 100)
    {
        NoteMeasurement m;
        m.note = notes.empty() ? 0 : notes.front();

        const int channels = juce::jmax (2, outputChannels());
        juce::AudioBuffer<float> buf (channels, blockSize);
        juce::MidiBuffer midi;

        for (int i = 0; i < 8; ++i) { buf.clear(); midi.clear(); instance->processBlock (buf, midi); }

        const int blocks = juce::jmax (1, static_cast<int> (holdSecs * sampleRate / blockSize));
        double sumSquares = 0.0; int totalSamples = 0;
        double brightAccum = 0.0; int brightBlocks = 0;

        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            midi.clear();
            if (b == 0)
                for (int n : notes)
                    midi.addEvent (juce::MidiMessage::noteOn (channel, n,
                                                              static_cast<juce::uint8> (velocity)), 0);

            instance->processBlock (buf, midi);

            float blockPeak = 0.0f;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                blockPeak = juce::jmax (blockPeak, buf.getMagnitude (ch, 0, blockSize));
            m.peak = juce::jmax (m.peak, blockPeak);

            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            {
                const float* d = buf.getReadPointer (ch);
                for (int i = 0; i < blockSize; ++i) sumSquares += static_cast<double> (d[i]) * d[i];
            }
            totalSamples += blockSize * buf.getNumChannels();

            if (blockPeak > 0.0005f) { brightAccum += brightnessOf (buf, blockSize); ++brightBlocks; }
        }

        midi.clear();
        for (int n : notes) midi.addEvent (juce::MidiMessage::noteOff (channel, n), 0);
        buf.clear();
        instance->processBlock (buf, midi);

        m.rms        = totalSamples > 0 ? static_cast<float> (std::sqrt (sumSquares / totalSamples)) : 0.0f;
        m.brightness = brightBlocks > 0 ? static_cast<float> (brightAccum / brightBlocks) : 0.0f;
        m.sounded    = m.peak > 0.0015f;
        return m;
    }

    // Holds a key for a while and captures the output, counting attacks along
    // the way. Transient density is the measurement that maps onto how a phrase
    // actually feels: a sustained chord has one attack, a chugging riff has
    // dozens. That is exactly the sparse-to-busy axis the generator asks for.
    struct PhraseMeasurement
    {
        int    note = 0;
        int    attacks = 0;
        double attacksPerSecond = 0.0;
        float  peak = 0.0f;
        float  brightness = 0.0f;
        // Fraction of the hold during which anything was sounding. This is what
        // separates a latching style phrase, which plays for as long as the
        // chord is held, from a one-shot "common phrase" that fires once and
        // stops - pressing one of those mid-song is what makes the band lurch.
        double sustain = 0.0;
        double firstGapAt = -1.0;   // seconds until the first silence
        std::vector<float> mono;   // captured for comparison against a baseline
    };

    PhraseMeasurement capture (const std::vector<int>& held, int channel, double seconds,
                               bool keepAudio, int velocity = 100)
    {
        PhraseMeasurement m;
        m.note = held.empty() ? 0 : held.back();

        const int channels = juce::jmax (2, outputChannels());
        juce::AudioBuffer<float> buf (channels, blockSize);
        juce::MidiBuffer midi;

        pump (5);
        for (int i = 0; i < 16; ++i) { buf.clear(); midi.clear(); instance->processBlock (buf, midi); }

        const int blocks = juce::jmax (1, static_cast<int> (seconds * sampleRate / blockSize));
        if (keepAudio) m.mono.reserve (static_cast<size_t> (blocks));

        double brightAccum = 0.0; int brightBlocks = 0;
        float prevPeak = 0.0f;
        bool  aboveGate = false;

        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            midi.clear();
            if (b == 0)
                for (int n : held)
                    midi.addEvent (juce::MidiMessage::noteOn (channel, n,
                                                              static_cast<juce::uint8> (velocity)), 0);

            instance->processBlock (buf, midi);

            float blockPeak = 0.0f;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                blockPeak = juce::jmax (blockPeak, buf.getMagnitude (ch, 0, blockSize));

            m.peak = juce::jmax (m.peak, blockPeak);
            if (keepAudio) m.mono.push_back (blockPeak);

            if (blockPeak > 0.0005f) { brightAccum += brightnessOf (buf, blockSize); ++brightBlocks; }

            // An attack is a clear rise after the level has dipped. Gating on
            // the dip stops one long swell being counted as many attacks.
            if (! aboveGate && blockPeak > 0.04f && blockPeak > prevPeak * 1.5f)
            {
                ++m.attacks;
                aboveGate = true;
            }
            else if (aboveGate && blockPeak < prevPeak * 0.7f)
            {
                aboveGate = false;
            }
            prevPeak = blockPeak;
        }

        midi.clear();
        for (int n : held) midi.addEvent (juce::MidiMessage::noteOff (channel, n), 0);
        buf.clear();
        instance->processBlock (buf, midi);

        m.brightness       = brightBlocks > 0 ? static_cast<float> (brightAccum / brightBlocks) : 0.0f;
        m.attacksPerSecond = seconds > 0.0 ? m.attacks / seconds : 0.0;

        // Sustain and the first gap, computed from the captured envelope.
        if (! m.mono.empty())
        {
            const float gate = juce::jmax (0.004f, m.peak * 0.06f);
            int sounding = 0;
            int quietRun = 0;
            const double perBlock = blockSize / sampleRate;

            for (size_t i = 0; i < m.mono.size(); ++i)
            {
                if (m.mono[i] > gate) { ++sounding; quietRun = 0; }
                else
                {
                    ++quietRun;
                    // A tenth of a second of silence is a real gap, not the dip
                    // between two notes of a riff.
                    if (m.firstGapAt < 0.0 && quietRun * perBlock > 0.10 && sounding > 0)
                        m.firstGapAt = (i - quietRun) * perBlock;
                }
            }
            m.sustain = static_cast<double> (sounding) / m.mono.size();
        }

        return m;
    }

    // Holds a chord for the whole span, but presses and releases the phrase key
    // at the start - reproducing exactly what Ghostband emits.
    PhraseMeasurement captureWithBlip (const std::vector<int>& chord, int phraseKey,
                                       int channel, double seconds, double blipSeconds)
    {
        PhraseMeasurement m;
        m.note = phraseKey;

        const int channels = juce::jmax (2, outputChannels());
        juce::AudioBuffer<float> buf (channels, blockSize);
        juce::MidiBuffer midi;

        pump (5);
        for (int i = 0; i < 16; ++i) { buf.clear(); midi.clear(); instance->processBlock (buf, midi); }

        const int blocks = juce::jmax (1, static_cast<int> (seconds * sampleRate / blockSize));
        const int blipBlocks = juce::jmax (1, static_cast<int> (blipSeconds * sampleRate / blockSize));
        m.mono.reserve (static_cast<size_t> (blocks));

        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            midi.clear();

            if (b == 0)
            {
                for (int n : chord)
                    midi.addEvent (juce::MidiMessage::noteOn (channel, n, (juce::uint8) 100), 0);
                midi.addEvent (juce::MidiMessage::noteOn (channel, phraseKey, (juce::uint8) 100), 1);
            }
            else if (b == blipBlocks)
            {
                midi.addEvent (juce::MidiMessage::noteOff (channel, phraseKey), 0);
            }

            instance->processBlock (buf, midi);

            float blockPeak = 0.0f;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                blockPeak = juce::jmax (blockPeak, buf.getMagnitude (ch, 0, blockSize));
            m.peak = juce::jmax (m.peak, blockPeak);
            m.mono.push_back (blockPeak);
        }

        midi.clear();
        for (int n : chord) midi.addEvent (juce::MidiMessage::noteOff (channel, n), 0);
        buf.clear();
        instance->processBlock (buf, midi);

        const float gate = juce::jmax (0.004f, m.peak * 0.06f);
        int sounding = 0, quietRun = 0;
        const double perBlock = blockSize / sampleRate;
        for (size_t i = 0; i < m.mono.size(); ++i)
        {
            if (m.mono[i] > gate) { ++sounding; quietRun = 0; }
            else if (++quietRun * perBlock > 0.10 && m.firstGapAt < 0.0 && sounding > 0)
                m.firstGapAt = (i - quietRun) * perBlock;
        }
        m.sustain = m.mono.empty() ? 0.0 : static_cast<double> (sounding) / m.mono.size();
        return m;
    }

    // Renders a scripted sequence of MIDI events and keeps every sample.
    //
    // The other measurements only need an envelope, so they keep one number per
    // block. Pitch cannot be recovered from an envelope, and the question here -
    // does this instrument bend, and how far - is entirely a question about
    // pitch, so this one keeps the audio.
    std::vector<float> captureSamples (const std::vector<std::pair<double, juce::MidiMessage>>& events,
                                       double seconds)
    {
        std::vector<float> mono;
        if (instance == nullptr) return mono;

        const int channels = juce::jmax (2, outputChannels());
        juce::AudioBuffer<float> buf (channels, blockSize);
        juce::MidiBuffer midi;

        const int blocks = juce::jmax (1, static_cast<int> (seconds * sampleRate / blockSize));
        mono.reserve (static_cast<size_t> (blocks) * blockSize);

        size_t next = 0;

        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            midi.clear();

            const double blockStart = b * blockSize / sampleRate;
            const double blockEnd   = (b + 1) * blockSize / sampleRate;

            while (next < events.size() && events[next].first < blockEnd)
            {
                const int offset = juce::jlimit (0, blockSize - 1,
                                        static_cast<int> ((events[next].first - blockStart) * sampleRate));
                midi.addEvent (events[next].second, offset);
                ++next;
            }

            instance->processBlock (buf, midi);

            for (int i = 0; i < blockSize; ++i)
            {
                double sum = 0.0;
                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                    sum += buf.getReadPointer (ch)[i];
                mono.push_back (static_cast<float> (sum / buf.getNumChannels()));
            }
        }

        return mono;
    }

    double sampleRate = 48000.0;
    int    blockSize  = 512;

private:
    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::String name;
};

juce::String noteName (int n)
{
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
}

} // namespace

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cout <<
            "Probe a VST3's MIDI map by measuring what each note actually produces.\n\n"
            "  ghostband_probe <plugin.vst3> [options]\n\n"
            "  --low <n>       first MIDI note to test (default 24)\n"
            "  --high <n>      last MIDI note to test (default 96)\n"
            "  --channel <n>   MIDI channel (default 1; use 10 for drums)\n"
            "  --hold <secs>   how long to hold each note (default 0.35)\n"
            "  --tail <secs>   how long to listen after release (default 1.2)\n"
            "  --warmup <secs> settle time before probing (default 3)\n"
            "  --chords        also test three-note chords, to find a chord zone\n"
            "  --mode lead     can it solo? pitch bend, sustain and legato\n";
        return 1;
    }

    const juce::String path = argv[1];
    int    low = 24, high = 96, channel = 1;
    double hold = 0.35, tail = 1.2, warmup = 3.0;
    bool   testChords = false;
    juce::String mode = "sweep";
    double phraseSecs = 4.0;
    int    phraseKey  = -1;

    for (int i = 2; i < argc; ++i)
    {
        const juce::String a = argv[i];
        const bool hasNext = (i + 1 < argc);
        if      (a == "--mode"       && hasNext) mode       = argv[++i];
        else if (a == "--phrase-secs"&& hasNext) phraseSecs = juce::String (argv[++i]).getDoubleValue();
        else if (a == "--phrase-key" && hasNext) phraseKey  = juce::String (argv[++i]).getIntValue();
        else if (a == "--low"     && hasNext) low     = juce::String (argv[++i]).getIntValue();
        else if (a == "--high"    && hasNext) high    = juce::String (argv[++i]).getIntValue();
        else if (a == "--channel" && hasNext) channel = juce::String (argv[++i]).getIntValue();
        else if (a == "--hold"    && hasNext) hold    = juce::String (argv[++i]).getDoubleValue();
        else if (a == "--tail"    && hasNext) tail    = juce::String (argv[++i]).getDoubleValue();
        else if (a == "--warmup"  && hasNext) warmup  = juce::String (argv[++i]).getDoubleValue();
        else if (a == "--chords")             testChords = true;
    }

    Probe probe;
    juce::String error;

    std::cout << "loading " << path << " ...\n";
    if (! probe.load (path, error))
    {
        std::cerr << "failed: " << error << "\n";
        return 1;
    }

    std::cout << "loaded  : " << probe.getName() << "\n";
    probe.describe();
    std::cout << "editor  : " << (probe.openEditor() ? "created (hidden)" : "none") << "\n";
    probe.setPlayingTransport (true);
    std::cout << "warming up " << warmup << "s (sample streaming, presets)...\n";
    probe.warmUp (warmup);

    std::cout << "\nprobing notes " << low << "-" << high << " on channel " << channel << "\n";
    std::cout << "note  name    peak      rms    bright   decay   verdict\n";
    std::cout << "-------------------------------------------------------\n";

    int sounded = 0;
    for (int n = low; n <= high; ++n)
    {
        const NoteMeasurement m = probe.measureNote (n, channel, hold, tail);
        if (m.sounded) ++sounded;

        juce::String verdict;
        if (! m.sounded)                       verdict = "silent";
        else if (m.decaySecs < 0.35)           verdict = m.brightness > 0.25 ? "short/bright" : "short/dark";
        else if (m.decaySecs < 1.0)            verdict = m.brightness > 0.25 ? "med/bright"   : "med/dark";
        else                                   verdict = m.brightness > 0.25 ? "long/bright"  : "long/dark";

        std::printf ("%4d  %-5s  %7.4f  %7.4f  %6.3f  %6.2f   %s\n",
                     n, noteName (n).toRawUTF8(), m.peak, m.rms, m.brightness,
                     m.decaySecs, verdict.toRawUTF8());
    }

    std::cout << "\n" << sounded << " of " << (high - low + 1) << " notes produced sound\n";

    if (mode == "lead")
    {
        // Can this instrument play a lead line, as opposed to a riff?
        //
        // Three questions, none of which the note sweep answers, and all three
        // decide whether a rhythm library can be made to solo or whether the
        // job needs a different instrument:
        //
        //   * does it respond to pitch bend, and over what range - bends and
        //     whammy dives are pitch bend, not a keyswitch;
        //   * how long does one note actually sustain before it dies, because
        //     a held note that lasts a beat cannot end a phrase;
        //   * does a second note taken while the first is held glide, or does
        //     it re-attack - legato against picked.
        const int testNote = (low + high) / 2;

        std::cout << "\nlead articulation test on note " << testNote
                  << " (" << noteName (testNote) << "), channel " << channel << "\n";
        std::cout << "=======================================================\n";

        // ---- 1. pitch bend -------------------------------------------------
        {
            const double windowSecs = 0.25;
            std::vector<std::pair<double, juce::MidiMessage>> events;
            events.emplace_back (0.0, juce::MidiMessage::pitchWheel (channel, 8192));
            events.emplace_back (0.0, juce::MidiMessage::noteOn (channel, testNote, (juce::uint8) 100));
            events.emplace_back (0.9, juce::MidiMessage::pitchWheel (channel, 16383));
            events.emplace_back (1.9, juce::MidiMessage::pitchWheel (channel, 0));
            events.emplace_back (2.9, juce::MidiMessage::pitchWheel (channel, 8192));
            events.emplace_back (3.6, juce::MidiMessage::noteOff (channel, testNote));

            const std::vector<float> audio = probe.captureSamples (events, 4.0);
            const double sr = probe.sampleRate;

            const double nominal = 440.0 * std::pow (2.0, (testNote - 69) / 12.0);

            const auto at = [&] (double t)
            {
                return estimatePitch (audio, static_cast<size_t> (t * sr),
                                      static_cast<size_t> (windowSecs * sr), sr, nominal);
            };

            const double centre = at (0.55);
            const double up     = at (1.45);
            const double down   = at (2.45);
            const double back   = at (3.25);

            const auto semis = [] (double f, double ref)
            {
                return (f > 20.0 && ref > 20.0) ? 12.0 * std::log2 (f / ref) : 0.0;
            };

            std::printf ("  bend centre : %8.2f Hz   (expected ~%.2f)\n",
                         centre, nominal);
            std::printf ("  bend up     : %8.2f Hz   %+6.2f semitones\n", up,   semis (up,   centre));
            std::printf ("  bend down   : %8.2f Hz   %+6.2f semitones\n", down, semis (down, centre));
            std::printf ("  released    : %8.2f Hz   %+6.2f semitones\n", back, semis (back, centre));

            const double range = std::max (std::abs (semis (up, centre)), std::abs (semis (down, centre)));
            if (centre < 20.0)
                std::cout << "  VERDICT: could not find a pitch - is the instrument sounding?\n";
            else if (range < 0.4)
                std::cout << "  VERDICT: IGNORES pitch bend. No bends, no dive bombs, no vibrato.\n";
            else
                std::printf ("  VERDICT: BENDS, range about +/-%.1f semitones.%s\n", range,
                             range > 6.0 ? "  Wide enough for a dive bomb." : "");
        }

        // ---- 2. sustain ----------------------------------------------------
        {
            std::vector<std::pair<double, juce::MidiMessage>> events;
            events.emplace_back (0.0, juce::MidiMessage::noteOn (channel, testNote, (juce::uint8) 110));

            const double window = 12.0;
            const std::vector<float> audio = probe.captureSamples (events, window);
            const double sr = probe.sampleRate;

            // Envelope in 50 ms steps, so a tremolo or a looped sample does not
            // read as a decay.
            const size_t step = static_cast<size_t> (0.05 * sr);
            std::vector<float> env;
            for (size_t i = 0; i + step <= audio.size(); i += step)
            {
                float peak = 0.0f;
                for (size_t j = i; j < i + step; ++j) peak = std::max (peak, std::abs (audio[j]));
                env.push_back (peak);
            }

            float top = 0.0f;
            for (float e : env) top = std::max (top, e);

            const auto fell = [&] (double frac)
            {
                for (size_t i = 0; i < env.size(); ++i)
                    if (env[i] < top * frac)
                    {
                        // Has to stay down, or one dip between picks counts.
                        bool stays = true;
                        for (size_t j = i; j < std::min (env.size(), i + 10); ++j)
                            if (env[j] >= top * frac) stays = false;
                        if (stays) return i * 0.05;
                    }
                return window;
            };

            std::cout << "\n  note held for " << window << "s with no note-off\n";
            std::printf ("  peak level  : %.4f\n", top);
            std::printf ("  -12 dB after: %5.2f s\n", fell (0.25));
            std::printf ("  -20 dB after: %5.2f s\n", fell (0.10));
            std::printf ("  -40 dB after: %5.2f s\n", fell (0.01));

            const double usable = fell (0.10);
            if (usable >= 6.0)
                std::cout << "  VERDICT: sustains. A held note can end a phrase.\n";
            else if (usable >= 2.5)
                std::printf ("  VERDICT: sustains about %.1fs - enough for a bar, not for a long hold.\n", usable);
            else
                std::printf ("  VERDICT: DIES after %.1fs. Held notes will not work.\n", usable);
        }

        // ---- 3. legato -----------------------------------------------------
        {
            std::vector<std::pair<double, juce::MidiMessage>> events;
            events.emplace_back (0.0, juce::MidiMessage::noteOn  (channel, testNote,     (juce::uint8) 100));
            events.emplace_back (1.0, juce::MidiMessage::noteOn  (channel, testNote + 2, (juce::uint8) 100));
            events.emplace_back (2.0, juce::MidiMessage::noteOff (channel, testNote + 2));
            events.emplace_back (2.1, juce::MidiMessage::noteOff (channel, testNote));

            const std::vector<float> audio = probe.captureSamples (events, 2.6);
            const double sr = probe.sampleRate;

            const auto peakBetween = [&] (double a, double b)
            {
                float p = 0.0f;
                const size_t from = static_cast<size_t> (a * sr), to = static_cast<size_t> (b * sr);
                for (size_t i = from; i < std::min (to, audio.size()); ++i)
                    p = std::max (p, std::abs (audio[i]));
                return p;
            };

            const float before = peakBetween (0.80, 0.98);   // first note, settled
            const float onset  = peakBetween (1.00, 1.10);   // the moment the second arrives

            // Level alone cannot tell "it glided" from "it ignored the second
            // note" - both leave the level exactly where it was. Pitch can.
            const double nominal1 = 440.0 * std::pow (2.0, (testNote - 69) / 12.0);
            const double heard1 = estimatePitch (audio, (size_t) (0.75 * sr), (size_t) (0.20 * sr), sr, nominal1);
            const double heard2 = estimatePitch (audio, (size_t) (1.55 * sr), (size_t) (0.20 * sr), sr, nominal1);
            const double moved  = (heard1 > 20.0 && heard2 > 20.0) ? 12.0 * std::log2 (heard2 / heard1) : 0.0;

            std::cout << "\n  second note (two semitones up) taken while the first is still held\n";
            std::printf ("  level before: %.4f\n", before);
            std::printf ("  level at 2nd: %.4f   (%.2fx)\n", onset,
                         before > 1.0e-6f ? onset / before : 0.0f);
            std::printf ("  pitch before: %8.2f Hz\n", heard1);
            std::printf ("  pitch after : %8.2f Hz   %+6.2f semitones\n", heard2, moved);

            if (before <= 1.0e-6f)
                std::cout << "  VERDICT: inconclusive, the first note was silent.\n";
            else if (std::abs (moved) < 0.5)
                std::cout << "  VERDICT: the second note did NOTHING - the pitch never moved.\n";
            else if (onset > before * 1.6f)
                std::cout << "  VERDICT: RE-ATTACKS. Every note is picked; no legato.\n";
            else
                std::cout << "  VERDICT: pitch moved with no new attack - that is legato.\n";
        }

        std::cout << "\n";
    }

    if (mode == "blip")
    {
        // The question the sustain test did not ask: does a phrase key have to
        // stay held? Ghostband presses it for about fifty milliseconds and lets
        // go, assuming it latches. If these instruments actually play only while
        // the key is down, that blip yields a fraction of a second of sound and
        // then silence - which is exactly what a user reported.
        std::cout << "\nchord held " << phraseSecs << "s; phrase key pressed briefly then released\n";
        std::cout << "note  name   sustain  first gap   verdict\n";
        std::cout << "-----------------------------------------------\n";

        for (int n = low; n <= high; ++n)
        {
            const auto m = probe.captureWithBlip ({ 36, 40, 43 }, n, channel, phraseSecs, 0.05);

            juce::String verdict;
            if (m.peak < 0.01f)        verdict = "silent";
            else if (m.sustain > 0.80) verdict = "LATCHES - a blip is enough";
            else                       verdict = "NEEDS HOLDING - blip is not enough";

            std::printf ("%4d  %-5s  %6.2f  %9.2f   %s\n",
                         n, noteName (n).toRawUTF8(), m.sustain,
                         m.firstGapAt < 0.0 ? phraseSecs : m.firstGapAt,
                         verdict.toRawUTF8());
        }
        return 0;
    }

    if (mode == "sustain")
    {
        // Hold a chord in the chord zone plus each candidate phrase key, and see
        // which ones keep playing. A latching style phrase sustains for as long
        // as the chord is held; a one-shot common phrase fires and stops, and
        // triggering one of those mid-song is what makes an arrangement lurch.
        std::cout << "\nholding a chord plus each key for " << phraseSecs << "s\n";
        std::cout << "note  name   sustain  first gap   attacks   verdict\n";
        std::cout << "--------------------------------------------------------\n";

        for (int n = low; n <= high; ++n)
        {
            const auto m = probe.capture ({ 36, 40, 43, n }, channel, phraseSecs, true);

            juce::String verdict;
            if (m.peak < 0.01f)          verdict = "silent";
            else if (m.sustain > 0.80)   verdict = "LATCHING - safe to select";
            else if (m.sustain > 0.45)   verdict = "intermittent";
            else                         verdict = "ONE-SHOT - do not use";

            std::printf ("%4d  %-5s  %6.2f  %9.2f  %7d   %s\n",
                         n, noteName (n).toRawUTF8(), m.sustain,
                         m.firstGapAt < 0.0 ? phraseSecs : m.firstGapAt,
                         m.attacks, verdict.toRawUTF8());
        }
        return 0;
    }

    if (mode == "params")
    {
        probe.dumpInterestingParameters();
        return 0;
    }

    if (mode == "phrases")
    {
        // Rank every key in the range by how busy it is, so the generator's
        // abstract "sparse" and "busy" can be mapped onto real phrase keys.
        std::cout << "\nphrase character, holding each key for " << phraseSecs << "s\n";
        std::cout << "note  name   attacks  per-sec    peak   bright\n";
        std::cout << "------------------------------------------------\n";
        for (int n = low; n <= high; ++n)
        {
            const auto m = probe.capture ({ n }, channel, phraseSecs, false);
            std::printf ("%4d  %-5s  %7d  %7.2f  %6.3f  %6.3f\n",
                         n, noteName (n).toRawUTF8(), m.attacks, m.attacksPerSecond,
                         m.peak, m.brightness);
        }
        return 0;
    }

    if (mode == "zone")
    {
        // Find the chord zone by difference. Hold a phrase key alone, then hold
        // it again with a triad added, and see whether the output changed. A
        // key that changes the phrase is a chord key; one that changes nothing
        // is outside the zone.
        if (phraseKey < 0)
        {
            std::cerr << "--zone needs --phrase-key <n> (a key that makes sound on its own)\n";
            return 1;
        }

        const auto baseline = probe.capture ({ phraseKey }, channel, 2.5, true);
        std::cout << "\nbaseline: phrase key " << phraseKey << " alone, peak "
                  << juce::String (baseline.peak, 4) << "\n\n";
        std::cout << "root  name   difference   verdict\n";
        std::cout << "------------------------------------\n";

        for (int n = low; n <= high; ++n)
        {
            const auto withChord = probe.capture ({ n, n + 3, n + 7, phraseKey }, channel, 2.5, true);

            double diff = 0.0, level = 0.0;
            const size_t count = juce::jmin (baseline.mono.size(), withChord.mono.size());
            for (size_t i = 0; i < count; ++i)
            {
                diff  += std::abs (withChord.mono[i] - baseline.mono[i]);
                level += std::abs (baseline.mono[i]);
            }
            const double rel = level > 1.0e-9 ? diff / level : 0.0;

            std::printf ("%4d  %-5s  %10.4f   %s\n", n, noteName (n).toRawUTF8(), rel,
                         rel > 0.08 ? "CHORD KEY" : "no effect");
        }
        return 0;
    }

    if (testChords)
    {
        std::cout << "\nprobing three-note chords (finding a chord zone)\n";
        std::cout << "root  name    peak      bright   verdict\n";
        std::cout << "------------------------------------------\n";
        for (int n = low; n <= high; ++n)
        {
            const NoteMeasurement m = probe.measureChord ({ n, n + 3, n + 7 }, channel, 1.5);
            std::printf ("%4d  %-5s  %7.4f  %6.3f   %s\n",
                         n, noteName (n).toRawUTF8(), m.peak, m.brightness,
                         m.sounded ? "sounds" : "silent");
        }
    }

    return 0;
}
