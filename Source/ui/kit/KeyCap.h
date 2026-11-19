// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A key hint ("F6"): small grey text in a thin rounded outline. */
class KeyCap final : public juce::Component
{
public:
    explicit KeyCap (const juce::String& key);

    int getIdealWidth() const;
    static int widthFor (const juce::String& key);
    void paint (juce::Graphics&) override;

private:
    juce::String key;
};

/** The same, drawn into any rectangle (for painted rows and buttons). */
void paintKeyCap (juce::Graphics&, juce::Rectangle<float>, const juce::String& key);

} // namespace encore::kit
