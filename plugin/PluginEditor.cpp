#include "PluginEditor.h"

#include "ghostband/Groove.h"

#include <algorithm>

namespace {

// The engine's own vocabulary. Combo ids are 1-based indices into these, so the
// UI never has to hold a second copy of the spelling the engine expects.
const char* kStyleIds[] = { "hard_rock", "metal", "thrash", "groove_metal",
                            "doom", "sludge", "punk", "prog_metal",
                            "alt_rock", "emo", "ballad", "blues" };
const char* kTuningIds[] = { "standard", "drop_d", "drop_c", "b_standard" };

constexpr int kNumStyles  = static_cast<int> (sizeof (kStyleIds)  / sizeof (kStyleIds[0]));
constexpr int kNumTunings = static_cast<int> (sizeof (kTuningIds) / sizeof (kTuningIds[0]));

juce::String styleIdToName (int id)
{
    return (id >= 1 && id <= kNumStyles) ? juce::String (kStyleIds[id - 1])
                                         : juce::String ("hard_rock");
}

int styleNameToId (const juce::String& name)
{
    for (int i = 0; i < kNumStyles; ++i)
        if (name == kStyleIds[i]) return i + 1;
    return 0;   // unknown: leave the box blank rather than lie about it
}

juce::String tuningIdToName (int id)
{
    return (id >= 1 && id <= kNumTunings) ? juce::String (kTuningIds[id - 1])
                                          : juce::String ("standard");
}

int tuningNameToId (const juce::String& name)
{
    for (int i = 0; i < kNumTunings; ++i)
        if (name == kTuningIds[i]) return i + 1;
    return 0;
}

} // namespace

//==============================================================================

void SectionList::setSections (std::vector<gb::SectionReport> s)
{
    sections = std::move (s);
    setSize (getWidth(), juce::jmax (1, static_cast<int> (sections.size()) * rowHeight));
    repaint();
}

void SectionList::setPlayhead (int tick)
{
    if (tick == playheadTick) return;
    playheadTick = tick;
    repaint();
}

void SectionList::setQueued (int index)
{
    if (index == queuedIndex) return;
    queuedIndex = index;
    repaint();
}

int SectionList::rowAt (juce::Point<int> p) const
{
    if (p.y < 0) return -1;
    const int row = p.y / rowHeight;
    return row < static_cast<int> (sections.size()) ? row : -1;
}

void SectionList::setSelection (const std::vector<int>& indices)
{
    selection = indices;
    repaint();
}

void SectionList::mouseDown (const juce::MouseEvent& e)
{
    const int row = rowAt (e.getPosition());
    if (row < 0) return;

    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        if (onSectionToggled) onSectionToggled (row);
    }
    else if (onSectionClicked)
    {
        onSectionClicked (row);
    }
}

void SectionList::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.getPosition());
    if (row != hoverIndex) { hoverIndex = row; repaint(); }
}

void SectionList::mouseExit (const juce::MouseEvent&)
{
    if (hoverIndex != -1) { hoverIndex = -1; repaint(); }
}

int SectionList::tickToY (int tick) const
{
    for (size_t i = 0; i < sections.size(); ++i)
    {
        const gb::SectionReport& s = sections[i];
        if (tick >= s.startTick && tick < s.endTick && s.endTick > s.startTick)
        {
            const double through = static_cast<double> (tick - s.startTick)
                                 / (s.endTick - s.startTick);
            return static_cast<int> ((i + through) * rowHeight);
        }
    }
    return -1;
}

void SectionList::paint (juce::Graphics& g)
{
    g.fillAll (ghost::panel);

    if (sections.empty())
    {
        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText ("No arrangement yet.", getLocalBounds().reduced (12),
                    juce::Justification::centredTop);
        return;
    }

    for (size_t i = 0; i < sections.size(); ++i)
    {
        const gb::SectionReport& s = sections[i];
        const auto row = juce::Rectangle<int> (0, static_cast<int> (i) * rowHeight,
                                               getWidth(), rowHeight);

        const bool active = playheadTick >= s.startTick && playheadTick < s.endTick;
        const bool queued = static_cast<int> (i) == queuedIndex;
        const bool hover  = static_cast<int> (i) == hoverIndex;

        if (active)
        {
            g.setColour (ghost::accent.withAlpha (0.13f));
            g.fillRect (row);
            g.setColour (ghost::accent);
            g.fillRect (row.withWidth (3));
        }
        else if (i % 2 == 1)
        {
            g.setColour (ghost::background.withAlpha (0.35f));
            g.fillRect (row);
        }

        if (hover && ! active)
        {
            g.setColour (ghost::text.withAlpha (0.05f));
            g.fillRect (row);
        }

        const bool picked = std::find (selection.begin(), selection.end(),
                                       static_cast<int> (i)) != selection.end();
        if (picked)
        {
            g.setColour (ghost::accent.withAlpha (0.10f));
            g.fillRect (row);
            g.setColour (ghost::accent.withAlpha (0.55f));
            g.drawRect (row.reduced (1), 1);
        }

        if (queued)
        {
            g.setColour (ghost::warn.withAlpha (0.14f));
            g.fillRect (row);
            g.setColour (ghost::warn);
            g.fillRect (row.withWidth (3));

            g.setFont (juce::Font (juce::FontOptions (9.0f).withStyle ("Bold")));
            g.drawText ("NEXT", row.reduced (10, 0).removeFromRight (150)
                                   .removeFromLeft (40),
                        juce::Justification::centredLeft);
        }

        g.setColour (ghost::line);
        g.drawLine (0.0f, static_cast<float> (row.getBottom()),
                    static_cast<float> (getWidth()), static_cast<float> (row.getBottom()), 1.0f);

        auto r = row.reduced (10, 0);
        if (active) r.removeFromLeft (4);

        // Intensity as a bar: the single biggest lever on how a section behaves,
        // so it should be readable at a glance.
        auto meter = r.removeFromRight (54).reduced (0, 13);
        g.setColour (ghost::line);
        g.fillRoundedRectangle (meter.toFloat(), 2.0f);
        g.setColour ((active ? ghost::accent : ghost::accent.withAlpha (0.6f)));
        g.fillRoundedRectangle (meter.withWidth (juce::roundToInt (meter.getWidth() * s.intensity))
                                     .toFloat(), 2.0f);

        r.removeFromRight (10);                    // keep the counts off the meter
        auto counts = r.removeFromRight (96);
        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (juce::String (s.drumHits) + " / " + juce::String (s.bassNotes),
                    counts, juce::Justification::centredRight);

        auto nameArea = r.removeFromTop (rowHeight / 2).withTrimmedTop (5);
        g.setColour (active ? ghost::accent : ghost::text);
        g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
        g.drawText (juce::String (s.name), nameArea, juce::Justification::centredLeft);

        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (juce::String (s.bars) + " bars   "
                        + juce::String (s.feel).replace ("_", " ") + "   "
                        + juce::String (s.chords),
                    r.withTrimmedBottom (5), juce::Justification::centredLeft);
    }

    // The playhead itself, drawn last so it sits over everything.
    const int y = tickToY (playheadTick);
    if (y >= 0)
    {
        g.setColour (ghost::accent.withAlpha (0.9f));
        g.fillRect (0, y, getWidth(), 2);
    }
}

//==============================================================================

void CalibrationList::setRows (std::vector<Row> r)
{
    rows = std::move (r);
    setSize (getWidth(), juce::jmax (1, static_cast<int> (rows.size()) * rowHeight));
    repaint();
}

void CalibrationList::setSelected (int index)
{
    if (index == selected) return;
    selected = index;
    repaint();
}

void CalibrationList::mouseDown (const juce::MouseEvent& e)
{
    const int row = e.getPosition().y / rowHeight;
    if (row >= 0 && row < static_cast<int> (rows.size()) && onRowClicked)
        onRowClicked (row);
}

void CalibrationList::paint (juce::Graphics& g)
{
    g.fillAll (ghost::panel);

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto r = juce::Rectangle<int> (0, static_cast<int> (i) * rowHeight,
                                             getWidth(), rowHeight);
        const bool active = static_cast<int> (i) == selected;

        if (active)
        {
            g.setColour (ghost::accent.withAlpha (0.15f));
            g.fillRect (r);
            g.setColour (ghost::accent);
            g.fillRect (r.withWidth (3));
        }

        auto inner = r.reduced (12, 0);
        g.setColour (active ? ghost::accent : ghost::text);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (rows[i].label, inner.removeFromLeft (inner.getWidth() - 60),
                    juce::Justification::centredLeft);

        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("note " + juce::String (rows[i].note), inner,
                    juce::Justification::centredRight);
    }
}

//==============================================================================

void ControlList::setRows (std::vector<Row> r)
{
    rows = std::move (r);
    setSize (getWidth(), juce::jmax (1, static_cast<int> (rows.size()) * rowHeight));
    repaint();
}

void ControlList::setSelected (int index)
{
    if (index == selected) return;
    selected = index;
    repaint();
}

void ControlList::mouseDown (const juce::MouseEvent& e)
{
    const int row = e.getPosition().y / rowHeight;
    if (row >= 0 && row < static_cast<int> (rows.size()) && onRowClicked)
        onRowClicked (row);
}

void ControlList::paint (juce::Graphics& g)
{
    g.fillAll (ghost::colours::card);

    if (rows.empty())
    {
        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (emptyMessage, getLocalBounds().reduced (12),
                    juce::Justification::centredTop);
        return;
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto r = juce::Rectangle<int> (0, static_cast<int> (i) * rowHeight,
                                             getWidth(), rowHeight);
        const bool active = static_cast<int> (i) == selected;

        if (active)
        {
            g.setColour (ghost::accent.withAlpha (0.16f));
            g.fillRect (r);
            g.setColour (ghost::accent);
            g.fillRect (r.withWidth (3));
        }

        auto inner = r.reduced (12, 0);

        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("CC " + juce::String (rows[i].cc), inner.removeFromLeft (52),
                    juce::Justification::centredLeft);

        juce::String kind;
        if      (rows[i].type == "switch") kind = "  (switch)";
        else if (rows[i].type == "select") kind = "  (" + juce::String (rows[i].positions)
                                                + "-way)";

        g.drawText (rows[i].follows + kind,
                    inner.removeFromRight (150), juce::Justification::centredRight);

        g.setColour (active ? ghost::accent : ghost::text);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawText (rows[i].name, inner, juce::Justification::centredLeft);
    }
}

//==============================================================================

