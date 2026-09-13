#include "PluginEditor.h"

#include "ghostband/Groove.h"

#include <algorithm>

namespace {

// The engine's own vocabulary. Combo ids are 1-based indices into these, so the
// UI never has to hold a second copy of the spelling the engine expects.
const char* kStyleIds[] = { "hard_rock", "metal", "thrash", "groove_metal",
                            "doom", "sludge", "punk", "prog_metal",
                            "alt_rock", "emo", "ballad", "blues" };
const char* kTuningIds[] = { "standard", "drop_d", "drop_c", "b_standard" };

// The standing half of the MIDI Learn help. refreshControls puts a live line
// above it saying what the selected part's mix knob currently reaches, which is
// the half that changes.
const char* kLearnHelp =
    "Add a control, name it, choose what it should follow. Knob sweeps, switch "
    "is on or off, select holds one of a fixed set of choices. Then put the "
    "control into MIDI Learn in the instrument and press Teach.";

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
    GB_WORK ("paint sections");
    ghost::drawSurface (g, getLocalBounds().toFloat(), 10.0f);

    if (sections.empty())
    {
        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (17.0f)));
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

            g.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Bold")));
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

        // Drums / bass / everything chordal.
        //
        // This used to show drums and bass only, so an intro carried by a lone
        // guitar read as "0 / 0" - indistinguishable from an empty section, and
        // it stayed 0 / 0 through every reroll. The section WAS being rerolled;
        // the two numbers on screen were simply blind to the only part playing
        // in it, which made a working feature look dead.
        r.removeFromRight (10);                    // keep the counts off the meter
        auto counts = r.removeFromRight (112);
        const int chordal = s.guitarChords + s.guitar2Chords + s.pianoChords;
        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawText (juce::String (s.drumHits) + " / " + juce::String (s.bassNotes)
                        + " / " + juce::String (chordal),
                    counts, juce::Justification::centredRight);

        auto nameArea = r.removeFromTop (rowHeight / 2).withTrimmedTop (5);
        g.setColour (active ? ghost::accent : ghost::text);
        g.setFont (juce::Font (juce::FontOptions (17.0f).withStyle ("Bold")));
        g.drawText (juce::String (s.name), nameArea, juce::Justification::centredLeft);

        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
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
    GB_WORK ("paint calibration");
    ghost::drawSurface (g, getLocalBounds().toFloat(), 10.0f);

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
        g.setFont (juce::Font (juce::FontOptions (16.0f)));
        g.drawText (rows[i].label, inner.removeFromLeft (inner.getWidth() - 60),
                    juce::Justification::centredLeft);

        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawText ("note " + juce::String (rows[i].note), inner,
                    juce::Justification::centredRight);
    }
}

//==============================================================================

void ControlList::setRows (std::vector<Row> r)
{
    rows = std::move (r);
    setSize (getWidth(), juce::jmax (1, static_cast<int> (rows.size()) * rowHeight));
    repaint();
}

void ControlList::setSelected (int index)
{
    if (index == selected) return;
    selected = index;
    repaint();
}

void ControlList::mouseDown (const juce::MouseEvent& e)
{
    const int row = e.getPosition().y / rowHeight;
    if (row >= 0 && row < static_cast<int> (rows.size()) && onRowClicked)
        onRowClicked (row);
}

void ControlList::paint (juce::Graphics& g)
{
    GB_WORK ("paint controls");
    ghost::drawSurface (g, getLocalBounds().toFloat(), 10.0f);

    if (rows.empty())
    {
        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (16.0f)));
        g.drawText (emptyMessage, getLocalBounds().reduced (12),
                    juce::Justification::centredTop);
        return;
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto r = juce::Rectangle<int> (0, static_cast<int> (i) * rowHeight,
                                             getWidth(), rowHeight);
        const bool active = static_cast<int> (i) == selected;

        if (active)
        {
            g.setColour (ghost::accent.withAlpha (0.16f));
            g.fillRect (r);
            g.setColour (ghost::accent);
            g.fillRect (r.withWidth (3));
        }

        auto inner = r.reduced (12, 0);

        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawText ("CC " + juce::String (rows[i].cc), inner.removeFromLeft (52),
                    juce::Justification::centredLeft);

        juce::String kind;
        if      (rows[i].type == "switch") kind = "  (switch)";
        else if (rows[i].type == "select") kind = "  (" + juce::String (rows[i].positions)
                                                + "-way)";

        g.drawText (rows[i].follows + kind,
                    inner.removeFromRight (150), juce::Justification::centredRight);

        g.setColour (active ? ghost::accent : ghost::text);
        g.setFont (juce::Font (juce::FontOptions (16.0f)));
        g.drawText (rows[i].name, inner, juce::Justification::centredLeft);
    }
}

//==============================================================================

void TakeList::setRows (std::vector<Row> r)
{
    rows = std::move (r);
    setSize (getWidth(), juce::jmax (1, static_cast<int> (rows.size()) * rowHeight));
    repaint();
}

void TakeList::setSelected (int index)
{
    if (index == selected) return;
    selected = index;
    repaint();
}

void TakeList::mouseDown (const juce::MouseEvent& e)
{
    const int row = e.getPosition().y / rowHeight;
    if (row >= 0 && row < static_cast<int> (rows.size()) && onRowClicked)
        onRowClicked (row);
}

void TakeList::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int row = e.getPosition().y / rowHeight;
    if (row >= 0 && row < static_cast<int> (rows.size()) && onRowDoubleClicked)
        onRowDoubleClicked (row);
}

void TakeList::paint (juce::Graphics& g)
{
    GB_WORK ("paint takes");
    ghost::drawSurface (g, getLocalBounds().toFloat(), 10.0f);

    if (rows.empty())
    {
        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (16.0f)));
        g.drawFittedText ("No takes saved yet.\n\n"
                          "When a roll gives you something worth keeping, name it "
                          "above and press Save take.",
                          getLocalBounds().reduced (16, 20),
                          juce::Justification::centredTop, 4);
        return;
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto r = juce::Rectangle<int> (0, static_cast<int> (i) * rowHeight,
                                             getWidth(), rowHeight);
        const bool active = static_cast<int> (i) == selected;

        if (active)
        {
            g.setColour (ghost::accent.withAlpha (0.16f));
            g.fillRect (r);
            g.setColour (ghost::accent);
            g.fillRect (r.withWidth (3));
        }

        auto inner = r.reduced (12, 6);
        auto top   = inner.removeFromTop (20);

        // The song sits on the right of the name rather than under it: two
        // takes of one song differ in their numbers, and the numbers are the
        // line worth reading straight down.
        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawText (rows[i].song, top.removeFromRight (juce::jmin (220, top.getWidth() / 2)),
                    juce::Justification::centredRight, true);

        g.setColour (active ? ghost::accent : ghost::text);
        g.setFont (juce::Font (juce::FontOptions (16.0f)));
        g.drawText (rows[i].name, top, juce::Justification::centredLeft, true);

        g.setColour (ghost::dim);
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawText (rows[i].detail, inner, juce::Justification::centredLeft, true);
    }
}

//==============================================================================
// The arrangement, drafted.

void ArrangementView::setSections (std::vector<gb::SectionReport> s)
{
    sections = std::move (s);
    repaint();
}

void ArrangementView::setPlayhead (int tick)
{
    if (tick == playheadTick) return;
    playheadTick = tick;
    repaint();
}

void ArrangementView::setQueued (int index)
{
    if (index == queuedIndex) return;
    queuedIndex = index;
    repaint();
}

void ArrangementView::setSelection (const std::vector<int>& indices)
{
    if (indices == selection) return;
    selection = indices;
    repaint();
}

int ArrangementView::laneH() const
{
    const int spare = getHeight() - 24 - curveHeight - nameHeight - 18;
    return juce::jlimit (laneHeight, 46, spare / numLanes);
}

int ArrangementView::totalBars() const
{
    int n = 0;
    for (const gb::SectionReport& s : sections) n += juce::jmax (1, s.bars);
    return juce::jmax (1, n);
}

// A section is as wide as it is LONG. A four bar intro next to an eight bar
// chorus has to look half its size, or the picture lies about the song.
juce::Rectangle<int> ArrangementView::columnFor (size_t index) const
{
    if (index >= sections.size()) return {};

    auto area = getLocalBounds().reduced (14, 12);
    area.removeFromLeft (gutter);          // the lane labels live here
    const int total = totalBars();

    int barsBefore = 0;
    for (size_t i = 0; i < index; ++i) barsBefore += juce::jmax (1, sections[i].bars);

    const int x0 = area.getX() + juce::roundToInt (area.getWidth() * (barsBefore / double (total)));
    const int x1 = area.getX() + juce::roundToInt (area.getWidth()
                        * ((barsBefore + juce::jmax (1, sections[index].bars)) / double (total)));

    return { x0, area.getY() + curveHeight, juce::jmax (2, x1 - x0),
             nameHeight + numLanes * laneH() };
}

int ArrangementView::sectionAt (juce::Point<int> p) const
{
    for (size_t i = 0; i < sections.size(); ++i)
        if (columnFor (i).contains (p)) return static_cast<int> (i);
    return -1;
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    const int i = sectionAt (e.getPosition());
    if (i < 0) return;

    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        if (onSectionToggled) onSectionToggled (i);
    }
    else if (onSectionClicked)
    {
        onSectionClicked (i);
    }
}

void ArrangementView::mouseMove (const juce::MouseEvent& e)
{
    const int i = sectionAt (e.getPosition());
    if (i == hoverIndex) return;
    hoverIndex = i;
    repaint();
}

void ArrangementView::mouseExit (const juce::MouseEvent&)
{
    if (hoverIndex < 0) return;
    hoverIndex = -1;
    repaint();
}

void ArrangementView::paint (juce::Graphics& g)
{
    GB_WORK ("paint arrangement");
    const auto full = getLocalBounds().toFloat();

    // ---- the page ----
    g.setColour (ghost::colours::card);
    g.fillRoundedRectangle (full, 3.0f);

    // Graph paper. Faint enough to read as texture rather than as content -
    // it is there to say "drawn", not to be counted.
    {
        const juce::Graphics::ScopedSaveState saved (g);
        juce::Path clip;
        clip.addRoundedRectangle (full, 3.0f);
        g.reduceClipRegion (clip);

        g.setColour (ghost::colours::line.withAlpha (0.30f));
        for (float x = full.getX(); x < full.getRight(); x += 16.0f)
            g.fillRect (x, full.getY(), 0.5f, full.getHeight());
        for (float y = full.getY(); y < full.getBottom(); y += 16.0f)
            g.fillRect (full.getX(), y, full.getWidth(), 0.5f);
    }

    g.setColour (ghost::colours::line);
    g.drawRoundedRectangle (full.reduced (0.5f), 3.0f, 1.0f);

    if (sections.empty())
        return;

    const auto area  = getLocalBounds().reduced (14, 12);
    const auto ink   = ghost::colours::text;
    const auto faint = ghost::colours::dim;

    const juce::Font mono (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              10.0f, juce::Font::plain));

    // ---- intensity, as a drawn curve across the whole song ----
    {
        const float top    = (float) area.getY() + 12.0f;
        const float bottom = (float) area.getY() + curveHeight - 8.0f;

        juce::Path curve;
        bool started = false;
        for (size_t i = 0; i < sections.size(); ++i)
        {
            const auto col = columnFor (i);
            const float y = juce::jmap ((float) juce::jlimit (0.0, 1.0, sections[i].intensity),
                                        bottom, top);
            if (! started) { curve.startNewSubPath ((float) col.getX(), y); started = true; }
            else             curve.lineTo ((float) col.getX(), y);
            curve.lineTo ((float) col.getRight(), y);
        }

        g.setColour (ghost::colours::red);
        g.strokePath (curve, juce::PathStrokeType (1.4f));

        g.setColour (faint);
        g.setFont (mono);
        g.drawText ("INT", area.getX(), area.getY(), gutter - 8, 10,
                    juce::Justification::centredRight);
    }

    // ---- the lanes ----
    struct Lane { const char* label; };
    const Lane lanes[numLanes] = { { "DRUMS" }, { "BASS" }, { "GTR" }, { "GTR 2" }, { "PIANO" } };

    // Busiest cell anywhere sets the scale, per lane. An absolute scale would
    // make the piano look silent next to the drums simply because a drummer
    // hits more things than a pianist does.
    int peak[numLanes] = { 1, 1, 1, 1, 1 };
    for (const gb::SectionReport& s : sections)
    {
        // Chords PLUS lead notes. A part playing fills or a solo writes a lead
        // line and no chords at all, so counting chords alone drew the second
        // guitar as silent through the sections it is loudest in.
        const int v[numLanes] = { s.drumHits, s.bassNotes,
                                  s.guitarChords  + s.guitarNotes,
                                  s.guitar2Chords + s.guitar2Notes,
                                  s.pianoChords };
        for (int l = 0; l < numLanes; ++l) peak[l] = juce::jmax (peak[l], v[l]);
    }

    for (size_t i = 0; i < sections.size(); ++i)
    {
        const gb::SectionReport& s = sections[i];
        const auto col = columnFor (i);

        const bool selected = std::find (selection.begin(), selection.end(),
                                         static_cast<int> (i)) != selection.end();
        const bool queued   = static_cast<int> (i) == queuedIndex;
        const bool hovered  = static_cast<int> (i) == hoverIndex;

        // Section name box. Choruses and solos are inked darker so the shape of
        // the song reads before any word does.
        auto nameBox = col.withHeight (nameHeight);

        g.setColour (selected ? ghost::colours::red.withAlpha (0.16f)
                              : ghost::colours::cardRaised
                                    .darker (float (s.intensity) * 0.14f));
        g.fillRect (nameBox);

        g.setColour (hovered ? ghost::colours::red : ghost::colours::line);
        g.drawRect (nameBox, hovered ? 1.4f : 0.8f);

        g.setColour (ink);
        g.setFont (mono.withHeight (10.5f));
        g.drawText (juce::String (s.name).toUpperCase(), nameBox.reduced (5, 0),
                    juce::Justification::centredLeft, true);

        g.setColour (faint);
        g.drawText (juce::String (s.bars), nameBox.reduced (5, 0),
                    juce::Justification::centredRight, false);

        // The lanes under it.
        const int v[numLanes] = { s.drumHits, s.bassNotes,
                                  s.guitarChords  + s.guitarNotes,
                                  s.guitar2Chords + s.guitar2Notes,
                                  s.pianoChords };

        for (int l = 0; l < numLanes; ++l)
        {
            auto cell = juce::Rectangle<int> (col.getX(), nameBox.getBottom() + l * laneH(),
                                              col.getWidth(), laneH());

            g.setColour (ghost::colours::line.withAlpha (0.45f));
            g.drawRect (cell, 0.5f);

            if (v[l] <= 0)
                continue;

            // Height carries the density, so a quiet part is a thin seam and a
            // busy one nearly fills its lane. Reading down a column tells you
            // who is playing and how hard, at a glance.
            // Gamma, not a straight ratio. A chord COUNT is a poor proxy for
            // how present a part is - four long open chords and forty strummed
            // ones are both "the guitar is playing" - so a linear scale drew
            // every part but the busiest as a hairline. The curve lifts the
            // quiet end without reordering anything.
            const double ratio  = juce::jlimit (0.0, 1.0, v[l] / double (peak[l]));
            const double amount = juce::jlimit (0.16, 1.0, std::pow (ratio, 0.55));
            const int h = juce::jmax (2, juce::roundToInt ((laneH() - 8) * amount));

            auto bar = cell.reduced (2, 0).withHeight (h)
                           .withY (cell.getBottom() - 4 - h);

            g.setColour (ink.withAlpha (0.30f + 0.55f * (float) amount));
            g.fillRect (bar);
        }

        if (queued)
        {
            g.setColour (ghost::colours::red);
            g.drawRect (col, 1.6f);
            g.setFont (mono.withHeight (9.0f));
            g.drawText ("NEXT", nameBox.reduced (5, 0), juce::Justification::centred, false);
        }
    }

    // ---- lane labels, down the left, over the drawing ----
    {
        const auto first = columnFor (0);
        g.setFont (mono.withHeight (9.0f));
        for (int l = 0; l < numLanes; ++l)
        {
            auto row = juce::Rectangle<int> (area.getX(),
                                             first.getY() + nameHeight + l * laneH(),
                                             gutter - 8, laneH());
            g.setColour (faint);
            g.drawText (lanes[l].label, row, juce::Justification::centredRight, false);
        }
    }

    // ---- the dimension line, as a drawing would carry ----
    {
        const int y = area.getBottom() - 10;
        const int x0 = area.getX() + gutter, x1 = area.getRight();

        g.setColour (faint);
        g.fillRect (x0, y, x1 - x0, 1);
        g.fillRect (x0, y - 3, 1, 7);
        g.fillRect (x1 - 1, y - 3, 1, 7);

        int bars = 0;
        for (const gb::SectionReport& s : sections) bars += juce::jmax (1, s.bars);

        g.setColour (ghost::colours::card);
        const juce::String text = juce::String (bars) + " BARS";
        g.setFont (mono.withHeight (9.0f));
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 12;
        g.fillRect (juce::Rectangle<int> ((x0 + x1) / 2 - w / 2, y - 5, w, 11));

        g.setColour (faint);
        g.drawText (text, juce::Rectangle<int> (x0, y - 6, x1 - x0, 13),
                    juce::Justification::centred, false);
    }

    // ---- playhead ----
    if (playheadTick >= 0 && ! sections.empty())
    {
        const int last = sections.back().endTick;
        if (last > 0)
        {
            const auto area2 = getLocalBounds().reduced (14, 12);
            const int x = area2.getX() + gutter
                        + juce::roundToInt ((area2.getWidth() - gutter)
                              * juce::jlimit (0.0, 1.0, playheadTick / double (last)));

            g.setColour (ghost::colours::red);
            g.fillRect (x, area2.getY() + curveHeight - 6,
                        1, nameHeight + numLanes * laneH() + 8);
            g.fillEllipse ((float) x - 3.0f, (float) (area2.getY() + curveHeight - 10),
                           6.0f, 6.0f);
        }
    }
}

