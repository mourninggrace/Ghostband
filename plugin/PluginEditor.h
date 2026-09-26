#pragma once

#include "PluginProcessor.h"

#include "GhostbandLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ghost
{
    // The palette lives in GhostbandLookAndFeel.h. These are the names the rest
    // of the editor already used, kept so the drawing code reads the same.
    //
    // REFERENCES, not copies, and that distinction is the whole theme feature.
    // These were const values initialised from the palette, so they took a
    // snapshot at program start and every one of the hundred and forty-nine
    // call sites below would have gone on drawing the first theme forever.
    inline juce::Colour& background = colours::background;
    inline juce::Colour& panel      = colours::panel;
    inline juce::Colour& line       = colours::line;
    inline juce::Colour& text       = colours::text;
    inline juce::Colour& dim        = colours::dim;
    inline juce::Colour& silver     = colours::silver;
    inline juce::Colour& accent     = colours::accent;
    inline juce::Colour& warn       = colours::warn;
}

//==============================================================================
// One number easing towards another.
//
// The whole of the animation system, because the thing that makes motion read
// as expensive is not what moves - it is the CURVE and the LENGTH. Linear
// motion looks mechanical at any duration; 400 ms of anything looks slow; and
// a thing that animates while you are dragging it feels broken. So: one eased
// value, a short default, and callers that snap rather than animate when the
// change is the user's own hand.
struct Eased
{
    // No motion at all. For setting a starting point, and for any change the
    // user made themselves - a knob should follow the mouse exactly.
    void set (float v) noexcept       { current = from = target = v; moving = false; }

    void moveTo (float v, int ms) noexcept
    {
        if (std::abs (v - target) < 0.0001f && moving) return;   // already going there
        if (std::abs (v - current) < 0.0001f) { set (v); return; }

        from      = current;
        target    = v;
        elapsedMs = 0;
        durationMs = juce::jmax (1, ms);
        moving    = true;
    }

    // Advances by one frame. Returns true while there is still motion left, so
    // the clock driving it knows when it can stop.
    bool advance (int deltaMs) noexcept
    {
        if (! moving) return false;

        elapsedMs += deltaMs;
        if (elapsedMs >= durationMs) { set (target); return false; }

        const float t = static_cast<float> (elapsedMs) / static_cast<float> (durationMs);

        // Cubic ease-out: fast at the start, settling at the end. The one curve
        // that reads as a thing ARRIVING rather than being dragged - which is
        // what almost every interface movement is.
        const float e = 1.0f - std::pow (1.0f - t, 3.0f);

        current = from + (target - from) * e;
        return true;
    }

    float value() const noexcept { return current; }
    bool  busy()  const noexcept { return moving; }

private:
    float current = 0.0f, from = 0.0f, target = 0.0f;
    int   elapsedMs = 0, durationMs = 1;
    bool  moving = false;
};

// Drives every Eased in the window, and RUNS ONLY WHILE SOMETHING IS MOVING.
//
// The editor already has a 30 Hz timer doing data refresh, and animation wants
// 60 to look smooth. Raising that one would double the cost of reading the
// sequence for the grid thirty times a second in order to make a 160 ms fade
// look better, which is the wrong trade every time nothing is fading.
//
// So this is separate, it starts when something is given a target, and it stops
// itself the moment everything has arrived. At rest it costs nothing at all.
class AnimationClock : public juce::Timer
{
public:
    explicit AnimationClock (std::function<bool (int)> step) : advanceAll (std::move (step)) {}

    void wake()
    {
        if (! isTimerRunning())
        {
            lastMs = juce::Time::getMillisecondCounter();
            startTimerHz (60);
        }
    }

    void timerCallback() override
    {
        const juce::uint32 now = juce::Time::getMillisecondCounter();

        // Measured rather than assumed. A timer that misses its slot - which on
        // this owner's machine happens for whole seconds at a time, and not
        // because of us - would otherwise animate in slow motion afterwards.
        const int delta = juce::jlimit (1, 100, static_cast<int> (now - lastMs));
        lastMs = now;

        if (! advanceAll (delta))
            stopTimer();
    }

private:
    std::function<bool (int)> advanceAll;
    juce::uint32 lastMs = 0;
};

// A screen change, drawn.
//
// Covers the content area in the background colour and fades out, so the new
// screen emerges rather than replacing the old one between two frames. A true
// cross-fade would need a snapshot of the outgoing screen held in an image; at
// 160 ms nobody can tell the difference, and this costs one filled rectangle.
class ScreenVeil : public juce::Component
{
public:
    ScreenVeil() { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        const float a = alpha.value();
        if (a <= 0.002f) return;

        g.setColour (ghost::colours::background.withAlpha (juce::jlimit (0.0f, 1.0f, a)));
        g.fillAll();
    }

    Eased alpha;
};

// A die, and it tumbles.
//
// Drawn rather than an image so it takes the theme's colours like everything
// else, and because a die is six dots in a square - there is nothing an image
// would add except a file to keep in step with ten palettes.
//
// The tumble is the one place in this interface where motion is decoration
// rather than explanation, and that is the point of it: the dice exists for
// fun, and a fun control that does not move is a button with a picture on it.
class DiceButton : public juce::Button
{
public:
    DiceButton() : juce::Button ("Dice") {}

    void paintButton (juce::Graphics& g, bool hovered, bool down) override;

    // Its own, because a Button has no right-click callback and adding a mouse
    // listener for one control would put a mouseDown on the whole editor.
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onRightClick) onRightClick();
            return;
        }

        juce::Button::mouseDown (e);
    }

    std::function<void()> onRightClick;

    // 0 at rest. While rolling it winds up past 1, and the face shown is chosen
    // from it - so the die changes number as it turns rather than cutting to
    // its answer at the end.
    Eased tumble;

    // What it lands on. Set when the roll finishes so the face left on screen
    // is the one it stopped at rather than whatever the last frame computed.
    int face = 5;

private:
    void drawPips (juce::Graphics& g, juce::Rectangle<float> r, int pips,
                   juce::Colour c) const;
};

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
    void setSelection (const std::vector<int>& indices);
    int  tickToY (int tick) const;

    // Plain click jumps to a section; ctrl-click adds it to the reroll
    // selection. Two gestures on one list, but jumping is by far the more
    // common one so it keeps the unmodified click.
    std::function<void (int)> onSectionToggled;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    std::function<void (int)> onSectionClicked;

    // Grown with the type. A section row carries a 15pt name over 13pt detail;
    // 36px held those at 13 over 11 and clips them now.
    static constexpr int rowHeight = 52;

private:
    int rowAt (juce::Point<int> p) const;

    std::vector<gb::SectionReport> sections;
    std::vector<int> selection;
    int playheadTick = -1;
    int queuedIndex  = -1;
    int hoverIndex   = -1;
};

// The list of things calibration steps through: every drum voice the kit
// claims to have, then the bass's lowest note.
class CalibrationList : public juce::Component
{
public:
    struct Row { juce::String label; int note = 0; };

    void setRows (std::vector<Row> r);
    void setSelected (int index);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    std::function<void (int)> onRowClicked;

