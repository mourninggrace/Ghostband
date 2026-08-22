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

// Scrollable read-only view of the arrangement. Sections are edited in the plan
// JSON, which is where a chart belongs; this shows what the engine made of it.
class SectionList : public juce::Component
{
public:
    void setSections (std::vector<gb::SectionReport> s);
    void paint (juce::Graphics& g) override;

    static constexpr int rowHeight = 34;

private:
    std::vector<gb::SectionReport> sections;
};

class GhostbandEditor : public juce::AudioProcessorEditor,
                        private juce::ChangeListener
{
public:
    explicit GhostbandEditor (GhostbandProcessor&);
    ~GhostbandEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshFromProcessor();
    void styleButton (juce::TextButton& b, bool primary);
    void styleSlider (juce::Slider& s);

    GhostbandProcessor& processor;

    juce::TextButton loadButton     { "Load plan..." };
    juce::TextButton generateButton { "Generate" };
    juce::TextButton rollButton     { "Roll" };

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

    juce::Viewport   viewport;
    SectionList      sectionList;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GhostbandEditor)
};