//==============================================================================
// The tracker: what is actually on the wire, one row per beat.

// Each part keeps its own HUE across every theme, and takes its saturation and
// brightness from the theme it is in. That is what lets one component be neon on
// a dark ground and ink on a light one without two implementations: the drums
// are always the red-pink column, whether that reads as a glowing tube or as a
// printed one.
juce::Colour TrackerView::partColour (int part) const
{
    static const float hues[5] = { 0.972f, 0.097f, 0.555f, 0.777f, 0.430f };
    const float h = hues[juce::jlimit (0, 4, part)];

    return onDarkGround() ? juce::Colour::fromHSV (h, 0.72f, 1.00f, 1.0f)
                          : juce::Colour::fromHSV (h, 0.95f, 0.52f, 1.0f);
}

bool TrackerView::onDarkGround() const
{
    return ghost::colours::card.getPerceivedBrightness() < 0.5f;
}

void TrackerView::setSections (std::vector<gb::SectionReport> s)
{
    sections = std::move (s);
    repaint();
}

void TrackerView::setPlayhead (int tick)
{
    // Only redraw when the ROW changes. The playhead moves every processed
    // block; repainting a dense grid at that rate would spend the whole CPU
    // budget drawing text nobody could read changing. At one row per bar that
    // is four times fewer repaints than before, and at one row per sixteenth
    // four times more - which is the honest cost of the finer view.
    const int row = rowTicks > 0 ? tick / rowTicks : 0;
    if (row == playheadRow && (tick < 0) == (playheadTick < 0))
        return;

    playheadTick = tick;
    playheadRow  = row;
    repaint();
}

void TrackerView::setQueued (int index)
{
    if (index == queuedIndex) return;
    queuedIndex = index;
    repaint();
}

void TrackerView::setSelection (const std::vector<int>& indices)
{
    if (indices == selection) return;
    selection = indices;
    repaint();
}

void TrackerView::setCells (std::vector<GhostbandProcessor::TrackerCell> c, int firstTick,
                            int rowTicksIn, int beat, int barTicksIn)
{
    cells     = std::move (c);
    firstRowTick = firstTick;
    rowTicks  = juce::jmax (1, rowTicksIn);
    beatTicks = juce::jmax (1, beat);
    barTicks  = juce::jmax (1, barTicksIn);
    repaint();
}

int TrackerView::visibleRows() const
{
    return juce::jmax (1, (getHeight() - headerHeight - 10) / rowHeight);
}

juce::Rectangle<int> TrackerView::ribbonFor (size_t index) const
{
    if (index >= sections.size()) return {};

    int total = 0;
    for (const gb::SectionReport& s : sections) total += juce::jmax (1, s.bars);
    total = juce::jmax (1, total);

    auto strip = getLocalBounds().reduced (12, 0).withY (8).withHeight (18);

    int before = 0;
    for (size_t i = 0; i < index; ++i) before += juce::jmax (1, sections[i].bars);

    const int x0 = strip.getX() + juce::roundToInt (strip.getWidth() * (before / double (total)));
    const int x1 = strip.getX() + juce::roundToInt (strip.getWidth()
                        * ((before + juce::jmax (1, sections[index].bars)) / double (total)));

    return { x0 + 1, strip.getY(), juce::jmax (2, x1 - x0 - 2), strip.getHeight() };
}

int TrackerView::sectionAt (juce::Point<int> p) const
{
    for (size_t i = 0; i < sections.size(); ++i)
        if (ribbonFor (i).expanded (0, 4).contains (p)) return static_cast<int> (i);
    return -1;
}

void TrackerView::mouseDown (const juce::MouseEvent& e)
{
    const int i = sectionAt (e.getPosition());
    if (i < 0) return;

    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        if (onSectionToggled) onSectionToggled (i);
    }
    else if (onSectionClicked)
    {
        onSectionClicked (i);
    }
}

void TrackerView::mouseMove (const juce::MouseEvent& e)
{
    const int i = sectionAt (e.getPosition());
    if (i == hoverIndex) return;
    hoverIndex = i;

    setTooltip (i >= 0 ? "Section \"" + juce::String (sections[(size_t) i].name)
                             + "\" - " + juce::String (sections[(size_t) i].bars)
                             + " bars. Click to jump here on the next bar line. "
                               "Ctrl-click to add it to the reroll selection."
                       : juce::String ("What Ghostband is actually sending: one row per beat, "
                                       "one column per player. The note, how hard it is played, "
                                       "and any articulation that lands on that beat."));
    repaint();
}

void TrackerView::mouseExit (const juce::MouseEvent&)
{
    if (hoverIndex < 0) return;
    hoverIndex = -1;
    repaint();
}