GhostbandEditor::GhostbandEditor (GhostbandProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    // Free plugin, quiet button. Opens in a browser rather than doing anything
    // clever, so it works the same everywhere and asks nothing of the host.
    styleButton (donateButton, false);
    addAndMakeVisible (donateButton);
    donateButton.onClick = []
    {
        juce::URL ("https://ko-fi.com/kyleyeroshefsky11806").launchInDefaultBrowser();
    };

    styleButton (loadButton, false);
    styleButton (reloadButton, false);
    styleButton (rollButton, true);
    styleButton (playPauseButton, false);
    addAndMakeVisible (playPauseButton);
    playPauseButton.onClick = [this]
    {
        processor.togglePaused();
        playPauseButton.setButtonText (processor.paused.load() ? "Play" : "Pause");
    };
    styleButton (calibrateButton, false);

    addAndMakeVisible (calibrateButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (reloadButton);
    addAndMakeVisible (rollButton);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (
            "Open a Ghostband plan",
            processor.getPlanFile().existsAsFile()
                ? processor.getPlanFile().getParentDirectory()
                : processor.bundledPlansFolder(),
            "*.json");

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const juce::File f = fc.getResult();
                                  if (f.existsAsFile())
                                      processor.loadPlan (f);
                              });
    };

    reloadButton.onClick = [this] { processor.reloadPlan(); };

    rollButton.onClick = [this]
    {
        // With sections selected, reroll only those - the rest of the song is
        // provably untouched, because each section derives its own seed. With
        // nothing selected, take a new global seed and reroll everything.
        if (! rerollSelection.empty())
        {
            processor.rerollSections (rerollSelection);
            return;
        }

        const int next = 1 + juce::Random::getSystemRandom().nextInt (999998);
        processor.seed.store (next);
        seedEditor.setText (juce::String (next), juce::dontSendNotification);
        processor.regenerate();
    };

    sectionList.onSectionToggled = [this] (int index)
    {
        const auto it = std::find (rerollSelection.begin(), rerollSelection.end(), index);
        if (it == rerollSelection.end()) rerollSelection.push_back (index);
        else                             rerollSelection.erase (it);

        std::sort (rerollSelection.begin(), rerollSelection.end());
        sectionList.setSelection (rerollSelection);
        updateRollButtonText();
    };

    // ---- song controls ----
    styleCombo (keyBox);
    styleCombo (styleBox);
    styleCombo (tuningBox);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (styleBox);
    addAndMakeVisible (tuningBox);

    // Both spellings, because a key is named by its music, not by its pitch
    // class - nobody writes a song in "A# minor" when they mean B flat minor.
    static const char* keyNames[12] = { "C", "C# / Db", "D", "D# / Eb", "E", "F",
                                        "F# / Gb", "G", "G# / Ab", "A", "A# / Bb", "B" };
    for (int i = 0; i < 12; ++i)
        keyBox.addItem (keyNames[i], i + 1);

    styleCombo (modeBox);
    addAndMakeVisible (modeBox);
    for (const char* m : { "major", "natural minor", "harmonic minor",
                           "dorian", "phrygian", "phrygian dominant", "mixolydian" })
        modeBox.addItem (m, modeBox.getNumItems() + 1);

    modeBox.onChange = [this]
    {
        if (modeBox.getSelectedId() > 0)
            processor.setMode (modeBox.getText().replace (" ", "_"));
    };

    // Ids are 1-based; the id order here is the order of kStyleIds below.
    const juce::StringArray styleNames { "hard rock", "metal", "thrash", "groove metal",
                                         "doom", "sludge", "punk", "prog metal", "alt rock" };
    for (int i = 0; i < styleNames.size(); ++i)
        styleBox.addItem (styleNames[i], i + 1);

    const juce::StringArray tuningNames { "standard", "drop D", "drop C", "B standard" };
    for (int i = 0; i < tuningNames.size(); ++i)
        tuningBox.addItem (tuningNames[i], i + 1);

    // Changing the key transposes every chord, written or generated. Setting it
    // to only affect auto progressions would make it silently do nothing on a
    // plan whose chords are spelled out - which is most of them.
    keyBox.onChange = [this]
    {
        const int id = keyBox.getSelectedId();
        if (id > 0) processor.setKeyPitchClass (id - 1);
    };

    styleBox.onChange = [this]
    {
        const int id = styleBox.getSelectedId();
        if (id > 0) processor.setStyle (styleIdToName (id));
    };

    tuningBox.onChange = [this]
    {
        const int id = tuningBox.getSelectedId();
        if (id > 0) processor.setBassTuning (tuningIdToName (id));
    };

    // The two feel dials become machined knobs; the editor's intensity field
    // stays a slider, because it sits in a form row of text fields.
    for (juce::Slider* s : std::initializer_list<juce::Slider*> { &complexitySlider, &humanizeSlider })
    {
        s->setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f, true);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 46, 15);
        s->setRange (0.0, 1.0, 0.01);
        s->setColour (juce::Slider::textBoxTextColourId, ghost::dim);
        s->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (*s);
    }

    complexitySlider.onValueChange = [this]
    {
        processor.complexity.store (complexitySlider.getValue());
        markDialsDirty();
    };
    humanizeSlider.onValueChange = [this]
    {
        processor.humanize.store (humanizeSlider.getValue());
        markDialsDirty();
    };

    // ---- per-part levels ----
    struct LevelKnob { juce::Slider* s; juce::Label* l; const char* name;
                       std::atomic<float>* target; };
    const LevelKnob levelKnobs[5] = {
        { &levelDrums,   &levelDrumsLabel,   "DRUMS",  &processor.levelDrums   },
        { &levelBass,    &levelBassLabel,    "BASS",   &processor.levelBass    },
        { &levelGuitar,  &levelGuitarLabel,  "GTR",    &processor.levelGuitar  },
        { &levelGuitar2, &levelGuitar2Label, "GTR 2",  &processor.levelGuitar2 },
        { &levelPiano,   &levelPianoLabel,   "PIANO",  &processor.levelPiano   },
    };

    for (const LevelKnob& k : levelKnobs)
    {
        k.s->setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k.s->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                  juce::MathConstants<float>::pi * 2.8f, true);
        k.s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.s->setRange (0.0, 1.0, 0.01);
        k.s->setValue (k.target->load(), juce::dontSendNotification);
        k.s->setDoubleClickReturnValue (true, 1.0);   // back to unity
        auto* target = k.target;
        auto& proc = processor;
        k.s->onValueChange = [this, target, &proc, s = k.s]
        {
            target->store (static_cast<float> (s->getValue()));
            proc.sendLevels();

            // Says which controller each knob actually reached, so a knob that
            // does nothing can be told apart from a knob whose message nothing
            // is listening to. "CC7(untaught)" means that part has no volume
            // control mapped, and CC 7 is a guess most instruments ignore.
            statusLabel.setText (proc.getLastMidiReport(), juce::dontSendNotification);
        };
        addAndMakeVisible (*k.s);
    }

    auto initLabel = [this] (juce::Label& l, const juce::String& t, float size,
                             juce::Colour c, juce::Justification j)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (size)));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (j);
        addAndMakeVisible (l);
    };

    initLabel (mixLabel,          "MIX",    10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (levelDrumsLabel,   "DRUMS",  9.0f,  ghost::dim, juce::Justification::centred);
    initLabel (levelBassLabel,    "BASS",   9.0f,  ghost::dim, juce::Justification::centred);
    initLabel (levelGuitarLabel,  "GTR",    9.0f,  ghost::dim, juce::Justification::centred);
    initLabel (levelGuitar2Label, "GTR 2",  9.0f,  ghost::dim, juce::Justification::centred);
    initLabel (levelPianoLabel,   "PIANO",  9.0f,  ghost::dim, juce::Justification::centred);
    initLabel (keyLabel,        "KEY",        10.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (modeLabel,       "MODE",       10.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (styleLabel,      "STYLE",      10.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (tuningLabel,     "BASS TUNING",10.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (tempoLabel,      "",           10.0f, ghost::dim,   juce::Justification::centredRight);
    initLabel (complexityLabel, "COMPLEXITY", 10.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (humanizeLabel,   "HUMANIZE",   10.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (seedLabel,       "SEED",       10.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (planLabel,       "",           13.0f, ghost::text,  juce::Justification::centredLeft);
    initLabel (headlineLabel,   "",           11.0f, ghost::accent, juce::Justification::centredLeft);
    initLabel (summaryLabel,    "",           11.0f, ghost::dim,   juce::Justification::centredRight);
    initLabel (transportLabel,  "stopped",    11.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (statusLabel,     "",           11.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (profilesLabel,   "",           10.0f, ghost::dim,   juce::Justification::centredLeft);

    // Deliberately not in any of updateModeVisibility's screen lists: this one
    // is on the footer rail, which every screen keeps, so it is always shown.
    initLabel (latencyLabel,    "",           10.0f, ghost::dim,   juce::Justification::centredRight);

    seedEditor.setJustification (juce::Justification::centredLeft);
    seedEditor.setInputRestrictions (7, "0123456789");
    seedEditor.setColour (juce::TextEditor::backgroundColourId, ghost::background);
    seedEditor.setColour (juce::TextEditor::outlineColourId, ghost::line);
    seedEditor.setColour (juce::TextEditor::focusedOutlineColourId, ghost::accent.withAlpha (0.6f));
    seedEditor.setColour (juce::TextEditor::textColourId, ghost::text);
    seedEditor.setFont (juce::Font (juce::FontOptions (13.0f)));
    seedEditor.onReturnKey = [this]
    {
        processor.seed.store (juce::jmax (1, seedEditor.getText().getIntValue()));
        processor.regenerate();
    };
    addAndMakeVisible (seedEditor);

    // Tempo, editable at last. Commits on Return and on losing focus, because a
    // field that only commits on Return silently discards what you typed the
    // moment you click somewhere else.
    bpmEditor.setJustification (juce::Justification::centredLeft);
    bpmEditor.setInputRestrictions (3, "0123456789");
    bpmEditor.setColour (juce::TextEditor::backgroundColourId, ghost::background);
    bpmEditor.setColour (juce::TextEditor::outlineColourId, ghost::line);
    bpmEditor.setColour (juce::TextEditor::focusedOutlineColourId, ghost::accent.withAlpha (0.6f));
    bpmEditor.setColour (juce::TextEditor::textColourId, ghost::text);
    bpmEditor.setFont (juce::Font (juce::FontOptions (13.0f)));

    const auto commitBpm = [this]
    {
        const int typed = bpmEditor.getText().getIntValue();
        if (typed <= 0)
        {
            bpmEditor.setText (juce::String (juce::roundToInt (processor.getPlanBpm())),
                               juce::dontSendNotification);
            return;
        }
        processor.setPlanBpm (typed);
    };

    bpmEditor.onReturnKey  = commitBpm;
    bpmEditor.onFocusLost  = commitBpm;
    addAndMakeVisible (bpmEditor);

    initLabel (bpmLabel, "BPM", 10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (rollHintLabel, "ctrl-click a section to reroll just that one",
               10.0f, ghost::dim, juce::Justification::centredRight);

    // Clicking a section queues it; the processor lands the jump on the next bar
    // line so the transition stays in time.
    sectionList.onSectionClicked = [this] (int index)
    {
        // In the editor the same click picks a section to edit; in the song view
        // it queues a live jump.
        if (screen == Screen::Edit)
        {
            editSelected = index;
            pullSectionEdit();
            return;
        }

        processor.queueSection (index);
        sectionList.setQueued (index);
    };

    viewport.setViewedComponent (&sectionList, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setColour (juce::ScrollBar::thumbColourId, ghost::line.brighter (0.4f));
    addAndMakeVisible (viewport);

    // ---- calibration ----
    for (juce::TextButton* b : std::initializer_list<juce::TextButton*> {
             &calibrateButton, &calDoneButton, &calSaveButton,
             &calLowerButton, &calHigherButton, &calPlayButton })
    {
        styleButton (*b, b == &calPlayButton);
        addAndMakeVisible (*b);
    }

    initLabel (calHintLabel, "", 12.0f, ghost::text, juce::Justification::centredLeft);
    initLabel (calNoteLabel, "", 22.0f, ghost::accent, juce::Justification::centred);

    styleButton (tempoModeButton, false);
    addChildComponent (tempoModeButton);

    const auto refreshTempoMode = [this]
    {
        const bool own = processor.usePlanTempo.load();
        tempoModeButton.setButtonText (own ? "Tempo: song" : "Tempo: host");
        statusLabel.setText (own
            ? "Songs play at their own tempo. The host's transport still starts and stops them."
            : "Songs follow the host tempo, so set it to match the song.",
            juce::dontSendNotification);
    };

    tempoModeButton.onClick = [this, refreshTempoMode]
    {
        processor.usePlanTempo.store (! processor.usePlanTempo.load());
        refreshTempoMode();
    };

    calibrateButton.onClick = [this] { screen = Screen::Calibrate; calSelected = 0;
                                       processor.enterCalibration(); };
    calDoneButton.onClick   = [this] { screen = Screen::Song; processor.exitCalibration(); };
    calPlayButton.onClick   = [this] { processor.auditionStep (calSelected); };
    calLowerButton.onClick  = [this] { processor.nudgeCalibrationNote (calSelected, -1); };
    calHigherButton.onClick = [this] { processor.nudgeCalibrationNote (calSelected, +1); };

    calSaveButton.onClick = [this]
    {
        juce::String err;
        if (processor.saveCalibration (err))
        {
            // Say what actually moved. The old message was a fixed string, so
            // the report naming every changed note - the whole point of it -
            // was built and then thrown away.
            calHintLabel.setText (err.isNotEmpty() ? err
                                                   : juce::String ("Saved."),
                                  juce::dontSendNotification);
            calHintLabel.setColour (juce::Label::textColourId, ghost::accent);
        }
        else
        {
            calHintLabel.setText (err, juce::dontSendNotification);
            calHintLabel.setColour (juce::Label::textColourId, ghost::warn);
        }
    };

    calList.onRowClicked = [this] (int row)
    {
        calSelected = row;
        calList.setSelected (row);
        processor.auditionStep (row);
        refreshCalibration();
    };

    calViewport.setViewedComponent (&calList, false);
    calViewport.setScrollBarsShown (true, false);
    calViewport.setColour (juce::ScrollBar::thumbColourId, ghost::line.brighter (0.4f));
    addChildComponent (calViewport);

    // ---- structure editing ----
    for (juce::TextButton* b : std::initializer_list<juce::TextButton*> {
             &editButton, &edDoneButton, &edAddButton, &edDeleteButton,
             &edUpButton, &edDownButton, &edSaveButton, &edSaveAsButton })
    {
        styleButton (*b, b == &edSaveButton);
        addAndMakeVisible (*b);
    }

    auto initField = [this] (juce::TextEditor& t)
    {
        t.setColour (juce::TextEditor::backgroundColourId, ghost::background);
        t.setColour (juce::TextEditor::outlineColourId, ghost::line);
        t.setColour (juce::TextEditor::focusedOutlineColourId, ghost::accent.withAlpha (0.6f));
        t.setColour (juce::TextEditor::textColourId, ghost::text);
        t.setFont (juce::Font (juce::FontOptions (13.0f)));
        t.onFocusLost = [this] { pushSectionEdit(); };
        t.onReturnKey = [this] { pushSectionEdit(); };
        addChildComponent (t);
    };

    initField (edName);
    initField (edBars);
    initField (edChords);
    edBars.setInputRestrictions (3, "0123456789");

    styleSlider (edIntensity);
    addChildComponent (edIntensity);
    edIntensity.onDragEnd = [this] { pushSectionEdit(); };

    styleCombo (edFeel);
    styleCombo (edFill);
    addChildComponent (edFeel);
    addChildComponent (edFill);
    for (const char* f : { "straight", "half_time", "double_time", "blast" })
        edFeel.addItem (juce::String (f).replace ("_", " "), edFeel.getNumItems() + 1);
    for (const char* f : { "auto", "none", "small", "big" })
        edFill.addItem (f, edFill.getNumItems() + 1);
    styleCombo (edLead);
    addChildComponent (edLead);
    for (const char* l : { "auto", "guitar", "piano", "both" })
        edLead.addItem (l, edLead.getNumItems() + 1);

    edFeel.onChange = [this] { pushSectionEdit(); };
    edFill.onChange = [this] { pushSectionEdit(); };
    edLead.onChange = [this] { pushSectionEdit(); };

    for (juce::ToggleButton* t : std::initializer_list<juce::ToggleButton*> {
             &edDrums, &edBass, &edGuitar, &edPiano })
    {
        t->setColour (juce::ToggleButton::textColourId, ghost::text);
        t->setColour (juce::ToggleButton::tickColourId, ghost::accent);
        t->setColour (juce::ToggleButton::tickDisabledColourId, ghost::line);
        t->onClick = [this] { pushSectionEdit(); };
        addChildComponent (*t);
    }

    initLabel (edNameLabel,      "NAME",      10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edBarsLabel,      "BARS",      10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edIntensityLabel, "INTENSITY", 10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edFeelLabel,      "FEEL",      10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edFillLabel,      "FILL",      10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edChordsLabel,    "CHORDS",    10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edPlaysLabel,     "PLAYS",     10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edLeadLabel,      "LEAD",      10.0f, ghost::dim, juce::Justification::centredLeft);

    editButton.onClick   = [this] { screen = Screen::Edit; editSelected = 0;
                                    pullSectionEdit(); updateModeVisibility(); };
    edDoneButton.onClick = [this] { screen = Screen::Song; updateModeVisibility(); };

    edAddButton.onClick    = [this] { processor.addSection (editSelected);
                                      editSelected = juce::jmin (editSelected + 1,
                                                                 processor.getSectionCount() - 1);
                                      pullSectionEdit(); };
    edDeleteButton.onClick = [this] { processor.deleteSection (editSelected);
                                      editSelected = juce::jlimit (0, processor.getSectionCount() - 1,
                                                                   editSelected);
                                      pullSectionEdit(); };
    edUpButton.onClick     = [this] { if (editSelected > 0)
                                      { processor.moveSection (editSelected, -1); --editSelected;
                                        pullSectionEdit(); } };
    edDownButton.onClick   = [this] { if (editSelected < processor.getSectionCount() - 1)
                                      { processor.moveSection (editSelected, +1); ++editSelected;
                                        pullSectionEdit(); } };

    edSaveButton.onClick = [this]
    {
        const juce::File target = processor.getPlanFile();
        if (! target.existsAsFile()) { edSaveAsButton.triggerClick(); return; }

        juce::String err;
        if (processor.savePlan (target, err))
            statusLabel.setText ("Saved " + target.getFileName()
                                     + "  (previous version kept alongside it)",
                                 juce::dontSendNotification);
        else
            statusLabel.setText (err, juce::dontSendNotification);
    };

    edSaveAsButton.onClick = [this]
    {
        const juce::File start = processor.getPlanFile().existsAsFile()
                                   ? processor.getPlanFile()
                                   : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                         .getChildFile ("my-song.json");

        chooser = std::make_unique<juce::FileChooser> ("Save the song", start, "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const juce::File f = fc.getResult();
                                  if (f == juce::File()) return;
                                  juce::String err;
                                  if (processor.savePlan (f.withFileExtension ("json"), err))
                                      statusLabel.setText ("Saved " + f.getFileName(),
                                                           juce::dontSendNotification);
                                  else
                                      statusLabel.setText (err, juce::dontSendNotification);
                              });
    };

    // ---- header navigation, settings and about ----
    for (juce::TextButton* b : std::initializer_list<juce::TextButton*> {
             &settingsButton, &aboutButton, &backButton,
             &resetSizeButton, &reloadProfilesBtn, &manualButton, &repoButton,
             &emailButton })
    {
        styleButton (*b, false);
        addAndMakeVisible (*b);
    }

    settingsButton.onClick = [this] { screen = Screen::Settings; updateModeVisibility(); };
    aboutButton.onClick    = [this] { screen = Screen::About;    updateModeVisibility(); };
    backButton.onClick     = [this] { screen = Screen::Song;     updateModeVisibility(); };

    resetSizeButton.onClick = [this] { setSize (600, 720); };

    reloadProfilesBtn.onClick = [this] { processor.reloadPlan(); };

    repoButton.onClick = []
    {
        juce::URL ("https://github.com/mourninggrace/Ghostband").launchInDefaultBrowser();
    };

    emailButton.onClick = []
    {
        juce::URL ("mailto:mourning.grace.2014@gmail.com?subject=Ghostband")
            .launchInDefaultBrowser();
    };

    manualButton.onClick = [this]
    {
        // Ships beside the plugin once it exists; until then, point at the
        // written documentation rather than pretending the button is broken.
        const juce::File pdf =
            juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                .getParentDirectory().getParentDirectory()
                .getChildFile ("Resources").getChildFile ("Ghostband-manual.pdf");

        if (pdf.existsAsFile())
            pdf.startAsProcess();
        else
            juce::URL ("https://github.com/mourninggrace/Ghostband#readme")
                .launchInDefaultBrowser();
    };

    struct ChannelBox { juce::ComboBox* box; juce::Label* label; const char* name;
                        std::atomic<int>* target; };
    const ChannelBox channelBoxes[5] = {
        { &chDrums,   &chDrumsLabel,   "DRUMS",    &processor.channelDrums   },
        { &chBass,    &chBassLabel,    "BASS",     &processor.channelBass    },
        { &chGuitar,  &chGuitarLabel,  "GUITAR",   &processor.channelGuitar  },
        { &chGuitar2, &chGuitar2Label, "GUITAR 2", &processor.channelGuitar2 },
        { &chPiano,   &chPianoLabel,   "PIANO",    &processor.channelPiano   },
    };

    for (const ChannelBox& c : channelBoxes)
    {
        styleCombo (*c.box);
        for (int ch = 1; ch <= 16; ++ch)
            c.box->addItem (juce::String (ch), ch);
        c.box->setSelectedId (c.target->load(), juce::dontSendNotification);

        auto* target = c.target;
        auto& proc = processor;
        c.box->onChange = [target, &proc, box = c.box]
        {
            if (box->getSelectedId() > 0)
            {
                target->store (box->getSelectedId());
                proc.applyChannels();
            }
        };
        addChildComponent (*c.box);

        initLabel (*c.label, c.name, 10.0f, ghost::dim, juce::Justification::centredLeft);
    }

    for (juce::Label* l : { &chDrumsName, &chBassName, &chGuitarName,
                            &chGuitar2Name, &chPianoName })
        initLabel (*l, "", 11.0f, ghost::text, juce::Justification::centredLeft);

    initLabel (learnPartName, "", 11.0f, ghost::accent, juce::Justification::centredLeft);

    juce::TextButton* testButtons[5] = { &testDrums, &testBass, &testGuitar,
                                         &testPiano, &testGuitar2 };
    for (int i = 0; i < 5; ++i)
    {
        styleButton (*testButtons[i], false);
        testButtons[i]->onClick = [this, i]
        {
            // Testing a part the song does not have used to send notes through
            // a default profile, whose guessed range the real instrument is
            // silent in. That produced visible MIDI and no sound - which reads
            // as a broken instrument rather than a song without one.
            if (! processor.partIsInSong (i))
            {
                static const char* names[] = { "drums", "bass", "guitar", "piano",
                                               "second guitar" };
                statusLabel.setText (juce::String ("This song has no ") + names[i]
                                         + ". Load a song that uses it, or add it in Edit song.",
                                     juce::dontSendNotification);
                return;
            }

            processor.testPart (i);
            statusLabel.setText (processor.getLastMidiReport(), juce::dontSendNotification);
        };
        addChildComponent (*testButtons[i]);
    }

    // ---- MIDI Learn helpers ----
    styleCombo (learnPart);
    addChildComponent (learnPart);
    learnPart.addItem ("drums",    1);
    learnPart.addItem ("bass",     2);
    learnPart.addItem ("guitar",   3);
    learnPart.addItem ("piano",    4);
    learnPart.addItem ("guitar 2", 5);
    learnPart.setSelectedId (3, juce::dontSendNotification);

    // An open-ended list rather than fixed slots: how many knobs, buttons and
    // switches are worth automating is the owner's decision.
    for (juce::TextButton* b : std::initializer_list<juce::TextButton*> {
             &ctlAdd, &ctlRemove, &ctlTeach, &ctlSave, &ctlSend, &ctlWalk })
    {
        styleButton (*b, b == &ctlTeach);
        addChildComponent (*b);
    }

    ctlName.setColour (juce::TextEditor::textColourId, ghost::text);
    ctlName.setFont (juce::Font (juce::FontOptions (13.0f)));
    ctlName.onFocusLost = [this] { pushControlEdit(); };
    ctlName.onReturnKey = [this] { pushControlEdit(); };
    addChildComponent (ctlName);

    styleCombo (ctlFollows);
    styleCombo (ctlType);
    addChildComponent (ctlFollows);
    addChildComponent (ctlType);

    ctlPositions.setColour (juce::TextEditor::textColourId, ghost::text);
    ctlPositions.setFont (juce::Font (juce::FontOptions (13.0f)));
    ctlPositions.setJustification (juce::Justification::centred);
    ctlPositions.setInputRestrictions (3, "0123456789");
    ctlPositions.onFocusLost = [this] { pushControlEdit(); };
    ctlPositions.onReturnKey = [this] { pushControlEdit(); };
    addChildComponent (ctlPositions);

    ctlValue.setColour (juce::TextEditor::textColourId, ghost::text);
    ctlValue.setFont (juce::Font (juce::FontOptions (13.0f)));
    ctlValue.setJustification (juce::Justification::centred);
    ctlValue.setInputRestrictions (3, "0123456789");
    ctlValue.onFocusLost = [this] { pushControlEdit(); };
    ctlValue.onReturnKey = [this] { pushControlEdit(); };
    addChildComponent (ctlValue);

    for (juce::TextEditor* e : { &ctlFrom, &ctlTo })
    {
        e->setColour (juce::TextEditor::textColourId, ghost::text);
        e->setFont (juce::Font (juce::FontOptions (13.0f)));
        e->setJustification (juce::Justification::centred);
        e->setInputRestrictions (3, "0123456789");
        e->onFocusLost = [this] { pushControlEdit(); };
        e->onReturnKey = [this] { pushControlEdit(); };
        addChildComponent (*e);
    }
    for (const char* f : { "intensity", "lead", "peaks", "rising",
                           "random", "random once", "level", "fixed", "none" })
        ctlFollows.addItem (f, ctlFollows.getNumItems() + 1);
    for (const char* t : { "knob", "switch", "select" })
        ctlType.addItem (t, ctlType.getNumItems() + 1);

    ctlFollows.onChange = [this] { pushControlEdit(); };
    ctlType.onChange    = [this] { pushControlEdit(); };

    // The list is in part order, so the id is the part index plus one.
    const auto part = [this] { return juce::jlimit (0, 4, learnPart.getSelectedId() - 1); };

    learnPart.onChange = [this] { ctlSelected = 0; refreshControls(); };

    ctlAdd.onClick    = [this, part] { processor.addControl (part());
                                       ctlSelected = juce::jmax (0, processor.getControlCount (part()) - 1);
                                       refreshControls(); };
    ctlRemove.onClick = [this, part] { processor.removeControl (part(), ctlSelected);
                                       ctlSelected = 0; refreshControls(); };
    ctlTeach.onClick  = [this, part] { processor.teachControlSlot (part(), ctlSelected); };

    ctlSend.onClick = [this, part]
    {
        pushControlEdit();          // commit a value still being typed
        processor.sendControlNow (part(), ctlSelected);
        statusLabel.setText ("Sent. Look at the instrument to see where it landed.",
                             juce::dontSendNotification);
    };

    ctlWalk.onClick = [this, part]
    {
        pushControlEdit();
        processor.walkControl (part(), ctlSelected);
        statusLabel.setText ("Stepping through every position, half a second each. Count them.",
                             juce::dontSendNotification);
    };

    ctlSave.onClick = [this, part]
    {
        juce::String err;
        if (processor.saveControls (part(), err))
            statusLabel.setText ("Mappings saved to the instrument's profile.",
                                 juce::dontSendNotification);
        else
            statusLabel.setText (err, juce::dontSendNotification);
    };

    ctlList.onRowClicked = [this] (int row) { ctlSelected = row; refreshControls(); };

    ctlViewport.setViewedComponent (&ctlList, false);
    ctlViewport.setScrollBarsShown (true, false);
    addChildComponent (ctlViewport);

    initLabel (ctlNameLabel,    "NAME",    10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlFollowsLabel, "FOLLOWS", 10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlTypeLabel,    "TYPE",    10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlPositionsLabel, "CHOICES", 10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlPositionsHint, "how many choices", 10.0f, ghost::dim,
               juce::Justification::centredLeft);
    initLabel (ctlValueLabel, "VALUE", 10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlRangeLabel, "RANGE", 10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlRangeToLabel, "to", 10.0f, ghost::dim, juce::Justification::centred);
    initLabel (ctlRangeHint, "", 10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlValueHint, "", 10.0f, ghost::dim, juce::Justification::centredLeft);

    initLabel (learnHeading, "MIDI LEARN", 11.0f, ghost::text, juce::Justification::centredLeft);
    initLabel (learnHelp,
               "Add a control, name it, choose what it should follow. Knob sweeps, switch is on or off, select holds one of a fixed set of choices. Use random for a control with no right answer, and level for the instrument's own volume so the mix knobs on the song screen reach it. Then put the control into MIDI Learn in the instrument and press Teach.",
               11.0f, ghost::dim, juce::Justification::topLeft);
    learnHelp.setJustificationType (juce::Justification::topLeft);

    initLabel (settingsHeading, "SETTINGS", 15.0f, ghost::text, juce::Justification::centredLeft);
    initLabel (channelsHelp,
               "Each part is sent on its own MIDI channel. Set the matching channel on each "
               "instrument, or use a channel filter in your host.",
               11.0f, ghost::dim, juce::Justification::topLeft);
    channelsHelp.setJustificationType (juce::Justification::topLeft);

    processor.stateChanged.addChangeListener (this);
    refreshFromProcessor();
    updateModeVisibility();

    // Resizable, with a floor that keeps the controls from overlapping. The
    // size lives on the processor so it survives closing the window and is
    // saved with the rest of the plugin state.
    setResizable (true, true);
    // The floor is what the song screen actually needs before the section list
    // starts being clipped, not an arbitrary small number.
    setResizeLimits (560, 690, 2200, 2000);

    // Restore the remembered size, then start recording changes to it. The
    // order matters: recording before this point captures the zero-sized
    // editor and loses what was remembered.
    setSize (juce::jmax (560, processor.editorWidth.load()),
             juce::jmax (690, processor.editorHeight.load()));
    sizeInitialised = true;

    // Fill it before the first tick, so the footer is never briefly blank -
    // and so an offline render of the editor shows it too.
    updateLatencyReadout();

    startTimerHz (30);
}

GhostbandEditor::~GhostbandEditor()
{
    stopTimer();
    processor.stateChanged.removeChangeListener (this);
    setLookAndFeel (nullptr);   // must outlive every child that uses it
}

void GhostbandEditor::markDialsDirty()
{
    dialsDirty   = true;
    lastDialMove = juce::Time::getMillisecondCounter();
}

// Read from the processor rather than hard-coded, so if Ghostband ever does
// take a lookahead the number moves on its own instead of quietly becoming a
// lie. The buffer size is the other half of the answer: it is where a rig's
// actual delay comes from, and it is the host's, not ours.
void GhostbandEditor::updateLatencyReadout()
{
    const double sr             = processor.getSampleRate();
    const int    latencySamples = processor.getLatencySamples();
    const int    blockSize      = processor.getBlockSize();

    juce::String text;
    if (sr > 0.0)
        text = "latency " + juce::String (latencySamples * 1000.0 / sr, 1) + " ms";
    else
        text = "latency " + juce::String (latencySamples) + " smp";

    if (blockSize > 0)
        text += "   buffer " + juce::String (blockSize);

    if (text == lastLatencyText)
        return;

    lastLatencyText = text;
    latencyLabel.setText (text, juce::dontSendNotification);
}

void GhostbandEditor::timerCallback()
{
    updateLatencyReadout();

    // Playhead.
    const int tick = processor.transportRunning.load() ? processor.playbackTick.load() : -1;
    if (tick != lastPlayheadTick)
    {
        lastPlayheadTick = tick;
        sectionList.setPlayhead (tick);

        juce::String where = "stopped";
        if (tick >= 0)
        {
            where = "playing";
            for (const gb::SectionReport& s : processor.getSections())
            {
                if (tick >= s.startTick && tick < s.endTick)
                {
                    const int barsIn = s.endTick > s.startTick
                                         ? (tick - s.startTick) * s.bars / (s.endTick - s.startTick)
                                         : 0;
                    where = "playing   " + juce::String (s.name)
                          + "   bar " + juce::String (barsIn + 1) + " of " + juce::String (s.bars);
                    break;
                }
            }
        }
        transportLabel.setText (where, juce::dontSendNotification);
        transportLabel.setColour (juce::Label::textColourId,
                                  tick >= 0 ? ghost::accent : ghost::dim);
    }

    // Queued section, which the processor clears once the jump lands.
    const int queued = processor.queuedSection.load();
    if (queued != lastQueued)
    {
        lastQueued = queued;
        sectionList.setQueued (queued);
    }

    // Host tempo, because Ghostband does not own it - the host does, and a BPM
    // control here would be a dead knob that looks live.
    const double bpm = processor.hostBpm.load();
    tempoLabel.setText (bpm > 0.0 ? juce::String (bpm, 1) + " bpm  (host)"
                                  : juce::String ("tempo from host"),
                        juce::dontSendNotification);

    // Debounced regenerate after the dials settle.
    if (dialsDirty && juce::Time::getMillisecondCounter() - lastDialMove > 200)
    {
        dialsDirty = false;
        processor.regenerate();
    }
}

void GhostbandEditor::styleButton (juce::TextButton& b, bool primary)
{
    b.getProperties().set ("primary", primary);

    b.setColour (juce::TextButton::buttonColourId, primary ? ghost::accent.withAlpha (0.16f)
                                                           : ghost::panel);
    b.setColour (juce::TextButton::buttonOnColourId, ghost::accent.withAlpha (0.30f));
    b.setColour (juce::TextButton::textColourOffId, primary ? ghost::accent : ghost::text);
    b.setColour (juce::ComboBox::outlineColourId, primary ? ghost::accent.withAlpha (0.45f)
                                                          : ghost::line);
}

void GhostbandEditor::updateRollButtonText()
{
    const int n = static_cast<int> (rerollSelection.size());
    // The hint lives on the button itself when nothing is picked. Ctrl-click to
    // select a section was only ever in a tooltip, so it was reported as "still
    // unable to roll one section" - the feature worked, nothing said how.
    rollButton.setButtonText (n == 0 ? "Roll"
                                     : "Roll " + juce::String (n)
                                           + (n == 1 ? " section" : " sections"));
    rollButton.setTooltip (n == 0
        ? "Rerolls the whole song. Ctrl-click sections to reroll only those."
        : "Rerolls only the selected sections; the rest of the song is untouched.");
}

void GhostbandEditor::styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, ghost::background);
    c.setColour (juce::ComboBox::outlineColourId, ghost::line);
    c.setColour (juce::ComboBox::textColourId, ghost::text);
    c.setColour (juce::ComboBox::arrowColourId, ghost::dim);
    c.setColour (juce::PopupMenu::backgroundColourId, ghost::panel);
    c.setColour (juce::PopupMenu::textColourId, ghost::text);
    c.setColour (juce::PopupMenu::highlightedBackgroundColourId, ghost::accent.withAlpha (0.22f));
    c.setColour (juce::PopupMenu::highlightedTextColourId, ghost::text);
}

void GhostbandEditor::styleSlider (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
    s.setRange (0.0, 1.0, 0.01);
    s.setColour (juce::Slider::backgroundColourId, ghost::line);
    s.setColour (juce::Slider::trackColourId, ghost::accent.withAlpha (0.65f));
    s.setColour (juce::Slider::thumbColourId, ghost::accent);
    s.setColour (juce::Slider::textBoxTextColourId, ghost::text);
    s.setColour (juce::Slider::textBoxBackgroundColourId, ghost::background);
    s.setColour (juce::Slider::textBoxOutlineColourId, ghost::line);
}

void GhostbandEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFromProcessor();
    updateModeVisibility();
}

void GhostbandEditor::updateModeVisibility()
{
    // Three screens share the window; exactly one set of controls is visible.
    // Spelled as initializer_list<Component*> because the members are all
    // different types and a bare braced list has nothing to deduce from.
    const bool song = (screen == Screen::Song);
    const bool cal  = (screen == Screen::Calibrate);
    const bool edit = (screen == Screen::Edit);
    const bool set  = (screen == Screen::Settings);
    const bool abt  = (screen == Screen::About);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &chDrums, &chBass, &chGuitar, &chGuitar2, &chPiano,
             &chDrumsLabel, &chBassLabel, &chGuitarLabel, &chGuitar2Label, &chPianoLabel,
             &chDrumsName, &chBassName, &chGuitarName, &chGuitar2Name, &chPianoName,
             &learnPartName,
             &settingsHeading, &channelsHelp, &resetSizeButton, &reloadProfilesBtn,
             &tempoModeButton,
             &testDrums, &testBass, &testGuitar, &testPiano, &testGuitar2,
             &learnPart, &learnHeading, &learnHelp,
             &ctlAdd, &ctlRemove, &ctlTeach, &ctlSave, &ctlName,
             &ctlFollows, &ctlType, &ctlPositions, &ctlViewport,
             &ctlNameLabel, &ctlFollowsLabel, &ctlTypeLabel, &ctlPositionsLabel,
             &ctlPositionsHint, &ctlValue, &ctlValueLabel, &ctlValueHint,
             &ctlSend, &ctlWalk, &ctlFrom, &ctlTo,
             &ctlRangeLabel, &ctlRangeToLabel, &ctlRangeHint })
        c->setVisible (set);

    if (set)
        refreshControls();

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &manualButton, &repoButton, &emailButton })
        c->setVisible (abt);

    // Settings and About are reachable from anywhere and lead back to the song.
    settingsButton.setVisible (! set && ! abt);
    aboutButton.setVisible    (! set && ! abt);
    backButton.setVisible     (set || abt);

    // Labels keep their bounds when a screen stops laying them out, so any that
    // the current screen does not position have to be hidden or they bleed
    // through it. Settings and About both paint over the song view's area.
    planLabel.setVisible (song || cal || edit);

    // Only the song screen ever positions the headline, so only the song screen
    // may show it. Left visible elsewhere it kept its old bounds and landed on
    // top of whatever that screen put there - the calibrate hint, and the edit
    // screen's name and bars fields.
    headlineLabel.setVisible (song);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &statusLabel, &profilesLabel })
        c->setVisible (! abt);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &loadButton, &reloadButton, &rollButton, &calibrateButton, &editButton,
             &complexitySlider, &humanizeSlider, &complexityLabel,
             &humanizeLabel, &seedEditor, &seedLabel, &keyBox, &styleBox,
             &tuningBox, &keyLabel, &styleLabel, &tuningLabel,
             &modeBox, &modeLabel,
             &tempoLabel, &transportLabel, &summaryLabel, &playPauseButton,
             &bpmLabel, &bpmEditor, &rollHintLabel,
             &mixLabel, &levelDrums, &levelBass, &levelGuitar, &levelGuitar2, &levelPiano,
             &levelDrumsLabel, &levelBassLabel, &levelGuitarLabel,
             &levelGuitar2Label, &levelPianoLabel })
        c->setVisible (song);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &calDoneButton, &calSaveButton, &calLowerButton, &calHigherButton,
             &calPlayButton, &calHintLabel, &calNoteLabel, &calViewport })
        c->setVisible (cal);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &edDoneButton, &edAddButton, &edDeleteButton, &edUpButton, &edDownButton,
             &edSaveButton, &edSaveAsButton, &edName, &edBars, &edChords,
             &edIntensity, &edFeel, &edFill, &edLead, &edDrums, &edBass, &edGuitar, &edPiano,
             &edNameLabel, &edBarsLabel, &edIntensityLabel, &edFeelLabel,
             &edFillLabel, &edChordsLabel, &edPlaysLabel, &edLeadLabel })
        c->setVisible (edit);

    // The section list is shared between the song view and the editor - the
    // same list, selected for a different reason.
    viewport.setVisible (song || edit);

    if (cal)  refreshCalibration();
    if (edit) pullSectionEdit();
    if (song) sectionList.setSelection (rerollSelection);

    resized();
}

