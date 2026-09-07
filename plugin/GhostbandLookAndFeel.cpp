#include "GhostbandLookAndFeel.h"

namespace ghost {

namespace {
    int activeTheme = 0;
}

void applyTheme (int index)
{
    // Ignored rather than clamped. A session naming a theme this build does not
    // have should keep the default, not silently land on whichever one happens
    // to sit at that index - the next release would then move it again.
    if (index < 0 || index >= numThemes)
        return;

    const Theme& t = kThemes[index];
    activeTheme = index;

    colours::background = juce::Colour (t.background);
    colours::card       = juce::Colour (t.card);
    colours::cardRaised = juce::Colour (t.cardRaised);
    colours::line       = juce::Colour (t.line);

    colours::text       = juce::Colour (t.text);
    colours::silver     = juce::Colour (t.silver);
    colours::dim        = juce::Colour (t.dim);

    colours::red        = juce::Colour (t.accentA);
    colours::purple     = juce::Colour (t.accentB);
    colours::accentDim  = juce::Colour (t.accentDim);
    colours::warn       = juce::Colour (t.warn);
}

int currentTheme() { return activeTheme; }

const char* themeName (int index)
{
    return (index >= 0 && index < numThemes) ? kThemes[index].name : "Ghost";
}

juce::ColourGradient accentGradient (juce::Rectangle<float> area)
{
    return juce::ColourGradient (colours::red,    area.getX(),     area.getCentreY(),
                                 colours::purple, area.getRight(), area.getCentreY(), false);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, bool raised,
                float corner, juce::Colour face)
{
    g.setColour (raised ? colours::cardRaised : face);
    g.fillRoundedRectangle (r, corner);
}

void drawLamp (juce::Graphics& g, juce::Rectangle<float> r, bool lit, juce::Colour colour)
{
    if (lit)
    {
        g.setColour (colour.withAlpha (0.22f));
        g.fillEllipse (r.expanded (r.getWidth() * 0.6f));
        g.setColour (colour);
        g.fillEllipse (r);
    }
    else
    {
        g.setColour (colours::line);
        g.fillEllipse (r);
    }
}

//==============================================================================

GhostbandLookAndFeel::GhostbandLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId,            colours::cardRaised);
    setColour (juce::PopupMenu::textColourId,                  colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::red.withAlpha (0.28f));
    setColour (juce::PopupMenu::highlightedTextColourId,       colours::text);
    setColour (juce::ScrollBar::thumbColourId,                 colours::line.brighter (0.5f));
    setColour (juce::TextEditor::textColourId,                 colours::text);
    setColour (juce::TextEditor::highlightColourId,            colours::purple.withAlpha (0.35f));
    setColour (juce::Label::textColourId,                      colours::text);
    setColour (juce::CaretComponent::caretColourId,            colours::red);
}

void GhostbandLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width,
                                             int height, float sliderPos,
                                             float startAngle, float endAngle,
                                             juce::Slider& slider)
{
    auto area = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    area = juce::Rectangle<float> (size, size).withCentre (area.getCentre());

    const auto centre = area.getCentre();
    const float radius = size * 0.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float ring = juce::jmax (2.5f, size * 0.075f);
    const float arcRadius = radius - ring * 0.5f;

    // Unlit track.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         startAngle, endAngle, true);
    g.setColour (colours::line);
    g.strokePath (track, juce::PathStrokeType (ring, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Value arc, in the signature gradient. This is the only place colour is
    // spent on a knob - there is no body, no bevel, no shadow.
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         startAngle, angle, true);
    if (slider.isEnabled())
        g.setGradientFill (accentGradient (area));
    else
        g.setColour (colours::accentDim);
    g.strokePath (value, juce::PathStrokeType (ring, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // A short indicator tick, not a pointer across the whole face.
    const float inner = arcRadius - ring * 0.9f;
    const float outer = arcRadius - ring * 0.1f;
    juce::Path tick;
    tick.startNewSubPath (0.0f, -inner);
    tick.lineTo (0.0f, -outer);
    g.setColour (slider.isEnabled() ? colours::text : colours::dim);
    g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded),
                  juce::AffineTransform::rotation (angle).translated (centre));
}

void GhostbandLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width,
                                             int height, float sliderPos,
                                             float, float,
                                             juce::Slider::SliderStyle style,
                                             juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                          0.0f, 0.0f, style, slider);
        return;
    }

    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float centreY = area.getCentreY();
    const auto slot = juce::Rectangle<float> (area.getX(), centreY - 2.0f,
                                              area.getWidth(), 4.0f);

    g.setColour (colours::line);
    g.fillRoundedRectangle (slot, 2.0f);

    const auto filled = slot.withRight (juce::jmax (slot.getX() + 4.0f, sliderPos));
    if (slider.isEnabled()) g.setGradientFill (accentGradient (slot));
    else                    g.setColour (colours::accentDim);
    g.fillRoundedRectangle (filled, 2.0f);

    const auto knob = juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ sliderPos, centreY });
    g.setColour (colours::text);
    g.fillEllipse (knob);
}

void GhostbandLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                 const juce::Colour& backgroundColour,
                                                 bool highlighted, bool down)
{
    juce::ignoreUnused (backgroundColour);

    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = 5.0f;

    // A button is either primary - filled with the gradient - or quiet, which is
    // a flat card with a hairline. Nothing in between, so the one primary action
    // on a screen is unmistakable.
    //
    // Read from an explicit property rather than sniffed from the background
    // colour: colour sniffing made every button on the panel read as primary,
    // which is the same as none of them being.
    const bool primary = static_cast<bool> (b.getProperties().getWithDefault ("primary", false));

    if (primary && b.isEnabled())
    {
        g.setGradientFill (accentGradient (r));
        g.fillRoundedRectangle (r, corner);
        if (down || highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.18f : 0.09f));
            g.fillRoundedRectangle (r, corner);
        }
        return;
    }

    g.setColour (b.isEnabled() ? (highlighted ? colours::cardRaised.brighter (0.14f)
                                              : colours::cardRaised)
                               : colours::card);
    g.fillRoundedRectangle (r, corner);

    g.setColour (down ? colours::red.withAlpha (0.55f) : colours::line);
    g.drawRoundedRectangle (r, corner, 1.0f);
}

void GhostbandLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                                           bool, bool down)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));

    const bool primary = static_cast<bool> (b.getProperties().getWithDefault ("primary", false));

    // White on the gradient. Using the accent colour here made the label the
    // same red as the fill behind it, so the button read as blank.
    juce::Colour c = primary ? juce::Colours::white : colours::silver;
    if (! b.isEnabled()) c = colours::dim.withAlpha (0.5f);

    g.setColour (c);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().translated (0, down ? 1 : 0),
                      juce::Justification::centred, 1, 0.9f);
}

void GhostbandLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                             bool highlighted, bool)
{
    // A pill, not a checkbox - it reads as on/off at a glance and suits the flat
    // look better than a tick in a box.
    const float h = juce::jmin (16.0f, b.getHeight() - 2.0f);
    const auto pill = juce::Rectangle<float> (h * 1.85f, h)
                          .withCentre ({ h * 0.95f + 1.0f, b.getHeight() * 0.5f });

    if (b.getToggleState())
    {
        g.setGradientFill (accentGradient (pill));
        g.fillRoundedRectangle (pill, h * 0.5f);
    }
    else
    {
        g.setColour (colours::line);
        g.fillRoundedRectangle (pill, h * 0.5f);
    }

    const float knobR = h * 0.36f;
    const float knobX = b.getToggleState() ? pill.getRight() - knobR - 2.5f
                                           : pill.getX() + knobR + 2.5f;
    g.setColour (b.getToggleState() ? juce::Colours::white : colours::dim);
    g.fillEllipse (juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f)
                       .withCentre ({ knobX, pill.getCentreY() }));

    g.setColour (b.isEnabled() ? (highlighted ? colours::text : colours::silver) : colours::dim);
    g.setFont (juce::Font (juce::FontOptions (15.0f)));
    g.drawText (b.getButtonText(),
                b.getLocalBounds().withTrimmedLeft (static_cast<int> (pill.getRight()) + 7),
                juce::Justification::centredLeft);
}

void GhostbandLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height,
                                         bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f,
                                           static_cast<float> (width),
                                           static_cast<float> (height)).reduced (0.5f);

    g.setColour (colours::cardRaised);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float cx = r.getRight() - 12.0f;
    const float cy = r.getCentreY();
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (box.isEnabled() ? colours::silver : colours::dim.withAlpha (0.4f));
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void GhostbandLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.setColour (colours::cardRaised);
    g.fillRoundedRectangle (0.0f, 0.0f, static_cast<float> (width),
                            static_cast<float> (height), 6.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (0.5f, 0.5f, width - 1.0f, height - 1.0f, 6.0f, 1.0f);
}

juce::Font GhostbandLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::Font (juce::FontOptions (16.0f));
}

juce::Font GhostbandLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (16.0f));
}

juce::Font GhostbandLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

void GhostbandLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                                     juce::TextEditor&)
{
    g.setColour (colours::cardRaised);
    g.fillRoundedRectangle (0.0f, 0.0f, static_cast<float> (width),
                            static_cast<float> (height), 5.0f);
}

void GhostbandLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                                  juce::TextEditor& editor)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width),
                                           static_cast<float> (height)).reduced (0.5f);
    if (editor.hasKeyboardFocus (true))
    {
        g.setGradientFill (accentGradient (r));
        g.drawRoundedRectangle (r, 5.0f, 1.4f);
    }
    else
    {
        g.setColour (colours::line);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }
}

} // namespace ghost
