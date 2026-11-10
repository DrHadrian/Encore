// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "session/ShowSession.h"
#include "ui/TunerDisplay.h"

namespace encore
{

/** What the stage view shows that isn't in the show itself, gathered once a
    frame by the main window (meters are taken from the engine only once). */
struct StageSnapshot
{
    float inputPeak = 0.0f, outputPeak = 0.0f;
    double cpu = 0.0;
    int overruns = 0;
    bool onBattery = false;
    Tuner::Reading tuner;
};

/** The performance screen: widgets placed on a 24 x 16 grid by the show's
    layouts (see StageLayout). Each cue can pick its own layout.

    Edit layout mode: drag widgets to move them, drag a corner or edge to
    resize (snapping to the grid), right-click for options, "+ Widget" to add.
    Locked: the mouse and keyboard change nothing except the lock itself
    (hold it for a second) and the panic button; footswitches keep working.
*/
class StageView final : public juce::Component
{
public:
    StageView (AudioEngine&, ShowSession&);
    ~StageView() override;

    /** Called by the main window's timer. */
    void update (const StageSnapshot&);

    void setLocked (bool);
    bool isLocked() const noexcept { return locked; }

    void setEditingLayout (bool);
    bool isEditingLayout() const noexcept { return editing; }

    /** The main window shows the edit view again. */
    std::function<void()> onLeave;

    void paint (juce::Graphics&) override;
    void resized() override;

    static const juce::Colour background, accent;

private:
    class Widget;
    class Editor;

    void rebuildWidgets();
    void layoutWidgets();
    void showLayoutMenu();
    void showAddWidgetMenu();
    void commitWidgets (std::vector<StageWidget> widgets);
    juce::Rectangle<int> getGridArea() const;
    juce::Rectangle<int> cellBounds (const StageWidget&) const;

    void beginUnlockPress();
    void endUnlockPress();
    juce::String unlockProgressText() const;

    const StageLayout& shownLayout() const;
    bool layoutHasTuner() const;

    AudioEngine& engine;
    ShowSession& session;

    bool locked = false, editing = false;
    juce::String editingLayoutId;   // in edit mode, the layout being edited
    StageLayout current;            // what's on screen

    juce::OwnedArray<Widget> widgets;
    std::unique_ptr<Editor> editor;
    TunerDisplay tunerOverlay;

    juce::TextButton editViewButton { "Edit view" }, editLayoutButton { "Edit layout" }, lockStripButton { "Lock" };
    juce::TextButton layoutsButton, addWidgetButton { "+ Widget" }, doneButton { "Done" };
    juce::Label hint;

    StageSnapshot last;
    int lastOverruns = 0;
    juce::uint32 lastOverrunTime = 0, unlockPressStart = 0;
    bool recentOverrun = false;
    juce::Point<int> addWidgetCell;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageView)
};

} // namespace encore
