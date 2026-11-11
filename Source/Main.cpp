// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AppSettings.h"
#include "app/EventLog.h"
#include "app/MainComponent.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/EncoreTheme.h"
#include "engine/core/Realtime.h"
#include "session/PluginCatalog.h"

namespace encore
{

/** Scrolls the main component vertically when the window is shorter than its content. */
class ScrollingContent final : public juce::Viewport
{
public:
    explicit ScrollingContent (AppSettings& settings)
        : content (std::make_unique<MainComponent> (settings))
    {
        setViewedComponent (content.get(), false);
        setScrollBarsShown (true, true);
        setScrollBarThickness (theme::size::scrollbar);

        content->onViewChanged = [this]
        {
            // The stage view fills the window exactly; the edit view scrolls.
            const auto stage = content->isShowingStage();
            setScrollBarsShown (! stage, ! stage);
            setViewPosition (0, 0);
            resized();
        };
    }

    void resized() override
    {
        Viewport::resized();

        if (content->isShowingStage())
        {
            content->setSize (getWidth(), getHeight());
            return;
        }

        // Below the minimum the edit view keeps its size and scrolls.
        const auto minimum = content->getMinimumEditSize();
        const auto w = juce::jmax (minimum.x, getMaximumVisibleWidth());
        const auto h = juce::jmax (minimum.y, getMaximumVisibleHeight());
        content->setSize (w, h);
    }

private:
    std::unique_ptr<MainComponent> content;
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, AppSettings& settings)
        : DocumentWindow (name, juce::Colour (theme::colour::window), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new ScrollingContent (settings), false);
        setResizable (true, true);
        setResizeLimits (640, 480, 8192, 8192);

        // Fit the screen we open on (laptops are often 768 px tall).
        auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();
        centreWithSize (juce::jmin (theme::size::referenceWidth, screen.getWidth() - theme::size::screenMargin),
                        juce::jmin (theme::size::referenceHeight, screen.getHeight() - theme::size::screenMargin));
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class EncoreApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    // Two copies would fight over the same ASIO driver, which most drivers
    // handle badly. A second launch just brings the first one to the front.
    // The plugin scanner (a copy of Encore that examines one plugin file) is
    // the exception: it opens no audio and no window.
    bool moreThanOneInstanceAllowed() override
    {
        return PluginCatalog::isScannerCommandLine (getCommandLineParameterArray());
    }

    void initialise (const juce::String&) override
    {
        if (PluginCatalog::isScannerCommandLine (getCommandLineParameterArray()))
        {
            setApplicationReturnValue (PluginCatalog::runScanner (getCommandLineParameterArray()));
            quit();
            return;
        }

        // Debug builds: count any allocation made on the audio thread.
        rt::installAllocationTripwire();

        // The look comes first and goes last: every window and menu uses it.
        lookAndFeel = std::make_unique<theme::EncoreLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        settings = std::make_unique<AppSettings>();
        eventlog::start (settings->getFolder().getChildFile ("Logs"), getApplicationVersion());

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *settings);
    }

    void shutdown() override
    {
        if (settings == nullptr)
            return;   // the plugin scanner: nothing was opened

        mainWindow = nullptr;   // closes the audio device first
        settings->save();
        settings = nullptr;
        eventlog::stop();

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
            mainWindow->toFront (true);
    }

private:
    std::unique_ptr<theme::EncoreLookAndFeel> lookAndFeel;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace encore

START_JUCE_APPLICATION (encore::EncoreApplication)
