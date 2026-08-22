#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ghost
{
    // One dark palette, defined once. Everything else refers back to it.
    const juce::Colour background { 0xff101216 };
    const juce::Colour panel      { 0xff181b21 };
    const juce::Colour line       { 0xff262a33 };
    const juce::Colour text       { 0xffd6dbe3 };
    const juce::Colour dim        { 0xff7c8595 };
    const juce::Colour accent     { 0xff7fe3d0 };
    const juce::Colour warn       { 0xffe8b25f };
}

// Read-only view of the arrangement, with the section currently sounding lit up.
// Sections are edited in the plan JSON, which is where a chart belongs; this is
// for seeing what you are hearing, which is most of what makes a reroll
// judgeable at all.
class SectionList : public juce::Component
{
public:
    void setSections (std::vector<gb::SectionReport> s);
    void setPlayhead (int tick);          // -1 when the transport is stopped
    void setQueued   (int index);         // -1 when nothing is waiting
    int  tickToY (int tick) const;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    std::function<void (int)> onSectionClicked;

    static constexpr int rowHeight = 36;

private:
    int rowAt (juce::Point<int> p) const;

    std::vector<gb::SectionReport> sections;
    int playheadTick = -1;
    int queuedIndex  = -1;
    int hoverIndex   = -1;
};

class GhostbandEditor : public juce::AudioProcessorEditor,
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    explicit GhostbandEditor (GhostbandProcessor&);
    ~GhostbandEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshFromProcessor();
    void markDialsDirty();
    void styleButton (juce::TextButton& b, bool primary);
    void styleSlider (juce::Slider& s);

    GhostbandProcessor& processor;

    juce::TextButton loadButton   { "Load plan..." };
    juce::TextButton reloadButton { "Reload" };
    juce::TextButton rollButton   { "Roll" };

    juce::ComboBox keyBox;
    juce::ComboBox styleBox;
    juce::ComboBox tuningBox;
    juce::Label    keyLabel;
    juce::Label    styleLabel;
    juce::Label    tuningLabel;
    juce::Label    tempoLabel;

    juce::Slider complexitySlider;
    juce::Slider humanizeSlider;
    juce::Label  complexityLabel;
    juce::Label  humanizeLabel;

    juce::TextEditor seedEditor;
    juce::Label      seedLabel;

    juce::Label planLabel;
    juce::Label headlineLabel;
    juce::Label statusLabel;
    juce::Label profilesLabel;
    juce::Label summaryLabel;
    juce::Label transportLabel;

    juce::Viewport viewport;
    SectionList    sectionList;

    std::unique_ptr<juce::FileChooser> chooser;

    // Dial moves are debounced rather than regenerating on every pixel: the
    // audio thread try-locks the sequence, and swapping it sixty times a second
    // would cost dropped blocks for no musical benefit.
    void styleCombo (juce::ComboBox& c);

    bool     dialsDirty       = false;
    juce::uint32 lastDialMove = 0;
    int      lastPlayheadTick = -2;
    int      lastQueued       = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GhostbandEditor)
};