const char* GhostbandEditor::screenName (int index)
{
    static const char* names[numScreens] = { "song", "calibrate", "edit", "settings", "about" };
    return names[juce::jlimit (0, numScreens - 1, index)];
}

void GhostbandEditor::showScreenForSnapshot (int index)
{
    switch (juce::jlimit (0, numScreens - 1, index))
    {
        case 1:  screen = Screen::Calibrate; processor.enterCalibration(); break;
        case 2:  screen = Screen::Edit;     break;
        case 3:  screen = Screen::Settings; break;
        case 4:  screen = Screen::About;    break;
        default: screen = Screen::Song;     processor.exitCalibration(); break;
    }
    updateModeVisibility();
}

double GhostbandEditor::controlUnitsToNorm (const juce::String& type, int positions, int typed)
{
    if (type == "select")
    {
        const int last = juce::jmax (1, positions - 1);
        return juce::jlimit (0, last, typed - 1) / static_cast<double> (last);
    }

    if (type == "switch")
        return typed != 0 ? 1.0 : 0.0;

    return juce::jlimit (0, 100, typed) / 100.0;
}

int GhostbandEditor::normToControlUnits (const juce::String& type, int positions, double v)
{
    if (type == "select")
        return juce::roundToInt (v * juce::jmax (1, positions - 1)) + 1;

    if (type == "switch")
        return v > 0.5 ? 1 : 0;

    return juce::roundToInt (v * 100.0);
}

