#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ghost {

// Flat and minimal, in the idiom of UJAM's newer instruments: large calm areas,
// generous spacing, hairline separators instead of bevels, and colour used to
// mean something rather than to decorate.
//
// Everything is drawn procedurally rather than from image assets, so it scales
// cleanly across monitors with different DPI and the plugin stays a single file.
namespace colours
{
    const juce::Colour background   { 0xff0a0a0c };   // near black
    const juce::Colour card         { 0xff141419 };   // panel / list surface
    const juce::Colour cardRaised   { 0xff1c1c23 };   // controls sitting on a card
    const juce::Colour line         { 0xff2a2a33 };   // hairline separators

    const juce::Colour text         { 0xfff2f3f5 };   // white
    const juce::Colour silver       { 0xffc4c9d2 };   // secondary text
    const juce::Colour dim          { 0xff8a8f99 };   // labels

    const juce::Colour red          { 0xffe23b54 };   // primary accent
    const juce::Colour purple       { 0xff8b5cf6 };   // secondary accent
    const juce::Colour warn         { 0xffe2a03b };

    // Kept so existing call sites read the same.
    const juce::Colour panel        = card;
    const juce::Colour panelRaised  = cardRaised;
    const juce::Colour accent       = red;
    const juce::Colour accentDim    { 0xff5c1f2a };
}

// The signature gradient: red into purple, left to right or around an arc. Used
// for anything active, and nowhere else, so "lit up" always means the same thing.
juce::ColourGradient accentGradient (juce::Rectangle<float> area);

// A flat surface with an optional hairline. No bevels - the whole point of this
// look is that depth is implied by spacing and contrast rather than drawn.
void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, bool raised,
                float corner = 6.0f, juce::Colour face = colours::card);

// A small indicator dot.
void drawLamp (juce::Graphics& g, juce::Rectangle<float> r, bool lit,
               juce::Colour colour = colours::red);

class GhostbandLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GhostbandLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawTextEditorOutline (juce::Graphics&, int width, int height,
                                juce::TextEditor&) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height,
                                   juce::TextEditor&) override;
};

} // namespace ghost
