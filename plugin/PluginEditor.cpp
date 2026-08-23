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

        auto counts = r.removeFromRight (104);
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

    static const char* keyNames[12] = { "C", "C#", "D", "D#", "E", "F",
                                        "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i)
        keyBox.addItem (keyNames[i], i + 1);

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

    styleSlider (complexitySlider);
    styleSlider (humanizeSlider);
    addAndMakeVisible (complexitySlider);
    addAndMakeVisible (humanizeSlider);

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

    auto initLabel = [this] (juce::Label& l, const juce::String& t, float size,
                             juce::Colour c, juce::Justification j)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (size)));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (j);
        addAndMakeVisible (l);
    };

    initLabel (keyLabel,        "KEY",        10.0f, ghost::dim,   juce::Justification::centredLeft);
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

    calibrateButton.onClick = [this] { processor.enterCalibration(); calSelected = 0; };
    calDoneButton.onClick   = [this] { processor.exitCalibration(); };
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

    processor.stateChanged.addChangeListener (this);
    refreshFromProcessor();
    updateModeVisibility();

    // Resizable, with a floor that keeps the controls from overlapping. The
    // size lives on the processor so it survives closing the window and is
    // saved with the rest of the plugin state.
    setResizable (true, true);
    setResizeLimits (460, 520, 2200, 2000);
    setSize (processor.editorWidth.load(), processor.editorHeight.load());

    startTimerHz (30);
}

GhostbandEditor::~GhostbandEditor()
{
    stopTimer();
    processor.stateChanged.removeChangeListener (this);
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
    const bool cal = processor.isCalibrating();

    // Song controls and calibration controls occupy the same space; only one
    // set is ever visible.
    // Spelled as initializer_list<Component*> because the members are all
    // different types and a bare braced list has nothing to deduce from.
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &loadButton, &reloadButton, &rollButton,
             &complexitySlider, &humanizeSlider, &complexityLabel,
             &humanizeLabel, &seedEditor, &seedLabel, &keyBox, &styleBox,
             &tuningBox, &keyLabel, &styleLabel, &tuningLabel,
             &tempoLabel, &transportLabel, &summaryLabel, &viewport })
        c->setVisible (! cal);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &calDoneButton, &calSaveButton, &calLowerButton, &calHigherButton,
             &calPlayButton, &calHintLabel, &calNoteLabel, &calViewport })
        c->setVisible (cal);

    calibrateButton.setVisible (! cal);

    if (cal)
        refreshCalibration();

    resized();
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

    sectionList.setSections (processor.getSections());
    sectionList.setSize (viewport.getWidth() > 0 ? viewport.getWidth() - 10 : 500,
                         sectionList.getHeight());

    // Drop any selection that a newly loaded plan no longer has room for.
    const int count = static_cast<int> (processor.getSections().size());
    rerollSelection.erase (std::remove_if (rerollSelection.begin(), rerollSelection.end(),
                                           [count] (int i) { return i >= count; }),
                           rerollSelection.end());
    sectionList.setSelection (rerollSelection);
    updateRollButtonText();
}

void GhostbandEditor::paint (juce::Graphics& g)
{
    g.fillAll (ghost::background);

    auto header = getLocalBounds().removeFromTop (54);
    g.setColour (ghost::panel);
    g.fillRect (header);
    g.setColour (ghost::line);
    g.drawLine (0.0f, static_cast<float> (header.getBottom()),
                static_cast<float> (getWidth()), static_cast<float> (header.getBottom()), 1.0f);

    g.setColour (ghost::accent);
    g.setFont (juce::Font (juce::FontOptions (20.0f).withStyle ("Bold")));
    g.drawText ("GHOSTBAND", header.reduced (16, 0).withTrimmedBottom (18),
                juce::Justification::centredLeft);

    g.setColour (ghost::dim);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("MIDI OUT -> YOUR INSTRUMENTS", header.reduced (16, 0).withTrimmedTop (28),
                juce::Justification::centredLeft);
}

void GhostbandEditor::resized()
{
    processor.editorWidth.store (getWidth());
    processor.editorHeight.store (getHeight());

    auto r = getLocalBounds();
    r.removeFromTop (54);
    r = r.reduced (16, 12);

    auto planRow = r.removeFromTop (28);

    if (processor.isCalibrating())
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

    auto songRow = r.removeFromTop (24);
    keyLabel.setBounds (songRow.removeFromLeft (34));
    keyBox.setBounds (songRow.removeFromLeft (62));
    songRow.removeFromLeft (14);
    styleLabel.setBounds (songRow.removeFromLeft (44));
    styleBox.setBounds (songRow.removeFromLeft (132));

    r.removeFromTop (6);
    songRow = r.removeFromTop (24);
    tuningLabel.setBounds (songRow.removeFromLeft (82));
    tuningBox.setBounds (songRow.removeFromLeft (110));
    tempoLabel.setBounds (songRow);

    r.removeFromTop (12);

    auto dialRow = r.removeFromTop (22);
    complexityLabel.setBounds (dialRow.removeFromLeft (86));
    complexitySlider.setBounds (dialRow);

    r.removeFromTop (6);
    dialRow = r.removeFromTop (22);
    humanizeLabel.setBounds (dialRow.removeFromLeft (86));
    humanizeSlider.setBounds (dialRow);

    r.removeFromTop (10);
    auto seedRow = r.removeFromTop (28);
    seedLabel.setBounds (seedRow.removeFromLeft (86));
    seedEditor.setBounds (seedRow.removeFromLeft (90));
    seedRow.removeFromLeft (10);
    rollButton.setBounds (seedRow.removeFromLeft (78));

    r.removeFromTop (10);
    transportLabel.setBounds (r.removeFromTop (16));
    r.removeFromTop (6);

    auto footer = r.removeFromBottom (32);
    statusLabel.setBounds (footer.removeFromTop (16));
    profilesLabel.setBounds (footer);

    r.removeFromBottom (8);
    viewport.setBounds (r);
    sectionList.setSize (r.getWidth() - 10, sectionList.getHeight());
}
