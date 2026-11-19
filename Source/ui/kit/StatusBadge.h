// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** Pre-show check badges (DESIGN-SYSTEM.md §5.12): a red ✕, an amber !,
    a green ✓, 22 px, drawn in a row by paintStatusBadge(). */
enum class Status { ok, warning, problem };

void paintStatusBadge (juce::Graphics&, juce::Rectangle<float> bounds, Status);

/** The 44 px summary badge: a lit dome with a soft glow. The glow is
    rendered once into an image (at the screen's scale) when the status or
    the scale changes, never per frame. Give it 44 px plus the glow on every
    side: getIdealSize(). */
class SummaryBadge final : public juce::Component
{
public:
    SummaryBadge();

    void setStatus (Status);
    Status getStatus() const noexcept { return status; }

    static int getIdealSize() noexcept;

    void paint (juce::Graphics&) override;

private:
    Status status = Status::ok;
    juce::Image glow;
    float glowScale = 0.0f;
    Status glowStatus = Status::ok;
};

} // namespace encore::kit
