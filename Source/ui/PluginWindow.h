// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/blocks/PluginBlock.h"
#include "ui/kit/ParameterList.h"

namespace encore
{

/** A plugin's own controls in a window of their own. The plugin draws the
    inside; Encore only gives it a native window (its title is the plugin's
    and the patch's name). If the plugin has no editor, its parameters are
    shown instead (kit::ParameterList).

    The window must close before its plugin does (PluginBlock::setBeforeClosing).
    After the plugin crashed, closing it doesn't touch the plugin again: the
    editor is left in memory, like the plugin. Message thread only. */
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (PluginBlock& block, const juce::String& patchName, std::function<void()> onClose);
    ~PluginWindow() override;

    PluginBlock& getBlock() const noexcept { return block; }

    /** From the UI timer: a parameter list shows values changed elsewhere. */
    void refresh();

    /** What the window shows (the plugin's editor, or the parameter list). */
    juce::Component* getShownContent() const noexcept { return editor != nullptr ? static_cast<juce::Component*> (editor.get()) : parameterList.get(); }

    void closeButtonPressed() override;

private:
    PluginBlock& block;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<kit::ParameterList> parameterList;   // when it has no editor
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

} // namespace encore