    static constexpr int rowHeight = 38;

private:
    std::vector<Row> rows;
    int selected = 0;
};

// The control mappings for one instrument. Grows as long as the owner wants,
// each entry named by them, because no fixed list of knob names was ever going
// to match a real instrument.
class ControlList : public juce::Component
{
public:
    struct Row { juce::String name; int cc = 0; juce::String follows, type;
                 int positions = 0; };

    void setRows (std::vector<Row> r);
    void setSelected (int index);

    // What to say when the list is empty. "No controls mapped yet" and "this
    // song has no guitar" look identical otherwise, and the second one reads
    // as every saved mapping having been lost.
    void setEmptyMessage (const juce::String& m) { emptyMessage = m; repaint(); }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    std::function<void (int)> onRowClicked;

    static constexpr int rowHeight = 38;

private:
    std::vector<Row> rows;
    juce::String emptyMessage { "No controls mapped yet. Press Add." };
    int selected = 0;
};

// The saved takes. One row per performance: what it was called, which song it
// was a performance of, and the four numbers that make it that performance
// rather than another one.
//
// The numbers are on the row rather than behind a click because they are the
// whole content of a take. A list of names alone would say nothing about why
// any two of them differ, and "which of these was the busy one" is the question
// a take library exists to answer.
class TakeList : public juce::Component
{
public:
    struct Row { juce::String name, song, detail; };

    void setRows (std::vector<Row> r);
    void setSelected (int index);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    std::function<void (int)> onRowClicked;
    std::function<void (int)> onRowDoubleClicked;   // recall, without the round trip

    // Two lines: a 16pt name over 14pt detail, same as a section row carries.
    static constexpr int rowHeight = 46;

    // ROWS ARRIVE, the same way screens do: 44 px sideways from the right over
    // 300 ms, cubic ease-out, because movement is what the eye catches and a
    // fade alone was measured invisible (session 20). Opening the screen
    // cascades every row in, 30 ms apart and capped so a long list is not a
    // slow one; saving a take slides in just that row. Only where a row is
    // DRAWN moves - clicks land on the settled rows throughout.
    static constexpr int arriveMs = 300, staggerMs = 30, maxStagger = 10;
    static constexpr float arriveFromPx = 44.0f;

    void arriveAll();
    void arriveRow (int index);
    bool advanceArrival (int deltaMs);        // true while anything is still moving
    bool arriving() const noexcept { return arrivalRunning; }
    void settleArrival();
    float rowOffsetPx (int index) const;      // 0 when settled

private:
    std::vector<Row> rows;
    int selected = 0;

    std::vector<int> arrivalDelayMs;          // per row; -1 = not arriving
    int  arrivalElapsedMs = 0;
    bool arrivalRunning   = false;
};

// The song laid out in time, which is what a song IS.
//
// The section list this replaces on the song screen was a form: eight rows of
// text, one per section, read top to bottom. It could tell you the bridge had
// ten bass notes; it could not show you that the bridge is where both guitars
// drop out, or that the solo is the only place guitar 2 is loud. That shape is
// the whole content of an arrangement and it was invisible.
//
// Sections run left to right, each as wide as it is long in bars. Five lanes run
// down, one per player, and a lane is inked in proportion to how much that part
// plays there. The intensity curve rides above it all and the playhead sweeps
// across. Everything is where it happens.
class ArrangementView : public juce::Component
{
public:
    void setSections (std::vector<gb::SectionReport> s);
    void setPlayhead (int tick);          // -1 when the transport is stopped
    void setQueued   (int index);         // -1 when nothing is waiting
    void setSelection (const std::vector<int>& indices);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override;

    std::function<void (int)> onSectionClicked;   // plain click: jump there
    std::function<void (int)> onSectionToggled;   // ctrl-click: select for reroll

    // Header band, section name row, then one lane per player.
    static constexpr int curveHeight = 40;
    static constexpr int nameHeight  = 26;
    static constexpr int laneHeight  = 24;
    static constexpr int numLanes    = 5;

    // Reserved down the left for the lane labels. Without it they were drawn
    // over the first section's bars, which is exactly the kind of thing that
    // makes a custom component look homemade.
    static constexpr int gutter      = 52;
    static constexpr int wanted = curveHeight + nameHeight + numLanes * laneHeight + 34;

private:
    // Lanes stretch to whatever height the window gives them, down to the
    // constant above. A fixed height left a band of empty page under the
    // drawing on any window taller than the minimum, which reads as the
    // component having failed to fill its space rather than as margin.
    int laneH() const;

public:

private:
    int  sectionAt (juce::Point<int> p) const;
    juce::Rectangle<int> columnFor (size_t index) const;
    int  totalBars() const;

    std::vector<gb::SectionReport> sections;
    std::vector<int> selection;
    int playheadTick = -1;
    int queuedIndex  = -1;
    int hoverIndex   = -1;
};

