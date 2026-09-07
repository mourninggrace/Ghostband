#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ghost {

// Flat and minimal, in the idiom of UJAM's newer instruments: large calm areas,
// generous spacing, hairline separators instead of bevels, and colour used to
// mean something rather than to decorate.
//
// Everything is drawn procedurally rather than from image assets, so it scales
// cleanly across monitors with different DPI and the plugin stays a single file.
// ---- THEMES ----
//
// One palette, swappable. Every colour the interface draws comes from here, so
// a theme is a table of eleven values and nothing else has to know.
//
// The colours below are VARIABLES rather than constants, and that is the whole
// trick: a hundred and eighty call sites read ghost::background and none of
// them had to change. Switching a theme rewrites the variables and repaints.
struct Theme
{
    const char* name;

    juce::uint32 background;   // the page
    juce::uint32 card;         // panels and lists sitting on it
    juce::uint32 cardRaised;   // controls sitting on a card
    juce::uint32 line;         // hairline separators

    juce::uint32 text;         // headings and names
    juce::uint32 silver;       // values
    juce::uint32 dim;          // labels and captions

    juce::uint32 accentA;      // the gradient's left end, and "active"
    juce::uint32 accentB;      // its right end
    juce::uint32 accentDim;    // the same accent, sunk into a surface
    juce::uint32 warn;         // something needs attention
};

// Eight, and each one is a real palette rather than a hue rotation. What has to
// survive in all of them: dim must stay READABLE on background, because it
// carries the section detail, every field label and the whole footer - that was
// the complaint that led to the type pass, and a theme is an easy way to undo
// it by accident.
inline const Theme kThemes[] =
{
    // The original, and the default. Near-black with a red-into-purple accent.
    { "Ghost",   0xff0a0a0c, 0xff141419, 0xff1c1c23, 0xff2a2a33,
                 0xfff2f3f5, 0xffd2d6de, 0xffa9aeba,
                 0xffe23b54, 0xff8b5cf6, 0xff5c1f2a, 0xffe2a03b },

    // Neutral grey, white accent. The one with no opinion, for anyone who finds
    // a coloured accent distracting on a stage.
    { "Ash",     0xff0e0e10, 0xff17171a, 0xff202024, 0xff30303a,
                 0xfff4f4f6, 0xffd4d4da, 0xffababb4,
                 0xffe8e8ee, 0xff9aa0aa, 0xff3a3a44, 0xffe2a03b },

    // Warm dark, orange into red. A valve amp's glow.
    { "Ember",   0xff100b08, 0xff1a1310, 0xff231a15, 0xff352a22,
                 0xfff6f0ea, 0xffdcd0c4, 0xffb3a496,
                 0xffff7a2f, 0xffe0344a, 0xff5a2a12, 0xffffc247 },

    // Blue-black, cyan into blue. Cold and clean.
    { "Cobalt",  0xff07090f, 0xff101520, 0xff18202e, 0xff26303f,
                 0xffeef3fa, 0xffc9d5e4, 0xff97a6bb,
                 0xff2fd4e8, 0xff3b82f6, 0xff123a4a, 0xffe2a03b },

    // Dark green-grey with a lime accent.
    { "Moss",    0xff090c09, 0xff121712, 0xff1a221a, 0xff28332a,
                 0xffeff4ee, 0xffcbd8c9, 0xff9aab98,
                 0xff8ede3f, 0xff2fb37a, 0xff2a4420, 0xffe2c03b },

    // Deep red-black with gold. The one that looks like an old amp head.
    { "Oxblood", 0xff0d0708, 0xff181011, 0xff211618, 0xff332224,
                 0xfff6eeee, 0xffdcc9cb, 0xffb39a9d,
                 0xffc0392b, 0xffd9a441, 0xff4a1a1c, 0xffe2a03b },

    // Blue-grey with amber. Daylight-neutral without being a light theme.
    { "Slate",   0xff0c0e12, 0xff161a20, 0xff1e242c, 0xff2d3640,
                 0xfff0f3f7, 0xffcdd5df, 0xff9ba7b5,
                 0xffffb43f, 0xff6f8ba6, 0xff4a3417, 0xffffb43f },

    // The light one. Not a tint of the dark themes - the roles invert, so text
    // is dark on a pale ground and the "raised" surface is lighter rather than
    // darker. Included because a plugin that only works in a dim room is not
    // finished, and screenshots need it.
    { "Paper",   0xfff4f2ee, 0xffffffff, 0xfffaf8f5, 0xffd8d4cc,
                 0xff1a1a1e, 0xff3d3d45, 0xff5e5e6a,
                 0xffc02a41, 0xff6d3fd4, 0xffe8c9cf, 0xffb87a12 },
};

inline constexpr int numThemes = sizeof (kThemes) / sizeof (kThemes[0]);

namespace colours
{
    // Written by applyTheme, read everywhere. Seeded with the default so the
    // interface is correct before anything calls it.
    inline juce::Colour background { kThemes[0].background };
    inline juce::Colour card       { kThemes[0].card };
    inline juce::Colour cardRaised { kThemes[0].cardRaised };
    inline juce::Colour line       { kThemes[0].line };

    inline juce::Colour text       { kThemes[0].text };
    inline juce::Colour silver     { kThemes[0].silver };
    inline juce::Colour dim        { kThemes[0].dim };

    inline juce::Colour red        { kThemes[0].accentA };
    inline juce::Colour purple     { kThemes[0].accentB };
    inline juce::Colour warn       { kThemes[0].warn };
    inline juce::Colour accentDim  { kThemes[0].accentDim };

    // References, not copies. These are the names the drawing code already
    // used, and a copy would freeze them at whatever the theme was when the
    // program started.
    inline juce::Colour& panel       = card;
    inline juce::Colour& panelRaised = cardRaised;
    inline juce::Colour& accent      = red;
}

// Switches every colour above. Out of range is ignored rather than clamped: a
// saved session naming a theme this build does not have should keep the default
// rather than silently land on whichever one happens to sit at that index.
void applyTheme (int index);

int         currentTheme();
const char* themeName (int index);

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