void TrackerView::paint (juce::Graphics& g)
{
    GB_WORK ("paint grid");
    const bool dark = onDarkGround();
    const auto full = getLocalBounds().toFloat();

    g.setColour (ghost::colours::card);
    g.fillRoundedRectangle (full, 4.0f);
    g.setColour (ghost::colours::line.withAlpha (0.7f));
    g.drawRoundedRectangle (full.reduced (0.5f), 4.0f, 1.0f);

    const juce::Font mono (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              12.0f, juce::Font::plain));
    const juce::Font small (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                               9.5f, juce::Font::plain));

    // ---- section ribbon ----
    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto box = ribbonFor (i);
        const bool playing  = playheadTick >= 0
                           && playheadTick >= sections[i].startTick
                           && playheadTick <  sections[i].endTick;
        const bool selected = std::find (selection.begin(), selection.end(),
                                         static_cast<int> (i)) != selection.end();
        const bool hovered  = static_cast<int> (i) == hoverIndex;

        const juce::Colour lit = selected ? ghost::colours::warn
                                          : (playing ? partColour (0) : ghost::colours::dim);

        g.setColour ((playing || selected) ? lit.withAlpha (dark ? 0.16f : 0.14f)
                                           : ghost::colours::cardRaised);
        g.fillRect (box);

        g.setColour ((playing || selected || hovered) ? lit
                                                      : ghost::colours::line);
        g.drawRect (box, (playing || selected) ? 1.4f : 0.7f);

        g.setColour ((playing || selected) ? lit : ghost::colours::dim);
        g.setFont (small);
        g.drawText (juce::String (sections[i].name).toUpperCase(), box.reduced (4, 0),
                    juce::Justification::centredLeft, false);
    }

    // ---- column headers ----
    const char* names[numParts] = { "DRUMS", "BASS", "GTR", "GTR 2", "PIANO" };
    const int colW = (getWidth() - 24 - barColumn) / numParts;

    g.setFont (small);
    g.setColour (ghost::colours::dim);
    g.drawText ("BAR", 12, headerHeight - 18, barColumn, 12,
                juce::Justification::centredLeft, false);

    for (int c = 0; c < numParts; ++c)
    {
        g.setColour (partColour (c));
        g.drawText (names[c], 12 + barColumn + c * colW, headerHeight - 18, colW, 12,
                    juce::Justification::centredLeft, false);
    }

    g.setColour (ghost::colours::line);
    g.fillRect (12, headerHeight - 4, getWidth() - 24, 1);

    // ---- rows ----
    const int rows = visibleRows();
    const int cur  = juce::jlimit (0, rows - 1, rows / 3);

    for (int r = 0; r < rows; ++r)
    {
        const int y = headerHeight + r * rowHeight;
        const int tick = firstRowTick + r * rowTicks;

        // Where this row sits in the bar, worked out in ticks rather than in
        // rows, so it stays right at every zoom. beatsPerBar can be five or
        // seven; nothing here assumes four.
        const int inBar     = ((tick % barTicks) + barTicks) % barTicks;
        const int beatInBar = inBar / beatTicks;
        const int subInBeat = inBar % beatTicks;
        const bool downbeat = inBar == 0;
        const bool onBeat   = subInBeat == 0;
        const bool isNow = playheadTick >= 0 && r == cur;

        if (isNow)
        {
            // The lit row. A gradient across it rather than a flat fill, which
            // is the whole of the neon idea applied to the one thing that
            // matters most: where you are.
            juce::ColourGradient lit (partColour (0).withAlpha (dark ? 0.28f : 0.16f),
                                      (float) 12, 0.0f,
                                      partColour (3).withAlpha (dark ? 0.28f : 0.16f),
                                      (float) getWidth() - 12, 0.0f, false);
            g.setGradientFill (lit);
            g.fillRect (12, y, getWidth() - 24, rowHeight);

            g.setColour (partColour (0).withAlpha (dark ? 0.85f : 0.55f));
            g.fillRect (12, y, getWidth() - 24, 1);
        }
        else if (downbeat)
        {
            g.setColour (ghost::colours::cardRaised.withAlpha (0.55f));
            g.fillRect (12, y, getWidth() - 24, rowHeight);
        }
        else if (onBeat && rowTicks < beatTicks)
        {
            // Below one row per beat the grid needs a second tier of banding,
            // or sixteen identical rows to the bar is a wall with no landmarks
            // in it and you cannot tell the downbeat from the "and" of three.
            g.setColour (ghost::colours::cardRaised.withAlpha (0.22f));
            g.fillRect (12, y, getWidth() - 24, rowHeight);
        }

        // The position, at whatever precision this zoom can actually resolve:
        // bar alone when a row is a bar, bar.beat at a beat, bar.beat.sub
        // below that. Only on landmarks, so the column stays quiet.
        g.setFont (small);
        g.setColour (isNow ? ghost::colours::text : ghost::colours::dim.withAlpha (0.75f));
        if (downbeat || isNow || (onBeat && rowTicks < beatTicks))
        {
            juce::String where (1 + tick / juce::jmax (1, barTicks));
            if (rowTicks < barTicks)
            {
                where += "." + juce::String (beatInBar + 1);
                if (rowTicks < beatTicks)
                    where += "." + juce::String (1 + subInBeat / rowTicks);
            }
            g.drawText (where, 12, y, barColumn, rowHeight,
                        juce::Justification::centredLeft, false);
        }

        for (int c = 0; c < numParts; ++c)
        {
            const size_t idx = static_cast<size_t> (r * numParts + c);
            if (idx >= cells.size()) continue;

            const auto& cell = cells[idx];
            const int x = 12 + barColumn + c * colW;

            if (cell.note < 0 && cell.cc < 0)
            {
                g.setFont (mono);
                g.setColour (ghost::colours::dim.withAlpha (0.22f));
                g.drawText ("---", x, y, colW, rowHeight,
                            juce::Justification::centredLeft, false);
                continue;
            }

            const juce::Colour hue = partColour (c);

            if (cell.note >= 0)
            {
                // Bright when it is the current row, dimmer behind. The colour
                // never changes, only how lit it is - which is what makes a
                // column read as one instrument.
                g.setColour (isNow ? hue : hue.withAlpha (dark ? 0.55f : 0.72f));
                g.setFont (mono);
                g.drawText (juce::MidiMessage::getMidiNoteName (cell.note, true, true, 3),
                            x, y, 44, rowHeight, juce::Justification::centredLeft, false);

                g.setColour ((isNow ? hue : hue.withAlpha (dark ? 0.42f : 0.55f))
                                 .withMultipliedSaturation (0.6f));
                g.setFont (small);
                g.drawText (juce::String (cell.velocity), x + 44, y, 26, rowHeight,
                            juce::Justification::centredLeft, false);

                // One row can cover more than one note - always at a bar per
                // row, sometimes at a beat. Showing the loudest and no sign of
                // the rest would read as "one hit here", which is a lie the
                // coarse zooms would tell on nearly every row.
                if (cell.hits > 1)
                {
                    g.setColour ((isNow ? hue : hue.withAlpha (dark ? 0.42f : 0.55f))
                                     .withMultipliedSaturation (0.35f));
                    g.drawText (juce::String::fromUTF8 ("\xc3\x97") + juce::String (cell.hits),
                                x + 70, y, 26, rowHeight,
                                juce::Justification::centredLeft, false);
                }
            }

            if (cell.cc >= 0)
            {
                g.setColour (ghost::colours::warn.withAlpha (isNow ? 1.0f : 0.6f));
                g.setFont (small);
                g.drawText ("cc" + juce::String (cell.cc),
                            x + 96, y, colW - 100, rowHeight,
                            juce::Justification::centredRight, false);
            }
        }
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
    styleButton (playPauseButton, false);
    addAndMakeVisible (playPauseButton);
    playPauseButton.onClick = [this]
    {
        processor.togglePaused();
        playPauseButton.setButtonText (processor.paused.load() ? "Play" : "Pause");
    };
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
                : processor.bundledPlansFolder(),
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
            // Say what happened, by name.
            //
            // A section reroll changes the hits/notes counts on the right of
            // each row and nothing else on screen - no new seed, no new
            // arrangement shape - so it was reported as "the button says Roll 2
            // sections and clicking it does nothing". The work was being done
            // and the plugin never said so. Whole-song rerolls only LOOKED like
            // they worked because the seed box changes with them.
            const auto sections = processor.getSections();

            juce::StringArray named;
            for (int i : rerollSelection)
                if (i >= 0 && i < static_cast<int> (sections.size()))
                    named.add (juce::String (sections[(size_t) i].name));

            processor.rerollSections (rerollSelection);

            rollHintLabel.setText ("rerolled " + named.joinIntoString (", ")
                                       + " - counts on the right have moved",
                                   juce::dontSendNotification);
            rollHintLabel.setColour (juce::Label::textColourId, ghost::accent);
            rollHintDirty = true;
            return;
        }

        const int next = 1 + juce::Random::getSystemRandom().nextInt (999998);
        processor.seed.store (next);
        seedEditor.setText (juce::String (next), juce::dontSendNotification);
        processor.regenerate();
    };

    sectionList.onSectionToggled = [this] (int index)
    {
        // NOTE: kept as the single place a section is toggled, so
        // ctrlClickSectionForTesting exercises the real thing rather than a
        // copy of it that can drift.
        const auto it = std::find (rerollSelection.begin(), rerollSelection.end(), index);
        if (it == rerollSelection.end()) rerollSelection.push_back (index);
        else                             rerollSelection.erase (it);

        std::sort (rerollSelection.begin(), rerollSelection.end());
        sectionList.setSelection (rerollSelection);
        arrangement.setSelection (rerollSelection);
        tracker.setSelection (rerollSelection);
        updateRollButtonText();

        if (rollHintDirty)
        {
            rollHintLabel.setText ("ctrl-click a section to reroll just that one",
                                   juce::dontSendNotification);
            rollHintLabel.setColour (juce::Label::textColourId, ghost::dim);
            rollHintDirty = false;
        }
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

    // ---- how much music one tracker row covers ----
    styleCombo (zoomBox);
    addAndMakeVisible (zoomBox);
    zoomBox.addItem ("bar",       1);
    zoomBox.addItem ("beat",      2);
    zoomBox.addItem ("8th",       3);
    zoomBox.addItem ("16th",      4);
    zoomBox.setSelectedId (1 + juce::jlimit (0, GhostbandProcessor::numTrackerZooms - 1,
                                             processor.trackerZoom.load()),
                           juce::dontSendNotification);
    zoomBox.onChange = [this]
    {
        const int id = zoomBox.getSelectedId();
        if (id <= 0) return;
        processor.trackerZoom.store (id - 1);

        // Force the next refresh through: the cache key is (tick, rows, perRow)
        // and the first two have not moved, so without this the grid would keep
        // the old resolution until the playhead happened to cross a row.
        lastTrackerTick = -1;
        refreshTracker();
    };

    // The two feel dials become machined knobs; the editor's intensity field
    // stays a slider, because it sits in a form row of text fields.
    for (juce::Slider* s : std::initializer_list<juce::Slider*> { &complexitySlider,
                                                                  &humanizeSlider,
                                                                  &fillsSlider })
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
    fillsSlider.onValueChange = [this]
    {
        processor.fills.store (fillsSlider.getValue());
        markDialsDirty();
    };

    // ---- per-part levels ----
    struct LevelKnob { juce::Slider* s; juce::Label* l; const char* name;
                       std::atomic<float>* target; };
    const LevelKnob levelKnobs[5] = {
        { &levelDrums,   &levelDrumsLabel,   "DRUMS",  &processor.levelDrums   },
        { &levelBass,    &levelBassLabel,    "BASS",   &processor.levelBass    },
        { &levelGuitar,  &levelGuitarLabel,  "GTR",    &processor.levelGuitar  },
        { &levelGuitar2, &levelGuitar2Label, "GTR 2",  &processor.levelGuitar2 },
        { &levelPiano,   &levelPianoLabel,   "PIANO",  &processor.levelPiano   },
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
        k.s->onValueChange = [this, target, &proc, s = k.s]
        {
            target->store (static_cast<float> (s->getValue()));
            proc.sendLevels();

            // Says which controller each knob actually reached, so a knob that
            // does nothing can be told apart from a knob whose message nothing
            // is listening to. "CC7(untaught)" means that part has no volume
            // control mapped, and CC 7 is a guess most instruments ignore.
            statusLabel.setText (proc.getLastMidiReport(), juce::dontSendNotification);
        };
        addAndMakeVisible (*k.s);
    }

    // `c` by reference, not by value, and that is the whole of what makes a
    // theme change reach the labels. The ghost:: names are references into the
    // live palette, so the address taken here keeps pointing at the colour
    // applyTheme overwrites. Copied by value, every label would record the
    // colour that was current when the window opened and keep it forever.
    auto initLabel = [this] (juce::Label& l, const juce::String& t, float size,
                             juce::Colour& c, juce::Justification j)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (size)));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (j);
        themedLabels.push_back ({ &l, &c });
        addAndMakeVisible (l);
    };

    initLabel (mixLabel,          "MIX",    15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (levelDrumsLabel,   "DRUMS",  14.0f,  ghost::dim, juce::Justification::centred);
    initLabel (levelBassLabel,    "BASS",   14.0f,  ghost::dim, juce::Justification::centred);
    initLabel (levelGuitarLabel,  "GTR",    14.0f,  ghost::dim, juce::Justification::centred);
    initLabel (levelGuitar2Label, "GTR 2",  14.0f,  ghost::dim, juce::Justification::centred);
    initLabel (levelPianoLabel,   "PIANO",  14.0f,  ghost::dim, juce::Justification::centred);
    initLabel (keyLabel,        "KEY",        15.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (modeLabel,       "MODE",       15.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (styleLabel,      "STYLE",      15.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (tuningLabel,     "BASS TUNING",15.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (zoomLabel,       "ROWS",       15.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (tempoLabel,      "",           15.0f, ghost::dim,   juce::Justification::centredRight);
    initLabel (complexityLabel, "COMPLEXITY", 15.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (humanizeLabel,   "HUMANIZE",   15.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (fillsLabel,      "FILLS",      15.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (seedLabel,       "SEED",       15.0f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (planLabel,       "",           17.0f, ghost::text,  juce::Justification::centredLeft);
    initLabel (headlineLabel,   "",           15.5f, ghost::accent, juce::Justification::centredLeft);
    initLabel (summaryLabel,    "",           15.5f, ghost::dim,   juce::Justification::centredRight);
    // The same words the timer will use. Set here as well, because the timer
    // only rewrites this when the playhead MOVES - so the very first thing a
    // new user sees, before anything has moved, is whatever was hard-coded
    // here, and "stopped" on its own does not tell them what to do about it.
    initLabel (transportLabel,  "stopped   -   press play in your host",
                                              15.5f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (statusLabel,     "",           15.5f, ghost::dim,   juce::Justification::centredLeft);
    initLabel (profilesLabel,   "",           15.0f, ghost::silver, juce::Justification::centredLeft);

    // Small caps captions, dim, so the eye reads them as headings rather than
    // as more of the same text. The values beside them are silver; the two
    // together are what turns a stack of grey lines into a labelled block.
    initLabel (statusCaption,   "STATUS",      14.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (profilesCaption, "INSTRUMENTS", 14.0f, ghost::dim, juce::Justification::centredLeft);

    // Deliberately not in any of updateModeVisibility's screen lists: this one
    // is on the footer rail, which every screen keeps, so it is always shown.
    initLabel (latencyLabel,    "",           15.0f, ghost::dim,   juce::Justification::centredRight);

    seedEditor.setJustification (juce::Justification::centredLeft);
    seedEditor.setInputRestrictions (7, "0123456789");
    seedEditor.setColour (juce::TextEditor::backgroundColourId, ghost::background);
    seedEditor.setColour (juce::TextEditor::outlineColourId, ghost::line);
    seedEditor.setColour (juce::TextEditor::focusedOutlineColourId, ghost::accent.withAlpha (0.6f));
    seedEditor.setColour (juce::TextEditor::textColourId, ghost::text);
    seedEditor.setFont (juce::Font (juce::FontOptions (17.0f)));
    seedEditor.onReturnKey = [this]
    {
        processor.seed.store (juce::jmax (1, seedEditor.getText().getIntValue()));
        processor.regenerate();
    };
    addAndMakeVisible (seedEditor);

    // Tempo, editable at last. Commits on Return and on losing focus, because a
    // field that only commits on Return silently discards what you typed the
    // moment you click somewhere else.
    bpmEditor.setJustification (juce::Justification::centredLeft);
    bpmEditor.setInputRestrictions (3, "0123456789");
    bpmEditor.setColour (juce::TextEditor::backgroundColourId, ghost::background);
    bpmEditor.setColour (juce::TextEditor::outlineColourId, ghost::line);
    bpmEditor.setColour (juce::TextEditor::focusedOutlineColourId, ghost::accent.withAlpha (0.6f));
    bpmEditor.setColour (juce::TextEditor::textColourId, ghost::text);
    bpmEditor.setFont (juce::Font (juce::FontOptions (17.0f)));

    const auto commitBpm = [this]
    {
        const int typed = bpmEditor.getText().getIntValue();
        if (typed <= 0)
        {
            bpmEditor.setText (juce::String (juce::roundToInt (processor.getPlanBpm())),
                               juce::dontSendNotification);
            return;
        }
        processor.setPlanBpm (typed);
    };

    bpmEditor.onReturnKey  = commitBpm;
    bpmEditor.onFocusLost  = commitBpm;
    addAndMakeVisible (bpmEditor);

    initLabel (bpmLabel, "BPM", 15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (rollHintLabel, "ctrl-click a section to reroll just that one",
               15.0f, ghost::dim, juce::Justification::centredRight);

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
        arrangement.setQueued (index);
        tracker.setQueued (index);
    };

    // The arrangement answers the same two gestures as the list it replaces on
    // the song screen, so everything downstream - reroll selection, section
    // jumping, the test hooks - is untouched by the change.
    arrangement.onSectionToggled = sectionList.onSectionToggled;
    arrangement.onSectionClicked = sectionList.onSectionClicked;
    addChildComponent (arrangement);

    tracker.onSectionToggled = sectionList.onSectionToggled;
    tracker.onSectionClicked = sectionList.onSectionClicked;
    addChildComponent (tracker);

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

    initLabel (calHintLabel, "", 16.0f, ghost::text, juce::Justification::centredLeft);
    initLabel (calNoteLabel, "", 28.0f, ghost::accent, juce::Justification::centred);

    styleButton (tempoModeButton, false);
    addChildComponent (tempoModeButton);

    const auto refreshTempoMode = [this]
    {
        const bool own = processor.usePlanTempo.load();
        tempoModeButton.setButtonText (own ? "Tempo: song" : "Tempo: host");
        statusLabel.setText (own
            ? "Songs play at their own tempo. The host's transport still starts and stops them."
            : "Songs follow the host tempo, so set it to match the song.",
            juce::dontSendNotification);
    };

    tempoModeButton.onClick = [this, refreshTempoMode]
    {
        processor.usePlanTempo.store (! processor.usePlanTempo.load());
        refreshTempoMode();
    };

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
            // Say what actually moved. The old message was a fixed string, so
            // the report naming every changed note - the whole point of it -
            // was built and then thrown away.
            calHintLabel.setText (err.isNotEmpty() ? err
                                                   : juce::String ("Saved."),
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
        t.setFont (juce::Font (juce::FontOptions (17.0f)));
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
    styleCombo (edLead);
    addChildComponent (edLead);
    for (const char* l : { "auto", "guitar", "piano", "both" })
        edLead.addItem (l, edLead.getNumItems() + 1);

    edFeel.onChange = [this] { pushSectionEdit(); };
    edFill.onChange = [this] { pushSectionEdit(); };
    edLead.onChange = [this] { pushSectionEdit(); };

    for (juce::ToggleButton* t : std::initializer_list<juce::ToggleButton*> {
             &edDrums, &edBass, &edGuitar, &edGuitar2, &edPiano })
    {
        t->setColour (juce::ToggleButton::textColourId, ghost::text);
        t->setColour (juce::ToggleButton::tickColourId, ghost::accent);
        t->setColour (juce::ToggleButton::tickDisabledColourId, ghost::line);
        t->onClick = [this] { pushSectionEdit(); };
        addChildComponent (*t);
    }

    initLabel (edNameLabel,      "NAME",      15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edBarsLabel,      "BARS",      15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edIntensityLabel, "INTENSITY", 15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edFeelLabel,      "FEEL",      15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edFillLabel,      "FILL",      15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edChordsLabel,    "CHORDS",    15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edPlaysLabel,     "PLAYS",     15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (edLeadLabel,      "LEAD",      15.0f, ghost::dim, juce::Justification::centredLeft);

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

        // No file, or a song that shipped in the bundle. A preset lives under
        // Program Files, so saving over it fails on permissions - and it should
        // not succeed anyway: the presets are the ones everybody gets, and
        // editing one is how you start a song of your own, not how you replace
        // a factory one. Both cases become Save as..., pointed at your songs.
        if (! target.existsAsFile() || processor.planIsFactory())
        {
            edSaveAsButton.triggerClick();
            return;
        }

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
        // Always your songs folder, never wherever the loaded song came from.
        // Starting from a preset used to open this dialog inside the installed
        // bundle under Program Files, which needs elevation to write and puts
        // your song where an installer has to work around it.
        //
        // The name comes from the loaded song, so saving a preset you have been
        // editing offers "preset-metal.json" in your own folder rather than
        // making you retype it.
        const juce::File current = processor.getPlanFile();
        const juce::String suggested = current.existsAsFile() ? current.getFileName()
                                                              : juce::String ("my-song.json");
        const juce::File start = GhostbandProcessor::userSongsFolder()
                                     .getChildFile (suggested);

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

    // ---- takes ----
    for (juce::TextButton* b : std::initializer_list<juce::TextButton*> {
             &takesButton, &tkDoneButton, &tkSaveButton, &tkRecall, &tkDelete })
    {
        styleButton (*b, b == &tkSaveButton);
        addAndMakeVisible (*b);
    }

    tkName.setColour (juce::TextEditor::backgroundColourId, ghost::background);
    tkName.setColour (juce::TextEditor::outlineColourId, ghost::line);
    tkName.setColour (juce::TextEditor::focusedOutlineColourId, ghost::accent.withAlpha (0.6f));
    tkName.setColour (juce::TextEditor::textColourId, ghost::text);
    tkName.setFont (juce::Font (juce::FontOptions (17.0f)));
    tkName.setTextToShowWhenEmpty ("name this take", ghost::dim.withAlpha (0.7f));

    // Return saves. Naming a take and pressing enter is the gesture, and having
    // to reach for a button after typing is the kind of small friction that
    // stops a feature from being used at the moment it would help.
    tkName.onReturnKey = [this] { saveTakeFromBox(); };
    addChildComponent (tkName);

    initLabel (tkHeading, "TAKES", 22.0f, ghost::text, juce::Justification::centredLeft);
    initLabel (tkHelp,
               "A take is one performance of the song that is loaded: its seed and its three "
               "dials, with the song itself saved alongside so it comes back exactly as you "
               "heard it. The mix and the channels are not part of a take - those are your rig.",
               15.0f, ghost::dim, juce::Justification::topLeft);
    tkHelp.setJustificationType (juce::Justification::topLeft);
    initLabel (tkNameLabel, "NAME", 15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (tkResult, "", 15.0f, ghost::accent, juce::Justification::centredLeft);

    tkViewport.setViewedComponent (&tkList, false);
    tkViewport.setScrollBarsShown (true, false);
    tkViewport.setColour (juce::ScrollBar::thumbColourId, ghost::line.brighter (0.4f));
    addChildComponent (tkViewport);

    tkList.onRowClicked = [this] (int row)
    {
        tkSelected = row;
        tkList.setSelected (row);

        // Clicking a take puts its name in the box, so Save writes over the one
        // you are looking at instead of quietly making a second copy of it.
        if (row >= 0 && row < static_cast<int> (takes.size()))
            tkName.setText (takes[static_cast<size_t> (row)].name,
                            juce::dontSendNotification);
    };

    tkList.onRowDoubleClicked = [this] (int row) { tkSelected = row; tkRecall.onClick(); };

    takesButton.onClick  = [this] { screen = Screen::Takes; tkResult.setText ({},
                                        juce::dontSendNotification);
                                    updateModeVisibility(); };
    tkDoneButton.onClick = [this] { screen = Screen::Song; updateModeVisibility(); };

    tkSaveButton.onClick = [this] { saveTakeFromBox(); };

    tkRecall.onClick = [this]
    {
        if (tkSelected < 0 || tkSelected >= static_cast<int> (takes.size()))
            return;

        const juce::String name = takes[static_cast<size_t> (tkSelected)].name;
        processor.recallTake (tkSelected);

        // Straight back to the song. Recalling a take is something you do in
        // order to listen to it, and leaving the library on screen hides the
        // thing that just changed.
        screen = Screen::Song;
        updateModeVisibility();
        statusLabel.setText ("Recalled take \"" + name + "\".",
                             juce::dontSendNotification);
    };

    tkDelete.onClick = [this]
    {
        if (tkSelected < 0 || tkSelected >= static_cast<int> (takes.size()))
            return;

        const juce::String name = takes[static_cast<size_t> (tkSelected)].name;
        processor.deleteTake (tkSelected);
        tkSelected = 0;
        refreshTakes();
        tkResult.setText ("Deleted \"" + name + "\".", juce::dontSendNotification);
    };

    // ---- header navigation, settings and about ----
    for (juce::TextButton* b : std::initializer_list<juce::TextButton*> {
             &settingsButton, &aboutButton, &backButton,
             &resetSizeButton, &reloadProfilesBtn, &manualButton, &repoButton,
             &emailButton })
    {
        styleButton (*b, false);
        addAndMakeVisible (*b);
    }

    settingsButton.onClick = [this] { screen = Screen::Settings; updateModeVisibility(); };
    aboutButton.onClick    = [this] { screen = Screen::About;    updateModeVisibility(); };
    backButton.onClick     = [this] { screen = Screen::Song;     updateModeVisibility(); };

    resetSizeButton.onClick = [this] { setSize (1180, 820); };

    styleCombo (themeBox);
    addChildComponent (themeBox);
    for (int i = 0; i < ghost::numThemes; ++i)
        themeBox.addItem (ghost::themeName (i), i + 1);
    themeBox.setSelectedId (processor.theme.load() + 1, juce::dontSendNotification);

    themeBox.onChange = [this] { applyThemeChoice (themeBox.getSelectedId() - 1); };

    reloadProfilesBtn.onClick = [this] { processor.reloadPlan(); };

    repoButton.onClick = []
    {
        juce::URL ("https://github.com/mourninggrace/Ghostband").launchInDefaultBrowser();
    };

    emailButton.onClick = []
    {
        juce::URL ("mailto:mourning.grace.2014@gmail.com?subject=Ghostband")
            .launchInDefaultBrowser();
    };

    manualButton.onClick = []
    {
        // The README IS the manual, decided 2026-09-07. It is long, current, and
        // its screenshots regenerate themselves from the harness, so a second
        // document could only duplicate it and then drift - which is exactly
        // what NEXT.md did before it had to be split up.
        //
        // This used to look for a Ghostband-manual.pdf beside the plugin and
        // fall back to the README when it was missing. That file was never
        // going to exist, so the fallback was the whole behaviour, dressed up
        // as a stopgap.
        juce::URL ("https://github.com/mourninggrace/Ghostband#readme")
            .launchInDefaultBrowser();
    };

    struct ChannelBox { juce::ComboBox* box; juce::Label* label; const char* name;
                        std::atomic<int>* target; };
    const ChannelBox channelBoxes[5] = {
        { &chDrums,   &chDrumsLabel,   "DRUMS",    &processor.channelDrums   },
        { &chBass,    &chBassLabel,    "BASS",     &processor.channelBass    },
        { &chGuitar,  &chGuitarLabel,  "GUITAR",   &processor.channelGuitar  },
        { &chGuitar2, &chGuitar2Label, "GUITAR 2", &processor.channelGuitar2 },
        { &chPiano,   &chPianoLabel,   "PIANO",    &processor.channelPiano   },
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

        initLabel (*c.label, c.name, 15.0f, ghost::dim, juce::Justification::centredLeft);
    }

    for (juce::Label* l : { &chDrumsName, &chBassName, &chGuitarName,
                            &chGuitar2Name, &chPianoName })
        initLabel (*l, "", 15.5f, ghost::text, juce::Justification::centredLeft);

    initLabel (learnPartName, "", 15.5f, ghost::accent, juce::Justification::centredLeft);

    juce::TextButton* testButtons[5] = { &testDrums, &testBass, &testGuitar,
                                         &testPiano, &testGuitar2 };
    for (int i = 0; i < 5; ++i)
    {
        styleButton (*testButtons[i], false);
        testButtons[i]->onClick = [this, i]
        {
            // Testing a part the song does not have used to send notes through
            // a default profile, whose guessed range the real instrument is
            // silent in. That produced visible MIDI and no sound - which reads
            // as a broken instrument rather than a song without one.
            if (! processor.partIsInSong (i))
            {
                static const char* names[] = { "drums", "bass", "guitar", "piano",
                                               "second guitar" };
                statusLabel.setText (juce::String ("This song has no ") + names[i]
                                         + ". Load a song that uses it, or add it in Edit song.",
                                     juce::dontSendNotification);
                return;
            }

            processor.testPart (i);
            statusLabel.setText (processor.getLastMidiReport(), juce::dontSendNotification);
        };
        addChildComponent (*testButtons[i]);
    }

    // ---- MIDI Learn helpers ----
    styleCombo (learnPart);
    addChildComponent (learnPart);
    learnPart.addItem ("drums",    1);
    learnPart.addItem ("bass",     2);
    learnPart.addItem ("guitar",   3);
    learnPart.addItem ("piano",    4);
    learnPart.addItem ("guitar 2", 5);
    learnPart.setSelectedId (3, juce::dontSendNotification);

    // An open-ended list rather than fixed slots: how many knobs, buttons and
    // switches are worth automating is the owner's decision.
    for (juce::TextButton* b : std::initializer_list<juce::TextButton*> {
             &ctlAdd, &ctlRemove, &ctlTeach, &ctlSave, &ctlSend, &ctlWalk })
    {
        styleButton (*b, b == &ctlTeach);
        addChildComponent (*b);
    }

    ctlName.setColour (juce::TextEditor::textColourId, ghost::text);
    ctlName.setFont (juce::Font (juce::FontOptions (17.0f)));
    ctlName.onFocusLost = [this] { pushControlEdit(); };
    ctlName.onReturnKey = [this] { pushControlEdit(); };
    addChildComponent (ctlName);

    styleCombo (ctlFollows);
    styleCombo (ctlType);
    addChildComponent (ctlFollows);
    addChildComponent (ctlType);

    ctlPositions.setColour (juce::TextEditor::textColourId, ghost::text);
    ctlPositions.setFont (juce::Font (juce::FontOptions (17.0f)));
    ctlPositions.setJustification (juce::Justification::centred);
    ctlPositions.setInputRestrictions (3, "0123456789");
    ctlPositions.onFocusLost = [this] { pushControlEdit(); };
    ctlPositions.onReturnKey = [this] { pushControlEdit(); };
    addChildComponent (ctlPositions);

    ctlValue.setColour (juce::TextEditor::textColourId, ghost::text);
    ctlValue.setFont (juce::Font (juce::FontOptions (17.0f)));
    ctlValue.setJustification (juce::Justification::centred);
    ctlValue.setInputRestrictions (3, "0123456789");
    ctlValue.onFocusLost = [this] { pushControlEdit(); };
    ctlValue.onReturnKey = [this] { pushControlEdit(); };
    addChildComponent (ctlValue);

    for (juce::TextEditor* e : { &ctlFrom, &ctlTo })
    {
        e->setColour (juce::TextEditor::textColourId, ghost::text);
        e->setFont (juce::Font (juce::FontOptions (17.0f)));
        e->setJustification (juce::Justification::centred);
        e->setInputRestrictions (3, "0123456789");
        e->onFocusLost = [this] { pushControlEdit(); };
        e->onReturnKey = [this] { pushControlEdit(); };
        addChildComponent (*e);
    }
    for (const char* f : { "intensity", "lead", "peaks", "rising",
                           "random", "random once", "level", "fixed", "none" })
        ctlFollows.addItem (f, ctlFollows.getNumItems() + 1);
    for (const char* t : { "knob", "switch", "select" })
        ctlType.addItem (t, ctlType.getNumItems() + 1);

    ctlFollows.onChange = [this] { pushControlEdit(); };
    ctlType.onChange    = [this] { pushControlEdit(); };

    // The list is in part order, so the id is the part index plus one.
    const auto part = [this] { return juce::jlimit (0, 4, learnPart.getSelectedId() - 1); };

    learnPart.onChange = [this] { ctlSelected = 0; refreshControls(); };

    ctlAdd.onClick    = [this, part] { processor.addControl (part());
                                       ctlSelected = juce::jmax (0, processor.getControlCount (part()) - 1);
                                       refreshControls(); };
    ctlRemove.onClick = [this, part] { processor.removeControl (part(), ctlSelected);
                                       ctlSelected = 0; refreshControls(); };
    ctlTeach.onClick  = [this, part] { processor.teachControlSlot (part(), ctlSelected); };

    ctlSend.onClick = [this, part]
    {
        pushControlEdit();          // commit a value still being typed
        processor.sendControlNow (part(), ctlSelected);
        statusLabel.setText ("Sent. Look at the instrument to see where it landed.",
                             juce::dontSendNotification);
    };

    ctlWalk.onClick = [this, part]
    {
        pushControlEdit();
        processor.walkControl (part(), ctlSelected);
        statusLabel.setText ("Stepping through every position, half a second each. Count them.",
                             juce::dontSendNotification);
    };

    ctlSave.onClick = [this, part]
    {
        juce::String err;
        if (processor.saveControls (part(), err))
            statusLabel.setText ("Mappings saved to the instrument's profile.",
                                 juce::dontSendNotification);
        else
            statusLabel.setText (err, juce::dontSendNotification);
    };

    ctlList.onRowClicked = [this] (int row) { ctlSelected = row; refreshControls(); };

    ctlViewport.setViewedComponent (&ctlList, false);
    ctlViewport.setScrollBarsShown (true, false);
    addChildComponent (ctlViewport);

    initLabel (ctlNameLabel,    "NAME",    15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlFollowsLabel, "FOLLOWS", 15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlTypeLabel,    "TYPE",    15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlPositionsLabel, "CHOICES", 15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlPositionsHint, "how many choices", 15.0f, ghost::dim,
               juce::Justification::centredLeft);
    initLabel (ctlValueLabel, "VALUE", 15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlRangeLabel, "RANGE", 15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlRangeToLabel, "to", 15.0f, ghost::dim, juce::Justification::centred);
    initLabel (ctlRangeHint, "", 15.0f, ghost::dim, juce::Justification::centredLeft);
    initLabel (ctlValueHint, "", 15.0f, ghost::dim, juce::Justification::centredLeft);

    initLabel (learnHeading, "MIDI LEARN", 15.5f, ghost::text, juce::Justification::centredLeft);
    // Shorter than it was, because refreshControls now puts a live line above
    // it saying what the mix knob reaches, and the label has room for one
    // paragraph rather than two. What came out is the part the list already
    // shows: which type means what.
    initLabel (learnHelp, kLearnHelp, 15.5f, ghost::dim, juce::Justification::topLeft);
    learnHelp.setJustificationType (juce::Justification::topLeft);

    initLabel (settingsHeading, "SETTINGS", 19.0f, ghost::text, juce::Justification::centredLeft);
    initLabel (themeLabel,      "THEME",    14.0f, ghost::dim,  juce::Justification::centredLeft);
    initLabel (channelsHelp,
               "Each part is sent on its own MIDI channel. Set the matching channel on each "
               "instrument, or use a channel filter in your host.",
               15.5f, ghost::dim, juce::Justification::topLeft);
    channelsHelp.setJustificationType (juce::Justification::topLeft);

    processor.stateChanged.addChangeListener (this);
    refreshFromProcessor();
    updateModeVisibility();

    // Resizable, with a floor that keeps the controls from overlapping. The
    // size lives on the processor so it survives closing the window and is
    // saved with the rest of the plugin state.
    setResizable (true, true);
    // The floor is what the screens actually need, not an arbitrary small
    // number, and it is set by SETTINGS rather than by the song screen. The
    // song screen wants the 320-wide rail plus five readable tracker columns,
    // which is about 900. Settings wants a 460 column of channels beside a 496
    // column of the learn form, which is 1020 - and a minimum that fits five
    // screens and breaks the sixth is not a minimum.
    setResizeLimits (1020, 820, 2400, 2200);

    // Restore the remembered size, then start recording changes to it. The
    // order matters: recording before this point captures the zero-sized
    // editor and loses what was remembered.
    setSize (juce::jmax (1020, processor.editorWidth.load()),
             juce::jmax (820, processor.editorHeight.load()));
    sizeInitialised = true;

    // ---- tooltips ----
    //
    // On everything, and they explain the CHOICES rather than just naming the
    // control. "Feel" is useless; "half time - the backbeat moves to beat 3, so
    // the section feels half as fast without changing tempo" is the thing
    // somebody actually wants to know. A vocabulary this plugin invented -
    // follows, feels, fills, takes - is not guessable, and until now it was
    // documented only in the README.
    {
        const auto tip = [] (juce::Component& c, const juce::String& text)
        {
            if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (&c))
                t->setTooltip (text);
        };

        // ---- transport and the song ----
        tip (loadButton,  "Open a different song. Presets live inside the plugin; your own are in "
                          "Documents\\Ghostband\\Songs.");
        tip (reloadButton,"Re-read this song from disk, throwing away unsaved edits. Useful if you "
                          "have been editing the JSON in a text editor.");
        tip (rollButton,  "A different performance of the SAME song - same chords, same structure, "
                          "different playing. Ctrl-click sections first to reroll only those; "
                          "every other section is provably untouched.");
        tip (playPauseButton, "Stops the band without stopping your host. Ghostband follows the "
                              "host transport, so it cannot start one that is not running.");

        tip (keyBox,   "Transposes the whole song, including chords written into the plan - not "
                       "just generated ones.");
        tip (modeBox,  "The scale the song is built from. natural minor is the default rock and "
                       "metal choice; harmonic minor raises the 7th for a darker, more classical "
                       "pull; dorian is minor with a brighter 6th; major is major.");
        tip (styleBox, "How the band plays: which grooves, how the bass moves, whether the guitars "
                       "use power chords, how busy the drums get. It changes the playing, not the "
                       "chords.");
        tip (zoomBox,  "How much music one row of the grid below covers, which is the single "
                       "biggest change you can make to what it tells you. bar is the shape of the "
                       "arrangement - one line per bar, with x7 meaning seven hits landed in it. "
                       "beat is the default and reads like a chart. 8th shows off-beat placement, "
                       "the pushes and the swing. 16th is a real tracker: every hi-hat where it "
                       "actually sits, at the cost of showing about a bar and a half at a time. "
                       "Nothing about the music changes - only how closely you are looking at it.");
        tip (zoomLabel,"How much music one row of the grid covers. Finer rows show placement; "
                       "coarser rows show shape.");
        tip (tuningBox,"What the bass is tuned to, which sets how low it can go. drop_d, drop_c "
                       "and b_standard each lower the bottom string further.");

        tip (complexitySlider, "How busy the playing is: ghost notes, extra kicks, fills, how much "
                               "the parts subdivide. Low is simple and solid; high is a busy "
                               "player. It does not change what the song IS.");
        tip (humanizeSlider,   "How loose the timing and velocity are. 0 is a machine, dead on the "
                               "grid; high is a human having a good night. Too high starts to "
                               "sound drunk.");
        tip (fillsSlider,      "How often the second guitar answers in the gaps. 0 silences the "
                               "answering across the whole song without editing a single section; "
                               "1 takes every opening it is offered.");

        tip (seedEditor, "The number the whole performance is generated from. The same seed and "
                         "the same dials always produce exactly the same song - that is what "
                         "makes a take recallable. Type one you liked to get it back.");
        tip (bpmEditor,  "The tempo the song is WRITTEN at, not a playback speed: the generators "
                         "subdivide against it, so a faster song is arranged differently rather "
                         "than just played faster.");

        tip (takesButton,     "Save the performance you are hearing under a name, and get it back "
                              "exactly. A preset is a song; a take is one performance of it.");
        tip (editButton,      "Change the song itself: sections, chords, bars, who plays where.");
        tip (calibrateButton, "Play one note at a time and tell Ghostband what it heard, so a "
                              "driver profile stops being a guess.");
        tip (settingsButton,  "MIDI channels, teaching Ghostband your instruments' own knobs, and "
                              "the colour theme.");
        tip (aboutButton,     "What this is, who wrote it, and the licence.");
        tip (backButton,      "Back to the song.");

        tip (levelDrums,   "Drum level, sent to the instrument's own volume control.");
        tip (levelBass,    "Bass level, sent to the instrument's own volume control.");
        tip (levelGuitar,  "Rhythm guitar level.");
        tip (levelGuitar2, "Second guitar level.");
        tip (levelPiano,   "Piano level.");
        tip (mixLabel,     "One knob per part, always all five, always in the same places - so a "
                           "knob you cannot use is never confused with a knob that has gone "
                           "missing. Each says why it is greyed: \"not in song\" means this song "
                           "has no such part; \"no reach\" means the instrument's volume cannot be "
                           "addressed from outside at all, so use a gain plugin after it instead; "
                           "\"CC7\" means the knob is guessing with a controller most instruments "
                           "ignore - teach that instrument its volume control in Settings and the "
                           "knob becomes real.");

        // ---- structure editor ----
        tip (edName,   "The section's name, and it is load-bearing: the ROLE is inferred from it, "
                       "so renaming a section to chorus2 genuinely makes it behave like a chorus.");
        tip (edBars,   "How many bars this section lasts.");
        tip (edChords, "Chords as text, e.g. Em Em C D. A shorter list repeats to fill the bars. "
                       "Leave it empty to have Ghostband choose a progression for the role.");
        tip (edIntensity, "How hard this section is played. It drives the drums, the dynamics and "
                          "which parts lead.");
        tip (edFeel,   "straight is normal. half time moves the backbeat to beat 3, so the section "
                       "feels half as fast without changing tempo. double time is the opposite. "
                       "blast is a blast beat.");
        tip (edFill,   "Drum fills into the next section. auto decides from the role, none never "
                       "fills, big always does a full-bar one.");
        tip (edLead,   "Which part leads this section; the others thin out and drop an octave "
                       "clear so they support rather than compete. auto decides from the style "
                       "and intensity.");
        tip (edDrums,  "Whether the drums play in this section at all.");
        tip (edBass,   "Whether the bass plays in this section at all.");
        tip (edGuitar, "Whether the rhythm guitar plays in this section at all.");
        tip (edGuitar2,"Whether the second guitar plays in this section - the part that solos and "
                       "answers in the gaps.");
        tip (edPiano,  "Whether the piano plays in this section at all.");
        tip (edAddButton,    "Add a section after this one, copying it - a new section is nearly "
                             "always a variation of the one before.");
        tip (edDeleteButton, "Delete this section. The last one cannot be deleted; a song with no "
                             "sections cannot render.");
        tip (edUpButton,     "Move this section earlier in the song.");
        tip (edDownButton,   "Move this section later in the song.");
        tip (edSaveButton,   "Save the song, keeping the previous version in a backups folder. "
                             "Editing a shipped preset saves a copy into your own songs folder "
                             "instead of overwriting the preset.");
        tip (edSaveAsButton, "Save as a new song in Documents\\Ghostband\\Songs.");
        tip (edDoneButton,   "Back to the song.");

        // ---- settings ----
        tip (chDrums,  "The MIDI channel the drums are sent on. Set the same channel on the "
                       "instrument, or use a channel filter in your host.");
        tip (chBass,   "The MIDI channel the bass is sent on.");
        tip (chGuitar, "The MIDI channel the rhythm guitar is sent on.");
        tip (chGuitar2,"The MIDI channel the second guitar is sent on.");
        tip (chPiano,  "The MIDI channel the piano is sent on.");
        tip (testDrums,  "Play a few obvious notes on this channel right now, with the transport "
                         "stopped. Turns 'nothing is playing' into 'this part is not routed'.");
        tip (testBass,   "Play a few obvious notes on this channel right now.");
        tip (testGuitar, "Play a few obvious notes on this channel right now.");
        tip (testGuitar2,"Play a few obvious notes on this channel right now.");
        tip (testPiano,  "Play a few obvious notes on this channel right now.");

        tip (learnPart, "Which instrument's controls you are editing. Mappings are remembered "
                        "against the INSTRUMENT, so teaching SSD5 once covers every song that "
                        "uses it.");
        tip (ctlAdd,    "Add a control. Name it whatever the knob is called on the instrument.");
        tip (ctlRemove, "Remove this control. Ghostband stops sending it entirely.");
        tip (ctlTeach,  "Sweep this CC so the instrument's MIDI Learn can latch onto it. Put the "
                        "instrument's control into MIDI Learn FIRST, then press this.");
        tip (ctlSend,   "Send this control once at its parked value, so you can see which knob "
                        "moves.");
        tip (ctlWalk,   "Step slowly through every position of a selector so you can count them.");
        tip (ctlSave,   "Write these mappings to disk. They are remembered per instrument.");
        tip (ctlName,   "What this knob is called on the instrument. Only for your own reading - "
                        "Ghostband matches on the CC number.");
        tip (ctlFollows,"What the arrangement does with this control. intensity tracks how hard "
                        "the section is played. lead is up when this part leads and down when it "
                        "supports. peaks is on for choruses and solos only. rising climbs across "
                        "the whole song. random re-chooses every section; random once chooses "
                        "per song and holds - use that for anything that is a TONE, or the sound "
                        "shifts under the same riff. level hands the control to the mix knob. "
                        "fixed parks it. none leaves it alone entirely.");
        tip (ctlType,   "knob sweeps continuously. switch is on or off. select holds one of a "
                        "fixed number of choices, like an amp model list.");
        tip (ctlPositions, "How many choices a selector has. Use Walk the list to count them.");
        tip (ctlValue,  "Where a fixed control is parked.");
        tip (ctlFrom,   "The low end of the range this control travels. Put the HIGHER number "
                        "first to invert a control that reads backwards.");
        tip (ctlTo,     "The high end of the range this control travels.");

        tip (themeBox,  "The colour scheme. Neon and the dark themes light the tracker; Paper and "
                        "Blueprint draw it as ink on a page. Saved with the rest of your settings.");
        tip (resetSizeButton,   "Put the window back to its default size.");
        tip (reloadProfilesBtn, "Re-read the driver profiles from disk, for when you have edited "
                                "one by hand.");
        tip (tempoModeButton,   "Whether the song plays at the tempo it was written at, or "
                                "follows your host. A VST3 cannot set the host's tempo, so this "
                                "is the only way a song plays at its own.");

        // ---- calibrate ----
        tip (calPlayButton,   "Play the selected note. If it does not sound like its label, nudge "
                              "it until it does.");
        tip (calLowerButton,  "Move this mapping down one semitone.");
        tip (calHigherButton, "Move this mapping up one semitone.");
        tip (calSaveButton,   "Write what you measured into the driver profile. It names every "
                              "note that changed.");
        tip (calDoneButton,   "Back to the song, discarding anything not saved.");

        // ---- takes ----
        tip (tkName,       "What to call this performance. Saving over a name replaces that take.");
        tip (tkSaveButton, "Save the seed, all three dials AND the whole song, so this take comes "
                           "back exactly even if the preset it came from is later edited.");
        tip (tkRecall,     "Load this performance and go back to the song. Your mix and channels "
                           "are left alone - those are your rig, not the playing.");
        tip (tkDelete,     "Delete this take permanently.");
        tip (tkDoneButton, "Back to the song.");

        // ---- about ----
        tip (manualButton, "Open the README, which is the manual.");
        tip (repoButton,   "Open the source code on GitHub.");
        tip (emailButton,  "Email the author.");
        tip (donateButton, "Ghostband is free and always will be. This is a button, not a nag.");
    }

    // Fill it before the first tick, so the footer is never briefly blank -
    // and so an offline render of the editor shows it too.
    updateLatencyReadout();

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

// Read from the processor rather than hard-coded, so if Ghostband ever does
// take a lookahead the number moves on its own instead of quietly becoming a
// lie. The buffer size is the other half of the answer: it is where a rig's
// actual delay comes from, and it is the host's, not ours.
void GhostbandEditor::updateLatencyReadout()
{
    const double sr             = processor.getSampleRate();
    const int    latencySamples = processor.getLatencySamples();
    const int    blockSize      = processor.getBlockSize();

    juce::String text;
    if (sr > 0.0)
        text = "latency " + juce::String (latencySamples * 1000.0 / sr, 1) + " ms";
    else
        text = "latency " + juce::String (latencySamples) + " smp";

    if (blockSize > 0)
        text += "   buffer " + juce::String (blockSize);

    // A stall the owner has already seen and I have not - so when one happens,
    // the plugin has to be the thing that says so. Riding along on the latency
    // reading rather than taking its own corner, because a permanent slot for
    // something that is normally empty is a permanent question.
    text += stallSummary();

    if (text == lastLatencyText)
        return;

    lastLatencyText = text;
    latencyLabel.setText (text, juce::dontSendNotification);

    // Only when the text changed, which for the tooltip means only when a stall
    // was added - rebuilding a paragraph thirty times a second to say the same
    // thing would be its own small version of the fault being measured.
    //
    // setColour is a no-op when the colour has not moved, so this is safe here
    // even though a theme change will have repainted it in the meantime.
    latencyLabel.setColour (juce::Label::textColourId,
                            stalls.empty() ? ghost::dim : ghost::warn);
    latencyLabel.setTooltip (stallDetail());
}

// Records a gap in the timer, if there was one, and starts the clock on this
// callback. See the note on Stall in the header for why the gap and the work
// are measured separately.
// A middle dot, and it has to be built this way.
//
// juce::String's CONSTRUCTOR from a const char* decodes UTF-8, but its
// operator+ and operator+= decode the same bytes as Latin-1 - so the perfectly
// good two bytes C2 B7 came out as two characters, and every separator in the
// interface read "DRUMS A. no reach" instead of "DRUMS - no reach". It had been
// wrong in the mix labels since they were written and nobody had seen it,
// because until this session the only label carrying one was a case that almost
// never came up. Caught by looking at a rendered snapshot, which is the whole
// reason the harness renders them.
static const juce::String& midDot()
{
    static const juce::String d = juce::String::fromUTF8 ("\xc2\xb7");
    return d;
}

void GhostbandEditor::noteTimerTick()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const unsigned blocks = processor.audioBlocks.load (std::memory_order_relaxed);

    if (lastTimerStartMs > 0.0)
    {
        const double gap = now - lastTimerStartMs;
        worstGapMs = juce::jmax (worstGapMs, gap);

        if (gap > stallThresholdMs)
        {
            Stall s;
            s.gapMs        = gap;
            s.workMs       = lastTimerWorkMs;
            s.ghostbandMs  = gbdiag::Work::total;
            s.worstPieceMs = gbdiag::Work::worst;
            s.worstPiece   = gbdiag::Work::worstName;
            s.audioBlocks  = static_cast<int> (blocks - lastAudioBlocks);
            s.screen       = static_cast<int> (screen);
            s.playing      = processor.transportRunning.load();
            s.at           = juce::Time::getCurrentTime().toString (false, true, false);

            stalls.push_back (s);
            if (stalls.size() > maxStallsKept)
                stalls.erase (stalls.begin());

            // Appended to a file as well as kept in memory, because the window
            // that would show it is the thing that was frozen, and the session
            // it happened in may be closed before anyone looks. Writing here is
            // safe: this runs AFTER the stall, and by definition rarely.
            const juce::File log = GhostbandProcessor::stallLogFile();
            log.getParentDirectory().createDirectory();
            // The audio block figure only means something beside the block
            // size and rate, so both go in the line rather than having to be
            // remembered. 512 samples at 48k is 10.67 ms a block; if the blocks
            // account for the whole gap, the audio thread never missed one and
            // only the window was stuck.
            const double sr = processor.getSampleRate();
            log.appendText (s.at + "  gap " + juce::String (s.gapMs, 0) + " ms"
                            + "   ghostband " + juce::String (s.ghostbandMs, 1) + " ms"
                            + " (worst " + juce::String (s.worstPieceMs, 1) + " ms "
                            + s.worstPiece + ")"
                            + "   timer callback " + juce::String (s.workMs, 1) + " ms"
                            + "   audio " + juce::String (s.audioBlocks) + " blocks of "
                            + juce::String (processor.getBlockSize())
                            + (sr > 0.0 ? " at " + juce::String (sr / 1000.0, 1) + "k" : "")
                            + "   screen " + juce::String (screenName (s.screen))
                            + (s.playing ? "   playing" : "   stopped")
                            // In case the grid provokes it: 16th repaints four
                            // times as often as bar, so a stall that only ever
                            // happens on the fine settings is a different fault
                            // from one that happens on all of them.
                            + "   rows " + zoomBox.getText()
                            + "\n");
        }
    }

    lastTimerStartMs = now;
    lastAudioBlocks  = blocks;

    // Zeroed AFTER recording, so what accumulates from here belongs to the next
    // gap. Painting and handlers both run between ticks, which is the whole
    // reason this exists.
    gbdiag::Work::reset();
}

// What the footer says. Short, because it shares a line with the latency.
juce::String GhostbandEditor::stallSummary() const
{
    if (stalls.empty())
        return {};

    return "   " + midDot() + "   " + juce::String (static_cast<int> (stalls.size()))
         + (stalls.size() == 1 ? " stall" : " stalls")
         + ", worst " + juce::String (worstGapMs / 1000.0, 1) + "s";
}

// The whole story, in the tooltip, phrased as a conclusion rather than as
// numbers - the point of measuring was to be able to say which it was.
juce::String GhostbandEditor::stallDetail() const
{
    if (stalls.empty())
        return "How long the plugin adds to your rig, which is nothing: Ghostband "
               "writes MIDI and reports no latency. The buffer is your host's.\n\n"
               "If the window ever freezes while the music keeps playing, a note "
               "about it appears here and is written to stalls.log.";

    juce::String s = "The window stopped updating "
                   + juce::String (static_cast<int> (stalls.size()))
                   + (stalls.size() == 1 ? " time" : " times") + " this session.\n\n";

    for (size_t i = stalls.size(); i-- > 0 && stalls.size() - i <= 6;)
    {
        const Stall& st = stalls[i];

        // The whole reason four numbers are collected rather than one. A gap
        // says something went wrong; only the split says WHAT, and saying which
        // is the difference between a diagnosis and a shrug.
        //
        // ghostbandMs covers PAINTING and our own handlers as well as the timer
        // callback - the two things that happen between ticks and would
        // otherwise be invisible here. Without it, a slow paint of ours would
        // have been blamed on the host, confidently and wrongly.
        const double ours = juce::jmax (st.ghostbandMs, st.workMs);

        const juce::String blame =
            ours > st.gapMs * 0.5
                ? "Ghostband did this - " + st.worstPiece + " took "
                      + juce::String (st.worstPieceMs / 1000.0, 1) + "s."
          : st.audioBlocks <= 0
                ? "Audio stopped too, so the whole plugin was held up."
                : "Not Ghostband: it used " + juce::String (ours, 0)
                      + " ms of that, and the audio thread ran every one of its "
                      + juce::String (st.audioBlocks)
                      + " blocks without missing one. Something else on your "
                        "host's interface thread held it.";

        s << st.at << "   froze for " << juce::String (st.gapMs / 1000.0, 1) << "s on the "
          << screenName (st.screen) << " screen"
          << (st.playing ? " while playing.\n" : " while stopped.\n")
          << "    " << blame << "\n";
    }

    s << "\nAlso written to stalls.log beside your takes.";
    return s;
}

void GhostbandEditor::runTimerForTesting()        { timerCallback(); }
int  GhostbandEditor::stallCountForTesting() const { return static_cast<int> (stalls.size()); }
juce::String GhostbandEditor::stallDetailForTesting() const { return stallDetail(); }

void GhostbandEditor::timerCallback()
{
    noteTimerTick();
    const double workStart = juce::Time::getMillisecondCounterHiRes();

    updateLatencyReadout();
    refreshTracker();

    // Playhead.
    const int tick = processor.transportRunning.load() ? processor.playbackTick.load() : -1;
    if (tick != lastPlayheadTick)
    {
        lastPlayheadTick = tick;
        sectionList.setPlayhead (tick);
        arrangement.setPlayhead (tick);
        tracker.setPlayhead (tick);

        // "stopped" alone is a status; this is an instruction. Ghostband
        // follows the host transport, so with it stopped nothing is sent and a
        // fresh install looks broken rather than idle - which is the single
        // most common reason it appears to do nothing. The word that fixes it
        // belongs on the screen, not only in the README.
        //
        // Not said while paused: then it IS just stopped, by you, and the
        // Pause button beside this label already says how to undo that.
        juce::String where = processor.paused.load()
                               ? juce::String ("paused")
                               : juce::String ("stopped   -   press play in your host");

        // The song ending and the transport stopping are different events, and
        // the difference matters: one wants rewinding, the other wants playing.
        if (processor.songFinished.load() && ! processor.paused.load())
            // Stop AND start, not rewind. On its own clock Ghostband counts
            // its own ticks and ignores the host's position entirely, so moving
            // the host's playhead does nothing; what resets the song is the
            // transport stopping, which is the moment the count goes back to
            // zero. Telling somebody to rewind would have them dragging a
            // playhead that Ghostband is not reading.
            where = "song ended   -   stop and start your host to play it again";
        else if (tick >= 0)
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
        arrangement.setQueued (queued);
        tracker.setQueued (queued);
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

    // Last, so it measures everything above it.
    lastTimerWorkMs = juce::Time::getMillisecondCounterHiRes() - workStart;
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
    // The hint lives on the button itself when nothing is picked. Ctrl-click to
    // select a section was only ever in a tooltip, so it was reported as "still
    // unable to roll one section" - the feature worked, nothing said how.
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
    if (std::find (themedSliders.begin(), themedSliders.end(), &s) == themedSliders.end())
        themedSliders.push_back (&s);

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
    const bool tks  = (screen == Screen::Takes);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &chDrums, &chBass, &chGuitar, &chGuitar2, &chPiano,
             &chDrumsLabel, &chBassLabel, &chGuitarLabel, &chGuitar2Label, &chPianoLabel,
             &chDrumsName, &chBassName, &chGuitarName, &chGuitar2Name, &chPianoName,
             &learnPartName,
             &settingsHeading, &channelsHelp, &resetSizeButton, &reloadProfilesBtn,
             &themeBox, &themeLabel,
             &tempoModeButton,
             &testDrums, &testBass, &testGuitar, &testPiano, &testGuitar2,
             &learnPart, &learnHeading, &learnHelp,
             &ctlAdd, &ctlRemove, &ctlTeach, &ctlSave, &ctlName,
             &ctlFollows, &ctlType, &ctlPositions, &ctlViewport,
             &ctlNameLabel, &ctlFollowsLabel, &ctlTypeLabel, &ctlPositionsLabel,
             &ctlPositionsHint, &ctlValue, &ctlValueLabel, &ctlValueHint,
             &ctlSend, &ctlWalk, &ctlFrom, &ctlTo,
             &ctlRangeLabel, &ctlRangeToLabel, &ctlRangeHint })
        c->setVisible (set);

    if (set)
        refreshControls();

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &manualButton, &repoButton, &emailButton })
        c->setVisible (abt);

    // Settings and About are reachable from anywhere and lead back to the song.
    settingsButton.setVisible (! set && ! abt);
    aboutButton.setVisible    (! set && ! abt);
    backButton.setVisible     (set || abt);

    // Labels keep their bounds when a screen stops laying them out, so any that
    // the current screen does not position have to be hidden or they bleed
    // through it. Settings and About both paint over the song view's area.
    planLabel.setVisible (song || cal || edit);

    // Only the song screen ever positions the headline, so only the song screen
    // may show it. Left visible elsewhere it kept its old bounds and landed on
    // top of whatever that screen put there - the calibrate hint, and the edit
    // screen's name and bars fields.
    headlineLabel.setVisible (song);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &statusLabel, &profilesLabel, &statusCaption, &profilesCaption })
        c->setVisible (! abt);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &loadButton, &reloadButton, &rollButton, &calibrateButton, &editButton,
             &takesButton,
             &complexitySlider, &humanizeSlider, &fillsSlider,
             &fillsLabel, &complexityLabel,
             &humanizeLabel, &seedEditor, &seedLabel, &keyBox, &styleBox,
             &tuningBox, &keyLabel, &styleLabel, &tuningLabel,
             &zoomBox, &zoomLabel,
             &modeBox, &modeLabel,
             &tempoLabel, &transportLabel, &summaryLabel, &playPauseButton,
             &bpmLabel, &bpmEditor, &rollHintLabel,
             &mixLabel, &levelDrums, &levelBass, &levelGuitar, &levelGuitar2, &levelPiano,
             &levelDrumsLabel, &levelBassLabel, &levelGuitarLabel,
             &levelGuitar2Label, &levelPianoLabel })
        c->setVisible (song);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &calDoneButton, &calSaveButton, &calLowerButton, &calHigherButton,
             &calPlayButton, &calHintLabel, &calNoteLabel, &calViewport })
        c->setVisible (cal);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &tkDoneButton, &tkSaveButton, &tkRecall, &tkDelete, &tkName,
             &tkHeading, &tkHelp, &tkNameLabel, &tkResult, &tkViewport })
        c->setVisible (tks);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &edDoneButton, &edAddButton, &edDeleteButton, &edUpButton, &edDownButton,
             &edSaveButton, &edSaveAsButton, &edName, &edBars, &edChords,
             &edIntensity, &edFeel, &edFill, &edLead,
             &edDrums, &edBass, &edGuitar, &edGuitar2, &edPiano,
             &edNameLabel, &edBarsLabel, &edIntensityLabel, &edFeelLabel,
             &edFillLabel, &edChordsLabel, &edPlaysLabel, &edLeadLabel })
        c->setVisible (edit);

    // The section list is shared between the song view and the editor - the
    // same list, selected for a different reason.
    // The list is the edit screen's, where picking one row IS the job. The song
    // screen shows the arrangement instead.
    viewport.setVisible (edit);
    arrangement.setVisible (false);   // superseded by the tracker; kept for now
    tracker.setVisible (song);

    if (song) { lastTrackerTick = -1; refreshTracker(); }
    if (tks)  refreshTakes();
    if (cal)  refreshCalibration();
    if (edit) pullSectionEdit();
    if (! song) controlsPanel = {};
    if (song) { sectionList.setSelection (rerollSelection);
                arrangement.setSelection (rerollSelection);
                tracker.setSelection (rerollSelection); }

    resized();
}