juce::String GhostbandEditor::controlUnitsHint (const juce::String& type, int positions)
{
    if (type == "select") return "positions, 1 to " + juce::String (juce::jmax (2, positions));
    if (type == "switch") return "1 for on, 0 for off";
    return "per cent of the knob's travel";
}

void GhostbandEditor::refreshControls()
{
    // 0..4, not 0..3. Clamping to four parts while the buttons beside this list
    // clamped to five meant picking "guitar 2" showed the PIANO's mappings while
    // Teach and Save acted on the second guitar - a list of one instrument's
    // controls with another instrument's buttons under it.
    const int part = juce::jlimit (0, 4, learnPart.getSelectedId() - 1);
    const int count = processor.getControlCount (part);
    ctlSelected = juce::jlimit (0, juce::jmax (0, count - 1), ctlSelected);

    std::vector<ControlList::Row> rows;
    for (int i = 0; i < count; ++i)
    {
        const auto c = processor.getControl (part, i);
        rows.push_back ({ c.name, c.cc, c.follows, c.type, c.positions });
    }
    ctlList.setRows (std::move (rows));
    ctlList.setSelected (ctlSelected);
    ctlList.setSize (juce::jmax (100, ctlViewport.getWidth() - 10), ctlList.getHeight());

    // Guitar and piano are optional. When the loaded song has neither, its
    // mappings are still safely in the profile file - they just belong to a
    // song that is not open. Say that, rather than showing a blank list.
    const bool inSong = processor.partIsInSong (part);
    ctlList.setEmptyMessage (inSong
        ? "No controls mapped yet. Press Add."
        : "This song has no " + learnPart.getText()
              + ". Its mappings are safe in the instrument's profile - load a "
                "song that uses it to see them.");

    const bool any = count > 0;
    ctlAdd.setEnabled (inSong);
    ctlRemove.setEnabled (any);
    ctlTeach.setEnabled (any);
    ctlName.setEnabled (any);
    ctlFollows.setEnabled (any);
    ctlType.setEnabled (any);

    suppressControlCallbacks = true;
    bool isSelect = false;
    bool isFixed  = false;
    bool isDriven = false;
    if (any)
    {
        const auto c = processor.getControl (part, ctlSelected);
        ctlName.setText (c.name, juce::dontSendNotification);
        for (int i = 1; i <= ctlFollows.getNumItems(); ++i)
            if (ctlFollows.getItemText (i - 1) == c.follows)
                ctlFollows.setSelectedId (i, juce::dontSendNotification);
        for (int i = 1; i <= ctlType.getNumItems(); ++i)
            if (ctlType.getItemText (i - 1) == c.type)
                ctlType.setSelectedId (i, juce::dontSendNotification);

        isSelect = c.type == "select";
        if (isSelect)
            ctlPositions.setText (juce::String (juce::jmax (2, c.positions)),
                                  juce::dontSendNotification);

        // The same box means a different number for each type, because "which
        // position" and "how far up" are not the same question.
        const juce::String units = controlUnitsHint (c.type, c.positions);

        isFixed = c.follows == "fixed";
        if (isFixed)
        {
            ctlValue.setText (juce::String (normToControlUnits (c.type, c.positions, c.low)),
                              juce::dontSendNotification);
            ctlValueHint.setText (units, juce::dontSendNotification);
        }

        isDriven = c.follows != "fixed" && c.follows != "none";
        if (isDriven)
        {
            ctlFrom.setText (juce::String (normToControlUnits (c.type, c.positions, c.low)),
                             juce::dontSendNotification);
            ctlTo.setText   (juce::String (normToControlUnits (c.type, c.positions, c.high)),
                             juce::dontSendNotification);

            // Said plainly, because "put the bigger number first" is not
            // something anyone guesses at a pair of boxes.
            ctlRangeHint.setText (units + " - put the higher one first to invert",
                                  juce::dontSendNotification);
        }

        ctlTeach.setButtonText ("Teach  (CC " + juce::String (c.cc) + ")");
    }
    else
    {
        ctlName.setText ({}, juce::dontSendNotification);
        ctlTeach.setButtonText ("Teach this control");
    }

    // The choices box is only meaningful for a selector, so it appears with one
    // rather than sitting there greyed out asking to be misread.
    const bool showChoices = any && isSelect && screen == Screen::Settings;
    ctlPositions.setVisible (showChoices);
    ctlPositionsLabel.setVisible (showChoices);
    ctlPositionsHint.setVisible (showChoices);
    ctlWalk.setVisible (showChoices);

    // The value box only means anything for a parked control, and the layout
    // gives back its row when it is not there.
    const bool showValue = any && isFixed && screen == Screen::Settings;
    ctlValue.setVisible (showValue);
    ctlValueLabel.setVisible (showValue);
    ctlValueHint.setVisible (showValue);
    ctlSend.setVisible (showValue);

    // A parked control has a value and no range; a driven one has a range and
    // no single value. They share the row because they are never both true.
    const bool showRange = any && isDriven && screen == Screen::Settings;
    ctlFrom.setVisible (showRange);
    ctlTo.setVisible (showRange);
    ctlRangeLabel.setVisible (showRange);
    ctlRangeToLabel.setVisible (showRange);
    ctlRangeHint.setVisible (showRange);

    suppressControlCallbacks = false;

    // The value row appears and disappears, so the rows under it have to move.
    if (screen == Screen::Settings)
        resized();
}

