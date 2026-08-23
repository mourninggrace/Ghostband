#include "GhostbandLookAndFeel.h"

namespace ghost {

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, bool raised,
                float corner, juce::Colour face)
{
    // A very slight vertical gradient does most of the work of reading as
    // metal; the bevel edges do the rest.
    g.setGradientFill (juce::ColourGradient (face.brighter (raised ? 0.06f : 0.0f),
                                             r.getCentreX(), r.getY(),
                                             face.darker (raised ? 0.06f : 0.10f),
                                             r.getCentreX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);

    const juce::Colour top = raised ? colours::bevelLight : colours::bevelDark;
    const juce::Colour bot = raised ? colours::bevelDark  : colours::bevelLight;

    juce::Path topEdge;
    topEdge.startNewSubPath (r.getX() + corner, r.getY() + 0.5f);
    topEdge.lineTo (r.getRight() - corner, r.getY() + 0.5f);
    g.setColour (top.withAlpha (0.9f));
    g.strokePath (topEdge, juce::PathStrokeType (1.0f));

    juce::Path bottomEdge;
    bottomEdge.startNewSubPath (r.getX() + corner, r.getBottom() - 0.5f);
    bottomEdge.lineTo (r.getRight() - corner, r.getBottom() - 0.5f);
    g.setColour (bot.withAlpha (0.9f));
    g.strokePath (bottomEdge, juce::PathStrokeType (1.0f));

    g.setColour (colours::bevelDark.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius)
{
    const juce::Rectangle<float> r (centre.x - radius, centre.y - radius,
                                    radius * 2.0f, radius * 2.0f);

    g.setColour (colours::bevelDark);
    g.fillEllipse (r.translated (0.0f, 0.5f));
    g.setColour (colours::screw);
    g.fillEllipse (r);
    g.setColour (colours::bevelLight.withAlpha (0.5f));
    g.drawEllipse (r.reduced (0.5f), 1.0f);

    // Slot, angled so a row of screws does not look stamped from one template.
    g.setColour (colours::bevelDark.withAlpha (0.8f));
    juce::Path slot;
    slot.startNewSubPath (-radius * 0.55f, 0.0f);
    slot.lineTo (radius * 0.55f, 0.0f);
    g.strokePath (slot, juce::PathStrokeType (1.2f),
                  juce::AffineTransform::rotation (0.6f).translated (centre));
}

void drawLamp (juce::Graphics& g, juce::Rectangle<float> r, bool lit, juce::Colour colour)
{
    g.setColour (colours::bevelDark);
    g.fillEllipse (r.expanded (1.0f));

    if (lit)
    {
        g.setColour (colour.withAlpha (0.20f));
        g.fillEllipse (r.expanded (r.getWidth() * 0.7f));
        g.setColour (colour);
        g.fillEllipse (r);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillEllipse (r.reduced (r.getWidth() * 0.34f).translated (0.0f, -r.getHeight() * 0.12f));
    }
    else
    {
        g.setColour (colour.withMultipliedSaturation (0.35f).withMultipliedBrightness (0.22f));
        g.fillEllipse (r);
    }
}

//==============================================================================

GhostbandLookAndFeel::GhostbandLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId,            colours::panel);
    setColour (juce::PopupMenu::textColourId,                  colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId,       colours::text);
    setColour (juce::ScrollBar::thumbColourId,                 colours::line.brighter (0.35f));
    setColour (juce::TextEditor::textColourId,                 colours::text);
    setColour (juce::TextEditor::highlightColourId,            colours::accent.withAlpha (0.30f));
    setColour (juce::Label::textColourId,                      colours::text);
    setColour (juce::CaretComponent::caretColourId,            colours::accent);
}

void GhostbandLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width,
                                             int height, float sliderPos,
                                             float startAngle, float endAngle,
                                             juce::Slider& slider)
{
    auto area = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    area = juce::Rectangle<float> (size, size).withCentre (area.getCentre());

    const float radius = size * 0.5f;
    const auto centre = area.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // Value arc, sitting outside the knob body.
    const float arcRadius = radius * 0.92f;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         startAngle, endAngle, true);
    g.setColour (colours::bevelDark);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         startAngle, angle, true);
    g.setColour (slider.isEnabled() ? colours::accent : colours::accentDim);
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Machined body: a dark disc with a brushed highlight from the top left.
    const auto body = area.reduced (size * 0.16f);
    g.setColour (colours::bevelDark);
    g.fillEllipse (body.translated (0.0f, 1.0f));

    g.setGradientFill (juce::ColourGradient (colours::panelRaised.brighter (0.16f),
                                             body.getX(), body.getY(),
                                             colours::panelRaised.darker (0.30f),
                                             body.getRight(), body.getBottom(), false));
    g.fillEllipse (body);

    g.setColour (colours::bevelLight.withAlpha (0.55f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    // Indicator.
    juce::Path pointer;
    const float pointerLength = body.getHeight() * 0.36f;
    pointer.addRoundedRectangle (-1.2f, -body.getHeight() * 0.46f, 2.4f, pointerLength, 1.2f);
    g.setColour (slider.isEnabled() ? colours::accent : colours::dim);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
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
    const auto slot = juce::Rectangle<float> (area.getX(), centreY - 3.0f,
                                              area.getWidth(), 6.0f);

    drawPanel (g, slot, false, 3.0f, colours::panel);

    g.setColour (slider.isEnabled() ? colours::accent.withAlpha (0.85f) : colours::accentDim);
    g.fillRoundedRectangle (slot.withRight (sliderPos).reduced (1.0f, 1.5f), 2.0f);

    // Machined cap.
    const auto cap = juce::Rectangle<float> (14.0f, area.getHeight() - 6.0f)
                         .withCentre ({ sliderPos, centreY });
    drawPanel (g, cap, true, 2.5f);
    g.setColour (colours::bevelDark.withAlpha (0.7f));
    g.drawLine (cap.getCentreX(), cap.getY() + 3.0f, cap.getCentreX(), cap.getBottom() - 3.0f, 1.0f);
}

void GhostbandLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                 const juce::Colour& backgroundColour,
                                                 bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);

    const bool primary = backgroundColour.getFloatAlpha() > 0.05f
                      && backgroundColour != colours::panelRaised;

    juce::Colour face = primary ? colours::accent.withAlpha (0.16f).overlaidWith (colours::panelRaised)
                                : colours::panelRaised;
    if (highlighted) face = face.brighter (0.10f);
    if (! b.isEnabled()) face = face.darker (0.25f);

    drawPanel (g, r, ! down, 3.0f, face);

    if (primary && b.isEnabled())
    {
        g.setColour (colours::accent.withAlpha (down ? 0.65f : 0.40f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }
}

void GhostbandLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                                           bool, bool down)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));

    juce::Colour c = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId
                                                      : juce::TextButton::textColourOffId);
    if (! b.isEnabled()) c = colours::dim.withAlpha (0.55f);

    g.setColour (c);
    g.drawFittedText (b.getButtonText(),
                      b.getLocalBounds().translated (0, down ? 1 : 0),
                      juce::Justification::centred, 1, 0.9f);
}

void GhostbandLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                             bool highlighted, bool)
{
    const float boxSize = 13.0f;
    auto box = juce::Rectangle<float> (boxSize, boxSize)
                   .withCentre ({ b.getHeight() * 0.5f, b.getHeight() * 0.5f });

    drawPanel (g, box, false, 2.0f, colours::panel);

    if (b.getToggleState())
    {
        g.setColour (colours::accent.withAlpha (0.22f));
        g.fillRoundedRectangle (box.reduced (1.5f), 1.5f);
        g.setColour (colours::accent);
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 3.0f, box.getCentreY());
        tick.lineTo (box.getCentreX() - 0.5f, box.getBottom() - 3.5f);
        tick.lineTo (box.getRight() - 2.5f, box.getY() + 3.0f);
        g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    g.setColour (b.isEnabled() ? (highlighted ? colours::text : colours::text.withAlpha (0.85f))
                               : colours::dim);
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    g.drawText (b.getButtonText(),
                b.getLocalBounds().withTrimmedLeft (static_cast<int> (b.getHeight())),
                juce::Justification::centredLeft);
}

void GhostbandLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height,
                                         bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f,
                                           static_cast<float> (width),
                                           static_cast<float> (height)).reduced (0.5f);
    drawPanel (g, r, false, 3.0f, colours::panel);

    juce::Path arrow;
    const float cx = r.getRight() - 12.0f;
    const float cy = r.getCentreY();
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 3.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (box.isEnabled() ? colours::dim : colours::dim.withAlpha (0.4f));
    g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void GhostbandLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (colours::panel);
    g.setColour (colours::line);
    g.drawRect (0, 0, width, height, 1);
}

juce::Font GhostbandLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::Font (juce::FontOptions (12.0f));
}

juce::Font GhostbandLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (12.5f));
}

juce::Font GhostbandLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

void GhostbandLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                                     juce::TextEditor&)
{
    drawPanel (g, juce::Rectangle<float> (0.0f, 0.0f,
                                          static_cast<float> (width),
                                          static_cast<float> (height)).reduced (0.5f),
               false, 3.0f, colours::panel);
}

void GhostbandLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                                  juce::TextEditor& editor)
{
    if (! editor.hasKeyboardFocus (true))
        return;

    g.setColour (colours::accent.withAlpha (0.55f));
    g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f,
                                                    static_cast<float> (width),
                                                    static_cast<float> (height)).reduced (0.5f),
                            3.0f, 1.0f);
}

} // namespace ghost
