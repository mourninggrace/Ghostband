#include "PluginEditor.h"

//==============================================================================

void SectionList::setSections (std::vector<gb::SectionReport> s)
{
    sections = std::move (s);
    setSize (getWidth(), juce::jmax (1, static_cast<int> (sections.size()) * rowHeight));
    repaint();
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
        auto row = juce::Rectangle<int> (0, static_cast<int> (i) * rowHeight, getWidth(), rowHeight);

        if (i % 2 == 1)
            g.setColour (ghost::background.withAlpha (0.35f)), g.fillRect (row);

        g.setColour (ghost::line);
        g.drawLine (0.0f, static_cast<float> (row.getBottom()),
                    static_cast<float> (getWidth()), static_cast<float> (row.getBottom()), 1.0f);

        auto r = row.reduced (10, 0);

        // Intensity as a filled bar behind the row: the single biggest lever on
        // how a section behaves, so it should be visible at a glance.
        auto meter = r.removeFromRight (54).reduced (0, 12);
        g.setColour (ghost::line);
        g.fillRoundedRectangle (meter.toFloat(), 2.0f);
        g.setColour (ghost::accent.withAlpha (0.75f));
        g.fillRoundedRectangle (meter.withWidth (juce::roundToInt (meter.getWidth() * s.intensity))
                                     .toFloat(), 2.0f);

        auto counts = r.removeFromRight (108);
        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (juce::String (s.drumHits) + " / " + juce::String (s.bassNotes),
                    counts, juce::Justification::centredRight);

        auto nameArea = r.removeFromTop (rowHeight / 2).withTrimmedTop (4);
        g.setColour (ghost::text);
        g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
        g.drawText (juce::String (s.name), nameArea, juce::Justification::centredLeft);

        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (juce::String (s.bars) + " bars   "
                        + juce::String (s.feel).replace ("_", " ") + "   "
                        + juce::String (s.chords),
                    r.withTrimmedBottom (4), juce::Justification::centredLeft);
    }
}

//==============================================================================

GhostbandEditor::GhostbandEditor (GhostbandProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    styleButton (loadButton, false);
    styleButton (generateButton, true);
    styleButton (rollButton, false);

    addAndMakeVisible (loadButton);
    addAndMakeVisible (generateButton);
    addAndMakeVisible (rollButton);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (
            "Open a Ghostband plan",
            processor.getPlanFile().existsAsFile() ? processor.getPlanFile().getParentDirectory()
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

    generateButton.onClick = [this] { processor.regenerate(); };

    rollButton.onClick = [this]
    {
        // A new seed is a new take of the same song, which is exactly what you
        // want when a section is nearly right.
        const int next = 1 + juce::Random::getSystemRandom().nextInt (999998);
        processor.seed.store (next);
        seedEditor.setText (juce::String (next), juce::dontSendNotification);
        processor.regenerate();
    };

    styleSlider (complexitySlider);
    styleSlider (humanizeSlider);
    addAndMakeVisible (complexitySlider);
    addAndMakeVisible (humanizeSlider);

    complexitySlider.onValueChange = [this]
    { processor.complexity.store (complexitySlider.getValue()); };
    humanizeSlider.onValueChange = [this]
    { processor.humanize.store (humanizeSlider.getValue()); };

    auto initLabel = [this] (juce::Label& l, const juce::String& t, float size,
                             juce::Colour c, juce::Justification j)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (size)));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (j);
        addAndMakeVisible (l);
    };

    initLabel (complexityLabel, "COMPLEXITY", 10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (humanizeLabel,   "HUMANIZE",   10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (seedLabel,       "SEED",       10.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (planLabel,       "no plan loaded", 13.0f, ghost::text,  juce::Justification::centredLeft);
    initLabel (headlineLabel,   "",           11.0f, ghost::accent, juce::Justification::centredLeft);
    initLabel (summaryLabel,    "",           11.0f, ghost::dim,   juce::Justification::centredRight);
    initLabel (statusLabel,     "Load a plan to begin.", 11.0f, ghost::dim, juce::Justification::centredLeft);
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

    viewport.setViewedComponent (&sectionList, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setColour (juce::ScrollBar::thumbColourId, ghost::line.brighter (0.4f));
    addAndMakeVisible (viewport);

    processor.stateChanged.addChangeListener (this);
    refreshFromProcessor();

    setSize (520, 600);
}

GhostbandEditor::~GhostbandEditor()
{
    processor.stateChanged.removeChangeListener (this);
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
                                  + juce::String (mins) + ":" + juce::String (secs).paddedLeft ('0', 2)
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
                           s.unverifiedProfiles ? ghost::warn
                                                : (s.ok ? ghost::dim : ghost::warn));

    profilesLabel.setText ("drums: " + s.drumProfile + "      bass: " + s.bassProfile,
                           juce::dontSendNotification);

    complexitySlider.setValue (processor.complexity.load(), juce::dontSendNotification);
    humanizeSlider.setValue (processor.humanize.load(), juce::dontSendNotification);
    seedEditor.setText (juce::String (processor.seed.load()), juce::dontSendNotification);

    sectionList.setSections (processor.getSections());
    sectionList.setSize (viewport.getWidth() > 0 ? viewport.getWidth() - 10 : 480,
                         sectionList.getHeight());
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
    auto r = getLocalBounds();
    r.removeFromTop (54);
    r = r.reduced (16, 12);

    auto planRow = r.removeFromTop (28);
    loadButton.setBounds (planRow.removeFromLeft (110));
    planRow.removeFromLeft (10);
    planLabel.setBounds (planRow);

    r.removeFromTop (6);
    auto infoRow = r.removeFromTop (18);
    headlineLabel.setBounds (infoRow.removeFromLeft (infoRow.getWidth() / 2));
    summaryLabel.setBounds (infoRow);

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
    seedRow.removeFromLeft (8);
    rollButton.setBounds (seedRow.removeFromLeft (64));
    seedRow.removeFromLeft (8);
    generateButton.setBounds (seedRow.removeFromRight (110));

    r.removeFromTop (12);

    auto footer = r.removeFromBottom (34);
    statusLabel.setBounds (footer.removeFromTop (16));
    profilesLabel.setBounds (footer);

    r.removeFromBottom (8);
    viewport.setBounds (r);
    sectionList.setSize (r.getWidth() - 10, sectionList.getHeight());
}