const char* GhostbandEditor::screenName (int index)
{
    static const char* names[numScreens] = { "song", "calibrate", "edit", "settings",
                                             "about", "takes" };
    return names[juce::jlimit (0, numScreens - 1, index)];
}

// The footer, laid out the same way on every screen.
//
// Four screens each built this by hand, which is why they had drifted into
// three different heights. It is now one block: a hairline, then a captioned
// line for the status message and a captioned line for the instruments in the
// song. The captions are the point - "what is it, why is it there" was the
// report, and two stacked grey lines answered neither.
void GhostbandEditor::layOutFooter (juce::Rectangle<int> area)
{
    footerRule = area.removeFromTop (1);
    area.removeFromTop (8);

    const int captionWidth = 88;

    auto statusRow = area.removeFromTop (20);
    statusCaption.setBounds (statusRow.removeFromLeft (captionWidth));
    statusLabel.setBounds (statusRow);

    area.removeFromTop (2);

    auto profilesRow = area.removeFromTop (20);
    profilesCaption.setBounds (profilesRow.removeFromLeft (captionWidth));
    profilesLabel.setBounds (profilesRow);
}

void GhostbandEditor::ctrlClickSectionForTesting (int index)
{
    if (tracker.onSectionToggled)
        tracker.onSectionToggled (index);
}

