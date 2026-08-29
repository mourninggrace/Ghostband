#include "PluginEditor.h"

#include "ghostband/Groove.h"

#include <algorithm>

namespace {

// The engine's own vocabulary. Combo ids are 1-based indices into these, so the
// UI never has to hold a second copy of the spelling the engine expects.
const char* kStyleIds[] = { "hard_rock", "metal", "thrash", "groove_metal",
                            "doom", "sludge", "punk", "prog_metal", "alt_rock" };
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
                : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
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
    const LevelKnob levelKnobs[4] = {
        { &levelDrums,  &levelDrumsLabel,  "DRUMS",  &processor.levelDrums  },
        { &levelBass,   &levelBassLabel,   "BASS",   &processor.levelBass   },
        { &levelGuitar, &levelGuitarLabel, "GUITAR", &processor.levelGuitar },
        { &levelPiano,  &levelPianoLabel,  "PIANO",  &processor.levelPiano  },
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
        k.s->onValueChange = [target, &proc, s = k.s]
        {
            target->store (static_cast<float> (s->getValue()));
            proc.sendLevels();
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
    initLabel (levelGuitarLabel,  "GUITAR", 9.0f,  ghost::dim, juce::Justification::centred);
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
            calHintLabel.setText ("Saved. The old map was backed up alongside it.",
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
    edFeel.onChange = [this] { pushSectionEdit(); };
    edFill.onChange = [this] { pushSectionEdit(); };

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
             &resetSizeButton, &reloadProfilesBtn, &manualButton, &repoButton })
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
    const ChannelBox channelBoxes[4] = {
        { &chDrums,  &chDrumsLabel,  "DRUMS",  &processor.channelDrums  },
        { &chBass,   &chBassLabel,   "BASS",   &processor.channelBass   },
        { &chGuitar, &chGuitarLabel, "GUITAR", &processor.channelGuitar },
        { &chPiano,  &chPianoLabel,  "PIANO",  &processor.channelPiano  },
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

    juce::TextButton* testButtons[4] = { &testDrums, &testBass, &testGuitar, &testPiano };
    for (int i = 0; i < 4; ++i)
    {
        styleButton (*testButtons[i], false);
        testButtons[i]->onClick = [this, i] { processor.testPart (i); };
        addChildComponent (*testButtons[i]);
    }

    // ---- MIDI Learn helpers ----
    styleCombo (learnPart);
    addChildComponent (learnPart);
    learnPart.addItem ("guitar", 1);
    learnPart.addItem ("piano",  2);
    learnPart.setSelectedId (1, juce::dontSendNotification);

    struct Learn { juce::TextButton* b; int cc; };
    const Learn learns[3] = { { &learnDrive, 22 }, { &learnTone, 23 }, { &learnEffect, 24 } };
    for (const Learn& l : learns)
    {
        styleButton (*l.b, false);
        auto& proc = processor;
        auto* partBox = &learnPart;
        l.b->onClick = [&proc, partBox, cc = l.cc]
        {
            proc.teachControl (partBox->getSelectedId() == 2 ? 3 : 2, cc);
        };
        addChildComponent (*l.b);
    }

    initLabel (learnHeading, "MIDI LEARN", 11.0f, ghost::text, juce::Justification::centredLeft);
    initLabel (learnHelp,
               "In the instrument, right-click a knob and choose MIDI Learn, then press the "
               "matching button here. Drive is CC22, Tone CC23, Effect CC24.",
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

void GhostbandEditor::timerCallback()
{
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
             &chDrums, &chBass, &chGuitar, &chPiano,
             &chDrumsLabel, &chBassLabel, &chGuitarLabel, &chPianoLabel,
             &settingsHeading, &channelsHelp, &resetSizeButton, &reloadProfilesBtn,
             &testDrums, &testBass, &testGuitar, &testPiano,
             &learnPart, &learnDrive, &learnTone, &learnEffect,
             &learnHeading, &learnHelp })
        c->setVisible (set);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &manualButton, &repoButton })
        c->setVisible (abt);

    // Settings and About are reachable from anywhere and lead back to the song.
    settingsButton.setVisible (! set && ! abt);
    aboutButton.setVisible    (! set && ! abt);
    backButton.setVisible     (set || abt);

