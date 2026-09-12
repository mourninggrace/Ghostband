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

private:
    std::vector<Row> rows;
    int selected = 0;
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

    // Filled by the editor's timer from the processor, because the view must
    // not reach into the audio thread's sequence itself.
    void setCells (std::vector<GhostbandProcessor::TrackerCell> c, int firstTick,
                   int beat, int barTicks);

    int  visibleRows() const;
    int  firstTickWanted() const { return firstRowTick; }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override;

    std::function<void (int)> onSectionClicked;
    std::function<void (int)> onSectionToggled;

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

    int firstRowTick = 0;
    int beatTicks    = 96;
    int barTicks     = 384;
    int playheadTick = -1;
    int playheadRow  = -1;
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

    void pressRollForTesting();
    int  rerollSelectionSizeForTesting() const;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshFromProcessor();
    void layOutFooter (juce::Rectangle<int> area);
    void markDialsDirty();
    void styleButton (juce::TextButton& b, bool primary);
    void styleSlider (juce::Slider& s);

    GhostbandProcessor& processor;

    juce::TextButton loadButton   { "Load plan..." };
    juce::TextButton reloadButton { "Reload" };
    juce::TextButton rollButton   { "Roll" };

    // Ghostband's own transport. The host's is usually left running for a whole
    // session, so stopping the band and stopping the host are different things.
    juce::TextButton playPauseButton { "Pause" };

    juce::ComboBox keyBox;
    juce::ComboBox modeBox;
    juce::ComboBox styleBox;
    juce::ComboBox tuningBox;
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

    juce::Label  complexityLabel;
    juce::Label  humanizeLabel;
    juce::Label  fillsLabel;

    // Per-part level, sent as MIDI CC 7. Mixing normally belongs in the host,
    // but four knobs here saves wiring four gain blocks in the rackspace and is
    // the fastest way to hear a part that is buried.
    juce::Slider levelDrums, levelBass, levelGuitar, levelGuitar2, levelPiano;
    juce::Label  levelDrumsLabel, levelBassLabel, levelGuitarLabel,
                 levelGuitar2Label, levelPianoLabel;
    juce::Label  mixLabel;

    juce::TextEditor seedEditor;
    juce::Label      seedLabel;

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
    int  lastTrackerTick = -1;
    int  lastTrackerRows = -1;

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

    // resized() runs during construction, before setSize has been called, when
    // the editor is still zero by zero. Without this guard it stored that zero
    // over the remembered size, which was then read back and clamped to the
    // minimum - so the window opened small every time however it was left.
    bool     sizeInitialised  = false;
    std::vector<int> rerollSelection;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GhostbandEditor)
};