void GhostbandEditor::pushControlEdit()
{
    if (suppressControlCallbacks || screen != Screen::Settings)
        return;

    const int part = juce::jlimit (0, 4, learnPart.getSelectedId() - 1);
    if (processor.getControlCount (part) == 0)
        return;

    const GhostbandProcessor::ControlSlot before = processor.getControl (part, ctlSelected);

    GhostbandProcessor::ControlSlot s = before;
    s.name    = ctlName.getText();
    s.follows = ctlFollows.getText();
    s.type    = ctlType.getText();
    if (s.type == "select")
    {
        // 128 is the ceiling because a controller has no more values than that.
        // An empty or half-typed box must not collapse the mapping to two
        // choices, so a number below two is left as it was.
        const int typed = ctlPositions.getText().getIntValue();
        s.positions = typed >= 2 ? juce::jmin (128, typed)
                                 : juce::jmax (2, s.positions);
    }
    if (s.follows == "fixed")
    {
        if (ctlValue.getText().isNotEmpty())
        {
            // low and high both move: a parked control has no range to travel.
            s.low = s.high = controlUnitsToNorm (s.type, s.positions,
                                                 ctlValue.getText().getIntValue());
        }
    }
    else if (before.follows == "fixed" && s.low == s.high)
    {
        // Coming off "fixed" leaves low and high sitting on the same number,
        // which is a range with nowhere to travel: the control would keep the
        // parked value and never move, however it was set to follow. Two
        // controls were found silently pinned this way, both marked "random
        // once" and both stuck on one choice for every song.
        s.low  = 0.0;
        s.high = 1.0;
    }
    else if (s.follows != "none")
    {
        // Not clamped into order: from above to is how a control that reads
        // backwards gets inverted, and the engine already interpolates either
        // way round.
        if (ctlFrom.getText().isNotEmpty())
            s.low = controlUnitsToNorm (s.type, s.positions, ctlFrom.getText().getIntValue());
        if (ctlTo.getText().isNotEmpty())
            s.high = controlUnitsToNorm (s.type, s.positions, ctlTo.getText().getIntValue());
    }

    processor.updateControl (part, ctlSelected, s);
    refreshControls();
}

