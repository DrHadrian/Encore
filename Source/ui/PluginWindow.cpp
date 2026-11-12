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
        // Asking hasEditor() first would make the plugin build its window
        // twice (crashing some plugins): create it once, fall back if none.
        editor.reset (plugin->createEditorAndMakeActive());

        if (editor != nullptr)
        {
            setContentNonOwned (editor.get(), true);
            setResizable (editor->isResizable(), false);
        }
        else
        {
            const auto& ref = block.getPluginRef();
            parameterList = std::make_unique<kit::ParameterList> (*plugin, ref.format, ref.name.isNotEmpty() ? ref.name : block.getName());
            setContentNonOwned (parameterList.get(), true);
            setResizable (false, false);
        }
    }
}

void PluginWindow::refresh()
{
    if (parameterList != nullptr && block.getFault() != PluginBlock::Fault::crashed)
        parameterList->refresh();
}

PluginWindow::~PluginWindow()
{
    clearContentComponent();
    parameterList = nullptr;

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