void GhostbandEditor::editSectionForTesting (int index)
{
    screen = Screen::Edit;
    editSelected = index;
    updateModeVisibility();
}

void GhostbandEditor::commitSectionEditForTesting()
{
    pushSectionEdit();
}

void GhostbandEditor::setSectionGuitar2ForTesting (bool on)
{
    edGuitar2.setToggleState (on, juce::dontSendNotification);
    pushSectionEdit();
}

bool GhostbandEditor::sectionGuitar2ForTesting() const
{
    return edGuitar2.getToggleState();
}

void GhostbandEditor::pressRollForTesting()
{
    if (rollButton.onClick)
        rollButton.onClick();
}

int GhostbandEditor::rerollSelectionSizeForTesting() const
{
    return static_cast<int> (rerollSelection.size());
}

// Switching palette with the window already open.
//
// The hard part is not the palette - applyTheme swaps that in one call, and
// everything drawn in a paint() method reads the live colours and comes back
// correct on the next repaint. The hard part is the components that were handed
// a colour once, when they were built: a Label with an explicit textColourId
// keeps it forever, and a LookAndFeel change cannot override an explicit
// colour. Those have to be told again, by name, which is what recolour() does.
void GhostbandEditor::applyThemeChoice (int index)
{
    processor.theme.store (index);
    ghost::applyTheme (index);

    // The LookAndFeel holds its OWN copy of the palette, and it is not only the
    // editor's children that read it: a ComboBox's popup is a separate window
    // that takes its colours from here rather than from the box that opened it,
    // so without this the theme list itself became unreadable the moment the
    // light theme was chosen from it.
    lookAndFeel.applyPalette();

    recolour();

    // sendLookAndFeelChange, not lookAndFeelChanged: the second tells this one
    // component and stops there, and every control on the screen is a child.
    sendLookAndFeelChange();
    resized();
    repaint();
}