void GhostbandEditor::pullSectionEdit()
{
    const int count = processor.getSectionCount();
    editSelected = juce::jlimit (0, juce::jmax (0, count - 1), editSelected);

    const auto e = processor.getSectionEdit (editSelected);

    // Guarded, because setting a control's value fires its callback, which would
    // immediately write the half-populated form back over the section.
    suppressEditCallbacks = true;
    edName.setText (e.name, juce::dontSendNotification);
    edBars.setText (juce::String (e.bars), juce::dontSendNotification);
    edChords.setText (e.chords, juce::dontSendNotification);
    edIntensity.setValue (e.intensity, juce::dontSendNotification);

    const juce::String feelText = e.feel.replace ("_", " ");
    for (int i = 1; i <= edFeel.getNumItems(); ++i)
        if (edFeel.getItemText (i - 1) == feelText)
            edFeel.setSelectedId (i, juce::dontSendNotification);
    for (int i = 1; i <= edFill.getNumItems(); ++i)
        if (edFill.getItemText (i - 1) == e.fill)
            edFill.setSelectedId (i, juce::dontSendNotification);
    for (int i = 1; i <= edLead.getNumItems(); ++i)
        if (edLead.getItemText (i - 1) == e.lead)
            edLead.setSelectedId (i, juce::dontSendNotification);

    edDrums.setToggleState  (e.drums,  juce::dontSendNotification);
    edBass.setToggleState   (e.bass,   juce::dontSendNotification);
    edGuitar.setToggleState (e.guitar, juce::dontSendNotification);
    edPiano.setToggleState  (e.piano,  juce::dontSendNotification);
    suppressEditCallbacks = false;

    sectionList.setSelection ({ editSelected });
    edDeleteButton.setEnabled (count > 1);
    edUpButton.setEnabled (editSelected > 0);
    edDownButton.setEnabled (editSelected < count - 1);
}

void GhostbandEditor::pushSectionEdit()
{
    if (suppressEditCallbacks || screen != Screen::Edit)
        return;

    GhostbandProcessor::SectionEdit e;
    e.name      = edName.getText();
    e.bars      = juce::jmax (1, edBars.getText().getIntValue());
    e.intensity = edIntensity.getValue();
    e.feel      = edFeel.getText().replace (" ", "_");
    e.fill      = edFill.getText();
    e.lead      = edLead.getText();
    e.chords    = edChords.getText();
    e.drums     = edDrums.getToggleState();
    e.bass      = edBass.getToggleState();
    e.guitar    = edGuitar.getToggleState();
    e.piano     = edPiano.getToggleState();

    processor.applySectionEdit (editSelected, e);
}

void GhostbandEditor::refreshCalibration()
{
    const int count = processor.getCalibrationStepCount();
    calSelected = juce::jlimit (0, juce::jmax (0, count - 1), calSelected);

    std::vector<CalibrationList::Row> rows;
    rows.reserve (static_cast<size_t> (count));
    for (int i = 0; i < count; ++i)
    {
        const auto s = processor.getCalibrationStep (i);
        rows.push_back ({ s.label, s.note });
    }
    calList.setRows (std::move (rows));
    calList.setSelected (calSelected);
    calList.setSize (juce::jmax (100, calViewport.getWidth() - 10), calList.getHeight());

    const auto s = processor.getCalibrationStep (calSelected);
    calNoteLabel.setText ("note " + juce::String (s.note), juce::dontSendNotification);

    if (! calHintLabel.getText().startsWith ("Saved"))
    {
        calHintLabel.setText ("Press Play. If it does not sound like a " + s.label
                                  + ", use < and > until it does.",
                              juce::dontSendNotification);
        calHintLabel.setColour (juce::Label::textColourId, ghost::text);
    }

    calSaveButton.setEnabled (processor.calibrationHasEdits());
}

void GhostbandEditor::refreshFromProcessor()
{
    const auto s = processor.getStatus();

    planLabel.setText (s.planName, juce::dontSendNotification);
    headlineLabel.setText (s.headline, juce::dontSendNotification);

    if (s.ok)
    {
        const int mins = static_cast<int> (s.seconds) / 60;
        const int secs = static_cast<int> (s.seconds) % 60;
        summaryLabel.setText (juce::String (s.bars) + " bars   "
                                  + juce::String (mins) + ":"
                                  + juce::String (secs).paddedLeft ('0', 2)
                                  + "   " + juce::String (s.drumHits) + " hits / "
                                  + juce::String (s.bassNotes) + " notes",
                              juce::dontSendNotification);
    }
    else
    {
        summaryLabel.setText ({}, juce::dontSendNotification);
    }

    statusLabel.setText (s.message, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId,
                           s.unverifiedProfiles || ! s.ok ? ghost::warn : ghost::dim);

    juce::String profiles = "drums: " + s.drumProfile + "    bass: " + s.bassProfile;
    // Which instrument is in each slot, beside the slot. The rows name a part
    // and the part is a different plugin from one song to the next.
    {
        const auto slot = [] (const juce::String& name)
        {
            return name.isNotEmpty() ? name : juce::String ("- not in this song -");
        };

        chDrumsName.setText   (slot (s.drumProfile),    juce::dontSendNotification);
        chBassName.setText    (slot (s.bassProfile),    juce::dontSendNotification);
        chGuitarName.setText  (slot (s.guitarProfile),  juce::dontSendNotification);
        chGuitar2Name.setText (slot (s.guitar2Profile), juce::dontSendNotification);
        chPianoName.setText   (slot (s.pianoProfile),   juce::dontSendNotification);

        const juce::String forPart[5] = { s.drumProfile, s.bassProfile, s.guitarProfile,
                                          s.pianoProfile, s.guitar2Profile };
        const int chosen = juce::jlimit (0, 4, learnPart.getSelectedId() - 1);
        learnPartName.setText (slot (forPart[chosen]), juce::dontSendNotification);

        for (juce::Label* l : { &chDrumsName, &chBassName, &chGuitarName,
                                &chGuitar2Name, &chPianoName })
            l->setColour (juce::Label::textColourId,
                          l->getText().startsWith ("-") ? ghost::dim : ghost::text);
    }

    if (s.guitarProfile.isNotEmpty())  profiles += "    gtr: "  + s.guitarProfile;
    if (s.guitar2Profile.isNotEmpty()) profiles += "    gtr2: " + s.guitar2Profile;
    if (s.pianoProfile.isNotEmpty())   profiles += "    piano: " + s.pianoProfile;
    profilesLabel.setText (profiles, juce::dontSendNotification);

    complexitySlider.setValue (processor.complexity.load(), juce::dontSendNotification);
    humanizeSlider.setValue (processor.humanize.load(), juce::dontSendNotification);
    seedEditor.setText (juce::String (processor.seed.load()), juce::dontSendNotification);

    // Not while it has focus, or the refresh overwrites what is being typed.
    if (! bpmEditor.hasKeyboardFocus (true))
        bpmEditor.setText (juce::String (juce::roundToInt (processor.getPlanBpm())),
                           juce::dontSendNotification);

    keyBox.setSelectedId (processor.getKeyPitchClass() + 1, juce::dontSendNotification);
    styleBox.setSelectedId (styleNameToId (processor.getStyle()), juce::dontSendNotification);
    tuningBox.setSelectedId (tuningNameToId (processor.getBassTuning()), juce::dontSendNotification);

    const juce::String modeText = processor.getMode().replace ("_", " ");
    for (int i = 1; i <= modeBox.getNumItems(); ++i)
        if (modeBox.getItemText (i - 1) == modeText)
            modeBox.setSelectedId (i, juce::dontSendNotification);

    sectionList.setSections (processor.getSections());
    sectionList.setSize (viewport.getWidth() > 0 ? viewport.getWidth() - 10 : 500,
                         sectionList.getHeight());

    // Drop any selection that a newly loaded plan no longer has room for.
    const int count = static_cast<int> (processor.getSections().size());
    rerollSelection.erase (std::remove_if (rerollSelection.begin(), rerollSelection.end(),
                                           [count] (int i) { return i >= count; }),
                           rerollSelection.end());
    updateRollButtonText();

    // In the editor the highlight means "being edited", so leave it alone.
    if (screen == Screen::Edit)
        sectionList.setSelection ({ editSelected });
    else
        sectionList.setSelection (rerollSelection);
}

