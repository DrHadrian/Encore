// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "session/ShowSession.h"
#include "ui/StageView.h"
#include "ui/kit/InfoList.h"
#include "ui/kit/Pane.h"
#include "ui/kit/StageBackdrop.h"

namespace encore
{

/** The block stage while the chain's IN or OUT tile is selected
    (NEW-FEATURES-M10-M11 §B.3):

    - IN: the patch's input, as cards, one per input of this computer (and
      the patch's own name first when this computer hasn't it: SAVED · NOT
      HERE, the one playing instead marked); a click picks it for the patch.
    - OUT: the patch's sends, a strip per output name (this computer's,
      then the patch's that aren't here, dimmed): name and channels or a
      chip, On, a fader on the send law beside the output's meters, the
      level (double-click to type). Sends live at once, no rebuild.

    It also gives the inspector its sections. Message thread only; refresh()
    and update() come from the main window's timer. */
class JackStage final : public kit::Pane
{
public:
    enum class Kind { input, output };

    JackStage (Kind, AudioEngine&, ShowSession&, juce::AudioDeviceManager&);
    ~JackStage() override;

    Kind getKind() const noexcept { return kind; }

    std::function<void()> onOpenRouting;

    /** Rebuilds what changed (the patch, its input or sends, the routing);
        true when it did (the inspector's sections changed too). */
    bool refresh();

    /** Meters (UI timer). */
    void update (const StageSnapshot&);

    /** What the inspector shows for this tile. */
    std::vector<kit::InfoList::Section> getInspectorSections() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SendStrip;
    class InputCard;

    void rebuild();
    juce::String patchName() const;

    const Kind kind;
    AudioEngine& engine;
    ShowSession& session;
    juce::AudioDeviceManager& deviceManager;

    kit::StageBackdrop backdrop;
    juce::TextButton routingButton { "Routing..." };
    std::vector<std::unique_ptr<SendStrip>> strips;
    std::vector<std::unique_ptr<InputCard>> cards;
    juce::Rectangle<int> box, warning;
    /** The line under the OUT box: items between dots, one word in each may be strong. */
    struct FooterItem { juce::String lead, strong, tail; };
    std::vector<FooterItem> footer;
    juce::String warningLead, warningRest, intro, rightNote;
    juce::int64 lastState = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JackStage)
};

} // namespace encore