void GhostbandEditor::recolour()
{
    for (const auto& e : themedLabels)
        e.first->setColour (juce::Label::textColourId, *e.second);

    for (juce::Slider* sl : themedSliders)
        styleSlider (*sl);

    // Everything below takes the same palette roles wherever it appears, so it
    // is found by type rather than remembered. A child that was never styled
    // gets styled now, which is correct rather than merely harmless.
    for (int i = 0; i < getNumChildComponents(); ++i)
    {
        juce::Component* c = getChildComponent (i);

        if (auto* b = dynamic_cast<juce::TextButton*> (c))
        {
            // styleButton stashed this when it first ran, so a primary button
            // stays primary instead of quietly demoting itself on every change.
            styleButton (*b, static_cast<bool> (b->getProperties()["primary"]));
        }
        else if (auto* combo = dynamic_cast<juce::ComboBox*> (c))
        {
            styleCombo (*combo);
        }
        else if (auto* field = dynamic_cast<juce::TextEditor*> (c))
        {
            field->setColour (juce::TextEditor::backgroundColourId, ghost::background);
            field->setColour (juce::TextEditor::outlineColourId, ghost::line);
            field->setColour (juce::TextEditor::focusedOutlineColourId,
                              ghost::accent.withAlpha (0.6f));
            field->setColour (juce::TextEditor::textColourId, ghost::text);

            // setColour alone only reaches text typed AFTER it: a TextEditor
            // stores a colour per run of text, fixed when that run was added.
            // Without this the seed and the tempo were already in the box, so
            // they kept the dark theme's near-white and vanished into the light
            // theme's page - two boxes that looked empty while holding numbers.
            field->applyColourToAllText (ghost::text, true);
        }
        else if (auto* toggle = dynamic_cast<juce::ToggleButton*> (c))
        {
            toggle->setColour (juce::ToggleButton::textColourId, ghost::text);
            toggle->setColour (juce::ToggleButton::tickColourId, ghost::accent);
            toggle->setColour (juce::ToggleButton::tickDisabledColourId, ghost::line);
        }
        else if (auto* vp = dynamic_cast<juce::Viewport*> (c))
        {
            vp->setColour (juce::ScrollBar::thumbColourId, ghost::line.brighter (0.4f));
        }
    }
}

void GhostbandEditor::setThemeForTesting (int index)
{
    themeBox.setSelectedId (index + 1, juce::dontSendNotification);
    applyThemeChoice (index);
}

// Feeds the tracker the beats it is currently showing.
//
// The view never touches the sequence itself - that belongs to the audio
// thread, and a component reaching into it during a repaint is how you get a
// dropout. The editor's timer pulls a window of cells across and hands them
// over, and only when the window has actually moved.
// How many ticks one tracker row covers, at the zoom currently chosen.
//
// A bar rather than "four beats", because a plan can be in 5/4 or 7/8 and a row
// that claimed to be a bar but covered four beats would drift a beat per bar -
// the sort of error that looks like a rendering bug and is arithmetic.
int GhostbandEditor::trackerRowTicks (int beatTicks, int barTicks) const
{
    switch (juce::jlimit (0, GhostbandProcessor::numTrackerZooms - 1,
                          processor.trackerZoom.load()))
    {
        case 0:  return juce::jmax (1, barTicks);
        case 2:  return juce::jmax (1, beatTicks / 2);
        case 3:  return juce::jmax (1, beatTicks / 4);
        default: return juce::jmax (1, beatTicks);
    }
}

void GhostbandEditor::refreshTracker()
{
    if (screen != Screen::Song || tracker.getHeight() <= 0)
        return;

    const int beat = juce::jmax (1, processor.getBeatTicks());
    const int bar  = juce::jmax (1, processor.getBarTicks());
    const int perRow = trackerRowTicks (beat, bar);
    const int rows = tracker.visibleRows();
    const int now  = processor.transportRunning.load() ? processor.playbackTick.load() : 0;

    // The current row sits a third of the way down, so you can see what just
    // happened as well as what is coming. Snapped to a row, or the whole grid
    // would slide continuously and be unreadable.
    const int lead  = (rows / 3) * perRow;
    const int first = juce::jmax (0, ((now / perRow) * perRow) - lead);

    if (first == lastTrackerTick && rows == lastTrackerRows && perRow == lastTrackerPerRow)
        return;

    lastTrackerTick  = first;
    lastTrackerRows  = rows;
    lastTrackerPerRow = perRow;

    const std::vector<int> channels {
        processor.channelDrums.load(), processor.channelBass.load(),
        processor.channelGuitar.load(), processor.channelGuitar2.load(),
        processor.channelPiano.load() };

    tracker.setCells (processor.getTrackerCells (first, rows, channels, perRow),
                      first, perRow, beat, bar);
}

void GhostbandEditor::refreshTakes()
{
    takes = processor.getTakes();

    std::vector<TakeList::Row> rows;
    rows.reserve (takes.size());

    for (const GhostbandProcessor::Take& t : takes)
    {
        TakeList::Row row;
        row.name = t.name;
        row.song = t.songName;

        // Per cent, because that is how the same three dials are labelled on
        // the song screen. A row reading 0.62 and a dial reading 62% are the
        // same number twice and look like two different settings.
        const auto pc = [] (double v)
        {
            return juce::String (juce::roundToInt (juce::jlimit (0.0, 1.0, v) * 100.0)) + "%";
        };

        row.detail = "seed " + juce::String (t.seed)
                   + "     cplx " + pc (t.complexity)
                   + "     hum "  + pc (t.humanize)
                   + "     fills " + pc (t.fills);

        if (t.savedAt.isNotEmpty())
            row.detail += "     " + t.savedAt;

        rows.push_back (row);
    }

    tkSelected = juce::jlimit (0, juce::jmax (0, static_cast<int> (takes.size()) - 1),
                               tkSelected);
    tkList.setRows (std::move (rows));
    tkList.setSelected (tkSelected);
    resized();
}

void GhostbandEditor::saveTakeFromBox()
{
    const juce::String name = tkName.getText().trim();
    juce::String error;

    if (! processor.saveTake (name, error))
    {
        tkResult.setColour (juce::Label::textColourId, ghost::warn);
        tkResult.setText (error, juce::dontSendNotification);
        return;
    }

    refreshTakes();

    // Land the selection on what was just saved, so Recall and Delete point at
    // it without another click. Found by name rather than assumed to be last,
    // because saving over an existing take replaces it where it already sits.
    for (size_t i = 0; i < takes.size(); ++i)
        if (takes[i].name.equalsIgnoreCase (name))
        {
            tkSelected = static_cast<int> (i);
            tkList.setSelected (tkSelected);
            break;
        }

    tkResult.setColour (juce::Label::textColourId, ghost::accent);
    tkResult.setText ("Saved \"" + name + "\".", juce::dontSendNotification);
}

void GhostbandEditor::showScreenForSnapshot (int index)
{
    // EVERY screen but Calibrate leaves calibration, not just the song screen.
    //
    // Calibration silences the band - deliberately, so the note being
    // identified is not buried under it - and only case 0 used to switch it
    // off. So any sweep of the screens that did not happen to END on the song
    // screen left the processor mute for everything after it, which cost a real
    // debugging session: a playback test reported the transport as not running
    // and the playhead as frozen, and the cause was a screenshot loop three
    // hundred lines earlier finishing on the Takes screen.
    const int wanted = juce::jlimit (0, numScreens - 1, index);

    if (wanted == 1) processor.enterCalibration();
    else             processor.exitCalibration();

    switch (wanted)
    {
        case 1:  screen = Screen::Calibrate; break;
        case 2:  screen = Screen::Edit;      break;
        case 3:  screen = Screen::Settings;  break;
        case 4:  screen = Screen::About;     break;
        case 5:  screen = Screen::Takes;     break;
        default: screen = Screen::Song;      break;
    }
    updateModeVisibility();
}

double GhostbandEditor::controlUnitsToNorm (const juce::String& type, int positions, int typed)
{
    if (type == "select")
    {
        const int last = juce::jmax (1, positions - 1);
        return juce::jlimit (0, last, typed - 1) / static_cast<double> (last);
    }

    if (type == "switch")
        return typed != 0 ? 1.0 : 0.0;

    return juce::jlimit (0, 100, typed) / 100.0;
}

int GhostbandEditor::normToControlUnits (const juce::String& type, int positions, double v)
{
    if (type == "select")
        return juce::roundToInt (v * juce::jmax (1, positions - 1)) + 1;

    if (type == "switch")
        return v > 0.5 ? 1 : 0;

    return juce::roundToInt (v * 100.0);
}

juce::String GhostbandEditor::controlUnitsHint (const juce::String& type, int positions)
{
    if (type == "select") return "positions, 1 to " + juce::String (juce::jmax (2, positions));
    if (type == "switch") return "1 for on, 0 for off";
    return "per cent of the knob's travel";
}

void GhostbandEditor::refreshControls()
{
    // 0..4, not 0..3. Clamping to four parts while the buttons beside this list
    // clamped to five meant picking "guitar 2" showed the PIANO's mappings while
    // Teach and Save acted on the second guitar - a list of one instrument's
    // controls with another instrument's buttons under it.
    const int part = juce::jlimit (0, 4, learnPart.getSelectedId() - 1);
    const int count = processor.getControlCount (part);
    ctlSelected = juce::jlimit (0, juce::jmax (0, count - 1), ctlSelected);

    std::vector<ControlList::Row> rows;
    for (int i = 0; i < count; ++i)
    {
        const auto c = processor.getControl (part, i);
        rows.push_back ({ c.name, c.cc, c.follows, c.type, c.positions });
    }
    ctlList.setRows (std::move (rows));
    ctlList.setSelected (ctlSelected);
    ctlList.setSize (juce::jmax (100, ctlViewport.getWidth() - 10), ctlList.getHeight());

    // Guitar and piano are optional. When the loaded song has neither, its
    // mappings are still safely in the profile file - they just belong to a
    // song that is not open. Say that, rather than showing a blank list.
    const bool inSong = processor.partIsInSong (part);
    ctlList.setEmptyMessage (inSong
        ? "No controls mapped yet. Press Add."
        : "This song has no " + learnPart.getText()
              + ". Its mappings are safe in the instrument's profile - load a "
                "song that uses it to see them.");

    // What this part's mix knob currently reaches, said out loud.
    //
    // "level" was explained in the help paragraph below and missed anyway,
    // which is fair: it is one word in the middle of five sentences, and the
    // consequence of not setting it shows up on a different screen as a knob
    // that silently does nothing. The mapping list is where it is fixed, so it
    // is where the state belongs.
    juce::String mix = "MIX KNOB: ";
    {
        juce::String reaches;
        for (int i = 0; i < count; ++i)
        {
            const auto c = processor.getControl (part, i);
            if (c.follows == "level")
            {
                reaches = c.name + " (CC" + juce::String (c.cc) + ")";
                break;
            }
        }

        mix += reaches.isNotEmpty()
                 ? reaches
                 : juce::String ("nothing yet - set one control to follow \"level\", "
                                 "or the knob falls back to CC 7 and most plugins ignore it.");
    }
    learnHelp.setText (mix + "\n" + kLearnHelp, juce::dontSendNotification);
    learnHelp.setColour (juce::Label::textColourId, ghost::dim);

    const bool any = count > 0;
    ctlAdd.setEnabled (inSong);
    ctlRemove.setEnabled (any);
    ctlTeach.setEnabled (any);
    ctlName.setEnabled (any);
    ctlFollows.setEnabled (any);
    ctlType.setEnabled (any);

    suppressControlCallbacks = true;
    bool isSelect = false;
    bool isFixed  = false;
    bool isDriven = false;
    if (any)
    {
        const auto c = processor.getControl (part, ctlSelected);
        ctlName.setText (c.name, juce::dontSendNotification);
        for (int i = 1; i <= ctlFollows.getNumItems(); ++i)
            if (ctlFollows.getItemText (i - 1) == c.follows)
                ctlFollows.setSelectedId (i, juce::dontSendNotification);
        for (int i = 1; i <= ctlType.getNumItems(); ++i)
            if (ctlType.getItemText (i - 1) == c.type)
                ctlType.setSelectedId (i, juce::dontSendNotification);

        isSelect = c.type == "select";
        if (isSelect)
            ctlPositions.setText (juce::String (juce::jmax (2, c.positions)),
                                  juce::dontSendNotification);

        // The same box means a different number for each type, because "which
        // position" and "how far up" are not the same question.
        const juce::String units = controlUnitsHint (c.type, c.positions);

        isFixed = c.follows == "fixed";
        if (isFixed)
        {
            ctlValue.setText (juce::String (normToControlUnits (c.type, c.positions, c.low)),
                              juce::dontSendNotification);
            ctlValueHint.setText (units, juce::dontSendNotification);
        }

        isDriven = c.follows != "fixed" && c.follows != "none";
        if (isDriven)
        {
            ctlFrom.setText (juce::String (normToControlUnits (c.type, c.positions, c.low)),
                             juce::dontSendNotification);
            ctlTo.setText   (juce::String (normToControlUnits (c.type, c.positions, c.high)),
                             juce::dontSendNotification);

            // Said plainly, because "put the bigger number first" is not
            // something anyone guesses at a pair of boxes.
            ctlRangeHint.setText (units + " - put the higher one first to invert",
                                  juce::dontSendNotification);
        }

        ctlTeach.setButtonText ("Teach  (CC " + juce::String (c.cc) + ")");
    }
    else
    {
        ctlName.setText ({}, juce::dontSendNotification);
        ctlTeach.setButtonText ("Teach this control");
    }

    // The choices box is only meaningful for a selector, so it appears with one
    // rather than sitting there greyed out asking to be misread.
    const bool showChoices = any && isSelect && screen == Screen::Settings;
    ctlPositions.setVisible (showChoices);
    ctlPositionsLabel.setVisible (showChoices);
    ctlPositionsHint.setVisible (showChoices);
    ctlWalk.setVisible (showChoices);

    // The value box only means anything for a parked control, and the layout
    // gives back its row when it is not there.
    const bool showValue = any && isFixed && screen == Screen::Settings;
    ctlValue.setVisible (showValue);
    ctlValueLabel.setVisible (showValue);
    ctlValueHint.setVisible (showValue);
    ctlSend.setVisible (showValue);

    // A parked control has a value and no range; a driven one has a range and
    // no single value. They share the row because they are never both true.
    const bool showRange = any && isDriven && screen == Screen::Settings;
    ctlFrom.setVisible (showRange);
    ctlTo.setVisible (showRange);
    ctlRangeLabel.setVisible (showRange);
    ctlRangeToLabel.setVisible (showRange);
    ctlRangeHint.setVisible (showRange);

    suppressControlCallbacks = false;

    // The value row appears and disappears, so the rows under it have to move.
    if (screen == Screen::Settings)
        resized();
}