void GhostbandEditor::paint (juce::Graphics& g)
{
    g.fillAll (ghost::colours::background);

    // ---- header ----
    // Flat, generous, and separated by a single gradient hairline rather than a
    // bevel. The gradient is the one visual signature; it appears here and on
    // anything active, and nowhere else.
    auto header = getLocalBounds().removeFromTop (62).toFloat();

    auto title = header.reduced (20.0f, 0.0f);

    g.setColour (ghost::colours::text);
    g.setFont (juce::Font (juce::FontOptions (23.0f).withStyle ("Bold")));
    g.drawText ("GHOSTBAND", title.withTrimmedBottom (22.0f).toNearestInt(),
                juce::Justification::centredLeft);

    g.setColour (ghost::colours::dim);
    g.setFont (juce::Font (juce::FontOptions (9.5f)));
    g.drawText ("MIDI BRAIN   -   DRUMS   BASS   GUITAR x2   PIANO",
                title.withTrimmedTop (36.0f).toNearestInt(),
                juce::Justification::centredLeft);

    // Transport dot.
    const bool running = processor.transportRunning.load();
    const auto lamp = juce::Rectangle<float> (8.0f, 8.0f)
                          .withCentre ({ header.getRight() - 24.0f, header.getCentreY() });
    ghost::drawLamp (g, lamp, running);

    g.setColour (running ? ghost::colours::red : ghost::colours::dim);
    g.setFont (juce::Font (juce::FontOptions (8.5f)));
    g.drawText (running ? "RUNNING" : "IDLE",
                juce::Rectangle<int> (static_cast<int> (header.getRight()) - 110,
                                      static_cast<int> (header.getCentreY()) - 6, 76, 12),
                juce::Justification::centredRight);

    const auto rule = juce::Rectangle<float> (header.getX(), header.getBottom() - 1.5f,
                                              header.getWidth(), 1.5f);
    g.setGradientFill (ghost::accentGradient (rule));
    g.fillRect (rule);

    // ---- footer ----
    auto footer = getLocalBounds().removeFromBottom (38).toFloat();
    g.setColour (ghost::colours::line);
    g.fillRect (footer.withHeight (1.0f));

    if (screen == Screen::About)
        paintAbout (g, getLocalBounds().withTrimmedTop (62).withTrimmedBottom (38).reduced (20, 14));
}

void GhostbandEditor::paintAbout (juce::Graphics& g, juce::Rectangle<int> area)
{
    // Every block is measured before it is drawn.
    //
    // This screen used to hand drawFittedText a fixed height per paragraph and
    // hope. When the text needed more lines than the box had room for, JUCE drew
    // them on top of each other - two paragraphs overlapping into an unreadable
    // smear, with a screen of empty space underneath. Laying each block out and
    // asking how tall it came out is the only version of this that cannot go
    // wrong when a font, a window size or a sentence changes.
   #ifndef GHOSTBAND_VERSION
    #define GHOSTBAND_VERSION "dev"
   #endif

    auto a = area.reduced (4, 0);
    const float width = static_cast<float> (a.getWidth());
    int y = a.getY();

    const auto block = [&] (const juce::String& text, juce::Font font,
                            juce::Colour colour, int spaceAfter)
    {
        juce::AttributedString as;
        as.setLineSpacing (3.0f);
        as.append (text, font, colour);

        juce::TextLayout layout;
        layout.createLayout (as, width);
        layout.draw (g, juce::Rectangle<float> (static_cast<float> (a.getX()),
                                                static_cast<float> (y),
                                                width, layout.getHeight()));
        y += static_cast<int> (layout.getHeight()) + spaceAfter;
    };

    // The header band already says GHOSTBAND, so the hero line here says what it
    // is instead of saying the name twice.
    block ("A MIDI brain for the instruments you already own.",
           juce::Font (juce::FontOptions (19.0f).withStyle ("Bold")),
           ghost::colours::text, 6);

    block ("Version " GHOSTBAND_VERSION "   ::   by Kyle Yeroshefsky",
           juce::Font (juce::FontOptions (12.0f)),
           ghost::colours::silver, 16);

    {
        const auto rule = juce::Rectangle<float> (static_cast<float> (a.getX()),
                                                  static_cast<float> (y), width, 2.0f);
        g.setGradientFill (ghost::accentGradient (rule));
        g.fillRect (rule);
        y += 20;
    }

    block ("Ghostband makes no sound of its own. It writes an arrangement - drums, "
           "bass, guitar and piano - and performs it through the instruments you "
           "already own, by sending them MIDI.",
           juce::Font (juce::FontOptions (13.5f)), ghost::colours::text, 12);

    block ("You own the song: its key, tempo, style, and the order of its sections. "
           "Ghostband fills in the playing. Every part is generated from a seed, so "
           "the same song always comes back exactly as you left it - and rerolling "
           "one section never disturbs another.",
           juce::Font (juce::FontOptions (13.5f)), ghost::colours::text, 22);

    // A small labelled block, which reads as specification rather than prose.
    {
        const juce::Font label (juce::FontOptions (10.0f).withStyle ("Bold"));
        const juce::Font value (juce::FontOptions (12.0f));

        struct Row { const char* label; juce::String value; };
        const Row rows[] = {
            { "LICENCE",  "GNU AGPLv3 - free software, and free of charge" },
            { "BUILT ON", "JUCE" },
            { "CONTACT",  "mourning.grace.2014@gmail.com" },
        };

        for (const Row& row : rows)
        {
            const int rowY = y;

            g.setColour (ghost::colours::dim);
            g.setFont (label);
            g.drawText (row.label, a.getX(), rowY, 78, 18, juce::Justification::centredLeft);

            g.setColour (ghost::colours::silver);
            g.setFont (value);
            g.drawText (row.value, a.getX() + 84, rowY,
                        a.getWidth() - 84, 18, juce::Justification::centredLeft);

            y = rowY + 22;
        }

        y += 6;
    }

    block ("The donate button is a button, not a nag. Nothing is gated behind it and "
           "nothing ever will be.",
           juce::Font (juce::FontOptions (11.5f)), ghost::colours::dim, 0);
}

