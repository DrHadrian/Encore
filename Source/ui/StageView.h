// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "session/ShowSession.h"
#include "ui/TunerDisplay.h"
#include "ui/kit/Lcd.h"
#include "ui/kit/NoticeBar.h"
#include "ui/kit/Pane.h"
#include "ui/kit/SegmentedControl.h"
#include "ui/kit/Toolbar.h"

namespace encore
{

/** What the stage view shows that isn't in the show itself, gathered once a
    frame by the main window (meters are taken from the engine only once). */
struct StageSnapshot
{
    float inputPeak = 0.0f, outputPeak = 0.0f;
    std::array<float, 8> inputSlotPeaks {};                    // by input slot (routing::maxInputs)
    std::array<std::array<float, 2>, 8> outputSlotPeaks {};    // by output slot, left and right
    double cpu = 0.0;
    int overruns = 0;
    bool onBattery = false;
    Tuner::Reading tuner;
    juce::String audioProblem;   // e.g. "AUDIO INTERFACE LOST - reconnecting to <device>..."
    juce::String notice;         // shown along the top for a while (e.g. after a crash recovery)
    juce::String deviceLine;     // footer: "ASIO · 48 kHz · 64 samples · 5.3 ms round trip" (the lost text while lost)
    juce::String footswitch;     // footer: "Footswitch connected" / "No footswitch connected"
    double roundTripMs = 0.0;    // 0: no device
};

/** The performance screen (SCREENS.md §3): a toolbar, strips for a lost
    interface, a notice and the muted output, then widgets placed on a 24 x 16
    grid by the show's layouts (see StageLayout), and a footer. Each cue can
    pick its own layout.

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

    /** The Outputs widget's Mute / MUTED: the main window keeps the mute in
        this computer's routing, as the bottom row's M does. */
    std::function<void (const juce::String& output, bool muted)> onMuteOutput;

    /** The layout editor's widget inspector (SCREENS.md §6): the Inspector
        toggle (key I). The main window remembers it per computer. */
    void setLayoutInspectorShown (bool);
    bool isLayoutInspectorShown() const noexcept { return inspectorShown; }
    std::function<void (bool shown)> onLayoutInspectorChanged;

    /** Layout editor: selects a widget by its position in the layout (-1: none). */
    void selectLayoutWidget (int index);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Widget;
    class Editor;
    class MiniMeters;
    class Caption;
    class WidgetPanel;
    class Artboard;
    class LayoutInspector;

    void rebuildWidgets();
    void layoutWidgets();
    void layoutToolbar();
    void showLayoutMenu();
    void showAddWidgetMenu();
    void commitWidgets (std::vector<StageWidget> widgets);
    juce::Rectangle<int> getGridArea() const;
    int stripHeight (const kit::NoticeBar&) const;
    juce::Rectangle<int> cellBounds (const StageWidget&) const;
    float getContentScale() const noexcept { return contentScale; }
    float getChromeScale() const noexcept;
    int getGap() const noexcept;
    juce::Component& widgetParent();
    void layoutEditor (juce::Rectangle<int> body);
    void addWidgetAtFreeCell (StageWidgetType);
    juce::Point<int> nextFreeCell() const;
    void selectionChanged();

    void beginUnlockPress();
    void endUnlockPress();
    juce::String unlockProgressText() const;

    const StageLayout& shownLayout() const;
    bool layoutHas (StageWidgetType) const;
    void refreshToolbar();
    void refreshStrips (const StageSnapshot&);
    void refreshFooter (const StageSnapshot&);

    AudioEngine& engine;
    ShowSession& session;

    bool locked = false, editing = false;
    juce::String editingLayoutId;   // in edit mode, the layout being edited
    StageLayout current;            // what's on screen
    float contentScale = 1.0f;      // widget content: the window against the reference size

    juce::OwnedArray<Widget> widgets;
    std::unique_ptr<Editor> editor;
    TunerDisplay tunerOverlay;

    // Toolbar (SCREENS.md §3.1)
    kit::Toolbar toolbar;
    kit::SegmentedControl modeSwitch;
    std::unique_ptr<Caption> layoutName, hint;
    std::unique_ptr<juce::Component> separator;
    kit::Lcd lcd { true };
    std::unique_ptr<MiniMeters> miniMeters;
    juce::TextButton lockButton, tunerButton { "Tuner" }, muteButton { "Mute" };
    juce::TextButton layoutsButton, addWidgetButton { "+ Widget" }, doneButton { "Done" }, inspectorButton { "Inspector" };

    // Layout editor (§6): widget list, the artboard with the stage preview, the widget inspector.
    kit::Pane widgetPane, canvasPane, inspectorPane;
    std::unique_ptr<WidgetPanel> widgetPanel;
    std::unique_ptr<Artboard> artboard;
    std::unique_ptr<LayoutInspector> layoutInspector;
    bool inspectorShown = true;

    // Strips under the toolbar (§3.2)
    kit::NoticeBar lostBar { kit::NoticeBar::Kind::lost }, noticeBar { kit::NoticeBar::Kind::stageNotice },
                   mutedBar { kit::NoticeBar::Kind::muted };

    // Footer (§3.4): drawn by paint(), placed by refreshFooter().
    juce::String footerDevice, footerFootswitch, footerHelp, stageHelp;
    bool footerAlert = false;
    juce::Rectangle<float> footerDeviceBox, footerFootswitchBox, footerHelpBox;

    StageSnapshot last;
    int lastOverruns = 0;
    juce::uint32 lastOverrunTime = 0, unlockPressStart = 0;
    bool recentOverrun = false;
    juce::Point<int> addWidgetCell;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageView)
};

} // namespace encore