void GhostbandEditor::pushControlEdit()
{
    if (suppressControlCallbacks || screen != Screen::Settings)
        return;

    const int part = juce::jlimit (0, 4, learnPart.getSelectedId() - 1);
    if (processor.getControlCount (part) == 0)
        return;

    const GhostbandProcessor::ControlSlot before = processor.getControl (part, ctlSelected);

    GhostbandProcessor::ControlSlot s = before;
    s.name    = ctlName.getText();
    s.follows = ctlFollows.getText();
    s.type    = ctlType.getText();
    if (s.type == "select")
    {
        // 128 is the ceiling because a controller has no more values than that.
        // An empty or half-typed box must not collapse the mapping to two
        // choices, so a number below two is left as it was.
        const int typed = ctlPositions.getText().getIntValue();
        s.positions = typed >= 2 ? juce::jmin (128, typed)
                                 : juce::jmax (2, s.positions);
    }
    if (s.follows == "fixed")
    {
        if (ctlValue.getText().isNotEmpty())
        {
            // low and high both move: a parked control has no range to travel.
            s.low = s.high = controlUnitsToNorm (s.type, s.positions,
                                                 ctlValue.getText().getIntValue());
        }
    }
    else if (before.follows == "fixed" && s.low == s.high)
    {
        // Coming off "fixed" leaves low and high sitting on the same number,
        // which is a range with nowhere to travel: the control would keep the
        // parked value and never move, however it was set to follow. Two
        // controls were found silently pinned this way, both marked "random
        // once" and both stuck on one choice for every song.
        s.low  = 0.0;
        s.high = 1.0;
    }
    else if (s.follows != "none")
    {
        // Not clamped into order: from above to is how a control that reads
        // backwards gets inverted, and the engine already interpolates either
        // way round.
        if (ctlFrom.getText().isNotEmpty())
            s.low = controlUnitsToNorm (s.type, s.positions, ctlFrom.getText().getIntValue());
        if (ctlTo.getText().isNotEmpty())
            s.high = controlUnitsToNorm (s.type, s.positions, ctlTo.getText().getIntValue());
    }

    processor.updateControl (part, ctlSelected, s);

    // Say it out loud when Ghostband chose something. Silently changing two
    // fields because of what you typed in a third is the sort of help nobody
    // asked for; saying which and why makes it an offer.
    const juce::String suggested = processor.getLastSuggestion();
    if (suggested.isNotEmpty())
    {
        statusLabel.setText (suggested + "   (change it if that is wrong)",
                             juce::dontSendNotification);
        processor.clearLastSuggestion();
    }

    refreshControls();
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
    for (int i = 1; i <= edLead.getNumItems(); ++i)
        if (edLead.getItemText (i - 1) == e.lead)
            edLead.setSelectedId (i, juce::dontSendNotification);

    edDrums.setToggleState   (e.drums,   juce::dontSendNotification);
    edBass.setToggleState    (e.bass,    juce::dontSendNotification);
    edGuitar.setToggleState  (e.guitar,  juce::dontSendNotification);

    // Load-bearing, not symmetry. applySectionEdit now WRITES playsGuitar2,
    // where before it left the flag alone - so a toggle that was never filled in
    // here would read false and switch the second guitar off the first time any
    // other field on this form was touched. Renaming a section would delete its
    // solo, which is precisely the failure the old code avoided by not writing
    // the flag at all.
    edGuitar2.setToggleState (e.guitar2, juce::dontSendNotification);

    edPiano.setToggleState   (e.piano,   juce::dontSendNotification);
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
    e.lead      = edLead.getText();
    e.chords    = edChords.getText();
    e.drums     = edDrums.getToggleState();
    e.bass      = edBass.getToggleState();
    e.guitar    = edGuitar.getToggleState();
    e.guitar2   = edGuitar2.getToggleState();
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

    // Never an empty caption. The status message is blank when there is nothing
    // wrong, which left "STATUS" sitting on the footer with nothing beside it -
    // reading as a value that failed to load rather than as good news. Now that
    // every profile can actually be marked verified, blank is the common case.
    statusLabel.setText (s.message.isNotEmpty() ? s.message
                                                : juce::String ("all profiles verified"),
                         juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId,
                           s.unverifiedProfiles || ! s.ok ? ghost::warn : ghost::dim);

    juce::String profiles = "drums: " + s.drumProfile + "    bass: " + s.bassProfile;
    // Which instrument is in each slot, beside the slot. The rows name a part
    // and the part is a different plugin from one song to the next.
    {
        const auto slot = [] (const juce::String& name)
        {
            return name.isNotEmpty() ? name : juce::String ("- not in this song -");
        };

        chDrumsName.setText   (slot (s.drumProfile),    juce::dontSendNotification);
        chBassName.setText    (slot (s.bassProfile),    juce::dontSendNotification);
        chGuitarName.setText  (slot (s.guitarProfile),  juce::dontSendNotification);
        chGuitar2Name.setText (slot (s.guitar2Profile), juce::dontSendNotification);
        chPianoName.setText   (slot (s.pianoProfile),   juce::dontSendNotification);

        const juce::String forPart[5] = { s.drumProfile, s.bassProfile, s.guitarProfile,
                                          s.pianoProfile, s.guitar2Profile };
        const int chosen = juce::jlimit (0, 4, learnPart.getSelectedId() - 1);
        learnPartName.setText (slot (forPart[chosen]), juce::dontSendNotification);

        for (juce::Label* l : { &chDrumsName, &chBassName, &chGuitarName,
                                &chGuitar2Name, &chPianoName })
            l->setColour (juce::Label::textColourId,
                          l->getText().startsWith ("-") ? ghost::dim : ghost::text);
    }

    if (s.guitarProfile.isNotEmpty())  profiles += "    gtr: "  + s.guitarProfile;
    if (s.guitar2Profile.isNotEmpty()) profiles += "    gtr2: " + s.guitar2Profile;
    if (s.pianoProfile.isNotEmpty())   profiles += "    piano: " + s.pianoProfile;
    profilesLabel.setText (profiles, juce::dontSendNotification);

    complexitySlider.setValue (processor.complexity.load(), juce::dontSendNotification);
    humanizeSlider.setValue (processor.humanize.load(), juce::dontSendNotification);
    fillsSlider.setValue (processor.fills.load(), juce::dontSendNotification);
    seedEditor.setText (juce::String (processor.seed.load()), juce::dontSendNotification);

    // Not while it has focus, or the refresh overwrites what is being typed.
    if (! bpmEditor.hasKeyboardFocus (true))
        bpmEditor.setText (juce::String (juce::roundToInt (processor.getPlanBpm())),
                           juce::dontSendNotification);

    keyBox.setSelectedId (processor.getKeyPitchClass() + 1, juce::dontSendNotification);
    styleBox.setSelectedId (styleNameToId (processor.getStyle()), juce::dontSendNotification);
    tuningBox.setSelectedId (tuningNameToId (processor.getBassTuning()), juce::dontSendNotification);

    const juce::String modeText = processor.getMode().replace ("_", " ");
    for (int i = 1; i <= modeBox.getNumItems(); ++i)
        if (modeBox.getItemText (i - 1) == modeText)
            modeBox.setSelectedId (i, juce::dontSendNotification);

    sectionList.setSections (processor.getSections());
    arrangement.setSections (processor.getSections());
    tracker.setSections (processor.getSections());
    lastTrackerTick = -1;          // the song changed; the window must be refilled
    refreshTracker();
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
    GB_WORK ("paint window");
    // A gradient rather than a flat fill. See ghost::fillBackground.
    ghost::fillBackground (g, getLocalBounds().toFloat());

    // ---- header ----
    // Flat, generous, and separated by a single gradient hairline rather than a
    // bevel. The gradient is the one visual signature; it appears here and on
    // anything active, and nowhere else.
    auto header = getLocalBounds().removeFromTop (62).toFloat();

    auto title = header.reduced (20.0f, 0.0f);

    g.setColour (ghost::colours::text);
    g.setFont (juce::Font (juce::FontOptions (29.0f).withStyle ("Bold")));
    g.drawText ("GHOSTBAND", title.withTrimmedBottom (22.0f).toNearestInt(),
                juce::Justification::centredLeft);

    g.setColour (ghost::colours::dim);
    g.setFont (juce::Font (juce::FontOptions (15.0f)));
    g.drawText ("MIDI BRAIN   -   DRUMS   BASS   GUITAR x2   PIANO",
                title.withTrimmedTop (36.0f).toNearestInt(),
                juce::Justification::centredLeft);

    // Transport dot.
    const bool running = processor.transportRunning.load();
    const auto lamp = juce::Rectangle<float> (8.0f, 8.0f)
                          .withCentre ({ header.getRight() - 24.0f, header.getCentreY() });
    ghost::drawLamp (g, lamp, running);

    g.setColour (running ? ghost::colours::red : ghost::colours::dim);
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    g.drawText (running ? "RUNNING" : "IDLE",
                juce::Rectangle<int> (static_cast<int> (header.getRight()) - 110,
                                      static_cast<int> (header.getCentreY()) - 6, 76, 12),
                juce::Justification::centredRight);

    // The panel the song controls sit on. Painted here rather than by a child
    // component so it lands BEHIND them - paint() runs before children do.
    if (screen == Screen::Song && ! controlsPanel.isEmpty())
        ghost::drawSurface (g, controlsPanel.toFloat(), 10.0f);

    // The footer's own separator. Without it the two lines down there read as
    // more page rather than as a distinct block - which was the complaint: no
    // obvious separation other than the colour.
    if (! footerRule.isEmpty())
    {
        g.setColour (ghost::line);
        g.fillRect (footerRule);
    }

    // The header's edge. It used to be a 1.5px bar of full-strength accent
    // gradient across the whole window, which is the loudest thing on the
    // screen and the first thing that reads as homemade. A hairline of the same
    // gradient at a third of its strength keeps the signature and stops it
    // shouting - the accent should appear where something is ACTIVE, not as
    // trim.
    const auto rule = juce::Rectangle<float> (header.getX(), header.getBottom() - 1.0f,
                                              header.getWidth(), 1.0f);
    g.setGradientFill (ghost::accentGradient (rule));
    g.setOpacity (0.38f);
    g.fillRect (rule);
    g.setOpacity (1.0f);

    // ---- footer ----
    auto footer = getLocalBounds().removeFromBottom (38).toFloat();
    g.setColour (ghost::colours::line);
    g.fillRect (footer.withHeight (1.0f));

    if (screen == Screen::About)
        paintAbout (g, getLocalBounds().withTrimmedTop (62).withTrimmedBottom (38).reduced (20, 14));
}

void GhostbandEditor::paintAbout (juce::Graphics& g, juce::Rectangle<int> area)
{
    // Every block is measured before it is drawn.
    //
    // This screen used to hand drawFittedText a fixed height per paragraph and
    // hope. When the text needed more lines than the box had room for, JUCE drew
    // them on top of each other - two paragraphs overlapping into an unreadable
    // smear, with a screen of empty space underneath. Laying each block out and
    // asking how tall it came out is the only version of this that cannot go
    // wrong when a font, a window size or a sentence changes.
   #ifndef GHOSTBAND_VERSION
    #define GHOSTBAND_VERSION "dev"
   #endif

    auto a = area.reduced (4, 0);
    const float width = static_cast<float> (a.getWidth());
    int y = a.getY();

    const auto block = [&] (const juce::String& text, juce::Font font,
                            juce::Colour colour, int spaceAfter)
    {
        juce::AttributedString as;
        as.setLineSpacing (3.0f);
        as.append (text, font, colour);

        juce::TextLayout layout;
        layout.createLayout (as, width);
        layout.draw (g, juce::Rectangle<float> (static_cast<float> (a.getX()),
                                                static_cast<float> (y),
                                                width, layout.getHeight()));
        y += static_cast<int> (layout.getHeight()) + spaceAfter;
    };

    // The header band already says GHOSTBAND, so the hero line here says what it
    // is instead of saying the name twice.
    block ("A MIDI brain for the instruments you already own.",
           juce::Font (juce::FontOptions (23.0f).withStyle ("Bold")),
           ghost::colours::text, 6);

    block ("Version " GHOSTBAND_VERSION "   ::   by Kyle Yeroshefsky",
           juce::Font (juce::FontOptions (16.0f)),
           ghost::colours::silver, 16);

    {
        const auto rule = juce::Rectangle<float> (static_cast<float> (a.getX()),
                                                  static_cast<float> (y), width, 2.0f);
        g.setGradientFill (ghost::accentGradient (rule));
        g.fillRect (rule);
        y += 20;
    }

    block ("Ghostband makes no sound of its own. It writes an arrangement - drums, "
           "bass, two guitars and piano - and performs it through the instruments you "
           "already own, by sending them MIDI.",
           juce::Font (juce::FontOptions (17.5f)), ghost::colours::text, 12);

    block ("You own the song: its key, tempo, style, and the order of its sections. "
           "Ghostband fills in the playing. Every part is generated from a seed, so "
           "the same song always comes back exactly as you left it - and rerolling "
           "one section never disturbs another.",
           juce::Font (juce::FontOptions (17.5f)), ghost::colours::text, 22);

    // A small labelled block, which reads as specification rather than prose.
    {
        const juce::Font label (juce::FontOptions (14.5f).withStyle ("Bold"));
        const juce::Font value (juce::FontOptions (16.0f));

        struct Row { const char* label; juce::String value; };
        const Row rows[] = {
            { "LICENCE",  "GNU AGPLv3 - free software, and free of charge" },
            { "BUILT ON", "JUCE" },
            { "CONTACT",  "mourning.grace.2014@gmail.com" },
        };

        for (const Row& row : rows)
        {
            const int rowY = y;

            g.setColour (ghost::colours::dim);
            g.setFont (label);
            g.drawText (row.label, a.getX(), rowY, 78, 18, juce::Justification::centredLeft);

            g.setColour (ghost::colours::silver);
            g.setFont (value);
            g.drawText (row.value, a.getX() + 84, rowY,
                        a.getWidth() - 84, 18, juce::Justification::centredLeft);

            y = rowY + 22;
        }

        y += 6;
    }

    block ("The donate button is a button, not a nag. Nothing is gated behind it and "
           "nothing ever will be.",
           juce::Font (juce::FontOptions (15.0f)), ghost::colours::dim, 0);
}

