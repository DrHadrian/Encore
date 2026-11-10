// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "engine/core/Block.h"

namespace encore
{

/** The editor for one block: name, on/off switch, CPU use, a file row for
    blocks that load one (amp captures, IRs), and a control per parameter,
    built automatically from the block's parameter list (on/off parameters
    become switches). The same panel becomes the parameter area of the edit
    view in milestone 4.

    Message thread only. It reads and writes the block's atomics, so it never
    waits on the audio thread.
*/
class BlockPanel final : public juce::Component
{
public:
    explicit BlockPanel (Block& blockToEdit);
    ~BlockPanel() override;

    /** Optional activity light (e.g. "gate open"). */
    void setStatusLight (juce::String label, std::function<bool()> isLit);

    /** Called after any change made in this panel (knob, switch, on/off, file). */
    std::function<void()> onEdited;

    /** Where the file chooser opens, and what to do after a successful load. */
    std::function<juce::File()> getBrowseFolder;
    std::function<void (const juce::File&)> onFileLoaded;

    /** Loads a file into the block as if picked in the chooser. */
    void loadFile (const juce::File& file);

    /** Call from a UI timer: syncs controls changed elsewhere (MIDI later), CPU and file info. */
    void refresh();

    int getPreferredWidth() const;
    int getPreferredHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Control
    {
        explicit Control (Parameter& p) : parameter (p) {}

        Parameter& parameter;
        bool isSwitch = false;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::TextButton switchButton;
        juce::Label label;
    };

    void chooseFile();
    void setLoading (bool isLoading);

    Block& block;
    juce::Label title, cpu;
    juce::TextButton power { "ON" };
    std::vector<std::unique_ptr<Control>> controls;

    // File row (only for blocks with a FileSlot).
    juce::TextButton loadButton;
    juce::Label fileName, fileInfo;
    std::unique_ptr<juce::FileChooser> chooser;
    bool loading = false;
    juce::String shownFile, shownInfo;

    juce::String statusLabel;
    std::function<bool()> statusLit;
    bool statusWasLit = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (BlockPanel)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockPanel)
};

} // namespace encore