void GhostbandEditor::resized()
{
    // Repaint the whole canvas, not just whatever the resize newly exposed.
    //
    // Every other screen is built from child components, and a component that
    // is moved or resized redraws itself - so those screens heal on their own
    // and this was never needed. The About screen is painted straight onto the
    // canvas, and its text is wrapped to the width it was painted at. Growing
    // the window redrew only the newly uncovered strip, which left the old
    // narrow wrap sitting underneath the new wide one: two layouts of the same
    // paragraphs on top of each other, each with pieces of the other missing.
    // That is what "the about section is still botched" was, and it is why
    // fixing the measurement last time did not fix it.
    //
    // Neither the overlap checker nor the snapshots can see this: both render
    // into a fresh image, which is a full repaint by definition. It only exists
    // on a screen that has already been painted at another size.
    repaint();

    // Only remember a size the user can actually have chosen.
    if (sizeInitialised && getWidth() > 0 && getHeight() > 0)
    {
        processor.editorWidth.store (getWidth());
        processor.editorHeight.store (getHeight());
    }

    auto r = getLocalBounds();
    r.removeFromTop (62);

    // Footer rail: present on every screen, so the donate button never moves.
    auto footerRail = r.removeFromBottom (38).reduced (20, 8);
    donateButton.setBounds (footerRail.removeFromRight (150));

    // Immediately left of the donate button, right-aligned against it, so the
    // reading sits in the bottom right corner on every screen and never moves.
    footerRail.removeFromRight (12);
    latencyLabel.setBounds (footerRail.removeFromRight (190));

    r = r.reduced (20, 14);

    // Header navigation sits in the header band itself, above everything else.
    {
        auto nav = getLocalBounds().removeFromTop (62).reduced (20, 0);
        nav = nav.removeFromRight (250).withSizeKeepingCentre (250, 26);
        nav.removeFromRight (118);   // clear of the transport indicator
        if (backButton.isVisible())
        {
            backButton.setBounds (nav.removeFromLeft (62));
        }
        else
        {
            settingsButton.setBounds (nav.removeFromLeft (66));
            nav.removeFromLeft (6);
            aboutButton.setBounds (nav.removeFromLeft (56));
        }
    }

    if (screen == Screen::About)
    {
        auto a = r;
        auto buttons = a.removeFromBottom (30);
        manualButton.setBounds (buttons.removeFromLeft (118));
        buttons.removeFromLeft (8);
        repoButton.setBounds (buttons.removeFromLeft (118));
        buttons.removeFromLeft (8);
        emailButton.setBounds (buttons.removeFromLeft (118));
        return;
    }

    if (screen == Screen::Settings)
    {
        // Reserve the footer before anything else takes the space, or the list
        // grows straight over it.
        auto footerArea = r.removeFromBottom (32);
        statusLabel.setBounds (footerArea.removeFromTop (16));
        profilesLabel.setBounds (footerArea);
        r.removeFromBottom (10);

        auto s = r;
        settingsHeading.setBounds (s.removeFromTop (24));
        s.removeFromTop (10);

        juce::ComboBox* boxes[5]  = { &chDrums, &chBass, &chGuitar, &chPiano, &chGuitar2 };
        juce::Label*    labels[5] = { &chDrumsLabel, &chBassLabel, &chGuitarLabel,
                                      &chPianoLabel, &chGuitar2Label };
        juce::TextButton* tests[5] = { &testDrums, &testBass, &testGuitar,
                                       &testPiano, &testGuitar2 };
        for (int i = 0; i < 5; ++i)
        {
            juce::Label* names[5] = { &chDrumsName, &chBassName, &chGuitarName,
                                      &chPianoName, &chGuitar2Name };

            auto row = s.removeFromTop (28);
            labels[i]->setBounds (row.removeFromLeft (70));
            boxes[i]->setBounds (row.removeFromLeft (78));
            row.removeFromLeft (10);
            tests[i]->setBounds (row.removeFromLeft (66));
            row.removeFromLeft (14);
            names[i]->setBounds (row);
            s.removeFromTop (6);
        }

        s.removeFromTop (4);
        channelsHelp.setBounds (s.removeFromTop (34));
        s.removeFromTop (12);

        learnHeading.setBounds (s.removeFromTop (16));
        s.removeFromTop (4);
        learnHelp.setBounds (s.removeFromTop (34));
        s.removeFromTop (6);

        auto learnRow = s.removeFromTop (28);
        learnPart.setBounds (learnRow.removeFromLeft (96));
        learnRow.removeFromLeft (10);
        ctlAdd.setBounds (learnRow.removeFromLeft (70));
        learnRow.removeFromLeft (6);
        ctlRemove.setBounds (learnRow.removeFromLeft (78));
        learnRow.removeFromLeft (6);
        ctlSave.setBounds (learnRow.removeFromLeft (124));
        learnRow.removeFromLeft (14);
        learnPartName.setBounds (learnRow);

        s.removeFromTop (8);
        auto editRow = s.removeFromTop (26);
        ctlNameLabel.setBounds (editRow.removeFromLeft (42));
        ctlName.setBounds (editRow.removeFromLeft (130));
        editRow.removeFromLeft (10);
        ctlFollowsLabel.setBounds (editRow.removeFromLeft (54));
        ctlFollows.setBounds (editRow.removeFromLeft (98));
        editRow.removeFromLeft (10);
        ctlTypeLabel.setBounds (editRow.removeFromLeft (36));
        ctlType.setBounds (editRow.removeFromLeft (84));

        s.removeFromTop (8);
        auto teachRow = s.removeFromTop (28);
        ctlTeach.setBounds (teachRow.removeFromLeft (170));
        teachRow.removeFromLeft (16);
        ctlPositionsLabel.setBounds (teachRow.removeFromLeft (54));
        ctlPositions.setBounds (teachRow.removeFromLeft (52).withSizeKeepingCentre (52, 26));
        teachRow.removeFromLeft (8);
        ctlWalk.setBounds (teachRow.removeFromLeft (112));
        teachRow.removeFromLeft (8);
        ctlPositionsHint.setBounds (teachRow);

        if (ctlValue.isVisible() || ctlFrom.isVisible())
        {
            s.removeFromTop (6);
            auto valueRow = s.removeFromTop (26);
            valueRow.removeFromLeft (186);          // line up under the choices box

            if (ctlValue.isVisible())
            {
                ctlValueLabel.setBounds (valueRow.removeFromLeft (54));
                ctlValue.setBounds (valueRow.removeFromLeft (52).withSizeKeepingCentre (52, 26));
                valueRow.removeFromLeft (8);
                ctlSend.setBounds (valueRow.removeFromLeft (72));
                valueRow.removeFromLeft (8);
                ctlValueHint.setBounds (valueRow);
            }
            else
            {
                ctlRangeLabel.setBounds (valueRow.removeFromLeft (54));
                ctlFrom.setBounds (valueRow.removeFromLeft (48).withSizeKeepingCentre (48, 26));
                ctlRangeToLabel.setBounds (valueRow.removeFromLeft (24));
                ctlTo.setBounds (valueRow.removeFromLeft (48).withSizeKeepingCentre (48, 26));
                valueRow.removeFromLeft (10);
                ctlRangeHint.setBounds (valueRow);
            }
        }
        s.removeFromTop (8);

        auto listArea = s.removeFromTop (juce::jmax (60, s.getHeight() - 46));
        ctlViewport.setBounds (listArea);
        ctlList.setSize (listArea.getWidth() - 10, ctlList.getHeight());
        s.removeFromTop (8);

        auto row = s.removeFromTop (28);
        reloadProfilesBtn.setBounds (row.removeFromLeft (170));
        row.removeFromLeft (8);
        resetSizeButton.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (8);
        tempoModeButton.setBounds (row.removeFromLeft (140));
        return;
    }

    auto planRow = r.removeFromTop (28);

    if (screen == Screen::Edit)
    {
        edDoneButton.setBounds (planRow.removeFromLeft (72));
        planRow.removeFromLeft (6);
        edSaveButton.setBounds (planRow.removeFromLeft (72));
        planRow.removeFromLeft (6);
        edSaveAsButton.setBounds (planRow.removeFromLeft (96));
        planRow.removeFromLeft (10);
        planLabel.setBounds (planRow);

        r.removeFromTop (10);

        auto row = r.removeFromTop (24);
        edNameLabel.setBounds (row.removeFromLeft (44));
        edName.setBounds (row.removeFromLeft (140));
        row.removeFromLeft (12);
        edBarsLabel.setBounds (row.removeFromLeft (40));
        edBars.setBounds (row.removeFromLeft (52));

        r.removeFromTop (6);
        row = r.removeFromTop (24);
        edIntensityLabel.setBounds (row.removeFromLeft (76));
        edIntensity.setBounds (row);

        r.removeFromTop (6);
        row = r.removeFromTop (24);
        edFeelLabel.setBounds (row.removeFromLeft (40));
        edFeel.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (12);
        edFillLabel.setBounds (row.removeFromLeft (34));
        edFill.setBounds (row.removeFromLeft (86));
        row.removeFromLeft (12);
        edLeadLabel.setBounds (row.removeFromLeft (38));
        edLead.setBounds (row.removeFromLeft (90));

        r.removeFromTop (6);
        row = r.removeFromTop (24);
        edChordsLabel.setBounds (row.removeFromLeft (60));
        edChords.setBounds (row);

        r.removeFromTop (6);
        row = r.removeFromTop (24);
        edPlaysLabel.setBounds (row.removeFromLeft (50));
        edDrums.setBounds  (row.removeFromLeft (74));
        edBass.setBounds   (row.removeFromLeft (66));
        edGuitar.setBounds (row.removeFromLeft (76));
        edPiano.setBounds  (row.removeFromLeft (70));

        r.removeFromTop (10);
        row = r.removeFromTop (26);
        edAddButton.setBounds (row.removeFromLeft (66));
        row.removeFromLeft (6);
        edDeleteButton.setBounds (row.removeFromLeft (70));
        row.removeFromLeft (14);
        edUpButton.setBounds (row.removeFromLeft (54));
        row.removeFromLeft (6);
        edDownButton.setBounds (row.removeFromLeft (60));

        r.removeFromTop (10);
        auto footer = r.removeFromBottom (32);
        statusLabel.setBounds (footer.removeFromTop (16));
        profilesLabel.setBounds (footer);
        r.removeFromBottom (8);

        viewport.setBounds (r);
        sectionList.setSize (r.getWidth() - 10, sectionList.getHeight());
        return;
    }

    if (screen == Screen::Calibrate)
    {
        calDoneButton.setBounds (planRow.removeFromLeft (72));
        planRow.removeFromLeft (6);
        calSaveButton.setBounds (planRow.removeFromLeft (86));
        planRow.removeFromLeft (10);
        planLabel.setBounds (planRow);

        r.removeFromTop (10);
        calHintLabel.setBounds (r.removeFromTop (18));
        r.removeFromTop (10);

        auto nudge = r.removeFromTop (40);
        calLowerButton.setBounds (nudge.removeFromLeft (52));
        nudge.removeFromLeft (8);
        calNoteLabel.setBounds (nudge.removeFromLeft (120));
        nudge.removeFromLeft (8);
        calHigherButton.setBounds (nudge.removeFromLeft (52));
        nudge.removeFromLeft (16);
        calPlayButton.setBounds (nudge.removeFromLeft (90));

        r.removeFromTop (12);
        auto footer = r.removeFromBottom (32);
        statusLabel.setBounds (footer.removeFromTop (16));
        profilesLabel.setBounds (footer);
        r.removeFromBottom (8);

        calViewport.setBounds (r);
        calList.setSize (r.getWidth() - 10, calList.getHeight());
        return;
    }

    calibrateButton.setBounds (planRow.removeFromRight (86));
    planRow.removeFromRight (6);
    editButton.setBounds (planRow.removeFromRight (86));
    planRow.removeFromRight (10);
    loadButton.setBounds (planRow.removeFromLeft (104));
    planRow.removeFromLeft (6);
    reloadButton.setBounds (planRow.removeFromLeft (72));
    planRow.removeFromLeft (10);
    planLabel.setBounds (planRow);

    r.removeFromTop (6);
    auto infoRow = r.removeFromTop (18);
    headlineLabel.setBounds (infoRow.removeFromLeft (infoRow.getWidth() / 2));
    summaryLabel.setBounds (infoRow);

    r.removeFromTop (12);

    auto songRow = r.removeFromTop (26);
    keyLabel.setBounds (songRow.removeFromLeft (32));
    keyBox.setBounds (songRow.removeFromLeft (84));
    songRow.removeFromLeft (12);
    modeLabel.setBounds (songRow.removeFromLeft (42));
    modeBox.setBounds (songRow.removeFromLeft (140));

    r.removeFromTop (7);
    songRow = r.removeFromTop (26);
    styleLabel.setBounds (songRow.removeFromLeft (44));
    styleBox.setBounds (songRow.removeFromLeft (132));
    songRow.removeFromLeft (12);
    tuningLabel.setBounds (songRow.removeFromLeft (80));
    tuningBox.setBounds (songRow.removeFromLeft (106));
    tempoLabel.setBounds (songRow);

    r.removeFromTop (12);

    auto knobRow = r.removeFromTop (76);

    auto k1 = knobRow.removeFromLeft (80);
    complexityLabel.setBounds (k1.removeFromTop (12));
    complexitySlider.setBounds (k1);

    knobRow.removeFromLeft (8);
    auto k2 = knobRow.removeFromLeft (80);
    humanizeLabel.setBounds (k2.removeFromTop (12));
    humanizeSlider.setBounds (k2);

    // Seed and Roll sit beside the knobs rather than under them, so the section
    // list keeps as much of the window as possible.
    knobRow.removeFromLeft (18);
    auto seedCol = knobRow.removeFromTop (58);
    seedLabel.setBounds (seedCol.removeFromTop (12));
    auto seedRow = seedCol.removeFromTop (26);
    seedEditor.setBounds (seedRow.removeFromLeft (86));
    seedRow.removeFromLeft (8);
    // Fixed width: letting it take the remaining space made it span half the
    // window, which read as the most important control on the panel.
    rollButton.setBounds (seedRow.removeFromLeft (juce::jmin (100, seedRow.getWidth())));
    seedRow.removeFromLeft (6);
    playPauseButton.setBounds (seedRow.removeFromLeft (juce::jmin (74, seedRow.getWidth())));

    seedRow.removeFromLeft (16);
    bpmLabel.setBounds (seedRow.removeFromLeft (juce::jmin (30, seedRow.getWidth())));
    bpmEditor.setBounds (seedRow.removeFromLeft (juce::jmin (52, seedRow.getWidth())));

    // Mix row: four small level knobs, one per part.
    r.removeFromTop (8);
    auto mixRow = r.removeFromTop (56);
    mixLabel.setBounds (mixRow.removeFromLeft (34).withTrimmedTop (16));

    juce::Slider* levelSliders[5] = { &levelDrums, &levelBass, &levelGuitar,
                                      &levelGuitar2, &levelPiano };
    juce::Label*  levelLabels[5]  = { &levelDrumsLabel, &levelBassLabel,
                                      &levelGuitarLabel, &levelGuitar2Label,
                                      &levelPianoLabel };

    // A knob is laid out only for a part whose volume can actually be reached.
    // SSD5 has no volume anything outside it can address, so a drum mix knob is
    // a knob that does nothing - and this project keeps rediscovering that a
    // control which does nothing is worse than no control at all. The knob
    // returns on its own if a kit that can be reached is loaded.
    //
    // Part order here is drums, bass, guitar, guitar 2, piano; the processor
    // indexes guitar 2 as 4, so the lookup is not the loop counter.
    static const int partForKnob[5] = { 0, 1, 2, 4, 3 };

    for (int i = 0; i < 5; ++i)
    {
        const bool reachable = processor.partVolumeReachable (partForKnob[i]);

        // Only the song screen ever positions these, so only it may show them -
        // resized() runs last after a screen change and would otherwise unhide
        // them over whatever the new screen has drawn there.
        const bool show = reachable && screen == Screen::Song;
        levelSliders[i]->setVisible (show);
        levelLabels[i]->setVisible (show);
        if (! reachable)
            continue;

        auto cell = mixRow.removeFromLeft (52);
        levelLabels[i]->setBounds (cell.removeFromTop (11));
        levelSliders[i]->setBounds (cell.reduced (3, 0));
        mixRow.removeFromLeft (3);
    }

    r.removeFromTop (8);
    {
        auto transportRow = r.removeFromTop (16);
        rollHintLabel.setBounds (transportRow.removeFromRight (juce::jmin (260, transportRow.getWidth() / 2)));
        transportLabel.setBounds (transportRow);
    }
    r.removeFromTop (6);

    auto footer = r.removeFromBottom (32);
    statusLabel.setBounds (footer.removeFromTop (16));
    profilesLabel.setBounds (footer);

    r.removeFromBottom (8);
    viewport.setBounds (r);
    sectionList.setSize (r.getWidth() - 10, sectionList.getHeight());
}