void GhostbandEditor::resized()
{
    // Repaint the whole canvas, not just whatever the resize newly exposed.
    //
    // Every other screen is built from child components, and a component that
    // is moved or resized redraws itself - so those screens heal on their own
    // and this was never needed. The About screen is painted straight onto the
    // canvas, and its text is wrapped to the width it was painted at. Growing
    // the window redrew only the newly uncovered strip, which left the old
    // narrow wrap sitting underneath the new wide one: two layouts of the same
    // paragraphs on top of each other, each with pieces of the other missing.
    // That is what "the about section is still botched" was, and it is why
    // fixing the measurement last time did not fix it.
    //
    // Neither the overlap checker nor the snapshots can see this: both render
    // into a fresh image, which is a full repaint by definition. It only exists
    // on a screen that has already been painted at another size.
    repaint();

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

    // Immediately left of the donate button, right-aligned against it, so the
    // reading sits in the bottom right corner on every screen and never moves.
    footerRail.removeFromRight (12);
    // Wide enough for the latency, the buffer AND a stall note - the note only
    // appears when something went wrong, and a reading that gets truncated
    // exactly when it has something to say would be worse than not having it.
    latencyLabel.setBounds (footerRail.removeFromRight (juce::jmin (340, footerRail.getWidth())));

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
        // The only screen with no status block, so it must clear the rule that
        // sits above one. layOutFooter is what sets footerRule, About never
        // calls it, and the rectangle therefore kept whatever the LAST screen
        // laid out - a hairline ruled across the middle of the About text at
        // whatever height the previous screen's footer happened to be. Painted
        // straight onto the canvas, so no child-component check could see it;
        // found by looking at a rendered snapshot.
        footerRule = {};

        auto a = r;
        auto buttons = a.removeFromBottom (30);
        manualButton.setBounds (buttons.removeFromLeft (118));
        buttons.removeFromLeft (8);
        repoButton.setBounds (buttons.removeFromLeft (118));
        buttons.removeFromLeft (8);
        emailButton.setBounds (buttons.removeFromLeft (118));
        return;
    }

    if (screen == Screen::Settings)
    {
        // TWO COLUMNS, and the reason is a bug rather than a preference.
        //
        // This was one narrow column of rows in a window 1180 wide, so the
        // instrument names were given 900 pixels to say "MODO Bass 2" while the
        // control list at the bottom was squeezed to sixty pixels. Then the
        // window got shorter and the squeeze became a fault: the rows under the
        // list were taken with
        //
        //     removeFromBottom (jmin (74, jmax (0, s.getHeight() - 60)))
        //
        // which protects the LIST by shrinking the CONTROLS, and at 820 tall it
        // handed the theme picker a rectangle eight pixels high. That is the
        // same fault, in the same place, as the one whose comment sits three
        // lines above it - a list can scroll, a combo box cannot, so when the
        // two compete for the last pixels the list must always be the one that
        // gives. Both columns below reserve their fixed rows first.
        layOutFooter (r.removeFromBottom (58));
        r.removeFromBottom (10);

        settingsHeading.setBounds (r.removeFromTop (24));
        r.removeFromTop (12);

        // The left column is a FIXED width, not half the window. Half looked
        // reasonable and broke at the minimum size: the right column came out
        // 406 wide, the learn form needs about 470, and removeFromLeft on an
        // exhausted rectangle returns zero-width pieces at the same x - so
        // three controls stacked on top of each other and a label came out two
        // pixels wide. A column with a job has a size; it does not get a
        // fraction and hope.
        auto left = r.removeFromLeft (460);
        r.removeFromLeft (24);
        auto right = r;

        // ---- left: the channels, and the things that are set once ----
        {
            // Fixed rows off the bottom BEFORE anything above them is measured.
            auto bottom = left.removeFromBottom (66);

            juce::ComboBox* boxes[5]  = { &chDrums, &chBass, &chGuitar, &chPiano, &chGuitar2 };
            juce::Label*    labels[5] = { &chDrumsLabel, &chBassLabel, &chGuitarLabel,
                                          &chPianoLabel, &chGuitar2Label };
            juce::TextButton* tests[5] = { &testDrums, &testBass, &testGuitar,
                                           &testPiano, &testGuitar2 };
            juce::Label* names[5] = { &chDrumsName, &chBassName, &chGuitarName,
                                      &chPianoName, &chGuitar2Name };

            for (int i = 0; i < 5; ++i)
            {
                auto row = left.removeFromTop (28);
                labels[i]->setBounds (row.removeFromLeft (70));
                boxes[i]->setBounds (row.removeFromLeft (78));
                row.removeFromLeft (10);
                tests[i]->setBounds (row.removeFromLeft (66));
                row.removeFromLeft (14);
                names[i]->setBounds (row);
                left.removeFromTop (6);
            }

            left.removeFromTop (6);
            // Capped rather than given everything left over: a Label centres
            // its text vertically, so a paragraph handed three hundred pixels
            // floats in the middle of them with a gap above and below that
            // reads as a layout mistake.
            channelsHelp.setBounds (left.removeFromTop (juce::jmin (72, left.getHeight())));

            // Two rows of two, not one row of three plus the picker. Three
            // buttons side by side need 476 and this column is 460 - and the
            // widest thing in a column is what sets the column's width, so the
            // choice was wrap them or make every other row wider than it needs
            // to be.
            auto themeRow = bottom.removeFromTop (28);
            themeLabel.setBounds (themeRow.removeFromLeft (60));
            themeBox.setBounds (themeRow.removeFromLeft (150));
            themeRow.removeFromLeft (10);
            tempoModeButton.setBounds (themeRow.removeFromLeft (140));

            bottom.removeFromTop (10);
            auto row = bottom.removeFromTop (28);
            reloadProfilesBtn.setBounds (row.removeFromLeft (170));
            row.removeFromLeft (8);
            resetSizeButton.setBounds (row.removeFromLeft (150));
        }

        // ---- right: teaching Ghostband an instrument's own knobs ----
        {
            learnHeading.setBounds (right.removeFromTop (16));
            right.removeFromTop (4);

            // Four lines, and one of them is live - it names the controller the
            // selected part's mix knob actually reaches.
            learnHelp.setBounds (right.removeFromTop (92));
            right.removeFromTop (6);

            auto learnRow = right.removeFromTop (28);
            learnPart.setBounds (learnRow.removeFromLeft (96));
            learnRow.removeFromLeft (10);
            ctlAdd.setBounds (learnRow.removeFromLeft (70));
            learnRow.removeFromLeft (6);
            ctlRemove.setBounds (learnRow.removeFromLeft (78));
            learnRow.removeFromLeft (6);
            ctlSave.setBounds (learnRow.removeFromLeft (124));
            learnRow.removeFromLeft (14);
            learnPartName.setBounds (learnRow);

            right.removeFromTop (8);
            auto editRow = right.removeFromTop (26);
            ctlNameLabel.setBounds (editRow.removeFromLeft (42));
            ctlName.setBounds (editRow.removeFromLeft (130));
            editRow.removeFromLeft (10);
            ctlFollowsLabel.setBounds (editRow.removeFromLeft (54));
            ctlFollows.setBounds (editRow.removeFromLeft (98));
            editRow.removeFromLeft (10);
            ctlTypeLabel.setBounds (editRow.removeFromLeft (36));
            ctlType.setBounds (editRow.removeFromLeft (84));

            right.removeFromTop (8);
            auto teachRow = right.removeFromTop (28);
            ctlTeach.setBounds (teachRow.removeFromLeft (170));
            teachRow.removeFromLeft (16);
            ctlPositionsLabel.setBounds (teachRow.removeFromLeft (54));
            ctlPositions.setBounds (teachRow.removeFromLeft (52).withSizeKeepingCentre (52, 26));
            teachRow.removeFromLeft (8);
            ctlWalk.setBounds (teachRow.removeFromLeft (112));
            teachRow.removeFromLeft (8);
            ctlPositionsHint.setBounds (teachRow);

            if (ctlValue.isVisible() || ctlFrom.isVisible())
            {
                right.removeFromTop (6);
                auto valueRow = right.removeFromTop (26);
                valueRow.removeFromLeft (186);      // line up under the choices box

                if (ctlValue.isVisible())
                {
                    ctlValueLabel.setBounds (valueRow.removeFromLeft (54));
                    ctlValue.setBounds (valueRow.removeFromLeft (52).withSizeKeepingCentre (52, 26));
                    valueRow.removeFromLeft (8);
                    ctlSend.setBounds (valueRow.removeFromLeft (72));
                    valueRow.removeFromLeft (8);
                    ctlValueHint.setBounds (valueRow);
                }
                else
                {
                    ctlRangeLabel.setBounds (valueRow.removeFromLeft (54));
                    ctlFrom.setBounds (valueRow.removeFromLeft (48).withSizeKeepingCentre (48, 26));
                    ctlRangeToLabel.setBounds (valueRow.removeFromLeft (24));
                    ctlTo.setBounds (valueRow.removeFromLeft (48).withSizeKeepingCentre (48, 26));
                    valueRow.removeFromLeft (10);
                    ctlRangeHint.setBounds (valueRow);
                }
            }

            right.removeFromTop (10);

            // Everything left over, and there is a lot of it now - the list was
            // sixty pixels tall in one column and is several hundred in two.
            ctlViewport.setBounds (right);
            ctlList.setSize (juce::jmax (100, right.getWidth() - 10), ctlList.getHeight());
        }
        return;
    }

    // The song screen is laid out around a RAIL and does not use planRow, so it
    // takes its area whole rather than having a row cut off the top of it.
    if (screen == Screen::Song)
    {
        layOutSongScreen (r);
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

        r.removeFromTop (12);
        layOutFooter (r.removeFromBottom (58));
        r.removeFromBottom (10);

        // THE FORM IS A RAIL, for the same two reasons the song screen's
        // controls are.
        //
        // Across the top, its six rows ended at six different x positions -
        // 204, 302, 442, 552, 780, 1160 - because each row was as wide as its
        // own contents happened to be. Nobody reads that as six coincidences;
        // they read it as crooked, and "things seem cock eyed" is exactly what
        // was reported. A rail gives every row the same right edge for free.
        //
        // And the arrangement it edits was getting whatever height was left
        // under the form, which at 820 tall is not much. Beside it, the list
        // gets the whole window.
        auto rail = r.removeFromLeft (kRailWidth);
        r.removeFromLeft (18);

        auto row = rail.removeFromTop (24);
        edNameLabel.setBounds (row.removeFromLeft (44));
        edName.setBounds (row);

        rail.removeFromTop (6);
        row = rail.removeFromTop (24);
        edBarsLabel.setBounds (row.removeFromLeft (44));
        edBars.setBounds (row.removeFromLeft (60));

        rail.removeFromTop (6);
        row = rail.removeFromTop (24);
        edIntensityLabel.setBounds (row.removeFromLeft (76));
        edIntensity.setBounds (row);

        rail.removeFromTop (6);
        row = rail.removeFromTop (24);
        edFeelLabel.setBounds (row.removeFromLeft (40));
        edFeel.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (12);
        edFillLabel.setBounds (row.removeFromLeft (34));
        edFill.setBounds (row.removeFromLeft (86));

        rail.removeFromTop (6);
        row = rail.removeFromTop (24);
        edLeadLabel.setBounds (row.removeFromLeft (40));
        edLead.setBounds (row.removeFromLeft (110));

        rail.removeFromTop (6);
        row = rail.removeFromTop (24);
        edChordsLabel.setBounds (row.removeFromLeft (60));
        edChords.setBounds (row);

        // Two per line, because the toggles carry words and the words are what
        // makes them readable. Wide enough for them: a toggle draws a pill and
        // then its label in what is left, and at 74 "drums" came out "dru...",
        // which read that way in every theme.
        rail.removeFromTop (8);
        row = rail.removeFromTop (24);
        edPlaysLabel.setBounds (row.removeFromLeft (50));
        edDrums.setBounds (row.removeFromLeft (100));
        edBass.setBounds  (row.removeFromLeft (86));

        rail.removeFromTop (4);
        row = rail.removeFromTop (24);
        row.removeFromLeft (50);
        edGuitar.setBounds  (row.removeFromLeft (96));
        edGuitar2.setBounds (row.removeFromLeft (110));

        rail.removeFromTop (4);
        row = rail.removeFromTop (24);
        row.removeFromLeft (50);
        edPiano.setBounds (row.removeFromLeft (90));

        rail.removeFromTop (12);
        row = rail.removeFromTop (26);
        edAddButton.setBounds (row.removeFromLeft (66));
        row.removeFromLeft (6);
        edDeleteButton.setBounds (row.removeFromLeft (70));
        row.removeFromLeft (14);
        edUpButton.setBounds (row.removeFromLeft (54));
        row.removeFromLeft (6);
        edDownButton.setBounds (row.removeFromLeft (60));

        viewport.setBounds (r);

        // At least as tall as the viewport, so the card the rows sit on reaches
        // the bottom instead of stopping under the last section and leaving a
        // slab of background that reads as the list having failed to draw. The
        // takes list has done this since it was written; beside a rail there is
        // far more empty space below a short arrangement, so it matters here
        // now too.
        sectionList.setSize (r.getWidth() - 10,
                             juce::jmax (r.getHeight(), sectionList.getHeight()));
        return;
    }

    if (screen == Screen::Takes)
    {
        tkDoneButton.setBounds (planRow.removeFromLeft (72));
        planRow.removeFromLeft (12);
        tkHeading.setBounds (planRow);

        r.removeFromTop (8);
        tkHelp.setBounds (r.removeFromTop (56));

        r.removeFromTop (10);
        auto nameRow = r.removeFromTop (28);
        tkNameLabel.setBounds (nameRow.removeFromLeft (52));
        tkName.setBounds (nameRow.removeFromLeft (juce::jmin (300, nameRow.getWidth() - 8)));
        nameRow.removeFromLeft (10);
        tkSaveButton.setBounds (nameRow.removeFromLeft (juce::jmin (110, nameRow.getWidth())));

        r.removeFromTop (10);
        auto actionRow = r.removeFromTop (26);
        tkRecall.setBounds (actionRow.removeFromLeft (86));
        actionRow.removeFromLeft (6);
        tkDelete.setBounds (actionRow.removeFromLeft (78));
        actionRow.removeFromLeft (14);
        tkResult.setBounds (actionRow);

        r.removeFromTop (10);
        layOutFooter (r.removeFromBottom (58));
        r.removeFromBottom (8);

        tkViewport.setBounds (r);

        // At least as tall as the viewport, so the card the rows sit on reaches
        // the bottom of the screen instead of stopping under the last row and
        // leaving a slab of background that reads as the list having failed to
        // draw. refreshTakes calls resized() after setRows, which is what keeps
        // this true as the library grows.
        tkList.setSize (r.getWidth() - 10, juce::jmax (r.getHeight(), tkList.getHeight()));
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
        layOutFooter (r.removeFromBottom (58));
        r.removeFromBottom (8);

        calViewport.setBounds (r);
        calList.setSize (r.getWidth() - 10, calList.getHeight());
        return;
    }

}

//==============================================================================
// The song screen: a RAIL down the left, and the grid taking everything else.
//
// What this replaces stacked every control across the top of a portrait window
// and gave the tracker whatever was left, which was 48% of the height - less
// than half the window spent on the one thing you actually watch. Worse, the
// controls only reached about 55% of the way across, so the right-hand
// two-fifths of every control row was empty. Measured rather than felt: see
// the --audit mode in the harness.
//
// A rail fixes both at once and buys a third thing that is easy to miss. When
// the window is resized, ONLY THE GRID CHANGES SIZE. Every control stays
// exactly where your hand left it, at every window size, which is the
// difference between a tool and a page.
void GhostbandEditor::layOutSongScreen (juce::Rectangle<int> r)
{
    // The status block spans the whole window rather than the rail: it is two
    // lines of prose naming five instruments, and 320 pixels would clip it.
    layOutFooter (r.removeFromBottom (58));
    r.removeFromBottom (10);

    auto rail = r.removeFromLeft (kRailWidth);
    r.removeFromLeft (18);                       // gutter between rail and grid

    // The rail sits on one surface, painted in paint() behind the controls.
    controlsPanel = rail.expanded (10, 10);

    // ---- what song this is, and what to do with it ----
    {
        auto row = rail.removeFromTop (28);
        loadButton.setBounds (row.removeFromLeft (128));
        row.removeFromLeft (8);
        reloadButton.setBounds (row.removeFromLeft (86));

        rail.removeFromTop (6);
        row = rail.removeFromTop (28);
        takesButton.setBounds (row.removeFromLeft (84));
        row.removeFromLeft (6);
        editButton.setBounds (row.removeFromLeft (104));
        row.removeFromLeft (6);
        calibrateButton.setBounds (row.removeFromLeft (104));

        rail.removeFromTop (10);
        planLabel.setBounds (rail.removeFromTop (22));
        rail.removeFromTop (10);
    }

    // ---- the song ----
    {
        auto row = rail.removeFromTop (26);
        keyLabel.setBounds (row.removeFromLeft (32));
        keyBox.setBounds (row.removeFromLeft (84));
        row.removeFromLeft (12);
        modeLabel.setBounds (row.removeFromLeft (42));
        modeBox.setBounds (row.removeFromLeft (140));

        rail.removeFromTop (7);
        row = rail.removeFromTop (26);
        styleLabel.setBounds (row.removeFromLeft (44));
        styleBox.setBounds (row.removeFromLeft (132));
        row.removeFromLeft (12);
        zoomLabel.setBounds (row.removeFromLeft (44));
        zoomBox.setBounds (row.removeFromLeft (84));

        rail.removeFromTop (7);
        row = rail.removeFromTop (26);
        tuningLabel.setBounds (row.removeFromLeft (80));
        tuningBox.setBounds (row.removeFromLeft (106));
        row.removeFromLeft (10);
        tempoLabel.setBounds (row);
    }

    rail.removeFromTop (14);

    // ---- the three feel dials, centred in the rail ----
    {
        const int knobW = 80, gap = 8;
        auto row = rail.removeFromTop (76)
                       .withSizeKeepingCentre (3 * knobW + 2 * gap, 76);

        juce::Slider* dials[3] = { &complexitySlider, &humanizeSlider, &fillsSlider };
        juce::Label*  names[3] = { &complexityLabel,  &humanizeLabel,  &fillsLabel  };

        for (int i = 0; i < 3; ++i)
        {
            auto cell = row.removeFromLeft (knobW);
            names[i]->setBounds (cell.removeFromTop (12));
            dials[i]->setBounds (cell);
            row.removeFromLeft (gap);
        }
    }

    rail.removeFromTop (12);

    // ---- seed, roll, transport ----
    {
        seedLabel.setBounds (rail.removeFromTop (12));
        auto row = rail.removeFromTop (26);
        seedEditor.setBounds (row.removeFromLeft (96));
        row.removeFromLeft (8);
        rollButton.setBounds (row);

        rail.removeFromTop (8);
        row = rail.removeFromTop (26);
        playPauseButton.setBounds (row.removeFromLeft (84));
        row.removeFromLeft (14);
        bpmLabel.setBounds (row.removeFromLeft (34));
        bpmEditor.setBounds (row.removeFromLeft (62));
    }

    rail.removeFromTop (16);

    // ---- the mix ----
    //
    // One knob per ROW here rather than five across, and that is the rail
    // paying for itself. Across the top there was room for a five-character
    // label, so a knob that could not work had nowhere to say why. Down the
    // side each one gets a whole line, and "PIANO - not in song" fits as
    // written.
    mixLabel.setBounds (rail.removeFromTop (16));
    rail.removeFromTop (4);

    juce::Slider* levelSliders[5] = { &levelDrums, &levelBass, &levelGuitar,
                                      &levelGuitar2, &levelPiano };
    juce::Label*  levelLabels[5]  = { &levelDrumsLabel, &levelBassLabel,
                                      &levelGuitarLabel, &levelGuitar2Label,
                                      &levelPianoLabel };
    static const char* kLevelNames[5] = { "DRUMS", "BASS", "GTR", "GTR 2", "PIANO" };

    // Part order here is drums, bass, guitar, guitar 2, piano; the processor
    // indexes guitar 2 as 4, so the lookup is not the loop counter.
    static const int partForKnob[5] = { 0, 1, 2, 4, 3 };

    // ALL FIVE, ALWAYS, IN THE SAME PLACES.
    //
    // These used to be laid out only for a part that could actually be reached,
    // on the reasoning that a control which does nothing is worse than no
    // control at all. That reasoning is half right and the half it gets wrong
    // costs more: a knob that is simply absent is indistinguishable from a knob
    // that is broken. Loading a song with no piano and finding the PIANO knob
    // gone reads as a bug, and worse, it makes every other control suspect -
    // "what else is missing that I have not noticed?" is not a question a
    // person should have to ask about their own mixer.
    for (int i = 0; i < 5; ++i)
    {
        const int part = partForKnob[i];

        // Only the song screen positions these, so only it may show them -
        // resized() runs last after a screen change and would otherwise unhide
        // them over whatever the new screen has drawn there.
        levelSliders[i]->setVisible (screen == Screen::Song);
        levelLabels[i]->setVisible  (screen == Screen::Song);

        const bool inSong    = processor.partIsInSong (part);
        const bool reachable = inSong && processor.partVolumeReachable (part);

        // A knob with no control following "level" still sends CC 7, which some
        // plugins answer and most ignore. That is a guess, not a connection, so
        // it is drawn as one - dimmed, with the label saying CC 7 outright.
        // Shreddage sat in exactly this state and looked identical to a knob
        // that worked, which is how it went unnoticed through two sessions.
        const bool taught = reachable && processor.levelIsTaught (part);

        juce::String suffix;
        juce::Colour colour = ghost::dim;
        float alpha = 1.0f;

        if (! inSong)
        {
            suffix = " " + midDot() + " not in song";
            colour = ghost::dim.withMultipliedAlpha (0.6f);
            alpha  = 0.3f;
        }
        else if (! reachable)
        {
            // The instrument's own volume cannot be addressed from outside at
            // all - SSD5 is the case this exists for. Not a fault to fix here;
            // put a gain plugin after it in the host.
            suffix = " " + midDot() + " no reach";
            colour = ghost::dim.withMultipliedAlpha (0.6f);
            alpha  = 0.3f;
        }
        else if (! taught)
        {
            suffix = " " + midDot() + " CC7";
            colour = ghost::warn;
            alpha  = 0.55f;
        }

        levelSliders[i]->setEnabled (reachable);
        levelSliders[i]->setAlpha (alpha);
        levelLabels[i]->setColour (juce::Label::textColourId, colour);
        levelLabels[i]->setText (juce::String (kLevelNames[i]) + suffix,
                                 juce::dontSendNotification);

        // Left-aligned beside its knob now, not centred over it.
        levelLabels[i]->setJustificationType (juce::Justification::centredLeft);

        // Why this one looks the way it does, on the knob itself, because even
        // a whole line has room for a statement and not for a remedy.
        levelSliders[i]->setTooltip (
            ! inSong    ? juce::String (kLevelNames[i]).toLowerCase()
                              + " is not in this song, so there is nothing to set a level for. "
                                "Load a song that has one, or add the part in Edit song."
          : ! reachable ? "This instrument has no volume Ghostband can reach - nothing outside "
                          "it can address its level. Put a gain plugin after it in your host "
                          "instead."
          : ! taught    ? juce::String (kLevelNames[i])
                              + " level. Nothing is taught for this part, so the knob is guessing "
                                "with CC 7, which most instruments ignore. Teach its volume "
                                "control in Settings and this knob becomes real."
                        : juce::String (kLevelNames[i])
                              + " level, sent to the instrument's own volume control.");

        auto row = rail.removeFromTop (34);
        levelSliders[i]->setBounds (row.removeFromLeft (34).reduced (1));
        row.removeFromLeft (10);
        levelLabels[i]->setBounds (row);
    }

    // ---- the grid, and the two lines that frame it ----
    {
        auto head = r.removeFromTop (18);
        headlineLabel.setBounds (head.removeFromLeft (head.getWidth() / 2));
        summaryLabel.setBounds (head);
        r.removeFromTop (10);

        auto foot = r.removeFromBottom (16);
        rollHintLabel.setBounds (foot.removeFromRight (juce::jmin (280, foot.getWidth() / 2)));
        transportLabel.setBounds (foot);
        r.removeFromBottom (8);
    }

    arrangement.setBounds (r);
    tracker.setBounds (r);
    refreshTracker();
}
