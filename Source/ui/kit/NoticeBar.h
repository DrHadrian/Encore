// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A strip under a toolbar (DESIGN-SYSTEM.md §5.10):
      notice       amber, "!" badge (Edit notices: crash restart, go to...)
      stageNotice  amber, big text (Stage: go-to prompt, reconnected...)
      lost         red, spinner (audio interface lost)
      muted        red bar, the whole bar is a button ("Output muted - click to go live")
    Text changes repaint; the spinner turns when the owner calls advance()
    from its existing timer. */
class NoticeBar final : public juce::Component
{
public:
    enum class Kind { notice, stageNotice, lost, muted };

    explicit NoticeBar (Kind);

    void setKind (Kind);
    void setText (const juce::String& text, const juce::String& rightNote = {});
    const juce::String& getText() const noexcept { return text; }

    /** Lost banners: turn the spinner a step (call from the UI timer). */
    void advance();

    std::function<void()> onClick;   // muted bar

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void update();   // note width, spinner shape and fill, when the text, kind or size changes

    Kind kind;
    juce::String text, rightNote;
    float noteWidth = 0.0f;
    juce::Path spinnerArc;
    juce::Rectangle<float> spinnerBox;
    juce::ColourGradient fill;
    static constexpr int spinnerSteps = 30;   // one turn a second at the 30 Hz UI timer
    int spinnerStep = 0;
};

} // namespace encore::kit