// What Ghostband is actually SENDING, one row per beat, one column per player.
//
// Every other view in this plugin has shown a summary - counts, densities, the
// name of a feel. This shows the wire: the note, how hard, and any articulation
// landing on that beat. Given that most of this project's hard faults were
// "what is it actually sending", that is worth a component.
//
// One implementation, nine themes. Each part keeps its HUE and takes saturation
// and brightness from the palette, so the same code reads as neon on a dark
// ground and as ink on a light one.
class TrackerView : public juce::Component,
                    public juce::SettableTooltipClient
{
public:
    void setSections (std::vector<gb::SectionReport> s);
    void setPlayhead (int tick);
    void setQueued   (int index);
    void setSelection (const std::vector<int>& indices);

    // 0..1. Repaints only the ribbon, not the grid.
    void setQueuedPulse (float p);

    // Filled by the editor's timer from the processor, because the view must
    // not reach into the audio thread's sequence itself.
    void setCells (std::vector<GhostbandProcessor::TrackerCell> c, int firstTick,
                   int rowTicks, int beatTicks, int barTicks);

    int  visibleRows() const;

    // Which row is lit, by the same arithmetic paint() uses. -1 when stopped.
    // Exposed because the fault it exists to pin is invisible in a still: the
    // highlight was on the RIGHT-LOOKING row for most of a song and the wrong
    // one for the first third of a screenful.
    int  litRow() const
    {
        return playheadTick >= 0 && rowTicks > 0
                 ? (playheadTick - firstRowTick) / rowTicks : -1;
    }
    int  firstTickWanted() const { return firstRowTick; }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override;

    std::function<void (int)> onSectionClicked;
    std::function<void (int)> onSectionToggled;

    // A click on a ROW rather than on the section ribbon, reported as the tick
    // that row starts at. The grid shows time, and everything editable is
    // attached to a bar, so the editor turns the tick into a bar itself.
    std::function<void (int)> onRowClicked;

    // Which row is being edited, so it can be drawn as the one you picked. -1
    // for none. Kept in ticks, not row numbers, so it survives the grid
    // scrolling under it.
    void setEditedTick (int tick);

    //==========================================================================
    // Motion the grid owns, advanced by the editor's clock rather than by a
    // timer of its own - so it stops dead when nothing is moving, like
    // everything else in this window.
    //
    // Returns true while something is still travelling, which is the contract
    // AnimationClock uses to decide whether to keep running.
    bool advanceMotion (int deltaMs);

    // Whether anything WOULD move if the clock ran. The editor asks this after
    // pushing a new playhead, because a playhead that has stepped to a new row
    // is the one source of motion in here that starts without a button press.
    bool motionPending() const;

    // A new arrangement, swept in from the top. Called on a reroll or a dice
    // roll: the notes change in place and every column looks broadly the same
    // afterwards, so without this the strongest feedback that anything happened
    // is a number in the seed box.
    void sweepIn();

    // Everything arrives at once. Called by the editor's settleAnimations(), so
    // a check that measures the grid measures a settled one.
    void settleMotion();

    // For checks that measure rather than watch.
    float paintedRowForTesting() const { return paintedRow; }
    bool  sweepingForTesting()   const { return sweep >= 0.0f; }
    float ribbonFlareForTesting (int i) const
    {
        return i >= 0 && static_cast<size_t> (i) < ribbonFlare.size()
                 ? ribbonFlare[static_cast<size_t> (i)] : 0.0f;
    }

    static constexpr int numParts     = 5;
    static constexpr int rowHeight    = 19;
    static constexpr int headerHeight = 52;
    static constexpr int barColumn    = 46;

private:
    juce::Colour partColour (int part) const;
    bool onDarkGround() const;
    juce::Rectangle<int> ribbonFor (size_t index) const;
    int  sectionAt (juce::Point<int> p) const;

    std::vector<gb::SectionReport> sections;
    std::vector<GhostbandProcessor::TrackerCell> cells;
    std::vector<int> selection;

    int editedTick = -1;
    // 0..1, driven by the editor's animation clock while a section is queued.
    // A jump waits for the next bar line, which can be several seconds away at
    // one row per bar - long enough that a static highlight reads as "selected"
    // rather than as "about to happen".
    float queuedPulse = 0.0f;

    int firstRowTick = 0;
    // How much music one row covers. Equal to beatTicks at the default zoom,
    // a whole bar at the coarsest and a sixteenth at the finest - so every
    // row-to-tick sum uses rowTicks and every "which beat is this" sum uses
    // beatTicks, and the two must not be confused again.
    int rowTicks     = 96;
    int beatTicks    = 96;
    int barTicks     = 384;
    int playheadTick = -1;
    int playheadRow  = -1;
    int queuedIndex  = -1;
    int hoverIndex   = -1;

    //==========================================================================
    // THE LIT ROW, EASED.
    //
    // litRow() stays exact and is not touched. It is the thing a check pins and
    // the thing that was wrong for four sessions - the highlight sitting on bar
    // 17 while the song played from bar 1 - so the smoothing is a SEPARATE
    // number used only for drawing. If these two ever disagree about anything
    // other than a hundred milliseconds of travel, the exact one is right.
    //
    // -1 when stopped. Otherwise it chases litRow() and the band glides between
    // rows instead of teleporting, which at a bar per row is a step every two
    // seconds and reads as a flicker rather than as movement.
    float paintedRow = -1.0f;

    // A flare when the playhead ENTERS a section, decaying over about a second
    // on top of the steady lit state. One entry per section rather than one for
    // "the current one", so a song with two choruses flares twice rather than
    // carrying a glow across from the first.
    std::vector<float> ribbonFlare;
    int flaringSection = -1;

    // 0..1 while a new arrangement sweeps in, -1 at rest. Drawn as a veil that
    // lifts rather than as an alpha threaded through every draw call in paint().
    float sweep = -1.0f;

    // Dirties only the rows a moving band touched. A full repaint of this grid
    // is 5 ms and the band moves at 60 Hz.
    void repaintBand (float fromRow, float toRow);
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

    // Lets the test harness render every screen to PNG. Layout bugs are
    // invisible to every other check, and screens nobody looks at are exactly
    // where they hide.
    void showScreenForSnapshot (int screenIndex);
    static const char* screenName (int screenIndex);
    static constexpr int numScreens = 6;

    // The reroll path, reachable without a mouse.
    //
    // It runs from a ctrl-click on the section list, through a lambda, into a
    // selection vector, through a second lambda on the Roll button, and only
    // then into the processor. Every piece of that was tested except the
    // pieces that join it up - and "the button says Reroll 2 sections and
    // clicking it does nothing" is a claim about precisely those joins.
    void ctrlClickSectionForTesting (int index);

    // Switching theme with the window open goes through a different path from
    // starting on one, and only the second was ever exercised - the picker
    // itself was laid out at zero height and could not be clicked.
    void setThemeForTesting (int index);
    // The structure editor's form, driven the way a person drives it.
    //
    // applySectionEdit writes playsGuitar2 now, where it used to leave the flag
    // untouched - so the second guitar's survival depends on the FORM being
    // filled in correctly, not on the processor refusing to write. Testing
    // getSectionEdit straight into applySectionEdit cannot see that: it never
    // goes near the toggle that would be wrong.
    void editSectionForTesting (int index);      // open that section in the form
    void commitSectionEditForTesting();          // as if a field lost focus
    void setSectionGuitar2ForTesting (bool on);  // click the guitar 2 toggle
    bool sectionGuitar2ForTesting() const;

    // The quick edit strip, driven the way a mouse drives it. Clicking a row is
    // the only way in, so a test that called openTrackerEdit directly would
    // skip the hit test - which is the part that can be wrong.
    void clickTrackerRowForTesting (int tick);
    int  trackerEditBarForTesting() const;
    juce::String trackerChordForTesting() const;
    void typeTrackerChordForTesting (const juce::String& chord);

    // Nothing moving, and nothing left on screen that was only there to move.
    // The veil covers the whole window, so "idle" has to mean it is gone.
    // A screen change the way a BUTTON makes one - not showScreenForSnapshot,
    // which deliberately settles everything so checks and images see the window
    // as it ends up. The difference between those two paths is exactly where an
    // animation that never runs would hide.
    void changeScreenForTesting (int index);

    // Put the fade at a given point so a frame of it can be rendered and
    // LOOKED AT. "It is running" and "you can see it" are different claims and
    // only the second one matters.
    void setFadeForTesting (float alpha);

    // Light every knob's trail, so a frame of it can be rendered and looked at.
    // Eye candy that has never been seen is a guess about eye candy.
    void setDialGlowForTesting (float glow, float ghostOffset);

    int  litTrackerRowForTesting() const { return tracker.litRow(); }
    int  visibleTrackerRowsForTesting() const { return tracker.visibleRows(); }

    // Put everything where it is going, now. Any check that MEASURES the window
    // has to call this after a gesture that starts motion, or it measures a
    // frame of a transition - the quick-edit strip arrives from twenty pixels
    // below, and mid-slide it genuinely does sit on top of the transport line.
    void settleAnimationsForTesting() { settleAnimations(); }

    // The takes list, as the eye sees it mid-arrival.
    float takeRowOffsetForTesting (int row) const { return tkList.rowOffsetPx (row); }
    bool  takesArrivingForTesting() const         { return tkList.arriving(); }
    void  advanceAnimationsForTesting (int ms)    { advanceAnimations (ms); }
    void  pressTakesButtonForTesting()             { takesButton.onClick(); }   // as the owner opens it, unsettled

    bool animationsIdleForTesting() const
    {
        return ! animator.isTimerRunning() && ! veil.isVisible();
    }

    void pressRollForTesting();

    // The planner's controls as the owner would see them.
    bool         writeEnabledForTesting() const { return writeButton.isEnabled(); }
    juce::String writeStatusForTesting()  const { return writeStatus.full; }
    juce::String writeStatusShownForTesting() const { return writeStatus.getText(); }
    juce::String writeTooltipForTesting()       { return writeButton.getTooltip(); }
    void         refreshPlannerForTesting()     { refreshPlannerControls(); }
    void         choosePlannerForTesting (int modelIndex, int effortIndex)
    {
        plannerModelBox.setSelectedItemIndex (modelIndex, juce::sendNotificationSync);
        plannerEffortBox.setSelectedItemIndex (effortIndex, juce::sendNotificationSync);
    }
    juce::String plannerChoiceForTesting() const { return plannerModelBox.getText() + " / " + plannerEffortBox.getText(); }
    juce::String plannerCostForTesting()   const { return plannerCostLabel.getText(); }
    bool plannerCostFitsForTesting()
    {
        const juce::Font font = getLookAndFeel().getLabelFont (plannerCostLabel);
        const auto area = plannerCostLabel.getBorderSize().subtractedFrom (plannerCostLabel.getLocalBounds());
        const int lines = (int) ((float) area.getHeight() / font.getHeight());
        // word wrap loses up to a word a line; a quarter of a line each is generous
        return plannerCostLabel.getMinimumHorizontalScale() >= 1.0f && lines >= 1
            && juce::GlyphArrangement::getStringWidth (font, plannerCostLabel.getText())
                   <= (float) area.getWidth() * 0.75f * (float) lines;
    }
    void         pressWriteForTesting (const juce::String& request) { writeRequest.setText (request, false); startWriting(); }
    void         showWriteStatusForTesting (const juce::String& line) { showWriteStatus (line, ghost::dim); }

    // Whether the shown line fits its one line at its width, never squashed.
    bool writeStatusFitsForTesting()
    {
        const juce::Font font = getLookAndFeel().getLabelFont (writeStatus);
        const auto area = writeStatus.getBorderSize().subtractedFrom (writeStatus.getLocalBounds());
        return writeStatus.getMinimumHorizontalScale() >= 1.0f
            && juce::GlyphArrangement::getStringWidth (font, writeStatus.getText()) <= (float) area.getWidth();
    }
    bool writeStatusOpensForTesting() const { return writeStatus.shortened && writeStatus.onClick != nullptr; }
    int  rerollSelectionSizeForTesting() const;

    // The stall detector, driven the way the clock drives it. There is no way
    // to make a real timer miss its slot on demand, and the piece that has to
    // work is the bookkeeping between two ticks - so the harness calls the same
    // callback with a real delay between the calls.
    void runTimerForTesting();

    // The same tick with the window off screen, which is what a plugin editor
    // is whenever the host has it behind a tab. Nothing should be recorded.
    void runTimerOffScreenForTesting();
    int  stallCountForTesting() const;
    int  audioShortfallsForTesting() const { return audioShortfallsLogged; }
    juce::String footerForTesting() { lastLatencyText.clear(); updateLatencyReadout(); return latencyLabel.getText(); }
    void checkMeasuredBlockSizeForTesting (juce::uint64 samples, unsigned blocks) { noteMeasuredBlockSize (samples, blocks); }
    juce::String stallDetailForTesting() const;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshFromProcessor();
    void layOutFooter (juce::Rectangle<int> area);

    // The song screen, which is laid out differently from every other one: a
    // fixed rail of controls down the left and the grid taking all the rest.
    // See the note on the definition for why.
    void layOutSongScreen (juce::Rectangle<int> area);

    // Wide enough for MODE's box beside its label, which is the widest pair in
    // it, and narrow enough to leave the grid the majority of the window at the
    // smallest size the window is allowed to be.
    static constexpr int kRailWidth = 320;
    void markDialsDirty();
    void styleButton (juce::TextButton& b, bool primary);
    void styleSlider (juce::Slider& s);

    GhostbandProcessor& processor;

    juce::TextButton loadButton   { "Load plan..." };
    juce::TextButton reloadButton { "Reload" };
    juce::TextButton rollButton   { "Roll" };

    // Everything at once, for the fun of it. Click rolls the character of this
    // song; ctrl-click picks a different preset first. Right-click puts the
    // last roll back - one step, and it exists because rolling past a good one
    // with no way back is the thing that would make this frustrating.
    DiceButton diceButton;

    //==========================================================================
    // THE AI PLANNER, on the song screen - chosen by the owner over a screen of
    // its own and a pop-up, 2026-09-24: "on the song screen", and a written song
    // replaces the current one at once with one step back rather than asking
    // first.
    //
    // One row - a text box whose placeholder says what it is for, and a Write
    // button - with a status line under it. The rail had fifty pixels to spare,
    // and a heading above a box that already says "write a song" was spending
    // twelve of them on saying it twice.
    class WriteButton : public juce::TextButton
    {
    public:
        WriteButton() : juce::TextButton ("Write") {}

        // Right-click puts the previous song back - the same one step the dice
        // has, and for the same reason: replacing a song with no way back is
        // what would make this frustrating rather than fun.
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
            juce::TextButton::mouseDown (e);
        }

        std::function<void()> onRightClick;
    };

    juce::TextEditor writeRequest;
    WriteButton      writeButton;
    // ONE LINE, AND NOTHING IN IT LOST. Text too long for the line is cut at a
    // word and ends "... more"; a click opens the whole of it. The planner's
    // explanation is one or two sentences and the rail has room for one line.
    class WriteStatusLine : public juce::Label
    {
    public:
        juce::String full;          // what the line says, before it was fitted
        bool         shortened = false;
        std::function<void()> onClick;

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (shortened && onClick && ! e.mods.isPopupMenu()) onClick();
        }
    };

    WriteStatusLine  writeStatus;
    juce::String     writeNote;                    // a line Write itself put up - a refusal, an undo;
    bool             writeNoteIsWarning = false;   // kept until the next write

    void showWriteStatus (const juce::String& line, juce::Colour colour);
    void fitWriteStatus();
    void openWriteStatus();

    // Settings: where the key goes in. There is no field that shows it back -
    // the placeholder says whether one is saved, and that is all.
    juce::Label      plannerKeyLabel;
    juce::TextEditor plannerKeyEditor;
    juce::TextButton plannerKeySave  { "Save key" };
    juce::TextButton plannerKeyClear { "Clear" };

    // Which model writes and how hard it thinks, with what that costs.
    juce::Label      plannerModelLabel, plannerEffortLabel, plannerCostLabel;
    juce::ComboBox   plannerModelBox, plannerEffortBox;
    double           plannerCostAtMs = 0.0;     // when the cost line was last worked out
    bool             plannerKeyUnreadable = false;   // saved, but Windows will not decrypt it
    void             refreshPlannerCost();

    void refreshPlannerControls();
    void startWriting();

    // Ghostband's own transport. The host's is usually left running for a whole
    // session, so stopping the band and stopping the host are different things.
    juce::TextButton playPauseButton { "Pause" };

    juce::ComboBox keyBox;
    juce::ComboBox modeBox;
    juce::ComboBox styleBox;
    juce::ComboBox tuningBox;

    // How much music one tracker row covers. Lives on the song screen next to
    // the thing it changes, not in Settings, because it is something you reach
    // for while looking at the grid.
    juce::ComboBox zoomBox;
    juce::Label    zoomLabel;

    juce::Label    modeLabel;
    juce::Label    keyLabel;
    juce::Label    styleLabel;
    juce::Label    tuningLabel;
    juce::Label    tempoLabel;

    juce::Slider complexitySlider;
    juce::Slider humanizeSlider;

    // How much the second guitar answers. A dial rather than a per-section
    // edit, because "less of that" and "none of that" are the two things
    // anyone actually wants to say about a fill, and neither is worth nine
    // section edits.
    juce::Slider fillsSlider;

    // HOW MUCH THE BAND PLAYS WHAT IT FEELS LIKE rather than what is obvious.
    // Beside the other three because it is the same kind of thing - a
    // disposition applied to the whole band - and not a quality control: both
    // ends of it are real sounds. See gb::byIntuition for why the middle of
    // this dial is exactly what the engine did before it existed.
    juce::Slider intuitionSlider;

    juce::Label  complexityLabel;
    juce::Label  humanizeLabel;
    juce::Label  fillsLabel;
    juce::Label  intuitionLabel;

    // Per-part level, sent as MIDI CC 7. Mixing normally belongs in the host,
    // but four knobs here saves wiring four gain blocks in the rackspace and is
    // the fastest way to hear a part that is buried.
    juce::Slider levelDrums, levelBass, levelGuitar, levelGuitar2, levelPiano;
    juce::Label  levelDrumsLabel, levelBassLabel, levelGuitarLabel,
                 levelGuitar2Label, levelPianoLabel;
    juce::Label  mixLabel;

    juce::TextEditor seedEditor;
    juce::Label      seedLabel;

    //==========================================================================
    // THE QUICK EDIT STRIP, under the grid. Click any row and it opens on the
    // bar that row is in.
    //
    // What can be edited here is what is AUTHORED, not what is generated. The
    // grid shows notes, and a note is the output of a seed and a plan - there
    // is nowhere to put a hand-placed one and no way to keep it through a
    // reroll. The chord under a bar and the feel of its section ARE authored,
    // they are what actually decide those notes, and changing either is heard
    // immediately. Anything deeper is a button away in Edit song.
    juce::Label      trkWhereLabel;      // "BAR 12   chorus1"
    juce::Label      trkChordLabel;
    juce::TextEditor trkChord;
    juce::Label      trkFeelLabel;
    juce::ComboBox   trkFeel;
    juce::TextButton trkReroll { "Reroll section" };
    juce::TextButton trkOpen   { "Open in editor" };
    juce::TextButton trkClose  { "Close" };

    // The bar the strip is editing, or -1 when it is not showing.
    int  trackerEditBar = -1;
    void openTrackerEdit (int tick);
    void refreshTrackerEdit();
    void closeTrackerEdit();

    // A heading for the transport row. Every other block in the rail has one -
    // KEY, STYLE, SEED, MIX - and this row had none, so the Pause button read
    // as a stray control rather than as the band's transport. It was reported
    // as missing while sitting in plain sight, which is what an unlabelled
    // control in a labelled column looks like.
    juce::Label      bandLabel;

    // Tempo, editable. The plan carries a bpm and nothing in the interface
    // could change it, so a preset was stuck at whatever tempo it was written
    // at unless you opened the file in a text editor.
    juce::Label      bpmLabel;
    juce::TextEditor bpmEditor;

    // Says how to select a section for a reroll, on the row directly above the
    // list you have to click. It was in the Roll button's tooltip and nowhere
    // else, so the feature read as broken rather than as undiscovered.
    juce::Label      rollHintLabel;

    // The theme picker, on Settings beside the other things about how the
    // plugin looks and behaves rather than about the song.
    juce::ComboBox themeBox;
    juce::Label    themeLabel;

    // True while rollHintLabel is reporting a reroll that just happened rather
    // than showing its standing hint, so the next selection change puts the
    // hint back instead of leaving a stale claim on screen.
    bool             rollHintDirty = false;

    juce::Label planLabel;
    juce::Label headlineLabel;
    juce::Label statusLabel;
    juce::Label profilesLabel;

    // What those two footer lines actually are.
    //
    // They were an unlabelled pile: two lines of small grey text stacked on
    // each other, in two colours, with nothing saying what either one was or
    // why it was there. Reported in those words. A caption each, and a hairline
    // above them, is the whole fix - the information was fine, its presentation
    // gave no way in.
    juce::Label statusCaption;
    juce::Label profilesCaption;

    // Where to draw that hairline. Set during layout, because only layout knows
    // where the footer ended up on each screen.
    juce::Rectangle<int> footerRule;

    // The panel the song controls sit on, likewise measured during layout.
    //
    // Everything above the section list used to float directly on the
    // background - key, mode, style, the three dials, the mix, the seed - which
    // is what made the window read as a form rather than as an instrument. A
    // ground with panels ON it is most of what separates the two.
    juce::Rectangle<int> controlsPanel;

    // The quick edit strip's own surface, painted behind it so it reads as one
    // thing attached to the grid rather than four loose controls.
    juce::Rectangle<int> trackerEditStrip;
    juce::Label summaryLabel;
    juce::Label transportLabel;

    juce::Viewport   viewport;
    SectionList      sectionList;   // the edit screen, where picking one row is the job
    ArrangementView  arrangement;   // kept, but no longer on screen - see TrackerView
    TrackerView      tracker;       // the song screen

    // One tooltip window for the whole editor. Without one, setTooltip is
    // recorded and never shown, which looks exactly like tooltips not working.
    juce::TooltipWindow tooltips { this, 550 };

    // Three screens share the window: the normal song view, the calibration
    // view, and the structure editor.
    enum class Screen { Song, Calibrate, Edit, Settings, About, Takes };
    Screen screen = Screen::Song;

    // ---- takes ----
    void applyThemeChoice (int index);

    // What a theme change has to re-apply by hand.
    //
    // Buttons, combo boxes and text fields are found by type at recolour time -
    // every one of them takes the same palette roles, so there is nothing to
    // remember. Labels and sliders are not like that: a label carries its own
    // choice of role, and a slider may be one of the linear dials or one of the
    // rotary mix knobs, which must never be restyled as linear. Those two get
    // recorded as they are built.
    std::vector<std::pair<juce::Label*, const juce::Colour*>> themedLabels;
    std::vector<juce::Slider*> themedSliders;

    // Re-applies every colour that was handed to a component when it was built.
    // A Label given an explicit textColourId keeps it through any number of
    // LookAndFeel changes, so without this half the interface stays on the old
    // palette and the theme looks broken rather than applied.
    void recolour();

    void refreshTracker();
    int  trackerRowTicks (int beatTicks, int barTicks) const;
    int  lastTrackerTick  = -1;
    int  lastTrackerRows  = -1;
    int  lastTrackerPerRow = -1;

    void refreshTakes();
    void saveTakeFromBox();

    juce::TextButton takesButton  { "Takes" };
    juce::TextButton tkDoneButton { "Done" };
    juce::TextButton tkSaveButton { "Save take" };
    juce::TextButton tkRecall     { "Recall" };
    juce::TextButton tkDelete     { "Delete" };
    juce::TextEditor tkName;
    juce::Label      tkHeading, tkHelp, tkNameLabel, tkResult;
    juce::Viewport   tkViewport;
    TakeList         tkList;
    int              tkSelected = 0;

    // What is in the list right now, so Recall and Delete act on the row that
    // was drawn rather than re-reading the file and hoping the order held.
    std::vector<GhostbandProcessor::Take> takes;

    void paintAbout (juce::Graphics& g, juce::Rectangle<int> area);

    // Header navigation, visible on every screen.
    juce::TextButton settingsButton { "Settings" };
    juce::TextButton aboutButton    { "About" };
    juce::TextButton backButton     { "Back" };

    // Settings
    juce::ComboBox chDrums, chBass, chGuitar, chGuitar2, chPiano;
    juce::Label    chDrumsLabel, chBassLabel, chGuitarLabel, chGuitar2Label, chPianoLabel;

    // What is actually in each slot. The rows name a part, not an instrument,
    // and "guitar 2" is a different plugin depending on the song loaded - which
    // is a thing you should not have to work out from memory while editing its
    // controls.
    juce::Label    chDrumsName, chBassName, chGuitarName, chGuitar2Name, chPianoName;
    juce::Label    learnPartName;
    juce::TextButton testDrums { "Test" }, testBass { "Test" },
                     testGuitar { "Test" }, testPiano { "Test" },
                     testGuitar2 { "Test" };
    // MIDI Learn helpers: sweep a CC at an instrument so it can latch onto it.
    void refreshControls();
    void pushControlEdit();

    // A control's value in the units it is actually described in - per cent for
    // a knob, a position number for a selector, one or zero for a switch - and
    // back again. One place, so the boxes that read it and the boxes that write
    // it cannot drift apart.
    static double controlUnitsToNorm (const juce::String& type, int positions, int typed);
    static int    normToControlUnits (const juce::String& type, int positions, double v);
    static juce::String controlUnitsHint (const juce::String& type, int positions);

    juce::ComboBox   learnPart;
    juce::TextButton ctlAdd    { "+ Add" };
    juce::TextButton ctlRemove { "Remove" };
    juce::TextButton ctlTeach  { "Teach this control" };

    // Teach sweeps, which MIDI Learn needs and a person cannot read. These two
    // are for looking at the instrument: park it on one value, or step through
    // every position slowly enough to count them.
    juce::TextButton ctlSend   { "Send" };
    juce::TextButton ctlWalk   { "Walk the list" };
    juce::TextButton ctlSave   { "Save mappings" };

    // Puts an instrument's mappings back to what its profile file says, and
    // says how many differ before you press it. Only the CC can differ - see
    // the note on resetControlsToProfile - so the count is small and exact
    // rather than a vague "this has been changed" warning.
    juce::TextButton ctlReset  { "Reset to profile" };
    juce::Label      ctlDiffLabel;

    // Opens the folder holding changes.log, stalls.log and the takes, because
    // "%APPDATA%\Ghostband" is not something anyone should have to be told
    // twice.
    juce::TextButton openDataFolder { "Show log folder" };
    juce::TextEditor ctlName;
    juce::ComboBox   ctlFollows, ctlType;

    // How many choices a selector has, typed rather than picked. Any list of
    // counts is eventually too short - a stompbox selector runs to thirty - and
    // the number is known exactly by whoever is looking at the instrument.
    juce::TextEditor ctlPositions;

    // Where a "fixed" control is parked. Without it, fixed could only ever mean
    // the bottom of the range, which is rarely the value anyone wanted.
    juce::TextEditor ctlValue;

    // The two ends of the range a driven control travels between, in the
    // control's own units. Putting the higher number first inverts it, which is
    // the whole answer for a control that reads backwards - and narrowing the
    // range is how a rolled selector is kept inside one bank of a long list.
    juce::TextEditor ctlFrom, ctlTo;
    juce::Label      learnHeading, learnHelp, ctlNameLabel, ctlFollowsLabel,
                     ctlTypeLabel, ctlPositionsLabel, ctlPositionsHint,
                     ctlValueLabel, ctlValueHint,
                     ctlRangeLabel, ctlRangeToLabel, ctlRangeHint;
    juce::Viewport   ctlViewport;
    ControlList      ctlList;
    int              ctlSelected = 0;
    bool             suppressControlCallbacks = false;

    juce::Label    settingsHeading, channelsHelp;
    juce::TextButton resetSizeButton   { "Reset window size" };
    juce::TextButton reloadProfilesBtn { "Reload driver profiles" };

    // Whether the song runs on its own tempo or the host's. A VST3 cannot set
    // the host's, so this is the only way a song plays at the tempo it was
    // written at without setting the host by hand each time.
    juce::TextButton tempoModeButton   { "Tempo: song" };

    // About
    juce::TextButton manualButton { "User manual" };
    juce::TextButton repoButton   { "Source code" };
    juce::TextButton emailButton  { "Email Kyle" };

    // ---- calibration ----
    void updateModeVisibility();
    void refreshCalibration();

    // ---- structure editing ----
    void pushSectionEdit();
    void pullSectionEdit();

    juce::TextButton editButton     { "Edit song" };
    juce::TextButton edDoneButton   { "Done" };
    juce::TextButton edAddButton    { "+ Add" };
    juce::TextButton edDeleteButton { "Delete" };
    juce::TextButton edUpButton     { "Up" };
    juce::TextButton edDownButton   { "Down" };
    juce::TextButton edSaveButton   { "Save" };
    juce::TextButton edSaveAsButton { "Save as..." };

    juce::TextEditor edName, edBars, edChords;
    juce::Slider     edIntensity;
    juce::ComboBox   edFeel, edFill, edLead;
    juce::Label      edLeadLabel;
    juce::ToggleButton edDrums { "drums" }, edBass { "bass" },
                       edGuitar { "guitar" }, edGuitar2 { "guitar 2" },
                       edPiano { "piano" };
    juce::Label      edNameLabel, edBarsLabel, edIntensityLabel,
                     edFeelLabel, edFillLabel, edChordsLabel, edPlaysLabel;

    int  editSelected = 0;
    bool suppressEditCallbacks = false;

    juce::TextButton calibrateButton { "Calibrate" };
    juce::TextButton calDoneButton   { "Done" };
    juce::TextButton calSaveButton   { "Save map" };
    juce::TextButton calLowerButton  { "<" };
    juce::TextButton calHigherButton { ">" };
    juce::TextButton calPlayButton   { "Play" };
    juce::Label      calHintLabel;
    juce::Label      calNoteLabel;
    juce::Viewport   calViewport;
    CalibrationList  calList;
    int              calSelected = 0;

    std::unique_ptr<juce::FileChooser> chooser;

    ghost::GhostbandLookAndFeel lookAndFeel;

    // Ghostband is free; this is a button, not a nag, and nothing is gated
    // behind it.
    juce::TextButton donateButton { "Support Ghostband" };
    juce::Label      donateLabel;

    // Latency, shown on every screen in the footer beside the donate button.
    //
    // Ghostband adds none: it places each event at its own sample offset inside
    // the block the host asked for, so the number is a flat zero and stays that
    // way. That is worth stating rather than leaving blank - when a rig feels
    // late, being able to see at a glance that the MIDI brain is not the thing
    // adding the delay is most of the diagnosis.
    juce::Label      latencyLabel;
    void updateLatencyReadout();

    //==========================================================================
    // Catching the stall.
    //
    // Reported 2026-09-12: "sometimes the ghostband UI is frozen, playhead not
    // moving, screen not changing, but audio is still heard like normal and
    // then suddenly it will start working normally again." It has not recurred
    // since, and every part of Ghostband's own frame was measured and is fast -
    // a full repaint is under 5 ms and a whole reroll is 1.2 ms - so there is
    // nothing here to fix by reasoning. The plugin has to catch it in the act.
    //
    // The measurement that matters is the GAP BETWEEN CALLBACKS, not the time
    // spent inside one, because the two have opposite causes:
    //
    //   long gap, short work   the timer was not called. The message thread was
    //                          busy elsewhere - the host's, not ours.
    //   long work              we did something slow. Ours to fix.
    //
    // And the audio block counter separates "the window froze" from "everything
    // froze". Three numbers, and between them they say which of those it was.
    struct Stall
    {
        double  gapMs      = 0.0;   // how long the timer went uncalled
        double  workMs     = 0.0;   // how long the PREVIOUS callback took

        // Everything Ghostband did on the message thread DURING the gap -
        // painting and its own handlers, both of which happen outside the timer
        // callback and both of which would otherwise be invisible here. Against
        // gapMs this is the whole answer: near zero means it was not us.
        double  ghostbandMs = 0.0;
        double  worstPieceMs = 0.0;
        juce::String worstPiece;    // and which piece that was

        int     audioBlocks = 0;    // blocks the audio thread ran during the gap

        // What the window thought it was, at the moment of the gap. Evidence,
        // not reasoning: the previous theory about these was argued from the
        // code and was wrong, and nothing in the log could have shown that.
        bool    showing    = true;
        bool    foreground = true;
        int     screen     = 0;
        bool    playing    = false;
        juce::String at;            // wall clock, so it can be matched to what you were doing
    };

    //==========================================================================
    // What goes in the change log, polled rather than reported.
    //
    // The alternative was a logChange call at every place a control can move,
    // which is forty-odd sites, all of them easy to forget when the forty-first
    // is added - and worse, a knob dragged across its range fires its callback
    // on every pixel, so a log built that way would bury the day's real
    // decisions under two hundred lines of one gesture.
    //
    // Polling the state and logging what SETTLED fixes both. It also catches
    // changes the editor never sees, like a host moving something.
    struct LogSnapshot
    {
        juce::String plan, key, mode, style, tuning, theme, rows;
        int  seed = 0;
        int  complexity = 0, humanize = 0, fills = 0;   // per cent: below that is jitter
        int  intuition  = 50;
        int  levels[5]   = { 0, 0, 0, 0, 0 };
        int  channels[5] = { 0, 0, 0, 0, 0 };
        bool paused = false;

        bool operator== (const LogSnapshot& o) const;
        bool operator!= (const LogSnapshot& o) const { return ! (*this == o); }
    };

    LogSnapshot takeLogSnapshot() const;
    void        pollChangeLog();

    LogSnapshot logged, pending;
    bool         haveLoggedBaseline = false;
    juce::uint32 pendingSince = 0;

    // Long enough that a knob dragged across its range is one line, short
    // enough that the log keeps up with somebody working.
    static constexpr int logSettleMs = 500;

    //==========================================================================
    // Motion. See Eased and AnimationClock at the top of this file.
    //
    // Three things move, and each was chosen because it makes something
    // CLEARER, not because it makes it livelier:
    //
    //   the screen change   so you can see that you moved, rather than
    //                       inferring it from the content having changed
    //   a queued section    so waiting for the bar line looks like waiting
    //                       rather than like nothing having happened
    //   the three dials     so recalling a take shows you WHICH of them moved,
    //                       which is the whole question a take answers
    //
    // Nothing animates while the user is holding it: a knob under the mouse
    // follows the mouse exactly.
    bool advanceAnimations (int deltaMs);
    void beginScreenFade();

    // Everything moving, put where it was going, now.
    //
    // For the harness and the snapshots, which must see the settled window
    // rather than a frame of one - and for the veil, which is a full-window
    // component that must not exist as a visible thing once its fade is done.
    // It covers the whole window by design, so leaving it visible makes it
    // overlap every control on every screen.
    void settleAnimations();
    void easeDialsTo (double complexity, double humanize, double fills, double intuition);

    // Every knob on the song screen, eased to wherever the processor now says
    // it should be. For a plan load and a take recall: both replace the whole
    // set at once, and watching them travel is how you see WHAT was replaced.
    void easeAllKnobsToProcessor();

    // A theme change repaints every pixel in the window at once, which is the
    // one moment a plain cross-fade is exactly right - there is no layout
    // change to communicate, only a new set of colours.
    void beginThemeFade();

    Eased mixDrums, mixBass, mixGuitar, mixGuitar2, mixPiano;
    bool  mixAnimating = false;

    ScreenVeil veil;

    // How far the content is displaced, in pixels, while a screen settles.
    //
    // OPACITY ALONE WAS NOT ENOUGH, and that is the whole lesson of the first
    // attempt. A veil fading from the background colour over a dark theme is
    // dark-on-dark: measurably there, perceptually nothing, and reported as
    // "not seeing any animations whatsoever". Movement is what the eye actually
    // catches - a dozen pixels of travel reads instantly where a fade of the
    // same length does not.
    //
    // Applied to the content rectangle in resized(), so the header and the
    // footer rail stay put and only the screen itself arrives. Chrome that
    // slides with its content looks like the window is broken.
    Eased contentSlide;
    bool  takesWereShowing = false;   // so the rows cascade in on OPENING the screen only

    // The quick-edit strip arriving. Its own value rather than the content
    // slide's, because opening it is not a screen change - the grid above must
    // not move, only the strip.
    Eased stripSlide;

    // Which way the screen comes in from. Going deeper - Song to Settings, to
    // Edit, to Takes - it arrives from the right; coming back it arrives from
    // the left, like the thing you left behind sliding back into place.
    // Direction is most of why a transition reads as navigation rather than as
    // a flicker.
    float slideFrom = 0.0f;

    //==========================================================================
    // Every knob that can be turned, and how lit it is.
    //
    //   ghost  where it WAS, lagging behind by a couple of hundred milliseconds
    //   glow   1 the instant it moves, easing back to 0 once it stops
    //
    // The lag is the part that means something. A knob with no number on it
    // tells you where it ended up and not how far it travelled, and the trail
    // between ghost and value is exactly that distance.
    struct DialTrail
    {
        juce::Slider* slider = nullptr;
        Eased ghost, glow;
        double lastSeen = -1.0;
    };

    std::vector<DialTrail> trails;
    bool trailsAnimating = false;

    void registerTrail (juce::Slider& s);
    void noticeDialMoves();
    int  slideOffsetPx() const { return juce::roundToInt (contentSlide.value()); }
    Eased      dialComplexity, dialHumanize, dialFills, dialIntuition;
    bool       dialsAnimating = false;

    // Rises and falls while a jump is waiting for the bar line.
    float queuedPhase = 0.0f;

    // What the window is currently SHOWING, as opposed to what `screen` has
    // just been set to. The difference between the two is a screen change.
    Screen lastShownScreen = Screen::Song;

    AnimationClock animator { [this] (int ms) { return advanceAnimations (ms); } };

    void noteTimerTick();           // called at the top and bottom of timerCallback
    juce::String stallSummary() const;
    juce::String stallDetail() const;

    std::vector<Stall> stalls;
    double   lastTimerStartMs = 0.0;
    double   lastTimerWorkMs  = 0.0;
    unsigned lastAudioBlocks  = 0;
    double   worstGapMs       = 0.0;

    // A gap measured while the window is NOT ON SCREEN is not a freeze anybody
    // saw, and logging it is worse than useless because it buries the ones that
    // were real.
    //
    // The first log off a real rig proved it: 1,118 lines, of which 1,109 were
    // recorded with the transport stopped and 1,104 of those were a gap of
    // EXACTLY 600-601 ms. Ghostband's timer asks for 33 ms and no part of it
    // asks for 600, so a flat 600 repeated a thousand times is something
    // outside imposing a period - Windows throttling a window that is alive but
    // hidden, which is what a plugin editor is whenever the host has it behind
    // a panel tab. Nine lines in that file were real, and they were the point.
    //
    // Tests have no desktop peer, so nothing is ever showing and the detector
    // would go permanently silent. They say so explicitly instead.
    bool pretendOnScreenForTesting = false;
    bool windowIsOnScreen() const { return pretendOnScreenForTesting || isShowing(); }

    // AND THAT GUESS WAS WRONG, WHICH THE NEXT LOG PROVED IN NINETY MINUTES.
    //
    // Gating on isShowing() changed nothing: 630 more lines arrived, every one
    // of them the same flat 600 ms with the transport stopped, the first of
    // them ten minutes AFTER the fix was installed. So the window was on
    // screen and something else was imposing that period. One guess, confidently
    // reasoned, confidently shipped, and wrong - the same mistake as believing
    // the first variety metric instead of measuring what the ear reports.
    //
    // THE MEASURED DISCRIMINATOR IS THE AUDIO BLOCK COUNT, and it was in every
    // line of the log the whole time.
    //
    // audioBlocks increments on EVERY processBlock, transport running or not.
    // So zero blocks across a 600 ms gap does not mean "the band was stopped",
    // it means THE HOST NEVER CALLED THE PLUGIN AT ALL - it was sitting in a
    // rackspace that was not active, or in a host whose audio engine was off.
    // A plugin nobody is processing is not a plugin whose window froze.
    //
    // And it is the exact opposite of the fault this detector exists for, which
    // was reported as "the playhead is not moving but audio is still heard like
    // normal". That fault has audio blocks by definition. Every one of the real
    // stalls ever caught had them; not one of the 1,734 phantoms did.
    //
    // So the gate is now a fact rather than a theory. isShowing() is still
    // consulted, because a genuinely hidden window cannot show anybody a
    // freeze - but it is no longer load-bearing, and every line records what
    // both of these thought, so if this reasoning is wrong too the next log
    // says so instead of needing another round of guessing.
    // THE AUDIO SIDE OF THE SAME QUESTION. A timer gap only shows the message
    // thread; an audio stall with a healthy window would never appear above.
    // So once a second the audio the host asked for is set against the time
    // that passed, and a shortfall is written down with what Ghostband's own
    // processBlock cost over the same second. Zero audio means the host was
    // not running us at all, which is not a stall (see idleGaps).
    double       audioWindowStartMs  = 0.0;
    juce::uint64 audioWindowSamples  = 0;
    unsigned     audioWindowBlocks   = 0;

    // THE BUFFER THE HOST REALLY SENDS: samples over blocks, measured each
    // second. The host's announced size (getBlockSize) is only an upper bound
    // it gave at prepare time - on the owner's rig it said 512 while the
    // Focusrite ran at 1024. 0 until audio has been seen.
    int          measuredBlockSize   = 0;
    int          blockSizeForDisplay() const;
    void         noteMeasuredBlockSize (juce::uint64 samples, unsigned blocks);
    double       audioWindowWorkMs   = 0.0;
    double       audioWindowWorstMs  = 0.0;
    double       gapAudioWorkMs      = 0.0;    // the same figures, for the timer gap line
    double       gapAudioWorstMs     = 0.0;
    int          audioShortfallsLogged = 0;    // capped, so a stuck condition cannot fill the disc
    void checkAudioKeptUp (double now);

    unsigned idleGaps      = 0;      // gaps with no audio: the host was not running us
    double   idleGapMsTotal = 0.0;
    unsigned idleLogged    = 0;      // how many summary lines have been written

    // 33 ms is the expected spacing. A quarter of a second is seven frames
    // missed, which is past anything a person reads as smooth and well past
    // normal scheduling jitter, so it catches real stalls without filling the
    // log with noise from a busy moment.
    static constexpr double stallThresholdMs = 250.0;
    static constexpr size_t maxStallsKept    = 24;

    // Dial moves are debounced rather than regenerating on every pixel: the
    // audio thread try-locks the sequence, and swapping it sixty times a second
    // would cost dropped blocks for no musical benefit.
    void styleCombo (juce::ComboBox& c);
    void updateRollButtonText();

    bool     dialsDirty       = false;
    juce::uint32 lastDialMove = 0;
    juce::String lastLatencyText;

    int      lastPlayheadTick = -2;
    int      lastQueued       = -2;

    // What the play/pause button is currently SAYING, so it can be corrected
    // when it stops matching what is true. -1 means "nothing shown yet".
    int      lastPausedShown  = -1;

    // resized() runs during construction, before setSize has been called, when
    // the editor is still zero by zero. Without this guard it stored that zero
    // over the remembered size, which was then read back and clamped to the
    // minimum - so the window opened small every time however it was left.
    bool     sizeInitialised  = false;
    std::vector<int> rerollSelection;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GhostbandEditor)
};
