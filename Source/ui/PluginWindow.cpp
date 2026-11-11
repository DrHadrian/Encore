// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/PluginWindow.h"

#include "ui/theme/EncoreTheme.h"

namespace encore
{

using namespace theme;

PluginWindow::PluginWindow (PluginBlock& b, const juce::String& patchName, std::function<void()> close)
    : DocumentWindow (b.getName() + (patchName.isNotEmpty() ? juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x94 ")) + patchName : juce::String()),
                      juce::Colour (colour::window), DocumentWindow::closeButton),
      block (b), onClose (std::move (close))
{
    setUsingNativeTitleBar (true);

    if (auto* plugin = block.getInstance())
    {
        editor.reset (plugin->hasEditor() ? plugin->createEditorAndMakeActive() : nullptr);

        if (editor == nullptr)
            editor = std::make_unique<juce::GenericAudioProcessorEditor> (*plugin);

        setContentNonOwned (editor.get(), true);
        setResizable (editor->isResizable(), false);
    }
}

PluginWindow::~PluginWindow()
{
    clearContentComponent();

    // A crashed plugin is never called again, not even its editor's destructor.
    if (block.getFault() == PluginBlock::Fault::crashed)
        juce::ignoreUnused (editor.release());

    editor = nullptr;
}

void PluginWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}

} // namespace encore