    // Labels keep their bounds when a screen stops laying them out, so any that
    // the current screen does not position have to be hidden or they bleed
    // through it. Settings and About both paint over the song view's area.
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &planLabel, &headlineLabel })
        c->setVisible (song || cal || edit);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &statusLabel, &profilesLabel })
        c->setVisible (! abt);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &loadButton, &reloadButton, &rollButton, &calibrateButton, &editButton,
             &complexitySlider, &humanizeSlider, &complexityLabel,
             &humanizeLabel, &seedEditor, &seedLabel, &keyBox, &styleBox,
             &tuningBox, &keyLabel, &styleLabel, &tuningLabel,
             &modeBox, &modeLabel,
             &tempoLabel, &transportLabel, &summaryLabel,
             &mixLabel, &levelDrums, &levelBass, &levelGuitar, &levelPiano,
             &levelDrumsLabel, &levelBassLabel, &levelGuitarLabel, &levelPianoLabel })
        c->setVisible (song);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &calDoneButton, &calSaveButton, &calLowerButton, &calHigherButton,
             &calPlayButton, &calHintLabel, &calNoteLabel, &calViewport })
        c->setVisible (cal);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &edDoneButton, &edAddButton, &edDeleteButton, &edUpButton, &edDownButton,
             &edSaveButton, &edSaveAsButton, &edName, &edBars, &edChords,
             &edIntensity, &edFeel, &edFill, &edDrums, &edBass, &edGuitar, &edPiano,
             &edNameLabel, &edBarsLabel, &edIntensityLabel, &edFeelLabel,
             &edFillLabel, &edChordsLabel, &edPlaysLabel })
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
    if (s.guitarProfile.isNotEmpty()) profiles += "    gtr: " + s.guitarProfile;
    if (s.pianoProfile.isNotEmpty())  profiles += "    piano: " + s.pianoProfile;
    profilesLabel.setText (profiles, juce::dontSendNotification);

    complexitySlider.setValue (processor.complexity.load(), juce::dontSendNotification);
    humanizeSlider.setValue (processor.humanize.load(), juce::dontSendNotification);
    seedEditor.setText (juce::String (processor.seed.load()), juce::dontSendNotification);

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
    g.drawText ("MIDI BRAIN   -   DRUMS   BASS   GUITAR   PIANO",
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
    auto a = area;

    g.setColour (ghost::colours::text);
    g.setFont (juce::Font (juce::FontOptions (26.0f).withStyle ("Bold")));
    g.drawText ("Ghostband", a.removeFromTop (34), juce::Justification::topLeft);

    g.setColour (ghost::colours::silver);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    // From CMake, so the plugin and the test harness cannot disagree about it.
   #ifndef GHOSTBAND_VERSION
    #define GHOSTBAND_VERSION "dev"
   #endif
    g.drawText ("Version " GHOSTBAND_VERSION "   -   by Kyle Yeroshefsky",
                a.removeFromTop (22), juce::Justification::topLeft);

    a.removeFromTop (14);

    const auto rule = a.removeFromTop (2).toFloat();
    g.setGradientFill (ghost::accentGradient (rule));
    g.fillRect (rule);

    a.removeFromTop (16);

    g.setColour (ghost::colours::text);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawFittedText (
        "Ghostband makes no sound of its own. It writes an arrangement - drums, bass, "
        "guitar and piano - and performs it through the instruments you already own, "
        "by sending them MIDI.\n\n"
        "You own the song: its key, tempo, style and the order of its sections. Ghostband "
        "fills in the playing, and every part is generated from a seed, so the same song "
        "always comes back exactly as you left it.",
        a.removeFromTop (120), juce::Justification::topLeft, 12, 1.0f);

    a.removeFromTop (10);

    g.setColour (ghost::colours::dim);
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    g.drawFittedText (
        "Free software under the GNU AGPLv3. Built with JUCE.\n"
        "Ghostband is free and always will be - the donate button is a button, not a nag, "
        "and nothing is gated behind it.",
        a.removeFromTop (54), juce::Justification::topLeft, 4, 1.0f);
}

void GhostbandEditor::resized()
{
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
        manualButton.setBounds (buttons.removeFromLeft (120));
        buttons.removeFromLeft (8);
        repoButton.setBounds (buttons.removeFromLeft (120));
        return;
    }

    if (screen == Screen::Settings)
    {
        auto s = r;
        settingsHeading.setBounds (s.removeFromTop (24));
        s.removeFromTop (10);

        juce::ComboBox* boxes[4]  = { &chDrums, &chBass, &chGuitar, &chPiano };
        juce::Label*    labels[4] = { &chDrumsLabel, &chBassLabel, &chGuitarLabel, &chPianoLabel };
        juce::TextButton* tests[4] = { &testDrums, &testBass, &testGuitar, &testPiano };
        for (int i = 0; i < 4; ++i)
        {
            auto row = s.removeFromTop (28);
            labels[i]->setBounds (row.removeFromLeft (70));
            boxes[i]->setBounds (row.removeFromLeft (78));
            row.removeFromLeft (10);
            tests[i]->setBounds (row.removeFromLeft (66));
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
        learnPart.setBounds (learnRow.removeFromLeft (92));
        learnRow.removeFromLeft (10);
        learnDrive.setBounds (learnRow.removeFromLeft (104));
        learnRow.removeFromLeft (6);
        learnTone.setBounds (learnRow.removeFromLeft (100));
        learnRow.removeFromLeft (6);
        learnEffect.setBounds (learnRow.removeFromLeft (108));

        s.removeFromTop (14);
        auto row = s.removeFromTop (28);
        reloadProfilesBtn.setBounds (row.removeFromLeft (170));
        row.removeFromLeft (8);
        resetSizeButton.setBounds (row.removeFromLeft (150));

        auto footer = r.removeFromBottom (32);
        statusLabel.setBounds (footer.removeFromTop (16));
        profilesLabel.setBounds (footer);
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
    rollButton.setBounds (seedRow.removeFromLeft (juce::jmin (130, seedRow.getWidth())));

    // Mix row: four small level knobs, one per part.
    r.removeFromTop (8);
    auto mixRow = r.removeFromTop (56);
    mixLabel.setBounds (mixRow.removeFromLeft (34).withTrimmedTop (16));

    juce::Slider* levelSliders[4] = { &levelDrums, &levelBass, &levelGuitar, &levelPiano };
    juce::Label*  levelLabels[4]  = { &levelDrumsLabel, &levelBassLabel,
                                      &levelGuitarLabel, &levelPianoLabel };
    for (int i = 0; i < 4; ++i)
    {
        auto cell = mixRow.removeFromLeft (58);
        levelLabels[i]->setBounds (cell.removeFromTop (11));
        levelSliders[i]->setBounds (cell.reduced (4, 0));
        mixRow.removeFromLeft (4);
    }

    r.removeFromTop (8);
    transportLabel.setBounds (r.removeFromTop (16));
    r.removeFromTop (6);

    auto footer = r.removeFromBottom (32);
    statusLabel.setBounds (footer.removeFromTop (16));
    profilesLabel.setBounds (footer);

    r.removeFromBottom (8);
    viewport.setBounds (r);
    sectionList.setSize (r.getWidth() - 10, sectionList.getHeight());
}
