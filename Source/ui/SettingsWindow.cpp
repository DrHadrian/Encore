// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/SettingsWindow.h"

#include "ui/theme/EncoreTheme.h"

namespace encore
{

using namespace theme;

/** The dialog's body: the shown tab in a viewport (small windows scroll). */
class SettingsWindow::Body final : public juce::Component
{
public:
    Body (juce::Component& a, juce::Component& m)
    {
        for (auto* v : { &audioView, &midiView })
        {
            v->setScrollBarsShown (true, false);
            addChildComponent (v);
        }

        audioView.setViewedComponent (&a, false);
        midiView.setViewedComponent (&m, false);
    }

    void show (bool audio)
    {
        audioView.setVisible (audio);
        midiView.setVisible (! audio);
        resized();
    }

    std::function<int (juce::Component&)> idealHeight;

    void resized() override
    {
        for (auto* v : { &audioView, &midiView })
        {
            v->setBounds (getLocalBounds());

            if (auto* content = v->getViewedComponent(); content != nullptr && idealHeight != nullptr)
                content->setSize (getWidth() - (v->canScrollVertically() ? v->getScrollBarThickness() : 0),
                                  juce::jmax (getHeight(), idealHeight (*content)));
        }
    }

private:
    juce::Viewport audioView, midiView;
};

SettingsWindow::SettingsWindow (juce::AudioDeviceManager& dm, const system::TuningReport& tuning, std::function<void()> resetCounters,
                                MidiRouter& router, AppSettings& settings, ShowSession& session,
                                std::function<void()> applyBindings, std::function<void()> applyOutput, std::function<void()> close)
    : frame ("Settings", "Close settings"),
      audio (dm, tuning, std::move (resetCounters)),
      midi (dm, router, settings, session, std::move (applyBindings), std::move (applyOutput))
{
    body = std::make_unique<Body> (audio, midi);
    body->idealHeight = [this] (juce::Component& c) { return &c == &audio ? audio.getIdealHeight() : midi.getIdealHeight(); };

    frame.setTabs ({ { "Audio", Icon::audioWave }, { "MIDI", Icon::midiPort } });
    frame.onTabChange = [this] (int index) { showTab (index == 0 ? Tab::audio : Tab::midi); };
    frame.setContent (body.get());

    // Changes apply immediately: both buttons close.
    frame.addFooterButton ("Cancel").onClick = close;
    frame.addFooterButton ("Done", Variant::primary).onClick = close;
    frame.onClose = std::move (close);

    overlay = std::make_unique<kit::DialogOverlay> (frame, juce::Rectangle<int> (size::settingsW + 2, size::settingsH + 2));
    showTab (Tab::audio);
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::showTab (Tab t)
{
    tab = t;
    const auto isAudio = tab == Tab::audio;
    frame.setSelectedTab (isAudio ? 0 : 1);
    frame.setDialogTitle ("Settings " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 ")) + (isAudio ? "Audio" : "MIDI"));
    frame.setFooterText (isAudio ? juce::String ("Changes apply immediately; audio restarts for less than a second when you change the device.")
                                 : juce::String (juce::CharPointer_UTF8 ("Mappings save automatically to this computer (%APPDATA%\\Encore), "
                                                                        "never into the show \xe2\x80\x94 unless you choose \xe2\x80\x9cThis show\xe2\x80\x9d.")));
    body->show (isAudio);
}

void SettingsWindow::setAudioStatus (const AudioSettingsView::Status& status)
{
    audio.setStatus (status);
}

} // namespace encore
