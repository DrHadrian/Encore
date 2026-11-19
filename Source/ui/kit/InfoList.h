// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "ui/theme/Icons.h"

namespace encore::kit
{

/** Read-only sections for the inspector (the IN and OUT tiles, §B.3): a
    subheader ("SENDS" + "this patch") over key / value rows (the value can
    have a muted tail, an amber colour or a chip), then an optional note and
    one button (with a key cap). Paints only, except the button. */
class InfoList final : public juce::Component
{
public:
    struct Row
    {
        juce::String key, value, tail;   // tail: muted, after the value ("· input 1")
        bool amber = false;
        juce::String chip;               // MUTED (red chip), in capitals
        bool operator== (const Row&) const = default;
    };

    struct Section
    {
        juce::String title, right;
        std::vector<Row> rows;
        juce::String note;
        juce::String button, buttonKey;
        std::optional<theme::Icon> buttonIcon;   // before the button's text
        bool operator== (const Section&) const = default;
    };

    InfoList();

    void setSections (const std::vector<Section>&);
    std::function<void (int section)> onButton;

    /** Its height for a width (sections, rows, notes, the button). */
    int getIdealHeight (int width) const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Layout;
    std::vector<Section> sections;
    juce::OwnedArray<juce::TextButton> buttons;
};

} // namespace encore::kit
