// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/AmpPreset.h"
#include "ui/kit/Chip.h"
#include "ui/kit/PopupField.h"

namespace encore
{

class ShowSession;
class AmpLibrary;

/** The amp builder (design/AMP-BUILDER.md, board BAmpBuilder): a full-area
    view in Edit (F8) where an amp's face is built from parts and saved, with
    its NAM capture, as an amp of the show (and a copy in this computer's
    library). Parts | Face (600 × 430 at 1:1) | Inspector.

    - Parts are dragged onto the face (a drop box shows where), or added at
      its centre with a click or Enter.
    - On the face: drag to move (2 px grid), smart guides (centres within
      6 px line up, magenta), equal spacing in a row, shift/ctrl-click to
      select several, Arrange (align tops / middles / bottoms, space evenly,
      same size).
    - Keyboard: Tab / Shift+Tab through the parts, arrows nudge 2 px (Shift
      10), Delete removes, Ctrl+D duplicates, Ctrl+Z / Ctrl+Y undo / redo,
      Esc clears the selection.
    - Save amp is blocked while a knob is NOT SET (its tooltip says why).

    The face is drawn from a composed image (kit::AmpFace), made again only
    when the amp changes; while a part is dragged, a face without it is
    shown with the part on top. Message thread. */
class AmpBuilderView final : public juce::Component
{
public:
    AmpBuilderView (ShowSession&, AmpLibrary&);
    ~AmpBuilderView() override;

    /** Starts editing an amp (a copy; nothing changes until Save amp). */
    void edit (const AmpPreset&);
    const AmpPreset& getAmp() const noexcept { return amp; }
    bool hasUnsavedChanges() const noexcept { return dirty; }

    /** Back to patches (F8). */
    std::function<void()> onClose;

    /** After Save amp: the amp as saved (MainComponent gives it to the amp block being built). */
    std::function<void (const AmpPreset&)> onSaved;

    /** Load capture…: the existing NAM chooser and loader (MainComponent: into the amp block). */
    std::function<void (std::function<void (const juce::File&, const juce::String& formatLine)> done)> onLoadCapture;

    /** The capture's format line ("48.0 kHz · standard · ~12% CPU"), from the loaded model. */
    void setCaptureInfo (const juce::String& formatLine);

    //== The model, as the UI uses it (and tests) ===============================
    /** A new part of a type ("knob", "toggle"...; style: a knob's, "push", "led"), centred on a face point. */
    juce::String addPart (const juce::String& type, const juce::String& style, juce::Point<int> centre);

    /** Where a part lands with its top left wanted at `topLeft`: the 2 px grid,
        inside the face, then the smart guides and equal spacing. */
    struct Guide { juce::Rectangle<int> line; };
    struct GapMark { juce::Rectangle<int> line; int px; };
    struct Landing
    {
        juce::Point<int> topLeft;
        std::vector<Guide> guides;
        std::vector<GapMark> gaps;
    };

    Landing landingFor (const AmpPart&, juce::Point<int> topLeft) const;

    void movePart (const juce::String& partId, juce::Point<int> topLeft);   // through landingFor
    void select (const juce::String& partId, bool addOrToggle = false);       // "" = none (the amp)
    const juce::StringArray& getSelection() const noexcept { return selection; }

    enum class Arrange { tops, middles, bottoms, spaceEvenly, sameSize };
    void arrange (Arrange);

    void changeSelected (const std::function<void (AmpPart&)>&);   // one undo step
    void changeLook (const std::function<void (AmpLook&)>&);
    void applyTemplate (const juce::String& key);                    // look and parts (undoable)

    void nudge (int dx, int dy);
    void removeSelected();
    void duplicateSelected();
    void selectNext (bool backwards);

    void undo();
    void redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }

    /** Save amp: to the show and the library. False (nothing saved) while a knob is NOT SET. */
    bool save();

    /** The footer's line: "12 parts · 6 turnable knobs · 1 fixed · template: British gold". */
    juce::String countText() const;

    /** "Add from the library...": this computer's amps not in the show, then
        the example amps (Encore Gold 30, JC-120, Super Crush 100, Blues
        Junior, 5150 III 50W) not in either. */
    std::vector<AmpPreset> libraryChoices() const;
    juce::String usedByText() const;

    /** The drag from the parts list (tests): a point on the face, and whether it was let go there. */
    void dragNewPart (const juce::String& type, const juce::String& style, juce::Point<int> facePoint, bool released);
    const juce::Component* getDropBox() const noexcept;
    juce::Component* getFace() const noexcept;
    juce::Component* getInspector() const noexcept;
    bool showSwatchFocus (const juce::String& rowLabel, int index);   // test hook: the focus ring on a swatch (off-screen)

    juce::Button& getSaveButton() noexcept { return saveButton; }

    bool keyPressed (const juce::KeyPress&) override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class PartsList;
    class FaceCanvas;
    class Inspector;

    void changed (AmpPreset before);   // after an edit: the undo step, the face, the inspector
    void refreshAll();
    void refreshToolbar();
    void showAmpMenu();
    void loadCapture();
    juce::String newPartId();
    juce::String templateKey() const;

    ShowSession& session;
    AmpLibrary& library;
    AmpPreset amp;
    juce::StringArray selection;
    std::vector<AmpPreset> undoStack, redoStack;
    bool dirty = false;
    int nextId = 1;
    juce::String captureInfo;

    juce::Rectangle<int> toolbarArea, titleArea, usedByArea;
    kit::PopupField ampPicker;
    struct TipHolder final : public juce::Component, public juce::SettableTooltipClient
    {
        kit::Chip chip { kit::ChipStyle::off };
        TipHolder() { addAndMakeVisible (chip); }
        void resized() override { chip.setBounds (getLocalBounds()); }
    } savedChip;
    juce::TextButton undoButton { "Undo" }, saveButton { "Save amp" }, backButton { "Back to patches" };

    std::unique_ptr<PartsList> parts;
    juce::Viewport partsViewport;   // the list scrolls in a short window
    std::unique_ptr<FaceCanvas> face;
    std::unique_ptr<Inspector> inspector;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpBuilderView)
};

} // namespace encore
