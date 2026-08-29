#pragma once

#include "PluginProcessor.h"

#include "GhostbandLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ghost
{
    // The palette lives in GhostbandLookAndFeel.h. These are the names the rest
    // of the editor already used, kept so the drawing code reads the same.
    const juce::Colour background = colours::background;
    const juce::Colour panel      = colours::panel;
    const juce::Colour line       = colours::line;
    const juce::Colour text       = colours::text;
    const juce::Colour dim        = colours::dim;
    const juce::Colour accent     = colours::accent;
    const juce::Colour warn       = colours::warn;
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

    static constexpr int rowHeight = 36;

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

    static constexpr int rowHeight = 26;

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
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    std::function<void (int)> onRowClicked;

    static constexpr int rowHeight = 26;

private:
    std::vector<Row> rows;
    int selected = 0;
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
    static constexpr int numScreens = 5;

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
    juce::Label  complexityLabel;
    juce::Label  humanizeLabel;

    // Per-part level, sent as MIDI CC 7. Mixing normally belongs in the host,
    // but four knobs here saves wiring four gain blocks in the rackspace and is
    // the fastest way to hear a part that is buried.
    juce::Slider levelDrums, levelBass, levelGuitar, levelPiano;
    juce::Label  levelDrumsLabel, levelBassLabel, levelGuitarLabel, levelPianoLabel;
    juce::Label  mixLabel;

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

    // Three screens share the window: the normal song view, the calibration
    // view, and the structure editor.
    enum class Screen { Song, Calibrate, Edit, Settings, About };
    Screen screen = Screen::Song;

    void paintAbout (juce::Graphics& g, juce::Rectangle<int> area);

    // Header navigation, visible on every screen.
    juce::TextButton settingsButton { "Settings" };
    juce::TextButton aboutButton    { "About" };
    juce::TextButton backButton     { "Back" };

    // Settings
    juce::ComboBox chDrums, chBass, chGuitar, chPiano;
    juce::Label    chDrumsLabel, chBassLabel, chGuitarLabel, chPianoLabel;
    juce::TextButton testDrums { "Test" }, testBass { "Test" },
                     testGuitar { "Test" }, testPiano { "Test" };
    // MIDI Learn helpers: sweep a CC at an instrument so it can latch onto it.
    void refreshControls();
    void pushControlEdit();

    juce::ComboBox   learnPart;
    juce::TextButton ctlAdd    { "+ Add" };
    juce::TextButton ctlRemove { "Remove" };
    juce::TextButton ctlTeach  { "Teach this control" };
    juce::TextButton ctlSave   { "Save mappings" };
    juce::TextEditor ctlName;
    juce::ComboBox   ctlFollows, ctlType, ctlPositions;
    juce::Label      learnHeading, learnHelp, ctlNameLabel, ctlFollowsLabel,
                     ctlTypeLabel, ctlPositionsLabel;
    juce::Viewport   ctlViewport;
    ControlList      ctlList;
    int              ctlSelected = 0;
    bool             suppressControlCallbacks = false;

    juce::Label    settingsHeading, channelsHelp;
    juce::TextButton resetSizeButton   { "Reset window size" };
    juce::TextButton reloadProfilesBtn { "Reload driver profiles" };

    // About
    juce::TextButton manualButton { "User manual" };
    juce::TextButton repoButton   { "Source code" };

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
                       edGuitar { "guitar" }, edPiano { "piano" };
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

    // Dial moves are debounced rather than regenerating on every pixel: the
    // audio thread try-locks the sequence, and swapping it sixty times a second
    // would cost dropped blocks for no musical benefit.
    void styleCombo (juce::ComboBox& c);
    void updateRollButtonText();

    bool     dialsDirty       = false;
    juce::uint32 lastDialMove = 0;
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
