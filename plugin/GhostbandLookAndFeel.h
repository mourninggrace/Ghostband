#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ghost {

// Dark industrial rack gear. Everything here is drawn procedurally rather than
// from image assets: it scales cleanly across monitors with different DPI, and
// it keeps the plugin a single file with nothing to install beside it.
namespace colours
{
    const juce::Colour background   { 0xff0e0f11 };   // the space behind everything
    const juce::Colour panel        { 0xff16181c };   // recessed surfaces
    const juce::Colour panelRaised  { 0xff1d2025 };   // faceplate
    const juce::Colour bevelLight   { 0xff2c313a };   // top-left edge
    const juce::Colour bevelDark    { 0xff07080a };   // bottom-right edge
    const juce::Colour line         { 0xff262a33 };
    const juce::Colour text         { 0xffc8ced6 };
    const juce::Colour dim          { 0xff6e7681 };
    const juce::Colour accent       { 0xff6fd6c4 };   // equipment LED
    const juce::Colour accentDim    { 0xff2f5a55 };
    const juce::Colour warn         { 0xffe8a33d };
    const juce::Colour screw        { 0xff23262c };
}

// A raised faceplate with a light top-left edge and a dark bottom-right one.
// Inverted for anything recessed - slots, wells, displays.
void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, bool raised,
                float corner = 3.0f, juce::Colour face = colours::panelRaised);

// Rack-panel screws. Purely decorative, and deliberately low contrast so they
// read as texture rather than as controls someone might try to click.
void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius);

// A small indicator lamp. Lit lamps get a soft halo; unlit ones stay a dark
// recessed lens so the panel does not look broken when nothing is happening.
void drawLamp (juce::Graphics& g, juce::Rectangle<float> r, bool lit,
               juce::Colour colour = colours::accent);

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
