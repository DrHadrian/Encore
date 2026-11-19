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
    explicit Body (std::initializer_list<juce::Component*> tabs)
    {
        for (auto* page : tabs)
        {
            auto* v = views.add (new juce::Viewport());
            v->setScrollBarsShown (true, false);
            v->setViewedComponent (page, false);
            addChildComponent (v);
        }
    }

    void show (int index)
    {
        for (int i = 0; i < views.size(); ++i)
            views[i]->setVisible (i == index);

        resized();
    }

    std::function<int (juce::Component&)> idealHeight;

    void resized() override
    {
        for (auto* v : views)
        {
            v->setBounds (getLocalBounds());

            if (auto* content = v->getViewedComponent(); content != nullptr && idealHeight != nullptr)
                content->setSize (getWidth() - (v->canScrollVertically() ? v->getScrollBarThickness() : 0),
                                  juce::jmax (getHeight(), idealHeight (*content)));
        }
    }

private:
    juce::OwnedArray<juce::Viewport> views;
};

SettingsWindow::SettingsWindow (juce::AudioDeviceManager& dm, const system::TuningReport& tuning, std::function<void()> resetCounters,
                                MidiRouter& router, AppSettings& settings, ShowSession& session, PluginCatalog& catalog,
                                std::function<void()> applyBindings, std::function<void()> applyOutput, std::function<void()> close)
    : frame ("Settings", "Close settings"),
      audio (dm, tuning, std::move (resetCounters)),
      midi (dm, router, settings, session, std::move (applyBindings), std::move (applyOutput)),
      plugins (catalog)
{
    body = std::make_unique<Body> (std::initializer_list<juce::Component*> { &audio, &midi, &plugins });
    body->idealHeight = [this] (juce::Component& c)
    {
        return &c == &audio ? audio.getIdealHeight() : &c == &midi ? midi.getIdealHeight() : plugins.getIdealHeight();
    };

    frame.setTabs ({ { "Audio", Icon::audioWave }, { "MIDI", Icon::midiPort }, { "Plugins", Icon::plug } });
    frame.onTabChange = [this] (int index) { showTab ((Tab) index); };
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
    static const juce::String names[] { "Audio", "MIDI", "Plugins" };
    static const juce::String footers[] {
        "Changes apply immediately; audio restarts for less than a second when you change the device.",
        juce::String (juce::CharPointer_UTF8 ("Mappings save automatically to this computer (%APPDATA%\\Encore), "
                                             "never into the show \xe2\x80\x94 unless you choose \xe2\x80\x9cThis show\xe2\x80\x9d.")),
        "Plugins belong to this computer. Each new one is tried in a separate process first, so a crash can't take Encore down." };

    const auto index = (int) tab;
    frame.setSelectedTab (index);
    frame.setDialogTitle ("Settings " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 ")) + names[index]);
    frame.setFooterText (footers[index]);
    body->show (index);
}

void SettingsWindow::setAudioStatus (const AudioSettingsView::Status& status)
{
    audio.setStatus (status);
}

} // namespace encore
